# Session UI ingress, queue and presentation raw emission

2026-10-13. Written first whole UI-owner subplan, before production edits. Root
reserves4000–4199. This is the remaining ownership path from a TerminalUI caller
or Composer action through retained UI queues/presentation to actual ChatOutput
transport. It covers census3–5,8–9,11 and its selected systemH mechanics. Root's
ONE plan attack was accepted with the corrections recorded below. Implementation
follows that review, direct red/green,
one code/oracle attack and finding-driven fixes. Workload/capacity/performance
qualification is explicitly deferred to operator discussion; direct owner tests
and required source checks remain necessary to deliver code.

## Grounding and actual boundaries

Read current terminal.hpp/cpp, chat_view.hpp/cpp, raw_command.hpp/cpp,
raw_runtime.hpp, the current completion plan,60-row census, authored/source and
product-storage subplans. Existing startup/reopen, keyboard128-byte acquisition,
foreground job routing, signal cancellation wake and Main/TUI worker Root
transfer already have their own emissions. Do not duplicate them or describe
all TerminalUI activity as newly unobserved.

The historical study's acquired Reno kern_ktrace.c ktrsyscall/ktrsysret copies
arguments and actual returns separately; ktrgenio's omission on error is a
counterexample. kdump reads persisted originals before formatting. Read those
actual retained sources and kdump's read loop again for this unit. Linux
man-pages6.19 write2's RETURN VALUE/ERRORS specifies short positive writes,
EINTR before transfer and EAGAIN/EWOULDBLOCK; the existing native output loop
already distinguishes those branches. The existing raw-emission study records
Saltzer/Gintell interference, Lampson's narrow interfaces/background work and
Lamport's publication-before-consumption principle. They underpin copying raw
fields at the existing boundary, not adding a metric SDK or another queue.

Quarantined Codex tui.rs separates raw-mode restoration, terminal handoff and
input flushing; it reports actual failed tcflush instead of assuming success.
Its timer/frame-scheduler machinery is not adopted. The earlier Blackbird
terminal-reference study supplies typed presentation and queue-authority
observations; Vibe's idle-only settings policy remains a counterexample to our
operator's activation semantics. No reference dependency, transformed metric,
new timer, callback registry, aggregate, parser or evaluator is introduced.

Current gaps are concrete:

* post(), operation_completed() and sessions() mutate/coalesce messages under
  mutex_ and ignore notification write results. There is no raw queue identity.
* Composer actions and actual queue enqueue/erase/clear/recovery transfers lack
  their own outcomes. A failed save/launch currently retains or restores prompts;
  instrumentation must preserve those actual distinctions.
* load/save_terminal_state retain existing native state and hex-v1 BBM bytes,
  but no UI caller request/outcome. They call tools read_file/write_file, not the
  opted-in journal storage API. Instrumentation must not encode a second packet.
* ChatView discards line bytes, trims complete blocks or leading byte ranges,
  and clears display history. This is presentation retirement, not audit expiry.
* ChatPainter prepares actual ANSI/native packets; ChatOutput tracks one borrowed
  packet and offset and performs up to8 actual8192-byte writes per flush. Current
  regions describe rendered/flushed/queued, not requested bytes and native returns.

## Scope splits for the remaining session/operator owners

This unit is not a grab bag of all remaining session work. The following remain
separate coherent units and need their own concrete plans:

|Unit|Census/current owners and boundary|
|---|---|
|Main input/command/settings/session lifecycle|2,6–7,12; main.cpp source selection, actual command classification/dispatch, SessionStore configuration, accepted/effective settings and attachment/detachment. Startup/reopen physical/semantic coverage is already delivered; remaining semantic caller distinctions are the work. Root reserves4200–4399 and owns main.cpp.|
|Restart/recovery/successor custody|23,58,60; CodingEngine validation/request, SessionStore retained restart note/handoff, explicit acknowledge-local-unknowns recovery and successor seed/switch. Preserve actual custody checks and old/new owner incarnations; no inference from a launch status. Root reserves4400–4499.|
|Complaint|55; local complaint.detail retention, separate advisory delivery and Beads outcome. Local retention and remote delivery can succeed/fail independently. Root reserves4500–4599.|
|Station|57; intake/validation/dedup receipt, control/admission/dispatch, finish and pause. Exact native event/feed bytes and retained receipts; do not infer station success from turn settlement. Root reserves4600–4699.|
|Backstop|59; actual claim/lineage, predicate/acquisition, assessment, pivot/action and pause. Existing authored capability does not instrument this owner. Root reserves4700–4799.|
|Terminal/editor native custody|RemainingH: TerminalMode tcgetattr/tcsetattr and control handoff; SIGWINCH setup/restoration/native handler publication; notification-pipe create/normalize/close; edit_terminal_draft temporary-file/spawn/wait/read/unlink and suspend/resume/tcflush. One lifetime owner must hold prepaid cleanup before effects. Native signal publication uses the already reviewed POD seam, never ordinary TLS/C++ emission in the signal handler. This unit's ordinary notification write records do not claim signal-delivery recording.|
|Native structured UI values|Session listing and task publication/activity receive Value. Their originals/bridge/representation owner supplies linkage or typed native leaves. This unit captures their native selection/kind/outcome and queue lifecycle; it never invokes encode_packet/dump_json/tree traversal solely for a metric body. Root reserves4800–4999 for representations.|

## Identities and layouts

