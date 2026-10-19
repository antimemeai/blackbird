#!/bin/sh
# drive.sh: extract each run's session one at a time into scratch, measure every raw run, delete the copy.
S=/private/tmp/claude-501/-Users-patrickbeam-projects-blackbird/7a9f433b-5f15-4206-8722-548bda7b4ce9/scratchpad/request-sizes
A=$HOME/projects/blackbird_runs/2026-10-17-cpp26
RR=$HOME/projects/blackbird/context/cpp26-toolchain/build/release/blackbird-raw-reader
mkdir -p "$S/out" "$S/extract"
for t in "$A"/*/session.tar.zst; do
  run=$(basename "$(dirname "$t")")
  if [ -f "$S/out/$run.done" ]; then continue; fi
  X="$S/extract/$run"
  rm -rf "$X"; mkdir -p "$X"
  tar -xf "$t" --use-compress-program=unzstd -C "$X" || echo "$run tar_failed"
  for d in "$X"/session/raw/bb*; do
    [ -d "$d" ] || continue
    b=$(basename "$d")
    "$RR" records "$d" | python3 -I "$S/sizes.py" "$run" "$b" > "$S/out/${run}__$b.jsonl" 2> "$S/out/${run}__$b.err"
    echo "$run $b status=$? $(cat "$S/out/${run}__$b.err" | tail -1)"
  done
  rm -rf "$X"
  touch "$S/out/$run.done"
done
echo ALL_DONE
