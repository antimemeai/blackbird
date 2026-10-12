# Context owner emission: completed source study and subplan

2026-10-11. Study and proposed implementation of census24–26 and29: ContextStore
working mutations, managed context, original/history reads and checkpoint/replay.
CodingEngine turn/budget/protocol/stop owners13–14,27–28 and32 remain the root's
separate unit. Native physical storage/indexing is a later owner: a Context API
return does not describe every syscall. Source was released after the Wake unit checkpoint and ONE plan attack.

Read context.hpp/cpp, retained_state.hpp/cpp, retained_events.hpp, saved_state,
the Context/delta/budget/saved-state fixtures and the actual turn catch paths.
The [older raw-emission study](2026-10-10-instrumentation-raw-emission-study.md)
supplies separate request/return records, immediate native copies and a separate
decoder. Rechecked acquired Reno kern_ktrace.c syscall/return/I/O routines:
its omitted errors and synchronous vnode writes are counterexamples, not adopted
mechanisms. [Recorder study](2026-10-10-raw-recorder-design-study.md) supplies
original retention and independent logging. [CLM study](2026-10-01-context-language-models-study.md)
and acquired context_env/env.py make editable view, original source and
provider input distinct; missing edits and discarded originals must not disappear.
[Context delta study](2026-10-08-context-deltas.md) explains existing fresh-entry
packets. Do not reintroduce history-sized mirrors or encode another packet to
measure one. No new literature campaign, dependency, timer or evaluator.

## Actual owners and source boundaries

|Class|Current source|Capture and independent business oracle|
|---|---|---|
|Working append/edit|append_impl/edit|Actual head and row sizes, actual application ID/channel, publication request/return, reached swaps; retained packet already contains originals/candidate/ranges. Existing delta/stale/invalid edit fixtures define the next view.|
|Successor/restore wrappers|seed_successor/validate_successor_seed/restore|Explicit parent links to validation/read/edit/append children, literal existing source/entry labels. Validation owns no Context instance; never invent empty state.|
|Managed proposals|manage/reject_managed/publish_managed|Existing parsed mode, selected-set size and stage; real protection invocation/return; staged rejection, pending transfer, actual accepted/rejected publication. Existing tool-group/pinned/continuation fixtures define effects.|
|Workflow lifetime|begin_workflow/finish_workflow/pending_proposal|Actual workflow/pending transfer, settlement and optional program boundary. Failed successful settlement must retain its original pending proposal.|
|Working reads|view/items/stats|Existing returned rows and codec lengths, before presentation allocation. Each existing codec runs once.|
|Original/history reads|original_entry/Original::entry/captured_entries/visit_context_records/inspect|Actual lookup/page arguments and returns; actual retained record references or literal resident bytes; failures before decode; existing inspection pages before hex.|
|Derived save/replay|checkpoint/maybe_checkpoint/constructor|Real saved accelerator attempt/fallback, native input record, selected replay branch, save Result, later pruning. Saved/compact/full replay fixtures define recovered state.|

ContextStore owns head_, entries_, originals_, captured_, workflow_, pending_
and archive_loaded_. A valid head is empty or its existing32-byte business
revision; copy it without parsing an identity for metrics. Its destructor only
unregisters the existing maintenance callback. It acquires no metrics resource
and cannot claim physical storage shutdown coverage.

append/edit/publish_managed prepare their candidate and returned Value before
publication, then transfer prepared state. Distinct publication success remains
visible if subsequent work fails. protect is fallible: emit its request before
argument acquisition/invocation and its return once. manage writes the staged
rejection before constructing Pending; later allocation failure differs from
that accepted rejection and cannot claim pending state. finish_workflow changes
workflow_=false first; preserve that reached transition if settlement fails.

checkpoint can publish saved state successfully and then fail while pinning
history/building live_ids/pruning. Its save return and final failure differ.
Construct the trace INSIDE its existing try: raw admission failure remains a
Result and cannot make optional checkpoint throw after an accepted edit. Capture
saved reconstruction fallback at the actual Error/bad_variant branch; replay is
not a fresh edit. Existing projection/revision validation runs once; capture the
input before those transformations.

