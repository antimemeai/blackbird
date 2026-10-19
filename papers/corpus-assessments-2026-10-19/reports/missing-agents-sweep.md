# Coding agents and harnesses missing from the Blackbird corpus (survey of 2026-10-19)

## Method and caveats

- **Baseline.** The corpus listing (`quarantine-ls.txt`) plus the origins and revisions in `papers/2026-09-30-known-agent-acquisition.json`, `papers/2026-09-30-discovered-agent-acquisition.json`, `papers/workflows-2026-10-14/agent-acquisition.json`, `papers/legacy-reference-catalog.json`, the frumentarii harness manifest (`papers/frumentarii-2026-10-06/harness/manifest.json`) and `QUARANTINE.md`.
- **Sources swept.**
  - AutoJunjie/awesome-agent-harness.
  - The ACP agent list at agentclientprotocol.com (43 agents).
  - bradAGI/awesome-cli-coding-agents and echoVic/awesome-cli-coding-agents (458 distinct repo links, each checked against the GitHub API).
  - The Harbor agent-adapter registry (about 50 installed-agent adapters).
  - Web searches covering July to October 2026 releases.
  - Vendor documentation for the closed-source agents.
- **Verification.** Every repo URL below was resolved against the GitHub REST API on 2026-10-19. The API also supplied canonical name after renames, language, SPDX license, stars, created and last-push dates, archive and fork status. I used read-only `gh api` metadata, README and tree reads plus WebFetch. Nothing was cloned, downloaded into the project, installed or executed.
- **Staleness.** I ran the compare API from each pinned corpus revision to the upstream default branch.
- **Figures.** Stars and dates are as of 2026-10-19. "Last" means the last push. Several mechanism descriptions come from READMEs and are vendor claims, not verified behaviour.

---

## Tier A: acquire (distinctive mechanism relevant to the Blackbird goals)

