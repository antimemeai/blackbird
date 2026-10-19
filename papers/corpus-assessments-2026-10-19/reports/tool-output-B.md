# How coding agents bound large tool output, and how the model gets it back

Read-only source study of snapshots under `/Users/patrickbeam/projects/blackbird/quarantine/` (taken around 2026-09-30). Nothing was run, built or installed. Line numbers refer to those snapshots. A sample of the key constants and markers was re-checked by direct reading; those carry a check mark (✔).

**OpenHands caveat:** `quarantine/openhands/` contains only the Agent Canvas web frontend (`package.json`, v1.24.0). The Python backend is absent: no `CmdOutputObservation`, `truncate_content`, `max_message_chars`, bash-session limits, editor view limits or condenser implementations. A grep of the whole quarantine for `max_message_chars|ObservationMaskingCondenser|truncate_content` found matches only in smolagents and mistral-vibe. The OpenHands section below therefore covers only the wire contract and UI that the frontend exposes.

## Summary table

| Ref | Shell shape | Read / grep | MCP | Full copy | Way back | Ageing |
|---|---|---|---|---|---|---|
| opencode | tail, whole lines, 2000 lines / 50 KiB | read pages (2000 lines / 50 KB); grep 100 matches; glob 100 | head, 2000 lines / 50 KiB | `~/.local/share/opencode/tool-output/tool_<id>`, kept 7 days | Read/Grep on the path (whitelisted) | prune (off by default) → `[Old tool result content cleared]`; no path |
| pi | tail, 2000 lines / 50 KiB | read head + `offset=`; grep 100 / find 1000 / ls 500 | head+tail, 20 KiB | `$TMPDIR/pi-bash-<hex>.log`, never cleaned up | read on the path | none; summary compaction only |
| oh-my-pi | head 20 KB + rolling tail, lines cut at 768 B | read 300 lines by default, code summarised; grep paged | shared spill: head 20 KB + tail 20 KB | per-session artifact dir, `N.<tool>.log` | `artifact://N` with `:a-b` selectors via read | supersede, useless and age prune (destructive); "shake" keeps an artifact link |
| gemini-cli | head 20% + tail 80% of 40,000 chars | read_file 2000 lines; grep 100; others head-only at 64 KB | single text part: 40k head+tail; otherwise 64 KB head-only | `~/.gemini/tmp/<proj>/tool-outputs/session-<id>/<tool>_<callId>.txt`, 30 days | read_file on the path (project temp dir allowed) | masking service (on): `<tool_output_masked>` + path |
| qwen-code | line-aware 1/5 head + 4/5 tail; 25k chars / 1000 lines | similar | 500k-char threshold | `<projectTemp>/…`, `tool-results/<callId>.txt`, about 24 h | read_file on the path | idle microcompaction → `[Old tool result content cleared]` |
| mini-swe-agent | head 5000 + tail 5000 chars when ≥ 10000 | bash only | n/a | trajectory JSON only | re-run selectively | none |
| swe-agent | head-only 100,000 chars by default; `bash_only` config uses head+tail 10k | editor 16,000 chars head; search refuses above 100 files | n/a | `.traj` only | re-run / `view_range` | opt-in history processors (`Old environment output: (N lines omitted)`) |
| openhands (frontend) | backend absent | glob/grep `truncated` flag ("100 files") | no truncation signal in contract | backend absent | backend absent | `CondensationEvent.forgotten_event_ids` + summary |

---

## 1. opencode (`quarantine/opencode/packages/`; `oc/` = `opencode/src/`, `core/` = `core/src/`, `tui/` = `tui/src/`)

There are two pipelines. The v1 path (`oc/`) is the one in use. A v2 path (`core/tool-output-store.ts`, `core/src/session/runner`) exists, but nothing in `oc/` imports it.

**1. Shape and limits**
- Shared truncator: `MAX_LINES = 2000`, `MAX_BYTES = 50 * 1024` (UTF-8 bytes) ✔ `oc/tool/truncate.ts:16-17`. It truncates when either limit is exceeded and keeps whole lines.
  - Head by default. A `direction: "tail"` option exists (:26, :90, :103-123), but nothing passes it.
  - Configurable via `tool_output.max_lines` / `tool_output.max_bytes` (:76-84; schema `core/v1/config/config.ts:130-141`). No environment variable.
  - Applies to every `Tool.define` tool whose `metadata.truncated` is undefined (`oc/tool/tool.ts:131-143`), to plugin tools (`oc/tool/registry.ts:175-183`) and to MCP (`oc/session/tools.ts:180`).
- **Shell:** tail, whole lines, same limits (`oc/tool/shell.ts:228-258`, :448, :582). It sets `metadata.truncated`, so it skips the generic truncator. A single line longer than maxBytes keeps its last maxBytes bytes, advanced to a UTF-8 lead byte (:242-248).
- **Read:** head window with paging.
  - `DEFAULT_READ_LIMIT = 2000` lines and `MAX_BYTES = 50 * 1024` ✔, both hard-coded and independent of the `tool_output` config (`oc/tool/read.ts:15-18`).
  - Each line is cut at `MAX_LINE_LENGTH = 2000` chars with `"... (line truncated to 2000 chars)"`.
