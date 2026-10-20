# Tribunal: provider and protocol truth in CONTEXT_LIFECYCLE.md

Reviewer fault class: what providers accept, reject, cache or require. The design under
review is `docs/CONTEXT_LIFECYCLE.md` (draft for `arconaut-rd7.1`), checked against the
research notes in `papers/tool-output-lifecycle-2026-10-19/research_notes/`, the report in
`reports/`, and primary provider documentation read on 2026-10-20. No API was called and
no Blackbird code was run. Adapter behaviour was read at `870ec8c`
(`src/anthropic_provider.cpp`, `src/coding.cpp`, `src/request_cache.cpp`).

The OpenAI pages were fetched as raw Markdown. Other quotes come from WebFetch output that
the fetch tool marked as verbatim. None of the pages shows a "last updated" date except
Gemini's thought-signatures page ("Last updated 2026-10-13 UTC").

Severity reflects the consequence for Blackbird: high means a request the design calls
valid will be refused, or a central rule rests on a false premise. Medium means the design
pays a large avoidable cost, misclassifies a provider outcome, or plans a probe that
checks for the wrong thing. Low means a mislabelled basis, an imprecise statement or a
gap in the spec.

## Findings

### F1 (high). A change to `tools` or `system` breaks the chapter, but the design never makes it a cut

- **Sections:** §3 ("one binding for hidden reasoning"), §4.1, §14 ("tools change only at a workflow boundary").
- **Claim:** within a chapter the context changes only by appends. The listed cut triggers
  are archival, edit, restore, summary, rebind and recovery. §14 accepts that "tools change
  only at a workflow boundary" and treats that change as a cache cost only.
- **What the source says:** the checked prefix "has three parts: The top-level `system`
  prompt; The set of `tools`; Every `message` before the block". The table "What counts as
  an edit" includes "Add, remove, rename, or edit a tool in `tools` | Invalid" and "Change
  the top-level `system` string or blocks | Invalid". On new accounts the API "enforces the
  prefix check by default for accounts created on or after August 31, 2026, 00:00 UTC". The
  page also prescribes the append-only replacement: "Editing the `tools` array mid-session
  invalidates preserved thinking blocks. Leave the array as you first sent it, and change
  which tools the model can use by appending a `role: "system"` message that carries
  `tool_addition` or `tool_removal` blocks" (beta `inline-tools-2026-09-15`, or
  `mid-conversation-tool-changes-2026-07-01` for changes by reference). Source:
  https://platform.claude.com/docs/en/build-with-claude/preserved-thinking, read
  2026-10-20.
- **Source fact:** `request_tools()` appends `lua_tools_` to the native definitions
  (src/coding.cpp:2361-2373 at `870ec8c`). `finish_workflow` activates `pending_lua_tools_`
  at a successful workflow boundary (src/coding.cpp:6087). §11.1 also mentions requests
  that "override `input` or `tools`".
