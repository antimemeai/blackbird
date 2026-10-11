# Command output, control and foreground routing

2026-10-11. Proposed owner subplan for completion group3: census rows10,49–50,
systems E/F. Study and design only; source changes wait for the integrated
runtime/provider/reader checkpoint and the root's targeted design/plan attack.
No dependency, evaluation, new timer or broad research campaign.

## Source and reference grounding

Read current `command_jobs.cpp`/header, `command_job_runner.cpp`, terminal PTY
routing and keyboard read, coding command admission/drain/output retrieval,
the existing native command fixture/tests, the acquired platform study and
provider native-child subplan/implementation. Their mechanisms stay owned here.

The acquired POSIX Issue8 read/write contracts distinguish positive partial
transfers, zero/EOF and negative native returns; interrupted partial transfers
are positive amounts. The wait contract and installed Darwin probe distinguish
exit, stop and continuation. Read the actual acquired HTML read/write/wait
sections, not just the earlier report. The GNU C Library2.44 shell example and
POSIX job-control rationale separate attachment, execution state and terminal
foreground. Tmux `job.c:308–381` keeps output closure and child death separate.
Its callback/libevent implementation is not adopted. The acquired oh-my-pi
PTY fixture at `pty.rs:781–825` independently constructs2MiB and checks exact
ordered delivery through a bounded queue; borrow its independent byte oracle,
not its async timers, strings-as-stream representation or callback pump.
Acquisition identities/restoration remain in the existing
[platform study](2026-10-09-command-jobs/platform.md) and
[harness study](2026-10-09-command-jobs/harnesses.md).

The existing worker blocks SIGWINCH. Spawn explicitly gives the child an empty
signal mask and resets its signal dispositions. The fresh PTY helper creates
its own session/controlling terminal before exec. Keep these behaviors; changing
the worker mask to make an instrumentation budget convenient is not justified.
Background releases a particular wait epoch. It neither stops nor relaunches
the process. Foreground attaches another wait to the same job/PID. Preserve
these distinctions in the events and tests.

## Owners, identities and original bytes

Use an inline owned `RawCommandTrace` in the worker, with its prepared lane and
finite cleanup claim created before dispatch. Add `raw_command.hpp/.cpp` and
immutable descriptions before any new site can publish. Sites2000–2047 are
assigned to this owner. Authored1500–1599 remain reserved; check the integrated
immutable descriptions before activation.

`Ref` is exactly lane_epoch[16],ordinal:u64. `Text` is size_t length followed by
the exact existing string bytes. The job identity is its existing textual32
hex bytes, copied as Text; do not parse it into a metric identity. Delivery IDs
are existing opaque strings of at most128bytes. Existing native wait epochs,
offsets, written amounts, queue amounts and resource handles are copied directly.
No new metric counters, durations, byte totals, formatted IDs, Value/BBM dump,
argv/environment serialization or metric dictionaries. Branch/stage/channel
constants have explicit u8 representations and describe source boundaries.
Every field is a separate span; never emit pointer values or C++ struct padding.
Descriptors and PIDs supplement the job/launch Ref; they are not incarnations.
Snapshot errno immediately after the native call, copy it unchanged even on
success, and preserve caller errno around hooks.

Link the caller's command request Ref to worker dispatch/launch Ref by explicit
handoff. A `JobEvent` chunk carries the read-return Ref alongside its existing
shared original string and offset. Queue/drain/retention records use this Ref;
they need not copy the same output body again. Worker code never touches
ContextStore. Parent and worker claims remain on their own lanes: a claim is
not a transferable reservation against another producer.

The raw output-return record contains every byte actually read, before string
allocation, limit checks, queue insertion, presentation or semantic append.
Current output_limit rejection discards an acquired chunk: retain that crossing
chunk in the bounded pending/tail ownership, mark capture incomplete/EFBIG,
then stop further reads instead of acquiring another rejected chunk. The
pending allowance already prepays an8192byte chunk. This fixes the concrete
nls.10 overlap without claiming that all semantic-audit deficiencies are fixed.
If later semantic append fails, the emitted read Ref still names preserved raw
originals; do not substitute a locator to an expired string.

## Exact sites and body layouts

E is the16byte envelope; R is24. Native widths are published in the existing
ABI description. Error is ErrorCode:u8,detail:i64. Optional fields are stated
below rather than encoded as a universal map. Each request has its own distinct
return site and Ref. Begin records never imply the effect completed.

