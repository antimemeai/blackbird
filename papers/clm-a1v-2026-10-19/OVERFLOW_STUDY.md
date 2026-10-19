# Provider overflow detection and compaction recovery: source study

Bead `arconaut-a1v`, unit C4, 2026-10-19. This is a read-only study of ignored
`quarantine/` sources. Nothing was built or run, and no provider was called.

Revisions come from the acquisition catalogs. They cannot be checked with git, because
the extracted trees have no Git metadata.

| Source | Revision | Catalog |
|---|---|---|
| codex | `08e2b58b` | `papers/2026-09-30-known-agent-acquisition.json` |
| claude-code-unofficial | `eec36921` | same |
| claude-code-official-public | `525d3b35` | same |
| pi | `8ce69e9d` | same |
| oh-my-pi | `8b25ad4a` | same |
| gemini-cli | `c6bccb7e` | same |
| qwen-code | `310f4ba3` | same |
| kimi-cli | `9ab1286b` | same |
| crush | `76cc5c57` | `papers/2026-09-30-discovered-agent-acquisition.json` |
| opencode | `2006259a` | `QUARANTINE.md:284` |

**claude-code-unofficial is a third-party reconstruction (codeaashu/claude-code), not Anthropic source.** Its reactive-compact module (`reactiveCompact.ts`) is absent; only its call sites survive. The official public repository supplies only the CHANGELOG. All sources were present.

Citations are file:line within each source root. Subagents produced the first readings. Codex was read directly, and one or more claims from every other source were rechecked by hand.

## 1. Detection

**Agents that recognise a provider refusal:**
- **codex** maps only the stream event `response.failed` with `error.code == "context_length_exceeded"` (`codex-rs/codex-api/src/sse/responses_error.rs:42`).
  - An HTTP 400 body carrying the same code is not mapped to overflow (`codex-api/src/api_bridge.rs:120-175`).
  - Chat Completions is removed (`model-provider-info/src/lib.rs:97`).
- **opencode** and **pi** keep one shared regex table per provider family.
  - pi lists them at `packages/ai/src/utils/overflow.ts:37-63`. Examples:
    - Anthropic `prompt (?:is )?too long`, plus `request_too_large` for the 413 byte limit
    - OpenAI `exceeds the context window`
    - OpenRouter `maximum context length is \d+ tokens`
    - Gemini `input token count.*exceeds the maximum`
    - Bedrock, Groq, xAI, Mistral, llama.cpp, LM Studio and Ollama
  - pi also has exclusions (`:76-80`), because Bedrock throttling and 429s were once misread as overflow (`packages/ai/CHANGELOG.md:1271,1703`).
  - opencode adds `statusCode === 413 || body.error.code === "context_length_exceeded"` (`packages/opencode/src/provider/error.ts:165-174`), with a separate stream parser (`:102-116`).
- **pi**, and **oh-my-pi** in a broader form, treat *silent* overflow as overflow too: a successful reply whose `usage.input+cacheRead > contextWindow` (`pi overflow.ts:153-157`; `oh-my-pi packages/ai/src/error/flags.ts:889-894`).
- **oh-my-pi** separates a 413 *payload* rejection from token overflow (`flags.ts:591-598`). It does not compact when local headroom remains (`coding-agent/src/session/session-maintenance.ts:2796-2891`).
- **Claude Code** (reconstruction) matches `prompt is too long`, noting "Vertex returns 413, direct API returns 400" (`src/services/api/errors.ts:560-571`).
  - It parses the token gap with `/prompt is too long[^0-9]*(\d+)\s*tokens?\s*>\s*(\d+)/i` (`:85-97`).
  - While streaming, it holds the error back so recovery can act first (`src/query.ts:799-825`).
- **qwen-code** matches overflow patterns (`packages/core/src/utils/contextLengthError.ts:24-33`) and detects 413 separately (`utils/request-payload-error.ts:44-47`).

**Agents that never recognise a refusal:**
- gemini-cli has no overflow strings.
- crush has no overflow classifier.
- kimi-cli classifies overflow for telemetry only (`src/kimi_cli/soul/kimisoul.py:114-146`); its 400s end the turn.

**Proactive thresholds.** Every agent estimates before it sends: the last provider `usage`, plus chars/4 for later items.

