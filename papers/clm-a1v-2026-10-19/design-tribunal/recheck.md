# Recheck: CONTEXT_LIFECYCLE.md revision 2 against the tribunal

This is the single layer-2 recheck under docs/BOUNDED_HARDENING.md. I checked the revised
`docs/CONTEXT_LIFECYCLE.md` against the three tribunal reports in this directory. Where the
design states source behaviour, I checked it against `ae81fbe`. The worktree HEAD `b51a9da`
changes only `JOURNAL.md` and `scripts/benchmark-startup` after `ae81fbe`. The design's claim
that `context.cpp`, `saved_state.cpp`, `retained_state.cpp`, `main.cpp`,
`anthropic_provider.cpp`, `session.cpp` and the other listed files are unchanged since
`f7a79b1` holds: `git diff --stat f7a79b1 ae81fbe` lists none of them. Nothing was built or
run. Settled operator decisions were not reopened.

Owner decision since the revision: `palette_only` ships, and each Anthropic model switches to
`appended_removal` once rd7.3 shows that removed tools cannot be called and that thinking
survives. Text that still frames this as open is listed at the end of section B.

## A. Disposition check

Provider and protocol report (P):

- P-F1 (high), tools/system change: **held.** §4.1 and §6.4 cover the whole prefix,
  `tools_digest` and `instructions_digest` are in the binding, and §20 has a test. Source
  checked: Lua tools activate only at the top-level boundary (coding.cpp:6097, 6112), and
  `Host cwd` comes from `current_path()` (coding.cpp:4357). See B5 for the case where the turn
  rule forbids the required cut.
- P-F2, the Anthropic step discards reasoning and cache: **held** as the owner has now decided
  it. §13.1 keeps the main request's effort. However, the "no new cache write" direction was
  written as "no `cache_control`", which also stops cache reads (B10).
- P-F3, break-even arithmetic: **held.** I recomputed the §16.1 table: 49, 24 and 11.5 times
  R/a give about 850/38, 420/19 and 200/9. The output cost of dropped thinking is carried as a
  separate term.
- P-F4, `chapter_start` reads only recent writes: **held** (§16.4; the anchor decision goes to
  bvu).
- P-F5, OpenAI lookup boundaries: **held** (§16.4 plan; rd7.2 checks the developer block and
  the cached-token count).
- P-F6, `model_context_window_exceeded`: **held.** It becomes `output_window_exhausted` and the
  response is appended. A residual adapter gap is in B, low.
- P-F7, overflow patterns and gap parsing: **held.** Every rule has a `basis`. The bounded
  "first integer after literal" extraction is defined, and it parses the Anthropic example
  correctly. Unknown gaps fall back to half the bytes.
- P-F8, mid-turn Anthropic outcome: **held.** The basis is `conservative`, and §7.4 records
  thinking presence, `input_transformations` and the client-drop case.
- P-F9, DeepSeek whole-round archival: **held** for `archive`. Other non-append kinds are not
  covered (B, low).

State, replay and concurrency report (S):

- S-H1 (high), a rejected snapshot resets the context: **held.** §4.4 adds one shared
  validator called from `load_slot` and from the constructor. On rejection the constructor
  enters whole-history playback through `history_snapshot()` (retained_state.cpp:787-899
  exists and reads archived facts from the catalogue), and never takes an empty head.
- S-H2 (high), delta playback: **held.** §4.3 has one `apply` with removal, replace, insert,
  start-anchor, chaining and disjointness rules, and checks `result` (count, CRC32C, bytes).
  The CRC32C cite is correct: journal.cpp:95. §22 withdraws "no new replay logic".
- S-H3 (high), edit and restore publish immediately: **held.** §8.1 stages every model and
  program non-append change. §5.3 and §11.5 protect units. The source confirms today's
  immediate publish (coding.cpp:5615-5622, 1648-1657).
- S-M1, pending basis and the `candidate` projection: **held.** The proposal field is renamed,
  the basis is stored as a `{count, last, observed}` reference, and `expected` advances on
  playback. The source confirms that settlement requires the basis to be an exact prefix
  (context.cpp:1583-1592). B2 breaks this.
