# Native program and callable workflow owner raw emission

2026-10-13. Written whole-owner subplan, awaiting root's ONE plan attack before
production code or builds. The authorized scope ends before workload qualification.
This completes native program/module registry validation and staging, Lua tool
definition validation/staging/invocation, callable workflow selection/deferral/
execution and the existing default programs' authored control decisions. It does
not claim that a VM begin/end or an Operation17 result supplies these lifetimes.

## Boundaries and existing evidence

Read current doctrine, bounded-hardening instructions, raw instrumentation and
completion census, whole workflows.hpp/cpp, the actual Coding program/tool/
workflow/activation/runtime methods, program_config/lua_tools/workflows and
workflow_repair fixture source, the existing turn owner plan, workflow design
study and W1 independent review. Revisited the existing old Unix emission and
transport studies. No new literature acquisition or reference execution.

Existing grounding is `papers/2026-09-30-workflow-design-study.md`,
`papers/workflows-2026-10-07/hermes-study.md`,
`papers/workflows-2026-10-07/w1-independent-review.md` and
`papers/2026-10-01-lua-embedding-grounding.md`. Directly reread pinned Hermes
`tools/delegate_tool_dispatch.py:110–172` and community `wfcommon.py:36–90`,
and Lua5.5.1 `ldo.c:69–108` and `lapi.c:1076–1111` from quarantine. These are
study references, not dependencies or a claim that Blackbird uses their scheduler.
Also read the actual qualified Linux5.4.8 `lapi.c:526–536,1043–1091` under
`quarantine/resource-sampler-2026-10-11/lua-5.4.8/src/`: pushlstring acquires a
Lua string/GC before returning; conventional pcall invokes luaD_pcall and returns
the acquired int status. The5.5.1 control-transfer study is not a new runtime
adoption or an asserted5.4/5.5 ABI equivalence.

Hermes separates completed child records from group completion and reports
uncompleted work separately on interrupt. Its best-effort record swallowing is
not our lossless emission policy. Its graph hashes normalize definitions and
invalidate ancestors; none of that processing belongs in Blackbird's writer.
W1's previous concrete defects establish the actual oracles: caught invocation65
must still latch failure, and immutable catalog lifetime does not make a stale
UI selection index valid. Current source already repairs both. The workflow
study's earlier next-decision activation proposal is superseded by today's
successful-workflow-boundary rule. Lua source establishes that allocation/error
transfer can skip C++ automatic owners in a C-built runtime; argument preparation
before a protected call cannot be presumed safe merely because evaluation later
uses lua_pcall.

This is census18–21 plus default control-program authored use22. Rows13–15 and17
are existing turn/source/VM/operation owners and remain in place. Row16 bridge
projection and SystemA native/Lua/value/codec representation require a separate
cohesive boundary owner (proposed4800–4999), described below. Row56 candidate is
the separate blackbird-candidate CLI's git/worktree/file/lease lifecycle, not
program_config. Row59 backstop owns successor creation, provider-only assessment,
artifact/pivot/quiescence and uses4700–4799. Neither is silently assigned here.
Session/UI owner owns mutex-protected displayed-registry publication and color/
palette consumers; this plan covers the immutable registry they receive.

## Current business behavior to preserve

`CodingEngine::program_config` validates complete proposal/CAS, modules and
workflow definitions, compiles candidates without executing them, issues a real
DefinitionGenerationId, constructs the final immutable WorkflowRegistry and
publishes pending fields. `define_tool` validates name/schema/source, constructs
the actual replacement array, issues a generation and publishes pending tools.
Both build returned Values after staging: returned-Value allocation failure
does not prove staging was undone. Repeated staging supersedes the previous
pending generation, not the effective generation. Effective config/tool revisions
remain native textual business strings; never hex-decode them for instrumentation.

Current accepted limits: modules32, module name64/source16384, model256;
workflow definitions32, name64/description256/source16384, aliases1..8,
powerwords0..8/token64; tools32, name64/description4096/source16384;
flat schema properties64 with key128, descriptions4096. Whole proposal,
workflow definitions and replacement tool array each must encode to at most65536
business BBM bytes. Preserve unknown-field, wrong-native-type, conflict and
syntax failure classes and the existing short-circuit validation order.

