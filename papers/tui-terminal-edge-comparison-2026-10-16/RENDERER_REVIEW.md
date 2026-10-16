# Independent renderer-unit review

2026-10-16. One concrete source/oracle review by the native-fixture owner, who
neither implemented the renderer policy changes nor owns their native assertions.
Reviewed the diff in `include/blackbird/chat_view.hpp` and `src/chat_view.cpp`,
`tests/terminal_edge_test.cpp`, and the comparison PLAN. No builds, tests, nested
review or source edits were performed for this review.

No actionable defect found in the changed renderer unit.

- Addressed remains the API default and follows the existing changed-span path.
  Every changed row gets absolute CUP, and the final cursor gets absolute CUP
  before synchronized output ends. A delayed-wrap cursor at the physical edge
  does not become an unaddressed start for the next row. The unchanged-plane
  native assertion preserves the existing empty-delta behavior.
- Erase scans only owned normal spaces of width1 without a cluster tail. It
  emits explicit normal rendition and an absolute cursor before EL. This gives
  the intended default-background cells whether erase follows the active
  background or uses the terminal default; styled blanks use literal cell
  output. The existing expansion around old/new wide continuation cells runs
  before the erase split, so replacing a prior wide footprint does not leave
  its lead outside the changed span. Native assertions distinguish colored
  literal spaces from normal-tail erasure; the independent terminal oracle is
  responsible for actual physical cells and styles.
- Corner wraps the complete scalar plus cluster tail only when its footprint
  reaches bottom-right. The width-two continuation is skipped after the lead,
  preventing a second bracket; combining marks remain inside the bracket.
  The matching DECAWM enable occurs in the same immutable output packet, before
  the final CUP. This implements the stated wrap-on experimental profile rather
  than claiming preservation of an arbitrary incoming mode. A blocked partial
  packet can temporarily leave wrapping disabled, but its bytes cannot be
  replaced and normal fixture exit drains it before mode handoff.
- Full forces the existing full-span path on every request and prefixes reset
  plus physical-screen erase after synchronized output starts. It repaints all
  cells and finishes with the same final cursor/mode sequence. The native
  unchanged-grid case directly distinguishes this policy from ordinary damage
  painting.
- The raw owned-grid bound adds capacity for the corner bracket and clear/reset
  sequences. Erase trades at least one literal normal blank cell for a reset,
  second CUP and EL; the bound's existing per-cell allowance covers that extra
  row work at the grid's actual bounded dimensions. The changed policies retain
  packet storage ownership, start/flush behavior, commit-after-complete and
  original Presenter geometry publication; no new early commit path appears.

The native cases attack policy-specific output and raw capacity, while physical
terminal behavior, resize, state after wrap transitions and partial-write input
responsiveness remain the separate Microsoft/PTY qualification. Source review
alone does not qualify those behaviors or choose a performance winner. Production
TerminalUI still reserves its old column at this reviewed comparison checkpoint;
full-viewport application integration is a separate planned change.