## Native layouts

Sites1800–1829, checked against1500 authored/1700 task/1900 turn/2000 command.
Root owns registration. E16 is the existing envelope; R24 is lane16/ordinal8;
Text is size_t8 and exact bytes. bool/u8 are1; ApplicationChannel and RetainedKind
are native uint16. Error is code1/detail8. JournalCursor is journal16/sequence8/
end8; RecordReference is journal16/sequence8. Widths include E and refer to the
selected64-bit profiles. No padding, Number conversion, dictionary or clock.

|Site|Body and real boundary|Maximum/fixed credit|
|---|---|---|
|1800 owner request|action:u8,has_parent:bool,[R],head Text,entries/originals/captured:size_t,workflow/pending/archive_loaded:bool; static validation omits all instance fields|85 unparented,109 parented; static18/42|
|1801 publication request|owner R,actual ApplicationRecordId16,ApplicationChannel2,actual root JournalCursor32; before existing record/record_boundary|90|
|1802 publication return|publication R,branch:u8,return_kind:u8, actual returned RecordReference OR JournalCursor on success, Error on failure; never infer a physical reference|reserve82; success<=74, failure51|
|1803 state transfer|owner R,stage:u8,head Text,three native sizes,three native flags; immediately after reached transfer|108|
|1804 protection request|owner R,callback_kind:u8; before argument acquisition/call|41|
|1805 protection return|protection R,returned/Error/allocation/unknown:u8,[Error]|50|
|1806 read return|owner R,variant:u8,actual returned rows:size_t; after returned object construction|49|
|1807 stats item|stats R,item ID Text,existing codec native length:size_t; before Number presentation|56+ID bytes, DATA|
|1808 stats return|stats R,entries/input_bytes/view_bytes:size_t; existing codec lengths|64|
|1809 inspect page|inspect R,page length:size_t,existing page bytes; before hex|48+limit DATA; max65584|
|1810 inspect return|inspect R,actual offset:u64,page size/total:size_t,snapshot Text,current head Text; after returned Value construction|144|
|1811 save request|checkpoint R,root JournalCursor,had_saved:bool,[prior boundary JournalCursor]|105|
|1812 save return|save R,branch:u8,[Error],root JournalCursor,has_saved:bool,[actual saved boundary]; before pruning|115|
|1813 original input request|owner R,purpose:u8,source_kind:u8,native byte length:size_t,has_record:bool,[actual RecordReference],has_application:bool,[actual ApplicationRecordId],ApplicationChannel2,index:size_t|102 with both optional identities|
|1814 original input return|input R,branch:u8,actual length:size_t on success OR Error on failure. Resident success includes exact bytes; retained-packet success references the preserved source|held50 CONTROL; resident success49+length DATA|
|1815 owner terminal|owner R,returned/refused/staged/Error/allocation/unknown:u8,reached_stage:u8,[Error]; once|51|
|1816 existing label|owner R,label_kind:u8,Text; existing origin/entry/reason/query kind/format only|49+length DATA; named bounded cleanup literals may use prepaid CONTROL|
|1817 history page request|owner R,first/requested/history.count:size_t,purpose:u8; before existing page(first,16)|65|
|1818 history page return|page R,branch:u8,[actual rows:size_t],[Error]; before packet decode|reserve58|
|1819 original lookup request|owner R,family:u8,RetainedKind2,actual parsed identity16,root JournalCursor32; before lookup|91|
|1820 original lookup return|lookup R,branch:u8,[actual RecordReference,ApplicationChannel2] OR Error|67|
|1821 reconstruction branch|reconstruct R,saved/fallback/accepted/rejected/invalid:u8,[Error]; at actual branch|50|
|1822 saved accelerator input|reconstruct R,actual saved boundary JournalCursor,version/source-present flags; existing saved object, no re-encoding|74|
|1823 managed fields|managed R,existing mode Text,actual selected count:size_t,existing stage Text; actual computed locals|105 for validated mode<=9/stage<=32|
|1824 reserved|No extra physical-reference event: native1802 already supplies it; Original retains provenance|—|
|1825 cancellation budget return|budget R,actual journal max_payload/max_batch:u32,existing JournalCapacity:u64 fields; after business computation|64|
|1826 inspect parameters|inspect R,actual requested/limit:size_t,json_export:bool; after existing parsing|57|
|1827 finish parameters|finish R,actual success/boundary-present:bool; before workflow transfer|42|

