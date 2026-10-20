# Context lifecycle: delivery plan

Plan for epic `arconaut-rd7`, from the reviewed design
[CONTEXT_LIFECYCLE.md](CONTEXT_LIFECYCLE.md) revision 3 (commit `4152691`). Revision 3 of the plan,
final: revision 2 answered the plan review (`papers/clm-a1v-2026-10-19/plan-review/`), and this
revision applies the single recheck (`plan-review/recheck.md`), the last hardening layer.
Appendix A records every disposition.

Each unit runs JSMNTL: written sub-plan, red tests, code, green tests, adversarial code review,
fixes. "Design §" points into the design. Unit names follow design §21, with these changes made
here:
- P1 splits into P1a and P1b;
- new units S0b (SIGTERM clean stop), Q1 (request signals), B (pure request builder) and R0 (the
  ib8.1 reader);
- P2 follows the operator's 5st unit;
- P3 and K1's Anthropic part are deferred until an Anthropic account exists (§1.4).

Line numbers are at `ae81fbe`, which the review confirmed still hold at `4152691`. They drift as
units land, so each sub-plan re-reads them before editing.

## 1. Order and lanes

### 1.1 One serial lane for the shared files

Almost every unit edits the same files: `src/coding.cpp`, `src/context.cpp`,
`src/retained_state.cpp`, `src/tools.cpp` and `src/anthropic_provider.cpp`, and with them
`src/main.cpp`, `CMakeLists.txt` and `tests/coding_test.cpp`. So **every unit that touches any of
those files runs in one serial lane**: one worktree, one unit at a time. A parallel lane is allowed
only for a unit whose files are disjoint from everything the serial lane edits in the same period.

**Serial lane, in order:**

| # | Unit | Depends on | Notes |
|---|---|---|---|
| 1 | S0 clean-stop snapshots (7c8) | — | |
| 2 | S0b SIGTERM clean stop | S0 | small unit with its own test |
| 3 | P1b profile wiring | P1a | kept before S1 so integration proposal 1 holds only S0, S0b and P1 (§5.3). If P1a is late, S1's work starts on its unit branch but merges after P1b |
| — | **integration proposal 1** to the operator | P1b | gives 5st a master base (§1.4) |
| 4 | S1 context persistence | S0 | |
| 5 | S2 units and classes | S1 | |
| 6 | T1 shaping at creation | S2 | after S2: both edit the `drain_workflows` append (src/coding.cpp:4281-4288) |
| 7 | S3a cut composition | P1b, S1, S2 | includes the wire-level turn state and the codecs' pure render |
| 8 | S3b cut timing | S3a | includes `rebase_pending` and the `pending` section's playback |
| 9 | Q1 request signals | S3b | the design §18 chapter fields |
| 10 | B pure request builder | Q1 | A1 and R1 both use it |
| 11 | V1 inspection and contracts | S2, S3b, T1, B | |
| 12 | E1 escalation | V1, S3b | folds in 2o9 |
| 13 | A1 archival step (C6a) | P1b, S3b, B, V1, E1 | |
| 14 | R1 overflow recovery (a1v.5) | S3b, B, P1b | |
| 15 | R0 final pass | R1 | the reader's mock-session pass against the post-R1 binary |
| 16 | U1 TUI | S1, S3b | |
| — | 5st rework onto P1 | proposal 1 accepted | **the operator's campaign-defects unit**; the serial lane pauses while it edits shared files |
| 17 | P2 remaining rule migration | 5st | |
| 18 | K1 cache, non-Anthropic part (bvu) | P2, S3a | |
| 19 | A2 C6b | A1 timing data, prerequisites | blocked today (§3 A2) |
| deferred | P3; K1's Anthropic part | an Anthropic account and rd7.3 | at the tail, off the trial path (§1.4) |
| interleaved | probe-row steps | the probe and the unit owning the fields | one small gated step per probe result (§4.5) |

**Parallel lanes**, each with disjoint files:

| Lane | Work | Files | Joins at |
|---|---|---|---|
| Profiles core | P1a | new files only (`src/profile_reader.cpp`, `src/provider_profile.cpp`, `include/blackbird/provider_profile.hpp`, `profiles/`, their tests), plus `scripts/install-blackbird`, `scripts/check-install-blackbird` and `scripts/check-linux` | serial #3 |
| Briefs | c3f, after S2 has merged | `src/workflows.cpp`, which no later serial unit edits | merged through the gate at the next boundary |
| Probes | rd7.2 now; the Paper probe after operator approval; rd7.3 when an account and P3 exist | no repository source. Probe programs and reports go on branch `work/clm-rd7-probes` (text only); run data goes under `~/projects/blackbird_runs/` | reports merge with the next gated merge; profile rows land as serial steps (§4.5) |
| Reader (R0) | the ib8.1 tertiary reader | new repository files and their own CMake target block, on worktree `context/clm-rd7-reader`, branch `work/clm-rd7-reader` | gated merge before the A1 live check; its final mock-session pass is serial #15 |
| Trial tooling | `run-one-clm`, a fork of `run-one` for this program (§7.3) | `~/projects/blackbird_runs/bin/`, outside the repository | the pilot |

`CMakeLists.txt` is the one shared file the parallel lanes touch. They only append new target
blocks, and every merge is gated (§5.2).

### 1.2 Critical path

The serial lane is the critical path for building:

`S0 → S0b → P1b → S1 → S2 → T1 → S3a → S3b → Q1 → B → V1 → E1 → A1 → R1 → R0`

P1a must merge before P1b; it is small and starts on day one. c3f runs beside the serial lane after
S2. The path to the first trial batch continues:

`… → R0 → A1 live check (needs rd7.2) → calibration and pilot (§7.2) → first batch`

- rd7.2 runs now in its own lane, so it is finished long before A1. The A1 live check is a step
  after A1's exit, so no probe blocks a unit's exit.
- **Deviation from design §21, named for the operator.** The design has ib8.1 depend on K1. Here
  the reader audits only the signals a batch uses. The first batch is OpenAI-only, so K1 is not on
  its path. K1's signals (Anthropic cache writes) are audited when K1's Anthropic part lands, before
  any Anthropic batch.
- P2 and K1 wait for 5st, whose timing is the operator's; neither is on the trial path.

### 1.3 Pre-work, before S0

Read-only and cheap; these shape later units:
1. **Raw region nesting check** (design §13.3, unconfirmed): may a provider `RawRegion` open inside
   the main request's assembly region? The answer shapes B.
2. **Operator questions**, asked once and recorded:
   - Will an Anthropic account be provided? Will an OpenAI API key?
   - Approval of Paper's metered spend for the Paper probe.
   - Confirmation of the ChatGPT subscription quota for the probes, calibration, pilot and first
     batch (§7.4).
3. **Prerequisite with no bead yet**: the OpenAI codex auth-file race between concurrent requests
   (C6_DESIGN, `openai.cpp:226-308` at `1f28945`). No bead exists; it is filed when A2 is
   scheduled.

### 1.4 What this plan orders but does not decide

- **5st** stays in the operator's campaign-defects phase. The owner set the order: P1 → the 5st
  rework onto P1 (its own unit, owned by campaign-defects) → P2.
  - 5st needs P1 on master. Integration proposal 1 comes right after P1b lands and carries S0, S0b,
    P1a, P1b and the a1v branch's units (§5.3).
  - **Until the operator accepts proposal 1, 5st waits.** campaign-defects waits for 5st, and
    nls.16 and a1v.6 wait for campaign-defects. So that chain now depends on proposal 1. This
    dependency is the operator's to control; this plan states it and reorders nothing.
  - Once proposal 1 is on master, 5st branches from master and returns through the operator's
    integration. This program picks it up through its master merge at the next unit boundary
    (§5.2). While 5st edits `main.cpp`, `coding.cpp` or `anthropic_provider.cpp`, the serial lane
    pauses at its next boundary, so the one-serial-lane rule holds across both.
