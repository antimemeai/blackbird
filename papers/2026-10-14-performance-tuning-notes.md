# Plausible performance work after the 2026-10-14 round

These are investigation notes requested at the end of the watch, with open beads.
No tuning has been implemented and no new experiment starts during handoff.
Discuss the measurements and select a concrete unit before optimizing. Read the
[round assessment](2026-10-14-performance-round.md) and the linked actual datasets;
read [BLACKBIRD.md](../BLACKBIRD.md) before treating a hypothesis as a design.

Raw events must leave the main processes quickly and losslessly. Producers and
the recorder preserve literal fields and bytes; transformation belongs in a
tertiary process after raw disk preservation. No filtering, sampling, coalescing
that erases events, normalization, aggregation, formatting, compression or new
producer timers. Preserve request/result separation, process-failure recovery,
protected completion/cleanup capacity, synchronization-before-reclaim and complete
audit across failure. Host power loss remains excluded. Significant memory use
is welcome when intentional and justified; there is no arbitrary RSS target.

## Correctness first: existing File integration defect

**arconaut-4by** already records the missing RawAdapterContext at CodingEngine's
LocalTools dispatch. Operator/Lua read_file returns correct business bytes and
emits whole-call/turn/bridge records, but skips the nested File tool/helper/native
syscall/original events. Direct File APIs with an explicit context do emit them.
Study actual dispatch, current operation parent, bank composition and lifetimes;
plan propagation through read/write/edit without a second transport or issuer.
Require actual operator/Lua/model dispatch integration oracles, exact source and
proposal/result bytes, distinct linked outcomes, error/cancellation and paid
cleanup. This is a wiring repair, not a performance optimization. Refresh only
measurements materially affected by the repaired path after the repair is tested.

## Owner waits and tails — arconaut-nls.11

Observed at 256 bytes on macOS: operator calls measure 33.593 ms capture-off and
35.919 ms capture-on, with a 456.122 ms active maximum. Local mock-provider turns
measure 51.677/61.410 ms; Station triplets measure 8.840/10.094 ms, with an active
151.246 ms p99. Large tails also occur without capture. Neither the emitter's
nanosecond median nor one matched batch explains those complete-call costs.

Study existing synchronous business audit and retained-storage calls, then use
scoped native CPU/off-CPU/file-operation observations to distinguish actual sync
waits, scheduler delay, repeated work, copies/allocations and recorder contention.
Use existing events where adequate; do not add a new product metric framework to
profile metrics. Possible improvements include avoiding proven redundant work,
reusing owned immutable storage and byte-preserving I/O batching within the
existing effect/recovery contract. Those are hypotheses until attribution.

Stream-specific write amplification already belongs to **arconaut-bzn**. Its
older optional-original/expiry/loss permission is superseded by **nls.10** and the
approved raw preservation rules. Do not resurrect it or implement batching twice.
This bead covers complete owner-call attribution, not a new provider campaign.

Direct check: one finite matched actual owner workload, exact business effects,
literal audit originals/links and every latency sample, with direct native cost
attribution. Do not remove outliers or infer stable tails from 100 calls.

## Recorder backlog and page costs — arconaut-nls.12

The launcher-grant 128 MiB stream emits in 78.90 ms on macOS and 229.18 ms on
Linux, while complete preservation takes 475.19/870.63 ms. Final-event observed
ACK lag is 396.28/641.45 ms. Smaller 1 MiB grants finish preservation in
349.19/940.16 ms. Linux 4 KiB burst emission measures 3.40–3.44 µs at the launcher
grant versus 429–438 ns at the small grant. No optimal grant or cause is known.

Inspect raw_channel publication and raw_runtime recorder drain/write/sync/cursor/
ACK/reclaim/fairness. Measure actual recorder stacks, writes/syncs/bytes, page
faults and producer/recorder scheduling. Plausible questions: redundant native
work, existing byte-preserving batch handling, first-touch versus reused pages,
and cache locality. A pre-touch proposal must account for startup and total work;
it must not simply move cost outside the timed emission pair. A larger ring can
hide capacity waits while increasing preservation backlog; that is not a free
throughput improvement. Do not add a timer to decide when to drain or sync.

Direct check: finite equal-body/equal-total-byte streams under explicit grants,
exact closed raw bytes/counts, scoped recorder cost and producer/ACK observations.
Any change also needs relevant refusal/failure/recovery/reader/fairness checks.
Keep traced trials separate from ordinary latency trials.

