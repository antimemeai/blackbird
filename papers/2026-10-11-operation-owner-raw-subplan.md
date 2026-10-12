# Native operation and result owner, census17

2026-10-11. Design/subplan only; production source is not released. This is the
whole CodingEngine::operation owner: request/preflight, actual admitted identities,
dispatch, returned native outcome, the existing business result encoder, original
publication, semantic retention/settlement and caller return or throw. Sites
2400–2439 are reserved by root. Root performs ONE plan attack before baseline red
and source. The completed provider PX1 correction is a separate finding layer.

## Source and study

Read current CodingEngine::operation/call/request/poll_commands, its private
Boundary, RetainedState::dispatch/submit/append/reserve_identity,
AuditLog::original, packet Encoder/encode_packet/encode_packet_string,
ProviderOriginalLease and the participant/source reference contract. Grounding is
the acquired raw-emission/transport/recorder studies and actual Reno ktrace.h,
kern_ktrace.c/kdump.c, not a new literature campaign or dependency. Reno separates
syscall requests and returns; kdump performs formatting and time subtraction.
Lampson's narrow interface and separate background work, the UNIX byte transport,
and Lamport's publication order remain the existing recorder architecture. None
licenses dropped failures, derived writer metrics, or a second original encoder.

Current ApplicationChannel has log/context/program; there is no production
ApplicationChannel::metrics emitter. operation.result nevertheless writes
duration_ns into its log metadata. Its two Observations::time calls scalarize raw
clock returns to ns, subtract a sampling span and construct Value projections.
Those operation-local calls and duration_ns metadata are removed. Whole-operation
begin/end copy native RawClockSample fields; consumers derive durations after raw
disk preservation. Legacy packet word IDs and readback of old duration metadata
stay readable. This does not migrate the separate variable/git/doctrine owners.

The existing human completion string uses elapsed milliseconds only when actual
operation_outcome/operation_completed presentation runs. Keep that business
presentation and its existing business clock baseline distinct from raw emission;
do not emit its formatted text or duration as a metric. Status format_value,
error guidance and process_output remain business presentation, not raw codecs.

## Real gaps and changed order

1. Preflight poll_commands/task validation/cancellation/effect_policy/status can
   fail before the current admission region. A live whole-operation terminal must
   already exist before those calls and before name DATA admission.
2. Native issued IDs currently pass through hex/Value allocation before raw
   operation identity capture. Copy each actual acquired ID immediately, including
   a partially issued sequence; do not fabricate a complete attempt on refusal.
3. encode_packet_string successfully acquires an encoded vector, then allocates
   its string copy. Failure at that conversion loses already encoded bytes. Use
   the existing encode_packet primitive ONCE and publish its actual returned
   vector before conversion, ImmutableBytes, metadata, callbacks or retention.
   This is not a new encoder or an extra encode. Other wrapper callers outside
   this owner are explicitly not completed by this unit.
4. Boundary currently returns its Value to RetainedState, which appends the
   AdapterReceipt before operation encodes the result. Recording refusal can
   destroy a successfully returned result without its existing encoder running.
   Move the existing result preparation/augmentation and ONE result encode into
   the private Boundary, after native adapter return and before Boundary returns
   to receipt retention. Copy native effect return first; publish successful
   encoded bytes immediately. Effect return, result preparation and receipt
   recording have separate native outcomes. Receipt failure does not rewrite a
   successful effect as failure or establish a complete durable result.

Boundary retains the actual returned Value for the existing caller. The existing
program_config/read_file/tasks error guidance and exec output_ref/timeout
augmentation move with the existing result preparation; they are not recomputed
by instrumentation. Native timed_out/persistent_command/adapter_capacity locals
feed the later existing business disposition logic once, not a second parser or
Value traversal for metrics. No native exit is established by exec's initial
backgrounded return. CommandJobs retains the actual later job lifecycle.

