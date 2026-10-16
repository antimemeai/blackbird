#!/usr/bin/env python3
"""Finite actual-PTY TUI observations. Analyze recorded samples with bench-tui-report.

Development only; derived from the retained 2026-10-14 startup observer.
The VT subset observes committed text, not pixels, style or a physical display.
"""
import argparse
import codecs
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import pty
import re
import select
import signal
import struct
import subprocess
import termios
import time
import unicodedata

SCHEMA = "blackbird-tui-observer-1"


def digest(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def native_identity(path):
    value = Path(path).stat()
    return {"device": value.st_dev, "inode": value.st_ino,
            "size": value.st_size, "mtime_ns": value.st_mtime_ns}


class InputScreen:
    """Small VT text observer: track cursor updates so split input renders join.

    This is not a terminal renderer. Color, graphics and keyboard modes are ignored.
    Retained PTY bytes remain the primary record of what each program emitted.
    """
    def __init__(self, cols, rows):
        self.width, self.height = cols, rows
        self.rows = [[" "] * cols for _ in range(rows)]
        self.row = self.col = 0
        self.saved = (0, 0)
        self.pending = ""
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")
        self.generation = 0
        self.committed = []

    def feed(self, data):
        self.pending += self.decoder.decode(data)
        i = 0
        while i < len(self.pending):
            char = self.pending[i]
            if char == "\x1b":
                if i + 1 >= len(self.pending):
                    break
                kind = self.pending[i + 1]
                if kind == "[":
                    match = re.match(r"\x1b\[([0-?]*)([ -/]*)([@-~])", self.pending[i:])
                    if match is None:
                        break
                    self.csi(match.group(1), match.group(3))
                    i += len(match.group())
                    continue
                if kind in "]_P^":
                    match = re.search(r"\x07|\x1b\\", self.pending[i + 2:])
                    if match is None:
                        break
                    i += 2 + match.end()
                    continue
                if kind == "7":
                    self.saved = (self.row, self.col)
                elif kind == "8":
                    self.row, self.col = self.saved
                if kind in "()":
                    if i + 2 >= len(self.pending):
                        break
                    i += 3
                else:
                    i += 2
                continue
            if char == "\r":
                self.col = 0
            elif char == "\n":
                self.row += 1
                if self.row >= self.height:
                    self.rows.pop(0)
                    self.rows.append([" "] * self.width)
                    self.row = self.height - 1
            elif char == "\b":
                self.col = max(0, self.col - 1)
            elif char == "\t":
                self.col = min(self.width - 1, (self.col // 8 + 1) * 8)
            elif char >= " " and char != "\x7f" and not unicodedata.combining(char):
                if self.col >= self.width:
                    self.col = 0
                    self.row = min(self.height - 1, self.row + 1)
                self.rows[self.row][self.col] = char
                self.col += 2 if unicodedata.east_asian_width(char) in ("W", "F") else 1
            i += 1
        self.pending = self.pending[i:]

    def csi(self, parameters, operation):
        private = parameters.startswith("?")
        values = [int(value) if value.isdigit() else 0 for value in parameters.lstrip("?<>").split(";")]
        first = values[0] if values else 0
        amount = first or 1
        if operation in "Hf":
            self.row = min(self.height - 1, max(0, amount - 1))
            self.col = min(self.width - 1, max(0, (values[1] or 1) - 1)) if len(values) > 1 else 0
        elif operation == "A": self.row = max(0, self.row - amount)
        elif operation == "B": self.row = min(self.height - 1, self.row + amount)
        elif operation == "C": self.col = min(self.width - 1, self.col + amount)
        elif operation == "D": self.col = max(0, self.col - amount)
        elif operation == "G": self.col = min(self.width - 1, amount - 1)
        elif operation == "d": self.row = min(self.height - 1, amount - 1)
        elif operation == "s": self.saved = (self.row, self.col)
        elif operation == "u" and not private: self.row, self.col = self.saved
        elif operation == "J":
            if first in (2, 3): self.rows = [[" "] * self.width for _ in range(self.height)]
            elif first == 0:
                self.rows[self.row][min(self.width, self.col):] = [" "] * (self.width - min(self.width, self.col))
                for row in range(self.row + 1, self.height): self.rows[row] = [" "] * self.width
        elif operation == "K":
            start, end = (0, self.width) if first == 2 else ((0, min(self.width, self.col + 1)) if first == 1 else (min(self.width, self.col), self.width))
            self.rows[self.row][start:end] = [" "] * (end - start)
        if private and (first, operation) == (2026, "l"):
            self.generation += 1
            self.committed = ["".join(row) for row in self.rows]

    def contains(self, text):
        return any(text in row for row in self.committed)



class TerminalReplies:
    """Answer terminal queries, including queries split across PTY reads."""
    def __init__(self):
        self.pending = b""

    def feed(self, block, master):
        self.pending += block
        queries = [
            (b"\x1b[6n", b"\x1b[1;1R"),
            (b"\x1b[c", b"\x1b[?1;2c"),
            (b"\x1b[>c", b"\x1b[>0;0;0c"),
            (b"\x1b[?u", b"\x1b[?0u"),
        ]
        for number, rgb in ((10, b"ffff/ffff/ffff"), (11, b"0000/0000/0000")):
            for end in (b"\x07", b"\x1b\\"):
                queries.append((b"\x1b]" + str(number).encode() + b";?" + end,
                                b"\x1b]" + str(number).encode() + b";rgb:" + rgb + b"\x1b\\"))
        for query, reply in queries:
            while query in self.pending:
                os.write(master, reply)
                self.pending = self.pending.replace(query, b"", 1)
        self.pending = self.pending[-32:]



def resize(master, screen, cols, rows):
    screen.width, screen.height = cols, rows
    screen.rows = [[" "] * cols for _ in range(rows)]
    screen.row = screen.col = 0
    screen.saved = (0, 0)
    screen.committed = []
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def scenario(cols, rows):
    return {
        "cols": cols, "rows": rows, "session": "fresh",
        "environment": {"TERM": "xterm-256color", "TERM_PROGRAM": "ghostty",
                        "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8", "BLACKBIRD_GRAPHICS": "off"},
        "frame_end": "CSI ?2026 l",
        "startup": "spawn begin to newer committed frame containing Enter send, ECHO disabled",
        "input": "BB_TUI_INPUT_0123456789, no Enter; committed row contains exact marker",
        "paste": "Ctrl-U; bracketed paste 4080 x bytes plus newline and BB_PASTE_TAIL, no Enter; exact tail in committed row",
        "palette_open": "Ctrl-U then /; committed selected > /help row",
        "palette_next": "CSI B; committed selected > /keys row",
        "resize": {"to": [max(32, cols - 14), max(8, rows - 5)],
                   "draft": "BB_RESIZE_DRAFT", "endpoint": "new committed frame contains exact draft in third-last row and closing border in second-last row"},
        "resize_restore": "restore original size; exact draft in third-last row and closing border in second-last row",
        "quit": "Ctrl-U then Ctrl-D, normal exit zero, no force",
        "endpoint_clock": "parent monotonic_ns after parsing read block and observing newer committed frame",
        "cache": "ordinary OS caches; all trials retained; no warmup exclusion",
    }


def sample(variant, spec, definition, trial, output, records, interruption):
    name = f"{variant}-{definition['cols']}x{definition['rows']}-{trial:03d}"
    run = output / "sessions" / name
    run.mkdir()
    master, slave = pty.openpty()
    os.set_blocking(master, False)
    screen = InputScreen(definition["cols"], definition["rows"])
    resize(slave, screen, definition["cols"], definition["rows"])
    transcript = bytearray()
    replies = TerminalReplies()
    child = None
    result = {"variant": variant, "trial": trial,
              "scenario": f"{definition['cols']}x{definition['rows']}-fresh",
              "status": "failed", "endpoints": {}, "forced_shutdown": False,
              "transcript": f"terminal-output/{name}.bin"}
    env = os.environ.copy()
    # Native execution needs no provider credentials. Do not inherit client keys.
    for key in tuple(env):
        if key.endswith(("_API_KEY", "_TOKEN")) or key.startswith("BLACKBIRD_"):
            env.pop(key)
    env.update(definition["environment"])
    command = [spec["executable"], "--session", str(run)]
    try:
        if native_identity(spec["executable"]) != spec["native_identity"]:
            raise RuntimeError("native executable identity changed before trial")
        # Finish assigning the owned PID before a signal can unwind the body.
        interruption["cleanup"] = True
        try:
            started = time.monotonic_ns()
            child = subprocess.Popen(command, cwd=output, env=env, stdin=slave, stdout=slave,
                                     stderr=slave, start_new_session=True)
        finally:
            interruption["cleanup"] = False
        result["pid"] = child.pid
        if interruption["signal"] is not None:
            raise InterruptedError("observer interrupted during native spawn")
        os.close(slave)
        slave = -1

        def until(predicate, generation):
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if select.select([master], [], [], .05)[0]:
                    try:
                        block = os.read(master, 65536)
                    except OSError as error:
                        if error.errno == errno.EIO:
                            break
                        raise
                    if not block:
                        break
                    transcript.extend(block)
                    replies.feed(block, master)
                    screen.feed(block)
                    if screen.generation > generation and predicate():
                        return time.monotonic_ns()
                    if len(transcript) > 16 * 1024 * 1024:
                        raise RuntimeError("excessive terminal output")
                elif child.poll() is not None:
                    break
            raise RuntimeError("endpoint not reached before exit/10s fixture guard")

        def observe(name, data, expected):
            generation = screen.generation
            begin = time.monotonic_ns()
            offset = 0
            write_deadline = time.monotonic() + 10
            while offset < len(data):
                if time.monotonic() >= write_deadline:
                    raise RuntimeError("keyboard write exceeded 10s fixture guard")
                try:
                    written = os.write(master, data[offset:])
                except BlockingIOError:
                    select.select([], [master], [], .05)
                    continue
                if written <= 0:
                    raise RuntimeError("keyboard write made no progress")
                offset += written
            written_at = time.monotonic_ns()
            observed = until(lambda: screen.contains(expected), generation)
            result["endpoints"][name] = {"begin_ns": begin, "write_complete_ns": written_at,
                                         "observed_ns": observed, "offered_bytes": len(data),
                                         "frame_generation": screen.generation}

        ready = until(lambda: screen.contains("Enter send"), 0)
        if termios.tcgetattr(master)[3] & termios.ECHO:
            raise RuntimeError("startup frame observed with kernel input echo enabled")
        result["endpoints"]["startup"] = {"begin_ns": started, "observed_ns": ready,
                                              "frame_generation": screen.generation}
        observe("input", b"BB_TUI_INPUT_0123456789", "BB_TUI_INPUT_0123456789")
        observe("paste", b"\x15\x1b[200~" + b"x" * 4080 + b"\nBB_PASTE_TAIL\x1b[201~", "BB_PASTE_TAIL")
        observe("palette_open", b"\x15/", "> /help")
        observe("palette_next", b"\x1b[B", "> /keys")
        observe("resize_draft", b"\x15BB_RESIZE_DRAFT", "BB_RESIZE_DRAFT")
        for name, (cols, rows) in (("resize", definition["resize"]["to"]),
                                   ("resize_restore", (definition["cols"], definition["rows"]))):
            generation = screen.generation
            begin = time.monotonic_ns()
            resize(master, screen, cols, rows)
            os.kill(child.pid, signal.SIGWINCH)
            observed = until(lambda: len(screen.committed) == rows
                             and "BB_RESIZE_DRAFT" in screen.committed[-3]
                             and screen.committed[-2].startswith("╰"), generation)
            result["endpoints"][name] = {"begin_ns": begin, "observed_ns": observed,
                                         "cols": cols, "rows": rows,
                                         "frame_generation": screen.generation}
        os.write(master, b"\x15\x04")
        deadline = time.monotonic() + 5
        while child.poll() is None and time.monotonic() < deadline:
            if select.select([master], [], [], .05)[0]:
                try:
                    block = os.read(master, 65536)
                    if block:
                        transcript.extend(block)
                        replies.feed(block, master)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
        if child.poll() is None:
            result["forced_shutdown"] = True
            raise RuntimeError("normal quit did not settle before 5s fixture guard")
        if child.returncode:
            raise RuntimeError(f"native exit {child.returncode}")
        result["status"] = "ok"
    except Exception as error:
        result["error"] = repr(error)
    finally:
        # The handler records signals during cleanup without throwing through it.
        # Repeated signals likewise cannot skip owned-child reaping/artifact writes.
        interruption["cleanup"] = True
        if child is not None:
            for sig in (signal.SIGTERM, signal.SIGKILL):
                if child.poll() is not None:
                    break
                result["forced_shutdown"] = True
                try:
                    os.killpg(child.pid, sig)
                except ProcessLookupError:
                    pass
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    continue
            result["exit_code"] = child.poll()
        os.close(master)
        if slave != -1:
            os.close(slave)
        result["finished_ns"] = time.monotonic_ns()
        result["terminal_bytes"] = len(transcript)
        (output / result["transcript"]).write_bytes(transcript)
        try:
            result["native_identity_after"] = native_identity(spec["executable"])
            if result["native_identity_after"] != spec["native_identity"]:
                result.update(status="failed", executable_changed=True,
                              error="native executable identity changed during trial")
        except OSError as error:
            result.update(status="failed", executable_changed=True, error=repr(error))
        if interruption["signal"] is not None:
            result.update(status="failed", interrupted=interruption["signal"])
        records.write(json.dumps(result) + "\n")
        records.flush()
        interruption["cleanup"] = False
    # Sessions and raw recordings intentionally remain for diagnosis and replay.
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("spec", type=Path, help="external JSON mapping labels to executable/source/profile/build_identity")
    parser.add_argument("output", type=Path, help="new artifact directory")
    parser.add_argument("--count", type=int, default=30)
    args = parser.parse_args()
    if not 1 <= args.count <= 10000:
        parser.error("count must be 1..10000")
    specs = json.loads(args.spec.read_text())
    if not specs or not isinstance(specs, dict):
        parser.error("spec must be a nonempty object")
    for name, spec in specs.items():
        if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
            parser.error("variant labels may contain only ASCII letters, numbers, _ and -")
        for key in ("executable", "source", "profile", "build_identity"):
            if not spec.get(key):
                parser.error(f"{name} requires {key}")
        spec["executable"] = str(Path(spec["executable"]).resolve(strict=True))
        spec["native_identity"] = native_identity(spec["executable"])
        spec["native_sha256"] = digest(spec["executable"])
        if native_identity(spec["executable"]) != spec["native_identity"]:
            parser.error(f"{name}: executable changed while reading initial hash")
    output = args.output.resolve()
    output.mkdir(mode=0o700)
    for name in ("sessions", "terminal-output"):
        (output / name).mkdir()
    (output / "observer.py").write_bytes(Path(__file__).read_bytes())
    (output / "spec.json").write_bytes(args.spec.read_bytes())
    definitions = [scenario(110, 35), scenario(80, 24)]
    metadata = {"schema": SCHEMA, "driver_sha256": digest(__file__), "variants": specs,
                "host": {"hostname": platform.node(), "system": platform.system(),
                         "release": platform.release(), "machine": platform.machine(),
                         "platform": platform.platform(), "python": platform.python_version()},
                "scenarios": definitions, "count_per_variant_and_size": args.count,
                "order": "trial, size, variant; serial round robin",
                "clock": "time.monotonic_ns, parent process, integer nanoseconds",
                "load_before": os.getloadavg(), "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "inference_prompts_submitted": 0, "launcher": None}
    if Path("/proc/cpuinfo").exists():
        cpu = Path("/proc/cpuinfo").read_text()
        metadata["host"]["cpu"] = next((line.split(":", 1)[1].strip() for line in cpu.splitlines()
                                         if line.startswith("model name")), "unknown")
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    failed = 0
    interruption = {"signal": None, "cleanup": False}

    def interrupted(number, _frame):
        if interruption["signal"] is None:
            interruption["signal"] = number
            if not interruption["cleanup"]:
                raise InterruptedError(f"observer interrupted by signal {number}")

    prior = {number: signal.signal(number, interrupted) for number in (signal.SIGTERM, signal.SIGINT)}
    try:
        with (output / "samples.jsonl").open("x") as records:
            for trial in range(1, args.count + 1):
                for definition in definitions:
                    for name, spec in specs.items():
                        result = sample(name, spec, definition, trial, output, records, interruption)
                        if result["status"] != "ok":
                            failed += 1
                            print(json.dumps(result), flush=True)
                        if interruption["signal"] is not None:
                            raise InterruptedError("round interrupted; diagnostic trial retained")
                if trial % 10 == 0 or trial == args.count:
                    print(f"{trial}/{args.count} trials per variant and size recorded", flush=True)
    except InterruptedError as error:
        (output / "interruption.json").write_text(json.dumps({"signal": interruption["signal"],
                                                            "observed_ns": time.monotonic_ns(),
                                                            "error": repr(error)}) + "\n")
    finally:
        # A final content hash complements per-trial identity checks. A changing
        # executable invalidates this round instead of labeling it as one build.
        interruption["cleanup"] = True
        changed = []
        for name, spec in specs.items():
            try:
                spec["final_native_sha256"] = digest(spec["executable"])
                if (spec["final_native_sha256"] != spec["native_sha256"] or
                        native_identity(spec["executable"]) != spec["native_identity"]):
                    changed.append(name)
            except OSError as error:
                spec["final_native_error"] = repr(error)
                changed.append(name)
        metadata.update(load_after=os.getloadavg(), failed=failed, interrupted=interruption["signal"],
                        executable_changed=changed,
                        finished_utc=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))
        (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
        for number, handler in prior.items():
            signal.signal(number, handler)
    print(f"{failed} failed trials; artifacts: {output}", flush=True)
    raise SystemExit(bool(failed or interruption["signal"] is not None or changed))


if __name__ == "__main__":
    main()
