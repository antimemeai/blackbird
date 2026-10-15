# Capability and action fabric: one working instrument, explicit kinds

2026-10-15. Research and surface-design contribution for the enduring TUI
doctrine. No product implementation, dependency, installation or account action.
The concrete designs are in [VIEW_DESIGNS.md](VIEW_DESIGNS.md); retained primary
sources and restoration commands are in [MANIFEST.tsv](MANIFEST.tsv).

## Purpose and theoretical derivation

An expert should be able to find a capability, understand its current meaning,
configure it, exercise it, turn a useful invocation into a reusable instrument,
bind it to an event, and inspect its later effects. A model should be able to do
the same through structured queries and programs. Configuration is part of the
working environment, not a detour into an unrelated administration product.

[INTERACTION_THEORY](../../../docs/INTERACTION_THEORY.md) explains why this
requires compatible references, legible feedback and continuity while instruments
change. The design therefore makes the active definition, its source and scope,
the proposed change, and an invocation's actual outcome accessible from the same
object. It keeps inspection, context inclusion and execution distinct. It permits
ordinary autonomous execution without a new per-call permission ritual.

The central episode is: the operator asks a model to add a start-turn hook that
loads project guidance, the model writes its source/configuration, and the hook
becomes effective for a later turn without restarting Blackbird. The same pattern
must support an MCP prompt, a parameterized API call, a reusable skill, a controller
binding, or an optional mod while preserving each one's actual execution semantics.

## What exists and what does not

Read the current [stock](../../../docs/CURRENT_STATE.md),
[core design](../../../docs/CORE_DESIGN.md),
[Lua tools](../../../docs/LUA_TOOLS.md),
[callable workflows](../../../docs/CALLABLE_WORKFLOWS.md),
[program usage](../../../docs/USING_BLACKBIRD.md) and
[instrumentation boundary](../../../docs/INSTRUMENTATION.md). The earlier
[integration synthesis](../../integrations-2026-10-06/SYNTHESIS.md) supplies research
intent, not completed machinery.

| Existing backend or source | Present semantics | What a new surface may legitimately expose |
| --- | --- | --- |
| `tool_define`, `tool_registry`, `blackbird.define_tool` | Retained Lua tool schema/body; staged and effective revisions; publication after successful affected workflow. Scalar-object schema subset. | Tool collection, source/schema editor, candidate/effective comparison, invocation and retained errors. Do not imply full MCP JSON Schema support. |
| `program_config`, `module_source`, `blackbird.module` | Complete versioned proposals, retained named modules, resolved request defaults, successful-boundary publication, per-workflow imports. | Program inventory, provenance and pending-change inspector; copy current complete configuration before changing one field. A file edit alone does not publish a retained module. |
| `workflow_registry`, `workflow_invoke` | Same-turn callable Lua registry, aliases/bare names/POWERWORDS; source retained per invocation; sequential continuations with explicit limits. | Reusable callable collection, arguments, triggers, source and execution history. Do not label it a durable parallel scheduler. |
| `programs/packages.lua` | Inert import; workflow-local trusted adapters with install/enable/configure/receive/display/include/event/status. | Existing adapter status and feed semantics through an explicit workflow owner. Its `install` is in-memory registration, not a persistent package manager. |
| Provider account operations and `/auth` | Existing provider selections/status, private credential handling, observed versus requested models. | Existing provider account status/actions with actual origin; this does not supply every integration's account lifecycle. |
| Original audit, command/participant operations, trajectory queries | Identified attempts, original references, observable outcomes, finite inspection. | Reusable invocation/result inspector and links to actual effects; query only the history requested. |
| General MCP client/catalog, persistent plugin manager, generic event-hook registry, personal/project configuration resolver | Not established as delivered by these sources/current stock. | Designed surfaces with named missing backend owners. No fake Connected/Active button or command that silently falls back to shell execution. |

The TUI work should reuse existing owners where their contracts fit. A rendered
control must not manufacture an unsupported operation. Missing backend work is
identified by the exact state/query/action a view requires, rather than a general
platform rewrite.

## Primary harness and protocol findings

