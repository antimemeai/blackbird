# Install/update direct results — 2026-10-17

RED: `scripts/check-install-blackbird` failed with the explicit oracle
`install/update entry point is missing` before implementation.

GREEN: the same direct filesystem transaction check passes (5.7 seconds). Its
fake CMake boundary supplies executable fixtures while the actual installer,
actual copied launcher, filesystem and native shell quoting execute unchanged.
Checked exact cwd/argv preservation with a prefix containing spaces and a quote;
packaged Lua/instrument/sprite files; generation-specific build and resource paths;
successful update retaining its predecessor; failed build, missing PTY helper and
failed native smoke preserving the old pointer; concurrent lock refusal; and
refusal to overwrite an operator launcher. `sh -n scripts/install-blackbird` passes.
The test is registered as CTest `install_blackbird`.

The production installer itself runs actual native `--help`, auth providers with
isolated empty auth home, and a local Lua `exec` with `pty=true`. Its success marker
is emitted only after an independently known zero child exit, exercising recorder
startup and the compiled PTY runner without model traffic. These real smoke
checks will run when the root installs the combined qualified source. No live
host prefix, shell configuration or account was touched by the delegated unit.

Design consequence: retain installed fixed generations, and pin each invocation,
including its existing restart loop. Updates activate on a new invocation; reopening
the saved session preserves its selected workflow, including paths into older
generations. Installation does not silently migrate those selections.

One bounded root review found a first-install rollback gap between launcher
publication and final current-pointer rename. Direct injected final-switch RED
fails with `failed first switch left a broken launcher`. Remediation retains the
prior launcher bytes until activation, restores them on update failure and removes
the new launcher on first-install failure; signal cleanup recognizes an already
activated current pointer. Extended direct GREEN (~10 seconds) checks both initial
and update final-rename failure, unchanged prior launcher bytes/current, all
production executables including blackbird-candidate, and the prior scenarios.
Clarified that a reopened session retains its absolute old Lua path and documented
explicit selection of the physical current generation's packaged workflow. No
second independent review layer is opened.

Root's combined source `9e9dad5` passes the full C++23 source gate:263/263 Mac
Debug tests and all owned C++/Lua checks. Scoped Linux Release and ASan+UBSan also
include the installer transaction case. Actual host installation then succeeds:
`~/.local/bin/blackbird` selects
`~/.local/lib/blackbird/releases/generation-z8hyFGNP`. All production executables
are built at their final paths. The real native help, isolated auth catalog and
Lua/PTY-helper smoke pass before activation. Through the installed PATH command,
help and auth providers pass from `/tmp`; real auth status still has only the
existing ready Paper account. No shell configuration, model selection, other
application's credentials or billing route changes. Logs:
`context/claude-main-2026-10-17/host-install.log` and `host-path-*.log`.

One generation was installed on the actual host. Update retention and rollback
were checked by the existing filesystem transaction fixture; no redundant second
production rebuild was run. `arconaut-5c4` is complete. Live Claude authorization
remains separate and unresolved.

At the operator's subsequent request, master fast-forwarded to `acfe0f9` and was
pushed, including installed source `9e9dad5`. The active host generation remains
the same qualified build; integration introduces no native/runtime change.
