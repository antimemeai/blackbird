# Terminal custody and selected plain output: remaining native owners

This completes the remaining H census in the system-surface study. It does not
change the recorder's startup or lifetime policy. The completed UI unit owns
message/composer/queue/task/presentation/frame acquisition, worker transfer and
ChatOutput native writes. It does **not** yet own TerminalMode, terminal_control,
editor handoff, the wake-pipe acquisition/restoration, or plain Main output.
Those omissions are actual native-owner gaps, not covered by Main input or BBM
encoding. This document proposes two separate conceptual units. Neither is a
new generic tracer: both use RawSessionCall/RawEmission and raw_publish[_linked].

## Source census and boundaries

| Actual owner | Native surfaces | Existing custody / disposition |
| --- | --- | --- |
| TerminalMode | tcgetattr; raw tcsetattr; screen-mode control; suspend/resume; restoration | Owns the original termios and creating thread from TUI entry through lexical cleanup. New H unit. |
| terminal_control | actual literal output packet; ChatOutput attach/start/flush/detach; existing poll/deadline | Existing unselected local writer. Select this actual writer and give it the preserved control packet; do not duplicate its native writes. New H unit. |
| TerminalUI startup/wake | setlocale result; pipe; descriptor normalization; CLOEXEC/nonblock fcntl; sigaction install/restore; close | Acquire grants before process_spawn_mutex / ui.mutex. Store cleanup grants beside actual descriptors. New H unit. |
| editor handoff | suspend → private draft → editor launch/wait → read result → resume → tcflush | Native business operation and lexical cleanup. New H unit. |
| editor draft | TMPDIR/TMP/TEMP/TEMPDIR selection, path, mkstemp mutation, low-fd normalization, fcntl, writes, close, open/fstat/read, unlink | Current editor has its own direct native owner. Selected trace must preserve its actual originals and failures. New H unit. |
| editor child | VISUAL/EDITOR/default selection, actual shell command/argv, posix_spawn return+pid, waitpid retry and raw status | Exactly one actual child, no collector/restart/new registration. New H unit. |
| UI frame / ordinary keyboard / notification writes / worker transfer | Existing4000–4084 UI and native command ownership | Completed earlier; reuse actual source refs, no second occurrence stream. |
| UI outer poll / notification drain | poll of existing input/wake/output descriptors; actual128-byte notification reads and EOF/errors | Currently uninstrumented. New H wake owner records native requests/actual outcomes and positive chunks; no signal-count inference. |
| resize_signal | existing async-signal-safe self-pipe byte | No allocation, recorder publication, binding or lock in the handler. Owner drain is observation of wake bytes, not a count of signals; it does not record the native handler write or its failure. That actual handler request/return needs an existing RawSignalPublisher prepared before signal installation, without TLS lookup or a producer per event, as required by Root review. |
| Main emit/status/outcome/plain greeting/prompt/newline | business formatting and actual stdout/stderr write | Separate synchronous plain-output unit, Root integrates Main. |
| pre-runtime help/version/discovery/auth/errors, read-only inspection | stdio without a selected running recorder | Explicitly outside this nls.2 recorder boundary. nls.10 audit-wide/bootstrap remains separate. |
| top-level diagnostics after run_session has unwound Runtime | stderr, prior-session restart/exec failure | Outside selected lifetime here; Root successor4400 owner decides its already-planned postrun custody. No Runtime lifetime expansion. |
| arbitrary library/third-party stdio | unowned incidental writes | No global stdio interception. Only exact owned sites are selected. |

The old census's proposed resource sampler and arbitrary mutex wait metrics are
not part of H completion. Neither ops nor evals emit these events; they consume
raw recorded events after preservation. No clock/timer/scheduler is introduced.

## Reference study and resulting choices

