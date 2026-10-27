# Reference reading and synthesis

## Source custody and scope

Reuse before acquisition: the needed originals already exist. No network fetch, upstream agent execution, installer, third-party test or account-backed reference session was necessary. [references.json](references.json) records URL, author, exact revision, original archive path/date/license/SHA256 and restoration procedure for Pi, Codex, Crush and libvaxis. [reference-files.json](reference-files.json) compares each of seven studied source files byte-for-byte against its original ZIP member. Work copies have no nested `.git`; existing originals remain intact outside this paper tree. The primary corpus is under `/Users/patrickbeam/projects/blackbird/quarantine/`; original agent ZIPs are under `/Users/patrickbeam/projects/quarantine_proj/archives/`. Do not mistake the reused sources' acquisition dates for a claim about latest upstream releases.

The study makes concrete claims about those pinned bytes. Source behavior is not observed performance or proof of usability. Broad corpus assessments supplied leads and counterexamples, not permission to adopt their source, implementation language, dependencies, or backlog ranking.

## Chat/agent mechanisms actually inspected

### Pi — local expansion as an instrument; queue semantics as a cost

Pin `8ce69e9d2b171d173fe4b6b2b6256f1f4411e69d`, Mario Zechner and contributors, MIT.

`packages/coding-agent/src/modes/interactive/components/tool-execution.ts:43–179,244ff,330ff` separates executionStarted, argsComplete, isPartial, isError, toolCallId and expanded state. Its default `expanded=false` and ten-line generic fallback produce a “more lines” notice using the effective `app.tools.expand` key hint. The result MouseRegion toggles expansion on a left click only when a result exists. Renderers receive tool-call identity and partial/error state; the rendering component expressly does not execute tools or inspect parameter schemas.

**Transfer:** a compact summary should be about a particular call, remain truthful while partial, and carry an actionable expansion affordance. Expansion belongs to derived presentation, not execution. Error visibility should not depend on opening a huge result.

**Tradeoff/counterexample:** ten first lines can hide the decisive last error; unrestricted expansion can flood the same transcript. Custom renderers may omit important semantics or run expensive work. Blackbird needs original/ranged retrieval and independent control even if a renderer fails, not wholesale copying of Pi's component boundary. Ten lines is Pi's parameter, not a proposed Blackbird budget.

`interactive-mode.ts:2983–3030` routes Escape during streaming to queued-message restoration/abort, during Bash to abortBash, during Bash-mode editing to leaving that mode, and at an empty idle editor to a double-Escape action. The same file distinguishes steering from follow-up, exposes dequeue editing, and updates pending inputs from queue events (`3351ff`, `4370ff`, `4573ff`).

**Transfer:** keep queued intentions available for correction, and show whether input changes the current activity or waits for its end. **Counterexample:** one key with multiple temporal meanings needs a legible scope; returning a queue into an existing draft can create new ambiguity. Pi's steering policy cannot be advertised over Blackbird's next-turn queue without an actual owner and reviewed semantics.

### Codex — a pending submission is not an empty composer

Pin `08e2b58b07b8a423d6577b66fb7756e980b53dbf`, OpenAI and contributors, Apache-2.0.

`codex-rs/tui/src/chatwidget/input_queue.rs:1–75` centralizes queued inputs, pending steers, rejected steers, startup submission, `user_turn_pending_start`, pending client identity and recovery/suppress-autosend state. This is a concrete source vocabulary for the interval after submission but before TurnStarted. `chatwidget.rs:1324–1368` prefers submission client identity when matching committed messages; it explicitly allows older server receipts that lack IDs and fall back to a compare key.

**Transfer:** visible local receipt should retain a submission identity through later launch/response events. Two genuine submits with identical bytes remain two intentions; retransmission/duplicate receipt is a different case. **Counterexample:** content comparison is a compatibility fallback, not safe general deduplication. Blackbird should not add text suppression to hide its own missing feedback. The queue reducer is not evidence of exactly-once remote effects.

`bottom_pane/mod.rs:845–918,1700–1717` routes active modal input separately; configured interrupt remains available during a task even when the status row is hidden. Popups, shortcut help, selected agent commands and Vim insert Escape can prevent that interrupt path. Tests near3710ff explicitly expect Escape to emit Op::Interrupt during a running task.

**Transfer:** a named interrupt action should not depend on an indicator being visible. **Counterexample:** the exceptions are numerous and have cognitive cost. “Escape cancels” is false without a scope rule. Neither copying this precedence nor adding a second-press timer is an approved answer for Blackbird. Interrupt event emission is not termination of work.

### Crush — summary orientation changes between working and settled

Pin `76cc5c574e15072b15aaed0f4f843a5711fae0d9`, Charmbracelet and contributors, MIT.

`internal/ui/chat/shell.go:191–195,242–310` invalidates cached rendering when expansion toggles. A pending collapsed shell shows the tail, with “earlier lines” above it; a finished collapsed shell shows the head, with “more lines” below. It counts from the trimmed output to avoid mismatched elision counts, strips cursor controls, clips by terminal width and keeps horizontal offset explicit.

**Transfer:** working diagnosis often needs the latest output, while a settled summary needs a stable meaning and exact omission direction. Distinguish vertical folding from horizontal clipping. **Counterexample:** switching head/tail on completion can remove the line the operator was reading. A stable reading anchor and explicit follow-live mode may matter more than the superficially neat summary. Blackbird's current head-plus-tail avoids that particular switch but still moves the tail while new output arrives.

