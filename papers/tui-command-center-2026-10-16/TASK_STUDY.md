# Current-session task observation study

2026-10-16, source `f2ab70b`. Read-only owning study for the next command-center
tranche. No product edit, build, new dependency or acquisition follows from this
report. BLACKBIRD/AGENTS, the interaction theory, TUI doctrine/components, current
coordination view design, the delivered rhi.3.4 command inspector and current task
implementation govern the recommendations below. Root owns integration and will
agree the contract before implementation.

## The actual existing owner path

There is already one authoritative sequential task owner and one UI projection;
adding another task store or giving the UI a reference into the engine is
unnecessary.

* `CodingEngine` owns `TaskStore tasks_` and exposes it through `tasks()`
  (`include/blackbird/coding.hpp:100,155`). `TaskStore` owns `TaskState`, an AuditLog
  reference and the accepted-change callback (`include/blackbird/tasks.hpp:57–78`).
  Neither type is thread-safe by itself.
* `TaskStore::edit` prepares changes, writes the actual application record, then
  commits the task state before calling `changed(packet)`
  (`src/tasks.cpp:734–822`). The callback receives a retained delta, not a command
  to repeat the edit. Display callback failure cannot undo accepted work.
* Main obtains the initial task snapshot before terminal execution, passes its
  actual snapshot source and revision to `ui.tasks`, then routes accepted delta
  callbacks to that same method (`src/main.cpp:514–523`). The ordinary engine
  execution thread can invoke this callback while the terminal thread runs.
* `TerminalUI` already owns `TaskState task_state_` beside its existing mutex,
  dirty flag, task activity maps and pane navigation state
  (`include/blackbird/terminal.hpp:192–203`). `TerminalUI::tasks` restores the
  initial snapshot or replays a delta **under that mutex**, updates pane state and
  notifies the terminal (`src/terminal.cpp:1495–1526`). The source explicitly says
  “one mutable derived view, not a queued snapshot per update”.
* The current UI loop locks the same mutex, reads that projection and copies
  current activity into a pane result (`src/terminal.cpp:2129–2169`). This is the
  available ownership boundary for the new center. The new center must observe
  the existing projection, rather than call `engine.tasks()` from its own thread.

The smallest mechanism is therefore a pure bounded `TaskState` observation,
invoked by TerminalUI on its existing state under a **try-lock** outside input and
render. TaskState stays sequential; its contract requires the caller to own its
access. The TerminalUI adapter supplies Busy when it cannot acquire its existing
mutex and returns fixed copied data plus the publication stamp. There is no new
subscription queue, reducer, TaskStore, engine access or cross-thread Value borrow.

## Task facts, identity and actual current costs

The native state stores an item map, explicit ordered ID vector,16 remembered
retry receipts, five precomputed top-level populations and per-parent child
rollups (`include/blackbird/tasks.hpp:19–53`). Limits are2048 current items,
64 operations per batch and64 rows for existing tool reads. Task status is a
**declared work status**: queued, active, blocked, done or dropped. The five counts
count top-level tasks, whereas `size()` counts all tasks/subtasks. These must not
be relabeled universal work counts or command completion.

Current task IDs are `t` plus a positive canonical uint64 decimal ordinal:
creation is `"t" + std::to_string(next++)` (`src/tasks.cpp:245`), and restore
validates canonical spelling and `ordinal < next` (`src/tasks.cpp:569–581`).
Creation rejects next==UINT64_MAX, so the greatest possible current ID ordinal is
UINT64_MAX-1, at most21 bytes including `t`. This is a different identity type
from a command's32 lowercase hex bytes. A native TaskId ordinal can be formatted
into a21-byte stack buffer for heterogeneous lookup. Zero is invalid; “no parent”
should be optional rather than another task identity.

Canonical stored rows have nine fields: id, parent, title, status, owner, note,
blocker, version and updated. Admission bounds title512 bytes (nonempty),
owner128, note2048, blocker512, IDs/parent32 and disallows NUL
(`src/tasks.cpp:76–86`). A parent has one subtask level; edits validate parent
relations and keep a parent's ordered children together. A title or owner is
free text, not an operation target or authenticated participant assignment.
Version is an item guard; updated is the retained wall-clock seconds value, not
a fresh measurement or a duration calculated by the observer.