- **Grep:** `limit = 100` matches; match text cut at 2000 chars plus `"..."` (`oc/tool/grep.ts:11,86-88,102-103`).
- **Glob:** first 100 paths (`oc/tool/glob.ts:53-70`).
- **Webfetch:** refuses above `MAX_RESPONSE_SIZE = 5MB`; the converted text then goes through the generic truncator (`oc/tool/webfetch.ts:9,96-104`).
- **MCP:** generic head truncation of the joined text parts (`oc/session/tools.ts:157-185`).
- **v2 store** (unused): head+tail with a middle marker, cut at code points (`core/tool-output-store.ts:44-98`).

**2. What the model sees**
- Generic ✔ (`oc/tool/truncate.ts:125-138`): `${preview}\n\n...${removed} ${unit} truncated...\n\n${hint}`, where `unit` is `bytes` or `lines`.
  - Hint: `The tool call succeeded but the output was truncated. Full output saved to: ${file}\nUse Grep to search the full content or Read with offset/limit to view specific sections.`
  - If the agent has the Task tool, the hint instead says to delegate to an explore subagent: `Do NOT read the full file yourself - delegate to save context.`
- Shell (`oc/tool/shell.ts:592`): `...output truncated...\n\nFull output saved to: ${file}\n\n` followed by the tail. There are no counts. Timeout and abort are reported in `<shell_metadata>…</shell_metadata>`. Empty output becomes `(no output)`.
- Read (`oc/tool/read.ts:351-362`): content in `<path>/<type>/<content>` with `N: line` numbering. It ends with one of:
  - `(Output capped at 50 KB. Showing lines A-B. Use offset=N to continue.)`
  - `(Showing lines A-B of TOTAL. Use offset=N to continue.)`
  - `(End of file - total N lines)`
- Grep: `Found ${total} matches` [` (more matches available)`] plus `(Results truncated: showing 100 of T matches (X hidden). ...)`.
- Glob: `(Results are truncated: showing first 100 results. ...)`.

**3. Where the full output goes**
- `TRUNCATION_DIR = Global.Path.data/"tool-output"` (`oc/tool/truncation-dir.ts:4`), i.e. `$XDG_DATA_HOME/opencode/tool-output/`. It is global, not per session. Files are named `tool_<ascending id>`.
- `RETENTION = Duration.days(7)` ✔ (`truncate.ts:14`). Cleanup first runs after 1 minute, then hourly (:144-151).
- The full copy is written before the preview is returned. A failed write kills the call (`Effect.orDie`).
- Shell streams to the file once accumulated output exceeds maxBytes. It first writes everything held so far, then appends (`shell.ts:511-535`).
- The session store keeps only the truncated text plus `truncated` and `outputPath` metadata (`oc/session/processor.ts:218-228`).

**4. Getting it back**
- The ordinary Read (offset/limit) and Grep tools on the absolute path. `tool-output/*` is pre-allowed under `external_directory`, and that permission is re-merged into every agent unless an agent explicitly denies it (`oc/agent/agent.ts:96-111,280-293`).
- The path survives resume as text, but the file is deleted after 7 days, leaving a dangling pointer.
- Read, grep and glob never spill. To see more, the model calls again with a larger `offset` or a narrower pattern.

**5. Ageing**
- **Prune:** `PRUNE_MINIMUM = 20_000`, `PRUNE_PROTECT = 40_000`, `PRUNE_PROTECTED_TOOLS = ["skill"]` ✔ (`oc/session/compaction.ts:40-43`, logic :253-296).
  - Off by default (`compaction.prune`, "default: false", `core/v1/config/config.ts:148`). When on, it runs after each prompt loop (`oc/session/prompt.ts:1470`).
  - It skips the last 2 user turns, estimates tokens as length/4, keeps the newest 40k tokens of tool output, and acts only when at least 20k tokens can be removed.
  - Placeholder: `[Old tool result content cleared]` ✔ (`oc/session/message-v2.ts:306`). The original stays in the store, but the placeholder has no path and no tool can retrieve it.
- **Summary compaction:** triggers when tokens ≥ usable context, where usable context is the input limit minus `compaction.reserved`, or minus `min(COMPACTION_BUFFER = 20_000, max output)` when no reserve is set (`oc/session/overflow.ts:8-33`).
  - The summariser's input has tool outputs cut to `TOOL_OUTPUT_MAX_CHARS = 2_000` with `\n[Tool output truncated for compaction: omitted N chars]`.
  - The last `DEFAULT_TAIL_TURNS = 2` turns are kept verbatim (`compaction.ts:42-46`).

**6. UI**
- Shell shows `metadata.output`, a 30k-char streaming preview, not the text the model got. It folds to 10 lines, toggled by mouse click ("Click to expand" / "Click to collapse"), with no key (`tui/routes/session/index.tsx:2039-2087`; `tui/util/collapse-tool-output.ts`).
- Generic and MCP output is hidden by default (`generic_tool_output_visibility` false) and folds to 3 lines when shown.
- Read, grep and glob show a single summary row.
- `outputPath` is never shown in the TUI.

