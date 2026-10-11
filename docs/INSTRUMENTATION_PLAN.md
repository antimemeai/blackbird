# Raw instrumentation implementation and verification plan

2026-10-11. Approved implementation sequence; first source unit integrated on
master. The [completion plan](INSTRUMENTATION_COMPLETION.md) distinguishes the
remaining concrete designs, owner integrations and unexecuted checks.
Read the [design](INSTRUMENTATION_RAW_DESIGN.md) and
[raw site/body census](../papers/2026-10-10-raw-instrumentation-surfaces.md).
The operator approved implementation and excludes host power loss.

## Decisions and measured work before source changes

The losslessness contract covers published raw events through process and recorder
failure while the host stays up. Host power loss is outside it. Emission returns
without waiting for disk; recorder synchronization precedes reclaim. Use native
`fsync` for this boundary; device cache/power-loss qualification and per-emission
synchronous durability are not requirements. Unsupported synchronization fails
visibly, never silently advances reclaim.

Select measured capacities and bounded recorder batch limits from real raw byte
rates/bursts and a stated stall allowance. These are transport allowances, not
audit TTLs. Define each owner's no-wait burst/terminal reservation before adding
its hooks. Neither a universal arbitrary buffer nor infinite buffering is a plan.

Research probes already executed are documented separately. They are **not**
qualification of this transport, wakeup protocol, recorder, audit integration or
product coverage. The primitive experiments do not qualify the product implementation. The approved
source unit has its own direct checks; eval work remains deferred.

## Unit1: lane lifetime, publication and wakeup

Own the native raw envelope, immutable site/lane descriptions, named byte lanes,
exclusive producer/recorder ownership, complete publication, preservation cursor,
safe-boundary credit, independent failure records and armed doorbell. Keep the
producer kernel small enough to read end to end. Setup owns descriptors/mappings
and durable discovery; the hot path cannot create/destroy them.

Write direct red oracles first. The expected output is a source-generated list
of unique records with independently known lengths/bytes/identities, not the
transport's decoder re-encoding its own output. Test zero/NUL/high-bit bytes,
each wrap position, whole/burst publication, exact full boundary, >capacity,
cursor limit/epoch switch, different mapping addresses, producer replacement,
and startup after the last mapper exited. Assert no visible partial record,
overwrite, omission, duplicate logical identity or unauthorized reclaim.
Include u32 envelope limits separately from capacity. Exercise held per-owner
control credit across yields and ordinary saturation; no publication holes.

For wakeup, enumerate the arm/publish/exchange/send/recheck/sleep interleavings
with a small state model whose success condition is pending-data ⇒ awake-or-
pending-notice-or-known-producer-death. Then exercise real multiprocess barriers
at the same cut points on macOS/ARM and Linux/x86. Include a full socket, stale
notice, death after P before exchange, death after exchange before send, recorder
restart, two armed lanes, dead launcher and descriptor replacement. Test the
actual pipe/socket result and independent byte list, not merely return success.
No conditional armed-load optimization without changing the proof and oracle.
Exercise reverse credit/durability wakes at the same cuts, including retained
descriptors, recorder death and owner cleanup/cancellation deadlines. Park two
contexts simultaneously; each has its own reverse endpoint, with no stolen wake.

Qualify the actual shared atomic ABI: compile-time scalar widths/alignment and
lock-free guards; object construction/lifetime before exposure; disassembly of
load/store/exchange; no hidden process-local atomic lock/runtime; acquire/release
publication at different virtual addresses and after remapping. Stress is useful
but cannot establish weak-memory correctness alone. TSan's process-shared blind
spots must be stated; a same-process threaded variant adds race-detection evidence
but does not replace process-lifetime tests.

Allocation/lock/syscall instrumentation on the owned producer primitive checks
the promised path directly. A prepaid normal publication may perform raw copies,
local arithmetic, atomic publication and one wake exchange, plus its specified
clock call and an occasional doorbell syscall. Force actual full/growth/error
paths separately; no fallible result may be ignored. Never test signal-handler
reentrancy by accidentally approving it on the ordinary lane.

## Unit2: raw audit recorder and recovery

Own explicit raw frame kind and audit segment/catalog descriptions, streaming
preserved cursor, finite ready-prefix group append, full sync, cumulative durable
ack, preserved-tail recovery, successor registration and failure-first recovery.
Reuse physical storage/framing mechanics outside Blackbird; do not import the
semantic reducer or permanent resident frame index into the recorder loop.

Drive fake I/O from a deterministic syscall schedule: positive short writes,
EINTR before/after progress, zero return, EIO/ENOSPC, sync failure, failed close,
lease conflict, naming/directory failure and corrupted commit/frame. Exact
expected append bytes and retained pending records are the oracle. Observe `D`
before/after each operation; it must never cross the validated fully synced
prefix. A failure record preserves the original identity/error/progress and does
not release the original. Fill ordinary failure space, spend the reserved bounded
recovery tranche, fail it, then externally register fresh capacity; old failure
records remain intact through recovery. Account for every fallible recovery
setup/write/sync stage before admitting its attempt. Failed close is not retried where the descriptor may
already be reused, and is not treated as a sync substitute.

