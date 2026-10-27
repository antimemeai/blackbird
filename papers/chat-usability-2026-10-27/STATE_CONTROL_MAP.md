# Actual task, state, control and failure map

This map describes the studied source, with runtime observations labeled in [actual use](ACTUAL_USE.md). It is not an invented universal execution state machine. A message, turn, provider attempt, tool operation, child job and participant run have different owners and terminal meanings.

## Submission and work state

| Operator distinction | Actual owner / transition | Present feedback / ambiguity | Required semantic restraint |
| --- | --- | --- | --- |
| Draft | Composer text/cursor/history in terminal owner; UI state save to session; paste is literal until deliberate Send | Composer displays draft; Enter clears it into a submitted action; newline alternatives require supported encoding | Draft editing is not execution. Do not reinterpret paste controls as actions or silently submit restored work. |
| Send acquired | Composer::feed_impl returns submit with source; raw4010/4011 link input and handling | Composer empties and prompt enters history; this alone is not acceptance into model context | A visible local receipt may claim input acquisition only until the relevant acceptance is known. |
| Locally accepted/queued | TerminalUI queued UiPrompt vector and raw enqueue identity; busy branch validates candidate state; queued count; `/queue` reads this vector | No per-item YOU block until start; explicit duplicate bytes remain separate. Save failure gates new work; capacity refusal returns prompt to composer | Queue is next-turn intention, not in-flight steering, provider receipt or remote delivery. |
| Recovered rather than queued | Saved UI state recovers old queue as drafts; `/drafts`/`/draft N` load explicitly | Recovery listing can be out of view; load is not send | Reopen must not auto-run earlier pending intention. Preserve distinguishable recovered/draft/queued states. |
| Worker launched / active turn | TerminalUI start, raw transfer/jthread; busy=true, YOU block, activity=Starting | Source association can exist before engine program retention/evaluation; Starting remains during bare Lua computation in reproduction | Do not label launched as delivered, inference underway or useful progress. |
| Program source retained and evaluated | CodingEngine::turn retains source via AuditLog/ContextStore/journal, then creates Runtime/evaluates Lua | Usually only coarse activity; composition failure reaches full sync without evaluation | A program-source write is not execution. A spinner is not worker progress. |
| User objective accepted into context | main/engine ordinary turn path, retained context revisions; `/lua` is a different inline-program path | YOU display precedes some later acceptance steps; output may be Lua display rather than partner inference | Identify endpoint: locally recorded objective versus included in an actual provider request. Do not invent provider delivery acknowledgment. |
| Provider active/delivered | CodingEngine/provider transport owns request/attempt IDs, streamed originals, retry and accepted complete response | Provider/tool status callbacks; live text is a preview until complete acceptance | Transport receipt, billed usage, accepted response and model comprehension differ. Unknown remote state remains unknown. |
| Tool operation admitted / active | CodingEngine operation admission/outcome, concrete tool owner (LocalTools, CommandJobs, tasks, etc.) | status header; command stream; typed AttemptDisposition mapped to success/failure/muted outcome | Admission is not launch. Known refusal/not_started is not generic unknown. Never infer result from a check mark alone. |
| Child running/foreground/background | CommandJobs owns process group, PTY, wait epoch, retained output and control results; terminal only attaches input/wait | Foreground/PT Y hint and command-results/center; backgrounded exec can return while child lives | Turn completion is not child completion. Background is not stop. PID alone is not durable target identity. |
| Finished turn | Worker posts complete after perform; terminal joins custody, retires cancel origin, busy=false; summary chooses cancelled/failed/completed | Footer and top activity; a queued turn can start immediately afterward | No blanket “all work succeeded”: child/participants may remain, earlier tools may fail and external effects can remain unknown. |
| Failed | Actual error/outcome owners; worker failure presentation plus completion | ERROR and generic diagnostic; local Lua error reproduced as external_unknown(0) | Preserve native known/unknown and actual error. Failed rendering/result storage need not mean an effect did not occur. |
| Stop requested | Terminal cancel_turn sets sticky cancelled, notifies RawCommandWake; Ctrl-C also withdraws queued prompts; `/cancel` local path prints stop-requested | Stopping; later complete/interrupt text is separate | Requesting cancellation is not undo or proof of termination. Native blocking calls/remote effects may not be preemptible. |
| Interrupted/settled | Engine cancellation polling/Lua hook/tool cleanup and actual worker/child owner observations | Pure Lua interruption and selected native reaped command observed; other owners unobserved | Show scope and actual settlement; cancellation racing success must preserve both facts, not force a fake interrupted effect. |

Source anchors: terminal.cpp738ff,1217ff,1856ff,2203ff,2520ff,2700ff,3532ff,3717ff; main.cpp576ff and command dispatch; coding.cpp Runtime hooks, operation recording and turn source retention. These are separate transitions with real failure points, not a request to expose every raw event in the main view.

## Effective input routing and focus

