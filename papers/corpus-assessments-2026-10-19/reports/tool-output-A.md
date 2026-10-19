# Large tool output in coding agents: truncation, spill and recovery (read-only source study)

Date: 2026-10-19. Snapshots under /Users/patrickbeam/projects/blackbird/quarantine/ (taken ~2026-09-30; hermes 2026-10-07).
Claude Code: bundle 2.1.286 (claude-code-official-distribution/distributions/2.1.286/), CHANGELOG, and live docs (labelled [source]/[changelog]/[doc]).
Nothing was built, run or installed. Citations are file:line (or bundle byte offset). Detailed per-reference sections follow the synthesis.
Spot-verified by grep in the parent pass: Codex markers (utils/string/src/truncate.rs:139-141, core/src/tools/context.rs:520, tui/src/exec_cell/render.rs:778),
goose (large_response_handler.rs:5, developer/shell.rs:907-910), crush (tools/bash.go:55,432-443), kimi (tools/utils.py:167, soul/toolset.py:1092),
hermes (tools/tool_result_storage.py:21, agent/context_compressor.py:856), Claude Code bundle strings ("Output too large (", "ctrl+o to expand",
MAX_MCP_OUTPUT_TOKENS, "characters truncated] ...", clear_tool_uses_20250919, "Old tool result content cleared").

## Comparison at a glance

| | Shell shape / limit | Full copy saved? | Model recovery | Ageing of old output | UI fold |
|---|---|---|---|---|---|
| Codex | head+tail middle elision, 10k "tokens" (=bytes/4) per model, `tool_output_token_limit`; 1 MiB in-memory head/tail pre-cap | No (rollout keeps truncated) | Re-run only | None per item; auto-compact at 90%/95% drops all tool items | 3 rows + `… +N lines (ctrl+t to view transcript)` |
| Claude Code | read back first 30,000 bytes (`BASH_MAX_OUTPUT_LENGTH`, max 150k); success: spill + 2 KB head preview; failure: 10k head+tail, no file | Yes, session `tool-results/` (64 MiB Bash, 1 GiB other) | Ordinary Read/Grep on path; replacements replayed on resume | Microcompact (keep 5, ≥20k tokens freed) persists to file; idle API `clear_tool_uses` (flagged off) loses; autocompact | 3 lines + `… +N lines (ctrl+o to expand)` |
| goose | tail-only: >2000 lines or >50,000 bytes → last 50 lines/10 KB | Yes, TempDir ring of 8 per stream (deleted on exit); generic >200k chars → system temp (never cleaned) | Shell `head/tail/sed` on path | Tool-pair summarization (off by default); auto-compact at 80% | first 10 + last 10, `/toggle` |
| crush | head+tail 30,000 display cells | Yes, `.crush/shell-output/` (7 days, 256 MB cap) | `view` tool on path | Whole-session summarize near limit only | 10 lines, `[click or space to expand]` |
| kimi-cli | head-only 50,000 chars / 2,000 per line | No (foreground); background tasks yes, session dir | Re-run (foreground); ReadFile on bg log | Whole-context compaction at 85% | Not rendered (failures: last 5 lines) |
| hermes | 40% head / 60% tail, 50,000 chars | Yes, `cache/terminal-output` (≤5M chars), generic spill >100k chars | `read_file`/`search_files` on path | Compaction at 50%/75%: one-line summaries, demotion stubs, `[Old tool output cleared to save context space]` | one-line rows |

## Patterns worth stealing
- **Spill before cut, then hand back a path plus metadata** (Claude Code `<persisted-output>`, hermes `full_output_path`, crush `[N lines truncated, full output: path]`). Report total size, lines omitted and the exact path. The model recovers with its ordinary read/search tools: no new retrieval tool and no re-execution of side-effecting commands.
- **Head+tail rather than head-only for shell output.** Errors and exit summaries live at the end. Codex/crush/hermes keep both ends, and hermes biases toward the tail (40/60). Kimi's head-only cut and Claude Code's head-only success preview are the weak cases.
- **Character-boundary-safe cutting** (Codex `truncate.rs`, goose char-boundary tail clip). Count in one declared unit and name it honestly.
- **Persist the replacement decision in the transcript** (Claude Code `content-replacement` entries). Resume then rebuilds the exact same request bytes (prompt cache stays valid), and the stub's path still resolves.
- **Reversible ageing:** when old output is cleared, write it to a file first and leave a stub naming the path (Claude Code microcompact `Tool result saved to: PATH ... Use Read to view`). A placeholder that names a working recovery route is far better than an anonymous `[cleared]`.
- **Paging hints with exact continuation parameters** (hermes web_extract footer with `read_file path=… offset=… limit=200`, kimi background `ReadFile(path=…, line_offset=1, n_lines=300)`, Claude Code Read `Call Read with offset=…`).
- **Two-stage bounding:** a bounded in-memory/stream buffer (Codex 1 MiB head+tail) protects the process; a model budget protects the request. Keep the raw stream on disk so neither stage loses data.
- **Fold in the UI by default, with a key to expand** (every reference with a TUI uses ~3–20 lines).

## Patterns to avoid
- **Dropping the full copy** (Codex, kimi foreground, crush grep/fetch, Claude Code failed-Bash excerpt, WebFetch remainder). The only recovery is re-running, which is wrong for non-idempotent or slow commands.
- **Recycled or short-lived spill paths:** goose's ring of 8 slots silently points an old path at a different command's output. Hermes deletes spill files after 24 h/7 d while history keeps the path, and Claude Code temp task files can be deleted by another process's cleanup. The path must outlive every transcript reference to it, including across resume.
- **Silent caps:** Codex `!` 1 MiB head-only with no marker, hermes execute_code stderr cut at 10 KB with no marker, crush fetch 100 KB, kimi's edge case that drops output with no message. Every cut needs a marker with counts.
- **Byte cuts that split UTF-8** (Codex 512 KiB pre-cap, Claude Code 30,000-byte read-back documented as chars, kimi background previews, hermes execute_code, crush fetch making the whole fetch fail).
- **Truncating before secret redaction / ANSI stripping** (hermes): a cut can split an escape sequence or a secret.
- **Unbounded paths:** MCP results with no limit (crush), images exempt from all budgets (Codex, Claude Code spill), unbounded in-memory shell capture (goose, crush background jobs), saved files truncated mid-structure (Claude Code MCP JSON at its cap).
- **Display-width or token-estimate units for the budget** (crush counts terminal cells, so ANSI codes count as zero). Budget in bytes or chars, and keep display width for the UI.
- **Separate stdout and stderr truncation that loses interleaving** (crush, Codex `!`). Keep one interleaved raw stream (Codex unified exec) or tag the chunks.
- **Recovery stubs that over-promise:** hermes demotion text says recover with `session_search`, which returns only the first ~4k chars. goose's tool description promises 2000 lines and delivers a 50-line tail.
- **Ageing that is unrecoverable by the model:** whole-history summarization (Codex local compact, crush, kimi, goose) keeps originals only for the UI or DB. Server-side `clear_tool_uses` leaves a placeholder the client does not control.

---
# Codex (quarantine snapshot ~2026-09-30): large tool output handling

Read-only source study of `/Users/patrickbeam/projects/blackbird/quarantine/codex/codex-rs/`. All paths below are relative to `codex-rs/`. Nothing was built or run.

## 0. The tool surface in this snapshot

- The model-facing shell is **unified exec** only: `exec_command` and `write_stdin` (`core/src/tools/handlers/shell_spec.rs:95`, `:117`). The old `shell` function tool and the `read_file`/`grep_files`/`list_dir` tools are **not in this snapshot**. No core handler exists for them. File reads and greps happen through `exec_command` (`cat`, `rg`, ...), so they follow the exec rules below.
- The only `read_file` left is `notes.read_file` in the remote history-notes extension (`ext/history-notes/src/tools.rs:78`). It reads private notes, not workspace files.
- `apply_patch` reports through the exec-style formatter (see 1c).
- The classic `core/src/exec.rs` pipe-capture path still serves user-typed `!cmd` shell commands (`core/src/tasks/user_shell.rs:225`, `ExecCapturePolicy::ShellTool`).
- Other tools: MCP tools (`core/src/tools/handlers/mcp.rs`), MCP resources (`mcp_resource.rs`), the web extension (`ext/web-search`), code mode (`core/src/tools/code_mode`), and the remote history/notes extension.

## 1. Truncation shape, constants and units

### Core primitive: middle elision (head + tail)
`utils/string/src/truncate.rs`
- `APPROX_BYTES_PER_TOKEN = 4` (`:4`). "Tokens" means bytes/4, rounded up (`approx_token_count`, `:77-80`). No tokenizer is used.
- `truncate_with_byte_estimate` (`:38-75`) splits the byte budget 50/50 (`split_budget`, `:132-135`), keeps the head and tail, and joins them with a marker. No newline is added around the marker (`assemble_truncated_output`, `:153-159`).
- Bytes mode (`truncate_middle_chars`, `:7`): the marker counts removed **chars**, and `split_string` cuts on char boundaries (`:92-130`).
- Tokens mode (`truncate_middle_with_token_budget`, `:15-36`): budget = tokens*4 bytes. Cuts use `floor_char_boundary`/`ceil_char_boundary` (`:59-60`). The marker reports ceil((total_bytes - max_bytes)/4) tokens (`:65-72`, `:145-151`).

`TruncationPolicy` (`protocol/src/protocol.rs:3386-3433`): `Bytes(usize)` or `Tokens(usize)`. `byte_budget()` = tokens*4. `Mul<f64>` rounds up.

`utils/output-truncation/src/lib.rs`
- `with_serialization_allowance(p) = p * 1.2` (`:16-18`). It is the "20% allowance for serialization and headers".
- `formatted_truncate_text` (`:20-31`) is middle truncation plus a warning header (see section 2).
- `truncate_function_output_items_with_policy` (`:115-203`) handles content-item arrays such as MCP and generic outputs. Text items are taken in order until the budget runs out. The item that crosses the budget is middle-truncated, and later text items are dropped and counted. **Images and encrypted content always pass through and are not charged against the budget** (`:162-167`, `:183-187`). Audio is charged at its estimated tokens and dropped whole if it doesn't fit.

### Default budget (per model) and config
- Every catalog model in `models-manager/models.json` (gpt-6-astra, gpt-6.1-sol, gpt-6-sol/luna, gpt-5.6-*, gpt-5.5, codex-auto-review, ...) has `"truncation_policy": {"mode": "tokens", "limit": 10000}` (e.g. `models.json:15-17`). That is 10,000 approx tokens = 40,000 bytes.
- Unknown or fallback models get `TruncationPolicyConfig::bytes(10_000)` (`models-manager/src/model_info.rs:127`; also `protocol/src/openai_models.rs:1088`).
- config.toml `tool_output_token_limit` (`config/src/config_toml.rs:337-338`, doc: "Token budget applied when storing tool/function outputs in the context manager") replaces the model policy and keeps its mode. For bytes-mode models it becomes tokens*4 bytes (`models-manager/src/model_info.rs:32-44`).
- Per-MCP-tool `output_token_limit` (`config/src/mcp_types.rs:85-87`, under `[mcp_servers.<server>.tools.<tool>]`) is "before the standard 20% serialization allowance". It is read in `codex-mcp/src/binding.rs:268-277` and used in `core/src/tools/handlers/mcp.rs:245-249`. Nested restrictions take the minimum (`mcp_types.rs:92-94`).
- `exec_command`/`write_stdin` take a `max_output_tokens` parameter. Its schema text reads "Defaults to 10000 tokens; larger requests may be capped by policy." (`shell_spec.rs:61`). `DEFAULT_MAX_OUTPUT_TOKENS = 10_000` (`core/src/unified_exec/mod.rs:79`). The effective budget is `min(max_output_tokens, model policy)` (`core/src/tools/context.rs:482-489`), so the parameter can **only lower** the budget, never raise it.
- Guardian history tool: `conversation_history_max_output_tokens`, default 4,000 (`config/src/config_toml.rs:586-588`). This is the reviewer, not the main model.

### 1a. Unified exec (`exec_command` / `write_stdin`): head + tail, two stages
1. **Collection cap (bytes, in memory).** `UNIFIED_EXEC_OUTPUT_MAX_BYTES = 1024 * 1024` (`core/src/unified_exec/mod.rs:80`). `HeadTailBuffer` (`core/src/unified_exec/head_tail_buffer.rs:11-124`) keeps the first 512 KiB and the last 512 KiB and counts the bytes dropped from the middle. Cuts are **raw byte cuts, not UTF-8 aware** (`:51-91`).
   - Each process has two such buffers (`core/src/unified_exec/process.rs:64-74`). `pending` is drained by each model poll (`std::mem::take`, `process_manager.rs:1609`). `transcript` is kept for the final UI/end event (`async_watcher.rs:464-468`).
   - stdout and stderr are **merged in arrival order** (`process.rs:369`, `combine_output_receivers`).
2. **Model formatting (tokens).** `ExecCommandToolOutput::response_text` (`core/src/tools/context.rs:546-571`):
   - Output policy = `min(max_output_tokens or 10k, model policy)`.
   - The whole response must fit `1.2 * model policy` bytes minus the header. The loop shrinks the policy until it fits, so history truncation never truncates it a second time.
   - Middle truncation runs via `formatted_truncate_text`, or via the omission-marker variant when the collection cap fired (`:495-522`).
- Per-call semantics: each `exec_command`/`write_stdin` returns only output produced **since the previous poll** (pending is drained). Poll windows: `MIN_YIELD_TIME_MS=250`, `MAX_YIELD_TIME_MS=30_000`, `MIN_EMPTY_YIELD_TIME_MS=5_000`, `DEFAULT_MAX_BACKGROUND_TERMINAL_TIMEOUT_MS=300_000` (`unified_exec/mod.rs:73-78`). `MAX_UNIFIED_EXEC_PROCESSES=64` (`:82`).
- `original_token_count` = approx tokens of **total bytes seen, including omitted bytes** (`process_manager.rs:659-663`, `1029-1033`).
- Sandbox-denial early exit uses `formatted_truncate_text` with `Tokens(UNIFIED_EXEC_OUTPUT_MAX_TOKENS)` = 1 MiB/4 = 262,144 tokens (`mod.rs:81`, `process.rs:345-347`). In practice that is not a meaningful bound before history truncation applies.
- Rejection messages: `EXEC_COMMAND_REJECTION_MAX_BYTES = 900` (`handlers/unified_exec/exec_command.rs:57`), `REJECTION_MESSAGE_MAX_TOKENS = 900` (`tools/events.rs:39`).
- UI streaming: `UNIFIED_EXEC_OUTPUT_DELTA_MAX_BYTES = 8192` per delta event, split on UTF-8 boundaries (`async_watcher.rs:36-42`, `:56`). The count is capped at `MAX_EXEC_OUTPUT_DELTAS_PER_CALL = 10_000` (`core/src/exec.rs:85`, `async_watcher.rs:68`).

### 1b. Classic exec (`core/src/exec.rs`, user `!` shell commands): head-only capture, then middle elision
- `EXEC_OUTPUT_MAX_BYTES = DEFAULT_OUTPUT_BYTES_CAP = 1024 * 1024` (`exec.rs:81`, `utils/pty/src/lib.rs:25`).
- `read_output` keeps the **first** 1 MiB of each stream (`append_capped`, `exec.rs:856-863`, `:1236-1240`). It keeps reading to EOF and discards the rest silently, with no omission marker or count. `READ_CHUNK_SIZE = 8192` (`:74`).
- `aggregate_output` (`exec.rs:865-911`) is **stdout followed by stderr, not interleaved**, capped at 1 MiB. Under contention stdout gets 1/3 and stderr 2/3, with unused stderr room rebalanced to stdout (`:894-899`).
- The `FullBuffer*` capture policies have no cap (`exec.rs:274-279`).
- Delta events: up to `MAX_EXEC_OUTPUT_DELTAS_PER_CALL = 10_000` events of up to 8 KiB raw chunks (`:1212-1233`). The comment at `:84`, "Aggregation still collects full output", is stale for `ShellTool`.
- To the model: `format_exec_output_str` = `formatted_truncate_text(model policy)` (`core/src/tools/mod.rs:128-136`), wrapped as a user-shell fragment (`core/src/user_shell_command.rs:14-17`).

