"""Actual CLI/PTY native custody, editor and signal originals; no provider turn."""
from pathlib import Path
import ctypes
import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time

# Liveness guard for the PTY handshakes and the exit: each happens eventually,
# and the guard only names the step that hung before CTest's 120-second
# watchdog. Host load must not reach it.
LIVENESS_GUARD = 60


def require(value, message):
    if not value:
        raise RuntimeError(message)


def preserved(reader, session):
    rows = {}
    for run in (session / "raw").iterdir():
        if not run.is_dir():
            continue
        stream = subprocess.run([reader, "records", str(run)], check=True,
                                capture_output=True, timeout=45).stdout
        position = 16 + struct.unpack_from("<I", stream, 12)[0]
        while stream[position]:
            size = struct.unpack_from("<I", stream, position + 97)[0]
            raw = stream[position + 101:position + 101 + size]
            length, site, ordinal = struct.unpack_from("=IIQ", raw)
            require(length == size and site not in (8000, 8001),
                    "actual CLI H records have exact lengths and no transport failure")
            key = (stream[position + 65:position + 81], ordinal)
            require(key not in rows, "actual CLI H native origin unique")
            rows[key] = (site, raw[16:])
            position += 101 + size
        require(position + 65 == len(stream), "actual native reader footer exact")
    return rows


def reference(body):
    require(len(body) >= 24, "complete actual native H reference")
    return (body[:16], struct.unpack_from("=Q", body, 16)[0]), body[24:]


def original(body):
    count = struct.unpack_from("=Q", body)[0]
    require(count <= len(body) - 8, "actual native H original extent")
    return body[8:8 + count], body[8 + count:]


def flags(mode):
    result = list(mode)
    result[3] &= ~termios.PENDIN
    result[6] = [byte if isinstance(byte, int) else byte[0] for byte in result[6]]
    return result


def decoded_termios(body):
    width = ctypes.sizeof(ctypes.c_ulong) if sys.platform == "darwin" else 4
    pattern = "=Q" if width == 8 else "=I"
    native_flags = []
    for _ in range(4):
        native_flags.append(struct.unpack_from(pattern, body)[0])
        body = body[width:]
    line = body[0]
    body = body[1:]
    require(line == (sys.platform != "darwin"), "qualified native termios line field")
    if line:
        body = body[1:]
    count = 20 if sys.platform == "darwin" else 32
    controls = list(body[:count])
    body = body[count:]
    speeds = []
    for _ in range(2):
        speeds.append(struct.unpack_from(pattern, body)[0])
        body = body[width:]
    return flags(native_flags + speeds + [controls]), body


