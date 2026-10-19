# Context surfaces: model, Lua and operator

Read-only source catalogue for the context-generations design (`arconaut-a1v`,
worktree `context/clm-a1v`, HEAD `f7a79b1`, 2026-10-19). Nothing was built, run or
sent to a provider. Line numbers are for this HEAD. "R" means the surface reads
model context, "W" means it changes it, and "R/W" means both. **[unconfirmed]** marks
inferences that a test or run would need to confirm.

## 0. The model that every surface shares

- **One live store per process.** `ContextStore` (`include/blackbird/context.hpp:68-157`,
  `src/context.cpp`) holds `entries_ = [{id, item}]` and `head_`, the
  current revision. It also keeps immutable originals (`originals_`, `captured_`)
  and at most one pending managed proposal (`pending_`).
  - Entry ID = `<revision hex32>.<index>`, issued by the append that captured the
    entry (`context.cpp:946-951`). Entries in one append share that revision prefix,
    and `append_of()` (`context.cpp:1155-1159`) uses it to keep response rounds whole.
- **Provider requests are stateless.** `request()` sends the full `context_.items()`
  every time, with `store:false` (`coding.cpp:4324`). Every adapter refuses
  `previous_response_id` (`responses_provider.cpp:20`, `anthropic_provider.cpp:313`,
  `openrouter.cpp:63`).
  - There is **no provider-side conversation object today**. A "provider-side
    conversation" can only mean the prefix of `input` that the provider caches and
    reasoning-binds. The only server session field is OpenRouter's optional
    `session_id` request override (`openrouter.cpp:74`).
- **There is one identity per session.** `SessionIdentity{actor, conversation, workflow}`
  (`session.hpp:13-17`) is issued once, recorded as the `session.identity` program
  record and in `session-info.bbm` (`session.cpp:100-140, 385-430`), and stays fixed
  for the session's life.
  - It flows into `DecisionEvent{actor, conversation, workflow, generation, context}`
    (`retained_events.hpp:122-125`, `coding.cpp:3316, 3351`), rageshake detail
    (`coding.cpp:5271`), `/session` (`main.cpp:971-985`) and the command-center/TUI
    action scope `(environment, conversation)` (`command_center.hpp:10-13`,
    `main.cpp:1657-1658`, `tui_action_bridge.cpp:177`).
- **"Generation" already has a meaning.** `DefinitionGenerationId` is the
  *program* generation:
  - `CodingEngine::generation_` (`coding.hpp:210`), re-issued for every turn at
    `coding.cpp:5941`;
  - the policy and tool revisions (`coding.cpp:2405, 2516`);
  - CORE_DESIGN "Definition and native generations" (`docs/CORE_DESIGN.md:222`);
  - the operation metadata `generation` field (`coding.cpp:3324`);
  - rageshake `generation` (`coding.cpp:5268`).

  `ChatView::generation_` is a display counter (`chat_view.hpp`). **A "context
  generation" needs a distinct name or field everywhere**, or the audit becomes
  ambiguous.

### Every writer of `ContextStore` (the append origin label is recorded in the packet)

| file:line | origin label | writer | content |
|---|---|---|---|
| coding.cpp:2619 | `initial` | engine construction | empty append that creates the first head |
| coding.cpp:5960 | `operator` | `turn()` with non-empty prompt (TUI/plain prompt, `/wf` invocation, station event, application objective) | `{role:user, content:prompt}` |
| coding.cpp:4803 | `provider.branch:<origin rev>` | `request()` final acceptance | whole `response.output` (reasoning, message, function_call items) |
| coding.cpp:4442 | `provider.request-state` | `request()` only when `provider_.retain_request_state()` (Anthropic only: `anthropic_provider.hpp:15`) | the developer operational-state item (CURRENT_TASKS / CONTEXT_SIZE / CONTEXT_BUDGET) |
| coding.cpp:1660 | `lua.synthetic` | Lua `blackbird.append(items)` (turn.lua tool outputs, workflows, packages, model `lua` tool) | arbitrary items, **including every `function_call_output`** |
| coding.cpp:4238-4244 | `workflow-continuation` | `drain_workflows()` | developer "Registered workflow X completed in this turn: <format_value(result)>". The result is **unbounded**. |
| coding.cpp:5157 | `workflow.protocol-repair` | `context_repair` tool | unknown-result placeholder outputs |
| coding.cpp:5909 | `stop.linkage` | interrupted/stopped turn | `stop_outputs()` placeholders (`context.cpp:1253-1273`) |
| session.cpp:597-598 | `rrc:<token>` | `SessionStore::resume` on restart continue | user "continue\n\nRestart/resume note: …" |
| context.cpp:924 | `session.successor` (+`lineage`) | `seed_successor` (`--seed-session`, backstop) | selected entries with declared source lineage |
| backstop.cpp:383-385 | `backstop.instructions` | backstop successor | developer instruction |
| main.cpp:1210-1219 | `operator.beads` | `/beads select ID` | user "Selected Beads task (external data…)" |
| application.cpp:299-301 | `application.observations` | `--application` framed host | user "Host page observations (untrusted…)" |

The publishing (non-append) writers are:

| file:line | writer | notes |
|---|---|---|
| context.cpp:1018-1076 | `edit` | CAS on `base` |
| context.cpp:1077-1108 | `restore` | implemented as an edit |
| context.cpp:1518-1685 | `publish_managed` | from `manage` immediately when no workflow is running, or from `finish_workflow` |

All three record audit packets on `ApplicationChannel::context`. Only these three
fire `ContextStore::changed` (`context.cpp:1010-1017`). **Appends never notify**, so
the badge never shows them.

## 1. Model tools about context

All tools are defined in `src/tools.cpp`, `tool_definitions()` from :400. Every
request advertises them (`request_tools()` `coding.cpp:2338-2351`, plus Lua-defined
tools). They are dispatched in `CodingEngine::call` → `operation()` → the lambda at
`coding.cpp:5123-5512`. Results return to the calling Lua. **Whether a result enters
context is decided by the workflow, not the tool.** `programs/turn.lua:27-34`, the
default `ultracode` workflow (`workflows.cpp:291-297`), `useful_turn.lua:24-27` and
`economy_pilot.lua:83-85` all append `{type:function_call_output, call_id,
output=result}` with the **whole native result**.

Shaping at creation exists only in `turn.lua:13-26`. It sets
`output_max_bytes=16384` for `exec` and for unranged `read_file`. **ultracode,
useful_turn and economy_pilot apply no default cap**, and no workflow caps any
other tool. Anthropic serializes a non-string output with `dump_json` into
`tool_result.content` (`anthropic_provider.cpp:396-408`); the OpenAI and OpenRouter
encoding is **[unconfirmed]**.

The size column uses S for the JSON size of the current context, N for its entry
count, and O for the size of all captured originals (O ≥ S, and it only grows).

| tool | def (tools.cpp) | dispatch (coding.cpp) | contract | output shape and size | appended by turn.lua? | consequence |
|---|---|---|---|---|---|---|
| `context_view` | 1223-1229 | 5502-5503 → `ContextStore::view` context.cpp:707-715 | "Inspect current editable context, base revision and stable entry IDs." No parameters, no bound. | `{base, entries:[{id,item}…]}`: ≈ S + about 45 B×N | yes, whole | **a1v.4.** Each call appends a copy of the whole context, earlier copies included, so context ≈ doubles per call. Value depth grows with each nesting against `ValueLimits.depth=128` (`value.hpp:36-40`) **[unconfirmed whether reachable]**. The CONTEXT_SIZE line steers the model to it ("context_view and context_manage compact when useful", coding.cpp:4414), and so does CONTEXT_BUDGET.md:104. |
| `context_edit` | 1230-1243 | 5504-5505 → `edit` context.cpp:1018-1076 | publish `{base, entries:[{id,item}]}` with CAS; reorder/replace/remove; originals immutable; keep active tool pairs | small outcome `{accepted, revision, current, reason}` (~150 B) | yes (small) | **The input is the problem.** The model must emit the whole candidate as function arguments, so the `function_call` item (appended by `provider.branch`) carries a second full copy of the context, at output-token cost. Publishing inside the running workflow is immediate, and fires the `edited` badge. |
| `context_manage` | 1244-1263 | 5165-5173 → `manage` context.cpp:1362-1451 | managed `select/archive/summarize/restore`; a missing base binds the invocation snapshot; staged until the workflow succeeds; one pending; pins all user/system/developer entries (context.cpp:1413-1424); whole tool groups (`whole_groups` :1173) and whole rounds (`whole_rounds` :1160) | `{accepted:false, reason:"staged", current, revision, staged:true}` or a rejection reason (~200 B) | yes (small) | Activation happens at `finish_workflow` (context.cpp:1452-1517). Summaries are fresh entries with `ancestry`. The appended suffix, including this tool's own call and output, is retained (:1606-1615). The badge shows `compacted` or `selected`. |
| `context_inspect` | 1264-1282 | 5497-5498 → `inspect` context.cpp:1686-1845 | bounded pages of `originals`/`history`/`index`; `format` `bbm2-stream` (default, unreadable by a model) or `json`; `limit` 1..65536 (default 4096); `revision` guard | `{encoding, bytes:hex, total_bytes, offset, next, revision, context_revision}`: 2×limit + ~250 B (8 KiB default, **128 KiB max**) | yes | Bounded, but hex doubles the bytes, and the default format is opaque to the model. `history` and `index` walk **every context packet in the journal** (`visit_context_records`), with no scope narrower than the session. |
| `context_originals` | 1283-1289 | 5506-5507 → `originals` context.cpp:1109-1125 | "Retrieve retained original context entries, including entries removed from presentation." No bound. | array of every captured `{id,item}`: **≈ O, the whole history**, larger than S | yes, whole | The same defect as a1v.4, but worse: it includes every earlier context_view copy and every archived entry. MANAGED_COMPACTION_SUBPLAN:155-156 keeps it "for compatibility". |
| `context_restore` | 1308-1318 | 5508-5511 → `restore` then **`view()`** | restore an original by entry ID | **the whole view** (≈ S) | yes, whole | **A second a1v.4 instance.** It is easy to miss because the tool reads as a write. Fires the `restored` badge. |
| `context_repair` | 1290-1307 | 5123-5162 | append unknown-result placeholders for calls that were already unanswered when the workflow began; needs current `base`, depth 1, no owned children | `{repaired, base, effect_outcome:"unknown", replayed:false}` (~120 B) | yes | Appends `workflow.protocol-repair` outputs. Meant for recovery Lua. |
| `context_budget` | 1173-1213 | 5163-5164 → `context_budget` coding.cpp:2472-2531, `budget_view` :2438-2471 | inspect or stage the byte policy. Default enabled: 393216/196608 (`default_context_budget` :2075-2085). Stages until the workflow succeeds. | `{revision, effective, pending, pending_revision, triggered, input_bytes, units, capability{…context_window_tokens:null…}}` (~800 B) | yes | `input_bytes` = BBM2 of `context_.items()`, the same basis as CONTEXT_SIZE. |
| `context_stats` | 1214-1222 | 5499-5501 → `CodingEngine::stats` :5571-5619 (+ `ContextStore::stats` context.cpp:716-751) | entry counts, per-entry bytes, last request bytes, usage | `{revision, entry_count, input_bytes, view_bytes, entries:[{id,item_bytes}]×N, units, maintenance_error, resident_tail_records, audit{…}, context_budget{…}, last_request_bytes, usage, …}`: **O(N)**, ~60-70 B/entry + ~1.5 KB **[estimate]** | yes | Grows with entry count; an unbounded list. `view_bytes` itself encodes the whole view (CPU cost only). |
| `rageshake` | 1154-1172 | 5174-5407 | advisory complaint captured with context revision, program generation, conversation, model and audit end; tries `bd create` | `{complaint, retained_locally, bead_created, delivery:<exec result, output ≤4096>, sink, repair_required_now}`: ~5 KB max | yes | Records `revision = context_.head()`, `conversation`, `generation` (program). The planned C4/C6 critical complaint would reuse it (PLAN.md:142-150). |
| `lua` | 1335-1351 | 5512 → `runtime_->eval(code)` | run Lua with the blackbird API | whatever the code returns; `return blackbird.context()` or `return blackbird.append(...)` returns the whole view | yes | Indirect a1v.4 paths (see §2). |
| `restart` | 1320-1334 | (restart note) | RRC after the turn | small | yes | The next process appends `rrc:<token>` "continue + note". |

