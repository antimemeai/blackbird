# C6 source map: harness-initiated compaction inside a turn

Read-only study of worktree `context/clm-a1v` at `1f28945`, 2026-10-19. Nothing was
built, run or tested. All references are `file:line` at that commit. **Unconfirmed**
marks an inference not checked in source.

C6 (PLAN.md, "C5 result and C6 decision") is a dedicated model request, limited to
`context_view`/`context_inspect`/`context_manage`, that runs when the budget trigger
fires. It runs between tool rounds, at boundaries with no open tool group, and if
possible while a tool runs.

## 1. Turn loop

### Native turn: `CodingEngine::turn` (src/coding.cpp:5763-6031)

- Refuses re-entry. `backstop_claimed_ || turn_running_` causes `conflict`
  (:5773). `turn_running_ = true` (:5809).
- Prepares admission (:5913-5937):
  - `repairable_outputs_ = placeholders(false, origin)` (:5919): calls already
    unanswered at admission.
  - Settlement credit is computed with an *empty* proposal (:5925-5932,
    `cancellation_budget`) and then `protect_settlement` or `refresh_settlement`.
  - `context_.protect` is set to `protect_workflow` (:5934-5937). It is cleared on
    every exit by `Reset` (:5882-5885).
- `context_.begin_workflow(origin)` (:5938). Every turn is one ContextStore
  workflow.
- Retains the program source, appends the operator prompt (:5960-5964), then
  `runtime_ = std::make_unique<Runtime>(*this); runtime_->eval(program)`
  (:5966-5968).
- After the Lua program returns:
  - `drain_workflows()` (:5970);
  - failure, capacity and cancel checks (:5971-5976);
  - pending program, tools and budget become the boundary program (:5979-6000);
  - `context_.finish_workflow(true, boundary_program, ...)` (:6001-6002);
  - `apply_activation()` (:6003);
  - a "Managed context:" display (:6004-6008).
- `rollback` on any error (:5889-5912):
  - `finish_workflow(false, ...)`, which rejects any pending proposal as
    `workflow-cancelled`;
  - on `interrupted`/`capacity`, `stop.linkage` placeholders are appended for
    unanswered calls (:5905-5910).

### Lua workflow: programs/turn.lua (48 lines)

| line | step |
|---|---|
| 2-4 | `for step=1,64`, `blackbird.emit(3880)`, `local response = blackbird.request()` |
| 6-35 | loop over `response.output` |
| 7-10 | namespaced call is an error |
| 12-24 | default `output_max_bytes` for `exec` and unbounded `read_file` |
| 26 | `local result = blackbird.call(item.name, args)` (synchronous) |
| 27-33 | `blackbird.append({{type="function_call_output", call_id=..., output=result}})` |
| 34-37 | `custom_tool_call` is an error; `message` goes to `blackbird.present` |
| 39-43 | `emit(3881)`, `blackbird.restarting()`, return on restart or `calls == 0` |
| 46 | 64-step budget error |

The siblings follow the same shape:
- programs/useful_turn.lua:18-35 is the same loop.
- programs/economy_pilot.lua:48-80 uses `pcall(blackbird.request)` and
  `pcall(blackbird.call)`.
- src/backstop.cpp:142-153 is the backstop assessment program.

`arco` is an alias of `blackbird` (src/coding.cpp:1101). The Lua program can be
edited by the model ("next turn reloads it", turn.lua:1), so a C6 hook placed in
Lua can be bypassed. A native hook applies to every workflow.

### Lua to native bridge (src/coding.cpp:1056-1065, 1582-1690)

There are 19 bridged names (:1056-1059). `bridge()` dispatches them:
- `request_op` → `engine.request(options)` (:1628-1632);
- `call_op` → `engine.call(name, args)` (:1633-1638);
- `manage_op` → `context_.manage` **directly, without `operation()`** (:1639-1641);
- `append_op` → `context_.append(items, "lua.synthetic")` (:1657-1662);
- `edit_op` → `context_.edit` (:1648-1650).