Use the actual request RawEventRef(lane16,ordinalu64), shortened below to R,
for new owner identities; no new UUID generator, timestamp parent inference,
identity dictionary or global clock. Thread dispatch already links Main/UI and
worker producers. A message carries its original successful enqueueR and current
last-coalescedR. A pending/recovered prompt carries its successful enqueueR;
reopen uses the observed loadR plus its existing list index, not a fabricated
historical run identity. ChatView blocks carry successful appendR; merged
append records link their actual target block. Frame identity is constructionR,
not a new frame counter. The raw run descriptions register before activation.

Notation: E=raw_envelope_size(16), R=16+8; N=sizeof(size_t), D=sizeof(int),
Z=sizeof(ssize_t), K=sizeof(actual enum type). Text=N+unchanged bytes. Error
fields are their exact ErrorCode/int representation, not a formatted message.
Body lists below are ordered fields, not dumped C++ structs with padding.
Every Error/std::exception/unknown exception has a distinct declared outcome;
std::exception is recorded as its actual branch without an invented native
Error or an unbounded what() scan. Unknown exceptions have no invented native
code/message. Existing caller-owned error text, if posted to UI, is captured by
its ordinary message ingress with its exact known string_view length.

|Sites|Exact existing/native body, distinct request and actual outcome|
|---|---|
|4000–4002 message ingress|4000 kind:Kind,outcome:Ink,text Text;4001 ingressR,target enqueueR,branch:u8(insert/coalesce),actual resulting text size:N;4002 ingressR,branch:u8(Error/std/unknown),actual native Error when available. Text is copied before queue allocation or coalescing can fail. A coalesce request and its response link the original anchor.|
|4003–4004 notification write|4003 ingressR,fd:D,requested:N,byte:char;4004 requestR,result:Z,error-valid:bool,errno:D iff result<0. Ordinary write's actual ignored error is still recorded; notification acceptance is separate from queue insertion. No claimed signal observation.|
|4005–4009 drain/apply|4005 UI-owner drain request;4008 drainR,actual swapped count:N,first enqueueR,last enqueueR (zero for empty);4006 drainR,enqueueR,lastR,kind:Kind,Ink,text size:N;4007 actual applyR,generic outcome;4009 drainR,generic outcome with actual successfully applied count:N. The existing swap under mutex is one native batch dequeue, not a reconstructed individual-dequeue stream. Each later application admits before presentation; its request links both queue anchors. Actual append4030 links its incoming source separately.|
|4010–4012 composer action|4010 actual feed4080R,byte index:N,InputAction enum,native cursor:N,draft size:N,result Text only for submit/choose-command (including honest empty result);4011 actionR,branch:u8(local/queued/started/refused/cancel/quit/draft-load),actual targetR;4012 feedR,generic outcome.4080 has bool keyboard-present,[actual2041R],index:N,char. No per-keystroke draft copy.|
|4013–4016 prompt transfer|4013 actionR,queue-kind:u8(pending/recovered),original sourceR,offset:N,length:N;4014 requestR,enqueued1:u8,actual index:N;4015 enqueueR,branch:u8(dequeued/withdrawn/moved-to-recovered/retired),actual index:N,targetR;4016 requestR,generic outcome.4018 launch request sourceR,offset:N,length:N;4070 launchR,actual accepted bool. Emit withdrawals before erase/clear; never call refused dispatch a dequeue.|
|4020–4023 UI state caller|4020 operation:u8(load/save),path Text;4021 requestR,length:N,exact already produced/read packet bytes;4022 requestR,branch:u8(absent/loaded/saved),actual restored cursor:N;4023 requestR,generic outcome. No additional state serializer or aggregate. Native partial reads and write/sync/rename results come from selected file machinery under callerR.4052 links actual load-field decoder return.|
|4030–4034 presentation|4030 actual ChatKind,merge:bool,sourceR,text Text;4031 requestR,target block identityR,result size:N;4032 block identityR or appendR for line-clipped source,u8 reason,offset:N,length:N,generation:u64;4033 requestR,generic outcome;4034 actual preclear count:N,bytes:N,generation:u64.4073 partial mutation snapshot appendR,count:N,bytes:N,newlines:N,generation:u64. Originals are copied by4030 before clipping.|
|4040–4043 frame lifecycle|4040 width:N,height:N,cursor row:N,column:N,owner bool;4041 constructionR,packet size:N,exact complete packet bytes;4042 constructionR,u8 constructed1/handoff2/complete3/retire4,actual native offset:N;4043 constructionR,generic outcome. Empty packet has its literal size0. Existing rendering/graphics remain business operations; capture copies their actual returned bytes.|
|4044–4047 ChatOutput transport|4044 frameR,fd:D,offset:N,requested:N;4045 writeR,result:Z,error-valid:bool,[errno:D iff negative];4046 attach request fd:D;4060 attachR,fd:D,operation:D,argument:D;4061 requestR,result:D,error-valid:bool,[errno:D iff negative];4062 attachR,generic outcome;4047 held detach request attachR,fd:D,flags:D,offset:N followed by actual4061 native return. Commit follows full transport.|
|4048–4049 terminal dimensions|4048 actual ioctl request fd:D,method:unsigned-long;4049 requestR,result:D,valid:bool,[ushort rows/columns on success],[errno:D on failure]. Capture native dimensions before fallback24/80. Failed ioctl has no successful zero sample.|

Outcome branches are consistent everywhere: returned, native Error, allocation
(std::bad_alloc), length (std::length_error), other standard exception and
nonstandard exception. Only native Error includes ErrorCode/u8 and detail/i64;
other branches carry their actual branch and reached stage, with no invented
Error. Optional Error fields occur only on that branch, not a zero placeholder.

