# Metrics and evidence views

2026-10-15. Proposed surfaces under the [TUI doctrine](TUI_DOCTRINE.md).
The present system has `/stats`, bounded trace/variable/correlation queries and a
separate native token-usage reader. These designs do not claim an integrated metrics
TUI, complete emission census, selected evaluator or benchmark result.

## Shared source and execution boundary

Maintain [raw emission → separate recorder → tertiary transformation](INSTRUMENTATION.md).
The main harness emits original raw events without metric formatting, sampling,
duration math, projection or aggregation. A separate consumer owns each transformed
query and cache. The terminal receives bounded display projections from that
consumer; it does not scan raw history or format measurements on producer paths.
Rendering labels in a UI is not permission to compute metrics in the emitter.

A query identifies session/run/source prefixes, examined-work limits, filter,
window, grouping, transform/version and next cursor. A result carries coverage,
source locators, units, missing/invalid fields and cursor/freshness. Different
sources may expose different cursors; do not invent one global snapshot revision.
Live refresh requests a new snapshot; an investigation or comparison can pin one.
Cancel stale queries and retain their displayed snapshot until replacement is ready.

Show a compact coverage line such as `Pinned run … · through record … · partial`.
Its inspector explains unlinked runs, unsupported sources, scan limits, gaps,
recording lag and unavailable originals. Unknown is not zero. Distinguish known
subtotal, complete total and estimate. Data acquisition and status observations
may continue while charts are paused. Pausing a chart never pauses raw capture.

The structured query/inspect interface is shared by humans, models and Lua. A
model receives small selected records, coverage and locators with economical
pagination; a human receives plots/tables and drill-down. Both can recover exact
source and transform inputs. New structured query names in these designs are
proposed contracts, not delivered tools.

## Distinct views for distinct judgments

| View | Principal question | Main representation | Useful next operation |
| --- | --- | --- | --- |
| Operations | What is working, waiting or degrading now? | Current owner/queue/resource collection plus short trends | Inspect the relevant work/owner; change local display scope |
| Execution | Where did this work spend time, and what depended on what? | Causal attempt timeline with expandable lanes | Open attempt, request, tool result or exact original |
| Usage and cost | What was reported or charged, to which route and scope? | Scoped counter/charge table and selected trends | Inspect response, change explicit tariff input, compare scopes |
| Context | What information reached a request and what changed? | Context composition, source lineage and request comparison | Inspect retained context versus final request; use existing CLM operations |
| Interaction | Where does control or presentation stall? | Input/action/output timeline and distributions | Inspect a slow episode and measured stage boundaries |
| Variables and comparisons | What changes co-occurred with different work? | Time-aligned categorical/numeric lanes plus matched tables | Select intervals/runs and inspect source differences |
| Experiments and evaluations | What did a named run actually test and produce? | Case/result/error matrix and explicit comparison | Inspect recorded cases/results; select a future run when separately authorized |

All share a range selector, coverage inspector and source navigation. They do not
share a mandatory dashboard layout or collapse into one score.

## Operations: current health and pressure

Start with selected session/workflow host, owner identity and observed time.
List active/waiting work, known queue depth/capacity, recorder retained/preserved
positions, consumer lag, process/resource samples and observed faults where those
sources actually exist. Distinguish process liveness from useful progress and from
freshness of the last sample. Unobserved queue capacities remain absent.

Use an overview table with selected-owner inspector. A graph may show bounded
recent history beside the current value. Distinguish application work, recorder,
transform worker and terminal work; pressure in one is not automatically the cause
of another. Domain controls remain in the owning work or configuration view.
A chart cannot kill a shared database or clear a recorder lane.

Failure behavior: a dead consumer leaves the last sample marked stale and local
navigation available; a dead recorder is a real status with source evidence, not
an empty flat line. Do not simulate current health from replayed historical events.

## Execution: causal paths and concurrent demand

A selected task/turn opens attempts and participants/workflows on separate lanes.
Show admission, dispatch, received data, settlement and unresolved regions only
where captured. Link parent/child and request/result by actual native identities.
An outcome observed later does not retroactively settle an earlier unknown attempt.
Repeated response capture is not another request.

```text
 Execution   task T · pinned source P             [range] [coverage]
 turn A    ├─ request R ───── received ── terminal
           └─ tool O  ── dispatch ─────── unknown   ← selected
 worker B       ├─ request S ───── terminal
 ----------------------------------------------------------------
 O · known dispatch, no terminal observation
 [input] [captured output] [causal links] [raw records]
```

Use monotonic deltas within their actual clock domain. Across hosts/processes,
show observed alignment/uncertainty or separate axes; no falsely precise overlap.
Separate dispatch wait, external service, local processing, recording lag and
presentation latency when the required endpoints exist. An unmeasured region is
unmeasured, not the residual confidently assigned to the network.