The VM interrupt hook calls `poll_commands()` and checks cancellation
(:1770-1784).

### `CodingEngine::request` (src/coding.cpp:4248-4814)

| line | what |
|---|---|
| 4249-4262 | `RequestGroup` RAII clears `request_group_active_` / `provider_.request_group_owned_` |
| 4267-4268 | `busy` if `request_group_active_ \|\| provider_.request_group_owned_` |
| 4271-4272 | `busy` if `restart_note_` |
| 4274 | `validate_protocol(context_.items())`: **any open tool group causes `conflict`** (validate_protocol :286-317 throws on pending calls) |
| 4276 | `drain_workflows()`: parked `workflow_invoke` continuations run here and append `workflow-continuation` developer items (:4215-4247, append :4238) |
| 4277-4279 | `items = context_.items(); origin = context_.head();` then validate again |
| 4280-4327 | request object: fixed instructions (:4282-4314), `"tools", request_tools()` (:4321), `parallel_tool_calls=false` (:4322), `store=false`, `stream=true` |
| 4329-4358 | `retry_policy` (default 5 attempts, 1 s base, 16 s cap) |
| 4359-4363 | `cache_policy`, `state_context` (`auto`/`omit`) |
| 4364-4374 | **every other option key overrides or adds a request field**, including `tools`, `instructions`, `input`, `model` |
| 4376-4392 | `CURRENT_TASKS` state |
| 4396-4400 | `input_bytes`, `budget_view(...)`, **`triggered`** |
| 4404-4416 | `CONTEXT_SIZE` line (C2) |
| 4417-4434 | `CONTEXT_BUDGET` guidance. It says proposals "activate only after successful workflow completion" (:4429) |
| 4436-4451 | state item. For `retain_request_state()` providers it is also appended to context (`provider.request-state`, :4442) and `origin` advances |
| 4453-4457 | request group taken |
| 4458-4461 | **`provider_.cancelled` overwritten** (poll_commands + engine.cancelled) |
| 4463 | `provider_.begin_request_group` |
| 4464-4466 | `prepare_request_cache` |
| 4467-4468 | `context_.head() != origin` causes `conflict` |
| 4476-4477 | `last_request_bytes_`, `usage_ = Value{}` (engine-wide single slots) |
| 4517 | fresh `retry_id` (OperationAttemptId) per request; raw 1325 policy |
| 4525-4528 | `provider_.cancelled` overwritten again |
| 4531-4750 | attempt loop. Each attempt is `operation("provider", ...)` (:4561). `provider.request` log record with `revision=origin`, `retry_group`, `ordinal` = attempt number (:4570-4582). Stream callback calls `poll_commands()` (:4616), retains `provider.stream` originals, and previews text through `display` (:4614-4628). Backoff sleep polls `cancelled` every 25 ms (:4743-4748) |
| 4752-4791 | usage selected into `usage_`; `observed_usage(usage_)` (:4787) |
| 4794-4795 | **`context_.head() != origin` causes `conflict`** |
| 4803 | `context_.append(response.output, "provider.branch:" + origin)`: **the function_call items enter context here; the tool group opens** |

### Dispatch: `CodingEngine::call` → `operation()` (src/coding.cpp:4815-5570, 3158-4110)

- `call` refuses the name `provider` (:4816).
- Everything runs inside `operation(name, ...)`:
  - `poll_commands()` (:3197);
  - cancel check unless already pumping (:3204-3206);
  - `effect_policy` (:3207);
  - depth at most 16 (:3211-3214);
  - `protect_workflow(entries, pending_proposal)` when a settlement is protected
    (:3220-3222);
  - `operation_started` and `status` callbacks (:3226-3238);
  - audited attempt admission and result.
- Context tools:
  - `context_manage` (:5165-5173) adds `base = head` when omitted, then
    `context_.manage`;
  - `context_inspect` (:5497-5498);
  - `context_view` (:5502-5503);
  - `context_edit` (:5504-5505);
  - `context_repair` (:5123-5163) requires `base == head`, `operation_depth_ == 1`
    and no owned children.
