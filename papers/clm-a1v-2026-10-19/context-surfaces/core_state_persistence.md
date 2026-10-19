# Context surfaces: core state and persistence

Read-only source catalogue, worktree `context/clm-a1v`, branch
`work/clm-a1v-2026-10-19` at `f7a79b1` (plus uncommitted PLAN/JOURNAL text). This is input
for the `arconaut-rd7` design: context generations and tool-output shaping at creation.
No source was built, run or changed. Line numbers refer to that tree. **[unconfirmed]**
marks claims taken from naming or partial reading and not traced end to end.

Direction key: **R** reads context, **W** writes or publishes context, **T** transforms
items, **P** persists to or replays from the audit journal or saved state.

---

## 1. ContextStore data model

`include/blackbird/context.hpp:68-157`, `src/context.cpp`.

| Member | Line (hpp) | Meaning |
|---|---|---|
| `head_` | 121 | Current revision: 32 lowercase hex characters, the `ApplicationRecordId` of the context record that last *published* (append, accepted edit or accepted managed). Empty only before the first append. |
| `entries_` | 122 | Live context, `[{id, item[, ancestry]}]`. `item` is a provider-neutral Responses-style object (message, reasoning, function_call, function_call_output). |
| `originals_` | 138 | `map<id, Original>`: the resident or lazily decodable exact original for each id still reachable. After a checkpoint it is pruned to live ids (`context.cpp:673-677`). |
| `captured_` | 139 | Every original captured since the last checkpoint, in capture order. Cleared on checkpoint (`:678`). |
| `historical_` | 120 | `RetainedState::FactHistory` snapshot used for history, index and originals listing. Refreshed lazily through `history_stale_`. |
| `archive_loaded_` | 116 | `true` means all originals are resident. It becomes `false` after a saved-state restore or a checkpoint: older originals are then found by journal lookup (`original_entry`, `:511-581`). |
| `workflow_` | 141 | A turn workflow is open (`begin_workflow` … `finish_workflow`). |
| `pending_` | 147 | A staged managed proposal: `{proposal, snapshot (entries at staging), expected (head at staging), stage (revision of the "staged" reject record)}`. |
| `protect` | 91 | Callback set per turn by `CodingEngine` (`coding.cpp:5934`). Every publication calls it with the prospective entries and the pending proposal, so cancellation capacity can be reserved before the record is written. |
| `changed` | 94 | Presentation listener for published revisions (`ContextChange{kind, revision, mode, bytes_before, bytes_after}`), wired in `main.cpp:661`. Failure is swallowed (`:1010-1017`). |
| `next_kind_` | 109 | Set to `restored` only while `restore` delegates to `edit` (`:1097-1106`). |
| `checkpoint_error_` | 117 | Last checkpoint failure. Checkpoint is a derived accelerator and never undoes a publication. |

**Entry ID format.** `id = revision + "." + index` (`context.cpp:951`). `revision` is the
hex `ApplicationRecordId` of the append or managed record that captured the entry, and
`index` is the decimal ordinal in that record's `originals` array. Several pieces of
code parse it:
- `original_entry` (`:513-531`) requires a length of 34-53, `'.'` at position 32, and a canonical decimal ordinal.
- `validate_successor_seed` (`:813-817`) checks the same shape.
- `append_of` (`:1150-1153`) takes the text before the last `'.'` as the response-round or append group.
- `inspect` index (`:1775-1776`) takes `capture_revision = id.substr(0,32)`.

A managed summary entry is `revision.0` of the managed record and carries
`ancestry = proposal.ids` (`:1569-1571`).

**Revision identity.** Every context record, published or not, issues a fresh
`ApplicationRecordId` through `AuditLog::issue` (`context.hpp:36`), and its hex form is
that record's `revision`. Rejected records (`edit` not accepted, `reject_managed`) carry
`revision` but do not move `head_`. The ID space comes from `RetainedState::issue`, so it
is unique per journal and environment. **[unconfirmed]**: uniqueness across journals and
sessions, which seeded successors rely on (see §6).

---

## 2. Context record schema (ApplicationChannel::context = 2)

The schema is defined only by the writers below and the replay reader
(`context.cpp:405-488`). Encoding is BBM2 (`encode_packet`).

| op | Writer | Fields | Publishes |
|---|---|---|---|
| `append` | `append_impl` `:959-968` | `op, base=head, observed=head, revision, accepted=true, origin=<label>, format="append-delta-v1", originals=[{id,item}], [lineage]` | always |
| `edit` | `edit` `:1045-1052` | `op, base (from candidate, may be null), observed=head, revision, accepted, candidate (minus entries), entries (full candidate list), outcome{accepted,revision,current,reason}` | only if `base==head` and the representation is valid |
| `managed` (accepted) | `publish_managed` `:1646-1655` | `op, base=head, observed=head, revision, accepted=true, candidate=proposal, stage, entries (full next list), originals (summary entry, if any), outcome` | yes |
| `managed` (rejected or staged) | `reject_managed` `:1226-1231` | `op, revision, observed=head, accepted=false, candidate, outcome{accepted=false,reason,current,revision}` (no `base`, no `entries`) | no |
| `append` (budget model) | `cancellation_budget` `:1322-1329` | synthetic `stop.linkage` with `entries` and all-zero revision. **Size estimate only, never written.** | — |

Replay rules (`:422-463`):
- `revision` must equal the record identity.
- `observed` must equal the running head.
- An accepted record must have `base == head`.
- `append` and `managed` contribute `originals`, each id unique.
- An `append-delta-v1` record extends `entries`; other accepted records replace `entries` wholesale.
- `candidate` is projected out of the decode (`omitted{"candidate"}`, `:414`).

**Append labels (`origin`) seen in source.**

| Label | Writer |
|---|---|
| `initial` | `coding.cpp:2619` |
| `operator` | `coding.cpp:5963` |
| `provider.request-state` | `coding.cpp:4442` |
| `provider.branch:<origin-head>` | `coding.cpp:4803` |
| `workflow-continuation` | `coding.cpp:4244` |
| `workflow.protocol-repair` | `coding.cpp:5157` |
| `stop.linkage` | `coding.cpp:5909` |
| `lua.synthetic` | `coding.cpp:1660` |
| `session.successor` | `context.cpp:924` |
| application, backstop and session labels | `application.cpp:299`, `backstop.cpp:383`, `main.cpp:1212`, `session.cpp:596` (§7) |

---

## 3. ContextStore operations

Each operation opens a `RawContextTrace` with an action code (§5). It emits 1800 at entry
with the state snapshot and 1815 once at exit. Publication writes 1801 and 1802 around
the journal submit. Credits are the per-action control credit given to the trace.

