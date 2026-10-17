# Actual application viewport oracle

This unit owns `scripts/check-tui-viewport`, an actual-main Microsoft terminal
episode on Ghostty and Alacritty. Before the application changes, start an isolated
local workflow/session, open the existing retained-result instrument at101x30,
resize to32x12, and require the full physical last column's intended characters
and styles. This must fail the old application before the native edit/build.

The narrow single-line layout explicitly requires top-right composer corner
`╮` at `(31,8)`, right border `│` at `(31,9)`, bottom-right box corner `╯` at
`(31,10)` and an ordinary blank at physical bottom-right `(31,11)`. Empty
instrument rows2..7 have ordinary blank final cells. Fixed model/effort and the
completed Source active caption supply the final cells of rows0 and1. These are
literal layout expectations, not a cropped framebuffer or another renderer.

After the original RED, extend the same episode to check editor focus and retained
midcursor draft, Unicode wide/combining input and current scalar deletion, repeated
growth/shrink and tiny return. Tiny8x3 draws `> 123456` across all eight columns;
1x1 and2x2 exercise actual physical bottom-right output before normal return.
Preserve current normal bottom padding. Cursor/modes and clean exit must match
each exercised contract. No turn is submitted; an explicit local workflow writes
a sentinel if accidentally invoked, so no provider is needed.

Capture all literal terminal state/cell responses once, automatic recording from
startup, binary identity and command history for each attempt. Await semantic
text/cell/cursor predicates with a finite fixture watchdog, never a guessed visual
quiescence sleep. Root owns native code, builds and CMake. One direct root source
review and fixes closes this oracle unit; no duplicate review panel.

The initial proposed whole-grapheme Backspace assertion is rejected after reading
the current native editor: `previous`/`next` at terminal.cpp250/258 step UTF-8
scalars. This viewport unit checks that existing behavior exactly, including
retaining e after deleting its combining acute, then removing the wide scalar
without leaving a continuation cell. Grapheme-edit policy is separate work.
