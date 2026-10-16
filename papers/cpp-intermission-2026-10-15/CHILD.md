# Native child custody repair

## Cause and contract

The audit's [process study](../cpp-bug-class-audit-2026-10-15/PROCESS.md)
identifies a real ownership gap: collect observes ECHILD but destructor still uses
its saved numeric PGID to signal. Inherited SIGCHLD=SIG_IGN/SA_NOCLDWAIT can erase
the unreaped leader that is supposed to pin this name. PID numbers alone confer
no authority. WNOWAIT only pins a child that still exists as our child.

Before spawn, inspect the parent SIGCHLD disposition under the existing spawn
mutex. Reject SIG_IGN and SA_NOCLDWAIT explicitly before creating any child;
never change the embedding host's global signal policy. Private child ownership
has explicit never-started, unreaped, reaped and lost states. Historical spawned
knowledge stays true after loss, because caller effect classification must not
mistake an executed command for a prelaunch refusal.

Before final group cleanup, use a real non-reaping wait observation even if
collect was never called. ECHILD revokes numeric group authority. Other inability
to establish custody also prohibits signaling. Collect's ECHILD and cleanup's
lost ownership both leave the process-wide uncertainty fence set. A successful
counter decrement is therefore insufficient to permit a quiescent handoff.
Retain native raw wait/signal/reap observations. Group cleanup remains before
leader reap; unresolved direct-child cleanup stays uncertain. No pidfd dependency, host-policy
mutation, PID reuse stress or foreign-process signal is introduced. This is
cooperative owner protection; arbitrary concurrent external reapers that race
between a successful wait observation and signaling are outside that contract.

## Permanent oracle and sequence

Write tests first and compile them against unchanged source. Separate process
modes isolate global signal disposition and sticky uncertainty. The real default
child emits exact output and an exit status; destructor must send the actual
owned group cleanup before reaping, with no fence. Ignored/no-cldwait policy must
reject before launch, leave child count unchanged, and preserve the exact policy.
Lost-custody modes externally reap only their own printed child PID, then exercise
actual ECHILD with collect and without collect. A kill seam records and refuses
any signal attempt; there must be zero. An owned cleanup-error mode establishes
that a failed final custody observation leaves the uncertainty fence and sends no
group signal. The fixture independently cleans only its still-pinned real child.
Real signals are forwarded only in the known-owned successful case.

Save exact RED, then implement owner transitions and run the same GREEN oracles.
Root performs one independent source/oracle challenge and this owner resolves
its real findings. Root integrates CMake and source qualification; this lane runs
no broad gate or shared Ninja build.

## Executed changes and direct results

The private owner now uses `Custody::{never_started,unreaped,reaped,lost}` and
keeps `spawned()` historical. `collect` clears numeric signal authority on actual
ECHILD before throwing. The destructor makes exactly **one** nonblocking WNOWAIT
observation; an error (including EINTR) conservatively revokes authority rather
than retrying an unbounded sequence using one prepaid record. It records that
actual call/result and performs no later signal if custody was not established.
The existing final waitpid retries EINTR and records its final result as before.
The final owned waitpid retains its original blocking policy and requires an
exact positive reap; ECHILD or another unsuccessful result marks custody lost.
An optional WNOHANG-on-signal-failure experiment was removed after the actual
Darwin exit-transition regression documented below. Broader live signal-denial
cleanup/hang behavior remains a separate queued question.

`RawChildTrace::cleanup_wait` emits the existing 1405 native layout from cleanup
credit, adding exactly `E+Ref+u8+4*int+pid_t+uid_t` (65 bytes on the qualified
profiles) to the pre-acquisition claim. No ordinary reservation or allocation is
performed by this new final observation. The permanent raw-on test withholds
**all** remaining ordinary CONTROL capacity after collect, then reads the actual
CONTROL originals and requires collect wait, final custody wait, group signal,
and exact reap in order. No fabricated native result is used.

