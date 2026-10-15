# Fresh startup, 2026-10-15

Operator rejects automatic global-session resume after a Paper launch selected
an old 356 MiB Arconaut conversation and refused recovery. A launch without
`--session` must create an independent private conversation. Existing paths,
including either former default, reopen only through explicit selection.

Remove the default-path helper and both legacy lookup branches. Native startup
allocates a `session-XXXXXX` directory under `~/.local/state/blackbird` after
argument parsing. The shell launcher allocates the same kind of directory and
passes its path explicitly so its existing exit-75 restart loop retains the
conversation. Atomic mkdtemp/mktemp creation gives independent concurrent launches
private directories. Print the selected path. Preserve explicit reopen, /new,
provider settings, restart and discovery; discovery lists the Blackbird state root.
Inspection, recovery, continuation and expiry require an explicit session path.
Help and discovery allocate nothing. Existing histories are never migrated or reset.

Direct oracle: launch the actual native executable and shell wrapper with an
isolated HOME containing poisoned old defaults. Require distinct conversations,
explicit reopen preserving identity/context, private permissions, unchanged old
files, and allocation-free probes/pathless-maintenance refusal. Exercise shell
exit-75 restart without an explicit path. Run relevant existing session/RRC tests,
build the optimized executable used by the operator and run the source gate.
No provider requests or new dependencies are needed. One implementation/direct
check layer and one focused recheck/fix layer; the previous command-job shutdown
investigation stays paused.

## Direct results

The old Release executable fails the new poisoned-default oracle. Rebuilt Release
and Debug pass startup/store/new-session PTY/RRC, four cases per profile. Review
identified the older published-UI maintenance selection gap; the shell now
refuses pathless maintenance before executable selection and routes explicit
maintenance to the native executable. Debug's recheck includes that dispatch
fixture and passes. Logs: context/fresh-start-{red,release-checks,debug-checks}.log.

The real launch options `--provider paper --model ultramarine-1-flash --effort max`
from the parent projects directory, with `--once /session`, succeed in a new
private session and report the requested settings. This issues no provider call;
the previous actual Paper tool-call qualification remains in QUALIFICATION.md.
