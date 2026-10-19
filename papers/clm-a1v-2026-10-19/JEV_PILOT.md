# Jev context-triage pilot on the C5 trace (2026-10-19)

**Question.** Can a cheap judge (Jev `jev-1.13.0`) tell which parts of a large context still matter, so the
expensive model doesn't pay to read what it discards?

**Data.** The final input of C5 (787 KB, 57 items, grouped into 18 response rounds). Each round was described by
its tool, its arguments (300 chars) and its output size, head (300) and tail (150). The ground truth is the files
C5's own FLOW.md cites: a round that read one of them counts as needed. That label is weak for small or indirect
rounds. Script, request and both responses are in `jev-pilot/`.

**Ask.** One Score per round, asked at the moment the agent is about to write FLOW.md: 0 not needed, 1 a summary
suffices, 2 needed verbatim.

**Result.**

| | |
|---|---|
| Cost | 7,796 input tokens per triage (~$0.0003 list) |
| Latency | 0.35–0.44 s |
| Stability | two identical runs differed by at most 0.08 |
| Cited source reads r02–r08 | 1.46–1.84 (kept) |
| r11, r18: luna's own `context_view` dumps, 646 KB | 0.80–0.94 ("summary suffices") |
| Trivial rounds r13–r17 | 0.06–0.14 (drop) |
| Labelled-needed misses | r09 (writing FLOW.md, 0.7), r10 (a 1.4 KB grep, 0.5) |

**Reading.**
- Jev ranks sensibly, cheaply and stably.
- At a byte-weighted cut, it would summarise the 80% of context that is redundant and keep every cited source
  read.
- The misses are tiny rounds whose "needed" label is doubtful (r09 is the act of writing the answer itself).
- It did not recognise that a dump of the context itself is fully redundant. That is a
  question-design or structural fact better handled in code (`context_view` output should not persist at all).
- One task and one decision point is a pilot, not a calibration result. JEV.md's guidance applies: rank under a
  byte budget, and learn thresholds on held-out workflow cases.

**Operator note.** Jev may not be the right fit for this particular job, and this pilot supports using it as a
ranking signal rather than a decider. The first fix is the `context_view` defect.
