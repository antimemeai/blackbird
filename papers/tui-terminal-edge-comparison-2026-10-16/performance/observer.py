#!/usr/bin/env python3
"""Observe the owned terminal-edge fixture; derive summaries in a separate process."""
import argparse
import datetime
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import pty
import select
import signal
import struct
import subprocess
import termios
import time


SCHEMA = "blackbird-terminal-edge-observer-1"
STRATEGIES = ("addressed", "erase", "corner", "full")
SCENARIOS = ("sparse", "stream", "blank", "style", "unicode", "resize")
FRAME_END = b"\x1b[?2026l"
FRAME_START = b"\x1b[?2026h"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def geometry(fd, cols, rows):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def base_environment():
    env = os.environ.copy()
    for key in tuple(env):
        if key.endswith(("_API_KEY", "_TOKEN")) or key.startswith("BLACKBIRD_"):
            env.pop(key)
    env.update(TERM="xterm-256color", TERM_PROGRAM="ghostty", LANG="C.UTF-8", LC_ALL="C.UTF-8")
    return env


def clean_child(child, result):
    if child is not None and child.poll() is None:
        result["forced_shutdown"] = True
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        child.wait(timeout=5)


def batch_trial(args, strategy, cols, rows, trial):
    name = f"batch-{strategy}-{cols}x{rows}-{trial:03d}"
    original = args.output / "originals" / name
    command = [str(args.executable), "--strategy", strategy, "--cols", str(cols),
               "--rows", str(rows), "--batch", "all", "--iterations", str(args.iterations)]
    result = {"mode": "batch", "strategy": strategy, "cols": cols, "rows": rows,
              "trial": trial, "scenario": "all", "status": "failed",
              "forced_shutdown": False, "command": command, "original": str(original.relative_to(args.output))}
    child = None
    try:
        result["begin_ns"] = time.monotonic_ns()
        child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, env=base_environment(), start_new_session=True)
        stdout, stderr = child.communicate(timeout=20)
        result["observed_ns"] = time.monotonic_ns()
        original.with_suffix(".stdout").write_bytes(stdout)
        original.with_suffix(".stderr").write_bytes(stderr)
        result["exit_code"] = child.returncode
        # Raw BATCH records are interpreted only by the separate report.
        if child.returncode != 0:
            raise RuntimeError(f"native batch exit {child.returncode}")
        result["status"] = "ok"
    except (OSError, subprocess.SubprocessError, RuntimeError, KeyboardInterrupt) as error:
        result["failure"] = repr(error)
    finally:
        clean_child(child, result)
        if child is not None:
            result["exit_code"] = child.returncode
        if not original.with_suffix(".stdout").exists():
            stdout, stderr = child.communicate(timeout=1) if child is not None else (b"", b"")
            original.with_suffix(".stdout").write_bytes(stdout)
            original.with_suffix(".stderr").write_bytes(stderr)
    return result