`TaskState::read` is unsuitable as the new refresh method:

* It constructs `RawTaskTrace` before validation (`src/tasks.cpp:627–628`). The
  trace's ordinary CONTROL claim can wait (`src/raw_tasks.cpp:11–20,23–31`).
  Passing preclaimed credit avoids that one wait but does not make the rest a
  pure observation.
* It builds a set of collapsed IDs and Value rows, adds rollup/parent-title fields
  and allocates an entire Value result. Selecting one task still loops the
  ordered vector until a matching row is found; sparse filters can examine all
  2048 entries even for limit1 (`src/tasks.cpp:647–715`).
* `TaskState::item` constructs `std::string{id}` for lookup and `position` scans
  all ordered IDs (`src/tasks.cpp:102–109`). Neither should be reused in a claim
  of allocation-free selected lookup or finite examined-row pagination.
* `snapshot` copies every current item, order entry and receipt into a Value
  (`src/tasks.cpp:517–550`). It is useful at existing startup, not on opening or
  redrawing a center.
* The existing integer helper formats `Number::text()` into an allocating string
  (`src/tasks.cpp:14–24`; `include/blackbird/value.hpp:28`,
  `src/value.cpp:146–180`). A new observer must not call it merely because the
  task number is small.

The actual production rows already contain native uint64 version/updated values
created by `number()`, and the binary packet codec preserves Number storage
variants (`src/packet.cpp:152–180,275–319`). Public restore also admits otherwise
valid integer-valued numeric representations. A no-allocation extractor should
read `Number::storage()`, accepting unsigned/nonnegative signed integral values.
For accepted Decimal values, bounded checked coefficient/exponent arithmetic
handles at most three limbs/20 decimal digits; for double, stack `to_chars` then
`from_chars<uint64_t>` matches the existing text-based acceptance without heap
formatting or out-of-range casts. Do not silently round an arbitrary double or
scan an arbitrary Decimal limb vector. Existing `token_usage.cpp` demonstrates
storage dispatch, but its generic all-limb zero scan should not be copied into a
bounded task query. This recommendation does not change task admission semantics.

There is one further bounded-work detail: current `restore` validates required
row fields but does not reject extra object keys. The new observer can explicitly
report Unavailable/unsupported row shape for a noncanonical row, without changing
TaskState's existing restore contract. Bound extraction to the nine canonical
fields and their admitted byte extents; do not scan an arbitrarily wide Value
object to locate them and then claim only eight examined rows. Normal current
producer/replay/snapshot rows have those nine fields. Decide this explicit
projection limitation before coding rather than assuming a stricter owner schema
than source actually enforces.

## Recommended first contract

Use a native task observation header independent of rendering. Retain Value
inside the existing task implementation for this unit; none crosses into the new
UI result or scene model. Refactoring the entire task domain model is not needed
for this observation boundary.

Proposed values:

* `TaskObservationId { uint64 ordinal; }`, checked positive; optional parent ID.
* `TaskObservationCursor { uint64 revision; size_t offset; }` for the existing
  task order. Query has optional cursor, optional selected TaskId and row_limit
  in0..8. Initial query has no cursor and begins at0. No arbitrary ID allocation
  or list scan is needed to interpret a cursor.
* Status Ready / RevisionConflict / InvalidIdentity / InvalidRange / Unavailable.
  The containing TerminalUI try-lock adapter additionally reports Busy and
  Uninitialized publication. An unsupported row shape/numeric representation is
  explicit Unavailable, not “no such task”.
* A catalog row has TaskId, optional parent, declared-status enum, version,
  updated, full fixed title[512]/owner[128] with byte lengths, and the owner's
  existing child-total/child-done rollup. A selected detail contains the same
  metadata plus full note[2048]/blocker[512]. Text is owned byte data with explicit
  lengths, not NUL strings or preinterpreted terminal columns.