1814 is emitted before decode/projection. Decode failure belongs to the enclosing
terminal/1821, not a second input terminal. 1818 describes the page API result;
per-fact/index reads belong to physical storage. Cold input uses a real retained
record whose payload is preserved. An application ID is neither a physical
locator nor a read permit: resident bytes still need paid DATA. No extra traversal
counts edits/selected IDs/rows; existing containers/locals supply these fields.
Actual retained publications already preserve their candidate originals.

## Explicit origin and native provenance seams

Add raw_context.hpp/cpp for immutable descriptions and the small native owner
helper. RawContextOrigin is explicit stack input: actual parent R, optional
borrowed same-producer CONTROL bank, optional retained-region bank and typed
input policy (fresh admission or no new input). No TLS parent guessing/registry.
Selected Context APIs/private children take an optional origin; children receive
their actual caller request R. Each helper splits its entire finite subtree before
its first state effect and reserves its terminal separately. Banks outlive those
stacks; no producer migrates. No callback/wait separates final grant, acquisition
and immediate copy. RawRegionPrepaidScope selects retained_append only, using
existing432 whole-region splits; it never spends semantic terminal capacity.

Original retains optional actual application/record provenance at creation and
reconstruction. Constructor tail_facts, history pages and archive original_entry
already own fact.record; preserve it. Resident Original{entry} owns an encoded
business entry: read and copy its existing bytes before decode. No new encoder
or generic ImmutableBytes locator registry. Local originals receive the actual
issued application ID/publication request. Return native Submission.record from
the existing preissued-ID AuditLog::record overload (other callers may ignore it).
Attach a physical record only when actually supplied; no metrics-only history
read and no sequence manufactured from a stale cursor.

record_boundary's second program ID is local to AuditLog. Pass a narrow optional
RawContextPublication bundle into that method: already held request/return credit
and real owner R, no callback. Once its existing issue returns, emit1801 for each
actual context/program ID before the existing two-event append. Each gets one
1802 describing that actual returned append cursor. Do not invent the first-event
RecordReference. No-pending successful finish preissues its ordinary program ID
and uses the native record overload. Business encoding/publication counts stay
unchanged; physical refs unavailable at a boundary remain unavailable.

The maintenance callback becomes Result<void>(const RawEventRef&). append_impl
passes its OWN retained_append begin lane/ordinal; Context passes that origin to
checkpoint. Update the two saved_state_test callbacks. protect takes an explicit
third const RawEventRef&: actual1804. CodingEngine passes it through protect_workflow
to budget/view/pending reads; update the two Context fixtures. Root approved both
seams. Participant2111 drain/provider acceptance may pass real owner origins to
append; their workers transfer bytes/Refs, never ContextStore or issuer claims.
Coordinate Context header/source windows after source release.

## Admission and exact finite banks

All finite post-effect records are held before effects; no fallible metrics
allocation follows publication. Actual Error/allocation is caught; a single-use
terminal defaults to unknown for another C++ exception. Emit return after Value/
array construction. Separate publication success survives later failure. Emitters
preserve errno; disabled capture adds no encoding, lookup or traversal.

