# Tribunal: state, persistence, replay, activation and concurrency

Design under review: `docs/CONTEXT_LIFECYCLE.md` (draft for `arconaut-rd7.1`).
Fault class: state, persistence, replay, activation and concurrency correctness against the source.
All source references are `file:line` at `870ec8c` (read with `git show 870ec8c:<file>`).
`src/context.cpp`, `include/blackbird/context.hpp`, `src/retained_state.cpp` and `src/main.cpp`
have no diff against `870ec8c` in the worktree. Read-only review. Nothing was built or run.

Findings are ordered by severity. "Unconfirmed" marks a point I could not settle from the source alone.

---

## High

### H1. A snapshot that ContextStore rejects in compact mode silently resets the context, and v2 widens that path

- **Section:** §4.4 (snapshot v2), §4.5 (safe mode).
- **Source evidence:**
  - context.cpp:347-350: the ContextStore reader refuses any version other than `"1"`. On any `Error` it clears `head_`, `entries_` and `originals_` (context.cpp:367-376) and leaves `boundary = 0`.
  - context.cpp:405-488: the reader then replays `log.root().tail_facts()`.
  - retained_state.cpp:1207-1220 and retained_state.hpp:220-222: in compact mode, `tail_facts()` holds only the suffix after the saved boundary. Compact mode is chosen by RetainedState from the saved state's `semantic_coverage` and `physical_records` (retained_state.cpp:340-393), and RetainedState never learns whether ContextStore accepted the context.
  - saved_state.cpp:143-153: `load_slot` checks the context shape on its own, including `version == "1"`. The design cites only context.cpp:349.
  - coding.cpp:2641-2642: `if (context_.head().empty()) context_.append({}, "initial");`.
- **Failure scenario:**
  1. A clean stop writes a snapshot. The boundary is the cursor, so the suffix has no context frames.
  2. On reopen, RetainedState accepts the slot and enters compact mode.
  3. ContextStore rejects the context object. The cause can be any v2-only field it validates and `load_slot` does not (`chapter`, `chapters`, `pending`), a `bad_variant_access` on one of them, or a version mismatch between the two readers.
  4. ContextStore clears its state and replays the empty suffix, so `head_` stays `""`.
  5. The engine appends `initial` with `base = observed = ""`.
  6. The session opens with an empty context and no operator notice. Only a raw control branch (1821) records the cause. The next two checkpoints overwrite both slots with the empty context.
  7. The journal is now also unreplayable from the start. A full replay reaches the `initial` frame whose `observed` is `""`, which is not the running head, and fails with `corrupt` (context.cpp:422-424).

  If the suffix does hold context frames, the first one fails `observed` and the session refuses to open. That failure is loud but does not recover.
- **Direction:**
  - Version and validate the v2 shape in both `load_slot` and ContextStore, from one shared definition.
  - When ContextStore rejects a snapshot that RetainedState accepted, it must never replay a suffix from an empty head. It should either play back the whole history through `history_snapshot()` (the real safe-mode path) or refuse to open with a recovery record.
  - Add a direct check that corrupts each v2 field in turn and expects safe mode, not a reset.

### H2. Delta playback has no check that it reproduces the writer's state, and its insert semantics are underspecified

- **Section:** §4.2, §4.3; §20 ("plays back with no new replay logic").
- **Source evidence:**
  - context.cpp:457-461: `edit` and `managed` frames carry the resulting `entries`, so replay could not diverge from the writer.
  - context.cpp:447-456: the only delta today is append, which is concatenation and cannot be ambiguous.
  - context.cpp:232-249: `valid_entries` checks only that each ID exists, has an original and is unique. It checks neither order nor protocol completeness.
- **Failure scenario.** The writer computes the next entries with its own algorithm: the managed algorithm (context.cpp:1566-1628), the index placement in §5.4 or the restore placement. It then records a delta. Replay applies the delta. Any difference between the two, in any of the cases below, gives a live state that differs from the reopened state, and both pass `valid_entries`. The provider then sees one input before reopen and another after, which breaks the cache prefix and, on prefix-checked Anthropic models, the thinking binding. Nothing detects it.
  - `after: ""` "for the head". In this codebase "head" means the newest revision (`head_`). The index must be able to go at position 0 when the earliest archived round is the first entry, which happens in seeded and operator-edited contexts. The frame needs an explicit start anchor.
  - Several inserts with the same anchor. Option B writes one summary per window into one frame (§12), and a restore can insert a whole round after one anchor. Do the inserts keep listed order, and may an anchor name an entry inserted earlier in the same frame?
  - An anchor that names an ID removed by the same frame. With removal applied first, the anchor no longer exists.
  - Overlap between `archived`, `swept` and `reasoning`. A second removal of the same ID fails "every listed ID exists".
  - `context_edit` `move` (§9.3). The frame has no move, so a move must be a remove plus an insert of the same ID, carrying the item again.
