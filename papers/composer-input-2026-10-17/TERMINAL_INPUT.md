# Composer terminal input: wheel scroll (arconaut-uss) and Shift+Enter (arconaut-v1i)

Research only. Written 2026-10-17. Blackbird worktree `work/glm-bugbash-2026-10-17` @ `1180d2bd45b4`.
No tracked source was edited, no TUI was run, and no reference code was executed.

## Citation conventions

- **`BB:`** means a Blackbird path in this worktree, e.g. `BB:src/terminal.cpp:924`.
- **`Q:`** means a path under `/Users/patrickbeam/projects/blackbird/quarantine/`.
- **Reference revisions** come from existing acquisition manifests:

| Reference | Path / revision |
|---|---|
| kitty | `Q:tui-2026-10-15/input/kitty` @ `0d006c44b3d9` |
| Ghostty | `Q:tui-2026-10-15/substrate/source/ghostty` @ `22fa134a07f5` |
| xterm ctlseqs | `Q:tui-2026-10-15/input/documents/xterm-ctlseqs.html`, "updated for XTerm Patch #411 (2026/08/23)" |
| codex | `08e2b58b07b8` |
| claude-code-unofficial | `eec3692193a3`. A third-party reconstruction, not Anthropic source. |
| claude-code-official-public | `525d3b353126` (CHANGELOG) |
| claude-code-official-distribution | 2.1.286 binary |
| opencode | `2006259a02a8` |
| crush | `76cc5c574e15` |
| gemini-cli | `c6bccb7ecbf6` |
| qwen-code | `310f4ba3ab95` |
| kimi-cli | `9ab1286b8fe4` |
| pi | `8ce69e9d2b17` |
| oh-my-pi | `8b25ad4a0562` |
| notcurses | `b26048eebc74` |
| libvterm | `934bc2fbf218` |
| ftxui | `d807cbca267a` |
| phux | `0fd4e511621f` |

- **`W:`** means a web primary source read 2026-10-17. These were read at branch head, so there is no pinned commit, and WebFetch gave no line numbers. Function names are given instead.
- **`[agent]`** marks file:line from a delegated read-only sweep. I did not re-open those lines. Anything without `[agent]` I read myself.

---

## 0. Blackbird facts relevant to both bugs

**Custody.**
- Entry string: `enter_screen = "\x18\x1b[?1049h\x1b[?2004h"` (`BB:src/terminal_custody.cpp:51`).
- Exit string: `leave_screen = "\x18" + kitty-graphics delete + "\x1b[?2026l\x1b[0m\x1b[?2004l\x1b[?1049l\x1b[?25h"` (`:52-53`).
- `leave_screen` is written on three paths:
  - `TerminalMode::suspend()`, the editor handoff (`:263`).
  - The destructor (`:290`).
  - The constructor sizes its credit by `leave_screen.size()` (`:200`).
- `resume()` re-sends `enter_screen` (`:250`).
- `rollback()` (`:182-191`) restores termios only. No bytes are written on that path.
- No mouse, 1007, kitty or modifyOtherKeys mode is set. No SIGTERM/SIGHUP terminal-restore handler exists in `terminal_custody.cpp`.

**Parser** (`Composer::feed_impl`, `BB:src/terminal.cpp:890-1025`).
- ESC starts accumulation (`:1020-1022`).
- Byte 2 equal to `[` or `O` continues (`:924`).
- From byte 3, parameter bytes 0x30-0x3F continue (`:926`). The next byte ends the sequence, and it is matched by exact string (`:938-1014`) or **dropped** (`:1014`).
- Consequences:
  - SGR mouse `CSI < b;x;y M/m` accumulates cleanly ('<', digits and ';' are all 0x30-0x3F) and is dropped today, so it is harmless.
  - `CSI 13;2u` and `CSI 27;2;13~` are also accumulated and silently dropped.
  - Intermediate bytes 0x20-0x2F are **not** continued.
  - X10 mouse `CSI M Cb Cx Cy` terminates at `M`, so its three raw bytes would be **inserted into the draft as text**. Never enable 1000 without 1006.
- A lone Esc is resolved by a 50 ms deadline (`:3226-3227`, `flush_escape` `:710-720`).

**Bindings relevant here.**
- Up/Down (`CSI A/B` or `SS3 A/B`) go to `vertical()` (`:975-977`). At the first or last line, `vertical()` calls `history()` (`:789-794`, `:769-788`).
- When the inspector is focused, arrows navigate it instead (`:937-958`). When the palette or slash menu is open, arrows move the selection (`:960-973`).
- `\x1b\r` and `\x1b\n` insert a newline (`:918-923`). Ctrl-J (0x0A) inserts a newline (`:1182-1184`). CR (0x0D) submits (`:1140-1181`).
- PgUp/PgDn give `InputAction::page_up/down` (`:1011-1013`), which scroll the transcript by rows/2 (`:3389-3392`).
- Ctrl-P/Ctrl-N already call `history()` (`:1131-1134`).
- Alt-b/f/d, Alt-Backspace and Ctrl/Alt-arrows are matched as legacy bytes (`:991-1009`).
- C0 bindings, all bare bytes: 0, 1, 3, 4, 5, 7, 9, 10, 11, 13, 14, 15, 16, 17, 20, 21, 23, 24, 25, 28, 29 (`:1024-1200`).

**Foreground PTY.** While a foreground PTY job owns input, every outer-terminal byte is forwarded verbatim to the child (`append_child_input`, `:3152-3182`). Only bracketed-paste markers and Ctrl-B (0x02, the release key) are interpreted. **Any input mode Blackbird enables is therefore also seen by the child**: mouse reports, CSI-u keys, and modifyOtherKeys keys.

**Hypothesis for arconaut-uss: CONFIRMED** for the operator's terminals (the journal names Ghostty and Alacritty).
- Both enable alternate scroll by default.
- In the alternate screen, with no mouse reporting, both translate wheel and trackpad motion into Up/Down arrow keys.
- Blackbird's `vertical()` then reaches `history()` at the first or last draft line.
- Evidence is in §1a.

---

## 1. Terminal behaviour

### 1a. Wheel in the alternate screen without mouse reporting

