# Independent challenge: supervisor-death RPC fixture cleanup

2026-10-15. Read-only review of root's layer2 fixture repair. No production edit,
test run or unchanged retry in this review.

The final gate reports `raw_runtime:300` after “supervisor death during actual
RPC.” The line asserts success of SIGKILL to the saved recorder PID *after*
SIGKILL to its supervisor, requester join, and verification of audit_unavailable.
That failure gives no retained errno, so it does not independently establish the
precise OS failure code. It does expose an invalid cleanup assumption: the
recorder is allowed to disappear before this late signal.

Source read:

- `tests/raw_runtime_test.cpp`: the interposed send marks status_sent only after
  the actual single-byte status request successfully enters the kernel; the
  fixture stops the supervisor before queuing that request.
- `src/raw_runtime.cpp`, recorder `lifecycle` around850–870: the supervisor-owned
  control channel closing produces an explicit failure path. The recorder need
  not remain alive after supervisor death.
- `start_recorder` around1091–1117 and `supervise` around1168–1191: the recorder
  is the supervisor's direct child. The supervisor installs a real SIGCHLD
  handler; it does not select SIG_IGN or SA_NOCLDWAIT. Its wait/restart logic runs
  in the supervisor loop.
- `RawRuntime::State` destructor around1619: runtime destruction owns the final
  supervisor exit reap. A fixture may consume a stopped-child observation without
  consuming that later exit observation.

Root's proposed order is sound: establish the stopped supervisor, queue the real
status RPC and observe status_sent, kill its recorder while the stopped parent
still pins child identity, then kill the supervisor and join the requester. The
stopped supervisor cannot reap/restart the recorder. No signal targets the saved
recorder PID after parent death, so orphan exit/reaping cannot turn fixture cleanup
into a stale PID operation. Recorder death cannot answer this status RPC: its
socket is the supervisor's, and the supervisor remains stopped until SIGKILL.
The expected audit_unavailable result therefore still attacks death of an actual
in-flight supervisor request, rather than a timeout or thread-start approximation.

One narrow strengthening is needed for the literal ownership claim: successful
`kill(SIGSTOP)` proves signal acceptance, not an observed stopped state. Observe
that owned child's stop with an EINTR-safe `waitpid(supervisor, &status,
WUNTRACED)` and require WIFSTOPPED plus WSTOPSIG==SIGSTOP before the recorder kill.
This consumes only the stop notification and leaves runtime-owned final exit
reaping intact. It adds no new deadline, production machinery or broader test
layer. Retain the existing status_sent barrier, failure-code assertion and named
lane cleanup. Do not weaken the recorder kill assertion to accept arbitrary
errors or retry the unchanged fixture until it happens to pass.

Disposition: accept the order repair with the explicit stopped-child observation.
The identified defect is the fixture's post-parent-death cleanup assumption;
this review finds no production change necessary for this failure.
