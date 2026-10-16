# Application integration source review

2026-10-16. One independent concrete review of the initial application integration
against4b4113d: `src/terminal.cpp` physical width, tiny reset and committed-geometry
policy selection, plus its `CMakeLists.txt` test registration. Read the owning
PLAN, existing Presenter/output lifecycle, image packet recapture and native
Kitty protocol helpers. Existing renderer policies are settled and were not
reviewed again. This review performed no test/build campaign or product edits.

Two actionable findings; fix and directly recheck these within this unit.

## F1: full recovery invalidates Kitty placement and image cache

The normal frame now selects `ChatPaintPolicy::full` on a geometry change,
which emits ED2. The following graphics branch runs only when computed placement
differs from committed `image_placement`; it retains `image_loaded` as well.
A row-only resize while the ordinary corner sprite is shown can leave placement
unchanged:101x30 to101x31 still computes the same12x6 placement at row0,column89.
The clear removes the image, but the branch does not restore it. Even when
placement changes, the old loaded flag can suppress a necessary retransmission.

This is explicit in the already acquired pinned Kitty reference
`0d006c44b3d94a6ad7cb9ca11e1549af45095fe4`:

- `docs/graphics-protocol.rst:1176–1181` specifies that clear-screen ED2 also clears
  images, unlike other text erasure commands.
- `kitty/screen.c:2972–3010`, `screen_erase_in_display`, calls `grman_clear` for ED2.
- `kitty/graphics.c:2510`, `grman_clear`, invokes `filter_refs` with
  `free_images=true`; `filter_refs:2319–2340` removes the image itself when its
  remaining placements are empty. Keeping a cached-image assumption is unsafe.

[Protocol reference](https://github.com/kovidgoyal/kitty/blob/0d006c44b3d94a6ad7cb9ca11e1549af45095fe4/docs/graphics-protocol.rst#L1176),
[screen implementation](https://github.com/kovidgoyal/kitty/blob/0d006c44b3d94a6ad7cb9ca11e1549af45095fe4/kitty/screen.c#L2972),
[image clearing](https://github.com/kovidgoyal/kitty/blob/0d006c44b3d94a6ad7cb9ca11e1549af45095fe4/kitty/graphics.c#L2319).

Treat full recovery as invalidating both prior placement and loaded-image state
for construction of that packet. If the new frame has a visible image, emit its
load and placement inside the same final synchronized packet even when geometry
is numerically unchanged. Publish those pending image facts only when that packet
commits. Preserve the existing final recapture and capacity handling. The existing
`terminal_pty_graphics` case moves startup artwork to the corner then shrinks to
six rows; it does not exercise a clear with unchanged visible placement. Extend
that direct episode with the row-only resize and exact replacement commands.

## F2: equal final dimensions can hide observed physical damage

The initial recovery predicate compares only `output.geometry()` with current
rows/columns, plus the committed tiny flag. The resize wake only sets `redraw`.
An already committed101x30 plane can be physically resized to32x12 and back to
101x30 before the next frame is prepared, particularly while its predecessor's
packet is blocked. The wake remains visible but dimensions equal the committed
plane. The code selects routine erase damage painting, possibly an empty delta,
and cannot restore cells lost during the temporary smaller viewport. A resize
that happens after a full packet was prepared can produce the same mistaken
trust if its commit erases any pending recovery flag.

Keep recovery debt for observed resize independently of numerical equality.
A resize observed after packet preparation must survive that packet's completion
and force a fresh recovery frame. This must not replace or append to pending
packet bytes. Directly exercise shrink-and-return while output is pending, with
final dimensions equal to the old committed frame, then inspect physical cells.
This is the existing pending-output/resize fault class rather than a new audit.

## Checked paths without additional findings

The physical width enters the actual grid and its raw packet bound. At the normal
minimum12 columns, inner width remains8; task pane and sprite paths retain their
larger width guards. Existing palette and composer bounds remain nonnegative.
Tiny presentation still has positive fallback ioctl dimensions, now resets SGR
before clearing, and its added literal bytes fit the existing fixed overhead.
Its own geometry is committed only after flush. Return from a committed tiny frame
selects full recovery. Editor handoff drains output, detaches and resets committed
geometry to zero; reattach therefore selects full recovery. The integration does
not change borrowed packet lifetime, sole input processing, output fairness,
commit-after-flush or editor mode order.

Normal image augmentation still replaces the final synchronized close, captures
the complete augmented literal packet in a new RawUiFrame and discards the first
capture. F1's fix must retain that ordering. The new CTest uses the actual main
executable and a finite90-second serial deadline; the independent oracle lane
owns its actual physical-screen assertions. No claim about its eventual test
results follows from this source review.
