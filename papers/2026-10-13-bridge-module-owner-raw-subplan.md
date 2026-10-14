# Native Lua bridge and module cache raw owner

2026-10-13. Whole unit under implementation after ONE root plan attack. Root approved the explicit
split: bridge/module cache4800–4899, then standalone SystemA representations/
codecs4900–4999 as their own unit, then candidate56. Stop before qualification.
The ONE root plan attack and its BR1/BR2 dispositions are recorded below. No dependency,
account/provider call, compiler or new broad acquisition is used for the study.

## Source and existing references read

Actual Runtime is src/coding.cpp:440–1297. Bridge currently lives at976–1087;
module loader/call691–755; the cache/loading wrapper in tool_api949–964.
Existing read_value162–262 has actual depth128/nodes500000 guards and reserves
its traversal stack before raw reads. push_value77–147 follows the existing
business Value representation. Their full representation observations are NOT
claimed by this unit. Program module_source/discovery, generation activation,
VM/source1500–1509 and actual module call1517/1518 already exist and keep their
original layout/credit. Program3800–3882 remains delivered, not reopened.

Read papers/2026-10-10-raw-instrumentation-surfaces.md row16/SystemA;
INSTRUMENTATION raw/completion/plan; actual module-config and Lua-tool delivery
papers and tests/program_config_test.cpp, lua_tools_test.cpp. Read the acquired
Lua embedding grounding/catalog, actual pinned sol2 trampoline103–174 (study
only), Lua5.5.1-tests/api.lua1293–1350 (failure strategy only), and release-matched
Lua5.4.8 source in quarantine/resource-sampler-2026-10-11. lapi.c111–126 confirms
lua_checkstack reports failed growth;405–422 confirms lua_tolstring can allocate
when coercing a number but returns an already-string body directly;1043–1092
confirms conventional lua_pcall catches through luaD_pcall and does not retain a
resumable native stack. Read ldo.c45–155 DIRECTLY from the intact5.4.8 tar archive:
C build transfers by longjmp, while a C++ build has a different catch policy.
Read lgc.c907–930: allocation can cause a protected finalizer to reenter native
callbacks. lparser.c mainfunc/parser at the end of the same archive gives every
text main chunk one first LUA_ENV upvalue; lapi.c1398 onward implements the actual
setupvalue return. The existing runtime is5.4.8, not the old5.5.1 proposal; neither
sol2 nor upstream tests are adopted or executed.

Consequences: catch native business exceptions in native-only regions; perform
allocating Lua result operations inside an actual protected shim; never leave
C++ owners across a Lua error jump. Give each real bridge invocation its own
result owner, since projection allocation/finalizer reentry can otherwise reuse
Runtime.scratch while an outer projection references it. Close paid terminals
and all native frames before raising the final Lua error. A Lua pcall caught error
does not mean the earlier host effect rolled back. No coroutine scheduler or
new asynchronous Lua continuation is added.

## Exact unit and ownership

Bridge owns actual Op/argument-count entry, actual selected immutable VM/generation,
optional currently-open Operation17 and optional current module invocation;
borrowed native string inputs used by lua_string; native host dispatch outcome;
protected result projection/status; array tagging through the same protected
path; the existing completed bounded bridge-error buffer. It does not serialize
Value, format an error for metrics, recompute a codec result or traverse a tree.

Move bridge's single scratch result into a per-invocation native BridgeFrame.
Stack-linked Runtime::bridge_frame points to the current frame only during its
protected invocation and restores the previous pointer on ordinary unwind.
The Value is the existing business result moved into this holder, not a clone.
Nested bridge/finalizer calls get separate holders. A result projection shim
uses only frame pointers, const references and scalars inside allocating Lua
APIs. The C++ frame remains OUTSIDE lua_pcall, so Lua memory/error transfer
returns to it normally. No holder reaches final lua_error/authored_raise.
Default owner context is absent without selected raw binding/VM; business safety
and exception containment still apply, raw emission opts out.

