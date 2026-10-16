# Child identity and cleanup survey

Baseline095d7d1, repair65e5877 included. Root owns this class; the three staff
reviewers own registry/effect/terminal classes. This report is a new source
survey, not another recheck of the earlier raw_runtime fixture correction.

## P1: native child cleanup signals a group after losing child ownership

High confidence, conditional on a supported POSIX launch environment. In
src/native_process.hpp89–108, Child's destructor unconditionally signals -group
when group>0, then waits for pid. collect()353–373 uses WNOWAIT to pin the leader
normally, but throws on ECHILD without clearing group or recording lost custody.
A private additional exec probe on the qualified Mac confirms SIGCHLD=SIG_IGN
survives exec (`SIGCHLD ignored after exec=1`). POSIX permits an implementation
to retain or reset this particular ignored signal; do not assume every platform
inherits it identically. No main startup check or
parent-side normalization establishes the opposite. The child's spawn attributes
cannot fix its parent's child-reaping policy. Ignored SIGCHLD permits automatic
reaping, so WNOWAIT cannot provide the claimed pin. The saved numeric process
group may no longer identify owned work when destruction signals it. Actual PID
reuse or an unrelated target is not demonstrated or required by this probe.

A direct scratch program sets SIGCHLD=SIG_IGN only in its own process, starts
/usr/bin/true through the actual current Child, and calls collect(). Its kill
symbol intercepts and logs the later attempted signal, returning ESRCH without
issuing a native signal. Observed on the qualified Mac Debug foundation:

```
collect error=io detail=10
intercepted-only kill target=-80554 signal=9
ownership-unavailable=1 subsequent-group-signal-attempt=1
```

The reproducer does not churn PIDs or attempt to target another process. It
establishes a signal attempt after ECHILD, not actual destructive effects.
The additional `signal-inheritance.cpp`/`.log` records the exec observation.
Exact source, build and run log remain under ignored
context/cpp-bug-class-audit-2026-10-15/process-child-*. The build uses the existing
Debug library, Clang23.1.2, C++20, BLACKBIRD_DEBUG=1, and its configured Xcode SDK.

Prevention: a move-only child owner must explicitly distinguish active/unreaped,
reaped and ownership-unavailable. Group signaling requires actual unreaped
custody and must stop after ECHILD; lost custody must remain visible rather than
claiming a successful reap. Establish the SIGCHLD precondition before spawn or
refuse this unsupported parent policy explicitly. Silently resetting a host's
process-global signal policy is unsuitable for arbitrary embedding. Keep ownership
transitioning inside one API; callers should not retain pid_t as a kill capability.

Linux pidfds can give stable individual-process identity, but are not a portable
substitute for group ownership. A numeric PGID still needs a valid pin, and a
pidfd obtained after exit can already be too late. This study selects no Linux
containment dependency or global signal-policy change. [wait(2)](https://man7.org/linux/man-pages/man2/wait.2.html),
[pidfd_open(2)](https://man7.org/linux/man-pages/man2/pidfd_open.2.html), and
[POSIX exec](https://pubs.opengroup.org/onlinepubs/9799919799/functions/exec.html)
supply the relevant contracts; the exact acquired Linux6.19 sources and POSIX2008
snapshot are in [the manifest](PROCESS_TOOLING_SOURCES.json).

## Other owners and counterexamples

| Owner examined | Actual custody mechanism / disposition |
| --- | --- |
| CommandJobs run and shutdown, src/command_jobs.cpp924–941,1028–1060,2049–2110 | ECHILD marks custody unavailable and suppresses later owned signal paths. A positive WNOWAIT observation and unreaped child normally pin group actions. No second stale-child signal defect established here. |
| EditorChild, src/terminal_custody.cpp634–690,944–975 | Tracks owned/reaped, clears owned on successful wait or ECHILD. No kill follows its observed lost custody. Failed cleanup remains a separate instrumentation/error concern; no new concrete failure reproduced. |
| Raw recorder supervisor, src/raw_runtime.cpp1119–1134,1168–1175,1226–1285 | Owns its direct recorder child and installs a real SIGCHLD handler; stop/reap ownership differs from the inherited-policy Child defect. |
| RawRuntime recovery, src/raw_runtime.cpp1440–1448 | kill(pid,0) is an ambiguity check, not adopted ownership. A live/reused PID prevents recovery; it does not authorize a signal to that PID. |
| TraceArchiveWorker, src/trace_archive_worker.cpp76–82 | Closes its private control pipe, waits only its saved direct child, and sends no late numeric signal. Final archive work may delay shutdown; this is not evidence of the same stale-signal defect. |
| SignalWaiter, src/workflow_main.cpp84–101 | Dedicated joinable thread, closing flag then wake signal then join. No detached thread/PID-lifetime analogue found in this owner. |
| tests/workflow_process_test.cpp44–74 | Clears pid_ immediately on positive waitpid result, preventing its destructor from signaling a reaped child. |

The source search covers owned src/include process calls and related native
fixtures. Broad matches such as Composer::kill are text editing, not OS signals.
No conclusion about arbitrary foreign reapers or arbitrary exec containment is
implied by these scoped negative findings.

## Fixture hazards requiring decisive checks before bug claims

Provider raw death-cut fixtures (tests/provider_raw_test.cpp793–823 and
provider_exchange_raw_test.cpp698–739) signal SIGSTOP to both holders but do not
observe the stopped state before changing the world. Stop signal acceptance is
not a stopped-state observation. However these fixtures also require actual
published-but-unacknowledged bytes before the kill cut, so source inspection
alone does not show a false success or a stale PID kill. Do not copy the previous
raw_runtime cleanup-race diagnosis onto them without a concrete schedule.

Similarly tests/ui_raw_test.cpp1250,1316,1365 treat any nonzero waitpid return as
completion, without explicitly rejecting -1. A zero-initialized status could then
look like exit0. No actual EINTR/ECHILD schedule is established in the unchanged
fixture (it installs no interrupting signal handler). Retain as a specific oracle
hazard, not an observed product failure. A future direct syscall fault at the
actual wait, with child demonstrably still alive, is the decisive check. An owned
wait helper returning Running/Exited/Error removes this representation ambiguity.

## Guard fit

Standard C++ move-only state owners and distinct OwnedChild/BorrowedPid types
reject accidental copying/target substitution, but do not independently prove
kernel custody. Clang consumable annotations can warn on calls after a modeled
reap; the two installed compilers reject a bad scratch sequence and accept its
good counterpart. The model must transition on every syscall result, including
ECHILD; unmodeled external auto-reaping remains a blind spot. Runtime custody
checks and an inherited-signal direct regression bear on that actual claim.

Compiler results and costs are in [TOOLING.md](TOOLING.md). No production code,
annotation wrapper, linter configuration or runtime process policy changed here.

## Implementation followup: selector meaning limits the negative control

The intermission's ordinary Linux command_jobs check establishes a sibling not
found in the initial negative survey. Status-only WSTOPPED|WCONTINUED can return
ECHILD for an actual unreaped exited child; only an exit-inclusive query supports
this total-loss classification. The original table's no stale-group-signal finding
is unchanged, but it must not be read as a general qualification of custody.
See [WAIT_SELECTOR](../cpp-intermission-2026-10-15/WAIT_SELECTOR.md) and P1
arconaut-vyi for the actual continued-child diagnostic and permanent red-first
repair. No observed selector/native errno is normalized to manufacture success.
