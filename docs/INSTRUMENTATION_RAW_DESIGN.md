# Raw instrumentation design

2026-10-11. Approved raw design. The operator approved implementation and excludes
host power loss. This supersedes the earlier metric framework.

Blackbird emits raw metric events into the audit log. A separate recorder writes
them to disk. A tertiary process transforms the preserved records. Operations
and evals consume metrics; they do not emit them.

```text
owner hook -> retained raw byte lane -> separate recorder -> raw audit disk
                                                           -> tertiary transforms
                                                           -> ops / eval consumers
```

## Guarantee and acquisition boundary

Every completely published event stays retained until the recorder confirms disk
preservation. Producer death, recorder death, or both together do not lose it
while the host and named shared memory remain available. Full queues stop
admission/backpressure sources; they never overwrite, truncate, sample, aggregate
or drop pending events. Ingest failure writes a separate failure event and leaves
the original pending.

Two boundaries cannot disappear behind "lossless": a process can die between
observing a fact and publishing it; a host crash before disk acknowledgement can
lose pending RAM. A pre-effect record makes a missing outcome unresolved, not
reconstructed. Async handoff cannot make every returned emission durable.

The operator excludes host power loss. Publication is asynchronous with respect
to disk; emitted records remain in named RAM until recorder synchronization and
reclaim. Producer code does not wait for per-record disk preservation. Process
death before complete publication can leave an acquisition unresolved. Neither
an event nor a successful effect is inferred from a missing terminal record.

## Grounding and transport choice

The primary mechanisms are Unix process separation, Lamport's FIFO publication,
and sequential append/group commit. Complete source paths were followed:

* [Older systems study](../papers/2026-10-10-instrumentation-raw-emission-study.md):
  Multics, Unix/V7, Lampson/Lamport, Berkeley traces, Cedar and BSD trace/decode.
* [Transport study](../papers/2026-10-10-raw-transport-design-study.md): the full
  qmail → pipe → multilog path, supervision, shared-memory lifetime and atomics.
* [Recorder study](../papers/2026-10-10-raw-recorder-design-study.md): actual logger
  buffers/write/flush/restart, existing Blackbird framing and platform durability.
* [Owned primitive probes](../papers/2026-10-10-raw-emission-probes.md): measured
  producer work and retained-byte/durable-ack process-death experiments.

Choose one named shared-memory byte lane per non-reentrant execution context.
A pipe alone is insufficient: the recorder can read into private memory and die
before preserving it. Supervision retains unread pipe bytes, not drained bytes.
Adding retention/replay around a pipe still requires the retained channel.
Shared memory supplies retention and transport together. No reference becomes
a dependency; no logging framework or SDK is introduced.

## Producer work

The writer copies fields already known to their owner, acquires an explicitly
specified raw clock reading where needed, and publishes bytes. No duration
subtraction, unit conversion, counters, sums, histograms, filtering, sampling of
emitted events, projection, normalization, compression, formatting, generic
`Value` construction, dynamic registry lookup, callbacks or interpretation.
Necessary lengths, framing, ordinals and synchronization remain explicit costs.

In prepaid space: copy the envelope/body, release-store publication, then perform
the wakeup exchange described below. There is no allocation, product mutex,
global sequence increment, file write or disk sync. Wrap needs at most two raw
copies. Setup, capacity growth, waits and failures are separate slow paths.
Notification can require a syscall when the recorder is armed; the healthy path
still has one atomic RMW per natural publication burst. It is not advertised as
the cheaper load/store-only primitive measured in the baseline probe.

Byte/work ledger: a body of B bytes costs16+B transport bytes and one copy into
retained memory (at most two spans). Its raw disk frame costs32 origin bytes +
32 frame-header bytes +16+B raw bytes =80+B, plus56 per committed group and112
per segment. Descriptions and three configured lane capacities are startup costs.
No claim that the64-byte probe body equals a real hook's complete record. Reserve
recorder batch buffers at setup; account for its CRC passes, staging copies,
partial-I/O work and syncs explicitly. Do not inherit the current journal's
unbounded resident index or repeated payload-vector/string copies by convenience.

