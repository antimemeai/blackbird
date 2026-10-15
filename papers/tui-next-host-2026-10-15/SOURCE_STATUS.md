# Native frame delivery and retained candidates

Current state: **delivered in source commit `676120c`**. The installed gate passes
157/157 plus complete C++/Lua analysis. The [build recovery and delivery](../mac-link-repair-2026-10-15/RESULTS.md)
records the concrete dependency-cache repair and actual qualification.
Original candidate archives below remain unchanged historical snapshots.

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
At that earlier inactive checkpoint, CMake, native implementation, tests and
registered scripts were restored to that commit. The new blocked fixture/driver were absent from the
active tree at that checkpoint. `build/debug/blackbird` was copied into candidate evidence and
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

`arconaut-rln` is closed after the test-shape repair and current qualification;
`arconaut-xbf` retains the historical native-stall observation. The concrete
wait-admission/tick cancellation fix is preserved with deterministic red/green
results, without claiming it explains every historical cancellation symptom.
That checkpoint left the bounded source task inactive. The subsequent operator
instruction explicitly reopened it; source was restored and now passes the full gate in `676120c`.
