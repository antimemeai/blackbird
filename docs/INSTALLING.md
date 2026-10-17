# Install and update Blackbird

From this configured checkout:

```sh
./scripts/install-blackbird
blackbird --help
blackbird auth providers
blackbird
```

This host already includes `~/.local/bin` on `PATH`. Another host can select a
prefix with `--prefix DIRECTORY` and add its `bin` directory to `PATH`. The script
prints the installed command location; it never edits shell configuration.
`CMAKE=/absolute/path/to/cmake` selects the CMake executable. The repository's
`release` preset still supplies the qualified compiler and Lua requirements;
the script does not install a compiler, libraries or other dependencies.

To build and activate the current local source again:

```sh
./scripts/install-blackbird --update
```

`--source CHECKOUT` chooses another configured local checkout; `--jobs N` controls
build parallelism (default 6). Update never fetches, resets, stashes or checks out
Git changes. Local edits are included in the build. Run source checks before
installing a changed checkout; the installer's offline smoke checks exercise
packaging, rather than replacing the development suite.

Each installation builds production executables directly into a fresh generation
under `~/.local/lib/blackbird/releases/`, alongside copied Lua programs and assets.
This preserves compiled absolute helper paths and removes the running program's
need for the source checkout's files. After help, isolated provider-catalog and a
local Lua/PTY-helper smoke succeed, the script atomically switches `current`.
A failed build or smoke preserves the previous `current` generation. An unrelated
existing `bin/blackbird` is refused. Installations on the same prefix are serialized
with `lib/blackbird/install.lock`; remove an abandoned lock only after its installer
has exited.

The launcher preserves the working directory, exact arguments, and existing
executable or sprite environment overrides. It chooses no provider/model, reads
no other application's account cache, and changes no authentication settings.
Use the existing `blackbird auth ...` commands separately to select accounts.
Runtime state continues under `~/.local/state/blackbird`; account storage keeps
its existing location. The smoke uses its own temporary auth directory and makes
no provider request.

An invocation pins its generation for its entire lifetime, including `/restart`.
A new `blackbird` invocation uses the newly installed native program. Reopening
with `blackbird --session DIRECTORY` preserves that session's saved absolute
workflow path, which can still select the old generation's Lua. The installer
retains older generations; do not remove one used by a process or saved workflow.
To deliberately select the new packaged default for an existing conversation:

```sh
blackbird_release=$(CDPATH= cd -- "$HOME/.local/lib/blackbird/current" && pwd -P)
blackbird --session DIRECTORY --workflow "$blackbird_release/share/blackbird/programs/turn.lua"
```

Use the corresponding prefix when installing elsewhere. Resolving `current`
first pins the selected Lua to that generation. Keep an explicitly authored
workflow selected when that is the desired behavior.

Generation build trees are intentionally retained with the executables: the
helper paths were compiled at those fixed paths. This is a host installation,
rather than a relocatable binary distribution. macOS and Linux use their native
atomic symlink-replacement flags; other platforms are refused.