Other tools whose outputs enter context with no shaping. These are out of the
a1v.4 scope, but they are inputs to "shaping at creation":

| tool | def | output |
|---|---|---|
| `read_file` | :786-842 | whole file unless `range` or `output_max_bytes` is set (16 MiB cap); turn.lua sets 16384 |
| `exec` | :935-983 | whole output unless `output_max_bytes` is set; `output_ref` lets the model retrieve the rest |
| `read_process_output` | :1021-1038 | a range |
| `audit_inspect` | :1132-1153 | pages, `limit` ≤ 65536, hex |
| `trajectory_read` | :1078-1131 | ≤ 64 rows |
| `variables_read` | :1040 | — |
| `tasks_read` | :609 | ≤ 64 rows |
| `colleague` | :544 | full remote answer text |
| `participant_read` | :404 | **[unconfirmed]** bounded |
| `workflow_registry` | :695 | full retained sources |
| `program_config` | :724 | full config, including workflow sources |
| `module_source` | :745 | full module source |
| `tool_registry` | :777 | definitions up to 64 KiB |
| `workflow_invoke` | :704 | the Lua result, and the drained continuation result as well (coding.cpp:4238) |

## 2. Lua surfaces

The bridge is installed in `Runtime::install_impl`, `coding.cpp:1004-1140`. The names
are at `:1056-1065` and the global aliases at `:1100-1101` (`blackbird` and `arco`).
Dispatch is `Runtime::bridge`, `:1582-1690`.

| Lua | line | R/W | returns | notes |
|---|---|---|---|---|
| `blackbird.context()` | 1645-1647 | R | `{base, entries}`, a full copy | the same value as `context_view` |
| `blackbird.append(items)` | 1658-1663 | W (`lua.synthetic`) | **the whole view** | **the only way turn.lua adds tool outputs**. Its return value is the whole context, so a Lua tool or a `lua` call ending in `return blackbird.append(...)` puts a whole copy into context. |
| `blackbird.edit(candidate)` | 1648-1650 | W | edit outcome | CAS; immediate even inside a workflow |
| `blackbird.restore(id)` | 1654-1657 | W | **the whole view** | |
| `blackbird.originals()` | 1651-1653 | R | every original (≈ O) | |
| `blackbird.manage(p)` | 1639-1641 | W (staged) | manage outcome | `base` is required (no snapshot convenience; that exists only in the model tool, coding.cpp:5167-5171) |
| `blackbird.inspect(q)` | 1642-1644 | R | inspect page | |
| `blackbird.stats()` | 1622-1624 | R | `CodingEngine::stats()` | used by `useful_work.lua:80`, `economy_pilot.lua:52` |
| `blackbird.request(opts)` | 1628-1632 → coding.cpp:4248-4813 | R context, W `provider.branch` (+`provider.request-state`) | provider response | Options may **override `input`, `tools`, `instructions` or `model`** (:4363-4373), so Lua can build a request from arbitrary input. The **response is still appended to the live context** (:4803). The CAS `context_.head() != origin` → conflict check is at :4467, 4794. `state_context:"omit"` drops operational state (:4358-4362). `cache_policy`, `retry_policy`. |
| `blackbird.call(name, args)` | 1633-1638 | — | tool result | does not itself append |
| `blackbird.present(item)` | 1678-1681 → coding.cpp:5664-5685 | display only | — | transcript, not context |
| `blackbird.display(text)` | 1670-1677 | display only | — | `display` original |
| `blackbird.restarting()` | 1625-1627 | — | bool | |
| `blackbird.decide(args)` | 1103 (prelude) | — | decision_model | Jev; no context access (explicit args) |
| `blackbird.colleague(q)`, `blackbird.participants.*`, `blackbird.process.*`, `blackbird.tasks.read/edit`, `blackbird.define_tool`, module `import` | 1104-1140 | — | tool results | prelude string built on `blackbird.call` |
| `blackbird.json/binary/format/array/emit` | 1086-1098, 1069 | — | — | |

