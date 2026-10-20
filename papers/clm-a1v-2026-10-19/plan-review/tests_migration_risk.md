# Plan review: test strategy, migration and compatibility, risk and kill criteria

Reviewer: independent adversarial agent, fresh context, 2026-10-20.
Subject: docs/CONTEXT_LIFECYCLE_PLAN.md (untracked, in worktree `context/clm-a1v`), delivering
docs/CONTEXT_LIFECYCLE.md revision 3. The design itself was not re-reviewed.
Fault class: oracles, the must-fail-on-old-code rule, migration and compatibility, live checks,
risks and kill criteria, the trial design (ib8). Read-only: source, tests, scripts, the 2026-10-17
campaign runs and the C5 run were inspected. Nothing was built or run.

Line numbers refer to the worktree at `4152691` unless stated.

## High

### H1. Installed generations get no provider profiles

- **Plan section:** P1 (files), P2 (exit), §5.4 point 1, §6 row P1 → P2.
- **Evidence:** P1 ships `profiles/providers/*.lua` "beside `programs/` through
  `BLACKBIRD_RESOURCE_ROOT` (CMakeLists.txt:196-197)". The resource root is a compile-time path
  (`CMakeLists.txt:196`, `${CMAKE_SOURCE_DIR}` by default). `scripts/install-blackbird:83` copies
  only `programs` and `assets` into `generation/share/blackbird/`, sets the root there (line 88),
  and its required-file check (line 27) does not list profiles. `./scripts/blackbird` follows the
  installed generation (docs/USING_BLACKBIRD.md:3). The plan never mentions `install-blackbird` or
  `check-install-blackbird`.
- **Consequence:** P2's "behaviour unchanged by construction" holds only in a build tree. An
  installed generation finds no profile files. The plan does not say what happens then. Either the
  load fails, or every route falls to `conservative`, which has no cache controls (§15.4). Then
  `prompt_cache_key`, the `session-id` header, the OpenAI cache policy and the effort sets all
  change on the operator's own launches. The regression table cannot see this, because it runs in
  the build tree. If `conservative` has no effort values, `--once` and application runs need a
  supported effort and stop.
- **Direction:** add `scripts/install-blackbird` (copy and required-file check) and
  `scripts/check-install-blackbird` to P1's files. Make a missing or unreadable profile directory a
  visible startup refusal, not a silent fallback. Add one P1 red test: an installed generation's
  `provider.request` names the expected profile id. It fails today: the generation has no profiles.

### H2. The first trial batch cannot exercise cuts on any of its routes

- **Plan section:** §7.1 entry criteria, §7.2 matrix, §4.2 rd7.2 case 5, A1 live check.
- **Evidence:**
  - The trial tasks grow inside one provider turn. In `~/projects/blackbird_runs/2026-10-17-cpp26`
    the long tasks spent their whole growth in turn 0: r-t03-luna 780 s then 6 s, r-t05-luna 481 s
    then 3 s, r-t06-luna 347 s then 6 s, r-t03-mimo 2531 s then 17 s (`meta.txt`). The continuation
    turn only answers DONE.
  - OpenAI ships `retain_current_turn`: it settles only cuts that touch nothing in the current turn
    (design §7.4). `conservative` ships `closed_only` with `turn.close = none` (§15.4). The boundary
    stays disabled until its probe passes (§7.3). A1 skips the step "when nothing can settle" (plan
    A1, design §13.1).
  - In an autonomous run every round after the task prompt is current-turn. On luna, nothing can
    settle during growth unless rd7.2 case 5 enables `user_note`. The plan names no turn rule and no
    probe for Paper `ultramarine-1-flash` or MiMo `mimo-v2.6-pro`. §4.4 probes only note placement
    there.
  - Few runs even reach the levels. REQUEST_SIZES.md §2 (native max): only r-t03-luna (753 KiB),
    r-t03-mimo (400), r-t06-luna (389) and r2-t05-paper (444) passed 384 KiB. Only r-t03-luna and
    r2-t05-paper passed 432 KiB.
  - Five of the nine long-task runs failed for reasons that have nothing to do with CLM: r-t03-paper
    127, r-t05-mimo 1, r-t05-paper 1, r-t06-mimo 1, r-t06-paper 1.
