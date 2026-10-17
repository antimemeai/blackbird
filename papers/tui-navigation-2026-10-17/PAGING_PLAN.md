# Command-run list paging

2026-10-17. The result instrument currently changes only output offsets through
its page control. Its bounded observer already returns eight rows plus an exact
continuation ID. Wire that continuation through the native adapter; this changes
selection/display reach, not command execution capacity.

Use the established command inspector/center focus rule: Ctrl-left/right changes
the command-run list page when the collection is focused and the selected run's
output window when detail is focused. Keep 33 optional cursor slots, matching the
existing native inspector and bounded 256-record owner. First-page resets list
and output navigation. Authored row keys remain presentation identities; command
operations resolve the row's stable native target.

A requested page, the last observed page and the scene's adopted page are distinct.
Never advance with a previous page's continuation while composition is pending.
Never pin or interrupt an old selected row using a new undisplayed observation.
Native focus/selection and backward/reset recovery remain bounded and available
if Lua composition, command observation or capture cannot currently progress.
A/B pins remain immutable across run-list and output navigation.

Direct specification test: execute 17 actual native commands (16 settled and one live), assert
exact expected command identities on all three pages, forward/backward exhaustion,
rapid repeated forward controls, first-page reset during composition, mapped row
selection, pin bytes and identity, plus output-window paging and immutable pins.
Save the first direct failing run before implementation. Run all existing native
instrument modes after the fix. The root owns one independent source review and
shared terminal, readable-screen, sanitizer, performance and final source gates.
No production dependency, provider call, live activation or fuzz campaign changes.
