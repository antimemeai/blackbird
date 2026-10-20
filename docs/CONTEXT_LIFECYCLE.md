# Context lifecycle: chapters, selective archival and shaped tool output

Design for bead `arconaut-rd7.1` (epic `arconaut-rd7`), branch `work/clm-a1v-2026-10-19`.
Status: revision 3, final. Revision 2 answered the adversarial tribunal
(`papers/clm-a1v-2026-10-19/design-tribunal/`); revision 3 applies the single recheck
(`design-tribunal/recheck.md`), which under docs/BOUNDED_HARDENING.md is the last hardening
layer. Appendix A records every disposition. The plan and its review follow. No source has
changed for this design.

Source references are `file:line` at `ae81fbe` unless marked otherwise. `ae81fbe` carries
o17's tribunal fixes: only the operator's outermost call stays exact, the DecisionEvent
continuation records the `native_defaults` keys Blackbird supplied, and a head alone keeps
the 6144-byte tail. `src/context.cpp`, `src/saved_state.cpp`, `src/retained_state.cpp`,
`src/main.cpp`, `src/request_cache.cpp`, `src/anthropic_provider.cpp`, `src/json.cpp`,
`src/responses_provider.cpp`, `src/openrouter.cpp`, `src/session.cpp` and `src/backstop.cpp`
have not changed since `f7a79b1` (the recheck confirmed `git diff --stat f7a79b1 ae81fbe` lists
none of them). `src/coding.cpp`, `src/tools.cpp`, `src/command_jobs.cpp` and
`include/blackbird/coding.hpp` were re-read at `ae81fbe`.

Provider documentation quoted here comes from the research notes in
`papers/tool-output-lifecycle-2026-10-19/research_notes/` and from the provider tribunal
(`design-tribunal/provider_protocol.md`, primary pages read 2026-10-20). Each quote names
its page.

## Contents

1. Inputs
2. What the source does today
3. Vocabulary and identities
4. Frames, playback, snapshots and recovery
5. Units, classes, pins and the archive note
6. Cut kinds, hidden reasoning and the provider prefix
7. Provider turns, the closing reply and the turn boundary
8. Staging and settle points
9. Tool output: shaped once, at creation
10. Inspection that does not linger
11. Model-facing contracts
12. Escalation: inform, nudge, direct, fail
13. The archival step (C6a, C6b)
14. Overflow recovery (C4)
15. Provider profiles
16. Cache economics
17. TUI and operator commands
18. Signals for trials
19. Other request makers and session paths
20. Testing
21. Delivery units
22. Rejected alternatives
23. Decisions taken in this draft
24. Decision record: the Anthropic step restriction
- Appendix A. Tribunal dispositions and recheck fixes

## 1. Inputs

Read in full:
- `~/projects/BLACKBIRD.md`, `AGENTS.md`, `docs/BOUNDED_HARDENING.md` (through the
  coordinator's direction), `docs/INTERACTION_THEORY.md` (through the doctrine tribunal's
  quotations).
- `papers/tool-output-lifecycle-2026-10-19/reports/Tool output lifecycle in agent context.md`,
  with its "Recheck against 10827cf"; `provider_mechanisms.md` Q1-Q5 in the research notes.
- `papers/clm-a1v-2026-10-19/`: `PLAN.md`, `C6_DESIGN.md`, `C6_SOURCE_MAP.md`,
  `OVERFLOW_STUDY.md`, `C5_LIVE.md`, `JEV_PILOT.md`, `REQUEST_SIZES.md`, the three
  `context-surfaces/` catalogues, the three `design-tribunal/` reports and `recheck.md`.
- `docs/CONTEXT_BUDGET.md`, `docs/ECONOMICAL_DEFAULTS.md`, `docs/TOKENOMICS.md`,
  `docs/INSTRUMENTATION.md`, `docs/CORE_DESIGN.md` (CLM and generations),
  `docs/CONCURRENT_DURABLE_WORKFLOWS.md` (skimmed).
- Beads `arconaut-rd7`, `rd7.1`, `rd7.2`, `rd7.3`, `a1v` and `a1v.1`-`a1v.6`, `nls.16`,
  `nls.17`, `5st`, `bvu`, `2o9`, `c3f`, `7c8`, `o17`, `ib8` and `ib8.1`-`ib8.5`.
