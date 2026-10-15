# Concurrent and durable workflow delivery sequence

Implementation sequence approved by the operator on2026-10-15. Read the
[design](CONCURRENT_DURABLE_WORKFLOWS.md) and
[research campaign](../papers/workflows-2026-10-14/CAMPAIGN.md). This refines the
W2–W6 families into coherent ownership units. Implementation proceeds in the
isolated workflow worktree; library adoption and main integration remain separate.

The first native host/state/executor/join path is implemented and qualified
(source checkpoint `7b99b6c` on the isolated work branch); its
[usage and actual scope](NATIVE_WORKFLOW_HOST.md) distinguish the delivered
substrate from the remaining F1–F12 capabilities. See the
[implementation plans and code reviews](../papers/workflows-implementation-2026-10-15/PLAN.md).

## Destination and first actual use

The first useful use is three independent tasks—native build/test work and a
selected-context colleague—with a shared all/first-success/quorum wait surface,
real operator control, source-pinned inputs, protected cleanup and a retained
frontier. Durable segments then recover those identities through actual crashes.
This delivery sequence constructs the complete destination. Early units are
operating foundations rather than permission to leave the rest as unowned ideas.

Performance attribution and required tuning accompany each real path. Existing
File/audit/recovery bugs and performance investigations join the next wave's
closeout according to the actual changed boundaries; they do not force this design
back into an instrumentation campaign. No optional-original/TTL policy superseded
by complete audit is revived.

## Owning units and dependencies

| Unit | Whole invariant and useful delivery | Depends on | Direct oracles and performance work |
| --- | --- | --- | --- |
| F1 Native ownership and task facade | Independent session execution host with one retained writer, fair inbox, task-local context/config/program facade, atomic application cells and shared client submissions. Extend actual exclusive storage transfer. | Existing RetainedState, Lua and command custody. | Wrong-thread/grant/reentry refusal; completion/control fairness; stale multi-cell update commits no effect; no worker writes parent context; closing every client preserves one owner. Attribute owner transition and wake/I/O costs. |
| F2 Concurrent scopes and executor ownership | Native/command/current colleague adapters, keyed task admission, standing scopes, aggregate permits and protected cleanup. | F1. | Independently counted simultaneous execution, nested-grant saturation, cancel/EOF/reap/late controls, no checkout reuse with live users. Compare complete work with current participants. |
| F3 Joins and dependency progress | All/first/first-success/quorum/select/collect, stable member expansion, explicit failure/loser policy and wait-graph diagnostics. | F2. | Controlled winners/failures/cycles, queued controls under full lanes, unrelated successor progress, selected result separate from scope drain. Measure ready-to-successor latency/tails. |
| F4 Durable segments and recovery | One decision commits state, consumed events, stable commands and waits; guarded release and explicit proven-unreleased successor-attempt recovery preserve logical identities; extend saved-state reducer/coverage. | F1–F3. | Actual crash cuts at decision/release/dispatch/result/selection, cancellation-before-release, complete referenced-byte retention; independent effect counters, exact originals and zero unintended replay. Measure reopen versus live frontier/archive size. |
| F5 Effect reconciliation and tool-using participants | Capability-described attempts, explicit reconciliation/retry/compensation, task-local request/tool loop and independent context branches; late callback fencing. | F2/F4; provider-specific qualification. | Two-effect interrupted tool; query/dedup versus unqueryable effect; actual final provider input/tool linkage; aggregate nesting; source publication with held lease. Measure complete owner calls and branch context costs. |
| F6 Recorded-command Lua frontend | Single-owner rooted async continuations, deterministic command trace and manifest/input matching; explicit checkpoints lower to F4. | F4/F5. | Replay known/pending commands; same-name changed input/tool, unordered expansion and time/random mismatch; yield/error/allocation/cancel/generation lifetime. Measure reconstruction and payload sharing. |
| F7 Durable inbox, streams and time | Atomic event acceptance/consumption/waits, source ACK contracts, local timers, bounded keyed streams and explicit civil schedule occurrence policy. | F3/F4; timezone mechanism before civil schedules. | Registration/delivery/ACK crashes, exact cursor semantics, late keyed items, EOF/window close, DST/fold/gap/downtime/backfill. Measure idle cost and bounded fan-out/control latency. |
| F8 Artifact lineage and incremental demand | Artifact validity, eligible action reuse, dynamic graph revision, targeted invalidation and input/demand-revision-fenced source proposal publication. | F4/F5. | Deleted/corrupt output, changed input/environment/toolchain, changed branch outputs, explicit actual fresh execution, denied old-demand publication/selection. Measure work avoided and actual invalidation cost. |
| F9 Live run evolution and context forks | Complete manifests, checkpoint/fork policies, isolated migration, explicit interrupt/apply and source/attempt/branch preservation. | F4–F6/F8. | Old callbacks, aliasing migration failure, outstanding tool interval, fork of already published action, default conclusion activation. Measure candidate preparation/reconstruction and retained bytes. |
| F10 Workflow paradigms and adapters | Lua durable-function/DAG/statechart/actor/saga/dataflow/asset modules and semantic import manifests over the same runtime. | Relevant F3–F9 primitives. | Equivalent-policy fixtures agree on actual effects; incompatible imports refuse; matrix/dynamic graph/orthogonal statechart examples do real work. Measure frontend versus execution costs. |
| F11 Station clients, remote placement and team work | Attach/detach/seat control, durable feed adapters, SSH/tool executors and future peer interfaces with capability/epoch/context isolation. | F1/F5/F7/F9; remote transport qualification. | One owner during multiple views; persist steer/profile/drafts; disconnect leaves exact unresolved handles; remote side respects fences where advertised. Measure mixed local/remote progress. |
| F12 Integrated repair campaign and closeout | Flagship composed demonstration; integrate valid source proposals, resolve affected bugs and deliver required end-to-end tuning. | Delivered paths above; explicit main/activation authorization. | Same useful work and exact effects/originals through crash/replan/fork/demand/migration/attachment; retain all samples/outliers. Fix actual attributed waste and only recheck changed paths. |

