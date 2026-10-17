# Separate presentation Lua owner: bounded mechanism study

2026-10-16. Read-only source study plus the explicitly authorized ignored-context
Lua5.4.8 mechanism probe. No tracked file, index, Beads, source/build directory,
artwork, AFL state or real session changed. Root owns the next written plan and
implementation. This note proposes a contract; it does not qualify production
embedding, allocator failure, action dispatch or UI performance.

## Exact source read

* Existing CMakeLists.txt155 selects Lua5.4 EXACT; installed lua.h identifies5.4.8.
  CodingEngine::Runtime src/coding.cpp929–944 creates the turn VM and1000-count
  hook. install_impl1029–1060 opens base/coroutine/table/string/math/utf8 and
  installs native request/call/context/etc. The hook1770–1784 calls actual
  engine.poll_commands and cancellation. This owner is unsuitable for composition.
  Its protected setup/preparation and native-exception conversion techniques are
  useful; its authority and hook body are not reusable presentation APIs.
* include/blackbird/tui_components.hpp14–18 explicitly makes TargetRef a
  presentation reference. Candidate58–64, typed local CollectionState85–88 and
  Draft53–56 are native. SceneHost::publish src/tui_components.cpp303–323 rejects
  wrong view/base revision, validates and copies owned immutable properties;
  338–357 reconciles selection/anchor using the current native state;407–415
  swaps only after all allocating preparation. Composer remains the actual editor
  (terminal.cpp800 onward), so a second editable SceneHost draft is inappropriate.
* TUI_DOCTRINE49–54 excludes Lua from input/layout/paint;242–247 requires last-good
  native controls and explicit action/receipt identity across reload.
  TUI_COMPONENTS37–60 and160–170 supply owner/candidate/lifecycle requirements.
* Acquired Ito08c6a5ac8bdf72f3595db17cf2c32a10edca3f9f (MIT), manifest
  papers/tui-2026-10-15/uji/sources.tsv4. runtime/composition.lua31–73 gives stable
  keyed nodes plus generation sweep. controls/list.lua48–63 still scans all IDs,
  and arrange calls identities before visible rows, so its laziness is not a work
  bound. window/init.lua33–49 can redraw after focus settlement. Reuse the keyed
  identity idea, not Lua drawing/control callbacks on the terminal owner.
* Read official5.4.8 directly from intact
  ../quarantine_proj/archives/lua-5.4.8.tar.gz (SHA256
  4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae):
  ldo.c55–75 distinguishes C longjmp/C++ exception builds;882–909 permits hook
  yield only with zero results/no continuation; ldebug.c928–968 returns yielded
  execution to resume; lbaselib.c472–478 implements pcall with lua_pcallk;
  ldebug.c835–843 calls xpcall's error handler non-yieldably. lgc.c907–929 disables
  hooks during __gc and calls it non-yieldably. These are actual5.4.8 constraints,
  not assumptions from the older5.5.1 embedding proposal.

## Recommended owner and bounded data flow

One worker exclusively owns a VM, its rooted invocation coroutine, Lua heap,
module definitions and candidate builder. UI input/render never locks that owner
or calls Lua. Transfer owned finite native snapshots and stable scope/view/source
references; no Lua stack index, raw VM pointer or borrowed domain text crosses a
queue. One current invocation, one latest not-started composition request and one
candidate-result slot are sufficient for presentation. Supersession applies only
to obsolete presentation requests, never accepted actions or their results.

A ComposeRequest carries actual environment/conversation, ViewIdentity, definition
generation, monotonic request sequence, base scene revision, relevant local-input
revision, query/source revisions and finite observation payload. ComposeResult
returns those exact dependencies with native SceneCandidate or explicit failure.
UI accepts only the current view/definition/request/query dependencies. SceneHost
still checks its actual base revision and limits. Read/copy/publication costs are
charged separately; native publication intentionally copies candidate strings.
The current scene/control owner remains independent while Lua is busy or failed.

No implicit draft in initial Lua candidates: Composer is external. Native key/
selection/scroll continue during composition; preserve current compatible keyed
state at actual publication. A delayed result must not overwrite a newer selected
target or local state. Initial simple policy rejects a result whose relevant local
input revision changed, then requests latest composition; later dependency-specific
rebase can be a distinct design. Never silently retarget an old result body.

