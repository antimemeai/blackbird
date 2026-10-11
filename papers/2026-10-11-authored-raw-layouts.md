# Authored event layouts and Lua load ownership

Concrete continuation of the reviewed authored subplan. Source has not started.
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
|1507 source load return|load R,int Lua status for actual C API load; module-source return has its own returned-to-Lua tag and never invents a load status|
|1508 execution request|VM R,load R; before the actual protected pcall|
|1509 execution return|execution R,int actual pcall status|
|1510 authored signed integer|G,VM R,lua_Integer local site,lua_Integer value (qualified signed64)|
|1511 authored real|G,VM R,lua_Integer local site,lua_Number value (qualified double)|
|1512 authored Lua boolean|G,VM R,lua_Integer local site,int returned by lua_toboolean|
|1513 authored bytes|G,VM R,lua_Integer local site,size_t length,exact borrowed bytes|
|1514 authored native boolean|G,VM R,i64 local site,native bool value|
|1515 admitted refusal|G,VM R,int argument count,int site type,int value type,bool integer site available,[actual i64 site when available],ErrorCode,i64 detail|
|1516–1599|Reserved; no invented emission|

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
and `CodingEngine::module_source`'s existing source return. Each is captured once
before that selected load/return boundary. Validation-only loads are a distinct
source-kind branch; no execution success is inferred from syntax success. The
module path is a source return into the installed Lua loader;1517 will be needed
if a distinct module load-result surface is included. Do not rewrite the loader
or wrap Lua's generic load solely to pretend closure attribution is complete.

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
