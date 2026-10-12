# Native command/UI/SIGINT wake ownership — proposed, not implemented

Status: root ONE plan attack completed; W1/W2 resolved in
2026-10-11-command-wake-raw-plan-attack.md. Whole-unit implementation released.
Current-source native red precedes replacement; root ONE code/oracle attack follows
direct greens. ContextStore implementation remains held.

## Concrete defect and complete boundary

RawCommandWake currently pipe/write-once/read-drain. notify discards write's actual
return and errno; EINTR can leave a raw-capacity waiter parked despite sticky business
cancellation. Closing the writer is insufficient when an actual stopped fork child
retains a pre-exec writer alias. clear discards read errors and drains the same pipe.
main's SIGINT handler duplicates the ignored write independently of notify.

Owners are (1) per-Job one-shot stop, (2) TerminalUI reusable per-turn cancellation,
(3) main's reusable session-wide SIGINT. Job waiters borrow ONLY the job stop fd.
The UI's main wait and outer main wait retain the global fd number across turns;
turn workers borrow global + per-turn fds. Therefore rearm must preserve the owned
numeric descriptors while making the changed socket incarnation explicit.

The whole unit replaces pipe activation with one actual connected AF_UNIX/SOCK_STREAM
shutdown(SHUT_WR), retains raw request and native return separately, includes pair
setup, fd normalization, rearm dup2/CLOEXEC repair, and late closes, and settles prompt
ownership on rearm refusal. No new timer, thread, broker, SDK, registry, transform,
retry loop, dependency, signal-mask change to workers, or invented successful wake.

## Source coordinates and current lifetime

* include/blackbird/raw_command.hpp and src/raw_command.cpp102–142: API, setup,
  destructor, notify, clear. raw_command_description receives sites2050–2061.
* command_jobs.cpp Job~309/314, caller_stop~340, run~435, worker_stop/deadline~639,
  output limit~878, start~1100, archive~1459, control-stop~1490, shutdown: one-shot.
  Create/hold the wake on job.raw_context BEFORE worker dispatch. Constructor currently
  precedes that binding and must move inside it. Job keeps setup/end credits and handle.
  caller cleanup lane currently57 first2029/job; worker2162/initial4658.
* terminal.cpp1286–1302: turn_cancel, ui_wait, worker, StopOnExit. Wake already outlives
  worker; StopOnExit runs before auto-join. start1474 joins old worker before clear;
  complete1659 joins worker. queued1719 is destructively removed BEFORE start, and
  keyboard2337 moves an accepted composer draft into start: both must preserve input
  until rearm/start admission succeeds. Initial1563 needs the same result contract.
* main.cpp27–38 handler,422–437 global setup,530/plain reset,893 worker regular notify,
 1060/UI reset callback: replace global fd-only handler target with lock-free atomic
  pointer to the owned signal wake; capture previous sigaction and restore on teardown.
  Global remains alive through UI join, shutdown_commands, shutdown_participants.
* raw_runtime.hpp/cpp: narrow direct prepaid signal publisher seam, described below.
  raw_channel.cpp RawProducer::emit and raw_send_notice remain the underlying writer.
* No change to native_process.hpp/worker SIGINT masks/spawn defaults: Child currently
  does NOT reset inherited SIGINT mask, whereas CommandJobs explicitly does. Masking
  worker SIGINT would otherwise change provider child semantics and is unnecessary.
* Runtime supervisor forks before main's handler/wake setup; its later recorder forks
  inherit that supervisor's earlier state, not the interactive handler/wake endpoint.
  Test fork deliberately retains aliases; CLOEXEC is not a pre-exec inheritance barrier.

## Atomic native-owner protocol

A lock-free atomic state has armed/notifying/notified/rearming/failed/closed values.
Verify is_always_lock_free for its actual integral type AND the handler pointer on both
supported toolchains. An armed→notifying CAS grants sole access to the activation fd,
incarnation Ref and notification token. Losers never read/use the activation fd, never
emit another native request, and never consume another native token. Winner publishes
shutdown request, calls shutdown exactly once, captures actual int/errno immediately,
publishes return, then release-publishes notified or failed. No wake write remains.

