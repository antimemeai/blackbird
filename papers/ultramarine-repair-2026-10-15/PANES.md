# Ultramarine pane corruption: cause, repair and direct checks

2026-10-15. Pane lane for arconaut-6ao. The screenshot defect is an owned Unicode
cluster-width error. Plain spaces and letters separated by U+200D were collapsed
into a two-cell modeled cluster. Real terminals keep their spacing cells, so
text crossed the pane divider; subsequent delta updates left vertical fragments.
The earlier owned VT oracle contained the same mistake and concealed the defect.

## Retained session and discriminating evidence

Read the original session only, without invoking its launcher, provider, commands,
recovery, or session restoration. [Triage](../ultramarine-triage-2026-10-15.md)
records the screenshot, original session, absent checkout and original build
identifier13a19f1daa95dd31c99697a1f7fc037aabaadeb803eec9c138bd42deed5f42ea.
Do not identify the screenshot binary as current676120c.

The original descriptions specify native little-endian unpadded fields: ARJ1
records contain a32byte raw origin followed by the16byte event envelope.
UI4040 records frame geometry,4041 captured bytes,4042 custody,4044 write requests
with packet reference/offset/extent, and4045 the native write result. Producer
ordinals order these records: data and control partitions can flush into journal
files in a different physical order. Replaying discarded candidates as output
would not describe the transport.

The run contains3485 captured packets and11274 native write outcomes, including
7115 failed writes. Most normal frames use a104×52 grid within a105column
physical terminal. The first later geometry change is4040 in events.5 at
byte1179568, ordinal1780931. The successful recorded write extents before that
change form a917008byte stream containing the screenshot's final conversation.
Original and reconstructed content stay in ignored context.

The initial owned decoder claimed a clean divider at zero-based column68,
a blank gutter at69 and task text beginning at70. Replaying the **same successful
bytes** into pinned Microsoft Ghostty and Alacritty engines at105×52 reproduces
the screenshot's vertical glyphs, including matching letters beside the task
labels and later transcript. Therefore no historical borrowed-byte mutation or
unrecorded writer is needed to explain the defect. A modeled grid and an oracle
that share a width assumption can agree while the terminal paints incorrectly.

The stream contains125 zero-width joiners, including scraped prose with
`SPACE ZWJ SPACE ZWJ SPACE`. The old native scanner models those three spaces as
two cells. Both real engines use three spacing cells. A separate minimal replay
places X at column3 after this sequence, and at column4 after two pictographs
separated by ZWSP/LRM, ZWJ. Exact original-stream and minimal-parser states are
retained under context/ultramarine-repair-2026-10-15/.

The historical redraw gate protected the borrowed normal, image and tiny packet
strings; the previous presenter study did not establish a use-after-free. Current
676120c owns those final packets until complete output. This repair concerns
incorrect cell modeling, which remained in its unchanged scanner. Current main
TUI callbacks route status, process output and notices through TerminalUI; plain
stdout/stderr paths are selected outside TUI. This source observation is not a
claim to inventory every write in the absent historical checkout.

## Selected design and implementation