- **Consequence:** in most cells, "inform + nudge + step" behaves like "inform + nudge". A model's
  own `context_manage` call stays `after-turn` until the task is finished. About 400M input tokens
  would measure the baseline five times. The risk "the step is noise" cannot be observed either:
  steps that never run produce no declines.
- **Direction:** before any batch, require per route that the shipped profile permits mid-turn
  settlement: boundary enabled by a passed probe, or `any_closed_round` cited or probed. Otherwise
  drop the route from the cut arms. Run a small pilot, about one run per route per arm on t03, read
  by the ib8.1 reader. It must show at least one settled mid-turn cut per cut arm. Either state
  trial levels low enough that the tasks cross them under the current build (A1 already uses 60 and
  96 KiB), or choose tasks by measured growth under the current build, not the 2026-10-17 build.

### H3. S0 misses the `--once` and restart exits, and its red test can pass on the old code

- **Plan section:** S0 (files, red tests, exit); §4.5 recognition rule; §7.2 trial tooling.
- **Evidence:**
  - The one-shot path returns with no checkpoint: `if (one) { perform(...); ...; return
    restart_pending.load() ? 75 : 0; }` (src/main.cpp:1607-1611). The station path also returns
    `75` or `0` (1603).
  - S0 lists `/quit`, `/exit`, EOF and signals for the plain and TUI loops. It does not list
    `--once`, `--resume-once` or the restart exit (75).
  - `run-one` launches every turn, continuations included, as a separate `--once` process on the
    same session (`~/projects/blackbird_runs/bin/run-one`, `launch()`). Self-development restarts
    leave through exit 75.
  - The red test reopens and checks "the saved boundary equals the journal end". On reopen the old
    code already checkpoints: `maybe_checkpoint` runs at the end of the constructor when the suffix
    is at least 64 records or 4 MiB (src/context.cpp:698-706, called at 488). `main.cpp:551-563`
    also checkpoints after any non-compact open.
- **Consequence:** every trial continuation and every one-shot or application reopen would report
  an unclean stop. That floods the §18 signal "recoveries and read-only sessions" with false
  events, and ib8.1 would bless it. S1's resume table depends on telling clean stops from unclean
  ones. A test fixture with 64 or more appends, or one that inspects state after open, passes on
  the old code for the wrong reason.
- **Direction:** list `--once`, `--resume-once`, the restart exit and the station exit as clean
  paths, each with a case. Take the oracle from the bytes on disk before any reopen: read both slots
  and the journal end, or count the recovery event written at open, never the state after open.
  Require the snapshot to be the last journal write on each path, with a background job and a
  participant active at quit. Name the channel of the recovery-event record. On the context channel
  it would itself be a frame that older binaries refuse (see M2).

### H4. No oracle checks that a cut removes only what it should and keeps the bytes before `p`

- **Plan section:** S3a red tests and exit.
- **Evidence:** S3a checks four things: the reasoning kept or dropped per `at_cut`, that the export
  does not throw and pairs correctly, note placement, and rebase. S1's model oracle checks that
  `apply` executes a frame. S2's model oracle checks `expand`. Nothing checks that the frame S3a
  composes removes exactly expand(selection), plus the derived entries §5.6 allows in the rewritten
  region, plus the reasoning the profile drops. Nothing checks that entries before `p` are
  byte-identical on the wire after the cut (§5.5: "every byte before `p` stays identical"). "A
  sweep that moves `p`" appears only under review attacks.
