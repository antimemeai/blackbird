# Composer terminal input: wheel, Shift+Enter and arrow history

Unit 1 of the 2026-10-19 little-bugs work, on `work/text-bugs-2026-10-19` (based on
startup-generation `26f3030`). Beads `arconaut-uss`, `arconaut-v1i` and
`arconaut-2bj`. Research is
[TERMINAL_INPUT.md](../composer-input-2026-10-17/TERMINAL_INPUT.md) (published on
`work/glm-bugbash-2026-10-17`). Citations below use the current file lines.

## What is wrong

- **Wheel recalls history (`uss`).** Ghostty and Alacritty enable alternate scroll
  by default. In the alternate screen with no mouse reporting, they send the wheel
  as Up/Down arrows. At the first or last draft line, `Composer::vertical`
  (`src/terminal.cpp:789`) calls `history()`. Wheel arrows are byte-identical to
  typed arrows, so the composer cannot tell them apart. Ghostty trackpad scrolling
  sends single arrows, which rules out a burst heuristic.
- **Shift+Enter submits or does nothing (`v1i`).** Ghostty sends `CSI 27;2;13~` by
  default. kitty, Alacritty and WezTerm send `CSI 13;2u` once the kitty protocol is
  enabled. The parser accumulates both and drops them at `src/terminal.cpp:1014`.
- **Arrow history (`2bj`).** `history()` (`:769`) already keeps the draft and its
  cursor when cycling starts, goes from newest to oldest, and restores the draft
  past the newest entry. The user-visible defect is the wheel, not the arrows.

## Operator decision (2026-10-19)

Use Claude Code's fullscreen design without drag or motion reporting. Mouse
reporting (SGR `?1000h ?1006h`) is on in Blackbird's screen, and the wheel scrolls
the transcript. Shift+drag selects text in Ghostty (source-verified). A launch
setting and a runtime command turn reporting off. Reporting is off while a
foreground PTY job owns input and while the external editor runs.

## What changes

1. **Decode.** `Composer::feed_impl`:
   - `CSI 13;2u`, `CSI 27;2;13~` (Shift+Enter) insert a newline. So do the Alt
     forms (`13;3u`, `27;3;13~`) and the Ctrl forms (`13;5u`, `27;5;13~`). The
     palette guard is the same one that applies to `ESC CR`.
   - An SGR mouse report `CSI < b ; x ; y M|m` is consumed whole. Wheel up is
     `(b & 0x43) == 0x40` and wheel down is `0x41`. The palette or slash menu
     moves its selection, a focused inspector moves to the previous or next item,
     and otherwise the result is a new `InputAction::scroll_up/scroll_down`.
     Clicks and releases change nothing, including inspector focus.
   - Enum values are appended, so existing raw action values stay stable.
2. **Scroll.** In the UI loop, `scroll_up/down` move the transcript 3 lines,
   following codex and qwen (`PgUp/PgDn` stay at half a screen).
3. **Mode bytes ride the frame packet.** The `ChatPresenter` owns nonblocking
   partial writes, so mode changes are not written beside it. When the desired
   reporting state differs from the emitted one, the next frame recovers fully
   and inserts `\x1b[?1000h\x1b[?1006h` or `\x1b[?1006l\x1b[?1000l` before the
   synchronized-update close. This follows the existing graphics insertion
   (`:3020-3043`) and its raw frame capture, and the tiny-frame path does the
   same. Emitted state commits when the packet completes, as image state does.
   - Desired state is the setting and not `foreground_pty`.
   - `leave_screen` (`src/terminal_custody.cpp:52`) gains `\x1b[?1006l\x1b[?1000l`
     before `?1049l`, unconditionally. Exit and editor suspend therefore always
     leave reporting off. After `mode.resume()` the emitted state is off, so the
     next frame re-enables it.
4. **Controls.**
   - `BLACKBIRD_MOUSE=0` at launch starts with reporting off, following
     `BLACKBIRD_MASCOT` and Claude Code's `CLAUDE_CODE_DISABLE_MOUSE`.
   - `/mouse`, `/mouse on`, `/mouse off` change it at runtime and report the result.
   - `/keys` documents Shift+Enter, wheel scrolling, Shift+drag selection and
     `/mouse off`.

Not in scope: kitty keyboard push or modifyOtherKeys (study §3 S2/S3), so
Shift+Enter in kitty, Alacritty and WezTerm stays CR until that work. Drag or
motion reporting, owned selection, terminal-specific selection hints, a persisted
setting, and draining wheel bytes already in flight when a PTY job takes input.
The edge-case behaviour is recorded rather than hidden.

## Direct checks

RED first, against the current source:

- `tests/terminal_test.cpp`:
  - each Shift, Alt and Ctrl Enter form inserts a newline, and plain CR still
    submits;
  - with history present, SGR wheel up and down leave the draft unchanged and
    return `scroll_up/down`, and the Shift-modified wheel (`b=68`) still scrolls;
  - a click press and release leave the draft and action alone;
  - in a focused inspector the wheel gives `inspect_previous/next` and a click
    gives no action;
  - an open palette moves its selection.
- `scripts/check-terminal-pty --case composer`, on the actual executable:
  - the first frame enables `?1000h?1006h`;
  - after a long `/keys` transcript, wheel up shows an earlier line and the draft
    stays as typed;
  - bare Up at the first line recalls history, and Down past the newest restores
    the draft and cursor (`2bj`);
  - Shift+Enter (`CSI 27;2;13~`) inserts a newline without sending;
  - `/mouse off` emits `?1006l?1000l`;
  - ordinary exit writes `?1000l` before `?1049l`.
- One existing PTY episode with a foreground PTY job also asserts that reporting
  goes off while the job owns input and comes back after Ctrl-B.

Then the installed full source hook, one independent review, fixes, and a
recheck of the changed cases only.

## Review and recheck (two layers, closed)

The independent review found no high-severity defect. It found three medium
issues, two low ones, one cosmetic issue and some test gaps. All were fixed:

- X10 reports (`CSI M Cb Cx Cy`, from terminals that ignore `?1006`) are
  decoded. Mouse decoding now runs before the parameter-byte rule, because X10
  coordinates are raw bytes.
- A report split across reads and expired at the escape deadline no longer types
  its tail. The existing contract stays: a due deadline flushes even when input
  is readable (`tests/terminal_test.cpp` pins it). Instead, the composer swallows
  that report's remainder.
- `/mouse off` does not send `?1007l`. Without a DECRQM query there is no way to
  restore the user's original setting, and leaving alternate scroll off would
  break wheel scrolling in later `less`/`vim` sessions. The help and docs state
  that with reporting off, Ghostty and Alacritty send the wheel as arrows, which
  recall history.
- `/keys` and the README state the Shift-Enter terminal caveat.
- SGR reports from the outer terminal are never forwarded to a PTY child. Child
  output is escaped text, so a child can never have enabled outer reporting.
- Wheel precedence matches the arrows.
- New tests: X10, split tails, the cursor after Down, precedence,
  `BLACKBIRD_MOUSE=0`, a tiny first frame, editor suspend and restore, an
  in-flight report before PTY input, and a drain after exit.

The bounded recheck found three low edge cases, all fixed:
- an input-owner handoff clears the composer's partial escape without arming a
  tail, or skips the rest of a report the child was holding;
- an expired bare `CSI M` (also SCO F1) arms nothing;
- an ESC aborts a pending X10 parse.

No third layer. Remaining known limits: an in-flight X10 report during PTY
input is not dropped, and abnormal termination leaves reporting on, as it
already leaves the alternate screen.
