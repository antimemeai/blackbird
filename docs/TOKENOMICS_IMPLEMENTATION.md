# Tokenomics and OpenRouter implementation

2026-10-14. Operator approves the researched sequence and asks for full Blackbird
discipline. This executes the [reviewed research design](../papers/tokenomics-2026-10-14/SYNTHESIS.md)
on `work/tokenomics-openrouter-2026-10-14`, descended from pushed `ad4a640`.
No new library or SDK. C++20/Lua, existing curl/auth, native values and BBM2.

## Purpose and owners

Deliver usable OpenRouter main-loop compatibility, stable model-controlled
request layout/cache policy and honest inspection of consumed tokens. Preserve
originals, exact request/effect linkage, model context repair and cancellation.
Public API estimates cannot stand in for subscription consumption. No claim of
measured savings or general coding quality follows from mock transport tests.

Three independent owners work in the shared checkout. Native-route lane owns
OpenAI conversion, request-group lifecycle/account binding, HTTP429 classification
and direct route tests (`arconaut-1ud`). Usage lane owns new native tertiary reader,
CLI and independent recorded-data cases (`arconaut-wiq`). Root owns OpenRouter
adapter, persisted provider selection, common cache/layout assembly, integration,
docs, beads and publication (`arconaut-nan`, `arconaut-2aw`). Provider lane
grounds the additional OpenRouter contract and reviews this concrete plan once.
No simultaneous Ninja writers; root grants direct build windows and later runs
the integrated source gate. Root alone modifies shared build registrations.

## Execution units

1. **Shared route foundation.** Begin/end one request group around preparation,
   all attempts and acceptance, with cleanup on throw and nested-group refusal.
   Pin non-secret route/account binding once; token renewal for the same binding
   remains allowed. Main owned OpenAI must apply native-to-external conversion
   exactly once. Only actual typed HTTP429 joins the existing retry policy;
   refresh-backoff sentinels, auth, malformed responses and cancellation do not.
   Keep generic auth protocol-neutral. Direct old-source reds: actual owned wire
   type mismatch, 429 attempt count; then binding switch/refresh/cancel cases.

2. **Tertiary usage foundation.** Read pinned raw prefixes without opening the
   semantic writer/recovery owner. Discover session raw runs, bound scans, retain
   source/attempt/model/route identity and expose missing, unsupported, corrupt
   or unlinked coverage. Parse original success/failure/incomplete observations,
   deduplicate copies by source lineage and scope, preserve unknown consumption.
   Include cache writes/reads, reasoning without output double count, colleague/
   helper units and reported OpenRouter charges separately from any dated
   estimates. CLI is read-only, with original locators; document raw-only coverage.
   Direct independent fixtures assert exact categories/subtotals, duplicate and
   unknown behavior, malformed/overflow refusal and unchanged source bytes.

3. **OpenRouter main adapter.** Add an explicit `--provider openrouter` choice,
   persisted in existing session settings/restart/new-session paths; legacy
   settings default to OpenAI. Require an explicit namespaced model when switching
   providers; do not silently select a model or fall back to another billing route.
   Keep existing OpenRouter Chat colleague descriptor intact. New main adapter uses
   public `/responses`, existing owned auth or explicitly configured OpenRouter
   key, request-group binding and cancellation. Its API remains stateless with
   `store:false` and full needed input. Convert typed tool calls/results only at
   the external boundary, preserve opaque reasoning and validate history item
   requirements. Capture exact submitted/received representations and trailing
   routing metadata. Require `provider.require_parameters:true` for tool/control
   semantics; do not silently accept routing that ignores them. Opt into routing
   metadata and send `X-OpenRouter-Cache:false` for main coding, keeping full
   response replay distinct from prefix caching. Preserve assistant id/status/
   phase and whole reasoning items (format/signature/encrypted content included),
   plus namespaced/custom tool structures or an explicit unsupported result.
   Distinguish terminal provider/auth failure from actual
   retryable transport/rate failures; never accept/dispatch partial streamed tools.
   Inject deterministic HTTP transport, then actual native-launcher fake-curl
   continuation; assert effect count, model/provider selection after reopen,
   failure/cancel behavior, final-stream metadata and raw source retention.
   Cover failed/incomplete/response.error/plain error events and nested canonical
   `error_type`, including authentication represented as `server_error`;
   malformed/truncated/conflicting terminal streams cannot advance tools.