Boundary catches actual adapter Error, bad_alloc, length_error, other std exception
and nonstandard exception distinctly in raw2407. Preserve existing Error/alloc/std
business mappings; length_error keeps the current std/external_unknown mapping.
The currently escaping nonstandard adapter exception gets external_unknown receipt
and is rethrown after bookkeeping rather than silently losing its receipt. It
does not run a new response encoder or fabricate an error-body original. An
exception_ptr holds an actual deferred business exception where needed to keep
the exact original throw across the synchronous receipt append; it is not an
emission callback, registry, or successful-path allocation. Result-preparation/
encoder failure is separate from the adapter's effect Result. Existing caught
adapter std exceptions retain their existing external_unknown business mapping,
not a new arbitrary-exception rethrow. Capture a preparation failure's actual
exception and finish receipt with the unchanged effect outcome before rethrowing.
Do not reconstruct a caught arbitrary exception from a metric Error code.

## Owner and lifetime

Add owned raw_operation.hpp/cpp with one stack RawOperationTrace, one native
capacity bank and direct field-copy helpers. No per-site lookup, callback,
formatting, hash, duration arithmetic, metric Value or dynamic event aggregate.
Descriptions are static/setup-only. Emission restores caller errno. Native
clock fields copy method/result/errno/tv_sec/tv_nsec and acquisition presence;
failed readings are described as native failed acquisitions, never valid zero
timestamps. All native structs are emitted memberwise, without padding/pointers.

The first safe boundary claims the whole finite CONTROL bank, then establishes a
fully live guard BEFORE2400/name DATA/preflight. Once BEGIN is published, every
exit has a prepaid terminal. Claim failure before BEGIN has no invented owner
event; existing recorder-ingest failures remain the raw transport's responsibility.
Name DATA uses checked48+actual name length: no arbitrary32/256 label limit.
The current inherited RawWaitScope remains in force; no timer, fd duplication,
new cancellation polling callback or wait under a product/spawn mutex.

Nested operations borrow the explicit current synchronous operation R through a
private CodingEngine pointer restored by a stack scope. Outermost request uses an
actual provider parent or turn R when passed/available, otherwise parent absent.
There is no nearest-event inference and no cross-thread pointer transfer. Parent
and claims stay on the issuer lane. Bind the actual command-admission1060 R as a
separate link; root's task1915/activity32 keep that exact existing parent and their
own349/late132 credits. Do not duplicate task state, notifications or map ownership.

Input claims49+ValueLimits.bytes before the existing input encoder, publishes the
returned vector, then moves that vector into the existing ImmutableBytes. This
removes the unnecessary intermediate string/copy. Release the unused input grant
before acquiring the result grant. Result claims49+ValueLimits.bytes BEFORE the
actual retained admission append/effect, and stays held through reentry, native
effect and encoding. No result DATA wait occurs after the effect. Smaller actual
outputs publish exact size and release the unused grant immediately. Owner-native
maximum-grant checks refuse impossible nested capacity before waiting.

For the selected provider result, keep its existing phase1320/1321 and ONE1322
upstream_packet body, but publish directly from the existing encoded vector using
the preclaimed result bank before its required business string conversion. Its
actual offset49/source Ref becomes provider_operation.result; operation2409 only
references that same original. Remove the current no-receiver result lease/string
wrapper from this block rather than making a second full body or new storage
service. Generic results use2402 instead. Provider/colleague/participant upstream
originals remain their own different actual business acquisitions. nls.10 tickets
and shared storage are not involved in this owner-only final result publication.

## Memberwise native layouts and finite banks

E=raw_envelope_size16, R=lane16+ordinal:u64(24), Error=ErrorCode:u8+i64(9),
ID=16, Cursor=journal16+sequence:u64+end_offset:u64(32), SourceReference=24.
Clock=2*(clockid_t+2*int+time_t+long)+bool=57 on qualified64-bit Mac/Linux.
Bytes lengths/counts use actual size_t; descriptions name exact native ABI.
OriginalReference=R+RawPartition:u32+record_offset:size_t+bytes:size_t=44.
Credits use sizeof constants in source and checked addition, not padded sizeof of
the enclosing C++ types. R in each body is the exact whole-operation request.