### Shipped Lua

- **`programs/turn.lua`** (48 lines), the default workflow.
  - `request()` → per call: `call` → `append(function_call_output)`; message items
    go to `present`. At most 64 steps.
  - Per-tool output caps are at :13-26.
  - Lines 1, 3 and 41-43 emit authored raw sites 3880, 3881 and 3882.
- **`programs/useful_turn.lua`**
  - Appends an assistant "Useful-work boundary" item (:7-17) before its loop.
  - Applies no output caps (:24).
- **`programs/economy_pilot.lua`**
  - Appends a developer policy item and an assistant feedback item (:18-25). The
    feedback is built from `stats()` via `useful_work.lua:79-100`.
  - Applies no caps (:75).
- **`programs/useful_work.lua`**
  - `M.feedback` reads `stats().entries` (the largest 3 entries) (:79-100).
  - `M.page` reads `audit_inspect`.
- **`programs/packages.lua:187-217`**
  - `S:context` builds an "UNTRUSTED external feed data" user item.
  - `S:include(id,key,append)` requires an explicit context sink function.
  - `S:event` embeds the same text into a station prompt.
- **`programs/orchestration.lua`**, **`programs/maintenance.lua`** and
  **`packages/github_releases.lua`** have no direct context access.
- **`programs/instruments/results.lua`** uses the TUI `ui.*` API (`tui_lua.cpp:538-552`)
  over command results only, with **no context access**.

### Authored programs that act on context

- **Built-in ultracode** (`workflows.cpp:280-307`), the only default registered
  workflow.
  - It appends a pinned **developer** instruction on every invocation. Developer
    items are mandatory under `manage`, so these accumulate and cannot be
    summarized away.
  - It copies the turn.lua loop **without** turn.lua's output caps.
- **Lua-defined tools** (`tool_define`, `coding.cpp:2352-2431`;
  docs/LUA_TOOLS.md:40-46).
  - Their bodies get the full blackbird API: they can read, edit, append and
    request.
  - Their result is appended by the calling workflow like any other tool result.
- **Registered workflows** (docs/CALLABLE_WORKFLOWS.md:7-8, 41, 49-54).
  - They append instructions to context ("stay in context until normally
    edited/compacted").
  - Their completion result is appended as a developer item.
- **Operator `/lua CODE`** (`main.cpp:1221-1223`) and **`--once`** run the same
  runtime with `prompt=""`, so no user item is appended. The operator `*_call`
  commands (`operator_call` coding.cpp:5719-5736) also run a turn with an empty
  prompt, so their tool results do not enter context.

## 3. Request assembly: what the provider actually sees

All of this is in `CodingEngine::request`, `coding.cpp:4248-4813`.

- **`instructions`**: one fixed string (:4284-4311). It says "Context is ordinary
  editable data: inspect it, transform it with Lua, repair it from originals" and
  "Keep tool call/result linkage valid". It is sent as the request's `instructions`
  field, not stored in context.
- **`input`** = `context_.items()` (:4277), after `validate_protocol`
  (`coding.cpp:286-316`; no unanswered calls) and `drain_workflows()`.
- **Operational state** is a trailing developer item that is never in context,
  unless the provider is Anthropic. It has three parts:
  - **CURRENT_TASKS** (:4376-4391): the open-task page (≤ 16 rows), plus task
    guidance. It is present whenever tasks have ever been used. It "survives
    compaction" because it is rebuilt from `TaskState` on every request (docs/TASKS.md:96-99).
  - **CONTEXT_SIZE** (:4404-4416), every request (a1v C2): `input_bytes=` (BBM2 of
    the selected input), `entries=`, `trigger_bytes=` and `target_bytes=`.
  - **CONTEXT_BUDGET** (:4417-4436), only when triggered: the guidance text plus
    the formatted budget view.
- **Anthropic carry-over**: `retain_request_state()` appends the state item to
  context as `provider.request-state` (:4437-4451), and CONTEXT_SIZE then rides
  only with the trigger (:4400-4405). So **Anthropic context contains past task
  pages and trigger blocks**, and a generation cut would carry or drop them.
- `prepare_request_cache(request, {mode, …, reusable_input_items})` (:4464-4466,
  `request_cache.cpp:95-118`) marks the reusable prefix.
- `last_request_bytes_` is set, and the display line "[request N native request
  bytes]" is shown (:4476-4481).
