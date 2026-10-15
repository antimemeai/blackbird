# Command center, team, and mods: proposed Blackbird interaction designs

2026-10-15. Concrete task-centered views derived from
[INTERACTION_THEORY](../../../docs/INTERACTION_THEORY.md) and the
[source study](REPORT.md). These are proposed designs for review. None of the
new routes, named view actions, layout slots, or missing runtime operations below
is claimed implemented. Existing operations retain their current contracts.
Wireframes are illustrative data and spatial proportions, not measured terminal
captures. Text labels accompany color and symbols.

## Common interaction contract

Each view answers a different question: **Command center:** what work deserves
my attention and how do I reach it? **Team:** who is doing or awaiting what,
what have they actually received, and what remains uncovered? **Mods:** what
changes this environment's presentation or behavior, which definition is
effective, and how can I shape it?

They are routes into the same work, not new schedulers. A participant visible
in the center and team view retains the same actual owner and identity. A Lua
tool invoked from mods uses the ordinary audited tool path. No screen can
declare a remote effect completed or bypass an existing restart/recovery fence.

### References and operations

A selected item carries a typed reference to its actual owner, session,
environment, object kind and ID, plus incarnation/revision where meaningful.
Labels are human shorthand, not the dispatch target. The examples use prefixes
`S` session, `P` participant, `J` command job, `W` workflow-host task, `T` session
task, and `D` definition. These are notation in the design, not a selected wire
format or a requirement to replace existing IDs.

An operation descriptor tells adapters its name, target kinds, argument fields,
owning service, admissibility and result states. Human bindings, model tools,
Lua and later MIDI adapters submit the same domain operation. Their encodings
and observations can differ. Display-only operations—open detail, filter, compare,
pin, return—use the presentation owner and do not create model turns or external
effects. A model need not operate a screen to inspect or direct work.

Every row distinguishes observed state from user-declared work status. A task
marked blocked, a command still running, and a participant waiting for direction
are three different facts. A combined row can link them but cannot collapse
them into one authoritative lifecycle. Scope and observation completeness are
explicit; a cached disconnected owner is not a currently running spinner.

### Focus and the persistent draft

Opening a route preserves the conversation draft and its cursor/selection.
Initial focus follows the explicitly invoked operation: open-and-browse selects
the list; open-and-search selects its query; passive background updates acquire
no focus. Entering a text field sends printable input to that field; browse
shortcuts do not intercept it. An explicit global command action remains
available through an owned native route.

The proposed conventional browse gestures are arrows for list movement, Enter
for opening the selected item, Space for explicit multi-selection, `?` for
context help, and Escape for leaving the current local interaction. Actual
bindings are supplied by the binding system, remappable and shown from its
effective state. Tab changes focus region; status filters are named actions,
not an unrelated hidden meaning for the same focus key. Prefix/gesture/device
capability negotiation remains in the input design, not inside each widget.

Escape first ends a local prefix/search/edit interaction, then returns to the
previous route/focus. It never silently cancels work. Stop is a distinct domain
operation; its target and observed disposition stay visible. A dialog or pending
RPC must not require painting to finish before a promised native escape/control
can be serviced. Actual containment limits remain explicit.

Selection follows identity through new rows, regrouping and ordering changes.
If filtering removes it, the header states that the selected object is outside
the filter and offers return/clear-selection; it does not silently select a new
effect target. If an owner retires it, detail becomes a retained reference and
effectful actions refuse or require a deliberate new target as their actual
semantics dictate. A stale view is inspectable; stale content does not become
current merely because it remains selected.

### Geometry and detail

Wide layouts show collection and detail together. Medium layouts replace
secondary columns with a compact state line and optional detail. Narrow layouts
open detail as a route with a saved return relation; they do not squeeze every
column into unreadable fragments. Titles/paths can truncate in the overview,
with the full value available in detail/copy. Essential identity, uncertainty,
selected scope and action state survive compression.

Layout chooses these forms from actual available columns and rows, including
grapheme/cell geometry. The wireframes' widths are examples, not final global
breakpoints. Each route retains list query, grouping, selected references,
scroll anchor, detail subview and drafts independently of data refresh. A
repaint does not refetch, reexecute, or insert context.

## Command center

### Useful episode and views

The operator returns after autonomous work, finds a failed check related to a
blocked investigation, compares its actual result with the previous attempt,
sends direction to the right colleague, then returns to their draft. A second
episode starts useful work across several independent owners and follows its
results without keeping every transcript open.