No site scans an exception pointer or formats an exception solely for metrics.
Native Error fields and actual std/unknown branches exhaust the outcomes here;
UI messages already constructed by business error handling retain their own
exact supplied bytes through4000.

### Structured input custody without a metric serializer

A RawUiValueOrigin is explicit call metadata, not Value data or a producer-side
lookup: origin-kind:u8,source-present:bool,[actual sourceR],fact-present:bool,
[actual ApplicationRecordId16],revision-present:bool,[native revision:u64].
4050 is its ingress request;4051 is the native returned/failed queued-or-applied
outcome. No absent source is represented by a fabricated successful locator.

For SessionStore::list_sessions, the main/session owner must capture each actual
native row at the existing producer loop before entries.push_back: stable path,
settings name/model/effort/workflow and actual snapshot/damaged/missing native
branch. It links the exact captured metadata-file packet/read when available,
plus its actual stat result. This producer loop already owns those native
strings; moving capture there adds no generic Value traversal in TerminalUI.
The actual listing completionR is explicitly passed through ui.sessions(listing,
origin) and retained on the queued Message before any allocating Value copy.
Failure copying the listing has4051 failed, while its already captured input
rows and native branches remain linked by4050. Drain can fail or abandon without
losing those originals. A direct opted-in caller without an original-bearing
origin is refused before copying/enqueue, with source-present=false and its
actual refusal; it is not accepted then retrospectively declared unaudited.

TaskStore::changed already runs only after1704's actual ApplicationRecordId and
native accepted delta have been preserved. Extend the callback's explicit origin
metadata with that fact identity and actual callback1709R; do not parse it out of
prose/Value. Initial/reopen task snapshot links the actual native snapshot1708R,
native revision and selected root's retained task facts; this is explicitly a
snapshot/source-state reference, not a byte-equality original-packet locator.
Native TaskState restore/replay sites continue to emit their actual outcomes.
UI task restoration fails before applying if it lacks that source-state origin.

Task activity capture is at its existing execution/participant owner before the
callback: exact already owned task_id,attempt,phase strings and actual activity
branch, without an activity Value encoder. That owner passes its activityR to
ui.task_activity. Its existing string-field accesses in TerminalUI move to the
safe immutable-input boundary before mutex_; any type failure records4051 and
no UI acceptance. The fixed4050/4051 grant precedes those accesses. Actual
revision/current task selection and absent-item branch are recorded from existing
native state under the mutex with held credit; no admission/traversal inside it.

This explicit origin propagation changes callback signatures/metadata at their
real owners and must be coordinated with root/program work. It is part of
closing this UI unit, not a deferral claiming accepted arbitrary Value inputs
are captured. File-native, listing and task-activity owner hooks implement their
own mechanical/native facts; this UI unit owns the boundary linkage and actual
queue/presentation consequences. Existing raw-off callers keep current behavior.

## Admission, lexical credit and mutation order

A RawUiTrace contains its actual requestR and an explicitly prepaid terminal.
RawUi methods preserve errno, and no destructor waits, registers a producer,
allocates, binds a foreign lane or takes a business mutex. A borrowed lexical
RawUiBurst borrows only explicit CONTROL from the existing keyboard/session bank;
DATA remains in each actual source owner's explicit argument, not a TLS loan.
its token remains with its sole thread and is not moved to a worker.

Message ingress must acquire its DATA body and all insert/failure/notification
CONTROL before mutex_. Copy original caller text immediately to raw storage,
then acquire mutex_, mutate/coalesce, copy its actual branch, attempt native
notification, emit its actual return and release mutex_. A vector/append bad_alloc
retains the original source and writes the held failure. No new admission inside
the lock. The queued batch's dequeue and end outcomes are a UI-owner grant
acquired before the swap; grants do not migrate from producer to UI thread.
The swap request/end credit is independent of unbounded message count;4008
copies the actual count/first/last anchor at the swap. Per-message application
admits before the actual presentation mutation with the original incoming
Message still owned. A batch end records actual applied count/abandonment if
that admission fails. Original ingress bytes were already copied before any
queue acceptance, so batch abandonment cannot discard an uncaptured source.
There is no unbounded per-fragment ref vector: each coalesce record links its
anchor and the actual latest record; the drain carries those native anchors.

For an individual record, credit is E plus its declared field widths and literal
byte counts. Message reserve, for example, is the exact4001 or4002 worst return
plus4003+4004 if notification_ can be valid. Because notification_ is protected
by the mutex, reserve that possible write before locking rather than assuming
its current value. Bounds are checked for overflow before effect; actual enum
and Error widths come from sizeof the shipped native fields on both platforms.
These equations are finite admission correctness, not measured capacity sizing.

The current keyboard acquisition already holds1155081 DATA and21950 CONTROL
plus its wake grant. Hold the fixed session CONTROL before the128-byte native
read, including128 possible feed/action/handler/enqueue records and withdrawal
records for at most128 pending prompts plus128 recovered drafts. Preserve the
whole native read2041 first. Each semantic feed then reserves its complete
source-derived DATA bound before mutation and releases unused capacity before
the next feed. Repeated history recall/submit can reproduce a full retained
draft many times from one read; no aggregate1MiB-per-read assumption is valid.
Queue text already acquired from a previous input/loaded state is referenced by
its actual preserved ingress/load record; cancel/clear does not recopy it128
times. Compose with existing command routes rather than claiming a second
standalone keyboard producer. Precisely calculate each site/variant's envelope
and worst simultaneous obligation before implementation; do not leave a magic
"large enough" bank. A failed fixed session claim precedes keyboard read;
a failed semantic claim precedes feed mutation and records the actual
unprocessed extent through the held batch outcome.

