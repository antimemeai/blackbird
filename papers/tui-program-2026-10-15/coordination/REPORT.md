# Coordination and extension views: grounded design research

2026-10-15. Contribution to the requested enduring TUI doctrine, reusable owned
C++/Lua components, and designs for present and future views. This lane owns
command center, team, and mods views. The concrete interaction proposal is in
[VIEW_DESIGNS.md](VIEW_DESIGNS.md). It proposes view behavior and identifies
runtime dependencies; it does not implement missing owners or adopt dependencies.

The [interaction theory](../../../docs/INTERACTION_THEORY.md) governs the
reasoning. [Current stock](../../../docs/CURRENT_STATE.md), the implemented
usage guides, and actual source govern availability. The participant/multiplayer
[discussion](../../../docs/PARTICIPANTS_AND_MODES.md) supplies intended breadth,
not evidence that its whole destination is implemented. Read-only baseline:
`5fbb15197776eca4402df79f4201e7fbe6f19a66`; other researchers' concurrent document
edits do not change the product observations here.

## Source and terminology findings

The OpenAI Docs skill was read and current official documentation searched and
opened. OpenAI product guidance here uses the official permitted documentation
domains. Claude sources below are explicitly comparative, not evidence about
Codex. Original downloads, timestamps, HTTP revision hints, byte counts, hashes,
and restoration commands are in [sources.tsv](sources.tsv). Live documentation
does not supply an immutable semantic version. Capturing its exact bytes and
timestamp does not make it an executable qualification.

| Requested idea | What sources actually establish | Blackbird interpretation |
| --- | --- | --- |
| Command center inspired by Codex | Codex documents an agent command center through `codex agents` and `/agents` on a shared local/remote server. | A useful source for overview, triage, selection, details, and switching. Blackbird's multiple existing owners must be joined honestly by projection adapters. |
| Team | Codex documents subagent workflows and agent threads; ChatGPT Teams/Team Tasks describe people organizing shared cloud work. Claude separately documents experimental agent teams. | A team view should reveal actual participants, communication, responsibility and ongoing work without importing any source's fixed hierarchy or claiming current P2P delivery. |
| Mods | Codex documents plugins, skills, MCP servers, optional interfaces and lifecycle hooks. Acquired sources do not establish a Codex visual-mod API equivalent to Claude's in-process Mods. Claude explicitly documents Mods that alter UI and behavior. | A Blackbird mods view concerns programmable interface and behavior extensions; a skill inventory is only one different capability. Existing Lua registries supply part of its foundation. |

### Official Codex command center