`WorkflowRegistry::select` examines actual unrendered operator text: exact
slash/bare alias wins; unknown slash never triggers a powerword; byte>=128 is a
word constituent; different selected definitions conflict. Arguments retain the
first whitespace and the full exact prompt remains a context entry. Explicit
workflow_invoke uses named definition selection, independently of operator matching.

`workflow_invoke` copies selected definition/arguments/revision and actual
selection-attempt text, records selection originals, then computes unanswered
provider outputs. A nonempty set queues at most8 continuations. The queue is
in-process, sequential and discarded on turn exit. Drain validates protocol,
refuses restart/cancel, moves the first continuation, erases it, executes it and
appends its real result. Execution has depth8/count64 guards inside the existing
failure-latching try; the count increments only when depth did not short-circuit.
Execution creates a scoped Runtime, retains the actual selected source and uses
Operation17 for effect/input/output linkage. It does not replay after failure.

ContextStore's accepted boundary remains the activation authority. The existing
noexcept swaps in turn's apply_activation and existing1905 observations already
record actual activation; new stage-lifetime terminal records link those records.
Even if a later callback throws, an accepted boundary and applied swap remain
effective. Turn clear_pending/Running cleanup discard pending/queued state without
claiming that earlier file/provider/command effects were undone.

## Explicit ownership and narrow source interfaces

Reserve native3800–3879 and authored domain integers3880–3899. Leave3900–3999
available to a separately planned candidate owner. Add owned raw_program.hpp/cpp:
literal site descriptions and immediate capture only; no Value, encoder, metric
dictionary, formatting, duration, filter, callback or new traversal in emitters.

Every selected business entry receives an explicit `RawProgramContext` containing
optional parent RawEventRef and an optional existing generation native16. Null
context means opt-out for direct WorkflowRegistry calls. Coding supplies actual
operation_trace_ for operation entry, turn_trace_ for drain, and an explicit
bootstrap owner for constructor restoration. A nonzero supplied parent must
belong to the actual active issuer. Never guess a parent from producer TLS or
nearest ordinal. Bootstrap has its own native request with actual engine
generation and no invented predecessor. Raw producer activity alone does not
select arbitrary standalone registries.

Lexical `RawProgramCall` owns request and maximum terminal credit before its
business body, uses a native templated runner to classify Error/bad_alloc/
length_error/standard/nonstandard throws and emits after local cleanup. It
borrows no Operation17/retained settlement bank. A move-only inline
`RawPendingProgram` owns the separately paid generation lifecycle; the engine
has one optional for pending program and one for pending tools. A move-only
`RawQueuedWorkflow` lives in each actual WorkflowContinuation. These are
instrumentation lifetime holders, not extra business queues/counters or mutable
shadow configurations. They retain their immutable RawThreadHandle/issuer and
held credit, not a borrowed stack trace pointer after operation return.

Registry constructor receives optional context; its function-try-block must
report native bad_variant_access as the existing converted invalid_range and
report actual allocation failure separately. Avoid a member trace whose ordinary
destructor fires before the constructor's exception conversion; use a delegating
factory/body runner or a constructor-owned state which the handler can close.
Coding bootstrap restore encloses actual current_programs-derived replacement
and final registry construction. Restored records remain effective-only; no
pending state or continuation is reconstructed or executed.

Change RawTurnTrace::activation's return type to RawEventRef, preserving1905's
body and its existing held credit. Its caller already has the actual published
ordinal; return it without a new event or counter. RawPendingProgram closes
activated immediately after the actual swap/1905, with an optional actual1905
Ref. If1905 publication fences emission, omit the missing Ref; never fabricate
an activation ordinal. Coordinate this narrow shared header/source change with root.

## Native schema and byte credits

E16; R24=lane16/u64 ordinal; G16 existing native generation; Text=size_t+bytes.
Current profiles: bool1, ErrorCode1, u8/u641/8, size_t8, unsigned4, int4.
Field descriptions state actual widths; no object padding. The generic terminal
is R, RawOperationBranch:u8, stage:u8, ErrorPresent:bool,[ErrorCode,i64], maximum52.
All emitted booleans/counts are actual maintained business values or actual
returned predicates, not inferred semantic success.

