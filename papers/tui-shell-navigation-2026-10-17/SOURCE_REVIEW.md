# Shared view navigation: independent source review

2026-10-17. One concrete implementation review, requested by the root owner,
against the working diff over `cbb206b629d936c13c9636b0a7bac999fa68e3e9`.
This report reviews the shell changes in `terminal.hpp`, `terminal.cpp`, the
new `tui_navigation.hpp`, inspector/center focus methods, terminal/native screen
tests, and the actual-main instrument driver. Command-run paging has its own
root-owned review and is not rereviewed here. No product files were edited and
no additional assurance campaign was launched.

## Findings requiring fixes

### F1 — Picker dismissal sends focus to the hidden draft

`src/terminal.cpp`, the `terminal_input_dispatch` call and following
`if (dispatch.redraw) visit_inspection(...focus_editor...)` branch, near3039–3043
in the reviewed diff. `Composer::flush_escape()` closes a palette for lone
Escape and returns true. The loop consequently moves an active inspector from
collection/detail control to its draft even when Escape only dismissed the view
picker. Opening the picker does not change the adapter's previous focus, so
this unconditional focus change breaks the promised invoking-focus restoration.

Retain the distinction between palette dismissal and ordinary inspector Escape.
Closing the picker should leave its invoking focus intact; ordinary Escape
outside it should keep the existing move-to-draft behavior. The actual-main
driver currently waits for picker removal and calls `selected(job_b)`; unchanged
selection alone cannot distinguish collection focus from hidden editor focus.
Add a direct assertion after dismissal: an arrow must navigate the same view
control, with the draft unchanged. Exercise detail invocation as well if using
that state to claim exact focus restoration.

### F2 — Route refusal has no visible feedback when another inspection view stays open

`src/terminal.cpp`, the `select_view` picker dispatch near3183–3186, and renderer
near2854–2884. Preparation correctly refuses an unavailable destination before
closing the current adapter. However, the refusal appends a generic notice to
the conversation transcript, which is hidden while the retained inspection view
remains active. The `instrument_refused` / `center_refused` / `inspector_refused`
render branches also occur only after `if (inspecting)`, so none is displayed.
For example, a caller without a center context can open command results, choose
Command center, and receive no visible explanation that navigation failed.
Allocation refusal of a lazily created adapter has the same path.

Display a shared native navigation refusal alongside the still-active view or
picker. Identify the requested view and the known reason, rather than silently
remaining in the previous surface. Clear obsolete refusal after successful
navigation. The existing source API permits contexts without these owners;
route availability must not be inferred merely from the four descriptor rows.
Check preservation and visible refusal directly with an unavailable destination.

### F3 — Emitted action text disagrees with the raw event schema

`src/raw_ui.cpp`, site4010 schema near500–501, still describes optional trailing
text as `bytes for submit/choose`. `Composer::feed` now emits that trailing text
for `InputAction::select_view` as well. The bytes are correctly captured in the
new branch, but their published interpretation omits the new action kind.

Update the schema to include view selection and its stable action text. A
scoped assertion in the existing UI raw test should consume a real selected
route event and require the appended action ordinal, exact stable action name,
original cursor/draft extent, and no leftover payload bytes. This directly
checks the changed raw boundary; no new event format or second ledger is needed.

## Concrete properties checked in source

- `select_view` is appended after all existing input actions. Existing action
  ordinals used in retained raw events do not shift.
- Route choices carry native `ViewRoute` values, not commands reconstructed
  from labels. Site4010 captures the stable action name. Current capture
  reservation bounds include the longest configured choice name and the existing
  action envelope; no missing extra typed payload is emitted.
- A typed route choice returns `select_view`, so it does not reach the generic
  `choose_command` branch that stores/replaces drafts, nor the submitted prompt
  branch. Slash entry points remain explicit submitted local commands.
- `inspecting = inspecting && !palette_open_` gives the picker precedence over
  inspection arrows and F6–F8. The new actual-main scenario checks no hidden
  F8 interrupt; native terminal tests also attack this dispatch distinction.
- Bracketed paste is handled before the new Ctrl-\\ action. Private-PTY input
  still runs before Composer; the updated fixture includes literal `0x1c` in
  the child's independently recorded input. `ISIG` remains disabled only under
  the existing terminal-custody owner.
- The coordinator prepares the destination before closing current adapters,
  resumes picker destinations without `first_page`, and focuses an already
  active route rather than toggling it closed. Existing hotkeys remain toggles;
  existing slash entry points retain explicit first-page behavior.
- Current-route authority is centralized in the loop's route value and all new
  entry points use the same coordinator. Existing adapters retain selections,
  source pages and comparison pins; route descriptors add no process authority,
  provider call, Lua execution or blocking source scan to input handling.

Root was already correcting the initial label-search ranking problem when this
review began. It is not an additional independent finding. The three findings
above were sent directly to the root owner with their owning paths. Root owns
the fixes and direct rechecks; this report does not claim they are applied or
request a second review.


## Owning fixes and direct rechecks

Root fixed F1 by returning an explicit focus decision from the existing escape
expiry helper: palette dismissal repaints without changing the invoking view's
focus; ordinary inspector Escape still returns to the editor. Native tests
check the decision, and the Microsoft episode dismisses the picker then uses
Home/Down on the original collection before pinning its actual target.

F2 now retains a shared route refusal string and renders it before current-view
caption/status. Adapter preparation happens before closing any active view.
The actual no-center-context TerminalUI PTY episode requests that unavailable
center and checks refusal plus retained COMMAND RESULTS in completed output.
F3 updates site4010's schema; ui_raw reads all four actual route choice strings
and the picker action from disk, checking feed4080 links and original draft/cursor.
No rereview or broader audit is added.