### libvaxis — event lifetime and shutdown are part of usability

Pin `d4c3b3666e0c090e2b5092ca09893c6db15e0b79`, Tim Culverhouse and contributors, MIT.

`src/Loop.zig:13–145` keeps a finite event queue and text cache whose queued key text outlives the reader. Stop closes the queue, interrupts native input where needed and cancels/joins the owned task. Buffered events drain before Closed/error. `postEvent` can block when full; `tryPostEvent` exposes pressure; resize notifications may be coalesced.

**Transfer:** the key path must preserve literal input and terminal restoration even while consumers stop, and expose failed input rather than silently drop it. **Counterexample:** a separate reader can block posting to a full queue. Concurrency is not itself a responsiveness guarantee. Superseding resize presentation does not authorize discarding submitted intentions, discrete controls, original output or partly written escape sequences. This source is a mechanism study, not a recommendation to adopt Zig/libvaxis.

## Authoritative primary HCI material

Originals reused from the prior interaction-theory acquisition. Exact hashes, URLs, versions and restore commands are in `papers/interaction-theory-2026-10-15/hci/MANIFEST.tsv`; originals are ignored PDFs in the primary repository. No PDFs or extracted third-party text are added to this tracked tree. These works are copyrighted primary publications, not implementation licenses. This unit's searchable PDF derivatives are retained under `ROOT/chat-study-01/reading/`.

1. **Beaudouin-Lafon, Instrumental Interaction (CHI2000 author preprint)**, URL `https://www.lri.fr/perso/~mbl/papers/CHI2000/paper.pdf`, SHA256 `a922c3693f1e43b497b9e1d3554feb0b112b106eb3ea7aba525f053e90810d8c`. Directly read extracted pp.1–5, especially the domain object/instrument/activation/reaction/feedback account pp.3–5. The author distinguishes the instrument reacting from the object responding; spatial and temporal activation both cost effort. A property box or shortcut is not automatically good: indirection, integration and compatibility support comparison rather than a universal ranking.

   **Blackbird application:** composer clearing is an instrument reaction. Queue acceptance, request dispatch and model response are different object facts. A Ctrl-O action requiring selection from an unrelated catalog has activation/translation cost. Showing everything inline avoids activation but can bury the next judgment. Compare alternatives on whole tasks instead of treating either always-inline or always-modal as a theorem.

2. **Hutchins, Hollan and Norman, Direct Manipulation Interfaces (1985 author-hosted scan)**, URL `https://hci.ucsd.edu/hollan/direct-manip.pdf`, SHA256 `d9f603b1ab0cdbcb7885c248bdee871e44089ec3f3a3d5892eaaff68648c979c`. Directly read extracted sections3.1–3.3 (printed pp.321–324) and conclusion/tradeoffs pp.336–337, using the prior HCI report to locate them. Semantic distance concerns whether the intention can be expressed and evaluated; articulatory distance concerns its form. The tank-level versus rate example shows how output can contain all required facts yet still require the user to reconstruct the relevant variable. The conclusion rejects directness as a panacea and preserves the power of symbolic description.

   **Blackbird application:** elapsed time plus Starting contains activity evidence but not the answer to “was my message received?” Expert knowledge of raw JSON/Lua does not erase that evaluation gulf. Conversely, replacing exact composable operations with fixed buttons could reduce agency. Familiarity learned through compensation is not evidence the mapping is good.

3. **Hollan, Hutchins and Kirsh, Distributed Cognition (2000 author revision)**, **Saltzer/Reed/Clark, End-to-End Arguments (1984)** and **Klein et al., Ten Challenges for Making Automation a Team Player (2004)** were consulted through the prior source-grounded HCI/systems reports, not newly claimed as full direct primary readings here. Their original versions and limitations remain in the theory pack. Their useful questions concern relationships across representations, endpoint knowledge and coordination cost. They do not establish that models have human beliefs or that visible text was noticed by its recipient. The present direct original reading supports narrower claims than “the literature validates this design.”

## Challenge to existing interaction theory

The theory's distinctions survive these observations, but their presence in a document did not ensure usable delivery:

- “Input acquired / accepted / effect observed” is not a screen contract until the actual owner and visible referent are specified. In the reproduction, the UI can keep saying Starting after Lua has printed a result and entered an infinite loop. Do not add imagined percentage progress to fix that mismatch.
- “Useful relationships” must include selection from the particular tool block to its originals. A universal Ctrl-O catalog can preserve bytes while leaving the human to reconstruct which command produced the block. The deployed command fold proves retention and discoverability are separate questions.
- “Stable control meaning” cannot be inferred from “ESC parser repaired.” The studied source has repaired escape flushing without a turn-interrupt action; this runtime episode shows no observed stop acknowledgment, not broad decoder-recovery qualification. Theory must predict and test task-level behavior, not rename a decoder pass as control success.
- “Density” must account for wrapped long identifiers/status arguments and an empty task pane consuming space. At42×16, the observed command route loses detail and clips its help line; preserving a data model is not preserving the operator's available judgment.
- “Continuity” is not simply restore all pixels. Draft restoration succeeded while prior conversation was initially hidden. The next design must explain how to resume the work, not merely demonstrate file persistence.

A discriminating future comparison should hold task/output/originals constant and vary coherent submission/stop/inspection models, measuring mistaken duplicate sends, wrong-target control, recovery and compensatory checking alongside subjective fluency. No numerical latency/workload limit follows from these sources; no performance superiority is claimed here.