- S-M2, outcomes naming the stage: **held** (§4.2). B, low, covers an inconsistency in the
  `turn-open` reason.
- S-M3, fresh-entry IDs: **held** (§4.3 step 5, §20).
- S-M4, origin is legacy and spoofable: **held** for edit candidates. B6 shows that `move`
  reopens the class change by another route.
- S-M5, seeded, Lua-synthetic and repaired rounds: **held** (membership by call ID, seeded
  grouping, Lua rows). `lineage.source_entries` exists (context.cpp:913-924).
- S-M6, pins not rechecked or atomic: **held.** There is a recheck at settlement, and `retain`
  is gone. The frame schema has no field for the `pinned` refusals (B, low).
- S-M7, side request appends and clobbers slots: **held.** §13.3 splits a pure builder from a
  committer. The raw nesting question is still marked unconfirmed.
- S-M8, C4 inside the request group: **held.** The group is unwound, control returns to G, and
  the state entry is swept.
- S-M9, per-request binding and order at G: **partially held.** The binding is resolved before
  G, `binding-change-mid-turn` is added and the order is fixed. But "a rebind advances the
  pending cut's expected head" contradicts the basis-prefix settlement it relies on (B2), and
  the prefix cut at the same step is not covered.
- S-M10, the reserve ignores the carried pending: **held.** The reserve is sized with the
  carried pending, the `cut` frames are priced, an interrupt writes no frame, and
  `/archive cancel` fails visibly. The source confirms the turn-start reserve uses an empty
  proposal today (coding.cpp:6042-6043).
- S-M11, safe mode on an unplayable frame: **held** as specified: read-only, plus an
  operator-approved `repair` frame with a pre-pass. B8, B9 and a low in B cover new gaps
  around it.
- S-M12, delivery order: **partially held.** `retain` is gone, V1 follows S3, and E1 follows
  V1 and S3. But S1 now ships snapshot v2 fields (class map, chapters, pending, review) whose
  producers arrive in S2, S3 and E1, which is the "field nothing fills" pattern the finding
  named. "Durable pending" is listed in both S1 and S3. And S3 changes the timing of existing
  tools before V1 changes their descriptions (B11).
- S-M13, C6b: **held** (admission before spawn, mailbox, unstaged until G, early TSan; the
  `value.hpp:92` cite is correct).

Doctrine, ergonomics and waste report (D):

- D-H1 (high), 32-hex IDs and list-only selection: **held.** Handles come from the issuer
  counter, with ranges and filters. The handle-to-identity derivation is sound only while the
  launcher refuses linked journals (B, low). One example reuses `r4870` (B, low).
- D-H2 (high), the current-turn rule makes the nominal path inert: **held** as a stated,
  probe-gated decision. Units are marked `after-turn`, proposals split, and the step is
  skipped when nothing can settle. With the shipped defaults (Anthropic `closed_only`, OpenAI
  `retain_current_turn`, boundary disabled) in-turn archival stays unavailable on both main
  providers until rd7.2/rd7.3, and §21 says so. B4 shows that a split remainder re-blocks the
  path.
- D-H3 (high), retained nudge on every request: **held** (§12.2 appends only on a change of
  meaning, with no changing numbers in the retained form; the task page only on a revision
  change).
- D-H4 (high), `retain` argument: **held** (removed in §9 and §22).
- D-M1, size target through thresholds: **held** (levels are never shown, the interval is in
  rounds, outcomes are recorded as they were).
- D-M2, inspection output stays: **held.** Request-local delivery is used, and retaining
  profiles are described honestly. Delivery gaps are in B, low.
- D-M3, the cost rule is ambiguous: **held** (profile-driven sentences, `reread_bytes`, no
  advice on position).
- D-M4, the model is not told after a step cut: **held** (`CONTEXT_CHANGED`).
- D-M5, one call, one mode: **held.** The frame's single `change` enum cannot label a combined
  call (B, low).
- D-M6, a snapshot after every cut: **held** (removed). The remaining reliance on 7c8 is in B8.
- D-M7, row size and keyed objects: **held.** The construction bounds check out
  (140 × 64 ≈ 9 KiB, 140 × 256 ≈ 35 KiB).