- **Response acceptance**: CAS `context_.head()==origin`, then append the
  `response.output` to context (`provider.branch:<origin>`, :4794-4803).

## 4. Operator surfaces

### Slash commands

The palette catalogue is `terminal.cpp:42-148` (`builtin_commands`); dispatch is
in `main.cpp:740-1240`.

| command | dispatch main.cpp | R/W | output and notes |
|---|---|---|---|
| `/context` | 1124-1126 | R | **full `context.view()` JSON** to the transcript or stdout. Display only, not into context. `/context` bypasses the effort-pending gate (:758). |
| `/originals` | 1150-1153 | R | **all originals**, JSON |
| `/compact JSON` | 1141-1145 | W | `context.manage(proposal)` **immediately**: no workflow is running, so it publishes at once (`MANAGED_COMPACTION_SUBPLAN:147`). Fires the badge. `base` is required. |
| `/inspect JSON` | 1146-1149 | R | an inspect page |
| `/restore ENTRY` | 1154-1157 | W | `context.restore`; prints "Original restored." and fires the badge |
| `/stats` | 1138-1140 | R | `engine.stats()` JSON |
| `/checkpoint` | 1134-1137 | — | `context.checkpoint()` (saved semantic state) |
| `/new` | 1055-1098 | — | after `validate_session_switch` and checkpoint, re-execs with a fresh `--session` directory and the **same model/provider/effort/workflow**. **Context, tasks and identity are not carried over** (`check-new-session-pty:163-167`). |
| `/resume DIR` | 991-1054 | — | re-exec into another session |
| `/sessions` | 1127-1133 | — | `list_sessions` (session-info snapshots) |
| `/session` | 969-986 | — | prints `conversation`, `actor` and `workflow_id` |
| `/restart NOTE` | 1099-1104 | — | RRC; the next process appends `rrc:` continue |
| `/lua CODE` | 1221-1223 | R/W | full Lua API, `prompt=""` |
| `/beads select ID` | 1189-1220 | W | appends `operator.beads` user item |
| `/tasks …` | 818-841 | tasks only | `TaskState::operator_command` |
| `/trace`, `/variables`, `/correlate`, `/git-observe`, `/decision`, `/colleague(s)`, `/runs`, `/run`, `/send`, `/join`, `/await`, `/stop`, `/jobs`, `/bg`, `/fg`, `/process` | 795-965 | — | `operator_call` turns with an empty prompt; **results are not appended to context** |
| `/clear` | terminal (TUI) | display only | "Clear display, not model context" (`terminal.cpp:61`, CHAT_UX.md:48, TUI_PLAN.md:143) |
| plain text / `/wf…` / powerword | 1224-1234 → `operator_turn` coding.cpp:5737-5761 | W | appends `operator` user item, runs the workflow |
| `continue` (resume) | 809-816 | W | `SessionStore::resume` appends `rrc:` item |

### CLI flags that create or select context

These are parsed in `main.cpp:100-240`. `--help` text is at :108-121 and lists
`/context`, `/originals` and `/restore`.

- `--session DIR` / none (a fresh directory) / `--list-sessions`.
- `--seed-session JSON` (`main.cpp:220-227, 422-424` → `validate_successor_seed`
  context.cpp:782-902, `seed_successor` :903-926).
  - This is **the existing "new conversation from selected context" mechanism**,
    the closest precursor of a generation: lineage
    `{session, environment, journal, prefix_sequence, prefix_end, context_revision,
    status, reason}` plus `source_entries`.
  - It requires an empty fresh audit and a different journal.
  - Reasoning must be self-contained (encrypted or Anthropic native).
- `--backstop MISSION` (`backstop.cpp:262-390`) builds a successor audit.
  - It selects all system/developer entries plus the last user entry from the
    predecessor view (:280-300).
  - It then calls `seed_successor` and appends `backstop.instructions`.
- `--resume-continue` / `--resume-once` / `--rebuild-session` / `--audit-last` /
  `--recover-unknown-effects`. Reopen semantics are in docs/SESSION_REOPEN.md.
- `--station DIR`: events become `operator` prompts (`main.cpp:1527-1561`), and
  `station-status` carries `context_head` (:1432-1442).
- `--application`: framed host, appending `application.observations`.

### Context-change badge

- **Wiring**: `main.cpp:659-670`, TUI only. It maps `ContextChange{kind, mode,
  bytes_before, bytes_after}` to `edited`, `restored`, `compacted` (summarize or
  archive) or `selected`, then calls `TerminalUI::context_changed`
  (`terminal.cpp:1653-1657`).
- **Text**: `terminal_context_badge` (`terminal.cpp:322-334`), e.g. `◆ CONTEXT
  compacted −48 KiB`, `±0 B`, or no delta when sizes are unknown.
- **State and render**: `terminal.cpp:2197-2203` holds the badge; it blinks for
  about 2 s; it is retired when a turn launches (:2242); it is received at
  :2626-2628 and rendered at :2963-2985.
- **Sources**: `ContextStore::changed` fires only from `edit`, `restore` and
  `publish_managed` (context.cpp:1068-1074, 1673-1676), never from append or seed.
  `ContextChange` (`context.hpp:59-67`) has **no generation or provider-conversation
  field**.

