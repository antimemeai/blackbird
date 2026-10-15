# A responsive, expressive Blackbird: evidence before the UX/HCI/BHI pivot

2026-10-15. Research supplement; implementation is not started by this document.
The operator wants a meticulously presented, extremely responsive C++/Lua TUI
with permissive controls, exotic bindings, MIDI/controller input, possible
haptics and useful density, followed by a UX/HCI/BHI investigation toward a
highly informed implementation path. BHI means bot-harness interaction/model
ergonomics. The [index](README.md) links the manifests, reports and limitations.

The central finding is that control responsiveness, expressive composition and
precise presentation have to be designed together. A renderer can be fast while
its caller waits for a slow terminal. An input thread can keep receiving keys
while cancellation sits behind obsolete display work. A densely packed view can
move the operator's target or make incomplete output look complete. Lua can make
the whole screen programmable while a non-yielding callback prevents the screen
from reacting. None of those is settled by choosing a framework or language.

These are source-grounded design consequences. No comparative performance run,
operator study or physical controller trial was performed. The corpus does not
establish a fastest or best TUI.

## What the new corpus contributes

| Design pressure | Particularly useful sources | Consequence to investigate |
| --- | --- | --- |
| Display work can obstruct control | Neovim overload recovery; btop runner waits; bottom consumer; Helix render lock | Trace the entire control path and each wait. Threads and async APIs alone do not establish independent progress. |
| Partially sent frames constrain replacement | Notcurses raster/write ownership; POSIX partial writes; Blackbird ChatOutput | Preserve the in-flight suffix and the state it will establish. Replace obsolete future presentation only at a coherent boundary. |
| Long tasks must return service to input | lnav deadline slices; Turbo Vision flush deadlines; Lite XL coroutine budget; uji local tasks | Bound actual work units and specify non-yielding callback behavior. Nominal frame cadence cannot preempt arbitrary work. |
| Lua can own expressive presentation | Ito composition/state/keyed lists/window; uji screen slots/toolbars; vis/Lite XL | Study small native primitives with owned semantic data and Lua composition; preserve Blackbird's nonblocking transport. |
| Input needs more semantics than shortcut strings | Kitty/fixterms; Kanata/QMK; Helix trie; Yazi continuations | Separate text, key events, timed gestures and named operations. Negotiate encodings; make prefix/focus/override state legible. |
| Controllers are richer than simulated keys | libremidi/RtMidi; Ableton takeover; liblo; SmartKnob | Represent values, relative motion, held controls, device lifetime and feedback explicitly. Share operations without erasing device semantics. |
| Density requires stable relationships | lnav linked views; tig evidence paths; Kakoune selections; btop PID focus | Maintain identity, target, history and return paths while data changes. Character occupancy is not a useful quality objective. |
| Discoverability can itself move or stall | Harlequin focused help; Yazi which-key; fzf previews; uji query generations | Preserve the help being read; reject stale results; show incomplete data and obsolete work honestly. |
| Display correctness depends on the terminal | Ghostty modes/graphemes; libvaxis negotiation/queue close; UAX11/29; xterm docs | Choose actual terminal/multiplexer profiles and failure behavior. Bytes, graphemes, columns and physical pixels differ. |
| Human and model ergonomics interact | SWE-agent ACI ablations; mixed initiative; distributed cognition; HAI guidelines | Study actions, observations, references, timing and correction for both participants; a pretty human screen cannot qualify the model interface. |

Detailed anchors, upstream quantitative parameters and caveats belong to the
five lane reports. Numbers in those source studies are not our measurements.

## Current Blackbird: preserve the progress already made

Read against baseline `626b51a` on 2026-10-15. The earlier October7 paper contains
historical presentation gaps; the following observations use current source.

