# Plan review: feasibility, sequencing, dependencies, integration and resources

Reviewer: independent adversarial agent, 2026-10-20. Subject: `docs/CONTEXT_LIFECYCLE_PLAN.md` (untracked, in
the `clm-a1v` worktree at `4152691`), against the design `docs/CONTEXT_LIFECYCLE.md` revision 3. I did not
re-review the design. Everything below was checked read-only, using git, grep, file reads and `bd show`.

Severities: 6 high, 9 medium, 7 low.

---

## High

### H1. The lanes are not file-disjoint, and merges into the program branch are never gated

- **Section.** §1.2 says the lanes "share no invariant and touch different files". §5.2 says each unit merges
  into the program branch with a merge commit.
- **Evidence.** These are the plan's own file lists:
  - `src/coding.cpp` is edited by P1 and P2 (Profiles lane), T1 (Shaping lane), and S2, S3b, V1, E1, A1 and R1
    (Spine lane). Some of these are the same statements:
    - S2 and T1 both edit the `drain_workflows` continuation append at src/coding.cpp:4281-4288. S2 adds class
      `result`; T1 shapes the result. I confirmed it is one `context_.append(...)` call.
    - P2 edits the retry decision at 4727-4737. That sits inside the attempt loop at 4538-4784, which R1
      restructures to unwind and return to G.
    - P1 (`provider.request` record, 4619-4630) and P2 (4512-4514) edit `CodingEngine::request`, which S3b, V1,
      E1, A1 and R1 rewrite (the settle point G, the builder/committer split, the delivery slot).
  - `src/retained_state.cpp` `current_programs` (2983-2990): P1 adds two programs, and S2 adds
    `context-pins-v1`. These are different lanes and the same `else if` chain.
  - `src/anthropic_provider.cpp`: P2 (289-299, 486-510), P3 (`export_anthropic_request`, `MessageStream`) and
    K1 (237-266) in Profiles; S3b (render at 346-351) and R1 (`finish`, 202-209) in Spine.
  - `src/tools.cpp`: T1 (Shaping); S3b, V1 and R1 (Spine).
  - `src/main.cpp`: S0, S1, V1 and U1 (Spine); P1 and P2 (Profiles).
  - Every lane also edits `CMakeLists.txt`, `tests/coding_test.cpp` and `JOURNAL.md`. §2.1 step 1 records every
    red failure in the journal. The a1v master merge already conflicted on `JOURNAL.md` and `coding_test.cpp`
    (JOURNAL, "merge master into the a1v branch").
- **Ungated merges.** The repository has only `.githooks/pre-commit`. `core.hooksPath` is
  `/Users/patrickbeam/projects/blackbird/.githooks`, and there is no `pre-merge-commit` hook. A clean `git merge`
  therefore runs no gate. Conflict resolutions in `coding.cpp` reach the program branch unchecked. The next unit
  branches from that unchecked tree, and its first gate then blames the wrong change.
- **Consequence.**
  - There will be repeated textual and semantic conflicts in the busiest file.
  - The "line numbers at `ae81fbe`" anchors stop being true after the first cross-lane merge.
  - The parallelism is mostly an illusion: the work is serial per file, with merge debt added on top.
- **Direction.**
  - Give each unit an explicit file and function ownership table.
  - Order the `coding.cpp` edits. P1 and P2's `request()` and attempt-loop edits should land before S3b
    starts, or the spine should own them.
  - Move T1's `drain_workflows` change behind S2, or fold it into S2.
  - Rebase a lane's unit branch on the program branch before its first gate.
  - Merge with `--no-commit` and then commit through the hook, or run the gate on the merged tree before pushing.

### H2. The critical path leaves out R1, and A1's exit depends on a probe

