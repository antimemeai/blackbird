# Current styled TUI snapshots

2026-10-15. Authorized implementation lane, development tests only. Own
`tests/tui_snapshot_test.cpp` and `tests/snapshots/tui/`; root owns CMake and
integration. No production code or library adoption in this lane.

Purpose: retain deterministic visual baselines of existing conversation rows,
composer editing/cursor, task presentation and palette choices before component
extraction. Follow the useful Neovim Screen pattern: readable styled rows with
stable attribute aliases and an explicit cursor, rather than raw ANSI goldens.
These record current behavior including imperfections; they do not establish
design correctness, GUI font appearance or full-terminal rendering.

Use the actual `ChatView`, `ChatGrid`, `Composer`, `terminal_lines` and
`task_pane_rows` seams. Fixed native values, geometry and byte input avoid model,
filesystem, clock and account dependencies. Composer fixtures exercise actual
editing/palette state; their component grid is explicitly a fixture composition,
not a capture of the private complete `TerminalUI` layout.

The snapshot encoding includes every glyph/cluster, Ink identity, cell width and
continuation, fixed dimensions, cursor and selected semantic state. A normal run
compares expected files and writes actual/diff artifacts only on failure. Updating
an explicitly named baseline is a separate command; never silently create or
replace an expectation during a normal test.

Initial cases: ordinary/narrow conversation with code/tool/error rows; composer
multiline editing, literal bracketed paste, wide/combining text and cursor;
task states/rollups/blocker with normal/narrow widths; deterministic custom-choice
palette including selected row, no-match and hidden-draft return. Direct semantic
assertions protect selected commands and unsent drafts independently of their
visual baseline.

Verification: build and run the new target; use deliberate temporary expected-file
changes to show text/style/cursor/width changes fail with readable artifacts and
leave baselines intact, then restore them. This is a direct check of the actual
regression mechanism, not a separate test framework. Run changed-file native
diagnostics. Root retains usual affected C++/Lua checks and external Microsoft
tui-test qualification; VHS is not selected. Async eventual-match belongs to
future async component scenarios; these current pure seams have no asynchronous
settlement to wait for.
