# Candidate solutions for the physical terminal edge

2026-10-16. The requested frumentarius read four mature, already quarantined
implementations at exact revisions. No new download, dependency, build or product
change was needed. [Source report](FRUMENTARIUS.md), [source/restoration catalog](SOURCES.tsv)
and [Blackbird renderer map](NATIVE_MAP.md) preserve the study.

The common design lesson is to **own the full physical viewport**. Content can
have an inset; the excluded area still has an intended appearance and must be
cleared. Right-edge output is a separate decision. The current columns-minus-one
rule makes the physical final column disappear from both painting and damage
tracking; a logical full redraw therefore cannot clean it after resize.

| Option | Reference mechanism | Fit and cost for Blackbird |
| --- | --- | --- |
| Erase the blank tail | tmux uses erase-to-end-of-line through the physical right edge, with background-erase capability checks | Smallest conservative route if a blank margin is intentional; clears without printing the final cell, but keeps the content inset |
| Use every column with explicit wrap state | Neovim represents delayed wrap and uses safe cursor addressing | Best match to a full-width owned grid; our painter already positions each changed row and final cursor absolutely. Immediate-wrap profiles and bottom-right behavior still need a declared policy |
| Special-case the bottom-right glyph | FINAL CUT brackets that glyph with auto-wrap disable/enable, with insertion alternatives | Contains the exception; requires capabilities and restoration across partial output, failure and editor handoff. Insertion adds Unicode complexity |
| Recover the physical screen explicitly | Notcurses has a clear/home/damage-all refresh distinct from ordinary deltas | Complementary to the other options after invalidation or terminal handoff. Its blocking writeout is unsuitable for our input owner |

## Provisional preference

Use a physical-size framebuffer and explicit content bounds. Prefer full-width
content with Neovim-style edge/cursor handling for the supported terminal profile;
use tmux-style erasure for blank tails where the intended style permits it. Keep
an explicit full recovery path for resize or returned terminal ownership. An
intentional blank margin remains a layout option, rather than an unowned column.

This preference fits the current absolute-addressed cell renderer without choosing
a new global mode change. FINAL CUT's narrow wrap suppression remains an alternative
if direct bottom-right checks expose a supported profile that needs it. Removing
one subtraction alone is not a qualified repair. Source study does not establish
which edge policy is sufficient on every terminal.

The implementation decision should retain existing packet ownership: finish an
in-flight immutable packet, recover at the latest measured size, capture actual
final bytes, and commit physical/cursor state only after output completes. Avoid
side writes and blocking flushes in input handling.

## What a later repair must show

The report gives direct cases for physical last-column clearing after101×30→32×12,
bottom-right scrolling, pending wrap, wide/combining cells, erase styles, tiny
geometry, output backpressure and mode handoff. The physical rightmost column
must be part of the oracle. Current grid-only snapshots crop that exact area;
the actual Microsoft Ghostty/Alacritty profiles already observe the residue.

arconaut-arw remains open. This is a candidate-options research checkpoint;
no old baseline was rewritten and no source/performance test was rerun.