Use typed constructors for collections/items/inspectors/spans/finite notice/action
references and raw-type/raw-table access. Native traversal charges field count,
array count, total text bytes and nesting before copying; reject unknown fields,
cycles/nonplain tables and invalid typed handles. Candidate limits reuse SceneHost
Limits and actual bounded source windows. Do not encode general domain Value/JSON,
traverse retained history or call owner read/query methods from a constructor.

Lua persistent local state should be explicit typed native input/output with a
revision and publish together with the accepted candidate. Lua scratch mutations
must not become untracked persistent authoritative state after failed composition.
Use a fresh request environment/closure (or fresh VM if chosen) rather than claim
arbitrary mutable Lua upvalues can be rolled back. Pure immutable authored module
source can be captured once per definition generation; no filesystem require on
composition. Exact choice of source/heap/node/byte/request limits belongs in the
implementation plan and tests; do not invent latency or RSS guarantees.

## Instruction interruption decision and probe

Recommend one hidden rooted coroutine per invocation and a count hook which stores
native Budget/Retired/Stopped reason, then yields zero values to lua_resume. Do
not expose coroutine.create/resume/yield to scripts. On a hard instruction budget,
return Budget and retire that invocation rather than automatically resume an
infinite script forever. Recheck retired/stop in the hook too. Hook performs no
allocation, raw capacity wait, domain poll or user callback. Preserve the native
reason even if error formatting fails later.

This supports plain pcall with useful local error handling. A fatal lua_error
budget plus no pcall/xpcall is a simpler viable first profile, but is less
expressive. Fatal hook errors while exposing pcall are insufficient: a script can
repeatedly catch the budget error inside an outer loop. Exclude xpcall initially
because its Lua error handler is called through a non-yieldable C boundary.

Actual isolated hook_probe.cpp compiled with Homebrew LLVM23.1.2, arm64/Xcode SDK,
C++20, linked /opt/homebrew/opt/lua/lib/liblua.a. It roots each coroutine with an
actual registry reference, sets1000-count hook, yields at10 hook calls, invokes
lua_resume exactly once, then resets/unrefs/closes on the same owner. run.log:

| Actual source | status | resumes | hook calls | results |
| --- | --- | --- | --- | --- |
| while true do end | LUA_YIELD1 |1|10|0|
| while true do pcall(function() while true do end end) end | LUA_YIELD1 |1|10|0|
| above loop inside another pcall | LUA_YIELD1 |1|10|0|
| finite pcall sums1..20 | LUA_OK0 |1|0|1|
| error('actual-source-error') | LUA_ERRRUN2 |1|0|3|
| forbidden globals absent assertion | LUA_OK0 |1|0|1|

All cases returned normally; reset was0 for yielded/successful threads. Reset of
the actual error thread returned2, preserving its error status; do not treat the
three stack entries on a failed resume as successful results or invent a cleanup
success. Probe compile/run exit0. This directly establishes the proposed pure-Lua
nested-pcall yield mechanism under the selected installed runtime, not production
allocator/finalizer/native-callback containment. Exact command/hashes are in
metadata.json; build.log and run.log retain actual output.

## Initial supported Lua profile and callback hazards

Keep plain Lua functions, closures, tables, conditionals, loops, arithmetic,
concatenation, primitive assert/error/type/select/tonumber, ipairs/next/pairs over
plain tables, and pcall. Math may expose deterministic primitive numeric functions.
Useful finite string operations: len, sub, byte, char with bounded arguments,
lower/upper/reverse over bounded strings, plus an owned literal-only find/contains
helper with explicit byte bounds. Expose finite plain-table insert/remove/concat
only after validating argument/output bounds. Lua can author more algorithms in
pure Lua, where the instruction hook applies. Preserve original bytes separately
from safe rendered spans. Do not call arbitrary tostring to obtain constructor
text; accept strings/numbers through explicit typed conversion only.

Exclude initially:

* setmetatable/getmetatable, debug, package/require, dofile/loadfile/load/native
  loaders, coroutine library and raw registry access. No user __gc/__close or
  executable userdata/metatable supplied by the script. Existing libraries and
  native userdata expose no route to set those methods. Native-owned userdata
  finalizers may only release bounded memory on the VM owner: no Lua callback,
  domain call, filesystem operation or blocking wait. User-owned table values may
  contain a field named __gc, but no API can install it as a metatable/finalizer.