|Site|Exact body and actual boundary|Maximum total|
|---|---|---|
|3800 call request|method:u8, parentPresent:bool,[R], generationPresent:bool,[G]; before actual business work|59|
|3801 call terminal|generic terminal; after body/result construction and cleanup|52|
|3802 current revision|call R, kind:u8(program0/tools1/workflows2),Text exact existing revision; prepay before any business copy of it|49+N|
|3803 business encoded original|call R, kind:u8(proposal0/definitions1/tools2),size_t N, exact existing BBM bytes|49+N DATA|
|3804 encoder request|call R,kind:u8,actual existing ValueLimits bytes/nodes/depth:size_t|65|
|3805 encoder terminal|generic terminal,[bool Result.has_value only if actual Result returned]|53|
|3806 existing validation loop state|call R,kind:u8(module0/workflow1/tool2/schema3),size_t existing container size,indexPresent:bool,[size_t actual existing index]; at reached existing iteration only|58 maximum|
|3807 syntax request|call R,kind:u8(module0/workflow1/tool2),size_t actual source size|49|
|3808 syntax terminal|generic terminal,[bool valid only on actual return]|53|
|3809 native definition generation|call R,kind:u8(program0/tools1),G; after existing issue returns, before existing hex presentation|57|
|3810 select request|parentPresent:bool,[R], Text exact revision,size_t actual input length; before select/named copy|57+revision N|
|3811 selection original|selection R,size_t N, exact existing text/name bytes|48+N DATA|
|3812 selection decision|selection R,route:u8(none0/alias1/powerword2/named3),indexPresent:bool,[size_t actual definition index]; before definition/argument copies|50 maximum|
|3813 selection terminal|generic terminal, selectedPresent:bool; true only after actual optional/borrowed result acquisition|53|
|3814 selected trigger|selection R,size_t N,actual copied trigger bytes; operator select only|48+N DATA|
|3815 discovery/module lookup request|call R,kind:u8(workflows0/tools1/module2/request-tools3); before actual output construction|41|
|3816 lookup terminal|generic terminal|52|
|3817 actual lookup name|lookup R,size_t N, original existing module name bytes|48+N DATA|
|3820 workflow execution request|selection R, G, unsigned current depth/count; before guard/counter/operation admission|64|
|3821 workflow execution terminal|generic terminal,unsigned current depth/count; after child Runtime/Depth cleanup|60|
|3822 actual execution attempt|execution R, native attempt16/invocation16/generation16; at actual Operation17 body entry|88|
|3823 execution source association|execution R,Operation17 R,actual selected revision Text|72+N|
|3824 execution guard state|execution R,unsigned actual depth/count; after reached guard state, before throw/dispatch|48|
|3825 drain request|turn R,bool draining,size_t actual queue size; before protocol validation|49|
|3826 drain terminal|generic terminal|52|
|3827 Lua tool invocation request|Operation17 R,Text effective tools revision,size_t actual source length; before schema/invoke|56+revision N|
|3828 Lua tool invocation terminal|generic terminal; after actual native result construction|52|
|3830 pending generation request|call R,kind:u8(program0/tools1),G; before textual revision allocation/final publication|57|
|3831 pending installed|pending R,Text actual pending revision; immediately after native pending transfers|80 for revision32|
|3832 pending lifecycle terminal|pending R,reason:u8,activationPresent:bool,[1905 R],ErrorPresent:bool,[ErrorCode,i64]|76 maximum|
|3833 queue request|selection R,size_t actual queue size; before actual parked copy/push|48|
|3834 queue installed|queue R,size_t actual queue size; immediately after successful push|48|
|3835 queue terminal|queue R,RawOperationBranch:u8,reason:u8,executionPresent:bool,[3820 R],ErrorPresent:bool,[ErrorCode,i64]|77 maximum|

Methods3800: bootstrap0, program-read1, program-propose2, tool-read3,
tool-propose4, registry-build5. Stages:1native arguments/CAS,2bounded business
encoding,3existing per-definition validation,4syntax,5actual issue/revision,
6pending construction,7pending transfer,8returned Value. Syntax native VM1506/
1507 remains the source/load-status owner;3807/3808 records the semantic
validation request and actual bool, not a second source body.

Pending reasons: activated0, superseded1, discarded-turn-exit2,
failed-before-install3, destructor-unfinished4. Queue reasons: dequeued0,
discarded-turn-exit1, push-failed2, destructor-unfinished3. None says cancelled
unless an actual Error::interrupted was observed at that owner's boundary.

