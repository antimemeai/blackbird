# Model-managed context: health check, investment and indicator

Bead `arconaut-a1v`, branch `work/clm-a1v-2026-10-19`, worktree `context/clm-a1v`,
from master `168bb26`. Operator 2026-10-19: does model-managed context still work,
it probably needs more investment, and when it happens a visual indicator should
blink.

## Findings (2026-10-19, read from source and records)

1. **The mechanism is intact and tested.**
   - The model has `context_view`, `context_edit`, `context_manage`,
     `context_inspect`, `context_restore` and `context_budget`
     (`src/tools.cpp:1175-1310`, dispatched in `src/coding.cpp:5136-5481`).
   - `tests/compaction_campaign_test.cpp` and `tests/context_budget_test.cpp` drive
     `CodingEngine` with a mock provider whose `function_call`s reach those
     tools. They passed in the 265-case gate of `9b9f855`.
   - The last live campaign was 2026-10-06 (papers/2026-10-06-compaction-campaign.md).
     It predates native values and BBM2, raw instrumentation, and the C++23/26
     upgrades.
2. **Nothing prompts the model to use it.**
   - `default_context_budget()` has `enabled = false` (`src/coding.cpp:2075`).
   - The CONTEXT_BUDGET guidance (`:4389`) appears only when the budget is
     enabled and its trigger is reached.
3. **Models do not use it unprompted.** Across all 39 autonomous runs of
   2026-10-17 (`~/projects/blackbird_runs/2026-10-17-cpp26`), no `context_*` tool
   was called. Some of those runs lasted nearly an hour.
4. **Nothing handles overflow.**
   - There is no model context-window knowledge: `context_window_tokens` is null by
     design.
   - No path recovers from a provider's input-too-long refusal, so a long
     unmanaged session ends at the provider.
   - The `5st` provider-settings table, unmerged on `work/defect-5st`, records
     output ceilings but not input windows.
5. **The guide is stale.** CONTEXT_BUDGET.md says inspect pages are hex-encoded
   JSON. `ContextStore::inspect` now offers `json` or `bbm2-stream`. Correct the
   guide from the source.
6. **Nothing tells the operator.** No TUI surface shows when the context changes.

## Work

- **A. Indicator.** `ContextStore` already publishes at three points: `edit`,
  `restore`, and `publish_managed` (when a managed proposal activates at
  workflow completion). Each publication emits a change, with its kind,
  revision and byte delta, through a callback, wired like `TaskState::changed`
  (engine, then `main.cpp`, then `TerminalUI`). The status line shows
  `◆ CONTEXT compacted −48 KiB` (or `edited`, `restored`), blinking through
  inverted ink for about 2 s, then steady until the next turn. Redraws are timed
  only while it blinks. Direct checks:
  - a `ContextStore` test covers each kind;
  - a PTY episode on the real executable covers `/compact` (or
    `context_manage` through `/lua`), the blink frames and the steady state;
  - both are RED on the old code first.
- **B. Correct the guide** from the source (finding 5).
- **C. Investment, for the operator to decide.** Candidates:
  - **C1.** Enable the budget by default, with defaults chosen from measured
    request sizes in the campaign records.
  - **C2.** Always give the model its current context size, not just when the
    trigger fires.
  - **C3.** Per-model input windows in the provider settings table, so the
    trigger can track the real limit.
  - **C4.** When the provider refuses an over-long input, start a model-authored
    compaction turn instead of failing.
  - **C5.** One supervised live session with the budget on and a small trigger:
    does the model follow the guidance, and is its summary sound? This needs a
    chosen provider and account.

## Operator decisions (2026-10-19)