| # | Name | Canonical repo | Lang / license | Activity | Lineage | Mechanism worth studying |
|---|---|---|---|---|---|---|
| A1 | **harness2** (Google Research / Cloud AI Research) | https://github.com/google-research/harness2 | Python / Apache-2.0 | 9.1k stars; created 2026-10-06; last 2026-10-14; 3 commits (one release dump) | New. Uses OpenCode (default) or Codex as executors, both already in the corpus | Recursive harness optimization with frozen weights, at two levels. Task-level recursion uses Propose-Probe-Compose: it probes K general and K domain harness edits and composes winners from behavioural contrasts between runs, without ground truth. Harnessing-level recursion keeps a persistent "improvement harness" whose adopted edits and lessons carry forward. Eight editable components: system prompt, guardrails, memory, tool guidance, subagents, skills, scripts, and plugins, where plugins are "middleware". Parallel and sequential scaling modes; resumable runs. Direct input for autodroit and self-improvement. The README warns that executor network access is unrestricted and that Codex runs with its sandbox disabled. |
| A2 | **DeepSeek Harness (`dsh`)** | https://github.com/deepseek-ai/deepseek-harness | TypeScript (+ Python SDK, `native/`) / MIT | ~254k stars; created 2026-08-13; last 2026-10-17; ~22k commits; developer preview | New, first-party DeepSeek. Built on the Cordis plugin-context framework from the Koishi lineage | "Everything is a plugin" on Cordis: plugins contribute services, typed events and **reversible effects that unwind on unload**, so there is no privileged core. Profiles, bundles and `cordis.patch.yml` stack config layers, every row is patchable, and config hot-module reload (HMR) is on in the web profile. An **append-only SessionEvent log is the source of model context**. Its rule is "model-visible means logged", so every request must be reconstructable from the log, and pure message projections apply on top. An `agent/pre-step` hook can rewrite or reject claimed input. The design also covers fork-at-turn-boundary, capability seams (fs, subprocess, sandbox and terminals move together), and experimental Agent Teams with durable roster and inbox. This is the closest published analogue to Blackbird's originals, audit, CLM and hot-reload requirements. `docs/architecture.md` is excellent. |
| A3 | **OpenHands Software Agent SDK** | https://github.com/OpenHands/software-agent-sdk | Python / MIT | 1.3k stars; created 2025-08-23; last 2026-10-19 | OpenHands V1 core. **The corpus `openhands` snapshot contains only the TypeScript "Agent Canvas" front end (9 .py files)**; its README points here for "agents, tools, conversations, workspaces, events, and the canonical server API" | Event-sourced conversation model, Agent Server REST/WS API able to run many agents per host, workspace abstraction. Without this the corpus has no OpenHands agent loop at all. |
| A4 | **Kimi Code CLI** | https://github.com/MoonshotAI/kimi-code | TypeScript / MIT | 7.9k stars; created 2026-05-22; last 2026-10-19 | Successor to `MoonshotAI/kimi-cli`, **archived** ("Legacy Python Kimi CLI, no longer maintained"). The corpus holds the archived Python one | A single-binary TypeScript rewrite. Parallel built-in subagents (coder, explore, plan) in isolated contexts, lifecycle hooks, a plugin marketplace that shows each install's trust level up front, conversational MCP configuration, video input, and `kimi acp`. Relevant because the operator's Kimi colleague tooling targets this vendor. `papers/2026-10-08-consolidation-and-field-assessment.md` already notes it but has not acquired its source. |
| A5 | **jcode** | https://github.com/1jehuang/jcode | Rust / MIT | 21k stars; created 2026-01-05; last 2026-10-19; ~10.5k commits | New, independent | **Self-dev mode**: on request the agent modifies and rebuilds its own source. **Server-mediated swarm**: several agents in one repo are coordinated by a server that notifies agent B when agent A edits a file B has read, and agents can spawn their own swarms with a swarm tool. Low per-session RAM (~6 MB per extra headless session, as claimed). Relevant to self-modification and multi-agent live collaboration. It is already cited in `papers/2026-10-19-agent-viz-references` for visualisation only. |
| A6 | **maki** | https://github.com/tontinton/maki | Rust + ~1 MB Lua / MIT | 1.2k stars; created 2026-02-13; last 2026-10-18; ~2.3k commits | New | **Native core with Neovim-style Lua plugins**: builtin plugins are in Lua, and `maki.provider.register` lets Lua add model providers. A tree-sitter bash parser drives permissions (`git diff && rm -rf /` is not treated as `git *`). An `index` tool returns tree-sitter file skeletons. A `code_execution` tool exposes every other tool as an async function inside a sandboxed interpreter. Weak, medium or strong model choice per subagent; each subagent gets its own visible chat window. This is the architecture Blackbird chose, built in Rust. |
| A7 | **smelt** | https://github.com/leonardcser/smelt | Rust + ~1 MB Lua / MIT | 55 stars (low), but ~3.4k commits; created 2026-02-25; last 2026-10-15 | New | **Lua-scriptable "like Neovim"**: keymaps, commands, autocmds, custom tools and **custom modes** are all Lua. Its own grid and layout terminal renderer (not ratatui), a vim editor, and **deterministic fuzzing with fixed clock and stubbed I/O so any crash replays**. That bears on both the Lua presentation work and the AFL campaign. It has a Lua API reference site. |
| A8 | **agentty** | https://github.com/agentty-org/agentty | **C++26** / MIT | 625 stars; created 2026-04-17; last 2026-10-19; ~3.3k commits; ~12 MB of C++ | New. The only substantial C++ coding agent found | Native single static binary built with CMake. Its own C++ libraries are submodules: `acp-cpp` (C++20 ACP, "first C++ ACP library"), `mcp-cpp` (C++23 MCP), `jsonrpc-cpp`, `maya` (C++26 TUI with a type-state DSL, Yoga flexbox, SIMD cell-diff renderer and Elm-style runtime), `claybin` (sandbox "authority compiler": policies form a meet-semilattice, so composition can only restrict; seccomp, landlock, cgroup2 and seatbelt) and `rag-cpp`. It begins read-only tools while the model is still streaming. Caveat: `maya`, `acp-cpp` and `mcp-cpp` declare no license, so they are study-only. Performance claims are unverified. |
| A9 | **Zed agent crates** | https://github.com/zed-industries/zed (`crates/acp_thread`, `agent`, `agent_servers`, `agent_ui`, `agent_skills`, `acp_tools`) | Rust / GPL-3.0 + Apache-2.0 (gpui) | 92k stars; last 2026-10-19 | Originator of ACP. The corpus has many ACP *agents* but no production ACP *host* | Reference ACP client and host in a native, GPU-rendered, multiplayer editor: thread model, external-agent process management, and its native "Zed Agent" loop. Acquire the crate subset at a pinned revision. |
| A10 | **CodeCompanion.nvim** | https://github.com/olimorris/codecompanion.nvim | Lua / Apache-2.0 | 6.9k stars; last 2026-10-19 | New | A pure-Lua agent and chat harness hosted inside Neovim. It is an **ACP client written in Lua**, with tools, "agentic workflows", slash commands and editor-context injection. It is the most mature published example of a Lua layer composing agent interactions over a native host. Secondary: avante.nvim (B). |
| A11 | **Harbor (Terminus 2)** | https://github.com/harbor-framework/harbor (`src/harbor/agents/terminus_2/`) | Python / Apache-2.0 | 6.2k stars; created 2025-08-04; last 2026-10-18 | Successor to Terminal-Bench (`laude-institute/terminal-bench` → `harbor-framework/terminal-bench-1`) | **Terminus 2**: a minimal harness whose only tool is keystrokes into a tmux session. It has JSON and XML plain response parsers, timeout templates, asciinema capture, and **context summarization with golden trajectory tests** (`tests/golden/terminus_2/*context-summarization*`). Harbor's `agents/installed/` holds adapters for ~50 agents (grok_build, kimi_code, muse_code, mimo, antigravity_cli, cursor_cli, copilot_cli, devin, junie, rovodev_cli, cortex_code, fx, mcode and others), which is a live index of the landscape. Evals stay deferred. **Correction found late: the corpus `evo-harness-modularrsi` is a Harbor fork and already contains `src/harbor/agents/terminus_2/` and `terminus_2_modular/`, plus the golden tests.** Upstream Harbor is therefore effectively **Tier B**: a refresh that adds the current adapter registry. It is not a missing mechanism. |
| A12 | **ACE: Agentic Context Engineering** | https://github.com/ace-agent/ace | Python / Apache-2.0 | 1.4k stars; created 2025-11-16; last 2026-08-24 | Stanford/SambaNova paper code (arXiv 2510.04618) | **Model-curated evolving context**: playbook bullets carry section IDs and helpful/harmful counters. A generator, reflector and curator apply incremental *delta* edits rather than rewrites, which the paper credits with avoiding "context collapse" and "brevity bias". Direct input for first-pass CLM: model-editable live context and model-authored transformations. |
| A13 | **Grok Build** (xAI / "SpaceXAI") | https://github.com/xai-org/grok-build | Rust (~76 MB) / Apache-2.0 | 27k stars; created 2026-07-14; last sync 2026-09-29; 51 commits (periodic monorepo syncs with `SOURCE_REV`) | New vendor lineage | A fullscreen, mouse-interactive TUI pager crate, separate from the agent runtime crate, which has leader, stdio and headless entry points. A workspace crate covers host filesystem, VCS, execution and **checkpoints**. MCP, skills, plugins, hooks, sandboxing and ACP are documented in-tree. A large, recent vendor-quality Rust harness that is not a Codex fork. |

