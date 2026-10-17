#!/usr/bin/env python3
"""Derive terminal-edge campaign summaries from retained native and parent originals."""
import argparse
import json
import math
from pathlib import Path
import sys


SCHEMA = "blackbird-terminal-edge-observer-1"
SCENARIOS = ("sparse", "stream", "blank", "style", "unicode", "resize")
STAGES = {0: "startup", 1: "blank", 2: "style", 3: "unicode", 4: "overwrite",
          5: "sentinel", 6: "resize_narrow", 7: "resize_restore"}


def quantiles(values):
    ordered = sorted(values)
    if not ordered:
        raise ValueError("no observations")
    return {name: ordered[math.ceil(fraction * len(ordered)) - 1]
            for name, fraction in (("p50", .5), ("p95", .95), ("max", 1))}


def positive_duration(before, after, label):
    if after < before:
        raise ValueError(f"negative {label} duration")
    return after - before


def batch_values(root, sample, iterations):
    path = root / (sample["original"] + ".stdout")
    result = {}
    for line in path.read_text().splitlines():
        fields = line.split()
        if len(fields) != 9 or fields[0] != "BATCH" or fields[1] not in SCENARIOS:
            raise ValueError(f"invalid native batch record in {path}")
        scenario = fields[1]
        count, frames, before, after, cpu_before, cpu_after, byte_count = map(int, fields[2:])
        if scenario in result or count != iterations or frames != iterations or frames < 1 or byte_count < 0:
            raise ValueError(f"invalid native batch extent in {path}")
        result[scenario] = {
            "cpu_ms_per_frame": positive_duration(cpu_before, cpu_after, "batch CPU") / frames / 1e6,
            "wall_ms_per_frame": positive_duration(before, after, "batch wall") / frames / 1e6,
            "bytes_per_frame": byte_count / frames,
        }
    if set(result) != set(SCENARIOS):
        raise ValueError(f"missing native batch scenario in {path}")
    return result


def pty_values(sample):
    prepared, acquired, ready = {}, {}, {}
    for event in sample["events"]:
        fields = event["fields"]
        kind = fields[0]
        if kind == "PREP":
            if len(fields) != 7:
                raise ValueError("invalid PREP fields")
            seq, before, after, cpu_before, cpu_after, count = map(int, fields[1:])
            if seq in prepared or count < 1:
                raise ValueError("duplicate/empty PREP")
            prepared[seq] = {"before": before, "after": after, "cpu_before": cpu_before,
                             "cpu_after": cpu_after, "bytes": count}
        elif kind == "INPUT":
            if len(fields) != 3:
                raise ValueError("invalid INPUT fields")
            seq, byte = map(int, fields[1:])
            if seq in acquired:
                raise ValueError("duplicate INPUT")
            acquired[seq] = (event["observed_ns"], byte)
        elif kind == "READY":
            if len(fields) != 4:
                raise ValueError("invalid READY fields")
            seq, cols, rows = map(int, fields[1:])
            if seq in ready:
                raise ValueError("duplicate READY")
            ready[seq] = (cols, rows)
    commands = {command["seq"]: command for command in sample["commands"]}
    if set(commands) != set(range(1, 13)) or set(acquired) != set(commands):
        raise ValueError("missing/duplicate command or acquired input")
    if not {0, 1, 2, 3, 4, 5, 6, 7, 8, 11}.issubset(ready):
        raise ValueError("missing required completed native scene")
    if ready[6] != (32, 12) or ready[7] != (sample["cols"], sample["rows"]) or ready[11] != ready[7]:
        raise ValueError("native geometry differs from commanded geometry")
    if len(prepared) != len(sample["frame_ends"]):
        raise ValueError("native prepared packets differ from actual terminal frame boundaries")
    # Descriptor delivery can be reordered by scheduling. Pair actual packet
    # order with actual frame-end order, never event-read time with frame time.
    frame_ends = {seq: frame["observed_ns"]
                  for seq, frame in zip(prepared, sample["frame_ends"])}
    result = {}
    for seq, name in STAGES.items():
        begin = sample["begin_ns"] if seq == 0 else commands[seq]["begin_ns"]
        result[name + "_frame_ms"] = positive_duration(begin, frame_ends[seq], name) / 1e6
    for seq, name in ((9, "blocked_edit"), (10, "blocked_resize_first"),
                      (11, "blocked_resize_latest"), (12, "blocked_quit")):
        observed, byte = acquired[seq]
        if byte != commands[seq]["byte"] or observed >= sample["pressure_release_ns"]:
            raise ValueError("blocked input was not acquired before terminal release")
        result[name + "_acquire_ms"] = positive_duration(commands[seq]["begin_ns"], observed, name) / 1e6
    result["drain_latest_frame_ms"] = positive_duration(sample["pressure_release_ns"], frame_ends[11], "drain") / 1e6
    result["native_packet_bytes"] = sum(value["bytes"] for value in prepared.values())
    result["native_prep_cpu_ms"] = sum(positive_duration(value["cpu_before"], value["cpu_after"], "prep CPU")
                                       for value in prepared.values()) / 1e6
    result["native_prep_wall_ms"] = sum(positive_duration(value["before"], value["after"], "prep wall")
                                        for value in prepared.values()) / 1e6
    return result


