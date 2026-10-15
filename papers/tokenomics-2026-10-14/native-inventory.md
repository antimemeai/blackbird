# Native request, context and usage inventory

2026-10-14. Research against the source descended from merged master `d74f248`.
Read-only source and existing report inspection; no provider calls, private session
exports, tests, builds, executable changes or dependencies. Line references below
refer to this source snapshot. Provider entitlement, tokenization, cache lifetime,
pricing and endpoint support are questions for the separate provider-contract lane.

## Findings that affect the next design

1. The default loop resends the complete live input and complete native/Lua tool
   schema set at every model step. Prior provider outputs, including opaque reasoning,
   and tool results become input on the next step. This creates reusable material,
   but it does not establish a cache hit. [Loop](../../programs/turn.lua:2),
   [assembly](../../src/coding.cpp:4061), [tools](../../src/coding.cpp:2280),
   [output acceptance](../../src/coding.cpp:4525).
2. Native assembly mixes changing task state and, when triggered, changing context
   budget data into top-level instructions. That is a concrete stability problem to
   investigate before assuming history is one reusable prefix. The effect on the
   provider's actual token prefix remains unmeasured; JSON member order is not a
   provider token ordering contract. [Instructions](../../src/coding.cpp:4141).
3. `/stats` exposes native packet bytes and the last response's selected provider
   counters. It is neither a cumulative token ledger nor a bill. Original request/
   response capture offers an existing path for a tertiary reader to recover exact
   reported counters without adding transforms to producers.
   [Stats](../../src/coding.cpp:5254), [raw bodies](../../src/raw_provider.cpp:320),
   [originals/linkage](../../src/provider_original.cpp:431).
4. There are two source-grounded route inconsistencies to repair before comparing
   tokenomics: owned OpenAI requests skip native-to-external tool field conversion;
   owned HTTP 429 errors bypass the main retry classifier. These are correctness
   findings from source, not observed live failures. Details and direct regression
   seams follow below.
5. Lua already permits request-local cache fields through its general override
   mechanism. Native code supplies no cache capability catalog, cache-key policy,
   breakpoint placement or cache-aware layout. Retained program defaults accept
   only model/effort and workflow/module fields; a cache policy cannot simply be
   added to `program_config` today. [Overrides](../../src/coding.cpp:4129),
   [retained config validation](../../src/coding.cpp:2141).

## What actually goes into a main request

A turn retains its effective Lua source, appends a nonempty operator prompt as a
user item, and evaluates a fresh Lua runtime. The program source is audited; it is
not automatically inserted verbatim into model input. The current default program
makes at most 64 requests, executes returned function calls sequentially, appends
native results with their `call_id`, presents messages and stops when there are no
calls or restart is pending. Default `parallel_tool_calls=false` does not prevent a
custom Lua program from explicitly orchestrating other work.
[Turn entry](../../src/coding.cpp:5615), [program](../../programs/turn.lua:1).

`CodingEngine::request` validates existing call/result linkage, drains queued local
workflow continuations, then snapshots `context_.items()` in existing entry order.
Completed registered workflow results can append developer text containing the
formatted result. Entry IDs, ancestry and revision wrappers are omitted from the
normal input projection; `items()` returns each entry's `item` only.
[Assembly](../../src/coding.cpp:4048), [workflow result](../../src/coding.cpp:4038),
[projection](../../src/context.cpp:752).

The default native request contains:

| Field | Current source/behavior |
| --- | --- |
| `model` | Retained program override, else engine model; launcher default `gpt-6.1-sol`. |
| `instructions` | Long native instruction string plus host cwd, then optional task/budget additions. |
| `reasoning.effort` | Retained program override, else engine effort; launcher default `medium`. |
| `input` | Entire projected live context, in entry order. |
| `tools` | All native definitions, then all effective Lua definitions in registry order. |
| `parallel_tool_calls` | `false`. |
| `include` | `reasoning.encrypted_content`. |
| `store`, `stream` | `false`, `true`; other values are rejected. |

[Default fields](../../src/coding.cpp:4063), [launch defaults](../../src/main.cpp:82).
There is no default max-output-token bound, service-tier selection,
`previous_response_id`, prompt-cache key or retention field in this assembly.
This is a source inventory, not a claim that a provider lacks those features.

Lua `blackbird.request(options)` converts arbitrary native Lua values, then merges
all fields except local `retry_policy` into the request. Unknown fields are added;
known fields replace defaults. Model, input, tools, reasoning, include and even the
instruction string can be overridden. Task and budget additions happen afterward.
`store=false`/`stream=true` and input call/result linkage remain enforced. There is
no general request-option whitelist or provider capability validation.
[Bridge](../../src/coding.cpp:1585), [merge](../../src/coding.cpp:4129),
[post-merge checks](../../src/coding.cpp:4140).

