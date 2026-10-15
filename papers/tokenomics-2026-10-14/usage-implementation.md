# Tertiary token usage reader implementation

2026-10-14; arconaut-wiq. Operator authorizes implementation after the research
and design review. This lane owns a read-only native consumer and its direct
recorded-source fixture. Root owns CMake registration, integration, independent
code review and publication. No provider calls or new dependencies.

## Concrete unit and scope

Read session raw-run discovery and pinned preserved prefixes through existing
RawRunReader. Decode existing provider phases, source links, HTTP acquisition,
child stdout, original packets and decision-model calls. Correlate using actual
run/lane/ordinal and retained attempt/invocation/generation IDs when emitted.
Never infer identity from model text or the most recent request. Retain locators
for source drill-down and disposition/dispatch observations.

This first consumer covers preserved raw prefixes only. RetainedState::open is
a writer/recovery owner and will not be used for inspection. A session lacking
raw runs has unavailable coverage, not zero consumption. Semantic-only old
sessions, unrecorded bodies, unmatched parents, unsupported ABIs and unreadable
runs remain explicit gaps. This does not close audit completeness or claim a
complete historical bill. No second audit/persistence format is introduced.

Use native observation/counter structs. JSON parsing occurs only over original
external provider bodies, through the existing boundary parser. Support Responses
JSON/SSE (including failed/incomplete terminal responses), Chat usage, Anthropic
ordinary/cache counters, Claude aggregate and per-model alternative scopes, and
Jev ordinary usage. Preserve reported OpenRouter costs separately. Deduplicate
transport/decoded/wrapper copies by actual attempt/source ancestry and scope;
never collapse equal text from separate attempts. Conflicting counter reports
remain conflicts rather than silently choosing a convenient total.

Apply finite record/body/observation limits and existing raw reader segment/origin
bounds. Report the scanned prefix, unfinished runs and stopped scans. Count known
subtotals per supported units/scope plus missing/invalid coverage. Missing cache
fields remain absent. Reasoning is a component of reported output where the
protocol defines it and is never added again. Optional price estimates require
explicit model/provider matching, a dated rate input and counter semantics;
observed costs are never overwritten by the estimate.

## Direct oracle and resource use

Write an independently encoded on-disk RawAudit fixture with literal external
responses and manually specified native record layouts before implementation.
The reader's first executable fixture must fail on its stub. Expected values
come from the literals, not production encoders or parser output. Cover retry
identity, duplicate captures, failed/incomplete usage, malformed/missing bodies,
unlinked records, CLI alternate scopes, Jev child output, OpenRouter cost,
reasoning overlap, bounded refusal and unchanged source files. Check scanner
source locators against fixture positions. Root coordinates one build owner;
this lane never races Ninja. No live/eval/performance campaign is selected.

After green, resolve concrete findings and use root's one independent code review
and its fixes. Do not add a third assurance layer. Record actual results and
remaining limitations here before handoff.

## Implemented surface and observation limits

The owned executable is `blackbird-token-usage SESSION`. It accepts
`--max-records N`, `--max-bytes N`, and optional explicit prices:

```sh
blackbird-token-usage SESSION --price 2026-10-14 openrouter vendor/model - 2 4 0.5 3
```

Price arguments are date, selected provider, **observed actual model**, service
tier (`-` for absent), and USD per million ordinary input, output, cache read and
cache write tokens (`-` for an absent cache rate). There is no built-in price
table or subscription/quota conversion. The input must match the observed
provider/model/tier. Missing counters, impossible overlaps, partial/ambiguous
usage or TTL-specific writes without an applicable rate leave the estimate
unavailable. Estimates never replace reported charges. The API exposes the same
typed observations, source locators, coverage and optional estimates.

Reports separate main/colleague/decision observations, aggregate and per-model
alternative scopes, original numeric fields, native terminal branch/stage,
external response status, observed dispatch stages, known subtotals and missing,
partial or invalid coverage. Actual retained attempt/invocation/generation and
retry-group identities are read from their emitted records. Non-secret account
labels require an actual raw auth/application link and acquired labels. Missing
identity stays unknown. A captured outbound body does not prove remote receipt;
an explicit wrapper `not_dispatched` remains a distinct local refusal.

