# Metrics and current-view integration: source report

2026-10-15. Root integration contribution. This is design/source analysis, not
new runtime measurement. Canonical designs are
[existing surfaces](../../../docs/TUI_EXISTING_VIEWS.md) and
[metrics families](../../../docs/TUI_METRICS_VIEWS.md).

## Findings

The current raw architecture emits original events, preserves them through a
separate recorder and transforms them through tertiary consumers. A UI that
computes durations, aggregates counters or samples records on producer paths
would violate that architecture. Display projections therefore arrive from
separate bounded consumers. Local chart selection and drawing do not rewrite
capture or imply an independent metric ledger.

The independent native token-usage reader is already a concrete consumer:
raw-prefix coverage, main/colleague/decision scopes, duplicates, incomplete and
invalid usage, source locators, known subtotals, observed charges and explicit
versioned estimates are meaningful existing contracts. A TUI adaptation must
preserve them instead of presenting one confident total. `/stats` is selected
last-response state, not the same accounting scope.

Trace queries are bounded, pinned metadata inspection; richer current operation
timing comes from raw originals and future transforms. Current variables and
correlation preserve observations/availability and explicit clock-domain limits.
Code commit time, observed HEAD, loaded executable and pinned program are different
facts. A visually aligned chart cannot erase unknown clock alignment or infer
causation. Evaluation selection remains deferred by operator direction; a future
results view does not launch an evaluation program.

The existing interface covers real owner semantics that extraction must preserve:
conversation drafts, palette/editor custody, task ID/version guards and declared
status distinct from job activity, private PTY text rather than terminal emulation,
session/restart fences, effective versus pending program configuration and selected
context/original recovery. UI redesign is not authorization to weaken these owners
or claim a new team/assignment/cross-session runtime.

## Design consequences

Seven metric families support different judgments: operations, execution,
usage/cost, context, interaction responsiveness, variables/comparisons, and future
experiments/evaluations. Share collection, range, timeline/plot, inspector and
coverage primitives; retain different domain meanings. Prioritize source-linked
queries over a monolithic dashboard. Preserve missing/partial/stale/zero, explicit
units/denominators, bounded examined work and new-snapshot versus pinned history.

Input receipt, local state update, action dispatch, output accepted and painter
commit identify distinct endpoints. A completed terminal write is not a physical
input-to-photon observation. The component design makes this source fact explicit;
interaction metrics should not overclaim it. Missing endpoints need capture in
their actual owner and calculations in consumers, not a timer inserted in drawing.

The first component applications should include conversation and current-session
command center, exercising different domain representations. Metrics consumers
can then reuse the same native control/local-state machinery. A blocked consumer
must not block typing/navigation or change recorder behavior.

## Evidence and limits

Read the internal identity inventory for the exact baseline. Principal sources:
[raw architecture](../../../docs/INSTRUMENTATION.md),
[raw completion/remaining census](../../../docs/INSTRUMENTATION_COMPLETION.md),
[trajectories](../../../docs/TRAJECTORIES.md),
[tokenomics](../../../docs/TOKENOMICS.md),
[current stock](../../../docs/CURRENT_STATE.md),
[tasks](../../../docs/TASKS.md),
[command jobs](../../../docs/COMMAND_JOBS.md), and
[session reopen](../../../docs/SESSION_REOPEN.md).
The separate usage implementation is `src/token_usage.cpp`; the current terminal
and ChatOutput source findings are detailed in the component report. Prior primary
provider originals and qualification remain in `papers/tokenomics-2026-10-14/`;
raw transport/recorder source studies remain linked by the instrumentation index.
No new primary acquisition is needed merely to design these consumers.

No performance result, complete instrumentation census, universal latency target,
evaluator choice or new product test result follows from these source readings.