- Nested calls inside an operation body are an existing pattern: `rageshake` calls
  `call("exec", ...)` (:5350-5351).

### Points between "response with function_calls received" and "next request sent"

| # | point | anchor | open tool group? |
|---|---|---|---|
| A | response output appended to context | coding.cpp:4803 | **opens** (the function_call has no output) |
| B | `request()` returns to Lua; message items presented | turn.lua:34-36 | open |
| C | argument defaulting | turn.lua:12-24 | open |
| D | `blackbird.call` → `call()` → `operation()` → tool body, including exec wait | coding.cpp:1633, 4815, 3158, 4826-4863 | open |
| E | `blackbird.append(function_call_output)` | turn.lua:27-33 → coding.cpp:1657 | closes after the batch's **last** output (`whole_groups` treats a contiguous call batch as one group, context.cpp:1149-1178) |
| F | `emit(3881)`, `restarting()`, loop test | turn.lua:39-43 | **none** |
| G | next `blackbird.request()` entry up to `validate_protocol` / `drain_workflows` | coding.cpp:4267-4279 | **none** (verified by :4274 and :4279) |
| H | `drain_workflows` runs continuations, each a nested Lua `invoke_tool` with its own requests and calls | coding.cpp:4215-4247 | none on entry; the child's own rounds reopen groups |
| I | assembly through `begin_request_group`, up to dispatch | coding.cpp:4280-4560 | none |
| J | workflow return, then `drain_workflows`, then `finish_workflow` | coding.cpp:5967-6003 | none (the current activation point) |

With `parallel_tool_calls=false` (:4322) a round usually has one call, so E closes
it. A multi-call response stays open until its last output. Points F, G, I and J
have no open group. **G (in `request()` after :4279, where `triggered` is computed
at :4398) is the one native boundary that every workflow passes through.**

## 2. Tool waits

### exec (src/coding.cpp:4826-4863)

- `exec` computes a settlement credit that includes `context_.pending_proposal()`
  (:4831-4840).
- It then calls `command_jobs_->start(id, ...)` (:4847) and
  `return command_jobs_->wait(id, options, [this]{ poll_commands(); }, [this]{ return cancelled && cancelled(); })`
  (:4860-4862).
- `yield_ms` comes from the arguments, at most 3,600,000 (:4841-4842). The exec
  schema says that an omitted `yield_ms` waits to completion and 0 releases
  immediately (src/tools.cpp:938-941, 974-977).
- `process` with `op=foreground` uses the same wait (:4864-4871).

### `CommandJobs::wait` (src/command_jobs.cpp:1363-1499)

- Only one waiter is allowed: `if (!impl_->waiting.empty()) throw busy` (:1432).
  `waiting` is cleared by `Release` (:1442-1449).
- The deadline is `now + yield_ms`, or `time_point::max()` when omitted
  (:1450-1452).
- Loop (:1453-1474):
  - `tick()`, which is `poll_commands`;
  - check `cancelled()`; with `stop_on_cancel`, `stop_and_join`, four more ticks,
    then `throw interrupted` (:1455-1464);
  - lock and check terminal, release epoch or deadline (:1466-1471);
  - `condition.wait_for(lock, 10ms)` (:1473).
- **The engine thread is held in a polling loop with 10 ms waits.** It is not free.
  The child runs on its own `job->worker` jthread (:1321). Output reaches the audit
  only through `poll_commands` on the engine thread (src/coding.cpp:2636-2795,
  `log_.original("process.output", ...)` at :2659-2662).
- `poll_commands` is not re-entrant. With `pumping_commands_` set it returns
  immediately (:2637-2643).

### Threads

- **TUI.** Each turn runs on a `std::jthread worker` started by `TerminalUI::run`
  (src/terminal.cpp:2312, `perform(prompt)` at :2356). The UI thread calls
  `idle_work`, which is `engine.poll_commands()` and `poll_participants()`, only
  when `!busy` (terminal.cpp:2661-2662; idle_work defined at main.cpp:1678-1695).
