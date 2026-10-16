# Blackbird physical edge map

Source base: qualified Lua source3e3a561, published through40a9212. This is a
source reading for candidate selection, not a new execution result.

## Where the physical column disappears

1. `src/terminal.cpp:2531–2562` obtains the actual terminal geometry with ioctl.
2. `src/terminal.cpp:2594` sets layout width to physical columns minus one (or1).
3. `src/terminal.cpp:2674` builds ChatGrid with that reduced width.
4. `src/chat_view.cpp:484–538` treats invalidation or geometry change as a full
   logical repaint. Both dirty detection and emission stop at next.width. There
   is no row-end erase or physical-screen clear in this ordinary painter.
5. `src/chat_view.cpp:718` records committed frame columns as grid.width. Thus
   downstream frame geometry also describes the logical plane, not the terminal.

For a physical32-column screen, the ordinary painter owns only31 cells per row.
A resize can preserve/reflow old cells into physical column32, beyond that plane.
The original margin's motivation is not documented in the introducing composer
commit20ffcea; avoiding right-edge wrapping remains an inference. The initial
explanation should not be treated as a recovered design decision.

The tiny fallback is different: `src/terminal.cpp:2607` emits home plus ED2 before
its bounded draft and records actual physical geometry. It does not justify
calling the ordinary path a physical full-screen clear.

## Constraints on each candidate

- **Framebuffer versus content bounds.** Keeping a visible margin and leaving a
  physical column unmanaged are different choices. A physical extent may contain
  a blank margin, cleared with an erase operation rather than a printed space.
  Retaining columns-minus-one for content need not imply the same painter extent.
- **Partial output.** ChatPresenter owns the containing packet. ChatOutput keeps
  its offset and nonblocking descriptor; the grid commits only after the complete
  packet flushes (`src/chat_view.cpp:708–771`). Do not erase the live terminal via
  a side writer, replace an incomplete packet or commit pending wrap/clear state
  before the corresponding bytes complete.
- **Raw originals.** Ordinary frame preparation is captured at terminal.cpp:2890.
  A changed packet must be captured with its actual bounds and handed to output.
  The graphics path already demonstrates legitimate final recapture: build final
  packet, image.capture, output.start(image.handoff), discard preliminary capture.
  No uncaptured direct erase/mode write is an acceptable shortcut.
- **Mode lifecycle.** TerminalMode enters alternate screen and bracketed paste;
  it currently does not set/reset DECAWM (`src/terminal_custody.cpp:51–53`). The
  editor drains pending output before detach/suspend, then resumes and invalidates
  on reattach (`src/terminal.cpp:2219–2241`). A wrap-mode candidate needs explicit
  enter/exit/error/editor behavior at this owner, rather than setting a mode once
  in the painter and forgetting it.
- **Graphics and styles.** Preserve the current image packet integration. EL/ED
  blank attributes, nondefault background and wide-cell footprints need actual
  terminal checks; erasing is not synonymous with emitting a default-styled space.

## The oracle must include the excluded area

Current styled snapshots are grids. Their logical widths cannot establish that
an excluded physical column is clear. The owned VT-subset oracle discards cells
outside the announced logical width on geometry changes
(`scripts/terminal-screen-oracle.py:303–305`); its cold/delta comparison therefore
cannot expose this exact fault either. It is not a general wrap/scroll emulator.

The existing actual Microsoft32-column episodes do retain the stale column in
both Ghostty and Alacritty. Reuse those profiles for later behavioral REDs, with
explicit expected physical cells and no stale baseline blessing. Required
observations are all physical cells including the last one, cursor and wrap/scroll
behavior, alongside the existing draft/partial-output/raw invariants. Candidate
selection does not run a new test campaign or declare a repair.

## Protocol reference already held

The original XTerm control-sequence capture from2026-10-15 is retained at
`quarantine/tui-2026-10-15/input/documents/xterm-ctlseqs.html`, SHA256
`b4c4fbe5738b82c91860b924c8c0ae1afbacf637b4184f98b0382e0e8f033475`.
Its manifest/restoration are in
[the input document catalog](../tui-2026-10-15/input/documents.tsv).
The EL entry defines erase-to-right; DECAWM7 selects automatic wrapping and its
reset disables it. These specify available operations, not which application
strategy Blackbird should adopt. Canonical upstream:
[XTerm control sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html).