Module cache remains the existing Lua cache/loading tables, with the same
lookup order, cached false semantics, cycle/unavailable/nil-result errors,
late pinned module_source lookup, environment and assignment order. Do not
replace it with a C++ cache, serialize it, add a metric-only enumeration or
publish pending definitions. Fixed private begin/observe/end seams capture literal
native locals and retain one invocation-owned userdata token. The wrapper's
Lua-level pcall remains yieldable; it keeps the token and parent strongly rooted
across suspension. A private weak-key coroutine association identifies only the
actual current invocation; it is not its owning root. The complete yield-safe
lifetime and protected setup design appears in the BR2 disposition below.

Bridge frames retain their actual issuing handle during synchronous protected
calls. Module tokens retain one lazily prepared dedicated issuer per Runtime;
all module acquisition under its mutex is finite, and all immediate held
publication binds that actual issuer. No ordinary blocking admission holds the
issuer mutex. Unselected entries create no records. Immutable operation/module
references are copied at entry. Reentry creates a separate paid frame/token and
restores its predecessor; suspended modules never borrow a C++ stack frame.

## Native fields and credits

All records use E16 and R=lane16+ordinal8. G is existing generation16; int4,
size_t8, pointer8, bool1 and ErrorCode1/detail8 on current profiles, asserted in
the fixture/source. Widths below exclude padding (no struct blobs). Optional
fields appear only with their actual presence flag. Records never infer a future
operation/source/terminal identity.

|Site|Exact existing/native body|Maximum bytes|
|---|---|---|
|4800 bridge entry|VM R,G,Op native int,actual lua_gettop int,operationPresent bool,[actual Operation17 R],modulePresent bool,[current4820 R]|114|
|4801 bridge outcome|4800 R,RawOperationBranch u8,reached stage u8,ErrorPresent bool,[ErrorCode,i64],returnsPresent bool,[actual returned int]|57|
|4802 borrowed/completed string|4800 R,role u8,size_t N,exact existing bytes|49+N|
|4803 projection entry|4800 R,mode u8 (result0,error1,array2),actual Lua stack base int|45|
|4804 projection outcome|4803 R,actual lua_pcall status int,actual returned lua_gettop int|48|
|4820 whole module entry|VM R,G,actual lua_gettop int,input lua_type int,outerModulePresent bool,[4820 R],operationPresent bool,[Operation17 R]|114|
|4821 borrowed name|4820 R,size_t N,exact Lua STRING bytes (no coercion)|48+N|
|4822 actual predicate|4820 R,branch u8,actual bool|42|
|4823 actual mutation|4820 R,mutation u8,actual value lua_type int,actual lua_topointer pointer (may be null)|53|
|4824 loaded source link|4820 R,actual existing1506 R|64|
|4825 whole module outcome|4820 R,RawOperationBranch u8,stage u8,ErrorPresent bool,[ErrorCode,i64],reason u8,pcallAvailable bool,[actual Lua boolean int],valueAvailable bool,[actual lua_type int,lua_topointer pointer],innerStatusAvailable bool,[actual native Lua status int]|76|
|4826 borrowed native chunk name|4820 R,size_t original Lua string extent,size_t native CString extent,exact CString including NUL|56+C|
|4830 environment assignment request|4820 R,1506 R,actual function pointer,actual environment table pointer,actual upvalue index int|84|
|4831 environment assignment return|4830 R,namePresent bool,[size_t CString extent,actual returned CString bytes including NUL]|54|
|4832 protected userdata setup|4820 R,mode3 u8,actual stack base int|45|
|4833 protected userdata setup outcome|4832 R,actual lua_pcall status int,actual stack top int|48|
|4834 native module call link|4820 R,actual1517 R,actual function pointer|72|