On queue insertion, source action and text are raw before validation/allocation;
only actual successful insertion adopts enqueueR. On dispatch refusal, don't
erase the pending front or move the composer source. On withdrawal, hold all
existing list entries' terminal records before clear/move; emit each actual
identity/index before invalidating it. Recovery/load source identity must exist
before restoring history/queued values. History retirement is native list index
and load/submitR, not a fake source locator for unaudited original history.

Initial input passed to TerminalUI::run carries the actual main input captureR
and known byte range; the root4200 owner must capture it before passing/moving
the string. Refuse an opted-in initial queue entry without that explicit origin,
never invent a historical session input. Ordinary keyboard submit4010 owns exact
submitted text; queue4013 references its already captured byte range instead of
duplicating it. Recovered prompts bind to the actual decoded load-field records:
4052 loadR,field-kind:u8(draft/history/queued),actual index:N,length:N,exact native
decoder-return bytes. Hold DATA E+R+kind1+2N+decoded limit before ui_hex decode;
copy the already produced result immediately before string/vector transfer.
Decoder failure has its actual load error and no successful string. Hold
7*ui_limit+48 before encoded acquisition. After the existing packet decode,
hold source.size()/2 plus57 times the actual native array sizes plus draft,
before any existing ui_hex decoder. This also covers malformed packets whose
arrays exceed the later validation quota. No instrument performs hex
conversion or a second packet decode. Composer history/queued restore explicitly
carries those fieldR metadata; fresh submit's4010R becomes its history origin.

UI-save obtains its request/terminal before state preparation/validation. Preserve
the already encoded packet before calling native write; save validation/encoding
failures emit failure without a fake packet. UI-load's selected file native
owner preserves every positive read prefix before decode and retains read errors.
Require explicit file-native ownership forwarded from this caller; file tooling
semantics and audit-recorder storage remain separate/opt-out.

ChatView append reserves original supplied text and bounded return/retirement
records before changing its string/deque. Because at most1025 blocks can be present immediately after insertion
(the previous bound is1024), reserve1025 block retirements plus one stored-prefix
retirement and up to floor(text.size()/2048)+1 skipped-line ranges. The existing
append loop supplies each contiguous skipped source range; hold its range until
the next newline/append end, emit before forgetting it. Each range references
the already copied appendR, with actual source offset/length. This is original
range bookkeeping, not a metric counter or a second text scan. New instrumentation does not add line counts, parse
text again, walk the preserved history or manufacture a "seen by operator" state.
Existing business loops already calculate each concrete cut; emit there. clear
prepays its own bounded block-retirement terminals before deque destruction.

Frame construction pays its fixed request/failure/end before rendering allocations.
Instrument the explicitly selected TerminalUI-owned grid; do not infer selection
from ambient raw_enabled. This grid is built only by reset/line. Every visible
complex-tail slice has its own appended arena extent; overwriting leaves unused
arena bytes, never aliases a previous cell's slice. Therefore tails.size() bounds
the sum of tail bytes emitted once across the entire frame. The public ChatGrid
API admits caller-crafted aliasing; it is not authorized to use this native-owner
bound. Public arbitrary ChatPainter calls remain opt-out unless supplied a
separate validated owner/bound. Full/invalid/first frames are bounded by all
cells, never the diff touched count or previous cache state.

Let C=grid.cells.size(),T=grid.tails.size(),H=grid.height,Q=max length of the
actual native color literals (compile-time),P=digits10(size_t)+1. Before
ChatPainter::prepare, hold DATA for its complete packet bound
B=C*(4+Q)+T+H*(4+2*P)+M+2*P, plus E+R+N framing. Four bounds utf8's maximum
actual output per cell, Q every possible style change, the native CSI row
position includes two decimal integers. M is sizeof the actual concatenated
initial/final mode/cursor control literals minus1 (36); the cursor supplies
another two decimal integers. This replaces the earlier64 constant and bounds
any grid maintained by reset/line, not merely the UI's clamped dimensions.
Checked arithmetic rejects before prepare on overflow or unavailable
capacity. This uses existing source sizes, not a second rendering/tree traversal
or the speculative4096-tail-per-cell bound. On actual prepare return, copy its
existing complete packet from the held grant before any later allocation,
callback, image_frame construction or write. Emit4041 as one native packet
record; no fresh admission after acquisition. An empty packet has its honest
empty result and no transport request.

Graphics composes a distinct actual packet. Before image_frame.assign/resize,
prepay its complete bound I=base_packet.size()+erase_literal.size()+load_prefix
.size()+4096+load_suffix.size()+place_fixed_literal.size()+4*P+close_literal.size().
The graphics owner enforces the already selected maximum3072-byte asset path
before load; its existing base64 construction produces at most4096 bytes.
Preserve the exact newly constructed image_frame under its own constructionR,
not as a reference claiming equality to base painter bytes. Neither producer
hook performs base64, cell styling or ANSI manipulation; those remain existing
business construction. Allocation failure during graphics has base bytes already
raw and a held truthful image-construction failure, with no accepted image frame.

For small-frame construction, bound the existing draft source by4*(draft.size()+2)
+existing literal lengths+2*P+erase_literal.size() before terminal_lines/string
construction; control-byte escaping is at most4 bytes per original byte. Capture
the actual produced small_frame from that grant before adoption. Existing native
controls use their known literal size. No packet is acquired and then thrown
away merely because instrumentation attempted a new capacity claim afterward.