- D-M8, generic cap without override: **held.** Budgets apply on every shaped tool, with head
  plus tail. Residual issues with schema bytes and parameter collisions are in B, low.
- D-M9, an interrupt discards pending: **held** (§8.4, decision 4, `/archive cancel`, badge).
- D-M10, option A is blind: **held** (recent rounds verbatim within the budget).
- D-M11, renames and removals not listed: **held.** They are listed in §23 as decisions the
  operator may overturn, per the owner's direction.
- D-M12, provenance validation: **held** (§15.4: recorded, never refused at load, repository
  test).
- D-M13, Lua appends pinned and the reach of edit: **held** (class option, default `result`;
  §11.5 description).
- D-M14, delivery units break integrity: **partially held.** U3, U4 and U5 are re-cut and the
  dependencies are fixed. But S3's stated invariant is two invariants joined by "and"
  ("no cut yields an input the provider refuses, and nothing publishes past the turn rule"),
  and S3 also carries non-validity content (note wording, `ContextChange`). Pending
  persistence is split between S1 and S3.
- D-M15, schemas and descriptions unspecified: **held** (§11 gives full schemas, descriptions
  and results). The `$ref` form is in B, low.

Spot-check of the lows:
- **P lows:** L1, L2, L5, L6, L7, L9, L10, L11 and L12 are present where Appendix A says.
- **S lows:**
  - L1, L2, L3, L5, L6, L7, L8 and L9 are present.
  - L4 is partially held: "the cut writer holds no references into `originals_` across the
    record call" and "checkpoints stay best-effort" appear only in Appendix A, not in §4 or in
    the S1 row.
- **D lows:**
  - L1, L3, L4, L5, L6, L8, L9 and L10 are present.
  - L11 is held as a decision.

## B. New defects

### B1 (high). §7.3 and §8.2: Anthropic's turn-start rule is not defined at the level the adapter renders, so a carried pending cut can drop the thinking of an open tool-use turn on the shipped `closed_only` profile

**Source.**
- The Anthropic adapter merges consecutive user-role blocks into one user message
  (anthropic_provider.cpp:346-351). It renders developer and system items as user text
  (anthropic_provider.cpp:428-433).
- `programs/turn.lua` and ultracode stop after 64 steps with `error(...)`, and return
  successfully on `restarting` while calls are outstanding. Either way, tool outputs are the
  last entries.

**Scenario.**
1. A turn stages an archive that cannot settle under `closed_only`.
2. The turn ends on the step budget with tool outputs at the tail. Decision 4 keeps the cut
   pending, and §8.2 says it waits for the next G.
3. The operator types "continue". The entry is a user message without results, so the
   entry-level reading of `user_message_without_results` calls the tail closed. On the wire,
   however, the prompt is merged into the user message that carries the trailing
   `tool_result` blocks.
