# Terminal representation class survey

2026-10-15 local date; source `095d7d14fcbf68826febb87f52d910f49f080caf`.
This is a new search for other instances of the representation bug class, not
another approval review of the delivered Ultramarine repair. No product, test,
configuration, issue database or shared journal was changed. No provider/session
replay, user artwork, AFL output, new fuzz campaign or broad test run was used.

The remaining problem is larger than ZWJ eligibility. Three current production
paths calculate cells independently, and the grid still fabricates spacing
widths for bytes that the physical terminal does not advance over. Four concrete
source findings follow. Terminal capability selection is a separate design gap.

## Direct observation

A small standalone C++ probe calls the current `terminal_lines`, `ChatGrid::line`
and `ChatPainter::prepare` against the existing owned Debug static libraries.
A Python writer sends the exact resulting packet to each pinned Microsoft
terminal backend. It does not run Blackbird, a provider or any user workflow.
Coordinates below are zero-based positions of the final literal `X`.

| Input before X | Native grid X | Ghostty legacy X | Ghostty 2027 X | Alacritty X, either mode request |
| --- | ---: | ---: | ---: | ---: |
| `A U+FE0F` | 2 | 1 | 1 | 1 |
| `A U+1F3FD` | 1 | 3 | 3 | 3 |
| `U+0301` at the start | 1 | 0 | 0 | 0 |
| `U+200B` at the start | 1 | 0 | 0 | 0 |
| `U+1F469 ZWJ U+1F4BB` | 2 | 4 | 2 | 4 |
| `U+2600 U+FE0E` (control case) | 1 | 1 | 1 | 1 |
| `A U+20E3` | 2 | 1 | 1 | 1 |
| Styled span `e`, separate span `U+0301` | 2 | 1 | 1 | 1 |
| U+1F469 plus 2100 U+0301, replaced by U+FFFD | 2 | 1 | 1 | 1 |

The Alacritty results reflect the observed backend behavior; sending 2027 did
not change them. They are not a claim that it implements that mode.
The bare Ghostty backend is not the full desktop Ghostty configuration.

Exact scratch source, reproduction command, logs, packets, cells, calls and
input/library hashes are in ignored
`context/cpp-bug-class-audit-2026-10-15/terminal/`:
`probe.cpp`, `build-probe.sh`, `provenance.json`, `probe.log`, `probe-legacy.vt`,
`probe.vt`, `replay.log`, `replay-unicode.log`, `*-cells.json` and
`*-unicode-cells.json`. The run uses the installed Microsoft driver profile:
0.1.0, revision `bcc41b8b11cbcc0290b07d01aada636b1df707a0`, macOS arm64
binary SHA256 `338f3c17be7241a6dc93cadf072c2d6a35049d8d8be13b10776034f12e6690a0`.
The first hand-written compile command omitted the qualified SDK sysroot and
failed before producing a binary; the saved command uses the installed profile's
actual SDK. No product compilation flags were changed.

## T1 — invalid emoji suffixes invent or hide spacing cells

