# Authored raw emission: one code and oracle attack

2026-10-11. Read raw_authored.hpp/cpp; Runtime trace, selected load/protected call,
strict emit, private module closures, native Error userdata, install handling and
joined-owner destructor in coding.cpp; retained root return and generation fields;
and the complete native/Lua oracle. Candidate's latest native fixture passes2.13s
and all four changed TUs pass narrow static checks. This is the ONE integrated
code/oracle attack, followed only by finding fixes and affected direct recheck.

## Finding: generation and END oracle does not fully bind successor outcomes

In generations_and_sources, every metric matches some VM by lane/reference and
copies that VM's generation. Value70 additionally checks that VM's retained source
against the independently supplied first program. Values71/72/73 only check their
value range, not that their matched VM retained the actual second program. A
writer attributing all successor work to the old VM can therefore pass that part
of the oracle. The lane inequality for value70 adds no evidence beyond distinct
VM lanes. The END oracle counts two records but does not require one actual
referenced END per VM; duplicate END of one VM plus a missing other END also fits.

Replace the weak statements with the direct owner oracle: for each actual metric,
resolve its VM's real retained source and compare70 to the original first source
and71/72/73 to the original second source. Require exactly one END whose native
R matches each VM allocation R and whose emitting lane is that VM's retained lane.
Compare its original close branch/error fields and ordinal after that VM's actual
emissions. This strengthens the existing oracle, rather than testing that test or
adding a second review. No production defect is asserted from this oracle gap.

## Source boundaries checked

Bodies copy native Lua integer/number/bool-int/string representations, immutable
G and actual VM R; no coercion, serializer, dictionary, duration or callback lookup
is on emission.86 covers maximum Lua refusal; native refusal74 fits.175 setup,
161 selected evaluation,53 validation and92 module execution banks cover their
actual fixed returns. Span/body arithmetic precedes data acquisition. Claims end
before new userdata/error jumps; protected install catches C++ admission errors
before raising a Lua error and records the actual pcall status.

The private Error object avoids borrowed string lifetime/GC identity bugs and
preserves deliberate rethrow without attributing a later unrelated Lua error to
a caught metric refusal. New metric capacity refusal does not set the business
capacity-stop flag. Retained handle precedes claims; actual destructor binds it
under current valid owner cancellation controls, keeps old G/VM, closes then emits
held END. A VM created without raw stays untraced under a later binding.

Source originals, retained root reference, selected function-pointer/load joins,
actual allocator null/ERRMEM/syntax branches, and full-lane real finalizers with
warning delivery installed/absent have direct native oracles. They do not claim
arbitrary Lua-created closure attribution, bounded arbitrary finalizer work,
concurrent use of one VM, or comprehensive Lua entry/warning instrumentation.
These limits are explicit and broader census owners remain open.

## Finding disposition

Fixed the existing generations_and_sources oracle. It requires exactly one each
of values70–73, resolves every metric's complete native VM R and original lane,
and reads that VM's actual retained source:70 equals the independently supplied
first program;71/72/73 equal the independently supplied second program. The
tautological lane inequality is removed.

Each VM must have exactly one END with its complete native R and original
emitting lane. The entire END body equals that R plus actual ordinary-close
branch1, without Error fields. END's original ordinal must follow every authored
record in its exclusive fixture lane; physical recorder partition order is not
used as a clock.

Affected native fixture rebuilt and passed1.90s. The changed test TU passes
narrow clang-tidy, format and diff checks. No production change or new review
was needed.
## Supported Linux static-Lua oracle linkage correction

The final supported Linux qualification in
context/linux/run-xtmoij2y/checks.log exposed duplicate Lua definitions at link:
the fixture's dynamic-library interposition names collided with the actual
Lua5.4.8 static archive's lapi/lauxlib definitions. This was an oracle portability
defect, not a production emission defect. The owning target now requests GNU
linker wrapping for luaL_newstate, luaL_loadbufferx and lua_tonumberx on Linux.
Its __wrap functions forward to the actual archive's __real functions; no Lua
return or numeric value is fabricated. Darwin keeps its actual RTLD_NEXT
interposition. The native allocator-null cut calls actual unwrapped lua_newstate
with the denying allocator on both platforms.

The changed fixture passed the Mac focused raw_authored check in2.14s; owning
test-TU narrow clang-tidy, formatting and diff checks are clean. Logs:
context/raw-authored-static-wrap-green.log and
context/raw-authored-static-wrap-tidy.log. Supported Linux linkage and actual
fixture execution await root's affected native qualification. No second panel.



## Full-gate native fixture lifetime finding

The old local_timing provider fixture declared its borrowed native Lane after
CodingEngine, so it destroyed the mapping before the new retained VM finished
closing. Actual macOS EXC_BAD_ACCESS shows RawProducer::emit → raw_emit →
raw_authored_publish → Runtime::Trace::finish → CodingEngine destruction.
The native binding contract requires its borrowed producer/mapping to outlive
the retained handle; Runtime deliberately retains that issuer past turn return.
Move the existing Lane declaration before CodingEngine. Keep all exact region/
provider fields and cardinality assertions, allocator hooks and capacities.
This corrects the actual fixture ownership lifetime; no VM event is suppressed
and no source guarantee is weakened. Direct same-oracle red precedes the fix;
run the affected fixture and narrow static recheck, then the required source gate.