The delivered `trajectory_read` query is the initial metadata adapter. Current
operation timing derivation requires tertiary raw-event work; old metadata timing
must retain its historical provenance. See [trajectory limits](TRAJECTORIES.md).

## Usage and cost: reported facts and explicit estimates

Adapt the existing separate [token reader](TOKENOMICS.md), keeping its raw-prefix
coverage, main/colleague/decision scopes, source locators and incomplete/invalid
usage. Show requested and observed model/provider/account reference, service tier,
input/output/cache counters and charge fields where observed. A rejected local
request, a dispatched request with missing usage and a zero-usage response differ.

Use independent columns for observed charge with currency, provider-specific
breakdowns and optional list-price estimate with explicit dated rate input. A
known subtotal is not a session invoice. Never convert subscription allowance to
API-dollar cost or mark an authored cache policy as a cache hit. Cache partitions
and inclusive reasoning cannot be counted twice. Price estimates require compatible
route/model/tier/units and complete required partitions.

An operator may select a tariff input in the consumer; that recalculates a display
projection, not source data or provider selection. Opening the actual model/account
editor is a separate action. Model/Lua reads receive the same selected accounting
scope and uncertainty rather than a vague `total_cost` scalar.

## Context: selection, assembly and information continuity

Inspect retained context, selected request input, request-local automatic material
and final encoded request as distinct related representations. Show bytes versus
reported/estimated tokens with their units and provenance. A context-budget ratio
requires an actual compatible denominator; no fabricated token count from bytes.

The selected request opens a source-linked composition list: instruction/program
identity, selected items, tool groups, transformed/compacted content and omitted
material with available repair references. Compare two requests by added/removed/
revised sources and actual content, not a generic percentage of context quality.
Do not pretend that included text was attended to or used by the model.

Read-only metrics link into the CLM workbench for deliberate edits, compaction and
repair using existing authority. Those actions do not mutate the historical request
being inspected. Sensitive account material is represented by references, not copied
into a chart or automatic prompt.

## Interaction: useful control under load

Investigate real input episodes: source event/capture, decode, binding resolution,
local state change, domain receipt/dispatch, frame prepare, output accepted and
presentation commit. Terminal write completion is not proof that a display's
physical pixels or a person's perception changed. Controller/haptic receipt has
its own endpoint. Do not label every such interval input-to-photon.

Show event-level drill-down and distributions with count, window, units, excluded
or unresolved observations and transform identity. Separate typing/navigation,
continuous scrubbing and effectful actions. Include tail/recovery cases, redraw
bytes, allocations and work examined where actually measured. A median alone does
not establish responsiveness under a blocked stdout or extension loop.

The raw census contains UI/input/output surfaces; a finished latency model and all
pairings are not assumed delivered. Add missing original captures in their owning
implementation units, then calculate intervals in the tertiary consumer. Numeric
latency targets and benchmark workloads will be chosen from these concrete actions;
this design neither selects an eval campaign nor invents a universal frame target.

## Variables and comparisons: correlative work

Extend the current variables/correlation surface. Place actual trajectory points
against observed code identity, effective doctrine/program/configuration generations,
model route and selected numerical series. Checkout HEAD, loaded executable and
pinned source are different variables. Uncommitted changes and observation gaps
remain visible. Point observations do not prove a continuous effective interval.

Selection brushes a range or category and opens the corresponding bounded work
collection. Compare named runs with workload, environment, coverage and denominator
visible. Missing and failed cases stay in the table. Correlation supplies hypotheses;
it does not establish a causal improvement or authorize restoring code/configuration.

## Experiments and evaluations: reserved design, separate selection

Provide a future collection of explicit definitions and recorded runs with case
identities, candidate/environment, evaluator version, input references, attempts,
raw results, scores/labels/errors and explanation. Comparison preserves unrun,
invalid and failed cases. Model judgments and deterministic outcomes retain different
meanings; no automatic promotion follows from a score.

This reserves a consistent interface shape for later discussion. It selects no
evaluator, case set, automatic experiment runner or background campaign. Existing
retained result inspection can be useful without implementing execution controls.

## Controls, layout and implementation boundary

A collection plus inspector is the default entry. Open a focused chart or timeline
when needed; return restores the selected source and range. At narrow widths the
inspector replaces the collection temporarily. Local range/focus/series changes
remain available while queries run. Empty, loading, partial, stale, unavailable,
conflicted source and consumer failure have distinct readable states.

Charts offer keyboard and optional controller scrub/zoom with a textual table
fallback, exact values and copyable locators. Live following is explicit. Continuous
scrub updates a bounded local cursor and coalesces superseded consumer requests;
it cannot rewrite originals or issue effectful domain actions implicitly.

Needed reusable parts: virtual collection/table, timeline, basic plot, range
selector, source inspector, async state/coverage footer, comparison and action
registry. Needed backends: bounded tertiary projection adapters and freshness/
coverage contracts. Existing native readers and source owners remain authoritative.
The terminal and plugins do not receive a second authoritative measurement ledger.