### TUI transcript (ChatView) versus model context

- **ChatView is "bounded derived display, no durable state or audit authority"**
  (`chat_view.hpp:31-32`), with about 1 MiB retained. Tool blocks fold to their
  head and tail.
- **What it shows**:
  - submitted prompts (`terminal.cpp:2292`);
  - streamed assistant previews and `present` text (`coding.cpp:5664-5685`);
  - process output (`ui.process_output`);
  - notices, such as request bytes, retries, audit headroom (`coding.cpp:4549`
    "context compaction does not reclaim audit history") and "Managed context: …"
    after settlement (`coding.cpp:6004-6008`);
  - the JSON dumps of `/context` and other commands.
- **What it does not show**: reasoning, developer items, operational state, tool
  results except process streams, and summaries.
- **Reopen rehydration differs.**
  - `main.cpp:1616-1634` walks the **current `context.items()`** and restores only
    user and assistant messages (`TerminalUI::restore_message` terminal.cpp:1645).
  - So after compaction the reopened transcript shows the *compacted* presentation:
    assistant summaries appear, and archived turns vanish. Developer summaries
    never appear.
  - Under generations this would show only the newest generation, unless it is
    changed.
- **Status-line usage summary**: `ui.usage` (`main.cpp:695-708`), provider
  input and output tokens of the last request.

### Command center, inspector and instruments

These are `command_center.cpp`, `command_inspector.cpp`, `tui_inspector.cpp`,
`tui_instrument.cpp`, `tui_lua.cpp`, `task_view.cpp` and `tui_action_bridge.cpp`.

- They read **tasks, participants and command jobs**, never `ContextStore`
  (grep: no context reads).
- They are scoped by `CommandCenterScope{environment, conversation}`
  (`command_center.hpp:10-13`; wire stamp `command_center.cpp:394, 444`,
  `tui_action_bridge.cpp:68, 101, 177` rejects a stamp whose `conversation`
  differs).
- **If a generation issued a new ConversationId, the scope would split
  mid-session.**

## 5. Tasks, participants, station and other state next to context

- **`TaskState`** (`include/blackbird/tasks.hpp:17-77`, `src/tasks.cpp`) is durable
  session state in the journal, independent of `ContextStore`.
  - It is injected per request as CURRENT_TASKS (coding.cpp:4376-4391).
  - It is never derived from context.
  - It is bound to the TUI through `engine.tasks().changed` (`main.cpp:652-657`).
  - Its tools are `tasks_read`/`tasks_edit` (tools.cpp:609-694, dispatch
    coding.cpp:5492-5496) and the plain `/tasks` command.
  - **It survives compaction by construction**, so it would survive a generation
    cut too. Only Anthropic's retained copies are in context.
- **Exec and participant task bindings**: `exec task_id` and
  `participant_start.task_id` (tools.cpp:430-444) publish observed activity badges.
  The binding lives in operation inputs and audit (TASKS.md:90-95), not in context.
- **Participants and colleagues** (`colleague.cpp:195-240`, `participants.cpp:1535-1551`)
  are context-only workers.
  - Their context is **explicit `[{id,text}]` selections** authored by the caller,
    with "no automatic transcript".
  - Directions append `participant-prior-answer:<run>`.
  - They never read `ContextStore`.
  - Each has its own provider request, not a branch of the main conversation.
- **Station** (`main.cpp:1327-1600`, docs/STATION.md) uses the same engine and
  context.
  - Event prompts become `operator` user entries.
  - `station-status` exposes `context_head`, the pre-dispatch boundary
    (STATION.md:53-56).
- **Session identity and settings**: `session-info.bbm`, `list_sessions`
  (`session.cpp:100-160`) and `/session`. There is no context head or generation in
  them.
- **Saved state**: `ContextStore::checkpoint` (context.cpp:587-697) and
  `saved_state.cpp:100-150` hold the context blob. Reopen restores `entries_`,
  `head_` and pending proposals (SESSION_REOPEN.md:3).
- **Operation metadata**: each operation records `revision=context_.head()` and
  `generation` (program) (coding.cpp:3316-3325). Rageshake records the same
  (:5266-5272).

## 6. Docs that state context contracts