[Developer commands](https://learn.chatgpt.com/docs/developer-commands) describes
`codex agents`, its local/remote shared-server requirement, and `/agents` for
chat status inspection and switching. `/subagents` addresses subagent threads,
whereas `/plugins` browses installed/discoverable plugins and `/hooks` manages
lifecycle hooks. The docs distinguish queueing to an existing session from
starting another one, and describe confirmation/lifecycle effects of archive and
delete. These commands are reference behavior, not Blackbird commands.

The useful transfer is the separation of overview from a focused working chat
and of viewing from lifecycle mutation. Blackbird must preserve its own complete
original retention and present switching constraints. Codex's delete or
archive-and-stop behavior supplies no authorization or design reason to copy it.
The full original Markdown was acquired; the HTML page was opened to verify the
actual official surface.

### Official subagent and long-running-work surfaces

[Subagents](https://learn.chatgpt.com/docs/agent-configuration/subagents)
documents parallel delegation, separate threads, custom definitions, and
surface-specific inspection/steering controls. The web sidebar's read-only
Active/Done lists differ from CLI thread switching and available IDE controls.
Do not infer controls from a screenshot of a different surface.

[Long-running work](https://learn.chatgpt.com/docs/long-running-work) describes
goal criteria, follow-up steering, independent per-chat context/results, and
desktop progress controls. Its goal mode is not Blackbird's task list, native
workflow host, or approval to invent a new goal scheduler. The design lesson is
that a persistent account of purpose and progress can support returning to work;
it need not expose every intermediate event or force the operator to monitor it.

### Official plugin and human-team terminology

[Plugin architecture](https://developers.openai.com/plugins/concepts/plugins)
describes packages combining skills, MCP, optional UI and lifecycle hooks, with
surface-specific availability. [Packaging](https://developers.openai.com/plugins/build/plugins)
distinguishes portable and compatibility layouts and limits lifecycle-hook
distribution. Neither page establishes a general native-host rendering extension
contract matching the Claude Mods API. Do not rename Blackbird modules or skills
as fully supported visual mods because plugins exist elsewhere.

[ChatGPT Teams and Team Tasks](https://learn.chatgpt.com/docs/enterprise/teams)
concerns people, service accounts, shared connections, and recurring/event-triggered
cloud tasks. Team membership is distinct from workspace groups. This is useful
terminology disambiguation; it is not a source for Blackbird agent-team topology.

### Claude as an explicit comparator

[Claude's agent-team documentation](https://code.claude.com/docs/en/agent-teams)
describes experimental team orchestration, independent teammate contexts and
direct messaging, with lead, resume, and shutdown limits. Its task and teammate
concepts suggest useful views of responsibility and communication, but its
particular hierarchy, approval procedures and lifecycle constraints do not become
Blackbird requirements.

[Mods overview](https://code.claude.com/docs/en/plugins/mods/overview) describes
in-process functions affecting events, commands and presentation, distinct from
skills and external hooks. [Interface documentation](https://code.claude.com/docs/en/plugins/mods/interface)
describes named render sites, host-routed focus, keyed controls, redraw
coalescing, and several state lifetimes. A pane opened while a user is typing
does not acquire their keys. Some native key behaviors cannot be rebound by a
mod; that is a limitation relative to this operator's more expressive intent.
Render invalidation coalesces drawings, not underlying effect records.

The previously acquired [mods study](../../integrations-2026-10-06/mods/report.md)
and [teams study](../../integrations-2026-10-06/teams/report.md) retain source pins,
read examples, engine-source limits and lifecycle caveats. New current-doc
captures supplement that corpus; no reference code or account was run.

## Pinned Codex source, read rather than executed

The actual official Codex extraction is `quarantine/codex` at
`08e2b58b07b8a423d6577b66fb7756e980b53dbf`, reused from the
[existing acquisition](../../2026-09-30-known-agent-acquisition.md). Its intact
original ZIP is also recorded and hash-checked in this lane's manifest.
`quarantine/open-interpreter/codex-rs` is a separately pinned derivative and
must not be misattributed to official Codex. This study uses official Codex for
the following mechanisms.

| Source inside `quarantine/codex/codex-rs/tui/src/app/` | Mechanism read | Consequence |
| --- | --- | --- |
| `agent_center/mod.rs`, `input.rs` | All/Needs you/Working/Ready/Inactive filters share status grouping; configured shortcuts take precedence; help and metadata editing have explicit input states. | Derive help and filters from actual semantics; local modal/focus state matters as much as top-level shortcuts. |
| `agent_center/navigation.rs`, `rows.rs` | Paging includes group headings; selected row and viewport move coherently; metadata columns disappear at narrow widths; loading, reconnect, fetch failure, empty and no-match states differ. | Make projection, navigation and layout use compatible row identities and geometry. Preserve failure distinctions under responsive compression. |
| `agents_overview_threads.rs` | Listing supplies membership without replaying every transcript; selected detail fetching targets recent/loaded and notification-relevant threads. | Overview need not load every original body. Membership and detail availability are separate facts. |
| `agents_overview_actions.rs` | Server RPC owns lifecycle result. A progress modal keeps drawing/resizing but ignores key, paste and mouse while lifecycle RPC runs. | A moving display does not establish available intervention. Blackbird should define promised escape/control progress explicitly. This source is an instructive tradeoff, not an ideal to copy. |
| `agent_center_tests.rs` and `app/snapshots/*command_center*` | Actual fixtures distinguish input-needed, active, unloaded and error states; test source exercises wide/narrow layout, status filters and navigation. | Useful direct fault cases and presentation alternatives. Tests were read, not executed; snapshots do not establish end-to-end latency. |

These are pinned source mechanisms, not a claim to have reproduced today's
installed app or every current Codex deployment. No new framework was selected.

## Blackbird's existing seams and material gaps

| Area | Present or partial owner | What the proposed view must not claim |
| --- | --- | --- |
| Current conversation and saved sessions | `TerminalUI`/coding session and retained session catalog; named picking, resume, drafts/context/tasks exist. | Viewing another session does not establish that switching a live owner is currently allowed. Existing active/unsettled-effect fences remain. |
| Session task list | Native sequential task owner; model `tasks_read/edit`, Lua wrappers, revision/item guards and explicit runtime bindings. | A free-text owner field does not establish a live participant assignment; command success does not finish the task. One subtask level does not prescribe team topology. |
| Participants | `Participants` in `include/blackbird/participants.hpp`; shared start/read/send/cancel/await/join/archive/configure; bounded process-lifetime context-only workers. | No peer room, tool-using colleague loop, durable inbox replay, worker resurrection, cross-session custody or remote-stop guarantee. |
| Commands | Existing command-job owner with stable IDs, bounded output and explicit input/signal/stop/resize operations. | Command lifecycle is distinct from participant/request/task lifecycle. A PTY attachment is not a full terminal emulator. |
| Independent workflows | Native session host, retained tasks/results/joins, attachable local clients and explicit cancellation; guide in `NATIVE_WORKFLOW_HOST.md`. | This has not replaced the conversational launcher. Task-local model loops, durable continuations and remote/team frontends remain owning units. |
| Definitions | `tool_define/tool_registry`, `program_config`, named modules and `workflow_registry/workflow_invoke`; effective/pending source and existing activation timing. | Source edits alone do not activate a retained definition. Atomic interrupt-and-apply is intended but not fully delivered. |
| Packages | Trusted opt-in G8 Lua package seam; metadata, receive, display, explicit context inclusion and prepared station event. | No plugin SDK, native custom pane, comprehensive installed-package registry, arbitrary event middleware, or service governor. Workflow-local package state is not durable global extension state. |
| Multiplayer/station | Local station admission exists; attachment and deeper peer work have explicit design/delivery owners. | UI connection is not peer membership. Independent P2P/E2EE conversation and shared artifacts require their actual transport/identity contracts. |

Implementation and promised capabilities must stay separately visible. The
proposed view owns navigation and projections; existing runtime owners retain
execution and settlement. Missing operations are named dependencies, not inert
buttons presented as working capability.

## Reusable component demands from this lane

The component researcher owns the detailed C++/Lua interfaces. These view needs
give concrete inputs to that work:

- A paged keyed collection with stable selection, independently scrollable
  detail, grouping/filtering and honest source completeness.
- A typed work reference that distinguishes owner/session, object kind, identity,
  incarnation where relevant, and a revision only when the operation needs it.
- An operation descriptor shared across presentation adapters: arguments,
  admissible targets, actual owner, admission/result states and error detail.
  It must not become an alternative effect executor inside a widget.
- A focus/return stack and per-route drafts preserving the active text field
  during background completion, list changes, resize and failed navigation.
- A named view-slot and input-action extension seam with generation/lifetime
  behavior, clean fallback and inspectable active bindings. No extension may
  gain direct terminal-write custody merely by rendering a panel.
- Subscription projections that stop unused work and can resynchronize after a
  missed update. View coalescing preserves exact originals and raw capture;
  metric transformations remain in their existing separate analysis process.

These are owned component requirements rather than proposals to adopt Ratatui,
React, a JavaScript runtime, a middleware library or an existing mod engine.

## Direct questions for the candidate design

The useful comparison is whether an operator can return to ongoing work,
identify a consequential uncertainty, inspect its actual source, direct the
correct owner, and resume the previous activity with less reconstruction. A
beautiful overview that loses the selected target under sorting fails that
purpose. So does a team graph that invents shared context or an extension panel
whose counters imply work happened when only a prompt was prepared.

The designs name actual focus and state transitions, provide narrow forms, and
map effects to current or missing owners. Their hypothetical names and layouts
can be revised without changing the underlying theory or the runtime's current
contracts. New numerical frame, latency or event-volume promises require actual
operating-envelope decisions; these sources do not establish those budgets.

## Acquisition limits

Nine current documentation originals and one reused official Codex source ZIP
are recorded. Documentation images/videos and the full website asset graphs were
not mirrored; no argument depends on an unseen video or figure. Source files
and literal snapshot fixtures supplied the command-center layout study.

No inaccessible mandatory source requires operator acquisition. Exact current
desktop app implementation, account-dependent feature availability, and future
Blackbird peer/native-mod runtime semantics remain unknown or undelivered rather
than guessed. No app install, authentication, account mutation, reference
execution, product change, dependency adoption, or performance test occurred.