- **Consequence:** a composer or sweep that also drops an unselected unit passes every listed test.
  So does a sweep that rewrites an entry before `p`. That is silent context loss, the failure class
  of the state tribunal's H1, or a silent cache cost. Review alone guards the design's central
  claim.
- **Direction:** add one property oracle over generated contexts. Post-cut entries must equal
  pre-cut entries, order kept, minus exactly that set, plus the inserted notes and boundary at their
  anchors. Export through the real adapter before and after the cut, and require the wire bytes
  before `p` to be equal. Both checks are cheap and reuse S3a's generators.

## Medium

### M1. Prefix and rebind cuts on existing sessions come from more units than V1

- **Plan section:** §6 rows S3b → V1, V1 → E1, P1 → P2; §1.2 lanes.
- **Evidence:**
  - The binding includes `tools_digest`, `instructions_digest` and `profile.digest` (design §4.2).
    Any change is a prefix or rebind cut at the next permitted point (§6.1, §6.4).
  - Units after S3b that change tool schemas or descriptions:
    - T1, if it lands after S3b. Lanes are parallel and T1 is ordered only "before V1"; it adds
      `output_max_bytes` and `output_head_bytes`.
    - E1: the `context_budget` schema.
    - R1: rageshake `raised_by` and `severity` (src/tools.cpp:1306-1318).
    - V1: the `lua` tool description at src/tools.cpp:1487-1490, which names
      `blackbird.restore(id)`. It is missing from V1's files.
  - Profile revisions change the digest: K1, rd7.2 results, 5st.
  - Profiles are read at run time from the resource root. In a build tree that root is the source
    checkout (CMakeLists.txt:196), so editing a profile file in a worktree changes the digest seen
    by already-built binaries and running trial sessions.
  - Unverified: whether each provider accepts a retained `function_call` or `tool_use` naming a tool
    that is no longer in `tools` (V1 removes `context_restore` and `context_originals`). The plan
    neither checks nor cites this.
- **Consequence:** the migration table understates the chapters existing sessions get. A trial
  batch can gain rebind chapters in the middle of a run. On Anthropic every prefix cut drops all
  reasoning. Sessions that called the removed tools might be refused after V1.
- **Direction:** list every post-S3b tool, instruction or profile change as a migration row. Decide
  whether the digest covers only behavioural fields (not `basis` or `read` dates). Pin the profile
  set per trial batch by copying it, and record its digest in `meta.txt`. Add one adapter-fixture
  case per route: a historical call to a tool that has been removed, plus one live request on the
  probe routes.

### M2. The downgrade story is overstated, and nothing protects the operator's sessions

- **Plan section:** §6 rows S0 → S1 and S3a → S3b; §5.4.
- **Evidence:**
  - `load_slot` ignores a version-2 slot (src/saved_state.cpp:147-153). With two slots, the old
    binary takes the other slot if it is still v1 (saved_state.cpp:226-237). Once both are v2 it
    needs a full replay, and the selector allows that automatically only while the journal's
    `available_end` is at most 64 MiB (src/retained_state.cpp:340-342). Otherwise the open refuses:
    "Session needs full recovery" (src/main.cpp:317-323). That limit counts journal bytes, not
    context frames.
  - S1 also writes `repair` frames, and S0 writes a recovery-event record of unnamed channel. The
    old replay refuses any context op other than `append`, `edit` or `managed`
    (src/context.cpp:426-427). Downgrade therefore breaks before S3b for repaired sessions, and
    possibly for every unclean reopen.
  - After S3b, an old binary does not refuse cleanly. `recover_coding_session` runs `root.reconcile`
    (src/coding.cpp:3078) before `ContextStore` is built (src/main.cpp:335, 422), and only then does
    the cut frame throw `corrupt`.
  - The plan has no rule keeping program-branch builds away from the operator's real sessions. It
    does not say that integration proposal 2 (or the first install) is the point after which the
    installed master generation cannot reopen sessions.
