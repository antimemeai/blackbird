#!/usr/bin/env python3
"""Compare native styled-grid expectations; baseline updates are explicit only."""
import argparse
import difflib
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--case", help="compare just this named fixture")
    update = parser.add_mutually_exclusive_group()
    update.add_argument("--update", metavar="CASE", help="explicitly replace one baseline")
    update.add_argument("--update-all", action="store_true", help="explicitly replace all baselines")
    args = parser.parse_args()
    expected = Path(__file__).resolve().parent / "expected"
    args.artifacts.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="tui-actual-") as temporary:
        actual = Path(temporary)
        subprocess.run([str(args.binary.resolve()), str(actual)], check=True)
        cases = {path.stem: path for path in actual.glob("*.snap")}
        if not cases:
            parser.error("native fixture producer emitted no cases")
        selected = args.update or args.case
        if selected and selected not in cases:
            parser.error(f"unknown case {selected!r}; available: {', '.join(sorted(cases))}")
        names = [selected] if selected else sorted(cases)
        if args.update or args.update_all:
            expected.mkdir(parents=True, exist_ok=True)
            for name in names:
                (expected / f"{name}.snap").write_bytes(cases[name].read_bytes())
            print(f"Explicitly updated {len(names)} baseline(s); review their diff.")
            return 0
        failures = 0
        if not selected:
            removed = {path.stem for path in expected.glob("*.snap")} - set(cases)
            for name in sorted(removed):
                print(f"FAIL {name}: expected fixture no longer emitted")
                failures += 1
        for name in names:
            baseline = expected / f"{name}.snap"
            observed = cases[name].read_text(encoding="utf-8")
            wanted = baseline.read_text(encoding="utf-8") if baseline.exists() else ""
            if baseline.exists() and observed == wanted:
                continue
            failures += 1
            actual_path = args.artifacts / f"{name}.actual.snap"
            diff_path = args.artifacts / f"{name}.diff"
            actual_path.write_text(observed, encoding="utf-8")
            difference = "".join(difflib.unified_diff(
                wanted.splitlines(keepends=True), observed.splitlines(keepends=True),
                fromfile=str(baseline), tofile=str(actual_path)))
            diff_path.write_text(difference, encoding="utf-8")
            print(f"FAIL {name}: {diff_path}\n{difference}")
        print(f"{len(names)} styled snapshot comparisons; {failures} failure(s)")
        return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
