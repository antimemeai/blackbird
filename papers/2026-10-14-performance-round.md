# Performance round assessment — 2026-10-14

The requested full finite round is complete on repaired source **66547c6**
(product repair ee4efc6), with Release/OFF builds on macOS ARM64 and Linux
x86_64. Startup remains quick. Prepaid burst emission remains inexpensive, but
complete owner calls and disk preservation have material tails. An actual File
hook integration gap remains open as **arconaut-4by**. No product source changes,
performance optimization, new dependency or external provider/eval run occurred.

The round includes 62 raw workload cases, 84 matched owner cases with 8,400 actual
calls, 150 startup/input trials, two complete native command-control sessions,
and one separate producer syscall trace. Raw cases preserve 1,039,144 exact-byte
events, plus 10,000 traced events. Final owner datasets check 458,952 closed raw
records and 7,200 independently prepared originals across their capture-active
cases. All startup and control oracles pass. Owner measurements cover the
existing paths; they do not qualify the omitted nested File hooks.

## Startup and native control

| Host | Fresh startup median / p90 | Reopened median / p90 | Fresh typing median |
| --- | ---: | ---: | ---: |
| macOS | 124.849 / 157.626 ms | 145.414 / 223.347 ms | 2.643 ms |
| Linux | 31.458 / 35.529 ms | 173.807 / 193.596 ms | 1.216 ms |

The reopened session contains 100 actual local workflow-registry calls in one
Lua turn, with independently checked request/result links. This is a finite
local-tool-history fixture. The retained previous-segment macOS executable
measures 125.370 ms median in the same round; it is not the exact source parent,
so the comparison does not isolate instrumentation cost. Every sample remains,
including fresh maxima of 210.385 ms macOS and 57.592 ms Linux.

Both actual background/foreground sessions pass exact PID/launch, input/effect,
resize, Ctrl-C, task separation, full-queue control, quit/reap and restored-mode
oracles. All 319,234 macOS and 348,282 Linux transferred outer-keyboard bytes equal
closed native disk originals. Under saturated input queues, stop-to-child-reap
observations are 62.156 ms and 22.492 ms. Detach plus a subsequent local Lua result
is 147.629 ms and 308.505 ms; that combined endpoint does not isolate detach time.

## Raw transport and preservation

At launcher grants, three burst trials give prepaid emit medians of 42 ns for
64/256-byte bodies and 250 ns for 4 KiB on macOS. The small-body result sits at
its 42 ns empty-pair observer floor. Linux observes 81–98 ns for small bodies
and 3.40–3.44 µs for 4 KiB at the launcher grant. The Linux 1 MiB pressure-grant
4 KiB median is 429–438 ns. Fresh mappings and scheduling remain part of these
observations; no causal attribution or capacity choice follows.

| Host / DATA grant | 128 MiB producer loop | Complete preservation | Body throughput | Final-event observed ACK lag |
| --- | ---: | ---: | ---: | ---: |
| macOS / 1 MiB | 346.78 ms | 349.19 ms | 384.38 MB/s | 2.41 ms |
| macOS / 256 MiB | 78.90 ms | 475.19 ms | 282.45 MB/s | 396.28 ms |
| Linux / 1 MiB | 930.00 ms | 940.16 ms | 142.76 MB/s | 10.16 ms |
| Linux / 256 MiB | 229.18 ms | 870.63 ms | 154.16 MB/s | 641.45 ms |

Larger grants let the producer get far ahead of preservation. Both concurrent
reader cases per host check every event. The actual stopped-recorder/full/refusal/
resume cases preserve every event, with resume-through-next-emission observations
of 0.928 ms macOS and 2.780 ms Linux. Separate producer allocation censuses observe
zero C++ new calls/bytes across 40,000 claim/emit pairs. That does not count malloc,
recorder allocation or complete owner hooks. A separate Linux trace observes 56
producer syscalls across 10,000 events; tracing is excluded from latency tables.
Copy, atomic-RMW and kernel wake/context-switch counts remain unmeasured.

## Actual owner calls

Each size and capture state has one batch of 100 calls. Capture order alternates
between off/on and on/off; there is no affinity, cache flush or excluded outlier.
The table shows complete-call medians for 256-byte bodies, including real
business work and existing synchronous audit writes. These are matched local
observations, not isolated hook instruction costs or causal regression budgets.
The provider is an in-process fixed-response fixture, not a network provider.

| Actual call | macOS capture off → on | Linux capture off → on |
| --- | ---: | ---: |
| File read | 0.016 → 0.025 ms | 0.008 → 0.008 ms |
| File write | 0.220 → 0.276 ms | 0.522 → 0.522 ms |
| Operator read_file | 33.593 → 35.919 ms | 3.046 → 3.918 ms |
| Lua read_file turn | 33.623 → 34.947 ms | 3.356 → 3.225 ms |
| Local mock-provider turn | 51.677 → 61.410 ms | 6.059 → 6.871 ms |
| Session settings save | 4.256 → 5.134 ms | 0.346 → 0.358 ms |
| Station admit/finish/prompt | 8.840 → 10.094 ms | 0.554 → 0.676 ms |