| doc:line | claim | status / effect of a generation design |
|---|---|---|
| CONTEXT_BUDGET.md:5-20 | default enabled 384/192 KiB; CONTEXT_SIZE every request; Anthropic gets it only with trigger | current; a generation cut is a new trigger outcome |
| CONTEXT_BUDGET.md:22-27 | "Measurement is **serialized input UTF-8 JSON bytes**"; trigger never forces shrinking | **stale today**: the code measures BBM2 native packet bytes (`coding.cpp:4396-4397`). "Never forces" would change if a harness-run compaction step or cut is added. |
| CONTEXT_BUDGET.md:84-100 | proposals activate only after the workflow succeeds | changes if a cut activates mid-turn (C6) |
| CONTEXT_BUDGET.md:104 | "Use `context_view` … to identify expendable assistant notes" | steers into a1v.4 |
| CONTEXT_BUDGET.md:125-149 | recover with bounded `context_inspect`; `arco.append` recovered facts; restore | inspect scope (all generations or one) must be defined |
| USING_BLACKBIRD.md:64-66 | "Previous user/assistant context is shown on reopening" | it is the current presentation, not the history; generation-dependent |
| USING_BLACKBIRD.md:111-112 | command list | — |
| USING_BLACKBIRD.md:186-262 | Lua API contracts: `context()` returns `{base,entries}`, `append` adds items with fresh original IDs, `stats` field list, "Preserve Responses call/result linkage", "Opaque items stay originals" | all would need a generation field or scope |
| USING_BLACKBIRD.md:191-193 | `blackbird.request` "assembles from live context" | a generation sets the prefix that is sent |
| USING_BLACKBIRD.md:296-299 | RRC: "Settings, identities and context survive" | does RRC keep the current generation? |
| USING_BLACKBIRD.md:361-383 | B1: `/compact` semantics; "**No automatic thresholds**"; "Audit/originals grow even when presentation shrinks" | "No automatic thresholds" is stale (budget on by default) and false after C6 |
| USING_BLACKBIRD.md:427-430 | rageshake captures context revision, program generation, actor/conversation/workflow | add context generation |
| USING_BLACKBIRD.md:439-470 | capacity successor via `--seed-session`: "New session and entry identities are fresh"; "Automatic handoff … [not implemented]" | a generation is an *in-session* successor: same session, a new provider prefix. The two need reconciling. |
| USING_BLACKBIRD.md:619-629 | `/new` inherits only model, effort and workflow; context, tasks and drafts are restored independently | a generation is neither `/new` nor `/resume` |
| CORE_DESIGN.md:163-220 (CLM) | one working context with immutable published revisions per participant; "Responses and tool results belong to the originating request's continuation branch, revision and generation"; "Provider assembly binds a request to a context revision, assembly-program generation, …"; "Never silently prune to fit"; each colleague has its own lineage | "generation" means program generation. Context-generation lineage is a new binding dimension. |
| CORE_DESIGN.md:222-240 | "Definition and native generations", activation timing | name collision |
| MANAGED_COMPACTION_SUBPLAN.md:22-56, 120-171 | managed modes; **all live user messages and all system/developer entries are mandatory**; default activation after the workflow; "Operator idle commands publish immediately"; "Existing unbounded generic original surface remains compatible"; crash does not activate orphan stages | the pinned set grows without bound across generations; a generation needs a rule for carrying pinned items |
| LUA_TOOLS.md:40-46, 67-69 | tool bodies have the full arco API; definitions publish at the workflow boundary with managed context | — |
| CALLABLE_WORKFLOWS.md:7-8, 41, 49-54 | instructions stay in context until edited/compacted; operator text is retained as a user entry; continuation results are appended | carried forward or not at a cut |
| TASKS.md:96-99 | task projection "survives context compaction through rereading native task state" | holds across generations |
| TOKENOMICS.md:41-43, 124-133 | successor seeds need encrypted reasoning; task/budget facts "**do not accumulate in retained context**" | **stale for Anthropic** (`provider.request-state`) |
| SESSION_REOPEN.md:3 | reopen restores context, pending proposals, settings and conversation identity | add the current generation |
| STATION.md:53-56 | station-status `context_head` | — |
| CHAT_UX.md:48, TUI_PLAN.md:143 | `/clear` is display-only | — |
| PLAN.md:172-184 (this branch) | the operator decision: generations, a new ID, grouped under the session, reasoning not carried | the design input |

## Contracts a generation design would change

1. **The tool-output contract.**
   - `context_view` (whole view) and `context_restore` (returns the whole view) are
     appended by every shipped workflow.
   - `context_originals` (all history) is appended the same way.
   - `context_edit`'s *arguments* carry a whole copy.
   - Shaping at creation must be decided per tool and per workflow. Today only
     turn.lua caps, and only exec and read_file.
   - Lua `append` and `restore` return the whole view (coding.cpp:1656, 1661).
2. **The request prefix.**
   - Today the request prefix is the whole live context, with no provider
     conversation object.
   - A generation defines a new prefix start. Reasoning items
     (`whole_rounds`, `validate_successor_seed` reasoning rules) must not cross the
     cut.
   - The Anthropic `provider.request-state` items must be carried or dropped
     deliberately.
   - The `prepare_request_cache` reusable prefix resets.
3. **Identity.**
   - `SessionIdentity.conversation` is fixed per session and scopes the command
     center and TUI action bridge. **Either it stays the session ID** and a new
     context-generation ID is added (recommended by the scope coupling), **or every
     scope consumer changes.**
   - "Generation" collides with `DefinitionGenerationId`.
4. **Audit binding.** The following carry `revision` and program `generation` but no
   context generation:
   - `DecisionEvent.context` (ContextRevisionId);
   - the operation metadata `revision`;
   - rageshake `revision`;
   - the request-record metadata `revision` (coding.cpp:4577-4583);
   - `ContextChange`.
5. **Activation timing.**
   - Managed proposals publish only at `finish_workflow`; operator `/compact`
     publishes immediately.
   - If a cut happens mid-turn (C6/C4), the "after workflow completion" text in
     CONTEXT_BUDGET.md, USING_BLACKBIRD.md and the `context_manage` and
     `context_budget` descriptions changes. The CAS checks in `request()` (:4467,
     4794) assume one stable head per request.