- **Consequence:**
  - Large sessions downgraded after S1 refuse to open without `--rebuild-session`.
  - A session touched by a post-S3b live check, or by an installed program-branch build, is stranded
    if the generation is rolled back, after the old binary has already written reconciliation
    records into it.
- **Direction:** correct the §6 rows. Live checks and trials use scratch sessions and
  `BLACKBIRD_EXECUTABLE`, never the operator's sessions. Integration proposal 2 states the
  no-downgrade consequence. Put the S0 recovery event and the S1 `repair` frame on a channel old
  binaries ignore, or record the downgrade cost honestly. Consider an S1 feature-gate record that
  S1 and later binaries check before any write, so later format steps refuse cleanly.

### M3. The lists of existing tests that change are incomplete

- **Plan section:** §2.3; S3b, E1 and P1 "existing tests that change".
- **Evidence:** S3b makes `edit`, `restore` and `manage` stage instead of publishing. These existing
  assertions expect immediate publication and are not listed:
  - tests/journal_checkpoint_test.cpp:124-126 and 147 (`edit` accepted, then `restore`);
  - tests/saved_state_test.cpp:146-199 and 416-419 (listed only for S1);
  - tests/operation_raw_test.cpp:1494 ("actual Context edit establishes a valid saved revision");
  - tests/turn_raw_test.cpp:409-430;
  - tests/raw_retained_test.cpp:674;
  - tests/tasks_test.cpp:173-182 (edit accepted, then the next request's state is checked);
  - tests/coding_test.cpp:1578 (`assert(arco.edit(v).accepted); arco.restore(id)`).

  E1 changes two unlisted tests:
  - tests/request_cache_test.cpp:89-101: a v1 `trigger_bytes` / `target_bytes` proposal, plus a
    check for `CONTEXT_BUDGET` text;
  - tests/turn_raw_test.cpp:358-376 and 667: `context-budget-effective-v1` records.

  P1 says "the existing record expectation changes", but no existing test checks the whole
  `provider.request` record (coding_test.cpp:755-765 and 1061-1068 read only `attempt`,
  `retry_group` and `ordinal`). That one is a new assertion.
- **Consequence:**
  - The must-fail-on-old-code record misses about ten changed expectations.
  - Several of them use native `ContextStore::edit` as a fixture step, not as the claim under test.
    Whether native and operator callers stage too (§8.2 Idle publishes operator proposals at once)
    decides how each one changes, and the plan does not say.
  - The gate will surface the failures one by one, mid-unit.
- **Direction:** before S3b's red phase, grep every caller of `edit(`, `manage(`, `restore(`,
  `arco.edit`, `blackbird.edit` and the v1 policy fields. Classify each as either a claim whose
  expectation changes (record its old-code failure) or a fixture moved to an explicit publishing
  path. State the native and operator staging rule in S3b's section.

### M4. P2 conflicts with the plan's own must-fail rule, and its regression set is incomplete

- **Plan section:** §2.1 step 1, P2 red tests.
- **Evidence:** §2.1 says "A test that passes on the old code is replaced". P2 is a
  behaviour-preserving move whose tests must pass unchanged. Its regression table compares frozen
  old functions with new profile-driven ones, so on the old code it cannot even link. Tests that
  cover the migrated rules but are not named: tests/openrouter_test.cpp,
  tests/responses_provider_test.cpp, tests/openai_auth_test.cpp (`session-id`,
  `prompt_cache_key`). The frozen copy of the old decisions also contradicts 5st, which is meant to
  change those decisions on the same files.
- **Consequence:** either the implementer contorts tests to "fail" on the old code (ceremony), or
  the rule is quietly ignored. When 5st lands, the frozen table blocks intended changes or gets
  edited into a tautology.
- **Direction:** say that rule 5 applies to changed behaviour, and that P2's oracle is the
  regression table plus the named existing tests. Generate the table over the full product of
  known providers, routes, models, efforts, statuses and error codes, not a chosen list. Retire the
  table in the 5st unit.

### M5. A1's main oracle compares against the code under test

- **Plan section:** A1 red tests.
- **Evidence:** "the step input equals the builder's transformed main input plus the brief, byte
  for byte". The builder is the new code under test. The real claim, from design §13.1 and recheck
  B10, is that the step shares the main request's cached prefix. Per the design that means the same
  items and cache breakpoints, and the same tools under `allowed_tools`. Also "(fails today if the
  step reuses `request()`)" is not an old-code failure: the step does not exist today.
