# Tribunal: doctrine conformance, model ergonomics (BHI) and waste

Design under review: `docs/CONTEXT_LIFECYCLE.md` (draft for `arconaut-rd7.1`), read in full on
2026-10-20. Standards: `~/projects/BLACKBIRD.md`, `AGENTS.md`, `papers/clm-a1v-2026-10-19/PLAN.md`,
`docs/INSTRUMENTATION.md`, `docs/INTERACTION_THEORY.md`, and the settled operator decisions in the brief.

Source facts I checked for this review, at `870ec8c` unless noted:
- IDs are 16 bytes: bytes 0-7 are the journal's issuer namespace, little-endian; bytes 8-15 are the
  issuer counter, little-endian (`src/retained_state.cpp:2873-2926`, the loop writing `bytes[index]` and
  `bytes[index + 8]`). Revision hex is `hex_identity(identity.bytes())` (`src/context.cpp:945-946`).
  So within one journal every round ID shares its first 16 hex characters. For counters below 65,536
  only hex characters 17-20 differ, and characters 21-32 are zeros. The design's own example
  `3de1dba2e9e6fb751b00000000000000` is counter 0x1b.
- On a retaining provider the whole operational-state text is appended to the context on every request
  that carries it (`src/coding.cpp:4460-4466`, `context_.append({state_item}, "provider.request-state")`).
- The version-1 snapshot writes the full `entries` list and `live_originals` (`src/context.cpp:645-648`).
- `arconaut-7c8`: "a non-empty suffix or any full replay is safe mode", and every clean stop writes a
  snapshot.
- C5's first launch grew from 1 KB to 362 KB inside one turn, in 12 requests (`C5_LIVE.md`).
- There are 43 built-in tool definitions in `src/tools.cpp`. Function schemas are sent with `strict`
  false (`src/responses_provider.cpp:35-41`).

Severity: **high** means the design as written defeats a settled decision or the model's ability to use
the mechanism. **Medium** means a real defect with a clear cost. **Low** means polish or a missing detail.

---

## High

### H1. Round IDs are 32 hex characters that differ in only about four places, and selection works only by listing them

- **Section:** §3 "Round", §5.4 (index rows and "Round IDs are given in full"), §9.1, §10.2, §11.1.
- **Problem.** The design says: "A round ID is the append revision (32 hex)" and "Round IDs are given in
  full. A shortened form would need ambiguity handling for no measured gain." The ID layout (above) means
  every round ID in a session looks like `3de1dba2e9e6fb75` + `XXXX` + `000000000000`. The model has to
  read these in the index and in `context_view`, and copy them into `context_manage {rounds: [...]}`.
  The only way to select is to list every ID. There are no ranges, no row numbers and no filters.
  The design's own example breaks its rule: `round 41b2…07` and `round 58aa…11` are shortened forms.
- **Governing rule.** INTERACTION_THEORY §1: practical agency needs a repertoire the participant can
  "name ... or reliably address". §2 favours "a concise relation with addressable originals" for the model.
  Waste doctrine: every byte counts.
- **Consequence.**
  - Copying 50 near-identical 32-character strings is a classic way for a model to make transcription
    errors. A single wrong digit returns a refusal or, worse, archives a different round.
  - Each ID costs roughly 16-24 tokens. Archiving 50 rounds therefore costs about 1,000 output tokens,
    the most expensive tokens in the request. The same IDs also sit in the archive index for every
    later request, and in every `context_view` page.
  - The claim of "no measured gain" was not measured. The waste is structural and predictable.
- **Direction.**
  - Give each round a short handle that is unique within the session, such as `r283` from the issuer
    counter. Print it in the index, in `context_view` and in the brief, and accept it in every context
    tool. Keep the full ID in the records.
  - Allow range and filter selection, such as `{rounds: "r12-r40"}` or `{superseded: true}`. The
    engine expands it to explicit IDs, and the cut frame records the expanded list.
  - Fix the example so it matches the rule.

