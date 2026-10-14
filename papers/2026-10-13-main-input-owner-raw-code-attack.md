# Main input owner's one code/oracle attack

2026-10-13. Read the whole Main input subplan and its MI1–MI3 plan review,
raw_input helpers/description, RawSessionCall/RawCallerTrace and explicit capture
failure scope, the actual Main input/dispatch/plain transport windows, complete
CLI/PTY script and embedded actual-Main native fixture. The newly rewritten
Station branch is a separate owner, not claimed by this review. No build,
production edits, workload or additional review ran here.

M1: plain SIGINT cannot reach its rearm boundary. Main's RawWaitScope at
src/main.cpp:427 borrows interrupt_wake for the session lifetime. perform creates
its4200 request and emits4202 before interrupt_owner.reset at548. Meanwhile the
plain read_line at1323/1333 starts with selected raw_main_poll/read admission.
After a real SIGINT shuts down that sticky socket, both paths refuse admission
with interrupted before reset can execute. The native poll EINTR handling cannot
repair this: the next raw claim fails before poll. A canceled/idle plain session
can therefore exit1 instead of accepting its next prompt; even an already-buffered
prompt reaches the unpaid rearm ordering. Put the existing nonwaiting reset at an
explicit plain-owner idle/joined boundary before fresh input/dispatch admission.
Do not bypass sticky cancellation during active work or reinterpret arbitrary
capacity/business failures as permission to reset. Preserve reset's actual
concurrent-handler resynchronization and refused/busy outcome. Add an actual
plain SIGINT followed by next-command/clean-quit native cut, checking preserved
source, real rearm/incarnation and unchanged process behavior.

M2: the native oracle does not yet substantiate all literal fields/failure cuts
in the plan. scripts/check-main-input:131 checks the4230 parent but discards its
actual fd/requested size. Its4233 parser checks only size/linkage, discarding
returned rc/error/revents.4201/4204 generally check branch, not their Error flag,
code/detail/stage; the embedded fixture checks two particular Error ends only.
The helper fixture proves DATA559 refuses before a requested512-byte read, but
has no paired actual560 success, genuine native negative/EINTR read and poll,
or allocation-after-positive-acquisition prefix cut. MI1's requested4205 grant
minus-one cut is absent: filling CONTROL at the second workflow open proves a
later file-read admission, after branch40 has already been emitted. Tighten the
existing finite parser with actual native arguments/results and explicit reached
Error variants. Add the missing finite native helper cuts and an actual Main
branch-admission cut before its effectful workflow condition. Compare same-lane
ordinal ordering to independent syscall/effect observations, and require absent
unreached work. Keep positive-source/EOF/paste/initial-resume assertions intact;
no generic campaign, new production injection API or extra polling.

The source read budgets themselves are correct: read request52 plus maximum
negative return52 CONTROL, positive DATA48+requested; poll request50 plus maximum
negative return50. errno is saved immediately on the failed native operation and
restored after publication; positive bytes precede pending-limit/append work.
Null contexts forward the original syscall. The fallback branch40 is selected
before the original effectful operator_turn condition; slash short circuits keep
their old behavior. Input originals precede dispatch/settings work. Explicit
capture-failure propagation precedes the interactive Error/backstop handler and
refresh advisory catch, distinguishing raw refusal from an equal business Error.
UI initial/resume sources use actual4202 originals at payload offset0. These
observations do not replace M1/M2 direct fixes and affected rechecks.

Resolve these findings within this ONE whole code/oracle attack. No rereview or
workload qualification is required by this report.

Owner disposition: M1 rearm moved ahead of the first plain dispatch admission;
idle read owner rearms actual pending SIGINT before fresh poll/read, and handles
only its signal's interrupted idle admission without retrying capacity/business
failure. A real forwarded poll raises SIGINT, native next-input acquisition sees
a real returned reset and new native wake incarnation, and the next command and
clean quit succeed. Native wake and Main have distinct producer lanes; the oracle
uses their explicit references and Main's within-lane reset/read ordinals rather
than inventing global order from recorder order.

M2 same finite fixture/parser now checks exact requested fd/512, native read /
poll fields, every CLI Error flag/code/detail/stage, actual560 success paired with
559 refusal, actual failed EBADF read, real non-restarting SIGUSR1 EINTR read and
poll, native EOF, actual pending-string allocation denial after positive512-byte
acquisition, and CONTROL40 branch41 refusal before effectful workflow condition.
The branch fixture forwards the actual native notice before waiting for recorder
ack; held grants release at the actual paid failed dispatch end. Both native
Error ends and original input survive, no workflow reread or effect/backstop is
reached. No timers or fabricated syscall returns are added. Native direct GREEN:
context/main-input-owner/finite-native-final.log; full existing real CLI/PTY /
resume byte graph GREEN: context/main-input-owner/finite-cli.log. Matching strict
link GREEN in finite-matching-rebuild.log. No new review panel or qualification.
