# Microsoft terminal-driver adoption

2026-10-15. Explicit operator adoption, with owned Neovim-style expectations and
usual native/fault/profile rigor. This unit supplies a development tool and
real-executable fixtures, not a production framework.

Pinned Microsoft release0.1.0 resolves tag source to
`bcc41b8b11cbcc0290b07d01aada636b1df707a0`. The Darwin-arm64 original archive is
6,298,768 bytes, SHA-256
`88e71dae1ebdead25f1a7d8a2618e39868a2fa8a4b14630d4c4cfa446af6caeb`.
The installed executable reports `tui-test 0.1.0`, byte SHA-256
`338f3c17be7241a6dc93cadf072c2d6a35049d8d8be13b10776034f12e6690a0`.
Original release metadata/archive copies and restoration commands remain in
[MANIFEST.tsv](MANIFEST.tsv); platform pins live in the tracked profile.

The owned installer downloads only by explicit `--install`, verifies archive
length/hash, extracts only the named regular executable without importing member
paths and places it under ignored build/tools. The launcher checks its installed
byte identity bound to the selected version/source/archive pin; same-version
pin changes invalidate stale installations. New-host source gates require the tool instead of silently skipping
its fixture. Linux provisioning is explicit before its selected source tests.

Direct local tool qualification accepts independently known `KNOWN_CELL` and
rejects `WRONG_CELL` with status1 and a structured assertion artifact. This is one
adoption diagnostic, not a second recurring framework to test our tests.
The actual Blackbird fixture passes through both Ghostty and Alacritty VT engines:

- Selected palette name, exact selected color/bold cells and next item identity.
- Actual keyboard fill, literal bracketed paste containing a cancellation byte,
  and no workflow dispatch merely from typing or paste.
- Native PTY/emulator resize and cursor bounds at32×8, then restored draft at80×24.
- One explicit submitted local workflow produces exactly one independently read
  file line through the ordinary audited native exec owner; no provider request.
- Actual exit and cleanup of only the fixture's unique terminal session.

Tool option qualification found that an unseparated target `--session` is consumed
as the driver's global option. The fixture now passes an explicit `--` before
the target and checks the returned driver's session identity. Initial fixture
code also assumed `io.open` existed in workflow Lua; Blackbird deliberately exposes
selected libraries, so the final fixture uses the supported native exec owner.
These failures were visible, not accepted as passing evidence. The final fixture
keeps target arguments as argv and its shell effect path is a quoted positional
parameter, not command interpolation.

Passing run artifacts contain final state/SVG and the command transcript under
build/debug/tui-microsoft. Failing runs retain the driver's text/SVG/HTML/timeline
artifacts for diagnosis. They contain only the controlled fixture's data.
Generated images and terminal engines do not establish physical GUI/font/GPU,
keyboard/device behavior or input-to-photon latency. Broader component migration
and the full Lua/command-center foundation remain subsequent source work.

Independent challenge and full source/platform results are recorded by the root
in the campaign review resolution and qualification record. This report's direct
passes do not stand in for that source gate.

Integrated review fixes: the sole exactly-one-effect assertion now occurs after
observed exit and driver closure, so a delayed duplicate cannot pass. Each CLI
call records exceptions as well as ordinary replies; nested finalization writes
commands.json even if close fails and preserves the primary error separately.
SIGTERM/SIGINT and a body watchdog route through bounded cleanup before CTest's
outer guard. Actual interrupted-body qualification and stale-profile receipt
refusal are rechecked by the root; uncatchable SIGKILL still cannot guarantee
user-space cleanup. The unique session reference is retained for such diagnosis.
