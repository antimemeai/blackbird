# Shape outputs once, archive whole rounds rarely

Every harness and provider examined here reaches the same answer to the "every function_call_output stays in context" problem. The full output lives outside the context. What enters the context is bounded when it is created. Later reduction either swaps content in place or removes material in one batch.

On Anthropic's newest models, the in-place swap is now a protocol hazard. Anthropic says shortening an earlier `tool_result` is "Invalid for every later thinking block" and tells clients to "Shorten a tool result … before the first time you send it, not after" ([Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)). Every provider's prefix cache also punishes per-turn rewrites. The literature's strongest result shows that masking old observations matches LLM summarization at roughly half the cost ([JetBrains, arXiv 2508.21433](https://arxiv.org/html/2508.21433v3)). Together these support the shape the operator has chosen for Blackbird:
- outputs are shaped at creation;
- originals stay in the audit log behind locators;
- the nominal context mechanism is **selective archival**: the model picks whole response rounds, typically 5–30% of hot context, to remove from the live context, with no prose summary in their place;
- **compaction** in the traditional sense, history replaced by a summary, is a failure mode. When it fires, as in the C4 overflow backstop, it is audited as a critical error that requires investigation, and Blackbird refuses to delegate it to providers;
- each archival is published as a new **context generation**, a fresh provider-side conversation that carries the retained rounds verbatim plus a compact archive index of what was removed.

What happens to the retained rounds' hidden reasoning at a cut differs by provider:
- **Anthropic 5.x** must drop it after the first archived position.
- **Gemini** validates only the current turn's signatures.
- **DeepSeek** returns a 400 if it is missing whenever tools are present.
- **Meta** allows a reasoning item to be kept whole or dropped, never rewritten.
- **Mistral** and the open-weight hosts replay unsigned text through per-host knobs.
- **OpenAI** is unverified.

That variance belongs in shipped provider-profile data, not in C++. Server compaction from OpenAI, xAI, Anthropic and Azure is the failure mode Blackbird refuses to delegate. At most, Anthropic's `clear_tool_uses` stays available as an optional assist.

The cache arithmetic is unkind to small cuts. A 5% archival near the start of the context rarely repays its cache rewrite, so cuts should be larger, later-positioned, or timed for a cold cache.