- **Anthropic-only work** (owner decision):
  - The parts that belong to units being built anyway are built now against mocks: S3b's closing
    reply, the `drop_after_change` rule (S3a), and the retaining-profile paths in V1 and E1.
  - Each Anthropic profile row they rely on ships with `basis = "unobserved"`. These rows are
    `turn.close`, `turn.start`, `reasoning.at_cut`, `request_state.placement` and
    `side_request_reasoning`. `unobserved` is a recorded basis value (P1a's schema allows it). It
    appears in the profile file, in `/profile` and in integration proposal 2.
  - **P3 and K1's Anthropic part are deferred.** They stay at the tail, off the trial path, and are
    unblocked by an Anthropic account and rd7.3.
- **nls.16** comes after campaign-defects; a1v.6 waits for it.
- **Probes**: no profile row moves from `conservative`, `cited` or `unobserved` to enabled or
  `probed` without its probe.

## 2. Common rules for every unit

### 2.1 JSMNTL as applied here

0. **Sub-plan**: a note in `context/clm-rd7-notes/<unit>.md` (ignored, outside every worktree) that
   copies the unit section below and names the defects it closes.
1. **Red tests.**
   - For every test that changes an existing expectation or covers changed behaviour: run it
     against the old code, watch it fail, and record the failure (test name, assertion, file:line)
     in the unit note. That record goes into the journal entry with the unit's merge.
   - Tests for brand-new code are written first and fail because the code is absent. No old-code
     failure is claimed for them (BLACKBIRD.md: "beyond brand-new code").
   - **Red tests are committed together with the green code**, never in a failing commit, because
     the hook refuses one.
2. **Code**, grounded in the design section named.
3. **Green**: the unit's direct checks.
4. **Layer 1**: one adversarial code review of the unit's diff, then fixes.
5. **Layer 2**: one recheck of the fixed findings, then fixes. A test rerun after a real fix belongs
   to layer 2. Nothing further (docs/BOUNDED_HARDENING.md). At the boundary, an unsafe candidate
   stays unmerged and the specific block goes to the operator. A real failure found after a unit has
   closed becomes a new bead and its own unit; closed units are never reopened.
6. Commits go through the full gate (§5.1). The unit branch is pushed after each commit.

Reviews use `scripts/kimi-review` when Kimi is reachable. The journal records a Kimi HTTP 429 on
2026-10-19; when that recurs, the review is an independent agent with a fresh context and the same
brief.

### 2.2 Where checks run

- **Mac**: the full gate through the pre-commit hook; targeted `ctest -R` while developing; PTY and
  `tui-test` checks for terminal units. Mac ASan only where a fault is Mac-specific.
- **Neuroses Linux** (`scripts/check-linux`: debug, release, ASan+UBSan): every unit that changes
  persistence, playback, adapters, raw ABI text or exit paths. That is S0, S0b, P1a, P1b, S1, S2,
  S3a, S3b, Q1, B, A1, R1, P2 and K1 (and P3 when it runs). ASan runs there, not on the Mac.
- **Fuzzing runs on Neuroses only**, through a `fuzz` profile that P1a adds to `scripts/check-linux`
  as a tooling change, requalified once with a planted crash. AGENTS.md keeps fuzzing off the
  laptop, and the Mac fuzz link fails until e5g lands.
  - `scripts/check-linux --profiles fuzz [--fuzz-runs N]` (default 1,000,000 executions) builds
    `profile_reader_fuzz` and runs it over copies of `profiles/providers/*.lua`.
  - libFuzzer writes crash, leak and timeout inputs to `build/fuzz/artifacts/` in the remote
    workspace; before that workspace is removed, `check-linux` copies them to
    `context/linux/run-*/fuzz-artifacts/artifacts/` (ignored) and prints both paths. The log is
    `context/linux/run-*/checks.log`.
  - The Linux link needs libstdc++ for the distribution's libFuzzer runtime, which is built against
    it (CMakeLists.txt, the P1a block).
- **TSan**: A2's first step adds a Linux `tsan` profile (requalified with a planted race). Mac TSan
  waits for bead 0zi in the operator's campaign.
- **Never** mutation runs on the laptop.

### 2.3 Tests that fail on the old code

The rule covers changed behaviour and changed expectations. A behaviour-preserving move (P2, B) is
checked by tests that must pass unchanged plus a regression oracle; it does not invent failures.

## 3. Units

Each unit gives: what and why; files and functions; red tests; direct checks; review attacks; exit.

### S0. Clean-stop snapshots (7c8)

**What and why.** Every clean exit writes a snapshot as its last journal write, so a clean reopen has
an empty suffix and S1 can tell clean stops from unclean ones (design §4.4-§4.5).

**Clean exits covered:**
- `/quit` and `/exit` (src/main.cpp:1808-1811);
- plain-loop EOF and TUI exit, which reach `finish_interactive` (src/main.cpp:1294);
- `--once` and `--resume-once` returns (src/main.cpp:1607-1611);
- the restart exit with code 75 (the same return, and the station return at 1603-1604);
- the station stop;
- `/new` (1059) and `/resume DIR` (993), which already checkpoint (1070, 1021);
- `/restart`.

SIGTERM is not a clean stop today: the session process installs no SIGTERM handler, so it dies by the
default action. S0b adds the handler. SIGKILL, crashes and power loss are unclean. SIGHUP stays
unclean, and is listed as such.

**Files and functions.** src/main.cpp: each path above calls `ContextStore::checkpoint()` as its last
journal write. The call is best-effort: a failure is reported but does not fail the exit.
src/application.cpp:325 stays as it is. At open, when the suffix after the saved boundary is not
empty, the launcher writes one recovery-event record on `ApplicationChannel::log`, which older
binaries ignore, and the TUI shows a notice.

**Red tests.** Specification oracle, read from the bytes on disk before any reopen:
- new `tests/clean_stop_test.cpp`, one case per clean path:
  - a session with fewer than 64 records and under 4 MiB since the last snapshot, below
    `maybe_checkpoint`'s thresholds (src/context.cpp:698-706);
  - a background job and a participant active at exit;
  - after the exit, read both saved-state slots and the journal end through `load_saved_state`,
    without constructing a ContextStore, and require the newer slot's boundary to equal the journal
    end;
  - this fails on the old code for `/quit`, `--once`, EOF and the restart exit.
- a kill-9 case: reopen writes exactly one recovery-event record naming the suffix length. This
  fails today.
- `scripts/check-session-startup`, `scripts/check-rrc` and `scripts/check-new-session-pty` keep
  passing.

**Direct checks.** Mac gate and the scripts above; Linux debug and ASan+UBSan.

**Review attacks.** A clean path that skips the snapshot; a write after the snapshot; a snapshot
failure failing the exit. The cost of the extra snapshot at quit is measured on a 384 KiB context and
recorded, not assumed.

**Exit.** Every listed path passes; an unclean stop produces exactly one recovery event; the cost is
recorded.

### S0b. SIGTERM clean stop

**What and why.** `run-one`, the operator's terminals and process supervisors send SIGTERM. Today it
kills the session uncleanly: the only `sigaction(SIGTERM)` calls in `src/` belong to the OAuth login
flow (src/provider_oauth.cpp:73) and the raw recorder (src/raw_runtime.cpp:796).

**Design, grounded in the existing interrupt path.** SIGINT is installed by `RawCommandInterrupt`
(src/raw_command.cpp:515-575), which stores into the atomic `interrupted` (src/main.cpp:40), and the
loops poll it (src/main.cpp:581, 1284, 1499, 1599). S0b installs SIGTERM the same way, with a separate
atomic `terminate_requested`:
- the handler only stores to the atomic, which is async-signal-safe;
- a running turn is interrupted exactly as SIGINT interrupts it;
- the loops then leave through `finish_interactive` or the `--once` return, so S0's checkpoint runs;
- a second SIGTERM falls back to the default action, so a stuck process can still be killed.

**Files.** src/raw_command.cpp and include/blackbird/raw_command.hpp (installation and restore,
matching `RawCommandInterrupt`); src/main.cpp (the atomic and the loop conditions).

**Red tests.** Specification: `tests/clean_stop_test.cpp` gains a SIGTERM case for an idle loop, a
running turn and a `--once` run. Each reads the slots on disk as in S0 and fails today. A second
SIGTERM terminates.

**Direct checks.** Mac gate; Linux debug and ASan+UBSan.

**Review attacks.** Work done in the handler; interaction with the raw recorder's own SIGTERM
handling; a turn that ignores the flag.

**Exit.** A SIGTERM'd session reopens with an empty suffix.

### P1a. Profile mechanism core (parallel lane)

**What and why.** The reader, schema, validation, layering and shipping of profiles (design
§15.1-§15.4), in new files and scripts.

**Files and functions.**
- new `src/profile_reader.cpp`: a native reader of the Lua table-constructor subset (tables;
  string, number and boolean literals; comments). Anything else is refused with the file, line and
  construct.
- new `include/blackbird/provider_profile.hpp` and `src/provider_profile.cpp`:
  - native schema types, `validate_profile`, `merge_override`, `select_profile`;
  - the profile digest, computed over behavioural fields only, so a provenance edit causes no rebind;
  - the `basis` values `cited`, `probed`, `conservative` and `unobserved`.
- new `profiles/providers/`: `conservative.lua`, plus one file per main-coding provider holding only
  `match` and `sources`. Later units add the fields they consume.
- shipping:
  - `scripts/install-blackbird` copies `profiles/` beside `programs` and `assets` (line 83) and
    lists it in the required-file check (line 27);
  - `scripts/check-install-blackbird` (which uses a fake `cmake` and builds nothing) checks the copy
    and the required-file entry;
  - `scripts/check-linux` adds `profiles` to its path list (line 25);
- the `fuzz` profile in `scripts/check-linux` (§2.2).

**Red tests** (brand-new code):
- `tests/profile_reader_test.cpp`, specification oracle:
  - accepted constructs round-trip;
  - each refused construct is refused with its exact reason: functions, calls, operator expressions,
    global references, `nil`, metatable syntax, unterminated strings, depth over the limit, and
    non-UTF-8 bytes.
- a fuzz target over the reader, run on Neuroses.
- `tests/provider_profile_test.cpp`:
  - schema refusals with their exact reasons;
  - overrides without `basis` are accepted;
  - layering precedence and most-specific match;
  - the contradictory pair `drop_after_change` + `retain_current_turn` is refused;
  - the digest is unchanged by a `basis` edit and changed by a behavioural edit.
- `scripts/check-install-blackbird`: `profiles/` is copied and required. This fails today.
- a repository test that every shipped behavioural field has a `basis`.

**Direct checks.** Mac gate; Linux debug, release and ASan+UBSan; the reader fuzzer for a bounded
budget on Neuroses.

**Review attacks.** Anything in the reader that executes, or recurses without bound; a profile that
validates but crashes a consumer.

**Exit.** Profiles are copied, required and validated; the fuzzer ran its budget with no crash.

**Deviation, owner decision 2026-10-20.** P1a ships the early behaviour rows in the provider files,
not only `match` and `sources`: the reasoning, turn, request-state, note, tool-change and archival
rows that P1b records and S3a consumes, with honest bases (Anthropic rows `unobserved` per §1.4,
OpenAI rows cited with their probe pending). S3a therefore consumes these rows rather than adding
them. Nothing reads them in P1a or P1b. When code starts reading a row that reproduces a hard-coded
rule, the regression comparison against the old code's decisions stays with P2 (design §15.5). The
families P2 and K1 migrate (cache detail, errors, stop reasons, retry per provider, effort, output)
remain the conservative profile's until then; transforms to disable are deferred to the unit that
sends them. The design's example profile and basis vocabulary were reconciled with the implemented
schema (design §15.2), and an operator override of `conservative` is the base layer under every
shipped profile (design §15.4).

### P1b. Profile wiring (serial)

**What and why.** Profiles take effect, are recorded per request, and are pinned per run.

**Files and functions.**
- src/coding.cpp `CodingEngine::request`:
  - resolve the profile once per session start or definition boundary, never mid-run;
  - add `profile {id, revision, digest, layers}` to the `provider.request` record (4619-4630).
- src/main.cpp:
  - a **visible startup refusal** when the profile directory is missing or unreadable;
  - `/profile`;
  - the operator override directory;
  - an explicit profile-set directory option (`BLACKBIRD_PROFILES`) and an expected-digest option
    (`BLACKBIRD_PROFILE_SET_DIGEST`). With the latter set, a session refuses to start if the loaded
    set's digest differs. The trial tooling uses both (§7.3).
- src/retained_state.cpp `current_programs` (2983-2990): `provider-profile-set-v1` and
  `provider-profile-override-v1`.

**Red tests.**
- `tests/coding_test.cpp`: a new assertion that `provider.request` carries `profile`. No existing
  test reads the whole record (coding_test.cpp:755-765 and 1061-1068 read three fields).
- an override edited mid-run takes effect only at the next session start or definition boundary.
- the expected-digest refusal and the missing-directory refusal.
- **the three-location check, with a real installed generation**:
  - a release install into a scratch prefix;
  - one `--once` request against a mock provider (the existing `BLACKBIRD_RESPONSES_CURL` fixture
    path);
  - read `profile.digest` from the session's `provider.request` record;
  - require it to equal the build tree's digest and the digest a `check-linux` run prints for the
    same profile set.
  It fails today: an installed generation has no profiles.

**Direct checks.** Mac gate; Linux debug; the scratch-prefix install.

**Review attacks.** A profile reread per request; a mid-run edit reaching a running session; any
behaviour change (there should be none).

**Exit.** Every request records its profile; the three locations agree; behaviour is unchanged.

**Owner decisions 2026-10-20 (P1b hardening layer 1).**
- Profile files load only at session start, never at a definition boundary: stricter than the
  text above. Every successor start (`/new`, `/resume`, a restart's resumed start) is a session
  start and loads afresh.
- Each `provider.request` resolves its profile from the request's final model (after request
  options) and its request group's actual binding, cached per (model, binding digest); files are
  never reread. The record gains `model` and `binding` (a digest of the binding's non-secret
  fields: provider, protocol, normalized base URL, account kind).
- A binding whose base URL cannot be normalized (an IPv6 literal, `_` in the host, over 512
  bytes) is not refused: the route ran before profiles existed, so it runs under `conservative`,
  and the reason is recorded in `provider-profile-set-v1` (`start_route.fallback`), in the
  request record (`fallback`) and shown by `/profile`.
- An auth store that cannot be read at start does not stop a session that never calls a
  provider: the start records the deferral (`start_route.deferral`) and the first provider
  request reads the binding, failing as an auth-store refusal that names the store's path.
- A new session's route is resolved before its directory is made, so a resolution refusal leaves
  no session directory; a refused `/new` successor removes the empty directory its predecessor
  made for it.

### S1. Context persistence

**What and why.** "Live state equals reopened state" for the new format (design §4.2-§4.5): the `cut`
frame, one `apply`, playback, snapshot v2 with sections, one validator, the resume table, the
read-only state and `repair` frames.

**Files and functions.**
- new `include/blackbird/context_cut.hpp` and `src/context_cut.cpp`: the frame type, `apply`, ref
  encoding, and CRC32C over refs (src/journal.cpp:95-107).
- src/context.cpp:
  - the replay loop (405-488) gains a `cut` branch, and the originals branch (432) extends to `cut`.
    Staged-frame and `pending` playback comes with S3b.
  - the snapshot reader (347-376) is replaced by the shared validator.
  - `checkpoint` (587-697) writes the version-2 core plus registered sections.
  - **the writer holds no references into `originals_` or `captured_` across a record call.** The
    maintenance checkpoint can run inside any append and prune them (src/retained_state.cpp:1672-1727;
    src/context.cpp:670-679). The writer builds its frame from values it owns, records, then merges.
    A checkpoint that fails stays best-effort and never fails the write (design §4.3).
- new `src/saved_context.cpp`: `validate_saved_context` and the section registry.
- src/saved_state.cpp `load_slot` (143-153): calls the shared validator.
- **format-level record**: before the first write that an older format cannot read, the binary writes
  a latest-wins program record `context-format-level`. S1 and later binaries refuse to open a session
  whose level exceeds theirs, before `recover_coding_session` reconciles anything (src/main.cpp:335).
- whole-history playback for a rejected snapshot, under the 64 MiB policy.
- the read-only state, with snapshots and maintenance suppressed until a `repair` frame is accepted.
- the `repair` frame.
- src/raw_context.cpp and include/blackbird/raw_context.hpp: action codes for `cut`, `repair` and the
  new branches, with their raw ABI text.

**Red tests.**
- new `tests/context_cut_test.cpp`:
  - a **model oracle**: a vector of IDs and items applying the same frames must agree with `apply`;
  - each design §4.3 refusal is `corrupt`;
  - `existing` re-insertion keeps the ID, the original and the creating record;
  - a **metamorphic oracle**: over generated sessions, live state = snapshot restore + suffix
    playback = whole-history playback.
- a fuzz target over the decode of `cut` frames from journal bytes, run on Neuroses.
- new `tests/saved_context_test.cpp`: every core field and registered test section is corrupted in
  turn. Each case must enter whole-history playback with a recovery record, never an empty head. This
  fails on the old code (src/context.cpp:367-376; src/coding.cpp:2654-2655).
- read-only state:
  - a corrupted cut frame opens the session read-only;
  - `/quit` writes no snapshot there;
  - an approved `repair` frame lets playback continue.
- format level: a session at a higher level is refused before reconciliation.
- a maintenance checkpoint fired inside the writer's record call leaves no dangling reference (Linux
  ASan).
