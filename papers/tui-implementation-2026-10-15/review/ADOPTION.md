# Independent challenge: Microsoft terminal-driver adoption

2026-10-15. One bounded code challenge of `scripts/tui-test`,
`scripts/check-tui-microsoft`, `tooling/tui-test-profile.json`, their CMake
registration and `scripts/check-linux` changes. This does not review the owned
snapshot lane, other reviews, or the root's subsequent fixes. Root owns the
single integrated finding-focused recheck.

## P2 — Exactly-once is checked before the scenario finishes

**Location:** `scripts/check-tui-microsoft`, lines 88–95 in the reviewed source.

The checker reads the effect count as soon as the first `MICROSOFT_EFFECT`
appears, then takes state, captures a screenshot, submits exit and waits for
exit. A delayed duplicate during those later steps is never counted. The test
can print “exactly one workflow effect and exit pass” with two effects.

**Direct counterexample:** ran the checker against a small independently scripted
driver response function, without launching the real tool or any account. The
explicit Enter produced one `ran` line; `wait exit` produced a second. The
checker returned success with two lines after exit. The capture is retained at
`context/tui-implementation-2026-10-15/adoption-counterexample.txt`. This proves
an oracle window, not a claim that the real Blackbird currently duplicates work.

**Direct fix:** place the single exact effect-count assertion after observed
target exit and appropriate owner closure, while the temporary fixture still
exists. First-marker recognition can remain a progress observation. Do not use
screen quiet as settlement. Include this delayed-duplicate case in the root's
one finding-focused recheck.

## P2 — Cleanup failure discards the owned command trace and masks the primary failure

**Location:** `scripts/check-tui-microsoft`, `call` lines 27–35 and `finally`
lines 96–100; CMake `tui_microsoft` timeout registration.

A timeout in `subprocess.run` is not appended to `history`. If `close` times out,
returns malformed JSON, or returns `ok=false`, the `finally` block throws before
writing `commands.json`. A primary style/input/effect assertion can then be
replaced by the cleanup exception, with the owned trace never written. This
loses precisely the evidence needed to diagnose the failed session and its
remaining resource state.

There is also no SIGTERM-to-cleanup path even though CTest imposes a 60-second
outer timeout and the tool documents persistent CLI sessions. The current Python
`finally` is not itself a cleanup contract for abrupt external termination.
This review does not claim to have demonstrated an actual leaked daemon under
CTest's platform-specific process termination.

**Direct fix:** append command intent before dispatch, retain timeout/parse/exit
errors, and write history in an unconditional nested `finally`. Preserve the
primary failure and report cleanup separately. Give controlled outer termination
a bounded cleanup path for this exact unique session, or explicitly own it in
a supervisor that outlives the timed scenario. Do not close all sessions. The
root's recheck should cause a failed/timeout close and confirm both diagnostics
and trace survive; it should state the actual outer-timeout cleanup limit.

## P2 — Cached installation is not tied to the selected profile's artifact pin

**Location:** `scripts/tui-test`, `paths` lines 29–32, installation receipt write
line 66, launch/check lines 74–86; `qualified_local_binary_sha256` in
`tooling/tui-test-profile.json`.

Installation validates the selected archive's byte count and SHA-256 correctly.
Subsequent launches check the executable only against its neighboring generated
receipt. That receipt contains no selected archive identity, and the profile's
qualified executable hash is unused. The cache path contains version/platform
but not the artifact pin.

Consequently, changing the profile's pinned artifact for the same version leaves
an old cached executable plus old receipt valid. `--check` also accepts it when
its version string is unchanged. This is a concrete profile/cache invalidation
gap, independent of any claim about hostile same-user tampering.

**Direct fix:** bind the installed receipt to the selected profile archive SHA
and validate that relation before checking the installed executable, or pin an
expected executable hash for each supported asset and verify it directly.
Require explicit reinstallation when the current profile and installed identity
differ. A version banner alone is insufficient for a same-version artifact
change. The root's recheck can change only a temporary profile pin and require
rejection of the stale cache; no real installation or account change is needed.

## Other inspected boundaries

Unique session IDs, explicit driver `--session`, the target-argument `--`
terminator, temporary Blackbird session/workflow, explicit empty tool config and
absence of broad `close --all` give sound separation for the ordinary path.
The fixture uses a local custom Lua workflow and an independently counted local
effect rather than paid inference or a screenshot's implied success. Archive
ingestion reads one exact regular-member executable without extracting arbitrary
archive paths.

CMake makes the adopted driver required rather than silently skipping it;
Linux provisioning is explicit before relevant CTest execution. Source transfer
includes the scripts/profile and the entire tests tree, including styled
expectations. No further substantive finding was identified in those integration
changes during this bounded challenge. No reference code, real tool session,
network provisioning or account was executed by this review.