4802 roles are call-name1,JSON input2,restore locator3,display input4,completed
existing bridge-error buffer5. Extend lua_string with an optional explicit
bridge context: its existing type check/tolstring yields the SAME borrowed body;
claim49+N and copy before its existing C++ string constructor. Default read_value
calls remain opted out pending the separate representation unit. This adds no
formatter/encoder/coercion. Input wrong type keeps its existing invalid_range
and has no fabricated string original. Embedded NUL is preserved.

Hold bridge CONTROL114+57+45+48=264 BEFORE business dispatch. Hold DATA49+511=560
for the existing maximum completed bridge_error buffer before an effect; consume
only if that buffer actually completes, release unused credit. Input string
claims use their actual borrowed extents separately. Whole-frame terminal is
held; stack refusal/Cpp allocation/error/cancellation do not require a fresh
claim. 4803/4804 are emitted only for an actual protected preparation, in its native
result/error/array mode. Failed dispatch uses error preparation, never a
manufactured successful result projection. Stage1 stack/type,2 input acquisition,3 native
dispatch,4 projection,5 native return are actual reached sites.

Module holds CONTROL114+76+2*42+3*53+64+84+54+45+48+72=800 before import body. Branch1
cache-hit is always reached; branch2 loading/cycle only on miss. Mutation1
loading=true,2 loading=nil after actual inner call,3 cache assignment after
existing nonnil validation. Syntax/unavailable/cycle/cache-hit paths emit only
reached events and release unused bank. Loaded-source link and environment pair
are used only after actual load. Existing1506 owns the source content;4824 links
it rather than duplicating that source.

At entry, STRING name gets48+actual length DATA before the body; other Lua types
have only actual type in4820 and keep the old language/host error semantics.
Inside module_load, chunk name is a borrowed caller string. Its native CString
may stop at an embedded NUL: fund57+the actual Lua extent before the loader,
then copy the actual CString extent/bytes unchanged, including its NUL. No new
chunk-name formatter is introduced; the existing Lua concatenation remains
business code. This observes the native boundary, not every Lua concatenation.
4831's5-byte CString maximum follows the release-matched text-main LUA_ENV
upvalue named `_ENV`; text-only load remains unchanged. Capture the actual
returned name/null immediately after setupvalue and before further work.
Unexpected width must fail recording honestly; it must never truncate or read
undefined bytes. Actual setupvalue side effect and failure remain distinct.

Module stage1 input/stack,2 cache/cycle,3 lookup/source load,4 environment,
5 source call,6 loading clear/value validation,7 cache publication,8 native
return. Whole-wrapper success is its actual Lua boolean; native source-call
status is present only after the existing module_call pcall. Raw returned means
the actual wrapper end was observed. A false wrapper boolean never becomes an
invented LUA_ERRRUN status or an import-success claim. Failed source calls never
get cache publication. Value type/pointer is direct native observation, not a table snapshot.
Full Lua/native representation bytes remain separate SystemA work.

## Failure and exception boundaries

A selected frame owns RawCaptureFailureScope. Final claims/publish/fence failures
mark actual origin; finite probes are not failures until rejected. Check origin
before starting later work and after final paid publication. An actual recorder
refusal latches Runtime's existing admission failure so Lua pcall cannot clear
it and admit another host effect. Ordinary business Error retains existing
behavior/capacity-stop policy. No recording failure becomes a success packet
or a new effect, and no terminal failure exposes a successful return.

Bridge catches Error/bad_alloc/length_error/std::exception/nonstandard native
exceptions around native dispatch ONLY. Preserve actual Error fields, native
allocation/length/standard/nonstandard branch and the actual existing mapping to
Runtime.failure/bridge_error. Add explicit foreign-exception containment (today
bridge has no catch(...)); no C++ exception may escape a Lua C callback.
Do not catch Lua control transfer by surrounding allocating Lua APIs with this
catch-all. Protected projection returns real Lua status; restore frame pointers
and destroy native holders before raising. Primary dispatch failure is preserved
if producing its Lua error string itself exhausts memory. The existing bounded
error buffer is captured unchanged after its business copy; no metric error
formatter or writer-side truncation is added.

