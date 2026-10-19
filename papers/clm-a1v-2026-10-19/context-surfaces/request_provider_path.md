# Context surfaces: request and provider path

This is a read-only source catalogue of worktree `context/clm-a1v` at `f7a79b1`, read on 2026-10-19 with a clean `src/`. Nothing was built, run or tested, and no provider was called.

It is input for three designs in PLAN.md ("Operator decision: Blackbird owns compaction through context generations"):
- **context generations**: a new provider-side conversation started from compacted context, grouped under one Blackbird session;
- **tool-output shaping** when the output is created;
- **shipped provider profiles**: provider rules held as config, not C++.

It extends [C6_SOURCE_MAP.md](../C6_SOURCE_MAP.md). That map was written at `1f28945`; line numbers here are re-read at `f7a79b1`.

How to read it:
- References are `file:line` relative to the worktree.
- **Dir** column: **R** reads the main context, **W** writes it, **T** transforms a copy, **S** sends to a provider or transport.
- **Unconfirmed** marks an inference that was not checked in source.
- §1 was read directly. §2-§4 were compiled by three parallel read-only passes and merged. Their key claims were spot-checked against source: `validate_protocol`, `export_provider_request`, the portable Responses rules, the Anthropic message mapping, the state-item append and `whole_rounds`.

Contents:
1. CodingEngine request assembly
2. Provider adapters: context items to wire format and back
3. Tool dispatch and how tool results become context items
4. Other request makers, identity, restart and continuation
5. Provider rules currently hard-coded in C++
6. Assumptions and coupling points for context generations
7. Unconfirmed items

## 1. CodingEngine request assembly (src/coding.cpp, include/blackbird/coding.hpp)

Direction key: **R** reads context, **W** writes context, **T** transforms a copy, **S** sends to a provider/transport.

### 1.1 Provider interface and engine state that a request touches

| file:line | surface | dir | notes |
|---|---|---|---|
| coding.hpp:21-49 | `CodingProvider` base | S | `request_group_owned_` plain bool (:23); `cancelled` shared `std::function` (:27); virtuals `provider_identity` (:28, default "unavailable"), `begin/end_request_group` (:31-32), `supports_prompt_cache_policy` (:33), `retain_request_state` (:35, "Signed native histories must replay operational input at its original place"), three `respond` overloads (:36-48). No virtual for "start new conversation", item-kind capability, or generation id. |
| coding.hpp:50-69 | `OpenAiCodingProvider` | S | identity `openai-codex` (:53-55); `request_binding_`, `responding_` plain (:67-68). |
| coding.cpp:319-327 | `OpenAiCodingProvider::begin_request_group` | S | `busy` if a binding exists; binds `openai_request_binding(config)`, copying `cancelled`. |
| coding.cpp:329-340 | `supports_prompt_cache_policy` | S | **Hard-coded:** true only for `kind=api_key`, `base_url=https://api.openai.com/v1`, `protocol=responses`. |
| coding.cpp:349-391 | `respond` | S | optional timeout 1..3600 s (:352-357); `busy` on re-entry (:361-362); temporary group if none (:373-374); `codex_login` then `openai_http(config, login, "responses", &request)` (:389-390) then `completed_response` (:391). |
| coding.hpp:140-145, 187-201 | engine single slots | R/W | `last_request_bytes_`, `usage_` (:187), `previewed_` (:203), `budget_`/`pending_budget_` (:193), `lua_tools_`/`pending_lua_tools_`, `program_config_` (model/effort/modules/prompt_cache/workflows). All per-engine, overwritten per request. |
| coding.hpp:208-211 | `identity_` (SessionIdentity), `restart_note_`, `generation_` (DefinitionGenerationId) | — | **Name collision:** `generation_` is a *definition* generation, re-issued every turn (coding.cpp:5942) and written as `"generation"` into `provider.request` metadata (:4578-4579) and operation admission metadata (:3323). A "context generation" id needs a distinct name. |
| coding.cpp:2532-2537 | `CodingEngine` ctor | — | one `ContextStore&`, one `CodingProvider&`, one `model_` string per engine; `identity_ = session_identity(log)`. |

### 1.2 `CodingEngine::request(options)` (coding.cpp:4248-4813)

