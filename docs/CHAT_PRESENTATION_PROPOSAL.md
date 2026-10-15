# Chat presentation: proposal

Historical October7 proposal. The [TUI doctrine](TUI_DOCTRINE.md),
[component design](TUI_COMPONENTS.md), [current/future views](TUI_VIEWS.md) and
[delivery plan](TUI_IMPLEMENTATION_PLAN.md) now govern the expanded program.
Retain the craft/source ideas below as reference; old mascot/sidebar exclusions,
fixed animation/layout targets, renderer diagnoses and campaign cutoffs do not
override current intent or implemented native output behavior.

2026-10-07. The operator requests a substantial visual investment, source study
and a proposal before coding. This extends TUI_PLAN with a concrete conversation
design. Production remains unchanged in this unit.

Subsequent operator steering makes renderer economy and failure recovery central:
think like a C programmer in 1988, learn from critical systems, and measure every
cost. The [renderer internals study](../papers/2026-10-07-rendering-internals-study.md)
adds Notcurses and libvterm source findings and the proposed cell/output fast path.
Read it alongside this visual proposal. A visually rich interface requires this
rendering work; frequent full-transcript repaints are not the design.

## The screenshot's problem

The new composer has a frame, but the conversation still has equal-weight bare
role labels, arbitrary blank lines, raw request diagnostics, a wrapping usage JSON
object and duplicate completion notices. A greeting takes most of a screen to
say almost nothing. Borders around that transcript cannot fix its composition.

The rendering input is part of the problem: main.cpp assigns the same `emit`
callback to `engine.display` and `engine.process_output`; coding.cpp writes request
size and provider usage through `display`. TerminalUI accumulates a flat string.
These must become distinct presentation events at their producers, while original
audit data and plain output remain available. Do not infer semantic identity from
brackets, the literal word “You”, or model prose.

## Proposed visual language

**A vivid technical conversation, composed like an excellent code editor.**
Warm text; cyan Arco accents; amber operator accents; lilac syntax; mint completion;
coral errors. Dark translucent-looking panels are solid terminal cell backgrounds,
not actual transparency. Use the terminal's background with contrast-adjusted
surfaces, and support a light variant. Avoid filling every message with a heavy box.

The [visual sketch](design/chat-concept.svg) is a code-generated concept, not a
screenshot of implemented behavior. All text is monospace; the accents map to
terminal cells, SGR styles, box drawing and background colors. It uses illustrative
fixtures, not measurements. Rounded vector corners approximate terminal glyphs.

**Operator messages:** compact amber role stamp and a subtle tinted band, with a
two-cell inset. Short prompts consume two or three rows. Long prompts wrap cleanly;
recoverable pasted blocks can fold with exact source available.

**Arco messages:** cyan role stamp and fine left rail, warm white prose, restrained
spacing between paragraphs. Consecutive text deltas extend the same message, not
new cells. Distinguish commentary from final response only when the provider
actually supplies that distinction. Use role/model identifiers from runtime facts.

**Reading width:** prose has a configurable 80–100-column measure, default 92,
left-aligned with a stable inset. Code, diffs and tables can use the wider pane.
At wide widths, right-align compact message facts in their header instead of
stretching every prose line across the window. At small widths, omit metadata first.
This is one flowing conversation, not left/right phone-chat bubbles.

**Hierarchy:** role stamps, bold section headings, inline emphasis, restrained
italic where supported, bright links and distinct inline-code surfaces. Typography
comes from weight, color and space on the existing terminal grid. We do not need
unsupported variable font sizes to make headings feel like headings.

**Code:** a lilac language/file caption, slightly raised background, highlighted
syntax, sensible indentation and an unobtrusive gutter. A generated snippet gets
snippet-relative line numbers; file line numbers require an actual source range.
Copy returns exact code without borders or gutters. Long lines scroll horizontally
when focused, with wrap as an explicit option. Start with C++, Lua, shell, JSON and
diff, falling back to plain code rather than broken highlighting.

**Markdown:** streaming headings, paragraphs, lists, blockquotes, fences, inline
code and links first; tables next. Nested lists keep hanging indents. Tables size
to their content and fall back to stacked records on narrow screens. Literal or
unsupported syntax remains visible. Do not claim a full CommonMark implementation
without the relevant conformance behavior. Fragmented UTF-8 and incomplete fences
must not jump the cursor or conceal output.