The acquired 4.3BSD Reno kern_ktrace.c separates syscall arguments, syscall
returns, and generic I/O copied bytes; kdump interprets them later. Its
ktrgenio deliberately skips failed I/O: our owned source protocol instead
preserves input proposals plus failed native request/returns. We use its raw
copy/off-writer decoding division, not its drop rule or blocking kernel writer.
The Berkeley termios manual already in quarantine states the session/process
and terminal-custody distinction; this unit changes outer UI termios, not child
foreground job ownership. The acquired write(2) primary manual requires actual
short results and EINTR to remain distinct attempts. The existing owned
ChatOutput is therefore the single terminal-control writer; a preserved packet
is not proof that the terminal accepted it.

The acquired libc++ filesystem operations.cpp source is inspected at
__temp_directory_path: on this supported POSIX path it chooses the first
**present** TMPDIR, TMP, TEMP, TEMPDIR (not first nonempty), defaults /tmp, then
stats and requires a directory. The narrow owned editor acquisition will retain
that behavior while exposing its actual source extent before allocating the
path. No new dependency is adopted. Reference:
https://github.com/llvm/llvm-project/blob/main/libcxx/src/filesystem/operations.cpp
(the acquired snapshot in quarantine/file-native-2026-10-11 is the local study).

## H unit: exact native publication contract

Use4100–4179, subject to Root allocation confirmation. E=16 native raw envelope,
R=24 (lane16+ordinal8), S=sizeof(size_t), I=sizeof(int), B=sizeof(bool),
P=sizeof(pid_t), T=sizeof(tcflag_t), C=sizeof(speed_t), L=sizeof(cc_t) on Linux and0 on Darwin. All records are static
native fields, no structure padding. Existing shared generic terminal fields are
R,u8 RawOperationBranch,u8 stage,bool Error,[ErrorCode,i64 detail]. Actual syscall
outcomes use result of the declared native type, bool failed, and **only on
failure** native int error. posix_spawn uses its returned int as failure code,
not ambient errno; waitpid preserves raw int status only when a child returned.
No unbounded what() string becomes metrics.

| Sites | Literal native fields after existing owner R | Required source and paid credit |
| --- | --- | --- |
|4100/4101| whole custody method u8(acquire1,resume2,suspend3,cleanup4,editor5,wake6), generic terminal | E+R+1 request plus shared max terminal; methods never relabel cleanup failure as successful restore. |
|4102/4103| tcgetattr request fdI; return resultI,failedB,[errnoI], then returned termios fields on success | Request+max result preheld before native call. Successful termios fields: four flagT; bool line-discipline member present,[native c_line cc_t only Linux]; all c_cc bytes at native known sizeof(before.c_cc); native c_ispeed/c_ospeed member valuesC on the supported Darwin/Linux owner layout. Do not add cfget* calls solely for instrumentation. Use existing member values, not whole padding. |
|4104/4105| tcsetattr request fdI,actionI, four flagT,line-presentB,[c_lineL],c_cc bytes,two speedC; return native resultI/failedB/[errnoI] | Q=E+R+2I+4T+B+L+sizeof(c_cc)+2C; O=E+R+I+B+I maximum. Prehold Q+O before modification; constructor fallback and destructor restoration own separate preheld Q+O. |
|4106| actual custody state methodu8,activeB | E+R+1+B, prepaid per actual mutation; record actual active state even when control output subsequently fails. |
|4107| literal control packet bytesS,original | DATA E+R+S+literal.size before packet acquisition; actual RawUiFrame wrapper provides existing4041 packet copy and distinct actual4042 lifetime, not a second copy4107.4107 is replaced by that existing original, referenced from custody. |
|4108/4109| poll request fdI,events short,timeoutI; actual resultI,failedB,[errnoI],revents short only nonnegative | Preheld per existing retry; no new polling cadence. Control helper's existing150ms deadline is business behavior, unchanged. |
|4110/4111| wake native request opu8(pipe1,fcntl2,close3,sigaction4), exact native arguments; actual return | Existing pipe returns both fdI on success, not speculative values. fcntl request fdI,commandI,argI; no incorrect borrowed native flags. close request fdI. sigaction uses signalI plus actual handler identity enum (resize1/restore2), native flagsI and native mask bytes of **sigset_t** initialized by sigemptyset; original opaque handler **native function pointer**+native flags+mask from actual sigaction return. No pointer is dereferenced/serialized. |
|4112| actual descriptor custody opu8,fdI,ownedB | prepaid actual transfer/normalization/retirement. Cleanup bank retains creator issuer and lexical order. |
|4113/4114| tcflush request fdI,queueI; actual return/error | Q+O=2(E+R)+3I+B+I. Actual result recorded before returned draft can enter composer; business ignores failure as before. |
|4115/4116| whole UI poll request nfds_t count, three actual fdI/events short pairs,timeoutI; actual resultI,failedB,[errnoI],three revents short only nonnegative | Exact max CONTROL2*(E+R)+sizeof(nfds_t)+4I+6*sizeof(short)+B+I before poll; existing business timeout evaluated once, no new timer. |
|4117/4118| notification read request fdI,requestedS; actual ssize_t,failedB,[errnoI] | Before each native read, CONTROL2*(E+R)+I+S+sizeof(ssize_t)+B+I and DATA E+R+S+128. Positive actual chunks copied before next drain attempt. Failed/EOF has no invented original. |
|4119| notification readR,sizeS,actual bytes | Actual positive128-byte bounded native read, not count of SIGWINCH; source before discarding. |
|4120/4121| whole editor input draftS,original; generic final outcome | DATA E+R+S+draft.size before first temp/path effects; the1MiB check still happens before effects, records refusal truthfully. |
|4122| source roleu8(temp environment1,template2,created path3,editor env4,shell command5,returned draft6),presentB,[bytesS,original] | Actual borrowed env extent is acquired once with the native business CString length used by path/command construction; DATA E+R+1+B+S+N held **before** that owned construction. Absent env has no bytes/extent. No fresh metric scan, clip or stringify. |
|4123/4124| editor native opu8(mkstemp1,fcntl2,write3,close4,spawn5,wait6,open7,fstat8,read9,unlink10), native input fields; native return/error | Exact operation table below; each request and maximal actual result prepaid before native effect. Input/read packet originals use4122 actual sources. |
|4125| editor result acceptedB,returned text source presenceB,[sourceR],message literal enumu8 | Actual result only after underlying return construction succeeds. Static existing diagnostic choices are enums, no metric formatter; the business dynamic catch message remains business data with selected failure-origin rethrow first. |