- **Section.** §1.3, §1.2 ("R1 and K1 feed ib8.1 but run beside A1"), §1.4 and §4.2.
- **Evidence.**
  - The design §21 has R1 depend on "S3b, A1 (builder), P1". The plan's own graph draws `A1 ──► R1`.
  - ib8.1 depends on R1 (design §21; plan §1.2). Trial entry requires ib8.1 (§7.1).
  - So R1 cannot "run beside A1". The real path is `… → E1 → A1 → R1 → ib8.1 → trials`, and R1 is a full unit
    with 3 gates and a Linux run.
  - A1's exit (§3 A1) requires "the live run completes with at least one settled cut", and that live run waits
    for rd7.2 (§4.2: "A1's live run waits for it"). This contradicts §1.4 ("they never block building"). rd7.2,
    or its Paper fallback, is on the path to A1's exit.
  - Trial entry (§7.1) says "K1 is not required for OpenAI", but ib8.1 depends on K1 (design §21, plan §1.1). K1
    is therefore required transitively.
- **Consequence.** The schedule understates the path by one large unit and hides a probe dependency on it. Any
  date or ordering built on §1.3 is wrong.
- **Direction.**
  - Split the pure request builder, with the raw region nesting check from design §13.3, out of A1 as its own
    small first unit. R1 and the step can then proceed in parallel after it.
  - Rewrite §1.3 to include R1, K1 and the rd7.2 dependency of A1's exit.
  - Run rd7.2 early enough that it is off the path.

### H3. The trial signals (design §18) are unowned, and ib8.1's remedy reopens settled units

- **Section.** ib8.1 ("gaps fixed in their owning units") and §7.1.
- **Evidence.**
  - Design §18 requires several fields on `provider.request`: `chapter`, `chapter_ordinal`, `chapter_first` and
    `purpose` (main, closing, clm.step, clm.recovery).
  - It also requires `chapter` on the DecisionEvent continuation (src/coding.cpp:3363-3368) and the chapter
    revision in the assembly raw phase (4535-4537).
  - None of these appear in any unit's file list or red tests. A grep of the plan for `chapter_ordinal`,
    `chapter_first`, `DecisionEvent`, `3363` and `4535` finds nothing. Only `purpose: clm.step` appears (A1).
  - The plan finds the gaps only in ib8.1, at the very end, and then "fixes them in their owning units". By then
    those units have closed both hardening layers.
- **Consequence.** BOUNDED_HARDENING forbids reopening settled checks without new scope and forbids a third
  layer. So either the units reopen (doctrine violation, and new gates on S3b, A1 and R1) or the trials start
  without the join keys their analysis needs.
- **Direction.**
  - Assign each §18 row to its producing unit, with a red test in that unit:
    - `chapter*` to S3b;
    - `purpose` closing to S3b;
    - `purpose` clm.step to A1;
    - `purpose` clm.recovery to R1;
    - DecisionEvent and raw chapter to S3b.
  - Build ib8.1's tertiary reader skeleton early, right after S1, and extend it in each unit. ib8.1 then becomes
    a final run, not a discovery phase.

### H4. The "one gate at a time" control does not exist as described