| Op | Line | Dir | Reads | Writes | Invariants | Audit record | Raw sites (beyond 1800/1815) |
|---|---|---|---|---|---|---|---|
| ctor `ContextStore(log, origin)` → private ctor | `:325-497` | P,R | `saved_state()->context` {version, base, entries, live_originals}, `tail_facts()` after `saved->boundary.sequence` | `head_, entries_, originals_, captured_, archive_loaded_=false` if saved, `historical_`, `root_lifetime_`; installs `set_maintenance_checkpoint` | saved version `"1"`, head of 32 characters, unique original ids, `valid_entries`; a corrupt saved state falls back to full replay (branch 1); per-record replay rules in §2; throws on a corrupt tail | none written. May call `maybe_checkpoint` (→ saved state) | 1821 branch (0 saved, 1 fallback, 2 accepted, 3 rejected, 4 invalid), 1822 saved cursor and flags, 1813/1814 per record read (purpose 1 replay), 1803 state, credit 450 |
| `~ContextStore` | `:498-501` | — | — | clears the maintenance checkpoint callback if the root is alive | — | — | — |
| `checkpoint(origin)` | `:587-697` | P | `entries_`, each `original_entry(id)` | `RetainedState::save_current_state({version:1, base:head_, entries, live_originals})`, then prunes `originals_` to live ids, clears `captured_`, `archive_loaded_=false`, refreshes history | every live id must resolve to an original (else corrupt); refused under `no_new` input when raw is on | saved-state file (not an application record) | 1811 request with cursor and prior saved cursor, 1812 reply, 1803; credit 488 |
| `maybe_checkpoint` | `:698-706` | P | cursor vs saved boundary | calls `checkpoint` when there is no saved state, or after ≥64 records or ≥4 MiB since the last save | failure ignored | — | (via checkpoint) |
| `head()` | hpp `:78` | R | `head_` | — | — | — | none (untraced) |
| `view(origin)` | `:707-715` | R | `head_, entries_` | — | returns `{base, entries}`, a full copy | — | 1806 variant 0 rows; credit 209 |
| `stats(origin)` | `:716-751` | R | `entries_`, `items()`, `view()` | — | sizes are BBM2 packet bytes, "not tokens" | — | 1807 per entry (data partition: id and item length), 1808 totals; credit 642 |
| `items(origin)` | `:752-763` | R | `entries_` | — | returns `item` values only, ids dropped. **This is the provider input source.** | — | 1806 variant 1; credit 209 |
| `append(items,label)` → `append_impl` | `:927-997` | W,P | `entries_, head_, originals_, pending_` | `entries_ += fresh`, `originals_`, `captured_`, `head_=revision`; retargets `pending_->expected` when it equalled the old head (`:988-989`) | every item is an object; fresh ids unused; `protect(next, pending, ref)` before the write | `context` append-delta-v1 | 1816 label 0 (`stop.linkage` may use held control), 1804/1805 protection, 1801/1802, 1803; credit 1228 (1080 under `no_new`) |
| `validate_successor_seed` (static) | `:782-902` | R | seed value | — | seed ≤1 MiB, version 1; `source{session absolute path, environment, journal, context_revision hex32, prefix_sequence, prefix_end ≥ header, status ∈ settled/unsettled/unknown, reason}`; 1-4096 entries with `revision.index` ids; item roles and types limited to message/reasoning/function_call/function_call_output; reasoning must be self-contained (`encrypted_content`, or Anthropic `thinking` with signature or `redacted_thinking`); call and output pairing complete | — | credit 93, no instance state |
| `seed_successor(seed)` | `:903-926` | W,P | seed, root state | one `append` labelled `session.successor` with `lineage{version, scope, source, source_entries=[old ids]}` | store empty, no workflow or pending, `fact_count()==0`, writer live, seed journal ≠ own journal | `context` append with `lineage` | credit 1481 plus child append |
| `edit(candidate)` | `:1018-1076` | W,P | candidate `{base, entries}`, `head_` | on accept: `entries_ = candidate.entries`, `head_ = revision`; `notify(ContextChange{next_kind_,…})` | every candidate id must exist as an original (`valid_entries` `:232-249`: resident or journal lookup), ids unique; `base == head_`. **Items are not checked against their originals: edit may change item content under an existing id.** **[confirmed by reading; intended "editable context"]** | `context` edit record, always written, carrying the full candidate entries | 1804/1805, 1801/1802, 1803; branch 0 accepted / 1 refused; credit 1228 |
| `restore(entry)` | `:1077-1108` | W,P | `original_entry(entry)` | replaces or appends `{id, original item}` and calls `edit` with kind `restored` | the entry's original must exist (else `invalid_identity`) | via edit | 1816 label 1; credit 1388 |
| `originals()` | `:1109-1125` | R,P | `captured_entries` → every accepted context record in history | — | sorted by id, `{id,item}` only | — | 1817/1818 pages, 1813/1814 (purpose 3), 1806 variant 3; credit 209 |
| `original_entry(id)` (private) | `:502-586` | R,P | `originals_`, else `RetainedState::lookup(visible, family 1, application, bytes)` when `!archive_loaded_` and saved state exists | — | lookup only at or before the saved boundary in the same journal; the record must be an accepted context record whose `originals[ordinal].id == id` | — | 1819/1820 lookup, 1813/1814 (purpose 0 resident or 2 archive), 1806 variant 4; credit 209 |
| `manage(proposal)` | `:1362-1451` | W,P (via publish or reject) | proposal `{base, mode, reason, source, ids, [summary]}`, `entries_`, `originals_`, captured (restore only) | without a workflow: `publish_managed` now. In a workflow: a `staged` reject record, and `pending_ = {proposal, entries_, head_, stage}` | rejection reasons are listed below this table | `context` managed (accepted, rejected or staged) | 1816 labels 2 and 5, 1823 mode and count, 1804/1805 when staged, branch 2 staged; credit 2894 |
| `reject_managed` (private) | `:1215-1243` | P | `head_` | journal only; `*settled = true` | — | `context` managed accepted=false | 1816 label 2 (literal), 1801/1802, 1803; credit 513 |
| `publish_managed` (private) | `:1518-1685` | W,T,P | proposal, `basis` (staged snapshot or `entries_`), `entries_` (for continuation), `captured_entries` (restore) | `entries_ = next`, `head_ = revision`, `originals_ += summary`, `captured_ += summary`; `notify(managed)`; `maybe_checkpoint` | the open trailing tool group and its whole round are kept even when unselected (`:1556-1564`). Summary inserted at the first selected position with `ancestry` (`:1568-1575`). With a non-empty `stage`, `basis` must be a prefix of `entries_`, and later appends are carried forward (`:1583-1592`), else `settlement-conflict`. Restore re-inserts originals in capture order without reordering live entries (`:1593-1628`). `protocol_complete(next)` (`:1128-1148`), else `invalid-tool-protocol` | `record_boundary`: a `context` managed record plus, optionally, a `program` boundary record **atomically in one journal append** (`:94-149`) | 1823, 1804/1805, 1801/1802 ×2 (context and program), 1803; credit 1809 |
| `inspect(query)` | `:1686-1845` | R,P | `historical_` (history, index, originals list), `original_entry` (originals plus entry) | — | `kind ∈ history/index/originals`; optional `revision` CAS against the history snapshot (last record revision, including rejected ones) or the head; offset and limit paging (limit 1-65536); `format ∈ bbm2-stream` (default, `BBMS\1` plus 8-byte LE length frames) or `json`; virtual stream, never materialised | — | 1816 labels 3 and 4, 1826 request, 1809 page (data partition), 1810 summary, 1817/1818 pages, 1813/1814; credit 361 |
| `stop_outputs(entries, interrupted)` (static) | `:1253-1274` | T | entries | — | synthesises `function_call_output{error: turn_interrupted or turn_stopped, detail}` for each unanswered call | — | none |
| `cancellation_budget(entries, proposal)` | `:1275-1351` | R | `journal_limits`, entries, proposal | — | encodes worst-case `stop.linkage` append and `settlement-conflict` reject packets to size the journal reservation; floor `{8192 B, 16 records}` | — | 1825; credit 224 |
| `pending_proposal()` | `:1244-1252` | R | `pending_` | — | returns the proposal or null | — | 1806 variant 2; credit 209 |
| `begin_workflow` | `:1352-1361` | W (state) | `workflow_` | `workflow_ = true` | not already open (else conflict) | — | 1803; credit 268 |
| `finish_workflow(success, boundary_program, acceptance)` | `:1452-1517` | W,P | `pending_, head_` | `workflow_ = false`. With no pending proposal: the boundary program record alone on success. On failure: reject `workflow-cancelled` (or the audit-unavailable outcome without a record). On success: `publish_managed(snapshot, stage, boundary)` if `expected == head_`, else reject `settlement-conflict` plus a separate program record. Clears `pending_` | sets `WorkflowAcceptance.{context_settlement_accepted, boundary_program_accepted}`, which `CodingEngine::apply_activation` gates on (`coding.cpp:5832-5856`) | `context` managed and/or `program` (`workflow-config-effective-v1`) | 1827 success and boundary flags, 1803 stages 1-3, 1801/1802; credit 2978 success / 997 failure |
| `input_bytes()` (private) | `:998-1009` | R | `entries_` items | — | noexcept; BBM2 size of `items()` for `ContextChange` | — | none |
| `notify` (private) | `:1010-1017` | — | — | calls `changed` | noexcept | — | none |
| `raw_state` (private) | `:221-224` | R | the state counters | — | — | — | feeds 1800 and 1803 |
| `refresh_history` (private) | `:225-231` | P | `history_snapshot()` | `historical_` | lazy, `history_stale_` | — | none |
| `captured_entries` (private) | `:313-324` | R,P | the whole context history | — | accepted records' `originals` | — | 1817/1818, 1813/1814 |
| `valid_entries` (private) | `:232-249` | R | entries, `originals_`, journal lookup | — | object form; every id has an original; unique | — | via `original_entry` |

