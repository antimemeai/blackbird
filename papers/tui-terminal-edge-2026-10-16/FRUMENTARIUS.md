# Mature terminal right-edge strategies

2026-10-16. One source-reading pass for arconaut-arw. Research only: no product
change, reference execution, framework adoption or new dependency. Existing
quarantine snapshots were sufficient; no source archive was downloaded or changed.
[Exact reused sources and restoration](SOURCES.tsv) retain original archive hashes,
licenses and revision identities. Original acquisition records:
[Neovim/FINAL CUT/Notcurses](../tui-2026-10-15/performance/MANIFEST.tsv),
[tmux](../2026-10-09-command-jobs/platform-references.json).

The elegant common rule is **own every physical cell, even if some cells are an
intentional blank margin**. What belongs in a cell and how to emit it are separate
choices. A physical blank may be emitted with an erase command; it need not be a
literal space that reaches the wrapping edge. This is a synthesis of the mechanisms
below, not a quotation or adopted implementation decision.

## 1. Erase to the real right edge: tmux

At `82abcd175cca43c671af3690cd0af74c4c75621c`, `tty_clear_line`
(`tty.c:1195–1233`) uses EL when a cleared segment reaches the physical terminal
width. It first checks background-color erase suitability; otherwise it tries
other supported erases or spaces. `tty_resize` updates physical dimensions then
calls `tty_invalidate`; that function discards cached cursor, margins and attributes,
resets terminal state and reestablishes position. [Exact line clearing](https://github.com/tmux/tmux/blob/82abcd175cca43c671af3690cd0af74c4c75621c/tty.c#L1195),
[resize](https://github.com/tmux/tmux/blob/82abcd175cca43c671af3690cd0af74c4c75621c/tty.c#L123),
[invalidation](https://github.com/tmux/tmux/blob/82abcd175cca43c671af3690cd0af74c4c75621c/tty.c#L2144).

Its cursor handling also recognizes pending wrap. `tty_cursor_pane_unless_wrap`
allows natural wrapping only under explicit full-width and region conditions.
`tty_region` first repositions an edge-pending cursor because some terminals do
not reset the pending-wrap flag correctly on region changes. [Cursor/region handling](https://github.com/tmux/tmux/blob/82abcd175cca43c671af3690cd0af74c4c75621c/tty.c#L2194).

**Blackbird candidate:** preserve a designed blank margin but explicitly erase its
physical cells. A resize recovery packet could clear the physical display before
repainting; a durable version represents physical dimensions and clears trailing
blank spans as part of normal damage output. Neither requires printing bottom-right.
Use the intended margin rendition before erasing; default blank and colored blank
are different requirements. This does not reclaim the column for content.

## 2. Use the full width and track pending wrap: Neovim

At `3068a8f652c8e83aac0696b4a513f369b5bbbd99`, `print_cell` and
`final_column_wrap` (`src/nvim/tui/tui.c:1054–1083`) distinguish terminals that
wrap immediately from those that wait for the next printable character. The grid
can retain `col == width` to represent that pending state. Cursor movement avoids
relative-left shortcuts when deferred wrap makes the actual cursor position
ambiguous, using absolute addressing instead. [Print/wrap](https://github.com/neovim/neovim/blob/3068a8f652c8e83aac0696b4a513f369b5bbbd99/src/nvim/tui/tui.c#L1054),
[cursor movement](https://github.com/neovim/neovim/blob/3068a8f652c8e83aac0696b4a513f369b5bbbd99/src/nvim/tui/tui.c#L1150).

`clear_region` selects screen/end-of-screen/end-of-line erases only when geometry
and rendition allow them, else character erases or spaces. `tui_grid_resize`
clips invalid rectangles and invalidates cursor position; `tui_grid_clear` clears
the entire terminal extent. [Clearing and resize](https://github.com/neovim/neovim/blob/3068a8f652c8e83aac0696b4a513f369b5bbbd99/src/nvim/tui/tui.c#L1275).

**Blackbird candidate:** recover the column for layout, explicitly address each
changed row and the final cursor, and model the terminal profile's edge behavior.
The current painter already uses absolute CUP for row starts and final cursor,
so it has useful structure. This source alone does not establish safe bottom-right
printing on every immediate-wrap profile; that needs a policy and direct checks.
Do not assume terminal row-wrap metadata and visible cell equality are interchangeable.

## 3. Protect only the bottom-right write: FINAL CUT

At `0ed8567d9438d093ee4bf9e33c013cbb37795b70`, `appendCharacter` routes the
physical lower-right cell to `appendLowerRight`
(`final/output/tty/ftermoutput.cpp:1265–1277,1334–1393`). With no automatic margins
it writes normally. With exit/enter-auto-margin capabilities it emits disable,
glyph, enable as an ordered sequence. Without those capabilities, it writes the
final glyph in the penultimate position, returns there, then inserts the
penultimate glyph so the final glyph shifts right. It chooses among parameterized
insert-character, insert mode, and single-character insertion.
[Exact lower-right path](https://github.com/gansm/finalcut/blob/0ed8567d9438d093ee4bf9e33c013cbb37795b70/final/output/tty/ftermoutput.cpp#L1265),
[capability alternatives](https://github.com/gansm/finalcut/blob/0ed8567d9438d093ee4bf9e33c013cbb37795b70/final/output/tty/ftermoutput.cpp#L1334).

**Blackbird candidate:** use full physical width, with a narrow bottom-right
emission policy. Local auto-wrap suppression has smaller mode scope than disabling
wrap for the whole application. It adds mode-restoration obligations after output
errors, suspension and teardown. The insertion fallback is a genuine different
technique but substantially complicates wide/combining-cell handling and tiny
geometries; the existence of this reference does not qualify our Unicode cases.
If the desired last cell is blank, EL can avoid the glyph-write case entirely.

## 4. Recover a physical screen whose cache is untrusted: Notcurses

At `b26048eebc74d5d254717d3332fa484718f9efe6`, `restripe_lastframe` preserves
the overlapping physical framebuffer on resize and initializes newly exposed
cells. `notcurses_refresh` separately remeasures, clears/homes, marks every retained
cell damaged, and rasterizes again. It is a recovery path, not evidence that every
ordinary resize performs a whole-screen erase. [Resize storage](https://github.com/dankamongmen/notcurses/blob/b26048eebc74d5d254717d3332fa484718f9efe6/src/lib/render.c#L8),
[refresh](https://github.com/dankamongmen/notcurses/blob/b26048eebc74d5d254717d3332fa484718f9efe6/src/lib/render.c#L1399).

Its rendering notes explain that resize can occur during writeout; signal receipt,
actual geometry and delivery are not atomic. Its deltas depend on previous emitted
frames, so they must be written in order. Its implementation uses blocking writeout,
which is unsuitable to transplant into Blackbird's input owner.
[Exact rendering/writeout discussion](https://github.com/dankamongmen/notcurses/blob/b26048eebc74d5d254717d3332fa484718f9efe6/doc/HACKING.md#L86).

**Blackbird candidate:** treat geometry change or returned terminal ownership as
invalidation of physical screen assumptions, with explicit full recovery. Do not
pretend one resize packet makes geometry atomic. Finish the immutable in-flight
packet, then recover using the latest measured geometry. Preserve responsive input
and the existing pending-output ownership rather than importing blocking writeout.

## Specification anchor

The retained [xterm control-sequence document](../tui-2026-10-15/input/documents.tsv)
was captured 2026-10-15, SHA-256
`b4c4fbe5738b82c91860b924c8c0ae1afbacf637b4184f98b0382e0e8f033475`,
293655 bytes, at
`quarantine/tui-2026-10-15/input/documents/xterm-ctlseqs.html`.
It defines EL erase-to-right, CUP absolute position, ICH insertion, and private
mode 7 enable/disable auto-wrap. These define the commands above, not an assurance
that every emulator implements every edge case identically.
[Primary documentation](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html).

## Options worth carrying into design

These are our engineering inferences, not claims copied from the references.

| Candidate | Useful property | Cost or limit |
| --- | --- | --- |
| Physical resize erase, retain current layout width | Small direct repair to stale cells | Leaves a reserved column; ordinary physical state still needs a clear ownership rule |
| Physical framebuffer with an explicit blank margin; EL for blank tails | Every cell has an intended rendition; avoids bottom-right printing | Painter geometry and raw frame geometry must distinguish physical and content extents |
| Full-width content with absolute positioning and wrap-state policy | Reclaims horizontal room | Must qualify pending wrap, actual bottom-right behavior and cell widths |
| Full-width content with local auto-wrap suppression at bottom-right | Contains the edge exception in one output routine | Own and restore changed mode; partial output may stop inside the bracket |
| Penultimate-cell insertion fallback | Supports profiles without switchable auto-wrap | More byte/state/Unicode complexity; likely unnecessary unless a supported terminal demands it |

The most promising durable design is physical framebuffer ownership, with content
insets represented explicitly. That is compatible with either a designed blank
margin or full-width content. Choose edge emission separately. A small resize
clear is a useful candidate repair, but should not disguise physical columns as
nonexistent. No implementation strategy is selected by this research report.

## Direct checks that would distinguish the candidates

No new tests were run in this research pass. Use the existing Microsoft actual-main
oracle and independent Ghostty/Alacritty styled screen inspection, alongside owned
packet/partial-write tests, when an implementation is authorized.

| Fault class | Required direct observation |
| --- | --- |
| Original stale-cell bug | Seed distinct right-edge text at 101×30, resize to 32×12; assert all 32 physical columns and intended bottom-right rendition, not a cropped logical snapshot |
| Margin policy | Both growth and shrink, unchanged logical rows and deliberately dirty physical margin; margin becomes the exact intended blank/style |
| Pending wrap | Print the final column, then absolute-position elsewhere and print; assert no phantom newline or overwritten cell |
| Bottom-right scrolling | Distinct sentinel on every row, update physical lower-right; assert no row shift, then print another addressed character |
| Unicode edge ownership | Width-two glyph ending at edge; glyph that cannot fit; width-two-to-narrow replacement; combining tails and supported width policy; no orphan continuation cell |
| Erase rendition | Default and non-default backgrounds/attributes; erased cell equals intended style, neighboring text unchanged |
| Partial output and resize | Backpressure interrupts every boundary in CUP/EL/mode/glyph sequence; resize while old packet pending; input stays serviceable and final drained recovery has correct physical contents |
| Mode lifecycle | For auto-wrap candidates: ordinary exit, failed output and editor suspend/resume leave the promised terminal mode; do not infer restoration from screen text alone |
| Geometry limits | One column/row, ordinary minimum UI size and tiny fallback; no underflow in penultimate-cell strategy |

Every emitted erase/mode byte belongs in the final containing packet whose bytes
are handed to the existing nonblocking output owner. Raw capture must preserve
those final bytes. Existing image handling may recapture a transformed packet;
the invariant is final-byte identity, not a ban on transformation after preliminary
capture. No direct terminal write should bypass the queued packet or its capture.
