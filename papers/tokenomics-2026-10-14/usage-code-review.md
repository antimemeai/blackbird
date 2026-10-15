# Independent usage reader code and oracle review

2026-10-14. One bounded review of `token_usage.hpp`, `token_usage.cpp`,
`token_usage_main.cpp` and `token_usage_test.cpp` after the owner announced the
BBM2 correction stable. Compared source directly with current provider,
colleague, decision-model, operation-original and raw-reader producers. No builds,
product edits, provider calls, issue changes or delegated reviews. Examples below
are synthetic source-derived counterexamples, not executed sessions.

## U1 — Generic cost fields do not establish USD, and cost validity must not erase tokens

**High: monetary truth and counter independence.**
[Cost parsing](../../src/token_usage.cpp:174) accepts any nonnegative numeric
`cost`/`usage.cost` into `observed_cost_usd` before the observation is associated
with the actual route. A custom provider returning
`{"usage":{"input_tokens":10,"output_tokens":2,"cost":7}}` therefore reports
`observed_cost_usd=7` even when the provider's unit is credits or unknown. The
route is resolved later in [request metadata](../../src/token_usage.cpp:750) and
[merge](../../src/token_usage.cpp:876); the generic field name is not a currency
contract. The owner independently identified this same candidate during review.

Qualify generic cost through the actual supported contract, initially OpenRouter;
keep explicit CLI `total_cost_usd` separately recognizable. Preserve other raw
numeric fields/source locators without assigning USD. Do not infer that the
reported upstream provider shares the account-charge contract of the routing
provider.

There is a second consequence in the same function: an invalid or unsupported
cost sets `observation.malformed`. [Subtotals](../../src/token_usage.cpp:1132) then
exclude otherwise valid token counts. Missing/invalid monetary coverage and
invalid token counts need separate state. A malformed `cost` cannot invalidate
an independently well-formed `input_tokens=10` and `output_tokens=2` observation.

**Direct oracle:** use the same literal token counters under (a) a known OpenRouter
route with supported numeric account charge, (b) an unknown/custom route with the
same generic numeric field, (c) a malformed charge, and (d) a Claude result with
explicit `total_cost_usd`. Assert USD only for the supported contracts, original
fields retained, and unchanged known token subtotals in the unpriced/malformed-cost
cases. Existing [cost case](../../tests/token_usage_test.cpp:317) covers the known
OpenRouter shape only.

## U2 — Conflicting terminal reports inside one stream are silently overwritten

**High: false known subtotals and price eligibility.**
[Response parsing](../../src/token_usage.cpp:227) reuses one aggregate scope and
assigns each subsequent event's identity/status/usage directly into it. Counter
assignment [overwrites](../../src/token_usage.cpp:164) a previous value. The
[conflict-aware merge](../../src/token_usage.cpp:876) runs only after the whole
body has been parsed, so it cannot detect contradictory terminal events inside
that body.

For one SSE body containing `response.completed` with input100/output10, followed
by another terminal response with the same ID/model but input7/output2, the final
candidate is input7/output2 with `conflicting=false`. Both numeric field versions
may survive in `reported_fields`, but the known subtotal and optional estimate use
the last counters. A failed/incomplete/native-rejected stream can be exactly the
case where only these raw originals survive; the tertiary reader cannot rely on
the main success decoder to have rejected contradictory terminals.

Across separate bodies, [merge](../../src/token_usage.cpp:887) also keeps the first
nonempty `service_tier` without flagging an explicitly contradictory later tier.
That can leave a price estimate eligible at one tier despite contradictory source
metadata. Keep tier absence distinct from tier conflict.

Choose recognized streaming-update semantics separately from terminal consistency:
Anthropic cumulative deltas can legitimately update counters, while contradictory
terminal scopes or IDs cannot silently replace one another. A terminal event
without counters must also not, by itself, promote earlier partial usage to a
complete terminal usage scope; [the current flag](../../src/token_usage.cpp:831)
tracks terminal event presence rather than terminal counter completeness.

**Direct oracle:** include conflicting terminal counters and IDs within one raw
SSE body; an identical duplicate terminal; a partial start/delta followed by a
terminal event with no final usage; and ordinary supported cumulative deltas.
Assert explicit conflict/partial state, no confident estimate from contradictory
or incomplete usage, and correct supported cumulative totals. Add a two-capture
service-tier disagreement case. The [existing conflict test](../../tests/token_usage_test.cpp:461)
only disagrees across two separate BBM2 bodies.

