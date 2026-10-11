# Completing nls.2

2026-10-11. Source inspected on master at `f99ccb2`; initial instrumentation
source is `56a965c`. This is the proposed remaining sequence, with design gaps
distinguished from implementation and verification work. Implementation of the
raw architecture is already approved. Evals remain deferred.

## What is ready

The [raw design](INSTRUMENTATION_RAW_DESIGN.md) settles process separation,
native envelopes, lane identity, publication/reclaim, armed wakeups, protected
terminal/failure credit, recorder framing, synchronization, and process-death
recovery. The older literature/reference studies and the single design review
support those choices. Neither a replacement framework nor another broad
research campaign is needed.

The channel, audit component, supervisor/recorder, native clock/region hooks,
immutable descriptions, and application startup/shutdown are implemented.
Startup, restore, retained append, command admission/worker, terminal phase, and
provider-owner hooks are integrated. Provider-owner observations are not adapter
wire-send or raw-read coverage. The installed source gate passed 89 debug cases,
all 125 owned C++ translation units and the owned Lua checks. The actual launcher
passed Linux debug, release and ASan/UBSan. Component and process-failure results
have narrower claims in the [implementation note](../papers/2026-10-11-raw-runtime-implementation.md).

The [census](../papers/2026-10-10-raw-instrumentation-surfaces.md) classifies 60
product owner surfaces and nine system domains. It names event families and
available fields. It is not 60 completed byte layouts or 60 completed owner
subplans. The [existing plan](INSTRUMENTATION_PLAN.md) provides the integration
order and direct checks; the remaining owner details still need to be written.

## Designs still needed before their source changes

The first completion unit is now implemented in the current candidate:
cancel/deadline-aware admission and RPC ownership, bootstrap/metadata recovery,
read-only discovery/reader, and one actual provider/native-child vertical.
Its reviewed subplans and direct results are
[admission](../papers/2026-10-11-raw-admission-subplan.md),
[metadata](../papers/2026-10-11-raw-metadata-recovery.md),
[reader](../papers/2026-10-11-raw-reader-subplan.md), and
[provider](../papers/2026-10-11-provider-raw-vertical-subplan.md).
These are conceptual units, not completed census rows by implication. Integrated
source/profile qualification and the next owner units remain explicit work.

| Boundary | Existing machinery / concrete gap | Required owner design |
| --- | --- | --- |
| Capacity waits and cleanup | `raw_wait_data`, `raw_claim_control` and supervisor RPC now carry sticky cancellation descriptors and absolute owner deadlines; interrupted replies retain their ownership. Context retirement honors the same control. | Wire actual UI/worker cancellation into owner scopes, prepay each finite terminal sequence, and qualify owner cleanup under full lanes. The admission capability alone does not complete those integrations. No periodic main-process timer. |
| Raw reads and no-wait regions | The immediate emitter and held control claims exist; a successful space check is not an owner-wide reservation. | Calculate each bounded burst before entering a product lock or reading bytes. Establish sole ownership across the check/read/publish interval; handle reentrancy and yields explicitly. Unknown-size reads become bounded actual reads, not post-read capacity refusals. Record requested and returned amounts, errors, EOF and partial results separately. |
| Capacity selection | Current defaults are 64MiB data, 256KiB control and 128KiB failure per producer, with 128KiB recorder failure space. These are implemented configuration, not a measured workload sizing result. | Measure source rates, finite bursts, terminal sequences and recorder stalls on the first real vertical. Choose capacities and physical frame/batch allowances together. Define oversized acquisition refusal before effects. No silent fallback to dropping, sampling or shortening records. |
| Native/Lua authored events | Native emission accepts a static site and byte spans. There is no shipped Lua raw-event bridge or authored-site description interface. | Define site identity scoped to immutable source generation, description publication before activation, permitted native/Lua scalar and byte types, exact layouts, invocation linkage, size admission and failure behavior. Copy existing representations; no generic Value/BBM serialization, dictionaries, callback registry or metric arithmetic in the writer. |
| Resource acquisition | Raw clock acquisition exists. The census proposes a separate sampler for OS observations. | Specify which native observations are obtainable externally, their method/units/incarnation, coherent owner-only snapshots, and acquisition-failure records. Define sampler lifecycle and its emission lane independently of Blackbird execution. Select sampling cadence/control outside the main process. Do not invent counters or label process-wide observations as per-child usage. |
| Reader discovery and audit enumeration | The candidate native reader and `blackbird-raw-reader` discover preserved run/segment prefixes, retain exact descriptions, expose native origins/bytes and stable restart cursors. | Finish integrated qualification and consumer independence under contention. Normal semantic inspection can reference this separate interface; no reader acquires the writer lease or changes D, retention or admission. Evaluator/scoring design is outside this work. |
| Bootstrap and metadata recovery | The candidate defines activation, independent registration/header/closure copies, bounded prepaid mapping acquisitions, unresolved missing sources and known closed-name cleanup. | Finish integrated qualification and actual runtime append/sync/reclaim cuts. Preserve damaged originals and strict legacy interpretation; never infer an empty lane from missing metadata. |