- existing tests that change, each failing on the old code first:
  - `tests/saved_state_test.cpp`;
  - the snapshot round-trip in `tests/context_test.cpp`;
  - `tests/journal_resume_test.cpp` and `tests/session_recovery_test.cpp` where they assert the saved
    shape;
  - `tests/raw_context_test.cpp`.

**Direct checks.** Mac gate; Linux debug, release and ASan+UBSan; the decode fuzzer on Neuroses.

**Review attacks.** Divergence between writer and playback; the section registry; the 64 MiB policy;
a write leaking from the read-only state; the format-level check running too late.

**Exit.** The oracles agree over generated sessions; every corruption case enters its recovery row.

### S2. Units and classes

**What and why.** "Only selectable units can be selected" (design §3, §5.1-§5.4, §11.1).

**Files and functions.**
- new `src/context_units.cpp` and `include/blackbird/context_units.hpp`:
  - the class map and its `classes` section;
  - legacy and seeded derivation;
  - `units`, `handle`, `parse_selection`, `expand`, `protected`, `superseded_copy`.
- src/context.cpp:
  - `whole_groups` (1170-1199) checks membership by call ID;
  - `append_impl` (927-997) records the class;
  - `manage` (1362-1450) works on units.
- src/coding.cpp:
  - `append_op` (1658-1663) takes a `{class}` option;
  - the `drain_workflows` continuation append (4281-4288) gets class `result`.
- src/workflows.cpp:291: ultracode declares `instruction`.
- `context-pins-v1` in `current_programs`.
- src/session.cpp `resume` (571-586): the `rrc:<token>` check by the creating record's label, read
  through identity lookup.

**Red tests.**
- new `tests/context_units_test.cpp`:
  - a **model oracle** for `expand`;
  - a **property** over generated contexts: no protected or pinned unit is ever in an expansion,
    except superseded instruction copies;
  - seeded sessions expand per source round;
  - a repaired round `[call][operator][output]` is selectable without its instruction (fails today:
    `partial-tool-group`);
  - `operator.beads` is a `result` (fails today: `mandatory-entry`);
  - a spoofed `origin` or `class` is refused.
- `tests/successor_seed_test.cpp`: seeded grouping.
- `tests/coding_test.cpp`: `arco.append(items, {class = "instruction"})` is protected.
- RRC: a checkpoint between the note and `consumed` no longer hides the note (fails today).

**Direct checks.** Mac gate, `scripts/check-rrc`; Linux debug and ASan+UBSan.

**Review attacks.** Handle collisions; the corners of the selection grammar; the class derived for
every append label; the byte comparison behind superseded copies.

**Exit.** Every append label maps to a class; the oracles agree.

### c3f. Briefs once per revision (parallel after S2)

src/workflows.cpp (291): append the brief only when no live instruction carries the same bytes.
Specification test: ten ultracode runs leave one brief entry (today they leave ten). Mac gate.
Exit: one copy per revision.

### T1. Shaping at creation

**What and why.** "Every model-visible output is shaped once before it is sent" (design §9).

**Files and functions.**
- src/coding.cpp `operation` (after 3940-3943): generic shaping.
- src/coding.cpp `drain_workflows` (4281-4288): shape the continuation result.
- src/tools.cpp: `generic_presentation`, reusing `observation_prefix_size` (209). Budget properties go
  only on tools whose measured schema cost is accepted.
- src/coding.cpp `define_tool` (2374+): `shape` metadata for Lua-defined tools.

T1 lands before any chapter exists, so its schema changes cause no prefix cut.

**Red tests.** Specification per kind:
- a 40 KB colleague answer comes back as 4 KiB head + marker + 12 KiB tail, with `result_ref`;
- explicit budgets are honoured;
- the operator's outermost call stays exact;
- a continuation result over 16 KiB is shaped.

Changed expectations, which fail on the old code: `tests/economical_defaults_test.cpp` and
`tests/observation_budget_test.cpp` for non-exec tools.

**Direct checks.** Mac gate. Measurements recorded: the `tools` wire bytes before and after; the
read-back time and peak RSS of a 16 MiB exec capture.

**Review attacks.** Internal `call()` users getting shaped; structured results cut mid-character; a
Lua tool's own `output_max_bytes` overridden.

**Exit.** Every model-visible kind is shaped; the measurements are recorded.

### S3a. Cut composition

**What and why.** "The input a cut produces is one the provider accepts" (design §6, §5.5, §5.6, §7.1,
§7.3). S3a ships a library with tests and no production call sites; its callers arrive with S3b.