**`manage` rejection reasons:** `stale-base`, `pending-overlap`, `invalid-mode` (modes:
select, archive, summarize, restore), `missing-source-or-reason`, `invalid-ids`,
`absent-id` (for modes other than restore), `invalid-summary` (summarize needs a
developer or assistant role, non-empty `content` and no `type`), `partial-tool-group`
(`whole_groups` `:1170-1199`), `partial-round` (`whole_rounds` `:1157-1169`: a
reasoning item binds every later entry of its append), `mandatory-entry` (user, system
and developer role items are pinned: select must keep them, archive and summarize must
not choose them), and `malformed-proposal` (for corrupt or invalid_range errors).

**AuditLog methods in `context.cpp`** (the class is declared in `context.hpp:33-58`):

| Method | Line | Dir | Notes |
|---|---|---|---|
| `record(channel, packet)` | `:58-63` | P | Issues an ID and submits one `ApplicationRecordEvent`; returns hex. |
| `record(id, channel, packet, publication)` | `:64-93` | P | `root_.submit`. With a publication, emits 1801 before and 1802 after (or on error). |
| `record_boundary(id, ctx, program, pub)` | `:94-149` | P | One `root_.append` with two events (context plus program), so the managed context and the activation commit together. With a null program it is a plain `record`. |
| `retain_program(source, generation)` | `:150-173` | P | A `log` record `program.source` with the raw source as dependency bytes, plus a `program` activation record `{generation, activation: turn-boundary}`. Called once per turn (`coding.cpp:5946`). |
| `original(capture)` / `original(id, capture)` | `:174-220` | P | For `provider.stream`: the raw bytes go to `store_diagnostic(<expires>.<id>)` with a 30-day expiry, plus a `log` metadata record `{diagnostic_file, expires_at, bytes, available[, storage_error]}`. **Stream originals are not journal sources.** Other labels: a `log` record whose `SourceReference` dependency is the raw bytes in the journal; returns that reference. |
| `inspect/trajectory/variables` | `audit.cpp:393-410` | R | see §7 |

---

## 4. Every caller of ContextStore in `src/`

### 4.1 `src/coding.cpp` (CodingEngine holds `ContextStore &context_`, `coding.hpp:157`; accessor `context()` `coding.hpp:101`)

| Line | Call | Dir | Purpose |
|---|---|---|---|
| 1640 | `context_.manage(arg)` | W | Lua `blackbird.manage`. **Direct, without the `operation()` admission path or an invocation record**, unlike the `context_manage` tool. |
| 1643 | `context_.inspect(arg)` | R | Lua `blackbird.inspect` |
| 1646 | `context_.view()` | R | Lua `blackbird.context()` |
| 1649 | `context_.edit(arg)` | W | Lua `blackbird.edit` (direct) |
| 1652 | `context_.originals()` | R | Lua `blackbird.originals()` |
| 1655-1656 | `restore` then `view` | W | Lua `blackbird.restore(id)` |
| 1660-1661 | `append(items,"lua.synthetic")` then `view` | W | Lua `blackbird.append`, which the turn programs use for **every tool result** (`function_call_output`) and for workflow developer messages |
| 2485, 2523 | `context_.items({view.reference()})` | R | `context_budget` tool: BBM2 input size for `budget_view` |
| 2618-2619 | `head().empty()` → `append({}, "initial")` | W | The engine constructor ensures a non-empty head; this is the **first context record of a fresh session**, an empty append |
| 3141-3144 | `cancellation_budget(entries, proposal)`, `view`, `pending_proposal` | R | `protect_workflow`: reserves journal settlement capacity before each publication; the `protect` callback target |
| 3221-3222 | `view`, `pending_proposal` → `protect_workflow` | R | `operation()` entry under protected settlement |
| 3316-3323 | `parse_id<ContextRevisionId>(context_.head())`, `revision: head` | R,P | **Every operation admission** stores the context head in `DecisionEvent.context` (`:3349-3356`) and in the continuation metadata `revision`. The head hex is reinterpreted as a `ContextRevisionId`. |
| 3361 | `admission.linkage({head, attempt})` | R | Raw region linkage |
| 4224 | `validate_protocol(items())` | R | `drain_workflows` precondition |
| 4238-4244 | `append([developer msg], "workflow-continuation")` | W | Result of a queued workflow |
| 4274-4279 | `validate_protocol(items())`; `items()`; `origin = head()` | R | `request()`: **provider input = `context_.items()` in full**, origin head captured |
| 4397-4435 | budget, `CONTEXT_SIZE` and `CONTEXT_BUDGET` operational state | R | Size measured on `request.input` (the context items) |
| 4441-4443 | `append({state_item}, "provider.request-state")`, `origin = head()` | W | Only when `provider_.retain_request_state()` (Anthropic, `anthropic_provider.hpp:15`): the operational-state developer item becomes **part of context**. Otherwise it is request-only. |
| 4467-4468 | `head() != origin` → conflict | R | Context must not change during cache preparation |
| 4570-4582 | `log_.record(log, provider.request{attempt, revision: origin, generation, retry_group, ordinal})` | P | **Request-to-context linkage**: each provider attempt records the context revision it was built from |
| 4590-4597 | `log_.original(provider.stream, {attempt, revision: origin})` | P | Raw stream blocks go to diagnostics, keyed to the revision |
| 4794-4795 | `head() != origin` → conflict | R | The context must be unchanged while the request is in flight |
| 4803-4809 | `append(response.output, "provider.branch:" + origin)` | W | **Response round appended as one append.** All output items (reasoning, message, function_calls) share one revision, which `whole_rounds` relies on. The label names the parent head. |
| 4831-4832 | `cancellation_budget(view.entries, pending_proposal)` | R | `exec` settlement reservation |
| 4940-4941 | `stop_outputs(view.entries,false)` | R | `workflow_invoke`: when calls are unanswered, the workflow is queued instead of run |
| 5124-5161 | `context_repair`: `head()` CAS, `stop_outputs`, `items()`, `append(outputs,"workflow.protocol-repair")` | W | Placeholder outputs for unanswered calls listed in `repairable_outputs_` |
| 5165-5172 | `context_manage` tool: base defaults to `head()`; `manage(proposal)` | W | Model-authored compaction path (admitted operation) |
| 5266 | `revision: head()` | R,P | Rageshake complaint captures the context head, plus `identity_.conversation` (`:5271`) |
| 5497-5510 | `context_inspect`/`view`/`edit`/`originals`/`restore` tools | R/W | Model tools (admitted operations) |
| 5572 | `context_.stats()` | R | `CodingEngine::stats()` / `context_stats` tool; adds the budget from `items()` (`:5611`) |
| 5641 | `validate_protocol(items())` | R | `validate_restart` |
| 5764 | `RawTurnTrace{…, head()}` | R | Turn trace carries the head at turn start |
| 5857-5861 | `stop_outputs(view(origin).entries, interrupted)` | R | Placeholders at turn start (`repairable_outputs_`, `:5919`) and at rollback |
| 5884, 5892 | `context.protect = {}` | — | Clears the per-turn protect callback |
| 5901 | `finish_workflow(false, empty, stop_origin)` | W,P | Rollback: cancels a staged proposal |
| 5909 | `append(results, "stop.linkage", stop_origin)` | W | Answers unanswered calls on interrupt or capacity stop (`no_new` raw input) |
| 5925-5926 | `cancellation_budget(view.entries, empty)` | R | Turn admission reservation |
| 5934-5937 | `context_.protect = …protect_workflow` | — | Installs protection |
| 5938 | `begin_workflow(origin)` | W | Turn begins the workflow |
| 5960-5963 | `append([{role:user, content:prompt}], "operator")` | W | The operator prompt |
| 5978 | `validate_protocol(items(origin))` | R | Before restart |
| 6001-6002 | `finish_workflow(true, boundary_program, origin, &acceptance)` | W,P | **The only activation point for staged managed proposals** (plus policy, program and tools activation) |

### 4.2 Other files

