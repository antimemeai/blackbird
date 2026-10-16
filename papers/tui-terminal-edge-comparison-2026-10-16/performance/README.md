# Full physical viewport: policy competition

Use full-width addressed incremental painting as the base. Default-rendition
blank-tail erasure is a qualified bandwidth optimization worth carrying into the
application integration, with a small sparse-edit CPU cost in this fixture.
Keep full clearing for recovery rather than routine frames. Reject this corner
wrap-suppression prototype: Ghostty puts a bottom-right combining mark onto the
wrong cell. These recommendations distinguish demonstrated cell correctness,
deterministic byte costs and noisy timing observations; no policy wins every
latency measurement.

This is a comparison on one native tree, not an application before/after tranche.
**The actual TerminalUI still uses its existing columns-minus-one layout. This
experiment has not repaired the visible gutter in the live application.** The
next integration must own the physical extent and use these results, with its
usual actual-main and historical performance checks. No live session or launcher
was changed.

## Stability first

| Policy | Physical terminal result | Disposition |
| --- | --- | --- |
| addressed | All eligible Ghostty and Alacritty cases pass | Full-width base |
| erase | Same cells/styles/cursor; only default-style blank tails use EL | Qualified optimization |
| full | Same physical result, including resize recovery | Recovery/reference path |
| corner | Ghostty 2x2 bottom-right e+acute becomes e plus an acute on the preceding blank | Disqualified; no timing rank |

The separate [Mac oracle report](../ORACLE_RESULTS.md) retains 319 passing planes
across 17 attempts, the initial cleanup API error, the intended legacy negative
and the corner counterexample. Do not relabel that mixed set as a clean 43-plane
matrix. Independent Linux Release qualification passes 43 full physical planes
for each surviving policy on both backends:258 planes, six clean episodes.
Linux native byte-contract cases plus existing chat_view pass 5/5 in 0.07 s. The
native corner byte contract passing does not establish terminal compatibility.
Every independent review closed after its concrete owning fixes; no further
review panel or rechecks were added for publication.

The corrected matched round completes 360/360 normal exit-zero processes:
180 batch processes, each running six scenarios for 200 frames (216,000 frames),
and 180 PTY episodes with ten actual committed packets each (1,800 packets).
All 2,160 input commands are observed; the 720 blocked edit/resize/quit acquisitions
arrive before stdout is released. There are no failed or forced exits in this
corrected round. Each policy/geometry has 30 trials; all first trials and tails
remain. PTY output is deliberately withheld for at least 120 ms after actual pending
output, while input is still read. That hold is a test condition, not an observed
responsiveness target.

## CPU and byte tradeoffs

CPU values below are median/p95 milliseconds per batch frame. The measured interval
includes common deterministic grid mutations and actual prepare+commit; it is not
isolated painter-only CPU. Base allocation precedes the interval. Native packet
bytes exclude PTY filler and terminal lifecycle controls. Initial full frames are
included in each 200-frame batch, so sparse byte values are amortized per frame.
Blank, style and Unicode batches alternate the paired scenes in the
[scene specification](../PLAN.md); these byte values are averages across that
sequence, rather than the byte count of one particular blank-tail packet.

| Geometry | Scenario | Addressed CPU p50/p95 | Erase CPU p50/p95 | Full CPU p50/p95 | Addressed / erase / full bytes per frame |
| --- | --- | ---: | ---: | ---: | ---: |
| 110x35 | sparse | 0.0174/0.0212 | 0.0189/0.0215 | 0.0567/0.0619 | 71.04 / 53.52 / 4272.00 |
| 110x35 | stream | 0.2451/0.2688 | 0.2529/0.2857 | 0.2444/0.2549 | 4264.00 / 4264.00 / 4272.00 |
| 110x35 | blank | 0.2365/0.2487 | 0.2278/0.4247 | 0.2394/0.2530 | 4264.00 / 2649.50 / 4272.00 |
| 110x35 | style | 0.2416/0.2935 | 0.2298/0.2924 | 0.2412/0.3469 | 4806.50 / 3192.00 / 4814.50 |
| 110x35 | unicode | 0.0657/0.0789 | 0.0513/0.0709 | 0.0579/0.0839 | 4283.50 / 2680.50 / 4291.50 |
| 110x35 | resize | 0.1340/0.2571 | 0.1369/0.1858 | 0.1374/0.1897 | 2404.50 / 2404.50 / 2412.50 |
| 80x24 | sparse | 0.0085/0.0109 | 0.0091/0.0110 | 0.0299/0.0333 | 60.69 / 52.27 / 2221.00 |
| 80x24 | stream | 0.1240/0.1418 | 0.1339/0.1419 | 0.1300/0.1424 | 2213.00 / 2213.00 / 2221.00 |
| 80x24 | blank | 0.1199/0.1316 | 0.1157/0.1218 | 0.1214/0.1279 | 2213.00 / 1464.50 / 2221.00 |
| 80x24 | style | 0.1214/0.2043 | 0.1153/0.1305 | 0.1200/0.1315 | 2585.00 / 1836.50 / 2593.00 |
| 80x24 | unicode | 0.0344/0.0435 | 0.0281/0.0310 | 0.0298/0.0316 | 2232.50 / 1494.50 / 2240.50 |
| 80x24 | resize | 0.0748/0.1003 | 0.0750/0.0845 | 0.0736/0.0795 | 1379.00 / 1379.00 / 1387.00 |

