# Native presenter tranche performance delta — 2026-10-15

All 120 TUI runs complete normally, recording 960 endpoint observations with no
failed runs, forced shutdowns, interrupted rounds or changed binaries. Median
short-input observations change 2.354→2.422 ms at 110×35 and 1.304→1.376 ms at 80×24.
Wide startup and paste tails worsen substantially in this round. Shared-host
activity was neither inspected nor controlled, so these observations establish
neither a source-causal regression nor a speedup from the ownership change.

The [full endpoint table](linux-release/deltas.md) shows every p50/p95/max
before/after value and absolute delta for all 16 endpoint/geometry combinations.
[Structured analysis](linux-release/deltas.json) additionally preserves p90/p99,
percentage deltas and complete-run counts. Original
[samples](linux-release/samples.jsonl), [metadata](linux-release/metadata.json)
and [executed spec](linux-release/spec.json) are copied byte-for-byte. All 30
trials per source and geometry are included, with no warmup or outlier exclusion.

The material tail observations are retained for the existing collective queue
item `arconaut-f8f`, without a new source-causation bug or product repair:

- Wide startup p95 increases 38.235→150.905 ms and max 69.518→477.668 ms.
  Candidate trials 5/6 observe 477.668/150.905 ms; baseline maximum is 69.518 ms.
- Wide paste p95 increases 60.132→104.216 ms and max 61.619→348.122 ms.
  Candidate trials 6/20 observe 348.122/104.216 ms.
- Narrow startup p95 decreases 157.867→130.939 ms, while max increases
  192.365→277.836 ms. Narrow paste p95 decreases 58.030→55.791 ms, while max
  increases 59.067→95.766 ms. The complete table retains the smaller interaction
  changes as well; mixed tails are not summarized as universal responsiveness.

## Exact source and build boundary

Baseline: Git source `d987f6fb207e47106ce4f5fa6339065e88a89f03`.
Candidate: the root-reviewed ready working-tree overlay on that base, with native
input-tree SHA256
`a12e20b409819e4c761f7de4c2d17669b3286c928d8e5c9299e41f0d6cf721a4`.
The candidate was archived only after root's source-ready signal, and file content
and modes were checked unchanged after archiving. It is recorded as an exact
working-tree snapshot, not a fabricated Git commit. The
[build provenance](linux-release/build-provenance.json) contains compiler/configure
commands, original source-archive hashes and the candidate's complete native file
manifest (path/content hash/size/mode).

Root subsequently reports the required full Mac source gate ends 146/147, with
the existing `/cancel`→exec-settlement PTY failure queued in `arconaut-3xf`; no
full lint phase or source commit follows that failed gate. Affected Linux
Debug/Release/ASan checks pass 27/27. This performance data remains attached to the
exact ready overlay above, independently of any later cancellation diagnosis or
repair. It is not relabeled as measurements of a future repaired source.

Both isolated builds use Ubuntu Clang 18.1.3, libstdc++ 13, Lua 5.4.8 and Release,
`BLACKBIRD_DEBUG=OFF`, `BLACKBIRD_INSTRUMENTATION=none`, from the same verified Lua
source/library. Each build compiles only target `blackbird` with two workers at
nice 10. The observer/native runs use the ordinary execution policy after builds
finish; there is no build running in their owned directories during timing.

| Binary | SHA256 |
| --- | --- |
| Baseline | `c6ff4b2d35ca0780381bc7d2f1af5ff18c4d048ac6d3f2d30d5cae46e10ce2e4` |
| Candidate | `45b4a2e5d333b7abacb527ef33fd97242898894a247a29c3ac20528b6ad4e13e` |

Initial/final content hashes match, and per-trial native device/inode/size/mtime
checks remain unchanged. [Helper identities](HELPERS.tsv) name the exact retained
experimental observer, tertiary report and finite build/snapshot glue. The
observer's `f19ae5fb…` hash, complete host identity and scenario definitions match
the preceding round. Those helpers remain outside active owned source; using
them here records observations and does not reactivate or qualify their release.
No source changes, dependency adoption, new harness framework or AFL interaction
belongs to this measurement unit.

## Timing window and coverage

The 30 serial round-robin trials per variant and size run from 20:01:03Z to 20:01:16Z
on 2026-10-15. Host: `neuroses`, Intel Xeon E5-2697 v2 at 2.70 GHz, x86_64,
Linux 6.8.0-146-generic/glibc2.39 and Python 3.12.3. Root reports all owned Linux
qualification builds completed and cleaned up before timing clearance; both
isolated performance builds also completed before the round. Other shared-host
jobs were left untouched and uninspected under the operator's instruction.
There is no idle-host guarantee. Load averages are 19.23/20.10/19.52 before and
19.68/20.16/19.55 after; these are not per-operation CPU attribution.

The retained observer runs directly on that host, so endpoints do not include
per-action SSH round trips. It starts fresh private native sessions at 110×35 and
80×24, drains PTY bytes and answers terminal queries with Ghostty identity,
C.UTF-8 and graphics off. It observes startup, short unsubmitted input, bracketed
paste with 4080 repeated bytes plus newline and a visible exact tail, slash
palette opening/next selection, short-draft preparation, resize and restoration.
No prompt is submitted. Setup is outside measured endpoints; ordinary caches
remain. Every attempted run is retained.

Parent monotonic timestamps bracket spawn begin to a committed input frame, then
keyboard write or WINCH to a newer committed frame containing the expected
result. These durations include native work, OS scheduling, PTY transport and
observer parsing. They measure neither physical terminal presentation nor isolated
native input-to-paint work. The visible paste-tail oracle does not establish exact
preservation of hidden bytes. Root's independent semantic/PTY/screen tests address
other contracts; these performance observations do not certify those tests or
replace source qualification.

Nearest-rank p99 with 30 trials equals max. No confidence bound, scheduling-noise
threshold or universal latency budget is inferred. This finite fresh-session
round does not measure long-history reopen, provider work, all surfaces, hardware
controllers or input latency while a separate local operation runs. The new
blocked-output fixture provides separate semantic coverage; it is not smuggled
into an unchanged timing baseline.

Full raw PTY transcripts, native sessions, executed observer/spec, source archives,
binaries and build logs remain in the retained owned host directory recorded in
build provenance, with copied public originals in this paper. Ignored local
artifacts are under `context/tui-next-host-2026-10-15/performance/`. The unchanged
retained tertiary report successfully transforms the complete round; no new
review campaign, mismatch test or product optimization is added here. The existing
[comparison rules](../../tui-perf-fuzz-2026-10-15/performance/README.md) and
[tranche plan](PLAN.md) govern the next collection as well.
