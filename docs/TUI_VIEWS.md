# Blackbird TUI working surfaces

2026-10-15. Design program under the [TUI doctrine](TUI_DOCTRINE.md), grounded
by the [interaction theory](INTERACTION_THEORY.md) and
[harness/component research](../papers/tui-program-2026-10-15/README.md).
These are proposed compositions and domain contracts, including future capability.
Current implemented behavior is recorded in [project stock](CURRENT_STATE.md).

## The design stack

| Artifact | Responsibility | Changes when |
| --- | --- | --- |
| Interaction theory | Explanations, mechanisms, values and counterexamples | Evidence or intended use changes an explanation |
| TUI doctrine | Enduring interaction commitments for future work | We explicitly change how this instrument should behave |
| Component contracts | Reusable owned C++/Lua mechanisms and ownership | A concrete reusable behavior needs a better mechanism |
| Domain/view designs | Work episodes, representations, actions and missing owners | Particular capabilities or working methods evolve |
| Implementation plan | Coherent slices, dependencies and direct acceptance | Delivery evidence changes the next useful slice |

This distinction prevents a cosmetic upgrade from standing in for doctrine, and
prevents a general component library from pretending to supply domain authority.
The [component design](TUI_COMPONENTS.md) gives ordinary future views native focus,
text editing, identity-aware collection navigation, async resources, error recovery,
layout, styling and action help through composition. This is the foundation for
broad, easy application of the doctrine; shared behavior does more work than a
catalog of matching borders.

## Map of views and work

| Family | Work and principal representations | Design |
| --- | --- | --- |
| Conversation and execution | Conversation document, draft, palette, tool/result inspector, command job input/output | [Existing surfaces](TUI_EXISTING_VIEWS.md) |
| Tasks, continuity and context | Task tree/editor, sessions/drafts/restart, context/source/request workbench | [Existing surfaces](TUI_EXISTING_VIEWS.md) |
| Settings and callable machinery | Model/account/program inspector, tools/workflows/decision calls, effective/pending changes | [Existing surfaces](TUI_EXISTING_VIEWS.md) |
| Command center | Selected-session/connected-owner overview, triage, actual work/result navigation | [Coordination](TUI_COORDINATION_VIEWS.md) |
| Team | Roster, responsibilities, directions, contexts, answers and linked work | [Coordination](TUI_COORDINATION_VIEWS.md) |
| Mods and instruments | Contribution browser, source/diff/preview, generation/lifetime and bindings | [Coordination](TUI_COORDINATION_VIEWS.md) |
| Capabilities and packages | Qualified cross-kind discovery, contribution/availability/source inspection | [Fabric](TUI_FABRIC_VIEWS.md) |
| MCP, integrations and infrastructure | Endpoint catalogs, account associations and consumer inventory | [Fabric](TUI_FABRIC_VIEWS.md) |
| Execution and reuse | Typed workbench, API/call recipes, prompt/procedure/skill library | [Fabric](TUI_FABRIC_VIEWS.md) |
| Hooks and action fabric | Event/binding authoring, source selection, configuration activation and dispatch history | [Fabric](TUI_FABRIC_VIEWS.md) |
| Metrics and evidence | Operations, execution, usage/cost, context, interaction, variables/comparisons, future evaluations | [Metrics](TUI_METRICS_VIEWS.md) |
| Controls and arrangements | Action discovery, effective binding inspector, controller mapping and saved local view arrangements | This document and shared components |

The Mods view is a task-oriented route into instruments and their contribution
relationships. Packages/capabilities is the cross-kind inventory. They reference
the same contribution/configuration owners; there are not two extension managers.
Likewise, command-center work links to domain execution views, while metrics
inspect evidence about it. A row's presence in multiple views does not duplicate
its actual owner, effect or durable state.

## The working shell

Use a small native shell with route history, current session/environment,
view-local focus and a persistent conversation draft. It can compose split views,
collection+inspector, full-width editor or focused plot. Do not impose a permanent
sidebar, immutable conversation-majority layout or a dashboard grid on every task.
The current startup/status avatar is compatible with the shared visual grammar.

A compact route selector or command palette reaches all installed view
contributions and supported actions. Pinning a view is local arrangement, not
subscription to every backend. Closing a route retires its presentation requests;
already admitted work continues under its actual owner. Returning restores query,
selection, anchor and unsaved input. Local navigation does not launch a model turn.

Composition and preview have an enforced effect-free bridge phase; action handlers
carry identified intentional invocations. Redraw/reload cannot create new effects
or retry an ambiguously acknowledged one. Acquired coordinate events retain their
geometry and target through later scene changes, with explicit refusal where stable
association cannot be established.

A view declares principal actions, focus regions, data dependencies, supported
width/height priorities, and whether it can show retained data when disconnected.
At narrow width, collection→detail becomes a route relation with preserved return.
Status is concise and actionable; provenance, uncertainty and detailed source are
available without forcing a dense operational header into every conversation.

The shell distinguishes local draft from domain form input and process stdin.
Only explicit focus/mode change transfers printable input. Popups, live roster
updates and background faults do not steal focus. Context help always describes
the effective bindings rather than hardcoded legends.