Kill the real recorder at: acquire P, raw read, every write boundary, commit
write, sync entry/success, before/after D, and segment/catalog publication. Restart
from only manifest+disk+named memory. Compare all preserved logical identities
and bytes to the independent source list, including each origin prefix and exact per-partition byte cursor.
Drive one continuously hot lane and another with a single terminal record; its
acknowledgement must progress under the bounded rotating service rule. Lost
acknowledgement resyncs/reconciles
without duplication. A partial/bad tail remains byte-for-byte available; valid
predecessor selection and successor writes cannot erase or acknowledge it.
Also kill producers at partial body/full body/before P/after P/notification, and
both processes while the host stays up. Open acquisitions remain unresolved.

Host/power loss is excluded by the operator. Do not add VM/block-device power
experiments or device-firmware qualification to this work. Process-death tests
keep the host running and compare published originals with disk/reclaim state.

Tertiary input is only the validated preserved prefix. Halt/kill/slow the consumer
and compare raw disk bytes/cursors. It must cause no producer callback, filter,
delay, mutation, additional acknowledgement or deletion.

## Unit3 onward: integrate whole semantic owners

First vertical slice: provider request preparation/send and response/raw stream
observation, plus one native external-process path. Emit exact failure/refusal/
unknown facts as well as success. Preserve the actual upstream usage/report,
request/response distinction and native partial reads. Replace aggregate
`LocalSpan` emission with separate raw endpoints rather than translating old
durations. Read the resulting raw audit prefix with an independent small decoder.

Next: participant worker/capture/owner drain and command output/control/terminal,
including full-buffer backpressure, cancellation/cleanup and foreground routing.
Use real child fixtures that report their own distinct byte/exit/control facts.
Use injected owner retention failures to show that already observed results stay
retained. No metrics hook may acquire ContextStore from a worker or hold its
product mutex while waiting for the recorder. Address the matching nls.10 defect
at its actual observation/admission boundary, not with a dropped-event counter.

Then integrate coherent groups from the60-row census: context/tasks/definitions;
file/storage/recovery; session/UI/workflow/station/backstop; authored raw events
and external resource acquisition. Each group lists all actual outcomes before
implementation. Successful, refused, thrown, cancelled, partial and unresolved
paths must independently emit their specified raw records. Replay reports replay
work, never fresh execution of historical effects. Read-only audit queries cannot
recursively produce their own query stream through the same failing path.

No generic callback registry or SDK intermediary is added to "simplify" owner
integration. Lua's raw event bridge receives source-owned scalar/byte values and
copies them; generic serialization or custom measurement computation is a
separate reviewed interface. Logical clocks remain nls.9, multiplayer is deferred,
and ops/eval transformations are tertiary work. No evaluator definition/execution
is smuggled into this sequence.

## Static and dynamic gates by fault class

| Claim / fault class | Direct mechanism and oracle |
| --- | --- |
| No producer transforms/hidden work | Complete producer call-graph review, focused compiler/lint diagnostics, optimized disassembly, actual allocation/lock/syscall counts; raw body byte comparison |
| Publication/reclaim correctness | Bounded FIFO/wakeup state model plus independent exact source list at every deterministic cut point |
| Memory/lifetime/arithmetic safety | Layout/type/static guards, owned C++ lint/analyzer, scoped ASan/UBSan and thread-race cases; actual remap/process tests |
| Crash replay/no premature ack | SIGKILL at I/O/ack cuts; actual disk parse against source list and D; host-crash/storage model only for its stronger claim |
| Failures not omitted | Scheduled real/fake I/O failures with exact failure body and original retention; exhausted ordinary and failure capacity |
| Owner semantics/lock safety | Real product operations with forced failure/cancel/cleanup, child-side byte/status fixtures and controlled owner locks; no emitter-only smoke test |
| Raw preservation/consumer isolation | Compare source bytes to disk across wraps/restarts and stalled/dead tertiary reader |
| Claimed performance | Measure actual full path on selected builds, not just enqueue: producer latency including clocks/wake, allocations/copies/syscalls, idle/loaded recorder CPU, drain and preservation lag, sustained rate/backpressure |

Use existing `scripts/rigor` profiles and the configured Linux host for changed
owned source. Full source commit gates apply when source is changed; text-only
design commits receive diff/document/link checks. Tests run with release builds
as well as relevant diagnostic profiles. Broaden only for a relevant change or
remaining fault class. Mutation remains deferred by operator instruction; never
run mutants on this laptop.

Before each implementation unit, declare concrete behavior, direct checks and
resource use. Hardening is two layers only: fix/direct verification,
then one finding-driven recheck/fix. Review the code and real oracles once, never
review the review. Bound the work by concrete defects and those two layers;
do not invent campaign stopwatches. Leave unsafe candidates inactive and record
exact remaining work. Passing this plan on paper is not qualification of product code.

## Performance experiments and completion

Measure unbatched and natural-burst records across64/256/4096-byte bodies, wrap
and full queue, clocked/unclocked sites, awake/idle recorder, cold/warm pages and
actual full-sync batches. Report p50/p99/max producer latency, preservation lag,
raw-byte throughput, RMW/syscall/wakeup count and recorder CPU. Preserve raw
results/configuration. Small current probes report ranges only, not tail claims.

Completion means the actual emitted/preserved byte list matches real owner
outcomes including failure, crash behavior matches the agreed contract, and
costs are measured honestly. Close nls.2 only after approved implementation and
these direct checks; a research proposal does not close it. Keep nls.9/nls.10
open until their actual causal/completeness defects are resolved. Record unit
results in JOURNAL, preserve research/restoration records, back up beads and
checkpoint/push completed work under current repository instructions.