6. **Mandatory pins.** All user, system and developer entries are unremovable
   (context.cpp:1413-1424), including ultracode's per-invocation developer text,
   workflow-continuation results and Anthropic state items. Over many generations
   the pinned set only grows.
7. **Inspect and history scope.**
   - `context_inspect` `history` and `index` and `originals()` cover the whole
     journal.
   - Entry IDs stay valid across generations, because they are journal-scoped.
   - Restore and repair across a generation boundary need a rule.
8. **The successor seed.** `--seed-session` and backstop already implement "start
   from selected context with lineage", but only into a *new audit*. Generations put
   that inside one session. Validation rules can be reused; "fresh session and entry
   identities" would not apply.
9. **Operator presentation.**
   - Reopen rehydration shows the current presentation only.
   - The badge has no "new generation" kind.
   - Appends are invisible to the badge.
   - `/session`, `/stats` and session-info have no generation.
   - `/context` dumps the whole view; whether that should be paged is open.
10. **Model guidance text.**
    - The CONTEXT_SIZE line names `context_view` (coding.cpp:4414).
    - The CONTEXT_BUDGET guidance (:4419-4436) says activation happens on the next
      turn.
    - The `instructions` text (:4297-4299) says context is editable data.
    - The `lua` tool description (tools.cpp:1340-1344) lists `context()`/`append()`.

## Tests that pin current behaviour

| test | pins |
|---|---|
| tests/context_test.cpp (498 lines) | ContextStore view/stats/edit CAS, restore, `changed` kinds E/R/M with sizes (:123-160), managed rejections (mandatory, partial group, duplicate, stale), summarize ancestry, restore order, inspect pagination and revision conflict (:375-390) |
| tests/context_delta_test.cpp | append-delta-v1 packet format |
| tests/raw_context_test.cpp | RawContextTrace sites, including the `stop.linkage` label path |
| tests/context_budget_test.cpp | default policy 393216/196608, CONTEXT_SIZE line and numbers (:30-36), trigger guidance, staged activation, `state_context` omit, reopen policy recovery, `arco.manage` and `arco.inspect` |
| tests/request_state.hpp (helper used by application_test, coding_test, compaction_campaign_test) | **every request ends with a developer CONTEXT_SIZE item**. Mocks strip it, so any layout change breaks all three. |
| tests/compaction_campaign_test.cpp | managed archive through `arco.manage` in a turn, CURRENT_TASKS/CONTEXT_BUDGET presence (:44-45), malformed presentation handling |
| tests/coding_test.cpp | mock provider calling `context_manage`/`context_stats` (:45, 51, 603), stats shape (:1605-1618), the arco context/edit/restore/originals/json round-trip (:1522), `context_manage` via a Lua call with snapshot base (:1636-1665), rageshake (:500-519), append capacity (:1126-1300) |
| tests/workflow_repair_test.cpp | `context_repair` contract |
| tests/successor_seed_test.cpp | `validate_successor_seed` and `seed_successor` lineage |
| tests/backstop_test.cpp, backstop_raw_test.cpp | backstop successor selection **[unconfirmed specific oracles]** |
| tests/anthropic_provider_test.cpp:201 | `retain_request_state()` true |
| tests/request_cache_test.cpp, turn_raw_test.cpp | cache prefix (`reusable_input_items`), budget raw sites |
| tests/tasks_test.cpp | CURRENT_TASKS projection |
| tests/saved_state_test.cpp, journal_checkpoint_test.cpp, journal_resume_test.cpp, session_recovery_test.cpp | saved-context checkpoint and reopen **[context-specific oracles unconfirmed]** |
| tests/terminal_test.cpp:254-255, 278-282 | `/compact` and `/context` hints, `terminal_context_badge` strings |
| tests/domain_admission_test.cpp | `context_stats` admission |
| tests/useful_feedback_regression.lua, useful_work_test.lua | `stats()` feedback shape |
| scripts/check-terminal-pty:585-614 | live badge: `arco.append` then `arco.manage` summarize → `◆ CONTEXT compacted −…KiB`, blink frames |
| scripts/check-new-session-pty:74-167 | `/new` does not carry context (`OLD_CONTEXT` absent) |
| scripts/check-session-startup:63, 91 | context retained across reopen (`RETAIN_ME`, a workflow that reads `blackbird.context().entries`) |
| scripts/check-rrc:16 | context across restart/continue (a workflow that reads `arco.context().entries`) |
| tests/station_driver.py, station_main_native_test.py | `context_head` in station status |

**No test calls `context_view`, `context_edit`, `context_inspect`,
`context_originals` or `context_restore` as model tools.** Those are only reached
through the Lua equivalents. A fix for a1v.4 has no existing oracle to change; it
needs new direct oracles. No test was found for the reopen transcript
(`restore_message`) or for the `workflow-continuation`/`lua.synthetic` append labels.

## Unconfirmed

- How the OpenAI Responses and OpenRouter adapters serialize non-string
  `function_call_output.output` (Anthropic uses `dump_json`).
- Whether repeated `context_view` nesting can hit `ValueLimits.depth=128` before
  the byte limit.
- The exact per-entry byte cost of `context_stats` and `context_view` (estimated
  from field shapes).
- Specific backstop and saved-state test oracles (named from file purpose, not
  read).