- **Section.** §5.3 ("runs through `hook-lock` with `BLACKBIRD_HOOK_LANES=1`, and before taking the lane it waits
  until no other lane directory … is held").
- **Evidence.**
  - In `~/projects/blackbird_runs/bin/hook-lock`, with `LANES=1` the loop tries only lane 0
    (`/private/tmp/blackbird-hook.lock`). It never inspects `.lock.1` or `.lock.2`.
  - Other sessions use the default of 3 lanes (JOURNAL: "The hook lock now has three lanes"; the
    CAMPAIGN_DEFECTS_HANDOFF calls `hook-lock git commit` with no lane override). They can hold lanes 1 and 2
    while this program's gate runs.
  - Nothing enforces the wrapper. `.githooks/pre-commit` calls `scripts/rigor check debug` directly, and a plain
    `git commit` takes no lock at all.
  - `hook-lock` lives outside the repository, in `blackbird_runs/bin`, and is unversioned.
  - The control the plan relies on against bead 9h5 load flakes is therefore absent.
- **Consequence.**
  - Gates overlap with other sessions' gates, and 9h5's known flakes (`command_raw`, `provider_exchange_raw`,
    `tui_retained_result`, `terminal_custody_native`, `turn_raw`) recur.
  - Every flake costs a full gate rerun. A single test rerun does not unblock a hook-gated commit.
  - The host also has an unresolved process-table stall finding. A hook run had 53/264 failures, and
    `sysctl(KERN_PROC)` blocked for up to 585 s (campaign-defects `docs/CAMPAIGN_DEFECTS_HANDOFF.md`, "Host
    finding"). The plan does not mention it.
- **Direction.**
  - Either change `hook-lock` so that a lane count of 1 waits for every `lock*` directory, or state the real
    behaviour.
  - Say who changes the script (it is outside this repository) and requalify it with a direct overlap case.
  - Put the stall check from the handoff (monitor running, process monitors closed) into the plan's
    preconditions.

### H5. The OpenRouter stand-in has no account, and as written it cannot answer the OpenAI questions

- **Section.** §4.2 (routes, fallback, cost), K1 (live check) and §5.1 spend.
- **Evidence.**
  - The JOURNAL entry for 2026-10-15 (around line 8965) says: "Native auth status contains no OpenRouter
    accounts. OPENROUTER_API_KEY is absent; the known workspace .env has no matching OpenRouter variable."
    CURRENT_STATE.md agrees. On 2026-10-17 the JOURNAL says "Safe installed auth metadata still lists only the
    existing ready Paper account."
  - No run in `blackbird_runs` (2026-10-17-cpp26, 2026-10-19-clm, 2026-10-19-sol) used `provider=openrouter`.
    They used openai (ChatGPT), paper and mimotp only.
  - The plan says "no API key is configured" for api.openai.com, yet it treats OpenRouter as available. It
    budgets $5 for it and $3 more in K1.
  - **Fixture validity.** The single fixture is a luna run on the ChatGPT route. Its encrypted reasoning items
    come from `gpt-6-luna` under the ChatGPT account. Replaying them on OpenRouter `openai/gpt-6.1-sol` mixes
    model and organization. A refusal of cases 1, 2 and 10 there would say nothing about cuts.
  - **Route validity.** OpenRouter's Responses endpoint is OpenRouter's compatibility layer. Blackbird also
    forces `provider.require_parameters = true` (src/openrouter.cpp:68-72), so an unsupported `tool_choice` shape
    is refused by OpenRouter routing, not by OpenAI.
  - The outcome table still writes the results into "OpenAI" rows. Design §13.1 scopes `allowed_tools` to the
    "OpenAI api-key route". A pass or fail on OpenRouter cannot set that row.
- **Consequence.** The fallback silently fails when no key is found, or it produces wrongly attributed profile
  rows. Those rows ship as `probed`, and the trials then run on them.
- **Direction.**
  - Before rd7.2, ask the operator for an OpenAI API key or an OpenRouter key, and state which one exists.
  - Produce a fixture per route and model: luna on ChatGPT, and the stand-in model on its own route.
  - Write OpenRouter results only to `openrouter-responses.lua`, and leave the `openai-api` rows as `cited, probe
    pending` until a real API-key run.
  - Show in §5.1 that the $5 and $3 items are conditional on an account.

### H6. P2 collides with the operator's staged 5st repair and fixes its oracle at a stale point

- **Section.** P2, §1.4 ("This plan only makes P1 ready for it and does not schedule 5st"), §3 "a1v.6 and 5st"
  and §9.
- **Evidence.**
  - bead 5st is `in_progress`, and its comment of 2026-10-18 says "repair staged (uncommitted) in
    context/defect-5st".
  - That index holds 23 files and 1,977 insertions. It adds `src/provider_settings.cpp` (a per-provider settings
    table) and edits `src/main.cpp` (+53/-), `src/coding.cpp`, `src/anthropic_provider.cpp` and
    `src/provider_auth.cpp`.
  - P2 moves the same rules (`route_efforts` main.cpp:466-496, the engine effort set, portable effort,
    Anthropic effort and thinking) into profiles. P3 edits the same `provider_auth.cpp` header block.
  - P2's regression oracle freezes the old decisions "as they are at `ae81fbe`".
  - The handoff's next step 2 is to commit 5st and merge it into the integration branch, and the operator can do
    that at any time.
- **Consequence.**
  - If 5st lands first, P2's oracle preserves pre-5st behaviour, and P2 either reverts 5st's validated settings
    or conflicts across four files. 5st's `provider_settings.cpp` then becomes a second hard-coded rules table
    that P2 does not migrate.
  - If P2 lands first, 5st's staged work is invalidated and has to be rebuilt. That decides the content and cost
    of an operator-held phase, which the plan says it does not do (memory: "phase membership is the
    operator's").
- **Direction.**
  - Before P2 starts, ask the operator which order applies.
  - Until 5st's disposition is known, take the effort, launcher and Anthropic-thinking rows out of P2's scope:
    migrate only the cache, retry and request-state rows.
  - Fix the oracle at the master or program-branch commit that is current when P2 starts, not at `ae81fbe`.

---

## Medium

### M1. The S3a/S3b boundary splits an invariant: S3a needs S3b's turn state, render and Pending

- **Section.** S3a and S3b, and S1 ("staged-frame and `pending`-field playback (data only; S3b fills it)").
- **Evidence.**
  - S3a's `place_note` and `place_boundary`, and its note-placement test ("after tool results on Anthropic's
    rendering"), need the codecs' pure render and the wire turn state. The plan introduces both in S3b
    (`turn_state.cpp`; "the codecs expose their pure render").
  - S3a's `rebase_pending` and its test ("a pending proposal survives a rebind …") need the Pending
    representation that S3b defines ("`Pending` holds units and a basis reference").
  - S3a owns the `chapter` section, but its only writer is S3b's `start` frame (§6).
  - S1 plays back a `pending` field that only S3b fills, against design §21's rule: "no unit ships a field or
    behaviour that another unit fills … later".
  - Provider acceptance after a cut (S3a's invariant) depends on whether the turn is open, for example Anthropic
    thinking validity.
- **On "no production callers".** S3a shipping with no production callers is not dead weight in itself. It is
  tested through the real adapters, and S3b follows directly in the same lane. The defect is where the line is
  drawn: S3a cannot be completed or reviewed honestly without code that belongs to S3b. S3b will then reshape
  S3a's interfaces after S3a has finished both layers.
- **Consequence.** S3b reopens S3a's code, which amounts to a third layer, or S3a's tests use stand-ins that S3b
  replaces.
- **Direction.**
  - Move `turn_state` and the pure render exposure into S3a. They are pure functions of render and profile.
  - Move `rebase_pending` into S3b.
  - Drop the pending-field playback from S1, so that S3b adds playback with its section.

### M2. The gate count and checkpoints are internally inconsistent, red commits cannot pass the hook, and real attempt rates are higher

- **Section.** §5.1 and §5.3.
- **Evidence.**
  - The table sums to 16 units × 3, plus c3f 1, plus ib8.1 2: 51 gates, not "about 48".
  - §5.3 asks for a commit at four checkpoints per unit (red, green, layer 1, layer 2), but budgets three.
  - A "red tests recorded" commit fails `scripts/rigor check debug`, so the hook blocks it. Bypassing the hook is
    forbidden, and the text-only override does not apply to test sources.
  - **Measured duration.** The passing locked gates were `q4n-commit4.log`, 07:48:43 to 07:58:30 (9 min 47 s,
    375.63 s of CTest), and `47k-commit2.log`, 10 min (381.17 s). The 10-12 minute figure holds for a pass.
  - **Measured attempts.**
    - q4n needed 4 gate attempts: 252, 263, 264 and then 265/265.
    - 47k needed 2, the first ended by SIGTERM.
    - 5st's hook failed 4 times (handoff).
  - At about 2.5 attempts per landed commit, the program needs about 120 gate attempts, or 20+ hours, serialized
    on one laptop.
  - The ASan full runs (§2.2), the bounded fuzz runs, and builds in three lane worktrees are also heavy. 9h5 names
    "gates or other heavy work" overlapping, but the lock covers only commits.
- **Consequence.** Three lanes compete for one serialized gate, so lane parallelism cannot shorten the schedule.
  The time budget is understated by about 2×.
- **Direction.**
  - Record red failures in the journal (text-only) and not as commits.
  - Budget attempts, not passes.
  - Hold the lane for ASan, fuzz and full builds too, or run those on Neuroses.
  - Fix the arithmetic.

### M3. `profiles/` is not shipped by the installer or the Linux check, and a test fixture lives outside the repository

- **Section.** P1 (files), V1 (C5 fixture), §2.2.
- **Evidence.**
  - `scripts/install-blackbird` copies only `programs` and `assets` into `share/blackbird`
    (`cp -R "$blackbird_source/programs" "$blackbird_source/assets"`).
  - `scripts/check-linux` ships an explicit path list (`include, src, tests, programs, …`) with no `profiles/`.
  - P1's file list touches neither script, nor `scripts/check-install-blackbird`.
  - V1's fixture is taken "from `~/projects/blackbird_runs/2026-10-19-clm/c5-luna`". That is outside the
    repository, and the run is 101 MB.
- **Consequence.**
  - Installed generations and every Linux run either fail profile tests or quietly select `conservative`.
    `conservative` is a valid profile, so this hides the failure.
  - The V1 test cannot run on Neuroses or a fresh clone.
- **Direction.**
  - Add `profiles/` to `install-blackbird`, `check-install-blackbird` and `check-linux` in P1.
  - Make a missing shipped profile directory a refusal, not a fallback.
  - Extract the 57-item input into `tests/fixtures/` within V1.

### M4. A2's required checks do not exist, and its prerequisite has no owner

- **Section.** A2, §2.2 ("TSan for A2"), §5.1 ("2 (TSan)").
- **Evidence.**
  - `scripts/check-linux --profiles` accepts only `debug`, `release` and `asan`. There is no Linux TSan through
    that script.
  - Mac TSan is not qualified. Bead 0zi says 9 cases fail identically on the C++23 and C++26 baselines, and 0zi
    sits in campaign-defects (handoff: "Waits for C and for a host without process-table stalls").
  - The design §21 prerequisite, the codex auth-file race fix (C6_DESIGN: `openai.cpp:226-308`), has no bead. A
    search of all beads finds none.
  - The exit "TSan clean on both hosts" cannot be met.
- **Consequence.** A2 cannot exit, or it is declared clean against a broken profile.
- **Direction.**
  - Make A2's entry explicit: 0zi resolved by the operator's phase, a Linux TSan profile added to `check-linux`
    as a tooling change (requalified), and a bead for the auth race.
  - Otherwise state that A2 is blocked.

### M5. The bounded fuzz runs depend on the unmerged e5g repair and the laptop fuzz rule

- **Section.** P1 (direct checks, exit), S1 (fuzz over `apply`), §8 (profile reader risk).
- **Evidence.**
  - `scripts/rigor check fuzz` on this Mac aborts linking with Anaconda's `ld` unless PATH puts `/usr/bin`
    first (bead e5g). The repair is "staged, uncommitted" in `context/defect-e5g`, inside campaign-defects.
  - AGENTS.md: "Keep fuzzing off the laptop and out of measured host windows". That rule was written in the AFL
    context, but it is unqualified.
- **Consequence.** P1's exit ("the fuzzer ran its bounded budget with no crash") is blocked by an operator-held
  repair, or it runs against the rule.
- **Direction.** Run the bounded libFuzzer targets on Neuroses, or state the explicit PATH workaround and get
  the operator's agreement that a 1,000-input bounded libFuzzer run is not "fuzzing" in the AGENTS sense.

### M6. The trial matrix is not grounded in the measured sizes, and the Jev cell silently degrades

- **Section.** §7.2.
- **Evidence.** From REQUEST_SIZES.md §2 (max input KiB):

  | Task | luna | mimo | paper |
  |---|---|---|---|
  | t03 | 769.8 | 412.2 | 287.3 |
  | t05 | 376.0 | 36.1 | 1.4 (r-t05), 467.8 (r2-t05) |
  | t06 | 378.3 | 1.0 | 1.0 (r-t06), 211.3 (r2-t06) |

  - Only 3 of 9 task × provider cells passed 384 KiB of input. Only two (t03-luna and r2-t05-paper) passed the
    432 KiB direction level at which the step fires (design §13.1). t06 never passed 384 KiB.
  - Several cells failed after 1-5 requests, so repeats are not alike.
  - The matrix names no policy levels, so the default levels apply and most "step" and "step + Jev" cells never
    trigger.
  - The roughly 2M tokens per run average is plausible: r-t03-luna is about 3.6M and r2-t05-paper about 7M.
    However, 70 luna runs at about 2M is about 140M tokens of ChatGPT subscription quota, and the plan does not
    assess that.
  - **Jev cell.** `run-one` launches with `env -i`. `scripts/blackbird` sets `BLACKBIRD_JEV_ENV_FILE` only from
    `$blackbird_root/../.env`. For a worktree checkout (`context/<x>`) that is
    `~/projects/blackbird/context/.env`, which does not exist. Only `~/projects/.env` exists. The Jev column
    would be dropped silently ("a failed call drops the column").
- **Consequence.** Much of the roughly 200 runs measures nothing about the step, and the Jev arm is not what it
  claims to be.
- **Direction.**
  - Set the policy levels per task from the measured growth, as C5 did with 60 KiB.
  - Run one repeat as a pilot before committing to 3.
  - Pass `BLACKBIRD_JEV_ENV_FILE` explicitly through `run-one` and record it in `meta.txt`.
  - Ask the operator to confirm the ChatGPT quota.

### M7. There is no rule for staying current with master, and integration proposal 1 misstates its effect

- **Section.** §5.2 and §5.4.
- **Evidence.**
  - Origin master gained 9 commits on 2026-10-19 alone, from several sessions.
  - Work pending for master:
    - work/4by-2026-10-20 edits `CodingEngine::call` in `coding.cpp`, and the same edit is uncommitted in this
      worktree;
    - the C++26 toolchain (9xx closed, "Master integration awaits operator go"), which would change the
      `CMAKE_CXX_STANDARD 23` profile that `check-linux` asserts;
    - 5st, 3o2 and e5g.
  - The plan never merges master into `work/clm-rd7`.
  - Proposal 1 claims "no behaviour change for the model". It carries the a1v branch: the context budget enabled
    by default (`1f28945`) and shaped observations by default (`870ec8c`). S2 also changes what is selectable
    (`operator.beads` and repaired rounds).
- **Consequence.** There is one very large final integration conflict concentrated in `coding.cpp`, and the
  operator decides proposal 1 on an inaccurate summary.
- **Direction.**
  - Merge master into the program branch, gated, at each proposal point and whenever master changes a hot file.
  - Name the model-visible changes in each proposal.

### M8. S0 drops session switch, which the design names as a clean stop

- **Section.** S0 (files and functions, exit "Every clean path snapshots").
- **Evidence.**
  - Design §4.4 and §21 S0 list "quit, exit, session switch and shutdown".
  - `src/main.cpp` switches sessions at `/resume` (993) and `/new` (1059). `/restart` also exists.
  - S0 lists only `/quit`/`/exit` (1808-1811), EOF, signals and the TUI loop.
- **Consequence.** S0's invariant is incomplete, and S1's resume table misclassifies the switched-away session
  as an unclean stop.
- **Direction.** Add the `/new`, `/resume` and `/restart` paths to S0's file list and to `clean_stop_test.cpp`.

### M9. The probe fixture does not match the C5 evidence it cites

- **Section.** §4.1 and §4.2.
- **Evidence.**
  - C5 (C5_LIVE.md; `c5-luna/meta.txt`) ran 19 requests in two launches, finished with one continuation (105 s),
    and reached 787 KB of input. That is far above "about 40k input tokens at the end" and well short of "about
    30 rounds".
  - Since C5, `context_view` and the observation defaults have changed (47k, 870ec8c), so a rerun will have a
    different shape.
  - Continuation requests in a scratch session issue real tool calls. The plan does not say the probe session
    runs in the fixture's seed workspace, so "whether the model continues the task" is confounded.
  - "Both probes reuse" the fixture, yet §4.1 also says the Anthropic probe has its own fixture.
- **Consequence.** Case 8 (archive rounds 2-25) may have no rounds to cut, and the continuation signal may be
  noise.
- **Direction.**
  - Specify the fixture task and continuation count that reach 30 rounds, and accept the run on rounds and
    tokens.
  - Run probes in a copy of the fixture's seed workspace.

---

## Low

- **L1. A path collision.** §2.1 puts sub-plans in `context/clm-rd7/<unit>.md`. §5.2 makes `context/clm-rd7` the
  Spine worktree. The notes would land as untracked files in a checkout root and be lost when the lease returns
  through `worktree remove --force`. Use `context/clm-rd7-notes/`.
- **L2. Internal inconsistencies.**
  - The §1.1 graph draws K1 from S3b; the text and design say S3a.
  - The §2.2 Linux list omits P2 and A1, which have Linux runs in §5.1. A1's direct checks list none.
  - S3a's Paper/MiMo live check (0.2M tokens) is in §5.1 but missing from S3a's direct checks.
  - P2's exit ("no hard-coded rule of §15.5 remains") contradicts the exclusion of stop reasons and of OpenRouter
    `session_id`.
  - `/repair` is in both S1 and U1.
  - R1 says error classes are "set by the profile's error rules (P1)", but P1 builds no rule application.
    Design §14 says nls.16 applies them.
- **L3. Spend arithmetic.**
  - `gpt-6.1-sol` has a $2.50 cache-write rate (provider-contracts.md:87-88). The worst case is 1.2M × $2.50 =
    $3.00 input, not $2.40, plus the OpenRouter fee.
  - Paper spend in S3a and A1 is unpriced, so "under $15" is not a closed figure.
  - Still within the $5 line.
- **L4. The kill criteria step outside the two layers.** "stop S1; redesign `apply` …" after review fixes would
  be work beyond layer 2. Per BOUNDED_HARDENING it should keep the candidate inactive and go to the operator.
- **L5. The raw region nesting check runs too late.** It is a cheap, read-only open design point (§13.3,
  "unconfirmed") and it shapes the builder that R1 also uses. Do it now, not inside A1.
- **L6. Program-branch binaries are forward-only after S3b.** No program-branch build may be installed (via
  `install-blackbird`) or used on the operator's long-lived sessions before integration, since master and
  installed binaries then refuse `op = "cut"`. Say so.
- **L7. `run-one` records the wrong checkout by default.** It records `checkout_commit` from
  `BLACKBIRD_CHECKOUT`, which defaults to `context/cpp26-toolchain`. Trials must set it to the program-branch
  checkout, or the "matched source" metadata is wrong. C5 set it correctly; the plan should say so.

---

## Sound parts

- **Spine order.** S0 → S1 → S2 → S3a → S3b → V1 → E1 → A1 follows design §21's dependencies. P1 and T1 start
  early. The probes do not gate building.
- **Hardening.** JSMNTL is mapped to exactly two layers, with no certification step, as BOUNDED_HARDENING
  requires.
- **Git rules.** The branch, push, no-force-push and no-implied-master-merge rules match AGENTS.md (operator
  2026-10-06). One lease per lane rather than per unit is right.
- **Gate duration.** "About 10-12 minutes" for a passing gate matches the measured `q4n-commit4` (9 min 47 s)
  and `47k-commit2` (about 10 min) runs. The JOURNAL's 375-460 s Debug figures also agree.
- **Neuroses.** `scripts/check-linux` exists and asserts Clang 23.1.4 and libc++ 23.1.4 at `-std=c++23`, as
  described. The JOURNAL shows recent Linux use, and the ssh config has `Host neuroses`.
- **Accounts.**
  - The ChatGPT route (`provider=openai`, `CODEX_HOME`), Paper and MiMo token-plan accounts are real and were used
    in the 2026-10-17 and 2026-10-19 runs.
  - `blackbird.request` options merge any key into the request (src/coding.cpp:4407-4416), so a probe can set
    `tool_choice`.
- **Line numbers.** The line numbers "at `ae81fbe`" are still valid at `4152691`. Since then only
  `tui_inspector` changed under src, include and tests.
- **Downgrade.** The "downgrade works until S3b" row is correct. `load_slot` returns `ignored()` for version ≠ 1
  (src/saved_state.cpp:146-153).
- **Trial arithmetic.** 225 → about 207 runs, 400M input tokens and the $290 scale figure are internally
  consistent. Every existing test file the plan names exists.