4. **Cache policy and layout on both routes.** Add a typed three-way policy:
   provider default, reusable-prefix explicit caching, and caching disabled.
   Retain defaults in `program_config`, with request-local Lua overrides. The
   local setting never leaks to the external protocol. Unknown/unsupported
   explicit capability fails before dispatch; no paid prewarm/keepalive, model
   substitution or backend-residency claim. Initially qualify explicit upstream
   controls for exact documented GPT-6.1 Sol public-key/OpenRouter model routes;
   existing subscription/compatibility paths keep provider defaults unless their
   actual controls are separately established. OpenRouter `session_id` and
   optional provider-routing choices remain explicit request controls.

   Keep stable instructions/tools stable; append one ephemeral developer-state
   item after complete selected input, respecting call groups/opaque reasoning.
   `state_context=auto|omit` is a local Lua choice, default auto. Task state follows
   the current revision/nonempty rule, budget triggers on selected input before
   state insertion. Never accumulate state messages in retained context; freeze
   per retry, derive fresh after restart. The final native request retains local
   policy plus the reusable-item boundary. The external encoder consumes it after
   native tool value conversion and places a marker at the latest eligible text
   endpoint before changing state. This keeps JSON formatting at the boundary;
   no local policy field leaks upstream. `stable_prefix` keeps automatic caching
   and adds that explicit boundary; `disabled` uses explicit-only with no markers.
   Preserve model-authored markers under provider-default policy; reject conflicts
   with an explicitly selected native policy. Structured provider content remains
   a typed content array, while ordinary native results encode at the boundary.

   Direct emitted-request cases assert exact role/order/freshness, tool/reasoning
   linkage, Lua overrides, all-tasks-complete, disabled budget, trigger boundary,
   restart, unsupported controls and prefix-marker selection. Compare exact
   unchanged instruction/tool fragments; do not call JSON byte prefixes actual
   tokenizer cache hits. Provider-side normalization can affect prefix geometry.

## Review, qualification and completion

Existing research/design review is input. Provider lane challenges this execution
plan and exact contract before relevant product coding. Each owner writes its
small subplan and direct failing oracle before implementation. One independent
code/oracle review per conceptual unit, fix findings, then one finding-driven
recheck/fixes. Never a third assurance layer, review of reviews, invented campaign
clock or mutation run on this laptop. Keep original acquisition/source evidence.

Run relevant direct Debug cases as units settle; integrated source commit runs
the installed full Debug/C++/Lua gate once. Qualify changed native paths in local
optimized/sanitizer and supported Neuroses Linux profiles with affected checks.
Record actual results and any host/profile limitations; no repeat of settled
unrelated source campaigns. No live provider comparison/eval campaign is needed
to prove deterministic protocol/ownership behavior. If an existing configured
account permits a useful bounded live continuation, record exact actual scope,
charges/unknowns and outcome; absence of credentials remains a live-qualification
limit, not permission to invent success or reroute billing.

Update using docs and journal, close only completed implementation issues, retain
remaining work, run supported `bd backup`, commit through the source hook and push
the work branch. Main integration is not inferred. Operator art, existing worktrees,
stashes, running executables and other services remain under their current owners.
Do not activate this build into an unrelated running session by implication.

Recoverable observation views and advanced Lua composition can use the existing
APIs; broader demand-graph scheduling/result reuse remains the separate concurrent/
durable workflow unit. No silent transcript truncation or semantic answer cache.

## Delivery record

Source and oracle reviews are complete with accepted findings integrated.
[Using the delivered interfaces](TOKENOMICS.md) and the
[qualification record](../papers/tokenomics-2026-10-14/qualification.md) describe
actual results and limits. Each conceptual unit received one independent code
review and finding-driven corrections. No further review layer or evaluator
campaign is introduced. Live OpenRouter continuation/cache observation is
`arconaut-438`; branch integration and running-executable activation remain
separate operator work.