**Tools:** a slim activity ribbon inside the turn. Routine calls become rows with
icon/state, operation, meaningful target and elapsed time. Group adjacent completed
calls without reordering them; one expandable body reveals details. A running
shell can expose a bounded live tail, not flood the whole chat. Edits show real
diff previews, green additions/coral removals, with full source accessible. Errors
get a visible coral block and actual exit/status facts, not a small hidden badge.

**Metrics:** one quiet turn footer, for example `2.4s · 1.9k in · 14 out`, only
from observed values. No wrapping provider JSON, request-byte announcement or
duplicate completion paragraph in normal chat. Inspectors retain these facts and
their source/audit references. Context bars have a denominator only when known.
Request retries and uncertainty remain visible as real events.

**Empty state:** a compact branded welcome and a few useful hints, disappearing
after the first conversation. No giant ASCII mascot or persistent tutorial text.

## Motion and terminal ambition

Deliver rich motion in the initial renderer, not as a distant polish phase:

- Small active indicator at 10–12 Hz; gently moving highlight in the active rail.
- Focus-color transitions around 100–150ms; completion settles into a mark with
  a short 200–300ms accent fade. Failure treatment remains visible afterwards.
- Immediate streaming text with a subtle live edge. Never delay real tokens to
  manufacture typing animation; never animate a scrolled reading position.
- Optional compact Braille activity visualization from actual sample history,
  such as received-token rate. No fabricated activity waveform; no samples means
  an honest indeterminate indicator. This comes after real metric wiring.

Use monotonic time, one effect scheduler and changed-cell writes. Wake only while
effects are active; do not build a permanent 60fps whole-screen loop. Motion off
keeps the same density and theme. The current full-transcript redraw is inadequate
for these effects; fix the render foundation before enabling frequent timers.

Modern terminals permit considerably more than ASCII. These are distinct layers:

| Technique | Proposal and boundary |
| --- | --- |
| Truecolor, background bands, Unicode rails/blocks/Braille | Core visual vocabulary; probe/fallback for actual capabilities and glyph width. |
| Changed-cell updates and synchronized output | Core rendering technique; avoid whole-screen flicker and unnecessary bytes. |
| OSC 8 links | Clickable file/reference targets with safe URI handling and ordinary visible text fallback. |
| Mouse hit regions | Click to expand tool/code blocks; preserve native selection via modifier/pass-through behavior qualified in Ghostty and phux. |
| Kitty graphics | Later optional inline images, diagrams and plots. Bound image bytes/lifetimes; qualify placement, scroll and deletion through multiplexers. No graphics requirement for readable chat. |
| OSC 66 text sizing | Experimental only after a positive capability result. Ghostty 1.3 notes parsing but no GUI implementation; do not base this proposal on enlarged text working there. |

