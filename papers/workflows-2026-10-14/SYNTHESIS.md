# Concurrent and durable workflow research synthesis

2026-10-14. The operator chooses this capability wave and requires an ambitious
design actively seeking to transcend coding-agent workflow offerings. The
proposed result is [concurrent and durable programmable workflows](../../docs/CONCURRENT_DURABLE_WORKFLOWS.md)
with a [delivery sequence](../../docs/WORKFLOW_DELIVERY.md),
[behavioral comparisons](COMPARISON.md) and completed
[design attack resolutions](REVIEW_RESOLUTION.md).

## Existing Blackbird and the actual competitive bar

The [native source inventory](native-inventory.md) establishes substantial reusable
machinery: retained typed decisions/effects, immutable originals, Lua/source
retention, command custody, concurrent context-only workers, callable workflow
definitions, context repair, candidate leases and current-state/suffix recovery.
It also establishes that the current workflow path is synchronous, its parked
continuations are volatile, workers reopen as unknown, and the shared CodingEngine
cannot simply be called by several new worker VMs. A dedicated native execution
owner and task-local facade are necessary architectural work.

The [fresh coding-agent study](agent-workflows.md) materially updates the older
corpus. Pi Durable is the strongest newly found comparator: retained phases,
intent-before-dispatch, owned settlement, atomic documents/transcript, shared
forks, migration and deterministic code-mode replay are already source mechanisms.
Oh My Pi has generation-safe cold revival/tombstones; CrewAI has checkpoint forks
and downstream invalidation; Agent Framework has structural concurrent replay
identity; LangGraph has task writes and delta channels; Hermes/Gas City have
schedule/claim/wait/reconciliation machinery. Basic persistence, forks and agent
trees are not sufficient competitive ambition.

The [durable systems study](durable-workflows.md) supplies complementary rigor:
Temporal command-history matching; Restate state/outbox and recorded completion
ordering; DBOS database-local atomic effects versus outside at-least-once steps;
Windmill named child checkpoints; Argo drain policy; OTP supervision; structured
channels; versioned civil schedules; content/action indices and actual output
validity. None converts arbitrary outside effects into exactly-once execution.

## Proposed architectural answer

Build one owned C++ execution runtime over the existing retained ledger. A session
execution host owns it independently of terminal clients. Workers execute effects
and return preserved observations; they never write another task's context/ledger.
Tasks progress from their own dependencies rather than global rounds. Each Lua VM
and suspended continuation has one actual owner.

Explicit durable segments define reference semantics: one decision commits selected
inputs, state, stable commands, consumed inbox, waits and publication intents.
Recorded-command functions give models familiar imperative composition over the
same semantics. Source/module/tool/runtime manifests and full replay frontiers
prevent same-name replacement or omitted commands from changing recovered meaning.
Process-lifetime arbitrary Lua remains useful with its actual durability boundary.

All/first/first-success/quorum/select/collect joins record selection independently
from scope closure and loser settlement. Ordinary recovery preserves logical
identities; new incarnation and retired/successor actors are distinct. External
effect uncertainty has its own observations and reconciliation capability, not one
success/failure flag. Proven-unreleased recovered work records a successor attempt
under its original invocation; released uncertainty never replays automatically.

Authored application cells, participant context branches and artifact/demand graphs
retain versioned lineage. Live invalidation revokes current publication eligibility;
old outputs remain available under old lineage. Reuse requires exact eligible
inputs/environment and intact outputs. Nested concurrency/retries consume shared
grants, parked coordinators return physical workers, and active effect leases/
capture/cleanup survive waits. Messages, schedules, paradigms and station clients
compose over that runtime rather than acquiring independent control planes.

## Research and review artifacts

- [Campaign and scope](CAMPAIGN.md).
- [Agent source acquisition](agent-acquisition.json): seven intact fresh archives,
  exact source revisions/hashes/restoration and clean study extractions.
- [Durable source acquisition](durable-source-manifest.md): fresh Temporal/DBOS
  snapshots, retained libDSE paper and reused reference sources.
- [Foundation acquisition](theory-acquisition.md): structured-concurrency original
  HTML and self-adjusting-computation PDF. The latter is acquired background
  literature; its formal claims are not asserted as Blackbird guarantees.
- [Native attack](review-native.md), [durability attack](review-durability.md),
  [expressiveness attack](review-expressiveness.md), and their one focused rechecks.

All source acquisition was study-only. No third-party runtime/tests/installers,
model/provider/evaluator calls, native product tests or performance campaigns ran.
No production code or library adoption was made. Rhizome's research-watch directory
was unavailable and no findings were invented from it.

## Useful delivery and performance

The sequence begins with sole execution ownership, task-local programming, real
scopes/executors and structured joins, then durable decisions/recovery and effect
reconciliation. It continues through recorded Lua, inbox/time/streams, incremental
artifacts, live revisions, alternate paradigms and station/remote clients. Each
is a complete conceptual unit with source consequences and direct fault oracles.

Performance tuning is required across these delivered paths and the integrated
repair campaign. Measure complete useful work, fair progress, owner waits, native
copies/bytes/I/O/wakeups, recorder lag, live-frontier recovery and every latency
sample. Preserve the raw producer/recorder/tertiary architecture and all originals.
There are no invented speed results or arbitrary RSS/wall-clock cutoffs.

The flagship demonstration composes heterogeneous tool-using investigators,
isolated source hypotheses, real parallel builds/tests, selected quorum,
controller crash/reconciliation, changed-input demand, context repair, workflow
migration and operator attach/detach. It earns the competitive claim through
behavior and measured complete costs, rather than assuming a checklist proves
superiority. Existing bugs and non-negotiable performance investigations join
closeout; they do not turn this design delivery into renewed instrumentation work.
