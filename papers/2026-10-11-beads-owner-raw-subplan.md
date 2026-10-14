# Beads51 whole owner: proposed raw emission subplan

2026-10-11 design; Oct13 implementation under the completed ONE root plan
review in [Beads plan review](2026-10-13-beads-owner-raw-plan-review.md). Native
old-source red precedes production edits; no second design-review layer. Sites3500–3599 proposed; the current source and Oct11 subplans have
no use of that range. This does not complete nls.2 or authorize evaluations,
dependency adoption, operator database writes or execution of quarantined code.

## Grounding and useful delivery

Read full `src/beads.cpp`, `include/blackbird/beads.hpp`, current Coding operation
dispatch/receipt/disposition and Beads caller, Child start/read/collect/cleanup,
RawChildTrace and RawAdapterContext/owned-original interfaces. Read
[orientation](../context/beads-owner-source-study.md), the
[pinned integration study](2026-10-07-beads-integration-study.md),
[design review](2026-10-07-beads-design-review.md), `tests/beads_test.cpp` and the
matching quarantined bd0.58.0 sources directly. The existing archive/restoration
manifest in that study and QUARANTINE.md pins
`ae14933db67a0f67da5a8fb69be72c2282ca0e73`; no new dependency is proposed.

Direct upstream grounding: version/where command bodies; show exact/prefix
resolution; update single/multiple-result output and claim handling; create's
CreateIssue before later dependent work; Dolt transaction SQL commit before
version commit; ClaimIssue's assignee-only conditional update; BEADS_DIR config
and database selection; main/version_tracking maintenance before nominal
read-only store opening. Read-only flags are argv facts, not a guarantee that
external bd performs no maintenance writes. Zero exit is not evidence of a valid
command-specific result or a fully completed composite upstream mutation.

Deliver one explicit synchronous source graph: actual Coding operation → Beads
run → actual argv preparation → each reached version, where, exact-show or action
invocation → existing Child native results and unchanged stdout/stderr → existing
observer/parse/validation/state/outcome. Preserve source bytes when semantic
audit, parse, limit checking or allocation fails. Keep existing business command
semantics, binding/cache behavior, five-second child deadline and conservative
uncertainty. Remove only Beads' writer `elapsed_us` calculation/result member.
Its only other occurrences are the separate decision owner and test-consumer
measurements; neither is changed by this unit.

## Explicit ownership and signatures

Public API becomes `Value run(const Value&, const RawAdapterContext* = nullptr)`.
Raw selection requires that explicit context, a nonzero actual parent Ref, and
an active producer. Default calls remain opt-out even under a raw thread binding.
No constructor effects, TLS nearest-owner lookup, provider-owner guess, parent
attempt overlay or generic metric subscription. The private invocation receives
an explicit prepared argv owner and invocation kind from its actual call site.

At `CodingEngine::call`'s existing Beads branch, inside the real operation body,
construct local `RawAdapterContext{nullptr, operation_trace_->reference(),
nullptr, nullptr}` when that actual trace is enabled, and pass its address to
`beads_.run`. The current `operation()` installs/restores `operation_trace_`
lexically at `coding.cpp:1956–1975`; use that exact owner. It already records
attempt/invocation/generation. Do not manufacture a second provider dispatch.
Nested calls from an observer cannot replace the immutable context borrowed by
this Beads run. Retain the same issuing thread handle through claims and child
cleanup; no source bytes/claims cross threads. Default opt-out has no raw grants.

Use an owned narrow Beads trace, not RawProviderPhase's raw-enabled constructor
as an implicit opt-in. Copy scalar native fields and borrowed existing byte views
directly to raw_emit. No JSON/BBM2 encoding for metrics, command joining,
description reconstruction, digest, normalized status/issue object, duration,
filter, compression, buffer clone or environment export. Operation17 already
preserves actual input and final result packets; do not encode them again here.

