# Station native ingress, replay and projection owner

2026-10-13. Proposed whole row57 unit: StationStore and the confined Main
station branch. No production edits, builds, performance study or workload
qualification for this unit precede root's ONE plan attack. Sites4600–4699.
Root owns shared Main integration, descriptions and build registration; this
owner may edit only the coordinated station branch after plan acceptance.

## Actual owner and grounding

Read station.hpp/cpp, Main's station branch, packet.cpp, current selected file
helpers and RawSessionCall, docs/STATION.md, the earlier station delivery paper,
the raw emission study, and the acquired Reno ktrace/kdump sources. Reno writes
native syscall arguments and actual returns separately; its error-omitting I/O
hook is a counterexample. kdump reads physical originals before naming or
formatting. Saltzer/Gintell's interference discussion and Lampson's narrow
background interface apply here: copy fields or an already acquired business
packet to the existing raw transport; do not construct metric Values, revisit a
Value tree, derive counters, reformat an event, add subscribers or schedule work.
No new dependency or reference code is adopted.

StationStore is one synchronous session owner, not another scheduler. Its
constructor reads program-channel ApplicationRecordEvent payloads directly from
Root's retained tail; these are ImmutableBytes, not paths. Control/admission
records precede set/map mutation. Admission deduplicates source/id, not payload;
failed workflow remains unknown and pauses. Existing view() visits native map
rows to build a business Value. Main reads local external JSON, parses once,
publishes an existing BBM packet, then runs one admitted event per existing poll.
The existing250ms product cadence remains. We do not optimize repeated snapshots,
change dedup/steer policy, qualify4096 exhaustion, add a feed queue or introduce
cursor acknowledgement. The earlier delivery paper explicitly reserves those
as separate work; its former qualification claims are not inherited here.

## Explicit selection and native source contract

StationStore constructor and each operation gain an optional explicit
RawAdapterContext pointer; existing direct callers remain opt out. Main owns a
selected RawSessionCall whose parent is the established session owner and passes
its child context. Reuse this already owned immediate native-field helper rather
than create a Station metric registry. It owns RawCaptureFailureScope and closes
that synchronous failure scope at the actual method return/error.

A Value is never serialized solely for metrics. Main captures each actual file
read in the selected file owner before parsing and links the parse request to
that returned sourceR. Station method requests link to the actual parse request;
this preserves malformed input and duplicate/invalid typed input without a second
serializer. Direct selected callers may link their actual native input owner;
otherwise their fixed fields are recorded with truthful source absence. Fixed
fields are captured only as the business code borrows them. Missing/wrong-type
fields emit native presence and variant-index status, then preserve the original
validation outcome; no default string is presented as acquired input. Ordinary
unselected calls retain existing behavior.

Native record notation: E=raw_envelope_size16; R=lane16+ordinal8; S=sizeof(size_t);
B=sizeof(bool); C=sizeof(ErrorCode); D=sizeof(int64_t). Concatenate independent
fields, host order, no padding. All arithmetic uses checked owned bound helpers.
No truncated what() strings, scans, inferred errno, clock or transformed bytes.
Terminal branches use the existing RawOperationBranch variants: returned,
Error(code/detail), allocation, length, standard, nonstandard, unfinished.
System errors additionally preserve actual error_code.value() at their native
filesystem result boundary, not text from what().

## StationStore sites and grants

4600 request: parentR,u8 method(restore1/control2/admit3/finish4/pause5/prompt6/
view7),bool paused,bool stopped. Generic4601 end is its distinct actual terminal.
Control/admit/view gain an optional RawEventRef* actual_owner outparam containing
their actual4600 request. Clear at entry and assign only after successful paid
terminal checks. Prompt gains an optional RawEventRef* actual_source for actual
4607 bytes, with the same lifetime rule. Main uses these explicit native outputs;
there is no ambient last-call getter or mutable trace pointer. Boolean4606 remains
a separate actual return, never conflated with4600.
The base request costs E+R+1+2B=43 on this host. The maximum generic Error end is
E+R+3+C+D=52; its CONTROL bank is95. A successful boolean result uses distinct
4606(callR,bool actual), E+R+B=41, prepaid before control/admit effects. No actual
return field is supplied when the method throws.