Raw1322 codec-adopt roles1–5 contain **BBM2 native packets**, as shown by
`colleague.cpp:capture_packet` and `provider_encode_original`; they use the
existing packet decoder. Role11 instead preserves original external JSON in
`colleague_main.cpp` before parsing; codec ownership does not determine its
format. That standalone source remains unlinked when the producer emits no
causal edge to a later invocation. Raw1312 acquired extents and raw1404 positive stdout
reads carry external bytes, which use the existing external JSON parser and
owned SSE framing. No format guessing or internal JSON persistence is added.
Raw data/control partitions may record a child's positive stdout before its
earlier-ordinal begin record; deferred graph resolution handles this ordering.

Limits bound directory entries, selected decoded records/bytes, retained
candidate body bytes, per-body size, causal depth and observation count.
Underlying RawRunReader still validates pinned audit prefixes, so `--max-bytes`
is **not a total filesystem-I/O cap**. Candidate stdout that arrives before its
control graph may temporarily count against the body limit before it can be
classified. Limits and corrupt/unsupported runs produce explicit coverage gaps.
Each source locator carries a restart cursor, actual lane/ordinal and byte range;
RawRunReader resumes that cursor for exact drill-down. Prefixes are pinned per
opened run, not as an atomic snapshot across all runs of a live session.

Only preserved raw runs are consumed. Semantic-only sessions, expired/absent raw
bodies and audit-completeness gaps are not reconstructed through a writer or
guessed from a successful final response. `complete_session_ledger` remains
false. No source is modified, recovered, compacted or deleted by this reader.

## Execution record

Root captured the genuine stub red: the direct fixture aborted at its expected
observation-count assertion (context/tokenomics-usage-red.log). Compiler findings
then corrected optional source construction and checked optional borrows; no
diagnostic suppression was added. A separate fixture error declared two raw ABI
enum widths as1 instead of their actual4-byte declarations; the reader properly
rejected it, and the independently literal fixture was corrected.

Source rereading exposed the BBM2 boundary before acceptance. The fixture now
owns a small BBM2 writer using literal tags/varints and external literal
specifications, without calling the production packet encoder; expected counts
are independently written assertions. It also records Jev stdout data before
the child's earlier-ordinal control begin, checks an exact original slice by
resuming its actual source cursor, and checks unchanged source files. Root's
current-source Debug batch passes `token_usage_test` in0.61seconds; the corrected
implementation, CLI and fixture also pass targeted clang-tidy. Root's one
independent [code/oracle review](usage-code-review.md) then identifies U1–U4.

The single remediation set separates reported numeric charge, USD qualification,
malformed monetary coverage and token validity. Generic charges acquire USD only
under the observed OpenRouter route; explicit CLI `total_cost_usd` retains its
declared unit. Counter subtotals remain useful when charge metadata is malformed.
It also checks conflicting terminal counter/identity reports within a single SSE
body, explicit service-tier disagreement, cumulative-update versus final-usage
scope, callback1310→1311 status linkage, and actual role11 JSON. A terminal event
without final counters cannot by itself promote partial counters to complete.

Added direct literals cover those counterexamples and identical duplicate
terminals. An actual RawRuntime/provider_http fixture records one injected503
callback and a second callback throwing after preparing an unresolved extent;
this exercises actual production capture layouts without credentials, a live
provider or an inference call. The source formatter and targeted clang-tidy pass
after this remediation. Corrected direct green/recheck results follow below;
there is no additional independent review or third certification layer.

The finding-driven recheck initially reproduced U2: parsed SSE terminal
conflicts were detected inside the body but their flag was not propagated into
the final attempt observation. The correction preserves that flag during the
final merge. A separate literal now reports `provider: openrouter` without an
actual OpenRouter request route; this verifies that an upstream label cannot
qualify a generic charge as USD. After these narrow corrections, the Debug build
of `token_usage_test`, `blackbird-token-usage`, `blackbird`, `openrouter_test` and
`request_cache_test` succeeds. The direct `token_usage_test` passes with exit0
in0.38seconds, including the production callback fixture and unchanged-source
checks. The build lease is released to root for integrated qualification.
Final targeted clang-tidy of the reader, CLI and fixture passes with exit0 and
no diagnostics; the owned-file whitespace check also passes. These results
qualify the stated raw-source scope, not a complete session ledger or absent
provider charges. Root owns broader profile and executable integration results.
