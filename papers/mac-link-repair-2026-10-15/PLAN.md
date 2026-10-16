# Repair the Mac build and finish the restored TUI unit

Operator explicitly asks to study the system, analyze the problems, formulate a
plan and fix the build, keeping the restored source. This scope includes actual
host/build diagnosis and the mandatory source gate; no TUI source is set aside.
Existing archives, original results and AFL jobs/output remain untouched.

## System and observations

The pinned Clang23 driver calls the installed Xcode Apple linker. CMake/Ninja
own generated dependency/build logs; production code and tests are unrelated to
the linker process itself. Previous Debug and additional profile links stalled
with negligible linker CPU; SIGKILL requests were pending. System ps/sample also
failed to return. Those observations did not locate the blocked operation.

At22:36UTC, exact known previous PIDs57610/62006/63250 are absent, and a read-only
UID-filtered libproc snapshot shows no linker/compiler/CMake/Ninja/sample process.
This is a changed host state, not proof that any prior source repair fixed it.
Do not infer concurrent builders without observing them. Source and cache bytes
remain available to study; preserve them before changing disposable metadata.

## Defects/questions and implementation sequence

1. Investigate repeated premature-EOF dependency recovery. Study the pinned Ninja
format/recovery code and exact retained `.ninja_deps`, identify an invalid record
if present, then preserve/hash the file and remove only that derived cache while
no build owns the directory. Do not switch build drivers or delete source.
2. Distinguish a currently broken linker from a past transient process/host wait.
Run a small configured-toolchain control and the real coding-test link into
unique ignored scratch outputs, with input tracing and exact child PID/command
records. Use only known-process libproc thread/FD observations if a link stalls;
no further broad ps/sample probes, service/security-policy changes or host restart.
Do not claim old hang causation from a successful control.
3. Apply a concrete build/provenance/process fix justified by those observations.
Use pinned tools and actual build ownership. If concurrent actors are observed,
serialize access to that build tree instead of terminating others. New diagnostic
utilities remain development-only Python; no production library adoption.
4. Rebuild through CMake/Ninja, check that unchanged source reaches no-work rather
than destructive recovery, and run the changed coding/PTY registrations. Preserve
all original interaction deadlines, actual child cleanup, raw originals and
stable presentation baselines. Existing Linux Release/ASan changed-case checks
remain settled unless relevant production/tool changes justify another scope.
5. One challenge of the concrete change, then one affected recheck/fixes. Stage
all intended source/docs/Beads backup and run the installed full source hook.
Finish the authorized source checkpoint and push the work branch when the gate
passes. Do not bypass source checks or claim main integration/live activation.

The two-layer repair/recheck bound applies to identified defects, not arbitrary
new wall-clock cutoffs or repeated unchanged retries. Keep partial logs and
exact process/source identities; conclusions distinguish cache faults, linker
work, kernel waits and prior test-oracle defects. No unrelated assurance campaign.

## Selected concrete repair

Exact Debug dependency cache SHA256
33f03f88031100e30f5a2cbb8d393e8f1fcd1ba6db46af41b66bbc87c9798412
contains a complete invalid path record at2611704: nextID1711, checksumID1709,
with that path already assigned1709. The pinned source advances its recovery
position past that semantically invalid record, retaining it on every recovery.
Preserve original bytes and remove only `.ninja_deps` while no builder is active;
regenerate with the same pinned Ninja and verify no-work on a second invocation.
No writer identity or concurrency cause is established.

Both fresh traced links now finish: a minimal control and the actual coding-test
objects/static libraries into unique ignored scratch outputs. This proves the
current configured linker can complete those inputs; it does not explain the
old kernel/tool wait. No linker replacement or host restart is warranted. Proceed
through the normal build and required source gate after the concrete cache repair.

The direct unchanged-build oracle passes: no EOF warning and no work. All four
Mac coding registrations now pass15.06seconds. A concrete integration finding in
the earlier split is corrected before full gate: CMake's later existing policy
made original `terminal_pty` RUN_SERIAL, but the new eight episode registrations
did not inherit it. Give those episodes the same RUN_SERIAL policy, retaining
30second process limits and all native3/5second deadlines; other tests retain
parallelism. No broader suite serialization or additional profile campaign.
