# Bridge/module owner implementation and direct checks

The published-source baseline actually performed bridge JSON work and a module
host effect, then a cache hit, while preserved disk had no4800/4820 observations.
It failed the missing-observation oracle. Matching libraries, fixture and native
log are retained under `context/bridge-module-study/baseline/`. The ONE root plan
attack found BR1 shared primary/error-buffer reentry and BR2 unprotected Lua
preparation/setup; both are resolved in the same plan and implementation.

Each synchronous Bridge invocation owns its result, primary Error and completed
512-byte business error buffer. Its preinstalled protected projection/error/array
shim executes allocating Lua work under actual lua_pcall while those native
owners live outside the protected call. Reentrant finalizers receive independent
frames; primary dispatch failures survive failed error projection. Native setup,
dispatch and preparation contain all C++ exception classes before the final Lua
raise, with no C++ automatic owner across that raise.

Module imports retain existing yieldable Lua lookup/cache behavior. Fixed private
begin/observe/end seams use explicit native tokens in protected userdata. Strong
Lua locals/uservalues keep actual tokens and parents alive across Lua-level pcall
and yield; a private weak coroutine association identifies the current token.
Actual source-call1517/1518 remains a separate nonyieldable native boundary.
Abandonment, explicit coroutine close and actual VM close emit distinct honest
unfinished reasons; they do not invent completion or replay module effects.
A dedicated issuer is prepared only at first selected import, once per Runtime.
Finite token original/control acquisition never waits under its mutex. Paid
cleanup retains and binds that creating issuer after binding exit and on final
drop from another thread. Source1506 and call1517 references are linked rather
than duplicating their original bodies.

The operator's subsequent consolidation removes bank/request/link/terminal and
exception-runner duplication. RawProgramCall/RawBridgeTrace use the shared
RawEmission/raw_publish_linked/raw_call_run machinery. Program lifetime and
module issuer/token decisions remain at their genuine owners. Exact Program
wire/grants, Bridge264 and Module800 grants, native errno, failure-origin checks,
original bodies and coroutine semantics remain unchanged.

Matching strict Debug build of Bridge and Program targets passed with two
compiler workers: `context/bridge-module-study/consolidated-matching-build.log`.
The first complete Bridge native run passed:
`context/bridge-module-study/consolidated-native.log`. It checks actual cache
hit/false/nil/cycle/absent behavior, embedded NUL/UTF8 bridge bytes, two imports
interleaved across real lookup yields, one actual host effect per successful
module body, explicit close/collection/VM-close, five real native host exception
classes after an effect, forced finalizer reentry during result/error projection,
real Lua allocation denial preserving the primary native Error, denied protected
userdata setup with zero source effect, and cold creating-issuer/fullCONTROL
cleanup. Expected native fields/bytes are independently parsed from preserved
raw disk, not emitted descriptions or a second encoder.

The complete preexisting Program owner fixture also passes after consolidation:
`context/program-workflow-raw/consolidated-native.log`. The only added finite
Bridge check is exact chunk-body DATA versus one-byte-short funding: actual
RawModuleToken receives the original Lua extent and native CString including NUL,
with a real held claim leaving precisely the declared grant. The short case must
have no4826 original or subsequent caller action and one paid actual capacity
terminal. Its final run passed in chunk-final-native.log. The sole root code/oracle attack
then found BM1/BM2, resolved below; no workload/performance qualification or standalone SystemA
storage/codec coverage is claimed.

## SAME code-review finite corrections

Root's ONE code/oracle attack is bridge-module-owner-raw-code-attack.md. BM1
was reproduced against matching pre-repair archives by a genuine forwarding C++
operatornew cut at the actual RawModuleToken holder allocation. It failed the
missing paid-entry/outcome assertion, with no source effect (holder-red.log).
The repair constructs the admitted token in stack optional storage first, then
noexcept-moves its actual issuer/request/bank into the escaping holder. Failure
of that actual heap allocation closes the original paid request with native
allocation branch4, stage0, actual allocation Error and reason4; name is already
preserved, with no second request/body or protected userdata/load/effect. Shared
RawEmission move leaves its source inactive; no new acquisition/binding occurs.

BM2 tightens the same independent parser: VM/generation links to actual1500 and
1902/1903 retained root source identity; literal Op/argc and operation/module
parents; reached stages and native Error code/detail; protected preparation
mode/status/stack; actual1506/1507 load function and4830 environment assignment;
actual1517/1518 function/status linked by4834; complete literal module name counts
and original native CString including actual published revision. Real native
host exceptions additionally require their exact Error body, not only a branch.
Within-lane ordinals establish local order; explicit references connect distinct
lanes without pretending a global ordinal. False Lua outer pcall remains distinct
from native C++ return/inner Lua status.

Matching strict Debug BM1/BM2 build passed (bm-finite-build.log), then the complete
real native fixture recheck passed (bm-finite-native.log). Same static helper/
fixture strict syntax passed (bm-finite-syntax.log); confined Runtime formatting
was applied before that matching build. No second panel, broad campaign or
standalone SystemA allocator/codec implementation is requested or claimed.

