# Current-session command center: episode and direct oracles

2026-10-16. Read against `f2ab70b483f2e2f87efb90229c17d0e947fb9de4`.
This is an independent episode/oracle contribution for the integrated plan. The
subsequent [integrated plan](PLAN.md) selects Ctrl-X and excludes any command/task
binding claim from this tranche; the oracle details below reflect those choices.
No
product code, build, test, library, provider account or running session was used.

## The smallest useful second application

An operator returns to a blocked investigation with an unfinished draft. A
command associated with the investigation failed; a context-only participant has
answered and is waiting for direction; another participant has no task binding.
The operator opens a current-session overview, distinguishes those facts, inspects
the actual selected work, switches briefly to command output, then resumes the
same draft and reading positions. Nothing is started, joined, messaged, stopped,
reassigned or marked done by browsing.

This exercises the theory's useful relationship, compatible reference and
continuity claims without requiring a new scheduler, search language, tree,
comparison engine, effect dispatcher or Lua presentation VM. A flat keyed
collection with kind labels and read-only detail is enough. Retain separate
task/participant/job states; do not invent a universal status or combined total.
The existing current-session task, participant and command owners define scope.
Saved sessions, independently hosted workflows, remote membership and mods remain
outside this episode until their real adapters exist.

The [theory](../../docs/INTERACTION_THEORY.md), especially mechanisms2–6, motivates
the episode. [Doctrine](../../docs/TUI_DOCTRINE.md) rules1–3/5/8/9 and the
[coordination design](../../docs/TUI_COORDINATION_VIEWS.md) require real references,
declared/observed distinctions, independent controls and a recoverable draft.
[Shared components](../../docs/TUI_COMPONENTS.md) supply keyed selection, explicit
focus and finite observations. The [implementation plan](../../docs/TUI_IMPLEMENTATION_PLAN.md)
already names this center as the second native application; the delivered
[command inspector](../tui-command-inspector-2026-10-16/PLAN.md) supplies the first.

## What the actual harness can publish

| Source at the study revision | Existing fact and consequence |
| --- | --- |
| [main.cpp](../../src/main.cpp), task snapshot/change and task_activity_native wiring around509–529 | Main supplies the real task snapshot, later packets and source/fact/revision origins. TerminalUI already owns a derived TaskState. Reuse that path; do not create another TaskStore or scrape the task pane. |
| [terminal.cpp](../../src/terminal.cpp), tasks/task_activity around1496–1555 | Task packets retain task identity/order. Activity carries task_id, attempt, operation and phase. The current activity path discards events with missing/unknown task_id and retains a selected live/last activity per task. It is not a complete participant roster or a complete historical relation index. |
| [coding.cpp](../../src/coding.cpp), CommandTaskActivity around393–467 | A command binding carries the real task ID and operation attempt. Exec's canonical ID comes from that attempt. This explicit binding can relate observations; a task owner label or similar title cannot. Phase is presentation text, so actual command exit/custody must come from its owner facts, not parsing that text. |
| [participants.cpp](../../src/participants.cpp), notify around238–278 | Current publication exposes run_id, task_id, phase, operation and cancel_requested. It does not publish a complete roster, result text, model fields, accepted directions or source coverage. Unbound participants are invisible to the present UI activity sink. A new bounded owner publication/observation is a necessary dependency. |
| Participants::read around1138–1171 and [participants.hpp](../../include/blackbird/participants.hpp) | Read calls drain, then locks/projects owner state; controls/drain belong to one owner. It must not be called from input, layout or paint. A registry maximum of32 does not make cross-owner access or the read's retention work safe. Main's existing idle path polls participants/commands on their owning route. |
| [command_observation.hpp](../../include/blackbird/command_observation.hpp) and [CommandInspector](../../src/command_inspector.cpp) | Pure try_observe already supplies at most8 catalog rows, one detail,4096 bytes and16 source chunks. The row contains actual lifecycle/custody/frontiers, but no task_id. A task relationship needs the explicit binding publication. Non-resident is not absent retained history. |
| Participants worker around665–707 | A successful answer normally becomes waiting, ready for direction. Completed requires later sealing/settlement. Merely opening detail must not invoke join and silently turn waiting into completed. Restored interrupted participants become unknown rather than live workers. |