4602 fixed acquired source: callR,u8 field(id1/action2/steer3/source4/event_id5/
cursor6/prompt7/reason8/outcome9),S length, unchanged bytes. Exact DATA=E+R+1+S+N
(49+N here), acquired at the existing native borrow before validation or mutation.
4609 fixed-field status:callR,u8 field,bool present,[S actual variant index only
when present]; the absent body has E+R+1+B=42. Maximum
E+R+1+B+S=50 CONTROL. This precedes a missing/wrong-type fallback; a true string
source also gets4602. No second generic field walk: the existing text()/field()
borrow is the site. Only native fixed fields this operation already consumes are
captured; all other typed input is linked to Main's preserved original.

4603 reached decision:callR,u8 decision,bool actual,S current table size,50 bytes.
Control preholds2*50 for duplicate/capacity; admit3*50 for paused-or-stopped,
duplicate,capacity; finish preholds one50 for missing/settled lookup. Evaluate
the existing decision once, emit its native result, then use it in the business
conditional. No invented accepted outcome precedes the semantic record.

4604 actual log.record return:callR,S32,unchanged existing32-byte native hex return,
DATA80 prepaid before log.record. This string is the existing business return;
metrics never encode/decode an identity. Root's semantic ApplicationRecordEvent
still owns the actual identity. If record fails, no4604 is fabricated. Capture
its existing return immediately before any subsequent set/map allocation.

4605 reached mutation/state:callR,u8 kind,bool paused,bool stopped,S controls,
S events. Exact CONTROL=E+R+1+2B+2S=59. Prehold one59 before the mutating method;
on success or exception emit the actual current state/counts. These are direct
native container sizes, not maintained metric counters. Set insertion followed
by a failed steering string assignment remains a partial native mutation with
its already successful semantic record separately visible.

Per control: base95 + result41 + decisions100 + state59, with typed status50 for
each actual fixed field (id/action and steer only if reached) and individual exact
DATA sources. Acquire log-return DATA80 before semantic record. Admit:95+41+150+
59 plus four reached fixed statuses/sources, existing steer source, and return80.
Pause:95+59 plus reason status/source and return80. Finish:95+50+59 plus actual
source/id/outcome fields, native requested returned bool in the request (B extra),
and return80. Prompt/view have no mutation bank.

Constructor replay has one actual invocation95, a held final state59, and bounded
per-fact grants **before** each existing decode/mutation.4610 callR,application16,
source journal16,u64 sequence,S payload,unchanged actual ImmutableBytes payload:
DATA=E+R+16+16+8+S+N=88+N. Root already owns the source persistently; admission
refusal cannot discard it. ImmutableBytes may be a cold checked reader, so eager
data()/begin() is forbidden. Inline the existing read_packet sequence at this
owner: enforce its existing ValueLimits bytes cap, prehold4610, call payload.read()
exactly once, capture that actual owned buffer immediately, then invoke existing
decode_packet on the same buffer. This is the existing business read, not an
extra metric read or pinned historical cache. Read failure is distinct from
decode failure, and no acquired buffer is discarded by post-read admission.
A separate4611 request(callR,application16) costs56,
actual decode/classification4612(R,u8 branch) costs41, maximum generic failure52:
per-decode CONTROL108. Successful4612 classification tags are ignored0/control1/pause2/admission3/
observation4. Actual string label uses4602 field10 before native comparisons;
missing/nonstring label uses4609 and remains ignored. Successful classification
names the reached native label;
unknown/nonstring labels stay ignored. Fixed fields consumed during restore use
4602/4609. One state59 precedes each actual restore mutation. The existing final
unknown scan emits its native unknown comparison at the same loop before pause,
50 per actual reached row, with a single held state59 for unresolved pause. No
second tail or event-map traversal is added for metrics; replay is not admission.

view() captures paused/stopped native fields and existing steer bytes at its
existing construction boundary. Prehold a method terminal and a native partial
row-count record4613(callR,S actual business vector size)48. Within the existing
business map loop, source/id/outcome each receives exact4602 DATA before the
existing Value row allocation. Emit4613 exactly once at final success/error, querying the existing business
array.size(); after successful move query the acquired result's known events
array, and on pre-transfer failure query the existing local vector. There is no
metric-only counter, generic tree walk or per-push record. No Value tree serializer is
introduced. Root Main later captures the actual BBM projection packet, including
an encoder failure after successful view; consumers need not infer a packet from
row fields.

