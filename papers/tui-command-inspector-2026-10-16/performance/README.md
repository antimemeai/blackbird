# Command inspector: matched Linux Release measurements

Baseline commit `45b1454`; delivered source `52f910372cd47bf4e95ed9de16dccfa3d87bc888`.
[The source mapping](DELIVERY.json) records the final native input tree
`a23d707da144aa47a797837a2500b802cb213d789880ccb53645e35f4a6ee48f` and binary
`0e8e1b513bfc95ab1da7ee3b45e3735011bcc1f204bd75d11b7c3209034a8ac6`.
Before timing, all421 native input contents and Git executable bits, plus the
driver check adjunct, matched that delivered commit. Full filesystem/tar
permission bits are not Git source identity.

The baseline reuses the exact qualified prior binary
`991d5d19363f3796e44648e41beba644f090c66bc3620e6eb36b5e49fdc28d54` at its original
path, retaining embedded archive/job-runner paths and the prior tree/build.
Its native tree is
`48027f980f11cfd5ce6913db9d56fe9f4f94b0d7bea23e64d14605e2e0f097d0`;
[the baseline mapping](baseline-source.json) records its content/executable-bit
equality to45b1454. The new campaign owns the candidate and recordings. Exact
[inputs](after-source.json), [build/check provenance](after-build-provenance.json)
and [profile/helper identities](profile.json) are retained.

All120 processes exited normally, recording960 endpoints and120 PTY transcripts;
no executable changed. Thirty trials per source and geometry, serial round robin,
ordinary OS caches, no warmup exclusion or discarded tails. Timing ran
2026-10-16 06:53:35–06:53:48 UTC. Both variants use Linux Clang18.1.3/libstdc++13,
qualified Lua5.4.8, Release, BLACKBIRD_DEBUG=OFF and no instrumentation.
No inference prompt was submitted. All owned builds/checks completed before the
round. The shared host was not exclusive: observed load averages began
6.260/6.098/6.056 and ended6.435/6.144/6.072; unrelated activity was not controlled.

[Every delta](deltas.md), [structured analysis](deltas.json),
[raw timestamps](samples.jsonl), [host/scenarios/executable identities](metadata.json),
[unchanged observer](observer.py) and [unchanged analyzer](report.py) are retained.
Across16 endpoint/geometry pairs, p50 decreased11 and increased5; p95 decreased9
and increased7; maxima decreased9 and increased7. The full table preserves every
p50/p95 delta and before/after maximum. Results are mixed:

- Wide startup p50:22.840→23.450ms; p95:103.086→26.076ms; max:121.722→34.887ms.
- Narrow startup p50:22.234→22.083ms; p95:25.746→23.825ms; max:414.143→25.930ms.
- Wide input p50:2.608→2.579ms; p95:2.859→3.315ms; max:2.899→3.435ms.
- Narrow paste p50:39.254→40.222ms; p95:44.887→56.199ms; max:46.772→58.021ms.

These are parent scheduling/PTY-read/parsing endpoints, not physical display
latency. At30 trials p99 is the maximum. One shared-host paired round does not
isolate a source cause. The common startup/text/paste/palette/resize scenarios
never open the new inspector and do not measure inspector selection/refresh
latency. Actual inspector and blocked-output functional checks are distinct.
No blanket responsiveness or nonregression claim follows. The existing f8f
performance issue retains the tails and mixed deltas; no repeat-to-improve round
or speculative optimization was added.

The final source passed [58 scoped Linux Release cases](final-scoped-release-checks.log)
in35.04s, including actual-main inspector, Microsoft cells,16 readable snapshots,
blocked-output episodes and raw_recorder_cuts. The adopted pinned driver used its
existing explicit installer; [its installed identity](driver-installed.json) and
[install output](driver-install.log) accompany the separately hashed check adjunct.
This is a scoped Release counterpart, not a claim of Linux sanitizer/full-suite
qualification or inspector latency.

Initial57-case qualification, the later fixture-only1-case overlay, and the final
typed-fix incremental build remain separately recorded under ignored context.
Changed source inputs received current mtimes before rebuilding. The prior
qualified campaign was never modified. Two local setup mistakes were retained:
the first baseline mapping overcompared tar permission bits despite identical
content/executable bits; and [a fetch-only Python API mismatch](fetch-setup-failure.log)
occurred after the remote observer/analyzer had succeeded. Only existing artifacts
were fetched again; no measured process was repeated.

All120 terminal recordings remain in ignored
`context/tui-command-inspector-2026-10-16/performance/perf-round/terminal-output`
and persistent remote campaign `tui-command-inspector-20261016-wcm_b0zz`; raw
sessions also remain in that remote campaign.
The scripts here are unchanged study artifacts, not active build/tool entries.
