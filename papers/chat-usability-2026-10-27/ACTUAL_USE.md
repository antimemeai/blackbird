# Actual-use diagnosis: retained terminal observations

## Method and evidence custody

Two new private sessions use the pinned generation described in [inventory](INVENTORY.md). The research driver opens a real OS PTY, sets physical rows/columns, launches the native executable with explicit provider/model/effort, sends recorded bytes, captures terminal output verbatim and decodes it with the existing independent emitted-VT-subset parser. No model request occurs in these two sessions. `BLACKBIRD_AUTH_HOME` points to a new directory per observation. Commands print synthetic diagnostic labels, loop in interruptible Lua, run harmless shell/Python children and write only an owned child-input ledger. These are actual execution fixtures, not invented screen mockups or source-test results.

Local evidence root: `/Users/patrickbeam/projects/blackbird/context/autodev-2026-10-26/chat-study-01/` (ROOT/chat-study-01 below). Preserve originals:

| Evidence | Exact retained location |
| --- | --- |
| First driver | `observe.py` |
| Second driver | `observe-02.py` |
| Raw terminal streams | `actual-use-01/terminal.bin` (27,926 bytes), `actual-use-02/terminal.bin` |
| Input/phase/exit records | each observation's `events.jsonl`; hex preserves actual input; screen records carry raw-stream byte offsets and monotonic-relative times |
| Readable screen projections | each numbered `.txt` beside its stream; named phases below |
| Native session originals/raw audit | each observation's `session/`, never reopened except the explicitly observed ordinary reopen of session01 after exit |
| Child's independent literal input | `actual-use-02/child-input.bin` = bytes `18 1b 03` |

Five-second waits are finite observation/cleanup safeguards modeled on the existing physical-PTY fixtures, **not** new product acceptance deadlines, mission limits, performance measurements or proof that all controls respond within five seconds. Fixed observation dwells mean absence of a change is bounded to that interval. Text projections are real parsed output but omit GUI/font/color perception; raw bytes remain available. No timing comparison, RSS or throughput claim is made.

## Episode 1: send, work, stop, inspect, resize and reopen

At100×30, startup shows model/effort, a ready instrument and composer. The driver's bracketed paste then explicit Enter submits:

`/lua print("ACCEPTED_LOCAL"); while true do end`

The YOU block appears and BLACKBIRD prints ACCEPTED_LOCAL. This establishes actual Lua evaluation in this reproduction. The activity line still says **Starting**. Bare Escape (`1b`) is sent; after a0.7s observation dwell the turn remains active (screen `03-after-escape.txt`). There is no observed interruption acknowledgment. The source explains why: Composer::flush_escape closes palettes/clears the prefix but does not create InputAction::cancel. This is **absent turn-interrupt binding**, not an established regression of the repaired lone-ESC parser, and not proof that Ctrl-C or the native Lua cancellation hook is broken.

Two subsequent identical explicit submits (`/lua print("QUEUED_LOCAL_A")`) leave the ordinary transcript without two corresponding YOU entries; the top line says **queued 2** (`04-two-identical-queued.txt`). `/queue` exposes both entries as numbered historical notice text (`05-queue-inspection.txt`). The UI has real queue acceptance feedback, but it is a count, not a persistent per-submission representation. The operator's original four/five similar sends were not reproduced on their live conversation; this narrower case demonstrates how intentional duplicate bytes are accepted and how little the ordinary view says about each one.

The driver types KEEP_UNSENT_DRAFT and sends Ctrl-C (`03`). Screen06 shows **Turn interrupted; context/audit retained**, **Turn cancelled**, and the unsent draft intact. No QUEUED_LOCAL_A evaluation output appears in the retained subsequent screens; the source explicitly withdraws pending queue entries. Importantly, the earlier “Pending prompts:2” listing remains in the historical conversation while the current status no longer says queued2. A reader can mistake the historical notice for current queue state. Cancellation preserves existing work/originals, not undoing prior prints or making all external effects impossible.

Next, a harmless exec prints START_TOOL and24 numbered lines, then sleeps. Screen07 shows an automatic fold: header, START_TOOL/line1/line2, **19 more lines · Ctrl-O inspects**, then lines22–24. This installed behavior directly contradicts “there is no tool folding.” The header's single logical line wraps over several physical rows; even this folded block occupies substantial conversation space. The command's ID and foreground control hint are longer than the available left-pane width.

Ctrl-O opens COMMAND RESULTS (screen08): selected native command ID, running/native-owned state, retained194/received194 bytes, range0..194, sealed-no, and output beginning at START_TOOL. This is genuine retained inspection without rerunning the command. At this size the visible result cuts off at line19; complete data availability is not identical to complete visibility. No result-page navigation was exercised in this reproduction, so the observation does not qualify long multi-page inspection or correct selection among many calls.

