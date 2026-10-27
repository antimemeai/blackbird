# Historical reading contribution — scope

Context-only research reader, not an independent review or implementation worker. Native pinned executable selected openai/gpt-6.1-sol/medium; one --once objective supplied the entire 844,823-byte base JOURNAL plus research-only directions. Native request 872,934 bytes; observed returned usage177,562 input tokens/4,229 output tokens; normal exit0. No tools used in the returned run. Exact retained session/log: ROOT/chat-study-01/journal-reader-once-session and journal-reader-once.log. Reader reports full extent read. This contribution is historical interpretation, not present-runtime qualification; source anchors and direct observations in the main study control its current claims.

An earlier attempt incorrectly piped the multiline file to --plain without --once, which treated lines as separate objectives. Its own native process was terminated after detecting the error; the last remote request may be unknown. Original journal-reader-session/log remain intact, no prior effects replayed, and no operator process was touched. The distinct --once request below was a new explicit research action, not resumption or certification of the interrupted attempt. This study does not claim that failed ingestion read the journal.

# Historical digest: Blackbird chat usability, polish, and “stop-embarassing-ourselves”

**Reading extent:** I read the entire supplied journal, from its opening **2026-10-16 empty-task/interface repair** through the closing **2026-10-26 auxiliary-worktree retirement**, including the embedded **2026-10-27 continuity handoff**, repeated partial-work entries, and out-of-order historical material. No supplied extent was unread. I used no tools.

This is a digest of the journal’s evidence—not a present-runtime review, implementation plan, or new acceptance judgment.

## Central finding

The journal documents substantial chat/TUI delivery, but repeatedly supplies counterexamples to equating that delivery with usable operation:

- A successful self-development demonstration was initially called a milestone, then **the operator tried it and rejected daily readiness**.
- Composer, palette, folding, inspectors, mouse handling, physical rendering and session controls were delivered in successive slices, yet **the October 26 operator still reported confusing UI, ineffective Escape interruption and ambiguous sent state**.
- Source was frequently qualified or merged without replacing an operator executable. Several actual failures came from **launching an older generation**, not the source being discussed.
- Many apparent product regressions proved to be faulty terminal observers; conversely, **the renderer and its owned oracle sometimes shared the same defect**, so passing tests concealed visible corruption.
- Repeated whole-source gates consumed substantial work on unrelated or scheduling-sensitive failures. The operator repeatedly corrected this into **bounded remediation, one findings recheck, reuse of settled checks, and no certification chasing**.

The latest usability request is explicitly a **broader program**, not just a cosmetic ticket or the sum of its four initial complaints.

---

## 2026-09-29–30 — Recovered foundations and expert-operator intent

The initial assessment found inherited usability problems before reconstruction: a terminal wait cycle, disconnected session/mode/auth surfaces, stale tool caching, and provider/tool-conversation losses. Passing inherited tests did **not** establish that the inherited behavior was the desired specification.

The September 30 operator intent remains important context:

- Build for an advanced operator who wants programmable workflows and ordinary computing integration, with **almost no interest in command approval ceremony**.
- Provide autonomy with visibility and steering, rather than treating every action as a permission event.
- Preserve model agency over governing programs and working context.
- Distinguish the harness from shared computational services: Blackbird is their consumer, not their governor.
- Apply governing-program changes **after current turns/workflows conclude**, with an interrupt-and-apply-now action desired. The example Control+Enter gesture was **not** a selected shortcut.
- Make **pending / interrupting / applied** states legible.
- Retain originals so the model can repair destructive compaction.
- Make rageshake available without obliging the reporting model to repair the complaint immediately.

These were requirements and discussion corrections, not evidence that later UI or steering implementations fulfilled them.

## 2026-10-01 — Baby TUI accepted; demonstration readiness later contradicted

The proposed baby TUI included conversation, composer, output details, status, visible context manipulation, interruption/pending changes and outpost attachment. Its presentation loop was explicitly not execution authority.

