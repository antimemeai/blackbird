# Mac build ownership and recurring dependency-log recovery

2026-10-15. Read-only source study and cache examination for `arconaut-cfj` and
`arconaut-ew5`. This note changes no build input, process, tool pin or cache.

The recurring recovery/rebuild problem has a concrete explanation in the current
Debug cache. The Apple linker stall remains a separate observation. Fixing the
cache must not be reported as establishing the linker's cause.

## Owned build boundaries

[`scripts/rigor`](../../scripts/rigor) resolves one source root, then configures and
builds a profile sequentially. [`CMakePresets.json`](../../CMakePresets.json) places
every caller of a preset in `${sourceDir}/build/${presetName}`. There is no
per-session suffix and no build-directory lock in the owned runner. Its isolated
process group controls interruption, not admission of other build invocations.
The pre-commit hook checks the tracked tree and index before and after qualification;
that protects source identity, not concurrent writes to build outputs.

Consequently, a separate Codex session, agent, editor or direct CMake invocation
*can* use the same build directory. This is a capability of the present wiring,
not evidence that another actor actually did so. Root's serialization excludes
its own intentional overlap. It cannot by itself establish global exclusion.
The retained logs do not identify an original competing writer. No process
ownership, shared-service action or termination follows from that possibility.

[`CMakeLists.txt`](../../CMakeLists.txt) hashes owned sources for raw/workflow build
identity. Source changes deliberately regenerate CMake and change the two runtime
compile definitions. That accounts for those units and their dependents; it does
not explain repeatedly rebuilding the unchanged foundation after every recovery.
There is no compiler launcher or cache wrapper in the inspected Debug cache.

## Exact tool/source match

The pinned Ninja is `/opt/homebrew/anaconda3/bin/ninja`, version
`1.13.0.git.kitware.jobserver-pipe-1`. Local installed `ninja-1.13.0.dist-info/METADATA`
identifies the Python distribution and Kitware revision `d74ef`. The distribution's
`1.13.0` tag pins `ninja-upstream` to full revision
`d74efef9fa331d3ae60b62479d49254827c081fe`; this was read through GitHub's contents API.
Its [dependency-log reader](https://github.com/Kitware/ninja/blob/d74efef9fa331d3ae60b62479d49254827c081fe/src/deps_log.cc)
was fetched for study, SHA256
`a75840186c525457ddc3b856d387125f79b81ba54a7734b552ecc4b6a8d02982`.

That reader advances its recovery offset after reading a complete record and
before validating its node IDs/checksum. If a complete record fails validation,
recovery retains that record. The next invocation reaches it again and drops the
new records appended after it. Upstream [PR2764](https://github.com/ninja-build/ninja/pull/2764)
fixed this specific loop by advancing the offset only after validation. Our pinned
revision has the earlier implementation. No new Ninja version is adopted here.

## Direct cache observation

The development-only reader in ignored
`context/mac-link-repair-2026-10-15/read_deps.py` opens the input read-only, checks
version-4 records, and models the current recovery offset. It adds safe bounds
checks rather than reproducing unchecked native accesses. It does not invoke
Ninja: even a Ninja dry run can load and truncate this cache.

`deps-readonly.json` records the exact observation:

| Field | Observed value |
| --- | --- |
| Input | `build/debug/.ninja_deps` |
| Bytes | 2,922,348 |
| SHA256 | `33f03f88031100e30f5a2cbb8d393e8f1fcd1ba6db46af41b66bbc87c9798412` |
| Signature/version | `# ninjadeps\n`, little-endian 4 |
| Valid prefix | 1,711 path records and 592 dependency records |
| First invalid record offset | 2,611,704 |
| Record type/body size | path / 60 bytes |
| Path | `CMakeFiles/blackbird_foundation.dir/src/raw_plain.cpp.o` |
| Checksum-derived ID | 1,709 |
| Next expected sequential ID | 1,711 |
| Existing ID for the same path | 1,709 |
| Current reader's truncation offset | 2,611,768, after the invalid record |
| Last valid prefix ends | 2,611,704, before the invalid record |

The complete duplicate record directly establishes semantic corruption and the
persistent recovery boundary. Input bytes matched on a second read in the same
observation. Its duplicate old ID is compatible with interleaved writers, stale
node tables or an already damaged log; the file does not identify a process,
time or original cause. Merely seeing the warning is not a concurrency diagnosis.

## Minimal repair and discriminating checks

1. Root establishes that no relevant writer is currently active. Preserve the
   exact dependency cache with its observed hash in the unit's ignored context.
2. Remove only `build/debug/.ninja_deps`. Keep objects, libraries, `.ninja_log`,
   CMake cache, source archives, tool versions and policies unchanged. This is a
   rebuildable dependency index, not retained application state.
3. Build the affected Debug targets using the same pinned configuration. A
   one-time rebuild reconstructs lost dependencies. Link completion is a separate
   direct result; if it stalls, retain the actual linker/process evidence.
4. Once that build completes, repeat the unchanged targeted build once. Its direct
   oracle is no EOF warning and no repeated foundation compilation. If the cache
   becomes malformed again, inspect its first failed record and known writers;
   do not keep deleting it or infer that an upgrade fixed the initial writer.
5. Finish the required source gate after the affected targets link. Preserve the
   existing test assertions and deadlines.

No build-lock framework is needed to repair this proven record. If future work
allows independent owners to write this same profile, the missing admission rule
should be resolved explicitly: one owned entry point with an OS advisory lock
held through configure/build/test/analysis, or separate build roots. A lock only
in `rigor` would not protect direct `cmake --build` calls documented in the README,
and a parent-only lock can release while an interrupted orphan still writes.
Those limits should guide a small ownership repair if actual concurrent use is
introduced; they do not justify inventing an actor in this incident.
