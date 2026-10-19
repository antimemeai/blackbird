# Corpus gaps, 2026-10-19

The source sweep is [reports/missing-agents-sweep.md](reports/missing-agents-sweep.md).
It covered roughly 500 candidates: the ACP agent list, three awesome-lists, Harbor's
agent adapters, and July–October 2026 releases. Read-only `gh api` and web fetches;
nothing was cloned or run. The tier-A repository identities below were rechecked
against the GitHub API on 2026-10-19. Nothing listed here has been acquired yet.
Acquire only what a live question needs, using the usual owned ZIP ingestion and a
QUARANTINE.md record.

## Holes in what we already hold

- **OpenHands:** the `openhands` snapshot is only the TypeScript frontend. The agent
  loop, condenser and observation truncation live in
  [OpenHands/software-agent-sdk](https://github.com/OpenHands/software-agent-sdk),
  so the corpus has no OpenHands agent at all. Question 1 hit this directly.
- **Kimi:** `kimi-cli` is archived upstream. Its successor is
  [MoonshotAI/kimi-code](https://github.com/MoonshotAI/kimi-code) (TypeScript).
- **OpenCode:** the snapshot dates from 2026-06-07 and is about 6.6k commits behind.
  The repository is now `anomalyco/opencode`, and its 2.0 hot-reloading plugins and
  shared background service postdate our copy.
- **Ended or dormant upstreams:** roo-code is archived and shut down (2026-05-15), so
  our copy is effectively its final state. Gemini-cli lost consumer use on 2026-06-18.
  Aider has had no commits since 2026-05-22; its community continuation is `cecli`.
  Gpt-engineer, plandex, sweep, devika and trae-agent are archived or dormant.
- **High churn:** openclaw, hermes, oh-my-pi and forge-workflow age within days.
  Re-pin only when a study depends on current behavior.

## Missing, worth acquiring

| Reference | Why it matters to Blackbird |
| --- | --- |
| [google-research/harness2](https://github.com/google-research/harness2) (Python) | Recursive harness optimization with fixed weights. Propose–Probe–Compose edits prompts, tools, memory, skills and plugins, and an improvement harness carries results across tasks. arXiv 2610.14224. Autodroit comparator, alongside the evo-harness set. |
| [deepseek-ai/deepseek-harness](https://github.com/deepseek-ai/deepseek-harness) (TS) | No privileged core: everything is a plugin whose registrations undo on unload. Context comes from an append-only session log, and every request must be rebuildable from it. Config hot reload, fork at a turn boundary. The closest published match to our originals/CLM/hot-reload aims. |
| [1jehuang/jcode](https://github.com/1jehuang/jcode) (Rust) | Self-dev mode: the agent edits and rebuilds its own source. A shared-repo server warns an agent when a colleague edits a file it has read. |
| [tontinton/maki](https://github.com/tontinton/maki) (Rust + Lua) | Neovim-style Lua plugins, including Lua-registered providers. Tree-sitter-parsed shell permissions. |
| [leonardcser/smelt](https://github.com/leonardcser/smelt) (Rust + Lua) | Tools, commands and modes all in Lua. Its own renderer. Deterministic fuzzing with a fixed clock and stubbed I/O. |
| [agentty-org/agentty](https://github.com/agentty-org/agentty) (C++26) | The only substantial C++ coding agent found. Owned ACP/MCP/TUI/sandbox libraries; some submodules are unlicensed, so study only. |
| [zed-industries/zed](https://github.com/zed-industries/zed) agent crates (Rust) | ACP's origin and the reference ACP client; the corpus has agents but no production client. |
| [olimorris/codecompanion.nvim](https://github.com/olimorris/codecompanion.nvim) (Lua) | A pure-Lua harness and ACP client inside Neovim. |
| [ace-agent/ace](https://github.com/ace-agent/ace) (Python) | Model-curated itemized context, updated by incremental deltas rather than rewrites. Direct CLM comparator. |
| [xai-org/grok-build](https://github.com/xai-org/grok-build) (Rust) | Fullscreen mouse-driven TUI decoupled from the agent runtime; workspace checkpoints. |

Harbor's Terminus 2 (keystrokes into tmux only, with summarization covered by golden
tests) is **already in the corpus**: `quarantine/evo-harness-modularrsi` is a Harbor
fork containing `src/harbor/agents/terminus_2/`. Upstream Harbor would only add its
current index of about 50 agent adapters.

Tier B (in the sweep) includes several that bear on current questions: **hax** (C;
partial redraw with native scrollback and an exact transcript view), **Ante** (shared
`/term` view of other agents' tmux sessions), **hcom** (cross-vendor agent
messaging), **Meta-Harness** (cited in the autodroit study, never acquired),
**Claw Code** (clean-room Claude Code reimplementation) and **Every Code** (Codex
fork with multi-vendor racing).

Closed products worth a documentation capture: the Amp manual and plugin API,
Factory hooks and plugins, Cursor hooks and ACP, and the GitHub Copilot SDK. Warp's
client became open source (AGPL) in April 2026.