- **Direction:**
  - Use one `apply(delta, entries)` function that the writer calls to produce `next` and that replay calls, so the writer never computes `next` any other way.
  - Fix the semantics in that function: removals first; inserts applied in listed order; an anchor may name an earlier insert; an explicit start sentinel; the three removal lists disjoint.
  - Have replay verify the frame's own measured `sizes.after` (encoded items bytes) and the entry count against the result, and treat a mismatch as `corrupt`. This checks the result without re-running selection.
  - Correct §20: the delta form needs new playback code. Today the `format` branch refuses any non-append format (context.cpp:447-450), and the originals branch is limited to `append` and `managed` (context.cpp:432).

### H3. `context_edit`, Lua `blackbird.edit` and `blackbird.restore` publish immediately, so a cut crosses the open round and bypasses G, J and idle

- **Section:** §4.1 (an accepted edit is a cut), §6.3 ("an open round never crosses a cut"), §7.1, §5.2 (pinned instructions "only the operator removes them").
- **Source evidence:**
  - context.cpp:1018-1076: `edit` publishes whenever `base == head_`. It never stages, even with `workflow_` set, and it has no pin or role check.
  - context.cpp:1077-1108: `restore` publishes through `edit`.
  - Callers: the `context_edit` tool (coding.cpp:5591) and the `context_restore` tool (coding.cpp:5595), which run inside `operation()` while the model's own `function_call` is unanswered; Lua `blackbird.edit` (coding.cpp:1672) and `blackbird.restore` (coding.cpp:1678).
- **Failure scenario:**
  1. On an Anthropic `drop_after_change` / `user_boundary_only` profile, mid-turn, the model calls `context_edit` with a delta that replaces an earlier tool output.
  2. The edit publishes at once, as a cut, while the `context_edit` call itself is open. That breaks "an open round never crosses a cut", and §7.1 G is never reached.
  3. The profile's reasoning rule drops every thinking block after `p`. That includes the thinking of the current tool-use turn, whose `tool_use` is the open call.
  4. The next request is invalid. Separately, the model or a model-authored Lua program can replace or remove an `operator` instruction through `replace` or `remove`, which defeats the pin rule in §5.2.
- **Direction:**
  - Route every non-append change from the model and from Lua (edit, restore, the new delta ops) through the staged pending-cut path, so it settles only at G, J or idle under `open_turn`.
  - Refuse model and Lua `replace` or `remove` on `instruction` entries and on pinned rounds. Keep that only for operator commands at idle.
  - State this in §4.1 and §9.3.

---

## Medium

### M1. A pending cut cannot settle after resume: the snapshot omits its basis, and replay drops `candidate`

- **Section:** §4.4 (`pending {proposal, stage, expected}`), §7.2, §7.3 (crash and failed-turn rows).
- **Source evidence:**
  - `Pending{proposal, snapshot, expected, stage}` (context.hpp:142-147). `snapshot` is the entry list at staging (context.cpp:1440).
  - A staged settlement needs it: `publish_managed` transforms `basis` and then requires `basis` to be a prefix of `entries_` before carrying the appended suffix forward (context.cpp:1583-1592). The design keeps this mechanism (§7.2).
  - Replay decodes context frames with `candidate` projected out (context.cpp:414-420), and today's staged record stores the proposal as `candidate` (context.cpp:1230).
- **Failure scenario:** with the design as written, both resume paths fail.
  - A turn fails with a pending cut, which the design keeps. A clean stop then writes a v2 snapshot. Normal resume restores `pending` without `snapshot`. At the next G, the settlement has no basis, so the implementation must invent one. Using the current entries applies the archive to entries the proposal never saw, and the carried-forward suffix is lost or duplicated.
  - Safe-mode playback of a staged cut frame laid out like today's (`candidate: proposal`) sees no proposal, because replay drops `candidate`, so it cannot restore pending at all.
- **Direction:**
  - Record the basis by reference in the staged frame and in the snapshot: the basis entry count, or the last basis ID plus `observed`. Replay rebuilds it from the entries at the staged frame.
  - Name the proposal field something other than `candidate`, or exempt it from the projection.
  - Have replay advance `expected` across append frames with exactly the rule in `append_impl` (context.cpp:988-989), and test it against live state.