prompt() validates/borrows its fixed fields before existing business formatting.
Prehold DATA(ValueLimits.bytes+48) for the actual format_value return4614 and
DATA(ValueLimits.bytes+prefix.size()+separator.size()+steer_.size()+48) for the
actual concatenated prompt4607 **before** invoking the formatter. Prefix and
separator bounds use sizeof(the existing literal)-1, not magic additions. Capture
the unchanged actual formatted return immediately, then perform the business
concatenation and capture its full actual result. Thus allocation during the
concatenation cannot erase the already acquired formatter result. The business
formatter limit remains16MiB; no extra format, estimate or metric transform runs.

## Confined Main station branch

4620/4621 whole selected station owner request/end. Adapter preparation uses the
selected owned raw_file_absolute, then preserves the existing lexical-normal
native result (prebound actual absolute native length+1 before normalization) in
4622. Its native filesystem is_directory has distinct4630 request(path sourceR)
and4631 actualbool/int error_code result. It uses error_code plus the same native
filesystem_error on failure; no fake errno. Existing session/actor identity
acquisition is captured as native bytes at the business return, not reformatted.

File polls get native existence request/result at their actual boundary. Selected
read_file(parent context,limit,&actual_read_source) preserves originals including
failed reads before JSON parse.4624 parse request:callR,originalR,u8 control/event,
S actual length.4625 actual parser result:requestR,S native variant index,[S actual
array size only for an array]. The max return is56; request73 plus max generic
failure52 and return56 use separately prepaid claims before parsing. Generic
4626 is the separate parser end. Limit rejection records actual array count,
then raises existing capacity; malformed/duplicate/invalid JSON remains in disk
through the original source. Main passes this parse request to StationStore.
Existing log.original remains a business accepted-input record; it is not the
only source record and its policy is not changed by instrumentation.

4632/4633 publish owner request/end:phase source bytes and selected view parent.
Before the existing Value construction/encode, hold DATA(ValueLimits.bytes+48)
for4634 actual BBM packet. This is the actual encoder's existing16MiB acceptance
bound, not a made-up blob grant: restored outcome/steer fields are not validated
by current replay to modern limits, so assuming current new-admission limits
would underfund actual supported restore data. There is no extra map traversal
or maintained metric size counter. Individual actual business metadata returns
(phase/pid/context_head/session/actor) use native fixed fields; existing actor
hex conversion happens once for the business packet and its returned bytes are
copied unchanged. Immediately capture existing encoded bytes before comparison,
then4635 native bool changed using prepaid41. On change, selected write_file
receives RawFileInput{4634,length}; actual return4636 is distinct from proposal.
Only after native write returns may previous_snapshot adopt the existing bytes.
Unchanged status remains an actual acquired projection, not omitted metrics.

4637/4638 invocation request/end begins only after the inputs are acquired and
links actual admitted4600, actual prompt4607 and actual File3410 read owner in
its request (E+R+3R=112; generic end52). Global station stages cover the preceding
acquisitions. Acquire prompt/workflow independently through their selected owners;
each original is preserved even if the other acquisition fails, and no invocation
request is fabricated before ready. Program/workflow owner captures its existing methods. Main preserves
its caught Error and native returned=false; Station.finish then records unknown.
Capture failure scope.require runs **before** treating raw failure as ordinary
workflow stop or attempting the outer business pause/publish fallback. Ordinary
adapter/workflow errors retain existing native pause/blocked behavior. A selected
raw failure stops this owner; no new acquisition or business fallback bypasses
it. Stage checkpoints distinguish admitted, busy published, prompt acquired,
workflow invoked, return acquired, finish applied, final projection written.

No destructor bank is needed: StationStore has no native descriptor/worker or
external ownership cleanup. Every entered operation holds its own terminal and
mutation outcome before effects. Main's established Session/Root/interrupt
owners still own their existing cleanup. Power loss remains the operator's
accepted act-of-god boundary, not a new qualification requirement.

## Direct red/green and one review

Before source edits, the baseline must run the real native store and external
Main station ingress successfully and fail its raw source/end oracle. No missing
symbol, stub or collector-only pretend red. Use existing native station test
fixtures as source guidance, not their4096 exhaustion/performance campaign.