4. G settles the cut under `drop_after_change`. That drops the thinking of the last assistant
   `tool_use` message, which Anthropic still treats as part of the tool-use turn ("Required:
   within a tool-use turn, pass thinking blocks back").
5. The shipped conservative profile thus makes exactly the mid-turn drop it exists to prevent.
   Per P-F8 the likely outcome is silent loss of thinking, not a 400. If instead the rule is
   read at the wire level, that turn never closes until the model writes a final answer, and
   the carried pending never settles at the turn start.

**The same gap in the boundary design.**
- Neither Anthropic boundary form can satisfy the profile's own `start` rule: `user_note` is
  merged after the tool results, and `system_message` is not a user message.
- So §7.2's "after it, the current turn is empty" is false by the design's own turn-state
  definition.

**Direction.**
- State that the turn state is evaluated on the adapter's rendering.
- Define the harness boundary entry explicitly as a turn start in every profile, so that
  rd7.3 tests exactly that equivalence.
- Add "operator prompt merged after trailing tool results" to the rd7.3 cases.
- Until it is probed, treat that tail as open on Anthropic. Alternatively, have the engine
  close such a turn by sending the prompt as a separate message after a harness-authored
  assistant acknowledgement. That is not allowed today, so the simpler form is the open
  reading.

### B2 (medium). §8.2 step 3: "a rebind advances the pending cut's expected head" breaks settlement, and the claim that a rebind "removes only reasoning, which the model cannot select" is false at the entry level

**Why it fails.**
- §5.1 expands a selected round to its reasoning, message, calls and outputs, and the staged
  frame records the expanded refs (§4.2 "expanded to entries").
- A `drop_foreign` rebind removes reasoning entries from inside the basis range.
- Settlement then fails in two ways:
  - The basis rebuilt from `{count, last}` is no longer a prefix of the entries, so the
    prefix check at context.cpp:1583-1592 gives `settlement-conflict`.
  - `apply` step 1 refuses `corrupt`, because listed reasoning refs are absent.
- The same happens with a `prefix` cut at step 3. Its effect on the pending cut is not stated
  at all.

**Scenario.**
1. The model archives r12-r20 at a G where the turn rule defers it.
2. The operator switches to another model family between turns.
3. At the next G, the rebind removes the foreign reasoning entries and advances `expected`.
4. The pending cut is then refused, or trips `corrupt`, although the design promised that
   it carries over.

**Direction.**
- Hold a pending proposal as units (creating-record refs) and re-expand it against the
  current entries at settlement.
- Have the rebind or prefix cut rewrite the pending proposal and basis in the same frame,
  and record that.
- Say what a prefix cut does to the pending cut.

### B3 (medium). §4.2 and §8.4: split settlement writes two context frames, but `record_boundary` batches exactly one, so a crash between them loses the remainder

**Source.**
- `record_boundary` takes one context packet and one program packet (context.cpp:94-149).
- At G there is no batch at all.

**Scenario.**
1. A split settles at J: the accepted cut names the stage, and then a new staged frame for
   the remainder should follow.
2. The process dies after the first record.
3. Replay clears the pending cut on the frame that names its stage, and finds no remainder.
4. The model was told "r5011, r5020 waiting", and §8.4's crash row promises restoration.

**Direction.**
- Carry the remainder inside the accepted frame (`remainder {proposal, basis}`), and have
  playback restore it as pending with that frame as its stage.
- Or write both events in one `append` batch, and let `record_boundary` take a list.

### B4 (medium). §8.3 and §8.4: a split remainder blocks every later proposal and the step for the rest of the turn

**Rules involved.**
- Only one pending cut is allowed at a time.
- §13.1 runs the step only when no cut is pending.
- Under `retain_current_turn`, the remainder of a split is the current-turn part, and it
  waits for the end of the turn.

**Scenario.**
1. On OpenAI, early in a long turn, the model archives a mixed selection.
2. The earlier-turn part settles and the current-turn part waits.
3. For the rest of the autonomous turn:
   - every `context_manage` archive, summary or restore is refused `pending-overlap`, even
     for units that could settle now;
   - the step never runs again.
4. The split meant to keep the nominal path live re-blocks it after its first use.

**Direction.** Let a new proposal merge with or replace a waiting remainder (one pending cut,
recomputed), or treat the deferred remainder as a queue that does not block new
settle-now proposals.

### B5 (medium). §6.3, §6.4 and §7.3: cuts the harness itself must make have no outcome when the turn rule forbids them

**The gap.**
- §6.4 says a request whose tools or instructions differ from the chapter's "is never sent
  silently".
- The only refusal path defined is for program requests (`binding-change-mid-turn`).
- A tools change from Lua activation at J, or a reopen under another account or directory,
  needs a prefix or rebind cut at the next G.

**Scenario.**
1. On Anthropic `closed_only`, the next G has an open tail, as in B1's merged-prompt case
   after a budget-ended turn that activated a Lua tool.
2. The required prefix cut would drop open-turn thinking, so the turn rule forbids it.
3. The request cannot be sent with the old tools (the engine has already swapped
   `lua_tools_`), and cannot be sent with the new tools without the cut.
4. Nothing is specified, and the session wedges: no request means no final answer, so the
   turn never closes.

**Direction.**
- Keep sending the chapter's tools and instructions until a permitted settle point, and
  activate the new set then.
- Or allow a prefix cut that drops all reasoning regardless of the turn: Preserved thinking
  lists "all of them" as valid, and rd7.3 should probe it.
- State the outcome for each harness cut kind.

### B6 (medium). §11.5: `move` recorded as remove plus insert creates a new original with a new ID, which changes the unit's creating record, class and handle, and can split a call from its reasoning

**Why.**
- §4.3 inserts only frame `originals`, with IDs `<cut revision>.<i>`.
- A moved entry therefore gets a new ID, a new handle and the creating record of the cut frame.
- That contradicts §5.2 ("the class belongs to the creating record … so editing an entry cannot
  change it").
- It also contradicts §3's round membership, which takes a round's reasoning, message and
  calls from one creating record.

**Scenario.**
1. The model moves a `function_call` entry.
2. The call becomes part of a new "round" created by the cut, while its reasoning stays in
   the old round.
3. On Anthropic the call is rendered as a `tool_use` without its thinking.
4. The unit's class follows whatever the cut-created class is, and the handle the model just
   used no longer exists.

**Direction.**
- Give `insert` an "existing ref" form that keeps the ID, its class and its original.
- Or restrict `move` to whole units, re-validated at settlement as a round.
- Or drop `move` from the model contract.
- Also state the class of items inserted by `context_edit insert` (see the low list).

### B7 (medium). §23 decision 2 contradicts §5.1, §5.3 and §11.3

**The contradiction.**
- Decision 2 lets the model archive an older byte-identical copy of an instruction: the
  ultracode brief, which §5.2 classes as `instruction`.
- But §5.1 says instruction units are "never selectable by the model".
- §5.3 says the model "may not archive … instruction units".
- §11.1 says expansion drops them as refused.
- The `context_manage` description says "Operator instructions are never archived".

**Scenario.** The model selects the older ultracode brief. The engine refuses it as
protected, so decision 2 cannot happen.

**Direction.** Either add the exception to §5.1 and §5.3 (an instruction unit whose bytes
equal a later live instruction unit becomes selectable), with a test and description text, or
withdraw decision 2 and rely on c3f.

### B8 (medium). §4.5: safe mode plays back the whole history after every unclean stop, and its trigger depends on 7c8, which is in no delivery unit

**Against the doctrine.**
- AGENTS.md: "Archived history must not force resident payloads or full replay merely because
  it exists".
- 7c8 calls a non-empty suffix safe mode, and does not require replay from fact 0.

**Scenario.**
1. A long session crashes with a valid v2 snapshot and a short suffix.
2. Reopen replays every context frame since fact 0 through the catalogue. That is not
   bounded by the launcher's 64 MiB automatic-replay guard (main.cpp:310-326), which
   applies only to RetainedState.
3. The correct and cheap path, snapshot plus suffix, is what the source does today.

**The 7c8 dependency.**
- The clean and unclean distinction, and D-M6's rationale ("a clean stop already
  snapshots"), rely on 7c8.
- 7c8 is open, and its bead records that `/quit` writes no snapshot today.

**Direction.**
- After an unclean stop with an accepted snapshot, play back the suffix (still recorded and
  shown as safe mode).
- Use whole-history playback only for a rejected snapshot.
- Make 7c8 a dependency of S1, or define how a clean stop is recognised without it.

### B9 (medium). §4.5: in the read-only state, a clean stop's snapshot bypasses the repair frame

**Scenario.**
1. A bad frame opens the session read-only at the last good head.
2. The operator quits. Under 7c8 a clean stop writes a snapshot.
3. The snapshot captures the last good state with a boundary past the bad frame and every
   frame after it.
4. The next reopen is a normal resume, and the session silently continues without any
   `repair` frame or operator approval. New frames chain on the last good head.
5. A later whole-history playback (B8) reaches the bad frame again. The repair rule ("skip
   the bad frame and every later context frame chained on it") no longer describes the
   journal, because the later frames chain on the last good head, not on the bad frame.

**Direction.** In the read-only state, suppress snapshot and maintenance checkpoints, so the
`repair` frame is the only way past a bad frame. Add this case to the §20 safe-mode row.

### B10 (medium). §13.1 and §16.3: side requests carry "no `cache_control`", which on Anthropic also stops cache reads, so the cached cost in §24 for `appended_removal` cannot be reached

**Why.**
- Anthropic looks up cache entries from breakpoints, "looking for prior writes", and caches
  only when the request includes `cache_control` (provider F4 and Unverified).
- A step with no breakpoint reads nothing, so it pays full price under `appended_removal`
  too.
- P-F2's direction was "no new cache write beyond the already-written prefix", not "no
  breakpoints".

**Scenario.**
1. After bvu, the main context is cached.
2. The step goes out under `appended_removal` with no `cache_control`.
3. It pays about 115k full-price tokens, not the roughly 5.8k claimed in §24.

**Direction.** On breakpoint profiles, the side request repeats the main request's existing
breakpoints, which read the already-written entries, and places none after them (the brief).
Fix the wording in §13.1, §16.3 and §24.

### B11 (medium). §21: S3 changes the timing and results of existing tools before V1 changes their contracts

**The gap.**
- S3 stages every non-append change and settles at G.
- The old tools remain until V1:
  - `context_manage`'s description says the change takes effect after a successful workflow
    (tools.cpp:1394-1410);
  - `context_restore` and Lua `blackbird.restore` call `restore` and then return
    `context_.view()` as if it had applied (coding.cpp:5619-5622, 1654-1657);
  - `context_edit` and Lua `blackbird.edit` return a publication result.

**Scenario.** Between S3 and V1, the model restores a unit and receives an unchanged view that
it reads as applied. Its archive takes effect before the next request, while the description
says "after the workflow". This is the "offer behaviour before it is honoured" fault that
S-M12 named, in reverse.

**Direction.** Move into S3 the description and result changes of every existing tool whose
semantics S3 changes. At minimum, the result says `staged` and when it takes effect.

### Lows

**Frames, settlement and playback.**
- **§4.2 and §5.4, refusal fields.**
  - §5.4 says the settle frame "lists it under refusals", but the frame schema has no
    refusals field.
  - §4.2 lists `pinned` and `turn-open` as whole-frame refusals that clear pending on replay
    (a refusal names the stage). §8.4, however, says a cut the turn rule does not permit
    "stays pending".
  - Direction: add `refused [{ref, reason}]`, and drop `turn-open` as a clearing outcome.
- **§7.2 and §4.3, boundary anchor.** The boundary anchors on "the last entry". If the archive
  includes the last round, that anchor is removed by the same frame and `apply` refuses. If
  the archive note's anchor is the same entry, the two inserts must chain. Specify "the last
  surviving entry, chained after any insert on it".
- **§4.5, skipped entries.** "The skipped frames' originals … can be restored" has no path.
  Restore and `scope: archived` read cut frames only, and skipped entries are in none.
- **§4.3 step 6, byte check.** Checking `encoded_bytes` ties playback of old frames to the
  exact encoder. Name the canonical encoding, or keep count plus CRC32C as the corrupt check
  and record bytes only.
- **§4.2 and §11.3, the `change` field.** `change` is a single enum, but one call may archive,
  restore and summarise, and overlapping archive and restore selections are unspecified. Make
  `change` a set, or define the combination.

**Selections, contracts and model-facing text.**
- **§3 and §11.1, handle uniqueness.** Handles are unique only within one issuer namespace.
  Each successor segment has its own namespace, and its counter starts at 0
  (retained_environment.cpp:571-573, 633-634). Uniqueness therefore holds only while the
  launcher refuses linked journals (main.cpp:303-307). State this, and give a segment-qualified
  form for later.
- **§10.1, request-local delivery.**
  - The rows are never delivered when the next request uses `state_context = "omit"` or
    overrides `input`, yet the stub says they were.
  - The engine holds the page arguments in memory, so they are lost on a crash.
  - Deliver on the next request that carries operational state, and word the stub
    accordingly.
- **§11.5, inserted items.** The class of a `context_edit insert` item is unspecified. A
  user-role inserted item must not fall into the role-based `instruction` fallback.
- **§6.2, DeepSeek.** `refused_until_probed` names only `archive`. `context_edit` remove and
  replace, `summarize` and C4 also remove or replace assistant messages.
- **§9, budget arguments.**
  - `output_max_bytes` and `output_head_bytes` on every shaped tool add unmeasured
    schema bytes to the fixed prefix, which is D-H4's own argument.
  - A Lua-defined tool may declare those names with another meaning.
  - Workflow-continuation results are appended by `drain_workflows` (coding.cpp:4282), not
    through `operation()`, so "generic shaping happens in `operation()`" does not cover them.
- **§11.3, schema reference.** `"$ref": "#/selection"` does not resolve inside a function's
  `parameters` schema. Inline the string-or-object schema, or use `$defs`, and check `anyOf`
  support per provider.
- **§7.2, example handle.** The boundary example calls the archive note `r4870`, which is a
  `read_file` unit in §5.5 and §10.3. A cut-created note would carry a handle like `r5100.0`.

**Provider and rendering details.**
- **§14 and §15.2, `output_window_exhausted` with calls.** Anthropic's `finish()` also refuses
  a response whose stop reason is not `tool_use` but which carries calls (`corrupt`,
  anthropic_provider.cpp:207-208). Say that complete calls in such a response are answered
  normally, and how a truncated `tool_use` is handled.
- **§6.3, binding options.** `reasoning` is listed as binding-relevant, but effort is not in
  the binding, so it is unclear whether an effort change mid-turn fails with
  `binding-change-mid-turn`.
- **§19, RRC check.** The RRC check is said to read "the class map's creating-record origin",
  but the class map holds one byte (§5.2, D-L2). The token check needs the creating record's
  label, read by identity lookup.
- **§7.2, §6.4 and §13.1, real system messages.**
  - The Anthropic adapter cannot emit a real mid-conversation `role: "system"` message today
    (system items render as user text).
  - The `system_message` boundary form, `appended_system_message` and `appended_removal` all
    need that adapter change and the `inline-tools-2026-09-15` header plumbing. Neither is in
    any unit.
  - The class and protection of a retained tool-change system message under archival are
    unspecified: archiving it would remove a tool the engine thinks is active.
- **§14, `retain_current_turn`.** On OpenAI's shipped `retain_current_turn`, a recovery inside
  one long turn can select nothing. The terminal outcome is stated only for `closed_only`.

**Sizing.**
- **§12.1, token estimate.** 448 KiB native at 3.4 wire bytes per token is 142k-150k tokens
  across the stated 5-10% native-to-wire range. That is not under the 147,640-token request
  at the 10% end. State it as "about 142k-150k", or pick the level from the lower bound.

### Text still framing the Anthropic step restriction as open

- **Contents** item 24 and the **§24** heading, "The operator question".
- **§24, lines 1577-1592**:
  - the question "How should the archival step restrict Anthropic's tools?";
  - the recommendation, which adds a bvu condition the owner did not set;
  - "`palette_only` stays the shipped default until the operator answers".
- **§13.1:**
  - line 1064-1065, "the shipped default everywhere else, Anthropic included, until the
    operator answers §24";
  - line 1069, "`appended_removal` (Anthropic, pending §24 and rd7.3)".
- **§7.4, line 665**: "the `tool_removal` restriction for the step (§24)". Correct as a
  probe, but the rd7.3 case should now name the two switch criteria: a removed tool cannot be
  called, and thinking survives.
- **§21 A1 row, line 1493**: "Anthropic `appended_removal` after §24 and rd7.3".
- **§23 decision 8, line 1572**: "`palette_only` for the Anthropic step pending §24".
- **Appendix A P-F2, line 1603**: "laid out as the operator question §24".

Direction: retitle §24 as a decision. Record that `palette_only` ships and that each model
switches to `appended_removal` when its rd7.3 run shows both criteria, on the ground that
Anthropic's enforcement is native restriction. Drop the bvu condition from the switch: cache
economics is a separate bvu measurement and is limited by B10. Replace "pending §24" with
"pending rd7.3 per model" in the places above.