Clocks stay raw: copy POSIX `timespec` seconds/nanoseconds separately, for example,
without converting to a scalar nanosecond value. Begin/end are separate records.
Existing product-owned totals may be copied unchanged; instrumentation adds no
accumulators. Request and response are distinct records. Reported usage retains
its original representation/report identity; overlap and revisions are resolved
only after disk preservation.

## Record layout and descriptions

| Offset | Field | Meaning |
| --- | --- | --- |
| 0 | native `u32 record_bytes` | Complete envelope/body length |
| 4 | native `u32 site_id` | Static source-site/body description |
| 8 | native `u64 ordinal` | Lane-local event identity, not a logical clock |
| 16 | site-specific scalar fields and raw byte tails | Exact observed values |

Initial supported profiles are little-endian 64-bit macOS/ARM and Linux/x86.
Persist native widths, representation, field offsets and clock methods in the
lane description. Another ABI needs its own description/qualification; do not
add producer conversion to manufacture portability. Copy scalars at explicit
offsets, never C++ padding, pointers or `std::string` objects. Tails have explicit
native lengths and exact bytes, without escaping, terminators or truncation.

Before activation, durably register environment/process incarnation, lane epoch,
exact shared-memory name/capacity, build/source generation, ABI, clock domains
and immutable site layouts. Names/units live here, not in every record. Event
identity is `(lane epoch, ordinal)`. Bodies copy actual owner request, operation,
job, revision and transfer identities. No generic parent tree is invented. A
local interval without an owner identity ends by referring to its begin record.

The [raw surface specification](../papers/2026-10-10-raw-instrumentation-surfaces.md)
defines every family, body and owner for all 60 current product surfaces and
nine system domains. It supersedes old registration, aggregate and callback
recommendations. Logical-clock work remains `arconaut-nls.9`; lane order and
wall-clock proximity do not establish a global causal order.

## Publication and preservation

Each lane has capacity `C`, producer-published byte cursor `P`, and recorder-
published disk-preserved/reclaim cursor `D`. Only the producer changes `P`; only
the recorder changes `D`. Its private read cursor gives no reuse permission.
Maintain `0 <= P-D <= C` within an epoch. Each physical reservation has one owner.
Do not allow concurrent reentrancy, signal-handler emission on the interrupted
lane, or unpublished physical reservations overlapping across async yields.
Held terminal byte-credit claims reserve capacity, not positions or holes.

```text
producer: reserve complete record/bounded burst at a safe boundary
          copy every byte; release-publish P; perform wakeup exchange
recorder: acquire P; select finite complete ready prefix
          append unchanged records + integrity frames + commit marker
          perform required durability operations
          release-publish D for the preserved prefix
producer: acquire D before reusing those bytes
```

Recorder death after read/write leaves the pending original in the lane. Death
after sync but before `D` requires disk-prefix reconciliation and resync before
acknowledgement, not duplicate logical events. Cursor arithmetic is checked;
close admission/drain before exhaustion and register a successor epoch. Never
rebase live cursors or let a replacement producer reuse the old epoch.

Process-shared aligned atomics are a supported-platform contract. C++'s
lock-free/address-free recommendations alone do not establish it. Compile/layout
guards, exact disassembly and cross-process/remap checks precede qualification.
No assumption that `std::atomic::wait/notify` works between processes.

## Wakeup without a spinning recorder

Each lane has atomic `armed`, and the recorder polls an OS doorbell plus producer-
death/control notifications. After publishing a natural complete burst, the
producer **always** exchanges `armed=false` with acquire/release ordering; if
the previous value was true, it sends a nonblocking notice. The recorder drains
old notices first, exchanges `armed=true`, then acquire-rechecks `P`. It sleeps
only if every lane is empty. It never drains a new notice after that final check.