The first coding vertical slice demonstrated actual source editing, red/build/green, restart continuity and model-authored context editing. The journal then declared a bounded coding milestone achieved.

**Inconvenient counterexample:** the operator subsequently reported that hello-world worked but the harness **was not ready to develop itself**. The earlier demonstration remained evidence; its readiness claim was superseded and transition acceptance reopened.

The resulting interactive slice delivered:

- Incremental assistant preview and final reconciliation.
- File/command details, streamed process output and visible errors.
- Persistent activity/timing.
- Multiline composer, bracketed paste, UTF-8 codepoint editing, history, scrolling, resize and queued prompts.
- A worker for the whole turn, separate from terminal presentation/input.
- Ctrl-C stopping work, clearing the runnable queue and preserving draft.

Specific corrections included:

- **Pasted Ctrl-C was incorrectly intercepted as cancellation.** Input classification moved into the composer.
- Idle Ctrl-C also preserves the draft; **Ctrl-U clears**.
- Completion wording changed to **“stop requested”**, avoiding an assertion that the effect had actually stopped.
- Long commands clipped critical queue/timing status; critical status moved ahead of command detail.
- Empty `argv:[]` with a command caused avoidable refusal and another model round trip; empty argv gained command fallback.

This slice was delivered locally, but the journal explicitly declined to reinstate daily-development readiness from the demo alone.

### `/exit` and recovery failures

The operator forced an exit after unexpected `/exit` behavior. Only `/quit` was recognized; **unknown slash commands were sent to the model**.

The correction added `/exit` to both clients and made unknown slash commands local errors with no provider request.

A separate historical bootstrap session had an unresolved provider attempt and poor debug-replay startup. Provider-only recovery was narrowed to recording unknown without replay; mixed or local-effect uncertainty remained fenced. The operator-specific historical error was not automatically identified by the bootstrap investigation.

## 2026-10-03 — Persistence, RRC, bounded observations and everyday comfort

Session persistence/RRC delivered saved model, effort, workflow and logical identities. Actual native replacement demonstrated stable conversation/workflow identity and one-time continuation injection.

Early limitations were explicit: drafts/queue were initially process-local; restart continuity was not arbitrary refit or general effect recovery.

A1–A6 then delivered practical chat support:

- **File ranges:** full originals retained; range slicing only affects presentation.
- **Bounded command output:** omission counts and audited retrieval without rerunning commands.
- **Request/context statistics:** serialized byte counts and actual returned usage, not token estimates.
- **Persistent draft/history:** queued prompts reopen as a recovered shelf, **not runnable work**.
- **Session discovery:** read-only listing from derived snapshots, with stale/missing legacy limitations.
- **Long-activity readability:** separate turn/tool timers and anchored scrolling.

Notable state semantics:

- `/draft N` loads; separate Enter submits.
- Ctrl-Q preserves unsent work.
- Ctrl-C deliberately clears runnable queue.
- Save failure warns and holds dispatch rather than silently losing pending work.
- Save-before-dispatch has an acknowledged crash gap: saved removal can precede execution. It avoids replay but does not prove completion.

Scroll anchoring needed repeated correction. One proposed `G-T` adjustment preserved a numeric index rather than the same content; another edit omitted the no-trim fallback and produced real drift.

These features completed their stated slices. Arbitrary Lua-error linkage and file/process recovery remained known defects.

## 2026-10-06–07 — Compaction, capacity failures and operational burden

Managed compaction delivered explicit staging, preserved originals, protocol-safe selection/archive/restore and actual post-RRC repair.

Important usability boundaries:

- **Staged is not applied.**
- Working-context reduction **does not reclaim the physical audit**.
- Small inspection limits initially still materialized enormous history and failed capacity.
- A pending restart note was not consumed by ordinary manual reopen; explicit continuation handling mattered.
- Two real physical-capacity stops required explicitly authored fresh working contexts. They were **not verified predecessor settlements**.

The operator repeatedly rejected expanding certification work and isolated prerequisites instead of useful outcomes. Master promotion was explicitly authorized with remaining hardening as backlog; that did not declare unfinished fixes correct.