## Cache controls and content breakpoints: what survives native validation

Request-local fields such as `prompt_cache_key` or `prompt_cache_retention` would
survive the native merge and JSON writer as ordinary fields, provided the request
otherwise satisfies the checks. That establishes passthrough only. It does not
establish that any selected provider/account accepts or acts on them.
[Merge](../../src/coding.cpp:4129), [writer](../../src/json.cpp:334).

Ordinary context append only requires each item to be an object. Request
`validate_protocol` checks duplicate, orphan and unanswered function calls; it does
not whitelist message content types or strip extra nested keys. Extra metadata
such as a `cache_control` member on an existing text part can therefore survive
native storage/request assembly. A request-local input override can likewise
supply such members. No native code inserts, scopes or counts breakpoints.
[Append](../../src/context.cpp:919), [protocol check](../../src/coding.cpp:285).

The successor-seed import path is stricter: message parts must be `input_text`,
`output_text` or `refusal`; reasoning summaries must be `summary_text`, with
self-contained encrypted reasoning. Additional keys on accepted parts are not
rejected. A provider-specific new block *type* can therefore pass ordinary live
assembly but fail successor import. Anthropic's colleague adapter constructs a
string `system` and one user message itself; the colleague request has no generic
upstream override or cache-breakpoint facility.
[Seed checks](../../src/context.cpp:823), [colleague construction](../../src/colleague.cpp:274).

Lua object fields are sorted on conversion into native values, whereas owned C++
objects and arrays preserve their constructed order. This can make serializations
of equivalent structures differ across construction paths. No cache benefit is
inferred from field ordering alone. [Lua conversion](../../src/coding.cpp:252),
[JSON writer](../../src/json.cpp:375).

## Supported routes, with account distinctions kept intact

The actual main loop instantiates `OpenAiCodingProvider`; changing a model name
alone does not select an Anthropic or chat-completions main loop.
[Main](../../src/main.cpp:428), [provider](../../src/coding.cpp:327).

| Route | Actual source behavior |
| --- | --- |
| Owned OpenAI selected account | `codex_login` recognizes owned selection; `openai_http` calls `ProviderAuth::request("openai","responses",...)`. Built-in base is `https://api.openai.com/v1`. Selected account kind can be key or OAuth. Do not relabel this source route as the Codex backend subscription endpoint. |
| Codex compatibility auth | With no owned selected account, reads installed Codex ChatGPT auth, requests auth status/refresh through `codex app-server`, verifies account consistency and calls fixed `https://chatgpt.com/backend-api/codex/responses` with account header. |
| Context-only OpenAI colleague | Owned binding if selected, else same Codex compatibility path; no tools, one selected-context prompt. |
| Other native colleagues | Registry protocol dispatch: `responses`, `messages`, or `chat` mapped to `responses`, `messages`, `chat/completions`; tools unavailable. |
| Claude compatibility colleague | Existing `claude --print --output-format json`, empty tools/settings/MCP and no session persistence. This is an external CLI service with its own reported usage/model metadata. |
| Decision model | Separate `decision_model` tool calls TypeSafe `/v1/systemone` directly; its response carries independently validated input/output usage. |

[Owned selection/compat auth](../../src/openai.cpp:157),
[owned dispatch](../../src/openai.cpp:269), [compat endpoint](../../src/openai.cpp:290),
[colleague preparation](../../src/colleague.cpp:246),
[colleague transport](../../src/colleague.cpp:468), [Claude argv](../../src/colleague.cpp:299),
[decision endpoint](../../src/decision_models.cpp:327).

Built-in other providers are Anthropic and Kimi (`messages`); MiMo, Grok,
Moonshot and OpenRouter (`chat`). Registry extensions accept HTTPS bases and one
of the three protocols. Selected account state prevents silent environment-key
fallback; when no account is selected, `ProviderAuth::request` can use its
provider-specific key environment variable. The main OpenAI login selector tests
owned selection first, so that environment fallback in the generic auth helper
does not establish an `OPENAI_API_KEY`-only main-loop route.
[Descriptors](../../src/provider_auth.cpp:271),
[extension checks](../../src/provider_auth.cpp:985),
[account/key resolution](../../src/provider_auth.cpp:1375),
[environment fallback](../../src/provider_auth.cpp:1492).
No credential contents or active-account assumptions were inspected.

## Prefix geometry and repeated payloads