def pty_trial(args, strategy, cols, rows, trial):
    name = f"pty-{strategy}-{cols}x{rows}-{trial:03d}"
    original = args.output / "originals" / name
    master, slave = pty.openpty()
    event_read, event_write = os.pipe()
    os.set_blocking(master, False)
    os.set_blocking(event_read, False)
    geometry(slave, cols, rows)
    command = [str(args.executable), "--strategy", strategy, "--events-fd", str(event_write)]
    result = {"mode": "pty", "strategy": strategy, "cols": cols, "rows": rows,
              "trial": trial, "status": "failed", "forced_shutdown": False,
              "command": command, "original": str(original.relative_to(args.output)),
              "events": [], "commands": [], "frame_ends": [], "pressure_filler_bytes": 0}
    terminal, events, pending = bytearray(), bytearray(), bytearray()
    frame_scan = 0
    synchronized = False
    child = None
    event_open = master_open = True

    def pump(drain, timeout=0.02):
        nonlocal frame_scan, event_open, master_open, synchronized
        descriptors = ([event_read] if event_open else []) + ([master] if drain and master_open else [])
        if not descriptors:
            time.sleep(min(timeout, 0.02))
            return
        for fd in select.select(descriptors, [], [], timeout)[0]:
            try:
                block = os.read(fd, 65536)
            except OSError as error:
                if fd == master and error.errno == errno.EIO:
                    master_open = False
                    continue
                raise
            observed = time.monotonic_ns()
            if not block:
                if fd == event_read:
                    event_open = False
                else:
                    master_open = False
                continue
            if fd == master:
                terminal.extend(block)
                while True:
                    markers = [(index, opening) for marker, opening in ((FRAME_START, True), (FRAME_END, False))
                               if (index := terminal.find(marker, frame_scan)) >= 0]
                    if not markers:
                        frame_scan = max(frame_scan, len(terminal) - len(FRAME_END) + 1)
                        break
                    index, opening = min(markers)
                    frame_scan = index + len(FRAME_END)
                    if opening:
                        if synchronized:
                            raise RuntimeError("nested synchronized frame boundary")
                        synchronized = True
                    elif synchronized:
                        synchronized = False
                        result["frame_ends"].append({"offset": frame_scan, "observed_ns": observed})
                    # TerminalMode cleanup closes synchronization defensively;
                    # an unmatched close is lifecycle output, not a new frame.
            else:
                events.extend(block)
                pending.extend(block)
                while b"\n" in pending:
                    line, _, suffix = pending.partition(b"\n")
                    pending[:] = suffix
                    fields = line.decode("ascii").split()
                    if not fields:
                        continue
                    if fields[0] not in ("INPUT", "PREP", "PENDING", "READY", "EXIT"):
                        raise RuntimeError(f"unexpected native event {fields!r}")
                    result["events"].append({"fields": fields, "observed_ns": observed})

    def has(kind, seq):
        return any(event["fields"][:2] == [kind, str(seq)] for event in result["events"])

    def until(predicate, label, drain=True, keep_full=False):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if keep_full:
                fill()
            pump(drain)
            if predicate():
                return
        raise RuntimeError(label)

    def fill():
        for extent in (4096, 256, 16, 1):
            while True:
                try:
                    count = os.write(slave, b"~" * extent)
                except BlockingIOError:
                    break
                result["pressure_filler_bytes"] += count

    def send(byte, seq, target=None):
        if target:
            geometry(slave, *target)
        begin = time.monotonic_ns()
        if os.write(master, byte) != len(byte):
            raise RuntimeError("partial one-byte native command")
        result["commands"].append({"seq": seq, "byte": byte[0], "begin_ns": begin,
                                    "geometry": list(target) if target else None})

    def stage(byte, seq, target=None):
        before = len(result["frame_ends"])
        send(byte, seq, target)
        until(lambda: has("READY", seq) and len(result["frame_ends"]) > before,
              f"stage {seq} frame did not complete")

    try:
        result["begin_ns"] = time.monotonic_ns()
        child = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=subprocess.PIPE,
                                 env=base_environment(), pass_fds=(event_write,), start_new_session=True)
        os.close(event_write)
        event_write = -1
        until(lambda: has("READY", 0) and result["frame_ends"], "startup frame did not complete")
        for seq, byte in enumerate((b"b", b"c", b"u", b"o"), 1):
            stage(byte, seq)
        stage(b"s", 5)
        stage(b"r", 6, (32, 12))
        stage(b"r", 7, (cols, rows))
        # The fixture owns the shared descriptor's nonblocking output flag.
        fill()
        if result["pressure_filler_bytes"] == 0:
            raise RuntimeError("could not establish physical terminal output pressure")
        send(b"c", 8)
        until(lambda: has("PENDING", 8), "native output did not remain pending", drain=False, keep_full=True)
        result["pressure_begin_ns"] = time.monotonic_ns()
        send(b"b", 9)
        until(lambda: has("INPUT", 9), "input not acquired while terminal blocked", drain=False, keep_full=True)
        send(b"r", 10, (32, 12))
        until(lambda: has("INPUT", 10), "first resize input not acquired while blocked", drain=False, keep_full=True)
        send(b"r", 11, (cols, rows))
        until(lambda: has("INPUT", 11), "latest resize input not acquired while blocked", drain=False, keep_full=True)
        send(b"q", 12)
        until(lambda: has("INPUT", 12), "quit input not acquired while blocked", drain=False, keep_full=True)
        hold_end = result["pressure_begin_ns"] + 120_000_000
        while time.monotonic_ns() < hold_end:
            fill()
            pump(False, 0.005)
        result["pressure_release_ns"] = time.monotonic_ns()
        until(lambda: has("READY", 11), "latest frame did not complete after terminal drain")
        until(lambda: child.poll() is not None, "native fixture did not exit cleanly")
        for _ in range(4):
            pump(True, 0.001)
        result["exit_code"] = child.wait(timeout=1)
        if result["exit_code"] != 0:
            raise RuntimeError(f"native PTY exit {result['exit_code']}")
        result["status"] = "ok"
    except (OSError, UnicodeError, ValueError, subprocess.SubprocessError, RuntimeError, KeyboardInterrupt) as error:
        result["failure"] = repr(error)
    finally:
        clean_child(child, result)
        if child is not None:
            result["exit_code"] = child.returncode
        result["finished_ns"] = time.monotonic_ns()
        original.with_suffix(".terminal").write_bytes(terminal)
        original.with_suffix(".events").write_bytes(events)
        original.with_suffix(".stderr").write_bytes(child.stderr.read() if child is not None else b"")
        for fd in (master, slave, event_read, event_write):
            if fd >= 0:
                os.close(fd)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source", required=True)
    parser.add_argument("--build-identity", required=True)
    parser.add_argument("--count", type=int, default=30)
    parser.add_argument("--iterations", type=int, default=200)
    parser.add_argument("--strategies", nargs="+", choices=STRATEGIES, default=list(STRATEGIES))
    parser.add_argument("--mode", choices=("all", "batch", "pty"), default="all")
    args = parser.parse_args()
    if args.count < 1 or args.iterations < 1:
        parser.error("counts must be positive")
    if len(set(args.strategies)) != len(args.strategies):
        parser.error("strategies must be distinct")
    args.executable = args.executable.resolve(strict=True)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "originals").mkdir()
    metadata = {"schema": SCHEMA, "started_utc": utc(), "host": platform.node(),
                "platform": platform.platform(), "source": args.source,
                "build_identity": args.build_identity, "executable": str(args.executable),
                "native_sha256": digest(args.executable), "observer_sha256": digest(__file__),
                "count": args.count, "iterations": args.iterations, "strategies": args.strategies,
                "geometries": [[110, 35], [80, 24]], "scenarios": list(SCENARIOS),
                "mode": args.mode, "load_before": list(os.getloadavg()), "failures": 0}
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    def interrupted(signum, frame):
        del signum, frame
        raise KeyboardInterrupt("observer interrupted")
    signal.signal(signal.SIGTERM, interrupted)
    try:
        with (args.output / "samples.jsonl").open("x") as samples:
            for trial in range(1, args.count + 1):
                for cols, rows in metadata["geometries"]:
                    for strategy in args.strategies:
                        if digest(args.executable) != metadata["native_sha256"]:
                            raise RuntimeError("native executable changed during round")
                        results = []
                        if args.mode in ("all", "batch"):
                            results.append(batch_trial(args, strategy, cols, rows, trial))
                        if args.mode in ("all", "pty"):
                            results.append(pty_trial(args, strategy, cols, rows, trial))
                        for result in results:
                            samples.write(json.dumps(result, separators=(",", ":")) + "\n")
                            samples.flush()
                            metadata["failures"] += result["status"] != "ok"
                        if any(result["status"] != "ok" for result in results):
                            raise RuntimeError("trial failed; original attempt retained, round stopped")
    except (OSError, RuntimeError, KeyboardInterrupt) as error:
        metadata["failure"] = repr(error)
    finally:
        metadata["finished_utc"] = utc()
        metadata["load_after"] = list(os.getloadavg())
        metadata["final_native_sha256"] = digest(args.executable)
        (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps({"output": str(args.output), "failures": metadata["failures"],
                      "failure": metadata.get("failure")}))
    return 1 if metadata.get("failure") or metadata["failures"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