`child.raw_capture(actual_invocation_R, true, true)` selects both existing native
streams. Set this before `child.start(argv)`'s by-value copy; retain separate_error
true. Existing Child read_permit/1404 publishes positive bytes before observer,
limit check or append. Use that one native byte body; neither observer nor Beads
parser emits another stdout/stderr body. `child.raw_reference()` supplies the
actual child Ref, never a synthetic child on copy/setup failure. No original
ticket queue is needed: Coding's existing observer still writes semantic audit,
while native raw bytes independently survive its failure. Pass no private child
environment to the recorder. Existing BEADS_/BD_/DOLT_ removal and BEADS_DIR child
overlay remain business work, never process-global chdir/setenv.

Invoke trace exists before Child construction/environment/callback preparation.
Run its business body in an inner lexical scope so Child closes/signals/reaps
before the invocation terminal. Save only acquired scalar outcomes/Refs before
locals die. Do not borrow out/result/error strings after destruction. Finish once
on returned/Error/allocation/length/standard/nonstandard/unresolved branch;
destructors consume prepared terminal credit without waits or throwing.

## Canonical-path dependency: explicit file47–48

Configure currently calls canonical(project,error_code), then canonical of the
actual joined `canonical / ".beads"`, and compares that returned path with the
business join. Its error-code overload is materially different from write_file's
throwing canonical call. Beads51 does not acquire authority to replace that path
logic or implement a private resolver.

Proposed shared file-owner interface, requiring incorporation in the pending
[file subplan](2026-10-11-file-owner-raw-subplan.md):

```
std::filesystem::path raw_file_canonical(
    const std::filesystem::path&, std::error_code&,
    const RawAdapterContext* = nullptr, RawEventRef* actual_owner = nullptr);
```

Ordinary overload/default remains unselected. The explicit file helper receives
the reached configure request as parent and assigns its actual owner Ref immediately
after that owner's begin is emitted, before native work or result allocation.
The outparam starts absent and remains absent if begin was not emitted; it never
means ordinal0 success. It preserves the selected native profile's realpath,
absolute/current_path and GCC ENAMETOOLONG fallback, exception classes and
error_code behavior. File owner supplies its native layouts/credits, caller-buffer
failure extents and cleanup. Beads supplies only reached configure call/return
and existing comparison outcomes. First output is captured by file owner before
Beads' `.beads` join allocates; second output is captured before comparison or
project string assignment.

This is a concrete dependency/interface proposal, not silent approval for new
filesystem production work. Resolve it in the file47–48 plan before Beads51 is
accepted as whole. If unavailable, leave whole Beads51 pending; do not emit fake
canonical success or call its missing native coverage complete. No full alternate
libc resolver, hidden filesystem subscription or reuse of raw-storage data.

## Business sequence and exact outcomes

Run begin/terminal credit precedes mutation reset and argument validation.
Record the actual `mutation_possible_=false` assignment every run. Missing/nontext/
NUL/over8192 value() remains the actual invalid_range throw; nonobject args still
return invalid. Keep short-circuit conditions: record only reached checks.

Configure retains allowed fields, absolute project/executable and nonempty actor,
two canonical calls and no redirected .beads. Capture project_, executable_,
actor_ immediately after each successful existing assignment; prepay before that
assignment. Keep current sequential assignment semantics, including possible
partial configuration on allocation failure: do not assert atomic replacement.
Emit bound=false and successful cache clear at their actual assignments.

Capture each native argv element in an owned preparation scope before a later
argument/validation/allocation can discard the completed vector element. Use the
existing vector and required argument strings. A narrow business argv builder
reserves that command's source-derived maximum element capacity before producing
arguments; before each element, claim its maximum body, evaluate the existing
copy/concatenation, move into the reserved vector, publish its unchanged bytes,
then continue. String moves use noexcept move and no new vector allocation.
If acquisition/insertion fails, omit a nonexistent completed element and mark
that preparation's actual exception/count; earlier published elements survive.
This owns the actual argument representation, not a second serializer. No join,
shell escaping, appended NUL or environment bytes. Strings are NUL-free by the
existing value() checks; record native size excluding their terminator.