### 1c. apply_patch (and other ExecToolCallOutput users through `ToolEmitter::finish`)
- `apply_patch` builds an `ExecToolCallOutput` with `aggregated_output = stdout + stderr` (`core/src/tools/runtimes/apply_patch.rs:208-214`). `finish` then calls `format_exec_output_for_model` (`core/src/tools/events.rs:389-406`, `core/src/tools/mod.rs:101-126`): `truncate_text` with the model policy, plus a metadata header.

### 1d. MCP tool results
- `McpToolOutput::response_payload` (`core/src/tools/context.rs:436-469`):
  - The result is converted by `as_function_call_output_payload` (`protocol/src/models.rs:2302-2337`). If `structuredContent` is non-null, **the whole structured JSON becomes a single text body**. Otherwise content items are used.
  - A `Wall time: ... Output:` header is prepended.
  - `truncate_function_output_payload` runs with `1.2 * policy`, where policy is the per-tool `output_token_limit` or the model policy. Text bodies get middle truncation. Item arrays are truncated per item (1. above).
  - The history override is `1.2 * policy` in tokens (`fallback_token_limit_override`, `context.rs:153-155`).
- UI/rollout event copy: `MCP_TOOL_CALL_EVENT_RESULT_MAX_BYTES = 1 MiB` (`core/src/mcp_tool_call.rs:124`). Results whose serialized form is over 1 MiB collapse to one text preview of the serialized JSON, middle-truncated (`:981-1023`).
- Transport bounds:
  - `MAX_MCP_STDIO_LINE_BYTES = 8 MiB`. If a line exceeds it, the transport is closed (`rmcp-client/src/bounded_stdio_transport.rs:22`, `:63-66`).
  - HTTP bodies are capped at 8 MiB **only** for discovery requests or the 2026-07-28 protocol. `events/stream` is capped at 1 MiB. Other HTTP responses have **no cap** (`rmcp-client/src/http_client_adapter.rs:191-195`).
- MCP resources (`list/read_mcp_resource`): the response JSON is serialized, then `truncate_text(1.2 * model policy)` runs (`core/src/tools/handlers/mcp_resource.rs:355-371`). There is no warning header. Comment: "Match regular MCP tool outputs by bounding the copy persisted to the rollout and injected into model context."

### 1e. Generic tools (no tool-specific truncation)
- Any `ToolOutput` without its own truncation is cut only at history record time (section 5) with `1.2 * model policy`. Extension tools get `ToolCall::response_byte_budget = min(tool max, 1.2 * policy bytes)` for direct calls; code mode is unbounded (`tools/src/tool_call.rs:167-181`).
- Web search extension: passes `max_output_tokens = policy.token_budget()` to the backend search API (`ext/web-search/src/tool.rs:120-129`). Search history context sent to it is cut to `ASSISTANT_CONTEXT_TOKEN_LIMIT = 1_000` (`ext/web-search/src/history.rs:10`, `:25`). Hosted web search inside the Responses API is server-side, so there is nothing client-side to see.
- Code mode: the script result is truncated to `max_output_tokens` (default 10k) with `formatted_truncate_text_content_items_with_policy` when all items are text, else per-item truncation (`core/src/tools/code_mode/mod.rs:316-332`). Nested tool calls inside code mode receive **raw, untruncated** results (`context.rs:466-469` comment "Code Mode keeps the raw result"; `code_mode_result` at `:446-468`).

## 2. What the model sees: exact strings

Marker primitives (`utils/string/src/truncate.rs:137-143`), no surrounding newlines:
- `…{N} tokens truncated…` (tokens mode)
- `…{N} chars truncated…` (bytes mode)

Warning header (`utils/output-truncation/src/lib.rs:28-30`):
```
Warning: truncated output (original token count: {original_token_count})
Total output lines: {total_lines}

{head}…N tokens truncated…{tail}
```

Unified exec collection-cap marker (`core/src/unified_exec/mod.rs:231-233`), inserted between head and tail with newlines (`head_tail_buffer.rs:106-124`):
`... {omitted_bytes} bytes omitted ...`
When this fires, the model text is (`core/src/tools/context.rs:495-522`):
```
Warning: truncated output (original token count: {n})
... {omitted_bytes} bytes omitted ...

{middle-truncated text}
```
In this variant **"Total output lines:" is not emitted**. The omission marker line is added only when the token truncation removed the inline marker.

Unified exec response header (`context.rs:524-544`), in order:
```
Chunk ID: {6 hex}
Wall time: {s:.4} seconds
Process exited with code {code}        (if exited)
Process running with session ID {id}   (if still running)
Original token count: {n}
Output:
```
The code-mode/structured variant has `chunk_id, wall_time_seconds, exit_code, session_id, original_token_count, output` (`shell_spec.rs:198-230`).

The test regex at `core/tests/suite/truncation.rs:638` matches this:
`Chunk ID: ...\nWall time: ... seconds\nProcess exited with code 0\nOriginal token count: \d+\nOutput:\nWarning: truncated output \(original token count: \d+\)\nTotal output lines: 150\n\n1\n2\n3\n.*…\d+ tokens truncated….*149\n150\n`

Exec/apply_patch format (`core/src/tools/mod.rs:114-125`):
```
Exit code: {code}
Wall time: {s:.1} seconds
Total output lines: {n}     (only if the line count changed after truncation)
Output:
{middle-truncated, no Warning header}
```
A timeout prefixes the content with `command timed out after {ms} milliseconds\n` (`mod.rs:139-149`).

MCP: `Wall time: {s:.4} seconds\nOutput:\n...` (`context.rs:442-456`). Item-array truncation appends `[omitted {n} text items ...]` and `[omitted {n} audio items ...]` (`output-truncation/src/lib.rs:191-200`).

Remote-compaction pre-trim placeholder (`core/src/compact_remote_history.rs:16-17`): `Output exceeded the available model context and was truncated`.

Images in a tool output for a model without image input are replaced by `UnsupportedMedia::IMAGE.render()` text (`core/src/context_manager/normalize.rs:330-376`).

## 3. Where the full output goes

**Nowhere durable.** No spill file, temp file or artifact store exists for tool output. Searching for tempfile/NamedTempFile/"saved to" outside tests finds nothing.
- Unified exec: only up to 1 MiB head+tail lives in memory, per poll (`pending`) and per process (`transcript`). It is dropped with the process. Everything beyond 1 MiB is discarded at collection.
- Classic exec: the first 1 MiB per stream is kept in memory. The rest is discarded at read time.
- Rollout (JSONL session file):
  - The `FunctionCallOutput`/`CustomToolCallOutput` response items are persisted **as produced by the handler**, i.e. already truncated to about 1.2 × 10k tokens. History-time truncation is not applied to the rollout copy (`core/src/context_manager/history.rs:495-496`: "Tool output truncation applies only to live history, preserving full rollout payloads"; `core/src/session/mod.rs:3496-3610`). Each item carries `history_truncation_token_limit` metadata so replay reapplies the original budget (`session/mod.rs:3503-3516`).
  - Default `ThreadHistoryMode::Legacy` (`protocol/src/protocol.rs:778-782`): `ExecCommandEnd` is transient and not persisted (`rollout/src/policy.rs:145`), so the 1 MiB UI copy is not stored. Legacy does persist `McpToolCallEnd`, already capped at 1 MiB (`policy.rs:131`, `mcp_tool_call.rs:981`).
  - `Paginated` mode: every `ItemCompleted` TurnItem is persisted (`policy.rs:96-110`). That includes `CommandExecutionItem` with `stdout`, `stderr`, `aggregated_output` and `formatted_output` (`protocol/src/items.rs:244-288`, `core/src/tools/events.rs:581-608`), which can hold up to the 1 MiB caps. That copy is for the UI/app-server rebuild, not the model.

## 4. How the model gets omitted output back

**No dedicated retrieval mechanism for truncated tool output exists in this snapshot.**
- No read-back tool or handle exists for a truncated chunk. `chunk_id` is a random 6-hex label (`unified_exec/mod.rs:235-240`) and nothing resolves it.
- `write_stdin` on a live session returns only **new** output; the drained middle is gone.
- What remains: re-run the command with filtering (`| head`, `sed -n`, `rg`, redirect to a file then read slices), or lower `max_output_tokens`. The tool-description text does not suggest this; it says only "possibly truncated" and "may be capped by policy".
- Remote `history.*` extension (`ext/history-notes/src/tools.rs:24-27`, `:84-96`): `history.list_windows`, `list_items` (with `max_chars_per_item`), `read_item` (with `offset_chars`/`limit_chars`) and `search_contents` call OpenAI backend endpoints `alpha/history/v2/*`. The stated purpose is "Recover prior conversation after a context-window reset". The model is told never to disclose its use. It is an ordinal/offset reader over server-side normalized history, so it can recover items **dropped by compaction**. Whether that store holds more than the truncated tool output can't be determined from the client. The client sends its truncation policy in header `x-openai-tool-output-truncation-policy` (`ext/history-notes/src/backend.rs:17`, `:64-72`), which suggests the server applies the same budget. The extension is backend-gated.
- `notes.*`: private model notes (≤ 1,000,000 UTF-8 bytes per file) that survive context transitions. The model must save things there itself. Nothing does it automatically for outputs.
- Session resume: history is rebuilt from rollout `ResponseItem`s, newest compaction checkpoint plus suffix (`core/src/session/rollout_reconstruction.rs:405-440`), with each item's saved budget reapplied (`replay_annotated_item`). Resume therefore reproduces the **truncated** outputs exactly and gains nothing. The paginated `CommandExecutionItem` copy is used to rebuild the UI, not model history.

## 5. Ageing: pruning, compaction, clearing

There is no per-item ageing or pruning of old tool outputs. Every output stays verbatim (at its truncated size) until compaction. Mechanisms:

1. **History record truncation** (one-time, at insert): `record_item_with_metadata` (`core/src/context_manager/history.rs:534-555`). Each tool output is truncated to `metadata.history_truncation_token_limit` or `1.2 * model policy`. It only ever cuts the handler's already-sized output.

2. **Auto-compaction trigger** (`core/src/session/context_window.rs:51-110`; `protocol/src/openai_models.rs:526-538`):
   - `auto_compact_token_limit = min(config model_auto_compact_token_limit, 90% of context_window)`.
   - Scope is either `Total` or `BodyAfterPrefix` (`model_auto_compact_token_limit_scope`).
   - Compaction is also forced at `effective_context_window_percent` (default 95) of the full window (`models-manager/src/model_info.rs:133`).
   - A fallback buffer from the token budget config can be added.
   - Manual `/compact` also exists.

3. **Local compaction** (`core/src/compact.rs:249-428`):
   - The whole history is sent with `SUMMARIZATION_PROMPT` (`prompts/templates/compact/prompt.md`: "You are performing a CONTEXT CHECKPOINT COMPACTION. Create a handoff summary for another LLM ...").
   - Replacement history = recent **user messages only**, newest-first up to `COMPACT_USER_MESSAGE_MAX_TOKENS = 20_000` and the oldest partially truncated (`compact.rs:58`, `:668-746`), plus one summary message prefixed by `SUMMARY_PREFIX` ("Another language model started to solve this problem and produced a summary ...").
   - **All tool calls and outputs are dropped.** If the compaction request itself overflows, the oldest history items are removed one by one (`compact.rs:308-322`).
   - Afterwards the user sees: "Heads up: Long threads and multiple compactions can cause the model to be less accurate. ..." (`:408-411`).

4. **Remote compaction v2** (`core/src/compact_remote_v2.rs`):
   - Before sending, `trim_function_call_history_to_fit_context_window` (`compact_remote_history.rs:68-124`) replaces trailing tool outputs, **newest first**, with "Output exceeded the available model context and was truncated" until the request fits the window. It stops at the first non-output item.
   - Retained after compaction: user messages and hook prompts (plus small non-progress agent messages ≤ `MAX_RETAINED_AGENT_MESSAGE_TOKENS = 10_000`), truncated to `RETAINED_MESSAGE_TOKEN_BUDGET = 64_000` (`:75-76`, `:505-529`, `:556-600`), plus the server's single opaque compaction item. Tool outputs survive only inside that encrypted item.

5. Recoverability after compaction:
   - The rollout is append-only. Pre-compaction items stay in the file and a `RolloutItem::Compacted { replacement_history }` checkpoint is added (`rollout/src/policy.rs:18`).
   - Reconstruction starts from the newest checkpoint, so the model has no local route back. The only route is the remote `history.*` extension, when enabled.

6. Modality normalization at prompt build: images and audio in tool outputs become placeholder text when the model lacks that modality (`normalize.rs:330-376`).

## 6. TUI folding

- Model-run commands (non-user-shell): compact preview of **3 screen rows**. `PREVIEW_LINES = 3` and `MAX_PREVIEW_LINE_BYTES = 16 * 1024` (`tui/src/tool_output.rs:19-20`), used through `output_preview_lines` (`tui/src/exec_cell/render.rs:194-201`, `:556-575`). The footer is `+{N} lines (ctrl + t to view transcript)` (`tool_output.rs:118-133`; hint from `tui/src/ui_consts.rs:12-17`).
- User `!` shell commands: up to `USER_SHELL_TOOL_CALL_MAX_LINES = 50` rows head and 50 tail, after wrapping. The ellipsis is `… +{N} lines (ctrl + t to view transcript)` (`render.rs:44`, `:117-184`, `:304-310`, `:576-625`).
- `TOOL_CALL_MAX_LINES = 5` (`render.rs:43`) is now used only for patch-failure output (`tui/src/history_cell/patches.rs:147`).
- MCP, dynamic-tool and plan previews: `DETAIL_PREVIEW_LINES = 3` (`tui/src/history_cell/activity_preview.rs:7`).
- Live streaming preview (`tui/src/exec_cell/live_output.rs:5-17`):
  - Everything is kept up to `LIVE_COMMAND_OUTPUT_MAX_BYTES = 1 MiB`.
  - After that only the first and last `LIVE_COMMAND_OUTPUT_MAX_LINES = 50` lines are kept, each line head/tail-capped at 1 MiB/(2*50+2) ≈ 10,280 bytes.
- The ctrl+t transcript shows the full `aggregated_output` from the end event (`tui/src/exec_cell/transcript.rs:1-2`, "retaining full command output"). That is at most what core kept (1 MiB). Interaction (`write_stdin`) output is deliberately left out of the expanded transcript (`tui/src/exec_cell/compact.rs:24`).

## 7. Traps and observations

- **UTF-8:**
  - The text truncators are char-boundary safe.
  - `HeadTailBuffer` and classic `append_capped` cut raw bytes, and the result then goes through `from_utf8_lossy` (`process_manager.rs:665`, `exec.rs:806-808`). A multibyte character split at the 512 KiB seam or the 1 MiB cut becomes U+FFFD.
  - Delta streaming is UTF-8 aware (`async_watcher.rs:56`).
- **Truncation before any full copy exists.** The 1 MiB caps fire at read time, so no complete copy ever exists to save. Classic exec's cap is **head-only and silent**: no marker, no omitted count. The model sees the "Total output lines" of the capped text, not the real count.
- **stdout vs stderr.**
  - Unified exec interleaves them in arrival order (PTY or merged pipes).
  - Classic exec concatenates stdout then stderr, so ordering is lost. Under the cap stderr is favoured 2:1.
  - Under PTY, stderr can't be told apart from stdout at all.
