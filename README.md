<p align="center">
  <img src="assets/blackbird-readme.png" alt="Blackbird — pixel-art SR-71 in afterburner" width="560">
</p>

# Blackbird

**A programmable coding agent for an operator who intends to use the whole machine.**

Blackbird begins with a particular kind of person at the keyboard: technically sophisticated, curious, impatient with artificial restrictions, and inclined to invent a workflow rather than choose one from a menu. Someone who might want a model to write C++, interrogate a standing database, enlist a colleague running a different model, hold an argument in a live channel, and redesign the agent's own turn program before lunch.

The machine should make those things possible. The operator should be able to steer the work. The model should have substantial agency over the programs it uses. Routine tools should execute without an approval pageant. And when the work goes wrong, there should be enough retained evidence to understand what happened.

That is the project. It is also the reason Blackbird is being built inside Blackbird. An agent that can change its own harness, rebuild it, resume the same conversation, and continue useful work gives us a direct way to improve the system we inhabit.

The [interaction theory](docs/INTERACTION_THEORY.md) explains Blackbird as a
programmable instrument for joint work: acting, understanding, delegating and
changing the instruments themselves. Its primary-source research includes human
and model ergonomics, expressive controls, and the SR-71's actual controls and
pilot task saturation. It guides subsequent interaction design without claiming
the present interface implements that whole ambition.

The [TUI doctrine](docs/TUI_DOCTRINE.md) states enduring rules for future terminal
work. Its [reusable component design](docs/TUI_COMPONENTS.md),
[working-surface designs](docs/TUI_VIEWS.md) and [delivery plan](docs/TUI_IMPLEMENTATION_PLAN.md)
cover existing views alongside command center/team/mods, capability and action-fabric
management, expressive controls and multiple metrics instruments. These are designs;
the current implementation remains described below.


This repository contains a **C++20 core, Lua 5.4.8 turn programs, a terminal interface, and an operating self-development loop**. It is an early system in active use and reconstruction. Its ambitions exceed its present implementation; the distinctions below are part of the description, rather than small print.

For current accepted capabilities, unfinished work and the development
cycle, see [the project stock](docs/CURRENT_STATE.md).

Master includes running-command background/foreground ownership
and private PTY text input through shared operator/model/Lua controls. See the
[command design](docs/COMMAND_JOBS.md) and [qualification record](papers/2026-10-09-command-jobs/qualification.md).
Integrated on master; running-executable activation remains separate.

Arconaut is now **Blackbird**. Old launch commands and the Lua `arco` API remain compatibility aliases; sessions and retained audit formats are preserved.

Startup carries a solid block BLACKBIRD wordmark and the operator-supplied SR-71 image
in Ghostty/Kitty. First input moves the same image into a square status avatar,
with an indicator: muted idle, cyan active, amber tool work, red failure.
For native CPU/allocation investigation, see [Profiling](docs/PROFILING.md).
Master also includes `/trace`, `/variables`, `/correlate` and
read-only `/git-observe`; see [trajectory usage and limits](docs/TRAJECTORIES.md).
Configurable [long-term trace storage](docs/TRACE_ARCHIVE.md) copies originals
to a local directory, SCP directory or S3 prefix using a separate native worker.
Archival copies do not reset the current semantic session's capacity limit.

## Come aboard

From a configured checkout with the release executable built:

```sh
./scripts/blackbird --session context/my-blackbird
```

In a terminal this opens the conversation view and composer. Piped input and `--once` select the plain interface; `--plain` selects it explicitly.

```sh
./scripts/blackbird --session context/my-blackbird --once 'Inspect this project and explain its build.'
./scripts/blackbird --session context/my-blackbird --model gpt-6.1-sol --effort medium
./scripts/blackbird --list-sessions
./scripts/blackbird --session context/my-blackbird --expire-diagnostics
```