**MCP tools**, [revision 2026-07-28](https://modelcontextprotocol.io/specification/2026-07-28/server/tools):
tool names are server-scoped; a server's display/name metadata is not a globally
unique disambiguator. Lists are paginated and can vary with request authorization.
`tools/call` has its own execution result and error distinctions; an additional-
input result can continue an operation through another round trip. Discovery
and invocation require separate displayed states. The tools page recommends human
confirmation, but does not mandate a particular UI; that recommendation is not
adopted as a Blackbird approval workflow. The operator's existing autonomy remains.

**MCP prompts**, [same revision](https://modelcontextprotocol.io/specification/2026-07-28/server/prompts):
`prompts/list` discovers a template and `prompts/get` resolves its arguments to
messages. Getting a prompt is not invoking a tool or necessarily starting a model
turn. Server-controlled contents retain their source even when selected by the
operator. Blackbird can expose preview, insertion into a draft, inclusion in
context and explicit execution as different operations.

**MCP resources**, [same revision](https://modelcontextprotocol.io/specification/2026-07-28/server/resources):
URI-addressed resources have listing, reading and templates; catalog-change and
resource-update support are separately advertised. Listings and reads support
caching. A tool-returned resource need not be discoverable through the catalog.
The resource inspector therefore needs direct links, source/URI, actual capture
and cache/freshness state, rather than treating catalog absence as nonexistence.

**MCP transport/versioning**, [versioning](https://modelcontextprotocol.io/specification/2026-07-28/basic/versioning)
and [HTTP](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http):
July 2026 uses per-request version/identity/capability metadata, not the older
initialize handshake. HTTP no longer has a protocol session or independent GET
stream; long-lived changes use `subscriptions/listen`. Transport cancellation does
not establish rollback of already performed work. A server inspector must report
the actual supported/used protocol profile and observed availability rather than
recycling a legacy session model. Backward compatibility remains an explicit
adapter responsibility. This research does not implement either profile.

**Claude Code settings**, [official live documentation](https://code.claude.com/docs/en/settings):
most settings, including hooks, reload on file changes without restart, while
some keys need explicit commands or a later session. `/status` reports loaded
sources but not the source of each effective key. Useful consequence: Blackbird's
inspector should answer both "which source loaded?" and "why this value?"; saving
bytes and adopting their meaning remain distinct observations.

**Claude Code classic hooks**, [official reference](https://code.claude.com/docs/en/hooks):
event cadence and matcher semantics matter; matching handlers run in parallel,
and some hook families can transform/block whereas others are observational.
Session-start context can race skill discovery, hence an explicit `reloadSkills`
result. These are reasons to expose event boundary, ordering and affected state.
Blackbird should not copy its policy gates, broad parallelism or particular event
names without an owned contract. "Hook" alone does not tell an operator when code
runs or what it can change.

**Claude Code plugin versus mod**, [manifest](https://code.claude.com/docs/en/plugins-reference),
[mod events](https://code.claude.com/docs/en/plugins/mods/events),
[development reload](https://code.claude.com/docs/en/plugins/mods/create) and
[API](https://code.claude.com/docs/en/plugins/mods/api):
a package can contribute several components; a mod adds in-process function
handlers and views. Middleware order affects who sees and changes an event.
Failure after downstream completion does not automatically repeat downstream
work. Watched development files saved during a model turn reload at turn end.
These patterns support separate package inventory, runtime instances and bindings.
They do not establish Blackbird's implementation or native hot reload. Documentation
claims for a closed runtime remain documentation, not source qualification.

**Pi**, [pinned extensions documentation](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/docs/extensions.md),
with its pinned `extensions/runner.ts` and `types.ts`: the runner snapshots handler
arrays for dispatch and rejects stale contexts after replacement/reload. Docs
separate registration from starting long-lived resources, call for idempotent
shutdown, and distinguish actionable pre-settlement from notification-only
settlement. This supports inspectable dispatch snapshots and stale-handle errors.
The source was read, not executed. Its TypeScript/runtime/dependencies are not
adopted.

**Pi skills, templates and settings**, [skills](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/docs/skills.md),
[templates](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/docs/prompt-templates.md),
[settings](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/docs/settings.md)
and [packages](https://github.com/earendil-works/pi/blob/5ab7fe74c400097b386662205cd93900695b8a71/packages/coding-agent/docs/packages.md):
metadata discovery is lighter than loading full skill instructions; templates
reuse argument-bearing text; packages group multiple resource kinds; settings
have kind-specific combination rules. Skill collisions warn and keep first
discovery. Blackbird should show conflicts and resolve identities explicitly.
The operator's word **skill** is deliberately broader here: a reusable prompt or
instruction procedure counts without requiring `SKILL.md`, a particular directory,
or a vendor's frontmatter convention. Import adapters can preserve those conventions.

Current sources were captured on 2026-10-15. Pi is pinned to
`5ab7fe74c400097b386662205cd93900695b8a71`; the public repository moved from the
older badlogic identity to `earendil-works/pi`. The manifest records exact originals,
including HTML, source files and the commit API response. MCP 2025 examples were
inspected during discovery but current conclusions use the retained July revision.

## Proposed shared semantics

The catalog groups related objects without pretending they are interchangeable.
A capability has a qualified identity, kind, source revision, scope, available
verbs and backend owner. A package has installed source; a loaded mod has a
particular runtime generation and resources. An integration associates capabilities
with an external service/account. Infrastructure inventory describes what this
Blackbird consumes; it does not authorize governing a shared database or kernel.
A binding connects an event/input to a named operation and an explicit target or
target-resolution rule. An invocation records a particular attempted use.

Each kind defines its own scope/override rules. Do not apply silent recursive
object merging to everything. A single-valued setting can have a winning source;
an event can have an ordered list of handlers; a tool name collision can be an
error. The inspector shows contributors, resolution and shadowed alternatives.
Personal, project and session scopes are design goals; current session-owned
registries should be labeled honestly until additional scope owners exist.

Save, activation and execution are separate transitions. "Save and activate" is
one ordinary editing action whose result states when it becomes effective. While
affected work runs, its definitions remain pinned and the candidate is pending.
At the normal successful boundary it becomes available to future work; failed
publication leaves an explicit error and the prior effective meaning. An idle
edit can become effective for the next admission through its local owner. Nothing
here requires a process restart. The existing combined interrupt/apply machinery
is incomplete: do not draw a working button until settlement and generation
publication really support it. Deactivation stops future admission, not already
performed effects; cleanup and running work need their own visible disposition.

Hooks need narrower contracts than arbitrary callbacks: event boundary, pure
selection rule where supported, ordered action/transform semantics, failure
policy, continuation behavior and owner lifetime. Start-turn hooks run once for
the admitted turn's pinned snapshot, before the designated context/request
assembly boundary; newly edited hooks do not insert themselves retrospectively
into that turn. Context-producing hooks identify their input, contribution and
resulting request/context revision. Intentional instruction injection is ordinary
authored behavior with a source, not an unlabelled replacement for operator text.

A trigger chain carries originating event, binding revision, parent invocation,
actual attempt and environment. Duplicate source-event delivery must not silently
repeat an admitted effect. Repeated intentional input events remain distinct.
Static obvious cycles should be diagnosed; dynamic reentrancy/continuations need
an explicit supported policy and an observable refusal at its configured bound.
No finite bound promises exactly-once remote execution. Unknown effects stay
unknown, with an explicit new attempt required when a participant elects to retry.

Event/action fabric is application execution. Raw metric capture remains on its
existing minimal lossless path; it does not call Lua hooks, format dashboards or
run transforms. Views query retained/derived observations and analysis results
outside that emission path. Timers, subscriptions and service clients are ordinary
identified resources with actual ownership, not renderer side effects.

## Required surface contracts

| Claim | Direct behavior that the design must support |
| --- | --- |
| Discovery is inspection | Opening/filtering a cached collection does not execute a capability, initialize an SDK or read every skill body. An explicit remote refresh is its own operation with age/error state. |
| Editing does not erase active meaning | A rejected/stale candidate keeps its draft and diagnostics; effective definitions remain usable. Source saved, candidate prepared, pending and active have different labels. |
| One invocation, compatible access | Human form, model call and Lua composition resolve the same qualified target/revision/environment and produce linked attempt/result references. They need not use identical syntax. |
| Work remains legible after change | Running work and results retain old source/generation references. Stale callbacks cannot dispatch as the new generation. Failed cleanup reports resources still live. |
| Prompt reuse is controllable | Local render/remote resolve, draft insertion, context inclusion and starting work are separate verbs. Fetching a prompt does not silently send it to a model. |
| Errors describe the actual boundary | Invalid input, unavailable adapter, rejected admission, failed dispatch, tool error, cancellation request, settled cancellation and unknown external effect remain distinguishable. |
| Reuse is deliberate | Saving a recipe does not invoke it; running it creates a new attempt. Reopen, refresh and catalog-change notifications do not replay previous effects. |
| Skill and binding conflicts are visible | Qualified identity and contributing scopes explain the result; ambiguous bare aliases do not silently choose another capability. |
| Hot authoring is useful | Adding a start-turn hook during an ordinary turn persists its source, shows pending/effective generations and applies to the next eligible turn without restart. Failure retains an intelligible draft and prior effective configuration. |

These contracts are design acceptance statements, not a launched test campaign.
They identify what future owner-level checks must observe. Concrete collection,
form, inspector, workbench and binding-editor behavior follows in the companion
design rather than selecting a new general backend framework.