One-shot job winner also owns one activation close request/return. Observer remains
open until the actual worker joins. Close error is recorded, not retried by descriptor
number or relabeled success; unknown descriptor closure is retained as unknown custody.
Repeated stop/deadline paths use the CAS no-op outcome without additional native calls.
Their original business2029 first-stop distinction remains unchanged.

Reusable UI/global winner leaves activation open until replacement/final closure.
Rearm is allowed only at actual joined-turn owner boundary. Owner CAS armed/notified
(or an already failed state only for final cleanup) to rearming; observing notifying
returns ErrorCode::busy BEFORE descriptor effects. No spin or wait under a mutex.
The owner must never clear cancellation again after obtaining exclusive rearm ownership.

Global reset linearizes interrupted=false BEFORE attempting the rearm CAS, as existing
reset does. A busy refusal restores interrupted=true and preserves the pending prompt.
A handler sets interrupted=true at entry, and a winning handler sets it again AFTER
obtaining native ownership. That second store covers a handler delayed before CAS
across owner reset. A handler encountering rearming only sets interrupted and returns;
owner release-publishes armed, then the existing sticky flag resync notifies the new
socket. Thus a signal during rearm either owns the new native call or is replayed as
the current sticky transition. No one emits metrics for a native call never attempted.

Stage a new normalized pair, then under process_spawn_mutex dup2 each staging fd over
its corresponding existing owned fd and restore FD_CLOEXEC with F_SETFD. Numeric
read/write descriptors remain stable for the owner's non-waiting RawWaitScope borrows.
New workers start only AFTER both dup2 calls and CLOEXEC repairs succeed. Each new
incarnation is identified by the actual2050 pair request Ref, not a derived counter.
The lock covers creation/CLOEXEC windows only; all credit is held before lock and no
claim/registration/RPC/wait happens under it. Nonblocking flag normalization belongs
to the staged pair before publication. Close staging fds with literal native events.

If a native setup/dup2/fcntl/close fails, retain its real request/result and any acquired
fd custody; publish failed, refuse the new turn, and keep the prompt. Partial dup2 is
not rolled back or called atomic. It is a failed, possibly mixed incarnation which
cannot admit another waiter. Final cleanup uses held credit. No arbitrary close retry
on EINTR: close ambiguity and fd reuse rules differ by platform; record unknown and
preserve audit, rather than guessing that number still identifies the same resource.

At main final teardown, first join the actual UI/command/participant worker families.
Then block SIGINT on the remaining owner thread, clear the global pointer, restore the
saved handler, close the native endpoints using held credit, and restore the prior
owner mask. This mask is solely installation/teardown protection, not child cleanup or
worker routing. Do not destroy signal publisher/producer before all potential worker
handlers joined. No concurrent notifier may outlive either ordinary or signal wake.
Install failure uses the same acquired-fd cleanup bank; no published handler pointer
may refer to a constructor which later unwinds.

## Producer ownership and handler path

Ordinary Job/UI notify publishes on the actual originating caller's producer with
its own prepaid native token. Native fd state is shared, raw claims are not shared.
Caller and worker each prepay one possible winning notification; only winner spends
its own token. Final observer closes use the setup owner's retained lane after join.
Job worker lane is sequentially bound for setup, released before start, bound by worker,
then joined before final owner binding. Destructor ordering must explicitly preserve
claims-before-producer lifetime; held close token is not a transient turn-thread claim.

Main SIGINT uses ONE dedicated, registered producer retained by its signal wake.
Registration and initial banks happen before handler activation. Add a narrow
RawSignalPublisher backed by that retained RawThreadHandle: direct held CONTROL
publication using a prepared POD/native-C writer seam over the same RawProducer
ownership/cursors/claim, without current TLS lookup/binding,
raw_fence, RPC, admission, allocation, lock, wait, subscription, formatting, or callback.
Only gate's winner calls it. Owner can bind/use that producer ONLY while rearming or
before activation/after retirement, never concurrently with a handler. Claims and
ordinary producer fields transfer through acquire/release ownership, not across
concurrent publications. The handler pointer, state and published cursors are lock-free.

