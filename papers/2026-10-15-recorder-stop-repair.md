# Recorder stop and deadline-test repair

The operator requests the two repairs identified by the twenty-run study and
only affected tests. Preserve the existing staged economical-defaults candidate.

Add a recorder-owned nonblocking CLOEXEC pipe before installing/unblocking its
stop handlers. The handler sets the existing stop mode and writes a wake byte,
preserving errno and retrying EINTR. A full pipe is already a persistent wake.
The recorder includes its read end in every idle poll; stop bytes need not be
drained because a stop request ends this recorder. Clear the handler descriptor
before closing the pipe. Keep graceful drain and immediate recovery semantics.
This follows the existing owned SIGCHLD wake-pipe mechanism; no dependency.

Add a native recorder cut after retirement reply and before the next kernel poll.
Block stop signals before announcing the cut, then atomically await the actual
supervisor stop signal. Forward a bounded kernel poll after its handler returns
and require a readable endpoint. This must fail on the old flag-only code, and
must preserve exact originals through graceful shutdown and explicit recovery.

Separate already-expired admission from the ordinary deadline case. An already
expired deadline must refuse without a blocking wait; a future deadline may
expire before or during that wait. Keep the actual blocking-wait requirement for
cancellation, and require exact interrupted errors and no native effects in all
cases. Report which deadline path occurred rather than assuming scheduler speed.

Direct checks: existing operation_raw, raw_runtime, raw_recorder_cuts, and
raw_admission. Build only those targets; use focused formatting/static analysis
for changed translation units. No whole-suite repetition, twenty-run extension,
host-profile campaign, or new review/certification loop. The operator's explicit
affected-tests-only scope supersedes the earlier blanket test gate for this unit.

## Executed result

The new native stop cut fails on the old flag-only implementation: the real
signal handler runs before poll, but its bounded native poll times out with no
readable endpoint. Parent checks fail at stop_wake, exit SIGABRT (-6); fixture
cleanup joins its owned supervisor. Log: `context/recorder-stop-red.log`.

Implement the pipe in `src/raw_runtime.cpp`; add the native cut and exact literal,
origin, closed-run and replacement-recorder assertions in
`tests/raw_recorder_cuts_test.cpp`. The original and post-recovery records have
their distinct actual lanes, DATA partition, source start zero, ordinal one and
exact NUL/high-byte literal. No extra record is accepted.

Update `tests/operation_raw_test.cpp` to name cancellation, already-expired and
future-deadline cases. Preserve exact terminal Error and no-effect/failure-site
assertions. The future deadline accepts either valid timing path and prints the
observed path; it does not claim every host will reach the wait before expiry.

Build only operation_raw_test, raw_runtime_test, raw_recorder_cuts_test and
raw_admission_test, including their required native libraries. Run those four
CTest entries once, serially, using their existing watchdogs:

| Test | Result | Seconds |
| --- | --- | ---: |
| raw_admission | Pass | 0.57 |
| raw_runtime | Pass | 0.45 |
| raw_recorder_cuts | Pass | 0.45 |
| operation_raw | Pass | 21.00 |

Total 22.47s. The recorder test passes both actual supervisor SIGTERM shutdown
and SIGUSR1 explicit recovery cuts. Operation output reports expired=1/blocked=0
and expired=0/blocked=1, so this run exercised both deadline timing paths.
`context/recorder-stop-affected-tests.log` and its copied LastTest log retain
the results. Qualified clang-format and clang-tidy pass all three changed C++
files; no suppressions. No full suite, new host-profile campaign or repetition.

Close arconaut-7qn for the concrete race and test flaw repaired here. The earlier
120-second timeout was not reproduced in the twenty-run study; this repair does
not retrospectively prove its cause. Checkpoint and push only this repair and
its records, using the operator's explicit affected-tests-only scope. Leave the
separate staged economical-defaults candidate and untracked artwork intact.