**Also Tier A, but a refresh rather than a new acquisition: OpenCode → `anomalyco/opencode`.** See the stale-snapshot list. OpenCode 2.0 (public beta July 2026, 2.0.0 on npm 2026-09-11) brought a rebuilt plugin system with **hot reloading**, a shared background service linking the TUI, web and desktop front ends, an embeddable SDK, and background subagents and shell. The corpus snapshot is from 2026-06-07 and predates all of it.

---

## harness2

**Identified** (the operator confirmed the repo): https://github.com/google-research/harness2.

| Field | Value |
|---|---|
| Title | "Recursive Agent Harnessing for an Open World" |
| Language | Python (≥3.12, `uv`/`pyproject.toml`) |
| License | Apache-2.0 |
| Activity | 9,131 stars, 799 forks; created 2026-10-06; last push 2026-10-14; 3 commits. Press reports date the public release to 2026-10-09 by Google Cloud AI Research, with Claude Opus 4.8 as the editor model. That model claim comes from secondary coverage and is unverified. |
| Kind | A harness optimizer, not an interactive coding CLI. It drives OpenCode or Codex as executors on JobBench, LAB and WorkBuddy-Bench. |
| Distinctive mechanism | Two-level recursion. (1) Task-level Propose-Probe-Compose refines the execution harness for the current task from behavioural contrasts between executions, without ground-truth feedback. (2) Harnessing-level recursion updates a persistent improvement harness so adopted edits and lessons carry across tasks. A harness editor and a composer test "invariant" and "domain" edits in candidate probes. |
| Corpus relation | Not in the corpus; created after the 2026-09-30 snapshot round. Thematically adjacent to `self-harness`, `auto-harness`, `gepa`, `evo-harness-*` (DGM, SICA, ModularRSI and others) and the not-yet-acquired Meta-Harness (B). |

