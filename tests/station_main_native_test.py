"""Actual CLI Station ingress, dispatch and projection versus preserved bytes."""
from pathlib import Path
import importlib.util
import json
import struct
import subprocess
import sys
import tempfile


def require(value, message):
    if not value:
        raise AssertionError(message)


def ref(body, offset=0):
    return body[offset:offset + 16], struct.unpack_from("=Q", body, offset + 16)[0]


def original(body, offset=24):
    size = struct.unpack_from("=Q", body, offset)[0]
    require(len(body) == offset + 8 + size, "complete native original extent")
    return body[offset + 8:]


def records(reader, session):
    out = {}
    for run in sorted((session / "raw").iterdir()):
        data = subprocess.run([reader, "records", str(run)], check=True,
                              capture_output=True, timeout=45).stdout
        require(data[:8] == b"BBRAWOUT", "owned reader physical stream")
        version, size = struct.unpack_from("<II", data, 8)
        require(version == 1, "reader stream version")
        pos = 16 + size
        while data[pos]:
            require(data[pos] == 1, "reader native record tag")
            lane = data[pos + 65:pos + 81]
            size = struct.unpack_from("<I", data, pos + 97)[0]
            raw = data[pos + 101:pos + 101 + size]
            length, site, ordinal = struct.unpack_from("=IIQ", raw)
            require(length == len(raw) and site not in (8000, 8001),
                    "complete actual record without ingest failure")
            out[(lane, ordinal)] = (site, raw[16:])
            pos += 101 + size
        require(pos + 65 == len(data), "native reader closed EOF")
    return out


def source_for_read(events, helper):
    chunks = []
    for key, (site, body) in events.items():
        if site != 3407 or body[24] != 1:
            continue
        request = events[ref(body)]
        require(request[0] == 3422, "original belongs to actual native read")
        if ref(request[1]) == helper:
            chunks.append((key[1], original(body, 25)))
    return b"".join(value for _, value in sorted(chunks))