- **Consequence:** on Fable 5.1 and Opus, Sonnet and Haiku 5.5, the first request after a
  Lua tool activation sends every retained thinking block behind a changed `tools`. Each
  one is invalid. A new account gets a 400 ("Invalid `signature` in `thinking` block. The
  block is bound to a different conversation."). An older account has the check enforced
  only when `block_binding` is set, so it is not rejected, but per the docs the model no
  longer has valid reasoning. The same applies to a program request that overrides
  `tools`, and to any change in the instruction string. "One binding for hidden
  reasoning" does not hold within a chapter. The chapter therefore does not mean what
  §3 says, and §6.2's specification oracle checks only the `input` half of the prefix.
- **Direction:** extend §4.1 to the whole provider prefix. A request whose `tools` or
  `instructions` differ from the chapter's either (a) is a rebind-like cut with `p` at the
  tools level, so every reasoning entry is dropped under `drop_after_change`, or (b) on
  prefix-checked profiles is expressed as an appended mid-conversation system message with
  `tool_addition` / `tool_removal` blocks, keeping `tools` fixed for the session. Option
  (b) also keeps the cache. Add `tools_digest` and `instructions_digest` to the chapter
  binding, so a mismatch is detected and never silently sent. Add a profile field for the
  tool-change mechanism, because the beta header and route support are provider facts.

### F2 (medium). The Anthropic side request discards all reasoning and the whole cache when a documented append-only restriction exists

- **Section:** §11.1 (`palette_only`, `side_request_reasoning: drop_all`), §14 ("its side
  request pays a full miss, which is the price of honouring the model's choices").
- **Claim:** on Anthropic, the only honest restriction is to send only `context_manage`.
  That invalidates every thinking block, so the step's copy of the prefix drops all
  reasoning, which is valid.
- **What the source says:** the claims as stated are right. "Remove `thinking` blocks
  from the start of the history, from the end, or all of them | Valid (the model loses
  that reasoning)", and "Add, remove, rename, or edit a tool in `tools` | Invalid" (Preserved
  thinking, URL above). The prompt-caching table says "Modifying tool definitions (names,
  descriptions, parameters) invalidates the entire cache"
  (https://platform.claude.com/docs/en/build-with-claude/prompt-caching, read
  2026-10-20). However, Anthropic documents a mechanism that restricts tools without
  changing `tools`: `{"type": "tool_removal", "tool": {"type": "tool_reference", "name":
  "..."}}` blocks in a `role: "system"` message. The page says "The `tools` array itself
  never changes, so the cached prefix stays intact." The placement rule is that a system
  message "must immediately follow a `user` turn ... and must either be the last entry in
  `messages` or be immediately followed by an `assistant` turn". Mid-conversation system
  messages are "available on the Claude API, Claude in Amazon Bedrock, and Google Cloud"
  for "Claude Fable 5.1, Claude Mythos 5.1, ..., Claude Opus 5.5, ..., Claude Sonnet 5.5,
  and Claude Haiku 5.5" and need no beta header. Tool changes use `inline-tools-2026-09-15`.
  Source: https://platform.claude.com/docs/en/build-with-claude/mid-conversation-system-messages,
  read 2026-10-20. Preserved thinking also says: "To turn tool use off for a request, send
  `tool_choice: {"type": "none"}`. Don't remove `tools`." `tool_choice` is outside the
  prefix check.
- **Consequence:** at `direct_bytes` (448 KiB native, about 100k+ tokens), every C6a step
  on Anthropic pays full-price input for the whole context. With the restriction appended
  instead, it would read it at 0.05x (Opus/Sonnet 5.5) or 0.025x (Fable 5.1), see F3, and
  the model would keep its reasoning while judging its own context. If the side request
  also carries `cache_control`, it pays 1.25x to write entries that the cut invalidates
  right away. The step brief already sits at the end after the operator's user message,
  which is exactly where a system message may go. A side request at a different effort
  from the main request would also miss the message-level cache ("Changing the
  `output_config.effort` value always invalidates message blocks", Prompt caching).
- **Direction:** add a fourth `tool_restriction` mode, `appended_removal`. It keeps
  `tools` and `tool_choice: auto`, and appends one system message carrying the brief text
  plus a `tool_removal` block for every tool except `context_manage`. Keep reasoning
  (`side_request_reasoning: keep`) under it. Probe it in rd7.3, and keep `palette_only`
  as the fallback. In every mode, the side request should keep the main request's effort.
  It should place no new cache write beyond the already-written prefix.

### F3 (medium). The break-even arithmetic uses the wrong read price for the targeted models and omits the output cost of dropped thinking

- **Section:** §14 "Cost of a cut", the cost rule in §10.2. Report line 228.
- **Claim:** "On Anthropic's 5-minute tier the report puts break-even at about
  11.5 × (H − p − a) / a requests". The report derives this from "write 1.25x, cached
  read near 0.1x".
- **What the source says:** "Cache read tokens are 0.1 times the base input tokens price
  (see the table footnote for per-model exceptions)". The exceptions are: Fable 5.1 and
  Mythos 5.1 reads are "0.025x the base input price", and Opus 5.5 and Sonnet 5.5 are
  "0.05x the base input price" (Prompt caching, URL above). For OpenAI GPT-5.6 and later:
  "Cache read charge | 0.1× uncached input (0.05× for GPT-6.1 Sol)" and "Cache write charge
  | 1.25× the uncached input-token rate"
  (https://developers.openai.com/api/docs/guides/prompt-caching, read 2026-10-20). On the
  cost of dropping thinking: "dropping thinking blocks once per session increased output
  tokens by 2.5% at low effort, 3.9% at medium, 4.5% at high, and 20.1% at max" (Preserved
  thinking).
- **Consequence:** with reads at 0.05x, break-even is (1.25 − 0.05)/0.05 = 24 × R/a. With
  reads at 0.025x it is (1.25 − 0.025)/0.025 = 49 × R/a. The report's example of a 5% cut
  of the oldest rounds (R/a ≈ 17.4) becomes about 420 requests on Opus/Sonnet 5.5 and
  about 850 on Fable 5.1, not 200. The "about 9" late cut becomes about 19 and 38. On
  `gpt-6.1-sol`, the only model in Blackbird's OpenAI cache allowlist, it is also 24×.
  Every `drop_after_change` cut also re-creates the dropped reasoning as output tokens,
  which Anthropic measured at up to 20.1% more output at max effort. The formula leaves
  that out entirely. The model-facing cost rule and the operator's expectations therefore
  understate the cost of a cut by 2-4x on the newest models.
- **Direction:** make the read multiplier and write multiplier profile data per model
  (`cache.read_multiplier`, `cache.write_multiplier`), not a constant in the text. State
  the reasoning-drop output cost as a separate, cited term for `drop_after_change`
  profiles. Rewrite §14's examples from the profile values. The qualitative rule (fewer,
  larger, later cuts) survives.