| Context | Actual present routing | Failure or design pressure |
| --- | --- | --- |
| Ordinary composer, active turn | Enter queues, Ctrl-J/recognized modified Enter inserts newline, Ctrl-C requests stop/clears queue/preserves unsent draft; bare ESC is buffered then flushed | User expected ESC interruption does not exist. Old lone-ESC recovery repair must not be mistaken for that binding. |
| Slash suggestion / command palette | arrows/Tab/Enter choose or fill according to command metadata; ESC flush closes; Ctrl-C in explicit palette closes it and returns no cancel action | An apparently global stop key can be consumed locally. New design must resolve dismiss versus stop explicitly and visibly. |
| Startup question/choices | Composer choices route answer; question prevents queue dispatch until answered; invalid submission retained as draft/question | UI waiting for an answer is not model thinking. No guessed defaults or invisible input ownership. |
| Command inspector / center / result lab | separate route and local collection/detail/editor focus; arrows navigate; Tab changes focus; ESC timeout focuses draft; Ctrl-O/X/] toggle routes; Ctrl-\ opens view picker | Focus return is not route close. Source distinguishes identity selection, reading anchor and composer. Reproduction shows route persists after ESC. |
| View picker | modal choices select named local view; no model turn for route action | Old coordinates or changing lists must not retarget delayed effectful input; modal keys must not fall through to Send. |
| Foreground non-PTY command | Composer stays usable, Ctrl-B detaches wait; Ctrl-C cancels turn | “Foreground command” is attachment/wait state, not necessarily process-stdin focus. |
| Foreground private PTY | terminal batches literal keys to selected child/epoch; Ctrl-B is native return/background escape; private child received Ctrl-X/ESC/Ctrl-C exactly | Global-looking shortcuts are child input here. Need a visible, reachable harness stop without corrupting child editing or pretending text-input PTY is a full terminal emulator. |
| External editor | idle-only; drain output with existing finite handoff, restore terminal descriptors/modes, return draft | Failed editor/handoff must preserve draft and return path; no new editor execution in this study. |
| Native signal / exit | SIGINT owner wake and cancelled callback; Ctrl-Q/exits stop owned work and preserve recoverable drafts | Exit acceptance differs from observed process exit/terminal restoration. Never kill/restart operator UI to study this. |

A universal binding architecture is not selected. Required decision: whether ESC first dismisses local UI, stops work, or follows another explicit hierarchy, and how the operator can always request the intended scope. Consider terminal legacy ESC ambiguity, modifier prefixes, literal paste, repeated controls and child input. The chosen contract must preserve ordinary text and drafts, not introduce a hidden timing game.

## Inspection, reading and return

ChatView is a bounded derived display, not an expandable retained database. Its tool-only fold deletes middle display text while recording retirement ranges. Tool header+head/tail and count are a useful preview; originals belong to command/audit owners. Existing Ctrl-O opens command-results catalog/detail, not a clicked transcript block's universal tool-result detail. `/inspect`/original tools expose retained evidence through other operations; those capabilities do not themselves make a human path discoverable.

The ordinary transcript uses scroll offset and trimming policies; inspector/center have their own selections and reading anchors. New output, folding or trim can change rendered rows. A stable original and a stable human reading location are separate promises. Detail pagination changes bytes while catalog pagination changes items; focus determines which Ctrl-Left/Right does. A design must label these operations and return to the same meaningful place/draft, not simply say “use arrows.”

On resize, native presenter/output own the pending packet/suffix and baseline; full viewport recovery must preserve their order. Reflow changes logical-to-physical rows. At narrow width the observed inspector suppresses detail. That can be a reasonable responsive alternative only with an operable, discoverable way into selected detail and back, identity/state/action preserved before decorative metadata. No new tiny-display or stalled-output qualification was run here.

Ordinary reopen restores saved draft/history/recovered inputs, not live process custody. Initial welcome may hide prior conversation; first deliberate input reveals retained chat. Saved Lua source may still name an older generation. Session continuity and runtime installation are distinct: a new managed current symlink cannot prove a running process changed, and source publication does not replace either.

## Failure/recovery map

| Failure frontier | What is known | Safe useful continuation / required feedback |
| --- | --- | --- |
| Queue/save/admission refusal | Source has explicit retained-draft/save failure paths; new work waits | Say not accepted/not started when known; preserve text and allow independent reading/control. Don't make an empty composer look sent. |
| Program retention / semantic-audit sync | Composition original reaches fd12 full-sync request; return/evaluation unrecorded | Keep stop scope and unresolved progress visible; capture/native diagnosis needed for causality. Do not relax storage or deadline to improve the screen. |
| Provider retry/partial stream | Transport owns per-attempt originals/dispositions, acceptance only after complete response | Local retrying/partial differs from final failure and unknown remote effects. Do not re-run Lua to inspect a partial. |
| Tool timeout/cancel | Command custody cleanup can end local child group while effects already occurred | Show actual timeout marker, partial retained output and effect unknown where applicable. Offer inspect, not implicit replay. |
| Resident job retired | Catalog may omit old safely settled run; audit output_ref remains | Distinguish absent from resident view versus missing original; direct retained paging must work without relaunch. |
| Presentation clipped/folded | Derived content omitted; original availability/extent belongs to owner | Name omission direction/range, identify source and make full/ranged inspection reachable. Display omission is not “no output.” |
| Stale owner observation | Bounded adapters can retain last-good snapshots on contention/refusal | Label stale/partial/retired with scope; don't flatten unavailable into zero jobs. Actions need actual target validity. |
| Delayed/full terminal output | Existing nonblocking ChatOutput/presenter retains partial packet | Input acquisition and effect control may progress without visible acknowledgment; eventual coherent screen is separate. No dropping original/raw bytes or half escape suffix. |
| Lua extension/render failure | Separate authored composition can retain last-good scene; ordinary /lua errors are turn failures | Preserve native control/focus/draft; explain failed source/generation; no effect from redraw/reload or uncertain automatic retry. |
| Ordinary reopen with unknown predecessor work | Retained originals/settings remain; old queue is recovered draft | Tell what resumed, what did not, and what needs deliberate recovery. Reopening is not remote settlement or undo. |

“Absent”, “hidden”, “incoherent”, “stale” and “broken” describe different remediation problems. A generic failure badge cannot choose between them; an inventory or full-suite pass cannot settle the actual task either.