---

## Tier B: nice to have

| Name | Repo | Lang / license | Activity | Lineage | Mechanism |
|---|---|---|---|---|---|
| hax | https://github.com/OleksandrChekhovskyi/hax | C / MIT | 924 stars; created 2026-04-24; last 2026-10-17; ~415 commits | New | Minimalist native C agent. Only the streaming line and input area redraw, and **native scrollback is preserved**, which matches Blackbird's partial-write output. Ctrl+T transcript of exactly what was sent and received; optional wire-protocol trace. Tests are tmux-driven. Composes through subprocesses instead of plugins, and `docs/philosophy.md` justifies each omission. |
| Warp (client now open) | https://github.com/warpdotdev/warp | Rust / AGPL-3.0 (UI crates `warpui*` MIT) | 65.5k stars; opened 2026-04-27 to 30; last 2026-10-19 | Previously closed | Block-structured terminal with an agent mode and a GPU UI framework. The server stays closed. Relevant to the TUI doctrine (blocks, command/agent interleaving), though it is a GUI terminal rather than a TUI. |
| Toad | https://github.com/batrachianai/toad | Python (Textual) / AGPL-3.0 | 3.5k stars; last 2026-05-26 (slowing) | Will McGugan | ACP client TUI with a **real embedded shell** (full colour, interactive commands, tab completion) that interleaves agent prompts with ordinary terminal work. |
| Ante | https://github.com/AntigmaLabs/ante | Rust / Apache-2.0 | 2.0k stars; created 2025-12-23; last 2026-10-19 | New | `/term` gives the user and Ante a **shared view of other agents' interactive sessions** (claude, codex, ante in tmux): Ante drives them, the user steps in, and conversations can be forked or handed off. One declarative profile defines a whole agent, including a replacement system prompt. Continuous public same-model, cross-harness Terminal-Bench runs. Embedded managed llama.cpp. |
| hcom | https://github.com/aannoo/hcom | Rust / MIT | 579 stars; last 2026-10-15 | New (already mentioned in `papers/capabilities`) | Cross-vendor agent messaging: agents message, watch and spawn each other across terminals (Claude Code, Codex, Antigravity, Cursor). Bears on multi-model live collaboration. |
| herdr | https://github.com/herdrdev/herdr | Rust / Apache-2.0 | 44.7k stars; created 2026-03-27; last 2026-10-18 | New; compare corpus `phux` and `ntm` | Agent-runtime multiplexer with a background server: detach without stopping work, restore layout after a restart, plugin marketplace. |
| Every Code | https://github.com/just-every/code | Rust / Apache-2.0 | 4.0k stars; last 2026-09-29 | **Fork of openai/codex** | Auto Drive (long autonomous runs), Auto Review decoupled from the command flow, multi-vendor agents racing or reaching consensus inside Codex, browser integration, compaction and archival of terminal-agent payloads. |
| Codebuff / Freebuff | https://github.com/CodebuffAI/freebuff (renamed from `CodebuffAI/codebuff`) | TypeScript / Apache-2.0 | 13.6k stars; last 2026-10-19 | Codebuff | A multi-agent pipeline of specialised agents for context gathering, planning, editing, research and review, plus an agent-definition SDK for custom agents. |
| Octofriend | https://github.com/synthetic-lab/octofriend | TypeScript / MIT | 1.0k stars; last 2026-10-16 | New | Small **autofix models** (for example `syntheticlab/fix-json`) repair malformed tool calls and edits for any coding LLM; model switching mid-session. |
| Claw Code | https://github.com/ultraworkers/claw-code (formerly `instructkr/claw-code`) | Rust / MIT | 195k stars; created 2026-03-31; last 2026-10-19 | Clean-room Rust reimplementation of Claude Code after the March 2026 source leak | Compare with the corpus `claude-code-unofficial` (leaked TypeScript) and `claude-code-official-distribution`. It shows which Claude Code behaviours survive a reimplementation. |
| Codewhale (formerly DeepSeek-TUI) | https://github.com/codewhale-hq/Codewhale (old URL `Hmbown/DeepSeek-TUI` redirects) | Rust / MIT | 41k stars; created 2026-01-19; last 2026-10-19 | Unofficial; not DeepSeek | A `/receipts` effect ledger listing changed files, commands, web and MCP calls; approvals. |
| Meta-Harness | https://github.com/stanford-iris-lab/meta-harness | Python / MIT | 1.7k stars; last 2026-10-02 | Paper arXiv 2603.28052 (Lee, Khattab, Finn et al.) | An outer-loop proposer agent (Claude Code) reads every prior candidate's **source, scores and traces from a filesystem** to search over harness code; reported gains on TerminalBench-2. It is already cited in `papers/2026-09-30-autodroit-frumentarii.md` but not acquired. |
| Live-SWE-agent | https://github.com/OpenAutoCoder/live-swe-agent | MIT | 467 stars; last 2026-01-19 | Built on mini-swe-agent (in corpus) | Runtime self-evolution: the agent writes and revises its own tools during a task. |
| Moatless Tools | https://github.com/aorwall/moatless-tools | Python / MIT | 642 stars; last 2025-09-01 (dormant) | SWE-Search | Monte Carlo tree search (MCTS) over agent trajectories with a value function, so sessions branch and backtrack. |
| Agentless | https://github.com/OpenAutoCoder/Agentless | Python / MIT | 2.1k stars; last 2024-12-22 (dormant) | Baseline | A fixed localize → repair → validate pipeline; a non-agentic control. |
| Confucius Code Agent | https://github.com/facebookresearch/cca-swebench | Python / MIT | 42 stars; last 2026-05-21 | Meta and Harvard (arXiv 2512.10398) | Confucius SDK with AX/UX/DX layering, cross-session note-taking, and a meta-agent build-test-improve loop over agent configurations. The repo may be partial. |
| avante.nvim | https://github.com/yetone/avante.nvim (canonical `avante-corp/avante.nvim`) | Lua (+ Rust) / Apache-2.0 | 18k stars; last 2026-10-19 | New | A second Lua-hosted agent in Neovim with a Cursor-style apply/diff flow. |
| agent-shell | https://github.com/xenodium/agent-shell | Emacs Lisp / GPL-3.0 | 2.0k stars; last 2026-10-18 | New | ACP client in native Emacs buffers and bindings, with no terminal emulation. An expert-programmable host. |
| gptel | https://github.com/karthink/gptel | Emacs Lisp / GPL-3.0 | 3.6k stars; last 2026-10-18 | New | A highly composable LLM client with tools and presets, editable in buffers. |
| ECA | https://github.com/editor-code-assistant/eca | Clojure / Apache-2.0 | 1.0k stars; last 2026-10-19 | New | An editor-agnostic agent server protocol shared by several editors. |
| oh-my-openagent (formerly oh-my-opencode) | https://github.com/code-yeongyu/oh-my-openagent | TypeScript / no SPDX | 70k stars; last 2026-10-19 | Layer over OpenCode | A large lifecycle-hook layer on OpenCode (an awesome-list cites 44 hooks), with orchestration modes. |
| Symphony | https://github.com/openai/symphony | Elixir / Apache-2.0 | 27.7k stars; created 2026-02-26; last 2026-09-15 | OpenAI | A BEAM daemon that turns tracker issues into isolated, autonomous Codex implementation runs. Compare the corpus `jido`. |
| GitHub Copilot SDK | https://github.com/github/copilot-sdk | Multi-language (Java the largest) / MIT | 10.6k stars; created 2026-01-14; last 2026-10-17 | Embeds the closed Copilot CLI agent | The public SDK contract for a closed agent runtime. |
| Kiro Crew | https://github.com/kirodotdev/KiroCrew | Python / Apache-2.0 | 4.5k stars; created 2026-07-16; last 2026-10-19 | AWS Kiro; runs on the closed `kiro-cli` | A persistent, self-improving workspace: unattended multi-step tasks, schedules, heartbeats, and "apps". |
| Raven | https://github.com/EverMind-AI/Raven | Python / Apache-2.0 | 5.5k stars; created 2026-05-21; last 2026-10-19; pre-alpha | New; on the ACP list | A host "harness of harnesses" built for recursive self-improvement; drives built-in and third-party agents. |