Each actual output write has its own pre-acquisition request/return grant. The
8192-byte limit is the existing native write request bound. Capture request
bytes through the already preserved frameR+offset/length, not regenerated ANSI
or a second renderer. Positive/partial/error returns are recorded before offset
change or exception. Failure during later admission leaves an actual partial
frame and its original packet; a held frame terminal records the true current
offset. In-flight frames cannot be replaced. The attach owner prepays flag
restoration; detach emits its actual ignored F_SETFL result even on unwind.

Do not let instrumentation itself make terminal restoration impossible. RawUi
holds a terminal cleanup grant before setting output flags. TerminalMode/signal/
editor full resource cleanup belongs to the separate whole unit above; its
future credit composes with this transport owner, rather than borrowing ordinary
post-failure raw admission. Until that unit lands, do not claim all of systemH.

### Exact finite CONTROL composition on the selected64-bit profiles

E16,R24,N8,D4,Z8,Error9,Kind/InputAction/ChatKind4,Ink1. Credit is owned
physical envelope+body, not sizeof a padded C++ record. A max generic outcome
is E+R+branch1+stage1+Error9=51. Optional Error is present only for the native
Error branch; allocation/length/standard/unknown variants need fewer bytes.

* Message return max73, notification request53/return53:179 per ingress after
  its DATA request. The actual optional notification result never consumes a
  fresh claim under mutex. Raw-off requires no claim.
* Drain request16, swap return E+R+N+2R=96, batch end E+R+N+branch1+stage1+Error9=59:
 171 before mutex/swap. Per-message apply request E+3R+Kind4+Ink1+N=101 and
 max apply outcome51, total152 before actual presentation. No list-length
  dependent grant is acquired after the swap merely to record that swap.
* Feed request maximum E+bool1+keyboardR+indexN+char1=50; action CONTROL
 maximum E+feedR+indexN+InputAction4+cursorN+sizeN=68; generic outcome51:
169 per feed. A submitted action4010 has
 E+R+indexN+InputAction4+cursorN+draft-sizeN+Text-lengthN=76 bytes before text.
 4013 references that exact captured action text; it does not copy the same
 prompt bytes again. Its fixed request E+2R+kind1+2N=81 plus maximum terminal51
 gives132. Handler request40 plus semantic return65 gives105. Transition
 E+2R+branch1+indexN=73. Total128*(169+105+132+73)+256*73=80000, composed
 with existing21950 and raw_wake_single_notify_credit. Sequential per-feed
 DATA is (max actual draft/history-quota/palette-source bound +1)+76 before
 each feed; unused capacity returns before the next. No per-keystroke draft copy.
* Presentation retire E+R+branch1+offsetN+lengthN+generation8=65. For input n,
 hold (1025+1+floor(n/2048)+1)*65+51+the actual append result(E+2R+N=72).
 clear prepays1024*65+its actual begin/end widths. These grants precede deque
 destruction; original source DATA E+ChatKind4+merge1+sourceR24+N+n=53+n.
* Frame request E+4N+bool1=49; native frame outcome E+R+branch1+offsetN=49;
 error max51. A construction pays49+3*49+51=247 before prepare. Accepted
 packet receives98 for handoff and terminal and per-flush maximum8
 write pairs. Write request E+R+D+2N=60, return E+R+Z+bool1+D=53:113 per actual
 native call. The frame DATA grant remains live through its one immediate copy;
 the packet's own fixed end grant remains live until commit/retire, independently
 of8-call flush grants. No fixed promise for infinitely repeated EAGAIN calls.
* Output attach holds request20, two fcntl pairs2*(52+49)=202, maximum terminal51
 and final restoration105:378. Attach catches actual F_GETFL/F_SETFL failures,
 returns unused setup grants and retains105 final restoration after success.
 The restoration request includes actual offset: E+R+fdD+flagsD+offsetN=56,
 and its native result maximum49.
 Restoration uses its creating issuer after worker join; no foreign TLS lookup.
* Dimensions ioctl request E+D+unsigned-long8=28; successful return
 E+R+D+bool1+2ushort=49, failed return E+R+D+bool1+D=49:77 before acquisition.

UI-save's DATA request E+operation1+Text(path),49 successful return and maximum51
terminal are separate
from file-native claims and business encoding's existing ValueLimits{}.bytes
pre-acquisition packet allowance. UI-load holds its selected max-file limit
7*ui_limit through the native reader, before acquisition; known packet bytes
use that caller-owned grant. Native helpers retain the real partial prefix,
not a returned-only string or successful-save illusion. Whole UI-state capture
has no new codec or native filesystem implementation here.

## Direct independent oracle and delivery sequence

Use real TerminalUI/ChatOutput/ChatView/Composer owners, native PTYs/pipes and the
read-only preserved-prefix reader. Expected native fields/body lists are written
in the fixture independently; never consume emitter helpers, mirror descriptions
or infer the expected result from raw events. A native forwarding seam records
actual write operands/return/errno before production mapping and forces selected
short/EINTR/EAGAIN/EBADF branches. Byte expected lists come from fixture-generated
binary inputs, actual encoded UI-state file bytes and the native forwarding
observation, never a second metric collector or a duplicate output renderer.

1. Genuine baseline red: real post/submit/frame transport execute successfully,
   but no4000 message/4010 action/4044 native-write sites exist in preserved disk.
   Keep the literal native input/output and queue outcomes in that red fixture.
2. Message success/coalesce/failure: two real worker fragments including NUL/high
   bytes, inserted and merged queue anchors, actual owner drain; force insert/
   append allocation failure after original capture. Compare exact original
   bytes, distinct request/return and successful/failed business outcomes.