| Terminal | Alt-scroll default | Bytes sent | Does DECRST `?1007l` disable it? | Evidence |
|---|---|---|---|---|
| **Ghostty** | **ON** (`mouse_alternate_scroll`, value 1007, `.default = true`) | `CSI A`/`CSI B` normally; `SS3 A`/`SS3 B` when DECCKM is set. Sends `y.magnitude()` copies. Ignores kitty flags. Default multiplier: 3 discrete, 1 precision (trackpad). | **Yes.** The conversion requires the alt screen, `mouse_event == .none` and the mode set. With 1007 off and no reporting, the wheel does nothing (the alt screen has no scrollback). | `Q:…/ghostty/src/terminal/modes.zig:394`; `src/Surface.zig:3665-3696`; `src/config/Config.zig:987-1000` |
| **kitty** | **Always.** There is no 1007 mode. | `fake_scroll()` encodes Up/Down through `encode_glfw_key_event` with the **current kitty flags and DECCKM**. Legacy: `CSI A` or `SS3 A`. Under any kitty flag: `CSI A`. | **No.** kitty has no 1007; `modes.h` lists 1000-1016, 1048, 47 and no 1007. Only mouse tracking stops it. | `Q:…/kitty/kitty/mouse.c:1781-1794`; `kitty/keys.c:375-391`; `kitty/modes.h:62-75` |
| **Alacritty** | **ON** (`TermMode::ALTERNATE_SCROLL` in the default) | Always `ESC O A`/`ESC O B`, regardless of DECCKM. About `scrolling.multiplier` (default 3) per notch. Holding Shift skips it. | **Yes** (`unset_private_mode` removes it). | W: github.com/alacritty/alacritty `alacritty/src/input/mod.rs` `scroll_terminal`; `alacritty_terminal/src/term/mod.rs`; CHANGELOG 0.4.0/0.7.0; alacritty.org/config-alacritty.html |
| **WezTerm** | **Always** | Up/Down key events `alternate_buffer_wheel_scroll_speed` times (default 3). DECCKM respected. | **No** (no 1007 check in `mouse_wheel`) | W: wezterm.org/config/lua/config/alternate_buffer_wheel_scroll_speed.html; `term/src/terminalstate/mouse.rs` `mouse_wheel`; `termwiz/src/input.rs` |
| **iTerm2** | **OFF.** The internal flag starts NO. The app's `?1007h` is honoured if the profile allows it ("Terminal may enable alternate mouse scroll", default YES). The Advanced `alternateMouseScroll` ("Scroll wheel sends arrow keys when in alternate screen mode") defaults NO. | `ceil(|delta|)` arrows per event. Option held disables it. | Partly. `?1007l` clears the app flag, but the Advanced setting and "Automatically enable" are checked first and win. | W: `sources/VT100/VT100Terminal.m` `executeDecSetReset:` case 1007; `PTYTextView.m` `mouseHandlerAlternateScrollModeIsEnabled:`; `PTYMouseHandler.m`; `iTermProfilePreferences.m`; `iTermAdvancedSettingsModel.m`; iterm2.com/documentation-preferences-profiles-terminal.html |
| **Terminal.app** | UNVERIFIED (closed source; Apple docs are silent) | UNVERIFIED | UNVERIFIED | W: support.apple.com/guide/terminal (profiles keyboard/advanced) |
| **xterm** | **OFF.** The `alternateScroll` resource defaults "false"; the wheel does scroll-back/forw. | When on: cursor up/down per DECCKM (`CSI A` or `SS3 A`). Count per notch UNVERIFIED. | **Yes** (DECRST 1007 = "Disable Alternate Scroll Mode") | `Q:…/input/documents/xterm-ctlseqs.html` → text lines 988-989, 1134-1135, 3021-3030; W: invisible-island.net/xterm/manpage/xterm.html `alternateScroll` |
| **tmux ≥3.6, `mouse on`** | tmux never synthesizes arrows. The default `WheelUpPane` binding does `send -M` when `alternate_on`, and `input_key_mouse` drops the event if the pane has no mouse mode. **Result: wheel is inert** for Blackbird. CHANGES 3.6: "Don't enter copy mode on mouse wheel in alternate screen". | none | n/a (tmux has no 1007) | W: github.com/tmux/tmux `key-bindings.c`, `input-keys.c` `input_key_mouse`, CHANGES; man.openbsd.org/tmux.1 |
| **tmux, `mouse off`** | Handled by the outer terminal. Whether those arrows reach the pane depends on tmux's own use of the outer alternate screen. UNVERIFIED. | | | |

**Reference confirmation of the convention.** phux encodes the same rule: "xterm's alternateScroll (DECSET 1007, on unless the program clears it): a program on the alternate screen that does not track the mouse gets the wheel as arrow keys" (`Q:phux/clients/phux-web/tests/input.rs:305-307`; `clients/phux-web/src/input.rs:228-245`).

**Can wheel arrows be told apart from typed arrows? No, not robustly.**
- The bytes are identical.
- A per-`read()` burst heuristic (several identical arrows in one chunk) catches Ghostty's discrete wheel (×3). It fails for exactly the reported case: Ghostty trackpad "precision" scrolling uses multiplier 1, which yields single arrows (`Config.zig:999`).
- It would also misfire on a fast key-repeat that lands in one read.
- Claude Code needed JetBrains-specific "spurious arrow keys" fixes (`Q:claude-code-official-public/CHANGELOG.md:4473`) and a "pendingArrowBoost" heuristic (string present in the 2.1.286 bundle). That is evidence the heuristic route is a maintenance sink.

### 1b. Shift+Enter bytes