Module observers and module_load may reject original admission after earlier
sources/effects exist. Finish actual outstanding import with its held status/
Error outcome and keep earlier original/effect records. Do not relabel source
call1518 as rollback. A failed import is not cached; an EXPLICIT later import
can execute it again as existing behavior. It is never automatic recovery/replay.
The existing Lua wrapper remains yieldable without a new scheduler or thread
migration. Its original native module_call barrier remains non-yieldable.

## Independent real-owner oracles and sequence

Create tests/bridge_module_raw_test.cpp, native temporary RetainedState and owned
RawRuntime/recorder, no provider calls. Expected raw headers/fields/known bytes
are specified literally and parsed from preserved disk, not generated by the
emitter/descriptions or another Value encoder. Inspect actual effect files and
native object identity inside real Lua; link every terminal exactly once to its
actual request, reject unreachable extra events and false returned outcomes.

Before product edits, preserve one published-source native red: real bridge
operation/string roundtrip plus first module import and cache hit succeeds;
actual disk lacks4800/4820 and branch records. It must fail on that specific
missing observation, not a compiler/fixture/API guess. Then implement the whole
unit after ONE root plan review.

Direct green/fault cases, finite not a campaign:

1. Real bridge borrowed strings with embedded NUL/UTF8; actual call/display,
   decode, restore error; successful direct array tagging. Read exact strings,
   Op/int count, reached stages and protected projection status. Nonstring inputs
   retain old errors. No tree walk/expected production encoder.
2. Native host callbacks throw actual Error, bad_alloc, length_error,
   std::runtime_error and int AFTER one real marker effect as appropriate.
   Require containment, distinct native branches/status, no second effect or
   invented rollback. Subsequent harmless ordinary business errors may recover;
   sticky recording refusal must prevent the forbidden later effect.
3. Forward the real Lua allocator, deny the actual result-projection growth,
   observe real LUA_ERRMEM and held terminal; no fabricated status. Force a real
   finalizer to reenter bridge during an outer projection and assert independent
   inner/outer results and refs, no overwritten outer Value. This attacks the
   single scratch owner directly, not a second lifetime certificate.
4. First module import executes one append; second cache hit returns SAME table
   identity and zero additional module_source/load/call/effect. Cached false
   remains a hit. Nested modules link actual outer4820; direct cycle and unavailable
   import have no fabricated load/cache write. Nil result is rejected after actual
   source call/loading clear. Pending replacement stays invisible, then the next
   real VM sees the effective generation; no old import is replayed on restore.
5. Real module effect then Lua error: original/effect remain, loading clears,
   cache does not publish. An explicitly requested retry is a distinct invocation.
   Test actual syntax error through the existing mutable Lua module_source adapter
   (normal program validation rejects syntax earlier), exact source1506 and
   status; ordinary source failure differs from recorder refusal.
6. Actual borrowed module/chunk strings, embedded-NUL native chunk operand,
   actual `_ENV` name and environment pointer association. Compare native Lua
   type/pointer and exact original byte extents independently. No pointer is a
   durable identity; R/G provide logical linkage.
7. One-byte-short string DATA grant prevents native copy/load/effect and keeps
   prior sources; held ordinary CONTROL exhaustion/cancellation after entry still
   closes actual bridge/module frames without fresh cleanup claims. Actual final
   terminal/notification failure cannot expose a successful return. Forward real
   native/allocator seams, no production injection API.

Use existing finite native allocation/producer-pressure mechanisms, not new
interceptors in production. Source/headers/test strict format/warnings and narrow
static checks plus affected Lua/program_config business fixtures belong to owner
validation. Root controls shared CMake/description registration/build slots and
later profile/integration gates. No workload sizing, performance claim, hook or
commit is part of this unit. ONE whole code/oracle attack follows direct greens;
valid findings receive finite fixes and one recheck, no additional panel.