| Site | Ordered body | Actual boundary |
| --- | --- | --- |
|2000 command request|job Text; operation:u8; originating-owner-present:u8; when present existing attempt[16],invocation[16],generation[16]|Before owner admission/validation; malformed caller payload originals remain with the upstream operation/UI owner, not reserialized here|
|2001 command admission return|request Ref; branch:u8; Error; max_live:size_t,max_records:size_t|Actual registry admission/refusal or worker construction exception|
|2002 worker launch|request Ref; job Text; pty:bool; lifetime:chrono::seconds::rep|Worker binding before native setup; published identity of this launch|
|2003 spawn return|launch Ref; native-stage:u8; result:int,error:int; pid:pid_t; group_ready:bool|Actual posix_spawnp return or failing setup operation, before throw|
|2004 bootstrap read request|launch Ref; fd:int; requested:size_t|Before each bounded helper-pipe read|
|2005 bootstrap read return|request Ref; result:ssize_t,error:int; if positive exact result bytes|Before protocol interpretation; preserve actual ready/failure wire bytes|
|2006 bootstrap disposition|launch Ref; group_ready:bool; native-stage:u8; failure:int|Actual helper-ready/failure/protocol refusal; no invented exec success|
|2007 output read request|launch Ref; fd:int; offset:size_t; requested:size_t|Before each bounded output read|
|2008 output read return|request Ref; result:ssize_t,error:int; if positive exact result bytes|Before allocation/limit/append; PTY EIO stays EIO here|
|2009 output queue publication|read Ref; job Text; offset:size_t; pending_bytes:size_t,tail_bytes:size_t; branch:u8; Error|Actual retained queue insertion, crossing-limit stop or allocation refusal|
|2010 output drain|read Ref; job Text; offset:size_t; pending_bytes:size_t|After actual transfer of the shared original to owner, before returning JobEvent|
|2011 output preserve request|read Ref; job Text; offset:size_t,length:size_t|Before semantic original append; body is already in raw read record|
|2012 output preserve return|request Ref; retained:size_t; branch:u8; Error|After actual append/retained advance or actual refusal; no claimed preserve on failure|
|2013 delivery request|job Text; operation:u8; delivery Text; operation-specific existing fields below|Before control/mailbox admission; captures offered input before refusal|
|2014 delivery admission return|request Ref; branch:u8; written:size_t; done:bool,failed:bool; Error|Actual queued/delivered/duplicate/conflict/refusal; duplicate is observation, not another write|
|2015 mailbox transfer|request Ref; job Text; delivery Text; branch:u8|Actual enqueue/dequeue/withdraw; existing UI-generated request ID is reused|
|2016 stdin write request|launch Ref; delivery Text; fd:int; written:size_t; requested:size_t|Before write of the remaining retained delivery bytes|
|2017 stdin write return|request Ref; result:ssize_t,error:int|Before advancing existing delivery written amount; never replay accepted prefix|
|2018 resize request|launch Ref; delivery Text; fd:int; rows:unsigned short,columns:unsigned short|Before TIOCSWINSZ, including initial resize with empty delivery Text|
|2019 resize return|request Ref; result:int,error:int|Before marking delivery done/failed|
|2020 signal request|launch Ref; delivery Text; target:pid_t; signal:int; method:u8|Before actual group/leader kill or getpgid check; empty delivery Text for cleanup|
|2021 signal return|request Ref; result:int,error:int; returned_group:pid_t when method=getpgid|Actual native return; successful kill is dispatch, not observed exit|
|2022 wait observation request|launch Ref; idtype:idtype_t; selector:id_t; options:int|Before each ordinary waitid, exit and nonterminal selectors separately|
|2023 wait observation return|request Ref; result:int,error:int; si_pid:pid_t,si_uid:uid_t,si_code:int,si_status:int|Before business exit-code conversion or stopped-state update; initialized fields are not asserted to be valid when call fails|
|2024 foreground wait request|job Text; yield-present:bool; when present yield_ms:u64; max_output:size_t; stop_on_cancel:bool|Before attaching to a wait; holds its terminal claim|
|2025 foreground wait admitted|request Ref; epoch:u64; pty:bool|Actual acquisition of the existing waiting slot|
|2026 foreground wait return|request Ref; epoch:u64; branch:u8; Error; terminal_retained:bool,leader_exited:bool,output_closed:bool|Actual completed/released/yield/cancel/refused/exception boundary, separately from exit|
|2027 background request|job Text; requested_epoch:u64|Before checking matching existing foreground wait|
|2028 background return|request Ref; current_epoch:u64; released:bool; Error|Actual epoch match/refusal, including stale request|
|2029 stop request|job Text; source:u8|Actual API/cancel/deadline/capture-failure/shutdown stop request, before sticky wake|
|2030 descriptor close request|launch Ref; channel:u8; fd:int; reason:u8|Before each owned close, including forced capture close|
|2031 descriptor close return|request Ref; result:int,error:int|Actual close result; no close retry on reused numeric descriptor|
|2032 final reap request|launch Ref; pid:pid_t; options:int|Before existing final waitpid retry operation, after last owned-group signal|
|2033 final reap return|request Ref; result:pid_t,error:int; status_valid:bool; when valid status:int|Actual final native return; this selected boundary is the complete reap operation, not each internal EINTR retry|
|2034 worker terminal|launch Ref; job Text; pid:pid_t; received:size_t; failure:int; capture_complete:bool,timed_out:bool,stop:bool,leader_exited:bool,output_closed:bool; custody:u8|After actual native cleanup; never derive exit status when none was observed|
|2035 terminal preserve request|worker-terminal Ref; job Text|Before semantic submit_settlement using existing encoded result|
|2036 terminal preserve return|request Ref; branch:u8; Error|Actual settlement/settled result; no second result encoding for metrics|
|2037 retained output read request|job/output_ref Text; mode:u8; requested offset/count size_t when already native, otherwise exact existing numeric Text|Before resident tail or full retained-source retrieval; no numeric parse for metrics|
|2038 retained output source|request Ref; existing fact/source identity and existing range|At the existing traversal/read, before append/presentation; no second history walk|
|2039 retained output read return|request Ref; branch:u8; Error; actual known returned source size/range where existing|Before returning projection or propagating native failure; no presentation-derived byte estimate|
|2040 keyboard read request|fd:int; requested:size_t|Before chosen bounded128byte STDIN read|
|2041 keyboard read return|request Ref; result:ssize_t,error:int; if positive exact result bytes|Before composer/paste/control parsing; enables exact original keyboard bytes even when refused|
|2042 PTY route|keyboard-read Ref when applicable; job Text; foreground_epoch Text; routed Text; branch:u8; pending:size_t|Actual append_child_input acceptance/refusal, before excess bytes disappear; literal route bytes are not reconstructed from presentation|
|2043 PTY flush request|job Text; input Text|Before mailbox request; preserve offered pending bytes and their original job|
|2044 PTY flush return|request Ref; accepted:bool; Error|Before clearing accepted child_input; later worker delivery is a separate event|
|2045 command ownership change|job Text; operation:u8; branch:u8; Error|Actual configure/archive/shutdown state/refusal, not a registry-wide metric snapshot|
|2046 collector poll request|launch Ref; output_fd:int,bootstrap_fd:int; output_events:short,bootstrap_events:short; timeout:int|Before existing native collector poll; no new timeout|
|2047 collector poll return|request Ref; result:int,error:int; output_revents:short,bootstrap_revents:short|Actual zero/readiness/EINTR/error, before the next collector iteration|