* Result carries actual revision, total items, the five top-level populations,
  full list title[512]/bead[128], at most eight catalog rows, an optional next
  revision/offset cursor, and separate selected state NotRequested / Resident /
  NotResident. Archived tasks become NotResident; this makes no claim that their
  retained history vanished.
* Include actual examined_rows and copied_text_bytes coverage. Eight catalog
  projections plus one selected projection are allowed; selected detail must not
  shrink catalog pagination. The catalog directly indexes the ordered vector,
  does at most eight item lookups and never searches for an item by position.
  The selected item uses a transparent map lookup against stack-formatted ID.
  A row may also read its already maintained rollup; this is a bounded map
  lookup, not a scan of all children.

This first contract needs no filter or collapsed set. It faithfully exposes
source order and parent relations for the shared collection. If the center needs
a status filter in this tranche, keep a separate <=8 **examined order entries**
budget; next must advance past those entries even when zero rows match. A zero-
match page with a next cursor is not an empty task owner. Do not inherit the
existing read's scan-until-eight-matches behavior. Owner labels, note search,
folded hierarchies or locate-selected queries should not be smuggled into an
otherwise constant-work first API.

At maximum admitted text, copied list rows cost8*(512+128)=5120 bytes, independent
selected detail costs512+128+2048+512=3200, and list title/bead cost640: **8960
copied text bytes** plus fixed metadata. This is an intentional full-text bounded
result, not a claim to fit the command inspector's4096-byte source page. It avoids
presentation-specific truncation; safe text formatting/clipping belongs in the
adapter/components. There is no claim that every map search inspects only nine
nodes: at most2048 items bound tree work, while field and string copying have
separate declared limits. A zero-row query permits selected-only detail.

Require cursor revision to match exactly before projecting any catalog page.
After any task edit a later old cursor yields RevisionConflict with the actual
new revision, never a mixed page. Root can restart the catalog while preserving
the independent selected TaskId/detail reference; a page omission must not be
reported as domain retirement. Do not call the linear `position()` to reconcile
an anchor on every refresh. Initial scope is the current UI projection, not all
saved sessions, an independent workflow host or a global scheduler.

## Publication provenance and failure

`TaskStore::edit` supplies `RawTaskPresentation` only during its callback:
source1709, actual retained ApplicationRecordId and state revision
(`src/tasks.cpp:786–815`). Initial snapshot supplies source1708 and revision but
no single delta fact (`src/main.cpp:515–517`). `raw_ui_value_trace` records those
actual source/fact/revision fields in4050 and returns a linked outcome4051
(`src/raw_ui.cpp:231–259`). These are usable actual publication relationships.

They are **not raw byte slices for every row**. An unchanged selected title can
come from an earlier edit or initial restored snapshot; the latest delta might
change a completely different item. Initial1708 records snapshot metadata, not
literal bytes for its entire body. Do not attach the newest callback's reference
to every task field as if it directly contained that field, or invent command-
style2008 source offsets. Do not scan the journal to repair this in a UI tour.

Root should store one last-successful publication stamp beside the existing
TerminalUI task_state_, under the same mutex: initialized, resulting projection
revision, original RawUiValueOrigin and actual UI ingress4050 reference. Update
it only after restore/replay succeeds. Capture stamp and typed page together
under the try-lock. They describe a current derived projection of a retained
publication chain; they do not give the view authority to declare or edit work.

For an adopted task observation, root's adapter should try-prepay a finite DATA
record of the actual typed text/metadata plus current publication scope, query,
view and observation generations. Use native fields/lengths and actual copied
bytes; no Value re-encoding or JSON is needed. DATA refusal keeps the last scene
explicitly stale, without an ordinary waiting claim in input/render or refresh.
Recording the finite literal observation is the smallest honest route here;
command-result extents alone cannot be transplanted to this different source.
The separate actual frame still proves safe rendered cells. This is view
publication, not a new task fact or a producer metric transformation.