If producer exchange precedes recorder arm, the recorder acquires the producer's
publication through their exchange order and sees the work. If arm precedes
producer exchange, the producer sends a notice. Do not optimize the producer
exchange into a conditional flag load: that reopens the weak-memory wake race.
Full doorbell means a notice is already pending, not permission to drop data.
Producer death between publication and notification must independently wake the
recorder. Use a nonblocking full-duplex Unix socketpair with qualified per-send
SIGPIPE suppression. The launcher holds both endpoints and passes producer and
recorder ends; queued notices and writer bindings survive recorder restart. It
transfers endpoints to a replacement. If every holder dies, rebuild the notifier
and scan discoverable lanes before new admission. Hard send errors fence admission
and retain a failure event; an independent control route or producer-death watch
must wake the recorder. Do not wait forever on a broken notifier. The transport
study specifies descriptor/death mechanics. Cost: one RMW per natural complete
burst and occasional wake syscalls.

Use one socketpair per producer execution context, shared only by that context's
ordinary/control/failure partitions. Its owner is the sole receiver of reverse
notices. Different parked contexts never read one another's reverse endpoint.
Recorder may poll all forward endpoints, but a lane-specific reply wakes exactly
that lane's owner. A launcher owns only raw-channel lifetime/supervision, not
metric interpretation; its process/descriptor costs are part of the pipeline.

Space/preservation wait uses the reverse socket direction and separate
`space_armed`. Recorder release-publishes D, always exchanges that flag false,
and notifies if previously armed. Producer drains old reverse notices, exchanges
it true, acquire-rechecks D/credit, and waits only if still blocked. The same
exchange-order argument applies; never drain a new acknowledgement notice after
the final check. Waits observe recorder death/control and owner cancellation/
cleanup deadlines. Held launcher endpoints mean HUP alone cannot detect recorder
death. `await_preserved` never returns durable success on recorder exit. Release
product locks and let cleanup/control handling progress with prepaid credit.

## Capacity and owner locks

Reserve a bounded synchronous burst before a product lock, dispatch, allocation-
failure region or other no-wait region. Inside, use prepaid bytes; never wait,
grow or contact the recorder under an owner lock. Credit covers that region,
not an entire yielding workflow. Each producer has an ordinary byte lane, a
terminal/control byte lane and independent failure space. Before async work,
reserve a bounded terminal/cancel/cleanup byte-credit claim in the actual terminal
owner's control lane; hold it in owner state through yields until used/released.
This is capacity bookkeeping, not an unpublished byte range. Ordinary traffic
cannot consume it. Other control admissions use only free bytes minus held
claims. Emissions still reserve/publish synchronously with one owner and no holes.
Credit covers the specified finite stop/TERM/KILL/reap/result sequence; additional
requests need their own admission. Bound each actual sequence, not "one terminal
record". Full ordinary space must not prevent cleanup or erase an observed result.

A known-size record larger than `C` needs a large enough successor lane admitted
before acquiring the fact/effect. Streams publish each actual bounded read chunk
as its own event; do not concatenate an unbounded response. When credit is
unavailable, stop reading and retain existing chunks. Unknown-sized observations
in no-wait regions need owner admission restructuring before their hook is sound.

The u32 envelope limits a complete record to `UINT32_MAX` bytes, including body
and envelope. A larger indivisible event is not fixed by increasing C: refuse
its unsupported acquisition before the effect, or explicitly revise the source
contract into independently observed bounded chunks. No narrowing/truncation.
At admission also require `record_bytes+32 <= segment.max_payload` and
`record_bytes+32+32+56 <= segment.max_batch_bytes` for raw body plus origin,
frame and commit overhead. Check file/sequence allowance before acquisition;
a larger C cannot override those physical limits.

Capacity is measured/configured burst and stall allowance, not a TTL. No finite
queue absorbs infinite recording failure. Size from actual raw byte rate, bounded
bursts and the chosen stall allowance; close admission or wait safely when full.
No overflow counter substitutes for a missing original.

## Recorder and audit storage

One separate recorder writes a raw instrumentation stream in the audit segment
family. It owns its descriptor/lease; it is never a second writer on the current
semantic journal. Existing semantic facts and new raw records have distinct
audit record classes. Audit enumeration, archive and recovery include both;
there is no duplicate authoritative metrics journal.

