# Prompt composers in terminal coding agents: comparative source study (A)

Date: 2026-10-19. This is a read-only study of the snapshots under `blackbird/quarantine/` (taken around 2026-09-30). Nothing was built, run or installed. Citations are file:line relative to each reference root. For Claude Code, `bin@N` is a byte offset in the 2.1.286 linux-x64 bundle and `CL:N` is a line in `claude-code-official-public/CHANGELOG.md`. Per-reference detail follows the summary; each part covers the nine topics and ends with its own steal and avoid lists.

Evidence quality differs by reference:

- **Claude Code:** a rich changelog of about 80 composer fixes, so its traps are observed failures.
- **Codex:** no real changelog in the snapshot. Its traps come from guard comments in the code.
- **Gemini:** release notes describe features only. Its traps come from code comments, guard code and issue references.
- **opencode:** neither a changelog nor issue references in this area, so its traps come from reading the code and are likely but unconfirmed. Its editor library (OpenTUI) is also not in the snapshot.

## At a glance

| Topic | Claude Code | Codex | Gemini (qwen differences) | opencode |
|---|---|---|---|---|
| Submit / newline | Enter submits. `\`+Enter, Shift/Alt+Enter and Ctrl+J insert a newline. Ctrl+Enter or `ctrl+x ctrl+s` is send-now. | Enter submits. Ctrl-J, Ctrl-M, Shift-Enter and Alt-Enter insert a newline. No `\`+Enter. | Rebindable table: Ctrl/Cmd/Alt/Shift+Enter, Ctrl+J and `\`+Enter insert a newline. | Enter submits. Shift/Ctrl/Alt+Return and Ctrl+J insert a newline. No `\`+Enter. |
| Terminal setup | `/terminal-setup` for VS Code, Cursor, Windsurf, Alacritty, Zed and Apple Terminal (Option as Meta). Kitty flags `CSI >5u` plus `CSI >4;2m`; probes with a deadline; known-terminal allowlist. | Startup kitty probe. Event-type reporting is skipped on Ghostty, iTerm2 and tmux without csi-u. The footer hint names Shift-Enter or Ctrl-J depending on what works. | `/terminal-setup` makes VS Code-family terminals send `\`+CRLF. One batched probe: Kitty, then modifyOtherKeys, then DA1. | Always requests kitty. No setup command. |
| Large paste | `[Pasted text #N +L lines]` above 800 chars or `min(rows-10,2)` newlines. Atomic. Pasting the same text again expands it. Expands on submit. | `[Pasted Content N chars]` above 1000 chars. Atomic. Expands on submit, on queue and in the editor seed. Cannot be expanded in place. | `[Pasted Text: N lines]`. Atomic. Expands in place with Ctrl+O or double-click. Kept in undo. | `[Pasted ~N lines]` at 3+ lines or 150+ chars. Expands at submit and on copy. |
| Paste without brackets | Any single key event over 800 chars counts as a paste. An Enter mid-paste waits. | Burst state machine: 3 chars, 8ms, 120ms Enter-as-newline window, non-ASCII never held. | Outside Kitty, Enter within 30–40 ms becomes a newline. (qwen: 1 s stuck-paste timeout; Ctrl+C escapes.) | None. |
| Undo | Ctrl+_ with 50 steps. No redo found. | Vim mode only: 64 steps or 1 MiB. | Snapshot per keystroke, capped at 100. Ctrl+Z undoes or suspends depending on state. | Ctrl+- / Ctrl+. (undo/redo). |
| Kill ring | Emacs kill ring with yank-pop. | One slot that survives submit and `/new`. | None (Ctrl+Y is YOLO). | None. |
| Visual rows | Up/Down walk wrapped rows before history. | Up/Down walk wrapped rows with a sticky column. | Up goes to home first, then history. Code-point math, not graphemes. | Ctrl+A/E for logical line ends, Alt+A/E for visual ones. Two-stage Up. |
| Vim | Large vim mode. | Vim mode, which is where undo lives. | NORMAL and INSERT, counts, `.`, `f`/`t`. | None. |
| History | Global `~/.claude/history.jsonl` with pastes stored by hash. Ctrl+R scope cycles session / project / everywhere. Consecutive dedup. | Global text-only `history.jsonl` merged with session entries. Recall only on an empty composer. Ctrl-R substring search. | Per project. Edits to recalled entries cached per level. Ctrl+R search in the composer; Enter submits. | Global, 50 entries, keeps attachments and mode. A draft blocks Up. No Ctrl-R (it renames the session). |
| Clear saves draft | Double-Esc clear goes to history. | Ctrl-C clear goes to history. | No. | Ctrl+C clear of 20+ chars goes to history. |
| Mentions | `@` with a native fuzzy finder. Slash commands complete anywhere, with argument hints and ghost text. | `@` files (nucleo, gitignore), plugins and skills. `$` skills. Esc dismissal remembered per token. | `@` with a deferred 200 ms spinner. LLM ghost text hard-disabled. | `@` files, agents, MCP resources and aliases, `#10-20` line ranges, frecency. |
| Modes | `!` shell. `#` memory removed. Shift+Tab cycles default / accept edits / plan / auto, shown by border colour. | `!` shell in red. Shift-Tab plan mode. | `!` shell and `?`. Shown by prompt glyph, colour and aria-label. | `!` shell at column 0. Tab cycles agents. |
| While busy | Type ahead always works. Queued messages in gray, recall with Up. Esc returns the queue first, then interrupts. | Enter steers at the next tool call. Tab queues. Shift-Left or Alt-Up pops the last queued message. Interrupt merges the queue into the draft. | Enter queues, joined into one prompt. Up recalls all of it. (qwen: steers at tool boundaries; Ctrl+Q waits for idle.) | Server queue with a QUEUED badge. Esc twice within 5 s interrupts. |
| Quit ladder | Ctrl+C clears, then "Press Ctrl-C again to exit". | Double-press quit disabled as "janky" (`bottom_pane/mod.rs:228-233`). | Esc cancel keeps the draft; Ctrl+C cancel wipes it. | Ctrl+C or Ctrl+D on an empty prompt exits immediately. |
| Stash | Ctrl+S one-slot toggle, plus a tip when a long draft is cleared. | None. | None. (qwen: Ctrl+S stash to a per-project file, restored at next start.) | Palette-only commands, persisted to disk. |
| Crash recovery | Not found. | Only the startup draft, echoed if startup fails. | No. (qwen: through the stash file.) | No. |
| External editor | Ctrl+G or `ctrl+x ctrl+e`. Pastes expanded, collapsed again on save. Last response as `#` comments. Opens at the caret line. | Ctrl-G. Temp dir outside sandbox-writable roots. Attachments reconciled by surviving labels. | Re-collapses by first matching text, which is fragile. `$EDITOR` classified by substring. | `ctrl+x e`. Does nothing if neither `$VISUAL` nor `$EDITOR` is set. Pastes stay expanded afterwards. |

## Elegant ideas worth stealing (merged)

1. **One Return rule plus a fallback ladder.** Enter submits. Shift/Alt+Enter, Ctrl+J and a trailing `\`+Enter all insert a newline; Claude Code records that the user knows `\`+Enter so its tips stop. Ctrl+Enter is reserved for send-now. Where modified Enter is unreliable, fall back to a prefix chord such as `ctrl+x ctrl+s`. Gemini's `/terminal-setup` routes VS Code's Shift+Enter through the same `\`+CRLF path, so there is one mechanism, not two.
2. **A footer hint that names the newline key that actually works on this terminal** (Codex: Shift-Enter once confirmed, otherwise Ctrl-J).
3. **Terminal capability probes that never block.** Each capability is pending with a likely default, a deadline and a recorded reason (Claude Code). Gemini batches its probes into one write: Kitty, then modifyOtherKeys, then a DA1 sentinel. Skip event-type reporting where it is known to leak (Codex), and always pop keyboard modes on exit, suspend, crash and editor handoff.
4. **Large pastes as atomic, id-bearing spans backed by the original bytes**, not as text. Rules:
   - Delete them as a unit and record them in undo.
   - Expand them on submit, on queue, on copy (opencode), in the editor seed, and in place on request (Gemini Ctrl+O; Claude Code "paste again to expand").
   - Re-collapse after the editor returns by exact content match or id, never by first substring match.
   - Store payloads by hash so history recall restores them (Claude Code).
5. **A paste-burst detector for terminals without bracketed paste** (Codex):
   - Pure and clock-injected.
   - Holds the first ASCII char about 8ms instead of drawing it and then retracting it.
   - Never holds non-ASCII, so IME input is unaffected.
   - Pulls already-typed text into the paste only if it looks "pastey".
   - Has a 120ms window in which Enter becomes a newline, except for `/` lines.
   - Flushes on submit.

   Add qwen's guards on top: a 1 s stuck-paste timeout, Ctrl+C always honoured, and a progress indicator. Claude Code's matching rule: an Enter that arrives mid-paste waits until the paste is processed.
6. **Clearing is never destructive.** Ctrl-C or double-Esc clear writes the draft into history, so Up recovers it (Claude Code, Codex, opencode).
7. **History browsing keeps the draft.** Gemini keeps a per-level edit cache with the live draft as level -1. opencode's run mode saves the draft on the first Up and restores it past the newest entry. Use two-stage Up/Down: first walk wrapped visual rows or go to home, then enter history.
8. **A Ctrl-R picker:**
   - Shows the match in the composer as a preview.
   - Restores the original draft and cursor on cancel.
   - Accepts into the composer for editing rather than submitting (Gemini's Tab).
   - Has one key that cycles scope between session, project and everywhere (Claude Code).
9. **Steer versus queue as two explicit gestures.** Enter steers at the next tool boundary and Tab queues for end of turn (Codex; qwen's Ctrl+Q). Esc with pending steers means "interrupt and send now". Queued items stay visible and can be recalled for editing (Shift-Left or Alt-Up, or Up on an empty composer). On interrupt, the queue merges back into the draft and is not auto-sent. Record the mode at the moment of enqueueing (qwen #11626).
10. **A kill buffer that survives submit and `/new`** (Codex), or a full kill ring with yank-pop (Claude Code).
11. **Whole-draft undo snapshots with a byte budget.** Each snapshot covers text, paste payloads, attachments and mention bindings (Codex's 64 steps or 1 MiB).
12. **A one-slot Ctrl+S stash.** Claude Code adds a hint that appears when a long draft is cleared. qwen persists the stash to a per-project 0600 file restored at next start, which doubles as crash recovery.
13. **An external editor that helps.**
    - The template carries the last response as `#` comments, stripped on save, and opens at the caret line (Claude Code).
    - The temp dir sits outside sandbox-writable roots, and attachments are reconciled by surviving labels (Codex).
    - Placeholders are expanded on the way out and re-collapsed on the way back.
14. **Rich `@` mentions:** files, agents, MCP resources and line ranges `#10-20` (opencode), frecency, Tab descending into directories, a loading spinner only after 200 ms (Gemini), and Esc dismissal remembered per token occurrence (Codex).
15. **Mode shown redundantly:** prompt glyph, border colour, label, placeholder text and screen-reader label. Store the mode with each history entry.
16. **Typed text is never lost before the session is up.** Codex prints the startup draft if startup fails.
17. **Strip invisible Unicode format and tag characters at submit** and show the cleaned text, keeping ZWNJ (Claude Code).

## Traps to design against (merged)

1. **Bracketed paste fails in practice** (Claude Code changelog):
   - Paste mode is reset mid-session.
   - The end marker is lost.
   - Focus and mouse reports arrive inside the paste.
   - Newlines arrive as Ctrl+J or in kitty encoding.

   Gemini blocks every key, including Ctrl+C, for up to 30 s waiting for a paste to end, then replays the leftover bytes as keystrokes, where an Enter can submit. Needed instead: a short timeout, an always-live escape key, and leftover bytes treated as literal text.
2. **Side effects of keyboard enhancement:**
   - Capitals and Shift+non-ASCII lost.
   - Non-Latin layouts breaking Ctrl shortcuts.
   - Dead keys and IME broken (Codex, VS Code on WSL).
   - Key-release events causing duplicate input.
   - Windows AltGr arriving as Ctrl+Alt.
   - tmux's xterm key format losing Shift-Enter.
   - Gemini remapping Option characters (`µ ƒ ∂ Ω`) as shortcuts, so they cannot be typed.
3. **Timing heuristics that misfire.**
   - Enter within 30 ms of typing becomes a newline (Gemini), which breaks text expanders, dictation and scripted input.
   - Slow links merge separate keys into one read; tokenise by escape sequence, never by read boundary.
   - A burst detector that holds characters and never flushes them leaves stuck text or stalls drawing (Codex guard comments).
4. **Placeholder integrity.**
   - opencode expands with string `replace`, so `$&` in pasted text is mangled and a duplicate label expands the wrong paste.
   - Placeholder ids collide across recall, stash and restore.
   - Labels are reused after deletion.
   - Pastes are trimmed or truncated.
   - qwen's editor shows raw placeholders, so editing them silently drops the content.
   - Editing commands (kill-word, vim operators, wrapped cursor stops) must treat placeholders and grapheme clusters as atoms.
5. **Mixed offset spaces.** opencode mixes code units, graphemes and display columns (the `Bun.stringWidth`/`indexOf` cursor slips after the editor returns). Gemini does code-point cursor math with no graphemes. Use one canonical offset space and convert only at the edges.
6. **Single-character mode triggers.**
   - `!`, `?` or `/` switching mode from paste, yank, dot-repeat, recall or recovery.
   - A paste that reveals a leading `!` and would run as a shell command (Codex guard).
   - Bare words `exit`, `quit` and `:q` quitting opencode.
   - The mode must survive stash, queue and recall, and be captured when the message is enqueued (qwen #11626).
7. **Completion and overlays eating input.**
   - Popups swallowing newline keys.
   - Enter on a suggestion submitting text the user didn't type.
   - Esc on opencode's `/` popup deleting typed text such as `/usr/lib/...`.
   - Keys meant for a dialog falling through to submit.
   - Type-ahead swallowed while a permission prompt is up (opencode).
   - Whether a single Esc cancels depends on the order key handlers subscribed (qwen #8201).
8. **History browsing that destroys or forbids the draft.**
   - Codex and opencode refuse to browse while a draft exists.
   - Codex clears the composer when Down goes past the newest entry.
   - Text-only history loses attachments and pastes.
   - A global file with concurrent writers needs atomic appends, tolerance of malformed lines, no UTF-8 split at 4KB buffer boundaries (Claude Code), size trimming, and must never record masked input.
   - Gemini splits multi-line shell commands on recall.
9. **Inconsistent interrupt and quit.**
   - Esc cancel keeps the draft while Ctrl+C cancel wipes it (Gemini).
   - Ctrl+C or Ctrl+D on an empty prompt exits immediately even while work is queued (opencode).
   - `<leader>q` bound to two actions (opencode).
   - Codex disabled its double-press quit as "janky".
   - Recalling a queued message overwrites the current draft (opencode run mode).
   - Destructive single keys on an empty prompt need arm-then-fire.

   Design one explicit ladder: clear the draft into history, interrupt, then quit with a confirmation that names queued work.
10. **Overloaded keys.** Claude Code repeatedly paid for binding over readline basics (Ctrl+A/E/B/F/K/U/W/Y/L/Z). Gemini's Ctrl+Z undoes or suspends depending on state; its Ctrl+Y is YOLO; opencode's Ctrl+R renames the session.
11. **External editor pitfalls.**
    - opencode does nothing when neither `$VISUAL` nor `$EDITOR` is set.
    - opencode splits the editor command with `split(" ")`; Gemini classifies `$EDITOR` by substring, so any path containing "vi" counts as vi.
    - Keyboard modes, cursor, scrollback and repaint are not restored after the handoff.
    - Paste markers leak into the file.
    - It is undefined what an empty result means.
12. **No crash recovery for the live draft** in any of the four main references. Only qwen's stash and Codex's startup echo come close.
13. **Auto-converting a pasted path into an attachment** with no literal escape hatch, and doing synchronous disk I/O on the key path (Codex).

---

# Claude Code prompt composer (snapshot 2.1.286)

Sources and citation scheme:

- `bin@N`: byte offset N in `quarantine/claude-code-official-distribution/distributions/2.1.286/linux-x64/claude` (Bun-compiled bundle; minified JS readable as strings). Identifiers are minified, so I quote the literal code or strings.
- `CL:N`: line N of `quarantine/claude-code-official-public/CHANGELOG.md` (7973 lines; line 3 is 2.1.286, line ~7935 is 0.2.37, so a lower line number means a newer entry).
- Nothing was executed. I extracted strings with `strings(1)` and read them.

The composer is a React/Ink component built on three layers: an editor model class (`class sV{` bin@224954559, which holds `value, cursorOffset, mode, pastedContents, stashedPrompt, isInputOverlayActive, isVimEditing, inputOwnsEscape, interimRange, decorations`); a readline-style key handler (`function lvt({value:e,...` bin@214061940); and a paste/drop front end (`function rGe({onPaste:e` bin@214070837). Keys are resolved through a context-scoped binding table that users can rebind (`TP=[{context:"Global"` bin@202011109; `~/.claude/keybindings.json`, added at CL:6510).

## 1. Submit and newline

The default Chat context bindings (bin@202011109) include:
`enter:"chat:submit"`, `"ctrl+x enter":"chat:queueSubmit"`, `"ctrl+x ctrl+s":"chat:sendNow"`, `"ctrl+enter":"chat:sendNow"`, `"ctrl+j":"chat:newline"`, `"ctrl+_"/"ctrl+-"/"ctrl+shift+-"/"ctrl+shift+_":"chat:undo"`, `"ctrl+g"` and `"ctrl+x ctrl+e":"chat:externalEditor"`, `"ctrl+s":"chat:stash"`, `[Se]:"chat:imagePaste"` (where `Se = windows||wsl ? "alt+v" : "ctrl+v"`), and `space:"voice:pushToTalk"`. Mode cycling is `shift+tab`, falling back to `meta+m` on old Windows Node (`ie=_e?"shift+tab":"meta+m"`).

The Return handler in the text input (bin@214065163) is the core rule:

```js
function Yt({meta:u,shift:A}){
  if(j&&!te&&w.offset>0&&w.text[w.offset-1]==="\\")return Pno(Ze),w.backspace().insert(be(`\n`)); // backslash+Enter
  if(u||A)return w.insert(be(`\n`));        // Shift+Enter / Alt(Meta)+Enter
  if(Qir())return w.insert(be(`\n`));       // Apple_Terminal shift probe (stubbed: vt()=>false)
  if(n)n(w.text.slice(F.length)),Ve=!0;     // submit
  return w}
...
case"return":if(u.ctrl)return;return Yt(u);
case"enter":return w.insert(be(`\n`));      // keypad/"enter" key name inserts newline
if(j&&u.ctrl&&!u.shift&&!u.meta&&u.name==="j")return w.insert(be(`\n`));  // Ctrl+J
```

- A backslash before Enter becomes a newline, and the first use is recorded in config (`markBackslashReturnUsed` bin@92934412, `hasUsedBackslashReturn`) so the tips can stop nagging.
- Ctrl+Return is deliberately left unhandled in the editor so it can reach `chat:sendNow`.
- The help overlay shows "ctrl+j for newline" in the agents view. Tips include "Run /terminal-setup to enable Shift+Enter for new lines" (bin@94514864) and "Run /terminal-setup to enable Option+Enter for new lines".

**Terminal detection.** Detection is mostly env-driven, plus a capability reader with defaults:

- `xar=["iTerm.app","kitty","WezTerm","ghostty","tmux","windows-terminal","WarpTerminal"]` (bin@210027269) is an allowlist for "extended keys".
- Capabilities `{extendedKeys, synchronizedOutput, kittyKeyboard, kittyGraphics, mousePixels}` each start `pending` with a `likely` default and a `source` string. A `deadline()` settles any unanswered probe to its `likely` value (`${source}, no answer`). This is a tidy pattern for terminal queries that may never answer.
- Escape sequences: push kitty flags `CSI >5u` (or legacy `CSI >1u`) and xterm `modifyOtherKeys` level 2 `CSI >4;2m`; pop them with `CSI <u` and `CSI >4m` (bin@197386374). Kitty is not enabled under `TMUX`/`STY`.
- CL:1355 says "terminals that answer the kitty keyboard query (such as foot and Alacritty 0.16+) now get Shift+Enter".

**`/terminal-setup`.** It writes keybindings only for `Tt=["vscode","cursor","windsurf","alacritty","zed"]` (bin@214025624) plus Apple Terminal's Option-as-Meta. For natively CSI-u terminals it prints "Shift+Enter is natively supported in ${name}. No configuration needed." It also enables iTerm2 clipboard access with `defaults write com.googlecode.iterm2 AllowClipboardAccess -bool true` and prints the undo command. On macOS, when an Option key combo arrives as a plain character, a 5 s hint says "To enable ${key}, set Option as Meta in ${terminal} preferences (⌘,)" or "run /terminal-setup" (bin@225284338).

## 2. Paste

- **Bracketed paste** is required for correct multi-line paste. The handler converts CRLF and CR to LF and **tabs to 4 spaces** (`.replace(/\r\n|\r/g,"\n").replaceAll("\t","    ")`, bin@225286306 area).
- **Placeholder rule** (bin@225286306): `let DT=Math.max(0,Math.min(Ar-10,2)); if(Gm.length>y9||DP>DT){ ... Sp(wX(_v,DP)) }`, with `y9=800` (bin@209923335). A paste over 800 chars or with more than `min(rows-10, 2)` newlines becomes `[Pasted text #N +L lines]` (`wX` bin@203021495: `n===0 ? "[Pasted text #${e}]" : "[Pasted text #${e} +${n} lines]"`). Images become `[Image #N]`.
- **Paste again to expand.** Pasting the same content again expands the placeholder in place (`IT[Tv].content===Gm&&PT(Tv)`), as long as the content is at most `WLe=1e5` chars (bin@203025540). The hint "paste again to expand" (bin@93711340) shows for 8000 ms.
- **Placeholders are atomic tokens**: one Backspace at the placeholder end deletes the whole token (CL:6549). Contents live in `pastedContents` keyed by id and are **expanded on submit**. In history they are stored by content hash in a separate paste store (`registerPastedTextForRecall`, `resolvePastedTextByHash`), so recall can show "no longer available" (`"... is no longer available and was removed from the prompt"`).
- **Long typed input truncation**: `jHe=1e4, BM=1000` (bin@225259803). A prompt over 10,000 chars is collapsed to the first 500 and last 500 chars around `[...Truncated text #N +L lines...]`.
- **Short pastes** of 20 chars or more are still recorded as "inline" pasted content with negative ids (`LKe=20,PFn=1` bin@205516134), so the model can tell pasted text from typed text (cf. VSCode CL:795: "a paste over 800 characters or over 2 line breaks is now marked so Claude can tell it from what you typed").
- **Pasting `!command`** into an empty prompt enters shell mode (`if(MT.length===0){let ET=Cy(Gm);if(ET!=="prompt")Sn(ET),Gm=FC(Gm)}`; CL:5343).
- **Enter pressed during a paste is deferred**: `if(h.current&&E.key==="return"){E.preventDefault(),S.current=!0;return}` (bin@214072875). It is synthesised after the paste is processed, which avoids the "paste lost when typing immediately after pasting" race (CL:5648).
- **Paste-burst fallback without bracketed paste**: any single key event whose text is longer than 800 chars is treated as a paste (`E.key.length>y9&&!E.defaultPrevented` bin@214072975). There is no timing heuristic.
- **Image paste**: an *empty* bracketed paste on macOS or WSL means Cmd+V carried an image, so the clipboard is read (`if(Q&&(k||H)&&n){U();return}`). Ctrl+V or Alt+V reads the clipboard explicitly and shows "Pasting…" in the footer (CL:4462). With no image you get "No image found in clipboard. Use ${key} to paste images." or, over SSH, "...You're SSH'd; try scp?". Images over 2000px are downscaled (CL:4577).
- **File drops**: pasted text is split on spaces that precede `/` or a drive letter, and on newlines. Image paths are read as images and the remaining text is inserted. A macOS screenshot `TemporaryItems/...screencaptureui.../Screenshot` path falls back to a clipboard read (bin@95515900). Focus-report tails `[I`/`[O` are stripped from the paste text.

## 3. Editing

**Readline set** (bin@214063177 onwards):

- Ctrl: A/E go to logical-line start/end (CL:4850). B/F/H/D/K/N/P behave as in readline; Ctrl-D on empty is exit-arm. U is kill to line start, with the hint "Ctrl+Y to paste deleted text" when 3 or more chars are killed. W is delete-WORD-before (whitespace-delimited, Bash style; CL:1659 "`keybindingFlavor` no longer has any effect"). Y is yank.
- Meta: B/F word, D kill-word, Y yank-pop.
- Cmd+←/→ go to line start/end. Ctrl/Alt/Fn+←/→ move by word. Cmd+Backspace kills to line start. Alt/Ctrl+Backspace kills a word back. Alt/Cmd+Delete kills to line end.
- The kill ring accumulates consecutive kills in `append`/`prepend` direction, Emacs style, and any non-kill key dispatches `interrupt` to break accumulation. Masked inputs never feed the kill ring.

**Undo**: Ctrl+_ uses an undo buffer `{maxBufferSize:50, debounceMs:1000}` (bin@225277176). Redo: none found in the snapshot.

**Visual rows**: Up/Down move by wrapped visual row and only fall through to history at the first or last row: `if(x&&w.getPosition().line<=...)` (CL:3685: "they now move through each visual row first, and history recall enters at the near edge"). Wrap tiling is checked defensively, with the warning "Wrapped rows do not tile the text ... fell back to plain hard wrapping".

**Vim mode** (`/config` → Editor mode; `/vim` removed at CL:5250) is large: NORMAL/INSERT/VISUAL/VISUAL LINE indicators (`-- ${mode} --`), text objects, `.` repeat, `;`/`,`, `>>`/`<<`, `J`, `vimInsertModeRemaps` (for example `jj` to Esc, CL:3052), and NORMAL `/` opening reverse history search (CL:4006). In NORMAL mode, j/k at the input boundary navigate history (CL:5139).

**Grapheme and IME** fixes are frequent:

- the native cursor tracks the caret so IME composition renders inline (CL:5440, CL:6870)
- "cursor landing mid-grapheme on Ctrl+E/A/K/U/arrow keys when an Indic conjunct or ZWJ emoji wraps across lines" (CL:4468)
- "vim operators corrupting text containing decomposed (NFD) accented characters" (CL:4469)
- word motion for CJK and non-Latin scripts (CL:6873, CL:6886)
- "pasted Korean/CJK text turning into mojibake in terminals that deliver paste as per-byte extended-key events" (CL:3436)

**Extras**: an optional `spellcheck` setting via aspell/hunspell/ispell (CL:2368); emoji shortcode completion `:heart:` (CL:2800); invisible Unicode format and tag characters stripped on Enter, with the cleaned prompt shown for review (CL:883, CL:717 for the ZWNJ exception).

## 4. History

- **Storage**: one global `~/.claude/history.jsonl` (bin@203028333). Appends are locked, the file mode is 0600 (`append(s,"",384)`), and writes are retried up to 5 times at 500 ms intervals. Each entry carries `display, pastedContents, project, sessionId, timestamp`.
- **Up-arrow** reads the current project, with the current session's entries first (CL:5801).
- **Ctrl+R** opens a picker with title "Search prompts · <scope>" and placeholder "Filter history…" (bin@225074229). The scope cycles with Ctrl+S through `["session","project","everywhere"]` (bin@203027507) and defaults to everywhere (CL:4501). Matching is substring first, then subsequence fuzzy (`lxt` bin@225075377), and fuzzy only after the scan completes. A preview pane sits right at ≥100 cols or bottom otherwise. Huge history is capped and shown as "· newest N only".
- Picker keys (bin@202011109, HistorySearch context): Ctrl+R next, Esc or Tab accept into the composer, Enter execute, Ctrl+C cancel.
- **Dedup**: consecutive duplicates are skipped only when they share project and session and have no pastes (`YX` bin@203027550; CL:4073). The picker dedupes on display text plus paste hashes.
- **Draft preserved while browsing**: CL:6472 "Fixed draft prompt being lost when pressing UP arrow" and CL:4054 "editing a recalled prompt-history entry losing the edit when navigating further".
- **Double-Esc clear saves the cleared text into history** (`Dt` handler bin@214063177: `fhe(ee,Ze,{submitMode:A})` before `r("")`), so an accidental clear is recoverable with Up.

## 5. Mentions and completion

- **`@` files**: a native Rust fuzzy finder (CL:7076), pre-warmed index (CL:6180), custom `fileSuggestion` command (CL:6895), `respectGitignore`. Accepted file suggestions become removable attachments (CL:6566). It also covers `@server:` MCP resources, `@agent`, and `@session` names (CL:2468), with `#anchor` fragments supported (CL:6297).
- **Slash completion** works anywhere in the input (CL:6709). Fuzzy matching prefers exact and prefix (CL:6692). "Pressing Enter on a slash-command typo ... now reports it instead of running the closest fuzzy match" (CL:2359). Clicking a command fills it in rather than running it (CL:3779).
- **Argument hints** come from `argument-hint` frontmatter (CL:7544) and render as ghost text after the command.
- **Ghost text**: prompt suggestions appear as dim inline ghost text (`inlineGhostText` param in `lvt`). Tab accepts for editing and Enter accepts and submits (CL:6851). They back off after 20 unused in a row (CL:403).
- **Shell mode** has Tab completion from bash history (CL:6535) and live path autocomplete (CL:3387).
- Completion triggers after CJK sentence punctuation with no space needed (CL:5187).

## 6. Modes

- **`!` shell mode**: typing `!` at offset 0 switches mode (`function ett(t){return t==="!"}` bin@210425073). Help says "! for shell mode" (bin@93705476). Esc, Backspace or Ctrl+U on an empty shell prompt exits (CL:5910). Output now triggers a model response (`respondToBashCommands`, CL:3458).
- **`#` memory**: removed. CL:6860 says "Removed # shortcut for quick memory entry (tell Claude to edit your CLAUDE.md instead)".
- **Permission modes** cycle with Shift+Tab and display labels `default`, `accept edits on`, `plan mode on`, `auto mode on`, each with its own colour and symbol (bin@231252889). The prompt border colour changes per mode (`promptBorder`).
- **Other toggles**: Meta+P model, Meta+O fast, Meta+T thinking, Space push-to-talk voice.

## 7. While busy

- **Type-ahead is always allowed.** Enter while working queues the message (CL:7850), and messages typed mid-turn are delivered to the running turn as steering (CL:7796).
- Queued messages show above the spinner (CL:664) in gray until the model receives them (CL:921).
- Footer hints (bin@93838128): "Press up to edit queued messages", "Press up to select a queued message to edit, or Enter to send them now", "Press Enter to edit the selected message, or up again for an older one".
- **Send now** (Ctrl+Enter, or Ctrl+X Ctrl+S where Ctrl+Enter arrives as a newline) "interrupts the current turn and sends all queued messages at once" (CL:921). It was later changed to background running tools instead of cancelling (CL:657). Ctrl+X Enter is an explicit queue-submit.
- **Esc** interrupts. With queued prompts, Esc first moves them back into the input without cancelling (CL:6690). In vim INSERT, Esc no longer pulls the queue (CL:4703).
- "Interrupting a prompt before any response now automatically restores your input so you can edit and resubmit" (CL:5508). After an interrupt, Up "restores the interrupted prompt and rewinds the conversation in one step" (CL:5780).
- **Ctrl+C** clears non-empty input, then shows "Press Ctrl-C again to exit" (bin@94120640). Double Ctrl+C exits even with background agents (CL:747).
- **Esc-Esc**: on an idle empty prompt it opens rewind. With text, "Esc again to clear" appears for 1000 ms and the second press clears (and saves to history).

## 8. Drafts

- **Ctrl+S stash** is a single slot toggle (bin@225290328): `if(value.trim()===""&&stashedPrompt!==void 0) popStash(); else if(value.trim()!=="") stash()`. The footer shows "› stashed".
- **Discovery hint** (bin@225276838): if the input shrinks from 20 or more chars to 5 or fewer (the user is clearing a draft to ask something quick), it shows "Tip: ctrl+s stash" until the stash has been used.
- **Stash fixes**: losing pasted content (CL:6499), being cleared on mid-turn submit (CL:5937), restoring `!` shell mode (CL:749), and being lost in background sessions (CL:1826).
- **Crash and exit recovery** of an unsent main-prompt draft is **not found** in the snapshot. Rewind restores the draft from a message (`restoreDraftInto`). Background-agent replies that fail to send are saved and redelivered (CL:3057, CL:3794).
- **External editor** (Ctrl+G or Ctrl+X Ctrl+E):
  - Writes a temp file with pastes expanded inline, and on save re-collapses any text that still exactly matches a paste back into its placeholder.
  - Opens at the cursor's line (CL:50).
  - Optionally prepends "# ─── Claude's last response (for reference; removed on save) ───" (the last 50 lines) above "# ─── Write your reply below this line ───" (bin@214206866, bin@214207202).
  - Maps `code`→`code -w` and `subl`→`subl --wait` (bin@214205649).
  - Uses the alternate screen for terminal editors.
  - On failure it reports "Couldn't open ${editor} — ..." or "${editor} quit unexpectedly (exit code N)".

## 9. Traps and gotchas (quoted)

**Terminal keys and protocols**

- CL:461 "pasted multi-line text being submitted line by line after the terminal's bracketed paste mode was reset mid-session"
- CL:2765 "multi-line paste collapsing into one line with `j` in place of newlines in terminals that encode pasted newlines as Ctrl+J"
- CL:4711 "multi-line paste losing newlines in terminals using kitty keyboard protocol sequences inside bracketed paste"
- CL:3765 "keyboard input becoming permanently unresponsive after a paste operation whose end marker is dropped by the terminal"
- CL:4471 "pasting dumping stray escape sequences into the prompt when focus events or mouse-tracking reports interleave with the bracketed paste"
- CL:2057 "text like `<35;150;7M` being inserted into the prompt when a mouse report arrived split across reads right after the escape prefix"
- CL:5936 "Shift+Enter printing `[27;2;13~` instead of inserting a newline in Ghostty over SSH"
- CL:5325 "Shift+Enter submitting instead of inserting a newline on Windows Terminal Preview 1.25"
- CL:467 "send-now hint showing ctrl+enter on terminals that send it as a newline"
- CL:5881 "Enter inserting a newline instead of submitting when typing over a slow SSH connection"
- CL:4804 "rare spurious escape/return triggers when key names arrive as coalesced text over slow connections"
- CL:5019 "alt+enter not inserting a newline in terminals using ESC-prefix alt encoding, and Ctrl+J not inserting a newline (regression)"
- CL:5105 "capital letters being dropped to lowercase on xterm and VS Code integrated terminal when the kitty keyboard protocol is active"
- CL:3733 "Shift+non-ASCII characters (e.g. Shift+ä → Ä) being dropped" under kitty protocol
- CL:2056 "Ctrl keyboard shortcuts not firing under non-Latin (e.g. Cyrillic) keyboard layouts in kitty-protocol terminals"
- CL:2184 "Ctrl+[ not leaving vim INSERT mode in terminals that encode modified keys"
- CL:5406 / CL:6727 "terminal left in enhanced keyboard mode after exit ... Ctrl+C and Ctrl+D now work correctly after quitting"
- CL:2299 "killing a suspended (Ctrl+Z) session sometimes leaving the terminal in bracketed-paste mode"
- CL:4402 "Backspace and Ctrl+Backspace getting swapped after using Ctrl+G ... on terminals with persistent extended-key modes"

**Placeholder integrity**

- CL:2263 "Ctrl+W, Ctrl+U, Ctrl+K, Option+Backspace, Option+D and vim `df`/`dt` leaving a broken `[Pasted text #N]` placeholder when the cursor was inside it"
- CL:2621 "recalled or restored paste content occasionally attaching wrong data or silently losing text when the paste had aged out or placeholder numbers collided"
- CL:4424 "pasted text being silently dropped when a long prompt with a pasted-text placeholder was auto-truncated"
- CL:3016 "paste markers leaking into external editors ... stray È/É characters"
- CL:3943 "plan file names including `[Image #N]` / `[Pasted text #N]` placeholders"
- CL:4094 "pasted text being delivered to agents as an unreadable `[Pasted text #N]` placeholder"

**History integrity**

- CL:5306 "prompt history entries containing CJK or emoji being silently dropped when they fall on a 4KB boundary in `~/.claude/history.jsonl`"
- CL:2183 "Ctrl+R history search and up-arrow history breaking when `~/.claude/history.jsonl` contains a malformed entry"
- CL:2780 "history entries being dropped or duplicated when history writes raced"
- CL:4059 "a just-submitted prompt could appear twice"
- CL:2409 "queued messages reappearing in the prompt history while still queued"
- CL:2264 "masked (password-style) inputs ... saved to prompt history when cleared with double Esc"

**Queue and interrupt**

- CL:5960 "Escape not interrupting a running turn when the input box has draft text"
- CL:2233 "pressing Esc with a prompt queued could let the next turn finish early ... letting a later resubmit repeat actions"
- CL:2408 "a queued `!` shell command being sent to the model as plain text after pressing up-arrow to edit"
- CL:7062 "queued messages being incorrectly executed as bash commands"
- CL:5629 "queued prompts being concatenated without a newline separator"
- CL:7063 / CL:6855 "input being lost/cleared when typing while a queued message is processed"
- CL:5837 "images being lost when pressing ↑ to edit a queued message"
- CL:4263 "pressing Enter while a permission/dialog prompt is open also submitting text in the input box"
- CL:2053 "fast arrow-key + Enter sequences acting on the row above the one you navigated to"
- CL:1624 "typed or pasted characters occasionally landing out of order or being dropped during fast input or key repeat"

**Mode leaks**

- CL:604 / CL:605 "`o`/`p` on a `!`-prefixed line switching to shell mode", and "`.`, `p` or `P` that inserts text starting with `!` switching to shell mode"
- CL:1139 "dropping a `!` typed at the start while already in shell mode"
- CL:7883 "pasting could trigger memory or bash mode unexpectedly"

**Completion over newline**

- CL:5404 "shift+enter and meta+enter being intercepted by typeahead suggestions instead of inserting newlines"
- CL:4429 "prompt suggestions being auto-submitted by Enter on an empty input"
- CL:6856 "prompt suggestions replacing typed input when pressing Tab"

**Keymap collisions with readline**

- CL:4588 "`Ctrl+L` clearing the prompt input — it now only forces a screen redraw"
- CL:5512 Ctrl+F (kill agents) was moved because it was "shadowing readline forward-char"
- CL:5490 Ctrl+B background "interfering with readline backward-char"
- Ctrl+U semantics flipped three times (CL:4906, CL:4864, CL:5431)
- CL:7594 / CL:7585: Ctrl+Z was first undo, then suspend

**Destructive navigation**

- CL:2764 "the left arrow key discarding the conversation with no undo". The guard code is visible: `"Press ← again"` arm/fire state machine in `lvt`.

**Rendering**

- CL:845 crash when the prompt held "text containing terminal color codes ... recalled from history or text loaded from the external editor"
- CL:1349 "interface being drawn twice after returning from an external editor"
- CL:4510 "external-editor handoff (Ctrl+G) blanking the conversation history above the prompt"

## Ideas worth stealing

- One Return rule: a trailing `\` becomes a newline (remembered so tips stop), Shift or Alt+Return inserts a newline, Ctrl+J inserts a newline, plain Return submits, and Ctrl+Return is reserved for send-now. Fall back to Ctrl+X-prefixed chords (`ctrl+x ctrl+s`, `ctrl+x enter`) wherever modified Enter is unreliable, and change the displayed hint to match the terminal.
- A capability reader with `pending/likely/source` and a deadline, so terminal probes never block and the reason for every decision is recorded.
- Paste placeholders as atomic tokens with stable ids. Content goes to a side store referenced by hash in history. "Paste again to expand" is a zero-UI way to expand. Re-collapse after external-editor round trips by exact content match.
- Defer an Enter that arrives mid-paste until the paste is processed.
- Up/Down walk wrapped visual rows before touching history, entering history at the near edge.
- Double-Esc clear writes the cleared draft into history, so clearing is undoable via Up.
- A single-slot Ctrl+S stash toggle, plus a hint that fires when a long draft is cleared down to a few characters.
- Queued messages are visible, gray until consumed, and recallable with Up. Esc first returns the queue to the composer and only then interrupts. An interrupt before any output restores the prompt automatically.
- A history picker with session, project and everywhere scope cycling on one key, substring-then-fuzzy ranking, timestamps and a preview pane.
- An external-editor template that includes the last response as `#` comments removed on save, and opens at the caret line.
- Invisible Unicode format and tag characters stripped at submit, with the cleaned text shown for review, keeping the ZWNJ exception.

## Traps to design against

- Bracketed paste can be reset mid-session, lose its end marker, interleave with focus or mouse reports, or carry kitty-encoded or Ctrl+J-encoded newlines inside it. Need: a timeout or recovery for an unterminated paste, re-arming of paste mode, and a filter for focus and mouse reports inside paste.
- Kitty and modifyOtherKeys side effects: case and Shift+non-ASCII loss, non-Latin layouts breaking Ctrl shortcuts, Ctrl+[ no longer being Esc, and keypad keys. Always pop keyboard modes on exit, suspend, crash and editor handoff, and re-push afterwards.
- Slow links coalesce keys into text, which produces spurious Enter or Esc, or turns Enter into a newline. Tokenise by sequence, not by read boundary.
- Editing commands must treat placeholders and grapheme clusters as atoms, including kill-word, kill-line, vim operators and wrapped-line cursor stops.
- Placeholder id collisions across recall, stash and restore. Renumber on accept, and never let auto-truncation drop paste content.
- Mode prefixes (`!`) must not be triggered by paste, yank, dot-repeat or `o`/`p`, and the mode must survive stash, queue and recall.
- Completion popups must not swallow newline keys, and Enter on a suggestion must not auto-submit something the user did not type.
- Keys that also act on overlays or dialogs must not fall through to submit. Interrupt keys with a non-empty draft must still interrupt.
- History file robustness: tolerate malformed lines, avoid splitting UTF-8 at buffer boundaries, lock or serialise concurrent writers, and never record masked input or still-queued messages.
- Do not bind over readline basics (Ctrl+A/E/B/F/K/U/W/Y/L/Z). Claude Code paid for this repeatedly.
- Destructive single keys on an empty prompt (left arrow, Esc-Esc rewind) need an arm-then-fire confirmation.
- External editor handoff must restore keyboard modes, the cursor, scrollback and repaint, and must keep paste markers out of the file.

---

# Codex (codex-rs/tui) prompt composer

Snapshot: `quarantine/codex/codex-rs/tui/src/`. Paths below are relative to that directory unless they say otherwise. Line numbers come from this snapshot.

Architecture in one line: `ChatComposer` (bottom_pane/chat_composer.rs, 13,001 lines, plus 20 submodules under bottom_pane/chat_composer/) is a state machine wrapped around `TextArea` (bottom_pane/textarea.rs, 4,642 lines). `PasteBurst` (bottom_pane/paste_burst.rs) is a pure state machine. `ChatComposerHistory` (bottom_pane/chat_composer_history.rs) handles recall and search. `ChatWidget` owns queue, steer and interrupt policy (chatwidget/input_flow.rs, input_queue.rs, input_restore.rs). The long `//!` module docs at the top of chat_composer.rs (lines 1-269) work as a written spec, and they are the best single thing to read.

The snapshot has no real CHANGELOG. `codex/CHANGELOG.md` is 5 lines that point to GitHub releases. `codex/docs/` holds no composer or keybinding documentation. The trap evidence therefore comes from code comments and guard code, not from a changelog.

## 1. Submit and newline

- **Defaults** (keymap.rs:1677-1694). `composer.submit = [Enter]` and `composer.queue = [Tab]`. `editor.insert_newline = [Ctrl-J, Ctrl-M, Enter, Shift-Enter, Alt-Enter]`. Plain Enter appears in the newline list, but the composer checks `submit_keys` before the textarea sees the key (chat_composer.rs:3518-3526), so in practice plain Enter submits. Everything is remappable in `tui.keymap.*`. A conflict validator stops app-level bindings from silently shadowing composer bindings (keymap.rs:157-162). Reserved fixed keys are Ctrl-C, Ctrl-D, Ctrl-V, Ctrl-Alt-V, Shift-Tab, Esc, Alt-Left/Right, `/`, `!` and `@` (keymap.rs:2462-2480).
- **No backslash-Enter continuation.** I searched for it and found nothing.
- **Tab**: "`Tab` requests queuing while a task is running; if no task is running, `Tab` submits just like Enter so input is never dropped" (chat_composer.rs:121-122). "`Tab` does not submit when entering a `!` shell command" (chat_composer.rs:126). The guard is at :3518-3519.
- **Footer advertises a newline key that actually works.** `footer_insert_newline_key` (chat_composer.rs:4446-4462) shows Shift-Enter only if `enhanced_keys_supported` and Shift-Enter is bound. Otherwise it shows the first binding that is not plain Enter, which is Ctrl-J by default.
- **Capability detection.** On Unix a startup probe reads the kitty keyboard flags reply (terminal_probe.rs:491-521, tui.rs:526-528). Elsewhere it uses crossterm's `supports_keyboard_enhancement()` (tui.rs:572-575). The flags pushed are `DISAMBIGUATE_ESCAPE_CODES | REPORT_ALTERNATE_KEYS`, plus `REPORT_EVENT_TYPES` except on Ghostty, iTerm2, or tmux without a confirmed `csi-u` extended-keys format (tui/keyboard_modes.rs:235-252): "iTerm and Ghostty can leak shortcut release events that the terminal consumes. tmux's xterm key format also loses Shift-Enter when event types are reported." Under tmux it enables modifyOtherKeys mode 2 only when tmux reports `extended-keys-format csi-u` (:263-271). The escape hatch is the env var `CODEX_TUI_DISABLE_KEYBOARD_ENHANCEMENT` (:19). On exit it writes an extra `CSI < u` reset "so the parent shell does not inherit enhanced key reporting if a terminal misses the normal stack pop" (:1-5, :277-292).
- **No `/terminal-setup` equivalent.** Codex probes the terminal and falls back to Ctrl-J.
- **C0 compatibility.** `KeyBinding::is_press` matches `ctrl-j` against raw LF, and `shift-a` against plain `A` (key_hint.rs:43-47). Key releases are dropped before burst logic "to avoid ... appending the same character twice via paste-burst logic" (chat_composer.rs:3603-3607).

## 2. Paste

- **Bracketed paste** goes through `handle_paste` to `apply_paste` (bottom_pane/chat_composer/paste_input.rs:48-51, 117-180). `apply_paste` normalises `\r\n` and `\r` to `\n`, then runs `sanitize_user_text`.
- **Large-paste placeholder.** Above `LARGE_PASTE_CHAR_THRESHOLD = 1000` chars (chat_composer.rs:440), the paste becomes an *atomic text element* `[Pasted Content N chars]`, and the payload is stored in `pending_pastes`. A second paste of the same size gets ` #2`, ` #3`, and so on. Numbering is computed from the placeholders that still exist, so the base label is reused once all of them are deleted (chat_composer.rs:205-214, 1924-1946). Placeholders **expand on submit and queue** (chat_composer.rs:130-133, `expand_pending_pastes` :2548). They also expand into the Ctrl-G editor seed (`current_text_with_pending` :1439). They cannot be expanded in place for editing, except during reconnection: "Collapsed pastes expand into editable text so the full draft can be copied before quitting" (:162). Deleting the element drops its payload (`reconcile_deleted_elements`, :3777-3790). The hard cap on input is `MAX_USER_INPUT_TEXT_CHARS = 1 << 20` (protocol/src/user_input.rs:10), with the message "Message exceeds the maximum length of …" (chat_composer.rs:442-446).
- **Blockquote-aware paste.** A multiline paste on a line that begins with `> ` continues that prefix after every newline, then adds `\n\n` (paste_input.rs:1-5, 136-176). This is skipped in shell mode.
- **Image path paste.** Any paste of more than 1 char that `normalize_pasted_path` resolves (handling `file://` URLs, quotes, shlex escapes, Windows and UNC paths, and WSL conversion; clipboard_paste.rs:249-288), and for which `image::image_dimensions` succeeds, is attached as `[Image #N]` with a trailing space (chat_composer.rs:1216-1237).
- **Clipboard image.** The fixed keys are Ctrl-V and Ctrl-Alt-V (keymap.rs:2468-2469). They run `paste_image_to_temp_png`, guarded by "Clipboard busy; try image paste after it finishes" (app/clipboard.rs:21-30).
- **Image numbering.** Remote images appear as non-editable `[Image #1..M]` rows above the textarea, and local placeholders are offset after them. Deleting a remote row renumbers the local ones (chat_composer.rs:216-232).
- **Paste-burst detection without bracketed paste** (paste_burst.rs, documented at :1-153):
  - Constants: `PASTE_BURST_MIN_CHARS = 3`; `PASTE_BURST_CHAR_INTERVAL = 8ms`; `PASTE_BURST_ACTIVE_IDLE_TIMEOUT = 8ms`, or 60ms on Windows ("Slower paste bursts have been observed in Windows environments"); `PASTE_ENTER_SUPPRESS_WINDOW = 120ms` (:159-170).
  - The first fast ASCII char is *held and not rendered* (flicker suppression). A second char within 8ms starts buffering (`RetainFirstChar` then `BeginBufferFromPending`, :222-245).
  - Non-ASCII (IME) chars are never held, "since holding a non-ASCII character can feel like dropped input" (:110-114). Those paths use *retro-capture*, which pulls already-inserted chars back out of the textarea, but only if the grabbed prefix "looks pastey": it contains whitespace or is at least 16 chars (:393-411). The aim is that "short IME bursts don't get misclassified".
  - Enter inside the window becomes a newline, not a submit (`append_control_char_if_active`, chat_composer/reconnect.rs:17-52). This is disabled when the first line starts with `/`, so a slash command still dispatches.
  - Tab joins bursts too: `paste_burst_tab_does_not_accept_a_completion` checks that `/review this\t` stays literal (chat_composer/paste_tests.rs:85-105).
  - Ctrl, Alt, Super, Hyper and Meta keys flush the buffer first. Windows AltGr (Ctrl+Alt) still counts as text (chat_composer.rs:3639-3650, key_hint.rs:280-291).
  - Submit flushes expired bursts "independent of UI flush ticks" (:243-244; test `enter_expires_paste_state_without_a_ui_tick`, chat_composer_enter_tests.rs:17).
  - `tui.disable_paste_burst` turns detection off and flushes in-flight state on the transition (paste_input.rs:53-82).
  - `flush_if_due` uses `>` rather than `>=`, so tests must cross the threshold by at least 1ms (paste_burst.rs:66-67).
- **Right-click paste** (fullscreen) reads the clipboard asynchronously. "Intervening input or focus loss cancels the pending paste; a late clipboard result cannot overwrite newer input" (chat_composer.rs:8-11).

## 3. Editing

- **Emacs-ish defaults** (keymap.rs:1695-1746):
  - Movement: Ctrl-A/E or Home/End; Ctrl-B/F; Ctrl-P/N or Up/Down.
  - Word motion: Alt-B/F, Alt-Left/Right, Ctrl-Left/Right.
  - Delete back a word: Alt-Backspace, Ctrl-Backspace, Ctrl-Shift-Backspace, Ctrl-W, Ctrl-Alt-H.
  - Delete forward a word: Alt-Delete, Ctrl-Delete, Ctrl-Shift-Delete, Alt-D.
  - Kill and yank: Ctrl-U (to line start), Ctrl-K (to line end), Ctrl-Y (yank). `kill_whole_line` is unbound by default. Ctrl-D and Ctrl-H work as delete keys.
- **Kill buffer** holds a single entry: "This module does not implement an Emacs-style multi-entry kill ring" (textarea.rs:10-11). The good part is that the kill buffer *survives submit, slash dispatch, `/new` and thread switches*. You can Ctrl-K part of a draft, run a composer action, then Ctrl-Y it back (chat_composer.rs:135-139, textarea.rs:4-8, chatwidget/input_restore.rs:12-18).
- **Word boundaries** combine `unicode_segmentation::split_word_bound_indices` with a separator set, ``WORD_SEPARATORS = "`~!@#$%^&*()-=+[{]}\\|;:'\",.<>/?"`` (textarea.rs:72-102).
- **Undo/redo: Vim mode only.** Vim Normal `u` undoes and Ctrl-R redoes (keymap.rs:1806-1807). History snapshots the *whole composer draft*, including paste payloads, images and mention bindings, capped at `MAX_VIM_UNDO_STEPS = 64` and `MAX_VIM_UNDO_BYTES = 1 MiB` (chat_composer/vim_history.rs:1-18). There is **no undo in the default (non-Vim) editor.** I found no Ctrl-Z or Ctrl-_ binding.
- **Vim mode** is toggled by `/vim` (slash_command.rs:145), with no default key (keymap.rs:1652). It supports Normal, Insert and Replace, operators d/y/c, text objects, and `/`/`?` search inside the composer. "Fresh Vim drafts start in Insert" (chat_composer.rs:37-38). Insert-mode Ctrl-R still opens history search (:94-96). In Vim Normal mode on an empty composer, `!` enters shell mode and switches to Insert (:3489-3506).
- **Wrapped visual rows.** Up and Down move by *visual* row through a wrap cache, with a sticky `preferred_col`. Up on the first visual row jumps to position 0, and Down on the last jumps to the end. It falls back to logical lines if there is no cache yet (textarea.rs:1415-1483, 1487-1531). "A saved column can outlive a resize. Do not land in hanging whitespace…" (:1425-1426). Trailing spaces wrap rather than pushing the cursor outside the box (:13-15).
- **Graphemes and width.** Selection "keeps graphemes and elements atomic" (textarea.rs:18). Columns come from `display_width`. Tabs render as one column so byte ranges still line up (:104-114).
- **IME.** There is no preedit or composition support; crossterm does not expose it. The only IME accommodations are the no-hold path for non-ASCII and the WSL/VS Code guard, which disables keyboard enhancement because it "can break dead-key composition in VS Code on WSL" (tui/keyboard_modes.rs:49-56).

## 4. History

- **Scope.** There are two stores merged into one offset space (chat_composer_history.rs:1-16):
  - *Persistent, global* history in `~/.codex/history.jsonl`, with records `{"session_id","ts","text"}`. It is appended with a single `write(2)` under `O_APPEND` for multi-process safety, and trimmed to 0.8 × `max_bytes` (message-history/src/lib.rs:1-15, 52-57). Config is `history.persistence = save-all | none` (config/src/types.rs:202-219). It is text-only, so attachments are lost on recall.
  - *Local, in-session* history with the full draft: text elements, images, mention bindings and paste payloads (chat_composer_history.rs:38-52).
- **Up/Down gating.** Recall happens only when the composer is empty, or when the text equals the last recalled entry *and* the cursor is at 0 or at the end (chat_composer_history.rs:394-412). Anything else is ordinary cursor movement.
- **No prefix filtering.** Down past the newest entry *clears* the composer (:440-475). **The pre-browse draft is not saved.** That doesn't matter in practice, because a non-empty unrelated draft blocks history browsing in the first place.
- **Dedup.** Local history skips an entry identical to the previous one (:345-348). Ctrl-R deduplicates exact text within one search session only (:13-16).
- **Ctrl-R reverse search** (chat_composer/history_search.rs:1-23):
  - The prompt sits in the footer: `reverse-i-search: `, with status ` searching` or ` no match` in red (:399-413).
  - Matching is a case-insensitive substring (`to_lowercase().contains`, chat_composer_history.rs:785-789).
  - Opening it does not preview anything. Typing restarts the scan from the newest entry. Ctrl-R or Up goes older; Ctrl-S or Down goes newer (keymap.rs:1684-1685).
  - Enter accepts the preview as an editable draft. Esc or Ctrl-C restores the saved draft, *including background edits that arrived during the search* (history_search_draft.rs:1-4, 41-68).
  - Persistent entries are fetched asynchronously in batches, and stale log IDs are ignored.
  - "Treating `AtBoundary` like `NotFound` would produce the visible 'no match' flicker at the end of a one-result search" (history_search.rs:336-342).
- **Ctrl-C clears into history.** Ctrl-C on a non-empty draft clears it but records it in both local and persistent history, so Up gets it back (chat_composer.rs:1699-1717, bottom_pane/mod.rs:1131-1143).
- **Slash-command history** is two-phase: staged, then recorded after dispatch (chat_composer.rs:98-100).

## 5. Mentions and completion

- **`@`** opens a unified popup listing plugins, filesystem entries and skills. **`$`** lists skills and apps. The `mentions_v2` flag reverts to file-only `@` (chat_composer.rs:23-27, bottom_pane/mentions_v2/mod.rs:1-4).
- **File search** runs in the `codex-file-search` crate, which uses `ignore::WalkBuilder` (gitignore-aware) and the `nucleo` fuzzy matcher (codex-rs/file-search/src/lib.rs:7-15). It keeps one session per query and drops it when the query empties (file_search.rs:1-6). The popup keeps showing stale results until new ones arrive (`display_query` versus `pending_query`, file_search_popup.rs:21-30). Paths are centre-truncated, and "Narrow rows retain the filename" (:1-2).
- **Completion insertion rules** are careful (chat_composer.rs:46-63). They treat atomic elements as hard boundaries, insert a separator so a completion cannot merge with an adjacent element, and place exactly one separator after the completion. "It also inserts a space rather than crossing a line break."
- **Esc dismissal is remembered per token.** The popup stays hidden while the query, the token text and the token's ordinal among identical tokens are unchanged, "without suppressing a later identical token" (chat_composer.rs:56-63). Slash-popup dismissal is tracked separately by `/name` (:65-67).
- **Slash commands** use a fuzzy filter (`codex_utils_fuzzy_match`, slash_commands.rs:8, 158) and give prefix matches precedence (command_popup.rs:147-180). Aliases are hidden (`quit`, `btw`; :20-22) and so are `debug*` commands.
- A bare `/name` dispatches even after Tab completion has moved the cursor past the token and hidden the popup (chat_composer.rs:3210-3220). Commands that take inline arguments are listed in `supports_inline_args` (slash_command.rs:166-181).
- Argument hints exist only as prose inside descriptions, for example "list MCP tools; use /mcp verbose or /mcp login <name>" (slash_command.rs:150).
- **No ghost text or inline autosuggestion** is in the composer. I searched and found none.

## 6. Modes

- **`!` shell mode.** Typing `!` as the first character strips it and sets `is_bash_mode` (chat_composer.rs:3768-3775). The footer shows `Shell mode` in light red (:3582-3586). Backspace at cursor 0, or Esc on an empty composer, leaves the mode (:3473-3481, :3741-3751). An empty `!` prints "Prefix a command with ! to run it locally" / "Example: !ls" (chatwidget.rs:457-458).
- **Escaping a revealed `!`.** A leading `!` that only appears after paste expansion is sent as *literal model input*, not run as shell (chat_composer.rs:132, 3188-3195, 3243-3250). Recovered answers also escape `!` and `/` prefixes (:107-109).
- **Plan mode** is cycled with Shift-Tab (keymap.rs:2470-2473). The footer shows `Plan mode (⇧Tab to cycle)` in magenta (bottom_pane/footer.rs:150-170). Switching collaboration mode mid-turn is refused: "Cannot switch collaboration mode while a turn is running." (chatwidget/input_flow.rs).
- Vim mode is shown by a mode-indicator span in the same right-side indicator line (chat_composer.rs:1396-1410).
- **No `#` memory mode** exists.
- The placeholder text is `"Ask Codex to do anything"` (chatwidget.rs:2055).

## 7. While busy

- **Enter while a turn runs means steer.** The message is submitted into the running turn and delivered "after next tool call". The preview header reads `Messages to be submitted after next tool call (press Esc to interrupt and send immediately)` (bottom_pane/pending_input_preview.rs:13-21, 96-100).
- **Tab while a turn runs means queue a follow-up**, shown under `Queued follow-up inputs`.
- **Rejected steers** (the turn was not steerable) appear as `Messages to be submitted at end of turn` and are retried first (:125, chatwidget/input_queue.rs:37-46).
- **Editing a queued message.** `chat.edit_queued_message = [Shift-Left, Alt-Up]` pops the *most recent* queued message back into the composer (keymap.rs:1673; hint ` edit last queued message`, pending_input_preview.rs:172). It is LIFO, one at a time, and goes back through async questions first (keymap.rs:145-146).
- **Interrupt.** `chat.interrupt_turn = [Esc]` (keymap.rs:1662).
  - With pending steers, Esc interrupts and resubmits the steers as one fresh turn: "Model interrupted to submit steer instructions." (chatwidget/input_restore.rs:312-330).
  - Otherwise, queued messages and the current draft are *merged back into the composer*, separated by newlines, with image placeholders renumbered, instead of being auto-sent (:308-311, :397-404).
  - Notice: `■ Conversation interrupted - use /feedback if something went wrong`.
- **Esc priority** is: dismiss shortcut help, then Vim Insert to Normal, then interrupt or backtrack (chat_composer.rs:16, 1390-1394).
- **Esc while idle** on an empty composer primes backtrack. The footer then says `Esc again to edit previous message`, and a second Esc opens transcript prompt selection, where Enter reverts and reloads that prompt (app_backtrack.rs:10-14, footer.rs:939-950).
- **Ctrl-C.** It first dismisses a view, then cancels history or Vim search, then clears a non-empty draft (into history). Only an empty composer interrupts a running task, or quits when idle (bottom_pane/mod.rs:948-985, chatwidget/interaction.rs:548-559).
- **Ctrl-D** quits only on an empty composer (:580-590). The composer swallows Ctrl-D on empty input (chat_composer.rs:3528-3536).
- **Double-press quit is turned off**: `DOUBLE_PRESS_QUIT_SHORTCUT_ENABLED = false`, with the comment "requiring a double press to quit feels janky in practice (especially for users accustomed to shells and other TUIs)" (bottom_pane/mod.rs:228-233). The scaffolding (`QUIT_SHORTCUT_TIMEOUT = 1s`, ` again to quit` hint) is still there.
- **Shell-only work.** When only user shell commands are running, a non-`!` Enter is queued rather than submitted (chatwidget/input_flow.rs:66-71).

## 8. Drafts and external editor

- **No draft stash key** and **no on-disk draft persistence** across crash or exit. The only recovery copy is for *startup*: one draft kept in memory and printed to the terminal after a startup error. "Recovery belongs to the startup task, never a process-global file, log, or session" (startup_recovery.rs:1-5). Thread switches capture and restore the full composer state in memory (chatwidget/input_restore.rs:556-566).
- **Startup typing.** You can type before the session is ready. A provisional plain-text composer records one submit intent, and the draft is handed over with cursor, paste placeholders and local history (chat_composer.rs:111-119).
- **Ctrl-G external editor** (keymap.rs:1649; app/input.rs:234-287, external_editor.rs):
  - Editor resolution: `$VISUAL`, then `$EDITOR`, split with shlex. Missing editor gives "Cannot open external editor: set $VISUAL or $EDITOR before starting Codex."
  - The seed is the draft with paste placeholders *expanded*. It is written to a `.md` tempfile in a `<codex_home>/editor/` directory that **must not be writable by the sandbox policy** and must not be a symlink ("editor directory must not be writable", external_editor.rs:89-170).
  - While the editor runs, the footer shows "Save and close external editor to continue." (app.rs:294).
  - On return, trailing whitespace is trimmed, Vim undo history is reset, and `pending_pastes` is cleared. Image attachments are kept only while their `[Image #N]` labels still appear in the text, and are then renumbered (chat_composer.rs:1239-1270).
  - A non-zero editor exit leaves the draft unchanged and shows an error.

## 9. Traps and gotchas (evidence in code)

The snapshot has no changelog and almost no issue references in composer code. The only one is a TODO pointing at PR #2567 for typed-path (clipboard_paste.rs:270-271). The guard comments are the evidence:

- **Release events double-insert**: "Ignore key releases here to avoid treating them as additional input (e.g., appending the same character twice via paste-burst logic)" (chat_composer.rs:3603-3604).
- **Leaked release events and lost Shift-Enter**: "iTerm and Ghostty can leak shortcut release events … tmux's xterm key format also loses Shift-Enter when event types are reported" (tui/keyboard_modes.rs:238-241).
- **Dead keys under VS Code on WSL**: "VS Code running a WSL shell can hide TERM_PROGRAM … avoid enabling the keyboard mode that can break dead-key composition" (keyboard_modes.rs:49-51). Probing for it launches `cmd.exe` under a deadline.
- **Parent shell left in kitty mode** if the pop is missed, which is why exit sends an extra `CSI < u` reset (keyboard_modes.rs:1-5).
- **Stuck paste buffer**: clearing the classification window before flushing "can leave a non-empty buffer unable to flush later (because `flush_if_due()` relies on `last_plain_char_time`…)" (paste_input.rs:66-69, chat_composer.rs:3705-3714).
- **Hidden editor stalls the draw loop**: "a hidden editor's pending burst can otherwise keep the shared draw loop waiting indefinitely" (chat_composer.rs:241-242).
- **Raw C0 bytes**: "treating the raw control character as ordinary input would insert an invisible byte into the search query or composer draft" (history_search.rs:107-111).
- **AltGr is Ctrl+Alt on Windows**, so any Ctrl/Alt shortcut matcher eats AltGr characters unless guarded (textarea.rs:636-647).
- **`?` toggles help only on an empty composer with no burst in progress**, "so typing/pasting `?` still inserts text", and both `?` and `shift-?` must be bound (chat_composer.rs:3794-3799, keymap.rs:1680-1683).
- **Stale yank**: "if they incorrectly assume those methods fully reset editing state, a later yank will appear to restore stale text" (textarea.rs:134-138).
- **Paste-revealed `!`** would otherwise run as a shell command (chat_composer.rs:132).
- **Image-path auto-attach**: pasting the path of any readable image converts it into an attachment. The user cannot paste the literal path text, and the check does synchronous disk I/O (`image::image_dimensions`) on the input path (chat_composer.rs:1216-1237).
- **Up/Down history is gated on an exact text match** with the last recalled entry. After editing a recalled entry even slightly, Up becomes cursor movement (chat_composer_history.rs:403-411). That is surprising but deliberate.
- **Placeholder labels are content-addressed** (`[Pasted Content N chars]`), so external edits or history recall can collide. Restores call `remap_colliding_paste_placeholders` (chatwidget/input_restore.rs:8).
- **Double-press quit was shipped and then turned off** as janky (bottom_pane/mod.rs:228-233).
- **Clock injection**: paste timing uses Tokio's clock "so asynchronous flush deadlines and input classification share a clock, including in paused-time tests" (chat_composer.rs:5-6).

## Ideas worth stealing

- A footer newline hint that names the key that works on *this* terminal: Shift-Enter if enhanced keys are confirmed, otherwise Ctrl-J.
- Per-terminal enhancement flags: skip event-type reporting on Ghostty, iTerm2 and non-csi-u tmux. Add an env kill switch, and a hard `CSI < u` reset at exit.
- A pure, clock-injected paste-burst state machine:
  - Hold the first ASCII char about 8ms instead of rendering and then retracting it.
  - Never hold non-ASCII.
  - Retro-capture only "pastey" prefixes (whitespace or at least 16 chars).
  - Keep a 120ms window in which Enter becomes a newline.
  - Disable Enter-as-newline when the line starts with `/`.
  - Flush on submit without waiting for a UI tick.
- Large pastes as *atomic elements* backed by the original text, with content-derived labels and reusable numbering. Expand them on submit, in the external-editor seed, and when copy-before-quit is needed.
- A kill buffer that survives submit, dispatch and `/new`, so Ctrl-K, an action, then Ctrl-Y works.
- Ctrl-C clears the draft *into history*, so it can be recalled with Up.
- Undo snapshots that cover the whole draft (text, paste payloads, attachments, mention bindings) with a byte budget.
- Ctrl-R search in the footer that previews in the composer, restores the original draft (including background edits) on cancel, deduplicates per session, and avoids flicker at the boundary.
- Enter while busy steers at the next tool boundary, and Tab queues for the end of turn. Esc with pending steers means "interrupt and send now". Interrupt merges queued messages back into the draft instead of auto-sending them. Shift-Left or Alt-Up pops the last queued message back for editing.
- Esc-dismissal remembered per token occurrence, and completion that never merges with adjacent atomic elements.
- A leading `!` revealed by paste or recovery is treated as literal.
- The external editor's tempdir must be outside sandbox-writable roots. Attachments are reconciled by checking that their labels survive the edit.
- An in-memory startup draft that is printed after a fatal startup error, so typed text is never lost before the session is up.

## Traps to design against

- Key-release events and kitty `REPORT_EVENT_TYPES` on terminals and multiplexers that leak releases or drop Shift-Enter.
- Keyboard enhancement breaking dead keys and IME (VS Code on WSL).
- A heuristic paste detector that holds characters and forgets to flush them: when focus changes, when the editor is hidden, or when timing state is cleared. The result is stuck text or a stalled draw loop.
- Windows AltGr arriving as Ctrl+Alt and being swallowed by shortcuts. Raw C0 bytes inserted into the buffer.
- Single-character toggles (`?`, `!`, `/`) firing on pasted or typed text in a non-empty draft.
- A paste or expansion that reveals a mode prefix (`!`) and silently changes semantics.
- Auto-converting pasted paths into attachments with no way to keep the literal text, and doing sync I/O in the key path.
- Content-addressed placeholder labels colliding across restore, edit and recall.
- History recall that discards a draft. Codex sidesteps this only by refusing to browse with a non-empty draft. Down past newest clears the composer.
- Text-only persistent history losing attachments and paste payloads on recall.
- A global history file shared by concurrent processes: it needs atomic appends and size trimming.
- No undo outside Vim mode.
- No on-disk draft recovery after a crash.
- Double-press-to-quit judged janky and turned off. Design one clear interrupt and quit ladder instead.

---

# Gemini CLI composer (and where qwen-code differs)

Snapshot: `quarantine/gemini-cli` (~2026-09-30). Paths below are relative to
`quarantine/gemini-cli/packages/cli/src/ui/` (prefix **G/**) unless noted;
qwen-code paths are relative to `quarantine/qwen-code/packages/cli/src/ui/` (**Q/**).
This is source reading only; nothing was run.

Architecture in one paragraph: raw stdin goes to a generator-based key parser
(`G/contexts/KeypressContext.tsx`). It then passes through a chain of
"bufferers" (paste, backslash-Enter, fast-return) and is broadcast to
priority-ordered subscribers. `InputPrompt.tsx` (1933 lines) is a single large
`handleInput` key dispatcher sitting on top of `useTextBuffer`
(`components/shared/text-buffer.ts`, 4285 lines). The buffer is a reducer over
logical lines with a cached visual-wrap layout and "transformations"
(collapsed image paths and paste placeholders). Keys are data-driven commands
in `G/key/keyBindings.ts`, and the user can rebind them.

## 1. Submit and newline

- Default table, `G/key/keyBindings.ts:361-372`:
  - `SUBMIT` = `enter`.
  - `NEWLINE` = `ctrl+enter`, `cmd+enter`, `alt+enter`, `shift+enter`, `ctrl+j`.
  - Comment: "Must also exclude shift to allow shift+enter for newline". In
    practice the binding matcher is exact on all modifiers (`KeyBinding.matches`,
    `:226-234`).
- **Backslash-Enter**, two layers:
  - In the key stream, `bufferBackslashEnter` (`G/contexts/KeypressContext.tsx:257-298`)
    holds a `\` for `BACKSLASH_ENTER_TIMEOUT = 5` ms (`:27`). If Enter follows,
    it emits a shift+Enter instead.
  - In the composer, on Enter with a `\` before the cursor it does
    `buffer.backspace(); buffer.newline()` (`G/components/InputPrompt.tsx:1272-1280`).
  - Documented as "`\` (at end of a line) + `Enter`: Insert a newline"
    (`docs/reference/keyboard-shortcuts.md:236`).
- **Fast-return heuristic** (applies only when Kitty is not enabled,
  `KeypressContext.tsx:870-872`): `bufferFastReturn` (`:225-250`) turns any
  Enter arriving within `FAST_RETURN_TIMEOUT = 30` ms (`:30`) of an insertable
  key into shift+Enter. Comment: "This is to accommodate older terminals that
  paste text without bracketing."
- **Capability detection** (`G/utils/terminalCapabilityManager.ts`):
  - At startup it writes, in one burst, `ESC[?u` (Kitty flags), OSC 11
    (background colour), `ESC[>q` (XTVERSION name), `ESC[>4;?m`
    (modifyOtherKeys) and DA1 `ESC[c` as a sentinel (`:44-48`, `:249-256`).
  - The queries are wrapped in hidden mode `ESC[8m` "to prevent potential "m"
    character from being printed" (`:244-248`).
  - Timeout is 1000 ms: "all terminals should respond to the device attributes
    query used as a sentinel" (`:154-156`).
  - Enabling (`:265-280`): Kitty `ESC[>1u` if supported, else modifyOtherKeys
    `ESC[>4;2m`. Bracketed paste `ESC[?2004h` is always enabled ("it'll be
    ignored if unsupported").
  - Exit cleanup is a synchronous `fs.writeSync` of
    `'\x1b[<u\x1b[>4;0m\x1b[?2004l\x1b[?1000l...'` (`:22-39`).
  - tmux guard: "tmux 3.5+ may report #ffffff ... (common false positive over
    mosh)" (`:172-178`).
- **`/terminal-setup`** (`G/utils/terminalSetup.ts`):
  - Only for VS Code, Cursor, Windsurf and Antigravity (`:13-17`, `:62`).
  - Detection uses env vars (`CURSOR_TRACE_ID`, `VSCODE_GIT_ASKPASS_MAIN`,
    `TERM_PROGRAM`) and then the parent process name (`:114-170`).
  - It writes `workbench.action.terminal.sendSequence` with
    `VSCODE_SHIFT_ENTER_SEQUENCE = '\\\r\n'` (`:43`). That is backslash + CRLF,
    which the backslash-Enter bufferer then turns into a newline. Elegant reuse
    of one mechanism.
  - It backs up `keybindings.json` with a timestamp (`:180-190`) and refuses to
    touch existing shift/ctrl+enter bindings: "Existing keybindings detected.
    Will not modify to avoid conflicts." (`:338-340`).
  - It is skipped when Kitty is active: "Your terminal is already configured
    for an optimal experience..." (`:444-448`).
  - A one-time nudge is shown, persisted as `terminalSetupPromptShown`
    (`:496-502`).
- **macOS Option glyphs**:
  - `MAC_ALT_KEY_CHARACTER_MAP` (`KeypressContext.tsx:200-209`) rewrites
    `∫ ƒ µ Ω ¸ ∂` into alt+b/f/m/z/Z/d.
  - It does this "even if we are not on Mac, because mac users may remotely
    connect" (`:677-688`).
  - The only exception is Greek locales, for Ω (`:383-385`, `:680-682`).
- **qwen-code differs**:
  - SUBMIT requires `paste: false`, and NEWLINE includes
    `{ key: 'return', paste: true }`
    (`qwen-code/packages/cli/src/config/keyBindings.ts:210-230`). Paste is a
    key attribute rather than a separate event.
  - Its Option-glyph map is narrower: only `†`→t and `√`→v (`Q/contexts/KeypressContext.tsx:52-64`),
    with a much better comment on why.
  - It adds MinTTY's `\x1f` as Ctrl+Backspace (`keyBindings.ts:141-148`).

## 2. Paste

- **Bracketed paste**:
  - `bufferPaste` (`KeypressContext.tsx:300-352`) accumulates everything between
    `ESC[200~` and `ESC[201~` (`:45-46`) into one `{name:'paste'}` key.
  - Timeout: `PASTE_TIMEOUT = 30_000` ms (`:29`). On timeout it emits
    `AppEvent.PasteTimeout`, which shows "Paste Timed out. Possibly due to slow
    connection." (`G/AppContainer.tsx:1709-1714`).
- **Distrust of terminals**:
  - `isTerminalPasteTrusted()` returns true only when the Kitty protocol is on
    (`InputPrompt.tsx:99-111`).
  - Comment: "Cursor users on windows reported being impacted by this issue
    (https://github.com/google-gemini/gemini-cli/issues/3763)".
  - For an untrusted paste, Enter within 40 ms becomes a newline (`:821-843`,
    `:1258-1270`). Comment: "40ms is chosen arbitrarily as it is faster than a
    typical human could go from pressing paste to pressing enter."
  - There is no separate paste-burst detector beyond the 30 ms fast-return
    rule (section 1) and this 40 ms guard.
- **Large-paste placeholders**:
  - Thresholds: `LARGE_PASTE_LINE_THRESHOLD = 5`, `LARGE_PASTE_CHAR_THRESHOLD = 500`
    (`text-buffer.ts:37-38`).
  - The placeholder text is `[Pasted Text: N lines]` or `[Pasted Text: N chars]`,
    with ` #2`, ` #3` added for duplicates (`generatePastedTextId`, `:1595-1612`;
    regex `:40-42`). The placeholder string is itself the map key into
    `pastedContent`.
  - Inserted in the reducer `insert` case when `isPaste` (`:1846-1861`).
  - **Atomic**: backspace or delete removes the whole placeholder and its
    content (`findAtomicPlaceholderForBackspace`, `:1115-1160`, `:1942-1973`).
  - **Expand in place**: Ctrl+O (`EXPAND_PASTE`, `keyBindings.ts:397`) or a
    double-click in the alternate buffer toggles between collapsed and expanded
    (`tryTogglePasteExpansion`, `InputPrompt.tsx:173-205`; `:620-672`).
  - A hint is shown: "Press Ctrl+O to expand pasted text" (`:853-858`). If the
    cursor is elsewhere: "Move cursor within placeholder to expand" (`:199-203`).
  - **Expanded on submit** via `expandPastePlaceholders` (`InputPrompt.tsx:409-417`).
  - Orphaned content is pruned when a placeholder is killed
    (`pruneOrphanedPastedContent`, `text-buffer.ts:1629`).
  - Undo snapshots include `pastedContent` (`:778-784`, `:1578-1593`).
- **External editor + placeholders**:
  - The text is expanded before the editor opens.
  - Afterwards it tries to "re-collapse unchanged pasted content back into
    placeholders" by substring search and `newText.replace(content, id)`
    (`text-buffer.ts:3319-3345`). See traps.
- **Image paste**:
  - Ctrl+V / Cmd+V / Alt+V (`keyBindings.ts:378-385`) calls `handleClipboardPaste`
    (`InputPrompt.tsx:535-600`).
  - The image is detected with `osascript -e 'clipboard info'`, `wl-paste`,
    `xclip` or PowerShell (`G/utils/clipboardUtils.ts:129-200`).
  - It is saved to `<projectTempDir>/images/clipboard-<ts>.png` (`:262-283`) and
    inserted as `@relative/path` with smart spacing (`InputPrompt.tsx:548-570`).
    Files older than 1 hour are deleted (`clipboardUtils.ts:394-419`).
  - In the buffer, `@foo.png` is rendered collapsed as `[Image ...foo.png]`
    (`text-buffer.ts:902-934`).
  - Optional OSC 52 read path: writes `\x1b]52;c;?\x07` (`InputPrompt.tsx:574-575`);
    the response is parsed as a paste (`KeypressContext.tsx:438-457`).
  - A right-click release also pastes (`InputPrompt.tsx:674-683`).
- **File-path drops**:
  - If a pasted string of 3+ characters is entirely existing file paths, it
    becomes `@escaped/path ` refs (`text-buffer.ts:2947-2965`;
    `parsePastedPaths` / `splitDragAndDropPaths`, `clipboardUtils.ts:420-529`).
  - It handles bare, `\ `-escaped, single-quoted (`'\''`) and Windows
    double-quoted forms. It is all-or-nothing: "If any segment is invalid,
    return null".
  - Paste is accepted even when the prompt is unfocused, "to support drag and
    drop" (`InputPrompt.tsx:736-742`).
- **Sanitising**: inserts pass through `stripUnsafeCharacters`, which removes
  ANSI, C0/C1, BiDi controls, U+200B and U+FEFF (`G/utils/textUtils.ts:128-144`).
  CR and CRLF are normalised to LF (`text-buffer.ts:1848`, `:1874-1876`).
- **qwen-code differs**:
  - Placeholder is `[Pasted Content N chars] #k`. Thresholds are 1000 chars /
    10 lines. Freed IDs are reused (`Q/components/InputPrompt.tsx:231-233`,
    `:336-365`, `:597-615`).
  - Placeholders are held in `InputPrompt` state rather than the buffer, and
    are expanded only on submit (`:635-643`).
  - Paste markers are intercepted at the raw-byte level, with a **paste
    progress bar** in the footer (`Q/contexts/KeypressContext.tsx:1455-1475`;
    `Q/components/Footer.tsx:121-122`).
  - `PASTE_IDLE_TIMEOUT_MS = 1000`, with the rationale: "a lost paste-end marker
    leaves `isPaste = true` forever, every subsequent keystroke (including
    Ctrl+C) is silently buffered" (`:74-81`). Ctrl+C is "an always-available
    escape hatch" even mid-paste (`:861-875`).
  - Image-path pastes are promoted to attachment chips
    (`classifyPastedImagePaths`, `Q/components/InputPrompt.tsx:111-150`;
    comment: "Mirrors Claude Code's usePasteHandler").
  - The anti-autosubmit guard is 500 ms and applies only under `pasteWorkaround`
    (Windows or Node < 20) (`:1077-1081`, `:1839-1845`).

## 3. Editing

- **Emacs-ish bindings** (`keyBindings.ts:263-316`):
  - Ctrl+A/E home/end; Ctrl+F right.
  - Ctrl/Alt+Left/Right and Alt+B/F for word motion.
  - Ctrl+K/U kill to end/start; Ctrl+W, Alt/Ctrl+Backspace delete word back;
    Alt+D, Ctrl/Alt+Delete delete word forward.
  - Ctrl+H backspace; Ctrl+D delete-right.
  - There is no Ctrl+B left (`MOVE_LEFT` is only `left`, `:268`).
- **No kill ring.** Ctrl+K/U just delete (`text-buffer.ts:2392-2463`). Ctrl+Y is
  bound to `TOGGLE_YOLO` (`keyBindings.ts:394`), so there is no yank.
  `yankRegister` exists only for vim `y`/`p` (`text-buffer.ts:1573`).
- **Undo/redo**:
  - Snapshot stack, `historyLimit = 100` (`text-buffer.ts:1576-1593`).
  - Every reducer edit (including each typed character) pushes a full copy of
    the lines (`:1835-1836`), so undo granularity is per keystroke and only 100
    steps deep.
  - Platform-specific keys (`keyBindings.ts:779-807`):
    - Windows: Ctrl+Z / Alt+Z.
    - macOS: Cmd+Z / Alt+Z.
    - Linux: Alt+Z, Cmd+Z, Ctrl+Z, with the comment "keep Ctrl+Z for smart bubbling".
  - Redo is Ctrl+Shift+Z.
  - "Smart bubbling": `UNDO` returns false when the undo stack is empty
    (`text-buffer.ts:3465-3470`), so Ctrl+Z then falls through to
    `SUSPEND_APP` (`keyBindings.ts:402`, `AppContainer.tsx:1828-1829`).
- **Word motion**:
  - Hand-written word-class logic that handles combining marks
    (`isWordCharWithCombining`, `text-buffer.ts:66-266`).
  - Plus `Intl.Segmenter({granularity:'word'})` for some boundaries (`:369-400`).
- **Visual rows versus logical lines**:
  - Wrap layout keeps `logicalToVisualMap` and `visualToLogicalMap`
    (`text-buffer.ts:1246-1480`).
  - left/right/up/down/home/end operate on visual rows (`:2045-2110`), so
    Home/End and Ctrl+A/E go to the start/end of the wrapped row, not the
    logical line.
  - Up/Down use a `preferredCol` measured in code points within the visual row
    (`:2085-2105`), not display columns.
  - The prompt viewport is 10 rows (`AppContainer.tsx:618`) and auto-scrolls to
    the cursor (`InputPrompt.tsx:1697-1729`).
  - Click-to-position cursor via mouse (`:602-616`).
- **Graphemes and wide characters**:
  - All cursor and slice math is in code points (`cpLen`, `cpSlice`,
    `toCodePoints`, `G/utils/textUtils.ts:51-100`). Width comes from
    `string-width` per code point during wrapping (`text-buffer.ts:1360-1396`).
  - No grapheme-cluster segmentation for cursor movement or deletion, so ZWJ
    emoji and flags take several Left/Backspace presses. Inferred from code;
    no grapheme handling was found.
  - Crash guard: "Fallback for characters that cause string-width to crash
    (e.g. U+0602) See: https://github.com/google-gemini/gemini-cli/issues/16418"
    (`textUtils.ts:197-201`).
- **IME**:
  - No explicit IME handling or comments found.
  - The input uses a forked Ink (`"ink": "npm:@jrichman/ink@6.6.9"`) with
    `terminalCursorFocus` / `terminalCursorPosition` props on the cursor line
    (`InputPrompt.tsx:1662-1665`, `:1852-1853`). This places the real terminal
    cursor, which is what IME candidate windows need. It is not documented as
    being for IME.
  - qwen comments on IME timing: `KITTY_SEQUENCE_TIMEOUT_MS = 200` was "Based
    on empirical testing with IME input patterns in VS Code integrated
    terminal" (`Q/contexts/KeypressContext.tsx:68-72`).
- **Vim mode** (`G/hooks/vim.ts`, actions in `components/shared/vim-buffer-actions.ts`):
  - Supported: INSERT/NORMAL only (no visual mode); counts; h j k l w W b B e
    E 0 $ ^ gg G; x X ~ r f F t T ; ,; i a o O I A; d c y with motions; dd cc
    yy Y D C; p P; `u` undo; `.` repeat (`:172-186`, `:759-1410`).
  - Placeholder changes to "  Press 'Esc' for NORMAL mode." or
    "  Press 'i' for INSERT mode." (`G/components/Composer.tsx:162-166`).
  - In INSERT mode, Tab, Enter, Up/Down, Ctrl+R, Ctrl+U/K, Ctrl+V and a leading
    `!` are passed to InputPrompt (`vim.ts:483-510`).
  - Esc Esc in NORMAL clears the buffer (`:693-697`).
  - Unmapped printable keys in NORMAL are swallowed (`:1489-1498`).
- **qwen-code differs**:
  - Ctrl+Z is hard-wired to undo and Ctrl+Shift+Z to redo
    (`Q/components/shared/text-buffer.ts:2704-2705`), with no suspend bubbling.
  - Same 100-entry snapshot stack (`:1155`).

## 4. History

- **Prompt history**:
  - `useInputHistoryStore` (`G/hooks/useInputHistoryStore.ts`): "Completely
    separated from chat history and unaffected by /clear commands" (`:20-23`).
  - Loaded from `logger.getPreviousUserMessages()`, which reads `logs.json` in
    `storage.getProjectTempDir()`
    (`packages/core/src/core/logger.ts:15`, `:148`, `:243-253`). So it is
    **per project** across sessions.
  - Deduplication of **consecutive** identical entries only (`:41-50`).
- **Navigation** (`G/hooks/useInputHistory.ts`):
  - Up/Down when on the first or last visual row, or Ctrl+P/N
    (`InputPrompt.tsx:689-701`).
  - **Two-stage Up**: if the cursor is not at column 0 of the top row, the
    first Up goes `home` and the next one navigates (`:1198-1205`). Down mirrors
    this with `end` (`:1215-1223`).
  - **Draft and edit preservation**: `historyCacheRef` stores `{text, offset}`
    per history index, with -1 as the live draft (`useInputHistory.ts:39-43`,
    `:62-108`). Edits to recalled entries survive browsing, readline-style, and
    the cursor offset is restored when you return to the level you just left
    (`:78-91`).
  - The cache is reset on submit (`:45-60`).
  - No prefix filtering.
- **Ctrl+R reverse search** (`G/hooks/useReverseSearchCompletion.tsx`):
  - The composer buffer itself is the query, debounced 100 ms (`:51`).
  - Matching is a case-insensitive substring search (`:71-84`), with an
    incremental cache when the query extends the previous one (`:96-105`).
  - Results appear in a popup list, and the prompt glyph becomes `(r:) `
    (`InputPrompt.tsx:1829-1840`).
  - Keys: Up/Down navigate, Tab accepts into the buffer for editing, **Enter
    submits immediately**, Esc restores the pre-search text and cursor
    (`InputPrompt.tsx:923-953`, `:991-1057`; bindings `keyBindings.ts:335-337`).
  - Prompt search uses `userMessages`; shell-mode search uses shell history
    (`:984-989`, `:1190-1195`).
- **Shell history** (`G/hooks/useShellHistory.ts`):
  - Separate per-project file (`storage.getHistoryFilePath()`, `:22-29`).
    `MAX_HISTORY_LENGTH = 100` (`:12`).
  - **Global dedupe**: re-running moves the command to the front (`:97-99`).
  - Down past the newest entry returns `''` (`:124-126`), so a typed shell
    draft is lost when browsing.

## 5. Mentions and completion

- **Mode detection** (`G/hooks/useCommandCompletion.tsx:121-210`):
  - It scans back from the cursor for `@` first, honouring `\ `-escaped spaces
    (`:137-175`), so "`/cmd @file` triggers file completion".
  - Then it checks for slash completion, only on row 0 (`:177-185`).
- **@ completion** (`G/hooks/useAtCompletion.ts`):
  - Covers files across all workspace dirs, with fuzzy matching through
    `AsyncFzf` and gitignore/geminiignore filtering (`:303-334`). Results are
    cached (`cacheTtl: 30`).
  - Also offers **MCP resources** as `serverName:uri` (`:113-139`) and
    **agents** (`:142-150`).
  - A loading indicator appears only after 200 ms ("slowSearchTimer", `:371-373`).
    Search timeout is 5 s (`:24`).
- **Slash completion** (`G/hooks/useSlashCompletion.ts`):
  - fzf fuzzy matching on name and `altNames`; hidden commands are excluded
    (`:229-241`).
  - Per-command argument `completion` functions (`:172-216`).
- **Enter on a suggestion** (`InputPrompt.tsx:1095-1176`):
  - It executes immediately if the command is `autoExecute` and has no
    argument completer, or if the suggestion has `submitValue`.
  - Otherwise it fills the buffer.
  - On a perfect match, Enter executes unless the user moved the selection
    (`:1059-1071`).
  - Right/Left expand or collapse long suggestion text (`:1019-1030`).
- **Shell-mode completion** (`useShellCompletion`) is off until Tab is pressed
  (`forceShowShellSuggestions`, `InputPrompt.tsx:788-802`).
- **Ghost text**:
  - LLM "prompt completion" (`G/hooks/usePromptCompletion.ts`,
    `model: 'prompt-completion'`, 250 ms debounce, minimum 5 characters).
  - It is **hard-disabled**: `const isPromptCompletionEnabled = false;` (`:47`,
    and again in `useCommandCompletion.tsx:189`). The changelog still
    advertises `"general": { "enablePromptCompletion": true }`
    (`docs/changelogs/index.md:1178-1180`).
  - The rendering path remains: inline plus overflow ghost lines in
    `theme.text.secondary`, accepted with Tab (`InputPrompt.tsx:1179-1188`,
    `:1430-1470`, `:1565-1575`).
- **Argument hints**: none found as inline hint text.
- **qwen-code differs**:
  - It has working "followup suggestions": a model-suggested next prompt shown
    on an empty prompt.
  - With an empty buffer, **Enter fills rather than submits**. Comment: "This
    prevents accidental execution of destructive slash commands (/clear, /quit)
    and aligns with Claude Code's design" (`Q/components/InputPrompt.tsx:1825-1837`).
  - It also has mid-input ghost text for slash commands
    (`Q/hooks/useCommandCompletion.tsx:372-376`).

## 6. Modes

- **`!` shell mode**:
  - Typing `!` into an empty buffer toggles it and swallows the `!`
    (`InputPrompt.tsx:913-922`). Esc exits shell mode (`:962-966`).
  - Indicators:
    - Glyph `!` instead of `>` (`:1828-1845`).
    - `statusText = 'Shell mode'`, coloured `theme.ui.symbol` (`:1752-1754`).
    - Placeholder "  Type your shell command" (`Composer.tsx:167-168`).
- **Approval modes**:
  - Shift+Tab cycles default → auto_edit → plan; "Plan mode is skipped when the
    agent is busy" (`keyBindings.ts:395`, `:645-646`). Ctrl+Y toggles YOLO
    (`:394`).
  - The composer colour reflects the mode: "YOLO mode" in red with prompt glyph
    `*`, "Plan mode" in green, "Accepting edits" in yellow
    (`InputPrompt.tsx:1743-1764`, `:1841-1842`).
- **`#` memory**: no `#` shortcut in the composer (none found in
  `InputPrompt.tsx`). Memory goes through slash commands.
- **Other composer-level toggles**:
  - `?` on an empty prompt opens the shortcuts panel (`:887-896`).
  - Tab Tab within 350 ms toggles a "clean UI" (`:168`, `:803-809`).
  - Voice push-to-talk on space when voice mode is on (`keyBindings.ts:404`,
    `InputPrompt.tsx:371-383`).
- **qwen-code**: same `!` trigger (`Q/components/InputPrompt.tsx:1198`).

## 7. While busy

- **Typing ahead** is always allowed. Composer keys stay live during streaming.
- **Enter while busy** (`G/AppContainer.tsx:1375-1453`):
  - If `experimental.modelSteering` is on and the agent is running, the text
    becomes a **steering hint**: `injectionService.addInjection(trimmed,
    'user_steering')`, rendered as a `hint` item (`:1359-1373`, `:1405-1409`).
    It is off by default (`docs/cli/model-steering.md`).
  - Otherwise it is **queued** via `addMessage` (`:1436-1452`).
  - Slash commands run concurrently only if `isSafeConcurrent` (`:1393-1403`).
  - Slash and shell submissions while Responding are refused with
    "Shell commands cannot be queued" / "Slash commands cannot be queued"
    (`InputPrompt.tsx:466-485`).
- **Tab while busy**: explicit queue (`QUEUE_MESSAGE` = `tab`,
  `keyBindings.ts:362`; `InputPrompt.tsx:765-786`). Tab is overloaded with
  completion, ghost acceptance, shell focus and double-Tab.
- **Queue behaviour** (`G/hooks/useMessageQueue.ts`):
  - All queued messages are **joined with `\n\n` into one prompt** when the
    stream goes Idle (`:69-91`).
  - Displayed as "Queued (press ↑ to edit):" (`G/components/QueuedMessageDisplay.tsx:25`).
  - **Recall**: Up on an empty buffer pops *all* queued messages into the
    buffer as one text (`tryLoadQueuedMessages`, `InputPrompt.tsx:519-532`).
    There is no per-item editing.
- **Interrupt**:
  - Esc during generation propagates to global cancel if no overlay consumed
    it (`InputPrompt.tsx:968-972`).
  - Ctrl+C always calls `cancelOngoingRequest` and counts toward
    "Press Ctrl+C again to exit." (3 s window, `AppContainer.tsx:1814-1820`;
    `constants.ts:32`).
  - Ctrl+D exits only on an empty buffer (`:1821-1827`).
  - Esc on an idle prompt: one press shows "Press Esc again to clear prompt."
    or "...rewind." The second press within 500 ms clears the text, or submits
    `/rewind` if the buffer is empty (`InputPrompt.tsx:266-286`;
    `components/ToastDisplay.tsx:64`).
- **Draft on interrupt** (`AppContainer.tsx:1320-1357`):
  - Esc-cancel: "user is in control - preserve whatever text they typed".
  - Ctrl+C cancel: `clearBuffer` is true, so **the draft is wiped**
    (`useGeminiStream.ts:830-857`).
  - On a context-overflow cancel, the last prompt plus queued text is restored
    into the composer (`useGeminiStream.ts:1415`; `AppContainer.tsx:1336-1348`).
  - In the composer itself, `CLEAR_INPUT` = Ctrl+C clears a non-empty buffer
    (`keyBindings.ts:290`, `text-buffer.ts:3435-3441`).
- **qwen-code differs**:
  - Enter while busy enqueues. Non-deferred entries are **drained into the
    running turn at tool boundaries as steer input** (`drainSteerAtBoundary`,
    `Q/hooks/use-llm-stream.ts:3542-3553`, `:4065-4069`).
  - **Ctrl+Q** queues with `deferUntilIdle=true` (`Q/components/InputPrompt.tsx:1818-1823`;
    `config/keyBindings.ts:219-221`).
  - Queue entries record shell intent at submit time "instead of the live
    shell-mode flag, which may have flipped while the entry waited (#11626)"
    (`Q/hooks/useMessageQueue.ts:24-31`).
  - Esc pops the queue into the input when not Responding
    (`Q/components/InputPrompt.tsx:1273-1305`).

## 8. Drafts

- **Gemini has no stash and no crash or exit recovery** of a draft: the buffer
  starts with `initialText: ''` (`AppContainer.tsx:616-624`). No "draft" or
  "stash" code was found in `G/`.
- **External editor**:
  - Ctrl+G, or Ctrl+Shift+G (`keyBindings.ts:374-376`). The old Ctrl+X now only
    shows "Use Ctrl+G to open the external editor." (`InputPrompt.tsx:1317-1324`).
  - Writes the expanded text to `os.tmpdir()/gemini-edit-*/buffer.txt`, takes
    an undo snapshot, and drops raw mode (`text-buffer.ts:3316-3361`;
    `G/utils/editorUtils.ts:141-142`).
  - Editor resolution: `preferredEditor` setting, then `$VISUAL`, then
    `$EDITOR`, then `vi` or `notepad` (`editorUtils.ts:59-110`).
  - GUI editors get `--wait` / `-w` added (`:87-103`). vi/vim/nvim get
    `-i NONE` "to prevent E138 'Can't write viminfo file'" (`:131-139`).
  - Terminal editors run via `spawnSync` with `stdio: 'inherit'` (`:145-153`).
  - The result replaces the buffer without an extra undo push, so one undo
    reverts the whole edit (`text-buffer.ts:3347`).
- **qwen-code differs, with a real stash**:
  - When the buffer is non-empty, **Ctrl+S** (overloaded on `SHOW_MORE_LINES`)
    saves the expanded draft to the per-project `prompt-stash.json`. The write
    is atomic, with mode 0600 and noFollow (`Q/components/InputPrompt.tsx:1121-1137`;
    `qwen-code/packages/cli/src/services/prompt-stash.ts:12-37`).
  - Message: "Prompt stashed. It will be restored next time."
  - Restored into an empty buffer once per target dir at the next startup
    (`Q/AppContainer.tsx:1564-1576`). Cleared on every submit
    (`Q/components/InputPrompt.tsx:659-660`).
  - Stashing does not clear the buffer, and there is one slot only.
  - External editor is Ctrl+X (`keyBindings.ts:234-237`).

## 9. Traps and gotchas (quoted)

Gemini:

1. **Untrusted bracketed paste.** Issue #3763 (`InputPrompt.tsx:107-110`): even
   VS Code-family terminals' bracketed paste is not trusted, so outside Kitty
   every paste arms a 40 ms "Enter becomes newline" window. Paste is
   effectively treated as unreliable everywhere except Kitty.
2. **30 s paste timeout swallows keys.** `PASTE_TIMEOUT = 30_000`
   (`KeypressContext.tsx:29`). While in paste mode, every key is appended,
   including Ctrl+C (`:318-332`). If `ESC[201~` is lost, the UI appears dead
   for 30 s, and then the partial buffer is emitted as a paste while the
   remaining bytes arrive as keystrokes, where Enter can submit. qwen fixed
   exactly this (1 s idle and a Ctrl+C escape hatch, quoted in section 2).
3. **Fast-return eats Enter.** Fast-return (`KeypressContext.tsx:230-249`) turns
   Enter within 30 ms of any printable key into a newline. Text expanders,
   dictation and scripted input cannot submit in non-Kitty terminals.
4. **Option-glyph remap blocks real characters.** `µ ∫ ƒ ∂ ¸` and `Ω`
   (except under a Greek locale) can never be typed literally, on any platform
   (`:200-209`, `:677-688`).
5. **Ctrl+Z is state-dependent.** It is undo when there is undo history and
   suspend when there is not (`keyBindings.ts:788-794`, `text-buffer.ts:3465-3468`).
   The same key does two very different things depending on hidden state.
6. **Per-keystroke undo, capped at 100.** Every insert snapshots all lines
   (`text-buffer.ts:1576-1593`, `:1835-1836`). Long drafts lose undo depth fast
   and pay O(lines) per key.
7. **Re-collapse is fragile.** After the external editor,
   `newText.replace(content, id)` (`text-buffer.ts:3341-3344`) replaces only the
   first occurrence of the pasted text. Content the user typed that equals a
   paste could be collapsed instead. A lightly edited paste stays expanded
   forever, while its stale `pastedContent` entry remains.
8. **Placeholder is plain text.** Its string is the key (`:51`, `:1595-1612`).
   Typing `[Pasted Text: 6 lines]` by hand while that id exists expands to the
   hidden content on submit.
9. **Editor detection by substring.** `terminalEditors.some(te =>
   executable.toLowerCase().includes(te))` and `executable.includes('vi')`
   (`editorUtils.ts:117-139`), plus `command.split(' ')` (`:112`). An `$EDITOR`
   path containing "vi" (for example `/home/david/bin/zed`) would be
   classified as a terminal editor and given `-i NONE`. A path containing
   spaces breaks.
10. **`!` grabs a leading exclamation.** A message beginning with "!" can't be
    typed directly in the normal prompt (`InputPrompt.tsx:913-922`). Similarly,
    `?` on an empty prompt opens help (`:887-896`).
11. **Enter in reverse search submits immediately.** There is no
    review-then-submit step unless you press Tab (`:1039-1048`).
12. **Ctrl+C during a turn erases the draft**, while Esc preserves it
    (`AppContainer.tsx:1326-1334`). In the composer, Ctrl+C on non-empty text
    is consumed as "clear input" (`text-buffer.ts:3435-3441`) before the global
    quit handler sees it.
13. **Vim INSERT mode delays cancel.** Esc is consumed to enter NORMAL
    (`vim.ts:474-480`), so cancelling generation takes a second Esc. vim's own
    Enter-submit branch (`:512-527`) is unreachable, because Enter already
    returned false at `:486`. Dead code that would also bypass placeholder
    expansion if it were ever reached (`useVim(buffer, handleFinalSubmit)`,
    `AppContainer.tsx:1493`).
14. **Shell history mangles multi-line commands.** It is written with `\n`
    joins (`useShellHistory.ts:61-68`) and re-read as lines, joined only on an
    odd trailing backslash (`:38-50`). Multi-line shell commands come back
    split. Down past the newest entry also loses the draft (`:124-126`).
15. **Ghost text dead but advertised.** The feature is hard-disabled while
    still advertised in the changelog (`usePromptCompletion.ts:47`).
16. **Stale doc comments.** `bufferBackslashEnter` says it "Buffers "/" keys"
    and refers to a nonexistent `DRAG_COMPLETION_TIMEOUT_MS`
    (`KeypressContext.tsx:252-256`).

qwen-code:

17. **Handler-order dependence, #8201.** Comment at `Q/components/InputPrompt.tsx:1273-1313`:
    "child useEffects subscribe to KeypressContext first ... Break the buffer
    invariant and the single-ESC cancel regresses with a fully green suite (no
    integration test covers this hop yet)". Also: "after any remount of
    InputPrompt (e.g. a tool-confirmation round trip) AppContainer runs first,
    so the cancel lands on the next press instead." Esc semantics depend on
    subscription order.
18. **Queue intent captured late, #11626.** The live shell-mode flag flipped
    while a message was queued and misrouted it (`Q/hooks/useMessageQueue.ts:24-31`).
19. **Placeholders leak into the editor.** qwen's external editor writes
    `stateRef.current.lines.join('\n')` (`Q/components/shared/text-buffer.ts:2594-2595`).
    Because placeholders live in InputPrompt, the editor shows the raw
    `[Pasted Content N chars]`, and editing that text silently orphans the paste.
20. **Unfocusable footer, #4171/#4308.** "Must stay narrow — followup
    suggestions and mid-input ghost text don't take Footer's space and
    shouldn't hide it." (`Q/components/InputPrompt.tsx:204-214`).

## Candidate ideas worth stealing

- **Keys as a command table.** Rebindable, documented and auto-generated into
  docs (`keyBindings.ts` plus `KEYBINDINGS-AUTOGEN`). Keybinding parse errors
  name the valid key set.
- **One mechanism for multi-line input in VS Code.** `/terminal-setup` makes
  the terminal send `\` + CRLF, and the same backslash-Enter path
  that power users type serves it too. It backs up config, refuses to clobber,
  and nudges once.
- **Batched startup probe.** Kitty, then modifyOtherKeys, then DA1 sentinel in
  one hidden-mode write with a single timeout. Synchronous terminal-mode
  cleanup on exit.
- **Atomic, expandable placeholders.** Paste placeholders as transformations:
  deleted as a unit, expandable in place (Ctrl+O or double-click), included in
  undo snapshots, expanded on submit. Avoid gemini's text-as-key and
  re-collapse heuristics: use an id-bearing span instead.
- **Drag-and-drop paths become references.** All-or-nothing parsing of quoted
  and escaped dropped paths into `@path` refs.
- **Collapsed image mentions.** `@path.png` rendered as `[Image …name.png]`
  while the logical text stays a plain path.
- **Per-level history edit cache with cursor restore.** Edits to recalled
  entries survive navigation, and the live draft is level -1.
- **Two-stage Up/Down.** Go to home/end first, then history.
- **Reverse search that restores on cancel.** Esc restores pre-search text and
  cursor; Tab means "accept to edit".
- **Mode shown in the prompt glyph and colour.** `!`, `*`, `(r:)`, plus a
  coloured frame and screen-reader `aria-label`. Placeholder text explains the
  current vim mode.
- **Deferred loading indicator.** Show "loading" for @-search only after 200 ms.
- **Queue recall.** "Queued (press ↑ to edit)" plus Up-on-empty to recall
  queued text.
- From qwen:
  - **Ctrl+C escapes a stuck paste.** It is honoured even mid-paste, with a
    short 1 s paste idle timeout.
  - **Paste progress indicator.**
  - **Queue vs steer.** Choose between steer at the next tool boundary (Enter)
    and defer until idle (Ctrl+Q).
  - **Record intent at enqueue time.**
  - **Durable stash.** Single-slot per-project file, atomic 0600, restored on
    the next start, cleared on submit.
  - **Enter on an empty prompt fills a suggestion rather than running it.**

## Candidate traps to design against

- **Bracketed-paste recovery.** Give it a short idle bound, keep Ctrl+C
  available inside paste mode, and never emit leftover bytes as live keys
  after a timeout.
- **Timing heuristics.** 30/40/5 ms rules that reinterpret Enter quietly break
  fast or scripted input. Prefer protocol signals (Kitty, bracketed paste) and
  make any heuristic visible and optional.
- **Global character rewrites** (Option glyphs) that make real characters
  untypeable.
- **One key, two meanings by hidden state** (Ctrl+Z undo/suspend, Ctrl+C
  clear/cancel/quit, Tab with five meanings). If you overload a key, show the
  current meaning.
- **Per-keystroke full-buffer undo snapshots with a small cap.** Coalesce edit
  runs.
- **Code-point editing.** Code-point cursor math plus per-code-point width:
  use grapheme clusters for movement and deletion and display columns for
  preferred column.
- **Placeholders as plain text.** Placeholder identity carried in literal text,
  or placeholders that are not round-tripped through the external editor.
- **Inconsistent draft handling.** Draft lost on Ctrl+C cancel, on shell-history
  Down, and on exit (gemini). Every path should preserve or recoverably stash
  it.
- **Esc semantics that depend on handler subscription order** (qwen #8201).
  Route Esc through one explicit priority policy.
- **Queued state that re-reads live mode** rather than the submit-time intent
  (qwen #11626).
- **Leading-character mode triggers** (`!`, `?`) that steal the first
  character of normal prose.
- **Substring-based editor detection and space-splitting** of
  `$EDITOR`/`$VISUAL`.
- **Shell history files that cannot represent multi-line entries.**

---

# opencode prompt composer

Snapshot: `quarantine/opencode/` (~2026-09-30). Paths below are relative to
`quarantine/opencode/packages/`. The main TUI composer is
`tui/src/component/prompt/index.tsx` (1686 lines), built on OpenTUI's
`TextareaRenderable` with "extmarks" (virtual, styled ranges) for placeholders.
A second composer exists for `opencode run` direct mode
(`opencode/src/cli/cmd/run/footer.prompt.tsx`, `prompt.shared.ts`), and it
behaves differently in places (history, queued prompts). The OpenTUI library
itself (`@opentui/core`, `@opentui/keymap`) is **not in the snapshot**: no
`node_modules`. So textarea internals (undo granularity, how extmarks delete,
how wrapping and IME composition work) can only be inferred from the call sites.

## 1. Submit and newline

- The defaults live in `tui/src/config/keybind.ts`:
  - `input_submit: "return"` (:160).
  - `input_newline: "shift+return,ctrl+return,alt+return,ctrl+j"` (:161).
  - There is no backslash-Enter continuation; nothing in the snapshot handles it.
- Every key is user-rebindable through a schema-validated config (`KeybindOverrides`, :242). A value of `"none"`/`false` unbinds a key, and unknown names throw `Unrecognized keybind` (:445).
- Aliases `enter`→`return` and `esc`→`escape` are expanded (`tui/src/keymap.tsx` ~:112).
- **Terminal capability:** the main TUI always requests the kitty keyboard protocol: `createCliRenderer({... exitOnCtrlC: false, useKittyKeyboard: {}, ...})` (`tui/src/app.tsx:182-193`). There is no `/terminal-setup` command and no detection or fallback advice anywhere in the snapshot. With no kitty support, shift+return falls back to whatever the terminal sends, and ctrl+j stays the portable newline.
- Run mode uses `useKittyKeyboard: { events: process.platform === "win32" }` (`opencode/src/cli/cmd/run/runtime.lifecycle.ts:188`).
- Windows: `win32DisableProcessedInput()` / `win32InstallCtrlCGuard()` keep `ENABLE_PROCESSED_INPUT` cleared. The comment explains: "On Windows, Ctrl+C becomes a CTRL_C_EVENT (instead of stdin input) when ENABLE_PROCESSED_INPUT is set. Various runtimes can re-apply console modes (sometimes on a later tick), and the flag is console-global" (`tui/src/terminal-win32.ts:58-69`). The fix combines a raw-mode hook with a low-frequency poll.
- **IME on submit:**
  - `onSubmit` double-defers: "IME: double-defer so the last composed character (e.g. Korean hangul) is flushed to plainText before we read it" (`index.tsx:1374-1378`).
  - `submitInner` then re-reads `input.plainText` and resyncs if the store lags (:942-948).
- **Submit guard** (`index.tsx:922-937`): "Prevent overlapping invocations (e.g. a double-pressed Enter, or the input's native onSubmit racing another dispatch). Without this guard, a second call slips past the empty-input check ... sending a phantom empty prompt to a freshly created session." A test covers it: `tui/test/cli/tui/prompt-submit-race.test.ts:71,85`.
- **Enter while the autocomplete is open** selects the item instead of submitting (`if (auto()?.visible) return false`, :951).
- **Magic words:** the trimmed input `"exit"`, `"quit"` or `":q"` destroys the renderer instead of sending (`index.tsx:955-959`). Run mode also matches `/exit`, `/quit` and `:q` (`prompt.shared.ts:40-43`).
- **Height:** `maxHeight = tuiConfig.prompt?.max_height ?? Math.max(6, floor(height/3))` (`index.tsx:1328`).

## 2. Paste

- **Bracketed paste** comes through `onPaste(event: PasteEvent)` (`index.tsx:1379-1403`):
  - Line endings are normalised: "Windows ConPTY/Terminal often sends CR-only newlines in bracketed paste".
  - An empty paste re-dispatches as a clipboard read: "Windows Terminal <1.25 can surface image-only clipboard as an empty bracketed paste" → `keymap.dispatchCommand("prompt.paste")`.
  - A default insertion is suppressed explicitly before the async work: "Once we cross an async boundary below, the terminal may perform its default paste unless we suppress it first".
- **Large-paste placeholder** (`pasteInputText`, `index.tsx:1189-1196`):
  - If `lineCount >= 3 || pastedContent.length > 150`, the paste inserts the virtual text `` `[Pasted ~${lineCount} lines]` `` plus a space, as an extmark with `virtual: true` and a paste style.
  - It also stores a `text` part whose `source.text.{start,end,value}` tracks the range (`pasteText`, :1132-1164).
  - Toggles: KV `paste_summary_enabled`, config `experimental.disable_paste_summary`, and keybind `app_toggle_paste_summary` (unbound by default, `keybind.ts:55`).
  - **On submit** the placeholder expands inline at its tracked range (`expandTrackedPastedText` with display-width slicing, `tui/src/prompt/part.ts:24-29`; `index.tsx:1018-1029`). The text parts are dropped from the message; it is sent as one text part.
  - **On edit,** the placeholder does not expand in place. It moves as an extmark, and `syncExtmarksWithPromptParts` drops parts whose extmark disappeared (:694-726).
  - **On copy,** `getClipboardText` is overridden to `expandPastedTextPlaceholders(text, parts)` (:1406-1408), so copying a selection yields the full pasted text. That is a nice touch.
  - **On external editor,** placeholders are expanded and *stay* expanded after return (:425-431, 498-503).
  - **History** stores the parts, so a recalled entry still has its placeholder and payload.
- **Trimming:** the summarised paste stores `pastedContent = normalizedText.trim()` (:1168, 1194). Leading and trailing whitespace and newlines of a large paste are silently lost. Small pastes insert `normalizedText` untrimmed (:1198).
- **Paste-burst detection** without bracketed paste: **not present** in the snapshot, since it relies on OpenTUI's paste event. Run mode only re-lays out after a paste: "Paste can leave the textarea layout stale until the next edit" (`footer.prompt.tsx:204-218`).
- **Image paste:**
  - The `input_paste: { key: "ctrl+v", preventDefault: false }` binding (`keybind.ts:159`) runs `prompt.paste`. It reads the system clipboard; `image/*` becomes an attachment and `text/plain` goes through `pasteInputText` (`index.tsx:364-385`).
  - Attachments insert `` `[Image N]` `` or `` `[PDF N]` `` as virtual extmarks, with a `file` part carrying a `data:` URL (:1207-1253).
- **File-path drop:**
  - If the whole trimmed paste is one path, `pastedFilepath` handles it. It strips quotes, decodes `file://` and un-escapes `\ ` on non-Windows (`index.tsx:75-84`).
  - `readLocalAttachment` maps the extension to a MIME type (`component/prompt/local-attachment.ts:25-34`): avif/gif/jpeg/png/webp/pdf become a binary attachment, and SVG becomes a text paste labelled `` `[SVG: name]` ``.
  - Anything else (source files, several dropped paths, URLs) falls through as plain text. Files are not auto-mentioned.

## 3. Editing

The defaults (`keybind.ts:162-195`) form an emacs-flavoured set:

| Action | Keys |
|---|---|
| move left / right | `left,ctrl+b` / `right,ctrl+f` |
| logical line home / end | `ctrl+a` / `ctrl+e` |
| **visual (wrapped) line** home / end | `alt+a` / `alt+e`, select variants `alt+shift+a` / `alt+shift+e` |
| buffer home / end | `home` / `end` |
| delete to line end / start | `ctrl+k` / `ctrl+u` |
| delete line | `ctrl+shift+d` |
| word forward / backward | `alt+f,alt+right,ctrl+right` / `alt+b,alt+left,ctrl+left` |
| delete word forward | `alt+d,alt+delete,ctrl+delete` |
| delete word backward | `ctrl+w,ctrl+backspace,alt+backspace` |
| undo / redo | `ctrl+-,super+z` / `ctrl+.,super+shift+z` |
| select all | `super+a` |
| shift-selection | `shift+arrows`, `shift+home/end` |

- **Kill ring:** none. `ctrl+k` and `ctrl+u` delete, and no yank is bound (`ctrl+y` is used for "copy-selection" in the debug console, `app.tsx:191`).
- **Undo/redo** exists, but its granularity lives inside OpenTUI and is not in the snapshot.
- **Vim mode:** none.
- **Visual vs logical rows:** logical-line and visual-line keys are explicitly separate (above). History up/down respects wrapped rows (see §4).
- **Graphemes and wide characters:**
  - Textarea offsets are display-width units, not code units.
  - `tui/src/prompt/display.ts:1-10` segments with `Intl.Segmenter(..., {granularity: "grapheme"})` and notes: "Textarea offsets count newlines as one position; Bun.stringWidth counts them as zero."
  - `displaySlice`/`displayCharAt` convert between the two. A test checks CJK around a placeholder (`tui/test/prompt/part.test.ts:24`).
- **IME:** only the submit double-defer above. Composition rendering is in OpenTUI and not visible.
- **Mouse:** `onMouseDown` focuses the prompt (`index.tsx:1420`).
- **Leader feedback:** while the `ctrl+x` leader is pending, prompt text dims to `textMuted` and the border goes neutral (`index.tsx:1272, 1356-1357`).

## 4. History

- **Scope:** global, in one file, `path.join(paths.state, "prompt-history.jsonl")` (`tui/src/prompt/history.tsx:53`). It is not per-project or per-session.
  - `MAX_HISTORY_ENTRIES = 50` (:27).
  - Each entry stores `{input, mode, parts}`, so placeholders, mentions and shell mode round-trip.
  - On load it parses line-by-line, skips corrupt lines and rewrites the file to "self-heal corruption and enforce the limit" (:54-61).
- **Dedupe:** consecutive exact JSON equality only (`isDuplicateEntry`, :44-47). The test is named "dedupes only identical consecutive entries".
- **Keys:** `history_previous: "up"`, `history_next: "down"` (`keybind.ts:196-197`).
  - Up acts only at cursor offset 0. If the cursor is elsewhere but on the first visual row, up jumps to offset 0 first (`if (input.scrollY + input.visualCursor.visualRow === 0) input.cursorOffset = 0`, `index.tsx:867-870`).
  - Down is symmetric at the last virtual line and end of text (:899-906).
  - Recall puts the cursor at 0 for older entries and at the end for newer ones.
- **Draft handling (main TUI): the draft blocks history.** `move()` returns nothing when `current.input !== input && input.length` (`history.tsx:73`), so a non-empty draft, or an edited recalled entry, freezes navigation.
  - Returning past the newest entry yields an empty prompt (`{ input: "", parts: [] }`, :82). There is no stash-and-restore.
- **Clear-to-history:** Ctrl+C on a non-empty prompt runs `clearPrompt()`. A draft of `DRAFT_RETENTION_MIN_CHARS = 20` trimmed characters or more, or any draft with attachments, is appended to history first (`index.tsx:101, 1255-1261`). An accidental clear is therefore recoverable with up-arrow.
- **Run-mode history is different and better** (`opencode/src/cli/cmd/run/prompt.shared.ts:1-147`): `HISTORY_LIMIT = 200`. Its header says: "When the user arrows up at cursor offset 0, the current draft is saved and history begins. Arrowing past the end restores the draft." Both composers dedupe consecutive entries and drop blank ones.
- **Prefix filtering and Ctrl-R reverse search:** **absent.** `ctrl+r` is bound to `session_rename` (`keybind.ts:90`).

## 5. Mentions and completion

The autocomplete lives in `tui/src/component/prompt/autocomplete.tsx`.

- **Triggers:**
  - `/` opens only at buffer position 0, with no whitespace before the cursor (:715-719).
  - `@` opens at the nearest `@` before the cursor that sits at the start or after whitespace, with no whitespace in the query (`mentionTriggerIndex`, `display.ts:38-48`; :722-726).
  - The popup closes when the cursor moves before the trigger, when whitespace is typed, or when "/<command>" is no longer the sole content (:697-708).
  - It reopens after a backspace removes the space (:711).
- **`@` sources:**
  - Files come from the server's "fff" finder, already ranked: "Trust the order returned by fff (frecency, fuzzy score, filename bonus, etc. are already factored in)" (:350-351). "it shouldn't be additionally sorted by fuzzysort as it will loose the results" (:510-511).
  - Agents (`@agentname`), MCP resources and reference aliases (`@alias/...`) are fuzzysorted over display and description, limit 10, weighted by local frecency: `score * (1 + frecencyScore)` (:524-544).
  - The frecency file is `state/frecency.jsonl`, with up to 1000 entries and score `frequency / (1 + days_since_open)` (`tui/src/prompt/frecency.tsx:10, 33-35`).
- **Line ranges:** `@path#10-20` becomes a `file` part with `?start=&end=` URL params (`autocomplete.tsx:25-45, 238-250`).
- **Inserted mentions:**
  - Each becomes a styled virtual extmark `@name` with a trailing space (only if the next char isn't already a space) (:168-236).
  - Re-mentioning the same file updates the existing part rather than duplicating it (:205-221).
  - `Tab` on a directory expands it into the query instead of selecting (`expandDirectory`, :580-597, 641-648).
- **Keys:**
  - prev `up,ctrl+p`; next `down,ctrl+n`; hide `escape`; select `return`; complete `tab` (`keybind.ts:211-215`).
  - Prompt "traits" capture escape/navigate/submit/tab only while the popup is visible, otherwise only `tab` (`tui/src/prompt/traits.ts:16-28`).
- **Slash list:**
  - It merges TUI palette commands that have a `slashName` (e.g. `/editor`, `/skills`, `/exit` with aliases `quit`, `q`) with server commands (MCP commands get the suffix `:mcp`; skills are excluded).
  - Entries are sorted alphabetically and padded into columns (:468-497; `keymap.tsx:260-285`).
  - Selecting a server command replaces the line with `"/name "`.
- **Argument hints and ghost text:** **not present** in the snapshot. Only a `description` column.
- **Multi-line slash commands:** the first line is the command and the rest is appended to arguments: "Parse command from first line, preserve multi-line content in arguments" (`index.tsx:1063-1083`). Typed text that starts with `/` but names no known server command is sent as an ordinary prompt.
- **Mouse hover workaround:** "When the filter changes ... the mousemove might still be triggered via a synthetic event as the layout moves underneath the cursor" (:160-166). A related effect copies the filter into a signal because "filter() reads reactive props.value plus non-reactive cursor/text state. On keypress those can be briefly out of sync" (:150-157).

## 6. Modes

- **Shell `!`:**
  - The `!` key binding is active only in normal mode, with no popup, at `visualCursor.offset === 0`. The `!` is consumed, not inserted, and the mode is set to `"shell"` (`index.tsx:808-833`).
  - Exit with `escape` (:835-841), or `backspace` at offset 0 (:843-852).
  - Display: border colour `theme.primary`, the label "Shell" in place of the agent name, model metadata hidden, placeholder `` `Run a command... "${example}"` `` (:1294-1303), footer hint "esc exit shell mode" (:1652-1656), and trait status `"SHELL"` (`traits.ts:25`).
  - Submit calls `session.shell` (:1051-1062), and the mode is saved into history.
- **`#` memory mode:** not present. In `@` completion, `#` is used only for line ranges.
- **Plan/agents:** `agent_cycle: "tab"` and `agent_cycle_reverse: "shift+tab"` (`keybind.ts:127-128`) cycle primary agents such as build and plan.
  - The current agent shows as a coloured left border plus a title-cased name; the model, provider and variant follow (`index.tsx:1271-1277, 1427-1455`).
  - `variant_cycle: "ctrl+t"`; `model_cycle_recent: "f2"`.
  - Because the prompt captures `tab`, Tab always means "next agent" unless the popup is open.
- **Placeholder:** a random example per session: `` `Ask anything... "${...}"` `` (:1302).

## 7. While busy

- **Typing ahead / queueing:** submit is not gated on session status. Prompts sent while busy are queued server-side.
  - The transcript marks them with a ` QUEUED ` badge when `message.id > pending` (`tui/src/routes/session/index.tsx:238-242, 1384, 1448`).
  - Main TUI: there is no way to edit, recall or delete a queued prompt. The keybind `session_queued_prompts: "<leader>q"` is *defined* (`keybind.ts:99`) but only registered in run mode. It collides with `app_exit`'s `<leader>q` (:48).
  - Run mode: the queued-prompt menu shows the footer `"queued · ctrl+e edit · ctrl+d remove"` (`opencode/src/cli/cmd/run/footer.command.tsx:602`). Edit removes the message from the server queue, then `composer.replacePrompt(item.prompt)`, which overwrites whatever draft is in the composer (`footer.view.tsx:560-563`).
- **Steering into the running turn:** no explicit mechanism in the TUI snapshot. Queued messages simply wait.
- **Interrupt:**
  - `session_interrupt: "escape"` (`keybind.ts:94`). It needs a double press within 5 s: the first press increments a counter, and the footer changes from "esc interrupt" to "esc again to interrupt"; the second calls `session.abort` (`index.tsx:386-416, 1565-1570`).
  - Esc priority: popup open → popup hides; shell mode → leave shell mode ("TODO: this should be its own command", :395); otherwise the interrupt counter.
  - Each press starts a fresh `setTimeout(..., 5000)` that zeroes the counter, and old timers are never cleared, so the window effectively starts at the first press.
  - **The draft is untouched by interrupt.**
- **Ctrl-C / exit:**
  - `input_clear: "ctrl+c"` is enabled only when the input is non-empty (`index.tsx:800-806`).
  - `app_exit: "ctrl+c,ctrl+d,<leader>q"` is enabled only when the prompt is unfocused or empty (`app.tsx:921-929`).
  - So Ctrl+C clears (saving a draft of 20 or more characters into history) and a second Ctrl+C exits immediately, with no "press again to exit" confirmation. Ctrl+D on an empty prompt also exits, while on text it deletes a character (`input_delete: "ctrl+d,..."`).
  - In run mode the command is "Clear prompt or exit" (`footer.prompt.tsx:906-920`).
- **Permissions:** the prompt is `disabled` while a permission or question is pending (`routes/session/index.tsx:236`). Keystrokes are `preventDefault`ed and pastes dropped (`index.tsx:1368-1373, 1380-1383`), so type-ahead during an approval is swallowed. The cursor is recoloured to the background to signal this (:245-249).

## 8. Drafts, stash, editor

- **Stash** (`tui/src/prompt/stash.tsx`): commands "Stash prompt", "Stash pop" and "Stash list" (`index.tsx:728-786`).
  - Default keys are `"none"` (`keybind.ts:153-155`), so the stash is reached only through the command palette (`ctrl+p`).
  - It persists `{input, parts, timestamp}` to `state/prompt-stash.jsonl` (`MAX_STASH_ENTRIES = 50`) as a LIFO.
  - The list dialog previews entries with `~N lines` and confirms deletion with a double `ctrl+d` ("`Press ${deleteHint()} again to confirm`", `component/dialog-stash.tsx:45`).
  - The stash does not record `mode`, so a stashed shell command pops back as a normal prompt.
- **Remount survival:** a module-level `let stashed` saves `{prompt, cursor}` in `onCleanup` and restores it in `onMount` (`index.tsx:138, 607-625`). A draft therefore survives route changes (home ↔ session) within the process. It is in-memory only: **no crash or exit recovery**. Only Ctrl+C-cleared drafts of 20 or more characters reach disk, through history.
- **External editor** (`editor_open: "<leader>e"`, i.e. `ctrl+x e`, plus `/editor`; `tui/src/editor.ts:9-37`; `index.tsx:417-507`):
  - `process.env.VISUAL || process.env.EDITOR`. If neither is set it silently returns, with no toast.
  - Temp file: `` path.join(os.tmpdir(), `${Date.now()}.md`) ``.
  - The command is split with `editor.split(" ")`, so editor paths containing spaces break.
  - A non-zero exit rejects. An empty result is ignored (`|| undefined`), so the editor cannot be used to clear the prompt.
  - The renderer is suspended and its buffer cleared around the editor.
  - Paste placeholders are expanded before editing. File and agent mentions are relocated afterward by `content.indexOf(virtualText)`, the first occurrence; a mention whose text was deleted is dropped ("if the virtual text is deleted, remove the part").
- **External append:** the server or plugins can push text into the composer through the `tui.prompt.append` event (`index.tsx:232-243`), with the comment "setTimeout is a workaround and needs to be addressed properly".
- **IDE editor context:** if an IDE selection exists, a `<system-reminder>` text part is prepended on submit, and the prompt footer shows a `file#12-20` label (`index.tsx:112-136, 1033-1049`). This is tangential to the composer itself.

## 9. Traps and gotchas (quoted or derived from code)

1. **Phantom empty prompt** from a double Enter or an onSubmit race. Fixed with a `submitting` guard (`index.tsx:922-937`; see the quote in §1).
2. **IME last-character loss** on submit with Korean hangul. Fixed with a double `setTimeout` plus a `plainText` resync (`index.tsx:942-948, 1375-1377`).
3. **Windows paste and clipboard quirks.** ConPTY sends CR-only newlines. Windows Terminal before 1.25 delivers an image clipboard as an empty bracketed paste. The terminal performs its own default paste if you await before `preventDefault` (`index.tsx:1385-1400`).
4. **Windows Ctrl+C** arrives as a console event unless `ENABLE_PROCESSED_INPUT` is cleared repeatedly, because runtimes re-apply it (`terminal-win32.ts:58-69`).
5. **Display-width offsets.** "Textarea offsets count newlines as one position; Bun.stringWidth counts them as zero" (`display.ts:6`). The code still uses raw `Bun.stringWidth` in places:
   - After the editor returns, `input.cursorOffset = Bun.stringWidth(content)` (`index.tsx:505`) undercounts by one per newline, so the cursor lands short of the end in multi-line text.
   - Extmark relocation uses `content.indexOf(virtualText)`, a UTF-16 index, as a display offset (:463-467). Wide characters before a mention will misplace it.
   (These are inferred latent bugs, not documented fixes.)
6. **`String.prototype.replace` with a string replacement.** `expandPastedTextPlaceholders` (`part.ts:11`) and the editor pre-expansion (`index.tsx:430`) call `result.replace(placeholder, part.text)`, so `$&`, `` $` ``, `$'` and `$$` in pasted content are interpreted as replacement patterns. The same calls replace the *first* occurrence of identical labels such as two `[Pasted ~3 lines]`. The submit path was fixed to use tracked ranges (test "only expands the tracked placeholder occurrence", `part.test.ts:39`), but the copy and editor paths were not. (Inferred.)
7. **Esc on a slash popup deletes your text.** `hide()` deletes from 0 to the cursor whenever the popup is `/`, the text starts with `/` and doesn't end with a space (`autocomplete.tsx:663-680`). Typing `/usr/lib/...` and pressing Esc to dismiss erases it. (Inferred from code.)
8. **A draft blocks history** (`history.tsx:73`). Pressing up with text typed does nothing, and editing a recalled entry locks navigation. Run mode solved this with draft save and restore, but the main TUI did not.
9. **Global 50-entry history** across all projects. It is small and leaks prompts between repositories.
10. **Typing `exit`, `quit` or `:q` quits the app** (`index.tsx:956`). Such a prompt cannot be sent literally.
11. **Ctrl+C on an empty prompt exits at once,** and Ctrl+D on an empty prompt exits. There is no confirmation, despite the double-press Esc for interrupt.
12. **Keybinding collisions in the defaults:** `<leader>q` is both `app_exit` and `session_queued_prompts` (`keybind.ts:48, 99`); `ctrl+d` is exit, delete-char, `session_delete` and `stash_delete`; `ctrl+r` renames the session rather than searching history. These rely on enable-conditions and focus scoping to disambiguate.
13. **Large pastes are trimmed** before being stored behind the placeholder (`index.tsx:1168`).
14. **Image label reuse:** the `[Image N]` number is `count + 1` of the remaining image parts (`index.tsx:1211-1216`). Delete `[Image 1]` and the next paste is labelled `[Image 2]` alongside an existing `[Image 2]`.
15. **Type-ahead swallowed** while a permission prompt is up (`index.tsx:1368-1373`).
16. **Run-mode queued "edit" overwrites the current draft** (`footer.view.tsx:560-563`).
17. **Workarounds flagged by the authors:** "setTimeout is a workaround and needs to be addressed properly" (`index.tsx:237, 1415`); "temporary hack to make sure the message is sent", a 50 ms delayed navigate after creating a session (:1117-1126); and `props.value // <- there surely is a better way to do this` (`autocomplete.tsx:145`).

## Ideas worth stealing

- **Placeholders as structured parts.** Attachments and summarised pastes are atomic virtual ranges in the buffer, each tied to a structured part with tracked `start`/`end`, expanded only at submit. History, stash and copy all carry the parts.
- **Copy expands placeholders** (`getClipboardText` override). A selection copied from the composer yields the real content, not the label.
- **Clear saves the draft.** Ctrl+C clear pushes a substantial draft (20 or more characters or any attachment) into history, so an accidental clear is one up-arrow from recovery.
- **Run-mode history semantics:** the draft is saved on the first up-arrow and restored past the newest entry; consecutive deduplication; blank entries never recorded.
- **Two-stage history keys.** Up off the first visual row first jumps to offset 0, then navigates. This works properly with wrapped rows.
- **Separate logical and visual line motion** (`ctrl+a`/`ctrl+e` vs `alt+a`/`alt+e`).
- **Paste fallbacks:** an empty bracketed paste falls back to a clipboard read, which handles image-only clipboards. CR/CRLF are normalised at the boundary. A single pasted path to an image or PDF becomes an attachment, with `file://` and shell escapes handled.
- **Rich `@` mentions:** files, agents, MCP resources and reference aliases in one ranked list; `#start-end` line ranges; Tab descends into directories; a frecency boost persisted to disk.
- **Shell mode as a first-class mode.** `!` at column 0 is consumed rather than inserted. The mode shows in border colour, label, placeholder and footer. Backspace at column 0 or Esc exits, and the mode is stored with each history entry.
- **Persistent JSONL state** (history, stash, frecency). Corrupt lines are skipped, retained entries are rewritten on load, and size is bounded.
- **Visible leader state:** the prompt dims while the leader key is pending.
- **Every key is a named, rebindable command,** with schema validation that rejects unknown names.
- **Queued-prompt management** in run mode: list, edit, remove.

## Traps to design against

- Do not mix code-unit, grapheme and display-column offsets. Pick one canonical offset space and convert only at the edges (see the `Bun.stringWidth`/`indexOf` slips).
- Never use substring search-and-replace to expand placeholders. Expand by tracked range, and never through an API with replacement-pattern semantics.
- Do not let dismissing a completion popup delete typed text.
- Do not block history browsing on a non-empty draft. Save and restore the draft instead.
- Do not make bare words like `exit`, `quit` or `:q` meta-commands in a free-text composer.
- Avoid immediate exit on Ctrl+C or Ctrl+D with an empty buffer when a running turn or queued work exists. At least make the meaning of Esc and Ctrl+C consistent.
- Do not overload one chord across clear, exit, delete and rename by focus state alone. Avoid default collisions (`<leader>q`).
- Do not silently swallow keystrokes while a modal is up. Buffer them or route them visibly.
- Do not trim paste payloads or reuse attachment labels.
- Recalling a queued message for edit must not clobber the current draft.
- Report a missing `$VISUAL`/`$EDITOR` instead of doing nothing. Parse the editor command properly rather than with `split(" ")`. Decide explicitly what an empty editor result means.
- Persist the live draft for crash or exit recovery. An in-memory remount stash is not enough.
- Plan for submit re-entrancy and IME commit ordering from the start, rather than patching them with deferrals.
