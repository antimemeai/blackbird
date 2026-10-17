# Startup question: matched application performance

The previous installed Release generation, source `9e9dad5`, and the repaired
generation, source `9641ef7`, ran together on this Mac. All **120 processes / 960
endpoint observations completed**, with30 trials per variant at110×35 and80×24.
No failed sample, discarded warmup, removed tail or repeated round. A separate
four-process smoke first qualified these existing observer endpoints on this host.

Results are mixed. Typing p50 moves2.805→2.684ms wide and2.262→1.971ms narrow.
Wide paste p50 rises128.003→134.159ms while p95 falls348.587→274.147ms.
Wide resize p50 moves2.181→2.255ms, restore3.774→3.680ms. Narrow startup p95
rises136.903→150.080ms and maximum137.449→167.324ms. These observations do not
establish a general speedup or regression cause. [All16 rows](DELTAS.md),
[derived values](DELTAS.json) and [every raw sample](round/samples.jsonl) retain
both directions. At30 samples nearest-rank p99 equals maximum.

Both binaries use Clang23.1.2, C++23, Lua5.4.8, Release/O3, DebugOFF and no
instrumentation, with the same macOS libc++ runtime2000.67.0. The
[selected cache/library fields](profile.json), [exact source mapping](spec.json),
[previous generation identity](baseline.json) and [full run metadata](round/metadata.json)
identify actual executables, initial/final hashes, host and observer. Both source
commits are published; both fixed generation directories retain their helpers,
Lua and assets. Do not relocate only the executables.

The unchanged retained [observer](../../tui-navigation-2026-10-17/performance/observer.py)
and [report](../../tui-navigation-2026-10-17/performance/report.py) cover startup,
unsubmitted input,4KiB paste, palette open/selection, resize draft, shrink and
restore. These are parent PTY committed-text observations, including scheduling
and parsing. They do not measure physical display latency, native paint CPU,
the new effort question, provider work or shell/launcher startup. Both common-path
variants use valid default effort and submit no inference. Graphics are disabled
only for timing. Microsoft, readable screens, raw originals and real question
semantics have separate qualification in the owning report.

One serial round-robin window ran12:04:57Z–12:05:36Z on2026-10-17. All owned
builds, tests and lint had ended. Shared-host load changed6.024/16.777/17.835→
10.082/16.633/17.740; ordinary host activity remains uncontrolled. This is a new
Mac/C++23 baseline pair, not a numeric continuation of old Linux/C++20 timings.
Keep input/startup tails with existing `arconaut-f8f`; no extra investigation or
retiming is inserted into this repair. Existing AFL is on another host and is
left uninspected.

Complete native sessions, original terminal streams, copied executed observer,
smoke and round remain under ignored
`context/startup-generation-2026-10-17/performance/`. Installed generations are
`generation-z8hyFGNP` and `generation-IW1976SM` under this host's existing prefix.