**Supported partner tool: [phux](https://github.com/no-phux/phux)**, by phall,
is the officially supported external multiplexer. It keeps Blackbird's terminal
running across detach/reattach and gives humans and models access to the same panes.
Start a named phux session with:

```sh
./scripts/blackbird-phux arco --session context/my-blackbird
```

Detach with `Ctrl-A`, then `d`; return with `phux attach arco`. See
[phux support](docs/PHUX.md) for installation, multiple Blackbirds, and terminal control.

The working directory is the tool working directory. New default sessions live at `~/.local/state/blackbird/default`; an existing `~/.local/state/arconaut/default` is reused until you select a new default. An explicit directory keeps a project's conversation where you choose. Only one process may hold a session. Model, reasoning effort, workflow path, and conversation identities persist across restart. Explicit launch options override and save the corresponding settings. See [session reopen behavior](docs/SESSION_REOPEN.md) for recovery size limits, uncertain operations and `--rebuild-session`.

Blackbird owns provider account storage, selection and refresh. Explicit OpenAI
sign-in uses browser OAuth; Kimi and Grok use device login; Anthropic has a
PKCE/copy-code flow; MiMo and other providers accept keys. The main coding loop
uses OpenAI by default, with explicit OpenRouter Responses support. See
[provider routing, prompt caching and usage](docs/TOKENOMICS.md) for selection and
limits. Direct context-only colleagues can use the wider provider registry.
Installed Codex/Claude compatibility paths remain available until owned accounts
are selected. Live account sign-in and remote logout revocation remain follow-through.
See [provider authentication](docs/USING_BLACKBIRD.md) for commands and limits.

### Building this checkout

The build is deliberately tied to qualified development profiles. The local macOS profile uses Clang 23.1.2 and Lua 5.4.8; the Linux profile uses Clang 18.1.3 and libstdc++ 13. CMake presets contain host-specific tool paths. A fresh machine needs its profile configured deliberately, rather than assuming these paths will exist.

```sh
scripts/rigor doctor
scripts/rigor build release
./scripts/blackbird --session context/my-blackbird
```

Once the release build is configured, the ordinary native rebuild is:

```sh
cmake --build build/release --target blackbird
```

The [tooling guide](tooling/README.md) documents profiles, diagnostics, sanitizer and fuzz tooling, and Linux checks. Python is used by some development and test drivers; production agent logic is C++ and Lua. The present provider transport also uses the system's `curl` executable. There is no JavaScript application runtime or terminal framework underneath the conversation.

`BLACKBIRD_DEBUG` defaults to `OFF`. Normal release builds omit developer
profiling benchmarks and fault/fuzz probes. Raw instrumentation is compiled in
both modes and starts automatically for application sessions; read-only audit
inspection starts no capture run. Explicit debug, sanitizer and profile presets
enable developer machinery. See [debug builds](docs/DEBUG_BUILD.md).

For everyday operation, keep [Using Blackbird](docs/USING_BLACKBIRD.md) nearby. It contains the exact options, limits, Lua interfaces, context proposals, and recovery instructions. This README supplies the landscape; that guide supplies the roads.

## The person, the model, and the machine

The central relationship is collaboration between an operator and a capable model, with programs available to both. Programmability belongs in ordinary use. Changing a tool definition, trying a different turn loop, or composing an experiment should be a normal act within a session.

The operator can choose a direction, inspect evidence, interrupt a turn, and change the environment. The model can operate files and processes, write Lua, edit its context, propose compaction, report a complaint, and change the harness source. Those capabilities make the model an active participant in shaping its working conditions.

Changes normally take effect at the end of the current turn or affected workflow. That boundary gives a running program a coherent environment to finish in. The broader design also calls for an explicit interrupt-and-apply-now operation; today's interface offers cancellation, queued commands, workflow selection, and restart controls. Full hot configuration and every proposed activation mode are not yet implemented.

The core is native C++ because the underlying machinery matters: bytes, processes, time, ownership, storage, failure, and continuity. Lua provides a small, expressive place to redefine behavior while the system is running. Native hot-loading systems such as RCC++ and Godot-related mechanisms were investigated; no native plugin framework has been selected. The functioning rebuild path today is restart/resume/continue.

## A turn is a program

[`programs/turn.lua`](programs/turn.lua) implements the current request/tool loop. Each turn reads and retains its effective program source and starts a fresh Lua VM. Editing the file changes subsequent turns. A different program can be selected with `--workflow FILE` or `/workflow FILE`.

The host provides request, tool, context, inspection, and display interfaces:

```lua
local result = blackbird.call("read_file", {
  path = "README.md",
  line_start = 1,
  line_end = 30
})

local state = blackbird.stats()
blackbird.display(blackbird.json.encode(state))
```

`blackbird.request(options)` lets a program configure a model request, including its assembly and request-local choices. `blackbird.context()`, `blackbird.edit(...)`, `blackbird.manage(...)`, and `blackbird.inspect(...)` expose the working context and retained material. `blackbird.call(...)` gives Lua the same native tools used by the model. The model's Lua tool makes these interfaces available during normal work.

Lua globals are ephemeral between turns. Persistent state belongs in context, files, or services. Base, coroutine, table, string, math, and UTF-8 facilities are available; host effects go through the audited interfaces. Raw `io`, `os`, `package`, and `debug` libraries are absent. Named modules can be staged through `program_config` and imported with
`blackbird.module(name)` from retained, workflow-pinned source; explicit tool reads and
`load` remain available for experiments. This arrangement keeps ordinary effects visible in the audit. It does not claim hostile-code isolation.

The native engine underneath the program owns operation admission, observed outcomes, retained source material, and publication of accepted context. In that vocabulary, **admission** means recording an operation before dispatch. It is an internal execution record, not a request for the operator to approve a command. Dispatch, completion, failure, and an unknown outcome remain separate facts.

Callable Lua workflows can register slash aliases with a configurable prefix,
explicit bare names, and colored operator POWERWORDS. `/ultracode` is the first
built-in example; the operator and model can redefine the retained registry.
See [callable workflows](docs/CALLABLE_WORKFLOWS.md) for dispatch and activation.

Native decision-model calls are available through `decision_model`,
`blackbird.decide(...)`, and `/decision JSON`, with Jev as the first adapter.
Batched Choice/Score/Noul results retain probabilities, usage and audit linkage;
workflow code chooses what to do with them. See [decision models](docs/DECISION_MODELS.md).

The native [colleague adapter](docs/COLLEAGUE.md) is built as
`build/release/blackbird-colleague`: one selected-context OpenAI or Claude request,
with retained request/result captures. Workflows can call it through audited `exec`.
It is a synchronous adapter, with wider provider coverage still ahead.

The current loop is synchronous. Its programmable seam is real; an arbitrary concurrent multi-model scheduler is still a design objective.

## Context is local data

The conversation a provider sees is a working presentation. It can be selected, rearranged, summarized, repaired, and studied. The original material has its own identity and history.

This distinction is foundational. **Context Language Models are part of the first-pass core**, informed by the [CLM research and source study](papers/2026-10-01-context-language-models-study.md). The model can author transformations of its own context and evolve those transformations. It can also discover that a summary was overzealous and retrieve what it needs to repair the damage.

The implemented managed operations are:

| Operation | Effect on the working presentation |
| --- | --- |
| Summarize | Replace selected material with a new summary and retained ancestry. |
| Select | Keep a selected presentation while enforcing required structure. |
| Archive | Remove selected material from the active view while preserving originals. |
| Restore | Reintroduce exact originals in capture order and repair selected presentations. |

Managed proposals use revision checks. Supplied bases are strict; stale proposals are retained as rejections. Complete call/result intervals stay together, and live user, system, and developer instructions are conservatively protected. Proposals made during a workflow stage until successful completion. Failed or interrupted workflows cancel them.

Structural acceptance cannot determine whether a summary preserved the right facts. Repair remains a first-class operation. Bounded inspection can retrieve index, history, and original bytes without pouring the complete archive back into a provider request. The managed-compaction campaign exercised actual model summaries, deliberate omissions and exact repair, live archive/restore, reopen, and rebuild continuity. Its [report](papers/2026-10-06-compaction-campaign.md) records the evidence and the limits of the claim.

Automatic compaction policy remains separate. A smaller provider context also does not make the physical audit smaller or make every replay cheaper. Long campaigns have made that distinction painfully concrete.

## The audit remembers what the conversation forgets

Blackbird's audit is a core instrument for diagnosis and improvement. It records operation inputs and dispositions, original provider requests and streamed bytes, context revisions, effective program source, file prior/proposed bytes, and process output. Causal identities connect decisions, invocations, attempts, and observed results. Authentication credentials are excluded from the provider audit interface.

An accepted answer does not retroactively make an earlier failed request succeed. A process that timed out may already have performed external effects. A command whose disposition is unknown cannot safely be treated as a command that never ran. Those distinctions affect what happens next.

The local audit explorer exposes committed records, causal references, and exact source bytes in bounded pages. Process output has retained references, so the model can inspect a later range without rerunning the command. Inspection records are evidence about observed operations; admission alone establishes no success.

The physical journal has checked frame and batch limits, staged recovery, and typed semantic records. The current coding path uses a single bounded journal. More elaborate retained-environment and custody machinery exists in the broader core work, but the full U0–U9 specification is not accepted as complete.

### A real limit, and the work it caused

An autonomous development session reached the configured **512 MiB audit limit**. Its originals were preserved at their existing paths. Work continued in a fresh session with an explicit account of the old session's unsettled final state. The completed experiment was not rerun.

That failure drove concrete work: physical byte/record headroom is now exposed, and an explicit successor-seed interface can initialize a genuinely fresh session with selected context and declared source lineage. The destination receives fresh identities. It does not inherit old admissions, verify the source's settlement, or replay its effects.

**Bounded workflow-capacity protection is now implemented and activated.** Turns reserve room for stop/cancellation records and refuse prospective obligations that cannot fit the physical limits. Capture refusal retains earlier fragments, reports received-but-unretained bytes, and preserves unknown outcomes. Explicit successor initialization is available; automatic recovery/handoff and targeted predecessor-original access remain queued. The physical caps are unchanged. The [capacity study](docs/HISTORY_CAPACITY_SUBPLAN.md) and [settlement subplan](docs/CAPACITY_SETTLEMENT_SUBPLAN.md) describe the measured growth and the implementation seam.

## The terminal is a place to work

The TUI keeps a conversation, activity display, and multiline composer on screen. Assistant text streams during inference. Tools show commands or paths, output, timing, and observed results. Scrolling can remain anchored while new output arrives. The Blackbird in the corner gently bobs and his plume shimmers while a turn is underway.

The [task pane](docs/TASKS.md) sits at the far right below the sprite. It shows
durable tasks and one level of subtasks, blockers, owners and explicitly bound
command activity. Models and Lua read and edit the same state through
`tasks_read`/`tasks_edit`; `/tasks help` lists operator controls.

The basic controls are intentionally ordinary:

| Control | Action |
| --- | --- |
| Enter | Submit; during a turn, queue the next prompt. |
| Alt-Enter / Ctrl-J | Insert a newline. |
| PgUp / PgDn | Move through the conversation. |
| Up / Down | Recall submitted prompts. |
| Ctrl-C | Stop the active turn, clear its queue, and preserve the current draft. |
| Ctrl-Q | Exit with drafts and queued work preserved as recoverable drafts. |
| `/exit` or `/quit` | Exit, stopping active work first. |
| `/session`, `/stats`, `/context` | Inspect settings, sizes, or current context. |
| `/name`, `/sessions`, `/resume DIRECTORY` | Name, select and return to saved working state. |
| `/runs`, `/run`, `/send`, `/await`, `/join`, `/stop` | Observe and control bounded selected-context participants. |
| `/new` | Create and enter a fresh saved session beside the current one, keeping model, effort and workflow settings. Waits behind active work; old context, tasks and drafts remain saved. |
| `/workflow FILE` | Select the turn program. |
| `/restart NOTE` | Arrange restart/resume/continue. |

Drafts and prompt history survive ordinary reopen and restart. Previously queued prompts reopen as drafts requiring explicit submission, rather than silently running. Display trimming and composer limits are separate from the original audit and provider context. Complex grapheme editing and richer terminal views remain future work.

The external **arcoboard** is a different tool: a project view of queued work, issue status, recent activity, and campaign process observations. It lives in [evotools](https://github.com/antimemeai/evotools), a private companion repository, so watching the project does not burden the harness with a dashboard server or display dependencies. Its records and snapshots let operator and model discuss the same work. Recorded issue status and observed process state remain distinct.

## Build the ship while sailing it

The first milestone was always to reach the point where we could develop Blackbird in Blackbird. The practical mechanism turned out to be straightforward:

**edit → test → build → restart → resume the session → continue.**

The launcher recognizes a requested restart and starts the replacement executable against the same session. The new process preserves settings, identities, and context, then injects a continuation with the retained note. The original operator prompt is not resubmitted.

Restart is deferred to the successful turn/tool boundary. Pending call linkage or unfinished admitted effects blocks it; failed or interrupted turns cancel the request. An ordinary reopen does not consume a pending continuation intent. Explicit `--resume-continue` is the recovery control when the launcher failed. These details prevent a convenient restart button from becoming an accidental side-effect replay mechanism.

Real models have edited C++ tools, exercised a failing regression, corrected the implementation, rebuilt the agent, and continued in the replacement. Subsequent autonomous campaigns have shipped compaction, observability, candidate tooling, and resilience changes through this loop. The operator has accepted the first milestone: Blackbird is developing Blackbird. Further comfort and resilience improvements remain active work; the broader core specification is still unfinished.

The larger **autodroit** design extends this agency from a few manually directed changes to model-governed research and improvement with operator steering. Outposts offer a future way to carry selected context into another working environment, including a temporary colleague during a core refit. Refit must quiesce this harness's active programs and provider activity. Standing work owned by this harness is intended to pause and survive; uninterrupted services belong in independently managed processes. Full outpost/refit and standing-worker continuity are not implemented in the current bootstrap.

## Failures should change the approach

A tool deadline now returns retained partial output, a timeout marker, and an unknown effect disposition. The model can inspect that output, split the work, choose a new attempt, or take another approach. The local child group is closed and reaped. Already-performed external effects remain possible. Explicit operator cancellation still stops the turn.

Provider transport has its own native retry path. A request can retry a classified transient transport or selected HTTP failure without restarting the Lua workflow. Each attempt has its own identity, original partial stream, and disposition. Only an accepted complete response can enter context or cause tool dispatch.

The default policy is five total attempts with bounded exponential backoff. Programs can configure a request-local policy; cancellation remains responsive while waiting. Authentication, configuration, malformed responses, and local audit failures are not treated as transient network outages. Failed-attempt billing is unavailable when the provider does not supply it, and is never counted as zero. See the [retry report](papers/2026-10-06-live-provider-retries.md) for direct fault tests, an independent review finding and its fix, and actual native activation. Live activation is not a claim that a spontaneous real outage was observed recovering.

An external campaign supervisor currently provides longer-lived provider-only recovery beyond a single request's retry budget. It is construction tooling in evotools, not a guarantee of survival across machine restart or every failure class. Audit capacity is not a network failure.

The [tokenomics frumentarii](papers/tokenomics-2026-10-14/SYNTHESIS.md) studies
prompt caching, request layout, per-attempt usage and recoverable context views.
The resulting [implementation](docs/TOKENOMICS.md) adds account-bound retries,
OpenRouter main coding, explicit prompt-cache policy and a native tertiary usage
reader. Changing task/budget state follows reusable input rather than changing
the leading instructions. `/stats` retains selected last-response counters; the
separate reader reports known consumption and missing coverage, never a complete
session bill. Subscription cache controls and actual cache savings remain unqualified.

## Complaints, candidates, and the possibility of learning

The model can complain. `rageshake` captures an observation together with context, program generation, model settings, actor/workflow identities, and audit locators. Local complaint detail is retained before advisory bead delivery. Delivery failure does not erase the observation or create an automatic retry of an uncertain effect. There is no obligation to interrupt the present task and repair the complaint immediately. The eventual external complaint database remains unconfigured.

Evolution candidates use **Git branches and commits for retained source**, with a configurable bounded checkout pool for actual execution. Serial experiments reuse a checkout once its users have stopped and its state is explicitly settled. Overflow queues. Source and experiment artifacts survive checkout retirement. Archiving source, qualifying behavior, and activating an implementation are different actions; pushing a branch establishes only the first.

This matters because autonomous improvement can otherwise bury itself under worktrees, repeated reviews, and elaborate declarations of confidence. Blackbird's working rules bound retries and review loops. A passed check stays settled until a relevant change or new evidence gives a reason to revisit it. Reviews attack consequential fault classes. They do not recursively certify each other.

The useful-work interstitial supplied audit exploration, complaints, editable Lua feedback and contrast records, branch candidates, and a small matched experiment. It compared ordinary information gathering with bounded-first reading on twelve fresh task sessions. All twelve artifacts were accepted; latency and resource outcomes were mixed. The disposition was **revise the unconditional policy**. There was no defensible general speedup claim.

The [pilot report](papers/2026-10-06-useful-work-pilot.md) retains measured outcomes, request identities, instrumentation corrections, deadline-setting deviations, and unavailable supervisor/attention costs. That is the intended relationship between research and evolution: preserve the results, change the policy when the results warrant it, and ship useful work along the way. Token minimalism is not the goal. Correctness, latency, resources, and operator attention each matter.

## Beyond one seat

Blackbird is designed toward heterogeneous colleagues: OpenAI, Kimi, MiMo, Claude, Grok, and whatever future providers make possible. The desired limit is upstream capability and explicitly configured operating policy. Cross-provider delegation and model limits belong in that design. The main coding connection defaults to OpenAI and also supports explicit OpenRouter Responses. The native `blackbird-colleague` library/CLI and shared model/Lua/operator surface support bounded selected-context OpenAI/Claude calls. Native participants provide concurrent requests, addressed direction at request boundaries, observe/await/join, explicit cancellation requests and task badges. A live OpenAI colleague/main-model repair worked; the single Claude trial timed out unknown. Workers remain context-only; useful live heterogeneous coding and remote tool-using outposts remain unfinished. See the [campaign results](papers/2026-10-08-collaboration-campaign.md).

Two operating styles are also on the road ahead. **Campaign mode** is the operator in the seat, steering sustained work. **Station mode** is an agent listening for an explicit feed or trigger and acting in the background. Campaigns have run with Lua and external supervision. An opt-in native local-file station is delivered, with retained event admission/deduplication and boundary inspect/steer/pause/resume/stop controls. Bounded local asynchronous participants are delivered separately. General trigger connectors and live TUI attachment remain future work.

Common integrations should arrive as optional packages: GitHub, Linear, Slack, Discord, Hugging Face, and others according to actual need. A minimal installation should stay small. The proposed HUD is a thematic feed relevant to a project set: security advisories, research papers, financial events, or other selected sources. Receiving an event, displaying it, including it in context, and acting on it are separate operations.

**Multiplayer means independent Blackbirds sharing work over a network.** Start with two; preserve the design for many. The intended communication is peer to peer and end-to-end encrypted, with agent–agent, human–human, and human–agent interaction. An integrated IRC-style channel can be enough. Richer collaboration can share a problem, files, or build identities; inter-Arco model mail is another possibility. Independent conversations need not become a single merged context. This is a design direction, not an implemented network protocol.

Kernels, standing databases, and computational services can serve an army of Blackbirds. Blackbird consumes them. Their provisioning, scaling, and control plane belong to the eventual project fabric. Restarting one harness must not halt a shared database simply because that harness used it.

The [participants and modes brief](docs/PARTICIPANTS_AND_MODES.md) and [integration research](papers/integrations-2026-10-06/SYNTHESIS.md) develop these questions, including Claude Code's teams and mods. Reference acquisition is research, not dependency adoption.

## How the work is done

[Blackbird](BLACKBIRD.md) is the working doctrine. It asks for aggressive progress underwritten by discipline: read the literature and source, make the design and plan explicit, choose tests with real oracles, fix consequential findings, and finish useful conceptual units. We abhor ceremony and unsupported claims.

The rigor stack includes strict compiler diagnostics, focused static analysis, debug and optimized builds, ASan/UBSan, TSan, fuzz tooling, and direct behavioral tests. Ordinary Linux checks run on Neuroses. Tool availability does not establish product correctness, and a sanitizer pass does not establish a protocol contract. Tests are selected for the fault class and the actual changed behavior. Mutation campaigns are deferred until a mature suite merits them; mutations run on the fleet, never the laptop.

Independent Kimi and ChatGPT reviews have found real defects. Unavailable credit, an authentication failure, or a timed-out reviewer is reported as such. Advisory Jev judgments are available for structured decisions and experiments; they do not replace exact checks or become approval gates.

Literature and reference implementations are first-class working material. The repository contains an agent capabilities survey and studies of CLM, systems rigor, harness evolution, orchestration, integrations, and evaluation. Acquired references stay quarantined and have restoration manifests. We study mechanisms and testing strategies; studies do not silently select production dependencies.

## Find your way through the repository

| Place | What belongs there |
| --- | --- |
| [`src/`](src/) and [`include/blackbird/`](include/blackbird/) | Native foundation, journal, context, provider, tools, sessions, terminal, and candidate machinery. |
| [`programs/`](programs/) | Turn programs, useful-work helpers, and experiment instrumentation. |
| [`tests/`](tests/) | Direct behavioral, recovery, protocol, tool, and terminal oracles. |
| [`scripts/`](scripts/) | Launcher, build/check utilities, and development exercises. |
| [`tooling/`](tooling/) | Qualified host profiles, diagnostic configuration, colleague and Jev guides. |
| [`docs/`](docs/) | Operator intent, design contracts, subplans, operating guide, and queued work. |
| [`papers/`](papers/) | Literature studies, independent reviews, campaign results, and measured consequences. |
| [`JOURNAL.md`](JOURNAL.md) | Decisions, completed actions, findings, and remaining scope. |
| [`QUARANTINE.md`](QUARANTINE.md) | Reference sources and restoration instructions. |
| `context/`, `quarantine/`, `build/` | Ignored working evidence, acquired references, and build products. |

For the conceptual route, read [Foundation](docs/FOUNDATION.md), then the [core design](docs/CORE_DESIGN.md). For the implementation route, start with [Using Blackbird](docs/USING_BLACKBIRD.md), the [implementation plan](docs/IMPLEMENTATION_PLAN.md), and the [autodevelopment queue](docs/AUTODEV_QUEUE.md). For the empirical route, read the [agent survey](papers/capabilities/README.md), [harness-evolution synthesis](papers/frumentarii-2026-10-06/SYNTHESIS.md), and the campaign reports linked above. Older plans and journal entries describe their time; current source and later explicit operator decisions resolve their status.

The [current assessment](papers/2026-10-08-consolidation-and-field-assessment.md) and [proposed next campaign](docs/NEXT_CAMPAIGN.md) describe the consolidated state and priorities.

Issues use the repository's beads database. Supported backups live under `.beads/backup/`. Raw session audits, provider history, working captures, acquired references, PDFs, and build products stay out of source publication.

## What happened to the previous Blackbird?

This is a reconstruction, not an uncritical continuation of inherited code. The earlier implementation and its plans were assessed, preserved, and moved to ignored `quarantine/arconaut/`. Archived instructions are historical reference. The [assessment baseline](docs/RESTART.md) and source manifests retain what was found and how to recover it.

The fresh C++/Lua implementation follows the operator's renewed brief. Historical Git records preserve the old project; current campaign branches preserve source and experiments from the reconstruction. A branch is a useful identity for work, not a reason to postpone integration forever.

Blackbird already does useful work on itself. The next obligation is to make that work comfortable, resilient, inspectable, and increasingly capable—while retaining enough evidence to tell whether the changes helped.

## License

Blackbird's original software and documentation are released under the
[MIT License](LICENSE). Copyright © 2026 Patrick Beam.

Third-party material retains its original license.


Before committing in a new clone, run `scripts/install-hooks`. The tracked
pre-commit hook runs full debug tests and owned C++/Lua lint. The operator's
text-only exception uses relevant document checks instead; see
[development tooling](tooling/README.md#required-pre-commit-checks).
