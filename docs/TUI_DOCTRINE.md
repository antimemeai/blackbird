# Blackbird TUI doctrine

2026-10-15. Enduring interaction rules for new and revised terminal surfaces.
This is a normative design doctrine. The accompanying component and view designs
are proposed work, not claims about the delivered interface.

[BLACKBIRD](../BLACKBIRD.md) governs engineering and ownership. The
[interaction theory](INTERACTION_THEORY.md) explains the proposed mechanisms and
their limits. This doctrine chooses how Blackbird's terminal instruments should
behave. [Components](TUI_COMPONENTS.md) make those choices reusable;
[view designs](TUI_VIEWS.md) apply them to particular work; the
[delivery plan](TUI_IMPLEMENTATION_PLAN.md) sequences implementation. Neither an
old screenshot nor a component's convenience overrides this doctrine.

## Purpose

Build a beautiful programmable instrument for joint work. An expert should be
able to understand, act, arrange, delegate, compose and alter the instrument
without fighting it. A model should be able to use the same working capabilities
through explicit structured operations without interpreting terminal coordinates.

A fast repaint is insufficient. Useful control, coherent meaning, evidence and
presentation must survive concurrent work, incomplete results and instrument
changes. Enable unfamiliar intentions as well as practiced operations. Retain
room for authorship: reusable parts should make excellent composition easy while
allowing a view to represent its particular work well.

## 1. Design around work and its references

A view exists to support named intentions and judgments. State what a person or
model can accomplish, what information relates those judgments, and what action
changes the work. Start with those relationships before choosing rows or panels.
A conversation is one working representation; collections, editors, timelines,
matrices and direct controls are equally legitimate when they fit the task.

Use stable domain identities for work, attempts, participants, definitions,
versions and results. A row number, visual location, display name or current
selection is never sufficient authority for a delayed action. Preserve the
relationship from summary to underlying work and original evidence.

## 2. Keep useful controls available under demand

Typing, draft editing, navigation, local inspection, cancellation and terminal
recovery must remain independently serviceable during streaming, slow storage,
full stdout, collection scans and extension failures. Show which domain action
is waiting without disabling unrelated controls. A moving spinner does not prove
control remains available.

No provider call, source scan, arbitrary Lua function or external action executes
inside input handling, measurement, layout or painting. The terminal owner
accepts bounded local work and asynchronous owner results. Expensive views use
paged projections and visible-range work. Under overload retain truthful work
state and controls; coalesce obsolete presentation requests before sacrificing
useful input. Preserve exact originals and published raw metric events.

Choose latency objectives from actual interactions and measured costs. Verify
under competing demand, including tail delays and recovery. An arbitrary frame
rate, timeout or memory ceiling is not an enduring doctrine rule.

## 3. Distinguish intention, receipt, dispatch and effect

Give immediate local acknowledgment when an action is received. Separately show
whether it is queued, admitted, dispatched, observed complete, failed, cancelled
or unresolved, using the states actually known by its owner. Do not invent
intermediate states to fill every visual step. Cancellation requested is different
from cancellation observed; submitting configuration differs from activating it.

A local acknowledgment must never look like external success. Repaint, a successful
request to an owner, a saved file and a remote effect are different observations.
Show the relevant distinction where the next decision depends on it. Use plain
language and compact state marks rather than a wall of lifecycle jargon.

Admission to the audit does not prove that a command launched. A known refusal
names its resource and says that execution did not start in both human activity
and model tool results. Preserve unknown outcome for an actually uncertain effect;
do not replace an observed refusal with a generic interruption story.

Keep the question with its answer: a filtered query that finds no eligible event
does not prove the underlying work or owner is absent. Native error codes and
empty result pages need their selector, coverage and scope before becoming a
human or model status.

An error after an observed file replacement must still identify that replacement
as applied. Result allocation, presentation and retention are separate outcomes.
If that observation cannot be preserved, further effects remain blocked across
turn boundaries. Formatting failure grants no authority to retry the operation.

## 4. Make effective scope and activation legible

Configuration work exposes its scope, inherited source, local override, saved
revision, effective revision and any pending activation. Show only distinctions
that exist for that capability. A running attempt retains its pinned environment;
a newer definition does not silently change its meaning.