Action argv preparation is a real reached business step before lazy binding.
It can complete even when later binding/preflight prevents action invocation.
Its terminal means argv preparation completed, not child launched. Version,
where and exact-show have separate preparation scopes at their actual call sites.
Each actual `invoke` emits its own request linked to that prepared argv, before
Child copy/start. No invocation record for a skipped child; no group collapse.

Unbound run: version invocation first; interrupted result returns immediately;
check ok/object/no-error/version text/0.58.0 before where. Where then checks
ok/object/no-error/path text/exact project_+"/.beads"; only actual successful check
sets bound=true. Invalid version prevents where/action. Invalid project prevents
action. Bound runs skip both. Configure/cached spawn zero children.

For write with explicit ID: readonly exact-show preflight must return exactly one
valid issue with exact ID before the write invocation. Retain original call and
checks; it is not a CAS, lease or later mutation guarantee. Create has no ID
preflight. Claim is update --claim alone and keeps its recheck notice. Reject
literal '-' description before spawn. IDs with whitespace/comma remain invalid;
shell metacharacters and leading '-' remain argv data after --.

Invocation preserves existing environment preparation before its try block.
Before attempted write start, prepay and record the exact conservative
mutation_possible_=true assignment; it remains true even if Child's argument
copy, cancellation check or setup refuses before an observed spawn. Do not move
it after spawn, infer it from write intent, or claim it proves an effect happened.
Record actual child.spawned separately. The existing Error catch records the
actual Error *before* allocating failed status/message or take_partial/result
members. Its returned status is unknown only for spawned write; otherwise
unavailable. This conversion is not a thrown invocation terminal. A later bad_alloc
is its own real terminal and mutation marker remains visible to Coding.

Collect uses the existing single business Clock::now and five-second deadline,
65536 stdout and independent 65536 stderr accumulation limits, with inherited
raw_wait_control and the actual deadline applied by Child. No new timer, polling
loop, elapsed subtraction/conversion or clock field. Native read count can exceed
remaining business capacity; already acquired positive bytes publish first.
Collector failure means partial acquired bytes, not a fabricated complete response.

Parse the actual out once using existing parse_json. Record its actual Result
success/error, even for nonzero exit (the parser already runs there).
parse_json converts bad_alloc to ErrorCode::allocation and length_error to
capacity; do not relabel those as thrown parser exceptions. Nonzero/malformed is
unknown for write, rejected for read. Ok envelope is preliminary until command
validation. Preserve actual shape/issue/ID/closed/claim assignee/status checks.
Wrong shape/target changes status to unknown/rejected at the existing branch;
capture that required business status before Value construction/assignment.
Cache updates only after a valid nonwrite action. Failed refresh retains cache.

No generic Value visitor or recursive encoder collects these checks. Instrument
the existing predicate/conditional sites and helper returns, retaining their
evaluation order and short circuits. For each reached check, prepay the check's
terminal before evaluating; copy the actual bool/Error/exception. Reuse computed
business booleans in their existing branch rather than evaluating again. Reached
value()/issue() helper checks include their existing NUL/size/type checks; no
decoded issue-body clone is emitted because original JSON and final operation
packet already exist.

Binding/cache attribution uses narrow optional actual source Refs alongside the
existing bound/cache state. bound=true stores actual accepted where invocation R;
successful cache update stores actual valid action invocation R. Successful config
project/executable/actor assignment invalidates old binding attribution, even if
a later allocation leaves the business bound flag unchanged. bound reset and cache
clear reset their corresponding attribution only after the actual assignment.
Unselected assignments invalidate any prior attribution instead of linking a
new uncaptured value to stale captured output. These are source links on actual
state, not a registry, new operation identity or cache-body copy. Run begin records
optional current links and actual flags; cached outcome uses that graph and does
not claim fresh child work.

