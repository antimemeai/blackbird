# Tool-output lifecycle in six harnesses: pi, oh-my-pi, gemini-cli, qwen-code, kimi-cli, crush

Method: read-only source reading of the quarantined upstream source ZIPs (nothing built, run or sent to a provider), 2026-10-19. Revisions are from `papers/2026-09-30-known-agent-acquisition.json` and `papers/2026-09-30-discovered-agent-acquisition.json`:
- pi `8ce69e9d2b17` (earendil-works/pi)
- oh-my-pi `8b25ad4a0562` (can1357/oh-my-pi)
- gemini-cli `c6bccb7ecbf6`
- qwen-code `310f4ba3ab95` (CHANGELOG through 0.24.7, 2026-09-29)
- kimi-cli `9ab1286b8fe4`
- crush `76cc5c574e15` (no CHANGELOG)

**Citation roots.** Sources are local files, cited as `ROOT/path:line`, where ROOT is one of:
- PI = `/Users/patrickbeam/projects/blackbird/quarantine/pi/packages`
- OMP = `/Users/patrickbeam/projects/blackbird/quarantine/oh-my-pi/packages`
- GC = `/Users/patrickbeam/projects/blackbird/quarantine/gemini-cli/packages/core/src`
- GU = `/Users/patrickbeam/projects/blackbird/quarantine/gemini-cli/packages/cli/src`
- QC = `/Users/patrickbeam/projects/blackbird/quarantine/qwen-code/packages/core/src`
- QU = `/Users/patrickbeam/projects/blackbird/quarantine/qwen-code/packages/cli/src`
- K = `/Users/patrickbeam/projects/blackbird/quarantine/kimi-cli`
- CR = `/Users/patrickbeam/projects/blackbird/quarantine/crush/internal`

**Verification.** I re-read the load-bearing constants and placeholder strings myself:
- pi summariser truncation, cut-point rule, orphan repair, branch-summary skip and bash notices
- crush `TruncateOutput`, thresholds and orphan handling
- kimi builder constants, compaction preservation and MCP cap
- qwen microcompaction placeholder and tools, full-history compression, defaults, gate and orphan text
- oh-my-pi pruning notices, cache-warm guard and call sites
- gemini masking constants and placeholder, the compression budget and the 64 KB cap

Other line numbers come from parallel source sweeps of the same trees and were not each re-opened. Items marked UNCONFIRMED or "inference" were not demonstrated by tests or execution.

## Q1. At creation: per-output caps, truncation strategy, placeholder text, where the full output goes

### Takeaway
All six cap tool output when it is created. Four of them (pi, oh-my-pi, gemini-cli, qwen-code) put the full output in a file and give its path to the model. crush does so for bash only. kimi-cli does so only for background tasks; everything else keeps the head and drops the rest. Creation caps run from about 20 KB to 50 KB per result (MCP up to 500 K chars in qwen-code), and the strategies vary. pi keeps the tail for bash and the head for read. Middle elision is used by crush, oh-my-pi, gemini-cli shell/MCP and qwen-code. kimi-cli keeps the head.

### Cited Findings

**pi**
- Shared limits: `DEFAULT_MAX_LINES = 2000`, `DEFAULT_MAX_BYTES = 50*1024`, `GREP_MAX_LINE_LENGTH = 500`; whichever limit is hit first applies (PI/coding-agent/src/core/tools/truncate.ts:4-13).
- Strategies: `truncateHead` keeps whole leading lines, `truncateTail` keeps trailing lines (and can keep part of an oversized last line), `truncateMiddle` keeps both ends with `…N chars truncated…` (truncate.ts:103-119, 199-213, 292-313).
- bash keeps the tail (PI/coding-agent/src/core/tools/output-accumulator.ts:65-66, 98-126).
  - The full output goes to `tmpdir()/pi-bash-<hex16>.log`, created once any limit is exceeded (output-accumulator.ts:26-29, 246-250; bash.ts:427).
  - The model is told the path, e.g. `[Showing lines ${start}-${end} of ${total}. Full output: ${path}]`, plus byte-limit and partial-last-line variants (PI/coding-agent/src/core/tools/bash.ts:349-356; verified).
  - Empty output becomes `(no output)`. A non-zero exit appends `Command exited with code N` and sets `isError` (bash.ts:341, 403-409).
- read keeps the head and writes no spill file; the file itself is the original.
  - Notices: `[Showing lines s-e of N. Use offset=K to continue.]` and an oversized-line hint pointing at `sed -n 'Np' … | head -c 51200` (PI/coding-agent/src/core/tools/read.ts:156-178, 161).
- grep: 100 matches by default, lines cut to 500 chars, byte cap only. Notices are joined in `[...]`, e.g. `Some lines truncated to 500 chars. Use read tool to see full lines` (PI/coding-agent/src/core/tools/grep.ts:41, 209, 282-303).
- find: 1000 results. ls: 500 entries (find.ts:41; ls.ts:23, 147).
- MCP: `MCP_OUTPUT_MAX_BYTES = 20*1024`, middle truncation. The model gets `Warning: truncated output (original token count: …)` and `[Full output: ${path} (read it with offset/limit)]`; the full output goes to `tmpdir()/pi-mcp-<hex>.txt` (PI/coding-agent/src/extensions/mcp/tools.ts:46, 66, 127-138).
- edit/write: the model sees only `Successfully replaced N block(s) in path.` or `Successfully wrote to path`. The diff lives in `details` only (PI/coding-agent/src/core/tools/edit.ts:204-210; write.ts:86).
- A `length` stop fails every tool call in that message with a re-issue instruction (PI/agent/src/agent-loop.ts:478-503).

**oh-my-pi**
- Defaults are raised to `DEFAULT_MAX_LINES = 3000` (50 KB bytes, 512-column cap). Artifacts are uncapped: `ARTIFACT_DEFAULT_MAX_BYTES = 0` (OMP/tui/src/tools/streaming-output.ts:10-24).
- A generic spill wrapper runs after every wrapped tool's `execute`, including MCP and extension tools (OMP/coding-agent/src/tools/output-meta.ts:500-690).
  - Over the 50 KB spill threshold it saves the full text with `saveArtifact` and inlines a head and tail (20 KB each by default) with `[…Nln elided…]` / `[…NB elided…]` (output-meta.ts:552-562; OMP/coding-agent/src/tools/settings.ts:12-109; streaming-output.ts:533-538).
  - Notice: `Read artifact://<id> for full output` (OMP/tui/src/tools/output-meta.ts:134-135).
