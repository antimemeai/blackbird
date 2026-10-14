# Durable concurrent workflows: mechanisms and an ambitious Blackbird destination

Research date: 2026-10-14. Frumentarius lane: durable execution, structured
concurrency, scheduling, assets/builds and reactive composition. Source study only;
no dependency adopted, reference code executed, product code changed or benchmark
claim established. Statements marked **proposal** are design deductions rather
than existing Blackbird behavior. This report complements the
[2026-10-07 ecosystem map](../workflows-2026-10-07/ecosystem.md), rather than
reclassifying its delivered W1 as missing.

The destination should combine durable orchestration, live agent/context evolution,
structured child ownership, keyed streaming and demand-driven artifacts in one
execution model. Merely adding persisted futures to the current call loop would
leave the hard problems untouched. The strongest references solve different parts;
Blackbird can surpass the combination through explicit cross-boundary semantics
and model agency over inspectable native execution objects.

## 1. What the primary mechanisms actually do

### Temporal: command replay, isolated effects and deployment identities

Temporal reconstructs workflow state by replaying retained events through workflow
code. Its orchestration emits commands; historical command events must match
corresponding machines. The inspected core's `handle_command_event` removes the
next scheduled command, routes the event to that command's state machine and
raises nondeterminism when no corresponding command exists. Its activation
construction treats query-only activations as replaying and does not record them
as normal history. This is a protocol with an exact history shape, not a Lua stack
snapshot. **Read source:**
[workflow_machines.rs:434–470,934–1008](../../quarantine/durable-temporal-sdk-core-212e81500665/crates/sdk-core/src/worker/workflow/machines/workflow_machines.rs#L934),
[pinned upstream](https://github.com/temporalio/sdk-rust/blob/212e8150066527b57e792b80626fe0d14e7f1da1/crates/sdk-core/src/worker/workflow/machines/workflow_machines.rs#L934).

Signals, queries and updates have different interaction contracts: asynchronous
history-backed input, read-only inspection, and tracked synchronous interaction.
The distinction matters for a model steering another model: accepted, observed
and applied must be distinguishable.
[Message passing](https://docs.temporal.io/encyclopedia/workflow-message-passing).

Worker versioning pins running executions to deployment/build versions; old code
must remain available while pinned work needs it. History size affects restart
latency, and Continue-As-New bounds an individual execution's history. Large
payloads should travel by reference. These are important production mechanisms
but do not implement mutable in-flight arbitrary code or retained OS resources.
[Worker practices](https://docs.temporal.io/best-practices/worker).

Cancellation is an instruction, not proof that a remote activity stopped. Workers
deliver activity cancellation through heartbeats; a non-heartbeating activity
does not receive it. A timed-out invocation may have acted externally.
[Activity failure/cancellation detection](https://docs.temporal.io/encyclopedia/detecting-activity-failures).

**Proposal:** own the same command/effect distinction and replay mismatch refusal,
but add explicit resumable segments and state migration. Refuse direct OS/provider
effects inside the replay lane. Preserve independently owned job handles and
context revision lineage outside orchestration frames.

### Restate: journal plus atomic state/outbox, durable races and single writers

The existing pinned Restate server applies commands through a partition state
machine. The inspected journal append stores invocation status and journal entry
with a log-derived append timestamp. A call command creates a service invocation,
queues it in the outbox and proposes a correlated invocation-ID completion.
This avoids a gap between orchestration state and internally queued RPC work.
**Read source:**
[journal append](../../quarantine/restate/crates/worker/src/partition/state_machine/entries/mod.rs#L365),
[call/outbox](../../quarantine/restate/crates/worker/src/partition/state_machine/entries/call_commands.rs#L96),
[pinned upstream](https://github.com/restatedev/restate/blob/2180b55410f6512f8534012167aa536116d75c8a/crates/worker/src/partition/state_machine/entries/call_commands.rs#L96).

Current SDK documentation supplies durable `all`, `any`, `race` and `allSettled`
combinators. They retain completion ordering for deterministic recovery. First
successful completion and first settled completion are different operations.
Plain language-runtime promises do not automatically supply those guarantees.
[Concurrent tasks](https://docs.restate.dev/develop/ts/concurrent-tasks).

Immutable deployments pin attempts to original endpoints. Moving paused work to
another deployment still requires journal compatibility; reordering operations,
changing inputs and unordered iteration can break it. Keyed object state must
also remain schema-compatible. Ordinary result journaling leaves an external
effect repeatable if failure occurs before its result becomes durable.
[Versioning](https://docs.restate.dev/services/versioning),
[explicit external-effect boundary](https://restate.dev/blog/why-we-built-restate).

**Proposal:** one writer owns each run's transition order, even while effects and
children execute concurrently. Durable join selection and child admission belong
in that writer's atomic transition batch. External providers/files/processes
remain adapters with declared guarantees; an internal outbox is necessary but
insufficient for their exactly-once execution.

### DBOS: replayed step results versus atomically recorded database effects

Fresh source was acquired at
`61426cb95bcd749dd18137b4eb483d02b9975d7d`. An ordinary step checks for an existing
result, runs the function if missing and records its serialized output/error
after the function returns. A crash can therefore occur after the function acts
and before the checkpoint. The inspected checkpoint lookup uses workflow ID,
numeric function ID and function name; that lookup does **not** compare an
argument digest. **Read source:**
[step pipeline:2292–2391](../../quarantine/durable-dbos-python-61426cb95bcd/dbos/_core.py#L2292),
[checkpoint lookup:2951–3031](../../quarantine/durable-dbos-python-61426cb95bcd/dbos/_sys_db.py#L2951),
[pinned upstream](https://github.com/dbos-inc/dbos-transact-py/blob/61426cb95bcd749dd18137b4eb483d02b9975d7d/dbos/_core.py#L2292).

Datasource transactions have a stronger boundary: the application mutation and
serialized result row share one database transaction. The source flushes generated
values, inserts the result before commit, and handles a competing recorded result
by rolling back that attempt and reading the winner. This applies to that
database's transaction, not a model call or arbitrary external shell effect.
[Datasource implementation:440–545](../../quarantine/durable-dbos-python-61426cb95bcd/dbos/_datasource.py#L440),
[transaction contract](https://docs.dbos.dev/python/tutorials/transaction-tutorial).

The current runtime rejects preemptible synchronous steps; async preemption is
supervised explicitly. Cancelled preemptible steps are not checkpointed and can
run again after resume. Workflow queue cancellation at a step boundary and worker
concurrency/global concurrency/rate limiting are distinct controls.
[Preemption source:2233–2257,2305–2313](../../quarantine/durable-dbos-python-61426cb95bcd/dbos/_core.py#L2233),
[queues](https://docs.dbos.dev/typescript/tutorials/queue-tutorial).

**Proposal:** compare semantic invocation identity, input digest and schema as
well as command position. Permit exactly-once *committed mutations* only when an
adapter places both mutation and durable operation identity in one atomic domain.
Keep remote-result retry and ownership-fencing claims separate.

### Windmill: named checkpoints, child IDs and correlated waits

The current quarantined WAC model retains source hash, completed step map, pending
step keys/mode, stable child job IDs and consumed resume-row IDs. Its resume logic
excludes already-consumed approval rows, a concrete defense against sequential
waits consuming the same historical response. Stable wait identity is required
across API/worker versions; the code derives it with a specified hash rather than
a runtime-dependent standard hasher. **Read source:**
[checkpoint and wait identity:23–81](../../quarantine/windmill/backend/windmill-common/src/wac.rs#L23),
[dispatch/resume:221–382](../../quarantine/windmill/backend/windmill-worker/src/wac_executor.rs#L221).
Pinned source revision and broader limitations remain in the
[existing dossier](../capabilities/dossiers/windmill.md).

**Proposal:** keep user-meaningful step keys and retained child handles, but avoid
using one textual name as execution identity. Every dynamic instance needs scope,
node, iteration/expansion and invocation identities. Native binary state must
retain resume correlation and consumption atomically with the transition it causes.

### Argo: failure admission and drain are explicit

Argo's documented DAG fail-fast policy stops new scheduling when a task fails and
waits for running tasks to finish before marking the DAG failed. Disabling it lets
branches complete despite sibling failures. This demonstrates why admission,
cancellation and draining cannot be represented by a single `fail_fast` boolean.
[DAG behavior](https://argo-workflows.readthedocs.io/en/latest/walk-through/dag/).

**Proposal:** provide policies named for behavior: stop-admission-and-drain,
cancel-siblings-and-settle, collect-all, isolate, supervisor-restart. Preserve the
original failure and later cleanup failures instead of choosing one arbitrarily.

### OTP and composable channels: ownership, restart amplification, backpressure

OTP distinguishes one-for-one, one-for-all and rest-for-one restarts, permanent/
transient/temporary children, shutdown timeouts and restart intensity. Deep
supervisors multiply retry allowances; identical limits at every layer can create
a restart storm. Finite tasks and resident services therefore need different
supervision policies.
[Supervisor principles](https://www.erlang.org/doc/system/sup_princ.html).

Guile Fibers channels expose rendezvous operations: send completes with an actual
receiver. Its source uses operation flags to coordinate competing selectable
send/receive operations. This illustrates that composition requires one winner
and cleanup of losing registrations, not polling every wait until one looks ready.
[Read channel source:62–138](../../quarantine/guile-fibers/fibers/channels.scm#L62),
[original systems study](../2026-09-30-systems-runtime-frumentarii.md).

**Proposal:** structured scope exit must retain all owned children until settled
or explicitly transferred to a durable supervisor. Detached work cannot mean
ownerless work. Streams use bounded credit or rendezvous; cancellation/control
traffic has a reserved route so a full data queue cannot prevent shutdown. Child
retries debit a shared parent retry/cost/deadline budget rather than multiplying it.

### Statecharts: restored state is not restored effect execution

XState persistence restores machine and recursively persisted actor state.
Previously executed actions are not rerun, while invocations restart. This is
useful, explicit behavior and also a warning: restored graph configuration does
not say whether an external invoked process/API already acted.
[Persistence](https://stately.ai/docs/persistence).

**Proposal:** the statechart frontend compiles transitions, orthogonal regions,
event selection and invoked-child scope into the same run/task/effect records.
An invocation carries restart/reattach/reconcile policy. Restoring the active
state cannot silently redispatch it. DAG, actor, stream and statechart frontends
need declared semantic profiles, not a lowest-common-denominator graph export.

## 2. Scheduling and artifacts that agent products often omit

Temporal schedules are independent identities with interval/calendar specifications,
overlap rules, catch-up windows, backfill and pause. Schedule pause affects future
admission, while workflow pause concerns an existing execution. Embedded timezone
definitions are supported in the specification. Its last successful completion
is selected at run start, rather than whichever result exists at later query time.
[Schedule contract](https://docs.temporal.io/schedule).

**Proposal:** an occurrence identifies schedule revision, nominal civil time,
resolved UTC instant and logical data interval independently of actual start.
Persist timezone database identity, DST ambiguous-time policy (first/second/both/
skip) and missing-time policy (skip/shift/reject). A timezone-rule update changes
future occurrence interpretation through an explicit schedule revision; it cannot
reinterpret already admitted work. Restart computes missed intervals with declared
skip/latest/bounded-catch-up/backfill policy. Backfill is an inspectable bounded
workload with its own budget, precedence and progress, not an unbounded loop
submitting old cron ticks. Durable sleep records its original target; restart
does not reset the duration. Wall time, monotonic elapsed time and logical event
time must remain different typed quantities.

Dagster distinguishes assets and partitions from individual runs; static, time,
multidimensional and dynamically discovered partitions enable selected materialization.
Backfills can process each partition separately or a range in one run, important
when an external engine already supplies parallelism.
[Partitions](https://dagster.io/docs/guides/build/partitions-and-backfills/partitioning-assets),
[backfill](https://dagster.io/docs/guides/build/partitions-and-backfills/backfilling-data).

Bazel remote caching separates an action-result index from content-addressed
output storage. Inputs, command, environment and outputs define a reusable action;
nonhermetic or concurrently modified inputs can poison reuse. The Remote Execution
API explicitly permits in-flight merging even when cache lookup is bypassed.
[Remote cache](https://bazel.build/remote/caching),
[Remote Execution protocol](https://github.com/bazelbuild/remote-apis/blob/main/build/bazel/remote/execution/v2/remote_execution.proto).

Nix derivations describe builder/system/environment/input dependencies and output
identities. This sharpens reproducibility requirements but does not turn general
agent work into pure computation.
[Derivations](https://nix.dev/manual/nix/2.32/language/derivations).

Nextflow resume needs cached task metadata plus intact work outputs; keyed joins
avoid pairing unrelated records by concurrent arrival order. Its documentation
includes a concrete incorrect merge of same-ID file pairs arriving in different
orders.
[Cache and resume](https://docs.seqera.io/nextflow/cache-and-resume).

Prefect's persisted results and task transactions support explicit commit timing
and rollback hooks. External cleanup must be authored; eager commits alone do
not prevent concurrent bodies both executing. Serializable keyed locking has a
scope determined by its lock manager.
[Transactions](https://docs.prefect.io/v3/advanced/transactions),
[caching](https://docs.prefect.io/v3/concepts/caching).

**Proposal:** distinguish demand (the artifact desired), plan (the producers needed),
execution (task/effect attempts), materialization (immutable available output) and
validation/freshness (whether that output satisfies this demand now). Reuse requires
both matching semantics and valid retrievable bytes. A model answer's cache policy
must identify effective provider/model parameters, context/tool/config revisions,
original external observations and allowed freshness; equal prompt text alone is
insufficient. Irreversible publishing effects never become reusable simply because
their preceding build was cached. An effect may be deduplicated by its operation
identity without its output being a timeless cache entry.

Dynamic fan-out records the expansion snapshot and stable item keys before child
admission. Keyed streams carry item identity, correlation, event time, sequence,
watermark and schema. Late events and correction/retraction policy remain explicit.
For mutable workspace reads, retain a snapshot/content version or mark the effect
nonhermetic; a later hash cannot prove which bytes the process actually consumed.

## 3. Owned C++/Lua kernel: concrete contracts

These are proposals for the complete destination, not a claim that all components
must be native or that delivery should begin with a generic platform.

### Three execution forms, one history

1. **Live procedure:** arbitrary Lua control with native effects and structured
   scopes. It is cancellable/inspectable, and survives replacement only at explicit
   native checkpoints or through independently owned jobs. No arbitrary closure,
   C stack, socket or userdata snapshot promise.
2. **Replay-safe durable procedure:** deterministic Lua code emits native commands;
   time/random/model/tool/event results are recorded observations. Recovery reruns
   pinned code and checks command identities/digests against retained history.
3. **Explicit resumable segment:** serializable owned values plus a named program
   counter and pending native handles. The segment restores directly without
   replaying its lifetime. It supports intentional schema and code migration.

All three use the same TaskId, EffectId, ScopeId, message/occurrence/artifact IDs,
cost counters and causal references. A workflow can call any form, but its inspection
shows the actual continuity boundary. Frontends cannot claim stronger recovery
than their chosen form supplies. Durable Lua restricts unrecorded external reads,
native effects and unordered iteration; separately scheduled coroutines cannot
mutate shared workflow state in nondeterministic host order. Native input batches
provide the order visible to deterministic orchestration.

### Atomic transition batches and the dispatch frontier

A single transition can consume event IDs, update serializable state, choose join
winners, enqueue children/effects, release/acquire permits and advance an inbox
cursor. Commit those obligations together to the existing owned binary storage
boundary. An outbox emits effect intents only after that batch is durable. Effects
may concurrently stream results and bytes by retained references; their completion
becomes another transition, not a hidden callback mutating Lua state.

Every effect adapter declares: repeatability; idempotency-key domain and retention;
dispatch acknowledgment strength; status query/reattach support; cancellation
semantics; fenced ownership if supported; reconciliation strategy; compensation;
cache eligibility; authority and relevant environment identity. Admission, dispatch
and outcome recording are separate events. Missing result after dispatch means
uncertain outcome. Recovery must query/reattach, retry under a justified idempotency
contract, or surface a durable reconciliation obligation. It cannot fabricate
failure and redispatch a shell mutation.

A lease/epoch rejects stale controller completions in the local store. It prevents
external writes only if the external resource honors that fence. A PID alone is
not a process identity: use job-owner identity plus host/boot/process-start data and
owned stream offsets. Transport disconnect does not prove provider cancellation.
Compensation is a new recorded effect with its own uncertainty and failure policy,
not history deletion or automatic inversion.

### Joins and settlement

An all/any/quorum join defines membership (closed set or explicit incremental
expansion), acceptable outcome predicate, ordering/tie rule, insufficiency condition,
deadline and loser policy. At-least-k success and k agreeing independent reviewers
are different predicates. Predicate evaluation may itself be a recorded model
decision; do not pretend a native quorum proves agreement quality.

The writer records exactly which accepted outcome IDs satisfied the join before
starting downstream effects. Replay returns those outcomes even if later results
arrived meanwhile. `result_ready` means the caller can proceed with that selection;
`scope_quiescent` means owned active execution stopped or ownership was explicitly
transferred. A loser can finish, fail cancellation or produce an uncertain effect;
each remains observable. A timeout creates a deadline observation and cancellation
policy, not a fictitious terminal result. Scope exit must retain cleanup obligations.

Finite nursery scopes favor cancel-and-settle or collect-and-settle. Resident
supervisors define restart groups, restart intensity, accumulated cost limits and
escalation. Shared services remain separately governed consumers' dependencies;
Blackbird restarts its own clients, not the entire kernel/database service.

### Capacity without nested deadlock

Apply one admission mechanism to command CPU/memory/I/O permits, provider in-flight
limits, tokens/money, workspace mutation claims and remote executor capability.
Distinguish queue timeout, start-to-close timeout and whole-scope deadline. Waiters
do not consume the execution permit needed by their children. Multi-resource
requests are atomic or use a declared global acquisition order. Reject a parent
waiting for children while retaining all capacity they require.

Use fair queued admission by participant/scope/priority with bounded priority
inversion, not one flat semaphore. Stream credits bound both messages and bytes;
persisted spool limits remain explicit. Retry sleeps release execution capacity.
Budgets inherit and reserve through the scope tree. Cancellation/reconciliation
and recorder/control capacity remain available during overload. These policies
are native values inspectable by humans and models, and changes take effect at
recorded boundaries without rewriting already admitted work's contract.

### Evolution and intervention

Pin source artifact, modules, input/state schemas and native callable ABI per run
or segment. Discovery can resolve latest definitions; active work resolves its
effective revision. Apply staged changes at the operator's selected affected-work
boundary. Interrupt-and-apply first stops admission and settles/reattaches affected
work, then commits a migration edge naming old/new revisions and transformed
state. Declared retained handles must still be type/authority compatible.

A replay-compatible fix checks a historical command prefix without dispatching
new effects. An incompatible topology change starts a new segment/run using
selected prior results and explicit ancestry. Fork means a new execution lineage
with deliberate effect reuse policy; it does not undo external history. Persistent
obligations and live job owners survive segment rotation so bounded history does
not orphan descendants or waiting events.

## 4. Performance is a semantic design input

The libDSE paper shows a stronger alternative to synchronously persisting every
internal operation: speculative message-passing state, atomic action blocks,
causal recovery graphs and barriers before external observers. It requires
application changes; it does not make arbitrary third-party services rollbackable.
Failure recovery may roll back extra in-memory state, and mismatched persistence
rates reduce the benefit. **Read:** sections 3–5 and 5.3 of
[Li et al., Distributed Speculative Execution](https://arxiv.org/abs/2412.13314),
[acquired PDF](distributed-speculative-execution-2412.13314.pdf).

**Proposal:** exploit the distinction between reversible planning and irrevocable
release. Group commit transition batches; evaluate pure dependency planning and
presentation speculatively; coalesce durable state changes where no external
observer/effect needs an earlier cut. Irreversible dispatch, durable acceptance
responses and reusable published results wait behind a durable frontier. If
Blackbird later permits speculation across cooperating outposts, carry causal
dependencies and implement rollback/fencing first. Never infer rollback support
for provider calls, files or kernels merely because local state can roll back.

Avoid full transcript copies in each task/checkpoint/DAG edge; retain originals
once and reference bounded projections. Rotate histories into explicit linked
segments with snapshots and outstanding obligations. Ready queues and wait indexes
replace scanning every future or every schedule. Persist only semantic transitions;
stream blobs have chunk/offset retention separate from scheduler events. Join state
updates should cost work proportional to affected outcomes, not all descendants.
Benchmark p50/p99 runnable-to-dispatch, cancel-to-quiescent, restart cost by history
length, scheduler CPU/byte traffic, admission fairness, and worst-case backlog.
The direct oracle checks behavior at the same time; speed obtained by weakening
durable dispatch or losing events is a failure, not a throughput improvement.

## 5. Hostile comparative scenarios

These are executable obligations for the destination. They do not assert every
named product necessarily fails them; they expose boundaries to compare directly.

| Scenario | Required Blackbird behavior | Mechanism exercised |
|---|---|---|
| Five models race; the winner returns just before controller death, two losers mutate workspaces | Same chosen winner after recovery; loser identities/outputs and mutation obligations retained; actual cancellation/drain known | Durable selection plus effect settlement |
| Crash after remote shell mutation, before completion record; then replace controller | Reattach/query stable job or represent uncertainty; no blind mutation replay | External acknowledgment and job continuity |
| Two same-name steps are reordered with changed inputs | Explicit replay mismatch/migration; no wrong old result accepted | Semantic invocation digest |
| Million-item dynamic fan-out with a slow recorder/provider | Bounded admission/bytes, fair interactive progress, stable item keys and cancel route | Expansion, flow control, performance |
| Parent holds all provider slots and awaits children requiring those slots | Detect/reject or release/transfer permits; no hidden deadlock | Structured resource ownership |
| Three-level supervisors each retry a provider failure ten times | Shared retry/cost budget prevents 1,000-attempt amplification | Supervision and budget inheritance |
| 01:30 local daily schedule across fall-back DST, outage and tzdb change | Stable occurrence IDs and chosen fold rule; bounded catch-up and old interpretation retained | Civil/logical/elapsed time |
| Delete one cached output; change one compiler/environment/input; leave others intact | Rebuild exactly demands that lost validity; keep intact independent outputs | CAS, lineage, selective invalidation |
| Stream file/context pairs arrive in opposite orders and late corrections arrive | Pair by key/version, retain late-data policy; never pair by arrival accident | Keyed dataflow and watermark semantics |
| Pause/edit a statechart with an already-started remote invocation | Restore/migrate active configuration and reattach/reconcile invocation independently | Statechart versus effect continuity |
| Fork a prior run that already published a release | New ancestry; explicit publish dedup/re-execution choice; prior external action remains | Counterfactual boundaries |
| Refit local Blackbird while a shared database serves other Blackbirds | Own tasks/clients quiesce; database remains independently governed and operational | Ownership boundary |
| New operator instruction arrives after signal acceptance, before next model request | Accepted/observed/applied identities and resulting context revision visible | Intervention and context causality |
| Full data spool while shutdown is requested | Cancellation/control remains deliverable; loss/overload policy observable | Reserved control capacity |

Use a deterministic model of run/effect/scope transitions with injected clocks,
completion order and crash cuts as the direct orchestration oracle. Adapter tests
attack actual OS/provider/transport fault classes separately. Durable storage tests
inject truncation/commit faults. Do not build a second receipt layer to certify
these observations. No laptop mutants.

## 6. Pressure test of explicit segments plus optional replay

The root design's choice of explicit durable segments as the execution substrate,
with recorded-command replay as an optional frontend, is stronger than making
every workflow pretend to be a serializable language stack. These constraints
must accompany that choice:

- A segment takes owned serializable values and emits its next state, commands,
  consumed inbox IDs and waits as one decision. Lua references, metatables,
  coroutines, functions, sockets and arbitrary userdata cannot cross the durable
  state boundary. Resource/context/artifact/task handles are typed native IDs.
  The codec defines canonical ordering, integer width, byte/string distinction,
  finite number behavior and bounds; JSON cannot become a hidden internal schema.
- Pure Lua planning still needs a pinned runtime/module/native-callable manifest.
  `pairs` over unordered maps, wall clock, random seeds, filesystem reads, mutable
  module state and host callbacks can change command order or inputs. Offer an
  isolated replay environment with ordered collections, recorded time/random/
  reads and scheduler-delivered observation batches. Pin or forbid math/native
  behavior whose cross-platform variation can change command emission. Source
  review is not a runtime guarantee that an unrestricted Lua module stays pure.
- Match structural command identity **and** semantic input/schema/callable
  manifest. A stable textual key alone conceals changed arguments; hashing only
  whole workflow source makes every harmless edit prohibit old-result reuse.
  Retain program revision separately, and make replay compatibility an explicit
  transition with a checked historical command prefix. Handle duplicate dynamic
  keys, removed historical commands and command-type changes by refusal.
- Partial reexecution is selective invalidation/new execution ancestry, not
  mutation of old history. Reuse selected immutable observations/artifacts under
  declared policy; issue new attempts for changed effects. A branch that already
  wrote externally remains part of history even if downstream planning ignores it.
  An old result may satisfy a new demand without being mislabeled as a fresh
  effect. Cache bypass also must specify whether in-flight work may be shared.
- Replay suppresses new physical effects and distinguishes historical observations
  from fresh execution in audit. It does not suppress the audit of a new replay,
  migration, selection or intervention. Group commit can batch semantic records;
  it cannot silently admit/dispatch external work ahead of the durable audit cut.

**Timezone dependency boundary:** implementing an owned TZif reader/calendar
resolver is compatible with the no-new-library rule. Timezone rule data is an
explicit versioned input, retained by content identity; no process-global `TZ`
mutation or libc's currently installed rules can define historical semantics.
Current [RFC 9636](https://www.rfc-editor.org/rfc/rfc9636.html) replaces RFC 8536
and covers v1–v4 files, 64-bit transition tables, footer rules and missing/truncated
validity. Read sections 3, 4, 6.1 and Appendix A before implementation. Preserve
the range over which the retained data gives an answer; outside it, use supported
footer rules or return unavailable rather than inventing the last offset forever.
Leap-aware versus UNIX-time data must be explicit. “Fall back” is not universally
one hour. [IANA's source/data account](https://data.iana.org/time-zones/tz-link.html)
is the rule-data source; acquiring data does not select a runtime library.
Exceptional external timezone-library adoption would need operator discussion.

**libDSE feasibility boundary:** local pure planning and display can be speculative
because they can be recomputed and have no released effects. A graph of independent
outposts cannot use the paper's protocol transparently unless each participating
state owner supports rollback, causal message metadata, incarnation handling and
barriers. The paper explicitly leaves preventing simultaneous old/new state
writers to the service implementation. Therefore the immediate fast design is
batched commits and local speculative planning with a durable dispatch frontier;
cross-outpost rollback is a separate advanced execution profile, requiring its
own direct fault model. Neither approach weakens core audit requirements.

## 7. Source scope, acquisition and remaining gaps

Fresh Temporal core and DBOS archives/revisions/SHA-256/restoration commands are
in [durable source manifest](durable-source-manifest.md). Existing Restate, Windmill,
Dagger and Guile references were read in place and remain covered by the repository
manifest. References were not executed. The Temporal API renamed/redirected
`sdk-core` to `sdk-rust`; the intact ZIP root was inspected before clean extraction.
Full source acquisition does not imply exhaustive code review.

Dagger's existing typed conversation/agent handles, mailbox correlations, control
revisions and reseed/drain refusal are relevant integration ingredients; its
[pinned dossier](../capabilities/dossiers/dagger.md) remains the detailed study.
This lane additionally read
[paused-drain reseed test](../../quarantine/dagger/core/agent_reseed_test.go#L11)
and [control publication](../../quarantine/dagger/core/agent_control.go#L136).
Those do not establish durable arbitrary external effects.

Dagster/Prefect/Argo/Nix/Bazel/XState and current OTP are documentation/protocol
studies here, not audited scheduler implementations. Source-level frontend lowering,
timezone parser behavior, fairness algorithm qualification and remote executor
ownership remain implementation-unit studies. No inaccessible essential reference
requires operator purchase. Claims that Blackbird already surpasses these systems
would be premature; the design makes the intended superiority concrete and testable.