|Site|Body and actual location|Maximum record bytes|
|---|---|---|
|2400 request|bool parent,[R],actual generation ID,operation_depth size_t,pumping_commands bool,capacity_stopped bool,Clock; before preflight|124 CONTROL|
|2401 name|R,size_t length,existing exact name; before name copy/preflight|48+actual length DATA|
|2402 original|R,u8 input/primary/fallback/capacity-message,size_t length,exact successful existing encoded vector or existing message|49+actual bytes; normal DATA, bounded fallback/message CONTROL|
|2403 identity|R,u8 decision/invocation/attempt/admitted-generation/context,actual ID; immediately after its business acquisition|57 CONTROL, at most5|
|2404 admission request|R,u8 admitted-batch,actual expected Cursor,actual events.size(); before append|81 CONTROL|
|2405 admission return|R,u8 kind,u8 returned/Error/alloc/length/std/nonstd,[actual Cursor or native Error]; before later work|74 CONTROL|
|2406 effect request|R,actual EffectIntent environment/actor/invocation/attempt IDs,size_t input length; at actual Boundary entry|112 CONTROL|
|2407 effect return|R,u8 native returned/Error/alloc/length/std/nonstd,bool Error,[actual Error]; before preparation/encoding|51 CONTROL|
|2408 dispatch report|R,bool dispatched,bool effect_ok,bool recording_ok,[failed native effect Error],[failed native recording Error]; actual returned report|61 CONTROL|
|2409 source/region link|R,u8 input/primary/fallback/message/command-admission,bool present,[actual OriginalReference or R]; after publication|86 CONTROL; four original links+one66 region link|
|2410 retention request|R,u8 result/capacity-message,actual ApplicationRecordId,actual expected Cursor; before original retention|89 CONTROL, at most2|
|2411 retention return|R,u8 kind,u8 actual branch,[actual SourceReference or native Error]; before hex/presentation|66 CONTROL, at most2|
|2412 disposition|R,actual AttemptDisposition,actual timed_out/persistent_command/adapter_capacity bools,bool native Error,[Error]; after existing business branch selection|54 CONTROL|
|2413 settlement request|R,bool settlement,bool fallback,actual attempt ID,AttemptPhase:u8,AttemptDisposition:u8,size_t observation bytes; before actual submit|68 CONTROL, at most2|
|2414 settlement return|R,u8 branch,[actual Submission existing bool,RecordReference24,RetainedEvidence:u8 OR Error]; before subsequent work|67 CONTROL, at most2|
|2415 callback request|R,u8 existing cancelled/effect_policy/started/status/outcome/completed/process_output,bool present|42 CONTROL, at most7|
|2416 callback return|R,u8 kind,u8 native absent/returned/Error/alloc/length/std/nonstd,[actual cancelled bool on its return OR native Error]; actual return/throw|51 CONTROL, at most7|
|2417 terminal|R,u8 returned/refused/Error/alloc/length/std/nonstd,u8 reached stage,bool Error,[Error],bool actual disposition,[AttemptDisposition:u8],Clock|111 CONTROL|
|2418 encoder return|R,u8 input/primary/fallback,u8 actual branch,[size_t actual encoded bytes OR native Error]; after immediate body publication or actual error|51 CONTROL, at most3|
|2419 encoder request|R,u8 role,actual ValueLimits bytes/nodes/depth; before existing encoder|65 CONTROL, at most3|

2412 maximum is16+24+1+3+1+9=54 (not a larger guessed54-byte payload).
The selected codecs' current bytes bound is16MiB, nodes500000, depth128; this
grounds the result/input grants in actual source. No second encoding establishes
a bound. A failed encoder has a native error return and no fabricated full body.

