# Existing working surfaces: proposed component applications

2026-10-15. Read with the [doctrine](TUI_DOCTRINE.md), [components](TUI_COMPONENTS.md)
and [complete view map](TUI_VIEWS.md). Current APIs remain authoritative; this
proposes their composed presentation and identifies missing work. It does not
claim every operation below is already a rich interactive screen.

## Conversation, composer and tool results

Principal episode: express an intention, follow useful work, inspect a result,
steer or cancel, and continue without losing a draft or reading position.
Use a document surface for conversation, a native multiline editor for the draft,
a typed action palette, compact work ribbons and a selected-result inspector.

```text
 BLACKBIRD   project / session          model · effective program
 ----------------------------------------------------------------
 YOU   Inspect the renderer under backpressure.

 BLACKBIRD   I found a retained output suffix. ...
   ▸ read_file  src/chat_view.cpp     completed       [source]
   ▾ exec       renderer probe       running J       [inspect]
     … bounded live output with more available …
 ----------------------------------------------------------------
 [selected work]               [new work below / follow live]
 ╭ draft · editable while work runs ─────────────────────────────╮
 │ …                                                           │
 ╰─────────────────────────────────────────────────────────────╯
 effective contextual keys             actual activity / queue
```

Treat this as a spatial relationship, not prescribed dimensions, key labels,
palette or an assertion that a measured duration exists. Use semantic text/code/
result blocks rather than identify roles by parsing prose. Stream into the same
message identity. Keep commentary/final distinctions only where supplied. Tool
rows carry actual invocation/attempt/job identity; their visible state comes from
the corresponding owner. Expand bounded output or open its originals without
replaying a tool. Code gutters never contaminate copied code.

The draft survives popup dismissal, failed send, session action refusal and editor
handoff. Palette help shows effective bindings and command timing. Filling an
argument is distinct from invoking a command. Busy queued input shows identity,
position and actual disposition, using existing supported queue operations; it
does not silently turn ordinary typing into control of a foreground process.
Paste inserts literal text without dispatch. Deliberate submission retains current
bare-workflow and POWERWORD selection, including its existing matcher behavior in
quoted/code text; preserve original submitted triggers and effective definitions.
Claimed recovery includes exact available originals and real
omission counts, not guessed truncated content.

Following is a viewport mode. Scrolling history leaves the anchor stable and shows
new material. Focus/selection can move between result and draft without retargeting
an accepted operation. In narrow layouts inspector and history share the content
area; the draft remains recoverable. Tiny/zero geometry has native minimal behavior.

Current foundation: native composer/palette/history/editor, retained drafts,
ChatGrid/ChatPainter/ChatOutput and exact tool observations. Work remaining:
component extraction, typed conversation projection, rich reusable document/result
composition and explicit focus/inspector navigation. The October 7
[chat proposal](CHAT_PRESENTATION_PROPOSAL.md) retains useful craft detail; its
source diagnosis and numerical/layout assumptions must be checked against current
code. Native nonblocking partial-write output is already present.

## Tasks and activity

Principal episode: maintain an intention list, distinguish status from live
activity, and inspect the work associated with an item. Compose the current compact
pane from a keyed tree/table plus selection-linked inspector. Offer an expanded
collection view without making a permanent sidebar compulsory for every domain.

Preserve current task identities, one-level subtasks, explicit owner/blocker/note,
version guards and task-bound command activity. A successful command does not finish
a task; a completed subtask does not finish its parent. Show task state and observed
runtime activity separately. The inspector can open associated work and source
results through actual references, not title matching.

Keep focused ID and first visible anchor across edits. Filtered or archived rows
retire action targets explicitly. Editing uses a form with read version/base,
local draft and authoritative conflict response. A conflict preserves the user's
input beside current values for deliberate reconciliation. Busy mutations keep
their existing next-idle-boundary semantics until a backend change is delivered.
Navigation remains local while work runs. Model/Lua use existing `tasks_read` and
`tasks_edit`; the reusable tree does not add a dependency scheduler or cross-session
synchronizer. See [current tasks](TASKS.md).

## Running commands and private PTY input

Principal episode: inspect command output, leave a wait without killing a job,
return, supply deliberate stdin/signal and observe settlement. Compose a job
collection, source-linked output document, explicit input editor and process
inspector. Show foreground ownership, execution outcome and waiter disposition
as different facts. Stable job IDs survive leaving this view.

Foreground process input visibly replaces the conversation input mode with a
named target. Local exit-from-input, background, signal and stop actions are
separately named. Preserve ordered/deduplicated input, partial-write/refusal facts,
resize and launch-relative deadline semantics. Copying output is not resubmission.
Current private PTY output is escaped text, not a terminal emulator. Do not draw a
shell cursor that implies full-screen terminal behavior. External editor handoff
continues through actual terminal custody restoration.