No current main callback supplies the proposed whole center. The integrated plan
must name the finite participant observation and its owner-to-UI publication,
source/generation, retirement and initial-empty semantics. Its initial current
snapshot must include restored/unbound residents; waiting only for a later bound
activity event loses them. Owner work must happen outside keyboard handling and
painting. Keep task order, participant state and job residency separate; a
composite scene can contain independently observed cuts rather than falsely
claiming an atomic three-owner snapshot.

Each source needs ready/partial/busy/unavailable/last-good distinctions. A stale
roster does not make ready task/job sources stale or absent. Existing task read's
64-row page limit does not itself bound examined filtering work. Participant and
task projection costs must be explicit in their owning units. A new center must
not clone all task/activity history on each key or draw.

## Proposed local interaction contract

Use a new local `/center` route and a separate discoverable native open/toggle
action, preserving Ctrl-O and `/jobs` as command-result inspection. The integrated
plan selects Ctrl-X (byte0x18), ignored by the studied Composer. This is a clean
old-source RED stimulus.
The command path alone cannot demonstrate nonempty draft preservation because
submitting it intentionally consumes the command draft.

Reuse CollectionInspector semantics: arrows/Home/End/Page keys navigate the
focused collection/detail, Tab switches those controls, Enter opens detail,
Escape returns to editing, printable/paste input resumes the existing Composer.
Tab must not acquire Codex's unrelated filter meaning. This slice needs no
filter/search field, multiselect, effect buttons or implicit foreground action.

Keep center and command-inspector instances/local state distinct. A route switch
does not destroy or call first_page on the other route. Save each selected typed
key, list anchor, detail target/page, detail reading anchor and focus, plus the
invoking route relation. Composer stays the sole draft/cursor owner. Returning
from result inspection restores the center reference and reading position;
closing the center restores the prior conversation orientation. Background
publications acquire no focus.

Kinds belong in keys and TargetRef, for example task:t1, participant:<run>,
command:<job>, within the bound session/view incarnation. Labels and row offsets
never identify a different owner. A useful minimal ordering is tasks in their
declared order, then participants and jobs in each declared catalog order. Do not
invent cross-owner recency ordering from incomparable timestamps. More/loading
notices are not selectable work objects.

When a selected key survives refresh/reorder, retain it. On actual retirement,
use the existing deterministic-neighbor policy, clear armed state and explicitly
identify the lost reference. Last-good detail must retain its own target/source
or become unavailable; it must never be relabeled as the neighbor's evidence.
The shared mismatch notice is useful here. The read-only slice grants no effect
authority to the automatically chosen neighbor.

Input priority is the actual current one: foreground private-PTY routing occurs
before Composer and owns bytes including Ctrl-C/Ctrl-O/the proposed center byte.
Only the established unpasted Ctrl-B escape releases that foreground wait.
Outside PTY input, existing background/cancel/quit authority remains available.
Pasted route/control bytes remain literal text. Escape alone must not cancel work.

## Permanent real-main RED and complete episode

Extend the pattern in [check-tui-inspector](../../scripts/check-tui-inspector),
using the pinned Microsoft driver and an isolated native process. First, before
any seed effects, type an ASCII draft, move its cursor twice, send the new center
byte and require the literal COMMAND CENTER header. On the current source the
byte is ignored. Save that behavioral failure before production code; do not
accept a missing driver, setup failure or empty screenshot as RED.

For the full episode, use the already working local fake-Claude PATH adapter in
[check-participants-pty](../../scripts/check-participants-pty), not a made-up
participant backend or account call. A temporary claude executable returns the
actual result envelope consumed by the native transport. Replace timing sleeps
with fixture-owned ready/release barriers. The seed Lua workflow uses existing
blackbird.tasks.edit, blackbird.participants.start and exec wrappers:

1. Explicitly add a blocked task with a blocker and a free-text owner label.
2. Start one participant bound to that task and one unbound participant; give
   their requests distinct markers and record each returned real run_id.
3. Start a command with yield_ms0, without a UI task-binding claim. Its real child records one launch,
   writes a unique output marker, waits at a barrier, then exits7. Record its
   returned canonical job_id. The workflow emits the actual IDs for the driver.
4. Open the center with a nonempty mid-cursor draft while work is still live.
   Release the job/participant barriers at declared points. The failed job must
   not complete the blocked task. Successful participants must show waiting
   without the test calling join/await to manufacture that state.
5. Select the bound and unbound participants and the actual command, retaining
   keys across updates. Read the same command ID and output through Ctrl-O/jobs,
   then return to the center's prior selection/detail anchor and original draft.

The driver computes expected ordering from the declared kind/catalog policy and
the emitted real identities, not from whichever row happens to be highlighted.
Keep source bodies and actual IDs out of echoed seed text where an assertion
could accidentally pass on the input transcript. Assert within the center/detail
rectangles, not any matching text anywhere on screen.

## Oracles and the fault each directly attacks

| Direct case | Stimulus and decisive oracle |
| --- | --- |
| Actual source semantics | In real main, task remains declared blocked with its blocker; command detail reports actual exit7 and canonical ID; successful participant reports waiting; unbound participant is present with no task binding. A free-text owner label is never promoted into a runtime assignment. |
| Navigation has no domain effect | Capture fixture launch/provider-request ledgers and task revision before browsing. After repeated route/selection/focus/resize/scroll operations, the job launched once, the two explicit participant requests remain two, task revision is unchanged, and no send/cancel/join/archive/input/stop/foreground request was admitted by browsing. Normal owner settlement/publication is allowed to advance; new UI/raw observations are not mistaken for effects. |
| Stable identity and detail | Give task/participant/job rows identical human labels while retaining their valid actual IDs and distinct typed keys. Reorder/insert around selection, then retire it. Independently supplied expected keys and source references check the surviving selection, deterministic neighbor/lost-reference notice, and detail identity; no stale body is named as a new target. |
| Distinct state owners | Give one task blocked status, a linked successful command and a waiting participant. Assert all three facts independently. Counts describe loaded/per-source coverage, not overlapping work summed into one authoritative count. |
| Per-owner cuts and stale publication | Adopt newer task revision plus participant generation; deliver an older task page afterward. It must not replace newer task data or erase the newer participant data. A busy/unavailable participant source keeps last-good roster visibly stale while ready task/job changes still apply. No synthetic global revision is used to imply simultaneity. |
| Busy/recording capacity | Owning tests exercise actual try-admission/owner contention, not just a fake scene notice. Hold capacity unavailable, attempt refresh, and compare last-good scene/applied source/selection/anchor. No wait or unrecorded adoption; release capacity and adopt one matching page. Distinguish never-observed/loading from ready-empty and stale-last-good. |
| Actual provenance | Decode the new center adoption contract independently. Resolve task source/fact/revision, actual participant observation source, and existing command byte-range references to their real records. Compare selected IDs and source payloads with fixture originals; reject mismatched owner/view generation and missing required source. Existing4087 is command-inspector-specific, not a center provenance contract to reuse unchanged. |
| Draft and route anchors | Native semantic cases compare byte text/cursor and separate center/jobs selection/anchor/content_offset/focus before and after round trips and refresh. Actual main checks the visible mid-draft cursor before open and after return, then exact literal paste. Pasting Ctrl-X/Ctrl-O/Ctrl-C cannot open routes or cancel work. |
| Foreground/global priority | Reuse the real PTY child and effect/input logs from check-command-jobs-pty: while foregrounded, the new route byte follows private-PTY input and no center/command-inspector route opens; respect the child terminal's line-discipline interpretation of control bytes. Ctrl-C retains its child interrupt meaning; unpasted Ctrl-B backgrounds the same job without relaunch. Then native center opens normally and outside-PTY cancel/quit still reach their existing owners. |
| Blocked stdout | Add a center episode to the existing actual TerminalUI blocked fixture. Open it before filling the physical PTY queue; require the real writer's EAGAIN event. Send navigation/focus/route-return, literal draft bytes and cancellation without draining stdout; the existing separate cancellation pipe must acknowledge progress before drain. After release, require exact draft/cursor and coherent selected-source cells. This proves control progress under backpressure, not physical display latency or a pre-paint timestamp for each selection. |