Fallback error_value contains two fields error/detail. Current longest error_name
is19 bytes; BBM2 is4 magic +2 object header +7 error key +21 error text +8 detail
key +11 signed-integer tag/varint =53 bytes. Therefore the one existing fallback
encode gets102 CONTROL (49+53), paid before effect; it never waits on DATA after
capture refusal. The existing capacity.stop literal is90 bytes, so its original
costs139 CONTROL. No encoder is added for that literal. Two possible business
retention calls are result and that existing capacity message. A terminal-capacity
fallback changes the existing returned Value but does not introduce a second
output encoder which the old operation did not run.

The fixed owner credit is3183:
124+5*57+(81+74)+(112+51)+61+(4*86+66)+2*(89+66)+54+
2*(68+67)+7*(42+51)+111+3*(65+51)+102+139.
Descriptions/source use this exact sizeof-derived sum; inactive variants omit
fields and release unused credit. The provider selected result additionally
keeps its existing phase/link credit206, not a second operation bank.

The selected retained_append bank is computed from actual immutable journal limits
and bounded operation proposals BEFORE the effect. The original13*432 plan was
incorrect: retain_rejection creates diagnostic chunk/group/marker appends, and a
single region can emit more than three source/event observations. ONE plan attack
finding O1 replaces that fixed bank, rather than adding a second reserve or review.
The owner record credit3183 is unchanged; whole CONTROL is3183+A below, with the
provider's existing206 additional. Claims and raw-current-thread lifetime precede
publication and outlive their last uses. Nested or unusually small-payload journals
may require more finite CONTROL than a configured lane has; refuse before effect,
never saturate accounting, truncate observations or shrink business encoder limits.

### O1: complete selected append and rejection credit

For one entered retained_append with actual S sources and E events, the maximum
native region credit is R(S,E)=276+52*(S+E): begin77, end86, one outcome28, one
error33, journal observation52, and one52-byte observation per source/encoded event.
Even a partially encoded/refused call fits this bound. Retained append supplies
these existing span counts to RawRegionOptions; when borrowing a bank, its
constructor splits that exact checked amount, not a fixed432. The caller's
existing RawRegionPrepaidScope reference API is unchanged. Other region sites and
record bodies remain unchanged; there is no extra registry/aggregate/metric walk.

R(0,3)=432 for admission, R(0,1)=328 for issuer/open/receipt/terminal,
R(1,1)=380 for original retention. A diagnostic source group with k chunks needs
R(k,0); its marker needs R(0,1); a combined diagnostic append needs R(C,1).
All these splits consume one generalized held CONTROL claim without admission,
allocation or wait. Unused per-region credit is released when that region ends.

Let B=ValueLimits.bytes (currently16MiB), M=journal max_payload,
N=journal max_batch_bytes and J=immutable max_file_bytes. Read M/N from existing
journal_limits. Add a trivial native journal_capacity() const noexcept returning
the already owned capacity_ so credit preparation does not perform a new fstat,
query, callback or encode. These are capacity calculations, not emitted metrics.
Every add/multiply/ceil division is checked; overflow refuses before effect.

retained_proposal.cpp uses48 fixed ARPROP01 bytes plus4 length bytes for each
source and encoded event. Retained event framing is8 bytes, dependency references
24 each. The smallest empty rejection marker is20 bytes (header8 + reason12);
an empty identity-conflict marker is30. Hence every successful rejection proposal
is bounded by L=min(J,M*floor((M-20)/24)); using20 is a conservative upper bound
for both actual marker variants, and no marker is encoded just for measurement.
If L<48, proposal encoding cannot reach any diagnostic child append.

