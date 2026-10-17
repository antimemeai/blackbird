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
case then passes Debug and ASan after two concrete observer fixes: use an ESC
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

Source `7523da7ab8134e377cff1186e12fc71919d68ea5` passes the full installed hook:
258/258 Debug tests in365.33s, all253 actual C++ translation units and owned Lua
checks. No new lint suppression or source-gate bypass. Independent Linux Release
qualification passes seven selected native/PTY cases and the18-phase actual-main
oracle on both backends. The source checkpoint is pushed on
`work/tui-full-viewport-2026-10-16`; master integration and running-user executable
activation remain separate.

The [matched application report](performance/README.md) retains120/120 normal
processes and960 endpoints,30 per variant/geometry, without failed, discarded or
repeated trials. Typing medians are slightly lower. Wide resize median rises
3.044→5.745ms and restore3.794→5.557ms; narrow restore rises1.792→3.575ms. Wide
paste p95 improves while narrow paste p95 worsens. All16 p50/p95/max rows remain
visible. The shared-host observation does not isolate source causation or establish
a blanket speedup/nonregression. Keep recovery costs and tails in arconaut-f8f.

The compiled Linux archive predates only two legacy test-coordinate corrections:
12-column composer border x10→x11 in check-tui-microsoft;120-column task origin
x83→x84 in check-tasks-pty. Native code, native build identity inputs and benchmark
helpers are unchanged. Exact source mapping accompanies the report; no native
rebuild or timing repeat is warranted for these test-only changes.

The first source hook is interrupted before qualification/commit when root notices
the old minimum-width border assertion. A direct RED and corrected complete
Microsoft episode are preserved. The next full gate ends257/258 in360.20s on the
old task-origin assertion; its corrected complete task episode passes, followed
by the full258/258 clean gate above. Do not relabel either earlier attempt as green.
The deliberate layout expectation updates retain physical borders and task origin
checks, without widening deadlines or changing the intended normal padding.

arconaut-arw is repaired by this source-qualified application integration.
arconaut-igs remains open for the ordinary failed-assert Mac teardown wait, and
arconaut-f8f retains all mixed performance observations. AFL, user artwork, live
sessions and unrelated services remain untouched.