Final shared API/H source matching: context/candidate-owner-raw/
whole-final-matching-build.log GREEN and whole-final-matching-native.log GREEN for
Bridge; Program's matching direct binary also passed under its own context log.
This is the required matching recheck after RawCallSites/static interface shapes,
not a new campaign or extra review layer. No standalone SystemA closure claimed.

## Same integrated source-gate foreign exception repair

The mandatory integrated gate found a compatibility regression missed by the
earlier Bridge fixture: the existing operation-native provider throws integer93
and requires that actual integer at its caller. The original bridge caught
owned Error and standard exceptions but did not normalize foreign exceptions.
The new catch-all correctly contained C++ execution before Lua error transfer,
but replaced the caller's actual foreign exception with `external_unknown`.
The Bridge fixture's old integer19 case had incorrectly expected that replacement.
Neither the existing operation oracle nor the native thrown value was weakened.

The same-unit plan/disposition is appended to the Bridge subplan. The confined
Runtime implementation gives each actual eval a stack-linked native exception
frame; each native callback retains an actual std::exception_ptr through its
own protected projection/error preparation and paid raw completion. It transfers
the pointer to the eval owner before the callback's C++ owners end and the final
Lua error jump occurs. Eval checks the retained exception after protected Lua
return even if Lua pcall caught that error, restores argument registry roots and
stack through its existing cleanup, then rethrows the original host exception.
Its predecessor eval pointer restores lexically for real nested Lua calls.
An already pending foreign exception blocks another native dispatch before
admission/effect. A finalizer in the same eval cannot replace an outer callback's
existing primary failure; an independent nested eval owns its separate frame.
The per-frame ptr is business exception custody, not a metric transformation or
new lifetime/registration platform. Raw sites/layouts/grants are unchanged;
owned Error and standard exception behavior stays as before.

Finite checks remain in this existing unit: the unchanged operation fixture
requires actual integer93; the Bridge integer19 case now requires that exact
foreign value, preserves the preceding actual file effect, and proves that Lua
pcall does not enable the later display callback. Its nested `call('lua')`
variant checks child-eval to parent-eval custody and two honest nonstandard raw
outcomes. No C++ exception unwinds through Lua C frames and no C++ owner crosses
the final Lua error jump. No Runtime header/public layout change or Root
RRC/Backstop/task publication edits were made by this owner.

The matching strict three-target build passed in
`context/bridge-module-study/gate-foreign-build.log`. The already running command
finished during the operator's stop/status hold with all three direct cases
green in `gate-foreign-native.log`: operation_raw30.53s,
program_workflow_raw21.21s, bridge_module_raw7.70s, total59.44s. No further command
or source edit began during that hold. The Program fixture's independent cwd
repair and prior CTest17.50s result are recorded in its implementation paper.

The operator's subsequent resume authorizes the same written integration
disposition and changed-TU static checks only. Analyze the current Coding Runtime
TU and the two changed native fixture TUs with the repository's existing
clang-tidy configuration, one static worker at a time; no new dynamic campaign
or Ninja build. An actual static finding must be analyzed and recorded before
a narrow owned-source repair. Root independently checks this same foreign
exception invariant and owns the final source-hook, journal and delivery.

The first current Coding tidy reports two owned static findings in
`gate-foreign-coding-tidy.log`. The finite repair plan is:

- SB1: the analyzer calls the pre-holder `offered = &*paid` store dead. Its
  actual exceptional use is important: capture refusal or holder allocation
  failure must close the paid stack token with reason4 and the actual Error,
  rather than its destructor's generic unfinished outcome. Remove the redundant
  early alias, then explicitly select `offered` or the still-owned optional
  `paid` at the existing failed cleanup branch. The heap/userdata alias remains
  unchanged after ownership transfer. Do not delete the paid failure outcome,
  start another request or synthesize a holder allocation result.
- SB2: two adjacent const Value references in `Runtime::invoke_tool` are
  interchangeable at the type boundary. Replace those parameters and the
  optional origin with one confined aggregate containing explicitly named
  definition/arguments references and origin. Use designated fields at the two
  existing actual workflow/tool callers. No Value clone, new source body,
  argument validation behavior, public header or holder lifetime changes.

Recheck the same changed Coding TU and its two native fixture TUs with the
existing tidy configuration after these narrow repairs. Their strict matching
compile/native verification can be supplied by Root's next required source
gate; do not start another dynamic campaign merely for static-only refactoring.

SB1/SB2 are implemented as planned. The final whole changed Coding TU tidy is
green (exit0) in `gate-foreign-coding-tidy-recheck.log`; the changed Bridge fixture
TU tidy is green (exit0) in `gate-foreign-fixture-tidy.log`. The Program fixture's
SB3 explicit presence-guard correction and final green tidy are recorded in its
implementation paper. All three source files pass the actual formatter dry check
(`gate-foreign-format.log`) and the scoped diff check. Only one static worker
ran at a time; none remains. No additional native run/build began on this resume.
The prior3/3 native result precedes these static-only cleanup/borrow/guard
refactors; Root's final matching build/source hook verifies the final integrated
source. All owned production, fixtures and papers are now idle for that handoff.
