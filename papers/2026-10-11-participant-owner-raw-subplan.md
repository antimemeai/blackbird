# Participant ownership, original capture and addressed directions

2026-10-11. Proposed whole-owner subplan for completion rows41–45/system G,
including their concrete nls.10 capture-limit and post-retain-drop overlap.
Study and plan only. Root completed the ONE plan attack; its two findings
are resolved below before source release. No new general literature campaign,
dependency, timer, evaluator or custody service. Multiplayer remains deferred.

## Actual boundaries studied

Read participants.hpp/cpp, colleague.hpp/cpp, the actual native Child read path,
provider_http/ProviderAuth::request/openai_http, LocalTools::exec, coding's
participant callbacks/dispatch, main's ParticipantShutdown and polling failures,
participants_test, participant_recovery_test, colleague/engine tests, the existing
raw-channel/claim/runtime/reader implementations, the command owner plan, and the
retained surface corpus/completion census. POSIX partial-read and original-before-
limit rules already underpin the reviewed provider/command units; this plan
uses those mechanisms, rather than reopening that transport design.

One owner controls Participants and drains semantic originals/state. Workers
touch guarded Run state and the bounded capture queue. Product limits are
32runs,16concurrent workers,64receipts/run,16queued directions/run,
1024queued captures and8MiB queued capture bytes. Existing await/join20ms waits
are business behavior; instrumentation introduces no cadence.

Concrete defects to resolve within this owner:

1. The worker's borrowed capture view is checked against8MiB/1024 limits before
   copying. The real9MiB capture fixture becomes unknown, but every acquired byte
   can disappear. A failure count or reference to the freed string does not fix it.
2. send retains admission, then allocates a receipt and deque entry. A successful
   receipt insertion followed by failed inbox allocation can later report duplicate
   acceptance for a direction never queued. Durable admission is not dispatch.
3. The stop-after-wait branch finishes without clearing/counting accepted inbox
   entries. Every discarded accepted direction needs its actual withdrawal link.
4. drain copies a front, invokes Retain unlocked, then pops whatever front exists.
   A reentrant owner callback can drain/pop twice. Retain failure leaves a copy
   pending today, but destructor cleanup can conceal the unresolved semantic debt.
5. Coding's discarded string-returning AuditLog::original/record results allocate
   formatted IDs after append. An accepted append followed by that allocation
   failure can be mistaken for failed retention and retried.
6. Worker catch is noexcept but constructs several Values. Its error reporting
   must not throw again and lose the worker's actual terminal outcome.
7. start/send retain one prepared request; the worker prepares it again and
   discards that second admission capture. A mutable public provider binding can
   differ between them. The retained admission must be the prepared Value
   actually transferred to the worker, not a similar earlier preparation.

Restore of running/waiting/cancel_requested rows deliberately becomes unknown;
there is no provider redispatch. Waiting is available for directions, not completed.
Cancellation of a dispatched request does not establish remote cancellation.

## One concrete original-byte contract shared with provider

Coordinated with provider_raw_vertical by root's instruction. Shared owned header
is provider_original.hpp, implemented in provider_original.cpp. The provider's adapter plan owns the physical source
lease and its source-site descriptions; Participants owns its prepared receiver
and semantic queue. Do not implement a second publisher or custody store here.

The agreed prepare signature takes
ProviderOriginalRequest{ProviderOriginalRole role,ProviderOriginalMode mode,
size_t maximum_bytes} and returns Result<unique_ptr<ProviderOriginalTicket>>.
The modes are native_extent,callback_extent,codec_adopt. Other owned shapes are ProviderOriginalLease;
ProviderOwnedOriginal; and ProviderOriginalReference. The latter contains the
actual RawEventRef, RawPartition, native record_offset:size_t and bytes:size_t. These
are native framing locations, not a metrics interpretation of a response.
RawPartition's actual underlying native type is u32. ProviderOwnedOriginal also
carries ProviderOriginalExtentKind:u8 (exact_used1/unknown_used2) and optional
native observed_used:size_t. Reference.bytes is the published memory-range
length; it is not an observed-used claim on an unknown extent.

prepare runs before source acquisition, outside the participant mutex. It stages
the capture node, exact existing metadata, native buffer/storage control block,
and participant CONTROL outcomes. The source lease holds issuer-lane DATA and
native-return credit. The ordinary maximum is the existing16MiB business codec/
provider response bound. Native Child's actual buffer bound is8192, not4096.
Codec-adopt stages only its empty storage control block/node/metadata; it does
not allocate an unused16MiB native body buffer. Extent modes allocate their
actual requested writable storage before the native source fills it.

The source acquires into the prepared writable storage, copies actual original
bytes into raw custody immediately, and only then performs pending append,
preview, parse or limit checks. Native Child returns its actual1404 Ref; shared
lease records that Ref/range rather than publishing the same raw bytes twice.
An owned injected application response uses the provider's bounded lease and
actual source site; its old arbitrary std::string callback is not a compatible
acquisition contract. Private credential HTTP bodies remain payload-inactive.