**Files and functions.**
- new `src/turn_state.cpp`: `turn_state(rendered_messages, profile)`. Each codec exposes its pure
  render (Anthropic's grouping at src/anthropic_provider.cpp:346-351, and the Responses item list).
- `src/context_cut.cpp`: `compose_archive`, `compose_edit`, `compose_restore`, `compose_summary`,
  `compose_start`, `compose_rebind`, `compose_prefix`, `place_note`, `place_boundary`, `sweep`.
- reasoning rules per profile, including `drop_after_change`, mock-only (§1.4).
- the `chapter` section; binding digests; note and boundary text templates.
- the reasoning, turn and note fields in the shipped provider files. The Anthropic rows carry
  `basis = "unobserved"`.

**Red tests** (brand-new code):
- **model oracle for removal**: for generated contexts and selections, the frame `apply` leaves must
  equal, in order:
  - all units, minus `expand(selection)`;
  - minus the swept entries §5.6 allows in the rewritten region;
  - minus the reasoning entries the profile rule drops;
  - plus the inserted notes and boundary at their anchors.
  The expected set comes from an independent reference implementation of §5.6 and §6.2 inside the
  test.
- **property oracle for the prefix**: export through the real adapter before and after the cut; the
  provider-visible bytes before `p` are byte-identical.
- **specification oracle** from design §6.2: which reasoning entries are kept and dropped per
  `at_cut`; inserted and re-inserted entries count after `p`.
- **adapter export oracle**: every composed frame exports without error through
  `export_anthropic_request`, `export_provider_request` and `export_registered_responses_request`;
  pairing holds; the profile row's reasoning is on the wire.
- turn state on rendered messages:
  - an operator prompt merged after trailing tool results is `open` on Anthropic;
  - the closing reply followed by a separate prompt is `closed`;
  - `user_note` meets `user_text`.

**Exit**, from S3a's own oracles only: the removal and prefix oracles hold over generated contexts on
every profile row.

S3a's exit does not wait for any probe. When rd7.2 or the Paper probe results exist, the request
bodies of their cut-composition cases (rd7.2 cases 1-5 and 8, Paper cases 1-2) become regression
fixtures. They land in that probe's row step (§4.5), not as S3a exit criteria. The fixture compares
the item sequence and kinds exactly. Note text is compared through S3a's template, which the probe
program uses when S3a has landed; a probe run before S3a declares its note text the provisional
template, and S3a adopts it.

**Direct checks.** Mac gate; Linux debug and ASan+UBSan.

**Review attacks.** An export the adapter refuses after a cut, on adversarial contexts; a reasoning
rule applied on the wrong side of `p`; a sweep that moves `p`.

### S3b. Cut timing

**What and why.** "A cut takes effect only where the provider's turn rules allow it, and never wedges
the session" (design §7.2, §8). In scope:
- the closing reply (mock-only for Anthropic; rows `unobserved`);
- staging of every non-append change;
- the settle points and their order;
- merge, split, rebase and durable pending, with the `pending` section's playback;
- harness-cut outcomes and the forced form;
- the reserve;
- the result and description changes of every existing tool whose timing changes.

**Staging rule for native and operator callers.**
- Model tools and Lua forms stage.
- Native `ContextStore::edit`, `manage` and `restore` keep a publishing entry point for idle callers
  (operator commands between turns, and tests acting at idle). Inside a workflow they stage.
- S3b's pre-work greps every caller of `edit(`, `manage(`, `restore(`, `arco.edit`, `blackbird.edit`
  and the v1 policy fields. Each caller is either a claim whose expectation changes, with its
  old-code failure recorded, or a fixture moved to the idle path.
- The grep is repeated whenever a master merge touches those callers.

**Files and functions.**
- src/coding.cpp `request`:
  - the settle point G between `drain_workflows()` (4320) and `items` (4321), in the order: start,
    harness cut, settlement, then the step slot;
  - binding resolution from options before G;
  - G is skipped when `input` is overridden;
  - `binding-change-mid-turn`.
- src/coding.cpp `turn`:
  - the turn-start hook before the operator append (6077-6080);
  - J (6119) gated by turn state;
  - the reserve sized with the carried pending proposal (6042-6043).
- src/session.cpp `resume` (557): the closing reply before the `rrc:` note.
- src/context.cpp:
  - staging, and `Pending` held as units;
  - merge, split in one frame, `rebase_pending`, and clearing matched by stage;
  - the `pending` section and its playback;
  - `finish_workflow` through the composers;
  - `cancellation_budget` (1275-1351) pricing `cut` frames.
- a pending `select` is converted at staging into the archive of its complement.
- tool dispatch (5276-5284, 5613-5622) and the Lua bridge (1639-1663) return `staged` and
  `takes_effect`.
- src/tools.cpp (1380-1410, 1458-1470): the descriptions say when changes take effect.

**Red tests.**
- mock-provider rows in `tests/context_test.cpp` and `tests/coding_test.cpp`, covering every row of
  design §8.4 and §8.5:
  - an interrupt keeps the pending proposal, and a crash restores it;
  - merge with a waiting remainder;
  - a split in one frame, with a crash after it;
  - rebase by a rebind and by a prefix cut;
  - a pin after staging;
  - the order at G;
  - the forced form;
  - `binding-change-mid-turn`;
  - the step-budget exit followed by the closing reply.
- a **model oracle**: a pending-proposal state machine (stage, merge, split, rebase, settle, cancel)
  matches ContextStore after each generated event, both live and after playback.
- existing tests that change, each failing on the old code first:
  - `tests/context_test.cpp`: edit and restore inside a workflow stage; `ContextChange` timing;
  - `tests/context_budget_test.cpp`: staged activation;
  - `tests/compaction_campaign_test.cpp`: a managed archive writes a `cut` frame at its settle point;
  - `tests/coding_test.cpp`: `context_manage` via Lua; the restore result; line 1578;
  - `tests/journal_checkpoint_test.cpp`:124-126 and 147;
  - `tests/saved_state_test.cpp`:146-199 and 416-419;
  - `tests/operation_raw_test.cpp`:1494;
  - `tests/turn_raw_test.cpp`:409-430;
  - `tests/raw_retained_test.cpp`:674;
  - `tests/tasks_test.cpp`:173-182;
  - `tests/workflow_repair_test.cpp`, where it observes publish timing;
  - `scripts/check-terminal-pty`: the badge.
  The pre-work grep may add rows; any added row is recorded the same way.

**Direct checks.** Mac gate; Linux debug, release and ASan+UBSan; a PTY check of the closing reply.

**Review attacks.** Any non-append path that publishes outside G, J, the turn start or idle; a cut
between `origin` and acceptance; a harness cut with no outcome; an undersized reserve.

**Exit.** Every row of §8.4 and §8.5 passes; the pending-state oracle agrees, live and after
playback.

### Q1. Request signals

**What and why.** The design §18 join keys, owned by one unit.

**Files.**
- src/coding.cpp `provider.request` record (4619-4630): adds `chapter`, `chapter_ordinal`,
  `chapter_first` and `purpose`. Q1 sets `main` and `closing`; A1 and R1 set their own values.
- the DecisionEvent continuation (3363-3368): adds `chapter`.
- the assembly raw phase (4535-4537): adds the chapter revision and its raw ABI text.

**Red tests.** New assertions on existing records, across a cut: each field on main and closing
requests; the DecisionEvent chapter; the raw field, read by the raw reader.

**Direct checks.** Mac gate; Linux debug (the raw ABI text changes).

**Exit.** Every request carries its chapter keys.

### B. Pure request builder

**What and why.** Split `request()` into a builder that writes nothing and a main-request committer
(design §13.3). The pre-work nesting check fixes the builder's shape.

**Files.** src/coding.cpp `request`. The committer takes the retained-state append (4484-4488), the
slots (4524-4525), the preview (4604) and display.

**Red tests.**
- specification: the builder writes nothing;
- a regression oracle against the code at B's base: main requests built through the builder and the
  committer are wire-identical to those the old code builds, for a recorded set of sessions.

**Direct checks.** Mac gate; Linux debug and ASan+UBSan.

**Exit.** Main requests are wire-identical; the builder is pure.

### V1. Inspection and model-facing contracts

**What and why.** "Inspection never inflates context, and every offered choice is honoured" (design
§10, §11). Closes a1v.4 and a1v.1. The retaining-profile path of design §10.2 is built mock-only
against rows marked `unobserved`.

**Files.**
- src/tools.cpp `tool_definitions`:
  - the §11 tools;
  - `context_originals` and `context_restore` removed;
  - the `lua` tool description (1487-1490), which names `blackbird.restore(id)` and
    `blackbird.originals()`.
- src/coding.cpp:
  - context tool dispatch (5276-5284, 5583-5622);
  - the request-local delivery slot;
  - the Lua bridge (1651-1663).
- src/context.cpp `inspect` (1686-1845).
- src/main.cpp: `/context`, `/inspect`, `/originals`, `/restore` (1124-1160).

**Fixture.** A generator in `tests/fixtures/` builds a C5-shaped context (57 items) from a small
descriptor extracted once from the C5 run.

**Red tests.**
- new `tests/context_inspect_test.cpp`:
  - a **property**: `context_view` output is bounded regardless of context size, from 1 KB to 2 MiB;
  - no inspection changes the head or the chapter;
  - on request-local profiles, the rows appear in exactly one request input and in no context entry;
  - delivery skips `omit` and overridden `input`;
  - the retaining-profile path delivers the rows as output, and the next cut sweeps them;
  - specification checks on the C5-shaped fixture.
- **removed-tool history**:
  - for each route, an adapter fixture with a retained call to `context_restore`, exported after its
    removal;
  - rd7.2 case 11 on the live route. If a route refuses such a call, V1 keeps the old definitions out
    of `tools` and renders historical calls to them as results. The decision is recorded.
- existing tests that change, each failing on the old code first:
  - `tests/coding_test.cpp`: the arco round trip; `arco.append` returning `{revision, ids}`;
  - `tests/context_test.cpp`: text pages;
  - `tests/terminal_test.cpp`;
  - `tests/useful_work_test.lua`.

**Direct checks.** Mac gate; the fixture's page sizes recorded; a PTY run of `/context`.

**Exit.** a1v.4 and a1v.1 closed; the descriptions match behaviour in a mock run.

### E1. Escalation

**What and why.** "Guidance informs and never sets a size goal" (design §12), folding in 2o9. The
retaining-profile path (append the review text only when it changes) is built mock-only against
`unobserved` rows.

**Files.** src/coding.cpp: `default_context_budget` (2075+), `validate_context_budget` (2144-2164),
`budget_view` (2455+), `context_budget`, and the operational state (4440-4488). Also the `review`
section and the docs.

**The v1 policy mapping.**
- `nudge_bytes = trigger_bytes`, and `target_bytes` is dropped.
- `direct_bytes` is 442,368 (432 KiB) when that is above `nudge_bytes`; otherwise it is
  `nudge_bytes + 65,536`.
- `enabled` is kept.
- The mapped record is written once, with the mapping named in its `reason`.
- A v2 policy with `direct_bytes ≤ nudge_bytes` is refused.

**Red tests.**
- the defaults; the mapping, including a v1 trigger above 432 KiB; the size line; the review text,
  with no level, target or percentage.
- changed expectations, which fail on the old code:
  - `tests/context_budget_test.cpp`;
  - `tests/request_cache_test.cpp`:89-101;
  - `tests/turn_raw_test.cpp`:358-376 and 667;
  - `tests/request_state.hpp` and its users.
- on retaining profiles, 100 requests above the nudge append one review entry.

**Direct checks.** Mac gate.

**Exit.** 2o9 closed; the docs match the code.

### A1. The archival step (C6a)

**What and why.** "A review offers only honoured actions and never touches the main context" (design
§13.1-§13.2), built on B's builder.

**Files.** src/coding.cpp:
- the step, at slot 4 of G;
- the side request with `purpose: clm.step`;
- `palette_only` and `allowed_tools`, with `side_request_reasoning`;
- the change notice and `clm.step`;
- the Jev column, through `decision_model`.

**Red tests** (brand-new behaviour). new `tests/clm_step_test.cpp`, with a mock provider that captures
every request on the wire:
- **step request compared with the pre-settle main request.**
  - The same scripted session runs twice from identical state: once with the mock step declining,
    once accepting.
  - The step requests of both runs must be equal.
  - Each must equal, up to the brief, the declining run's following main request before its
    operational state, after the profile transform.
  - This compares two requests the product builds; it does not compare the builder with itself.
- **post-settle main request compared with the cut model.** In the accepting run, the next main
  request's input must equal S3a's reference model of the surviving entries (all units, minus the
  selection and the permitted derived entries), exported.
- **restriction-specific clauses** (design §13.1):
  - under `allowed_tools`: the same `tools` and the same breakpoint positions as the main request;
  - under `palette_only`: exactly one tool, no breakpoint, and the profile's
    `side_request_reasoning` applied.
- no context append, and no write to `usage_` or `last_request_bytes_`.
- decline, refused proposal and provider failure: the main request goes out unchanged.
- the review interval.
- the step is skipped when nothing can settle, when `input` or `tools` are overridden, with `omit`,
  and for the backstop engine.
- merge with a waiting remainder; split; the change notice.
- `purpose = clm.step`.
- Ctrl-C during the step; a length refusal is handed to C4 once.

**Direct checks.** Mac gate; Linux debug.

**Exit.** All mock cases pass.

### A1 live check

**When.** After A1, R0's merge, and rd7.2. It runs in a scratch session, with an installed
program-branch generation in a scratch prefix (§6.3).

**Policy.** `nudge_bytes` 60 KiB and `direct_bytes` 96 KiB.

**Task.** A two-part fixture task:
- part 1 is the probe fixture's trace task;
- part 2 is handed over in the continuation prompt: verify every citation in FLOW.md by reading the
  cited lines, and correct FLOW.md.

So the continuation turn needs several main requests after the step. Under `retain_current_turn`,
without an enabled boundary, cuts settle only outside the current turn, so they settle at the first G
of the continuation turn.

**Pass** is read from the audit and raw lanes by a script, never from stdout:
- at least one `clm.step` with `outcome = proposal`, and its `cut` frame settled;
- every archived unit absent from the next main request's input;
- at least one, and up to three, following main requests with HTTP 200 and no stream error;
- cached tokens recorded per request;
- fetch-backs counted;
- the task check recorded.

**Fail**: a provider refusal after a cut, or an archived unit still present in the next input.

A failure is recorded as a new bead and goes to its own unit; A1 is not reopened. Until that unit
lands, the step stays disabled by policy for trials, and A1's code is kept (§8).

### R1. Overflow recovery (a1v.5)

**What and why.** "A fired recovery is audited as critical and retried once in a new chapter" (design
§14), at the classified-overflow seam.

**Files.**
- src/coding.cpp `request`: on a `context_overflow` from the attempt loop (4538-4784):
  - unwind the request group and return to G;
  - run option A, then B, through the builder;
  - settle the recovery cut and retry once;
  - the terminal outcome;
  - `purpose = clm.recovery`.
- the classification comes from the test seam; nls.16 applies the profile rules later (a1v.6).
- src/anthropic_provider.cpp `MessageStream::finish` (202-209): handle `output_window_exhausted` with
  or without calls; a truncated `tool_use` is answered without dispatch.
- rageshake `raised_by` and `severity` (src/coding.cpp:5285+; src/tools.cpp:1306-1318).
- the `clm.recovery` record.

R1's schema change is a tools change: existing sessions get one prefix cut (§6.1).

**Red tests.** new `tests/overflow_recovery_test.cpp`, specification at the seam:
- A fits;
- A fails and B fits;
- neither fits: the terminal outcome, the critical complaint, and no loop;
- pinned instructions over the budget;
- a DeepSeek profile ends at the terminal outcome;
- an open tail on `closed_only`;
- the group is unwound before the side requests run;
- the failed attempt's state entry is swept;
- `purpose = clm.recovery` on the recovery side requests.

Changed behaviour, which fails on the old code: `tests/anthropic_provider_test.cpp`, the stop reason
with and without calls.

**Direct checks.** Mac gate; Linux debug and ASan+UBSan.

**Exit.** a1v.5 closed.

### R0. The ib8.1 reader

**What and why.** Every CLM signal a batch uses is recoverable from the audit and raw lanes. The reader
is a tertiary program; it builds nothing in the units and reopens none.

**Lane.** Worktree `context/clm-rd7-reader`, branch `work/clm-rd7-reader`. New files plus their own
CMake target block, beside `blackbird-token-usage` (src/token_usage.cpp). It merges through the gate
before the A1 live check.

**What it does.** It reports each design §18 signal by name, present or missing. It runs over:
- a scripted mock session that exercises every §18 row a batch uses. The final pass runs against the
  post-R1 binary, as serial step 15;
- the A1 live check's session;
- the rd7.2 probe sessions.

**Missing signals.** A missing signal is a defect in the unit that owns it under §3. If that unit is
open, it is fixed in its layer 2. If it has closed, it becomes a new bead and scope for the operator.

**Exit.** Every signal the first batch uses is reported present on all three sources.

### U1. TUI

**Files.**
- include/blackbird/context.hpp: `ContextChangeKind` (59-67).
- src/main.cpp:
  - the badge mapping (659-670);
  - reopen (1616-1634);
  - `/compact` renamed `/archive` (1143-1147);
  - `/session`;
  - `/archive cancel`;
  - the `/repair` screen. S1 owns the frame and the command's semantics.
- src/terminal.cpp: `terminal_context_badge` (322-334).
- src/station.cpp: the packet.

**Red tests.**
- `tests/terminal_test.cpp`: the badge strings and command names. These fail on the old code, which
  shows `compacted` and `/compact`.
- a `tui-test` expectation for reopen across three chapters.
- `scripts/check-terminal-pty` frames.

**Direct checks.** Mac gate.

### 5st (operator's unit, campaign-defects)

After integration proposal 1 is accepted, the staged repair in `context/defect-5st` is reworked onto
P1 from master:
- its effort vocabularies, model lists, plan restrictions, output ceilings and citations become fields
  of the shipped provider files;
- its `provider_settings.cpp` table does not ship as a second hard-coded table;
- its plan, review and gates belong to campaign-defects.

### P2. Remaining rule migration

**What and why.** After 5st, move the rules of design §15.5 that 5st did not move into profiles, with
unchanged decisions:
- the cache allowlist and TTL;
- the OpenAI cache route gate;
- the cache virtuals;
- the affinity field and header;
- the retry set;
- `retain_request_state`;
- Anthropic `max_tokens`.

Stop reasons (R1), cache-write usage and OpenRouter `session_id` (K1) are named as excluded.

**Files.**
- src/request_cache.cpp: 87-93 and 115.
- src/coding.cpp: 329-340, 4512-4514 and 4727-4737.
- include/blackbird/coding.hpp: 33-37.
- include/blackbird/anthropic_provider.hpp: 8 and 15.
- src/openai.cpp: 405-411.

**Tests** (behaviour-preserving):
- a regression table over the full product of providers, routes, models, efforts, statuses and error
  codes, comparing the decisions at P2's own base (the program branch when P2 starts, after 5st)
  with the profile-driven ones;
- these must pass unchanged:
  - `tests/request_cache_test.cpp`;
  - `tests/anthropic_provider_test.cpp`;
  - `tests/openrouter_test.cpp`;
  - `tests/responses_provider_test.cpp`;
  - `tests/openai_auth_test.cpp`;
  - `tests/coding_test.cpp` (`cache_affinity_test`).

**Direct checks.** Mac gate; Linux debug and release.

### K1. Cache, non-Anthropic part (bvu)

**Files and changes.**
- src/request_cache.cpp is driven by the profile's breakpoint plan.
- read and write multipliers move into profiles.
- src/openrouter.cpp takes `session_id` from the profile. There is no OpenRouter account, so this is
  mock-tested only.
- the OpenAI API-key breakpoint plan is mock-tested only; there is no key.

**Red tests.** Breakpoint positions per plan, including a chapter's first request; OpenRouter
`session_id` is set (it is not today).

**Direct checks.** Mac gate; Linux debug and ASan+UBSan.

**Deferred: K1's Anthropic part.** This covers `cache_control` emission, the usage mapping
(src/anthropic_provider.cpp:237-266) that keeps cache writes, and the live before-and-after
measurement. It runs when an account exists, after rd7.3.

### P3. Anthropic system messages and beta headers (deferred)

Unblocked by an Anthropic account. rd7.3 needs it.

- src/anthropic_provider.cpp `export_anthropic_request`: system messages at their documented
  placement; `tool_removal` and `tool_addition`.
- src/provider_auth.cpp (1545-1556): beta headers declared in the profile.
- `MessageStream` keeps `input_transformations`.
- Red tests: placement, wire bytes and header presence.
- Checks: Mac gate; Linux debug and ASan+UBSan; `scripts/check-anthropic`.

### A2. C6b

Blocked today.

**Entry:**
- A1's measured step latency;
- the codex auth race fixed (named in §1.3, no bead yet).

**First step:** add the Linux `tsan` profile and requalify it with a planted race. Mac TSan waits for
0zi.

**Content:** design §13.4, with a deterministic simulation of the worker against a fake job and a
blocking fake provider.

### a1v.6

After R1 and nls.16: wire nls.16's classified errors into R1. Then check live on the ChatGPT route
that an overflow is detected, recovery fires and the critical complaint is recorded. The cost is a
context near the window, in subscription quota.

## 4. Probes

### 4.1 Workspace, fixture and cut point

**Workspace.** Each probe session runs:
- in a copy of the fixture's workspace, restored to the cut point;
- under the same sandbox as `run-one` (`confine.sb`);
- in a scratch session directory under `~/projects/blackbird_runs/<date>-probes/`;
- with `BLACKBIRD_EXECUTABLE` pointing at a master or program-branch generation in a scratch prefix.

Never the operator's sessions.

**Fixture.** Luna on the ChatGPT route, one operator prompt, on a fresh copy of this repository:
- the task is to trace raw instrumentation through three subsystems (context, command jobs, provider
  transport) and write FLOW.md with file:line citations;
- the fixture is accepted when turn 0 has at least 30 rounds and the final input is between 30k and
  150k tokens. Otherwise a fourth subsystem is added and it runs again;
- the fixture is the session journal plus the workspace commit.

**The cut point** is the G before round 20. That is ten or more rounds before the task finishes, so an
uncut model still has reading to do. Rounds 2-18 are available to archive, which is more than 20
eligible message endings before the request end, as case 8 needs.

The task writes only FLOW.md, at its end, so the workspace at round 20 equals the seed. This is
checked in the fixture's audit: no `write_file`, `edit_file` or writing `exec` before round 20. If one
exists, it is replayed from its `file.proposed` original.

**Baseline.** From the cut point, the uncut request plus three continuations runs three times. "Degraded"
and "continues" are judged against this baseline.

### 4.2 rd7.2, OpenAI (ChatGPT route only)

**Route.** The ChatGPT compatibility route (`openai-codex`, subscription) is the only OpenAI route
available. There is no OpenAI API key and no OpenRouter account. Nothing probed here is written into
`openai-api` rows, which stay `cited` or `conservative` until a key exists.

**When.** Now, in the probes lane.

**Cases.** Each case is one cut request at the cut point plus three continuations. Validity cases run
twice. Behavioural cases (5, 6, 7) run three times, and their row is enabled only on 3/3.
1. keep retained encrypted reasoning after archiving rounds 3-15;
2. drop it;
3. rebind: no reasoning at all;
4. an in-place edit of an earlier `function_call_output`;
5. `user_note` boundary, then archive rounds 3-15;
6. `request_local_user` boundary, then three requests without it;
7. `allowed_tools` with only `context_manage` and mode `auto`, both accept and decline;
8. a deep cut: archive rounds 2-18, then read `cached_tokens`;
9. a leading developer item versus none, compared by `cached_tokens`;
10. `reasoning.context = "current_turn"` versus the default, for case 1;
11. a retained call to a tool no longer in `tools`.

**Readings**, from the audit and raw lanes:
- **accepted**: HTTP 200, a completed response, and no stream `error` or `response.failed`;
- **refused**: any 4xx, with its exact error;
- **degraded**: accepted, but the continuations carry fewer `reasoning` items, or `reasoning_tokens`
  is 0, where the baseline runs carried reasoning;
- **continues**: the case's continuations contain a `function_call` at least as often as the median
  baseline run's (the baseline is chosen to continue in at least 2 of 3);
- a behavioural case passes when every repeat is accepted, not degraded, and continues.

**Outcomes**, in `profiles/providers/openai-chatgpt-responses.lua` only:

| Case | Row |
|---|---|
| 1 passes; 2 refused or degraded | `reasoning.at_cut = keep`, basis `probed` |
| 2 passes; 1 refused or degraded | a drop rule for this route |
| 3 refused | `at_rebind` keeps same-family reasoning |
| 4 | `edit_acceptance` recorded |
| 5 passes 3/3 | `turn.boundary = user_note`, `enabled = true` |
| 6 passes 3/3 | `turn.boundary = request_local_user`, `enabled = true` (preferred over 5 if both pass) |
| 7 passes 3/3 | `tool_restriction = allowed_tools` on this route; otherwise `palette_only` |
| 8, 9 | implicit-mode cache behaviour after cuts. Explicit breakpoints need the API-key route (src/coding.cpp:329-340) |
| 10 | the `reasoning.context` value sent |
| 11 refused | V1's fallback for historical calls to removed tools |

**Cost.** The ChatGPT subscription quota pays:
- 7 validity cases × 2, 3 behavioural cases × 3 and case 11 × 2: 25 case runs × 4 requests = 100
  requests;
- plus 12 baseline requests;
- about 112 requests at 30k-150k input tokens each: **about 3.4M-17M input tokens**, and 0.5-1M
  output tokens.

No metered spend.

**Fallback.** If the route refuses the probe overrides, every OpenAI row stays `cited, probe pending`:
the boundary disabled, `retain_current_turn` only, and `palette_only`.

### 4.3 Paper probe

Paper (`responses-portable`, plaintext reasoning) has no probed turn rule. The conservative profile
allows no mid-turn settlement, so without this probe Paper cannot carry a cut arm.

**Fixture and cut point.** A Paper fixture made the same way (§4.1).

**Cases**, each three times, with the §4.2 readings and baseline:
1. **`any_closed_round`**: at the cut point, inside the open current turn, archive closed rounds 3-15
   of that same turn, with no boundary. This is the rule's actual behaviour: no turn constraint.
2. `user_note` boundary, then the same archive.
3. note placement: a developer note mid-history, which the portable route sends as system.

**MiMo.** Note placement only: two requests on the token plan.

**Cost.** About 48 requests at 30k-150k tokens: **about 1.4M-7.2M input tokens, metered**. The probe
runs only after the operator approves the spend.

**Outcomes.** Case 1 passing 3/3 sets Paper `open_turn` to include `any_closed_round`, basis `probed`.
Case 2 passing 3/3 enables `user_note`. Case 3 sets `note.placement`.

### 4.4 rd7.3, Anthropic (deferred)

**Account.** None today; whether to provide one is the operator's decision.

**When.** After P3, and only with an account.

**Setup.** Each case uses `thinking-binding-controls-2026-08-01`. Opus 5.5 and Fable 5.1 run cases
1-9; all four models run case 10. Validity cases run twice; behavioural cases 1, 2 and 10 run three
times. The cut point and baseline follow §4.1 on an Anthropic fixture.

**Cases:**
1. the closing reply after trailing tool results;
2. `reply_then_note`, then archive earlier rounds;
3. an operator prompt merged after trailing tool results, then a cut;
4. `system_message` as a boundary;
5. no boundary, with client removal of thinking at or after `p`;
6. a forced prefix cut with the tail open;
7. `drop_block`;
8. the open turn rendered as text;
9. `tool_change = appended_system_message`;
10. per model: a tool removed with `tool_removal` cannot be called, and thinking survives.

**Readings.** As in §4.2, with "thinking blocks present", and `input_transformations` empty or naming
only what the case dropped.

**Outcomes.** Rows that pass move from `unobserved` to `probed`:
- case 1 confirms `turn.close`;
- case 2 enables `reply_then_note`;
- case 3 may count a merged prompt as a turn start;
- case 4 allows `system_message`;
- if case 6 is refused, §8.5's forced rows become "end the turn with a recorded reason";
- case 9 enables `appended_system_message`;
- case 10 passing 3/3 switches that model's step to `appended_removal` (design §24).

**Cost.** About 232 requests at 30k-150k tokens: about 7M-35M input tokens, priced on the day with
the operator's approval.

### 4.5 Probe-result steps

Each probe result lands as a profile-row change in its own small serial-lane step:
- a unit branch, the row change and its probe-body regression fixtures (§3 S3a), one gated commit and
  one gated merge;
- each step cites its report.

A row lands only after the unit that owns its field has merged:
- S3a for the reasoning, turn and note rows;
- A1 for `tool_restriction`;
- V1 for case 11.

**Budgeted steps** (§5.4):
- rd7.2: two steps, after S3a and after A1;
- the Paper probe: one step;
- rd7.3: up to four steps, one per model, when it runs.

Each step changes a behavioural field, and so produces one `rebind` frame per existing session
(§6.1). Probe reports and their journal entries commit text-only on `work/clm-rd7-probes`, and merge
with the next gated merge.

## 5. Resources, gates and integration

### 5.1 One gate at a time on this host

The rule is one gate at a time on this host. It does not rely on `hook-lock`.

**Before every gate:**
1. no other `scripts/rigor`, `ctest`, `clang-tidy`, `clang`, `clang++` or `ninja` process is running
   (`pgrep -fl`);
2. no trial, probe or fuzz run is active on the laptop;
3. the stall precautions from the campaign-defects handoff hold: `proc-stall-watch` is running, and
   process monitors (Task Manager, Activity Monitor) are closed.

**During every gate**, a background sampler appends to the gate log every 5 seconds:
- `pgrep -fl 'scripts/rigor|ctest|ninja|clang'`, with the gate's own process tree excluded;
- the load average (`sysctl -n vm.loadavg`).

After the gate, it also reads the holder timestamps in `/private/tmp/blackbird-hook.lock*`.

**Overlap.** An overlap means a sample showed another session's gate or build process. A failure in
one of 9h5's load-sensitive tests (`command_raw`, `provider_exchange_raw`, `tui_retained_result`,
`terminal_custody_native`, `turn_raw`) during a sampled overlap or a load spike is attributed to the
overlap, not to the unit, and the whole gate is rerun. Any other failure is a defect in the unit.