| Current mechanism | Evidence | Remaining question |
| --- | --- | --- |
| Typed derived chat blocks, cached rows and incremental stream state | `include/blackbird/chat_view.hpp`, `ChatView`; `src/chat_view.cpp` | Determine actual cost for a small append, a width/style change, a large draft and deep-history navigation. Do not assume all retained history is reformatted on every key. |
| Compact native cells and delta painter | `ChatGrid`, `ChatPainter::prepare/commit` | Verify final cell correctness under wide/combined text, clipping, resize and interrupted transport before changing representation. |
| Single-writer nonblocking output with retained offset | `src/chat_view.cpp`, `ChatOutput::attach/start/flush`; max eight writes of at most8192bytes per flush call | This already handles partial progress and EAGAIN. Preserve the packet until completion and commit painter state afterward. Real key-to-display latency remains unmeasured. |
| Input, notification, output readiness and resize descriptors share polling | `src/terminal.cpp`, `TerminalUI::run` polling near2592; `terminal_custody.cpp` | The busy/idle timeout is not an unavoidable delay when a descriptor wakes. Expensive message application/layout between polls can still delay service; characterize actual batches. |
| Consecutive text/process updates merge in the incoming presentation queue | `TerminalUI::post` near1266; incoming swap/application near1973 | Whole swapped batches are processed before later polling. Examine maximum work/age and fairness during perpetual heterogeneous updates; do not infer a hang without running the case. |
| Tiny-window path, task-pane thresholds, palette, external editor and graphics | `src/terminal.cpp` render/input paths; current terminal PTY drivers | Treat them as existing capabilities. Determine intended behavior across custom layouts, controller focus and terminal feature degradation. |
| Selected legacy escape matching and contextual timeout handling | `Composer::feed_impl` near861; `flush_escape` near695; deadline near2754 | Source shows a lone ESC outside the palettes remains pending and can consume the next ordinary character. Enhanced CSI-u is not decoded. Characterize exact recovery/supported grammar before redesign. |
| Raw terminal mode disables canonical input, ISIG and IXON | `src/terminal_custody.cpp` near241 | This inspection does not identify Ctrl-S flow control as a current Blackbird freeze. Test the actual nested/multiplexer path when investigating a report. |

This research pass changes none of these mechanisms. The legacy ESC behavior
deserves a focused follow-up independently of the larger visual redesign. A
detected parser limitation is not evidence that Blackbird requests every enhanced
keyboard mode or that all historical input lag shares that cause.

## Candidate boundaries for discussion

These are hypotheses to pressure-test, not an adopted component design.

**Keep history, presentation and transport distinct.** Retained originals and
raw instrumentation remain exact according to their existing contracts. Derived
presentation may maintain a current snapshot, cache or supersedable update. A
partly emitted terminal packet remains ordered and intact. Do not use display
coalescing as permission to sample/drop audit or raw metric events.

**Give control a route independent of optional work.** Decide what must remain
operable during expensive formatting, preview generation, a full worker pool,
device bursts, stalled output and broken Lua. Acquisition, acknowledgment,
effect termination and visible acknowledgment are four different observations.
A native control route also needs named boundaries where long-running work can
yield, cancel or be contained. No unsupported hard real-time promise follows.

**Make actions independent of adapters.** A command should name the target and
operation so keyboard, controller, Lua and model can refer to the same effect.
Adapters retain their own text/key/device details. Held buttons and relative
encoder deltas are not interchangeable with the latest absolute slider value.
Help should name the active bindings, mode and target; conflicting or unavailable
bindings need an inspectable explanation. Exact syntax and precedence remain open.

**Compose presentation through a disciplined Lua boundary.** Ito is a concrete
reference for state-driven views, identity, focus and extension ownership. Decide
which native operations a view can call, how long work runs, what invalidates a
cache, and what happens when a callback errors or never yields. A coroutine budget
checked between resumes cannot interrupt the resume that never finishes. Specify
generation and lifetime rules for reloaded mappings and in-flight callbacks.

**Preserve what the operator is working on.** Stable target identity, selection,
scroll anchoring, linked summaries and return paths should survive asynchronous
insertions. A transient expanded view or a reading pause can expose more useful
detail than permanently drawing every status. Make incomplete, pending, failed,
unknown and stale states legible without presenting routine approval friction.

**Keep haptic feedback tied to observed state.** Hardware can confirm a physical
press, transport receipt, application acceptance or applied actuator state; those
mean different things. Define output feedback, host disconnect, focus change,
mapping replacement, stale target and held-pedal behavior. Keep the motor's local
control loop independent of terminal painting. No controller hardware is selected.

## The next UX/HCI/BHI pass

