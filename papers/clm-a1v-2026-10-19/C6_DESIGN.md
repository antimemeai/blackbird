# C6: harness-initiated, model-authored compaction step (design)

Bead `arconaut-a1v`. Operator decisions are in [PLAN.md](PLAN.md) under "C5 result and C6 decision". The source
facts come from [C6_SOURCE_MAP.md](C6_SOURCE_MAP.md), read at `1f28945`; references below use its anchors.

## What the step is

When a main request is about to be sent and the budget trigger is reached, Blackbird asks the model for one
compaction proposal in a **separate, single-shot request**. The model authors the change; the harness only decides
when to ask.

- **The request.** It sends:
  - the effective instructions, so the model knows the task;
  - the working map: every context entry with its ID, kind, tool-group membership, byte size and content;
  - a short imperative brief: "Your context is N bytes, over the trigger T. Propose one compaction toward G.
    Summarise or archive completed tool groups and finished exploration. Keep live instructions, the current
    task, and the most recent round. Preserve paths, decisions, errors and open questions in summaries."
  - exactly one tool, `context_manage`, which the model must call (forced tool choice where the provider
    supports it).
- **What comes back.** The response is read for that single call. Its arguments are the proposal. No tool is
  dispatched, and nothing from this request enters the main context (constraint 2). The request is still audited:
  raw capture, plus a semantic record linking it to the turn and attempt.
- **Applying it.** The proposal goes through the existing `ContextStore::manage` validation and is published at
  once by a new `settle` entry point (below). The badge from unit A blinks.
- **Outcomes.** A decline (no call or malformed arguments), a validation rejection, or a provider failure is
  audited, and the main request proceeds with the context as it was.

## When it runs

Only at points with no open tool group (source map §1).

**Phase 1 (C6a): synchronous at main-request entry.** At the entry of `request()`, before the protocol checks
pass the input along (around `src/coding.cpp:4277`), if `triggered` holds and no step is already due, the step
runs on the engine thread. This covers every turn, including turns with no long tool, and needs no concurrency.

**Phase 2 (C6b): concurrent during a tool wait.** When `CommandJobs::wait` (`command_jobs.cpp:1453-1474`) has
been waiting longer than a short grace period (500 ms) and the trigger is reached, a **compactor worker** starts
on its own thread:
- It has its own provider instance and an owner-thread binding, following participants and the backstop
  engine.
- It works from an immutable snapshot of the context taken at wait start. `Value`s are shared, so the snapshot
  costs no copy.
- The wait loop keeps polling the job unchanged.
- The worker's proposal is held until the next `request()` entry, where it is settled.
- If the turn ends first, it is settled at workflow end. If it is still in flight then, it is cancelled and
  audited.
- At most one worker runs at a time. It never touches `ContextStore`, `AuditLog` or the main provider's slots
  from its own thread.
- If phase 2 has already produced a proposal, phase 1 does not run a second request.

## Activation: the `settle` entry point

`ContextStore::settle(proposal, origin)` publishes a managed proposal mid-workflow, and only at a no-open-group
point.
- **Same checks as `publish_managed`:**
  - base/CAS;
  - `protect`;
  - no unanswered call (`context.cpp:1596`);
  - entries appended after the snapshot are carried forward (`:1550-1558`).
- **A stale or conflicting proposal** is rejected and audited, never silently rebased.
- **Contract change.** Today managed proposals activate only when a workflow succeeds. A settled compaction
  publishes immediately and **survives a later failure of the turn**. That is acceptable because originals are
  retained and restorable, and compaction settles no effects. It is documented in the tool description,
  `docs/CONTEXT_BUDGET.md` and the in-request guidance.
- **A pending proposal the model staged itself** takes precedence. If one is staged in the current workflow, the
  step does not run.

## Avoiding loops

- After a decline or rejection, the step does not run again until input bytes grow by a quarter of the trigger
  over the size at which it last ran.
- After a successful compaction, it runs again only when the trigger is reached anew.
- The audit counts each step and its outcome. The live-check stand-in in the plan reports how often the step
  runs per session.

## Provider caveats to verify, not assume

The source map could not confirm whether removing earlier entries mid-turn breaks provider continuity: Anthropic
thinking signatures, and OpenAI reasoning items paired with function calls. So:
- The working map marks the most recent round as not selectable; `manage` already refuses incomplete tool groups.
- One live check per provider family (luna/OpenAI first, then Paper, then the Anthropic route when its account
  works) runs a C5-style session to completion, checking that continuation still succeeds after a settled
  compaction.

## Brief variants

C6 ships one brief. `a1v.2` will tailor briefs to model strength.

## Direct checks (RED first, on the old code)

- **ContextStore:** `settle` publishes mid-workflow at a no-open-group point, refuses with an open group,
  rejects a stale base, and carries forward appended entries. A later failed `finish_workflow` keeps the
  settled revision.