- **Units are approximate.** "Tokens" are bytes/4. The marker's token count comes from bytes removed beyond budget, not from the actual cut. Two different "original token count" numbers can appear in one output: the header counts total bytes including omitted bytes, while the Warning line counts the same value or the text actually seen.
- **max_output_tokens can't exceed the policy.** A model asking for more gets 10k tokens silently.
- **Double-truncation avoidance:**
  - The handler pre-shrinks to fit `1.2 * policy` minus the header (`context.rs:552-569`), so history doesn't insert a second marker.
  - Other tools rely on the 1.2 allowance alone.
  - Generic tools without their own truncation get history truncation **without** a Warning header, only the inline marker.
- **MCP:**
  - Images in MCP results are not charged against any budget (`output-truncation/src/lib.rs:162-167`), so a tool returning many images is unbounded in model input. Only the transport caps apply (8 MiB stdio line; HTTP uncapped except discovery/modern protocol/events).
  - `structuredContent` wins over `content`, so a large JSON blob replaces the friendlier text items and is then middle-truncated mid-JSON.
- **Code mode nested calls are raw.** Inside a code-mode script, tool results arrive untruncated. Only the script's final output is bounded.
- **Rollout:**
  - The rollout holds handler-truncated outputs. The comment "preserving full rollout payloads" means "not history-truncated", not "full original".
  - In Paginated mode, `CommandExecutionItem` can persist up to about 1 MiB × several fields (stdout, stderr, aggregated, formatted) per command, which can bloat the rollout.
  - For MCP, the event copy is collapsed when over 1 MiB, specifically to avoid multi-MB rollouts (`mcp_tool_call.rs:986-987`).
- **Stale comment:** `exec.rs:84`, "Aggregation still collects full output", is untrue for `ShellTool` capture.
- **Remote-compaction pre-trim** blanks the **most recent** trailing outputs, not the oldest, and only the contiguous trailing run.
- **"Total output lines:"** appears in exec format only when the line count changed. In the unified-exec omission-marker path it is never shown.

---

# Claude Code: how large tool output is kept out of the model request (snapshot 2.1.286)

Sources and labels
- [source] = Bun-compiled native binary `quarantine/claude-code-official-distribution/distributions/2.1.286/darwin-arm64/claude` (version 2.1.286, `metadata/latest.txt`; stable = 2.1.285). The JS is embedded twice. Offsets are byte offsets from `grep -abo` into that file, in the second (~176M-215M) copy. Short identifiers are minified names that only mean something inside their own chunk.
- [changelog] = `quarantine/claude-code-official-public/CHANGELOG.md`, with the version heading.
- [doc] = live docs fetched 2026-10-19. Where docs name versions newer than the snapshot (for example Read `allow_large` in v2.1.296+, idle compaction, WebFetch offset in 2.1.290+), the snapshot does not have them.
- Live corroboration: this research session runs a newer harness. Three of my WebFetch calls came back as `<persisted-output> Output too large (56.6KB). Full output saved to: ~/.claude/projects/<proj>/<session>/tool-results/toolu_….txt / Preview (first 2KB):`, and I recovered the content with ordinary grep/sed on that file. A Bash call that was auto-backgrounded at 120 s wrote to `/private/tmp/claude-501/<proj>/<session>/tasks/<id>.output`.

---------------------------------------------------------------------------

## 0. Architecture in one paragraph

There are four layers.
1. **Tool-specific shaping.** Each tool caps or paginates its own output: Read's token cap plus partial view, Grep `head_limit`, Glob's 100 files, WebFetch's 100K chars plus secondary-model extraction, MCP's token check, and Bash's read-back window.
2. **Generic persist-to-disk.** When a text-only tool_result is larger than the tool's `maxResultSizeChars` (clamped by a 50,000-char ceiling), the full text is written to `~/.claude/projects/<project>/<session>/tool-results/<tool_use_id>.txt|.json`. The model then gets a `<persisted-output>` stub containing the path and a 2,000-char head preview. It reads the rest back with the ordinary Read, Grep or Bash tools.
3. **Optional per-message aggregate budget.** This is behind a flag. When parallel results in one turn total more than 200,000 chars, the largest are persisted first.
4. **Ageing.** There is a server `context_hint` beta with a client microcompact fallback, an optional API `clear_tool_uses_20250919` edit after 65 minutes idle, and autocompact (summarization) near the window limit.

Replacements are recorded in the transcript (`content-replacement` entries), so a resumed session re-applies byte-identical stubs.

---------------------------------------------------------------------------

## 1. Truncation shape per tool, constants, configurability

### Generic persistence layer (applies to every tool whose result passes through it)
- [source] @183235008 module constants:
  `var Ote="<persisted-output>",UGn="</persisted-output>",L="<truncated-output>",K="</truncated-output>",Y="[Old tool result content cleared]"` and `var fTe=2000` (preview chars).
- [source] @177588881 global constants: `var fM=50000,WWo=4000,sXe=128000,Dxe=500000;var Mxe=4,GWo=400000,P_n=200000,...,zWo=1073741824,VWo=1e4`.
  - fM = 50,000 is the default persistence-threshold ceiling (chars).
  - Mxe = 4 chars/token is the estimate used in telemetry.
  - GWo = 400,000 is the fallback threshold.
  - P_n = 200,000 is the per-message aggregate budget.
  - zWo = 1 GiB is the cap on the persisted file.
  - VWo = 10,000 is the hook-output threshold.
- Threshold function [source] @183235008:
  `function rce(e,n,r=fM,s=!1){if(!Number.isFinite(n))return n;if(s)return Math.min(n,r);let o=R(J,{})?.[e];if(typeof o==="number"&&...o>0)return o;return Math.min(n,r)}`
  - effective threshold = `min(tool.maxResultSizeChars, tool.persistenceThresholdCeiling ?? 50000)`.
  - The GrowthBook flag `tengu_velvet_ibis` can override it per tool name.
  - `maxResultSizeChars = Infinity` means the result is never persisted (Read uses this).
- The decision is "length > threshold and no image/document blocks" [source] @183236900:
  `function POr(e,n){return!D(e)&&H(e)>n}function D(e){return Array.isArray(e)&&e.some((n)=>...n.type==="image"||n.type==="document")}`.
  H() counts only text chars.
- Shape: **head only**. The persisted file holds everything up to 1 GiB, and the model sees the first ≤2,000 chars, cut back to the last newline if that newline lies past 50% of the window:
  `function B4e(e,n){if(e.length<=n)return{preview:e,hasMore:!1};let s=e.slice(0,n).lastIndexOf("\n"),a=s>n*0.5?s:n;return{preview:e.slice(0,a),hasMore:!0}}` [source] @183237xxx.
- Empty result: `(${n} completed with no output)` [source] @183236300.
- [changelog] 2.1.2: "Changed large tool outputs to be persisted to disk instead of truncated, providing full output access via file references". 2.1.51: "Tool results larger than 50K characters are now persisted to disk (previously 100K)". 2.1.265: "Added a 1 GB cap on tool results saved to disk".

Per-tool `maxResultSizeChars` [source], from the `name:…,maxResultSizeChars:` list @181453348–207759963. The effective threshold is min(value, 50,000) unless the tool has its own ceiling.

| Tool | maxResultSizeChars | Effective persist threshold |
|---|---|---|
| Read (`lt`) | `1/0` (Infinity) | never persisted; has its own token cap |
| Bash / PowerShell | `get maxResultSizeChars(){return p5e()}` = 30,000 default; ceiling `persistenceThresholdCeiling:sXe` = 128,000 | 30,000 (4,000–128,000 via `bashOutputMaxChars`) |
| Grep | `maxResultSizeChars:20000` | 20,000 |
| Glob | `maxResultSizeChars:1e5` | 50,000 |
| WebFetch | `maxResultSizeChars:fM,skipAggregateToolResultBudget:!0` | 50,000 |
| Agent (Task) | `maxResultSizeChars:f8e` with `f8e=1e5` @179748520 | 50,000 |
| MCP tools | `maxResultSizeChars:1e5` (@203461815), plus a separate token check (below); `_meta["anthropic/maxResultSizeChars"]` override | 50,000 by default |
| Edit / Write | 1e5 | 50,000 |

### Bash (and PowerShell)
- [source] @183400212:
  `var VDr=150000,Szn=30000;function p5e(){return S(Je().bashOutputMaxChars)??Szn}function Sce(){let e=S(Je().bashOutputMaxChars);if(e!==void 0)return e;return MFe("BASH_MAX_OUTPUT_LENGTH",process.env.BASH_MAX_OUTPUT_LENGTH,Szn,VDr).effective}function S(e){...return Math.min(Math.max(e,WWo),sXe)}`
  - `p5e()` = the inline ceiling, which is the tool's maxResultSizeChars. Default 30,000; the setting is clamped to 4,000–128,000.
  - `Sce()` = the **read-back window**. It is `bashOutputMaxChars` if that is set, else `BASH_MAX_OUTPUT_LENGTH` (default 30,000, capped at 150,000; invalid values log `Invalid value "…" (using default: 30000)`, too-large values log `Capped from X to 150000`).
- Setting description [source] @176731459: "How many characters of a successful Bash or PowerShell command's output Claude receives inline (default 30000; values clamp to 4000-128000). Output past this is saved to a file and Claude receives a short preview plus the path. When set, this also replaces BASH_MAX_OUTPUT_LENGTH, which on its own only sizes the read-back window." `taskOutputMaxChars`: "Deprecated: no longer has any effect (the TaskOutput tool was removed)".
- Capture mechanism [source] @183819555, `class c$` (TaskOutput):
  - The normal local path is `stdoutToFile = true`. The child's output is written straight to a file `<tmp>/claude-<uid>/<sanitized-cwd>/<session>/tasks/<taskId>.output` (`hp()/bNt()`: `b(MT(),V(),"tasks")`, `${e}.output`).
  - At exit, `getStdout()` → `this.#m(e??Sce())` reads **the first Sce() BYTES** from offset 0: `kIe(n,0,e)`, then `u.toString("utf8",0,l)` @176205792.
  - If the file is larger than the window (`outputFileRedundant` false), the result carries `outputFilePath/outputFileSize/outputTaskId`.
  - The Bash tool then copies the task file (raw bytes, up to `Lce=67108864` = 64 MiB) into `tool-results/<taskId>.txt` (`ANt(Pe.outputFilePath,xr,Lce)` @187640390) and sets `persistedOutputPath/persistedOutputSize`.
- Kill caps [source] @180471956: `Sut=5368709120,Tqn="5GB",Lce=67108864`. Over 5 GB the process is killed with `Command killed: output file exceeded 5GB`. The appender writes `[output truncated: exceeded 5GB disk cap]`.
- In-memory fallback (not stdoutToFile, e.g. hooks or remote) [source] @183819555:
  - Buffer `Wun=8388608` (8 MiB). On overflow it spills to a file, and stderr chunks are prefixed `[stderr] `.
  - The model then gets **the last 5 lines** (ring buffer `getRecent(5)`) plus `\nOutput truncated (${s}KB total). Full output saved to: ${this.path}`. If the spill failed: `...The full output could not all be saved to ${path}; that file may be missing or incomplete.`
  - In a diskless session: `Output truncated (NKB total): later output was discarded because this session writes no files.`
- Successful result over the inline ceiling: head-only `<persisted-output>` with a 2 KB preview (see section 2). Leading blank lines are stripped and the end is trimmed first.
- **Failure (non-zero exit / tool error)**: **head + tail with middle elision**, 10,000 chars, no file path.
  - [source] @179164500: `var OU=1024,…;function PU(e){return"\n\n... [${e} characters truncated] ...\n\n"}function zc(e,n=r2t){if(e.length<=n+OU)return e;let r=Math.floor(n/2);return TQ(e,r,n-r,…)}`, where `r2t=1e4` @177588881.
  - It is applied to tool errors through `rE()` @185118586: `let r=EAr(e)…join("\n")…g=zc(r,s)`. For the ShellError (`uU`) the joined parts are `[`Exit code ${e.code}`, interrupted?…, e.stderr, e.stdout]`.
  - Trigger: input longer than 11,024 chars. The result is 5,000 head + 5,000 tail.
  - [doc] tools-reference "Output limits": "Failure | Inline up to roughly 10,000 characters; past that, a head-and-tail excerpt of that size cut from the read-back window, with no file path". Note the phrase "cut from the read-back window": the tail is the tail of the first 30 KB, not of the real output.
  - [doc] exit-1 is benign (counts as valid) only for grep, rg, egrep, fgrep, find, diff, test, `[`, git diff and git grep.
- Notebook cell outputs use `Tut()` @184605465: head `slice(0,Sce())` + `\n\n... [${g} lines truncated] ...`.
- [changelog] 2.1.2 "Changed large bash command outputs to be saved to disk instead of truncated"; 2.1.45 "RSS no longer grows unboundedly with command output size"; 2.1.261 added `bashOutputMaxChars`; 2.1.282 fixed "leaving large output files in temp".
- [doc] env-vars: "`BASH_MAX_OUTPUT_LENGTH` | Maximum number of characters of bash output that Claude Code reads back into a command's result (default: 30000; maximum: 150000). If you set the `bashOutputMaxChars` setting, Claude Code ignores this variable."

### Read
- Defaults [source] @179371671: `var q2r=25000,K2r=128;` with error class `File content (${e} tokens) exceeds maximum allowed tokens (${t}). Use offset and limit parameters to read specific portions of the file, or search for specific content instead of reading the whole file.`
  - `CLAUDE_CODE_FILE_READ_MAX_OUTPUT_TOKENS` overrides it if > 0.
  - `defaultFileReadingLimits ??= {maxSizeBytes:e2e, maxTokens: env ?? 25000}`, where `e2e=262144` (256 KB) @176974507.
- 2,000 lines by default [source] @179852862: `x$t=2000` and the prompt says "Reads up to ${x$t} lines by default".
- **No per-line 2,000-char truncation string exists in 2.1.286.** I grepped for "Lines longer than" and "characters will be truncated" and got nothing. The only line-width cap I found is ripgrep's `--max-columns 500` in Grep.
- Whole-file size gate [source] @182501407: `File content (${un(e)}) exceeds maximum allowed size (${un(n)}). Use offset and limit parameters…` It applies only when no `limit` is given. Files under `xW=10485760` (10 MiB) are read whole; others are streamed.
- Line-range byte gate: `maxSelectedBytes:B*K2r` = maxTokens × 128 bytes (3.2 MB at the default). Error text: "The requested line range contains over … of text…".
- **Token-cap partial view** (head only) [source] @184641478, used when the call has offset ≤1, no limit and no pages:
  - It shrinks the line count by an estimate and then by ×0.7 up to 6 times.
  - If even one line is too long, it slices by characters and drops a dangling high surrogate: `if(dn>=55296&&dn<=56319)Qt=Qt.slice(0,-1)`.
  - Explicit offset/limit reads over the cap still throw the error above.
  - [changelog] 2.1.145 "Improved the Read tool to return a truncated first page with a 'PARTIAL view' notice instead of a hard error…".
- Read results are never persisted (`maxResultSizeChars:1/0` @184624724), which keeps Read from ever pointing at a file that would itself need a Read.
- [doc] (newer than snapshot) `allow_large: true` on v2.1.296+ "is sized against the room left in the session's context window".

### Grep
- [source] @184657600: `maxResultSizeChars:20000`. Schema: "`head_limit` … Defaults to 250 when unspecified. Pass 0 for unlimited (use sparingly — large result sets waste context)." `offset` "Skip first N lines/entries before applying head_limit". `nzn=250`.
- Head-only pagination: `Fve(e,n,r=0){if(n===0)return{items:e.slice(r)…};let s=n??nzn,g=e.slice(r,r+s)…}`.
- ripgrep arguments [source] @184661664: `Ae.push("--max-columns","500")`. Long lines become rg's own "[Omitted long line …]".
- Marker: `[Showing results with pagination = limit: 250, offset: N]`. Count mode reports a total across all files.

### Glob
- [source] @184652526: `maxResultSizeChars:1e5`, `b=s?.maxResults??100`. Truncation note `qGn` @184651676:
  - `(Results are truncated. Consider using a more specific path or pattern.)`
  - `(Showing ${n} of ${e.totalMatches} matching files; ${r} more are not listed. Narrow the pattern or path to see the rest.)`
  - `(Showing the first ${n} files; there are more than ${e.totalMatches} matches. …)`