- Artifacts are stored at `<session.jsonl minus .jsonl>/{id}.{tool}.log`, written atomically (OMP/coding-agent/src/session/artifacts.ts:44-63, 116-157).
  - `artifact://N` is readable with line selectors; artifacts over 8 MiB require selectors (OMP/coding-agent/src/internal-urls/artifact-protocol.ts:26, 53-62, 111-141).
- bash has a last-resort cap of 60% head and 25% tail, with footer `[raw output: artifact://<id>]` (streaming-output.ts:691-710). If the shell minimizer rewrites output, the original is saved first as a `bash-original` artifact (OMP/coding-agent/src/tools/bash.ts:319-321).
- read defaults to 300 lines, capped at 3000. Notice: `[N more lines in file. Use <path>:M to continue]` (OMP/coding-agent/src/tools/read.ts:1405-1413, 2397).
- Images: oldest images are dropped per request to fit the provider's limit, placeholder `[image omitted: provider image limit]` (OMP/coding-agent/src/session/provider-image-budget.ts:20-73).

**gemini-cli** (default path, `experimental.contextManagement = false`)
- Shell and single-text-part MCP outputs over `min(4*(tokenLimit - lastPromptTokens), truncateToolOutputThreshold)` are truncated. The setting default is `DEFAULT_TRUNCATE_TOOL_OUTPUT_THRESHOLD = 40_000` chars (GC/config/config.ts:482, 3842-3849; GC/scheduler/tool-executor.ts:219-317).
  - The truncation keeps 20% head and 80% tail under the header `Output too large. Showing first X and last Y characters. For full output see: ${outputFile}`, with `... [N characters omitted] ...` between (GC/utils/fileUtils.ts:774-794).
- All other outputs are capped per text part at `MAX_STORED_TOOL_OUTPUT_BYTES = 64*1024` (verified), keeping the head, with `\n... [Tool output truncated to conserve memory. For full output see: ${path}]` (GC/utils/constants.ts:18; GC/scheduler/tool-executor.ts:340-403; GC/utils/tool-utils.ts:153-180).
- Full output is written to `${projectTempDir}/tool-outputs[/session-<id>]/<tool>_<id>.txt` (GC/utils/fileUtils.ts:799-825).
- Other caps:
  - read_file: 2000 lines, 2000 chars per line, 20 MB, with a paging header (GC/utils/constants.ts:10-12; GC/tools/read-file.ts:175-187)
  - grep: 100 matches (GC/tools/constants.ts:6)
  - web_fetch: `MAX_CONTENT_LENGTH = 250000`; the primary path is model-mediated (GC/tools/web-fetch.ts:42-46, 807)
  - shell process buffer: 16 MB, tail kept, `[GEMINI_CLI_WARNING: Output truncated. The buffer is limited to 16MB.]` (GC/services/shellExecutionService.ts:50, 488-518)
- Opt-in model summarisation of shell output (`summarizeToolOutput`) uses flash-lite with `maxOutputTokens: 2000` and saves no original (GC/tools/shell.ts:1111-1134; GC/utils/summarizer.ts:79-104).
- An errored call sends only `{error: message}`; `llmContent` is discarded (GC/scheduler/tool-executor.ts:560-585).
- With management on, `ToolOutputDistillationService` takes over (read_file and read_many_files exempt):
  - threshold 40,000 chars; prefix `[Message Normalized: Tool output exceeded size limit]` and suffix `Full output saved to: ${savedPath}`
  - outputs over 80,000 chars (and up to 1,000,000) also get an LLM "Strategic Significance" summary (GC/context/toolDistillationService.ts:32, 57-153, 196-231; GC/context/truncation.ts:18-19)

**qwen-code**
- Defaults: `DEFAULT_TRUNCATE_TOOL_OUTPUT_THRESHOLD = 25_000`, `..._LINES = 1000`, `DEFAULT_TOOL_OUTPUT_BATCH_BUDGET = 200_000` chars per batch (verified) (QC/config/config.ts:960-967).
- Layer 1, disk-spill gate. It fires above threshold + `GATE_HEADROOM = 3000` (28,000 chars) (verified) (QC/core/coreToolScheduler.ts:281, 6194-6205).
  - Full text goes to `<projectTempDir>/tool-results/<callId>.txt` with mode 0600. Caps: 50 MB per file, 500 MB per session (QC/tools/truncation.ts:21-22, 435-534; QC/config/storage.ts:632-633).
  - The model gets a `<persisted-output>` stub: `Output too large (N KB). Full output saved to: /abs/path`, `Full output sha256: <hex>`, `Note: this file may be cleaned up after 24 hours.`, a read_file instruction and a 2000-char preview (QC/tools/truncation.ts:551-559).
  - Exempt: tools that set `outputBudgetApplied` (shell, environments) and `exec`, `read_file`, `read_mcp_resource`, `enter_plan_mode`, `search_memory` (coreToolScheduler.ts:286-292).
- Layer 2, per-tool truncation. 20% head and 80% tail around `... [CONTENT TRUNCATED] ...`, full output saved to `<projectTempDir>/<tool>_<12hex>.output`, wrapper `Tool output was too large and has been truncated.` (QC/tools/truncation.ts:112-137, 198-236).
- Layer 3: if hook context or a system reminder was appended afterwards, a combined pass re-bounds at 2x the budget (truncation.ts:52).
- Layer 4, batch budget. The 200 K budget is shared fairly across the batch, not newest-first. Each result over its share is spilled with `Tool output truncated. Persisted tool-output artifact: <path>` (QC/tools/tool-response-finalizer.ts:163-256, 330-494).
- Per-tool caps:

  | Tool | Cap | Source |
  |---|---|---|
  | shell | 30,000 | QC/tools/shell.ts:114 |
  | grep / ripGrep | 20,000 | QC/tools/grep.ts:679-681 |
  | MCP | 500,000 | QC/tools/mcp-tool.ts:1155-1163 |
  | agent | 32,000, tail | QC/tools/agent/agent.ts:769-775 |
  | web_search | 102,000 | QC/tools/web-search.ts:41-48 |
  | read_file | Infinity; pages itself | QC/tools/read-file.ts:580-588 |