Start by choosing real operator tasks and recording their current failure modes.
This pack is a research basis for that discussion, not an implementation plan or
a preselected widget arrangement. The existing visual-redesign issue
`arconaut-os3` remains relevant; the richer operator brief adds controller and
model ergonomics beyond that earlier scope.

1. **Specify useful work and the visible/control vocabulary.** Candidate tasks:
   locate a blocked collaborator; inspect exact tool output while new output
   arrives; steer one of several workflows; interrupt and distinguish accepted
   stop from completed termination; select context originals; switch/reload a
   mapping or view while preserving the draft. Establish shared object identities
   and the information human/model each need to act correctly.
2. **Deepen the primary literature around those tasks.** Read the acquired papers'
   methods and limitations, finish distributed cognition and MIDI specifications,
   and acquire the shopping-list originals where useful. Add current ACI/model
   ergonomics research; keep an expert operator's agency and Blackbird's current
   no-routine-approval requirements explicit when interpreting HCI guidance.
3. **Characterize the failure mechanisms directly.** Select discriminating cases
   from the matrix below. Choose numerical budgets from intended behavior and
   measured traces; this document invents none. Compute durations/aggregates in
   the separate analysis process, following existing raw capture architecture.
4. **Compare coherent interaction alternatives.** Use identical tasks to compare
   linked overview/detail, focus expansion, command search/prefix help, target
   selection and controller gestures. Measure wrong-target actions, recovery,
   successful completion, effort and model round trips alongside latency/bytes.
   A screenshot or average FPS cannot resolve those questions.
5. **Then design and review the smallest useful vertical slice.** Ground the owned
   C++/Lua interfaces, render/input/lifetime contracts and direct oracles in these
   observations. Discuss a library only if a concrete adoption proposal survives
   comparison with owned machinery. No framework was adopted by acquisition.

| Discriminating case | What must be observed |
| --- | --- |
| Tool output floods a stalled PTY; operator requests stop | Input acquisition, native dispatch, action acknowledgment, effect state, queued bytes/age and final correct screen after drain. |
| A non-yielding Lua view or saturated worker pool | Whether control can progress, what feedback remains available and what containment actually stops. Do not mistake a timeout future for preemption. |
| Every input sequence split at every boundary, malformed/truncated CSI, interleaved replies | Expected text/events from an independently stated negotiated grammar; no stolen next character or stuck prefix. |
| MIDI bursts with press/release, relative deltas, hotplug and echoed feedback | Correct final value, preserved discrete transitions, no stuck hold, no feedback-triggered command and no stale-generation target. |
| Long history plus streamed Markdown while inspecting an old result | Stable identity/selection/scroll, bounded useful recomputation, exact available originals and truthful incomplete display. |
| Resize during a partial frame and a pending prefix/modal | Latest dimensions eventually visible, ordered terminal bytes, correct cursor/target and no stranded parser/mode. |
| Human/model follow summary to exact evidence and back | Same target/effect identity, usable result references, unambiguous status, manageable turns and recovery from errors. |
| Animation, rich graphics or controller feedback during another task | Useful state recognition versus attention cost, actual display timing and subjective control feel on selected hardware. |

The work is ready to pivot from acquisition into that task-centered discussion.
The acquired sources provide mechanisms and counterexamples; the operator's
work will determine which ones Blackbird must solve first.

## Subsequent explicit theory

The operator accepts the feedback-loop intuition in spirit and requires it to
become an explicit written theory with researchers and frumentarii. Read
[INTERACTION_THEORY](../../docs/INTERACTION_THEORY.md) and its
[primary-source research](../interaction-theory-2026-10-15/README.md). It adds
epistemic work on representations, generative agency, skilled instruments,
delegation, attention and continuity, including actual SR-71 controls and pilot
task saturation. The investigative cases above remain conditional examples,
not design priorities established by an implementation plan.

The theory research also records a current-source scope correction:
`src/coding.cpp` installs a Lua instruction-count hook and polls cancellation.
Current non-yielding pure-Lua workflows are not necessarily uninterruptible.
Long native callbacks and a future Lua UI composition path remain distinct
questions. Preserve the existing hook rather than infer its absence from
cooperative scheduling in another reference implementation.