def summarize(root):
    metadata = json.loads((root / "metadata.json").read_text())
    if metadata["schema"] != SCHEMA:
        raise ValueError("unknown observer schema")
    samples = [json.loads(line) for line in (root / "samples.jsonl").read_text().splitlines()]
    complete = ("finished_utc" in metadata and not metadata.get("failure") and
                metadata["native_sha256"] == metadata.get("final_native_sha256"))
    rows, failures = [], []
    expected_modes = ("batch", "pty") if metadata["mode"] == "all" else (metadata["mode"],)
    for sample in samples:
        if sample["status"] != "ok" or sample["forced_shutdown"] or sample.get("exit_code") != 0:
            failures.append(sample)
    for strategy in metadata["strategies"]:
        for cols, height in metadata["geometries"]:
            for mode in expected_modes:
                group = [sample for sample in samples if (sample["strategy"], sample["cols"], sample["rows"], sample["mode"])
                         == (strategy, cols, height, mode)]
                trials = [sample["trial"] for sample in group]
                valid = (len(trials) == metadata["count"] and set(trials) == set(range(1, metadata["count"] + 1))
                         and all(sample["status"] == "ok" and not sample["forced_shutdown"] and sample.get("exit_code") == 0
                                 for sample in group))
                if not valid:
                    complete = False
                    rows.append({"strategy": strategy, "geometry": f"{cols}x{height}", "mode": mode,
                                 "attempted": len(group), "expected": metadata["count"], "valid": False})
                    continue
                observations = {}
                for sample in group:
                    if mode == "batch":
                        for scenario, metrics in batch_values(root, sample, metadata["iterations"]).items():
                            for metric, value in metrics.items():
                                observations.setdefault((scenario, metric), []).append(value)
                    else:
                        for metric, value in pty_values(sample).items():
                            observations.setdefault(("episode", metric), []).append(value)
                for (scenario, metric), values in observations.items():
                    rows.append({"strategy": strategy, "geometry": f"{cols}x{height}", "mode": mode,
                                 "scenario": scenario, "metric": metric, "count": len(values), "valid": True,
                                 **quantiles(values)})
    return {"metadata": metadata, "complete": complete and not failures, "failures": failures, "rows": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        result = summarize(args.artifacts)
        if args.json:
            print(json.dumps(result, indent=2))
        else:
            print("Nearest-rank p50,p95,max. PTY observations include scheduling; no physical display latency claim.\n")
            print(f"Complete: {result['complete']}; failed process samples: {len(result['failures'])}.\n")
            print("| Policy | Geometry | Mode | Scenario | Metric | N | p50 | p95 | Max |")
            print("| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: |")
            for row in result["rows"]:
                if not row["valid"]:
                    print(f"| {row['strategy']} | {row['geometry']} | {row['mode']} | incomplete | | "
                          f"{row['attempted']}/{row['expected']} | | | |")
                else:
                    print(f"| {row['strategy']} | {row['geometry']} | {row['mode']} | {row['scenario']} | "
                          f"{row['metric']} | {row['count']} | {row['p50']:.6f} | {row['p95']:.6f} | {row['max']:.6f} |")
        return 0 if result["complete"] else 1
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
