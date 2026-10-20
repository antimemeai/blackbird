# Plan recheck: CONTEXT_LIFECYCLE_PLAN revision 2

Layer 2 under docs/BOUNDED_HARDENING.md. This is the single recheck of the revised plan against the
two plan reviews (`sequencing_resources.md` = SR, `tests_migration_risk.md` = TM). Owner directions are
settled and are not reopened here. It was read-only: I read the plan, the design (§7, §12, §13, §18,
§20, §21), `scripts/install-blackbird`, `scripts/check-install-blackbird`, `scripts/check-linux`,
`.githooks/pre-commit`, `~/projects/blackbird_runs/bin/{run-one,queue-worker,hook-lock,proc-stall-watch}`,
and the `src/main.cpp` exit paths. I also spot-checked the cited lines in `src/coding.cpp`,
`src/context.cpp`, `src/application.cpp`, the signal handling in `src/`, and `ApplicationChannel`. I
built and ran nothing.

## A. Dispositions

| Finding | Verdict | Reason |
|---|---|---|
| SR-H1 lanes share files; ungated merges | held | §1.1 puts every unit that touches one of the five shared files in one serial lane. §5.2 merges with `--no-ff --no-commit` and then commits through the hook. `git commit` runs `pre-commit` when it concludes a merge, and the hook's unstaged-diff check passes after `--no-commit`. T1 comes after S2. |
| SR-H2 critical path omits R1; A1 exit waits on a probe | held | R1 is on the path, B is split out, and the A1 live check is separate from A1's exit. However, the revision adds a new probe dependency to S3a's exit (B1). |
| SR-H3 trial signals unowned | held | Q1 owns `chapter*`, `purpose` main and closing, DecisionEvent and the raw chapter. A1 owns and tests `purpose = clm.step`. R1 names `clm.recovery` in its files but has no red test for it (B, low). |
| SR-H4 the one-gate control does not exist | partially held | The host rule replaces `hook-lock`, as directed. But the pre-gate `pgrep` is only a point-in-time check, and the "after every gate, check `pgrep` against the gate's time window" step cannot observe anything: `pgrep` sees only processes alive now. So overlaps are never recorded, and every 9h5 load flake is charged to the unit as a defect (B5). |
| SR-H5 OpenRouter has no account | held | ChatGPT route only. Probed rows go only into `openai-codex.lua`; the `openai-api` rows stay cited or conservative. K1's OpenRouter part is mock-only; §9 says so. |
| SR-H6 P2 versus 5st | held (order, oracle) | The order is P1 → 5st → P2, and P2's oracle is taken at its own base. How 5st gets onto a P1 that has not been integrated is still unstated (B2). |
| SR-M1 the S3a/S3b split | held | `turn_state` and the pure render move to S3a. `rebase_pending` and the `pending` playback move to S3b. S1 no longer plays back `pending`. |
| SR-M2 gate arithmetic, red commits | held | The totals agree: 19 × 11 + 6 = 215 attempts, about 36 h, and 18 Linux runs, which match the table. Red tests land with green code. Not counted: the ib8.1 reader lane, and the commits that apply probe rows (B4, B13). |
| SR-M3 profiles not shipped; fixture outside the repository | partially held | The script edits are real: `install-blackbird` lines 27 and 83 and `check-linux` line 25 are correct anchors, and V1's fixture generator is in place. But the startup refusal and the three-location test are assigned to P1a, which has no binary entry point (B3). |
| SR-M4 A2's checks | held | A2 is blocked with explicit entry conditions. One wording point: the entry requires the `tsan` profile that §2.2 says A2 itself adds (low). |
| SR-M5 fuzz runs | held | Fuzzing runs on Neuroses only, through a `fuzz` profile in `check-linux`, requalified with a planted crash. |
| SR-M6 matrix ungrounded; Jev cell | held | Levels come from the pilot, `BLACKBIRD_JEV_ENV_FILE` is passed explicitly, and the quota is confirmed. The pilot's ordering is circular (B, low). |
| SR-M7 staying current with master | held | Master is merged at each unit boundary through the gate, and each proposal names its model-visible changes. |
| SR-M8 S0 drops session switch | held | `/new` (checkpoint at 1070), `/resume` (1021), `/restart`, `--once` (1607-1611) and the station return (1603-1604) are listed. All interactive returns go through `finish_interactive`. However, the revision adds a SIGTERM claim that is false (B6). |
| SR-M9 fixture does not match C5 | held | The task is named and acceptance is set on rounds and tokens. Probes run in a copy of the seed workspace under the sandbox. The cut point inside the fixture is not specified (B9). |
| TM-H1 installed generations get no profiles | partially held | Shipping is held. The visible refusal and the installed-generation test are not real as placed (B3). |
| TM-H2 the first batch cannot exercise cuts | partially held | On luna: the question requires rd7.2 case 5 or 6 to pass, and the pilot gate demands mid-run cuts, so this part holds. On Paper, the fallback's turn-rule probe is mis-defined and cannot show current-turn settlement (B7). |
| TM-H3 S0 misses exits; red test passes on old code | held | The oracle reads from disk before any reopen, through `load_saved_state`. Sessions stay below the 64-record and 4 MiB thresholds (context.cpp:698-706). A job and a participant are active at exit. The recovery event goes on `ApplicationChannel::log` (retained_events.hpp:19). |
| TM-H4 no removal or prefix oracle | held | S3a has a removal model oracle with an independent reference for §5.6 and §6.2, and a byte-identity property for the bytes before `p` through the real adapters. |
| TM-M1 prefix and rebind cuts from more units | partially held | The §6.1 rows, the behavioural-field digest, the `lua` description and case 11 are in place. But §6.2's profile pinning relies on "an installed generation", and `run-one` runs any `BUILD_DIR` (B8). |
| TM-M2 downgrade overstated | held | §6.3 is corrected. The recovery event is on the log channel. The format-level record refuses before `recover_coding_session`. Program builds never touch real sessions. |
| TM-M3 changed-test lists incomplete | held | Every cited test is listed. The staging rule for native and operator callers is stated, with a pre-work grep. P1b's record assertion is now called new. |
| TM-M4 P2 and the must-fail rule | held | §2.3 limits the rule to changed behaviour. The regression table covers the full product at P2's base, plus the named tests. |
| TM-M5 A1's oracle tests itself | partially held | The comparison is now at the wire. But "the following main request" differs from the step's input whenever the step's own proposal settles, and "the same breakpoint positions" contradicts design §13.1 for `palette_only`, which is the only restriction shipping (B10). |
| TM-M6 probes and live checks lack criteria | partially held | The criteria are now audit and raw readings, behavioural cases run 3/3, and the placement check has a probe. But the probe-body fixture rule cannot be met for half the cases (B1). The "continues" reading is uncontrolled (B9). The A1 live pass may be unobservable (B11). |
| TM-M7 kill criteria cannot trigger; missing risks | held | Thresholds are numeric. The closing reply is "unobserved until rd7.3". C4 is counted by the reader. Harm, quota, Anthropic-unobserved and gate-versus-trial rows are added. |
| TM-M8 no question; underpowered | partially held | One question, pairing and randomized order are held. The power argument is wrong, and the arm contrast is confounded (B12). |
| TM-M9 arm configuration and provenance | held | The prelude fails closed, a run is admitted to an arm only on its recorded policy and profile set, and `BLACKBIRD_CHECKOUT` and the profile digest go into `meta.txt`. |
| TM-M10 the v1 mapping can be invalid | held | The mapping is total: `direct` is 432 KiB when that is above `nudge`, and otherwise `nudge + 64 KiB`. Validation refuses `direct ≤ nudge`, and there is a red test. |