- **UI to job control.** It goes through the locked mailbox
  `CommandJobs::request` (command_jobs.cpp:2032; called from terminal.cpp:2123).
  `poll_commands` drains it on the engine thread (coding.cpp:2644-2646). This is
  the existing pattern for passing cross-thread work to the engine owner.
- **Plain REPL.** The turn runs on the main thread (`perform` → `engine.turn`,
  main.cpp:818, 1223, 1561).

### Could another model request run during the wait?

- **On the engine thread, nested in the wait (for example from the tick):**
  - The guards in `request()` would not stop it. `request_group_active_` is false
    between requests, because RequestGroup ends when `request()` returns.
  - **`request()` itself rejects it.** `validate_protocol(context_.items())` at
    :4274 throws `conflict` because the exec function_call is still unanswered. So
    `blackbird.request` cannot be the compaction path while a tool runs.
  - If a new native request were run from inside `tick`, `pumping_commands_` would
    be true. Its own `poll_commands` calls (stream :4616, cancelled :4459) would do
    nothing, so the job's output would not be retained while it ran.
  - If it ran beside `tick` instead, retention continues through the stream
    callback. But the wait cannot see job completion or the `yield_ms` deadline
    until the request returns, which adds latency to the main turn.
- **On another thread:** no code path does this today. What prevents it:
  1. Lua is blocked synchronously in `blackbird.call`.
  2. No native hook exists in the wait loop.
  3. `idle_work` is gated on `!busy` (terminal.cpp:2661).
  4. The single-owner state has no locks: `ContextStore`, `AuditLog` /
     `RetainedState` (no mutex in include/blackbird/context.hpp,
     retained_state.hpp, journal_writer.hpp), and the provider flags (§6).

## 3. Activation rule (src/context.cpp)

### Staging

- `begin_workflow` sets `workflow_`, or gives `conflict` if already set
  (:1331-1340).
- `manage` (:1341-1424) checks, in order:
  1. `base != head_` → `stale-base` (:1346-1347);
  2. `pending_` set → `pending-overlap` (:1348-1349). **Only one pending
     proposal.**
  3. mode, reason/source, ids, summary;
  4. `whole_groups(entries_, selected)` → `partial-tool-group` (:1386-1388). An
     open current batch is allowed only if untouched (:1172-1177).
  5. pinned user/system/developer entries → `mandatory-entry` (:1389-1398).
- `if (!workflow_) return publish_managed(proposal, entries_, "", ...)`
  (:1407-1408). **This is the existing immediate path.**
- Otherwise:
  - `protect(entries_, proposal)` reserves settlement credit (:1409-1411);
  - a `staged` record is written (:1412);
  - `pending_ = Pending{proposal, snapshot=entries_, expected=head_, stage=<staged revision>}`
    (:1413).
  - Staging does **not** check `protocol_complete`, so a proposal can be staged
    while a group is open.
- `append_impl` advances `pending_->expected` only while
  `pending_->expected == head_` (:988-989). Pure appends keep the chain intact.
  An `edit`/`restore` publishes immediately even inside a workflow (:1018-1075,
  with no `workflow_` check), which breaks the chain.

### Publishing

- `finish_workflow(success)` (:1425-1490) clears `workflow_`:
  - no pending: publish the boundary program only;
  - failure: `workflow-cancelled` (:1465);
  - success:
    - `pending.expected != head_` → `settlement-conflict` (:1471-1473);
    - otherwise `publish_managed(proposal, snapshot, stage, boundary_program)`;
    - a rejected context still publishes the boundary program (:1477-1478).