Coding's existing Beads disposition remains authoritative: status unknown records
UNKNOWN; ok/configured/cached success; invalid/rejected/unavailable failure.
An escaping exception with mutation_may_have_started forces UNKNOWN. Marker true
with proven no spawn may still return unavailable/failure through the existing
converted Error path. Do not erase this distinction. Operation result capture,
receipt and terminal failures remain distinct; no automatic mutation replay or
PID adoption and no stronger containment claim than Child actually supplies.

## Literal sites, native bodies and credits

E=16 physical envelope (u32 length,u32 site,u64 ordinal); R=24 (lane16,u64 ordinal).
No acquisition clock is present. On selected Mac/Linux profiles bool/u8=1,
size_t/ssize_t=8, int=4, pid_t/uid_t=4, ErrorCode=1, Error.detail=i64=8.
Emit individual native fields without structure padding; budget with checked
sizeof sums and record-bound checks. Optional flags are always present; their
payload exists only when the flag is true. B=existing RawOperationBranch u8.
Stages are actual reached owner control positions, not derived success metrics.

|Site|Literal body after E; maximum total bytes|Partition/admission|
|---|---|---|
|3500 run begin|operationR, boundSourcePresent bool,[R],cacheSourcePresent bool,[R],bound bool,oldMutation bool =92|CONTROL; begin+3501 maximum171 before run effects|
|3501 run terminal|runR,B,stage u8,mutation bool,bound bool,statusChoicePresent bool,[R],ErrorPresent bool,[code,detail] =79|held CONTROL; absent selected status distinct from returned success|
|3502 actual bool assignment|ownerR,kind u8,value bool,sourcePresent bool,[R] =67; no-source43|claim before assignment; kinds mutation reset/start,bound reset/accepted|
|3503 actual acquired/assigned field|ownerR,field u8,native length size_t,unchanged bytes =49+N|DATA before reached bounded value/assignment acquisition; no env fields|
|3504 actual status selection|ownerR,role u8,native length size_t,actual business status bytes =49+N; max60|DATA held60 once per run and once per invoke; consume before moving required status string into Value|
|3505 actual cache assignment|runR,kind u8(clear/update),sourcePresent bool,[R],arrayCountPresent bool,[size_t] =75|CONTROL before assignment; clear omits source/count; update source=actual action|
|3508 configure path call|runR,kind u8(project/.beads) =41|CONTROL pair with3509, before shared file helper|
|3509 configure path return|3508R,B,fileOwnerPresent bool,[R],errorCodePresent bool,[native error.value int,actual category pointer],ErrorPresent bool,[code,detail]; returned max80, Error max77|held CONTROL; pair121; error_code fields only on actual return; escaping Error separately present, never fabricated error_code success|
|3510 argv preparation begin|runR,kind u8(version/where/exact-show/action),write bool =42|CONTROL pair102 before actual vector/argument work|
|3511 argv element|3510R,native index size_t,native length size_t,existing element bytes =56+N|per-element DATA before acquisition; index actual vector insertion position|
|3512 argv preparation terminal|3510R,B,stage u8,ErrorPresent bool,[code,detail],actual argc size_t =60|held CONTROL; completed preparation does not mean invocation|
|3516 reached check begin|ownerR,check kind u8,subjectPresent bool,[actual invocation/file R] =66|CONTROL pair119 before existing check/helper|
|3517 check return|3516R,B,boolPresent bool,[actual bool],ErrorPresent bool,[code,detail] =53|held CONTROL; no bool on throw; no skipped check event|
|3520 actual invoke begin|runR,argvPreparationR,kind u8,write bool,actual argc size_t =74|CONTROL pair174 before child preparation/copy/start|
|3521 invoke terminal|3520R,B,stage u8,write bool,mutation bool,spawned bool,actual exit int,out.size size_t,error_bytes.size size_t,statusChoicePresent bool,[R],ErrorPresent bool,[code,detail] =100|held CONTROL; exit initially-1 is business state, not an invented observed native exit|
|3524 collect begin|3520R,actual limit size_t =48|CONTROL pair121 before actual collect|
|3525 collect return|3524R,B,stage u8,returnedOutCountPresent bool,[size_t],actual stderr size_t,actual exit int,ErrorPresent bool,[code,detail] =73|held CONTROL; out count only when returned; Child graph retains acquired prefixes on throw|
|3528 existing observer begin|3520R,childPresent bool,[actual childR],channel u8,actual view.size size_t,observerPresent bool =75|CONTROL pair126 before arbitrary stdout/stderr observer; no second body|
|3529 observer return|3528R,B,ErrorPresent bool,[code,detail] =51|held CONTROL; absent/return/real throw separate; Child parent provides source graph, not a guessed most-recent read Ref|
|3530 existing caught Error|3520R,stage u8,code,detail,spawned bool,write bool =52|independently held before invoke body; emit immediately on existing catch, before result allocation|
|3532 parse begin|3520R,actual out.size size_t =48|CONTROL pair101 before parser|
|3533 parse return|3532R,B,resultPresent bool,resultHasValue bool,ErrorPresent bool,[actual Result Error code/detail] =53|held CONTROL; on throw resultPresent=false/resultHasValue=false is absence, not failed Result|