Plain3800+3801=111 maximum. Syntax pair=102; encoder pair=118 with extension
prepaid before encoder call. Pending lifetime reserve=57+80+76=213 per actual
replacement, claimed before issue/publication; unused3831 credit is released
on failed-before-install. Queue reserve=48+48+77=173 per parked continuation,
maximum eight currently resident claims (1384); each can drain or clear through
sticky cancellation/full ordinary CTRL without a new claim. Execution owns
64+60+88+48 plus3823's actual revision allocation bound before effect, i.e.
332+revision N at most364 for generated32-byte revisions. Returned-only events
are omitted if not acquired; reserve unused portions, never emit fabricated values.

WorkflowRegistry already maintains its native size_t index. Module/tool/schema
range loops do not:3806 omits the index for those loops rather than introducing
an instrumentation counter. Capture their actual container size at the reached
business iteration. Repeated native events and their ordinals provide sequence;
there is no additional metrics traversal or enumeration state.

### Original body acquisition and existing encoder bound

The three business encode_packet_string calls currently use default16MiB and
only afterward compare the successful body with65536. A49+65536 DATA claim
would not preserve an acquired oversized body under that ordering. Refactor
those same business encoders to use `{65536,500000,128}` (the existing acceptance
limit), retain their actual Result, emit3803 on actual success BEFORE comparison/
discard, and emit3805's actual returned has_value/error before unwrap. This is
deliberate bounded business preparation, not an added metric serializer. Accepted
BBM bytes remain exactly unchanged; oversize fails capacity before acquiring a
complete oversized encoded result. Wrong native structure still reaches the
existing native error conversion; do not rearrange type checks casually.

Reserve49+65536=65585 DATA before each existing encoder invocation. Registry
construction validates twice during program proposal (candidate/final): both
real encoders remain real, each acquired body is published once, not deduplicated
from presumed value equality. Do not add a proposal encoder to read-only paths.
Input/output Operation17 originals remain separately retained and provide rejected
input bytes when no complete encoded body was acquired.

Selection input is already a borrowed immutable native view. Pay48+actual input
size with checked arithmetic before select's native copies; publish original
input immediately, then record actual route/index before optional selection
construction. Alias trigger has at most129 bytes (`/`+prefix64+alias64);
powerword trigger has at most64. Hold48+129 for the actual copied trigger before
copying, publish only after success. Named lookups capture their borrowed name
with actual length, not a guessed64 bound before rejection. No metrics formatting
or additional matcher pass. Current index exists for alias/powerword; named loop
may expose its ordinary iteration index as actual business traversal state.

### Staging, queue and stop ordering

Construct the new pending lifetime holder before issuing its generation. Open
3830 after the real issue returns, before actual hex_identity can allocate; on
failure before transfer close failed-before-install. Build the new registry and
all revision/Value state before touching old pending holders. Close an existing
pending holder superseded immediately at the actual replacement boundary, then
transfer new pending fields and install the new holder with no throw. Result
construction failure after transfer leaves it pending;3801 reports the thrown
branch independently. Turn clear_pending closes actual still-pending holders
discarded-turn-exit before destroying fields, never the accepted/activated one.

Make WorkflowContinuation's actual parked token move-only and noexcept movable.
Selection request survives by Ref, not pointer. Queue push reserves before the
fallible push and reports push-failed if it never installed; accepted count is
observed only afterward. Drain moves/erases the continuation using the existing
business queue; execution emits3820 before guards and then closes its parked
holder dequeued with that actual Ref. A guard failure still has an execution
request/terminal and does not become a fabricated child execution. Running
cleanup closes every genuinely parked holder discarded-turn-exit via its own
held claim; the existing clear traversal is reused, no separate metrics walker.

Guard-state3824 preserves short-circuit behavior: depth8 does not increment
count. If depth permits, increment the existing count once, emit acquired
state before capacity throw, then dispatch. Catching the Lua bridge error still
sets the existing workflow_failure_ latch. Nested executions are actual spans,
not another invocation counter or a guessed native result from error Values.

## Protected Lua invocation preparation