Delivery variants: input copies Text bytes; resize copies existing requested
rows/columns numeric Text before business validation, then native ushort in
the actual ioctl request; signal copies existing signal-name Text, then native
signum in the actual signal request. Framing lengths and event admission size
arithmetic are transport requirements, not emitted product calculations.

PTY bootstrap remains a thin owned protocol. Extend failure packets with an
explicit source-stage byte so actual helper setup/exec failures are distinguishable;
raw read2005 preserves the complete protocol bytes. Bootstrap buffer becomes
sizeof(int)+3: ready byte plus failure tag/stage/int can arrive in one read.
This does not pretend the
collector observed every syscall inside the separate helper. Success means
private terminal setup was reported ready, not successful target exec.

## Admission, ownership and cleanup

Prepare/register the worker lane outside the command mutex and spawn mutex,
before creating the worker. Preclaim its complete finite setup/cleanup/terminal
sequence on that lane before dispatch, using a temporary binding only during
setup; worker handoff is synchronized by thread construction. Release unused
claim before unbinding. Never share a producer concurrently or borrow a claim
from its issuer after that producer retires.

Create one job-owned nonblocking sticky cancellation pipe during setup, with
CLOEXEC normalization under the existing spawn mutex. Its descriptors outlive
all waits. Stop/capture_failed/stop_all/turn cancellation sets existing job.stop
and writes a single sticky byte; worker waits borrow its read endpoint and
the existing immutable job deadline. Do not use input/resize arrivals as raw
admission cancellation, drain this byte, or schedule a new timer. The existing
TERM200ms/KILL/close500ms and collector poll10ms are business behavior, not a
new instrumentation timer. Final integration must include the raw runtime's
non-atomic pipe/socketpair CLOEXEC window in the existing spawn-serialization
discipline; registering a context while holding the spawn mutex would wait on
RPC and is forbidden.

