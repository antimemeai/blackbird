# Provider-native mechanisms and protocol constraints for editing or eliding tool results

All pages below were read on 2026-10-19 through WebFetch or WebSearch. No API was called. None of the official pages fetched shows a "last updated" date, so the read date is the only date available. Community sources are labelled as such. They show errors people have observed, not documented contracts.

## Q1. Anthropic context editing (clear_tool_uses, clear_thinking), compaction and the memory tool

### Takeaway
Anthropic runs server-side context editing under beta header `context-management-2025-06-27`. It has two strategies, `clear_tool_uses_20250919` and `clear_thinking_20251015`, and puts a placeholder in place of each cleared result. Because the server edits its own copy, the client's history stays unchanged. That is why server-side clearing never invalidates preserved thinking, while the same edit made by the client does. The newer mechanisms are server compaction: threshold compaction `compact_20260112` (beta `compact-2026-01-12`) and on-demand compaction (beta `compact-2026-09-04`, a top-level `compaction` parameter). The fetched pages list no `clear_tool_uses` version newer than `_20250919`.

### Cited Findings
**Context editing basics**
- Context editing is beta and is enabled with header `anthropic-beta: context-management-2025-06-27`. The request body is `context_management.edits[]`. Strategies are `clear_tool_uses_20250919` (clears the oldest tool results, and optionally inputs, past a threshold) and `clear_thinking_20251015` (manages thinking blocks from earlier assistant turns). The page says "Context editing is available on all supported Claude models." — [Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)

**`clear_tool_uses_20250919` parameters** — [Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)
- `trigger`: `{type: "input_tokens" | "tool_uses", value: N}`, default 100,000 input tokens.
- `keep`: `{type: "tool_uses", value: N}`, default 3. "The API removes the oldest tool interactions first."
- `clear_at_least`: `{type: "input_tokens", value: N}`, no default. "If the API can't clear at least the specified amount, the strategy will not be applied."
- `exclude_tools`: a list of tool names whose uses and results are never cleared.
- `clear_tool_inputs`: boolean, default `false`. By default only results are cleared; `true` also clears the tool_use parameters.

**What replaces a cleared result**
- "The API replaces each cleared result with placeholder text indicating to Claude that it was removed." The exact placeholder string is not published. — [Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)

**`clear_thinking_20251015`**
- `keep` is `{type: "thinking_turns", value: N>0}` or `"all"`, and the default depends on the model.
- Models that keep all prior thinking by default: Opus 4.5 and later, Sonnet 4.6 and later, Haiku 5.5 and later, and all Fable/Mythos models. Opus 4.1, Sonnet 4.5, Haiku 4.5 and earlier keep only the last turn.
- "If your code runs across multiple model tiers, set `keep` explicitly."
- "When using multiple strategies, the `clear_thinking_20251015` strategy must be listed first in the `edits` array."
- Source: [Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)

**Response reporting**
- Responses report `context_management.applied_edits[]`, for example `{type:"clear_tool_uses_20250919", cleared_tool_uses: 8, cleared_input_tokens: 50000}` and `{type:"clear_thinking_20251015", cleared_thinking_turns: 3, cleared_input_tokens: 15000}`.
- When streaming, these arrive in the final `message_delta`.
- `count_tokens` accepts `context_management` and returns `input_tokens` (after clearing) plus `context_management.original_input_tokens`.
- Source: [Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)

**Prompt-cache interaction**
- Tool-result clearing "Invalidates cached prompt prefixes when content is cleared. To account for this, clear enough tokens to make the cache invalidation worthwhile. Use the `clear_at_least` parameter." Each clearing costs a cache write; later requests reuse the new prefix.
- For thinking clearing: when blocks are kept "the prompt cache is preserved"; when cleared, "the cache is invalidated at the point where clearing occurs."
- Source: [Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)

**Thinking interaction**
- "Your client keeps the full, unmodified history, so you don't need to sync client state."
- "On Claude Fable 5.1, Claude Opus 5.5, Claude Sonnet 5.5, and Claude Haiku 5.5, server-side context management never invalidates thinking blocks. Client-side edits to earlier turns can invalidate the thinking blocks in every later assistant turn. For new accounts created on or after August 31, 2026, a request that replays an invalidated block is rejected unless you opt into dropping it."
- Source: [Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)