- Glob has no offset parameter, so the only recovery is to narrow the pattern.

### MCP tool results
- Token limit [source] @203458719: `var C=0.5,l=1600,P=25000;function u(){let e=a.MAX_MCP_OUTPUT_TOKENS;if(e!==void 0&&e>0)return e;let t=R("tengu_velvet_ibis",{})?.mcp_tool;…return P}`.
  - Default 25,000 tokens.
  - An image is estimated at 1,600 tokens.
  - Pre-check: a local estimate above 0.5 × the limit triggers an API token count (`sAe`), which is then compared with the limit (`YRt`).
- Main path [source] @210331965, `Mo()`:
  1. `if(n==="ide")return g;`
  2. If over the limit and `ENABLE_MCP_LARGE_OUTPUT_FILES===false`, it truncates: `tee()`.
  3. If the result contains images, it truncates: `reason:"contains_images"`.
  4. Otherwise it persists the full result to `tool-results/mcp-<server>-<tool>-<ms>.txt|.json`. A single text block is unwrapped to plain text; everything else is pretty-printed JSON. The model gets the `ktn()` message (section 2), which has **no preview**.
  5. If persisting fails: `Error: result (N characters) exceeds maximum allowed tokens. Failed to save output to file: …`.
- Legacy truncation shape (head only) [source] @203459119: `XRt` slices text to `u()*4` chars (100,000 by default). Images that don't fit are downscaled to 75% of the remaining budget.
- `_meta["anthropic/maxResultSizeChars"]` per tool [source] `Qo()` @210299226. [changelog] 2.1.91 "(up to 500K)". 2.1.98 "Fixed MCP tools with `_meta[\"anthropic/maxResultSizeChars\"]` not bypassing the token-based persist layer".
- Binary/non-text MCP content is written to disk: `${o}Binary content (${type}, ${size}) saved to ${path}` [source] @183403416. [changelog] 2.1.283 "images returned by MCP tools are now also saved to a file".
- Error results (`isError`) go through `zc` head+tail. For memory servers it is raised to `eE=65536` with a note.
- [doc] mcp: "Claude Code displays a warning when MCP tool output exceeds 10,000 tokens and limits output to 25,000 tokens by default… The warning threshold is fixed. Unless the server raises a tool's own limit, successful text results longer than 50,000 characters are saved to a file regardless of this variable." "…The file goes in the session's `tool-results` directory under `~/.claude/projects/`."

### WebFetch
- [source] @185081530: `var Kto=2000,Vto=10485760,…,zwt=1048576`.
  - Turndown (HTML→Markdown) gets only the first 1 MiB of HTML; past that it appends `\n\n[Content truncated due to length...]`. The 10 MiB constant is the maximum response size.
- [source] @185088611: `ZMt=1e5`. The content given to the secondary extraction model is `n.slice(0,1e5)+"\n\n[Content truncated due to length..."`. The main model receives only that model's answer, which is lossy by design.
- Preapproved-domain raw path `rno()`:
  - Budget `eLt=fM-2000` = 48,000 chars minus URL/header overhead.
  - Overflow: the verbatim head is followed by a secondary-model summary (`uxe=8000` chars) of the remainder. Marker: `[The verbatim page text stops here, ${ke.length} of ${B.length} characters in; re-fetching this URL returns the same split.` and then "What follows is a model-extracted summary…" or "The remaining N characters were NOT read…".
- Binary content types are persisted to disk (`webfetch-<ts>-<rand>`) with a path.
- The tool's own `maxResultSizeChars:fM` (50,000) is skipped by the aggregate budget.
- [doc] "WebFetch reads up to 100,000 characters of a page's content per call. On Claude Code v2.1.290 or later, the result for a longer page tells Claude how much went unread". The `offset` parameter is newer than the snapshot. [changelog] 2.1.117 "truncating input before HTML-to-markdown conversion".

### Agent/Task results
- The subagent's final text goes through the generic layer with threshold 50,000 chars (`f8e=1e5` clamped by `fM`). Above that it is persisted with a 2 KB preview. Subagent tool calls never enter the parent context.
- Background bash/agents write to `tasks/<id>.output`, and Claude is told to Read that path. [changelog] 2.1.0 "Fixed API context overflow when background tasks produce large output by truncating to 30K chars with file path reference". [doc] env-vars: TaskOutput was removed in 2.1.277; "Claude reads a background task's output file with `Read` instead".

### Hooks
- [source] `oce()` @183238508 has threshold `VWo=1e4`. Output above that is persisted with the same `<persisted-output>` stub. If persisting fails: `${head}\n\n[Hook ${r} truncated at ${s} chars — persist-to-disk failed: ${err}]`. [changelog] 2.1.89.

### Per-message aggregate budget (flagged)
- [source] @183238508 `ie()`:
  - It groups the tool_results produced by one assistant message.
  - If the fresh plus frozen sizes exceed `P_n=200000` chars, it persists the **largest fresh results first** until the group is under budget: `ne(): sort by size desc … while o>r`.
  - Exempt: tools with infinite size (Read) or `skipAggregateToolResultBudget` (WebFetch).
  - Gated by `R("tengu_hawthorn_steeple",!1)` in `ZPo`, default false.
  - Decisions are frozen per tool_use_id (`seenIds`) so the prompt-cache prefix stays stable.

---------------------------------------------------------------------------

## 2. What the model sees (verbatim)

Persisted (generic, Bash, hooks, Agent, Glob, Grep, WebFetch) [source] @183235008 `tye()`:
```
<persisted-output>
Output too large (56.6KB). Full output saved to: /Users/…/.claude/projects/<proj>/<session>/tool-results/<tool_use_id>.txt

Preview (first 2KB):
<first ≤2000 chars, cut at a newline if past 50%>
...
</persisted-output>
```
- The `...` line appears only when hasMore. Sizes are human-formatted by `un()`.
- When over the cap, the first line is instead: `Output exceeded the ${cap} persist limit; only the first ${cap} were saved to: ${path}` (1 GB generic, 64 MiB Bash).
- When the result could not be saved and the session is diskless/remote [source] `O()`:
```
<truncated-output>
Output too large (X). It could not be saved, so only the first Y are shown; the rest was dropped. If the tool can page or filter its results, call it again for the part you need.

<head>
</truncated-output>
```
- The microcompact replacement (section 5) [source] @198036000:
  `<persisted-output>Tool result saved to: ${path}${" (truncated to the first …)"}\n\nUse Read to view</persisted-output>`, or the bare `[Old tool result content cleared]`.

MCP over the token limit [source] @185069978 `ktn()`:
- First line: `Error: result (${n} characters across ${count} lines) exceeds maximum allowed tokens. Output has been saved to ${path}.\nFormat: ${Plain text | JSON with schema: … | JSON array with schema: …}`.
- Then format-specific recipes:
  - JSON: "probe the structure (e.g., jq 'type, length, keys?' path)…".
  - Plain text: "read ${path} in chunks of ~N lines using offset/limit", where N = floor(maxTokens×4×0.8/(maxLen+8)).
  - Very long lines: "Slice by character range via Bash … python3 -c 'print(open(…).read()[A:B])' in ~N-char spans".
- Plus: "If the Agent tool is available, do this inside a subagent so the full output stays out of your main context".
- Legacy variant (`MCP_TRUNCATION_PROMPT_OVERRIDE=legacy`): "You MUST read the content from the file at … in sequential chunks until 100% … ***If you did not read the entire content, you MUST explicitly state this.***" and "Bash output is limited to N chars".
- If the persist cap was hit: "Note: the output exceeded the persist byte limit; the saved file contains only the first X of it and may end mid-structure…".

MCP legacy truncation [source] @203459119: `\n\n[OUTPUT TRUNCATED - exceeded ${limit} token limit]\n\nThe tool output was truncated. If this MCP server provides pagination or filtering tools, use them to retrieve specific portions of the data. If pagination is not available, inform the user that you are working with truncated output and results may be incomplete.`

Read partial view [source] @184641478 (prefix `Vke="[Truncated: PARTIAL view — "`):
- `[Truncated: PARTIAL view — ${path}: showing lines 1-${n} of ${total} total (${tok} tokens, cap ${cap}). Call Read with offset=${n+1} limit=${n} for the next page, or Grep to find a specific section. Do NOT answer from this page alone if the answer may be further in the file.]`
- Long-line variant: `…showing the first ${k} of ${len} characters …; this file has very long lines and cannot be paginated by line. …`

Bash failure head+tail: `\n\n... [${N} characters truncated] ...\n\n` [source] @179164500.

Bash in-memory spill: `<last 5 lines>\nOutput truncated (NKB total). Full output saved to: <tasks/id.output>`.

Background: `Command was manually backgrounded by user with ID: X. Output is being written to: <path>.` [source] @184605465 `bUn`. Live wording seen in this session: "…Output is being written to: …/tasks/brmxue43a.output. … To check interim output, use Read on that file path."

---------------------------------------------------------------------------

## 3. Where the full output goes

| Artifact | Location | Cap |
|---|---|---|
| Generic persisted tool_result | `~/.claude/projects/<sanitized-cwd>/<sessionId>/tool-results/<tool_use_id>.txt` (or `.json` for arrays of text blocks, JSON.stringify(…,null,2)) [source] @177123757 `var Que="tool-results"; L_(){return kt(V(),ve)} kt(e,n){return Ce(T2e()??rg(n()),e,Que)}`; filename `eye(dir,id,isJson)=\`${id}.${json?"json":"txt"}\`` | 1 GiB, UTF-8-boundary-safe (`IIe`) |
| Bash overflow copy | `tool-results/<taskId>.txt` under the root session (`n_(s.session)`) | 64 MiB raw bytes |
| Bash working file (live) | `$TMPDIR/claude-<uid>/<sanitized-cwd>/<session>/tasks/<taskId>.output` | 5 GB, then the process is killed |
| MCP large result | `tool-results/mcp-<server>-<tool>-<ms>.txt/json` | 1 GiB |
| MCP / WebFetch binary | tool-results, with a derived name | n/a |
| Microcompacted result | `tool-results/<tool_use_id>.txt` | 1 GiB |
| Hook output | `tool-results/hook-<event>-<id>.txt` | 1 GiB |

- Writes are exclusive (`writeExclusive`). On EEXIST the code accepts an existing regular file and refuses links, non-regular files and multi-link paths, so re-persisting the same tool_use_id (on resume) is idempotent and symlink-safe [source] @183235008.
- Remote/"storageV5" mode can store the file through a storage API instead.
- [changelog] 2.1.83 "Fixed tool result files never being cleaned up, ignoring the `cleanupPeriodDays` setting": tool-results are swept with the session data. Task `.output` files live in temp and can be removed by another process's startup cleanup [source] @183819555: `<bash output unavailable: output file … could not be read (…). This usually means another Claude Code process in the same project deleted it during startup cleanup.>`

---------------------------------------------------------------------------

## 4. How the model gets omitted output back

