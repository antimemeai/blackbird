# First presentation Lua owner unit: concrete implementation subplan

2026-10-16. Adopted first reusable-owner subplan after mainline integration0bf6712. This
unit supplies asynchronous generic composition and accepted action envelopes. It
does NOT supply domain dispatch, delivery/receipt projection, instrument routing,
file reload, raw publication or complete Unit2. Root owns those later units.
No command/task/participant headers enter this reusable owner.

## Public native API contract

Names can change, semantics below should not. Header uses foundation, raw event
reference and tui_components only; never CodingEngine, TerminalUI or CommandJobs.

```
struct Dependencies {
  EnvironmentId environment;
  ConversationId conversation;
  tui::ViewIdentity view;
  uint64_t source_generation, request_sequence;
  uint64_t scene_revision, local_input_revision, query_revision;
};
template<size_t N> struct Text { array<char,N> bytes; size_t size; string_view view() const; };
struct InputRow { Text<128> key; Text<512> label; FixedTarget target; };
struct PresentationInput {
  Dependencies stamp;
  array<InputRow,8> rows; size_t row_count;
  // Two already bounded selected original windows; never a domain reader.
  array<array<byte,4096>,2> detail;
  array<size_t,2> detail_size;
};
class Script { // shared immutable owned original source
 public: static Result<shared_ptr<const Script>> create(string source);
 // max65536 bytes, no file I/O, source hash/generation supplied by caller;
 // creation only checks bytes; worker loads/validates returned module.
};
enum class Phase : uint8_t { module_load, compose, preview, handler };
enum class Completion : uint8_t {
  candidate, handler_finished, script_error, native_failure, allocation,
  instruction_budget, retired, stopped
};
struct ActionKey { IdentityBytes inbox; uint64_t trigger; uint32_t ordinal; };
struct ActionEnvelope {
  ActionKey key; Dependencies stamp; RawEventRef input_origin;
  FixedTarget target; Text<64> handler; Text<64> verb;
};
class ActionInbox {
 public:
  explicit ActionInbox(IdentityBytes unique_native_incarnation);
  Result<Trigger> new_trigger(); // checked monotonic native counter, never wraps
  // Trigger is an opaque native owner-issued token; Lua cannot create/alter it.
  Result<ActionPage> try_read() const noexcept; // <=8 copied accepted envelopes
  // No domain-success/receipt field and no effect executor.
 private: friend PresentationOwner; fixed8 slots, mutex, counter;
};
struct ComposeRequest {
 shared_ptr<const Script> script; PresentationInput input; bool preview=false;
};
struct HandlerRequest {
 shared_ptr<const Script> script; Dependencies stamp; Text<64> name;
 FixedTarget frozen_target; RawEventRef input_origin; Trigger trigger;
};
struct Outcome {
 Dependencies stamp; Completion status; optional<SceneCandidate> candidate;
 Error native_error; FixedDiagnostic diagnostic; size_t accepted_requests;
};
class PresentationOwner {
 public:
  static Result<unique_ptr<PresentationOwner>> create(shared_ptr<ActionInbox>);
  SubmitStatus try_compose(ComposeRequest); // try-lock: Accepted/Busy/Stopped
  SubmitStatus try_handle(HandlerRequest); // bounded discrete queue, never coalesce
  PollStatus try_take(Outcome&); // try-lock: Ready/Empty/Busy/Stopped
  void request_stop() noexcept; // atomic flag + worker notification
  bool quiescent() const noexcept;
  ~PresentationOwner(); // stop then actual join, never detach
};
```

FixedTarget is the exact TargetRef representation with bounded owner/kind/key
strings (owner256,kind32,key128 bytes), nonzero incarnation. Validate before
queue acceptance. An input index is1-based and refers only to supplied actual
InputRows. There is no global target lookup or domain authority in a row label.
Public compose mode is only compose/preview; Phase is internal and native-owned.
Outcome owns its native candidate, not borrowed Lua memory. Opaque Trigger checks
its inbox incarnation before handler acceptance. A different intentional trigger
gets a fresh counter; redelivery retains it. Each request within a handler uses
successive ordinals0..7 under that same trigger.