Audit the ENTIRE signal-path call graph: stack literal fields/Ref, native C arrays,
plain POD result structs, POSIX memcpy/native ring copy, lock-free atomic member calls,
possible native send doorbell, shutdown. Do NOT call RawProducer::emit unchanged:
span/array/optional/Result operations are ordinary standard-library functions, which
C++ signal rules do not identify as safe. The narrow helper must use raw pointers,
native lengths/cursors, prepared pointers to held and publication counters, and fixed
request/return/failure storage. Share native ring-copy/envelope-publication primitives
with the ordinary writer so the same ring invariant is maintained; normal API may
adapt its std::span outside handler, handler never invokes that adaptation. No std::min,
std::exchange, optional access, Result/variant construction/access, iterator/span member,
std::array member, std::shared_ptr member or dynamic initialization in handler path.
Prepared state includes immutable epoch/request body and mutable single-owner ordinal,
held remaining/cursor pointers; actual return POD is captured without Error/Result API.
Native publication helpers enforce known finite record sizes, held credit and monotonic
publication; owner preparation validates capacities before activation.

Save/restore errno around the entire handler. No std::mutex, shared_ptr
increment/decrement, exception construction, optional callback, atomic wait/notify,
registration, or event-size computation based on dynamic payloads occurs there.
Publisher preparation obtains all internal pointers/epoch/claim references beforehand.

RawProducer::emit success means published even if raw_send_notice fails. Preserve that
actual notice error and publication; do NOT retry/relabel the request/result event or
skip shutdown. The native signal seam writes the existing literal8001 into the lane's already
reserved FAILURE25 bytes on its first writer/doorbell failure, sets prepared plain context-fenced / native error slots directly (never TLS raw_fence or std::optional), and continues the already-held shutdown
return. It does not recurse on another notice error. No later new rearm is admitted
on a fenced producer. At exclusive owner handoff harvest the plain native notice error into the ordinary
context's optional/error representation; do not transform the already captured metrics.
Recorder death/owner fencing still finds the published originals.
Native shutdown failure is a literal failure terminal, not evidence of EOF or joined
waiters. Under supported connected stream/single-winner preconditions kernel paths
produce peer EOF; arbitrary damaged fd/security-policy/native failures remain honest
failed wake custody. W2 resolved failure disposition follows; tests must not claim a
failed shutdown awakened a real waiter.

### W2: actual failed shutdown with potential native borrowers

Native-source distinction: Linux v6.6 unix_shutdown has only invalid-how refusal before
shared shutdown under an intact connected local stream. Darwin soshutdown refuses
unconnected state/invalid how; its first uipc_shutdown on intact stream returns0 after
peer EOF. A second invocation can ENOTCONN, prevented by winner ownership. Descriptor
EBADF/ENOTSOCK imply broken ownership; Linux syscall security/filter denial is an actual
reachable native refusal even when user-side ownership is intact. Do not translate
EINTR/any other actual observed failure into a successful wake or quietly retry it.

Configure the wake's lifecycle policy before publishing ANY borrowed fd: Job becomes
potentially borrowed immediately before worker construction/registry publication,
per-turn wake immediately before exposing it to a worker, global wake before exposing
it to main/UI wait scopes and before handler activation. This is actual owner lifetime,
not metrics-derived waiter count, subscriber registry or new cancellation executor.
A simple prepared POD boolean selects fatal-on-failure conservatively while published;
clear it only after actual join/exclusive final retirement, never based on an observation
that a particular worker appears outside poll. Setup before publication is ordinary
Error + held acquired-resource cleanup; final nonborrowed cleanup preserves literal
failure/unknown closure and does not claim process teardown was an orderly close.

After publishing actual2057 failed native return, a winner whose prepared policy says
potentially borrowed calls POSIX ::_exit(70) directly. Code70 is the existing native
raw recorder/supervisor fatal status. Handler path is only prepared POD/lock-free/native
publication and the native _exit call: no pre_exit product/metrics callback, business
encoding, std exception, allocation, lock, formatting, raw_fence, or attempt to join.
Ordinary caller follows the same immediate native disposition BEFORE unwinding any
stack into std::jthread destruction/join. No extra duplicate wake-failure record:2057
already holds the exact failure. Its finite48 return bytes were already paid within
shutdown96; _exit adds no new event/credit. One-shot activation close is NOT attempted
as a fallback once live-borrower shutdown failed; unused held92 remains with process
teardown and does not imply released child custody.