Publication pair90+82=172. Basic append/edit/single-record managed publication
requires109+51+108+91+172=531 parented (507 unparented). Two-event context/program
boundary adds172:703. Unused protection/pending argument credit releases normally. The531 base
describes native append effects; ordinary append/edit also budget their actual
pending_proposal child when protect is installed. Stop clears protect and has no
such child. Checkpoint fixed max
109+51+108+105+115=488. Dynamic acquisitions prepay their bounded request/return
and resident DATA BEFORE any existing read; source pair102+50=152, lookup91+67=158,
page65+58=123. Release unused DATA after the immediate native copy. Enclosing
terminals cannot be consumed by children.

|Ordinary parented owner|Finite CONTROL subtree; dynamic acquisitions/labels separate|
|---|---|
|begin_workflow|109+51+108=268|
|view/items/pending_proposal/originals/original_entry wrapper|109+51+49=209|
|static successor validation|42+51=93; no instance fields|
|checkpoint|488; dynamic original reads separately admitted|
|append/edit|531+209+488=1228; actual pending_proposal argument before protection; origin label before issue/publication|
|reject_managed|109+51+108+172+(49+24)=513; all compiled reasons<=24|
|publish_managed|703+105+513+488=1809 including nested rejection/checkpoint; single event releases172|
|manage|109+51+2*108+91+105+1809+513=2894; actual pending transfer and staged-result extension|
|finish(false)|109+51+3*108+513=997|
|finish(true)|109+51+3*108+1809+513+172=2978; conflict rejection, managed branch and rejected-boundary program record|
|restore|109+51+1228=1388 plus input/entry label; child edit|
|seed_successor|109+51+93+1228=1481 plus labels; child validation/append|
|stats|109+51+64+209+209=642; real items/view and per-item56+ID DATA|
|inspect|109+51+144+57=361, labels, per-page/source,48+limit DATA|
|cancellation_budget|109+51+64=224; existing cost work once|

Reconstruct preholds109+51+2*108+74=450. Per-record1821 branch50 and each
source/page budget are held BEFORE obtaining that record. No history-sized
allocation or new tail traversal for metric counts. Readers keep existing pinned
lifetimes. Valid modes max9 (summarize), issued stage32 and compiled reasons24
justify those banks; IDs/origin/query strings without validated limits use exact
DATA, not guessed CONTROL. Copy labels at existing business acquisition, without
parsing a returned Value for its already-owned native count/accepted flag.
Selected cleanup stop.linkage is12 bytes:61 CONTROL.

## Turn cancellation bank and optional work

Actual turn Error cleanup clears protect, enters MaintenanceScope, calls
finish_workflow(false), and for interrupted/capacity with live root calls view,
existing static stop_outputs and append(stop.linkage) iff nonempty. Unknown calls
only finish(false). Root prepays before turn effects and lends explicit origins.
No fresh wait/check/input acquisition occurs under this held cleanup policy.

|Maximum selected cleanup|CONTROL|
|---|---:|
|finish(false) own request/terminal/three states|484|
|one reject_managed child with ID/publication/reason|513|
|view request/read return/terminal|209|
|stop append531 + literal label61|592|
|optional checkpoint fixed allowance|488|
|Context semantic subtotal|2286|
|retained_append whole-region allowance16*432|6912|
|Complete Context stop bank|9198|

Root approved9198 plus1346 fixed turn:10544 before effects. Stop Value allocation
can fail; held failure emits and prior originals stay preserved. Optional cleanup
checkpoint gets no paid input/save acquisition allowance. Refuse BEFORE an unpaid
read/save, inside its Result contract; emit held capacity failure at that stage.
Never replenish canceled DATA with raw_wait_data/raw_claim_control. This reports
admitted-capacity shortage, not fictional native I/O. Accepted parent append
stays accepted. Ordinary optional checkpoint may admit its separate operation
inside the same Result boundary. Disabled capture keeps existing checkpoint
behavior. Future physical hooks must honor this no-unpaid-acquisition boundary.