- **Ordinary tools only. No dedicated retrieval tool exists.** The stub's path is read with Read (offset/limit, subject to the 25K-token cap and the 256 KB whole-file gate, so a large saved file must be paged), Grep, or Bash (`sed`, `jq`, `python` slicing). The MCP message tells the model to do exactly this, and suggests a subagent to keep the bulk out of the main context.
- WebFetch: the remainder is not saved. The model re-fetches (snapshot: same split; the docs' newer `offset` parameter fixes this) or uses curl. Glob: narrow the pattern. Grep: `offset`/`head_limit`. Read partial: offset/limit.
- Bash failures (head+tail, no path) and MCP legacy truncation are **not recoverable** except by re-running the command, for example redirecting to a file.
- **Resume**: stubs are recorded as transcript `content-replacement` entries `{type:"content-replacement",sessionId,agentId,replacements}` [source] @187867233. They are replayed on load (@187970090), and `jGn()` rebuilds `seenIds/replacements`, so the resumed request is byte-identical (prompt-cache safe). The tool-results files live under `~/.claude/projects/` and survive resume until `cleanupPeriodDays`. Bash task files under `$TMPDIR` may not survive, but overflow is copied into tool-results first. Forking copies the replacement records (@196775058).

---------------------------------------------------------------------------

## 5. Ageing

### a) Client microcompact (fallback for the server `context_hint` beta) [source] @179863500, @198036000
- Eligible tools: `Rt=new Set([lt,...pb,Gr,eo,Rg,_r,Ht,hn])` = Read, Bash, PowerShell, Grep, Glob, WebSearch, WebFetch, Edit, Write. MCP and Agent results are not eligible.
- Selection, `x1r(messages, keepRecent)`: keep the last `keepRecent` eligible tool_use ids and clear the older ones. Results that are already stubs (`Ol()`: starts with `<persisted-output>` or equals the cleared marker) are skipped.
- Fires only if tokensSaved ≥ `Tfn=20000`. Images and documents are counted at `Pl=2000` tokens.
- Replacement: each cleared result is persisted via `H()` into tool-results and replaced with `<persisted-output>Tool result saved to: PATH\n\nUse Read to view</persisted-output>` (so it is recoverable). If persisting fails, or the block held an image/document, it becomes `[Old tool result content cleared]` (not recoverable).
- Trigger: `createContextHintController` (main REPL thread only, gated `tengu_hazel_osprey`, default false).
  - Each request it may add beta `context_hint` with body `{context_hint:{enabled:true,target_tokens_saved:75000}}` (`tengu_hazel_osprey_floor`, default 75000), but only when ≥20,000 tokens would be clearable with keepRecent=5.
  - If the server rejects it (HTTP 422/424), or a stream error leads to the non-streaming fallback, the client runs the microcompact locally with `keepRecent:5` and retries ("retry:context-hint").
  - What the server does with an accepted context_hint is not visible in the bundle.
- Logged as `tengu_time_based_microcompact` with trigger "context_hint". A `microcompact_boundary` system message subtype exists.

### b) API context editing after idle: `clear_tool_uses_20250919` [source] @179865212
- Constants: `An={minIdleSeconds:3900,keepToolUses:5,clearAtLeastTokens:2e4,triggerToolUsesBelowCount:3,minTriggerToolUses:20}`.
  - Idle threshold is 65 minutes, i.e. past the 1-hour prompt-cache TTL, so the cache is already cold and clearing costs no cache.
- Edit sent: `{type:"clear_tool_uses_20250919",trigger:{type:"tool_uses",value:clearable-3},keep:{type:"tool_uses",value:5},clear_at_least:{type:"input_tokens",value:20000},exclude_tools:[every non-eligible tool name seen]}`.
  - Requires the `context-management-2025-06-27` beta (`CSe=O("context_management"` @177943681).
  - Requires ≥20 trigger tool uses and more than 5 to clear.
  - Results that already contain the marker are treated as excluded.
- Gated `tengu_zany_pike` (off / shadow / on, default "off").
- The server reports `context_management.applied_edits[].cleared_tool_uses/cleared_input_tokens`, which the client logs.
- Placeholder: server-defined. [doc] "The API replaces each cleared result with placeholder text indicating to Claude that it was removed." The exact string is not in the docs or the bundle.
- Recoverability: the client transcript keeps full content, but nothing is persisted for the model. It must re-run the tool.
- [doc] API defaults: trigger 100,000 input tokens, keep 3 tool uses, `clear_tool_inputs` false. Claude Code overrides all three.

### c) Autocompact (summarization) [source] @184743000
- Effective window = window − min(model max output, `Xmt=20000`).
- Threshold `WG()` = effective − `13000`, or `CLAUDE_AUTOCOMPACT_PCT_OVERRIDE`% of effective, whichever is lower (the override can only lower it).
- Warn at threshold − 20,000. Blocked at effective − 3,000 (`CLAUDE_CODE_BLOCKING_LIMIT_OVERRIDE`).
- Precompute starts earlier, at buffer fraction `zJ=0.2` (flag-tunable).
- `DISABLE_COMPACT` / `DISABLE_AUTO_COMPACT` / the `autoCompactEnabled` setting turn it off. `CLAUDE_CODE_AUTO_COMPACT_WINDOW` sets the window (env > setting > clientdata > experiment > model default).
- Thrash guard: "Autocompact is thrashing: the context refilled to the limit within 3 turns of the previous compact, 3 times in a row. A file being read or a tool output is likely too large…".
- [doc] "It clears older tool outputs first, then summarizes the conversation if needed."
- [changelog] 2.1.247: Sonnet 5 compacts at about 967K on 1M.

---------------------------------------------------------------------------

## 6. UI

- Completed tool output is collapsed by default [source] @183679300:
  - `var d=3,u=10`: the first **3** wrapped lines are shown, wrapped at `max(columns-10,10)`.
  - Then a dim `… +N lines (ctrl+o to expand)`. Label `ZPe(): \`… +${e} ${line(s)}\`` @185424053. Key `Y_("app:toggleTranscript","Global","ctrl+o")`.
  - With 4 lines in total, all 4 are shown rather than "+1 line".
  - For huge strings only 3×width×4 chars are processed, and the line count is estimated.
- Running Bash shows the last **5** lines (`x=5`) plus `+N lines` / `~N lines` and a byte count (@204272705). Progress polls the file tail every 1 s (`Gun=1000`, `zun=4096` bytes).
- Ctrl+O toggles the verbose transcript ([changelog], an entry around line 4932: "Changed `Ctrl+O` to toggle between normal and verbose transcript only"). MCP read/search calls collapse to "Queried {server}" ([changelog]). Clicking expands in fullscreen.
- User-pasted text over 10,000 chars becomes `[...Truncated text #N +M lines...]`, keeping 500 head and 500 tail chars (`$He=1e4,TM=1000`). This is input, not tool output.

---------------------------------------------------------------------------

## 7. Traps

1. **Bash window is bytes, documented as chars.** The read-back reads `Sce()` **bytes** (`kIe` → `Buffer.toString("utf8",0,l)`), so a multi-byte UTF-8 sequence split at the boundary decodes to U+FFFD. The overflow decision (`outputFileRedundant`) is also byte-based. Settings and docs say "characters".
2. **Failure tail is not the real tail.** Failure excerpts (head+tail, 10K) come from the read-back window, i.e. the first 30 KB of a file that may be larger. The actual final lines (often the error) are lost, and no path is given. Valid results get the path; failures do not.
3. **Truncation before persisting.** The 64 MiB Bash copy cap and the 1 GiB generic cap mean "full output" is not always full; the stub then says so ("only the first … were saved"). MCP's persisted JSON can be cut mid-structure. Generic `IIe` cuts at a UTF-8 character boundary; the Bash `ANt` copy is a raw byte cut.
4. **Images bypass persistence.** Results with image/document blocks bypass the char threshold entirely (`POr` returns false) and cannot be persisted ("Cannot persist tool results containing non-text content"). MCP results with images fall back to destructive truncation (images downscaled into the remaining budget, each counted at 1,600 tokens). Microcompact can only blank them (`[Old tool result content cleared]`).
5. **MCP has no preview.** The model gets only the path plus recipes, so every large MCP result costs extra read turns. `ENABLE_MCP_LARGE_OUTPUT_FILES=false` reverts to lossy truncation. The token count is an API call when the estimate is over half the limit. IDE MCP results are exempt.
6. **stdout/stderr.** In file mode both streams go to one file, so interleaving is preserved and `getStderr()` is "". In memory mode stderr is kept separately until spill, then tagged `[stderr] ` inline. For failures, stderr comes before stdout in the joined error text.
7. **Read token cap vs size gate.** The 25K-token whole-file read degrades to a partial view, but an explicit `limit` read over the cap is a hard error. Whole files over 256 KB are refused even when their token count would fit. Read results are never persisted, so a huge Read is limited only by these gates.
8. **WebFetch is lossy.** The main model normally sees a secondary-model extraction of at most 100K chars of converted Markdown. HTML is cut at 1 MiB before conversion. In the snapshot, "re-fetching returns the same split".
9. **Surrogates.** The changelog repeatedly fixes "no low surrogate in string" from truncation that split emoji (2.1.132, 2.1.136, 2.1.162). The Read partial view explicitly drops a trailing high surrogate.
10. **Cache stability.** Persistence decisions are frozen per tool_use_id and recorded in the transcript, so a cleared or persisted result is never re-inlined. Any design that re-decides per request would break the prompt cache.
11. **Temp-file lifetime.** Task output in `$TMPDIR` can be deleted by another Claude Code process's startup cleanup. Overflow is therefore copied into the session tool-results directory before the stub is built.
12. **Flags.** Several mechanisms are behind server flags (aggregate budget, context_hint, idle clearing, per-tool threshold overrides). The constants above are the snapshot defaults, not guaranteed live behavior.

---

# Large tool output handling: goose and crush (source study, snapshots ~2026-09-30)

Read-only study. Paths are relative to `/Users/patrickbeam/projects/blackbird/quarantine/`.
Nothing was built or run.

Snapshot caveat (goose): the old `goose-mcp` "developer" extension (text_editor view
limits, ripgrep tool) is **not in this snapshot**. Developer tools now live in
`goose/crates/goose/src/agents/platform_extensions/developer/` and expose only
`write`, `edit`, `shell`, `tree`, `read_image` (`developer/mod.rs:108-187`, dispatch
`mod.rs:204-256`). There is no read/view or ripgrep tool in the developer extension;
the instructions tell the model to use `rg`, `cat`, `sed` through the shell
(`developer/mod.rs:56-72`). `EditTools::file_read_with_cwd` (`developer/edit.rs:47-64`)
exists but is only called from tests. `goose-mcp/src/computercontroller` has no
web_scrape/fetch tool in this snapshot either.

---

## GOOSE

### 1. Truncation shape per tool type

**Shell (developer `shell`)** — `goose/crates/goose/src/agents/platform_extensions/developer/shell.rs`
- Constants (`shell.rs:158-163`):
  - `OUTPUT_LIMIT_LINES: usize = 2000`
  - `pub OUTPUT_LIMIT_BYTES: usize = 50_000`
  - `OUTPUT_PREVIEW_LINES: usize = 50`
  - `OUTPUT_PREVIEW_BYTES: usize = 10_000`
  - `OUTPUT_SLOTS: usize = 8`
  - Not configurable (plain consts).
- Trigger (`truncate_output`, `shell.rs:882-922`): `total_lines > 2000 || total_bytes > 50_000`
  (lines = `split('\n')` count, bytes = UTF-8 `len()`).
- Shape: **tail-only**. Preview = last 50 lines (`shell.rs:903-904`), then clipped to the
  last 10,000 bytes, snapped forward to a char boundary (`truncate_preview_bytes`,
  `shell.rs:924-938`). That byte clip exists for `\r` progress bars, minified files and
  base64 content (comment at `shell.rs:924-926`).
- Applied three times per call: once to stdout, once to stderr, and once to the
  interleaved rendering (`shell.rs:427-466`, slots `stdout-{slot}`, `stderr-{slot}`,
  `output-{slot}`).
- The tool description tells the model something different from what it gets:
  "The output of each stream is limited to up to 2000 lines, and longer outputs will be
  saved to a temporary file." (`developer/mod.rs:144-148`). Over the limit, the model
  actually gets a 50-line / 10 KB tail.

**Universal tool-result handler (all extensions, including MCP)** —
`goose/crates/goose/src/agents/large_response_handler.rs`
- `DEFAULT_LARGE_TEXT_THRESHOLD: usize = 200_000` (`:5`). Configurable through
  `GOOSE_MAX_TOOL_RESPONSE_SIZE` (`:7-11`). The unit is **Unicode chars**
  (`text.chars().count()`, `:26`).
- Shape: **whole replacement, no preview**. A text block over the threshold is replaced
  by a pointer message (`:26-36`). Each text block is checked separately. Image and other
  non-text blocks pass through unchanged (`:53-56`).
- Wired into every tool call's post-hook path: `agents/agent.rs:709-710` and
  `agents/state_machine/ops_toolcalling.rs:256-257`.
- The same limit also bounds skill supporting-file loads (`skills/supporting_files.rs:15-26`).

**ACP terminal mode (shell delegated to the ACP client)** — `goose/crates/goose/src/acp/fs.rs`
- `CreateTerminalRequest...output_byte_limit(OUTPUT_LIMIT_BYTES as u64)` = 50,000 bytes
  (`acp/fs.rs:76-85`). The ACP client does the truncation. Goose returns
  `"exit code: N"` plus `output_res.output` (`acp/fs.rs:300-314`). The response's
  `truncated` flag is never read. No marker, no spill.

**ACP `read` tool** (`acp/fs.rs:101-160`): passes `line`/`limit` to the client. Goose adds
no cap of its own beyond the 200k-char universal handler.

**tree** (`developer/tree.rs:28-80`): default depth 2 (`:17-19`), no entry-count cap,
output not truncated except by the universal handler.

**computercontroller / peekaboo (macOS only)** — `goose-mcp/src/computercontroller/mod.rs:540-548`:
**head-only**, 12,000 chars, no save:
`"{}\n\n[Output truncated. {} total chars.]"`.

**Fetch / grep / search**: no dedicated tools in this snapshot (search goes through `rg`
in the shell, so shell limits apply).

### 2. What the model sees (verbatim)

Shell notice (`shell.rs:851-866`), sent as a **separate content block** after the
output block, "so the model sees them as instructions rather than part of the command's
data output" (`shell.rs:468-470, 506-519`):
```
[{reason} Full output saved to {path}. Read it with shell commands like `head`, `tail`, or `sed -n '100,200p'` up to 2000 lines at a time.]
```
where `{reason}` is one of (`shell.rs:906-913`):
```
Output exceeded 2000 line limit ({total_lines} lines total).
Output exceeded 50000 byte limit ({total_bytes} bytes total).
```
(On Windows the command hint names PowerShell `Get-Content`/`Select-String`.) Only one
reason appears even when both limits are exceeded. Lines takes precedence. The notice
reports neither the count of omitted lines nor the size of the preview.

Other shell suffixes added to the output text (`shell.rs:479-503`):
`"\n\nCommand timed out after {} seconds"`,
`"\n\nOutput may be incomplete because stream draining timed out after process exit."`,
`"\n\nOutput collection error occurred; output may be incomplete: {error}"`,
`"\n\nCommand exited with code {code}"`.
Empty output becomes `"(no output)"` (`shell.rs:874-877`).

Universal handler (`large_response_handler.rs:31-35`):
```
The response returned from the tool call was larger ({N} characters) and is stored in the file which you can use other tools to examine or search in: {path}
```
If the write fails, the full content goes to the model anyway, with a prefix (`:40-44`):
`"Warning: Failed to write large response to file: {e}. Showing full content instead.\n\n{text}"`.

`structured_content` for shell is a `ShellOutput {stdout, stderr, exit_code, timed_out,
output_truncated, output_collection_error}` built from the *truncated* previews
(`shell.rs:188-207, 452-460`). I found no use of `structured_content` in the provider
formatters (`goose-provider-types/src/formats/*`; the only hit is a test), so it appears
not to reach the model.

### 3. Where the full output goes
- Shell: `std::fs::write(output_dir.join(label), output)` (`shell.rs:940-948`).
  `output_dir` is a `tempfile::TempDir` owned by the `ShellTool` (`shell.rs:325-341`).
  File names are `stdout-{slot}`, `stderr-{slot}`, `output-{slot}` with
  `slot = call_index % 8` (`shell.rs:428`). This is a **ring of 8 slots per stream**, and
  older files are overwritten. The file holds the lossy-decoded text
  (`from_utf8_lossy`, `shell.rs:821`), not the raw bytes.
- Universal handler: `tempfile::Builder::new().prefix("goose_mcp_response_").suffix(".txt").tempfile()?`
  then `file.keep()` (`large_response_handler.rs:67-77`). The file lands in the system temp
  dir with owner-only permissions (test `:251-264`). Goose never deletes it.
- ACP terminal: nowhere.
- peekaboo: nowhere.

### 4. Recovery path
- The ordinary shell tool (head/tail/sed), which the notice names. No dedicated tool.
- Resume survival:
  - Shell spill files **do not survive**. TempDir is removed when `ShellTool` drops
    (process exit or extension teardown). Within a session the path can **silently point
    at a different command's output** after 8 more shell calls, because of slot reuse.
  - `goose_mcp_response_*.txt` files persist (`keep()`) until OS temp cleanup, so they
    survive resume but leak.
- After ageing (section 5) there is no in-session recovery tool. `chatrecall` searches
  other sessions only (`exclude_session_id = current`, `platform_extensions/chatrecall.rs:197`).

### 5. Ageing
- **Tool-pair summarization** (`goose/src/context_mgmt/mod.rs`):
  - Off by default: `GOOSE_TOOL_PAIR_SUMMARIZATION` defaults to false (`:27-31`).
  - Cutoff: `compute_tool_call_cutoff = clamp(3 * (context_limit * threshold) / 20_000, 10, 500)`
    tool calls (`:366-374`).
  - When the eligible calls exceed `cutoff + TOOLCALL_SUMMARIZATION_BATCH_SIZE (10)`
    (`:25, :376-407`), the oldest 10 are summarized. The current turn's calls are
    protected (`protect_last_n`).
  - Each request+response pair is sent to the model with a "summarize a tool call &
    response pair to save tokens" prompt, giving a one-line result such as
    `"A call to github was made to get the project status"` (`:444-490`).
  - Application (`agents/agent.rs:3483-3505`, invisibility at `:3498`): both original messages get
    `with_agent_invisible()` and stay in the session DB, still user-visible. The summary
    is added as a user-role, agent-only message.
  - The model is not told an original exists and cannot recover it.
- **Auto-compaction**:
  - Threshold `DEFAULT_COMPACTION_THRESHOLD = 0.8` (`goose-context-management/src/lib.rs:32`),
    overridable with `GOOSE_AUTO_COMPACT_THRESHOLD`. A value ≤0 or ≥1 disables it
    (`context_mgmt/mod.rs:224-273`).
  - The entire agent-visible history is summarized. Originals are marked agent-invisible
    (kept in the DB for the UI). Then come an agent-only summary and a continuation
    message: "Your context was compacted. The previous message contains a summary of the
    conversation so far. Do not mention that you read a summary..." (`:33-46, :149-196`).
    The last user text message is re-appended.
- **Recovery compaction** on `ContextLengthExceeded`: up to 2 attempts. Notification text:
  "Context limit reached. Compacting to continue conversation..." (`agents/agent.rs:3163-3200`).
- The summarizer input renders the **full text** of every tool response
  (`goose-context-management/src/format.rs:27-45`). If the summarizer itself overflows,
  tool responses are dropped from the middle outward at `REMOVAL_PERCENTAGES = [0, 10, 20, 50, 100]`
  (`summarize.rs:14, 36-75, 124-182`).

### 6. UI
- CLI `print_tool_output` (`goose-cli/src/session/output.rs:727-760`):
  - On a TTY, shows at most **20 lines**: the first 10 and the last 10, with
    `"... ({n} lines hidden, /toggle to show all)"` between.
  - `GOOSE_SHOW_FULL_OUTPUT` (default false, `output.rs:100-102`) or `/toggle` shows all.
  - Non-TTY output prints everything.
  - Blocks whose `audience` excludes User, or whose priority is below
    `GOOSE_CLI_MIN_PRIORITY` (default 0.0), are skipped (`output.rs:604-635`).
- Live shell streaming:
  - CLI status shows the last 3 lines (`SHELL_STATUS_MAX_LINES = 3`, `session/mod.rs:68`).
  - Notifications are batched every 16 KiB / 150 ms and **capped at 256 KiB** per command,
    after which a `truncated: true` notification is sent
    (`developer/shell_output_streaming.rs:8-10, 65-77`).

### 7. Traps
- **Model-visible shape differs by path**: the shell gives a 10 KB tail plus a path. The
  universal handler gives a path with **no preview at all**. ACP terminal mode gives
  client-truncated text with no marker or path. peekaboo gives a 12k-char head with no
  path.
- **8-slot ring overwrite**: an old notice's path silently refers to newer output.
- **Spill files lost on restart** (TempDir), while universal-handler files leak forever.
- **Unbounded capture before truncation**: all lines are collected through an
  `unbounded_channel` into a `Vec<(bool, String)>` (`shell.rs:587, 682-688`), and there is
  no memory cap. Lines split only on `\n`.
- **Not byte-exact**: invalid UTF-8 is replaced per line (`from_utf8_lossy`, `shell.rs:821`)
  in both the preview and the spill file.
- **stdout/stderr**: the model content block is the interleaved stream. stdout and stderr
  are truncated and spilled separately, so one big command can emit 2-3 notices with
  different files. The separated stdout/stderr live only in `structured_content`.
- **The universal handler checks only text `ContentBlock`s**. `Resource` (embedded text),
  `Err(ErrorData)` results (`:63`) and `structured_content` pass through unbounded. Each
  text block is checked separately, so many 199k-char blocks are not capped together.
- peekaboo compares bytes (`stdout.len() > 12000`), takes 12000 *chars*, and reports
  `stdout.len()` (bytes) as "total chars" (`computercontroller/mod.rs:540-546`).
- The ACP terminal `truncated` flag is ignored.
- UTF-8 boundary handling is correct in `truncate_preview_bytes` (snaps to char boundary,
  with a test at `shell.rs:1246-1251`).

---

## CRUSH

### 1. Truncation shape per tool type

**bash** — `crush/internal/agent/tools/bash.go`
- `MaxOutputLength = 30000` (`bash.go:55`). Not configurable. The unit is **terminal
  display cells** (`ansi.StringWidth`), not bytes or chars (`bash.go:432`).
- Shape: **head + tail with middle elision**. `halfLength = 15000` cells from each end,
  using `ansi.Truncate` / `ansi.TruncateLeft` (`bash.go:428-448`).
- stdout and stderr are truncated **separately** (`formatOutput`, `bash.go:391-425`), and
  stderr is appended after stdout. Interleaving order is lost. The total can reach about
  60k cells plus two spill files.
- The tool description template says: "Truncate if exceeds {{ .MaxOutputLength }}
  characters. The truncation marker names a file holding the complete output - read it
  with the view tool when the omitted middle matters" (`bash.md.tpl:14`).
- Commands auto-background after `DefaultAutoBackgroundAfter = 60` s (`bash.go:54`).

**job_output** (`job_output.go:33-90`): returns the **entire accumulated** stdout+stderr
on every call, passed through the same `TruncateOutput` (`:74`). Each call can produce a
new spill file.

**Bang-mode shell (user `!cmd`, persisted as a user message)** — `crush/internal/shell/truncate.go`
- `maxPersistedLines = 2000`, `maxPersistedBytes = 50 << 10` (51,200) (`truncate.go:22-25`).
- Shape: **head + tail**. The budget is spent from both ends, 1000 lines / 25,600 bytes
  each (`headAndTail`, `:99-132`). A single over-wide line is rune-safely clipped
  (`clipHead`/`clipTail`, `:225-248`).
- If the output is within budget once ANSI is stripped, it is kept whole (`:62-67`).
  Output that is cut has its ANSI stripped.
- Called from `shell.PersistOutput` (`persist_message.go:17-48`), by
  `workspace/app_workspace.go:131` and `backend/agent.go:294`.

**view** — `crush/internal/agent/tools/view.go`
- `DefaultReadLimit = 200` lines, `MaxLineLength = 2000` (bytes), `MaxViewSize = 200 * 1024`
  (`view.go:75-79`).
- Head-only window: `offset` (0-based) + `limit` (`view.go:51-55`).
- Lines over 2000 bytes are cut to `strings.ToValidUTF8(line[:2000], "") + "..."`
  (`view.go:337-341`).
- If the **selected section** exceeds 200 KB, the tool returns an **error**, not partial
  content: `"Content section is too large (%d bytes). Maximum size is %d bytes"`
  (`view.go:241-247, 343-346`).
- Skill files get no limit (`params.Limit = 1000000`, `maxContentSize = 0`, `view.go:197-201, 236-239`).
- Images over 200 KB are rejected (`view.go:207-210`).

**grep** — `crush/internal/agent/tools/grep.go`
- Results are hard-coded to 100 matches (`grep.go:143`), sorted by file mtime (newest
  first), then cut (`:194-216`).
- Match line text is cut at `maxGrepContentWidth = 500` cells with `"..."` (`:91, :165-167`).
- The fallback Go walker stops at 200 matches (`:349`).
- The ripgrep command is `rg --json -H -n -0 pattern [--glob include] path`, with **no
  `-m`/max-count** (`rg.go:49-62`). The whole rg output is buffered by `cmd.Output()`
  (`grep.go:232`).
- Timeout comes from config (`config.GetTimeout()`, `:140`).

**glob**: 100 results (`glob.go:70`). rg candidate pool `max(limit*20, 1000)`, preferring
shortest paths (`glob.go:131-134, 185-187`).

**ls**: `maxLSFiles = 1000` (`ls.go:53`). Configurable through `ToolLs.Limits()`
(`ls.go:143-144`, `config/config.go:773`).

**fetch** — `crush/internal/agent/tools/fetch.go`
- `MaxFetchSize = 100 * 1024` (`fetch.go:22`). Raw body read with
  `io.LimitReader(resp.Body, MaxFetchSize)` (`:130`), silently.
- After text/markdown/html conversion, a **head-only** byte cut:
  `content[:MaxFetchSize]` + marker (`:181-185`).

**web_fetch / agentic_fetch** (sub-agent)
- Raw body cap of 5 MB (`fetch_helpers.go:46-47`).
- If the converted content is over `LargeContentThreshold = 50000` bytes
  (`fetch_types.go:13`), the **whole content is written to a file** and only a pointer is
  returned (`web_fetch.go:51-73`).
- `web_fetch` is only registered inside the agentic_fetch sub-agent
  (`agent/agentic_fetch_tool.go:165`), whose working dir is a temp dir under
  DataDirectory, removed with `defer os.RemoveAll(tmpDir)` (`:102-106`).

**MCP tools** — `crush/internal/agent/tools/mcp/tools.go:41-109`, `tools/mcp-tools.go:110-157`
- **No truncation at all.** Text parts are joined with `"\n"`. Only the **first** image
  and the first audio block are kept. Other content types are rendered with
  `fmt.Sprintf("%v", v)`.
- `read_mcp_resource.go` is likewise unbounded.

**sourcegraph**: count default 10, max 20 (`sourcegraph.go:70-74`), marker
`"(Result limit reached, try a more specific query)"` (`:235`).

**lsp_diagnostics**: shows 10, then `"\n... and %d more diagnostics"` (`diagnostics.go:187`).

**crush_logs**: `maxLogLines = 100` (`crush_logs.go:48`).

### 2. What the model sees (verbatim)
- bash / job_output (`bash.go:441-447`):
  ```
  {head}\n\n... [{N} lines truncated, full output: {path}] ...\n\n{tail}
  ```
  If the spill fails: `... [{N} lines truncated] ...`.
  `{N}` = newline count of the whole output minus the newlines kept in head and tail.
  No byte or cell totals are given.
- Bash output then gets `"\n\n<cwd>%s</cwd>"` (`bash.go:287, 371`). Exit/abort suffixes:
  `"Exit code %d"`, `"Command was aborted before completion"` (`bash.go:404-415`). Empty
  output gives `"no output"`.
- job_output prefix: `"Status: %s\n\n%s"` (`job_output.go:87`).
- Bang-mode persisted (`truncate.go:73-82`):
  ```
  {head}\n\n{N} lines omitted, full output: {path}\n\n{tail}
  ```
  (`"output truncated"` replaces `{N} lines omitted` when no lines were omitted.)
- view: `"\n\n(File has more lines. Use 'offset' parameter to read beyond line %d)"`
  (`view.go:259-262`). Content is wrapped in `<file>…</file>` with `%6d|` line numbers.
  The per-line cut shows only a trailing `...`, with no count.
- grep: `"\n(Results are truncated. Consider using a more specific path or pattern.)"`
  (`grep.go:179`). The header `"Found %d matches"` shows the *kept* count, not the total.
- glob: `"\n\n(Results are truncated. Consider using a more specific path or pattern.)"`
  (`glob.go:82`).
- ls (`ls.go:163`): `"There are more than %d files in the directory. Use a more specific
  path or use the Glob tool to find specific files. The first %[1]d files and directories
  are included below.\n"`.
- fetch: `"\n\n[Content truncated to %d bytes]"` (`fetch.go:184`).
- web_fetch (`web_fetch.go:69-71`):
  `"Fetched content from %s (large page)\n\nContent saved to: %s\n\nUse the view and grep tools to analyze this file."`
- MCP: raw text, no marker.

### 3. Where the full output goes
- `shell.SpillOutput` (`shell/truncate.go:134-169`) writes to
  `{DataDirectory}/shell-output/output-*.log` (`os.CreateTemp`, dir mode 0700). This
  serves both the bash/job_output and bang-mode paths.
  - DataDirectory defaults to `.crush` in the project (`config/config.go:25`,
    `config/load.go:586-591`), "so the agent's view tool can read them without a
    outside-the-working-directory permission prompt" (`truncate.go:137-140`).
  - If dir is empty, it falls back to `os.TempDir()` with no pruning (`:146-151`).
  - The bash path spills `ansi.Strip(content)` (`bash.go:442`), so ANSI is stripped in
    the file.
- The session DB stores only the truncated text. `BashResponseMetadata.Output` is also the
  truncated text (`bash.go:276-283`).
- web_fetch: `page-*.md` in the sub-agent temp dir (deleted when the sub-agent returns).
- fetch, grep, glob, ls, view, MCP: nowhere.

### 4. Recovery path
- Ordinary `view` on the spill path, as the bash description directs (`bash.md.tpl:14`),
  or shell commands.
- Retention: `spillRetention = 7 * 24 * time.Hour`, with a directory cap of
  `spillDirLimit = 256 << 20` (oldest deleted first). The prune runs once per directory
  per process (`truncate.go:35-43, 171-217`). Spill files **survive session resume**
  (the stated design goal, `:35-37`) unless they are pruned by age or size.
- No dedicated retrieval tool. Grep, glob and ls caps can only be worked around by
  narrowing or re-running. Fetch can be re-run, but it re-truncates at the same 100 KB.
  MCP output is never cut, so nothing needs recovering.

### 5. Ageing
- **No per-tool-output pruning or clearing.** Only whole-session summarization exists.
- Trigger (`agent/agent.go:55-58, 1096-1115`):
  - With context window `cw > 200_000`, summarize when `remaining ≤ 20_000` tokens.
  - Otherwise summarize when `remaining ≤ 0.2 * cw`.
  - Skipped if `cw == 0`. `Options.DisableAutoSummarize` turns it off.
- `Summarize` (`agent.go:1411-1559`):
  - Streams a summary of the **full message list** with `templates/summary.md` ("This
    summary will be the ONLY context available ... Length: No limit") and stores it as
    `IsSummaryMessage`.
  - Sets `session.SummaryMessageID`.
  - Later requests load only messages from the summary onward, with the summary's role
    rewritten to User (`agent.go:1792-1814`).
  - Older messages stay in the DB.
  - If the turn was cut mid-tool-calls, the turn is re-queued with the prompt
    `"The previous session was interrupted because it got too long, the initial user request was: `%s`"`
    (`agent.go:1269-1277`).
- No fallback that drops tool output if the summarizer request itself overflows.
- The model cannot recover pre-summary tool output. Spill files that are still on disk
  stay readable if the summary kept their paths.

### 6. UI
- Generic tool output is collapsed to `responseContextHeight = 10` lines. If collapsing
  would hide only one line, everything is shown (`ui/chat/tools.go:27-41`).
- Marker: `"… (%d lines hidden) [click or space to expand]"` (`ui/chat/assistant.go:22`,
  used at `tools.go:705-710`). The same applies to code (`tools.go:715-`) and diffs
  (`unified_diff.go:160-168`).
- Bash UI renders `meta.Output` (already model-truncated) through the same collapse
  (`ui/chat/bash.go:99-108`).
- Bang-mode `ShellItem`:
  - `shellMaxCollapsedLines = 10` (`ui/chat/shell.go:23`).
  - While running it shows the last 10 lines with `"… %d earlier lines"`. When done it
    shows the first 10 with `"… %d more lines"` (`shell.go:248-301`).
  - Long lines are cut to width with horizontal scroll.
- Thinking is collapsed to 10 lines, with an expanded tail window of 200 lines
  (`assistant.go:31-52`).

### 7. Traps
- **The width unit is display cells.** `ansi.StringWidth` counts CJK as 2 cells and
  escape sequences as 0. ANSI sequences are kept in the model-visible head and tail, and
  stripped only in the spill file. Unverified: whether newlines and other control
  characters count as zero width, which would let newline-heavy output evade the 30k cap.
  The `charmbracelet/x/ansi v0.11.8` source is not in the snapshot.
- **The view tool cannot fully read back a wide spill.** Lines are cut at 2000 bytes with
  no column offset, so a minified or one-line spill is unrecoverable through `view`.
  `sed`/`cut` through bash still works. A 200-line window of long lines can also hit the
  200 KB section **error**.
- **fetch UTF-8**:
  - `LimitReader(100KB)` can split a multibyte rune, and the following
    `utf8.ValidString` check then **fails the whole fetch** with "Response content is not
    valid UTF-8" (`fetch.go:130-139`). Same risk at the 5 MB cap in
    `fetch_helpers.go:46-55`.
  - The post-conversion `content[:MaxFetchSize]` byte-slices without a rune check
    (`fetch.go:183`).
  - Raw-body truncation is silent when the converted text ends up under 100 KB.
  - In markdown mode the closing code fence is cut off.
- **grep buffers all rg JSON output in memory** (no `-m`) before keeping 100 matches. The
  truncation marker gives no total.
- **job_output and background buffers are unbounded** `bytes.Buffer`s
  (`shell/background.go:22-40, 213-221`). Every poll re-truncates the full buffer and
  writes another spill file.
- **MCP results are unbounded.** `result.IsError` is not checked in `RunTool`. Extra
  images and audio are silently dropped.
- stdout and stderr are truncated independently and concatenated, so their relative order
  is lost.
- **Spills are not searchable from the root.** They live in the hidden `.crush/` dir,
  which gets a `*` `.gitignore` (`cmd/root.go:295-299`), so the grep and glob tools
  (ripgrep defaults) skip them unless the spill path is passed explicitly.
- Rune safety is handled correctly in bang-mode `clipHead`/`clipTail`, in the bash path
  via `ansi.Truncate`, and in view via `ToValidUTF8`.
- The full copy is saved before replacement in every truncating path. A failed spill
  degrades to a marker without a path (bash) or a note without a path (bang-mode); the
  output is not lost from memory, only the full copy.

---

# Large tool output handling: kimi-cli vs hermes-agent (read-only source study)

Roots:
- K = /Users/patrickbeam/projects/blackbird/quarantine/kimi-cli/
- H = /Users/patrickbeam/projects/blackbird/quarantine/hermes-agent-2026-10-07/

Nothing was run. All claims are from source reading. One trap (K shell readline limit) depends on
CPython asyncio StreamReader semantics, which are not in the snapshot; it is marked as inferred.

---------------------------------------------------------------------------------------------------

## A. kimi-cli

### A0. Shared mechanism: ToolResultBuilder (K src/kimi_cli/tools/utils.py)

- `DEFAULT_MAX_CHARS = 50_000`, `DEFAULT_MAX_LINE_LENGTH = 2000` (utils.py:50-51). Units are Python
  `str` characters (code points), not bytes or tokens. Constructor args only, not user-configurable.
- Shape: **head-only**. `write()` splits into lines; each line is cut to
  `min(remaining_chars, max_line_length)` by `truncate_line` (utils.py:33-46, 107-118), which keeps
  the beginning plus the marker plus the original line break. Once `_n_chars >= max_chars`, all further
  writes return 0 (utils.py:98-99, 108-109).
- Per-line marker: `"[...truncated]"` (utils.py:66).
- Message marker (in the `message` field): `"Output is truncated to fit in the message."`
  (utils.py:167, 187). It carries no total size, no omitted count and no path.
- How the model sees it: `tool_result_to_message` (K src/kimi_cli/soul/message.py:35-62) emits
  `<system>{message}</system>` (or `<system>ERROR: {message}</system>`), then the output text.
  So a truncated successful shell result looks like:
  `<system>Command executed successfully. Output is truncated to fit in the message.</system>` followed by the head.
- **Builder trap:** `_truncation_happened` is set only when a line is actually cut
  (utils.py:119-120). If a line ends exactly at `max_chars`, every later line is dropped silently by the
  `is_full` early return (utils.py:98-99, 108-109), and the "truncated" message never appears. Also,
  `truncate_line` lets output go past `max_length` when the limit is smaller than the marker
  (utils.py:45).

### A1. Truncation shape per tool

| Tool | Shape | Constants | Where |
|---|---|---|---|
| Shell (foreground) | head-only | 50,000 chars total, 2,000 chars/line (builder defaults) | tools/shell/__init__.py:82, 106-112 |
| Shell background task, TaskOutput | **tail-only** byte window | `TASK_OUTPUT_PREVIEW_BYTES = 32 << 10` (32 KiB) | tools/background/__init__.py:14, 191-211 |
| Background completion notification | tail | `notification_tail_lines = 20`, `notification_tail_chars = 3_000` (configurable, `[background]`); the "chars" value is used as **bytes** | config.py:103-104; notifications/llm.py:34-38; background/store.py:208-224 |
| ReadFile | head from `line_offset` (or tail with negative offset); line cap | `MAX_LINES = 1000`, `MAX_LINE_LENGTH = 2000` chars, `MAX_BYTES = 100 << 10` (100 KiB UTF-8) | tools/file/read.py:16-18, 172-226, 228-300 |
| Grep (ripgrep) | rg stdout byte cap, then head + offset pagination, then builder | `RG_MAX_BUFFER = 20_000_000` bytes; `head_limit` default 250 lines/entries (0 = unlimited); `offset`; `--max-columns 500` for non-content modes only; builder 50k chars / 2k per line | tools/file/grep_local.py:101-120, 142-143, 277-278, 325-360, 441-445, 552-566 |
| Glob | head | `MAX_MATCHES = 1000` paths; **not** through the builder, so it has no char cap | tools/file/glob.py:20, 159-168 |
| FetchURL | head-only | builder with `max_line_length=None` gives 50,000 chars; `response.text()` loads the whole body with no byte cap | tools/web/fetch.py:44, 73-79, 121-122, 130-171 |
| SearchWeb | head-only | builder 50,000 chars, `limit` 5 (max 20) results; later results are lost if earlier content fills the cap | tools/web/search.py:20-38, 58, 140-146 |
| MCP tools | head-only, shared char budget across parts | `MCP_MAX_OUTPUT_CHARS = 100_000` (text and `data:` URL lengths) | soul/toolset.py:1019-1104 |

Grep details: content mode has no `--max-columns`, so long matching lines are cut by the builder at
2,000 chars. When the 20 MB buffer overflows, the last partial line is dropped (grep_local.py:441-445).

### A2. What the model sees (verbatim)

- Shell: `Output is truncated to fit in the message.` in the `<system>` prefix, plus `[...truncated]`
  on cut lines. On failure the message is `Command failed with exit code: {exitcode}.` (shell:124-127).
  The model gets no size, omitted count or handle.
- bash.md tells the model: "The stdout and stderr will be combined and returned as a string. The
  output may be truncated if it is too long." (tools/shell/bash.md:4)
- ReadFile message (read.py:209-222):
  `"{n} lines read from file starting from line {start}. Total lines in file: {total}."`, plus one of
  `" Max 1000 lines reached."`, `" Max 102400 bytes reached."` or `" End of file reached."`, plus
  `" Lines [..] were truncated."`. Per-line marker `"..."` (default `truncate_line` marker). read.md
  says "Any lines longer than ${MAX_LINE_LENGTH} characters will be truncated, ending with "..."".
- Grep: `"Results truncated to {effective_limit} lines (total: {total}). Use offset={offset+limit} to see more."`
  (grep_local.py:561-564); `"Output exceeded buffer limit. Some results omitted."` (445);
  `"Grep timed out after 20s. Partial results returned."` (457).
- Glob: `" Only the first 1000 matches are returned. You may want to use a more specific pattern."` (glob.py:161-163).
- MCP: appended part
  `"\n\n[Output truncated: exceeded 100000 character limit. Use pagination or more specific queries to get remaining content.]"`
  (toolset.py:1088-1095).
- TaskOutput (background/__init__.py:37-108): structured lines
  `output_path: <abs>`, `output_size_bytes: N`, `output_preview_bytes: N`, `output_truncated: true|false`,
  `full_output_available: ...`, `full_output_tool: ReadFile`,
  `full_output_hint: Use ReadFile(path="<abs>", line_offset=1, n_lines=300) to inspect the full log. Increase line_offset to continue paging through the file.`
  and before the output: `[Truncated. Full output: <abs>]`.

### A3. Where full output goes

- Foreground Shell, Grep, Glob, FetchURL, SearchWeb, MCP: **nowhere**. The omitted part is discarded
  in memory. Context records (`context.jsonl`) store only the truncated tool message.
- Background shell tasks: the worker opens `output.log` in append-binary mode and points both
  stdout and stderr at it (background/worker.py:122-131). The file is stored under
  `session.context_file.parent / "tasks" / <task_id> / output.log` (background/manager.py:49;
  background/store.py:35, 71). It has no size cap.
- No temp-file or "persisted output" mechanism was found for foreground tools. A grep for tempfile
  found only the updater, the rg download, OAuth and plugins.

### A4. Getting omitted output back

- Foreground shell: the only option is to **re-run** the command with narrower output (pipes, head,
  tail, redirect to a file). No handle exists.
- ReadFile: page with `line_offset` / `n_lines`, or use a negative offset for the tail. A line cut at
  2,000 chars is **not recoverable through ReadFile** because there is no column offset; the model has
  to use Shell (cut, sed and similar).
- Grep: re-run with `offset=` (re-executes rg). Glob: re-run with a narrower pattern.
- FetchURL, SearchWeb: no continuation. The model must re-fetch, which returns the same head, so in
  practice the rest is unrecoverable.
- MCP: depends on the server offering pagination.
- Background tasks: ordinary ReadFile on `output.log` (hinted explicitly). The log lives in the session
  directory, so it **survives session resume**. Foreground truncated results survive resume only in
  their truncated form.

### A5. Ageing

- There is no per-tool-result pruning, clearing or placeholdering (searched for prune, microcompact
  and old tool; no hits in the context path).
- The only mechanism is whole-context **SimpleCompaction** (soul/compaction.py:110-198; kimisoul.py:250):
  - Trigger: `token_count >= max_context_size * compaction_trigger_ratio` **or**
    `token_count + reserved_context_size >= max_context_size` (compaction.py:60-76). Defaults are
    `compaction_trigger_ratio = 0.85` and `reserved_context_size = 50_000` (config.py:88-95). Manual
    `/compact` with an optional custom instruction is also available.
  - It keeps the last `max_preserved_messages = 2` user/assistant messages (and anything after them).
    It serializes **all text parts of every earlier message, tool results included, unbounded** into a
    single compaction request (`## Message {i}\nRole: ...\nContent:\n`), using the prompts/compact.md
    priorities ("MUST KEEP: Error messages, stack traces...").
  - Result: one user message `<system>Previous context has been compacted. Here is the compaction output:</system>`
    followed by the summary (compaction.py:18, 139-145), plus a background-task snapshot if any tasks
    are active (kimisoul.py:1441-1460).
  - Recoverability: `context.clear()` **rotates** `context.jsonl` to a numbered sibling
    (`next_available_rotation`, soul/context.py:202-224; kimisoul.py ~1575). The pre-compaction
    transcript (truncated tool messages) stays on disk, but the model is not told about it and no tool
    targets it. In practice it is not recoverable by the model.

### A6. UI (shell UI)

- The shell UI **never renders raw tool output.** `_ToolCallBlock._compose`
  (ui/shell/visualize/_blocks.py:451-548) renders a headline (`Used <Tool> (<key arg>)`), then only
  display blocks: diffs, Todo, background-task line, and `BriefDisplayBlock` text.
- Successful Shell has an empty brief, so the user sees only the headline. Failed Shell brief is
  `Failed with exit code: N` plus `builder.tail()`: last **5** non-empty lines, each capped at **200**
  chars (utils.py:130-148; shell:120-123).
- Subagent tool calls: only the last `MAX_SUBAGENT_TOOL_CALLS_TO_SHOW = 4` are shown, then
  "N more tool calls ..." (_blocks.py:51, 466-475). Thinking preview: 6 lines (_blocks.py:49).
  Notification body preview: 2 lines plus "..." (_blocks.py:609-612).
- Ctrl-O is "Edit in external editor", not expand-output (ui/shell/slash.py:59). The `/task` browser
  shows a `tail_output` preview (ui/shell/task_browser.py:172-188).

### A7. Traps (kimi)

1. **No full copy, ever, for foreground tools.** Truncation happens while the output streams in
   (`builder.write` per line), so nothing is saved first. The model can only re-run.
2. **stdout and stderr share one builder with no labels.** The two async readers interleave by line
   arrival (shell:106-112, 249-252), and stderr can be pushed past the 50k head by stdout and lost.
   The tail of stderr appears only in the user-facing brief on failure, never in the model-facing output.
3. **Head-only for shell.** Errors at the end of a long build log are the part that gets dropped.
4. **Inferred: lines over 64 KiB with no newline.** `kaos.exec` uses
   `asyncio.create_subprocess_exec` with the default StreamReader limit (packages/kaos/src/kaos/local.py:169-175),
   and Shell reads with `stream.readline()` (shell:228-234). In CPython a line over 64 KiB raises
   ValueError. That falls through to the generic handler as `Command execution failed: ...`
   (shell:133-142), and `_run_shell_command` kills the process only on Cancelled or Timeout
   (shell:256-261).
5. **UTF-8 boundaries.** Shell decodes per line with `errors="replace"`, which is safe at line
   granularity. TaskOutput/`read_output` and `tail_output` seek to an arbitrary **byte** offset
   (`size - 32KiB`, `size - 3000`) and decode with `errors="replace"`
   (background/store.py:191-203, 215-221), so the first character can come out as U+FFFD.
6. **The Grep offset hint can lie.** Pagination is applied to lines before the builder's 50k char
   cap. If 250 long lines exceed 50k chars, the model sees fewer lines than `head_limit`, but the
   hint still says `offset=+250`, which skips lines it never saw (grep_local.py:552-570).
   `head_limit=0` falls back to the builder with only the generic message.
7. **MCP:** text is cut with Python slicing (code-point safe). Media parts over the remaining budget
   are dropped "silently" (toolset.py:1079); despite the docstring there is no per-part placeholder,
   only the one generic trailing marker. Converted parts are TextContent, Image, Audio and embedded
   resources; `structuredContent` is not forwarded at all (convert_mcp_tool_result iterates
   `result.content` only). An error result still goes through the same budget. Budget units are chars
   of base64 `data:` URLs, so a 100k budget is about 73 KB of image.
8. **Glob output is uncapped in chars** (up to 1000 arbitrarily long paths, ToolOk direct).
9. **FetchURL reads the whole body into memory** (`await response.text()`) before the 50k cap.
10. **Compaction input is unbounded.** All earlier tool output text goes verbatim into one compaction
    request, which can itself overflow the window. Images and other non-text parts are dropped from
    the compaction input.
11. **`notification_tail_chars` is applied as bytes** (store.tail_output `max_bytes`).

---------------------------------------------------------------------------------------------------

## B. hermes-agent

Hermes has layered defenses: (1) per-tool caps, (2) per-result spillover to disk with a preview and a
path, (3) a per-turn aggregate budget, (4) ageing via prune, demote or summarize passes in the context
compressor. (tools/tool_result_storage.py:1-6)

### B0. Shared constants

- `tool_output` config section (tools/tool_output_limits.py:13-15, 34-54):
  `max_bytes = 50_000` (named bytes, **measured in chars** via `len(str)`), `max_lines = 2000`
  (read_file page cap), `max_line_length = 2000`. Configurable in config.yaml and cached per profile home.
- One head/tail algorithm (tools/tool_output_truncate.py:12-32): `HEAD_RATIO = 0.4`, so 40% head /
  60% tail, chars. Marker:
  `"\n\n... [{LABEL} TRUNCATED - {omitted:,} {unit} omitted out of {total:,} total] ...\n\n"`.
  The default label is `OUTPUT`, the unit is `chars`, and MCP uses `MCP RESULT`.
- Spillover budget (tools/budget_config.py):
  `DEFAULT_RESULT_SIZE_CHARS = 100_000`, `DEFAULT_TURN_BUDGET_CHARS = 200_000`,
  `DEFAULT_PREVIEW_SIZE_CHARS = 1_500`, `DEFAULT_MCP_RESULT_SIZE_CHARS = 50_000`
  (config `tool_budget.mcp_result_size_chars`), `PINNED_THRESHOLDS = {"read_file": inf}`
  (budget_config.py:10-20). These scale with the context window: per result
  `clamp(window*4*0.15, 8_000, 100_000)`, per turn `clamp(window*4*0.30, 16_000, 200_000)`
  (budget_config.py:81-114; `CHARS_PER_TOKEN = 4` agent/model_metadata.py:2374). Resolution order:
  pinned, then tool_overrides, then the `mcp_` prefix, then the registry `max_result_size_chars`
  (100_000 for terminal, file tools, web, execute_code), then the default (budget_config.py:56-75).

### B1. Truncation shape per tool

| Tool | Shape | Constants | Where |
|---|---|---|---|
| terminal (foreground) | **head+tail, middle elided** (40/60), streaming collector | `tool_output.max_bytes` 50,000 chars; spill file cap `_SPILL_CAP_CHARS = 5_000_000` | tools/environments/base_output.py:28-165, 169-200; tools/terminal_tool_result.py:151-154, 230 |
| terminal background (`process`) | **tail-only rolling buffer** | `MAX_OUTPUT_CHARS = 200_000`; completion/poll output tail `COMPLETION_OUTPUT_CHARS = 2000`, `HEARTBEAT_OUTPUT_CHARS = 2000`; `read_log` limit 200 lines | tools/process_registry.py:55-60, 85, 519-534, 2050-2080 |
| execute_code | stdout head+tail **bytes** (40/60); stderr **head-only** bytes | `MAX_STDOUT_BYTES = 50_000`, `MAX_STDERR_BYTES = 10_000`, `MAX_SPILLED_STDOUT_BYTES = 5_000_000` | tools/code_execution_tool.py:50-101; tools/code_kernel.py:294-302, 556-559 |
| read_file | page (offset/limit) + char budget, cut on a line boundary; per-line cap | `DEFAULT_READ_LIMIT = 2000` lines (clamped by `max_lines`); `_DEFAULT_MAX_READ_CHARS = 100_000` (config `file_read_max_chars`); 2,000 chars/line; raw line clamp `4*max_line_length+1` bytes | file_operations_common.py:281, 295-301; file_tools.py:51-115, 693-703; file_operations.py:486-500, 837-841, 924-983 |
| search_files | head with offset/limit pagination; per-match content cap | `DEFAULT_SEARCH_LIMIT = 50`, offset 0; match `content[:500]`; rg fetch `offset+limit+1` then `head -n` | file_operations_common.py:282-283; file_operations_search.py:199-245, 335-408, 837-907 |
| web_extract | **head+tail (75/25), snapped to line boundaries** | `DEFAULT_EXTRACT_CHAR_LIMIT = 15000` (config `web.extract_char_limit`, clamped [2,000, 500,000]); stored copy cap `MAX_STORED_TEXT_CHARS = 2_000_000`; base64 images become `[IMAGE: alt]` | tools/web_tools_truncate.py:19-24, 47-56, 86-128 |
| browser snapshot | head on line boundaries + stored copy | `browser.snapshot_threshold` | tools/browser_tool_snapshot.py:34-107 |
| MCP | hard flood cap head+tail 40/60, then spillover at 50k | `_MCP_HARD_RESULT_CAP_CHARS = 2_000_000`; resource blocks `50 MiB` decoded; ≤4 native images | tools/mcp_tool_content.py:17-34, 28-29, 129; tools/mcp_tool_handlers.py:440-500, 523-562 |
| any tool result (layer 2) | over threshold: replaced by a head preview (cut at the last newline if past half) plus a path | threshold 100k (50k for `mcp_`), preview 1,500 chars | tools/tool_result_storage.py:174-179, 264-334; agent/tool_executor.py:1107-1120 |
| whole turn (layer 3) | spill the largest results first until the turn total ≤ 200k | `turn_budget` | tool_result_storage.py:359-391; tool_executor.py:1193-1200 |

### B2. What the model sees (verbatim)

- terminal JSON (terminal_tool_result.py:258-277):
  `{"output": "...<head>\n\n... [OUTPUT TRUNCATED - 123,456 chars omitted out of 173,456 total] ...\n\n<tail>", "exit_code": N, "error": null, ...}`
  plus, on overflow, `"output_total_chars": N, "full_output_path": "<path>", "truncation_note": "Output exceeded the capture window (head+tail shown). Full output (N chars) saved to <path> — search it with search_files or page it with read_file instead of re-running the command."`
  (terminal_tool_result.py:190-193). Optional `exit_code_meaning`, `hint` and others.
- execute_code: same notice with `unit="bytes"`, metadata `stdout_truncated`,
  `stdout_bytes_captured/total/omitted`, `stdout_spill_path`, and the warning
  `execute_code stdout was truncated (head/tail shown); the script did run. FULL output saved to <path> — page it with read_file(path="<path>", offset=...) instead of re-running.`
  (code_execution_tool.py:56-81). Stderr is appended as `"\n--- stderr ---\n" + stderr` (575).
- Spillover replacement block (tool_result_storage.py:264-279):
  ```
  <persisted-output>
  This tool result was too large (N characters, X.Y KB).
  Full output saved to: <path>
  Use the read_file tool with offset and limit to access specific sections of this output.
  Recovery: page through the saved file with read_file (offset/limit) or process it with execute_code — do NOT re-request the same data from the remote API; the full result is already on disk.

  Preview (first N chars):
  <preview>
  ...
  </persisted-output>
  ```
  Fallback if no write succeeded: `[Truncated: tool response was N chars. Full output could not be saved to sandbox.]` (333-334).
- read_file: JSON with `truncated`, `truncated_by: "bytes"`, `next_offset`, `hint`:
  `"Output truncated at the 100,000-char read budget after K line(s) (showing lines A-B of T). Use offset=N to continue."`
  plus, if the first line alone overflowed, `" Note: the first line alone exceeded the budget and was clamped mid-line; its remainder is not retrievable via offset."`
  (file_tools.py:96-114). Long lines show `"... [truncated]"` (file_operations.py:500). Line format is `N|content`.
- search_files: `_hint`: `"Results truncated. Use offset={offset+limit} to see more, or narrow with a more specific pattern or file_glob."` (file_tools.py:1131-1135), plus `total_count`.
- web_extract footer (web_tools_truncate.py:107-128):
  `[... middle omitted — see footer ...]` between head and tail, then
  `──────── [TRUNCATED] ────────` /
  `Showing H chars (head) + T chars (tail) of N total clean characters.` /
  `Full text saved to: <path>` /
  `To read the omitted middle: read_file path="<path>" offset=<line> limit=200  (the file is the complete page; raise/lower offset to page through it).`
- MCP flood: `... [MCP RESULT TRUNCATED - N chars omitted out of T total] ...`. The result is a
  JSON envelope `{"result": text, "structuredContent"?: ..., "_meta"?: ...}`.
- process completion/poll: `{"output": <last 2000 chars>, "output_cut": N}` (process_registry.py:526-534).

### B3. Where full output goes

| Source | Location | Cap | Retention |
|---|---|---|---|
| terminal foreground overflow | `$HERMES_HOME/cache/terminal-output/out-<ts>-<pid>-<id>.log` (redacted and ANSI-stripped after the fact) | 5,000,000 chars, then `... [spill capped at 5,000,000 chars] ...` | files older than 7 days pruned when a new collector is created (base_output.py:26, 179-189) |
| layer-2/3 spillover (any tool) | `$HERMES_HOME/cache/spillover/<tool_call_id>.txt`. Remote backends get a translated mounted path (probed with `test -r`) or a copy in the sandbox temp `.../hermes-results/` | size-verified write; fails closed | **24 h** (`SPILLOVER_MAX_AGE_HOURS = 24`), pruned once per process on first spill and hourly by the gateway (tool_result_storage.py:23-78; gateway/run.py:4582-4591) |
| execute_code stdout | `cache/exec` (digest-keyed) | 5,000,000 bytes | no age prune found in snapshot |
| web_extract | `cache/web/<host>-<sha10>.md` (overwritten per URL; mounted read-only into remote backends) | 2,000,000 chars | extract index keeps 500 entries (web_result_cache.py:28-29); no file age prune found |
| background process | in-memory rolling 200k-char tail; remote backends also have `{temp}/hermes_bg_<id>.log` in the sandbox | 200k chars | finished processes kept 30 min, results 7 d / 64 entries (process_registry.py:59; process_registry_results.py:19-20) |
| MCP flood beyond 2M chars | nowhere (lossy) | | |
| session history | the SQLite session DB holds the message as sent (post-spill preview or ≤50k terminal). Compaction **archives** original rows (`active=0, compacted=1`) via `archive_and_compact` | | durable |

### B4. Getting omitted output back

- The intended path is the **ordinary `read_file` on the saved path** (offset/limit), or
  `search_files` / `execute_code` on it. Every marker names the path and the exact call.
  `read_file` is pinned to threshold infinity so reading a spill never re-spills
  (budget_config.py:9-10). The MCP envelope is unwrapped to pageable text before saving
  (`_pageable_text`, tool_result_storage.py:182-220), so `read_file` pagination works on it.
- Guardrails: a byte-identical repeat result gets a stub that points at the spill path
  (`[The referenced result was persisted to: <path> — page through it with read_file ...]`,
  agent/tool_guardrails.py:525-545; recorded at tool_executor.py:77-87).
- read_file dedup: re-reading an unchanged `(path, offset, limit)` returns
  `"File unchanged since last read. The content from the earlier read_file result in this conversation is still current — refer to that instead of re-reading."`
  (file_tools_write_guards.py:548-551; file_tools.py:670-684). The generation resets at each
  compaction and proactive prune, so the first re-read afterwards serves full content
  (file_tools_read_tracking.py:161-180; conversation_compression.py:3403-3415; turn_preflight.py:376-392).
  4 identical consecutive reads hard-block (file_tools.py:742-749).
- Session resume: the session DB keeps the preview and the path, but the **spill file is deleted after
  24 h** (terminal-output after 7 d). After that the marker references a missing file and the data is
  gone, so re-running is the only option.
- Background processes: `process` `read_log(offset, limit)` pages the in-memory tail buffer.
  The head of long output is unrecoverable.

### B5. Ageing

Context compressor (agent/context_compressor.py), config defaults in hermes_cli/config_defaults.py:578-630:
- Main trigger: `compression.threshold = 0.50` of the effective input window (floored to 0.75 for
  windows < 512K, context_compressor.py:1195-1196, 2850-2854). `tail_mode: "lean"` (tail 2.5% of
  window, 10K–25K tokens, ≤20% of window), `protect_last_n: 20`.
- Phase 1 on every compression, no LLM: `_prune_old_tool_results` (3372-3419):
  - Pass 1, dedup: older byte-identical tool results ≥ `_PRUNE_MIN_CHARS = 200` become
    `"[Duplicate tool output — same content as a more recent call]"` (3252-3268). Lossless.
  - Pass 2, demote: tool results outside the protected tail and larger than 200 chars are replaced by
    a **one-line summary** from `_summarize_tool_result` (1683-1690, 1973+, 2038-2050). Examples:
    `[terminal] ran \`cmd\` -> exit 0, 412 lines output`,
    `[read_file] read <path> from line 1 (N chars)`,
    `[search_files] content search for 'pat' in . -> 37 matches`,
    `[web_extract] <url> (N chars)`, generic `[tool] k=v (N chars result)`.
    Tool-call arguments are never rewritten.
  - Pass 3: image payloads retired except the newest `_MAX_KEEP_TOOL_IMAGES = 3`.
  - Pass 4, pressure: demotion happens inside the protected tail when it exceeds the soft ceiling,
    keeping `_PRESSURE_KEEP_RECENT_MESSAGES = 3` and the unread pending tool round (3302-3370).
- Lean tail stub for tool results older than `_LEAN_TAIL_KEEP_TOOL_ROUNDS = 6` and ≥ 1,500 chars:
  `"[<tool> output demoted at compaction — N chars preserved in session history. Recover with session_search(query=..., session_id='<sid>')]"`
  (957-967, 3748-3770).
- Salvage placeholder: `_PRUNED_TOOL_PLACEHOLDER = "[Old tool output cleared to save context space]"`
  for all but the last `_SALVAGE_KEEP_RECENT_TOOLS = 2` tool results (546, 597-606, 856).
- Summarizer input: per message head 4,000 + tail 1,500 chars with marker
  `⟪HERMES-CONTEXT-COMPRESSION: {omitted} of {total} chars omitted here by Hermes's context compressor.⟫`,
  tool args 1,200, aggregate `_SUMMARY_INPUT_MAX_CHARS = 160_000` (3570-3576, 3606-3607;
  agent/compression_marker.py:19-24, 52-86).
- Optional **proactive prune** (`proactive_prune_tokens`, default **0 = off**;
  `proactive_prune_min_result_chars = 8000`; `proactive_prune_min_reclaim_tokens = 4096`). It runs the
  same passes deterministically and commits via `archive_and_compact` (3482-3561). Optional
  `micro_compact` (default off) folds exchanges into a rolling summary every N turns
  (agent/micro_compaction.py).
- Recoverability: originals are archived in the session DB, and `session_search` can surface
  compaction-archived rows. Its message content is **head-capped at 1,200 / 2,000 / 4,000 chars**
  with `"…"` and `content_truncated`, `original_content_chars` (tools/session_search_tool.py:34-40,
  237-251, 299-347, 456). So a demoted 50k terminal result is recoverable through session_search only
  as about its first 4k chars. If the original was a spill preview, the path is in the archived row
  (inside the first 4k), so the file is recoverable only while it is younger than 24 h.

### B6. UI

- Classic CLI (agent/display.py, agent/tool_executor.py): tool calls show as one-line previews or
  spinner "cute rows". No result body is shown by default. Non-quiet mode prints
  `✅ Tool N completed in Xs - <first log_prefix_chars of result>...`, where `log_prefix_chars` is 100
  in the AIAgent ctor and 20 in the batch entry point (tool_executor.py:1213-1219; run_agent.py:246, 1482).
  Full result appears only under verbose logging. Inline edit diffs are capped at
  `_MAX_INLINE_DIFF_FILES = 6`, `_MAX_INLINE_DIFF_LINES = 80` (display.py:26-27). Tool preview length
  is `display.tool_preview_length` (0 = unlimited).
- Ink TUI (ui-tui/src): the completed trail line carries a note of 72 chars max (lib/text.ts:198-203).
  Verbose Args/Result blocks are bounded to the **last 12 lines / 800 chars** (tail, "showing live
  tail") via `VERBOSE_TRAIL_MAX_LINES = 12`, `VERBOSE_TRAIL_MAX_CHARS = 800`
  (config/limits.ts:16-17; lib/text.ts:107-140, 205-218). Live streaming tail: 240 lines / 16,000
  chars (limits.ts:3-4). The default details mode is `collapsed`, but the `tools` section resolves to
  expanded (domain/details.ts:45-46; __tests__/details.test.ts:80).

### B7. Traps (hermes)

1. **Spill retention is shorter than session life.** Spillover files are deleted after 24 h and
   terminal spills after 7 d, while session history (with `Full output saved to: <path>`) is durable.
   On a resume the model is pointed at a missing file.
2. **Demotion drops the spill path.** `_summarize_tool_result` has no handling for
   `<persisted-output>` or `full_output_path`, so a demoted terminal or spilled result becomes
   `[terminal] ran ... -> exit 0, N lines output` with no handle. Recovery goes only through
   session_search (head-capped 4k) or re-running.
3. **session_search recovery is head-capped** (1,200 / 2,000 / 4,000 chars) despite the lean stub's
   claim of "N chars preserved in session history".
4. **Terminal truncation happens before ANSI strip and secret redaction**
   (terminal_tool_result.py:230-242). The 50k window counts escape bytes, a cut can split an escape
   sequence or a secret token at the head/tail seam (redaction afterwards may then miss it), and the
   "total" in the marker counts pre-strip chars. The spill file is redacted afterwards; if that fails,
   the file and the handle are dropped (170-193).
5. **Double truncation:** the collector renders head + notice + tail within max_chars, then
   `_truncate_head_tail` runs again after the sudo/transform-hook annotations. If a hook enlarged the
   output, a second marker appears whose counts refer to the already-truncated text.
6. **`max_bytes` is chars** (tool_output_limits, collector, truncate_head_tail all use `len(str)`).
   execute_code is really bytes: it slices UTF-8 bytes and decodes with `errors="replace"`
   (code_execution_tool.py:60-70), so U+FFFD appears at the head/tail seams. Kernel stderr is cut at
   10,000 bytes the same way.
7. **execute_code: stdout and stderr are handled differently.** stdout gets head/tail, a marker and a
   spill. stderr gets **head-only 10 KB, silently**: `append` just stops keeping bytes, with no marker
   and no spill (code_kernel.py:294-298). Terminal merges stderr into stdout (`stderr=subprocess.STDOUT`,
   base_output.py:259; local.py:1071), so stderr is not distinguishable there.
8. **Background `process` output is a rolling tail.** `read_log` is documented as "Read the full
   output log" but reads `session.output_buffer`, which holds the last 200k chars (process_registry.py:55,
   558, 2050-2060). `offset=0` is the start of the buffer, not of the process output, and `read_log`
   does not report how much was dropped (`total_output_chars` exists but is not surfaced there).
9. **MCP above 2M chars is lossy before spill.** The flood cap is head+tail with no saved copy. Below
   2M the spill keeps everything. `structuredContent` is forwarded (deduped only when the JSON is
   duplicated in a text block) and capped the same way.
10. **The spill preview is head-only** (1,500 chars; cut at the last newline if it falls past half),
    even for terminal output where the tail matters. The terminal path rarely reaches spillover,
    because its own 50k head/tail plus cache/terminal-output handles it first and keeps the result
    under the 100k threshold.
11. **Turn-budget spill happens after the session-DB flush** (`_flush_session_db_after_tool_progress`
    at tool_executor.py:1144-1145 precedes `_finalize_tool_batch` at 1193-1200). Whether the DB row
    is rewritten with the spilled form was not verified in this study.
12. **read_file**: a single line longer than 100k chars is clamped mid-line, and the hint says
    explicitly that its remainder is not retrievable via offset. Lines over 2,000 chars are cut with
    `... [truncated]` and have no column paging. The byte clamp (`4*2000+1`) is sized so the 2,000-char
    cut always falls inside it, which avoids UTF-8 splitting.
13. **web_extract** stores the base64-cleaned page, not the raw page, and the `offset` in the footer
    is computed against that cleaned text, so the two are consistent. Same-URL extracts overwrite the
    stored file.