| File:line | Call | Dir | Purpose |
|---|---|---|---|
| `main.cpp:230` | `ContextStore::validate_successor_seed(seed)` | R | Validates a successor seed (§7) |
| `main.cpp:422-424` | `ContextStore context{log}`; `context.seed_successor(...)` | W,P | Process-level store construction (replay) and successor seeding |
| `main.cpp:563, 1021, 1070, 1138` | `context.checkpoint()` | P | Explicit saved-state checkpoint (session save or switch, `/checkpoint`) |
| `main.cpp:661-670` | `engine.context().changed = …` | — | TUI badge (edited, restored, compacted) |
| `main.cpp:1126` | `context.view()` | R | `/context` |
| `main.cpp:1146` | `context.manage(json)` | W | `/compact <json>` (direct, without admission) |
| `main.cpp:1151` | `context.inspect(json)` | R | `/inspect` |
| `main.cpp:1156` | `context.originals()` | R | `/originals` |
| `main.cpp:1159` | `context.restore(id)` | W | `/restore` |
| `main.cpp:1212` | `context.append(...)` | W | see §7 |
| `main.cpp:1432` | `context.head()` | R | see §7 |
| `main.cpp:1616` | `context.items()` | R | see §7 |
| `application.cpp:299, 325` | `engine.context().append(...)`, `checkpoint()` | W,P | see §7 |
| `backstop.cpp:272, 280` | `source_engine.context().head()`, `view()` | R | Backstop reads the source context to build a successor |
| `backstop.cpp:375-383` | `ContextStore context{log,…}`; `seed_successor`; `append` | W,P | Successor context in a **new journal** (§7) |
| `session.cpp:557-596` | `SessionStore::resume(ContextStore&)`; `context.append(user …)` | W | Resume appends a note (§7) |
| `coding.hpp:81, 101, 157` | constructor, accessor, member | — | One engine ↔ one ContextStore reference |

The `context_.` hits in `raw_beads.cpp:37`, `terminal_custody.cpp:276`, `raw_runtime.cpp:1986`
and `raw_turn.cpp:235`, and the `pending_proposal_` hits in `retained_state.cpp`, are
**unrelated** members of the same name (raw adapter contexts, and RetainedState's own
journal pending proposal). See §6.

### 4.3 Lua and model access

- **Tool definitions** (`tools.cpp:1175-1315`): `context_budget`, `context_stats`,
  `context_view`, `context_edit`, `context_manage`, `context_inspect`,
  `context_originals`, `context_repair`, `context_restore`.
- **Lua functions** (`coding.cpp:1056-1060`): `blackbird.context`, `edit`, `originals`,
  `restore`, `append`, `manage`, `inspect`, `stats`, `request`, `call`.
- **Turn programs** append each tool result verbatim as
  `{type='function_call_output', call_id, output=result}`: `workflows.cpp:298`
  (built-in `ultracode`), `programs/turn.lua:28`, `useful_turn.lua:25` and
  `economy_pilot.lua:83`.
- **Shaping at creation today.** `programs/turn.lua:13-25` injects
  `output_max_bytes = 16384` for `exec` and for unranged `read_file`. Native
  presentation (`tools.cpp:208-235`, `file_content_presentation` and
  `process_output_presentation`) returns `omitted_bytes`, an `output_ref` (exec) and a
  `snapshot{journal, sequence}` (read_file, `coding.cpp:5557-5562`).
- **Fetching the full original.** `read_process_output` (`coding.cpp:5033-5122`) does a
  **linear scan of every journal fact** for `operation.result` and `process.output` log
  records matching the attempt, then reads their dependency sources.
  `read_file{snapshot}` (`:5514-5534`) reads `log_.root().source({journal, sequence})`
  directly. **Both locators are bound to a journal ID.**

---

## 5. Raw instrumentation of context (`src/raw_context.cpp`, `include/blackbird/raw_context.hpp`)

ABI text: `raw_context.cpp:335-367`. Action codes run from 0 to 19 (`raw_context.hpp:18-39`):

| Range | Actions |
|---|---|
| 0-5 | reconstruct, checkpoint, view, stats, items, append |
| 6-11 | validate_seed, seed, edit, restore, originals, manage |
| 12-19 | inspect, budget, pending, begin, finish, reject, publish, original |

Labels: origin 0, entry 1, reason 2, query_kind 3, format 4, managed_source 5.

Input purposes for `raw_context_read`:

| Code | Purpose | Callers |
|---|---|---|
| 0 | resident Original | `Original::entry` |
| 1 | replay | ctor |
| 2 | archive lookup | `original_entry` |
| 3 | history | `visit_context_records` |

| Site | Emitter | What it carries | Partition |
|---|---|---|---|
| 1800 | `RawContextTrace` ctor `:46-83` | action, parent ref, head text, entries/originals/captured counts, workflow/pending/archive_loaded | control |
| 1801/1802 | `raw_context_publication[_return]` `:218-250` | record identity, channel, cursor before; record or cursor or error after | control (held publication credit) |
| 1803 | `state` `:117-126` | stage plus the same state tuple after a transition | control |
| 1804/1805 | `protection` (hpp `:73-89`) | before and after the `protect` callback | control |
| 1806 | `rows` | variant: 0 view, 1 items, 2 pending, 3 originals, 4 original lookup; plus count | control |
| 1807/1808 | `stats` | per-entry id and item length (data); totals | data / control |
| 1809/1810 | `inspect` | page bytes (data); offset, size, total, snapshot, head | data / control |
| 1811/1812 | `checkpoint` | save request and reply with cursors | control |
| 1813/1814 | `raw_context_read` `:251-334` | source descriptor; **literal resident bytes** (data) for non-retained sources, or a reference only for retained packets | control + data |
| 1815 | `finish` | branch, stage, error | control |
| 1816 | `label` | label kind plus bytes (data, or a held control literal for cleanup) | data |
| 1817/1818 | `visit_context_records` pages | history paging | control |
| 1819/1820 | `original_entry` lookup | parsed identity, cursor, result | control |
| 1821/1822 | reconstruct | per-record branch; saved cursor | control |
| 1823 | manage, publish | mode, selected count, stage | control |
| 1824 | reserved | — | — |
| 1825 | `cancellation_budget` | limits and computed credit | control |
| 1826 | `inspect` | request offset, limit, json flag | control |
| 1827 | `finish_workflow` | success flag, boundary present | control |

**Origins and capacity.** `RawContextOrigin::Input::no_new` (stop and cleanup paths,
`coding.cpp:5826-5828`) forbids fresh claims. Data credit, labels and reads then throw
`capacity` (`raw_context.cpp:52-53, 134-135, 197-198, 261-262`), and `checkpoint`
refuses (`context.cpp:592-593`).

**Context IDs elsewhere in raw instrumentation.**

| Location | Carries |
|---|---|
| `RawTurnTrace{…, head}` (`coding.cpp:5764`) | head at turn start |
| `trace.placeholder_request(…, head)` (`:5858`) | head for stop placeholders |
| `assembly.scalar(15,7, origin)` (`:4489`) | request origin head |
| `final_acceptance.scalar(31,7, head)` (`:4809`) | head after the response append |
| region linkage `raw_text(origin)` (`:4491, 4585`) and `raw_text(head)` (`:3361`) | head text |

None of these has a generation field. They carry only head text, and the definition
generation is the Lua program generation, not a context generation.

---

## 6. Durable persistence and replay

### 6.1 Event model and channels (`include/blackbird/retained_events.hpp`, `src/retained_events.cpp`)

| file:line | Surface | Dir | Notes |
|---|---|---|---|
| retained_events.hpp:19 | `enum ApplicationChannel {log=1, context=2, program=3}` | — | There is exactly one `context` channel per journal. Nothing in it names a generation, conversation or stream. Decoding accepts only values 1-3 (retained_events.cpp:332-333), so a new channel is a wire change. |
| retained_events.hpp:88-93 | `ApplicationRecordEvent{identity, channel, payload}` | P | The context payload is an opaque BBM2 packet. The 16-byte `identity` **is** the revision (`hex(identity) == packet.revision`, context.cpp:421-423). |
| retained_events.cpp:238-242, 566-575 | encode/decode body kind 14 | W/R | Writes identity16, channel u16 and a payload blob. There is no schema version in the event; the packet shape is versioned only by packet fields (`op`, `format:"append-delta-v1"`). |
| retained_events.hpp:99-114 | `RetainedKind::application = 14` | — | Lookup key for context records (`query_key(1, application, identity)`, retained_state.cpp:75-83, 100-104). |
| retained_events.hpp:122-125; retained_events.cpp:471-474 | `DecisionEvent{…, conversation, …, context: ContextRevisionId}` | P | Every admitted operation durably names the context head (written at coding.cpp:3349-3356). |

