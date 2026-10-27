# Systematic walkthrough and design requirements

## Method: whole episodes, not a list of cosmetic bugs

For each task ask: Can the operator form the intended action? Find its effective control? Identify the current target/mode? Tell whether input was received? Distinguish work from presentation? Recover/inspect without repeating effects? Return to the interrupted task? Repeat the questions while work is active, another view has focus, the display is constrained, output continues, and the session reopens. A source capability is not an actual-use pass. D means direct private observation, S source trace, H retained historical report, U unobserved here.

| Task | Present finding and consequence | Evidence / unresolved observation |
| --- | --- | --- |
| Begin or reopen work with known runtime | Managed launcher and saved Lua can select different generations; exact executable manifest missing | S + read-only process identity; D ordinary own-session reopen; U operator session's actual saved program |
| Compose multiline literal instructions | Enter submits; supported modified Enter/Ctrl-J newline; literal bracketed paste avoids controls | S/D paste-submit; U GUI IME, alternate keyboard, grapheme editing, slow unbracketed paste |
| Correct history without losing draft | Native history and kill/yank exist; up/down can cross draft line/history boundary | S; D draft retention on stop/reopen, not full edit/history task |
| Know whether one instruction was sent | Active YOU block and queue count exist, but queued per-item intention hidden; Starting not a progress frontier | D/S; U full inference lifecycle and actual user's duplicated-send episode |
| Deliberately repeat an instruction | Identical submits remain distinct and execute twice | D; any dedup policy must preserve that intention |
| Steer while partner works | Main turn's busy Enter is next-turn queue, not steering; `/send` targets participants, not current main inference | S; don't relabel queue as steer because references do it |
| Revise/remove queued work | `/queue`, stop-clears-queue, recovered drafts exist; no observed per-item correction instrument | D/S; preservation/withdrawal policy is a design decision, not permission to silently lose intentions |
| Interrupt current activity | Ordinary ESC fails expected action; Ctrl-C local Lua works; palette can consume Ctrl-C; private PTY owns bytes | D/S; U provider/storage/remote settlement and modal stop under load |
| Stop one child without undoing work | CommandJobs offers identity-based signal/stop; foreground/background differs from lifecycle | D native reaped/PTY byte ledger; U lost-custody/remote kill conditions, not retested |
| Understand a failing tool | Command stream visible, generic Lua failure diagnostic inadequate; lower process outcomes can overwhelm high-level exec | D/S; U reported generic tool-result serialization path |
| Scan many successful calls without losing partner reply | Tool-only head/tail folding exists; headers wrap; notices/assistant/Lua display don't share fold semantics | D/S; U realistic long multi-tool dialogue |
| Inspect a particular omitted original | Ctrl-O accesses selected command bytes, not a universal per-tool target; catalog can differ from transcript order | D one command; S wider adapters; U multi-page and retired-call recovery in actual-use study |
| Compare and return | Authored result lab and route-local anchors exist; selection, detail and draft focus differ | S/H; D inspector-to-draft focus without route close; U real comparison task with new results inserting |
| Scroll older work while output arrives | Display caches/scroll and inspector anchors exist, but folds/trimming may change rows | S/H; U sustained real reading/selection episode |
| Choose the right action by discovery | `/help`, `/keys`, slash palette and view picker exist; common operations still require JSON/Lua | S/H; D Ctrl-O hint visible in fold, narrow help clipped; U expert's unaided discoverability |
| Recover after local error | Generic error plus audit-retained message offers little task-specific explanation | D local Lua failure; U storage refusal/native failure recovery |
| Resize/dense work | Empty tasks pane still takes space; long arguments/IDs compete with status; narrow inspector loses detail | D100×30→42×16; U GUI font/contrast/tiny geometry profiles |
| Delegate and come back | Tasks/participants/commands are distinct; declared task active isn't native progress; badges may be stale/partial | S/H; U actual returning operator triage and wrong-target risk |
| Change settings or program while active | Deferred activation, saved override and effective source differ; `/workflow` isn't a generic mode toggle | S/H; U real operator editing/activation comprehension |
| Leave safely | Ctrl-Q preserves draft and exits own tests; child attachment and session custody differ | D; U blocked-output/editor/multiplexer real exit profiles |
| Use accessible/alternate terminal channels | Labels supplement color, optional graphics exist; screen-reader semantics not established | S; U screen-reader, low vision, motion sensitivity, light background and physical keyboard experience |