**Trial batches.** A trial batch that measures wall time reserves the host, and no gate starts in its
window (§7.4).

### 5.2 Branches, merges and master

- **Program branch.** `work/clm-rd7`, from the current `work/clm-a1v-2026-10-19` head. The uncommitted
  4by edits in the `clm-a1v` worktree are someone else's and stay there.
- **Worktrees**, leased per lane:
  - `context/clm-rd7` for the serial lane;
  - `context/clm-rd7-profiles` for P1a;
  - `context/clm-rd7-reader` for R0;
  - c3f takes the profiles worktree after P1a returns it;
  - probes and trial tooling need no repository worktree.
- **Unit branches.** Each unit runs on `work/clm-rd7-<unit>`, branched from the program branch head.
- **Merges.** Every merge into the program branch is `git merge --no-ff --no-commit`, then a commit
  through the pre-commit hook, so the merged tree always gets a full gate.
- **Master.** Master is merged into the program branch at each unit boundary when it has moved, the
  same way. After S3b, the mutation-caller grep is repeated whenever a master merge touches those
  callers.
- **Journal.** Notes live in `context/clm-rd7-notes/`. A unit's journal entry is written in its merge
  commit.
- **Commits.** One per landed step: the implementation with its red tests, the layer-1 fixes, the
  layer-2 fixes. Each is pushed at once.
  - No force-push. A push failure is recorded and the local commit kept.
  - Text-only commits use the documented per-command hook override after their diff and link checks.