The default scope is explicit: current session and any deliberately connected
workflow owners. Saved sessions can be browsed as retained sessions; this does
not attach a live process. Optional global/project views declare which sources
participate, which are unavailable, and whether their lists are complete or
paged. No universal count is synthesized by summing overlapping objects.

```text
BLACKBIRD   Command center                    local / blackbird   Sources: 2 live, 1 unavailable
Views: Current session | Connected work | Saved sessions       Filter: unresolved, failed, blocked
Search: [ parser                                      ]      Group: purpose   Order: recent change
──────────────────────────────────────────────────────────────────────────────────────────────────
Work / actual reference                   Observed state       │ Selected: J:check-42
Investigation · parser                                          │ Owner: S:main · local
  T:17  Explain recovery failure            declared blocked     │ Command: ./check-parser
> J:42  Parser checks                       failed · exit 1      │ Bound task: T:17
  P:8   Independent diagnosis               waiting direction    │ Request attempt: a:42
Build comparison                                                │ Result complete · originals available
  W:b7  Native build                         running             │
  W:j2  Join checks                          waiting 2 of 3      │ Failed assertion …
                                                                │ [Open original] [Compare attempt]
Source unavailable: remote workshop · last observed earlier      │ [Open task] [Inspect command]
──────────────────────────────────────────────────────────────────────────────────────────────────
Selection: J:42 · focus: work list         [Open] [Filter] [Direction…] [Stop…] [Help]
Draft preserved: “Compare the two…”        Current request still running · 2 accepted prompts queued
```

The last line reports the current conversation without making it the center's
only owner. Queue counts refer to the actual conversation queue; they are not
native-host pending-task counts. If no reliable count is available, show
unavailable rather than inventing one. The thematic project-news HUD remains a
different view from runtime work status.

```text
Command center · current session
Filter: needs attention   3 matching / 9 loaded
> J:42 Parser checks
  failed · exit 1 · linked T:17
  P:8 Diagnosis · waiting direction
  W:j2 Join checks · waiting
Source: remote workshop unavailable
[Open J:42] [Search] [Filters] [Help]
Draft preserved · focus: list
```

### States and transitions

| State / trigger | Required behavior |
| --- | --- |
| First open / no work | Distinguish loading from an actually empty owner list. Offer existing start actions and ordinary navigation; no artificial setup wizard. |
| No matches | Preserve query and scope; state the number loaded and availability of further pages. Clear filter is a view action. |
| Owner unavailable | Retain last known observations with source/uncertainty. Offer reconnect only through an existing qualified connection operation. Local owners continue operating. |
| Select / open | Detail reads only needed bounded data. Opening original uses actual retained locator. Opening a task or participant carries the same reference into its route. |
| New update while comparing | Update the overview; retain chosen historical attempts in the comparison. Offer explicitly selecting the new result. |
| Action admitted | Show owner receipt and accepted/pending/running disposition as appropriate; do not turn row green as completed. |
| Stop requested | Display request versus observed stop/unknown independently. A stopped join wait does not imply cancellation of its members. |
| Session open refused | Preserve center selection and current draft; display actual active/unsettled-work reason. Do not stop all work to make navigation appear seamless. |
| Multi-select | Display exact selected count, target kinds and hidden selections before invocation. Only explicitly compatible batch operations appear; per-target results stay attributable. |

### Actions and actual owners

| Intent | Existing operation or missing dependency |
| --- | --- |
| Inspect session task; change title/status/owner/note | Existing `tasks_read/edit` with correct base/item guards. Busy operator mutation retains current queue/boundary behavior. Owner label remains a label. |
| Inspect/direct/cancel participant | Existing `participant_read/send/cancel`; `/runs`, `/send`, `/stop`; direction IDs and admission outcomes preserved. |
| Inspect/control a command | Existing command-job read/input/signal/stop operations; target remains the actual stable job. Stop never implies shared-service governance. |
| Read native workflow task/join; request cancellation | Existing workflow-host `status/read/join-read/cancel` through a new UI adapter; host remains sole owner. Conversational integration is a missing adapter, not missing execution substrate. |
| Browse saved sessions / resume | Existing session catalog and resume owner; retain current switching fences. Center route adapter is new. |
| Reprioritize all work by dragging | Not an implemented global operation. Future scheduling priority requires its owning policy; local visual grouping does not silently change scheduling. |
| Inspect why attention is suggested | Proposed derived presentation using actual blocked/failure/unresolved/input-needed facts, with explicit reason. It is not a learned stress/workload claim or a metric emitted by producers. |

