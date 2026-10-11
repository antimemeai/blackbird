# One participant owner plan attack

Root read the whole participant subplan against actual Participants::work,
start/send/drain/cancel/join/archive/shutdown, CodingEngine callbacks, the native
RawWaitControl contract and source-ticket/provider plan. This is the ONE whole
plan attack for this owner. Source remains held until these findings are resolved;
no review of this review or settled transport choices is added.

## 1. An interrupted wake does not make cancellation observable

RawWaitControl observes sticky descriptor readiness, not Run::stop or the
condition variable. The proposed one-byte wake has a single105-byte request/
return bank and explicitly permits EINTR/error as its actual outcome. If that
write returns EINTR before writing, a worker parked in raw admission with its
recorder stopped never sees stop. changed.notify_all() only reaches the business
condition wait. join can therefore block forever after cancellation.

Specify an actual persistent descriptor transition for every first stop. A
bounded write attempt needs a prepaid fallback that makes the descriptor sticky
on failure (for example, retiring its sole write endpoint so waiters see EOF).
Define sole ownership, concurrent owner/worker stop, native close identity,
issuer-lane credit and endpoint reuse/lifetime precisely. Do not add a polling
timer, unbounded post-stop syscall stream or fresh credit acquisition. Preserve
the actual failed write request/result, rather than labeling it successful.

Direct cut: park the real worker in raw admission with the recorder stopped;
forward or interpose the actual cancellation write so its first attempt fails
EINTR without a byte. Require native failed-write fields, the actual subsequent
persistent transition, worker held terminal/withdrawals and join. Include a
competing owner/worker first stop and an already-full readable pipe. Fixture
cleanup resumes only its own recorder before orderly preservation teardown.

## 2. Cleanup still begins with fallible ordinary admission

The finite shutdown9326/join6158/cancel6029 arithmetic is useful, but the plan
says claims acquire before each action and only the worker/late-close banks are
held from setup. Owner shutdown can run with the caller's sticky cancellation
already ready and ordinary CONTROL full. Claiming the action/stop/join bank then
refuses before the real worker gets stopped. Holding the worker's bank does not
make owner stop/join records publishable, and credits cannot cross lanes.

Pay the finite owner cleanup sequence before publishing each live Run (or pay
one explicitly owned bounded Participants lifetime bank). Retain its actual
owner producer handle and establish sequential use if cleanup occurs after the
setup binding returns. Distinguish ordinary cancel/join/read requests from the
mandatory no-new-admission cleanup path; do not claim fresh drain/Retain/Save
success when cancelled. Acquired sources remain owned/preserved and unresolved
semantic debt propagates honestly. Account every actual stop/wake/fallback-close,
join, END and action outcome in the issuer's bank, releasing unused slots. No
unfunded fresh request may precede required cleanup.

Direct cut: ready the actual owner's sticky cancellation descriptor, fill all
unheld owner CONTROL, and leave a real live worker parked in admission. Invoke
mandatory shutdown and require its held owner stop/wake/join records plus worker
withdrawals/END, without raw_wait_data/raw_claim_control admission after entry.
Ordinary user actions may still refuse before effects. Report unresolved semantic
retention rather than treating cancellation as an accepted Save.

## Other claims inspected

The initialized unknown-used extent shares the provider's actual buffer and raw
source reference; it is not parsed or Retained as a complete response. Exact
source offsets58/50/61/49 and actual partitionu32 are consistent. The171 ticket,
5720 owner-drain,5195 worker direction and64receipt limits otherwise cover their
stated finite boundaries. Source and CONTROL stay on the acquisition issuer
lane; workers transfer owned nodes only after the last synchronous borrow.

Preallocated receipt/ring staging, reference-consuming direction transfer,
preissued native semantic append identity and stable unlocked front address the
actual post-retain allocation and reentrant drain defects. The exact prepared
Value must reach call_prepared_colleague without a second preparation. No worker
uses ContextStore and no second encoder or metrics-only traversal is proposed.

Resolve these findings in the subplan and shared interfaces, then genuine red,
source/green and ONE code/oracle attack with direct finding rechecks. The current
completed-owner source checkpoint must finish before this source window opens.

## Finding dispositions

The owner plan resolves finding1 with an AF_UNIX stream socketpair and one
shutdown(SHUT_WR), followed by one close of the taken activation endpoint.
There is no wake write. The studied Linux and XNU paths change the shared
socket endpoint, so a forked child retaining an alias cannot postpone peer EOF
until its own close. The direct cut now holds that child stopped before exec
through worker terminal and join. Native shutdown48+48 and close45+48, together
with stop67, cost256 CONTROL. Competing stops take the endpoint exactly once.

Finding2 is resolved by holding706 CONTROL on each Run's actual setup lane
before live publication, retaining that issuer handle through stop/join/close
and owner END. Mandatory cleanup consumes this held bank without ordinary
admission, Save or Retain. It reports pending semantic debt. Worker5279 and
owner706 remain distinct same-issuer claims; no credit crosses lanes.

These are resolutions of the two findings above. Participant source and its
genuine red/green/code attack remain future work after the current checkpoint.