F1/F2 define the sole scheduler/retention/executor ownership before concurrent
workers touch it. F4/F5 define recovery before a frontend advertises durability.
F7/F8 are independent conceptual lanes once their substrates exist. F9/F10/F11
can advance against delivered primitives without waiting for unrelated adapters.
No unit delegates half an invariant to another owner or bundles unrelated leftovers.

F1 opens the retained state/raw grant inside the execution-host process. The
existing RawRetainedTransfer applies only between that host's threads. Adoption
of an already UI-owned session settles/closes/reopens unless a separate qualified
cross-process handoff exists; ordinary IPC cannot move process-bound objects.

## Capability tracking and incorporation

W1 registry is delivered. F1–F3 develop W2; F4–F6 develop W3; F7 develops W4;
F8 develops W5; F10 develops W6. F5/F9/F11 incorporate the participant, context,
generation and station contracts necessary to make those workflow families useful.
Native refit/standing-process preservation and E2EE multiplayer retain their own
implementation units; F11 advertises actual support, never imaginary continuity.

Before implementation, create child beads for the selected conceptual units and
dependencies, link this design/sequence and record the affected source boundaries.
Do not reopen closed W1 or bootstrap units just because an old queue says queued.
Research task arconaut-30y closes when the requested design package is delivered;
it is not the implementation epic. Destination epic arconaut-45a records F1–F12
and the flagship acceptance; it is now in progress. Child arconaut-45a.1 owns the
first integrated native execution path, now delivered. Follow-up owners are
arconaut-45a.2 (task-local participants/scopes), 45a.3 (durable segments and explicit
recovery/frontier restore), 45a.4 (recorded Lua, depending on both), 45a.5 (required
complete-work tuning), and 45a.6 (Linux ASAN symbolizer repair). Later owning
tasks/dependencies are created when selected. The epic stays in progress until
the complete campaign is delivered.

## Unit practice and bounded review

Write the concrete purpose, source/literature consequences, changed invariants,
independent expected behavior and resource use before source changes. Red checks
attack the chosen fault seam, then implementation/affected green and relevant
source diagnostics. Layer one is remediation/fix with one consequential attack;
layer two is one finding-focused recheck/fix. Never a third layer or an assurance
loop. No invented whole-unit wall-clock cutoff replaces finishing authorized work.
Mutation remains deferred and never runs on the laptop.

Checkpoints preserve source, actual selected results and continuation notes;
commit/push completed conceptual units and meaningful experiment branches.
Main integration and running-executable activation are separately explicit.
Use supported Beads backup and record errors honestly. Existing ignored evidence,
references and operator artifacts remain intact.

## Implementation decisions to settle concretely

- Native record layout and schema versions; stable structural key representation
  and exact input equality; run/scope/wait/reconciliation projection reducers.
- Execution-host launch/lease/IPC boundaries, original raw-storage grant transfer,
  rooted Lua continuation lifetime and supported native executor cancellation.
- First actual tool-using provider path and exact request/session capabilities.
- Calendar/zone parsing against versioned timezone data; discuss proposed library
  adoption before choosing one. Owned UTC timers can precede that qualification.
- Artifact/environment declaration and validity policy; workspace/buildtree lease
  integration with existing candidate machinery.
- Baseline-informed performance objectives and finite comparison workloads for
  each delivered path, including the non-negotiable integrated tuning round.

These decisions belong to their owning units. They are not an excuse to select
the whole dependency graph in advance or turn optional service adapters into
prerequisites for local native execution.
