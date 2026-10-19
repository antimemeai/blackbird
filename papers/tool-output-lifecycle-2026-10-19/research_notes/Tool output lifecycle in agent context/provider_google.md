# Google Gemini: tool-call outputs, reasoning continuity and client-edited history

All pages were read on 2026-10-19. The "last updated" date shown on each page is given with it. No API was called. Method caveat: pages were read through a fetch tool that returns a model-written extraction (quotes plus paraphrase), not the raw HTML. Quoted strings are as the extractor returned them. Short phrases are reliable; exact punctuation in longer passages should be rechecked before anyone quotes them as normative text.

Context for the report writer, as of October 2026:
- On the Gemini Developer API (ai.google.dev), the **Interactions API** is now the primary surface. `generateContent` is labelled **"Gemini Generate Content API (Legacy)"** and its docs have moved under `/gemini-api/docs/generate-content/...`.
- On Google Cloud, the Vertex AI generative AI docs now live under **"Gemini Enterprise Agent Platform"** (docs.cloud.google.com/gemini-enterprise-agent-platform/...). The old cloud.google.com/vertex-ai URLs redirect there (301). Those pages describe `generateContent`-style `Part`/`thought_signature` semantics. None of the Agent Platform pages I read mentions the Interactions API.
- The current pages name these models: Gemini 4 Argon, 3.8 Flash, 3.6 Flash, 3.1 Pro Preview, 2.5. Gemini 3.5 Flash is deprecated and requests to it are routed to 3.6 Flash.

## Thought signatures: what they are, which models and APIs require them, how they bind to parts

### Takeaway
A thought signature is an encrypted, opaque piece of the model's reasoning state. Where it lives depends on the API:
- In `generateContent` (Gemini API legacy and Agent Platform/Vertex), it sits on individual `Part`s. The API enforces it only on the **first `functionCall` part of each step within the current turn**, for Gemini 3 and later.
- In the Interactions API, signatures sit only on `thought` steps and built-in-tool steps, never on standard function calls. Stateless clients "MUST" resend every thought block exactly as received.