### M2. Settlement outcomes do not name the staged frame, so replay cannot tell which rejection clears pending

- **Section:** §4.2 (rejected cut "no change fields"), §7.2 ("a later cut frame that settles or cancels it clears it").
- **Source evidence:**
  - `reject_managed` writes `{op, revision, observed, accepted:false, candidate, outcome}`, with no stage (context.cpp:1226-1231).
  - That one function writes `settlement-conflict` (context.cpp:1499, 1586), `invalid-tool-protocol` (context.cpp:1630), `workflow-cancelled` (context.cpp:1492) and also `pending-overlap` for an unrelated second proposal (context.cpp:1370).
- **Failure scenario:**
  1. P is staged.
  2. The model's second `context_manage` Q is refused `pending-overlap`.
  3. Later P is refused `settlement-conflict` at G.
  4. Crash. If replay clears pending on any rejection, step 2 drops P too early (a crash between steps 2 and 3 loses P). If replay clears pending only on frames that name the stage, step 3's rejection never matches and P is resurrected after reopen.
- **Direction:**
  - Every settlement outcome frame (settled, conflict, protocol refusal, cancelled) names `stage: <staged revision>`.
  - Replay clears pending only on a frame whose stage matches.
  - A refusal of a different proposal names no stage.

### M3. Fresh entries in a cut need IDs of the form `<cut revision>.<ordinal in originals>`; otherwise checkpoints fail and the compact tail caps block every append

- **Section:** §4.2 (`originals [entries] … (index, summaries)`, `insert … edit inserts`), §4.6, §9.3 (delta `insert`).
- **Source evidence:**
  - `valid_entries` requires each live ID to resolve to an original (context.cpp:244).
  - After a checkpoint, `original_entry` resolves an ID only by parsing it as `<record identity hex>.<ordinal>` and reading `originals[ordinal]` from that record, which must be accepted and must carry a matching `id` (context.cpp:513-581).
  - `checkpoint` throws `corrupt` if any live entry fails `original_entry` (context.cpp:595-599). The failure is only recorded (context.cpp:704).
  - Compact mode rejects any append beyond 1024 records or 32 MiB since the boundary (retained_state.cpp:1728-1736, 1779-1797, per the catalogue §6.5).
- **Failure scenario:**
  1. A delta `context_edit` `insert` of a new item (not an existing ID) is written with the item in `insert` but not in `originals`. The design's `originals` list names only the index and summaries. Alternatively, a frame carries an index entry and a summary whose IDs do not match their positions in `originals`.
  2. Writer-side `valid_entries` either refuses the insert, which breaks the feature, or, if the writer skips the check, the next checkpoint fails `corrupt`, and so does every one after it.
  3. The tail grows to the cap, and then every append, the provider response included, fails with `capacity`. The session is wedged.
- **Direction:**
  - Every newly created entry of a cut is listed in `originals`, and its ID is `<cut revision>.<index in originals>`.
  - Writer and replay assert this. Add a direct check: checkpoint and then `original_entry` for every fresh-entry kind.

### M4. Entry `origin`: legacy entries have none, and an origin stored in the entry is editable

- **Section:** §5.2 (origin "set in `append_impl` … and in replay from the recorded packet `origin`").
- **Source evidence:**
  - Version-1 snapshot entries are `{id, item}` (context.cpp:645-648, 354).
  - `managed` frames record full `entries` without origin (context.cpp:1653), and so do `edit` frames (context.cpp:1051).
  - Edit candidates are arbitrary entry objects: `valid_entries` checks only `id` and `item` (context.cpp:240-246).
  - Lua `blackbird.edit` passes a full candidate (coding.cpp:1672). `blackbird.context()` returns `view()` entries (coding.cpp:1669).
- **Failure scenarios:**
  - **Legacy entries.** A session reopened from a version-1 snapshot, or replayed through `managed` frames, has entries with no `origin`. The class table has no row for them. If the default is selectable, old operator instructions become archivable. If the default is role-based, every continuation result and state item stays pinned forever.
  - **Spoofing.** A model-authored Lua program reads `blackbird.context()`, sets `origin = "provider.branch:…"` on an `operator` entry, and calls `blackbird.edit`. If origin travels with the entry, the instruction becomes an archivable round. In the other direction, setting `origin = "operator"` pins arbitrary rounds.