No high or medium finding is fully not held.

## B. New defects

**B1. Medium. §3 S3a (probe-body fixtures, exit) and §4.1.**
- **Scenario.** S3a's exit requires that "each rd7.2 request body" be reproduced byte for byte by composing the same cut.
  - Cases 7 (`allowed_tools`, A1's step), 9 (leading developer item), 10 (`reasoning.context`) and 11 (removed tool, V1) are not S3a compositions, so they can never reproduce.
  - Cases 5 and 6 use note text written by hand in the probe program before S3a's templates exist. Either the probe script then fixes the template, or the bytes cannot match.
  - S3a's exit also now waits on rd7.2 finishing, and on the Paper probe (§4.1: "each saved request body becomes an S3a fixture"), which waits on the operator's approval of metered spend.
  - This brings back the probe-on-a-unit-exit coupling that SR-H2 removed, and puts it on the spine.
- **Direction.**
  - Limit the fixtures to the cut-composition cases (1-5 and 8).
  - Compare item sequence and kinds exactly, and compare note text through a template that the probe program and S3a share. Or declare the probe's text the provisional template.
  - Fixtures from probes that have not run are added when they exist, and are not part of S3a's exit.

**B2. Medium. §1.1 (row "5st rework"), §1.4, §3 5st.**
- **Scenario.**
  - 5st is to be "reworked onto P1", but P1a and P1b exist only on `work/clm-rd7` until integration proposal 1, which comes after T1.
  - campaign-defects works in `context/defect-5st`, from master or the integration branch. It has no P1 to build on unless it branches from the program branch, or waits for proposal 1 and the operator's integration.
  - Design §21 marks campaign-defects as waiting on 5st, and nls.16 and a1v.6 wait on campaign-defects. So the operator's phase now silently waits on S0, P1a and P1b, and possibly on S1, S2, T1 and a master integration.
  - The 5st staged diff edits `main.cpp`, `coding.cpp` and `anthropic_provider.cpp`. If it runs in its own worktree while the serial lane is active, it breaks the one-serial-lane rule.
- **Direction.**
  - State the base and return path for 5st: either it branches from the program branch after P1b and merges back through the gate, or it waits for proposal 1.
  - State that the serial lane pauses while 5st edits shared files.
  - Tell the operator the resulting dependency of campaign-defects on this program. This states a consequence; it does not reorder anything.

**B3. Medium. §3 P1a (shipping, installed-generation test, exit), §3 P1b.**
- **Scenario.**
  - P1a is "new files only plus scripts", yet it owns "a visible startup refusal" for a missing profile directory. Startup lives in `src/main.cpp`, which is P1b's file.
  - The three-location test has `scripts/check-install-blackbird` check that the build tree, the installed generation and a `check-linux` run "load the same profile set (same set digest)". That script uses a fake `cmake` that writes a shell-script `blackbird`; it states "no compiler" and never builds or loads anything real. Also, until P1b no binary resolves or reports a profile.
  - So P1a's exit ("the three locations load the same set") cannot be observed as written. TM-H1's intended red test, "an installed generation's `provider.request` names the expected profile id", appears in no unit.
- **Direction.**
  - Move the refusal and a real installed-generation case into P1b. One way: a release install into a scratch prefix, one `--once` request against a mock provider, then read `profile.digest` from the record.
  - Leave P1a with what the fixture script can check: `profiles/` copied and listed among the required files, and the `check-linux` path list.

**B4. Medium. §1.1 (Probes lane, "no repository source"), §4.2 outcome table, §5.4.**
- **Scenario.**
  - Probe outcomes change rows in `profiles/providers/openai-codex.lua`, and the Paper probe changes Paper's file. Those are serial-lane files from P1a onward, and they are behavioural configuration checked by the gate (the repository test that every behavioural field has a `basis`).
  - rd7.2 runs "now", probably before `profiles/` even exists. Its rows have no owning commit.
  - Later results (the Paper probe, rd7.3) need gated commits on the program branch, which "only the serial lane and gated merges move". The table budgets the probes at 0 gates.
  - The probe reports' journal entries and `papers/` commits also have no named branch.
- **Direction.**
  - The unit that consumes a field applies its probed row and cites the report: S3a for reasoning, turn and note rows; A1 for `tool_restriction`; V1 for case 11.
  - A result that arrives later is a small profile-update step in the serial lane, budgeted as a gated commit (and producing one `rebind` per §6.1).
  - Name the branch that probe reports commit on.

**B5. Medium. §5.1.**
- **Scenario.**
  - "After every gate, the same `pgrep` is checked against the gate's time window from the log" cannot detect an overlap. `pgrep` lists only live processes, and `proc-stall-watch` logs only listings slower than 2 s.
  - Another session's gate that ran from minute 2 to minute 8 of this gate leaves no trace. A 9h5 flake in that window is then classed as "a defect in the unit" and spends layer allowances.
  - The pre-gate check is also only point-in-time: other sessions follow `hook-lock`, not this rule.
- **Direction.** Sample during the gate. For example, a background loop appends `pgrep -fl` output every 10 s to the gate log. It may also read `/private/tmp/blackbird-hook.lock*/holder` timestamps afterwards. "Recorded overlap" is then defined on those samples.

**B6. Medium. §3 S0 ("Signals … SIGTERM through the existing handler").**
- **Scenario.**
  - The session process has no SIGTERM handler. The only `sigaction(SIGTERM)` calls in `src/` are in the OAuth login flow (`provider_oauth.cpp:73`, which restores the old handler) and in the raw recorder (`raw_runtime.cpp:796`). A SIGTERM therefore kills the session with the default action, and the stop is unclean.
  - The exit list counts it as clean, the tests cover no signal path, and the files list adds no handler.
- **Direction.** Either classify SIGTERM (and SIGHUP) as unclean, or add the handler as listed S0 scope with a test. The handler would set the existing interrupt or stop flag through an async-signal-safe path and then reach `finish_interactive`.

**B7. Medium. §4.3 (Paper turn-rule probe), §7.1 fallback.**
- **Scenario.**
  - The `any_closed_round` case is described as "a cut mid-turn that leaves the current turn untouched". That is `retain_current_turn`'s rule. Design §7.4 defines `any_closed_round` as "no turn constraint".
  - The trial tasks grow entirely in turn 0, so a cut that leaves the current turn untouched has nothing to archive. A pass would still enable a Paper row whose real behaviour, archiving closed rounds of the current turn, was never sent.
  - The Paper fallback batch would then rest on an unprobed rule.
- **Direction.** Define the case as archiving closed rounds inside the open current turn, with the §4.2 readings.

**B8. Medium. §6.2, §7.3 (provenance), trial tooling lane.**
- **Scenario.**
  - §6.2 says trial batches run from an installed generation, so worktree profile edits never reach them. But `run-one` takes any `BUILD_DIR` and sets `BLACKBIRD_EXECUTABLE="$BUILD/blackbird"`.
  - A build tree's resource root is the source checkout (CMakeLists.txt:196).
  - Every `run-one` turn, the prelude and each continuation, is a separate `--once` process, which is a session start, so the profile is resolved again each time.
  - If an implementer edits a profile in the serial worktree mid-batch (S3a adds fields, a probe row lands), continuations pick up the edit and record a mid-run `rebind`.
  - `meta.txt` "records the installed generation", but nothing requires one.
- **Direction.** `run-one` refuses a `BUILD_DIR` that is not `<generation>/build/release`, or records the compiled resource root and refuses if it is a checkout. The reader rejects runs whose profile digest changes between turns.

**B9. Medium. §4.1 and §4.2 ("continues", the fixture point).**
- **Scenario.**
  - The fixture is a completed session; turn 0 ends when FLOW.md is written. "Continues" needs function calls in 2 of 3 continuation responses because "the fixture task always has more reading to do". That is false at the fixture's end.
  - Case 8 needs a cut point past round 25 of a run accepted at 30 or more rounds, which is near completion.
  - The uncut baseline is used only for "degraded", not for "continues". A cut-free model that is finishing would fail "continues", and the boundary would stay disabled for a reason unrelated to the cut.
  - The workspace copy is the seed, not the workspace state at the cut point.
- **Direction.** Name the cut-point round, chosen so that the uncut baseline continues (2 of 3) from it. Judge "continues" relative to that baseline. Restore the workspace to the cut point (replay the fixture's file writes), or accept and state the difference.

**B10. Medium. §3 A1 red tests (the wire oracle).**
- **Scenario.**
  - The step's input up to the brief is compared with "the following main request's input". When the step's proposal settles at the same G (the case under test), the following main request is post-cut and the step's input is pre-cut, so the oracle fails on correct code. It holds only on decline, refusal or failure.
  - "The same cache breakpoint positions" contradicts design §13.1 for `palette_only`, which sends no breakpoint and drops all reasoning on prefix-checked profiles. `palette_only` is the only restriction shipping until case 7 passes.
- **Direction.**
  - On decline or refusal, compare with the following main request.
  - On a settled proposal, compare with the main input that the same G captures before settlement (wire capture of the previous main request plus appended items), not with the builder's output.
  - Make the breakpoint and tools clauses conditional on the restriction: `allowed_tools` gets the same tools and positions; `palette_only` gets no breakpoint and the profile's `side_request_reasoning`.

**B11. Medium. §3 "A1 live check".**
- **Scenario.**
  - Without a boundary, the luna cut settles at the first G of a continuation turn. In the 2026-10-17 luna runs, continuations answered DONE in 3-6 s, and `run-one` stops at DONE. So "the next three main requests HTTP 200" may have no three requests to read, and the check is neither pass nor fail.
  - A real fail after A1 has closed both layers has no path. A1 is already merged, §8 has no row for it, and the check is on the trial path.
- **Direction.**
  - Give the live task a second part that is handed over in the continuation prompt, or count "up to three, at least one".
  - Add a §8 row for a failed live check: the step stays disabled by policy, A1's code is kept, and the block goes to the operator as new scope.

**B12. Medium. §7.1, §7.3 (arms), §7.4 (size), §8 (harm row).**
- **Power.**
  - The claim that twenty pairs "can show a completion difference if the discordant pairs split at least 2:1 among nine or more" is wrong. Under no effect, a split of 6:3 or worse occurs with one-sided p = 130/512 ≈ 0.25. At 8:4 of 12 it is about 0.19, and at 10:5 of 15 about 0.15.
  - So the harm row stops the step arm on noise roughly one route in five or more, more often if it is checked repeatedly.
- **Confound.**
  - The step arm is `{enabled = true, nudge, direct}` against `{enabled = false}`. It bundles inform, the `CONTEXT_REVIEW` nudge, and model-initiated `context_manage` calls together with the step.
  - So a completion difference cannot be attributed to "the archival step", which the question names. The primary outcome also mixes step cuts with the model's own nudged cuts unless the reader separates them by `clm.step` or by stage.
- **Direction.**
  - Phrase the question and the harm row as "CLM policy on" versus off. Restrict the primary outcome to cuts settled from `clm.step` proposals.
  - State the harm rule as an attention trigger with its false-positive rate, or use an exact sign-test threshold fixed in advance (for example 9:1 or better among 10 or more, one-sided p ≈ 0.011) and state the smallest difference 20 pairs can detect.

**B13. Low. §1.1 Trial tooling lane, §3 ib8.1, §5.4.**
- **Location.** The reader is "in new repository files" in a lane said to be "outside the repository".
- **Scenario.**
  - No worktree or branch is named for it, and its gates are not in §5.4.
  - Its mock session must exercise every §18 row, which needs the post-R1 binary, so its last step is serial.
  - §3 ib8.1's mock covers "every §18 row", while §1.2 narrows the audit to the batch's signals.
  - That narrowing, which drops K1 from ib8.1's dependencies, is a deviation from design §21 and should be named as one to the operator.
- **Direction.** Name a worktree and branch for the reader, put its gates in §5.4, run its final mock-session pass as a serial step after R1, and align §3 with §1.2.

**B14. Low. Smaller items.**
- **Pilot circularity (§7.2-§7.3).** Step-arm levels come from "the pilot's baseline runs", in the same pilot. Run the 9 baseline runs first.
- **Probe token estimates.**
  - Probe tokens are priced at "about 30k" per request, while the fixture's acceptance range is 30k-150k. rd7.2 could be about 15M tokens and the Paper probe up to about 5M metered tokens. Give the operator the range.
  - Case 6's row does not say it sets `enabled = true`, yet §7.1 treats case 6 as enabling mid-run settlement.
- **Small gaps.**
  - R1 has no red test for `purpose = clm.recovery`.
  - The A1 live check says "direction 96 KiB"; it means direct.
- **Ordering and placement.**
  - P1b sits at serial position 2, so S1 idles if P1a is late. P1b could go anywhere before S3a.
  - c3f touches only `src/workflows.cpp`, so by the plan's own rule it need not lengthen the serial path.
- **Integration and downgrade.**
  - Integration proposal 1 carries S1, so it should state the downgrade cost (a full replay above 64 MiB; repaired sessions refused).
  - The master binary reconciles before refusing a `repair` frame, as it does for `cut`.
- **Lists and tooling.**
  - Q1 changes raw ABI text but has no Linux run.
  - `run-one` is shared and unversioned tooling used by other campaigns, so the fail-closed prelude change affects them. Version it, or fork it for this program.
  - Master merges after S3b can bring in new context-mutation callers. Re-grep the mutation callers whenever a merge touches them.
  - Reserving the host for a batch of about 120 runs, 2 at a time, blocks serial-lane gates for most of a day. The schedule does not count this.

## The open choice: building the Anthropic-only paths against mocks now

The choice is sound for the parts that share invariants, and weaker for the separable units.

**The S3b, V1 and E1 parts.** These are the closing reply and the step-budget exit (S3b), `drop_after_change` with its validator pairing, and the retaining-profile paths of V1 and E1 (2o9).
- They belong to the invariants of units that are being built anyway. Leaving them out would split those invariants. Design §21 forbids that: "no unit ships a field or behaviour that another unit fills … later".
- The design grounds them in cited, documented behaviour. Specification oracles and mock wire capture can check them.
- One caution: the Anthropic profile ships them active, at cited basis. So the first real Anthropic session runs code that has never seen the provider. The "unobserved" marking should therefore also go into the profile file and the integration proposal, not only into exit notes.

**P3 and K1's Anthropic plan.**
- These are separable units with no consumer. P3's own exit says "nothing enables it before rd7.3", and K1's live measurement waits for an account.
- Their content depends on rd7.3's outcomes (case 4 placement, case 9, the cache plan). Building them now risks rework that BOUNDED_HARDENING would count as new scope.
- They cost about 22 gate attempts (about 4 h) of the plan's dominant resource.
- Both sit after 5st and P2 at the tail and off the trial path, so the decision loses nothing by waiting until the serial lane reaches them. The plan could record it as deferred to that point rather than settled now.