This broader walkthrough identifies hierarchy, diagnostic, mode, identity and continuity failures beyond the initial report. It is not evidence that every listed task is broken. Unobserved cases become explicit obligations, not invented failure counts.

## Priorities for the holistic design commission

Priorities reflect consequence to the operator's decisions, not implementation order or issue IDs. Existing trackers are incompatible and no new Beads IDs were created.

### P0 — Trustworthy control and intention lifecycle

**R1: Stop has a named target, scope and honest endpoint.** Choose explicit conflict semantics across composer, suggestions/palettes, view picker, inspector, active inference, queued prompts, foreground command, child PTY and native long calls. The operator must be able to discover and request usable turn interruption without abandoning their draft. Show local request acknowledgment separately from observed stopping/settlement, including unresolved remote effects and stop/completion races. Never claim cancellation is undo or signal acceptance means process death.

**R2: Every deliberate send remains identifiable.** Draft→locally received→queued or launched→context/request inclusion→finished/failed/interrupted must be mapped only where the actual owner supports the distinction. No invented remote read/delivery stage. The ordinary view must let the operator determine whether their particular input is already pending without opening a separate diagnostic command. Equal text in two real sends remains two intentions. A transport duplicate/receipt must not create another effect. Explicitly handle rejection/save failure with draft intact and “not accepted/not started” when known.

**R3: Work state survives attention shifts.** Current state must be distinguishable from historical notices, and the main turn from children/participants. A backgrounded command does not become complete because the turn ends. Starting/working/waiting/retrying/stopping labels need source-defined meanings and useful inspection when no progress frontier is available. Do not derive a percentage from elapsed time or a spinner.

### P1 — Readable evidence and continuous work

**R4: Default tool projection is compact, typed and attributable.** Show identity/purpose, relevant arguments, active/partial/terminal disposition, essential error/outcome and original availability. Preserve native fields and exact originals, but do not dump their serialization as the main reading interface. Separate command/log text, structured native values, file/source excerpts and rich model prose. Existing folding must be treated as a partial capability to preserve, not reimplemented blindly or generalized by coloring all text ACTIVITY.

**R5: Inspect the actual selected call without rerunning it.** A folded or clipped block needs discoverable keyboard and mouse paths to that call's full/ranged retained original, including completed, failed, interrupted, retired-resident and partially captured cases. State received/retained/selected/omitted extent and sealed/unsealed meaning. Catalog selection and output paging must not accidentally change each other or act on another row. Returning restores draft/cursor, selected object and reading context. Text copy remains exact and safe controls cannot execute in the terminal.

**R6: Reading is an explicit activity, not a race against output.** Follow-live differs from reading older evidence. Insertion, fold updates, resizing and trimming must preserve a surviving meaningful anchor or explicitly explain its loss. Provide a reliable return-to-live path and new-output indication without stealing focus. Consider immutable pins/comparison where needed; do not require every user to arrange a dashboard to read one error.

**R7: Diagnose at the right level.** Keep high-level exec/tool outcome distinct from lower input/poll/process operations. A Lua failure should expose its actual useful recorded cause or state that it is unavailable, not require guessing from external_unknown(0). A summary cannot erase applied-effect knowledge after formatting/retention failure. Generic inspectors need source linkage, not merely plausible labels.

### P2 — Broad coherence and expressive capacity

**R8: Discover effective controls and mode, not memorized folklore.** Explain send/newline/stop/return/inspection and active input ownership at the point of use; use supported terminal capability, not a hardcoded modern-key hint. Help must be readable without displacing a draft. Local navigation is not execution. Preserve expert scripting/composition and no-routine-approval policy; do not convert every operation into a mandatory form.

**R9: Layout degrades by task relevance.** Preserve identity/state/draft/focus/principal action before decorative/task metadata. Long arguments, IDs and structured values need deliberate clipping/expansion, not unmarked ambiguity or unlimited wrapping. Narrow list/detail navigation must remain useful when simultaneous detail disappears. Consider dense and constrained geometries together with color/motion/glyph/accessibility fallbacks. This requirement selects no particular layout/framework.

**R10: Reopen and change retain meaning.** Make saved-versus-effective source/settings and installed-versus-running generation inspectable. Show recovered queued intentions as drafts requiring explicit send. Preserve unresolved effects and original references across reopen; do not manufacture live custody. Prior conversation hidden on welcome needs an understandable resume path. Extension/reload failure leaves useful native controls and last-good state without repeating admitted effects.

## Alternatives requiring explicit decisions, not approvals supplied here

