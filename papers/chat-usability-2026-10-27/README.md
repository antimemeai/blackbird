# blackbird chat usability, polish, and stop-embarassing-ourselves

## Source-grounded chat-usability study — 2026-10-27

This is the first conceptual unit of the [operator's program](../2026-10-26-chat-usability-program-brief.md): reference/UX research and actual-use diagnosis, **not implementation or approved holistic design**. The next commission must choose and pressure-test the whole interaction model and delivery plan before coding. No framework, layout, dependency, storage change or key-conflict policy is selected here.

**Central diagnosis:** Blackbird has substantial execution and inspection machinery, but its conversation does not consistently make that machinery into a legible working instrument. “The capability exists” answers the wrong question when the operator cannot determine what was received, what is running, what a key will stop, or how to inspect the particular output they just saw. The three operator reports are operating failures, not requests for three decorations.

A new private physical-PTY observation of the pinned installed executable shows no observed bare-Escape interruption of a pure-Lua turn during the recorded interval, whereas Ctrl-C interrupts it and preserves the draft. Two identical busy submissions become two queue entries, with only a count in the ordinary conversation until `/queue` is requested. A long command is already automatically head/tail-folded and its originals are accessible through Ctrl-O; that is an important counterexample to “folding is absent.” It is not proof that all tool results are readable, that Ctrl-O targets the relevant block, or that the operator's reported flood was imaginary. Details and real captures are in [actual use](ACTUAL_USE.md).

## Read this unit

1. [Identity, source inventory and reading scope](INVENTORY.md), with [file hashes](inventory.json).
2. [Primary references and critical synthesis](REFERENCES.md), [archive provenance/restoration](references.json), and [studied source-file hashes](reference-files.json).
3. [Actual-use method, captures, findings and limits](ACTUAL_USE.md).
4. [Actual owners, lifecycle, focus/control and failure map](STATE_CONTROL_MAP.md).
5. [Systematic task walkthrough, priorities, design decisions and direct obligations](REQUIREMENTS.md).
6. [Missing sources and observation gaps](SHOPPING_LIST.md).
7. [Independent review and remediation disposition](REVIEW.md), original [review](REVIEW_REPORT.md) and ONE [focused recheck](RECHECK_RESULT.md).
8. [Full historical-journal reading contribution](HISTORY_READING.md).

## Consequential conclusions

- **Stop is a cross-mode contract, not a decoder fix.** Lone-ESC parser recovery was implemented previously. Mapping recovered ESC to useful interruption is a different missing behavior. Palettes, inspector focus, view picker and child PTY ownership must have explicit, discoverable semantics. Cancellation is not undo; requested stop is not observed settlement.
- **Submission needs a persistent referent, not only an empty composer and a spinner.** Local UI acceptance, queued intention, worker launch, context acceptance, provider dispatch and response completion are different facts. “Delivered” cannot be a single unchecked badge. Never suppress two intentional messages just because their bytes match.
- **Collapse is a semantic projection plus a reliable path back.** The installed command fold works in the observed case. But its first logical line can wrap into several rows; Lua display is assistant text; notices and generic structured results do not become typed command-output blocks automatically. A job catalog is not a universal tool-result inspector.
- **Continuity is selective and must be explained.** Drafts survive observed ordinary reopen. The opening screen does not immediately expose all prior conversation. An old `/queue` listing remains visible after cancellation as historical text. These are opportunities for false current-state readings, not evidence of lost audit originals.
- **Animation is not progress.** The composition-gate failure reached semantic-audit full-sync request with no retained return or evaluation, while the UI kept drawing Starting. Kernel versus scheduling remains unknown. A targeted pass does not repair the failed309/310 gate. Better feedback must retain that uncertainty rather than weaken durability/deadlines.

## What this does not establish

No operator live UI was controlled. New private fixtures use local /lua paths with no request invocation and an isolated auth home; absence of provider calls is source-path inference, not independently enumerated network/provider-attempt evidence; model inference/retry/remote effect interruption was not reproduced. Captured text is derived from real PTY bytes by the existing independent VT-subset parser, not a fabricated screenshot and not a Ghostty GUI/font/accessibility observation. This is a task analysis with a small set of direct reproductions, not a controlled human experiment, performance comparison or exhaustive failure qualification. The installed executable is pinned by path and SHA256; its exact build-source commit/tree cannot be proven from the installation's available manifest information. Read the inventory before attributing runtime findings to checkout source.

Source publication, later design approval, integration, installation and user-observed improvement remain distinct. This study declares none of the latter outcomes and does not declare chat fixed.