Finite store test: real temporary AuditLog/Root, actual control steer/admit/finish,
duplicate changed payload, paused refusal, unknown finish and reopen unknown
pause. Independently decode persisted raw native fields and compare literal
source bytes, semantic retained fact IDs/state and actual results. Actual
operator-new cuts after semantic record/before set or map mutation and after
formatter return/before prompt concatenation establish partial effects, actual
error branch and no fabricated return. Actual selected DATA bound refusal
precedes formatter/file/status effect. Wrong type and oversized fixed strings
must keep the external original and truthful failed status, never clipped input.

Finite Main test: actual executable with private fake provider and native local
files/PTY-free process, no account, real atomic control/events files. Literal
malformed control and event JSON survive parse failure; duplicate/accepted input
links to distinct original/source/actual outcomes. One tiny actual workflow writes
one native private effect and returns; status raw packet equals independently
opened on-disk BBM bytes on changed publish; unchanged publish has no second
native file write but does preserve its acquired packet. Native adapter/stat or
write failure remains distinct, and raw admission failure skips ordinary
workflow/pause fallback/new dispatch. A closed run reveals actual source→parse→
Station admission→program return→finish→status write linkage.

One root whole-plan attack, implementation/direct greens, ONE independent whole
code/oracle attack, finite fixes/direct recheck. No additional panel, source gate,
commit, build profile or workload qualification in this delegated lane.

## Same plan attack ST1–ST3 dispositions

Read root's ONE `2026-10-13-station-owner-raw-plan-review.md`. All three findings
are valid; no second plan panel. The corrected interfaces above are authoritative.

ST1: 4605 kind literal map is control1/pause2/admit3/finish4/restoreControl5/
restorePause6/restoreAdmission7/restoreObservation8/reopenPause9. It describes
which mutation was reached; native counts/flags describe actual partial state.
The generic trace stage is set immediately before each business step: control
fields1/validation2/duplicate3/capacity4/record5/insert6/apply7; admit fields1/
paused2/duplicate3/capacity4/record5/emplace6; pause input1/validation2/record3/
assign4; finish input1/lookup2/outcome3/record4/assign5/pause6; replay read1/decode2/
label3/validate4/mutate5/unknownScan6; prompt validate1/format2/concatenate3/source4;
view header1/row2/return3. Failure after successful record and before insertion
is record4604 plus a native state snapshot and terminal stage6, not accepted.
Each state bank is consumed once at success/error, never retried by a catch.

Main metadata DATA4640 is callR,u8 role(phase1/adapter2/session3/context_head4/
actor5),S bytes,unchanged bytes. Phase is borrowed/captured before Value copy;
prepay actual native path.size() before the SAME adapter/session string()
conversion. Context head is the actual existing borrowed string. Actor conversion
gets32 bytes before the SAME business hex_identity call; capture its returned
string, no metric hex formatter. Native getpid has4642 request(callR,u8 method1)
41 and4643 actual(requestR,pid_t result)40+sizeof(pid_t), both prepaid before the
business call.4644 station identity actual return(callR,actor16)56 is prepaid
before the existing session_identity acquisition.4645 view source(callR,actual
view4600R)64 is prepaid before view acquisition and emitted only after its return.
All are distinct native sites, not a merged metadata Value.

ST2: explicit Station out-refs are cleared at entry and assigned after the paid
terminal/failure checks. Actual admission4600 is linked only when bool returned
true, and its separate4606 remains the actual bool. Prompt source4607 and File
actual3410 are independently owned and preserved before invocation.4637 contains
all three real refs. View4600 is linked by4645 before the SAME business encoder.
No ref uses a last TLS record, parse-owner substitution, or modified startup ID.

ST3: row count and mutation state emit once on final success/error through a
consumed held bank; native business vector.size() supplies the count. Reopen's
existing unknown scan captures its current borrowed key source/id via4602 and
emits4615(callR,sourceR,idR,bool actual unknown),89 CONTROL preheld before those
borrow/capture operations. Its decision therefore identifies the actual row
without an auxiliary map or metric row index. Keep the original comparison and
pause branch; no second traversal. The final reopen pause snapshot59 is likewise
consumed once. Cold replay preserves a single existing bounded payload.read()
then decode on that very returned buffer. Finish preholds max(sizeof("unknown"),
sizeof("workflow_returned"))-1 +49 for actual outcome before string construction
and captures it before Value/log allocation. No additional business validation
is imposed on old control/observation replay fields.

