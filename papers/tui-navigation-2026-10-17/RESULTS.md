# Command-run paging and shared view navigation

2026-10-17. Source 064e794ea1665f241c82e92b431c689290deb205 is qualified and pushed on
work/tui-navigation-2026-10-17. Operator requested repair of the authored result comparison's missing
list continuation, followed by the shared shell/navigation/named-controls slice.
This is a local display/navigation delivery. Command execution capacity and the
existing external job_id protocol are unchanged.

The comparison now pages all current owner-resident command-run entries through
bounded eight-row reads. Ctrl-Left/Right pages the list in collection focus and
selected output in detail focus. The adapter distinguishes requested, observed,
input and published page cursors, refuses stale-page pins/interrupts, and retains
immutable A/B windows. A pending/failed Lua page retains the last good view;
back/reset and native focus remain useful. This is not an archive browser.

The four shared typed view actions are view.conversation, view.command-runs,
view.command-center and view.result-comparison. Ctrl-\ or /views opens the native
picker. Selection resumes existing per-view state without inserting a slash
command or duplicating the draft; existing keys remain toggle aliases and existing
slash entry points retain their explicit page-reset behavior. Picker arrows and
F8 cannot act behind the modal; Escape restores invoking focus. Unavailable
routes display a reason while preserving the current view.

## Direct behavior and independent review

The original native17-run test failed at run-list entries9–16. Original actual
main and native Composer failed to open the view picker. After implementation,
all7 native instrument modes pass, including8/8/1 identity/source checks, mapped
presentation keys, rapid paging/reset/exhaustion, a live child's pending-page
interrupt refusal, failed composition and exact immutable output pins.

The native terminal checks and21 readable styled expectations pass. The new
view-picker baseline preserves a nonterminal draft cursor. The two existing
instrument expectations intentionally adopt Command run/COMMAND RUNS wording,
including the resulting title wrapping; other18 prior baselines are preserved.

Actual-main Microsoft Ghostty and Alacritty both pass the complete retained-result
episode: picker routes/draft/cursor/comparison anchor, modal arrow/F8 isolation,
Escape focus restoration, nine additional native command runs, second-list-page
selection and exact output pinning, backward navigation, source failure/reload/
preview, exact interrupt receipt, and private PTY control ownership. Eleven actual
runs are independently identified through owner-returned IDs in that episode.

Root independently reviewed the paging source once. Independent shell review
found three defects, now fixed and directly rechecked: palette dismissal stole
focus, unavailable-route feedback hid behind the old view, and site4010's schema
omitted select_view action bytes. The ui_raw case reads the actual recorded view
strings/feed links/draft positions and checks an unavailable center through the
real TerminalUI PTY. No review of reviews or broader hardening layer follows.

## Qualification and original failed attempts

Affected ASan/UBSan terminal/instrument/styled/editor/graphics cases pass. The
first12-case run also selected an older linked blocked fixture; its target was
rebuilt and that specific episode passes separately (11.25s).
The newly changed ui_raw target passes ASan independently (7.75s).
The installed full source gate passes259/259 Debug cases in458.96s, all253 actual
C++ translation units and owned Lua analysis. No hook bypass or new suppression. All16 selected Linux Release checks pass.
One matched Release round completes120/120 processes and960 endpoints without
failures, discarded samples or repeats. [Performance results](performance/README.md)
retain every endpoint and source/profile identity. Typing/resize medians change
in both directions; paste tails improve in this round and wide startup maximum
rises40.963→388.296ms. No blanket speedup/nonregression or source-causal claim.
Keep the tail in arconaut-f8f; no repeat or deeper resize investigation is started.

All attempted fixture failures remain in ignored context/tui-navigation-2026-10-17.
These include a native cursor setup using an unbound Ctrl-B, an old physical100
column clip after the title grew, waiting for conversation Turn complete while
comparison hid it, and expecting a literal private Ctrl-\ while the fixture
child's VQUIT still interpreted it as SIGQUIT. Corrected the specific fixture
boundaries: actual Left, complete physical101 detail/body, leave comparison for
the visible settlement check, disable only that child's VQUIT for its literal
input claim. Its ISIG/VINTR behavior and original signal assertions remain.
The old first-three generic command choices retain their original ordering.

## Scope and next work

This delivers shared native view routing and named local navigation, not general
configurable bindings or every proposed shell component. Layout/forms/editors,
effective-binding inspection and shared operation locators remain owning work.
Carry this foundation into Unit3 settings, Context and team as those owners are
implemented. The [first form proposal](../tui-settings-form-2026-10-17/PLAN.md)
identifies the actual missing guarded session-default owner, separate program
overrides and the reusable two-field form; it remains proposed source work. Resize latency decomposition stays in arconaut-8zx. No dependency,
provider/session activation, mainline merge, artwork work or AFL interaction.