| Terminal | Default (no app request) | modifyOtherKeys mode 1 (`CSI >4;1m`) | mode 2 (`CSI >4;2m`) | kitty flag 1 (`CSI >1u`) |
|---|---|---|---|---|
| **Ghostty** | **`CSI 27;2;13~` by default.** The function-key table entry `.{ .mods = shift, .sequence = "\x1b[27;2;13~" }` has no modify-other-keys condition. Blackbird already receives it and drops it. | Parsed as `other_keys_none`. Only mode 2 sets `modify_other_keys_2`, so mode 1 is a no-op and output is the same as default. | `CSI 27;2;13~`. Also any char with a non-shift modifier becomes `CSI 27;m;cp~`. | `CSI 13;2u` (test "kitty: shift+enter emits CSI u") |
| **kitty** | `\r` (spec legacy table: Shift column of Enter is `0xd`) | **Ignored.** `screen_modify_other_keys` only logs "superseded by the kitty keyboard protocol". | ignored | `CSI 13;2u` (see the source trace below) |
| **Alacritty** | `\r` (inferred; Enter is not built as a sequence) | **Ignored.** The vte trait default `set_modify_other_keys` is empty and there is no override. | ignored | `CSI 13;2u`. Kitty protocol is always honoured (`kitty_keyboard: true` hardcoded). Shift-modified Enter has been disambiguated since **0.16** (CHANGELOG 0.16.0). |
| **WezTerm** | `\r` (Shift dropped) | `CSI 27;2;13~`. Honoured with no setting needed ("When wezterm receives the sequence `CSI >4;Nm`… the keyboard encoding is changed"). | `CSI 27;2;13~` | `CSI 13;2u` **only if the user sets `enable_kitty_keyboard = true`** (default false) |
| **iTerm2** | UNVERIFIED (probably `\r`) | `CSI 27;2;13~` (inferred from source: `iTermModifyOtherKeysMapper1.m` routes Return when `control \|\| shift`; format `\e[27;%d;%d~`) | `CSI 27;2;13~` | Supported (push/pop handlers exist; docs say "Applications should use the Kitty keyboard protocol"). Exact first version UNVERIFIED (3.5.x implied). |
| **Terminal.app** | UNVERIFIED. No protocol support documented. Claude Code reads native macOS modifier state when `TERM_PROGRAM=Apple_Terminal` (`Q:claude-code-unofficial/src/hooks/useTextInput.ts:261-265`). pi synthesizes `CSI 13;2u` from a bare CR when the OS reports Shift held (`Q:pi/packages/tui/src/terminal.ts:55-61` [agent]). | none known | none known | none known |
| **xterm** | `\r` (default `modifyOtherKeys` "0") | `CSI 27;2;13~` (source reading: input.c `ModifyOtherKeys()` level-1 has `case XK_Return: case XK_Tab: result = (modify_parm != 0)`; not run). `CSI 13;2u` if `formatOtherKeys=1`. | `CSI 27;2;13~` | **Not supported** (not in the kitty implementers list) |
| **tmux** | Plain `\r` unless `extended-keys` is on (default **off**). | With `extended-keys on`, the pane's `CSI >4;1m`/`2m` is honoured. tmux emits `CSI 27;2;13~` (default `extended-keys-format xterm`) or `CSI 13;2u` (`csi-u`), and only if the outer terminal gave tmux a distinct Shift+Enter. `always` forces mode 1. | same | **Not supported.** tmux does not implement kitty keyboard. Claude Code notes it "doesn't forward the kitty sequence to the outer terminal" (`Q:claude-code-unofficial/src/ink/terminal.ts:149-156`). |

Sources:
- Ghostty: `Q:…/ghostty/src/input/function_keys.zig:214-226`; `src/terminal/stream.zig:2096-2140` (`CSI > Pp ; Pv m` parsing); `src/terminal/stream_terminal.zig:716-719`; `src/input/key_encode.zig:386-449`, `:1402-1413`.
- kitty: `Q:…/kitty/kitty/screen.c:2100-2109`; `kitty/vt-parser.c:1468-1470`.
- xterm: ctlseqs text lines 1336-1359 and 2363-2400; W: man page `modifyOtherKeys`; github.com/ThomasDickey/xterm-snapshots `input.c`, `ptyx.h`.
- WezTerm: W: wezterm.org/config/key-encoding.html; `enable_kitty_keyboard.html`; `termwiz/src/input.rs` `KeyCode::encode`.
- Alacritty: W: `alacritty/src/input/keyboard.rs` `should_build_sequence`; `alacritty/src/config/ui_config.rs`; vte `src/ansi.rs`.
- iTerm2: W: iterm2.com/documentation-csiu.html; `-preferences-profiles-keys.html`; `iTerm2-3_5_0.changelog`; `sources/VT100/VT100Terminal.m`.
- tmux: W: tmux.1 `extended-keys`, `extended-keys-format`; `options-table.c` (defaults 0 and xterm); `input.c` `INPUT_CSI_MODSET`; `input-keys.c` `input_key_extended`; `tty-features.c` `extkeys` = `\E[>4;2m` / `\E[>4m`.

**Shift+Enter under kitty flag 1: the source trace.**
- The spec says: "The only exceptions are the Enter, Tab and Backspace keys which still generate the same bytes as in legacy mode" (`Q:…/kitty/docs/keyboard-protocol.rst:367-370`). Read alone, that can look like Shift+Enter stays CR.
- kitty's encoder shows the exception covers only **unmodified** keys:
  - `encode_function_key` computes `legacy_mode = !report_all_event_types && !disambiguate && !report_text` (`kitty/key_encoding.c:165`).
  - The bare `\r`/`\x7f`/`\t` returns are guarded by `!ev->mods.value` (`:179-202`) and `!(mods & ~LOCK_MASK)` (`:206-220`).
  - A Shift-modified Enter falls to `S(13,'u')` (`:230`) and is serialized as `CSI 13;2u`.
- Ghostty agrees (`key_encode.zig:1402-1413`), and so does Alacritty 0.16 (CHANGELOG "Enter, Tab, Backspace not disambiguated with shift in kitty keyboard's disambiguate mode" — fixed).

**Third-party Shift+Enter claim.** The Claude Code docs say Shift+Enter "works without setup" in Ghostty, Kitty, iTerm2, WezTerm, Warp, Apple Terminal and Windows Terminal, and in foot/Alacritty ≥0.16 via the kitty query. VS Code/Cursor/Zed/Alacritty <0.16 need `/terminal-setup`. tmux "also requires the tmux configuration… even when the outer terminal supports it" (W: code.claude.com/docs/en/terminal-config). This is achieved by Claude Code pushing kitty and modifyOtherKeys (§2), not by terminal defaults.

### 1c. Side effects of each protocol on Blackbird's existing bindings

**Kitty flag 1 ("disambiguate").** Spec: `Q:…/kitty/docs/keyboard-protocol.rst:341-374`; encoder: `kitty/key_encoding.c:160-240`, `:397-436`.
- Push with `CSI >1u`, pop with `CSI <u`. The stacks are **per screen**: "Terminals must maintain separate stacks for the main and alternate screens" (`:320-330`). So push after `?1049h` and pop before `?1049l`.
- Query with `CSI ?u`, reply `CSI ?flags u` (`:311-317`).

Under flag 1:

| Key | Becomes | Effect on Blackbird |
|---|---|---|
| Esc | `CSI 27u` | Breaks the lone-Esc path, though it would remove the 50 ms ambiguity if decoded |
| Ctrl+letter / Ctrl+symbol | `CSI <cp>;5u` (e.g. Ctrl-C → `CSI 99;5u`, Ctrl-\ → `CSI 92;5u`, Ctrl-Space → `CSI 32;5u`) | **Every C0 binding** in `feed_impl` stops matching, including Ctrl-J newline, Ctrl-P/N history, Ctrl-C cancel, Ctrl-Q quit, and the PTY release Ctrl-B at `:3155` |
| Alt+key, Ctrl+Alt+key, Shift+Alt+key | `CSI <cp>;3u` / `;7u` / `;4u` | Alt-b/f/d stop matching |
| Alt+Enter | `CSI 13;3u` | `\x1b\r` newline stops |
| Alt+Backspace | `CSI 127;3u` | Alt-Backspace stops |
| Ctrl+Backspace | `CSI 127;5u` | |
| Shift+Tab | `CSI 9;2u` (not `CSI Z`) | |
| Shift+Enter | `CSI 13;2u` | Wanted |
| Unmodified Enter/Tab/Backspace | Legacy `\r`, `\t`, `\x7f` | Unchanged |
| Plain text, Shift+letter | Text | Unchanged |
| Unmodified arrows/Home/End/PgUp/PgDn/F5+ | Legacy CSI forms | Unchanged. DECCKM `SS3` forms are no longer used (legacy_mode false, `:167`). F3 becomes `CSI 13~`. F1/F2/F4 become `CSI P/Q/S`. |
| **Keypad Enter** | **`CSI 57414u`** (keypad keys are no longer folded, `:461`) | Numpad Enter would **stop submitting** unless mapped |
| Lock modifiers | Folded into the modifier (Caps Lock adds 64) | oh-my-pi masks lock bits 64+128 (`Q:oh-my-pi/packages/tui/src/keys.ts:310` [agent]) |
| SIGINT | Ctrl-C is no longer SIGINT | Irrelevant: ISIG is already off, `BB:src/terminal_custody.cpp:240` |

Net: flag 1 is safe **only** with a complete CSI-u decoder that maps back to Blackbird's legacy semantics, and only if it is popped (or translated) before PTY forwarding.

**xterm modifyOtherKeys mode 1.**
- xterm: "the usual shift- and control-modifiers work as expected, but other modifiers (such as alt- and meta-modifiers) cause ordinary keys to be encoded as if they were function-keys. For example, alt-Tab sends CSI 2 7 ; 3 ; 9 ~" (ctlseqs text 2366-2370). The man page says level 1 excludes "keys… with well-known behavior, e.g., Tab, Backarrow and some special control character cases".
- Expected impact on Blackbird:
  - Alt-b/f/d may arrive as `CSI 27;3;98~`. This depends on xterm's `metaSendsEscape` and is UNVERIFIED per terminal.
  - Alt+Enter may become `CSI 27;3;13~` in xterm. Ghostty keeps `\x1b\r` when not in state 2 (`function_keys.zig:218`).
  - Ctrl-letters stay C0 in xterm level 1.
- Per terminal:
  - Ghostty: no-op.
  - kitty and Alacritty: ignored.
  - WezTerm: has its own mode-1 exclusion list (`'c'|'d'|ESC|DEL|BS`). So other Ctrl+letters **may** become `CSI 27;5;cp~`. UNVERIFIED; it needs a capture.
  - tmux: honoured only with `extended-keys` on.
- modifyOtherKeys is a single global flag, not per-screen stacked (oh-my-pi comment, `Q:oh-my-pi/packages/tui/src/terminal.ts:903-909` [agent]; codex `Q:codex/codex-rs/tui/src/tui/alternate_screen.rs:171-173`).
- **Reset choice:**
  - `CSI >4m` resets to the resource's *initial* value (ctlseqs: "If the second parameter is omitted, the resource is reset to its initial value").
  - `CSI >4;0m` forces 0.
  - In Ghostty both clear state 2 (`stream.zig:2098-2108`). In iTerm2 `CSI >4m` resets slot to −1.
  - `CSI >4m` is the more polite restore. codex, gemini-cli and pi use `>4;0m`; Claude Code and tmux use `>4m`.

**Mode 2.** All modifiers apply, so Ctrl+letters become `CSI 27;5;cp~` in xterm and Ghostty (`key_encode.zig:386-449`). This breaks every C0 binding, like kitty flag 1, without kitty's per-screen stack. It is not recommended for Blackbird.

**SGR mouse ?1000 + ?1006.**
- Wheel arrives as `CSI <64;x;yM` (up) and `CSI <65;x;yM` (down). Modifier bits: Shift 4, Meta 8, Ctrl 16. Mask `button & 0x43` (Claude Code `parse-keypress.ts:676-685`). There is no release event for wheel buttons (ctlseqs text ~3017-3021).
- Clicks become `CSI <0;x;yM/m`. The Blackbird parser already swallows these as unknown CSI (`BB:src/terminal.cpp:924-927`, `:1014`).
- **Cost:** native click-drag selection is suppressed while reporting is on. The conventional bypass is Shift-drag:
  - Ghostty `mouse-shift-capture` default false means Shift extends the selection (`Config.zig:948-973`). notcurses also sends XTSHIFTESCAPE `CSI >1s` to guarantee this (`Q:notcurses/src/lib/mice.c:42-43` [agent]).
  - On macOS iTerm2/Terminal.app, Option is commonly used. qwen-code documents "hold Shift (or Option on macOS)" (`Q:qwen-code/packages/cli/src/config/settingsSchema.ts:1275` [agent]).
  - Per-terminal bypass keys beyond Ghostty are UNVERIFIED.
- Ghostty skips alt-scroll whenever mouse reporting is active (`Surface.zig:3668-3670`), so 1007 becomes irrelevant.

---

## 2. Reference agents and libraries

### codex (Rust, crossterm)

Read directly. All paths are under `Q:codex/codex-rs/tui/src/`.

