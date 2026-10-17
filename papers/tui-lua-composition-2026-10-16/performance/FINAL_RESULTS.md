# Lua retained-result tranche: matched Linux Release performance

One matched round completed:120/120 processes, zero failed/forced/interrupted
trials,30 per source and geometry (110x35 and80x24), serial round robin. Every
sample, terminal transcript and session remains available locally and at the
persistent owned campaign path. No warmup, tail or failure was discarded, and no
repeat was run to improve results. No owned remote build/check overlapped timing.
The host is shared; these observations do not identify the cause of changed tails.

## Exact inputs and qualification

| Input | Identity |
| --- | --- |
| Baseline committed source | 0bf6712afea66989e94ba9601205888cd5ca342c |
| Baseline native tree,442 files | 193606716b1129964ee6e364b00d3e6b8da198384220050a155ccd42e712c5ec |
| Baseline native binary | 9bd9d9b5ac95545c9c2f32ba0752fedd31c662e9e8783c53cbda9762426717f5 |
| After compiled native tree,457 files | 34c5d5a1b17959bcf491c9629416e9b5cab527ad2fe168be077629eef487fb0e |
| After native binary | e918c16dbbdb6a07c633ae88f112ffd9c1b609e0fed46c4d9978089280031d04 |
| After qualified fixture tree | b4483f0c233bef889d1ecb0e746af05eafb16933e180f07ca62fa81611a73230 |
| Default authored results.lua | 89c85f88a915b32e944bf43d6c4e4f988553f2f7606d5cdae3e29da89a16868a |
| Observer | f19ae5fb117cf969836301d8002336aa642d2476bac3bdee6f2248d50fbde377 |
| Report | dd4227cf1b29825ccc758f3aeb6da39fab23670310149b1d1935f585eb73650c |

Both isolated builds use the qualified Clang18.1.3/libstdc++13/Lua5.4.8 Release,
BLACKBIRD_DEBUG=OFF and instrumentation=none profile. The Lua library is
95fa79336051315a656c56e0271a2990ffb598adf6e8b1987ae0e239efff7fc2;
its original archive is
4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae.
Baseline/native helpers remain at their original embedded paths, rather than
relocating a binary with mismatched dependencies. All companion hashes and
configure/build commands are in FINAL_RESULTS.json and original provenance.

Baseline qualification remains the previously observed13 combined cases plus
one real PTY pressure check; none was repeated for this measurement. Root's
separate native25 counterpart remained distinct. The after build completed106/106;
its initial two-check attempt passed snapshots and failed actual main because the
workflow's transient INSTRUMENT_FIXTURE_READY print had already scrolled away
under later original output. The native completed-turn status, returned-ID file,
both child launches and B's output were present. The original failed checks,
frame, effects, source archive and false checks_completed field are retained.

The explicitly reviewed correction waits for persistent completed-turn status,
actual B output and the native-returned-ID file together. It preserves the4s
allowance, exact ID/byte/effect assertions, producer output and first old-main RED.
Root observed the same transient failure in the first Mac source gate. After the
one-script recorded overlay, Linux checks passed2/2 in23.97s: actual main23.89s,
snapshots0.07s. Corrected Mac Ghostty independently passed its full episode. No
native binary was rebuilt for this fixture correction. The after binary therefore
names compiled tree34c5..., while application-check source has explicit overlay
tree b448...; both full manifests and archives are retained separately.

A later final-source optional guard in tests/tui_snapshot_test.cpp is a further
test-only spelling change. Its exact before/final hashes and patch are retained in
final-test-only-delta.json/.patch. It changes neither product/library/default Lua
nor measured observer scenarios. Delivered source `3e3a561b89b712e52f586d5727f8a1aebfd84ff2` passed the installed source hook.
The additional bridge fixture correction waits only on documented pre-operation
busy reads/trigger allocation and preserves exact handoff/order assertions.
All three test-only differences are in delivered-test-delta.json/.patch; this report does not relabel the original source
archive or claim all native-tree inputs equal that later commit.

## Entire common delta

Nearest-rank quantiles in milliseconds; p99 at 30 trials is the maximum.
PTY/observer endpoints include scheduling and parsing; no native/display latency claim.