### F4 (medium). A `chapter_start` breakpoint can read only an entry that some earlier request wrote at that position, and old entries expire

- **Section:** §14 "Anthropic caching (bvu)".
- **Claim:** a `chapter_start` breakpoint "at the last entry before `p`" lets "a late cut
  ... read the unchanged prefix that earlier requests wrote".
- **What the source says:** "Cache writes happen only at your breakpoint. Marking a block
  with `cache_control` writes exactly one cache entry ... The system does not write entries
  for any earlier position." On reads: "If none exists, it walks backward one block at a
  time ... It is looking for prior writes, not for stable content." "The system checks at
  most 20 positions per breakpoint, counting the breakpoint itself as the first." On TTL:
  "By default, the cache has a 5-minute lifetime. The cache is refreshed for no additional
  cost each time the cached content is used," measured "from the start of the request that
  writes or reads the cache entry". Anthropic's own advice: "Add a second breakpoint closer
  to that position from the start so a write accumulates there before you need it."
  (Prompt caching, URL above).
- **Consequence:** under the planned layout, entries exist only at each earlier request's
  moving-tail position. An entry is refreshed only when a later request reads it, and
  later requests read the newest tail. So a tail entry from more than 5 minutes ago is
  gone. `chapter_start` hits only when some request in the last 5 minutes had its tail
  within 20 positions before `p`. In practice that means only cuts of rounds appended in
  the last few minutes. Every deeper cut misses back to the `system` breakpoint, which is
  the outcome the breakpoint was meant to prevent. The design states the hit as a
  property; it is a narrow special case.
- **Direction:** decide in bvu whether to keep a persistent anchor breakpoint (the "spare")
  that advances in fixed steps of fewer than 20 positions, so an entry exists before a cut
  needs it. Weigh its 1.25x write per step, or the 2x write if it uses `ttl: "1h"`.
  Remember that "a 1-hour cache entry must appear before any 5-minute cache entries".
  Until bvu measures it, state `chapter_start` as reading recent writes only, and let the
  cold-cache note cover the rest.

### F5 (medium). On OpenAI GPT-5.6 and later, an unchanged prefix hits only at a cache lookup boundary, and the design places none for cuts

- **Section:** §14 "Keep the fixed prefix identical across chapters", "Affinity stays per
  conversation ... the unchanged prefix can still hit".
- **Claim:** keeping tools, instructions and the pre-`p` entries identical, with the same
  `prompt_cache_key`, lets the unchanged prefix hit after a cut.
- **What the source says:** "For GPT-5.6 and later, the cache lookup boundaries in the
  incoming request are: ... Implicit mode: The first 2 and latest 50 explicit breakpoints,
  the implicit breakpoint, up to 20 earlier eligible message endings, and the endpoint of
  the initial consecutive block of developer messages." "Top-level `instructions` cannot
  contain an explicit breakpoint." "A cached prefix remains eligible for reuse for 30
  minutes after its most recent write or reuse." On GPT-5.6+, `prompt_cache_key` mainly
  separates accounting: "Keys influence routing; they do not pin requests to a machine or
  guarantee a cache hit." (https://developers.openai.com/api/docs/guides/prompt-caching,
  read 2026-10-20). The SDK docstring says "up to the latest 80 breakpoints"
  (openai-python `response_create_params.py`), which disagrees with the guide's "first 2 and
  latest 50".