**P1; confirmed, high confidence.**
[chat_view.cpp:82](../../src/chat_view.cpp#L82) unconditionally absorbs every
skin-tone modifier after every base. At line86 it unconditionally promotes any
base followed by VS16 or keycap enclosing mark to two cells.

Reachable trigger: ordinary pasted/model/tool text `A🏽X`, `A️X` or `A⃣X`.
`terminal_lines` admits these valid UTF-8 scalars; `ChatView` and the composer
ultimately send them through the grid. The input need not be a well-formed emoji
sequence, and ordinary displayed text has no such precondition.

For `A🏽X`, the native grid places X at1 while both engines paint it at3. Repeating
this unit can cross a pane boundary in the same way as the original undercount.
For invalid VS16/keycap bases the grid overcounts by1; the actual rest of the
row shifts left and the painter's later delta coordinates no longer describe
the displayed row. The owned Python oracle shares the modifier and unconditional
VS16/keycap promotion assumptions, so it also misses these disagreements.

Invariant: suffix properties alone cannot decide the emitted cluster's spacing
extent. Segmentation, permitted emoji presentation and the selected terminal
width behavior are separate questions. In particular, treating a modifier as a
non-breaking grapheme component does not justify discarding its positive terminal
advance after an arbitrary ASCII base.

Direct repair check: literal terminal-engine X coordinates above, valid modifier
base controls, repeated units adjacent to a divider, and incremental replacement
of the first unit. Test invalid bases as deliberately allowed display text.
Do not merely assert the model's chosen width or compare two owned scanners.

## T2 — zero-width starts and style boundaries become phantom cells

**P1 for display correctness; confirmed, high confidence.**
[chat_view.cpp:100](../../src/chat_view.cpp#L100) forces every cluster except the
special-cased leading ZWJ to at least one cell. `ChatGrid::line` restarts the
scanner for each style span at [line492](../../src/chat_view.cpp#L492).

Reachable trigger: leading U+0301 or U+200B in displayed text, a line wrapping
before such a scalar, or markdown `**e**` immediately followed by U+0301 and X.
The markdown produces a heading span containing e followed by a normal span
starting with the accent. A boundary between styled runs is not a new terminal
spacing character. Both engines place X at1 for the split-span example; the
grid reserves2. The leading examples reserve1 while engines advance0.

These are distinct new tokens of the repaired joiner class: the special case
for U+200D does not cover other zero-advance scalars or the style split. No
universal grapheme-editing claim is needed to establish the emitted-cell mismatch.

Direct repair check: the literal cases above, the same text in one span and two
spans, the marks after a rail/padding space, and a narrow wrap followed by a delta.
A useful metamorphic property is that introducing an SGR-only style boundary must
not move later spacing text. Choose explicitly whether an unattached mark is
omitted, attaches to an available base, or gets a visible placeholder; in the
last case emit the placeholder whose width was reserved.

## T3 — composer wrapping and caret placement use another width model

**P2; confirmed native mismatch, high confidence.**
[terminal.cpp:268](../../src/terminal.cpp#L268) sums per-scalar `wcwidth`;
[terminal.cpp:1138](../../src/terminal.cpp#L1138) wraps per scalar;
[terminal.cpp:2523](../../src/terminal.cpp#L2523) separately sums the draft prefix.
The same scalar sum determines right padding at line2515. The rasterizer uses
its cluster scanner instead.

The direct probe returns:

```text
terminal_lines("👩‍💻X", 2) -> [👩‍] [💻] [X]
```

The grid regards that valid emoji sequence as2 cells. On a Unicode-mode Ghostty,
the physical terminal also paints it as2; the composer calculates4 before X.
Thus the caret and right border can be displaced by2 even in a terminal matching
the grid's intended profile. Near a line boundary the wrapper splits the sequence
before the laptop, changing what is displayed and which draft row contains the
caret. This also affects task title clipping and other `terminal_lines` callers,
although those surfaces have different presentation limits.

The snapshot helper at
[tui_snapshot_test.cpp:128](../../tests/tui_snapshot_test.cpp#L128) repeats the
production caret sum, including `mbrtowc` and `wcwidth`. It is a stable-baseline
mechanism, not an independent expected caret-column oracle for these inputs.

Direct repair check: literal composer cursor coordinates after a family/ZWJ
emoji and keycap under a selected Unicode width profile, at both roomy and
narrow widths; assert the emitted caret and right border in an external engine.
The source-level property is that wrapping, rasterization, padding and caret
mapping consume the same byte-to-cell map. Retained source offsets remain bytes.

## T4 — fallback glyph retains the width of the discarded cluster

**P2; confirmed, high confidence.**
[chat_view.cpp:516](../../src/chat_view.cpp#L516) assigns original cluster width;
at line519 the oversized-cluster path replaces its scalar with U+FFFD, but
line526 advances by the original width and line527 still installs a wide-cell
continuation. A discarded width2 cluster therefore emits one replacement cell
while the grid reserves2.

The direct probe uses U+1F469 plus2100 combining acute marks. It takes the
intentional4096byte fallback and produces a width2 U+FFFD cell; both physical
engines place the next X at1, the grid at2. This path is reachable through the
composer's1MiB draft even though the separate ChatView per-line source cap can
prevent that large cluster in ordinary assistant transcript text. This is not
an argument to raise the display cap.

Invariant: replacement is a change of displayed representation; recalculate its
actual footprint and clipping before writing the grid. The current regression
at `tests/chat_view_test.cpp` checks only that the replacement scalar appears.

Direct repair check: wide and narrow oversized bases, exact X/border cell,
subsequent overwrite of the former continuation, and clipping at the last cell.

## Profile selection and compatibility — explicit open design question

There is no current terminal2027 negotiation in owned terminal source. The grid
assumes joined emoji width2. In the bare pinned backend default, both engines
paint the valid sequence above with4 cells; explicitly setting2027 switches
Ghostty to2. However, the acquired full Ghostty desktop source defaults
`grapheme-width-method` to Unicode, while the bare embedded engine does not
apply that desktop configuration. Therefore this experiment **does not prove**
a new valid-ZWJ failure in the operator's desktop Ghostty.

The current component design already calls for a negotiated/configured profile
with declared tailoring and fallback, and explicitly acknowledges real terminal
width disagreements. This remains that capability gap, separate from T1–T4,
which violate the owned/physical contract even within the observed matching
Unicode profile. A meaningful compatibility check must name the mode/profile;
calling a parser “Ghostty” alone is insufficient.

## Surveyed boundaries and negative cases

- Owned `src`, `include`, `programs`, relevant tests and terminal scripts were
  searched for width/UTF-8/locale calls, control output and rendering consumers.
  The width implementations are concentrated in terminal.cpp/chat_view.cpp;
  task_view uses terminal_lines, so it inherits T3 rather than owning another
  scanner. Lua's package HUD deliberately emits ASCII; no additional Lua
  terminal-cell arithmetic was found in programs.
- The usual ChatView path calls `terminal_lines` before rasterization; C0, DEL,
  invalid UTF-8 and unprintable bytes are presented as literal hex escapes.
  The task and composer presentation paths also pass through this sanitizer.
  This survey found no new reachable raw ESC injection through those paths.
- `ChatGrid::line` itself accepts ordinary strings and assumes safe content.
  `ChatCell` fields are publicly writable. These are construction hazards for
  future adapters, not a demonstrated current untrusted bypass. A safe-display
  type would make the intended boundary explicit.
- The writer advances only by successful native write counts, retains EAGAIN
  offsets and bounds each flush batch. The current presenter owns the final
  string and commits the target only on output completion. The inspected main
  paths do not replace the prepared/writing packet. No additional concrete
  borrowed-packet mutation was found; the public mutable preparation reference
  remains a contract callers can violate, not an observed new violation.
- `mbrtowc`/`wcwidth` depend on the selected host locale. Locale selection does
  not establish a terminal's Unicode version, ambiguous-width preference or
  grapheme mode. This survey does not claim complete UAX29/terminal conformance
  or a new failed-locale product defect.
- The Python VT oracle shares normative EP/Extend data with production, but uses
  separate streaming state. Its modifier/VS16/keycap heuristics still share T1;
  its leading-combining fallback still shares T2. The saved `owned-oracle.json`
  shows X at2/1/1 for invalid VS16/modifier/leading accent, matching production
  rather than either external engine. The style-split case happens to differ
  from production in the useful direction. These disagreements should be
  captured as explicit oracle limitations; do not upgrade it to an unbounded
  terminal emulator as another layer of assurance.

## Literature and reference mechanisms

[UAX11 revision44](https://www.unicode.org/reports/tr11/tr11-44.html) explicitly
warns that East_Asian_Width is not an off-the-shelf answer for modern terminal
emulators. It supports separating the terminal profile from Unicode scalar
properties. [UAX29 revision47](https://www.unicode.org/reports/tr29/tr29-47.html)
provides segmentation, not a complete terminal-cell advance protocol; use its
normative boundary data/tests for segmentation claims, while keeping observed
width tests separate.

[UTS51 revision29](https://www.unicode.org/reports/tr51/tr51-29.html#Emoji_Modifiers)
defines modifier bases and restricts valid presentation sequences to the supplied
data. Its keycap grammar limits bases to digits, # and *. Those property/sequence
checks prevent the specific inference that every arbitrary ASCII base plus a
suffix becomes a two-cell emoji. This is source grounding for the grammar;
physical width still requires the selected terminal profile.

[Neovim mbyte.c at3068a8f652c8](https://github.com/neovim/neovim/blob/3068a8f652c8e83aac0696b4a513f369b5bbbd99/src/nvim/mbyte.c#L1854)
returns the next byte position and cells together in `ClusterInfo`, limiting
independently repeated walks. Its VS16 promotion first checks an emojilike base.
Study this result shape and the property precondition; it does not select
utf8proc as our dependency or make Neovim's tailored behavior our specification.

[Ghostty Terminal.zig at22fa134a07f5](https://github.com/ghostty-org/ghostty/blob/22fa134a07f5749edd9e92c6bc17d8aaf61843be/src/terminal/Terminal.zig#L1489)
separates zero-width attachment from spacing output, ignores unattached zero-width
characters, and checks the preceding base for emoji variation selectors. Its
configuration and modes are separate from the core parser. These are directly
relevant guards for T1/T2 and the profile experiment.

[libvaxis Vaxis.zig atd4c3b3666e0c](https://github.com/rockorager/libvaxis/blob/d4c3b3666e0c090e2b5092ca09893c6db15e0b79/src/Vaxis.zig#L312)
queries capabilities, stores an explicit width method and enables2027 only in the
appropriate detected path. Its separate `gwidth.zig` demonstrates why a reference
is not automatically an oracle: that source also promotes a cluster on any VS16
without the base validation needed for our malformed-input cases. Study the
capability seam, not wholesale heuristics.

The [Terminal Unicode Core proposal](https://github.com/contour-terminal/terminal-unicode-core)
at64f53851ceab9a3cf08db4939bcaef75a0899573 describes2027 as a selected Unicode
mode, including cluster and emoji advance behavior. It identifies itself as an
alpha proposal, not a universally implemented standard. The
[kitty text sizing protocol](https://sw.kovidgoyal.net/kitty/text-sizing-protocol/)
is another possibility: explicit declared cell extent removes reliance on the
peer making the same width calculation, on peers that support it. Capability
query, fallback and restoration are prerequisites; this audit adopts neither
protocol or dependency.

All acquired originals, version/date/author/license/URL, SHA256 and restoration
commands are in [terminal-sources.json](terminal-sources.json). New documents and
the intact small Unicode Core source archive/extraction are ignored under
`quarantine/cpp-bug-class-audit-2026-10-15/terminal/`. Existing Ghostty/libvaxis/
Neovim archives remain intact in their original ignored shelf, explicitly reused
by that manifest rather than copied again. Those extractions were previously
cleaned; the new extraction omits Git metadata, symlinks and filesystem detritus.
Archived instructions were not executed. Open Group wcwidth pages failed through
the browsing adapter; no conclusion depends on quoting them. No reference code
was shipped or dependency selected.

## Mechanisms that narrow the possibility space

These are concrete implementation options for root's plan, not changes made by
this audit. Own the small abstractions; a framework adoption is unnecessary.
The existing component design already specifies the shared text service, so
this is an incremental way to make that design enforceable.

| Mechanism | Directly prevents/catches | Blind spot | Runtime/hot-path cost | Fit under current stack |
| --- | --- | --- | --- | --- |
| Owned `ByteOffset`, `ByteCount`, `CellColumn`, `CellCount`, `CellRow` wrappers with explicit conversions | Accidental byte/cell interchange, row/column swaps and byte slicing by cells | A wrong measurement can still construct a correctly typed CellCount | Zero representation overhead; no allocation; inlined operations | C++20 on both Clangs; use native Lua boundary validation and explicit units when exposing counts |
| One immutable cluster result containing source byte range, emitted text, cell extent and selected profile; use for wrap/grid/caret/padding | T3's independent calculations; T4 replacement footprint left behind | Shared algorithm can still be wrong, as original oracle showed | Linear scan once; scalar ASCII fast path; can replace several current scans | Owned C++ service; Lua consumes bounded results, does not reproduce UTF-8 arithmetic |
| Property/sequence preconditions for modifier/presentation/keycap behavior | T1's unsupported inference from suffix alone | Peer profile/version can still disagree; not full UAX29 merely because tables exist | Bounded table lookup for non-ASCII; ASCII path unchanged | Existing pinned data mechanism is usable, but extend it only for a written behavior contract |
| Normalize unattached zero-width and style-run continuation before grid placement | T2 and style-split phantom spacing | Selection/copy must still map visible fallback/omission to retained bytes | Streaming state across spans; bounded lookback/tail; no per-scalar allocation required | Fits current per-line composition, keep styling as metadata over clusters |
| Typed safe display text accepted by grid, separate trusted control packet type | Raw controls/invalid data accidentally bypassing sanitizer; future adapters relying on undocumented safety | Correctly sanitized Unicode can still have wrong cell extent | One boundary pass already largely exists; wrapper itself zero cost | C++20 private constructors; Lua/adapters return raw source until owned sanitizer creates display text |
| Owned AST/source rule: prohibit direct `wcwidth`/`mbrtowc` cell arithmetic outside the selected decoder/profile module | New copies of T3 and future hidden locale-dependent width walkers | Does not prove allowed module's semantics; must not ban legitimate decoding | Development-only; simple allowlist search initially, AST rule if precise ownership requires it | Current rigor can add one source-policy check after its own direct diagnostic qualification |
| Literal external-engine cells for the small failing corpus, selected modes, style boundaries and incremental overwrite | T1/T2/T4, current shared-oracle blind spots and profile mismatch | Only named peers/modes tested; no universal terminal conformance | Development-only, no production hot-path work | Extend already adopted Microsoft mechanism with semantic literals; retain readable baselines for different purpose |
| UAX29 conformance data plus metamorphic style-split and byte-feed partition properties | Segmentation errors, state loss across feeds/spans | Normative grapheme boundaries are not themselves terminal width | Development-only; bounded corpus, no new fuzz campaign needed | Pin data like current Unicode sources; do not label current limited scanner fully conformant |
| Debug grid shape assertions: valid tail extent, complete wide-cell continuation, bounded cursor and geometry | Corrupt constructed grid and fallback footprint shape | Shape alone accepts T1/T4's wrong declared width; do not call this a display oracle | O(changed cells) or existing traversal; debug-only | Both compilers/sanitizers; check at owned construction, not a new assurance layer around a test |

The compiler cannot infer Unicode semantics from `std::size_t`. Existing
`-Wconversion`, `-Wsign-conversion`, `-Werror` and the current tidy/analyzer slate
already permit these concrete failures. Increasing warning volume alone would
not prevent them.

[bugprone-easily-swappable-parameters](https://clang.llvm.org/extra/clang-tidy/checks/bugprone/easily-swappable-parameters.html)
is already enabled. It diagnoses some same-type parameter hazards, not semantic
mixing of equal-width integers inside functions. Distinct unit types do the
mechanical work after the call boundary. This is a stronger targeted move than
another broad warning bundle.

[Clang lifetimebound](https://clang.llvm.org/docs/AttributeReference.html#lifetimebound),
also documented in
[Clang18](https://releases.llvm.org/18.1.8/tools/clang/docs/AttributeReference.html#lifetimebound),
can expose some dangling views returned from an owner/parameter. It cannot make
a still-live string immutable or generally model `ChatOutput::start` storing a
borrow into an existing object. Do not add the attribute there and claim the
packet ownership problem is solved. A move-only prepared packet/owner interface
and the existing presenter state checks provide the relevant guarantee.
`[[nodiscard]]` can help checked construction results but cannot establish a
width. Consumed/typestate annotations are an optional tool experiment, not a
substitute for an interface whose states/ownership are representable in C++.
No annotation or new compiler flag was added here.

One additional tidy option,
[bugprone-suspicious-stringview-data-usage](https://clang.llvm.org/extra/clang-tidy/checks/bugprone/suspicious-stringview-data-usage.html),
concerns converting a non-null-terminated string_view through data(). It does
not check cell counts, borrowed mutation or UTF-8 segmentation. It is not a
remedy for these findings and is not recommended as campaign expansion merely
because terminal source uses string_view.

The smallest useful delivery unit is a shared representation boundary covering
T1–T4 together: safe display transformation, cross-style cluster results,
profile-qualified extent, byte/cell maps, and fallback before measurement. Give
it the literal cases above and the existing independent-engine mechanism. Keep
terminal negotiation/explicit-width capability work a separately designed unit;
never hide an unobserved terminal profile behind a claim of Unicode correctness.