**7. Traps**
- In head mode, a first line larger than maxBytes yields an empty preview containing only the marker (`truncate.ts:104-109`).
- The 30k shell preview `slice(-30000)` and the 2000-char `substring` cuts in read and grep work in UTF-16 units and can split surrogate pairs.
- stdout and stderr are merged (`handle.all`, `shell.ts:498`).
- Grep and glob mark themselves `truncated`, so they bypass the generic limit. 100 matches × 2000 chars is about 200 KB with no spill file. The ripgrep fallback collects every match before slicing.
- MCP images and blobs become base64 attachments with no size check. Content types other than text, image and resource are dropped, and `structuredContent` is ignored (`oc/session/tools.ts:159-175`).

## 2. pi (`quarantine/pi/packages/coding-agent/src/`)

**1. Shape and limits**
- `DEFAULT_MAX_LINES = 2000`, `DEFAULT_MAX_BYTES = 50 * 1024`, `GREP_MAX_LINE_LENGTH = 500` (`core/tools/truncate.ts:11-13`). Whichever of the line or byte limit is hit first wins. These are not user settings; the model passes `limit`/`offset`.
- **bash / powershell:** tail via `OutputAccumulator` + `truncateTail` (`core/tools/output-accumulator.ts:98-126`; `truncate.ts:168-241`).
- **read:** head, whole lines (`read.ts:156`; `truncate.ts:78-160`).
- **grep:** head, bytes only. 100 matches by default, lines cut at 500 chars (`grep.ts:41,282`).
- **find:** 1000 results by default (`find.ts:41`). **ls:** 500 entries (`ls.ts:23`).
- **MCP:** head+tail. `MCP_OUTPUT_MAX_BYTES = 20 * 1024` (`extensions/mcp/tools.ts:46`); `truncateMiddle` splits the budget in half and cuts on character boundaries (`truncate.ts:292-314`).
- **bash `structuredContent.output`** (used by scripts): capped at 1 MiB, kept as 512 KiB + 512 KiB with `\n\n[... ${omitted} bytes omitted ...]\n\n` (`bash.ts:24,56-65`).

**2. What the model sees**
- bash ✔ (`core/tools/bash.ts:347-356`):
  - `[Showing lines ${start}-${end} of ${total}. Full output: ${path}]`
  - `[Showing lines ${start}-${end} of ${total} (50.0KB limit). Full output: ${path}]`
  - `[Showing last ${X} of line ${end} (line is ${size}). Full output: ${path}]`
  - Exit, timeout and abort notes are appended (:375-409).
- read (`read.ts:161-178`):
  - `[Showing lines a-b of N. Use offset=K to continue.]`
  - `[N more lines in file. Use offset=K to continue.]`
  - For a line over 50KB: `[Line L is X, exceeds 50.0KB limit. Use bash: sed -n 'Lp' path | head -c 51200]`
- grep/find/ls: `[100 matches limit reached. Use limit=200 for more, or refine pattern. 50.0KB limit reached. Some lines truncated to 500 chars. Use read tool to see full lines]` (`grep.ts:286-303`).
- MCP: `Warning: truncated output (original token count: ${bytes/4})\nTotal output lines: N\n\n<head>…N chars truncated…<tail>\n\n[Full output: <path> (read it with offset/limit)]` (`extensions/mcp/tools.ts:131-137`).

**3. Where the full output goes**
- `$TMPDIR/pi-bash-<16hex>.log` (or `pi-powershell-…`) (`output-accumulator.ts:26-29`). The file opens only once the limit is exceeded, and earlier buffered chunks are written first (:252-262), so no bytes are lost. Content is raw undecoded bytes.
- User `!` commands write ANSI-stripped text to `pi-bash-<hex>.log` (`bash-executor.ts:69,82`).
- MCP writes `pi-mcp-<hex>.txt` with mode 0600. Binary resources are saved as `[Binary resource uri (type, size) saved to <path>]` (`extensions/mcp/tools.ts:65-70,182-193`).
- Nothing unlinks these files. Omitted grep/find/ls output is not saved.

**4. Getting it back**
- The ordinary read tool on the path, with `offset`/`limit`. The path persists in the session JSONL; the file lives until the OS cleans `$TMPDIR`.
- grep/find/ls: re-run with a higher `limit` or a narrower pattern.

**5. Ageing**
- No pruning of old tool output.
- Summary compaction: `reserveTokens 16384`, `keepRecentTokens 20000`, triggered when `contextTokens > contextWindow - reserveTokens` (`compaction/compaction.ts:127-129,269`).
- The summariser's input cuts each tool result to `TOOL_RESULT_MAX_CHARS = 2000` with `[... N more characters truncated]` (`compaction/utils.ts:94-103,149`).
- The compaction is appended as a new entry, so original results remain in the session file.

**6. UI**
- One global expand toggle, `app.tools.expand` = Ctrl+O (`core/keybindings.ts:117`).
- bash shows the last 5 lines (`BASH_PREVIEW_LINES = 5`) with `... (N earlier lines, ctrl+o to expand)` and a footer `[Full output: path. Truncated: showing X of Y lines]` (`renderers/bash.ts:18,67-80`).
- read shows nothing when collapsed; grep shows 15 lines, find/ls 20, MCP 5.

**7. Traps**
- read's own advice (`head -c 51200`) can split a UTF-8 sequence. Grep's `truncateLine` cuts in UTF-16 units.
- The text the bash tool returns to the model is not ANSI- or CR-cleaned; only the renderer cleans it.
- stdout and stderr are merged in arrival order (`bash.ts:142-143`).
- The rolling tail buffer is trimmed to 2×50KB once it exceeds 4×50KB, and a display snapshot can start mid-line.
- read loads the whole file into memory. Custom and extension tools have no shared limit. MCP images are not limited.

