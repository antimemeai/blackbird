# Startup effort question

The operator rejected the silent max prototype. An incompatible requested effort
now remains explicitly pending while the harness opens. Paper presents low,
high and max through the existing choice palette. Up/Down selects; Enter answers.
Esc dismisses the palette while keeping the draft. Submitting ordinary text while
pending keeps that text in the composer and restores the question. Ctrl-Q exits.
Valid saved or explicitly supplied effort bypasses the question.

`SessionSettings::effort_pending` is retained in native audit settings and the
native discovery snapshot. Pending settings may retain an unrecognized effort
string, bounded to1024bytes; accepted settings still require a recognized value.
A valid intentional /effort answer alone clears pending. Quit/reopen preserves an
unanswered choice, its route and its draft. The title and /session show pending
state. Main dispatch independently refuses provider-capable work while pending;
the UI also holds queued/initial work. A pending restart is consumed only when
its continuation actually runs after resolution. No provider, billing or account
fallback occurs.

Plain interactive input prints the same supported-level question and accepts
/effort LEVEL while keeping its command loop alive. Once, application and station
execution without an answering surface return actionable --effort guidance before
provider work. This unit adds no application-interface question protocol.

The native `TerminalQuestion` uses the owned message queue and Composer choices.
Its answer action preserves draft/cursor and carries the selected command's exact
raw input bytes through existing storage-owner handoff. Rendering and questions
remain in the terminal owner; settings and provider dispatch remain in main.

## Direct checks

Question-specific RED is retained in ignored
`context/startup-generation-2026-10-17/question-red.log`: actual PTY launch exits
before its first harness frame with unsupported and unrelated session-recovery
advice. The new oracle requires an opened, responsive harness and an unanswered
pending configuration, rather than merely accepting a process exit.

The focused Debug C++23 checks are `session_startup`, `terminal`, `session_store`
and `tui_snapshots`. The real startup fixture uses a synthetic Paper descriptor,
a synthetic owned key and a transport that marks any forbidden provider dispatch.
It checks selection, preserved draft bytes, valid reopen, invalid startup and live
effort repair, unanswered quit/reopen, plain interactive resolution, intentional
one-shot refusal, preserved restart intent and answer-then-continue. PTY terminal
configuration is restored; Darwin's documented-in-the-existing-fixture PENDIN
queued-input bit is excluded from that comparison.

The readable `effort-question` expectation adds one selected-high palette and
hidden-draft baseline; all21 earlier expectations remain unchanged. Independent
native assertions verify that Enter emits an answer and preserves the draft.
Final focused result: **4/4passed in10.00s** after the final source changes.
The focused log is `context/startup-generation-2026-10-17/question-focused.log`.
The root owner's single review, full gate, scoped Linux qualification, matched
performance and installed activation are recorded in [the delivery results](RESULTS.md).
These direct synthetic checks make no claim about live inference or service
model-capability discovery.

## Bounded independent review fixes

The root owner's single source review found two continuation details. Deferred
resume input must keep its resume classification when resume_requested is true
before resume_turn becomes true. Plain input also needed to hold its initial
continuation until a valid answer, then dispatch it once. Both are corrected.

The new real-native plain RED opens a pending resume, quits unanswered, reopens,
answers high and repeats the valid answer. Before the fix it prints the answer
confirmation but never runs the local continuation. That RED is retained at
`context/startup-generation-2026-10-17/plain-resume-red.log`. The corrected oracle
requires exactly one continuation marker, no provider transport dispatch and no
remaining consumed restart intent. Modified C++ ranges were formatted with the
pinned LLVM23 clang-format; no unrelated format sweep was run.

Post-review focused result: **4/4 passed in10.33s** (session_startup7.67s),
recorded at `context/startup-generation-2026-10-17/question-review-green.log`.
This includes the plain exactly-once continuation regression. The owner returns
this bounded fix pass without another code review or full-gate duplication.

## Required full-gate integration-oracle updates

The full source gate reached259/263 passing tests. Four older actual-process
oracles still specified the superseded behavior: invalid effort throws and keeps
the prior accepted effort. Updated only those test expectations; production
fences remain unchanged.

Native Anthropic and registered Responses now assert the requested unsupported
effort plus effort_pending, the in-harness question, persistence across reopen,
and zero new requests or effects from ordinary prompts while pending. An explicit
valid answer repairs the setting before their existing real injected-transport
continuation. Existing tool-effect, signed-prefix/native-original, failure-tail,
cancellation and secret checks remain. One-shot unsupported effort still refuses
with actionable --effort guidance and preserves accepted settings.

The RRC fixture retains requested nonsense as pending across reopen, then clears
it with high while preserving actor/conversation/workflow identity. The raw Main
input fixture explicitly answers high after bogus: all five exact dispatch input
originals, both effort commands, their non-error outcomes, the malformed-name
error, unknown-command path, literal paste bytes and complete native-read source
remain asserted. This reconciliation found no new production defect.

Focused gate-feedback result: **5/5 passed in24.32s**:
anthropic_native, registered_responses_native, session_startup, rrc and
main_input_raw. Log: `context/startup-generation-2026-10-17/gate-oracle-recheck.log`.
This is required gate feedback within the existing bounded recheck, not another
review campaign. Root owns the remaining full gate and delivery.
