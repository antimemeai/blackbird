# Cancellation between command launch and wait admission

Source diagnosis and bounded repair, 2026-10-15. The first diagnosis was read-only;
the separately authorized repair and direct checks are recorded below. No timeout
change or AFL interaction accompanies this work.

There is a concrete reachable ownership hole in `CommandJobs::wait`. The failure
trace must still establish whether the latest PTY episode took this exact path;
the visible Stopping status alone does not establish its native instruction.

`CodingEngine::call("exec")` calls `command_jobs_->start` and then
`command_jobs_->wait` ([coding.cpp](../../src/coding.cpp), around4649–4685).
Successful `start` has already published a command worker. That worker's raw wait
scope watches its **job stop wake**, not the enclosing UI turn wake
([command_jobs.cpp](../../src/command_jobs.cpp), around447).

The UI can cancel before `wait` establishes itself. `wait` first acquires
`raw_command_claim(110)` and emits the2024 request, both **before its try block**
(around1210–1247). Raw capacity admission checks inherited cancellation
descriptors even if free capacity exists
([raw_runtime.cpp](../../src/raw_runtime.cpp), `claim_capacity`, around2000).
The terminal worker has installed the turn wake in its raw wait scope
([terminal.cpp](../../src/terminal.cpp), around1842). A cancellation in this
interval therefore throws interrupted/ECANCELED from wait admission while the
already launched command has not received its sticky job stop.

Even after wait registration, its loop calls `tick()` **before** testing
`cancelled()`. `CodingEngine::poll_commands` can itself encounter an interrupted
raw acquisition and propagates interruption without stopping jobs. This bypasses
the sole existing stop-on-cancel branch too. `wait`'s Error catch records failure;
it does not issue the stop. An already published worker can consequently survive
the interrupted foreground wait. Normal yield/background behavior requires this
cleanup to apply specifically when `stop_on_cancel` is selected.

The failing PTY fixture's `wait_for(b"sleep 30")` is **submitted-command display**,
not process readiness ([check-terminal-pty](../../scripts/check-terminal-pty),
around162). It can land in either interval. Replacing that observation with
process-ready output would mask the early boundary rather than repair it.

For trace discrimination: a successful start2001 followed by no wait2024/2025,
and no job stop2029/source5, supports the pre-wait path. A registered2025 followed
by interruption from a tick without2029/source5 supports the loop path. This is a
specific source-level explanation to check against originals, not a claim that
all Stopping failures share one cause.

## Bounded repair suggestion

Make the selected wait own cancellation cleanup **before the first cancellable
admission**. On interrupted acquisition/loop exit with `stop_on_cancel`, issue
that job's existing prepaid `caller_stop(...,5)` under the registry mutex, release
the mutex, join its worker, and mark its retained loan joined before propagating
the original error. Reuse one cleanup path for the explicit predicate branch and
the interrupted Error branch. It must not depend on fresh raw capacity, a new
generic shutdown, all-job cancellation or a new timeout. Recording the wait
request is optional when acquisition never admitted; do not invent a2024 request
or terminal observation for that refused admission.

The direct regression cases should force (1) an already sticky caller wake
immediately after real command launch but before wait admission, with capture
enabled, and (2) an interrupted `tick` after wait registration. Require the owned
child to be dead/reaped before wait returns, unchanged exact output, and retained
single terminal settlement. Keep the existing three-second actual PTY oracle.
These are the defect's two entry points, not another broad campaign.

## Authorized repair subplan

The root colleague selected this owned-cancellation gap as an egregious exception
to the ordinary bug queue, within this tranche's one fix/recheck layer. Add two
deterministic real-child cases in `command_raw_test`: the child has already
published and the command owner has acquired its exact `READY\n` original, then
either the caller wake is sticky before wait admission or the first registered
tick throws a distinguishable interrupted/EINTR Error. Both demand child join
before wait returns, unchanged output, one terminal settlement and actual raw
request linkage (including no request at refused admission).

Wrap the actual wait admission in the existing Error boundary, tracking whether
the request really admitted. Reuse a single exact-job prepaid stop/join operation
for interrupted Errors and the explicit cancellation predicate. Exception cleanup
will add no tick calls, preserving the original thrown error; the existing
explicit predicate path keeps its terminal-pump attempts. No stop-all, added
deadline, blanket cancellation shielding or new lifecycle is introduced.

Both deterministic cases fail against the original source at the return-time
assertion that its child has already been reaped. The repair implements the
described common stop/join path and preserves the original Error. The tick case
also refuses any second tick, which would replace that Error with an unrelated
callback exception. Independent root integration review found no actionable
issue; no additional review layer follows.

Original build/test output is retained under ignored
`context/tui-next-host-2026-10-15/cancel-red-build.log`,
`cancel-red-admission.log`, `cancel-red-tick.log` and `cancel-green-build.log`.
Actual end-to-end causation for the queued PTY failure remains unestablished by
these deliberately isolated direct cases.

The repaired Debug build passes. One focused `command_raw` CTest run passes in
6.43seconds, including both new deterministic boundaries and the existing native
callback-fault, false/yield wait and raw ownership checks. Original stdout:
`context/tui-next-host-2026-10-15/cancel-green-test.log`. No additional focused
pass follows; the root owns the required source gate and PTY outcome.

The normal wait path adds only stack state and the existing admission work.
Shared ownership is copied only while selecting the already registered job for
exception cleanup. There is no new heap object, worker, queue or syscall on each
successful wait-loop tour. Cleanup retains the existing command's stop grace and
join behavior; it does not create a new elapsed-time promise.