Inbox acceptance is an independent explicit fact, not a domain receipt. Same key
and same envelope returns its original accepted disposition without appending;
changed verb/target/arguments conflict. Full inbox refuses before acceptance.
Accepted envelopes survive worker/view/source destruction because caller owns the
shared inbox independently. No automatic eviction or destructive take in this
first unit: try_read copies the at-most8 retained entries. Thus steady-state broker
consumption/dedup-after-consumption is explicitly unavailable until next domain
unit designs it. Do not pretend this temporary first unit is an indefinitely
operating action queue. Root can choose an explicit broker acknowledgment API
with retained-key window later; that is a stated gap, not guessed exactly-once.

## Script surface

Module returns a plain table with compose plus up to8 named handler functions.
Names are1..64 literal bytes; raw type/rawget/rawnext validation, <=9 total fields,
no metatable traversal. Each invocation creates a FRESH VM/environment, installs
restricted profile, loads original source in module_load phase, selects compose
or handler, runs it, extracts owned result and closes on that same worker. Native
local state persists externally via stamped immutable input; globals/upvalues are
per-invocation scratch. A failed invocation cannot poison the next invocation's
scratch state. Existing effective source means immutable Script, not a live VM.

Positional typed constructors:

* `ui.rows()` returns native bounded count; `ui.key(i)`, `ui.label(i)` return copies
  from actual immutable input; `ui.detail(1|2)` returns exact finite window bytes.
* `ui.collection(key,title)` adds native collection, returns a checked numeric
  builder handle valid only for this invocation. No accepted scene changes.
* `ui.item(collection,key,label,inputTargetIndex)` checks handle/key/text/index,
  copies frozen supplied target, appends bounded native item.
* `ui.inspector(key,title,text)` adds native inspector with safe literal rows.
  Text rendering uses existing safe terminal text conversion, originals stay input.
* `ui.request(verb)` is only allowed in native handler phase; it takes the frozen
  handler target and native Trigger, never a script-selected substitute target.
  It attempts finite inbox acceptance and returns a local accepted/rejected fact;
  it does NOT execute or claim that requested domain work happened.

Module_load/compose/preview calling request sets sticky native phase failure
BEFORE inbox lock/admission. Any callback failure (wrong phase, capacity, invalid
constructor, allocation, inbox contention) is remembered natively even if pcall
catches its Lua error; no candidate is published from that invocation. An already
accepted handler envelope remains accepted if later Lua throws/OOMs. Preserve
accepted_requests alongside failure, never roll it back or report no effect.

## Initial finite capacities and their purpose

* Eight input rows: matches existing owner catalog cuts. Two4096-byte windows:
  enough for the planned two pinned command results without all-output/history.
* Source<=65536 original bytes; module<=9 fields/8 named handlers; each identifier
  <=64 bytes (input domain key may be128), label<=512. Explicit limits prevent a
  nominally tiny constructor from scanning arbitrary Lua graphs.
* Candidate scene Limits{4,32768,196608}, existing command-center budget. Four
  nodes allow two collections/two inspectors; eight row labels<=4096 bytes and
  both windows8192 bytes expand at most4x for control-byte escaping<=32768, leaving
  space for keys/targets/headings. Constructor charges node/item/string budgets
  before appending. SceneHost remains final validator. This also admits both
  newline-only4096 windows and their row/span overhead; test that actual boundary.
* Lua heap16MiB per fresh VM, source64KiB, hard100000 VM instructions at1000 hook
  quantum. These are proposed initial documented workload limits, not measured
  latency/RSS guarantees. Native input/candidate storage is separately bounded.
  The worker is allowed one active invocation, one latest pending composition,
  one ready outcome, and two pending discrete handler invocations. Reserve output
  space before starting a discrete handler; never overwrite an unread handler
  outcome or accepted action. Bound inbox to8 requests across all retained keys.
* Input and result channels use short native mutex try-lock. Busy is explicit;
  no UI caller waits for worker, allocator, VM, inbox, domain or recorder. Worker
  takes owned request then releases mailbox lock before any Lua or allocation.
  If unread result blocks progress, keep it and report pending, do not silently
  replace a discrete outcome. Latest pending composition may be superseded.

## VM preparation, interruption and retirement

Use actual existing Lua5.4.8 C-built ABI. Custom allocator tracks live requested
Lua bytes, checks subtraction/addition overflow, handles ptr==null osize-as-type
correctly, realloc failure preserves old ownership, free succeeds without budget.
Allocator itself is noexcept, no raw/callbacks. Prepare base installation/loading/
argument pushes inside protected C shims; root coroutine through actual registry
reference. C-built Lua longjmp must never cross live native RAII owners: callbacks
finish native scopes before lua_error; Lua result preparation is protected apart
from native catch scopes. No C++ exception escapes a lua_CFunction.

