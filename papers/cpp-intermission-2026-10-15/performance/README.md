# Repair intermission: matched Linux Release measurements

Before commit `84f5ea8`; delivered repairs `abaee8a`. The timed native input tree is
48027f980f11cfd5ce6913db9d56fe9f4f94b0d7bea23e64d14605e2e0f097d0,
which matches the delivered source commit. [Delivery mapping](DELIVERY.json)
records that relationship and hashes the 120 retained PTY transcripts. Exact
[source inputs](after-source.json) and [build profile](after-build-provenance.json)
identify the candidate, alongside the corresponding before manifests.

All 120 processes exit normally, with 960 recorded endpoints and no changed
executable. Thirty trials per source and geometry, serial round robin, ordinary
OS caches, no warmup exclusion. Timing window 2026-10-16 04:54:53–04:55:07 UTC.
Both sources use Linux Clang 18.1.3/libstdc++ 13, owned Lua 5.4.8, Release,
BLACKBIRD_DEBUG=OFF and no instrumentation. No provider prompt is submitted.
Root-owned builds and checks finish before timing. The shared host is not
isolated: observed load averages begin 5.96/5.93/5.69 and end 6.13/5.97/5.71.
No claim of quiescent unrelated services or exclusive scheduling follows.

[All deltas](deltas.md), [structured analysis](deltas.json), [raw timestamps](samples.jsonl),
[host/scenarios/binary identities](metadata.json), [observer](observer.py) and
[separate report](report.py) are retained. The two scripts are unchanged study
artifacts copied from the actual measured run; they add no active build/tool entry. The endpoints include parent scheduling,
PTY reads and parsing; they are not physical display latency. At 30 trials p99 is
the maximum. Each 120-run round is one matched observation, not causal isolation.

Results are mixed. Wide startup median 24.224→23.869 ms, p95 28.089→112.375 ms,
max 29.829→437.612 ms. Narrow median 22.579→22.766 ms, p95 27.400→230.796 ms,
max 115.443→273.499 ms. Input medians 2.566→2.569 ms wide and 1.453→1.496 ms
narrow. Paste medians 40.601→40.435 ms wide and 39.071→41.053 ms narrow;
wide paste p95 50.772→60.737 ms while its maximum 195.218→63.329 ms. Most other
p95 endpoints increase; the full table preserves every direction. These results
support neither blanket performance improvement nor a source-causal explanation
for the startup tails. The existing arconaut-f8f issue retains this round.
No repeat-to-improve round or speculative optimization is added.

The final Linux source passes 43 scoped owner/integration tests in 18.91 s;
[the direct CTest output](final-release-selector-checks.log) includes the unchanged
command_jobs final-reap assertion. Earlier failed/stale builds and their logs
remain under ignored context/cpp-intermission-2026-10-15/performance/.
Incremental archive overlays force current timestamps on changed inputs and
verify their content before rebuilding. The provenance names the initial full
build archive separately from the final overlay archive; prior build inputs do
not imply that their later tests passed. No failed or stale binary was timed.

The full source gate passes 195/195 Debug tests in 233.52 s plus complete C++/Lua
analysis before commit `abaee8a`. See [repair results](../RESULTS.md). Raw sessions,
terminal bytes and source archives stay in the declared ignored/remote campaign
directories; this directory contains the publishable raw metrics and manifests.