**Entry, in order** (`tui/alternate_screen.rs:85-124`):
1. `EnterAlternateScreen`.
2. `enable_keyboard_enhancement`:
   - `DisableModifyOtherKeys` (`CSI >4;0m`), then `PushKeyboardEnhancementFlags(DISAMBIGUATE|REPORT_ALTERNATE_KEYS[|REPORT_EVENT_TYPES])` (`tui/keyboard_modes.rs:216-238`).
   - REPORT_EVENT_TYPES is omitted for Ghostty and iTerm2 ("can leak shortcut release events") and for tmux without `csi-u` ("tmux's xterm key format also loses Shift-Enter when event types are reported") (`:240-259`).
   - `CSI >4;2m` only inside tmux with `extended-keys-format csi-u` (`:233-236`, `:270-278`).
   - Env opt-out `CODEX_TUI_DISABLE_KEYBOARD_ENHANCEMENT`; auto-disabled for VS Code on WSL (`:20-56`).
3. `configure_input`:
   - Capture on: `DisableAlternateScroll` (`?1007l`) plus `?1000h?1002h?1003h` plus `?1006h` (`alternate_screen.rs:52-58`, `:109-117`).
   - Capture off: disable mouse and `EnableAlternateScroll` (`?1007h`) (`:118-122`).

**Policy.** The owned full-screen transcript captures the mouse; onboarding and static pager overlays do not and keep wheel-as-arrows (`tui.rs:633-650`). tmux `mouse off` suppresses capture (`tui/tmux.rs:46-60`).

**Wheel.** `ScrollUp/Down` scroll ±3 rows (`transcript_view/input.rs:226-233`).

**Selection.** codex owns it: left-drag selection, right-click copy (`transcript_view/input.rs:234-240`; `tui/selection_clipboard.rs`).

**Exit and handoff** (`release_input` `:155-178`, `leave` `:180-188`):
- Disable mouse.
- On write error, re-send `?1003l ?1002l ?1000l ?1006l` individually (`:132-150`).
- `PopKeyboardEnhancementFlags`.
- `?1007l`.
- `LeaveAlternateScreen`.
- Process exit additionally sends `CSI <u` and `CSI >4;0m` (`keyboard_modes.rs:280-292`).

**Shift+Enter.** Default `insert_newline` = Ctrl-J, Ctrl-M, Enter, Shift+Enter, Alt+Enter (`keymap.rs:1688-1694`). The footer shows Shift+Enter only when enhanced keys are supported (`bottom_pane/chat_composer.rs:4446-4462`).

**Toggle.** `tui.alternate_screen = auto|always|never` and `--no-alt-screen`, where inline mode keeps native scrollback (`Q:codex/codex-rs/protocol/src/config_types.rs:647-659`).

### Claude Code (TypeScript/Ink)

Unofficial reconstruction, read directly. Paths are under `Q:claude-code-unofficial/src/`.

**Modes.**
- Kitty push/pop and modifyOtherKeys: `ENABLE_KITTY_KEYBOARD = CSI >1u`, `DISABLE = CSI <u`, `ENABLE_MODIFY_OTHER_KEYS = CSI >4;2m`, `DISABLE = CSI >4m` (`ink/termio/csi.ts:296-319`).
- Both are written at raw-mode entry, but only for the allowlist `iTerm.app, kitty, WezTerm, ghostty, tmux, windows-terminal` (`ink/terminal.ts:148-165`; `ink/components/App.tsx:236-244`).
- Comment on why: "some terminals honor the enable and emit codepoints our input parser doesn't handle (notably over SSH and in xterm.js-based terminals like VS Code)".
- Re-assertion pops before pushing so the kitty stack does not grow ("the shell is left in CSI u mode where Ctrl+C/Ctrl+D leak as escape sequences") (`ink/ink.tsx:875-908`).
- Mouse `?1000h ?1002h ?1003h ?1006h` is enabled only in the alt-screen "fullscreen" mode (`ink/termio/dec.ts:46-61`). No 1007.

**Exit** (`ink/ink.tsx:1482-1500`): `?1049l`, mouse off, stdin drain "so in-flight mouse events don't leak to the shell", `CSI >4m`, `CSI <u`, `?1004l`, `?2004l`, `?25h`.

**Parser.** `CSI_U_RE` and `MODIFY_OTHER_KEYS_RE` (`/^\x1b\[27;(\d+);(\d+)~/`, with the comment "Emitted by Ghostty/tmux/xterm…") (`ink/parse-keypress.ts:18-31`). Wheel uses SGR `button & 0x43` 0x40/0x41, plus an X10 fallback (`:676-698`).

**Newline.** `handleEnter`: backslash+Enter, then `key.meta || key.shift`, then the Apple_Terminal native-Shift probe (`hooks/useTextInput.ts:247-267`).

**Wheel binding.** `wheelup` maps to `scroll:lineUp` (`keybindings/defaultBindings.ts:196-201`).

**Toggles.**
- `CLAUDE_CODE_DISABLE_MOUSE` keeps the alt screen but skips capture "so tmux/kitty/terminal-native copy-on-select keeps working" (`utils/fullscreen.ts:132-142`).
- `CLAUDE_CODE_DISABLE_MOUSE_CLICKS` keeps wheel only (`:144-153`).
- tmux hint: "scroll with PgUp/PgDn · or add 'set -g mouse on'" (`:180-196`).

**`/terminal-setup`.**
- Native list Ghostty/kitty/iTerm2/WezTerm/Warp (`commands/terminalSetup/terminalSetup.tsx:25-31`).
- VS Code/Cursor/Windsurf `keybindings.json`: shift+enter → `sendSequence "\u001b\r"` (`:194-260`).
- Alacritty `[[keyboard.bindings]] … chars = "\u001B\r"` (`:373-377`).
- Terminal.app: PlistBuddy sets `useOptionAsMetaKey` (`:270-310`).

