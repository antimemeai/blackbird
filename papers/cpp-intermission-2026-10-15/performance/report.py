#!/usr/bin/env python3
"""Tertiary analysis of bench-tui originals; refuse mismatched tranche comparisons."""
import argparse
import json
import math
from pathlib import Path
import sys

QUANTILES = (("p50", .5), ("p90", .9), ("p95", .95), ("p99", .99), ("max", 1))
ENDPOINTS = ("startup", "input", "paste", "palette_open", "palette_next", "resize_draft", "resize", "resize_restore")


def read(path):
    metadata = json.loads((path / "metadata.json").read_text())
    if metadata.get("schema") != "blackbird-tui-observer-1":
        raise ValueError(f"{path}: unsupported/missing observer schema; historical numbers are descriptive only")
    if "finished_utc" not in metadata or metadata.get("interrupted") is not None:
        raise ValueError(f"{path}: incomplete/interrupted observation round")
    if metadata.get("executable_changed") or any(
            spec.get("final_native_sha256") != spec["native_sha256"]
            for spec in metadata["variants"].values()):
        raise ValueError(f"{path}: native executable changed or final content hash unavailable")
    records = [json.loads(line) for line in (path / "samples.jsonl").read_text().splitlines()]
    return metadata, records


def summarize(metadata, records, variant):
    expected = metadata["count_per_variant_and_size"]
    if variant not in metadata["variants"]:
        raise ValueError(f"unknown variant {variant}")
    result = {}
    for definition in metadata["scenarios"]:
        scenario = f"{definition['cols']}x{definition['rows']}-fresh"
        selected = [record for record in records if record["variant"] == variant and record["scenario"] == scenario]
        complete = [record for record in selected if record["status"] == "ok" and not record["forced_shutdown"]
                    and record["exit_code"] == 0 and set(record["endpoints"]) == set(ENDPOINTS)]
        trials = {record["trial"] for record in selected}
        valid = len(complete) == expected == len(selected) and trials == set(range(1, expected + 1))
        row = {"attempted": len(selected), "complete": len(complete), "expected": expected,
               "failed": len(selected) - len(complete), "valid": valid, "metrics_ms": {}}
        if valid:
            for endpoint in ENDPOINTS:
                values = sorted((record["endpoints"][endpoint]["observed_ns"] -
                                 record["endpoints"][endpoint]["begin_ns"]) / 1e6 for record in complete)
                if values[0] < 0:
                    raise ValueError("negative duration in recorded endpoint")
                row["metrics_ms"][endpoint] = {name: values[math.ceil(fraction * len(values)) - 1]
                                               for name, fraction in QUANTILES}
        result[scenario] = row
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--compare", nargs=2, metavar=("BEFORE", "AFTER"), help="variant labels")
    parser.add_argument("--baseline", type=Path, help="separate artifact directory containing BEFORE")
    parser.add_argument("--json", action="store_true", help="external structured report")
    args = parser.parse_args()
    try:
        current, records = read(args.artifacts)
        result = {"variants": {name: summarize(current, records, name) for name in current["variants"]}}
        if args.baseline and not args.compare:
            parser.error("--baseline requires --compare BEFORE AFTER")
        if args.compare:
            before_name, after_name = args.compare
            baseline, baseline_records = read(args.baseline) if args.baseline else (current, records)
            for key in ("schema", "host", "driver_sha256", "scenarios", "launcher"):
                if key not in baseline or key not in current or baseline[key] != current[key]:
                    raise ValueError(f"comparison refused: unmatched {key}")
            before_spec = baseline["variants"][before_name]
            after_spec = current["variants"][after_name]
            for key in ("profile", "build_identity"):
                if before_spec[key] != after_spec[key]:
                    raise ValueError(f"comparison refused: unmatched {key}")
            before = summarize(baseline, baseline_records, before_name)
            after = result["variants"][after_name]
            if not all(row["valid"] for group in (before, after) for row in group.values()):
                raise ValueError("comparison refused: failed, missing, duplicate or incomplete trials; retain originals and queue failure")
            deltas = {}
            for scenario in before:
                deltas[scenario] = {}
                for endpoint, quantiles in before[scenario]["metrics_ms"].items():
                    deltas[scenario][endpoint] = {}
                    for quantile, old in quantiles.items():
                        new = after[scenario]["metrics_ms"][endpoint][quantile]
                        deltas[scenario][endpoint][quantile] = {"before_ms": old, "after_ms": new,
                                                              "delta_ms": new - old,
                                                              "delta_percent": (new - old) / old * 100 if old else None}
            result["comparison"] = {"before": before_spec, "after": after_spec,
                                    "host": current["host"], "deltas": deltas}
        if args.json:
            print(json.dumps(result, indent=2))
        else:
            print("Nearest-rank quantiles in milliseconds; p99 at 30 trials is the maximum.")
            print("PTY/observer endpoints include scheduling and parsing; no native/display latency claim.\n")
            if "comparison" in result:
                print("| Scenario | Endpoint | Before p50 | After p50 | Δ p50 | Before p95 | After p95 | Δ p95 | Max before / after |")
                print("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
                for scenario, endpoints in result["comparison"]["deltas"].items():
                    for endpoint, quantiles in endpoints.items():
                        p50, p95, maximum = (quantiles[name] for name in ("p50", "p95", "max"))
                        print(f"| {scenario} | {endpoint} | {p50['before_ms']:.3f} | {p50['after_ms']:.3f} | "
                              f"{p50['delta_ms']:+.3f} | {p95['before_ms']:.3f} | {p95['after_ms']:.3f} | "
                              f"{p95['delta_ms']:+.3f} | {maximum['before_ms']:.3f} / {maximum['after_ms']:.3f} |")
            else:
                print("| Variant | Scenario | Attempted / complete / expected | Endpoint | p50 | p90 | p95 | p99 | Max |")
                print("| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: |")
                for variant, scenarios in result["variants"].items():
                    for scenario, row in scenarios.items():
                        counts = f"{row['attempted']} / {row['complete']} / {row['expected']}"
                        if not row["valid"]:
                            print(f"| {variant} | {scenario} | {counts} | unavailable: incomplete/failed | | | | | |")
                        for endpoint, quantiles in row["metrics_ms"].items():
                            values = " | ".join(f"{quantiles[name]:.3f}" for name, _ in QUANTILES)
                            print(f"| {variant} | {scenario} | {counts} | {endpoint} | {values} |")
        return 0 if all(row["valid"] for group in result["variants"].values() for row in group.values()) else 1
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
