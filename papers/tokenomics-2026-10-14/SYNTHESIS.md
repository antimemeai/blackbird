# Token caching and token economics

2026-10-14. Frumentarii after archive merge `d74f248`. Research and proposed
native design; no cache feature, account qualification or measured saving is
delivered by this document. Start with the [native inventory](native-inventory.md),
[provider contracts](provider-contracts.md) and [mechanisms study](mechanisms.md).
The [campaign](CAMPAIGN.md) records scope and acquisition boundaries.

The immediate opportunity is stable request assembly, honest accounting and
better use of existing Lua/context views. Two adapter defects come first. A
generic cache switch would conceal important differences between the actual
model, endpoint and account routes.

## What the pass established

* The default loop sends the complete live history and all current tool schemas
  every step. Task changes and triggered budget values enter the leading
  instruction string. Preserve stable instructions, schemas and context prefixes
  where useful, while keeping changing operational state visible to the model.
  Moving text changes a prompt; it requires an explicit behavioral design and
  direct checks, rather than a claim of transparent byte optimization.
* Owned OpenAI transport skips the native-to-external conversion used by the
  compatibility transport. Native tool arguments/results can reach that route
  with different JSON types. Its HTTP 429 mapping also bypasses the main retry
  classifier. These are source-derived inconsistencies, with direct regression
  seams in the inventory; no live rejection or failed recovery was observed.
* Main `/stats` selects counters from the last returned response in this process.
  It is not an all-attempt ledger. Cache writes, colleague/helper usage and failed
  attempts cannot be priced by reading only that object. Raw external bodies and
  attempt links are the appropriate starting point for an independent reader.
* Current provider caching is model- and route-specific. Modern OpenAI cache
  writes can cost more than ordinary input; reads are discounted. Earlier-model
  retention/key advice is not a safe universal default. Public API pricing does
  not establish subscription allowance consumption. The provider report records
  the current guide/reference breakpoint discrepancy and account uncertainties.
* Observation selection, Lua composition, compaction, request-prefix reuse and
  durable artifact reuse address different costs. A provider cache hit still
  makes a new generation. It cannot establish a settled effect or authorize
  reusing a previous answer.

These conclusions combine inspected local source with dated primary documents;
they do not establish today's active account, backend residency or task quality.

## Proposed native design

### One external representation per supported route

Native context continues to store typed values. Apply the Responses boundary
conversion once for every OpenAI main/colleague route that consumes those values;
retain the native request and the exact actual external bytes with their existing
attempt linkage. Keep provider-auth transport generic for protocols whose callers
already supply external-shaped values. Do not stringify and parse internally.

Classify retryable failures using the actual adapter's typed status, with the same
policy for an equivalent main request across owned and compatibility routes.
Preserve separate attempts, partial streams, cancellation and terminal failures.
Changing retry eligibility does not authorize replay of completed tools.

Pin the non-secret provider/account/revision/kind/origin/protocol binding for
the assembled request group, and compare it during dispatch and each retry.
An account/route change is a conflict before submission, not permission to send
the same cache policy elsewhere. Ordinary token renewal for the same bound
account remains allowed; the current refresh path preserves its account revision.
Historical accounting uses each attempt's actual recorded identity, and marks
an absent identity unknown. Context head/model/group ID cannot invent one.

Keep conversion caller-owned: direct owned colleagues bypass `openai_http`,
although today's tool-free wrapper already has external-shaped inputs. The
429 rule must match the actual HTTP condition, not every `provider_rate_limit`
enum instance: negative refresh-backoff sentinels have a different meaning.

### A tertiary usage reader

Build a native read-only consumer of pinned retained/raw prefixes. Parse each
supported external response into a derived observation carrying its source
locator, request group/attempt, actual response model and route, reported service
tier when present, original usage fields and acceptance/failure disposition.
Resolve account identity only through existing non-secret identities; never copy
credentials into accounting or request cache keys. Report unsupported formats,
unlinked records, missing/corrupt bodies and bounded scan coverage explicitly.

The consumer derives per-attempt and selected workflow/session totals. Request
and stream/wrapper copies of the same response must not be counted twice. A
retry with unknown usage remains an unknown-consumption attempt alongside known
subtotals. A cancelled or rejected response with reported usage still contributes
that observation. Main, colleague and decision-model calls retain their separate
provider units and lineage before aggregation.

Deduplicate by original/attempt lineage and reported counter scope, never by
identical response text across attempts. Claude aggregate `usage` and per-model
`modelUsage` are alternative scopes until their relationship is established;
preserve both without adding them together. Parse failed/incomplete SSE originals
directly instead of using a success-only decoder. Distinguish a known local
pre-dispatch refusal from a dispatched request with unavailable usage; an
outbound body captured before child startup does not prove remote receipt.

