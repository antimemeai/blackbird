# Next native TUI host slice: owned pending frames

Outcome: [implementation and measurements](RESULTS.md) are retained; the source
candidate was retained [inactive](SOURCE_STATUS.md) after the final failed gate.
The operator subsequently reopened the failures; current source is restored,
with [test repairs and a linker block](../tui-settlement-repair-2026-10-15/RESULTS.md).
The proposal below records its original scope, not a delivered host claim.

2026-10-15. Source grounding at `c2d4e2af49387433106007751a8d271f566c3cd3`.
This is a read-only implementation proposal for the next part of
[Unit 1](../../docs/TUI_IMPLEMENTATION_PLAN.md#unit-1-native-component-host-and-preserved-current-controls),
under the [component ownership contract](../../docs/TUI_COMPONENTS.md#ownership).
No source change, build, test rerun, dependency or review campaign accompanies it.
The queued Ctrl-C exec-settlement and raw-recorder recovery failures are outside
this slice; this note neither investigates nor repairs them. AFL work is outside
this unit.

Implementation exception: the full gate repeats the existing cancellation
failure. A [narrow source diagnosis](CANCEL_BOUNDARY.md) identifies a launched
job whose wait can propagate interruption before issuing its required stop.
That concrete missed-stop obligation receives the operator's egregious-fault
exception: deterministic tests and one specific-job cleanup repair, within the
same bounded fix/recheck. The original failure's exact native path remains
unconfirmed. Other queued issues retain their owning work.

## Proposed deliverable

Integrate one native **frame presenter** into the actual `TerminalUI` render loop.
It contains the painter, writer and one owned pending frame: exact final bytes,
the matching candidate grid/cursor/geometry or an explicit tiny-frame completion
kind, and the existing capture handoff. Complete output publishes that exact
frame through one non-allocating completion path. Partial output keeps the whole
remaining suffix and its containing owner alive. The current loop continues to
handle native editing, scroll, stop, foreground input and resize.

This advances reusable host integration in a real application. It is deliberately
smaller than a layout engine, scene-to-widget renderer, async composition worker
or new binding system. Existing `SceneHost` keyed-state primitives remain as they
are. The presenter accepts presentation values, not task/job/workflow operations,
Lua closures or arbitrary domain callbacks. Graphics composition remains a
Blackbird adapter feeding final bytes through the same native frame lifecycle.

## Current source and the precise gap

| Source seam | Current behavior | Next-slice change |
| --- | --- | --- |
| [`ChatOutput` header](../../include/blackbird/chat_view.hpp#L101), [`start`](../../src/chat_view.cpp#L673) | Stores a borrowed `string_view`; caller promises storage until `pending()` becomes false. `RawUiPacket` is already owned. | Keep that low-level borrow inside a containing, nonmovable presenter. Its public admission transfers ownership of the final packet and matching frame state; callers cannot replace active storage. |
| [`ChatOutput::flush`](../../src/chat_view.cpp#L691) | Writes at most eight calls of at most 8192 bytes, retains offset on short write/EAGAIN, retries EINTR, and reports terminal completion only after the suffix is written. | Preserve these transport semantics and budgets. The host consumes the completion once and commits only its own matching staged state. |
| [`ChatPainter::prepare`](../../src/chat_view.cpp#L529) | Diffs against `previous_`, stores bytes in its own reusable string, and advances cursor fields during preparation. | Keep committed cursor/grid separate from prepared cursor/grid. A failed or unstarted preparation must not move the committed cursor observation. Move prepared packet storage into the containing pending frame before borrowing its bytes. |
| [`ChatPainter::commit`](../../src/chat_view.cpp#L583) | Copies the candidate grid after successful I/O. A growing grid can allocate at this point. | Commit through moves/swaps and scalar assignments after complete output; reserve all necessary storage before admitting bytes. Recycle the former committed grid and packet capacity for the next build. |
| [`TerminalUI` local render state](../../src/terminal.cpp#L1576), [`main-loop completion`](../../src/terminal.cpp#L1969) | Separate `grid`, painter, packet strings and `pending_grid` describe one logical frame. Main-loop completion commits the current local grid. | Replace the convention with one frame owner and one completion result. Completion cannot accidentally refer to a later rebuilt grid. |
| [`redraw gate`](../../src/terminal.cpp#L2259) | Layout runs only when redraw is requested and no packet is pending. | Retain this one-slot admission policy. Changes while blocked set redraw; no second layout or byte packet is built until the active frame completes. |
| [`tiny frame`](../../src/terminal.cpp#L2267), [`normal/graphics frame`](../../src/terminal.cpp#L2541) | Tiny bytes live in `small_frame`; normal bytes in painter storage; graphics bytes in `image_frame`. Immediate and delayed completion use separate branches. | Route all three through the same owned frame lifecycle, preserving exact emitted protocol bytes and explicit tiny-frame painter invalidation. |
| [`editor handoff`](../../src/terminal.cpp#L1915) | Drains pending output within the existing handoff policy, detaches/resumes terminal custody, invalidates the painter and flushes editor typeahead. | Use the same presenter service/completion path during drain, preserving the current deferral policy and terminal-mode/typeahead behavior. |
| [`RawUiFrame`](../../include/blackbird/raw_ui.hpp#L150), [`capture/handoff`](../../src/raw_ui.cpp#L373) | Captures actual packet originals before writer handoff; owns terminal lifecycle credit and issuer identity. Graphics augmentation captures the final packet and discards the intermediate text frame explicitly. | Preserve actual originals, issuer, credits and causal handoff. No presentation admission may consume a failed capture or report a partial packet as completed. |

The current borrowed storage is protected by the redraw gate: there is no demonstrated
use-after-free in this note. The problem is that reusable admission/async ownership
would otherwise have to reproduce several private local-variable and branch
conventions. The extraction should encode those relationships before another
composition owner can publish into them.

## One coherent implementation sequence

1. Add the native containing frame presenter near the current grid/painter/writer
   code. Keep a single execution owner and one in-flight frame. Define idle,
   prepared and writing states; make invalid transitions explicit. A pending frame
   cannot move after the writer acquires its byte view, including short strings
   whose contents reside inside the string object. The presenter itself must not
   be moved while attached. Its teardown detaches the writer before destroying
   pending storage and retains existing native owner-thread cleanup checks.
2. Make grid/cursor publication transactional with transport. All packet/grid
   construction and capture admission happen before `start`; committed grid and
   cursor change only on complete output. Use a consuming grid commit/swap,
   returning the former grid and packet capacity as scratch. Do not introduce a
   per-frame `shared_ptr` allocation or another full viewport copy just to pin
   storage. A zero-byte diff requires no writer admission and must not fabricate
   a new emitted geometry or first-frame flush event.
3. Migrate the actual normal, tiny and graphics branches to the containing frame.
   Preserve graphics escape order and capture/discard originals. Stage Blackbird's
   image-placement/load observations with the frame; advance them on completion,
   or explicitly invalidate them on detach/transport failure. This is presentation
   bookkeeping, not a generic callback facility or domain-operation authority.
   The graphics frame owns the exact final byte string containing the text delta,
   erase/load/place commands and synchronized-update close; it cannot retain only
   a text prefix plus a borrow into mutable image scratch. A tiny frame likewise
   owns its exact clear/text/cursor/optional-image-erase packet, terminal rows and
   columns, and the computed single-line cursor position. Tiny output invalidates
   normal-grid comparison when completed. An unknown or
   partially emitted frame is never installed as the next diff baseline.
4. Replace immediate-write, main-loop and editor-drain completion branches with
   one host service result. Keep the existing render gate and redraw flag. A resize
   during a partial frame does not replace an escape-sequence suffix: finish that
   frame, then build the latest geometry. No unbounded queue of intermediate grids
   or resize packets is needed. An admitted partial packet is never superseded.
5. Leave the existing input/domain/control routes in place. The presenter owns no
   workflow execution, command ownership, task selection authority, persistence,
   provider settings or cancellation settlement. Its integration must not insert
   a wait for stdout before those existing routes can run.

Do not add unused scene/frame IDs, public plugin ABI, mouse action dispatch,
composition queues or optimistic frame supersession in this unit. Later scene
publication can retain a real emitted geometry revision through this owner;
this slice supplies the owned lifetime and completed-byte boundary first. Physical
terminal display time remains unobservable here.

## Input and lifetime edges to preserve

The current poll set includes stdin, native notifications, conditional stdout
POLLOUT and resize wakeup at [terminal.cpp:2600](../../src/terminal.cpp#L2600).
Expired composer prefixes are serviced before ready/idle input selection at
[line2624](../../src/terminal.cpp#L2624). Keyboard originals and reserved capture
credit are acquired before the existing 128-byte native read at
[line2629](../../src/terminal.cpp#L2629). Preserve that capture ownership rather
than replaying decoded keys into another unlinked input path.

Keep foreground PTY paste/control routing and Ctrl-B at
[line2688](../../src/terminal.cpp#L2688), Composer feeding at
[line2728](../../src/terminal.cpp#L2728), scroll at
[line2812](../../src/terminal.cpp#L2812), and cancel/quit at
[line2816](../../src/terminal.cpp#L2816). Keyboard controls may change logical
state before a blocked frame is emitted. Pending-frame ownership freezes display
state, not the editor or cancellation flag. The next permitted layout consumes
the latest local state. There is no promise of visible feedback while the actual
terminal output path is blocked.

Current quit can leave a pending frame before teardown
([line2167](../../src/terminal.cpp#L2167)); preserve explicit abandonment and terminal
restoration rather than adding an indefinite close-time drain. Existing editor
handoff is a distinct finite drain/deferral path. Neither path may mark abandoned
bytes or grid state as completely emitted. This note does not claim every current
local operation is independently serviceable: persistence, raw capacity and domain
callbacks retain their own existing behavior and failure boundaries.

## Necessary direct oracles for implementation

- **Owned frame and exact suffix:** admit normal/tiny/graphics packets from
  short-lived and reused caller storage, force actual pipe backpressure, then
  drain and compare exact bytes and ordering. Include a short inline string.
  A narrow native write seam may deterministically exercise every cut across
  escape/UTF-8 boundaries, EINTR and EAGAIN; reuse it in this unit rather than
  inventing another transport framework. The existing
  [pipe case](../../tests/chat_view_test.cpp#L187) keeps its real descriptor/flag
  restoration check; it currently holds the caller payload alive and does not
  establish the new containing lifetime.
- **Completion is the only commit:** while frame B is partial, the committed
  grid/cursor remains A. On full B completion it becomes exactly B once. Failed
  construction/capture, empty diffs and abandoned output have explicit outcomes.
  Deny allocation at the grid-publication point to establish that post-write
  commit requires none; do not claim the whole transport/capture path allocates
  nothing. Injected preparation failure must retain A's cursor as well as cells.
- **Independent terminal meaning:** feed actual emitted bytes through the existing
  [VT oracle](../../scripts/terminal-screen-oracle.py), requiring specification
  cells/styles/cursor for A→B→resized C, wide-glyph overwrite and tiny→normal.
  Do not rely only on comparing two packets from the same painter. Check the
  graphics protocol's exact packet ordering separately; a text oracle does not
  establish physical image presentation.
- **Native controls under blocked output:** a finite actual `TerminalUI` fixture
  uses a local bounded worker that acknowledges start and observes the existing
  cancellation flag through a separate test channel. After deliberate stdout
  backpressure, send typing/paste, page navigation, Ctrl-C and resize. Require
  cancellation acknowledgment before releasing stdout; afterward require exact
  draft/cursor and the expected scrolled content in the completed frame. No shell,
  provider or queued failing exec-settlement path is needed to isolate this host
  question. Wait for actual acquisition/acknowledgment, never a sleep as a proxy.
- **Preserved application paths:** run the affected existing native, PTY, raw-output,
  readable screen and Microsoft scenarios through the migrated production seam;
  baseline presentation remains unchanged unless a concrete intended difference
  is written. Existing queued failures remain failures, with their owning issue
  references; passing a narrow presenter fixture does not settle them. Apply the
  required source gate and its current limits when implementing, not while writing
  this source plan.

These questions attack different boundaries; no oracle certifies another oracle.
One implementation challenge and the established bounded fix/recheck apply when
source work begins, not a new review campaign for this read-only note.

## Costs and tranche measurements

The intended steady storage remains two viewport grids (committed and staged),
one final active packet and reused scratch capacity, plus existing capture and
application data. Cell storage is at most `2 × width × height × 16` bytes from the
current `ChatCell` bound; at 109×35 that is at most 122,080 bytes, excluding complex
cluster tails/vector capacity. This is an accounting bound, not an RSS limit.
Commit swaps avoid the current viewport copy and any resize allocation after
bytes complete. Capture originals and the existing two graphics capture scopes
still have their actual byte costs; never elide emitted originals for a prettier
measurement. Do not add another retained-history representation.

Keep the existing full-grid diff work in this slice. Measure actual moved/copied
storage, prepared/emitted bytes and implicated allocations before claiming a win;
there is no promised speedup from moving code. Run the
[matched tranche observer](../tui-perf-fuzz-2026-10-15/performance/README.md) against
retained pre-slice and resulting executables on the same host/profile/spec, keeping
all samples and tails. The new blocked-output episode establishes its own semantic
coverage, and any new timing scenario gets its own baseline. No producer-side
duration calculation, histogram, sampling or aggregate is introduced.
