# Full physical viewport strategy comparison

Operator2026-10-16 adopts ownership of every physical viewport cell and asks for
several approaches compared for performance and stability. Starts at source3e3a561
and published research4af196d. Use the four reference mechanisms already studied;
no library/framework adoption, acquired execution, live launcher activation or
AFL/artwork interaction.

## Actual candidates

One native ChatPainter/ChatPresenter implementation, one test fixture executable,
runtime policy choice over identical physical-size grids:

- addressed: full-width changed spans, absolute CUP at every changed row and final
  cursor, ordinary delayed-wrap profile. No terminal-mode mutation.
- erase: addressed plus EL for default-rendition blank tails reaching the physical
  edge. Colored/styled blanks remain ordinary cell output; no assumed BCE support.
- corner: addressed plus DECAWM disable/enable around the glyph whose footprint
  reaches bottom-right (including width-two glyphs). The experimental terminal
  profile starts wrap-on; no promise of arbitrary incoming-mode preservation.
- full: clear physical screen and repaint every requested frame. A deliberately
  simple recovery/performance reference, using the same addressed cell output.

Corner is an experimental rejected prototype, selected only by the native fixture;
Ghostty combining at2x2 fails even though Alacritty passes. Keep its exact source
and observed failure; exclude it from eligible performance ranking and the passing
physical-source-gate selection. Its native byte-shape test is not a physical
compatibility claim. No production caller selects this policy.

All own physical width. A synthetic legacy cropped-width fixture is only a
negative control; it is not eligible to win. This is strategy comparison, not a
competition between four unrelated frameworks or terminal applications.

The reusable policy API defaults to addressed. Keep production width-minus-one
until the actual old-main RED and native candidate cases exist; compare full physical candidates before changing the application caller. Production
integration of the selected policy follows this comparison as a distinct change,
using actual surviving results rather than an a-priori timing claim. Other policies
remain explicit native-fixture choices, not operator UI flags. No new duration or
aggregation is added to production capture.

## Ownership and sequence

Root owns renderer policy/native fixture/tests/CMake and integration. One agent
owns independent Microsoft physical-cell oracle and actual-main stale-edge RED.
One owns external comparison observer, remote isolated build/timing/results. These
are distinct mechanisms; no test validates another test. One concrete independent
source/oracle review and fixes follows implementation. No review-of-review panel.

1. Source-grounded design/API/scenes below, direct red oracles against current
   behavior or refusing policy seam before implementation. Keep original failures.
2. Implement finite policies and native experiment fixture. Preserve immovable
   packet, partial-write fairness, final literal raw capture, commit-after-flush,
   escaped/owned Unicode cells and sole input semantics.
3. Native invariants plus both real emulator profiles inspect full physical cells,
   styles/cursor/modes, resize and bottom-right behavior. Broken candidates remain
   in results and are excluded from performance ranking. Add meaningful sanitizer
   checks for changed native owner; no assertion weakening.
4. Matched finite renderer CPU/bytes batches and actual PTY frame/input/resize
   observations on one compiler/profile/host, serial round-robin. Thirty runs per
   eligible policy/geometry initially. No owned build/check overlap with timing,
   tail exclusion or repeat-to-improve. Shared host not claimed exclusive.
5. Publish every attempted qualification/sample and external derived p50/p95/max,
   bytes and CPU costs with exact scope. Distinguish synthetic renderer/fixture
   input latency from full app/provider latency. Keep ordinary bugs in queue.
6. Full required source hook before source commit/push; then docs/data results
   publication with relevant checks. Do not merge main by implication.

## Common native fixture protocol

`terminal_edge_fixture --strategy addressed|erase|corner|full --events PATH`
or `--events-fd N`. Optional `--legacy` deliberately limits painting to cols-1.
Raw stdin single bytes: s sentinel, b blank tails, c styled tails, u Unicode,
o overwrite, r recover using current ioctl geometry, q clean exit. Startup s,
sequence0. Every input command emits INPUT sequence unsigned-byte immediately;
render commands advance sequence. While a packet is pending, keep only latest
requested scene/sequence, never replace its bytes. q acknowledges input, stops
new input and exits after the current packet and already accepted latest request
drain; mode teardown follows.

Events (plain space-delimited, external experiment boundary):
`INPUT seq byte`, `PREP seq wallBefore wallAfter cpuBefore cpuAfter packetBytes`,
`PENDING seq` once per incomplete frame, `READY seq cols rows` only after the
actual presenter commit. PENDING alone is not proof of EAGAIN. Parent prefill of
its retained PTY slave establishes actual refusal for pressure cases.

Batch mode `--strategy NAME --batch sparse|stream|blank|style|unicode|resize
--iterations N --cols N --rows N` uses actual ChatPainter prepare/commit on
common generated grids, emits raw start/end steady/thread-CPU timestamps, number
of frames and summed literal packet bytes. Separate observer derives times;
none is a production event transformation. Exact batch record agreed with owner.

## Explicit scene specification

Coordinates are zero-based; default cells are normal-rendition spaces. Physical
size is current ioctl size. No expected plane is emitted by the native fixture.

- s: each row y consists of width copies of ASCII `A + (y modulo26)`; replace its
  final physical cell with `a + (y modulo26)`. Thus every row has a unique edge.
- b: each row begins with `(y modulo10)` followed by `abc`, clipped at physical
  width. Every remaining cell is a normal space, including bottom-right.
- c: identical characters to b, but the entire physical row, including trailing
  spaces, has Ink::code (foreground188,168,239; background37,40,51).
- u: blank plane; row0 has U at0 then width-two 界 at cols-2 if width>=2;
  row1 has e+combining-acute in final cell; row2 attempts 界 at finalcell, which
  cannot fit and leaves it blank; row3 has code-colored 界 ending at rightedge.
  Only existing rows participate. If width1, row0 retains U; no negative offsets.
- o: each existing row first gets O at0; then n at cols-2 if width>=2 and z at
  cols-1. All other cells normal spaces. This replaces prior wide footprints.
- r retains the last scene and rerenders at current measured dimensions, forcing
  complete invalidation/recovery. End cursor is row0,column0 for all scenes.

The independent driver encodes these expected cells/styles from this specification,
not by reading candidate grids or treating a full-render policy as truth. Compare
101x30->32x12->101x30, ordinary/tiny geometry, styles and all physical edge cells.