## 3. oh-my-pi (`quarantine/oh-my-pi/packages/`)

**1. Shape and limits**
- Base constants: `DEFAULT_MAX_LINES = 3000`, `DEFAULT_MAX_BYTES = 50*1024`, `DEFAULT_MAX_COLUMN = 512` (`tui/src/tools/streaming-output.ts:10-14`).
- User settings in `coding-agent/src/tools/settings.ts`, all configurable:
  - `tools.artifactSpillThreshold` 50 KB ✔ (:12-16)
  - `artifactHeadBytes` / `artifactTailBytes` 20 KB each
  - `artifactTailLines` 500
  - `outputMaxColumns` 768
  - `read.defaultLimit` 300 lines
- **Shared spill wrapper for every tool, including MCP, extension and RPC tools:** `wrapToolWithMetaNotice` → `spillLargeResultToArtifact` (`coding-agent/src/tools/output-meta.ts:500-680`, applied in `sdk.ts:3292,3383,4404`).
  - Over the threshold, the full text becomes an artifact, and the model gets `truncateMiddle`: 20 KB + 20 KB and 500 + 500 lines.
  - Results already backed by an artifact or a page of one are skipped.
- **bash/eval/ssh `OutputSink`:**
  - Keeps the first 20 KB plus a rolling tail.
  - Lines are cut at 768 bytes with `…`.
  - Middle marker: `[…${n}ln elided…]` / `[…${n}B elided…]`.
  - Final `enforceInlineByteCap` keeps about 60% head and 25% tail (`streaming-output.ts:533-537,691-708,1076-1121`).
  - The bash executor hard-codes the 50 KB budget rather than reading the threshold setting (`coding-agent/src/exec/bash-executor.ts:538-545`).
- **Native shell output minimiser:** on by default, `maxCaptureBytes` 4 MiB. It rewrites command output after saving the original as a `bash-original` artifact, footed `[raw output: artifact://N]` (`exec/settings.ts:180-213`; `bash-executor.ts:760-782`).
- **read:**
  - 300 lines by default, 3000 maximum; byte budget `max(50KB, lines*512)`.
  - A plain whole-file read of code returns a tree-sitter summary with function bodies elided (`read.summarize.enabled` default true; `tools/read.ts:2002-2050,2124-2130`).
- **grep:** native. 20 files per page × 20 matches per file; files over 4 MiB are not fully searched (`tools/grep.ts:72-93`). **glob:** hard maximum 200.

**2. What the model sees** (`tui/src/tools/output-meta.ts:134-136,200-270`)
- `[Showing lines 1-H and T-N of N; K middle lines (B) elided. Read artifact://ID for full output]`
- `[… Use :K to continue. Read artifact://ID ...]`
- From read:
  - `[N more lines in file. Use path:K to continue]`
  - `[More lines in file (S total; not scanned to EOF). Use path:K to continue]`
  - Summary footer: `[…Nln elided; re-read needed ranges with path:a-b,c-d]`
- From grep: `Showing files a-b of N. Use skip=K for the next page, or narrow paths/pattern.`

**3. Where the full output goes**
- A per-session artifact directory, named after the session file minus `.jsonl` (`coding-agent/src/session/session-manager.ts:1704`).
- Files are named `${id}.${tool}.log` with sequential ids (`artifacts.ts:139-144`).
- Writes are atomic: temp file, verify, rename (`artifacts.ts:44-63`).
- Numbering continues across resume. Subagents share the parent's store. Artifacts move or are deleted with the session.
- An optional `artifactMaxBytes` cap (default 0, meaning unbounded) would itself make the artifact head+tail with `[ARTIFACT TRUNCATED: …]`.
- Streamed copies are ANSI/CR-normalised, not raw bytes.

**4. Getting it back**
- The `artifact://N` internal URL (`internal-urls/artifact-protocol.ts`), paged by the read tool with `:N-M` or `:raw:N-M` selectors.
- Full resolution is refused above `MAX_INLINE_ARTIFACT_BYTES = 8 MiB`. A miss reports `Artifact N not found. Available: ...`.
- Artifacts survive resume. Background jobs report `Full output: artifact://N`.

**5. Ageing** (`agent/src/compaction/pruning.ts`; `coding-agent/src/session/session-maintenance.ts`)
- **Every turn:**
  - `SUPERSEDED_NOTICE = "[Superseded by a newer read of this file]"` and `USELESS_NOTICE = "[Uneventful result elided]"` ✔ (`pruning.ts:7,10`). Settings `compaction.supersedeReads` / `dropUseless`, both on by default.
  - Gated to protect the provider's prompt cache: only when the suffix is 8000 tokens or less, or after 90 minutes idle.
- **After each successful turn:** age-based pruning replaces results with `[Output truncated - ${tokens} tokens]` ✔ (`pruning.ts:52`). It keeps the most recent 40k tokens and acts only when it saves at least 20k.
- **These prunes rewrite the session file in place** (`rewriteEntries`). The placeholder has no link, so the original is gone unless an earlier spill artifact exists.
- **"Shake" compaction** is the exception: `[shaken ~N tokens — recover: artifact://ID (region k)]` ✔ (`session-maintenance.ts:1031`). Without an artifact it falls back to `[shaken ~N tokens]` (:1033).

