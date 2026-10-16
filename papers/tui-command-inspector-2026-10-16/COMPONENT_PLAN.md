# Native collection and inspector component

This unit makes the existing checked SceneHost paintable and controllable for one
bounded collection plus read-only inspector. The host remains the sole collection,
selection, anchor and focus owner. The component emits no terminal bytes, performs
no domain call, owns no second item store and never edits Composer or its draft.

Grounding: TUI_DOCTRINE rules1/2/5/8/9/11/12; TUI_COMPONENTS native ownership,
virtual collection and checked rectangle sections; delivery Units1–2; existing
tui-next-components SOURCE_MAP. The acquired Ito list at08c6a5ac8bdf, especially
`controls/list.lua` identities/arrange/handle and `runtime/composition.lua` keyed
placement, supplies the useful identity/focus pattern and exposes full-list
arrangement cost. This implementation renders only the supplied bounded window;
it does not call a provider or claim that lazy drawing bounds a domain query.
Sources/restoration remain in the existing uji manifest; no code is adopted.

## Data and ownership

SceneCandidate/Scene gain trailing inspector properties: node, title, optional
TargetRef, styled read-only logical lines, explicit Notice, and source content
offset. Collection gains trailing title/Notice. Existing aggregate callers keep
their defaults. Inspector source offset identifies a different supplied byte page;
same target+offset refresh preserves local reading position, while a changed target
or page resets it. Presentation references remain locators, never effect authority.

Publication validates node kind/duplicate inspector identity, target identities,
notice/ink enums, terminal-safe UTF8/no controls, and all owned counts/text before
copying and atomically swapping. Detail logical rows and style spans share the
existing items budget with collection rows, so empty spans cannot evade structural
bounds. Every inspector/reference/title/notice/style string uses the existing byte
budget. Existing inspector-kind nodes without props remain legal for old callers;
the concrete component requires its configured inspector properties.

Root sanitizes raw adapter text once on page adoption, using the existing shared
terminal sanitizer wrapper. The native candidate rejects an unsafe independent
caller rather than trusting that boundary. Rendering consumes accepted safe rows
through DisplayCursor, preserving style across combining/emoji boundaries and
using actual fallback footprints. No terminal.hpp or command owner dependency.

## Controls and geometry

CollectionInspector borrows the stable SceneHost address and checked collection/
inspector NodeHandles. Semantic previous/next/first/last/page commands operate on
the actual focused control. Collection movement uses one atomic host position
update for selected key and first-visible anchor, clearing any armed target when
selection changes. Inspector movement changes only a local visual-line offset.
Focus collection/detail/toggle uses host focus; narrow detail returns via that
same focus action. The root maps existing decoded input and owns leave/return.

At72columns and four rows, use adjacent list/detail: list widthclamp(width/3,24,36),
one divider cell, detail remainder. Otherwise render only the focused list/detail.
Empty/tiny rectangles clip without unsigned overflow. A rectangle that bisects an
existing wide glyph refuses before painting, preventing ChatGrid footprint cleanup
from changing a neighboring owner. Within a valid rectangle every clear, header,
notice, row and divider is clipped; one-cell rows use visible replacement fallback.
Selection marker and focused header provide non-color counterparts.

Render may cache viewport geometry and one line-height prefix index for the current
immutable scene+detail width. It never mutates host/domain state. Only supplied
detail rows are measured on adoption/width change; visible rows are painted with
the shared cursor and no whole-history copy. The cache retains one immutable scene
owner, not a chain of prior pages. Rendering allocation failure cannot publish a
frame; the root must abandon its uncommitted target, retaining the prior presenter.

## Red, implementation and one recheck

First add API scaffolding/stubs and permanent semantic/literal cases. Stub output
must fail named cell/selection/scroll oracles, and the initial unvalidated property
scaffold must fail unsafe/overbudget/duplicate publication oracles. Preserve the
exact red log before implementing behavior. Cases cover checked target focus,
stable selection/anchor under reorder/removal, immutable detail ownership, rejected
candidate preserving last-good props/state, clipped wide/narrow/tiny grids,
cross-style clusters, one-cell replacement, same-page reading continuity and
different-page reset. Literal cells do not derive expected positions from renderer
measurement. Existing scene tests and presentation baselines remain intact.

Implement the owning mechanisms, rerun the same direct cases and affected existing
scene checks, then root performs one concrete code/oracle challenge. Resolve actual
findings in that second layer. Root owns CMake registration, command adapter,
TerminalUI/input integration, actual-main physical/readable cases, Linux/sanitizers,
matched performance, full installed source gate and publication. No extra reviewer,
framework, AFL, provider/live session, artwork or main activation scope.

## Static-analysis findings from the installed source gate

The full native suite passed206/206; analysis then identified swappable width/
row-index and x/width scalar parameters, repeated optional retrieval not locally
proven to the checker, and two fixture constructions. Use existing CellCount in
the private width helpers and existing Rect for row placement. Bind selected once
from stable owned state before guarding/dereferencing. Construct the invalid
target explicitly and create invalid Ink byte255 with C++20 bit_cast, preserving
the original publication-refusal oracles. No suppression, public API or behavior
change is needed. Rebuild the owning two targets, run the same component cases and
analyze their two translation units before the root reruns the source gate.