Unicode [UAX29 GB11](https://www.unicode.org/reports/tr29/tr29-47.html#GB11)
restricts this pictographic sequence to Extended_Pictographic, Extend*, ZWJ,
Extended_Pictographic. [Unicode17 emoji data](https://www.unicode.org/Public/17.0.0/ucd/emoji/emoji-data.txt)
and [GraphemeBreakProperty](https://www.unicode.org/Public/17.0.0/ucd/auxiliary/GraphemeBreakProperty.txt)
provide these exact properties. [The manifest](UNICODE_SOURCES.tsv) records
original URLs, versions, hashes, acquisition, author, license and restoration.
Original data remain intact in ignored quarantine/unicode-17.0.0/.

Root reviewed the cause-specific design and plan before product edits:

1. Add literal native red expectations for plain-space/ASCII joiner positions.
2. Gate pictographic joining on these Unicode properties in the shared wrapping/
   raster scanner; preserve ordinary spacing cells and real emoji sequences.
3. Correct the separate streaming VT oracle and add literal expected cells.
4. Exercise production composition through both independent Microsoft parsers,
   readable styled grids and real native stdout EAGAIN with graphics/resize.
5. Integrate root's concrete review findings and relevant profile results.

src/unicode_pictographic.hpp owns156 Extended_Pictographic ranges and384 Extend
ranges derived from these normative data. ASCII skips table search. Both arrays
have explicit dimensions; binary search is bounded and no library is adopted.
src/chat_view.cpp tracks pictograph eligibility through actual Extend scalars,
permits joining only after an eligible ZWJ, and preserves the existing emoji
modifier, family, regional flag and ordinary combining behavior. An unattached
leading ZWJ occupies no cell and cannot absorb the next ASCII scalar; its exact
source remains retained while rasterization omits the unattached format scalar.
Repeated leading joiners always advance input.

This is the selected terminal-cluster profile, not complete UAX29 segmentation
or universal terminal-width conformance. Ordinary width still uses the qualified
host wcwidth behavior. The independent development oracle consumes the same
normative property values, with a separate streaming state implementation and
literal expected cells; Microsoft engines supply the independent parser check.

## Direct regression checks and review findings

The unchanged pre-repair scanner fails the new native test with
`plain-text joiners collapse spacing cells`. An isolated synthetic production
105×52 Microsoft scenario also fails on the pre-repair executable with
`wide pane divider overwritten`. That executable is a current-stack control,
not a claim to reconstruct the absent screenshot checkout. Its retained identity
is8735ce1769f24f2cb669605c27e172234c7260e90d0f08d60b3a3580510e7fc8.

The candidate passes these focused checks on macOS:

- chat_view_test: literal space/ASCII positions and wrapping, leading/repeated
  joiners, emoji combining/modifier/family preservation, non-Extend barriers.
- chat_render_oracle: nine emitted frame episodes, incremental/full styled
  cells, byte-split controls and literal plain-joiner/barrier cells.
- terminal_blocked: the original80column scroll episode plus a121column to
  **105×52** episode with plain joiners, empty task pane and Kitty graphics.
  Actual EAGAIN occurs before typing/paste and Ctrl-C; a side-channel observation
  proves cancellation before stdout is drained. After drain/resize, every divider
  and gutter cell, task origin, draft/cursor, image commands and restoration match
  their specified values. A fresh atomic observation distinguishes the forced
  EAGAIN from natural startup backpressure.
- Microsoft Ghostty and Alacritty: actual isolated Blackbird workflow, wide pane
  under24 rows of synthetic plain-joiner output, all52 divider/gutter rows, task
  origin at70/7 and composer cursor at4/49. The existing palette, paste, resize,
  exactly-one effect and exit checks remain.
- Readable styled grid: new conversation-plain-joiners expectation deliberately
  captured with the existing single-case update command and reviewed. It retains
  ZWJ bytes in cell tails while all spacing cells remain width1. Other baselines
  were not changed.

Root's source review identified that wcwidth0 alone does not mean Extend:
U+200B ZWSP and U+200E LRM must break pictographic eligibility. Both engines
independently confirmed the four-cell result, and the pinned Extend table plus
literal barrier/post-ZWJ-Extend cases correct this finding. The Linux Clang18
profile also found that384-element std::array CTAD exceeds its256-expression
nesting limit. Explicit array dimensions fix that portability error without
compiler flags. Root owns the final integrated profile/gate result and matched
performance report; focused Mac checks do not substitute for that delivery.

The Linux ASan recheck subsequently exposed a fixture-pressure error: the
original80column episode passed, but the new pane episode did not observe the
required native EAGAIN within its existing5second step. A failed4096byte filler
write did not guarantee that Linux's PTY write/flip buffers had no room for a
small title/spinner delta. The script now fills through4096/256/16/1byte requests
and tops up newly available physical room during the B/C waits without reading
stdout. Actual native B and pre-drain cancellation remain mandatory; deadlines
and expected cells are unchanged. The final script passes both Mac episodes;
root retains the affected Linux ASan rerun and final source-gate result. This
is the same bounded recheck's concrete fixture correction, not a product change.