**6. UI**
- Ctrl+O expands (`tui/src/app-keybindings.ts:132-134`).
- bash shows a 10-line preview. MCP shows 4 lines collapsed, still capped at 12 when expanded. Generic results use JSON-tree previews.
- Expanding does not load artifact content.

**7. Traps**
- With no `sessionManager`, the spill wrapper returns the result unbounded (`output-meta.ts:506`).
- Non-persistent sessions write artifacts to an in-memory Map that the `artifact://` resolver never reads, so the advertised links fail.
- The spill merges all text blocks into one and places it after the images, reordering content.
- MCP blob resources are shown to the model only as `[Resource: uri]`.
- stdout and stderr share one pipe.

## 4. gemini-cli (`quarantine/gemini-cli/packages/`)

**1. Shape and limits** (all in `core/src/scheduler/tool-executor.ts:202-406`, after the tool returns)
- **Shell string result, and MCP results with exactly one text part:** head 20% + tail 80% ✔ (`core/src/utils/fileUtils.ts:774-794`).
  - Measured in UTF-16 chars against `DEFAULT_TRUNCATE_TOOL_OUTPUT_THRESHOLD = 40_000` ✔ (`core/src/config/config.ts:482`).
  - Setting `tools.truncateToolOutputThreshold`; 0 or negative disables it (`cli/src/config/settingsSchema.ts:1751-1760`).
- **Everything else** (read_file, read_many_files, grep, glob, multi-part MCP): any string or text part over `MAX_STORED_TOOL_OUTPUT_BYTES = 64 * 1024` ✔ (`core/src/utils/constants.ts:18`) is cut head-only. This cut is byte- and grapheme-safe (`core/src/utils/tool-utils.ts:153-180`) and not configurable.
- **Capture limits before the executor sees anything** (`core/src/services/shellExecutionService.ts`):
  - Child-process path: a 16 MB rolling tail buffer, warning `[GEMINI_CLI_WARNING: Output truncated. The buffer is limited to 16MB.]` (:50, :488-518, :860-865).
  - PTY path: headless terminal with `SCROLLBACK_LIMIT = 50000` lines (:69, :256-307), with no warning.
- **Per-tool limits:**
  - read_file: 2000 lines, 2000 chars per line with `... [truncated]`, refuses files over 20 MB.
  - grep: `DEFAULT_TOTAL_MAX_MATCHES = 100`.
  - glob: no count cap.

**2. What the model sees**
- Head+tail ✔: `Output too large. Showing first {head} and last {tail} characters. For full output see: {path}\n{head}\n\n... [{N} characters omitted] ...\n\n{tail}`.
- 64 KB cut: `\n... [Tool output truncated to conserve memory. For full output see: {path}]` (`tool-utils.ts:162-164`).
- read_file: `IMPORTANT: The file content has been truncated.\nStatus: Showing lines X-Y of T total lines.\nAction: … use start_line: {Y+1}` (`core/src/tools/read-file.ts:175-187`).
- read_many_files: `[WARNING: This file was truncated. To view the full content, use the 'read_file' tool on this specific file.]`
- grep: ` (results limited to 100 matches for performance)`.
- Shell and MCP text are wrapped in `<untrusted_context>`.

**3. Where the full output goes**
- `~/.gemini/tmp/<projectId>/tool-outputs/session-<id>/<tool>_<callId>.txt` (`fileUtils.ts:801-825`). This is the raw string before truncation.
- In the 64 KB path, a failed save is silently ignored (`tool-executor.ts:333-335`).
- Cleanup deletes files older than `maxAge` `'30d'`, and deleting a session removes its folder (`cli/src/utils/sessionCleanup.ts:480-640`).
- Background shell logs go to `<globalTmp>/background-processes/background-<pid>.log`.

**4. Getting it back**
- The ordinary read_file on the path. The project temp dir is a permitted read root (`config.ts:3345-3437`). The model can page with `start_line`/`end_line`.
- Background logs are outside the project temp dir, so read_file cannot reach them; only the shell tool can.
- Truncated responses and masked placeholders are synced into the session record (`core/src/services/chatRecordingService.ts:955+`), so the paths survive resume until the 30-day cleanup.

**5. Ageing**
- **`ToolOutputMaskingService`** runs every turn, with no switch.
  - `DEFAULT_TOOL_PROTECTION_THRESHOLD = 50000` tokens are protected; it fires only when more than `DEFAULT_MIN_PRUNABLE_TOKENS_THRESHOLD = 30000` tokens can be masked; `DEFAULT_PROTECT_LATEST_TURN = true` ✔ (`core/src/context/toolOutputMaskingService.ts:25-27`). Skill, ask-user and plan tools are exempt.
  - It writes `JSON.stringify(response, null, 2)` to `…/<tool>_<callId>_<rand>.txt`.
  - Replacement ✔ (around :360-366): `<tool_output_masked>\n{preview}\n\nOutput too large. Full output available at: {path}\n</tool_output_masked>`. The preview for shell is the first 10 + last 10 lines; for other tools, 250 + 250 chars.