## High-volume production — arconaut-nls.13

The actual 100-call registry session produces 91,183 events and 13.52 MB of event
journal; native storage/catalog hooks dominate its count. Complete native control
sessions produce 653,756/692,862 events and 115.77/122.38 MB. Keyboard-to-job routing
site 2042 contributes 316,775/346,217 records. Whole sessions include other work;
these totals do not isolate a keyboard amplification ratio. Owner batches produce
1,201–35,086 events per 100 operations, including run overhead.

Enumerate reached sites and the actual calls behind them. Account for body and
frame bytes, repeated fields, copies/allocations, publication, and downstream
writing. Distinguish necessary distinct outcomes from accidental repeated
business or hook invocation. The initial tuning target is cheaper production of
the same events: owned lifetime/storage reuse and unchanged-byte publication/I/O
improvements if direct attribution justifies them. A proposal changing event
boundaries or census needs explicit operator discussion. Do not reduce event
counts through loss, filtering or aggregation in Blackbird.

Direct check: actual finite CLI/tool/control work, per-site volume and directly
observed costs, exact original bytes/outcomes/links and unchanged completeness.
Do not substitute synthetic raw_emit throughput for useful application work.

## Linux reopen — arconaut-nls.14

Linux fresh startup measures 31.458 ms median; reopening its native 100-call local
tool history measures 173.807 ms. macOS measures 124.849/145.414 ms. Every scenario
has 30 ordinary-cache starts, with seed copying excluded. The fixture is not a
large model conversation; host differences have not been attributed.

Trace the actual restore/confirm/recovery path, saved-state selection, catalogue,
journal/storage reads and suffix/full replay. Establish which path is selected
before proposing a saved-state/cache optimization. Separate emission during
reopen from reading business history; do not assume that raw metric history is
replayed. Preserve complete audit and unknown-effect fences. **arconaut-985** is
an independent failed development startup probe, not a demonstrated product
startup blocker; repair it only if the selected investigation needs that probe.

Direct check: exact native seeds and the existing completed-frame/unsubmitted-input/
normal-exit endpoints, every sample and attributable phase/read/work counts.
Any later recovery change needs semantic state/originals/no-replay/no-effect checks.
Keep fresh startup quick.

## Explicit authored emission — arconaut-nls.15

Native raw_authored_emit and Lua blackbird.emit are wired; the shipped turn
program emits step, call count and restart state at authored sites 3880–3882.
The last perf round uses custom Lua without emit. Its inspected seed/control runs
have zero authored scalar/byte records at 1510–1514. Complete authored API cost
is therefore a measurement gap, not measured by the prepaid-emitter figures.

Use one finite actual native/Lua workload for integer, floating-point, boolean
and literal bytes, including NUL and larger bodies, through real ownership and
binding. Measure admission, boundary conversion and publication together, and
report construction/startup separately. Use independent exact native value bits,
site/source-generation/VM links and descriptions as the disk oracle. Include
relevant first-use, burst, paced and capacity/failure cases without starting a
new generic benchmark framework. A local fixed provider can exercise shipped
turn metrics without network calls or selecting an evaluator.

Direct check: literal preserved values/types/links/counts and truthful emitted,
refused or failed acquisition outcomes; every sample, including failures. Scope
allocation/syscall/wait counters to the actual API and label unavailable counters.

## Working order and limits

Start the next discussion with arconaut-4by, then select the smallest worthwhile
performance question. Owner-tail attribution and the authored-API gap are useful
first options; volume and recorder work may follow their findings. There is no
need to run all five beads as a campaign or make them a prerequisite to building
the intended harness. These open P2 tasks record options, not promises of a
speedup or operator approval for a changed emission/audit contract.

Use the existing owned source/PTY/raw drivers and platform profiling machinery
where they answer the question. [PROFILING](../docs/PROFILING.md) explains profile
binaries, macOS attachment entitlements and Linux perf; Release binaries are not
signed for allocation attachment. Existing Linux strace/perf were present, but
actual permissions must be checked. Compiler/static workers stay at six aggregate,
one Ninja writer per tree; serialize native timing work per host. Do not stop
unrelated operator services to manufacture a clean benchmark environment.

No always-running collector, writer-side processor, new library or profiler
dependency is proposed. Study existing older literature/reference material and
actual source, produce a concrete design/plan, then use direct fault-class checks
for a selected change. Do not repeat an entire suite for these text-only notes.