Final permanent test source has seven private modes. Against baseline HEAD's
unchanged native header plus existing Debug foundation, `default` passes and the
other six fail their correct-behavior assertions. To rerun the identical permanent
raw-on oracle against the original owner after adding it, an ignored test-source
copy changes only its include to the exact `git show HEAD:src/native_process.hpp`
copy. The corrected source and its actual `src/raw_provider.cpp` are compiled
against the same existing foundation archive (the direct object supersedes the
archive's old raw_provider object). All seven then pass. The final standalone
build also uses all the normal CMake warning flags without suppressions.

| Mode | Old source | Corrected source |
| --- | --- | --- |
| default actual output/nonzero exit/group signal/exact reap | PASS | PASS |
| ignored parent policy prelaunch refusal/policy unchanged | FAIL | PASS |
| SA_NOCLDWAIT parent policy prelaunch refusal/policy unchanged | FAIL | PASS |
| actual externally reaped child, collect ECHILD/sticky fence | FAIL | PASS |
| actual externally reaped child, destructor prohibits signal | FAIL | PASS |
| actual child, final custody-observation EINTR prohibits signal | FAIL | PASS |
| raw-on final custody observation under CONTROL pressure | FAIL | PASS |

Exact source copies/binaries/build logs and RED/GREEN stdout are in ignored
`context/cpp-intermission-2026-10-15/child/` (`red.log`, `green.log`,
`red-build.log`, `green-build.log`). The initial raw fixture build lacked the
local Result unwrapping helper; this was a compile setup defect, corrected before
claiming raw RED/GREEN. Root owns CMake registration, Linux/sanitizer qualification,
the independent source challenge, and the required installed full source gate.
This lane has not run that full gate, committed, or changed shared build files.

## Scope correction: Darwin zombie-only groups

A first implementation fenced every group-signal error except ESRCH. The normal
singleton counterpart exposed a false new assumption: this Mac returns EPERM
when its only group member is our WNOWAIT-pinned zombie; a live owned group signal
succeeds. A separate actual posix_spawn/WNOWAIT/kill/reap fixture confirms that
result without the signal seam (`zombie.cpp`/binary in the ignored directory).
`kill(group,0)` also returns EPERM in this case. This cannot mean an inaccessible
live group is empty.

Read Apple's primary [XNU kern_sig.c](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_sig.c#L1612-L1621)
on 2026-10-15: killpg1 filters zombie members, then returns EPERM when its callback
found no signalable members. This is a live URL/source read rather than a pinned
acquisition; no source archive was ingested or implementation copied. Root
explicitly constrained the repair to child custody. Therefore the final code
preserves the existing best-effort group-signal scope and exact raw error
visibility after verified custody, requires an exact positive direct-child reap,
and fences lost custody. It neither infers empty group from EPERM nor
claims portable descendant containment. Broader group-cleanup certainty is a
separate queued question, not an invented libproc/procfs membership proof.

Root's single independent owner/oracle review found no remaining scoped defect.
All seven integrated Debug CTests pass (domain-child-green.log). The broader
process-group containment question is actually queued as arconaut-ruj; exact
leader custody is the completed claim.

## Full-gate regression: native overflow cleanup during Darwin exit

The full installed Debug gate reports provider_raw's existing exact final-reap
assertion failing. An isolated diagnostic reads actual raw1405/1407/1408 fields:
normal fixtures observe the exited leader, group SIGKILL returns EPERM, exact
waitpid succeeds. In the overflow fixture, the final custody waitid returns0 with
si_pid0; the child is transitioning out. Group kill returns EPERM while a following
WNOHANG waitpid still returns0. Our optional nonblocking-reap change therefore
releases the owner before its actual child is reaped. This is a real regression,
not an ordinal/golden update or a reason to weaken the existing reap assertion.
The extra recorded custody observation itself is accurate.

Exact independent diagnostic source/build/raw-decoded stdout are ignored
`context/cpp-intermission-2026-10-15/child/provider-raw-diagnostic*`. Root is choosing
the smallest correction under the existing scoped best-effort group contract;
no source fix or regression weakening preceded this evidence.

Root selected restoration of the original blocking exact final wait after the
actual custody check. The existing provider_raw final-reap assertion is unchanged.
The corrected standalone full provider fixture passes and records that same
waitid0 / killEPERM transition followed by the exact positive leader reap;
`provider-raw-final.log` contains those real native values. No deadline,
process enumeration, or weakening of cleanup was introduced. The separate
unresolved live group-signal-denial/hang concern is queued as arconaut-ruj.

The former experimental cleanup-error case has been replaced, under its existing
CTest name, by the scoped final-observation failure oracle: one injected waitid
EINTR immediately before destruction; no retry, zero group signals, sticky
uncertainty after owner-counter release. Fixture cleanup bypasses that consumed
seam only after independently checking its actual child remains unreaped, then
kills/reaps only its owned child. This revised permanent oracle fails against the
retained original header (`final-observation-red.log`) and all seven final modes
pass using the corrected header (`final-green.log`, `final-build.log`). The
original failed-signal experiment and its original logs are preserved as
historical failed design evidence, not a claim that the final contract implements
nonblocking signal-failure cleanup. Root owns integrated/sanitizer reruns; this
lane has run no shared Ninja or source gate for the correction.