Take C1, C2, C4 and C5, plus A and B, all on this branch. C5 runs on `luna`
(OpenAI), as in the 2026-10-17 campaign. Order: A, then C2 and C1 (both in the
request's context guidance), then C4, then B, then C5 on the finished build.
C3 is not chosen. Each unit has a written plan, tests that fail on the old code
first, one review and one recheck.

## C2 and C1: context size every request, budget on by default

- **C2.** The request's operational state (`src/coding.cpp`, after `budget_view`)
  gains one line on every request:
  `CONTEXT_SIZE input_bytes=N entries=M trigger_bytes=T target_bytes=G` (or
  `budget=off`), with the note that these are native packet bytes, not tokens,
  and that `context_view`/`context_manage` compact when useful. The full
  CONTEXT_BUDGET guidance still appears only when the trigger is reached.
  - The operational state is appended as the request's last developer item: the
    changing-state layout that keeps cache prefixes stable.
  - A provider that retains request state (`retain_request_state()`, today only
    Anthropic) also appends that item to the context. There the size line rides
    only on a state item already being sent (tasks or the trigger), so the
    context does not grow by one item per request.
  - `state_context = "omit"` still drops it.
- **C1.** `default_context_budget()` becomes enabled. Trigger and target come
  from the measured request sizes of the 2026-10-17 campaign (site 1301 wire
  bodies, analysis in progress), not invented. A session's explicit policy and
  `context_budget` proposals are unchanged.
- **Direct checks.** Request-assembly oracles assert the size line and its
  numbers, and that it is absent under `omit`. For a retaining provider, the line
  rides only with existing state. Existing exact-request expectations change
  deliberately, and each changed expectation fails on the old code. The default
  budget oracle in `context_budget_test` changes to the new defaults.

### C1 defaults from the measurements

[REQUEST_SIZES.md](REQUEST_SIZES.md) covers 762 requests across 39 runs.
- **Input sizes.** Requests grew steadily and were never compacted. Input-array
  p90 was 293–358 KiB and p99 409–730 KiB; the maximum was 770 KiB, for luna at
  141k tokens.
- **No refusals.** Nothing was refused for length at up to 148k tokens.
- **Bytes per token** (median) vary by provider: luna 6.8, paper 4.1, mimo 3.6.
- **Fixed cost.** About 27 KiB of every request is tool schemas and
  instructions.
- **Units.** The budget measures BBM2 input bytes, 5–10% below these wire sizes.

Defaults: **trigger 384 KiB, target 192 KiB, enabled.**
- In the measured runs the trigger fires only in the longest sessions (above the
  p90 of every model).
- 384 KiB is about 96k tokens on paper, 109k on mimo and 58k on luna, which
  leaves headroom under a 128k-token window on the densest provider.
- The target halves the context, so one compaction buys a long stretch, rather
  than shaving at every request.
- These are byte heuristics, not token limits. C4 handles a real refusal.

### C4 decision (operator, 2026-10-19)

Overflow should not arise once the nominal path (C1/C2) is tuned, so C4 is a
minimal backstop.
- **Option B only.** Send the pinned instructions plus the oldest complete tool
  groups that fit a byte budget, and have the model summarise them with
  `context_manage`. Repeat window by window, then retry the original request.
- **Bounds.** At most one recovery per request, a bounded number of windows, and
  a stop when a window makes no byte progress.
- **No fallback.** There is no harness-authored archive (option C). When the
  smallest window still overflows, recovery stops with a clear message and every
  original is kept.
- **A sign to retune.** If C4 fires in real use, the nominal path needs retuning.
- **Audit as a critical error (operator, 2026-10-19).** Every fired overflow
  recovery, and every recovery that stops, is written to the audit log as a
  critical error that requires investigation, not just logged as handled. It
  must use the audit's existing severity or complaint mechanism, after reading
  how that works, rather than a new one.
  - **Mechanism found:** the existing rageshake complaint (`src/tools.cpp:1157`,
    `src/coding.cpp:5168+`). It captures the context, program, model and audit
    identities immutably, attempts bead delivery and retains the complaint
    locally if that fails. On every fired recovery the harness raises one
    complaint itself, as the actor, marked critical. The complaint carries the
    failed request's reference, its sizes, the provider's refusal original and
    the recovery outcome.
