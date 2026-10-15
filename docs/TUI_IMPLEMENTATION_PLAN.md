# TUI program: delivery plan

2026-10-15. Proposed implementation sequence for the
[doctrine](TUI_DOCTRINE.md), [components](TUI_COMPONENTS.md) and
[view designs](TUI_VIEWS.md). The original unit delivered research and reviewed design/plan; the source campaign
is now underway as recorded below. No new runtime,
library, controller or haptic device is selected.

## Campaign underway

Operator2026-10-15 authorizes implementation and explicitly adopts Microsoft
`tui-test` plus Neovim-style readable screen expectations, alongside all usual
rigor. Stable prior presentation is a deliberate regression baseline, including
known imperfections. See [tooling](../tooling/TUI_TESTING.md). The first source
unit adds these regression paths, the existing ESC prerequisite repair, and
native scene/collection/local-state primitives. Broader host/Lua/application
work remains in arconaut-rhi.3 until its actual contracts are delivered.

Run and record [matched performance deltas](../tooling/TUI_PERFORMANCE_FUZZING.md)
between each subsequent tranche. Expand/deepen scenarios when they answer a real
question; new cases establish their own baseline. Retain ordinary findings for
[collective bug action](TUI_BUG_QUEUE.md). AFL runs through the Python launcher on
Neuroses and is then left alone, following the operator's explicit instruction.

## Why this order

The operator asks for enduring doctrine, reusable machinery and forthcoming
surfaces alongside existing ones. The theory supplies the reasons: practical and
generative agency need an expressible repertoire; fluent control needs stable
meaning; useful action under competition needs independently serviceable controls;
joint work needs compatible references and repair. The result should make new
instruments ordinary to compose, rather than repeatedly polish one screen.

Choose work by dependency and useful episodes. First preserve native control and
output contracts; then prove reuse across genuinely different existing domains;
then add missing owners where future views require them. Hook authoring is an early
application because changing the instrument is a central intended use. Metrics
consumer work can proceed independently once the same controls exist. Broad MCP,
remote teams and durable agent execution cannot be supplied by a widget library.

This is a dependency-driven proposal, not a universal ranking of value or an
assertion of measured speed. Evidence from a concrete slice can change the next
slice. Do not wait for every future backend or a complete general framework before
using the shared machinery.

## Unit 1: native component host and preserved current controls

Extract a domain-independent native presentation/control layer above current
terminal custody, ChatGrid/ChatPainter and nonblocking ChatOutput. Define checked
view/node handles, native local state, typed action descriptors, scene/frame
ownership and async publication. Keep the Blackbird domain adapters outside it.
The existing writer's borrowed packet must gain an explicit containing lifetime;
retain complete pending suffixes and commit the painter only on complete output.

Separate terminal decoding from native editor/palette behavior while retaining
present draft/history/paste/editor controls, including literal insertion followed
by existing deliberate bare/POWERWORD workflow selection on submission. Resolve the known lone/incomplete
ESC recovery defect in its tracked owning unit (`arconaut-600`) before claiming a
new decoder correct. Preserve raw input capture/credit and terminal restoration.
Native focus, scroll, typing, cancellation and current result acknowledgment
remain available while composition or stdout is blocked.

Start only the controls required by the next unit: layout/pane/overlay, editor,
keyed virtual collection/table, inspector, action palette, status/error/async
boundary and shared text/style service. Charts, generic menus and specialized
controls should grow from real views rather than speculative convenience classes.
The toolkit may remain in this repository without a package release or public ABI.

Direct acceptance: independent expected editor/action event sequences, keyed
selection under reorder/removal, candidate failure preserving last-good state,
short-write/EAGAIN frame completion and resize, an acquired coordinate target
remaining pinned or refused after later geometry publication, tiny geometry, correct copy/source
mapping, actual work examined and retained payload ownership. Measure implicated
costs rather than announce a performance target from a primitive benchmark.

## Unit 2: Lua composition and two useful native-backed applications

