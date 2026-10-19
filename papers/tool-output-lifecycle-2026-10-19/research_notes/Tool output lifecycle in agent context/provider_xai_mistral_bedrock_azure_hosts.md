# Tool-output and reasoning lifecycle on xAI, Mistral, Bedrock, Azure/Foundry, Cohere, Meta, and OpenAI-compatible hosts (Together, Fireworks, Groq, Cerebras)

Scope and method. Every page cited below was read on 2026-10-19. No API was called. Wherever a page showed a date, these notes give it. Most vendor pages show none: xAI, Mistral, Cohere, Meta, Fireworks, Together and Cerebras all lack dates, and AWS user-guide pages show none either. Each Microsoft Learn page carries an `ms.date`. When a page was fetched as raw Markdown (`.md` / `llms.txt`), the notes say so. The fetch tool summarised a few pages instead of returning them verbatim. For those pages, quoted strings are what the summariser reported as exact quotes, and they are flagged where it matters. Community reports appear only to illustrate observed errors, and they are labelled **[community]**.

Each section below covers the six aspects the coordinator asked for: **Pairing**, **Reasoning**, **Server-side history**, **Caching**, **Errors**, and **Starting fresh**.

---

## xAI (Grok): how does it handle tool outputs, reasoning and history?

### Takeaway
xAI points developers to an OpenAI-style Responses API. Chat Completions is labelled "Deprecated". Responses are stored for 30 days, and conversations chain with `previous_response_id`. A reasoning item is an opaque `encrypted_content` blob that is "recommended" to send back verbatim. xAI does not document an error for leaving it out. Cache hits depend on exact prefix matching: per xAI, dropping prior reasoning is "the top cause of cache misses". xAI also has a first-party `POST /v1/responses/compact` endpoint. It returns one opaque `compaction` item, which becomes the head of the next request.