Per-read control claim prepays both distinct request and nonpositive response.
Positive reads preflight E+R+sizeof(ssize_t)+sizeof(int)+requested data capacity
in addition, before acquisition. No callback, allocation, owner yield or another
emission consumes that checked data capacity between check/read/publish. Native
requests use the prepaid control claim, preserving the checked data bytes.
For output, requested=8192; bootstrap is bounded by its remaining fixed packet.
On admission cancellation/deadline/fence, no read occurs; earlier publications
remain. Cleanup skips further ordinary acquisitions and records forced closure
and actual reap from held credit. It cannot call raw_wait_data/raw_claim_control.

Select one bounded worker-control burst before entering the existing product
mutex: at most one initial resize, one queued signal, one queued input/resize.
Preclaim request+return bytes before locking, using max128byte delivery identity
and actual native widths. Unused credit is released after the burst. The worker
then owns stable delivery strings while under the lock; no raw wait occurs
there. Keep the current queue ordering and duplicate identity rules. Native
write result is recorded before written/input_bytes mutation. The main owner
similarly prepays delivery, drain and state-return bursts before its mutex.
For drain, limit remains at most64 chunks per selected job and the actual
configured registry limit is bounded256; preflight chosen active count rather
than accumulating historical archived jobs. Prefer selecting smaller bounded
transfers if a configured lane cannot represent the whole chosen burst.

### Sequential handoff and retry-preserving acknowledgement

Job retains its RawThreadHandle until every claim against that producer is
released. Worker cleanup, one semantic settlement pair and one owner cleanup
retry are prepaid on that WORKER producer before dispatch. The Job owns this
bundle; terminal JobEvent carries shared ownership, never a naked producer or
a claim against a transient engine-turn producer. Declare the context before
its claims so claims die first. Join any live worker before Job destruction
releases a claim. Worker binding is lexical: emit terminal, publish finished
under the mutex, leave the binding, return. Finished is a scheduling hint, not
permission to rebind before the old binding destructor has run.

The registry owner selects finished Job under the mutex, releases the mutex,
joins the worker, then sequentially binds its retained handle for settlement
or custody retry. Join supplies completion and C++ happens-before. No product
mutex is held while joining/binding/emitting/appending. Output-drain while a
worker remains live never transfers its producer/cleanup claim to the parent.

Make drain non-destructive until acknowledgement. Return shared original chunks
and their exact read Refs from pending without popping them. retained(id,end)
validates offsets, removes only acknowledged prefix chunks, updates existing
pending_bytes and wakes the collector. Interrupted fresh owner admission or
failed append keeps the exact chunk pending; later drain retries preservation
without rereading the child or cloning its payload. Select at most64 chunks per
chosen job, and prepay selected bounded drain records before locking. Transfer
selection must not include all historical jobs merely because they exist.

Cache the original terminal observation in Job and offer it until settled()
acknowledges successful submit_settlement. Do not set terminal_sent before
submission. Forward its original raw Ref; build its small native result once.
Fresh output-preserve request/return credit is acquired on the current owner
lane BEFORE log_.original. Interrupted admission postpones append; it does not
set command_failure_, increment unretained_bytes_ or clear the pending chunk.
Genuine semantic append failure records its actual error, stops acquisition and
keeps shared originals/Refs through root recovery. Do not blindly retry a
poisoned writer. Current command_failure_ paths must not discard these chunks.