- read_file has a session read cache. A repeat read returns `[File <p> unchanged since last read in this session — the full content was provided earlier in this conversation. If you cannot retrieve that prior content (e.g. after context compaction) ... re-read with explicit offset/limit ...]` (QC/tools/read-file.ts:513-527).

**kimi-cli**
- Shared `ToolResultBuilder`: `DEFAULT_MAX_CHARS = 50_000`, `DEFAULT_MAX_LINE_LENGTH = 2000` (verified), per-line marker `[...truncated]`. Once the buffer is full it keeps the head and silently drops all later writes (K/src/kimi_cli/tools/utils.py:33-120).
  - It appends `Output is truncated to fit in the message.` (utils.py:167-191).
  - No spill file.
- MCP: `MCP_MAX_OUTPUT_CHARS = 100_000` (verified), notice `[Output truncated: exceeded 100000 character limit. Use pagination or more specific queries to get remaining content.]`. Oversized media is dropped silently, although the docstring says it is replaced by a placeholder (K/src/kimi_cli/soul/toolset.py:1023-1094, 1042, 1077).
- ReadFile: `MAX_LINES = 1000`, `MAX_LINE_LENGTH = 2000`, `MAX_BYTES = 100<<10` (K/src/kimi_cli/tools/file/read.py:16-18, 209-222).
- Glob: `MAX_MATCHES = 1000` (K/src/kimi_cli/tools/file/glob.py:20).
- Grep: `head_limit` 250 lines plus the builder (K/src/kimi_cli/tools/file/grep_local.py:101-110, 561-564).
- Background shell tasks are the only spill. `TaskOutput` previews the **last** 32 KiB with `[Truncated. Full output: {output_path}]` and a ReadFile hint (K/src/kimi_cli/tools/background/__init__.py:14, 76-100, 198-209).
- Model-facing wrapping: the message goes in `<system>...</system>` before the output. Errors become `<system>ERROR: …</system>`; empty output becomes `<system>Tool output is empty.</system>` (K/src/kimi_cli/soul/message.py:19-62).

**crush**
- bash: `MaxOutputLength = 30000`, measured in display width (verified) (CR/agent/tools/bash.go:50-56, 428-448).
  - Stdout and stderr are each truncated separately; half of the budget is kept from each end.
  - The full ANSI-stripped output is spilled, and the marker becomes `... [%d lines truncated, full output: %s] ...` (otherwise `... [%d lines truncated] ...`).
  - Spill files go to `.crush/shell-output/output-*.log`, kept 7 days within a 256 MB directory cap (CR/shell/truncate.go:22-43, 175-217).
  - `<cwd>%s</cwd>` is appended on success (bash.go:287, 371).
- view: `DefaultReadLimit = 200` lines, `MaxLineLength = 2000`. A section over `MaxViewSize = 200 KB` is an **error, not a truncation** (CR/agent/tools/view.go:74-79, 239-240, 337-347).
- grep and glob: hard limit of 100 results, notice `(Results are truncated. Consider using a more specific path or pattern.)` (CR/agent/tools/grep.go:143-179; glob.go:70-82).
- ls: 1000 files (CR/agent/tools/ls.go:53, 162-167).
- fetch: `MaxFetchSize = 100 KB`, head byte cut with `[Content truncated to %d bytes]`, no spill (CR/agent/tools/fetch.go:22, 130, 181-185).
- web_fetch (sub-agent): pages over 50,000 are saved to `page-*.md` with `Content saved to: %s ... Use the view and grep tools` (CR/agent/tools/fetch_types.go:13; web_fetch.go:51-71). agentic_fetch deletes that directory on return (CR/agent/agentic_fetch_tool.go:102-106).
- MCP and sourcegraph have no size cap (CR/agent/tools/mcp/tools.go:64-109; sourcegraph.go:137).
- Tool `Metadata` (e.g., edit's full `OldContent`/`NewContent`, write's `Diff`) is stored for the UI but never sent to the model (CR/agent/agent.go:2232-2270; CR/message/content.go:627-660).

### Inferences
- **qwen-code gate ordering (possible bug).** The 28 K universal gate runs before per-tool truncation (coreToolScheduler.ts:6198 precedes 6353). From the code alone, MCP, agent and web_search text above 28 K would be spilled with a 2,000-char preview and never reach its declared 500 K / 32 K / 102 K budget. No test confirms this.
- **kimi-cli silent full-buffer case.** If a line ends exactly at 50,000 chars, later lines are dropped with no truncation notice, because the notice flag is set only when a line is actually shortened (utils.py:98-120).
- **gemini-cli per-part cap.** The 64 KB cap applies per part, so a multi-part read_many_files result can exceed 64 KB in total.
- **crush ls.** The depth-notice branch assigns `output =` instead of appending, so it appears to overwrite the truncation notice (ls.go:162-167).
- **Head vs tail.** Head-keeping caps (kimi builder, read tools, crush fetch) lose the end of a log, where errors usually are. Tail- or tail-weighted caps (pi bash, qwen 1/5 head and 4/5 tail, gemini 20/80, kimi background task output) favour recent output.

### Gaps
- Temp-file cleanup is UNCONFIRMED for pi (no unlink found for `pi-bash-*.log`) and gemini-cli. qwen-code cleans after 24 h at session start (QC/core/client.ts:668, 1870; QC/utils/toolResultCleanup.ts:19-56).
- I could not confirm whether pi's codemode and MCP extensions are enabled by default.
- crush depends on `charm.land/fantasy v0.45.1`, which is not present locally, so how it maps `ClientMetadata` into provider messages is UNCONFIRMED.

## Q2. Later in the session: age- or size-based clearing, masking, pruning, compaction; exact placeholders; recoverability