Tag bytes are immutable source-site variants, not status normalization. Exact tags:
argv/invoke kind1version,2where,3exact-show,4action; bool assignment kind1mutation
reset,2mutation start,3bound reset,4bound accepted; field1op,2configured project,
3configured executable,4configured actor,5validated ID,6assigned project_,
7assigned executable_,8assigned actor_; status role1run selection,2invoke selection,
3command validation replacement; cache kind1clear,2update; path kind1project,
2.beads (the actual canonical/".beads" join).

Check tags select the actual reached business condition/helper:1args object,
2configuration key allowed,3actor/executable nonempty,4project absolute,
5executable absolute,6project canonical error,7.beads canonical error,
8.beads identity,9configured state,10supported operation,11write classification,
12ID empty,13ID ambiguous,14limit numeric,15from_chars error,16parsed end,
17limit range,18stdin sentinel,19required title,20update fields,21allowed action
key,22version interrupted,23version binding validation,24where interrupted,
25where binding validation,26preflight interrupted,27preflight validation,
28action ok,29action initial shape,30issue validity,31single target count,
32target ID,33close status,34claim assignee,35claim status,36value helper result.
Compound validation tags retain their existing short-circuit expression and
record its actual final bool; do not evaluate missing operands for raw capture.
Tags36/30 record actual helper return/throw, not recursive decoded-value copies.

Run stages1reset,2arguments,3configure,4argv,5binding,6preflight,7action,
8validation,9cache,10return. Invoke stages1environment,2observer preparation,
3mutation marker,4start,5close input,6collect,7parse,8status,9result members,
10return. Argv stages1reserve,2element,3complete; collect stages1call,2return.
All branch bytes use the existing RawOperationBranch values without remapping.

Status role distinguishes native business selection from returned object: selection
may publish before Value allocation then fail. Returned terminal B establishes
whether that selection actually reached the caller. Strings are required business
storage moved into Value, not an extra metric string. Max status is unavailable
(11); configured10, rejected8, invalid/unknown7, cached6, ok2. A returned action
links its existing invocation statusChoice; wrong-shape replacement uses run's
held status body. No redundant final-run status body for an unchanged action.

Before each bounded acquired field3503, reserve49+8192 where value() bounds apply.
That site covers the enumerated op/configuration/ID and assigned state fields,
not every option copy or parsed response string. Options have their one actual
argv body; parsed string content already belongs to the native JSON stream.
For assigned canonical project reserve49+actual already-captured path native size;
do not assume8192 for all canonical output profiles. No body is emitted for a
field whose business acquisition threw. Arbitrary input keys/messages are covered
by original operation packet; do not format them into new metric text. Every
observer/check call prepays its own terminal; loops are per-reached-call, with no
finite aggregate claim that all issues/short reads fit.