- **Source fact:** `stable_prefix` places one explicit breakpoint at the latest eligible
  text part in implicit mode with `ttl: "30m"` (src/request_cache.cpp:45, 111-115 at
  `870ec8c`). The fixed instructions are top-level `instructions`.
- **Consequence:** after a cut deeper than about 20 eligible message endings, the
  incoming request has no lookup boundary at or before `p`. Each closed round adds one
  ("the last tool response in a consecutive group"). The request then misses the whole
  prefix, possibly including the ~27 KiB of tools and instructions, unless the initial
  developer-message rule applies to Blackbird's layout. That was not checked. The claim
  that affinity keeps the unchanged prefix hitting is unverified at best.
- **Direction:** give the OpenAI profile a breakpoint plan as well. Put an explicit
  breakpoint on the first stable input item, since "the first 2" explicit breakpoints are
  always lookup boundaries. On a chapter's first request, put one on the last entry before
  `p`. Check whether Blackbird's first input items form an "initial consecutive block of
  developer messages". Measure the cached-tokens field for a deep cut in rd7.2.

### F6 (medium). `model_context_window_exceeded` is a truncated success, not an input overflow

- **Sections:** §13.2 `errors`, §12 Detection and Retry.
- **Claim:** `{ class = "context_overflow", stop_reason = "model_context_window_exceeded" }`
  triggers C4. "The failed attempt never appended anything, so it is not in the retried
  input."