The naturally stable pieces are the native instructions (within one cwd/build),
native schemas, effective Lua descriptions/parameter schemas, and the unchanged
leading context items. Each successful response appends full output, including
reasoning items; each tool result follows. The next request carries that retained
material again. Encrypted reasoning is preserved as opaque content, not converted
to visible reasoning or estimated from its byte size.
[Tools](../../src/coding.cpp:2285), [include](../../src/coding.cpp:4099),
[accept output](../../src/coding.cpp:4525),
[opaque SSE handling](../../src/openai.cpp:573).

Two native additions can change a nominally stable instruction block:

- Once tasks exist or task revision is nonzero, every request appends
  `CURRENT_TASKS` with up to 16 compact open items plus fixed task instructions.
  Edits/completion affect this request content; the addition can persist even
  when no open items remain. [Task inclusion](../../src/coding.cpp:4141).
- When the opt-in byte trigger fires, every request appends `CONTEXT_BUDGET` with
  current input bytes, effective/pending policy revisions and capability fields,
  followed by fixed compaction advice. Input byte growth alone can change the
  instruction string on successive steps. [Budget injection](../../src/coding.cpp:4166),
  [budget value](../../src/coding.cpp:2379).

Colleague prompts start with a formatted object containing request ID, from/to,
task, then selected context. IDs/address/task can change ahead of otherwise
identical source text. Participants also derive addressed request IDs/tasks for
new directions. These are native layout facts; any provider cache consequence is
conditional on its token ordering and policy.
[Prompt](../../src/colleague.cpp:222), [directions](../../src/participants.cpp:1218).

No existing cache-hit causal experiment, token-prefix comparison or native
cache-aware scheduling was found. Byte common-prefix length of JSON/native
packets would be a diagnostic of representation stability, not actual provider
cached-token eligibility. Use the exact external request and reported cache
counters for those separate questions.

## Tool representation and output selection already available

Every main request advertises the native schema array plus effective Lua tool
schemas. Lua implementation source stays retained locally rather than being sent
as part of the schema; `request_tools` selects name/description/parameters only.
A Lua program can replace `tools` request-locally, but this is model-authored
behavior, with no automatic discovery/subset planner in the default loop.
[Native schema array](../../src/tools.cpp:364), [Lua schema projection](../../src/coding.cpp:2285).

The default loop calls tools with native arguments and appends the entire native
return. A Lua tool can perform multiple local operations and return a selected
value; internal call results do not all have to become main-model context unless
the workflow returns/appends them. This is a concrete existing code-mode seam,
not measured token savings. [Loop](../../programs/turn.lua:9),
[Lua tool surface](../../src/tools.cpp:1262).

`read_file` captures the complete file original (16 MiB limit) and returns a
selected line/byte presentation. With no range it returns the whole file. Ranges
reduce model-facing bytes while preserving the original; they do not reduce full
local file read cost. Invalid UTF-8 presentation becomes a hex object.
[Read path](../../src/tools.cpp:218), [range](../../src/tools.cpp:160),
[hex presentation](../../src/tools.cpp:99).

Main `exec` accepts `output_max_bytes`, captures full originals, and can expose
omitted output through `read_process_output`. The default owned job wait maximum
is 16 MiB, so small-output behavior is not selected automatically. Snapshot output
is bounded by retained tail/capacity; result metadata distinguishes received,
retained, returned, omitted and offset bytes. These counters are bytes, not
provider tokens. [Exec option](../../src/coding.cpp:4559),
[default](../../include/blackbird/command_jobs.hpp:12),
[snapshot](../../src/command_jobs.cpp:396), [advertised contract](../../src/tools.cpp:878).

## Compaction changes the live view, not the originals

Default context byte policy is disabled; advisory defaults are trigger 262144 and
target 131072 native input packet bytes. No tokenizer or qualified model context
window catalog is present; `context_window_tokens` is explicitly unavailable.
[Defaults](../../src/coding.cpp:2032), [capability](../../src/coding.cpp:2395).

Managed proposals support select/archive/summarize/restore, with base/reason/source,
whole tool groups and mandatory live user/system/developer entries. Summaries
must be nonempty assistant/developer text. Under a workflow, proposals stage and
publish only on successful completion; failure cancels them. Publication preserves
append-only continuation settlement. Restore merges exact originals in capture
order. [Validation](../../src/context.cpp:1295),
[finish](../../src/context.cpp:1379), [publication/restore](../../src/context.cpp:1485).