- `publish_managed` (:1491-1652):
  - builds `next` from **basis** (the snapshot). It keeps every entry of an open
    group from the basis (`active_entries`, :1515-1531) and inserts the summary at
    the first chosen entry (:1532-1548).
  - `if (!stage.empty())`: the live `entries_` must start exactly with the basis,
    else `settlement-conflict` (:1550-1555). The entries appended after the basis
    are carried forward (:1556-1558). **This is the append-only continuation that
    lets a proposal staged mid-turn carry the later tool outputs.**
  - `protocol_complete(next)` → `invalid-tool-protocol` (:1596-1599). **Any
    unanswered call in the result rejects the publish.**
  - `protect(next, proposal)` (:1624-1626);
  - `record_boundary(packet, boundary_program)`, one durable append (:1630);
  - swap entries and head (:1640-1641);
  - `ContextChange{managed}` → `notify` → badge (:1642-1651; main.cpp:661-670).

### Existing immediate publishes

- **`/compact JSON`** (main.cpp:1143-1147) calls `context.manage` between turns,
  where `workflow_` is false, so it publishes immediately.
- **`context.edit`/`restore`** publish immediately even mid-workflow. This matches
  CORE_DESIGN.md:189: "Ordinary context editing takes effect within the running
  program before subsequent requests."
- **No path publishes a *managed* proposal mid-workflow.**

### Guards on publication

- base CAS (manage :1346; edit :1035);
- one pending proposal (:1348);
- whole groups (:1386);
- pinned instructions (:1389);
- append-chain `expected` (:988, :1471);
- basis-prefix check (:1550);
- `protocol_complete` (:1596);
- `protect`, i.e. `protect_workflow` → `cancellation_budget` (context.cpp:1254-1330
  includes a `settlement-conflict` reject record for a pending proposal) →
  `refresh_settlement` (coding.cpp:3133-3157);
- `request()` CAS `head != origin` → `conflict` (coding.cpp:4467, :4794).

### What a publish between tool rounds inside a running workflow would break, or change

1. **Rollback semantics.** A proposal published mid-turn is not undone by the
   `finish_workflow(false)` rollback (coding.cpp:5901). Today an interrupted or
   failed turn cancels staged proposals. The proposal would also commit without the
   boundary program, which stays separate and atomic at J.
2. **The documented contract changes:**
   - the guidance text (coding.cpp:4426-4430);
   - the `context_manage` description, "Stages until successful workflow
     completion" (tools.cpp:1246-1258);
   - CONTEXT_BUDGET.md:84-96.
3. **Bases held by the Lua program.** An `edit`/`manage` with a base taken earlier
   gets `stale-base`. This is a safe CAS failure, but the program must re-read.
4. **Timing within a round.**
   - At D (group open), `publish_managed` rejects with `invalid-tool-protocol`.
   - Publishing during `request()` between :4278 and :4794 gives `conflict`.
   - A publish is therefore safe only at F, G (before :4277 captures `origin`)
     or J.
5. **The model's own pending proposal.** A pending model proposal blocks C6
   (`pending-overlap`); C6 must not overwrite it. A C6 publish would make a
   later-staged model proposal against the old head stale.
6. **Retaining-state providers (Anthropic).** Request-state items are appended to
   context (:4442) and `retain_request_state` replays operational input in place
   (coding.hpp:34-35). Mid-turn compaction that removes or summarises signed
   thinking blocks of the current tool loop may be refused upstream.
   **Unconfirmed.** Likewise, splitting OpenAI reasoning items from their
   function_call is **unconfirmed**: `whole_groups` covers only call/output
   entries.
7. **Prompt cache.** `prepare_request_cache` (`reusable_input_items`) loses its
   prefix after any compaction. This is a cost, not a correctness issue.
8. **Rollback placeholders** (`stop_outputs` from the current view, :5857-5873)
   still work on compacted context, because open groups are kept intact.

## 4. Concurrent model requests: participants and colleagues

- **Engine wiring.**
  - `Participants` is built with owner-thread `Retain`/`Save`/`Publish`
    callbacks (coding.cpp:2584-2609).
  - `participant_start` (coding.cpp:5441-5468) uses `native_colleague_transport`,
    a separate HTTP and auth stack, **not `provider_`**.