Independent recorder/supervisor sees owner death and preserves all published original
metrics. Actual incomplete attempts and borrowed command/participant child custody
remain unresolved/unknown; recovery refuses to replay or claim successful effect
settlement and requires the existing recovery/refit fence. External children might
remain alive: this is explicitly fatal owner-process teardown, not orderly stop/reap.
Do not claim a bounded successful join. No loss-of-power durability requirement is
introduced; powerloss remains excluded by operator direction. Actual native ownership
faults are recorded before deliberately ending the owning process.

Actual isolated production-path oracle (not fixture unblocking qualification): launch
selected exec with the real raw producer parked and an actual forked pre-exec alias
child stopped. Close the activation fd in that isolated owner fault cut, retaining
alias, then call the production wake: actual shutdown returnsEBADF,2057 is published,
process exits70 without unwinding/join. Parent test waits actual exited owner while
alias stays stopped, then reads/recover-drains original metrics and verifies exact
failed Ref/result/errno, absent claimed worker close/join/success, and business reopen
uncertain-custody fence. Only afterward kill/reap the fixture's actual owned alias and
actual recorded child PID. Supported Linux can separately deny shutdown using genuine
seccomp-return errno to exercise the actual native security-refusal path. No other
fixture's recorder release or artificial successful wake is evidence for production
failure cleanup. Root qualification owns supported profile coverage.

## Events and source-derived exact native banks

Ref is24 bytes (lane16 + ordinal8), envelope16, int4 on supported ABIs. All fields copy
native bytes; request/return are separate. No writer aggregates, parses or transforms.

2050 pair request: parent Ref, domain:int,type:int,protocol:int =52.
2051 pair return: request Ref,result:int,errno:int,returned fds:int[2] =56. Pair108.
2052 fcntl request: incarnation Ref,fd:int,cmd:int,arg:int =52.
2053 fcntl return: request Ref,result:int,errno:int =48. One call100.
2054 close request: incarnation Ref,fd:int =44.
2055 close return: request Ref,result:int,errno:int =48. One close92.
2056 shutdown request: incarnation Ref,fd:int,how:int =48.
2057 shutdown return: request Ref,result:int,errno:int =48. Shutdown96.
2058 dup2 request: incarnation Ref,source:int,target:int =48.
2059 dup2 return: request Ref,result:int,errno:int =48. One dup2 96.
2060 owner reset/retire request: old incarnation Ref,operation:u8,fds:int[2] =49.
2061 owner return: request Ref,branch:u8,ErrorCode:u8,detail:i64 =50. Lifecycle99.
2062 sigaction request: incarnation Ref,signal:int,handler pointer,flags:int,
meaningful native mask bytes, and restorer pointer on Linux.2063 return is
request Ref,result:int,errno:int,previous-present:bool, then those previous
members on success. No padded struct or unused libc signal words are emitted.
Native masks are4 bytes Darwin,8 bytes on the qualified Linux64-signal kernel ABI.
A=93+2*(handler-width+flag-width+mask-width+restorer-width), with restorer absent
on Darwin:125 Darwin/149 Linux per call. No pointer dereference or formatting.
2064 pthread_sigmask request: incarnation Ref,how:int,requested native mask bytes.
2065 return: request Ref,result:int,errno:int,previous-present:bool, then successful
previous native mask bytes. M=93+2*mask-width:101 Darwin/109 Linux. Actual pthread
result is authoritative; errno is separately copied as observed.
These owner-only calls keep the same dedicated producer exclusive before activation /
after all worker handlers join; a nonallocating held pair records failures too.
Native errno is copied immediately even after success (meaningful on failure).
The returned fds of failed socketpair are the initialized native storage, not invented
valid endpoints. A missing parent is all-zero Ref and means absent, not a fake request.

Setup max native bank=108(pair)+6*100(F_DUPFD_CLOEXEC/F_GETFL/F_SETFL, each endpoint)
+4*92(original + acquired-resource terminal closes) =1076. Four is a source-derived
upper bound: at most two original-fd closes and at most two still-owned cleanup closes;
unused bank released. Refuse immediately after ambiguous failed close, never acquire
more fds while retaining an unknown numeric ownership assumption. Successful setup
uses892; final two endpoint closes remain separately paid184.

