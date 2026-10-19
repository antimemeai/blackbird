# Chinese model providers: tool-call output and reasoning lifecycle in long multi-turn conversations

All pages were read on 2026-10-19 through a summarizing fetch tool; no API was called. Most vendor pages show no "last updated" date. Where a page has a blank date field, these notes say so. Quotes are as the fetch tool returned them. Short Chinese quotes are translated.

**Model lineup caveat.** The live docs have moved past the models named in the brief. As of today:
- DeepSeek: `deepseek-flash` (served by DeepSeek-V4.1-Flash-1017) and `deepseek-v4-pro`.
- Kimi: `kimi-k3`, `kimi-k2.7-code` and `kimi-k2.6`.
- GLM: GLM-5.x and the GLM-4.7 series.
- Qwen: Qwen3.6, 3.7 and 3.8.
- MiniMax: M2 through M3.1-Flash.
- MiMo: v2.5 and v2.6.

Earlier rules (DeepSeek-R1 `deepseek-reasoner`, K2-Thinking, GLM-4.5/4.6) are noted where they could still be found. Several are no longer on the vendor pages.

## Pairing: tool call / result pairing rules

### Takeaway
No vendor publishes a formal pairing specification, a list of pairing errors, or orphan rules. Every vendor shows the same OpenAI-style pattern in examples: append the whole assistant message with its `tool_calls`, then add one `role:"tool"` message per call, keyed by `tool_call_id`. The Anthropic-compatible endpoints use `tool_use` / `tool_result` with a matching `tool_use_id`. Pairing behaviour beyond that is undocumented everywhere.