### 5.3 Master integration

Never by implication. Each proposal to the operator names its commits, the gates passed, the open
probes, the **model-visible changes** and the **downgrade cost**:

1. **Right after P1b.** Contents:
   - the a1v branch's units, including the context budget enabled by default (`1f28945`) and shaped
     observations by default (`870ec8c`);
   - S0 and S0b;
   - the P1 mechanism.

   Model-visible: the a1v changes. Downgrade cost: none, since no new format has been written.
   **5st waits for this proposal** (§1.4).
2. **After S1 through R0.** Contents: the nominal path with chapters and cuts. Model-visible:
   - S2's selectable classes;
   - T1's shaping;
   - the new tool contracts;
   - the step.

   Downgrade cost:
   - from S1, once both snapshot slots are version 2, a master binary needs a full replay, automatic
     only up to 64 MiB of journal;
   - a session with a `repair` frame or a `cut` frame cannot be opened by a master binary, which
     reconciles before it refuses (§6.3);
   - the Anthropic rows marked `unobserved` are listed.
3. **After U1, P2 and K1** (non-Anthropic), and whatever probe-row steps have landed.

### 5.4 Resource declaration (BOUNDED_HARDENING)

**Gate cost, from the measured history.**
- A passing gate takes about 10 minutes.
- Recent units needed about 2.5 attempts per landed commit (q4n 4, 47k 2, 5st 4).
- A unit lands about 4.5 commits: the implementation, the layer-1 fixes, the layer-2 fixes, the gated
  merge into the program branch, and a gated master merge when master has moved.
- A probe-row step lands 2: its commit and its merge.

| Unit | Defects closed | Landed commits | Gate attempts | Linux runs | Provider spend |
|---|---|---|---|---|---|
| S0 | 7c8 | 4.5 | 11 | 1 | none |
| S0b | SIGTERM unclean | 4.5 | 11 | 1 | none |
| P1a | decision 6 mechanism; profiles not shipped | 4.5 | 11 | 2 (incl. fuzz) | none |
| P1b | profile record, refusal, pinning | 4.5 | 11 | 1 | none |
| S1 | state H1, H2, M3, M11; recheck B8, B9, S-L4 | 4.5 | 11 | 2 (incl. fuzz) | none |
| S2 | state M4, M5, L7; doctrine H1, M13; recheck B7 | 4.5 | 11 | 1 | none |
| c3f | c3f | 2.5 | 6 | 0 | none |
| T1 | doctrine M8; continuation shaping | 4.5 | 11 | 0 | none |
| S3a | provider F1, F9, L9, L10; state L8; recheck B2, B6 | 4.5 | 11 | 1 | none |
| S3b | state H3, M1, M2, M6, M9, M10, L3, L5, L6; doctrine H2, M9; recheck B1, B3, B4, B5, B11 | 4.5 | 11 | 2 | none |
| Q1 | signal ownership | 4.5 | 11 | 1 | none |
| B | state M7 | 4.5 | 11 | 1 | none |
| V1 | a1v.4, a1v.1; doctrine M2, M7, M15, L3, L9 | 4.5 | 11 | 0 | none |
| E1 | 2o9; doctrine H3, M1; v1 mapping | 4.5 | 11 | 0 | none |
| A1 | doctrine M4, M5, L6, L8; recheck B10 | 4.5 | 11 | 1 | live check: about 3M tokens of ChatGPT quota |
| R1 | a1v.5; provider F6; state M8, L9 | 4.5 | 11 | 1 | none |
| R0 | signal audit | 4.5 | 11 | 0 | none |
| U1 | visibility; renames | 4.5 | 11 | 0 | none |
| P2 | §15.5 remainder | 4.5 | 11 | 1 | none |
| K1 (non-Anthropic) | bvu code part | 4.5 | 11 | 1 | none |
| A2 | C6b | 4.5 | 11 | 2 (TSan) | none |
| probe-row steps | rd7.2 ×2, Paper ×1 | 6 | 15 | 0 | none |
| rd7.2 | OpenAI facts | text only | 0 | 0 | 3.4M-17M tokens of ChatGPT quota |
| Paper probe | Paper turn rule | text only | 0 | 0 | 1.4M-7.2M tokens, metered, operator-approved |