---

## Tier C: skip (duplicate lineage, dead, toy or out of scope)

| Name | Repo (verified) | Reason |
|---|---|---|
| Kilo Code / Kilo CLI | https://github.com/Kilo-Org/kilocode (TS, MIT, 27.7k stars) | Its CLI README says it is "a fork of OpenCode"; the extension descends from Roo/Cline. Duplicate lineage. |
| Zoo Code | https://github.com/Zoo-Code-Org/Zoo-Code (TS, Apache-2.0, 2.0k stars) | Community fork of archived Roo Code. |
| cecli (formerly aider-ce) | https://github.com/cecli-dev/cecli (fork of Aider-AI/aider) | Aider continuation; see the aider staleness note. |
| Kode CLI | https://github.com/shareAI-lab/Kode-CLI (renamed from `Kode`; TS, Apache-2.0, 5.2k stars) | A Claude-Code-alike. |
| OpenClaude | https://github.com/Gitlawb/openclaude → `Twigpine/openclaude` (34k stars, no SPDX) | Claude Code derivative; licensing unclear. |
| Claurst | https://github.com/Kuberwastaken/claurst (Rust, GPL-3.0, 10k stars) | A second Rust Claude Code reimplementation; keep Claw Code (B) instead. It does reimplement the Claude Code hooks contract if that is ever needed. |
| free-code | https://github.com/freecodexyz/free-code (8.8k stars, no license) | A modified Claude Code build with guardrails stripped. Skip. |
| grok-cli (Superagent) | https://github.com/superagent-ai/grok-cli (TS, MIT, 3.5k stars; last 2026-07-06) | Superseded by the official Grok Build. |
| nanocoder | https://github.com/Nano-Collective/nanocoder (TS, 2.5k stars) | Generic TS CLI. |
| MiniMax Code (`mcode`) | https://github.com/MiniMax-AI/minimax-code (TS, MIT, 2.0k stars) | Vendor CLI with no distinctive mechanism identified. Harbor has an adapter. |
| DeepSeek-Reasonix | https://github.com/esengine/DeepSeek-Reasonix (Go, MIT, 35.9k stars) | Generic unofficial agent. |
| Refact | https://github.com/smallcloudai/refact | **Archived** (last push 2026-05-30). |
| Void | https://github.com/voideditor/void | **Archived** (last push 2026-06-02). |
| Amazon Q Developer CLI | https://github.com/aws/amazon-q-developer-cli (Rust, Apache-2.0) | README: "no longer being actively maintained… now available as Kiro CLI, a closed-source product." It is the last open lineage; optional historical study. |
| AutoCodeRover | https://github.com/AutoCodeRoverSG/auto-code-rover | Dormant since 2025-04; its AST-search and fault-localization ideas are covered by papers. |
| RA.Aid | https://github.com/ai-christianson/RA.Aid | Dormant since 2026-01. |
| iFlow CLI | https://github.com/iflow-ai/iflow-cli | Shell-only distribution repo; a Gemini CLI derivative. |
| OpenHarness (HKUDS) | https://github.com/HKUDS/OpenHarness (16k stars) | Dormant since 2026-06-04. |
| augment-swebench-agent | https://github.com/augmentcode/augment-swebench-agent | A benchmark scaffold with an ensembling selector; low novelty. |
| vibe-kanban | https://github.com/BloopAI/vibe-kanban | **Sunsetting** (README banner). |
| claude-squad | https://github.com/smtg-ai/claude-squad | A tmux and worktree multiplexer; corpus `ntm`, `gastown`, `phux` cover this. |
| coder mux (renamed **coder/xum**) | https://github.com/coder/mux → `coder/xum` (TS, AGPL-3.0) | Desktop parallel-worktree app. |
| Emdash, Superset, Happy, Crystal (now Nimbalyst), Orca, agent-deck, agent-of-empires, Multica, Paseo, cmux | e.g. https://github.com/generalaction/emdash, https://github.com/superset-sh/superset, https://github.com/slopus/happy, https://github.com/stravu/crystal, https://github.com/stablyai/orca, https://github.com/asheshgoplani/agent-deck | Orchestrator, IDE and dashboard shells over other agents. Many lookalikes, little harness mechanism. |
| claude-code-router | https://github.com/musistudio/claude-code-router | Provider proxy. |
| google/ax, GoogleCloudPlatform/scion, vercel/eve, omnigent | https://github.com/google/ax, https://github.com/GoogleCloudPlatform/scion, https://github.com/vercel/eve, https://github.com/omnigent-ai/omnigent | Cluster orchestration or general agent frameworks; outside the "own agent" scope and against the shared-services boundary. |
| Small idea repos | https://github.com/wrg32786/aigent-os (refresh context at a threshold instead of compacting), https://github.com/jo-inc/pi-reflect (self-editing behaviour files from transcripts), https://github.com/Continuum-AI-Corp/OrcaReplay (record, replay, fork agent runs), https://github.com/tenequm/pond (lossless cross-client session store), https://github.com/jackispm/nausicaa-harness (dynamic "lanes" topology) | Tiny (≤300 stars). Note the ideas; do not acquire. |