Region derivation: false settlement may issue one rejection ID/record; stop append
may issue one ID/record. Reservoir1024, truncated nearUINT64_MAX, allows one refill
submit per issued ID. At most FOUR outer append_impl calls, zero sources/one
encoded event T<=actual max_payload M. Rejection proposal52+T<=52+M. Valid limits
M>=24 and max_batch>=M+88; empty rejected marker20 allows floor((M-20)/24)
dependencies. Three chunks require M<52, whose dependency limit allows at most
one chunk: proposal encoding fails before diagnostic append. Executed rejection
has at most two source chunks/groups plus marker, and outer append: FOUR regions.
Diagnostic append cannot recurse. Each has<=four52-byte observations, fitting
existing432 whole credit. Sixteen regions6912 covers actual refusals, not cached-ID
success only. Release surplus; writer computes no aggregate measurement.

## Pending settlement bug within this owner

finish(true) currently moves *pending_ before fallible reject/publish. On exception,
pending_ stays engaged with moved-away proposal/snapshot/expected/stage;
pending_proposal returns null and manage refuses pending-overlap. Preserve the
original until settlement completes. Copy a staged candidate before publication
(failure leaves pending_ intact), then reset only at actual accepted/rejected
settlement. Cancellation(false) intentionally moves/resets as before and records
that reached state. Actual copy-allocation/native publication cuts must leave the
exact proposal available for owner retry, rather than settle an empty proposal.
Never silently retry a poisoned/uncertain writer.

There is a second real allocation boundary: append/edit/publish_managed currently
call history_snapshot AFTER accepted record publication but BEFORE prepared
head/entry/original swaps. history_snapshot constructs archive_reader and the
FactHistory std::function closures; these can allocate even though SharedSequence
itself pins its root cheaply. A bad_alloc can therefore leave the retained
accepted revision ahead of the current Context view. Perform prepared noexcept
state transfers immediately after acceptance, then pin history as a separately
fallible stage. Pass an explicit local actual-settlement flag to reject/publish
from finish(true); set it upon actual record acceptance. If later pinning fails,
finish resets pending custody for that accepted settlement and propagates the
failure, never reappending an accepted child. Prepublication failure keeps the
original pending proposal. Keep failed history pinning visible: mark the owned
pin stale at acceptance and refresh it at the next historical operation before
reading pages; a failed refresh returns its actual error rather than serving an
old pin as current history. No new metric count/registry or metrics-only traversal.
Root approved the actual-acceptance flag and noexcept-transfer order. The direct
allocation cut targets the actual closure allocation after append, rejection and
managed/boundary publication; it checks accepted head/entries, retained original,
pending reset versus preservation, and historical retry/refusal separately.

The root's ONE plan attack requires TWO distinct native publication flags:
context_settlement_accepted and boundary_program_accepted. A managed rejection
record may settle pending custody before a separate program write; that first
success cannot activate pending definitions/policy. Conversely, the no-pending
program write or the atomic context/program batch can be accepted before a later
history-pin allocation throws. Supply an explicit native output status to the
turn owner, initialized before effects and updated only by the actual respective
accepted publication. The turn catch performs its already-prepared noexcept
activation swaps only if boundary_program_accepted, including postpublication
failure; it cannot discard/reappend an accepted boundary. Pending Context custody
uses context_settlement_accepted independently. Direct cuts distinguish rejection
accepted/program write refused, no-pending program accepted/pin failure, and batch
accepted/pin failure. The corrected root fixed turn allowance is1346 (including
two actual stop_outputs request/return pairs139 each);1346+9198=10544 preheld.
This resolves the single plan finding without another panel.

## Implementation order and direct oracles

1. Genuine baseline red: real ContextStore with RawRuntime lacks1800. Independently
   expect bodies/Refs from real head/IDs/cursors/rows and literal originals, not
   candidate descriptions or another metric codec. Pending allocation/write-cut
   custody oracle also must fail against actual unchanged business code.
2. Implement owner helper, explicit origin/provenance, callbacks and registration.
   Root owns10544 turn integration and CMake. No broad shared coding.cpp edit.
3. Implement mutations/workflow, cold/resident acquisitions, managed modes, query,
   checkpoint/reconstruction as this complete owner. Every selected request and
   actual terminal includes refusal/stale/invalid/Error/allocation/unknown/staging.
