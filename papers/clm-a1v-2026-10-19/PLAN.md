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

## C5 result and C6 decision (2026-10-19)

C5 ([C5_LIVE.md](C5_LIVE.md)): luna received the size line on every request and the
trigger guidance on 16 consecutive requests. It never compacted, and finished at
787 KB of input (26× the target). Advisory guidance alone does not work.

Operator decisions:
- **C6: a harness-initiated, model-authored compaction step.** When the trigger is
  reached, Blackbird runs a dedicated compaction request.
  - The model gets only the context tools (view, inspect, manage) and a short
    imperative brief, and answers with a `context_manage` proposal.
  - The proposal is audited, and the badge blinks.
  - A decline or a rejected proposal is audited, and work continues.
- **When it runs:** between tool rounds inside a turn, at boundaries with no open
  tool group, and especially while a tool runs, using that idle wait. This extends
  the activation rule, which today activates proposals only at workflow
  completion. The extension needs its own design.
- **Follow-up task:** different compaction instructions for models of different
  strength (bead filed). C6 ships one brief first.
- **C4 reuses this machinery** (a dedicated compaction request with restricted
  tools) for its windowed overflow recovery.

C6 follows the doctrine: a written design from source reading, one review, a plan,
then tests that fail on the old code before implementation.

## Operator decision: Blackbird owns compaction through context generations (2026-10-19)

The research found that Anthropic's newest models reject a client-shortened earlier tool_result for every later
thinking block. The operator rejects any reading of that which leaves compaction to providers alone.

The resolution is **context generations**. A model-authored CLM compaction starts a new provider-side conversation
from the compacted context: a new context generation ID, grouped under the same Blackbird session with lineage and
audit links. Earlier generations' thinking and reasoning blocks are not carried forward, so no provider's prefix
binding is violated.
- **Cost:** the prompt cache and prior hidden reasoning are lost once per cut.
- **Fit:** this matches "compact rarely, in batches".
- **Provider assists:** server-side clearing remains optional, never the authority.
- **Reuse:** the same mechanism serves C4 overflow recovery and mid-session model switches.

## Operator correction: compaction is selective archival (2026-10-19)

Compaction is **selective archival of roughly 5–30% of hot context**, not summarisation:
- whole response rounds leave the live context (round atomicity is already enforced);
- their originals are retained and fetchable by locator;
- no prose summary replaces them.

A context-generation cut carries the retained rounds verbatim plus a compact archive index (IDs, kinds, sizes,
locators). Whether the retained rounds' reasoning is kept, dropped or required at a cut is a per-provider profile rule:
- Anthropic's newest models: drop, because thinking is bound to its prefix;
- Gemini: keep (it validates only the current turn);
- DeepSeek-with-tools: must resend;
- OpenAI: pending a live probe.

Summarisation is at most optional and secondary. This supersedes the "summary" framing in C6_DESIGN.md: the C6
step's model proposal becomes an archival selection.

## Operator definition: compaction is a failure mode (2026-10-19)

**Compaction as traditionally understood is a failure mode for Blackbird.** That means replacing history
with a summary, whether a harness or a provider does it. When it fires, as in the C4 overflow backstop, it is
audited as a critical error that needs investigation. The nominal mechanism is **selective archival**, and a
context-generation cut that applies one is an **archival cut**. "Compaction" names only the failure, in
documents, designs, code names and UI. Existing uses to rename, each proposed for operator decision rather
than done:
- the C6 "compaction step" becomes an archival step;
- the `ContextChangeKind` "compacted" kind and the badge text "◆ CONTEXT compacted" become archived;
- the report's "compaction cut" becomes an archival cut;
- `compaction_campaign_test` keeps its name, since it tests the failure path.

## Operator direction: capability, not a target (2026-10-19)

The operator gave three directions:
- **Always available, escalate later.** Context management stays available to the model's judgment at all
  times. Blackbird nudges at the trigger and directs only later, at a higher level. The C6 harness step is a
  direction; the C4 compaction backstop is the failure.
- **Don't impose CLM every round.** The model needs enough context to understand what it can drop.
- **No fetish for sizes.** Never compacting, or holding context at some target, is not the aim. The aim is for
  the model to use these capabilities, and future ones, to make itself more effective for the moment and
  challenge it faces.

Consequences:
- Below the trigger, the C2 `CONTEXT_SIZE` line informs only. It no longer invites `context_view` or
  `context_manage`, a nudge that matches C5, where luna spent 646 KB on `context_view` while under the trigger.
  The test fails on the old code.
- The earlier suggestion to retune the C1 target toward a 30% cut is withdrawn: 5–30% describes typical
  archival, not a goal.
- The triggered `CONTEXT_BUDGET` wording ("propose managed context ... toward target_bytes") still frames the
  target as a goal. It needs rewording around effectiveness, as part of the `a1v.4`/C6 brief work.
