# Identity and source inventory

## Studied source versus installed and running code

Study checkout: `/Users/patrickbeam/projects/blackbird/context/autodev-2026-10-26/continuity`, branch `work/autodev-chat-study-2026-10-27`, initial HEAD **b7c082eb2522e7dbca0c4f3d320705673edee13b**. Source citations in this study name that base unless explicitly qualified. Only papers and this checkout's JOURNAL change. No other checkout or worktree was created, no product/test/configuration code changed, and no build ran.

Observed 2026-10-27 around 04:13 UTC:

| Object | Exact observation | What it does not prove |
| --- | --- | --- |
| PATH launcher | `/Users/patrickbeam/.local/bin/blackbird`, managed-launcher v1 shell file | A shell launcher is not the loaded executable or Lua identity. |
| Managed current | `~/.local/lib/blackbird/current` → `/Users/patrickbeam/.local/lib/blackbird/releases/generation-6QI9PQx9` | Current selection does not replace an existing process. |
| Pinned executable | `generation-6QI9PQx9/build/release/blackbird`, 5,133,798 bytes; SHA256 `145be9185bcdaa509aaf3f2c2c2a958fb9e21fcdfa05c1c0f663ed642d61e1f1` | Hash identifies bytes, not source parity or qualification. |
| Packaged launcher | 3,403 bytes; SHA256 `8a07f5b795c3d96d52f8c8d8c9fc0a2b0141a536a810846e695e96abcf77c9ed` | Explicit executable overrides still win. |
| Packaged default Lua | SHA256 `856042aed9aed2e16feb37446523b4b6d477f2cd74b700edfff0e2ba97f91fe2` | A saved session can select a different absolute workflow. |
| Build cache | Release; resource root in this generation; `CMAKE_HOME_DIRECTORY` and `blackbird_SOURCE_DIR` name retired `context/sol-observations-2026-10-19` | Source directory path is not a commit/tree manifest. |
| Retired checkout index | Read-only `~/archives/blackbird-worktrees-2026-10-26/retirement.sqlite`, worktrees row names that directory, branch `refs/heads/work/sol-observations-2026-10-19`, retirement HEAD `a94d09436d00ce6f05a8399b1d46268e1d1ae38a` | Retirement HEAD is not necessarily build-time HEAD; local build-time modifications remain unknown. |
| Live operator-terminal process | `ps` showed PID4954, PPID4946, `ttys006`, generation-6QI9PQx9 native path, with native descendants4955/4956; read-only `lsof -d txt` selected its executable mapping | No interaction, native stack, session contents, focus or current task was observed. |
| Study runtime | Native60674 and descendants60675/60676 initially matched the same generation | This research session is not the operator's UI. |

The installer (`scripts/install-blackbird`) copies programs/assets and builds at a fixed generation path. Inspection found no installed SOURCE_COMMIT/SOURCE_TREE or equivalent build-source manifest. `compile_commands.json` and CMakeCache are retained/hashes recorded in [inventory.json](inventory.json), but supply build paths/commands, not source-byte certification. The default Lua bytes match `git show a94d094:programs/turn.lua`. `git diff a94d094 b7c082e -- src/terminal.cpp src/chat_view.cpp include/blackbird/terminal.hpp` is empty. Other sources differ, notably coding/tools/command_jobs/program defaults. Therefore terminal source tracing is relevant, but exact installed build-source revision remains **unobserved**. Do not promote a94d094 from a plausible source association into a proven executable manifest.

All private reproductions directly select the absolute native executable above, not PATH, current, a checkout binary, or a rebuild. Launch selects `--provider openai --model gpt-6.1-sol --effort medium`; `/lua` local workflows do not invoke inference. A new private `BLACKBIRD_AUTH_HOME` prevents use/mutation of real account selection. Existing shell auth/defaults/install and Hermes profiles are untouched.

## Primary Blackbird source map