Full clearing costs roughly 3.3–3.5 times the sparse-edit CPU and 37–60 times the
bytes of addressed deltas in these scenes. Its dense-stream CPU stays close to
addressed because those frames already change the whole plane. This supports
keeping explicit recovery without discarding ordinary damage tracking.

Erase reduces blank-scene bytes by 38% wide and 34% narrow; the Unicode scene's
large default blank regions give 37% and 33% reductions. The actual ten-packet PTY
episode shrinks 41,128→31,464 bytes wide (24%) and 21,988→17,518 narrow (20%).
The advantage comes from default blank tails, not colored erasure or assumed BCE.
Sparse CPU medians increase 0.01740→0.01891 ms wide and 0.00849→0.00915 ms narrow
(about 9% and 8%, approximately 1.5 µs and 0.7 µs absolute). This is a tradeoff, not a
claim that erasure is universally faster.

## PTY and blocked-input observations

Each cell below is median/p95 milliseconds. Times run from parent command writes
to complete bracketed terminal bytes observed, or to the immediate native INPUT
acknowledgment observed on the separate event descriptor. They include parent and
native scheduling. They are not GPU paint, human key-to-photon, actual application
input, provider, child-process cancellation or model response timings.

| Geometry | Parent-observed endpoint | Addressed p50/p95 | Erase p50/p95 | Full p50/p95 |
| --- | --- | ---: | ---: | ---: |
| 110x35 | startup | 3.1742/4.1599 | 3.2079/6.2313 | 3.1593/3.7792 |
| 110x35 | blank tails | 0.4696/0.6226 | 0.3784/0.8123 | 0.4663/0.6161 |
| 110x35 | colored blanks | 0.4237/0.5878 | 0.4339/0.8878 | 0.4296/0.5389 |
| 110x35 | Unicode | 0.1585/0.3008 | 0.1167/0.2452 | 0.1694/0.2562 |
| 110x35 | resize narrow | 0.0992/0.1571 | 0.0922/0.2284 | 0.0872/0.1321 |
| 110x35 | resize restore | 0.3714/0.5193 | 0.4095/0.9852 | 0.3713/0.5121 |
| 110x35 | blocked edit acquisition | 0.0387/0.0891 | 0.0416/0.1177 | 0.0367/0.0600 |
| 110x35 | blocked quit acquisition | 0.0353/0.0875 | 0.0348/0.0822 | 0.0320/0.0420 |
| 110x35 | release to latest resized frame | 0.6040/1.2935 | 0.5357/0.9368 | 0.5814/0.8061 |
| 80x24 | startup | 2.7777/3.4267 | 2.6817/3.4259 | 2.7977/3.2215 |
| 80x24 | blank tails | 0.2340/0.3667 | 0.2112/0.2881 | 0.2319/0.3070 |
| 80x24 | colored blanks | 0.2046/0.2516 | 0.2059/0.2948 | 0.2072/0.2763 |
| 80x24 | Unicode | 0.1175/0.1391 | 0.0845/0.1407 | 0.1085/0.1856 |
| 80x24 | resize narrow | 0.0879/0.1141 | 0.0872/0.1298 | 0.0887/0.1290 |
| 80x24 | resize restore | 0.1942/0.2637 | 0.1964/0.2680 | 0.1961/0.2550 |
| 80x24 | blocked edit acquisition | 0.0365/0.0599 | 0.0366/0.0710 | 0.0360/0.0833 |
| 80x24 | blocked quit acquisition | 0.0324/0.0596 | 0.0323/0.0782 | 0.0326/0.0599 |
| 80x24 | release to latest resized frame | 0.3874/0.5946 | 0.3753/0.7583 | 0.3904/0.7279 |

The PTY tails are mixed. Wide erase blank-tail median improves0.470→0.378 ms,
but p95 worsens0.623→0.812 ms; narrow median and p95 both improve. Wide resize
restore p95 is 0.519 ms addressed versus 0.985 ms erase, whereas narrow results are
close (0.264 versus 0.268 ms). Full clearing has some lower tails here despite its
large sparse-byte cost; those endpoint observations do not make it an efficient
routine renderer. Blocked quit p95 remains visible for every policy/geometry,
ranging 0.042–0.088 ms, with every quit acknowledgment observed before drain.
No global responsiveness winner follows from these shared-host observations.
[All 204 metric rows](ALL_METRICS.md) retain p50,p95,max, including the slower tails;
[the structured derivation](ALL_METRICS.json) and [raw samples](round/samples.jsonl)
retain every endpoint. At30 trials p99 is the maximum, so no extra p99 is claimed.