Existing business encoding occurs once. Before that encoder, source lease holds
ValueLimits{}.bytes plus its source record overhead. Adopt the resulting existing
std::string by move under that protected capacity; do not add a metrics encoder,
Value traversal, BBM dump, Number.text, JSON, parse or formatted identity.

The provider's concrete shared source description is
[provider adapter subplan](2026-10-11-provider-adapter-raw-subplan.md). Its native
1404 byte payload starts at offset61 of the raw record; exact-used injected
callback1312 starts at58; unknown-used callback1312 starts at50 with no used
field; codec/prompt1322 starts at49. The exact actual Ref and committed
length accompany those native positions. Source-owned maximum grants are Child
DATA8192+61/CONTROL61; application callback DATA16MiB+58/CONTROL99; codec
DATA16MiB+49/CONTROL142. Participants'171byte ticket bank is separate and
never spends those source claims.

After publication and the last synchronous source borrow,
ticket.complete(ProviderOwnedOriginal&&) performs a noexcept, nonallocating
transfer and returns native ProviderCaptureDisposition. The source may raise the
existing capacity Error only after that transfer. Normal storage is owned/pinned
through both existing business uses and semantic retention; two competing unique
moves must not invalidate either. A preallocated shared native storage control
block is allowed where the actual business response and capture both need it.
The lease guard completes the transfer even when a later append/preview throws.
For an injected whole-body response with a participant receiver, that receiver
first owns the shared original; constructing the existing business response
std::string is an explicit product copy afterward. If that allocation fails,
the queued original still exists. With no receiver, move prepared storage into
the business response normally. Native chunk pending append/preview finishes
before transfer. These are stated product costs, not work in the raw writer.
CONTROL never transfers to another lane. The worker owns ticket prepare/handoff
outcomes. A node carries only original storage and actual published references
to the owner; drain2110/2111/2112 always acquires its separate owner-lane bank
before locking/picking/calling Retain. Neither worker nor owner keeps a
borrowed pointer after source publication/transfer.

On semantic queue overflow, link the staged node into that Run's single emergency
slot, emit the actual capacity disposition from held credit, close acquisition,
set sticky stop and wake the worker. The crossing body is already in raw custody.
The normal queue's8MiB/1024 limits remain unchanged; the one emergency body is
bounded by the already admitted source extent. Do not acquire another original
while that slot is occupied. drain can retain that node on the owner. On Retain
failure keep precisely the same node, bytes and source Ref; only semantic append
may be retried, never the transport or source acquisition. A reference-only
OwnedOriginal variant is honest pending semantic-retention debt if owned storage
is unavailable: it cannot imply an immediately completed Retain.

Root's single provider plan attack resolved fill-then-throw-before-commit by
preserving the entire initialized prepared extent with unknown-used qualification.
It records no guessed observed prefix or successful response. Participant
handoff moves that actual storage/range into the same bounded emergency node,
closes acquisition and wakes sticky stop. Owner drain records2135 unresolved
semantic debt and raises its actual incomplete source refusal, leaving the node
owned. It calls neither Retain nor parse and claims no complete semantic body.
That unknown extent cannot silently become exact during a drain retry. This is
different from an exact committed prefix followed by an exception: the latter
is an exact original and may be retained without asserting remote completion.

Use the provider contract's fixed native ProviderOriginalRole, not a free-form
metrics label. The prepared participant node still owns its actual existing
business label; copying that existing label in2107 requires no new dictionary.
The exact role travels alongside it. Native Child reads directly into prepared
participant storage, copies once to raw1404, then does its existing whole-response
business accumulation. It does not read into stack storage and additionally copy
that buffer into the participant node. Default nonparticipant stack behavior is
unchanged. Do not introduce another raw body publication.

The general borrowed ColleagueCapture observer is insufficient for participant
acquisition. Participant-aware call_colleague encoder sites and native transport
adapters use this prepared ticket. Ordinary main/standalone synchronous preview
keeps its existing stack read buffer and does not allocate a participant node.
Tests/custom participant transports use the same explicit prepare/commit contract;
there is no arbitrary borrowed-view compatibility escape hatch. A literal or
already-owned string may be adopted by the lease, with source admission preceding
the callback that acquires/generates it. Calling prepare after that callback
returned is too late.

## Owners, state and transfers

Reserve participant sites2100–2199, disjoint from command2000–2047 and existing
task/authored/provider/auth sites. Root registers immutable descriptions before
activation. New raw_participants.hpp/cpp implements native finite owner banks.

Transfer the exact staged prepared Value from start/send into the worker's
selected call_prepared_colleague helper, alongside its actual admission source.
That helper uses the shared existing execution/reply business implementation;
it does not reprepare or encode the already retained admission again. Ordinary
call_colleague still prepares once and captures its own admission. Keep actual
ProviderAuth expected-binding validation at transport dispatch: a changed binding
is a real conflict/not-dispatched result, not permission to dispatch a newly
prepared request that differs from its original. Direction metadata/request ID
and admitted prepared Value move together through the same staged node.