For each semantic owner, write the exact sites, body widths/optional variants,
existing identity sources, observation/admission boundaries, held credit and
all actual outcomes. Name an independent expected event/byte list for success,
refusal, exception, cancellation, partial and unresolved work where applicable.
Recheck current source symbols: census line numbers describe the older baseline.
Review those new details and their plan before implementing them. Settled transport
choices do not need another review merely because another owner adopts them.

## Remaining sequence

These groups assign every product census row once. Shared native mechanisms emit
their own mechanical facts; each caller still emits its own semantic facts. A
group is a sequencing aid: each actual owner gets a complete written subplan and
its own direct checks, rather than one agent receiving unrelated leftovers.

| Order / owners | Product census rows | Work and direct completion check |
| --- | --- | --- |
| 1. Runtime admission, recording and recovery | System I; shared machinery for every row | Resolve cancellation-safe waits and bootstrap discovery first. Finish actual runtime append/sync/ack/catalog cuts, forward/reverse wake interleavings, two parked contexts, hot-lane fairness and failure-space exhaustion/replenishment. Compare independently generated bytes/identities, exact failure bodies and D at the relevant cuts. Audit-component fake-I/O results do not substitute for runtime failure propagation. |
| 2. Provider and native child boundaries | 33–40, 46–48, 51–52, 54; system D and adapter boundaries | First implement one actual provider request/wire-read/native-child vertical. Then cover both owned and compatibility provider paths, retry/auth, colleague/decision adapters, file operations, Beads and Git callers. Capture existing external request bodies before send, raw response chunks before parsing/preview, actual syscall/HTTP/child results and separate linked request/response records. Do not copy credential-bearing curl configuration as a metric body. Child fixtures supply exact binary stdout/stderr, partial-input and exit/control facts; malformed responses, limits, transport failures and cancellation must retain already observed originals. |
| 3. Command output and control | 10, 49–50; systems E/F | Cover pipe/PTY launch, output, stdin partial writes/refusal, resize, signal, foreground routing, stop/continue, EOF/reap and terminal preservation. Reserve reads and terminal control before acquisition. Real children provide byte/status oracles under full lanes, cancellation and foreground changes; cleanup remains possible while ordinary admission is blocked. Foreground wait completion is separate from process exit. |
| 4. Participants | 41–45; system G | Cover worker binding, configuration/admission, request/response, capture/owner drain, direction delivery, cancel/join/archive/shutdown. Test the actual capture limit and retention failures with original bytes still retained; never wait for the recorder under the owner mutex or acquire ContextStore from a worker. Direction identities come from actual transfers, without claiming a global logical clock. |
| 5. Context, tasks and storage | 24–32, 53; systems A/B/C | Cover context/CLM changes and reads, budgets, validation/unknown placeholders, checkpoints, tasks/activity, retained-state indexing/archive and physical storage. Use exact original references only where originals are retained. Force conflicts, allocation/I/O/sync failures and reopen. Replay emits replay work rather than a fresh historical effect. Storage instrumentation cannot recursively append through the failing storage it observes. |
| 6. Programs, workflows and authored events | 13–22, 56, 59; bridge parts of A | Cover turn/source pinning, VM/bridge lifecycle, definition/tool activation, callable workflows, candidates and backstop. Implement the reviewed raw native/Lua authored interface. Exercise generation changes, retained descriptions, accepted/refused bodies, Lua/native errors and invocation linkage. Authored raw values remain uninterpreted until disk preservation. |
| 7. Session and operator surfaces | 1–9, 11–12, 23, 55, 57–58, 60; system H | Complete launch/reopen/settings, input/composer/queues, UI persistence/messages/frame transport, presentation retirement, restart/handoff, complaint, station and explicit recovery. Real CLI/PTY/station/reopen fixtures compare owner outcomes with preserved events, including short writes and failures. Read-only audit inspection creates no new capture run. |
| 8. External samples and consumption | Acquisition part of I; reader interface | Implement the reviewed sampler and read-only discovery interface. A stopped, slow or killed reader cannot filter, mutate or acknowledge raw events. Compare preserved bytes/cursors with and without the reader and under reader disk contention. Demonstrate native resource-acquisition errors as records. Ops/evals consume the output; no eval definitions or executions here. |
| 9. Workload sizing and cost | Actual paths above | Measure 64/256/4096-byte bodies, natural bursts, wrap/full lanes, raw clocks, awake/idle recorder and cold/warm pages. Report producer p50/p99/max, allocations/copies/RMW/syscalls/wakes, preserved-prefix lag, sustained throughput and recorder CPU. Measure real owner paths as well as the primitive. Use the results to finish capacity choices; preliminary enqueue medians do not qualify end-to-end performance. |