After join/bind,2035/2036 consumes a protected settlement pair around the actual
submission whether it succeeds or fails. One actual initial attempt is protected.
Failure leaves the cached terminal offered; later explicit live-owner retry credit
comes from this same retained worker lane before another submission and outside
the mutex. Cancellation postpones, never consumes the only terminal. After an
acknowledgement/publication error, use root.attempt()'s existing state to check
whether the terminal is already retained before submitting again. No duplicate
semantic terminal. Persistent root failure keeps raw originals and debt.

### Actual cleanup budget and custody

Use custody not_launched/owned/reaped/unavailable/pending. Unify final owned
group action/reap outside normal/error branches: the current duplicated normal
and fallback path must not produce a second unbudgeted sequence after a native
error. Store actual PID/native error and owned_children contribution in Job.
Clear it exactly once after waitpid==pid or ECHILD. ECHILD means parent custody
unavailable, not observed exit, and forbids later signals to that numeric PID.
Other non-EINTR failures keep pending custody and the contribution. Debt includes
pending custody even if an unknown semantic terminal was retained; archive
refuses it. Preserve existing signal masks and stop/continue semantics.

On the current64bit Mac/Linux ABI E16,R24,int/pid_t/uid_t/idtype_t/id_t4,
size_t/ssize_t/seconds::rep8,bool/ErrorCode/u8=1. Implement checked sizeof
equations; these constants do not establish another ABI.

| Protected operation | Exact maximum bytes | Source count |
| --- | --- | --- |
|Close pair|46+48=94|Pipe:4 original normalization+3 postspawn+1 final output=8. PTY:2 bootstrap normalization+master+bootstrap writer+bootstrap reader+input master+output master=7. Failure unwind closes each acquired descriptor once|
|Cleanup group kill pair|57+48=105|Empty delivery Text contributes its8byte length|
|Optional getpgid pair|57+52=109|Native returned group present only for getpgid|
|Optional leader kill pair|57+48=105|Same pinned PID|
|One signal_owned|105+109+105=319|Worst branch; at most TERM,KILL,final cleanup=3|
|Final reap pair|48+53=101|One selected operation begin/final return; optional valid status|
|Worker terminal|102|One actual terminal/custody observation|
|Setup result|54|One actual native setup/spawn result|
|Acquisition refusal|50|2001 worker variant: launch Ref,operation:u8,Error; immutable description distinguishes it from caller variant|
|Worker launch|89|Prepaid before dispatch, no startup data wait|
|Worker setup/cleanup claim|8*94+3*319+101+102+54+50+89=2105|Finite protected sequence|
|Owner retry ownership probe|52+64=116|waitid(P_PID,pid,WEXITED\|WNOHANG\|WNOWAIT)|
|One owner cleanup retry|116+319+101+102=638|Probe, optional one signal_owned, reap, updated custody|
|Semantic settlement pair|80+50=130|2035/2036, existing32byte job ID|
|Initial held bundle|2105+638+130+1728=4601|Worker cleanup, first owner shutdown retry, one semantic attempt with nested region allowance|

The ordinary read/write/poll/waitid paths acquire fresh credit per occurrence.
The final reap site selects the existing complete reap operation and preserves
its final native return/status, matching provider Child. It does not claim each
internal EINTR retry, compute a retry count or summarize interim errnos.

Ordinary maximum pairs: output request60/nonpositive return52=112control plus
8244data for a positive8192byte read; bootstrap request52/nonpositive return52
=104control plus59data for its maximum7bytes; write196+52=248control;
resize184+48=232control; queued signal_owned703control including its128byte
delivery ID on each of its maximum3 native operations; initial resize104control.
The selected no-wait mutex burst is104+703+max(248,232)=1055control. waitid pair
is116control; poll pair56+52=108control. Fresh owner output-preserve pair is
96+58=154control. One-job64chunk drain record burst is64*96=6144data. Foreground
wait's admitted+return claim is49+61=110control; its max begin is74data. All
admission precedes the corresponding native call/product mutex; record actual
smaller strings/results and release unused credit.

shutdown stops/joins workers outside the mutex, then sequentially binds each
pending worker lane for the held638byte retry. Probe native child ownership
BEFORE signaling: ECHILD marks unavailable/no kill; another probe failure leaves
pending/no kill. A valid P_PID selector confirms this parent still owns that
child, including a WNOWAIT-pinned terminal child. Then final group action
precedes reap. Further explicit retries preclaim fresh capacity before any new
native acquisition; they do not consume settlement credit or touch a live
worker's producer. No unbounded busy retry is introduced.