Do not introduce RawProgramCall automatic objects across unprotected allocating
Lua APIs. Runtime::eval currently prepares/restores `_native_args` with
push_value/getglobal/setglobal before/around the later protected load/call.
For native tool/workflow calls, introduce a narrow protected preparation shim
owned by Runtime: store immutable argument/source pointers and preparation state
in already-live Runtime fields, prepare/restore Lua arguments through lua_pcall
on a static native C closure, and convert its acquired status after that call
returns. The closure has no owning automatic C++ objects across Lua allocation/
error transfer. Existing raw VM install/load/call hooks remain; no catch-all may
intercept Lua's control transfer. The native program invocation owner sits
OUTSIDE these protected calls and can safely close after their actual statuses,
or after ordinary C++ errors while constructing the existing source wrapper.

Argument restoration must itself use the same protected boundary and preserve
the primary invocation error if restoration also fails. Keep actual Lua stack
base/rooted previous `_native_args` until real restoration or Runtime destruction;
never fabricate restored state. The new shim is specifically needed to make
this invocation owner's lifetime safe and is part of this unit's direct fault
oracle. It is not a broad bridge/codec refit. Existing read_value/push_value
representation event coverage remains the separately planned SystemA owner.

## Default authored events without new metric state

Use existing blackbird.emit(site,value), not a new Lua emitter or bridge registry.
Add identical semantic sites to programs/turn.lua and default_workflows' actual
embedded ultracode source:3880=existing step lua_Integer at loop head;
3881=existing calls lua_Integer after the actual response traversal;
3882=actual restarting boolean. Evaluate blackbird.restarting exactly once as
business control (store its returned bool and use it in the original condition),
then emit that native bool before the return/continue branch. Do not add another
counter, sum, output projection or formatted body. VM terminal covers errors or
the existing64-step budget failure; no synthetic successful-loop terminal.
Authored1510/1512 envelopes already include native generation, VM Ref and native
site/value. Current profiles integer event=72 (E16+G16+R24+site8+value8), boolean
event=68 (same with Lua int4); existing authored admission/error bank applies.
These markers are selectable authored program behavior; changing operator source
can remove/change them. Native semantic ownership remains independent.

Module cache resides in the embedded tool_api Lua closure. Cache-hit work has
no module_source operation and must not be claimed complete by this unit's native
module lookup. Its cache hit/cycle/value publication and bridge projection are
explicit residual row16/SystemA work, not a metric-only extra traversal added here.

## Independent direct native oracles and delivery sequence

New `tests/program_workflow_raw_test.cpp` uses actual selected CodingEngine,
WorkflowRegistry and Runtime on native temporary roots and owned raw recorder;
fake providers return literal known bodies, never live accounts. Read preserved
disk with the independent reader. Declare expected native scalars, literal Lua
source/BBM fixture bytes and actual native event variants locally; do not call
production raw_program encoder/descriptions to construct expected records.
Existing semantic fixtures supply business assertions, not the raw-byte oracle.

1. Published source red: actual program proposal/tool proposal/workflow selection
   and execution succeed, real one-effect marker exists, preserved raw lacks
   their semantic request/staged/parked bodies. Preserve source/binary/log before
   implementing; a missing symbol or invented stub is not the red.
2. Actual valid proposal and exact65536 bound, one-byte excess, malformed native
   type, invalid syntax, stale CAS, duplicate module/alias/schema/name. Compare
   acquired business encoder bytes exactly; require no candidate code effect
   during validation and no nonexistent body after an encoder failure.
3. Stage program A then B/tools A then B in one turn. A receives superseded,
   B only activates after accepted finish and actual1905. Failed/cancelled turn
   closes pending holders discarded with old effective state unchanged. Cut
   returned-Value allocation AFTER pending transfer: distinguish lexical failure
   from still-pending state, then observe actual eventual discard/activation.
4. Exact operator suffix ` \tA\nB`, slash precedence, non-ASCII neighbors,
   repeated same powerword, conflicting definitions, unknown slash, direct API
   opt-out. Assert literal original prompt/trigger, actual route/index/presence,
   zero child/workflow effects on conflict and correct selected source revision.
5. Real fake-provider unanswered outputs park a selected invocation. Require
   queue48/48/77 bytes, no effect before its outputs append, real execution after
   protocol-valid drain, exactly one effect, selected old definition unchanged
   by a staged replacement, next request after real completion. Force failed
   protocol/cancel/restart/queue9 and require exact remaining queue outcomes,
   no replay and no invented execution/attempt identity on refused guards.