### 6.2 Saved state (current projection)

| file:line | Surface | Dir | Notes |
|---|---|---|---|
| saved_state.hpp:8-16 | `SavedState{boundary, fact_count, issuer_counter, context, programs, unresolved, slot}` | — | `context` is a free-form `Value`. |
| context.cpp:644-648 → retained_state.cpp:3025 | `save_current_state(context)` | P | ContextStore passes `{version:1, base, entries, live_originals}`. RetainedState **adds** `physical_records`, `archive_entries` and optionally `semantic_coverage:"settled-no-station-v1"` to the same object (retained_state.cpp:3127-3149), so the context object also carries recovery-gating keys that RetainedState owns. |
| retained_state.cpp:3038-3040 | preconditions | — | The writer must be live, with no transaction, nothing prepared, reconciled, and `historical_.empty()` (no predecessor segments). |
| retained_state.cpp:3043-3049 | `SavedState next{cursor(), …, slot ^ 1}` | — | Two alternating slots. The boundary is the exact current cursor, so the saved context must equal the head at that cursor. ContextStore guarantees this by saving only after its own record commits. |
| retained_state.cpp:2937-3023, 3049 | `current_programs()` → `programs` | T | Latest `program` packet per label. This covers `context-budget-effective-v1`, `workflow-config-effective-v1`, `session.identity`, `session.settings`, `participants-state-v1`, `station.*` and tasks. **Budget policy persists here, not in context.** |
| retained_state.cpp:3095-3120 | `ArchiveCatalog::publish_merge` | P | Sorted locator catalog `audit.archive.<slot>` for every committed fact, context included. |
| retained_state.cpp:3150-3179; saved_state.cpp:263-373 | `publish_saved_state` | P | `audit.state.<slot>` (BBSAVST1) holds a 192-byte prefix, then the context BBM2, the programs BBM2, unresolved events and a CRC32C. **The whole file is capped at 64 MiB** (saved_state.cpp:11), so live entries, live_originals and programs share that limit. Writes use tmp + fsync + rename + dir fsync, and the boundary must equal the cursor. |
| saved_state.cpp:54-210 | `load_slot` | R | Checks magic and CRC. Requires a **byte-identical journal header** (90-94), which binds the file to this journal, environment and namespace. Also checks the anchor commit frame and the context shape (`version=="1"`, `base` of 32 characters, `entries` and `live_originals` arrays, 143-153). Extra keys are tolerated. A mismatch is ignored and loading falls back. |
| saved_state.cpp:212-262 | `load_saved_state` | R | Picks the slot with the greater boundary sequence. |
| retained_state.cpp:3180-3210 | resident retirement (compact only) | — | After a checkpoint, tail facts before the boundary leave RAM. Later reads of older originals go through the catalog (§6.4). |

### 6.3 Open, suffix recovery and compact mode

| file:line | Surface | Dir | Notes |
|---|---|---|---|
| retained_state.cpp:314-316; main.cpp:303-307 | `header.predecessor` → unsupported | — | **Linked journal segments cannot be reopened.** One session directory holds one `audit` journal. |
| retained_state.cpp:340-393 | resume selector | R | With saved state, `semantic_coverage=="settled-no-station-v1"` and valid `physical_records`, the store resumes in compact mode at the saved boundary. Otherwise it runs a full replay (automatic up to 64 MiB, main.cpp:311-312; unlimited with `--rebuild-session`) or reports `unsupported`. |
| retained_state.cpp:407-443 | post-rebuild selection | — | Non-compact mode keeps the saved state only if its boundary is at or before the cursor. |
| retained_state.cpp:1207-1220 | `rebuild()` | R | In compact mode, `tail_facts()` is the **suffix after the boundary**; otherwise it is every fact. The ContextStore constructor also skips facts at or before the boundary (context.cpp:405-408). |
| retained_state.hpp:216-222 | `fact_count()`, `tail_facts()` | R | `fact_count = archived + tail`. `seed_successor` requires `fact_count()==0`. |
| main.cpp:552-564 | one-time `context.checkpoint()` after a full replay | P | Converts the session to saved-state form when there is no station state and no pending restart. |

### 6.4 History, lookup and paging (used by ContextStore originals, inspect and restore)

| file:line | Surface | Dir | Notes |
|---|---|---|---|
| retained_state.cpp:787-899 | `history_snapshot()` → `FactHistory{count, read}` | R | Ordinals below `archived` are read from the catalog and disk; later ordinals come from the tail copy. |
| retained_state.cpp:732-775 | `FactHistory::page(first, limit ≤ 16)` | R | **`visit_context_records` (context.cpp:283-311) scans every fact of every kind**, 16 at a time, and filters `channel==context`. So `originals()`, `inspect{history,index,originals}`, `captured_entries()` and managed `restore` each cost O(total journal facts). There is no channel or generation index. |
| retained_state.cpp:535-652 | `lookup(snapshot, family 1, application, identity)` | R | Scans the tail first, then `archive_->find`. Used by `original_entry` (context.cpp:537-548) only when `!archive_loaded_` and a saved state exists. The fact must then be in the saved boundary's journal, at a sequence at or before the boundary (561-564). **The archived-original locator is the entry ID itself** (record identity plus ordinal). |
| archive_catalog.hpp:13-46 | `ArchiveCatalog` | P/R | Immutable sorted 96-byte `{RecoveryKey, PhysicalJournalRecord}` entries bound to the header and boundary. Context-agnostic. |
| recovery_index.cpp | `RecoveryIndex` | R | Optional derived paged index. **[unconfirmed]** whether production sessions enable it. |
| retained_state.cpp:2658-2700 | `source(SourceReference)` | R | Reads dependency payloads, i.e. tool and provider originals (`AuditLog::original`). These are not context entries. Used by `read_file{snapshot}` and `read_process_output`. |

### 6.5 Checkpoint triggers and capacity gates

| file:line | Surface | Notes |
|---|---|---|
| retained_state.hpp:107-110; context.cpp:494-501 | `set_maintenance_checkpoint(fn)` | **One callback slot**: exactly one ContextStore owns checkpointing of a RetainedState. |
| retained_state.cpp:1672-1727 | maintenance trigger in `append_impl` | Compact mode, outside settlement, not re-entrant. Fires when tail facts plus sources reach 128 or tail bytes reach 8 MiB. On failure it retries after 64 more sequences. |
| retained_state.cpp:1728-1736, 1779-1797 | hard tail caps | Above 1024 records or 32 MiB since the boundary, **any append is rejected with `capacity`**. So a context that cannot be saved (for example a projection over 64 MiB, or unresolved operations) eventually blocks every append. |
| context.cpp:698-706 | `maybe_checkpoint` | Runs after each publication: ≥64 sequences or ≥4 MiB since the boundary. Failure is recorded only. |
| main.cpp:563, 1021, 1070, 1138; application.cpp:325 | explicit `checkpoint()` | Called on session switch, `/new`, `/checkpoint` and application shutdown. |
| journal_checkpoint.cpp:9-10, 59, 208 | `FramedJournal` `BBHINT1` | Physical frame hints only ("not a durable semantic/current-context snapshot"). |

### 6.6 Two unrelated "pending" mechanisms

| file:line | Surface | Notes |
|---|---|---|
| retained_state.hpp:30-33, 233-235, 343; retained_state.cpp:1897-1966, 2086-2152 | `RetainedState::pending_proposal_` | An in-RAM **journal batch** proposal held during append for capture and poison recovery. |
| context.hpp:142-147 | `ContextStore::pending_` | A staged CLM `manage` proposal. **Volatile**: the only durable trace is the `managed accepted=false reason="staged"` record (context.cpp:1439). It is lost on reopen. Replay treats that record as a rejection. |

They share only the name.

### 6.7 Diagnostics, provider originals and the trace archive

