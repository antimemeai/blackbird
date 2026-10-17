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
The root owner performs the single independent source review, full gate, scoped
Linux qualification, matched common-path performance and installed activation.
These direct synthetic checks make no claim about live inference or service
model-capability discovery.