An action R is exactly lane_epoch[16]/ordinal:u64. Copy actual native attempt,
invocation and immutable generation from CodingEngine when supplied. Transfer
them explicitly into Run; worker transport dispatch supplies its actual2104 R
as RawProviderOwner::dispatch. Native/direct users can omit these identities;
never invent them or infer them from textual run IDs. Run identity is its exact
existing opaque string. A reopened row has its actual2129 row R and no worker
launch. Actual direction offer/acceptance R travels with its queued request.
Duplicate observes its original accepted R; it is not another dispatch.

Stage Run, callable captures, initial row data, receipts/direction nodes and
callback metadata before semantic admission/queue publication. Replace the
post-retain allocating inbox insertion with a preallocated16-slot ownership ring;
stage the receipt map node before retention and insert by node handle after the
effect. Consume staged objects by reference/move without constructing another
deque or allocation after append (including on libstdc++13). Recheck finished,
sealed and sticky stop after Retain, before nonallocating transfer; record a real
withdrawn-before-enqueue outcome if the worker has ended. Keep the retained
admission original; do not advertise accepted queueing on that branch.

Workers use an owned intrusive capture-node queue: node allocation/metadata is
part of prepare, linking is nonallocating. Existing capture_bytes and event
count remain product admission state, not metrics aggregates. One owner drain
guard refuses reentrant drain before it can change the front. Its pending front
is stable while Retain runs unlocked; successful return pops that exact node.
Failed return keeps it. Workers only append or install their emergency node.

Every direction receives one worker-lane transfer outcome: dequeue for dispatch
or withdrawal. The real64receipt lifetime bound, rather than only16simultaneous
slots, funds64 outcomes at worker setup. Clear/count all remaining accepted
directions on every terminal path, including stop after the condition wait.
Reopen reports unknown and never reconstructs/replays them into a worker.

Add a native void/result overload of AuditLog::original accepting a preissued
ApplicationRecordId and returning its actual SourceReference after append without
post-effect formatting. Existing string-returning callers remain outside this
unit. Participant Retain returns optional actual SourceReference (native custom
retainers may truthfully have none). Coding Save preissues the real identity and
uses existing void record(identity,program,snapshot); it returns that actual
identity to the selected save hook. SourceReference is journal/sequence, distinct
from a raw lane/ordinal R; do not cast one into the other. Build all metadata and
business packets before append. No worker obtains AuditLog or ContextStore.

The worker's failure terminal records the actual Error/allocation/unknown branch
before fallible business reporting. Prepare its emergency failure projection and
needed replacement slots before launch, or defer projection to the owner; the
noexcept worker catch itself must only move prebuilt state, set native flags,
withdraw owned directions and emit from held credit. It cannot allocate a new
error Value while reporting bad_alloc. Keep remote disposition unknown after
uncertain dispatch; a stop-before-dispatch branch remains not_dispatched.

## Exact selected sites

E=16; R=24; N=sizeof(size_t)=8, unsigned=4, int=4, ssize_t=8 and bool=1 on
qualified profiles. Text is native size_t length plus exact existing bytes.
Error is ErrorCode:u8/detail:i64, present only on error branches. Maxima below
include E. Source R and semantic SourceReference have distinct descriptions.
Branch/stage tags describe the actual executed boundary; no status-string parse
or successful result is invented for an error.