- **Chat compression** at `model.compressionThreshold` 0.5 of the context window (`core/src/context/chatCompressionService.ts`).
  - First, tool responses beyond a 50k-token budget are rewritten with the 40k head+tail and saved as `tool-outputs/<tool>_<counter>.txt`.
  - Then the oldest 70% of history becomes a `<state_snapshot>` summary, which is unrecoverable.
- **Sub-agents only:** `collapseOlderFunctionResponses` keeps 2 KB plus `... [Tool output collapsed from previous turn: N bytes omitted to conserve memory] ...`, with no file saved.
- **Experimental `experimental.contextManagement`** (off by default): `ToolOutputDistillationService` adds an LLM summary of large outputs.

**6. UI**
- Shell and sub-views are capped at 15 lines (`cli/src/ui/constants.ts:48-67`). Tools keep the tail; agents keep the head.
- Marker: `... first N lines hidden (Ctrl+O to show) ...` (`MaxSizedBox.tsx:136-165`). Ctrl+O toggles the last turn's tools.
- A display cap of `MAXIMUM_RESULT_DISPLAY_CHARACTERS = 20000` applies outside alternate-buffer mode, and Ctrl+O does not lift it.

**7. Traps**
- **Splitting.** The 40k head+tail and the 16 MB buffer cut by UTF-16 position; they can split surrogate pairs and never respect line boundaries. Only the 64 KB cut is safe.
- **read_file can't recover long lines.** It cuts at 2000 chars per line with no column offset. That makes masked files, which hold `JSON.stringify` output as a single escaped line, effectively unreadable past about 2000 chars.
- **Masking.** It computes size, line and token counts but never shows them. It also saves the already-truncated history copy, not the original.
- **File clobbering.** Several oversized parts in one result share one `<tool>_<callId>.txt` and overwrite each other under `Promise.all`. Compression files use a per-process counter, so names collide across restarts.
- **Errors bypass truncation.** A shell `result.error` drops the output entirely. An MCP `isError` result puts the whole JSON into the error with no cap (`core/src/tools/mcp-tool.ts:455-468`).
- **Unbounded MCP media.** MCP image, audio and blob content has no cap, and `structuredContent` is ignored.
- **stdout/stderr interleaved.** They share one buffer.
- **Binary output.** Binary detection halts accumulation and saves nothing.
- **Opt-in summariser.** `model.summarizeToolOutput` replaces shell output with an LLM summary, with no full copy saved.
- **Large session JSONL.** The shell `returnDisplay`, up to 16 MB, is recorded untruncated in the session JSONL.

### qwen-code deltas (`quarantine/qwen-code/packages/core/src`)
- **Defaults.** Threshold `25_000` chars plus `DEFAULT_TRUNCATE_TOOL_OUTPUT_LINES = 1000` (`config/config.ts:960-961`).
- **Line-aware split.** Head 1/5 and tail 4/5 of lines, separator `... [CONTENT TRUNCATED] ...`. The header says `The full output has been saved to: {path}\nTo read the complete output, use the read_file tool…`. The file is `<projectTemp>/<tool>_<12hex>.output`, mode 0600 (`tools/truncation.ts:43-170`).
- **Tool-specific budgets.** Shell uses its own 30,000-char budget with a 4000-char preview. MCP text uses a 500,000-char threshold.
- **Second gate after the tool returns.** `persistAndTruncateToolResult` writes `tool-results/<callId>.txt` and emits a stub.
  - Stub: `<persisted-output>` ✔ with the size in KB, `Full output sha256:`, `Note: this file may be cleaned up after 24 hours.` ✔ and a 2000-char preview cut at a newline (`tools/truncation.ts:435-560`).
  - Caps: 50 MB per file and 500 MB per session.
  - Unlike gemini-cli, this gate also applies to oversized errors.
- **Ageing.** There is no masking service. Idle microcompaction runs after `toolResultsThresholdMinutes` (default 60) without an API call. It replaces all but the 5 most recent results with `[Old tool result content cleared]` ✔ (`services/microcompaction/microcompact.ts:14`).
  - The placeholder has no path.
  - It resets the read-file cache so that a re-read is allowed.

## 5. OpenHands (`quarantine/openhands/`: frontend only)

**Backend absent.** Truncation shape, markers, spill location, MCP caps and condenser algorithms cannot be cited from this snapshot.

What the frontend does show:
- **Truncation flag.** Glob and grep observations carry `truncated: boolean`, "Whether results were truncated to 100 files." (`src/types/agent-server/core/base/observation.ts:253-256,284-287`).
- **MCP contract.** `MCPToolObservation` has only `content[]`, `is_error` and `tool_name`, so no truncation signal is defined for MCP.
- **Soft timeout.** Bash `exit_code -1` means a soft timeout; a later observation with an empty `command` continues output from a still-running command (`:62-77`).
- **Ageing contract.**
  - `CondensationEvent` has `forgotten_event_ids` ("removed from the View given to the LLM"), optional `summary` and `summary_offset` (`src/types/agent-server/core/events/condensation-event.ts:5-27`). Events are forgotten from the view, not deleted from the stream.
  - Settings: `condenser.enable_default_condenser` (default true, "LLM-based condenser…") and `condenser_max_size` (`src/types/settings.ts:133-134`).
  - The planning agent gets `{kind:"LLMSummarizingCondenser", max_size: 100, keep_first: 6}` (`src/api/agent-server-adapter.ts:1521-1533`).
  - Manual compaction: `POST /api/conversations/{id}/condense`.