3. Input/queue: PTY submits a known draft while busy, queues distinct prompts,
   cancels one actual queue, refuses a save/launch and retains its source, then
   explicit retry starts/dequeues exactly once. Include bracketed paste with
   control-looking bytes, recovered draft loading without dispatch and EOF/quit.
   Native keyboard events remain the already covered original acquisition.
4. Persistence: actual saved packet/file bytes match4021, reopen restores exact
   draft/queued bytes without invented historical identities. Corrupt input,
   positive-read-prefix then native error, validation refusal and write/sync/
   rename failures produce exact caller and native outcomes; no fake saved state.
5. Presentation: real line clipping, whole-block retirement, prefix trim and
   clear preserve original append bytes and exact known deleted block/ranges.
   A native allocation failure records the actual append/construct error and
   does not fabricate a completed display mutation.
6. Transport: genuine pipe pressure/PTY generates short writes/EAGAIN; precise
   forwarded EINTR/EBADF validates native branches. Compare captured full packets
   and actual requested slices to independently captured native operands, and
   bytes read on the real descriptor. Assert offsets advance only by positive
   actual counts, painter.commit only after completion and detach flag result.
7. Stop the real recorder and exhaust ordinary capacity before acquisition;
   cancel admission through the actual UI wake. No new input/queue/write effect
   occurs after refused admission; already held outcome/restoration records
   remain possible. This is an owner fault case, not workload qualification.

Then ONE whole-unit code/oracle attack, fix valid findings and rerun affected
oracles, owned static/source checks and relevant Mac/Linux profiles. Six total
build/analysis workers. No full-suite reruns for plan text; no performance
measurements, sizing campaign, eval execution or laptop mutants. Root merges
this unit with other authorized units and keeps nls.2 open at the agreed stop.

## Same plan review implementation refinements

Root accepted the concrete corrected plan under its ONE attack. Actual frame
CONTROL is247: request49 + constructed/handoff/retire3*49 + error51. After exact
packet capture, handoff moves98 onto ChatOutput on the same producer; no lexical
pointer survives the frame stack. Output flag lifetime is378: attach request20,
actual fcntl pairs2*101, max actual attach terminal51, final detach request56 and
fcntl return49. Native get/set requests/returns4060/4061 are distinct records;
4046 selects the attach action,4062 its actual outcome. Pending-frame retirement
always consumes its already held49 on detach/unwind.

Current TerminalUI::tasks calls instrumented TaskState restore/replay under its
mutex, and task-pane refresh calls instrumented read under that mutex. Do not
permit those preexisting hooks to wait under the lock. Extend explicit
RawTaskOrigin with caller-owned DATA and borrowed CONTROL pointers; the actual
trace child carries those caller loans rather than borrowing its insufficient
own terminal. UI acquires restore221 or replay322+prepare230=552 CONTROL and
ValueLimits.bytes+48 DATA (replay only) before mutex. Replay's own322 includes
its parented commit221; do not pay that twice. Initial raw-disabled origins
need no claims. UI task-pane read holds222 CONTROL before mutex. Task prepare
splits the supplied DATA before its existing business encoder; no fresh raw
claim is allowed on these selected locked call paths. Loans remain stack-owned
by the actual producer, and no new TLS interceptor, subscription or filter is
introduced. Root owns TaskStore snapshot/completion getters and main/Coding
metadata wiring; this UI unit owns these narrow existing native-task seams.

Root accepted the source-derived sequential Composer admission correction under
the same review:2041 already preserves the entire native read. Before each feed,
reserve one complete result body using the current draft/history business quota/
palette source bound plus one byte, then capture submit inside feed before clear
or history push. Unused per-feed DATA returns before the next feed. Repeated
Ctrl-P+Enter cannot consume a fictional1MiB-per128-byte bound. The original read
and fixed CONTROL stay owned until processing/refusal/cancellation. Exact fixed
keyboard CONTROL is128*(169 feed +105 handler +132 enqueue +73 possible new
withdrawal)+256*73 existing withdrawals=80000 on64-bit native profiles. The
implementation derives all terms from sizeof fields; no rounded bank. Separate
UI interpretation batch request4085(fd/int,requested/size_t), source binding4086
(native2041R,actualssize_t return), and actual outcome4084 include native processed
cursor/totalextent and Error/class branch. Its167 held CONTROL precedes read;
a failed per-feed admission records an actual refusal and unprocessed extent
with the whole original read already preserved, never a made-up cooked result.

Participant taskactivity source crosses the existing business callback explicitly:
optional fifth PublishNative(Value const&,RawEventRef), actual2130R, one callback
(native if supplied else existing fourth Publish). Coding exposes typed
task_activity_native(Value const&,RawTaskPresentation), and Main forwards its
source into UI metadata. There is no metrics subscriber or racy shared last-event
getter on CodingEngine. Root owns TaskStore/Main metadata; this lane owns the
confined participant constructor/notify and Coding public callback/lambda seam.

The actual PTY cancellation oracle exposed publication admission inheriting the
worker's sticky cancellation descriptor. Cancellation stops new business
acquisition; it must not drop an already acquired UI publication. Root approved
explicit RawWaitControl{} for text/operation/structured UI publications and
their borrowed task banks, before mutex. RawUiTrace/helper defaults retain the
caller's control; keyboard/feed/render waits are unchanged. Recorder failure
and capacity refusal remain errors. There is no TLS cancellation clearing or
descriptor consumption. Whole worker binding now covers its final complete
message.4076 copies actual sourceR/dispatchR;4077 records actual native outcome
and admission/perform/completion stage through115 prepaid CONTROL. Arbitrary
std::exception what() text is replaced by the existing fixed failure notice,
avoiding unbounded construction before native source admission. Perform and
completion publication failures are distinguished from worker admission.