### H2. The current-turn rule makes the nominal path inert for growth inside one turn, and the wording tells the model otherwise

- **Section:** §6.3, §7.1, §10.2, §11.1, §12 ("Mid-turn on `user_boundary_only` profiles").
- **Problem.**
  - `retain_current_turn` allows a mid-turn cut only "if it leaves every entry of the current turn
    untouched". `user_boundary_only` allows a cut "only where no turn is in progress". The conservative
    profile, used for every unprobed host, is `user_boundary_only`.
  - In autonomous work, one operator prompt drives the whole episode. Almost all growth is therefore in
    the current turn. C5's first launch grew to 362 KB inside a single turn.
  - So on every profile except `any_closed_round`:
    - the model's own `archive` waits until the turn ends;
    - the C6a step runs but its proposal cannot settle (§6.3: "A selection touching the current turn
      waits for turn end");
    - growth continues until the provider refuses. Then §12 says that on Anthropic "such an overflow
      ends the turn with the critical complaint".
  - The nudge still tells the model: "An archive starts a new chapter before your next request". The
    step brief lists all rounds without marking which ones can be archived now.
- **Governing rule.** "Never offer choices that will not be honoured". Escalation is inform, nudge,
  direct, fail, so failure should be the last resort, not the routine outcome. CLM serves the model's
  effectiveness.
- **Consequence.**
  - For the workload this design exists for (long autonomous turns on Anthropic and on unprobed hosts),
    the failure path becomes the normal path.
  - On OpenAI, in a single long turn, it also comes down to C4.
  - The model is told its archive takes effect now when it will not. It cannot plan around a delay that
    nobody announced.
  - Each step on these routes pays for a full side request that cannot settle.
- **Direction.**
  - State plainly, in the design and in every model-facing text, which rounds can be archived now and
    which only at turn end. Mark this on each `context_view` row and each brief row (for example
    `cut: now | turn-end`).
  - Split a mixed proposal: settle the part outside the current turn now, and stage the rest. Do not
    hold the whole proposal back.
  - Do not run the C6a step when nothing it could select can settle at this G.
  - Treat "growth inside one turn on a turn-bound profile" as an open design problem with its own
    decision, rather than leaving it to C4. Options include the rd7.3 text rendering, or an
    operator-visible turn boundary the model can request.

### H3. On a retaining provider, the new nudge appends about 1 KB to the context on every request

- **Section:** §10 table ("retaining providers: only with the nudge, per 2o9"), §10.2, §19 U7 and U12.
- **Problem.**
  - On Anthropic, `retain_request_state()` appends the whole operational-state item to the context
    (`src/coding.cpp:4460-4466`).
  - The design puts `CONTEXT_SIZE` and the roughly 900-byte `CONTEXT_REVIEW` there on every request at
    or above `nudge_bytes`. Both contain `N`, which changes on every request.
  - 2o9's fix direction ("emit retained state only when it changes") therefore never removes a
    duplicate.
  - U7 (the new wording) does not depend on U12 (2o9), so U7 can ship first.
- **Governing rule.** Decision 5: inspection, and here guidance, must not inflate the context it
  describes. Waste doctrine. C5 is the precedent: 80% of the final input was self-inflicted.
- **Consequence.**
  - Above 384 KiB, every Anthropic request adds about 1 KB of stale guidance that every later request
    resends. Over 100 requests that is about 100 KB, and the total resent bytes grow quadratically.
  - The `state` class makes these entries selectable and sweepable, but only in a rewritten region. On
    `user_boundary_only` no cut happens mid-turn (H2), so in a long turn nothing removes them.
- **Direction.**
  - On retaining providers, append the review text once, when the nudge level is first crossed, and
    again only when its meaning changes (for example, the direction level is reached). Keep the
    changing numbers out of the retained form.
  - Make U7 depend on 2o9.

### H4. A reserved `retain` argument on every tool costs bytes on every request, and its `ephemeral` half is not honoured as described

- **Section:** §5.3, §5.5, §19 U3.
- **Problem.**
  - §5.3: "Every tool accepts a reserved `retain` argument, `ephemeral` or `durable` ... `ephemeral`
    marks the round for removal at the next cut". But §5.5 limits removal to "the rewritten region
    only ... Ephemeral and state entries before `p` wait for a later cut that reaches them".
  - So `ephemeral` does not mean removal at the next cut. It means removal if some later cut happens
    to start before that round, which may be never.
  - `durable` duplicates `context_manage {mode: pin}`.
  - If `retain` is in each schema, it adds an enum property and description to 43 built-in tools plus
    every Lua-defined tool. That is several KB in the fixed prefix of every request, against 27 KiB of
    fixed overhead today, and it is not measured.
  - If it is not in the schemas, the model cannot discover it.
  - A Lua-defined tool with its own `retain` parameter collides with the reserved name.
- **Governing rule.**
  - "Never offer choices that will not be honoured".
  - "Is this bullshit?": a second way to express a pin, plus a removal flag that usually does nothing.
  - Waste doctrine.
- **Consequence.** The fixed overhead grows on every request of every session. The model declares
  rounds ephemeral, and they stay. It is misled about the cost of what it keeps.
- **Direction.**
  - Drop `retain`. Keep pins in `context_manage`.
  - If ephemeral declarations survive review, make their meaning exactly "removed at the first cut whose
    rewritten region contains this round", and say so in the result.
  - Measure the schema cost before any form of it ships.

---

## Medium

### M1. A size target comes back through the thresholds and the hysteresis

- **Section:** §10.2 `CONTEXT_SIZE`, §11.1 hysteresis and brief.
- **Problem.**
  - Every request now shows `nudge_bytes=T direct_bytes=D` next to `input_bytes=N`: a gauge with a
    redline.
  - The brief says "above the review level D".
  - The hysteresis says "a cut that leaves input at or above `direct_bytes` counts as a decline". An
    archive of 5-10% of hot context is the operator's own description of typical archival, yet if it
    leaves the context above D the harness treats it as a refusal and asks again after every 96 KiB of
    growth.
- **Governing rule.** "No fetish for sizes ... holding context at some target is not the aim." The
  operator's 5-30% is "typical archival, not a goal".
- **Consequence.**
  - The model learns that the job is to get below D. That is the retired `target_bytes` under a new name.
  - If `clm.step` records such a cut as a `decline`, trials misread a real archival decision as a refusal.
- **Direction.**
  - Show `input_bytes` and what will happen next, in words: "Blackbird will ask you to review at D". Do
    not print threshold keys on every request.
  - Re-ask on growth since the last step, whatever its outcome.
  - Record the true outcome (`proposal`, and its sizes) and leave the classification to the tertiary
    process.

### M2. Inspection output stays in the context, and the design says it does not

- **Section:** §9.1 ("Calling it does not change the head or the chapter"), §5.5, §8.1 class `ephemeral`.
- **Problem.**
  - A `context_view` result is a `function_call_output` append. It changes the head, and it stays live
    in every later request until a cut whose rewritten region contains it.
  - If the model views and then declines, or views more than once while the nudge repeats, each page
    (up to about 40 KiB by the design's own estimate, more by M7's) stays indefinitely.
- **Governing rule.** Decision 5: inspection never durably alters context.
- **Consequence.** It is a bounded version of the C5 failure: inspection still feeds the growth it
  reports. The design's statement about it is false.
- **Direction.**
  - Correct the claim.
  - Choose a delivery that is actually non-durable, for example a short output stub plus the rows
    carried once in the request-local state. Accept that a retaining provider needs a different answer
    (see H3).
  - Otherwise, state the bound openly: one live view at a time, with an earlier view output swept when
    the next view lands.

### M3. The cost rule is ambiguous and false where there is no cache

- **Section:** §10.2 ("fewer, larger and later cuts cost less"), §14.
- **Problem.**
  - "Later" can mean "cut later in time" or "cut later rounds". The design means position, but a model
    can read it as "wait".
  - Read as position, the rule pushes the model to archive recent rounds rather than the oldest. A 5%
    cut of the oldest rounds breaks even only after about 200 requests (§14). Old rounds are usually the
    least relevant ones.
  - On routes with no cache, the rule is false: Anthropic today (§14: "Anthropic gets no caching
    today"), the conservative profile, and uncached hosts. There a cut costs nothing extra and saves on
    every later request.
- **Governing rule.** CLM serves effectiveness. Never mislead the model. Make no invented performance
  claims.
- **Consequence.** The model avoids useful archival because of a cost that does not exist on its route,
  or it archives recent material it still needs.
- **Direction.**
  - Make the cost sentence profile-driven. Say "no prompt cache on this route: a cut costs no extra
    re-read" where that applies.
  - Phrase the cost in the model's terms: "archiving round X re-reads B bytes once". The `context_manage`
    result already returns that number.

### M4. After a step makes a cut, the model is not told

- **Section:** §11.1 ("Nothing you write here enters your context"), §5.4 placement.
- **Problem.**
  - The step's reasoning and its call are discarded, which is correct.
  - The main request then goes out as the first request of a new chapter. The model's only sign of what
    happened is an index entry inserted at `p`, somewhere in the middle of its context.
  - A `chapter=K` counter also changes in `CONTEXT_SIZE`.
- **Governing rule.** INTERACTION_THEORY §4 (feedback must tell the participant what changed) and §6
  (compatible reference).
- **Consequence.**
  - The model does not remember choosing the archive. Material has vanished, and it may re-read files or
    rerun commands to recover it, which wastes the cut.
  - "Lost in the middle" makes it likely to miss an index entry placed mid-context.
- **Direction.** On the first request of a chapter made by a step or a recovery, put a one-line notice in
  the request-local state: what was archived, by whom, and where the index sits. The badge serves the
  operator; this serves the model.

### M5. The step allows one `context_manage` call, so one mode

- **Section:** §11.1 ("one `context_manage` call (archive, pin, unpin, or one-round summary)").
- **Problem.** A model that wants to archive some rounds and pin others, which is the natural reading of
  "Keep what you still need, pin what you will need later", cannot do both in the step.
- **Governing rule.** INTERACTION_THEORY §1: the participant should be able to express the intended
  scope "without a workaround that changes its meaning".
- **Consequence.** Pins are silently left for later, or the model drops one of the two intentions.
- **Direction.** Let one `context_manage` call carry `archive`, `pin` and `unpin` lists together. Pins
  change no bytes, so combining them changes nothing in the cut.

### M6. A snapshot after every cut copies the whole context, which the design rejects elsewhere

- **Section:** §4.5, against §4.2 "Size" and §20 "Full-entries cut frames".
- **Problem.**
  - The design rejects `managed` frames because they carry "a copy of the whole retained context per
    cut, against the waste doctrine".
  - It then adds "one more snapshot point: after every cut". A version-1 snapshot writes all `entries`
    and `live_originals` (`src/context.cpp:645-648`), so this is the same copy on disk.
  - The stated benefit, "the next reopen an empty or short suffix", does not hold under 7c8:
    - every clean stop already writes a snapshot;
    - after an unclean stop, "a non-empty suffix ... is safe mode" whether or not a cut snapshot exists.
- **Governing rule.** Waste doctrine. "Is this bullshit?"
- **Consequence.** A full-context write on every cut, for no resume case the design names.
- **Direction.** Drop the snapshot after a cut, or name the concrete case it serves (for example, safe
  mode starting playback from the newest snapshot) and measure it.

### M7. The `context_view` row size is invented, and the keyed-object format wastes bytes

- **Section:** §9.1 ("A row is about 100-150 bytes, so a page is at most about 40 KiB").
- **Problem.**
  - A row has 14 named fields. If it is emitted as keyed objects, the key names alone are about
    110 bytes per row.
  - With a 32-hex round ID, an 80-byte argument head and a 32-hex locator, a realistic row is
    250-350 bytes. Then 64 rows is about 20 KB, and 256 rows about 80 KB.
  - The `entries` field is undefined: a count, or a list of entry IDs that adds about 120 bytes more.
- **Governing rule.** No invented performance claims. Waste doctrine. And M2: the page stays in the
  context.
- **Consequence.** The view costs two to three times its stated size, and the page stays live.
- **Direction.**
  - Emit rows as aligned text with one header line, using the short handles from H1.
  - Define `entries` as a count.
  - Measure the C5 fixture before stating a size.

### M8. The generic 16 KiB cap has no per-call override, and it keeps only the head

- **Section:** §8.1 (generic cap; `colleague`, `participant_read` and `decision_model` "text capped at
  16 KiB head").
- **Problem.**
  - For `exec` and `read_file` the model chooses its shape with `output_max_bytes` and ranges, "honoured
    exactly". For the generic cap and for colleague text, the design gives the model no way to ask for
    more.
  - It keeps only the head. A colleague review or a Lua result usually has its conclusion at the end.
- **Governing rule.** Arbitrary ceilings are rejected. Model agency. Shaping at creation should follow
  the model's declared intent where it has one.
- **Consequence.** A 40 KB Kimi review costs the model several extra `context_inspect` rounds. Each round
  is another live entry, which is worse waste than one request at the size it wanted.
- **Direction.**
  - Accept `output_max_bytes`, and a head/tail split, on every shaped tool, using the same semantics as
    `exec`.
  - Use head plus tail for text results.

### M9. An operator interrupt discards the model's pending archival

- **Section:** §7.3 ("Operator interrupt or stop: cancelled"), against §21 item 4 and §7.4.
- **Problem.**
  - §21 item 4 keeps a pending cut through a failed turn and a crash, because "it settles no effects, and
    every original stays retained". That reasoning applies equally to an interrupt.
  - AGENTS.md separates a plain interrupt from interrupt-and-apply-now. A plain interrupt is not a
    rejection of pending work.
- **Governing rule.** Model agency. Change-activation semantics. Timing semantics are "not a new approval
  gate".
- **Consequence.** The operator presses Ctrl-C to redirect the work, and the model's context decision
  disappears. The operator has to know this to avoid it.
- **Direction.** Keep the pending cut through an interrupt. Give the operator an explicit cancel command
  and show the pending cut in the TUI.

### M10. Option A recovery asks the model to choose without seeing what it is choosing

- **Section:** §12 Option A, §21 item 1 ("A keeps history verbatim and leaves the choice with the model").
- **Problem.** The side request carries the pinned instructions, the latest prompt and "the round index
  without bodies". The model selects from tool names and 80-byte argument heads, without the recent work
  that tells it what the task needs now.
- **Governing rule.** Operator: "The model needs enough context to understand what it can drop."
- **Consequence.** The model archives blind, or it archives too little and recovery falls through to B.
  The claim that A "leaves the choice with the model" overstates what the model can judge.
- **Direction.** Within the byte budget, include the most recent rounds verbatim, newest first, and the
  index for the rest. State in §21 that this is a decision made under a failure.

### M11. The design makes renames and capability removals the operator reserved, and §21 does not list them

- **Section:** §3 (last paragraph), §15 (`/compact` becomes `/archive`, the badge text, the
  `ContextChangeKind` mapping), §5.1 (`select` retired), §9.2 (`kind: history` and `kind: index` removed
  from the model's tool), §9.3 (`context_originals` and `context_restore` removed), §4.5 (snapshot per
  cut), §10.1 (`direct_bytes` 448 KiB).
- **Problem.** PLAN.md: the renames are "each proposed for operator decision rather than done". The
  design does them. It also removes model capabilities and sets a new level, and none of this appears in
  §21's "Decisions taken in this draft".
- **Governing rule.** Operator decisions belong to the operator. Phase and naming membership: propose,
  don't decide.
- **Consequence.** The operator reviews §21 and misses most of the decisions actually taken.
- **Direction.** List every rename, every capability removed from the model, and the new level in §21 as
  proposals.

### M12. Validating provenance on every profile field is receipt ceremony, and it blocks operator overrides

- **Section:** §13.2 ("Every behavioural field carries `basis` ... Unknown keys are refused"), §13.3
  ("`basis` on every behavioural field, sources on cited fields ... refused with the exact reason").
- **Problem.**
  - Native validation refuses a profile, or an operator's sparse override, that lacks provenance
    metadata.
  - The example puts `basis` on families, not on fields, so the schema is not even consistent with its
    own rule.
- **Governing rule.** "We don't ... chase receipts." "Almost no routine approval friction."
- **Consequence.** An operator who writes a quick override to fix a broken route has it refused for a
  missing citation.
- **Direction.**
  - Keep `basis` as recorded information, reported per request.
  - Validate only what affects behaviour: schema, enumerations, ranges and codec consistency.
  - Never refuse an override for missing provenance.

### M13. Items a model's own Lua program appends become pinned instructions it cannot archive, and `context_edit`'s reach over instructions is unstated

- **Section:** §5.2 (the instruction row: "workflow and program briefs appended by Lua with role
  developer, user or system"), §5.2 ("Only the operator removes them"), §9.3 delta `context_edit`.
- **Problem.**
  - The design criticises role-based pinning for "pin[ning] derived data along with instructions". It
    then classifies every Lua append by role, so data that a model-written program appends with role
    user becomes un-archivable.
  - Separately, `context_edit` keeps `replace` and `remove` operations, and the design does not say
    whether they may touch instruction entries. Either "only the operator removes them" is false, or
    `context_edit` carries an undocumented restriction.
- **Governing rule.** Model agency over its own programs. "Context tools never silently rewrite history".
  Honest tool descriptions.
- **Consequence.** The model's own program output accumulates with no recourse, and the tool's real
  limits are unknown to the model.
- **Direction.**
  - Let an append declare its class (`instruction` or `result`), with `result` as the default for
    model-authored programs.
  - Write `context_edit`'s rule for instruction entries into its description and into §5.2.

### M14. The delivery units break conceptual integrity

- **Section:** §19.
- **Problems.**
  - **U3 is a grab bag.** It holds the generic cap and `result_ref` (shaping), the
    workflow-continuation class change (classification, which is U5), the `retain` argument (pins, U5;
    §5.3 itself says "Shape is not part of `retain`"), argument heads (the index, U4/U5) and the exec
    read-back measurement.
  - **U4 comes before U5 but presents U5's concepts.** `context_view` rows show `class`, `pinned`,
    `chapter`, `archived_in` and `superseded_by`, none of which exist before U5. U4 also removes
    `context_restore` while its replacement, `context_manage {mode: restore}`, arrives with U5's cuts.
    Between the two, the model has no restore at all.
  - **U5 is too large for one owner.** It covers the ID type, frames, playback, origin and classes,
    pins, the index and its placement, sweep, reasoning rules, start and rebind cuts, snapshot v2, the
    chapter table, the snapshot after each cut, and `ContextChange`. These hold separate invariants:
    persistence and playback; selection and classes; provider validity at a cut.
  - **One invariant is split across two units.** "A cut never produces an input the provider will
    refuse mid-turn" lives partly in U5's reasoning rules and partly in U6's `open_turn` enforcement at
    settle points.
  - **U7's dependencies are wrong.** Its text promises settlement "before your next request", which is
    U6's work. It also needs 2o9 (H3). It lists only U4 and U5.
- **Governing rule.** BLACKBIRD.md, "Delegation units are conceptual units": "never hand an agent a grab
  bag ... Never split one invariant across several owners".
- **Direction.**
  - Move `retain`, the classes and the continuation class into the selection unit.
  - Order the inspection unit after the core frames, or split the `context_view` index (no chapter
    fields) from the chapter-aware columns.
  - Split U5 along its three invariants.
  - Put `open_turn` with the settle points.
  - Make U7 depend on U6 and 2o9.

### M15. The model-facing schemas and descriptions of the new context tools are not specified

- **Section:** §5.1, §9.3 ("Every context-tool description states how it interacts with chapters"),
  §10.2, §11.1.
- **Problem.**
  - Today's `context_manage` takes an untyped `proposal` object, and its description is full of jargon
    ("strict CAS", "invocation snapshot", "Structural acceptance does not certify accuracy",
    `src/tools.cpp:1390-1408`).
  - The design changes the modes but does not give the new parameter schema, the result shape, or the
    description text.
  - The nudge writes `context_manage {mode: "archive", rounds: [...]}`, which does not match the current
    wrapper.
  - The design also puts "chapter" into model-facing text, where the model needs the consequence ("your
    context changes before the next request; B bytes are re-read once"), not the harness's taxonomy.
- **Governing rule.** BHI. INTERACTION_THEORY §1 and §3: stable, intelligible meaning.
- **Consequence.** The wording that decides whether a model uses the tool, and uses it correctly, is left
  to implementation, which is where C5 failed.
- **Direction.**
  - Write the exact typed schemas, result fields (`settles_at`, `rewrite_bytes`, the handles archived)
    and description text in the design, so the tribunal can judge them.
  - Keep "chapter" out of model-facing text, or use only the ordinal.

---

## Low

- **L1. §3: `ContextChapterId` is redundant.** A chapter is identified by the revision of the cut frame
  that starts it. A new ID type, a new issuance and an extra 16 bytes in every request record duplicate
  that revision. Direction: name a chapter by its start revision, and keep the ordinal for people.
- **L2. §5.2: the per-entry `origin` label string is resident and snapshotted.** Labels such as
  `provider.branch:<32 hex>` are about 50 bytes per entry, in memory and in every snapshot, to derive a
  six-value class. Direction: store the class as a small enum, or intern the label.
- **L3. §5.6 and §9.2: `context_inspect {round}` returns everything in the round.** That includes
  encrypted reasoning or thinking signatures, and full call arguments such as a whole `write_file` body.
  These are bytes the model cannot use or already has. Direction: add `part: output | call | message`
  and leave out reasoning.
- **L4. §5.4: message-only index rows carry no content.** `round 58aa…11 · message · 0.6 KiB` tells the
  model nothing about what it is choosing. Direction: add the first 60-80 bytes of the message text.
- **L5. §11.1 and §4.2: small writer-side transformations.**
  - `clm.step` outcome strings such as `refused:<reason>` and `failed:<class>` compose a string in the
    producer.
  - "Counts as a decline" reclassifies an outcome in the producer (see M1).
  - The `rules {...}` field repeats what the profile digest in the binding already identifies.
  - Direction: separate native fields, no reclassification, and drop `rules` or justify it.
- **L6. §11.1: details of the side request.**
  - On breakpoint profiles the side request should carry no `cache_control`. Its prefix differs from the
    tools onward, so a cache write is a premium paid for a prefix that is never reused.
  - The step input, "the main request's exact input prefix ... plus one final developer brief", may also
    carry the nudge's `CONTEXT_REVIEW`. That gives two guidance texts with different instructions.
  - The §18 oracle "byte for byte" conflicts with `side_request_reasoning: drop_all`.
  - Direction: state the step input exactly.
- **L7. §3: an inaccurate claim.** "which is also the prefix of every entry ID in it" is false. The
  `function_call_output` entries answering a round come from separate appends with their own revisions.
- **L8. §11.1: the brief omits two things the model needs to plan.** It does not say that declining
  leads to another review after about 96 KiB of growth. It does not say what a pin protects against
  (option B windows, and the model's own later selections). Both are cheap to add.
- **L9. §9.1: the source of `scope: archived` rows is unspecified.** Cut frames carry only IDs, so the
  rows need either a lookup of each original in the journal or parsing the index text. Direction: say
  which, and what it costs.
- **L10. §4.2: cut frames list every entry ID as a hex string.** They could list rounds as native 16-byte
  IDs plus their output entries. That is about a third of the bytes, and it matches the internal-format
  rule's preference for native binary.
- **L11. §10.1: the gap between nudge and direction is narrow.** `direct_bytes` is 448 KiB against a
  384 KiB nudge. At 8-16 KiB per round, that is roughly 4-8 requests of nudging before direction. That is
  thin for "directs only later", and C5 already showed 16 nudges being ignored. Trials will tell, but
  §21 should list the level as a decision (M11).

---

## Judged sound

- **The cut frame is a delta.** It plays back as recorded, with no selection, pin or profile logic during
  replay. A malformed frame sends the session to safe mode. This matches "replay plays back recorded
  frames".
- **One journal and a constant conversation ID across chapters.** This keeps locators resolving and keeps
  OpenAI cache affinity. The rejected alternatives (§20) are argued from source and provider mechanisms.
- **Provider and server compaction are refused**, Anthropic `clear_tool_uses` is declined, and there is
  no harness-authored fallback (option C). This is consistent with the decisions that Blackbird owns
  context management and that compaction is a failure.
- **`target_bytes` is retired**, and the nudge is reworded around the work. Declining is a real,
  honoured outcome in the step, and the step's own output never enters the main context.
- **Shaping happens once, at creation.** A cut never reshapes a retained output. The retained original
  stays complete, and operator and internal calls stay unshaped.
- **The index sits at `p`, not at the head, and sweeps are confined to the rewritten region.** Both are
  the right cache choices (within H4's caveat about what `ephemeral` promises).
- **`context_view` becomes a bounded index, and `context_inspect` returns text pages instead of hex.**
  This removes the C5 defect at its cause (with M2 and M7 as caveats).
- **Tool restriction follows the profile**, with `palette_only` as the fallback for unprobed routes. A
  forced tool choice is rejected. This honours "never offer choices that will not be honoured".
- **Jev is advisory, on the side request only, never at creation**, and a failed Jev call drops its
  column.
- **Profiles are data-only Lua tables read by a native constructor-subset parser and recorded as BBM2.**
  No JSON. The hard-coded rules are migrated with a regression oracle against the old code's outputs.
  The parser is a new owned surface to fuzz, which is a fair price for making "data-only" a property of
  the grammar.
- **There is no pin ceiling**, and `pinned_bytes` is visible to the model.
- **Overflow recovery is audited as a critical error** through the existing rageshake complaint, with one
  recovery per request and the failed attempt excluded from the retry.
- **Frames and records carry measured sizes only**, with no durations or ratios, and step timing comes
  from raw clock facts. This matches INSTRUMENTATION.md (subject to L5).
- **A pending cut survives a crash and a failed turn** (subject to M9 on interrupts).
- **The testing section** picks oracles by fault class, requires every test to fail on the old code, and
  keeps mutation runs on the fleet. The model oracle and the metamorphic oracle attack different fault
  classes, semantics and persistence, so neither is a check on the other.
- **Stateless full replay and the refusal of `previous_response_id` are kept as Blackbird policy**, not
  turned into profile rules.