| file:line | Surface | Dir | Notes |
|---|---|---|---|
| context.cpp:187-209; diagnostics.cpp:165-281 | `provider.stream` → `store_diagnostic` | P | Raw stream bytes go to `diagnostics/<expires>.<id>` with a **30-day expiry**, not to the journal. The `log` record carries `{attempt, revision, capture_policy, diagnostic_file, expires_at, bytes, available}`. |
| coding.cpp:4570-4582 | `provider.request` log record | P | `{attempt, revision: origin head, generation: DefinitionGenerationId, input_binding, retry_group, ordinal}`. **This is the only durable request-to-context link besides DecisionEvent.context.** |
| audit.cpp:498-525 | audit `diagnostic` query | R | Reads the stream diagnostic file for a `provider.stream` log record. |
| provider_original.cpp:185-468 | `ProviderOriginalLease` | T | In-memory ownership of request and response bytes. Not persisted itself. |
| packet.cpp:67-68 | BBM2 static string table | — | Includes `provider.request` and `provider.stream`. **[unconfirmed]** whether new labels need table entries. |
| trace_archive_main.cpp:430-442; trace_archive_worker.cpp:12-75 | trace archive | P (remote) | Uploads `audit`, `diagnostics/*` and `raw/<run>/…`. **It does not upload `audit.state.*` or `audit.archive.*`**; both are derived and rebuildable. Session-directory scoped. |

### 6.8 Raw instrumentation in the retained layer

All descriptions are in `raw_retained.cpp:777-851`:

| Sites | Event |
|---|---|
| 3104/3105 | open (returns compact) |
| 3110/3111 | save_current_state |
| 3140/3141 | lookup |
| 3144/3145 | `FactHistory::page` |
| 3146/3147 | history_snapshot |
| 3148/3149 | per-ordinal read |
| 3154 | public page selection |
| 3220-3227 | saved load, slot, publish, root selection and unresolved record |
| 3232/3233 | resident retirement |
| 3252/3253 | current_programs |
| 3102 | RetainedState pending batch proposal |

These sites carry cursors and counts only. Context bytes reach raw only through sites 1813/1814.

### 6.9 Journal identity

- **One bounded journal per session.** `JournalHeader{environment, journal: AuditStreamId, issuer_namespace, limits{32 MiB payload, 96 MiB batch}, predecessor}` is created fresh for each new session, with capacity 512 MiB / 200,000 records (main.cpp:263). All context history of a session lives in this one journal.
- **IDs are journal-scoped.** `issue()` is namespace8 + counter8 (retained_state.cpp:2915-2925) and the counter is burned, never reused, across reopen. Context revisions and entry IDs are therefore unique within the journal namespace, not globally.
- **The saved state is bound to the header byte for byte.** Archived-original lookup requires the record's journal to equal the saved boundary's journal.

---

## 7. Audit, session, successor and backstop surfaces

### 7.1 Audit query layer (`src/audit.cpp`)

| file:line | Surface | Dir | Notes |
|---|---|---|---|
| audit.cpp:97-220 | `summary()` rows | R | Decodes only log and program packets of 64 KiB or less. **Context packets are deliberately not decoded** (125-128). Lifts `revision`, `generation` and `attempt` from metadata (151). |
| audit.cpp:165-172 | DecisionEvent summary | R | Shows actor, conversation, workflow, generation and `revision = e.context`. |
| audit.cpp:242-408 | `trajectory`, `variables` | R | Bounded scans of one journal ("committed prefix only"). Context records appear as `application` rows without content. |
| audit.cpp:410-585 | `AuditLog::inspect` | R | One record or a list. With `packet:true`, a context packet decodes only up to 64 KiB (476-477); larger ones are reachable as paged hex only. `source` reads a dependency original (451). `diagnostic` reads a stream file (494-531). |
| audit.cpp:579-580 | scope text | — | "record indices stable within this journal lineage". |

### 7.2 Session (`session.hpp`, `session.cpp`)

| file:line | Surface | Dir | Notes |
|---|---|---|---|
| session.hpp:13-17 | `SessionIdentity{actor, conversation, workflow}` | — | **One ConversationId per session.** No context-generation field. |
| session.cpp:384-430 | `session_identity()` | R/W | Order: latest `session.identity` program; else the last DecisionEvent; else fresh IDs. It always re-records `session.identity`. Called from main.cpp:568, 611, 972, 1383 and 1658. |
| session.cpp:114-155 | `save_session_info` | P (file) | `session-info.bbm`: settings plus actor, conversation and workflow. No context head. |
| session.cpp:157-382 | `list_sessions` | R | Session directories only. No context or lineage data. |
| session.cpp:431-519 | `SessionStore` ctor and `save` | R/W program | `session.settings` (model, effort, workflow, provider, name). **A model or provider change writes settings only; the context is untouched.** |
| session.cpp:521-556 | `SessionStore::restart` | W program | `session.restart{token, state:"pending", note}`. |
| session.cpp:557-616 | `SessionStore::resume(ContextStore&)` | R/W context and program | Scans **`tail_facts()` context packets for `origin == "rrc:"+token`** (575-586). If none is found, it appends the user item `continue\n\nRestart/resume note:\n…` labelled `rrc:<token>` (596-598), then records `session.restart{consumed}` (602-607). The idempotence check is coupled to the append `origin` field and to the tail after the saved boundary. **[unconfirmed edge]**: the append and the consumed record are not atomic. |

### 7.3 Backstop successor (`src/backstop.cpp`), the existing cross-journal lineage mechanism

| file:line | Surface | Dir | Notes |
|---|---|---|---|
| backstop.cpp:227-253 | destination `<session>/backstop` | P (dir) | Created by atomic mkdir as a **separate session directory and journal**. |
| backstop.cpp:254-278 | `lineage` | R | `{session, environment, journal, prefix_sequence, prefix_end, context_revision: source_engine.context().head(), status:"unsettled", reason}`. Matches the `validate_successor_seed` source keys. |
| backstop.cpp:279-303 | entry selection from `source_engine.context().view()` | R/T | Keeps all system and developer entries plus the **last user entry**, and drops assistant, reasoning and tool items. Refuses more than 64 KiB rather than truncating. Source IDs become locators. |
| backstop.cpp:304-354 | backstop packet | R | Last 32 attempt inputs (each ≤1 KiB) with the unknown-outcome flag and `replay_authority:false`. |
| backstop.cpp:363-373 | new `RetainedState::create` | P | Fresh environment and journal, `predecessor` empty. **No header link**; the lineage lives only in the seed. |
| backstop.cpp:374-386 | `ContextStore context{log}`; `seed_successor`; `append(developer, "backstop.instructions")` | W | The first context records of the new journal. |
| backstop.cpp:387 | `log.original("backstop.packet")` | P | Mission packet kept as an original, not as context. |
| backstop.cpp:405, 549, 613 | second `CodingEngine` on the successor context | — | Runs the assessment turns. |
| backstop.cpp 4700-4741 | raw session-call sources | — | 17 = context head, 20 = entry ID, 15 = role. |

### 7.4 Launcher (`src/main.cpp`)

| file:line | Surface | Dir | Notes |
|---|---|---|---|
| 224-231 | `--seed` → `validate_successor_seed` | R | New sessions only. |
| 293-331 | reopen: header check (predecessor refused), `RetainedState::open` | R | |
| 335-339 | `recover_coding_session` (coding.cpp:2805) | R/W | Provider and unknown-effect recovery before context use. **[not traced]** |
| 421-425 | `AuditLog log{*root}; ContextStore context{log}; seed_successor(seed)` | R/W | **One ContextStore per process and session journal.** |
| 448-457, 528-546 | provider chosen once per process; `CodingEngine engine{log, context, *provider, model}` | — | **The context carries over verbatim across a provider change at reopen.** |
| 520-521, 810-819 | `session_store.resume(context)` → `engine.turn("continue")` | W | RRC continuation. |
| 552-564 | one-time checkpoint after full replay | P | |
| 652-670 | `engine.context().changed` → `ui.context_changed` | — | Maps edited, restored, compacted (summarize or archive) and selected (select). A cut needs a kind of its own or a mode. |
| 995-1058 | `/session PATH` | P | `validate_session_switch`, `checkpoint` (1021), then re-exec. |
| 1059-1107 | `/new` | P | Checkpoint (1070), then a fresh sibling session with the same settings. **No context lineage.** |
| 1124-1160 | `/context`, `/checkpoint`, `/stats`, `/compact`, `/inspect`, `/originals`, `/restore` | R/W | Operator paths straight into ContextStore. `/compact` calls `manage` directly. |
| 1161-1169 | `/model NAME` | W program | `session_store.save` and `engine.model()`. **No context cut and no change to the input prefix.** This is the natural hook for a model-switch generation. |
| 1210-1219 | `/beads select` → `context.append(user, "operator.beads")` | W | |
| 1432-1444 | station publication `context_head = context.head()` | R → external | The head is exposed in the station status packet. |
| 1614-1635 | TUI reopen: `context.items()` → `ui.restore_message` | R | **Chat history on reopen is rebuilt from live context items**, not from audit history. |
| 1657-1659 | `CommandCenterContext{environment, conversation}` | R | The TUI and ActionInbox scope is keyed by ConversationId (tui_instrument.cpp:251, 490). |

