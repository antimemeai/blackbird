# Current styled TUI baseline delivery

2026-10-15. Implements the [bounded subplan](PLAN.md), development-only, without
product changes or dependencies. Root owns CMake registration and the integrated
C++/Lua/source checks. Microsoft tui-test qualification is an independent lane;
VHS was not selected.

## Delivered

- `tests/tui_snapshot_test.cpp`: deterministic native scenario producer using
  actual ChatView, ChatGrid, Composer, terminal text layout and task-row renderer.
- `tests/snapshots/tui/check.py`: standard-library comparison, readable unified
  failure diffs and actual files, explicit `--update CASE` / `--update-all` only.
- `tests/snapshots/tui/expected/`: eleven captured current expectations, inspected
  as styled rows, dimensions, state, cursor and wide-cell geometry.
- `tests/snapshots/tui/README.md`: format, invocation, update workflow, source
  pattern and scope/limits. CTest name is `tui_snapshots`.

The owned encoding follows the useful Neovim Screen pattern of readable
attribute-annotated rows and explicit cursor. The pinned original Screen source
was read, including its asynchronous eventual-state semantics. These current
public rendering seams are synchronous, so this slice has no arbitrary sleep or
eventual matcher. Future async scenarios need their actual state transition
oracle, rather than waiting for quiet output.

| Fixture family | Fixed inputs and meaningful retained observations |
| --- | --- |
| Conversation ordinary/narrow | Typed user/assistant/tool/error blocks, Markdown heading/inline code, C++ keyword/string styling and actual narrow wrapping. |
| Composer multiline | Real native up-arrow sequence across a short middle line; byte cursor 4 and rendered cursor row0/column6. |
| Composer literal paste | Bracketed paste retains slash, literal tab, newline, wide and combining text without executing completion; original draft bytes remain in state. |
| Composer Unicode | Native movement over wide/combining text; byte cursor7, rendered cursor row0/column6, leader width2 and continuation width0. |
| Tasks ordinary/narrow/tiny | Actual status marks, owner/blocker/rollup, independent running activity, wrapping/clipping, wide continuation and existing empty rendering below width8. |
| Palette selected/no match/return | Fixed custom session catalog, selected Beta, bounded help, empty search result and preserved hidden draft. A direct semantic assertion requires the actual selected command path and choose-command action. |

Names refer to Ink roles, not invented RGB values or inferred attributes. Every
cell's role, scalar/cluster bytes and non-default width participate; cursor and
selected semantic state are separate explicit fields. Storage arena offsets are
not presentation and are intentionally excluded. No clocks, provider requests,
account state or changing filesystem paths enter the fixtures.

## Direct verification

1. Built `tui_snapshot_test` in the actual debug profile. Its added target caused
   regeneration and a broad existing-source dependency rebuild; no app executable
   was relinked by this lane. The producer linked successfully.
2. Ran comparison before expectations existed: all eleven failed and the normal
   command created no baseline directory. Actual/diff files were written under
   `build/debug/tui-snapshots-initial/`.
3. Explicitly captured all eleven with `--update-all`, then inspected every
   expected screen. `ctest --test-dir build/debug -R '^tui_snapshots$'
   --output-on-failure` passed, 1/1, covering all eleven comparisons and native
   semantic assertions.
4. Temporarily changed one expected Unicode screen's text, Ink alias, cursor and
   wide-cell metadata. Its normal comparison failed with all changes visible in
   `build/debug/tui-snapshots-mismatch/composer-unicode.diff` and an actual file.
   The runner left the changed expectation untouched. Restored the original
   expected bytes exactly. This was one direct qualification of the new mechanism,
   not an additional recurring test framework.
5. Exact-profile syntax check, changed-file clang-tidy and clang-format checks
   pass. Initial compiler signedness and analyzer geometry findings were fixed;
   the focused changed-file recheck is clean. Python parses successfully.

Native assertions test real selected command identity, hidden-draft survival,
literal pasted text and multiline cursor, alongside visual regression. No fixture
contacted a model or dispatched production tool/domain work. Logs for
the initial missing-baseline and direct mismatch runs are retained in ignored
`context/tui-implementation-2026-10-15/snapshots/`.

## Current imperfections preserved

The narrower conversation splits a string literal across lines. At44 columns
the palette help is clipped before its final Escape wording. Task summary text
wraps even at36 columns; narrower row/detail bounds omit parts of long metadata,
and below8 columns task presentation emits nothing. These are captured current
behavior, not newly chosen UX or assertions that the design is correct. This
lane made no production fix to obtain a prettier baseline.

Composer and palette grids are explicitly test-local component compositions,
not captures of the private complete TerminalUI window. Palette style splitting
mirrors the present production branch until extraction supplies a public
production composition seam. These tests cannot independently detect a change
made only to that private branch, actual theme RGB mapping, terminal font/GPU
rendering, image behavior, physical device input, transport modes or external
effect correctness. Existing VT/PTY checks and the selected Microsoft tool attack
those other classes. Linux/alternate-locale execution was not performed by this
lane; the program selects a supported UTF-8 locale or fails explicitly.