The center can initially be useful over current session tasks, participants and
commands; an external workflow-host column appears only after its adapter exists.
The destination includes connected owners and future peers, with actual source
scoping and completeness. This describes meaningful stages without claiming
every proposed row is immediately buildable.

## Team

### Useful episode and structure

The operator needs to know who is investigating a disputed fault, whether a
message was merely accepted or actually included in a subsequent request, what
context each colleague was given, which results exist, and where responsibilities
overlap or are unassigned. The team view exposes these relationships without
assuming that a parent/child tree is the communication graph.

The default roster groups by observed activity or selected purpose; creation
lineage is an optional relationship. Context, provider/model, workflow, runtime
location and role are independent fields where the owner exposes them. A role
is declared purpose, not proof of competence. The current participants are
context-only workers; the UI labels that capability rather than implying they
can edit the repository. A future tool-using peer advertises its actual qualified
capabilities.

```text
BLACKBIRD   Team · parser investigation                 Members: 3 observed · current owner S:main
Roster | Communication | Contexts | Responsibilities | Work         No shared-context assumption
─────────────────────────────────────────────────────────────────────────────────────────────────
Participant / capability                 Activity             │ P:8 · Kimi · context-only
  P:main Main conversation                request in flight    │ Purpose: challenge recovery hypothesis
> P:8    Independent diagnosis            request running      │ Environment: selected local evidence
  P:9    Trace comparison                 waiting direction    │ Origin context revision: c:18
                                                              │ Prior answer: retained original r:88
Declared responsibilities                                     │
  T:17 Recovery cause   P:8                                │ Direction m:12 accepted
  T:18 Trace contrast  P:9                                │ Included in request a:9 · answer pending
  T:19 Parser change   unassigned                         │ Latest actual request: [Inspect]
                                                              │ [Message…] [Read answer] [Cancel]
Overlap: two labels mention parser; no automatic conflict claim │ [Context sources] [Linked work]
─────────────────────────────────────────────────────────────────────────────────────────────────
Message draft → P:8 [Check the missing-ESC hypothesis before proposing another parser…          ]
Send disposition: boundary-delivered request; no rewrite of the request already in flight
```

The responsibility rows illustrate a future explicit relation. Today the task
owner is free text plus optional runtime task binding; until a real relation
owner is delivered, show those actual labels/bindings with their meanings.
Do not infer assignment from similar names. Do not display an invented percentage
of team coverage or equate responsibility coverage with code/test coverage.

### Communication and context detail

The communication view shows addressed observations: sender attribution, actual
destination, message identity, admission, later request inclusion where observable,
answer and any dropped/unresolved disposition. Existing participant directions
are owner-routed; these rows do not establish a peer-room transport. A sender
string is not cryptographic authentication. Future room/mail adapters must expose
their actual transport and identity semantics, including P2P/E2EE requirements.

Opening an accepted direction must show that it cannot rewrite a request already
in flight. Current join seals new directions and waits for accepted work; it is
not the casual action for opening a teammate's result. Read is the ordinary
inspection. Await can wait without sealing; interrupting that await does not
cancel the colleague. These distinctions appear in action descriptions and
pending state, without making every routine action a confirmation dialog.

Context detail shows available selected sources, their provenance, originating
context revision, and actual recorded request inputs. A new message, a displayed
artifact, inclusion in model context, and an acted-upon result remain different
events. Showing a task in two participants' panes does not merge their contexts.
Complete evidence may require opening retained originals rather than expanding
an increasingly large summary inside the roster.

```text
Team · P:8 selected
Kimi · context-only · request running
Purpose: challenge recovery hypothesis
Context c:18 · 3 selected originals
Directions: m:12 included · answer pending
[Message] [Answer] [Context] [Work]
Return: roster → investigation
```

### Team state and operations

| Condition | View behavior and owner |
| --- | --- |
| Participant first answer arrives | Show actual waiting-for-direction state and available result; do not mark linked user task done. |
| Direction queued / dropped | Keep message identity, actual acceptance and dropped reason. Never relabel a sent message consumed without the owner observation. |
| Cancel admitted but remote request unresolved | Show local cancellation and unknown remote disposition independently. No automatic new attempt. |
| Process reopen | Historical participant work can be inspected; unfinished work is unknown, not resurrected. A future durable workflow participant uses its own qualified recovery contract. |
| Participant joins/leaves future peer work | Preserve other participants and their contexts. Parent exit or UI disconnect is not proof peers stopped. Actual membership/connection owner is a missing dependency. |
| Spawn or change model | Existing start/configuration operations where supported. Current provider/model changes for existing requests cannot be fabricated as live mutation; proposal uses the actual future activation owner. |
| Reassign responsibility | Today a guarded task edit changes a label. A future explicit assignment/claim operation needs real concurrency, identity and task ownership contracts; a drag on a graph supplies none. |

