# Prompt composers in terminal coding agents: source study B

This is a read-only study of the snapshots under `/Users/patrickbeam/projects/blackbird/quarantine/`, taken around 2026-09-30. Hermes is the 2026-10-07 snapshot. Nothing was run or built.

- Every claim cites file:line in the snapshot.
- "Library" means the behaviour lives in a dependency that is not vendored (prompt_toolkit, bubbles textarea, rustyline, Textual), so it is stated from the call site only.
- "Inferred" marks a hazard found by reading code, not one reported upstream.

## At a glance

| | Newline keys | Large paste | Unbracketed paste guard | History scope | Reverse search | While busy | Queue recall/edit | Draft stash/persist |
|---|---|---|---|---|---|---|---|---|
| pi | Shift+Enter, Ctrl+J, `\`+Enter; Kitty → modifyOtherKeys → OS-modifier poll | atomic `[paste #N +L lines]`, expands on submit/editor | none | per session, 100, in memory | none | Enter=steer, Alt+Enter=follow-up | Alt+Up pulls all back | none |
| oh-my-pi | same + Ctrl+Q/Ctrl+Enter follow-up | chip/menu (wrap/file/inline) | 2-line-break burst in 10 ms window, stall probe, 1 s/64 MiB end-marker watchdog | global SQLite, UNIQUE + use_count | Ctrl+R FTS5 | steer / follow-up / `->` yield queue, numbered-list split | Alt+Up/Shift+Up | history keeps paste registry; no stash |
| crush | Shift+Enter, Ctrl+J (`\` buggy) | becomes `paste_N.txt` attachment | none | session DB, 200 | none | Enter queues | none; one Esc clears whole queue | none |
| kimi-cli | Alt+Enter, Ctrl+J | `[Pasted text #N +L lines]`, re-folds after editor | none | per cwd JSONL | library | Enter queues, Ctrl+S steers | ↑ pops last queued | modal-suspend only |
| aider | Alt+Enter (swap with `/multiline-mode`), `{tag … tag}` | none | none | per git root file | library Ctrl+R | no composer | n/a | prefill mechanism only |
| hermes | Alt+Enter, Ctrl+J, Shift/Ctrl+Enter aliasing, `\`+Enter | file-backed `[Pasted text #N: K lines → path]` | 50 ms rapid-Enter = newline; 2 s end-marker flush | global file | none (Ctrl+R = dock) | `busy_input_mode` interrupt/queue/steer | Ink TUI only | Ctrl+S stash stack (20) |
| mistral-vibe | Shift+Enter, Ctrl+J | none (paths → `@`) | none | global, merged across sessions | none (Ctrl+R = voice) | queued, merged into one turn; empty Enter steers | Up → select/edit/delete in place | none |
| vtcode | Shift/Alt+Enter, Ctrl+J, `\`+Enter | real text, rendered `[Pasted Content N chars]` | none | session + 20 archived first lines | Ctrl+R fuzzy picker | Enter=steer, Ctrl+Enter/Tab=queue | Alt+Up pops newest | none |
| letta-code | Shift/Alt/Ctrl+Enter, literal `\r` | `[Pasted text #N +L lines]`, holder-aware registry | 128-char/16 ms burst aggregator | per session, 500 | none | Enter queues; Ctrl+D immediate/defer | Up pulls all (race-guarded); Esc parks queue | none |
| goose | Ctrl+J (configurable) | Windows-only chips `[Pasted N lines #id]` | Windows console peek | global file | library | no composer | n/a | `/edit` with conversation template |

---

## pi (TypeScript, own TUI) and oh-my-pi (fork, differences only)

Paths below are relative to `quarantine/pi/packages/` unless prefixed `omp:` (= `quarantine/oh-my-pi/packages/`).

pi has its own editor component, `tui/src/components/editor.ts` (2472 lines). The coding agent subclasses it in `coding-agent/src/modes/interactive/components/custom-editor.ts`. Every key is a named, rebindable action (`tui/src/keybindings.ts:72-160`, app actions `coding-agent/src/core/keybindings.ts:75-145`). The project rule is "Never hardcode key checks" (`pi/AGENTS.md`), although the editor still hardcodes a few, such as `shift+space` and `shift+backspace`.

### 1. Submit and newline
- **Enter** submits (`tui.input.submit: "enter"`, keybindings.ts:144). **Shift+Enter** and **Ctrl+J** insert a newline (`tui.input.newLine: ["shift+enter","ctrl+j"]`, :143).
- The newline branch also accepts raw encodings without a decoded key: an LF that arrives with more data after it, `"\x1b\r"` (Alt+Enter on legacy terminals), `"\x1b[13;2~"`, any ESC+CR combination, and a bare `"\n"` (editor.ts:890-897).
- **Backslash-Enter**: "Workaround for terminals without Shift+Enter support: If char before cursor is \, delete it and insert newline instead of submitting" (editor.ts:911-918). The reverse also exists. If the user has rebound submit to Shift+Enter, then `\`+Enter submits (`shouldSubmitOnBackslashEnter`, :1352-1361).
- **Alt+Enter** is not a newline in the coding agent. It is `app.message.followUp` (queue a follow-up), or `ctrl+q` under Windows/WSL keybindings (core/keybindings.ts:134-136).
- **Terminal setup** (`tui/src/terminal.ts`):
  - Turns on bracketed paste with `\x1b[?2004h` (:187).
  - Sends `\x1b[>7u\x1b[?u\x1b[c`: push Kitty flags 1|2|4, query them, then send a DA1 query as a sentinel (:12-14, :258-265).
  - If DA1 arrives before any Kitty reply, it falls back to xterm `modifyOtherKeys` (`\x1b[>4;2m`, :365-367). This needs no startup timeout. The changelog records the switch: "Fixed inherited intermittent Shift+Enter handling by making Kitty keyboard protocol fallback response-driven instead of timeout-driven (#5188)" (coding-agent/CHANGELOG.md:1535).
  - **Apple Terminal / Windows hack**: when a plain `\r` arrives, pi asks a native helper whether Shift is physically held and rewrites the key to `\x1b[13;2u` (terminal.ts:11, :53-61, :352-361). Changelog: "Fixed Apple Terminal `Shift+Enter` by detecting local macOS modifier state when Terminal.app sends plain Return" (:1672), and "Fixed inherited Windows Shift+Enter detection by reading modifier state from the native Win32 helper" (:796).
  - On exit it pops Kitty with `\x1b[<u` before draining stdin, "so any late key releases do not generate new Kitty escape sequences" (terminal.ts:391-400).
- **omp**:
  - Shift+Enter and Ctrl+J are the same (omp:tui/src/keybindings.ts:126).
  - Follow-up is `["ctrl+q","ctrl+enter"]`, because "Windows Terminal does not emit a distinct event for Ctrl+Enter — Ctrl+Q is listed first … (#1903)" (omp:tui/src/app-keybindings.ts:144-149).
  - A bare word like `exit`, or a known slash-command name without the slash, needs a second Enter: "Press Enter again to run /x; add a leading space to send "x" as a message" (omp:coding-agent/src/modes/controllers/input-controller.ts:1040-1046, :1365-1390).

### 2. Paste
- **Bracketed paste**:
  - The editor buffers between `\x1b[200~` and `\x1b[201~` and replays any bytes after the end marker as input (editor.ts:719-743).
  - It decodes tmux re-encoded CSI-u Ctrl-letters inside the paste, e.g. `ESC[106;5u` becomes LF: "Some terminals (e.g. tmux popups with extended-keys-format=csi-u) re-encode control bytes inside bracketed paste … leaking the printable tail (e.g. "[106;5u") into the editor" (:1266-1276, CHANGELOG #3623).
  - It normalises CRLF/CR to LF, expands tabs to 4 spaces, and strips other control characters (:1147-1149, :1281-1285).
- **Large-paste placeholder**:
  - A paste of more than 10 lines or more than 1000 characters is stored in a `pastes` map and replaced by an atomic marker, `[paste #N +L lines]` or `[paste #N C chars]` (:1300-1315).
  - The marker is a single unit for movement, deletion and wrapping (`segmentWithMarkers`, :41-100).
  - The marker expands **only on submit** (`submitValue` → `expandPasteMarkers`, :1363-1366) and when opening the external editor (`getExpandedText`, :1102).
  - The paste is not editable inside the composer.
  - Backspacing a marker deletes its registry entry and **renumbers** all higher markers (`[paste #3]` becomes `[paste #2]`, :1394-1419).
  - Undo snapshots include the paste registry (`EditorSnapshot`, :224-229) and are deep-cloned (`undo-stack.ts:11-13`). Changelog: "Fixed inherited editor paste registry corruption after deleting and undoing paste markers, preventing literal or mismatched paste markers in submitted prompts (#6844)" (:963), and also #6397 (:1148).
- **File-path paste**: if the paste starts with `/`, `~` or `.` and the character before the cursor is a word character, a space is prepended (editor.ts:1287-1295).
- **Image / file paste** uses **Ctrl+V** (`app.clipboard.pasteImage`; `alt+v` under Windows keybindings), not the terminal paste:
  - Copied Finder files are inserted as paths. Each path is shell-quoted in `!` mode, otherwise the paths are newline-joined. Spaces are added around the insertion as needed.
  - A clipboard image is written to `$TMPDIR/pi-clipboard-<uuid>.<ext>` and **its path is inserted as text**.
  - Plain text is the fallback (interactive-mode.ts:3046-3104).
  - Paths with control characters are rejected (:3070).
- **No unbracketed paste-burst detection in pi.**
- **omp adds**:
  - Raw multiline burst classification in `StdinBuffer` (omp:tui/src/stdin-buffer.ts:85-108, :490-520, :650-700). The first read that contains a line break is held for `RAW_PASTE_CLASSIFICATION_TIMEOUT_MS = 10` (:83). Only "two completed logical line breaks" makes it a paste, because "A single Enter may be batched with surrounding keystrokes in one stdin read".
  - Once DECRQM confirms mode 2004, an unbracketed burst is assumed to be an IME/dictation commit (#13344) unless an event-loop stall probe says typed keys were batched (#12540).
  - A paste-end watchdog: 1000 ms of inactivity or a 64 MiB cap delivers what has arrived so "a lost/corrupted end marker cannot consume unbounded memory" (:29-30, omp:tui/src/bracketed-paste.ts:44-58).
  - It decodes both tmux formats, `ESC[cp;5u` and `ESC[27;5;cp~` (bracketed-paste.ts:6-41).
  - **Paste+Enter in one read**: "A paste-and-Enter burst … must not split into two dispatches: the paste may open an overlay that would then swallow the Enter meant to submit it" (stdin-buffer.ts:606-610). The editor queues the trailing bytes until any async image paste finishes, which "fixes the race where submit runs against an empty `pendingImages`" (omp:tui/src/prompt/custom-editor.ts:1444-1470).
  - A **large-paste menu** above a threshold offers "Attach as a wrapped block" / "Attach as local file" / "Paste inline". Esc falls back to the marker, "so the pasted content is never lost". The menu is skipped when a submit key is already in the burst (omp:coding-agent/src/modes/controllers/input-controller.ts:2478-2505).
  - Text-attachment chips appear as a band card above the editor, alongside host atom tokens like `[Image #1, 800x600]` (omp:tui/src/components/editor.ts:713-725).
  - Raw paste is `ctrl+shift+v` / `alt+shift+v` (app-keybindings.ts:168).
  - Changelog: "Fixed multiline pastes splitting into separate submissions after a terminal drops bracketed-paste mode, and text typed right after Enter being erased by the post-submit clear (#13440)" (omp:coding-agent/CHANGELOG.md:247).

### 3. Editing
- **Emacs keys**:
  - Ctrl+A/E (also Home/End), Ctrl+B/F.
  - Word motion on Alt+B/F, Alt/Ctrl+Left/Right.
  - Ctrl+W and Alt+Backspace delete a word back. Alt+D and Alt+Delete delete a word forward.
  - Ctrl+U/K delete to line start/end (keybindings.ts:82-139).
  - **Ctrl+D** deletes forward, and exits only when the editor is empty (custom-editor.ts:117-125).
- **Kill ring**: real Emacs semantics. Consecutive kills accumulate, backward kills prepend, forward kills append. Kills at a line boundary push `"\n"`. **Ctrl+Y** yanks and **Alt+Y** yank-pops, but only right after a yank (kill-ring.ts; editor.ts:1624-1776, :1997-2029).
- **Undo**:
  - **Ctrl+-** (`ctrl+z` on win32, `alt+z` on WSL) (core/keybindings.ts:77-80).
  - Fish-style coalescing: "Consecutive word chars coalesce into one undo unit … Each space is separately undoable" (editor.ts:1203-1213).
  - `setText` pushes an undo snapshot, so programmatic clears such as Ctrl+C can be undone (:1119-1122).
  - The undo stack is cleared on submit (:1372).
  - **No redo** in pi or omp.
- **Character jump**: **Ctrl+]** then a character jumps forward, **Ctrl+Alt+]** jumps backward, across lines (:698-717, :2137-2165).
- **Wrapped visual rows**:
  - Up/Down move by **visual (wrapped) row** (`buildVisualLineMap`, :1835-1860).
  - They use a sticky preferred column documented as a decision table (:1560-1611).
  - The cursor snaps to the start of an atomic marker it lands inside (:1517-1554).
  - PageUp/PageDown move by 30% of the screen height (:1960-1970).
- **Graphemes and width**:
  - `Intl.Segmenter` grapheme clusters for movement and deletion (:1386-1393, :1910-1933).
  - Word-aware wrapping with CJK break opportunities (:193-208, CHANGELOG #5585).
  - Wide atomic segments are re-wrapped only visually (:172-188).
- **IME**: a zero-width `CURSOR_MARKER` is emitted before the fake reverse-video cursor "so TUI can position the hardware cursor for IME candidate-window placement even while autocomplete … is visible" (:566-569, CHANGELOG #5283).
- **Vim**: none in pi. **omp** adds opt-in `tui.vimMode` (omp:tui/src/vim.ts, 760 lines). From omp:coding-agent/CHANGELOG.md:1315-1319:
  - Normal mode: `hjkl 0 ^ $ w b e gg G`, counts, `x D C dd yy p P u`, and `v`/`V` visual.
  - A showcmd-style pending indicator, e.g. "2d".
  - The cursor shape changes per mode.
  - Escape is handed to the host only when the editor does not need it (`vimConsumesEscape`, omp:tui/src/components/editor.ts:929-936).
- **omp extras**: macOS-spelling autocorrect and replacement candidates (`tui.editor.spellingSuggestions: ctrl+.`), word-completion ghost text, and a space-bar-hold push-to-talk detector. The detector recognises OS auto-repeat because "OS key-repeat is metronomic; a human smashing the bar is fast but irregular" (omp:tui/src/space-hold.ts:1-20).

### 4. History
- **pi**:
  - An in-memory array capped at **100**, most recent first. Consecutive duplicates are dropped (`addToHistory`, editor.ts:427-437).
  - On resume it is filled from the session's user messages (`populateHistory`, interactive-mode.ts:3858-3860), so the scope is **per session**.
  - There is no global history file, no prefix filter and no reverse search.
  - **Up** recalls history only on the first visual row, and only when the editor is empty, already browsing, or the cursor is at column 0. Otherwise the first Up jumps to the line start (editor.ts:925-938). CHANGELOG #5789: "non-empty drafts jump to the start of the line before browsing".
  - **Draft preservation**: the draft is captured in `historyDraft` on entering history and restored when stepping past the newest entry (:462-481, CHANGELOG #5494). Entering history also pushes an undo snapshot.
  - The cursor goes to the start when browsing up and to the end when browsing down (#5454).
  - Optional dedicated `historyPrevious`/`historyNext` bindings are unbound by default (keybindings.ts:74-81).
- **omp**:
  - Global SQLite history with `prompt TEXT NOT NULL UNIQUE`, `cwd`, `session_id` and `use_count`. Duplicates bump `use_count` (`ON CONFLICT(prompt) DO UPDATE`) (omp:coding-agent/src/session/history-storage.ts:58-69, :126-130).
  - An FTS5 prefix-token search overlay on **Ctrl+R** (`app.history.search`, app-keybindings.ts:236; history-storage.ts:103, :234-247).
  - Recalled entries keep their paste and atom registries (`LocalHistoryEntry.draft`, omp:tui/src/components/editor.ts:518-526).

### 5. Mentions and completion
- `CombinedAutocompleteProvider` (tui/src/autocomplete.ts:303+):
  - **Slash** commands open automatically when `/` is typed at the start of the message, first line only (editor.ts:2188-2203).
  - Fuzzy filter on the bare name, then the full name (autocomplete.ts:356-365).
  - `argumentHint` is shown in the list (:346).
  - Per-command `getArgumentCompletions` (:386-390).
- **@file**:
  - Fuzzy, via `fd`, which respects .gitignore and follows symlinks (CHANGELOG #3507). Up to 100 results (autocomplete.ts:148-200, :771-802).
  - Quoted `@"path with spaces"` (:77-116).
  - Opens after wrappers like `(`, `` ` `` (CHANGELOG :137).
  - Default triggers are `@` and `#`. Providers can add characters, but never `/` or whitespace (editor.ts:253, :2340-2351).
  - A 20 ms debounce applies only to attachment triggers (:252, :2353-2361).
- **Tab**:
  - Accepts the selection.
  - With no menu open, it forces file completion. If there is exactly one result it is applied directly (:2265-2284, :2392-2409).
- **Enter on a slash suggestion** applies it and **falls through to submit**. On a file suggestion it only applies (:805-813).
- Stale-result guard: a result is dropped unless the text, line and column all match the snapshot taken at request time (:2415-2429).
- The menu re-queries on cursor movement: "Without this, arrowing left from `/cmd ` back into the command name leaves the argument picker showing … (and a Tab there would concatenate the stale suggestion onto the partial command name)" (:1943-1953).
- **Ghost text**: none in pi. omp has word-completion ghost text accepted with Tab or Right, with accept/diverge feedback to the provider (omp:tui/src/components/editor.ts:553-575, :2196). It also has emoji, GitHub-ref, model-mention and internal-URL completers (omp:tui/src/prompt/*-autocomplete.ts).

### 6. Modes
- **`!` shell**:
  - A draft that begins with `!` switches to bash mode, which is shown **only by the editor border colour** (`theme.getBashModeBorderColor()`) (interactive-mode.ts:3036-3042, :4402-4410).
  - `!!cmd` runs without adding the command to the model's context (:3259-3274; hint text `"!!" "to run bash (no context)"`, :1015).
  - Esc in bash mode clears the draft (:2988-2991).
  - If a command is already running: "A bash command is already running. Press Esc to cancel it first." The text is put back (:3264-3267).
- **Thinking level**: **Shift+Tab** cycles it, and the border colour encodes the level (`getThinkingBorderColor`, :4406-4408).
- The working status can be drawn inside the editor's top border (custom-editor.ts:36-79).
- Plan mode: **not in pi's snapshot**. omp has `app.plan.toggle: alt+shift+p` (app-keybindings.ts:232) and a Python mode, also shown by border colour (input-controller.ts:568-571).

### 7. While busy
- **Enter while streaming = steer**: the message is injected into the running turn (`session.prompt(text, {streamingBehavior:"steer"})`, interactive-mode.ts:3289-3297).
- **Alt+Enter = follow-up**, delivered after the turn ends. When idle it behaves like Enter (:4359-4389).
- During compaction both are queued locally (:3277-3287, :4648-4655).
- Queued items are shown as dim `Steering: …` / `Follow-up: …` rows with the hint `↳ Alt+Up to edit all queued messages` (:4608-4625).
- **Alt+Up (`app.message.dequeue`)** pulls *all* queued messages back into the editor, joined by blank lines and placed above the current draft (:4627-4646).
- **Esc while streaming**: restores the queued messages to the editor **and** aborts (:2983-2984).
- **Ctrl+C**: first press clears the editor (undoable through Ctrl+-). A second press within 500 ms exits (:4161-4169).
- **Double Esc** on an empty editor within 500 ms opens `/tree` or the fork selector, depending on settings (:2992-3006).
- **omp**:
  - Esc never touches a non-empty idle draft: "Esc must not destroy an in-progress draft." (input-controller.ts:575-577).
  - Esc has an explicit owner priority: mcp test, side panels, compaction/handoff/retry, TTS, loop mode, focused subagent, collab, turn (:455-600).
  - Typing a draft that starts with `->` or `=>` queues it for after the turn ("yield queue"). A numbered list (1. 2. / a) b) / Roman) is split into several queued messages (omp:tui/src/prompt/queue-input.ts:1-60; input-controller.ts:1030-1037, :1741).
  - `Ctrl+Q`/`Ctrl+Enter` sends a follow-up, and `Alt+Up`/`Shift+Up` dequeue (app-keybindings.ts:144-160).

### 8. Drafts
- **pi**:
  - No stash and no persistence of the draft across a crash or exit.
  - The external editor is **Ctrl+G**. It writes the *expanded* text to `$TMPDIR/pi-editor-*/prompt.md`, prints "Launching external editor: … Pi will resume when the editor exits.", and stops the TUI. A non-zero exit means **cancel** (the draft is unchanged). One trailing `\n` is stripped (external-editor.ts:14-45; interactive-mode.ts:4481-4496).
  - Using async `spawn` instead of `spawnSync` is deliberate: "On Windows, synchronous child_process calls can keep Node/libuv's console input read active … racing vim/nvim for the console input buffer" (:22-24).
- **omp**: also not in snapshot for crash recovery. It has `clearDraft(historyText)` and re-anchors the history draft.

### 9. Traps and gotchas (quoted)
- Shift+Enter needed four mechanisms: Kitty, modifyOtherKeys, polling the OS modifier state for Terminal.app/Windows, and the backslash fallback. The changelog lists it again and again (#5188, :796, :1672).
- "Fixed split `Alt+Enter` input over SSH being misread as Escape, added `PI_TUI_ESC_TIMEOUT` for high-latency terminals, and limited that timeout to lone Escape input (#7899)" (coding-agent/CHANGELOG.md:572).
- "Fixed xterm uppercase typing … so `Shift+letter` no longer disappears" under modifyOtherKeys (#3436, :2259).
- Paste markers corrupted after delete + undo (#6844, #6397), as above.
- "Fixed editor rendering artifacts for Thai Sara Am and Lao AM vowel characters (#3904)" (:1997).
- "Fixed idle follow-up submission to clear the editor like normal message submission (#3926)" (:1996).
- omp: "Fixed Ctrl+D quitting the prompt even with draft text" (omp:coding-agent/CHANGELOG.md:884). Also "Fixed the composer stranding blank rows below the input after … tall multi-line editor collapses (#11007)" (:861). Also "Fixed rewinding (`/rewind`, `/tree`) during a running turn hiding the queued-prompt bar, making the still-pending queue look deleted and uneditable (#13680)" (:72).
- **Code-reading hazards (inferred, not reported upstream)**:
  - (a) `restoreQueuedMessagesToEditor` combines the queue with `this.editor.getText()`, which still has unexpanded markers, and then calls `setText`. `setText` runs `pastes.clear()` (editor.ts:1123), so Esc-while-streaming or Alt+Up turns a draft's `[paste #1 +40 lines]` into dead literal text. Undo can restore it (interactive-mode.ts:4637-4640).
  - (b) Paste ids restart at 1 after every submit (:1368-1369), but the kill ring survives. Yanking an old marker into a new prompt that has its own `#1` expands to the *new* content.
  - (c) Typing the literal text `[paste #1 +12 lines]` while paste 1 exists expands it, because the replacement is a global regex (:1089-1096).
  - (d) The sticky "visual column" is measured in UTF-16 code units, not cells (`cursorCol - startCol`, :1490-1495). Vertical movement between CJK and ASCII rows therefore drifts.
  - (e) The `$EDITOR` command is split on spaces (`options.command.split(" ")`, external-editor.ts:19), so a quoted path or argument breaks.
  - (f) `submitValue` trims the whole prompt (editor.ts:1365), which loses intentional leading indentation, for example in a pasted code block.
  - (g) The Ctrl+V text fallback uses `insertTextAtCursor` and so bypasses large-paste collapsing (interactive-mode.ts:3097-3101).

---


## crush (Go, bubbletea v2 + bubbles v2.2.1 textarea)

Composer lives in `crush/internal/ui/model/ui.go` (6236 lines), `keys.go`, `history.go`. The textarea itself is `charm.land/bubbles/v2 v2.2.1` (`crush/go.mod:6`), not vendored.

### 1. Submit and newline
- `enter` = send (`keys.go:139-142`). Newline = `shift+enter` or `ctrl+j` (`keys.go:147-153`); help text defaults to `ctrl+j` and is swapped to `shift+enter` only when the terminal reports key disambiguation: `case tea.KeyboardEnhancementsMsg: ... if msg.SupportsKeyDisambiguation() { ... m.keyMap.Editor.Newline.SetHelp("shift+enter", "newline") }` (`ui.go:1147-1152`). Capability detection is the bubbletea kitty-keyboard enhancement reply; no explicit terminal list.
- Backslash-Enter: `if before, ok := strings.CutSuffix(value, "\\"); ok { // If the last character is a backslash, remove it and add a newline. m.textarea.SetValue(before) ... break }` (`ui.go:3170-3178`). Note: the code strips the backslash but never inserts `'\n'`, and it tests the end of the whole buffer, not the cursor (see traps).
- No Alt-Enter binding.
- `tea.TerminalVersionMsg` is used only to enable the progress bar for `"ghostty", "iterm2", "rio"` (`ui.go:1127-1133`).
- Typing exactly `exit` or `quit` and Enter opens the quit dialog instead of sending (`ui.go:3185-3187`).

### 2. Paste
- Bracketed paste arrives as `tea.PasteMsg` (`ui.go:1422-1433`), CRLF normalised first (`ui.go:5879`).
- Large paste: `pasteLinesThreshold = 10` lines or `pasteColsThreshold = 1000` columns (`ui.go:75-79`, `hasPasteExceededThreshold` `ui.go:5940-5955`) turns the paste into a **file attachment** named `paste_%d.txt` with sniffed MIME, cap 5 MB (`"Paste is too big (>5mb)"`) (`ui.go:5890-5906`). It is not a textual placeholder: it is shown in the attachment row, cannot be expanded or edited in place, and is deleted with `ctrl+r` then index or `ctrl+r r` for all (`keys.go:178-189`).
- File-path drops: `fsext.ParsePastedFiles(msg.Content)`; only if **every** path exists **and** is an image does it attach them; otherwise the paste is literal text (`ui.go:5908-5934`).
- Image paste: `ctrl+v` = `PasteImage` (`keys.go:158-161`) reads clipboard image, else treats clipboard text as a path (unescaping `"\\ "`) (`ui.go:6005-6050`). `ctrl+shift+v` = text paste "as an alternative to bracketed paste", which synthesises a `tea.PasteMsg` so it goes through the same pipeline (`keys.go:35-37`, `ui.go:5991-6001`).
- A paste that begins with `!` enters bang mode (`checkBangModeAfterPaste`, `ui.go:5858-5874`).
- No paste-burst heuristic for terminals without bracketed paste: not in snapshot.

### 3. Editing
- Textarea configured with `DynamicHeight`, `MinHeight = 3`, `MaxHeight = 15`, virtual cursor off (real terminal cursor, which helps IME placement) (`ui.go:469-476`, `ui.go:97-105`).
- `ctrl+a` kept as line start, select-all moved to `ctrl+shift+a`; textarea built-in copy disabled so crush's clipboard backend is used (`ui.go:477-489`). `ctrl+shift+c` / `ctrl+shift+x` copy/cut selection (`keys.go:190-201`); mouse drag selection inside the textarea (`ui.go:4175-4202`).
- Word motion, kill keys, undo, wrapped-row movement, grapheme handling: library (bubbles textarea); none customised in snapshot. `github.com/rivo/uniseg` and `clipperhouse/displaywidth` are dependencies (`go.mod:36,57`). No vim/emacs modes. No undo in the composer in snapshot.

### 4. History
- Source is the session database, not a file: `ListUserMessages(session)` if a session exists, else `ListAllUserMessages` (`history.go:20-46`). Comment: "Both queries stop at the 200 most recent entries". Shell commands are folded in as `"!"+sc.Command` (`history.go:40-42`), and recalling one re-enters bang mode via `syncBangModeFromTextarea` (`history.go:115-133`).
- Up enters history only when empty or at (0,0); on line 0 elsewhere it first moves the cursor to start (`history.go:50-68`). Down symmetrical from end of buffer (`history.go:71-89`).
- Draft: saved when leaving index -1 (`history.go:139-141`); `esc` returns to the draft (`history.go:92-104`); **any edit of a recalled entry makes it the new draft and resets the index** (`history.go:107-112`, called at `ui.go:3350-3351`), so history entries are never mutated.
- No prefix filtering, no reverse search (`ctrl+r` is attachment-delete mode), no dedup.
- History reload after every send resets `draft = ""` (`ui.go:987-990`, `ui.go:3221`).

### 5. Mentions and completion
- `@` opens a file completion popup only at start or after whitespace (`ui.go:3297-3310`); filter is a list filter re-sorted by `namePriorityTier` (`completions/completions.go:210-249`); files from `fsext.ListDirectory(".", nil, depth, limit)` with config limits (`completions.go:407-408`). Select = `enter`, `tab`, `ctrl+y`; `ctrl+n`/`ctrl+p` = insert next/previous (`completions/keys.go:20-45`). Space closes the popup (`ui.go:3362-3364`). Choosing a file also attaches it, skipped if already read and unmodified (`ui.go:4737-4760`). MCP resources also complete (`ui.go:3125-3129`).
- `/` on an empty composer opens the command palette dialog (`ui.go:3279-3282`; help `"/ or ctrl+p"` `ui.go:3767-3769`), not inline completion. Argument entry goes through `dialog/arguments.go` (not studied in depth).
- No ghost text.

### 6. Modes
- Bang mode: typing `!` at the start (optionally after whitespace) strips the `!` and switches the prompt function and placeholder to `"Run a shell command"`; backspace on an already-empty bang prompt exits (`ui.go:3289-3348`, `ui.go:1558-1559`, `ui.go:4521-4535`).
- `shift+tab` cycles code -> plan -> (YOLO code) with careful rules about which YOLO the cycle may turn off (`ui.go:4602-4622`); `ctrl+y` toggles YOLO (`keys.go:118-121`). Shown by a prompt badge function per mode and placeholder `"Let's plan"` / `"Go crazy"` (`ui.go:1553-1572`). Mode switch refused while busy: `"Agent is busy, please wait before switching input mode..."`. Enter during switching: `"Switching input mode, one moment..."` (`ui.go:3164-3166`).

### 7. While busy
- Typing is always allowed; Enter while busy enqueues in the agent ("either starts a run or is enqueued behind one", `ui.go:5117-5124`). Pill `"%d Queued"` with expandable list (`pills.go:59`, `pills.go:334-335`). Queued items cannot be edited or recalled individually in snapshot.
- `esc` while busy: if `isCanceling` cancel; **else if queue > 0 clear the whole queue** (`AgentClearQueue`); else arm a 2 s double-press (`cancelTimerDuration = 2 * time.Second`) (`ui.go:5285-5346`). Help text switches between `"clear queue"` and `"press again to cancel"` (`ui.go:3776-3782`). This check runs before editor key handling (`ui.go:3097-3105`), so `esc` cannot return to the history draft while busy.
- `ctrl+c` always opens the quit dialog (`keys.go:96-99`, `ui.go:3046-3053`). `ctrl+z` suspend refused while busy (`ui.go:3020-3025`). `ctrl+o` editor refused while busy: `"Agent is working, please wait..."` (`ui.go:3231-3235`).
- Draft survives cancel (cancel does not touch the textarea). No steering into the running turn.

### 8. Drafts and external editor
- `ctrl+o`: writes `msg_*.md` temp file, opens `$EDITOR` **at the cursor position** via `editor.AtPosition(line+1, col+1)` (`ui.go:4478-4495`); empty result -> `"Message is empty"` and the composer is left unchanged; content is `TrimSpace`d; temp removed (`ui.go:4497-4517`). Bang mode is round-tripped by prefixing `"!"` before and re-parsing after (`ui.go:3236-3239`, `ui.go:1434-1438`).
- No draft persistence across exit or crash in snapshot.

### 9. Traps and gotchas
- Backslash-Enter strips `\` but inserts no newline, despite the comment "remove it and add a newline" (`ui.go:3170-3178`); and it tests `value` suffix, not the char before the cursor.
- `@` trigger uses `curIdx := len(curValue)` (end of buffer) rather than the cursor, so `@` typed mid-text checks the wrong preceding character and `completionsStartIndex` is wrong (`ui.go:3298-3306`, close test `ui.go:3360`).
- A single `esc` while busy silently discards every queued prompt (`ui.go:5326-5337`).
- Bare `exit`/`quit` cannot be sent as a prompt (`ui.go:3185-3187`).
- `isAgentBusy` reads a memoized value: comments explain that a probe would be "an HTTP round-trip per keystroke in client/server mode" (`ui.go:4862-4867`) and send optimistically sets busy so "esc pressed right after enter" routes correctly (`ui.go:5117-5124`). The composer depends on cache-generation counters to avoid stale state.
- Paste threshold counts bytes (`len(line)`) as "columns" (`ui.go:5949`), so CJK text trips it about three times earlier.

---

## kimi-cli (Python, prompt_toolkit)

Composer: `kimi-cli/src/kimi_cli/ui/shell/prompt.py` (2259 lines), `placeholders.py`, `visualize/_interactive.py`, `visualize/_input_router.py`, `__init__.py`.

### 1. Submit and newline
- Enter submits (prompt_toolkit default, `multiline` not set). Newline: `@_kb.add("escape", "enter", eager=True)` + `@_kb.add("c-j", eager=True)` -> `insert_text("\n")` ("Insert a newline when Alt-Enter or Ctrl-J is pressed.") (`prompt.py:1322-1329`). No Shift-Enter, no backslash-Enter, no kitty-keyboard negotiation.
- Enter with a slash completion menu open accepts **and submits** in one step; with a file-mention menu it only accepts (`prompt.py:1276-1285`; CHANGELOG line 169).
- Toolbar tips rotate: `"ctrl-x: toggle mode"`, `"shift-tab: plan mode"`, `"ctrl-o: editor"`, `"ctrl-j: newline"`, `"ctrl-v: paste clipboard"`, `"@: mention files"` (`prompt.py:1162-1177`).
- `ensure_tty_sane()` restores termios before each prompt "so Ctrl-C works after raw-mode operations" (`utils/term.py:28-29`, `__init__.py:332`).

### 2. Paste
- Own `Keys.BracketedPaste` handler (`prompt.py:1483-1485`) -> `normalize_pasted_text` (CRLF/CR -> LF) -> placeholderise in agent mode only (`prompt.py:1899-1909`).
- Placeholder: `[Pasted text #N]` or `[Pasted text #N +L lines]` (`placeholders.py:68-72`) when `>= 1000` chars or `>= 15` lines, env-tunable `KIMI_CLI_PASTE_CHAR_THRESHOLD` / `KIMI_CLI_PASTE_LINE_THRESHOLD` (`placeholders.py:34-35`). Expanded **on submit** (`resolve_command`, `placeholders.py:455-486`); expanded when written to history (`serialize_for_history`, `placeholders.py:307-308`, used at `prompt.py:2077-2079`); expanded for the external editor and **re-folded afterwards** by diffing with `SequenceMatcher` and re-inserting a token only where the pasted span survived unchanged (`placeholders.py:313-389`). The token is plain buffer text, so editing one character inside it breaks the match and it is sent literally.
- `ctrl+v`: tries media first (images cached to disk as `[image:<id>,WxH]`, `placeholders.py:394-398`; non-image files inserted as paths, `shlex.quote`d in shell mode), then text (`prompt.py:1487-1501`, `1911-1961`). Images refused with `"Image input is not supported by the selected LLM model"` when the model lacks `image_in`. Headless Linux falls back to `xclip`/`wl-paste` (`utils/clipboard.py:44-53`).
- On submit, null bytes stripped and lone UTF-16 surrogates replaced (`prompt.py:2052-2055`, `placeholders.py:38-46`).
- No burst detection without bracketed paste; drag-drop path detection: not in snapshot.

### 3. Editing
- prompt_toolkit emacs defaults; no vi mode option (`EditingMode` absent from `src`). Kill ring via prompt_toolkit clipboard; comment: "PromptSession built-in keybindings (ctrl-k, ctrl-w, ctrl-y) use clipboard without error handling, so a broken clipboard object would crash the UI." so `PyperclipClipboard` is used only when it works (`prompt.py:1503-1507`). Undo (`c-_`), word motion, wide characters: library. Grapheme clusters: prompt_toolkit uses `wcwidth` per code point (library); no custom handling.
- Completion is retriggered on every text change including backspace (`prompt.py:1532-1538`; CHANGELOG 863 "Fix completion after backspacing").

### 4. History
- Per working directory: `share_dir/user-history/<md5(cwd)>.jsonl` (`prompt.py:1196-1199`), JSON lines with corrupt-line skipping (`prompt.py:810-845`).
- Consecutive dedup only (`prompt.py:1232-1235`, `2084-2086`). Pasted placeholders stored expanded; image tokens stored raw.
- Prompts submitted while a turn is running are **not** written to the history file: `append_history = not was_running` (`prompt.py:2059-2062`).
- No prefix filtering (`enable_history_search` unset). Ctrl-R reverse search: library default only.

### 5. Mentions and completion
- `@` fuzzy file completion (`LocalFileMentionCompleter`, `prompt.py:626-803`): ignored if the char before `@` is alphanumeric or one of `. - _ \` ' " : @ # ~` (`_TRIGGER_GUARDS`, `prompt.py:636`, `741-758`); fragments under 3 chars without `/` list only the top level; deeper queries use `git ls-files --recurse-submodules` (5 s timeout) with cache invalidation on `.git/index` mtime ("like Claude Code"), else `os.walk` with a 1000 limit (`prompt.py:697-738`; CHANGELOG 179 "fixes #1375"); results re-ranked basename-prefix, basename-contains, other (`prompt.py:786-801`). No completion once the fragment already names a file (`prompt.py:760-768`).
- Slash: fuzzy over names **and aliases**, shown `/name (alias)`, only when `/` begins the buffer and nothing follows the cursor (`prompt.py:89-172`); replaced default popup with a full-width custom menu showing multi-line descriptions (`prompt.py:343-623`, `1545-1589`); the bottom toolbar hides while it is open (`prompt.py:2100-2105`). No argument hints, no ghost text (`auto_suggest` absent).

### 6. Modes
- `ctrl+x` toggles agent/shell (`prompt.py:1287-1298`); shell mode shows a plain `─` separator and `$ ` (`PROMPT_SYMBOL_SHELL = "$"`, `prompt.py:80`, `1618-1646`) and swaps completer to shell slash commands. No `!` prefix mode.
- `shift+tab` toggles plan mode with toast `"plan mode ON"` / `"plan mode OFF"` (`prompt.py:1300-1320`). The input header reads `── input ───` normally and `╌╌ input · plan ╌╌` in blue dashes in plan mode, and appends `· N queued` (`prompt.py:1786-1820`). Mode toggles ignored while a running-prompt delegate is attached.

### 7. While busy
- The prompt stays live during a turn; agent output renders above it (`_PromptLiveView`, `_interactive.py:54-63`).
- Enter -> queue (`classify_input`, `_input_router.py:31-48`). Queued items listed above input with hint `"↑ to edit · ctrl-s to send immediately"` (`_interactive.py:389-398`). `↑` on an empty buffer pops the **last** queued message back into the buffer for editing (`_interactive.py:434-455`). `ctrl+s` steers: buffer text if any, else the **oldest** queued item, injected into the running turn (`_interactive.py:457-470`, `315-353`). Shell-only slash commands refused while streaming: `"/{name} is not available during streaming"` (`_interactive.py:285-298`). `/btw <q>` runs a side question in a modal (`_input_router.py:39-43`).
- Interrupt: `escape` cancels the turn when a cancel event exists (`_interactive.py:432-433`); `ctrl+c` raises `KeyboardInterrupt` from `prompt_async` and calls the running interrupt handler (`__init__.py:336-347`); idle `ctrl+c` prints `"Tip: press Ctrl-D or send 'exit' to quit"` (`__init__.py:594-596`). No double-press exit.
- Draft: a modal that hides input stashes the buffer document and restores it if the buffer is still empty, else logs "Dropping suspended buffer document because buffer was modified externally" (`prompt.py:1741-1768`). Unsubmitted text survives turn end (CHANGELOG 320). On error or cancel, queued messages are printed `"Queued message dropped: …"` (`__init__.py:1031-1037`).
- Background auto-triggered turns wait while the user is typing: `_BG_AUTO_TRIGGER_INPUT_GRACE_S = 0.75` and pending text blocks them entirely (`__init__.py:77`, `1043-1060`). CHANGELOG 189: "to avoid stealing the prompt or breaking CJK IME composition".

### 8. Drafts and external editor
- `ctrl+o` -> `run_in_terminal(edit_text_in_editor)` with configured editor, `$VISUAL`, `$EDITOR`, then auto-detect `code --wait`, `vim`, `vi`, `nano` (`prompt.py:1648-1674`, `utils/editor.py:16-29`); temp `kimi-edit-*.md`; no cursor position passed. `/editor` sets the default. `"No editor found. Set $VISUAL/$EDITOR or run /editor."` toast.
- `set_prefill_text` one-shot default text (`prompt.py:1963-1970`), used by `/undo` fork to pre-fill a past message (CHANGELOG 201).
- No crash recovery of drafts in snapshot.

### 9. Traps and gotchas
- CHANGELOG 209: thresholds made tunable because "lowering these thresholds works around CJK input method breakage after multiline paste on some terminals (e.g., XShell over SSH)".
- CHANGELOG 313: thresholds raised from 300 chars / 3 lines to 1000 / 15 "making voice/typeless workflows less disruptive" (dictation tools arrive as pastes).
- CHANGELOG 321: "Fix Ctrl-C and Ctrl-D not working correctly after an agent run completes — keyboard interrupts and EOF were silently swallowed".
- CHANGELOG 333 and 752: lone UTF-16 surrogates from the Windows clipboard crashed history writes.
- CHANGELOG 247: paste placeholders were not expanded in approval/question modal answers.
- CHANGELOG 169: Enter on completion used to need two presses; re-completion after accept caused stale menus, so `_suppress_auto_completion` (`prompt.py:1254-1263`).
- Inferred, not verified: the `escape` running-prompt binding is `eager=True` (`prompt.py:1428-1433`), so during a turn Alt-Enter (ESC CR) can fire cancel before the `("escape","enter")` newline binding. `ctrl+j` is the safe newline.
- Inferred from prompt_toolkit defaults: `ctrl+c` discards the buffer (app exits with `KeyboardInterrupt`), while `escape` keeps it.
- Queue `↑` pops LIFO while `ctrl+s` takes FIFO: two different ends of the same queue.
- Messages typed during a turn are missing from the persisted history (`prompt.py:2059-2062`).

---

## aider (Python, prompt_toolkit)

Composer: `aider/aider/io.py` (`get_input` at `io.py:523-734`), `commands.py`, `copypaste.py`, `editor.py`, `coders/base_coder.py`.

### 1. Submit and newline
- Normal mode: Enter submits, `escape enter` (Alt/Meta-Enter) inserts newline. `--multiline` or `/multiline-mode` swaps them (`io.py:612-634`, `args.py:819-823`, `commands.py:1524-1526`); toggle messages `"Multiline mode: Enabled. Enter inserts newline, Alt-Enter submits text"` (`io.py:1105-1115`). Prompt prefix becomes `multi> ` (`io.py:548-552`). In multiline + vi mode, Enter in NAVIGATION mode submits (`io.py:615-621`; HISTORY 293).
- Brace blocks: a line that is exactly `{` or `{tag` starts multi-line collection until `}` or `tag}` (`io.py:694-731`; HISTORY 597). Each line is a separate `prompt()` call with the continuation prefix.
- No Shift-Enter, Ctrl-J or backslash handling. `ctrl+space` inserts a space ("Ignore Ctrl when pressing space bar", `io.py:582-585`).
- Capability detection: `is_dumb_terminal()` disables prompt_toolkit entirely and falls back to `input()` (`io.py:330-338`, `io.py:672`; HISTORY 466); `--no-fancy-input` does the same (`args.py:813-817`). `NO_COLOR` disables pretty output (`io.py:278-280`).

### 2. Paste
- Bracketed paste: library default (no custom handler). No placeholders; pasted text goes straight into the buffer.
- `/paste [name]`: clipboard image saved as PNG/JPEG to a temp dir and **added to chat as a file**, replacing a same-named one; otherwise clipboard text is returned and printed (`commands.py:1278-1322`).
- Clipboard watcher (`--copy-paste` mode): polls `pyperclip.paste()` every 0.5 s; on change calls `interrupt_input()` and sets the placeholder to the clipboard text, wrapping multi-line text in newlines (`copypaste.py:20-35`).
- URL in a prompt triggers an offer to scrape it (HISTORY 1146). File-path drop detection: not in snapshot.

### 3. Editing
- `--vim` sets `EditingMode.VI` with `ModalCursorShapeConfig()` cursor shapes (`args.py:742-746`, `io.py:350-353`; HISTORY 730). Emacs otherwise. Word motion, kill ring, undo: library defaults. `PygmentsLexer(MarkdownLexer)` highlights the composer (`io.py:349`).
- `ctrl+z` suspends to background when `SIGTSTP` exists (`io.py:577-580`; HISTORY 545).
- Wide chars, graphemes, IME: library; nothing custom.

### 4. History
- Per git root: `.aider.input.history` (`args.py:271-281`), prompt_toolkit `FileHistory` (`io.py:354-356`) which appends on accept and never deduplicates. Shared by every aider run in that repo.
- `ctrl+up` / `ctrl+down` navigate history whole-message even inside multi-line text (`io.py:587-595`; HISTORY 557); plain up/down move by line first (library).
- Reverse search: library emacs `ctrl+r`; the custom Enter bindings use `filter=~is_searching` so Enter during search still works (`io.py:612`, `626`). No prefix filtering.
- `/run` commands recorded as `/run <cmd>` (`base_coder.py:2474`).

### 5. Mentions and completion
- `ThreadedCompleter(AutoCompleter)` with `complete_while_typing=True`, `MULTI_COLUMN` menu, `reserve_space_for_menu=4` (`io.py:558-567`, `657-666`).
- Words come from in-chat and addable file names, basenames mapped to full relative paths, and **Pygments-tokenised identifiers from in-chat files** inserted in backticks (`io.py:127-146`). Prefix match, case-insensitive, only after 3 typed characters, never after a trailing space (`io.py:186-232`; HISTORY 319).
- `/` commands: prefix completion of names, then per-command argument completion (`completions_<cmd>` lists or `completions_raw_<cmd>` completers, e.g. `/read-only` whole-filesystem paths) (`io.py:148-184`, `commands.py:702`). No `@` mentions; files are added with `/add`. No ghost text.

### 6. Modes
- `!cmd` runs a shell command (`commands.py:313-315`); there is no visual mode, it is just a prefix. On non-zero exit with output added, the next prompt is pre-filled `"What's wrong? Fix"` (`commands.py:1049-1050`).
- Chat modes `/ask`, `/code`, `/architect`, `/context`, `/chat-mode` (`commands.py:138`, `1182-1196`); shown by the prompt prefix carrying the edit format, e.g. `ask> ` or `architect multi> ` (`io.py:546-552`).

### 7. While busy
- No composer during a turn: `get_input` is only called between turns (`base_coder.py:882-890`). Type-ahead is whatever the tty buffers.
- `ctrl+c` during streaming interrupts the response (`base_coder.py:1489-1491`). At the prompt, first `ctrl+c` prints `"^C again to exit"`; a second within 2 s exits (`base_coder.py:986-1000`; HISTORY 1445). The draft is discarded (library behaviour on Ctrl-C).
- `ctrl+d` exits (HISTORY 525).
- File watcher (`--watch-files`, "AI!" comments) calls `interrupt_input()`, which saves `current_buffer.text` into `placeholder` and exits the prompt; the next prompt starts with that text as `default` (`io.py:516-521`, `640-646`, `watch.py:142`).

### 8. Drafts and external editor
- `ctrl+x ctrl+e` opens `$EDITOR` on the buffer (suffix `.md`), strips trailing newlines, cursor to end (`io.py:597-610`; HISTORY 250). `/editor` (alias `/edit`) composes in the editor and pre-fills the result into the prompt so it can start with a `/command` (`commands.py:1569-1578`; HISTORY 580). Defaults `vi` / `vim` (macOS) / `notepad` (`editor.py:20-22`).
- `placeholder` is aider's general prefill mechanism (voice transcript `commands.py:1276`, `/run` failure, file-watcher interrupt, `/editor`). No draft persistence across exit.

### 9. Traps and gotchas
- Clipboard watcher overwrites a draft: `interrupt_input()` stores the buffer in `placeholder`, then the watcher immediately does `self.io.placeholder = current` (`copypaste.py:28-31`), so typed text is lost whenever the clipboard changes.
- Brace-block and multi-line collection call `prompt()` once per line, so prompt_toolkit's history (append-on-accept) stores each line as its own entry (inferred from library behaviour plus `io.py:636-731`).
- `restore_multiline` decorator forces multiline off during `confirm_ask` / `prompt_ask` so Enter answers questions (`io.py:57-71`, `806`, `927`); HISTORY 408 "multi-line mode persists through ^C at confirmation prompts" was a bug.
- HISTORY 389 and 395: continuation prompt `". "` leaked into multi-line input.
- HISTORY 1442: "Use Meta-ENTER (Esc+ENTER in some environments)": the Alt-Enter newline depends on the terminal sending ESC-prefixed Enter.
- HISTORY 705: "architect mode handles Control-C properly" was a fix.
- `AutoCompleter.tokenize` reads every in-chat file on the first keystroke; the code guards `except Exception: # On Windows, bad ref to time.clock which is deprecated` (`io.py:137-141`). HISTORY 850 mentions autocomplete performance in large repos.
- Ambiguous command prefixes print `"Ambiguous command: …"` rather than choosing (`commands.py:329-330`).

---

## Steal / trap summary

### crush
- Steal: history that only engages at buffer start or end, with Esc back to the draft, and edits of a recalled entry turning it into the new draft. Shell commands stored with their `!` prefix so recall restores the mode. External editor opened at the cursor line and column. Mode-specific prompt glyphs plus placeholder text. Large paste becomes a named attachment with a 5 MB cap. Image-path paste attaches only when every path exists and is an image.
- Trap: a single Esc wipes the whole queue. Backslash-Enter strips the backslash without adding a newline. `@` and backslash checks use the end of the buffer rather than the cursor. Bare `exit` cannot be sent. The paste "column" threshold counts bytes.

### kimi-cli
- Steal: queue and steer as separate keys, with `↑` on an empty buffer recalling a queued item for edit. The live prompt keeps the turn visible above the input. Paste placeholders expand on submit and in history, and re-fold after the external editor through a diff. Paste thresholds are tunable. `@` trigger guard characters. git-index-mtime cache for file completion. Background turns defer while the operator is typing (helps IME). Queued messages dropped on failure are announced. The input header shows plan mode and the queued count.
- Trap: the escape cancel binding is eager while running, so Alt-Enter can cancel (inferred). `↑` pops LIFO while `ctrl+s` takes FIFO. Prompts typed during a turn are missing from saved history. Placeholder tokens are editable plain text and break silently. Paste-placeholder CJK IME breakage. Ctrl-C loses the draft where Esc keeps it.

### aider
- Steal: one swap toggle for Enter and Alt-Enter (with `multi>` in the prompt). `ctrl+up`/`ctrl+down` for whole-message history inside multi-line text. `{tag … tag}` fences for pasting text that contains braces. Prefilled next prompt as a general mechanism (`"What's wrong? Fix"`, voice, editor). Identifier completion drawn from in-chat files. Dumb-terminal fallback to plain `input()`.
- Trap: the clipboard watcher clobbers the draft. Line-at-a-time multi-line input pollutes history. Alt-Enter depends on ESC-prefix support. There is no composer while busy, and Ctrl-C discards the draft.

---

## Composer study, part: hermes-agent, mistral-vibe, vtcode

Read-only source study of snapshots under `/Users/patrickbeam/projects/blackbird/quarantine/`.
Path prefixes used below:

- `H/` = `hermes-agent-2026-10-07/` (classic prompt_toolkit CLI in `hermes_cli/`; the separate Ink TUI is in `ui-tui/`)
- `V/` = `mistral-vibe/` (Textual TUI in `vibe/cli/textual_ui/`; a newer Rust CLI is in `vibe/cli-rust/`)
- `T/` = `vtcode/crates/codegen/vtcode-ui/src/tui/core_tui/` (Rust, ratatui + crossterm + ratatui_textarea)

---

## 1. hermes-agent (classic CLI, prompt_toolkit)

The composer is a multiline `TextArea` (`H/hermes_cli/cli_tui_mixin.py:2216-2230`) with `height=Dimension(min=1, max=8)`, `wrap_lines=True`, `FileHistory`, a `ThreadedCompleter` and a combined auto-suggest. A height function counts both explicit newlines and soft wraps from live terminal cells (`H/hermes_cli/cli_terminal_input.py:548-569`: "Never substitute a fake wide fallback: a mis-sized TextArea leaves stale cells at the bottom").

### 1.1 Submit and newline
- Enter always submits (`_bind_prompt_submit_keys`, `H/hermes_cli/cli_terminal_input.py:464-476`).
- Alt+Enter (`'escape','enter'`) inserts a newline (`cli_tui_mixin.py:1933`, handler at 1185-1188: "Windows Terminal intercepts Alt+Enter (fullscreen) and delivers Ctrl+Enter as c-j").
- Ctrl+J inserts a newline when `display.cli_multiline_shortcuts` is on (default true) or when the environment is known to deliver Ctrl+Enter as LF (`cli_tui_mixin.py:1934-1938`; `_preserve_ctrl_enter_newline`, `cli_terminal_input.py:439-461`, which checks `win32`, `SSH_CONNECTION/SSH_CLIENT/SSH_TTY`, `WT_SESSION`, Ghostty vars and `microsoft` in `/proc/version` "WSL env vars can be scrubbed under sudo; also peek /proc").
- Shift+Enter and Ctrl+Enter: Kitty CSI-u `ESC[13;<m>u` (plus CapsLock/NumLock twins), and xterm `ESC[27;<m>;13~`/`;13u` are remapped to `(Escape, ControlM)` so the Alt+Enter newline handler fires (`H/hermes_cli/pt_input_extras.py:92-122`). Comment: "Stock prompt_toolkit maps the tilde form to plain ControlM (i.e. Shift+Enter == Enter, the very bug this fixes)" and "macOS Terminal and stock Windows Terminal send the same byte for Enter and Shift+Enter, so nothing can be done for them here."
- Backslash-Enter: a trailing `\` (regex `\\[ \t]*$`, `cli_terminal_input.py:348`) with the cursor at end becomes a newline (`cli_tui_mixin.py:1434-1444`). Comment: "Explicit `\` + Enter continuation runs first so its backslash is consumed identically whether the Enter was typed or arrived inside a paste."
- Capability setup: an allowlist (`WT_SESSION`, `TERM_PROGRAM` in iTerm.app/WezTerm/ghostty/vscode, kitty, tmux) gets both the Kitty push `ESC[>1u` and modifyOtherKeys `ESC[>4;2m` (`cli_terminal_input.py:339-413`). Ghostty receives only modifyOtherKeys because "its Kitty disambiguate mode strips Alt from Backspace (upstream bug), breaking backward-kill-word" (351-361). The exit reset sequence pops both modes, bracketed paste, focus and mouse (328-336).

### 1.2 Paste
- Bracketed paste (`Keys.BracketedPaste`, eager) goes to `_tui_handle_paste` (`cli_tui_mixin.py:1699-1743`). It normalises CRLF/CR to LF, strips leaked paste wrappers, CPR replies and SGR mouse fragments, sanitises surrogates ("Word/Google Docs paste"), and logs a warning when the handler takes over 500 ms ("CLI freezes on paste" reports, #16263).
- Large-paste placeholder: at 5 or more lines (`paste_collapse_threshold`) or 2000 or more chars (`paste_collapse_char_threshold`) (`cli_tui_mixin.py:1692-1697`), the text is written to `~/.hermes/pastes/paste_<n>_<HHMMSS>.txt` and the buffer gets `[Pasted text #N: K lines → /abs/path]` (1678-1690).
  - It expands on submit: `_expand_paste_references` (`H/hermes_cli/cli_stream_mixin.py:209-226`), called from `cli_tui_runtime_mixin.py:110-111`. If the file has gone, the placeholder text is sent literally, with a log warning ("Paste file gone or unreadable, returning placeholder").
  - It also expands before history and before the external editor (`_inline_pastes`, `H/hermes_cli/cli_modal_mixin.py:183-200`): "History recall and the external editor need the content (the file may be gone or on another machine)."
  - Recalling an expanded entry from history would trigger a fresh collapse, so `_tui_recall_without_recollapse` sets a skip flag (`cli_tui_mixin.py:1153-1164`).
- Paste-burst fallback without bracketed paste comes in two parts:
  1. `on_text_changed` collapses when `chars_added > 1 or newlines_added >= 4` and the text crosses `paste_collapse_threshold_fallback`. Slash-prefixed text is skipped (`cli_tui_mixin.py:1745-1782`).
  2. An Enter within 50 ms of the last buffer change (`_RAPID_INPUT_ENTER_WINDOW_S = 0.05`, line 46) inserts `\n` instead of submitting (1446-1452). Comment: "Paste without bracketed-paste (tmux strips it) and IME/voice dictation deliver each newline as its own Enter key event ... (#10994)".
- Stuck paste: `Vt100Parser.feed` is monkeypatched to flush a paste whose `ESC[201~` never arrives after 2 s (`cli_terminal_input.py:242-308`). Warning text: "Terminal may have dropped ESC[201~ (see #16263)".
- Image paste:
  - An empty or whitespace-only bracketed paste triggers a clipboard image probe (`_should_auto_attach_clipboard_image_on_paste`, `cli_terminal_input.py:207-209`: "Auto-attach clipboard images only for image-only paste gestures"). This avoids attaching stale images to text pastes.
  - Alt+V attaches the clipboard image (`cli_tui_mixin.py:1166-1171`: "Alt combos pass through every terminal ... reliable on WSL2/VSCode/SSH").
  - Ctrl+V does the same for terminals that send raw 0x16 (1173-1178).
  - Attachments appear as badges, `[📎 name]` or numbered, that adapt to width (`cli_terminal_input.py:183-204`).
- File drop: on submit, a leading path token (quoted, `/`, `~`, `./`, `../`, `file://`, or a drive letter) is detected. The detector tries progressively shorter space-split prefixes to cope with unescaped spaces (`cli_terminal_input.py:145-180`). A non-image file becomes `[User attached file: <path>]` (`cli_tui_runtime_mixin.py:~97`). Guard comment: "ENAMETOOLONG for a pasted `/goal <long prose>` ... would otherwise reach process_loop and silently lose the input" (130-131).

### 1.3 Editing
- prompt_toolkit Emacs mode by default, or Vi when `display.vim_mode` is set (`H/hermes_cli/cli_tui_runtime_mixin.py:374-377`; `cli_init_mixin.py:443`). The kill ring, word motion, and undo (Ctrl+_) are prompt_toolkit's stock Emacs bindings. Hermes adds none of its own.
- Ctrl+D deletes forward and exits only when the composer is empty and no images are attached (`cli_tui_mixin.py:1143-1151`).
- Cmd+Backspace maps to Ctrl+U and Cmd+ForwardDelete to Ctrl+K under Kitty/modifyOtherKeys (`pt_input_extras.py:125-141`).
- Up and Down move by logical line inside a multiline buffer. History is used only on the first or last line (`auto_up`/`auto_down`, `cli_tui_mixin.py:1943-1950, 1808-1815`). The snapshot has no visual-row movement code beyond what prompt_toolkit provides.
- Width uses `get_cwidth` in the height estimator (`cli_terminal_input.py:557-566`). Grapheme and IME handling are prompt_toolkit's, apart from the 50 ms Enter heuristic aimed at IME and dictation.
- A large `pt_input_extras.install_modify_other_keys_aliases` table registers CapsLock/NumLock twins for every CSI-u combo (`pt_input_extras.py:8-10, 163-317`). Comment: "with NumLock on, Ctrl+C arrives as ESC[99;133u (5 + 128) instead of ESC[99;5u" (#88221, #89651).

### 1.4 History
- Global scope: `~/.hermes/.hermes_history` through prompt_toolkit `FileHistory` (`cli_init_mixin.py:318`; `cli_tui_mixin.py:2224`). There is no per-project split and no dedup code in the snapshot.
- Prefix filtering: none. `enable_history_search` is not set. History-based ghost text is used instead (`AutoSuggestFromHistory` wrapped by `SlashCommandAutoSuggest`, `H/hermes_cli/commands_completion.py:469-514`).
- Reverse search: Ctrl+R is rebound to `toggle_dock` (`cli_tui_mixin.py:1972-1979`: "Ctrl+R is the portable fallback because macOS often reserves F7"), so readline-style reverse-i-search is not available in this composer.
- The draft is preserved by prompt_toolkit Buffer working lines while browsing.
- History stores expanded paste text rather than placeholders (1472-1474).

### 1.5 Mentions and completion
- `@` completion offers static refs, `@file:`/`@folder:` directory browsing, and otherwise fuzzy search over project files. The file list comes from `rg --files --sortr=modified`, falling back to `rg --files` and then `fd`, with a 2 s timeout, a 5000-file cap and a 5 s TTL cache (`commands_completion.py:350-417`). An empty `@` lists recently modified files. Inserted tokens look like `@file:path`. Expansion at send time (`@file:`, `@diff`, `@folder:`) is budgeted against the model's context length and can be refused (`H/hermes_cli/cli_chat_turn_mixin.py:152-175`).
- Slash completion covers built-ins, subcommands, skill bundles (`▣`), skills (`⚡`) and plugins (`🔌`). Exact matches get a trailing space so the menu stays open, except picker commands: "Bare-run picker commands get no trailing space: "/model " would block the picker" (`commands_completion.py:262-263, 340-345`).
- Stacked skill invocations (`/skill-a /skill-b do X`) are completed up to a cap (299-327).
- Ghost text: for slash names it prefers the shortest match ("so /he ghosts "lp", not "artbeat"", 492-499). Otherwise it falls back to history ghost text.
- Tab accepts the open completion, then ghost text, then starts completion (`cli_tui_mixin.py:1051-1070`). Tab also re-triggers after a provider like `anthropic:`, so stage-two models appear.
- The completer runs in a `ThreadedCompleter` because "fuzzy @-file indexing shells out to rg/fd with a 2s timeout ... would stall the render loop per keystroke (WSL2/slow FS)" (2225-2229).

### 1.6 Modes
- `!` shell mode: `!cmd` runs locally in the session cwd with zero tokens, never entering model history (`H/hermes_cli/bang_shell.py:1-7`). `!!` yields a literal `!`. The timeout is 120 s ("so an accidental `!sleep 999` cannot wedge the composer"). It is disabled for gateway, cron and API sessions as "a remote-execution surface with no approving human" (41-55).
- `!` is shown only as typed text. There is no mode chrome.
- Plan mode toggle: not found in the composer key bindings.

### 1.7 While busy
- Typing ahead is always allowed. Enter while the agent runs follows `busy_input_mode`: `interrupt` (default), `queue` or `steer` (`cli_init_mixin.py:58`; `cli_tui_mixin.py:1505-1570`).
  - `steer` calls `agent.steer(text)`. It falls back to queue when images are attached or the steer is refused.
  - `interrupt` tries `agent.redirect(text)`, otherwise the legacy interrupt queue.
  - `queue` sends the message on the next turn and prints `queued_next_turn` with an 80-char preview.
- Slash and `!` commands never steer: "it must never be routed into steer/redirect (injecting `!git status` into the model's context as a prompt)" (1453-1456).
- `/steer`, `/bg` and `/btw` run inline on the UI thread so they are not delayed to the next turn (#34569, #75221; 1476-1503).
- A one-time onboarding tip about `/busy` appears on the first busy submit (1557-1567).
- Queued messages in the classic CLI cannot be recalled or edited. The Ink TUI can do this: Up cycles queued items into the composer before history, Ctrl+X removes the item being edited, Esc cancels, and Ctrl/Cmd+K sends the next queued item now (`H/ui-tui/src/app/useInputHandlers.ts:322-335, 646-648, 652-676, 702-706, 837-843`; `H/ui-tui/src/components/queuedMessages.tsx`).
- Interrupt keys (`cli_tui_mixin.py:953-978`). Ctrl+C works through a ladder: cancel voice, close picker/palette, clear overlays, interrupt the agent, force-exit on a second press within 2 s, and when idle clear the draft or exit. Ctrl+Q is the same without the double-press force exit (980-995).
- Overlays must not swallow the interrupt: "Callers must NOT return on True alone ... a stale/orphaned overlay ... can't swallow the press before the agent-interrupt branch, leaving the chat frozen (#14026)" (928-941).
- Double Esc discards the draft and attachments even while streaming, and appends the draft to history first, "which is what makes a reflex key safe" (1072-1086).
- The draft survives an interrupt. Ctrl+C clears the draft only when idle.

### 1.8 Drafts
- Ctrl+S stash is a stack of up to 20 entries (`MAX_STASH_ITEMS = 20`, `H/hermes_cli/prompt_stash.py:17-18`). Each entry holds text and images (`cli_tui_mixin.py:1784-1801`).
  - Ctrl+S with content pushes and clears.
  - Ctrl+S on an empty composer pops the single entry, or opens a browse panel when there are several (Up/Down, Enter restore, `d`/`D` delete, Esc close; 1981-2004).
  - Restore extends attachments rather than replacing them: "dropping it silently would be data loss" (1020-1032).
  - Ctrl+S is suppressed during secret prompts "so Ctrl+S can't stash a password".
  - The stash is in memory only. No crash or exit recovery was found.
- External editor: Ctrl+G, or Alt+G "in VSCode/Cursor" where Ctrl+G is Find Next (1981-1988). Placeholders are inlined first. Saving the file submits the prompt ("TUI Ctrl+G parity: save == send"; `cli_modal_mixin.py:117-176`), and an empty save is ignored. A guard notes: "buffer.tempfile = "prompt.md" takes the complex-tempfile branch that re-mkdir()s the mkdtemp() dir and raises EEXIST" (`cli_tui_mixin.py:2231-2233`).

### 1.9 Traps and gotchas
- Leaked terminal replies land in the buffer as literal text. CPR replies (`^[[39;1R`) and SGR mouse reports are stripped on every text change and in paste payloads (`cli_terminal_input.py:309-326, 527-545`). CPR queries are disabled on POSIX because "delayed replies leak into input" and "can freeze input when the reply is slow (#13870)" (485-518).
- Focus reports `ESC[I`/`ESC[O` would self-insert without a no-op `Keys.Ignore` binding (`cli_tui_mixin.py:1088-1104`, #60920, #25337).
- Enabling CSI-u broke Ctrl+C: "Ctrl+C arrived as ``ESC[99;5u`` and died, #56684" (`cli_terminal_input.py:388-391`).
- Ghostty sends Super+o as `ESC[27;9;111~` (#114242) (`pt_input_extras.py:256`).
- "No Ctrl+Shift+C binding: terminal emulators intercept it ... and prompt_toolkit's key parser doesn't recognise 'c-S-c' anyway (#19884/#19895)" (`cli_tui_mixin.py:1953-1954`).
- After `process_command()` printed through `patch_stdout`, the cleared input kept showing the submitted text, "looking unsent, inviting a re-submit", so every branch now invalidates (1488-1491).
- The 50 ms rapid-Enter heuristic means a fast typist pressing Enter within 50 ms of the last keystroke gets a newline instead of a submit.
- Paste placeholders refer to an absolute file path. If the file disappears, the literal placeholder is sent to the model.
- Ctrl+R hijacked for the dock means there is no reverse search, and the voice key had to default to Ctrl+B "to avoid conflict with Ctrl+R readline reverse-search" (2086-2087).

---

## 2. mistral-vibe (Textual TUI; Rust CLI noted)

The composer is `ChatTextArea(TextArea)` (`V/vibe/cli/textual_ui/widgets/chat_input/text_area.py`). Input modes are a separate prefix state: `InputMode = Literal["!", "/", ">", "&"]` with `>` as the default (`V/vibe/cli/input_modes.py`).

### 2.1 Submit and newline
- Enter submits (`text_area.py:640-644`). Shift+Enter and Ctrl+J insert a newline through a priority binding (`text_area.py:57-63`).
- Ctrl+Enter or Super+Enter on an empty input posts `SteerQueueRequested` (629-638): "promote everything waiting in the queue into the running turn" (127-133).
- Backslash-Enter and Alt-Enter: not in the snapshot.
- Terminal setup: Textual's own setup. VS Code 1.110+ sends space as CSI-u `\x1b[32u`, which Textual parsed with `character=None`, so `patch_vscode_space` fixes it (`V/vibe/cli/textual_ui/widgets/vscode_compat.py:9-18`).
- The Rust CLI pushes `DISAMBIGUATE_ESCAPE_CODES` only (`V/vibe/cli-rust/src/terminal.rs:58`) and maps Ctrl+J and Shift+Enter to a newline: "Shift+Enter only reaches us on terminals that report it distinctly" (`V/vibe/cli-rust/src/keymap.rs:20-24`).

### 2.2 Paste
- `_on_paste` rewrites a standalone pasted path, or a newline-delimited list of paths, into `@path` mentions (`text_area.py:379-394`; `paste_path.py:27-40`). An empty paste means "clipboard holds image bytes the terminal cannot deliver as text", so the app probes the OS clipboard silently (388-393).
- Ctrl+V for an explicit image paste is registered only on macOS (`CLIPBOARD_IMAGE_PASTE_SUPPORTED_SYSTEM`). The comment gives the reason: "the user never discovers a feature that wouldn't work for them" (71-87).
- Drag-and-drop without bracketed paste: `on_text_area_changed` scans the whole text and prefixes bare absolute image paths with `@`. This is idempotent, with a fast path that skips stat calls when no `/~'"` is present (`text_area.py:413-425`; `paste_path.py:43-60`).
- Large-paste placeholder: not in the snapshot (Textual TUI). Paste-burst Enter detection: not in the snapshot.
- Event-stop guard: "event.stop() prevents App.on_event from auto-forwarding the Paste back to the focused widget (which would otherwise dispatch this handler a second time and double-insert)" (380-386).

### 2.3 Editing
- Textual TextArea bindings, plus Alt+Left/Right word motion, Shift+Backspace/Delete, and Ctrl+G external editor (`text_area.py:56-70`). Undo and redo come from TextArea. There is a local override for an upstream undo crash: "Workaround for an undo crash fixed upstream in https://github.com/Textualize/textual/pull/6687" (189-194; CHANGELOG `V/CHANGELOG.md:480` "Crash on undo of a large multiline insert in the chat input").
- No kill ring and no vim mode in the snapshot.
- Mouse: double-click selects a word, triple-click a line, and the click chain cycles char→word→paragraph with drag snapping (215-377).
- Up and Down use wrapped-line awareness through `navigator.is_first_wrapped_line`/`is_last_wrapped_line` (474-515). Up first jumps the caret to (0,0) and only then recalls history (484-494). Down on the last wrapped line with no history loaded relinquishes focus to the widget below (`NavigateBelow`, 687-694).
- Grapheme and wide-character handling is Textual's. The Rust CLI uses `unicode_segmentation` graphemes for delete and move (`V/vibe/cli-rust/src/utils/input_edit.rs:40-83`).

### 2.4 History
- Global `~/.vibe/vibehistory`, JSON-per-line, capped at 100 entries (`V/vibe/core/paths/_vibe_home.py:27`; `V/vibe/cli/history_manager.py:13`).
- Dedup is consecutive only (`entries[-1] == text`, 75-77).
- Concurrent sessions merge: the file is read, merged and written atomically through a temp file and `os.replace`, off the UI thread (79-118). CHANGELOG: "Concurrent Vibe sessions now merge their prompt history instead of overwriting each other" and "Submitting a prompt no longer pauses the UI on history file I/O" (`V/CHANGELOG.md:85-86`).
- Prefix filtering was deliberately removed: "History navigation no longer filters by prefix" (CHANGELOG:1488).
- The draft is saved in `_temp_input` when browsing starts and restored past the newest entry (`history_manager.py:120-149`).
- Mode prefixes are parsed back out of history entries so `!cmd` restores into `!` mode (`body.py:172-190`).
- Reverse search: not in the snapshot. Ctrl+R starts voice recording (`text_area.py:549-557`).

### 2.5 Mentions and completion
- `@` path completion uses a Git-aware indexer, a watcher and a fuzzy scorer. It runs in an executor with generation-based staleness cancellation (`V/vibe/cli/autocompletion/path_completion.py:80-125`; `file_indexer/`). CHANGELOG: "Make @ file mentions use Git-aware discovery and accept standalone pasted files and folders" (221) and "Path autocomplete now prioritizes matches beneath an explicitly typed directory prefix" (279).
- The slash popup does fuzzy matching. Tab applies the completion. **Enter applies it and submits immediately** (`CompletionResult.SUBMIT`, `slash_command.py:63-72`).
- Ghost text: mid-prompt `/skill` tokens get an inline ghost through TextArea `suggestion`, accepted with Tab (`inline_skill_completion.py:14-20`). A mouse click or caret move re-evaluates so a stale ghost clears (`text_area.py:235-241, 452-464`).
- Argument hints: not found.

### 2.6 Modes
- Typing `!`, `/` or `&` (teleport, when available) as the first character of an empty default-mode input consumes the character and switches mode. The prompt glyph widget then shows the mode character instead of `>` (`text_area.py:651-660`; `body.py:162-170`).
- Backspace on an empty moded input returns to `>` (662-669, 785-790).
- `get_full_text()` re-prefixes the mode for submission and completion (792-795).
- Shift+Tab cycles agent profiles, plan and similar (`app.py:686, 5882-5887`).

### 2.7 While busy
- Prompts submitted while busy are queued on the app server and shown above the input. All prompts queued during one generation fold into one server item, so they are delivered as a single turn (`V/docs/adr/0013-queue-edit-mode.md` "Merged delivery").
- Slash, shell and teleport commands cannot be queued and are rejected with a hint. The input is restored if empty (`app.py:2040-2065`).
- Queue selection: Up while busy with a non-empty queue enters selection mode. Up/Down move, Backspace/Delete removes (`session/turn/queue/remove`), Enter loads the item for editing, and submit calls `queue/replace`, keeping its ID and FIFO slot. Esc exits and restores the original draft (ADR 0013; `text_area.py:586-612`; `body.py:333-484`).
- Race guard: if the item was promoted while being edited, the user sees "This message was already processed — press Enter to submit as new, or Escape to discard." (`body.py:512-527`).
- Steering: empty Enter or Ctrl+Enter during a running turn sends the merged queue as steering (`app.py:1843-1847`; ADR 0013 "Steering the merged queue").
- Esc is a priority binding that interrupts the agent (`app.py:682`). It is disabled on modals and in queue mode through `check_action` ("would otherwise win and trap the user inside it", 5778-5793).
- Interrupt-settle window: a submit fired right after Esc waits up to 30 s for queue state to settle "so it resumes or starts a turn instead of being dropped or trapped" (5611-5652, 1817-1836).
- The Ctrl+C ladder (`app.py:6097-6128`): clear a non-empty input, quit if already confirmed, run no-job steps, otherwise **pop the last queued item (LIFO)**, otherwise interrupt the running job, otherwise request quit confirmation.
- Ctrl+D deletes right, or quits with confirmation (6130-6147).
- CHANGELOG: "Esc pauses the queue, Ctrl+C drops the last queued message (LIFO), Enter flushes the queue when paused" (922).

### 2.8 Drafts
- External editor: Ctrl+G under `app.suspend()` replaces the text with the result and does not submit (`text_area.py:402-411`).
- No stash, and no draft persistence across crash or exit, in the snapshot.
- Double Esc within `DOUBLE_ESC_DELAY = 0.2` s clears a non-empty input without saving it, or enters rewind when the input is empty (`app.py:485, 5595-5600`). Ctrl+C with text also clears without saving (6111-6113).

### 2.9 Traps and gotchas
- `terminal_input_filter.py:1-30` patches Textual against malformed SGR mouse reports `\x1b[<32;NaN;NaNM` from VS Code focus changes (Textual issue 6573), OSC 11 colour replies "spill[ing] into the focused input box", and X10 mouse after tmux reattach (Textual issue 6668).
- CHANGELOG: "Escape sequences in command output no longer flood the input box" (449). "NaN SGR mouse reports no longer leak into the chat input" (944). "Escape+Enter to restart a queued turn no longer drops the Enter; Escape just as a turn finishes no longer stalls the next message" (363). "A message sent while idle or right after interrupting no longer flashes as queued" (362). "Arrow-key history navigation at wrapped-line boundaries" (1543). "Reset cursor position on up-arrow" (1111).
- Enter on an open slash popup submits immediately. There is no chance to add arguments.
- An empty Enter while busy steers the queue, so a stray Enter has an effect.
- The 0.2 s double-Esc window is tight, and the clear is not recoverable (no history push).
- Ctrl+C pops the last queued message with no confirmation.
- Focus: `on_blur` refocuses the composer unless focus was deliberately relinquished. This keeps the caret captive (`text_area.py:196-213`).

---

## 3. vtcode (Rust, ratatui + ratatui_textarea)

Key handling lives in `T/session/events.rs` (`process_key`, line 241) and the app layer `T/app/session/events.rs`. Text storage is `InputManager` over `ratatui_textarea::TextArea`, which supplies undo/redo and character boundaries (`T/session/input_manager.rs:1-6, 141-145`).

### 3.1 Submit and newline
- Enter idle submits. Enter during a running turn **steers**: "injected into the conversation right after the current tool-call batch". Slash commands queue instead (`T/session/events.rs:693-702`).
- Ctrl+Enter during a running turn queues for after the turn (668-676). Ctrl+Enter on an empty composer while idle sends `ProcessLatestQueued`.
- Shift+Enter or Alt+Enter inserts a newline (680-684). Ctrl+J inserts a newline (495-499).
- Backslash-Enter replaces a trailing `\` with a newline (647-654).
- Enter on an empty composer with live PTY sessions submits `/jobs` (636-645).
- Tab accepts ghost text first, otherwise **enqueues the draft like Ctrl+Enter** (`enqueue_tab_draft`, 10-31, 706-728). CHANGELOG: "Add Tab-to-queue and line-wise editing shortcuts" (`vtcode/CHANGELOG.md:895`).
- Capability setup: Kitty flags by config mode. `default` is DISAMBIGUATE|REPORT_EVENT_TYPES|REPORT_ALTERNATE_KEYS, and REPORT_ALL_KEYS is forced on macOS Ghostty "so bare Command presses surface as modifier-key events" (`T/../config/mod.rs:364-436`; push in `T/runner/terminal_modes.rs:97-118`). Only `Press` events are processed (`T/runner/events.rs:158`).
- `normalize_terminal_control_event` folds legacy macOS Cmd mappings (Cmd+Left→0x01) into Ctrl+letter so both families share code (`events.rs:441-453` comment).

### 3.2 Paste
- Bracketed paste is enabled through crossterm (`T/runner/drive.rs:108`). `sanitize_pasted_text` drops `\r` and DEL (`T/session/editing.rs:22-24`).
- Large paste: the full text stays in the buffer. Only a byte range is tracked as a compact block, rendered as `[Pasted Content N chars ...]`, at 10 or more lines or 1000 or more chars, or by image or `@file` count (`editing.rs:172-202`; `T/session/input.rs:1705-1735`; constants `T/../config/constants/ui.rs:49,53`).
  - Paste-twice toggle: "Pasting while already collapsed expands to the full text instead, so paste-once summarizes and paste-twice reviews the complete content" (178-180).
  - A single Backspace at the end of a collapsed block deletes the whole block (`editing.rs:268-287`).
  - Shift+Ctrl+V pastes raw without collapse (207-221; CHANGELOG:90).
  - Nothing expands at submit, because the buffer already holds the real text.
- Paste routing: a paste into an overlay with search goes to the search field, and pastes into wizard notes are flattened (`events.rs:33-76`; CHANGELOG:1255 "Route paste to overlay search instead of composer").
- Image paste: `T/session/clipboard_image.rs` covers PowerShell/WSL clipboard probes and inserts `[Image #N]` placeholders tied to attachments (`input_manager.rs:31-90`). The file header says "Slice 4 wires this helper into application shortcut handling" with `#![allow(dead_code ...)]`, so wiring was partial in this snapshot.
- Paste-burst detection without bracketed paste: not in the snapshot. Each newline would arrive as Enter and submit or steer.
- File-path drops: path-like tokens beginning `@`, `/` and so on are classified (`input.rs:1417-1430`).

### 3.3 Editing
- Rich Emacs set, hardcoded (`events.rs:441-569`): Ctrl+A/E, Ctrl+W, Ctrl+U (clears the current logical line, or all if single-line), Ctrl+K, Ctrl+F/B, Ctrl+P/N (history), Ctrl+T transpose chars, Alt+D, Alt+T transpose words, Alt+U/L/C case, `Alt+\` delete whitespace, Alt+B/F words.
- Undo with Ctrl+Z, redo with Ctrl+Y (500-509). No kill ring (Ctrl+Y is redo).
- Cmd variants: Cmd+Backspace clears the line, Cmd+Delete kills to end, Cmd+A clears the line (829-842).
- Word segmentation uses `split_word_bound_indices` (`editing.rs:30-33`).
- Vim mode: an optional `VimState` intercepts keys before everything else (`T/session/vim/mod.rs:57-74`; engine in `vtcode-ui/src/vim/`, about 1.2k lines).
- Wrapped rows: in a multi-row composer, soft-wrapped or multi-line, Up/Down move by **visual row** and never traverse history. Ctrl+P/N stay as unconditional history keys (`events.rs:393-428`; `editing.rs:590-620`). CHANGELOG: "Up/Down traverses wrapped input rows instead of history" (226) and "Gate Up/Down history on single-line input" (223).
- `?` on an empty composer opens help (850-853).
- IME: not in the snapshot.

### 3.4 History
- In-memory per session, with entries carrying attachments (`input_manager.rs:96-139, 873-885`). Dedup is consecutive.
- The draft, including attachments, is preserved in `history_draft` on first Up and restored on Down (939-944, 919-931).
- Cross-session: on start, the user prompts of up to 50 recent session archives are loaded, truncated to the **first line, 200 chars**, capped at 20 entries, and prepended to Up-arrow history and the Ctrl+R picker (`vtcode/src/agent/runloop/unified/turn/session_loop_runner/orchestration.rs:2838-2877`; `input_manager.rs:887-917`, deduplicated by content).
- Reverse search: the Ctrl+R history picker uses nucleo fuzzy matching and shows relative time labels and session labels (`T/app/session/history_picker.rs:1-50`). A legacy inline reverse search also exists (`T/session/reverse_search.rs`). It restores content, cursor and attachments on cancel (51-58).

### 3.5 Mentions and completion
- `@` file palette with directory browsing (synthetic `..`, symlink targets as `→ target`) and fuzzy search mode (`T/app/session/file_palette/`). `@agent-` tokens are recognised (`input.rs:154-176`).
- Slash palette (`T/app/session/slash_palette.rs`). Argument hints: not found.
- Ghost text: a model-generated inline prompt suggestion requested with Alt+P (`GeneratePromptSuggestion`, `T/session/action.rs:262-265`) and accepted with Tab.
- Most keys are rebindable through a binding store. Readline keys stay hardcoded (`action.rs:196-265, 489-520`).

### 3.6 Modes
- `!` shell mode is recognised by a leading `!`. The composer drops its padding and shows the border title `" ! Shell mode "` (`T/session/input.rs:269, 1144-1162`).
- Shift+Tab (BackTab) cycles primary agents. Mode switches are locked during a turn and produce a notice (`events.rs:728-741, 905-944`).

### 3.7 While busy
- Typing ahead is allowed. Enter steers, and Ctrl+Enter or Tab queues.
- The queue renders above the input as strict FIFO, with `[i/n]` numbering and flattened multiline items (`T/session/queue.rs:48-60`).
- Edit-queue pops the **latest** queued item back into the composer. The key is Alt+Up, or Shift+Left under tmux, with a hint of "⌥ + ↑ edit" or "⇧ + ← edit" (`action.rs:234`; `events.rs:766-780`; `terminal_capabilities.rs:158-180`). Only the newest item can be edited, and editing removes it from the queue.
- CHANGELOG: "Surface coalesced warning when queued inputs hit the soft cap" (72).
- Interrupt: Esc interrupts a running turn or PTY (`events.rs:571-597`). Ctrl+C interrupts, and a second press within 1 s exits (`events.rs:107-128`; `T/app/session/events.rs:782-828`). Ctrl+C with a composer selection copies instead (79-105).
- Ctrl+D exits unconditionally (`T/app/session/events.rs:829-832`).
- The draft is not touched by interrupt.

### 3.8 Drafts
- External editor: Ctrl+G sends `LaunchEditor { draft }`. The result is put back with `set_input` and not submitted (`events.rs:465-469`; `vtcode/src/agent/runloop/unified/turn/session/interaction_loop_runner/support.rs:869-872, ~221`). The fallback editor is `vi` or `notepad` (CHANGELOG:4259).
- Double Esc (800 ms window, `action.rs:471-485`): with content it clears the **current line** if multiline, or everything if single-line. On an empty composer it submits `/rewind`. Undo (Ctrl+Z) can recover a clear.
- No stash and no crash recovery of the draft in the snapshot.

### 3.9 Traps and gotchas
- **Typed newlines are silently refused past 10 lines.** `insert_char('\n')` returns when `!can_insert_newline()` (`editing.rs:165-167, 248-265`; `INLINE_INPUT_MAX_LINES = 10`). Pastes bypass the cap.
- Archived history entries are first-line previews of 200 chars. Recalling an old multi-line prompt with Up gives only its first line. The doc comment says "last 24 hours" but the code takes 50 recent sessions and keeps the first 20 entries found (`orchestration.rs:2838-2873`). Timestamps are approximated by session start ("per-message timestamps are not stored").
- Tab queues the draft. Users expecting completion or indentation will send messages unintentionally.
- Plain Enter while busy steers into the running turn. A user may have meant to queue.
- Ctrl+D exits even with a draft present.
- Ctrl+B is bound to `BackgroundOperation` by default, so readline Ctrl+B only works when that binding is removed (`action.rs:208-214`; `T/app/session/events.rs:833-840`).
- `sanitize_pasted_text` drops `\r` rather than mapping it to `\n`, so a CR-only paste would lose its line breaks (`editing.rs:22-24`).
- There is no paste-burst fallback without bracketed paste.
- CHANGELOG history of the same area: "Remove double escape functionality" (3965), then "Resolve double escape functionality issue" (3959), then "Open checkpoint picker with double-Escape" (804). Also "Prevent arrow keys from triggering external editor launch" (3789), "Remove external editor keybinding (Control+E)" (3790), "Make external editor opens transient and suppress escape leaks" (3926), "utf-8-safe editing, paste-block delete" (1168), and "collapse multiline history entries and fix arrow navigation" (1001).
- The `editing.rs` doc comments still say history navigation is "disabled to prevent cursor flickering" (`editing.rs:530, 541`), which contradicts the live code. The README says "History navigation (currently disabled)" (`T/session/README.md`).

---

## Per-reference: steal / trap

### hermes-agent
Steal:
- `\`-Enter continuation applied before every other Enter rule, so pasted and typed input behave the same.
- Shift/Ctrl+Enter aliasing over both CSI-u and modifyOtherKeys, with CapsLock/NumLock modifier twins, plus an allowlist for pushing extended key modes.
- A 50 ms rapid-Enter window to keep unbracketed pastes and IME/dictation newlines from submitting.
- A 2 s timeout flush for a bracketed paste whose end marker never arrives.
- Large pastes saved to a file with an inline placeholder, expanded on submit, in history and for the editor.
- An empty bracketed paste means a clipboard image. Alt+V is the portable image key.
- Double Esc clears the draft but pushes it to history first ("makes a reflex key safe").
- A Ctrl+S stash stack (20 entries) with a browse panel, which merges attachments on restore.
- `busy_input_mode` with steer/interrupt/queue, local commands never steering, and a one-time onboarding tip.
- The Ctrl+C ladder must fall through stale overlays to the interrupt.

Trap:
- Leaked CPR, mouse and focus bytes self-insert. Strip them on every change and register ignore bindings.
- Enabling CSI-u can silently kill Ctrl+C unless every combo is mapped.
- Placeholder file references go stale.
- Rebinding Ctrl+R costs reverse search.
- An input that does not visibly clear after submit invites a duplicate send.

### mistral-vibe
Steal:
- A prefix-mode state machine: the first `!`/`/`/`&` is consumed into a prompt glyph, Backspace on empty exits, and history round-trips the mode.
- Queue selection and edit mode: Up enters, Backspace removes, Enter edits in place keeping the FIFO slot, and Esc restores the draft. Includes a "message was already processed" race guard.
- All prompts queued during one generation merge into one turn. Empty Enter or Ctrl+Enter steers the queue into the running turn.
- Global history with atomic read-merge-write across concurrent sessions, off the UI thread.
- Pasted paths, and a bare image path in typed text, become `@` mentions.
- Interrupt-settle gating so a submit right after Esc is neither lost nor reordered.

Trap:
- Enter on a slash popup submits immediately.
- A 0.2 s double-Esc window, and a clear that cannot be recovered.
- Ctrl+C silently pops the last queued item.
- Empty Enter while busy has side effects.
- Terminal reports (OSC colour, NaN mouse, X10 after reattach) leak into the input. VS Code CSI-u space arrives without a character.

### vtcode
Steal:
- Large pastes stay in the buffer as a tracked range rendered `[Pasted Content N chars]`. Paste again to expand, one Backspace deletes the block, and Shift+Ctrl+V pastes raw.
- Up/Down move by visual row in multi-row input and never traverse history there. Ctrl+P/N are always history.
- History entries carry attachments. The draft and its attachments are saved on first Up.
- A Ctrl+R fuzzy history picker across sessions, with time and session labels.
- A full readline set: transpose, case, `Alt+\`, Cmd-key normalisation. Undo/redo in the composer.
- Enter steers and Ctrl+Enter queues, as distinct verbs. Queue shown FIFO with `[i/n]`.
- tmux-aware edit-queue key with an on-screen hint. An explicit `! Shell mode` border title.

Trap:
- A silent 10-line cap on typed newlines.
- Archived history truncated to first lines then recalled as if complete.
- Tab-to-queue overloads a completion key.
- Ctrl+D exits with a draft present.
- A default binding shadows readline Ctrl+B.
- CR stripped instead of normalised.
- No unbracketed-paste protection.
- Comments and README drift from behaviour (history "disabled").

---

## Composer study: letta-code and goose CLI

Source snapshots under `/Users/patrickbeam/projects/blackbird/quarantine/`. Paths below are relative to `letta-code/` or `goose/crates/goose-cli/src/`. Read-only study; nothing was run.

---

## letta-code (TypeScript, vendored and patched Ink plus ink-text-input)

The composer has three layers.
- `vendor/ink-text-input/build/index.js` is the basic single-buffer editor: cursor, a few Ctrl keys, a one-slot kill buffer.
- `src/cli/components/PasteAwareTextInput.tsx` wraps it. It keeps a display value (with placeholders) apart from the actual value, handles modified Enter, image paste, word motion and raw escape sequences.
- `src/cli/components/InputRich.tsx` (2116 lines) adds history, bash mode, Esc and Ctrl-C handling, queue editing, permission-mode cycling and the footer.

Bracketed paste, a paste-burst fallback and CSI-u parsing are patched into the vendored Ink: `vendor/ink/build/components/App.js` and `vendor/ink/build/hooks/use-input.js`.

### 1. Submit and newline
- **Enter** submits (`ink-text-input/index.js:111-116`). Submission is blocked while an autocomplete list has matches (`InputRich.tsx:1617-1620`).
- **Shift-, Meta- or Ctrl-Enter** insert `\n` when the key parser reports `key.return` with a modifier (`PasteAwareTextInput.tsx:208-229`). The inner input filters these keys out (`index.js:24-25`).
- **Alt/Option-Enter** arrives as raw `\x1b\r` or `\x1b\n` and is caught on Ink's private `internal_eventEmitter` (`PasteAwareTextInput.tsx:516-522`).
- **Literal `\\r`**: the two characters backslash and r. The code treats them as a newline because VS Code keybindings are "often configured to send a literal "\\r" sequence for Shift+Enter" (`:524-530`). Trap: typing a backslash followed by r in a burst could collide with this. It is an exact-match test on the whole chunk, so it is unlikely but possible.
- **Ctrl-J** is not bound explicitly. **Backslash-Enter** continuation is not in the snapshot.
- **Kitty keyboard protocol detection** (`src/cli/utils/kitty-protocol-detector.ts`):
  - It writes `\x1b[?u\x1b[c` and waits 150 ms (`:128-132`).
  - It then enables flag 1 (`\x1b[>1u`) **even when the query goes unanswered** ("Best-effort ... we enable whenever we're on a TTY", `:66-78`).
  - Flag 7 was abandoned because "flag 2 (REPORT_EVENT_TYPES) causes release events that leak into input when typing fast in iTerm2/Kitty" (`:142-148`).
  - It pops the protocol on exit, SIGTERM and SIGINT (`:76-78`). `LETTA_DISABLE_KITTY=1` opts out.
- **CSI-u and modifyOtherKeys** (`CSI 27;mod;key~`) are parsed as a fallback in `use-input.js:119-140`.
- **Linux-only guard**: a bare `\n` arriving within 75 ms after a modified Enter is dropped "so Shift+Enter doesn't submit" (`use-input.js:9,20-21,108-114`). A related guard strips a trailing newline that arrives in the same chunk as a CSI-u sequence (`:99-101`).
- **Terminal setup is automatic and silent.**
  - On startup it detects VS Code, Cursor or Windsurf and appends a `shift+enter` → `workbench.action.terminal.sendSequence {text:"\u001b\r"}` binding to the user's `keybindings.json`, commented in code as "Silently install keybinding (no prompt, just like Claude Code)" (`src/index.ts:1453-1457`; binding at `terminal-keybinding-installer.ts:98-103`).
  - It also rewrites `wezterm.lua` to work around wezterm issue 3758, where Delete sends 0x08 under the kitty protocol (`index.ts:1462-1486`; `installer.ts:331-380`).
  - `/terminal` and `/terminal --revert` install or remove the binding manually (`commands/registry.ts:480-493`).

### 2. Paste
- **Bracketed paste**: Ink enables `\x1B[?2004h` together with raw mode (`App.js:80-87`). A small state machine (`App.js:100-165`) handles START/END markers split across reads and keeps only partial-prefix bytes of length 2 or more, "to avoid swallowing a bare ESC" (`:120`).
- **Large-paste placeholder**: a paste counts as large above 5 lines or 500 characters (`PasteAwareTextInput.tsx:281`). The display shows `[Pasted text #${id} +${n} lines]` (`:284`) and the content goes into a global `Map` registry (`helpers/paste-registry.ts:11-24`).
  - Placeholders are expanded at submit time by `buildMessageContentFromDisplay` (`paste-registry.ts:150-197`), which also turns `[Image #N]` into base64 image parts.
  - Placeholders never expand in the editor. Deleting characters inside one turns it into literal text that no longer resolves. If an id is missing, "keep the literal placeholder as text" (`:188-189`).
- **Small multi-line pastes** show each newline as `↵` (`sanitizeForDisplay`, `PasteAwareTextInput.tsx:58-61`). `resolvePlaceholders` turns **every** `↵` back into `\n` (`paste-registry.ts:37-38`).
- **Placeholder lifetime is carefully engineered.**
  - `draft-placeholder-discards.ts:15-22` explains that an edit must not free a placeholder, because "the text input's kill buffer (Ctrl+K / Ctrl+U, then Ctrl+Y) or a retyped bracket can bring the same placeholder back".
  - Entries are released only at boundaries (submit, clear, draft replacement). Placeholders still referenced by live holders are kept: the parked history draft, queued messages, restored input, and the in-flight submit (`InputRich.tsx:1086-1103`). The comment at `paste-registry.ts:109-115` says the same.
- **Paste-burst fallback when bracketed paste is absent**, in Ink `App.js:94-99,172-262`:
  - A single chunk of 128 characters or more, or two chunks within 16 ms totalling 128 or more, starts aggregation.
  - The aggregation window is 150 ms once escalated and 16 ms otherwise.
  - On flush, the text becomes a paste event if it is escalated or over 5 lines or 500 characters.
  - A second heuristic in `handleChange` runs a longest-common-prefix/suffix diff on any edit that adds more than 500 characters or more than 5 lines, more than 1 s after the last paste (`PasteAwareTextInput.tsx:636-705`).
- **Image paste** (`helpers/clipboard.ts`):
  - iTerm2 OSC 1337 inline files (`:84-102`) and `data:image/...;base64` URLs (`:108-111`).
  - A pasted single image file path, which covers Finder drag-and-drop. The code strips quotes, unescapes `\ `, decodes `file://` and resolves relative paths (`:116-141`).
  - The macOS clipboard via `osascript -l JavaScript`, triggered by **Ctrl-V**, **Cmd-V** (meta-v) or **Ctrl-Shift-V**, and also by an empty bracketed paste (`PasteAwareTextInput.tsx:231-264,310-324,333-362`; `clipboard.ts:219-310`). Images are resized if needed.
  - Clipboard images are macOS only: `if (process.platform !== "darwin") return null` (`clipboard.ts:279`).
- **Non-image file-path drops** are inserted as ordinary text.

### 3. Editing
- **Inner keys**, all applying to the whole buffer rather than the current line (`index.js:144-177`):
  - Ctrl-A and Ctrl-E jump to buffer start and end.
  - Ctrl-K kills to buffer end; Ctrl-U kills to buffer start.
  - Ctrl-Y yanks a one-slot `killBuffer`.
  - The filter at `index.js:32` swallows every other Ctrl-letter so it cannot insert text.
- **Home and End** match `ESC[H`, `ESC[1~`, `ESCOH` and `ESC[F`, `ESC[4~`, `ESCOF` (`PasteAwareTextInput.tsx:541-559`).
- **Word motion**: Option-Left/Right as `ESC b`, `ESC f`, or `ESC[1;{3,4,7,8,9}D/C` (`:109-121,587-601`). Word boundaries are plain `\s` and `\S` (`:63-105`).
- **Word delete**: `ESC DEL`, `ESC BS` or `\x17` (Ctrl-W, "Warp: Ctrl+W") (`:572-585`). The comment notes "macOS Terminal sends plain \x7f ... no modifier info" (`:576`).
- **Forward delete**: `ESC[3~` and `ESC[3;N~`. It sets a global timestamp so the inner input skips its own delete handling within 100 ms (`:561-570`; `index.js:43-49`). This is a cross-component race patch.
- **Undo/redo**: not in the snapshot. A multi-slot kill ring: not present (one slot only).
- **Vim/emacs modes**: none for the composer. Bare `j`/`k` exist only in list pickers that have no text field (`vim-navigation.test.ts:10-27`).
- **Wrapped visual rows**: Up/Down move by visual row with a sticky preferred column (`InputRich.tsx:1448-1541`) using `getVisualLines` (`helpers/text-layout.ts:18-43`). That function wraps by **UTF-16 code-unit count**, not display width, and wraps per character rather than per word. Ink renders with wrap-ansi, so on wide characters, emoji or word wrap the cursor's row model can drift from what is drawn.
- **Two-step boundary rule**: Up on the first visual row first jumps the cursor to 0; only the next Up recalls history or a queued message (`:1474-1486`). Down mirrors this (`:1543-1555`).
- **Grapheme clusters**: not handled. Backspace removes one UTF-16 unit (`index.js:139-142`), which splits surrogate pairs. Cursor rendering walks code points (`for (const char of value)`) while the offset counts UTF-16 units (`index.js:91-101`), a mismatch for astral-plane characters.
- **IME**: bug #4493 is documented twice. When "an IME commit [is] split across stdin reads", a stale effect "would clamp the caret short and drop the newer chunks" (`PasteAwareTextInput.tsx:177-187`; `index.js:57-63`). The fix tracks the latest value in a ref.
- **Cursor sentinel**: the cursor is drawn with a private-use sentinel `\u{10FFFD}` so that wrap-ansi's trimming does not eat an inverse space at a line edge (`index.js:5-10,193`).

### 4. History
- **Scope**: in memory, per session and per component (`useState<string[]>([])`, `InputRich.tsx:1065`). Not persisted across runs in the snapshot.
- **Size and dedup**: capped at 500 entries after LET-13148, which notes "Without a cap the array grows for the whole session ... pinning every submitted prompt — including large pasted ones" (`components/input-history.ts:1-20`). Only a repeat of the most recent entry is dropped, compared ignoring surrounding whitespace (`:16-17`).
- **Draft preservation**: the draft is parked in `temporaryInput` on the first Up and restored when Down passes the newest entry (`InputRich.tsx:1513-1520,1568-1573`). Typing while browsing leaves history mode and keeps the edited text (`:1595-1603`).
- **Prefix filtering and reverse search (Ctrl-R)**: not in the snapshot.
- **Trap: recalled pastes go out as literal text.** History stores display text. After a successful submit the registry entries are freed, so "recalled history placeholders already degrade to literal text" (`InputRich.tsx:1089-1091`). Resubmitting such an entry sends the literal `[Pasted text #3 +40 lines]`.

### 5. Mentions and completion
- **`@` file completion** is "Ported from Pi TUI packages/tui/src/autocomplete.ts" (`helpers/file-autocomplete.ts:17-18`).
  - It uses `fd --hidden --follow --exclude .git`, plus `--full-path` when the query contains `/` (`:162-187`).
  - It supports quoted `@"..."` paths, `~/` and absolute scopes (`:627-687`).
  - Choosing a file appends a space; choosing a directory does not (`:581-583`).
  - **Trap: it downloads a binary at runtime.** If `fd` and `fdfind` are both absent, it downloads `fd` from GitHub releases at runtime (`:470-516,539-563`).
- **Slash completion** is a case-insensitive **substring** match (`SlashCommandAutocomplete.tsx:225-229`). Mod commands show their args inside the description (`:161`).
  - **Tab** fills in the command text; **Enter** runs the highlighted match immediately (`use-autocomplete-navigation.ts:77-92`; `InputRich.tsx:1690-1724`).
  - With no matches, submit is not blocked, so unregistered commands still run (`SlashCommandAutocomplete.tsx:248-255`).
- **Ghost text**: none. **Inline argument hints**: none.
- **Idle placeholder**: rotating hints such as `Try "debug this error"`, changing every 6 s (`InputRich.tsx:77-85`).

### 6. Modes
- **`!` shell mode**: a `!` typed into an empty input is consumed before it renders, "no flicker" (`PasteAwareTextInput.tsx:610-618`). The prompt character becomes `!` (`InputRich.tsx:975`) and the status line shows `⏵⏵ bash mode (backspace to exit)` (`:196-206`).
  - Backspace on an empty input leaves the mode. Exit is armed on entry, so the first empty backspace exits at once (`:1176-1194`).
  - The mode stays active after a command runs (`:1641`).
  - Input is locked while a command runs ("LET-7199", `:1628-1629`).
  - Bash runs the display text verbatim and never resolves placeholders (`:1637-1639`).
- **Shift-Tab** cycles `unrestricted → acceptEdits → standard` (`:1417-1436`). `strict` is not in the cycle, and `unrestricted` is the default (`permissions/mode.ts:10`). The mode shows as a transient status hint, e.g. `standard (request approval) mode ▶ (shift+tab to cycle)` (`:87-114,209-227`).
- **Plan mode**: not in the snapshot.
- **Tab with no autocomplete open and the agent idle** cycles reasoning-effort tiers (`:1396-1407`).

### 7. While busy
- **Type-ahead**: the composer stays live during a turn, and Enter queues the message. Queued messages render above the input (`components/QueuedMessages.tsx`).
- **Queue editing**:
  - The hint reads `press ↑ to edit queued message · ctrl+d to hold queue until done` (`InputRich.tsx:236-245`).
  - Up at position 0 pulls **all** queued user messages into the composer joined with `\n` and removes them from the queue (`:1488-1506`; `app/AppCoordinator.tsx:3876-3907`).
  - That removal is race-guarded: "if removeItem returns null, the item was already dequeued ... should NOT be loaded into the input (it would cause a double-send)" (`AppCoordinator.tsx:3882-3886`).
- **Ctrl-D** toggles `immediate` and `defer` while messages are queued (`InputRich.tsx:1343-1352`).
  - In `immediate` mode, queued messages are bundled with tool results mid-turn, which is effectively steering at the next tool boundary (`app/use-conversation-loop.ts:2019-2044`).
  - `defer` waits for `end_turn`. Defer is disabled for local agents because the local backend "fires end_turn between each sequential tool call" (`AppCoordinator.tsx:1627-1638`).
- **Esc while streaming** interrupts and leaves the draft untouched (`InputRich.tsx:1310-1316`). Queued user messages are **parked**, not sent or lost: "Esc stops the turn and holds them until Enter on an empty input or the next message" (`app/use-interrupt-handler.ts:281-284`). Esc also interrupts a running bash command (`InputRich.tsx:1304-1308`).
- **Esc when idle with text** needs a double press within 2.5 s to clear (`:74-75,1322-1338`). The prompt reads `Press Esc again to clear` (`:191`).
- **Ctrl-C**:
  - It interrupts only a running bash command (`:1364-1368`). Otherwise the **first press wipes the draft immediately** and starts a 1 s window; the second press exits (`:1370-1385`). The status line reads `Press CTRL-C again to exit`.
  - The help dialog says `Ctrl+C — Interrupt operation / exit (double press)` (`HelpDialog.tsx:77-80`), but in the snapshot Ctrl-C does not interrupt an agent turn. Ink's built-in handler is off (`exitOnCtrlC: false`, `index.ts:2571`).

### 8. Drafts
- **After a failed or rejected submit** the text is restored: `if (!result.submitted) setValue(previousValue)` (`InputRich.tsx:1661-1665`).
- **After a stream error** the last dequeued message is restored through `restoredInput` (`use-conversation-loop.ts:2603-2607`). That restore applies **only if the composer is empty**; otherwise "don't clobber - drop the restored value instead" (`InputRich.tsx:1117-1127`).
- **Stashing, crash recovery and on-exit persistence**: not in the snapshot.
- **External editor ($EDITOR)**: not in the snapshot for the composer.

### 9. Traps and gotchas (quoted)
- **Silent edits to user config.** `keybindings.json` is rewritten on first run with no prompt. The JSONC handling is `jsonc.replace(/\/\/.*$/gm, "")` (`terminal-keybinding-installer.ts:108-120`), which also deletes `//` inside strings such as URLs. The file is written back as `JSON.stringify` (`:220-223`), so all user comments are lost (a backup is made, `:203`).
- **`↵` round-trip.** Any literal `↵` the user types is submitted as a newline (`paste-registry.ts:37-38`).
- **Display and actual offsets diverge.** On paste, `actualValue.slice(0, at)` uses the *display* caret offset (`PasteAwareTextInput.tsx:287-288`). After an earlier placeholder, `at` points to the wrong place in the actual string. This is masked because submit re-resolves from the display text (`:717-722`).
- **Queued messages lost on stream error.** After restoring only the last dequeued message, the loop runs `tuiQueueRef.current?.clear("error")` ("Clear any remaining queue on error", `use-conversation-loop.ts:2609-2610`). The handler only logs and empties the display (`helpers/tui-queue-runtime.ts:50-55`).
- **Ctrl-C destroys the draft** on the first press with no undo (`InputRich.tsx:1374-1378`). The first Ctrl-K/Ctrl-U kill can be yanked back, but a Ctrl-C wipe cannot.
- **Enter on the autocomplete list runs the highlighted substring match**, not the literal text typed (`use-autocomplete-navigation.ts:87-91`). Typing `/mod` and pressing Enter runs whatever is highlighted.
- **Escape stripping.** A lone ESC is removed from text so it can drive the double-escape logic (`PasteAwareTextInput.tsx:620-634`). A separate raw-stdin listener cancels commands on a bare `\u001b` (`InputRich.tsx:1254-1265`).
- **WezTerm Delete bug** (wezterm issue 3758) is fixed by injecting Lua into the user's config (`installer.ts:331-380`).

### letta-code: steal
- Separate display text from actual text: placeholder chips, plus a release-at-boundary registry that knows about live holders (kill buffer, parked history draft, queue, in-flight submit).
- On interrupt, park the queue instead of dropping or auto-sending it. Recall the queue with Up into a single editable draft, guarded against the double-send race.
- Give the queue an `immediate`/`defer` toggle (Ctrl-D) with a visible hint.
- Use a two-step Up/Down at buffer edges (first to the edge, then history) with a sticky preferred column.
- Make `!` at an empty input a zero-flicker mode switch and leave with backspace on empty. Lock input while a shell command runs.
- Enable kitty flag 1 only, pop it on every exit path, and give users a debug env switch (`LETTA_DEBUG_INPUT`, which hex-dumps raw input).
- Add the Linux bare-Enter-after-modified-Enter suppression window.

### letta-code: traps
- Do not silently rewrite terminal or editor config files, and especially do not strip JSONC with a regex.
- Visual-row math in UTF-16 units against a width-aware renderer; per-unit backspace.
- Visual stand-ins (`↵`) that collide with real user characters.
- Destructive first Ctrl-C, and a help text that promises an interrupt the code does not perform.
- Recalled history whose placeholders have already been freed.
- Queue cleared on error; a restored message dropped if the user has typed anything.
- Downloading binaries at runtime for completion.
- Cross-component timestamp handshakes (`__lettaForwardDeleteTimestamp`) to stop double handling.

---

## goose CLI (Rust, rustyline 18)

The composer is a stock rustyline `Editor` (`session/mod.rs:645-662`) with custom bindings set up on every prompt (`session/input.rs:142-181`). It is active only between turns. Files: `session/input.rs`, `session/paste.rs`, `session/completion.rs`, `session/editor.rs`.

### 1. Submit and newline
- **Enter** and **Ctrl-M** are bound to `PasteAwareEnterHandler`. It submits (`Cmd::AcceptLine`) unless the Enter is the leading newline of a Windows paste burst (`input.rs:156-168`; `paste.rs:306-316`).
- **Ctrl-J** inserts a newline (`Cmd::Newline`). The key is configurable through `GOOSE_CLI_NEWLINE_KEY`, but `m` and `c` are rejected because they "would otherwise shadow the paste and interrupt handlers" (`input.rs:88-101,170-176`).
- **Shift-Enter, Alt-Enter and backslash-Enter** are not bound. The `Validator` always returns `Valid` (`completion.rs:568-575`), so there is no continuation.
- **Terminal capability detection and kitty/CSI-u**: not in the snapshot.
- **Empty-prompt ghost hint**: `Enter to send · Ctrl+J newline` (`completion.rs:536-539`).

### 2. Paste
- **Off Windows**, the code relies on rustyline's native bracketed paste ("rustyline handles pastes natively via bracketed paste off Windows", `paste.rs:3-4`). No chip is shown: the full paste lands in the buffer.
- **On Windows**, a paste-burst detector (`paste.rs:1-9`):
  - It peeks `GetNumberOfConsoleInputEvents`, with a threshold of 2 or more queued events (`:15-18,40-55`).
  - It counts a burst as a paste "only when a newline is followed by more input", so fast type-ahead ending in Enter still submits (`:186-201`).
  - It drains the burst directly with `ReadConsoleInputW`, polling 4 × 2 ms across gaps between chunks (`:93-134`).
  - It handles emoji that arrive on the Alt (`VK_MENU`) key-up from Alt+numpad injection (`:62-84`).
- **Chips** read `[Pasted {lines} lines #{id}]` or `[Pasted {n} chars #{id}]`, at 2 or more lines or 400 or more characters (`:19-22,327-337`). The id exists "so a cleared chip can never be expanded into a later, identical-looking one" (`:325-328`).
  - Chips expand on submit, by position, so "reordering them in the prompt still expands every block" (`:339-362`).
  - Chip state is created fresh for each prompt (`input.rs:147`). History stores the **expanded** text (`input.rs:192-195`, after `read_paste_aware_input`).
- **Image paste and file-drop handling**: not in the snapshot.

### 3. Editing
- **Emacs by default.** `EDIT_MODE=vi` selects vi (`session/builder.rs:840-850`; `mod.rs:650-653`).
- **Kill ring, word motion, undo, grapheme and width handling** all come from rustyline and are not overridden here. The rustyline source is not vendored, so the details are unverified.
- **Ctrl-C with text** issues `Cmd::Kill(Movement::WholeBuffer)` (`input.rs:68-72`).
- **Up/Down by wrapped row and IME handling**: no custom code (rustyline defaults).

### 4. History
- **Scope**: global, a single file at `Paths::state_dir()/history.txt`. It migrates from `config_dir/history.txt` and deletes the old file after a successful save (`mod.rs:189-231`).
- **Saving**: the file is saved after every input kind (`mod.rs:676-786`). Blank input is not recorded (`input.rs:193`).
- **Dedup, size, prefix search and Ctrl-R**: rustyline defaults, with no config set (`mod.rs:648-653`).
- **Draft preservation while browsing**: handled by rustyline.
- Lines sent through the external editor are added explicitly (`input.rs:134`; `mod.rs:749`).

### 5. Mentions and completion
- **No `@` mentions.** Plain text gets rustyline `FilenameCompleter` on the last whitespace-delimited word (`completion.rs:380-406`). Completion runs only with the cursor at end of line (`:418-421`), and the list type is `CompletionType::Circular` (`mod.rs:649`).
- **Slash completion** covers command names, `/prompt` names, `--info`, `key=` argument names from MCP prompt schemas, `/prompts --extension`, `/model` names from a cache, `/mode` values and `/skills` names (`completion.rs:30-380,422-504`).
- **Ghost text**: only the status hints (see 7). There are no inline argument hints.

### 6. Modes
- **`/mode <name>`** sets the GooseMode (`input.rs:292-294`). Modes are listed from `GooseMode::VARIANTS`.
- **Prompt indicator**: there is none; the prompt is always `"> "` (`paste.rs:368`).
- **`!` shell mode and a plan toggle**: not in the snapshot.
- **`/r`** toggles full tool output; **`/t`** sets the theme.

### 7. While busy
- **No live composer during a turn.** rustyline is not reading. **Type-ahead, queued messages and steering**: not in the snapshot.
- **Ctrl-C during a turn** is caught by a `tokio::signal::ctrl_c` task that cancels the token (`mod.rs:1319-1325`). The session then:
  - answers pending tool calls with the error `"Interrupted by the user to make a correction"`,
  - injects a **synthetic assistant message** `"Yes — what would you like me to do?"` (`mod.rs:1646-1690`),
  - sets the next prompt's hint to `Interrupted, what should goose work on instead?` (`completion.rs:530-532`).
- **Ctrl-C at the prompt with text** clears the buffer. On an empty prompt the first press shows `Press Ctrl+C again to exit, or type new instructions to continue` and the second exits (`input.rs:59-86`; `completion.rs:533-535`). Typing resets the hint (`completion.rs:518-522`).
- **On a stream error**, the conversation is rolled back to the last user message ("We've removed the conversation up to the most recent user message", `mod.rs:1513-1523`).

### 8. Drafts
- **External editor**: `/edit [text]` opens `GOOSE_PROMPT_EDITOR`, then `$VISUAL`, then `$EDITOR` (`editor.rs:13-38`). The temp file is a random `goose_prompt_*.md` holding a `# Your prompt:` section plus `# Recent conversation for context (newest first):` with `## User:`/`## Assistant:` headings (`editor.rs:91-123`).
  - `extract_user_input` takes the text between those markers (`:196-229`).
  - Editor commands containing quotes go through shlex; otherwise the command is split on whitespace "to avoid shlex stripping backslashes from Windows paths" (`:125-139`).
  - Setting `goose_prompt_editor` makes the editor the default for **every** prompt unless `goose_prompt_editor_always false` is set (`input.rs:103-114,125-140`). Empty editor output falls back to the inline prompt.
- **Inline stash, crash recovery and draft persistence**: not in the snapshot.
- **`/edit` cannot carry the current inline buffer** into the editor: the prefill is the text typed after `/edit`.

### 9. Traps and gotchas (quoted)
- **Typing `exit` or `quit` as plain text** (case-insensitive) ends the session (`input.rs:199-208`).
- **Unknown slash commands are sent to the model** as messages (`input.rs:214-217`). Messages are `trim()`ed, so leading indentation in pasted code is lost (`:210`).
- **Editor failure loses the draft.** A non-zero exit (e.g. vim `:cq`) gives `Failed to open editor` and the text is gone with the temp file (`mod.rs:754-756`; `editor.rs:164-169`). Saving and quitting **sends immediately**, with no review in the composer (`mod.rs:748-752`).
- **Marker collision.** A user line containing `# Recent conversation for context` truncates the prompt (`editor.rs:203-214`).
- **Past temp-file attack.** A security regression test exists because the editor used to write a **predictable** `.goose_prompt_temp.md` in the cwd, which made a symlink exfiltration attack possible (`editor.rs:540-613`, asserting `assert_ne!(edited_path, Path::new(".goose_prompt_temp.md"))`).
- **Windows type-ahead heuristic.** Pasted-burst detection must tell "fast typing / type-ahead" apart from paste. A single typed line plus Enter queued together must still submit (`paste.rs:186-190`).
- **Changelog**: `goose-cli` has no CHANGELOG in the snapshot. The other crate changelogs were not composer-related.

### goose: steal
- Per-instance chip ids so a deleted chip can never resolve to a later identical one; position-based expansion that survives reordering; history storing the expanded text.
- A configurable newline chord with validation against keys that would shadow other handlers.
- An empty-prompt ghost hint that changes with state (default, maybe-exit, interrupted) and resets on the first keystroke.
- An editor template with the recent conversation as read-only context and the prompt section first; random temp files; the cwd preserved.
- Windows console burst detection that peeks queued input without consuming it.

### goose: traps
- Magic plain-word commands (`exit`/`quit`) and unknown slash commands silently going to the model.
- No composer during a turn, so no type-ahead or steering. Interrupting fabricates an assistant turn.
- Losing the external-editor draft on a non-zero exit, and sending immediately on save.
- In-band text markers that the user's own content can collide with.
- Trimming the whole message.

---

## Elegant ideas worth stealing

1. **Negotiate keys from the terminal's replies, not from a timer, then fall back in layers.**
   - pi sends `\x1b[>7u\x1b[?u\x1b[c`. A DA1 reply that arrives before any Kitty reply switches to `modifyOtherKeys` with no startup timeout (pi tui/src/terminal.ts:12-14, :258-290). This fixed the intermittent Shift+Enter failures (#5188).
   - For Terminal.app and Windows, which send plain `\r` for Shift+Enter, pi asks the OS whether Shift is physically held (terminal.ts:53-61, :352-361).
   - hermes aliases Shift+Enter and Ctrl+Enter across CSI-u, `27;m;13~` and the CapsLock/NumLock modifier twins (pt_input_extras.py:92-122, :163-317).
   - letta enables only Kitty flag 1 because flag 2 "causes release events that leak into input" (kitty-protocol-detector.ts:142-148).
2. **Make `\`+Enter a universal newline and apply it before every other Enter rule**, so typed and pasted input behave the same (hermes cli_tui_mixin.py:1434-1444). pi also has the reverse: with Shift+Enter as submit, `\`+Shift+Enter submits.
3. **Large-paste chips that are atomic for editing, carry a unique id and expand only at submit.**
   - pi: `[paste #N +L lines]` is merged into a single grapheme for movement, deletion and wrapping (editor.ts:41-100). Undo snapshots include the paste registry.
   - goose: ids are never reused, "so a cleared chip can never be expanded into a later, identical-looking one", and expansion is by position (paste.rs:325-362).
   - letta: registry entries are freed only at boundaries, and entries still held by the kill buffer, history draft, queue or in-flight submit are kept (InputRich.tsx:1086-1103).
   - vtcode keeps the real text in the buffer and only renders it collapsed: "paste-once summarizes and paste-twice reviews" (editing.rs:172-202).
   - kimi expands chips for `$EDITOR` and re-folds them afterwards with a diff (placeholders.py:313-389).
4. **Treat the paste and a following Enter as one event.** omp emits the paste with its leading Enter, and queues trailing bytes until any async image paste finishes, so "submit [never] runs against an empty `pendingImages`". It also skips the large-paste menu when a submit is already in the burst (omp stdin-buffer.ts:606-610; prompt/custom-editor.ts:1444-1464; input-controller.ts:2478-2492).
5. **Detect unbracketed pastes, but bias toward typed input.**
   - omp holds the first read that contains a line break for 10 ms and only calls it a paste after *two* completed line breaks. Once mode 2004 is confirmed, an unbracketed burst is assumed to be an IME commit unless an event-loop stall probe fires (stdin-buffer.ts:85-108, :420-430, :650-700).
   - hermes treats an Enter within 50 ms of the last change as a newline (cli_tui_mixin.py:46, :1446-1452).
   - goose peeks at the Windows console queue without consuming it, and counts a burst as a paste "only when a newline is followed by more input".
6. **Watchdogs on bracketed paste.** If `ESC[201~` never arrives, flush after an inactivity timeout (omp 1 s with a 64 MiB cap; hermes 2 s). Without one, input looks dead and memory grows without bound.
7. **Emacs editing done properly.**
   - pi's kill ring accumulates consecutive kills (prepending on backward kills, appending on forward kills), and yank-pop works only right after a yank (kill-ring.ts; editor.ts:1624-2029).
   - Fish-style undo coalescing: a word is one undo unit, each space separately (editor.ts:1203-1213).
   - Programmatic `setText`, including the Ctrl+C clear, is undoable (:1119-1122).
   - vtcode adds redo, transposition (Ctrl+T, Alt+T), case changes (Alt+U/L/C) and `Alt+\` (events.rs:441-569).
8. **Vertical motion by wrapped visual row with a sticky column, and a two-step boundary rule.**
   - The first Up on the top row goes to column 0; only the next Up enters history (pi editor.ts:925-938; crush history.go:50-68; letta InputRich.tsx:1474-1486; vibe text_area.py:484-494).
   - vtcode keeps Up/Down purely spatial in a multi-row composer and always uses Ctrl+P/N for history (events.rs:393-428).
   - pi documents the sticky-column rule as a decision table (editor.ts:1560-1611).
9. **History browsing never mutates entries and always gets back to the draft.**
   - Editing a recalled entry turns it into the new draft (crush history.go:107-112).
   - Esc returns to the draft.
   - Shell entries are stored with their `!` so recalling one restores the mode (crush history.go:40-42; vibe body.py:172-190).
   - History entries carry their attachments and paste registries (vtcode input_manager.rs:96-139; omp editor.ts:518-526).
10. **Persistent, searchable history with dedup by count.**
    - omp stores history in SQLite with a `UNIQUE` prompt, `use_count`, cwd and session_id, searched by FTS5 prefix on Ctrl+R (history-storage.ts:58-130, :234-247).
    - vibe merges concurrent sessions atomically off the UI thread (history_manager.py:79-118).
    - vtcode's Ctrl+R picker shows time and session labels.
11. **Busy-time input as distinct verbs.**
    - Steer, which injects at the next tool boundary: pi Enter, vtcode Enter, kimi Ctrl+S, hermes `busy_input_mode=steer`.
    - Follow-up, which waits for the turn to end: pi Alt+Enter, omp Ctrl+Q/Ctrl+Enter, vtcode Ctrl+Enter.
    - Local commands (`/…`, `!…`) never steer (hermes cli_tui_mixin.py:1453-1456).
    - omp's `->`/`=>` prefix queues the message, and a numbered list splits into several queued messages (queue-input.ts).
12. **Queues stay editable.**
    - vibe: Up enters a selection mode with in-place edit that keeps the item's ID and FIFO slot, Backspace removes an item, and Esc restores the draft. If an item was promoted while being edited: "This message was already processed — press Enter to submit as new, or Escape to discard." (body.py:512-527).
    - letta: Up pulls the whole queue back in, guarded against double-send (AppCoordinator.tsx:3882-3886). On interrupt it *parks* the queue rather than dropping or sending it (use-interrupt-handler.ts:281-284).
    - pi/kimi show queued items dim, with a hint line such as `↳ Alt+Up to edit all queued messages`.
13. **Reflex keys that cannot destroy work.**
    - hermes double-Esc clears the draft but appends it to history first, "which is what makes a reflex key safe" (cli_tui_mixin.py:1072-1086).
    - omp: "Esc must not destroy an in-progress draft." (input-controller.ts:575-577).
    - Ctrl+D deletes forward and exits only on an empty draft (pi custom-editor.ts:117-125; hermes :1143-1151).
14. **A drafts stash.** hermes Ctrl+S is a 20-entry stack: push and clear when there is text, pop or browse when the composer is empty. Restore merges attachments. It is suppressed during secret prompts "so Ctrl+S can't stash a password" (prompt_stash.py:17-18; cli_tui_mixin.py:1784-1801, :1981-2004).
15. **External editor done well.**
    - crush opens `$EDITOR` at the cursor's line and column (ui.go:4478-4495).
    - pi treats a non-zero exit as cancel and keeps the draft (external-editor.ts:34-36).
    - goose's template puts the prompt first, with the recent conversation below as context, and uses random temp files after a predictable-path symlink attack (editor.rs:91-123, :540-613).
16. **Modes shown as consumed prefixes.** The first `!`/`/`/`&` turns into a prompt glyph, Backspace on an empty composer exits the mode, and history round-trips it (vibe text_area.py:651-669; letta PasteAwareTextInput.tsx:610-618, `⏵⏵ bash mode (backspace to exit)`; vtcode border title `" ! Shell mode "`). pi also offers `!!cmd` to run without adding it to the model's context.
17. **Guard magic words with confirmation, not silence.** omp requires a second Enter for a bare `exit` or a slashless command name, with the message "add a leading space to send "x" as a message" (input-controller.ts:1040-1046).
18. **Stale-completion guards.**
    - A completion result is dropped unless text, line and column all match the request snapshot (pi editor.ts:2415-2429).
    - Moving the cursor re-queries the menu (:1943-1953).
    - Completion runs on a worker thread because rg/fd would "stall the render loop per keystroke" (hermes :2225-2229).
19. **Hardware-cursor marker for IME.** A zero-width marker is emitted at the caret even while a menu is open, so the IME candidate window lands correctly (pi editor.ts:566-569). Background turns are deferred while the operator is typing, so they do not break CJK composition (kimi `__init__.py:77, :1043-1060`).
20. **Space-hold push-to-talk via auto-repeat cadence.** "OS key-repeat is metronomic; a human smashing the bar is fast but irregular" (omp space-hold.ts:1-20).

## Traps to design against

1. **Paste-registry desync.** Most chip bugs are lifecycle bugs:
   - pi #6844 and #6397: corruption after delete and undo.
   - Inferred in pi: `restoreQueuedMessagesToEditor` calls `setText` on the unexpanded draft, which clears the registry, so Esc-while-streaming turns `[paste #1 …]` into dead text (interactive-mode.ts:4637-4640 with editor.ts:1123).
   - Inferred in pi: paste ids restart at 1 after submit while the kill ring persists (editor.ts:1368-1369).
   - letta: recalled history sends `[Pasted text #3 +40 lines]` as literal text (InputRich.tsx:1089-1091).
   - kimi/letta: one edited character inside a plain-text token makes it go out literally.
   - hermes: the chip points at a file that may vanish.
   - pi: typing the literal marker text expands it (editor.ts:1089-1096).
   - Rule: ids are unique per process, entries are released by reachability, and an unresolved chip is an error, never silent text.
2. **Visual stand-ins that collide with real input.** letta shows newlines as `↵` and turns *every* `↵` back into `\n` (paste-registry.ts:37-38). It also treats a literal two-character `\r` as Shift+Enter (PasteAwareTextInput.tsx:524-530). goose truncates a prompt at a user line equal to its template marker (editor.rs:203-214).
3. **Code units used as columns.** pi's sticky column (editor.ts:1490-1495) and letta's `getVisualLines` (text-layout.ts:18-43) count UTF-16 units. letta's Backspace splits surrogate pairs (ink-text-input index.js:139-142). crush counts bytes as "columns" for its paste threshold (ui.go:5949). Measure in cells over grapheme clusters everywhere.
4. **Shift+Enter is never solved by one mechanism.**
   - pi, hermes and letta each carry three or four paths.
   - An eager Esc binding can swallow Alt+Enter (inferred in kimi, prompt.py:1428-1433).
   - SSH latency splits `ESC CR` into a lone Escape (pi #7899, `PI_TUI_ESC_TIMEOUT`).
   - Linux delivers a stray `\n` after a modified Enter (letta use-input.js:108-114).
   - Kitty flag 2 leaks release events (letta).
   - Ghostty's Kitty disambiguate mode strips Alt from Backspace (hermes cli_terminal_input.py:351-361).
   - Enabling CSI-u killed Ctrl+C: "Ctrl+C arrived as ESC[99;5u and died, #56684".
   - NumLock adds 128 to the modifier.
5. **Terminal replies leaking into the composer.** CPR replies, SGR mouse reports (including `NaN` coordinates from VS Code), OSC 11 colour replies, focus `ESC[I`/`ESC[O`, X10 mouse after a tmux reattach, tmux re-encoding paste control bytes as `ESC[106;5u` or `ESC[27;5;106~`, and VS Code sending space as `\x1b[32u` (hermes cli_terminal_input.py:309-326, :485-518; vibe terminal_input_filter.py; pi editor.ts:1266-1276; omp bracketed-paste.ts:6-41).
6. **Unbracketed multiline input submitting line by line**: tmux strips 2004, and IME, dictation or voice tools type their text.
   - Over-correcting also hurts. hermes' 50 ms rapid-Enter rule turns a fast typist's Enter into a newline.
   - kimi had to raise its paste thresholds for dictation, and lower them for XShell CJK IME breakage.
   - omp needed a stall probe because event-loop stalls batch real Enters (#12540, #13344, #13440).
7. **Destructive reflex keys.**
   - The first Ctrl+C wipes the draft with no undo (letta InputRich.tsx:1374-1378; libraries in kimi and aider).
   - One Esc while busy clears the *whole queue* (crush ui.go:5326-5337).
   - Ctrl+C silently pops the last queued item (vibe app.py:6097-6128).
   - A 0.2 s double-Esc clears without saving (vibe).
   - Ctrl+D exits with a draft present (vtcode; omp fixed this, CHANGELOG:884).
   - aider's clipboard watcher overwrites the draft (copypaste.py:28-31).
8. **Overloaded keys and magic words.**
   - Tab queues the draft (vtcode events.rs:706-728).
   - Empty Enter while busy steers (vibe).
   - Plain Enter steers when the user may have meant to queue (vtcode, pi).
   - Enter on a slash suggestion submits, so there is no chance to add arguments (vibe, kimi, pi), and it runs the highlighted substring match rather than what was typed (letta).
   - Typing `exit` or `quit` ends the session or cannot be sent (goose, crush).
   - Unknown slash commands go to the model (goose).
   - Ctrl+R hijacked for a dock or voice removes reverse search (hermes, vibe).
   - Ctrl+B shadowed (vtcode).
9. **Queue semantics that surprise.**
   - LIFO recall next to FIFO steer on the same queue (kimi).
   - The queue cleared on a stream error, and the restored message dropped if the user has typed (letta use-conversation-loop.ts:2609-2610; InputRich.tsx:1117-1127).
   - Prompts typed during a turn missing from saved history (kimi prompt.py:2059-2062).
   - A rewind hiding the queue bar (omp #13680).
   - A submit right after Esc being lost or stalled (vibe CHANGELOG:363; the 30 s settle gate).
   - Optimistic busy flags so "esc pressed right after enter" routes correctly (crush ui.go:5117-5124).
10. **Silent caps and truncation.**
    - vtcode refuses typed newlines past 10 lines, while pastes bypass the cap (editing.rs:165-167, :248-265).
    - vtcode archived history keeps only first lines of 200 chars, then recalls them as if complete.
    - Whole-message `trim()` destroys leading indentation in pasted code (pi editor.ts:1365; goose input.rs:210; crush editor result).
    - vtcode drops a pasted CR instead of turning it into LF (editing.rs:22-24).
11. **External editor pitfalls.**
    - A `$EDITOR` string split on spaces (pi external-editor.ts:19).
    - The draft lost on a non-zero exit, and save meaning send (goose; hermes "save == send").
    - A predictable temp path open to symlink attack (goose regression test).
    - Windows `spawnSync` racing vim for console input (pi :22-24).
    - Slow startup when tmpdir holds many entries (pi #6903).
    - `buffer.tempfile` raising EEXIST in prompt_toolkit (hermes :2231-2233).
12. **Modifying user configuration.** letta silently rewrites the VS Code `keybindings.json`, stripping JSONC with `/\/\/.*$/gm` (which destroys URLs and drops every comment), and injects Lua into `wezterm.lua` (terminal-keybinding-installer.ts:108-120, :220-223, :331-380). It also downloads `fd` from GitHub at runtime when it is missing (file-autocomplete.ts:470-516).
13. **Cross-component input races.** letta uses a global `__lettaForwardDeleteTimestamp` so two layers do not both handle Delete. An IME commit split across stdin reads was clamped by a stale effect (#4493). mistral-vibe double-inserted a paste unless `event.stop()` was called. Have a single owner for each key event.
14. **Submitted text that visibly lingers.** It "look[s] unsent, inviting a re-submit" (hermes :1488-1491). omp's post-submit clear erased text typed right after Enter (#13440). Clear only what was submitted, compared by revision.
15. **Documentation drifting from behaviour.** letta's help says Ctrl+C interrupts the agent, but it does not. vtcode's README says history is "currently disabled", but it is enabled. crush's comment says "remove it and add a newline", but no newline is added (ui.go:3170-3178).