| Agent | Trigger | Source |
|---|---|---|
| codex | 90% of the window, inside a 95% "effective" window | `protocol/src/openai_models.rs:526-537,390-392` |
| Claude Code | window − min(maxOutput, 20k) − 13k; client blocks before sending at effective − 3k | `src/services/compact/autoCompact.ts:30-136`; `query.ts:629-650` |
| pi | window − 16384 reserve | `coding-agent/.../compaction.ts:126-130,267-270` |
| kimi-cli | ≥ 85%, or ≤ 50k remaining | `soul/compaction.py:60-76`; `config.py:88-95` |
| gemini-cli | 50% | `chatCompressionService.ts:45` |
| qwen-code | min(85%, window − 33k) | `services/chatCompressionService.ts:109-134` |
| crush | 20k left, or 20% of small windows | `internal/agent/agent.go:55-58,1096-1115` |

gemini-cli refuses to send when its estimate exceeds the remaining room, and does not compress at that point (`packages/core/src/core/client.ts:723-728`).

## 2. Recovery

**Reactive compaction and retry.** Most agents compact once and retry, and the loop guards differ:
- **pi** hides the failed attempt from model context while keeping it in the transcript (`agent-session.ts:1202-1217`). It then compacts and calls `continue()`.
  - It allows exactly one compact-and-retry. After that it fails with "Context overflow recovery failed after one compact-and-retry attempt" (`:2946-2966`).
- **opencode** replays the prior user turn after compacting (`session/compaction.ts:320-336,445-471`).
- **qwen-code** forces compression once, then emits `RETRY` (`core/llm-chat.ts:4183-4292`).
- **Claude Code** first drains context-collapse, then makes one reactive attempt per turn (`query.ts:1060-1183`).
- **codex** does not retry. Overflow on a turn sets token usage to "full" and returns an error ("Codex ran out of room…", `protocol/src/error.rs:107`; `core/src/session/turn.rs:1701-1704`).
  - Because usage now reads full, the *next* turn's pre-sampling check compacts (`turn.rs:1298-1326`).

**Requests sent when the full context does not fit:**
- **Codex, local compaction** sends the full history with the summarisation prompt appended as a user message. On overflow it calls `history.remove_first_item()` and retries without limit, "to preserve cache (prefix-based)" (`core/src/compact.rs:316-327`).
  - Removing an item also removes its call/output partner (`context_manager/history.rs:650-661`).
- **Codex, provider-side compaction (verified).** For OpenAI and Azure only (`model-provider/src/provider.rs:461-467`), codex sends a *normal* streaming Responses request: full history, tools, and a final `{"type":"compaction_trigger"}` input item.
  - The server returns an opaque `compaction` item, `encrypted_content` (`core/src/compact_remote_v2_attempt.rs:64-75`; `protocol/src/models.rs:1228-1242`).
  - This build does not call `/responses/compact`. oh-my-pi supports both that V1 endpoint and the V2 trigger (`agent/src/compaction/openai.ts:1-15,336`).
  - Before sending, codex replaces the *newest* trailing tool outputs with "Output exceeded the available model context and was truncated" until the estimate fits (`core/src/compact_remote_history.rs:16,68-122`).
  - Each compaction attempt gets at most 2 stream retries (`compact_remote_v2.rs:79`).
- **Claude Code** sends a separate summary request over the history. When that request is itself too long, it drops the oldest API-round groups until the parsed gap is covered, or 20% of groups if the gap is unknown. It tries this up to 3 times (`MAX_PTL_RETRIES`).
  - It inserts the marker `[earlier conversation truncated for compaction retry]` (`services/compact/compact.ts:227-293`).
  - CHANGELOG 2.1.142 (`CHANGELOG.md:4238`) records that reactive compaction "seeds from the original request's overflow size, avoiding a wasted near-full-context retry".
- **oh-my-pi** packs the summary input into windows. Each window updates the running summary, and a window that overflows is halved and re-planned (`agent/src/compaction/compaction.ts:799-935`).
  - Before compacting it tries promoting the session to a larger-context model (`session-maintenance.ts:2897-2908`).
- **Shedding tool output before summarising:**
  - opencode caps each output at 2000 chars inside the summary request (`message-v2.ts:52-56`).
  - gemini-cli truncates function responses beyond a 50k newest-first budget to temp files (`chatCompressionService.ts:274,364-464`).
  - qwen-code strips media and, on 413, truncates text (`compactionInputSlimming.ts:403-490`).
