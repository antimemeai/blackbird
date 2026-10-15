# Source map for the next reusable TUI slice

2026-10-15. Read-only harness study of the working candidate while its required
source gate runs. No implementation, build, test, dependency or runtime selection
accompanies this paper. The candidate presenter subsequently failed source
qualification and remains [inactive](../tui-next-host-2026-10-15/SOURCE_STATUS.md).
Source line references describe the studied candidate snapshot; the active native
files have been restored to its baseline.
Scope follows [Unit1/2](../../docs/TUI_IMPLEMENTATION_PLAN.md) and the
[component contracts](../../docs/TUI_COMPONENTS.md).

## What actually exists

| Seam | Present mechanism and implication |
| --- | --- |
| [SceneHost values](../../include/blackbird/tui_components.hpp) and [publication](../../src/tui_components.cpp#L166) | Checked view/node generations, keyed collections, focus, selection/anchor reconciliation, draft CAS and pinned armed target. Publication deliberately copies externally mutable candidate storage before swapping accepted state. There are no layout properties, styled control values, renderer or production TerminalUI adapter. Keys are currently unique across the view. |
| [ChatPresenter candidate](../tui-next-host-2026-10-15/CANDIDATE_SOURCES.json) | Candidate containing owner for exact final packet, staged grid and committed geometry. One in-flight frame; normal completion swaps grids, tiny completion invalidates comparison. This is the final transport seam, not a scene renderer or an action bridge. |
| [TerminalUI rendering](../../src/terminal.cpp#L2258) | Native layout still directly draws conversation, task pane, palette, status and artwork into ChatGrid. Width/height decisions and native control policy remain in this monolith. No SceneHost call occurs here. |
| [ChatView rows](../../src/chat_view.cpp#L385), [ChatGrid cells](../../src/chat_view.cpp#L466) | Existing safe styled text, Unicode clusters, width reflow, clipped display source and delta painting are useful owned machinery. They do not yet supply a selectable document/source map or an independent collection viewport. |
| [Composer](../../include/blackbird/terminal.hpp#L56), [feed](../../src/terminal.cpp#L870) | Current native byte decoding, paste/history/editing, cursor and palette are combined. Preserve this actual editor; creating a second editable SceneHost draft would introduce competing truth until an explicit revision adapter connects them. |
| [UI ingress](../../src/terminal.cpp#L1290), [main-loop drain](../../src/terminal.cpp#L1984) | Owned native messages and wakeup already move presentation into the UI owner. Tasks also maintain a derived TaskState and bounded64-row page. These are Blackbird adapters, not reusable toolkit dependencies. |
| [Task publications](../../src/main.cpp#L513), [task row formatter](../../include/blackbird/task_view.hpp) | Real snapshot/change/source/revision plumbing exists. Reuse owner IDs and task origins; replace duplicated pane interaction/rendering through shared controls rather than creating another task store. |
| [CommandJobs boundary](../../include/blackbird/command_jobs.hpp), [snapshot](../../src/command_jobs.cpp#L396) | Thread-safe read/request supports existing UI routing; only the engine drains/retains commands. Reads allocate/project under a mutex and the all-jobs read scans the registry. “Thread-safe” does not make an unrestricted read suitable for each input/render tour. Job ID/output_ref already identify a real command result. |
| [Participants ownership](../../include/blackbird/participants.hpp), [read](../../src/participants.cpp#L1118) | One owner controls/drains. `read` calls `drain()` before projection, so it is not an effect-free composition query. Current native publication reports task activity, not a complete roster. The registry has at most32 runs, but bounded size does not authorize entering it from another owner. |

The present [turn Lua Runtime](../../src/coding.cpp#L463) holds a CodingEngine
reference, installs native effect bindings, and belongs to turn execution. Its
[constructor](../../src/coding.cpp#L929) installs a1000-instruction interruption
hook; the [hook](../../src/coding.cpp#L1770) polls actual command work. Its
[installation](../../src/coding.cpp#L1010) already excludes io/os/package loaders,
but exposes `arco.call` and other turn operations. Reusing that VM for painting
would share execution ownership and domain authority. The existing protected
callback/error-conversion technique is source to study, not a composition API.

## Smallest useful native vertical slice

Build one bounded **collection plus inspector** composition, with a simple split
layout and narrow collection→detail route. Use it in two real Blackbird adapters:

1. **Conversation:** retain ChatView, the current Composer and startup/status
   artwork. Add a selected **command result** inspector using real command IDs
   and bounded retained output pages. This first tool inspector need not claim
   every arbitrary tool already has a typed UI result publication. The existing
   operation callbacks mostly carry display strings; do not scrape those strings
   into invented attempt references.
2. **Current-session command center:** combine actual task, command-job and
   participant observations in the same keyed collection/inspector. Keep declared
   task status, process state and participant state separate. Add a bounded owner
   publication for the roster; existing task activity alone cannot reconstruct
   it. Open detail, change focus, select, scroll and return are native local
   actions. Preserve the conversation draft/cursor and route-local anchors.

This supplies an actual second application of shared behavior before Lua
composition. It does not require a general tree widget, chart suite, new scheduler
or generic domain command dispatcher. Keep process stdin focus and global stop
on their present paths. Initial inspection/navigation does not need new effectful
row actions; those need the identified invocation bridge, not `take_armed()` wired
straight to arbitrary callbacks.

## Contracts needed before that implementation

- Add only the typed scene properties used by these controls: finite pane bounds,
  styled row cells, selected detail/source reference and explicit loading/stale/
  unavailable state. Define clipping, narrow fallback and local focus order.
  SceneHost's current node kind and item label are insufficient to paint these.
- Define native semantic navigation separately from Composer byte decoding.
  Collection selection and scroll use keys/anchors, not row indices; an inspector
  keeps the selected domain reference through reorder/removal. Local typing stays
  in Composer until its edit revision is connected explicitly to scene CAS.
- Domain owners publish immutable, bounded observations with session/environment,
  source identity, observation generation and completeness. A center snapshot
  need not pretend three owners were observed atomically. Accept results only for
  the matching view/query generation and display stale last-good observations.
  [Main wiring](../../src/main.cpp#L1470) already drains commands/participants on
  the current owning path; extend owner publication there and at actual update
  points, without a background thread calling CodingEngine concurrently.
- Declare page/result limits and outstanding slots. Task read's returned-page
  bound does not bound filtering's examined work; command/participant full reads
  likewise require explicit cost accounting. Render only supplied windows.
- Keep accepted scene revision distinct from ChatPresenter's completely emitted
  geometry. This first keyboard slice can avoid coordinate effects. Later mouse
  admission must pin acquisition-time geometry/target rather than reinterpret
  coordinates using whichever scene is newest.

## Lua dependency after native reuse

Unit2 still needs a **separate VM owner**, immutable view input, finite candidate
admission and native local-state revision checks. Its constructors can build the
same native controls just exercised by the adapters. Native invocation phase must
refuse effects during composition/preview before queue acceptance. Handlers need
origin/invocation identity and retained actual receipts across view reload; a
generic queued function is insufficient. Native callbacks stay finite and have
no synchronous filesystem/provider/process wait. A looping Lua composition must
leave controls and last-good content usable; native callback containment and
quiescence remain real limits. None of these contracts exists merely because the
turn VM already runs Lua5.4.8.

## Costs and direct oracles for the proposed slice

Keep the present two viewport grids and one active packet. SceneHost currently
copies accepted node/row strings and rebuilds maps; expose candidate/window costs
rather than claiming zero-copy publication. Share immutable retained command
payloads where their owner permits, retain only finite result windows, and avoid
cloning transcript/history per center refresh. Rendering costs should depend on
viewport plus supplied rows; domain query work is measured separately.

Direct episodes should specify: the same command ID/result appears through both
adapters; reorder/removal preserves or explicitly retires selection; route changes
retain draft/cursor/anchors; narrow detail returns to the same item; out-of-order
observations cannot replace newer ones; raw source links remain genuine; and
blocked output/delayed observation leaves native typing/navigation/stop usable.
Use readable screen expectations and Microsoft terminal scenarios for actual
presentation, semantic tests for references/local revisions, and matched tranche
performance samples for the changed paths. These are distinct questions. This
paper adds no new certification gate or review hierarchy.
