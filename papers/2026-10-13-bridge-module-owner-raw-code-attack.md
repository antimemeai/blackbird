# Bridge/module owner's one code and oracle attack

Root read the complete shared helper/token ownership, Runtime's protected bridge
frames, module private begin/observe/end/load/call and yieldable Lua wrapper,
setup/GC/close paths, the prior plan attack, and whole direct fixture including
ordinary/cache/cycle/false/nil, interleaved yields, abandonment, native exceptions,
reentrant result/primary-error preparation, Lua allocator refusal, cold issuer and
exact native chunk grant. No new workload or second review panel.

BM1: module_begin performs std::make_unique<RawModuleToken> before the token
constructor can admit/publish4820 or preserve4821. A genuine C++ allocation
failure at this existing new holder therefore leaves no module request or module
failed end, even though the selected blackbird.module call reached begin. The
current setup-refusal fixture cuts the later Lua userdata allocation and cannot
observe this earlier gap. Construct the paid owner in stack/optional storage
first, preserving request and name; then transfer its existing bank/issuer to the
escaping holder. If that actual C++ allocation fails, finish the still-owned paid
request with allocation/error reason4 before the native raise. Use shared
RawEmission ownership transfer; never create another request, registration or
body copy. Keep all C++ holders out of the final lua_error interval. An actual
forwarding allocation cut at the RawModuleToken holder must show exactly one4820,
exact4821 original, one4825 actual allocation outcome, no protected userdata or
source lookup/load/effect. Preserve the existing later Lua setup refusal as a
separate actual fault, not a test for this test.

BM2: validate currently parses then discards both request native fields and much
of the native return. It ignores4800 VM/generation/op/argc/actual operation and
module parents;4820 VM/generation/input type/parents; generic stage/Error code /
detail;4804/4833 native status/stack;4834 function; ordinary names' exact bytes.
The specific C++ exception tests match only the branch while the business catch
matches Error; a wrong native Error body survives. Tighten this SAME parser and
specific cases: independently known native VM/generation/actual call relationships,
literal module names and existing CString argument, reached stages and Error
code/detail, native protected setup/status versus outer Lua pcall bool, exact
one terminal per reached request and absence of unreached work. Correlate4834
function with its1517 actual native source and4824 with its1506 returned load,
rather than another encoder. Same-lane ordinals substantiate within-lane order;
explicit refs connect different lanes without inventing a global order. Case
specific expected fields must retain cache false versus miss, nil/cycle failed
outer pcall, honest abandoned reasons and the existing primary-error survival
oracle. No blanket assertion that every Lua-false outer pcall is a failed C++
return:4825 is the actual native end plus explicit actual Lua bool.

The shared-core consolidation has removed independent request/bank/publication/
terminal mechanics. Yieldable Lua pcall and strong token/parent/thread ownership
keep lookup yield/interleaving legal. Protected projection/error/array userdata
setup roots results while C++ owners remain outside Lua's longjmp boundary.
Each reentrant NativeFrame keeps its own Value/Error/message; actual C++ errors
remain separate from native Lua status and raw-origin failure. The dedicated
module issuer is lazy once per Runtime and permits only finite admission under
its existing serialization lock; cold paid cleanup preserves that creating issuer.
Source1506 and call1517 are linked without another source encoder. These grounded
properties do not replace BM1/BM2 finite correction and affected direct recheck.
Resolve both in this same one review; do not open another panel or qualification.


Owner disposition: BM1 genuine old C++ holder red is holder-red.log, not the
later Lua setup case. Stack optional paid token now emits its actual request /
name before holder allocation and transfers its one existing bank/issuer/ref
via shared noexcept move. The actual allocation cut emits allocation4,stage0,
Error allocation,reason4 exactly once, with zero userdata/source/effect. BM2
same literal parser now checks actual VM/generation/op/argc/parents, stage/Error,
protected status/top, exact names/CString native chunk, source1506/1507 and
function1517/1518 links. Matching native finite recheck GREEN:
context/bridge-module-study/bm-finite-native.log; strict matching link GREEN:
bm-finite-build.log. Narrow static diagnostic corrections preserve native fields
and share typed observation/return argument groups; final affected recheck follows
those confined source corrections, with no new review panel.