Current participants and user tasks can support a useful roster/answer/direction
view now. Tool-using task-local colleagues depend on workflow F5; broader remote
team/seat interfaces depend on F11 and separately owned peer transport. An
IRC-style P2P/E2EE room is a destination, not something a roster widget can
implement by sharing a local store. No fixed lead, one-team limit, role taxonomy,
parent-only messaging route or per-spawn permission is added by this design.

## Mods

### Useful episode and scope

The operator and model create a small evidence-comparison instrument, bind it
to a keyboard phrase or controller, inspect its active and pending definition,
change its view while ordinary work continues, and recover if the new drawing
fails. A second episode asks why a command or key has its current meaning.
This is programmable presentation and behavior, broader than reusable skill
instructions and narrower than claiming a complete global extension ecosystem.

Inventory separates kinds: native view contribution, input mapping, named Lua
tool, callable workflow, module, context transform, feed adapter, skill/resource
package, or external tool connection where implemented. A package can contribute
several kinds without erasing their distinct effects and owners. Existing G8
modules are trusted Lua, not sandboxed third-party extensions.

```text
BLACKBIRD   Mods / instruments                     Session S:main · definitions revision d:31
Views | Controls | Tools | Workflows | Modules | Packages          Effective | Pending | Changes
──────────────────────────────────────────────────────────────────────────────────────────────────
Contribution / kind                 State                 │ compare-evidence · proposed view mod
> compare-evidence / view           effective g:4          │ Owner: UI extension host [future]
  compare-evidence / control        pending g:5            │ Active source: s:144 · source available
  research / workflow               effective d:31         │ Pending source: s:151
  parser_check / Lua tool            effective d:31         │ Applies after affected work concludes
  toolchain-feed / package           configured; not fetched│ Running uses: workflow w:9 retains g:4
                                                          │
Selected contribution surface                              │ Changes: target query + one binding
  Center detail / Evidence comparison                       │ Binding: <actual configured gesture>
  Read-only retained results → compare view                 │ Target: selected result pair
                                                          │ [Inspect source] [Edit] [Try display]
Recent fault: none observed                                │ [Inspect active] [Inspect pending]
──────────────────────────────────────────────────────────────────────────────────────────────────
Draft preserved · focus: contribution list      Showing future contributions requires their owner
```

The first two rows intentionally illustrate the future native-mod seam, not
currently available plugins. Present implementation uses actual tools, workflows
and module definitions; absent categories say no host capability rather than
displaying pretend active rows. UI-only layout parameters belong in presentation
state; changing execution or context behavior routes through the governing owner.

### Editing, testing a display, and activation

Inspect source reads the retained effective definition and provenance. Editing
creates a proposal, not an immediate change to old running programs. Complete
configuration proposals preserve unrelated fields and carry the current base;
conflicts return the actual current revision so the author can reconcile them.
Successful existing boundaries activate eligible proposals. Failed workflows keep
the effective configuration according to its actual contract.

Try display is a proposed preview over explicitly selected retained observations,
with effectful APIs unavailable in that preview context. It can exercise layout
and input mapping without pretending external work occurred. This requires a
real capability boundary in the view host; merely naming a pane Preview is
insufficient. Executing an extension's real tool remains an ordinary explicit
operation through its owner. A preview is optional exploration, not a mandatory
approval ceremony for every edit.

The future combined interrupt-and-apply operation is exposed only when its
qualified owner exists. It must name affected work, preserve earlier request/
effect identity, and display pending/interruption/applied distinctions. Until
then the view presents actual deferred timing and existing interruption as
separate operations; it cannot make them atomic with two button callbacks.

```text
Mods · research / callable workflow
Effective d:31 · source s:144
Pending edit d:32 · not active
Activation: after affected turn succeeds
[Effective source] [Pending diff] [Edit]
Used by: current turn retains d:31
Conflict/error detail opens here
```

### Failures, lifetimes, and input