Reading this branch's source (`f7a79b1`) turns up defaults that matter more than any new mechanism:
- `context_view` returns full bodies (646 KB of C5's final 787 KB);
- `exec` shows up to 16 MiB to the model by default;
- `read_process_output` with no end reads to EOF;
- `context_inspect` returns hex.

## Eleven harnesses cap output at creation, and none ages it well by default

Every harness studied bounds a tool output when it is created. Nearly all of them keep the overflow somewhere the model can reach.

The reconstruction of Claude Code (a third-party "leaked source" tree, `codeaashu/claude-code`, provenance unverified) swaps any result over **50,000 characters** for a `<persisted-output>` block. The block holds a 2 KB head preview and the path of a session file. Its replacement decisions are **frozen**: a result left unreplaced the first time is "never replaced later (would break prompt cache)", and each decision is recorded so that a resume reproduces byte-identical previews ([toolResultStorage.ts](file:///Users/patrickbeam/projects/blackbird/quarantine/claude-code-unofficial/src/utils/toolResultStorage.ts)). That is operator decision 1, implemented for cache reasons alone.

The other harnesses:
- **opencode** keeps at most 2000 lines or 50 KB. It spills the rest to a directory kept for 7 days, and tells the model to Grep/Read the spill or delegate ([truncate.ts](file:///Users/patrickbeam/projects/blackbird/quarantine/opencode/packages/opencode/src/tool/truncate.ts)).
- **codex** keeps a head and tail within 10,000 tokens. It gives the model **no path** to the dropped middle ([truncate.rs](file:///Users/patrickbeam/projects/blackbird/quarantine/codex/codex-rs/utils/string/src/truncate.rs)).
- **pi, oh-my-pi, gemini-cli and qwen-code** spill to files and name the path. qwen-code adds a SHA-256 and a warning that the spill may be cleaned up after 24 hours ([qwen truncation.ts](file:///Users/patrickbeam/projects/blackbird/quarantine/qwen-code/packages/core/src/tools/truncation.ts)).
- **OpenHands V1** middle-truncates terminal output above 30,000 characters and names a hash-deduplicated full-output file ([truncate.py](https://github.com/OpenHands/software-agent-sdk/blob/33d8aa29c3078d074799e2fbf5e847aeb2d71360/openhands-sdk/openhands/sdk/utils/truncate.py#L11-L117)).

The shape matters because logs put errors at the end. pi's bash, qwen-code's split and gemini-cli's split all weight the tail. kimi-cli keeps only the head and silently drops later writes ([kimi utils.py](file:///Users/patrickbeam/projects/blackbird/quarantine/kimi-cli/src/kimi_cli/tools/utils.py)).

| Harness | Creation cap and shape | Full output | Locator in model text |
|---|---|---|---|
| Claude Code (reconstruction) | 50k chars → 2 KB head preview; Bash 30k | session `tool-results/<id>` | yes, path |
| opencode | 2000 lines / 50 KB, head (tail for shell) | 7-day directory | yes, path plus Grep/Read hint |
| codex | 10k tokens, head+tail | rollout keeps the formatted item only | **no** |
| pi | 2000 lines / 50 KB, tail for bash | temp log | yes, path |
| oh-my-pi | 50 KB spill, 20 KB head + 20 KB tail | session artifacts | yes, `artifact://id` |
| gemini-cli | 40k chars, 20% head / 80% tail | project temp | yes, path |
| qwen-code | 28k gate, 2000-char preview | `tool-results/<callId>`, 24 h | yes, path and SHA-256 |
| kimi-cli | 50k chars, head | none (background tasks only) | no |
| crush | bash 30k, head+tail | 7 days, 256 MB cap | bash only |
| SWE-agent | 100k chars, head | trajectory | no ("use head/tail/grep") |
| OpenHands V1 | terminal 30k, middle cut | conversation `observations/` | yes, path |

Later in a session, the defaults reveal what the projects trust:
- **opencode's `prune`** ships **off** ([config.mdx](file:///Users/patrickbeam/projects/blackbird/quarantine/opencode/packages/web/src/content/docs/config.mdx)).
- **Claude Code's time-based microcompact** (in the reconstruction) clears all but the last 5 compactable results after a 60-minute idle gap, because "the server's 1h cache TTL is guaranteed expired". It is flag-gated off ([timeBasedMCConfig.ts](file:///Users/patrickbeam/projects/blackbird/quarantine/claude-code-unofficial/src/services/compact/timeBasedMCConfig.ts)).
- **SWE-agent's** `LastNObservations` left the default config. Its docstring says it "will break prompt caching" and "is not always needed anymore" ([history_processors.py](https://github.com/SWE-agent/SWE-agent/blob/3ea751c087f32b16e039a2233dd6eefecef325d5/sweagent/agent/history_processors.py#L106-L111)).
- **OpenHands V1** keeps one LLM-summarizing condenser. That condenser cuts only at "manipulation indices" that never split a call from its result, a parallel batch, or a thinking tool loop ([tool_loop_atomicity.py](https://github.com/OpenHands/software-agent-sdk/blob/33d8aa29c3078d074799e2fbf5e847aeb2d71360/openhands-sdk/openhands/sdk/context/view/properties/tool_loop_atomicity.py#L14-L126)). That is the round atomicity Blackbird already enforces.

Three harnesses do rewrite old results by default:
- **gemini-cli** masks every turn behind a 50k-token protected window, with a placeholder that carries the path ([toolOutputMaskingService.ts](file:///Users/patrickbeam/projects/blackbird/quarantine/gemini-cli/packages/core/src/context/toolOutputMaskingService.ts)).
- **qwen-code** microcompacts to `[Old tool result content cleared]`. It overwrites the whole response, **destroying any spill path inside it**, but never clears errors ([microcompact.ts](file:///Users/patrickbeam/projects/blackbird/quarantine/qwen-code/packages/core/src/services/microcompaction/microcompact.ts)).
- **oh-my-pi** blanks superseded reads and results its tools flag as uneventful. Its "shake" placeholders carry `recover: artifact://<id>` ([pruning.ts](file:///Users/patrickbeam/projects/blackbird/quarantine/oh-my-pi/packages/agent/src/compaction/pruning.ts)).

The common gaps are more telling than the features.
- **No lifecycles by kind.** No harness declares lifecycles by output kind; exemptions are hand-kept name lists.
- **No working pin.** No harness lets the model pin a result. SWE-agent's `keep_output` tags land on the action entry while the masker reads only observations, so as written they cannot protect anything. That is inferred from the code, not run ([history_processors.py](https://github.com/SWE-agent/SWE-agent/blob/3ea751c087f32b16e039a2233dd6eefecef325d5/sweagent/agent/history_processors.py#L179-L212)).
- **No way back.** No harness offers the model a route back to a result cleared later, other than re-running the tool or reading a spill path the placeholder kept.
- **Silent divergence.** Every harness renders the user's transcript from native results while the model sees placeholders, and does not tell the user.
- **Repair at send time.** Every harness repairs call/result pairing when it builds the request, keeping `tool_use_id`/`call_id` stable.

Blackbird already has stronger pieces than most of these harnesses, but its defaults undercut them. `exec` returns an `output_ref`, and `read_process_output` fetches byte slices "without rerunning effects". `read_file` returns a `snapshot` ID for later range fetches. Yet in this branch:
- `exec`'s visible budget defaults to **16 MiB** ([command_jobs.hpp:16](file:///Users/patrickbeam/projects/blackbird/context/clm-a1v/include/blackbird/command_jobs.hpp)).
- `read_process_output` treats a missing end as EOF ([tools.cpp](file:///Users/patrickbeam/projects/blackbird/context/clm-a1v/src/tools.cpp)).
- `context_view` and `context_originals` take no parameters and return bodies.
- `context_inspect` is bounded (limit 1..65,536 bytes, default 4,096) but returns `hex-bbm2-stream` or `hex-json-utf8`, doubling every byte the model reads.

The coding workflow's 16,384-byte default for `read_file` is the only creation cap of the kind every surveyed harness applies everywhere.

## Providers punish late rewrites twice: once in reasoning validity, once in the cache

Anthropic's preserved-thinking check settles decision 1 for Anthropic. On Fable 5.1, Opus 5.5, Sonnet 5.5 and Haiku 5.5:
- **Prefix binding.** Each thinking block is bound to `system`, `tools` and every earlier message. "Clearing or shortening an earlier `tool_result`" invalidates every later thinking block. Accounts created on or after 2026-08-31 get the rejection by default ([Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)).
- **What stays valid.** Removing thinking blocks "from the start (oldest first), the end, or all of them" stays valid, and so do blocks before the first change.
- **The stopgap.** `drop_block` drops each failing block and all later ones. In Anthropic's testing it raised output tokens **67.1%** when used every turn.
- **Kept tails fail.** Keeping a tail of turns after a client rewrite fails, because "the kept turns' thinking blocks were produced while the removed history was still in place."
- **The server assist.** Server-side `clear_tool_uses_20250919` never invalidates thinking, because the check "compares what you sent, not the server's edited copy". Its placeholder text is unpublished ([Context editing](https://platform.claude.com/docs/en/build-with-claude/context-editing)).

OpenAI documents a weaker constraint.
- **What must stay untouched.** Only the items "between the last user message and your function call output" must be "passed into the next response untouched" ([Reasoning guide](https://developers.openai.com/api/docs/guides/reasoning)).
- **No documented binding.** OpenAI publishes no prefix binding for encrypted reasoning, so whether later reasoning depends on exact earlier content is unknown. Decision 6's live probe is the right response.
- **Opaque server compaction.** The compaction item is "opaque and not intended to be human-interpretable", and `previous_response_id` chaining forbids manual pruning ([Compaction guide](https://developers.openai.com/api/docs/guides/compaction)).
- **Failure modes in both directions.** Community reports show 400s in both directions: a reasoning item replayed without its following item, and a `function_call` "provided without its required 'reasoning' item" ([community 1236046](https://community.openai.com/t/responses-api-invalid-request-error-function-call-was-provided-without-its-required-reasoning-item/1236046)). Neither keeping nor dropping retained reasoning is safe by default until probed.

Google's contract is the most permissive about older turns.
- **Current turn only.** In `generateContent`, Gemini 3 and later enforce a signature only on the first `functionCall` part of each step **in the current turn**: "Only current turn is required; we don't validate on previous turns." The current turn begins at the latest user message with standard content, not at a `functionResponse` ([Thought signatures](https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures)).
- **Part-level rules.** Signatures must stay "inside its original `Part`" and must never be merged.
- **Silent pairing failures.** Mismatched call/response pairs do not raise errors. They "return empty responses with `finish_reason: STOP` in most cases", so Blackbird must validate pairing itself ([What's new in Gemini 3.5 Flash](https://ai.google.dev/gemini-api/docs/generate-content/whats-new-gemini-3.5)).
- **Whole or nothing.** Google's Agent Platform advises "either include full context (thoughts, function calls, responses) or omit it entirely", because "Partial context can degrade model performance" ([Agent Platform 3.5 Flash guide](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/guides/gemini-3-5-flash)). Whole-round archival does exactly the "omit entirely" half of that.
- **Interactions is stricter.** In stateless mode, clients "MUST always resend all thought blocks exactly as they were received". Stored chains cannot be edited, so archival there means either a new interaction or the `generateContent` contract ([Thinking guide](https://ai.google.dev/gemini-api/docs/thinking)).

The Chinese vendors split on reasoning return, and DeepSeek is strict:
- **DeepSeek with tools.** "the reasoning_content must be fully passed back to the API in all subsequent requests", or "the API will return a 400 error". Without tools it is ignored ([DeepSeek Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode)). The R1-era rule was the opposite, strip it or get a 400, so clients written against old docs now fail.
- **Kimi k3 and k2.7-code** want "the complete assistant message … as-is (including `reasoning_content`)" ([Kimi thinking guide](https://platform.kimi.ai/docs/guide/use-kimi-k2-thinking-model)).
- **GLM** (preserved thinking) demands that "All consecutive reasoning_content blocks must exactly match the original sequence", warning that edits degrade performance and cache hits ([Z.ai Thinking Mode](https://docs.z.ai/guides/capabilities/thinking-mode)).
- **MiniMax** says not to remove `<think>` content ([MiniMax M2 guide](https://platform.minimax.io/docs/guides/text-m2-function-call)).
- **Qwen** reads historical reasoning only under opt-in `preserve_thinking` ([Model Studio Deep Thinking](https://help.aliyun.com/zh/model-studio/deep-thinking)).
- **Same model, different host, different contract.** Kimi on Alibaba's Model Studio requires `reasoning_content` in every assistant message, while kimi-k2.6 on Moonshot's own API ignores history by default.
- **Server state and assists.** Only Qwen's Responses API holds server state (`previous_response_id`, valid 7 days). MiniMax's Anthropic endpoint and MiMo's Responses API ignore or reject `context_management`, so server-side clearing simply does not exist there ([MiMo Responses](https://mimo.mi.com/docs/en-US/api/chat/responses)).

Paper Instruments rejects compaction, truncation and encrypted reasoning, and returns `400 invalid_value` for unmatched function results ([Ultramarine Responses](https://docs.paperinstruments.com/docs/ultramarine/responses)). OpenRouter's "context compression" plugin removes middle messages with no pairing guarantee, and it is on by default for endpoints of 8k context or less ([message transforms](https://openrouter.ai/docs/guides/features/message-transforms)). Blackbird should disable it explicitly.

Caching makes rewrite cadence matter everywhere, because every documented cache is exact-prefix:
- **Anthropic** requires "100% identical prompt segments", in the order tools, system, messages. Writes cost 1.25x (5-minute) or 2x (1-hour), and the lookback is 20 positions ([Prompt caching](https://platform.claude.com/docs/en/build-with-claude/prompt-caching)).
- **Gemini** states that "Cached content is a prefix to the prompt". Implicit caching starts at 4,096 tokens on Gemini 3 and 4 ([Context caching](https://ai.google.dev/gemini-api/docs/generate-content/caching)), and the Agent Platform discount is 90%.
- **Kimi k3** charges $3.00 per million for a miss and for a 5-minute write, against $0.30 for a hit ([Kimi caching](https://platform.kimi.ai/docs/guide/use-context-caching-feature-of-kimi-api)).
- **Qwen's explicit cache** costs 125% to create and about 10% on a hit ([Model Studio Context Cache](https://help.aliyun.com/zh/model-studio/context-cache)).
- **DeepSeek** persists cache prefix units at each request's end ([DeepSeek KV cache](https://api-docs.deepseek.com/guides/kv_cache)).
- **Manus** calls KV-cache hit rate "the single most important metric" ([Manus](https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus)).

| Provider | Late edit of an old output | Retained-reasoning consequence | Server assist | Server state |
|---|---|---|---|---|
| Anthropic Fable 5.1, Opus/Sonnet/Haiku 5.5 | valid only if every later thinking block is dropped | rejected by default; `drop_block` stopgap | `clear_tool_uses` (thinking-safe); server compaction | stateless |
| Anthropic pre-5.1, Mythos 5.1 | valid; no prefix check | none documented | same | stateless |
| OpenAI Responses | not forbidden before the last user message | unknown; both directions reported to 400 | encrypted compaction | `previous_response_id` optional |
| Gemini `generateContent` | not validated outside the current turn | current-turn signatures only; partial context degrades | none for plain models | stateless |
| Gemini Interactions | stateless must resend thoughts exactly | outside contract | Managed Agents only (~135k) | stored chains, 55 days paid |
| DeepSeek (tools) | not forbidden | `reasoning_content` required on every assistant message | none | stateless |
| Kimi, GLM, MiniMax, MiMo | not forbidden | return as-is (quality loss, few documented errors) | ignored where offered | stateless |
| Qwen Model Studio | not forbidden | read only with `preserve_thinking` | none | Responses chains, 7 days |
| Paper Instruments | not forbidden; every call needs its result | plaintext replay allowed | none | `store` false by default |
| xAI Responses | caching guide says append only | encrypted reasoning "recommended" back | `/v1/responses/compact`, opaque (refused) | stored 30 days |
| Mistral | not forbidden; tool → user rejected (community) | unsigned text; replay advised | none | Chat stateless; Conversations stateful |
| Bedrock Converse / InvokeModel (Claude) | Anthropic validators apply underneath | Anthropic rules | Anthropic compaction on InvokeModel only | stateless |
| Azure OpenAI / Foundry | OpenAI rules / Anthropic rules | as upstream | OpenAI compaction; Claude context editing | Responses chains, 30 days |
| Meta Model API | **drop whole turns only**; never rewrite | reasoning items may be dropped, not rewritten | none; rejects `truncation:auto` | optional chains |
| Together, Fireworks, Groq, Cerebras | not forbidden | per-host knob over the model vendor's rule | none | stateless (Fireworks Responses optional) |

The remaining platforms add two more provider-only compactions to refuse, one near-ideal validator, and a strong argument for data over code.
- **xAI** points developers to a Responses API (stored 30 days, `previous_response_id`). Its `POST /v1/responses/compact` returns "a **single** compaction item" whose `encrypted_content` is "opaque", to be sent "verbatim — do not modify" at the head of the next request ([xAI Context compaction](https://docs.x.ai/developers/advanced-api-usage/context-compaction)). That is one more compaction that only the provider can author. xAI's caching guide says "never edit, remove, or reorder earlier messages — only append", and calls omitted reasoning "the top cause of cache misses" ([xAI multi-turn caching](https://docs.x.ai/developers/advanced-api-usage/prompt-caching/multi-turn)).
- **Mistral** returns thinking as unsigned plain-text `ThinkChunk`s and says to "always replay the full assistant message (including `ThinkChunk`)", because stripping it "significantly degrades output quality" ([Mistral Reasoning](https://docs.mistral.ai/capabilities/reasoning/)). Community reports show Mistral rejecting "Unexpected role 'user' after role 'tool'" ([Zed commit](https://git.secluded.site/zed/commit/b8c1b54f9eaf78d3bcf073f6495cfaeb8b3bf2b4)). That matters for where Blackbird places archive-index stubs.
- **Amazon Bedrock Converse** documents only that reasoning text and its signature go back "unmodified". Anthropic's validators still run underneath, as community errors such as "`thinking` … blocks in the latest assistant message cannot be modified" show ([Classmethod](https://dev.classmethod.jp/articles/bedrock-converse-extended-thinking-tool-use-validation-exception/)). Bedrock processes cache checkpoints in the order "`tools` → `system` → `messages`… changing content in an earlier section invalidates the cache for later sections". A change to `tools` therefore costs the cached `system` and `messages` too ([Bedrock Prompt caching](https://docs.aws.amazon.com/bedrock/latest/userguide/prompt-caching.html)). Anthropic's server compaction exists only on InvokeModel and is "not supported by the Converse API" ([Bedrock Claude compaction](https://docs.aws.amazon.com/bedrock/latest/userguide/claude-messages-compaction.html)).
- **Azure OpenAI** proxies OpenAI's Responses rules, compaction included. Claude in Foundry lists context editing but not compaction ([Claude in Foundry](https://learn.microsoft.com/en-us/azure/foundry/foundry-models/concepts/claude-models)).
- **Meta's Model API** documents legal client edits more precisely than anyone else. It rejects `truncation: "auto"` ("Model API does not trim for you"). It states that encrypted reasoning items "cannot [be summarized or rewritten], only keep it whole or drop it. Compact at the level of whole turns", that "dropping one is allowed", and that every reasoning item must be followed by an assistant message or a `function_call` before the next user, system or developer message ([Meta Responses API](https://dev.meta.ai/docs/protocols/responses.md)). That is Blackbird's archival rule stated by a vendor, and a near-ideal reference for Blackbird's own validator.
- **The OpenAI-compatible hosts** each expose their own control over re-sending earlier reasoning: Fireworks' `reasoning_history` (`disabled`, `interleaved`, `preserved`), Cerebras' `clear_thinking` on Qwen, Together's `clear_thinking: false`, and Groq's `reasoning_format` with no `raw` under tools ([Fireworks API](https://docs.fireworks.ai/api-reference/post-chatcompletions.md), [Cerebras Reasoning](https://inference-docs.cerebras.ai/capabilities/reasoning.md)). Together defines the cached prefix as "the prompt exactly as the model sees it, after the model's chat template renders your request" ([Together caching](https://docs.together.ai/docs/inference/chat/prompt-caching.md)). So when a host's template strips earlier reasoning from the rendered prompt, the prefix shifts at each new user turn and **cache hits suffer even if the client only appends**. The preserved modes keep the rendered prefix append-stable.
- **Cohere v2** is stateless with unsigned thinking, and it documents no replay rule or caching.

## The literature favours archival over summary, but archival removes more than masking did

The strongest controlled evidence comes from JetBrains' comparison of observation masking and LLM summarization on SWE-bench Verified ([arXiv 2508.21433v3](https://arxiv.org/html/2508.21433v3)):
- Masking observations older than 10 turns cut cost about **50%** against the raw agent. Solve rates were statistically indistinguishable from summarization's, and masking was cheapest in 4 of 5 configurations.
- Summaries lengthened runs about **15%** and "rarely hit the cache".
- With Gemini 2.5 Flash thinking, both strategies lost accuracy: from 40.4% raw to 36.4% with masking and 31.4% with summaries.

Other results point the same way:
- Moonshot's own K2-Thinking evaluations handled overflow by a strategy that "hides all previous tool outputs" rather than by summarizing ([Kimi-K2-Thinking card](https://huggingface.co/moonshotai/Kimi-K2-Thinking)).
- The ACE study shows how prose rewrites fail. One LLM rewrite shrank an 18,282-token context to **122 tokens**, and accuracy fell below the no-context baseline ([ACE](https://arxiv.org/html/2510.04618)).
- In the original SWE-agent ablation, keeping the last 5 observations beat full history, **18.0% vs 15.0%**. A 100-line viewer beat whole files, 18.0% vs **12.7%**, so shaping at the source matters as much as later elision ([SWE-agent](https://arxiv.org/html/2405.15793)).

Taken together, this evidence supports making archival the nominal operation. History replaced by a summary, which is compaction proper, belongs on the failure path.

The same evidence also strains the operator's design in one specific way. JetBrains masking, SWE-agent's processor and Anthropic's server clearing **keep every action and all reasoning**, and blank only observations. A 2026 study that compresses reasoning instead also keeps actions, tool calls and observations ([arXiv 2609.29875](https://arxiv.org/abs/2609.29875)). Whole-round archival removes the action too, so the model forgets that it ran the command, not just what the command printed. No study measures that loss.

The archive index is the place to compensate. Each archived round's entry should carry the tool names and a short argument head, such as a path, a command or a query, beside its sizes and locator. The model then keeps the action trace that the masking evidence assumed it had.

The newer work shows where removal costs hide:
- **Older observations can hold needed facts.** On AppWorld, keep-recent FIFO scored **45.8** against 56.0 uncompressed, because older observations held API documentation and IDs ([ACON](https://arxiv.org/html/2510.00615)). Model choice of what to archive, rather than age alone, is warranted.
- **The cost shows up as re-retrieval, not lower completion.** At 5x compression, completion was unchanged but GPT-5.5's retrieval calls rose from **21.0 to 63.9**. In the same study, random selection performed about as well as a hindsight oracle ([arXiv 2608.16370](https://arxiv.org/abs/2608.16370)).
- **Repeated compaction destabilizes runs,** raising blocked actions and run-to-run inconsistency ([arXiv 2608.06503](https://arxiv.org/abs/2608.06503)).

So archival should be rare and batched, and the fetch path has to be cheap. The literature has one plain gap: **no controlled study compares a bare placeholder with a placeholder plus a re-fetchable locator.** Manus ("compression must be restorable") and Anthropic ("lightweight identifiers") recommend locators without measuring them.

## Context generations make archival Blackbird's own, and valid on every provider

A context generation is how Blackbird publishes an archival. Two kinds of cut start one:
- An **archival cut** happens when the model's archival selection removes whole rounds. Blackbird starts a fresh provider-side conversation from the remaining context.
- A **rebind cut** archives nothing. It carries the visible context unchanged and drops only hidden reasoning, as at a model switch or a provider failover.

Either way, the new generation ID sits under the same Blackbird session, linked to the context revision and originals it came from.

For stateless endpoints a generation is simply a new message array plus a lineage record. For stateful ones it must also be a new chain:
- OpenAI `previous_response_id`;
- Gemini Interactions `previous_interaction_id`;
- Qwen Responses.

An archival cut's generation carries three things. **The retained rounds verbatim**, typically 70–95% of the hot context, still in their creation shape. **A compact archive index** of what was removed: round IDs, kinds, tool names with argument heads, byte sizes and locators. And **no synthetic summary**, since history replaced by a summary would be the compaction failure path.

Round atomicity, meaning reasoning plus calls plus outputs removed together, is already enforced. That keeps every provider's pairing rule intact, including Gemini's silent-failure "exactly one per call" rule and Paper's 400 on unmatched results.

The one per-provider question is what happens to the retained rounds' reasoning. It should be shipped profile data keyed by endpoint and model, because the same model changes contract across hosts:

| Profile | Retained rounds' reasoning at a cut | Open-round rule | Basis |
|---|---|---|---|
| Anthropic Fable 5.1, Opus/Sonnet/Haiku 5.5 | keep before the first archived position; **drop** in every round after it | cut at a user-turn boundary; mid-turn only after a probe, or with `drop_block` | prefix check ([Preserved thinking](https://platform.claude.com/docs/en/build-with-claude/preserved-thinking)) |
| Anthropic pre-5.1, Mythos 5.1 | keep | keep | no prefix check |
| Gemini `generateContent` 3+ | keep signatures in their original Parts | keep; never merge signed Parts | current turn validated only |
| Gemini Interactions | new interaction per generation; stateless edits are outside contract | n/a | "MUST resend … exactly" |
| OpenAI Responses | **probe pending**; until probed, keep, since the docs say irrelevant reasoning is ignored, and record outcomes | pass the reasoning returned with the last call | unverified; both directions reported |
| DeepSeek with tools | **must keep** `reasoning_content` on every retained assistant message | keep | documented 400 |
| Kimi k3/k2.7-code, GLM preserved, MiniMax, MiMo | keep as-is | keep | quality and cache loss when edited |
| Qwen Model Studio | keep; read only with `preserve_thinking` | keep | opt-in |
| Bedrock (Claude via Converse or InvokeModel), Foundry Claude | as the matching Anthropic row | as Anthropic | validators run underneath |
| Azure OpenAI | as the OpenAI row | as OpenAI | proxy of OpenAI rules |
| Meta Model API (Responses) | keep each retained item whole with its `encrypted_content`, or drop it; never rewrite | each reasoning item followed by an assistant message or call | documented ([Meta Responses API](https://dev.meta.ai/docs/protocols/responses.md)) |
| xAI Responses | keep encrypted reasoning verbatim | keep | caching guide |
| Mistral | keep `ThinkChunk`s as-is | the index must not follow a `tool` message as `user` | replay advised; tool → user rejected |
| Fireworks, Together, Cerebras, Groq | set the host's knob to its preserved mode where the model supports it (`reasoning_history: preserved`, `clear_thinking: false`); Groq `parsed` with tools | keep | template stripping shifts the rendered prefix |
| Unprobed host | conservative: cut at user-turn boundaries only, keep reasoning, probe once | user boundary only | C6's "probes, not assumptions" |

Where the index sits matters on four providers:
- **DeepSeek with tools:** an assistant-role message without `reasoning_content` likely triggers the 400.
- **Gemini:** a user text message resets where the "current turn" begins.
- **Mistral:** a user message directly after a `tool` message is reportedly rejected.
- **Meta:** a reasoning item must be followed by an assistant message or call before any user, system or developer message.

Whole-round archival keeps the Meta rule automatically. The index's role and position still belong in the profile: a developer/system item where the protocol has one, otherwise a clearly marked harness note in a user message at a position the profile allows. It is never an assistant message.

The index can take two forms:
- **One cumulative index item** after the pinned instructions. It is simple, but every later cut rewrites it near the start of the prefix.
- **In-place stubs**, one compact entry where each archived run used to be, like OpenHands' condensation "tombstones" ([condenser README](https://github.com/OpenHands/software-agent-sdk/blob/33d8aa29c3078d074799e2fbf5e847aeb2d71360/openhands-sdk/openhands/sdk/context/condenser/README.md)). These keep every byte before the earliest archived run unchanged. They cost more items, and each stub must be unambiguous enough that the model cannot mistake it for user input.

The cache arithmetic shows why position and batch size dominate. An archival cut is the opposite of compaction in cache terms. Compaction replaces history with a short summary, so it re-writes only a few KiB and pays back on cache almost at once. But it discards the context itself. An archival cut carries 70–95% of the hot context verbatim, and every retained byte after the first archived position has to be re-written once.

Write H for the hot context, p for the bytes before the earliest archived round, and a for the archived bytes. After a cut, the H − p − a retained bytes after p must be re-written instead of read from cache. On Anthropic's 5-minute tier (write 1.25x, cached read near 0.1x) that extra cost is about 1.15 × (H − p − a). Each later request saves about 0.1 × a. Break-even is therefore about 11.5 × (H − p − a) / a requests. Take H = 384 KiB (C1's trigger) and p ≈ 30 KiB of pinned material:

| Archived share | Position | Break-even requests (Anthropic) |
|---|---|---|
| 5% (19 KiB) | oldest rounds | about 200 |
| 20% (77 KiB) | oldest rounds | about 41 |
| 30% (115 KiB) | oldest rounds | about 24 |
| 20% (77 KiB) | late rounds, p ≈ 250 KiB | about 9 |
| any | cache already cold (idle past TTL) | about 0 |

Kimi k3 (write at miss price, hit at 0.1x) gives about a fifth less. The longest 2026-10-17 run made 52 requests, and C5's continuation made 7. **On cache price alone, a small early cut rarely pays back within a session.** Its justification has to be window headroom and attention, which context-rot evidence supports ([Chroma](https://www.trychroma.com/research/context-rot)).

Several consequences follow:
- Batch archival toward the 30% end of the operator's range. C1's halving target goes beyond it.
- Cut opportunistically when the cache is already cold. Anthropic's TTL is 5 minutes by default, and the Claude Code reconstruction and oh-my-pi both time clearing to idle gaps for exactly this reason.
- When candidates are equally stale, prefer later positions.
- Keep tools and system instructions byte-identical across generations, since about 27 KiB per request is fixed overhead that stays cached through a cut. On Bedrock, a change to `tools` also invalidates the cached `system` and `messages`. This is one more reason a restricted palette, as in C6's context-only fallback, has a cache price.
- Set each host's reasoning-history knob to its preserved mode where the model supports it. A template that strips earlier reasoning shifts the rendered prefix at every new user turn, and costs cache hits between cuts that no client discipline can recover.
- Keep `session_id`, `prompt_cache_key` and `x-session-id` stable per session. OpenRouter's sticky routing otherwise hashes the first non-system message, which a cut can change ([OpenRouter prompt caching](https://openrouter.ai/docs/guides/best-practices/prompt-caching)).

Hidden reasoning has its own cost. On Anthropic 5.x, every retained round after p loses its thinking at each cut. That is the per-turn `drop_block` cost paid once per cut, but it is one more reason to prefer late positions and user-turn boundaries.

Generations compose with the planned units:
- **Shaping at creation.** Shape-at-creation stays the only content rewrite. A cut removes whole rounds; it never re-shapes the rounds it keeps. That keeps decision 1's rule literal, apart from the profile-driven reasoning drop.
- **C6 becomes the archival step.** The step's request is "the main request's exact prefix plus one final developer brief", a pure append. That is valid and cache-friendly on every provider. Its output is an **archival selection plus the archive index** for the next generation, not an authored reduced context. The brief asks which whole rounds to archive, using the index columns below. Summarization drops out of the nominal path. A model-written note on an archived round could at most be an optional secondary field in its index entry, never a replacement for history, and under decision 4 it may be offered only if it will be honored.
- **C4 is the compaction failure path.** Overflow means the nominal path failed. When C4 fires, it is audited as a critical error through the rageshake mechanism, as already decided. Whatever reduced context it produces is published as a generation like any other cut. Archival makes the overflow study's index-only selection (option A) available to it, because the model can choose rounds from an index without reading bodies, so the request fits by construction. Whether C4 keeps the operator's windowed option B or moves to index-only archival before falling back to compaction proper is a decision for the operator.
- **Model switching is a rebind cut.** It archives nothing and applies the new profile's reasoning rule. Gemini Interactions says the backend "manages compatibility", Flash-Lite drops foreign signatures, and OpenAI omits incompatible reasoning.

Server assists stay optional and never hold authority. Anthropic's `clear_tool_uses` is thinking-safe, but its placeholder is unpublished, it excludes by tool name only, and it removes the locator. Server compaction from OpenAI, xAI and Anthropic is exactly the failure mode Blackbird refuses to delegate: history replaced by an opaque or provider-authored summary. MiniMax and MiMo ignore the context-management fields entirely, and Converse lacks Anthropic's compaction.

## Design options for Blackbird, and what each costs

**Typed output lifecycles per tool kind.** Each tool declares a creation shape, a locator, and an archival class that tells the model and the index how cheaply the round can go. The shape is applied once, before first send; the class is used only when choosing rounds at a cut. That replaces the name lists every surveyed harness keeps by hand. The numbers below are starting points to measure, not settled values.

| Kind | Shape at creation | Locator | Archival class |
|---|---|---|---|
| `read_file` | requested range or 16 KiB default excerpt, with file/selected/omitted bytes | `snapshot` ID plus range | supersedable: a later read or edit of the same path makes the round a prime candidate (oh-my-pi) |
| `exec` / `process` | exit code, wall time, bytes, tail-weighted head+tail; default lowered from 16 MiB to tens of KiB | `output_ref` plus byte range | archivable; **failed runs flagged durable**, since keeping errors is the Manus lesson and qwen-code never clears them |
| search and listing (`git_observe`, grep-like Lua tools) | counts plus the first N matches | rerunnable query plus retained original | archivable; zero-hit rounds "uneventful" |
| `edit_file` / `write_file` | confirmation and diff stats | before/after snapshots | durable but tiny |
| `context_view`, `context_stats`, `audit_inspect`, `trajectory_read` | **index only** | revision ID | **ephemeral**: always archived at the next cut |
| participants, colleague, decision models | the returned distillate | transcript reference | durable distillate |
| MCP / web / custom Lua tools | generic cap with spill unless the tool declares a shape | spill reference | archivable |

A per-round aggregate budget is also worth adopting. Claude Code's reconstruction and qwen-code both cap parallel results at 200k characters per batch. The benefits are determinism, cache stability, reproducible resumes and no model call. The costs are the heuristic relevance guess at creation, which too-tight shapes turn into re-fetches, and a declared shape for each new kind.

**Ephemeral or compact introspection: `context_view` as an index.** This is the most urgent change. C5 shows guidance already moves the model to inspect, and the two `context_view` dumps were **646 KB of 787 KB**. The Jev pilot also showed that a cheap judge does not recognize a context dump as redundant, so structural redundancy belongs in code. The change:
- `context_view` returns the same columns as the archive index: round ID, kinds, tool names and argument heads, bytes, locator, archival class, and Jev score if used. For C5's 57 items that is on the order of 6–8 KB, against 441 KB.
- The index is paged above a bound, and the call's own round is ephemeral.
- `context_originals` gets the same treatment, and `context_inspect` returns UTF-8 text for text entries.

The tradeoff is real but smaller under archival than under summarization. The model no longer needs bodies to choose rounds, only to check a doubtful one through a bounded page.

**Decay.** Under decision 1, decay is not a gradual rewrite. It is the archival class plus position, applied only at a cut:
- ephemeral rounds always go;
- superseded reads are prime candidates;
- ordinary archivable rounds go oldest-first unless the model prefers otherwise;
- durable rounds go only by explicit choice.

A "keep the last K rounds" floor belongs in the brief. The literature's windows do not transfer between scaffolds (10 in SWE-agent, 58 in OpenHands), so K should be measured, not borrowed. The benefit is masking's substance at the cadence the reasoning and cache rules demand, plus staleness handling through supersession, a failure JetBrains names in its own method. The cost is that the context grows unreduced between cuts, so C1's trigger has to stay tuned from measured sizes.

**Model-declared retention.** No surveyed harness offers this. SWE-agent's attempt appears not to work, and codex lets the model only lower its exec budget. Decision 4 permits two honest forms:
- an at-call `retain` hint (`brief` or `full`, bounded by the kind's ceiling) that changes the creation shape;
- `pin` and `unpin` modes in `context_manage` that mark rounds durable.

Both are honorable because Blackbird owns creation and cuts. Two strains follow. First, Anthropic's `clear_tool_uses` cannot honor per-call pins, since it excludes by tool name, so it must stay off or exclude every pinnable kind. Second, models never called context tools unprompted in the 2026-10-17 campaign, and over-pinning would block archival. Pins need a byte ceiling and should appear in the archival-step index for review.

**Recoverable locators with bounded fetch.** Every shaped output, and every archive-index entry, needs a locator that resolves from the audit store after a cut, a process restart or a session reopen. `process` says "custody is process-lifetime only" for job records while `output_ref` "stays readable". That needs a direct check across a restart. Every fetch path also needs a default bound and a `next` cursor:
- `read_process_output` should default to `read_file`'s 16 KiB instead of EOF;
- `context_inspect` should return text;
- an archived round should be fetchable whole or by entry.

Fetched slices should be shaped like any other output, so a fetch cannot recreate the C5 failure. The benefit: aggressive shaping and archival become safe, and Blackbird already owns the originals (decisions 2 and 3). The cost: re-retrieval appends bytes and turns, which roughly tripled under 5x compression in 2608.16370. Fetch counts per archived round should be a first-class metric in the next live check.

**Jev-scored relevance.** The pilot measured **7,796 input tokens (~$0.0003)** per triage, 0.35–0.44 s, and stability within 0.08 across repeats. It kept every cited source read and rated the 646 KB of dumps at "summary suffices" ([JEV_PILOT.md](file:///Users/patrickbeam/projects/blackbird/context/clm-a1v/papers/clm-a1v-2026-10-19/JEV_PILOT.md)). That is one task at one decision point, not a calibration. The best fit is a score column in the archival-step index that pre-sorts candidates for the model, never a decider. Decision 5 keeps authorship with the model, and the near-oracle performance of random selection in 2608.16370 suggests recoverability matters more than ranking. Running Jev at creation is the wrong fit: it adds latency to every call and nondeterminism to bytes that must stay cache-stable. Trained, task-conditioned verbatim extractors such as Squeez and SWE-Pruner are the creation-time technique the literature supports, and they would be a separately qualified later unit ([Squeez](https://arxiv.org/abs/2604.04979)).

**Provider profiles as shipped data.** Hosts disagree on the same model. Each host exposes its own reasoning-history knob. Meta, DeepSeek and Mistral each impose a different structural rule. So all of this should live in profile data keyed by host, endpoint and model, extending the unmerged `5st` settings table:

| Profile field | Example values | Grounding |
|---|---|---|
| `retained_reasoning_at_cut` | `drop_after_first_archived` (Anthropic 5.x), `keep` (Gemini, DeepSeek, Kimi, GLM, MiniMax, Mistral, xAI), `keep_whole_or_drop` (Meta), `probe_pending` (OpenAI) | provider docs above |
| `open_round_cut` | `user_boundary_only`, `allowed`, `drop_block` | Anthropic and OpenAI tool-turn rules |
| `index_placement` | `developer`, `system`, `user_note_not_after_tool` | DeepSeek, Gemini, Mistral, Meta ordering |
| `server_state` | `stateless`, `response_chain`, `interaction_chain` | OpenAI, xAI, Azure, Bedrock Responses, Gemini, Qwen |
| `host_reasoning_knob` | `reasoning_history=preserved`, `clear_thinking=false`, `reasoning_format=parsed`, `preserve_thinking=true` | Fireworks, Together/Cerebras, Groq, Qwen |
| `cache_session_keys` | `session_id`, `prompt_cache_key`, `x-grok-conv-id`, `x-session-affinity` | OpenRouter, OpenAI, xAI, Fireworks |
| `tool_restriction` | `allowed_tools`, `named_function`, `none` | C6 probes |
| `disable_provider_transforms` | OpenRouter `context-compression: off`; never `truncation:auto` | OpenRouter, Meta, Paper |

Every value is either cited or probed and records which. Unprobed combinations get the conservative row. The OpenAI probe fills in its row, and each newly used host gets one probe. The benefit is that host variance, such as Kimi on Model Studio or Fireworks' GLM template default, and new models become data changes rather than code changes. The cost is a probe per combination and a profile that goes stale as vendors change contracts, as DeepSeek's R1 rule did.

**Archival, the compaction failure path, caching and provider rules together.**

| Option | Validity | Cache cost | Authority | Fit with decisions |
|---|---|---|---|---|
| Archival cut published as a generation (nominal) | valid everywhere through profile rules | rewrite of the retained 70–95% after the first archived position; tools+system stay cached | Blackbird, model-chosen | supports 1–5; decision 6 becomes one profile row |
| Rebind cut (model switch or failover) | valid everywhere | rewrite from the first changed reasoning onward | Blackbird | no selection needed |
| C4 overflow backstop | must still produce a valid generation | side requests miss the cache | Blackbird | compaction failure path; critical rageshake every time; index-only archival before compaction proper is an operator decision |
| Optional model note in an archive-index entry | valid where not an assistant message | a few bytes per round | model, secondary | never replaces history; ACE collapse and run elongation argue against more |
| Anthropic `clear_tool_uses` inside a generation (assist) | thinking-safe | one invalidation per clear | provider; unpublished placeholder | strains decision 4 and exact audit |
| Server compaction (OpenAI, xAI, Anthropic, Azure) | valid | small rewrite; history gone | provider; opaque blobs | the failure mode Blackbird refuses to delegate |

## Conclusion

The question has moved from "should old tool outputs leave the context?" to "what is the one boundary where the context may change, and what does each provider let survive it?" For Blackbird the answer is two points: creation, where an output is shaped once, and a generation cut, where whole rounds are archived (or, at a rebind cut, only hidden reasoning changes) and the profile decides what hidden reasoning survives. Meta's documented rule, "keep it whole or drop it", shows that a vendor has independently reached the same validator.

Treating compaction as a failure rather than a mechanism keeps the riskiest operation the literature documents, the free-form prose rewrite, off the nominal path, and makes every occurrence an investigated event. It does go past that literature's evidence by dropping actions as well as observations, and the archive index has to make up for that. The cache arithmetic adds an awkward constraint: small early cuts do not pay for themselves on price. Cuts should be fewer, larger, later-positioned or cold-cache-timed, and justified by window headroom rather than by billing.

The highest-value work is still correcting the defaults that let tool outputs fill the context:
- an index-only, ephemeral `context_view`;
- a real creation cap on `exec`;
- default bounds on every fetch;
- text instead of hex.

Then come the per-kind table, archival as generations with profile-driven reasoning rules, retention modes Blackbird can honor, and Jev as a sorting column. The next live check should report fetches per archived round, cuts per session, cache-read against cache-write bytes, and run-to-run variance alongside completion, because the 2026 literature shows those are where removal costs hide.
