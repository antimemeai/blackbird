# Native review of tokenomics design

2026-10-14. One bounded substantive review of [SYNTHESIS](SYNTHESIS.md) against
[native-inventory](native-inventory.md) and the current source. Scope: request and
account ownership, accounting, route corrections, ephemeral state, overrides and
recovery. Source/document inspection only; no calls, tests, builds or product
changes. Findings below are implementation decisions or corrections the design
needs, not a certification or request for another review.

## N1 — Pin the account/route used for cache eligibility to actual dispatch

**Priority: high; request ownership decision.** The proposed policy is conditioned
on model/endpoint/account and says not to substitute billing routes
([design](SYNTHESIS.md)), but request revision pinning alone does not implement
that promise. The main provider calls `codex_login` again for each attempt; owned
login records only an `owned_auth_` flag, then `openai_http` resolves the selected
account again without an expected binding. An external account change between
policy selection and transport, or between retries, can therefore change the
account/route under an unchanged request body.
[Provider entry](../../src/coding.cpp:345),
[owned selection](../../src/openai.cpp:167),
[unbound owned dispatch](../../src/openai.cpp:269).

Existing machinery already exposes a non-secret binding
(provider/account/revision/kind/base/protocol) and can compare an expected binding
inside account resolution; direct native colleagues use it. The design should
choose a binding lifetime for one assembled request group and reject a changed
binding before sending unsupported cache fields. Token renewal must retain its
normal account semantics rather than being accidentally classified as an account
switch. The exact comparison should distinguish those cases deliberately.
[Binding](../../src/provider_auth.cpp:785),
[expected-binding check](../../src/provider_auth.cpp:1381),
[colleague binding](../../src/colleague.cpp:248),
[colleague dispatch](../../src/colleague.cpp:476).

The usage reader must still use each attempt's actual recorded auth/transport
identity, including historical attempts where no such pin existed. A group ID or
model name cannot supply a missing account identity. A direct account-switch
fixture between preparation and dispatch answers this fault class; no live account
migration is required.

## N2 — The ephemeral state message still needs an actual behavioral contract

**Priority: medium; explicit unresolved design decision.** The synthesis correctly
calls for role/order/update/override semantics before implementation
([proposal](SYNTHESIS.md)), but those semantics are not yet selected. Moving
state out of `instructions` changes instruction priority and recency. An appended
ordinary assistant message, a user message and a developer message have different
behavior; no byte-stability argument chooses among them.

The implementation needs one concrete rule for:

- message role and placement relative to the complete actual overridden input;
- whether task/budget guidance remains present when Lua overrides `instructions`;
- whether Lua can suppress/replace the assembly addition, and how;
- empty input, repeated requests, all tasks completed, a disabled/below-trigger
  budget and pending changes;
- preservation of complete tool groups and opaque reasoning order.

Today all Lua options merge first, then task/budget text is unconditionally added
when its native condition holds. Changing that precedence silently would change
model agency and operational behavior. `validate_protocol` only matches call IDs;
it does **not** reject a non-tool message inserted between a call and its result.
The new placement rule therefore needs its own direct ordering assertion rather
than assuming the existing validator enforces it.
[Merge/addition order](../../src/coding.cpp:4129),
[protocol validator](../../src/coding.cpp:285).

Keep the addition request-local. Persisting a fresh developer state message into
normal context each step would accumulate stale mandatory instructions: managed
compaction protects every live developer item.
[Mandatory entries](../../src/context.cpp:1343).

## N3 — Define the budget's counted input before adding the budget message

**Priority: medium; concrete scope decision.** The mutable state proposal moves
`CONTEXT_BUDGET` from instructions into `input`. Current budget triggering counts
the selected request input packet before the report is inserted into instructions
([current calculation](../../src/coding.cpp:4165)). Counting the final input after
inserting a report containing its own `input_bytes` value creates a self-dependent
representation, and changing inclusion/exclusion silently changes the advisory
trigger's meaning.

Choose and label the counted span explicitly. The existing basis is the actual
selected input, including a Lua `input` override, before ephemeral additions;
retaining that basis avoids self-inclusion. If a separate final-input or wire-byte
observation is desired, derive it separately and name it accordingly. None of
these sizes is a provider token count. The direct boundary case is a selected
input below the trigger whose injected state would itself take the final packet
above it; the result must follow the selected definition without recursive
assembly or trigger oscillation.
[Budget units](../../src/coding.cpp:2395),
[request override](../../src/coding.cpp:4129).