Reusable rearm native bank=108+6*100+4*92+2*96(dup2)+2*100(F_SETFD CLOEXEC)=1468.
Includes staging cleanup on success/failure; target terminal closes are separately held.
Lifecycle bank99 is spent on every explicit reset/retire boundary, including busy/refusal.
Rearm never accepts unpaid native acquisition. W1 resolved sequence:
1. Actual calling owner lane nonblockingly prepays lifecycle99 and emits2060. This is
   NOT the dedicated signal lane. If it cannot prepay, refuse before reset/CAS/effects.
2. Clear the existing interrupted flag at its reset boundary, then one strong state CAS
   acquires rearming. A notifying/failed/CAS-lost result emits2061 on that actual caller
   lane, restores sticky interrupted on busy, preserves prompt, and NEVER binds or
   admits on the signal producer.
3. Only after successful rearming CAS may owner bind the dedicated signal producer.
   Keep the old notification token and prepared pointers intact while nonblockingly
   admitting native1468 plus96 ONLY if old token is spent (prior notified state). An
   armed old token retains exactly96 and is reused for the next incarnation. Never
   release unused96 before grant: on a capacity/fence refusal the original token must
   still make the old armed incarnation activatable.
4. If grant fails, end dedicated binding first; release-publish the original state,
   preserve/restore sticky interrupted, then resynchronize any signal observed during
   rearming by the same gated notify on the old incarnation. Emit literal refusal2061
   on caller's lifecycle token. No dedicated publication/binding occurs while armed.
5. On grant success, keep or split the exact96 notification token, update prepared
   native pointer/body only under rearming, do the paid fd operations, and end binding
   BEFORE release-publishing armed/failed. Busy refusal cannot touch the prepared slot.
   An uncanceled repeated reset reuses its96 once rather than leaking96 or retaining
   a stale prepared pointer. A spent old token has zero remaining and is destroyed
   only while exclusive; replacement pointer is rebuilt before armed publication.

Maximum reset credit is1663 =caller99 +dedicated1468 +replacement96. Armed uncanceled
reset requires caller99 +dedicated1468 with the existing96 held; same total occupancy.
Capacity refusal leaves original socket/state/prompt. Normal UI can freshly admit at a
live owner boundary, but must not wait with the old canceled descriptor or hold any
product lock. Final retirement releases any still-unused notification96 only after
closed state excludes all borrowers/notifiers; producer outlives every claim.
Hold separate final retire99+two closes184=283 throughout whole object's lifetime.

Ordinary one-shot native notification=96 shutdown +92 activation close=188.
Caller first-stop budget changes57→245. Worker bank2162→2350; initial4658→4846 for
its candidate notification. Setup1076 + final retire99+both possible closes184=1359 on that
same job producer before dispatch; total job-worker initial is exactly4846+1359=6205.
This is a finite physical-capacity bank, not number-of-events metric aggregation.
Review initial claim split placement so setup consumes its1076 first and untouched
final283 survives worker release/join. Caller245 is independent cleanup-owner credit.
Ordinary setup which opts out of reset/retire lifecycle could save99, but this unit
keeps the explicit owned terminal boundary rather than optimizing a smaller bank.

Reusable UI initial bank=setup1076 +notify96+final283=1455. Global signal initial
bank adds exact owner install/restore/mask operations:1455+2*A+2*M. Setup installation
uses A; final bank retains283+A+2*M for final block, handler restoration and prior-mask
restoration even after canceled work. All struct sizes are compile-time actual ABI sizes,
with initialized request storage and returned native storage captured verbatim.
Future actual rearm1663 fresh
at exclusive owner boundary. UI owner uses the existing retained owner handle; global
uses the dedicated signal handle. Setup itself is not done in signal context.
Signal FAILURE25 is existing context reserve, independent from these CONTROL banks.
Static sizeof assertions and test actual emitted envelope totals confirm these equations.
No timer/retry budget is invented; exactly one native shutdown per incarnation.

## Caller business custody

Change start to return an honest admitted/refused result before moving/staging a prompt.
A busy/capacity/native reset refusal preserves queued.front (do NOT erase/pop beforehand)
or restores the exact submitted composer draft. Initial/resume prompt remains pending.
No prompt is silently consumed when owner cannot rearm or register the new turn lane.
Do not immediately spin automatic retry on busy: show actual refusal and leave queued
work pending for a later explicit live owner start opportunity. No deadline changes.