Expose orderly-shutdown status to CodingEngine; shutdown_commands refuses
completion/refit with pending custody. The noexcept destructor performs the
already protected attempt and, on persistent failure, preserves the raw unknown
observation/run and keeps the process-wide owned_children/refit fence set.
Never silently claim release. Process teardown is a separate boundary; no later
PID reopening can restore the dead parent's promised wait history.

### Bounded keyboard burst and actual cancellation

Use128byte native STDIN acquisition, retaining the4096byte pending child-input
allowance. Prepay the entire foreground-PTY read/route/flush burst before that
read; pass its inline claim to nested CommandJobs::request admission hooks so
none acquires another claim inside the burst. Byte-bearing read/route/request
records use the data partition; fixed returns use held control. The existing
paste/control parser remains business work. Every refused byte is already
preserved by2041 and its actual route refusal by2042.

At most128 new bytes and5 bytes of existing undecided escape prefix exist.
Budget129 route/flush attempts (extra for old-prefix/after-read flush),128
Ctrl-B background requests, hence257 mailbox requests/transfers. Repeated failed
flushes may each offer the same4096byte pending prefix; their total is NOT just
new keyboard length. Maximum UI-generated delivery Text is64bytes:32job+4
':ui:'+20decimal request counter+8length. Existing epoch Text is at most28.

| Foreground-PTY burst | Maximum admitted bytes |
| --- | --- |
|Positive read|52+128=180 data|
|Route, including6byte invalid escape|129*(16+24+40+28+8+6+1+8)=16899 data|
|Flush bodies|129*(16+40+8+4096)=536640 data|
|Input mailbox requests|129*(16+40+1+64+8+4096)=545025 data|
|Background mailbox requests|128*(16+40+1+64+28)=19072 data|
|Mailbox transfers|257*(16+24+40+64+1)=37265 data|
|Total data preflight|1155081 bytes|
|Read request/nonpositive return|28+52=80 control|
|257 admission returns|257*60=15420 control|
|129 flush returns|129*50=6450 control|
|Total held control|21950 bytes|

These maxima fit current64MiB data/256KiB control; a smaller configured lane
refuses before acquisition. Runtime uses actual body lengths/fields and releases
unused credit. This is admission arithmetic, not an emitted aggregate or a
performance claim. A later natural-burst measurement can revise native read
size with the same budget calculation. No sampling, filtering, deduplication,
delayed summary or new timer. Non-PTY composer surfaces remain their own later
owner; their original STDIN bytes still publish immediately before parsing.

Create a separate session-owned nonblocking CLOEXEC SIGINT selfpipe in main;
keep it alive through UI/engine waits and select it in main/UI RawWaitScope.
Signal handler preserves errno, sets the existing interrupt indication and
writes a sticky byte, using only signal-safe operations; qualify the current
atomic indication's lock-free requirement. Block SIGINT while restoring its
handler/invalidation/closure so a late handler cannot write a reused FD. The
mixed SIGWINCH/message pipe is not sticky cancellation.

UI owns a separate turn-cancel pipe. Replace every cancelled.store(true) site
with the owner helper that sets the atomic AND writes the pipe. Turn worker
RawWaitScope borrows turn-cancel plus session SIGINT. Reset/drain owner pipes
only after the preceding turn worker joins, before context preparation/dispatch;
waiters never drain them. Move interrupt reset out of the running perform()
worker into that owner boundary. Job workers borrow their own sticky stop pipe
and existing immutable deadline, not the transient turn pipe: background jobs
survive ordinary turn completion. stop_on_cancel transfers actual foreground
turn cancellation to that job's pipe. SIGINT wakes stalled UI/engine admission
so the owner can reach stop/join despite a full keyboard lane. Ctrl-B cannot
rescue this state because its bytes remain unacquired; the SIGINT escape is
explicit, not an invented successful keyboard read.

### Descriptor inheritance invariant

Use the existing inline process_spawn_mutex from process_lifetime.hpp in all
matching non-atomic creator/CLOEXEC windows. raw_runtime make_pipe/control_pair
and raw_channel raw_socket_pair lock only creation+normalization, releasing
before registration/RPC/waits/fork. RawRuntime open/recover_orphans run before
workers. Supervisor fork follows creator lock scopes; supervise make_pipe
returns before start_recorder fork. Every child inherits this mutex unlocked;
never hold it across fork or let setup claim/RPC wait while holding it.