The October 6 mascot work also foreshadowed later presentation churn: decorative delivery existed alongside a pre-existing tiny-terminal clamp defect, which remained separately tracked.

## 2026-10-07 — Chat-first baseline, composer, editor and palette

The operator adopted **chat-first / tools-secondary** UX.

Composer delivery included logical-line vertical movement, sticky cell column, explicit history via Ctrl-P/N, word/draft movement and bounded kill/yank. Wrapped-row movement, adaptive composition and external editing were not initially claimed complete.

Subsequent slices added:

- **External editor:** trusted VISUAL/EDITOR/vi, terminal handoff, bounded private file, failure retention, no implicit send.
- **Palette:** ranked local search, arrow selection, Enter loads only, Escape restores draft, bounded viewport and recovery shelf.

**Activation counterexample:** accepted UI source and a prepared UI executable were followed by a concurrent G4 build overwriting the shared release path with an unqualified candidate. The journal introduced a pinned UI path for operator launch. This was a concrete executable-selection failure, not merely documentation ambiguity.

### Startup and ordinary command ergonomics

A roughly 315 MB operator history reopened in about 12 seconds. After the operator rejected that as unacceptable, checksum and lookup work reduced an observed run to 1.3661 seconds. Ordinary cache/load remained uncontrolled.

Repeated model range-mixing errors were operationally material: they disrupted G2/G3/G8 work, and one station run failed eight bounded calls. The later exclusive range object was a direct ergonomic correction, preserving originals and honest audited failures.

## 2026-10-07 — Visual shell and native renderer evolution

Operator screenshots drove a sequence from slash hint wall to richer chat:

- Automatic inline slash menu for manually typed leading slash.
- Filtering, arrows, Tab fill, Enter run-or-fill.
- Recalled/pasted text does not open it.
- Escape preserves draft; modal palette remains distinct.
- Rounded composer border and adaptive one-to-six-row viewport.
- Actual physical terminal dimensions and minimal tiny-terminal fallback.

The later native renderer introduced typed roles/notices/tools/usage, fenced code, cached rows, changed-cell output, synchronized updates and nonblocking completion.

Its one tribunal found real defects across Unicode, footprint overwrite, source expansion, unfinished-line cost, output handoff, Escape deadlines, tiny-frame invalidation and restored roles.

Limits remained: clipping/eviction bounded presentation; eviction returned to live tail; surviving-anchor preservation remained backlog. Microfixture timings were not end-to-end responsiveness measurements.

### Artwork corrections

The journal records several operator corrections, not one settled mascot decision:

1. Qud sprite introduced, attributed, then withdrawn.
2. Original SR-71 silhouettes introduced and redrawn.
3. Operator rejected the redraw because a better attachment had already been supplied.
4. Exact supplied PNG adopted through terminal graphics, with cached relocation and cleanup.
5. Wordmark/layout subsequently revised.

These are useful evidence about respecting supplied assets and the difference between visual ambition and the operator’s intended result.

## 2026-10-08 — Tasks, session switching and collaboration

Task UI delivery used exactly two levels, stable IDs, explicit completion and separate observed run badges. The far-right pane sits below the sprite. Running badges are not resurrected as live work on reopen.

Specific visible failures/corrections:

- Slash selection’s black-on-cyan background leaked across rows; replaced with restrained foreground selection and explicit style restoration.
- `/new` was missing; delivered fresh sibling sessions without copying context/tasks/drafts/queue, retaining old sessions.
- Reopened task/conversation state was hidden behind welcome; existing working state now dismisses welcome.
- Tiny-viewport collaboration repair fixed stale rows and cursor bounds.

Collaboration delivered common colleague interfaces and bounded native participants, but one Claude review took **337 seconds** after an earlier 90-second unknown timeout. Successful eventual response did not retroactively settle the earlier attempt or establish model identity beyond reported metadata.

## 2026-10-09–14 — Steering and command ownership: control is not interruption

The steering investigation established:

- Busy main input queued another **whole workflow**.
- Provider request input was fixed.
- Participant directions applied after a request.
- Station steering applied at event boundaries.
- Cancellation unwound Lua continuation.

There was **no active main-turn steering inbox** or demonstrated subscription-endpoint live steering. Proposed steering remained discussion, then deferred.

Command background/foreground work delivered permanent ownership, private PTYs and attachment changes without treating backgrounding as process termination.

The later record preserves difficult failures:

- Signals queued behind blocked input.
- Control-result retention failure did not stop jobs.
- Station idle failed to pump settlement.
- Pasted control sequences under load were misclassified.
- Quit/foreground/resize/pressure fixtures repeatedly failed, sometimes because of observers or test subprocesses rather than product behavior.
- A real later cancellation escape existed in `CommandJobs::wait`: interruption before/inside its try path could leave launched work running.

The journal therefore does not support collapsing “Ctrl-C exists,” “stop requested,” “child joined,” “remote effect settled” and “Escape interrupts” into the same claim.

## 2026-10-15 — Interaction theory and enduring TUI doctrine

The operator explicitly rejected feature priorities without written theory.

The delivered theory treats an **episode of joint work** as the unit, including acting, arranging representations, delegating and changing instruments. It distinguishes:

- Instrument response from observed effect.
- Human attention, software queue pressure and model resource costs.
- Practical agency from generative agency.
- Learned stable meaning from merely attractive controls.
- Autonomous work from compulsory human feedback.

SR-71/NASA sources informed mode communication, crosschecks, feedback and task saturation—not cockpit imitation or imported aviation thresholds.

The subsequent doctrine/design package separated:

- Native local control and typed keyed scenes.
- Independently owned Lua composition.
- Human/model/Lua access.
- Target acquisition and frozen coordinates.
- Per-invocation identity and ambiguous acknowledgment.
- Literal insertion versus deliberate POWERWORD submission.
- Context/request/original repair as an explicit working surface.

These were design deliveries. Broader settings/team/fabric/capability/metrics views remained implementation work.

## 2026-10-15–17 — Regression tooling, inspectors, Lua instrument and physical ownership

Microsoft tui-test and Neovim-style readable screen expectations were explicitly adopted as **development tools**, not production dependencies.

A crucial counterexample followed: the native grapheme scanner and owned VT oracle both collapsed ordinary space/ZWJ chains incorrectly. Actual retained Ultramarine output reproduced pane corruption in **both Ghostty and Alacritty**. Pinned Unicode properties and independent physical cell expectations supplied the repair.

The same Ultramarine session exposed a separate command-lifecycle defect:

- 32 resident records exhausted command capacity.
- Two later commands were refused before launch.
- Generic settlement misleadingly presented unknown effect.
- The model invented interruption and retried.

Safe oldest-record retirement and **not-started/resource** outcomes corrected that class while retaining stronger fencing when result capture itself failed.

### Delivered local instruments

- **Ctrl-O command inspector:** local read-only command results, not a model turn or JSON transcript dump; native source references and bounded reads.
- **Ctrl-X current-session center:** task/participant observation using shared collections.
- **Ctrl-] Lua result instrument:** asynchronous preparation, frozen action targets and protected VM ownership.
- **Ctrl-\\ view picker:** shared navigation and draft preservation.
- Command catalog paging moved beyond the initial eight displayed entries.

Each had source delivery separate from merge and activation. The October 16 status explicitly said master did not yet include some Lua/viewport deliveries; October 17 later integrated them.

### Physical viewport correction

Research compared addressed deltas, blank-tail EL, corner wrap suppression and full repaint.

**Rejected counterexample:** corner suppression passed native byte tests but failed Ghostty’s bottom-right combining behavior.

The adopted physical-frame work fixed:

- Unowned final-column residue.
- Graphics removed by ED2 without cache restoration.
- Coalesced shrink/return damage lost while an older packet remained pending.

This did not settle deeper resize latency; that work was parked separately.

## 2026-10-17 — Installed generation and effort questioning