- **Runs.**
  - `Participants::start` (participants.cpp:1284-1427) checks `max_runs` and
    `concurrency` (:1298-1318).
  - `prepare_colleague` (:1319-1325) retains the admission original
    `participant.admission` on the owner thread (:1354-1371).
  - It launches `std::jthread` → `Impl::work` (:1406-1408). Workers touch only
    guarded state and a bounded capture queue (participants.hpp:15-18).
  - Capture overflow (more than 1024 events or 8 MiB) gives `capacity`, not a
    block (participants.cpp:21, 539-548).
- **Results come back through owner `drain()`** (:1174-1260). Only
  `poll_participants` calls it (UI idle, main.cpp:1688, 1748), plus
  read/await/join/send.
  - **Nothing drains participants during an exec wait.** `poll_commands` does not
    call `drain()`.
  - The result row is projected and truncated above 64 KiB (participants.cpp:82-93).
- **What it can carry** (`prepare_colleague`, colleague.cpp:166-318):
  - `profile.tools` must be `"none"` and `requests` must be 1 (:197-199);
  - upstream `tools` is `[]` (:257, :271); chat-completions `tool_calls` →
    `unsupported` (:518);
  - **the whole request is at most 64 KiB** (:185-186);
  - context is an explicit array of `{id,text}` (:201-213), so structured items
    become text.
  - The instructions say "No tools are available" (:236-239).
- **Consequence for C6.** At the 384 KiB trigger, a participant cannot receive the
  context or a full view snapshot. It cannot call the context tools; it can only
  return text, which could be a JSON proposal the engine parses. The engine could
  then apply that proposal at boundary F/G with `base` = the head the snapshot came
  from. `manage` enforces the CAS (context.cpp:1346), and the chain and prefix
  checks enforce append-only continuation.
- **Audit.** Admission, results and raw capture are audited through the 21xx sites
  and owner retention. The colleague tool path retains `colleague.*` originals
  (coding.cpp:5470-5478).
- **Precedent for a second engine.** `run_backstop` (backstop.cpp:363-460)
  builds a separate `CodingEngine` over its own `RetainedState`/`AuditLog`/
  `ContextStore`. The context is seeded by `seed_successor` and given developer
  instructions. It **reuses the same provider object sequentially**
  (main.cpp:1273-1275 passes `*provider`), and restores `provider.cancelled`
  afterwards (backstop.cpp:388-403).

## 5. Tool restriction

- `request_tools()` (coding.cpp:2338-2350) returns `tool_definitions()`
  (tools.cpp:400) plus the active Lua tools.
- The `tools` option of `blackbird.request` replaces the field through the generic
  override (coding.cpp:4364-4374). So `tools` can be a subset, or
  `blackbird.array({})` as in the backstop assessment
  (`blackbird.request({tools=blackbird.array({}), state_context='omit', retry_policy={max_attempts=1}})`,
  backstop.cpp:143).
- Two gaps:
  - **The restriction is advertisement only.** `call()` dispatches any name
    (:4815-5570). A program that relies on `tools=` must refuse other names itself
    (backstop.cpp:146), or use `effect_policy` (coding.hpp:115-117, checked at
    coding.cpp:3207).
  - Even with `tools` overridden, `request()` still appends output to the main
    context (:4803), injects state (:4436-4451) and checks the head CAS.
- Context tool schemas are at tools.cpp:1175-1310: `context_view` :1225,
  `context_manage` :1246, `context_inspect` :1266.

## 6. Provider-request accounting

- **Provider object.** `request_group_owned_` is a plain `bool` (coding.hpp:23).
  - OpenAI: `request_binding_` and `responding_` are plain (coding.hpp:66-68;
    busy guards at coding.cpp:321-322, 363-364).
  - Responses, Anthropic and OpenRouter use plain `group_`/`responding_` (for
    example anthropic_provider.cpp:574-575, 605-606).
  - **None is atomic or locked.** They catch re-entry on the same thread, not
    concurrent use from two threads. A concurrent C6 needs its own provider
    instance.
- **`provider_.cancelled`** is a shared `std::function`, overwritten by every
  `request()` (:4458, :4525) and never restored. The backstop restores it by hand.