---

## Closed-source notable agents: is there anything worth acquiring as documentation?

| Agent | Public source / SDK | Docs worth capturing? |
|---|---|---|
| **Amp** (Sourcegraph) | None found. Closed. | **Yes (B).** The ampcode.com/manual set covers threads, **Orbs** (per-thread preconfigured machines, agent-to-agent, event-driven), **Puck** (an agent that launches and coordinates agents), the plugin API (https://ampcode.com/docs/plugin-api), the external API, decision models and model routing. |
| **Cursor CLI / agent** | Closed binary. A `@cursor/sdk` TypeScript SDK was reported in public beta from 2026-04-29 (third-party source, unverified). ACP mode is documented. | **Yes (B):** https://cursor.com/docs/hooks (sessionStart, preToolUse, beforeShellExecution, stop and others; CLI parity was partial per forum threads) and https://cursor.com/docs/cli/acp. |
| **Factory Droid** | Closed. https://github.com/Factory-AI/factory exists (57 stars) but has no code. | **Yes (B):** the hooks reference (https://docs.factory.ai/reference/hooks-reference) and plugins (https://docs.factory.ai/cli/configuration/plugins; it accepts Claude Code plugin layouts). Droid exec docs also exist. |
| **Warp** | **Now open** (client AGPL, UI crates MIT; server closed). | Acquire the source (B above). |
| **GitHub Copilot CLI** | https://github.com/github/copilot-cli is a distribution and issues repo (Shell, 374 KB). The open **github/copilot-sdk** (MIT). ACP support in public preview since 2026-01-28. | The SDK source (B); the CLI's own docs are optional. |
| **Junie** (JetBrains) | https://github.com/JetBrains/junie is public, but its license is proprietary ("other"); installer and issues only. | Low (C). Docs at junie.jetbrains.com; Junie CLI in beta since about 2026-08. |
| **Augment / Auggie** | https://github.com/augmentcode/auggie is a distribution repo. Open pieces: `auggie-zed-extension` (ACP), `augment-swebench-agent`, `context-connectors` (Context Engine SDK), `context-engine-plugin` (MCP). | Low to medium (C/B). The Context Engine SDK shape is the only distinctive part. |
| **Jules** (Google) | Closed. Jules API preview and the Jules Tools CLI (`@google/jules`) since 2025-10-02; no source repo found. | Low (C). |
| **Devin** (Cognition) | Closed; an API exists (Harbor ships a `devin` adapter). | Low (C). |
| **Antigravity CLI (`agy`)** (Google) | Closed Go binary. **It replaced Gemini CLI for individual and consumer users on 2026-06-18**; enterprise keeps Gemini CLI. | Low (C). Its relevance is to the corpus `gemini-cli` snapshot; see below. |
| **Kiro CLI** (AWS) | Closed; successor to the Apache-2.0 Amazon Q Developer CLI. | Low (C); KiroCrew (B) is the open part. |

---

## Corpus entries whose upstream has moved since the snapshot (stale or superseded)

The "ahead" column counts commits from the pinned corpus revision to upstream HEAD on 2026-10-19 (GitHub compare API). Where several snapshots exist, the newest is used.

| Corpus entry | Pinned (date) | Upstream now | Ahead | What changed | Action |
|---|---|---|---|---|---|
| `kimi-cli` | 2026-09-30 | `MoonshotAI/kimi-cli` **archived** | – | Replaced by the TypeScript `MoonshotAI/kimi-code` | Acquire kimi-code (A4). |
| `opencode` | legacy-catalog rev `2006259a`, **2026-06-07** | Renamed **`anomalyco/opencode`** | **6,655** | OpenCode 2.0: new plugin system with hot reload, shared background service, new SDK and API | Refresh (Tier A). |
| `openhands` | 2026-09-30 | Org renamed `OpenHands/OpenHands`; the repo is now the TS Agent Canvas | 270 | **The agent core moved to `OpenHands/software-agent-sdk`**; the snapshot has no agent loop | Acquire the SDK (A3). |
| `roo-code` | `b867ec91` (2026-05-15, essentially the final commit) | `RooCodeInc/Roo-Code` **archived 2026-05-15** ("The Roo Code Extension was shut down") | 0 | Dead; community fork Zoo-Code | Mark as terminal; no refresh. |
| `gemini-cli` | 2026-09-30 | Repo active (56 commits ahead) | 56 | **Consumer service shut down 2026-06-18**, replaced by closed Antigravity CLI; open repo continues for enterprise | Note the changed status. |
| `aider` | 2026-09-30 discovered rev | `Aider-AI/aider`: no upstream push since **2026-05-22** | – | Upstream stalled; community continuation `cecli-dev/cecli` | Note; optional cecli. |
| `gpt-engineer` | – | **Archived** (2025-05) | – | "Precursor to lovable.dev" | Terminal. |
| `plandex` | – | Last push 2025-10-03; cloud winding down | – | Dormant | Terminal. |
| `sweep` | – | Pivoted to a JetBrains assistant; last push 2025-09-18 | – | Dormant | Terminal. |
| `devika` | – | Last push 2025-09-25 | – | Dormant | Terminal. |
| `trae-agent` | – | Last push 2026-02-05 | – | Dormant | Terminal. |
| `mini-swe-agent` | legacy rev, 2026-06-06 | Active | 37 | Minor drift; the only snapshot is June | Optional refresh. |
| `swe-agent` | – | Org moved `princeton-nlp` → `SWE-agent` | – | Cosmetic | None. |
| `beads` (legacy) | 2026-06-05 | Renamed `gastownhall/beads` | 2,143 | Superseded by the corpus `beads-2026-10-07` | None. |

**High-churn entries.** These are not superseded, but their snapshots age within days:

| Entry | Commits since snapshot |
|---|---|
| `openclaw` | +11,612 since 2026-10-01 |
| `hermes` | +4,231 since 2026-10-14 |
| `oh-my-pi` | +2,069 since 2026-10-14 |
| `forge-workflow` | +1,133 |
| `qwen-code` | +720 |
| `codex` | +647 |
| `vtcode` | +592 |
| `open-swe` | +490 |
| `pi_agent_rust` | +428 |

Re-pin these only when a study depends on current behaviour.

`open-interpreter` (the Rust rewrite, 6 commits behind), `claude-code-official-public`, `crush`, `goose`, `cline`, `letta`, `mistral-vibe` and `pi` (2026-10-14) are current.

---

## Short list to act on

1. **Refresh or replace:** kimi-code, OpenCode 2.0, OpenHands software-agent-sdk.
2. **New Tier A:** harness2, DeepSeek Harness, jcode, maki, smelt, agentty (with its C++ submodules), the Zed ACP crates, CodeCompanion.nvim, ACE, Grok Build. Harbor upstream is optional, because Terminus 2 is already in the corpus via `evo-harness-modularrsi`.

Note: the lone "harness2" hit in `papers/2026-09-30-reference-acquisition.json` is a false positive. It matches `self-harness-2720…`, the corpus Self-Harness snapshot, which is unrelated to Google's harness2.
3. **Documentation captures:** Amp manual and plugin API, Factory hooks and plugins, Cursor hooks and ACP, Copilot SDK.