Native argv bounds: executable8192; --json6; --actor= plus actor8200;
--readonly10; --description= plus body8206; --title=8200; --type=8199;
--priority=8203; --status=8201; --reason=8201; --limit=100 eleven bytes;
--claim7; --2; ID8192. Actual body uses actual length, releasing unused credit.
Maximum update has11 elements and65607 content bytes; framing makes66223 DATA
bytes across eleven independent published bodies, not one mandatory aggregate
grant. Version3,where4,exact-show6; action maxima ready6,list7,show7,create8,
update11,close7,claim7. These are bounds from code, not emitted computed totals.

Existing Child sites remain their current immutable layout:

|Native site|Existing total/credit on selected profiles|
|---|---|
|1400 begin|E+invokeR+4bool=44 DATA before native setup|
|1401 spawn/setup return|E+childR+stage1+2int+2pid_t=57 held CONTROL|
|1402 poll return|E+childR+channel1+4int+2short=61 CONTROL before poll|
|1404 selected read|E+childR+channel1+size_t+ssize_t+int=61; prepay61 CONTROL and61+8192=8253 DATA before read; positive publishes unchanged bytes once in DATA, zero/negative CONTROL|
|1405 waitid return|E+childR+method1+2int+pid_t+uid_t+2int=65 CONTROL before waitid|
|1406 close|E+childR+channel1+3int=53 held CONTROL|
|1407 owned group signal|E+childR+pid_t+3int=56 held CONTROL|
|1408 final reap|E+childR+2pid_t+int+bool+int=57 held CONTROL|
|1409 owner cleanup|E+childR+branch1=41 held CONTROL|
|1410 first refusal|E+childR+2u8+ErrorCode+i64=51 held CONTROL|

RawChildTrace already holds `12*53+56+57+41+57+51=898` CONTROL before setup.
It retains the leader through WNOWAIT before group cleanup. Beads never sends
stdin content; close_input is the existing call. Do not extend Child native sites
to export argv/env globally or replace its cleanup credit with a Beads estimate.
Unused claims reset before issuing handle release. The Beads layer separately
holds invoke174+caught52=226 CONTROL and status60 DATA before invocation body;
Child's898/44 are admitted before native start. Run171 CONTROL and status60 DATA
are separate. Configure uses shared file-owner grants, not these child grants.

Native errno copied by Child remains meaningful only on its native failure;
POSIX spawn functions return error numbers separately from errno. Preserve those
existing fields rather than normalize them. Owner Error9 is a different layout.
Configure error_code preserves actual integer plus category pointer (8 on these
profiles), without category name formatting or a guessed generic/system enum.
All admission inherits existing sticky cancellation/endpoints. No fresh ordinary
loan from operation17's settlement bank; current Boundary resets receipt_scope
before calling its body. Prepared owner/observer/Child cleanup terminals consume
held credit through local fence without waits. Fresh work stops before native
effect; a finished native effect is never inferred from a grant. No per-emission
disk wait or host-power-loss campaign.

## Direct red and independent whole-owner oracles

Use a dedicated self-exec fixture plus real pipes, marker files and explicit
acknowledgement gates under a temporary configured project/.beads. No live bd,
operator issue database, provider/model call, SQL runtime or quarantine execution.
Extend the existing native fixture pattern; it already verifies argv/env/project,
unknown retained write outcomes, cache and cancellation. Its sleeping child is
not sufficient to prove both native streams were acquired before an exception.

