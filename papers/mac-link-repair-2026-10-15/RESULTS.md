# Mac build recovery and source qualification

The repeated rebuild has a concrete cause and repair. The preserved version4
Ninja dependency log contains a complete invalid duplicate path record at byte
2611704; its checksum names ID1709 when the nextID is1711. The pinned Kitware
reader's known recovery bug retains that failed record, so later invocations
repeat recovery and discard newly appended dependency entries. See the exact
[source study](BUILD_OWNERSHIP.md), [before](DEPS_BEFORE.json) and
[after](DEPS_AFTER.json) observations.

With no relevant build process present, preserve the exact original cache
SHA25633f03f88031100e30f5a2cbb8d393e8f1fcd1ba6db46af41b66bbc87c9798412,
then remove only `build/debug/.ninja_deps`. Rebuild the actual coding test and
native executable with the same pinned CMake/Ninja/Clang/Lua. Both links finish.
The regenerated log has1447 valid paths,89 dependencies and no invalid record.
One unchanged targeted build reports [no work to do](UNCHANGED_BUILD.log), with
no recovery warning or foundation rebuild. No toolchain, source assertion or
native deadline is changed by this cache repair. The initial corrupt writer is
not identified; concurrency is a capability of shared build roots, not a finding.

The earlier native linker/sample processes are absent at22:36UTC. Fresh traced
links with the same configured Apple linker complete a minimal control in0.336s
and actual coding-test objects/libraries in0.270s. These current successes do not
explain the earlier OS/process wait or establish that cache repair cured it:
those links succeeded before cache regeneration. The [primary-source study](PRIMARY_SOURCE_RESEARCH.md)
records diagnostic distinctions and safe next steps if that separate fault recurs.

All four repaired Mac coding registrations pass in15.06s after the regular build.
Earlier changed-case Linux Release13/13 and ASan13/13 and Mac9/9 PTY observations
remain valid for their recorded source. Preserve the original
[test repair results](../tui-settlement-repair-2026-10-15/RESULTS.md).

A current integration correction gives the eight new PTY episode registrations
RUN_SERIAL, matching the original terminal_pty policy already present later in
CMake. Each keeps30seconds and all original3/5second step deadlines; other tests
keep parallelism. CMake-only registration edits also regenerate literal raw/
workflow build-identity tags. Production C++/Lua bodies remain the measured
presenter/cancellation candidate; existing performance datasets retain their exact
source/binary identities and are not relabeled as a newly measured binary.

The installed full source hook passes **157/157 in248.93s**, followed by complete
owned C++ analysis and Lua formatting/syntax/diagnostics. It creates source commit
`676120c` on `work/tui-reference-2026-10-15`, without a source-hook override.
The earlier daemon restart lost the tool session handle, but the original Git,
hook and CTest processes continued. Root verified their identity and followed
the same run; no duplicate gate was launched.

The presenter, cancellation cleanup and regression repairs are now committed,
rather than retained outside the working source. `arconaut-rhi.3.3`,
`arconaut-rln` and the exact dependency-cache fault `arconaut-cfj` are closed.
The broader component/Lua host remains in progress. Historical native quit and
linker-wait causes remain in their existing issues, distinguished from the
repaired test/cache faults. No main integration or live activation occurred;
source archives, original performance identities and AFL state remain intact.

The final follow-up changes documentation and issue backup only, with relevant
diff/data/link checks under the explicit text-only exception. Settled source
checks are not rerun for that follow-up.