## Files and explicit residual unit

Proposed new raw_bridge.hpp/cpp contains only fixed-field capture/banks. Confined
Coding Runtime bridge/projection/module methods and tool_api wrapper change;
new dedicated fixture. No root Decision/Git/Beads/restart/Main branches, shared
runtime scope source or completed program lifetimes are rewritten. Root owns
CMake/runtime description registration. Declare exact files before edits.

The following separate4900–4999 SystemA plan must read src/value.cpp/json.cpp/
packet.cpp/presentation.cpp and public headers plus actual read_value/push_value/native Number
storage. Owners include parse_number/Number.text/valid_utf8, Value accessors and
object construction/limits, JSON parse/parse_fields/response/dump, packet encode/
decode/string/value/flattening and format_value, and actual recursive Lua/native
representation traversal. It needs per-existing-traversal node/field/scalar
capture and actual codec input/output/body admission, no new Value visitor,
serializer, normalization or duplicate business encoding. This paper does not
claim those bytes from bridge Op/type fields. Candidate56 is still a later unit.

SystemA's original census also names value.hpp shared storage, SharedSequence
radix nodes and foundation ByteBuffer::copy. They need explicit allocate/share/
detach/final-drop owners, including drops after binding exit or on another
thread. Synchronous codec fields cannot discharge that cleanup obligation. The
next source study records those separate resource boundaries and must disposition
them explicitly; they are not silently added to this bridge implementation.

## Same-review BR1/BR2 disposition before source

BR1 resolved: BridgeFrame retains its own optional native primary Error and
completed512-byte business error buffer as well as Value. Nested bridge restores
its predecessor's native failure state. Result AND error preparation are actual
protected calls while that frame and bytes remain alive. After preparation and
paid terminal/origin checks, native holders die before raising the already-rooted
Lua error. Allocator failure during error preparation preserves the primary
native Error; reentrant callbacks cannot overwrite the primary text/buffer.

BR2 resolved:4803 mode u8 adds one byte; request45/return48, complete bridge bank
264. Install one no-upvalue C shim into a fixed registry key during existing
protected VM install. Entry retrieves it with nonallocating rawgetp after
lua_checkstack; no allocating closure is created outside protection. Entry/
admission/frame setup is native-only try/catch for all C++ outcomes; leave that
scope before any Lua raise. An unadmitted request has no invented terminal.
Module invocation and observer setup use equivalent containment. Existing
Loaded.reference is the actual1506 used for4824/4830, with no source-ref API
extension. Native test includes primary native error plus real finalizer bridge
reentry and allocator refusal during actual error preparation. No plan rereview.

## SAME BR2 final yield-preserving module design (supersedes synchronous wrapper)

Root rejected the proposed whole-import native lua_pcall because an overridden
Lua module_source/setmetatable path could previously yield before the native
module_call barrier. Preserve that behavior. Read release-matched lbaselib.c
finishpcall/luaB_pcall directly from the intact5.4.8 archive: Lua-level pcall uses
lua_pcallk and treats LUA_YIELD as continuation, unlike conventional C pcall.
Read lapi.c userdata/uservalue and actual GC/finalization source. No new coroutine
scheduler is required. The bridge's protected native result/error preparation
is still synchronous; module cache/lookup remains yieldable Lua code.

Replace the experimental module_invoke/ambient ModuleFrame with fixed private
begin/observe/end seams and invocation-owned native userdata. The original cache/
loading import body receives its token explicitly. A fixed private weak-key
coroutine→current-token table belongs only to actual import invocation ownership;
no subscriber/callback registry, enumeration or processing is added. Pin the
original coroutine.running and Lua pcall locally at bootstrap. Wrapper gets the
actual thread key, captures its actual parent token, calls begin(name,parent),
then keeps `local token <close>` and parent as strong locals during Lua-level
pcall(import_body,name,token), including every yield. Weak association alone does
NOT root a paid loan. Each userdata also retains its parent in a uservalue.
Normal completion restores current-token to parent before end(token,actual ok,
actual value), then keeps original return/error behavior. The import body uses
observe(token,phase,actual local) at its already-declared predicates/mutations.
Cache hits, cached false, source calls, cycle/nil/errors and pending visibility
retain their actual order and meaning. module_call keeps its existing nonyieldable
native barrier. No borrowed stack frame survives suspension.