| Site | Ordered body and boundary | Maximum |
| --- | --- | --- |
|2100 action request|action:u8,parent-present:bool,[parent R],origin-present:bool,[attempt16,invocation16,generation16]; before configure/start/read/send/cancel/await/join/archive/drain/shutdown/restore validation|91 CONTROL|
|2101 action return|action R,branch:u8,stage:u8,[Error]; actual return/refusal/exception|51 CONTROL|
|2102 Run setup|start R,run Text,task Text,prepared worker lane16,concurrency:unsigned; before publishing staged live Run, not proof thread started|332 CONTROL (two128byte product IDs)|
|2103 worker entry|Run R,initial direction/admission R,inbox:size_t,stop:bool; actual binding before work|73 CONTROL|
|2104 transport request|worker R,direction/admission R,origin-present:bool,[three actual IDs],source-present:bool,[actual admission source R]; before selected call_colleague/transport|138 CONTROL|
|2105 transport return|transport R,branch:u8,stage:u8,stop:bool,[Error]; after actual selected call, before result projection|52 CONTROL|
|2106 state publication|Run R,branch:u8,finished:bool,sealed:bool,stop:bool,inbox:size_t,receipts:size_t,dirty:bool; copy actual guarded state at its existing transition|61 CONTROL|
|2107 capture prepare|transport R,role:u8,label Text,maximum:size_t,source-mode:u8; before prepared source acquisition, label is exact existing business label|58+label bytes DATA|
|2108 capture source handoff|prepare R,branch:u8,[source R,RawPartition:u32,record_offset:size_t,published bytes:size_t,ExtentKind:u8,used-present:bool,[observed_used:size_t]],[Error]; after raw source publication, before queue/limit disposition; failed prepare has no invented source|104 CONTROL conservative error+source maximum|
|2109 capture queue outcome|handoff R,capture_bytes:size_t,captures:size_t,branch:u8,stop:bool,[Error]; normal/emergency publication or honest failed transfer|67 CONTROL|
|2110 owner capture transfer|handoff R,captures:size_t,capture_bytes:size_t; stable pending node selected under guard, before unlocked Retain|56 CONTROL|
|2111 retain request|handoff R,actual source R,RawPartition:u32,record_offset:size_t,length:size_t,ExtentKind:u8(exact_used only); before the actual semantic original append|85 CONTROL|
|2112 retain return|retain R,branch:u8,semantic-source-present:bool,[journal16,sequence:u64],[Error]; actual success/failure, before releasing the pending node|75 CONTROL conservative maximum|
|2113 snapshot save request|action R,runs:size_t,concurrency:unsigned,dirty:bool; before existing snapshot/Save owner work|53 CONTROL|
|2114 snapshot save return|save R,branch:u8,identity-present:bool,[actual ApplicationRecordId16],[Error]; actual callback/append outcome|67 CONTROL conservative maximum|
|2115 direction offer|action R,run Text,message Text,from Text,text Text; exact already acquired native strings before receipt/inbox/duplicate checks, no additional serialization|72+actual string bytes DATA|
|2116 worker direction transfer|offer R,Run R,branch:u8,inbox:size_t,stop:bool; exactly one dequeued/withdrawn outcome for each accepted queued direction|74 CONTROL|
|2117 stop transition|action/failure R,Run R,source:u8,stop:bool,finished:bool; actual stop/capture/retain/save/shutdown transition|67 CONTROL|
|2118 direction admission return|offer R,branch:u8,matched-offer-present:bool,[original accepted offer R],inbox:size_t,receipts:size_t,[Error]; duplicate/conflict/capacity/accepted/withdrawn-before-enqueue|91 CONTROL|
|2119 seal transition|join R,Run R,sealed:bool,inbox:size_t; actual seal before waiting for worker completion|73 CONTROL|
|2120 socketpair/fcntl request|start R,method:u8,three native ints: domain/type/protocol for socketpair, or fd/command/flags for fcntl; actual owned cancellation setup|53 CONTROL|
|2121 socketpair/fcntl return|request R,method:u8,result:int,errno:int,descriptors-valid:bool,[two int descriptors]; actual native return, descriptor fields success-only|58 CONTROL|
|2122 cancellation shutdown request|stop R,fd:int,how:int; actual shutdown(SHUT_WR) on the taken activation endpoint; no wake write|48 CONTROL|
|2123 cancellation shutdown return|shutdown R,result:int,errno:int; actual native return unchanged, not a fabricated successful write|48 CONTROL|
|2124 descriptor close request|Run/start R,fd:int,reason:u8; actual taken activation endpoint close after shutdown, observer close after join, or failed setup cleanup|45 CONTROL|
|2125 descriptor close return|close R,result:int,errno:int; no retry on a reused descriptor|48 CONTROL|
|2126 worker end|Run R,stop:bool,finished:bool,sealed:bool,inbox:size_t,emergency-pending:bool,branch:u8,[Error]; actual terminal owner after withdrawals|62 CONTROL|
|2127 join request|action R,Run R,joinable:bool; actual selected jthread join, outside guard|65 CONTROL|
|2128 join return|join R,branch:u8,[Error]; actual joined/not-joinable/failure|50 CONTROL|
|2129 restored row|restore R,run Text,previous-state Text,branch:u8,finished:bool,sealed:bool; exact retained row boundary and unknown/no-dispatch disposition|59+exact string bytes DATA|
|2130 presentation request|state/action R,callback-present:bool; before existing Publish|41 CONTROL|
|2131 presentation return|publish R,branch:u8,[Error]; actual success/absent/swallowed callback failure|50 CONTROL|
|2132 read row|read R,Run/restored-row R,inbox:size_t,receipts:size_t,sealed:bool,finished:bool,stop:bool,dirty:bool; existing selected row traversal only, no second census|84 CONTROL|
|2133 configuration transfer|configure R,old:unsigned,new:unsigned; actual existing product change|48 CONTROL|
|2134 archive transfer|archive R,Run R,branch:u8,runs:size_t; actual removal after join; no claimed erase on failure|73 CONTROL|
|2135 unresolved source debt|drain R,handoff R,published extent bytes:size_t,branch:u8,[Error]; owner sees unknown-used or unresolved physical source, keeps the node and refuses without Retain/parse|82 CONTROL|
|2136 owner cleanup end|cleanup action R,Run R,worker-END-present:bool,[actual2126 R],semantic-debt:bool,branch:u8,[Error]; once after actual join/endpoint cleanup, or actual unstarted/failure branch; no redundant joined bool|100 CONTROL conservative maximum|