**Memory tool and context editing**
- The memory tool type is `memory_20250818`. When context approaches the clearing threshold, "Claude receives an automatic warning to preserve important information." This lets Claude save results to memory files before they are cleared. — [Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)
- The memory tool is client-side: Claude requests file operations and your application executes them. The tool entry is `{"type": "memory_20250818", "name": "memory"}` and needs no beta header.
- Its commands are `view` (with optional `view_range`), `create`, `str_replace`, `insert`, `delete` and `rename`, all restricted to `/memories`.
- It is available on "all Claude 4 and later models". The API auto-injects a memory protocol into the system prompt ("ASSUME INTERRUPTION: Your context window might be reset at any moment").
- Source: [Memory tool](https://platform.claude.com/docs/en/agents-and-tools/tool-use/memory-tool)
- **Wording conflict:** the memory-tool page says "Context editing clears specific tool results on the client." The context-editing page says editing is applied on the server before the prompt reaches Claude. The context-editing page is the authoritative description. — [Memory tool](https://platform.claude.com/docs/en/agents-and-tools/tool-use/memory-tool) vs [Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)

**Deprecated SDK client compaction**
- SDK-side compaction (`compaction_control` in the TypeScript and Ruby tool runners) is deprecated, and Python removed it in v1.0. Anthropic recommends the server-side `compact_20260112` edit.
- The SDK's client compaction measures `input_tokens + cache_creation_input_tokens + cache_read_input_tokens + output_tokens`, which can misfire after server tools such as web search.
- "When the SDK triggers compaction while a tool use response is pending, it removes the tool use block from the message history before generating the summary."
- Source: [Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)

**Threshold compaction: `compact_20260112`**
- Beta header `compact-2026-01-12`.
- Parameters: `trigger` (only `input_tokens`; default 150,000; minimum 50,000), `pause_after_compaction` (default false; when true, `stop_reason: "compaction"`), and `instructions` (replaces the default prompt completely).
- Supported models: Fable 5.1, Mythos 5.1, Fable 5, Mythos 5, Mythos Preview, Opus 5.5, Opus 5, Opus 4.8/4.7/4.6, Sonnet 5.5, Sonnet 5, Sonnet 4.6 and Haiku 5.5.
- "When the API receives a `compaction` block, all content blocks before it are ignored."
- Usage is reported in `usage.iterations[]`, and compaction iterations are excluded from the top-level counts.
- A `cache_control` breakpoint can sit on the compaction block. The docs recommend a breakpoint at the end of the system prompt so it stays cached across compactions.
- On Fable 5.1, Mythos 5.1, Opus 5.5, Sonnet 5.5 and Haiku 5.5: "thinking blocks from before a `compaction` block aren't carried forward". Re-inserted turns must have their thinking stripped or use `drop_block`, otherwise "the continuation request is rejected with a 400 error."
- Source: [Compaction at a token threshold](https://platform.claude.com/docs/en/build-with-claude/compaction-threshold)

**On-demand compaction**
- Beta `compact-2026-09-04` with a top-level `compaction` parameter.
- The returned block is placed "first in `messages`, in place of the messages it summarizes".
- It supports keeping recent turns word for word, background compaction, and kept turns retaining thinking under stated conditions. Anthropic says to "Use on-demand compaction wherever it is available."
- Source: [Compaction overview](https://platform.claude.com/docs/en/build-with-claude/compaction)

### Inferences
- For Blackbird on Anthropic, `clear_tool_uses_20250919` with `clear_at_least`, a stable `exclude_tools` list, and a `keep` matched to the working set is the only documented way to elide old tool results without breaking preserved thinking on the 5.x models. The client keeps exact originals, which fits Blackbird's preserve-originals rule. The server's placeholder text is not under client control.
- The placeholder string is unpublished, so the model sees server text Blackbird cannot audit. Blackbird's final-request audit cannot reproduce the exact model-visible context byte for byte. The audit can record the `applied_edits` counts and the request's `context_management` configuration instead.

### Gaps
- The exact placeholder text for a cleared result is undocumented.
- The pages do not say whether clearing happens per request or persists across requests. The doc implies it is recomputed per request, because the client sends the full history.
- No `clear_tool_uses` version after `_20250919` appeared on the fetched pages as of 2026-10-19.

## Q2. Anthropic Messages rules: tool_use/tool_result pairing, thinking preservation, and editing a tool_result later

### Takeaway
Every `tool_use` needs a `tool_result` in the immediately following user message, and the results must come before any text. Within a tool-use turn, the thinking blocks must come back complete and unmodified. On Fable 5.1, Opus 5.5, Sonnet 5.5 and Haiku 5.5, the "preserved thinking" prefix check also binds each thinking block to everything before it. Anthropic explicitly says that "Clearing or shortening an earlier `tool_result`" is "Invalid for every later thinking block." Client-side placeholder replacement is therefore not safe on those models unless the client also drops every later thinking block, or opts into `drop_block`.

### Cited Findings
**Pairing and ordering**
- "Tool result blocks must immediately follow their corresponding tool use blocks in the message history. You cannot include any messages between the assistant's tool use message and the user's tool result message."
- "In the user message containing tool results, the tool_result blocks must come FIRST in the content array. Any text must come AFTER all tool results."
- The documented error string is "tool_use ids were found without tool_result blocks immediately after".
- `tool_result.content` may be a string, a list of `text`/`image`/`document`/`search_result` blocks, or omitted entirely (an empty result is shown as valid). `is_error` is optional.
- Computer/browser member results must echo `toolset_name`.
- Source: [Handle tool calls](https://platform.claude.com/docs/en/agents-and-tools/tool-use/handle-tool-calls)

**Thinking in tool loops**
- "Required: within a tool-use turn, pass thinking blocks back. Recommended: across turns, pass everything back. Allowed: outside tool use, omit prior turns' thinking."
- "Within the latest assistant message, the sequence of consecutive `thinking` blocks must match what the model generated ... you can't rearrange, edit, or partially drop them. This includes `redacted_thinking` blocks."
- "Modified thinking blocks are rejected with a 400 error."
- Source: [Thinking](https://platform.claude.com/docs/en/build-with-claude/thinking)
- "Echo the assistant message exactly as received: rebuilding the message or filtering out `redacted_thinking` blocks triggers a 400 error." — [Thinking in tool and multi-turn workflows](https://platform.claude.com/docs/en/build-with-claude/thinking-tool-workflows)
- With `display: "omitted"`, the default on the 5.x models, the `thinking` field is empty and "The `signature` field still carries the encrypted full thinking." — [Thinking](https://platform.claude.com/docs/en/build-with-claude/thinking)

**Per-model preservation of prior turns**
- Opus 4.5+, Sonnet 4.6+, Haiku 5.5 and Fable/Mythos keep all prior turns' thinking, which is billed as input.
- Earlier models keep only the last turn: "When you pass older thinking blocks back, the API strips them automatically."
- On last-turn-only models, "once you send a user message that isn't a tool result, all previous thinking blocks are stripped from context."
- Source: [Thinking](https://platform.claude.com/docs/en/build-with-claude/thinking)

**Preserved-thinking prefix check (Fable 5.1, Opus 5.5, Sonnet 5.5, Haiku 5.5)**
- The checked prefix is the top-level `system`, `tools`, and every message before the block. Not in the prefix: `effort`, `max_tokens`, `output_config`, `tool_choice`, `metadata`, `thinking.display` and `cache_control` markers.
- "Claude Mythos 5.1 and models before Claude Fable 5.1 don't run the prefix check."
- Source: [Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)

**What counts as an invalidating edit**
- "Clearing or shortening an earlier `tool_result`, re-encoding an earlier image, or changing an earlier `tool_use` input" is "Invalid for every later thinking block."
- Also invalid: editing, reordering or deleting earlier messages; changing `system` or `tools`; adding or removing a text block in an earlier user turn; and removing a middle thinking block while keeping later ones. Kept blocks must be "an unbroken run of the original sequence".
- Source: [Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)

**What stays valid**
- Appending at the end.
- Removing thinking blocks "from the start (oldest first), the end, or all of them" ("the model loses that reasoning").
- Adding, moving or removing `cache_control` markers.
- Server-side compaction or context editing ("the check compares what you sent, not the server's edited copy").
- Source: [Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)

**Anthropic's recommended remedy**
- "Shorten a tool result or downscale an image before the first time you send it, not after. To clear old results later, trim context on the server with `clear_tool_uses_20250919`." — [Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)

**Enforcement controls**
- `thinking.block_binding.prefix_mismatch_behavior` takes `"error"` (default) or `"drop_block"`. With `drop_block`, the API drops "each failing block and every thinking block after it, and the request succeeds."
- The field requires beta `thinking-binding-controls-2026-08-01`. Without the beta header, setting it returns `block_binding: Extra inputs are not permitted`.
- Accounts created on or after 2026-08-31 00:00 UTC get `"error"` enforcement by default. Older accounts are enforced only when the field is set.
- Source: [Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)
- Error text: "messages.1.content.0: Invalid `signature` in `thinking` block. The block is bound to a different conversation. Remove the block, or set `thinking.block_binding.prefix_mismatch_behavior` to "drop_block"."
- A tampered signature always returns a 400, and `drop_block` does not apply to it.
- In Anthropic's testing, `drop_block` on every turn raised output tokens by 67.1% at max effort. Anthropic calls it a stopgap.
- Responses carry `input_transformations` (log `prefix_binding_mismatch`).
- Source: [Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)
- Sonnet 5.5 and Haiku 5.5 blocks are also account-bound (`organization_binding_mismatch`). — [Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)

**Client compaction**
- "Simple compaction" means summarizing the whole session into one user message, so no thinking is replayed. Keeping a tail of turns after a client rewrite fails the check: "The kept turns' thinking blocks were produced while the removed history was still in place, so they fail the check." — [Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)

**Community reports**
- Third-party write-ups (Apidog, MemoryLake) report the same "bound to a different conversation" 400 and note that it coincides with cache restarts. — community, via [search results](https://apidog.com/blog/claude-fable-5-1-preserved-thinking/)

### Inferences
- On prefix-checked models, a protocol-valid client-side elision of an old tool_result at position p must also drop every thinking block after p. Thinking blocks before p remain valid, since their prefix is unchanged. Under the rule that kept blocks form an unbroken run from the start, that drop is allowed.
- Such an elision cannot fall inside an in-progress tool-use turn without violating "Required: within a tool-use turn, pass thinking blocks back." The safe client-side point is a user-turn boundary, and it costs all later reasoning. The cost is also measurable in output tokens (the 67.1% figure).
- On pre-Fable-5.1 models and Mythos 5.1, the prefix check does not run. Replacing an old tool_result's `content` with a shorter string while keeping `tool_use_id` and position is protocol-valid there. Changing it still breaks the prompt cache from that point. The docs do not explicitly bless editing a tool_result after the fact; they only list the pairing and order rules.
- A placeholder must stay a `tool_result` with the same `tool_use_id`, in the first positions of the same user message. Removing the block entirely would trigger the "tool_use ids were found without tool_result blocks" error.

### Gaps
- No official statement says whether a pre-5.1 model treats edited tool_result content differently from original content, beyond the cache effect.
- The full text of the "keep-tail compaction" and "custom compaction on the client" subsections was beyond the fetched window (the page is 122k characters; 100k were read).

## Q3. OpenAI Responses: truncation, compaction, state, encrypted reasoning, and replacing function_call_output

### Takeaway
OpenAI offers `truncation: "auto"` (drops items from the start of the conversation), server-side compaction through `context_management: [{type: "compaction", compact_threshold}]`, and a stateless `/responses/compact` endpoint. Each produces an opaque encrypted compaction item. The official docs require that "all items between the last user message and your function call output" be passed "untouched". They set no rule against changing older `function_call_output` content, and they publish no thinking-style prefix binding. The pairing errors seen in practice are documented only in community threads.

### Cited Findings
**`truncation`**
- `auto`: if the input exceeds the context window, the model drops items from the beginning of the conversation. `disabled` is the default: an oversize request fails with a 400. This is per the openai-python SDK docstring generated from the OpenAPI spec. — [openai-python response_create_params.py](https://raw.githubusercontent.com/openai/openai-python/main/src/openai/types/responses/response_create_params.py)
- **Conflict:** a search snippet reported an older SDK docstring that said items are dropped "in the middle". The current docstring says "beginning". — [search result, play.hs-kl.de copy of response.py](https://play.hs-kl.de/ki/python/venvs/openai/types/responses/response.py) (secondary)
- A community report says `auto` truncated inputs above about 75k down to about 72k, well below the advertised window. No resolution was posted. — [OpenAI community, Ultra Aggressive Truncation Behavior](https://community.openai.com/t/ultra-aggressive-truncation-behavior/1376390) (community)
- "Do not combine configuration updates with automatic compaction or automatic truncation." `/responses/compact` rejects histories that contain `configuration_update` items. — [Reasoning guide](https://developers.openai.com/api/docs/guides/reasoning)

**Server-side compaction**
- `context_management: [{"type": "compaction", "compact_threshold": 200000}]`. "When the rendered token count crosses the configured threshold, the server runs server-side compaction." "No separate `/responses/compact` call is required in this mode."
- The compaction item is encrypted and "opaque and not intended to be human-interpretable."
- Source: [Compaction guide](https://developers.openai.com/api/docs/guides/compaction)
- The SDK says `context_management.type` "Currently only 'compaction' is supported." — [openai-python](https://raw.githubusercontent.com/openai/openai-python/main/src/openai/types/responses/response_create_params.py)

**`/responses/compact` endpoint**
- "This endpoint is fully stateless and ZDR-friendly."
- "The window you send to `/responses/compact` must still fit within your model's context window."
- "The returned window is the canonical next context window, so pass it into your next `/responses` call as-is." The returned window may include retained items as well as the compaction item.
- Source: [Compaction guide](https://developers.openai.com/api/docs/guides/compaction)

**Pruning rules**
- With input-array chaining, you can drop items before the most recent compaction item.
- "If you use `previous_response_id` chaining, do not manually prune."
- Server compaction is "ZDR-friendly when you set `store=false`."
- Source: [Compaction guide](https://developers.openai.com/api/docs/guides/compaction)

**`previous_response_id` and `store`**
- With `previous_response_id`, "all previous input tokens for responses in the chain are billed as input tokens."
- Responses are stored for 30 days by default, and `store: false` disables storage.
- Source: [Conversation state](https://developers.openai.com/api/docs/guides/conversation-state)
- `previous_response_id` "Cannot be used in conjunction with `conversation`." — [openai-python](https://raw.githubusercontent.com/openai/openai-python/main/src/openai/types/responses/response_create_params.py)

**Encrypted reasoning**
- In stateless mode (`store: false` or ZDR), "reasoning items in the response's `output` array include an `encrypted_content` property by default." The legacy `include: ["reasoning.encrypted_content"]` "is still accepted ... but doesn't require it." — [Reasoning guide](https://developers.openai.com/api/docs/guides/reasoning)
- The SDK docstring still describes `include: reasoning.encrypted_content` as what "allows reasoning items to be used in multi-turn conversations when ... `store` is `false`." — [openai-python](https://raw.githubusercontent.com/openai/openai-python/main/src/openai/types/responses/response_create_params.py)

**Reasoning with function calls**
- Pass back "any reasoning items returned with the last function call". After several calls in a row, pass all reasoning, function_call and function_call_output items since the last user message.
- "Our systems will smartly ignore any reasoning items that aren't relevant to your functions."
- If you truncate manually, "ensure all items between the last user message and your function call output are passed into the next response untouched."
- Source: [Reasoning guide](https://developers.openai.com/api/docs/guides/reasoning)

**Cross-turn reasoning rendering**
- Models before GPT-5.6 do not render earlier-turn reasoning by default. GPT-5.6 renders available reasoning by default.
- `current_turn` "does not render reasoning from earlier turns into the next sample".
- "When you switch model families, the API omits incompatible reasoning."
- Source: [Reasoning guide](https://developers.openai.com/api/docs/guides/reasoning)

**`function_call_output` schema**
- `{type: "function_call_output", call_id, output}`, where `output` is a JSON string or an array of text, image or file content. — [Responses create reference](https://developers.openai.com/api/reference/resources/responses/methods/create)

**Observed pairing errors (community, not in the official docs)**
- "Item 'rs_…' of type 'reasoning' was provided without its required following item". Reported causes are replaying a reasoning item without the item it preceded, or leaving a reasoning item last in the input.
- Reported workarounds are replaying reasoning with id plus encrypted_content, or dropping reasoning replay entirely.
- Sources: [community 1151686](https://community.openai.com/t/how-to-solve-badrequesterror-400-item-rs-of-type-reasoning-was-provided-without-its-required-following-item-error-in-responses-api/1151686), [community 1280238](https://community.openai.com/t/responses-api-error-with-type-reasoning-provided-without-its-required-following-item/1280238), [fantasy fix commit](https://git.secluded.site/fantasy-mirror/commit/dff62fadc1831c5cf13e3c05aafa8040a6bcacf6) (all community)
- The reverse error, "'function_call' was provided without its required 'reasoning' item", has also been reported. — [community 1236046](https://community.openai.com/t/responses-api-invalid-request-error-function-call-was-provided-without-its-required-reasoning-item/1236046) (community)
- "No tool output found for function call" (a call without its output) and "No tool call found for function call output with call_id" (an orphan output) have been reported, often with `previous_response_id` or Conversations.
- The Continue project strips orphaned `function_call_output` items before sending.
- Sources: [community 1354672](https://community.openai.com/t/openai-responses-api-no-tool-output-found-for-function-call-when-using-previous-response-id-anyone-have-a-stable-workaround/1354672), [community 1142327](https://community.openai.com/t/issue-with-new-responses-api-400-no-tool-call-found-for-function-call-output-with-call-id/1142327), [Continue commit](https://gitea.varghacsongor.hu/GithubMirror/continue/commit/02525d119b1a150e04428ec565e9626b0ebafb36) (community)

### Inferences
- **What a placeholder must preserve:** a stateless (`store: false`) harness that replaces an old `function_call_output.output` with a shorter placeholder string must keep the item itself, the `call_id`, its position after its `function_call`, and the adjacency of each reasoning item to the item that followed it. With those intact, nothing in the official docs forbids the edit. The documented "untouched" constraint applies only to the span from the last user message to the latest function_call_output.
- **What it breaks:** the edit breaks prompt-cache reuse from that point onward.
- **`previous_response_id` chaining:** under this chaining, client-side editing of earlier items is impossible, since the server holds them. Combined with "do not manually prune", this means an eliding harness should use stateless input arrays.
- **Undocumented signature binding:** OpenAI does not document whether encrypted reasoning is bound to the exact prior content the way Anthropic's thinking signatures are. Absence from the docs does not prove there is no such binding.

### Gaps
- No official OpenAI page lists the exact error strings for broken reasoning adjacency or a missing call output. Only community threads do.
- No official statement says whether changing an earlier `function_call_output` affects how later encrypted reasoning is used.
- Supported models for server compaction are not listed. Examples use `gpt-5.3-codex` and `gpt-6-astra`.
- The live Responses API reference page is 506k characters. The `truncation` text was taken from the SDK docstring rather than the reference page.

## Q4. OpenRouter, Paper Instruments (Ultramarine Responses) and MiMo (Chat)

### Takeaway
None of the three documents a server-side tool-result clearing or compaction feature comparable to Anthropic's or OpenAI's. OpenRouter's Responses endpoint is strictly stateless. Its only server-side reduction is the "context compression" (middle-out) plugin, which is on by default only for endpoints of 8k context or less and gives no tool-pairing guarantee. Paper Instruments rejects `truncation: auto`, compaction and encrypted reasoning, and returns 400 `invalid_value` for unmatched function results. MiMo only recommends keeping `reasoning_content` across tool turns.

### Cited Findings
**OpenRouter**
- The Responses API is stateless: "Requests that set `store: true` or a non-null `previous_response_id` are rejected with a `400` error." The page does not mention compaction or context management. — [OpenRouter Responses overview](https://openrouter.ai/docs/api/reference/responses/overview)
- Reasoning details: "the entire sequence of consecutive reasoning blocks must match the outputs generated by the model during the original request." You "cannot rearrange or modify the sequence of these blocks." The example comments read "Pass back unmodified."
- Blocks are `reasoning.text` (optional `signature`), `reasoning.encrypted` (`data`), and summaries tagged with `format` (e.g. `anthropic-claude-v1`, `openai-responses-v1`).
- The page does not state what error results from modification.
- Source: [OpenRouter reasoning tokens](https://openrouter.ai/docs/guides/best-practices/reasoning-tokens)
- The Responses reasoning page shows a reasoning item with `encrypted_content`. Its multi-turn example replays only assistant message items and does not say reasoning must be replayed. — [OpenRouter Responses reasoning](https://openrouter.ai/docs/api/reference/responses/reasoning)
- Tool calling: tools "must be included in every request ... so the router can validate the tool schema on each call". The example pairs `tool_call_id` with the call `id`. The page says nothing about the errors that follow broken pairing. — [OpenRouter tool calling](https://openrouter.ai/docs/guides/features/tool-calling)
- Context compression plugin: it works by "removing or truncating messages from the middle of the prompt". Over a message limit it keeps half the messages from the start and half from the end.
- "All OpenRouter endpoints with 8k (8,192 tokens) or less context length will default to using context compression." Disable it with `plugins: [{"id": "context-compression", "enabled": false}]`.
- The page makes no tool call/result pairing guarantee.
- Source: [OpenRouter message transforms](https://openrouter.ai/docs/guides/features/message-transforms)

**Paper Instruments (Ultramarine Responses)**
- `store` defaults to false. `previous_response_id` requires `store: true`.
- "Only `"disabled"` is accepted" for `truncation`. `include` must be `[]`: "Ultramarine rejects encrypted reasoning." Reasoning cannot be disabled.
- Plaintext reasoning before an assistant message or function call may be replayed.
- "Unmatched function results return `400 / invalid_value`." With `store: false`, send "a result for every function call."
- Compaction and input-token counting return `400 / unsupported_parameter` (`error.param: operation`).
- Prompt caching: `prompt_cache_key` hint, "Cache hits are not guaranteed", optional `x-session-id` sticky routing, and `usage.input_tokens_details.cached_tokens`.
- Source: [Paper Instruments Ultramarine Responses](https://docs.paperinstruments.com/docs/ultramarine/responses)

**MiMo (Chat Completions)**
- Tool messages use `role: "tool"` with a required `tool_call_id`. Assistant `tool_calls` carry `id`, `type: function`, `function.name` and `function.arguments`.
- In thinking mode the model returns `reasoning_content` with `tool_calls`. The page recommends that you "keep all previous reasoning_content in the messages array for each subsequent request". This is framed as best performance, not a requirement, and the page documents no error for omitting it.
- The page documents no compaction or context management. Caching is visible only as `prompt_tokens_details.cached_tokens`.
- Source: [MiMo OpenAI-compatible API](https://mimo.mi.com/static/docs/api/chat/openai-api.md)

### Inferences
- On all three, eliding old tool outputs is a client-side job. The documented hard constraints are structural: every call keeps a matching result (`call_id` or `tool_call_id`), and reasoning sequences replay unmodified and in order.
- Replacing an old result's text with a placeholder violates neither constraint on paper. It does reset prefix caching from that point.
- OpenRouter's Anthropic-model routes presumably inherit Anthropic's preserved-thinking prefix check for Fable 5.1, Opus 5.5 and other 5.x models, since signatures are passed through as `reasoning.text.signature`. OpenRouter does not document this.
- Blackbird should keep the context-compression plugin explicitly disabled. Its middle-out removal could orphan tool calls on small-context models.

### Gaps
- I found no OpenRouter statement on whether it validates `tool_call_id` pairing itself or passes failures through from upstream.
- I found no Paper Instruments or MiMo statement on whether editing an earlier tool result affects replayed reasoning.
- None of the three pages carries a date.

## Q5. Prompt-cache implications of rewriting older items

### Takeaway
Every provider documented here caches by exact prefix. Rewriting an older tool result therefore forfeits the cache from that item onward and costs a fresh cache write for the remainder. That is why Anthropic tells you to batch clearing with `clear_at_least`. On Anthropic 5.x models the same rewrite also invalidates later thinking: "the edits that invalidate thinking are the edits that restart the cache."

### Cited Findings
**Anthropic**
- "Cache prefixes are created in the following order: `tools`, `system`, then `messages`." "Changes at each level invalidate that level and all subsequent levels."
- "Cache hits require 100% identical prompt segments." "Changing any block at or before the breakpoint produces a different hash on the next request."
- Breakpoints: up to 4, with 20-position lookback per breakpoint, where "a run of consecutive `tool_use` blocks counts as one position, and so does a run of consecutive `tool_result` blocks."
- Automatic top-level `cache_control` uses one slot.
- Minimum cacheable length is 512 tokens on the 5.x models. TTL is 5 minutes by default; `ttl: "1h"` costs 2x for writes.
- "Dropped thinking blocks" invalidate the messages level on Fable 5.1, Mythos 5.1, Opus 5.5, Sonnet 5.5 and Haiku 5.5.
- Source: [Prompt caching](https://platform.claude.com/docs/en/build-with-claude/prompt-caching)
- "The same discipline keeps the prefix stable for prompt caching: the edits that invalidate thinking are the edits that restart the cache." — [Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)
- "A thinking block the API drops ... changes the cached prefix from that block's position onward." Changing the thinking or effort configuration starts the cache over. — [Thinking](https://platform.claude.com/docs/en/build-with-claude/thinking)
- Context editing "Invalidates cached prompt prefixes when content is cleared". Use `clear_at_least` so each invalidation is worthwhile. — [Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)
- After a compaction, put a breakpoint at the end of `system` so that "Only the compaction summary needs to be written as a new cache entry." — [Compaction at a token threshold](https://platform.claude.com/docs/en/build-with-claude/compaction-threshold)

**OpenAI**
- "Cache reuse requires the entire rendered prefix to match." "Put stable developer instructions and shared reference material first."
- Minimum is 1,024 tokens on GPT-5.6+.
- "Compaction can reduce cache reuse."
- Source: [OpenAI prompt caching](https://developers.openai.com/api/docs/guides/prompt-caching)
- `prompt_cache_options` applies to GPT-5.6 and later. `mode` is `implicit` (default: one implicit breakpoint plus up to 3 explicit) or `explicit` (up to 4 explicit; none means no caching). Matching considers up to the latest 80 breakpoints with no lookback limit. `ttl` is `30m`, the only value. `prompt_cache_breakpoint` goes on content blocks. — [openai-python](https://raw.githubusercontent.com/openai/openai-python/main/src/openai/types/responses/response_create_params.py)
- On GPT-5.6+, `prompt_cache_key` mainly separates accounting: "Keys influence routing; they do not pin requests to a machine or guarantee a cache hit." — [OpenAI prompt caching](https://developers.openai.com/api/docs/guides/prompt-caching)

**OpenRouter**
- Anthropic routes allow up to four explicit breakpoints plus automatic top-level caching. OpenAI routes are automatic, with explicit breakpoints on GPT-5.6+.
- Sticky provider routing is keyed by a hash of the first system/developer message and the first non-system message, or by `session_id` / `x-session-id`. It expires after 10 minutes idle.
- For Gemini, "keep the initial portion of your message arrays consistent between requests."
- Source: [OpenRouter prompt caching](https://openrouter.ai/docs/guides/best-practices/prompt-caching)

### Inferences
- **Cost profile:** the cheapest client-side elision cadence is rare and batched, like `clear_at_least`. Each rewrite at depth d re-writes everything after d at cache-write rates (Anthropic 1.25x for 5-minute entries, 2x for 1-hour entries). Elision that marches one item forward per turn would cost a near-full cache write every turn.
- **Write placeholders at a stable point:** placing a placeholder at the moment an output first enters context (Anthropic's "before the first time you send it") avoids both cache churn and thinking invalidation. This supports Blackbird deciding elision at insertion time, or at infrequent planned boundaries, rather than continuously.
- **Breakpoint placement:** because of Anthropic's 20-position lookback, a breakpoint must sit within 20 positions after an edited region, or a cache entry behind it, to be found again.

### Gaps
- The Anthropic prompt-caching page is 160k characters and only the first 100k were read. Any explicit context-editing-and-caching section later on the page was not seen.
- Paper Instruments and MiMo publish no TTL, minimum prefix or breakpoint contract.
