# Chat ergonomics baseline

The [terminal redesign discussion plan](TUI_PLAN.md) adds the operator's explicit
2026-10-07 requirement: vivid colors, animation, visual quality and useful density
are first-pass work alongside inline slash discovery. Advanced controls follow
later. The delivered behaviors below remain the current implementation baseline.

The operator adopted this direction on2026-10-07: **chat-first, tools-secondary**, with a capable composer, discoverable commands and a readable conversation. This is the baseline, not ornamental polish. Powerful Lua/JSON/CLI paths remain available; ordinary interaction should not require memorizing them.

## Composer: delivered first editing slice

Enter sends. Shift-Enter, Alt-Enter or Ctrl-J inserts a newline. Bracketed paste stays literal and never submits itself, invokes a shortcut, or dispatches a command.

| Key | Behavior |
| --- | --- |
| Up / Down | Move between explicit newline-delimited draft lines, maintaining the desired terminal-cell column across short lines. On the first/last logical line, browse history. |
| Ctrl-P / Ctrl-N | Explicit older/newer history, even inside a multiline draft. Returning to the current draft restores its original cursor as well as its text. |
| Left / Right | Move between UTF-8 codepoint boundaries. Application-mode arrow sequences are also supported. |
| Home / End, Ctrl-A / Ctrl-E | Start/end of the current logical line. |
| Ctrl-Home / Ctrl-End | Start/end of the entire draft (when the terminal sends the corresponding modified-key sequences). |
| Alt-B / Alt-F, Ctrl-Left / Ctrl-Right | Move backward/forward through tokens separated by ASCII whitespace. Option-as-Meta/terminal key mapping may be needed for Alt shortcuts on macOS. |
| Ctrl-W, Alt-Backspace | Delete the preceding token. |
| Alt-D | Delete through the next token. |
| Ctrl-K | Kill to line end; at line end, kill the newline. |
| Ctrl-U | Clear the draft. |
| Ctrl-Y | Yank the last deletion/clear. This is one transient kill buffer, not a persistent kill ring or general undo. An oversized yank is rejected atomically. |
| Ctrl-C | Request stop and clear pending queue; preserve the current unsent draft. |
| Ctrl-Q | Stop and exit, persisting the current draft/cursor. |

Vertical motion uses the existing renderer's terminal-cell rules, including wide characters, combining marks, pasted tabs and escaped invalid bytes. This is codepoint editing, not full grapheme-cluster editing. Wrapped visual rows are not separate editing lines yet. History selection is a draft preview; it is never submitted merely by navigation.

## Command discovery: delivered

Typing `/` in a fresh draft opens the inline command list. Typing filters names,
descriptions and groups; Up/Down moves the highlighted selection. Tab fills that
visible selection without submitting. Enter runs an argument-free selection or
fills an argument-taking command with a trailing space. Exact complete commands
still submit normally. Escape dismisses without changing the draft; Ctrl-C keeps
its stop behavior. Paste, restored drafts and history recall do not open the list.
Outside the inline list, Tab retains prefix completion at the end of the token.

The composer has cyan rounded borders, grows with the draft up to six rows,
and preserves cursor visibility. The header shows a compact session label rather
than a full filesystem path; `/session` retains full details. A ruled status line
separates runtime activity from the conversation. The explicit searchable palette
below retains its separate Enter-to-load behavior.

`/help`, `/keys`, `/queue` and `/cancel` are immediate local TUI controls, even while work is active. Other session/model/context commands keep their owning worker's turn-boundary semantics. `/clear` clears display only, not model context. `/queue` distinguishes pending prompts from recovered drafts, whose submission remains explicit.

## External editor: delivered

**Ctrl-G** opens the current draft in `$VISUAL`, falling back to `$EDITOR` and then `vi`. **`/edit`** opens a blank draft. This is idle-only: a busy turn refuses the handoff without changing the current draft. Set a foreground command that waits for editing to finish, e.g. `VISUAL='code --wait'` or `EDITOR='nvim'`. Editor command strings are trusted operator configuration, with the private temporary file appended as a shell-quoted argument.

The terminal returns to its ordinary screen/canonical mode for the editor and restores the TUI afterwards. A private 0600 temporary file starts with the draft. Only a successful editor exit and a regular, nonsymlink file of at most1MiB are imported; failures retain the original text/cursor. Unchanged text preserves cursor position; changed text returns with the cursor at its end. Typeahead is discarded on return. **Enter still sends explicitly.** The temporary file is removed afterwards. This does not govern shared editor daemons or claim all remote/editor effects have settled.

## Searchable palette: delivered

**Ctrl-Space** (NUL in ordinary terminals) or **Ctrl-T** opens the command palette without disturbing the draft. **`/commands`** also opens it locally. Type to filter names, descriptions and groups case-insensitively; exact and partial command-name matches rank ahead of description/group matches. **Up/Down**, **Ctrl-P/N** or **Tab** select a result. **Enter loads the command into the composer, not into the worker.** A second explicit Enter sends it. Commands with arguments load their name and a trailing space for you to fill in.

**Escape**, **Ctrl-C** or the opening shortcut backs out to the untouched draft and cursor. Ctrl-C is modal while the palette is open; close it before using Ctrl-C to stop the worker. Ctrl-Q still quits. Paste only filters; embedded control bytes do not execute commands. The palette is a local bounded viewport and remains available during busy work; selecting a command does not change model/workflow settings or queue a turn.

When selection replaces a nonempty draft, that draft is retained in **`/drafts`**; **`/draft N`** retrieves it without submitting. If draft storage is full, selection is refused and the original draft remains. The number of visible results adapts to the window; expand very short windows to see more entries. Lone Escape is resolved on the input polling idle interval, distinguishing it from arrow/meta sequences.

## Next separate UX slices

1. Wrapped visual-row navigation and a more generous adaptive composer viewport.
2. Human-friendly session/model pickers beyond the general command palette.
3. Distinct conversation blocks, bounded collapsible tool output, unobtrusive truthful activity, search, unread/follow-tail controls.

Keep useful expert paths and truthful work ownership. A polished display must not pretend a stored steer was consumed, a stopped request settled remote effects, a tool completion accepted a unit, or a source commit activated a running executable.