Add a separately owned composition VM with typed constructors, immutable view
inputs, finite candidate/request admission and versioned local-state publication.
No Lua executes in the terminal/input owner. Expose async query/action requests;
no synchronous filesystem/provider wait in the UI bindings. Native invocation
phases distinguish effect-free composition/validation/preview from identified
action handlers before queue acceptance. Read-only view queries are owner-declared
observations; a remote `get` is not presumed effect-free. UI bindings cannot bypass
this through synchronous system execution or native loaders. Each operation carries
a per-invocation origin/key and retains its real receipt or unresolved disposition
across a closed/reloaded view; an ambiguous acknowledgment never causes automatic
resubmission or a new effect identity. A loop/error in
composition leaves native controls and last-good content useful. Actual native
callback containment and VM quiescence remain explicit limits; do not detach a
stuck thread and call that reload.

Apply the same components to conversation/composer/tool inspector **and** a
current-session command center over tasks, participants and command jobs. This
exercises document/editing, virtual collections, detail routing, async status and
shared target identity across different work. Preserve startup/status artwork,
current owner semantics, bounded originals, process-input mode and session fences.
The center gains a real source adapter rather than a second task/job ledger.

Deliver a small authored Lua instrument using those components over selected
retained results. It should arrange an inspection/comparison and a named action,
showing that an expert can construct a new useful working method. Document the
C++/Lua contract from this application; do not require every extension to mimic
conversation layout.

Direct acceptance: type and navigate during failed/delayed composition; publish a
candidate built before newer typing without overwriting it; resolve async queries
out of order; leave/return with drafts and anchors; act on a stable failed job
while nearby rows change. Reject effect dispatch from composition/preview before domain admission; withhold
an acknowledgment after owner admission and reload without repeating the effect.
Compare actual operation references across human, model and Lua paths. This proves semantics, not just matching screenshots.

## Unit 3: shared settings, definitions and initial team surface

Migrate sessions/drafts, tasks/forms, model/account settings and native callable
registries onto the collection/editor/inspector pattern. Deliver the Context/
request/original-repair workbench as a named application of the shared document,
source inspector and editor: retained context versus actual selected/final request,
guarded next-request context edits, bounded original recovery and conflict-preserved
drafts. Its context changes keep their own timing, distinct from governing program
activation; context metrics in Unit6 are a linked consumer, not a substitute. Show source/effective/
pending revisions, meaningful field errors and actual conflict responses. Build
team roster, selected answers/context and addressed directions over present
context-only participants. Keep role labels, explicit bindings and observed
execution distinct; do not fabricate an assignment owner.

Use one shared capability projection for existing tools/modules/workflows and
package contributions. Expose supported kind/operation/source fields; missing
catalog/account/skill owners remain unavailable. Definition editor publishes
complete native proposals with actual bases, preserving unrelated fields. Align
Mods and Packages routes on one contribution model rather than parallel managers.

Direct acceptance: conflicting edits retain user input; directions show actual
acceptance and later request inclusion; await versus join/cancel retain current
meanings; changing account/route cannot silently redirect a draft invocation;
opening a package or skill never executes it. Current APIs keep their limits.

## Unit 4: start-turn binding and action-fabric owner

Write and review the narrow owning execution contract before source work: event
identity and phase, pinned handler list, selection, order, scope, input/output
schema, original capture, failure/continuation semantics and dynamic cycle policy.
This is a domain execution unit, not code hidden in a view callback. Reuse current
retained program definitions and context operations where they fit.

Implement the useful initial episode in the [fabric design](TUI_FABRIC_VIEWS.md):
human or model authors a start-turn instruction/action, persists its configuration,
sees saved/pending/effective generation, finishes affected current work, then the
next eligible turn uses the pinned new binding without process restart. Idle
eligible authoring uses local publication. Generic file watching is not assumed
to be authoritative publication. Opening a hook editor never fires the hook.

Add event/binding editor, effect-free match explanation where truly supported,
dispatch history and source/request linkage through shared components. State
which hook kinds exist; don't advertise arbitrary interception from one observer.
Default activation needs no new approval. Atomic interrupt-and-apply remains with
its owning runtime unit and is exposed only after actual settlement/publication
semantics work; two UI callbacks are insufficient.

Direct acceptance: exact generation on current versus next turn; invalid candidate
keeps prior effective source; stale-base preserves draft; duplicate delivery
returns existing admission rather than repeats an effect; two intentional events
remain distinct; self-trigger/cycle refusal is visible; post-effect handler failure
does not replay work. Record unknown effects honestly. No raw metric emitter calls
these programmable hooks.

## Unit 5: capabilities, skills, recipes and external adapters