**Totals for the build:**
- about 98 landed commits and about **241 gate attempts, roughly 40 hours of serialized gate time**;
- about 19 Linux runs;
- provider spend outside trials: the Paper probe (metered, operator-approved) and about 6M-20M tokens
  of ChatGPT quota.

Gate time, not coding, is the dominant cost of the schedule.

**Deferred, counted when an account exists:** P3 and K1's Anthropic part (about 22 attempts, 4 hours),
rd7.3's four row steps (20 attempts), and rd7.3's tokens (§4.4).

## 6. Migration and compatibility

### 6.1 What a session written by unit N looks like to unit N+1

| Boundary | Effect on existing sessions | On disk |
|---|---|---|
| S0, S0b | a clean exit or SIGTERM snapshots; an unclean reopen writes a recovery event on the log channel | more snapshots; log records |
| S1 | version-1 snapshots read by the shared validator; `edit` and `managed` frames play back as before | version-2 snapshots; `context-format-level`; `repair` frames only after an approved repair |
| S2 | snapshots without `classes` get classes rebuilt by identity lookup; unreadable records classed by role | `classes` section |
| T1 | outputs shaped; no chapters yet, so no cut | none |
| S3a | nothing (no production writer) | none |
| S3b | the first request writes `start`; legacy staged `managed` records replay as rejections; new non-append changes write `cut` frames; units archived through legacy `managed` frames are not listed by `context_view {scope: "archived"}`, and their originals stay readable by entry ID | `cut` frames; `chapter` and `pending` sections |
| Q1, B | none | new record fields |
| V1 | two tools removed and the `lua` description changed: one `prefix` cut per existing session | one `prefix` frame |
| E1 | a v1 policy mapped; the `context_budget` schema changed: one `prefix` cut | policy v2; one `prefix` frame |
| A1 | none on the main prefix | `clm.step` records |
| R1 | the rageshake schema changed: one `prefix` cut | one `prefix` frame |
| 5st, P2, K1, each probe-row step that changes a behavioural value | the behavioural digest changes: one `rebind` frame at the next session start, removing nothing for same-family reasoning. A step that changes only provenance (a basis from `cited` to `probed`, a source) changes the set's content digest, not the behavioural digest, and causes no rebind | one `rebind` frame per behavioural change |

### 6.2 Tools, instructions and profile digests on existing sessions

- V1, E1 and R1 change tool definitions. T1 does as well, but lands before chapters exist.
- 5st, P2, K1 and every probe-row step that changes a behavioural value change behavioural profile
  digests. Every edit, provenance included, changes the set's content digest, which trial records
  carry (§7.3).