6. Actual public nested invocation reaches workflow depth7/count8 before the
   earlier operation-depth16 guard refuses the next selection; count65 reaches
   the workflow counter guard. Assert those real acquired values/short circuits
   with Lua pcall plus a subsequent harmless host call, latched outer failure,
   no activation. Assert execution guard/body/native Operation17 association,
   Runtime destruction before terminal and honest Error/alloc/standard outcomes.
7. Native Lua allocator failure during `_native_args` push/restoration reaches
   protected preparation, owner terminal and actual original VM error; no skipped
   native holder, no dangling roots/custody and no manufactured result. Also cut
   C++ wrapper allocation before load. Use actual allocator/native forwarding
   fixture cuts, not a production fault-injection API.
8. After holders are admitted, fill ordinary CTRL or signal sticky cancellation
   and clear/activate/dequeue through real owner paths. Exact terminal bank
   emissions succeed without fresh claims. Fail admission one byte before a
   persistent holder's reserve: no issue/pending/queue/effect mutation. Reopen
   committed session restores effective definitions only, emits bootstrap restore
   and never re-executes an abandoned selection or reconstructs pending state.
9. Literal fake-provider two-step default loop checks authored3880/3881/3882
   native values and ordering against actual step/call behavior, unchanged result/
   effect count and existing budget error. No writer clock subtraction or metrics
   encoder. Both native-file and embedded default sources get the same oracle.

After ONE root plan attack, implement this whole unit, run direct red/green and
one independent whole source/oracle review. Fix its valid findings and recheck
the affected direct cases once within the same two-layer doctrine. Root owns
common build scheduling (six workers maximum), CMake/immutable-description/main
integration and source gate. No build is started by this planning lane. Report
scope and actual native results; stop before workload/performance qualification,
candidate/backstop/session ownership or a new assurance layer.

## SAME root review dispositions before source

R1: prepare one dedicated persistent issuer lazily for each selected Coding
engine, before issue/push. Only program lifetime holders use it. Under its mutex
temporarily bind that dedicated original issuer, use finite raw_try_claim_control
for213/173 admission, then publish held native requests/install/terminal events
immediately. No ordinary capacity wait owns this mutex. Main and workers keep
their independent active lanes while persistent requests/terminals share this
dedicated original lane. Handle destruction/drain occurs outside the mutex.

R2: execution admission uses332+actual borrowed revision length, with checked
framing;364 applies only to generated32-byte revisions. Public/retained revisions
are not presumed32. All variable borrowed strings use actual native lengths.

R3: previous `_native_args` is rooted in Lua's actual registry before replacement.
Protected restore retains that root until real restoration returns; primary plus
restoration failure preserve the primary business error while emitting both
actual statuses. Clear borrowed argument pointers on every ordinary unwind.
Add3840 preparation/restore request (VM R,u8 phase,bool previous-root,int native
registry ref:46) and3841 generic terminal plus actual returned int status/bool
root-held (maximum57), paid before the protected Lua call. No result is invented
when a status was not acquired. C++ wrapper failures remain prior ordinary native
invocation failures. Main operator selection uses explicit3800 method6 entry,
and workflow_invoke uses method7 for its selected snapshot lifetime; neither
infers a TLS parent. The existing bootstrap0/read1/propose2/tools3/4/registry5
methods and other descriptions remain unchanged.
Restoration's protected setter returns before a separately protected phase2
registry-reference release, so release cannot remove the previous root before
the actual restoration status returns. Phase3 protects existing numeric Lua
error-to-string conversion, which can allocate; it retains the existing displayed
message bytes rather than doing an unprotected conversion across the new owner.

Same invoke-owner body-lifetime disposition: the actual C++ tool/workflow wrapper
is complete before eval prepares Lua arguments, while existing1506 occurs later.
Actual preparation failure therefore needs3836 wrapper request (invoke R,size_t
source length:48),3837 generic52 terminal and3838 actual completed wrapper body
(R,size_t,unchanged bytes:48+N). Pay48+literal prefix length+actual source length+
literal suffix length before the existing business construction; reserve the
complete business string once and append the same literals/source without another
formatter, so a later suffix allocation cannot discard an acquired intermediate.
Publish completed body before preparation. Existing1506 remains unchanged as the
later actual VM offer; duplicate bounded views have different actual boundaries
and protect the failure path, not a presumed equality cache. Root approved this
confined correction in the SAME invoke owner unit. The allocator oracle checks
the acquired wrapper even when prep fails and no1506 tool load is reached.