For each syscall under4123/4124 use a common **static field specification**, not
another general owner implementation. raw_publish[_linked] owns publication;
RawEmission owns request/bank/end. The editor wrapper supplies only these fields:

- mkstemp: mutable template sourceR; int return/failed/error; copied actual changed
  path has preheld source length equal input template native length, captured even
  on failure (it is the actual mutable output buffer, not assumed success).
- fcntl: fdI,commandI,argI → int. A close for replaced low fd is a distinct effect.
- write: fdI,input sourceR,offsetS,requestedS → ssize_t. Full draft already copied;
  actual positive source extent and failures remain separate, no per-write copy.
- close/unlink: fdI or created-path sourceR → int. Never retry close; existing
  destructor ignore policy stays, actual unsuccessful return still recorded.
- spawn: actual command sourceR, literal executable/argv native bytes (sh,-c),
  environ pointer value only (no environment tree scan) → int failed code and
  pidP only on success. It is a proposal/actual distinction, not an environment
  snapshot. Existing launch mutex unchanged.
- wait: actual childP,optionsI → pidP,failedB,[errnoI],[raw statusI if returned>0].
  Each EINTR request and outcome stands alone.
- open: created-path sourceR,flagsI → int. fstat: fdI → int/failed/error and native
  mode_t+off_t only success. Existing regular-file predicate unchanged.
- read: fdI,requestedS → ssize_t/failed/error. DATA E+R+1+B+S+4096 **before** each
  actual read, then positive actual bytes copied before1MiB check/edited.append.
  Failed/EOF reads have no fabricated positive original. Oversized returned bytes
  survive the business refusal. Final editor text refers to actual acquired raw
  read chunks plus its exact returned original, source-funded by1MiB native
  business limit before accumulating/copying final result.