Before production edits, after this subplan's one root attack accepts the unit,
compile the new source-to-disk red against the current published header/library
through unchanged public `CodingEngine::operator_call("beads",args)` (no dependency
on a future direct run signature). Configure/bind with observer nonthrowing, then
perform a write fixture whose stderr prefix is acknowledged by a forwarding
test-only native read observer before the child releases stdout through an
external pipe/file gate. The same observer records actual stdout read fd/count/
literal bytes, then arms a thread-local allocation cut for the existing Coding
semantic observer's next allocation. Child's existing order invokes that observer
before pending.append. This reaches old published headers/library without access
to the private engine adapter or a future overload. Interposition forwards the
real selected syscall, records only fixed preallocated facts and preserves errno;
prove the actual throw site is that observer, not fixture bookkeeping. No
production interceptor is added. This proves both native reads occurred; child
write alone would not. Child writes an independent effect marker
before the final stream and a forbidden replay/aftereffect marker behind a gate.
Close/drain real raw runtime and inspect actual preserved source envelopes/body.
Old source has operation packets but lacks Beads-selected native1404 bodies and
3500/3520 graph. Assert the missing actual literal source bytes/child link as red,
not a mocked encoder or a postimplementation source grep. Preserve the old red
log, executable/header/library identity and exact fixture bytes in context.

Whole candidate expected values are authored independently from literal fixture
argv, stdout/stderr arrays, syscall outcomes, marker count and source field table.
Disk parser reads envelope/member widths directly; do not call candidate encoder
or reconstruct expectation from candidate trace. Compare concatenated acquired
prefixes per child/channel at the consumer; this is consumer transformation, not
writer aggregation. Accept nondeterministic chunk boundaries while requiring
each raw count/extent and order to match actual native reads. JSON business data
is validated by fixture literals, never by a duplicate metric JSON parser.

Required cases, each against real production owner path:

1. Configure/cached zero children; first unbound ready version→where→action;
   bound next action only; exact-target update/close/claim preflight separate;
   all child requests/argv preparations and actual terminals linked correctly.
2. Literal argv bytes per element, including quotes/newlines in content, shell
   metacharacters, UTF8 and8192-byte accepted values;8193/NUL refusal, '--' IDs,
   unsupported keys, invalid limits and '-' body spawn no action. Inspect child's
   argv independently. Keep private environment sentinel absent from raw output;
   verify actual routing removal/cwd through child fixture without logging env.
3. Both streams with literal NUL/invalidUTF8/malformed JSON and nonzero exit,
   exact8192/65536 and overflow prefix. Acquired bytes survive before observer,
   business append limit and parser. Native output may be larger than retained
   partial semantic string; do not compare only result stdout_bytes to raw count.
4. Observer Error, bad_alloc, length_error, ordinary/nonstandard throw after
   acknowledged prefix: raw source unchanged; observer terminal exact branch;
   Child cleanup before invoke terminal; spawned write remains UNKNOWN in actual
   retained terminal. Never let a forbidden replay marker occur.
5. Genuine phase cancellation before version, after where starts, exact-show
   prefix, and action marker/prefix; actual child counts and native refusal,
   converted Error and run outcome. No phase after interrupted early return.
   Cancellation during raw admission has no subsequent native call; paid cleanup
   still completes. Existing five-second deadline remains business behavior.
6. Native setup/spawn/open-pipe failure versus postspawn read/wait error and
   cancellation. Test-only selected interposition forwards unrelated calls.
   Preserve actual rc/errno and before-spawn distinction; no fabricated child.
7. Exact CTRL/DATA maxima and one-byte-less after held run/invoke/Child cleanup:
   refuse before relevant acquisition; maximum grants publish correct variants.
   Fill only ordinary headroom after acknowledged prefix, then force refusal;
   prior bodies remain on disk, cleanup consumes898 without another grant.
8. Allocation cuts before argv insertion, after published element, after child
   argv copy admission, after acquired stream, within parse_json, caught-Error
   result construction, validated cache assignment provenance/shared-storage identity, and postaction result/audit_ref.
   Preserve actual thrown/converted branch and conservative marker. Reuse
   operation17's result-capture allocation oracle for its existing boundary;
   this unit supplies Beads linkage and real child effect marker.
9. Unsupported version, wrong where, invalid preflight/ID, zero-exit error object,
   create-object/read-array mismatch, wrong close status and claim assignee/state.
   Native zero exit retained separately from validation; write UNKNOWN/read
   rejected. Failed refresh preserves actual previous cache/source; configure
   successful clear resets it; partial allocation during config records actual
   sequential assignments without claiming atomic configuration.