A callback failure is already caught after accepted task work
(`src/tasks.cpp:807–821`). If TerminalUI replay misses one delta, a later delta
cannot fill the gap because replay requires revision+1 (`src/tasks.cpp:475–479`).
Do not call the last successful UI projection “latest engine tasks” after known
publication failure. Keep its exact revision/source and explicit stale or
unavailable state until a real complete snapshot is supplied by its existing
owner. This study does not propose an autonomous snapshot RPC, replay retry,
audit scan, second authoritative store or broader delivery repair. Root's
adapter should preserve initialization/failure distinctions, rather than treating
its default empty TaskState as proof that the engine has no tasks.

Task runtime activity is a separate observation. TerminalUI's task_activity and
task_runs maps receive native bound-attempt publications under the same mutex
(`src/terminal.cpp:1529–1552`); CodingEngine produces declared task ID, actual
attempt and observed phase (`src/coding.cpp:393–455`). They do not mutate declared
task status. First task observation should not copy those whole maps or parse
phase strings into invented lifecycle enums. Existing command/participant
observers can remain independent sources; a later explicitly typed binding
observation is its own unit if the center needs that relationship.

## Direct RED and permanent oracles

1. Prebuild the old read request, refuse all allocations, then call actual old
   TaskState::read through an ignored adapter. It must fail the new claim that a
   valid empty/resident observation is Ready without allocation. A missing new
   symbol is not RED. Then require the real typed API to pass the same allocation
   cut, including maximum-width IDs/numbers/text and late selected lookup.
2. Apply actual accepted task packets using prepare/commit, snapshot restore and
   contiguous delta replay. Compare native fixed fields to specification values,
   including parent counts versus total items, notes/blockers, nonzero item
   versions and canonical uint64 identity boundaries. Observe a selected task
   outside the catalog page and verify archive becomes NotResident while its
   original retained edit remains. Observation must not advance revision, alter
   the owner's state or emit task1700–1713 records.
3. Populate all2048 allowed items, request the last eight ordered entries and
   separately select an early ID. Require exact order/values and <=9 metadata
   projections, no position scan and copied text within8960. Structural reorder,
   archive and ordinary edits invalidate the old revision cursor explicitly;
   last-good copied results remain unchanged.
4. Exercise accepted numeric variants and canonical nine-field extraction under
   allocation refusal. An exotic admitted row shape or unsupported state must
   return explicit Unavailable within the stated work bound, not be silently
   omitted or scan its arbitrary payload. Checked byte lengths preserve actual
   source bytes; adapter rendering tests handle control escapes and text geometry.
5. Root's integration oracle should hold the actual UI mutex through an existing
   controlled publication allocation and require immediate Busy from the new
   try-observation. A failed/contended publication must not lose accepted detail
   or change the draft. Use real main task edits during a running workflow and
   actual retained source/revision, not a second test-only task store in product.
6. Under actual exhausted UI DATA capacity, advance a real retained task delta.
   Native editing/navigation/stop remain serviceable, the prior accepted task
   scene/reference remain exact and visibly stale, and capacity release permits
   a later actual observation adoption. Decode its literal fields and genuine
   publication stamp. A ready unchanged redraw does not manufacture another task
   edit or claim an unrecorded new view adoption.

These are separate fault classes, not tests of tests. Parent owns the reviewed
contract, one implementation challenge and normal tranche source/screen/sanitizer/
Linux/performance qualification. No new gate or campaign is requested by this
study.

## Acquired reference read and transfer

Re-read the acquired Ito `lua/ito/controls/list.lua` identities/cursor path:
identities() walks the entire supplied collection before drawing. Stable domain
keys and independently retained selection are useful; that whole-list work is
not the bounded task observer. The pinned Codex coordination report's read of
`agents_overview_threads.rs` distinguishes membership from selected detail, while
its command-center/navigation studies distinguish unloaded, error, empty and
filtered results. Use that separation with Blackbird's actual task/publication
owners, not a new shared server or imported scheduler. Source pins and existing
restoration remain in the prior coordination report/manifests; no reference was
executed or adopted as a library.