The editor temp-directory resolver obtains the real first-present environment
CString extent as business path acquisition, holds exact source/path grants,
constructs the same path and uses raw_session_stat on it, preserving libc++'s
non-directory/stat-error behavior. Its template bound is native base size plus
one separator plus sizeof("arco-draft-XXXXXX")-1. Actual mkstemp path length is
that template length. Shell quote bound is2+4*path.size (each apostrophe expands
four bytes, other characters one). Command bound is5+editor.size+1+quoteBound.
Acquire before quote/command formatting; then copy the actual command unchanged.
This is existing business shell formatting, never an instrumentation transform.

## Cleanup and locked acquisition equations

One invocation preholds editor cleanup **before mkstemp**, enough for at most
three distinct actual closes: low-fd normalization old descriptor, draft writer,
reopened reader; plus one unlink and native returned custody field per effect.
For each close Q=E+R+1+I; O=E+R+I+B+I; custody K=E+R+1+I+B.
For unlink Q=E+R+1+R; same O,K. Thus cleanup bank is3*(Qclose+O+K)+Qunlink+O+K.
If a cleanup operation never becomes owned, its unused prepaid bytes release.
No ordinary claim occurs during unwinding; cleanup uses the retained issuer,
existing thread rule and RawWaitControl{} for publication of already acquired
state. No new cleanup mutex or thread registration.

Before process_spawn_mutex, wake bootstrap holds one pipe Q+O (two actual fd
on successful result) + at most two low-fd fcntl Q+O + two replacement close Q+O
+ four protection fcntl Q+O + two actual failure cleanup close Q+O. Branch-specific
credits split from this exact bounded bank. The lexical WakeCleanup separately
preholds restore sigaction Q+O and two close Q+O+K before pipe acquisition.
Sigaction grants account for actual sigset_t, function pointer and native flags
sizes. Cleanup resets resize_pipe before restoring the actual prior
handler, clears ui.notification under existing mutex, and closes actual fds.
The current WakeCleanup always restores its zero-initialized `before` even if
installation failed. That value is not an acquired prior handler. Keep actual
installation success as resource ownership and restore only when successful;
failed installation must not emit a fabricated previous handler or modify one
that was never acquired. This is a confined native-custody bug fix, explicitly
part of the plan attack.
Do not add a blocking claim under either lock. Initial pipe failure has no fd
cleanup records. Partial protection failure records the two actual closes.

Before the editor spawn mutex hold its single launch Q+O; before the private
file-create mutex hold its mkstemp request/outcome/pathoriginal and max existing
normalization/protection requests/outcomes. Every repeated read/write/wait/poll
iteration claims before that next actual business operation, not a magic bank
for unbounded retries. Result publication and lexical cleanup do not wait on
ordinary admission. Admission/refusal origin remains distinct and escapes the
editor's ordinary std::exception fallback via RawCaptureFailureScope::require.

## H direct native oracle and implementation sequence

Baseline RED uses actual TerminalUI PTY Ctrl-G handoff and helper fixtures: real
terminal flags restore; actual shell/editor modifies a private draft; raw reader
finds no selected H native requests. No mocked tcgetattr/write/spawn/cleanup.
Implement the complete custody owner after ONE Root plan attack, strict syntax
and narrow static checking. Reuse existing UI fixture native cuts and PTY runner.
Independent source→preserved-byte greens then require:

1. PTY actual initial/suspended/resumed/final termios agrees with recorded native
   fields; screen entry/exit actual packets and accepted byte counts remain
   linked, including a blocked actual pipe (EAGAIN) and restored stdout flags.
2. Actual editor script writes literal changed draft; complete output and actual
   read chunks agree with resulting composer/disk original. VISUAL precedence,
   absent/default editor, empty-present values and apostrophe path quote use
   actual inherited environment and actual shell, not a metric reconstruction.
3. Actual child nonzero/signal exit, unavailable editor, invalid/nonregular output
   and1MiB+1 returned source preserve originals, native returns and cleanup;
   no false accepted draft. fstat/close/unlink/status are independently observed.
4. Actual forwarding allocator denial during command/path construction or draft
   append retains already copied native source, paid exception and lexical
   cleanup; selected raw DATA/CONTROL refusal occurs before effect/acquisition
   and propagates its actual origin instead of a benign editor diagnostic.