Userdata has a protected __close and a nonallocating __gc native cleanup. On
abnormal scope/coroutine close, __close restores the weak association from its
actual thread/parent uservalues before closing the still-active paid token.
On collection/VM close, __gc closes an unfinished import with unknown/unfinished
outcome and releases the C++ token. Ended tokens do not emit twice. Active token
is strongly rooted by its local/pending native setup owner, not the weak map.
Token does not retain Runtime*, a borrowed failure scope or a C++ Value across
yield. It stores immutable VM/G/parent refs, reached stage, actual inner status
if observed, optional actual recording failure, preheld credit and issuer.
The creating issuer/handle survives binding and Runtime destruction. Runtime
sets the issuer's actual closing state before lua_close so abandoned collect
and actual VM close remain distinguishable. No old process/command adoption.

Prepare ONE dedicated serialized issuer lazily at the FIRST actual selected
module begin, outside its mutex; no registration per import/cache hit/validation
VM or when module path is unused. Token acquisition holds its issuer mutex and
binds that actual handle; use finite raw_try_claim_control and root-approved
raw_try_claim_data, never an ordinary wait under the mutex. Both are probes;
only an actually rejected result is marked raw_capture_failed. All immediate
held event publication/terminal and final release use that same mutex/handle.
RawModuleToken methods take fields from actual Lua locals and native API returns;
no encoder/visitor/formatter. A final cleanup does not inherit some later TLS
issuer. Lost live custody cannot silently emit ordinal0 or free the token.

Begin allocates a native token under native-only catch containment. Its ordinary
C++ owner stays OUTSIDE an actual protected Lua preparation shim. Mode3 of the
preinstalled shim allocates userdata/uservalues/metatable and offers the native
pointer; the protected shim attaches the pointer only after all allocating userdata setup
completes. Caller releases its native owner immediately if that attachment
occurred, before any later capture check, including when that later check fails.
Unattached failure keeps native ownership and closes from the paid bank.
No C++ automatic is jumped over, no half-initialized userdata frees a pointer
still owned by the setup caller. VM install owns all allocating fixed metatable/
registry setup. No allocating closure is created outside protection. Error
preparation also runs protected with immutable per-invocation primary/buffer;
never manufacture LUA_ERRMEM when only lua_checkstack reported false.

Bridge/module_load/module_call resolve the actual token for their actual Lua
thread through raw registry/table operations with checked stack, copy immutable
refs, and keep the returned userdata rooted while using it. They never rely on
a suspended ambient frame. Existing Loaded.reference is the1506 ref used for
4824/4830. Add4834 at actual module_call entry: tokenR,actual1517R,function pointer,
72 bytes before execution. At actual1518, store the returned native Lua status
in the token (no extra transformed status). Parent/child source and call linkage
therefore survives two interleaved coroutine imports.

Updated module outcome4825 is native:
R,u8 RawOperationBranch,u8 stage,bool Error,[ErrorCode,i64],u8 reason,
bool pcallAvailable,[int actual Lua boolean],bool valueAvailable,
[int actual Lua type,pointer],bool innerStatusAvailable,[int actual Lua status].
Maximum76. A Lua pcall boolean is NOT converted into invented LUA_OK/LUA_ERRRUN.
Reasons0observed end,1abandoned collection,2VM close,3scope/coroutine close,
4setup/refusal. Only actual reached values are present; GC/close cannot fabricate
pcall/value/inner completion. Source-call native status and whole-wrapper Lua
boolean are distinct. Actual token fields supply the inner status if reached.