Consequences: all operator messages remain mandatory in this managed path; a
summary cannot automatically reclaim arbitrary old user instructions. Successful
compaction may rewrite an early input span and sacrifice cache reuse of later
material. Repair may grow the input again. Summary quality is not established by
structural acceptance, and smaller native bytes do not prove fewer provider tokens
or equal task quality. Source-backed existing tests cover policy staging,
cancellation, intentional omission and exact restoration before a new request;
they are not token/quality experiments. [Existing oracle](../../tests/context_budget_test.cpp:138).
The semantic audit remains capacity-bounded independently of live context;
compaction does not reclaim it. [Headroom warning](../../src/coding.cpp:4264).

## Usage: exact observations versus absent accounting

| Quantity | Native observation and limitation |
| --- | --- |
| `stats.input_bytes`, per-entry `item_bytes`, `view_bytes` | Encoded native packet sizes; include native representation overhead, not tokenizer counts. [Context stats](../../src/context.cpp:716). |
| `last_request_bytes` | Full encoded native request packet size, before external JSON formatting; not total wire bytes. Set once per request group. [Assembly](../../src/coding.cpp:4191). |
| Input/output/total tokens | Selected decimal, nonnegative integer fields from final returned response `usage`; no inference from bytes. Missing fields stay missing. [Selection](../../src/coding.cpp:4481). |
| Cache read observation | Only OpenAI-shaped `input_tokens_details.cached_tokens` is copied into main stats if present. It is a provider-reported count, not a native cache or bill. |
| Cache writes/other provider details | No selected main stats fields for cache creation, TTL buckets, input modality, output modality, price or invoice. Exact upstream originals can contain more fields. |
| Reasoning | Only reported `output_tokens_details.reasoning_tokens` selected. Opaque encrypted-content bytes and visible text length are different quantities; no accounting is derived from them. |
| UI | Plain diagnostics show selected usage object. Terminal status shows input/output only; cached/reasoning/total omitted. [Terminal callback](../../src/main.cpp:530). |
| Scope/restart | `usage_` cleared at new request assembly, then set from last returned response; stats explicitly say last request in this process, null when unavailable. No cumulative session/workflow/account bill. [Reset](../../src/coding.cpp:4199), [scope](../../src/coding.cpp:5295). |
| Colleague usage | Copies upstream `usage` as reported; Claude also preserves `modelUsage`, with observed single model or ambiguous multiple models. Failure/refusal can retain received metadata. [Metadata](../../src/colleague.cpp:96), [capture ordering](../../src/colleague.cpp:364). |
| Jev usage | Validates response input/output token counters and preserves full response under the tool return; not folded into main-model stats. [Validation](../../src/decision_models.cpp:238), [return](../../src/decision_models.cpp:424). |
| Monetary cost | No native price table, bill, entitlement reconciliation or currency calculation in these paths. Subscription counter consumption is not an API invoice. |

Main usage selection happens after transport/decode succeeds but *before* the
context-head conflict check and final output append. A returned provider usage
value can therefore be observed even if later native acceptance fails. Conversely,
SSE `response.failed`/`response.incomplete` causes decode to throw before main
usage selection; any reported counters in those raw events are not copied into
`usage_`. Do not equate accepted context with all consumption.
[Ordering](../../src/coding.cpp:4480), [decode errors](../../src/openai.cpp:495).
The existing usage test checks selective presentation and null reset, not a
complete ledger. [Test](../../tests/coding_test.cpp:1336).

Raw acquisition captures external request bodies (site 1301), selected child-read
original bytes (1404), native phase/request/wrapper originals (1322), actual links
(1324) and retry policy/decisions/wait clocks (1325–1327). Semantic `provider.request`
records associate attempt/revision/generation/retry group/ordinal; bounded
`provider.stream` blocks retain received content. These are suitable inputs to a
tertiary tokenomics reader, subject to retained-source completeness and unresolved
acquisition cuts. Do not count duplicate representations or delta text plus final
response as duplicate model consumption. [Raw fields](../../src/raw_provider.cpp:320),
[phase fields](../../src/provider_original.cpp:431),
[semantic linkage](../../src/coding.cpp:4294), [stream capture](../../src/coding.cpp:4314).
Current architecture requires lossless raw capture/transport and a separate
transformer, not producer/recorder aggregation or price normalization.
[Instrumentation contract](../../docs/INSTRUMENTATION.md:3).

## Retries and two concrete route defects

One assembled request is reused within a retry group; default policy is five
attempts, 1000 ms base and 16000 ms cap with doubling. Lua may set 1–100 attempts
and 0–60000 ms base/cap. Only selected transient HTTP/curl errors retry while the
journal remains live. Partial output is retained/previewed but not accepted as
context or dispatched as tools. Curl itself has no retry option. Repeated provider
attempts can consume tokens even when the client did not obtain counters; this
inventory cannot quantify that consumption.
[Policy](../../src/coding.cpp:4105), [classification](../../src/coding.cpp:4401),
[compat curl](../../src/openai.cpp:325), [existing direct retry oracle](../../tests/coding_test.cpp:557).