### 7.5 Embedded application (`src/application.cpp`)

| file:line | Surface | Dir | Notes |
|---|---|---|---|
| 289-301 | `log.original("application.observations")` then `context.append(user item carrying the retained source id, "application.observations")` | P/W | **The existing precedent for a "shaped item plus locator to the full original".** |
| 325 | `engine.context().checkpoint()` at shutdown | P | |

### 7.6 Other raw and peer surfaces

| file:line | Surface | Notes |
|---|---|---|
| raw_turn.cpp:226-250 (site 1900) | `RawTurnTrace(running, backstop, generation, head)` | Records the program generation and the head text. |
| raw_turn.cpp:345-352 (1918) | `placeholder_request(interrupted, head)` | Stop placeholders. |
| raw_turn.cpp:107-170 (1905/1908) | `RawPolicyTrace` revision | A budget/program/tool revision, **[unconfirmed]** not a context revision. |
| raw_session.cpp:83-125 (4214) | actor, conversation, workflow | No head. |
| raw_audit.cpp:72, 125 | `predecessor_prefixes` | Linked prefixes exist at the raw journal layer, but the launcher refuses them. |
| colleague.cpp:170-245; colleague.hpp:7 | colleague request | Context comes only from an explicit `{id,text}` selection; each call is a fresh provider conversation. ContextStore is never read. |
| participants.cpp:1538-1551 | participant follow-up | Participant continuity is private (`participant-prior-answer:<run>`), separate from ContextStore. |
| token_usage.cpp:1085-1195, 1316 | `read_token_usage` | Reads raw runs only, keyed by raw request refs, model and provider. **There is no revision, conversation or generation key.** Coverage is "raw-prefix-only". |

---

## 8. Provider-facing coupling of context items (adjacent, read for assumptions)

| File:line | Surface | Notes |
|---|---|---|
| `coding.cpp:4280-4325` | request object | `input = items()`, `store:false`, `include:[reasoning.encrypted_content]`, `stream:true`, no `previous_response_id`. **Blackbird already resends the whole client-side context every request; there is no provider-side conversation ID.** |
| `responses_provider.cpp:20-22`, `anthropic_provider.cpp:313-314`, `openrouter.cpp:63-65` | reject `previous_response_id` | Provider-held conversation state is refused. A "provider-side conversation" today is just the prefix of the resent input. |
| `anthropic_provider.cpp:215-234` | response → items | Every block becomes an item with `anthropic_message_id` and `anthropic_content` (the exact native block, including thinking and its signature). |
| `anthropic_provider.cpp:359-430` | items → Messages | Groups by `anthropic_message_id`. Requires reasoning with `anthropic_content`; `function_call_output` becomes a `tool_result` (JSON-dumped when not a string; `is_error` if `output.error`); developer and system messages are prefixed. **Prior thinking blocks are resent verbatim. This is the binding that a client-shortened earlier tool_result breaks.** |
| `json.cpp:453-463` | `export_provider_request` | For non-Anthropic exports, a reasoning item carrying `anthropic_content` is **unsupported** (throws). Other `anthropic_*` fields are stripped. **A model or provider switch over a context that holds Anthropic reasoning fails today.** |
| `responses_provider.cpp:42-49` | portable Responses | Rejects non-empty `encrypted_content` and maps developer → system. |
| `request_cache.cpp:82-103` | `prepare_request_cache` | `reusable_input_items` = the count of context items (+1 for retained request state, `coding.cpp:4436-4445`). The stable-prefix cache assumes the context prefix is unchanged between requests. |
| `coding.cpp:4759-4791` | usage selection | `input_tokens`, `cached_tokens` and `reasoning_tokens` are kept in `usage_` (last request only) and passed to `observed_usage`. **[unconfirmed]** that they are durable anywhere keyed by revision; `token_usage.cpp` reads raw only (§7.6). |

---

## 9. Assumptions and coupling points for context generations

Each item states the current fact and the coupling it creates. It does not propose a design.

### 9.1 Identity, linearity and naming

1. **One linear revision chain per journal.**
   - Every context record must satisfy `observed == head` at replay, and accepted ones `base == head` (context.cpp:422-430).
   - `op` must be `append`, `edit` or `managed` (`:426`).
   - There is one `context` channel (retained_events.hpp:19), one `SavedState.context`, and one maintenance-checkpoint slot (§6.5).

   Coupling: a generation cut inside the same journal fits only as a publication that *continues* the chain (its base is the previous head). A parallel chain, or a new `op`, needs replay changes, an `op` whitelist entry and a saved-state discriminator.
2. **Head identity is reused as a typed ID.** `head_` is the hex `ApplicationRecordId` of the last published record. It is reinterpreted as a `ContextRevisionId` in every `DecisionEvent` (coding.cpp:3316, 3349-3356). It also appears in `provider.request`/`provider.stream` (`revision`), complaints (5266), the station packet (main.cpp:1432), the turn and placeholder raw traces, and the backstop lineage. A generation ID cannot ride in head text without changing all of these consumers. It needs its own field.
3. **The entry ID format `revision.index` is parsed in four places:**
   - `original_entry` (`:513-531`): the archive locator;
   - `validate_successor_seed` (`:813-817`);
   - `append_of` (`:1150-1153`): round grouping;
   - `inspect` index `capture_revision` (`:1775-1776`).

   `whole_rounds` relies on a response's output being one append (`coding.cpp:4803`), so that reasoning and its calls share one revision prefix. Re-capturing carried entries under a new revision (as `seed_successor` does, with `lineage.source_entries`) would break the "same prefix = same round" grouping unless the whole round is re-captured in one append.
4. **"generation" is already taken.** `DefinitionGenerationId generation_` (the Lua program and turn generation) appears under the key `generation` in `provider.request`, operation metadata, complaints, `RawTurnTrace` and the audit summary. A context-generation field needs a distinct name, or the existing keys need renaming.
5. **One ConversationId per session** (session.cpp:384-430). It is stored in every DecisionEvent and keys the TUI, ActionInbox and command-center scope (main.cpp:1658). Mapping each generation to a new ConversationId would split the UI and inbox scope. A separate generation ID under the same conversation matches "grouped under one Blackbird session".

### 9.2 What "provider-side conversation" means today

6. **There is no provider-side conversation object.** Every request sends the full `context_.items()` with `store:false`. `previous_response_id` is rejected by every adapter (§8). The provider binding therefore lives entirely in *which items are in `entries_`*:
   - Anthropic `anthropic_content` thinking blocks with their signatures;
   - Responses reasoning `encrypted_content`;
   - the `anthropic_message_id` grouping.

   A "new conversation" equals a request whose input no longer contains earlier-generation reasoning or thinking, with earlier tool_results left untouched (they are simply absent).
7. **Today's managed invariants conflict with a full cut.**
   - `mandatory-entry` (`:1416-1426`) forbids archiving or summarising user, system and developer items, and requires `select` to keep them. A cut that replaces old user prompts with a summary is rejected today.
   - `whole_rounds` and `whole_groups` forbid splitting rounds.
   - `publish_managed` force-keeps the trailing open tool group and its whole round (`:1556-1564`).

   A cut needs either a new mode or a relaxation of these rules (the PLAN lists "pinned instructions" and "last visible round" as cut content).
8. **An open tool call across a cut.** C6 wants compaction while a tool runs. A `function_call` appended in generation N, whose output is appended after the cut, must pair in generation N+1 (`validate_protocol`, coding.cpp:286-317; `protocol_complete`). On Anthropic, the `tool_use` lives in an assistant message grouped with that round's thinking block (anthropic_provider.cpp:359-419). Whether the new generation must carry that round's thinking, or must not, is a provider-profile question. **[open; not answerable from source]**
9. **Model and provider switching.** `/model` and `--provider` keep the context verbatim (main.cpp:448-457, 1161-1169). `export_provider_request` throws on Anthropic reasoning for non-Anthropic exports (json.cpp:457-459), and portable Responses rejects non-empty `encrypted_content` (responses_provider.cpp:44-46). A switch over a context holding reasoning fails today. This is the motivating case for "reuse: model switches".
10. **Retained request state.** For Anthropic, `retain_request_state()` appends one operational-state developer item per request that carries tasks or the trigger (coding.cpp:4404-4445). These items are role `developer`, so `mandatory-entry` pins them. A cut must decide whether they cross; today nothing can compact them away.

