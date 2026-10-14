# Workflow comparison and design obligations

2026-10-14. This is a behavioral design comparison, not a benchmark or claim of
implemented superiority. Current primary/source findings are in
[agent workflows](agent-workflows.md), [durable workflows](durable-workflows.md)
and the [native inventory](native-inventory.md). The October7
[ecosystem map](../workflows-2026-10-07/ecosystem.md) adds scientific, build,
automation and alternate-paradigm breadth. Inspect those reports for exact source
revisions, evidence limits and archived material. The proposed
[Blackbird design](../../docs/CONCURRENT_DURABLE_WORKFLOWS.md) owns the deductions.

## The actual strongest comparison

[Pi Durable](https://earendil.com/posts/pi-durable/) changes the competitive bar.
Its fresh [source](https://github.com/earendil-works/pi/tree/b45e0ebcc5ae2fc42a6f2f23104d10e656e6bedb/packages/durable)
already implements retained task phases, intent before tool execution,
safe/unsafe replay, owned bottom-up abort/settlement, background work, typed
documents committed with transcript state, shared-history forks, migration and
controlled code-mode replay. These mechanisms are real design obligations.
Source study does not establish an upstream production qualification.

Blackbird's target is a richer composition: independent node progress, expressive
durable joins, pinned executable meaning, separate effect/attempt reconciliation,
incremental artifact demand, context transformation lineage, hierarchical capacity
and client/mode continuity. Being native or programmable does not establish that
composition, and none of its individual ingredients is assumed globally novel.

## Mechanisms and required advances

| Comparator family | Strong existing mechanism | Blackbird obligation beyond the inspected baseline | Direct comparison workload |
| --- | --- | --- | --- |
| Pi Durable | Durable tasks, atomic documents/transcript, shared forks, code-mode replay, structured completion and migration. | Per-attempt/effect reconciliation; content-pinned tools; all/first-success/quorum joins; incremental demand and capacity-aware execution. | Interrupt a two-effect tool, replace a same-name tool, recover a quorum with a live loser, then demand only changed outputs. |
| Oh My Pi | Persistent transcript roster, parked agent revival, generation-safe replacement and explicit kill tombstones. | Durable task/effect frontier and revocable publication across revived participants, not only session revival. | Delayed old reply after kill/recreation cannot satisfy a new task or publish under a new generation. |
| Claude teams and Codex agents | Specialist participants, independent contexts, steering and parallel work; Claude direct peer messaging/task claims. | Stable whole-workflow recovery, arbitrary configured topology, tool-using continuation, durable aggregate resource policy and source publication. | Parent dies during heterogeneous fan-out; independently completed work survives and pending effects remain identified. |
| Hermes and Gas City | Parallel delegation, durable schedule/wait/claim state, actor holds and reconciliation. | Real cancellation/settlement observations, source-fenced publication, shared hierarchical budgets and custody limits. | Silent worker loses publication authority without declaring its remote effects stopped or retrying them blindly. |
| LangGraph | Parallel task writes with superstep checkpoints, replay/forks, durability choices and delta channels. | Successors progress independently outside global rounds; source-matched effect recovery and bounded current-state recovery. | One long sibling does not delay successors of a completed branch; crash retains completed sibling results. |
| Agent Framework and CrewAI | Functional/graph composition, replay keys, checkpoints, interactive forks and downstream invalidation. | Exact structural/input/executable matching and effect-aware invalidation with complete capture failure semantics. | Reorder identical-name steps, edit prior output, delete an artifact, and prevent accidental second publication. |
| Temporal, Restate and DBOS | Durable commands/results, signals, timers, versioning, queues; atomic internal state/outbox or database-specific transactions. | Compose those semantics with mutable model context, native command custody, explicit outside-effect uncertainty, rich live revisions and source proposals. | Crash after external action before result commit; reconcile stable attempt and continue unrelated branches. |
| Argo, Airflow, Prefect and Windmill | DAG/code flows, dynamic fan-out, named checkpoints, failure rules, schedules/backfill and service connectors. | Shared native identity across paradigms and participant programs, explicit conditional joins and model/operator interventions. | Fail one branch, choose drain versus cancel, survive downtime and reproduce DST occurrence selection. |
| Dagger, Bazel, Make, Nextflow and Dagster | Environment/content identity, demand-driven work, materialized assets and output-dependent resume. | Context/source/model lineage integrated with actual artifact validity and effect eligibility, not a chat-only cache. | Change compiler/input, delete one output, and rebuild only affected demanded outputs while old evidence remains. |
| OTP, structured concurrency and XState | Supervision, scoped lifetimes, cancellation propagation and restored actor/statechart state. | Durable selection/ownership across process death, explicit local versus remote settlement, shared retry budgets and state/effect migration. | Deep supervisor failures do not amplify attempts; restore state without repeating old entry effects. |

Rows describe studied baseline boundaries. They do not assert that a competitor
could never supply an extension implementing the Blackbird behavior. All foreign
services remain consumed services; no connector qualifies local semantic parity.

## Where the proposed system should lead

The intended advantage is ordinary access to a single machine for all of these:

1. **Useful concurrency without workflow-wide rounds.** A model, child tool,
   stream, build and event waiter can each advance from its own dependencies.
2. **Recovery with preserved meaning.** Retain actual selected code, input,
   context and effect identity; do not reconstruct an old action using a new tool
   discovered by the same name.
3. **Live experimental programming.** Models write and evolve workflow programs,
   context transforms and alternate paradigms, with isolated candidate state and
   a real conclusion/interrupt activation contract.
4. **Incremental intelligence and computation.** Share original evidence, demand
   affected derived outputs, reuse eligible artifacts and keep side effects outside
   cache equivalence. An old model result remains an old result, not new truth.
5. **Explicit control of incomplete work.** Result readiness, local settlement,
   unknown past outcomes, potentially live remote work and publication authority
   remain independently inspectable.
6. **Whole-machine ergonomics and cost.** Aggregate resource policy, isolated
   workspace leases, attachable clients and complete original capture permit
   aggressive work without accidental oversubscription or lost observations.

## Demonstrations required before superiority claims

Compare complete actual work against supported competitor configurations. Retain
the declared input, source versions, resource envelope, effect/original oracles,
all outcomes and all performance samples. Separate expressibility, fault semantics,
interoperability and speed. Equal-resource and equal-time comparisons answer
different questions. Documentation absence is not an executed competitor failure.

The flagship durable repair campaign in the design combines heterogeneous workers,
isolated source proposals, real builds, independent tests, early qualified join,
controller crash, effect reconciliation, context repair, changed-input demand,
workflow revision and station attachment. A stronger claim requires that composed
behavior, not a checklist or a successful happy-path script.

The design's oracle table names failure boundaries directly. It does not create
a universal ranking score, another approval mechanism, or a separate certification
campaign. Performance tuning remains non-negotiable and is measured on the actual
workflow paths as they are delivered.