## Controls, controller mappings and local arrangements

The binding view starts with searchable named actions and current scope stack.
Select an action to inspect argument schema, target policy, effective gestures,
source and pending edits. Select an observed input to inspect decoded event,
terminal/device capability, resolution path and winning/conflicting bindings.
The observation mode records selected event facts for diagnosis; it never turns
all typed secrets into ordinary public history.

```text
 CONTROLS   project / conversation                effective mapping m7
 Find: [ scrub execution                         ]
 Action                 Binding              Target policy
 > timeline.scrub       MIDI dial 2 relative  focused execution view
   work.cancel          pedal press          pinned selected work
 ----------------------------------------------------------------
 timeline.scrub   local range action · no external effect
 Device instance: controller C / present
 Decode: relative signed delta · acceleration: explicit profile
 Scope: focused timeline → execution view → application
 [Edit mapping] [Observe input] [Explain resolution] [Source]
```

These mappings are examples, not selected hardware, default gestures or delivered
MIDI integration. The editor offers sequences/chords/repeat/held/continuous options
only where the input adapter supports them. It exposes ambiguity timeout,
unmatched-prefix disposition and conflict precedence. Simulated/test resolution
is effect-free; deliberately exercising a real action is a separate operation.

A controller disconnect clears held state and feedback for that device incarnation.
Reconnection does not resurrect a held pedal or retarget a running operation.
Relative motion accumulates according to its explicit policy; absolute preview
can replace obsolete preview values; discrete effectful triggers remain distinct.
Continuous chart controls update local state and bounded async queries, not one
remote effect per sample. Feedback maps received/accepted/observed outcomes
explicitly, with visual/text alternatives. No haptic device or library is selected.

Saved local arrangements retain route/layout preferences, selected reference and
safe recoverable drafts where supported. They do not serialize Lua closures,
borrowed pointers, provider credentials, active device holds or fake live owners.
Restored unavailable sources display retained references and real reconnection
choices. Arrangement restore cannot restart a service or repeat an old call.

## Shared semantics, different activation boundaries

| Change | Governing owner and intended activation |
| --- | --- |
| Focus, scroll, form draft, chart cursor, theme preference | Native local presentation state; coherent local update without waiting for an unrelated turn |
| View composition definition | View-generation owner; publish a validated coherent scene at a safe composition boundary, retaining compatible native state and actually quiescing old callbacks |
| Governing program/tool/workflow/hook definition | Existing or proposed runtime definition owner; affected current work retains its generation, default activation after conclusion/successful eligible boundary |
| Ordinary context data edit | Current Context owner and next-request semantics; preserve actual revision/request audit |
| Connection, account, service call or recipe invocation | Its domain owner; explicit admission and effect/result semantics |
| Raw metrics and derived query | Original capture/recorder independent of view; transformed snapshot owned by tertiary consumer |

A presentation-only edit must not silently change running workflow meaning; a
harmless local layout adjustment need not wait for a provider request to finish.
The combined interrupt-and-apply action is intended runtime work, not a UI callback
sequence that pretends to be atomic. Failed publication preserves effective state.

## Human and model operation surfaces

Every domain family needs bounded discovery, selected inspection, typed mutation/
invocation, outcome observation and exact-reference recovery. Adapters share
existing owners and expose equivalent operation semantics to human, model and Lua.
They do not all have to use one universal function or eagerly describe every
capability in each model request. Discovery metadata is economical; full schemas,
skill bodies and originals load when needed. Model observations state their scope,
revision/coverage and next cursor. Pagination limits examined work as well as
returned matches.

Human routes can be richly spatial. Models can compose operations, workflows and
queries directly without manipulating the human's current selection, requiring a
screen, or displacing their draft. A model editing a hook can leave a source/base
reference for the human editor; a human invoking a recipe produces the same actual
attempt references the model can inspect.

## What the harness studies establish

Current official Codex documentation describes an agent command center,
subagent navigation and plugin/hook/MCP management. The pinned source contributes
specific grouping, responsive-row and configured-help mechanisms. Its lifecycle
modal keeps painting while rejecting input: that is a useful counterexample to
calling animation responsive control. See the
[coordination report](../papers/tui-program-2026-10-15/coordination/REPORT.md), with
[official commands](https://learn.chatgpt.com/docs/developer-commands) and
[CLI reference](https://learn.chatgpt.com/docs/cli/reference).

Claude Code's actual mods/events and reload documentation, MCP's versioned
protocol, and Pi's pinned extensions/skills/templates supply distinct mechanisms.
Their reports distinguish documentation from inspected source. Codex plugin
packages are not claimed to be Claude visual mods, and organizational Teams are
not relabeled as tool-using agent peers. The
[fabric report](../papers/tui-program-2026-10-15/fabric/REPORT.md) records current
primary captures and proposed transfer. No harness, framework, library or hardware
is adopted by studying those sources.

Implementation dependencies and direct acceptance episodes are in the
[delivery plan](TUI_IMPLEMENTATION_PLAN.md). Designs can grow as new owners arrive;
unsupported categories remain explicitly unavailable, rather than presenting
fictional toggles, charts or successful actions.