Adapt the owned physical framing **off the producer path**: 112-byte stream
header, 32-byte frame header, 56-byte group commit frame, CRC32C and full sync.
Add an explicit raw frame kind and catalog typing. Append producer envelope/body
unchanged; do not BBM-decode/re-encode it. CRC is explicit recorder integrity
work. Replace the current full resident record index/capacity-refusal policy with
a streaming preserved cursor and bounded batch; archival lookup stays outside
the recorder hot loop.

Each disk raw-frame payload has a32-byte recorder origin prefix, then the
unchanged producer record: lane epoch (16B), source partition (u32), reserved
zero (u32), source start byte cursor (u64). End is start+record_bytes, checked;
the ordinal already lives in the raw record. Control/failure partitions have
distinct registered cursors. Segment environment and lane manifest bind origin
to immutable schema/name. Recovery reconstructs each exact cumulative D, not
merely a physical audit sequence. Gaps, overlaps or conflicting identity/bytes
block acknowledgement. This is recorder framing, not a producer transform;
batch integrity includes every origin prefix.

Take a finite complete ready prefix and sync immediately, without a batch-fill
delay or durability timer. Rotate the first lane each tour and take at most one
bounded ready prefix per lane per tour. Check failure/control partitions first
but bound their service too, so they cannot starve ordinary sources. One hot lane
cannot prevent another's terminal preservation. Native file `fsync` and relevant
parent-directory sync preserve the recorder's prefix before reclaim. The operator
excludes host power loss; this contract does not require `F_FULLFSYNC`, powersafe
overwrite qualification, sector isolation, or synchronous producer return.

Keep damaged/uncommitted tails intact. Recovery selects a validated complete
prefix; it may register a successor segment/predecessor before replaying pending
records. No blind truncation, original expiry or invented acknowledgement.
Tertiary readers receive only preserved-prefix cursors and immutable descriptions.
Their absence, failure or slowness cannot affect recording or effect execution.

## Failure and lifecycle

Producer/recorder failure records have independent preallocated space. Reserve a
failure record before each fallible ingest/write/sync attempt. Record boundary,
original identity/range, returned error and actual progress without recursively
calling the failed emitter. Keep the original pending. Exhausted failure space
stops further attempts/admissions; retries never overwrite earlier failures.

Keep a recovery-only tranche reserved at setup; ordinary attempts cannot consume
it. The external launcher/controller can admit a bounded recovery attempt with
that credit, preserving old failures first. If recovery exhausts its allowance,
no autonomous retry is possible. Before another attempt, the controller supplies
fresh registered failure capacity through a working independent metadata/audit
route; exhausted lanes stay retained. This is explicit resource replenishment,
not overwriting failures or an infinite retry loop. Finite memory cannot record
infinitely many failed recovery attempts without replenishment.

When every disk path fails, even the failure event is pending in retained RAM.
Report blocked recording; do not pretend it is on disk. A working independent
audit path may preserve it; otherwise drain failure and original after recovery.
Routine physical progress is represented by frames/commit boundaries, avoiding
a recursive "record the recording" event chain.

Durably register named memory before activation; never unlink at setup. Recorder
restart finds names from the manifest even after all processes unmap/exit.
Acquire exclusive recorder ownership and reconcile disk before changing `D`.
Graceful shutdown preserves every published record, writes lane closure, then
unlinks. Forced exit leaves an open lane discoverable for recovery.
An absent named object for an open lane is a recovery error, never an empty
lane. Closure includes final per-partition cursors and is durable before unlink;
recovery accepts absence only for that drained closed epoch.

This does not declare `arconaut-nls.10` fixed. Participant capture rejection,
post-effect result refusal and provider diagnostic expiry remain concrete bugs.
Their owner admission/retention paths must adopt the no-drop rule when integrated.
Multiplayer, steering/trajectory work and eval definitions/execution remain deferred.

## Implementation boundary

The [implementation and verification plan](INSTRUMENTATION_PLAN.md) is sequenced
by complete invariants: lane lifetime/publication, durable recorder/recovery,
then one complete product owner at a time. It distinguishes executed research
probes from future implementation qualification. This draft receives one
adversarial review and finding-driven recheck within the declared 45-minute
research/design unit. The operator approved implementation on2026-10-11 under the
operator's explicit instruction and BLACKBIRD.md.