Module setup pair4832/4833 uses the45/48 preparation layout with mode3, linking
actual userdata allocation. Reserve the complete fixed module bank:
114 entry +76 terminal +84 predicates +159 mutations +64 source link +138 ENV
pair +72 call link +93 protected setup =800 CONTROL. Borrowed name48+N and
chunk57+LuaExtent DATA are separately finite-paid before native copy/load.
The rejected synchronous proposal banks629/722 are not implemented; the table
and whole-bank800 above describe the final yield-safe design. Bridge264 remains
unchanged. Fixed stage/events
are unused/released on cache/cycle/syntax/GC branches, never fabricated.

Direct finite oracle additionally uses a real coroutine with overridden lookup
that yields before module_call, another interleaved coroutine import, actual
resume/native effect/cache identity and exact independent token/call origins.
A separately abandoned coroutine/actual coroutine.close/VM close must emit one
honest held outcome without a leaked credit or early GC. Fill ordinary CONTROL
while a real token is suspended; resume/end and cold cleanup still use paid
issuer credit. Deny actual protected userdata allocation, assert native owner
keeps/releases its token and zero lookup/effect. Actual primary native error plus
finalizer reentry/error-preparation denial remains BR1's distinct direct cut.
These replace the wrong synchronous oracle; no new review panel or general
coroutine platform is added. Root approved finite try-data API under SAME BR2.

## Shared-emission consolidation

The operator required consolidation before finishing the remaining surfaces.
RawProgramCall and RawBridgeTrace now use shared RawEmission bank/request/terminal
ownership and raw_publish_linked; Program uses raw_call_run for classification.
RawProgramLifetime and RawModuleIssuer/Token retain their genuine lifetime and
serialized creating-issuer decisions. No wire layout, grant, error origin,
coroutine behavior or native reference changes. The sole shared-core source
attack is papers/2026-10-13-raw-emission-consolidation-code-review.md. Existing
real native Program and Bridge fixtures are the migration checks; no extra
adapter review panel is added.

## Same integrated gate repair: actual foreign exception custody

The existing operation fixture's native provider throws integer93 and requires
that exact integer at the actual caller. The old bridge caught owned Error and
standard exceptions but allowed foreign exceptions to escape. The new contained
bridge accidentally normalized that actual nonstandard exception to
`external_unknown`. Restore the caller behavior without reinstating an exception
unwind through Lua C frames: each actual synchronous Runtime::eval owns a
stack-linked native exception frame with an owned std::exception_ptr. Each
NativeFrame retains the actual caught foreign exception through its protected
error preparation, raw outcome and native owner cleanup, then transfers it to
the eval frame before the final Lua error jump. The raw nonstandard outcome and
existing bounded Lua error payload remain honest; they do not replace the
exception's identity or payload at the host boundary.

After protected Lua execution returns, eval rethrows the actual retained primary
through its existing argument/root and stack cleanup. It checks even a successful
Lua return, so Lua pcall cannot hide a foreign host exception or permit a later
native effect: a pending eval exception refuses another bridge dispatch before
new native admission. Eval's previous exception-frame pointer restores lexically
for actual nested Lua tool calls. Finalizer reentry during an already failed
outer callback cannot replace that outer primary in the same eval; an independent
nested eval has its own frame. No process-global exception pointer, Lua coroutine
scheduler, Value encoding or new exception policy is introduced. Standard/owned
exception normalization remains as before.

Root delegated and accepted this narrow invariant repair under the same Bridge
unit. The unchanged `operation_raw` integer93 oracle is authoritative. The
existing Bridge foreign display case is tightened to require its actual integer19,
preserve the earlier actual file effect, and reject a later display callback even
if Lua pcall caught the prepared error. Its one nested `blackbird.call('lua')`
variant directly exercises child eval → parent eval ownership and the two actual
nonstandard raw outcomes. These are the finite checks of this gate finding, not
a new review panel or qualification campaign.
