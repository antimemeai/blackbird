# OpenRouter main-loop adaptation contract

2026-10-14. Official public documentation and embedded OpenAPI read for the
approved implementation. No credentials, account calls, inference, SDK/library
adoption or production changes. This note defines adapter work and mock oracles;
it does not qualify a model/provider/account combination.

## Request and model identity

OpenRouter Responses is stateless at
`https://openrouter.ai/api/v1/responses`, using an OpenRouter bearer key.
`store:true` and non-null `previous_response_id` are rejected. Send the needed
history in each request, retaining `store:false`.
[Responses overview](https://openrouter.ai/docs/api/reference/responses/overview).
Assistant history messages require `id` and `status` according to the usage
guide; preserve returned values rather than rebuilding an assistant message from
its displayed text.
[Basic usage](https://openrouter.ai/docs/api/reference/responses/basic-usage).

Keep the selected routing provider (`openrouter`), requested model slug, actual
response `model`, and actual upstream provider separate. A runtime-selected slug
must pass unchanged into the request. Model metadata can expose `id`,
`canonical_slug`, context length, output limits, pricing, defaults and
`supported_parameters`; aliases resolve, so catalog identity is not the requested
string or a completed-inference observation.
[Model metadata](https://openrouter.ai/docs/api/api-reference/models/get-model).

`provider.require_parameters` defaults false: unsupported parameters can otherwise
be ignored. Tools receive a supporting-provider preference, but if no provider
supports a parameter it can still be ignored. `require_parameters:true` excludes
unsupported providers; `allow_fallbacks` defaults true.
[Provider selection](https://openrouter.ai/docs/guides/routing/provider-selection).

**Native recommendation:** define required tool/reasoning semantics explicitly,
retain the chosen routing controls with the request, and never claim capability
from a model name alone. An operator-selected fallback is a routing policy, not
permission for Blackbird to change the requested model or silently drop tools.

## Tools and opaque continuation

Responses function tools use flat `type`, `name`, `description`, `parameters`,
`strict` fields, rather than Chat's nested `function` object. Calls carry JSON
text in `arguments` and pair with `function_call_output` through `call_id`.
Output accepts a string or typed input-text/image/file array; its `id` is
optional. Parallel tool calls remain multiple independently identified items.
[Tool calling](https://openrouter.ai/docs/api/reference/responses/tool-calling).

The current create schema additionally includes namespace/custom tools,
`additional_tools`, function-call `namespace`, assistant `phase`, and reasoning
`content`, `summary`, `encrypted_content`, `format`, `signature`, `id` and
`status`. It accepts `include:["reasoning.encrypted_content"]`. Preserve phases
when returned. The schema's summary entries are typed `summary_text` objects,
although the reasoning tutorial contains a string-array example. Treat the
schema as the structural reference and retain original items.
[Create schema](https://openrouter.ai/docs/api/api-reference/responses/create-a-response),
[reasoning example](https://openrouter.ai/docs/api/reference/responses/reasoning).

The general reasoning guide uses `choices[].message.reasoning_details` and
`choices[].delta.reasoning_details` for Chat. It says consecutive reasoning
blocks must retain their complete original ordering/content during tool
continuations. That is not an instruction to turn a Responses `reasoning` item
into Chat history.
[Reasoning preservation](https://openrouter.ai/docs/guides/best-practices/reasoning-tokens#preserving-reasoning).

**Native recommendation:** share owned native-to-Responses conversion with the
correct route boundary, including JSON encoding native argument/result values.
Do not stringify then reparse internal state. Preserve complete returned
reasoning items and provider extensions through tool results, reopen and context
repair; display summaries separately. Do not fabricate, flatten, reorder or
re-encode encrypted payload contents. Dispatch only client function/custom tool
forms deliberately supported by Blackbird; an unfamiliar output type must remain
retained and cannot silently become a shell command or disappear from continuation.

An API union admitting namespace, async or server-tool objects does not prove
every selected downstream endpoint implements them. Retain required capability
checks; do not enable hosted execution/deferred-tool insertion merely because the
schema contains it.

## Stream acceptance and errors

The create schema distinguishes `response.completed`, `response.failed` and
`response.incomplete` and carries a response status independently of item status.
Function argument delta/done events are distinct from completed response output.
[Create schema](https://openrouter.ai/docs/api/api-reference/responses/create-a-response).

Responses native `error.code` is lossy: authentication, overload and timeout can
all appear as `server_error`. Canonical `error_type` is on the response object,
including inside a `response.failed` wrapper. Error shapes also include
`response.error` and plain `error` events. Do not classify retries by native
`server_error` alone.
[Responses errors](https://openrouter.ai/docs/api/reference/responses/error-handling),
[wire error variants](https://openrouter.ai/docs/api/reference/errors-and-debugging#skin-specific-error-formats).

HTTP 200 can precede all output and later carry failure. Router fallback may
already have run before a failure reaches Blackbird. Some length errors are
converted into length-limited completions. `Retry-After` can accompany 429/503;
402 is a transient wait case only for the documented in-flight-budget condition,
not ordinary insufficient credits.
[Error handling](https://openrouter.ai/docs/api/reference/errors-and-debugging).

**Native recommendation:** success requires a valid completed response, not EOF,
`[DONE]`, nonempty text or a finished argument item. Retain partial originals and
reported usage on failure/incomplete/cancel. Parse SSE incrementally across byte
boundaries, multiline data and comments. A malformed payload, inconsistent
terminal status, truncated stream or conflicting duplicate terminal event must
not produce an accepted continuation or execute a partially assembled call.
Unknown nonterminal event kinds may be retained without being dispatched. Exact
terminal/metadata disposition needs fixtures; tutorial snippets that skip invalid
JSON are not an acceptable production parser specification.

Retry only under the existing bounded cancellation/deadline policy after typed
classification. Preserve each external attempt and do not rerun settled local
tools. Missing classification or usage remains unknown, not a fabricated success
or zero-cost failure.

## Usage, routing metadata and cache policy

Responses usage includes total input/output, input cached/write subsets,
reasoning output details, optional `cost`, `cost_details` and `is_byok`; usage
itself may be null.
[Create schema](https://openrouter.ai/docs/api/api-reference/responses/create-a-response).
OpenRouter's usage guide calls `cost` the account charge and separately exposes
upstream inference cost. Usage is automatic; `usage.include` and
`stream_options.include_usage` are deprecated. The guide's example is Chat-shaped,
so read Responses field names from its schema. Upstream cost may be null/zero
outside BYOK; it is not an additional charge to add to `cost`.
[Usage accounting](https://openrouter.ai/docs/cookbook/administration/usage-accounting).

**Native recommendation:** preserve exact numeric values and source locations;
derive arithmetic only in the tertiary reader. Keep account-reported charge,
upstream breakdown, list-price estimate, cached/write tokens and reasoning
subsets distinct. Do not reconstruct an invoice from only direct OpenAI prices.

Opt-in `X-OpenRouter-Metadata: enabled` surfaces `openrouter_metadata`, including
requested identity, routing strategy, candidate/selected provider/model,
attempts and pipeline stages. Stream metadata is documented on the final chunk
before `[DONE]`; do not stop capturing as soon as `response.completed` arrives.
Metadata can be absent on early failures, masked 500s and response-cache hits;
unknown additive fields/stages must be tolerated. Reported pipeline stages can
expose transformations performed beyond Blackbird's submitted boundary.
[Router metadata](https://openrouter.ai/docs/guides/features/router-metadata).

OpenRouter `session_id` controls routing affinity: body beats `x-session-id`, then
`prompt_cache_key` is a fallback. IDs have a 256-character limit; explicit IDs
activate affinity after a successful request, while default affinity waits for a
cache hit. Affinity expires after ten idle minutes, can fall back, and manual
`provider.order` takes precedence. This is not backend KV residency or a cache-hit
guarantee. Current OpenAI explicit cache controls are supported on Responses text
blocks for GPT-5.6+; OpenRouter translates block markers across some providers,
but does not translate TTLs. The page's generic OpenAI read-price examples lag
model-specific tariffs; use observed usage and exact dated model/provider prices.
[Prompt caching](https://openrouter.ai/docs/guides/best-practices/prompt-caching).

OpenRouter response caching is a different feature: it can replay successful
tool-call responses, with new generation IDs and zeroed billable counters. It is
off unless enabled by request/preset; `X-OpenRouter-Cache:false` disables it even
when a preset enables it. It is not prompt KV reuse.
[Response caching](https://openrouter.ai/docs/guides/features/response-caching).

**Native recommendation:** use an explicit stable non-secret session scope for
affinity only after policy selection. Default main coding must not inherit
unselected answer/tool-call replay. Send the explicit response-cache opt-out when
that is the selected policy; retain cache-status headers. Cache markers belong
at chosen reusable content endpoints after final request assembly, independently
of the session routing key. Do not assume direct OpenAI image/file marker support
from OpenRouter's current text-block contract.

## Direct mock oracles for the implementation plan

- Round-trip a selected OpenRouter slug through startup, persistence/reopen,
  request, returned runtime identity and audit; an unknown slug gets metadata or
  an explicit unknown-capability state, never the fixed OpenAI model table.
- Exact tool continuation: typed native arguments/results become valid external
  fields; namespaced call IDs remain distinct; encrypted reasoning and assistant
  phases survive byte-for-byte as field values.
- Feed split SSE framing, parallel argument deltas, completed output, trailing
  metadata, HTTP-200 failed/incomplete/error events, refusal output, malformed and
  truncated records. Assert only a valid accepted response can advance the tool
  loop; retain every received original and usage observation.
- Exercise null/missing cost/write fields, upstream/account charge distinctions,
  provider fallbacks, missing/unknown routing metadata, cache headers and session
  override precedence. Never turn absent metadata into invented provider identity.

These are implementation oracles, not live access/cache-benefit experiments.
The owner runs the integrated source gate after implementation; this research
does not add a separate gate or reopen the failed Kimi review.