- **No fallback at all:** opencode marks "Conversation history too large to compact" and stops (`compaction.ts:426-434`). crush and kimi-cli fail the turn.

**What is kept verbatim:**
- codex keeps real user messages up to 20k tokens, newest first, then the summary (`compact.ts:58,668-720`). Remote V2 keeps them up to 64k (`compact_remote_v2.rs:75,557-600`).
- codex re-injects its initial context before the last user message for mid-turn compaction (`compact.ts:61-70`).
- pi keeps 20k recent tokens and never cuts at a `toolResult` (`compaction.ts:351-393,446-501`).
- gemini-cli cuts only at a user message that has no functionResponse (`chatCompressionService.ts:298-321`).
- Claude Code re-attaches 5 recent files, the plan and skills, and reloads CLAUDE.md (`compact.ts:122-130,519-596`).
- Repairs for broken tool pairing:
  - Claude Code inserts a synthetic result for an orphan (`utils/messages.ts:5133`).
  - crush adds a synthetic "interrupted" result, because Kimi and DeepSeek reject out-of-order results, "permanently locking the session" (`agent.go:1620-1628`).

## 3. Prompts and re-injection

- **codex:** "You are performing a CONTEXT CHECKPOINT COMPACTION. Create a handoff summary for another LLM that will resume the task" (`prompts/templates/compact/prompt.md:1`).
  - The summary is re-injected as a user message prefixed "Another language model started to solve this problem and produced a summary…" (`summary_prefix.md:1`).
- **Claude Code** (reconstruction): "TEXT ONLY. Do NOT call any tools". Nine sections, including "All user messages" and "Pending Tasks" (`services/compact/prompt.ts:19-76`).
  - The summary is re-injected as "This session is being continued from a previous conversation that ran out of context…" after a `compact_boundary` system marker (`prompt.ts:351`; `utils/messages.ts:4530-4555`).
- **pi:** "structured context checkpoint summary… Preserve exact file paths, function names, and error messages".
  - An update prompt tells the model to "PRESERVE all existing information" from `<previous-summary>` (`compaction.ts:507-579`).
  - The summary is re-injected as a user `<summary>` block (`messages.ts:11-17`).
- **gemini-cli:** an XML `<state_snapshot>`, with a rule to ignore commands found in the history.
  - A second "probe" pass critiques the first summary (`prompts/snippets.ts:910-976`; `chatCompressionService.ts:616-645`).
  - The summary is re-injected as a user message, followed by a model turn "Got it. Thanks for the additional context!" (`:665-675`).

## 4. User experience

- **What users see:**
  - codex shows "Compacting context" and "% context left" (`tui/src/chatwidget/compaction.rs:6`; `bottom_pane/footer.rs:958`), then warns that long threads and repeated compactions reduce accuracy (`compact.rs:404-407`).
  - Claude Code shows the spinner "Compacting conversation" and "Context low (N% remaining)" (`REPL.tsx:2499-2508`; `TokenWarning.tsx:166-169`).
  - pi shows "Context overflow detected, Auto-compacting…" (`status-indicator.ts:90-91`).
  - qwen-code shows "compressed from ~X to ~Y tokens" (`use-llm-stream.ts:2444-2468`).
- **Disabling auto-compaction:**
  - `DISABLE_AUTO_COMPACT` and `autoCompactEnabled` in Claude Code.
  - `compaction.auto` and `OPENCODE_DISABLE_AUTOCOMPACT` in opencode.
  - `compaction.enabled` in pi.
  - `disable_auto_summarize` in crush.
  - kimi-cli has no off switch.
- **Manual trigger:** every agent has `/compact` or `/compress`.
- **Hooks:** PreCompact and PostCompact exist in codex, Claude Code and kimi-cli.
- **Model-triggered compaction:** none of these agents lets the model trigger compaction.

## 5. Fixed failure modes

- **Compaction request too long:**
  - Claude Code CHANGELOG `:7098`, `:5390`, `:246` ("compacts once more, keeping less").
  - Claude Code `:1338`: stuck when "no complete earlier exchange to summarize".