### Cited Findings
- **Definition.** Gemini API (Interactions): "Thought signatures are encrypted representations of the model's internal reasoning. They are required to maintain reasoning continuity across multi-turn interactions." Each `thought` step's `signature` field is always present, even after minimal reasoning. — [Thinking guide, Gemini API, updated 2026-10-14](https://ai.google.dev/gemini-api/docs/thinking)
- **Where signatures appear (Interactions vs generateContent).**
  - In Interactions, signatures are limited to `thought` steps and built-in tool steps such as `google_search_call` and `google_search_result`. They "never appear on user inputs, model outputs, or standard function calls."
  - The page contrasts this with `generateContent`, where signatures can sit on any part, `functionCall` parts included.
  - The old `/thought-signatures` URL now redirects to this section of the Thinking guide (that redirect page is dated 2026-08-18).
  - — [Thinking guide](https://ai.google.dev/gemini-api/docs/thinking); [old thought-signatures URL](https://ai.google.dev/gemini-api/docs/thought-signatures)
- **Interactions, stateful mode** (`store: true` plus `previous_interaction_id`): the server manages thought blocks and signatures. "In this mode, you do not need to do anything regarding signatures." — [Thinking guide](https://ai.google.dev/gemini-api/docs/thinking)
- **Interactions, stateless mode.**
  - The client "MUST always resend all thought blocks exactly as they were received from the model" and must not remove or modify them.
  - When switching models mid-session, the client still resends the previous model's thought blocks, and "the backend manages compatibility."
  - Signatures on built-in tool calls and results must also be resent.
  - — [Thinking guide](https://ai.google.dev/gemini-api/docs/thinking)
- **generateContent (Gemini API legacy): models.**
  - Thinking models in the Gemini 4, 3 and 2.5 series can return signatures.
  - "Gemini 4 and 3 always return a signature on the first function call part."
  - In function calling the client must pass signatures back, or the request fails.
  - — [Thought signatures, Generate Content API (Legacy), updated 2026-10-13](https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures)
- **generateContent (Gemini API legacy): validation scope.**
  - "Only current turn is required; we don't validate on previous turns."
  - The current turn starts at the most recent user message with standard content (such as text), not at a `functionResponse`.
  - Omitting the signature on the first `functionCall` part of any step in the current turn returns 400.
  - — [Thought signatures (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures)
- **generateContent (Gemini API legacy): error and non-function-call parts.**
  - The error form is: "Function call `<Function Call>` in the `<index of contents array>` content block is missing a `thought_signature`."
  - The final part of a text response may also carry a signature. Returning it is recommended, but "The API does not strictly enforce validation."
  - — [Thought signatures (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures)
- **Agent Platform (Vertex) differences.**
  - Gemini 3 models enforce stricter validation. Clients must return signatures "even when using `MINIMAL` thinking levels."
  - Gemini 3 Pro Image does not enforce them and returns no 400 when they are missing, though returning them is still recommended.
  - Flash-Lite models drop signatures that came from non-Flash-Lite models rather than failing.
  - The current turn is defined the same way: "A turn begins with the most recent user message that is not a `functionResponse`."
  - The page states the 400 status but quotes no message text, and does not say outright that older turns go unvalidated.
  - — [Thought signatures, Gemini Enterprise Agent Platform, updated 2026-10-15](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/thinking/thought-signatures)
- **Binding to parts (Agent Platform and Gemini API function-calling pages).**
  - Return the signature "inside its original `Part`."
  - "Don't merge a `Part` containing a signature with one that does not."
  - "Don't combine two `Part`s that both contain signatures, because the signature strings cannot be merged."
  - — [Agent Platform thought signatures](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/thinking/thought-signatures); [Function calling (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/function-calling)
- **SDK handling.** The Google Gen AI SDKs (Python, Node.js, Go, Java) handle signatures automatically when you use chat history or append the full model response to history. Manual handling is needed for REST, manual part extraction, "or when trimming history." — [Agent Platform thought signatures](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/thinking/thought-signatures); [Thought signatures (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures)
- **Streaming.**
  - generateContent: for responses without function calls, the signature may arrive in a part with empty text. Clients should parse all parts until `finish_reason`.
  - Interactions: a `thought_signature` delta arrives just before `step.stop`.
  - — [Agent Platform thought signatures](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/thinking/thought-signatures); [Thinking guide](https://ai.google.dev/gemini-api/docs/thinking)
- **Thought summaries and billing.** Summaries appear only when enabled (`thinking_summaries`). A thought block may carry only a signature with no summary. Billing covers all thought tokens, not just the summary. — [Thinking guide](https://ai.google.dev/gemini-api/docs/thinking)
- **Thought preservation from Gemini 3.5 Flash on.**
  - "Thoughts from previous turns are now preserved by default." The service no longer strips earlier reasoning before the model sees it.
  - In generateContent, the model uses reasoning from all earlier turns "when thought signatures are present." To get this, pass "the full, unmodified conversation history (including thought signatures) in `contents`."
  - "preserved thoughts increase input token count over multiple turns." For simple, independent queries clients may clear them.
  - — [What's new in Gemini 3.5 Flash (Legacy generateContent), updated 2026-10-12](https://ai.google.dev/gemini-api/docs/generate-content/whats-new-gemini-3.5); [Developer's guide to Gemini 3.5 Flash, Agent Platform, updated 2026-10-15](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/guides/gemini-3-5-flash)
- **Community report: dropped signatures.** Clients that drop `thoughtSignature` when appending function calls to history get 400s on the second tool iteration. — [GitHub geminik23/ai-agents #2](https://github.com/geminik23/ai-agents/issues/2); [n8n community thread](https://community.n8n.io/t/gemini-3-pro-missing-thought-signature-in-function-calls-error-400/224332)
- **Community report: parallel calls.** `gemini-3-flash-preview` inconsistently emits `thought_signature` on parallel calls, which leads to 400s. — [Google AI Developers Forum](https://discuss.ai.google.dev/t/gemini-3-flash-preview-inconsistent-thought-signature-generation-in-parallel-function-calls-causes-400-errors-and-potential-silent-data-loss/118936)
- **Community report: Gemini 2.5.** A blog post says Gemini 2.5 is more forgiving: missing signatures degrade quality rather than hard-fail. This is unverified against official docs. — [HelixML blog](https://helix.ml/blog/adding-support-for-gemini-thought-signatures)

### Inferences
- In generateContent, hard enforcement covers only (a) Gemini 3 and later, (b) `functionCall` parts, and (c) the current turn. Older turns can be edited, and their signatures dropped, without a 400.
  - The cost is reasoning quality. From 3.5 Flash on, preserved thoughts in older turns are actually used, so stripping them changes model behaviour rather than just removing dead weight.
- In Interactions stateless mode, the "MUST resend exactly" rule leaves no documented exemption for older turns, unlike generateContent's current-turn rule. A client that compacts history under `store=false` is acting outside the documented contract. I found no documented error for doing so.

### Gaps
- Whether Interactions stateless mode returns a validation error, or silently degrades, when old thought blocks are removed. No page states an error behaviour.
- The Agent Platform's exact 400 message text, and whether Vertex validates older turns. The page is silent on the latter.
- Whether signatures from one model are accepted by another in generateContent. Only the Interactions "backend manages compatibility" claim and the Flash-Lite drop rule were found.

## Pairing: functionCall/functionResponse rules, and what happens when earlier turns are dropped, edited or reordered

### Takeaway
- Pairing is by `id`. Gemini 3 and later always issue one, and from 3.5 Flash on each `FunctionResponse` must match the call's `id`, `name` and count, exactly one per call.
- Mismatches do **not** raise an error in generateContent. They mostly produce **empty responses with `finish_reason: STOP`**.
- The only documented hard 400s are a missing current-turn signature and the interleaved parallel order `FC1, FR1, FC2, FR2`.
- Interactions uses `function_result` steps with a `call_id`.

### Cited Findings
- **IDs (generateContent).** Gemini 3 always returns a unique `id` with every `functionCall`: "Always include the exact `id` from the `function_call` in your `function_response`." The examples send function results as a Content with role `"user"` and append the model's full content, role `"model"`, first so its signatures are kept. — [Function calling (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/function-calling)
- **Parallel result order: the page is internally inconsistent.** One passage says "you don't need to return the `function_result` objects in the same order that the `function_call` objects were received," because results are matched by id. Another says to "include the responses in the same order as they were requested." — [Function calling (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/function-calling)
- **Grouping of parallel calls and responses (hard rule).** The required pattern is "FC1+signature, FC2, FR1, FR2." Interleaving as `FC1 + signature, FR1, FC2, FR2` returns 400. Only the first parallel `functionCall` carries a signature. — [Thought signatures (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures); [Agent Platform thought signatures](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/thinking/thought-signatures)
- **Strict matching from 3.5 Flash on.**
  - "Every `FunctionResponse` must include the `id` from the corresponding `FunctionCall`."
  - The `name` must match.
  - "Return exactly one `FunctionResponse` for each `FunctionCall` received."
  - Agent Platform adds that it "must strictly match the `id`, `name`, and execution count."
  - — [What's new in Gemini 3.5 Flash (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/whats-new-gemini-3.5); [Agent Platform 3.5 Flash guide](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/guides/gemini-3-5-flash)
- **Mismatch behaviour.** generateContent does not raise an error on mismatches. Instead, "mismatched responses cause the model to return empty responses with `finish_reason: STOP` in most cases." Agent Platform states the same. — [What's new in Gemini 3.5 Flash (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/whats-new-gemini-3.5); [Agent Platform 3.5 Flash guide](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/guides/gemini-3-5-flash)
- **Consistency of history (Agent Platform).** Clients should "either include full context (thoughts, function calls, responses) or omit it entirely," because "Partial context can degrade model performance." My extraction of the Gemini API legacy 3.5 page did not surface this sentence. — [Agent Platform 3.5 Flash guide](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/guides/gemini-3-5-flash)
- **Multimodal and inline content.** Multimodal function responses and inline instructions should go inside the function response, not as separate parts. On generateContent the supported types are PNG, JPEG, WebP, PDF and plain text, each as `inlineData`, referenced from `response` with `{"$ref": "<displayName>"}`. — [What's new in Gemini 3.5 Flash (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/whats-new-gemini-3.5); [Function calling (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/function-calling)
- **Interactions API pairing.**
  - A `function_result` step references the call by `call_id` (the `function_call` step's `id`) and `name`. Its `result` is a list of typed content blocks: text, image and so on.
  - Stateful continuation passes `previous_interaction_id` "along with the same `tools` list."
  - Stateless continuation (`store=false`) resends the original `user_input`, then all model steps (thought and `function_call`) "exactly as received," then the `function_result`.
  - — [Function calling (Interactions), updated 2026-09-23](https://ai.google.dev/gemini-api/docs/function-calling)
- **Other documented errors.** `Malformed_Function_Call` can occur when prompts require text before the tool call. — [Function calling (Interactions)](https://ai.google.dev/gemini-api/docs/function-calling)
- **Injected or synthetic function calls.**
  - For calls not generated by the API (client-made calls, or traces from models without signatures), the Gemini API legacy page allows the dummy signatures `"context_engineering_is_the_way_to_go"` or `"skip_thought_signature_validator"`. It strongly discourages injecting custom function calls.
  - Agent Platform documents only `skip_thought_signature_validator`, as a "last resort" that "will negatively impact model performance."
  - — [Thought signatures (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures); [Agent Platform thought signatures](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/thinking/thought-signatures)

### Inferences
- A client that elides or rewrites tool outputs in older turns but keeps the call/response pairs one-to-one and id-matched should see no error. Dropping a response while keeping its call, or the reverse, breaks the "exactly one per call" rule. The documented symptom is a silent empty `STOP` response, not a 400, so clients need their own pairing validation.
- A client that rewrites a `functionResponse` payload, for example to truncate or elide it, while keeping its `id` and `name`, is not hitting any validated field. The signature binds to the call part, not the response.

### Gaps
- No documented error, and no documented behaviour, for reordering whole earlier turns.
- The Gemini API legacy page's two statements on parallel result order are not reconciled.
- What the Interactions API does with an orphaned `function_call` or a mismatched `call_id`.

## Server-side history management, compaction, truncation and stateful conversation

### Takeaway
- The Interactions API (GA June 2026) gives server-side state: `store=true` plus `previous_interaction_id`, retained 55 days on paid tier and 1 day on free.
- It carries only inputs and outputs. Tools, system instruction and generation config must be resent each turn.
- Automatic compaction (~135k tokens) is documented only for **Managed Agents / Antigravity**. I found nothing about server compaction or truncation for plain model interactions.

### Cited Findings
- **Status.** The Interactions API is "Generally Available and recommended for all new projects" as of June 2026. generateContent is legacy but "remains fully supported." New models, tools and agentic features launch on Interactions. — [Interactions overview](https://ai.google.dev/gemini-api/docs/interactions-overview.md.txt) (no last-updated date shown in the fetched markdown)
- **State and retention.**
  - Storage is on by default (`store=true`), which enables `previous_interaction_id` and background execution.
  - `store=false` is incompatible with background execution and with `previous_interaction_id`.
  - Retention is 55 days on paid tier (configurable to 7, 14, 28 or 55) and 1 day on free. Stored interactions can be deleted.
  - — [Interactions overview](https://ai.google.dev/gemini-api/docs/interactions-overview.md.txt)
- **What carries over.** `previous_interaction_id` preserves only conversation history (inputs and outputs). `tools`, `system_instruction` and `generation_config` are interaction-scoped and must be re-specified. — [Interactions overview](https://ai.google.dev/gemini-api/docs/interactions-overview.md.txt)
- **Mixing agents and models.** One conversation can mix agents and models, for example Deep Research and then a standard model, linked by `previous_interaction_id`. Long-running tasks use `background=true`. — [Interactions overview](https://ai.google.dev/gemini-api/docs/interactions-overview.md.txt)
- **Managed Agents compaction.**
  - Managed Agents (the Antigravity agent, `antigravity-preview-09-2026`) run "a native context compaction step at around 135k tokens," automatically, to prevent token-limit errors and "context rot."
  - Conversation context (history, reasoning trace, tool use) travels via `previous_interaction_id`. Sandbox state travels separately via `environment`.
  - The page says neither what is compacted nor whether compaction can be configured or disabled.
  - — [Managed agents quickstart, updated 2026-10-01](https://ai.google.dev/gemini-api/docs/managed-agents-quickstart); [Antigravity agent](https://ai.google.dev/gemini-api/docs/antigravity-agent)
- **Long decode.**
  - On Gemini 4 Argon, long decode keeps state client-side in an encrypted `continuation_token` that "carries the model's state, including its thinking." Nothing is stored server-side.
  - Clients must not append partial output to `contents`.
  - — [Long decode, updated 2026-10-13](https://ai.google.dev/gemini-api/docs/generate-content/long-decode)
- **Token counting for compaction.** Clients should not use the summed `prompt_token_count` or `total_token_count` to decide when to summarize or trim history. Use `initial_prompt_token_count`, which the SDK computes and which is absent from REST responses (where the first hop's `promptTokenCount` serves instead). — [Long decode](https://ai.google.dev/gemini-api/docs/generate-content/long-decode)
- **Chat sessions in generateContent.** SDK chat history is a client-side convenience that handles signatures automatically. The docs describe no server state for generateContent. — [Agent Platform thought signatures](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/thinking/thought-signatures)
- **Community report: Antigravity.** Long Antigravity sessions compacted 4–7 times, and the user asks for a configurable threshold. — [Google AI Developers Forum](https://discuss.ai.google.dev/t/frustrated-with-aggressive-context-compaction-before-plan-execution/167983)
- **Community report: Gemini CLI.** The CLI (a client, not the API) compresses at a configurable fraction of the window and falls back to truncation if summarization fails. — [gemini-cli #21792](https://github.com/google-gemini/gemini-cli/issues/21792); [gemini-cli #19456](https://github.com/google-gemini/gemini-cli/issues/19456)

### Inferences
- A client using stateful Interactions cannot edit earlier history in place. It can only chain from an interaction ID, or start a new interaction. Client-side compaction therefore forces either stateless mode or a fresh chain.

### Gaps
- Branching from an older `previous_interaction_id`, a fork rather than the latest turn, is not documented either way.
- Behaviour when a stored chain exceeds the context window for plain (non-agent) Interactions.
- Whether the Agent Platform offers the Interactions API at all.

## Context caching: implicit and explicit `cachedContent`, prefix rules, and the effect of rewriting earlier parts

### Takeaway
Both forms of caching are strictly **prefix-based**: "Cached content is a prefix to the prompt."
- **Implicit caching** is on by default for Gemini 2.5 and later. The minimum is 4,096 tokens for Gemini 3 and 4 on the Gemini API. Agent Platform states a 90% discount and some 6,144-token minimums.
- **Explicit caching** is immutable except for TTL, Beta on generateContent v1beta only, and **not available in Interactions or on Gemini 4 Argon and later**.

### Cited Findings
- **Implicit caching (Gemini API legacy).**
  - Enabled by default on 2.5 and later, with "no cost saving guarantee."
  - Minimums: Gemini 4 Argon, 3.8 Flash, 3.6 Flash and 3.1 Pro Preview need 4,096 tokens. 2.5 Flash and 2.5 Pro need 2,048.
  - Guidance: "Try putting large and common contents at the beginning of your prompt" and "send requests with similar prefix in a short amount of time." Hits appear in `usage_metadata`.
  - — [Context caching (Legacy), updated 2026-10-13](https://ai.google.dev/gemini-api/docs/generate-content/caching)
- **Implicit caching (Interactions).** Works in both stateful and stateless modes. Hits are reported in `usage.total_cached_tokens`. `previous_interaction_id` "makes it easier for the system to reuse cached conversation history." — [Caching (Interactions), updated 2026-10-13](https://ai.google.dev/gemini-api/docs/caching); [Interactions overview](https://ai.google.dev/gemini-api/docs/interactions-overview.md.txt)
- **Explicit caching (Gemini API legacy).**
  - Beta, v1beta, supported on Gemini 3 and 2.5. Not supported on Gemini 4 Argon or later, and not in the Interactions API.
  - "Cached content is a prefix to the prompt." The model does not distinguish cached tokens from ordinary input.
  - TTL defaults to 1 hour, with "no minimum or maximum bounds."
  - "Changing anything else about the cache isn't supported": only `ttl` or `expire_time` can be updated.
  - Minimum size "varies by model." The examples cache `system_instruction` and `contents`.
  - — [Context caching (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/caching); [Caching (Interactions)](https://ai.google.dev/gemini-api/docs/caching)
- **Agent Platform (Vertex), implicit.**
  - Enabled by default for all projects. It can be disabled, or durable caching alone can be disabled.
  - "90% discount on cached tokens compared to standard input tokens." No storage fee.
  - In-memory caching covers many models. Durable caching covers only Gemini 4 Argon (and Cyber Permissive), 3.8 Flash and 3.8 Flash Cyber.
  - Minimums: 4,096 tokens for Gemini 3 and 4. 6,144 for 3.0 Flash Preview, 3.1 Pro Preview, 3.7 Flash and 3.8 Flash (implicit only). 2,048 for Gemini 2.
  - "A cached prefix expires no later than 24 hours after it's created, and most entries are evicted earlier."
  - — [Context caching overview, Agent Platform, updated 2026-10-15](https://docs.cloud.google.com/vertex-ai/generative-ai/docs/context-cache/context-cache-overview)
- **Agent Platform (Vertex), explicit.**
  - 90% discount on Gemini 2.5 and later, 75% on 2.0.
  - Default TTL 60 minutes, minimum 1 minute, no maximum. 10 MB per cached blob or text item. Storage is billed by duration.
  - Explicit caches "can cause additional caching beyond the specified content."
  - Caches are isolated by project. For agentic workloads the page recommends scoping caches to each user session.
  - Both cache types report hits in `cachedContentTokenCount`.
  - — [Context caching overview, Agent Platform](https://docs.cloud.google.com/vertex-ai/generative-ai/docs/context-cache/context-cache-overview)

### Inferences
- Because matching is prefix-based, rewriting, eliding or compacting any earlier part breaks the cache from the first changed token onward. This covers a tool output, a dropped thought signature, or a reordered part. In practice:
  - Editing late tool outputs keeps most of the hit.
  - Editing near the start, or replacing history with a summary, resets implicit caching to the system-instruction and tools prefix at best.
- Preserved thoughts (3.5 Flash and later) inflate the prefix, but they also stay cacheable only if they are resent byte-identically.
- Explicit caches are immutable, so a compacted history cannot update one. It needs a new cache. On Gemini 4 Argon and in Interactions, implicit caching is the only option.

### Gaps
- No page gives the exact matching granularity for implicit caching (token, part or content block).
- Whether a request using `cached_content` may also set `system_instruction` or `tools`. Not stated on the pages I read.
- Whether thought-signature bytes count toward the prefix match. I assume so, but this is not documented.

## Starting fresh from a compacted summary, and whether stale thought signatures may or must be omitted

### Takeaway
Google gives no explicit "restart from a summary" procedure. The documented rules do imply one.
- **In generateContent**, a new history that ends in a plain user text message makes that message the current turn, so no older signature is validated. Clients may omit stale signatures and old tool calls, at a quality cost. Any synthetic `functionCall` placed inside the current turn needs a dummy signature.
- **In Interactions**, clients start a new interaction without `previous_interaction_id`. Stateless clients are told to resend all thought blocks exactly, which a summary by definition does not.
- Agent Platform advises keeping full context or omitting it entirely, never partial.

### Cited Findings
- **Gemini API legacy.** Only the current turn is validated. The current turn starts at the latest user message with standard content, not a `functionResponse`. — [Thought signatures (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures)
- **Agent Platform.** "A turn begins with the most recent user message that is not a `functionResponse`." Its own guidance says manual handling is needed "when trimming history." — [Agent Platform thought signatures](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/thinking/thought-signatures)
- **Non-API-generated calls.** For these (client-executed calls, history from other models) use `skip_thought_signature_validator`. The Gemini API also accepts `context_engineering_is_the_way_to_go`. Agent Platform says this "will negatively impact model performance." — [Thought signatures (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures); [Agent Platform thought signatures](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/thinking/thought-signatures)
- **Omitting thoughts.** "either include full context (thoughts, function calls, responses) or omit it entirely. Partial context can degrade model performance." Clearing thoughts between turns is suggested for simple, independent queries. — [Agent Platform 3.5 Flash guide](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/guides/gemini-3-5-flash); [What's new in Gemini 3.5 Flash (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/whats-new-gemini-3.5)
- **Interactions stateless.** "MUST always resend all thought blocks exactly as they were received". — [Thinking guide](https://ai.google.dev/gemini-api/docs/thinking)
- **Interactions fresh start.** Omitting `previous_interaction_id` starts a fresh conversation. For Managed Agents the sandbox can be kept via `environment`. — [Managed agents quickstart](https://ai.google.dev/gemini-api/docs/managed-agents-quickstart)
- **Re-specify config.** Tools, system instruction and config must be resent on every interaction. — [Interactions overview](https://ai.google.dev/gemini-api/docs/interactions-overview.md.txt)
- **Third-party commentary (not official).** One guide suggests resetting chats at phase boundaries and summarizing in long agent loops to control preserved-thought token growth. — [we0.ai Gemini 3.5 Flash developer guide](https://we0.ai/es/articles/gemini-3-5-flash-developer-guide)

### Inferences
- **Safe generateContent pattern.**
  - Put the summary into a new history as a user text message, or as system instruction plus a user message, with no carried-over `functionCall`/`functionResponse` parts.
  - This omits all stale signatures, triggers no validation, and is consistent with the "omit it entirely" advice.
  - Carrying over raw old tool pairs without their thoughts is "partial context." It is legal outside the current turn but documented as harmful to quality.
- **Unsafe pattern.** Compacting mid-turn, while a tool loop is still running inside the current turn, is the risky case. Every step's first `functionCall` in that turn still needs its original signature, or a dummy one. The safer course is to compact only at a user-text turn boundary.
- **Interactions.** Client-side compaction is effectively a new conversation: a new interaction without `previous_interaction_id`, or `store=false` with a history the client owns. Signatures are attached to thought steps, so a summary carries none. No validation of this case is documented.

### Gaps
- No official Google page describes summary-based restart or client compaction for the raw model APIs. The only official compaction is the server-side Managed Agents step.
- Whether dummy signatures in non-current turns have any effect.
- Whether Interactions rejects stateless histories that lack thought blocks.

## Official guidance on long-running agents

### Takeaway
Google's official long-running-agent guidance is mostly product-level:
- Interactions with `background=true` and stored state.
- Managed Agents with automatic compaction at about 135k tokens.
- Long decode with client-held continuation tokens.
- Correct token accounting for compaction decisions.
- Tool-set discipline.

I found no detailed official doctrine on how to trim tool outputs.

### Cited Findings
- **Background execution.** Long-running tasks use background execution (`background=true`), which requires `store=true`. — [Interactions overview](https://ai.google.dev/gemini-api/docs/interactions-overview.md.txt)
- **Managed Agents.** Automatic compaction at about 135k tokens. Environment state is separable from conversation state. — [Managed agents quickstart](https://ai.google.dev/gemini-api/docs/managed-agents-quickstart)
- **Compaction triggers.** Base them on `initial_prompt_token_count` plus candidate and thought tokens, not on summed counts. — [Long decode](https://ai.google.dev/gemini-api/docs/generate-content/long-decode)
- **Tool-set discipline.** "Keep active set to 10-20 tools maximum." Validate function calls before executing them. — [Function calling (Interactions)](https://ai.google.dev/gemini-api/docs/function-calling)
- **Gemini 3.5 Flash.** 1M-token input context and 65k output. Preserved thoughts raise token use and latency over turns. Testers should check that the model's new step conciseness "does not trigger premature exits." — [What's new in Gemini 3.5 Flash (Legacy)](https://ai.google.dev/gemini-api/docs/generate-content/whats-new-gemini-3.5); [Agent Platform 3.5 Flash guide](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/guides/gemini-3-5-flash)
- **Agentic caching.** Agent Platform recommends scoping caches to each user session for agentic workloads. — [Context caching overview, Agent Platform](https://docs.cloud.google.com/vertex-ai/generative-ai/docs/context-cache/context-cache-overview)

### Inferences
- For a client that owns its own context management, Google's documented direction (Interactions plus server state plus Managed Agents compaction) pulls history control toward the server. A client-managed approach is best served by generateContent, where the contract is explicit: current-turn signatures only, one-to-one id pairing, prefix caching. Or it can use Interactions stateless mode, accepting the "resend exactly" rule.

### Gaps
- I found no Google developer-blog post or google-genai SDK changelog entry on compaction or history editing. Neither was searched exhaustively, given the tool-call budget.
- I found no official guidance on eliding or summarizing individual tool outputs within a live conversation.
