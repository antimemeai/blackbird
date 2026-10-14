# Main input selection and operator dispatch

2026-10-13. Separate whole Main input/dispatch unit, following UI and session
state owners. Reviewed raw architecture and literal Main/terminal/native input
source plus existing cli/ui_terminal/session fixtures ground this work. No new
reader, timer, encoder, normalization, dependency or policy. Root rough earlier
session/operator sketch incorrectly mentioned getline/8192: actual Main already
polls and reads512 bytes with a1MiB pending-line bound. Preserve that machinery.
Read-only inspection/discovery never starts a recorder; pre-runtime option/open
bootstrap remains the declared existing raw activation boundary, not fabricated
successful raw acquisition. nls.10 audit-wide bootstrap work is separate.

E16/R24/native fields. Actual startup1000 Ref selects Main callers once its runtime
is active, including TUI worker calls on their own lane. Immutable Ref survives
startup region completion. Caller context never derives parent from last TLS event.
4200/4201 whole input request/end:parentR,u8 kind(once1,plain2,TUI3,resume4),
size_t actual prompt length. Request49+terminal52 paid before business dispatch.
4202 actual unrendered input original:inputR,size_t N,bytes;48+N DATA. Capture
existing borrowed prompt before parsing, setting allocation, workflow file read
or operation admission. Initial TUI/resume draft similarly preserves a real4202
original before UI run and gives RawUiSource(actual4202 Ref,logical original payload offset0,length).
No false source is made from an empty/default reference; zero-length original is
still a real valid event. UI's actual queue source is independent of Main's later
actual dispatch request; both lifetimes are recorded, not conflated.

4203/4204 dispatch request/end:inputR. Same52 terminal and stage nativeu8. Invoke
actual existing operator branch body inside dispatch.run before existing Error
handler. Therefore Error handled by interactive Main still has a failed dispatch;
outer input can return after its error presentation/backstop as business does.
Unknown slash is actual returned dispatch, not invented accepted native operation.
4205 selected branch:dispatchR,u8 kind. E16+R24+u8=41; prepaid41
BEFORE each reached branch body. Static branch tags document existing branches,
not parsed/normalized metric names or a new command classifier. Emit only at the
entered existing branch; keep short circuits. Raw branch is selected before its
subsequent parse/copy/settings/engine call, so malformed inputs have real selection.
Method context passes actual dispatchR to selected settings, identity, snapshot,
listing and file helpers. Refresh-info invoked after dispatch gets the same owner,
without changing secret exclusion in auth flows. Auth secret entry stays external.

4230/4231 plain read request/return:sessionR,int fd,size_t512; native result
requestR,ssize_t rc,int saved-error only ifnegative,[positive unchanged bytes].
Pay request52 + maxreturn52 CONTROL and DATA52+512 BEFORE existing ::read. Read
bytes emitted before1MiB bound check/append; actual native EOF/EINTR/error remain
separate. No fabricated bytes on negative/EOF. Raw only captures existing read;
line splitting and /paste business assembly remain unchanged. Native poll
4232/4233:sessionR,int fd,short events,int timeout40; returnrequestR,int rc,int
error on negative,short revents. Pay request50+maximum50 before poll. No timer
added, no extra poll, no clock subtraction. Existing idle service/interrupt behavior
remains. Stream parsing/line allocations fail after their acquired raw512 bytes
are retained. Explicit refusal before read has zero fixture input consumption.

MI1–MI3 disposition:4205 records these existing outer entered branches:
1help,2exec/process,3fg,4jobs,5bg,6resumeContinue,7tasks,8runs,
9run/send/join,10await,11stop,12authStatus,13authProviders,14authUnsupported,
15variables/correlate,16gitUsage,17gitObserve,18trace,19colleagues,20colleague,
21session,22name,23resume,24new,25restart,26workflow,27context,28sessions,
29checkpoint,30stats,31compact,32inspect,33originals,34restore,35model,
36effort,37decision,38beads,39lua,40workflowFallback. Internal runs/Beads/
variables branches keep their actual existing native lower owners;4205 introduces
no second classifier. Branch40 is paid/emitted in an enclosing final else BEFORE
the original effectful operator_turn condition. Unknown slash reaches40 as before.

4206 handles actual plain pre-dispatch controls only:sessionR,u8 kind(quit1,
exit2,paste3,send4),size_t N,actual control bytes; DATA49+N paid/emitted before
break/clear/assembly. No4203 is fabricated for them. Send is emitted only when
the existing paste inner condition actually sees it; an ordinary /send argument
continues to the existing participant branch. Actual unbounded paste assembly
is unchanged. An explicit RawCaptureFailureScope is closed at the synchronous
owner run boundary, and actual marked raw errors are required before Main's
business fallback/backstop or refresh warning. Native business errors with the
same ErrorCode retain their existing behavior. No process-wide flag is added.