def main():
    binary, reader = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="blackbird-h-pty-") as temporary:
        root = Path(temporary)
        session = root / "session"
        workflow = root / "unused.lua"
        workflow.write_text("blackbird.call('write_file',{path='/never',content='unreached'})")
        master, slave = pty.openpty()
        process = None
        try:
            before = termios.tcgetattr(slave)
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 110, 0, 0))
            environment = dict(os.environ)
            environment["TERM"] = "xterm-256color"
            environment["VISUAL"] = "sh -c 'printf native-editor-result > \"$1\"' --"
            process = subprocess.Popen([binary, "--session", str(session), "--workflow", str(workflow)],
                                       stdin=slave, stdout=slave, stderr=slave, env=environment)
            output = bytearray()

            def drain(timeout):
                ready, _, _ = select.select([master], [], [], timeout)
                if ready:
                    try:
                        output.extend(os.read(master, 65536))
                    except OSError:
                        pass  # EIO: the slave side has no more writers

            def until(literal):
                # A redrawing TUI keeps writing, so the guard is on the whole wait,
                # not on silence between reads.
                deadline = time.monotonic() + LIVENESS_GUARD
                while literal not in output:
                    require(time.monotonic() < deadline and process.poll() is None,
                            ("actual native PTY handshake absent", literal, process.poll(),
                             bytes(output[-2000:])))
                    drain(.1)

            def exit_status():
                # Keep reading the PTY while the TUI restores the terminal, so its
                # last writes cannot block on a full PTY buffer.
                deadline = time.monotonic() + LIVENESS_GUARD
                while process.poll() is None:
                    require(time.monotonic() < deadline,
                            ("actual selected CLI H exit absent", bytes(output[-2000:])))
                    drain(.1)
                return process.returncode

            until(b"Ctrl-G editor")
            process.send_signal(signal.SIGWINCH)
            os.write(master, b"\x07")
            until(b"Editor returned")
            until(b"native-editor-result")
            os.write(master, b"\x11")
            require(exit_status() == 0, "actual selected CLI H exits normally")
            after = termios.tcgetattr(slave)
            require(flags(after) == flags(before), "actual CLI final owned termios flags/controls restored")
            rows = preserved(reader, session)
            custody = {}
            requests = {}
            returns = {}
            terms = {}
            modes = []
            originals = {}
            child_acquired = child_reaped = resize_notify = False
            actions = []
            resize_incarnations = set()
            notifications = []
            for key, (site, body) in rows.items():
                if site == 4100:
                    parent, body = reference(body)
                    require(parent in rows and rows[parent][0] in (4200, 4100),
                            "actual H custody anchored in Main input or creating custody")
                    require(len(body) == 1, "actual H custody method extent")
                    custody[key] = body[0]
                elif site == 4101:
                    parent, body = reference(body)
                    require(parent in custody and body == bytes((1, 0, 0)),
                            "actual H whole native returned terminal literal fields")
                    terms[parent] = terms.get(parent, 0) + 1
                elif site == 4104:
                    parent, body = reference(body)
                    descriptor, action = struct.unpack_from("=ii", body)
                    require(descriptor == 0 and action == termios.TCSANOW,
                            "actual H native termios descriptor/action")
                    decoded, body = decoded_termios(body[8:])
                    require(not body, "native H termios excludes padding")
                    modes.append((custody[parent], decoded))
                    requests[key] = 4104
                elif site == 4105:
                    parent, body = reference(body)
                    require(requests[parent] == 4104 and body == struct.pack("=i?", 0, False),
                            "actual tcsetattr result distinct from proposal")
                    returns[parent] = returns.get(parent, 0) + 1
                elif site == 4122:
                    parent, body = reference(body)
                    role, present = body[:2]
                    body = body[2:]
                    text, body = original(body) if present else (b"", body)
                    require(not body, "actual H source optional fields exact")
                    if present and role in (1, 2, 3, 4, 5, 8, 9):
                        require(text.endswith(b"\x00"), "actual native CString terminator preserved")
                        text = text[:-1]
                    originals.setdefault(role, []).append((key, parent, text))
                elif site == 4128:
                    _, body = reference(body)
                    pid, owned, reaped = struct.unpack("=i??", body)
                    require(pid > 0, "actual H editor PID")
                    child_acquired |= owned and not reaped
                    child_reaped |= not owned and reaped
                elif site == 2056:
                    parent, body = reference(body)
                    if len(body) == 8 and struct.unpack("=ii", body)[1] == 1:
                        # One existing signal-wake shutdown notification; no inferred signal count.
                        notifications.append(parent)
                elif site == 2062:
                    parent, body = reference(body)
                    signal_number = struct.unpack_from("=i", body)[0]
                    if signal_number == signal.SIGWINCH:
                        require(parent in rows, "native resize action links actual endpoint incarnation")
                        actions.append(key)
                        resize_incarnations.add(parent)
            require(custody and all(terms.get(key) == 1 for key in custody),
                    "one paid native terminal for each actual H custody method")
            require(len(modes) == 4 and [method for method, _ in modes] == [2, 3, 2, 4],
                    "actual initial/editor suspend/resume/final native mode sequence")
            require(modes[1][1] == flags(before) and modes[3][1] == flags(before),
                    "actual captured restoration fields match independently acquired PTY original")
            require(all(returns.get(key) == 1 for key in requests),
                    "each real native termios call has one distinct actual result")
            resize_notify = any(parent in resize_incarnations for parent in notifications)
            require(len(actions) == 2 and resize_notify, "actual native SIGWINCH install/notify/restore captured")
            require(child_acquired and child_reaped, "actual editor child ownership and native reap captured")
            require([text for _, _, text in originals[6]] == [b"native-editor-result"] and
                    b"".join(text for _, _, text in originals[7]) == b"native-editor-result",
                    "actual editor native read chunks and returned original match independently observed display")
            require(b"native-editor-result" in output, "actual editor original reaches native terminal presentation")
            print("actual CLI/PTY H mode/editor/signal originals and native restoration passed")
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
            os.close(slave)
            os.close(master)


if __name__ == "__main__":
    main()