These are distinct semantic, effect, source, service and presentation questions.
Do not add a second oracle whose only job is certifying another oracle. Owner
mutation/retention tests remain in their owning units rather than repeating a
domain lifecycle campaign inside a screen fixture.

## Microsoft cells and Neovim-style readable expectations

Keep the existing16 readable expectations unchanged. Add only center-wide and
center-narrow/stale cases using the native styled-grid producer and the existing
[screen comparator](../../tests/snapshots/tui/check.py). Pin literal state lines,
selected kind/ID, text, style markers and viewport bounds; review the actual
expected text as a specification. Snapshot emission alone is not acceptance.

For actual-main cells, propose keeping the current physical101-column layout
convention: logical width100, center content starts at row2. With shared split
geometry, list width33, divider column33 and detail origin34 are literal expected
coordinates. In a complete-ready fixture, header row2 and first item row3 provide
specific cells to assert; partial/stale notices deliberately consume their own
rows. The root's integrated layout contract must settle these numbers before RED
tests. Tests must not call the production layout routine to choose coordinates.

Use Microsoft cells/attributes to distinguish the selected marker from focus and
declared task status. Exercise32x12 collection/detail routing, then the existing
tiny branch (rows<8 or columns<12), then return wide with exact draft/selection.
One task title containing a terminal control verifies source-safe display through
the new adapter; the original bytes remain unchanged. Shared display-map/profile
qualification already exists; do not expand this into a new Unicode campaign.
Use only matching declared terminal profiles for any complex-cluster literal.

## Acquired references actually read

- Ito at `08c6a5ac8bdf72f3595db17cf2c32a10edca3f9f`: captured
  [controls/list.lua](../../quarantine/tui-2026-10-15/uji/source/ito/lua/ito/controls/list.lua)
  identities/cursor/arrange and [runtime/composition.lua](../../quarantine/tui-2026-10-15/uji/source/ito/lua/ito/runtime/composition.lua)
  keyed placement. Chosen/first identities survive positional change; however,
  identities walks all items and arrange can retry rows. This grounds keyed
  anchors, not an inference that lazy drawing bounds owner scans. Exact archive,
  license and restoration live in the [existing manifest](../tui-2026-10-15/uji/sources.tsv).
- Official Codex at `08e2b58b07b8a423d6577b66fb7756e980b53dbf`: captured
  [agent_center/navigation.rs](../../quarantine/codex/codex-rs/tui/src/app/agent_center/navigation.rs),
  [rows.rs](../../quarantine/codex/codex-rs/tui/src/app/agent_center/rows.rs),
  [input.rs](../../quarantine/codex/codex-rs/tui/src/app/agent_center/input.rs), and
  [agent_center_tests.rs](../../quarantine/codex/codex-rs/tui/src/app/agent_center_tests.rs),
  especially live_center_show_more_navigation_search_and_loading. Paging accounts
  for group headings; empty/loading/reconnect/error/no-match are distinct; narrow
  metadata drops deliberately; repeated Enter does not duplicate one in-flight
  load request. These are useful source cases, not Blackbird backend availability.
  Its Tab-filter binding is deliberately not transferred into shared Tab-focus.
  Pin/archive/restoration are in the [coordination manifest](../tui-program-2026-10-15/coordination/sources.tsv).

The references were read, not executed or adopted. The study proposes one useful
current-session browsing episode and its direct falsifiers. Root integrates the
owner publication design and final controls/layout, then assigns permanent RED
oracles before implementation; it does not need another framework or review layer.