- Commits `870ec8c` (o17) and `ae81fbe` (o17's tribunal fixes).
- The staged 5st table in `context/defect-5st/` as profile source material only.

Not available: the three further context-surface sweeps (provider adapters, tool dispatch,
other request makers) never landed as files. §2-§4 of `request_provider_path.md` and its
recheck cover that ground, and the changed files were re-read at `ae81fbe`.

## 2. What the source does today

**The provider input is the whole live context, rebuilt every request.**
- `request()` validates pairing, drains parked workflows, then takes
  `items = context_.items()` and `origin = context_.head()` (src/coding.cpp:4318-4323).
- Program options replace or add any request field, `model`, `tools` and `input` included,
  after that point (src/coding.cpp:4407-4416).
- The request is stateless: `parallel_tool_calls=false`, encrypted reasoning included,
  `store=false` (src/coding.cpp:4366-4368), and `store`/`stream` are enforced
  (src/coding.cpp:4437-4439). Every adapter refuses `previous_response_id`
  (src/responses_provider.cpp:20, src/anthropic_provider.cpp:313, src/openrouter.cpp:63).
- The head must not move during assembly or dispatch (src/coding.cpp:4515, 4842). The
  response round enters context as one append labelled `provider.branch:<origin>`
  (src/coding.cpp:4851).
- Assembly writes engine state: on a retaining provider it appends the operational state to
  context and moves `origin` (src/coding.cpp:4484-4488); it resets `usage_` and
  `last_request_bytes_` (src/coding.cpp:4524-4525); it clears `previewed_`
  (src/coding.cpp:4604).
- No request is made while a restart is scheduled (src/coding.cpp:4315).

**The Anthropic adapter renders entries into messages.** Consecutive user-role blocks
(tool results, user text, developer and system items rendered as user text) merge into one
user message; a new message starts only when the role changes or the assistant group changes
(src/anthropic_provider.cpp:346-351, 428-433). The adapter accepts `tool_choice` `auto` and
`none` (src/anthropic_provider.cpp:479-485).

**Turns can end with tool outputs at the tail.** `programs/turn.lua` ends with
`error(...)` after 64 steps and returns while calls are outstanding when a restart is
scheduled (programs/turn.lua:45-51); the built-in `ultracode` loop does the same
(src/workflows.cpp:292-305).

**The context is one linear chain of records in one journal.**
- Replay accepts only `append`, `edit` and `managed` frames (src/context.cpp:426) and checks
  `revision`, `observed` and `base` (src/context.cpp:420-431). It extends entries for
  `append-delta-v1` and installs the recorded `entries` of other accepted frames
  (src/context.cpp:447-460). The `format` branch refuses any non-append format
  (src/context.cpp:447-450) and the originals branch serves only `append` and `managed`
  (src/context.cpp:432). It never re-runs management rules.
- Entry IDs are `<record revision>.<ordinal>` (src/context.cpp:946-952). A revision is
  `hex_identity` of a 16-byte ID whose bytes 0-7 are the journal's issuer namespace and bytes
  8-15 its counter, both little-endian (src/retained_state.cpp:2873-2926, per the doctrine
  tribunal). Each successor segment has its own namespace with its counter starting at 0
  (src/retained_environment.cpp:571-573, 633-634, per the recheck); the launcher refuses linked
  journals (src/main.cpp:303-307).
- A managed publication records the full next `entries` list (src/context.cpp:1646-1656).

**Snapshots.**
- The saved context is `{version: 1, base, entries, live_originals}` (src/context.cpp:645-648).
  Two readers check its shape separately: `load_slot` (src/saved_state.cpp:143-153) and the
  ContextStore constructor (src/context.cpp:347-363).
- When the constructor rejects a snapshot it clears head, entries and originals and leaves the
  boundary at 0 (src/context.cpp:367-376), then replays `tail_facts()` (src/context.cpp:405-488).
  In compact mode that is only the suffix after the saved boundary
  (src/retained_state.cpp:1207-1220), and RetainedState never learns that the constructor
  refused the snapshot. The engine then appends `initial` to an empty head
  (src/coding.cpp:2654-2655): a silent empty context (state tribunal H1).
- RetainedState's maintenance trigger can run `ContextStore::checkpoint` inside any append
  (src/retained_state.cpp:1672-1727; registered at src/context.cpp:494-495), and a checkpoint
  prunes `originals_` and clears `captured_` (src/context.cpp:670-679).
- The launcher's automatic full replay is limited to 64 MiB (src/main.cpp:310-326), a guard
  that applies to RetainedState only.
- `current_programs` keeps only an enumerated set of latest-wins labels
  (src/retained_state.cpp:2983-2990).

**Write-time rules.**
- `manage` refuses partial tool groups and rounds (src/context.cpp:1412-1415; `whole_rounds`
  :1157-1169, `whole_groups` :1170-1199). `whole_groups` checks contiguity by position.
- Every user, system and developer entry is pinned (src/context.cpp:1416-1426).
- One proposal may be pending (src/context.cpp:1369-1370). Inside a workflow it is staged as
  `managed accepted=false reason=staged` with the proposal in `candidate`
  (src/context.cpp:1435-1443, 1226-1231). Replay projects `candidate` out
  (src/context.cpp:414-420) and treats the staged record as a rejection.
- `record_boundary` batches exactly one context packet with one program packet
  (src/context.cpp:94-149).
- `edit` publishes whenever `base == head_`, even inside a workflow, with no pin check
  (src/context.cpp:1018-1076); `restore` publishes through `edit` (src/context.cpp:1077-1108).
  The model tools and Lua forms call them directly (src/coding.cpp:5615, 5619-5622, 1648,
  1654-1657).
- `finish_workflow(true)` is the only activation point for a staged proposal
  (src/coding.cpp:6119); failure rejects it (src/context.cpp:1452-1517). The turn-start reserve
  is sized with an empty proposal (src/coding.cpp:6042-6043).

**Identity.** `generation` names `DefinitionGenerationId`, re-issued every turn
(src/coding.cpp:6059) and written into `provider.request` (src/coding.cpp:4619-4630) and
operation metadata (src/coding.cpp:3362). `SessionIdentity{actor, conversation, workflow}` is
fixed per session (src/session.cpp:384-430); the conversation ID is OpenAI's `prompt_cache_key`
(src/coding.cpp:4512-4514) and the TUI scope.

**Context tools copy the context into itself** (a1v.4): `context_view` returns the whole view
(src/coding.cpp:5613-5614), `context_originals` every original (src/coding.cpp:5617-5618),
`context_restore` the whole view (src/coding.cpp:5619-5622), `context_edit` takes the whole
candidate (src/tools.cpp:1380-1392), `context_inspect` returns hex (src/context.cpp:1820-1829;
src/tools.cpp:1414-1430). Lua `append` and `restore` return the whole view
(src/coding.cpp:1654-1663). In C5 two `context_view` outputs were 646 KB of a 787 KB input.

**Guidance names a size goal**: "propose managed context with context_manage or Lua toward
target_bytes" (src/coding.cpp:4462-4466). On a retaining provider the whole operational state
is appended to context every time it is sent (src/coding.cpp:4484-4488).

**Hard-coded provider rules**: the cache allowlist (src/request_cache.cpp:87-93) and TTL
(src/request_cache.cpp:115); the OpenAI cache route gate (src/coding.cpp:329-340); the virtuals
`supports_prompt_cache_policy`, `supports_prompt_cache_key`, `retain_request_state`
(include/blackbird/coding.hpp:33-37; include/blackbird/anthropic_provider.hpp:15); the
`session-id` header (src/openai.cpp:405-411); the retry set (src/coding.cpp:4727-4737);
Anthropic effort and thinking rules (src/anthropic_provider.cpp:289-299, 486-510) and stop
reasons (src/anthropic_provider.cpp:202-209: `model_context_window_exceeded` becomes
`incomplete`, and calls under a stop reason other than `tool_use` are `corrupt`); the portable
effort set (src/responses_provider.cpp:24-29); `route_efforts` (src/main.cpp:466-496); the
engine effort set (src/coding.cpp:5769-5772).

**Shaping at creation since o17.** `default_observation_budget` shapes `exec`,
`process foreground` and unranged `read_process_output` to 8192 bytes (2048 head, 6144 tail)
and unranged `read_file` to 16384 bytes (src/coding.cpp:2099-2134). Only the operator's
outermost call stays exact (src/coding.cpp:4972-4974), and the supplied keys are recorded as
`native_defaults` (src/coding.cpp:3363-3368). Workflow-continuation results are appended by
`drain_workflows` (src/coding.cpp:4281-4288), not through `operation()`.

**Tools change during a session.** `request_tools()` appends the active Lua tools to the native
definitions (src/coding.cpp:2374-2386), and pending Lua tools activate at a successful
workflow boundary (src/coding.cpp:6112).

**Usage.** The provider attempt's response, usage included, is retained as its
`operation.result` original (src/coding.cpp:3940-3943). The Anthropic adapter folds cache
writes into `input_tokens` (`request_provider_path.md` §2.5).

## 3. Vocabulary and identities

**Chapter.** A context generation is a *chapter*: a run of provider requests whose whole
provider prefix (`tools`, `instructions` and `input`) extends by appends only, so the provider
sees one conversation, one cacheable prefix and one binding for hidden reasoning. A *cut* ends
a chapter and starts the next.
- A chapter is named by the revision of the `cut` frame that started it, an existing unique
  record identity; no new ID type is issued. `chapter_ordinal` (1, 2, 3…) is for people.
- "Chapter" stays out of model-facing text (§11).

**Conversation stays constant.** `ConversationId` keeps scoping the TUI and command center and
keying OpenAI cache affinity.

**Session and journal.** Every chapter lives in the session's one journal. Entry IDs and
locators are journal-scoped.

**Unit.** What the model selects. A *round* is the reasoning, message and `function_call` items
of one creating record, plus every output that answers those call IDs, wherever the outputs
sit. Membership is by call ID, not by position. Entries that belong to no round (instructions,
results, state items, notes, summaries) are one-entry units.

**Handle.** Every unit has a short handle, `r` plus the decimal issuer counter of its creating
record: `r4821`. A record that created several units gives each `r4821.k`. The handle needs no
table and survives reopen and playback. It is unique because a session has one journal with
one issuer namespace, which holds while the launcher refuses linked journals
(src/main.cpp:303-307). If linked segments are ever allowed, a segment-qualified form
(`s2:r4821`) is required, because each segment's counter restarts at 0. Full IDs stay in
records.

**Provider turn.** What the provider counts as one turn, evaluated on the provider-visible
message sequence the adapter sends, after merging (§7.1). The *current turn* is everything
after the latest turn start; the *turn state* at the tail is `closed` or `open`.

**Cut position `p`** is the encoded byte offset of the first entry a cut changes. The
*rewritten region* is everything at or after `p`; the provider re-reads it uncached once.

"Compaction" names only the failure path: history replaced by a summary (§14 option B).

## 4. Frames, playback, snapshots and recovery

### 4.1 The chapter rule

Within a chapter the provider prefix changes only by appends. Every other change of `input`,
`tools` or `instructions` is a cut and starts a new chapter.

Grounding:
- Anthropic's prefix check covers "the top-level `system` prompt; the set of `tools`; every
  `message` before the block". It lists as invalid: editing, reordering or deleting earlier
  messages; "clearing or shortening an earlier `tool_result`"; "Add, remove, rename, or edit a
  tool in `tools`"; "Change the top-level `system` string". Valid: appending, removing thinking
  blocks from the start, from the end or all of them, and changing `cache_control` markers
  (Preserved thinking).
- Every documented cache is exact-prefix (report; provider tribunal "Verified").
- So a non-append change costs a rewrite from `p` everywhere, and on prefix-checked Anthropic
  models it is valid only with the reasoning rule of §6.

### 4.2 The `cut` frame

One record on the context channel (include/blackbird/retained_events.hpp:19). It carries
references and new entries, never a copy of retained entries.

```
op          "cut"
format      "cut-delta-v1"
revision, base, observed, accepted
kind        start | archival | rebind | prefix | recovery
changes     set of: archive, edit, restore, summary, compaction, boundary
chapter     {ordinal, previous: revision | null,
             binding: {provider, protocol, base_url, account, model,
                       profile: {id, revision, digest},
                       tools_digest, instructions_digest}}
archived    [ref]       units the model or the recovery chose, expanded to entries
swept       [ref]       derived entries removed in the rewritten region (§5.6)
reasoning   [ref]       reasoning entries dropped by the profile rule (§6.2)
replace     [{ref, item}]
insert      [{anchor, fresh | existing}]   anchor = "start" or a ref;
                        fresh = index into originals; existing = a ref removed by this frame
originals   [{item, class}]   every entry this frame creates; its ID is <revision>.<index>
refused     [{unit, reason}]  units of the settled proposal dropped at settlement
pending     {proposal, basis} | null   the pending proposal after this frame (§8.3)
result      {count, crc32c, encoded_bytes}
sizes       {before, prefix}
cause       {source: model | step | operator | recovery | rebind | prefix | start,
             attempt, stage, forced?}
```

- A `ref` is a native pair (16-byte record identity, u32 ordinal), not a hex string.
- `changes` is a set, because one `context_manage` call may archive, restore and summarise
  together. A selection that both archives and restores the same unit is refused before staging.
- `result` gives the entry count, CRC32C over the ordered refs (the owned CRC32C,
  src/journal.cpp:95-107) and the encoded bytes of the resulting items. Count and CRC32C are the
  playback check; `encoded_bytes` is recorded only, so playback of old frames is not tied to the
  exact encoder.
- Sizes are measured. No ratios, durations or reclassifications are written
  (docs/INSTRUMENTATION.md). The profile values applied are identified by
  `binding.profile.digest`.
- `pending` makes multi-part settlements atomic: a settlement that leaves a remainder, a rebind
  that rebases a pending proposal, and a merge all record the resulting pending proposal inside
  the same frame (§8.3). Nothing depends on a second record being written.

**Staged, settled and refused frames.**
- A staged cut is `cut accepted=false reason=staged` carrying `proposal` (held as units:
  creating-record refs plus the selection text, under that name so replay keeps it) and
  `basis {count, last, observed}`, a reference to the entries at staging, never a copy.
- Every frame that settles, rebases, merges or cancels a pending proposal names its stage.
  Refusals that clear it are only `settlement-conflict`, `invalid-tool-protocol`, `units-gone`
  and `cancelled`. A turn rule that does not permit settlement clears nothing: the proposal
  stays pending. A refusal of an unrelated request names no stage.

### 4.3 One apply function

The writer and playback call the same `apply(frame, entries) -> entries`. The writer builds the
frame, calls `apply`, measures `result` from the output and records the frame; it never computes
the next entries any other way.

`apply` in order, each step refusing with `corrupt` when it does not hold:
1. `archived`, `swept` and `reasoning` are disjoint and every listed ref is present.
2. Remove them.
3. Each `replace` names a present ref; its item replaces the item under the same ID.
4. Apply `insert` in listed order. `anchor = "start"` puts the entry at position 0; otherwise
   the anchor must be present at that moment, which may be an entry inserted earlier in this
   frame. Each entry goes immediately after its anchor. No two inserts name the same anchor: the
   writer chains them. An anchor removed by this frame is absent and refuses. When an insert's
   natural anchor is removed by the same frame (an archive note or boundary placed after the
   last entry, which the archive also removes), the writer anchors on the last surviving entry
   before it, chained after any insert already on that entry.
5. `fresh` inserts `originals[i]` with ID `<frame revision>.<i>` and the class recorded beside
   it; every original is inserted exactly once, so `original_entry` resolves it after a
   checkpoint (src/context.cpp:513-581). `existing` re-inserts an entry this frame removed,
   keeping its ID, original and creating record, so a reordered unit keeps its handle and class.
   Only whole units may be re-inserted, and the writer re-validates the result as whole rounds.
6. The result's count and CRC32C equal `result`.
7. `valid_entries` holds (src/context.cpp:232-249).

Inserted originals and re-inserted entries count as lying at or after `p` for the reasoning
rule: a restored or moved round's reasoning is dropped under `drop_after_change`.

**Memory safety around the record call.** The maintenance trigger can checkpoint inside any
append and prune `originals_` (§2). The cut writer therefore holds no references or iterators
into `originals_` or `captured_` across the record call: it builds the frame and the next entry
list from values it owns, records, and only then merges the new originals. A checkpoint that
fails stays best-effort, as `maybe_checkpoint` is (src/context.cpp:698-706), and never fails the
cut.

**Playback is new code.** Replay gains a `cut` branch that calls `apply`, extends the originals
branch to `cut` frames, restores pending state from staged frames and from a frame's `pending`
field, clears it on frames naming its stage, and advances a pending cut's expected head across
appends with the rule in `append_impl` (src/context.cpp:988-989). Legacy `edit` and `managed`
frames keep playing back as now, and staged `managed` records in old journals keep replaying as
rejections.

### 4.4 Snapshots

Snapshots are written at clean stops and by the existing maintenance triggers, never per cut: a
per-cut snapshot would copy the whole context on disk, which §4.2 refuses in the journal. Clean
stops (quit, exit, session switch, application shutdown) all write a snapshot through unit S0
(§21, which delivers 7c8); today `/quit` writes none (7c8).

The saved context becomes version 2: `{version: 2, base, entries, live_originals, sections}`.
`sections` is a list of named sections. Each section's shape, validator, writer and reader
belong to the unit that owns its state (§21), so the snapshot never carries a field no unit
fills:

| Section | Contents | Owner |
|---|---|---|
| `classes` | live creating records and their one-byte class; seeded grouping (§5.2) | S2 |
| `chapter` | current chapter revision, ordinal, binding, and the chapter table `[{revision, ordinal, kind}]` | S3a |
| `pending` | `{proposal, basis, expected, stage}` (§8.3) | S3b |
| `review` | `{last_round, next_review_rounds}` (§12.3) | E1 |

- The chapter table stores revisions; a cut frame is read by identity through
  `RetainedState::lookup` and the archive catalogue, as `original_entry` reads originals
  (src/context.cpp:537-548).
- **One validator.** `validate_saved_context` is defined once and called by `load_slot`
  (replacing src/saved_state.cpp:143-153) and by the ContextStore constructor (replacing
  src/context.cpp:347-363). It validates the core fields itself and each section through the
  validator its owner registers, and accepts versions 1 and 2. A section with no registered
  validator is refused. RetainedState and ContextStore can no longer disagree about a snapshot.
- A version-1 snapshot has no `classes` section. On first load the constructor reads each live
  creating record's packet `origin` by identity lookup and derives the classes; a record that
  cannot be read is classed by role, today's rule. The first request then writes a `start` frame.

### 4.5 Resume and recovery

| Situation | What happens |
|---|---|
| Clean stop, snapshot accepted | restore the snapshot; the suffix is empty |
| Unclean stop, snapshot accepted | restore the snapshot and play back the suffix after its boundary. This is ordinary playback of a short tail, recorded as a recovery event and shown to the operator (7c8's notice), not whole-history playback |
| Snapshot rejected by the shared validator | whole-history playback from fact 0 through `history_snapshot()`, which reads archived facts from the catalogue (src/retained_state.cpp:787-899), recorded as a recovery event. It follows the launcher's existing policy: automatic up to 64 MiB of context frames, beyond that only with `--rebuild-session` (src/main.cpp:310-326). The engine never appends `initial` over a head playback has not established |
| A frame does not apply | the read-only state below |

A clean stop is recognised by the suffix: a snapshot whose boundary equals the journal end on
reopen. Nothing in recovery sends a request, runs a tool or selects anything.

**The read-only state** (state M11, recheck B9). Playback stops at the last good frame and the
session opens read-only: no requests, no context writes, and **no snapshot or maintenance
checkpoint**, including at the operator's quit, so nothing can record a state past the bad frame
without the operator's repair. The bad frame's revision and error are shown. The operator may
approve a `repair` frame. It names the skipped revisions (the bad frame and every later context
frame chained on it), has `observed` and `base` equal to the last good head, and may carry
`insert` entries the operator picks from the skipped frames' originals; other skipped originals
stay readable through audit inspection. Playback first collects every `repair` frame's skip
list, skips those revisions and continues from the repair frame. Snapshots resume after the
repair frame is accepted.

### 4.6 Lineage

Each chapter names its predecessor and the frame that started it; the frame names its cause. Every
provider request names its chapter (§18).

## 5. Units, classes, pins and the archive note

### 5.1 What can be selected

The model selects units by handle, range or filter (§11.1). The engine expands the selection to
explicit units, and the frame records the expanded refs.
- A unit is whole by construction. `whole_rounds` and `whole_groups` keep binding the model's
  selections; `whole_groups` checks membership by call ID instead of contiguity, so a call and an
  output separated by an instruction can be archived without the instruction.
- An open round (an unanswered call) is never selectable.
- Pinned units are never selectable.
- Instruction units are never selectable, with one exception from §23 decision 2: an instruction
  unit whose bytes equal a later live instruction unit is a *superseded copy*, flagged
  `superseded` in rows, and the model may archive it. The harness never sweeps it, and the newest
  copy stays protected.

### 5.2 Classes

The class belongs to the creating record, not to the entry, so editing an entry cannot change
it. ContextStore keeps a resident map from live creating revision to a one-byte class, built at
append and at playback from the recorded packet, and saves it in the `classes` section. `origin`
or `class` keys in edit candidates and deltas are refused.

| Class | Creating record | Selectable by the model |
|---|---|---|
| instruction | `operator`, `rrc:*`, `backstop.instructions`; a Lua append that declares `class = "instruction"` (built-in workflows declare it for their briefs) | no, except superseded copies (§5.1) |
| round | `provider.branch:*`; a Lua append of `function_call` items; seeded rounds | yes |
| result | `workflow-continuation`; `operator.beads` (its text says "external data, not instructions", src/main.cpp:1212-1219); `application.observations`; a Lua append of any other item, the default for model-authored programs; items a `context_edit insert` creates, whatever their role | yes |
| state | `provider.request-state`; the closing reply's note (§7.2) | yes; swept in the rewritten region |
| summary | summaries a cut creates | yes |
| note | archive notes and turn boundaries a cut creates (§5.5, §7.3) | archive notes yes; the newest boundary no |
| tool state | a retained tool-change system message (§6.4) | no; replaced only by a later prefix change |

- Outputs appended as `lua.synthetic`, `workflow.protocol-repair` or `stop.linkage` join the round
  of the call they answer.
- `blackbird.append(items, {class = "instruction" | "result"})` lets a program declare what it
  appends; the default is `result`. The built-in `ultracode` workflow passes `instruction` for
  its brief (src/workflows.cpp:291).
- **Seeded sessions.** `seed_successor` appends a whole seed as one record
  (src/context.cpp:913-924). The class map groups seeded entries by the source revision prefix in
  `lineage.source_entries`, so each source round is its own unit, and classes seeded user, system
  and developer entries as instructions, by role.
- **Legacy records** without a recoverable label are classed by role, today's rule.

### 5.3 Who may change instructions and pinned units

The model and Lua may not archive (except superseded copies), replace, remove or move
instruction units, tool-state units or pinned units; `context_edit` and `blackbird.edit` refuse
such operations with `protected-unit`. Operator commands may, and only as cuts that settle under
the same rules (§8). The tool descriptions say so (§11).

### 5.4 Pins

`context_manage {pin, unpin}` records pins at once in a latest-wins program record
`context-pins-v1 {units}`, added to the latest-wins set in `current_programs`
(src/retained_state.cpp:2983-2990). A pin changes no context bytes.
- **Rechecked at settlement.** A pending cut that contains a unit pinned after staging settles
  without it; the settle frame lists it in `refused` with reason `pinned`, and the pin call's
  result told the model "r12 is in your pending archive and will be kept".
- A pin protects a unit from the model's own later archives and edits and from overflow
  recovery's windows (§14).
- No pin ceiling (§23 decision 3). `pinned_bytes` is visible in `CONTEXT_SIZE`, `context_view` and
  the step brief.
- A cut never writes the pins record.

### 5.5 The archive note

An archival cut inserts one note at the position of its earliest archived unit, so every byte
before `p` stays identical. It lists what left, with the action trace the masking literature
assumed the model keeps:

```
[Blackbird archive note] These units left your live context; nothing was summarised.
Read any of them with context_inspect {unit: "r4821"}.
r4821  exec       "cmake --build build/debug"            exit=0   8.0K of 1.2M
r4870  read_file  "src/context.cpp" lines 1-400          16.0K
r4902  message    "The replay loop rejects frames whose…" 0.6K
```

- Columns: handle, tool or kind, argument head (the tool's declared head argument, or the first
  60 bytes of message text), outcome flags, bytes shown and original bytes where known. Heads are
  cut on character boundaries with `observation_prefix_size` (src/tools.cpp:209), at 60 bytes.
- The handle is the locator. `context_inspect` maps it to the kind's original (`process.output`,
  `file.read`, `operation.result`).
- The note itself is created by the cut and carries a handle like `r5100.0`.
- **Placement and role** come from the profile (§15). On Anthropic the note is rendered as user
  text after the tool results of the preceding user message; that is Blackbird's own rendering
  of developer items (src/anthropic_provider.cpp:421-445), valid because "any text must come
  AFTER all tool results" (Handle tool calls). A profile whose rendering is a real
  `role: "system"` message sets `placement = "after_user_turn_before_assistant"`. Where a note may
  not follow a tool output (Mistral's documented flow table), it moves to just before the next
  retained user entry. It is never assistant-role.
- **Later cuts** absorb an older note that lies in their rewritten region; notes before `p` stay.
  `context_view {scope: "archived"}` lists every archived unit from the cut frames.
- **Restore** rewrites the note that lists a restored unit (a `replace` of that note), so the
  note never claims a unit left when it is back; `p` moves to the note, and the result reports
  the re-sent bytes.

### 5.6 What a cut removes besides the selection

- Reasoning the profile drops (§6.2).
- In the rewritten region only: `state` entries, absorbed archive notes, turn boundaries other
  than the newest, and, on retaining profiles, introspection rounds: rounds whose only calls are
  `context_view`, `context_stats` or `context_budget` reads, swept whole so no call is orphaned
  (§10.2).

Sweeps stay in the rewritten region, so they never move `p` or add cache cost.

### 5.7 Fetch-back and restore

- **Fetch-back** appends a bounded page: `context_inspect {unit}` for any unit, live or archived,
  or the kind-native `read_process_output` and `read_file {snapshot}`. A fetch never cuts.
- **Restore** puts archived units back in place. It is a cut, and it restores the originals,
  which for a unit edited before archival are its pre-edit bytes; the result says so.

## 6. Cut kinds, hidden reasoning and the provider prefix

### 6.1 Kinds

- **start**: binds chapter 1, or the first chapter after a version-1 snapshot, to its binding.
- **archival**: any model-authored or operator-authored non-append change of `input`.
- **rebind**: the binding changes: provider, account, model or profile (§6.3).
- **prefix**: `tools` or `instructions` change under `tool_change = "cut"` (§6.4).
- **recovery**: overflow recovery (§14).

Start, rebind and prefix cuts are *harness-required*: the model did not ask for them. §8.5 gives
each one an outcome under every turn rule.

### 6.2 Reasoning at a cut

| `reasoning.at_cut` | Meaning | Profiles (basis) |
|---|---|---|
| `drop_after_change` | keep reasoning before `p`, drop every reasoning entry at or after `p`, inserted and re-inserted entries included | Anthropic Fable 5.1, Opus/Sonnet/Haiku 5.5; Bedrock and Foundry Claude (cited: Preserved thinking) |
| `keep` | keep retained reasoning verbatim | Anthropic before 5.1 and Mythos 5.1 (cited: no prefix check); Gemini `generateContent` 3+ (cited); GLM, MiniMax, MiMo, Mistral, xAI (cited); OpenAI (cited, probe pending rd7.2) |
| `must_keep` | as `keep`; a cut that would drop reasoning on a retained assistant message is refused | DeepSeek with tools (cited 400); Kimi k3 and kimi-k2.7-code ("you must therefore (not optionally) keep the reasoning_content") |
| `keep_whole_or_drop` | keep each item whole or drop it | Meta Model API (cited) |

**DeepSeek removal is refused until probed.** "reasoning_content must be fully passed back … in
all subsequent requests" can be read to cover removed turns (DeepSeek Thinking Mode). Its profile
sets `archival = "refused_until_probed"`, basis `conservative`. That refuses every kind that
removes or replaces assistant messages: `archive`, `summarize`, `restore` of a different
version, `context_edit` remove and replace, and overflow recovery. The step does not run there,
and an overflow ends at the terminal outcome (§14).

**OpenAI's earlier-turn rendering.** GPT-5.6 renders earlier-turn reasoning by default, and
`reasoning.context = "current_turn"` stops that (Reasoning guide). The OpenAI profile records the
value it sends, so rd7.2's results can be read.

### 6.3 Rebind

`reasoning.at_rebind`:
- `keep_in_family` within the Anthropic family. "The API removes the unreadable blocks before the
  prompt reaches the model … the dropped blocks stay in your history. When the same history goes
  back to Claude Fable 5.1, its blocks are readable again", and "Keep passing thinking blocks back
  unchanged when you switch models" (Preserved thinking; Thinking).
- `drop_foreign` across families: reasoning produced under another family is dropped, because
  every adapter refuses it today (src/json.cpp:454-458; src/anthropic_provider.cpp:412-419;
  src/responses_provider.cpp:42-49).

The binding includes the account: Sonnet and Haiku 5.5 thinking is account-bound and the API
drops it silently under another account, so a reopen under a different account is recorded as a
rebind.

**When the binding is resolved.** `request()` reads the binding-relevant options (`model`,
`tools`, `instructions`) before the settle point G, so G knows the effective binding. Effort is
not in the binding: an effort change mid-turn is allowed and costs only Anthropic's message cache
("Changing the `output_config.effort` value always invalidates message blocks", Prompt caching).
A per-request binding change is a rebind subject to §8.5; where it cannot be made now,
`blackbird.request` fails with `binding-change-mid-turn` and the program sees why.

### 6.4 Tools and instructions are part of the prefix

The chapter binding carries `tools_digest` and `instructions_digest`. A request whose tools or
instructions differ from the chapter's is never sent silently. The profile field `tool_change`
decides how a change is made:
- `cut` (the shipped default on every profile): a `prefix` cut with `p` at the tools level, so
  every retained reasoning entry is dropped under `drop_after_change` and the whole prefix is
  re-read once. Dropping all thinking blocks is documented as valid, it needs no beta, and tools
  change rarely: Lua tools activate only at a successful workflow boundary (src/coding.cpp:6112),
  and `Host cwd` in the instructions changes only across a restart in another directory
  (src/coding.cpp:4357).
- `appended_system_message` (Anthropic, after rd7.3 probes it): keep `tools` as first sent and
  append a `role: "system"` message carrying `tool_addition` / `tool_removal` blocks. Anthropic
  prescribes this: "Leave the array as you first sent it, and change which tools the model can use
  by appending a `role: "system"` message" (Preserved thinking; beta `inline-tools-2026-09-15`).
  The retained message is class `tool state` (§5.2), never archivable, and replaced only by a
  later tool change. It needs the adapter to emit real mid-conversation system messages and the
  beta header, unit P3 (§21).

When a change is pending, the engine keeps sending the chapter's tools and instructions until the
prefix cut can be made (§8.5). Pending Lua tools therefore do not reach the model before the cut.

## 7. Provider turns, the closing reply and the turn boundary

Autonomous runs are one operator prompt followed by hundreds of rounds; C5's first launch grew
from 1 KB to 362 KB inside one turn. If cuts could only wait for a turn's end, the nominal path
would be inert exactly where it is needed and C4 would become routine. This section is the answer.

### 7.1 Turn state is read from the wire

The turn state is evaluated on the provider-visible message sequence as the adapter renders it,
after merging, never on stored entries (recheck B1). The engine calls the active codec's pure
render function, the same one the adapter uses (for Anthropic, the grouping at
src/anthropic_provider.cpp:346-351), and applies the profile's `turn.start` rule to its output.

What the providers say:
- **Anthropic.** "Required: within a tool-use turn, pass thinking blocks back. Recommended: across
  turns, pass everything back. Allowed: outside tool use, omit prior turns' thinking." On
  last-turn-only models, "once you send a user message that isn't a tool result, all previous
  thinking blocks are stripped from context" (Thinking). The profile's start rule is therefore
  `user_message_without_results`: a tool-use turn is closed only by a provider-visible user message
  that contains no `tool_result`. After an assistant message with `tool_use`, the next user message
  must carry the results, and the adapter merges any user text into it. So on Anthropic only an
  assistant message without `tool_use`, followed by a new user message, closes a tool-use turn.
- **OpenAI.** Only "items between the last user message and your function call output" must be
  "passed into the next response untouched" (Reasoning guide). A user message is its own input
  item, never merged. Start rule `user_text`.
- **Gemini.** The current turn starts at "the most recent User message that contains standard
  content (e.g., text)" (Thought signatures). Start rule `user_text`.

The tail is `closed` when the last provider-visible message is an assistant message without calls,
or a user message that is a turn start under the profile's rule. Otherwise it is `open`.

### 7.2 The closing reply

On profiles whose start rule cannot be met by a user note (Anthropic), Blackbird closes an open
tool-use turn by asking the model for a short reply with tools turned off:
- the request is an ordinary main request with the chapter's tools unchanged and
  `tool_choice: "none"`, which Anthropic prescribes: "To turn tool use off for a request, send
  `tool_choice: {"type": "none"}`. Don't remove `tools`" (Preserved thinking). `tool_choice` lies
  outside the prefix check, and the adapter already accepts `none`
  (src/anthropic_provider.cpp:479-485). No reasoning is dropped and no cut is made by it;
- its operational state carries a marked note, class `state`:

```
[Blackbird] Blackbird is closing this turn here. Reply with one or two lines on where you
stand in the task; tools are off for this reply only. Your work continues right after.
```

- the model's reply is appended as its own assistant message, which closes the turn, and is
  presented to the operator as usual. The words are the model's; Blackbird never writes an
  assistant message.

A closing reply is made only when something waits that needs a closed turn: a pending cut, a
pending tools change, or a binding change. It is the profile's `turn.close = "reply_request"`;
OpenAI and Gemini have `turn.close = "none"`, because a user note is a turn start there.

**The step-budget exit and other turns ending with tool outputs** (recheck B1). A turn that ends
on the step budget, an error or an interrupt leaves tool outputs at the tail. The next operator
prompt (or RRC note) would be merged into that tool-result message on Anthropic, and the turn
would stay open. Blackbird sends exactly this at the start of the next turn, before it appends
the operator prompt or the `rrc:` note (src/session.cpp:557 appends that note before the turn
runs):
1. if the tail is `open` on a `reply_request` profile and something waits that needs a closed
   turn: the closing reply, then the operator prompt as a separate user message, which is now a
   provider-visible turn start;
2. otherwise: the operator prompt as today, merged after the trailing tool results, and the tail
   counts as `open` until the model next answers without calls.

A turn that ends because a restart is scheduled cannot make the closing reply (no request is made
while a restart is scheduled, src/coding.cpp:4315); the restarted process does it before
appending the `rrc:` note.

### 7.3 The turn boundary

At a settle point inside an open turn, a pending cut whose selection lies in the current turn may
close the turn first, when the profile's `turn.boundary` is enabled. The cut frame then inserts one
boundary note at the tail (anchored on the last surviving entry, chained after any other insert on
it), and archives. After it the provider-visible current turn is empty by the profile's own start
rule, every earlier closed round is selectable, and on Anthropic the last tool-use turn is closed,
so its thinking may be dropped with the rest after `p`.

The boundary text, plainly from Blackbird:

```
[Blackbird turn boundary] This note is from Blackbird, not the operator. Blackbird closed
the turn here so that earlier rounds could leave your context. Your task is unchanged:
continue from where you were. The operator's request is r4810; the archive note r5100.0
lists what left. No reply to this note is needed.
```

Boundary forms, each defined to meet its profile's start rule:
- `user_note` (OpenAI, Gemini): a marked user-role entry, its own provider-visible user message,
  which `user_text` counts as a turn start.
- `reply_then_note` (Anthropic): the closing reply (§7.2) first, then the boundary note as user
  text. The adapter starts a new user message after the reply's assistant message, so the note is
  a provider-visible user message without results, a turn start under
  `user_message_without_results`.
- `request_local_user` (request-local profiles only): the same marked user text sent at the tail
  of the cut request only and never kept, so nothing lingers. It meets `user_text` for that
  request; whether the later requests, which no longer carry it, still pass is what rd7.2 tests.
  `user_note` stays the OpenAI candidate until then, because a retained user message is the
  documented anchor in every later request.
- `system_message` (Anthropic, candidate only): a mid-conversation `role: "system"` message. It is
  not a user message, so it does not meet the shipped start rule; the profile may count it as a
  turn start only if rd7.3 shows Anthropic treats it as closing the tool-use turn. It needs unit P3.

`boundary.enabled` is false until the form's probe passes; until then current-turn units show
`after-turn` and cuts touching them wait (§8.4). The boundary is inserted only when a pending cut
needs it. Only the newest boundary is protected; older ones are swept in a later rewritten region.

### 7.4 Turn rules

`turn.open_turn` lists the modes a profile allows while the tail is open:
- `closed_only` (basis `conservative`): settle only when the tail is closed. Anthropic's shipped
  value until rd7.3, and every unprobed host's. The closing reply (§7.2) makes a closed tail
  reachable.
- `retain_current_turn`: settle with the tail open if the cut touches no entry of the current turn,
  reasoning included. The validator refuses it together with `drop_after_change`. OpenAI (cited).
- `boundary`: settle with the tail open after inserting the boundary (§7.3). OpenAI and Anthropic
  list it, enabled only after rd7.2 and rd7.3.
- `any_closed_round`: no turn constraint (Anthropic before 5.1, Paper plaintext, once cited or
  probed).

### 7.5 What rd7.2 and rd7.3 must check

Each case runs mid-autonomous-run: one operator prompt, many rounds, then a cut of earlier rounds,
followed by several more requests. Each records, through nls.16 once it exists:
1. the HTTP status and full error, if any;
2. whether each later response carries thinking blocks (Anthropic) or reasoning items (OpenAI);
3. Anthropic's `input_transformations` (beta `thinking-binding-controls-2026-08-01`): empty, or an
   honest list of what the API dropped;
4. whether the model continues its task (calls tools or reports progress) rather than answering a
   note;
5. the cached-token counts of the later requests.

Cases:
- **rd7.3, Anthropic** (Fable 5.1, Opus, Sonnet, Haiku 5.5): the closing reply; `reply_then_note`;
  an operator prompt merged after trailing tool results, to learn whether Anthropic treats it as a
  new turn; `system_message` as a boundary; no boundary with client removal of every thinking
  block at or after `p` mid-turn; a forced prefix cut dropping all reasoning with the tail open
  (§8.5); `drop_block`; the open turn rendered as text; `tool_change = appended_system_message`;
  and, for the step restriction (§24), the two switch criteria per model: **a tool removed with
  `tool_removal` cannot be called, and thinking survives**.
- **rd7.2, OpenAI** (api-key route and ChatGPT route, `store=false`): `user_note` and
  `request_local_user`; keep, drop and rebind of retained encrypted reasoning; an in-place edit of
  an earlier `function_call_output`; `allowed_tools` on both routes (the ChatGPT route may not
  accept the object form); whether Blackbird's first input items form an initial developer block,
  and the cached-token count after a deep cut.

## 8. Staging and settle points

### 8.1 Everything non-append stages

Every non-append change from the model or a program stages: `context_manage`, `context_edit`, Lua
`blackbird.manage` and `blackbird.edit`, and restore (a `context_manage` field). Nothing publishes
inside an operation, so no cut can cross the open round of the call that requested it. Each tool's
result says `staged` and when the change takes effect (§11), from the unit that changes the timing
(S3b, §21).

### 8.2 Settle points and the order at G

- **G**: in `request()` after `drain_workflows()` and before `items` and `origin` are taken
  (between src/coding.cpp:4320 and 4321). In this order:
  1. `start` frame if the chapter has none;
  2. a harness-required rebind or prefix cut, if the resolved binding differs and §8.5 allows it
     now. Its frame rebases the pending proposal (§8.3) in the same record;
  3. settle the pending proposal, wholly or in part, under the turn rule (§7.4), inserting a
     boundary if that is what makes it permitted;
  4. the archival step (§13).
  Then take items and origin and validate again. Rebind before settlement, so the pending
  proposal is expanded against the entries the provider will actually see. Settlement is skipped
  at G when the request overrides `input`, since its response is reasoned against input that is
  not the chapter's context.
- **J**: `finish_workflow(true)` (src/coding.cpp:6119), atomically with the boundary program
  through `record_boundary` (src/context.cpp:94-149), only when the tail's turn state permits.
- **Turn start**: before the operator prompt or `rrc:` note is appended, the closing reply of §7.2
  when it applies; then the next G settles what waits.
- **Idle**: between turns, an operator proposal publishes at once if the turn rule permits,
  otherwise it stages. A pending cut carried from an earlier turn waits for the next G.

Never between taking `origin` and accepting the response (src/coding.cpp:4515, 4842).

### 8.3 One pending proposal, held as units

- The pending proposal is held as units (creating-record refs and the selection text), not as
  expanded entries. It is re-expanded against the current entries when it settles (recheck B2).
- **Merging** (recheck B4). A new proposal while one is pending merges into it: archive, pin and
  restore sets are unions; where the two disagree about a unit (archive in one, restore in the
  other), the newer wins and the result says so. The merge is one staged frame naming the old
  stage. `pending-overlap` is retired. A waiting remainder therefore never blocks a new proposal,
  and the settle-now part of the merged proposal settles at the next G.
- **Rebase.** A rebind or prefix cut removes only reasoning entries, which are never units. Its
  frame carries the pending proposal rebased onto the post-cut entries (`pending` with a new basis).
  If any of the pending units no longer exist, the frame instead refuses the pending proposal with
  `units-gone` and lists them. Settlement then holds the basis-prefix check
  (src/context.cpp:1583-1592) against the rebased basis.
- **Split settlement** (recheck B3). A settlement that leaves a remainder writes one frame: the
  settled part, and the remainder in its `pending` field. Playback restores the remainder with that
  frame as its stage. No second record is needed.
- **Durability.** Playback and the `pending` section restore pending state; the basis list is
  rebuilt from the reference; settlement keeps carrying later appends forward.
- **Reserve.** The turn-start settlement reserve is sized with the carried pending proposal, and
  `cancellation_budget` prices the exact `cut` frames (staged, settled, refused, cancelled) instead
  of only `stop.linkage` and a managed rejection (src/context.cpp:1275-1351; src/coding.cpp:6042,
  3177-3181).

### 8.4 Outcomes for a model or operator proposal

| Event | Pending proposal |
|---|---|
| A settle point where the turn rule permits | settles; badge; if the cause was the step or recovery, a change notice (§13.1) |
| The turn rule permits part of it | the permitted part settles; the rest stays pending in the same frame; the result and notice say which units wait |
| The turn rule permits none of it | stays pending; each unit shows `after-turn`; the closing reply at the next turn start or J makes the tail closed |
| A new proposal arrives | merged (§8.3) |
| Turn fails | stays pending |
| Operator interrupt | stays pending; the TUI shows it |
| `/archive cancel` (operator, idle) | cancelled with a frame naming the stage; if the journal is not live the command fails visibly and nothing changes |
| Crash | restored from the staged frame or the `pending` section; the TUI notice says so |
| A non-append change lands first | rebased if it was a rebind or prefix cut (§8.3); otherwise refused `settlement-conflict`, naming the stage |

This replaces "takes effect only after a successful workflow" (CONTEXT_BUDGET.md:88-94;
src/tools.cpp:1394-1410). A cut settles no effects and every original stays retained.

### 8.5 Outcomes for harness-required cuts (recheck B5)

A harness-required cut never wedges the session. For each kind:

| Kind and cause | Tail closed | Tail open, `reply_request` profile | Tail open, other profiles |
|---|---|---|---|
| start | written at G | written at G (it removes nothing) | written at G |
| prefix, from Lua tool activation | cut at the next G | the new tools wait; the old chapter tools keep being sent; at the next turn start the closing reply runs and the cut follows at G | under `retain_current_turn` the cut is made if it drops no current-turn reasoning (`keep` profiles drop none); otherwise as the `reply_request` column, with a user note as the boundary |
| rebind or prefix, from `/model`, a reopen under another binary, directory, account or model, or a profile change | cut at the next G | at the turn start: closing reply, then the cut at G | `keep` profiles: cut at G; others: the forced form below |
| rebind or prefix where the old prefix cannot be sent any more and no closing reply is possible (for example a restart into a binary whose tool definitions changed, with tool outputs at the tail) | cut at the next G | **forced form**: the cut drops all retained reasoning, which Preserved thinking lists as valid ("all of them"); recorded with `cause.forced = true` and shown to the operator; rd7.3 records what Anthropic does with it | same forced form |
| program request changing the binding mid-turn | rebind at G | refused `binding-change-mid-turn` | refused `binding-change-mid-turn` unless the profile allows it |

Model-authored cuts never use the forced form; they wait.

### 8.6 Relation to change activation

AGENTS.md's activation default (after the current turn and affected workflows, with
interrupt-and-apply-now) governs definition changes: programs, tools, policy, profiles. CORE_DESIGN.md
says context editing "takes effect within the running program before subsequent requests"; cuts do,
subject to the provider's turn rules. Budget policy and profile overrides stay staged to the workflow
boundary. No settle interrupts anything: a request or tool in flight finishes first.

## 9. Tool output: shaped once, at creation

| Kind | Shape at creation | Original | Delivery |
|---|---|---|---|
| `read_file` | 16 KiB head (o17); ranges exact | `file.read` + `snapshot` | output; `superseded` flag when a later live read or write of the path exists |
| `exec`, `process foreground`, `read_process_output` | 8 KiB: 2 KiB head + 6 KiB tail with gap marker (o17, ae81fbe) | `process.output` + `output_ref` | output; `exit`, `error`, `timed_out` flags |
| `write_file`, `edit_file` | confirmation and sizes | `file.before`, `file.proposed` | output |
| `context_view`, `context_stats`, `context_budget` reads | bounded page (§10) | none needed | request-local (§10.1) |
| `context_inspect`, `audit_inspect`, `trajectory_read`, `variables_read`, `tasks_read`, `git_observe` | bounded pages; inspect is text | source reference | output |
| `colleague`, `participant_read`, `decision_model`, `workflow_invoke`, `lua`, `program_config`, `module_source`, `workflow_registry`, `tool_registry` | generic: 16 KiB, 4 KiB head + 12 KiB tail with gap marker | `operation.result` (`result_ref`) | output |
| Lua-defined tools | generic, unless the tool declares its own shape in `tool_define` | `operation.result` | output |
| workflow-continuation results | generic, applied in `drain_workflows` (src/coding.cpp:4281-4288), which is outside `operation()` | the workflow's result original | developer item, class `result` |

- **Generic shaping** for operations happens in `operation()` after the full result is retained as
  `operation.result` (src/coding.cpp:3940-3943) and before it is returned. Text results are cut head
  plus tail, because conclusions sit at the end; the 1:3 split is o17's exec ratio.
- **Native shaped tools accept `output_max_bytes` and `output_head_bytes`** with exec's semantics,
  honoured exactly, and supplied keys are recorded as `native_defaults` (src/coding.cpp:3363-3368).
  The schema bytes this adds to the fixed prefix are measured before it ships, and it ships only on
  tools where the measured cost is justified. A Lua-defined tool that declares parameters with
  those names keeps its own meaning; its shaping comes from `tool_define` metadata, never from
  argument names.
- Only the operator's outermost call stays exact (src/coding.cpp:4972-4974).
- **A cut never re-shapes a retained output.** Anthropic: "Shorten a tool result … before the first
  time you send it, not after" (Preserved thinking).
- **Known cost carried forward.** `retained_process_output` assembles a command's whole capture to
  shape 8 KiB. The shaping unit measures it before deciding anything.

There is no `retain` argument. Pins go through `context_manage`; ephemerality is decided by kind,
natively.

## 10. Inspection that does not linger

### 10.1 Request-local delivery

`context_view`, `context_stats` and `context_budget` reads describe the context; any change makes them
stale. On profiles whose operational state is request-local (`request_state.placement =
"request_local"`, every provider except Anthropic today):
- the tool's output in context is one line: `context index: 57 units; shown in the next request that
  carries Blackbird's state block, then not kept; call context_view again to see it later`;
- the engine keeps the page arguments and builds the rows when it assembles the next request that
  carries operational state. A request with `state_context = "omit"` or an overridden `input` does
  not consume the delivery; it waits for the next one;
- the page arguments live in memory only. A crash loses an undelivered page; the model calls again.
- the rows reach the provider in exactly one request and never enter context.

This is chosen over sweeping view outputs at the next settle point regardless of position, because
a sweep is a non-append change: each view would cost a cut, a re-read from the view's position, and
on Anthropic the reasoning after it.

### 10.2 Retaining profiles

Anthropic retains operational state in place (`request_state.placement = "retained_in_place"`): a
thinking block is bound to the prefix that preceded it, and the state text sat in that prefix
(src/coding.cpp:4484-4488; Preserved thinking). Request-local delivery is impossible there without
dropping the current turn's thinking. So on retaining profiles:
- the rows come back as the tool output, in the compact text format below;
- the round that carries them is swept whole by the next cut whose rewritten region holds it (§5.6);
- a second view while the first is still live returns only the rows that changed since it.
The tool description on those profiles states this.

### 10.3 Row format

Aligned text with one header, no keyed objects:

```
unit   kind      tool       head                                 size   age  flags
r4810  instr     -          "Implement the raw recorder stop…"   1.2K   57   protected
r4821  round     exec       "cmake --build build/debug"          8.0K   41   exit=0
r4870  round     read_file  "src/context.cpp" lines 1-400        16.0K  33   superseded
r4902  round     message    "The replay loop rejects frames…"    0.6K   12
r5011  round     exec       "ctest -R context"                   8.0K   1    after-turn
```

- Columns: handle, class, tool, 60-byte head, bytes, age in requests, flags (`protected`, `pinned`,
  `superseded`, `exit=N`, `error`, `after-turn`). On cached routes a `reread` column gives the bytes a
  cut there would re-send.
- Width is fixed by construction, at most about 140 bytes per row: a 64-row page is at most about
  9 KiB and the 256-row maximum about 35 KiB. These are construction bounds; the inspection unit
  measures the C5 fixture before any size is stated as observed.
- `scope: "archived"` reads the cut frames through the chapter table, then the first call's tool and
  head argument from each unit's original by identity lookup: one lookup per row shown.

### 10.4 Originals as text

`context_inspect` returns the original as the provider saw it: a string output as is, a structured
output as its exported JSON text (allowed at this boundary, a1v.1), cut on character and
nearby-newline boundaries (src/tools.cpp:209). Hex only for real binary. `part` selects the output
(default), the call's arguments or the message; hidden reasoning is never returned.

## 11. Model-facing contracts

The schemas are JSON Schema at the provider boundary; internally they are native values. Descriptions
that depend on the profile (cache, turn rule, delivery) are rendered from it, so they are fixed within
a chapter. No schema uses `$ref` or `anyOf`, so every provider's function schema accepts it.

### 11.1 Selections

A selection is one string:
- items separated by commas are alternatives: `"r12, r15-r40"`;
- an item is a handle (`r12`, `r4821.1`), a range (`r12-r40`, every unit whose handle number lies in
  it), or filter terms separated by spaces, all of which must hold: `tool=read_file`, `kind=result`,
  `superseded`, `failed`, `before=r900`;
- example: `"r4821, tool=read_file superseded, kind=result before=r4900"`.

The engine expands a selection over live units (over archived units for `restore`), drops protected
and pinned units (listed in the result as refused), and records the expanded units.

### 11.2 `context_view`

```json
{"name": "context_view",
 "parameters": {"type": "object", "additionalProperties": false, "properties": {
   "scope": {"type": "string", "enum": ["live", "archived"]},
   "after": {"type": "string", "description": "handle to continue after"},
   "limit": {"type": "integer", "minimum": 1, "maximum": 256}}}}
```

Description (request-local profiles): "List the units in your context: each tool round, result,
instruction and note, with a short handle such as r4821, the tool and a short argument head, its
size, its age in requests and flags. scope=archived lists units archived earlier; you can still read
them with context_inspect. The list is shown to you once, in your next request that carries
Blackbird's state block, and is not kept in your context; call again to see it later. Default 64
rows, at most 256; the last line gives the handle to continue after."

On retaining profiles the third sentence reads: "The list is added to your context and removed the
next time your context is archived."

### 11.3 `context_manage`

```json
{"name": "context_manage",
 "parameters": {"type": "object", "additionalProperties": false, "properties": {
   "archive":   {"type": "string", "description": "selection, e.g. \"r12-r40, tool=read_file superseded\""},
   "pin":       {"type": "string"},
   "unpin":     {"type": "string"},
   "restore":   {"type": "string"},
   "summarize_unit": {"type": "string"},
   "summary":   {"type": "string"},
   "next_review_rounds": {"type": "integer", "minimum": 1, "maximum": 200},
   "reason":    {"type": "string"}}}}
```

Description: "Change what stays in your context. archive removes whole units; their originals stay
readable with context_inspect, and a note in their place lists them. pin keeps units from being
archived, by you or by Blackbird's overflow recovery; unpin releases them. restore puts archived units
back where they were. summarize_unit with summary replaces one unit with your own text.
next_review_rounds says after how many more rounds Blackbird may ask you to review your context
again. Instructions from the operator are never archived, except an older copy of an instruction that
appears again later, which shows as superseded. Select units by handle (r12), range (r12-r40) or
filter (tool=read_file superseded). One call may archive, pin, unpin, restore and summarise together;
the same unit may not be both archived and restored. The result lists what was accepted and when it
takes effect." Then the profile's cost sentence and turn sentence:
- cached route: "Archiving makes the provider re-read your context after the first archived unit
  once, without the prompt cache; the result gives those bytes, so one larger archive costs less than
  several small ones."
- uncached route: "This route has no prompt cache, so archiving costs no extra re-read."
- turn sentence where `closed_only` holds: "On this provider, units of your current turn can be
  archived only after the turn closes; they are marked after-turn, and Blackbird may ask you for a
  one-line status reply to close the turn."

Result:

```
{accepted: "r12, r15-r40", refused: [{unit, reason}], pinned, unpinned,
 takes_effect: "before_next_request" | "after_turn" | "split",
 waiting: "r5011, r5020", merged_with_pending: true | false,
 reread_bytes, removed_bytes, next_review_rounds}
```

### 11.4 `context_inspect`

```json
{"name": "context_inspect",
 "parameters": {"type": "object", "additionalProperties": false, "properties": {
   "unit":   {"type": "string"},
   "part":   {"type": "string", "enum": ["output", "call", "message"]},
   "offset": {"type": "integer", "minimum": 0},
   "limit":  {"type": "integer", "minimum": 1, "maximum": 65536}},
  "required": ["unit"]}}
```

Description: "Read the original of a unit, live or archived, in text pages. part=output (default) is
the tool output, call the call's arguments, message the assistant text; hidden reasoning is never
returned. Default 8192 bytes per page. Returns text (hex only for binary data), total_bytes and next,
the offset to continue from. The page is added to your context like any tool output."

### 11.5 `context_edit`

```json
{"name": "context_edit",
 "parameters": {"type": "object", "additionalProperties": false, "properties": {
   "ops": {"type": "array", "items": {"type": "object", "properties": {
     "op":    {"type": "string", "enum": ["replace", "remove", "insert"]},
     "unit":  {"type": "string"}, "units": {"type": "string"},
     "after": {"type": "string", "description": "handle, or \"start\""},
     "text":  {"type": "string"}, "item": {"type": "object"}},
     "required": ["op"]}}},
  "required": ["ops"]}}
```

Description: "Edit your context directly when archive and summarize do not fit: replace a unit's text
or item, remove units, insert an item after a handle. Inserted items count as your results, whatever
their role. Operator instructions and pinned units cannot be edited. Edits take effect at the same
moments as context_manage and re-send your context after the first changed unit. Prefer
context_manage to remove material."

There is no `move` in the model contract (recheck B6). Lua's full-candidate `blackbird.edit` may
reorder whole units; the engine records a reorder as removal plus `existing` re-insertion (§4.3), so
the unit keeps its ID, handle, class and round.

### 11.6 `context_stats` and `context_budget`

`context_stats` returns totals and the five largest units, delivered like `context_view`.
`context_budget` reads or proposes the policy of §12.1; its description: "Read or change when
Blackbird suggests and when it asks for a context review. Changes take effect after this workflow."
The policy fields appear only in this tool's own result.

### 11.7 Removed

`context_originals` and `context_restore` leave the palette (replaced by `context_view {scope:
"archived"}`, `context_inspect` and `context_manage {restore}`). `select` leaves `context_manage`.
`context_inspect` kinds `history` and `index` stay for Lua and operator commands only. These removals
ship together with their replacements (§21).

### 11.8 Lua

- `blackbird.manage(p)` takes the same fields as `context_manage` plus an explicit `base`, and stages.
- `blackbird.edit(candidate)` keeps the full-candidate form, which never enters model context; the
  engine turns it into the same delta, refuses protected units, and stages.
- `blackbird.restore` is removed; `blackbird.append(items, opts)` returns `{revision, ids}`;
  `blackbird.context()` keeps returning the full view to the program, and a `lua` tool that returns it
  is shaped like any other output.

## 12. Escalation: inform, nudge, direct, fail

### 12.1 Policy

`context-budget-effective-v2 {enabled, nudge_bytes, direct_bytes, reason}`. `target_bytes` is retired.
`nudge_bytes` keeps the measured 384 KiB (REQUEST_SIZES.md; PLAN.md). `direct_bytes` starts at
**432 KiB** (442,368 bytes; revision 2 had 448 KiB). Grounding: on the densest measured provider
(paper, about 3.4 wire bytes per token at the top of its range, native bytes 5-10% below wire),
432 KiB native is about 137k-145k tokens across that whole range, under the 147,640-token request that
was the largest accepted in the campaign. 448 KiB was about 142k-150k and therefore not under it at
the 10% end (recheck low). Both levels are internal; the model is never shown either number.

### 12.2 Levels and wording

| Level | When | What the model receives |
|---|---|---|
| inform | every request on request-local profiles; on retaining profiles only inside the retained review text (below) | `CONTEXT_SIZE input_bytes=N units=U pinned_bytes=P (native packet bytes, not tokens)` |
| nudge | `input_bytes ≥ nudge_bytes` | `CONTEXT_REVIEW` text |
| direct | `input_bytes ≥ direct_bytes` and the model's review interval has passed | the archival step (§13) |
| fail | the provider refuses the request as too long | overflow recovery (§14) |

`CONTEXT_REVIEW`:

```
CONTEXT_REVIEW Your context has grown large (N bytes in U units). Long contexts cost more per
request and can make earlier material harder to use well. If whole earlier units no longer
help the work in front of you, archive them with context_manage; their originals stay
readable, and a note in their place lists them. Keep what you need, pin what you will need
later, or change nothing. context_view lists your units. If your context keeps growing,
Blackbird will ask you to look at it. <cost sentence> <turn sentence>
```

The cost and turn sentences are the profile's (§11.3). The text names no level, no target and no
percentage.

**On retaining profiles** (folds in 2o9): retained state is appended only when it changes meaning, and
the retained form carries no changing numbers beyond the sizes at that moment. `CONTEXT_REVIEW` is
appended once when the nudge level is first crossed, and not again until a step or a cut changes the
situation; the task page only when the task revision moves.

### 12.3 The review interval

After any review (a step, or the model's own `context_manage` call that sets `next_review_rounds`),
the step runs again only after that many new rounds. If the model gives none, the interval is 20
rounds, a starting value retuned by ib8 trials. The interval keys on the model's choice and on work
done, not on bytes, and every outcome is recorded as what it was. The interval lives in the `review`
section of the snapshot.

## 13. The archival step (C6a, C6b)

### 13.1 C6a, synchronous at G

**When.** At G (§8.2 step 4), all of:
- policy enabled and `input_bytes ≥ direct_bytes`;
- the review interval has passed (§12.3);
- at most one step per turn; a waiting remainder does not block it, and its proposal merges (§8.3);
- at least one unit is selectable now under the turn rule; otherwise the step does not run;
- the request does not override `input` or `tools`, does not use `state_context = "omit"`, and is not
  from the backstop engine;
- the profile allows archival (not DeepSeek until probed).

**The request**, built by the pure request builder (§13.3):
- `instructions` and `tools` as the main request, or as the profile's restriction requires;
- `input`: the items the main request would send, transformed only by the profile's
  `side_request_reasoning` rule, then the brief as the last item;
- no operational state, so the nudge text is not repeated; the main request's effort, because a
  different effort misses the message cache (Prompt caching);
- on breakpoint profiles, **the main request's existing cache breakpoints, which read the entries the
  main requests already wrote, and none after them**, so the brief causes no cache write; where the
  restriction changes `tools` (`palette_only`), no breakpoint, because nothing written can match
  (recheck B10).

The brief:

```
CONTEXT_REVIEW_REQUEST from Blackbird, not the operator. Your context holds N bytes in U
units. Take a moment to decide what still helps the work in front of you.
<rows: handle, kind, tool, head, size, age, flags including after-turn>
You may call context_manage once: archive units, pin or unpin units, summarise one unit, and
set next_review_rounds. Declining is a real choice: answer without a tool call and work
continues as before; Blackbird asks again after 20 more rounds unless you set another number.
Pins keep units from your own later archives and from Blackbird's overflow recovery. Nothing
you write here stays in your context; only an accepted context_manage call changes it.
<cost sentence> <turn sentence> [jev column note]
```

**Tools offered** by `tool_restriction`:
- `allowed_tools` (OpenAI api-key route, after its probe): the main tools list unchanged, with
  `tool_choice: {type: "allowed_tools", mode: "auto", tools: [context_manage]}`, so declining stays
  possible and the tools prefix stays cached ("make only a subset of tools available … but not modify
  the list of tools you pass in", Function calling).
- `palette_only`: only `context_manage`. **This ships on Anthropic and on every route not yet probed
  for a native restriction** (§24). On prefix-checked profiles a changed tools list invalidates every
  thinking block, so `side_request_reasoning = "drop_all"` removes all reasoning from the step's copy
  of the prefix, which Preserved thinking lists as valid. The main context keeps its reasoning.
- `appended_removal` (each Anthropic model, once its rd7.3 run meets both criteria in §24): `tools`
  unchanged, `tool_choice: auto`, and the brief carried in an appended `role: "system"` message with a
  `tool_removal` block for every tool except `context_manage`; reasoning kept. It needs unit P3.

Forced tool choice is never used: declining must stay possible (decision 7). Anthropic also refuses
forced tool use on every request on Opus 5.5, Sonnet 5.5, Fable 5.1 and Mythos 5.1 (Thinking; Errors).

**Outcome.** The call's arguments go through the same validation as the model's own call and become a
pending proposal (merged with any waiting remainder), which settles at this G, wholly or in part. Then
G continues and the main request goes out. Its operational state carries a one-line change notice:

```
CONTEXT_CHANGED At the review just now you archived r12-r40 (31 units, 118 KiB) and pinned
r4870. The note listing them sits where r12 was; context_inspect reads any of them.
```

On retaining profiles the notice is retained once. A decline, a refused proposal or a provider failure
is recorded and the main request goes out unchanged; `interrupted` and `capacity` pass through. A length
refusal of the step hands over to C4, which does not fire twice for one request.

**Record.** `clm.step {attempt, chapter, input_bytes, units, palette, jev_attempt, outcome, reason,
stage}` with `outcome` and `reason` as separate enumerated fields.

### 13.2 Jev as an advisory column

When a decision model is configured, the step may ask Jev to score each unit's relevance and show it as
a column labelled advisory. The pilot measured about 7,800 input tokens and 0.35-0.44 s per triage
(JEV_PILOT.md); one task is not a calibration. Jev never decides; a failed call drops the column and
the brief says so. It runs on the side request only, never at creation.

### 13.3 The pure request builder

`request()` is split into a builder that produces `instructions`, `tools`, `input` and the operational
state with no context or engine writes, and a main-request committer that does the retained-state
append (src/coding.cpp:4484-4488), the slot updates (src/coding.cpp:4524-4525), preview and display.
Side requests (the step, C4) use only the builder and their own audited `operation("provider")` attempt
with `purpose: clm.step` or `clm.recovery`. The closing reply (§7.2) is a main request through the
committer. Whether a provider `RawRegion` may open inside the main request's assembly region is checked
against the raw region nesting rules before the builder is wired (unconfirmed).

### 13.4 C6b, during tool waits

Designed after C6a's timing data, with these constraints:
- the engine thread busy-polls in `CommandJobs::wait` (src/command_jobs.cpp:1367-1477), so the step
  runs on a worker with its own provider instance;
- the engine admits the worker's attempt on the engine thread before spawning it; the worker hands raw
  response bytes back through a locked mailbox and the engine retains them; a crash leaves the attempt
  visibly unresolved;
- the worker works from an immutable snapshot; TSan runs on the shared Value storage early, because
  `Value` copy-on-write reads `use_count()` (include/blackbird/value.hpp:92);
- the worker's proposal waits unstaged in the mailbox until G; if the model has proposed since the
  worker's snapshot, the worker's proposal is dropped with a record;
- prerequisite: the OpenAI codex auth-file race between concurrent requests (C6_DESIGN).

## 14. Overflow recovery (C4)

A fired recovery is a failure (decisions 1 and 3).

**Detection.** A provider refuses the request with class `context_overflow` (§15.3), applied by nls.16;
until then a test seam presents it (a1v.5). Anthropic's `stop_reason: "model_context_window_exceeded"`
is not an overflow: "the API accepts the request", and the response "is still valid but was limited by
context window" (Thinking; Handling stop reasons). It becomes `output_window_exhausted`, carried on the
accepted response, which is appended as returned. Anthropic says: "pass the assistant turn back unchanged
and append a new `user` message (with a `tool_result` for each `tool_use` block in that turn)". So
complete calls in such a response are dispatched and answered as usual, and a `tool_use` whose input was
truncated is answered without dispatch by `{error: "truncated_by_context_window"}`. The adapter changes
to accept that stop reason with or without calls, instead of `incomplete` or `corrupt`
(src/anthropic_provider.cpp:202-209).

**Unwinding.** Recovery does not run inside the failed request's group. The attempt loop ends, the
request group is destroyed and `end_request_group` runs, control returns to G, the recovery runs its side
requests through the builder, its cut settles, and assembly starts again. On a retaining profile the
failed attempt's state entry lies in the rewritten region and is swept by the recovery cut.

**Option A, archival from an index** (the first choice, §23 decision 1):
- side request: pinned instructions, the latest operator prompt, an `OVERFLOW` notice with the failed
  request's size and the provider's refusal, the most recent rounds verbatim newest first within the
  byte budget, and the index rows for every other unit;
- tools: `context_manage` only;
- budget: seeded from the parsed provider gap (§15.3), otherwise half the failed request's bytes;
- result: a `recovery` cut with `changes: {archive}`.

**Option B, windowed summary** (compaction proper), when A does not make the request fit: windows of the
oldest complete unpinned rounds, each summarised by the model with the previous summary carried forward;
halve the budget on a second overflow; bound the windows; stop on no byte progress. One `recovery` cut
with `changes: {compaction}`.

**Turn rules.** Recovery obeys the turn rule like any cut:
- `closed_only` with an open tail: the closing reply is a request and would overflow too, so recovery
  can use only units outside the current turn; if the boundary is enabled, it inserts it;
- `retain_current_turn`: units outside the current turn, or the boundary if enabled.
Where nothing selectable can make the request fit, the terminal outcome applies.

**Retry** once, as the first request of the new chapter.

**Terminal outcome**, under every turn rule and on profiles that refuse archival: when pinned
instructions alone exceed the budget, when nothing selectable can make the request fit, or when neither
option makes progress, recovery stops, the turn ends with the critical complaint, every original is kept,
and the same request is not retried in a loop. The documented outcome of a mid-turn drop on Anthropic may
be a successful request without thinking rather than a 400; rd7.3 measures it, so this rule is a measured
stopgap, not a claim that the request would fail.

**Audit.** `clm.recovery {failed_attempt, chapter, sizes, class, option, windows, outcome, cut}`, and a
rageshake complaint raised by the harness after the outcome is known and outside any request group, with
two new fields, `raised_by: harness` and `severity: critical` (src/tools.cpp:1306-1318;
src/coding.cpp:5285). The badge shows the critical state.

## 15. Provider profiles

### 15.1 Format and location

- Authored as data-only Lua tables under `profiles/providers/` in the resource root
  (CMakeLists.txt:196-197), read by a native parser of the table-constructor subset: tables, string,
  number and boolean literals, comments. Nothing is executed; other constructs are refused.
- Recorded as BBM2: the loaded set is written as a latest-wins program record
  `provider-profile-set-v1` (id, revision, digest, layer, body).
- Never JSON.

### 15.2 Schema `blackbird.provider-profile/1`

The schema as implemented (src/provider_profile.cpp). An illustrative shipped profile; the shipped
files are in `profiles/providers/`:

```lua
return {
  schema = "blackbird.provider-profile/1",
  id = "anthropic-messages-prefix-checked",
  revision = 1,
  -- The default for native Anthropic: every family not listed by anthropic-messages.
  match = { provider = "anthropic", protocol = "messages" },
  sources = {
    preserved = { url = "https://platform.claude.com/docs/en/build-with-claude/preserved-thinking", read = "2026-10-20" },
    thinking = { url = "https://platform.claude.com/docs/en/build-with-claude/thinking", read = "2026-10-20" },
    errors = { url = "https://platform.claude.com/docs/en/api/errors", read = "2026-10-20" },
    streaming = { url = "https://platform.claude.com/docs/en/build-with-claude/streaming", read = "2026-10-20" },
    effort = { url = "https://platform.claude.com/docs/en/build-with-claude/effort", read = "2026-10-20" },
    overflow_reports = {
      path = "papers/clm-a1v-2026-10-19/OVERFLOW_STUDY.md",
      read = "2026-10-19",
      note = "community: the Claude Code reconstruction and pi; Anthropic documents no overflow text",
    },
  },
  pairing = { validator = "anthropic-messages", basis = "cited:thinking" },
  reasoning = {
    at_cut = "drop_after_change",
    at_rebind = "keep_in_family",
    side_request_reasoning = "drop_all",
    basis = { at_cut = "cited:preserved", at_rebind = "cited:preserved", side_request_reasoning = "unobserved" },
  },
  turn = {
    start = "user_message_without_results",
    close = "reply_request",
    open_turn = { "closed_only", "boundary" },
    boundary = { form = "reply_then_note", enabled = false, basis = "conservative:probe rd7.3 pending" },
    basis = "cited:thinking",
  },
  request_state = { placement = "retained_in_place", basis = "cited:preserved" },
  note = {
    role = "developer_as_user_text",
    placement = "after_tool_results",
    basis = "conservative:Blackbird's rendering, src/anthropic_provider.cpp:428-434",
  },
  tool_restriction = { mode = "palette_only", basis = "conservative:§24 owner decision" },
  tool_change = { mode = "cut", basis = "cited:preserved" },
  archival = { mode = "allowed", basis = "cited:preserved" },
  edit_acceptance = { in_place = "unknown", basis = "conservative:not probed" },
  cache = { capability = "none", basis = "conservative:no cache controls until bvu" },
  errors = {
    { class = "context_overflow", status = 400, contains = "prompt is too long",
      extract = { input_tokens = "prompt is too long: ", limit_tokens = "> " },
      basis = "cited:overflow_reports" },
    { class = "payload_too_large", status = 413, error_type = "request_too_large", basis = "cited:errors" },
    { class = "overloaded", status = 529, basis = "cited:errors" },
    { class = "overloaded", event = "error", error_type = "overloaded_error", basis = "cited:streaming" },
  },
  stop_reasons = { model_context_window_exceeded = "output_window_exhausted", basis = "cited:thinking" },
  retry = {
    http = { 408, 429, 500, 502, 503, 504, 529 },
    transport = { 5, 6, 7, 16, 18, 28, 52, 55, 56, 92, 95, 96 },
    rate_limit_requires_retry_after = true,
    basis = "cited:errors",
  },
  effort = { values = { "low", "medium", "high", "xhigh", "max" }, thinking = "adaptive", basis = "cited:effort" },
  output = { max_tokens = 16384, basis = "conservative:matches today, include/blackbird/anthropic_provider.hpp:8" },
}
```

**Anthropic selection** (owner decision, 2026-10-20). `anthropic-messages-prefix-checked` is the
default for native Anthropic Messages. It covers Fable 5.1, Opus 5.5, Sonnet 5.5 and Haiku 5.5, and any
new or unknown family, because `drop_after_change` is valid on every Anthropic model while `keep` is not
valid where the prefix check runs. `anthropic-messages` lists in `match.models` only the older families
that do not run the check (Mythos 5.1 and families before Fable 5.1), and keeps reasoning at a cut. A
family whose name is a `-`-prefix of a prefix-checked name (Fable 5, Mythos 5, Opus 5, Sonnet 5), and
Sonnet 4 and Opus 4 by their bare names, are not listed; they take the valid default.

The shipped Anthropic file differs from this example where plan §1.4 requires it: its unprobed rows
carry `unobserved`, and the families it does not consume yet (errors, stop reasons, retry, effort,
output, cache) are left to the conservative profile until P2 and K1.

**Basis.** A `basis` sits inside a family table: a string for every field the table sets, or a table
of field = basis. The value is `kind[:detail]` with exactly four kinds:
- `cited:<source>`: names an entry in `sources`. A community-observed rule is cited too, through a
  source whose `note` says `community:` and names who observed it;
- `probed:<probe>`: a Blackbird probe's result;
- `conservative[:why]`: the safe choice, or Blackbird's own rendering or decision;
- `unobserved[:why]`: built against documentation or mocks, never observed live.
A source entry has a `url` (https) or a repository `path`, a `read` date, and an optional `title` and
`note`. A basis must cover at least one field its layer sets. A missing basis is never refused.

Field families (decision 6): pairing and validity; reasoning (`at_cut`, `at_rebind`,
`side_request_reasoning`, `context` for OpenAI); turn (`start`, `close`, `open_turn`, `boundary`);
request-state placement; note role and placement; tool restriction; tool change; archival
(`allowed` or `refused_until_probed`, a table so it carries a basis); edit acceptance; cache
(capability, breakpoints, TTL, minimum, `read_multiplier`, `write_multiplier`, affinity body field and
header; detail fields only where the capability gives them meaning); error classification; stop
reasons; retry; effort; output ceilings. **Transforms to disable** (OpenRouter `context-compression`
off, never `truncation: auto`) are deferred to the unit that sends them (P2, with the OpenRouter
adapter); until then the schema has no such family and refuses the key.

### 15.3 Error rules

- A rule matches on HTTP status, provider error type or code, SSE event name, and a case-insensitive
  literal substring. Every rule carries a `basis`; most overflow phrases are community-observed:
  - Anthropic documents only `400 invalid_request_error` and `413 request_too_large`; "prompt is too
    long: N tokens > M maximum" comes from the Claude Code reconstruction and pi.
  - OpenAI: `context_length_exceeded` with "Your input exceeds the context window of this model"
    appears in OpenAI's own Codex client fixture, not in the Responses error list.
  - Meta documents a full message; xAI names `context_length_exceeded`; Gemini's phrase is forum-only;
    DeepSeek and Mistral document none.
- **Number extraction** for the provider gap: "the first integer after this literal", named per field.
  No regular expressions. When nothing is extracted, the gap is unknown and recovery uses half the
  failed request's bytes.
- A 429 without `retry-after` is not retried as a rate limit: a tier spend-cap 429 "has no
  `retry-after` header and keeps failing" (Errors).
- Bedrock, Vertex and Foundry Claude get their own rows before those routes ship; Vertex returns 413
  for token overflow, so status alone cannot separate bytes from tokens there.

### 15.4 Selection, layering, validation, override

- **The query.** A request is matched on five fields (`ProfileQuery`,
  include/blackbird/provider_profile.hpp), derived from the session's binding by `profile_query`:
  - `provider`: the binding's provider name;
  - `protocol`: the codec the main-coding route actually sends with, which is not always the
    descriptor's: `messages` for native Anthropic; `responses` for OpenAI's ChatGPT route and for
    OpenRouter, whose descriptor says `chat` (src/provider_auth.cpp:313) but whose main-coding
    adapter is Responses (src/main.cpp:528-532); a registered route's own `responses` or
    `responses-portable`. Any other binding is refused for main coding;
  - `base_url`: normalized (lowercase scheme and host, port 443 dropped, trailing `/` dropped);
    a non-https or malformed URL is refused. A profile's `match.base_url` must already be in normal
    form;
  - `account_kind`: the binding's `kind` (`oauth`, `api_key`, `environment`, `codex_chatgpt`, ...),
    never the account's name (src/provider_auth.cpp:797-806, src/openai.cpp:157-163);
  - `model`: a `match.models` entry `F` names a family, as src/anthropic_provider.cpp:486-492
    identifies families by name. It matches `M == F` (exact, the higher rank) or `M` starting
    with `F` followed by `-` and any suffix, such as a snapshot date (ranked like a prefix of
    length `F` + 1). The `-` keeps `claude-opus-5-50` out of the `claude-opus-5-5` family. A
    `match.model_prefixes` entry matches any `M` that starts with it, ranked by its length.
- **Selection.** The most specific matching shipped profile wins: more constrained dimensions, then
  an exact model over a prefix, then the longer prefix. Equal specificity is refused as ambiguous,
  never broken by file order. A shipped `conservative` profile matches everything else:
  `closed_only`, `turn.close = none`, boundary disabled, `keep`, `palette_only`, `tool_change = cut`,
  no cache controls, the current retry set.
- **Layers**, lowest first:
  1. the shipped `conservative` profile, the base under every profile;
  2. the operator override targeting `conservative`, if any. It sits at the base under every shipped
     profile, so one operator file changes the default everywhere (owner decision, 2026-10-20);
  3. the selected shipped profile;
  4. the operator override targeting that profile, if any;
  5. the session override (program record `provider-profile-override-v1`, staged to the workflow
     boundary).
  Operator override files live in `~/.local/share/blackbird/profiles/`; at most one per target. An
  absent operator directory means no overrides; any other error reading it refuses the set.
  Overrides are sparse; tables merge by key, arrays replace. A field a layer sets takes that layer's
  basis, or none.
- **Validation** checks what affects behaviour: schema, enumerations, numeric ranges, codec
  consistency (for example `allowed_tools` only where the codec sends it, `reply_then_note` only with
  `close = reply_request`), cache detail that contradicts `cache.capability`, and contradictory pairs
  (`drop_after_change` with `retain_current_turn`). Every override is validated in every effective
  profile it can reach at load: the override of `conservative` under `conservative` and under each
  shipped profile. An invalid profile is refused with the exact reason and the previous set stays in
  force.
- `basis` is recorded data, reported per request and shown by `/profile`; a repository test checks that
  every shipped behavioural field has one. Loading never refuses a profile or an override for missing
  provenance.
- **Digests.** The behavioural digest of a profile covers its schema and behavioural families only;
  the set digest covers every profile's id, match and behavioural digest. A basis, source or revision
  edit changes neither, so it causes no rebind. The set's content digest covers every layer whole,
  provenance included, and identifies exactly what a trial ran.
- Each request records `profile {id, revision, digest, layers, model, binding}`: `digest` is the
  effective profile's behavioural digest, `model` the request's own and `binding` a digest of the
  request group's route fields (provider, protocol, normalized base URL, account kind).
- **Owner decisions 2026-10-20 (P1b).** Profile files load only at session start; each request
  resolves from its final model and its request group's binding without rereading them. A
  binding whose base URL cannot be normalized runs under `conservative` with the reason recorded,
  rather than being refused. An unreadable auth store defers the binding to the first provider
  request, which fails as an auth-store refusal naming the store. The plan's P1b section has the
  detail.

### 15.5 Migration of hard-coded rules

| Rule today | Site | Profile field |
|---|---|---|
| explicit cache policy only for `gpt-6.1-sol` on two routes | src/request_cache.cpp:87-93 | `cache.capability` |
| TTL `30m` | src/request_cache.cpp:115 | `cache.ttl` |
| OpenAI cache route gate | src/coding.cpp:329-340 | `match` + `cache.capability` |
| cache virtuals | include/blackbird/coding.hpp:33, 35 | `cache` |
| `prompt_cache_key` = conversation | src/coding.cpp:4512-4514 | `cache.affinity.body_field` |
| `session-id` header | src/openai.cpp:405-411 | `cache.affinity.header` |
| OpenRouter `session_id` never set | src/openrouter.cpp:74-76 | `cache.affinity.body_field`, after a probe |
| `retain_request_state` | include/blackbird/coding.hpp:37; include/blackbird/anthropic_provider.hpp:15 | `request_state.placement` |
| retry set | src/coding.cpp:4727-4737 | `retry` |
| Anthropic effort rules | src/anthropic_provider.cpp:289-299 | `effort.values` |
| Anthropic thinking families and budgets | src/anthropic_provider.cpp:486-510 | `effort.thinking` |
| Anthropic stop reasons | src/anthropic_provider.cpp:202-209 | `stop_reasons` |
| Anthropic `max_tokens` 16384 | include/blackbird/anthropic_provider.hpp:8 | `output.max_tokens` (basis `conservative`) |
| portable effort | src/responses_provider.cpp:24-29 | `effort.values` |
| launcher effort offer | src/main.cpp:466-496 | `effort.values` |
| engine effort set | src/coding.cpp:5769-5772 | `effort.values` |

Each migrated rule keeps its behaviour exactly in its first profile, checked against the old code's
decisions (§20). Stateless full replay and the refusal of `previous_response_id` stay Blackbird policy
(decision 4). Route and auth constants move in a later profile unit. 5st's effort vocabularies, model
lists, plan restrictions, output ceilings and citations become the first profile contents;
campaign-defects waits for 5st, as the operator set.

## 16. Cache economics

### 16.1 Cost of a cut

With write multiplier `w`, read multiplier `r`, retained bytes after `p` `R` and archived bytes `a`, a
cut costs about `(w − r) × R` once and saves about `r × a` per later request, so it breaks even after
about `(w − r)/r × R/a` requests. The multipliers are profile data per model:

| Model | `w` (5 min) | `r` | `(w − r)/r` | 5% cut of the oldest (R/a ≈ 17.4) | 20% cut of late rounds (R/a ≈ 0.78) |
|---|---|---|---|---|---|
| Anthropic Fable 5.1, Mythos 5.1 | 1.25 | 0.025 | 49 | about 850 requests | about 38 |
| Anthropic Opus 5.5, Sonnet 5.5 | 1.25 | 0.05 | 24 | about 420 | about 19 |
| Anthropic others | 1.25 | 0.1 | 11.5 | about 200 | about 9 |
| OpenAI `gpt-6.1-sol` | 1.25 | 0.05 | 24 | about 420 | about 19 |
| OpenAI GPT-5.6+ others | 1.25 | 0.1 | 11.5 | about 200 | about 9 |
| cache already cold | — | — | — | about 0 | about 0 |

Sources: Prompt caching (Anthropic, "0.025x" and "0.05x" exceptions); Prompt caching (OpenAI, "0.1×
uncached input (0.05× for GPT-6.1 Sol)", write 1.25×). The R/a values are the report's examples at
H = 384 KiB.

**Dropped thinking costs output.** On `drop_after_change` profiles every cut drops the reasoning after
`p`: Anthropic measured "dropping thinking blocks once per session increased output tokens by 2.5% at
low effort, 3.9% at medium, 4.5% at high, and 20.1% at max" (Preserved thinking). This is a separate,
cited term; trials measure it in Blackbird.

So on a cached route, small early cuts rarely pay for themselves on price; their justification is
attention and headroom. The model is told the re-read bytes of each proposal and when the cache is cold,
and never told to prefer a position.

### 16.2 The model-facing cost note

- Cached routes: the §11.3 sentence, plus `[cache likely cold: the last request started 7 min ago, past
  this route's 5 min cache lifetime]` when the profile TTL has passed since the previous request's
  start: Anthropic measures the lifetime "from the start of the request that writes or reads the cache
  entry", OpenAI from "most recent write or reuse".
- Uncached routes, Anthropic included until bvu: "no extra re-read".

### 16.3 Keeping the prefix

- `tools` and `instructions` stay identical within a chapter by rule (§6.4).
- Affinity stays per conversation.
- Side requests keep the main request's effort and repeat its existing breakpoints, reading what the
  main requests wrote and writing nothing new; a side request whose tools differ carries none (§13.1).

### 16.4 Breakpoint plans (bvu, and the OpenAI profile)

**Anthropic** (up to 4 breakpoints, 20-position lookback, "Cache writes happen only at your breakpoint",
lifetime refreshed only when read):
- end of `system`: tools and instructions cached across every chapter;
- the moving tail, as `stable_prefix` places OpenAI's breakpoint today;
- `chapter_start`: on a chapter's first request, at the last entry before `p`. It hits only if a request
  in the last 5 minutes wrote an entry within 20 positions before `p`, which in practice means cuts of
  rounds appended in the last few minutes. Deeper cuts miss back to the `system` breakpoint.
- bvu decides whether to keep a persistent anchor that advances in steps of fewer than 20 positions,
  weighing its 1.25× write per step (2× with `ttl: "1h"`, which must precede any 5-minute entries), and
  measures before and after on a live route.

**OpenAI GPT-5.6+** ("the first 2 and latest 50 explicit breakpoints, the implicit breakpoint, up to 20
earlier eligible message endings, and the endpoint of the initial consecutive block of developer
messages" are the lookup boundaries; top-level `instructions` cannot hold a breakpoint):
- an explicit breakpoint on the first stable input item;
- on a chapter's first request, one on the last entry before `p`;
- rd7.2 checks the initial developer block and records the cached-token count after a deep cut. The SDK
  docstring's "latest 80 breakpoints" disagrees with the guide; the guide is followed until a probe says
  otherwise.

**Usage.** The Anthropic adapter keeps cache writes as their own field ("total_input_tokens =
cache_read_input_tokens + cache_creation_input_tokens + input_tokens", Prompt caching).

**Host reasoning knobs.** Where a host's template strips earlier reasoning (Fireworks, Together,
Cerebras), the profile sets the preserved mode.

## 17. TUI and operator commands

- **Reopen across chapters.** Today the chat is rebuilt from `context.items()` (src/main.cpp:1616-1634).
  The rebuild walks the chapter table newest first until the chat view's budget is full, shows live
  messages as today and archived ones from their originals, dimmed and folded under `── chapter 4 ·
  archived 31 units · 118 KiB ──`, reading each cut frame by identity.
- **Badge.** `ContextChangeKind` (include/blackbird/context.hpp:59-67) gains `cut`, and `ContextChange`
  gains the cut kind, changes and chapter ordinal. The mapping in src/main.cpp:659-670 shows `◆ CONTEXT
  archived −118 KiB · ch 4` (also `edited`, `restored`, `summarised`, `rebound`), `◆ CONTEXT RECOVERY ·
  critical` in the alert style, with `compacted` only for option B, `◆ CONTEXT archive pending (after
  turn)`, and `◆ CONTEXT forced cut` for §8.5's forced form.
- **Commands.** `/archive SELECTION`, `/archive cancel`, `/context` (the paged index), `/inspect UNIT`,
  `/session` with the chapter ordinal, `/profile`. `/compact` becomes `/archive`; `/originals` and
  `/restore` follow §11.7. The read-only state (§4.5) offers `/repair`.
- The station packet adds the chapter beside `context_head`.

## 18. Signals for trials

| Signal | Where it is written | Join key |
|---|---|---|
| chapter and revision per request | `provider.request` gains `chapter` (start revision), `chapter_ordinal`, `chapter_first`, `purpose` (main, closing, clm.step, clm.recovery), `profile {id, revision, digest, layers}` (src/coding.cpp:4619-4630) | attempt |
| same, per operation | DecisionEvent continuation gains `chapter`, beside `native_defaults` (src/coding.cpp:3363-3368) | decision, attempt |
| raw linkage | the assembly raw phase gains the chapter revision beside revision and definition generation (src/coding.cpp:4535-4537) | raw request ref |
| usage and cache per chapter | the provider attempt's `operation.result` (src/coding.cpp:3940-3943), with Anthropic cache writes kept separately | attempt → `provider.request.chapter` |
| settings in force | policy record revision; `provider-profile-set-v1`; profile per request; the operational state and step brief inside each request's retained input | attempt, record |
| every context change, proposal, outcome | `cut` frames: staged, settled, split, merged, rebased, refused, cancelled, forced, each naming its stage | revision, stage |
| model versus harness removals | `archived`, `swept`, `reasoning` lists; notes as `originals` with their class | cut revision |
| fetch-backs | admitted `context_inspect`, `read_process_output`, `read_file {snapshot}` operations whose input names an archived unit | handle → cut `archived` |
| reviews and closing replies | `clm.step` records, side-request attempts, `purpose = closing` requests; review intervals | attempt |
| truncated outputs | `output_window_exhausted` on accepted responses | attempt |
| recoveries and read-only sessions | `clm.recovery` records, critical complaints, recovery events (§4.5) | failed attempt, record |
| outcomes | turn outcomes, task state, complaints, provider errors (nls.16) | turn, attempt |

Lua-direct reads appear only in the raw context sites (1809, 1826), which is a source of record.

## 19. Other request makers and session paths

- **Backstop** keeps its own journal and engine (src/backstop.cpp:254-460); its successor seed is classed
  by §5.2's seeded rule, so C4 can make progress there.
- **`--seed-session` and `/new`** start new sessions; the first request writes `start`.
- **Participants, colleagues, Jev** never read `ContextStore`; their results are shaped outputs.
- **Child workflows** run inside `drain_workflows` before G's settle, so their results are in the context
  a cut sees.
- **RRC.** The `rrc:<token>` check reads the creating record's label of live entries by identity lookup
  (the class map holds only a byte) instead of scanning tail facts (src/session.cpp:571-586), which also
  closes the compact-mode checkpoint gap the state tribunal confirmed.
- **Model switching** is a rebind at G (§6.3, §8.5); `/model` runs between turns.
- **Station and application** feed the same context through ordinary turns.

## 20. Testing

Every test that changes behaviour must fail on the old code first, and that failure is recorded. Mutation
runs, when they come, run on the fleet.

| Mechanism | Oracle | Direct checks |
|---|---|---|
| `apply`, playback, snapshot | **Model oracle**: a small reference model of entries, classes, chapter table, pending and review state. **Metamorphic**: live state = snapshot restore = suffix playback = full playback after every operation of generated sessions | `result` count or CRC32C mismatch → `corrupt`; anchors (`start`, chained, removed, last surviving); disjoint removal lists; fresh-entry IDs resolve after a checkpoint; `existing` re-insertion keeps ID, handle and class; a maintenance checkpoint inside the cut's record call |
| Shared saved-context validator | **Specification**: each core field and each section corrupted in turn → whole-history playback with a recovery record, never an empty context | `load_slot` and ContextStore agree on every generated snapshot |
| Resume and recovery | **Specification** (§4.5) | unclean stop with accepted snapshot → suffix playback only; rejected snapshot → full playback within the 64 MiB policy; unplayable frame → read-only, no snapshot at quit; approved repair frame → playback skips and continues |
| Classes and protection | **Property** over generated contexts: no model or Lua operation removes, replaces or moves a protected or pinned unit, except archiving a superseded instruction copy; spoofed `origin`/`class` keys refused | seeded sessions archivable per source round; repaired rounds archivable without their instruction; `context_edit insert` items are `result` |
| Selections | **Model oracle**: expansion equals the set of live units satisfying the predicate | ranges, filter terms, handles of multi-unit records |
| Reasoning per profile | **Specification** from §6.2 | export through each adapter after a cut; final wire bytes for remaining reasoning |
| Turn state on the wire | **Specification** from §7.1, evaluated on the codec's rendered messages | an operator prompt merged after trailing tool results is `open` on Anthropic; the closing reply then a separate prompt is `closed`; `reply_then_note` and `user_note` meet their profile's start rule |
| Prefix changes | **Specification**: a tools or instructions change is a `prefix` cut, never sent silently; pending Lua tools are not sent before the cut | every row of §8.5, including the forced form |
| Settle points and pending | **Specification** from §8 | every row of §8.4; pending re-expanded from units; merge with a waiting remainder; rebase by a rebind and by a prefix cut in the same frame; split remainder restored after a crash between frames (there is only one); stage-matched clearing; pin after staging; order at G; reserve with carried pending |
| Request-local delivery | **Property**: view rows appear in exactly one request's input and in no context entry on request-local profiles | delivery skips requests with `omit` or overridden `input`; retaining profile: the view round swept by the next cut |
| Retained state | **Specification**: on retaining profiles the review text is appended once per meaning change | 100 requests above the nudge add one review entry, not 100 |
| Side requests | **Specification**: the step input equals the builder's transformed main input plus the brief, byte for byte; no context append, no slot write; breakpoints only where the main request has them | Anthropic step with `drop_all`; decline; split settlement; skip when nothing can settle |
| C4 | **Specification** at the classified-overflow seam | request group unwound before side requests; A with recent rounds; B; terminal outcome under each turn rule and on DeepSeek; `output_window_exhausted` appended, complete calls answered, truncated `tool_use` answered without dispatch |
| Profiles | **Specification** for validation; **regression oracle against the old code** for the migration | non-data constructs refused; overrides without basis accepted; contradictory pairs refused |
| Tool contracts | **Specification** per §11 | between S3b and V1, existing tools return `staged` and when the change takes effect (S3b ships it) |
| TUI | **Specification** on screen expectations | reopen across three chapters; badges; pending cut; read-only state |
| C6b (later) | deterministic simulation of the worker against a fake job and a blocking provider; TSan | completion on time; proposal settles at G; precedence |

Live probes: §7.5 (rd7.2, rd7.3), each tool restriction per route, note placement on Paper and MiMo,
Anthropic cache (bvu), and the C6a live run on luna after rd7.2.

## 21. Delivery units

Each unit owns one invariant, and no unit ships a field or behaviour that another unit fills or describes
later (BLACKBIRD.md, "Delegation units are conceptual units").

| Unit | Invariant it owns | Content | Depends on | Blocked |
|---|---|---|---|---|
| S0 Clean-stop snapshots (7c8) | every clean stop leaves a snapshot with an empty suffix | snapshot at quit, exit, session switch and shutdown; clean stop recognised by an empty suffix; its cost measured | — | — |
| P1 Profile mechanism | a request runs under exactly one validated, recorded profile | reader, schema, validation, layering, override records, `provider-profile-set-v1`, per-request record, `conservative` profile, `/profile` | — | — |
| P2 Profile migration | migrated rules decide exactly as the old code | §15.5 into shipped profiles | P1 | — |
| 5st (existing) | effort and output settings follow vendor docs | 5st contents as profile data | P1 | campaign-defects waits on it |
| P3 Anthropic system-message codec | Blackbird can send a real mid-conversation system message where the profile asks | adapter rendering of `role: "system"` messages with the documented placement; `inline-tools-2026-09-15` header plumbing; `tool state` class handling | P1, S2 | enabling waits on rd7.3 |
| S1 Context persistence | live state = reopened state | `cut` frame format and `apply` (§4.3, including the rule that the writer holds no references into `originals_` across the record call and that checkpoints stay best-effort); replay `cut`, format and originals branches; snapshot v2 core with the section framework and the shared validator in `load_slot` and ContextStore; resume table (§4.5): suffix playback, whole-history playback for a rejected snapshot, read-only state with suppressed snapshots, `repair` frames | S0 | — |
| S2 Units and classes | only selectable units can be selected | class map and its `classes` section; legacy and seeded derivation; round membership by call ID; handles; selection grammar; protected units and the superseded-copy exception; pins record and settle recheck; `blackbird.append` class option; RRC check by creating-record label | S1 | — |
| S3a Cut composition | the input a cut produces is one the provider accepts | composition of archive, edit, restore, summary; reasoning rules; tools and instructions digests and `tool_change`; start, rebind and prefix cuts and their rebase of a pending proposal; archive note text and placement; boundary note forms; sweeps; the `chapter` section | P1, S1, S2 | boundary forms disabled until rd7.2 / rd7.3 |
| S3b Cut timing | a cut takes effect only where the provider's turn rules allow it, and never wedges the session | wire-level turn state; the closing reply; staging of every non-append change; settle points G, J, turn start, idle and their order; merge, split and durable pending with its `pending` section; §8.5 outcomes and the forced form; reserve sizing; **the result and description changes of every existing tool whose timing it changes** (`context_manage`, `context_edit`, `context_restore`, Lua `manage`, `edit`, `restore`): `staged` and when it takes effect (recheck B11) | S3a | — |
| T1 Shaping at creation | every model-visible output is shaped once before it is sent | generic cap and `result_ref`; head and tail; budgets on native shaped tools after measuring their schema bytes; `tool_define` shape metadata; continuation results shaped in `drain_workflows`; the exec read-back cost measured | — | — |
| V1 Inspection and contracts | inspection never inflates context, and every offered choice is honoured | request-local delivery; retaining-profile view handling; row format; `context_inspect` text pages; §11 schemas and descriptions; removal of `context_originals`, `context_restore`, `select` together with their replacements; Lua return values | S2, S3b, T1 | — |
| E1 Escalation | guidance informs and never sets a size goal | policy v2; size line; `CONTEXT_REVIEW`; retained state only on meaning change (folds in 2o9); review interval and its `review` section; docs (CONTEXT_BUDGET.md, USING_BLACKBIRD.md) | V1, S3b | — |
| c3f (existing) | briefs are appended once per revision | ultracode declares `instruction` and appends once per brief revision | S2 | — |
| A1 C6a archival step | a review offers only honoured actions and never touches the main context | pure request builder; side request with the main breakpoints; brief; tool restriction (`palette_only` shipped, `allowed_tools` where probed); change notice; `clm.step`; Jev column | P1, S3b, V1, E1 | live run after rd7.2; each Anthropic model's switch to `appended_removal` after its rd7.3 run meets §24's criteria, and P3 |
| R1 C4 recovery (a1v.5) | a fired recovery is audited as critical and retried once in a new chapter | detection classes at the seam; `output_window_exhausted` handling in the adapter; unwinding; options A and B; turn rules and terminal outcome; `clm.recovery`; harness-raised complaint | S3b, A1 (builder), P1 | mid-turn Anthropic recovery beyond §14 waits on rd7.3 |
| a1v.6 (existing) | live overflow wiring | profile error rules through nls.16 | R1, nls.16 | nls.16, after campaign-defects with 5st |
| K1 Cache (bvu) | the cache plan matches what the provider documents and measures | breakpoint plans, multipliers, cache-write usage, OpenRouter `session_id`, persistent-anchor decision | P1, P2, S3a | live measurement needs an Anthropic account |
| U1 TUI | the operator sees every context change and pending cut | `ContextChange` kinds; reopen across chapters, badges, `/archive`, `/archive cancel`, `/repair`, `/session`, station chapter | S1, S3b | — |
| A2 C6b | a concurrent review is audited and never outranks the model | worker, admission before spawn, mailbox, precedence, TSan | A1 and its timing data; codex auth race fix | — |
| ib8.1 (existing) | every CLM decision is recoverable | tertiary check of every §18 row | S3b, A1, R1, K1 | — |
| rd7.2, rd7.3 (existing) | provider facts are probed, not assumed | §7.5 | — | they block enabling, not building |

S0, P1 and T1 can start in parallel; S1, S2, S3a and S3b form the spine in that order.

## 22. Rejected alternatives

- **Provider or server compaction** (OpenAI, xAI, Anthropic, Azure): decision 4.
- **Anthropic `clear_tool_uses`**: unpublished placeholder, excludes by tool name only so it cannot honour
  pins, removes the locator.
- **Rewriting old outputs in place**: invalid for later thinking on prefix-checked models, a cache rewrite
  everywhere, and against decision 5.
- **A summary as the nominal path**: decision 1.
- **A new conversation ID or session per chapter**: splits the TUI scope and OpenAI affinity and strands
  locators.
- **A new `ContextChapterId` type**: the start frame's revision already identifies a chapter.
- **Full-entries cut frames**, and **a snapshot after every cut**: both copy the whole context per cut.
- **One cumulative note at the head**: every cut would rewrite near the start of the prefix.
- **A `retain` argument on every tool**: a second way to pin, an ephemeral flag that usually did nothing,
  and fixed bytes on every request.
- **Dense per-session ordinals for handles**: they need a persisted table; the issuer counter is in every
  ID.
- **Sweeping view outputs at the next settle point regardless of position**: each view would cost a cut.
- **Request-local view delivery on retaining profiles**: the rows would be retained anyway, or the current
  turn's thinking dropped.
- **A harness-authored assistant acknowledgement to close a turn**: Blackbird never writes in the model's
  voice; the closing reply (§7.2) has the model write it.
- **A separate harness user message before the operator prompt as the closing boundary on Anthropic**: the
  adapter merges consecutive user blocks (src/anthropic_provider.cpp:346-351), so it would still sit in the
  tool-result message; only an assistant message in between closes the turn.
- **Whole-history playback after every unclean stop**: against "archived history must not force resident
  payloads or full replay merely because it exists" (AGENTS.md).
- **A model-facing `move`**: it would change a unit's identity or split a round; reorders stay a Lua
  capability with `existing` re-insertion.
- **Object selections with `anyOf`**: not every provider's function schema accepts it; the string grammar
  expresses the same filters.
- **Profile loading by running Lua in an empty environment**: it executes the file.
- **Refusing overrides that lack provenance**: receipt ceremony.
- **Forced tool choice for the step**: declining must stay possible.
- **A harness-authored archive in recovery (option C)**: rejected by the operator.
- **CLM every round**: decision 2.
- **A cold-cache early direction**: a second trigger dimension before trials; the cost note carries the
  cache state.
- **Jev at creation or Jev deciding**: decision 12.
- **"Replay needs no new logic"**: withdrawn; §4.3 specifies the new playback branch.

## 23. Decisions taken in this draft

The doctrine and the evidence settle these; the operator may overturn any of them.

1. **C4 order: option A, then option B only when A does not make the request fit.** A keeps history
   verbatim and leaves the choice with the model; B is compaction proper, the deeper failure. Both are
   fully implemented (operator, 2026-10-19). A decides under a failure, so it carries the most recent
   rounds verbatim beside the index.
2. **Superseded instruction copies.** When a byte-identical copy of an instruction (the ultracode brief,
   c3f) is retained later in the context, the model may archive the older copy by its own selection
   (§5.1, §11.3). The harness never sweeps it, and the newest copy stays protected. c3f's fix removes most
   cases anyway.
3. **No pin ceiling.** `pinned_bytes` is visible at every level, and trials (ib8) show whether pinned
   growth ever needs more than visibility.
4. **A pending cut survives a failed turn, a crash and an operator interrupt**, and settles at the next
   permitted settle point; only `/archive cancel` cancels it. Changed in revision 2: a plain interrupt is
   not a rejection of pending work, the archival settles no effects, and every original stays retained.
5. **Renames** that follow from the operator's definition of compaction as a failure: `/compact` →
   `/archive`; the badge's `compacted` → `archived` on the nominal path, with `compacted` reserved for C4
   option B; `ContextChangeKind` gains `cut`; the C6 "compaction step" is the archival step;
   `compaction_campaign_test` keeps its name.
6. **Capabilities removed from the model**, each replaced in the same unit: `context_originals`,
   `context_restore`, the `select` mode, `context_edit move`, `context_inspect` kinds `history` and
   `index` (kept for Lua and operator), Lua `blackbird.restore`. The `retain` argument is never shipped.
7. **Levels and starting values**: nudge 384 KiB (measured); direction 432 KiB (grounded in §12.1;
   revision 2 had 448 KiB, which the recheck showed was not under the largest accepted request at the 10%
   end); review interval 20 rounds; generic output cap 16 KiB, 4 KiB head + 12 KiB tail. The gap between
   nudge and direction is narrow; ib8 trials retune all of them.
8. **Profile defaults until probes**: Anthropic `closed_only` with the closing reply and the boundary
   disabled; unprobed hosts `closed_only`; `tool_change = cut`; `palette_only` for the Anthropic step,
   switching per model after rd7.3 (§24); DeepSeek removal refused; OpenAI `keep` with
   `retain_current_turn`.
9. **The closing reply** (§7.2): on Anthropic, Blackbird closes an open tool-use turn, when something needs
   it closed, by asking the model for a one-line status with `tool_choice: none`; it never writes an
   assistant message itself.

## 24. Decision record: the Anthropic step restriction

**Decision (owner, 2026-10-20).** `palette_only` ships. Each Anthropic model switches to
`appended_removal` once its rd7.3 run shows both criteria: **a tool removed with `tool_removal` cannot be
called, and thinking survives.** Anthropic's own enforcement of `tool_removal` counts as native
restriction under the rule "never offer choices that won't be honoured; restrict natively or offer only the
honoured action". bvu is not a condition of the switch; cache economics is a separate bvu measurement.

What the two modes do, for the record:

| | `palette_only` | `appended_removal` |
|---|---|---|
| What is sent | the context, `tools = [context_manage]`, the brief | the context, the full `tools` as first sent, `tool_choice: auto`, an appended `role: "system"` message carrying the brief and a `tool_removal` block for every other tool (beta `inline-tools-2026-09-15`; unit P3) |
| Strictness | only one tool exists in the request | the provider removes the other tools; confirmed per model by rd7.3 before the switch |
| Reasoning in the step | all thinking dropped from the step's copy (a changed `tools` invalidates every block) | kept: `tools` unchanged, so the prefix check holds |
| Cache | none possible: the tools differ, so no earlier write matches and the side request carries no breakpoint | the side request repeats the main request's breakpoints, so once bvu caches the main context it reads that prefix; before bvu, Anthropic requests are uncached and both modes pay full price |
| Input cost per step at the direction level (432 KiB native, about 110k tokens at ~4 bytes per token; Anthropic's ratio in Blackbird is unmeasured) | about 110k tokens at full input price | before bvu: the same; after bvu: about 5.5k full-price equivalents on Opus/Sonnet 5.5 (0.05×) or 2.8k on Fable 5.1 (0.025×), plus the uncached brief |
| Output | the step reasons without its prior thinking; Anthropic measured 2.5-20.1% more output after a drop, by effort | no drop |

## Appendix A. Tribunal dispositions and recheck fixes

Reports: **P** = `provider_protocol.md`, **S** = `state_replay_concurrency.md`, **D** =
`doctrine_ergonomics_waste.md`. "Unverified" items in P are carried into the probes of §7.5 and §16.4.
Section numbers in this table refer to revision 3.

| Finding | Disposition | What changed, where, or why rejected |
|---|---|---|
| P-F1 (high) tools/system change breaks the chapter | accepted | §3, §4.1 cover the whole prefix; §6.4 `tool_change` (`cut` default, `appended_system_message` after rd7.3), digests in the binding; §8.5 outcomes; §20 |
| P-F2 Anthropic step discards reasoning and cache | changed | owner decision recorded in §24: `palette_only` ships, per-model switch to `appended_removal` after rd7.3; §13.1 keeps the main effort and repeats the main breakpoints |
| P-F3 break-even uses wrong read price, omits dropped-thinking output | accepted | §16.1 formula with profile multipliers, corrected table, separate output term |
| P-F4 `chapter_start` reads only recent writes | accepted | §16.4 states the narrow case; persistent anchor decision assigned to bvu |
| P-F5 OpenAI lookup boundaries | accepted | §16.4 OpenAI breakpoint plan; rd7.2 checks (§7.5) |
| P-F6 `model_context_window_exceeded` is a truncated success | accepted | §14 `output_window_exhausted`, including calls; §15.2 `stop_reasons` |
| P-F7 overflow patterns mostly community; gap parsing | accepted | §15.3 per-rule basis, bounded number extraction, unknown-gap fallback, cloud rows before shipping |
| P-F8 mid-turn Anthropic outcome likely not a 400 | accepted | §7.4 `closed_only` basis `conservative`; §7.5 probe checks; §14 wording |
| P-F9 DeepSeek whole-round archival | accepted | §6.2 refusal of every removal kind until probed |
| P-L1 Anthropic note role basis | accepted | §5.5 basis is Blackbird's rendering; system-rendering placement flag |
| P-L2 Kimi "must" | accepted | §6.2 `must_keep` |
| P-L3 Mistral basis | accepted | §5.5 documented flow table plus community reports |
| P-L4 Gemini turn definition | accepted | §7.1 start rule `user_text` |
| P-L5 overloaded event field | accepted | §15.2 `event = "error"`, `error_type = "overloaded_error"` |
| P-L6 429 without `retry-after` | accepted | §15.2, §15.3 |
| P-L7 `max_tokens` basis | accepted | §15.2, §15.5 basis `conservative` |
| P-L8 forced tool use wording | accepted | §13.1 precise model list |
| P-L9 rebind within Anthropic, account binding | accepted | §6.3 `keep_in_family`, account in binding |
| P-L10 restore and dropped thinking | accepted | §4.3 inserted and re-inserted entries count at or after `p` |
| P-L11 idle measured from request start | accepted | §16.2 |
| P-L12 OpenAI `reasoning.context` | accepted | §6.2 recorded in the profile |
| S-H1 (high) rejected snapshot silently resets context | accepted | §4.4 one shared validator; §4.5 whole-history playback for a rejected snapshot, never an empty head; S1 |
| S-H2 (high) delta playback unverified, inserts underspecified | accepted | §4.3 one `apply`, exact rules, count and CRC32C verified; §22 withdraws "no new replay logic" |
| S-H3 (high) edit and restore publish immediately | accepted | §8.1 every non-append change stages; §5.3; §11.5, §11.8 |
| S-M1 pending basis lost; `candidate` projected out | accepted | §4.2 `proposal` held as units and a basis reference; §4.3; §8.3 |
| S-M2 outcomes do not name the stage | accepted | §4.2; turn-rule waits clear nothing |
| S-M3 fresh entry IDs | accepted | §4.3 step 5 |
| S-M4 entry `origin` legacy and spoofable | accepted | §5.2 class per creating record; candidates refuse `origin`/`class`; §4.4 v1 rebuild |
| S-M5 seeded, Lua-synthetic, repaired rounds | accepted | §3, §5.1, §5.2 |
| S-M6 pins not rechecked or atomic | changed | §5.4 recheck at settlement, `refused` field in §4.2; `retain` removed; a cut never writes pins |
| S-M7 side request appends and clobbers slots | accepted | §13.3 pure builder and committer |
| S-M8 C4 inside the request group; state entry | accepted | §14 unwinding |
| S-M9 binding per request; order at G | accepted (after recheck) | §6.3 binding resolved before G; §8.2 order: start, harness cut with rebase, settlement, step; §8.3 rebase or `units-gone` |
| S-M10 reserve ignores carried pending | changed | §8.3 reserve with carried pending and exact frames; interrupt writes no frame (§23 decision 4) |
| S-M11 safe mode on an unplayable frame | accepted | §4.5 read-only state and `repair` frames |
| S-M12 delivery order | accepted (after recheck) | §21: snapshot sections owned by their producers; durable pending only in S3b; S3b ships the timing-related tool results and descriptions |
| S-M13 C6b audit, precedence, CoW race | accepted | §13.4 |
| S-L1 chapter locator has no reader | accepted | §4.4 revisions, read by identity |
| S-L2 restore after archive | accepted | §5.5, §5.7 |
| S-L3 J is not a provider turn boundary | accepted | §7.1 turn state on the wire; §8.2 |
| S-L4 maintenance checkpoint inside a record | accepted (after recheck) | §4.3 "Memory safety around the record call" and the S1 row; per-cut snapshot removed |
| S-L5 settle with overridden `input` | accepted | §8.2 |
| S-L6 idle settlement of carried pending | accepted | §8.2 |
| S-L7 `operator.beads` classed instruction | accepted | §5.2 class `result` |
| S-L8 contradictory profile pair | accepted | §7.4, §15.4 |
| S-L9 pinned instructions over the window | accepted | §14 terminal outcome |
| D-H1 (high) 32-hex IDs, list-only selection | accepted | §3 handles; §11.1 string grammar with ranges and filters |
| D-H2 (high) current-turn rule makes the nominal path inert | accepted | §7 wire-level turn state, closing reply, boundary forms probed in rd7.2/rd7.3; `after-turn`; split and merge (§8.3); step skipped when nothing can settle |
| D-H3 (high) retained nudge appended every request | accepted | §12.2; 2o9 folded into E1 |
| D-H4 (high) `retain` argument | accepted | removed (§9, §22) |
| D-M1 size target via thresholds and hysteresis | accepted | §12.1-§12.3 |
| D-M2 inspection output stays | accepted | §10.1 request-local delivery; §10.2 |
| D-M3 cost rule ambiguous, false without cache | accepted | §11.3, §16.2 |
| D-M4 model not told after a step cut | accepted | §13.1 `CONTEXT_CHANGED` |
| D-M5 one call, one mode | accepted | §11.3; `changes` set in §4.2 |
| D-M6 snapshot after every cut | accepted | removed (§4.4) |
| D-M7 view row size invented, keyed objects | accepted | §10.3 |
| D-M8 generic cap without override, head only | accepted | §9 |
| D-M9 interrupt discards pending | accepted | §8.4, §23 decision 4 |
| D-M10 option A blind | accepted | §14 recent rounds verbatim |
| D-M11 renames and removals not listed | changed | §23 decisions 5-8, per the owner's direction |
| D-M12 provenance validation | accepted | §15.4 |
| D-M13 Lua appends pinned; edit reach | accepted | §5.2, §5.3, §11.5 |
| D-M14 delivery units break integrity | accepted (after recheck) | §21: S3 split into S3a (composition) and S3b (timing), one invariant each; `ContextChange` in U1; sections owned by producers |
| D-M15 schemas and descriptions unspecified | accepted | §11, without `$ref` or `anyOf` |
| D-L1 `ContextChapterId` redundant | accepted | §3 |
| D-L2 per-entry origin string | accepted | §5.2 one-byte class |
| D-L3 inspect returns reasoning and bodies | accepted | §10.4, §11.4 `part` |
| D-L4 message rows carry no content | accepted | §5.5, §10.3 |
| D-L5 writer-side transformations | accepted | §13.1; no `rules` field |
| D-L6 side request details | accepted | §13.1 exact step input, breakpoints per B10 |
| D-L7 inaccurate round-ID claim | accepted | §3 |
| D-L8 brief omits decline and pin consequences | accepted | §13.1 |
| D-L9 archived row source | accepted | §10.3 |
| D-L10 hex IDs in frames | accepted | §4.2 native refs |
| D-L11 narrow nudge-direction gap | changed | level moved to 432 KiB for the recheck's sizing low; widening rejected for now (no evidence for another value; ib8 retunes it); §23 decision 7 |

### Recheck fixes

| Recheck item | What changed |
|---|---|
| S-M9 (partial) | §8.2 fixed order with the harness cut before settlement; §8.3 rebind and prefix cuts rebase the pending proposal in their own frame, or refuse it `units-gone` |
| S-M12, D-M14 (partial) | §4.4 snapshot sections owned and validated by their producing units; §21 S0 added, S3 split into S3a and S3b with one invariant each, pending only in S3b, `ContextChange` in U1 |
| S-L4 (partial) | §4.3 "Memory safety around the record call"; the S1 row states it |
| B1 (high) turn rules on stored entries | §7.1 turn state evaluated on the codec's rendered messages; Anthropic closed only by a user message without results; §7.2 the closing reply with `tool_choice: none` and what Blackbird sends after a step-budget exit; §7.3 boundary forms `user_note` and `reply_then_note` meet their start rules, `system_message` is a probed candidate; merged-prompt case added to rd7.3 (§7.5) |
| B2 rebind breaks pending | §8.3 pending held as units, re-expanded at settlement, rebased in the rebind or prefix frame |
| B3 split writes two frames | §4.2 `pending` field; §8.3 one frame carries the remainder |
| B4 remainder blocks later proposals | §8.3 merge; `pending-overlap` retired; §13.1 step not blocked |
| B5 harness cuts with no outcome | §8.5 outcome table per kind and turn rule; forced form; old tools kept until the cut |
| B6 `move` changes identity | `move` removed from the model contract (§11.5); §4.3 `existing` re-insertion for Lua reorders |
| B7 decision 2 vs §5/§11 | §5.1 superseded-copy exception; §5.3, §10.3 flag, §11.3 description; §20 test |
| B8 whole-history playback after unclean stops | §4.5 resume table: suffix playback after an unclean stop, whole history only for a rejected snapshot within the 64 MiB policy; S0 delivers 7c8 |
| B9 read-only snapshot bypasses repair | §4.5 snapshots and maintenance checkpoints suppressed until the repair frame is accepted; §20 |
| B10 side requests stop cache reads | §13.1 and §16.3 repeat the main breakpoints, none after them; §24 numbers corrected |
| B11 S3 changes timing before contracts | S3b ships the result and description changes of every existing tool whose timing it changes (§8.1, §21) |
| Low: refusal fields, `turn-open` | §4.2 `refused` field; turn-rule waits clear nothing |
| Low: boundary anchor | §4.3 step 4, §7.3 last surviving entry, chained |
| Low: skipped entries | §4.5 the repair frame may insert chosen skipped originals; the rest via audit inspection |
| Low: `encoded_bytes` check | §4.2, §4.3 count and CRC32C checked; bytes recorded only |
| Low: `change` single enum | §4.2 `changes` set; archive and restore of the same unit refused |
| Low: handle uniqueness | §3 tied to the single-namespace journal; segment-qualified form for later |
| Low: request-local delivery gaps | §10.1 delivered on the next request that carries operational state; crash loss stated |
| Low: class of `context_edit insert` | §5.2, §11.5 class `result` |
| Low: DeepSeek other kinds | §6.2 every removal or replacement kind refused, C4 terminal |
| Low: budget arguments | §9 schema bytes measured before shipping; Lua-defined tools shape by `tool_define`; continuation shaping in `drain_workflows` |
| Low: `$ref` | §11 schemas use no `$ref` or `anyOf`; string selection grammar |
| Low: example handle | §5.5, §7.3 the note is `r5100.0` |
| Low: `output_window_exhausted` with calls | §14 complete calls answered; truncated `tool_use` answered without dispatch; adapter accepts the stop reason with calls |
| Low: effort and the binding | §6.3 effort not in the binding; allowed mid-turn |
| Low: RRC label | §19 creating record's label by identity lookup |
| Low: real system messages | unit P3; §6.4 `tool state` class; §7.3, §13.1 depend on P3 |
| Low: recovery under `retain_current_turn` | §14 turn rules and terminal outcome under every rule |
| Low: token estimate | §12.1 level moved to 432 KiB (about 137k-145k tokens, under 147,640) |
| Text framing §24 as open | Contents, §13.1, §7.5, §21 A1 row, §23 decision 8, P-F2 row, §24 retitled "Decision record" without the bvu condition |

No recheck item is left unresolved. One judgement worth noting for the plan: the closing reply (§7.2)
adds one short request when a turn must close, and how models answer it is a behaviour rd7.3 observes.