- **Engine single slots.** These are overwritten per request and read by `stats()`
  (:5612-5618) and the UI (`observed_usage`, main.cpp:696-709):
  - `last_request_bytes_`, `usage_` (:4476-4477);
  - `previewed_` (:4556);
  - `display` preview text (:4625), which would stream C6 text into the
    transcript.
  - `operation_started(operation == "provider")` flips the UI's provider/tool state
    (main.cpp:682-685).
- **Retries.** Each request has its own `retry_id` and attempt ordinals (:4517,
  :4531, :4570-4582). There is no session-level request counter.
  `token_usage.cpp` links attempts by causal parents (`retry_group`, 1325, and the
  ancestor walk at :600-625), not by sequence. Overlapping requests stay
  attributable, provided each has its own operation parent. Not exhaustively
  checked.
- **Raw capture** is per-thread lane. Worker threads need `raw_prepare_thread` /
  `RawThreadBinding` (participants.cpp:1335-1341; terminal.cpp:2312-2316).
- **Cancellation and Ctrl-C.**
  - SIGINT sets `interrupted` → `engine.cancelled` (main.cpp:580-582). In the TUI,
    `cancel_turn` sets the `cancelled` atomic (terminal.cpp:1824-1827).
  - It is observed in the exec wait (command_jobs.cpp:1455-1464), the provider
    stream, the backoff (coding.cpp:4745), the Lua hook (:1779-1782) and
    `operation()` (:3204).
  - It ends in `rollback`: the pending proposal is cancelled and placeholders are
    appended.
  - A C6 request on the engine thread inherits all of this. A worker-thread C6
    would need its own stop flag, as participants use `run.stop`.
- **Journal headroom** is checked per attempt (:4540-4554).
- **Settlement credit** is reserved per turn and refreshed by `protect` with the
  pending proposal (coding.cpp:3133-3157, 3220-3222, 4831-4840).

## Constraints for the C6 design

1. **There is a publish window, and it has no open group.**
   - `publish_managed` refuses any result with an unanswered call (context.cpp:1596).
   - `request()` refuses open groups (coding.cpp:4274) and any head change during
     assembly or dispatch (:4467, :4794).
   - So C6 may *compute* or *stage* during a tool wait, but may *publish* only at F,
     G (before :4277) or J.
   - The existing `Pending{snapshot, expected, stage}` with the append chain
     (:988) and the basis-prefix carry-forward (:1550-1558) already supports
     staging against a snapshot that has an open group, then publishing after its
     outputs are appended.
   - What is missing is a mid-workflow "settle pending now" entry point. That entry
     point changes rollback (a published proposal survives turn failure) and the
     documented contract.
2. **The compaction request cannot be `blackbird.request`.**
   - It would append C6's function_calls to the main context (:4803).
   - During a wait it fails `validate_protocol` (:4274).
   - It overwrites engine-wide slots and the callbacks (`provider_.cancelled`,
     `usage_`, `display`).
   - C6 needs its own native request path. The precedents are the backstop's
     separate engine and context (backstop.cpp:363-460) and the Lua
     `tools=`/`state_context='omit'` options. It needs a scratch transcript for its
     view/inspect rounds, with only `context_manage` staged into the main store
     through `manage` with an explicit `base`.
   - Its input must leave out the open group, or close it only in the request copy.
3. **Use of the wait is constrained by threads.**
   - The engine thread is busy-polling in `CommandJobs::wait`
     (command_jobs.cpp:1453-1474).
   - `ContextStore`, `AuditLog` and the provider flags have no locks.
   - Option one runs C6 on the engine thread beside the tick, not inside it. This
     delays seeing job completion and the `yield_ms` deadline until C6 returns.
   - Option two is a worker thread with a separate provider instance, and
     owner-thread retention and apply (the Participants pattern plus the
     `CommandJobs::request` mailbox). The existing participant transport cannot
     carry a 384 KiB context (64 KiB cap, colleague.cpp:185) or tools.