- **Direction:**
  - Do not store `origin` in the editable entry. Derive the class from the record that created the ID: the append packet's `origin`, found through the ID's revision prefix (the same lookup `original_entry` already does), or the cut frame's kind for fresh entries.
  - Ignore or refuse `origin` in candidates and deltas.
  - Document the legacy fallback: an entry whose creating record has no origin is classed by role, as today.

### M5. Class and round definitions fail for seeded sessions, Lua-synthetic calls and repaired rounds

- **Section:** §3 ("Round … a round ID is the append revision"), §5.2 table.
- **Source evidence:**
  - `seed_successor` appends the whole seed, rounds and instructions together, in one append labelled `session.successor` (context.cpp:913-924). The backstop and `--seed-session` both use it (backstop.cpp:377; main.cpp:424).
  - `whole_rounds` ties every entry after a reasoning item in the same append (context.cpp:1157-1169).
  - `blackbird.append` labels everything `lua.synthetic` (coding.cpp:1683), including `function_call` items and assistant messages.
  - `context_repair` appends `workflow.protocol-repair` outputs for an earlier turn's open calls (coding.cpp:5209-5243). This runs after the new turn's `operator` entry has been appended (coding.cpp:6051-6055); `placeholders` at turn start exists for exactly this case (coding.cpp:6011).
  - `whole_groups` requires every entry from a call to its output to be selected together (context.cpp:1183-1190).
- **Failure scenarios:**
  - **Seeded sessions.** The whole seed is one "round" and contains user instructions. Any seeded round fails `partial-round` or `mandatory-entry`, so the seeded history can never be archived. C4 option A cannot make progress on a backstop successor, and option B's "oldest complete, unpinned rounds" has no valid window.
  - **Lua-synthetic calls.** A Lua-synthetic `function_call` or assistant message matches no row of the table.
  - **Repaired rounds.** A turn fails with a call open and not interrupted, so no `stop.linkage` is written. The next turn appends the operator prompt, then `context_repair` appends the output. The context is now `[call] [operator user] [repair output]`. That group can never be archived without archiving the instruction.
- **Direction:**
  - Define round membership from the creating `provider.branch` append plus the outputs answering its call IDs, independent of contiguity. Use the seed's `source_entries` lineage to recover per-round grouping for seeded entries. Add rows for Lua-synthetic calls and assistant messages.
  - For the repaired case, either allow the round to be archived while the instruction stays (the output moves with its call), or record that such a round is not archivable. Do not leave it to fail silently on every proposal.

### M6. Pins are not rechecked at settle and are not written atomically with what they protect

- **Section:** §5.3.
- **Source evidence:**
  - `manage` validates against the entries at staging (context.cpp:1362-1444).
  - Settlement re-runs only the `expected` and protocol checks (context.cpp:1495-1502, 1629).
  - `record_boundary` batches exactly one program packet with the context packet (context.cpp:94-149), and J already uses that slot for `workflow-config-effective-v1` (coding.cpp:6074-6094).
- **Failure scenarios:**
  - **Pin after staging.** The model stages `archive [R]`, then in the same turn calls `context_manage {mode: pin, rounds: [R]}` (or uses `retain: durable` on a related call). The pin is recorded immediately. At G or J the staged archive settles and removes the pinned round. The model was told R is pinned.
  - **Durable pin and crash.** `retain: durable` writes the output append (context channel) and the `context-pins-v1` record (program channel) as two submissions. A crash between them leaves the output with no pin. The tool output, which the model has already seen as "pinned", is wrong after reopen, and the next step may archive the round.
  - **J with both records.** At J, a cut that must also rewrite the pins record (for example after an operator removal or a restore) cannot go in the same batch as the boundary program.
- **Direction:**
  - Re-validate pins (and instruction class) at settle time against the current pins record. Drop or refuse pinned IDs with a reason in the settle frame.
  - Write the durable pin in the same batch as the output append (extend `record_boundary`, or use a multi-event `append`).
  - Let `record_boundary` take a list of program packets.

### M7. A side request that reuses assembly at G appends to the main context on Anthropic and clobbers engine slots

- **Section:** §11.1 ("reuses assembly"; "does not move the head"; "no write to the engine's last-usage slot").
- **Source evidence:**
  - Assembly with `retain_request_state()` appends the operational state to the main context and moves `origin` (coding.cpp:4459-4466). The Anthropic adapter retains request state (include/blackbird/anthropic_provider.hpp:15).
  - Assembly also does `last_request_bytes_ = …` and `usage_ = Value{}` (coding.cpp:4503-4504), clears `previewed_` and sends preview text to `display` unconditionally (coding.cpp:4583, 4647-4653).