| file:line | surface | dir | notes |
|---|---|---|---|
| 4249-4262 | `RequestGroup` RAII | — | clears `request_group_active_` and `provider_.request_group_owned_`; calls `end_request_group` only if begun. |
| 4267-4268 | re-entry guard | — | `busy` if a group is active on engine or provider. |
| 4271-4272 | restart guard | — | `busy` while `restart_note_` set (no requests after restart scheduled). |
| 4274 | `validate_protocol(context_.items())` | R | open tool group → `conflict`. `validate_protocol` (:286-317) checks only `function_call`/`function_call_output` pairing (duplicate call ids, unmatched outputs, unanswered calls). `custom_tool_call` and reasoning are ignored. |
| 4276 | `drain_workflows()` | W | runs parked workflow continuations (:4215-4247): each `execute_workflow` may make nested requests/calls; each completion appends a **developer** message `"Registered workflow NAME completed in this turn: <format_value(result)>"` with origin `workflow-continuation` (:4238-4244). Unbounded text size (only `format_value` limits). |
| 4277-4279 | snapshot | R | `items = context_.items()` (context.cpp:752-763: plain projection of every entry's `item`, no filtering), `origin = context_.head()`, revalidate. **The whole live context is the input; there is no per-provider or per-generation selection.** |
| 4280-4327 | request object | T | `model` = `program_config_.model` or `model_` (:4281-4283); `instructions` = fixed native doctrine string ending `"Host cwd: " + current_path()` (:4284-4313); `reasoning.effort` (:4314-4319); `input` = items (:4320); `tools` = `request_tools()` (:4321); `parallel_tool_calls=false` (:4322); `include=["reasoning.encrypted_content"]` (:4323); `store=false` (:4324); `stream=true` (:4325). Responses-API shape is the canonical internal request; adapters convert from it. No `tool_choice`, no `max_output_tokens`, no `prompt_cache_key`, no conversation/session header at this layer. |
| 4328-4351 | `retry_policy` | — | defaults 5 attempts, 1000 ms base, 16000 ms cap; limits 1..100, ≤60000. |
| 4352-4357 | `cache_policy` | — | option, else `program_config_.prompt_cache`, else `provider_default`. |
| 4358-4362 | `state_context` | — | `auto` (default) or `omit`; `omit` suppresses CURRENT_TASKS/CONTEXT_SIZE/CONTEXT_BUDGET. |
| 4363-4374 | generic option override | T | **every other option key replaces or adds a top-level request field**, including `input`, `instructions`, `tools`, `model`, `tool_choice`, `include`. A Lua program can already send a hand-built `input` (a "selection"), but the response is still appended to the main context (:4803). |
| 4375 | `store` read | — | after override. |
| 4376-4392 | `CURRENT_TASKS` | R(tasks) T | `tasks_.read({status=open, limit=16, compact=true})` + fixed guidance text. Source is TaskStore, not context. |
| 4393-4395 | wire guard | — | `store` must be false and `stream` true, else `unsupported`. **Hard rule: stateless, streaming only.** |
| 4396 | validate `request.input` | R | also validates a Lua-supplied `input`. |
| 4397-4400 | size and trigger | R | `input_bytes` = native packet encoding of `input` **before** state is appended; `budget_view` (:2438-2471) returns `triggered`, `capability.context_window_tokens = null` ("no qualified model capability catalog", :2475-2478). Defaults: `trigger_bytes=393216`, `target_bytes=196608`, enabled (:2080-2082). |
| 4401-4416 | `CONTEXT_SIZE` line | T | sent when `state_context` and (`!retain_request_state()` or triggered). Anthropic (retaining) only sees it with the trigger. |
| 4417-4434 | `CONTEXT_BUDGET` guidance | T | full `budget_view` JSON + text; states proposals "activate only after successful workflow completion" (:4429-4430). Must change for C6/generations. |
| 4435 | `reusable_input_items` | — | count of `input` items before state; passed to cache policy. |
| 4436-4451 | operational state item | W/T | a `{role=developer, content=operational_state}` item is **appended to the request input** (end of input). If `retain_request_state()` (Anthropic) it is **also appended to the main context** with origin `provider.request-state`, `origin` advanced, `reusable_input_items++` (:4441-4445). So for Anthropic, every request with state leaves a developer item in context (CURRENT_TASKS each time; CONTEXT_SIZE/BUDGET only when triggered). For other providers state is ephemeral, at the tail, so it never enters the cacheable prefix. |
| 4453-4461 | group taken; `provider_.cancelled` overwritten | — | lambda polls commands and engine `cancelled`. Never restored. |
| 4462-4463 | `provider_.begin_request_group(request, ctx)` | S | provider may refuse/prepare (OpenAI binds auth; others see request). |
| 4464-4466 | `prepare_request_cache` (request_cache.cpp:82-102) | T | see 1.3. |
| 4467-4468 | head CAS | R | `context_.head() != origin` → `conflict`. |
| 4469-4477 | final packet | T | `ProviderOriginalLease{request_packet, codec_adopt}` adopts the native packet encoding of the full request; `last_request_bytes_` = its size; `usage_` reset. |
| 4478-4481 | request size display | — | `"[request N native request bytes]"` to `diagnostic`/`display`. |
| 4483-4497 | raw scalars | — | definition generation (field 8) and origin revision (field 7) bound into the assembly raw phase. |
| 4517-4525 | retry group | — | fresh `retry_id` (`OperationAttemptId`) per request, raw event 1325 (lane, ordinal, retry id, policy); `provider_.cancelled` re-set. |
| 4531-4750 | attempt loop | S | per attempt: cancel check (:4537); journal headroom warning (:4540-4554, "context compaction does not reclaim audit history"); `previewed_.clear()` (:4556). |
| 4559-4565 | `operation("provider", request, body)` | S/audit | the **whole pre-export request Value** is the operation input: `encode_original(input, RawOperationOriginal::input)` (:3310-3312); admission `DecisionEvent{actor, conversation, workflow, definition generation, context revision, invocation}` (:3345-3354) + `InvocationEvent` + `AttemptAdmissionEvent`. |
| 4566-4570 | `observations_->doctrine(instructions, invocation, attempt)` | — | observations.cpp:112-131: stores an id per distinct instruction text, samples `doctrine.effective` with `field=instructions`. |
| 4571-4582 | `provider.request` log record | — | metadata `attempt`, `revision=origin`, `generation` (definition generation), `input_binding=attempt-invocation-v1`, `retry_group`, `ordinal`. **This is the attempt→context-revision link**; there is no conversation/generation field. |
| 4588-4601 | `StreamCapture` | — | every raw stream block retained as original `provider.stream` with `attempt`, `revision`, `capture_policy=bounded-blocks-v1`; capacity errors count `unretained_bytes_`. |
| 4609-4626 | `provider_.respond(request, cb, owner, ctx)` | S | callback: `poll_commands()`, capture, then `ResponsePreview::feed` (openai.hpp:58-69; parses Responses **and** Anthropic SSE, openai.cpp:532-559) → `previewed_[item_id] += text`, `display(text)`. Preview never publishes context. |
| 4656-4662 | acceptance | T | `import_provider_response(result)` (json.cpp:432-451): parses each `function_call.arguments` JSON string into a Value. Nothing else normalised here. |
| 4673-4701 | retry eligibility | — | **Hard-coded:** HTTP 408/429/500/502/503/504 (`external_unknown`), curl codes 5,6,7,16,18,28,52,55,56,92,95,96 (`provider_transport`), typed `provider_rate_limit` 429; only while journal live. Context-length/overflow is not retryable here (C4 territory). |
| 4702-4712 | retry notice | — | original `provider.retry` with `retry_group`; "partial output not accepted". |
| 4713-4746 | backoff wait | — | 25 ms cancel poll, clock facts 1327. |
| 4752-4791 | usage selection | — | keeps numeric `input_tokens`, `output_tokens`, `total_tokens`, `input_tokens_details.cached_tokens`, `output_tokens_details.reasoning_tokens` only (Responses names; adapters must map to them). → `usage_`, `observed_usage`, `"[provider usage …]"`. |
| 4794-4795 | head CAS again | R | `conflict` if context moved during dispatch. |
| 4803 | **output append** | W | `context_.append(response.output, "provider.branch:" + origin)`: every output item (message, reasoning with `encrypted_content` or `anthropic_content`, function_call) enters context as one append; origin label carries the request's base revision. Opens the tool group. |
| 4804-4810 | head raw scalar | — | new head bound to final acceptance. |
| 4811 | return | — | the full response Value goes back to Lua (`blackbird.request()`). |

### 1.3 Prompt-cache boundary (src/request_cache.cpp, json.cpp)

| file:line | surface | dir | notes |
|---|---|---|---|
| request_cache.cpp:73-81 | `prompt_cache_mode` | — | `provider_default` / `stable_prefix` / `disabled`. |
| request_cache.cpp:82-102 | `prepare_request_cache` | T | no-op for `provider_default`. Otherwise **hard-coded model/provider allowlist**: `openrouter-responses` + `openai/gpt-6.1-sol`, or `openai-codex` + `gpt-6.1-sol` (:88-91); also requires `route_supported`. Conflicts if request already has `prompt_cache_options`, `prompt_cache_retention`, `cache_control`, or any input part marker. Adds local field `blackbird_cache_policy{mode, reusable_input_items}`. |
| request_cache.cpp:104-136 | `export_request_cache` | T | at export: removes local field, puts `prompt_cache_options{mode: explicit|implicit, ttl:"30m"}` (**hard-coded TTL**); for `stable_prefix`, walks back from `reusable_input_items` and marks the last `input_text` part of the nearest markable item with `prompt_cache_breakpoint{mode:explicit}` (string content is promoted to parts; assistant string content and function_call items are skipped). One breakpoint. |
| json.cpp:452-506 | `export_provider_request` | T | Responses export (used by openai.cpp:364,389, responses_provider.cpp:52, openrouter.cpp:102): **rejects** a `reasoning` item carrying `anthropic_content` (`unsupported`, :456-458) — Anthropic thinking cannot be replayed to a Responses provider; strips `anthropic_message_id`/`anthropic_content` from all items; validates `blackbird_output_parts` (only `input_text`/`input_image`/`input_file`); JSON-encodes non-string `function_call.arguments` and `function_call_output.output`; strips `blackbird_output_parts`; then `export_request_cache`. |

Coupling: the cache prefix is "all context items before the state item". Any context edit, manage publication, or generation cut invalidates it. The Anthropic path appends the state item to context, so the next prefix includes it.

### 1.4 Tool advertisement

| file:line | surface | dir | notes |
|---|---|---|---|
| coding.cpp:2338-2350 | `request_tools()` | — | `tool_definitions()` (tools.cpp) + active Lua tools as `{type:function,name,description,parameters}`. Advertisement only; `call()` dispatches any name. |
| coding.cpp:2351-2430 | `define_tool` | — | Lua tool staging; ≤32 tools, ≤64 KiB encoded, activates at the workflow boundary. |

### 1.5 Lua bridge (coding.cpp:1056-1065, 1582-1690)

| file:line | surface | dir | notes |
|---|---|---|---|
| 1056-1059 | 19 bridged names | — | `request, call, context, edit, originals, restore, append, decode, encode, display, array, present, restarting, stats, manage, inspect, decode_packet, encode_packet, format`; `arco` alias (:1101). |
| 1628-1632 | `request_op` | S | `engine.request(options)`. |
| 1633-1638 | `call_op` | — | `engine.call(name, args)` (see tools section). |
| 1639-1647 | `manage_op`, `inspect_op`, `context_op` | R/W | direct `ContextStore` access, **not through `operation()`**, so not audited as operations. |
| 1648-1650 | `edit_op` | W | `context_.edit` publishes immediately, even mid-workflow. |
| 1651-1657 | `originals_op`, `restore_op` | R/W | originals listing; restore publishes. |
| 1658-1663 | `append_op` | W | `context_.append(items, "lua.synthetic")`: the **only** path by which tool results (`function_call_output`) enter context in the shipped turn programs. No shape check on append (append_impl, context.cpp:953-1001, only requires objects). |
| 1670-1677 | `display_op` | — | original `display`. |
| 1678-1680 | `present_op` | — | `present()` (:5664-5683) emits `output_text` parts minus streamed preview prefix; warns if final differs from preview. Not context. |

### 1.6 Turn boundaries (coding.cpp:5763-6031)

| file:line | surface | dir | notes |
|---|---|---|---|
| 5919 | `repairable_outputs_ = placeholders(false, origin)` | R | calls already open at admission. |
| 5923-5935 | settlement credit | R | from the full current view. |
| 5938 | `context_.begin_workflow(origin)` | W | one ContextStore workflow per turn. |
| 5942 | `generation_ = issue<DefinitionGenerationId>()` | — | new definition generation per turn. |
| 5960-5964 | operator prompt | W | `{role:user, content:prompt}` origin `operator`. |
| 5966-5968 | `runtime_->eval(program)` | — | Lua program drives requests. |
| 5970 | `drain_workflows()` | W | continuations after program return. |
| 5977-5978 | restart check | R | if restart scheduled, context must be protocol-complete. |
| 6001-6002 | `finish_workflow(true, boundary_program)` | W | activates staged managed proposal + program/tools/budget atomically. |
| 5889-5910 | rollback | W | `finish_workflow(false)`; on `interrupted`/`capacity` append `stop_outputs` (context.cpp:1253-1272: `function_call_output{error: turn_interrupted|turn_stopped, detail: "Consult audit…"}`) origin `stop.linkage`. |
| 5719-5736, 5737-5762 | `operator_call`, `operator_turn` | — | synthesise a program around `workflow_invoke`/tool calls; same request path. |

### 1.7 Other engine surfaces that read the context

| file:line | surface | dir | notes |
|---|---|---|---|
| 5606-5611 | `stats()` `context_budget` | R | size of full `context_.items()`; `last_request_bytes`, `usage` "last request in this process". |
| 2472-2531 | `context_budget` tool | R | budget view over the whole context. |
| 5620-5660 | `validate_session_switch`, `validate_restart`, `take_restart_note` | R | restart requires no participants/jobs and a protocol-complete context. |
| 5012-5031 | `restart` tool | — | sets `restart_note_` (≤64 KiB); request() then refuses. |
| 4566-4570 | doctrine observation | — | instructions identity per process. |
| main.cpp:1143-1147 | `/compact JSON` | W | `context.manage` between turns (immediate publish). |
| main.cpp:1161-1169 | `/model NAME` | — | `engine.model(name)` within the **same provider object**; provider is fixed per process at main.cpp:527-546. Existing reasoning items (encrypted for the old model) stay in context and are replayed to the new model. **Unconfirmed** whether providers accept another model's `encrypted_content`. |
| main.cpp:527-546 | provider construction | — | `openrouter` → OpenRouter; `anthropic` → Anthropic; any other non-`openai` name → `RegisteredResponsesCodingProvider{provider=name}`; `openai` → Codex. One provider per process. |

## 2. Provider adapters: context items to wire format and back

The shared engine pipeline is in §1. `export_provider_request` (json.cpp:452-506) is the common Responses export that the OpenAI, registered and OpenRouter adapters call.

### 2.1 Prompt cache (request_cache.cpp)

| file:line | surface | dir | notes |
|---|---|---|---|
| request_cache.cpp:83-84 | local field | reject | A request already containing `blackbird_cache_policy` throws `invalid_range`. |
| :85-86 | provider_default | — | No-op: nothing is added. |
| :87-93 | route gate | reject | Supported only for `openrouter-responses` with model `openai/gpt-6.1-sol`, or `openai-codex` with model `gpt-6.1-sol`, and only when `route_supported`. Otherwise `unsupported`. Anthropic and registered providers always fail when a non-default mode is requested. |
| :94-96 | conflict | reject | Throws if `prompt_cache_options`, `prompt_cache_retention` or `cache_control` is already present, or any content/output part has `prompt_cache_breakpoint`/`cache_control` (16-37). |
| :97-102 | local marker | W | Adds `blackbird_cache_policy:{mode,reusable_input_items}`. It is retained in the native request packet. |
| :104-135 | `export_request_cache` | transform | Called only from `export_provider_request`. Writes `prompt_cache_options:{mode: "implicit" (stable_prefix) \| "explicit" (disabled), ttl:"30m"}`. `disabled` stops there, so explicit mode with no breakpoint means no cache. `stable_prefix` walks backward from item `reusable_input_items-1` and marks exactly one breakpoint: the last `input_text` part of the first markable item gets `prompt_cache_breakpoint:{mode:"explicit"}` (38-50). `function_call` is skipped. String content or output on a non-assistant item is converted to `[{type:input_text,text}]` (63-69). This includes `function_call_output.output`, which then goes on the wire as an array. Assistant string content is not markable. If nothing is markable, it throws `unsupported` (134). |
| coding.cpp:329-340 | `OpenAiCodingProvider::supports_prompt_cache_policy` | — | True only when the binding has kind `api_key`, base_url `https://api.openai.com/v1` and protocol `responses`. |
| openrouter.hpp:18 | OpenRouter support | — | `return group_`, i.e. true whenever a request group is active. |

### 2.2 OpenAI / Codex adapter (`OpenAiCodingProvider`, identity "openai-codex")

| file:line | surface | dir | notes |
|---|---|---|---|
| coding.hpp:49-69 | class | — | `request_binding_` is an optional Value, `responding_` a flag. It does not retain request state. |
| coding.cpp:319-327 | begin_request_group | — | Throws `busy` if a binding already exists. Otherwise sets `request_binding_ = openai_request_binding(config)`. |
| coding.cpp:328 | end_request_group | — | Resets the binding. |
| coding.cpp:350-392 | respond | send | Sets a timeout scope, throws `busy` on reentry, creates a temporary group when none exists, calls `codex_login(config, refresh=false, &binding)`, then `openai_http(..., "responses", &request)`, then `completed_response`. There is no refresh-and-retry on 401. |
| openai.cpp:171-225 | `openai_request_binding` | — | Probes owned auth only when `codex_home` is empty and the executable is "codex" (175). If an owned `openai` account is selected, it uses the ProviderAuth binding (189-195). Otherwise it reads `~/.codex/auth.json` (or `$CODEX_HOME/auth.json`, 51-61, ≤1 MiB, 69-70), requires `auth_mode=="chatgpt"` and `tokens.account_id`, and builds the compatibility binding (157-164): kind `codex_chatgpt`, base `https://chatgpt.com/backend-api/codex`, protocol `responses`, revision -1. |
| openai.cpp:226-335 | `codex_login` | — | Owned path: returns an empty-credential login with `owned_auth_=true`. Compatibility path: spawns `codex app-server --listen stdio:// -c model_provider="openai"` (276-279) with a 30s deadline (280). JSON-RPC sequence: `initialize` (clientInfo blackbird/Blackbird/0.1.0), `initialized`, then `getAuthStatus{includeToken,refreshToken}` (283-298). Requires authMethod chatgpt. Re-reads auth.json and requires the same account and an access token equal to authToken, else `conflict` (304-308). Header values are checked for CR/LF/NUL and length ≤65536 (47-50). |
| openai.cpp:355-369 | `openai_http` owned path | send | Only route "responses", else `unsupported`. Sends `auth.request("openai","responses",export_provider_request(req))`, which goes to `https://api.openai.com/v1/responses` with Bearer auth (provider_auth.cpp:1521,1560). |
| openai.cpp:378-381 | compatibility routes | reject | Allows only `responses` or `models?client_version=0.160.0`. URL `https://chatgpt.com/backend-api/codex/<route>`. |
| openai.cpp:389 | body | transform | `dump_json(export_provider_request(request))`. |
| openai.cpp:397-406 | headers | W | `Authorization: Bearer`, `ChatGPT-Account-ID`, `Content-Type: application/json`, `originator: blackbird`. No session_id, conversation or version headers. |
| openai.cpp:415-417 | curl | — | `--disable --silent --no-buffer --max-time T --write-out "\n%{http_code}" --config -`. There is no `--proto =https` here, unlike the owned path. |
| openai.cpp:425-440 | response | R | Limit `response_limit` (16 MiB, openai.hpp:15). A non-zero curl exit becomes `provider_transport(exit)`. Any non-2xx becomes `external_unknown(status)`: on the compatibility route 401, 403 and 429 are not typed, although 429 is still retried via detail 429. An ETIMEDOUT interruption becomes `provider_transport 28` (446-448). |
| openai.cpp:582-736 | `completed_response` (SSE parser, shared) | R | Collects `data:` lines into events. Event `error` or `response.failed` gives `external_unknown`; `response.incomplete` gives `incomplete` (619-628). Any event after `response.completed` is `corrupt` (629-633). `response.output_item.done` items are recorded by `output_index`, and duplicate indices are `conflict` (634-656). `response.completed` requires `response.status=="completed"` and an output array (657-669). Other event types are ignored (670-671). If `completed.output` is empty, it is filled from the done items, which must have contiguous indices or it is `incomplete` (696-709). Otherwise each done item must equal `output[i]`, else `conflict` (710-714). A trailing partial line or missing completion is `incomplete` (676-677, 694-695). A literal `[DONE]` payload would fail JSON parsing: **Unconfirmed** whether any route sends it to this parser. Item `id`, `status` and `encrypted_content` pass through untouched. |
| openai.cpp:482-581 | `ResponsePreview::feed` | R (display only) | Skips `[DONE]`. Shows `response.output_text.delta` by item_id, and Anthropic `message_start`/`content_block_start`/`content_block_delta` text keyed `<msgid>:<index>`. Total limit 16 MiB (490). |
| — | reasoning | both | Sends `include:["reasoning.encrypted_content"]`. The returned reasoning items (id, summary, encrypted_content) are appended verbatim and resent verbatim under store:false. Nothing drops reasoning items that lack encrypted content (**Unconfirmed** whether Codex ever returns them). |
| — | state | — | Stateless: no previous_response_id or conversation id. The only per-group state is the auth binding, pinned across retries and checked against the account and revision. |

### 2.3 Registered Responses adapter (`RegisteredResponsesCodingProvider`, identity is the provider id, e.g. Paper)

| file:line | surface | dir | notes |
|---|---|---|---|
| main.cpp:538-543 | construction | — | Used for any `--provider` other than openai, openrouter and anthropic. `BLACKBIRD_RESPONSES_CURL` overrides curl. |
| responses_provider.cpp:90-106 | begin_request_group | reject | Throws `busy` on reentry. Binding protocol must be `responses` or `responses-portable`, else `unsupported`, so builtin kimi, mimo, grok and moonshot are refused here. |
| :117-157 | respond | send | Requires a timeout in [1,3600]. Sends `auth.request(provider,"responses",export_registered_responses_request(req, portable))`, then `registered_responses_completed_response`. |
| :18-22 | gates | reject | Requires `store===false` and `stream===true`. A non-null `previous_response_id` is `unsupported`. |
| :24-29 | portable effort | reject | `reasoning.effort` must be low, high or max. The engine default "medium" is refused (main.cpp:493-494 marks effort pending). |
| :30-34 | portable include | transform | Any include other than `reasoning.encrypted_content` is `unsupported`. `include` is then replaced with `[]`. |
| :37-41 | portable tools | transform | A `function` tool without `strict` gets `strict:false`, because Paper normalises omitted strict to true. |
| :42-49 | portable input | reject/transform | Any item with a non-empty `encrypted_content` is `unsupported`. `role:"developer"` is rewritten to `"system"`. Other kinds pass through. |
| :52 | export | transform | `export_provider_request` (see §1). |
| :54-86 | response checks | R | `completed_response`, then a non-null top-level `error` is `external_unknown`. For each `function_call`/`custom_tool_call`: `status` other than "completed" is `incomplete`; empty call_id or name, or a duplicate call_id, is `corrupt`; `function_call.arguments` must parse to an object. |
| — | non-portable `responses` | — | No transforms beyond `export_provider_request`. include and encrypted reasoning pass through. |
| — | cache | — | `supports_prompt_cache_policy` is false (base default), so any non-default cache mode is `unsupported`. |
| — | errors | — | HTTP status is typed via `require_provider_success` (provider_auth.cpp:422-429): 401 and 403 give `provider_auth`, 429 gives `provider_rate_limit`, other non-2xx gives `external_unknown(status)`. |

### 2.4 OpenRouter Responses adapter (`OpenRouterCodingProvider`, identity "openrouter-responses")

| file:line | surface | dir | notes |
|---|---|---|---|
| openrouter.cpp:36-40 | ctor | — | Forces `openrouter_metadata=true` and `openrouter_no_response_cache=true`. |
| main.cpp:528-532 | construction | — | `BLACKBIRD_OPENROUTER_CURL` overrides curl. |
| :41-59 | begin_request_group | reject | The model must be 1-256 characters and contain '/', else `invalid_range`. The binding base_url must equal `https://openrouter.ai/api/v1`, else `conflict`. The binding protocol is **not** checked: the builtin descriptor says `chat` (provider_auth.cpp:312-313). |
| :62-66 | gates | reject | Requires `store===false`. A non-null `previous_response_id` is `unsupported`. Stream is not checked here; the engine enforces it. |
| :67-73 | provider routing | transform | `provider.require_parameters` is forced to true. An explicit non-true value is `unsupported`. |
| :74-76 | session_id | reject | If present, must be 1-256 characters. A non-string throws `bad_variant_access` (not mapped to an Error). |
| :85-101 | assistant items | transform | For each `role=="assistant"` item, adds a missing `id="blackbird_view_<ordinal>"`, `status="completed"` and `type="message"`. String content becomes `[{type:"output_text",text}]`. Other items are untouched. |
| :102 | export | transform | `export_provider_request`. |
| provider_auth.cpp:1541-1544 | headers | W | `X-OpenRouter-Metadata: enabled`, `X-OpenRouter-Cache: false`, `Authorization: Bearer`. |
| :104-211 | SSE parse | R | Requires a `[DONE]` sentinel exactly once, after `response.completed`, else `incomplete`. Data after `[DONE]` is `corrupt` (115-123). Event `error`, `response.error` or `response.failed`, or an untyped event with an `error`, goes to `stream_error` (126-128). `response.incomplete` is `incomplete`. An `openrouter_metadata` field must be consistent, else `conflict` (131-134). An untyped metadata event is accepted only after completion as a single "tail" with no output (135-143). A second completion or an untyped event is `corrupt` (145-146). Typed events are re-serialised into a canonical stream and passed to `completed_response` (173). After that: a top-level `error` or `error_type` goes to `stream_error`; call items are checked (status completed, unique call_id, name, string arguments) (179-196). Writes back `openrouter_metadata` and `openrouter_tail` on the response object, not on items (197-204). |
| :25-34 | `stream_error` | — | `error_type` of authentication, permission_denied or payment_required gives `provider_auth`. Everything else gives `external_unknown` (detail 0, not retried). |
| — | reasoning | both | `include:["reasoning.encrypted_content"]` passes through. Reasoning items are sent and received verbatim. **Unconfirmed** whether OpenRouter returns encrypted content for non-OpenAI models. |
| — | cache | — | Policy supported only for model `openai/gpt-6.1-sol`. Uses `prompt_cache_options` and `prompt_cache_breakpoint`, not `cache_control`. |

### 2.5 Anthropic Messages adapter (`AnthropicCodingProvider`, identity "anthropic")

| file:line | surface | dir | notes |
|---|---|---|---|
| anthropic_provider.hpp:5-9 | config | — | timeout 180s, `max_tokens` default 16384. |
| anthropic_provider.hpp:15 | `retain_request_state()` | — | true: the developer state item is persisted into context (coding.cpp:4441-4445). |
| main.cpp:533-537 | construction | — | Only when `--provider anthropic`. `BLACKBIRD_ANTHROPIC_CURL` overrides curl. |
| :572-591 | begin_request_group | reject | Throws `busy` on reentry. Binding protocol must be `messages`, else `unsupported`. Model must be 1-256 characters. (It never calls `phase.finish`, but `run` does that.) |
| :602-639 | respond | send | Calls `export_anthropic_request(req, max_tokens, kind=="oauth")`, sends `auth.request("anthropic","messages",…)` to `https://api.anthropic.com/v1/messages`, then `anthropic_completed_response`. |
| :302-310 | top-level allow-list | reject | Allowed: model, instructions, input, tools, reasoning, include, store, stream, parallel_tool_calls, max_output_tokens, previous_response_id, temperature, top_p, stop, tool_choice. Anything else (metadata, provider, session_id, prompt_cache_options, text, `blackbird_cache_policy`…) is `unsupported`. |
| :311-315 | gates | reject | Requires store false and stream true. A non-null previous_response_id is `unsupported`. |
| :316-319 | max_tokens | W | Request `max_output_tokens` overrides the config value. 0 is `invalid_range`. |
| :289-299,322 | `validate_anthropic_route` | reject | Effort must be low, medium, high, xhigh or max. `xhigh` is refused for `claude-sonnet-4-6*` and `claude-opus-4-6*`. `xhigh` and `max` are refused for `claude-opus-4-5*`. Also used by main.cpp:475-486 to filter offered effort levels. |
| :323-327 | reasoning keys | reject | Only `effort` is allowed. |
| :328-331 | include | reject | Only `reasoning.encrypted_content` is allowed. It is then ignored. |
| :332-343 | tools | transform | `type` must be "function", else `unsupported`. Produces `{name, input_schema:parameters, description?, strict?}`. |
| :344-358 | `append` merge rule | transform | A new message starts when the role changes, or when the role is assistant and `anthropic_message_id` differs from the last group. So consecutive user-side blocks (tool_results, user text, developer and system text) merge into one `user` message. Consecutive assistant items with the same group (an empty group counts as equal) merge into one `assistant` message. |
| :364-380 | retained-native consistency | reject | Items with `anthropic_content` need a non-empty group. Kind must match: `text` needs role assistant and type message (or empty); `tool_use` needs type function_call and no role; thinking or redacted_thinking needs type reasoning. A text message with array content must have exactly one part. Otherwise `conflict`. |
| :57-67 | `native_block` | transform | If `anthropic_content` exists, every projected field must equal the original, else `conflict`. The original block is then sent verbatim, preserving citations and other extra fields. |
| :381-395 | function_call → `tool_use` | transform | A new call while others are pending is allowed only if no results have started and the group is the same, else `conflict`. call_id must be non-empty and unique, name non-empty, and `arguments` must be an object (a string arguments value throws). Produces `{type:tool_use,id:call_id,name,input:arguments}` in the assistant role. |
| :396-411 | function_call_output → `tool_result` | transform | Must match a pending call, else `conflict`. `blackbird_output_parts` is `unsupported`, so there are no image or file results. `content` is the string output, or the output dumped as JSON. `is_error:true` if output is an object with a non-null `error`. Placed in the user role. |
| :412-419 | reasoning → thinking | transform/reject | Same pending and group rule as tool calls. Requires `anthropic_content` and a group, else `unsupported`: OpenAI encrypted reasoning items cannot be sent to Anthropic. `validate_thinking` (41-52): `thinking` needs a non-empty `signature` and `redacted_thinking` needs non-empty `data`, else `incomplete`. The block is sent verbatim. |
| :420-446 | message → text | transform | While calls are pending, only assistant messages in the same group with no results started are allowed, else `conflict`. Role must be assistant, user, developer or system, else `unsupported`. String content: developer and system get the prefix `"[Blackbird <role> guidance]\n"` and are sent as `user`. Array content: parts must be input_text, output_text or refusal (`refusal` text taken from `.refusal`). Each part becomes its own text block, and **developer/system array content gets no prefix**. Images are `unsupported`. Other item types (e.g. `custom_tool_call`) are `unsupported` (447-448). |
| :450-452 | final structure | reject | Any pending call, empty messages, or a first message not from `user` is `conflict`. |
| :453-456 | system | W | `system = instructions` as a plain string with no blocks. With OAuth it is prefixed `"You are Claude Code, Anthropic's official CLI for Claude.\n"`. |
| :457-465 | body | W | `{model, system, messages, tools, max_tokens, stream:true, tool_choice:{type:"auto",disable_parallel_tool_use:true}}`. |
| :466-470 | sampling | W | `temperature` and `top_p` are copied. `stop` becomes `stop_sequences`. |
| :471-478 | parallel_tool_calls | W | Must be a bool. Sets `disable_parallel_tool_use = !value`. The engine always sends false, so the result is true. |
| :479-485 | tool_choice | W | Only the strings "auto" and "none" are allowed, else `unsupported`. Replaces `tool_choice.type`. |
| :486-510 | thinking | W | "Manual" families: prefixes `claude-sonnet-4-5`, `claude-haiku-4-5`, `claude-opus-4-5`, `claude-sonnet-4-2025`, `claude-opus-4-2025`, `claude-3-7-sonnet`. These get `thinking:{type:enabled,budget_tokens:min(desired,max_tokens-1)}` with low=1024, medium=4096, high=8192, xhigh=12288, max=16384; max_tokens ≤1024 is `invalid_range`. `claude-opus-4-5*` also gets `output_config:{effort}`. All other models get `thinking:{type:adaptive}` and `output_config:{effort}`. |
| — | cache | — | **No `cache_control` breakpoints are ever emitted.** `prepare_request_cache` refuses non-default modes for this identity. |
| provider_auth.cpp:1545-1556 | headers | W | `anthropic-version: 2023-06-01`. API key uses `x-api-key`; OAuth uses `Authorization: Bearer` plus `anthropic-beta: oauth-2025-04-20,claude-code-20250219`, `User-Agent: claude-cli/2.1.163 (external, cli)` and `x-app: cli`. |
| :513-569 | SSE parse | R | Parses `event:` and `data:` lines. A non-empty event name must equal `data.type`, else `corrupt` (536-537). A trailing partial event is `incomplete`. `bad_variant_access` is mapped to `corrupt` (566-567). |
| :71-201 | `MessageStream::event` | R | `error` gives `external_unknown`. `ping` is ignored. Unknown event types are ignored (84-85). `message_start`: type message, role assistant, non-empty id, empty content; usage is merged. `content_block_start`: indices must be sequential, otherwise `corrupt`. Block types text, thinking (text plus signature), redacted_thinking (validated), and tool_use (unique id, name, object input) are accepted; any other block type (server_tool_use, web_search_tool_result, …) is `unsupported`. Deltas: `input_json_delta` (tool_use), `text_delta`, `thinking_delta` and `signature_delta` are accepted; anything else (e.g. citations_delta) is `unsupported`. `content_block_stop`: tool_use input is parsed from the partial JSON, and a non-empty start input together with deltas is `conflict`. Thinking is validated, so a missing signature is `incomplete`. `message_delta`: stop_reason (must stay consistent) and usage. `message_stop` requires a preceding delta and a stop_reason. |
| :202-209 | stop reasons | R | Accepted: end_turn, tool_use, stop_sequence, refusal. Anything else (max_tokens, pause_turn, model_context_window_exceeded…) is `incomplete`. `tool_use` must occur exactly when calls exist, else `corrupt`. |
| :210-236 | output items | R | text becomes `{type:message,id:"<msgid>:<index>",role:assistant,content:[{type:output_text,text}]}` with no `status`. tool_use becomes `{type:function_call,call_id:block.id,name,arguments:<object>}` with no id or status. thinking and redacted_thinking become `{type:reasoning,summary:[]}` with no id and no encrypted_content. **Every** item also gets `anthropic_message_id:<msgid>` and `anthropic_content:<raw block>`. |
| :237-266 | usage | R | `input_tokens = input + cache_creation_input_tokens + cache_read_input_tokens`. `input_tokens_details.cached_tokens = cache_read_input_tokens` (only if present). `output_tokens` is passed through. `total_tokens = input + output` (only if both are present). No reasoning_tokens. Response is `{id, model, output, usage}`. |
| — | carry-forward | both | The raw thinking and redacted blocks (with signatures) ride in context as `anthropic_content` and are replayed verbatim in their original assistant group. The state item persists in context, so later requests replay it at its original position. |
| — | errors | — | HTTP status is typed via `require_provider_success`. 529 (overloaded) becomes `external_unknown 529`, which is **not** in the retry list (coding.cpp:4678-4681). The SSE `error` event (overloaded_error, …) is detail 0, so it is not retried either. |

### 2.6 Chat-Completions and other routes (MiMo, Grok, Moonshot, OpenRouter chat, Kimi): colleague path only

There is **no main-coding Chat-Completions adapter.** main.cpp:488-492 refuses any protocol other than `responses`, `responses-portable` or the anthropic `messages` route ("Main coding requires a Responses provider or native Anthropic Messages"). `RegisteredResponsesCodingProvider` also refuses chat and messages (responses_provider.cpp:100-102). Chat is used only by `native_colleague_transport`.

| file:line | surface | dir | notes |
|---|---|---|---|
| colleague.cpp:171-213 | `prepare_colleague` validation | reject | Allowed keys only. Request ≤65536 bytes (184-185). Profile must have `tools:"none"` and `requests:1` (197-199). Context entries `{id,text}` with unique ids. |
| :236-241 | instructions | W | Fixed text "You are an independent colleague…". |
| :246-259 | openai upstream | W | `{model,instructions,input:[{role:user,content:prompt}],tools:[],store:false,stream:true}`. No reasoning or include. |
| :265-273 | responses/portable upstream | W | Same shape as the openai upstream. It does not go through `export_registered_responses_request`; it is sent raw (dump_json of body at provider_auth.cpp:1533). |
| :274-285 | messages upstream (anthropic, kimi) | W | `{model,system,messages:[user],max_tokens:8192,stream:false}`. The Anthropic OAuth system prefix is added. |
| :286-295 | chat upstream (mimo, grok, moonshot, openrouter) | W | `{model,messages:[{role:system,content:instr},{role:user,content:prompt}],stream:false}`. No tools, max_tokens or temperature. |
| :472-477 | route select | — | responses or responses-portable go to `responses`, messages to `messages`, anything else to `chat/completions`. |
| :486-487 | responses decode | R | `completed_response`. |
| :496-510 | messages decode | R | type must be message. stop_reason must be end_turn or stop_sequence, else `incomplete`. thinking and redacted_thinking are dropped. Only text blocks are allowed, else `unsupported`. |
| :511-529 | chat decode | R | `choices` must have exactly one element, else `corrupt`. `finish_reason` must be "stop", else `incomplete`. `tool_calls` present is `unsupported`. A non-empty `refusal` string becomes a refusal part; otherwise `content` becomes output_text. |
| :530-538 | normalised result | R | `{status:completed,model,usage:<raw>,output:[{type:message,role:assistant,status:completed,content}]}`. Usage is not normalised. |
| :135-163 | `openai_text` / refusal | R | Skips reasoning items. Items must be assistant messages with status completed, and parts output_text or refusal. |
| :541-555 | openai via Codex compatibility | send | `codex_login` then `openai_http("responses")`. |

### 2.7 Provider registry, auth and transport (provider_auth.cpp, main.cpp)

| file:line | surface | notes |
|---|---|---|
| provider_auth.cpp:271-315 | `builtin(id)` | openai: `https://api.openai.com/v1`, responses, `OPENAI_API_KEY`, browser login, client `dynamic_agent_client`, token URL `https://auth.openai.com/api/accounts/oauth/token`, scope `openid profile email offline_access resource.invoke chatgpt.tokens.use.direct`. anthropic: `https://api.anthropic.com/v1`, messages, `ANTHROPIC_API_KEY`, browser, client `9d1c250a-e61b-44d9-88ed-5944d1962f5e`, token URL `https://console.anthropic.com/v1/oauth/token`, scope `org:create_api_key user:profile user:inference`. kimi: `https://api.kimi.com/coding/v1`, messages, `KIMI_API_KEY`, device flow, client `17e5f671-…`, `https://auth.kimi.com/api/oauth/{device_authorization,token}`. mimo: `https://api.xiaomimimo.com/v1`, chat, `MIMO_API_KEY`. grok: `https://api.x.ai/v1`, chat, `XAI_API_KEY`, device flow, client `b1a00492-…`, `https://auth.x.ai/oauth2/{device/code,token}`, scope `openid profile email offline_access grok-cli:access api:access`. moonshot: `https://api.moonshot.ai/v1`, chat, `MOONSHOT_API_KEY`. openrouter: `https://openrouter.ai/api/v1`, **chat**, `OPENROUTER_API_KEY`. |
| :316-328 | `descriptor` | A builtin wins; otherwise the stored `providers[id]`. |
| :661-693 | `catalog` | The 7 builtins in fixed order, plus stored providers. |
| :985-1040 | `add_provider` (CLI `add-provider ID BASE PROTOCOL ENV`, provider_oauth.cpp:851-852) | id and env must be identifiers. base must be https and ≤2048 characters (71-76); trailing '/' is stripped. Protocol must be responses, responses-portable, chat or messages. A builtin id cannot be shadowed, the id must not already exist, and there can be at most 32 registered providers. Stored as `{id,base_url,protocol,environment,login:"none"}`. |
| :133-200, 624-633 | store | `credentials.bbm` (packet format, ≤1 MiB, :28) under `$BLACKBIRD_AUTH_HOME` or `~/.local/share/blackbird/auth`. Guarded by flock `lock` with a 30s wait. Holds providers, accounts (≤128), active, selection_revision and pending. No providers.json or toml registry exists. |
| :785-826 | `binding` | `{provider,account,revision,kind (api_key\|oauth\|environment\|logged_out),base_url,protocol}`. |
| :1345-1588 | `ProviderAuth::request` | Routes: responses, chat/completions, messages, models (GET), else `invalid_range`. The expected binding must match provider, account, revision, base, protocol and kind, else `conflict` (1382-1399). An OAuth token expiring within 60s is refreshed (1415). The refresh body adds `&resource=https%3A%2F%2Fapi.openai.com%2Fv1` for openai (1434-1435); for anthropic the refresh body is JSON (1436-1442). A refresh answered with 429 or ≥500 sets `refresh_after = now+60` and throws `provider_rate_limit(-status)`, a negative detail that is not retried. Other non-2xx sets the account to `reauthorize` and throws `provider_auth`. With no active account, the token comes from the environment variable (1495-1501). Target is `base+"/"+route`. Body limit 16 MiB (463). |
| :410-420 | oauth headers | Form content type and Accept JSON. Kimi adds `X-Msh-Platform: kimi_cli` and `X-Msh-Version: blackbird-0.1`. |
| :1557-1558 | kimi messages | `User-Agent: KimiCLI/blackbird-0.1`. |
| :512-514 | curl | `--disable --silent --no-buffer --proto =https --max-time T --write-out "\n%{http_code}" --config -`. Response limit 16 MiB (528). |
| :422-429 | `require_provider_success` | 401 and 403 give `provider_auth`; 429 gives `provider_rate_limit`; other non-2xx gives `external_unknown(status)`. |
| main.cpp:91,95,96 | defaults | model `gpt-6.1-sol`, effort `medium`, provider `openai`. |
| main.cpp:448-453 | provider switch | Changing the provider requires `--model`. |
| main.cpp:466-496 | `route_efforts` | openai and openrouter offer all 5 levels. anthropic+messages is filtered by `validate_anthropic_route`. Any other protocol that is not responses is refused. responses-portable offers `{low,high,max}`. |
| main.cpp:527-545 | factory | openrouter gives OpenRouterCodingProvider; anthropic gives AnthropicCodingProvider; any other non-openai value gives RegisteredResponsesCodingProvider; openai gives OpenAiCodingProvider. |
| src/provider_original.cpp | raw capture only | Phase, lease and original byte capture for audit. No item conversion. The site table is at :431-466. |
| decision_models.cpp:328 | Jev (not a coding provider) | `https://api.typesafe.ai/v1/systemone`. Out of scope. |

### 2.8 Cross-adapter findings and risks

1. **Switching providers with reasoning in context fails.** Anthropic-origin reasoning sent to any Responses route throws `unsupported` (json.cpp:457-459). OpenAI-origin encrypted reasoning sent to Anthropic throws `unsupported` (anthropic_provider.cpp:415-417). A portable registered route refuses any non-empty `encrypted_content` (responses_provider.cpp:44-46). The context must be edited first. Anthropic `text` and `function_call` items do cross over, with the anthropic_* fields stripped. Their ids (`msg_…:N`) go to Responses unchanged: **Unconfirmed** whether OpenAI or OpenRouter accept them (OpenRouter only synthesises ids where none exist).
2. **Anthropic developer/system messages with array content are sent as user text without the `[Blackbird … guidance]` prefix** (anthropic_provider.cpp:435-446 vs 429-434).
3. **Anthropic HTTP 529 and SSE `overloaded_error` are not retried** (coding.cpp:4678-4681).
4. No context-overflow classification on any route. An HTTP 400 is `external_unknown 400`, which is terminal. Anthropic `model_context_window_exceeded` and `max_tokens` stops are `incomplete`.
5. The Codex compatibility route never refreshes on 401 (coding.cpp:388 `refresh=false`), and its 401, 403 and 429 are untyped `external_unknown`.
6. Anthropic gets no prompt caching at all: no `cache_control` and no automatic breakpoints. OpenAI caching is a single explicit breakpoint, and only for `gpt-6.1-sol` (and `openai/gpt-6.1-sol` on OpenRouter).
7. `custom_tool_call` is validated in the registered and OpenRouter parsers but never declared, dispatched or converted. Anthropic rejects it. `validate_protocol` ignores it.
8. The OpenRouter builtin protocol is `chat`, but main coding posts to `/responses` without checking protocol. Only the base_url is checked.
9. The portable route refuses effort `medium`, which is the engine and CLI default.
10. Only Anthropic persists the operational-state developer item into context, so the item accumulates there and becomes part of the history. Other providers send a fresh state item at the tail of every request.
11. `completed_response` treats any SSE event after `response.completed` as `corrupt`, and a `[DONE]` payload as a JSON error. This is relevant to registered routes, which do not strip `[DONE]` the way OpenRouter does: **Unconfirmed** whether Paper emits `[DONE]`.

## 3. Tool dispatch and how tool results become context items

### 3.1 Dispatch path (Lua to native to Lua to context)

| file:line | surface | dir | notes |
|---|---|---|---|
| programs/turn.lua:12-26 | default workflow arg injection | transform | Sets `args.output_max_bytes = 16384` only when it is nil, and only for `exec` and for `read_file` with no range/byte_*/line_* arguments. This is the only place the "16 KiB default" exists; no C++ code applies it. |
| programs/turn.lua:27 | `blackbird.call(item.name, args)` | send | The model's function_call `arguments` arrive already parsed (json.cpp:440-449). |
| programs/turn.lua:28-34 | `blackbird.append({{type="function_call_output", call_id, output=result}})` | W | `output` is the **structured Lua table/Value**, not a string. Nothing is encoded or truncated here. |
| programs/useful_turn.lua:24-27, programs/economy_pilot.lua:75-85, src/workflows.cpp:293-298 (`ultracode` built-in) | other turn loops | W | **No output_max_bytes injection**: exec can return up to the resident tail (1 MiB) and read_file the whole file (16 MiB or less). economy_pilot wraps a pcall error as `{error=tostring, effect_outcome="unknown"}`. |
| src/coding.cpp:1057-1065 | Lua `blackbird.*` bridge names | — | request, call, context, edit, originals, restore, append, decode, encode, display, array, present, restarting, stats, manage, inspect, decode/encode_packet, format |
| src/coding.cpp:1633-1637 | bridge `call_op` | send | `engine.call(name, read_value(args))`. The returned Value is pushed to Lua by `push_value` (coding.cpp:81). On a thrown Error the bridge raises a Lua error (coding.cpp:1727-1767). |
| src/coding.cpp:1658-1662 | bridge `append_op` | W | `context_.append(items, "lua.synthetic")`, then returns the **entire context view** to Lua (copy cost on every append). |
| src/coding.cpp:4815-4817 | `CodingEngine::call` | — | Refuses `provider`. Wraps every tool in `operation(name, args, body-lambda, …)`. The lambda contains the per-tool branches (§3.2). |
| src/coding.cpp:3158 | `CodingEngine::operation` | — | Common envelope for every tool, and also for `provider` (4560) and `workflow_execute` (4146). |
| src/coding.cpp:3197 | `poll_commands()` | R/W | Drains job output into durable originals before each operation (§3.4). |
| src/coding.cpp:3207-3208 | `effect_policy(name,input)` | — | Optional hook. Only set in src/backstop.cpp:504, where it gates `provider` calls. There is no tool-output shaping hook. |
| src/coding.cpp:3228-3238 | `status(description)` | UI | name plus the first of path/command/argv |
| src/coding.cpp:3243-3291 | `encode_original` | W | bbm2 `encode_packet(value)` under `ValueLimits{}` (16 MiB / 500k nodes / depth 128; value.hpp:36-40). Raw-traced. |
| src/coding.cpp:3298-3305 | identities | — | decision, invocation, generation, attempt. `exec_id`/`output_ref` = hex(attempt). |
| src/coding.cpp:3310, 3346-3360 | **input original** | W durable | The encoded input goes into the Invocation and AttemptAdmission events. For `provider` the input is the **whole request, including every context item**, so it is re-retained on each provider attempt. **Unconfirmed**: whether the journal shares the ImmutableBytes buffer the two events reference, or stores it twice. |
| src/coding.cpp:3440-3585 | `prepare` lambda | transform | **Single per-result finalizer.** An error becomes `error_value` = `{error,detail}` (coding.cpp:269). Adds `effect_outcome:"applied"` for file effects. Adds help `message`s for program_config (3448-3464), read_file invalid_range (3465-3486, with `example`) and tasks (3487-3501). Adds command/domain refusal fields (3509-3547). exec: adds `output_ref`, plus `timed_out`/`effect_outcome:"unknown"` (3553-3560). Then **encodes the full result into `output` (bbm2)** at 3561-3584. |
| src/coding.cpp:3613-3695 | `Boundary::dispatch` | — | Calls the tool body (3640) and catches exceptions into `error`. On allocation failure after a file effect there is one fallback presentation (3664-3677). |
| src/coding.cpp:3777 | `root.dispatch(attempt, boundary, …)` | — | Journal dispatch fence. |
| src/coding.cpp:3894-3902 | **`operation.result` original** | W durable | `retain(0, {"operation.result", output(bbm2), {attempt, source_encoding:"bbm2", operation}})`. This is the full presented result Value, i.e. after prepare, **not** the raw process/file bytes. |
| src/coding.cpp:3903-3925, 3926-3944 | capacity fallback / `capacity.stop` | W | On capture refusal, a bounded error result plus a message original. |
| src/coding.cpp:3947-3951, 4005-4045 | terminal AttemptObservation | W durable | Carries the `{result_id,result_record}` locator, not the result again. Persistent exec jobs skip this; it happens later in poll_commands. |
| src/coding.cpp:4079-4090 | `process_output` error banner | UI only | `"\n[name: error]…"`. Goes to the TUI (main.cpp:676), not to context. |
| src/coding.cpp:4100-4105 | rethrow | — | Rethrows for capacity (non-adapter), interrupted, provider, workflow_execute and a non-live journal. Every other error is **returned as a Value**, so it becomes a function_call_output. |
| src/coding.cpp:4111 | `return result` | R to Lua | The **structured Value** returns to Lua. The bbm2 `output` string is used only for the audit record. |
| src/context.cpp:927-1001 | `ContextStore::append_impl` | W durable | Each item becomes `{id: revision.i, item}`. The packet `{op:append, origin:label, format:append-delta-v1, originals:[…]}` is recorded on the context channel (982). The full item, including the tool output, **is stored again here**. |
| src/coding.cpp:4277, 4320 | request assembly | send | `input = context_.items()` verbatim. There is no request-time shaping apart from the operational-state item (§3.6). |
| src/json.cpp:453-506 | `export_provider_request` (OpenAI/Responses/OpenRouter: openai.cpp:364/389, responses_provider.cpp:52, openrouter.cpp:102) | send/transform | A non-string `output` becomes **`dump_json(value)` (496)**. With `blackbird_output_parts:true` an explicit parts array (input_text/image/file) passes through (469-488) and the marker is stripped (502-504). |
| src/anthropic_provider.cpp:396-406 | Anthropic tool_result | send/transform | A string is kept as is; anything else is dumped with `dump_json`. Sets `is_error` when `output.error` is non-null. Explicit parts are unsupported here. |
| src/request_cache.cpp:22-70 | cache breakpoints | send/transform | `mark_item` turns a string `output` into an `[input_text]` part to place `prompt_cache_breakpoint`. |

**Output string answer:** in context, a tool result is a **structured Value under `output`**. It becomes a string only at provider export, through `dump_json`, with no truncation at that point. All size limiting happens upstream, inside tool presentation functions, and only when the Lua program passes `output_max_bytes` (turn.lua does this only for exec and whole-file read_file).

### 3.2 Per-tool catalogue

| file:line | tool | result shape | limits / truncation | originals / handles | notes |
|---|---|---|---|---|---|
| coding.cpp:4826-4863; command_jobs.cpp:1363-1490, 472-524 | `exec` | `CommandJobs::wait` returns `Impl::snapshot`: job_id, output_ref, state, pty, launches, received_bytes, retained_bytes, sealed, native_custody_pending, leader_exited, output_closed, pid; terminal fields once retained (termination, signal, output_bytes, capture_complete, timed_out, stop_requested, effect_outcome, exit_code, error, error_detail, retained_bytes: command_jobs.cpp:2244-2264, coding.cpp:2735); output, returned_bytes, omitted_bytes, byte_start, next_offset, omitted_prefix_bytes | `max_output` = `output_max_bytes` (coding.cpp:4843-4845). The default is 16 MiB (command_jobs.hpp:18), capped at 16 MiB (command_jobs.cpp:116/248). It is a **prefix only**, starting at `max(0, tail.front.offset)`. The resident tail is evicted above `pending_limit` = 1 MiB (command_jobs.cpp:38, 962-965). Total capture is capped at 16 MiB, beyond which EFBIG stops the job (950-954). No head+tail form and no inline marker; only the counts. | Chunks become `process.output` originals in poll_commands (coding.cpp:2668-2671, metadata attempt and byte_start). `output_ref` = attempt hex. | timeout_seconds 1..3600, default 120 (tools.cpp:325-333; **Unconfirmed** that the jobs path uses the same default: see Launch parsing near command_jobs.cpp:230-252). yield_ms gives a background result without exit_code. A later completion is **never appended to context**; the model must use process read/foreground. |
| coding.cpp:4864-4874; command_jobs.cpp:1792-1830 | `process` | foreground: same snapshot as exec. read: `CommandJobs::read`. With no job_id it returns a list of snapshots without output; with job_id it returns a snapshot plus an output window (1700-1790). Other ops return control values. | **foreground ignores `output_max_bytes`**: the call does not set `options.max_output` (coding.cpp:4866-4871) even though validation accepts the key (command_jobs.cpp:95). The effective cap is the 16 MiB default, so up to the 1 MiB resident tail. read: `count` default and maximum 65536, `offset` absolute (1704, 1746). | — | poll_commands also calls `call("process", request)` for UI mailbox requests (coding.cpp:2646-2647). Those are audited but **not appended** to context. |
| coding.cpp:5033-5122 | `read_process_output` | `process_output_presentation`: output, output_bytes, returned_bytes, omitted_bytes (tools.cpp:229-235) | **No output_max_bytes.** Takes an optional byte_start/byte_end only; line_* is refused (5087-5088). With no range it returns the **whole original, up to 16 MiB**. | Lookup is a **linear scan of every audit fact, decoding each log packet** (5045-5083) to find `process.output` records whose attempt equals the ref, then concatenating their source bytes. | Retired jobs stay readable. O(audit) cost on every read. |
| coding.cpp:5535-5566; tools.cpp:247-261, 208-228 | `read_file` (path) | `{content}`; when output_max_bytes is given, also file_bytes, selected_bytes, returned_bytes, omitted_bytes; plus `snapshot:{journal,sequence}` (5557-5562) | `original_limit_` 16 MiB (tools.hpp:19); a larger file throws capacity (tools.cpp:22-23). range {mode lines/bytes,start,end} or legacy fields (134-206). The budget is applied by `selected.resize(budget)` (214-215), a **prefix**, with no marker. | `file.read` original holds the **whole file** (tools.cpp:259, observer at coding.cpp:5536-5551). The snapshot is its source reference. | Error help message and example come from prepare (3465-3486). |
| coding.cpp:5514-5534 | `read_file` (snapshot) | same presentation plus the echoed `snapshot` | same budget/range | Reads `log_.root().source({journal,sequence})`. Any source ref is accepted; there is no label check (**Unconfirmed** whether that is intended). | No disk access. |
| tools.cpp:262-320 | `write_file` / `edit_file` | `{written:true, path, effect_outcome:"applied"}` | content at most 16 MiB (297, 289) | `file.before` (276/308) and `file.proposed` (316) originals. The input original also holds the full content/old/new. | Small result. LocalTools `captured_` keeps an in-memory copy (tools.cpp:103) for the duration of the call. |
| — | `apply_patch` | — | — | — | **Does not exist** (no match in src/include/programs). |
| coding.cpp:4911-4915 | `workflow_registry` | `workflows_->discover` | none observed | — | |
| coding.cpp:4916-4978; 4215-4246 | `workflow_invoke` | With open tool calls: `{status:"accepted",name,revision,execution}`. Otherwise `execute_workflow` returns the Lua result. | none | `workflow.selection` and `workflow.source` originals (4937, 4174) | The parked result later becomes a **native developer append** (§3.6). |
| coding.cpp:4987-5011, 1814-1834 | Lua-defined tools (`tool_define`) | whatever the Lua body returns | **none** | `tool.source` original | source at most 16384 (coding.cpp:2381) |
| coding.cpp:5512 | `lua` | `runtime_->eval(code)` return | **none** | input original | |
| coding.cpp:4875-4909 | `decision_model`, `beads` | adapter Value plus `audit_ref` | **Unconfirmed** (adapters not read) | labeled raw originals per attempt (4879, 4896) | |
| coding.cpp:5012-5032 | `restart` | `{scheduled:true}` | note at most 65536 | — | |
| coding.cpp:5123-5162 | `context_repair` | `{repaired,base,effect_outcome,replayed}` | — | — | **Native append** of placeholder outputs (§3.6) |
| coding.cpp:5163-5173 | `context_budget` / `context_manage` | policy / manage result | — | context channel | |
| coding.cpp:5497-5511; context.cpp:707-751, 1109-1124, 1686-1760 | `context_inspect`, `context_stats`, `context_view`, `context_edit`, `context_originals`, `context_restore` | inspect: a page bounded by `limit` (default 4096, maximum 65536). view, restore and originals return the **entire context / all originals**. stats lists per-entry byte sizes. | view, originals and restore are **unbounded** and copy the whole context into a tool result. | — | |
| coding.cpp:5174-5407 | `rageshake` | `{complaint,retained_locally,bead_created,delivery,sink,repair_required_now}` | observation and references at most 65536 | `complaint.detail` and `complaint.delivery` originals | Internally calls `call("exec", … output_max_bytes:4096)` (5351-5359), which is a nested operation. |
| coding.cpp:5408-5413 | `audit_inspect`, `trajectory_read`, `variables_read` | AuditLog pages | count 1..64, scan at most 256, byte limit 1..65536 (tool descriptions in tools.cpp:1042-1145) | — | |
| coding.cpp:5414-5421; observations.cpp:132-160 | `git_observe` | head plus history | history 0..64 | git.head/git.commit samples | |
| coding.cpp:5422-5464; participants.cpp:82-94, 1428-1458 | `participant_*` | rows. A result that encodes to more than 65536 is projected to `{status,remote_disposition,text[:8192],details:"Read retained participant result original; projection truncated"}`. read with no run_id drops `result`. | **the only existing truncate-with-marker shaping** | result original kept per run | |
| coding.cpp:5469-5491; colleague.cpp:320-450 | `colleague` | `{status,remote_disposition,text,…}` | request at most 64 KiB; **reply text unbounded** | `colleague.*` originals | |
| coding.cpp:5492-5496 | `tasks_read/edit` | pages | page 64, title 512, notes 2048 | — | |
| tools.cpp:106-118 | `display_bytes` (all byte presenters) | a string if valid UTF-8, else `{encoding:"hex",bytes}` | — | — | **The budget cut is byte-exact** (tools.cpp:215, command_jobs.cpp:511-512). If it splits a multibyte char, the strict `valid_utf8` check (value.cpp:187-215) fails and the **whole slice becomes hex (2x size)**. Raw PTY escape sequences are not stripped. |

### 3.3 Durable copies of one exec's output bytes

1. A `process.output` source original per chunk (coding.cpp:2668). This is the raw truth.
2. `operation.result` bbm2 (coding.cpp:3896), containing the presented slice.
3. The context `append` packet `originals` (context.cpp:957-982), the same slice again.
4. Every later `provider` operation's input original holds the whole request input (coding.cpp:3310 via 4560), so the slice is retained again on each request (**Unconfirmed** dedupe).

For read_file, (1) is replaced by `file.read`, the whole file, and the model sees a prefix or range of it.

### 3.4 Command jobs, poll and retention

| file:line | surface | dir | notes |
|---|---|---|---|
| command_jobs.cpp:930-980 | worker read loop | W mem | Each chunk is pushed to `pending` and to `tail`. The tail is trimmed to `pending_limit` (1 MiB) but always keeps at least 1 chunk. |
| command_jobs.cpp:2215-2272 | `drain()` | R | Returns at most 64 pending chunks per job per round, plus a terminal result once finished. |
| coding.cpp:2636-2795 | `poll_commands` | W durable | Each chunk becomes a `log_.original("process.output", bytes, {attempt, byte_start})`, then `retained(id,end)`. The terminal result plus retained_bytes becomes a settlement AttemptObservation (2737-2756). UI echo goes through `process_output` (2681-2688). **No context append.** It runs up to 4 drain rounds and is called at every operation start (3197) and in every wait tick (4861). |
| command_jobs.cpp:1640-1698; command_observation.hpp | `try_observe` | R (UI) | A no-allocation projection for the TUI: 8 rows, a 4096 B window, 16 chunk sources with provenance. Not model-visible. |
| command_jobs.cpp:421, 1258-1300 | registry | — | max_live 8 (up to 64) and max_records 32 (up to 256). The oldest settled job is retired; its output_ref stays readable through read_process_output. |
| src/command_job_runner.cpp | PTY launcher | — | Only does setsid, sets the controlling tty, and calls execvp. No output handling. |
| tools.cpp:323-397 | `LocalTools` exec | — | **Not reached from CodingEngine::call**, which returns at 4860. It is used by the colleague CLI transport (colleague.cpp:556-564) and by WorkflowRuntime (workflow_runtime.cpp:116, 1 MiB limit). It applies a prefix `output.substr(0,budget)` (386). The `[exit N]` echo at coding.cpp:5563-5565 is dead code for exec. |

### 3.5 Existing shaping at creation

- turn.lua:13-25: default `output_max_bytes` 16384 (exec and whole-file read_file only).
- exec and process snapshot: prefix bytes plus `returned/omitted/omitted_prefix_bytes`, `next_offset`, `output_ref`. No inline elision marker and no tail.
- read_file: prefix by budget plus counts (only when a budget is given), `snapshot` handle, range selector.
- process read: a 65536 window; context_inspect: a 4096/65536 page; audit, trajectory and git pages capped at 64.
- participants: a 64 KiB projection that keeps 8192 bytes of text plus a `details` marker.
- `display_bytes`: hex fallback.
- prepare help text: read_file, program_config, tasks messages and refusal fields.
- Instructions text (coding.cpp:4286-4288) tells the model to "inspect omission counts and recover needed facts from captured originals".

### 3.6 Context writes that bypass the Lua function_call_output append

| file:line | label | item |
|---|---|---|
| coding.cpp:2619 | `initial` | empty |
| coding.cpp:5960-5964 | `operator` | user prompt |
| coding.cpp:4442 | `provider.request-state` | developer `CURRENT_TASKS…`/`CONTEXT_SIZE…`/`CONTEXT_BUDGET…` text. Only when `provider_.retain_request_state()`; otherwise it is added to the request only (4446-4450). |
| coding.cpp:4803 | `provider.branch:<rev>` | response output items (function_call, message, reasoning) |
| coding.cpp:4238-4244 | `workflow-continuation` | developer item `"Registered workflow X completed in this turn: " + format_value(result)`. The result is **unbounded**. |
| coding.cpp:5157; 5139-5149 | `workflow.protocol-repair` | function_call_output placeholders `{error:"workflow_result_missing",effect_outcome:"unknown",replayed:false,detail}` |
| coding.cpp:5900-5910; context.cpp:1253-1273 | `stop.linkage` | function_call_output `{error:"turn_interrupted"/"turn_stopped",detail:"Consult audit…"}` for each unanswered call, on interrupted or capacity rollback |
| context.cpp:924 | `session.successor` | seed items with lineage |
| context.cpp:1018 / 1362 / restore | edit / manage / restore | model-authored replacement of the context (transform, not append) |
| programs/useful_turn.lua:7-17 | `lua.synthetic` | assistant boundary text (a Lua append that is not a tool output) |

### 3.7 Shaping choke points

1. **`operation()` after `retain(operation.result)` and before `return result`** (coding.cpp:3902 to 4111). Every tool except `provider` passes here, with name, attempt, input and the full result, and the full result is already durable. A shaped projection returned here keeps the original intact and needs only the attempt id as its handle. Caveat: `call()` is also used internally (rageshake's exec at 5351, poll_commands' process at 2647), so shaping must be limited to the model-visible path or be harmless there.
2. **The `prepare` lambda** (coding.cpp:3440-3585). Shaping here also changes the retained `operation.result`, which loses the presented-vs-full distinction. Use it only for fields such as `output_ref` and `shaped_by`.
3. **The bridge `call_op`** (coding.cpp:1633-1637), which is Lua-only. Or **the turn programs** (turn.lua:12-34), which are model-editable but must be repeated per workflow: useful_turn, economy_pilot and ultracode currently have no defaults.
4. **The tool presenters** that cut bytes: `file_content_presentation` (tools.cpp:208), `process_output_presentation` (tools.cpp:229), `CommandJobs::Impl::snapshot` (command_jobs.cpp:472) and `CommandJobs::read` (1700). These are where head+tail, UTF-8-safe cuts and inline elision markers would go. Plumbing needed: the process foreground `max_output` (coding.cpp:4866-4871) and a budget for read_process_output (coding.cpp:5089).
5. **`ContextStore::append_impl`** (context.cpp:931). This is the single write point for all context entries, including native appends, with an origin label. It suits a context-generation stamp, but shaping here would make the append `originals` packet hold the shaped form.
6. **`export_provider_request` / Anthropic tool_result** (json.cpp:453, anthropic_provider.cpp:396). This is send-time projection, not creation. Every request already runs a `dump_json` pass here.

## 4. Other request makers, identity, restart and continuation

The shared `request()` path is in §1. Every engine-routed maker (child workflows, the backstop's own engine, station, application) ends there.

### 4.1 Backstop (`run_backstop`)

| file:line | surface | dir | notes |
|---|---|---|---|
| src/main.cpp:1271-1279 | caller | trigger | Runs only with `--backstop MISSION_JSON` together with `--once` (main.cpp:195-201), when a turn fails and the failure is not an interrupt. It passes the **same `*provider` object**, `session_store.settings()` and `engine.cancelled`. |
| src/coding.cpp:5686-5710 | `claim_backstop` | gate | It requires a failed turn that was not interrupted, no running turn and depth 0, an unclaimed backstop, local quiescence, a live journal and no uncontained exec. |
| src/backstop.cpp:195-205 | mission | validate | The mission must be an object of at most 32 KiB with a non-empty `mission`. |
| src/backstop.cpp:227-253 | `<session>/backstop` mkdir 0700 | fs | The directory is created atomically, is never replaced, and returns conflict if it already exists. |
| src/backstop.cpp:265-278 | `lineage` {session, environment, journal, prefix_sequence, prefix_end, **context_revision = source head**, status:"unsettled", reason} | audit | This is the existing **successor lineage shape**. |
| src/backstop.cpp:280-303 | selection from `source_engine.context().view()` | **R selection** | It keeps **all system and developer entries plus the last user entry**. Assistant, reasoning and tool entries are dropped, so **prior thinking is not carried**. Over 64 KiB is refused (capacity) rather than truncated. |
| src/backstop.cpp:304-341 | attempts from source audit | R audit | It takes the last 32 AttemptAdmission entries, with input truncated to 1024 bytes and `replay_authority:false`. |
| src/backstop.cpp:342-354 | `packet` | transform | {mission, source:lineage, cause, attempts, …} |
| src/backstop.cpp:360-375 | **new RetainedState journal, new EnvironmentId and AuditStreamId, new AuditLog, new ContextStore** | isolation | It caps the journal at 64 MiB and 20000 records (372). |
| src/backstop.cpp:377-381 | `context.seed_successor({version:1, source:lineage, entries:selected})` | W (new ctx) | |
| src/backstop.cpp:383-387 | developer `instruction` appended (`backstop.instructions`), plus original `backstop.packet` | W | |
| src/backstop.cpp:391-405 | the cancellation callback of the shared provider is saved and restored | provider reuse | This is the only way the borrowed provider is protected. |
| src/backstop.cpp:405-413 | **new CodingEngine{log, context, assessor, settings.model}** with the same effort and a 2-minute deadline | engine | **New session identity**: the engine constructor calls `session_identity(log)` on the empty log (coding.cpp:2535), which issues a fresh actor, conversation and workflow (session.cpp:413-416). |
| src/backstop.cpp:142-154 | assessment Lua: `blackbird.request({tools={}, state_context='omit', retry_policy={max_attempts=1}})` | send | No tools. A function_call in the reply is an error. The output text is decoded as JSON. |
| src/backstop.cpp:504-527 | `effect_policy` | gate | Only one provider call is allowed per assessment and only while assessing. exec and restart are refused. Writes are path-protected. |
| src/backstop.cpp:545-549, 612-616 | `engine.turn({packet, assessment})`, then `engine.turn({…, action})` | send | These are ordinary engine turns inside the recovery context, with at most 2 pivots. |
| src/backstop.cpp:593, 640, 663, 685 | `backstop.pivot`, `backstop.outcome`, `backstop.blocked` originals | audit | |
| return | outcome Value | result | **Nothing is written back into the source context.** main.cpp:1276 only emits the outcome. |

### 4.2 Colleague (one-shot) and Participants (background colleague runs)

| file:line | surface | dir | notes |
|---|---|---|---|
| src/coding.cpp:5469-5491 | `blackbird.call('colleague', q)` (alias at coding.cpp:1105) | send | Uses `call_colleague` with the native transport and the RawProviderOwner{attempt, invocation, generation} (5487). |
| src/colleague.cpp:171-172 | request keys {request_id, from, to, provider, model, task, **context**, profile} | input | |
| src/colleague.cpp:184 | encoded request at most 65536 bytes | limit | |
| src/colleague.cpp:197-199 | profile `tools` must be "none" and `requests` must be 1 | limit | No tools and no continuation (comment at colleague.hpp:7: "No implicit context, continuation or retries"). |
| src/colleague.cpp:201-213 | `context` is an **explicit array of {id, text}** with unique ids | **caller-selected text projection** | Nothing is read from ContextStore. The model or Lua writes the selection. |
| src/colleague.cpp:222-227, 236-241 | prompt = format_value({request_id, from, to, task, selected_context}), plus fixed "independent colleague" instructions | transform | |
| src/colleague.cpp:246-297 | upstream per protocol: Responses (store:false, tools:[]), Messages (max_tokens 8192), chat | send | Uses a ProviderAuth binding, which is a **separate transport from the main provider object**. |
| src/colleague.cpp:298-315 | provider "claude" runs the `claude --print … --no-session-persistence --tools ""` CLI | send (exec) | Implemented in LocalTools exec (colleague.cpp:556-562). |
| src/coding.cpp:5470-5476 | capture: `log_.original("colleague.<label>", {attempt, request_id})` | audit | |
| src/coding.cpp:3981-3987 | disposition from status and remote_disposition | audit | |
| result path | returned as the `call` result | **W indirect** | It enters the main context only as the Lua or tool output (for example the `function_call_output` append in the default workflow, src/workflows.cpp:298). |
| src/colleague_main.cpp:34-64 | `blackbird-colleague REQUEST.json DIR` standalone | send | Request at most 65536 bytes. Uses its own RawRuntime with a random EnvironmentId. No session. |
| src/participants.cpp:20-21 | limits: max_runs 32, max_messages 64, max_inbox 16, max_events 1024, capture 8 MiB | limit | |
| src/participants.cpp:1284-1330 | `participant_start` uses `prepare_colleague(args.request)` | send (worker thread) | The same explicit `context` array. Concurrency and run admission apply. |
| src/participants.cpp:1460-1556 | `participant_send` | transform | It **appends `participant-prior-answer:<run>` with the prior result text to the run's own context array** and appends the direction to the task (text at most 2048, :1509). A participant has its own tiny conversation that is rebuilt per direction (no provider-side state). |
| src/participants.cpp:696-706 | the worker calls `call_prepared_colleague` with the transport | send | |
| src/participants.cpp:82-93 | a result over 65536 is projected to status plus text[:8192] | limit | |
| src/participants.cpp:236-246, coding.cpp:2588-2601 | `participants-state-v1` program snapshot, restored at engine construction | persist | |
| src/participants.cpp:~1123-1135 | on restore, `running` and `waiting` rows become `unknown` | restart | The live run is not resumed. |
| src/coding.cpp:5426-5463 | RawParticipantIdentities{attempt, invocation, generation} | raw | |
| src/coding.cpp:5617-5629 | `validate_session_switch` refuses while participants or commands are active | gate | It is also used by restart. |

### 4.3 Decision models (Jev)

| file:line | surface | dir | notes |
|---|---|---|---|
| src/coding.cpp:4875-4892 | `blackbird.decide` maps to `call('decision_model')` | send | Originals go through the observer to `log_.original(label, {attempt})`. The result gets `audit_ref = attempt`. |
| src/decision_models.cpp:172-236 | `jev_request` keys {provider:"jev", model:"jev-*" (default jev-1.13.0), **state**, questions, timeout_seconds} | input | `state` is a **model-authored description**. **No automatic context is attached.** |
| include/blackbird/decision_models.hpp:11-12 | request and response limit 1 MiB each | limit | |
| src/decision_models.cpp:327-369 | curl to `api.typesafe.ai/v1/systemone` | send | Uses its own transport (curl child). It does not use the main provider. |
| result | the call result | W indirect | It enters context only as a tool or Lua output. |

### 4.4 Workflows

| file:line | surface | dir | notes |
|---|---|---|---|
| src/coding.cpp:4916-4977 | `workflow_invoke` | | Records a `workflow.selection` original and a program record. If tool outputs are still unanswered, it **queues a WorkflowContinuation (at most 8)**. Otherwise it executes immediately. |
| src/coding.cpp:4115-4205 | `execute_workflow` runs a child `Runtime child{*this}` on the **same engine and same ContextStore** | R/W full | Depth at most 8, at most 64 per turn. Any `blackbird.request` inside it uses §1 on the main context. |
| src/coding.cpp:4215-4246 | `drain_workflows` (before every request, and at turn end, coding.cpp:5970) | **W** | The result is appended as a developer message `"Registered workflow X completed…"` with label `workflow-continuation`. |
| src/coding.cpp:5915, 5816-5818 | continuations are cleared at turn start or teardown | | They do not survive a restart. |
| src/workflow_runtime.cpp:405-520 | native WorkflowRuntime `start` (tools exec, read, write, edit, echo only) | no model | **Issues a fresh actor, ConversationId, WorkflowId, DefinitionGenerationId and ContextRevisionId per task** (436-440) in its DecisionEvent (444-455). There is no context. |
| src/workflow_host.cpp, include/blackbird/workflow_host.hpp:19-48 | unix-socket transport only | no model | |
| include/blackbird/workflow_executor.hpp:40-44 | `WorkflowExecutorInput{source, context, input}` payload refs | | The `context` slot exists but runtime.cpp:497 passes `{}`. |

### 4.5 Station, application and candidate

| file:line | surface | notes |
|---|---|---|
| src/station.cpp:490-519; src/main.cpp:~1546-1561 | the station turns a feed event into a prompt (prefix + event + steer) and calls `engine.turn` | The same main context. The station never does the implicit restart continue (main.cpp:1496). |
| src/application.cpp:296-304 | appends host page observations to the main context, then calls `operator_turn` | The same main context. |
| src/candidate.cpp:1 | the `blackbird-candidate` binary (CMakeLists.txt:185) | Its own comment says "No provider or activation authority". It has no context and no model. |

### 4.6 Identity: what ids exist

| file:line | id | scope / notes |
|---|---|---|
| include/blackbird/foundation.hpp:116-127 | EnvironmentId, ParticipantId, **ConversationId**, WorkflowId, InvocationId, OperationAttemptId, ContextRevisionId, **DefinitionGenerationId**, AuditStreamId … | |
| src/session.cpp:384-430 | `session_identity` gives {actor, conversation, workflow} | It reads the `session.identity` program record first. Otherwise it uses the **last DecisionEvent**. Otherwise it issues new ids, then records them. |
| src/retained_state.cpp:2983-2985 | `session.identity` is **first-wins** in current_programs | The identity is immutable per journal. |
| src/coding.cpp:2535 | `identity_` is cached at CodingEngine construction | |
| src/coding.cpp:3346-3356; include/blackbird/retained_events.hpp:118-128 | **DecisionEvent{decision, actor, conversation, workflow, definition, context, invocations, continuation}** for every operation, provider requests included | `context` = ContextRevisionId parsed from `context_.head()` (coding.cpp:3316). The continuation metadata holds {operation, invocation, revision, generation} (3318-3324). The binary schema is in src/retained_events.cpp:190, 471. |
| src/context.cpp:945-946, 1538-1539 | context revision = hex(ApplicationRecordId issued by the log) | Entry id = `<revision>.<ordinal>` (context.cpp:951). Ids are **journal-bound**: original lookup goes by record identity in this journal and saved boundary (context.cpp:513-568). |
| src/context.cpp:1569-1571 | a summarize entry carries `ancestry: ids` | This is per-entry lineage. |
| src/context.cpp:918-924, 967-968 | `lineage` field on the `session.successor` append packet | This is the only context-level lineage today. |
| src/coding.cpp:2536, 5942 | **`generation_` = DefinitionGenerationId, reissued every turn** | It is called "generation" in the `provider.request` log (4578), the decision metadata (3324), the workflow metadata (4171), raw sites and token usage, and audit.cpp:172 prints `DecisionEvent.definition` as "generation". **This is a name collision.** |
| src/coding.cpp:4517-4524 | retry_id / retry_group (OperationAttemptId) per request group | |
| src/token_usage.cpp:330-440, 590-645; include/blackbird/token_usage.hpp:40-41 | UsageObservation {attempt, invocation, generation (Definition), retry_group}. Causal parent walk (606-622). Actor main, colleague or decision comes from site (3600) and kind | **Usage carries no context revision and no conversation.** A per-generation rollup would have to join via attempt to the DecisionEvent context or via the `provider.request` revision. |
| src/session.cpp:114-140, 301-303 | `session-info.bbm` {actor, conversation, workflow_id} for discovery | |
| src/main.cpp:970-986 | `/session` shows conversation and workflow_id | |
| src/main.cpp:1658; src/tui_action_bridge.cpp:175-178; src/command_center.cpp:394, 444; src/tui_instrument.cpp:251, 489-492 | The TUI action scope is {environment, **conversation**}. An envelope whose conversation is not the scope's is **refused** (wrong_environment). | Changing the ConversationId in the middle of a session would break the action inbox and the command center. |

### 4.7 Restart and continuation

| file:line | surface | notes |
|---|---|---|
| src/coding.cpp:5012-5031 | `restart` tool sets `restart_note_` (at most 64 KiB) | Blocks further requests (4271) and workflow drain (4231). |
| src/coding.cpp:5631-5643 | `validate_restart`: no uncontained exec, no active participants or commands, valid protocol | |
| src/session.cpp:521-555 | `SessionStore::restart` writes the program record `session.restart` {token=new ApplicationRecordId, state:"pending", note} | Refused if one is already pending or if there are unresolved attempts (533-536). |
| src/main.cpp:1240-1246, 1108-1113, 1574-1578 | the note is taken after a turn or `/restart` | Exit code 75. |
| src/main.cpp:1294-1325, 1850-1915 | re-exec `--session S --resume-continue --new-session-instance` | **Same directory, same journal, same identity.** |
| src/main.cpp:293-359 | reopen: `RetainedState::open` (saved state + tail; full replay limited to 64 MiB unless `--rebuild-session`), then `recover_coding_session` | Linked audit segments (`header.predecessor`) are refused (main.cpp:303-307). |
| src/context.cpp:327-497 | ContextStore reconstruct: saved `context` {version 1, base, entries, live_originals}, then replay of **all** `ApplicationChannel::context` tail records | Requires `observed == head_` and `base == head_`. The op must be append, edit or managed (425-427), otherwise corrupt. **One linear chain per journal.** |
| src/context.cpp:587-706 | `checkpoint` → `save_current_state({version, base, entries, live_originals})` | Runs every 64 records or 4 MiB (701-703), on `/checkpoint`, and before a session switch (main.cpp:1021, 1070). |
| src/retained_state.cpp:3025-3153 | SavedState = {boundary, context, programs=current_programs, unresolved} | `semantic_coverage` is refused if a `session.restart` is pending (3145-3149). |
| src/main.cpp:520-521; src/session.cpp:557-615 | `SessionStore::resume` | If no context record has origin `rrc:<token>`, it **appends a user message** "continue\n\nRestart/resume note:\n<note>" with label `rrc:<token>`, then records `session.restart` state "consumed". A continue turn follows (main.cpp:810-818). |
| src/main.cpp:225-231, 423-425 | `--seed-session JSON` uses `validate_successor_seed` then `seed_successor` | Only into a **new, empty session directory**. |
| src/context.cpp:782-902 | `validate_successor_seed` | seed at most 1 MiB, entries 1-4096, id format, roles; **accepts reasoning items** (encrypted or anthropic thinking, 860-884); tool groups complete. |
| src/context.cpp:903-925 | `seed_successor` | It **requires an empty log (`fact_count()==0`)**, no entries and no workflow, and the source journal must differ from its own (908-912). The append label is `session.successor` and it carries lineage. |
| src/main.cpp:1059-1107, 993-1058 | `/new` and `/resume DIR` | Exec into another session. **No context carried and no lineage.** |
| src/coding.cpp:2618-2619 | an empty head appends `"initial"` | |
| src/coding.cpp:5934-6002; src/context.cpp:1352-1517 | managed proposals are staged during a workflow and published at `finish_workflow(success)` atomically with the boundary program (`record_boundary`, context.cpp:1663) | This is the existing activation point ("after conclusion of turn"). |

## 5. Provider rules currently hard-coded in C++

Every rule below is compiled into the binary. Each one belongs in a shipped provider profile, or in a protocol codec that a profile selects. The rules fall into four kinds:
- **(P) protocol codec**: the shape of the Responses, Messages or Chat wire format. These stay as code, but a profile selects the codec.
- **(F) model-family capability**: effort levels, thinking mode and budgets, cache support, reasoning carry-forward, item-kind acceptance.
- **(R) route and account**: base URLs, headers, auth flows, beta flags, client ids.
- **(E) engine policy that is really provider policy**: retryable statuses, usage field names, the state-item persistence rule, cache allowlists.

The rows below come mainly from the provider pass. The engine rows were added or confirmed in §1.

### 5.1 Engine (applies to every provider)
- (E) Default model `gpt-6.1-sol`, effort `medium`, provider `openai`: main.cpp:91,95,96. Engine effort default: coding.hpp:162. Effort set {low, medium, high, xhigh, max}: coding.cpp:5657-5661.
- (E) Fixed `instructions` doctrine, ending with the host cwd: coding.cpp:4284-4313. On Anthropic OAuth it is prefixed again (§5.6).
- (P/E) `parallel_tool_calls:false`, `include:["reasoning.encrypted_content"]`, `store:false`, `stream:true`: coding.cpp:4322-4325. `store` and `stream` are enforced at :4393-4395. **Stateless full replay is a hard rule**, and every adapter also refuses `previous_response_id`.
- (E) Retry defaults (5 attempts, 1000 ms, 16000 ms) and the retryable set (HTTP 408/429/500/502/503/504, curl 5/6/7/16/18/28/52/55/56/92/95/96, typed 429): coding.cpp:4328-4351, 4673-4688. Anthropic 529 and SSE `overloaded_error` are not in the set. No route classifies a context-overflow error.
- (E) Usage field names kept (Responses vocabulary): coding.cpp:4772-4776. Adapters must translate into these names (Anthropic: anthropic_provider.cpp:237-266).
- (E/F) `retain_request_state()` decides whether the operational state item is persisted into context (coding.cpp:4441-4445), and whether CONTEXT_SIZE rides every request or only the trigger (:4404). Only Anthropic sets it (anthropic_provider.hpp:15).
- (E) State item role `developer`, task read limit 16: coding.cpp:4376-4392, 4436-4440.
- (E) Budget defaults: trigger 393216, target 196608, enabled (coding.cpp:2080-2082). There is no model context window (`context_window_tokens: null`, coding.cpp:2462-2467).
- (P) Responses export: anthropic_* stripping; rejection of a `reasoning` item that carries `anthropic_content`; output-part kinds input_text/input_image/input_file; JSON-stringifying arguments and outputs. All in json.cpp:452-506.
- (P) `import_provider_response` parses only `function_call.arguments`: json.cpp:432-451.
- (P) `ResponsePreview` parses Responses **and** Anthropic SSE in one class (openai.cpp:482-581; `anthropic_message_` at openai.hpp:66). This is a preview codec keyed by guesswork rather than by the active profile.
- (E) `validate_protocol` knows only `function_call`/`function_call_output` (coding.cpp:286-317). `custom_tool_call` is unmodelled (§2.8 item 7).

### 5.2 Prompt cache
- (F/E) Supported pairs `openai-codex`+`gpt-6.1-sol` and `openrouter-responses`+`openai/gpt-6.1-sol`: request_cache.cpp:88-91.
- (R) The OpenAI cache route requires kind `api_key`, `https://api.openai.com/v1` and `responses`: coding.cpp:329-340. OpenRouter supports it whenever a group is active: openrouter.hpp:18.
- (P/F) `prompt_cache_options{mode implicit|explicit, ttl:"30m"}` and a single `prompt_cache_breakpoint{mode:explicit}` placed before the state item; conflicting fields refused: request_cache.cpp:94-96, 104-136.
- (F) Anthropic: no `cache_control` is ever emitted, and non-default modes are refused (§2.5).

### 5.3-5.9 Per provider (from the provider pass)

#### OpenAI / Codex
- `https://chatgpt.com/backend-api/codex` with kind `codex_chatgpt`: openai.cpp:157-164, 380-381.
- Routes `responses` and `models?client_version=0.160.0`: openai.cpp:378.
- Headers Authorization, ChatGPT-Account-ID, Content-Type, `originator: blackbird`: openai.cpp:398-404.
- `auth_mode=="chatgpt"` requirement: openai.cpp:199, 265.
- `~/.codex/auth.json` / `$CODEX_HOME`, 1 MiB limit: openai.cpp:51-61, 69.
- `codex app-server --listen stdio:// -c model_provider="openai"`, 30s deadline, clientInfo version 0.1.0: openai.cpp:276-286.
- `getAuthStatus` with includeToken: openai.cpp:293-296.
- Response limit 16 MiB, timeout 180s: openai.hpp:15-16.
- Owned route must be `responses`: openai.cpp:360.
- SSE event names: openai.cpp:619-670.
- Empty `completed.output` fill rule: openai.cpp:696-714.

#### Registered Responses (Paper and others)
- Protocols `responses` and `responses-portable`: responses_provider.cpp:101.
- Portable effort low/high/max: :26-28; main.cpp:493-494.
- Portable include cleared: :30-34.
- Portable `strict:false` default: :37-41.
- Portable refuses encrypted_content: :44-46.
- Portable rewrites developer to system: :47-48.
- Call status, call_id and arguments checks: :65-79.

#### OpenRouter
- base `https://openrouter.ai/api/v1`: openrouter.cpp:54; provider_auth.cpp:313.
- Model must contain '/' and be ≤256 characters: openrouter.cpp:49.
- `provider.require_parameters:true`: :69-73.
- session_id ≤256: :74-76.
- Synthetic `blackbird_view_<n>` id, status completed, type message, output_text conversion: :87-98.
- Headers `X-OpenRouter-Metadata: enabled` and `X-OpenRouter-Cache: false`: openrouter.cpp:38-39; provider_auth.cpp:1541-1544.
- `[DONE]` requirement and `openrouter_metadata`/tail handling: openrouter.cpp:115-146.
- error_type auth classes authentication, permission_denied, payment_required: :28-30.
- Builtin protocol `chat`: provider_auth.cpp:313.
- Cache model `openai/gpt-6.1-sol`: request_cache.cpp:90.

#### Anthropic
- max_tokens default 16384: anthropic_provider.hpp:8, 29.
- Request key allow-list: anthropic_provider.cpp:304-308.
- Effort set and model exclusions (`claude-sonnet-4-6`, `claude-opus-4-6`, `claude-opus-4-5`): :292-298.
- Manual-thinking families `claude-sonnet-4-5`, `claude-haiku-4-5`, `claude-opus-4-5`, `claude-sonnet-4-2025`, `claude-opus-4-2025`, `claude-3-7-sonnet`: :488-492.
- Budgets 1024/4096/8192/12288/16384, min(max_tokens-1), max_tokens>1024: :494-504.
- `output_config.effort` for opus-4-5 and adaptive models: :505-509.
- `tool_choice{auto, disable_parallel_tool_use:true}`, and tool_choice only auto or none: :464-485.
- `stop` becomes `stop_sequences`: :469-470.
- `[Blackbird <role> guidance]` prefix: :430-432.
- OAuth system prefix "You are Claude Code, Anthropic's official CLI for Claude.": :454-456 (and colleague.cpp:282-285).
- First message must be user: :450-452.
- Accepted stop reasons: :205-207.
- Block and delta allow-lists: :113-127, 159-174.
- Output id `<msgid>:<index>`: :219.
- Usage summing: :238-262.
- Headers `anthropic-version: 2023-06-01`, x-api-key or Bearer, `anthropic-beta: oauth-2025-04-20,claude-code-20250219`, `User-Agent: claude-cli/2.1.163 (external, cli)`, `x-app: cli`: provider_auth.cpp:1545-1556.
- OAuth client id, token URL and scopes: provider_auth.cpp:292-296.
- JSON refresh body: provider_auth.cpp:1436-1442.
- `retain_request_state` true: anthropic_provider.hpp:15.

#### Kimi
- `https://api.kimi.com/coding/v1`, messages protocol (colleague only): provider_auth.cpp:297-301.
- `anthropic-version` header and `User-Agent: KimiCLI/blackbird-0.1`: :1545-1558.
- OAuth headers `X-Msh-Platform: kimi_cli`, `X-Msh-Version: blackbird-0.1`: :415-418.

#### MiMo / Grok / Moonshot (chat, colleague only)
- MiMo `https://api.xiaomimimo.com/v1`, `MIMO_API_KEY`: provider_auth.cpp:302-303.
- Grok `https://api.x.ai/v1` with device OAuth: :304-309.
- Moonshot `https://api.moonshot.ai/v1`: :310-311.
- Chat body is system plus user, `stream:false`: colleague.cpp:286-295.
- One choice, `finish_reason=="stop"`, no tool_calls: colleague.cpp:512-519.

#### Colleague (all)
- Request ≤64 KiB, tools none, 1 request: colleague.cpp:184-199.
- Messages `max_tokens 8192`, `stream:false`: colleague.cpp:280-281.

#### Transport and auth (all owned routes)
- Routes responses, chat/completions, messages, models: provider_auth.cpp:1373-1375.
- https only, ≤2048-character URL: :71-76.
- 16 MiB body and response limits: :463, 528.
- Timeout 1-3600s: :462.
- OAuth refresh window 60s, back-off 60s: :1415, 1456.
- OpenAI refresh `resource` parameter: :1434-1435.
- Status typing: :422-429.
- At most 32 registered providers, 128 accounts: :1012, 931.
- Store `credentials.bbm`, `$BLACKBIRD_AUTH_HOME`, `~/.local/share/blackbird/auth`: :167, 624-631.
- curl override environment variables `BLACKBIRD_{OPENROUTER,ANTHROPIC,RESPONSES}_CURL`: main.cpp:530, 535, 541.

## 6. Assumptions and coupling points for context generations

Each item states an assumption the current code makes, where it is enforced, and what a generation design must keep or change.

### 6.1 One provider conversation per session, and what "conversation" means today
1. **No provider-side conversation exists.** Every request is stateless full replay: `store:false` and `stream:true` are enforced (coding.cpp:4393-4395), and every adapter refuses `previous_response_id` (responses_provider.cpp:20, anthropic_provider.cpp:313, openrouter.cpp:63). The only provider "conversation" is the sequence of identical-prefix inputs. A new generation therefore means:
   - the next request's `input` starts from the compacted entries;
   - no replayed reasoning or thinking from earlier generations;
   - a deliberate cache-prefix break.

   No provider handle exists to rotate. OpenRouter `session_id` (openrouter.cpp:74-76) is the only provider session field, and the engine never sets it.
2. **`input` is the whole live ContextStore**, read verbatim (coding.cpp:4277; context.cpp:752-763). A generation must therefore be a state of the ContextStore, or else `request()` needs a selection step between `items()` and `input`. The Lua `input=` override (coding.cpp:4363-4374) can send a selection today. But the response is still appended to the main context (:4803) and the head CAS still applies (:4467, :4794).
3. **One linear context chain per journal.** Reconstruction requires `base == observed == head` and knows only the ops append, edit and managed (context.cpp:422-431, 425-427). A generation cut must be a linear record in that chain, for example a `managed` packet with a generation field, or a new op. A new op is `corrupt` to older binaries.
4. **`seed_successor` assumes a fresh journal** (context.cpp:903-925: `fact_count()==0`, no entries, a different source journal). The backstop and `--seed-session` both use it to start a *new session*. Its `lineage{version, scope, source, source_entries}` shape (:913-918) and the backstop lineage (backstop.cpp:265-278, including `context_revision`) are the existing lineage vocabulary. In-session generations need a sibling operation or relaxed preconditions.
5. **ConversationId is a session constant and a UI scope key.** It is first-wins (retained_state.cpp:2983-2985), cached at engine construction (coding.cpp:2535) and stamped on every DecisionEvent (coding.cpp:3345-3354). The TUI action scope and the command center reject envelopes whose conversation differs (tui_action_bridge.cpp:175-178). **Do not rotate ConversationId per generation.** Add a separate generation identity.
6. **"generation" is already taken** by `DefinitionGenerationId`, which is re-issued every turn (coding.cpp:5942). It already appears as `"generation"` in:
   - `provider.request` metadata (:4578-4579);
   - operation admission metadata (:3323);
   - workflow metadata (:4171);
   - raw sites (:4486);
   - token usage;
   - the audit display (audit.cpp:172).

   The new identity needs a distinct name, such as `context_generation`, and ideally its own id type in foundation.hpp:116-127.

### 6.2 Where a generation id would attach (existing seams)
- Engine single point of assembly: coding.cpp:4277-4280. Here `origin` is captured, and a generation would select or label the input.
- `provider.request` log metadata (coding.cpp:4571-4582). This is the attempt → context revision link. It is free-form, so it needs no binary schema change.
- DecisionEvent `continuation` metadata (coding.cpp:3318-3324). It is free-form packet bytes, written per operation, including `provider`.
- `provider.stream` original metadata (coding.cpp:4593-4598).
- The context packet `lineage` (managed or successor) and summary `ancestry` (context.cpp:1569-1571).
- A latest-wins program record, for example `session.context-generation`, reduced in `current_programs` and saved in SavedState (retained_state.cpp:2986-2989, 3025-3153), so that it survives restart.
- Usage carries no context or conversation key (token_usage.hpp:40-41). Per-generation cost would need a tertiary join: attempt → `provider.request.revision` or `DecisionEvent.context` → generation. This is consistent with the raw-capture doctrine.

### 6.3 Encrypted reasoning and signed thinking carried forward
- **Responses routes:** reasoning items with `encrypted_content` come back in `response.output`. They are appended verbatim at coding.cpp:4803 and replayed verbatim on every later request. This follows from `include:["reasoning.encrypted_content"]` with `store:false`. The engine never drops stale reasoning. Successor seeds require non-empty `encrypted_content` (context.cpp:855-883), but ordinary append does not check it.
- **Anthropic:** thinking and redacted_thinking come back as `{type:reasoning, summary:[], anthropic_message_id, anthropic_content}` (anthropic_provider.cpp:210-236). They are replayed verbatim inside their original assistant group (:344-358, 412-419), and `native_block` refuses any projected field that differs from the original (:57-67). Together with the operator's research finding, this is why client-side shortening of an earlier tool_result invalidates later thinking.
- **Cross-family replay fails hard.** A reasoning item with `anthropic_content` sent to any Responses route throws `unsupported` (json.cpp:456-458). An OpenAI encrypted reasoning item sent to Anthropic throws `unsupported` (anthropic_provider.cpp:415-417). The portable route refuses any `encrypted_content` (responses_provider.cpp:44-46). Provider and model switches (§6.6) therefore already require dropping reasoning. A generation cut that drops reasoning would serve them too.
- **`whole_rounds` keeps reasoning bound to its append** (context.cpp:1155-1169), and `manage` enforces it (:1412-1414). `publish_managed` keeps an open call's whole round (context.cpp:1556-1565). A generation that keeps calls and messages but drops reasoning needs a generation-specific rule. **Unconfirmed:** whether Responses `store:false` accepts a `function_call` whose preceding reasoning item is removed, and whether Anthropic requires thinking only within an open tool loop. Cutting at a boundary with no open group (points F, G and J in C6_SOURCE_MAP) avoids the open-loop case.

### 6.4 Prompt-cache prefix stability
- The cacheable prefix is "every input item before the state item". The breakpoint count is `reusable_input_items` (coding.cpp:4435, request_cache.cpp:104-136). Any edit, managed publication or generation cut moves it. The cost of one cache miss per cut matches the operator's "compact rarely, in batches".
- Non-retaining providers put the state item at the **tail**, ephemerally (coding.cpp:4446-4450), so the prefix is stable between requests.
- Anthropic **persists** the state item into context (coding.cpp:4441-4445). The item becomes prefix history and accumulates: CURRENT_TASKS on every request, CONTEXT_SIZE/BUDGET when triggered. These are developer entries, which `manage` pins (context.cpp:1416-1425, rule "mandatory-entry"). A generation cut is the natural point to drop the old ones, but the pinning rule must allow it.
- Anthropic receives no cache breakpoints today (§2.5). The other routes cache only on `gpt-6.1-sol` (request_cache.cpp:88-91). Profiles must say per family where the breakpoint goes and how many breakpoints are allowed.

### 6.5 Attempt and raw linkage
- Per request: one `retry_id`/`retry_group` (coding.cpp:4517-4524, raw 1325), with N attempts. Each attempt is an `operation("provider")` carrying the **whole pre-export request as the input original** (coding.cpp:3310-3312), a `provider.request` record `{attempt, revision=origin, generation(definition), retry_group, ordinal}`, `provider.stream` originals `{attempt, revision}`, and the final request packet lease (coding.cpp:4469-4475).
- The output append label `provider.branch:<origin>` (coding.cpp:4803) binds the context entries to the request's base revision. This is the request ↔ context link that lineage must keep across a cut: the last request of generation N and the first request of generation N+1 must both resolve.
- Duplication note: the provider input original re-retains the whole context on every attempt. Dedupe is **Unconfirmed** (§3.3). A generation cut shrinks this per-request cost.
- Head CAS: `origin` is captured before the state item and advanced only by the state append (coding.cpp:4443). A generation switch must not happen between :4278 and :4794.

### 6.6 Model and provider switch
- The provider is fixed per process (main.cpp:527-546). Changing provider requires `--model` (main.cpp:448-453) and a new process.
- `/model` changes only the model on the same provider object (main.cpp:1161-1169), and reasoning from the old model stays in context. **Unconfirmed** whether a Responses provider accepts another model's `encrypted_content`.
- PLAN.md names mid-session model switches as a generation use. The switch would be "cut a generation, drop reasoning, then switch". For providers this needs either a provider object per generation or a re-bindable provider.

### 6.7 Other request makers
- **Backstop** is the only isolated request maker today. It builds a separate journal, ContextStore and engine. It carries only system, developer and last-user entries, so no thinking (backstop.cpp:280-303). It reuses the same provider object and saves and restores `provider.cancelled` (backstop.cpp:391-405). It is the closest precedent for "start from compacted context", but it is a new session, not a generation.
- **Child workflows** share the main context and engine (coding.cpp:4115-4205). Their continuation results are appended as developer entries before the next request (:4238-4244). A generation cut must order `drain_workflows` first, and these developer entries are pinned too.
- **Colleague, participants and Jev** never read ContextStore. Their context is an explicit `{id,text}` array or a `state` text (colleague.cpp:201-213, decision_models.cpp:172-236). Generations do not affect them, except that their results enter context only as tool outputs. Each has its own transport, not `provider_`.
- **Station and application** feed the same main context through `engine.turn`/`operator_turn` (station.cpp:490-519, application.cpp:296-304).

### 6.8 Restart and RRC
- Restart re-execs the **same** session directory, journal and identity (main.cpp:1294-1325, 1850-1915). The context comes back from SavedState plus a replay of the context-channel tail (context.cpp:327-497). A generation id must live in a context packet or a program record so that it reconstructs.
- RRC appends the user message `continue\n\nRestart/resume note:…` labelled `rrc:<token>` (session.cpp:557-615), deduped by scanning tail facts. **Unconfirmed:** whether the dedupe misses a note that was folded into saved state before a checkpoint.
- A restart requires a protocol-complete context and no active jobs or participants (coding.cpp:5620-5643). These are the same quiescence conditions a cut would want.
- Activation timing: managed proposals publish at a successful `finish_workflow` together with the boundary program (coding.cpp:6001-6002; context.cpp:1663 `record_boundary`). Generation switching fits that boundary. The C6 extension (publish at F or G) is the open design point.

### 6.9 Tool-output shaping (for the creation-time design)
- In context, a tool result is a **structured Value** under `output`. It is stringified only at export (json.cpp:496; anthropic_provider.cpp:396-411).
- The 16 KiB default exists only in `programs/turn.lua:12-26`. Other shipped programs (`useful_turn`, `economy_pilot`, the built-in `ultracode` at workflows.cpp:293-298) set no default.
- `process foreground` ignores `output_max_bytes` (coding.cpp:4864-4874). `read_process_output` has no budget and scans all audit facts linearly (coding.cpp:5033-5122).
- Cuts are byte-exact prefixes. A split UTF-8 character turns the whole slice into hex (tools.cpp:106-118, 208-235; command_jobs.cpp:472-524).
- Best single choke point: `operation()` after `operation.result` is retained and before `return result` (coding.cpp:3894-4111). At that point every tool's full result is already durable, and the attempt id is a ready handle. Caveat: internal `call()` users are rageshake's exec (:5351) and the poll_commands process mailbox (:2646-2647).
- Shaping in `ContextStore::append_impl` (context.cpp:927-1001) would store the shaped form as the context "original". It is the right place for a generation stamp, not for shaping.

## 7. Unconfirmed items (collected)
- Whether the journal deduplicates the provider-operation input original (the whole context) across attempts and requests (§3.3, §6.5).
- Whether Responses providers accept another model's `encrypted_content` after `/model` (§6.6).
- Whether Responses `store:false` accepts a function_call whose reasoning item has been removed. Whether Anthropic requires thinking only within an open tool loop (§6.3).
- Whether Anthropic-origin item ids (`msg_…:N`) are accepted by OpenAI or OpenRouter when replayed after a switch (§2.8 item 1).
- Whether OpenRouter returns encrypted reasoning for non-OpenAI models (§2.4). Whether Codex ever returns reasoning without `encrypted_content` (§2.2).
- Whether any registered route (Paper) emits `[DONE]`, which `completed_response` would reject (§2.8 item 11).
- Whether the exec jobs path uses the 120 s timeout default from tools.cpp (§3.2). Whether `read_file` snapshot reads should be restricted to `file.read` sources (§3.2).
- Whether the RRC dedupe can miss a note folded into saved state (§6.8). Whether WorkflowRuntime DecisionEvents (with a fresh ConversationId per task) can land in a coding journal and mislead `session_identity`'s fallback (§4.6, others item 7).
- Whether the operator wants pinned instructions carried verbatim across generations (§6.4).