Payload bytes belong only to the shared source record, not2108/2111 again.2107
and2115 DATA frame actual existing strings. Oversized direction field originals
must still have a real source owner: an external engine call supplies its retained
input/operation owner; a direct native caller supplies the shared bounded owned
input when it expects that guarantee. This unit does not claim an encoded byte
record for malformed arbitrary Value arguments before the existing business
encoder. It does record the admitted action's actual key/type/length failure.
The broader audit-every-call boundary belongs to nls.10; do not mark that whole
bead complete merely because this owner fixes captured response loss.

## Credit, cancellation and lifetime

Claims acquire outside participant/spawn mutexes. Only raw_emit using held claims
and primitive copies runs under guard. Never share a producer concurrently.
Prepare/register a Run worker lane before dispatch; temporarily bind it during
setup to claim its own finite bank, then thread construction hands that sole
owner to the worker. Owner claims cannot fund a worker publication.

Setup creates a Run-owned AF_UNIX/SOCK_STREAM socketpair with protocol0. Call
the first endpoint activation and the other observer. Under the existing spawn
mutex use F_DUPFD_CLOEXEC to normalize both descriptors above standard descriptors,
close each replaced descriptor once, and use native F_GETFL/F_SETFL to set
nonblocking; then release the mutex. These are at most one socketpair,six fcntl
calls,two replaced-descriptor closes and two eventual endpoint closes. Only the
observer is borrowed by RawWaitControl, and it outlives every worker admission
wait. The owner closes it only after join. No turn/Lua stack's cancellation
endpoint is cached past its call; no data is written, read, consumed or reset on
this dedicated pair. Existing business transport deadlines remain separate
inputs whose descriptors outlive their waits.

### Resolution of interrupted wake and inherited endpoints

Root's ONE plan attack found that a failed wake write can leave a worker parked
forever. The first proposed pipe-close fallback also failed to account for a
child stopped between fork and exec: CLOEXEC does not close its inherited writer
until exec. That alias can keep a pipe from EOF after the parent closes its copy.
This is the same wake finding's resolution, not another panel or source unit.
Replace the pipe/wake write with one shutdown(SHUT_WR) on the stream socketpair.
Shutdown changes the shared native socket state even while fork aliases remain
open; it does not rely on their exec or close.

Each first stop takes the sole Participants-owned activation descriptor under
the participant mutex, stores -1 in Run, sets sticky stop and closes source
admission. It releases the mutex, emits2122, makes exactly one native
shutdown(fd,SHUT_WR), immediately preserves actual int/errno in2123, then emits
2124, closes its local descriptor once and records actual int/errno in2125.
There is no wake write, retry or success relabeling. The observer becomes
persistently readable at EOF through receive shutdown; RawWaitControl already
observes POLLIN/POLLHUP. The condition notification separately releases the
business wait. Close releases this process's descriptor after shutdown; EOF
no longer depends on that close destroying the last socket reference.

Only the winning owner or worker takes the Participants-owned activation FD.
A competing stop sees -1 and cannot shutdown, close or reuse that number; it
records only its actual stop transition. Existing native process spawning may
fork an alias before exec, and even a stopped nonexec child may retain that alias:
it shares the already shut socket state. CLOEXEC and setup's replaced-descriptor
closes still prevent unintended lifetime beyond exec, but are not a cancellation
liveness argument. Source waits only borrow the observer. The winner uses its
issuer bank; CONTROL never moves between lanes. Final observer close is on the
retained setup-owner lane after worker join. Failed setup closes only actual
successfully acquired descriptors from held cleanup slots.

