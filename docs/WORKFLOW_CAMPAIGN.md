# Blackbird workflow campaign — discussion and orders

2026-10-14: the operator selects concurrent and durable workflows for the new
wave and requests deep frumentarii research and a robust competitive design.
Read the [new proposed design](CONCURRENT_DURABLE_WORKFLOWS.md),
[delivery sequence](WORKFLOW_DELIVERY.md) and
[research synthesis](../papers/workflows-2026-10-14/SYNTHESIS.md).
Three source/research lanes and bounded independent design review are complete.
The W2–W6 families below map to that proposal; historical timers and W0 failures
do not restart or govern the new wave. Implementation awaits the requested
design discussion. Performance tuning is non-negotiable; affected bugs and
tuning join the next implementation closeout.

Operator2026-10-07: fresh Hermes study; resume BB autodev on workflows; aim for a
true superset and beyond of the workflow ecosystem. This authorizes the research
run and preparing implementation orders, not selecting libraries or starting
an unbounded implementation campaign before this discussion.

## In front of us

- [Fresh Hermes study](../papers/workflows-2026-10-07/hermes-study.md): actual core
  delegation/schedules/task graphs and separate community workflow plugin.
- [Workflow ecosystem matrix](../papers/workflows-2026-10-07/ecosystem.md): 22
  references across durable execution, data/scientific, automation, build/CI,
  statechart/reactive and agent-team families; 12 candidate native primitives.
- BB W0 was a real gpt-6.1-sol/medium run in a leased candidate checkout.
  Oversized request context and repeated180-second transport timeouts prevented
  its report; it was stopped and its original25-minute allowance expired.
  Audit and performance captures remain intact. Source branch candidate/workflow-foundations-2026-10-07;
  issue arconaut-3oz.1, parent arconaut-3oz. Run context/workflows-campaign,
  independent session/audit, immutable native generation, 25-minute deadline.
  Existing unknown/reserved lanes stay preserved; W0 appears in the board.

Operator subsequently authorizes W1 core implementation with persistent profiling,
workflow registration as configurable slash commands (with or without a naming
prefix), and colored POWERWORDS such as `ultracode`. The scoped order is
[workflow registration and POWERWORDS](WORKFLOW_REGISTRATION.md), issue3oz.2.
This is a new implementation unit, not a restarted W0 research allowance.

Superset means executable behavior with correct failure/recovery semantics,
not merely representing a graph or forwarding calls to another product.
Imported formats are front ends. External kernels/databases/runners remain
services consumed by Blackbird; connectors remain optional packages.

## Proposed first orders

| Order | Deliverable | Concrete demonstration | Depends on |
| --- | --- | --- | --- |
| P0 | Persist autodev resource/stack/trace captures; design action attribution | Distinguish native CPU/memory/I/O from provider wait, tools, profiler cost and missing observations; retain source/executable/capture identity. | Existing runtime |
| W1 | Versioned callable workflow definitions and inspect/start/steer interface | Human or model authors a named Lua procedure/graph, inspects effective/pending definition, changes it at affected boundaries. | W0 source map |
| W2 | Independently supervised participants and structured joins | Run three real tasks concurrently; all/any/quorum policies, deadlines, cancellation requests/observations, independently inspectable handles. | W1; useful colleague integration for model tasks |
| W3 | Restartable segments and effect reconciliation | Crash around dispatch/result/checkpoint boundaries; resume local orchestration without replaying uncertain external effects; source/version mismatch explicit. | W1–W2 |
| W4 | Events, waits and durable schedule policy | Signal a waiting run; deduplicate admitted feed/webhook events; defined timezone/DST/misfire/backfill/catch-up behavior. | W2–W3; station seams |
| W5 | Artifact/data lineage and demand-driven work avoidance | Scientific/build matrix caches only outputs justified by exact inputs, environment/code identity and effect policy; targeted invalidation after changes. | W3 |
| W6 | Alternate workflow paradigms and adapters | DAG, statechart, reactive pipeline, durable-function and agent-team programs share native task/effect identities; unsupported import semantics refuse clearly. | W1–W5 as needed |

Each order is a family, to split into small whole implementation units after W0's
actual source findings. No generic workflow platform first and no full catalog
in every installation. Useful runnable examples, direct fault oracles and measured
resource costs accompany each unit. RRC is available when native machinery changes.

Before each unit: use actual primary/reference source, write the short subplan,
choose direct failure checks, implement, affected checks, one bounded adversarial
code review, integrate valid findings, commit/push the branch and its result.
Hardening is remediation then one recheck/fix only, never a third certification
layer; max25minutes within the unit's fixed allowance. No laptop mutants.