**D1 — owned OpenAI boundary conversion differs.** `import_provider_response`
converts external JSON argument strings to native values. Default Lua appends
native tool return values. The compatibility route explicitly serializes
`export_provider_request`, which JSON-encodes nonstring `function_call.arguments`
and `function_call_output.output` exactly at the external boundary. The owned
route passes the native request directly to `ProviderAuth::request`, which uses
`dump_json(body)` without this conversion.
[Import/export](../../src/json.cpp:431), [default output](../../programs/turn.lua:10),
[compat serialization](../../src/openai.cpp:301),
[owned dispatch](../../src/openai.cpp:276), [owned serialization](../../src/provider_auth.cpp:1530).

Synthetic source-derived example, not captured private traffic:

```text
Native input items:
  {type="function_call", call_id="c", name="read_file", arguments={path="README.md"}}
  {type="function_call_output", call_id="c", output={content="hello"}}

Compatibility external fields:
  "arguments":"{\"path\":\"README.md\"}"
  "output":"{\"content\":\"hello\"}"

Owned-route external fields from current dump_json(body):
  "arguments":{"path":"README.md"}
  "output":{"content":"hello"}
```

The type asymmetry is established by source. Actual provider rejection was not
exercised, and a future API might support additional output forms; native argument
objects still require deliberate protocol handling. Direct regression seam: use
an injected owned-auth HTTP callback to inspect the serialized request body for a
complete native call/result pair; assert boundary string types and exact decoded
values, with native retained items unchanged. Pair with the compatibility transport
fixture only as needed to establish route parity. Current auth test calls
`responses` with an empty object and only checks auth headers, so it does not cover
this continuation. [Auth fixture](../../tests/provider_auth_test.cpp:48).

**D2 — owned HTTP 429 bypasses main retry eligibility.** Compatibility HTTP maps
non-2xx to `external_unknown(status)`; owned auth maps 429 to
`provider_rate_limit(429)`. Main retry code tests 429 only inside
`external_unknown`. The same HTTP response therefore selects different retry
behavior by auth route. No live failure or billed retry count is claimed.
[Compat status](../../src/openai.cpp:350),
[owned status](../../src/provider_auth.cpp:422),
[retry classifier](../../src/coding.cpp:4402).
Direct regression seam: exercise a main request against an owned transport fixture
returning 429 then success and assert attempt count/group/ordinal and exact request
reuse; retain a terminal auth failure case. Current flaky-provider cases include
`external_unknown(429)` but not this adapter mapping.
[Existing cases](../../tests/coding_test.cpp:695).

## Bounded existing live evidence and implementation seams

The existing 2026-10-08 collaboration report records one main repair turn with four
successful requests totaling reported 20632 input/376 output tokens, and a
context-only OpenAI participant with 6134 input/527 output. It also records Claude
multi-model metadata and an earlier timeout with unknown consumption. Those are
historical workload observations, not a current cache baseline, complete bill,
matched token-efficiency comparison or quality estimate. No session contents were
read for this inventory. [Existing report](../2026-10-08-collaboration-campaign.md:65).

Useful next native seams, in dependency order:

1. Resolve D1/D2 with direct adapter-level correctness cases before interpreting
   any owned/compatibility comparison. Keep native retained values and originals.
2. Build tokenomics projections in a tertiary reader over exact external request,
   response and attempt links. Separate last-request UI from per-attempt reported
   usage, missing usage, cache read/write classes and independently priced routes.
   Preserve unknown attempts; never backfill them as zero.
3. Compare exact stable/changing request regions: instructions/task/budget, tool
   schemas, live input and selected-colleague wrapper. A proposed stable layout
   should preserve the model-visible meaning and audit original/final requests.
   Model/provider token prefix and reported cache use require direct evidence.
4. Keep cache controls in provider/account-aware adapters and model-authored request
   choices. Generic passthrough already exists; provider support and retained policy
   are separate work. Do not make unsupported cache controls global defaults.
5. Use existing range/output-budget/Lua return/managed-context machinery for bounded
   representation experiments. Compare provider counters, cache counters, latency
   and retained task quality separately. No automatic result reuse or unrequested
   truncation is selected by this report.

These are research seams for root synthesis, not an implementation plan, adopted
API, newly selected evaluation campaign or executable activation.