- **Consequence:** a builder bug that shapes both requests the same way passes. So does a committer
  that adds items to the main request but not the step, or the reverse.
- **Direction:** capture both requests at the mock provider's wire. Require the step's input up to
  the brief to equal the following main request's input before its operational state, after the
  profile transform. Require the same breakpoint positions, and the same `tools` under
  `allowed_tools`. Keep the "no slot write" check. Drop the "fails today" wording for brand-new
  code.

### M6. The live checks and probes lack exact pass and fail criteria

- **Plan section:** A1 direct checks and exit; §4.2 outcome table; §4.4; S3a resources.
- **Evidence:**
  - A1's live exit is "at least one settled cut and no provider refusal". It does not check that
    the model's choice was honoured, whether archived units were fetched back, the cached tokens
    after the cut, or whether the task finished.
  - Under `retain_current_turn` the C5 task can settle only in its continuation turn. The plan does
    not say this.
  - C5's own first reading was wrong: the stdout grep missed two `context_view` calls (C5_LIVE.md,
    Correction). Yet A1's live run comes before ib8.1's reader exists.
  - In rd7.2, "2 refused or degraded" leaves "degraded" undefined. "5 passes all four checks" names
    no four checks: §7.5 lists five, one of them Anthropic-only. Each case runs once (n = 1). "The
    model continues the task" over three requests decides `enabled = true` for production.
  - The probes build their cut requests by hand in Lua (§4 preamble). Nothing ties the probed bytes
    to the bytes S3a and the adapters will emit.
  - S3a's Paper and MiMo placement check appears only in the §5.1 resource table and §4.4. S3a has
    no production writer and lists no live check or criterion.
- **Consequence:** probes and live runs can "pass" on impressions. A profile row can be enabled from
  one sample, or from wire bytes the product never sends.
- **Direction:**
  - Write each criterion as a reading from the audit or raw lanes: status, the exact error, whether
    reasoning items are present, cached tokens, and whether the next response contains a tool call.
  - Run the behavioural cases (5, 6, 7) three times and enable a row only if all pass.
  - Save each probe's request body as a fixture that S3a's export must reproduce byte for byte.
  - Give the placement check a procedure (a probe program) and a criterion, or move it to the
    probes.

### M7. Kill criteria that cannot be observed, and missing risks