- **UI.**
  - Terminal and MCP display is head-only, `MAX_CONTENT_LENGTH = 1000` chars plus `...` (`src/components/conversation-events/chat/event-content-helpers/shared.ts:3`; `get-observation-content.ts:78-80,147-149`). Browser output gets `...(truncated)`.
  - `CodeBlock` has Expand/Collapse, and copy always takes the full text (`tool-visualizers/primitives/code-block.tsx:41-80`).
  - All of this is display only.

## 6. mini-swe-agent (`quarantine/mini-swe-agent/src/minisweagent/`)

**1–2. Shape and marker.** A single Jinja rule in the observation template; bash is the only tool. ✔ `config/default.yaml:119-140`:
- If `output | length < 10000`, the full output goes in `<output>`.
- Otherwise the model sees a `<warning>`, `output[:5000]` in `<output_head>`, `{{elided_chars}} characters elided` in `<elided_chars>`, and `output[-5000:]` in `<output_tail>`. The unit is Python string characters.
- Warning text: "The output of your last command was too long. / Please try a different command that produces less output. / If you're looking at a file you can try use head, tail or sed to view a smaller number of lines selectively. / If you're using grep or find and it produced too much output, you can use a more selective search pattern. / If you really need to see something from the full command's output, you can redirect output to a file and then search in that file."
- Duplicated in `mini_textbased.yaml` and the swebench/programbench configs. `mini.yaml:113-125` emits JSON instead (`output_head`, `output_tail`, `elided_chars`, `warning: "Output too long."`).
- Configurable only by editing the template.

**3. Full copy.** `extra.raw_output` on the message (`models/utils/actions_toolcall.py:82-93`). It is stripped before the API call (`models/litellm_model.py:77`) and saved in the trajectory JSON after each step (`agents/default.py:101-170`).

**4. Getting it back.** Re-run selectively. Every action is a fresh subshell.

**5. Ageing.** None. History only grows; only the step, cost and wall-time limits stop it.

**6. UI.** No folding.

**7. Traps.**
- stderr is merged into stdout, decoded with `errors="replace"` (`environments/local.py:28-39`). Python string slicing gives no mid-UTF-8 cuts, but cuts can land mid-line.
- Output of exactly 10000 chars shows "0 characters elided", because the test is `< 10000`.
- Capture is unbounded and fully in memory.
- Timeout is 30 s by default. Partial output is kept with `returncode -1`. Docker timeout kills only the local `docker exec` client (`docker.py:115-136`).

## 7. SWE-agent (`quarantine/swe-agent/`)

**1. Shape and limits.**
- Default `max_observation_length: int = 100_000` chars, head-only (`sweagent/agent/agents.py:69-82,729-744`).
- `config/bash_only.yaml:181-201` uses head+tail at 10_000: `observation[:max//2]` … `{{elided_chars}} characters elided` … `observation[-max//2:]`.
- The multimodal configs set it to 10,000,000.
- **str_replace_editor:** `MAX_RESPONSE_LEN = 16000` chars, head-only (`tools/edit_anthropic/bin/str_replace_editor:27-28,680-685`). Python files over 16000 chars viewed without `view_range` get a filemap summary (`USE_FILEMAP: 'true'`).
- **Windowed viewer:** `WINDOW=100` lines, with `[File: {path} ({n} lines total)]`, `({k} more lines above)` and `({m} more lines below)` (`tools/windowed/lib/windowed_file.py:164-174`).
- **Search:** `search_dir` refuses when more than 100 files match; `find_file` has no cap.

**2. Markers.**
- Default: `<response clipped><NOTE>Observations should not exceeded {{max_observation_length}} characters. {{elided_chars}} characters were elided. Please try a different command that produces less output or use head/tail/grep/redirect the output to a file. Do not use interactive pagers.</NOTE>` (`agents.py:70-73`).
- Editor: `<response clipped><NOTE>To save on context only part of this file has been shown to you. You should retry this tool after you have searched inside the file with \`grep -n\`…</NOTE>`.
- Search: `More than $num_files files matched for "$term" in $dir. Please narrow your search.`

**3. Full copy.** The untruncated observation is stored in the `.traj` `TrajectoryStep` (`agents.py:1221-1224`). Model history holds only the templated text.

**4. Getting it back.** Re-run, use `view_range`, `goto`/`scroll`, or search.

**5. Ageing.** History processors work on a copy for every query (`agents.py:539-550`); none are enabled by default (default config uses only `cache_control` with `last_n_messages: 2`). Available processors:
- **`LastNObservations(n, polling)`:** replaces observations other than the last n with `Old environment output: ({num_text_lines} lines omitted)` (`sweagent/agent/history_processors.py:85-176`).
  - It never removes the first (instance) observation.
  - `keep_output` / `remove_output` tags override it.
  - The docstring warns it breaks prompt caching; `polling` mitigates that.
  - Used with `n: 5` in several configs.
- **`ClosedWindowHistoryProcessor`:** `Outdated window with {N} lines omitted...`.
- **`RemoveRegex`:** strips `<diff>…</diff>`.