Main reset callback must propagate Result<void> rather than silently returning void;
plain perform/resume/once boundaries refuse before user prompt/effect admission when
rearm fails. Reset/handler installation and destruction must be exception-safe using
held banks, with actual previous handler and mask ownership restored on all paths.
Existing cancellation/late-quit operation receipt banks are not reopened by this unit.

## Direct red / green native oracles

1. Genuine old failure: actual RawRuntime producer blocked by stopped recorder and
   known-full DATA; establish real parked admission. Interpose ONLY actual old pipe
   write failure with -1/EINTR and no byte, retain native request evidence in fixture.
   Real sticky cancellation stays true, actual admission stays parked. No substitute
   bool-only wake. Explicit release recorder cleans up the red fixture afterward.
2. Retained pre-exec alias: fixture real fork child SIGSTOP before exec, verify owned
   waitpid stopped status, retaining activation endpoint. Old failed write followed
   by parent close still yields no EOF while child remains stopped. New one-shot
   shutdown gives literal request/success return, real parked admission interrupted,
   worker terminal/withdrawals recorded and join completes BEFORE alias child resumes.
   Kill/reap only fixture's actual owned child after asserting that order.
3. Concurrent ordinary stop winners: gate caller and worker at real notify entry,
   actual child/parked admission, exactly one shutdown and matching ordinal-ordered
   return on actual winning issuer lane, no cross-lane token use; losers no nativecall.
4. Reuse: first actual canceled turn joined, successful staged pair+dup2+CLOEXEC events,
   fd numbers unchanged, new incarnation Ref changed, new real capacity waiter parks,
   second cancellation wakes it; old inherited alias cannot activate new socket.
5. Hold genuine handler winner at native shutdown seam, owner reset returns busy before
   dedicated raw bind/claim or any pair/dup2 effect; caller lane alone emits reset req/res.
   Separate gated race: pause reset after successful CAS, attempt handler activation,
   confirm no handler publisher access during claim; force actual capacity refusal,
   restore original prepared/token/state, then native activation succeeds with old96.
   Repeat uncanceled resets and inspect actual remaining/held/free cursors to confirm
   no96 leak and no stale claim pointer, without writer-side aggregated metrics.
   On the real UI failure actual queued prompt/draft survives and is dispatchable once
   winner returns and an explicit subsequent live owner start retries. No auto timer.
6. Real pthread_kill SIGINT on an actual background owned worker: dedicated signal lane
   publishes shutdown req/res on its own ordinal sequence, not interrupted worker TLS;
   parked turn wakes. Competing signal and ordinary global notifier grants one caller.
   Deliver during rearm seam; sticky resync cancels new incarnation without loss.
7. Actual setup/dup2/fcntl/shutdown/close failure seams, each separate native req/res and
   no false armed state/successful join. Shutdown failure uses the actual isolated fatal production cut above; no fixture
   recorder release is substituted for qualification of production failure disposition. Partial replacement refuses new waiter.
   Notice send failure after publication retains exactly one request + return +8001;
   native shutdown progress survives, new admission fenced, originals disk readable.
8. Signal lifecycle: previous sigaction restored after normal/exception teardown,
   no global pointer/claim/producer use after join/destruction, repeated full session
   invocation gets a fresh signal wake. Verify no raw signal path allocation/lock/RPC;
   supported Linux/Darwin compiler-generated atomic/memcpy path inspected narrowly.
9. Existing command_raw, command_jobs, unchanged terminal_pty assertions/deadlines.
   Affected native fixtures + narrow static one worker only after root release; root
   owns full gate and supported debug/release/ASan qualification.

## Acquired literature and reference implementation grounds

POSIX.1-2017 chapter2 primary snapshot posix-signal.html: async-signal-safe table includes
shutdown, send, memcpy, sigaction, pthread_sigmask; errno save/restore required. C++
lock-free atomics checked on supported implementations, not assumed from POSIX C text.
https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html
C++ primary working draft support.signal explicitly permits plain lock-free atomic
operations and excludes ordinary standard-library calls, TLS object access, exceptions,
try-blocks and lazy static initialization from a signal handler. This drives the POD
seam, rather than relying on unpromised inlining of existing C++ wrapper functions.
https://eel.is/c++draft/support.signal
POSIX snapshot SHA2568c15d0b5fc84e4b49ff1d32b789a3027f1dd1c8aa14ab22e619606b78caf4e96.