Native Child pipes, command pipe/posix_openpt and terminal notification pipes
already hold this same mutex. In-process file opens use O_CLOEXEC. PTY helper's
non-CLOEXEC open is in a fresh single-thread executable and gets stdio binding
before exec. Additional matching windows are provider_oauth Callback listener
socket and accept followed by unchecked CLOEXEC. Normalize listener under the
same mutex, check fcntl errors, make it nonblocking; then accept+client CLOEXEC
normalization can hold this mutex without blocking accept. Poll, auth parsing,
cancellation and network waits stay outside. This fixes the same descriptor
inheritance invariant; no OAuth body hook or authentication redesign.

### Prepaid nested retained-append regions

Root's targeted attack selected a narrow TLS RawRegionPrepaidScope borrowing a
job-producer control allowance only after completed-worker join/handoff. It has
no subscriber, callback registry, transformation or general SDK. RawControlClaim
split(bytes) transfers remaining bytes to a child claim against the SAME producer
without incrementing held accounting; parent remaining decreases, and child
unused bytes release normally. Borrowed allowance, split claims and scope cannot
outlive Job's retained context. A scope on one thread cannot fund another lane.

RawRegion selects this mode only for the existing retained_append site family.
Each region splits432bytes for its whole native BEGIN/at most4 observations/
END/outcome/error sequence, all emitted CONTROL. Site IDs and bodies are unchanged;
immutable description permits that protected partition variant. No new data
wait, cancellation check, allocation or recorder admission is performed after
this scope starts. Normal regions remain unchanged. Clock samples are still
actual native clock return fields; no duration is calculated. The scoped API
owns capacity, never computes an emitted count or folds events into a summary.

Grounding for the four-region bound: submit_impl submits ZERO sources and ONE
AttemptObservationEvent. A successfully encoded event has length T<=actual
journal max_payload M; failure to encode never enters retain_rejection. Its
ARPROP01 rejection proposal is B=48+4+T=52+T, hence B<=52+M. Journal validity
requires M>=24 and max_batch>=M+88. The empty RejectedSubmissionEvent marker is
20bytes; even a larger allowed IdentityConflictEvent marker only reduces credit.
retain_rejection limits proposal bytes to floor((M-marker_size)/24)*M, sufficient
for its actual marker dependencies. If an actual proposal needed at least3chunks,
B>2M with B<=52+M implies M<52. Then that dependency limit permits at most ONE
chunk, so encode_retained_proposal fails BEFORE diagnostic append. Therefore
any executed diagnostic rejection has at most2chunks. Every individual source
frame fits a valid batch:56commit+32frame+M<=max_batch. There are no empty groups,
so separated diagnostic grouping has at most2 source groups plus1 marker append.
Together case has1 diagnostic append. diagnostic=true forbids further recursion.
One outer+at most3 diagnostic append_impl calls means at mostFOUR regions.

Each region fixed maximum is77clock begin+86clock end+28outcome+33error=224.
Observation is52bytes. Together diagnostic case has journal-state+encoded-marker
+at most2source observations=4, so432 covers the largest individual region.
Four*432=1728 protected allowance per actual semantic attempt. Source-derived
unused capacity is intentionally accepted to avoid carrying source/event counts
through the business append API for instrumentation. The tighter cumulative
actual-path claim1312 is not selected; fixed per-region capacity is simpler.
Initial complete Job bundle2105+638+(130+1728)=4601bytes, with ONE protected
semantic attempt. No arbitrary two-attempt promise or unbounded cancelled retry.

An actual failed semantic attempt retains originals/terminal/debt. A subsequent
explicit live-owner retry acquires a fresh1858byte semantic request/return+nested
allowance before any semantic effect; its interruption postpones the attempt.
The638byte native owner-shutdown cleanup claim remains separate and available
regardless of semantic retry admission. A successful duplicate carrying uncertain
RetainedEvidence must not acknowledge a definitely retained terminal: preserve
unknown/pending debt until ordinary root recovery supplies actual retention.

Ordinary output-preserve append retains fresh admission on the current owner
lane, before its semantic effect. Existing RawRegion BEGIN/observations are before
journal write; interruption there postpones preservation while shared originals
remain. Actual poisoned/partially retained diagnostic root failures follow root
recovery and must not be reclassified as harmless pre-effect cancellation. This
protected settlement scope is not silently applied to arbitrary ordinary appends
or checkpoint maintenance callbacks with an unbounded unrelated region count.