Use endpoint-specific field definitions. For an API where cache reads R and
writes W are disjoint subsets of total input I, estimated input charge is
`(I-R-W)*p + R*p_read + W*p_write`, with prices expressed in the same token unit.
Reasoning already included in output is not added again. Other APIs may report
ordinary input separately from cache categories. Never apply the subset equation
blindly. Missing fields and impossible overlaps produce an incomplete estimate,
not a fabricated zero or negative charge. Rates are dated, versioned inputs to
the reader; public list-price estimates are distinct from actual bills and plan
limits. Keep unpriced usage useful.

All projection, arithmetic, first-change analysis, price lookup and rendering
run in this tertiary consumer. Producers and recorder continue capturing raw
observations without formatted metric summaries or cost aggregation. Reuse
existing originals/linkage before adding instrumentation; any genuinely absent
source field needs its own minimal capture change. This reader cannot close the
separate P0 audit-completeness issue or infer unseen consumption.

### Model-controlled cache policy and request layout

Keep request-local Lua overrides. Add a typed, retained policy only when its
model/endpoint/account capability and downstream field mapping are specified.
An initial policy should distinguish provider default, an explicitly supported
policy, and unsupported/unknown capability. Explain or reject an explicit
unsupported setting; do not silently substitute credentials, billing routes or
another model. Do not emit speculative cache fields to every provider.

For eligible routes, construct stable instruction/tool fragments deterministically,
with their representation/source versions. Keep genuinely changing tasks and
budget reports in a clearly defined current-state message near the mutable end.
Use one request-local developer message after the complete selected input,
including a Lua input override. Validate all call/result groups before appending
it and preserve their order and opaque reasoning. It follows the last user item;
its text identifies operational facts rather than a new operator request.
Add it after Lua overrides, preserving automatic task/budget visibility by
default. A proposed local-only `state_context` choice of `auto` or `omit` lets
Lua suppress it deliberately; consume that choice without leaking it upstream.
Default stable advice belongs in default instructions, while the state message
contains concise labels/facts and necessary interpretation. An instruction
override still replaces the default instructions.

In `auto`, task state appears under the existing nonempty-or-revision condition,
including a current empty list after all tasks complete. Budget state appears
only when its enabled policy triggers. If neither condition holds, emit no state
message, even for empty input. Construct it for each newly assembled request;
never append it to retained context. Request inspection distinguishes this
addition from retained entries and does not offer it as a restorable entry ID.

Keep budget `input_bytes` based on the actual selected native input before
ephemeral state, including a Lua override; explicitly label this basis. This
avoids measuring a report that contains its own length. The boundary case where
inserting the state would cross the trigger still follows the pre-state count.
Final input/wire sizes are separate observations, not provider token counts.

Preserve original context revisions and audit the complete actual request,
including ephemeral assembly additions. Its final original and actual external
body identify the request; context head alone does not. Freeze state and policy
within one retry group. After restart, a new request derives fresh state from
retained policy/tasks and the effective program; it does not replay the prior
ephemeral message. Do not mutate history merely to place a cache marker.

Breakpoint placement belongs at the final provider boundary over the final
assembled request, at selected reusable content endpoints; it does not mean
always marking the last prompt item after a changing suffix. It must follow
that exact model's eligible content types,
marker limits and TTL semantics. Use conservative documented behavior where
official sources disagree; do not invent a resolution or rely on an unverified
larger lookback. A cache key needs a documented purpose and non-secret scope;
automatically making one random key per session can obstruct legitimate reuse.
No background paid keepalive/prewarm traffic is selected by this design.

Needed edits still take effect through existing activation and context rules even
when they invalidate a prefix. Pin each request revision, and retain normal
model-authored repair/compaction and provider-opaque reasoning. A native packet's
common byte prefix is a diagnostic only; the provider controls tokenization,
rendered layout, cache admission and eviction.

### Spend less work where it helps the task

Use existing file ranges, process-output retrieval, Lua return projection and
managed context repair first. An authored view identifies retained source and
omitted extent, supports exact drill-down, and preserves complete captured
originals. Central facts outside head/tail need a recovery path. A compact result
can require more calls; measure the complete useful continuation.

Lua can perform mechanical loops/branches and retain intermediate values rather
than asking the model to decide every deterministic step. Every actual command
still uses the existing effect/audit owner. Tool-schema selection and peer-context
selection are model-authored choices; no universal truncation/filter is chosen.