- **Plan section:** §8.
- **Evidence:**
  - "The closing reply misleads models" is to be shown "in rd7.3 or A1's live run". A1's run is on
    luna, where `turn.close = none` (design §7.2), so the closing reply never fires there. rd7.3
    needs an account that does not exist.
  - "C4 fires routinely" is to be measured "in trials". The trials do not require a1v.6 (§7.1), so
    no live overflow is classified and recovery cannot fire.
  - The thresholds are not observable: "nearly every review", "within a few rounds", "act as if they
    saw nothing".
  - Missing risks:
    1. The cut arms lower task success (harm without any fetch-back), the outcome ib8 says matters
       most.
    2. Anthropic-only machinery ships with mock evidence only, and no live user or check exists:
       the only retaining profile (so E1's 2o9 path, retaining-profile views and sweeps), the only
       `reply_request` and `drop_after_change` profile, P3 and K1's Anthropic plan.
    3. Subscription quota or rate limits run out mid-batch.
    4. Trials on the laptop measure wall time while gates and other lanes load the machine. §5.3
       stops trials from starting during a gate, but not a gate from starting during a trial.
- **Consequence:** two of the nine responses can never trigger. The most important trial risk has
  no response, and a large mock-only surface is not weighed.
- **Direction:**
  - Replace the closing-reply source with a mock-only acknowledgement and an explicit "unobserved
    until rd7.3" status.
  - For C4, count the terminal provider overflow refusals per route.
  - Add numbers: decline rate of at least 90% over at least 10 reviews; more than 50% of archived
    units fetched back within 5 rounds; for view rows, the next `context_manage` call names a handle
    shown only in the rows.
  - Add a harm criterion: success rate in a cut arm below baseline over matched pairs, then stop
    that arm on that route.
  - Decide with the operator whether the Anthropic-only paths are built now or deferred.
  - Trials hold the hook lane, or run on the fleet, when wall time is a measure.

### M8. The trial batch has no stated question and is underpowered

- **Plan section:** §7.2.
- **Evidence:**
  - The batch is 5 settings × 3 providers × 5 tasks × 3 repeats with about ten measures and no
    primary question or outcome.
  - Three repeats against a long-task failure rate of 5/9 on Paper and MiMo for non-CLM reasons (H2)
    leaves cells of zero to two usable runs.
  - Nothing randomises or interleaves run order across arms. Provider drift over a multi-day batch
    therefore confounds arm with time.
  - Dropping the Jev cell on medium tasks unbalances the design.
  - "Task outcome" has no oracle, and AGENTS.md says "Evals remain deferred until discussion".
  - ib8 requires "correlation kept distinct from causation". The plan says only that summaries are
    derived by the tertiary reader. Fetch-back against task failure, for example, is observational
    within an arm.
- **Consequence:** about 200 runs and 400M tokens can produce comparisons too noisy to decide
  anything the risk table names. The operator's "lighter on our feet" steering
  (assessments-light-on-feet memory, said of corpus work) points the same way.
- **Direction:** make the first batch answer one question per stated decision. For example: on luna
  with the boundary enabled, does the step settle cuts whose units are not fetched back, at what
  cached-input cost, without lowering completion against baseline? Use matched pairs (same task,
  adjacent time slots, alternating arm order), enough repeats to see the named effect, a stated
  rule for infrastructure failures, and a task-outcome oracle agreed with the operator.

### M9. Arm configuration and provenance are not checked per run

- **Plan section:** §7.2; E1 policy mapping; `run-one`.
- **Evidence:**
  - The plan never maps the arms (off, inform, inform + nudge, step, step + Jev) to policy v2
    fields.
  - The tooling sets the policy through `PRELUDE`, and `run-one` continues when the prelude fails:
    `launch p "$PRELUDE" || echo "prelude_exit=$?"`.
  - After E1, a v1-style prelude like C5's (`context_budget` with `trigger_bytes` and
    `target_bytes`) is either refused, so the run proceeds on the default 384/432 KiB, or mapped.
  - `run-one` defaults `CHECKOUT` to `context/cpp26-toolchain`. `meta.txt`'s `checkout_commit` and
    the launcher script come from that tree, not from the build. Profiles read from the resource root
    at run time are covered by neither `blackbird_sha256` nor `checkout_commit`.
- **Consequence:** a run can count toward an arm it did not run under, and its "matched source"
  metadata can name the wrong commit and the wrong profile set.
- **Direction:**
  - The tertiary reader admits a run to an arm only when the policy record and
    `provider-profile-set-v1` in force match the arm's declaration.
  - A failed prelude fails the run.
  - Batches set `BLACKBIRD_CHECKOUT` to the program branch worktree and record the profile digest.

### M10. Mapping a v1 budget policy to v2 can produce an invalid policy