**Official 2.1.286.**
- The bundle contains `Ha(">5u")`, `Ha(">1u")`, `Ha("<u")`, `Ha(">4;2m")`, `Ha(">4m")` and a `CSI ?u` kitty query (`Q:claude-code-official-distribution/distributions/2.1.286/darwin-arm64/claude`, string scan only).
- CHANGELOG lines:
  - 1355: kitty-query detection for foot and Alacritty 0.16+.
  - 2184: Ctrl+[ broken under modified-key encodings.
  - 3844: "Fixed mouse wheel scrolling prompt history instead of the transcript". This is the same bug class.
  - 4711: newlines lost when kitty sequences appear inside bracketed paste.
  - 3811: "hold {key} for native selection" hint per terminal.

### opencode (TypeScript, OpenTUI; the renderer is not in quarantine)

- Renderer options `useKittyKeyboard: {}` and `useMouse: !OPENCODE_DISABLE_MOUSE && config.mouse` (`Q:opencode/packages/tui/src/app.tsx:182-193`). The run-mode footer uses `useMouse: false` (`packages/opencode/src/cli/cmd/run/runtime.lifecycle.ts:184-188`).
- `input_newline` = `shift+return,ctrl+return,alt+return,ctrl+j` (`packages/tui/src/config/keybind.ts:161`).
- Docs: Windows Terminal `sendInput "\u001b[13;2u"` for shift+enter (`packages/web/src/content/docs/keybinds.mdx:261-295`).
- `mouse` setting: "When disabled, the terminal's native mouse selection/scrolling behavior is preserved" (`tui.mdx:398`). Env `OPENCODE_DISABLE_MOUSE` (`cli.mdx:699`).
- The exact bytes OpenTUI emits are UNVERIFIED (dependency not acquired).

### Others [agent]

**crush** (bubbletea v2; library not in tree, so library defaults are UNVERIFIED).
- `v.AltScreen = true` with `MouseModeCellMotion` (?1002), or AllMotion for hover (`internal/ui/model/ui.go:3684-3703`).
- Wheel is coalesced every 16 ms, delta used as lines; the code notes Ghostty sends 3 per tick (`filter.go:13-104`, `ui.go:1346-1371`).
- Newline `shift+enter`, `ctrl+j` (`keys.go:139-145`).
- Has its own chat selection and copy, plus OSC 52.
- Toggle via `options.tui.mouse` and the palette entry "Disable Mouse": "let the terminal emulator or tmux handle text selection" (`config/config.go:344`; `dialog/commands.go:560-565`).

**gemini-cli** (`packages/core/src/utils/terminal.ts`).
- Mouse `?1002h?1006h` (:28). Kitty `>1u`/`<u` (:37/:41). modifyOtherKeys `>4;2m`/`>4;0m` (:45/:49).
- Probes `CSI ?u`, `CSI >4;?m`, `CSI >q` and DA1. Kitty is preferred, with modifyOtherKeys (level ≥2) as fallback (`terminalCapabilityManager.ts:44-48`, `:265-281`).
- Cleanup `\x1b[<u\x1b[>4;0m\x1b[?2004l\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1006l` (:22-23).
- Mouse only with the opt-in alt buffer. Ctrl+S toggles mouse; F9 is copy mode (`keyBindings.ts:392-393`).
- `CSI 27;m;k~` is rewritten to CSI-u (`KeypressContext.tsx:566-570`). Backslash-Enter within 5 ms counts as Shift+Enter.
- `/terminal-setup` writes VS Code `'\\\r\n'`.

**qwen-code.**
- Kitty `>1u`/`<u`, re-pushed after entering the alt screen because "flags are per screen buffer" (`ui/utils/kittyProtocolDetector.ts:14-15`, `:134-152`).
- Mouse `?1002h?1006h`; 3 lines per wheel tick (`ui/utils/mouse.ts:219-224`; `ScrollableList.tsx:115`).
- Decodes `CSI 27;2;13~` explicitly, noting "Ghostty emit[s] this form" (`KeypressContext.tsx:546-554`).
- Has its own selection. Documents Shift/Option-drag. `ui.mouseTracking` toggle.

**kimi-cli.** Inline prompt_toolkit with no mouse. Newline is Esc-Enter and Ctrl-J (`ui/shell/prompt.py:1322-1329`).

**pi.**
- Kitty probe pushes `>7u` then queries `?u` + DA1. Fallback `>4;2m`. Exit `<u`, `>4;0m`.
- Fullscreen mouse `?1000h?1002h?1004h?1006h`, default on (`packages/tui/src/terminal.ts:11-14`, `:253-291`, `:366-442`; `tui-alt-screen.ts:62-68`, `:271`, `:366-375`).
- Newline: `shift+enter`, `ctrl+j`, `\x1b\r`, backslash-Enter (`keybindings.ts:143-144`; `components/editor.ts:889-922`).

**oh-my-pi.**
- Queries `?u` + DA1, then pushes `>5u` (disambiguate + alternates, "regression #3259"), `>7u` or `>1u`; fallback `>4;2m` (`packages/tui/src/terminal.ts:1463-1503`, `:1869`).
- "Native text selection becomes Shift+drag while on" (`tui.ts:1276`).
- Masks lock bits.

**notcurses.**
- `CSI >u` + `CSI =27u` (flags 1|2|8|16); fallback XTMODKEYS `\x1b[>2;1m\x1b[>4;2m`; undo `\x1b[>2m\x1b[>4m`. Pops and re-pushes on every alt-screen switch (`src/lib/termdesc.c:389-400`, `:461-465`, `:528-595`, `:1239-1244`; `termdesc.h:22,27`).
- Mouse: XTSHIFTESCAPE `>1s` then `?100x;1006h` (`mice.c:3-55`).
- Defines but never emits `SET_ALTERNATE_SCROLL` 1007 (`internal.h:1184`).

**libvterm** (as an emulator).
- Encodes Shift+Enter as `CSI 13;2u` and Alt+Enter as `ESC CR` (`src/keyboard.c:166-177`).
- No 1007, modifyOtherKeys or kitty support (`src/state.c:777-936`).

**ftxui.** `?1000h?1003h?1015h?1006h`. CSI `u` and `~` are opaque, so Shift+Enter is not decoded (`src/ftxui/component/app.cpp:694-709`; `terminal_input_parser.cpp:404-479`).

**libvaxis.** Kitty flags 29/31; `CSI 13;2u` decodes to shift+enter; `CSI 27;…~` is dropped; wheel 64/65, 3 lines (`src/Key.zig:25-31`; `Parser.zig:505-700`; `vxfw/ListView.zig:58`).

**neovim TUI.** `?u` + DA1, then `>3u` or `>4;2m`; reset `<u` / `>4;0m`; never modifyOtherKeys inside kitty (`src/nvim/tui/tui.c:309-347`, `:2554-2563`).

**libtermkey.** `CSI 13;2u` and `CSI 27;2;13~` both decode to Shift+Enter (`driver-csi.c:97-155`).

**phux** (read directly).
- Host entry `?1049h`, `CSI >1u` ("disambiguate only, so modified keys… arrive as CSI u while plain text stays plain bytes"), `?2004h`, `?1004h`, and optional `?1002h?1006h` (`Q:phux/crates/phux-tui/src/attach/driver/terminal.rs:146-173`).
- Reset order: mouse off **before** leaving the alt screen "so native selection returns", then pop kitty, then `?1049l` (`:220-243`).
- ADR-0048 tradeoff: "Enabling outer tracking suppresses the host terminal's native click-drag selection… We rely on the near-universal terminal convention that holding Shift bypasses application mouse reporting… Hosts that do not honour Shift-bypass lose easy selection until `mouse = false`" (`docs/adr/0048-drag-to-resize-and-default-mouse-capture.md:101-107`).

### Cross-reference patterns

| Pattern | Who does it |
|---|---|
| Enable ?1007 | Nobody except codex's non-capturing overlays |
| Disable ?1007 explicitly when capturing the mouse | codex |
| Decode both `CSI 13;2u` and `CSI 27;2;13~` | Claude Code, gemini, qwen, pi, oh-my-pi, notcurses, libtermkey, neovim |
| Mouse-capture opt-out toggle | opencode, crush, gemini, qwen, codex (no-alt-screen), Claude Code, phux |
| Probe `CSI ?u` + DA1 sentinel before pushing kitty | pi, oh-my-pi, gemini, qwen, notcurses, libvaxis, neovim, Claude 2.1.286 |
| Allowlist by `TERM_PROGRAM` instead | Claude Code (reconstruction) |

---

## 3. Options for Blackbird (tradeoffs, not a decision)

### Bug arconaut-uss (wheel)

**W1. Enable SGR mouse in the composer screen: `?1000h ?1006h`.** Map `CSI <64;…M` and `CSI <65;…M` (mask 0x43) to a new transcript line-scroll action of N lines. codex and qwen use 3. Today PgUp scrolls rows/2 (`BB:src/terminal.cpp:3389`).
- Works in all eight cases where the terminal honours reporting, including kitty and WezTerm, where 1007 cannot help.
- Works in tmux ≥3.6 `mouse on`: the pane has a mouse mode, so `send -M` delivers it.
- The parser needs no new accumulation logic (§0). The 1000 click press/release events are already swallowed.
- Do **not** add ?1002/?1003: they bring motion traffic, and nothing needs drag or hover yet.
- Cost: native click-drag selection inside Blackbird needs the Shift/Option bypass. Blackbird has no owned selection, so a toggle is the precedent (setting plus runtime key, like gemini's Ctrl+S or crush's palette). `docs/CHAT_PRESENTATION_PROPOSAL.md:123` already anticipates "preserve native selection via modifier/pass-through".
- Must be disabled while `foreground_pty` forwards input (§0). Otherwise the child receives SGR reports, and Ctrl-B release still works because it is a raw byte.
- Must be disabled in `suspend()` before the editor runs, which happens if it is added to `leave_screen`.
- Inspector and palette modes need their own wheel mapping, or the event is ignored there.
- Optionally also send `?1007l` while capturing, as codex does. It is redundant for Ghostty (reporting already bypasses alt-scroll) but harmless.

**W2. DECRST ?1007 only (no mouse).**
- Stops history cycling in Ghostty, Alacritty and xterm. It does nothing for kitty or WezTerm (no 1007), or for an iTerm2 user with the Advanced setting on.
- The wheel then does **nothing** in Blackbird, which is not the scroll the bead asks for.
- 1007 is terminal-global, not per-screen. Exit must restore it with `?1007h` (correct for the Ghostty/Alacritty default, wrong for xterm/iTerm2, whose default is off), or query `CSI ?1007$p` (DECRQM) first and restore the reported state. DECRQM support per terminal is UNVERIFIED.
- Cheapest. Partial.

**W3. Treat arrows as ambiguous and rebind.** At the draft boundary, plain Up/Down scroll the transcript instead of recalling history; history stays on Ctrl-P/Ctrl-N (`:1131-1134`).
- Zero modes and native selection intact. Works under every alt-scroll terminal including kitty and WezTerm.
- Conflicts with the bead's stated intent ("History cycling should remain an arrow-key behavior only") and with shell convention.
- In multi-line drafts the wheel would also move the caret, not the transcript, until it reaches a boundary.

**W4. Burst heuristic.** Rejected on evidence (§1a): Ghostty trackpad sends single arrows (precision multiplier 1).

**W1 + W2-style hygiene** is codex's shipped design.

### Bug arconaut-v1i (Shift+Enter)

**S1. Always recognize `CSI 13;2u` and `CSI 27;2;13~` as insert-newline.** Optionally also `CSI 13;5u`, `CSI 27;5;13~` (Ctrl+Enter), `CSI 13;3u`, `CSI 27;3;13~` (Alt+Enter).
- Harmless: these sequences are dropped today (`:1014`).
- **Fixes Ghostty with no mode change**, because Ghostty sends `CSI 27;2;13~` by default (§1b).
- Also covers tmux with extended keys, WezTerm/iTerm2/xterm when something else enabled modifyOtherKeys, and user-configured terminal bindings.
- Insert the match next to `:918`, inside the `!palette_open_` guard, mirroring `\x1b\r`.
- Should also be decoded when `pasted_`? No: bracketed paste inserts raw bytes (`:910-916`). Claude CHANGELOG 4711 shows kitty-encoded keys can appear inside a paste in some terminals; that is UNVERIFIED for Blackbird's targets.

**S2. Add modifyOtherKeys mode 1: `CSI >4;1m` on entry, `CSI >4m` (or `>4;0m`) on exit.**
- Gains: WezTerm, iTerm2 and xterm (mode-1 Shift+Return to `CSI 27;2;13~`, from source reading), and tmux with `extended-keys on`.
- No effect on Ghostty, kitty or Alacritty.
- Side effects: Alt-modified ordinary keys may become `CSI 27;3;cp~` (Alt-b/f/d, Alt-Backspace, possibly Alt+Enter in xterm). WezTerm level-1 Ctrl handling is UNVERIFIED. So S2 needs a small `CSI 27;mod;cp~` to legacy translator: Alt gives `ESC`+cp, Ctrl gives cp&0x1f, Shift+13 gives newline. That translator is S1 generalized.
- Global flag, so it must be reset on suspend, exit, and PTY handoff.
- Mode 2 is not recommended: it re-encodes every Ctrl binding.

**S3. Add kitty flag 1: `CSI >1u` after `?1049h`, `CSI <u` before `?1049l`.**
- Gains: kitty, Alacritty (all versions with kitty; Shift+Enter since 0.16), and WezTerm only if the user enabled it. Also Ghostty and iTerm2, which already work via S1/S2.
- Requires a full CSI-u decoder that maps back to legacy semantics for every binding in §0, including keypad Enter `CSI 57414u` → submit and lock-bit masking. Without it, Ctrl-J/P/N/C/Q, Alt-b/f/d, Esc, Alt-Enter and Ctrl-B PTY release all break (§1c).
- Gains Esc disambiguation for free (`CSI 27u`), which could retire the 50 ms deadline for kitty-capable terminals.
- Per-screen stack makes cleanup safe across the alt screen.
- Must pop before PTY forwarding (or translate child-bound keys back to legacy) and before the editor handoff.
- Best practice (pi, oh-my-pi, notcurses, neovim, libvaxis, gemini): query `CSI ?u` followed by DA1. Push only if the `?u` reply precedes the DA1 reply, rather than pushing blind. This needs a reply parser in the input path; today an unknown `CSI ? … u` would be dropped, which is harmless.

**S4. Document terminal-side bindings** (Claude `/terminal-setup` pattern). Alacritty <0.16, VS Code-family and Zed can send `\x1b\r` for Shift+Enter, which Blackbird already handles (`:918`). This touches user configuration and is out of scope unless the operator asks.

**Minimal-robust ladder by cost.**
1. S1 alone fixes Ghostty, tmux with extended keys, and user bindings.
2. S1 + S2 adds WezTerm, iTerm2 and xterm. It needs a `27;m;cp~` translator.
3. S1 + S2 + S3 adds kitty and Alacritty. It needs a full CSI-u translator and probe.
4. Terminal.app has no protocol path. Its options are Alt-Enter/Ctrl-J, or a native modifier probe as Claude Code and pi do.

### Restore sequences custody would need

`BB:src/terminal_custody.cpp:52-53` and the paths at `:200`, `:250`, `:263`, `:290` would need the following.

**Entry (`enter_screen`):** `?1049h`, then `CSI >1u` (if S3, after `?1049h` because the stack is per screen), then `CSI >4;1m` (if S2), then `?1000h?1006h` (if W1; plus `?1007l` if W2 or codex-style), then `?2004h`.

**Exit (`leave_screen`), in this order:**
1. `?1006l?1000l` (mouse off before leaving the alt screen; phux `:236-238`, codex `:180-188`).
2. `CSI >4m` (or `>4;0m`).
3. `CSI <u` (pop while still on the alt screen).
4. `?1007h`, or the DECRQM-restored state, if 1007 was reset.
5. The existing `?2026l`, `0m`, `?2004l`, `?1049l`, `?25h`.

**Gaps to cover:**
- `rollback()` (`:182-191`) writes no bytes. If `enter_screen` is partially emitted and then throws, the new modes would leak. That matters more once mouse/kitty are in `enter_screen`.
- No signal-time restore exists. Claude Code and oh-my-pi keep an emergency `writeSync` restore string (`Q:oh-my-pi/packages/tui/src/terminal.ts:444-461` [agent]).
- After a mouse-off write, codex and Claude Code drain stdin so in-flight reports don't reach the shell (`ink/ink.tsx:1489-1491`).
- The foreground-PTY handoff (`BB:src/terminal.cpp:2576-2583`, where `foreground_pty` flips) needs its own mode-off/mode-on pair: mouse, kitty and modifyOtherKeys. This is not a custody exit path today.
- Raw-capture credit is sized from `leave_screen.size()` (`:200`), with fixed control credits at `:199`. `tests/terminal_custody_raw_test.cpp:440` matches `2004l`. Re-validate both when the strings grow.

---

## 4. Unverified

1. Terminal.app: wheel behaviour in the alternate screen, 1007 support, Shift+Enter bytes, SGR 1006 support, and the selection-bypass modifier. No primary source; closed source.
2. iTerm2:
   - Whether alt-scroll arrows follow DECCKM.
   - Default Shift+Enter bytes with no app request.
   - First version with kitty push/pop.
   - Exact Shift+Return under mode 1. This was inferred from `iTermModifyOtherKeysMapper1.m` and not run.
   - What `textViewAlternateMouseScroll:` keys off.
3. xterm: Shift+Return → `CSI 27;2;13~` under mode 1 comes from a source reading (`ModifyOtherKeys()` level-1 `XK_Return`) and was not run. `min_mod` was not traced. Arrows per wheel notch were not checked.
4. WezTerm:
   - Whether level-1 modifyOtherKeys re-encodes Ctrl+letters other than c/d (the exclusion list suggests it may).
   - The absence of a 1007 DEC-mode entry.
   - The release that added modifyOtherKeys.
5. Alacritty: legacy Shift+Enter being exactly `\r`, and 3 arrows per notch (both inferred).
6. tmux:
   - `mouse off` behaviour: whether outer alt-scroll arrows reach the pane.
   - Whether the outer terminal's mode-1 encoding is enough for tmux to see Shift+Enter.
   - The tmux 3.6 release date.
   - The extended-keys default: the wiki text conflicts with `options-table.c` `default_num = 0`.
7. DECRQM (`CSI ?1007$p`) support per terminal, needed for a polite 1007 restore.
8. The Shift/Option mouse-reporting bypass in kitty, WezTerm, Alacritty, iTerm2 and Terminal.app. Only Ghostty is source-verified (`mouse-shift-capture`).
9. OpenTUI (opencode) and bubbletea v2/ultraviolet (crush) emitted sequences: the libraries were not acquired.
10. All `[agent]`-tagged file:line citations (gemini-cli, qwen-code, kimi-cli, crush, pi, oh-my-pi, notcurses, libvterm, ftxui, libvaxis, neovim, libtermkey) come from a read-only sweep and were not re-opened by the author. Spot-check before relying on exact lines.
11. Web sources were read at branch head (no pinned commits) without line numbers.
12. Claude Code: the `claude-code-unofficial` tree is a third-party reconstruction, so its behaviour may differ from current official builds. The 2.1.286 evidence is a string scan of the binary only.
13. No live capture was taken. Every byte claim is from source or docs. A capture (e.g. `cat -v` or a raw-input log in each terminal) would close items 1-6.