10. Default direct adapter calls under a raw binding emit no Beads/selected Child
    graph; explicitly selected Coding calls do. Interleave selected/unselected
    cache/binding changes and nested observer operations; old source links cannot
    falsely describe a new uncaptured state and parent cannot drift to nested
    operation. Exact emitted field widths/counts and no elapsed_us result member.

Recorder process failure remains the already implemented raw transport contract;
do not rerun an unrelated power-loss/lifecycle campaign. Relevant ownership cuts
must prove published bodies survive owner unwind and clean runtime closure.
Native process cleanup proves cooperative owned-child actions, not containment
of arbitrary descendants. No old process adoption, effect replay or exactly-once
external mutation promise is introduced.

## Implementation order after accepted root attack

1. Root resolves shared canonical/error_code interface with pending file47–48;
   incorporate literal field/grant corrections before either consumer ships.
2. Preserve published-source native red and independently authored fixture.
3. Own narrow Beads trace/argv preparation and explicit run/Coding context; wire
   native Child capture, observer/parse/check/state/caught-error/terminals as one
   unit, remove Beads writer elapsed metadata. Default helpers remain opt-out.
4. Run the affected native/engine/raw literal byte oracles and existing Beads
   business checks. Root owns shared builds; no compiler started by this plan.
5. One focused changed-unit recheck/fixes under current two-layer doctrine;
   repair concrete findings and rerun affected direct oracle only. No third
   assurance layer, review of the review, unrelated source gate repetition or
   invented wall-clock cutoff. Report actual results and remaining dependency
   limits; do not activate an incomplete whole-owner candidate.


## Oct13 accepted implementation corrections and genuine red

The completed ONE plan review requires status admission before the required
business status construction (literal view input, one owned string moved into
Value), per-element admission before argv construction and reserved-vector
noexcept insertion, source invalidation on selected/default/partial state
mutations, and lexical Child destruction before invocation terminal. Parser
Result failure remains distinct from a thrown exception. The completed file
canonical helper supplies an optional actual3410 request outparam immediately
after its request publication; opt-out/begin refusal leaves it absent. This
narrow signature dependency belongs to Beads' whole owner review, not a second
review of settled file behavior.

Matching old current headers and three Debug archives were preserved before
Beads edits in ignored context/beads-owner-unit/published. The genuine public
CodingEngine::operator_call baseline forwards actual native stderr24/stdout24
binary reads. An inherited pipe acknowledgment releases stdout only after the
parent acquires stderr; the child writes its independent once effect marker.
A stack-selected real AuditLog::original allocation then throws after acquired
stdout. Recorder closes the native run without8000/8001. Old raw originals are
0/0 and3500 run graph is absent, so the direct byte/owner assertion exits1 at
the intended missing-capture oracle. Initial setup permission/SDK compile
corrections were not called red. Source, archives, headers, executable and log
are preserved, no operator bd/database was used.

Current Value source correction: `Value::operator=(const Value&)` shares the
existing Storage when the parsed data handle is unexposed. Beads receives that
const parsed handle; there is no allocating cache-copy boundary to fault. The
direct cache case compares actual returned/cached Storage identity, accepted
3505 actionR/count and failed-refresh/default-invalidation behavior instead of
inventing a clone or injection seam. The actual8192 argv acquisition allocator
rounds to8200 on this libc++; its cut selects the real RawBeadsArgv::push stack
and required source extent, not a guessed8193 heap size.


2026-10-13 SAME B1 amendment:3503 field9 is the completed SAME
canonical-project/".beads" operand, paid49+actual canonical.native().size()+1+6
BEFORE join, emitted before following File admission, reused by equality.
Configure's second canonical path receives it by const reference, with no second
join or copy. B2 literal whole-native pair checks and shared RawEmission migration
are recorded in the one code attack disposition. No new owner/bank/transport.