### Cited Findings
- **DeepSeek:**
  - The thinking-mode guide's sample appends the full assistant message, including `tool_calls`, then one `role:"tool"` message per call with `"tool_call_id": tool.id`. The loop ends when `tool_calls` is `None`. The page gives no formal pairing rules. — [DeepSeek Thinking Mode guide](https://api-docs.deepseek.com/guides/thinking_mode)
  - On the Anthropic-compatible endpoint, `tool_use` (`id`, `input`, `name`) and `tool_result` (`tool_use_id`, `content`) are "fully supported". `is_error` and `cache_control` on `tool_result` are **ignored**. — [DeepSeek Anthropic API guide](https://api-docs.deepseek.com/guides/anthropic_api)
- **Kimi:** Tool results are appended with `role:"tool"`, a `tool_call_id` that matches `tool_call.id`, and the function `name`, after the assistant message. The page gives no further pairing rules. — [Kimi thinking-model guide](https://platform.kimi.ai/docs/guide/use-kimi-k2-thinking-model) (platform.moonshot.ai now 301-redirects to platform.kimi.ai)
- **Qwen / Model Studio:**
  - "`tool_call_id` 是系统为每一次的工具调用请求生成的唯一标识符" ("tool_call_id is a unique identifier the system generates for each tool call request").
  - For the Responses API: "将工具调用和结果成对追加到上下文中" ("append tool calls and their results to the context in pairs"), matched by `call_id`.
  - Several tool results may follow one assistant message (`parallel_tool_calls: true`).
  - In thinking mode, `tool_choice: "required"` is not supported: "思考模式下当前不支持\"required\"".
  - The page does not document a "tool message must follow tool_calls" error. — [Model Studio Function Calling](https://help.aliyun.com/zh/model-studio/qwen-function-calling) (date field blank; only the first 100k of about 163k characters were read)
- **MiniMax:**
  - OpenAI-compatible: append the whole `response_message`, then `role:"tool"` with `tool_call_id`.
  - Anthropic-compatible: append the full `response.content`, then a `user` message with a `tool_result` whose `tool_use_id` matches. — [MiniMax M2 function-call guide](https://platform.minimax.io/docs/guides/text-m2-function-call)
- **Z.ai:**
  - The error code table has no tool or role-ordering code. The nearest codes are 1210 "Invalid API parameter", 1213 "Parameter `${field}` was not received", 1214 "Parameter `${field}` is invalid" and 1261 "Prompt too long".
  - The page does not say which code a malformed history returns. — [Z.ai error codes](https://docs.z.ai/api-reference/api-code)
  - A GLM quirk on Model Studio: without `extra_body={"tool_stream": True}`, GLM will not return `tool_calls`. — [Model Studio Function Calling](https://help.aliyun.com/zh/model-studio/qwen-function-calling)
- **MiMo:** The Responses endpoint lists `FunctionCall` and `FunctionCallOutput` item types but does not describe `call_id` pairing. — [MiMo Responses API](https://mimo.mi.com/docs/en-US/api/chat/responses)

### Inferences
- In practice the pairing contract is the emulated protocol's: OpenAI Chat Completions on `/v1/chat/completions`, Anthropic Messages on `/anthropic`. None of the vendors documents any relaxation.
- A compactor should keep each assistant `tool_calls` message and all of its results together as one unit. It should elide or summarise result contents inside the `tool` message rather than drop the message. No vendor documents what happens to orphaned results or unanswered calls.
- DeepSeek's Anthropic endpoint ignores `is_error`. A client that marks failures only with `is_error` should also say so in the result text.

### Gaps
- None of the providers documents errors for orphaned `tool` messages, missing results for issued calls, or reordered parallel results.
- Z.ai does not say which error code (1210 or 1214) covers malformed history.
- The second part of the Qwen function-calling page was not read.

## Reasoning: must, may or must not reasoning be sent back on later turns

### Takeaway
The vendors split three ways:
- **Must return, whenever tools are present:** DeepSeek's current docs.
- **Must or should return unmodified, including interleaved thinking:**
  - Kimi k3 and k2.7-code;
  - GLM preserved thinking;
  - MiniMax (`<think>` inside content);
  - MiMo (recommended).
- **Opt-in parameter:**
  - Qwen `preserve_thinking`;
  - Kimi k2.6 `thinking.keep`;
  - GLM `clear_thinking: false` on the standard API.

The older DeepSeek-R1 rule, "stripping is required; including reasoning_content gives a 400", has disappeared from the current official pages. It survives only in mirrors and community reports.

### Cited Findings

#### DeepSeek
- **Without tools:** "the CoT content from previous turns will not be concatenated into the context in the next turn", and "reasoning_content does not need to be passed back; even if passed to the API, it will be ignored." — [DeepSeek Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode)
- **With tools:**
  - "the reasoning_content must be fully passed back to the API in all subsequent requests", "even for turns where the model did not perform a tool call".
  - Earlier turns' reasoning is also resent ("we still pass the reasoning_content generated in Turn 1 to the API").
  - Omitting it means "the API will return a 400 error".
  - Thinking is on by default (effort `high`). The examples use `deepseek-flash`, with outputs dated 2026-04-19. — [DeepSeek Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode)
- **Legacy `deepseek-reasoner`:** The current reasoning-model page (English and zh-cn) no longer mentions the field, the 400 error or `deepseek-reasoner`. It now lists `deepseek-flash` and `deepseek-v4-pro`. The legacy names `deepseek-v4-flash` and `deepseek-v4-flash-vision-exp` are accepted but "retired". — [DeepSeek reasoning model page](https://api-docs.deepseek.com/guides/reasoning_model); [zh-cn](https://api-docs.deepseek.com/zh-cn/guides/reasoning_model)
- **Historical R1 rule (third-party mirror of the old official doc):** previous-round CoT is not concatenated. Including `reasoning_content` in input messages returns a 400, so the field must be removed before the next request. Labelled as a mirror, not live official text. — [glama.ai mirror of DeepSeek reasoner docs](https://glama.ai/mcp/servers/@liuchongchong1995/deepseek-mcp-server/blob/40398b613ffb1cace8eeb51a962b93625d0a7642/src/docs/deepseek-api-example.md)
- **Community reports of the current rule's error text:**
  - "The `reasoning_content` in the thinking mode must be passed back to the API."
  - "Missing `reasoning_content` field in the assistant message at message index 2."
  - Clients such as Cursor reportedly fail on the second tool step because they drop the field. These are community sources, labelled as such. — [Cursor forum: deepseek-v4-pro error](https://forum.cursor.com/t/deepseek-v4-pro-error-report/159983/5); [manifest.build 400 guide](https://manifest.build/errors/deepseek/400-reasoning-content-required/); [horadecodar.com.br](https://horadecodar.com.br/?p=45722); [Zed commit fixing reasoner tool-call handling](https://git.secluded.site/zed/commit/5dd8561b06c8af4ee46f3aa8bcf839f208b8c7bf)
- **Anthropic-compatible endpoint:** `thinking` blocks are "Supported" and `redacted_thinking` is "Not Supported". The page says nothing about signatures or how to handle thinking in history. — [DeepSeek Anthropic API](https://api-docs.deepseek.com/guides/anthropic_api)

#### Moonshot Kimi
- **kimi-k3:** Preserved Thinking is always on. Pass "the complete assistant message returned by the API back to `messages` as-is (including `reasoning_content`)". The FAQ says this is required for multi-turn conversations and tool loops. — [Kimi thinking-model guide](https://platform.kimi.ai/docs/guide/use-kimi-k2-thinking-model)
- **kimi-k2.7-code / -highspeed:**
  - Preserved Thinking is always on and cannot be disabled. The client must keep every historical `reasoning_content` as-is.
  - `thinking.type:"disabled"` errors, as does any invalid `thinking.keep`. Omitting `keep` is treated as `"all"`. — same page
- **kimi-k2.6:** `thinking.keep` defaults to `null`, which ignores historical reasoning and lowers cost. `"all"` preserves it. — same page
- **Within one task:** "Keep all reasoning content from the tool-call loop and send it back"; the model chooses which parts it needs.
- **Cost and limits:**
  - Preserved reasoning stays in the context window and is billed.
  - `max_tokens` should be at least 16000 for tool workflows, because `reasoning_content` counts toward it.
  - The page documents no error for omitting `reasoning_content`. — same page
- **Model Studio on Kimi:** "需在每轮 assistant 消息中保留 reasoning_content 字段" ("keep the reasoning_content field in every assistant message"). — [Model Studio Function Calling](https://help.aliyun.com/zh/model-studio/qwen-function-calling)
- **K2-Thinking model card:**
  - 256K context; "stable tool-use across 200–300 sequential calls".
  - In its benchmarks, when tool results exceeded 256k, "we employ a simple context management strategy that hides all previous tool outputs".
  - The card does not say whether reasoning must be kept. — [Kimi-K2-Thinking model card](https://huggingface.co/moonshotai/Kimi-K2-Thinking)

#### Zhipu / Z.ai GLM
- **Interleaved thinking:** on by default. "thinking blocks should be explicitly preserved and returned together with the tool results." — [Z.ai Thinking Mode](https://docs.z.ai/guides/capabilities/thinking-mode)
- **Preserved Thinking (`"thinking": {"clear_thinking": false}`):**
  - Enabled by default on the Coding Plan endpoint. Disabled by default on the standard API, where `clear_thinking:false` must be set.
  - When enabled, "you must return the complete, unmodified reasoning_content back to the API", and "All consecutive reasoning_content blocks must exactly match the original sequence".
  - If blocks are reordered or edited, "performance may degrade and cache hit rates may be affected".
  - The page documents no explicit error for omitting them. — same page
- **Models:** Thinking is on by default for GLM-5.3, 5.3-FLASH, 5.2, 5.1, 5 and the GLM-4.7 series. GLM-5.3 and 5.3-FLASH have forced thinking that cannot be disabled. The `clear_thinking` example uses `glm-4.7`. — same page
- **GLM-4.7 model card:**
  - Names Interleaved, Preserved ("the model automatically retains all thinking blocks across multi-turn conversations" in coding-agent scenarios) and Turn-level Thinking.
  - The config pairs `"clear_thinking": false` with `enable_thinking: true`.
  - Preserved Thinking through `chat_template_kwargs` is supported only in SGLang.
  - The card does not say whether the template strips historical reasoning by default. — [GLM-4.7 model card](https://huggingface.co/zai-org/GLM-4.7)
- **GLM-4.6 model card:** no statement on historical reasoning handling. — [GLM-4.6 model card](https://huggingface.co/zai-org/GLM-4.6)

#### Alibaba Qwen (Model Studio / DashScope)
- **`preserve_thinking`:** "控制模型是否读取历史 assistant 消息中的 reasoning_content" ("controls whether the model reads reasoning_content in historical assistant messages").
  - When `true`, "这些思考内容会拼接到下一轮输入" ("this thinking is concatenated into the next-turn input").
  - The client must supply the historical assistant messages with their `reasoning_content`.
  - Once enabled, history reasoning "计入输入 Token 数量和计费" ("counts toward input tokens and billing").
- **Passing the parameter:** It is not an OpenAI field. Pass it via `extra_body` in Python, top-level in Node and in `parameters` for DashScope HTTP. The Java SDK does not support it.
- **Models and errors:** Supported models include Qwen3.8 Max/Omni, Qwen3.7 Max/Plus/Flash, Qwen3.6 Plus and some Kimi models. For Qwen3.8 Max, 3.7, 3.6 and Kimi, sending the parameter without history reasoning does not error. — [Model Studio Deep Thinking](https://help.aliyun.com/zh/model-studio/deep-thinking) (date field blank)
- **Qwen-Omni thinking with tools:** multi-turn requests must "将上一轮 assistant 消息的思考内容、回复和工具调用一起加入历史" ("add the previous assistant message's thinking, reply and tool calls to history together"). `preserve_thinking` defaults on for Qwen-Omni. — [Model Studio Function Calling](https://help.aliyun.com/zh/model-studio/qwen-function-calling)
- **Open-weight Qwen3:** On Model Studio, open-weight Qwen3 models in thinking mode support only streaming output. This comes from search snippets of Model Studio pages and was not fetched in full. — [Model Studio doc (search result)](https://help.aliyun.com/document_detail/2868565.html)
- **Another platform, for comparison only:** Huawei Cloud's MaaS doc says `reasoning_content` is an output field that should not be sent as input, and that multi-turn history should keep only `role` and `content`. — [Huawei Cloud MaaS](https://support.huaweicloud.com/intl/zh-cn/model-call-maas/model-call-005.html)

#### MiniMax
- **Keep the full assistant message:**
  - "In multi-turn function call conversations, the complete model response (i.e., the assistant message) must be append[ed] to the conversation history." For thinking-capable turns, "always append the full returned message to the history whether or not it contains thinking."
  - Omitting it means "subsequent conversation will lose context" (Anthropic), and keeping it is "essential for achieving the model's best performance" (OpenAI). The guide documents no API error for omission. — [MiniMax M2 function-call guide](https://platform.minimax.io/docs/guides/text-m2-function-call)
- **OpenAI-compatible endpoint:**
  - `reasoning_split: true` puts thinking in `reasoning_content`. With `false`, thinking stays in `content` inside `<think>` tags, "which must be preserved completely".
  - This applies to MiniMax-M2 through M2.7-highspeed, M3.1-Flash and M3.
  - M3.1-Flash cannot set `reasoning_split:false`, and its `reasoning_content` "must also be preserved completely".
  - Thinking tokens count toward `max_tokens`. Too small a value gives `finish_reason:"length"` with empty `content`. — [MiniMax OpenAI-compatible API](https://platform.minimax.io/docs/api-reference/text-openai-api)
- **Anthropic-compatible endpoint:** "When a response includes `thinking` blocks, preserve them unchanged in later turns, especially in tool-use conversations." The page does not mention signatures. — [MiniMax Anthropic-compatible API](https://platform.minimax.io/docs/api-reference/text-anthropic-api)
- **M2 model card:**
  - "MiniMax-M2 is an interleaved thinking model … it is important to retain the thinking content."
  - "the historical content is passed back in its original format."
  - "Do not remove the `<think>...</think>` part, otherwise, the model's performance will be negatively affected." — [MiniMax-M2 model card](https://huggingface.co/MiniMaxAI/MiniMax-M2)

#### Xiaomi MiMo
- **Chat Completions:**
  - In thinking mode the model returns `reasoning_content` alongside `tool_calls`, and "it is recommended to keep all previous `reasoning_content` in the `messages` array for each subsequent request." The example uses `mimo-v2.6-pro`.
  - The FAQ also warns that `tool_calls` appearing inside `reasoning_content` "indicates instability and incomplete output", and recommends turning thinking off when making tool calls.
  - The FAQ lists no error codes. — [MiMo FAQ: API integration](https://mimo.mi.com/static/docs/quick-start/faq/api-integration.md)
- **Responses API:** "it is recommended to keep all previous reasoning content in the `input` array for each subsequent request." The schema of reasoning items is not given, and `encrypted_content` is not mentioned. — [MiMo Responses API](https://mimo.mi.com/docs/en-US/api/chat/responses)
- **Model migration banner:** MiMo-V2.5-Pro and V2.5 requests "will auto-route to V2.6 on Oct 14, 2026", and old IDs become invalid on Oct 21, 2026, 10:00 UTC+8. — [MiMo Responses API](https://mimo.mi.com/docs/en-US/api/chat/responses)
- **MiMo on Model Studio:** In thinking mode each assistant message must include `reasoning_content` or the API returns an error, and `tool_choice` accepts only `auto`. This is from a search snippet of Alibaba's MiMo page, not fetched in full. — [Model Studio MiMo page](https://help.aliyun.com/en/model-studio/mimo)
- **Third-party claim, secondary:** a third-party guide claims that omitting `reasoning_content` in MiMo tool conversations can give a 400. — [Tabbit MiMo 2.5 Pro guide](https://go.tabbit.ai/mimo-2-5-pro-api)

### Inferences
- **What an editing client can and cannot change:**
  - For DeepSeek (tools present), Kimi k3 and k2.7-code, GLM preserved thinking, MiniMax and MiMo, the client may not freely strip reasoning from assistant turns it keeps.
  - It can elide or compact tool result contents, but DeepSeek gives a hard 400 if an assistant message in a tool-bearing request lacks `reasoning_content`.
  - The other vendors document quality loss, not errors.
- **Same model name, different contract:** The contract depends on the endpoint and the hosting platform. Kimi on Model Studio requires `reasoning_content` per assistant message, while kimi-k2.6 on Moonshot's own API ignores history by default. Qwen through Model Studio drops reasoning unless `preserve_thinking` is set. GLM differs between the Coding Plan and the standard API.
- **The R1-era rule has inverted:** "strip reasoning_content or get a 400" has become "return it whenever tools are present or get a 400". Any client written against the old rule now fails against current DeepSeek.
- **Edited reasoning:** GLM and MiniMax ask for byte-exact, ordered return of reasoning. So does Kimi ("as-is"). Edited or summarised reasoning is outside the documented contract, and GLM explicitly ties edits to quality and cache loss.

### Gaps
- No vendor page gives an exact current error string. The DeepSeek strings above are from community reports.
- Whether DeepSeek accepts truncated or edited `reasoning_content`, as opposed to an absent one, is undocumented.
- Whether Kimi, GLM, MiniMax or MiMo return an error, rather than degrade, when reasoning is omitted is undocumented.
- The official, live text of the old R1 400-on-include rule could not be retrieved. Only a mirror was found.
- The Z.ai Anthropic-compatible endpoint's thinking or signature rules were not found in the pages read.
- The open-weight Qwen3 chat-template handling of historical `<think>` content was not checked. No Qwen3 model card was fetched.

## Server-side history: stateful vs stateless APIs, and server-side truncation or compaction

### Takeaway
All of the Chat Completions and Anthropic-style endpoints are documented by example as client-resent history. None of the vendors states this explicitly as a rule. Only Qwen/Model Studio documents server-side conversation state: Responses `previous_response_id`, with response IDs valid for 7 days. MiMo's Responses API explicitly does not support `previous_response_id` or `context_management`. No vendor documents server-side truncation or compaction of history.

### Cited Findings
- **Qwen / Model Studio Responses API:**
  - "通过 previous_response_id 参数自动关联上下文，无需手动构建消息历史" ("previous_response_id automatically links context; no need to build message history manually").
  - "当前响应id有效期为7天" ("response IDs are valid for 7 days").
  - The page does not mention a `store` parameter, truncation or compaction.
  - Thinking is returned as `reasoning` output items. `reasoning.effort` takes precedence over `enable_thinking`, and `thinking_budget` is unsupported.
  - Supported models include Qwen3.5–3.8, DeepSeek-V4, GLM-5.2/5.3 and kimi-k3. Models not on the list get only basic compatibility. — [Model Studio Responses API compatibility](https://docs.bailian.console.aliyun.com/zh/model-studio/compatibility-with-openai-responses-api)
- **MiMo Responses API:** "Fields such as `background`, `previous_response_id`, and `context_management` are not currently supported. Carrying these parameters in a request will be ignored or trigger an error"; "undefined parameters will be filtered out and may cause request errors." — [MiMo Responses API](https://mimo.mi.com/docs/en-US/api/chat/responses)
- **MiniMax Anthropic-compatible endpoint:** `context_management` (and `container`, `mcp_servers`, `top_k`, `stop_sequences`) are marked "This parameter will be ignored." — [MiniMax Anthropic-compatible API](https://platform.minimax.io/docs/api-reference/text-anthropic-api)
- **Kimi:** The caching guide refers to Chat Completions, Responses and Messages APIs. Whether Kimi's Responses API supports `previous_response_id` was not verified. — [Kimi context caching](https://platform.kimi.ai/docs/guide/use-context-caching-feature-of-kimi-api)
- **Kimi K2-Thinking evaluation harness:** It hid all previous tool outputs when context exceeded 256k. This is client-side context management in an eval, not a server feature. — [Kimi-K2-Thinking model card](https://huggingface.co/moonshotai/Kimi-K2-Thinking)
- **Z.ai and DeepSeek:**
  - Z.ai's HTTP intro gives the general base URL `https://api.z.ai/api/paas/v4/`. Its multi-turn example resends prior messages, and the page does not explicitly describe statelessness. — [Z.ai HTTP introduction](https://docs.z.ai/guides/develop/http/introduction)
  - DeepSeek's examples likewise resend all messages. — [DeepSeek Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode)

### Inferences
- **The client owns history:** For every vendor except Qwen Responses, the client is the sole owner of history. Its edits take effect directly, and nothing is reconciled on the server.
- **Qwen Responses with `previous_response_id`:** server-held history is used, and the client cannot edit earlier items. Compaction there means abandoning the chain and sending a fresh `input`.
- **Ignored context management:** MiniMax and MiMo silently ignore (or reject) Anthropic/OpenAI context-management fields. A client relying on server-side tool-result clearing through those fields gets nothing.

### Gaps
- No vendor explicitly documents "stateless" semantics or the absence of server-side truncation. Behaviour on context overflow (error or silent truncation) is undocumented, apart from Z.ai's error 1261 "Prompt too long".
- The Kimi Responses API's `previous_response_id` and `store` support, and the DeepSeek and Z.ai Responses offerings (if any), were not checked.
- Z.ai's Coding Plan base URL is not given on the intro page.

## Caching: context caching or disk caching and its prefix rules

### Takeaway
Every vendor that documents caching uses exact-prefix matching. Any change at a position invalidates reuse from that point on.
- **DeepSeek:** automatic disk KV cache with "cache prefix units".
- **Kimi:** implicit cache with 5m/1h TTL tiers (k3 only for writes), plus a top-level `cache_control` on Messages.
- **Qwen:** implicit cache (always on) and explicit `cache_control` (5 min TTL, 125% write, about 10% read).
- **Z.ai:** automatic, about 50% price.
- **MiniMax and MiMo:** no caching doc found on their own platforms.

### Cited Findings

#### DeepSeek (disk KV cache)
- **Matching:** A hit needs to "**fully match** a **cache prefix unit**".
- **Where units are persisted:**
  - at the end of user input and of model output for each request;
  - at common prefixes detected across requests;
  - at fixed token intervals for long content.
- **Lifetime:** construction takes seconds, and unused caches clear "usually within a few hours to a few days".
- **Reporting:** usage fields `prompt_cache_hit_tokens` and `prompt_cache_miss_tokens`.
- **Behaviour:** on by default and best-effort, with no 100% hit guarantee.
- **Divergent prefixes:** In the doc's example, A+B then A+C misses, but A is persisted, so A+D hits.
- The page states no minimum token count and no pricing. — [DeepSeek KV cache guide](https://api-docs.deepseek.com/guides/kv_cache)
- **Anthropic-compatible endpoint:** `cache_control` is ignored on `text`, `tool_use` and `tool_result`. — [DeepSeek Anthropic API](https://api-docs.deepseek.com/guides/anthropic_api)

#### Kimi
- **Prefix rule:** "When any part of a prefix changes, the content after that position cannot be reused." Keep fixed content's order and text unchanged, and keep timestamps and random IDs out of the prefix.
- **Mode and TTL:**
  - The only documented mode is `prompt_cache_options.mode: implicit`, with a 5m default TTL and a `1h` option.
  - "A cache entry's TTL is locked at first write"; hits renew it.
- **Blocks:** Caching works in blocks; "a portion smaller than one full block cannot be written to cache". The block size is not stated.
- **Cache Write models:** only `kimi-k3` supports Cache Write; k2.7, k2.7-highspeed and k2.6 do not.
- **Messages API:** a top-level `cache_control` sets the TTL, and markers inside messages are ignored.
- **Reporting:**
  - Chat Completions: `prompt_tokens_details.cached_tokens` and `cache_write_tokens`.
  - Messages: `cache_read_input_tokens` and `cache_creation_input_tokens`.
  - Headers: `Msh-Usage-Cache-Write-Tokens-5m` and `-1h`.
- **k3 prices (per 1M tokens):** $3.00 miss, $3.00 for a 5m write, $6.00 for a 1h write, $0.30 for a hit. — [Kimi context caching](https://platform.kimi.ai/docs/guide/use-context-caching-feature-of-kimi-api)

#### Qwen / Model Studio
- **Two modes:** implicit cache ("无法关闭", "cannot be turned off") and explicit cache. The two are mutually exclusive per request.
- **Minimum size:** 1024 tokens, or 512 for the Zhipu-deployed GLM and MiniMax models.
- **Matching:**
  - Implicit: prefix match.
  - Explicit: "最长的匹配前缀" ("longest matching prefix"), with `cache_control: {"type":"ephemeral"}`, at most 4 markers and a look-back of 20 blocks. System, user, assistant and tool messages are cacheable, but tool definitions alone are not.
- **TTL:** explicit 5 minutes, refreshed on a hit. Implicit has no fixed TTL and is periodically cleared.
- **Billing:**
  - Explicit: 125% to create, about 10% on a hit.
  - Implicit: hits are usually 20%. Exceptions: deepseek-v4.1-flash 10%, GLM-5.3 25%, Kimi k3 10%.
- **Reporting:** OpenAI-compatible responses report `usage.prompt_tokens_details.cached_tokens` and `cache_creation_input_tokens`. Anthropic-compatible responses report `cache_read_input_tokens`.
- **Session cache:** The Responses API has a "Session cache". Its section was not captured. — [Model Studio Context Cache](https://help.aliyun.com/zh/model-studio/context-cache) (date field blank); [Responses compatibility](https://docs.bailian.console.aliyun.com/zh/model-studio/compatibility-with-openai-responses-api)

#### Z.ai
- **Mode:** automatic implicit caching that "intelligently identifies repeated context content".
- **Matching:** "identical or highly similar to previous requests"; "Minor formatting differences may affect cache effectiveness". The page gives no explicit prefix rule, minimum or TTL ("reasonable time limits").
- **Reporting and price:** usage field `prompt_tokens_details.cached_tokens`; priced at "usually 50% of standard price". — [Z.ai Context Caching](https://docs.z.ai/guides/capabilities/cache)
- **Thinking edits:** Editing or reordering preserved reasoning means "cache hit rates may be affected". — [Z.ai Thinking Mode](https://docs.z.ai/guides/capabilities/thinking-mode)

#### MiniMax
- The OpenAI-compatible and Anthropic-compatible API pages do not mention prompt caching. — [MiniMax OpenAI API](https://platform.minimax.io/docs/api-reference/text-openai-api); [MiniMax Anthropic API](https://platform.minimax.io/docs/api-reference/text-anthropic-api)
- When MiniMax is hosted on Model Studio, implicit caching applies there, with a 512-token minimum. — [Model Studio Context Cache](https://help.aliyun.com/zh/model-studio/context-cache)

#### MiMo
- The Responses API reports `usage.input_tokens_details.cached_tokens`; the sample shows 0. — [MiMo Responses API](https://mimo.mi.com/docs/en-US/api/chat/responses)
- No MiMo or Token Plan caching doc was found.
- Third-party listings for Token Plan show base URLs such as `https://token-plan-ams.xiaomimimo.com/v1` and list cache read and write cost as 0. These are secondary. — [pi.dev Token Plan listing](https://pi.dev/models/xiaomi-token-plan-ams/mimo-v2-5)
- The official FAQ defers to "The Base URL provided on the Token Plan page shall prevail." — [MiMo FAQ](https://mimo.mi.com/static/docs/quick-start/faq/api-integration.md)

### Inferences
- **Where a compaction starts paying full price:** Any edit to an earlier message invalidates the cache from the edit point onward with every vendor here. This covers eliding a tool result, summarising, or stripping or altering reasoning.
  - DeepSeek persists units at each request's end of input and end of output. Edits therefore cost the full miss price for everything after the earliest edited position.
  - Compacting into a stable new prefix and then appending only creates a new reusable prefix after the first post-compaction request. DeepSeek's A+D example shows this pattern.
- **Reasoning and the cached prefix:**
  - Where reasoning must be returned (DeepSeek with tools, GLM preserved, Kimi k3, MiniMax), the returned reasoning becomes part of the cached prefix. Byte-exact return is needed for both quality and hits.
  - Where history reasoning is ignored or dropped (DeepSeek without tools, Qwen without `preserve_thinking`, Kimi k2.6 default), whether the dropped text still affects the cache key is undocumented.
- **Cache markers do nothing on some endpoints:** Kimi Messages ignores `cache_control` inside messages, and DeepSeek Anthropic ignores it entirely. Anthropic-style `cache_control` breakpoints set by a client have no per-block effect there.

### Gaps
- DeepSeek's minimum token count, unit size, interval and cache pricing (not on the KV-cache page) were not found.
- Kimi's block size, and whether non-k3 models get cache reads, were not found.
- Z.ai's TTL and minimum, and what "highly similar" matching means, were not found.
- MiniMax native caching, and MiMo or Token Plan caching rules and pricing, were not found.
- No vendor documents whether stripped or ignored reasoning participates in the cache key.

## Errors: documented errors when history is edited

### Takeaway
DeepSeek is the only vendor with a documented, official history-edit error: a 400 when `reasoning_content` is missing from an assistant message in a tool-bearing request. Kimi documents errors for invalid `thinking` parameters, not for edited history. GLM, MiniMax and MiMo document only quality or cache degradation. Z.ai's error table has only generic parameter codes.

### Cited Findings
- **DeepSeek:**
  - Omitting required `reasoning_content` in tool-call requests means "the API will return a 400 error". No other codes are documented. — [DeepSeek Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode)
  - Community-reported strings: "The `reasoning_content` in the thinking mode must be passed back to the API" and "Missing `reasoning_content` field in the assistant message at message index 2". — [Cursor forum](https://forum.cursor.com/t/deepseek-v4-pro-error-report/159983/5); [manifest.build](https://manifest.build/errors/deepseek/400-reasoning-content-required/)
  - The manifest.build guide also claims that consecutive same-role messages are rejected. This is unverified and third-party. — [manifest.build](https://manifest.build/errors/deepseek/400-reasoning-content-required/)
- **Kimi:** k2.7-code errors on `thinking.type:"disabled"` and on invalid `thinking.keep`. The page documents no error for omitted `reasoning_content`. — [Kimi thinking-model guide](https://platform.kimi.ai/docs/guide/use-kimi-k2-thinking-model)
- **Z.ai:**
  - The error table has 1210, 1213, 1214, 1215 and 1261 "Prompt too long", with no tool or role-order codes. — [Z.ai error codes](https://docs.z.ai/api-reference/api-code)
  - Edited or reordered reasoning: "performance may degrade and cache hit rates may be affected" (no error). — [Z.ai Thinking Mode](https://docs.z.ai/guides/capabilities/thinking-mode)
- **Qwen:** For Qwen3.8 Max, 3.7, 3.6 and Kimi, sending `preserve_thinking` without history reasoning "does not error". — [Model Studio Deep Thinking](https://help.aliyun.com/zh/model-studio/deep-thinking)
  - Kimi models hosted on Model Studio give errors for unsupported `tool_choice` ("否则会报错", "otherwise an error is raised"). — [Model Studio Function Calling](https://help.aliyun.com/zh/model-studio/qwen-function-calling)
  - Model Studio's MiMo page reportedly errors if an assistant message lacks `reasoning_content` in thinking mode (search snippet). — [Model Studio MiMo](https://help.aliyun.com/en/model-studio/mimo)
- **MiniMax:** The pages document no API errors for omitting thinking; they say context is lost and performance degrades. — [MiniMax M2 function-call guide](https://platform.minimax.io/docs/guides/text-m2-function-call)
- **MiMo:**
  - The native FAQ lists no error codes. — [MiMo FAQ](https://mimo.mi.com/static/docs/quick-start/faq/api-integration.md)
  - The Responses API warns that unsupported or undefined fields "will be ignored or trigger an error". — [MiMo Responses API](https://mimo.mi.com/docs/en-US/api/chat/responses)

### Inferences
- **Silent degradation:** For most vendors, a history edit fails silently as reduced quality, not loudly. A client cannot rely on server errors to detect a broken reasoning chain.
- **Hosting changes the error behaviour:** The same model can error on one host and not another. MiMo and Kimi are documented to require `reasoning_content` on Model Studio. On their native APIs, MiMo only "recommends" it, and Kimi k2.6 ignores it by default.

### Gaps
- No exact official error strings or codes exist for any vendor's reasoning or pairing violations.
- The DeepSeek same-role rule is unverified.
- No vendor documents behaviour for tool messages without a matching call, or for calls without results.

## Starting fresh: what a client must do to start a fresh conversation from a compacted summary

### Takeaway
No vendor documents a compaction or "continue from summary" procedure. The rules found imply the following:
- A fresh request built from a summary (system/user text) plus new turns is valid everywhere, because the reasoning-return obligations attach only to assistant messages actually present.
- On Qwen Responses, the client must drop `previous_response_id`.
- On MiMo, server-side `context_management` is unavailable.
- Every vendor's prefix cache restarts at the summary.

### Cited Findings
- **DeepSeek:** The reasoning-return rule applies to assistant messages in subsequent requests when tools are present ("must be fully passed back to the API in all subsequent requests"). Without tools, it is ignored. — [DeepSeek Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode)
- **Qwen:** Server-held context is linked only via `previous_response_id`, with IDs valid for 7 days. — [Model Studio Responses](https://docs.bailian.console.aliyun.com/zh/model-studio/compatibility-with-openai-responses-api)
  - `preserve_thinking` reads reasoning only from historical assistant messages the client supplies. — [Model Studio Deep Thinking](https://help.aliyun.com/zh/model-studio/deep-thinking)
- **MiMo:** Responses lacks `previous_response_id` and `context_management`. — [MiMo Responses API](https://mimo.mi.com/docs/en-US/api/chat/responses)
- **MiniMax:** Anthropic-compatible `context_management` is ignored. — [MiniMax Anthropic API](https://platform.minimax.io/docs/api-reference/text-anthropic-api)
- **Kimi:** Its own K2-Thinking evals managed overflow by hiding all previous tool outputs, rather than by summarisation. — [Kimi-K2-Thinking model card](https://huggingface.co/moonshotai/Kimi-K2-Thinking)
- **Caching:** Any change to a prefix forfeits reuse after that position (Kimi). DeepSeek persists new prefix units after a request completes, so later requests can hit. — [Kimi context caching](https://platform.kimi.ai/docs/guide/use-context-caching-feature-of-kimi-api); [DeepSeek KV cache](https://api-docs.deepseek.com/guides/kv_cache)

### Inferences
- **Safest fresh start (all vendors):** a new `messages` array containing:
  - the stable system prompt and tool definitions, kept byte-identical for cache reuse;
  - the summary as system or user text, not as a synthetic assistant message;
  - the new user turn.

  With no prior assistant turns there is nothing to return. That satisfies DeepSeek's 400 rule and the Kimi, GLM and MiniMax "as-is" rules without fabricating reasoning.
- **Keeping recent turns verbatim after the summary:** the client must keep each such assistant message whole.
  - DeepSeek with tools needs `reasoning_content`, or it returns a 400.
  - Kimi k3 and k2.7-code, GLM preserved, and MiniMax `<think>` or `reasoning_content` need it unmodified.
  - Each `tool_calls` message must keep all of its `tool` results.

  Results may have shortened bodies. No vendor forbids shortening result content, and none documents it either.
- **Synthetic or edited reasoning:** writing a summary into an assistant message's `reasoning_content`, or editing reasoning, is outside every vendor's documented contract. GLM explicitly warns of degraded quality and cache.
- **Qwen Responses:** a compacted restart is a new chain with no `previous_response_id` and a full `input`.
- **Kimi k2.6:** with `thinking.keep: null` (the default), historical reasoning is ignored anyway, so dropping it costs nothing documented.

### Gaps
- No provider documents any compaction or summary API, recommended summary placement or role, or behaviour when a summary replaces earlier tool turns.
- Whether a synthetic assistant message without `reasoning_content` (for example a summary placed in the assistant role) triggers the DeepSeek 400 with tools present is not explicitly documented. By the stated rule it likely does.

## Note: Anthropic-compatible and OpenAI-compatible endpoints

Which rules apply on these endpoints was folded into the sections above. In summary:
- **The vendor's own rules ride on the emulated protocol.** The emulated protocol supplies the message shape and pairing, but each vendor overrides it with its own field support and reasoning rules.
- **DeepSeek `/anthropic`** (`https://api.deepseek.com/anthropic`):
  - `thinking` is supported; `redacted_thinking` is not.
  - `cache_control` and `is_error` are ignored.
  - Signatures are not mentioned. — [DeepSeek Anthropic API](https://api-docs.deepseek.com/guides/anthropic_api)
- **MiniMax `/anthropic`** (`https://api.minimax.io/anthropic`):
  - Thinking blocks are returned unchanged.
  - `context_management`, `container`, `mcp_servers`, `top_k` and `stop_sequences` are ignored.
  - Support for `cache_control` and `redacted_thinking` is not stated. — [MiniMax Anthropic API](https://platform.minimax.io/docs/api-reference/text-anthropic-api)
- **Kimi Messages API:** a top-level `cache_control` sets the TTL, and markers inside messages are ignored. — [Kimi caching](https://platform.kimi.ai/docs/guide/use-context-caching-feature-of-kimi-api)
- **MiMo:**
  - Paths: `/v1/chat/completions` and `/anthropic/v1/messages`.
  - Base URL: `https://api.xiaomimimo.com/v1`. Token Plan base URL comes from the console. — [MiMo FAQ](https://mimo.mi.com/static/docs/quick-start/faq/api-integration.md)
  - Responses API: `https://api.xiaomimimo.com/v1/responses`. — [MiMo Responses API](https://mimo.mi.com/docs/en-US/api/chat/responses)
- **Qwen:** offers OpenAI Chat, DashScope native, Anthropic-compatible (cache fields reported) and OpenAI Responses. Fields differ per surface; for example, implicit cache reporting location varies by region. — [Model Studio Context Cache](https://help.aliyun.com/zh/model-studio/context-cache)
- **Z.ai:** the Anthropic-compatible endpoint was not covered by the pages read (gap).