- **Plan section:** E1 files, §6 row V1 → E1.
- **Evidence:** v1 validation requires only `target < trigger` (src/coding.cpp:2144-2164). E1 maps
  `trigger_bytes` to `nudge_bytes` and takes the default `direct_bytes` of 432 KiB. A v1 trigger
  above 432 KiB yields `nudge > direct`. The plan does not say whether v2 validation refuses that,
  or what happens to a policy record that fails validation at open.
- **Consequence:** depending on the implementation, an upgraded session either loses its policy
  silently, refuses to open, or gets a direct level below its nudge.
- **Direction:** specify the mapping for every v1 value (for example `direct = max(default,
  nudge)`), and add the case to E1's red tests.

## Low

- **L1. Linux runs (§2.2, §5.1).** K1 changes adapters (src/anthropic_provider.cpp usage mapping,
  src/openrouter.cpp) but has no Linux run. S0 changes clean-stop persistence but has none.
- **L2. Critical path (§1.3, §7.1).** ib8.1 depends on K1 and R1, and §7.1 requires ib8.1. K1 is
  therefore on the path to the first batch, although §7.1 says it is "not required for OpenAI".
- **L3. Raw action codes.** S1 adds the raw action codes for `cut` and `repair` (its files list
  src/raw_context.cpp), but raw_context_test changes, and the raw reader's knowledge of the new
  codes, are listed only under S3b.
- **L4. ib8.1 checks only a scripted mock.** Also run the reader over the A1 live session and the
  probe sessions. They are real, and they already exist by then.
- **L5. Earlier archives are not listed after upgrade.** Sessions upgraded past S3b keep their
  a1v-era `managed` archives, but `context_view {scope: "archived"}` lists only cut frames (design
  §5.5). Units archived before the upgrade drop out of that listing. State this, or list legacy
  archives too.
- **L6. Ceremony.**
  - The bounded fuzz target over `apply` uses the same generated frames as the model oracle, with a
    weaker implicit oracle. Fuzzing the decode of `cut` frames from journal bytes would add
    evidence instead.
  - ib8.1's "a removed field fails it" reads as a test of the test. State it as "the reader reports
    the missing signal by name".
- **L7. Vacuous "fails today" claims.** §2.1's blanket rule pushes brand-new code into claims that
  are not old-code failures: A1 (M7 note), R1 (`busy`), P3 ("no such item kind"). The doctrine's
  rule is for changed behaviour; say so.
- **L8. Integration point 1 (§5.4).** It claims "no behaviour change for the model", but S2 changes
  what is selectable and how rounds group, and H1 changes behaviour in installed generations.
- **L9. Pending proposals with `select`.** Proposals staged under S3b that use `select` are carried
  in the `pending` section, but V1 removes `select` from `context_manage`. Their re-expansion after
  V1 is unspecified.

## Sound parts

- The oracle tiers fit the claims in most units:
  - S1: a model oracle plus a metamorphic one ("live = snapshot plus suffix = whole playback").
  - S2: a model oracle for `expand`, and a property oracle for protection.
  - S3b: a specification row for every line of §8.4 and §8.5, plus a pending-state model oracle
    checked live and after playback.
  - E1: the 100-request check on retaining profiles.
  - P2: a regression oracle, the right tier for a move that preserves behaviour.
- S1's corruption tests name a real failure today with file:line (src/context.cpp:367-376,
  src/coding.cpp:2654-2655).
- S3a ships with no production call site, so no session holds a cut composed before its timing
  rules existed. That makes the S2 → S3a and S3a → S3b boundaries clean.
- Probes block enabling, not building. Without an account the Anthropic fallback is stated
  honestly, and the plan does not guess outcomes.
- The plan follows bounded hardening: two layers, no third layer, and a rerun after a fix counts as
  layer 2. It runs one gate at a time, with the 9h5 flake list. Mutation stays off the laptop.
  Resources and token spend are declared per unit.
- Legacy `managed` and `edit` frames keep replaying, and the loss of a proposal staged by an older
  binary matches today's behaviour.