| Source | Studied responsibilities and useful anchors |
| --- | --- |
| `src/terminal.cpp` | command table/help43–355; UI state persistence431–638; lone ESC738–754; Composer routing/feed860–1275; messages/post1540ff; new/recovered queue1856ff; save refusal1890ff; routes1965ff; launch2203–2400; incoming messages2520–2615; foreground custody2650ff; queue dispatch2700ff; status2916; keyboard handling3350ff; cancel3532; local `/queue`/`/cancel`3633ff; busy enqueue3717ff |
| `include/blackbird/terminal.hpp` | InputAction vocabulary, TerminalState, Composer; submit/cancel are distinct actions, not aliases of any key byte |
| `src/main.cpp` | owns engine/providers/settings/root; cancellation callback576ff; typed presentation callbacks650–715; `/lua` engine.turn dispatch (search `prompt.starts_with("/lua ")`); ordinary turn/control command serialization |
| `src/coding.cpp` | native call admission/outcome, process-output callback, Lua display1670ff, provider streaming4598ff, retained original/output retrieval, engine.turn program retention/evaluation; staging and successful-boundary activation |
| `src/chat_view.cpp` | typed derived blocks; bounded display trim; tool-only head/tail fold321–376; tool streams378–408; labels486ff; safe text/Markdown subset; display clipping is not audit truncation |
| `src/command_jobs.cpp`, `docs/COMMAND_JOBS.md` | resident command/native custody, foreground wait epoch, input/signal/resize and retained output; resident retirement differs from preserved output references |
| `src/command_inspector.cpp`, `src/command_center.cpp` | selected-command observations, loaded page versus whole catalog, stale/retired states and detail reading; not a universal tool-result store |
| `scripts/terminal-screen-oracle.py` | independent emitted-VT-subset parser used only to decode actual captures; not a full GUI terminal |
| `scripts/check-terminal-pty`, `tests/chat_view_test.cpp`, `tests/ui_raw_test.cpp` | existing pure-Lua interruption, queue/draft, command fold and original/raw linkage examples; source read, not suite rerun |

Anchors are navigational ranges, not an assertion every line of these large source files was read. [inventory.json](inventory.json) records exact major file identities. Source facts, direct runtime observations, prior reports and proposed requirements are labeled separately throughout.

## Governing material and historical reading

Read workspace `/Users/patrickbeam/projects/BLACKBIRD.md`, own AGENTS and README, ROOT/OVERSIGHT, campaign and exact governing brief. ROOT has no papers directory; “primary papers” resolve to `/Users/patrickbeam/projects/blackbird/papers/`, not a fabricated ROOT/papers path. Read local campaign/brief plus primary corpus assessments read-only. Read INTERACTION_THEORY, TUI_DOCTRINE, TUI_COMPONENTS, TUI_VIEWS and TUI_IMPLEMENTATION_PLAN. These deliberately distinguish design intent, qualified source slices and activation; old plan status is not current runtime evidence.

Existing research studied before any new acquisition: October7 terminal study/chat review disposition; October15 TUI synthesis and manifests; interaction-theory HCI/systems reports and source manifests; coordination reference report; TUI bug queue, testing/performance procedures; October19 corpus tool-output/slash synthesis, gaps, and selected composer report sections; October26 repository orientation. Corpus assessments are retained on `work/corpus-assessments-2026-10-19` / `9fa7e2a` and in the primary papers, not silently copied over this base. Their composer synthesis link is unfinished, not evidence of a completed design. Source claims here were checked directly for the selected references, rather than inheriting rankings such as “strongest retention” as universal facts.

The initial combined required read of JOURNAL was truncated: the file is 844,823 bytes /11,781 lines at this base. Recent campaign entries and relevant chat/TUI history were read directly. The full historical journal was then supplied, intact, to one context-only native research reader with explicit openai/gpt-6.1-sol/medium selection; its full-extent reading contribution is retained in [HISTORY_READING.md](HISTORY_READING.md). Native request872,934 bytes and observed usage177,562 input tokens support that the large request was actually processed, not merely queued. The owner read its entire returned contribution. This corrects incomplete orientation coverage through delegated reading; it does not pretend the initial truncated command was a full first read, or that every historical entry was personally reread in this context. The failed first line-oriented ingestion and its unknown last remote outcome remain recorded; no effects were replayed.

## Settlement input that must survive synthesis

Read `ROOT/composition-gate-diagnostic-01/HANDOFF.md` in full, not just its title. Composition stays at9df54fe, staged tree8db2e5af5902ffa134ae5a172e6d1c1b4447bb26, nine staged files, mandatory gate failed309/310. Source retention reaches semantic-audit fd12 F_FULLFSYNC request3013 without retained return3014/evaluation. The UI still draws Starting roughly5s later. Native stack absent; kernel versus scheduling unknown. One unchanged targeted oracle passes; it is not a fix, proof of flakiness or a new gate result. Continuity38a82ea is accepted/published source, not merged/installed here. Neither sibling was edited or merged and no source hook was retried.