This mechanism and its direct split/whole-region cancellation/full-data oracles
remain unimplemented at this study checkpoint. No source/build until parent
releases the integrated foundations checkpoint.

## Direct red oracles and execution

Use new `tests/command_raw_test.cpp` plus the existing separately executed
`command_job_fixture`. Keep native byte/status fixture expectations independent
of the production event helper/decoder. Assemble expected literal binary data
in the test/child, read the actual preserved audit files and decode spans in
the fixture; do not compare one production serializer with itself. The dedicated
build is `build/nls-command`, one compiler worker. Parent owns aggregate gates.

| Direct fixture/cut | Independent oracle |
| --- | --- |
|Pipe identity, split binary and exit7|One external launch marker/PID; exact UTF8 fragments,NUL,0xff and descendant tail; every positive read amount equals its body length; concatenation equals fixture original despite arbitrary read boundaries; distinct linked request/returns|
|EOF before exit, then exit before descendant EOF|Separate raw EOF/native terminal wait facts in the actual opposite orders; no process exit invented at EOF and no truncation at leader exit|
|PTY private identity/resize/input|Actual private tty session/group, independent TIOCGWINSZ37x113, child reads exact hello/end bytes; raw bootstrap/source stages, native ioctl results and input request linked to queued delivery|
|Stopped PTY with65536 binary input and later CONT|Independent child stopped state; positive partial accepted prefix recorded once, negative EAGAIN separate, same delivery completed without replay; later CONT bypasses blocked input; exact child byte file equals literal payload|
|Duplicate/conflicting/refused control|Same immutable request duplicate returns current delivery status without new write; changed same ID conflicts; excess input/unknown job/non-PTY refuses with distinct request/refusal and no child-side effect|
|Background stale/matching epochs and reattach|Existing launch count/PID remains one; stale releasefalse; actual matching release true; new foreground epoch; no STOP/CONT sent merely for attachment; wait-return reason independent of process exit|
|Output crossing16MiB boundary|Fixture constructs exact binary16MiB+crossing chunk independently; whole already-acquired crossing bytes exist in raw records and bounded pending ownership before EFBIG; no subsequent read; actual incomplete terminal rather than success|
|Full worker data/control with stopped recorder|Fixture handshake proves next bytes remain unread before capacity; job stop/actual deadline wakes blocked admission; prior P bytes unchanged; held close/signal/reap/terminal emits without new recorder wait; process is actually gone or explicitly unresolved|
|Native setup/bootstrap/exec/read/resize failure|Actual missing executable/EMFILE/helper errno stage; targeted descriptor-native EIO injection; originals retained before refusal; no fabricated pid/status/success and cleanup only for acquired resources|
|Final reap failure|Targeted final waitpid ECHILD and one native non-EINTR fault; assert actual return/status-valid record, no reaped success, no signal after unavailable custody; other-error pending debt and later successful owned retry; counts/refit/archive reflect custody|
|Semantic append/settlement failure and retained retrieval|Read Ref still names exact raw originals; preserve return has actual failure; semantic retained cursor advances only on actual successful append; resident and archive retrieval requested range matches original bytes before presentation, unknown reference fails|
|Real outer PTY route/refusal|Original stdin literal includes split paste envelope and binary child bytes;2041 equals literal input; route sites name actual job/epoch and preserve refused bytes; partial escape prefix stays owned by old job; Ctrl-B release remains observable|

Use borrowed registered channels for deterministic P/credit cuts and one actual
RawRuntime source-to-preserved-disk child path. Native syscall wrappers belong
only in the direct fixture and discriminate the actual owned descriptor/PID;
they do not modify unrelated raw recorder reads/waits. Reuse existing actual
Mac/Linux stop/continue and byte conservation fixtures instead of introducing
new timing campaigns. Static attack checks every wait/claim against product/
spawn mutexes, sole-lane ownership, exact native returns, masks/PTY setup,
cleanup-before-reap, pending custody and original retention. One targeted
design/plan attack, direct reds, implementation/greens, then one code/oracle
attack and findings/recheck. No source writes before parent releases the integrated checkpoint. The
prepaid nested-region seam above is part of this owner unit, not a settled check.
