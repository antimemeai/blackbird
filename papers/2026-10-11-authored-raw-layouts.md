# Authored event layouts and Lua load ownership

Concrete continuation of the reviewed authored subplan. The setup-only current
handle accessor, native helpers, Lua emission and selected load/execution hooks
are implemented; the direct fixture qualifies their actual native boundaries.
The namespace is an immutable VM definition generation, not a dynamic closure
identity. Ops/evals interpret these bodies only after preservation.

R is the actual lane16+ordinalu64 reference, G is generation16, E is envelope16.
SourceReference is journal16+sequenceu64. Each native scalar retains its original
representation. Distinct fixed sites avoid converting native bool to Lua's int
boolean representation merely to share a body.

| Site | Body and actual boundary |
| --- | --- |
|1500 VM allocation request|G,bool root source available,[actual retained root SourceReference]; before luaL_newstate; publication R identifies this VM allocation/execution|
|1501 VM allocation return|VM R, native branch u8 (null/created); actual existing pointer-null branch, no fabricated Lua status|
|1502 VM installation request|VM R; before the protected installation pcall|
|1503 VM installation return|installation R, native int pcall status|
|1504 VM end|VM R, native u8 end branch, optional ErrorCode/i64 detail; held before allocation and published after actual lua_close or failed allocation|
|1505 source binding|G, actual retained root SourceReference,size_t source length; after successful root retention|
|1506 source load request|VM R,native u8 source kind,size_t length,exact existing source bytes; before system luaL_loadbufferx/loadstring or module-source return|
|1507 source load return|load R,int actual Lua status,bool function available,[actual void* from lua_topointer on a successfully loaded function]|
|1508 execution request|VM R,load R; before the actual protected pcall|
|1509 execution return|execution R,int actual pcall status|
|1510 authored signed integer|G,VM R,lua_Integer local site,lua_Integer value (qualified signed64)|
|1511 authored real|G,VM R,lua_Integer local site,lua_Number value (qualified double)|
|1512 authored Lua boolean|G,VM R,lua_Integer local site,int returned by lua_toboolean|
|1513 authored bytes|G,VM R,lua_Integer local site,size_t length,exact borrowed bytes|
|1514 authored native boolean|G,VM R,i64 local site,native bool value|
|1515 admitted refusal|G,VM R,int argument count,int site type,int value type,bool integer site available,[actual i64 site when available],ErrorCode,i64 detail|
|1517 module execution request|VM R,actual void* loaded function; before its protected pcall|
|1518 module execution return|execution request R,int actual protected status|
|1516 native refusal|G,VM R,i64 local site,u8 native API kind(1 signed/2 real/3 bool/4 bytes),ErrorCode,i64 detail|
|1519–1599|Reserved; no invented emission|

Native rejection1516 is74 bytes and fits the86-byte common claim; its kind names
the actual typed API, without invented Lua argument types.

Native signed64/double/byte APIs use the corresponding exact scalar/string
layouts; native bool has a distinct body. Native helpers accept an explicit
generation/VM reference and local site, not an inferred current program or a
writer dictionary. A Lua call returns zero values on success.

Before each admitted authored call hold the maximum1515 control outcome. Only
then inspect argument count/types/scalars using nonallocating Lua APIs. After
strict extraction and checked transport size, data admission precedes immediate
raw copying. No callback or Lua allocation lies between that final check and
copying. An admission failure before extraction has no acquired metric; it raises
the original error and marks the held VM owner end's admission-closed outcome.
An interruption after control admission has an acquired/refused call and uses
that held outcome. Native helpers follow the same pre-acquisition boundary.

VM setup prepays1501,1502,1503 and1504 before1500; cleanup does not acquire new
credit. Source binding and source-load bytes require their bounded data capacity
before capture. Each actual load/run pair has its own prepaid native return.
Calls can execute arbitrary instrumented Lua; their held return survives those
emissions. No claim is handed across concurrent producers.

Actual system-provided source paths are root `Runtime::eval`, tool wrappers
already produced by `invoke_tool`, installation tool_api/print_program literals,
and `CodingEngine::module_source`'s existing source at its selected native load.
Each is captured once before that selected load boundary. Validation-only loads are a distinct
source-kind branch; no execution success is inferred from syntax success. The
module helper captures its two private native closures once, as detailed below.
Do not wrap Lua's generic load or claim arbitrary closure attribution.

Change retain_program to return the SourceReference it already places on its
retained event, after the append succeeds. Store it on CodingEngine for the
generation; child tool/workflow VMs use that actual same generation binding with
distinct VM R. A failed retain_program cannot publish source binding or allocate
its VM. Root/tool/module bytes themselves are preserved in the raw stream; the
semantic root SourceReference does not purport to locate another wrapper's bytes.

Lua close keeps the original borrowed owner cancellation endpoints alive and
uses them independently of disabled debug hooks. Multiple finalizers under a
full lane test admitted invalid arguments versus unadmitted canceled calls.
Finalizer warning delivery is separate from the held refusal/end records. The
finite VM end budget does not impose a work limit on arbitrary Lua finalizers.