One owner-rooted coroutine runs module+selected function. Count hook stores sticky
Budget/Retired/Stopped and zero-result yields. Owner treats budget as completion,
not infinite automatic resumption. Actual5.4.8 research probe establishes one
resume escapes nested pcall loops after10x1000 hook calls, not general native
preemption. Fresh VM closes only after actual resume returned and native callback
scopes ended. request_stop is atomic/cooperative; destructor joins the actual
worker. No user __gc/__close/finalizer registration path, no detach/kill fallback.
Only native bounded memory-release userdata is permitted; no user cleanup Lua.
Instruction count does not preempt native compilation/allocator/text conversion,
which must have source/byte/field bounds and no waits.

Initial libraries: pure Lua functions/tables/loops/closures/arithmetic/concat;
primitive assert/error/type/select/tonumber; plain-table ipairs/next/pairs and
pcall. Omit xpcall, setmetatable/getmetatable, debug/package/require/loaders,
coroutine controls, print/warn/collectgarbage and raw registry access. Finite
string whitelist: len/sub/byte/char/lower/upper/reverse; owned literal-only find
with capped input/output. NO pattern find/match/gmatch/gsub, dump or format.
Strip original string library IN PLACE before authored code, because string
metatable.__index otherwise exposes removed functions through ('x').gsub.
No table.sort, move/insert/remove/concat initially (sparse shift/range hazards);
pure Lua can implement small bounded alternatives under hook. Omit math initially
or expose individually deterministic primitive funcs; no random/randomseed.
Use native raw access in constructors, not tostring/__index/__pairs metamethods.

## Behavioral RED -> implementation -> GREEN

Create public interface plus deliberately refusing implementation scaffold first;
compile permanent oracle successfully and require actual runtime failure on a
simple module which should compose eight real input targets/two literal windows.
That direct RED is not a missing symbol. Then implement this owner only.

Permanent direct cases: valid keyed scene and exact target/body; nested-pcall loop
Budget with last-good host unchanged; caught constructor/phase error still native
failure and zero accepted inbox envelopes; allocation denial during VM/setup/
constructor; two4096-byte control-heavy windows admitted or explicit exact budget
refusal without partial candidate; wrong phase in module/compose/preview; named
handler accepts once with original frozen target/key and preserves acceptance on
later Lua error; repeated same key idempotent/changed envelope conflicts/new trigger
distinct; fixed inbox full before append and persists after view/source/worker
retirement; pending composition supersession and discrete handler outcomes never
lost; stale dependency rejection by actual native adopter; cooperative stop and
actual join, no user finalizer route/string-metatable bypass. Source/candidate/raw
publication adapters and domain receipt tests are later owning units, not claimed
by this generic first owner. Owning tests/tidy/sanitizers and one independent
implementation review follow agreed coordination; no extra research/review panel.

## Root decisions before implementation

Adopt this bounded API and existing center scene limits. Compose mode may be an
explicit compose/preview enum; internal phase never comes from Lua. The first
unit has a bounded retained action inbox, no domain-success claim and no silent
recycling. The domain bridge must design explicit handoff/reconciliation before
claiming an indefinitely operating instrument. Keep unread discrete handler
outcomes, even when this pauses further handler start; native adapter must poll
results regardless of the active route. No domain effect is admitted by this
component alone. Root integrates raw capture and actual application later.

Names/files: include/blackbird/tui_lua.hpp, src/tui_lua.cpp,
tests/tui_lua_test.cpp. Root owns CMake changes. Permanent semantic tests precede
implementation; a successfully compiled refusing scaffold is labeled a new-contract
RED, not evidence the old main had this API. The separate actual-old-main route
RED establishes missing product behavior. No tests-of-tests.

Use a private ignored manual/unit build while other source owners edit, linking
qualified existing native libraries; do not mutate shared build/debug or CMake
without a lease. Root subsequently registers actual translation units and does
whole-source integration. Preserve actual flags, failures and source identities.
One root independent implementation challenge/fixes, owning sanitizers/profile
checks and ordinary installed full source gate; no additional review layer.
