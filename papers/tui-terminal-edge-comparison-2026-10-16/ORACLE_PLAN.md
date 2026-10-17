# Actual physical viewport oracle

2026-10-16. This lane owns one direct terminal experiment driver,
`scripts/check-terminal-edge`. It observes candidate output through the already
adopted Microsoft tui-test Ghostty and Alacritty backends. It must request every
physical cell, including the last column and bottom-right cell; it must not use
the owned VT-subset parser or crop to a native logical grid.

The fixture interface is agreed with the native owner before implementation.
Each candidate renders the same explicit finite scenes through the actual
ChatPainter/ChatPresenter output path. A readiness acknowledgement follows
completed output, and is distinct from the expected cell content. Expectations
come from the scene specification, not a second run of the candidate or an
approved stale screenshot. Capture full `state`, `cells`, actual terminal
recording, exact fixture binary identity and command history on success/failure.

Required episodes: distinct row/edge sentinels, long-to-short blank tails,
nondefault styled blanks, last-column replacement, nonblank bottom-right without
scroll, wide and combining clusters at the edge and overwritten wide footprints,
cursor restoration after pending wrap, 101x30 to32x12 resize and recovery, and tiny
geometry. The whole physical expected plane and terminal cursor must agree after
each completed phase. Temporary auto-wrap suppression must restore wraparound.

The current columns-minus-one behavior is the initial behavioral RED: seed a
physical last-column sentinel, render a frame with intentionally blank final
column and demand that the sentinel disappear. Preserve the failing terminal
capture; do not bless it in a baseline. An actual-main resize episode also checks
the existing arw manifestation where practical.

Backpressure uses a separate raw PTY episode owned by the measurement lane,
because tui-test owns its PTY and does not expose its slave descriptor. Retain an output slave, fill actual stdout
to EAGAIN, require an independent fixture event that input was serviced without
draining stdout, then release pressure and check exact packet completion and
latest-geometry recovery. The native owner provides its bounded event/control
interface; do not infer responsiveness from an unchanged screen. This lane does
not add a duplicate pressure observer.

The driver is correctness qualification, not the performance sampler. No new
dependency, provider request, live launcher activation, shared Debug build,
remote timing or AFL interaction belongs to this lane. One concrete independent
review follows implementation; relevant findings are fixed here.

Before finalizing the comparison, include2x4 geometry explicitly: the specified
row3 wide glyph then occupies bottom-right. The original heights30,12,2,1 exercise
combining at bottom-right but never the width-two case. Add that geometry to the
full episode and a focused `wide-corner` episode (startup s, then u/o/c/b at2x4)
for the new direct checks without rerunning the settled37-plane campaign cases.
