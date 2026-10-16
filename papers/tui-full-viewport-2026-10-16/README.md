# Full physical viewport application integration

The actual TerminalUI now builds its grid and normal content against physical
columns, including the last column. Normal frames select addressed deltas with
EL only for ordinary blank tails. Startup, dimension changes, resize notifications,
return from tiny and editor reattach use full physical clear/repaint. Tiny output
uses physical width and resets SGR before clearing. Existing view-specific inner
padding and the normal bottom padding remain explicit.

The [plan](PLAN.md) follows the measured [policy competition](../tui-terminal-edge-comparison-2026-10-16/performance/README.md).
No framework, wrap-suppression prototype, new input owner or producer metric
transform is adopted. In-flight packet bytes remain unchanged until complete;
committed geometry and image state publish only after successful flush. Exact
final graphics packet capture is preserved.

## Direct behavior

The [actual-main oracle](ORACLE_RESULTS.md) first fails the old executable on
both Ghostty and Alacritty's physical final column. After integration and owning
fixes, each profile passes18 phases: full-width border/final-row cells and styles,
midcursor draft, Unicode scalar editing and wide-footprint removal, growth/shrink,
8x3/2x2/1x1 tiny output, normal recovery, route changes and clean exit/modes. No
turn/provider request occurs. All eight attempts remain, including the corrected
caption literal and rejected whole-grapheme editor assumption.

The [single source review](SOURCE_REVIEW.md) finds two concrete recovery defects
in the initial integration; both are fixed in this unit:

- F1: ED2 can clear Kitty placements and cached data. Full and tiny clears now
  invalidate pending loaded-image state; visible images are reloaded and placed
  inside the final synchronized recovery packet even if coordinates stay equal.
- F2: a shrink-and-return can leave damage while final dimensions equal the old
  committed plane. A resize wake retains viewport invalidation independently of
  geometry equality; an old pending packet's completion cannot erase that fact.

The extended graphics PTY case and actual blocked-output case reproduce the two
initial defects. The blocked case confirms native EAGAIN, stops only its own
child, shrinks/restores while pending, resumes and requires a physical clear after
drain while retaining input/cancellation/draft/scroll semantics. This packet
contract does not pretend its text parser models terminal resize reflow; the
Microsoft oracle independently observes actual physical cells.

Initial affected Debug10/10 pass18.73s and ASan+UBSan6/6 pass28.55s. After owning
fixes, blocked/ui_raw/custody_raw pass on Debug and ASan. The extended graphics
case then passes both profiles after two concrete observer fixes: use an ESC
prefix for raw graphics matching, and await the latest synchronized frame/cursor
rather than an older matching packet followed by a partial new frame. Every full
clear must reload the image; multiple independent resize observations may produce
multiple recovery packets. No deadline is widened. Source/native bytes do not
change for those observer corrections.

The graphics failure also exposes a preexisting unbounded launcher teardown
wait. Capture the primary exception and terminal bytes, target only the owned
process group and bound that wait to5s. Its underlying Mac waitpid stall remains
an ordinary queued finding, arconaut-igs, without a claim of a product TUI hang.
All failure logs and raw terminal/event bytes stay in ignored context.

Root's one physical-oracle source review closes after the source-grounded
expectation corrections. The two native review findings are resolved by direct
RED/fix/GREEN within the existing bound; no additional panel. No shared state or
concurrency primitive is introduced by the UI-owner invalidation flag; repeat
TSan would not attack a new race class. ASan/UBSan and actual blocked-output/input
checks address the changed bounds and recovery paths.

## Delivery and measurement

The full installed source hook and matched previous/current Linux Release app
round are pending at this source checkpoint. [Measurement plan](MEASUREMENT_PLAN.md)
uses the unchanged historical observer at both geometries with all30 trials and
all failures/tails retained. This supplies the application delta that the earlier
one-tree renderer competition intentionally did not claim. Source and results
will be committed and pushed on `work/tui-full-viewport-2026-10-16`; no master
merge, launcher replacement or live-user executable activation is implied.

arconaut-arw remains in progress until full source qualification. The ordinary
teardown finding stays queued. AFL, user artwork, live sessions and unrelated
services remain untouched.

The first source hook is deliberately interrupted before qualification/commit
when root finds an older minimum-width Microsoft assertion still expecting the
12-column border at x10. A direct run reproduces that assertion failure on the
repaired app. The expected physical border moves to x11; the unchanged rest of
that Microsoft episode then passes. This is a deliberate application-boundary
expectation change, not a new native fix. The measured Linux archive predates
only this test/comment correction; native algorithms and build identity inputs
match the delivered candidate. The fresh full source hook remains required.

The next full gate ends257/258 in360.20s: only the older120-column tasks fixture
expects origin x83. Full width gives82 main +2 gutter +36 task cells; update its
read range and literal expected origin to x84. The complete task episode then
passes, including bound activity, burst updates and narrow fallback. No native
source changes. The measured archive's exact final difference is now these two
legacy test-coordinate helpers, not a renderer or benchmark change. A subsequent
full clean gate remains required before source delivery.