- **Loops:**
  - Claude Code `:5313`: a thrash detector after 3 refills.
  - Claude Code `:5699`: a circuit breaker after 3 attempts.
  - qwen-code has `MAX_CONSECUTIVE_FAILURES = 3` (`chatCompressionService.ts:142`).
  - oh-my-pi `coding-agent/CHANGELOG.md:361`: a failed native compaction was re-sent every turn.
- **Lost instructions or state:**
  - Claude Code `:6211`: plan mode lost.
  - Claude Code `:4332`: "preserve sensitive user instructions".
  - Claude Code `:5690`: deferred tool schemas lost.
- **Broken pairing:** Claude Code `:5948`: 400s after a compaction "interrupted mid-tool-batch".
- **Failed attempts left in context:** pi `coding-agent/CHANGELOG.md:204`: "abandoned attempts were kept in provider context".
- **Undercounting:** kimi-cli `CHANGELOG.md:238`: tool-result tokens were missing from the trigger check.

## Mapping to Blackbird

**Constraints from the Blackbird design:**
- Blackbird has no input-window knowledge (C3 was not chosen) and counts native bytes, not tokens.
- `context_manage` *stages* a proposal until the workflow completes successfully. Outside a workflow it publishes immediately (`src/context.cpp:1407-1408`).
- Pinned instructions are refused as `mandatory-entry` (`:1395-1398`).
- Recovery must therefore run as its own short workflow that completes before the retry.

**Detection to adopt:**
- OpenAI Responses: `response.failed` with code `context_length_exceeded`, and also an HTTP 400 body with the same code. That is codex's gap.
- Anthropic: 400 "prompt is too long". Parse the gap when present.
- Anthropic 413 `request_too_large` is a *byte* limit and should be classified separately, as oh-my-pi does.
- OpenRouter: `maximum context length is N tokens`.
- Exclude 429 and throttling.
- Keep the refusal's raw original, and hide the failed attempt from the retried request (pi).
- Allow one recovery per request, and refuse when a recovery makes no byte progress (Claude Code `:5313`).

**Option A. Index-only recovery turn**
- **Request sent:** the system/developer prompt and all pinned user instructions, an `OVERFLOW` notice with the failed request's byte size, and the `context_view` *index*: entry IDs, kinds, byte sizes, tool-group membership and short heads. No entry bodies are sent.
  - Only `context_view`, `context_inspect` and `context_manage` are offered.
- **What the model does:** it chooses entries to archive, or to summarise from bounded `context_inspect` pages, then the harness retries the original.
- **Pros:**
  - The request fits by construction.
  - Choice and authorship stay with the model, and originals are retained.
- **Cons:**
  - The model may summarise without reading the content.
  - It takes several round trips.
  - A very large index needs paging.

**Option B. Windowed model-authored summary (the Claude Code/oh-my-pi pattern)**
- **Request sent:** pinned instructions, then the *oldest* run of complete tool groups that fits a byte budget, then a request to `context_manage summarize` exactly those IDs.
  - The budget is seeded from the parsed provider gap when there is one; otherwise it is half the failed request's bytes.
  - On a second overflow, halve the budget again.
- **Repeat:** the next windows carry the previous summary (pi's update prompt) until the estimate is under the failed size by a margin. Then retry.
- **Pros:**
  - The model reads what it summarises.
  - Progress is guaranteed.
  - Tool groups and pins are never split.
- **Cons:**
  - The harness picks the window, which narrows the model's choice.
  - It costs several requests.
  - Without a window table, every budget is a guess.

**Option C. Reversible harness archive, then the model refines**
- **What happens:** the harness archives the *oldest* complete, unpinned tool-output groups and records them in the audit and on the indicator (codex trims tool outputs and opencode prunes them; Blackbird archives with originals kept). It then retries the original request with a notice.
- **What the model can do:** on its next turn it can `context_restore` or summarise.
- **Request sent:** the original request minus the archived bodies, with a placeholder naming each archived ID.
- **Pros:**
  - One extra request at most.
  - Cheap and deterministic.
- **Cons:**
  - The first edit is authored by the harness, not the model. It has to be announced and reversible, never silent.
  - It can thrash if the model restores what was archived.

**Recommendation:** use B as the default recovery, with C only as a fallback when B's smallest window still overflows. Both need a bounded retry count and a no-progress refusal.