The eight surface classes remain lifecycle, state/publication, data boundary,
queue/handoff, coherent owner snapshot, resource ownership, authored domain event
and acquisition/failure. The table covers all of them, not merely timed regions.
System A–I are representation/codec/bridge; physical storage; retention/recovery;
file/native child; command lifecycle; command flow/control; workers/queues;
terminal/UI; and clocks/acquisition/channel.

## Shared defects and boundaries

The participant capture limit, post-effect result refusal and provider original
expiry are tracked in P0 nls.10. Where an nls.2 hook depends on those originals,
its owner unit must fix that retention boundary and update nls.10. A locator to
discarded bytes cannot qualify the hook. This is a concrete overlap, not a claim
that instrumentation closes the entire audit-completeness issue or that its
whole audit review must precede every hook.

Lane ordinal order is not a logical clock. nls.9 remains separate. Host power
loss is excluded. A process dying before complete publication leaves an
acquisition unresolved; do not fabricate its outcome. Multiplayer, steering,
new trajectory work and particular evals remain deferred. The independent
startup_loading development-fixture investigation is arconaut-985.

## Closing the bead

For each owner: reviewed concrete subplan, direct red oracle, implementation,
green checks, one code/oracle attack and finding-driven fixes/recheck. Use the
existing owned lint/source gate and relevant Mac/Linux release/diagnostic checks.
Six build/analysis workers remain the approved maximum. Choose checks for actual
fault classes; no tests for tests, another assurance layer, arbitrary campaign
stopwatch or laptop mutation campaign. Text-only changes get document checks.

nls.2 closes when all current census owners have their specified emissions and
failure paths implemented, independent source-to-preserved-byte checks pass,
runtime failure/cleanup/consumer behavior matches the design, native/Lua authored
events and read-only consumption work, and real costs/capacities are measured.
The remaining sequence is not itself evidence that those conditions hold.