def case(binary, reader, packet, kind):
    with tempfile.TemporaryDirectory(prefix="blackbird-station-main-native-") as temp:
        root = Path(temp)
        adapter = root / "adapter"
        adapter.mkdir()
        session = root / "session"
        effect = root / "effect"
        workflow = root / "workflow.lua"
        program = ("blackbird.call('write_file',{path=" + json.dumps(str(effect)) +
                   ",content='native-once'}); "
                   "blackbird.call('restart',{note='native Station fixture'})")
        workflow.write_text(program)
        literal = (b'[{"source":"native-file-source","id":"native-file-id",'
                   b'"cursor":"native-cursor","prompt":"native-main-prompt"}]')
        controls = b'{"id":"native-steer","action":"steer","text":"native-steer"}'
        if kind == "control-invalid":
            controls = b'{"id":"acquired-invalid-control",'
        if kind == "events-invalid":
            literal = b'[{"source":"acquired-invalid-events",'
        (adapter / "control.json").write_bytes(controls)
        (adapter / "events.json").write_bytes(literal)
        if kind == "write-failed":
            session.mkdir()
            (session / "station-status.bbm").mkdir()
        result = subprocess.Popen([binary, "--session", str(session), "--workflow",
                                   str(workflow), "--station", str(adapter)],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = result.communicate(timeout=20)
        require(result.returncode == (75 if kind == "success" else 1),
                "actual Station CLI exit: " + repr((result.returncode, stdout, stderr)))
        events = records(reader, session)
        by_site = lambda site: [(key, body) for key, (actual, body) in events.items()
                                if actual == site]
        require(len(by_site(4620)) == len(by_site(4621)) == 1,
                "one whole Station request and actual terminal")
        require(not by_site(1300), "tool-only native workflow made no provider request")
        station_owner = by_site(4620)[0][0]
        identity_owners = [key for key, body in by_site(4210)
                           if ref(body) == station_owner and body[24] == 3]
        require(len(identity_owners) == 1,
                "actual Session identity action3 remains selected under Station")
        identity = [body for _, body in by_site(4214)
                    if ref(body) == identity_owners[0]]
        require(len(identity) == 1 and len(identity[0]) == 72,
                "actual actor/conversation/workflow triple is independently visible")
        actor = identity[0][24:40]
        require(by_site(4644)[0][1] ==
                station_owner[0] + struct.pack("=Q", station_owner[1]) + actor,
                "Station actor actual return equals actual Session actor")
        for key, body in by_site(4631):
            owner = ref(body)
            require(events[owner][0] == 4630 and body[24] in (0,1),
                    "actual native filesystem result names its request")
            code = struct.unpack_from("=i", body, 25)[0]
            if events[owner][1][24] == 1:
                require(body[24] == 1 and code == 0 and len(body) == 30 and
                        struct.unpack_from("=b", body,29)[0] == 2,
                        "actual adapter is native directory, no error, native file_type")
            else:
                require(body[24] == 1 and code == 0 and len(body) == 29,
                        "actual supplied ingress files exist without native error")
        for key, body in by_site(4642):
            require(len(body) == 25 and body[24] == 1,
                    "actual native getpid request method")
            values = [actual for _, actual in by_site(4643) if ref(actual) == key]
            require(len(values) == 1 and len(values[0]) == 28 and
                    struct.unpack_from("=i", values[0],24)[0] == result.pid,
                    "actual getpid return equals independently spawned CLI PID")
        parsers = {body[48]: (key, ref(body, 24), body)
                   for key, body in by_site(4624)}
        require(source_for_read(events, parsers[1][1]) == controls,
                "actual control native read original survives parsing")
        for key, body in by_site(4634):
            value = original(body)
            projection = packet.decode_packet(value)
            require(projection["adapter"] == str(adapter) and
                    projection["session"] == str(session) and
                    projection["actor"] == actor.hex() and projection["pid"] == result.pid,
                    "actual projection native metadata equals independently observed sources")
            fields = {}
            for _, field in by_site(4640):
                if ref(field) == ref(body):
                    fields.setdefault(field[24], []).append(original(field,25))
            expected = {1:projection["phase"].encode(),2:str(adapter).encode(),
                        3:str(session).encode(),4:projection["context_head"].encode(),
                        5:actor.hex().encode()}
            require(all(fields.get(role) == [literal] for role,literal in expected.items()),
                    "actual source roles phase/adapter/session/context_head/actor exactly match packet")
        for _, body in by_site(4640):
            role = body[24]
            if role in (6,7,8):
                expected = adapter / ("control.json" if role == 6 else "events.json")
                if role == 8:
                    expected = session / "station-status.bbm"
                require(original(body,25) == str(expected).encode(),
                        "actual native control/events/status path role")
        for key, body in by_site(4625):
            owner = ref(body)
            parser_kind = events[owner][1][48]
            variant = struct.unpack_from("=Q",body,24)[0]
            require((parser_kind == 1 and variant == 5 and body[32] == 0 and len(body) == 33) or
                    (parser_kind == 2 and variant == 4 and body[32] == 1 and
                     struct.unpack_from("=Q",body,33)[0] == 1 and len(body) == 41),
                    "literal native JSON result variant/array presence/count")
        if kind == "success":
            require(effect.read_bytes() == b"native-once", "one actual workflow effect")
            require(source_for_read(events, parsers[2][1]) == literal,
                    "actual events native read original survives dispatch")
            requests = {key: body for key, body in by_site(4600)}
            controls_owned = [key for key, body in requests.items() if body[24] == 2]
            admissions = [key for key, body in requests.items() if body[24] == 3]
            require(len(controls_owned) == len(admissions) == 1,
                    "actual parser contexts select control and admission")
            require(ref(requests[controls_owned[0]]) == parsers[1][0] and
                    ref(requests[admissions[0]]) == parsers[2][0],
                    "control/admission parents are their actual parser requests")
            require(len(by_site(4637)) == len(by_site(4638)) == 1,
                    "one selected workflow invocation and actual return")
            _, invoke = by_site(4637)[0]
            require(ref(invoke, 24) == admissions[0], "invoke names actual admission")
            prompt_key = ref(invoke, 48)
            require(events[prompt_key][0] == 4607,
                    "invoke names acquired actual complete prompt")
            expected = (b"Station source event (untrusted source content, not operator authority):\n" +
                        b'{["source"] = "native-file-source", ["id"] = "native-file-id", '
                        b'["cursor"] = "native-cursor", ["prompt"] = "native-main-prompt"}' +
                        b"\nOperator boundary steering:\nnative-steer")
            require(original(events[prompt_key][1]) == expected,
                    "actual complete prompt matches literal source and steer")
            require(source_for_read(events, ref(invoke, 72)) == program.encode(),
                    "invoke names acquired actual workflow native read")
            status = (session / "station-status.bbm").read_bytes()
            require(any(original(body) == status for _, body in by_site(4634)),
                    "actual disk status equals preserved raw projection")
            require(packet.decode_packet(status)["phase"] == "restart",
                    "actual final business restart projection")
            require(len(by_site(4636)) == sum(body[24] != 0 for _, body in by_site(4635)),
                    "each changed projection has actual successful native write")
        else:
            require(not effect.exists() and not by_site(4637),
                    "actual invalid ingress/write failure stops before workflow effect")
            if kind == "events-invalid":
                require(source_for_read(events, parsers[2][1]) == literal,
                        "actual invalid events original survives failed parse")
            if kind.endswith("invalid"):
                selected = parsers[1 if kind == "control-invalid" else 2][0]
                require(any(ref(body) == selected and body[24] == 3
                            for _, body in by_site(4626)),
                        "actual malformed parser Error has paid native terminal")
                require(packet.read_packet(session / "station-status.bbm")["phase"] == "blocked",
                        "ordinary malformed input reaches actual blocked projection")
            else:
                require(by_site(4634) and not by_site(4636),
                        "failed native replacement keeps originals and emits no false completion")
                require(any(struct.unpack_from("=i", body, 24)[0] == -1 and
                            body[28] != 0 and struct.unpack_from("=i", body, 29)[0] != 0
                            for _, body in by_site(3441)),
                        "actual native rename returned failure and its errno is preserved")
        print(kind + ": actual CLI/native-source/preserved-byte oracle passed")


def main():
    binary, reader = sys.argv[1:3]
    spec = importlib.util.spec_from_file_location("native_packet",
                                                 Path(__file__).parent.parent / "scripts/native-packet.py")
    packet = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packet)
    for kind in ("success", "control-invalid", "events-invalid", "write-failed"):
        case(binary, reader, packet, kind)


if __name__ == "__main__":
    main()