Deliver first-class qualified catalog and definition owners needed by the workbench:
reusable prompts/procedures/parameterized invocations, typed defaults and input
snapshots, scope/contribution rules and source/effective inspection. Keep saving,
resolving, inserting in context and executing distinct. Reuse actual native tool/
workflow routes before adding an external protocol where that covers the task.

Add versioned MCP tools/prompts/resources, integration-account associations and
known-infrastructure consumer inventory in coherent adapter units. Each unit
specifies protocol/schema/transport coverage, child/service ownership, lifecycle,
original result capture and error/cancellation/unknown-effect behavior. Research
is not qualification of a client. Keep unsupported schema features explicit;
current scalar tool schemas do not become arbitrary nested MCP forms by relabeling.

Direct acceptance: paged catalog with stale last-good data; changing authorization
or schema cannot retarget a form; prompt resolve doesn't run a turn; resource read
and attach differ; saved API recipe doesn't execute; intentional rerun creates a
new attempt and retains unknown prior effects; inventory cannot govern an external
service. Model/Lua access gets the same qualified operations and bounded discovery.

## Unit 6: metrics consumers and evidence instruments

Adapt existing trace/variable/correlation and separate usage-reader projections
first. Add shared timeline/plot/range/comparison parts from real prepared series.
Deliver the [distinct metrics views](TUI_METRICS_VIEWS.md) incrementally:
operations, execution, usage/cost, context, interaction and variables/comparisons.
The prospective evaluation view can display explicit retained results when they
exist; selecting/running evaluators remains deferred to operator discussion.

Needed transforms, scans and caches run in a tertiary consumer. Keep raw capture
and recording lossless in their defined fault domain; charts cannot sample emitted
records or introduce producer-side duration/aggregation work. Add missing original
capture only in its appropriate owner unit, not while writing a plotting callback.

Direct acceptance: known subtotal/unknown/zero differ; stopped consumer leaves
usable stale view; pinned prefix never borrows later outcomes; clock domains and
gaps stay explicit; repeated response captures aren't double-counted; charge and
versioned estimate remain separate; selecting a chart point recovers actual source.
Range scrubbing remains available while a bounded query runs.

## Later dependencies: complete teams, mods and expressive devices

Connect the native workflow host through a real adapter before showing it live
in the center. Task-local model/tool-using peers, durable continuation/recovery,
explicit responsibility claims and independent remote rooms follow their existing
workflow/peer owners. A roster implementation does not complete F5/F11.

General mod contribution slots, resources, preview and retirement should grow from
the Lua instrument and binding owners. They need documented lifecycle and stale
callback checks. A display preview excludes effects through an actual API boundary.
A new native plugin ABI or fault-isolation process is a separate deliberate choice.

MIDI/other device adapters follow the common action/binding contract, with source
clock/device incarnation, absolute/relative/held semantics, disconnect cleanup and
feedback. Implement the binding inspector on terminal controls first; add chosen
devices after actual hardware/protocol decisions. No synthetic-keystroke-only
architecture. Haptic research does not establish any chosen force-feedback device.

## Verification and publication

Each implementation slice names concrete defects/contracts, its direct oracle and
resource cost before work. Preserve existing affected behavior and use the required
C++/Lua checks for source commits. Test authority/lifetime boundaries independently
of widget implementations; visual snapshots alone cannot qualify target dispatch,
nonblocking control, recovery or external effects.

Apply the existing two-layer bound: fixes, then one recheck/fixes. No review of
reviews, perpetual certification, invented wall-clock cutoff or redundant full
suite for documentation. Research/design delivery uses source, manifest, link and
diff checks plus one integrated conceptual challenge. Checkpoint/push candidates
under existing authorization; main integration and running-process replacement
retain their separate scope.

## Tracked delivery

- `arconaut-rhi.3` — Implement native component host and Lua composition through conversation and command center.
- `arconaut-rhi.4` — Apply components to current settings, Context workbench and initial team surface.
- `arconaut-rhi.5` — Own start-turn bindings and action-fabric configuration with live boundary activation.
- `arconaut-rhi.6` — Deliver qualified capabilities, broad skills, reusable API recipes and external client views.
- `arconaut-rhi.7` — Deliver tertiary metrics consumer views with source and coverage inspection.

The design unit is `arconaut-rhi.2`; these implementation issues remain open.
Known ESC recovery stays `arconaut-600`. Existing workflow/peer delivery issues
retain their owning contracts rather than being duplicated as a UI scheduler.
