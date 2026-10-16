# Final repaired-source presenter performance — 2026-10-15

All 120 TUI runs complete normally, recording 960 endpoint observations with no
failed runs, forced shutdowns, interrupted rounds or changed binaries. Median
short-input observations change 2.389→2.481 ms at 110×35 and 1.401→1.390 ms at
80×24. Wide paste median changes 38.631→39.462 ms; narrow paste changes
38.441→38.465 ms. Other deltas and tails are mixed, under uncontrolled shared-host
activity. Source causation remains unresolved.

The [complete table](final-linux-release/deltas.md) shows every p50/p95/max
before/after value and absolute delta for all 16 endpoint/geometry combinations.
[Structured analysis](final-linux-release/deltas.json) also contains p90/p99,
percentage deltas and complete-run counts. Original
[samples](final-linux-release/samples.jsonl),
[metadata](final-linux-release/metadata.json) and
[executed spec](final-linux-release/spec.json) are copied byte-for-byte.
All 30 trials per source and geometry remain included. There is no separate
warmup, first-touch exclusion or outlier removal.

Wide startup p95 changes 37.566→87.705 ms, while max changes
650.330→162.279 ms: baseline trial 22 has the 650.330 ms observation; final candidate
trials 19/26 observe 87.705/162.279 ms. Wide paste p95 changes 57.193→59.687 ms and
max changes 106.561→96.377 ms. Narrow startup p95/max decrease, while narrow paste
p95/max increase; the complete table retains every smaller interaction delta too.
These ordinary tail observations belong with `arconaut-f8f` for collective action.
This unit adds no optimization, automatic threshold, follow-up timing campaign or
new source-causation bug.

## Exact final source, preserved intermediate

Baseline remains Git source `d987f6fb207e47106ce4f5fa6339065e88a89f03`.
Final candidate is the separately captured ready native overlay with tree SHA256
`7df6ab79a40d4df6ade50f3197360ce0abb36b55f4517f2d9ddb32524ff77200`.
Its full path/content-hash/size/mode manifest, source-archive identity, compiler
and configure/build commands are in
[build provenance](final-linux-release/build-provenance.json).

| Binary | SHA256 |
| --- | --- |
| Unchanged baseline | `c6ff4b2d35ca0780381bc7d2f1af5ff18c4d048ac6d3f2d30d5cae46e10ce2e4` |
| Final candidate | `0e6f53b154a34cd4aec7ff706ae5deb0474fb8b6ae53e412bd9b539a237cecbd` |

The [intermediate a12e20b4 round](RESULTS.md), original files and source identities
remain unchanged. Before reusing its build directory, the incremental driver
copies its exact `45b4a2e5…` binary to `retained-a12e20b4/blackbird`; its original source archive,
manifest and complete round remain available. The provenance records that
retention mapping explicitly. Original metadata is never rewritten to point at
the later executable.

Final source incorporates root's specific launched-command cleanup repair:
raw-wait admission/tick exceptions must not skip the pending stop request for the
launched job. The owner supplies deterministic red cases and the repair; this
measurement unit changes no product code. Compared with the intermediate tree,
only `src/command_jobs.cpp` and `tests/command_raw_test.cpp` content changes.
Rewriting changed inputs with fresh mtimes preserves correct Ninja dependency
tracking. The [incremental build](final-linux-release/incremental-build.log)
performs 10 actions rather than a full clean rebuild, builds `blackbird` and
`command_raw_test` with its actual fixture dependency, and preserves unchanged
inputs. Both before/final builds use the same verified Lua library and Ubuntu
Clang 18.1.3/libstdc++ 13/Lua 5.4.8 Release profile, debug off and native
instrumentation none.

This compares the baseline to the final combined candidate. It does not isolate
frame ownership from the cancellation repair or scheduling effects. The benchmark
never submits work, so it does not time the repaired cancellation path.

## Direct final-source checks

The requested final Release recheck passes 2/2:
`terminal_pty` 9.09 s and `command_raw` 1.38 s, total 10.47 s.
The [original check log](final-linux-release/release-checks.log) and provenance
retain the exact CTest command, executable/fixture hashes and result. Existing
fixture deadlines and assertions remain unchanged.

Root separately reports final-source Linux ASan passes 3/3:
`terminal_pty` 12.33 s, `command_raw` 14.25 s and `command_jobs_engine` 2.33 s,
total 28.91 s. The earlier Debug/Release/ASan 27/27 belongs to intermediate
native tree a12e20b4, before this repair; it is not relabeled as a final-source
rerun. Root's subsequent full Mac check passes 147/147 tests but native analysis
finds an adjacent int/char parameter warning in the test helper. Root changes
`tests/terminal_blocked_fixture.cpp` so `read_control(int)` returns `char` and
callers compare `P`/`F`, then repeats the required source gate. This is the only
measured-native-input difference from tree 7df6ab79 at this publication point;
production inputs and the measured binary remain unchanged. The original
measurement source, hashes and raw files retain their exact 7df6ab79 identities.
Final source qualification remains the owning gate's separate result; these
observations do not claim that gate or lint passed.

## Timing window and limits

The fresh final round runs 20:13:39Z–20:13:52Z on 2026-10-15. Root clears timing
only after the scoped ASan job completes and cleans up, with no root-owned remote
build remaining. This unit's incremental Release build and requested Release
checks also finish beforehand. Other shared-host jobs remain untouched and
uninspected. Load averages are 12.85/14.89/17.12 before and 13.29/14.89/17.08 after;
there is no idle-host guarantee or per-operation CPU attribution.

Host identity, profile, scenario definitions and exact retained observer match
the earlier rounds: `neuroses`, Xeon E5-2697 v2, Linux 6.8.0-146-generic/glibc2.39,
Python 3.12.3, direct native execution, fresh 110×35 and 80×24 PTYs, Ghostty
identity, C.UTF-8 and graphics off. [Final helper identities](FINAL_HELPERS.tsv)
record the experimental observer/report and the finite snapshot/build glue.
Those helpers stay outside active owned tooling. No AFL interaction belongs to
this unit.

Eight endpoints cover startup, unsubmitted input, literal bracketed paste with a
visible tail, palette opening/next selection, short-draft preparation, resize and
restoration. Parent monotonic timestamps include native work, OS scheduling,
PTY transport and observer parsing. They measure neither physical presentation
nor isolated native input-to-paint work. Hidden paste-byte preservation is a
separate semantic/PTY question. No provider request, long-history reopen,
competing-local-work latency or hardware controller is measured here.
Nearest-rank p99 with 30 trials equals max; no confidence bound or universal
latency budget is inferred. Ordinary caches remain and setup stays outside
endpoint intervals.

Full transcripts, native sessions, executed observer/spec, old/final source
archives, binaries and logs remain in the retained owned host directory and
`perf-round-final` named in build provenance. Local working artifacts are under
`context/tui-next-host-2026-10-15/performance/final/`. The unchanged tertiary report
successfully transforms this complete round. It does not trigger another review,
mismatch diagnostic or assurance layer.