### Cited Findings
**Pairing**
- In the Responses examples, a result returns as a `function_call_output` item that carries the `call_id` of the `function_call` it answers. The page never mentions a `tool` role or `tool_call_id`, and it states no pairing or ordering rule in prose. On parallel calls it says only "Process all of them before continuing." Parallel calling is on by default and turned off with `parallel_tool_calls: false`. A request may carry up to 350 tools. — [xAI Function Calling](https://docs.x.ai/docs/guides/function-calling)
- "With streaming, the function call is returned in whole in a single chunk." "Custom tools pause execution and return to you for handling." — [xAI Function Calling](https://docs.x.ai/docs/guides/function-calling)

**Reasoning**
- Encrypted reasoning "can be returned if you pass `include: ["reasoning.encrypted_content"]` to the Responses API". "You can send the encrypted content back to provide more context to a previous conversation." `grok-4.7` "returns `reasoning.encrypted_content` on every response, whether or not `include` lists it". Reasoning items should go back unchanged in the next `input`. The guidance is advisory; the page never says this is required. — [xAI Reasoning](https://docs.x.ai/docs/guides/reasoning)
- On `grok-4.7`, receiving the encrypted content "does not stop [the vendor] from storing the thinking trace server-side for rehydration". "Clients that ignore the field keep working as before." Response storage "is governed by `store`, not by this setting." Chat Completions "is unaffected; it has no field for the ciphertext." The page describes no signature scheme. Reasoning "cannot be disabled" on grok-4.7, 4.6 or 4.5. Only summaries of internal reasoning are exposed. — [xAI Reasoning](https://docs.x.ai/docs/guides/reasoning)
- (The reasoning page names the operator "SpaceXSI". These notes record that wording as read and do not interpret it.)

**Server-side history**
- Responses API: "Responses stored for 30 days". "Built-in support via `previous_response_id`". `store` defaults to true. Chat Completions is labelled "Chat Completions API (Deprecated)", "Legacy endpoint, limited updates", "Stateless — must resend full history", "No reasoning content returned". — [xAI model capabilities comparison](https://docs.x.ai/developers/model-capabilities/text/comparison)
- The inference reference, as summarised by search: "New responses will be stored for 30 days and then permanently deleted." `DELETE /v1/responses/{id}` exists. — [xAI REST reference: responses](https://docs.x.ai/developers/rest-api-reference/inference/responses)
- Compaction: `POST /v1/responses/compact` (OpenAI SDK `client.responses.compact`). The response has `object: "response.compaction"` and an `output` with "a **single** compaction item": `type: "compaction"`, a `cmp_` id, and `encrypted_content` that is "**opaque**", not to be parsed or modified. Usage reports `dropped_message_count`. "Compaction shrinks the conversation; it does not rescue an over-limit request." "At most one compaction per call." "Re-compacting is fine." The xAI SDK offers `chat.compact()`. For reasoning models, `use_encrypted_content=True` is "recommended... so the model's reasoning content from prior turns is preserved through the compaction". The page says nothing about how tool call/result pairs are handled inside a compaction, and nothing about `previous_response_id` / `store`. — [xAI Context compaction](https://docs.x.ai/developers/advanced-api-usage/context-compaction)

**Caching**
- Caching is automatic prefix caching: "The cache works from the start of your messages array." "Prompt caching is not 100% guaranteed". Entries may be evicted, and requests may land on other servers. — [xAI Prompt caching: how it works](https://docs.x.ai/developers/advanced-api-usage/prompt-caching/how-it-works)
- Routing: "The `x-grok-conv-id` HTTP header routes requests with the same conversation ID to the same server." On Responses, `prompt_cache_key` "functions identically". On gRPC the ID goes in metadata. No TTL or minimum is stated. — [xAI Maximizing cache hits](https://docs.x.ai/developers/advanced-api-usage/prompt-caching/maximizing-cache-hits)
- "Any change to earlier messages breaks the cache"; "never edit, remove, or reorder earlier messages — only append new ones." "For reasoning models, you must include reasoning_content from previous responses; omitting it is the top cause of cache misses." The page offers `previous_response_id` as the stateful alternative. — [xAI Prompt caching: multi-turn](https://docs.x.ai/developers/advanced-api-usage/prompt-caching/multi-turn)

**Errors**
- The only tool-related error documented is a 400 for a tool whose `parameters` root is not an object. xAI documents no error for broken call/result pairs or edited history. Over-limit requests fail with `context_length_exceeded`; the compaction page mentions it. — [xAI Function Calling](https://docs.x.ai/docs/guides/function-calling); [xAI Context compaction](https://docs.x.ai/developers/advanced-api-usage/context-compaction)

**Starting fresh**
- Send the compaction item "verbatim — do not modify" as the head of the next request, and append new turns "after it, never before". The original messages can then be dropped from client state. — [xAI Context compaction](https://docs.x.ai/developers/advanced-api-usage/context-compaction)

### Inferences
- The docs conflict about Chat Completions. The comparison page says it returns "No reasoning content", and the reasoning page says it has "no field for the ciphertext". Yet the multi-turn caching page tells users to "include reasoning_content from previous responses". The most likely reading is that the caching page means Responses-API reasoning items, or older Chat-Completions models such as grok-3-mini that exposed plain `reasoning_content`. Treat it as unresolved.
- A client-written summary has no first-party standing. The only documented fresh-start object is xAI's opaque compaction item. A client that summarises on its own can only start a plain new request; xAI documents no special form for that.

### Gaps
- No documented tool pairing or ordering validation, and no error strings for orphaned `function_call_output`.
- Undocumented: TTL and minimum cacheable prefix; how compaction treats tool pairs; whether compaction works with `store=false`.
- I could not tell whether `previous_response_id` past 30 days returns 404 or 400.

---

## Mistral: how does it handle tool outputs, reasoning and history?

### Takeaway
Mistral's Chat Completions is stateless and uses OpenAI-style `tool_calls` / `role:"tool"` + `tool_call_id`. The docs give a role-flow table, and in it parallel results follow the single assistant message that requested them. Reasoning output is plain `ThinkChunk` content, with no encryption or signature. The docs say to replay it: stripping it "significantly degrades output quality". A separate, stateful Conversations (Agents) API keeps history server-side (`store=False` opts out). Prompt caching works on 64-token blocks, with `prompt_cache_key`. No server-side compaction is documented.

### Cited Findings
**Pairing**
- A tool result message has `"role": "tool"`, `"name"`, `"content"` and `"tool_call_id": tool_call.id`. The page appends the full assistant `message` object, including `tool_calls`. Role flows in the table: single call is "assistant fc.1 → tool r.1 → assistant"; successive calls interleave; parallel calls are "assistant fc.1, fc.2 → tool r.1 → tool r.2 → assistant". "A model is allowed to followup a tool call with another tool call." `parallel_tool_calls` defaults to true. Tool errors are returned as ordinary JSON content. — [Mistral Function calling](https://docs.mistral.ai/capabilities/function_calling/)

**Reasoning**
- Reasoning output is a list of chunks: a `ThinkChunk` (`type: "thinking"`) holding `TextChunk`s, then a `TextChunk` answer. `reasoning_effort` `"high"` produces a full thinking chunk, while `"none"` omits it. Multi-turn: "always replay the full assistant message (including `ThinkChunk`) back into the message history"; "Stripping them increases token efficiency but significantly degrades output quality." No encryption or signature is mentioned. Models: `mistral-small-latest`, `mistral-medium-3-5`, `mistral-large-4-0`, `zai-glm-5-3`. `magistral-*` is deprecated. — [Mistral Reasoning](https://docs.mistral.ai/capabilities/reasoning/)

**Server-side history**
- Conversations API: "A new Conversation ID is provided at each append"; continuing needs `conversation_id` and `inputs`. You can "opt out from the automatic storing with `store=False`". Handoffs run `server` (default) or `client`. The page as read did not cover restart, function-result entries, truncation or compaction. — [Mistral Agents & Conversations](https://docs.mistral.ai/studio-api/agents/agents-api)
- A restart endpoint, `POST /v1/conversations/{id}/restart`, requires `from_entry_id` (422 if omitted) and returns a new conversation. **This is from a third-party mirror of the API reference, not an official page.** — [withone.ai mirror of Mistral API](https://www.withone.ai/knowledge/mistral-ai/conn_mod_def%3A%3AGMZh1TPX9mA%3A%3A7UWFzDWDR1G4sMBW5kzVOA)

**Caching**
- Set the same `prompt_cache_key` on requests likely to share a prefix. It "increases the chance of a cache hit, but it **doesn't guarantee one**". The cache works in 64-token blocks, so "Prompts with fewer than 64 prompt tokens do not have cache hits." `usage.prompt_tokens_details.cached_tokens` is "a multiple of 64". Cached tokens cost 10% of the input price. Caching helps less when "requests change the first part of the prompt on every call." No TTL is stated, and the examples use only `/v1/chat/completions`. — [Mistral Prompt caching](https://docs.mistral.ai/studio-api/conversations/advanced/prompt-caching)

**Errors**
- Mistral's own docs document no errors for broken pairs. **[community]** Observed 400s include: "Unexpected role 'user' after role 'tool'". After a tool message the next message must be assistant; Zed worked around this by inserting an empty assistant message. Another is "Expected last role User or Tool (or Assistant with prefix True)". A third rejects an assistant message with neither content nor tool_calls. — [Zed commit #31736](https://git.secluded.site/zed/commit/b8c1b54f9eaf78d3bcf073f6495cfaeb8b3bf2b4); [Portkey error library](https://portkey.ai/error-library/response-role-error-10573); [Zed commit #47579](https://git.secluded.site/zed/commit/49dee40073ac7c8ced737e0c154d4046eddde1eb)

**Starting fresh**
- No compaction primitive is documented. On Chat Completions a fresh start is just a new `messages` array. On Conversations it is a new conversation, or (per the mirror) a `restart` from an entry.

### Inferences
- Mistral's ordering is stricter than OpenAI's: tool → user is rejected (community-observed). An editor that elides a tool loop must keep a tool result followed by an assistant message, or remove the whole loop.
- Thinking chunks are plain text with no binding, so a client may edit or summarise them without a validation error. The documented cost is only quality and cache prefix.

### Gaps
- No official statement of error strings, of whether `tool_call_id` must match an earlier call, or of whether `name` is required.
- No official Conversations-API rules on function.result pairing, truncation, context limits or retention period. No caching TTL.

---

## Amazon Bedrock (Converse, InvokeModel for Anthropic, Responses): how does it handle tool outputs, reasoning and history?

### Takeaway
Bedrock has three surfaces with different rules:
- **Converse** is stateless and AWS-native. `toolUse` / `toolResult` blocks pair by `toolUseId`. `reasoningContent` is a union of `reasoningText` (text + `signature`) and `redactedContent`, which must be passed back "unmodified".
- **InvokeModel for Claude** carries Anthropic's Messages rules through essentially unchanged. That covers thinking-block preservation during tool use, context-editing betas and both server-side compaction betas. Compaction is explicitly **not** available on Converse.
- **Responses API** (bedrock-runtime / bedrock-mantle) is stateful (`store`, 30 days, `previous_response_id`), with documented Bedrock deviations.

Caching is explicit (`cachePoint`, chained in the order tools → system → messages) or implicit, varying by model.

### Cited Findings
**Pairing**
- The AWS user-guide tool-use page did not render for the fetch tool, so the official Converse pairing rules could not be quoted. The Anthropic-on-Bedrock examples show `tool_use` (id `toolu_bdrk_...`) answered by a user `tool_result` with a matching `tool_use_id`. — [Bedrock Claude extended thinking](https://docs.aws.amazon.com/bedrock/latest/userguide/claude-messages-extended-thinking.html)
- **[community]** Converse errors observed for broken pairs: "Expected toolResult blocks at messages.N.content for the following Ids" and "tool_use ids were found without tool_result blocks immediately after" ([Strands SDK issue #1077](https://github.com/strands-agents/sdk-python/issues/1077)). Results reportedly must form the leading run of the next user message: `[toolResult, text, toolResult]` is rejected ([particula.tech](https://particula.tech/blog/tool-use-ids-without-tool-result-blocks)). Splitting results across consecutive user messages also failed ([ZeroClaw patch](https://git.polymech.info/polymech/zeroclaw/commit/993ec3fba66f22de0cb8e823df32fa989d0b4a68.patch)).

**Reasoning (Converse)**
- `ReasoningContentBlock` is a UNION of `reasoningText` and `redactedContent`. `redactedContent` is "The content in the reasoning that was encrypted by the model provider for safety reasons." — [API_runtime_ReasoningContentBlock](https://docs.aws.amazon.com/bedrock/latest/APIReference/API_runtime_ReasoningContentBlock.html)
- `signature`: "A token that verifies that the reasoning text was generated by the model. If you pass a reasoning block back to the API in a multi-turn conversation, include the text and its signature unmodified." — [API_runtime_ReasoningTextBlock](https://docs.aws.amazon.com/bedrock/latest/APIReference/API_runtime_ReasoningTextBlock.html)

**Reasoning (Claude via InvokeModel; Anthropic rules carried over)**
- "During tool use, you must pass thinking blocks back to the API for the last assistant message. Include the complete unmodified block". If the thinking block is not passed in with the tool_use, "an error occurs". "The entire sequence of consecutive thinking blocks must match the outputs generated by the model during the original request; you cannot rearrange or modify the sequence." — [Bedrock Claude extended thinking](https://docs.aws.amazon.com/bedrock/latest/userguide/claude-messages-extended-thinking.html)
- Thinking blocks from earlier turns may be omitted. "The Anthropic API automatically ignores thinking blocks from previous turns and they are not included when calculating context usage." "When a non-tool-result user block is included, all previous thinking blocks are ignored and stripped from context." "If thinking becomes disabled, requests will fail if you pass thinking content in the current tool use turn." — same page
- Thinking block clearing (beta) uses `context-management-2025-06-27` with `clear_thinking_20251015`, `keep: {type: "thinking_turns", value: N}` (default 1). The page lists support for Sonnet 4/4.5, Haiku 4.5 and Opus 4/4.1/4.5. Interleaved thinking uses beta header `interleaved-thinking-2025-05-14`. — same page
- Newer Claude models on Bedrock (Fable 5.1, Mythos 5.1, Mythos 5, Fable 5) "do not support extended thinking"; `thinking.type: "enabled"`/`"disabled"` "will return a 400 error"; they use adaptive thinking only. — same page

**Server-side history**
- Converse and InvokeModel are stateless; the client sends the full `messages` each time.
- Responses API, from the endpoint pages: `store` defaults to true, and Bedrock "retains the response, including the input and output, for 30 days". With `store=false`, "`previous_response_id` cannot be used". Stored responses are scoped to a Project. The account retention mode `none` "rejects an explicit `store=true` outright". — [Bedrock Responses API](https://docs.aws.amazon.com/bedrock/latest/userguide/inference-responses-api.html)
- Behaviour on bedrock-runtime differs from OpenAI and mantle:
  - "`model` is required on every request, including one that supplies `previous_response_id`".
  - `background=true` is rejected with a 400.
  - Server-side tools are unavailable.
  - "A stored response belongs to the AWS Region that served it". An unknown, foreign or unstored response ID "returns the same 404 error in every case".
  - Cross-Region profiles can store data in any Region the profile routes to.
  — same page
- Claude compaction, `compact-2026-01-12` (`context_management.edits: [{type: "compact_20260112"}]`):
  - Default trigger is 150,000 tokens, with a minimum of 50,000. `pause_after_compaction` exists, and `instructions` "completely replaces" the default prompt.
  - "The API automatically drops all message blocks before the `compaction` block".
  - The page lists only Sonnet 4.6 and Opus 4.6.
  - Top-level usage excludes compaction, so sum `usage.iterations`.
  — [Bedrock Claude compaction](https://docs.aws.amazon.com/bedrock/latest/userguide/claude-messages-compaction.html)
- Claude compaction, `compact-2026-09-04` (top-level `compaction: {type: "summarize", instructions?}`):
  - The server returns "a single signed `compaction` block, alone, with `stop_reason: "compaction"`". "The server keeps no state between requests."
  - The beta flag goes "on the compaction request and on every later request that carries the returned block". "Neither mechanism is supported by the Converse API."
  - Instructions are capped at 16,384 chars and screened by a classifier. A flagged instruction gets a 200 with `stop_reason: "refusal"`. If the classifier is unreachable, the response is a 529, and screening "fails closed".
  - A compaction request returns a 400 if it is combined with `context_management`, `stop_sequences`, `output_config.format`, forced `tool_choice`, `output_config.task_budget.remaining` or `max_tokens: 0`.
  — same page

**Caching**
- Two types, implicit (best-effort) and explicit (checkpoints). "Changes to a prompt prefix in subsequent requests result in cache misses."
  - Minimums range from 512 to 4,096 tokens per checkpoint, evaluated cumulatively. Most Claude models allow 4 checkpoints.
  - TTL resets on each hit. Many models get 5 minutes; Claude 4.5+ also offers `"ttl": "1h"`, and sending `ttl` to a 5-minute-only model "can result in a `ValidationException`".
  - "Cache checkpoints are processed in this order: `tools` → `system` → `messages`… changing content in an earlier section invalidates the cache for later sections."
  - For Claude, simplified cache management looks back "approximately 20 content blocks" from a single breakpoint.
  - On GPT-5.6+ models, `prompt_cache_breakpoint` is used, the minimum TTL is 30 minutes, and "Converse doesn't support explicit caching for this model".
  — [Bedrock Prompt caching](https://docs.aws.amazon.com/bedrock/latest/userguide/prompt-caching.html)
- Thinking and cache: "Changes to thinking parameters (such as enabling, disabling, or altering the budget allocation) invalidate message cache breakpoints"; interleaved thinking "amplifies cache invalidation"; system and tools stay cached. — [Bedrock Claude extended thinking](https://docs.aws.amazon.com/bedrock/latest/userguide/claude-messages-extended-thinking.html)
- Compaction and cache: a `cache_control` breakpoint may sit on a compaction block. "When compaction is triggered, it can result in a cache miss on the subsequent request." — [Bedrock Claude compaction](https://docs.aws.amazon.com/bedrock/latest/userguide/claude-messages-compaction.html)

**Errors**
- Official, compaction 09-04: "compaction block content does not match its signature" / "invalid signature in compaction block" (400, altered block); "compaction: Extra inputs are not permitted" (400, no beta flag or unsupported model); "the compaction summary could not be signed; retry the request" (529). — [Bedrock Claude compaction](https://docs.aws.amazon.com/bedrock/latest/userguide/claude-messages-compaction.html)
- Over-context: "The system now returns a validation error if prompt tokens + max_tokens exceeds the context window size." — [Bedrock Claude extended thinking](https://docs.aws.amazon.com/bedrock/latest/userguide/claude-messages-extended-thinking.html)
- **[community]** Converse with Claude Sonnet 4.6 extended thinking (article dated 2026-06-28). An allowlist filter dropped `toolUse`/`toolResult` and left `reasoningContent` blocks adjacent. The errors observed: "`thinking` or `redacted_thinking` blocks in the latest assistant message cannot be modified", "Invalid `signature` in `thinking` block", and "The content field in the Message object at messages.1 is empty." The author's workaround was to merge adjacent reasoning text under one signature, which the author asserts "does not verify the text". This is the author's claim, not AWS's. — [DevelopersIO / Classmethod](https://dev.classmethod.jp/articles/bedrock-converse-extended-thinking-tool-use-validation-exception/)
- **[community]** Placing `toolUse` before `reasoningContent` in an assistant turn produced a 400; reasoning must come first. — [Bifrost issue #3688](https://github.com/maximhq/bifrost/issues/3688)

**Starting fresh**
- Converse: no compaction. The client builds a new `messages` array (e.g. a user message with its own summary). No special form is documented.
- InvokeModel / Claude, 09-04 beta: send the returned block "back byte-for-byte as the first block of its first user or assistant message, followed by the turns kept or added since compaction"; the adopting request omits `compaction`. If `tool_changes` is present, send it back unmodified, since it is covered by the signature. — [Bedrock Claude compaction](https://docs.aws.amazon.com/bedrock/latest/userguide/claude-messages-compaction.html)
- Responses API: start a request with no `previous_response_id`. Bedrock documents no Responses compaction endpoint (not found).

### Inferences
- **Proxying.** For Claude via InvokeModel, AWS republishes Anthropic's rules, often word for word: thinking preservation, consecutive-sequence immutability, compaction semantics and error strings. Model coverage, beta availability and some limits are Bedrock-specific; the compaction 01-12 list, for example, is only Sonnet/Opus 4.6. Converse is a translation layer: Anthropic `thinking`/`redacted_thinking` map to `reasoningText`/`redactedContent`. The community errors show Anthropic's validators still run underneath, with Anthropic-named messages such as "`thinking` … blocks … cannot be modified". So Anthropic's binding rules carry over through Converse even though AWS documents them only as "include the text and its signature unmodified".
- For OpenAI models, Bedrock's Responses API follows the OpenAI spec, with the documented exceptions: `model` required with `previous_response_id`, Region-bound storage, no background on runtime, Project scoping.

### Gaps
- The official Converse user-guide text on `toolResult` ordering, `status: "error"` and pairing could not be fetched.
- Not found: whether Converse accepts `redactedContent` from one model on another, or how Converse handles reasoning for GPT-OSS / OpenAI models.
- Not found: whether `include: ["reasoning.encrypted_content"]` and `/responses/compact` are supported on Bedrock's Responses API.

---

## Azure OpenAI / Microsoft Foundry: how does it handle tool outputs, reasoning and history?

### Takeaway
Azure OpenAI v1 serves the OpenAI Responses API with OpenAI semantics:
- Stored responses are kept 30 days and chained with `previous_response_id`.
- Stateless mode is `store=false` plus `include: ["reasoning.encrypted_content"]`.
- Compaction is supported both server-side (`context_management` with `compact_threshold`, and it works with `store=false`) and standalone (`POST /responses/compact`).

Prompt caching follows the OpenAI rules: a 1,024-token minimum, 128-token increments before GPT-5.6, and explicit breakpoints from GPT-5.6 on. Claude in Foundry is the Anthropic Messages API at `/anthropic/v1/messages`, and Microsoft defers semantics to Anthropic's docs. Context editing is listed. Compaction is not listed for the Azure-hosted version.

### Cited Findings
**Pairing**
- Function calling uses `{"type": "function_call_output", "call_id": item.call_id, "output": ...}` in a follow-up with `previous_response_id`. The pairing rule appears only in code; there is no prose rule. — [Azure Responses API how-to (ms.date 2026-08-18)](https://learn.microsoft.com/en-us/azure/ai-foundry/openai/how-to/responses)

**Reasoning**
- "When you use the Responses API in stateless mode (`store=false`), you must still preserve reasoning context across conversation turns. To do this, include encrypted reasoning items in your requests." "To retain reasoning items across turns, add `reasoning.encrypted_content` to the `include` parameter." The .NET sample comment reads "Don't use PreviousResponseId when not stored." — [Azure Responses API how-to](https://learn.microsoft.com/en-us/azure/ai-foundry/openai/how-to/responses)

**Server-side history**
- "By default, response data is retained for 30 days." Turns chain via `previous_response_id`, "Alternatively, you can manually carry forward output items in the next request." "Background mode requires `store=true`. Stateless requests are not supported." — same page
- Compaction: "Compaction reduces the input context while preserving essential state for later turns." "When the output token count crosses the configured threshold, the Responses API automatically runs compaction." "The response includes an encrypted compaction item." "Server-side compaction will work when you set store=false". "If you use `previous_response_id` chaining, do not manually prune." The example is `context_management=[{"type": "compaction", "compact_threshold": 200000}]`. Standalone compaction is `POST /responses/compact`. — same page (as summarised by fetch tool)
- Azure-specific: for Astra models, a safety system can end a Responses session with annotation code `misalignment-enforcement`, and "Once a conversation is stopped for this reason in the API, it is unable to be resumed." — same page

**Caching**
- "A minimum of 1,024 tokens in length." "The first 1,024 tokens in the prompt must be identical." On GPT-5.5 and earlier, hits after that come "in 128-token increments". "A single character difference in the first 1,024 tokens results in a cache miss". "Keep conversation context append-only." What is cached: the complete messages array plus tool definitions. — [Azure Prompt caching (ms.date 2026-08-11)](https://learn.microsoft.com/en-us/azure/ai-foundry/openai/how-to/prompt-caching)
- GPT-5.6+: `prompt_cache_key` is used, and more than about 15 rpm per prefix+key may miss. `prompt_cache_breakpoint: {"mode":"explicit"}` goes on `input_text`/`input_image`/`input_file` (Responses). Each request gets up to 4 writes. "Breakpoints from earlier conversation turns are read-only." Reads consider "up to the latest 50 breakpoints". `prompt_cache_options.ttl` accepts only `"30m"`, as a minimum. Older models use `prompt_cache_retention` (`in_memory` caches are "typically" cleared in 5–10 minutes and always within 1 h; `24h` is extended). Sending breakpoint/options to pre-5.6 models returns a 400. — same page

**Errors**
- Guardrail input blocks return a 400 `content_filter`. Mid-stream errors arrive as a 200 with an error event. A 404 means `model` must match the deployment name. Azure documents no errors for broken pairs; presumably OpenAI's apply. — [Azure Responses API how-to](https://learn.microsoft.com/en-us/azure/ai-foundry/openai/how-to/responses)

**Claude in Foundry (proxy of Anthropic)**
- Base URL `https://<resource>.services.ai.azure.com/anthropic`; endpoint `/anthropic/v1/messages` with `anthropic-version: 2023-06-01`; Anthropic SDK `AnthropicFoundry`. — [Deploy and use Claude in Foundry (ms.date 2026-09-21)](https://learn.microsoft.com/azure/ai-foundry/foundry-models/how-to/use-foundry-models-claude)
- The models come in two versions: "Hosted on Anthropic infrastructure" and "Hosted on Azure". The Anthropic-hosted version "supports more capabilities than the ones listed". The tables list automatic prompt caching, 5 m and 1 h caching, and "Context editing… including clearing tool results and managing thinking blocks. Requires the anthropic-beta header `context-management-2025-06-27`." Every feature row links to platform.claude.com docs for semantics. **Compaction is not listed.** The Files API is Anthropic-hosted only. Opus 5.5 rejects `thinking: disabled` (at effort above high) or `enabled` + `budget_tokens` with a 400. — [Claude models in Foundry (ms.date 2026-09-21)](https://learn.microsoft.com/en-us/azure/foundry/foundry-models/concepts/claude-models)

**Starting fresh**
- Feed the encrypted compaction item (from server-side or `/responses/compact`) into the next request's input, or chain with `previous_response_id` without pruning. A client-made summary can only start a new request; there is no special form.

### Inferences
- Azure OpenAI is an OpenAI-rules proxy. Microsoft's pages restate OpenAI semantics and add Azure-specific limits: deployment-name `model`, `content_filters` extension, PTU-M lacks breakpoints, the misalignment stop. Pairing errors and encrypted-reasoning binding should be the OpenAI ones. For Claude, Microsoft defers to Anthropic's docs. Because compaction is absent from the Foundry capability tables, check per deployment before relying on it, especially on Hosted-on-Azure.

### Gaps
- The `truncation` parameter, Conversations API and the exact compaction item schema were not in the captured page text.
- No Azure-specific error strings for orphaned `function_call_output`.

---

## Cohere: how does it handle tool outputs, reasoning and history?

### Takeaway
Cohere Chat v2 is stateless: v1's `conversation_id` and `prompt_truncation` were removed. Tool calls pair through `tool_call_id`, and the assistant turn carrying `tool_calls` plus `tool_plan` is appended to history. Reasoning models (`command-a-reasoning-08-2025`) return plain `thinking` content blocks. Cohere's pages say nothing about replaying thinking on later turns, and they document no prompt caching, no compaction and no pairing errors.

### Cited Findings
**Pairing**
- v2: "Append the tool call details (`tool_calls` and `tool_plan`) to the `messages` list"; "Append `tool_call_id` and `tool_content` to `messages`"; "Tool calls emit tool call IDs. This will help the model match tool results to the right tool call." — [Cohere migrating v1 to v2](https://docs.cohere.com/docs/migrating-v1-to-v2)
- Tool message `content` is a string or list of `{type: "document", document: {data, id?}}`. The sequence is user → assistant(tool_calls) → tool → assistant, and in multi-step use "steps 2 and 3 will run multiple times in loop". No ordering or error rules are stated. — [Cohere tool use overview](https://docs.cohere.com/docs/tool-use-overview)

**Reasoning**
- "For reasoning models, `thinking` is enabled by default". `thinking: {type: "disabled"}` turns it off, and `token_budget` caps it. The output contains "thinking" content blocks. The page says nothing on whether thinking should, must or must not be included in later turns or with tool calls. — [Cohere Reasoning](https://docs.cohere.com/docs/reasoning)
- **[community-adjacent]** Cohere Labs' official HF demo Space rebuilds chat history without thinking messages before each call. — [HF Space command-a-reasoning](https://huggingface.co/spaces/CohereLabs/command-a-reasoning/blob/9e6e7118a435a2d0f7b5d933276e763bcbb7b680/app.py)

**Server-side history**
- "`conversation_id` parameter (chat history is now managed by the developer via the `messages` parameter)" is unsupported in v2, and so is `prompt_truncation`. — [Cohere migrating v1 to v2](https://docs.cohere.com/docs/migrating-v1-to-v2); [Cohere v2 API release changelog](https://docs.cohere.com/changelog/v2-api-release)

**Caching / Errors / Starting fresh**
- I found no Cohere documentation of prompt caching, of errors for broken pairs, or of a compaction primitive. A fresh start is simply a new `messages` array.

### Inferences
- v2 has no server truncation and thinking is unsigned, so a client can edit history freely, subject only to whatever undocumented validation `tool_call_id` matching enforces.

### Gaps
- Pages carry no dates. Reasoning replay rules, caching and pairing validation errors are all undocumented.

---

## Meta (Llama API, now "Meta Model API"): how does it handle tool outputs, reasoning and history?

### Takeaway
`llama.developer.meta.com/docs` now redirects to `dev.meta.ai`, the **Meta Model API**. Its model list covers Muse Wonder / Muse Spark and other Muse models; no Llama models are listed. Of every platform in this set, it documents history editing most explicitly:
- Three wire formats: Responses (recommended), Chat Completions, and Anthropic-compatible Messages.
- Precise 400 rules for pairing and reasoning-item placement.
- Encrypted reasoning replay, which cannot be combined with `previous_response_id`.
- `truncation: "auto"` rejected: "Model API does not trim for you."
- Explicit client compaction guidance: whole turns only, and old reasoning items may be dropped.

Prompt caching is automatic.

### Cited Findings
(The `.md` pages were fetched raw via `curl` from the `dev.meta.ai/llms.txt` index on 2026-10-19 and carry no dates. A 302 redirect from `llama.developer.meta.com/docs/features/tool-calling` was observed.)

**Pairing**
- "Always include the full assistant message… with its `tool_calls` array before the `tool` message. Omitting it returns `HTTP 400`." On Chat Completions "each `tool` message must carry the `tool_call_id` of the call it answers". On Responses without `previous_response_id`, "every `function_call_output.call_id` must match a `function_call.call_id` in the same request; a mismatch returns `HTTP 400`". `call_id` must be 1–64 chars. "Tool name validation applies only to the most recent assistant turn". Historical calls to tools no longer in `tools` are fine "as long as those calls already have corresponding tool results in history." — [Meta Tool calling](https://dev.meta.ai/docs/tool-calling.md)
- Ordering: "When you use `previous_response_id`, the server does not enforce strict ordering of `function_call_output` items… When you build history manually… keep tool results in the same order as their corresponding tool calls." Parallel: "append one `tool` message per call… before the next request". `tool_choice` must be `"auto"`, and anything else returns a 400. — same page

**Reasoning**
- Chat Completions "cannot carry reasoning across turns for external API keys". The `reasoning_content` field is "**redacted to empty**". — [Meta Reasoning](https://dev.meta.ai/docs/reasoning.md)
- Responses: reasoning replay is encrypted-only, through `include: ["reasoning.encrypted_content"]`, which works with `store` true or false. Hard rules:
  - "`include: ["reasoning.encrypted_content"]` cannot be combined with `previous_response_id`… returns `HTTP 400` (`param: "include"`)".
  - "Every reasoning input item must include a `summary` field (send `summary: []`…)". The `id` is optional on input.
  - "Every `reasoning` item in the `input` array must be followed by an assistant message or a `function_call` before the next `user`, `system`, or `developer` message", or the request gets a 400.
  - Intermediate assistant text before a `function_call` must carry `phase: "commentary"`, or it gets a 400.
  — [Meta Responses API](https://dev.meta.ai/docs/protocols/responses.md)
- The Messages adapter maps `encrypted_content` to `redacted_thinking`: "replay it to preserve chain of thought". The adapter "is stateless. It always runs with storage off". `thinking: disabled` returns a 400. — [Meta Messages API](https://dev.meta.ai/docs/protocols/messages.md)

**Server-side history**
- `store: true` is the default, and `previous_response_id` "appends your new input to stored history". "`previous_response_id` carries conversation turns only… Top-level `instructions` does not persist… resend `instructions`… `tools` and `reasoning.effort`." Deletion is a soft delete. Persistence can lag `response.completed`, giving a brief 404 / 400 on chaining, so retry. No retention period is stated. — [Meta Responses API](https://dev.meta.ai/docs/protocols/responses.md)
- "Model API does not trim for you: `truncation: "auto"` is rejected (`HTTP 400`, only the default `"disabled"` is accepted) and an over-context request fails with `HTTP 400`. You compact history yourself". — same page

**Caching**
- "Model API uses **prefix caching**… only the tokens after the first difference are computed from scratch." It is automatic, with "no cache key, set a flag, or mark breakpoints". `prompt_cache_key` is optional and improves routing. "Prompt caching is independent of the Responses API `store` parameter." `prompt_cache_retention` (`in_memory`/`24h`) is only a hint. — [Meta Prompt caching](https://dev.meta.ai/docs/prompt-caching.md); [Meta Responses API](https://dev.meta.ai/docs/protocols/responses.md)

**Errors (official strings)**
- "function_call_output call_id 'call_xyz' does not match any function_call call_id in the same request" (`param: "call_id"`).
- "Invalid conversation structure: reasoning items must be followed by an assistant message or function call before a new user, system, or developer message".
- `Invalid reasoning ID format` (400). Duplicate reasoning ids are rejected.
- Bare `rs_` id without content: "Referenced reasoning item ... was not found or has expired".
- Context: "You passed {N} input tokens and requested {M} output tokens. However, the model's context length is only…".

— [Meta Error handling](https://dev.meta.ai/docs/error-handling.md); [Meta Responses API](https://dev.meta.ai/docs/protocols/responses.md)

**Starting fresh / compaction**
- "Encrypted reasoning items are opaque: you cannot summarize or rewrite one, only keep it whole or drop it. Compact at the level of whole turns."
- "Keep recent reasoning, drop old… The server does not require a reasoning item to travel with its message or tool call, so dropping one is allowed."
- "every retained item must carry its `encrypted_content`… you must preserve `function_call` / `function_call_output` pairing and valid conversation structure."
- The alternative is to "use `store: true` with `previous_response_id` and let the server reconstruct history".

— [Meta Responses API](https://dev.meta.ai/docs/protocols/responses.md)
- Meta publishes no server compaction endpoint. Its cookbook shows OpenCode's harness compaction ("replaces the old middle of the session with a summary note… and keeps the recent turns") as the fallback. — [Meta cookbook: multi-turn context management](https://dev.meta.ai/docs/cookbook/multi-turn-context-management.md)

### Inferences
- Meta is the only platform here that both forbids server-side truncation and documents exactly which client edits are legal: drop whole turns or reasoning items, never rewrite them, and keep pairs. It is a near-ideal reference for a client-compaction validator.
- The original "Llama API" product appears superseded, and these notes cannot confirm that any Llama model is still served there.

### Gaps
- No stated retention period for stored responses.
- No first-party compaction endpoint.
- No statement on whether a `reasoning` item may be replayed on a different Muse model.

---

## OpenAI-compatible hosts (Together, Fireworks, Groq, Cerebras): how do they handle tool outputs, reasoning and history?

### Takeaway
All four use OpenAI Chat Completions pairing (`tool_calls` → `role:"tool"` + `tool_call_id`) and stateless history. The real variation is in how plain-text reasoning (`reasoning_content` / `reasoning`) is re-rendered by the chat template on later turns:
- **Fireworks** has `reasoning_history` (disabled / interleaved / preserved).
- **Cerebras** has `clear_thinking` (on Qwen).
- **Together** follows each model's own rules: preserved thinking for Kimi K3 and GLM; DeepSeek drops reasoning after non-tool turns.
- **Groq** recommends omitting prior thinking for Qwen and forbids `raw` reasoning with tools.

None of the four signs or encrypts reasoning. All cache on exact rendered-prefix matches, so template-driven stripping of old reasoning shifts the prefix. Only Fireworks offers a stateful Responses API (30-day `store`). Groq's Responses API is stateless.

### Cited Findings
**Together AI**
- Prefix = "the prompt exactly as the model sees it, after the model's chat template renders your request". That includes tools and rendered settings such as `reasoning_effort`. Caching is automatic, and "Cache entries typically last about an hour" (best effort). "Summarizing, truncating, or reordering the history changes the prefix from the first edited message onward." "Pass reasoning back unchanged… Editing or reordering it changes the prefix." `prompt_cache_key` routes requests and doubles as a stickiness key on dedicated endpoints. — [Together Prompt caching](https://docs.together.ai/docs/inference/chat/prompt-caching.md)
- Interleaved thinking is "The default mode". Preserved thinking is enabled with `clear_thinking: false`: "include the unmodified `reasoning_content` from previous turns". "all consecutive `reasoning_content` blocks must exactly match the original sequence… Don't reorder or edit these blocks. Otherwise, performance may degrade and cache hit rates will drop." The field name (`reasoning` or `reasoning_content`) varies by model: "pass it back under the same key". — [Together Reasoning](https://docs.together.ai/docs/inference/chat/reasoning.md)
- Kimi K3: "trained in preserved thinking history mode… Return the complete assistant message on every turn, `reasoning_content` included. If you keep only `content`, generation quality becomes unstable." A system message with dynamic tools must have empty content, or the request gets "400: a system message with dynamic tools must have empty content". — [Together Kimi K3 quickstart](https://docs.together.ai/docs/kimi-k3-quickstart.md)
- DeepSeek V4 Pro: "If the assistant's response included tool calls, pass both the previous answer and the CoT (`reasoning_content`) back… if there was **no tool call**, the prior CoT… is **not** included in the next turn's context." — [Together DeepSeek V4 quickstart](https://docs.together.ai/docs/deepseek-v4-quickstart.md)
- No Responses API appears in Together's docs index, and no compaction primitive is documented. — [Together llms.txt](https://docs.together.ai/llms.txt)

**Fireworks AI**
- "When building multi-turn tool-calling agents with models that support interleaved thinking, you **MUST** include the `reasoning_content` from previous assistant turns". "Interleaved thinking is triggered when the last message in your API request has `"role": "tool"`". Models that don't support it ignore the field. If you omit it, "the model will not have access to its previous reasoning process." — [Fireworks Reasoning](https://docs.fireworks.ai/guides/reasoning.md)
- `reasoning_history` values: `null` (the template default; for GLM-4.7, "historical reasoning is cleared by default"), `disabled` ("Strip `reasoning_content` from all messages"), `interleaved` ("Strip `reasoning_content` from messages up to (and including) the last user message"), and `preserved`. — [Fireworks Chat Completions API reference](https://docs.fireworks.ai/api-reference/post-chatcompletions.md)
- Caching: "Prompt caching works only for exact prefix matches"; "For function calling models, tools are considered part of the prompt"; "Prompt caching only works within 1 replica", so send `user` or `x-session-affinity`. Separation tools are `x-prompt-cache-isolation-key` and `reset_prompt_cache`. Entries "usually stay in the cache for at least several minutes". — [Fireworks Prompt caching](https://docs.fireworks.ai/guides/prompt-caching.md)
- Responses API: "By default responses are stored", and conversations chain with `previous_response_id`. "Responses created with `store=False` cannot be referenced by `previous_response_id`." Deletion is permanent. Retention is 30 days with `store=True` (per the docs index description of the data-handling page). Supported fields include `truncation` and `prompt_cache_key`. The only supported `include` value is `message.output_text.logprobs`, so there is no encrypted reasoning. — [Fireworks Responses API](https://docs.fireworks.ai/guides/response-api.md); [Fireworks llms.txt (data handling entry)](https://docs.fireworks.ai/llms.txt)

**Groq** (console.groq.com returned 403 to both fetch and curl, so these findings come from search-result extracts of the official pages)
- `reasoning_format` takes `parsed` (the separate `message.reasoning` field), `raw` or `hidden`. It "must be set to either parsed or hidden when using tool calling or JSON mode", and `raw` there returns a 400. `include_reasoning` is mutually exclusive with `reasoning_format`. The Qwen model pages say to include "only the final output" from previous turns, "not the thinking content". — [Groq Reasoning](https://console.groq.com/docs/reasoning); [Groq Qwen 3.6 27B](https://console.groq.com/docs/model/qwen/qwen3.6-27b); [Groq API reference](https://console.groq.com/docs/api-reference)
- Responses API (beta): `previous_response_id`, `store`, `truncation`, `include` and `prompt_cache_key` are unsupported. It "doesn't support stateful conversations yet". `store` accepts only `false`/`null`. — [Groq Responses API](https://console.groq.com/docs/responses-api); **[community]** Groq staff: they do not store previous requests or responses — [Groq Community](https://community.groq.com/t/support-for-previous-response-id-in-responses-api/652)
- Prompt caching "works automatically… no code changes"; "Cached data automatically expires after 2 hours without use". It is limited to a model table that could not be read. — [Groq Prompt caching](https://console.groq.com/docs/prompt-caching)

**Cerebras**
- `reasoning_format`: `parsed` (`message.reasoning`), `raw`, or `hidden`, with per-model support. On Qwen, "Chat Completions is stateless… When you include an earlier assistant message with its `reasoning` field, `clear_thinking` controls whether Qwen receives that historical reasoning": `false`/omitted (the default) preserves it, while `true` "Removes historical assistant reasoning… final-answer content remains". Kimi "does not implement `clear_thinking`, so do not send it". Do not send Qwen-native flags (`preserve_thinking`, `enable_thinking`). — [Cerebras Reasoning](https://inference-docs.cerebras.ai/capabilities/reasoning.md)
- Caching: "works automatically"; "processes prompts in 128-token blocks"; "We guarantee a Time-To-Live (TTL) of 5 minutes, though caches may persist up to 1 hour"; "the entire beginning of your prompt must match *exactly*… Even a single character difference… cache miss for that block and all subsequent blocks". The prefix includes tool definitions. — [Cerebras Prompt caching](https://inference-docs.cerebras.ai/capabilities/prompt-caching.md)
- Tool pairing: `role: "tool"` with `tool_call_id: call.id`, and `parallel_tool_calls` defaults to true. No pairing error strings are documented. — [Cerebras Tool calling](https://inference-docs.cerebras.ai/capabilities/tool-use.md)

### Inferences
- On these hosts, reasoning content is plain text. Its "binding" is behavioural: model quality and cache prefix. It is not cryptographic. An editor can drop or summarise it without validation errors, except where a vendor validates format (Groq `raw` with tools).
- In `interleaved` or template-default modes (Fireworks GLM-4.7, Together DeepSeek, Cerebras `clear_thinking: true`), the template strips reasoning from turns before the last user message. The rendered prefix therefore changes when a new user turn arrives, even under append-only history. The `preserved` modes keep the prefix append-stable, and Together says preserved thinking improves "cache hit rates".
- These hosts proxy open-weight models, so the reasoning-replay rule is really the model vendor's (Moonshot, Z.AI, DeepSeek, Qwen) exposed through host-specific knobs. The host rule and the model rule can differ, as with Kimi K3's "must replay" against DeepSeek's "dropped after non-tool turns".

### Gaps
- Groq's pages could not be read directly; its cache model list and pairing errors remain unverified.
- No host documents error strings for orphaned `tool` messages or missing results.
- No host offers server-side compaction.
- Together's and Cerebras' Responses API support was not found and is probably absent.

---

## Cross-platform: where do hosts merely proxy another vendor's rules, and do they carry over unchanged?

### Takeaway
Bedrock InvokeModel (Claude) and Foundry (Claude) proxy Anthropic's Messages rules almost verbatim. Bedrock Converse translates them but keeps Anthropic's validators underneath. Azure OpenAI, and Bedrock's Responses API for OpenAI models, proxy OpenAI's Responses rules with documented local deviations. The OpenAI-compatible hosts proxy the open-weight model's chat-template conventions through host-specific parameters.

### Cited Findings
- Bedrock republishes Anthropic's thinking rules, compaction semantics and compaction error strings, with Bedrock-specific model lists and the Converse exclusion. — [Bedrock Claude extended thinking](https://docs.aws.amazon.com/bedrock/latest/userguide/claude-messages-extended-thinking.html); [Bedrock Claude compaction](https://docs.aws.amazon.com/bedrock/latest/userguide/claude-messages-compaction.html)
- Foundry links every Claude capability to platform.claude.com, and the Anthropic-hosted version "supports more capabilities". Compaction is not listed. — [Claude models in Foundry](https://learn.microsoft.com/en-us/azure/foundry/foundry-models/concepts/claude-models)
- Bedrock Responses deviates from OpenAI: `model` is required alongside `previous_response_id`, storage is Region-bound, there is a uniform 404, and storage is Project-scoped. — [Bedrock Responses API](https://docs.aws.amazon.com/bedrock/latest/userguide/inference-responses-api.html)
- Meta's Messages adapter is "Anthropic Messages-compatible" but "stateless… storage off". It maps Meta's `encrypted_content` to `redacted_thinking`, so the Anthropic shape carries a Meta blob. — [Meta Messages API](https://dev.meta.ai/docs/protocols/messages.md)

### Inferences
- A client cannot assume an Anthropic or OpenAI compaction feature exists on a proxy. On Bedrock, Anthropic compaction exists only on InvokeModel. Foundry does not list it. Azure documents OpenAI compaction. Bedrock does not document OpenAI `/responses/compact`.
- Encrypted or signed reasoning blobs are platform-bound. xAI's compaction blob "only works with xAI's API" (search summary of the xAI compaction page). Meta's blobs map into Anthropic shapes but are Meta ciphertext. Portability across hosts is undocumented everywhere.

### Gaps
- No platform documents cross-host or cross-model portability of encrypted reasoning or compaction items.