Default governing changes activate after the current turn or affected workflows
conclude. Design an explicit interrupt-and-apply-now action with the actual affected
owners and unresolved work visible; do not present it as delivered until supported.
An idle eligible configuration change should activate without a process restart.
Ordinary edits to context data retain their own next-request semantics.

Editing, saving, enabling, connecting and executing are distinct operations. Their
composition may be convenient and immediate when requested; no routine permission
pageant is added. Shared infrastructure is observed and consumed by Blackbird.
Its governance is not smuggled into an inventory or a convenient stop button.

## 5. Preserve orientation through change

Refresh must preserve focused object, selection, reading anchor, expansion and
unfinished input while those objects survive. Sorting, filtering, streaming and
resize do not silently transfer an action to another object. If the focused object
vanishes, show that fact and choose a deterministic neighboring location; require
a fresh deliberate action before acting on a replacement target.

Treat follow-live as an explicit viewport mode. Reading older material suspends
following, with a truthful new-work indicator and a return-to-live action. Modal
layers restore their invoking focus and draft. Returning from an editor, command
foreground view, another session or a failed extension restores a useful instrument.

## 6. Give human and model agency compatible semantics

Domain operations have stable structured identities, typed arguments, bounded
reads, source references and meaningful results. The human UI, model tools and Lua
use the same domain owner. Human selection supplies identities and arguments;
models need no screen scraping, synthetic keystrokes or hidden UI state.

Both can discover available operations, inspect effective configuration, execute,
observe and revise work where authorized. This does not force equal presentation:
humans need spatial and perceptual organization; models need economical explicit
schemas, exact references, pagination and recoverable detail. Don't require a human
to supervise every autonomous transition or a model to reread a whole registry to
operate on one item.

## 7. Treat controls as a programmable language

Separate input decoding, binding resolution, named actions and domain execution.
Support scoped, discoverable bindings and authored combinations: sequences,
chords where the device/protocol provides them, modifiers, text entry, pointers,
continuous controls and discrete controller events. Capability-dependent controls
must declare what the terminal or device can actually report. Legacy byte streams
cannot reveal every physical key distinction.

Resolve conflicts deterministically, expose effective bindings and let operators
inspect why a control resolved as it did. Timeout, ambiguity, repeat, release,
cancellation and unmatched-prefix behavior are explicit. A partial escape sequence
must not consume the next unrelated character indefinitely. Text entry, paste and
IME update a literal draft without being reinterpreted as keybindings or firing
action macros. Explicit Send still applies the effective submitted-command/workflow
grammar, including existing bare invocation and POWERWORDS. A deliberate grammar
redesign would be a separate change, not a side effect of input extraction.

MIDI knobs, pedals, pads, alternate keyboards and possible haptic feedback are
first-class future adapters to named actions. Bind an action to a stable target
or clearly declared live-selection policy. A knob can scrub a local timeline
without issuing a remote action per tick. Discrete effectful operations require
deliberate bindings, not accidental conversion of every motion sample to a call.
Preserve native timestamps/device identity where available; filtering or smoothing
is an explicit adapter policy, not rewritten source input.

## 8. Use density to preserve useful relationships

Optimize useful judgments and actions per occupied space, including the cost of
finding and reorienting. Offer compact overviews, adjacent comparisons and detail
on demand. Put related state and controls together when joint use warrants it.
A collection may need a table; a causal sequence may need a timeline; editing may
need a dedicated workbench. Do not force all work into chat or a permanent grid.

Use responsive layout with explicit drop/reflow priorities. Preserve identity,
state, focus, draft and principal action before secondary metadata. Narrow layouts
can switch between list and inspector with a reliable return path. Never truncate
away a difference necessary to choose between targets. Expose the full value or
an unambiguous short identity on demand.

## 9. Make presentation meticulous and expressive

Beauty is a product aim in its own right. Establish a coherent visual grammar:
typography achievable in terminals, spacing, alignment, contrast, restrained
semantic accents, carefully drawn borders and deliberate code/data presentation.
Give active focus a recognizable form. Maintain a clear visual distinction among
focus, selection, running activity and attention requests.