- **What the source says:** "On Claude 4.5 models and newer, if input tokens plus
  `max_tokens` exceeds the context window size, the API accepts the request. If generation
  then reaches the context window limit, it stops with `stop_reason:
  "model_context_window_exceeded"` instead of returning an error."
  (https://platform.claude.com/docs/en/build-with-claude/thinking, read 2026-10-20). The
  stop-reasons table says "The response filled the model's context window." → "Treat the
  response as truncated." The example comment reads "The response is still valid but was
  limited by context window."
  (https://platform.claude.com/docs/en/build-with-claude/handling-stop-reasons, read
  2026-10-20). For a stop "soon after a tool call or tool result", Anthropic says: "To
  continue, pass the assistant turn back unchanged and append a new `user` message (with a
  `tool_result` for each `tool_use` block in that turn)" (Thinking, URL above).
- **Consequence:** this is an HTTP 200 with content, possibly including a `tool_use` and a
  progress-update block. Treating it as a failed attempt either drops a valid assistant
  turn and retries the same input, or archives history in reaction to an output-side
  stop. Neither matches the docs. The "nothing appended" invariant of §12 is false for this
  case.
- **Direction:** classify it as its own outcome, `output_window_exhausted`, carried on the
  accepted response. Append the turn as returned. Let the escalation levels in §10 see the
  size on the next request. Reserve `context_overflow` for refusals of the request.

### F7 (medium). Overflow detection: most providers return no code, and gap parsing needs more than literal substrings

- **Sections:** §13.2 ("providers return codes for most cases (`context_length_exceeded`),
  and literal phrases cover the rest"; "No regular expressions"), §12 Option A ("a budget
  seeded from the parsed provider gap").
- **What the sources say:**
  - Anthropic's error page lists only `400 - invalid_request_error` and `413 -
    request_too_large`, with no overflow code or message
    (https://platform.claude.com/docs/en/api/errors, read 2026-10-20). "prompt is too long:
    208310 tokens > 200000 maximum" appears only in community reports and reference
    implementations (OVERFLOW_STUDY.md §1, from the Claude Code reconstruction and pi).
    The reconstruction also notes "Vertex returns 413, direct API returns 400", so a
    `status = 413` rule cannot by itself separate bytes from tokens on cloud routes.
  - OpenAI: the Responses error `code` literal list in openai-python `response_error.py`
    has no `context_length_exceeded`. That code appears only in OpenAI's Codex fixture, as
    a stream `response.failed` with message "Your input exceeds the context window of this
    model. Please adjust your input and try again."
    (https://raw.githubusercontent.com/openai/codex/main/codex-rs/codex-api/src/sse/responses.rs,
    read 2026-10-20).
  - Gemini, DeepSeek and Mistral document no overflow error. Gemini's "The input token
    count (N) exceeds the maximum number of tokens allowed (M)." is forum-only
    (https://discuss.ai.google.dev/t/gemini-3-5-flash-the-input-token-count-n-exceeds-the-maximum-number-of-tokens-allowed-32768/170484).
  - xAI names only the code `context_length_exceeded`
    (https://docs.x.ai/developers/advanced-api-usage/context-compaction.md).
  - Meta documents a full message: "You passed {N} input tokens and requested {M} output
    tokens. However, the model's context length is only {context_length} tokens..."
    (https://dev.meta.ai/docs/error-handling.md).
- **Consequence:** the profile's error rules are mostly community-observed phrases. Yet the
  example `errors` block carries no `basis`, although §13.2 requires one on "every
  behavioural field". Extracting the provider gap that §12 relies on requires pulling two
  numbers out of a message ("N tokens > M maximum"). The pattern language (status, type,
  event, substring) cannot express that.
- **Direction:** give each error rule a `basis` and source. Add a bounded, non-regex
  number-extraction form for the gap (for example, "first integer after literal A, first
  integer after literal B"), or record that the gap is unknown and use the half-size
  budget. Add Bedrock and Vertex Claude rows with their own observed strings and statuses
  before those routes ship.

### F8 (medium). Mid-turn behaviour on Anthropic: the sources conflict, and the likely outcome is not a 400

- **Sections:** §6.3 (`user_boundary_only`: "Both cannot hold mid-turn"), §11.1, §12
  "Mid-turn on `user_boundary_only` profiles", §18 rd7.3.
- **What the sources say:**
  - The Thinking page says: "Required: within a tool-use turn, pass thinking blocks back",
    and "Within the latest assistant message ... you can't rearrange, edit, or partially
    drop them".
  - The same page describes degradation: "the API may strip thinking blocks that would
    create an invalid turn structure, or disable thinking when the conversation history is
    incompatible with thinking being enabled. To confirm whether thinking was active, check
    for the presence of `thinking` blocks in the response."
  - Preserved thinking lists "Remove `thinking` blocks from ... the end, or all of them" as
    "Valid". With `drop_block`, "the API drops each failing block and every thinking block
    after it, and the request succeeds". Its only stated mid-turn prohibition is about
    open rounds: "Don't compact between an assistant turn's `tool_use` and the
    `tool_result` that answers it."
  - Sources: https://platform.claude.com/docs/en/build-with-claude/thinking and
    https://platform.claude.com/docs/en/build-with-claude/preserved-thinking, read
    2026-10-20.
- **Consequence:** "cannot be valid" is stronger than the evidence. The documented outcomes
  of a mid-turn cut that drops current-turn thinking are a successful request without that
  reasoning (`drop_block`, or client removal from the end), or thinking silently disabled
  for the request. If rd7.3 checks only for a 400, it can record "accepted" while the model
  ran without thinking. The interim rule in §12 (end the turn on a mid-turn overflow) may
  be more costly than necessary.
- **Direction:** keep `user_boundary_only` as the conservative default, with basis
  `conservative`, not `cited`. In rd7.3, record for each case whether the response
  carries thinking blocks and what `input_transformations` lists (beta
  `thinking-binding-controls-2026-08-01`), not only the HTTP status. Include "client drops
  every thinking block at or after `p`, mid-turn, closed round" as an rd7.3 case beside
  text rendering and `drop_block`.

### F9 (medium). DeepSeek's rule covers every assistant message, so whole-round archival may be refused

- **Section:** §6.2 `must_keep` ("a cut that would drop reasoning on a retained assistant
  message is refused").
- **What the source says:** "for requests carrying the tools parameter, the
  reasoning_content must be fully passed back to the API in all subsequent requests — even
  for turns where the model did not perform a tool call. If your code does not correctly
  pass back reasoning_content, the API will return a 400 error."
  (https://api-docs.deepseek.com/guides/thinking_mode, read 2026-10-20; no exact error text
  documented).
- **Consequence:** `must_keep` guards only retained messages. An archival cut removes whole
  assistant messages, reasoning included. The DeepSeek page does not say whether a history
  missing whole earlier turns passes. "In all subsequent requests" can be read as
  requiring them. Blackbird would accept the cut and could then hit a 400 on the next
  request. The index justification holds: a harness-written assistant message without
  `reasoning_content` would plausibly 400, so it must not be assistant-role.
- **Direction:** mark DeepSeek archival `conservative`. Refuse archival cuts on that
  profile until a probe shows that removing whole rounds passes.

## Low findings

- **L1 §5.4, §13.2 Anthropic index role.** `index = { role = "developer", after_tool_output
  = true, basis = "cited" }`. Anthropic has no developer role. Blackbird's adapter renders
  developer and system items as user text prefixed `[Blackbird developer guidance]` and
  merges them into the adjacent user message (src/anthropic_provider.cpp:421-445 at
  `870ec8c`). That is valid ("Any text must come AFTER all tool results", Handle tool
  calls). The basis is Blackbird's own rendering, not a citation. Anthropic's real
  `role: "system"` messages must "immediately follow a `user` turn" and be "the last entry
  in `messages` or ... immediately followed by an `assistant` turn"
  (Mid-conversation system messages). If the adapter ever maps developer to system,
  an index placed after an assistant-only round would be a 400, and `after_tool_output`
  cannot express that. Record the rendering as the basis, and add a placement flag
  `after_user_turn_before_assistant` for system-role renderings.
- **L2 §6.2 Kimi.** Kimi k3 and kimi-k2.7-code say "you must keep the reasoning_content of
  every historical assistant message in messages as-is" and "you must therefore (not
  optionally) keep the reasoning_content" (https://platform.kimi.ai/docs/guide/use-thinking-models,
  read 2026-10-20). No 400 is documented. Under `keep` a cut never drops retained
  reasoning, so the practical difference from `must_keep` arises only at rebind. Give those
  two models `must_keep`, or note the "must" in the profile source. GLM with
  `clear_thinking: false` and MiniMax also say "must", but state only quality and cache
  consequences; `keep` is right for them.
- **L3 §5.4 Mistral.** "Unexpected role 'user' after role 'tool'" is community-reported
  only; Mistral's error glossary has none (https://docs.mistral.ai/resources/error-glossary.md).
  The documented basis is the flow table "system * → user → assistant fc.1 → tool r.1 →
  assistant fc.2 → tool r.2→ assistant → user"
  (https://docs.mistral.ai/studio/conversations/function-calling), which also implies no
  system message mid-history. Record the basis as the flow table plus community reports.
- **L4 §6.3 Gemini.** "Gemini validates only the current turn" is verified: "Strict
  validation is enforced for all function calls within the current turn. (Only current
  turn is required; we don't validate on previous turns)"
  (https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures, last updated
  2026-10-13, read 2026-10-20). Gemini's current turn starts at "the most recent User
  message that contains standard content (e.g., text)", not at Blackbird's last operator
  entry. Any user-text item a program appends mid-turn moves Gemini's turn start. That
  loosens validation rather than breaking it, but the oracle for `retain_current_turn`
  should use Gemini's definition on that profile. Nothing may be inserted between
  "FC1 + signature" and the following parallel calls or responses.
- **L5 §13.2 overloaded event.** The rule `{ class = "overloaded", event = "overloaded_error"
  }` names the wrong field. The SSE event name is `error`, and the error type is
  `overloaded_error`: `event: error` / `data: {"type": "error", "error": {"type":
  "overloaded_error", "message": "Overloaded"}}`
  (https://platform.claude.com/docs/en/build-with-claude/streaming, read 2026-10-20).
- **L6 §13.2 retry of 429.** "A tier spend-cap 429 has no `retry-after` header and keeps
  failing until access resumes" (Errors page). The retry set treats every 429 as
  retryable. Add a rule that a 429 without `retry-after` is not retried as rate-limit.
- **L7 §13.2 `output.max_tokens = 16384` with `basis = "cited"`.** This is Blackbird's
  constant (include/blackbird/anthropic_provider.hpp:8), not a citation. Anthropic's advice
  is "At `high` and above, set a large `max_tokens`", and for Sonnet 5.5 agentic coding
  "set `max_tokens` to 128,000, the model's maximum"
  (https://platform.claude.com/docs/en/build-with-claude/effort, read 2026-10-20). Keeping
  16384 is right for the migration (§13.4), but mark it `conservative` (current
  behaviour).
- **L8 §11.1, §20 forced tool choice.** "Anthropic refuses a forced choice while thinking is
  on" is imprecise. Forced tool use is refused "on every request" on Opus 5.5, Sonnet 5.5,
  Fable 5.1 and Mythos 5.1, and on any model under manual extended thinking. "Adaptive
  thinking ... supports forced tool use" on the others, Haiku 5.5 included (Thinking
  page; Errors page "Forced tool use not supported"). The conclusion holds for the
  profile's four models except Haiku 5.5. Decision 7's real reason, keeping declining
  possible, is unaffected.
- **L9 §6.1, §6.2 rebind within Anthropic.** "The API removes the unreadable blocks before
  the prompt reaches the model. ... The API never edits your `messages` array, so the
  dropped blocks stay in your history. When the same history goes back to Claude Fable 5.1,
  its blocks are readable again." Anthropic says to "Keep passing thinking blocks back
  unchanged when you switch models" (Preserved thinking; Thinking). `drop_foreign` must
  treat all Anthropic models as one family, or a Fable→Opus→Fable switch loses reasoning
  for good. Sonnet and Haiku 5.5 blocks are also account-bound and "the API drops the
  block ... and the request succeeds" under another account. The chapter binding has no
  account or auth identity, so a reopen under a different Anthropic account (API key
  versus OAuth) is not recorded as a rebind. That costs no error, only silent reasoning
  loss.
- **L10 §5.6, §6.2 restore.** "Put back a `thinking` block you removed on an earlier request |
  Invalid for thinking blocks produced while it was gone" (Preserved thinking). Under
  `drop_after_change`, the restored rounds' own reasoning lies at or after `p`, so it is
  dropped. The frame semantics in §4.2 should say explicitly that inserted originals
  count as at or after `p` for the reasoning rule.
- **L11 §10.2 cold-cache note.** Anthropic measures TTL "from the start of the request that
  writes or reads the cache entry, not from the end of its response". OpenAI measures it
  from "most recent write or reuse". Idle time for the `[cache=cold ...]` note must be
  measured from the previous request's start. Long thinking responses can otherwise hide
  an expired cache.
- **L12 §6.3 OpenAI rendering of earlier reasoning.** "GPT-5.6 models instead default to
  rendering available reasoning from earlier turns. Use `reasoning.context` to select
  either behavior", with `current_turn` "does not render reasoning from earlier turns"
  (https://developers.openai.com/api/docs/guides/reasoning, read 2026-10-20). Under the
  default, a cut changes what reasoning the model sees, not only the cache. The profile
  should record the `reasoning.context` value in force so rd7.2's results are interpretable.

## Unverified

- **OpenAI `allowed_tools` on the ChatGPT/Codex route.** The API-key route is documented
  (see Verified). Nothing documents `chatgpt.com/backend-api/codex`. OpenAI's own Codex
  client declares `pub tool_choice: String` and never sends the object form
  (https://raw.githubusercontent.com/openai/codex/main/codex-rs/codex-api/src/common.rs,
  read 2026-10-20). The design already requires a probe. Expect it may fail there.
- **OpenAI behaviour after an in-place edit or drop of older items, and after a rebind
  into OpenAI.** The docs publish no binding of encrypted reasoning to prior content. The
  community errors "reasoning was provided without its required following item" and
  "function_call was provided without its required reasoning item" remain community-only.
  rd7.2 is the right instrument.
- **Whether Blackbird's first OpenAI input items count as an "initial consecutive block of
  developer messages"** (F5). Not checked against the adapter.
- **Whether Anthropic caches tool loops without `cache_control`.** The Thinking page says
  caching in tool loops "occurs automatically, even without explicit `cache_control`
  markers". The mid-conversation page says "Caching only happens when the request includes
  `cache_control`". The pages conflict. This bears on the report's statement that every
  Anthropic request pays full uncached input today.
- **Overflow strings on Bedrock, Vertex and Foundry Claude.** No primary source was read.
- **DeepSeek whole-round archival** (F9) and **Mistral role ordering** (L3) have no
  documented outcome.
- **`tool_removal` availability per model** (F2). The mid-conversation page lists models
  for system messages, and the beta header for tool changes. It was not checked whether
  every listed model accepts `tool_removal` under `inline-tools-2026-09-15` on each
  platform.
- **Whether a `cache_control` breakpoint on a `tool_result` still matches** after the
  index text block is merged into the same user message behind it. The hash is "the
  prefix ending at that block", which suggests yes, but this was not confirmed.

## Verified as correct

- **§4.1, Anthropic.** The edits listed as invalid are right: editing, reordering or
  deleting earlier messages; clearing or shortening an earlier `tool_result`; changing
  `system` or `tools`. So is the list of valid changes: appending, removing thinking from
  the start, from the end or all of it, and changing `cache_control` markers (Preserved
  thinking). A non-append change of `input` costs a rewrite from `p` on every documented
  cache: Anthropic, OpenAI ("Append new messages rather than rewriting earlier turns"),
  Together, Fireworks and Cerebras (research notes). The rule is necessary for validity
  only on the prefix-checked Anthropic models (and Bedrock and Foundry Claude). For every
  other provider it is a cache discipline, not a validity rule. Uniform treatment is still
  sound.
- **§6.2 `drop_after_change`.** It is valid on Fable 5.1 and Opus, Sonnet and Haiku 5.5:
  blocks before `p` keep an unchanged prefix, and dropping every block at or after `p`
  leaves "an unbroken run of the original sequence". "Cutting turns out of the middle.
  Removing individual turns invalidates every thinking block after them" confirms that the
  blocks after `p` must go. Mythos 5.1 and pre-Fable-5.1 models "don't run the prefix
  check", so `keep` is right for them.
- **§6.2 other rows.** Gemini `keep` (current turn only validated). Meta `keep_whole_or_drop`:
  "you cannot summarize or rewrite one, only keep it whole or drop it", and "Every
  `reasoning` item ... must be followed by an assistant message or a `function_call` before
  the next `user`, `system`, or `developer` message", returning HTTP 400 otherwise
  (https://dev.meta.ai/docs/protocols/responses.md, https://dev.meta.ai/docs/error-handling.md).
  Mistral `keep` (replay advised, no signature). xAI `keep` (encrypted reasoning optional;
  "must" applies to cache hits only). MiMo `keep` ("recommended"). DeepSeek `must_keep` for
  retained messages (documented 400).
- **§6.3 OpenAI wording.** "just ensure all items between the last user message and your
  function call output are passed into the next response untouched", together with "Our
  systems will smartly ignore any reasoning items that aren't relevant to your functions"
  (Reasoning guide). The docs set no rule for older items. "Only" is a fair reading,
  labelled correctly as "cited, probe pending".
- **§6.3 open round.** "Don't compact between an assistant turn's `tool_use` and the
  `tool_result` that answers it" (Preserved thinking). Gemini parallel-call rules agree.
- **§11.1 OpenAI `allowed_tools`, API-key route.** "configure an `allowed_tools` list in
  case you want to make only a subset of tools available across model requests, but not
  modify the list of tools you pass in, so you can maximize savings from prompt caching"
  (https://developers.openai.com/api/docs/guides/function-calling). The schema is
  `{"type": "allowed_tools", "mode": "auto" | "required", "tools": [...]}` (openai-python
  `tool_choice_allowed_param.py`).
- **§11.1 Anthropic.** "A different tools list invalidates every thinking block" is right
  ("Add, remove, rename, or edit a tool in `tools` | Invalid"). Dropping all reasoning from
  the side request's copy is valid. `tool_choice` is outside the prefix check, and only
  `auto` and `none` are accepted on the four newest models ("tool_choice: type "tool" and
  "any" are not supported for this model.").
- **§13.2 / §14 cache constants.** "up to 4 cache breakpoints", and the automatic
  breakpoint "uses one of the 4 available breakpoint slots". The 20-position lookback, with
  "a run of consecutive `tool_use` blocks counts as one position". A minimum of 512 tokens
  on Fable 5.1, Mythos 5.1, Opus 5.5, Sonnet 5.5 and Haiku 5.5. 5-minute default TTL and
  1-hour option, writes 1.25x and 2x. "Dropped thinking blocks ... the cached prefix changes
  from that block's position onward." `cache_creation_input_tokens` is separate from
  `input_tokens` ("total_input_tokens = cache_read_input_tokens +
  cache_creation_input_tokens + input_tokens"), which supports §14's demand that the
  adapter stop folding writes into input.
- **§13.2 Anthropic errors and effort.** 529 `overloaded_error`. 413 `request_too_large`
  for the byte limit (32 MB on Messages; "Cloudflare returns this error before the request
  reaches the API servers"). 400 `invalid_request_error`. All five effort levels (`low`,
  `medium`, `high`, `xhigh`, `max`) are supported on Fable 5.1, Opus 5.5, Sonnet 5.5 and
  Haiku 5.5. Adaptive thinking is the mode on all four. Thinking counts toward
  `max_tokens` ("`max_tokens`, which includes all thinking Claude generates in the current
  turn").
- **§14 OpenAI.** `ttl` `30m` is the only value. Cache matching is exact-prefix ("Cache reuse
  requires the entire rendered prefix to match"). The minimum is 1,024 tokens on GPT-5.6
  and later.