The prior [Jev retrieval pilot](../../../codex-tools/experiments/library-comparison.md)
retained 11/11 annotated facts with 28.1% less Codex input than full context, across
six questions. Every answer trial reported zero cached input; the helper added
62,121 input tokens. This is useful historical evidence for advisory reranking,
not a current net-cost, warm-cache or general quality result. Warm full context
can compete differently with a changing shortlist. Compare helper costs and
evidence loss against a simple native shortlist under the actual route.

Scheduling for prefix locality, in-flight sharing and reusable artifacts belong
with the proposed [concurrent/durable runtime](../../docs/CONCURRENT_DURABLE_WORKFLOWS.md).
Require readiness/fair progress and explicit exact-input/environment/freshness
eligibility. A hosted client does not own backend KV residency. Server-side
approximate suffix-state reuse and semantic answer substitution remain distinct,
unselected mechanisms.

## Implementation sequence and direct checks

This is an ordered proposal after research, not an approved eval campaign or an
invitation to activate a new executable. Each unit owns its code, direct oracles
and bounded finding-driven recheck; do not reopen the settled archive gate.

| Unit | Concrete result | Direct oracle |
| --- | --- | --- |
| 1. OpenAI route correctness | Shared boundary conversion, pinned account/route and equivalent typed HTTP retry behavior | Inject actual owned transport: native call/result continuation has specification-required external field types and exact decoded values, retained native values unchanged; 429 then success yields distinct linked attempts, unchanged submitted request and one accepted continuation; account-switch conflicts, same-account token renewal works, auth/cancel/refresh sentinel remains terminal. |
| 2. Usage reader | Bounded per-attempt observations, known subtotals, missing coverage and optional dated API estimates | Independent recorded request/SSE/colleague/helper fixtures include success, failure/incomplete/cancel, duplicates, retries, unknown fields, corruption, price classes and reasoning overlap. Assert exact counts, unknowns and source drill-down; read-only inputs remain unchanged. |
| 3. Request layout and cache policy | Explicit provider/account support, stable fragments, defined current-state message and final-boundary markers | Exact emitted request fixtures across repeated steps, task/budget changes, context repair, tool continuations, Lua overrides and restart. Assert role/order/linkage and current-state freshness. Any later cache-benefit claim requires observed provider usage on the actual supported route. |
| 4. Useful context and orchestration | Selected recoverable views and Lua composition through existing owners | Same task-critical facts, including a deliberately omitted middle fact, recover through original handles; actual outputs/effects and cancellation settlement remain correct. Report extra model/tool work and repairs alongside input/cache/output usage. |
| 5. Runtime reuse/locality | Fair ready-work scheduling and eligible artifacts in the workflow implementation | Explicit changed-input/environment/freshness/corrupt-output cases; no unknown effect replay, starvation or stale publication. Separate fixed-trace serving observations from adaptive task-quality claims. |

Units 1–3 are the immediate foundation. Unit 2 can begin alongside route repair;
route-specific cache comparisons depend on both. Account/endpoint documentation
conflicts feed the existing provider-auth work, rather than turning this pass
into a wholesale credential migration.

Beads records these whole units as `arconaut-1ud` (route repair), `arconaut-wiq`
(tertiary usage reader) and `arconaut-nan` (cache policy/layout, depending on both).
Provider/auth follow-through stays in `arconaut-uaa.5`. Research issue
`arconaut-c8h` does not stand for implementation completion.

Before any later live comparison, declare the actual account/model/route, useful
task and independent outcome oracle. Match effort, relevant context/tool variants
and stopping condition. Record cold/warm conditions, mutations/forks/tool waits,
all attempts and helper/tool costs, reported cache reads/writes, output/reasoning,
time to first token and completion. Stable bytes alone prove no provider hit;
lower input alone proves no bill reduction; a shorter successful transcript alone
proves no general quality preservation. Integrated eval selection remains deferred.

## Design review disposition

One [native/source review](design-review-native.md) found six ownership/semantic
decisions. This revision resolves N1 with request-group account binding; N2 with
the explicit ephemeral developer-message/override contract; N3 with pre-state
selected-input budget units; N4 with lineage/scope accounting and dispatch-stage
distinctions; N5 with final-original provenance and frozen retry/fresh restart
state; N6 with caller-owned conversion and HTTP-only typed retry classification.
These are proposed design choices, not tested behavior. The provider lane's
single review found no cost/capability correction and clarified selected-endpoint
marker placement. No review of these reviews or second certification is needed.

## Delivery and limits

Archive work is merged and pushed on `master` at `d74f248`; its source gate passed
132 Debug tests and owned C++/Lua checks. This pass changes research, navigation,
journal and issue records only. It adopts no library, sends no live inference,
spends no cache-warmup requests and replaces no running executable. Its doc/link
checks do not qualify the proposed product behavior. Public references and exact
source reading boundaries remain in the three lane reports.