Color has textual or shape counterparts. Unicode width, combining sequences,
wide characters, fallback glyphs, light/dark backgrounds and terminal capabilities
belong to rendering design. Preserve exact content for copy and inspection while
presenting readable Markdown, code, tables and structured results.

The emitted text must occupy the cells that composition assigns it. Invisible
format characters in ordinary fetched text belong to this contract. Use pinned
Unicode properties for the relevant cluster rules and check literal positions in
an independent terminal parser: a renderer and a test decoder can share the same
width mistake. A clean modeled grid alone does not establish a clean terminal.

Source matching precedes display escaping. Escaping a control must not invent a
word boundary or remove a real one. Wrapping, styled rasterization, fallback,
caret placement and vertical motion share one explicit byte-to-cell contract;
style spans do not split display clusters. A visible replacement owns its actual
footprint throughout that contract. Keep terminal-profile limitations explicit.

Motion expresses real transition or activity and never delays information or
input. Support reduced motion and static equivalents. Do not animate reading
anchors or require continuous repaint when settled. Images, the Blackbird avatar,
controller lighting and haptics are optional output channels; none exclusively
carries necessary state. Device acknowledgment is not evidence of a remote effect.

## 10. Allocate attention instead of demanding constant monitoring

Separate background activity, actionable exceptions, requested decisions and
terminal outcomes. Repeated facts need not repeatedly interrupt. Preserve
acknowledgment and unresolved intentions so an interruption need not be remembered
in working memory. Let the operator choose what to pin, mute, fold or follow.

Bound recent resident observations without turning routine work into a lifetime
quota. Retire safely settled records under pressure while preserving originals
and stable effect identity; live custody, pending capture and accepted controls
remain owned. A control accepted for dispatch reserves its finite delivery capacity
through selection until dispatch or explicit refusal. Transient input pressure
may wait for actual consumption; an exhausted per-job history budget must refuse
immediately and visibly, since waiting alone cannot release it. Expose the distinction between recent control views and retained
history when an old observation leaves the resident view.

A command center provides overview and paths into work. It does not turn the
operator into a mandatory traffic controller. Team views show responsibilities,
actual coverage and communication; activity alone does not establish progress.
Use the SR-71 case for its attention to feedback, control modes and task saturation,
not for a cockpit imitation or a compulsory pilot hierarchy.

## 11. Let uncertainty remain visible and inspectable

Unknown, unavailable, partial, stale and zero are different values. Status marks,
charts and summaries preserve these differences without burying ordinary use in
warnings. A live connection is not proof of complete catalogs, healthy work or
fresh evidence. Show coverage and freshness at the level needed for a decision.

Metrics carry units, denominator/window, source and transform identity, and a path
to originals. Clock alignment and causal linkage are distinct. Derive displays in
consumers, never by adding aggregation or formatting to raw producers. An explicit
filtered chart may hide points visually; original capture remains intact.

## 12. Make new instruments ordinary and recoverable

Composition, new bindings, views, reusable calls, skills and hooks should be normal
work. Enable inspection of their source, effective configuration and faults.
An extension publishes a coherent presentation revision or leaves the last good
one usable. Native local controls and recovery survive failure. Domain effects
never arise from composing, validating, measuring or drawing a component; the
native action bridge enforces that phase boundary. Deliberate invocations retain
their own identities and receipts across presentation reload. Ambiguous delivery
is inspected or remains unresolved, rather than triggering an automatic replay.

Give reusable components strong default behavior and documented escape points.
A new view should obtain focus, paging, async states, readable errors, command
help, responsive layout and lifecycle behavior through composition. It should only
have to implement what is particular about its domain. Avoid a universal component
so configurable that each caller must reconstruct the doctrine from scratch.

## Applying and revising the doctrine

For a view, write its principal work episodes and concrete source/operation
relationships, select components, identify missing domain support and specify
observable failure/recovery behavior. Keep that information with the design and
meaningful direct checks; do not add a ceremonial approval checklist to every edit.

Evaluate alternatives against expressed and newly possible intentions, continuity,
attention demand, evidence quality, presentation and real control under demand.
Familiar-task speed alone cannot settle expressive capability. If predicted gains
do not appear, change the design or narrow the explanation rather than redefine
the user's intention after the fact. Doctrine changes are explicit written changes;
they do not silently rewrite running definitions or the general engineering rules.
