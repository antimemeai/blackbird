# Current styled screen expectations

These are deterministic component baselines of Blackbird's present behavior,
including imperfections. They use the owned `ChatView`, `ChatGrid`, `Composer`
and task-row renderer. Composer/palette grids are fixture compositions at these
public seams; they do not claim to capture the private whole-window `TerminalUI`
layout, actual GUI fonts/images, terminal negotiation or external effects.

The readable styled rows follow
[Neovim's Screen-expectation pattern](https://github.com/neovim/neovim/blob/3068a8f652c8e83aac0696b4a513f369b5bbbd99/test/functional/ui/screen.lua),
implemented here with owned code and no Neovim dependency. Named aliases refer
to exact native `Ink` roles; runs include spaces and padding. `cursor` is the
zero-based row/column, or `none` for a non-focused fixture. Cursor visibility
here describes the fixture, not a captured terminal mode.

Each `{ink:text}` run records the glyph/complex-cluster bytes and role of every
cell. Backslash, braces, row delimiters and control bytes are escaped. A
`<continuation: >` marker represents the actual following wide-glyph cell with
its own Ink; it is not printed UI text. `cell-widths` lists every non-default
cell width by `row:column=width`, including width-zero continuations. This keeps
wide-cell geometry observable rather than flattening it into a plain string.
Arena offsets are storage details and are not baselined; actual cluster bytes
are. `state` records the chosen draft/cursor/palette facts in addition to the
visual grid.

Run comparisons from the repository root after building the target:

```sh
python3 tests/snapshots/tui/check.py build/debug/tui_snapshot_test \
  --artifacts build/debug/tui-snapshots
```

A normal run never creates or replaces expectations. Missing or removed cases
fail. A mismatch emits `.actual.snap` and a unified `.diff` under the selected
artifact directory; expected rows, styles, cursor and widths remain untouched.
Use `--case composer-unicode` to compare one case. Independent native assertions
also check the selected command, literal pasted text and hidden-draft survival.

Update a baseline deliberately after deciding that the changed behavior belongs
in the baseline:

```sh
python3 tests/snapshots/tui/check.py build/debug/tui_snapshot_test \
  --artifacts build/debug/tui-snapshots --update composer-unicode
```

`--update-all` explicitly captures every emitted fixture. It is suitable for an
intentional first capture or a reviewed broad change, never normal test execution.
Review the source-controlled diff. No environment variable or missing-baseline
fallback silently approves a new expectation. Delete retired expectations
deliberately as a source change; the normal comparator flags their absence.

Initial fixtures cover ordinary/narrow conversation (Markdown, code, tool and
error), multiline/paste/Unicode composer, ordinary/narrow/tiny tasks, selected
palette, no matches, and return to the unsent draft. Their fixed custom choice
catalog avoids failures caused solely by unrelated built-in command additions.
Palette style composition mirrors the present TerminalUI branch; extraction
should eventually expose that production seam directly instead of keeping a
test-local composition.

The native tests are synchronous. Eventual matching would add no evidence here;
future asynchronous component scenarios must wait for their actual promised
state and retain transition/timeout evidence. Microsoft tui-test qualification,
existing real-PTY/mode/effect checks and the independent VT oracle attack other
fault classes. A passing snapshot does not establish the desired design is good.

`conversation-plain-joiners` specifies ordinary U+200D text as separate spacing
cells, retaining joiner bytes in tails. It was added after both the renderer and
the original VT-subset oracle incorrectly collapsed plain joined spaces/letters.
The production105×52 whole-window case in the Microsoft driver independently
checks divider/gutter cells in both Ghostty and Alacritty qualification; this
component snapshot alone cannot establish real terminal width behavior.

`center-wide` and `center-narrow-stale` use the actual current-session
CommandCenter and a real TaskState. They specify the shared controls' composed
coverage, selected task and blocked detail, including narrow stale last-good
content. They preserve all16 earlier expectations. Actual-main source identity,
participant/command behavior and the sole editor are checked separately.

`instrument-lua-wide` and `instrument-lua-narrow` load the actual authored
`programs/instruments/results.lua` through `Script` and the bounded asynchronous
`PresentationOwner`. Two synthetic rows and original pinned byte windows have
stable fixture-only IDs; ESC and embedded NUL must render as literal escaped
bytes, while tab uses the shared terminal sanitizer's two spaces. The returned
Lua candidate is adopted through `lua::publish` into a real `SceneHost`, then
rendered with the explicit independent `CollectionInspector` binding. Wide keeps
collection focus with B selected and A anchored; narrow keeps B selected and focuses the
comparison. Both original full IDs and source suffixes are asserted in the
rendered grid independently of the baseline text. Compose accepts no action and
the actual native inbox must remain empty. The worker's real completion is
awaited within a finite five-second allowance.

These two cases cover authored composition, native scene adoption and readable
styled rendering. Their scope is synthetic retained windows: they do not claim
native command-query capture, actual source reload, signal delivery, raw-input
provenance, the sole Composer or the physical whole-window layout. Those have
separate actual-main/native owner checks. The existing18 expectations are
unchanged. Physical terminal gutter residue after resize is preserved separately
in the instrument episode's paper fixtures; a freshly reset native grid cannot
establish that a real terminal erased its previous cells.