Source inspection found another actual lifetime requirement: runtime_ persists
after turn() returns and is destroyed during a later replacement or CodingEngine
destruction. A held token cannot borrow a retired turn-thread producer, and a VM
cannot cache that turn's borrowed cancellation FDs for its later destructor.
Retain the actual current RawThreadHandle during VM setup; this extends the
existing producer/mapping lifetime without allocating a second64MiB lane. The
trace's claims are destroyed before that retained handle. Before later Lua close,
temporarily bind that exact handle under the current execution owner's valid
RawWaitControl. Never reuse stored expired FDs or the mutable engine generation
as if they were this VM's original generation. Store G/VM R on the VM trace.

Provide a setup-only current-handle accessor from RawThreadBinding's actual
shared handle (one shared-ownership increment, no per-emission ownership work).
Sequential transfer requires the previous turn worker to be joined before a
later thread binds the retained producer. Native fixtures directly exercise VM
replacement and engine destruction on another joined thread, with originals and
held END intact. This does not claim concurrent VM execution is supported.

An externally executed workflow can have no retained root workflow SourceReference;
allocation records mark it absent and capture its actual workflow source separately.
Do not manufacture a root reference or reinterpret a tool wrapper's source as the
retained root. Broader program lifecycle work owns finalizer business behavior;
these hooks name the actual old VM generation even if other engine state changes.

Before implementation, compute setup/load/refusal widths with sizeof, guard the
qualified Lua integer/number ABI, choose the complete module-load versus return
coverage boundary, and write literal independent disk-body oracles. This note
does not claim those unfinished choices or direct checks complete.

## Final module boundary and byte allowances

Use two private native closures captured once by the installed module helper:
load existing source/name/environment through luaL_loadbufferx and its existing
_ENV setup, and protect the already loaded function through lua_pcall. Remove the
temporary fields from blackbird after capturing those closures into the helper;
leave Lua's general load/pcall alone. This observes selected system module loads
without a per-function registry, extra wrapper on each loaded function, or
claiming attribution for arbitrary Lua-created closures. Preserve the helper's
existing loading/cache/error and first-result behavior.

For selected load return1507 emit load R,int actual status,bool function available,
and, on success, the actual void* returned by lua_topointer on that loaded
function. This is an unchanged native acquisition, not a formatted identifier.
The per-VM native description records sizeof(void*). Module execution uses1517
request VM R,void* actual loaded function and1518 return request R,int actual
protected status. These fields join a selected load to its real function without
keeping a writer dictionary or storing a lossy signed ordinal in Lua. VM identity
bounds the pointer's meaning; it is not a cross-VM or cross-process address claim.
Other selected load/execution pairs retain their actual native load-reference
linkage. Existing module_source semantic retention is unchanged; raw load copies
its existing source once, rather than adding a second metric source-return copy.

On the qualified64-bit profiles: admitted refusal max86 bytes, scalar signed/
real72, Lua bool(int)68, native bool65, bytes72+actual length. VM allocation request
is33 without root reference or57 with it. Prepaid setup/END is175 = allocation
return41 +installation request40 +installation return44 +END maximum50. Each
selected native load reserves its maximum53-byte return before acquiring source;
load request is49+actual source length. Root/tool evaluation additionally prepays
execution request64 and return44 before code can emit. Validation-only loads
reserve53 only and never emit an execution success. Selected module execution
request is48 (E16+VM24+pointer8), return44. Claims release unused variants.
Root source binding is64 fixed bytes (E16+G16+SourceReference24+size_t8).
Copying root source again here would duplicate load capture; the actual root
bytes are captured at1506.
The earlier1505 draft's raw length has no raw byte tail. SourceReference itself
is journal16+sequenceu64. Checked framing/arithmetic precedes every acquisition.

Implement setup-only raw_current_thread() by a TLS pointer to the actual current
binding handle and restore that pointer in nested binding teardown. Retaining it
costs one shared ownership increment at VM setup. No extra weak pointer/context
allocation, per-emission increment, epoch copy or persistent borrowed cancellation
FD accompanies the VM. Retained handle is declared before credits so credits die
first. Destructor temporarily binds that handle only when one was captured.
Absent raw capture does not hide Lua argument validation or change its zero-result
success contract. Guard lua_Integer signed64, lua_Number double, native bool1 and
pointer width in source; immutable descriptions precede activation.

Direct reds/greens cover missing Lua API, scalar bit patterns/NUL/high bytes,
strict types/arity/site refusal, rejected source retention before VM allocation,
root/tool/module originals before selected execution, immutable generation under
joined-thread replacement, and real full-lane finalizer cancellation independent
of Lua debug hooks. The existing one authored plan attack governs this continuation;
no second transport/design panel or eval campaign is added.

## Native Lua error object and capture policy

The new authored API raises a private native Error userdata only after all
claims have been destroyed. The selected protected-call boundary recognizes
the actual userdata/metatable. A caught refusal does not set the business
capacity-stopped flag or leave a stale native Error on Runtime; deliberate
rethrow preserves the same object. A later distinct Lua error stays distinct.
No borrowed string pointer, per-error dictionary, or generic pcall wrapper is
used. Its fixed __tostring is an ordinary error presentation surface, independent
of raw metric validation and copying. Existing bridge error policy is unchanged.

A VM's setup capture policy is immutable with its generation and handle. A VM
created without raw capture cannot publish a fake zero VM reference when a later
execution owner binds a raw producer. Strict authored argument validation and
zero-result success still apply without capture.
