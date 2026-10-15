# Terminal regression tools

2026-10-15. The operator adopts Microsoft tui-test and Neovim-style screen
expectations together, alongside ordinary semantic/fault/profile qualification.
Stable presentation, including understood imperfections, is a useful baseline.
No single test family supplies the whole result.

## Microsoft driver

Microsoft tui-test is a development executable; no terminal framework or foreign
runtime enters production. [The profile](tui-test-profile.json) pins upstream
release0.1.0, source revision `bcc41b8b11cbcc0290b07d01aada636b1df707a0`, archive
lengths and publisher SHA-256 digests. Install explicitly:

```sh
scripts/tui-test --install
scripts/tui-test --check
scripts/check-tui-microsoft build/debug/blackbird --artifacts build/debug/tui-microsoft
scripts/check-tui-microsoft build/debug/blackbird --backend alacritty --artifacts build/debug/tui-microsoft
```

Original release archives remain intact in ignored quarantine; the executable
lives in ignored build/tools. Setup checks the archive, extracts only the named
regular executable without importing archive paths, and records its installed
byte identity bound to the selected release/source/archive pin. A changed profile
invalidates an older installed cache even when the version string stays the same. The launcher detects subsequent local corruption and checks its
reported version on `--check`. Tests never download missing tools. A clean host
must run installation first; missing tooling fails rather than silently skips.
Linux source qualification explicitly provisions the same pinned tool before
configuration when its selected tests require it.

The registered `tui_microsoft` case runs the actual launcher through Ghostty's VT
engine with isolated local workflow/session and no provider request. It checks
styled palette identity, keyboard selection, literal control-containing paste,
physical PTY resize/cursor bounds, one deliberately submitted native workflow
effect and exit. It keeps native Microsoft failure traces and selected final
state/SVG captures under its build artifact directory. Session names are unique
and only that session is closed. Signal/body watchdog handling reserves cleanup
time before CTest termination; command traces survive primary and cleanup failures.
The single-effect count is read after observed exit and closure. Target arguments follow an explicit `--` so
Blackbird's `--session` cannot override the driver's own session selection.

The Alacritty profile is also directly exercised in adoption qualification.
A VT engine plus generated SVG does not qualify the Ghostty GUI/font/GPU/physical
keyboard stack. Actual Ghostty/phux compatibility remains a separate profile.
Neither output quiet nor a moving animation is the completion/control oracle.

## Readable owned screen expectations

The registered `tui_snapshots` case produces deterministic component grids and
compares [reviewed baselines](../tests/snapshots/tui/README.md). Their readable
rows, named style aliases, cursor, byte-preserving state and wide-cell markers
follow the useful Neovim pattern without adopting its RPC/framework dependencies.
Fixtures exercise actual public conversation/composer/palette/task rendering
seams. They do not claim full-window GUI capture. Current Ink roles are preserved;
Microsoft assertions independently exercise resolved terminal colors/styles.

Ordinary tests never update goldens. Use the explicit update command documented
with the fixtures, inspect its diff and retain known imperfections deliberately.
Failures produce expected/actual differences and observed snapshots. Semantic
assertions remain alongside snapshots, native scene/collection scenarios, the
existing independent VT-subset oracle and real PTY/custody tests. Sanitizers,
source analysis, OS qualification and the existing bounded review rules remain.

See [the options assessment](../papers/tui-regression-2026-10-15/OPTIONS.md) for
fault-class boundaries and the [campaign](../docs/TUI_IMPLEMENTATION_PLAN.md)
for component/application scope. VHS was considered and is not selected.