## N4 — Deduplication needs source lineage and counter scope, not response text or IDs

**Priority: medium; truthful accounting decision.** The reader design excludes
duplicate stream/wrapper copies and retains unknown consumption
([reader](SYNTHESIS.md)). Implementation still needs an explicit contribution
rule for overlapping sources:

- One response can occur as received SSE bytes, terminal SSE response, upstream
  packet, converted result and colleague wrapper. Count one contribution using
  source/attempt linkage; do not deduplicate identical bytes across distinct
  attempts or simply sum every usage-bearing object.
- Claude `usage` and per-model `modelUsage` are both preserved. They are not
  automatically additive. Keep their native reported scopes and mark a model
  split unavailable/ambiguous when its relationship to the aggregate is unknown.
- A complete response's counters may be followed by native context rejection;
  they remain observed consumption. A failed/incomplete SSE response may contain
  usage even though the current `completed_response` helper throws. The tertiary
  reader must parse the original event rather than call a success-only decoder.

[Original/link fields](../../src/provider_original.cpp:431),
[Claude metadata](../../src/colleague.cpp:96),
[usage before acceptance](../../src/coding.cpp:4480),
[success decoder rejection](../../src/openai.cpp:495).

Also distinguish "locally refused before provider dispatch" from "dispatched but
usage unavailable". An admitted operation or captured outbound body does not prove
that a provider received an inference: the body is captured before child startup,
and auth can fail before HTTP. Do not manufacture a monetary zero from absence,
but do not label all local refusals as unknown remote consumption either. Preserve
the acquired stage/disposition and known subtotal separately.
[Body-before-spawn](../../src/openai.cpp:306),
[auth-before-body](../../src/provider_auth.cpp:1370).

## N5 — Ephemeral inputs need request provenance distinct from a context revision

**Priority: medium; recovery/source-identity decision.** The design preserves context
revisions and audits ephemeral additions ([design](SYNTHESIS.md)). A context
head already does not uniquely identify actual input because Lua can override
`input` and `instructions`; the proposed message, cache policy and boundary markers
add further assembly state. Task changes can alter a request even without changing
that context head. Do not use context revision alone as a request identity, cache
eligibility identity or recovery source.

The existing final native request original and actual external body are the right
sources. Any context view shown to the model should clearly distinguish retained
items from request-local additions; otherwise a model may try to inspect/restore
an ephemeral message as a nonexistent context entry. After restart, derive fresh
state from retained task/budget policy and the effective program; do not replay the
last request-local message as durable conversation history. Historical request
drill-down should still show the exact old message and markers.
[Final request capture](../../src/coding.cpp:4191),
[external capture](../../src/raw_provider.cpp:320),
[retained item projection](../../src/context.cpp:752),
[restore by original ID](../../src/context.cpp:1043).

For a retry of the same assembled request, freeze the additions with that request;
rebuilding task/budget state during the retry would break the unchanged submitted
request claim in unit 1. A later new request may use new state. Existing assembly
outside the retry loop already provides that separation.
[Assembly/group boundary](../../src/coding.cpp:4191),
[retry reuse](../../src/coding.cpp:4284).

## N6 — Put the route correction at all actual caller boundaries

**Priority: low; implementation seam clarification.** The proposed conversion owner
is sound, but there are two owned OpenAI entry paths. Main requests go through
`openai_http`, while owned colleagues call `ProviderAuth::request` directly. A fix
placed only above the owned/compatibility branch in `openai_http` covers main and
compatibility colleague calls but not the direct owned-colleague path. Current
colleagues use a tool-free, already external-shaped input, so this is not an
additional observed failure. Keep generic auth transport protocol-neutral and
make callers' shape/ownership explicit rather than installing conversion in every
JSON serializer.
[Main owned branch](../../src/openai.cpp:269),
[direct colleague branch](../../src/colleague.cpp:468),
[colleague input](../../src/colleague.cpp:250),
[boundary conversion](../../src/json.cpp:452).

For 429 correction, match only the provider-rate-limit condition representing the
actual retryable HTTP response. Owned auth also raises `provider_rate_limit` with
negative sentinel details for refresh backoff. Broadly retrying the enum would
expand behavior beyond the equivalent HTTP 429 promised by the design.
[HTTP mapping](../../src/provider_auth.cpp:422),
[refresh backoff](../../src/provider_auth.cpp:1425),
[current classifier](../../src/coding.cpp:4402).