Primary terminal guidance: [Ghostty synchronized output](https://ghostty.org/docs/help/synchronized-output),
[Ghostty 1.3 notes](https://ghostty.org/docs/install/release-notes/1-3-0),
[Kitty text sizing](https://sw.kovidgoyal.net/kitty/text-sizing-protocol/),
[Kitty graphics](https://sw.kovidgoyal.net/kitty/graphics-protocol/).
Capability checks get a short bounded deadline and must distinguish replies from
operator keystrokes. Terminal identity/environment alone does not prove support
through phux/SSH. Images and expanded typography are stretch features, not excuses
to postpone the strong baseline.

## Implementation shape

Small C++ presentation model: message ID, role/kind, source reference, revision,
typed blocks and live state. Runtime producers emit text deltas, tool lifecycle,
process output, notices, failures and observed usage separately. Presentation is
a bounded projection over retained source, never a second durable audit log.
Persist view preferences/anchors, not duplicate all original text.

Separate source text, Markdown structure, styled spans, wrapped rows and output
cells. Cache completed blocks by source revision/width/theme generation; replace
bounded caches rather than retain every width forever. Streaming updates revisit
the parse frontier and dependencies that can change earlier interpretation.
Blank lines alone do not guarantee Markdown immutability: later link definitions
can change earlier prose. Define supported syntax and dependency invalidation.

The viewport anchors to message/block/source offset. Expansion, wrapping and
incoming events preserve that anchor when reading history. Following the live
tail is explicit; unseen output gets a modest indicator. Copy/inspection use
source spans, not rendered ANSI strings. Unicode clusters, display cells and byte
offsets are separate concepts. Input stays authoritative during motion/streaming.

Compose a viewport-sized cell buffer and diff against the last presented buffer.
Invalidate old/new footprints on reflow or collapse so stale cells disappear.
Batch output bursts, synchronized when supported. Escape/control data in content
is never emitted as terminal control; style comes from trusted renderer tokens.
Synchronized-output mode must end on exceptions, interruption and editor handoff.

Lua supplies theme tokens and presentation options that can hot reload at the
normal boundary. Native layout/Unicode/cell machinery remains C++. We can later
expose richer custom block renderers without making that extension framework a
prerequisite for good conversation.

## Why this is grounded, and what to borrow

- **Codex:** source-backed message cells, activity groups whose call positions
  remain stable, width/theme-aware Markdown caches, time-driven shimmer. Study
  `history_cell/{messages,activity_group,markdown_render_cache}.rs` and `shimmer.rs`.
  Avoid copying cached-row clones or per-character animation allocations blindly.
- **Pi:** separate operator/assistant components and tool-specific call/result
  renderers; source-aware Markdown and bounded preview expansion. Study
  `packages/tui/src/components/markdown.ts` and coding-agent interactive components.
- **Oh-my-pi:** incremental Markdown tests compare each streaming prefix against
  a cold render while bypassing shared caches; tool-preview tests reject late
  batches after terminal results. Its render LRU counts keys as well as rows and
  has entry/aggregate bounds. These are direct useful oracle/performance patterns.
- **Crush:** independent section caches avoid invalidating prose when reasoning
  changes; source diffs and explicit hidden-line notices. Study
  `internal/ui/chat/{assistant,unified_diff}.go`.
- **FTXUI:** C++ easing functions and a cell canvas using 2×4 Braille dots and block
  glyphs show a native route to smooth-looking small graphics. Study
  `src/ftxui/{component/animation,dom/canvas}.cpp` and
  `examples/component/canvas_animated.cpp`. Acquired pinned source is recorded in
  `papers/2026-10-07-chat-native-reference.json`.

These are pattern/reference studies, not shipped foreign code. I recommend the
small C++ renderer described above; no library adoption in this proposal. FTXUI
would supply layout/widgets/canvas/animation, but would also introduce another
event/layout system to integrate with our composer, audit projection, and hot
configuration. Adopting it would require a separate concrete tradeoff discussion.

## Build order and acceptance

1. **Conversation identity and composition.** Typed events, styled role blocks,
   proper spacing, compact observed footer, useful error blocks. The greeting in
   the screenshot should become one compact exchange with no diagnostic dump.
2. **Incremental layout and motion.** Cached blocks, anchored viewport, changed
   cells, synchronized output, active indicators and settling effects. Ship motion
   here alongside the renderer, not after all features are finished.
3. **Rich content.** Markdown/code, tables, real diffs, compact expandable tool
   ribbons, exact source copying. Demonstrate an actual C++/Lua coding turn.
4. **Capability experiments.** Measured image/Braille visualization experiments;
   accept only those that improve real work. Variable typography remains optional.

Before each production unit, agree the intended screen and interaction, then
direct tests/code/review with at most two bounded remediation/recheck layers.
Keep accepted changes on main and rebuild locally. No autonomous campaign restart.

Direct oracles: semantic event/role boundaries; streaming-versus-cold layout with
shared caches bypassed; exact code copy; anchor stability through collapse/resize;
Unicode/control-byte handling; fake-clock phase/settled-idle silence; actual PTY
cursor/key/cancel behavior and Ghostty/phux inspection. Benchmark long histories,
high-output tools and partial Markdown for CPU, allocations and terminal bytes.

Provisional targets, to measure rather than declare achieved: no whole-history
parsing on token/animation ticks; settled idle produces no animation output;
input-to-display p95 below 30ms in a local 120×40 fixture; visual frame composition
p95 below 5ms; bounded memory independent of how long the audit has existed.
Report fixture/hardware and actual results, not blanket “fast” claims. No synthetic
timing budget blocks a visibly useful first composition slice.
