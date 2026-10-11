# Native and Lua authored emission

`blackbird.emit(local_site, value)` writes the native value into the raw audit
stream. Success returns zero Lua values. The native `raw_authored_emit` overloads
return the actual unsigned64 lane ordinal. Emission does no numeric conversion,
aggregation, metric lookup, Value/BBM/JSON encoding, or subscriber invocation.

Lua requires exactly two arguments and an actual positive integer site. An
actual Lua integer, number, boolean, or string is accepted. Strings retain their
length, embedded NULs, and high bytes. Boolean events contain the native int from
`lua_toboolean`; native bool has a separate site and its original representation.
Tables, userdata, functions, threads, nil, numeric string sites, and integral
floating point sites are refused without coercion or metamethod calls.
Oversized indivisible bodies are refused, without truncation or splitting.

Each event copies the immutable VM definition generation, VM allocation request
reference, local site, and original native value. Local sites share that VM
generation's namespace. They do not automatically name a defining closure,
module, or nearest loaded source. Dynamic Lua-created source has no promised
static attribution. Consumers may interpret preserved bodies and descriptions
after they leave the producer and recorder.

Root source retention returns the SourceReference written by its successful
append. Source binding uses that reference before VM allocation. Failed root
retention cannot allocate its new VM. Selected root evaluations, existing tool
wrappers, installed helper literals, module loads, and validation-only loads copy
the exact source bytes before the native load. Native load and protected call
request/return events are distinct, with actual Lua status values. A successful
load retains its actual `lua_topointer` result; module execution retains that
same native pointer under the VM identity. Validation does not claim execution.
The module helper captures two private C closures once, removes their temporary
public fields, and keeps its cache, errors, environment, and first result.

The VM captures its actual producer handle and capture policy at setup. A VM
created without raw capture stays untraced under a later raw binding. A traced
VM keeps its original generation and producer when replaced or closed by a later
joined owner. Its destructor temporarily binds that producer under the current
owner's valid borrowed cancellation/deadline control. It does not retain expired
turn FDs. Held claims die before the producer handle. Concurrent execution or
close of one VM is unsupported.

Before acquiring authored arguments, a call claims86 control bytes for its
maximum refusal. Once admitted, all argument and publication failures use that
claim. Capacity rejection caught by Lua does not stop unrelated business work.
Failure to claim before argument acquisition raises the original Error and closes
that owner's admission; no acquired metric or per-attempt refusal is invented.
The VM's held END records the admission failure. Later canceled calls stop before
argument acquisition and do not wait for new refusal credit.

Lua errors from this new API carry the original native Error in a private userdata.
The userdata is created only after native claims have been destroyed. The
selected protected-call boundary recognizes its actual metatable and preserves a
deliberate rethrow. A caught metric error cannot misattribute a later unrelated
Lua error. Its ordinary `__tostring` reports the error name; metric validation
never invokes it. Existing bridge error behavior remains separate.

On the qualified64-bit ABI, setup holds175 control bytes: allocation return41,
installation request40, installation return44, and END maximum50. A selected
load holds its maximum53-byte return before acquisition. Root/tool evaluation
holds161 for that return, execution request64, and execution return44. Module
execution holds92. Scalar integer/real bodies are72 bytes, Lua boolean68,
native boolean65, and bytes72 plus their exact length. Native refusal is74,
Lua refusal at most86. Immutable run descriptions name sites1500–1518, source
kinds, widths, optional fields, and branch values before activation.

VM END credit covers END only. Lua5.4 disables debug hooks while running
finalizers. Authored admission independently checks the owner's cancellation
control during actual `lua_close`; warning delivery is optional and is not the
preservation path. This does not bound arbitrary Lua finalizer computation or
claim a complete audit of all Lua call entries and warnings.

The direct fixture reads real preserved disk records and compares independent
native bodies and source references. It exercises native/Lua signed limits,
negative zero/nonfinite real bytes acquired from the real Lua API, binary/empty
strings, strict refusals, caught error versus rethrow, root/tool/module originals,
joined-owner generation/END lifetime, and immutable disabled capture. Native
allocator seams call actual Lua allocation/load functions to obtain null and
LUA_ERRMEM; real invalid source yields LUA_ERRSYNTAX. Retention capacity failure
is checked before allocation. The stopped-recorder fixture fills the original
lane before actual finalizers, cancels its real wait, and checks admitted refusal
versus unadmitted sticky cancellation and held END with warning delivery both
available and unavailable. Test-only interposition forwards actual native calls;
there is no production callback seam.