A pathless Paper launch selected an October 8 UI that did not understand `--provider`; another checkout binary also predated Paper. Fresh-session behavior already existed, but **the wrong generation was launched**.

The operator rejected silent effort fallback. The repair opened the harness and asked the intended effort, preserving draft/cursor and refusing provider dispatch until answered. A managed installed generation was activated and the exact Paper argv verified without inference.

This is unusually direct evidence that **launch selection, settings intent and runtime generation are chat-usability concerns**, not peripheral packaging details.

## 2026-10-19 — Mouse, modified Enter and tool folding

Operator choices:

- SGR mouse reporting to prevent Ghostty alternate-scroll wheel arrows from recalling history.
- Shift-drag for selection.
- `/mouse` and environment opt-out.
- Reporting disabled during foreground PTY, editor and exit.
- Shift/Alt/Ctrl Enter insert newline in supported encodings.
- Arrow history itself was already correct.

Tool output folding then adopted **header + first three + last three**, with Ctrl-O for the rest. Stream attribution, exit-marker accounting, fence parity, trim reset and finished-header state required correction.

The journal admits an earlier false report: a failed edit script meant trim reset was **not** actually applied until review caught it.

Another inconvenient detail: 4030’s recorded display behavior changed to rewritten text, while command originals remained separately available. “Pretty printing” must not be confused with byte-identical original presentation.

## 2026-10-23–27 — Latest usability request and current historical endpoint

October 23 found Oxcart’s retained runtime still tied to older source, despite newer publication.

October 26 orientation found substantial unmerged programs, archival unqualified checkpoints and stale issue backups. Beads access was incompatible with the legacy workspace; backups were not treated as authoritative completion.

The exact latest program, **“blackbird chat usability, polish, and stop-embarassing-ourselves,”** records:

- Confusing UI.
- Escape cannot interrupt the turn.
- Sent state unclear enough that four/five similar messages were sent.
- Tool-call output needs pretty printing, automatic collapse and inspection.

The journal explicitly says this list is **not exhaustive** and prior TUI theory/research does not prove current usability adequate.

Sequencing is also explicit: after safely settling the current continuity/composition units, this program is **next**, ahead of automatic S3b or general backlog advancement.

The requested P1 epic was **not created**: bd 1.3.1 refused the legacy 0.58.0 workspace. The saved brief is neither a replacement tracker nor successful filing.

At the supplied endpoint:

- Continuity source was delivered on its work branch, **not merged or activated**.
- Composition remained in a focused diagnostic successor after 309/310, with missing composer-history/Starting behavior unexplained.
- One unchanged targeted pass did not explain the historical timeout.
- The obsolete 64-step default had also stopped work; an explicitly selected uncapped campaign workflow was checked, but repository-default removal remained separate.

## Deferred findings most directly relevant to the usability program

The journal leaves these materially unresolved rather than silently successful:

- **Clear submission/queue/active/accepted/completed state**, especially the October 26 duplicate-message complaint.
- **Escape interruption semantics** versus existing Ctrl-C, palette dismissal and incomplete-ESC recovery.
- General active-turn steering, distinct from queued next workflows.
- Pretty/collapsed tool output across actual runtime generations and all attributed streams.
- Async collection growth/navigation behavior; an idempotent repeated-End fixture workaround did not decide product behavior.
- Long metadata clipping markers and broader shared bindings/components.
- Quiet-host quit/settlement stalls and historical pressure timeouts without causal attribution.
- Resize/startup tails and real owner-loop costs; mixed measurements do not establish nonregression.
- Model-managed context’s current live behavior and the requested blinking indicator when it edits/compacts.
- The proposed full-output-out-of-model-request/on-demand expansion work.
- Broader context/settings/team/fabric/capability/metrics surfaces.
- Issue-store incompatibility and stale task status, which obstruct reporting and orientation.

**Bottom line:** the history supports retaining substantial delivered machinery, but not assuming its presence, tests, merge status or decorative polish resolves the operator’s current chat experience. The latest complaints are direct counterevidence, and the journal itself requires them to lead the next program.