The exact supported native paths were read for this finding. Linux unix_shutdown
sets the socket's SEND_SHUTDOWN and its connected peer's RCV_SHUTDOWN, then wakes
the peer; unix_poll reports the latter as readable even without queued data.
The state belongs to the socket shared by fork aliases, not one descriptor.
See [Linux Unix sockets](https://github.com/torvalds/linux/blob/master/net/unix/af_unix.c).
XNU soshutdownlock_final dispatches SHUT_WR to uipc_shutdown; it calls
socantsendmore and unp_shutdown sets the connected stream peer's socantrcvmore.
See [XNU socket shutdown](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/uipc_socket.c)
and [XNU Unix sockets](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/uipc_usrreq.c).
No new dependency or portable assumption about close/EINTR is needed. Preserve
any actual shutdown error and do not claim successful cancellation on its failed
branch. The owned valid connected pair, SHUT_WR argument and single winner are
part of this native method's preconditions; it is not a claim that arbitrary
invalid sockets or an OS policy rejecting shutdown will wake successfully.
Blackbird does not use pthread_cancel for these workers.

A read-only native Darwin study used socketpair/fork, waited for the actual child
SIGSTOP before exec, then shut down/closed the parent's activation endpoint.
The observer was readable and returned EOF while waitpid still showed no resumed
or exited child. That is native-method study, not a participant fixture green.
The direct real-worker/held-credit qualification below remains pending source
release, including the same stopped inherited-endpoint boundary.

### Resolution of mandatory owner cleanup

Ordinary cancel/join/archive/read/drain still admit their finite current-owner
banks before their effects and may refuse before doing them. Mandatory cleanup
is a separate no-new-admission operation. Before publishing a live Run, start
retains its actual setup RawThreadHandle and holds its finite706byte cleanup
bank on that lane. Obtain this bank before native socketpair setup, so partial
setup and failed thread launch already have actual close credit. An inability to hold
it prevents live publication. The worker owns a separate prepared lane/bank;
that bank cannot pay owner stop, join or late close.

The per-Run706 covers cleanup request/terminal142, stop67, one shutdown request/
return96, at most two actual endpoint close pairs186, join115 and owner END100.
The activation close is already one of those two endpoints; it is counted once.
Owner END2136's100 maximum is
E16+cleanupR24+RunR24+worker-END-present1+[R24]+semantic-debt1+branch1+[Error9].
The branch identifies actual joined/unstarted/join-failure; there is no redundant
joined bool. After a real join, the actual worker2126 R is copied from Run,
not inferred from a nearest ordinal. Restored rows have no live worker or
fabricated worker END. Unpublished partial setup may use close slots with start
R and releases unused Run END/action slots. A failed launch after Run setup may
publish the actual unstarted owner END with no worker R.

Mandatory shutdown walks the existing bounded Run set without allocating a
selection vector. It sequentially binds each retained setup handle, uses only
that Run's held slots, stops/shuts down every unjoined worker, joins outside the
mutex, closes remaining owned endpoints after join and emits its single owner END/terminal. All Runs
are cleaned even if an earlier Run has semantic debt or an actual native failure;
return the first actual error afterward. A guarded business finished flag does
not mean the native thread joined: it may still be in its final Publish. Shut down
every unjoined live worker even on that branch. An already cleanup-completed or
archived Run is skipped; unused branches release credit. No fresh outer action
request, seal operation,
raw_claim_control, raw_wait_data, Retain, Save or Publish is inserted into this
mandatory path. Mandatory stop closes admission, so it does not call the ordinary
join wrapper that first seals/adopts a fresh bank. For32 live Runs the maximum
preheld owner cleanup is32*706=22592 bytes, issued on their actual setup lanes.

The retained handle grants lifetime, not concurrent use of its producer. The
Participants owner and its callbacks remain serialized; before a cleanup binding
on another thread the previous owner has ended/joined and handed sole ownership
over. Never use the same retained producer concurrently with a still-running
caller. Held claims die before their handles. Keep the Run's worker/setup handles
owned after join until ordinary recorder preservation can proceed: releasing the
last mapping can perform retirement and wait for a stopped recorder. Mandatory
cleanup's join/endpoint completion must not secretly include that retirement wait.
Archive/destruction later release those handles under existing orderly preservation.

Pending originals remain owned/raw-preserved and are reported as semantic debt,
including a known original not yet Retained and an unknown-used extent that cannot
be parsed. Mandatory shutdown does not call that an accepted snapshot or successful
Retain. Explicit shutdown reports the debt after stopping/joining all workers;
its destructor cannot advertise success. To retain final joined-worker originals
on a normal exit, the caller performs an explicit ordinary owner drain after
mandatory worker cleanup has completed, under that operation's own admission and
error handling. Coordinate that main/CodingEngine adapter with their owner; do
not hide it inside mandatory cleanup or attempt it under sticky cancellation.
A cancelled exit may leave raw-preserved semantic debt. Cleanup END remains the
truth at its own boundary if a later independent ordinary drain resolves the debt.

Fixed whole-bank arithmetic, conservatively allowing optional fields together:

- Action request+terminal:91+51=142. A child parent only uses fields actually
  supplied, releasing unused bytes; no phantom parent records.
- Capture ticket:2108+2109=104+67=171 CONTROL, plus exact2107 framing DATA and
  the shared source's separately owned pre-acquisition DATA/return credit. This
  ticket bank is obtained before bytes, node allocation or source callback.
- Worker setup:entry73 + final state61 + final Publish91 + END62 +64*74 direction
  outcomes + first worker-origin stop/shutdown/activation-close256 =5279 CONTROL.
  Initial admission has its retained source R, not a direction withdrawal slot. Each
  execution separately prepays2104+2105+normal state+Publish =138+52+61+91=342
  before dispatch, outside the guard. Once admission closes, use held final state/
  withdrawals/END and acquire no new response or argument fact.
- First stop/shutdown/activation-close per Run:67+48+48+45+48=256. Retain/Save
  failure can stop at most32Runs, so prepay32*256=8192 before either callback. Nonexistent,
  already joined or already stopped branches release unused slots; a race loser
  does not publish an imagined second shutdown or close.
- Drain item:transfer56+Retain85+return75+failure-stop8192=8408. Unknown debt uses
  its82byte2135 instead of the160byte Retain pair, from that same bank. Save:
  request53+return67+failure-stop8192=8312. Each actual item obtains its own bank
  outside the guard; there is no imaginary unbounded stream. Cancelled admission
  before Retain leaves the owned node pending, with no fictitious success.
- Stop-socket setup:one socketpair and at most6fcntl calls, each53+58, gives777. Two
  replaced-descriptor close pairs cost186. The two eventual endpoint close pairs
  are already inside the held706 owner cleanup maximum; no second late-close bank.
- start maximum:142+setup332+admission Retain160+Save8312+Publish91+native setup777+
  replaced closes186+held owner cleanup706=10706, apart from separately issued
  worker5279 and source extent. Stage all post-admission objects before Retain.
  Thread construction failure keeps retained admission and emits actual unstarted
  outcomes; it is not a launched request.
- send:142+Retain160+direction return91=393, plus exact2115/source DATA. Prestage
  receipt/ring transfer and returned business object before retention; post-retain
  transfer uses held outcomes only.
- configure:142+48+Save8312=8502. cancel:142+stop/shutdown/close256+Save8312+Publish91=
  8801. Their final read is a separate actual operation, not the caller terminal.
- read:142+at most32*84=2830; drain separately owns its callback banks. await holds
  142 for its terminal and admits each actual drain/read before that operation,
  with no instrumentation polling stream or held period budget.
- ordinary join:142+32*(seal73+join115)=6158. archive:142+join115+archive73+Save8312=
  8642, plus the Run's already held cleanup bank for actual remaining endpoint/
  owner-END publication. It must use held cleanup before erasing the last handle.
  Mandatory shutdown acquires no9326byte ordinary bank; it consumes at most706
  per still-live Run already paid before publication, releasing unused slots.

The counts bound native hooks, not arbitrary callback bodies. Retain/Save/Publish
business code owns its own pre-acquisition admission. Sticky cancellation buys
no infinite callback refusal events. After source admission closes, no prepare
acquires another fact. Held source handoff, worker withdrawals/END and descriptor
outcomes remain publishable without fresh admission even after cancellation/fence.
Current raw_emit allows a valid held DATA/control claim after ordinary context
fencing; each source publishes once.

Acquisition interruption remains distinct from producer/recorder preservation.
Mandatory cleanup can stop/join source workers while the recorder is stopped;
it keeps the mapping handles until later orderly preservation. RawRuntime
destruction itself may still wait for its stopped recorder. A fully published raw
source preserves originals even if final semantic nodes are eventually destroyed.
If physical publication fails, keep the acquired extent/node and report that actual
failure; a locator to destroyed storage is not a successful source handoff.
Power loss remains outside the adopted guarantee.

## Direct red and green oracles

Use tests/participants_raw_test.cpp with literal independently built native bodies
and actual raw disk reader. Root adds the target/descriptions. Preserve business
participants/colleague/engine/recovery checks; replace weak orphan/absence checks
with exact native boundaries where relevant. No timers, mutants on the laptop,
test-of-tests, new review panel or repeated broad gate.

1. Genuine current9MiB loss: actual prepared participant transport acquires a
   deterministic9MiB binary original (independent bytes include NUL/high bytes),
   reaches the actual8MiB queue branch, then stops. Existing code drops it.
   Candidate raw source preserves every byte before the exact capacity outcome;
   emergency ownership is visible and no subsequent transport acquisition occurs.
   At the1024event boundary, stage a source ticket while a slot still exists,
   let another gated worker fill that slot, then complete the actual acquired
   crossing original; inspect it rather than merely final unknown state. A
   preflight refused at an already full queue legitimately acquires no body;
   check that branch separately without demanding a fabricated original.
2. Real native source cut: forward actual OS read, retain its exact returned
   buffer/ssize_t/errno in fixture memory, inject cancellation and allocation
   refusal after positive read but before participant queue publication. Compare
   disk1404/body, exact ticket Ref/range and eventual semantic original. No callback
   fabricates an OS return, no duplicate raw publication, no post-transfer borrow.
3. Post-retain send cut: actual retained journal accepts direction admission;
   reject every allocation in the subsequent acceptance/queue transfer interval
   on Mac/libc++ and supported Linux/libstdc++13. Baseline exposes the receipt/
   deque problem; candidate either accepts a queued direction once or reports a
   true pre-enqueue withdrawal with original retained. Duplicate/reordered input
   names the original accepted R and never dispatches twice.
4. Retain/save failure: reject the actual selected original/state append using
   native file identity/call forwarding. Check source full bytes, stable same
   pending node/Ref, actual error return, all live stop/shutdown branches, no dispatch
   retry, no successful semantic SourceReference/identity invented. A successful
   append followed by blanket allocation refusal must have no discarded hex-ID
   allocation and no second append on drain retry. Reentrant callback cannot pop
   a different front. Verify callbacks stay on owner; worker never ContextStore.
   For fill then throw before commit, compare the full initialized unknown
   extent/range, kind2 and absent observed-used in source/handoff; require
   emergency ownership/stop and2135, with Retain/parse call count0. Commit then
   throw retains its exact prefix/kind1 and the original exception separately.
5. Direction custody: gated two actual workers, one continued direction and
   one cancelled Run with16accepted directions. Resolve each offer/admission to
   exactly one actual dequeue or withdrawal on its Run's worker lane. Exercise
   both stop-during-transport and stop-after-condition-wait branches; exactly
   zero withdrawn requests dispatch. Mutate the actual provider descriptor after
   admission and before dispatch: require the worker's exact admitted binding,
   actual conflict/no-dispatch on stale expected binding, and no second admission
   encoder. Check original from/text and actual origin
   identities, not lane-wide ordinal coincidence or a manufactured global clock.
6. Setup/shutdown: actual socketpair/fcntl/launch failure branches, callback
   failure, source failure after a published partial original and worker bad_alloc.
   Stop the fixture's actual recorder and park a real worker in raw admission.
   Fork a test-owned child that retains the activation endpoint, performs no exec
   and stops itself with SIGSTOP. Waitpid must confirm it is stopped before
   mandatory stop. Forward the real shutdown(SHUT_WR) and close, and require
   exact method/FD/how/native result/errno plus persistent observer readability/
   EOF and worker held withdrawals/END/join WHILE that child remains stopped and
   still holds its alias. No wake write is attempted; count it as0. This replaces
   the candidate failed-write oracle since that fallible method was removed.
   If the prior pipe fallback is temporarily exercised as a design red, its first
   write fails EINTR with no byte, parent close cannot give EOF while this alias
   remains, and fixture cleanup kills only its child to release that native wait.
   Gate competing owner/worker first stops; exactly one takes the endpoint and
   issues shutdown/close, and no loser uses the retired numeric FD. No endpoint
   data or full-pipe test is needed for the candidate's zero-write method.
   Ready the actual owner's sticky cancellation descriptor and fill every
   unheld owner CONTROL byte with the recorder stopped. Mandatory shutdown must
   emit held stop/shutdown/close/join/END/action outcomes on actual saved owner
   lanes, with zero raw_claim_control/raw_wait_data after mandatory entry. Require
   original pending-source references and semantic-debt branch; Retain/Save call
   count is0. Ordinary cancel/join separately refuse before effects. Verify actual
   worker END references, exact original native shutdown/close/join fields and
   owner END2136 body, not only counts. A later explicit ordinary drain may
   resolve exact-source debt after recorder resumption; unknown-used stays debt.
   No raw wait occurs under a mutex. Every failure cleanup resumes/terminates/
   joins only the exact test-owned stopped child/holder/worker before reporting
   failure; mapping retirement and RawRuntime destruction occur after recorder
   resumption. Child post-fork code uses native async-signal-safe operations only.
7. Restore/archive/read: genuine saved running/waiting rows reopen unknown with
   their exact original previous state and no new worker/provider call. Require
   selected row counts/flags, exact seal/join/archive and END R per real worker,
   no duplicated END or fabricated worker for restored rows. Independent business
   request/result originals retain reported failure/partial metadata. Read rows
   come from the actual business traversal, not a second metrics census.

Qualify one Mac focused native green plus changed-TU narrow static checks, then
root performs this unit's ONE code/oracle attack. Resolve concrete findings and
recheck affected cuts. Root owns supported Linux/debug/release/sanitizer gates,
completion/journal/beads and merge. This paper grants no source release itself.

## Source ownership and integration

The shared provider contract is now resolved against root's ONE provider plan
attack: initialized unknown extent, max callback DATA16MiB+58, native carrier
kind/optional-used, actual RawPartitionu32. Root completed the ONE whole
participant plan attack in
[participant plan attack](2026-10-11-participant-owner-raw-plan-attack.md). Both
inherited-endpoint cancellation and mandatory-cleanup finding resolutions are now written above, including
corrected native shutdown widths, whole banks and direct inherited-endpoint cuts. Parent finding
acceptance and the completed-owner checkpoint still precede source release.
No participant source is released and no second plan panel is requested.

Participant owner owns new raw_participants.hpp/cpp, participants.hpp/cpp,
participants_raw_test, affected existing participant/recovery fixtures and this
paper/docs. Coordinate colleague capture encoder/transport changes and the shared
source-ticket implementation with provider_raw_vertical. Provider owns the
physical source lease, Child/provider adapters and source descriptions; this
owner owns the prepared participant receiver. Do not race both writers on those
files or implement two source leases. Root owns CMake/raw description integration.
Coordinate CodingEngine constructor/start origin/callback changes and main
shutdown linkage with command owner before editing common files; select named
boundaries and format only owned ranges while another lane edits coding/main.