Large tails are retained: macOS capture-on operator at 256 bytes reaches 456.122 ms,
and capture-on Station at 256 bytes has 151.246 ms p99 / 153.935 ms max. These are
100-call batch observations, not established tail limits. The owner driver uses
256 MiB DATA / 2 MiB CONTROL / 128 KiB FAILURE, whereas the launcher uses 1 MiB
CONTROL. Construction is outside the call timing; owners are initialized before
capture starts. Neither standalone lifetime cost nor every production lifecycle
is measured by those call batches.

The initial operator/Lua oracle rejects absent nested File events on both hosts.
Source study confirms CodingEngine calls LocalTools::run without its optional
RawAdapterContext. Whole-call input/result and turn/bridge events remain; literal
native caller input/result bytes and exact business results are checked in the
final measurements. The missing nested File tool/helper/native-syscall emissions
remain a defect, not a passing qualification. The bead includes a context-
propagation plan and actual integration oracles; no fix is implemented here.
Three dev-driver expectations also needed source-grounded correction: the actual
operator wrapper source, mock-provider decoded-native-result versus literal
JSON diagnostic bytes, and Station's unconditional empty-steering separator.
Earlier failures and their raw audits remain. Initial failed owner attempts
precede per-call CSV writing and lack those timing samples; successful final
case samples are retained. Settled successful cases are not repeated.

## Results and next discussion

Detailed [raw transport results](2026-10-14-raw-transport-perf-round.md),
[interactive results](2026-10-14-interactive-perf-round.md) and
[owner results](2026-10-14-performance-owner-round.md) link to exact
executed driver snapshots, commands, host/native/archive hashes and complete
selected datasets. Tracked text selections normalize line endings and trailing display whitespace;
original native outputs remain unchanged in context. Full native audits, timing samples and PTY transcripts remain
in context/perf-round-2026-10-14, including copied Linux archives. This report
completes the requested measurement round; it does not close nls.2 or select
optimizations. Discuss the observed complete-call tails, preservation backlog,
Linux reopening and the File integration defect before further performance work.
Broader SystemA boundaries, logical clocks and audit-completeness work remain
separate, along with the operator's eval/trajectory/multiplayer deferrals.


## Follow-up: actual wiring and event production

Read existing preserved runs; no native test or performance campaign is repeated.
The actual launcher starts RawRuntime and binds its producer during normal
session startup (src/main.cpp:236). Real CLI/PTY runs therefore produce events
without the owner benchmark manually opting each surface in. Explicit native
File, Session and Station benchmark calls also supply their adapter contexts;
they are API-path measurements, not proof that every upstream caller propagates
those contexts. CodingEngine's omitted LocalTools context remains arconaut-4by.

For each host and body size, the capture-active owner batch produces these
closed raw event counts per100 operations, including run overhead:

| Actual owner operation | Events per100 operations |
| --- | ---: |
| Native File read | 1,201 |
| Native File write | 2,601 |
| Operator read_file | 15,901 |
| Authored Lua read_file turn | 15,301 |
| Local mock-provider turn | 35,086 |
| Session settings save | 1,401 |
| Station admit/finish/prompt triplet | 5,001 |

The actual100-call workflow-registry seed emits91,183 events into13,522,365 bytes
of event journal, including startup and shutdown. Its100 tool request/result
pairs,100 workflow lookup request/result pairs and101 bridge pairs are present.
Native storage/catalog hooks dominate its event count. The complete native PTY
control sessions emit653,756 events into115,771,006 event-journal bytes on macOS,
and692,862 into122,381,233 bytes on Linux. This includes all control work as well
as319,234/348,282 transferred keyboard bytes; it is not a pure keyboard byte-
amplification measurement. Site2042 accounts for316,775/346,217 keyboard-to-job
routing events, recording both acceptance and refusal. No8000/8001 ingest-failure
events appear in these three saved runs. These counts describe emitted records;
they do not establish completeness of every intended hook or isolate their costs.

Native raw_authored_emit overloads and Lua blackbird.emit are wired. Runtime
registers the Lua function in src/coding.cpp:1026; programs/turn.lua emits step,
call count and restart state at authored sites3880–3882. The previous performance
round uses custom Lua sources and does not call blackbird.emit: its inspected
real seed/control runs contain zero authored scalar/byte events at1510–1514.
Consequently that round does not measure authored-emission API performance or
production of the shipped turn program's three domain metrics. It does measure
actual operation, bridge, storage, UI/input and lifecycle event production.
