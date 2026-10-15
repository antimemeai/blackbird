# Microsoft tui-test adoption and terminal regressions

2026-10-15. Explicit operator selection: Microsoft tui-test plus Neovim's readable
screen-expectation approach alongside usual rigor. The external tool is development
only; C++/Lua remain the product stack. Pin release0.1.0 and publisher archive
identity, retain original bytes and install only its executable under ignored
build/tools. Provide a reproducible owned setup/launcher with explicit installation
and integrity checks. No command downloads tools implicitly from a test run.

Add a real Blackbird scenario through Microsoft CLI: isolated session/workflow,
no provider calls, explicit readiness, palette/input/draft/resize state, styled
cell/cursor inspection and actual Lua effect marker when deliberately submitted.
Use original bytes/raw input for fragmented-key/paste behavior rather than claiming
a high-level key simulation tests physical keyboard encoding. A test failure
retains tool failure artifacts and cleans up its own session/child.

Red evidence: the new registered fixture/tool paths initially do not exist.
Green acceptance: pinned binary identity/version, actual expected cells/cursor,
keyboard/paste/resize behavior and exact fixture result, plus successful cleanup.
Test normal immutable baselines and one explicit alternate output/candidate where
needed to observe a meaningful failure. Do not build tests to certify snapshot
tests. A small fake target can qualify external assertions directly; ordinary
regression scenarios then exercise Blackbird.

Root integrates one code challenge of meaningful boundaries then fixes, one direct
recheck/fixes, required Debug/lint and affected optimized/sanitizer checks.
No new eval/model/hardware/fleet mutation campaign, and no GUI/font claim from
the emulator backend. Baselines intentionally preserve the current presentation.
