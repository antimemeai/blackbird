# Paging and navigation application performance

One matched Release round compares committed baseline
`cbb206b629d936c13c9636b0a7bac999fa68e3e9` with the paging, shared view navigation and named-action
candidate. All **120 processes / 960 endpoint observations completed**, with
30 trials per variant and geometry, zero failures, no discarded warmups/tails
and no repeat. Source delivery is **`064e794ea1665f241c82e92b431c689290deb205`**.
[The profile](profile.json) and [exact source mapping](delivered-source-delta.json)
identify the compiled source and its one test-driver-only qualification difference.

Results are mixed. Typing p50 rose 2.644→2.780 ms at 110x35 and
1.331→1.379 ms at 80x24. Wide shrink p50 moved 5.590→5.620 ms, wide restore
5.937→5.823 ms and narrow restore 3.458→3.580 ms. Wide paste p95 fell
122.708→57.016 ms and narrow paste p95 50.873→45.367 ms. Wide startup maximum
rose 40.963→388.296 ms, while narrow startup p95 fell 157.814→66.976 ms.
These are observations on a shared host, not causal attribution or a blanket
speedup/nonregression claim. Retain startup/input tails for the existing
`arconaut-f8f` performance investigation rather than retiming this valid round.
[All sixteen endpoint rows](DELTAS.md) and [raw records](round/samples.jsonl)
retain the adverse directions. At 30 trials p99 equals the maximum.

The unchanged historical [observer](observer.py) and [external report](report.py)
cover startup, unsubmitted text, 4 KiB bracketed paste, palette open/selection,
resize draft, shrink and restore at 110x35 and 80x24. Each endpoint is a parent
PTY committed-text observation including scheduling and parsing. These common
scenarios do not measure the new command-list paging or view-picker latency,
isolated native paint CPU, GPU/display, providers or model latency. No inference
turn was submitted. Graphics were disabled only in this timing workload.

The baseline passed six selected Linux native/rendering/custody/blocked/editor
checks first attempt. The candidate passed all sixteen explicitly enumerated
[changed-path Linux checks](candidate-qualification.log), including seven
instrument modes with paging, readable snapshots, native terminal, raw UI,
blocked output, editor, graphics, custody and the retained-result actual-main
PTY episode. That semantic episode exercises the new navigation independently
of the historical common-path observer. This performance report adds no Mac or
whole-suite certification claim.

Both binaries use Clang 18.1.3, libstdc++ 13, C++20 Release, DebugOFF,
instrumentation=none and identical previously qualified Lua 5.4.8 files.
[Build/profile provenance](profile.json), [baseline source](baseline-source.json),
[candidate source](candidate-source.json) and [their diff](candidate-baseline.patch)
preserve exact inputs. The native-scope source identity hashes sorted
path/content/byte-count/executable-bit records; manifests also retain full
original file modes. Baseline tree is `0c3b16a1a8114e016e655507baa2eceaf9205616beab28eee1e49906e4c0ea7e`; compiled candidate
is `c4966e284ce086f9c09d2edb770cf30628d65b581b2e6b10c65c28a77ca65c82`. Candidate qualification uses one later test-driver
correction to disable only child VQUIT so literal Ctrl-backslash reaches the
fixture while ISIG/VINTR remain active. [Its exact patch](qualification-driver-delta.patch)
does not alter compiled production code, build-hash scopes, Lua or timing helpers.
The qualified source tree is `66dee5a207bff34b4806a463b3c5fd684b9ddc23ec6fb105852c8f2fe03356f2`.

One serial round-robin window ran on Neuroses at
2026-10-17T04:35:59Z–2026-10-17T04:36:15Z. Every owned remote build/check had
ended. Shared-host load averages changed from [4.62646484375, 5.26123046875, 5.640625] to
[5.28955078125, 5.37451171875, 5.669921875]; no unrelated process survey or global-idle claim was
made. No live launcher/session activation occurred.

The 120 original terminal streams (3,286,800 bytes), isolated
sessions, ELF binaries, original source archives and overlaid qualification
driver remain under ignored `context/tui-navigation-2026-10-17/fetched/` and
persistent remote `/home/patrick/.local/share/blackbird-campaigns/tui-navigation-20261017-p8UYMqsE`. [The originals bundle](originals.json)
records its intact archive identity. Do not relocate executables alone: embedded
sibling helper/source paths matter. Extract
`context/tui-navigation-2026-10-17/originals.tar.gz` into new scratch storage to
inspect; restore the recorded campaign path, source archives, Lua files and
build commands when recreating its runnable layout.