5. Cold creating-issuer scope cleanup and nested different binding preserve actual
   original lane and restore prior binding. Cross-thread methods refuse before
   custody effects; no new destructor thread behavior beyond existing UI rule.

No random workload, capacity campaign, timers, duration normalization, sampler,
qualification, or performance claim. Exact finite cuts test these owner faults.

## Separate plain-output owner plan

Root owns Main integration. Selected source sites must pass the existing startup
context or actual dispatch parent to one synchronous helper using RawSessionCall.
Do not globally intercept stdio. Use4340–4379 subject to Root collision check.

The actual business format remains at its exact source: safe(text), brackets,
newlines and welcome/prompt composition. Source-fund **before** constructing the
actual formatted string: safe bound4*input.size; status/outcome bound4*N+3;
prompt/literal boundsizeof(literal)-1. For dynamic welcome fields, acquire each
existing native component once and sum its actual bytes plus exact literals;
no extra conversion such as .string()/Number.text solely for metrics. Existing
formatters like dump_json/correlation_plot/terminal_help remain business returns;
pass their already-owned exact result to the output helper, or prehold their
established source bound at the business acquisition when they are selected.
Never present a speculative formatter original as a source that was acquired.

4340 request contains parentR,fdI,sourceclassu8(emit1,status2,outcome3,greeting4,
prompt5,newline6,warning7),bytesS,actual complete business write proposal.
DATA E+R+I+1+S+N is preheld before source acquisition.4341 terminal is generic.
4342 native request names4340R,fdI,offsetS,requestedS.4343 actual return is
requestR,ssize_t,failedB,[errnoI]. Per attempt exact CONTROL maximum is
2*(E+R)+I+2*S+sizeof(ssize_t)+B+I. Return zero, short, EINTR and other failure
are distinct; only actual positive bytes advance the existing write offset.
The helper writes the already-formatted proposal through ::write to the actual
native descriptor, source already preserved; no raw transformation/buffering
policy, metric formatting, new producer or second writer. It is a synchronous
owned output path; no frame labels on plain output.

This replacement must make failed stdout/stderr behavior explicit: current
std::ostream defaults latch failbit and may silently drop later output; direct
write must record the native failure and throw the actual owned Error instead
of reporting a successful accepted packet. This is an existing actual-output
deficiency exposed by instrumentation, not grounds for inventing successful
writes. Root's existing selected Main failure-origin boundary propagates capture
refusal; business write failure reaches its ordinary command/session error path.
The ONE plan attack must approve that concrete behavior change before edits.
TUI callback paths continue to their existing typed native UI owners; they must
not also traverse this plain helper.

Plain-output direct RED/green uses real --plain/--once and actual pipes/PTY, known
literal command output and sanitized control bytes. Independently read the native
pipe bytes and raw disk source, require equality of actual proposal/native
positive ranges; include short writes, closed reader/ignored SIGPIPE actual EPIPE,
actual source-admission refusal before write, and std allocation failure before
safe formatting. Private fake provider only at a genuine provider boundary;
no network request is needed for authored/tool-only cases. This is a second
whole owner and gets its own ONE plan/code review, not a pooled leftovers pass.

## SAME H/P plan attack disposition

H1: use the existing qualified RawCommandInterrupt native action/mask fields and
credit constants, including Linux's actual8-byte64-signal mask and restorer
pointer; Darwin's supported actual mask. Export the existing raw_command_action /
raw_command_mask implementation, with no second serializer. Restore only an
actually acquired previous action, fixing the old unconditional restore. Native
sigaction/mask2062–2065 records retain their existing literal layout and errno
meaning. New H records identify the actual associated custody; no uninitialized
libc tail or padding is copied.

