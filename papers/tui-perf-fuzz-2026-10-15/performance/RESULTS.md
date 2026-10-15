# First TUI tranche performance delta — 2026-10-15

All 120 fresh native processes complete normally with zero failures, forced
shutdowns or changed executables. The 960 endpoint observations compare exact
pre-tranche source 4da8355 with b0b4759 on the same Linux Release host/profile and
new observer. Median short-input responses are close: 2.302→2.322 ms at 110×35 and
1.293→1.313 ms at 80×24. Larger startup/paste tails remain visible and deserve
retained follow-up; these measurements do not establish their cause.

The complete [derived table](linux-release/deltas.md) and
[structured deltas/quantiles](linux-release/deltas.json) include all eight
endpoints at both sizes. Exact public originals are
[per-process samples](linux-release/samples.jsonl),
[host/source/scenario metadata](linux-release/metadata.json) and
[executed spec](linux-release/spec.json). [TRANCHES.tsv](TRANCHES.tsv) records this
round for subsequent delivery comparisons. No original or scheduling outlier is
excluded.

| Size / endpoint | Before p50 ms | After p50 ms | Delta ms | Before p95 ms | After p95 ms | Max before / after ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 110×35 startup | 22.074 | 21.970 | −0.104 | 26.466 | 146.343 | 28.052 /164.989 |
| 110×35 input | 2.302 | 2.322 | +0.020 | 3.099 | 3.232 | 4.119 /4.161 |
| 110×35 paste | 38.157 | 38.264 | +0.107 | 56.973 | 54.860 | 58.565 /234.904 |
| 110×35 palette opening | 1.505 | 1.474 | −0.031 | 2.095 | 2.077 | 9.483 /2.156 |
| 80×24 startup | 21.232 | 20.986 | −0.246 | 51.760 | 23.565 | 69.985 /41.849 |
| 80×24 input | 1.293 | 1.313 | +0.020 | 1.855 | 1.953 | 2.187 /3.130 |
| 80×24 paste | 37.575 | 37.009 | −0.567 | 54.094 | 57.420 | 201.296 /101.384 |
| 80×24 palette opening | 1.413 | 1.442 | +0.029 | 2.086 | 2.022 | 2.442 /3.749 |

Current-source wide startup trials 24/25 observe 146.343/164.989 ms; all before-source
wide startup trials are at most 28.052 ms. Current wide paste trial 10 observes
234.904 ms, while its p95 is 54.860 ms. Before-source narrow trial 24 also has a
69.985 ms startup and 201.296 ms paste; current narrow maximums are lower. Preserve
these exact observations for the collective queue rather than labeling the
median as universal responsiveness or treating one tail as a diagnosed source
regression. No product fix, automatic performance threshold or additional timing
campaign belongs to this collection unit.

## Build and host boundary

The 30 trials per source and size run serially in round-robin order between
18:26:45Z and 18:26:56Z. Host: `neuroses`, Intel Xeon E5-2697 v2 at 2.70 GHz,
x86_64, Linux 6.8.0-146-generic/glibc2.39, Python 3.12.3. Both native binaries use
Clang 18.1.3/libstdc++13/Lua5.4.8 Release. Source/build/driver identities are
retained in the metadata, including full commits:

- Before: `4da8355f490bc9a987f3cd8c8c5d0d9a7176c8be`, native SHA256
  `61dba5a06df97403066c9f7c5d4c5b5496d80fbbc1f58a9958724c26437fb36b`.
- After: `b0b4759465df543bd6d383d111aa64bb2f86d662`, native SHA256
  `97fb7066b19456b2ee1c8d1e55036235b48b4d6383c862ee6658244e83c4c00e`.

Before/after each trial native device/inode/size/mtime remain unchanged, and final
content hashes match initial binaries. The executed observer hash matches the
`bench-tui` candidate retained under ignored context (see [source status](../SOURCE_STATUS.md)). No repository launcher is used.
Root reserved this window without this campaign's builds/fuzz workers; other
operator/system activity remains uncontrolled. Load averages are
21.82/18.83/16.63 before and 21.34/18.87/16.68 after. Ordinary OS caches remain;
fresh private session setup is excluded, no first-touch sample is discarded.

Each process observes startup, short unsubmitted input, bracketed paste with 4080
repeated bytes plus a newline and visible exact tail, slash palette opening/next
selection, short-draft preparation, resize and restoration. The terminal is a
PTY with Ghostty identity, graphics off and a partial VT text observer. Resize
requires the draft in the third-last row and closing border in the second-last
row, distinguishing new geometry from old text. No prompt is submitted. Full
PTY/native-session artifacts remain in the retained host `perf-round` directory;
this paper preserves the copied metadata, exact spec and every process sample.

## Interpretation and qualification

The parent clock brackets spawn begin to a committed input frame, then keyboard
write or WINCH to a newer committed frame containing the expected result. These
observations include native work, OS scheduling, PTY transfer and observer
parsing. They do not measure physical terminal presentation or isolate native
input-to-paint spans. Visible tail observation does not establish exact
preservation of every hidden paste byte; semantic/PTY suites own that question.
The workload does not cover long-history reopen, provider work, every interface,
real MIDI/haptics, or input responsiveness during competing local operations.
Nearest-rank p99 at 30 trials equals max; no confidence bound or fixed latency
budget is inferred. Historical 2026-10-14 measurements have different observer
and scenario boundaries and remain descriptive, without numerical conversion.

Direct qualification includes Python 3.9.6 bytecode/help checks, the corrected
actual Linux smoke passing all four source/size combinations, and root's actual
TERM diagnostic: stop an owned native child, terminate the observer, retain its
interrupted sample, reap the child and refuse comparison of that round. The
planned mismatched-profile diagnostic uses a private copy of this actual round
with only the after profile deliberately changed; the report exits 1 with
`comparison refused: unmatched profile`. The changed copy remains in ignored
context and is not published as a measurement. The unchanged actual round
reports successfully. No new test framework or review layer was added.

The first smoke's paste predicate incorrectly required an unbroken row marker;
its transcript shows the full expected tail across two rows. Add a newline before
the marker inside literal paste. Correct an initial final-row assumption for the
reserved footer. Preserve that failed smoke without a product-defect claim or
enlarged deadline. Root's one independent source challenge then identifies two
P2 tooling boundaries: TERM cleanup and native identity changing after its first
hash. Install interruption-safe PID assignment/cleanup and diagnostic persistence;
check native identity around each trial and final content hashes. The direct
corrected smoke and TERM checks conclude that bounded remediation/recheck unit.
