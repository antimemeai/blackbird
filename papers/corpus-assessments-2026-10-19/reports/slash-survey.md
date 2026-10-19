# Built-in slash commands across coding agents: survey

Date: 2026-10-19. Sources are snapshots under `/Users/patrickbeam/projects/blackbird/quarantine/` (around 2026-09-30, plus `hermes-agent-2026-10-07`). This was a read-only grep/read survey. Nothing was run, built or installed.

Layout:
- Sections 1-4: synthesis (coverage, per-agent mechanics, union table, common and distinctive commands).
- Appendices A-E: per-agent detail. Each appendix gives the registry `file:line`, every command with its purpose and aliases, hidden or flagged commands, and notes. Appendix paths are relative to the quarantine root unless stated.
- "?" marks uncertainty.

## 1. Coverage and caveats

- **23 agents surveyed.** None was skipped. Four are not ordinary `/` command systems:
  - **crush** has no typed slash commands. `/` on an empty editor (or ctrl+p) opens a command palette.
  - **plandex** uses a backslash prefix (`\cmd`). Its REPL commands mirror the CLI subcommands.
  - **roo-code** ships one built-in command (`/init`). Its `/` menu otherwise lists modes and custom commands.
  - **open-interpreter-python** (the classic Python interpreter) uses `%` magics. They are listed in appendix D.
- **Claude Code** is covered from two sources:
  - The public repo (`claude-code-official-public`) has no command table. It contributes only CHANGELOG mentions and the plugin command format.
  - The real list comes from strings in the 2.1.286 darwin-arm64 binary. Grep anchor: `function uAo(){return[`.
- **Fork families.** These are counted separately, but the counts in section 3 also show lineages:
  - codex → open-interpreter. The only command difference is a new `/harness`.
  - opencode → mimo-code
  - pi → oh-my-pi
  - gemini-cli → qwen-code
- **Multi-surface agents.** Their surfaces are merged in the tables:
  - cline: VS Code extension + CLI
  - roo-code: webview + CLI
  - continue: IDE + CLI
  - goose: CLI + agent level + desktop
  - hermes: CLI + messaging gateway + desktop

Abbreviations: CC claude-code, CX codex, OI open-interpreter (codex fork), OC opencode, MM mimo-code, PI pi, OMP oh-my-pi, GM gemini-cli, QW qwen-code, KM kimi-cli, CR crush, GS goose, CN continue, AD aider, HM hermes-agent, MV mistral-vibe, GP gptme, PX plandex, LC letta-code, FC forgecode, VT vtcode, CL cline, RC roo-code.

## 2. Per-agent mechanics at a glance

Column key:
- **Built-ins:** approximate count of visible built-in commands.
- **Custom commands:** user-defined commands from files or config.
- **Args:** how arguments are substituted into a custom command.
- **Skills / MCP prompts:** whether skills or MCP prompts become slash commands.
- **Matching:** how the command menu matches typed text.
- **Hint / help:** argument hints, and whether `/help` exists.
- **While model busy:** what happens when a command is entered mid-turn.

| Agent | Registry | Built-ins | Custom commands | Args | Skills as cmds | MCP prompts as cmds | Matching | Hint / help | While model busy |
|---|---|---|---|---|---|---|---|---|---|
| CC | binary `function uAo(){return[` | ~95 + ~30 bundled skills | `.claude/commands`, `.claude/skills`, plugins (md + frontmatter) | `$ARGUMENTS`, `$N`, named `$arg`, `` !`cmd` ``, `@file`; appends `ARGUMENTS:` if unused | yes `/name` | yes, `mcp__srv__prompt`, shown `server:prompt (MCP)` | Fuse fuzzy over name, aliases, `:` parts, description words | argumentHint / yes | Per-command `immediate` flag runs mid-turn. Others queue to turn end. ctrl+enter sends queued input now |
| CX | `codex-rs/tui/src/slash_command.rs:12` | ~55 + gated | none in snapshot (`~/.codex/prompts` appears removed ?) | n/a | no; skills are `$name` mentions | no | exact, then prefix (fuzzy highlight) | none / **no /help** | `available_during_task()==false` is **refused**. Tab queues slash input and validates it at dequeue |
| OI | same as CX | CX + `/harness` | as CX | | | | | | as CX; `/harness` allowed mid-task |
| OC | TUI keymap `slashName` (`packages/tui/src/app.tsx:525` etc.) + `packages/opencode/src/command/index.ts:53` | ~28 | `.opencode/command/*.md`, config | `$1..$N`, `$ARGUMENTS`, `` !`cmd` ``, `@file` | yes (hidden from autocomplete; via /skills) | yes `/name` (`:mcp` tag) | fuzzysort | server computes hints (TUI display ?) / yes, plus ctrl+p palette | TUI commands immediate (/undo aborts first). Prompt commands join the running loop (inferred queue/inject) |
| MM | `packages/cli/src/command/index.ts:63` | OC + ~20 | `.mimocode/command/` | as OC | as OC | as OC | as OC | as OC | as OC (not traced) |
| PI | `packages/coding-agent/src/core/slash-commands.ts:19` | 24 (+3 hidden) | prompt templates `~/.pi/agent/prompts`, `.pi/prompts`; extensions `registerCommand` | `$1`, `$@`, `${@:N:L}`, `${ARGUMENTS:-default}` | `/skill:name` | none (no MCP in core) | fuzzy | argumentHint / no (`/hotkeys`) | Built-ins run immediately mid-stream. Templates and skills queued as a **steer** |
| OMP | `packages/coding-agent/src/slash-commands/builtin-registry.ts:40` | ~95 | `.omp/commands`, `.claude/commands`, codex/opencode dirs, TS commands, plugins | pi's | `/skill:name` | `/server:prompt` | fuzzy + subcommand inline hints | yes / no | Built-ins immediate. Skills steer. Explicit `/queue` |
| GM | `packages/cli/src/services/BuiltinCommandLoader.ts:123` | ~35 + ~10 gated | `.gemini/commands/**/*.toml` (user, workspace, extension); `:` namespaces | `{{args}}`, `!{shell}`, `@{file}` | yes `/skill` | yes, named/positional args, `help` sub | fzf | completion fns, no hint field / yes | `isSafeConcurrent` commands run. Others **refused** ("Slash commands cannot be queued") |
| QW | `BuiltinCommandLoader.ts:124` | ~60 | `.qwen/commands` **toml + md** (frontmatter `argument-hint`) | as GM | yes, stackable (max 5) | yes | fzf | argumentHint / yes (`?`) | `canRunDuringStreaming` commands run. Others **queued** (not refused). Custom commands are model-invocable |
| KM | `src/kimi_cli/soul/slash.py:34` + `ui/shell/slash.py:35` | ~35 | none | skill args appended as "User request:" | `/skill:name`, `/flow:name` | no | fuzzy, alias-aware | none / yes (h, ?) | `/btw` immediate. Soul commands queue FIFO. Shell commands **refused**. Ctrl+S steers |
| CR | palette `internal/ui/dialog/commands.go:449` | ~20 palette items | `user:` / `project:` md commands | named `$UPPER` vars collected by a dialog | `user:<skill>` | yes (MCP tab, arg form) | fuzzy palette | arg dialog / ctrl+g | Some **refused** (new, summarize, init, effort). Custom and MCP commands queued into the active turn |
| GS | `crates/goose-cli/src/session/input.rs:220` + `crates/goose/src/agents/execute_commands.rs:27` | ~16 CLI + 9 agent | recipes mapped to `/name` (config `slash_commands`) | recipe params as positionals and flags | yes `/skill` | via `/prompt name k=v` only | prefix tab; desktop popover | recipe/skill hints / yes | CLI takes no input mid-turn. Desktop queues |
| CN | IDE `core/config/profile/doLoadConfig.ts:157`; CLI `extensions/cli/src/commands/commands.ts:25` | IDE 1 (+6 legacy); CLI ~20 | `.continue/prompts` `.prompt` files, YAML `prompts:`, invokable rules | `{{{ input }}}` (v1) | CLI | IDE only (empty args, TODO) | IDE prefix; CLI substring | none / CLI | IDE: Enter blocked while streaming. CLI: queued |
| AD | `aider/commands.py:276` (`cmd_*` reflection) | 42 | none (`/load` replays a file of commands) | n/a | no | no | unique-prefix *execution* | completion fns / yes (+ `/help <question>`) | Blocking loop; Ctrl-C interrupts |
| HM | `hermes_cli/commands.py:83` `COMMAND_REGISTRY` | 102 entries | `quick_commands` (exec/alias), plugins, skill bundles | alias appends args | `/skill-name`, stackable | no (MCP prompts are model tools) | prefix completer + ghost text; `/palette` fuzzy; unique-prefix expansion | args_hint / yes (categorized) | CLI: slash commands wait for turn end, except /model, /steer, /queue, /bg, /btw. Gateway: per-command `busy_policy` (dispatch / reject / interrupt-then-dispatch) |
| MV | `vibe/cli/commands.py:41` | 34 | none (skills only) | appended instructions | `/skill-name` | no | fuzzy | none / yes | `side_channel` commands immediate. Others **refused**. Skills queued |
| GP | `gptme/commands/base.py:42` | ~27 + 5 tool-owned | plugins; unknown `/tool args` runs the tool directly | n/a | `/skill:name` + bare alias | via MCP tool subcommands (?) | prefix tab | completers / yes | CLI blocking. TUI **refuses** commands except /quit, /display |
| PX | `app/cli/lib/repl.go:39` + `term/help.go:19` | 7 native + ~50 mirrored (backslash) | none | n/a | no | no | fuzzy + prefix, did-you-mean | desc only / `\help` | REPL blocks. Streaming TUI keys: s stop, b background |
| LC | `src/cli/commands/registry.ts:50` | ~55 | `.commands/`, `~/.letta/commands` md | `$ARGUMENTS`, `$1..9`, `` !`cmd` `` | `/skill-id` | no | substring | args hint / hidden | "Interactive" and "non-state" commands immediate. Others **refused**, including custom. Mods opt in with `runWhenBusy` |
| FC | `crates/forge_main/src/model.rs:411` (clap enum; `/` or `:`) | ~42 | `.forge/commands` md | `{{parameters}}` | agents as `/agent-<id>` | no | prefix + nucleo fuzzy picker | usage text / yes | Sequential; no input mid-turn |
| VT | `crates/codegen/vtcode-skills/src/command_skills.rs:144` | ~40 | `.vtcode/prompts/templates` (expands into the input buffer) | `$ARGUMENTS`, `$1`, `$@` | every built-in is also a `cmd-*` skill; `/skills use` | no | prefix, then substring, then fuzzy over name+desc | usage / `/help [cmd]` | /stop, /pause, /resume intercepted. An immediate-submit list is sent mid-turn (? whether acted on). Others queued one per turn. Enter steers plain text |
| CL | ext `src/shared/slashCommands.ts:8`; CLI `apps/cli/src/tui/commands/slash-command-registry.ts:48` | ext 4; CLI 15 | workflows md (`.clinerules/workflows`, toggleable), plugins | none; token anywhere, wrapped in a block | yes | listed as `/mcp:srv:prompt` (expansion ?) | ext prefix; CLI substring over name+desc | none / CLI | ext: /compact immediate, other text sent as steering feedback. CLI: Enter queues as text; autocomplete picks run immediately |
| RC | `src/services/command/built-in-commands.ts:10` | 1 (`/init`) + modes; CLI `/new` | `.roo/commands` md (description, argument-hint, `mode`) | none; token anywhere | merged | no | Fzf | display-only argument-hint / no | Webview queues. Model can call `run_slash_command` (experiment) |

## 3. Union table of command concepts

Each count is "agents (lineages)". Lineages collapse the four fork pairs CX/OI, OC/MM, PI/OMP and GM/QW, so the maximum is 23 (19). An asterisk means the concept lives inside another command, for example effort inside the `/model` picker.

### Session
| Concept | Typical names | Agents | Count |
|---|---|---|---|
| New session | /new, /clear (alias new/reset), New Session | CC CX OI OC MM PI OMP GM QW KM CR GS HM MV LC FC VT CL RC PX | 20 (16) |
| Resume / session picker | /resume (continue), /sessions, /chat resume, \plans | CC CX OI OC MM PI OMP GM QW KM CR CN HM MV LC FC VT CL PX | 19 (15) |
| Fork / branch / clone / tree | /fork, /branch, /clone, /tree, \checkout | CC CX OI OC MM PI OMP QW KM CN HM MV GP LC FC VT CL PX | 18 (15) |
| Rename / title | /rename, /name, /title | CC CX OI OC MM PI OMP QW KM CN HM MV GP LC FC PX | 16 (13) |
| Export / share transcript | /export, /share, /dump, /save | CC CX OI OC MM PI OMP GM QW KM CN HM GP FC VT | 15 (11) |
| Side question (/btw) | /btw, /side | CC CX OI MM OMP QW KM HM LC | 9 (8) |
| Continue elsewhere (cloud, desktop, web, phone, messaging) | /teleport, /desktop, /app, /handoff, /web, /ade | CC CX OI MV HM KM LC | 7 (6) |
| Delete / archive session | /delete, /archive, \delete-plan | CX OI QW OMP GP PX | 6 (5) |
| Send to background / background agent | /background, /tan, /fork (directive), /bg | CC OMP QW HM | 4 |
| Import session | /import | PI KM CN | 3 |
| Import config from another agent | /import, /import-config | CC CX OI QW | 4 (3) |

### Context
| Concept | Typical names | Agents | Count |
|---|---|---|---|
| Compact / summarize | /compact, /compress, /summarize, /smol | CC CX OI OC MM PI OMP GM QW KM CR GS CN HM MV GP LC FC VT CL | 20 (16) |
| Workspace dirs / files in context | /add-dir, /directory, /add /drop /ls, \load, /mention, /files | CC OMP KM GM AD PX CX OI VT | 9 (8) |
| Clear history, keeping the session | /clear, /reset | AD OMP KM GS CN LC PX | 7 |
| Context usage view | /context, /tokens | CC OMP QW HM GP AD LC | 7 |
| Retry / recover interrupted turn | /retry, /recover | OMP HM MV FC MM | 5 |
| Context limit / auto-compact threshold | /autocompact, /context-limit, /compaction | CC MM LC | 3 |
| Cheap context surgery without a model | /shake, /compress-fast, /rebuild | OMP QW MM | 3 |

### Model
| Concept | Typical names | Agents | Count |
|---|---|---|---|
| Model select | /model(s), Switch Model, \set-model | CC CX OI OC MM PI OMP GM QW KM CR GS CN AD HM MV GP LC FC VT CL PX | 22 (18) |
| Reasoning effort / thinking | /effort, /thinking, /reasoning(-effort), /variants, /think-tokens | CC CX* OI* OC MM PI QW CR AD HM MV FC LC KM* VT* | 15 (13) |
| Fast / priority service tier | /fast, /slow | CC CX OI OMP HM | 5 (4) |
| Role models / presets (weak, editor, commit model) | /weak-model, /editor-model, /config-commit-model, /modelpreset | AD FC PX OMP | 4 |
| Second model: advisor, arena, mixture | /advisor, /arena, /moa | CC OMP QW HM | 4 |

### Config
| Concept | Typical names | Agents | Count |
|---|---|---|---|
| Theme | /theme, /themes, /skin, /t | CC CX OI OC MM GM QW KM CR GS HM MV VT CL | 14 (11) |
| Settings / config | /config (settings), /settings, \set-config | CC PI OMP GM QW CN HM MV FC VT CL AD PX | 13 (11) |
| Permissions / approval mode / yolo / trust | /permissions, /approval-mode, /yolo, /mode, /trust, \set-auto | CC CX OI GM QW KM CR HM MM PI GS PX | 12 (10) |
| Reload config / plugins / skills | /reload, /reload-plugins, /reload-skills | CC PI OMP GM QW KM HM MV LC FC | 10 (8) |
| Statusline / footer / title | /statusline, /footer, /title | CC CX OI GM QW LC HM VT | 8 (6) |
| Change working dir / move session | /cd, /move, /chdir, /pwd | CC CX OI QW LC OMP OC | 7 (6) |
| Keybindings / hotkeys | /keybindings, /keymap, /hotkeys, /shortcuts | CC CX OI PI OMP GM | 6 (4) |
| Vim mode | /vim | CX OI GM VT (CC hidden stub) | 4 (3) |
| Terminal setup (Shift+Enter etc.) | /terminal-setup, /terminal | CC GM LC VT | 4 |
| Output style / personality / system prompt | /output-style, /personality, /system | CC QW HM LC | 4 |
| UI language | /language | MM QW | 2 |

### Tools and extensibility
| Concept | Typical names | Agents | Count |
|---|---|---|---|
| MCP management | /mcp, /mcps, /extension, Toggle MCPs | CC CX OI OC MM GM QW KM CR GS CN OMP HM MV LC VT CL GP | 18 (15) |
| Skills browse / manage | /skills, /skill | CC CX OI OC MM OMP GM QW GS CN HM MV LC FC VT CL GP | 17 (14) |
| Plan / mode / agent-persona switch | /plan, /mode, /ask /code /architect, /act /sage, \chat \tell | CC CX OI OMP GM QW KM HM FC VT CL RC PX AD GS | 15 (12) |
| Agents / subagents | /agents, /subagents, /agent, /team | CC CX OI OC MM GM QW OMP HM LC FC VT CL | 13 (9) |
| Plugins / extensions / marketplace | /plugin(s), /extensions, /marketplace | CC CX OI OMP GM QW MV HM VT CL GP | 11 (9) |
| Background tasks / jobs / terminals | /tasks, /jobs, /ps, /bg, \ps | CC CX OI GM QW KM CN OMP LC HM PX | 11 (9) |
| Goal / stop condition | /goal, /grind, /subgoal | CC CX OI MM OMP QW GS HM | 8 (7) |
| Hooks | /hooks | CC CX OI GM QW KM LC | 7 (5) |
| Tools list / toggle | /tools, /toolsets | GM QW OMP HM GP FC | 6 (5) |
| Loop / recurring prompt | /loop, /heartbeat, /loops | CC OMP HM MV MM | 5 |

### Project
| Concept | Typical names | Agents | Count |
|---|---|---|---|
| Init (write AGENTS.md, CLAUDE.md, GEMINI.md) | /init, Initialize Project | CC CX OI OC MM GM QW KM CR CN HM VT RC LC* (LC: memory init) | 14 (10) |
| Memory | /memory, /memories, /remember, /dream, /reflect | CC CX OI GM QW OMP HM LC MM | 9 (7) |
| Learn / distill skills from material | /learn, /distill, /curator, /skill-creator | QW HM MM LC | 4 |

### Git and review
| Concept | Typical names | Agents | Count |
|---|---|---|---|
| Undo / rewind / checkpoint restore | /rewind (undo, checkpoint), /undo, /redo, /restore, /rollback, /backtrack | CC OC MM GM QW KM HM MV AD GP VT CL OMP PX | 14 (11) |
| Diff | /diff | CC CX OI OC QW HM AD PX CN | 9 (8) |
| Review | /review, /code-review, /security-review | CC CX OI OC OMP HM VT CN | 8 (7) |
| Commit | /commit, /commit-preview | CC QW AD FC GP CN | 6 |
| Worktree | /worktree, /wt | CX MM OMP HM | 4 |
| GitHub setup / PR | /install-github-app, /setup-github, /autofix-pr | CC GM LC | 3 |

Note: no codex-lineage agent (CX, OI) has undo or rewind.

### Account and status
| Concept | Typical names | Agents | Count |
|---|---|---|---|
| Login / logout / provider auth | /login, /logout, /auth, /connect, /provider, /account | CC CX OI OC MM PI OMP GM QW KM GS HM LC FC VT CL PX GP | 18 (14) |
| Status / session info | /status, /about, /info, /session | CC CX OI OC MM QW GS HM MV VT CN FC PI GM KM | 15 (12) |
| Cost / usage / stats | /usage (cost, stats), /stats, /tokens, \usage | CC CX OI OMP GM QW KM HM LC FC GP PX AD | 13 (11) |
| Feedback / bug report | /bug, /feedback, /report | CC CX OI PI GM KM AD HM LC VT | 10 (9) |
| Self-update | /update, /upgrade, /install | CC QW CN HM FC VT KM | 7 |
| Doctor / diagnostics | /doctor, /debug | CC QW GS GP LC VT | 6 |
| Version / changelog | /release-notes, /changelog, /version | CC PI OMP KM HM GM | 6 (5) |

