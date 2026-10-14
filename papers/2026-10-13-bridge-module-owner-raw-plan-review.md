# Bridge and module owner's one plan review

2026-10-13. Read the complete bridge/module subplan, actual Runtime bridge,
module_load/module_call/load, tool_api cache wrapper, read_value/push_value and
native Value/Number ownership. Existing Lua5.4.8 protected-call/allocator study
grounds this review; no new dependency, execution, broad campaign or source
edit. This is the ONE plan attack before this whole unit's implementation.

BR1: reentry protection must cover the primary native failure and completed
business error buffer as well as Value. Runtime.failure and bridge_error are
currently shared, reset/overwritten by bridge entry/failed(), while a real
finalizer can enter another bridge during projection or Lua error allocation.
Move those per-invocation values into the same native frame or explicitly
preserve/restore them with an equally precise lifetime. A final error-string
allocation must be protected while the frame's bytes remain alive, then raise
the already rooted Lua error after destroying C++ holders. Copying into the old
shared Runtime buffer before an allocating Lua push does not establish this:
reentry can replace that buffer during the allocation. Require an actual primary
native error plus nested finalizer bridge oracle, including allocator failure
during error preparation, preserving the original business failure.

BR2: pin protected result/error preparation in the site table and bank. The plan
currently reserves4803/4804 only for successful result projection, yet correctly
requires protected Lua preparation for failed dispatch too. Either define that
pair for both actual preparation modes with a native mode field and corrected
credit, or give the actual error preparation a separate finite pair. Do not
manufacture successful projection after native failure. Entry/claim/frame
construction errors also need native-only catch containment before returning
through a Lua C callback; no new C++ exception may escape during setup. Similarly
prepare a preinstalled protected shim or otherwise account for any allocating
Lua closure/stack operations used to ENTER the pcall. No owning C++ automatic
may cross an unprotected luaL_error/lua_error or stack-growth transfer.

Placement clarification, not a third defect: current Runtime::load already
returns Loaded{lua_status,reference}.4824 and4830 must use THAT actual1506 ref,
not a last TLS ref or another module's source; no API extension is needed. Source
capture precedes the loader, environment pair precedes setupvalue, returned
CString follows the actual setupvalue before later work. The text-only first
_ENV upvalue justifies its5-byte maximum; an unexpected width must fail honestly.

The proposed maximum native bodies114/57/44/48 and module114/70/42/53/64/84/54
are otherwise consistent with the stated native widths. Recalculate the bank
for the resolved preparation declaration. Keep actual cache/loading values and
short circuits, cached false, nil/error rejection and explicit retry semantics.
Module frame nesting must survive reentrant imports; returned status is actual
pcall status, with successful import requiring LUA_OK. Projection reads existing
Values; it adds no encoder/visitor or metric transform. Source/body claims occur
before the existing C++ copies, and final origin checks follow paid terminal
publication so a recording failure cannot expose a successful return.

Resolve these in the SAME plan, then genuine native red, implementation/direct
checks and ONE whole code/oracle attack with finite fixes. No plan rereview.
Standalone representations/storage ownership4900–4999 and candidate56 remain
separate subsequent conceptual units; this unit cannot claim them implicitly.