Closing an output inspector does not kill a job. Active/unsettled jobs continue to
fence restart/session switch as current owners require. Unknown effects remain
unknown after recovery; no old-PID adoption. Human/model/Lua controls continue to
share the [command owner](COMMAND_JOBS.md). Components replace composition and
navigation, not job custody.

## Context, requests and original repair

Principal episode: discover what the model can see, deliberately change it and
recover a source after a bounded observation or compaction. Use a source collection,
document inspector, request comparison and edit/repair workbench. Distinguish
retained context, proposed edits, selected request input and final encoded request.

Rows name context item/source/revision, kind, available original and inclusion or
transformation relation. Show byte counts as bytes and reported tokens as reported
tokens. Inspect archived source by bounded locator. A missing original, expired
diagnostic and omitted display window are different states. A transformation names
its source and new representation; no fake reconstruction of unavailable content.

Use existing CLM edit/manage/inspect and original-repair authority. A staged edit
keeps its base revision; conflict preserves draft and shows the actual current
source. Ordinary context edits can affect the next request under their present
semantics. They do not automatically activate new governing program definitions.
Historical request inspection remains read-only even when its source is editable.
Link context metrics to this workbench; do not add a second context store.

## Sessions, drafts and restart

Principal episode: find or resume the right conversation with task/draft continuity,
or replace the executable without confusing recovery with fresh work. Compose the
existing searchable session choice, metadata inspector and recovered-draft list.
Show actual session identity and path on demand, effective model/workflow and known
in-flight restrictions. A label is not a session's unique identity.

Selecting a session previews its identity; switching uses the actual session owner
and preserves refusal reasons when active/unsettled work blocks it. A recovered
draft loads into the editor without automatic send; preserve the displaced draft
through current retention. New/rename/resume/restart are distinct actions with
applicable arguments. Restart status distinguishes requested handoff, saved state,
new executable and resumed work when observed; no claim that a rebuilt binary
already governs the running process.

Cross-session command-center navigation initially opens or selects an existing
supported session route. Multi-session live attachment is a separate backend
capability. Do not represent another session as writable just because its directory
can be enumerated. Existing [reopen rules](SESSION_REOPEN.md) remain authoritative.

## Models, accounts and program configuration

Principal episode: select an actual route, understand effective defaults, change
request/program policy and inspect authentication state without exposing secrets.
Compose model/account collection, typed settings editor and source/effective/pending
inspector. Show model and effort, account reference and provider route together
when the choice depends on them. Main-loop route availability differs from a
context-only colleague's wider provider support.

Sign-in/device/browser progress uses asynchronous status and keeps local navigation.
Show reference, expiration/refresh state where observed and actual failure. Never
paste credentials into transcript, metric source labels or model read results.
Explicit account/route changes use existing authority; no hidden fallback. Cache
policy and actual observed hit/charge remain different. Saved program proposals
show base revision and effective/pending state; a pending generation is not a
successful activated turn. See [provider/configuration behavior](TOKENOMICS.md).

## Programmable tools, workflows and decision calls

Principal episode: find callable machinery, inspect arguments/effective definition,
invoke it deliberately, inspect the result, then revise or save it for reuse.
Compose the same capability collection/workbench used by future integrations.
Native tools/modules/workflow definitions, program configuration and opt-in packages
retain their actual current distinctions. A context-only participant is not a
workflow task with arbitrary tools; a Lua callable workflow is not automatically a
durable continuation. The independent native host retains its own task/result/join
identities and current limits.

Decision calls show named model, typed input/choice/score shape, actual attempt,
result and explanation where supplied. Treat judgments as judgments with their
source, not deterministic permission or correctness certificates. An authored
repeatable invocation can become a recipe through the future fabric contract;
it does not retroactively change past results. See the
[fabric designs](TUI_FABRIC_VIEWS.md) for the unified future management surface.

## Traces, variables, HUD and diagnostics

Current `/trace`, `/variables`, `/correlate`, usage-reader and `/stats` observations
become entry adapters to the [metrics families](TUI_METRICS_VIEWS.md). Keep exact
query limits, source coverage and causal references. Existing operations remain
useful before rich charts are implemented. A dedicated HUD feed is selected
content; its display, model inclusion and actions remain separate choices.

A native fault/detail boundary can show extension errors, unavailable originals,
consumer faults and ordinary operation failures without stealing the draft or
freezing controls. Failure detail links the corresponding owner and exact evidence.
Do not append duplicate diagnostic JSON to normal conversation for every refresh.
Presentation recovery preserves source results; it does not re-execute effects.