Paid RawUiPacket is a lexical owner: an unused handoff or a rejected replacement
retires at native offset0 from its existing token. ChatOutput retains ownership
through actual positive writes and retires at its actual offset. Native graphics
load now obtains image_path once and checks the existing3072 source limit before
business base64 construction; availability's earlier getenv is not treated as
a bound on a later acquired value. The original base painter packet and the
actual graphics packet remain distinct preserved sources. Block identity means
the native presentation block, not a byte locator into its first source;
skipped input ranges link appendR and retained-block trims link block identity.

Direct fault classes include native allocation refusal before queue insertion,
history allocation failure retaining submitted draft, partial presentation
mutation snapshot, scripted EINTR/real positive native prefix/EAGAIN, genuine
kernel pipe pressure, actual EBADF, rejected and unused paid packets, zero/absent
structured source, and held detach/packet retirement under actual exhausted
ordinary CONTROL. The state encoder/read packet is compared to independently
opened physical file bytes. A real PTY exercises keyboard read/feed, prompt
queue/cancellation, worker publication, application, frame and native write.

## Same code review: U1–U6 finite corrections

The sole code/oracle report is `2026-10-13-session-ui-owner-raw-code-attack.md`.
All six findings are valid. These corrections stay within that review; there is
no second panel and no workload qualification.

U1: RawUiSource offsets address the decoded original text payload. The original
site determines the payload; offset0 is the first text byte. Root owns the Main
4202 correction and its actual initial/resume range oracle.

U2: the existing command catalog construction maintains the largest actual
chosen text length while it appends its native commands. capture_bound obtains
that immutable catalog and its maintained bound before DATA admission; the same
snapshot drives the selected feed. A concurrent workflow publication cannot
replace the admitted snapshot. Explicit choices retain their actual maintained
bound. No catalog walk, formatter or classifier is added at emission.

U3: a selected synchronous UI run owns RawCaptureFailureScope through its actual
return/error. Load, save, draft and queue fallback catches require that scope
before adopting ordinary damaged/full/save behavior. Borrowed UI and Task split
rejection marks the final raw capture failure, exactly as fresh admission does;
capacity probes remain unmarked. The worker owns its independent synchronous
scope. No wait moves under the UI mutex.

U4: root approved the native completion/failure handoff on 2026-10-13. The worker
preholds request64 + possible perform failure51 + final outcome51 + existing
native wake106 =272 bytes on its own issuer before perform. 4078 records the
actual caught perform failure; 4077 remains live until complete publication is
accepted. Late refusal writes the held failed4077, puts exception_ptr into one
native failure slot under the existing UI mutex, writes the existing wake using
held credit and returns. There is no fresh diagnostic or complete retry. The UI
checks the failure slot at its existing loop boundary, cancels, joins existing
worker/retained-transfer custody, and rethrows. Early admission failure without
a grant uses the existing busy poll boundary; it acquires no business input or
new effect. Existing product polling cadence is unchanged.

U5: root approved retaining the actual creating RawThreadHandle and creating
thread identity. ChatOutput attachment, frame, and handed-off packet retain that
issuer. Selected owner methods check thread identity before business effects.
Cleanup on the creating thread temporarily binds the original issuer, even after
the original binding scope ends or under another inactive nested binding, then
restores the prior binding. No producer registration, admission wait or destructor
mutex is added. The creating thread must destroy the holder; foreign destruction
is a contract violation. Retained handles keep owned Runtime state alive; a
standalone externally owned RawProducer must still outlive its handle. A selected
emission with ordinal0 is failure rather than an accepted record.

U6: frame handoff requires actual successful capture for selected frames. Missing
capture emits the paid actual refusal and throws before native output. Captured
frames not handed off retire on their original issuer. Unselected owners remain
opt out.

Finite direct additions: literal empty-composer /help and maximum workflow alias
choose; actual cold-scope and foreign-binding attach/unused-packet cleanup with
original lanes and restored prior binding; foreign-thread selected operation
refusal before native write; missing-capture refusal before output; final borrowed
claim failure origin versus ordinary business error; genuine worker completion
publication refusal after exactly one native effect, failed4077 and no complete.

U4's native failure handoff also carries optional actual synchronous raw failure
origin from worker_capture.error(), under the same existing UI mutex. On observing
it, UI marks that origin before cancellation/join/rethrow. An exception_ptr alone
would transfer the Error value while losing the worker TLS origin; identical
business capacity errors must remain distinguishable in enclosing Main owners.
The actual PTY completion/load/save refusal oracle checks that enclosing owner
scope, as well as native failed outcome and absence of completion/dispatch.
U5/U6's cold selected output check uses retained owner.selected(), so lack of an
ambient binding cannot turn an uncaptured packet into accepted unselected output.


## Shared emission consolidation, 2026-10-13

`raw_ui_emit`, `raw_ui_link` and record sizing now delegate to the owned shared
`raw_publish`, `raw_publish_linked` and `raw_record_bytes`. `RawUiTrace` adopts its
already-published native request and preheld CONTROL into `RawEmission`; the shared
owner retains/releases the bank and publishes linked terminal fields. UI admission
still chooses its explicit borrowed bank and cancellation policy before acquisition.
DATA request admission remains separate. No sites, native layouts or grants change.