| Decision | Coherent alternatives and their costs |
| --- | --- |
| Escape precedence | First dismiss local layers then stop, or a defined turn-stop priority with separate local dismissal. Former protects editing but can conceal stop; latter is predictable for work but can disrupt inspection/child editing. Need explicit fallback under literal PTY focus and legacy ESC prefix ambiguity. |
| Queue correction/preservation | Inline per-item pending representation versus compact pending collection linked to the composer. Persistent legibility costs space; collection activation costs attention. Stop may withdraw, restore or preserve queue, but must say which; no silent new send. |
| Summary/detail relation | In-transcript expansion versus linked focused inspector versus both over one stable call reference. Expansion preserves locality but can overwhelm; inspector aids paging/comparison but adds activation/return cost. Both need one original owner, not duplicated retention. |
| Working versus settled previews | Stable head+tail, tail-follow while active, tool-specific important fields. Tail supports live diagnosis but moves; head-only hides final errors. A design must preserve reader mode and error salience, not copy a reference's line count. |
| Lifecycle vocabulary | A compact persistent per-intention mark plus expandable detail versus a more explicit activity rail. The contract matters more than the arrangement. Do not show internal stages the owner cannot substantiate. |
| Uninterruptible/native waits | Truthful unresolved stop/wait feedback versus stronger future owner mechanisms. Better UI cannot create preemption. Any storage/owner change is a separate design decision outside this study, not a reason to weaken durability. |

## Direct obligations for later design and implementation

These are proposed testable obligations, **not tests run or a plan already approved**. Each later implementation tranche chooses relevant cases; do not rerun all assurance families just to certify these words.

| Contract | Task/semantic oracle | Physical-PTY / readable-screen obligation |
| --- | --- | --- |
| One intention through delayed start | Independently assigned input identity links acquisition/acceptance/queue/launch/context inclusion; withheld admission does not look sent; intentional equal-text actions remain distinct | Delay owner start/source retention while composing; screen keeps particular pending input legible and the draft safe; operator can answer “already sent?” without repeated sends |
| Useful interruption | Pure Lua, provider wait/retry, long command, child input, native slow call, modal layer and completion race each preserve actual requested/observed states and prior effects | Real selected executable and recorded bytes: ESC policy and fallback effective; screen names scope, pending stop and actual outcome; no ghost input or accidental exit; provider tests require authorized safe fixture/live task |
| Command or generic tool detail | Same actual operation and original reference across model/Lua/human paths; bounded pages exact against independent bytes; retired catalog does not erase original | Select folded call among several, page middle/tail, copy exact content, return to same reading anchor/draft while output arrives; explicit omitted/truncated/unknown-extent marks |
| Readable default collapse | Tool-specific summary preserves essential failure/refusal/effect knowledge and no unsupported success; projection cannot alter originals | Readable named-style expectations for short/long, structured/text, failed/partial, narrow/wide; no huge argument header or low-level status flood hides partner reply |
| Stable focus/control target | Reorder/filter/retirement and delayed coordinate input retain actual identity or refuse safely; modal actions cannot fall through | PTY keys and mouse under resize/live insertion; independent target ledger, clear focus/selection/mode; child literal ledger distinct from harness controls |
| Responsive opportunity to act | Input acquisition, cancellation request, owner effect and visible acknowledgment measured separately; output backpressure cannot corrupt retained frame/originals | Stall own PTY output, request stop/edit/inspect then drain; control outcome before drain when promised, coherent final screen after; derive objectives from selected tasks, no arbitrary FPS/RSS claims |
| Continuity/recovery | Saved draft/cursor/recovered queue/definition generation exact; unknown predecessor effects never auto-replayed | Normal reopen and failed-source/retention cases retain understandable next action; initially hidden history has a discoverable reveal; no blank “Ready” interpreted as all prior work settled |
| Readable physical presentation | Expected byte→cell mapping, safe escape rendering, literal values and meaningful fallback independent of production renderer | Microsoft tui-test selected Ghostty/Alacritty backends plus readable expectations; real Ghostty/phux/font/keyboard/operator observations remain separate, including tiny/narrow/resize/selection and color/motion fallback |

Existing full suites, old screenshot baselines and source publication are useful but do not establish these holistic task claims. Later changes need old-behavior REDs where relevant, native semantic/physical/screen checks, original preservation and matched performance deltas under the established procedure. Do not reinterpret this as permission to run AFL, mutants, builds or a full-source gate in this documentation unit.