* table.sort (calls a Lua comparator through native C), string.gsub function/
  replacement-table callbacks, gmatch/find/match patterns and the potentially
  exponential native pattern engine. A finite byte length alone does not turn
  those into an instruction-preemptible native callback.
* xpcall's Lua error handler, generic tostring/__tostring, metatable __index/
  __newindex lookups in native getters, __pairs and Lua equality/order callbacks.
  Use lua_rawget/lua_next and exact tagged-userdata checks in constructors.
* print/collectgarbage and helpers with external I/O, unbounded formatting widths,
  native reentry or a call back into arbitrary Lua. Full stock libraries are not
  the supported profile merely because luaopen_base/string/table exists.

Native C calls are never preempted by a Lua instruction budget. Bound them by
native fields/bytes/algorithm and keep their callback surface closed. Protected
shims must contain allocating Lua API preparation too, not just final pcall.
In a C-built Lua, avoid any Lua error transfer across live C++ RAII owners. Catch
native failures around native work only; finish native owner scopes before Lua
error/result preparation. Preserve the original native outcome if conversion or
capture fails, especially at the eventual action boundary.

## Quiescence, reload and action authority

A generation moves accepting -> stop requested -> invocation returned/yielded ->
owned callbacks/resources settled -> closed. Only owner acknowledges quiescent.
Retire pending results immediately by identity, but do not free or replace a live
VM from another thread, detach an unjoinable worker, release generation pins early
or call an arbitrary native callback interrupted because the Lua budget expired.
The initial API has no user-finalizer path; otherwise __gc can loop with hooks
explicitly disabled, invalidating that shutdown claim. Even controlled cleanup
has actual status which must remain distinct from the source execution status.
Native host/last-good view and runtime action receipt inspection continue while
presentation retirement is pending.

Composition constructors have no effect authority. An effect-capable entry checks
an owner-held, nonforgeable invocation phase before queue acceptance; a script
argument/boolean cannot set it. Load/compose/preview rejects before actual mailbox
entry. Named action invocation carries current captured input origin, stable typed
target and view/definition/handler generation plus a fresh invocation key for a new
intent. Accepted envelope/actual receipt belongs to the existing domain owner,
not the VM; close/reload only retires presentation. Lost ack remains unresolved or
is inspected using the same key, never automatic repeat under a new key. Native
callbacks enqueue finite owned requests and return; they never synchronously wait
for provider/process/filesystem or raw capture admission.

## Direct behavioral REDs for the implementation

1. Actual old-main route cannot create the named Lua instrument; preserve that
   behavior as RED, not a missing symbol/compiler failure.
2. Actual Lua normal loop and nested-pcall loop with the profile return owner
   Budget after the configured instruction count; native key navigation and
   draft editing advance before/after it; last-good exact scene stays retained.
3. Source OOM/error/overcapacity constructor or missing source rejects without
   partial publication, dangling userdata or a false operation success.
4. A candidate begun for selectionA, then actual input selectsB/types draft,
   cannot overwriteB/draft. Wrong view/definition/query generation similarly
   rejects; result keeps original target/source instead of relabeling it.
5. Two actual request completions arrive out of order; only compatible latest
   result applies. A selected key moved among siblings preserves target/anchor.
6. Each native constructor receives hostile __index/__tostring tables/functions;
   it rejects without calling them. Finalizer/native-loader/coroutine/xpcall and
   comparator/pattern callback APIs are actually absent, not merely discouraged.
7. A trusted fixture-owned gated C callback shows the limit honestly: native UI
   continues, reload cannot acknowledge quiescence/close until that actual callback
   returns. No detach or arbitrary live-thread kill is accepted as cleanup.
8. Real effect attempt in load/compose/preview leaves zero mailbox entries. Named
   action accepted once survives withheld acknowledgment and view reload under
   its original invocation key; repeated delivery does not issue a second effect.
   Two genuinely new intentional actions receive distinct keys and actual links.
9. DATA/CONTROL recording refusal leaves accepted scene/action status truthful;
   no synchronous capture wait enters input/render or a presentation callback.

These are separate direct mechanism claims, not a request for another audit panel.