|Selected action|Maximum direct calls|S,E|Maximum ARPROP01 Q before rejection limit|
|---|---|---|---|
|Issuer reservations (initial three IDs, result ID, capacity-message ID)|5|0,1|68:48+4+16-byte retained issuer event|
|Admitted decision/invocation/attempt batch|1|0,3|60+min(M,B+128)+min(M,B+60)+min(M,56)|
|Attempt open|1|0,1|76:48+4+24-byte retained open event|
|Adapter receipt|1|0,1|88:48+4+36-byte retained receipt event|
|Result original|1|1,1|56+B+min(M,B+54)|
|Existing capacity-stop original|1|1,1|56+90+min(M,129)|
|Normal, fallback and interrupted terminal (conservative exclusive paths)|3|0,1|52+min(M,112)|

Decision contains one planned invocation: fixed retained128 plus its existing
bounded continuation packet. Invocation is fixed60 plus its existing bounded
input packet. input_from_invocation admission is fixed56 and does not encode a
second full input. Result original has one dependency, so retained framing54
plus its bounded semantic metadata packet. Capacity metadata's two fixed fields
produce at most75 packet bytes, giving event129; this is derived from existing
field/tag/native-u64 bounds, not a new encode or copied cache. Terminal observation
is at most80 bytes: max(existing exec-interrupted literals60/80, capacity literal56,
bounded error packet53, result locator packet38), plus retained event32 gives112.
The minima with M bound successful event encodes; an oversized event returns
before reject chunking and cannot consume the hypothetical nested credit.

For each action independently set q=min(Q,L). If q<48, diagnostic credit D=0.
Otherwise C=ceil(q/M). Native valid_limits guarantees N>=M+88, therefore
K=floor((N-56)/(M+32))>=1 complete chunks fit one source group. The actual greedy
group loop has at most G=ceil(C/K) groups: all chunks except the last are M bytes,
and the shorter last chunk cannot create more groups. Diagnostic=true child calls
never call retain_rejection again, so this is one bounded level, not an unbounded
recursive chain.

If q+56*C+118<=N, every possible full proposal+source frames+largest30+24*C
marker+marker frame+commit fits together; reserve D=R(C,1). Otherwise reserve
D=276*(G+1)+52*(C+1). This covers all source groups plus one marker, including a
smaller actual proposal that happens to fit together. Actual entered regions
split their actual R(S,E), not C/G maxima. No instrumentation simulates the group
loop after effect or reparses/encodes the proposal. The already existing business
groups and source spans supply the actual counts.

The complete bank A is the sum of count*(R(S,E)+D) for the seven table rows.
It includes all thirteen direct upper calls AND all possible diagnostic child
regions, all their source/event observations, error/outcome/end records and
partially written/rejected prefix paths. Before-effect budgets use immutable
limits; later native current remaining journal space can only lower actual work.
For example M256/N344/J100000 gives A13848, whole generic17031; M1MiB/N(M+88)/
J1e9 gives A16196, whole19379. These illustrate the arithmetic, not new source
constants or changed journal defaults. Physical capacity refusal remains native.

### Direct loan isolation and maintenance

At append_impl entry capture the current borrowed prepaid-bank pointer, construct
the direct RawRegion with actual S/E credit, then immediately suspend the existing
TLS loan for the remaining business append. One pointer-taking
RawRegionPrepaidScope(nullptr) (reference constructor delegates) supplies this
pause; it restores the previous pointer at scope end. No second TLS registry,
shared ownership, callback or claim state is introduced.

Pass that captured borrowed bank explicitly into private retain_rejection. Each
actual diagnostic child append temporarily reinstalls this loan for entry only;
its own append entry consumes actual R(S,E) and immediately pauses again. It does
not obtain new credit or wait. The outer region remains alive on its independent
split claim until its own actual rejected return/end. Retained event record
encoding already runs once as business work; the credit helper never repeats it.