H2: SIGWINCH gets a **separate** RawCommandWake::Mode::signal endpoint using the
existing prepaid native publisher and lock-free notification state. The ordinary
worker notification pipe stays separate. The owner installs an atomic handler
target, records the native action through the same existing helper, and adds the
resize endpoint to its existing poll array. The handler sets the sticky resize
flag then calls the existing notify; it neither binds nor registers nor allocates.
Every actual reached native wake operation has the established2056/2057 records;
already-pending resize notifications coalesce because the business reads no count.
The owner legally rearms through clear after observing that endpoint; if the flag
raced rearm it notifies the new incarnation, following RawCommandInterrupt's
existing reset rule. Poll's native nfds is now4, and the field/grant equation is
2*(E+R)+sizeof(nfds_t)+5I+8*sizeof(short)+B+I. No new polling cadence.

The handler target is removed and the acquired prior action restored when run
ends. The actual target object, wake, producer and native credits are retained as
a TerminalUI member until the existing Main ParticipantShutdown has joined all
command/participant workers before UI destruction. Thus a worker that loaded the
old target can finish against a live object. Final UI destruction blocks its
creating thread's SIGWINCH, retires/destroys the retained wake, then restores the
actual prior mask using preheld mask credits. No new join or cleanup lock.
Existing RawCommandWake::reference exposes only its actual native incarnation
for the owning thread. Main supplies literal actual4200 parent via raw_parent;
ordinary direct APIs without an explicit parent remain opt-out. No inferred
initial-text ref or producer is substituted.

H3: editor's whole RawSessionCall encloses the native cleanup local, so returning
EditorResult first constructs the actual business result, executes its cleanup,
then closes the paid method terminal. Acquired positive native read chunks are
copied before append/limit branches. Before the final1MiB-bounded returned draft
acquisition/copy, reserve its source grant and the fixed accepted/source/result
fields; the output text is copied before moving into EditorResult. After any
actual allocation/Error/class exception, shared exception dispatch ends the
paid editor method. The outer ordinary diagnostic catch first requires the real
raw failure origin; selected raw errors escape instead of becoming benign
messages. Native positive fd/path ownership is installed in the cleanup local
before a potentially failing return/source publication. Native return publication
uses shared raw_publish Result, so the actual acquired result remains owned even
when publication fails. Each cleanup effect preserves its real errno and uses its
preheld bank; no false successful close/unlink or accepted returned draft.

Root approved bounded best-effort terminal destructor output in this SAME review.
Prehold actual exit-literal DATA48+literal.size and CONTROL frame247+attach378+
8*113, plus native tcsetattr request/outcome and state. The destructor performs
one existing ChatOutput flush batch (at most8 native writes), retires any remaining
packet through its already-paid native terminal with actual offset, and ALWAYS
attempts the paid termios restore. No ninth effect, unbounded destructor grant or
poll/timer. Public suspend/resume retain their existing150ms business cadence.
RawUiFrame accepts an explicit preheld DATA bank for this actual owned literal;
its CONTROL already uses the explicit existing RawUiBurst loan. This is a narrow
source-funding seam, not a second emission owner or generic admission scope.
A real interrupted/partial8-write fixture checks the preserved original, actual
positive ranges, retirement remainder and independently observed restored flags.

P1: plain output is separate. Borrow the actual existing business strings and
literals as raw span parts, not an extra joined greeting/status packet.4340 is
one actual DATA request: parentR,fdI,kindu8,total bytesS,existing raw byte parts,
E+R+I+1+S+N =53+N on supported native widths. Adopt that actual request and the
preheld52-byte generic4341 terminal bank into RawEmission.4342/4343 retain the
exact max113 native per-piece request/return grant; each write names the actual
proposal and global actual offset. Do not introduce writev or a formatter merely
for instrumentation. Preserve emit's actual original input at separate4344
before the SAME existing business safe formatter, with4*N source bound reserved
before construction. The implementation must also prepay its format failure
outcome before that acquired request exists; shared owner fields, no invented
zero request. Source pieces/count/length are transport framing only.

P2: real write Error replacing silent ostream failbit loss is authorized by the
operator's bug scope. Production signal policy stays; the native EPIPE fixture
explicitly ignores SIGPIPE. Bootstrap/read-only/post-teardown boundaries remain
outside this selected recorder, with nls.10/successor ownership explicit.

## SAME H implementation refinements

