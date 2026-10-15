# TUI tranche performance plan — 2026-10-15

Promote the retained 2026-10-14 PTY observer into an owned, finite development
command. Preserve its terminal-query replies and VT text observation, and make
frame completion explicit: synchronize-output end commits the observed text.
Measure parent spawn to committed input frame, then keyboard write to a newer
committed frame containing the expected result. These include PTY transport,
OS scheduling and observer work; they do not measure native paint submission or
physical display presentation. No provider request is submitted.

Run fresh sessions at 110×35 and 80×24. Observe short unsubmitted input, a 4 KiB
bracketed paste with a visible tail, palette opening/selection, and resize plus
restoration. Each operation follows its stated preceding draft state. Thirty
fresh processes per size and source are a finite initial round; counts are
configurable for later direct questions. Retain every sample, including failures,
and complete PTY transcripts. Setup is outside measured endpoints; ordinary OS
caches remain. Source commit/profile, executable/launcher/driver hashes, host
identity, scenario spec, seed digest when used, clocks, raw timestamps and
shutdown outcomes accompany observations.

A separate analysis command computes nearest-rank p50/p90/p95/p99/max and
absolute/percentage deltas only for identical host, profile, observer, launcher,
terminal environment and scenario definitions. Failures remain counted and
invalidate numerical comparisons. Historical reports remain descriptive unless
these match. Use an actual retained pre-tranche executable if available; never
invent an earlier baseline or attribute a broad source difference to one patch.

Root supplies build provenance and coordinates a host window without builds or
fuzz workers. No builds, remote jobs, product tuning or production changes belong
to this unit. Direct qualification is an actual short run followed by the finite
round, a genuine mismatched comparison refusal, and one independent source
challenge with fixes; no review of the review. Concrete product failures enter
the shared bug queue through root unless egregious.