Selected native read/poll helpers wrap exactly the existing syscalls and native
arguments. Positive4231 body is requestR,ssize_t rc,actual bytes:48+512 DATA;
nonpositive return is CONTROL48/52. Both return grants are preheld before read.
No saved errno is read from successful output fields or appended on success.
The plain transport originals precede subsequent line allocations. The4202
assembled input originals precede all later setting/dispatch work. Default/null
context directly calls the old syscall and emits nothing.

4210–4259 session-state sites remain separate; session listing returns actual4225
Ref for UI kind1. Root wires ui.raw_capture(tui) before its first post/copy; initial
Task snapshot uses genuine1708 completion and native revision as UI kind2. Changed
TaskStore callback supplies actual1709/application/revision during the callback,
restores prior metadata on nested reentry/unwind, outside callback returns absent.
Participant worker activity uses explicit actual2130 Ref parameter, never shared
engine getter/TLS guessed parent. UI kinds3/4 consume those typed origins without
Value visits. No metrics subscriber callbacks are added.

Direct published-source red: real --once /help and piped --plain /name/invalid
command produce normal behavior but lack4200/4202/4203 and423x. Real candidate
fixtures compare literal native bytes (including NUL/FF, split lines, /paste and
unterminated EOF), actual entered branch/error, unchanged saved settings/read-only
inspection, explicit once/resume/TUI origin and native read counts. The UI owner
checks native keyboard/queue lifecycle separately. Exact ref linkage + branch
outcomes, real admission refusal before input read, partial/read/EINTR/error and
allocation after acquisition cuts use existing native fixture seams. No workload
throughput/startup/consumer contention measurements. One code/oracle review and
finite recheck, then source gate, stop before qualification discussion.

The direct Main failure fixture compiles the actual Main source with only its
provider type replaced by a finite native CodingProvider fixture; no network or
operator credential acquisition. A forwarding native open at the second actual
workflow read holds the creating producer's real remaining CONTROL with finite
try grants. The subsequent selected read admission must refuse before reading.
The existing real close releases those grants before Main's catch, so recording
capacity is available again and only the actual origin distinguishes this error
from eligible business failure. Require zero provider/backstop and preserved
input/dispatch Error; separately an ordinary Lua error must enter backstop and
reach the fixture provider. Direct helper DATA grant-minus-one must leave the
real pipe bytes unconsumed. No production injection API is introduced.

### Final gate branch observer race: exact crash and finite correction

The actual gate's SIGABRT is classified by
`~/Library/Logs/DiagnosticReports/main_input_native_test-2026-10-14-030509.ips`.
Its active stack is main_branch_refusal → SessionStore::save → retained append
publication → RawNativePublisher::finish → terminate. main_acquired_prefix had
already caught its genuine bad_alloc and completed its assertions. No interrupt
teardown defect is established. The fixture's throwing send observer reads from
an acknowledged cursor snapshot while the real recorder can advance that cursor;
RawChannel::read legitimately rejects the stale start with invalid_range. Throwing
that fixture Error across the actual noexcept publisher causes the abort.

Inside its own send callback the single producer is parked, so its publication
end and record bytes cannot be overwritten by another publication on that lane.
The observer can retry a stale start only when a new cursor proves monotonic ACK
advance and the same publication end. If ACK has reached that end, it must forward
the original native notice exactly once and select that same control record from
already-preserved disk bytes: exact lane, CONTROL partition and source_start plus
native record length equals the captured publication end. Recorder publishes its
physical cursor before ACK. Read-only RawRunReader defaults opt out of product
storage capture, so this acquisition cannot recursively publish. DATA/IPC notices
must not be mistaken for a new control publication.

Every observer exception is retained as exception_ptr inside the fixture Csend
wrapper, any held fixture grants are released, and the exact exception is rethrown
at the test's ordinary boundary after invoke. No production Error is suppressed
and no noexcept production API changes. The physical cut still leaves40 CONTROL
bytes before the actual41-byte Main branch grant; exact owned input/dispatch
Error(capacity), absent branch/effect and all prefix512/closed/no-dispatch assertions
remain unchanged. One matching Main native test and narrow fixture static check
complete this same finite correction; no new fault campaign or panel.

The narrow fixture static check also exposed run_session's two interchangeable
successor-output vector references in its included Main source. Resolve that
finite Main/handoff interface with one private SessionSuccessorOutputs aggregate
borrowing named next/fallback references; every Main/embedded-fixture caller uses
designated fields. No vector copies, public API, wire or class-layout change.
Keep selected_handoff separate and unchanged. Existing native Main test checks
all original/prefix/branch and selected handoff outcomes after matching app/fixture
link, with Main plus fixture static checks clean; no warning suppression.
