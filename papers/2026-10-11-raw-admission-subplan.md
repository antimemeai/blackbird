# Raw admission cancellation and RPC ownership

2026-10-11. Concrete first completion unit for nls.2. The raw producer kernel,
published bytes, reclaim rules and prepaid terminal emission remain unchanged.
This unit makes the existing slow admission/setup path interruptible; actual
owner cancellation and cleanup integrations follow in their own owner units.

Grounding: the acquired daemontools iopause.c, skalibs timed_read.c/selfpipe.c,
and the raw transport study use descriptor readiness and explicit deadlines.
Their callback plumbing, timestamp representation, timeout padding and periodic
limits are not adopted. Own a native poll wait with a sticky owner cancellation
descriptor and an optional existing steady-clock deadline. No callback or newly
scheduled periodic timer runs in the main process.

RawWaitControl contains two borrowed cancellation descriptors and an optional
steady-clock deadline. Descriptors must stay valid until all borrowing waits
finish; readability/HUP means cancelled and is never drained by the waiter.
Invalid descriptors report I/O failure. RawWaitScope selects that control for
the current thread, merges distinct parent cancellation descriptors and the
earliest deadline, and restores the previous scope. More than two distinct
controls is a setup capacity error. Native owner control and signal wakeups can
therefore coexist; nested provider work narrows the deadline without dropping
the outer cancellation. There is no emitter callback or allocation.

Admission checks cancellation/deadline before acquiring a fact, including when
space is already available. Full data/control waits include space, supervisor
death and both cancellation descriptors. Return interrupted/ECANCELED or
interrupted/ETIMEDOUT without changing P/D, consuming held credit or fencing the
emitter. Hook admission wrappers propagate interruption without calling the
ingest-failure fence. Already admitted terminals still emit immediately.

The existing RPC std::mutex can otherwise block cancellation behind a stopped
supervisor. Replace its slow-path ownership with a setup-created nonblocking
pipe containing one token. A requester takes the token or polls token/death/
cancellation; its guard returns the token on every exit. This local RPC gate is
separate from per-lane reverse notices. An acquire/release ownership atomic
orders the private RPC state between execution owners; pipe operations alone
are not a C++ memory-ordering primitive. Nonblocking send/recv prevents a full
socket or stale readiness from creating an uninterruptible syscall. Poll uses
only the actual deadline; without one it waits indefinitely for a real event.

Exactly one request/reply may be outstanding under this gate. If cancellation
follows a successful send, State keeps pending_response=true. The next owner
must consume and validate that response before sending its own request. It does
not report an old response as the new operation's result. Successful reply
consumption clears pending state even when that request reports an error.
Registration cancelled after send can have succeeded; retain supervisor-owned
mapping/registration until closure/recovery rather than unlinking uncertain work.

Direct red/green checks use real pipes, shared memory and the actual supervisor:

- Sticky cancellation on free/full data and control leaves source cursors and
  held terminal bytes unchanged; prepaid terminals still publish; reset control
  permits new admission without an ingest-failure event.
- Stop the supervisor and observe an actual queued request before cancellation.
  Requester returns interrupted while its reply remains outstanding.
- While one RPC holds the gate, a second requester with its own cancellation
  descriptor returns interrupted without taking ownership or stealing a reply.
- Resume the supervisor, replace the recorder, then request fresh status. The
  old reply is drained and the new reply identifies the replacement recorder.
- Expired deadlines refuse free admission; actual deadline waits at full-space,
  gate and reply boundaries return ETIMEDOUT with originals unchanged.
- Preserve existing supervisor-death, restart and source-to-disk byte oracles.

One targeted design/plan attack precedes source changes. One code/oracle attack
and finding-driven recheck follows. Existing source gate and relevant Mac/Linux
diagnostic/release checks apply; six workers total, no arbitrary stopwatch,
power-loss work, eval run, laptop mutants or dependency adoption.

## Implementation and direct results

The direct baseline run rejected the old behavior at free-space cancellation
(raw_admission:91, exit134). The API scaffolding alone did not honor the control.
After implementation, the same oracle passed. The expanded actual-runtime
fixture stops the recorder, fills data/control while retaining terminal credit,
expires both waits, cancels an actual slow-path status request and publishes the
held terminal with D still zero. Resuming the recorder preserves/reclaims all64
data and64 control bytes. The separate stopped-supervisor fixture cancels a
gate waiter and reply waiter, expires an actual gate waiter, resumes the old
reply and verifies zero unread bytes before a new recovery request.

One code/oracle attack found four defects documented in
[the review](2026-10-11-raw-admission-code-review.md). Duplicate descriptors are
normalized before scope merging. The native RPC now carries five i64 fields,
including error.detail. An actual registration write returns ENOSPC in the
supervisor and the requester receives io/ENOSPC unchanged. State retains the
latest observed supervisor fence in an atomic; new free-space data/control and
thread admission refuse it, while already-held terminals still publish. A later
successful explicit recovery reply updates that known state. This does not
claim that a free producer polls for an as-yet-unobserved supervisor failure.

Context retirement observes the same current control and leaves uncertain
registration/mapping in supervisor custody on interruption. Registration's
nonblocking EINTR retry returns to the caller's cancellation check. Existing
runtime send interposers now forward the actual flags rather than converting
nonblocking setup sends into blocking sends. Default admission with no control
does not poll cancellation FDs or acquire a deadline clock; a configured sticky
FD entails zero-time readiness checks at admission. Full-path cost measurement
remains the workload unit rather than an unmeasured speed claim.

Finding-driven direct recheck passes on native debug. Logs are ignored
/tmp/blackbird-admission-{red,green,recheck}.log. Qualified source lint and the
integrated Mac/Linux debug/release/diagnostic checks follow at the source
checkpoint; they are not claimed by this direct result. No evals or mutants ran.