- **Engine, with a mock provider:**
  - a triggered main request first sends one compaction request, whose input is the working map with a single
    forced `context_manage`, and then sends the main request with the compacted context;
  - a decline or malformed call leaves the context unchanged, is audited, and the main request still goes out;
  - the hysteresis suppresses a retry;
  - a model-staged proposal suppresses the step;
  - the compaction request's output never appears in the main context.
- **Phase 2:** a mock provider that blocks until released, plus a fake long exec:
  - the compaction request runs while the job waits;
  - the job's completion is still observed on time;
  - the proposal settles at the next request entry;
  - turn-end cancellation is audited.
  - Sanitizer runs (TSan) cover the worker.
- **PTY episode:** the real executable, with a fake provider fixture, shows the badge blinking after a settled
  compaction.
- **Live:** a C5 rerun on luna shows the step firing and the context staying near the target.

## Not in scope

- Per-model briefs (`a1v.2`).
- Overflow recovery (C4, which reuses this request path with windowing).
- Compaction of participants' contexts.

## Review resolution (one adversarial design review, 2026-10-19)

**Verdict:** proceed with C6a after the changes below. C6b becomes its own unit, designed after C6a's live
data. Changes adopted:

1. **Selection unit is the whole response round.** A round is the reasoning item, the message, the calls and
   their outputs. `manage` enforces it: summarising part of a round is rejected. Today `whole_groups`
   (`context.cpp:1149-1178`) counts only calls and outputs, so an OpenAI reasoning item can be orphaned. The
   harness also refuses the newest round. Direct check: export a compacted context through each adapter,
   covering OpenAI reasoning and Anthropic thinking.
2. **Tool choice is a per-provider capability.**
   - Anthropic rejects a forced `tool_choice` and always enables thinking (`anthropic_provider.cpp:479-509`).
   - OpenRouter's `require_parameters` narrows routing.
   - Where forcing is unsupported or unprobed, the request sends `auto` with the context tool, and any other
     answer counts as a decline.
   - Each provider is probed once in the live check.
3. **The compaction request is the main request's exact prefix plus one final developer brief.**
   - The brief carries a compact ID index: kind, round, bytes and a short head.
   - Encrypted payloads appear as sizes only.
   - The full working map is never re-serialised. This keeps the request no larger than the main one, and
     cache-friendly.
   - If the compaction request is itself refused for length, it hands over to C4 (and C4 does not fire twice).
4. **Failure handling.**
   - `interrupted` and `capacity` pass through and end the turn as usual.
   - The request uses `max_attempts=1` and no transcript preview.
   - It goes through `operation("provider")`, so the existing attempt and audit records cover it, including
     token usage.
5. **Settle order at `request()` entry:** validate, settle, recapture `items` and `origin`, validate again.
   The step is skipped when the request overrides `input` or `tools`, uses `state_context='omit'`, or comes from
   the backstop engine.
6. **Loop bounds.**
   - A success that leaves input at or above the trigger counts as a decline.
   - At most one step per turn.
   - Hysteresis is unchanged.
   - Summaries use the assistant role, so a later step can summarise them again rather than pinning them as
     developer entries.
7. **The contract change is sound.** A settle is one durable context append with no program record. The
   request guidance at `coding.cpp:4426-4430` and the docs are updated with it.
8. **The direct checks gain the review's list:**
   - export through each adapter;
   - a rejected forced `tool_choice`;
   - Ctrl-C during compaction;
   - crash after settle, then reopen;
   - each skip case;
   - a continuation append before settle;
   - the newest round refused;
   - the per-turn cap with pinned entries over the trigger;
   - no state leak on a retaining provider;
   - usage attribution.

**C6b (worker during tool waits), deferred.** It needs:
- snapshot-based validation in `settle`;
- its own provider construction (main.cpp builds providers today);
- its own stop flag;
- capture drained in the wait tick;
- a fix for the OpenAI codex auth-file race between concurrent requests (`openai.cpp:226-308`).
It is designed separately once C6a's live data shows how long steps take.

## Operator decisions on the review (2026-10-19)

- **Phasing.** C6a now; C6b designed next, with C6a's live timing data.
- **Tools: keep the palette, and grey out the other choices.** The model must never be offered a choice that
  will not be honored ("we cannot torture the model by letting it make choices that aren't honored").
  - **Native restriction available:** OpenAI Responses `tool_choice` `allowed_tools` with only `context_manage`
    allowed, or a named-function choice. The compaction request keeps the main tools list for the prompt cache,
    and the provider itself makes every other tool unselectable.
  - **No native restriction:** Anthropic, whose thinking forbids forced choice, and any provider whose probe
    fails. The request offers **only** `context_manage`, accepting the cache cost there.
  - **Declining** (answering without a call) is always a real, honored outcome. The brief says so plainly.
  - **Probes, not assumptions.** Each provider's capability is established by a direct probe (codex backend,
    Paper, OpenRouter, MiMo, Anthropic) and recorded with the provider settings. An unprobed provider gets the
    context-only palette.
- **The principle generalises.** Every harness-initiated request offers the model only actions that will be
  honored.
