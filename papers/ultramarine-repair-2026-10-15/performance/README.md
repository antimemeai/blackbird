# Ultramarine repair tranche performance

All120 processes complete normally:30 trials per source and geometry,960 endpoint
observations, zero failed/interrupted trials and unchanged executable hashes.
Same retained observer, serial round-robin, Clang18.1.3/libstdc++13/Lua5.4.8 Linux
Release profile,110×35 and80×24. Owned remote builds/checks finish before timing;
shared-host work remains uncontrolled. No trial/outlier exclusion or separate
warmup. These are PTY/observer endpoints including scheduling/parsing, not physical
display latency or isolated native costs.

Wide input p50 changes2.639→2.580ms; narrow1.431→1.469ms. Wide paste p50
41.987→40.470ms; narrow39.834→39.799ms. Other medians/tails are mixed. In particular,
wide startup p95 worsens29.283→95.466ms and max31.403→347.610ms. Retain this
ordinary startup-tail observation in arconaut-f8f; no source attribution, blanket
nonregression or whole-application speedup follows from this shared-host round.
The full [delta table](deltas.md) preserves all16 endpoint/geometry combinations;
[structured analysis](deltas.json) includes p90/p99 and every percent delta.

[Metadata](metadata.json), [raw timestamp samples](samples.jsonl), [executed
spec](spec.json) and [build provenance](build-provenance.json) retain exact source,
compiler, archive and native identities. Before is9cea416 (native676120c).
Provenance embeds both complete source manifests; source archives and exact raw
terminal output remain under ignored context/ultramarine-repair-2026-10-15/ and
the recorded owned remote campaign. The baseline binary is preserved independently
as retained-before/blackbird before incrementally updating its build directory.
An initial Clang18 property-array CTAD build fails, is retained separately, and is
corrected with explicit dimensions before any measurements. No compiler flag
changes. The missing auxiliary Microsoft development-tool profile is transferred
explicitly with its hash before Release checks, without changing native inputs.

Final script-only physical-pressure correction is declared in
[the test overlay](final-test-overlay.json); no production body or measured binary
changes. Release24/24 precedes it, with the affected final fixture1/1 separately
passing. LinuxASan23/24 first plus corrected affected1/1 are recorded in the parent
results; do not relabel that as one24/24 invocation. Current test-source follow-up
and published source identities are recorded in the parent delivery report.

Published source is `65e5877dda1cc5b965b276a65c57668be0e1e038`. The subsequent
[test-source follow-up](test-source-followup.json) identifies the only native
test change after the measured snapshot, with its actual Mac/Linux targeted
results. It does not change the measured executable. Final Debug160/160 and
full C++/Lua analysis are in the [delivery record](../RESULTS.md).