Maintenance_checkpoint and all native storage/custody callbacks run with this
caller loan paused. The actual Context checkpoint receives only the existing
parent R and its own origin/bank; absent retained_regions cannot inherit the
operation bank. A child that owns an explicit different bank may scope that bank
normally and restores null afterward. Boundary restores receipt scope before
the actual body; each issuer/retention/settlement scope covers only its direct
append tree. An arbitrary adapter issuing journal calls cannot consume caller
credit. Replace exec's current overlapping caller bank with this operation bank;
CommandJobs' later joined-worker and Context/participant budgets stay owned by
their respective units. Existing owners' own rejection budgets are not silently
certified by this operation calculation.

## Retention, cancellation and return paths

Use typed AuditLog::original(actual issued ApplicationRecordId,capture), which
returns the actual SourceReference. Emit that native return before hex(result_id)
or allocating a locator. The result original is already raw-published; metadata
construction, issue refill, encoding its retention packet, journal capacity,
native journal partial I/O and hex/locator allocation can fail without losing it.
Pass no new metric metadata; existing operation/attempt/source_encoding semantic
metadata stays, while the legacy time/duration projections are removed.

After Boundary returns, copy both actual DispatchReport effect/recording results
before any unwrap. Recording failure is reported/propagated separately; it cannot
erase the raw original or fabricate an accepted semantic record. Preserve the
current no-replay behavior. Retention capacity still runs existing capacity stop,
command shutdown and bounded error return, using held body/outcome/append credit.
Other retention failures retain their original exception and source Ref. All
post-effect source publication is from already held DATA; cleanup records use
held CONTROL even with sticky cancellation/known fence. A semantic append under
cancellation may itself return interrupted; raw return/terminal describe that
actual outcome rather than resetting cancellation or pretending settlement.

Terminal describes the actual caller result/throw, with reached stage and optional
already selected disposition. It is emitted after completion/presentation callback
return when those actually run. A callback exception is its own actual branch,
with the already preserved result still referenced. Destruction fallback means
unfinished scope/actual unwind, never inferred successful effect or journal
acceptance. Effect returned, initial operation returned, persistent job completed
and task finished remain distinct facts. poll_commands and CommandJobs own later
job exits; no whole-operation duration extends to a background job's lifetime.

## Independent red and green oracles

No source before root's ONE plan attack. Baselines use the current public APIs;
do not disable new hooks or fabricate a library outcome. Add one native operation
fixture with literal BBM2 bodies and memberwise expected fields, real runtime and
native journal, using genuine allocator/native I/O interposition that forwards
all unselected calls. Expected packet bodies are built from the documented tags
and independent literal fields, never the production encoder under test.

* Existing nonprovider write_file: target rename completes, actual file equals
  the literal input, then forward a short native receipt pwrite followed by EIO.
  Arm at the actual selected business rename, not an assumed allocation ordinal.
  On baseline no generic whole request/original; on correction exact input/result
  originals, native success effect, actual receipt EIO, truthful caller Error and
  no successful result-retention/terminal fabrication. Do not treat rename as the
  adapter return; actual Boundary entry/return fields establish that return.
* Actual encoded-vector conversion: fixed allocator observer finds the complete
  independently expected BBM2 result in a live native allocation, then refuses
  the real next allocation. Verify the external file effect and actual acquired
  vector, then exact raw original survives the failed string/metadata work.
  Provider counterpart gates acquisition watching on its actual existing kind9
  phase in the published native lane, using a setup-mapped source probe; it does
  not add a production callback. Check ONE1322 result and its exact receipt Ref,
  no duplicate2402 full provider body. Genuine source gates/logs precede green.
* Native body Error/alloc/length/std/nonstd variants via actual custom Lua/tool or
  owned adapter callbacks; independent error/detail/disposition expectations,
  one effect return/receipt and one whole terminal. Oversize/depth codec refusal
  has actual Error and no fake successful original. Nested operations use real
  parent R and independent IDs; passed task IDs keep actual1060/task ownership.
