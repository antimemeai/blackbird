# Retained native frame candidate

Current state: the operator reopened the two test failures. The candidate and
repairs are **restored in the working tree, uncommitted**, with current
[results and host-gate block](../tui-settlement-repair-2026-10-15/RESULTS.md).
Original archives below remain unchanged.

The original checkpoint retained the implementation without source publication.
Final source hook:145/147 in372.14s, `coding`60.11s timeout and existing
`terminal_pty` quit-during-exec failure. Full C++/Lua analysis is not reached.
The preceding repaired run passed147/147, then found one test-helper lint issue;
its focused repair does not supersede the final failed source hook. See
[results](RESULTS.md) for each actual run and bounded scope.

The [manifest](CANDIDATE_SOURCES.json) preserves12 source-file hashes, modes,
sizes and local retained paths. The untouched source-overlay archive, binary
patch, copied final LastTest log and compiled native executable remain under:

`context/tui-next-host-2026-10-15/candidate/`

The full base is `d987f6fb207e47106ce4f5fa6339065e88a89f03`.
Active CMake, native implementation, tests and registered scripts have been
restored to that commit. The new blocked fixture/driver are absent from the
active tree. `build/debug/blackbird` was copied into candidate evidence and
removed, preventing that path from executing the unqualified native candidate.
No main merge or native activation occurred. Existing baseline tests were not
rerun after restoring unchanged source.

To reconstruct for explicitly reopened work, first verify the archive and every
file against this manifest. In an isolated checkout at the exact base, overlay
the archive with `tar -xzf /absolute/path/to/source-overlay.tar.gz` from its root.
The retained `source.patch` is an alternative `git apply` input against the same
base; do not apply both. Restoring an experiment does not qualify it: the source
hook remains required before publication. No restoration occurs in this checkpoint.

The measured final overlay is separately pinned to `7df6ab79…` in
[performance provenance](performance/final-linux-release/build-provenance.json).
It differs only in the later blocked-test helper signature, not production inputs.
Both matched120-process rounds, raw samples, original source archives and binaries
remain associated with their actual identities. Retained remote resources are
listed in the performance reports; none includes authority to inspect or alter
the separate AFL campaign.

Queue `arconaut-rln` and `arconaut-xbf` retain final failures. The concrete
wait-admission/tick cancellation fix is preserved with deterministic red/green
results, without claiming it explains every historical cancellation symptom.
That checkpoint left the bounded source task inactive. The subsequent operator
instruction explicitly reopened it; current source publication awaits the Mac
linker and required full gate, with existing authorization intact.