## U3 — Callback response evidence is attached to an unresolved acquisition node

**Medium: inaccurate dispatch-stage reporting on the actual injected path.**
The [producer](../../src/provider_original.cpp:138) emits site1310 as
`callback-invoke -> application-request`, then site1311 points to that invoke.
[ProviderAuth's callback transport](../../src/provider_auth.cpp:468) uses this
path and does not emit the native HTTP-parser site1302.

The [reader](../../src/token_usage.cpp:421) handles1311 by setting `response` on
its referenced node, but it never defines/links1310. Consequently that placeholder
cannot resolve to the HTTP request or retained attempt. A complete acquired1312
response can have real usage while the observation's dispatch remains
`outbound_captured` rather than `response_observed`.

Resolve the callback acquisition chain or use the acquired response fact with its
actual owner; do not infer remote completion or a zero charge from either stage.
This is a stage/provenance correction, not a request to treat injected HTTP as a
live-provider qualification.

**Direct oracle:** encode actual1300/1310/1312/1311 records, with1311 referencing
1310 rather than1300. Put data before control as the recorder permits. Assert one
observation, the original source range, `response_observed`, and retained terminal
failure/counter semantics. Also cover unknown-used extent and a callback that
throws before returning status. The [current acquired helper](../../tests/token_usage_test.cpp:283)
writes1312 alone, so it cannot expose the missing1310 edge.

## U4 — Standalone request role11 contains original external JSON

**Low: concrete format/coverage correction.**
The new [1322 classification](../../src/token_usage.cpp:469) marks every accepted
role, including11, as a native packet. The implementation report likewise says
roles1–5/11 are BBM2. But [the actual standalone producer](../../src/colleague_main.cpp:40)
adopts the original file bytes under `standalone_request`, then calls
`parse_json` over those bytes. `codec_adopt` describes ownership, not proof of a
BBM2 format. This source role can contain external JSON, including malformed
external bytes retained before parsing.

Classify it from the real producer contract. Preserve an explicitly unlinked
standalone source as a gap when no actual causal edge connects it to a later
request; do not attach it to the nearest call just to obtain metadata. The current
format bug may be masked by that existing unlinked topology rather than appearing
as a malformed-request error.

**Direct oracle:** add a literal external JSON role11 original and a malformed
original, using the actual standalone phase topology. Assert truthful format and
source/coverage reporting, without fabricating a causal association. The fixture's
[packet helper](../../tests/token_usage_test.cpp:253) currently BBM2-encodes every
role it is asked to write, which would conceal this distinction.

## Scope and mechanisms checked without another review gate

Native roles1–5 use BBM2 in the inspected `capture_packet`/operation-original
paths;1312 and positive1404 reads carry acquired external bytes. The independent
fixture's literal tag/varint writer avoids using the production BBM2 encoder as
its own oracle. Its current bodies, duplicate captures, separate attempts,
failed/incomplete usage, Claude aggregate/model-alternative scopes, Jev stdout,
unknown fields, source drill-down and unchanged-file assertions directly attack
useful fault classes; U1–U4 identify the missing cases above.

The reader uses read-only native directory/raw-reader paths, not the semantic
writer or recovery owner. [Native directory open](../../src/journal_storage.cpp:208)
uses read-only flags; [pinned raw snapshots](../../src/raw_reader.cpp:84) bound the
preserved prefix. The documented limits correctly distinguish selected record/body
budgets from total filesystem I/O: RawRunReader validates pinned prefixes on open
and segment load. The report does not promise an atomic snapshot across all runs
or a complete semantic session ledger. These limits must remain visible; no new
I/O campaign or wider storage review is requested by this report.

The source graph preserves lane/ordinal and source cursors, and the fixture now
covers a child stdout record preceding its control begin. Counter arithmetic and
price calculation remain in the tertiary consumer. The observed charge, API list
estimate, reasoning component and raw unknown coverage are separate fields. Fix
the concrete findings above and run their direct cases within the existing
implementation/recheck bound; this review does not call for rereview or another
certification layer.