* Real native semantic retention capacity after effect: preserve complete result,
  bounded53-byte maximum error codec/source, actual capacity stop90-byte original,
  attempted settlement outcomes and no success on refusal. Native journal EIO,
  result-identity/locator allocation and actual presentation callback throw each
  retain the earlier exact original and actual native failure branch.
* Stop the real recorder, fill DATA then execute sticky cancellation and deadline
  admission at name/input/result preeffect boundaries. No effect occurs; begun
  owner gets prepaid terminal. Cancel after effect but before receipt/result
  retention: original was already published, held cleanup works, no reset or
  fabricated accepted journal receipt. Check real cursor/bytes and original Ref.
* Native clock interposition forwards actual clock_gettime and records the exact
  returns independently. Compare both request/end raw field sequences including
  one genuine injected native clock failure. Assert no time/duration metadata or
  new metric channel emission; only an enabled human callback receives its existing
  elapsed presentation. No elapsed/throughput timer promise is tested.
* Persistent exec returns backgrounded with no fabricated exit; actual later
  CommandJobs exit/task notifications stay in their owners. Initial result source
  and operation terminal remain immutable through owner drain. Raw-inactive mode
  preserves existing business return with absent raw Ref, never all-zero custody.
* Genuine producer death after raw result publication but before semantic receipt:
  recover actual unacknowledged P>A prefix and compare exact original/Refs; do not
  invent an owner completion. Root recorder qualification supplies transport
  failure coverage, this oracle targets the actual operation publication cut.

O1 adds three actual retained-rejection/scope oracles to this SAME unit, before
source. They use native journals/current public append APIs and exact independent
literal sources; no synthetic disabled region or fabricated rejection return:

* A genuine stale original proposal with M4096/N32768, three diagnostic source
  chunks and marker together: compare the complete captured literal proposal,
  actual refs/region observations and every end. The combined child needs
  R(3,1)=484, so the old432 split cannot publish its full selected burst.
* M1024/N1112 with a genuine stale28000-byte source and small valid event creates
  roughly28 actual source groups and one marker (derive exact Q/count from the
  independent literal event). This genuinely exceeds thirteen business regions.
  New preeffect bounded credit covers all actual regions; native group write/
  synchronization failure after an accepted prefix preserves that exact prefix
  and real error/end, with no fabricated accepted marker. Check total native
  observation/end counts and parents against actual source counts/group size,
  not physical DATA/CONTROL disk order.
* Actual compact maintenance invokes the existing Context checkpoint owner while
  a selected append loan is installed. Check the caller bank's remaining bytes
  change only by its own direct region (and rejection children if any), not by
  maintenance Context regions or native callback reentry. The child sees the real
  parent R and its own credit; its return/throw restores the suspended selection.
  Sticky cancellation during child work must not steal or wait on caller terminal
  credit. A separate explicit child loan is restored to null after child unwind.

Direct exact-bound arithmetic cases cover no-marker-reference M, joint versus
split batches, one partial last chunk, checked overflow and configured capacity
refusal BEFORE any effect. Test expected constants derive from native framing
and independent source limits; they do not mirror a helper by calling that helper
to compute its own expected answer. Source/native fixture greens and root's ONE
code attack remain after release; this is plan resolution, not a passed-test claim.

Qualification uses one compiler/static worker and affected operation/provider/
command/participant native checks. Root owns supported debug/release/ASan profiles,
ONE code/oracle attack, finding recheck and integrated gate. No evals, dependency,
mutants, new timer, second panel or nls.2/nls.10 completion claim.

Root's ONE whole-plan attack accepted the complete O1 resolution after reading
actual ARPROP48/framing, codecs, valid journal limits and all selected operation
original/locator/fallback/capacity branches. Variable topology prepayment and
direct loan suspension resolve the fixed-bank finding. Genuine baseline reds
may proceed; production coding/local_timing/retained_state changes await the
Context/turn attack and source checkpoint. No additional plan panel is needed.
