# Current-session command center: integrated plan

2026-10-16. Baseline f2ab70b (native source 52f9103). Next bounded application
under arconaut-rhi.3, following the delivered command inspector. Read the
[task study](TASK_STUDY.md), [participant study](PARTICIPANT_STUDY.md), and
[episode study](EPISODE_STUDY.md). This plan precedes product implementation.

## Theory, purpose and limits

The [interaction theory](../../docs/INTERACTION_THEORY.md) predicts lower
coordination cost when declared intentions and observed execution remain visible
and related without forcing the operator to reconstruct them from chat. It also
predicts harm when a polished overview implies completeness or shared authority
that its sources do not have. The [TUI doctrine](../../docs/TUI_DOCTRINE.md)
therefore leads to three separately named source cuts, stable typed references,
read-only inspection, explicit missing/stale coverage, and one recoverable draft.
The SR-71 study motivates reducing reconstruction and mode ambiguity; it does not
supply a measured workload threshold for this application.

The useful episode is returning to a blocked task, seeing a failed command and a
waiting participant (including an unbound participant), reading actual detail,
and returning to the same draft. Browsing must not execute, message, join, stop,
assign, archive, or mark work done. A declared owner label is text, not a runtime
binding. The center is the current process/session, not a global scheduler or a
remote-team implementation. Explicit participant task_id is a relationship;
commands currently have no retained task binding in their observation contract,
so this tranche presents their actual ID and lifecycle without inventing that
relationship. An expanded relation index is a separate future owner unit.

The acquired Ito keyed controls and Codex center source read in the studies
supply identity, coverage and narrow-layout cases. They are reference material,
not dependencies. Rhizome's current C++ rigor design-constraints section reinforces
owner isolation and bounded geometry; its project decisions do not transfer.

## Owners and finite costs

* TaskState adds a pure sequential fixed observation: at most8 catalog entries
  plus independent selected detail, native canonical task IDs, revision/offset
  cursors, <=8960 copied text bytes. Existing TerminalUI mutex protects its one
  existing derived TaskState. A try-lock adapter supplies busy/initialization and
  the last successful retained-publication stamp. Known failed publication is
  explicitly stale; no engine task access or autonomous replay RPC is added.
* Participants adds a shared observation endpoint containing the product mutex
  and nullable owner pointer. It projects guarded const fields directly, at most
  8 rows plus selected4096-byte text page. No second roster/result copy, drain,
  journal read, callback or thread-handle inspection occurs. Destruction retires
  the pointer under that guard before cleanup. Missing lifecycle/provenance facts
  get small native caches at their actual transitions; cleanup_closed publication
  moves under the guard. Arbitrary legal128-byte participant IDs stay legal.
* Commands reuse the delivered bounded try_observe without changing4087 or its
  byte/source protocol. No new execution or task-binding machinery is implied.
* CommandCenter owns one immutable scene and the existing CollectionInspector,
  local per-kind catalog cursors, selected typed key and result reading offset.
  The catalog contains at most8 tasks,8 participants,8 commands in that order.
  Counts and page coverage are per owner; the cuts are not simultaneous. Fixed
  native result storage is intentional; presentation allocates only after the
  finite owner copies, outside input and paint. Shared controls do no owner work.

For each source busy/unavailable preserves its own last successful page while
other ready sources may advance. UI adoption generation is not a domain revision.
Scope contains actual environment/conversation IDs and a live view incarnation;
participant cursors additionally retain their real reader incarnation. Selection
never promotes an old result into the body of a new target.

## Controls and presentation

Ctrl-X toggles the center; /center opens its first pages. Ctrl-O and /jobs retain
the command inspector. Only one route is visible; switching retains each route's
local selection, page and reading anchors. The sole Composer owns draft/cursor.
Arrows/Home/End/Page/Tab/Enter/Escape retain shared inspector meanings. Ctrl-Left
and Ctrl-Right page the selected kind when collection-focused, selected result
bytes when detail-focused. Foreground private PTY input still precedes Composer;
paste remains literal. No second editor, search/filter or effect controls.

Use existing split geometry and explicit kind-prefixed row labels. At logical
width100 content begins row2, collection width33, divider33, detail origin34.
Tiny view follows existing terminal branch. Selected detail includes declared
status or observed phase, exact identity and actual coverage/provenance states.
Controls/control characters go through safe terminal text; originals are copied
and recorded unchanged. Add two deliberate readable baselines, retain all16.

## Recording before adoption

New4088 DATA records contain finite literal typed observations, query/scope,
source statuses, successful task publication source/fact/revision/ingress,
participant raw/original/semantic references and selected bytes, and existing
command source extents plus copied bytes. Latest task publication is a projection
chain reference, never falsely a byte slice for every row. Missing references
remain missing. New4089 CONTROL records link the actual4088, view generation,
input origin and actual adopted/refused outcome. Serialization uses explicit
native fields and lengths, no padding dumps, JSON or Value re-encoding.

Try-claim exact DATA and CONTROL extents before either record or scene mutation.
On refusal keep the previous scene/applied reference with stale recording notice.
Prepare finite scene, emit the literal observation, publish the candidate and
emit its prepaid outcome; only successful publication advances applied state.
Unchanged observation bytes do not create repeated adoptions. Existing4087 stays
unchanged. No ordinary waiting claim is permitted on this refresh path.

## Direct work and checks

Owner lanes first capture behavioral RED, then implement and pass native
semantic/bounded-work/lifecycle oracles. Task RED is actual old read allocating;
participant RED is a genuine unavailable seam failing required native semantics,
not a missing symbol. Integration RED uses actual old main, nonempty mid-cursor
draft and Ctrl-X requiring COMMAND CENTER before any effects. One independent
concrete review per owning unit, fix findings, no third assurance layer.

Integration checks actual main with pinned Microsoft cells on Ghostty and
Alacritty, real local fixture commands/participants, per-owner state distinctions,
no browsing effects, draft/route continuity, resize and literal paste. Native
adapter checks real recording capacity refusal, retained last-good source and
literal/source decoding; existing blocked-output episode extends to center.
Affected ASan/UBSan and TSan exercise native ownership/destructor schedules.
Linux profile remains Clang18.1.3/libstdc++13/Lua5.4.8. Required normal source
commit hook runs complete Debug and owned C++/Lua lint with stable tracked files.

After owned builds/checks stop, run one matched before/after Release timing round
using the previous immutable inspector binary and the same observer/scenarios.
Keep every sample/tail/failure, derive deltas outside producers, append the real
TAB ledger and record shared-host limits. Common scenarios do not establish
center interaction latency. No AFL inspection or additional fuzz operations.
Commit and push the work branch, update issues/journal and supported Beads backup;
no main merge or running-process activation. Lua bridge and later views remain
under their existing parent scope.