| Scenario | Endpoint | Before p50 | After p50 | Δ p50 | Before p95 | After p95 | Δ p95 | Max before / after |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 110x35-fresh | startup | 24.726 | 27.019 | +2.293 | 31.681 | 101.848 | +70.167 | 468.819 / 102.365 |
| 110x35-fresh | input | 2.741 | 2.737 | -0.004 | 3.931 | 3.941 | +0.011 | 4.439 / 18.122 |
| 110x35-fresh | paste | 41.905 | 42.284 | +0.379 | 51.629 | 76.044 | +24.415 | 60.761 / 299.929 |
| 110x35-fresh | palette_open | 1.698 | 1.684 | -0.014 | 2.507 | 2.459 | -0.048 | 5.720 / 2.679 |
| 110x35-fresh | palette_next | 1.230 | 1.156 | -0.074 | 1.494 | 1.591 | +0.097 | 1.589 / 1.608 |
| 110x35-fresh | resize_draft | 1.435 | 1.386 | -0.049 | 2.035 | 1.889 | -0.146 | 2.094 / 2.027 |
| 110x35-fresh | resize | 3.165 | 3.246 | +0.081 | 4.168 | 4.683 | +0.515 | 5.085 / 6.921 |
| 110x35-fresh | resize_restore | 3.917 | 3.931 | +0.014 | 6.402 | 6.179 | -0.223 | 6.773 / 8.307 |
| 80x24-fresh | startup | 23.018 | 26.614 | +3.597 | 27.504 | 30.047 | +2.542 | 28.871 / 72.700 |
| 80x24-fresh | input | 1.539 | 1.518 | -0.021 | 1.770 | 2.098 | +0.328 | 1.844 / 2.150 |
| 80x24-fresh | paste | 41.873 | 40.944 | -0.929 | 46.975 | 61.350 | +14.375 | 99.618 / 69.704 |
| 80x24-fresh | palette_open | 1.670 | 1.632 | -0.038 | 2.068 | 1.918 | -0.150 | 2.285 / 2.580 |
| 80x24-fresh | palette_next | 1.171 | 1.126 | -0.045 | 1.344 | 1.810 | +0.466 | 1.391 / 2.081 |
| 80x24-fresh | resize_draft | 1.294 | 1.293 | -0.001 | 1.674 | 1.941 | +0.267 | 1.845 / 2.020 |
| 80x24-fresh | resize | 1.442 | 1.429 | -0.013 | 1.873 | 1.810 | -0.063 | 2.172 / 2.178 |
| 80x24-fresh | resize_restore | 1.905 | 1.892 | -0.013 | 2.565 | 2.245 | -0.321 | 2.980 / 2.294 |

Both startup medians rose:24.726→27.019ms wide and23.018→26.614ms narrow.
Wide startup p95 rose31.681→101.848ms while its max fell468.819→102.365ms.
Paste p95 rose in both geometries (51.629→76.044ms wide,46.975→61.350ms narrow);
wide paste max rose60.761→299.929ms, while narrow max fell99.618→69.704ms.
Input medians stayed close (2.741→2.737ms wide,1.539→1.518ms narrow), with
wide input max rising4.439→18.122ms. Every other direction, including improvements,
remains in the complete table and raw JSON. No blanket responsiveness or
nonregression conclusion follows from these mixed observations.

## Original records and bounds

Timing ran2026-10-16T15:18:35Z through15:18:48Z on neuroses,
Linux6.8.0-146-generic/x86_64, XeonE5-2697v2. Load averages before were
[5.177246,4.905273,5.077637], after [5.366211,4.959961,5.093262]. These are
context, not a shared-host exclusivity or source-causation argument. All120
terminal originals (2,374,200bytes),120 session directories and120 raw endpoint
records are fetched under perf-round/, with remote originals untouched. Hashes
stayed fixed for each native variant throughout observation. No inference prompt
was submitted.

The unchanged observer measures parent PTY committed-text endpoints for startup,
ordinary input, paste, palette and resize. It does not measure instrument-open,
Lua composition, selected-result refresh or action latency. Functional real-main
and native owner checks supply separate facts. No after-only timing scenario was
invented by changing the common observer's semantics. p99 at30 is the maximum.
Physical gutter residue remains queued as arconaut-arw; arconaut-14o remains an
unexplained abort. This campaign resolves neither issue and makes no claim of
flawless physical presentation or complete TUI coverage.

The installed source gate and native owning qualification are reported in
[the tranche report](../README.md). Large terminal/session originals and source
archives stay under ignored context and at the persistent remote campaign path;
the tracked raw endpoint samples, manifests and exact test delta remain here.
No additional performance round was run after test-only corrections.