Actual owner source resides in terminal_custody.cpp/hpp. Literal native field
suppliers call shared raw_publish_linked; no second transport, end classifier,
bank manager or generic tracer is introduced. Editor's4120 is an actual DATA
request emitted/adopted through existing RawUiTrace; its4121 uses that adapter's
existing UI outcome layout.4122 is R,roleu8,presentbool,[size_t N,original bytes];
role7 is positive native read chunk,8 actual directory path,9 actual returned
business exception diagnostic. RawUiTrace's borrowed current credit remains
open through local cleanup; its final classification follows actual construction
and cleanup. Actual locale request4126 is R,int category,NUL char;4127 is R,bool
present,char* actual returned opaque value. No returned locale string is scanned.

Actual first-present temp environment source is preserved before path construction;
actual owned directory path has source capacity held from that same source extent
before construction. The SAME native stat follows symlinks like libc++ and its
request4123 method11 names directory sourceR, result4124 includes mode_t only on
success. Static errno is used only on actual failure; an actual non-directory
uses ENOTDIR. No lstat substitute changes temp-directory behavior.

Resize construction allocates/retains its endpoint before install. Actual handler
installation occurs only after TerminalUI's member owns it and the lexical
uninstall guard exists. A selected terminal-publication failure after installation
therefore uninstalls the handler but retains its borrowed endpoint until the
existing ParticipantShutdown joins workers and UI destruction retires it. Native
custody method8 names this actual separate installation. Native notification pipe
setup also closes actual acquired descriptors when the owner terminal itself
fails. TerminalMode handles a failed constructor terminal by paid actual restore,
without relying on a destructor for an object whose construction failed.

The acquired child guard is prepaid before spawn, and actual PID/owned/reaped
state is installed before publication. On reached recording/admission failure,
cleanup uses the existing qualified mask emitter to block handler signals on the
current thread (sigfillset), sends SIGKILL to only that actual positive editor PID,
performs one blocking native wait, and restores the saved mask. Every native
result remains distinct; no retry/new admission happens in cleanup. If blocking
or waiting fails,4128 records truthful pending ownership rather than claiming a
reap. Actual ECHILD retires ownership without killing a no-longer-owned PID.
Native4123 method12 is pid_t,int signal;4124 is int/failed/[errno]. Ordinary and
cleanup method6 wait fields remain identical.4128 is R,pid_t,bool owned,bool
reaped. Cleanup exact credit is two existing mask_credit plus kill Q/O plus wait
Q/maxO plus two4128 state records. The primary errno is restored after cleanup.
This closes the real post-spawn-publication/next-admission ownership edge under
Root's SAME H plan disposition, without a timer, group containment or new child.

The native flag oracle retains complete raw F_GETFL values, but compares only
owned configuration. XNU exposes kernel FWASWRITTEN0x10000 after an actual write;
its F_SETFL changes only FCNTLFLAGS and cannot clear that history. The independent
native pipe observation therefore masks that single kernel-history bit when
comparing entry and exit. The preserved metric fields remain unchanged. Source:
[pinned XNU fcntl.h](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/sys/fcntl.h)
and existing acquired kern_descrip.c. Header acquired intact beside that source;
restoration and acquisition manifest updated. Darwin termios PENDIN likewise
tracks kernel pending-input state; all owned flags/control bytes/speeds are
compared, with actual original/restoration fields still preserved literally.

### Same whole-code review HC1–HC4 final source corrections

Resize observation binds its retained creator; explicit unselected resize owners
use an empty existing RawThreadBinding for their entire native wake/action/mask
lifecycle. Source roles1/2/3/4/5/8/9 are complete existing Cstrings including NUL,
so DATA bounds gain one byte before acquisition/construction. Draft4120 and
roles6/7/10 remain counted byte spans. Constructor rollback owns one attempt:
actual tcsetattr result determines active state, then4106 is paid from the held
set-request/return/state grant. A failed restore remains active; no outer retry
or additional ordinary admission. The exact prior cleanup equation funds this
single set/state pair. Direct same-unit finite checks and limits are recorded
in the whole review's owner disposition.