### Takeaway
Three harnesses rewrite individual old tool results in place before compaction: gemini-cli (masking, every turn), qwen-code (microcompaction) and oh-my-pi (superseded-read and useless pruning, plus artifact-backed "shake"). pi, kimi-cli and crush keep old results verbatim until a whole-history compaction or summary replaces them. Recoverability varies:
- Recoverable by file path: gemini-cli masking, oh-my-pi shake/artifacts and qwen-code spills.
- Not recoverable: qwen-code microcompaction (the path is lost), oh-my-pi pruning (the stored session is rewritten), and every LLM summary.

### Cited Findings

**pi: no per-result clearing; LLM compaction with a verbatim tail**
- No pruning or masking of old results exists in core. compaction.md presents compaction as the only reduction mechanism (PI/coding-agent/docs/compaction.md).
- The other mechanism is an append-only `ContextEditEntry {targetId, replacement|null}`, applied when the context is projected (PI/coding-agent/src/core/session-manager.ts:175-180, 519-573).
  - It is written only by extensions or by overflow recovery (`_omitRecoveryAttempt` nulls the failed assistant message and its tool results) (PI/coding-agent/src/core/agent-session.ts:928-929, 1202-1218).
- Trigger: `contextTokens > contextWindow - reserveTokens`, with defaults `reserveTokens 16384` and `keepRecentTokens 20000` (PI/coding-agent/src/core/compaction/compaction.ts:126-130, 267-270; settings-manager.ts:23-26).
  - It is also checked mid-run between tool execution and the next assistant request, so large fresh results get compacted before they are sent (agent-session.ts:742-751, 871; coding-agent/CHANGELOG.md:423, #6879).
- Summariser input: tool results are serialised as `[Tool result]: …` and cut to `TOOL_RESULT_MAX_CHARS = 2000`, head only, with marker `\n\n[... N more characters truncated]` (verified) (PI/coding-agent/src/core/compaction/utils.ts:94-104, 149; CHANGELOG.md:3040, #1796). Tool-call arguments are not truncated (utils.ts:114-155).
- The model then sees the summary as a user message `The conversation history before this point was compacted into the following summary:\n\n<summary>…</summary>` (PI/coding-agent/src/core/messages.ts:11-17).
- Read and modified files are appended as `<read-files>` / `<modified-files>` lists and carried forward (utils.ts:30-87).
- Branch summarisation skips tool results entirely ("Skip tool results - context is in assistant's tool call") (verified) (PI/coding-agent/src/core/compaction/branch-summarization.ts:155-160).
- Recovery: the session JSONL is append-only and holds everything (agent-session.ts:1108-1128). bash exports `PI_SESSION_FILE` (tools/bash.ts:202-211), but no tool re-reads old entries.

**oh-my-pi: per-result pruning, shake, windowed summariser**
- Per-turn stale-result pass (verified constants):
  - `SUPERSEDED_NOTICE = "[Superseded by a newer read of this file]"`, `USELESS_NOTICE = "[Uneventful result elided]"`
  - suffix limit `DEFAULT_SUFFIX_TOKEN_LIMIT = 8_000`, idle flush `PRUNE_IDLE_FLUSH_MS = 90*60_000` (exceeds the 1 h Anthropic cache TTL)
  - (OMP/agent/src/compaction/pruning.ts:94-99, 137; OMP/coding-agent/src/session/session-maintenance.ts:309-317)
  - Supersession is keyed on the read path with its selector stripped (pruning.ts:448-455).
  - "Useless" flags are set by tools on zero-hit grep/glob/find etc. and copied by the agent loop; errors always win (OMP/agent/src/agent-loop.ts:540-598).
- Threshold prune `pruneToolOutputs` (verified):
  - placeholder `[Output truncated - ${tokens} tokens]`
  - defaults: protect 40 K newest tool tokens, 20 K minimum savings, `MIN_PRUNE_TOKENS = 50`
  - **cache guard:** results whose all-message suffix exceeds `cacheWarmSuffixTokens` are skipped. The coding agent passes 8,000, or 0 for prefix-bound thinking models (pruning.ts:140-152, 332-436; session-maintenance.ts:674-700).
- Pruning blanks `content` in place and rewrites the session file atomically; the original is not kept in model-readable form (pruning.ts:76-86; OMP/coding-agent/src/session/session-manager.ts:3040-3043).
- Shake (artifact-backed):
  - auto mode protects the last 16 K tokens, needs 4 K minimum savings and targets fenced/XML blocks of at least 400 tokens; manual `/shake` protects 4 K (OMP/agent/src/compaction/shake.ts:48-76)
  - placeholder `[shaken ~N tokens — recover: artifact://<id> (region k)]` (session-maintenance.ts:1029-1043)
- Compaction:
  - default method order `["remote","snapcompact","handoff","shake","soft"]` (/Users/patrickbeam/projects/blackbird/quarantine/oh-my-pi/docs/compaction.md:512)
  - threshold `contextWindow - max(floor(0.15*window), 16384)`, `keepRecentTokens = 20000` (OMP/agent/src/compaction/compaction.ts:213, 233, 334-410)
  - the summariser cuts tool results to 2000 chars and drops useless call/result pairs (OMP/agent/src/compaction/utils.ts:200-262)
- "Windowed" is confirmed and means a fold over summariser input. When the serialised span exceeds `max(minWindow, 0.8*window - maxTokens - 16384)`, messages are packed into windows on message boundaries and each window updates the summary carried from the previous one. A provider overflow halves the window and re-plans it (compaction.ts:781-929). `MAX_SUMMARY_TOKENS = 16384` (compaction.ts:225); CHANGELOG OMP/agent/CHANGELOG.md:276.
- Advisor-only eviction turns read/grep/glob results from earlier reviews into `[Stale result elided - N tokens]` (OMP/coding-agent/src/advisor/tool-result-eviction.ts:10-14).
- Opt-in `experimentalContextManagement` disables pruning and exposes `history://current/full` (OMP/coding-agent/src/internal-urls/history-protocol.ts:343-361).

**gemini-cli: masking every turn, newest-first budget in compression**
- `tryMaskToolOutputs` runs every turn, unconditionally, on both paths (verified) (GC/core/client.ts:713).
- `ToolOutputMaskingService` (verified):
  - `DEFAULT_TOOL_PROTECTION_THRESHOLD = 50000` (newest tool tokens protected), `DEFAULT_MIN_PRUNABLE_TOKENS_THRESHOLD = 30000`, `DEFAULT_PROTECT_LATEST_TURN = true`
  - exempt tools: `activate_skill`, `ask_user`, `enter_plan_mode`, `exit_plan_mode`
  - (GC/context/toolOutputMaskingService.ts:25-41, 92-154)
  - The response is written as pretty JSON to `${projectTempDir}/tool-outputs/session-<id>/<tool>_<callId>_<rand>.txt` (toolOutputMaskingService.ts:163-190, 272-286).
  - Placeholder (verified) (toolOutputMaskingService.ts:360-367):
    ```
    <tool_output_masked>
    ${preview}

    Output too large. Full output available at: ${filePath}
    </tool_output_masked>
    ```
  - Preview: shell gets the first and last 10 lines; other tools get 250+250 chars of the JSON.
- `ChatCompressionService` (default path):
  - trigger: `tokens >= 0.5 * tokenLimit` (`DEFAULT_COMPRESSION_TOKEN_THRESHOLD`) (GC/context/chatCompressionService.ts:45, 501-519)
  - `COMPRESSION_PRESERVE_THRESHOLD = 0.3`, split by JSON character count (chatCompressionService.ts:269, 557-560)
- The reported "newest-first budget to temp files" is **confirmed** (verified):
  - `truncateHistoryToBudget` walks history newest to oldest against `COMPRESSION_FUNCTION_RESPONSE_TOKEN_BUDGET = 50_000`. Any response that would push the counter over budget is saved via `saveTruncatedToolOutput` and replaced by `formatTruncatedToolOutput(...)`, using the 40 K threshold's head/tail form (chatCompressionService.ts:274, 364-464).
  - The check is greedy per part. The newest history turn is not exempt, but the current turn's new responses are not in history yet (client.ts:817).
  - Files go to `${projectTempDir}/tool-outputs/<tool>_<N>.txt` with a per-session counter and no session subfolder (GC/config/config.ts:3860-3862).
  - The docstring's "last 30 lines" wording is stale.
- The summariser sees the original slice if it fits, otherwise the truncated one. The kept 30% is the truncated version (chatCompressionService.ts:563-585).
- Self-verification pass: `Critically evaluate the <state_snapshot> you just generated. Did you omit any specific technical details, file paths, tool results, or user constraints ...` (chatCompressionService.ts:616-645).
- New history: `[user: summary, model: 'Got it. Thanks for the additional context!', ...kept]` (chatCompressionService.ts:665-675).
- If compression inflates the token count, later auto runs do truncation only (`CONTENT_TRUNCATED`) (chatCompressionService.ts:528-555, 696-705).
- Subagent collapse (`collapseOlderFunctionResponses`):
  - protects the 3 newest response turns; strings over 2048 bytes are cut to a 2 KB prefix with `[Tool output collapsed from previous turn: N bytes omitted to conserve memory]`; no file is written
  - retrieval tools are exempt (chatCompressionService.ts:50-178)
- Management-on pipeline:
  - ToolMasking at 8000 tokens, also masking functionCall arguments (GC/context/processors/toolMaskingProcessor.ts:123-226)
  - NodeDistillation replaces responses with `{summary}`, no file (GC/context/processors/nodeDistillationProcessor.ts:40-170)
  - NodeTruncation with `[... OMITTED n chars ...]` (GC/context/processors/nodeTruncationProcessor.ts:43-47)
  - The "pristine graph" keeps originals in memory but nothing exposes it to the model (GC/context/contextManager.ts:295-300).

**qwen-code: microcompaction plus full-history compression with no tail**
- Microcompaction placeholders (verified): `MICROCOMPACT_CLEARED_MESSAGE = '[Old tool result content cleared]'`, `MICROCOMPACT_CLEARED_IMAGE_PREFIX = '[Old inline media cleared:'` + ` <mime>]` (QC/services/microcompaction/microcompact.ts:14-15).
  - Compactable tools: read_file, shell, grep, glob, web_fetch, web_search, read_mcp_resource, edit, write_file, skill, search_memory (microcompact.ts:38-50).
  - Triggers:
    - idle: at least `toolResultsThresholdMinutes ?? 60` since the last API reply
    - size: compactable output above `DEFAULT_TOOL_RESULTS_TOTAL_CHARS_THRESHOLD = 500_000`, cleared down to half that
    - `/compress-fast`
    - memory pressure
    - (microcompact.ts:297-316, 635-728; QC/config/clearContextDefaults.ts:7; QC/core/llm-chat.ts:2830-2900)
  - The newest `toolResultsNumToKeep` results are kept (default 5; env `QWEN_MC_KEEP_RECENT`) (microcompact.ts:1072-1093).
  - Error results are never cleared. The whole `response` becomes `{output: '[Old tool result content cleared]'}`, keeping `id`/`name` (microcompact.ts:976, 1012-1017).
- Full compression (verified comment): "CLAUDE-CODE-STYLE FULL-HISTORY COMPRESSION: the entire curated history is sent to the summary side-query (no split, no tail preservation)" (QC/services/chatCompressionService.ts:535-539).
  - Thresholds: `auto = min(0.85*window, window - 20000 - 13000)`. Other triggers: 20+ tool images, or a 413/context-length retry (chatCompressionService.ts:101-131, 210-245, 487-510).
  - Post-compact history:
    - the summary, with the trailer `Resume the prior task using the summary above. Continue from the last in-flight step; do not acknowledge the summary ...`
    - `Got it. Thanks for the additional context!`
    - up to 5 restored files (5,000 tokens each, 50,000 total) and 3 images
    - (QC/services/postCompactAttachments.ts:27-60, 514-515, 800-882)
- "Slims compaction inputs" is **partly confirmed**:
  - Every compaction replaces unsupported media with `[image: <mime>]` / `[document: <mime>]`.
  - Text is cut to `4000` chars with `\n[...truncated for compaction]` **only on the 413 payload-overflow path**; token-driven compactions send tool text in full (QC/services/compactionInputSlimming.ts:54-57, 280, 306-497; chatCompressionService.ts:254).
- gemini-cli's `COMPRESSION_FUNCTION_RESPONSE_TOKEN_BUDGET`, `findCompressSplitPoint` and `ToolOutputMaskingService` are absent from qwen-code (grep).
- Microcompaction disarms the read cache for evicted paths, so the next read_file returns real content rather than the "unchanged" stub (QC/core/client.ts:3280-3290).

**kimi-cli: whole-context operations only**
- Auto-compaction fires when `token_count >= max_context_size * 0.85` or `token_count + 50_000 >= max_context_size` (verified) (K/src/kimi_cli/config.py:88-95; K/src/kimi_cli/soul/compaction.py:60-76). It is checked before every step (K/src/kimi_cli/soul/kimisoul.py:1016-1024).
- `SimpleCompaction(max_preserved_messages=2)`:
  - keeps everything from the second-to-last user/assistant message onward
  - renders compacted messages as `## Message i\nRole: …\nContent:\n` + **TextParts only**: tool results go in full, while tool-call names, arguments, ids, images and reasoning are omitted (verified) (compaction.py:160-188)
  - new history starts with `<system>Previous context has been compacted. Here is the compaction output:</system>` (compaction.py:18, 139)
- D-Mail (only the `okabe` agent; disabled in default) (K/src/kimi_cli/agents/okabe/agent.yaml:6; default/agent.yaml:9):
  - It reverts the context to a checkpoint, dropping every later tool result. The model-written D-Mail text is the only thing carried back (K/src/kimi_cli/soul/context.py:135-200; kimisoul.py:1313-1335).
  - Injected text: `<system>You just got a D-Mail from your future self. ... You MUST NEVER mention to the user about this information. D-Mail content: …</system>` (kimisoul.py:1101-1106).
- I found no age-based clearing (UNCONFIRMED beyond the files searched).
- Rotated `context_{n}.jsonl` files and `wire.jsonl` keep the already-truncated results. No session path or tool is exposed to the model (K/src/kimi_cli/utils/path.py:34-58; K/src/kimi_cli/soul/agent.py:50-64).

**crush: summary replaces history**
- Auto-summarise runs as a `StopWhen` check after each step. With remaining = window − (prompt+completion), it fires if remaining ≤ 20,000 for windows over 200 K, else if remaining ≤ 20% of the window. It is skipped for unknown windows or `disable_auto_summarize` (verified) (CR/agent/agent.go:55-58, 1095-1116).
- The summariser gets the history from the last summary onward, verbatim (tool results as already truncated), and no tool definitions. The prompt says `This summary will be the ONLY context available ... **Length**: No limit.` (agent.go:1411-1559; CR/agent/templates/summary.md:1-48).
- The summary is re-labelled a User message. `ListFromSummary` selects rows at or after the summary's `created_at` (agent.go:1795-1812; CR/db/sql/messages.sql:12-20).
- No per-result clearing exists (grep for prune/mask/elide/clear).
- SQLite keeps all rows, but no tool exposes them to the model. Spill files stay readable for 7 days.

### Inferences
- **oh-my-pi's threshold prune effectively cannot fire for ordinary results.** I checked this against code but did not run it. A non-superseded, non-useless candidate needs at least 40 K tool-output tokens after it (`accumulatedTokens >= protectTokens`) while its all-message suffix is at most 8 K (`inWarmPrefix` false). Tool tokens are a subset of message tokens, so both can't hold. In practice only superseded/useless results are blanked per turn. Deeper reclamation is left to shake/compaction, which matches the code comment "deeper stale/age victims are left to compaction/shake, which rebuild the cache anyway".
- **Recovery hints get lost.** qwen-code microcompaction overwrites the whole response, so any `<persisted-output>` path inside it is lost, and its placeholder gives no pointer. gemini-cli and oh-my-pi placeholders carry a path; qwen-code and pi summaries don't.
- **Prompt cache drives pruning.** oh-my-pi (warm-suffix guard, 90-minute idle flush) and gemini-cli (protect newest 50 K, require 30 K savings) gate pruning on prompt-cache economics. qwen-code times idle microcompaction to 60 minutes, apparently for the same cache reason (inference from the "since last API reply" trigger).
- **gemini-cli file naming** (`<tool>_<N>.txt` with a per-session counter, no session folder) suggests later sessions in the same project can overwrite earlier truncation files (not tested).

### Gaps
- Whether gemini-cli's session recording or rewind keeps pre-masking originals is UNCONFIRMED (GC/services/chatRecordingService.ts:955-1031 syncs the masked parts).
- Whether qwen-code's transcript stores the pre-truncation original is UNCONFIRMED.
- What oh-my-pi's `artifact://` resolves to in non-persisted sessions: artifacts sit in an in-memory map with no reader found (OMP/coding-agent/src/session/session-manager.ts:2659-2667).

## Q3. Differential treatment (read-only vs effectful), typed output lifecycles, model-declared retention

### Takeaway
None of the six splits retention by read-only versus effectful. Where tools are treated differently, it is through explicit name lists: protected tools, compactable tools and retrieval exemptions. oh-my-pi has the richest typed lifecycle: superseded reads, tool-flagged "useless" results, protected skill and plan outputs. No harness lets the model mark a tool result "keep". The closest equivalents are kimi-cli's D-Mail (the model writes what survives a revert), compaction custom instructions, and oh-my-pi's experimental `context_notes`.

### Cited Findings
- **Model vs UI channels.** Every harness separates model-visible content from UI/debug data:
  - pi: `content` vs `details`; `structuredContent` "Not sent to the model" (PI/agent/src/types.ts:424-446)
  - kimi: `output`/`message` vs `display`/`brief`/`extras` (K/packages/kosong/src/kosong/tooling/__init__.py:112-170)
  - crush: Content vs Metadata (CR/message/content.go:627-660)
  - gemini: `llmContent` vs `returnDisplay` (GC/tools/tools.ts:750-791)
- **pi.** Read-only vs coding tool sets differ only by which tools exist (PI/coding-agent/src/core/tools/index.ts:164-211). The model has no context-edit tool; `appendContextEdit` callers are extension drafts and recovery only (agent-session.ts:929, 1213).
- **oh-my-pi.**
  - Supersession applies only to `read`; edits and writes only feed the `<files>` list.
  - Protected from pruning: `skill` results, `skill://` reads, the active plan file. `artifact://` re-reads are protected from auto-shake (OMP/agent/src/compaction/tool-protection.ts:46-58).
  - "Useless" flags come from zero-hit search tools and wait/memory-recall (OMP/agent/src/agent-loop.ts:540-598).
  - No model-declared pin for results; `session-pins.ts` pins sessions for the resume picker.
- **gemini-cli.** Exemptions are list-based:
  - masking exempts skill, ask-user and plan-mode tools; read_file is **not** exempt (GC/context/toolOutputMaskingService.ts:36-41)
  - distillation exempts read_file and read_many_files (GC/context/toolDistillationService.ts:83-87)
  - subagent collapse exempts the retrieval tools (read_file, grep, glob, read_mcp_resource …) (GC/context/chatCompressionService.ts:56-86)
- **qwen-code.**
  - `COMPACTABLE_TOOLS` covers both read-only and effectful tools (edit, write_file).
  - Error results are never cleared (QC/services/microcompaction/microcompact.ts:976).
  - Managed-memory reads are always kept (QC/core/client.ts:3267-3275).
  - write_file results count as evidence that a file is still in history; edit results do not (microcompact.ts:599-610).
- **kimi-cli.**
  - Typed errors: `ToolNotFoundError`, `ToolParseError`, `ToolValidateError`, `ToolRuntimeError`, and `ToolRejectedError`, whose default tells the model to "Stop what you are doing and wait" (K/packages/kosong/src/kosong/tooling/error.py; K/src/kimi_cli/tools/utils.py:202-219).
  - Identical repeated calls get `<system-reminder>` warnings at streak 3, 5 and 8, and a forced stop at 12 (K/src/kimi_cli/soul/toolset.py:116-226).
  - The SendDMail description says "give your past self only the useful part" (K/src/kimi_cli/tools/dmail/dmail.md).
- **crush.** The `safeCommands` list only skips permission prompts (CR/agent/tools/safe.go:9-58).
  - Loop detection stops the run when more than 5 identical (tool, input, output) hashes appear in a window of 10 (CR/agent/loop_detection.go:12-70).

### Inferences
- **Supersession is the one semantic lifecycle.** Only oh-my-pi, and only for reads, treats a newer observation as invalidating an older one. qwen-code's read cache is related but works the other way: it avoids re-sending unchanged content, and microcompaction disarms the cache when it evicts content.
- **Clearing effectful results.** Effectful tool results (edit/write confirmations) are generally tiny at creation in pi, crush and kimi-cli, because diffs go to UI-only fields. qwen-code still clears them, so their history value rests on the call arguments.

### Gaps
- No harness exposes a model-callable "retain/pin result" operation. I found none (grep-based; absence is not proven exhaustively).

## Q4. Protocol validity: call/result pairing and thinking/reasoning items

### Takeaway
Every harness that rewrites results replaces only the payload and keeps the call id and name. Compaction cut points never fall on a tool result. Missing results are synthesised as errors at request-build time, except in kimi-cli, which commits a step's assistant message and results atomically so orphans cannot arise. Cross-provider thinking is normalised in pi, oh-my-pi and gemini-cli; kimi-cli and qwen-code drop reasoning in compaction.

### Cited Findings
- **pi.**
  - Cut points: `isCutPointMessage` returns false for `toolResult` ("Never cut at tool results (they must follow their tool call)") (verified) (PI/coding-agent/src/core/compaction/compaction.ts:351-364, 389-393).
  - Request-time repair inserts synthetic `No result provided` error results for orphaned calls. System messages that land between a call and its result are held back (verified) (PI/ai/src/api/transform-messages.ts:158-232).
  - Errored or aborted assistant messages are dropped (transform-messages.ts:200-203).
  - Thinking: cross-model it becomes plain text; redacted thinking is kept only for the same model; `thoughtSignature` is stripped when the model differs; tool-call ids are normalised through a map that also covers the results (transform-messages.ts:84-142).
- **oh-my-pi.**
  - Calls from an errored or aborted assistant are kept and get an `aborted` error result. Orphan results become `<stale-tool-result tool=… id=…>…</stale-tool-result>` user notes (OMP/ai/src/providers/transform-messages.ts:1086-1291).
  - Agent-loop synthetic results: `Tool execution was aborted`, `Tool call was not executed because the assistant ended its turn`, `Skipped due to <reason>. Do not count this skipped result as completed work…` (OMP/agent/src/agent-loop.ts:3765-3869).
  - After a prune, prefix-bound thinking models get thinking stripped from earlier assistant turns (OMP/ai/src/providers/transform-messages.ts:638-658, 798-803).
- **gemini-cli.**
  - Masking, budget truncation and collapse all spread `...functionResponse` and replace only `response` (GC/context/toolOutputMaskingService.ts:224-231; chatCompressionService.ts:429-435).
  - Compression splits only before a user turn with no functionResponse (chatCompressionService.ts:282-322).
  - `SYNTHETIC_THOUGHT_SIGNATURE = 'skip_thought_signature_validator'` is injected on the first functionCall of each model turn in the active loop (GC/core/geminiChat.ts:110, 1259-1312).
  - The pipeline's `hardenHistory` injects `{error:'The tool execution result was lost due to context management truncation.'}` (GC/utils/historyHardening.ts:21-377).
- **qwen-code.**
  - Every send synthesises a missing result: `'Tool execution result was not recorded — likely interrupted by network failure, abort, or process exit. Treat as failure and retry if needed.'` (verified) (QC/core/llm-chat.ts:1795-1799, 2086-2125).
  - The OpenAI converter's `cleanOrphanedToolCalls` keeps only calls whose results follow directly and drops orphan or empty-id results. Media from tool results moves to a following user message (QC/core/openaiContentGenerator/converter.ts:489-491, 743-785, 2106-2264).
  - Thoughts map to `reasoning_content`. `/compress-fast` strips thought parts (QC/core/llm-chat.ts:2850-2853).
- **kimi-cli.**
  - The assistant message and all its tool messages are appended together after every result arrives, shielded from cancellation. Cancellation writes nothing for that step (K/src/kimi_cli/soul/kimisoul.py:1279-1409; K/packages/kosong/src/kosong/__init__.py:168-222).
  - The kept compaction block starts at a user or assistant message, so it never begins with an orphan result (compaction.py:160-174).
  - Kimi `reasoning_content` round-trips (K/packages/kosong/src/kosong/chat_provider/kimi.py:326-353). Compaction drops reasoning (compaction.py:143, 187).
- **crush.**
  - `preparePrompt` re-emits results directly after their call, citing strict-adjacency providers (Kimi, DeepSeek), and drops `"Dropping orphaned tool result with no matching tool call"` (verified) (CR/agent/agent.go:1608-1705, 1657).
  - Missing results become `"tool call was interrupted and did not produce a result, you may retry this call if the result is still needed"` (verified) (agent.go:1765-1790).
  - Cancellation stores `"Error: user cancelled assistant tool calling"` (agent.go:1158-1212).
  - Reasoning is replayed only when `Thinking != ""`, with all provider signatures attached (CR/message/content.go:596-616).
  - Every history starts with a fixed `<system_reminder>` claiming the todo list "is currently empty", sent whatever the actual todos are (agent.go:1610-1619).

### Inferences
- **Signature-only reasoning.** crush drops reasoning that has a signature but no visible text (content.go:596-616). That could break signature continuity for providers that require it (not tested).
- **Blanking and the cache.** oh-my-pi's in-place blanking forces thinking stripping for prefix-bound providers. Pruning therefore costs both cache and reasoning continuity, which is why its prune guard drops to 0 for those models.

### Gaps
- How crush's fantasy library serialises cross-provider reasoning is UNCONFIRMED (library not local).
- pi: whether tool results that follow a dropped aborted assistant can be sent unpaired is UNCONFIRMED (transform-messages.ts:213-215 suggests they pass through).

## Q5. What the user sees versus the model

### Takeaway
In every harness the user typically sees less of a tool result than the model: collapsed previews of 5-20 lines. The user also sees different data the model never gets, such as diffs and display metadata. After compaction the user still sees the original transcript, while the model sees only the summary. Masking, microcompaction and pruning are silent to the user in gemini-cli, qwen-code and oh-my-pi.

### Cited Findings
- **pi.**
  - Output is collapsed by default (`toolOutputExpanded = false`, ctrl+o toggles) (PI/coding-agent/src/modes/interactive/interactive-mode.ts:497).
  - bash shows its last 5 visual lines; the model's notice footer is replaced by a `[Full output: path. Truncated: …]` warning (PI/coding-agent/src/core/tools/renderers/bash.ts:18-99).
  - read shows nothing collapsed. grep shows 15 lines, find/ls 20.
  - After compaction the TUI rebuilds from raw entries; the summary appears as `[compaction] Compacted from N tokens` (PI/coding-agent/src/modes/interactive/components/compaction-summary-message.ts:36-57).
- **oh-my-pi.**
  - The TUI strips the model notice and shows its own warning (OMP/tui/src/tools/output-meta.ts:353-376).
  - read/grep render `details.displayContent`, so the user still sees originals after the model's copy is pruned (OMP/tui/src/tools/read.ts:513-517).
  - bash shows a 10-line preview (OMP/coding-agent/src/render/render-utils.ts:119).
- **gemini-cli.**
  - The UI shows `returnDisplay` up to `MAXIMUM_RESULT_DISPLAY_CHARACTERS = 20000` (GU/ui/components/shared/SlicingMaxSizedBox.tsx:12).
  - Shell shows the full output even when the model got a truncated or summarised version (GC/tools/shell.ts:946-950).
  - Shown: `Output too long and was saved to: …` (GU/ui/components/messages/ToolGroupMessage.tsx:467-482) and `Context compressed from X% to Y%.` (GU/ui/hooks/useGeminiStream.ts:1391-1392).
  - Silent: masking, budget truncation and collapse (debug/telemetry only).
- **qwen-code.**
  - Shell display: `Output too long and was saved to: <file>`. read_file display: `Read lines S-E of N from <path> (truncated)` (QC/tools/shell.ts:3238-3241; QC/utils/fileUtils.ts:1617-1625).
  - Microcompaction logs to debug only (QC/core/client.ts:3297-3321).
  - The UI history is compacted separately under memory pressure, keeping 20, with the same placeholder (QU/ui/hooks/useHistoryManager.ts:23-26, 195-320).
- **kimi-cli.**
  - The shell TUI renders only `display` blocks (brief, diffs). I found no rendering of `.output` in `ui/shell` (K/src/kimi_cli/ui/shell/visualize/_blocks.py:497-534).
  - ACP shows display blocks then output (K/src/kimi_cli/acp/convert.py:104-127).
- **crush.**
  - The TUI lists the whole transcript, including messages before the summary (CR/ui/model/session.go:93).
  - Collapsed output is 10 lines, `… (%d lines hidden) [click or space to expand]` (CR/ui/chat/tools.go:28-41; CR/ui/chat/assistant.go:22).
  - bash shows `meta.Output` without `<cwd>`; view shows the raw content without line numbers (CR/ui/chat/bash.go:57-109; file.go:79-82).

### Inferences
- **Two views diverge.** In every harness the transcript the user reads and the context the model gets differ after any reduction. The user can still see what the model has lost (oh-my-pi pruning, crush and pi compaction), and the model can hold full output the user saw only as a 5-10 line preview.

### Gaps
- That pi's TUI shows context-edited content in its original form is inferred from the rebuild path, not observed (UNCONFIRMED).
- Web UIs (kimi web, gemini/qwen IDE companions) were not audited.