## Original failed attempt and observer correction

The first Linux timing attempt stops after 44 process samples:43 ok and one
collector failure, addressed80x24 trial 4. It has all ten prepared/completed packets
and INPUT 1–12, but the collector polls the exit predicate and immediately polls
again to break on child termination. A child exiting between the two polls
triggers a false wait failure. Its actual final exit code was not recorded and
remains unknown. The [44 original samples](failed-first/samples.jsonl), incomplete
metadata, transcript/event originals and failed timing interval stay separate;
none becomes an excluded tail in the corrected round.

The correction removes that second poll and retains the same 5 s deadline and
explicit exit-zero requirement. It records final return codes on failure as well.
Root stopped its own first source hook before commit and reviewed this concrete
same-unit correction. Actual corrected Linux development qualification passes
12/12 normal exit-zero processes on the unchanged native binary before the new
matched round. The first Mac protocol qualification had passed16/16; that earlier
success did not expose this termination race. No native rebuild or physical-oracle
rerun follows from an observer-only correction. The fresh matched round repairs
an aborted measurement, rather than repeating a valid result to improve numbers.
[Exact correction](observer-exit-correction.patch), [qualification](observer-qualification-corrected/metadata.json).

## Exact scope and restoration

| Input | Identity |
| --- | --- |
| Native source base | `4af196d` |
| Compiled snapshot tree | `eef11f879462e6c33e5112f3c9ad3a13c4983ef4ba4c59e3299e9bab9aa5c026` |
| Source archive SHA256 | `d6bef5c1c68e58f3e43b2001fb805c670d7a90b04afec7fe7c90284d9f94384f` |
| Fixture binary SHA256 | `8ee33392e97391b2b83804a2ddba0f6d6c4fa56f5e3b1d6e6491fc121f268a40` |
| Corrected observer SHA256 | `ba06db9f2f6624ae647a88fcbf241f6fee1a6689d9bdb1cb19c167a3f6c83854` |
| Report SHA256 | `078e94684c8618185221bf62acf9aa88668282804475135e768437290ceeb87f` |
| Qualified Lua 5.4.8 library SHA256 | `95fa79336051315a656c56e0271a2990ffb598adf6e8b1987ae0e239efff7fc2` |
| Delivered source commit | `0c62e9641a154bb90c8367368d2dff6d43b7c68f` |

Timing runs 2026-10-16T20:57:13.454351+00:00 through 2026-10-16T20:58:09.424535+00:00 on `neuroses`,
Linux-6.8.0-146-generic-x86_64-with-glibc2.39. The single binary uses Clang 18.1.3/libstdc++ 13/Lua 5.4.8 Release,
DebugOFF and instrumentation=none. Load averages are [4.9833984375, 4.72998046875, 4.93994140625] before and
[4.01171875, 4.49951171875, 4.84765625] after. Owned remote builds/checks finish before the measured
window; unrelated shared-host activity is uncontrolled. The local Mac source
hook runs on a different host. No AFL inspection or global-idle claim is made.

The compiled snapshot contains the original observer. The actual corrected round
uses the explicitly hashed uploaded observer above. Final source additionally
changes only CMake's physical test registration to select the three survivors;
[the two exact differences](delivered-source-delta.patch) do not change native
algorithms, scenes or packet transport. The source archive is not relabelled as
the final delivered tree, and no timing is repeated for those registration/docs
changes. Final source gate/commit status is supplied by the parent tranche report.

All 180 corrected terminal originals (7,487,010 bytes), native event bytes,
first44-sample originals, corrected12-process qualification, Linux physical
responses/recordings, three compiled binaries and provenance are fetched under
ignored `context/terminal-edge-comparison-2026-10-16/fetched/`. The original bundle
is 6787141 bytes, SHA256
`1f80b05d7ff78d61ca9cb88d248ea5c5b48f1895df79a8bb8e01b604fd72158e`.
Persistent remote originals remain at:
`/home/patrick/.local/share/blackbird-campaigns/tui-terminal-edge-20261016-qdv85I8D`.
Do not relocate binaries with their embedded sibling helper paths. Original
source archive/source manifest, build commands, helper identities and
[case ledger](CASES.tsv) make this comparison restorable. The [Lua source restoration catalog](../../../QUARANTINE.md) records the intact5.4.8 archive and restoration command. Large originals stay
ignored; raw endpoint samples and exact source/observer changes are published.