### UX
| Concept | Typical names | Agents | Count |
|---|---|---|---|
| Exit | /exit, /quit, /q, :q | CC CX OI OC MM PI OMP GM QW KM CR GS CN AD HM MV GP PX LC FC VT CL | 22 (18) |
| Help | /help, /?, \help | CC OC MM GM QW KM CR GS CN AD HM MV GP PX FC VT CL (LC hidden) | 17 (14) |
| Copy last response | /copy | CC CX OI OC MM PI OMP GM AD HM MV FC VT | 13 (10) |
| Voice | /voice, /live | CC CX OI MM OMP GM QW AD HM MV | 10 (8) |
| External editor | /editor, /edit, /prompt | OC MM GM KM CR GS AD HM FC | 9 (7) |
| Display toggles (timestamps, thinking, verbose, focus, raw) | /focus, /raw, /timestamps, /verbose, /display | CC CX OI OC MM HM GS GP LC | 9 (7) |
| Fun / easter eggs | /radio, /stickers, /pets, /pet, /corgi | CC CX OI HM PI GM MM | 7 |
| Paste image / attach | /paste, /image, /paste-image | AD HM MV CR | 4 |
| Queue / steer as commands | /queue, /steer, /busy | OMP HM | 2 |

## 4. Most common and distinctive commands

### Shipped by about 5 or more lineages, most common first
1. **Exit** (22) and **model select** (22): universal. Roo is the main exception (it switches modes instead).
2. **New session** (20) and **compact** (20). Many agents make `/clear` an alias of new.
3. **Resume / session picker** (19), **fork/branch** (18), **MCP** (18), **login/logout** (18).
4. **Help** (17; notably absent in codex, pi, omp, roo), **skills** (17), **rename** (16), **status** (15), **plan/mode switch** (15), **effort/thinking** (15), **export** (15).
5. **Init** (14), **undo/rewind** (14), **theme** (14), **settings** (13), **copy** (13), **agents** (13), **cost/usage** (13), **permissions** (12).
6. Mid-frequency: plugins (11), background tasks (11), feedback (10), reload (10), voice (10), diff (9), memory (9), `/btw` side question (9), external editor (9), review (8), goal (8), statusline (8).

### Distinctive commands worth noticing
**Session and context**
- `/btw` side question that leaves the main thread untouched: 9 agents. Codex runs it as an ephemeral fork (`/side`).
- `/goal` with a judge-checked stop condition: CC, MM, QW, OMP, HM; goose's `/grind` runs until max turns.
- `/handoff`: OMP writes a handoff document, then compacts; HM moves the session to Telegram or Discord.
- `/rebuild` from a checkpoint with no model turn (MM). `/shake` drops heavy content (OMP). `/compress-fast` strips old tool outputs without a model (QW).
- `/tree`, the session-tree navigator (PI, OMP). `/branch` that keeps the old path (OMP).

**Model**
- Role-specific models: aider weak/editor/architect; forgecode commit and suggest models; plandex model packs.
- Multi-model commands: `/arena` (QW), `/moa` (HM), `/advisor` (CC, OMP, QW).

**Concurrency and control**
- Mid-turn control as commands: `/queue` and `/steer` (HM, OMP), `/busy` (HM sets what plain text does while busy), `/pause` at a safe boundary and `/stop` (VT), `/pause` freezes all agents (OMP).
- `/approve` grants a single retry of an auto-review denial (CX). Gateway `/approve` and `/deny` handle dangerous commands from chat (HM).

**Project memory and self-improvement**
- `/dream`, `/distill` (MM); `/learn`, `/curator` (QW, HM); `/reflect`, `/sleeptime`, `/memfs`, `/palace` (LC); `/refine` (HM); `/omfg <complaint>`, which forges a rule (OMP).

**Commands as a programming surface**
- vtcode registers every built-in as a skill (`cmd-*`).
- gptme's `/shell ls` runs an unknown command as a direct tool call.
- roo-code (experiment) and qwen-code let the model invoke commands as tools; Claude Code's `disable-model-invocation` frontmatter implies the same capability.
- hermes `quick_commands` define shell-exec and alias commands in config; aider `/load` and `/save` replay command scripts.
- goose maps recipes to `/name`, with parameters as flags.

**Code workflow**
- aider: `/run` and `/test` add output to the chat (test only on failure); `/lint`, `/web` scrape, `/map`, `/ok`.
- plandex: `\apply`, `\reject`, `\build`; `\debug` reruns a command and auto-applies fixes until it passes.
- forgecode: `/suggest` turns natural language into a shell command.
- OMP: `/cleanse` fixes diagnostics with parallel subagents; `/green` iterates on CI.

**Collaboration**
- OMP: `/collab`, `/join`, `/leave` for live relay sharing.
- QW: `/peers` controls messages from other sessions.
- HM: `/kanban`, `/cron`, `/blueprint`.
- CC: `/list-agents` (peers), `/remote-control`, `/teleport`.

**Busy-handling contrasts**, the main design axis:
- **Refuse:** gemini, codex, mistral, letta, gptme TUI and crush refuse most commands mid-turn and whitelist a few.
- **Queue:** qwen and continue CLI queue them.
- **Per-command flag:** Claude Code (`immediate`) and hermes gateway (`busy_policy`) declare the behavior on each command.
- **Run built-ins now, steer the rest:** pi and omp run every built-in immediately and steer prompt-like commands into the running turn.
- **No input mid-turn:** aider, forgecode, plandex, goose CLI and gptme CLI simply block.

**Discovery outliers**
- aider executes any unique prefix (`/tok` runs `/tokens`).
- hermes expands unique prefixes, shows ghost-text suggestions, and offers `/palette` too.
- plandex offers "did you mean" before sending a bare word.
- Claude Code fuzzy-matches description words.
- codex has no `/help` and no argument hints.

---

# Appendices: per-agent detail

## Appendix A

### Part A: built-in slash commands (Claude Code public repo, Claude Code 2.1.286 binary, Codex TUI)

Read-only survey. Nothing in the reference trees was run, built or installed. For the
Claude Code binary, printable strings were pulled from the Mach-O with `strings` into the
scratchpad, then searched with regular expressions.
Paths are relative to `/Users/patrickbeam/projects/blackbird/quarantine/`.

---

## 1. claude-code-official-public (docs/CHANGELOG/plugins)

**Registry location:** none. The repo ships no built-in command table and no docs page
that lists commands. The built-in set can only be read off `CHANGELOG.md` (7973 lines,
top entry 2.1.286), which matches the binary in section 2. See section 2 for the real list.

**Commands named in CHANGELOG** (mention counts in parentheses; the list is noisy and
includes paths):
/model(95) /plugin(92) /config(68) /mcp(54) /resume(49) /login(47) /ultrareview(42)
/effort(38) /clear(36) /doctor(35) /usage(32) /status(31) /context(30) /skills(26)
/feedback(25) /code-review(24) /rename(23) /permissions(22) /compact(22)
/remote-control(19) /add-dir(19) /copy(17) /workflows(16) /loop(16)
/install-github-app(16) /btw(15) /rewind(14) /fork(14) /tasks(13) /goal(13) /cost(13)
/tui /terminal-setup /hooks /help /fast /claude-api /usage-credits /bg /reload-plugins
/diff /color /bug /artifacts /web-setup /theme /stats /memory /voice /upgrade /branch
/agents /teleport /sandbox /plan /insights /extra-usage /cd /background /autofix-pr
/advisor /review /export /chrome /share /schedule /release-notes /recap /exit /desktop
/update /simplify /remote-env /mobile /logout /ide /setup-vertex /setup-bedrock
/settings /output-style /keybindings /focus /commit-push-pr /checkup /autocompact /vim
/verify /ultraplan /todos /tag /security-review /scroll-speed /reload-skills /powerup
/heapdump /debug /batch /update-config /undo /team-onboarding /subtask /statusline
/skill-doctor /rate-limit-options /radio /proactive /pr-comments /list-agents
/init /dataviz /continue /commit.

**Renames and removals in the CHANGELOG** (useful lineage):
- L5250 removed `/vim`; Editor mode moved into `/config`. L5249 removed `/tag`.
- L3331 removed the `/agents` wizard. L5677 renamed `/fork` to `/branch`. `/fork`
  later came back as a separate command; see section 2.
- L4070 renamed `/simplify` to `/code-review`. L2659 made `/review` an alias of `/code-review`.
- L3177 made `/doctor` a full checkup with alias `/checkup`. L4973 made `/undo` an alias
  of `/rewind`. L5004 made `/proactive` an alias of `/loop`.
- L4132 renamed `/extra-usage` to `/usage-credits`. L6842 added `/settings` as an alias
  of `/config`.

**(a) Custom commands.** Plugins under `plugins/*/commands/*.md` are plugin-shipped,
not built-ins. Examples: `commit-commands/commands/commit.md`,
`ralph-wiggum/commands/ralph-loop.md`, `feature-dev/commands/feature-dev.md`.
The mechanism, per `plugins/plugin-dev/skills/command-development/SKILL.md` and
`references/frontmatter-reference.md`:
- **Locations:** project `.claude/commands/`, user `~/.claude/commands/`, plugin
  `commands/`. Subdirectories give namespacing. Plugin commands appear as
  `/plugin:cmd` (for example `/plugin-dev:create-plugin`).
- **Frontmatter:** `description`, `allowed-tools`, `model`, `argument-hint`,
  `disable-model-invocation`, plus `hide-from-slash-command-tool` (seen in ralph-loop).
- **Body substitution:** `$ARGUMENTS`; positional `$1`, `$2`, ...; `@file` inlining;
  inline shell `` !`cmd` ``; `${CLAUDE_PLUGIN_ROOT}`.
- Skills (`skills/<name>/SKILL.md`) are also slash-invocable. Full frontmatter is in
  section 2.

**(b) Discovery (CHANGELOG):**
- L51: suggestions match descriptions by word prefix.
- L1600: a slash command typed mid-prompt shows a match list; Tab opens it.
- L1906 (VSCode): a filterable "Slash commands" dialog.

**(c) Behaviour while busy (CHANGELOG):**
- Lines 442, 2205, 2429-2430 and 2501 describe "immediate" commands that run while Claude
  is working: `/model`, `/rename`, `/artifacts`, `/fast`, `/effort`, `/permissions`,
  `/add-dir`, `/feedback`, `/bug`. In the fullscreen TUI, some dialogs also open mid-turn.
- Other commands queue "until the turn ends" (L2205, L991).
- L921: a send-now key (ctrl+enter) interrupts the turn and sends all queued messages at once.

**`mods/`:** the source of four built-in "mods", which are hooks-module plugins. One of
them, `mods/diff`, provides `/diff` through the `command.register` and `command.run`
events (`mods/README.md`).

---

## 2. claude-code-official-distribution (2.1.286 Bun-compiled binary)

The npm tgz holds only a wrapper and an installer. The JS is embedded in
`distributions/2.1.286/darwin-arm64/claude`. The linux-x64 binary was not checked, so
it may differ.

**Registry anchors** (grep-able strings in the binary):
- Built-in table: `builtinCommandTable??=uAo()` and `function uAo(){return[v$e,w$e,E$e,gHe,`.
- Bundled skills register through `hs({name:`; init happens in `bundledSkillsInitialized`.
- Internal-only list, compiled out: `yAl=[...Z6t?[Z6t]:[],xqt,Jqt,fqt,`. In this external
  build almost every entry resolves to the stub `{isEnabled:()=>!1,isHidden:!0,name:"stub"}`.
- Gated groups: `function r5t(){return{fleetFork:` (fleetFork, fleetBackground, daemon,
  skillDoctor, logout, pluginTypes).
- Command shapes:
  - `type:"local"`: text result, loaded lazily.
  - `type:"local-jsx"`: Ink UI panel.
  - `type:"prompt"`: expands into a model prompt; skills, MCP prompts and custom commands
    use this shape.
- Many commands exist twice: a `local-jsx` variant for the interactive TUI and a `local`
  variant with `supportsNonInteractive:!0` and `isHidden` set to `!Ce()`. `Ce()` is
  probably the non-interactive (`-p` or SDK) predicate. Several functions are named
  `Ce` in different chunks, so this is moderately certain.

### Visible built-ins

| Command | Aliases | Purpose |
|---|---|---|
| add-dir `<path>` | | Add a working directory |
| advisor | | Let Claude consult a stronger model at key moments (gated by `Vw()`) |
| artifacts | | Browse published and shared artifacts |
| autocompact `[auto\|<tokens>]` | | Set the context fill level that triggers auto-summarizing |
| auto-mode-setup | | Teach auto mode about the environment and adjust rules |
| autofix-pr | | Monitor and autofix the current PR (claude-ai only) |
| background `[prompt]` | bg | Send the session to the background and free the terminal |
| branch `[name]` | | Branch the conversation at this point |
| brief | | Toggle brief-only mode (gated by `enable_slash_command`) |
| btw `[question]` | | Ask a side question without interrupting |
| bug `[report]` | share | Report a bug or share the conversation |
| cd `<path>` | | Move the session to a new working directory |
| chrome | | Claude in Chrome settings (claude-ai only) |
| clear `[name]` | reset, new | Start a new session with empty context; the old one stays resumable |
| cloud-plugins | | Choose whether cloud sessions use local plugins |
| color | | Set the prompt bar color |
| compact `<instructions?>` | | Summarize the conversation to free context |
| config `[key=value]` | settings | Open settings, or set one key |
| context `[all]` | | Visualize context usage as a grid |
| copy `[N]` | | Copy the last (or Nth-latest) response |
| daemon | | Manage background services and routines |
| desktop | app | Continue the session in Claude Desktop (claude-ai only) |
| diff | | Uncommitted changes and per-turn diffs; a panel in fullscreen |
| effort | | Set the effort level |
| exit | quit | Exit the CLI, or detach from a background session (`:q`, `:wq` and similar also recognized) |
| export `[filename]` | | Export the conversation to a file or the clipboard |
| fast `[on\|off]` | | Toggle fast mode |
| feedback `[report]` | | Send feedback or report a bug |
| focus `[on\|off]` | | Focus view: prompt, summary and response only |
| fork `[prompt]` or `<directive>` | | Copy the conversation into a background session, or spawn a background agent; two variants depending on the fleetFork gate |
| goal `[<condition>\|clear]` | | Set a goal Claude checks before stopping |
| help | | Help and available commands |
| hooks | | View hook configurations |
| ide `[open]` | | Manage IDE integrations |
| import `[codex\|gemini\|cursor] [--dry-run]` | | Import config from another coding agent |
| init | | Create CLAUDE.md (a prompt command; `CLAUDE_CODE_NEW_INIT` also offers skills and hooks) |
| insights | | Report analyzing past sessions (prompt) |
| install | | Install the native build |
| install-github-app | | Set up Claude GitHub Actions |
| install-slack-app | | Install the Slack app |
| keybindings | | Open the keybindings file |
| list-agents | peers | List subagents, teammates and sessions you can message |
| login / logout | | Sign in or switch account; sign out |
| mcp `[reconnect\|enable\|disable ...]` | | Manage MCP servers |
| memory | | Edit CLAUDE.md files and memory settings |
| mobile | ios, android | QR code for the mobile app |
| model `[model]` | | Set the model |
| output-style `[style]` | | List or switch output styles (non-interactive; moved to /config in the TUI behind a flag) |
| passes | | Share a free week of Claude Code (eligibility-gated) |
| permissions | allowed-tools | Allow and deny rules |
| plan `[open\|<description>]` | | Enable plan mode or view the plan |
| plugin | plugins, marketplace | Manage plugins |
| powerup | | Interactive feature lessons |
| privacy-settings | | Privacy settings |
| radio | | Claude FM lo-fi radio |
| rate-limit-options | | Usage limits and upgrade options |
| recap | | One-line session recap |
| release-notes | | Release notes |
| reload-plugins `[--force]` | | Activate pending plugin changes |
| reload-skills | | Pick up skills changed on disk |
| remote-control `[name]` | rc | Control the session from a phone or the web, or disconnect |
| remote-env | | Default environment for cloud agents |
| rename `[name]` | name | Rename the conversation |
| resume `[id\|search]` | continue | Resume a previous conversation |
| rewind | checkpoint, undo | Restore code and/or conversation to an earlier point |
| sandbox `exclude "pattern"` | | Sandbox status and configuration |
| scroll-speed | | Mouse wheel speed (some terminals only) |
| security-review | | Security review of the branch's pending changes (prompt) |
| session | remote | Cloud session URL and QR code |
| setup-bedrock / setup-vertex | | Reconfigure provider authentication (shown when that provider is in use) |
| skill-doctor | | Unused skills that still cost context |
| skills | | List skills |
| status | | Version, model, account, connectivity and tools |
| statusline | | Set up the status line (prompt) |
| stickers | | Order stickers |
| stop | | Stop a background session (background sessions only) |
| subtask `<task>` | | Send a subagent off with full context; the result comes back |
| tasks | bashes | View and manage background work |
| team-onboarding | | Teammate ramp guide from your usage (prompt) |
| teleport | tp | Send the session to the cloud, or resume one from claude.ai |
| terminal-setup | | Install the Shift+Enter binding and similar terminal fixes |
| theme | | Change the theme |
| tui `[default\|fullscreen]` | | Choose the renderer |
| ultraplan `<prompt>` | | Draft an editable plan in a cloud session |
| ultrareview | | Cloud agent that finds and verifies bugs (paid) |
| upgrade | | Upgrade to Max |
| usage | cost, stats | Session cost, plan usage and activity |
| usage-credits | | Configure or request usage credits |
| voice `[hold\|tap\|off]` | | Toggle voice mode |
| web-setup | | Connect GitHub for cloud sessions |
| workflows | | Browse workflows (gated by `Lf()`) |
| (commit-push-pr) | | Prompt command `oFr="commit-push-pr"`: commit, push and open a PR. It is defined but no reference to it in `uAo` was found, so it may not be registered |

**Bundled skills invocable as `/name`** (`source:"bundled"`, registered via `hs({...})`;
gated by `isEnabled`):
- Setup and workflow: doctor (alias checkup), code-review (alias review), commit,
  pr, simplify, verify, loop `[interval] [prompt]` (alias proactive), schedule (alias
  routines), batch `<instruction>`, debug, explain-usage, fewer-permission-prompts,
  claude-api, claude-code-docs, run, run-skill-generator.
- Artifacts and design: workshop, whiteboard, prototype, dataviz,
  artifact-diagramming, artifact-components, doc, artifact-pr-review, design and
  design-sync (subcommands consent, revoke, sync, login, import, export, status).
- Integrations and config: claude-in-chrome, update-config, workflow-authoring,
  setup-claude (alias setup-cowork; cowork entrypoint only).

Model-only skills (`userInvocable:!1`) are not typable: artifact-design (`y6`),
keybindings-help, cowork-plugin, and the memory-type taxonomy skill (`t$r`).

### Hidden, internal or gated (separate list)
- `isHidden:!0`:
  - `agents`: removed; its text tells the user to ask Claude instead.
  - `heapdump`: dump the JS heap.
  - `plugin-types [dir]`: write .d.ts files for plugins.
  - `update` (alias restart): disabled.
  - `extra-usage`: renamed to usage-credits.
  - `pro-trial-expired`.
  - `design-consent`, `design-revoke`.
  - `__remote-workflow`, `workflow-launch-exec`: server-launched sessions only.
  - `vim`: "moved to /config" stub gated by `tengu_maple_sundial`.
  - `low-priority`, `limit-reset`, `claim-credit`: rate-limit and promo flows.