- Profiles load once per session start (owner decision 2026-10-20, stricter than "or
  definition boundary"; see P1b).
- Trial runs pin their profile set per run (§7.3), so an edit in a worktree never reaches a run.

### 6.3 Downgrade, plainly

- **Program-branch builds never run the operator's real sessions.** The installed generation stays on
  master builds until integration. Live checks, probes and trials use scratch sessions and scratch
  install prefixes.
- **Before S1:** any binary opens any session.
- **From S1 to before S3b:** a master binary still opens these sessions. But once both slots are
  version 2, it ignores them (src/saved_state.cpp:146-153) and needs a full replay. That replay is
  automatic only while the journal is at most 64 MiB (src/retained_state.cpp:340-342); larger
  sessions need `--rebuild-session`. A session with a `repair` frame cannot be opened by a master
  binary, which reconciles it before refusing.
- **From S3b on:** no downgrade. A master binary reconciles (src/coding.cpp:3078) before the `cut`
  frame throws `corrupt`. S1-and-later binaries refuse cleanly through the format-level record before
  reconciling.

## 7. Trials (ib8)

### 7.1 The question

> On luna (ChatGPT route), with CLM on, does adding the archival step settle mid-run cuts that the
> model does not undo, without a large loss of task completion?

**Arms.** Both arms have the same policy levels and the same size line, nudge and model-initiated
cuts. Only the step differs.
- **control**: `{enabled = true, nudge_bytes = N_t, direct_bytes = 67,108,864}`. That is the
  validator's maximum, so the step never fires.
- **step**: `{enabled = true, nudge_bytes = N_t, direct_bytes = D_t}`.

**Outcomes.**
- **Primary**: the share of cuts settled from `clm.step` proposals (frames with `cause.source = step`)
  whose archived units are not fetched back for the rest of the run.
- **Secondary**:
  - task completion, paired against control;
  - input tokens, cached and uncached, per run and per chapter;
  - output tokens around cuts;
  - model-initiated cuts in both arms;
  - fetch-back counts and timing.

**Task completion** is each task's own acceptance check from the 2026-10-17 set
(`~/projects/blackbird_runs/2026-10-17-cpp26/VERIFICATION.md`), applied by script. Evals stay
deferred under AGENTS.md: there is no general eval harness and no model judge.

**Prerequisite route.** The question needs mid-run settlement on luna, which needs rd7.2 case 5 or 6
to pass.
- If neither passes, the batch moves to Paper, if the Paper probe's case 1 or 2 passed.
- If neither route permits mid-run settlement, there is no step arm to trial. The plan reports that to
  the operator instead of running a batch.

### 7.2 Entry, calibration and pilot

**Entry:**
- S3a, S3b, Q1, B, V1, E1, A1 and R1 merged with both layers done; T1 merged;
- the A1 live check passed;
- rd7.2 done (and the Paper probe, if Paper is used);
- R0 reports every signal the batch uses, on its three sources;
- the trial tooling of §7.3 in place.

**Calibration, 6 runs.** The three long tasks run twice each, with CLM on and the step disabled (the
control policy), at the default nudge. They give each task's median final input. The levels are then
set from these runs alone:
- `N_t` at about 30% of that median;
- `D_t` at about 45%.

**Pilot, 18 runs.** Three tasks, both arms, three repeats each, at the calibrated levels.
- The pilot measures only whether reviews and step-settled cuts occur.
- **The batch starts only if** at least two of three step runs per task settle a step cut.
- Otherwise the levels are recalibrated once from the pilot's control runs and the pilot repeats once.
  After that, the result goes to the operator.

### 7.3 Tasks, pairing and provenance

**Tasks.** t03, t05 and t06 on luna. In the 2026-10-17 runs, these long tasks did all their growth in
turn 0 (r-t03-luna 780 s then 6 s; r-t05-luna 481 s then 3 s; r-t06-luna 347 s then 6 s).

**Pairing.** Runs are paired by task. The two arms of a pair run in adjacent slots, with the arm order
randomized per pair and the pairs interleaved across tasks.

**Arm verification.** The prelude that sets the policy fails closed. The reader admits a run to an arm
only when the policy record in force matches the arm's declaration.

**Profile pinning, per run.** `run-one-clm`:
- passes the generation's profile directory explicitly (`BLACKBIRD_PROFILES`), with its expected
  digest (`BLACKBIRD_PROFILE_SET_DIGEST`), on every launch (prelude, turn 0, each continuation). A
  session whose loaded set differs refuses to start (P1b);
- refuses a `BUILD_DIR` that is not `<generation>/build/release` of an installed generation.

The reader also rejects a run whose recorded profile changes between turns: it compares the
`id` and `layers` as well as the `digest`. The behavioural digest alone does not identify the
profile: one that adds no behavioural row to `conservative` digests like it (today
`openrouter-responses` and `conservative` share a digest).

**Provenance.** `meta.txt` records:
- the checkout commit (`BLACKBIRD_CHECKOUT` set to the program-branch worktree);
- the installed generation;
- the binary hash;
- the profile set digest;
- the Jev env file path (`BLACKBIRD_JEV_ENV_FILE`, passed explicitly because `run-one` uses `env -i`);
- `run-one-clm`'s own hash.

**`run-one-clm`.** A fork of `run-one` for this program, so the fail-closed prelude and the pinning do
not change other campaigns' shared tooling.

**Failures unrelated to CLM** are recorded as their own class: provider timeouts, 4xx responses
unrelated to context, sandbox refusals and quota exhaustion. Their pair is excluded with the reason
and rerun once.

### 7.4 Size, harm rule and spend

**Size.** 3 tasks × 20 pairs = 120 runs.

**What the first batch does.** It estimates effect sizes: the primary share, its interval, and the
paired completion difference. It stops the step arm early only for a large harm stated in advance.

**The harm rule.** This is an exact one-sided sign test on discordant pairs (one arm completed, the
other did not), pooled over the route's pairs, at α = 0.01.
- It is checked once, at the end of the batch.
- The arm stops on that route if the discordant pairs against the step reach the threshold:

| Discordant pairs | 10 | 12 | 14 | 15 | 16 | 18 | 20 | 22 | 25 | 30 |
|---|---|---|---|---|---|---|---|---|---|---|
| Against step needed | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 19 | 22 |
| Exact p | 0.001 | 0.003 | 0.007 | 0.004 | 0.002 | 0.004 | 0.006 | 0.009 | 0.007 | 0.008 |

**What 60 pairs can detect.** With about 15 discordant pairs, a step that loses 4 of 5 discordant pairs
is detected 40% of the time, and one that loses 9 of 10 about 82%. With 20 discordant pairs: 63% and
96%. So this batch detects only large harms. Smaller differences are estimated, not decided, and are
reported as estimates.

**Spend.**
- calibration, pilot and batch total about 144 runs;
- long luna runs used about 2M-3.6M input tokens each;
- so the total is about **290M-520M input tokens and 5-9M output tokens of ChatGPT subscription
  quota**. The operator confirms the quota before calibration.
- For scale only: at `gpt-6.1-sol` API list prices ($2 input, $0.10 cached, $10 output per million)
  with 75% of input cached, the top of that range would be about $390.
- Paper is used only if luna cannot carry the question, and its metered spend is approved first.

**Host reservation.** The batch runs on the laptop through `queue-worker`, at most two runs at once.
Long runs took 6-40 minutes, so 144 runs reserve the host for roughly 1-2 days, and no gate starts in
that window (§5.1).
- The batch is scheduled after R0, when the remaining serial work can wait: U1, and P2 and K1, which
  wait for 5st anyway.
- That reservation is part of the schedule.

## 8. Risks and kill criteria

Each criterion is an observation from the audit and raw lanes. When one triggers inside a unit's
hardening, the candidate stays unmerged and the block goes to the operator. A redesign is new scope,
which the operator opens.

| Risk | Observation that triggers | Response |
|---|---|---|
| Playback diverges from the writer | the S1 model or metamorphic oracle disagrees after layer 2 | S1 stays unmerged; report; the operator decides the fallback |
| Boundary notes break continuation | any repeat of rd7.2 case 5 or 6, or Paper case 2, is refused, degraded or does not continue | the boundary stays disabled on that route |
| The A1 live check fails | the §3 fail readings | new bead and unit; the step stays disabled by policy for trials; A1's code is kept |
| The closing reply misleads models | unobserved until rd7.3; mock-only until then | stated as unobserved in proposal 2 |
| Request-local view rows are ignored | over at least 5 `context_view` calls in the pilot and A1 live check, no following `context_manage` names a handle shown only in the rows | deliver rows as bounded output on that profile |
| The step is noise | in the first batch, at least 90% of at least 10 reviews decline, or more than 50% of step-archived units are fetched back within 5 rounds | stop the step arm for a brief redesign with a1v.2 |
| The step harms completion | the §7.4 sign test reaches its threshold | stop the step arm on that route; report |
| C4 would fire routinely | the reader counts provider refusals matching overflow patterns; more than one in ten runs on a route | retune levels or the brief before more batches |
| Cuts cost more than they save | per-chapter input shows no route where cuts repay within the observed run lengths, with no completion gain | keep cuts for headroom only; report |
| Anthropic paths ship unobserved | no account by proposal 2 | listed as `unobserved` in the profile files and proposal 2 |
| Quota runs out mid-batch | a 429 or plan-limit error class in the reader | pause; keep the runs; resume when quota returns |
| Gate time crowds out work | gates wait behind other sessions for hours | fewer landed commits per unit where steps can combine; never skip a gate |
| Host process-table stalls | `proc-stall-watch` logs listings over 2 s during a gate | rerun the gate after the stall source is closed; recorded |
| The profile reader is attackable | the fuzzer finds a crash after layer 2 | P1a stays unmerged; report |

These thresholds are decision points for the operator's attention, not performance claims.

## 9. What this plan does not do

- It does not change phase membership: 5st, campaign-defects and nls.16 move when the operator moves
  them. It does state that 5st waits for integration proposal 1.
- It does not use OpenRouter or the OpenAI API-key route, for which no account exists.
- It does not build P3 or K1's Anthropic part, run Anthropic probes or run Anthropic trials without an
  account.
- It does not run fuzzing or mutation on the laptop.
- It does not design C6b in detail, tailor briefs per model (a1v.2) or build the pattern library
  (ib8.4).
- It does not add a general eval harness; evals stay deferred under AGENTS.md.
- It does not install program-branch builds over the operator's generation or open the operator's
  sessions with them.
- It does not integrate into master without the operator's explicit decision.
- It does not add a third hardening layer, a certification step or a performance gate.
- It does not touch the uncommitted 4by changes in the `clm-a1v` worktree.

## Appendix A. Plan review dispositions

**SR** = `plan-review/sequencing_resources.md`, **TM** = `plan-review/tests_migration_risk.md`.

| Finding | Disposition | What changed, where |
|---|---|---|
| SR-H1 lanes not file-disjoint; ungated merges | accepted | §1.1 one serial lane for the shared files; parallel lanes only for disjoint files; §5.2 every merge through the hook; T1 after S2; journal at merge |
| SR-H2 critical path omits R1; A1 exit on a probe | accepted | §1.2 path with R1 and R0; builder unit B; the A1 live check separate from A1's exit |
| SR-H3 trial signals unowned | accepted | unit Q1; A1 and R1 set their `purpose`; R0 only audits |
| SR-H4 one-gate control does not exist | accepted | §5.1 the host rule with a sampler during the gate |
| SR-H5 OpenRouter has no account | accepted | §4.2 ChatGPT route only; API-key rows unprobed |
| SR-H6 P2 versus 5st | accepted | §1.4 P1 → proposal 1 → 5st → P2; P2's oracle at its own base |
| SR-M1 S3a/S3b boundary | accepted | turn state in S3a; rebase and pending playback in S3b |
| SR-M2 gate arithmetic | accepted | red tests with green code; §5.4 from measured attempts |
| SR-M3 profiles not shipped; fixture outside the repository | accepted | P1a ships; P1b refuses and checks a real install; V1's fixture generator |
| SR-M4 A2's checks | accepted | A2 blocked; `tsan` as its first step; auth race named |
| SR-M5 fuzz runs | accepted | Neuroses only |
| SR-M6 matrix not grounded; Jev cell | accepted | §7.2 calibration; §7.3 tasks, Jev env file |
| SR-M7 staying current with master | accepted | §5.2 master merges at boundaries; §5.3 model-visible changes |
| SR-M8 S0 drops session switch | accepted | S0's exit list |
| SR-M9 fixture does not match C5; workspace | accepted | §4.1 fixture, cut point, workspace restored to it |
| SR-L1 notes path | accepted | `context/clm-rd7-notes/` |
| SR-L2 inconsistencies | accepted | fixed throughout |
| SR-L3 spend arithmetic | accepted | ranges; no closed dollar total |
| SR-L4 kill criteria beyond two layers | accepted | §8 |
| SR-L5 nesting check too late | accepted | §1.3 |
| SR-L6 program binaries forward-only | accepted | §6.3 |
| SR-L7 `run-one` checkout | accepted | §7.3 |
| TM-H1 installed generations get no profiles | accepted | P1a shipping; P1b refusal and real-install test |
| TM-H2 first batch cannot exercise cuts | accepted | §7.1 prerequisite route; Paper probe; calibration and pilot |
| TM-H3 S0 misses exits; red test passes on old code | accepted | S0 exits; on-disk oracle; S0b for SIGTERM |
| TM-H4 no removal or prefix oracle | accepted | S3a removal model oracle and prefix property |
| TM-M1 prefix and rebind cuts from more units | accepted | §6.1, §6.2; per-run pinning |
| TM-M2 downgrade overstated | accepted | §6.3; format-level record; log-channel recovery event |
| TM-M3 changed-test lists | accepted | S3b and E1 lists; staging rule; pre-work grep |
| TM-M4 P2 and the must-fail rule | accepted | §2.3; P2 table at its base |
| TM-M5 A1 oracle compares against the code under test | accepted | A1 decline/accept twin runs; post-settle against the cut model |
| TM-M6 live checks and probes lack criteria | accepted | §4.1-§4.2 readings, cut point, baseline, repeats; A1 live pass and fail |
| TM-M7 unobservable kill criteria; missing risks | accepted | §8 |
| TM-M8 no question; underpowered | accepted | §7.1 one question with an arm contrast on the step only; §7.4 exact thresholds and stated power |
| TM-M9 arm configuration and provenance | accepted | §7.3 |
| TM-M10 v1 mapping invalid | accepted | E1 |
| TM-L1 Linux runs for K1 and S0 | accepted | §2.2 |
| TM-L2 K1 on the trial path | accepted | §1.2, named as a deviation |
| TM-L3 raw action codes | accepted | S1 |
| TM-L4 ib8.1 only on a mock | accepted | R0's three sources |
| TM-L5 earlier archives not listed | accepted | §6.1 |
| TM-L6 ceremony | accepted | S1 fuzzes decode; R0 reports by name |
| TM-L7 vacuous "fails today" | accepted | §2.1, §2.3 |
| TM-L8 integration point 1 | accepted | §5.3 |
| TM-L9 pending `select` | accepted | S3b converts at staging |

### Plan recheck fixes

| Recheck item | What changed |
|---|---|
| Owner 1: Anthropic-only paths | §1.4: S3b's closing reply, `drop_after_change`, and the V1 and E1 retaining paths built against mocks with rows at `basis = "unobserved"` (in the files, `/profile` and proposal 2); P3 and K1's Anthropic part deferred to the tail, unblocked by an account |
| B1 / owner 3: S3a exit waited on probes | S3a's exit rests on its own oracles; probe bodies for cut-composition cases only (rd7.2 1-5 and 8, Paper 1-2) become regression fixtures in the probe-row steps; note text through a shared or provisional template |
| B2 / owner 2: 5st base | §1.4 and §5.3: proposal 1 right after P1b, carrying S0, S0b, P1 and the a1v units; 5st waits for its acceptance; the serial lane pauses while 5st edits shared files; the campaign-defects dependency stated |
| B3 / owner 11: P1a/P1b | refusal and three-location check moved to P1b with a real scratch-prefix install; P1a keeps what the fake-cmake script can check |
| B4 / owner 6: probe-row commits | §4.5 probe-row steps, after the field's owning unit, budgeted in §5.4; probe reports on `work/clm-rd7-probes` |
| B5 / owner 4: gate overlap | §5.1 sampler every 5 s during the gate (processes and load), holder timestamps after; flakes during overlap attributed to the overlap |
| B6 / owner 5: SIGTERM | new unit S0b: a SIGTERM handler on the `RawCommandInterrupt` pattern, with tests; SIGHUP listed as unclean |
| B7 / owner 12: Paper probe | §4.3 case 1 archives closed rounds inside the open current turn |
| B8 / owner 10: profile pinning | P1b's explicit profile directory and expected-digest refusal; `run-one-clm` passes both and accepts only installed generations; the reader rejects digest changes between turns |
| B9 / owner 9: "continues" | §4.1 cut point at round 20, workspace equal to the seed (checked), baseline of three uncut runs for both "continues" and "degraded" |
| B10 / owner 8: A1 oracle | twin decline/accept runs; post-settle request against the cut model; breakpoints only under `allowed_tools`; `palette_only` clauses |
| B11 / owner 8: A1 live check | two-part task; "at least one, up to three" requests; failure → new bead and unit, step disabled by policy, §8 row |
| B12 / owner 7: trials | arms differ only in the step; primary outcome limited to step cuts; exact sign-test thresholds with stated power; levels from a separate calibration set; per-run profile pinning |
| B13: reader lane | R0 as a unit with worktree, branch, gates; final pass serial; §1.2 deviation named |
| B14: pilot circularity | calibration runs separate from the pilot |
| B14: probe token estimates | ranges at 30k-150k per request for rd7.2, Paper and rd7.3 |
| B14: case 6 enabling | §4.2 row sets `enabled = true` |
| B14: R1 `clm.recovery` test | added |
| B14: "direction 96 KiB" | written as `direct_bytes` |
| B14: P1b position, c3f | P1b kept before S1 for proposal 1; c3f moved to a parallel lane after S2 |
| B14: proposal downgrade cost | §5.3 states it for proposal 2; master reconciles before refusing `repair` and `cut` |
| B14: Q1 Linux run | added |
| B14: shared `run-one` | forked as `run-one-clm` |
| B14: master merges and mutation callers | re-grep on merges after S3b (§5.2) |
| B14: host reservation for the batch | §7.4 counted and scheduled after R0 |
| SR-M4 low: `tsan` entry wording | `tsan` is A2's first step, not an entry condition |
