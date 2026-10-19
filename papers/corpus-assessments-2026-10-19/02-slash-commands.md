# Shipped slash commands against Blackbird's palette

Survey: [reports/slash-survey.md](reports/slash-survey.md). It covers 23 agents (19
lineages once the four fork pairs are merged), with each registry read from source
and cited by file:line. The Claude Code list comes from strings in the 2.1.286
binary. Blackbird's side is master: `builtin_commands` at `src/terminal.cpp:43` has
57 built-ins, plus callable-workflow aliases such as `/ultracode`. The
text-bugs branch adds `/mouse`.

## Coverage of the common commands

Counts are agents out of 23.

| Concept | Agents | Blackbird |
| --- | ---: | --- |
| exit | 22 | `/quit`, `/exit` |
| model | 22 | `/model NAME`. No picker; the name must be typed. |
| new session | 20 | `/new` |
| compact | 20 | `/compact JSON`. Everywhere else it is a bare `/compact [instructions]` that summarizes with the model. |
| resume / picker | 19 | `/sessions` picker and `/resume DIRECTORY` |
| fork / branch / tree | 18 | **missing** |
| MCP | 18 | **missing** (an MCP client proposal exists, not adopted) |
| login / logout | 18 | `/auth status\|providers` only; sign-in happens outside chat |
| help | 17 | `/help` grouped guide, `/keys`, palette |
| skills | 17 | **missing** as a concept. Lua workflows and modules fill part of the role. |
| rename | 16 | `/name` |
| status | 15 | `/session` |
| plan / mode switch | 15 | `/workflow FILE` (a different idea: it swaps the turn program) |
| effort / thinking | 15 | `/effort` |
| export / share | 15 | **missing** |
| init (write AGENTS.md) | 14 | **missing** |
| undo / rewind | 14 | **missing** for files and conversation. `/restore` restores a context entry, not work. |
| theme | 14 | **missing** |
| settings | 13 | **missing** as one surface |
| copy last response | 13 | **missing** |
| agents / subagents | 13 | `/run`, `/runs`, `/send`, `/join`, `/await`, `/stop`, `/colleague(s)` |
| cost / usage | 13 | `/stats` (bytes plus observed provider usage) |
| permissions | 12 | intentionally absent (no routine approval) |
| background tasks | 11 | `/jobs`, `/fg`, `/bg`, `/process`, `Ctrl-B` |
| reload | 10 | not needed for turns: `turn.lua` reloads every turn |
| feedback / bug | 10 | **missing** as an operator command. The model has a complaint path. |
| diff | 9 | **missing** |
| memory | 9 | **missing** |
| side question (`/btw`) | 9 | nearest is `/colleague JSON`, a single selected-context request |
| external editor | 9 | `/edit`, `Ctrl-G` |
| review | 8 | **missing** |
| goal / stop condition | 8 | **missing**. `/ultracode` is a bounded workflow, not a judged stop condition. |

## What only Blackbird ships

Participants (`/run`, `/send`, `/join`, `/await`), shared tasks (`/tasks`), retained
context (`/context`, `/originals`, `/inspect`, `/restore`), trajectory and
observation tools (`/trace`, `/variables`, `/correlate`, `/git-observe`), decision
models (`/decision`), the result lab (`/instrument`), Beads (`/beads`), and views
(`/center`, `/views`, `/jobs`). Recovered drafts (`/drafts`, `/draft N`) have no
counterpart either. This is a deep, unusual surface. The gap is the ordinary,
everyday commands listed above.

## Findings

1. **Typed JSON is the operator interface for 12 commands:** `/compact`,
   `/inspect`, `/exec`, `/run`, `/send`, `/join`, `/await`, `/process`, `/decision`,
   `/colleague`, `/git-observe`, and `/beads configure`. No surveyed agent asks the
   operator to type JSON. The 10-08 field assessment already named this ("common
   actions still require raw Lua/JSON"); the survey makes it concrete. The
   underlying operations are good, but these commands are API calls dressed as
   slash commands.
2. **`/clear` means something else here.** In Blackbird it clears the display
   only. In 7 agents it clears history, and in many more it is an alias of new
   session. Operators arriving from other agents will expect their context to be
   gone. It isn't, which is the safe direction to be wrong, but it is still a
   surprise.
3. **Four singular/plural pairs:** `/session` and `/sessions`, `/run` and `/runs`,
   `/colleague` and `/colleagues`, `/draft` and `/drafts`. Each pair does a
   different thing. `complete_command` (`src/terminal.cpp:218`) completes to the
   common prefix, which is the singular, and then lets an exact match win. So
   `/sess` Tab gives `/session`, and Tab never reaches `/sessions`; the operator
   must type the `s`. Tab then Enter lands on the singular command.
4. **No custom prompt files.** Five agents load markdown command files with
   `$ARGUMENTS` (Claude Code, OpenCode, Pi, Letta Code, Qwen), and most expose
   skills as commands. Blackbird's callable workflows are more powerful: Lua, a
   configurable prefix, and POWERWORDS matched anywhere in the text. But the
   cheapest form, "a saved prompt with an argument", has no equally cheap path.
5. **Busy handling is sound and matches the better designs.** `/help`, `/keys`,
   `/queue` and `/cancel` act immediately; everything else keeps turn-boundary
   semantics, and the palette stays usable while work runs. This is close to Claude
   Code's per-command `immediate` flag. Pi and Oh My Pi go further and steer
   prompt-like commands into the running turn. Blackbird has no operator steer
   command for its own turn; `/send` addresses participants only.
6. **Discovery is above average.** The grouped `/help`, a palette that matches
   descriptions and groups, Tab completion that never submits, and palette
   selection that only loads the composer all compare well. Codex has no `/help`
   and no argument hints. Missing, compared with Aider and Hermes: unique-prefix
   execution and ghost-text suggestions. Missing, compared with most: argument
   hints beyond a placeholder word (`JSON`, `NAME`).
7. **Absent on purpose:** permissions/approval modes (doctrine), reload (turns
   reload themselves) and theme (no theming yet). Undo/rewind is the one high-count
   absence with a real cost. Fourteen agents ship it, and Blackbird already retains
   prior and proposed file bytes in audit, so the machinery for file rewind exists.

## Candidates, smallest first

- Bare `/compact [instructions]`, keeping `/compact JSON` as the precise form.
- `/copy` and `/export`. Both are cheap, and the originals are already retained.
- A `/model` picker over known names, and argument hints that show the shape of
  the expected value.
- Rename one member of each singular/plural pair. For example, `/session` →
  `/status`, which matches 15 agents.
- `/diff` and `/rewind` for files, built from retained file prior bytes.
- Markdown prompt commands with `$ARGUMENTS`, registered through the existing
  workflow alias registry rather than a second command system.
- Operator-facing forms of `/run`, `/send` and `/compact` with positional
  arguments; JSON stays the exact form.
- `/btw` as a selected-context colleague call on the current context.