**6. UI.** The inspector shortens actions to 67 chars; observations are not folded.

**7. Traps.**
- **`search_file`'s 100-line cap never fires.** ✔ It counts `cut -d: -f1` of `grep -nH` output, which is the filename, so `num_lines` is always 1 (`tools/search/bin/search_file:30-44`).
- The editor truncates before line numbering, so the cut can land mid-line.
- On timeout, partial output is discarded and replaced by `The command '{{command}}' was cancelled because it took more than {{timeout}} seconds...`, and the session is interrupted (`agents.py:963-988`). `execution_timeout=30`, `max_consecutive_execution_timeouts=3`.
- stdout/stderr handling lives in SWE-ReX, which is not in the snapshot.

---

## Patterns worth stealing
1. **Spill the full output before truncating, and return a stable handle in the marker.** Examples: opencode's path plus Read/Grep hint, gemini-cli's path, oh-my-pi's `artifact://N`. Write atomically (oh-my-pi's temp → verify → rename). Store the original bytes, not a re-serialised or already-truncated copy.
2. **Make the handle pageable with the ordinary read tool.** oh-my-pi's `artifact://N:a-b` and `:raw` selectors, plus the 8 MiB whole-resolution refusal, are the cleanest version. Pre-authorise the spill directory for reads (opencode `external_directory` allow; gemini project-temp read root).
3. **Put counts and the continuation in the marker.** pi and oh-my-pi state lines a-b of N, bytes elided and the next offset. read tools say `Use offset=K to continue`. Grep says `showing 100 of T (X hidden)` and the next `skip=`.
4. **Scope the store to the session, with lifecycle tied to the session.** oh-my-pi's per-session artifact dir survives resume, continues numbering, moves or deletes with the session, and is shared with subagents. Compare: opencode's global 7-day directory and pi's uncleaned `$TMPDIR` both produce dangling paths.
5. **Shape per tool.** Shell keeps head + tail, because errors are at the end and the command echo or setup is at the start; head-only shell (swe-agent's default) loses the error. Reads page from the head. Search caps by count and pages.
6. **Spill shell output while streaming.** opencode and pi open the file at the threshold and flush the earlier buffer first, so memory stays bounded and nothing is lost. Report capture-layer loss explicitly (gemini's 16 MB warning).
7. **Age with recoverable placeholders.** gemini's `<tool_output_masked>` keeps a preview plus a path; oh-my-pi's shake keeps `recover: artifact://ID`. Protect the newest N tokens and act only when the saving is worth it, as gemini (50k/30k), opencode (40k/20k) and oh-my-pi (40k/20k) all do. Gate ageing on prompt-cache cost (oh-my-pi's idle/suffix gate; swe-agent `polling`).
8. **Prune semantically, not only by age.** Supersede older reads of the same file; elide uneventful results (oh-my-pi). Reset read-dedup caches when content is cleared (qwen).
9. **Make the full output inspectable from the UI.** Collapsed previews with one expand key (Ctrl+O in pi, oh-my-pi and gemini); copy takes the full text (OpenHands).

## Patterns to avoid
1. **Placeholders without a handle.** opencode prune, qwen microcompaction and oh-my-pi age prune leave `[Old tool result content cleared]` / `[Output truncated - N tokens]` with no way back. oh-my-pi also rewrites the session file in place, destroying the original.
2. **Re-running as the only recovery.** mini-swe-agent and swe-agent keep the full output in the trajectory, where the model cannot reach it. Re-running non-deterministic or slow commands is costly or wrong.
3. **Cutting by UTF-16 index or mid-line.** gemini 40k/16 MB, opencode previews, pi grep, read line cuts. Cut at code points or graphemes, prefer line boundaries, and handle a single giant line explicitly (opencode's head mode yields an empty preview).
4. **Line-only retrieval of long lines.** Readers that cut each line at 2000 chars with no column or byte offset (gemini read_file, especially on JSON-stringified masked files) make the "full" copy unrecoverable. Offer byte- or column-range reads of spill files.
5. **Saving after lossy steps.** Summarise or minimise only after saving the original (gemini's summariser saves nothing; oh-my-pi saves `bash-original` first). Do not save the already-truncated history copy (gemini masking).
6. **Path collisions and silent save failures.** gemini's shared `<tool>_<callId>.txt` across parts under `Promise.all`, its per-process counter names, and ignored save errors. On failure, say so in the marker (oh-my-pi: `artifact write failed`).
7. **Errors and non-text content bypassing limits.** gemini MCP `isError` passes JSON uncapped; MCP image, audio and blob content goes uncapped in opencode, pi and gemini; `structuredContent` is ignored in opencode and gemini. Count-limited search that bypasses the byte limit (opencode grep can reach about 200 KB with no spill file).
8. **A limit that depends on optional context.** oh-my-pi's spill is a no-op without a session manager, and its in-memory artifact store is unreadable by its own resolver. Ensure the bound and the way back always exist together.
9. **Merging stdout and stderr irreversibly.** Every reference merges them. Keep a merged interleaved stream for the model if useful, but record the stream of origin in the spill so stderr can be selected later.
10. **Dead or wrong guards.** swe-agent's `search_file` cap never fires; mini-swe-agent's `< 10000` boundary emits "0 characters elided". Test the limit boundaries directly.