Linux v6.6 immutable tag snapshot linux-unix.c: unix_shutdown2904–2950 writes shared
socket shutdown state and maps SEND_SHUTDOWN to peer RCV_SHUTDOWN + POLL_IN wake;
client descriptor aliases refer to that same socket. Readable EOF does not require
all aliases closed. https://raw.githubusercontent.com/torvalds/linux/v6.6/net/unix/af_unix.c
SHA25660a274445311a60784234517d24dcb6b4d2c55a56b3e5d12dc200979cede1836.

XNU snapshots xnu-socket.c soshutdownlock_final4343 + xnu-unix.c uipc_shutdown774 /
unp_shutdown2222: SHUT_WR invokes protocol shutdown, socantsendmore, peer
socantrcvmore. Repeated SHUT_WR can return ENOTCONN: single winner is substantive.
https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/uipc_socket.c
SHA25668d5a0dfb22ad62bca963b3bc3b1c08ed28549f3c7679789a549eae428fa60bc.
https://raw.githubusercontent.com/apple-oss-distributions/xnu/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/uipc_usrreq.c
SHA256c34d28100931762957c2cb2f3b7a2bef5fe078e4d9c1dca1c494345cbec0e12c.
XNU acquisition2026-10-11 is pinned to f6217f891ac0bb64f3d375211650a4c1ff8ca1ea;
re-downloaded pinned sources match both recorded SHA256 hashes exactly. Restoration
URLs also reside in xnu-restoration.txt. Do not use archived instructions as live doctrine.

Existing participant plan310–363/530–550 supplies the reviewed one-shot native
ownership reference, not interchangeable reusable clear behavior. Existing process
spawn mutex invariant and CommandJobs explicit POSIX_SPAWN_SETSIGMASK child path
are preserved. Linux man-page dup2 primary implementation documentation: FD_CLOEXEC
is cleared on successful duplication, and implicit target close errors differ from
explicit close; retain actual dup2 result, never manufacture a separate close result.
https://man7.org/linux/man-pages/man2/dup2.2.html

## Implemented seams / exact plan-attack decisions