The successful body of `RawUiTrace::run` deliberately leaves the request open.
Frames use that body to acquire their packet grant before later rendering/capture.
`raw_call_run<false>` shares exception classification while preserving this actual
lifetime; intermediate worker caught events use the same compile-time dispatcher
and leave the surrounding owner open. UI maps native exception classes to its
already-declared branch numbers and preserves its progress/extent fields.
`RawEmission::release()` emits nothing: it is used only where this owner has
already emitted its real outcome or transferred the paid frame retirement bank.
Creating-issuer cold cleanup, borrowed loans and thread affinity remain UI-specific.
There is no new runtime flag, publisher, generic tracing owner or subscription.

Strict syntax for the changed UI owner and Station fixture is green in
`context/station-owner/adopt-syntax.log`. Matching direct rechecks follow the Root
shared-core source boundary; consolidation is included in the ONE shared-core
review, not a second UI panel.

### Required source-gate terminal cancellation/publication correction

The current mandatory source gate is genuinely red in terminal_pty,
command_jobs_pty and tasks_pty. Exact preserved actual terminal records locate
expected Ctrl-C refusal in Main dispatch4204 and worker4078/4077 stage2 as owned
Error(interrupted,ECANCELED), before joined transfer cleanup. Failure-origin scopes
must distinguish this actual cancellation from recorder/capacity failure without
clearing unrelated errors or accepting a missing original. The active worker
scope consumes only its matching Error(interrupted,ECANCELED) when its existing
cancellation flag is true, after perform and Main scopes unwind.4078 and the
command outcome retain the actual error. No Main catch/helper API changes belong
to this correction; all other recording errors propagate. The UI owner now owns
the whole actual-join/rearm/Root-transfer invariant as one conceptual unit.

The task-pane failure is the actual exec task publication seam: CommandTaskActivity
calls only legacy task_activity, whereas Main now selects task_activity_native.
The confined owner will accept both existing callback references, preserve its
existing1915 binding and actual1916 activity source, and invoke native if present,
otherwise legacy. It constructs the existing business Value once and forwards
RawTaskPresentation with actual1916 source (absent if capture off), no shared
last-event getter, no added metrics subscriber or fabricated reference.1920 records
actual selected subscriber presence;1917 records its actual callback result.
The existing three real PTY oracles will be rechecked without weaker expectations
or changes to warning/status text. This is a finite same-unit regression correction
under the completed UI review, not another campaign or review panel.

The exact preserved terminal failure narrows the cancellation correction to the
worker that owns4078: after perform unwinds all completed Main scopes, the active
worker capture scope may consume its **matching** Error(interrupted,ECANCELED)
only when the existing cancellation flag is true.4078 and command.error retain
the actual failure; completion remains inside paid4077 lifetime. A fixed literal
interrupted notice tells the operator the real outcome. No other recording error
is consumed, no endpoint is drained/reset here, and no business acquisition is
retried. This uses the existing exact consume primitive and needs no Main helper
API or new tracing component. Joined Root bank restoration belongs to this same whole owner.


The exact canceled completion bank is3*(E16+sizeof Kind+sizeof Ink+sizeof size_t)
plus the larger of the two existing fixed notices for DATA, and3*179 CONTROL for
failure/text/complete. Each original enters the existing4000 before queue mutation;
RawUiTrace splits explicit borrowed DATA and existing RawUiBurst splits CONTROL.
These held banks are used only when actual cancellation is set; uncanceled
completion keeps its existing direct-refusal oracle/path. The worker holds them
before perform. No TLS cancellation change or new emission component is added.

After the actual native worker join, Main retires the old turn incarnation and
clears it with its held99+1468+96 CONTROL. Descriptor numbers remain stable through
actual dup2, so existing cancellation watches observe the rearmed native endpoint.
The interrupt owner holds one reset pair before installation/launch:99 on its
original caller issuer,1468+96 on its actual prepared signal-wake issuer. Its native
reset consumes these paired held credits; replacement pair admission occurs only
after actual rearm, using finite probes, before another launch can be accepted.
Handler code neither binds nor registers. New captured business input, frame or
Root restoration admission resumes only after actual retirement/rearm succeeds.

Transfer joined(optional actual rearm refusal) first marks the actual child and
its stop banks joined and retains raw_joined_owner. Refusal then preserves the
existing no_new/raw_owner_blocked debt and returns that real Error without any
fresh Root grant. Success alone calls existing restore_raw_owner after rearm.
Canceled flag stays true for the correct Turn cancelled display until next launch.
Genuine recorder/refused bank/foreign issuer/native rearm failures stay fatal or
truthfully block transfer; no endpoint or failure marker is globally cleared.


2026-10-14 — Final terminal integration correction is complete. The same owner
now handles typed exec task activity, matching interrupted/ECANCELED capture
consumption after perform unwinds, held failure/text/complete publication, actual
worker join, paid native turn/IRQ wake rearm, then Root transfer. Actual rearm
refusal retains joined child debt and blocks new Root work. The existing
uncanceled completion-refusal path remains strict. Matching whole app/UI/H/wake
build passes; all seven existing tasks_pty, terminal_pty, command_jobs_pty,
raw_command_wake, ui_raw, terminal_custody_raw and terminal_custody_native cases
pass in59.19s (context/station-owner/cancel-whole-direct.log). All four changed
translation units pass owned static analysis; formatting and diff checks pass.
No timers, new publisher or swallowed recorder/capacity failure. All delegated
owners are idle before Root stages and runs the installed full source hook.