Exact4612 terminal clarification: classification is the native optional successful
field on the distinct generic decode end: R,u8 generic branch,u8 stage,bool Error,
[ErrorCode,i64 detail on Error],[u8 label classification only returned]. A success
costs44 and maximum Error52;4611 request56 + maximum52 =108. There is no additional
classification event or duplicate terminal. Final constructor state kind9 means
restoreFinal: actual flags/counts on completion/unwind, including unknown pause;
it is not a claim that an earlier partial failure performed the pause assignment.

Main filesystem result correction:4630/4639 is the distinct generic filesystem
owner request/end, separate from actual native result4631.4631 is requestR,bool
actual,int error_code,[native file_type only directory status]. Prehold45 plus
sizeof(file_type) for directory or45 for exists; generic request41/end52 bank93.
Directory uses the existing libc++ status/classification sequence explicitly so
file_type::not_found stays ordinary false even with native ENOENT; other error
status preserves the same filesystem_error code/path. Exists uses its native
error_code overload, which clears the error for a known not-found status just as
the throwing overload does. Actual error_code is copied unchanged; never errno.
The source was read in installed libc++ __filesystem/operations.h.

Implementation protocol clarification:4609 applies to named Value fields only;
plain native string_view pause reason and native map rows have no Value variant
index or absence. They emit their exact4602 source directly. Main's source4640
roles also include controlPath6/eventsPath7/statusPath8. The existing base/operand
path composition is funded by base.native().size()+operand.size()+1 before its
actual business append, then the returned native path is copied unchanged. This
avoids post-acquisition admission without caching/changing poll policy. Control/
events path string conversions used by log.original use the same bounded one
business conversion and preserve its returned bytes before Value allocation.
Parser's actual array-present bool adds one byte:4625 maximum57, smaller absent
array body49. These replace the earlier56 shorthand in the plan.


## Actual Main oracle correction

The first actual CLI run preserved ingress/prompt/status and made its single native
workflow effect, but literal link inspection found admission ref zero in4637.
`parse()` had queried `RawSessionCall::context()` after its successful run closed
the emitting bank. Its completed request still exists, but that query is deliberately
inactive, so control/admit were opt-out. Main now saves the actual parser context
immediately after constructing its request, before calling `run`, and passes that
immutable real request onward after successful return. The native oracle explicitly
requires control/admit request parents equal the two actual parser requests and
requires a nonzero acquired admission in4637. No shared API policy changes.

The first store fixture's reopened Root attempted a new audit record after only
`confirm_recovery`. Existing Station native fixture also calls
`recover_coding_session`; adding that same actual business recovery makes the
reopened journal live. The recorded `audit_unavailable` terminal was truthful,
not an ingest failure; production changes were not needed for that fixture error.

`tests/station_main_native_test.py` invokes the real CLI with a tool-only authored
workflow, then reads its closed raw run through the independent native RawRunReader
CLI stream. Its four actual cases check source→parse→control/admit→prompt→invoke,
literal complete prompt and workflow bytes, final raw BBM equals actual status
disk bytes, malformed original control/events surviving parse failure, and failed
native replacement retaining the raw proposal with no false successful write.
These are finite owner fault cases, not workload qualification.


## First complete native greens and whole-review handoff

Root's matching final UI/Station/Main build is green. The consolidated UI native
fixture remains green (`context/station-owner/consolidated-ui-native.log`). The
whole actual StationStore success/reopen/allocator/bound/refusal fixture is green
(`second-store-native.log`), and the real CLI four-case byte oracle is green
(`main-four-native.log`). UI shared adoption read-only tidy is clean. Station
read-only tidy is completing; no performance or workload qualification has run.
The whole Station source/plan/native fixture is handed to Root for its ONE code
attack; no second panel or test-of-tests is requested.


The owned Station tidy found a local `Value` argument copied only for const
use in `record`; changing it to `const Value&` removes that copy without changing
AuditLog semantics. Its matching finite Station link/native recheck and narrow
tidy are green/clean (`finite-store-build.log`, `finite-store-native.log`,
`finite-store-tidy.log`). CLI four cases including actual3441 rename−1/native
errno are green (`main-final-native.log`); that CLI is Root's previous matching
binary before this const-ref-only helper correction. Root's final matching
application gate will relink it. No further review or broad test pass was added.
