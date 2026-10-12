# One command, UI and SIGINT wake plan attack

Root read the whole ignored wake subplan against RawCommandWake, RawProducer
publication, RawThreadContext failure credit, main handler/reset/teardown,
TerminalUI prompt transfer, command worker/cleanup and the acquired POSIX,
C++ signal rules, Linux AF_UNIX and pinned XNU shutdown paths. This is the ONE
whole plan attack. There is no signal-source qualification yet.

## W1: reset must first acquire exclusive signal-producer ownership

The draft prepaid reset on the dedicated signal producer before its rearming
CAS. An armed handler could publish concurrently. A busy reset could also emit
its lifecycle result on that lane while the handler still owned it. Neither
an atomic wake state nor preheld credit permits two execution owners of a lane.

Reset request/return now use99 preheld bytes on the actual caller's producer.
Only a successful CAS to rearming permits binding the dedicated producer and
attempting its nonblocking native grant, before descriptor effects. A failed
grant restores the actual old state and sticky cancellation after ending that
binding. An unused old96-byte notify claim is retained and reused; only a spent
claim is replaced. Prepared signal pointers cannot refer to moved/released
credit. The binding ends before armed is published and sticky resynchronization
can invoke the signal publisher. Busy reset preserves the prompt and never
borrows the handler's lane. Direct interleavings gate an actual handler against
reset/grant and repeatedly reset uncanceled work without leaking credit.

## W2: failed shutdown cannot establish cancellation or a joined worker

Linux's connected AF_UNIX SHUT_WR path and XNU's single-owner protocol path
wake the peer without waiting for inherited writer aliases. They do not justify
success after descriptor corruption or an actual native denial. The draft
recorded failure but could then unwind into an indefinitely blocked jthread
join. Releasing a fixture recorder would hide that production defect.

Potentially borrowed wakes now select a literal fatal owner-process disposition:
after publishing the actual failed2057 result, call async-signal-safe _exit(70).
No callback, stack unwinding, retry, join or fabricated orderly child settlement
precedes exit. The existing native result is the failure event; there is no
second event merely certifying it. The independent recorder preserves published
bytes and records owner death; unfinished effects remain uncertain on reopen.
Setup failures before publishing a borrowed wake remain ordinary errors with
preheld resource cleanup. A real isolated production failure cut must establish
actual exit70 before a blocked join, exact preserved request/result, unresolved
recovery and the fixture parent's later cleanup of its actually owned child.
This is a failed cancellation, never a successful cleanup claim.

## Signal publication and the other inspected boundaries

The revised plan rejects calling the ordinary std::span/optional/Result emitter
inside the handler. Its prepared POD/native-pointer path shares the existing
ring/envelope invariant and permits only fixed native copies, qualified lock-free
atomics and listed POSIX calls. No TLS, registration, RPC, allocation, mutex or
metrics transform runs there. Existing FAILURE25 records the first ingest error;
held shutdown/result still publish, and exclusive owner handoff reports fencing.
The entire call graph and supported emitted machine code need the direct check.

Staged socket replacement preserves actual borrowed descriptor numbers via
dup2, repairs CLOEXEC and names each actual incarnation. Partial replacement
is failed custody and cannot admit a new waiter. All staging and final closes
are counted; no guessed close retry follows EINTR. Joined-turn rearm preserves
queued/composer input on refusal. Handler lifetime extends through the actual
UI, command and participant joins; restoration retires its pointer and previous
action under the owner's signal mask. Native signal delivery on an owned worker,
stopped pre-exec aliases, competing stop issuers and notice failure remain direct
oracles, not implied by generic cancellation tests.

## Disposition

W1/W2 are resolved in the owner's revised subplan, including source-derived
finite credits and pinned XNU restoration sources. Publish that concrete plan,
run the genuine existing pipe/EINTR parked-admission red before replacement,
then implement the whole owner and run its direct greens. Root performs one
code/oracle attack after implementation, fixes findings and rechecks their actual
paths. No extra review panel, timer, evaluator or power-loss work is authorized
by this plan resolution.