- Upsells in `P6t=[iU({name:"ultraplan"...` (hidden, `isEnabled:A6t`, "available with
  Claude for Enterprise"): ultraplan, ultrareview, teleport (tp), remote-control (rc),
  schedule (routines), autofix-pr.
- Disabled outright (`isEnabled:()=>!1`) in this build: pause-memory (aliases
  memory-pause, toggle-memory), loops, version, wellbeing (aliases breaks,
  break-reminder, downtime).
- Compiled-out internal (Anthropic-only) commands: `yAl` resolves to `"stub"` objects.
  Several others are null in this build, including `NHe`, `n2`, `FHe`, `$He`, `BHe`,
  `Zde`, `OHe` (promoteMemory) and `ZK` (memoryAccount).

### (a) Custom and user-defined commands
- **Skill and command loader:** anchor `B=["name","description","model","allowed-tools","argument-hint","arguments"`.
  - Accepted frontmatter: name, description, model, allowed-tools, argument-hint,
    arguments (named args), disable-model-invocation, user-invocable, effort, shell,
    version, when_to_use, paths, hooks, context (`fork`), agent, background.
  - Directories are `.claude/commands`, `.claude/skills` and `.claude/agents`. The anchor
    `k_o=["commands","agents","output-styles","skills"` is followed by a description
    attached to each source, such as "(plugin)" or "(project)" (function `V3e`).
- **Argument substitution** (`function aCe(`):
  - Named `$argname`, `$ARGUMENTS[N]`, `$N`, `$ARGUMENTS`.
  - When nothing was substituted, ` ARGUMENTS: <args>` is appended.
  - Inline `` !`cmd` `` shell runs through `function Cle(` (frontmatter `shell: bash|powershell`).
  - Also replaced: `${CLAUDE_SKILL_DIR}`, `${CLAUDE_PROJECT_DIR}`, `${CLAUDE_SESSION_ID}`,
    `${CLAUDE_EFFORT}`.
- **MCP prompts as commands:**
  - Anchor `name:"mcp__"+vn(e.name)+"__"+g.name`.
  - Shown as `server:prompt (MCP)`. For http/sse servers the bare prompt name is shown,
    with aliases.
  - Arguments are split on whitespace and mapped positionally to the prompt's `arguments`.
  - Missing required arguments throw a usage error.
- **Plugin commands:**
  - Markdown commands become `type:"prompt"`, `source:"plugin"`.
  - Hooks-module plugins can call `$.command.register({name,description,argumentHint,immediate})`
    and answer `command.run` (anchor `registered /${n.name} but no command.run hook answered it`).
- **Workflows:** prompt commands with `kind==="workflow"` show "(dynamic workflow)".

### (b) Discovery
- **Typeahead:** Fuse fuzzy search (anchor `threshold:0.3,location:0,distance:100,keys:[{name:"commandName",weight:3}`).
  Keys are commandName, displayName, partKey (`:`-split parts) and aliasKey, with word
  hits on the description as well.
- **Argument hints:** `argumentHint`. Some commands also define `getArgumentCompletions`
  (config, design) or `subcommands`.
- **Menu text:** `menuDescription` overrides `description` in the menu (function `txr`).
  The menu skips `isHidden` entries. `/help` and `/skills` list commands.

### (c) Behaviour while the model is busy
- **Immediate flag:** each command declares `immediate: true|fn(args,presentation)`,
  resolved by `function i$e(e,o,n){let r=e?.immediate;return typeof r==="function"?r(o,n):r===!0}`.
- **Immediate commands:** the submit path (anchor `tengu_immediate_command_executed`, near
  `Zo=Vo&&(bn||D?.fromKeybinding)`) checks the query guard. When it is active, an
  immediate `local-jsx` or `local` command runs mid-turn. A JSX panel opens with
  `retireAtTurnBoundary:!0`. Text results show as a toast and are appended after the turn.
- **Everything else** goes through the normal submission and message queue (`messageQueue.enqueue`)
  and runs when the turn ends. ctrl+enter sends queued input now.
- **Immediate unconditionally (`immediate:!0`):** artifacts, autocompact, btw, bug,
  daemon, effort, exit, fast, feedback, focus, goal, hooks, import, mcp, model, permissions,
  plugin, remote-control, rename, skill-doctor, skills, status, stop, tasks, usage,
  workflows, brief, color.
- **Immediate conditionally:**
  - add-dir, config: when args are given or in fullscreen.
  - diff, help, theme, advisor: fullscreen only.
  - background: when it has no args.
  - sandbox: unless the arg is `install`.
- Plugin-registered `immediate` is read-only once declared (`command.describe` check).

---

## 3. codex (openai codex-rs TUI)

**Registry:** `codex/codex-rs/tui/src/slash_command.rs:12` (`enum SlashCommand`).
- Enum order is popup order; a comment says do not alpha-sort.
- Descriptions are in `description()` at L90.
- Gating: `available_during_task()` L239, `supports_inline_args()` L166,
  `available_in_side_conversation()` L193, `is_visible()` L304.
- Feature filters: `codex/codex-rs/tui/src/bottom_pane/slash_commands.rs:71`
  (`builtins_for_input`).

| Command | Aliases | Purpose | During task |
|---|---|---|---|
| model | | Choose model and reasoning effort | yes |
| ide | | Include IDE selection and open files | yes |
| permissions | | What Codex may do | yes |
| keymap | | Remap TUI shortcuts | no |
| vim | | Toggle composer Vim mode | no |
| setup-default-sandbox | | Elevated agent sandbox (gated by `allow_elevate_sandbox`) | no |
| experimental | | Toggle experimental features | no |
| approve | | Approve one retry of an auto-review denial | yes |
| memories | | Memory use and generation | no |
| skills | | Use or toggle skills | yes |
| import | | Import setup, project and chats from Claude Code | no |
| hooks | | View and manage lifecycle hooks | yes |
| review | | Review current changes (validated at dispatch) | no |
| rename | | Rename the thread | yes |
| new | | New chat | no |
| archive / delete | | Archive / permanently delete the session | no |
| resume | | Resume a saved chat | yes (but blocked mid-turn) |
| fork | | Fork the chat | no |
| worktree | | Conversation in a new worktree (gated) | no |
| app | | Continue in the Desktop app (macOS and Windows only) | yes |
| init | | Create AGENTS.md | no |
| compact | | Summarize to save context | no |
| recap | | Summarize the conversation now | no |
| plan | | Plan mode (collaboration-modes gate) | no |
| voice | | Start or stop voice; settings (gated) | yes |
| goal | (`go+al`, e.g. `goooal`) | Goal for a long task (gated) | yes |
| agents | | Agent command center | yes |
| side | btw | Side conversation in an ephemeral fork | yes |
| copy | | Copy the last response (not on Android) | yes |
| export | | Export as markdown | no |
| raw | | Raw scrollback mode | yes |
| tui | | TUI mode for the next launch | no |
| diff | | git diff including untracked files | yes |
| mention | | Mention a file | yes |
| status | | Session config and token usage | yes |
| daemon | | Local background server | yes |
| warnings | | Retained warnings | yes |
| cd | | Change the working directory (blocked mid-turn) | no |
| pwd | cwd | Show the working directory | yes |
| usage | | Account usage and limit reset (gated) | yes |
| debug-config | | Config layers and requirement sources | yes |
| title / statusline | | Configure the terminal title / status line items | yes |
| theme | | Syntax highlighting theme | no |
| pets | pet | Terminal pet | no |
| mcp | | List MCP tools; `verbose`, `login <name>` | yes |
| apps | | Manage apps (connectors gate) | yes |
| plugins | | Browse plugins (gated) | yes |
| logout | | Log out | no |
| quit, exit | (quit is an alias of exit in the popup) | Exit Codex | yes |
| feedback | | Send logs | yes |
| ps | | List background terminals | yes |
| stop | clean | Stop all background terminals | yes |
| clear | | Clear the terminal and start a new chat | no |
| subagents | | Switch between subagents (`MultiAgents`) | yes |

**Model service tiers as commands:** `ServiceTierCommand` (for example `/fast`, `/slow`)
are inserted after `/model` when `service_tier_commands_enabled` is set
(`slash_commands.rs:86`). They always count as available during a task.

**Hidden and debug commands:**
- rollout (print the rollout path) and test-approval: `cfg!(debug_assertions)` builds only.
- debug-m-drop and debug-m-update: "DO NOT USE".

### (a) Custom and user-defined commands
- **Custom prompts:** no `~/.codex/prompts` or `/prompts:name` support exists in this
  snapshot. Searches for prompt directories and custom-prompt types in codex-rs found
  only the review "custom prompt" editor (`OpenReviewCustomPrompt`). The doc comment at
  `command_popup.rs:144` still mentions "user prompts", which looks like a leftover.
  The feature appears to have been removed. Moderately certain.
- **Skills are not slash commands.**
  - They are invoked by `$name` mention in the composer (`chat_composer.rs:2735`,
    `sigil '$'|'@'`, popup `skill_popup.rs`).
  - `/skills` manages them.
  - Roots: `codex/codex-rs/ext/skills/src/host_roots.rs:24` (repo `.agents/skills`), user
    skills, deprecated `$CODEX_HOME/skills`, system `$CODEX_HOME/skills/.system` (embedded
    assets: imagegen, openai-docs, review-agent, skill-creator, skill-installer), and admin.
- **MCP prompts:** no prompts/list or GetPrompt use was found in the TUI, so MCP prompts
  are not slash commands.

### (b) Discovery
- **Popup:**
  - Opens on `/`; built from `CommandPopup` (`command_popup.rs`).
  - With an empty filter it lists everything in enum order and hides aliases `quit` and `btw`.
  - With text it matches exact names first, then prefixes (`filtered()` L147), and
    highlights the matched characters.
- **Typed lookup:** `find_builtin_command` resolves aliases through strum.
  `has_slash_command_prefix` uses `fuzzy_match`.
- **Inline arguments:** only for commands in `supports_inline_args`. There are no
  per-command argument hints; descriptions sometimes carry usage text, such as for `/mcp`.
- **Unknown commands:** `Unrecognized command '/x'. Type "/" for a list...` (`chat_composer.rs:3032`).

### (c) Behaviour while the model is busy
- **Rejected:**
  - Enter on a command whose `available_during_task()` is false gives
    `'/{cmd}' is disabled while a task is in progress.` (`chat_composer.rs:3387-3400`).
  - Dispatch checks again (`chatwidget/slash_dispatch.rs:137-157`, `:174`).
  - `/resume` and `/cd` are also blocked while a turn is pending or running.
  - `/review` is blocked when a turn is pending or running, or follow-ups are queued.
- **Queued:**
  - Tab queues slash-led input while a task runs, deferring validation
    (`chat_composer.rs:3173` `should_parse_on_dequeue`; test `tab_queues_slash_led_prompts_while_task_running_without_validation` L9963).
  - When the queue drains, `submit_queued_slash_prompt` (`slash_dispatch.rs:1078`) parses
    and dispatches with `SlashCommandDispatchSource::Queued`.
- **Side conversations:** only the `available_in_side_conversation` subset is usable.

## Appendix B

### Part B: built-in slash commands (opencode, pi, oh-my-pi, crush, mimo-code)

All paths are relative to `/Users/patrickbeam/projects/blackbird/quarantine/`. This was a read-only grep/read survey: nothing was run or built.

---

## 1. opencode

**Registries.** Commands come from two layers.
- **TUI-local (client) commands** are keymap/palette entries that carry `slashName`/`slashAliases`. They are registered in:
  - `opencode/packages/tui/src/app.tsx:525` (`appCommands`)
  - `opencode/packages/tui/src/routes/session/index.tsx:458` (`sessionCommandList`, `slash:{name,aliases}`)
  - `opencode/packages/tui/src/component/prompt/index.tsx:329` (`promptCommands`)
  - `opencode/packages/tui/src/feature-plugins/system/diff-viewer.tsx:1042`
  - `opencode/packages/tui/src/feature-plugins/session/index.tsx:17`

  These are converted to `/name` entries at `opencode/packages/tui/src/keymap.tsx:270-285`.
- **Server commands** expand into prompts. They live in `opencode/packages/opencode/src/command/index.ts:53` (`Default`), and the built-ins start at `:77`.

**Server commands (prompt templates)**
| cmd | purpose |
|---|---|
| /init | "guided AGENTS.md setup" (template `command/template/initialize.txt`) |
| /review | "review changes [commit\|branch\|pr], defaults to uncommitted"; `subtask: true` (runs as a subagent task) |

**TUI commands**
| cmd | aliases | purpose |
|---|---|---|
| /sessions | resume, continue | session switcher dialog |
| /new | clear | new session (navigate home) |
| /models | mo | model picker (the alias `mo` biases ahead of /move) |
| /agents | | agent picker |
| /mcps | | toggle MCP servers |
| /variants | | model variant picker (hidden when the model has no variants) |
| /connect | | provider connect dialog |
| /org | orgs, switch-org | switch console org (registered only when the account has more than one switchable org) |
| /status | | status dialog |
| /themes | | theme picker |
| /help | | help dialog |
| /exit | quit, q | exit the app |
| /share | | share session or copy the share link (disabled when `config.share === "disabled"`) |
| /unshare | | unshare (enabled only when the session is shared) |
| /rename | | rename session |
| /timeline | | jump to a message |
| /fork | | fork from a timeline point |
| /compact | summarize | `session.summarize` with the current model |
| /undo | | revert the last user message and put its text back in the prompt (aborts first if busy) |
| /redo | | unrevert (enabled only when a revert exists) |
| /timestamps | toggle-timestamps | show/hide timestamps |
| /thinking | toggle-thinking | cycle thinking display |
| /copy | | copy the session transcript to the clipboard |
| /export | | export the transcript to a file (options dialog) |
| /editor | | open $EDITOR on the prompt |
| /skills | | skill picker; selecting one inserts `/<skill> ` |
| /move | | move the session to another project dir |
| /diff | | open the diff viewer (builtin TUI plugin) |

**Feature-flagged or hidden**
- `/workspaces` (`hidden: !Flag.OPENCODE_EXPERIMENTAL_WORKSPACES`, app.tsx:581).
- `/warp` (change workspace; `enabled: Flag.OPENCODE_EXPERIMENTAL_WORKSPACES`, prompt/index.tsx:528).
- Many palette-only commands have no slash name: sidebar toggle, conceal, tool details, scrollbar, theme mode/lock, docs, debug panel, model/agent/variant cycling, quick-switch 1-9.

**Direct mode (`opencode run`, non-TUI).** Only `/new` and `/exit` (also `/quit` and `:q`) exist (`opencode/packages/opencode/src/cli/cmd/run/prompt.shared.ts:42-46`, `footer.prompt.tsx:417`).

**(a) Custom commands**
- **Markdown files.** The loader globs `{command,commands}/**/*.md` in each config dir, which includes `.opencode/command/`. Frontmatter supplies agent/model/description/subtask (`opencode/packages/opencode/src/config/command.ts:18`).
- **Config.** Commands can also be defined under `command` in config (`command/index.ts:97`).
- **Template expansion** (`opencode/packages/opencode/src/session/prompt.ts:1488-1530`):
  - `$1..$N` are positional; the last placeholder takes the rest of the arguments.
  - `$ARGUMENTS` is the raw argument string.
  - If the template has no placeholders, the arguments are appended.
  - `` !`cmd` `` shell output is inlined.
  - `@file` parts are resolved.
- **MCP prompts.** Registered as `/<name>` with `source: "mcp"`. Prompt args map to `$1..` (`command/index.ts:112`). Autocomplete shows them with a `:mcp` suffix.
- **Skills.** Registered as `/<skill>` with `source: "skill"`, and lose to any same-named command (`:141`). They are hidden from autocomplete (`autocomplete.tsx:470`) and reachable through /skills.
- **Plugins.** TUI plugins can add slash entries via `slash:{name,aliases}` (`opencode/packages/tui/src/plugin/command-shim.ts:59`).
- **Hook.** A `command.execute.before` hook runs before execution.

**(b) Discovery**
- Typing `/` at position 0 opens autocomplete. It merges TUI slashes with server commands, sorts them, and applies fuzzysort filtering (`opencode/packages/tui/src/component/prompt/autocomplete.tsx:467`).
- The `ctrl+p` command palette lists all non-hidden commands (`opencode/packages/tui/src/config/keybind.ts:57`).
- `/help` opens a dialog.
- Argument hints are computed server-side (`Command.hints`: `$1`, `$ARGUMENTS`). I did not confirm whether the TUI shows them (uncertain).

**(c) While the model is busy**
- **TUI commands** run immediately on the client. `/undo` explicitly aborts a busy session first (session/index.tsx ~613).
- **Typed server commands** (`/init`, `/review`, custom) go to `session.command` (prompt/index.tsx:1063-1084), then `prompt()`. `prompt()` persists the user message right away and calls `loop()`, which goes through `ensureRunning` (`opencode/packages/opencode/src/effect/runner.ts:115`). When a run is already active, ensureRunning joins it rather than refusing. My inference is that the new message is picked up by the running loop, so it behaves like a queued or injected message. I did not trace this step by step.
- **Refusals.** Only shell starts (`startShell`, giving a BusyError) and message delete/revert (`assertNotBusy`) refuse while busy.
- **/compact while busy.** Not verified (uncertain).

---

## 2. mimo-code (an opencode fork, confirmed)

**Fork evidence.** The root `package.json` has `"name": "opencode"`, and the tree has the same structure (`packages/cli/src/command/index.ts`, `cli/cmd/tui/...`). Only differences from opencode are listed below.

**Registries**
- Server: `mimo-code/packages/cli/src/command/index.ts:63`.
- TUI:
  - `mimo-code/packages/cli/src/cli/cmd/tui/app.tsx:454`
  - `.../routes/session/index.tsx:542`
  - `.../component/prompt/index.tsx:615`

  This is the older `command.register(...)`/`slash:{}` style; the newer opencode uses keymap layers.
- Built-in descriptions are i18n'd (`cli/cmd/tui/i18n/slash-command.ts`).

**Added server commands**
| cmd | purpose |
|---|---|
| /dream | manual memory-consolidation pass (agent `dream`; reads memory files and the trajectory DB) |
| /distill | find repeated workflows and package them as skills, subagents or commands (agent `distill`) |
| /goal | sets a stop-condition goal; the runLoop won't stop until a judge says it's met. `/goal clear`/`reset` clears it. Special-cased in `session/prompt.ts:5940` |
| /rebuild | rebuilds context from the latest checkpoint, with no model turn (`session/prompt.ts:5985`) |

**Added TUI commands**
| cmd | aliases | purpose |
|---|---|---|
| /force-agent | | force a mode switch, bypassing the mid-session lock |
| /modalities | | configure input modalities |
| /context-limit | | set the context limit (early compaction) |
| /never-ask | | toggle auto-decide at question points (permissions excluded) |
| /skip-permissions | | toggle skipping all permission prompts |
| /permission-timeout | | set the permission ask timeout |
| /login, /logout | | provider auth |
| /worktree | wt | worktrees dialog |
| /background | | switch background image |
| /logo | | switch logo design |
| /vivid | | toggle vivid/minimal visuals |
| /dark, /light | | explicit theme mode (opencode has only an unnamed toggle) |
| /doc | docs | open docs (opencode has no slash name for this) |
| /language | lang | UI language |
| /recover | | continue an interrupted turn |
| /btw | | side question that does not touch the conversation (details below) |
| /voice, /voice-send, /voice-control | | voice input toggles |

**Detail on /btw.** The `/btw <q>` form with arguments is handled inline at prompt/index.tsx:1181. It calls `session.ask` and shows the answer in a read-only dialog.

**Removed relative to opencode**
- /workspaces, /warp, /move, /diff.
- The `mo` alias on /models.
- The `toggle-*` aliases still exist.

**Flagged**
- `/workflows` (TUI) and `/deep-research` (server) need `MIMOCODE_EXPERIMENTAL_WORKFLOW_TOOL`.
- `/loops` (list/cancel scheduled jobs) needs `MIMOCODE_EXPERIMENTAL_CRON`.

**Other differences**
- **Custom command dirs.** Commands load from `.mimocode/command[s]/` (`config/command.ts:47`).
- **Enter dispatch.** Pressing Enter on an exact `/name` runs the client slash even without an autocomplete selection (prompt/index.tsx:1166).
- **Busy behavior.** It keeps opencode's runner model (`ensureRunning` in `session/run-state.ts`), which I assume works the same way (not traced).

---

## 3. pi (pi-mono, `packages/coding-agent`)

**Registry.** `pi/packages/coding-agent/src/core/slash-commands.ts:19` (`BUILTIN_SLASH_COMMANDS`, with name, description and an optional argumentHint). Dispatch is a chain of string compares in `pi/packages/coding-agent/src/modes/interactive/interactive-mode.ts:3113-3256`.

| cmd | arg hint | purpose |
|---|---|---|
| /settings | | settings menu |
| /model | `<provider/model>` | model selector, or a direct set with fuzzy argument completion |
| /tree | | navigate the session tree (branches) |
| /thinking | `<level>` | set the thinking level |
| /scoped-models | | choose models for Ctrl+P cycling |
| /export | [path] | export the session (HTML by default, or .html/.jsonl) |
| /import | [path] | import and resume a JSONL session |
| /share | | secret GitHub gist |
| /bug | `<description>` | report a bug |
| /copy | | copy the last agent message |
| /name | [name] | set the session display name |
| /session | | session info and stats |
| /changelog | | changelog |
| /hotkeys | | list keyboard shortcuts (pi has no /help) |
| /fork | | fork from a previous user message |
| /clone | | duplicate the session at the current position |
| /trust | | save the project trust decision |
| /login | `<provider>` | provider auth |
| /logout | | remove auth |
| /new | | new session |
| /compact | [instructions] | manual compaction with optional custom instructions |
| /resume | | session selector |
| /reload | | reload keybindings, extensions, skills, prompts, themes and context files |
| /quit | | quit |

No aliases are defined.

**Hidden (dispatched but not in the registry or autocomplete)**
- `/debug` writes render/debug data to a log (interactive-mode.ts:3233).
- `/arminsayshi` and `/dementedelves` show easter-egg components (:3238, :3243).

**Non-slash prefixes.** `!cmd` runs bash; `!!cmd` runs bash with the output excluded from context.

**(a) Custom commands**
- **Prompt templates.** Markdown in `~/.pi/agent/prompts/` and `<cwd>/.pi/prompts/` becomes `/name args` (`core/prompt-templates.ts:218-236`). Placeholders: `$1..`, `$@`/`$ARGUMENTS`, `${@:N[:L]}`, and `${ARGUMENTS:-default}` (`:61-75`).
- **Skills.** Available as `/skill:<name> args` when the `enableSkillCommands` setting is on (default true) (`core/settings-manager.ts:162`, `core/agent-session.ts:2070`).
- **Extensions.** `pi.registerCommand(name, ...)` (`core/extensions/types.ts:1628`). A name that conflicts with a built-in is skipped in autocomplete or renamed (interactive-mode.ts:688).
- **MCP prompts.** None found in core; pi core has no MCP.

**(b) Discovery**
- The pi-tui autocomplete provider merges built-ins, prompt templates (interactive-mode.ts:765) and `skill:*` (:787). It uses fuzzy filtering, shows argument hints, and has dynamic argument completions for /model, /thinking and /login (:704-760).
- There is no palette. `/hotkeys` serves as the help page.

**(c) While the model is busy**
- **Built-ins** are matched before the streaming branch, so they execute immediately mid-stream. Some handlers guard themselves:
  - `/reload` warns "Wait for the current response to finish" (:6231).
  - `/tree` navigation aborts the active response (:5516).
- **Extension commands** execute immediately even during streaming (`agent-session.ts:1902`).
- **Other text** (including expanded templates and skills) is queued as a *steer* via `prompt(text,{streamingBehavior:"steer"})` (interactive-mode.ts:3291). The follow-up key queues *followUp* instead.
- **During compaction**, input is queued, except extension commands, which run (:3279).

---

## 4. oh-my-pi (pi fork, "omp")

**Registry**
- `oh-my-pi/packages/coding-agent/src/slash-commands/builtin-registry.ts:40` concatenates the specs from `builtin-{modes,collaboration,session,lifecycle,marketplace,skills,control}.ts`.
- The lookup covers aliases (:50).
- Each spec has `handle` (ACP/text) and/or `handleTui`, plus `subcommands`, `inlineHint` and `allowArgs`.
- Dispatched from `modes/controllers/input-controller.ts:1060`.

Commands marked † also exist in pi.

**Modes and models (`builtin-modes.ts`)**
| cmd | aliases | purpose |
|---|---|---|
| /security | | plan, run, inspect, import and compare security scans |
| /settings † | | settings menu |
| /setup | providers | provider setup |
| /plan [prompt] | | toggle plan mode |
| /plan-review | | reopen the latest plan review |
| /vibe [prompt] | | vibe mode (persistent fast/good worker sessions, read-only toolset) |
| /goal [objective] | | persistent autonomous objective mode |
| /guided-goal | | the agent interviews you, then sets goal mode |
| /loop [count\|duration] [--while\|--until cmd] [prompt] | | resubmit the prompt after every yield |
| /queue `<msg>` | | queue a message for after the agent yields |
| /model † | models | model selector |
| /switch [model] | | session model switch (fuzzy, `@role`, `:level`) |
| /fast [on\|ultra\|off\|status] | | priority/fast service tier |
| /slow [on\|off\|status] | | flex/low-priority tier |
| /skillful | | toggle listing skills in the system prompt |
| /extended-context | | toggle extended context windows |
| /computer | | toggle the computer-use eval prelude |
| /ratchet | | build an eval for an LLM flow and hillclimb it |
| /prewalk | | arm a one-shot model handoff |
| /modelpreset [save\|switch\|delete\|list] | | model presets |

**Collaboration and output (`builtin-collaboration.ts`)**
| cmd | purpose |
|---|---|
| /advisor | second model that reviews each turn |
| /export † | HTML export (`--themes`) |
| /trace | open the trace in the stats dashboard |
| /dump | copy the transcript and write the request JSON |
| /share † | encrypted link or secret gist |
| /collab [start\|view\|list\|stop\|status] | live share via relay |
| /join `<link>` | join a collab session |
| /leave | leave a collab session |
| /browser | headless/visible browser prelude |
| /copy † | picker for text or code (pi copies the last message) |
| /open | open the last link |

**Session and tools (`builtin-session.ts`)**
| cmd | aliases | purpose |
|---|---|---|
| /todo | | view or modify the todo list |
| /session † | | session subcommands (pi only shows info) |
| /jobs | | background jobs |
| /usage | | provider usage and limits |
| /stats | | local stats dashboard |
| /changelog † | | |
| /hotkeys † | | |
| /tools | | visible tools |
| /context | | context usage breakdown |
| /extensions | status | Extension Control Center |
| /agents | | agents hub |
| /git [rev] | | git UI |
| /hub | | live agent hub |
| /branch | rewind | rewind, keeping the old path as a branch |
| /fork † | | |
| /tree † | | |
| /login † | | |
| /logout † | | |
| /mcp `<sub>` | | manage MCP servers (add/list/remove/test) |

**Lifecycle (`builtin-lifecycle.ts`)**
| cmd | aliases | purpose |
|---|---|---|
| /ssh `<sub>` | | SSH hosts |
| /new † | | |
| /fresh | | reset provider stream state |
| /clear | | clear context in place |
| /delete | | delete the session and start a new one |
| /compact † | | |
| /shake | | drop heavy content from context |
| /handoff [focus] | | handoff document, then compact |
| /resume † | | accepts `[id\|@claude\|@codex]` |
| /pin | | pin a session in the resume list |
| /btw [q] | | side question or BTW history |
| /tan `<work>` | | background agent on tangential work |
| /omfg `<complaint>` | | forge a TTSR rule |
| /cleanse | | fix diagnostics with parallel subagents |
| /retry | | retry the last failed turn |
| /debug | | debug tools selector (hidden in pi, listed here) |
| /memory | | memory maintenance |
| /rename [title] | | |
| /move [path] | | |
| /wt [branch] | worktree | move the session into a new worktree |
| /add-dir, /remove-dir, /dirs | | multi-root workspace dirs |
| /exit | | |
| /restart | | relaunch and resume |

**Marketplace, skills and control**
| cmd | aliases | purpose |
|---|---|---|
| /marketplace | | plugin sources |
| /plugins | plugin | installed plugins |
| /reload-plugins | | reload all plugins |
| /skills | | skills.omp.sh registry: search, install, update |
| /force `<tool>` [prompt] | force: | force the next turn to use a tool |
| /live | | realtime voice |
| /record | | screen recording |
| /pause | | freeze all agents |
| /quit † | q | |

**Bundled commands (shipped TS custom commands, `extensibility/custom-commands/loader.ts:155`)**
- `/green`: iterate on CI until green.
- `/review`: interactive code review.
- `/annotate`: annotate a diff, file, reply or text.

**Differences from pi**
- **Removed from pi's list:** /scoped-models, /thinking, /import, /bug, /name (replaced by /rename), /clone, /trust, /reload (replaced by /reload-plugins), /arminsayshi, /dementedelves.
- **Added:** everything else above.
- **Aliases:** pi has none; omp has setup/providers, model/models, extensions/status, branch/rewind, wt/worktree, plugins/plugin, force/force:, quit/q.
- **Registry shape:** declarative subcommands with inline hints, plus a shared ACP dispatcher (`slash-commands/acp-builtins.ts`).
- **Collab guests:** limited to an allowlist (`collab/guest.ts:43`).

**Hidden or flagged**
- No env/feature flags were found in the registry files.
- `/exit` without a slash (bare `exit`/`quit`/`q`) works on an empty session (`input-controller.ts:86`).
- The opt-in setting `input.bareSlashCommands` (default false, `modes/settings.ts:854`) lets a bare word that names a command run it. In a non-empty session it asks for confirmation first.

**(a) Custom commands**
- **Markdown file commands.** Loaded from `.omp/commands`, `.claude/commands/*.md` (with namespace aliases), `~/.codex/commands`, `.codex/prompts`, opencode dirs, and Claude/omp plugins (`discovery/builtin.ts:350`, `discovery/claude.ts:333`, `discovery/codex.ts:319`). They support an argument hint.
- **Prompt templates.** Inherited from pi.
- **TS custom commands.** Loaded from `commands/` dirs (`extensibility/custom-commands/loader.ts:104`).
- **Extensions.** `registerCommand`.
- **Skills.** Available as `/skill:<name>` when `enableSkillCommands` is set (`slash-commands/available-commands.ts:64`).
- **MCP prompts.** Exposed as `/<server>:<prompt>` and rebuilt on `prompts/list_changed` (`sdk.ts:1451`).

**(b) Discovery**
- Autocomplete covers built-ins (with subcommand completions, inline hints and a dynamic /switch model list), skills, extension, custom, MCP and file commands (`available-commands.ts`).
- Subcommand `help` entries exist for /mcp, /ssh and /marketplace.
- There is no /help command.

**(c) While the model is busy**
- **Built-ins** dispatch before the streaming branch (input-controller.ts:1060 vs :1216), so they run immediately. Commands can return `{prompt}` to pass text on.
- **Skill commands** are sent as steers (:1111-1126).
- **Plain text and templates** are steered via `prompt(...,{streamingBehavior:"steer"})` (:1216).
- **Explicit queueing.** `/queue` and the queue shorthand (:1030) defer a message until the agent yields.
- **During compaction**, input is queued (:1123, :1181).

---

## 5. crush (Go)

**No typed slash commands.** Crush uses a **command palette dialog** instead.
- `/` opens it, but only when the editor is empty (`crush/internal/ui/model/keys.go:162`, `ui.go:3279`).
- `ctrl+p` also opens it (`keys.go:101`, `ui.go:2968`).
- There is no `/name args` parsing.

**Registry.** `crush/internal/ui/dialog/commands.go:449` (`defaultCommands`). The palette has three tabs: System / User / MCP (`:34`).

**System commands.** Items are title-based, not names. Aliases feed the filter (`commands_item.go:50`).
| item | key | condition and purpose |
|---|---|---|
| New Session (alias clear) | ctrl+n | |
| Sessions | ctrl+s | switcher |
| Switch Model | ctrl+l | |
| Summarize Session | | only with an active session (compaction) |
| Enable/Disable Thinking Mode | | Anthropic-style reasoning models |
| Select Reasoning Effort | | models with reasoning levels |
| Toggle Sidebar | ctrl+b | session open and width at least 120 |
| Open File Picker | ctrl+f | model supports images |
| Open External Editor | ctrl+o | only when $EDITOR is set |
| Enable / Disable Docker MCP Catalog | | when Docker MCP is available or enabled |
| Toggle MCPs | | when MCP servers are configured |
| Toggle To-Dos/Queue | ctrl+t | when todos or a queue exist |
| Notification Style | | |
| Toggle Yolo Mode | ctrl+y | skip permissions |
| Toggle Help | ctrl+g | |
| Initialize Project | | writes the project context file (the /init equivalent) |
| Enable/Disable Background Color | | transparency |
| Themes | | |
| Enable/Disable Mouse | | |
| Quit (alias exit) | ctrl+c | |

Every item marked conditional above is shown only when its condition holds. There are no env-flagged commands.

**(a) Custom commands** (`crush/internal/commands/commands.go`)
- **Markdown files** (`:121`):
  - `$XDG_CONFIG_HOME/crush/commands/` and `~/.crush/commands/` get the prefix `user:`.
  - `<data dir, default .crush>/commands/` gets the prefix `project:`.
  - Subdirectories become `:` segments.
- **Arguments** are named `$UPPER_CASE` placeholders, all treated as required (`:18`, `:190`). An Arguments dialog collects them and `substituteArgs` fills them in (`ui.go:2535`, `ui.go:2588`).
- **Skills.** User-invocable skills (`user-invocable: true`) appear as `user:<skill>` and selecting one attaches the skill (`commands.go:66`, `ui.go:2558`).
- **MCP prompts.** Shown as `<server>:<prompt>` in the MCP tab, with an argument form built from prompt arguments (`commands.go:90`, `ui.go:2561`).

**(b) Discovery.** Palette only: a "Type to filter" fuzzy list, tab cycling between System/User/MCP, and `ctrl+g` help. There is no inline slash completion.

**(c) While the agent is busy (`ui.go`)**
- **Refused with a warning:** New Session (:2147), Summarize (:2156), External Editor (:2172), Initialize Project (:2469; its warning text wrongly says "summarizing"), Reasoning Effort (:2481).
- **Queued:** custom commands and MCP prompts call `sendMessage` with no busy check. The agent queues the call (`crush/internal/agent/agent.go:402` `enqueueCall`). `drainQueueForStep` (:420) folds queued calls without a RunID into the active turn at the next step.
- **Toggles** (sidebar, yolo, help, theme, mouse) apply immediately.

## Appendix C

### Part C: built-in slash commands, gemini-cli / qwen-code / kimi-cli / goose / continue

Read-only survey of source snapshots under `/Users/patrickbeam/projects/blackbird/quarantine/`. Paths below are relative to each agent's tree. "?" marks uncertainty.

---

## 1. gemini-cli (TypeScript)

**Registry**
- `packages/cli/src/services/BuiltinCommandLoader.ts:123`: the `allDefinitions` array holds the built-ins, with conditional gates inline (lines 125-239).
- Loader assembly (interactive): `packages/cli/src/ui/hooks/slashCommandProcessor.ts:334-337`. Order is Builtin, Skill, McpPrompt, File.
- Non-interactive (`-p`) loaders: `packages/cli/src/nonInteractiveCliCommands.ts:45-47`. Builtin, McpPrompt and File only; no skills.
- Command contract: `packages/cli/src/ui/commands/types.ts:193` (`SlashCommand`). Fields: `name`, `altNames`, `hidden`, `suggestionGroup`, `autoExecute`, `isSafeConcurrent`, `completion`, `subCommands`.
- `CommandKind` (types.ts:182) is one of BUILT_IN, USER_FILE, WORKSPACE_FILE, EXTENSION_FILE, MCP_PROMPT, AGENT, SKILL.
- Name conflicts: `services/SlashCommandResolver.ts:28`. A built-in keeps its name. Conflicting non-built-ins are renamed with a source prefix (extension name or MCP server name).

**Built-in commands** (all in `packages/cli/src/ui/commands/*.ts`)

| Command (aliases) | Purpose / subcommands |
|---|---|
| /about | Version info. `isSafeConcurrent` |
| /auth | `signin` (login), `signout` (logout) |
| /bug | File a bug report |
| /bug-memory | Write a V8 heap snapshot for a bug report |
| /chat | No args: opens the session browser. Subcommands `list`, `save <tag>`, `resume`(load) `<tag>`, `delete <tag>`, `share <file>` (md/json), and hidden `checkpoints`(checkpoint) for compatibility. `debug` added on nightly builds |
| /resume | Same session browser and same subcommands as /chat (chatCommand.ts:384; resumeCommand.ts:16) |
| /clear (new) | Clear the screen and start a new session |
| /commands | Custom commands: `list`, `reload`(refresh) |
| /compress (summarize, compact) | Replace the context with a summary |
| /copy | Copy the last result or code to the clipboard |
| /docs | Open the docs in a browser |
| /export-session | Write the session to JSON |
| /directory (dir) | `add <paths>`, `show` |
| /editor | Choose the external editor |
| /extensions | `list`, `update <names>`/`--all`, `enable`, `disable`, `install`, `link`, `uninstall`(delete), `explore`, `reload`(restart), `config`. Admin-disabled: a stub that returns an error |
| /help | Help dialog |
| /footer (statusline) | Configure footer items |
| /shortcuts | Toggle the shortcuts panel |
| /rewind | Jump back to a message and restart from there |
| /ide | `status`, `install`, `enable`, `disable` |
| /init | Write GEMINI.md |
| /mcp | `list`(ls, nodesc, nodescription), `desc`(description), `schema`, `auth`, `reload`(refresh), `enable`, `disable`. Admin-disabled: stub |
| /memory | `show`, `reload`(refresh), `list` (GEMINI.md paths), `inbox`. **No `add` in this snapshot** (memoryCommand.ts:148) |
| /model | `set <model>`, `manage` (dialog) |
| /policies | `list` (active policies by mode) |
| /privacy | Privacy notice |
| /quit (exit) | Exit |
| /restore | Restore a tool-call checkpoint (file snapshot) |
| /stats (usage) | `session`, `model`, `tools`. All `isSafeConcurrent` |
| /theme | Theme dialog |
| /tools | `list`, `desc`(descriptions) |
| /settings | Settings dialog |
| /gemma | Local Gemma routing status. `isSafeConcurrent` |
| /tasks (bg, background) | Toggle the background tasks view |
| /vim | Toggle vim mode. `isSafeConcurrent` |
| /setup-github | Set up GitHub Actions |
| /terminal-setup | Configure terminal keybindings |

**Conditional / hidden / feature-flagged**
- /agents (`list`, `enable`, `disable`, `config`, `reload`(refresh)): only when `isAgentsEnabled()` (loader:125).
- /hooks (`panel`(list, show), `enable`, `disable`, `enable-all`(enableall), `disable-all`(disableall)): only when `getEnableHooksUI()` (166).
- /permissions (`trust`): only when folder trust is on (192).
- /plan (`copy`): only when `isPlanEnabled()` (193).
- /skills (`list`, `link`, `disable`, `enable`, `reload`(refresh)): only when skills support is on. Admin-disabled: stub (206-228).
- /voice (`model`): only when voice mode is on (235).
- /upgrade: only with LOGIN_WITH_GOOGLE auth (236).
- /oncall (`dedup`, `audit`): nightly builds only (170).
- /profile: development builds only (196).
- /corgi: `hidden: true` (corgiCommand.ts:12).
- chat `checkpoints`: `hidden: true` (chatCommand.ts:397).
- /chat|/resume `debug`: nightly only.

**(a) Custom commands**
- **FileCommandLoader** (`services/FileCommandLoader.ts`) reads TOML files from three places:
  - `~/.gemini/commands/**/*.toml` (user)
  - `<proj>/.gemini/commands/**/*.toml` (workspace; requires a trusted folder)
  - `<ext>/commands/` for active extensions, which get a `[ext]` description prefix (dirs at :205-247)
- File schema: `prompt` (required) and `description`.
- Subdirectories become `:` namespaces, e.g. `git/commit.toml` becomes `/git:commit` (:306-320).
- Prompt processors (`services/prompt-processors/`):
  - `{{args}}` substitutes the arguments.
  - Without `{{args}}`, the raw invocation is appended (DefaultArgumentProcessor, :354).
  - `!{cmd}` runs a shell command. It goes through a `confirm_shell_commands` return (:400) unless already allowlisted.
  - `@{path}` injects file content.
- `/commands reload` re-scans the directories.
- **McpPromptLoader** (`services/McpPromptLoader.ts:45`):
  - Each MCP prompt becomes `/<prompt-name>`, with spaces replaced by `-`.
  - Arguments can be named (`--arg="v"`) or positional.
  - Each prompt gets a `help` subcommand listing its arguments.
  - `autoExecute` is true when the prompt takes no arguments.
  - Arguments get completion.
- **SkillCommandLoader** (`services/SkillCommandLoader.ts:38`): each displayable skill becomes `/<skill>`. The action returns `{type:'tool', toolName: activate_skill}`, so skills run as commands.
- AGENT kind appears only in `@` completion (`useAtCompletion.ts:150`), not as slash commands.

**(b) Discovery**
- Typing `/` opens a suggestion list with fzf fuzzy matching (`ui/hooks/useSlashCompletion.ts:8,239`).
- `suggestionGroup` draws section separators. Suggestions carry kind tags such as ` [Agent]` (`SuggestionsDisplay.tsx:70`).
- `autoExecute`: Enter on a suggestion either runs it immediately or only completes it into the prompt.
- Per-command `completion()` handles arguments, e.g. checkpoint tags.
- `/help` opens a dialog.
- No explicit argument-hint field.

**(c) While the model is busy**
- `isSafeConcurrent` opts a command in to running during a stream (types.ts:217). Opted in: about, stats (+subs), gemma, vim.
- `InputPrompt.tsx:461-487`: while Responding, a safe-concurrent slash command runs at once. Any other slash command or shell input is **refused** with "Slash commands cannot be queued".
- `AppContainer.tsx:1386-1404` makes the same check against agent-running/tool-executing state.
- Plain text, by contrast, can be queued, or sent as a steering hint when model steering is enabled.

---

## 2. qwen-code (gemini-cli fork): differences only

**Registry**
- `packages/cli/src/services/BuiltinCommandLoader.ts:124`.
- Every built-in is stamped `source:'builtin-command'`, `modelInvocable:false` (:204-212).
- Loaders (`ui/hooks/slashCommandProcessor.ts:767-772`): McpPrompt, Builtin, **BundledSkill**, Skill, **SavedWorkflow**, File. Non-interactive and ACP use the same set (`nonInteractiveCliCommands.ts:451-458`).
- The interface (`ui/commands/types.ts:381`) adds:
  - `supportedModes` (interactive / non_interactive / acp)
  - `canRunDuringStreaming` (replaces `isSafeConcurrent`)
  - `userInvocable` / `modelInvocable`
  - `argumentHint`, `completionPriority`, `whenToUse`, `examples`, `source`/`sourceLabel`

**Renamed or changed from gemini**
- /about is now **/status** (alias `about`), with a `paths` subcommand.
- /auth gains aliases `connect`, `login` and has no signin/signout subcommands.
- /clear aliases are `reset`, `new`.
- /compress keeps only the alias `summarize`. `compact` is dropped.
- /help gains alias `?`.
- /resume gains alias `continue`. The /chat group is gone; session operations are split into separate commands (below).
- /rewind gains alias `rollback`.
- /export (`md`, `html`, `json`, `jsonl` [path]) replaces /export-session.
- /memory opens a manager dialog (`[migrate-team]`). Its subcommands are dropped.
- /mcp opens a management dialog (args `desc|nodesc|schema`).
- /extensions keeps `explore`, `manage`, `list`, `install`.
- /hooks keeps only `list`.
- /permissions means rule management. Folder trust moved to **/trust**, which appears only when folder trust is on.
- /plan toggles plan mode.
- /tools takes `[desc]`.
- /stats adds `skills`, `daily`(day), `monthly`(month), `export`.
- /update replaces /upgrade.
- /statusline replaces /footer.

**Removed** relative to gemini: /chat, /commands, /corgi, /bug-memory, /footer, /shortcuts, /policies, /privacy, /profile, /gemma, /oncall, /upgrade, /export-session.

**Added**

| Command | Purpose |
|---|---|
| /advisor `[<model>\|off\|review [focus]]` | Second-opinion reviewer model |
| /agents `manage`, `create` | Subagent management (always on, unlike gemini) |
| /approval-mode `<mode>` | Tool approval mode |
| /arena `start\|stop\|status\|select`(choose) | Several models compete on one task |
| /branch | Fork the conversation into a new session |
| /btw | Side question that leaves the main conversation alone |
| /fork `<directive>` | Background agent that inherits the conversation |
| /peers | Accept/deny messages held from other Qwen sessions; `controllers`, `revoke` |
| /cd `<path>` | Move the session to another working directory |
| /commit | Git commit with an AI-drafted message |
| /compress-fast | Compression without AI: strips old tool outputs and thinking |
| /config `<key>[=<value>]` | Get or set any setting by dot-path |
| /context [`detail`] | Context-window usage breakdown |
| /curator `status\|run\|restore` | Auto-skill lifecycle maintenance |
| /diff | Working-tree stats against HEAD |
| /delete | Delete a past session |
| /doctor `memory\|cpu-profile\|rollback` | Diagnostics |
| /effort `[low..max]` | Reasoning effort |
| /history `collapse-on-resume\|expand-on-resume\|expand-now` | Transcript collapse behaviour |
| /import-config | Import MCP servers from Claude configs |
| /language `ui\|output` | UI and output language |
| /learn `<path\|URL\|text>` | Build a skill from a source |
| /goal `[set\|edit\|pause\|resume\|clear]` | Session goal |
| /output-style | Response style |
| /remember `<text>` | Save a durable memory |
| /recap | One-line session recap |
| /reload-plugins | Reload extensions from disk |
| /rename (tag) `[--auto]` | Rename the session |
| /summary `[path]` | Write `.qwen/PROJECT_SUMMARY.md` |
| /insight | ? (description not extracted) |
| /tasks | Text dump of background tasks |
| /voice `[hold\|tap\|off\|status]` | Voice input |

**Conditional**
- /workflows: `isWorkflowsEnabled()`, i.e. `QWEN_CODE_ENABLE_WORKFLOWS` / `tools.workflowsEnabled` (:134).
- /dream, /forget: only when managed memory is available (:170).
- /lsp: `isLspEnabled()` (:201).
- /ide: interactive-only, and skipped in sandbox (:110).
- No `hidden: true` built-ins were found.

**Custom command format changes**
- User dir: `~/.qwen/commands`. Project dir: `<proj>/.qwen/commands` (core `storage.ts:221,652`).
- Scans **both `*.toml` and `*.md`** (`FileCommandLoader.ts:99-105`).
- Markdown commands use YAML frontmatter (`markdown-command-parser.ts:17`): `description`, `argument-hint`, `when_to_use`, `disable-model-invocation`. The body is the prompt.
- There is a TOML-to-MD migration tool (`command-migration-tool.ts`).
- Extensions can declare custom command paths (FileCommandLoader:201).
- Prompt, skill, file and MCP commands can be **model-invocable**, i.e. called by the model as a tool. Built-ins never are.
- Stacked skills: up to 5 leading `/skill` tokens in one input (`ui/commands/commands.ts`, `MAX_STACKED_SKILLS=5`).
- `--bare` mode skips auto-discovered commands (FileCommandLoader:83).

**Busy behaviour**
- `canRunDuringStreaming: true` makes a command run immediately (`AppContainer.tsx:3271-3279`). Opted in: status, bug, editor, help, lsp, hooks, theme, tools, settings, docs, vim, terminal-setup, tasks, voice.
- `/btw` is special-cased and admitted during streaming (:3280).
- Other slash commands are **queued, not refused**. The queue drain (`ui/hooks/useMessageQueue.ts:304-347,436-450`) batches plain messages first and runs slash entries one at a time. That drain order is inferred from the code and was not traced end to end.

---

## 3. kimi-cli (Python)

**Registry**
- Generic class `src/kimi_cli/utils/slashcmd.py:27` (`SlashCommandRegistry`, decorator `@registry.command(name=, aliases=)`). The description comes from the function docstring.
- Three registries:
  - shell-level `registry`: `ui/shell/slash.py:35`. Runs in the UI process.
  - `shell_mode_registry`: `ui/shell/slash.py:36`. The subset allowed in Ctrl-X shell mode.
  - soul-level `registry`: `soul/slash.py:34`. Runs inside the agent turn loop (`soul/kimisoul.py:725-733`).
- Merging: `KimiSoul._build_slash_commands` (`soul/kimisoul.py:854`) adds skills and flows to the soul commands.
- Dispatch: if a name is not shell-level it routes to the soul (`ui/shell/__init__.py:656-676, 785-797`).

**Built-in commands**

| Command (aliases) | Level | Purpose |
|---|---|---|
| /exit (quit) | shell, shell-mode | Exit |
| /help (h, ?) | shell, shell-mode | Help |
| /btw | shell | Side question without interrupting |
| /version | shell, shell-mode | Version |
| /model | shell | Switch model or thinking mode |
| /editor | shell, shell-mode | External editor for Ctrl-O |
| /changelog (release-notes) | shell, shell-mode | Release notes |
| /feedback | shell, shell-mode | Submit feedback |
| /new | shell | New session |
| /clear (reset) | shell **and** soul | Shell: new session (slash.py:558). Soul: clear context (soul/slash.py:80). The shell definition wins because shell lookup comes first |
| /title (rename) | shell | Set or show the session title |
| /sessions (resume) | shell | List sessions and optionally resume |
| /task | shell, shell-mode | Background task browser |
| /theme | shell, shell-mode | Dark/light |
| /web | shell | Open the Web UI |
| /vis | shell | Open the tracing visualizer |
| /upgrade | shell | Install the "Kimi Code" successor |
| /mcp | shell | Show MCP servers and tools |
| /hooks | shell, shell-mode | List hooks |
| /undo | shell | Fork at a previous turn and retry |
| /fork | shell | Copy the session to a new session |
| /export, /import | shell, shell-mode (`ui/shell/export_import.py:23,63`); soul variants too (soul/slash.py:275,303) | Export context to markdown; import from a file or session id |
| /debug | shell (`ui/shell/debug.py:147`) | Inspect the context |
| /reload | shell (`ui/shell/setup.py:207`) | Reload config |
| /login (setup), /logout | shell (`ui/shell/oauth.py:61,84`) | Platform login/logout |
| /usage (status) | shell (`ui/shell/usage.py:37`) | API usage and quota |
| /init | soul | Write AGENTS.md |
| /compact `[focus]` | soul | Compact with an optional focus |
| /yolo | soul | Toggle auto-approve |
| /afk | soul | Auto-dismiss questions and auto-approve |
| /plan `[on\|off\|view\|clear]` | soul | Plan mode |
| /add-dir `<path>` | soul | Add a workspace dir; no args lists them |

- No hidden or feature-flagged built-ins were found.
- ACP exposes **only soul-registry commands** (`acp/server.py:183`).

**(a) Custom commands**
- Skills: each `standard` or `flow` skill becomes **`/skill:<name>`**. Arguments are appended as "User request:" and the skill text is submitted as a user turn (`kimisoul.py:858-876, 918-932`).
- Flows: each flow skill also becomes **`/flow:<name>`**, run by `FlowRunner` (`kimisoul.py:878-899`).
- Skill search order (`skill/__init__.py:28-71`):
  - user: `~/.kimi/skills` > `~/.claude/skills` > `~/.codex/skills`, plus `~/.agents/skills` / `~/.config/agents/skills`
  - project: `.kimi/skills`, `.agents/skills`
  - extra: `--skills-dir`
- No TOML or markdown command-file loader exists.
- No MCP-prompts-as-commands: no `list_prompts` call was found.

**(b) Discovery**
- `SlashCommandCompleter` (`ui/shell/prompt.py:89`) uses prompt_toolkit `FuzzyCompleter` over names and aliases. An alias match displays as `/name (alias)` and inserts the canonical name.
- Unknown commands print "type "/" for all available commands".
- The Web UI has its own `/` popup (`web/src/features/chat/useSlashCommands.ts`).
- No argument hints.

**(c) While the model is busy**
- Every input during a stream goes through `classify_input` (`ui/shell/visualize/_input_router.py:31-48`):
  - `/btw` runs immediately as a side-question modal.
  - Everything else is **queued**.
- Queued shell-level commands are **refused** with the toast "/{name} is not available during streaming" (`visualize/_interactive.py:284-299`).
- Soul-level commands, skills and flows queue and drain FIFO as new turns after the current one (`ui/shell/__init__.py:905-922`).
- Ctrl+S steers immediately: it sends the buffer, or the oldest queued item, into the running turn (`_interactive.py:461-470`).

---

## 4. goose (Rust)

**Registry** (two layers)
- **CLI-local** (REPL): `crates/goose-cli/src/session/input.rs:220` (`handle_slash_command`, a string-match table), returning the `InputResult` enum (:15). Help text is at `input.rs:411`.
- **Agent-level**, shared by CLI, desktop and ACP: `crates/goose/src/agents/execute_commands.rs:27` (`COMMANDS`). Dispatch is `execute_command` (:136), called from `Agent::reply` at `agents/agent.rs:2222`.
- Unknown CLI slash text is sent as a message (input.rs:214), so the agent layer can handle it.
- Merged listing for ACP/desktop: `crates/goose/src/slash_commands/slash_command.rs:20` (`list_acp_commands`): builtins, then recipes, then skills. Builtin names are reserved against the other two.

**CLI-local commands** (input.rs)

| Command | Purpose |
|---|---|
| /exit, /quit (bare `exit`/`quit` text also works) | Exit |
| /help, /? | Help text |
| /t, /t `<light\|dark\|ansi>` | Toggle or set theme |
| /r | Toggle full tool-output display |
| /extension `<ENV=.. cmd args>` | Add a stdio MCP extension |
| /builtin `<a,b>` | Add builtin extensions |
| /prompts `[--extension <name>]` | List MCP prompts |
| /prompt `<name> [--info] [k=v...]` | Show or execute an MCP prompt |
| /mode `<auto\|approve\|chat\|...>` | Set GooseMode (variants from the enum) |
| /model `[name]` / `--provider <p> [model]` | Show or switch model or provider |
| /clear | Clear history |
| /new | Fresh session, same provider and extensions |
| /compact | Compact the conversation |
| /summarize | Deprecated alias for /compact; prints a warning |
| /edit `[text]` | Compose in $GOOSE_PROMPT_EDITOR / $VISUAL / $EDITOR |
| /skills `[names...]` | List skills, or load them by name |

**Agent-level commands** (execute_commands.rs:27-65): prompts, prompt, compact, clear, skills, **doctor** (check setup), **goal** `<desc>|off`, **grind** `<desc>|off` (pursue until max_turns), **status** (model, provider, mode, tokens).
- `goal` and `grind` start a turn immediately (`command_starts_turn`, :126; agent.rs:2238).

**Absent from this snapshot**: `/plan`, `/endplan`, `/recipe`. `grep` found no handler for them, so they were probably removed from older versions.

**Feature flag**: `GOOSE_SLASH_COMMANDS_ENABLED` (default true) gates the agent-level commands (execute_commands.rs:13; `state_machine/ops_slash_command.rs:56`).

**(a) Custom commands**
- **Recipes as commands**: the config key `slash_commands` maps `/name` to a recipe path (`slash_commands/recipe_slash_command.rs:14,40`). Set it via ACP `set_recipe_slash_command` (`acp/server/recipe/mod.rs:219`).
  - Recipe parameters map to positionals and flags (`parse_recipe_args`, :227).
  - Input hints are derived from the parameters (:116).
- **Skills as commands**: `SKILL.md` from `~/.agents/skills/`, `~/.agents/plugins/*/skills/`, `.agents/skills/` becomes `/<skill> [args]` (`skill_slash_command.rs:8-56`), with an argument hint.
- **MCP prompts**: reachable only through `/prompts` and `/prompt <name> k=v`, not as top-level `/name`.

**(b) Discovery**
- CLI: rustyline Tab completion with circular candidates (`session/completion.rs:235`). It does **prefix** matching over a static list plus `list_commands()`. /edit and /r are not in that list.
- There is argument completion for prompt names, prompt `k=` keys, /mode values, model names and skill names (`completion.rs:30-149`).
- Desktop: the `/` popover (`ui/desktop/src/components/MentionPopover.tsx:533`) lists builtins, recipes and skills via ACP `slashCommandsList_unstable` (`ui/desktop/src/acp/autocomplete.ts:58`).

**(c) While the model is busy**
- CLI: the REPL is synchronous. `input::get_input` is called only between turns (`session/mod.rs:635-640`), so there is no typeahead dispatch. Ctrl-C interrupts.
- Desktop: submissions while busy are **queued**, or interrupt-and-queue (`ChatInput.tsx:1274-1281, 1120-1150`). Slash commands get no special treatment and run when dequeued.

---

## 5. continue

**Registries**
- **IDE (core + GUI)**: `config.slashCommands` is assembled during config load:
  - `core/config/profile/doLoadConfig.ts:157-172`: invokable rules plus the built-in `Init`.
  - `core/config/profile/doLoadConfig.ts:~230-260`: MCP prompts.
  - `core/config/yaml/loadYaml.ts:218-270`: prompt files and YAML `prompts:` blocks.
  - `core/config/load.ts:172-197`: legacy `config.json`.
- **CLI (`extensions/cli`)**:
  - Metadata: `extensions/cli/src/commands/commands.ts:25` (`SYSTEM_SLASH_COMMANDS`).
  - Handlers: `extensions/cli/src/slashCommands.ts:257` (`commandHandlers`).
  - Dispatch: `slashCommands.ts:295`.

**IDE built-ins**
- **/Init**: "Initialize Codebase". Prompt-only; writes `.continue/rules/CONTINUE.md` (`core/promptFiles/initPrompt.ts:81`). It is the only always-on built-in.
- **Legacy built-ins** (`core/commands/slash/built-in-legacy/index.ts:13`) load **only** when listed under deprecated `config.json` `slashCommands` (load.ts:172-178). They run via a `run()` generator in `core/llm/streamChat.ts:51-68`. The YAML config path never loads them.

| Legacy command | Purpose |
|---|---|
| /share | Export the chat to markdown |
| /cmd | Generate a shell command |
| /http | Call an HTTP endpoint for the response |
| /commit | Commit message for current changes |
| /review | Review code |
| /onboard | Familiarize with the codebase |

**CLI built-ins** (commands.ts:25-125)

| Command | Purpose |
|---|---|
| /help | Help and keyboard shortcuts |
| /clear | Clear history |
| /update | Update the CLI |
| /info | Session info |
| /model | Model selector |
| /config | Switch config |
| /mcp | Manage MCP connections |
| /init | Create AGENTS.md |
| /compact | Summarize history |
| /resume | Resume a session |
| /fork | Forked session |
| /title, /rename | Set the session title (rename is a separate handler entry, not an alias field) |
| /exit | Exit |
| /jobs | Background jobs |
| /sessions | List sessions |
| /skills | List skills |
| /import-skill | Import a skill into `~/.continue/skills` |
| /export, /import | Session JSON |

- **Remote-mode only** (`REMOTE_MODE_SLASH_COMMANDS`, commands.ts:128): /exit, /diff (remote diff), /apply (git-apply the remote diff), handled in `ui/hooks/useChat.helpers.ts:252-305`.
- Hidden or flagged: none apart from the legacy set and remote mode.

**(a) Custom commands**
- **Prompt files** (`.prompt`/md): read from `<ws>/.continue/prompts`, `<ws>/.continue/rules`, `~/.continue/prompts`, `~/.continue/rules`. v1 `.prompts/` is also checked (`core/promptFiles/getPromptFiles.ts:41-70`; dirs in `promptFiles/index.ts:3-5`).
  - Format: YAML preamble, `---`, then the body. Preamble keys are `name`, `description`, `version`; an optional `<system>` block overrides the system message (`parsePromptFile.ts`).
  - v1 supports `{{{ input }}}` and context-provider templating (`gui/.../renderPromptv1.ts`).
- **YAML `prompts:` blocks** in config.yaml or from the Hub become slash commands (`promptBlockSlashCommand.ts`).
- **Invokable rules** (`invokable: true`) become slash commands (`ruleBlockSlashCommand.ts`; doLoadConfig.ts:157).
- Legacy `customCommands` in config.json (`customSlashCommand.ts`).
- `config.ts` slash commands (source `config-ts-slash-command`).
- **MCP prompts as commands** (IDE): each server prompt becomes `/<name>`. Content is pre-fetched with **empty args** ("TODO: handle prompt arguments", doLoadConfig.ts:~241) and rendered by `renderMcpPrompt` at send time.
- **CLI**: assistant `prompts`, invokable rules and markdown skills. A skill command makes the model load the skill via the Skills tool (slashCommands.ts:311-346). MCP prompts are **not** CLI slash commands (getAllSlashCommands, commands.ts:149).
- An unknown CLI `/x` with no substring match is sent as plain text (slashCommands.ts:349-358).

**(b) Discovery**
- IDE: the TipTap `/` dropdown filters by **prefix** (`gui/.../getSuggestion.ts:207-213`). When nothing exists it offers a "Create a prompt" action.
- CLI: the `/` popup in `UserInput.tsx:284` filters by **substring**, ranking prefix matches first (:357-365). Tab completes. /help prints shortcuts.
- No argument hints in either.

**(c) While the model is busy**
- IDE: Enter is **ignored** while streaming (`gui/.../editorConfig.ts:394`), so there is no queue.
- CLI: input, slash commands included, is **queued** while waiting or compacting (`UserInput.tsx:479-481, 587-590`). The queue lives in `stream/messageQueue.ts`. One queued item is drained when the turn finishes (`ui/hooks/useChat.ts:366-378`), and the UI queue display is cleared at that point.
  - Whether several queued items all drain is unclear: `getNextMessage` is called once per turn completion.
- CLI remote mode sends input directly, and the server queues it.

---

## Cross-agent summary

| Agent | Busy policy | Custom commands | Discovery |
|---|---|---|---|
| gemini-cli | Opt-in `isSafeConcurrent` commands run now; others refused | TOML files (`{{args}}`, `!{}`, `@{}`), MCP prompts, skills | fzf fuzzy, groups, autoExecute |
| qwen-code | Opt-in `canRunDuringStreaming` commands run now; others queued | TOML + MD (frontmatter), skills, saved workflows, MCP prompts; model-invocable | Same, plus argumentHint |
| kimi-cli | `/btw` immediate; soul commands queued; shell commands refused; Ctrl+S steer | `/skill:x`, `/flow:x` only | Fuzzy, alias-aware |
| goose | CLI: no input while busy. Desktop: queue | Recipes mapped to `/name`, skills; MCP prompts via `/prompt` | Prefix tab completion; desktop popover |
| continue | IDE: Enter blocked. CLI: queue | `.prompt` files, YAML prompts, invokable rules, MCP prompts (IDE), skills (CLI) | IDE prefix match; CLI substring match |

## Appendix D

### D. Built-in slash commands: aider, hermes-agent, mistral-vibe, gptme, open-interpreter (codex fork + classic), plandex

Source snapshots under `/Users/patrickbeam/projects/blackbird/quarantine/` (paths below are relative to that). This is a read-only survey: nothing was run.

---

## 1. aider (`aider/aider/commands.py`)

**Registry:** reflection, with no table. Any `Commands.cmd_<name>` method becomes `/<name>`, with `_` shown as `-` (`commands.py:276` `get_commands`; the comment at `:334` reads "any method called cmd_xxx becomes a command automatically"). Dispatch is `run()` at `:312`. `is_command` at `:255` accepts a leading `/` or `!`. Any unique prefix runs the command (for example `/tok` runs `/tokens`). An ambiguous prefix is reported as an error (`:300-335`). The method's docstring is the description.

| Command | Purpose (docstring) |
|---|---|
| /add | Add files to the chat so aider can edit them or review them in detail |
| /architect | Enter architect/editor mode (2 models); with a prompt, runs one message in that mode and then switches back |
| /ask | Ask about the code base without editing; with no prompt, switch to ask mode |
| /chat-mode | Switch to a new chat mode |
| /clear | Clear the chat history |
| /code | Ask for code changes; with no prompt, switch to code mode |
| /commit | Commit edits made outside the chat (message optional) |
| /context | Enter context mode (see surrounding code context); with no prompt, switch to it |
| /copy | Copy the last assistant message to the clipboard |
| /copy-context | Copy the current chat context as markdown for a web UI |
| /diff | Display the diff of changes since the last message |
| /drop | Remove files from the chat to free context |
| /edit | Alias for /editor |
| /editor | Open $EDITOR to write a prompt |
| /editor-model | Switch the Editor Model |
| /exit, /quit | Exit the application (two separate methods) |
| /git | Run a git command (output excluded from chat) |
| /help | With no args, list commands. `/help <question>` asks a help-mode coder that uses aider's docs (installs the help extras on first use) |
| /lint | Lint and fix in-chat files, or all dirty files if none are in chat |
| /load | Load and execute commands from a file |
| /ls | List known files and mark which are in the chat |
| /map | Print the current repository map |
| /map-refresh | Force a refresh of the repository map |
| /model | Switch the Main Model |
| /models | Search the list of available models |
| /multiline-mode | Toggle multiline mode (swaps Enter and Meta+Enter) |
| /ok | Alias for `/code Ok, please go ahead and make those changes.` plus any args |
| /paste | Paste an image or text from the clipboard (image name optional) |
| /read-only | Add reference-only files, or make added files read-only |
| /reasoning-effort | Set the reasoning effort level |
| /report | Report a problem by opening a GitHub issue |
| /reset | Drop all files and clear the chat history |
| /run | Run a shell command and optionally add its output to the chat (alias: `!cmd`) |
| /save | Save commands that can reconstruct the session's file set |
| /settings | Print the current settings |
| /test | Run a shell command and add its output to the chat on non-zero exit |
| /think-tokens | Set the thinking token budget (8k, 0.5M, 0 to disable) |
| /tokens | Report tokens used by the current context |
| /undo | Undo the last git commit if aider made it |
| /voice | Record and transcribe voice input |
| /weak-model | Switch the Weak Model |
| /web | Scrape a webpage, convert it to markdown, and send it in a message |

- **Custom commands:** none. No user-defined commands, no prompt files, no MCP prompts. The closest substitutes are `/load <file>` and the `--load` CLI flag (`main.py:1124`), which replay a file of slash commands, and `/save`, which writes such a file.
- **Discovery:** `/help` with no args prints every command with its docstring (`basic_help` at `:1103`). prompt_toolkit completes command names by prefix. Argument completion comes from `completions_<cmd>()` (static candidates, substring-filtered) or `completions_raw_<cmd>()` (custom completers, used for /add, /read-only, /load, /save): see `commands.py:258-274` and `io.py:155-185`. Commands with argument completion: model, add, drop, read-only, ask, code, architect, context, load, save. There are no argument hints and no palette.
- **While the model is busy:** the loop is blocking. Input is read only between turns, so commands cannot be typed mid-stream. Ctrl-C interrupts the stream (`coders/base_coder.py:1572-1579` appends "^C KeyboardInterrupt" to the conversation). The file watcher can call `io.interrupt_input()` (`io.py:516`) to break an idle prompt, but not a running generation.
- **Hidden or feature-flagged:** none. `/voice` and `/help <q>` need optional extras at runtime.

---

## 2. hermes-agent-2026-10-07

**Registry:** `hermes_cli/commands.py:83` `COMMAND_REGISTRY: list[CommandDef]`. The dataclass is at `:45`, with fields name, description, category, aliases, args_hint, subcommands, cli_only, gateway_only, gateway_config_gate, busy_policy, busy_handler, execute, argument_mode, desktop, desktop_subcommands. The module docstring says the registry is the single source for CLI help, gateway dispatch, Telegram BotCommands, Slack subcommand mapping, autocomplete, and the desktop catalog. CLI dispatch is `cli.py:1134` `_SLASH_DISPATCH`, which falls back to the `_handle_<name>_command` method convention (`:1156`). `process_command` is at `:1169`. Aliases resolve through `resolve_command` (`commands.py`, `_COMMAND_LOOKUP`).

Legend:
- Surface: `CLI` = cli_only; `GW` = gateway_only (messaging platforms); blank = both; `gate:x` = CLI-only unless config key x is truthy; `desk:` = desktop-app disposition (hidden, terminal, advanced, messaging, and so on).
- Busy (gateway `busy_policy`): `run` = dispatch mid-run; `rej` = refuse mid-run; `int+run` = interrupt the run, then dispatch.

| Cmd | Aliases | Purpose | Surface | Busy |
|---|---|---|---|---|
| /start |  | Acknowledge platform start pings without a reply | GW | run |
| /new | /reset | Start a new session (fresh session ID + history) |  | int+run |
| /topic |  | Enable or inspect Telegram DM topic sessions | GW | rej |
| /clear |  | Clear screen and start a new session | CLI desk:terminal | rej |
| /redraw |  | Force a full UI repaint (recovers from terminal drift) | CLI desk:terminal | rej |
| /history |  | Show conversation history | CLI desk:terminal | rej |
| /save |  | Export the current conversation (bare /save shows usage) |  | rej |
| /retry |  | Retry the last message (resend to agent) |  | rej |
| /prompt | /compose | Compose your next prompt in $EDITOR (markdown), then send it | CLI | rej |
| /undo |  | Back up N user turns and re-prompt (default 1) |  | rej |
| /title |  | Set a title for the current session |  | rej |
| /handoff |  | Hand off this session to a messaging platform (Telegram, Discord, etc.) | CLI | rej |
| /branch | /fork | Branch the current session (new thread on Discord/Telegram/Slack/Matrix; --here stays here) |  | rej |
| /worktree |  | Show, list, create, or prune isolated git worktrees | CLI | rej |
| /compress | /compact | Compress conversation context (add 'here [N]' to keep recent N turns; --preview shows what would happen) |  | rej |
| /rollback |  | List or restore filesystem checkpoints (restores keep your hand-edits; --all overrides) |  | rej |
| /snapshot | /snap | Create or restore state snapshots of Hermes config/state | CLI desk:terminal | rej |
| /export |  | Export a profile (config, skills, theme) to a shareable archive | CLI | rej |
| /import |  | Import a shared profile archive as a new profile | CLI | rej |
| /stop |  | Kill all running background processes |  | int+run |
| /pause |  | Pause new work globally (emergency stop); '/pause off' resumes | GW | run |
| /approve |  | Approve a pending dangerous command | GW desk:messaging | run |
| /deny |  | Deny a pending dangerous command (optionally with a reason) | GW desk:messaging | run |
| /bg |  | Run a prompt in a separate background session |  | run |
| /btw |  | Ask a side question about the current conversation without interrupting it |  | run |
| /agents | /tasks | Show active agents and running tasks |  | run |
| /journey | /learning, /memory-graph | Open the learning journey timeline | CLI | rej |
| /queue | /q | Queue a prompt for the next turn, or list/edit/rm/move/clear queued prompts |  | run |
| /steer | /s | Inject a message after the next tool call without interrupting |  | run |
| /goal |  | Set a standing goal Hermes works on across turns until achieved |  | run |
| /heartbeat | /hb | Set a recurring prompt that re-enters this session when idle |  | run |
| /refine |  | Review this conversation now and save lessons to memory/skills |  | rej |
| /review |  | Spawn an independent subagent to review the work just discussed (PR, code, docs) |  | rej |
| /loop | /proactive | Re-run a prompt on a recurring interval in this session |  | run |
| /plan |  | Write a markdown implementation plan to .hermes/plans/ without executing anything |  | rej |
| /moa |  | Run one prompt through the default Mixture of Agents preset, then restore your model |  | rej |
| /subgoal |  | Add or manage extra criteria on the active goal |  | run |
| /status |  | Show session, model, token, and context info |  | run |
| /egress |  | Show Docker egress proxy status |  | run |
| /context | /ctx | Show detailed context window view with usage gauge, category breakdown, compression stats, and throughput |  | run |
| /whoami |  | Show your slash command access (admin / user) |  | rej |
| /profile |  | Show active profile name and home directory |  | run |
| /sethome | /set-home | Set this chat as the home channel | GW desk:terminal | rej |
| /resume |  | Resume a previously-named session |  | rej |
| /sessions |  | Browse and resume previous sessions |  | rej |
| /config |  | Show current configuration | CLI desk:terminal | rej |
| /model |  | Switch model (session-scoped; --global to persist) | desk:hidden | rej |
| /codex-runtime | /codex_runtime | Toggle codex app-server runtime for OpenAI/Codex models |  | rej |
| /personality |  | Set a predefined personality |  | rej |
| /statusbar | /sb | Toggle the context/model status bar | CLI desk:terminal | rej |
| /battery |  | Toggle a color-coded battery indicator in the status bar | CLI | rej |
| /timestamps | /ts | Toggle [HH:MM] timestamps on messages and /history | CLI | rej |
| /diff |  | Show git changes in the working directory |  | rej |
| /verbose |  | Cycle tool progress display: off -> new -> all -> verbose | CLI gate:display.tool_progress_command desk:terminal | run |
| /focus |  | Toggle focus view — show only your prompt and the final response | CLI | rej |
| /footer |  | Toggle gateway runtime-metadata footer on final replies | desk:terminal | run |
| /yolo |  | Toggle YOLO mode (skip all dangerous command approvals) |  | run |
| /approvals |  | Show or set the persistent dangerous-command approval mode |  | rej |
| /reasoning |  | Manage reasoning effort and display |  | rej |
| /fast |  | Fast mode — OpenAI Priority Processing / Anthropic Fast Mode (normal/fast/auto/cold) | desk:advanced | rej |
| /skin |  | Show or change the display skin/theme | CLI | rej |
| /indicator |  | Pick the TUI busy-indicator style | CLI desk:terminal | rej |
| /voice |  | Toggle voice mode | desk:composer-voice | rej |
| /wake |  | Toggle the 'Hey Hermes' wake word listener | CLI | rej |
| /busy |  | Control how messages behave while Hermes is working | desk:terminal | run |
| /tools |  | Manage tools: /tools [list\|disable\|enable] [name...] | CLI | rej |
| /toolsets |  | List available toolsets | CLI desk:terminal | rej |
| /skills |  | Search, install, inspect, or manage skills | CLI gate:skills.write_approval | rej |
| /memory |  | Review pending memory writes / toggle the approval gate |  | rej |
| /bundles |  | List skill bundles (aliases /<name> for multiple skills) |  | rej |
| /pet |  | Toggle or adopt a petdex mascot (/pet, /pet list, /pet <slug>) | CLI | rej |
| /hatch | /generate-pet | Generate a new petdex pet from a description | CLI | rej |
| /learn |  | Learn a reusable skill from anything you describe (dirs, URLs, this chat, notes) |  | rej |
| /init |  | Generate or update AGENTS.md project instructions from a repo scan |  | rej |
| /cron |  | Manage scheduled tasks | CLI desk:terminal | rej |
| /suggestions | /suggest | Review suggested automations (accept/dismiss) |  | rej |
| /blueprint | /bp | Set up an automation from a blueprint template |  | rej |
| /curator |  | Background skill maintenance (status, run, pin, archive, list-archived) | desk:advanced | rej |
| /kanban |  | Multi-profile collaboration board (tasks, links, comments) | desk:advanced | run |
| /reload |  | Reload .env variables into the running session | CLI desk:terminal | rej |
| /reload-mcp | /reload_mcp | Reload MCP servers from config | desk:advanced | rej |
| /reload-skills | /reload_skills | Re-scan ~/.hermes/skills/ for newly installed or removed skills | desk:advanced | rej |
| /browser |  | Connect browser tools to your live Chromium-family browser via CDP, or switch to Browser Use mode | CLI | rej |
| /plugins |  | List installed plugins and their status | CLI desk:terminal | rej |
| /commands |  | Browse all commands and skills (paginated) | GW | run |
| /help |  | Show available commands (/help skills lists skill commands, /help <text> filters) |  | run |
| /palette |  | Open the fuzzy command palette (also Ctrl+P) | CLI | run |
| /restart |  | Gracefully restart the gateway after draining active runs | GW desk:terminal | run |
| /usage |  | Show token usage and rate limits; `reset` redeems a banked Codex limit reset |  | rej |
| /subscription | /upgrade | View your Nous plan and change it in the browser | CLI | rej |
| /login |  | Sign in with a Nous account (keeps your connectors) | desk:settings | run |
| /topup |  | Show your Nous balance and manage billing on the portal |  | rej |
| /insights |  | Show usage insights and analytics | desk:advanced | rej |
| /platforms | /gateway | Show gateway/messaging platform status | CLI desk:terminal | rej |
| /platform |  | Pause, resume, or list a failing gateway platform | GW | rej |
| /copy |  | Copy the last assistant response to clipboard | CLI desk:terminal | rej |
| /paste |  | Attach clipboard image from your clipboard | CLI desk:terminal | rej |
| /image |  | Attach a local image file for your next prompt | CLI desk:terminal | rej |
| /update |  | Update Hermes Agent to the latest version | desk:terminal | run |
| /version | /v | Show Hermes Agent version |  | run |
| /debug |  | Upload debug report (system info + logs) and get shareable links |  | rej |
| /quit | /exit | Exit the CLI (use --delete to also remove session history) | CLI desk:terminal | rej |

There are 102 registry entries. Subcommands are tab-completable, from the explicit `subcommands` tuple or parsed from `a|b|c` in args_hint (`SUBCOMMANDS`).

- **Hidden or flagged (from the table):**
  - GW-only (not in CLI help): /start, /topic, /pause, /approve, /deny, /sethome, /commands, /restart, /platform.
  - Config-gated onto the gateway: /verbose (`display.tool_progress_command`) and /skills (`skills.write_approval`).
  - `desk:hidden`: /model (runs in desktop but stays out of its popover).
  - Underscore aliases (/reload_mcp, /reload_skills, /codex_runtime) are omitted from gateway help (`gateway_help_lines`).
  - The `--global`/`--session` flags of /model and /reasoning are argument-level, not separate commands.
- **Custom commands.** For unregistered `/x`, the precedence is quick_commands, then plugins, then bundles, then skills, then prefix expansion (`cli.py:1210 _process_unregistered_slash`).
  - **quick_commands** (config.yaml `quick_commands:`; `cli.py:1230`): `type: exec` runs a shell snippet (shell=True, sanitized env, 30 s timeout, redacted output, never shown to the model). `type: alias` re-dispatches to another slash command with the user's args appended.
  - **Plugin commands:** `PluginContext.register_command` (`commands.py` `_iter_plugin_command_entries`); they also fire gateway `command:<name>` hooks.
  - **Skill bundles:** `/<bundle>` loads several skills at once and wins over a skill of the same name.
  - **Skills:** every uniquely resolvable SKILL.md becomes `/<skill-name>` (`agent/skill_commands.py:478 scan_skill_commands`; first claimant wins on collision, disabled skills are skipped). Invoking one queues a skill-load message as the next user turn. Skills can be stacked (`/a /b ...`, `split_stacked_skill_commands`).
  - **Prefix expansion:** a unique prefix, or the unique shortest match, expands across built-ins, skills, and bundles (`cli.py:1330`).
  - **MCP prompts** are not slash commands. They are model tools (`list_prompts`/`get_prompt`, `tools/mcp_tool_registration.py:34`).
  - `!cmd` is a bang shell (`hermes_cli/bang_shell.py`).
- **Discovery:**
  - `/help [skills|<filter>]` groups commands by category with Session subgroups (`HELP_SESSION_SUBGROUPS`) and lists quick_commands (`cli_info_mixin.py:360`).
  - `/palette` or Ctrl+P opens a fuzzy palette.
  - The prompt_toolkit `SlashCommandCompleter` (`hermes_cli/commands_completion.py:416`) prefix-completes built-ins (with description and `args_hint` usage), bundles (▣), skills (⚡), and plugins (🔌). It also completes subcommands, gives dynamic argument completion for /skin, /personality, /tools, /handoff and others, and offers fuzzy `@file` completion.
  - `SlashCommandAutoSuggest` shows ghost text.
  - An unknown command prints "did you mean" (`cli_unknown_command`).
  - Gateway: `/commands [page]` paginates commands and skills, and Telegram `set_my_commands` publishes the menu (`plugins/platforms/telegram/adapter.py:2701`).
  - Descriptions are localized (`slash.<name>.description`).
- **While busy, CLI** (`hermes_cli/cli_tui_mixin.py:1419 _tui_handle_enter`):
  - Slash and `!` input always takes the local-dispatch path and is never steered into the model. It normally goes to `_pending_input`, so it runs after the current turn.
  - Exceptions run inline on the UI thread mid-run: /model (`_should_handle_model_command_inline`), /steer and /queue, /bg and /btw (`cli_info_mixin.py:451-482`).
  - Plain text while busy follows `/busy [queue|steer|interrupt]`, which persists as `display.busy_input_mode` (default `interrupt`; `cli_tui_mixin.py:1506 _tui_enter_while_busy`). `steer` calls `agent.steer()` and falls back to queue. `interrupt` calls `agent.redirect()` when supported, else the interrupt queue. `queue` holds the text for the next turn.
- **While busy, gateway:** every resolvable command bypasses the pending queue (`should_bypass_active_session`; the rationale is that queued commands were silently discarded, issues #5057/#10370). Each command's `busy_policy` decides what happens: `dispatch` runs it; `reject` refuses with a generic "Agent is running" or a per-command text (/model, /codex-runtime, /moa: `gateway/run.py:4011 _BUSY_REJECT_TEXT`); `interrupt_then_dispatch` (/new, /reset, /stop) cancels, then runs (`gateway/run_busy.py:956 _dispatch_busy_slash_command`).
- **Uncertain:** this covers the prompt_toolkit CLI and the gateway. `tui_gateway/` and `apps/desktop` consume the same registry (`commands.catalog`), but I did not trace their busy handling.

---

## 3. mistral-vibe (`vibe/cli/commands.py`)

**Registry:** `vibe/cli/commands.py:41` `CommandRegistry._build_commands()` returns a dict of `Command(aliases, description, handler, exits, side_channel, is_available)`. `parse_command` (`:286`) resolves exact aliases only (case-insensitive). There is no prefix matching. Bare-word aliases such as `exit` or `quit` match only when typed with no arguments.

| Command | Aliases | Purpose | Side-channel (runs while busy) |
|---|---|---|---|
| /help | | Show help (keyboard shortcuts plus the command list) | yes |
| /config | | Edit config settings | |
| /model | | Select the active model | |
| /skills | | Browse, import, and manage skills. **Flag:** only when `registry_skills_enabled` | |
| /thinking | | Select the thinking level | |
| /reload | | Reload config, agent instructions, and skills from disk | |
| /clear | /new | Start a new conversation; an optional prompt seeds it | |
| /copy | | Copy the last agent message to the clipboard | yes |
| /paste-image | | Paste an image from the OS clipboard. **Flag:** only on the supported OS (`CLIPBOARD_IMAGE_PASTE_SUPPORTED_SYSTEM`) | yes |
| /log | | Show the path of the current interaction log | yes |
| /log-level | | Change the log level for the session, or persist it to config.toml | |
| /debug | | Toggle the debug console | yes |
| /compact | | Summarize history; optional guidance for the summary | |
| /exit | exit, quit, :q, :quit | Exit | yes |
| /status | | Agent statistics | yes |
| /whoami | | Mistral signed-in user, workspace, plan | yes |
| /teleport | | Teleport the session to Vibe Code Web | |
| /remote-project | | Select the Vibe Code Web project for this repo | |
| /proxy-setup | | Configure proxy and SSL certificate settings | |
| /resume | /continue | Browse, resume, or delete saved sessions | |
| /rename | | Rename the session | yes |
| /mcp | /connectors | List MCP servers and connectors; a name lists its tools; subcommands add, status, login, logout | |
| /plugins | | List the session's plugins. **Flag:** `experimental_harness` | |
| /reload-plugins | | Re-pin plugins and report changes. **Flag:** `experimental_harness` | |
| /todo | | Show the todo list. **Flag:** `experimental_harness` | |
| /voice | | Configure voice settings | |
| /leanstall | | Install the Lean 4 agent (leanstral) | |
| /unleanstall | | Uninstall the Lean 4 agent | |
| /rewind | | Rewind to a previous message (also Esc Esc) | |
| /branch | | Fork into a new resumable session, leaving this one unchanged | |
| /retry | | Continue an interrupted response; optional extra instructions | |
| /loop | | Recurring prompt: `/loop <interval> <prompt>`, `list`, `cancel <id\|all>` | |
| /data-retention | | Show data retention information | yes |
| /theme | | Select the theme | |

- **Hidden or flagged:** /skills (`registry_skills_enabled`); /plugins, /reload-plugins, /todo (`experimental_harness`); /paste-image (OS check). The constructor also accepts `excluded_commands` (`:32`), but the TUI never passes it (`app.py:744,1069`), so this looks unused.
- **Other surfaces:**
  - The ACP adapter has its own smaller registry (`vibe/acp/commands/registry.py:50`): help, compact, reload, log, mcp, teleport, proxy-setup, retry, leanstall, unleanstall, data-retention.
  - `vibe/cli-rust/src/commands/commands.rs:24` is a proof-of-concept ratatui rewrite. It mirrors the Python list minus /skills, /paste-image, /remote-project, /branch, /plugins, /reload-plugins, /todo.
- **Custom commands:**
  - Skills with `user_invocable: true` (the default; SKILL.md frontmatter; project `.vibe/` and user skill dirs, `vibe/core/skills/manager.py:96`) are invoked as `/<skill-name> [extra instructions]` (`manager.py:384 parse_skill_command`; dispatched in `core/agent_loop/_loop.py:2304`). `user_invocable: false` hides a skill from the slash menu, and the built-in `vibe` skill is model-only. The built-in skill-creator skill is user-invocable.
  - Plugins (Claude-Code-, Kimi-, and native-format compatibility in `vibe/core/plugins/`) contribute skills, not new built-in commands.
  - There are no prompt-file commands. I found no MCP prompts as commands (grep found no `list_prompts`).
- **Discovery:**
  - The completion popup (`widgets/chat_input/container.py:96,111`) lists every alias of every command plus skill entries, with descriptions.
  - Matching is fuzzy (`vibe/cli/autocompletion/completers.py:54` `_fuzzy_filter` with `fuzzy.py`).
  - Inline skill completion is separate (`inline_skill_completion.py`).
  - `/help` renders shortcuts and all aliases (`commands.py:303 get_help_text`).
  - No argument hints beyond the description text.
- **While busy** (`vibe/cli/textual_ui/app.py:1862-1866`):
  - Commands with `side_channel=True` run immediately through `SideChannelController`, one at a time. A second one gets "A slash command is already running" (`app.py:1991-2004`).
  - Any other slash command is **rejected** with "Slash commands cannot be queued", and the text is restored to the input (`app.py:2052`).
  - Skill invocations and plain prompts are **queued**, and the queue can be edited with Up/Down and Enter (`app.py:2053-2063`).
  - `!bash` and /teleport are rejected.
  - Enter on an empty input steers queued messages into the running turn (`app.py:1844`).
  - The built-in `vibe` skill text documents the same rules (`vibe/core/skills/builtins/vibe.py:1030-1040`), and adds that config-persisting pickers (theme, model, thinking, voice, proxy) require idle.

---

## 4. gptme (`gptme/gptme/commands/` package)

**Registry:** `commands/base.py:42` holds the global `_command_registry`. Commands register through the `@command(name, aliases, completer, auto_undo)` decorator (`:54`) or `register_command(..., owner_tool=)` (`:102`). Dispatch is `handle_cmd` (`:266`). `is_message_command` (`util/content.py:1`) treats `/x` as a command only if the first word has exactly one slash, so `/tmp/foo` is a path. With `auto_undo=True` (the default), the command message is removed from the log before the handler runs. The help descriptions table is `commands/meta.py:43 action_descriptions`.

| Command | Aliases | Purpose | File |
|---|---|---|---|
| /account | /creds | Account status and provider onboarding | account.py:178 |
| /backtrack | | Rewind the conversation to a checkpoint or message index | backtrack.py:65 |
| /checkpoint | | Manage workspace checkpoints | checkpoint.py:43 |
| /summarize | | Summarize the conversation | export.py:15 |
| /replay | | Replay the conversation or specific tool operations | export.py:49 |
| /export | | Export the conversation as HTML | export.py:162 |
| /model | | Show or switch the current model | llm.py:106 |
| /models | | List available models | llm.py:153 |
| /tools | | Show tools, or load new ones mid-conversation | llm.py:188 |
| /context | | Context token usage breakdown | llm.py:279 |
| /tokens | /cost | Token usage and costs | llm.py:392 |
| /impersonate | | Write an assistant message yourself, then execute its tool blocks (auto_undo=False) | meta.py:75 |
| /setup | | Set up completions, config, project | meta.py:87 |
| /doctor | | System diagnostics | meta.py:95 |
| /help | | Show help | meta.py:103 |
| /plugin | | Manage plugins: list, info, install status | meta.py:142 |
| /rag | | Search the RAG index and inject the top results (auto_undo=False) | rag.py:28 |
| /log | | Show the conversation log | session.py:27 |
| /rename | | Rename the conversation | session.py:41 |
| /fork | | Fork (copy) the conversation | session.py:66 |
| /delete | /rm | Delete a conversation by ID | session.py:103 |
| /edit | | Edit previous messages in $EDITOR | session.py:159 |
| /undo | | Undo the last action(s) | session.py:165 |
| /clear | /cls | Clear the terminal screen | session.py:174 |
| /exit | | Exit | session.py:181 |
| /restart | | Restart gptme, or reopen in another interface (`cli\|tui\|web`) | session.py:201 |
| /snapshot | | List, diff, restore, or prune workspace auto-snapshots | snapshot.py:157 |

**Tool-owned commands.** These are registered only while the owning tool is loaded in the session (`tools/base.py:678`; `_command_enabled_in_session`); otherwise /x reports "unavailable because tool ... is not enabled".
- `/commit`: autocommit tool, `tools/autocommit.py:172`.
- `/compact`: autocompact tool, `tools/autocompact/hook.py:453`.
- `/pre-commit`: precommit tool, `tools/precommit.py:395`.
- `/lesson`: lessons tool, `tools/lessons.py:504`. `lessons/commands.py:357` also registers `/lesson` and `/skills`.
- MCP tool, `tools/mcp.py:519`. Its commands are registered under multi-word keys (`"mcp search"`, `"mcp info"`, `list`, `load`, `unload`, `resources list|read`, `templates list`, `prompts list|get`, `roots list|add|remove`). **Uncertain:** `handle_cmd` looks up only the first token, so `/mcp search ...` would miss these keys unless something else routes it. I did not confirm how they are reached.

**TUI-local commands** (`tui/app.py:900 TUI_COMMANDS`): `/quit` (also `/q`), `/display <thinking|outputs|hidden|highlight> <on|off>`. The TUI refuses `/edit` (`UNSUPPORTED_COMMANDS`).

- **Custom commands:**
  - **Skills as commands:** every lesson-index skill becomes `/skill:<name>`, plus a bare `/<name>` alias when it collides with neither a command nor a loaded tool (`lessons/skill_commands.py:144 register_skill_commands`, called from `init.py:122`).
  - **Plugins:** `Plugin.register_commands` (`commands/__init__.py:76 init_commands`; `plugins/plugin.py:65`).
  - **Tool fallback:** an unknown `/name args` is tried as a direct tool invocation, `ToolUse(name, [], full_args)`, when that tool is runnable (`base.py:300-309`), so `/shell ls` or `/ipython ...` run the tool directly.
  - **MCP prompts** are reachable only through the MCP tool's `prompts list|get` subcommands (see the uncertainty above), not as first-class slash commands.
  - No user prompt-file commands.
- **Discovery:**
  - `/help` uses `action_descriptions` plus handler docstrings (`get_commands_with_descriptions`, `base.py:316`).
  - Per-command argument completers are registered with `completer=` (log, rename, fork, delete, restart, replay, backtrack, model, tools, plugin); see `get_command_completer`.
  - The TUI tab-completes command names by prefix and reuses the CLI completers for arguments (`tui/app.py:909 complete_input`).
  - No palette, no fuzzy matching.
- **While busy:**
  - The CLI is blocking: input is read between turns, and Ctrl-C interrupts generation (`chat.py:482,576`).
  - The Textual TUI keeps input live (`tui/app.py:1-7`). Plain prompts submitted while generating are queued (`_enqueue_prompt`, `:2028-2029`). Slash commands go straight to `_handle_command` and are **refused** with "Commands can't run while the agent is working; retry when idle". The exceptions are `/quit` and `/display`, which run immediately (`tui/app.py:2052-2070`). `/restart` also refuses while busy (`:2332`).
  - A durable `prompt-queue.jsonl` (`prompt_queue.py`) backs queued prompts for active conversations, for example on the server.

---

## 5. open-interpreter

### 5a. `open-interpreter/` is now a Codex fork
`FORK_BRANDING.md` describes it as "Open Interpreter inherits internal crate, protocol, and compatibility names from OpenAI Codex". Branding is centralized in `codex-rs/product-info` (`Product::{Codex, OpenInterpreter}`, selected by `codex-package.json` `variant: "open-interpreter"`; the executables are `interpreter` and `i`). I compared `open-interpreter/codex-rs/tui/src/slash_command.rs` (403 lines) with upstream `codex/codex-rs/tui/src/slash_command.rs` (366 lines). These are the **only** differences:

- **New command `/harness`** (`slash_command.rs:16`, description at `:159`): "choose the tool harness for the current model". It is listed under `available_during_task() == true` (`:303`), so it is allowed mid-task. It is backed by `InterpreterHarness` app-server calls (`app_event.rs:1235 LoadHarnesses`, `app/event_dispatch.rs:2187`).
- `/model` description changed to "choose provider, model, reasoning effort, and harness".
- `/mcp` description changed to "list configured MCP tools; use /mcp verbose for details". The upstream text mentions `/mcp login <name>`, which the fork drops; I did not check whether the login subcommand itself was removed.
- `/copy`: "copy the last response, code block, or quote" (upstream: "...or part of it").
- `/import`: "...from another coding agent" (upstream: "from Claude Code").
- `/init`, `/app`, `/quit`/`/exit`, `/skills`, `/permissions`, `/logout`: descriptions now switch on `Product::current()` and say "Open Interpreter" instead of "Codex". `/daemon` gained a trailing period.
- `bottom_pane/slash_commands.rs`: the only change is a new test asserting that `personality` is neither advertised nor parsed. Upstream's enum does not have it either, so this changes no behavior.
- `bottom_pane/command_popup.rs` is identical.

Everything else (the enum set, aliases, visibility, feature-flag gating, side-conversation and busy rules) matches the upstream snapshot.

### 5b. Classic Python interpreter (`open-interpreter-python/interpreter/terminal_interface/magic_commands.py`)
These are not slash commands. They are `%`-prefixed magics, dispatched by `handle_magic_command` (`magic_commands.py:314`) through a `switch` dict (`:323`). Interactive mode only (`terminal_interface.py:121`).

| Magic | Purpose |
|---|---|
| `%% <cmd>` | Run a command in the system shell (through `computer.run("shell")`) |
| %verbose [true\|false] | Toggle verbose mode |
| %debug | Deprecated: prints a rename notice and maps to %verbose (`:342`). `handle_debug` still exists, but dispatch remaps before reaching it |
| %auto_run [true\|false] | Toggle running code without confirmation |
| %reset | Reset the session |
| %undo | Remove the last user message and its response |
| %save_message [path] | Save messages to JSON (default `messages.json`) |
| %load_message [path] | Load messages from JSON |
| %tokens [prompt] | EXPERIMENTAL: estimate the next request's tokens and cost |
| %info | System and interpreter info |
| %jupyter | Export the conversation to a Jupyter notebook |
| %markdown [path] | Export the conversation to Markdown |
| %help | List magics (static dict at `:49-62`) |

An unknown magic goes to `default_handle`, which shows help. There are no custom magics, no completion, and no palette. Busy behavior is blocking: Ctrl-C cancels the LLM or code run and returns to the `> ` prompt (`terminal_interface.py:527-535`).

---

## 6. plandex (Go, `plandex/app/cli`)

**The prefix is a backslash (`\`), not `/`.** REPL-native commands live in `lib/repl.go:39` `ReplCmdAliases` and are parsed in `cmd/repl.go:919-1010` (`parseCommand`). The rest of the command set is the CLI table `term/help.go:19` `CliCommands []CmdConfig{Cmd, Alias, Desc, Repl}`: every entry with `Repl: true` becomes a `\<cmd>` REPL suggestion (`cmd/repl.go:45-61`). It executes by **spawning the plandex binary as a child process** (`lib.ExecPlandexCommandWithParams`, `lib/repl.go:152`), so REPL commands mirror the cobra CLI subcommands one-to-one. Typing `plandex <args>` or `pdx <args>` in the REPL does the same (`cmd/repl.go:328`).

REPL-native commands:

| Cmd | Alias | Purpose |
|---|---|---|
| \chat | \ch | Switch to chat mode (talk, no changes) |
| \tell | \t | Switch to tell mode (implementation) |
| \multi | \m | Toggle multi-line mode |
| \send | \s | Send the current multi-line prompt |
| \run <file> | \r | Send a file's contents through tell or chat, depending on mode |
| \quit | \q | Exit the REPL |
| \help | \h | REPL info and command list |

CLI-mirrored commands (`Repl: true`; alias in parentheses):
- **Plans:** \new (with `--full|--semi|--plus|--basic|--none` auto-mode and `--daily|--reasoning|--strong|--cheap|--oss|--gemini-planner|--o3-planner|--r1-planner|--perplexity-planner|--opus-planner` model-pack variants), \plans (\pl), \plans --archived, \cd, \current (\cu), \rename, \delete-plan (\dp), \archive (\arc), \unarchive (\unarc).
- **Config:** \config, \config default, \set-config, \set-config default, \set-auto [none|basic|plus|semi|full|default].
- **Context:** \load (\l), \ls, \rm, \clear, \update (\u), \show.
- **Changes:** \diff, \diff --ui, \summary, \apply (\ap), \reject (\rj), \log, \rewind (\rw), \continue (\c), \debug (\db: rerun a command and auto-apply fixes until it passes), \build (\b), \convo.
- **Branches:** \branches (\br), \checkout (\co), \delete-branch (\dlb).
- **Models:** \models [default|available [--custom]|custom], \providers [--custom], \model-packs [--custom|show], \set-model [default|daily|reasoning|strong|cheap|oss|gemini-planner|o3-planner|r1-planner|perplexity-planner|opus-planner].
- **Streams:** \ps, \stop, \connect (\conn).
- **Org and account:** \sign-in, \invite, \revoke, \users, \connect-claude, \disconnect-claude, \claude-status.
- **Billing:** \usage [--today|--month|--plan|--log], \billing.

- **Hidden:** `CliCommands` entries with `Repl: false` (`tell`, `chat`, `diff --plain`, `convo 1`, `convo 2-5`, `convo --plain`) are not suggested in the REPL. The commented-out `--full`/`--daily` REPL-start flags at `term/help.go:22-31` are dead.
- **Custom commands:** none. There are no user, skill, or MCP commands. `@path` in the REPL runs `load <paths> -r` (`cmd/repl.go:353-395`).
- **Discovery:**
  - Typing `\` lists suggestions (go-prompt) with descriptions and aliases.
  - Ranking is fuzzy plus prefix (`prompt.FilterFuzzy` and `FilterHasPrefix`, `cmd/repl.go:650-766`), with REPL-native commands pinned first and an exact alias match promoted.
  - `\load` and `\run` complete paths.
  - A bare word that resembles a command prompts "did you mean" before sending it as a prompt (`findSimilarCommands`, `cmd/repl.go:421-460`).
  - `\help` and the welcome banner list the key commands (`cmd/repl.go:778-916`).
- **While busy:** the REPL blocks on the child process, so no commands can be typed during a run. The streaming TUI has single keys instead: `s` stop, `b` background, `ctrl+c` quit, `j`/`k` scroll (`stream_tui/model.go:140-161`). After `b`, the plan keeps streaming server-side and can be managed with `\ps`, `\connect`, and `\stop`.

## Appendix E

### Part E: built-in slash commands (letta-code, forgecode, vtcode, cline, roo-code)

Source root: `/Users/patrickbeam/projects/blackbird/quarantine/`. Read-only grep/read survey. Paths below are relative to each agent's directory.

---

## 1. letta-code (TS, Ink TUI)

**Registry:** `src/cli/commands/registry.ts:50` (`export const commands: Record<string, Command>`). Each entry has `desc`, `handler`, optional `args` hint, `hidden`, `order` (sort key for autocomplete) and `noArgs` (`registry.ts:26-33`). Many handlers only return a placeholder string ("Handled specially in App.tsx"). The real work happens in `src/cli/app/use-submit-handler.ts`.

Visible built-ins, in autocomplete order:

| Command | Purpose | Notes |
|---|---|---|
| /agents | Browse agents (pinned, Letta Code, all) | hidden aliases /pinned, /profiles |
| /model | Switch model | |
| /init | Initialize or re-init the agent's memory | |
| /doctor | Investigate an agent problem in this conversation | |
| /memory | View the agent's memory | |
| /search | Search messages across all agents `[query]` | |
| /sleeptime | Configure sleep-time reflection trigger and merge | |
| /compaction | Configure compaction mode | |
| /context-limit | Set or reset the max context window | hidden alias /set-max-context |
| /palace | Open the Memory Palace in a browser | |
| /connect | Connect LLM API keys | |
| /clear | Clear in-context messages | |
| /chdir `<path>` | Change the TUI working directory | hidden alias /cd |
| /resume | Resume a previous conversation | |
| /new | New conversation, agent memory kept | |
| /fork | Fork the current conversation | |
| /btw `<question>` | Fork and ask a side question | |
| /pin, /unpin | Pin or unpin the agent | the menu shows only the applicable one (`SlashCommandAutocomplete.tsx:141-152`) |
| /rename agent\|convo | Rename the agent or conversation | |
| /description | Set the agent description | |
| /toolset | Switch toolset | replaces /link and /unlink |
| /experiments | Toggle experiments | |
| /reload | Reload settings and local mods | |
| /mods | Run mod learning targets | |
| /memfs [enable\|sync\|reset] | Filesystem-backed memory | |
| /skills | Browse skills | |
| /ade | Open the agent in ADE (browser) | |
| /skill-creator [desc] | Enter skill creation mode | |
| /system | Switch system prompt | |
| /personality | Switch personality | |
| /subagents | Manage custom subagents | |
| /mcp | Manage MCP servers, including OAuth | |
| /secret | Manage secrets for shell commands | |
| /memory-repository | Push the memory repo to another git remote | |
| /usage | Session usage and balance | |
| /context | Context window usage | |
| /recompile | Recompile the agent and conversation (evicts the cache) | |
| /feedback | Send feedback | |
| /hooks | Manage hooks | |
| /statusline | Customize the statusline | |
| /title | Configure the terminal title | |
| /reasoning-tab on\|off\|status | Toggle the Tab shortcut for reasoning tiers | |
| /system-reminders | Show or hide system reminders | |
| /terminal [--revert] | Install the Shift+Enter keybinding in VS Code, Cursor or Windsurf | |
| /install-github-app | Set up the GitHub Action | |
| /bg | Show background shell processes | |
| /workflows | Workflow runs, agents and token usage | |
| /exit | Exit | bare `exit` and `quit` also map here (`command-routing.ts:53`) |
| /login, /logout | Sign in; clear credentials and exit | |
| /dream | Reflect on memory | alias for /reflect |
| /reflect [--recent N\|--conversation ID\|--auto] [--instruction] | Launch reflection | hidden alias /reflection |
| /reflect-arena | **Experimental** blind A/B comparison of reflection models | visible, no flag gate found |
| /compact [all\|sliding_window\|self_compact_all\|self_compact_sliding_window] | Summarize history | has no `order`, so it sorts last |

**Hidden but working** (`hidden: true`): /help (described as redundant with autocomplete), /clear-messages (destructive full reset), /stream (toggle token streaming), /link and /unlink (deprecated), /cd, /reflection, /set-max-context, /pinned, /profiles.

**Possibly stale:** `command-routing.ts:4-24` lists /conversations and /profile as interactive, but the registry has no entries for them.

**Other surface:** chat-channel adapters (Slack, Discord, Telegram) have their own small command set in `src/channels/commands.ts:~460`: help, whoami, status, pause, resume, cancel, chat, feedback, detach, model, new, reflect/reflection, reload.

- **(a) Custom commands:**
  - `.md` files in `.commands/` (project) and `~/.letta/commands/` (user). Project files win, and subdirectories act as namespaces (`src/cli/commands/custom.ts:1-90`).
  - Frontmatter supplies `description` and `argument-hint`.
  - The body supports `$ARGUMENTS`, `$1`..`$9` and inline shell via `` !`cmd` `` (`custom.ts:~178-200`).
  - User-invocable skills resolve as `/skill-id`, with bundled-skill aliases listed in `SlashCommandAutocomplete.tsx:19-27` and the fallback at `use-submit-handler.ts:3474`.
  - Local mods can register commands that carry a `runWhenBusy` flag (`src/mods/types.ts`).
  - No MCP-prompts-as-commands path was found.
- **(b) Discovery:**
  - The popup opens on `/`, sorted by `order`, with a cap of 8 rows.
  - Matching is a case-insensitive **substring** on the name, not fuzzy (`SlashCommandAutocomplete.tsx:226-229`).
  - Descriptions carry argument hints. Custom, mod and skill entries are tagged "(mod)" or "(… skill)".
  - Custom commands shadow built-ins of the same name.
- **(c) While busy** (`use-submit-handler.ts:728-760`, `command-routing.ts`):
  - **Run immediately:** "interactive" commands (/model, /memory, /agents, /mcp, /resume, /search …), which open overlays and defer any change they make until end_turn. Also "non-state" commands (/bg, /btw, /usage, /exit, /reload, /reflect …).
  - **Refused**, with "'/x' is disabled while the agent is running": every other slash command, **including custom commands**.
  - **Mod commands** run only when they set `runWhenBusy`.
  - Plain messages are queued.

---

## 2. forgecode (Rust, readline REPL plus zsh plugin)

**Registry:**
- `crates/forge_main/src/model.rs:411`: `enum AppCommand`, a clap `Subcommand`. Names and aliases come from `#[command(name/alias)]`; descriptions come from `strum(props(usage))`.
- Parser at `model.rs:288`. Both `/` and `:` work as sentinels.
- Runtime list held by `ForgeCommandManager`: built-ins, workflow/custom commands (marked ⚙) and agent commands (`model.rs:161-250`).
- Reserved-name list at `model.rs:95`.

| Command | Aliases | Purpose |
|---|---|---|
| /new | (n in zsh) | Start a new conversation |
| /compact | | Compact the context |
| /info | (i in zsh) | System information |
| /usage | | Token and request usage |
| /exit | | Exit |
| /update | | Update forge |
| /act | forge | Switch to the "forge" agent (implementation) |
| /plan | muse | Switch to the "muse" agent (planning, no edits) |
| /sage | | Switch to the "sage" agent (research) |
| /help | | Prints the command list (`ui.rs:2248`). Its usage text claims "help mode for tool questions", which looks stale |
| /dump [--html] | (d in zsh) | Save the conversation as JSON or HTML |
| /model | m | Model picker |
| /tools | t | List tools with their schemas |
| /agent | a | Interactive agent picker |
| /provider | login, provider-login | Configure a provider |
| /logout | | Log out of the provider |
| /retry | r | Retry the last command |
| /conversation [id] | conversations, c | List or switch conversations |
| /conversation-tree | ct | Nested child conversations |
| /rename `<name>` | rn | Rename the current conversation. Marked internal, so it is not listed but still parses |
| /conversation-rename [name] | | Rename any conversation interactively |
| /clone [id] | | Clone a conversation |
| /copy | | Copy the last reply to the clipboard |
| /commit [max-diff] | | AI commit message, then commit |
| /commit-preview | | Preview the AI commit message |
| /suggest `<nl>` | s | Natural language to a shell command |
| /edit [text] | ed | Write the prompt in an external editor |
| /skill | | List skills |
| /config | env, e | Show the effective config |
| /config-model | cm | Set the global model |
| /config-reload | cr, model-reset, mr | Reset session overrides |
| /reasoning-effort | re | Session reasoning effort |
| /config-reasoning-effort | cre | Global reasoning effort |
| /config-commit-model | ccm | Model used for commit messages |
| /config-suggest-model | csm | Model used for /suggest |
| /config-edit | ce | Open the config in an editor |
| /workspace-sync | sync | Sync the workspace for semantic search |
| /workspace-status | sync-status | Per-file sync status |
| /workspace-info | sync-info | Workspace information |
| /workspace-init | sync-init | Initialize a workspace without syncing |
| /index | | Index the workspace for semantic search |
| `!cmd` | | Run a native shell command, bypassing clap (`model.rs:290`) |

**Hidden or not typable:**
- `Delete` is `#[command(skip)]`, so it has no slash form.
- `Message`, `Custom`, `Shell` and `AgentSwitch` are internal (`model.rs:767`).
- **Agent commands:** `/agent-<id>` is generated for each loaded agent (description "🤖 Switch to X agent", `model.rs:197`).

**zsh plugin:** `shell-plugin/lib/dispatcher.zsh:120+` mirrors the same set as `:cmd` at the shell prompt. It adds the short forms n, i, d and an `ask` action.

- **(a) Custom commands:**
  - `.md` files with `name` and `description` frontmatter. The body is a template that may use `{{parameters}}`.
  - Locations: `~/.forge/commands/` and `./.forge/commands/`, with `resolve_command_conflicts` merging them (`crates/forge_services/src/command.rs:56-103`, `crates/forge_domain/src/env.rs:141-146`).
  - The repo's top-level `commands/github-pr-description.md` is compiled in as a default via `include_str!` (`command.rs:27-31`).
  - Custom commands run as `AppCommand::Custom` events sent to the agent (`ui.rs:2263`).
  - Agents are also switched through commands (/act, /plan, /sage, /agent-*).
  - No MCP prompts as commands.
- **(b) Discovery:**
  - The completer filters on prefix, then opens the nucleo **fuzzy** picker `forge_select` (`completer/command.rs`, `forge_select/Cargo.toml:14-15`).
  - /help prints the list. Unknown commands get "Run ':help'" (`model.rs:~390`).
  - Argument hints exist only inside the usage strings.
- **(c) While busy:**
  - The loop is sequential: prompt, then `on_command`, then prompt again (`ui.rs:397-442`).
  - No input is read while a command or turn runs. The only interaction is Ctrl+C, which cancels via `tokio::select!`.
  - So nothing is queued or steered; commands are simply unavailable until the turn ends.

---

## 3. vtcode (Rust, ratatui TUI)

**Registry:**
- `crates/codegen/vtcode-skills/src/command_skills.rs:144`: `COMMAND_SKILL_SPECS`. Every slash command is also a "command skill" (`cmd-<name>`). Each spec carries a backend: `BuiltInCommand` or `TraditionalSkill` (`/review` uses `.system/cmd-review`).
- Exposed as `SLASH_COMMANDS` in `crates/codegen/vtcode-core/src/ui/slash.rs:17`.
- Dispatch: `src/agent/runloop/slash_commands/dispatch.rs:12`, then the `match` at `builtins.rs:559-603`.

| Command | Purpose |
|---|---|
| /init [--force] | Guided workspace setup (vtcode.toml, AGENTS.md, indexing) |
| /config [memory\|permissions\|model\|ide\|tasks\|jobs\|log\|subprocess\|notify\|checkup\|path\|reset] | Settings browser and workspace panels |
| /model | Model picker, including reasoning effort. Alias **/models** |
| /mode [agent] | Switch the active agent mode |
| /theme [id] | UI theme |
| /review [instr\|--last-diff\|--target\|--file\|files] [--style] | Review the diff or files (skill-backed) |
| /files [filter] | File browser and picker |
| /copy | Copy the last reply |
| /skills [manager\|list\|search\|create\|load\|unload\|info\|use\|validate\|package\|regenerate-index] | Skills manager |
| /agent [list\|threads\|inspect\|close\|create\|edit\|delete] | Subagents and child threads |
| /status | Model, provider, workspace and tool status |
| /stop | Stop the active turn now |
| /pause | Pause at the next safe boundary |
| /resume [limit\|--all] | When idle, list archived sessions. While paused, resume the turn |
| /update [check\|install] | Self-update |
| /mcp [status\|list\|tools\|refresh\|config\|repair\|diagnose\|login\|logout] | MCP manager |
| /webmcp [...] | Opt-in browser editor bridge |
| /plugin [...] | Agent plugins manager |
| /local [status\|start\|stop\|configure\|troubleshoot] [ollama\|lmstudio\|llamacpp] | Local inference servers |
| /fork [limit] [--all] | Fork an archived session |
| /history | Command history picker (same as Ctrl+R) |
| /clear [new] | Clear the screen. `new` or `fresh` clears the conversation |
| /transcript [stats\|clear\|export] | Manage the TUI transcript snapshot |
| /compact [--instructions …] \| edit-prompt \| reset-prompt | Compact now, or manage the saved compaction prompt |
| /new | New session |
| /share [json\|markdown\|html] | Export the session |
| /rewind [turn] [conversation\|code\|both] | Restore a checkpoint |
| /redo | Undo the last rewind |
| /plan [task] | Planning workflow |
| /docs, /feedback, /donate | Support links |
| /help [command] | Slash command help |
| /exit | Exit. Bare `exit` and `help` are also handled (`slash_command_handler.rs:~38-45`) |
| /terminal-setup | Terminal configuration. **Shown only on terminals that need it** (`slash.rs:31`) |
| /statusline [instr], /title, /vim [on\|off] | Terminal UI settings |
| /login, /logout, /auth, /refresh-oauth [provider] | Provider authentication |
| /secret [...] | API keys in the OS keyring |

**Hidden aliases**, typed only and kept out of the palette (`dispatch.rs:134-161`):
- /settings and /setttings route to /config.
- /context routes to /compact.
- /permissions, /ide, /tasks, /jobs, /log, /subprocess(es), /notify, /checkup and /doctor route to `/config <section>`.

**Stale names:** /git and /continue appear in the immediate-submit list (`vtcode-ui/.../session/slash.rs:407-436`) but have no spec.

- **(a) Custom commands:**
  - Prompt templates live in `.vtcode/prompts/templates/` (workspace) and the user prompts directory under `templates/` (`vtcode-core/src/prompts/resources.rs:13-14,253`).
  - Templates expand `$ARGUMENTS`, `$1`.. and `$@` (`resources.rs:328`). The result *replaces the input buffer* (`SlashCommandOutcome::ReplaceInput`) rather than being sent.
  - Unknown `/x` is passed to the model as literal prompt text (`dispatch.rs:52`).
  - Skills run through `/skills use <name>`. Built-in commands are also registered as `cmd-*` skills (`command_skills.rs:100`).
  - No MCP-prompt commands found.
- **(b) Discovery:**
  - Slash palette. Ranking: prefix match first, then substring position, then **fuzzy** match over name and description (`slash.rs:67-146`).
  - Usage strings act as argument hints. `/help [command]` gives per-command help.
- **(c) While busy** (`vtcode-ui/.../session/events.rs:1134,1160-1240,1857`):
  - **Intercepted immediately:** /stop (Interrupt), /pause and /resume.
  - **Submitted immediately even mid-turn:** commands in `IMMEDIATE_SUBMIT_COMMANDS` (/model, /files, /status, /mcp, /skills, /new, /help, /clear, /compact …). The UI submits them at once; **unverified** whether the runloop then acts on them mid-turn.
  - **Queued, one per turn:** other slash commands. They are never batched (`inline_events/queue.rs:10`).
  - **Plain text:** Enter steers the running turn; Ctrl+Enter queues it.

---

## 4. cline (two surfaces: VS Code extension and `apps/cli` TUI)

### 4a. VS Code extension (`apps/vscode`)

**Registry:**
- `src/shared/slashCommands.ts:8`: `BASE_SLASH_COMMANDS`. `VSCODE_ONLY_COMMANDS` is empty.
- Autocomplete list assembled in `src/core/controller/slash/getAvailableSlashCommands.ts:9`.
- The prompt for /deep-planning lives in `src/sdk/builtin-slash-commands.ts:98`.

| Command | Purpose |
|---|---|
| /newtask | **Alias of /compact** in this snapshot: condense and continue. The legacy new_task tool is gone |
| /compact | Condense the context window, run as a real SDK condense RPC |
| /smol | Alias for /compact |
| /deep-planning | Expands into a four-step instruction block: investigate, ask, write implementation_plan.md, hand off |

/compact, /smol and /newtask are intercepted in the webview, but only when an exact message equals the command and a task exists (`webview-ui/.../hooks/useMessageHandlers.ts:24-26,113-130`).

**Hidden or removed:** legacy /newrule and /reportbug are "hidden until their prompt expansions are ported to the SDK runtime" (`slashCommands.ts:26-28`). No /explain-changes was found.

### 4b. CLI TUI (`apps/cli`)

**Registry:**
- `apps/cli/src/tui/commands/slash-command-registry.ts:48`: `TUI_LOCAL_COMMANDS`.
- `/team` and the runtime list come from `src/tui/interactive-welcome.ts:113-150`.
- Handlers live in `src/tui/hooks/local-command-actions.ts`.

| Command | Purpose |
|---|---|
| /settings | Modify agent configuration. Hidden alias **/config** (`visible: false`) |
| /model | Switch model or provider |
| /theme | Color theme |
| /account | View or switch the Cline account |
| /mcp | Manage MCP servers |
| /plugins | Manage plugins |
| /compact | Compact the context |
| /skills | Browse skills and workflows. Keeps the typed input |
| /fork | Named fork of the session. Visible only when forking is possible |
| /undo | Restore a checkpoint |
| /clear | New session |
| /team [prompt] | Start the task with an agent team. Rewritten in `src/main.ts` (`rewriteTeamPrompt`) |
| /history | Session history |
| /help | Help dialog with a commands list, @mentions and keys (`components/dialogs/help-dialog.tsx`) |
| /quit | Exit |

- **(a) Custom commands:**
  - **Workflows as commands.** `.md`, `.markdown` or `.txt` files under `.clinerules/workflows/`, `.cline/workflows/`, `~/Documents/Cline/Workflows` and `~/.cline/workflows` (`sdk/packages/shared/src/storage/paths.ts:~598-606`, `apps/vscode/src/core/storage/disk.ts:24`).
  - **Remote workflows** come from enterprise remote config.
  - **Toggles:** each workflow has an enable toggle (local, global, remote). The extension lists only enabled ones (`getAvailableSlashCommands.ts:26-80`).
  - **Skills** are commands too (`kind: "skill"`).
  - **Plugin commands:** plugins can register them through `api.registerCommand` (`sdk/packages/core/src/services/plugin-commands.ts:27`).
  - **Expansion:**
    - A token matched anywhere in the text (`/(^|\s)\/name(?=\s|$)/`) is wrapped as a `<user_command>` or explicit-instructions block (`apps/vscode/src/sdk/slash-command-expansion.ts:8,168`; CLI `slash-command-registry.ts:240-296`).
    - No `$ARGUMENTS` substitution was found. The remaining text simply follows the block.
    - In the CLI, skill commands pass through unexpanded when the runtime's skills tool is available.
  - **MCP prompts** are listed in the webview autocomplete as `/mcp:<server>:<prompt>` (`webview-ui/src/utils/slash-commands.ts:77`). **Uncertain:** no `prompts/get` or expansion path was found on the SDK side, so these may reach the model as literal text.
- **(b) Discovery:**
  - Webview `SlashCommandMenu`, grouped into default, custom and mcp sections. Matching is case-insensitive **prefix** (`slash-commands.ts:199`).
  - CLI autocomplete matches substrings in the name *or the description*, with name-prefix matches ranked first (`hooks/use-autocomplete.ts:62-80`). Workflows and skills appear in a separate group from system commands.
  - No argument hints.
- **(c) While busy:**
  - **Webview:**
    - /compact runs the condense RPC immediately, with no streaming check. It is suppressed only while Retry or Start New Task is offered.
    - Other text, including workflow and /deep-planning tokens, is sent as follow-up "queued/steering feedback" during streaming (`useMessageHandlers.ts:167`).
    - Server-side ordering was not traced.
  - **CLI:**
    - Enter while running submits with `delivery: "queue"`, which skips local-command interception (`use-prompt-input-controller.ts:270,438`). A typed `/model` mid-turn is therefore queued as text (workflows expanded); this is inferred from the code.
    - Local commands picked *from autocomplete* run immediately. /compact is a no-op while running (`local-command-actions.ts:54-60`).

---

## 5. roo-code (VS Code extension plus `apps/cli`)

**Registry:**
- `src/services/command/built-in-commands.ts:10`: `BUILT_IN_COMMANDS`.
- Discovery merges built-in, global and project commands (`src/services/command/commands.ts:124-142`).
- The webview list is built in `src/core/webview/webviewMessageHandler.ts:123` (`getDiscoveredCommands`), which also merges skills.

Built-in commands, which expand into prompt content:

| Command | Purpose |
|---|---|
| /init | Analyze the codebase and write concise AGENTS.md files |

**Mode switching from the slash menu:**
- The webview "/" menu lists every mode as `/<slug>`. Defaults are /architect, /code, /ask, /debug and /orchestrator (`packages/types/src/mode.ts:170-216`), plus any custom modes.
- *Selecting* one switches mode immediately and clears the input (`webview-ui/src/components/chat/ChatTextArea.tsx:320-326`).
- Typing `/code` and sending it without the menu does **not** switch mode, as far as the code shows. It is just an unmatched command token.

**CLI (`apps/cli`):**
- Adds one CLI-only global command, **/new**, which starts a new task by sending clearTask (`apps/cli/src/lib/utils/commands.ts:31`).
- Other prefix triggers: `!` mode picker (fuzzysort), `#` task history, `@` files, `?` shortcut help (`apps/cli/src/ui/components/autocomplete/triggers/*.tsx`).

**Feature-flagged:** the `run_slash_command` tool lets the model invoke commands itself. It sits behind an experiment (`src/core/tools/RunSlashCommandTool.ts:26-29`, `packages/types/src/tool.ts:45`).

- **(a) Custom commands:**
  - `.md` files in `.roo/commands/` (project) and `~/.roo/commands/` (global). Project wins over global, which wins over built-in.
  - Frontmatter: `description`, `argument-hint` and `mode`. `mode` switches to that mode when the command is used (`commands.ts:218-249`; `src/core/mentions/index.ts:137-145`).
  - **Expansion:** every `/name` token anywhere in the message that resolves to a command or a mode-available skill is replaced with "Command 'x' (see below…)". The body is appended as `<command name=…>` blocks (`mentions/index.ts:111-160,234-258`).
  - No `$ARGUMENTS` substitution; `argument-hint` is display-only.
  - Settings UI can create and delete commands (`webview-ui/src/components/settings/SlashCommandsSettings.tsx`, `CreateSlashCommandDialog.tsx`).
  - No MCP-prompt commands found.
- **(b) Discovery:**
  - The webview context menu on `/` shows two groups, "Commands" then modes, with Fzf **fuzzy** matching (`webview-ui/src/utils/context-mentions.ts:132-205`).
  - Mode entries show the mode's description; command entries show description and argument hint (`SlashCommandItemSimple.tsx`).
  - The CLI has a `?` shortcut-help menu. No /help command exists.
- **(c) While busy:**
  - **Webview:** any send while `sendingDisabled`, streaming, queue non-empty or a command is running is posted as `queueMessage` (`ChatView.tsx:592-606`). Slash expansions therefore run when the queued message is dequeued.
  - Mode switching through the menu is immediate, even while busy (inferred).
  - **CLI:** `/new` is handled before any busy check (`apps/cli/src/ui/hooks/useTaskSubmit.ts:69-89`), so it clears the task immediately. Whether input is accepted while loading was not verified.