Escape then Ctrl-C while this route is open leaves the command-results route visible (screen09), with selected command **settled | native reaped**. Escape's “draft” meaning is focus return, not necessarily closing the route. Ctrl-O is the explicit route toggle. The screen alone does not prove all cleanup/retention details or a remote effect outcome; it establishes the observed native command state. “sealed no” can coexist with native reaped, an explanation gap for a human who treats settled as all output finalized.

At42×16 (screen10), the command route keeps the selected row but hides detail and clips the help line before its full return instruction. REOPEN_DRAFT remains in the composer. Ctrl-Q exits0. After ordinary reopen of this **own** session (screen11), the draft is visible and the screen says Ready / Enter continues, with prior conversation initially hidden. A draft persisted; a GUI continuity or full transcript restoration claim would exceed this observation. Both observed native invocations exit0.

## Episode 2: identical intention delivery, failure and child ownership

A second fresh100×30 session submits a harmless command printing QUEUE_GATE then sleeping2s. Two identical `/lua print("SAME_INTENTION")` submissions queue. After the gate finishes, screen03 shows **two YOU blocks, two SAME_INTENTION outputs, and separate completed-turn summaries**. The queue is real next-turn execution; it does not silently collapse equal bytes. This is the reason a future solution must distinguish accidental duplicate transport/receipt from two deliberate actions, rather than implement text deduplication.

`/lua error("READ_ONLY_FAILURE")` produces screen04: **external_unknown (0); context/audit retained** and **Turn failed**. The deliberately supplied error text is visible in the YOU source but not a useful explanatory error in the result. We do not infer an uncertain external shell effect from the label: this is an observed generic user-facing diagnostic for this Lua failure. The next design needs a useful path from a failure summary to its recorded cause, preserving native error distinctions rather than replacing unknown with guessed success/failure.

A private raw-mode Python child launched through `exec{pty=true}` prints PTY_READY, then records exactly three bytes. While the UI labels the composer **PTY input … Ctrl-B background**, the driver sends Ctrl-X, Escape, Ctrl-C. The independent child ledger contains **18 1b 03** in that order, and PTY_RECORDED appears (screen05). Those bytes went to the child; Ctrl-X did not open center, Escape did not stop the main turn, and Ctrl-C was raw child input in this selected mode. Ctrl-B returns control/backgrounds the foreground wait (screen06); it is not termination or undo. The main turn subsequently reports completed although the exec result says backgrounded. This is not inherently a bug: turn completion and child lifetime have different owners. The screen needs to make the distinction usable.

The long quoted Python command wraps through many lines, and the result presents several **process completed** activity blocks, with PTY_READY/PTY_RECORDED attached directly to a completion line. This is a second, broader failure of hierarchy/attribution despite the command fold. More controls and lower-level operations can make the transcript harder to read rather than more truthful. This run exits0, no unrelated PID is signaled, and the private child ends normally.

## Classification, not blanket diagnosis

| Capability/report | Classification supported here |
| --- | --- |
| Esc turn interrupt in ordinary composer | **Absent binding** in traced source and direct private use; the operator's expected control fails. |
| Lone-ESC decoder recovery | **Present**; old parser defect is not a demonstrated explanation of current report. |
| Ctrl-C pure-Lua interruption/draft retention | **Present and observed** for one local case; provider/storage/remote cancellation remains unobserved. |
| Submission feedback | **Present but incoherent/incomplete for the task**: YOU on active launch, queued count and optional listing; no per-message lifecycle contract. |
| Existing command fold and Ctrl-O | **Present and observed**, limited to command-output path; not universal default-collapse or per-call expansion. |
| Busy Starting label after actual Lua evaluation | **Misleading/coarse state**, not proof the worker never started. |
| Historical `/queue` after stop | **Stale as a current-state interpretation**, not corrupt queue data; listing is historical transcript. |
| Narrow inspection/return | **Hidden detail/clipped discovery** at the tested geometry; keyboard focus route still exists. |
| Generic Lua error | **Inadequate diagnosis**, observed external_unknown(0) rather than explanatory failure text; not proof of an external effect. |
| Installed source parity | **Unknown**, not a known stale binary. The fold's presence rules out simply assuming the pre-fold runtime. |
| Operator's reported unreadable generic tool output | **Retained operating failure**, not fully reproduced by the command-only fold example. Need exact reported call/output path. |

## What remains unobserved

No actual operator inference/remote interruption, provider outage/retry, disk-stalled stop, failed retention, pipe-backpressured screen, multi-page full-output recovery, text selection/copy, external editor, screen reader, light theme, GUI modified keys, phux detach/reattach or realistic long reading episode was newly tested. No control-latency guarantee or universal responsiveness follows. Existing source/oracles inform required obligations, not actual-use passes. The original composition-gate failure supplies one real storage/control/feedback boundary; it is not repaired or rerun here.