- **Failure scenario:**
  1. On Anthropic, the C6a step at G builds its request through assembly.
  2. Assembly appends a `provider.request-state` entry to the main context. The head moves and the pending basis shifts. The step's response is then discarded, as designed.
  3. The main request appends a second state entry. The main context now holds an orphaned state item from a request whose response never entered context, and the step was not a pure side request.
  4. The TUI usage meter and `/stats` show an empty usage and the step's byte count in place of the main request's.
- **Direction:**
  - Split assembly into a pure builder, which produces input, tools and operational state with no context or engine writes, and a main-request committer that does the `provider.request-state` append and the slot updates. The side request uses only the builder.
  - Unconfirmed: whether a provider assembly `RawProviderPhase`/`RawRegion` may be opened nested inside the main request's assembly phase at stage 12 (coding.cpp:4286-4299). Check the raw region nesting rules before choosing nested calls.

### M8. C4 runs its side request inside the request group, and the failed attempt did append on Anthropic

- **Section:** §12 ("Retry … The failed attempt never appended anything"), §19 U9 (reuses U8's side path).
- **Source evidence:**
  - The overflow is seen inside `group.run` after dispatch (coding.cpp:4538-4784). At that point `request_group_active_` and `provider_.request_group_owned_` are set (coding.cpp:4475-4477).
  - `request()` throws `busy` if either is set (coding.cpp:4290). Anthropic `begin_request_group` throws `busy` if `group_` is set (anthropic_provider.cpp:574-575); OpenAI throws `busy` if `request_binding_` is set (coding.cpp:344-345).
  - The state item was appended to context before dispatch (coding.cpp:4464).
- **Failure scenario:**
  - From inside the attempt loop, C4's option A or B side request either throws `busy`, which ends the recovery, or must bypass the guards and run a second group on a provider that holds a live group binding.
  - The retry after the recovery cut, on Anthropic, also carries the failed attempt's `provider.request-state` entry, and then appends a new one. "Not in the retried input" is false.
- **Direction:**
  - Recovery unwinds the request group completely (destroy the `RequestGroup`, `end_request_group`), returns to G, settles the recovery cut, and re-enters assembly.
  - Specify that the failed attempt's state entry is swept by the recovery cut (it lies in the rewritten region only if `p` is before it; say what happens otherwise).

### M9. The chapter binding is per request, not per session, and G does not know it; the order of work at G is unspecified

- **Section:** §6.1 (rebind sources), §7.1 G, §17 ("Model switching … takes effect at the next request, which is a turn start").
- **Source evidence:**
  - The model comes from `program_config_.model` or `model_` (coding.cpp:4304-4306). `request(options)` then overwrites any request key, `model` and `reasoning` included (coding.cpp:4386-4396).
  - Options are merged at stage 13, after G (G sits between coding.cpp:4299 and 4300).
- **Failure scenarios:**
  - **Per-request model.** A Lua program calls `blackbird.request{model = "<other model>"}` mid-turn. G compares the session binding, sees no change and writes no rebind. The request goes out under a binding the chapter does not record. Cross-family reasoning still throws at export (src/json.cpp:454-458; anthropic_provider.cpp:412-419, per the design's §6.1), and same-family switches proceed with a stale chapter binding in `provider.request`.
  - **Order at G.** G may need to do four things: a start frame, a rebind cut, settling the model's pending cut and the C6a step. The order is not given. If the rebind cut (a non-append publication) lands before the pending cut settles, the model's archive is refused `settlement-conflict` (context.cpp:1498-1500), and its IDs may name reasoning entries the rebind removed.
- **Direction:**
  - Resolve the effective binding (after options) before G, and make it an input of G.
  - Either refuse per-request binding changes mid-turn on `user_boundary_only` profiles, or treat them as rebind cuts subject to `open_turn`.
  - Fix the order at G: start, then settle the pending cut, then rebind, then the step. Have a rebind advance a pending cut's `expected`, since it only drops reasoning the proposal cannot select.

### M10. The cancellation reserve does not cover a pending cut carried over a failed turn, or the new record shapes

- **Section:** §7.3 (failed turn: stays pending; interrupt: `cancelled` frame), §19 U6 ("cancellation budget sizing").
- **Source evidence:**
  - At turn start the settlement reserve is sized with an empty proposal: `cancellation_budget(view entries, empty, origin)` (coding.cpp:6017-6018).
  - The per-operation refresh uses `pending_proposal()` (coding.cpp:3243-3245, 3164-3170). It runs only when `protected_settlement()` is set and an operation is admitted.
  - `cancellation_budget` prices a `stop.linkage` append and a `managed` settlement-conflict rejection (context.cpp:1313-1344), not a `cut` frame with `chapter`, `cause`, `rules` and `stage`.
  - On the stop path, `finish_workflow(false)` writes nothing if the journal is not live (context.cpp:1486-1490), and that path runs under `no_new` (coding.cpp:5918-5920, 5993).
- **Failure scenario:**
  1. Turn 1 fails with pending P, which the design keeps.
  2. Turn 2 starts with a reserve that ignores P. The operator interrupts before any operation refreshes the reserve.
  3. Writing the `cancelled` cut frame exceeds the reserve or finds the journal not live. The frame is not written, while the in-memory pending is cleared.
  4. On reopen, replay sees the staged frame with no cancellation and restores P (§7.3, crash row). P settles at the next G, against the operator's interrupt.
- **Direction:**
  - Size the turn-start reserve with the carried pending proposal.
  - Price the exact `cut` cancelled and conflict frames in `cancellation_budget`.
  - When the cancel frame cannot be written, record that in the snapshot or the recovery record, so reopen shows the pending cut to the operator instead of settling it silently.

### M11. Safe mode cannot handle a frame that does not apply, because safe mode is the same playback

- **Section:** §4.3 ("A frame that does not apply is `corrupt` and sends the session to safe mode"), §4.5.
- **Source evidence:** the replay loop rethrows on any frame error, so ContextStore construction throws (context.cpp:468-487).
- **Failure scenario:** a cut frame that fails playback (H2's divergence or M3's unresolvable ID) sends the session to safe mode. Safe mode replays the same journal (§4.5) and fails on the same frame. Two outcomes are possible, and neither is defined:
  - The session cannot be opened at all.
  - Safe mode stops at the last good frame and then writes new frames. Their `observed` is the last good head, not the bad frame's revision, so every later replay fails `observed` (context.cpp:422-424).
- **Direction:**
  - Define safe mode's behaviour on an unplayable frame. A sound choice is a read-only state at the last good frame, followed by an explicit operator-approved "repair" frame that names the skipped revision and that replay recognises.
  - Test it with a corrupted cut frame.

### M12. The delivery order splits invariants and offers behaviour before it is honoured

- **Section:** §19.
- **Source evidence:**
  - Design §9.1 has `context_view` rows with `class`, `pinned` and `archived_in`, and `scope: archived` reads the chapter table. §9.3 makes delta `context_edit` a cut. §5.3 has `retain` semantics. §10.2 says an archive "starts a new chapter before your next request".
  - Today staged changes settle only at the workflow boundary (context.cpp:1434-1443; coding.cpp:4450-4452).
- **Failure scenarios:**
  - **U4 before U5.** U4 must produce cut frames, the chapter table, classes and pins, which are all U5 work, or ship an interim full-entries `edit` that U5 replaces.
  - **`retain` offered early.** U3 ships the `retain` argument (stripped, no effect) before U5 implements the ephemeral sweep and durable pins. The model is offered a choice the harness does not honour.
  - **U7 promises U6 behaviour.** U7 depends on U4 and U5 only, but its `CONTEXT_REVIEW` text promises G settlement, which is U6. Between U7 and U6 the model is told an archive applies before its next request when it actually waits for the workflow boundary.
  - **Pending split across units.** Snapshot v2 with `pending` is U5 and durable pending is U6, so the snapshot schema changes twice, or U5 ships a `pending` field nothing fills.
  - **Missing work.** The `load_slot` change (H1), the replay `format` and originals branches (H2), and the rrc origin check (§17) are in no unit.
- **Direction:**
  - Move `retain` into U5.
  - Make U4's chapter-dependent parts depend on U5, or split U4 into U4a (bounded views, text inspect) and U4b (archived scope, delta edit) after U5.
  - Make U7 depend on U6.
  - Put snapshot v2, durable pending and the `load_slot` change in one unit.

### M13. C6b: the worker cannot be audited as specified, and staging order inverts the precedence rule

- **Section:** §11.2.
- **Source evidence:**
  - Every provider attempt goes through `operation()` admission and writes `provider.request` and `provider.stream` records through `log_` (coding.cpp:4586-4630). The design says the worker "never touches … AuditLog".
  - `manage` refuses any proposal while one is pending (context.cpp:1369-1370).
  - `Value` copies share `std::shared_ptr<Storage>`. `write()` decides copy-on-write with `data_.use_count() != 1` (include/blackbird/value.hpp:105-109).
- **Failure scenarios:**
  - **Audit.** The worker's provider call cannot be admitted or recorded. Either it is unaudited, against the audit doctrine, or the engine must admit it before spawning and record its bytes at poll time. The design describes neither. A crash during the worker loses all trace of a billed request.
  - **Precedence.** If the engine stages the worker's proposal at its next poll, the model's own `context_manage` in the same round is then refused `pending-overlap`. The harness's proposal wins, which contradicts §7.2 ("the model's own pending proposal takes precedence").
  - **Copy-on-write race (unconfirmed).** `use_count()` is a relaxed load. An engine-thread `write()` that observes 1 after the worker's last copy is released is not ordered after the worker's reads, so TSan may report races on shared storage.
- **Direction:**
  - Admit the worker's attempt on the engine thread before spawning. Hand back raw response bytes through the mailbox, and retain them on the engine thread. A crash leaves the attempt visibly unresolved.
  - Keep the worker's proposal in the mailbox unstaged until G. Drop it with a record if the model has staged its own.
  - Run TSan on the snapshot sharing early.

---

## Low

- **L1. The chapter-table locator has no reader (unconfirmed).** §4.4 and §15. `chapters[].cut: {journal, sequence}` has no read path: RetainedState reads facts by ordinal (`fact`, `fact_page`) or by identity (`lookup`, used at context.cpp:546-548). Its public `source(SourceReference)` reads dependency payloads, not facts (declared in include/blackbird/retained_state.hpp). The cut frame's revision is already its record identity and resolves through `lookup` and the archive catalogue. Store the revision; keep `{journal, sequence}` only if a by-sequence reader is added.
- **L2. Restore after archive does not undo the archive.** §5.6.
  - An archived entry that had been edited before archival is fetched and restored as its pre-edit original (restore uses originals, context.cpp:1077-1096 and 1593-1627). The archive index and `context_view {scope: archived}` describe bytes the model never had live.
  - Index rows for a restored round stay in an index entry before `p`, which then claims the round "left the live context".
  - Either record the archived (live) item version, or state that restore returns originals. Have restore absorb or rewrite the stale index rows, accepting the earlier `p`.
- **L3. J is not necessarily a provider turn boundary (unconfirmed).** §6.3 `user_boundary_only`, §7.1 J.
  - A workflow can end successfully after appending tool outputs without asking the model to continue. The provider's tool-use turn is then still open.
  - J settles the cut and `drop_after_change` strips that turn's thinking. The next engine turn adds the user text into the same user message as the `tool_result`, and Anthropic sees a `tool_use` turn without its thinking.
  - Define the turn boundary by entries (a final assistant message followed by a user entry), not by `finish_workflow`. The design also uses two definitions of "turn": "entries at or after the last operator user entry" (§3) and engine turns (§6.3, §11.1). `/lua` turns and `workflow_invoke` turns append no `operator` entry (coding.cpp:5850-5851, 6051-6055).
- **L4. The maintenance checkpoint can run inside any `log_.record` call.** §4.5 (snapshot after every cut).
  - RetainedState's maintenance trigger calls `ContextStore::checkpoint` from inside an append (retained_state.cpp:1672-1727; registered at context.cpp:494-495). That prunes `originals_` and clears `captured_` (context.cpp:676-679) while the cut writer is between building its frame and swapping state. A cut writer that holds references into `originals_` across the record call is left holding dangling references.
  - The snapshot after a cut can also fail: `no_new` throws `capacity` (context.cpp:592-593); `save_current_state` requires a live writer with no transaction, nothing prepared, reconciled and no predecessor segments (retained_state.cpp:3038-3040). That failure must stay best-effort, as `maybe_checkpoint` is (context.cpp:704-705), and never fail the cut.
- **L5. Settle at G for requests with an overridden `input`.** §7.1. A request whose `input` is overridden by options (coding.cpp:4386-4396) does not send the context, but its response is appended to it (coding.cpp:4830). A cut settled at that G produces a branch whose reasoning was generated against input that is not the new chapter's context. Skip settlement when `input` is overridden, as the step already skips (§11.1).
- **L6. Idle settlement of a carried pending cut is ambiguous.** §7.3, failed-turn row. "Idle … publishes at once" (§7.1) and "settles at the next permitted settle point" together could mean the pending cut settles immediately after the failure, between turns. Say whether idle is a settle point for a carried proposal or only for new operator proposals.
- **L7. `operator.beads` is classed as an instruction.** §5.2. Its content says "external data, not instructions" (main.cpp:1212-1219). Classing it as `instruction` pins external data forever. Class it `result`.
- **L8. Contradictory profile combination.** §6.2 and §13.3 validation. `drop_after_change` with `retain_current_turn` contradict each other: any cut with `p` before the current turn drops current-turn reasoning. The validator should refuse the pair.
- **L9. Pinned instructions have no overflow path.** §12. Lua developer-role appends are classed `instruction` and pinned forever. When pinned bytes alone exceed the window, options A and B both make no progress. Specify the terminal outcome (critical complaint, turn ends) so the session is not retried in a loop.

---

## Claims checked and found correct

- §2 anchors at `870ec8c`:
  - request flow: coding.cpp:4297-4302, 4494, 4821-4822, 4830;
  - replay: context.cpp:420-431, 426, 447-460;
  - entry IDs: context.cpp:946-952, 1150-1153;
  - managed frame: context.cpp:1646-1656;
  - saved state and checkpoint: context.cpp:645-648, 349, 670-677;
  - manage rules: context.cpp:1412-1426, 1369-1370, 1435-1443;
  - pending and finish: context.hpp:142-147; context.cpp:1452-1517; coding.cpp:6094;
  - request state and continuations: coding.cpp:4463-4466, 4260-4267.
- §6.3: `request()` refuses open groups before assembly (coding.cpp:4297, `validate_protocol` at coding.cpp:286-317). `publish_managed` refuses an incomplete result (context.cpp:1629-1632). G therefore never sees an open round. H3 is the exception, because it bypasses G.
- §7.1: in the single-threaded engine, a settle before `items` and `origin` cannot race the two head checks. The only context write between `origin` and dispatch is the request's own `provider.request-state` append, which reassigns `origin` (coding.cpp:4464-4465). `poll_commands` during streaming does not append to context (coding.cpp:2659-2700). Child workflows run by `drain_workflows` inside `request()` call `request()` again before the outer one takes `items` (coding.cpp:4239-4268, 4299), so the outer G sees their results. `request_group_active_` is set only at stage 14 (coding.cpp:4475), so a nested request at G is not refused `busy`. Parked `workflow_invoke` runs only after the outputs are appended (coding.cpp:5115-5149).
- §7.1 J: `record_boundary` writes the context and program packets in one `append` batch (context.cpp:94-149).
- §7.2: `append_impl` advances `expected` across appends (context.cpp:988-989). `edit` does not, so a non-append change yields `settlement-conflict` (context.cpp:1498-1500).
- §4.4: `current_programs` keeps only enumerated latest-wins labels and drops unknown ones from the saved programs (retained_state.cpp:2983-3013). New program record names must be added there.
- §4.6: archived originals stay reachable by entry ID after a checkpoint, through `original_entry` journal lookup in the same journal at or before the boundary (context.cpp:511-581). Before the checkpoint they are in the resident `originals_` (pruning happens only on a successful checkpoint, context.cpp:670-679).
- §5.2: `items()` projects only `item` (context.cpp:752-763), so an extra entry field does not reach the provider. Lua cannot choose an append label: `blackbird.append` hard-codes `lua.synthetic` (coding.cpp:1683). M4 is the remaining spoofing route.
- §5.2 append sites: the full set of `ContextStore` mutation callers is the one listed in the design plus `initial` (coding.cpp:2642, empty) and the idle operator paths: application.cpp:299; backstop.cpp:377, 383; coding.cpp:1663, 1672, 1678, 1683, 2642, 4261, 4464, 4830, 5243, 5258, 5591, 5595, 6001, 6052; main.cpp:424, 1146, 1159, 1212; session.cpp:596. No other label exists.
- §11.1: a side request at G runs before the main request group is owned, so its own `RequestGroup` can begin and end cleanly before stage 14. C4 is the exception (M8).
- §11.2: the engine busy-polls in `CommandJobs::wait` (command_jobs.cpp:1366-1476, `condition.wait_for` 10 ms). The provider fields (`request_group_owned_`, `cancelled`) are plain and unlocked (include/blackbird/coding.hpp:23 and following), so a separate provider instance is required.
- §17:
  - `seed_successor` requires an empty journal and no pending proposal (context.cpp:908-912).
  - The RRC check scans tail facts for the append `origin` (session.cpp:571-586). In compact mode a checkpoint between the note and `consumed` hides it. Reading the live entry's creating-record origin fixes this, subject to M4's derivation.
  - The backstop builds its own `ContextStore` on its own root (backstop.cpp:375-385).