### 9.3 Activation, staging and protection

11. **Managed proposals activate only at `finish_workflow(success)`** while a workflow is open (`:1434-1444`, `:1495-1502`; `coding.cpp:6001-6002`).
    - The staged `pending_` is volatile: a crash loses it, and replay sees a `staged` rejection (§6.6).
    - Settlement carries later appends forward only if `basis` is still a prefix of `entries_` (`:1583-1592`).
    - `append_impl` retargets `pending_->expected` (`:988-989`), but `edit` does not, so any edit during the workflow causes `settlement-conflict`.

    C6 (mid-turn cuts) needs a new activation point. That point must also respect the in-flight request checks: `head() != origin` → conflict at `coding.cpp:4467` and `:4794`.
12. **`protect` and `cancellation_budget`.** Every publication calls `protect(next, pending, ref)` before writing (`:974-977`, `:1053-1056`, `:1436-1438`, `:1656-1658`) to reserve settlement capacity. `cancellation_budget` sizes only a `stop.linkage` append and a `settlement-conflict` rejection (`:1313-1344`). A new cut record that can arise on stop or settlement paths must be added to that model.
13. **Several entry paths bypass admission.** Lua `blackbird.manage/edit/append/restore` (coding.cpp:1640-1661) and the operator `/compact` and `/restore` (main.cpp:1146, 1159) call ContextStore directly, with no `operation()` admission and no DecisionEvent. The `context_manage` and `context_*` tools go through admission. A harness-initiated cut must pick one of these paths explicitly.

### 9.4 Persistence and replay

14. **Saved-state context is `{version:"1", base, entries, live_originals}`.**
    - ContextStore requires `version=="1"` exactly (`:349`).
    - `load_slot` tolerates extra keys, and RetainedState already adds its own keys to the same object.
    - Adding generation or lineage fields is possible additively, but a cleaner form is a version bump on both sides.
    - The file is capped at 64 MiB, with programs included.
15. **Originals reachability after a checkpoint.** `live_originals` covers live entries only. Older originals are reachable by archive lookup only in the same journal and at or before the saved boundary (`:561-564`), or from the tail.
    - Within one journal, earlier-generation originals stay retrievable by entry ID.
    - Across journals (the backstop or `--seed` model) there is **no** lookup path; the lineage is declarative only ("no inherited admissions or settlement verification", `:921`).
    - Linked journal segments (`predecessor`) are refused at open (main.cpp:303-307; retained_state.cpp:314-316).

    So keeping all generations in one session journal is the only option that needs no migration work.
16. **History cost is O(all facts).**
    - `visit_context_records` pages every fact (16 at a time) for `originals()`, `inspect{history,index,originals}`, `captured_entries()` and managed restore.
    - `read_process_output` linearly scans every fact (coding.cpp:5045).

    There is no channel or generation index, so per-generation views and fetches would rescan the whole session.
17. **Capacity gates.**
    - A `managed` record carries the full next `entries` plus fresh originals (`:1646-1655`), and an `edit` carries the full candidate. Both are bounded by the 32 MiB payload limit.
    - Compact-tail caps apply: a checkpoint at 128 records or 8 MiB, and append rejection at 1024 records or 32 MiB.
    - The saved state is capped at 64 MiB.
    - Journal capacity is 512 MiB / 200,000 records per session. Context compaction never reclaims audit history (coding.cpp:4549). Many generations in one journal draw on this one budget.
18. **Replay coupling to packet fields.**
    - `candidate` is projected out of replay.
    - The RRC idempotence check reads the append `origin` label `rrc:<token>` from tail context packets (session.cpp:575-586).
    - Backstop and `--seed` lineage reuse the `append` record with an extra `lineage` key.

    New cut records must keep these readers working.
19. **Archive retention.** The trace archive ships the journal and diagnostics only. `provider.stream` raw bytes expire after 30 days, while the parsed response items, including thinking blocks, are durable in context append records. Earlier-generation thinking therefore stays in the audit even when no longer sent, which matters for "not carried forward" in the audit-versus-input sense.

### 9.5 Accounting, cache and UI

20. **Prompt cache.** `reusable_input_items` assumes an append-stable prefix of context items (coding.cpp:4436-4466; request_cache.cpp:82-130). A cut, an edit or a manage invalidates it. No cache key ties to a revision or generation.
21. **Usage accounting.**
    - `usage_` holds the last request only.
    - `token_usage` reads raw runs with no revision, conversation or generation key.
    - `provider.request` carries `revision`.

    The natural place for per-generation accounting is the `provider.request` log record and the DecisionEvent, not ContextStore.
22. **Presentation.**
    - `ContextChange` has three kinds; main.cpp:661-670 maps them to edited, restored, compacted and selected.
    - TUI chat restore on reopen reads `context.items()` (main.cpp:1616). After a cut, reopened chat shows only the new generation's live user and assistant text.
    - The station packet exposes `context_head`.
    - Stats, budget and `CONTEXT_SIZE` measure the BBM2 bytes of `items()`.

### 9.6 Tool-output lifecycle coupling

23. **Two meanings of "original".**
    - ContextStore originals are the items *as appended* (each entry's first capture), so `context_restore` and `restore` mode bring back the shaped item, not the raw tool output.
    - The raw tool output is a separate `log` record with a source dependency, written through `LocalTools`'s observer (`file.read`, `process.output`, …) at coding.cpp:5536-5551, keyed by `attempt`.

    Shaping at creation adds a third layer between them.
24. **Shaping today lives in Lua turn programs**, not per tool kind in native code.
    - `programs/turn.lua:13-25` injects `output_max_bytes=16384` for `exec` and unranged `read_file`.
    - The built-in `ultracode` workflow (workflows.cpp:291-305) passes results unshaped.
    - Each result is appended verbatim as `function_call_output.output`.
25. **Existing locators are journal-scoped.**
    - `output_ref` is an attempt hex resolved by a linear log scan.
    - `snapshot{journal, sequence}` is a `SourceReference`.
    - `application.observations` (application.cpp:289-301) is the only current "item carries locator to full original" example.

    All of them resolve only within the current journal, consistent with §9.4 item 15.
26. **Context edits keep item IDs.** `edit` accepts changed item content under an existing ID; only ID existence is checked (`valid_entries`). So a "client edit to an earlier tool_result" is exactly what `context_edit`, `restore` and managed summarisation do today. Those are the operations the Anthropic thinking-binding rule forbids within one provider conversation.

### Unconfirmed items (collected)

- Uniqueness of issued IDs across journals (namespace8 randomness assumed, not verified).
- Whether RecoveryIndex is enabled in production.
- Whether BBM2 static-table entries are needed for new labels.
- Non-atomicity edge of the RRC append versus the consumed record.
- `RawPolicyTrace` revision semantics.
- `recover_coding_session` context effects (coding.cpp:2805; not traced).
- Durable per-revision usage (none found).

## Correction: replay is playback (2026-10-19)

The summary above says a cut "fits only if it continues the chain; anything else needs replay changes". That
framing is wrong, and the operator corrected it: replay plays back a recording; it does not repeat the action.
`src/context.cpp:405-463` confirms this:
- Accepted `edit` and `managed` records carry the full resulting `entries`, and replay installs them directly
  (`entries_ = entries.array()`, :458-461).
- Appends add their recorded delta (:447-456).
- Replay never re-runs selection, round atomicity, pin rules or any other management logic.
- The `revision`, `observed` and `base` comparisons (:422-431) are integrity checks that each recorded frame
  follows the previous one.
- The `op` allow-list (:426) only names which frame kinds the reader knows.

So a generation cut recorded as a frame that carries its resulting entries plays back with no new replay
logic. The real constraints sit at write time: `manage` rules (pinned user, system and developer entries;
whole rounds; the open tool group) and when staged changes take effect. A brand-new frame kind would only
need the reader to recognise its name. `valid_entries` additionally requires every live id to resolve to a
retained original, which archival satisfies.