4. Real append/delta/edit verify exact old/new head, ID/channel and returned record
   against native journal. Accepted publication plus later history-pin/Value/optional
   checkpoint failure has no false failed publication, duplicate terminal or fence;
   protection failure is prepublication. Cut the real postacceptance history closure
   allocation, retaining the accepted Context revision and correct pending custody.
   Existing retained packets specify changed content without a new serializer.
5. All select/archive/summarize/restore modes, whole groups/pins, staged/overlap/
   continuation/stale/cancellation, and pending custody fault. Each child has actual
   earlier same-producer parent, including1804 budget reads. Context/program boundary
   has both actual IDs, never fabricated physical refs.
6. Real compact reopen/archive/resident inputs check native references and literal
   bytes before decode. Bad saved accelerator, fallback, malformed packet, missing/
   corrupt source and partial page failure have honest input/owner outcomes. No
   metadata-only read permit. Full DATA prevents resident acquisition until grant.
7. Stats lengths match actual business codecs/outputs. Inspect verifies BBMS framing,
   explicit JSON export pages before hex, slicing/offsets, stale snapshot, invalid
   kind/format/limit and replay equality. Save write/sync/replace/directory-sync cuts
   reuse native FaultFile/FaultDirectory patterns. Allocation after actual saved
   publication retains save success and final pruning failure separately.
8. Full ordinary lanes plus sticky cancel use held9198, including real issuer refill
   and refusal/diagnostic path. Stop record/placeholders survive without fresh waits;
   optional checkpoint refusal cannot undo them. Actual turn fixture checks complete
   generation/origin/state links, not a standalone helper simulation.
9. Affected Context/delta/budget/saved/engine/native raw_context greens; narrow static;
   ONE code/oracle attack, finding fixes/direct recheck; root source gate and Mac/
   supported Linux debug/release/ASan. Build/nls-command -j1 after release, six total
   workers. No extra panel/timer/eval/benchmark campaign/laptop mutant.

The ONE plan attack and source release are complete. Root owns turn integration,
registration and the remaining ONE code/oracle attack. Participant/provider optional
origins share coordinated Context windows. Physical syscall/index owners remain
separate work and are not completed by this unit.


## Implemented owner and direct qualification

The new raw_context helper publishes native fields with explicit actual parents,
separate requests/returns, exact resident input/page bytes before decode/hex,
and held failure/allocation/unknown terminals. No metrics encoder, subscriber,
new historical mirror, count traversal, timer or dependency was added. Existing
stats/inspection codecs run at their existing business boundaries. Original owns
actual application and physical provenance where the creating/reconstructing
operation supplies it; resident entry bytes still use paid DATA even when their
parent packet has a physical locator.

finish(true) copies Pending before any publication. Actual Context settlement
acceptance retires it even if later history pinning fails. Actual program append
or atomic two-record acceptance sets the distinct boundary flag. Prepared state
swaps precede fallible history pinning; historical reads refresh an explicitly
stale pin. finish parameters1827 precede workflow transfer. Its42-byte record
uses the preheld allowance for a third108-byte parent state that this code never
reaches (at most workflow transfer plus pending retirement). Banks and9198 remain
unchanged. Protection callbacks receive the actual1804; maintenance callbacks
receive the actual retained_append begin lane/ordinal, captured at begin.

Two actual source baselines were run before implementation:
`context/context-owner-missing-owner-red.log` shows a real append/view with zero
1800 owner requests; `context/context-owner-pending-custody-red.log` shows the
exact pending proposal lost on allocation refusal inside settlement. The initial
allocation placement accidentally cut default Value argument construction and
preserved pending; that run was not the defect. The corrected baseline constructs
the argument before arming the allocator, so it reaches the actual function body.
Both sensitive cases now pass.