| Condition | Required behavior and dependency |
| --- | --- |
| Invalid definition or revision conflict | Show actual owner error, retain the draft, preserve effective definition. No synthetic partial activation. |
| View rendering fails | Native host draws a small usable fallback with fault/source/generation reference; preserve navigation/draft and available core controls. General render-fallback host is new work. |
| Lua work runs long | Use actual execution containment and cancellation. Current pure Lua workflow hook exists; an indefinitely blocking native call is different. A frame timer cannot preempt arbitrary code. |
| Reload with old callback pending | Tag callback/result by contribution generation and target identity; retired work cannot overwrite a new view generation. Its already-issued effects retain ordinary settlement. |
| Session or route changes | Distinguish ephemeral view state, session-persisted presentation preference, retained executable definition, and external effect state. No unqualified shared get/set store becomes a multiplayer task owner. |
| Binding collision | Explain active precedence, scope, source and pending alternative. Offer editing through the actual binding owner. A mod cannot silently claim the global escape/control route. |
| Feed receive or source failure | Keep prior snapshot with stale/unavailable status; display is separate from fetch, context inclusion and station admission. No polling/model request merely from drawing a row. |
| Disable contribution | Future capability-specific deactivation reports whether it removes a view, retires a mapping, prevents future admission or requests cancellation. It cannot promise reversal of earlier effects. |

An extension is allowed to define expressive controls using owned primitives,
including MIDI continuous/relative/held inputs and feedback when their adapters
exist. It does not read the terminal stream independently or bypass named
target/effect operations. Restricting every mod to single-letter hotkeys would
miss the operator's brief; exposing raw device messages without lifetime and
target meaning would miss the theory.

## Capability/dependency map

| Proposed view delivery | Existing base | Missing whole owner or adapter |
| --- | --- | --- |
| Current-session command center | Session task, participant and command owners | Shared paged projection/navigation adapter and identity-aware detail routes. |
| Connected workflow-host center | Independent native host status/read/results/joins | Explicit connection/discovery/UI adapter; no launcher replacement implied. |
| Team roster, answers, directions | Current Participants API, selected request originals and user tasks | Team projection/detail route; honest link presentation. |
| Tool-using/durable colleagues | Existing colleague transports and native host | Task-local context/model loop F5, durable owner/recovery qualification. |
| Responsibility assignment or peer rooms | Free-text task owners and explicit runtime bindings; research contracts | Real relation/claim owner where desired; F11 and qualified P2P/E2EE identity/transport for independent peers. |
| Definition browser/editor | Effective/pending tool/program/workflow registries and current activation | Source/diff/editor route over native operations. |
| General visual/behavior mods | Lua modules/tools/workflows and package patterns | Owned view-slot registration, event/input dispatch, lifetime/state/fallback contracts and effect-free preview boundary. |
| Arbitrary event interception | No general current middleware contract established | Separate semantic owner for observer/transform/interceptor ordering and result/error/unknown-effect behavior. A visual mod API must not accidentally supply it. |
| Atomic interrupt-and-apply | Current deferred activation and separate interruption | Existing intended activation owning unit; do not implement atomicity in a widget. |

## Direct discriminating walkthroughs

These are task-level design questions, not claims that tests or operator trials
have run, or demands for another certification layer.

1. With asynchronous completions and filters changing, act on the selected failed
   command, open its exact result, send one direction, and return to the old draft.
   The alternatives differ in target mistakes and reconstruction, not cell count.
2. Compare a task label, its bound running job, and a colleague's answer. Identify
   which is declared status, which is observed execution, and which scope each
   conclusion supports. A green answer must not settle all three.
3. Send direction during a request, inspect actual next-request inclusion, cancel
   an await, then reopen after process loss. Each operation must retain its actual
   effect rather than being reduced to one Sent/Done label.
4. Add a useful new instrument spanning a selected result set, its comparison,
   and a declared follow-up operation. Evaluate whether the repertoire permits
   the method, preserves its meanings, and supports revision—not only whether
   familiar work takes fewer keystrokes.
5. Keep typing while a mod opens a pane; then deliberately focus it, remap a
   gesture, resize, and leave it. Input goes to the intended field; active and
   pending meanings remain inspectable.
6. Fail a proposed drawing and separately fail an effectful extension call.
   The first preserves core navigation; the second retains actual effect state
   without automatic replay. A pleasant fallback cannot hide a missing outcome.

The enduring doctrine should preserve these relationships while the chosen
layouts, key phrases and reusable C++/Lua components evolve. A single shared
native operation contract is the source of meaningful adapter equivalence;
screen similarity alone cannot establish it.