Dedicated direct signal publication uses the owned RawSignalPublisher API, with
prepared native handler state and retained setup/destruction ownership. Owner reset
Result propagation reaches TerminalUI and main; those callers retain the exact source
prompt before moving it. Failure after mixed dup2 permanently refuses the wake.
The owner sigaction/mask bank uses the actual ABI sizes above; the fixture prints
Darwin and Linux constants rather than substituting another platform's struct size.
Selected2062–2065 record installation/restoration/protected teardown. Existing
worker/child signal setups remain outside this unit; this does not capture every
signal delivery. Global regular notify (perform's worker exit) uses the same
dedicated publisher/gate as SIGINT and never binds that producer concurrently.

Implementation ownership refinement: sigaction/mask request and return use the actual caller producer, held before any native effect; the dedicated producer handles only wake publication/rearm. RawCommandInterrupt owns previous handler/restoration banks and destroys its wake while final owner SIGINT is blocked, after all potential handler threads join. Busy rearm does not read mutable incarnation fields:2060 zero Ref/fds{-1,-1} records no granted view; successful reset snapshots metadata only after the CAS. Pre-dispatch destruction still owns both endpoints, so one-shot final283 is required even though notified closure only uses191.


## Implementation and native qualification

RawCommandWake owns stream socket endpoints normalized above stdio with CLOEXEC and
nonblocking flags under the existing spawn mutex. Job wake creation moved onto its
registered producer before dispatch; jthread is declared last so automatic destruction
joins before wake/claims/producer. Cleanup caller pays245, worker2350, initial worker
4846, setup+final1359, combined6205. Control stop pays188 before the product mutex.
The parent stop hook notifies only on its actual first transition, so finished jobs do
not attempt a notification with a released token. Global signal state is owned by
RawCommandInterrupt; sigaction and mask records use actual caller credit. The signal
handler reads a native pointer, not unique_ptr, then executes the lock-free ownership
gate and prepared POD publisher. It never reads normal producer TLS, spans, Results,
optional claims, or standard-library storage on the signal branch. Ordinary one-shot
notification prepares its actual caller view after winning the same gate.

Native publisher and ordinary publisher share the envelope/ring publication primitive.
Preparation validates normal ownership before effects. Publication uses fixed C fields,
memcpy, release/armed atomics and prepared native send only. A literal notice failure
sets the retained context's admission fence and writes existing8001 from reserved25;
already held native terminals remain emit-able. No writer copy/parse/transform beyond
required framing is added. Native state supersedes a separate optional-notice harvest:
new owner admission is already fenced, and8001 preserves the actual error.

Terminal reset returns refusal before moving any queued/composer/initial source. Queue
front erasure follows successful dispatch. Refused queued work waits for an explicit
submission opportunity, retaining queue order; no automatic retry loop. Native thread
registration/preparation refusal also preserves the original prompt.

The genuine old red was executed before source replacement, exit1 in0.65s:
write=-1/EINTR(errno4), original observer poll0, actual raw admission still parked,
actual inherited alias stopped. It is a damaged descriptor incarnation with a blocking
replacement pipe; it does not claim an intact nonblocking pipe normally blocks.
The original failure function/source and log remain under ignored
context/raw-command-wake-study/old-eintr-red-fixture.cpp and
old-eintr-red.log, against checkpoint2bda565 originals. The new fixture directly tests
intact stopped-alias socket wake, real parked raw admission, actual worker SIGINT,
gated handler versus reset refusal, stable descriptor replacement, old alias exclusion,
unused token preservation across uncanceled reset and physical capacity refusal,
predispatch two-close cleanup, genuine fd EBADF in fcntl/dup2/cleanup, notice failure
with literal8001/held return, and fatal isolated production owner with an actual opened
exec, real parked jthread and stopped alias. Signal allocation counters directly observe
actual handler threads; the no-lock/no-RPC/std-wrapper claim follows the source path.

Darwin actual ABI is sigaction16, sigset4; the corrected memberwise layout gives
A125, M101, global initial1907 across
caller and dedicated producer. Supported Linux values are sizeof-derived and printed
by the same fixture; root owns qualification. Native bank equations are physical held
capacity bounds, not metric aggregation. Source narrow tidy found ambiguous private
integer framing parameters and unchecked optional access; a private POD Record replaces
the former and an explicit bank check resolves the latter. No suppression was added.
The existing command resource fixture now applies its same RLIMIT6 after both actual
wake F_SETFL returns, preserving all original worker terminal assertions. An additional
native predispatch wake EMFILE assertion covers the earlier admission boundary.

Final affected Mac debug checks pass4/4 in21.80s: command_jobs3.93s,
terminal_pty12.44s, command_raw4.41s, raw_command_wake1.00s. Earlier direct primitive
checks against the same final publication implementation pass raw_channel1.30s and
raw_runtime1.25s. Terminal PTY assertions and deadlines are unchanged. The separate
one-observation rerun of the old exact-input failure passes4.19s; it did not capture
empty file contents. Source inspection establishes that the fixture's old save made
the marker name visible at open, before write/close, while the oracle waited only
for existence. The fixture now writes/closes/checks a same-directory staging file,
then renames the complete file into view. Exact byte assertions remain; failure
directories and observed mismatched bytes are preserved for diagnosis.

Fatal-path fixture cleanup waits on the actual business writer flock after killing
the stopped inherited alias. SIGKILL acceptance alone did not establish descriptor
release: the first direct reopen refused busy35. This fixture-only native lock wait
follows the observed owner exit70 and never substitutes for production wake success.
An actual retained opened exec reopens as external_unknown without replay; the
literal shutdown EBADF return is separately read from disk. The independent recorder
is resumed only after fatal exit/stopped-alias observation. No orderly cleanup or
bounded native-failure join is claimed.

Root's ONE whole-unit code/oracle attack and supported-platform/profile integration
are pending. These direct checks do not close nls.2 or qualify unrelated owner work.

Final narrow clang-tidy passes all six changed/revised TUs: raw_channel,
raw_command, terminal, raw_command_wake_test, command_jobs_test and
command_job_fixture. The earlier owning pass was already clean for raw_runtime,
command_jobs, main and command_raw_test; those source bodies did not change after
that pass. LLVM23 formatting and owned-path diff whitespace checks pass. Compiler
and static checks used one worker; no full suite, profile sweep or additional
review panel was run by this owner.