The fixture measures archive_reader closure allocations during an actual history
snapshot, resolves its executable call site outside the fault, then refuses only
that same allocation after publication. Darwin debug measured144 bytes. It does
not choose a guessed allocation size or fail an earlier candidate allocation.
Direct cases distinguish accepted append, accepted context/program batch,
program-only acceptance, and accepted rejection before unattempted program write.
They verify live state, exact pending retirement, native cursor advancement and
no second append on retry. Successful saved publication followed by the same
history-pinning allocation refusal retains save success and a failed final Result.

Actual native forwarding JournalFile/Directory cuts cover prepublication write
EIO, saved-file replacement EIO, cold tail reconstruction input EIO and archive
lookup/read EIO. The latter blocks FramedJournal by existing storage policy; the
oracle checks later restore refusal and unchanged active view rather than
pretending the damaged owner can continue publishing. Saved replay and cold
restore independently recover exact original bytes.

The full-lane stop case stops the real recorder/supervisor, fills physical
ordinary CONTROL, exhausts the actual durable issuer reservoir, and publishes
sticky native socket EOF. A fresh admission returns interrupted. The original
9198 bank alone publishes cancellation rejection, reads the working view and
appends the exact unresolved tool output, including the actual issuer refill.
Real intervening journal records force the existing checkpoint threshold; the
held checkpoint refuses unpaid input with a capacity Result before any save,
without undoing the accepted output append or changing the previous saved boundary.

The native audit reader checks complete envelopes, sole-producer ordinal order,
all selected earlier parent links and exactly one matching terminal. Literal
input IDs/channels/physical references and lengths are compared with actual
retained facts; resident bytes with actual original entry packets; inspection
pages with the actual business page. Native field widths, callback Error detail,
allocation terminals and successful lookup shapes are checked independently.
The cold oracle found lookup credit underestimated in the paper/source: actual
request is91 (E16+R24+family1+kind2+ID16+cursor32), and return67, so the dynamic
bank is158. Managed field1823 is105, correcting publish1809/manage2894/finish2978.
These ordinary/dynamic corrections do not change the stop/turn banks.

Affected Context/delta/budget/saved-state fixtures retain their business assertions.
Native raw_context includes the new provenance/fault/pressure oracles. Qualification
logs are under context/context-owner-*. Root owns the ONE code/oracle attack,
supported-platform profiles and final integrated gate; those are not claimed here.

Current source qualification: affected `raw_context`, `context`, `context_delta`,
`context_budget` and `saved_state` pass5/5 in17.83s
(`context/context-owner-final-direct.log`). The native fixture takes0.97s.
The final build is clean (`context/context-owner-final-build.log`). LLVM23.1.2
clang-tidy exits0 without diagnostics for context.cpp and, sequentially, the
raw_context helper, retained_state maintenance seam, local_timing epoch seam,
native raw_context fixture and the three changed business callback fixtures
(`context/context-owner-tidy-context.log`,
`context/context-owner-tidy-remaining.log`). All owned source passes the configured
clang-format dry check and git diff --check. No compiler/static worker remains.
The1827 finish argument event and its exact native field oracle are included in
this current qualification; its42 bytes fit the unused third-state allowance,
leaving the approved9198 stop bank unchanged.

The root's ONE code/oracle attack found C1 in the native fixture: publication
pairing alone did not compare native identities or returned references, and1827
only checked the canceled arguments. The same fixture now collects actual
application IDs/channels/physical references from retained facts and independently
reads the accepted native journal frames to locate each real commit cursor.
It compares every1801 ID/channel/native prefix, every successful1802 physical
reference or batch cursor, and all seven1827 caller arguments in order. Failed
EIO identities/channels/prefixes come from the actual unaccepted ARPROP draft;
their1802 return must have kind0 and the literal error, without a physical Ref.
Both batch members must name the same actual two-record commit. No production
source or bank changed. The affected native fixture passes1/1 in1.06s; LLVM23.1.2
fixture-only tidy exits0 without diagnostics, configured format/diff checks pass.
Qualification logs are in context/context-owner-c1-*; the optional-access static
finding/red is retained and resolved by the explicit owned-packet branch.
