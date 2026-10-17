# Host install/update unit — 2026-10-17

Purpose: make `blackbird` callable from any working directory while preserving the
operator's source, session defaults, credentials and already-running processes.
Updating installs the explicitly selected local source; it never fetches, resets,
checks out or executes provider login.

Source grounding: `scripts/blackbird` owns fresh-session and restart semantics.
CMake currently embeds absolute default workflow/sprite, PTY job runner and
archive worker paths; `src/main.cpp` derives the results instrument from the
compiled workflow parent. Raw recorder/supervisor processes fork from the native
program and require no separate executable. Copying only the main binary would
retain dependencies on a mutable checkout/build.

Use owned fixed generation directories beneath `PREFIX/lib/blackbird/releases`.
Copy tracked owned programs/assets and launcher into each generation, build the
production targets directly at their final paths, and provide CMake with the
packaged resource root. Existing checkout builds retain their current defaults.
Only after offline help/auth-catalog/local-workflow smoke succeeds, atomically
replace `current`. The prefix launcher resolves the physical generation once,
then runs its launcher preserving caller working directory and arguments. Retain
older generations for running processes and saved absolute workflow paths.

The installer supports `--prefix`, `--source`, `--jobs`, and explicit `--update`
(repeating installation from that local source); default prefix is ~/.local.
It refuses to overwrite an unrelated `bin/blackbird`, serializes installations
with an owned mkdir lock, and removes only its own incomplete generation on
failure. No shell configuration, account or model mutation. No dependencies are
adopted or installed: use the selected CMake preset's qualified compiler/Lua.

Direct RED first: development test invokes the absent installer, then exercises
real filesystem transactions with a fake build boundary. Oracles check preservation
of caller cwd/arguments, selected generation, packaged resources, helper absolute
paths, idempotent updates, failed-build rollback, unrelated-launcher protection,
and no developer UI binary or credential fallback. GREEN runs that direct test;
the root additionally builds and smoke-tests a real isolated generation after the
combined source gate. One independent review can attack transaction boundaries;
fix findings once without opening a new assurance layer.
