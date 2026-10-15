# Native concurrent workflow checkpoint — qualification and costs

2026-10-15. First delivered slice of arconaut-45a, owned by arconaut-45a.1.
Source branch: `work/concurrent-durable-workflows-2026-10-15`, isolated checkout
`/Users/patrickbeam/projects/blackbird-workflows`. No main integration or running
launcher replacement. [Usage and limits](../../docs/NATIVE_WORKFLOW_HOST.md),
[remaining sequence](../../docs/WORKFLOW_DELIVERY.md),
[implementation/review record](PLAN.md).

## Actual fault oracles

| Boundary | Direct check |
| --- | --- |
| Store/native journal | Exact multi-cell read revisions; duplicate submission equality; stale transaction has no writes/effects; real reopen preserves cells and native linkage; faulted publication retains accepted state. |
| Admission/executor | Independent gated workers establish concurrency and bounded capacity; failed admission produces no leaf effect; immutable input survives external mutation; owner-only controls and generation fences reject stale callers. |
| Completion/cancel/drain | Failed/throwing publication preserves exact completion and capacity; queued cancellation prevents invocation; running cancellation requests stop; shutdown joins and leaves pending output drainable. |
| Result funding | Ordinary journal headroom exhausted after admission still permits the reserved atomic result cell plus native terminal observation; protected append refuses new release records. |
| Join | Explicit expected winners for all/race/success/quorum/select/collect; impossible finite quorum; final versus unresolved unknown; epoch/untimed-order rules; selection stays hidden until retained and immutable afterward; BBM canonical key/numeric restoration. |
| Facade | Real concurrent native children; exact file/process originals and bounded overflow; ordinary cancellation intent before stop; healthy destructor drain; writer fault closes admission and contains the existing population while retaining pending output. |
| Actual processes/raw recorder | Real private host plus archive worker; duplicate owner refusal; detach/reconnect; raw binary CLI presentation; child signal mask and graceful host signal handling; two independently counted gated effects, owner SIGKILL, stale-socket reopen and zero replay. |

These tests establish the native execution path. They do not establish arbitrary
crash recovery, tool-using model continuations, escaped-child containment or
cross-host takeover. Code received one independent attack and one focused
recheck/fix; compiler/sanitizer repairs addressed actual diagnostics afterward.
No new review panel or mutation campaign.

## Qualification captures

Source checkpoint `7b99b6c` passed the installed full pre-commit gate:
142/142 macOS debug tests (249.76s), all owned C++ formatting/diagnostics and
Lua checks. Qualified macOS compiler: LLVM 23.1.2 on arm64 Darwin 25.6.0.
It is published on the work branch. No hook bypass or added suppression.
Final-source Neuroses ASAN capture `context/linux/run-3ixzkegy/checks.log`
passes 9/9 affected cases (13.41s test time), including the final API/guard
cleanup. The earlier full macOS debug run passed 142/142 cases. The Neuroses capture `context/linux/run-3v1rxc14/checks.log` passed 9/9 affected cases
in each of debug, release and ASAN. It includes the store allocator and queue
repairs but predates final const-reference/optional-guard diagnostic cleanup.
All six workflow cases and ten affected clang-tidy units pass after that cleanup.
No sanitizer flag changes, new dependencies or rule suppressions.

Actual failed runs are retained. The first process/runtime fixtures and join
restore exposed integration faults corrected in this slice; the first Linux
reopen assertion incorrectly equated two distinct records in one batch and now
checks adjacent sequences in the same journal. Linux ASAN exposed missing sized
deletes in a malloc-backed fault fixture; adding matching overloads fixed it.
Full lint exposed swapped queue ends, unchecked optional values and unnecessary
copies. The failed source gate was stopped after collecting its real findings,
then all affected units were checked before rerunning the mandatory full gate.
No failed run was counted as a passing qualification.

## Complete-work baseline

`scripts/bench-workflow-host` runs three samples, each with 32 echo tasks,
four external CLI clients, 128-byte text payload, an all join, graceful close,
same-session reopen and exact task/result/selection validation. All calls,
stdout/stderr bytes, errors, IDs, sample timings and session inventories are
retained. CLI spawn/JSON, BBM framing, owner/journal/executor/raw capture, polling
and orchestration all contribute. This is not a native hot-loop or competitor
comparison; no performance target or optimization claim follows from it.

Raw producer behavior is unchanged: raw events go to named memory and a separate
recorder; the benchmark is an external tertiary reader of completed files. Logical
and allocated disk sizes are separate. It never scans/copies global shared memory
or sparse producer buffers. Private `/tmp` sessions remain available until OS or
operator cleanup. Ignored context retains the complete report and raw captures;
tracked tables preserve every sample, not a selected aggregate.

### Initial integration binary

Executable SHA256: `0fcd6411d31e1a5d254961bfab150fcb6b3eb9323296c205bd6d1adb58aaff3c`.

Capture: `context/workflows-implementation-2026-10-15/benchmark-debug/report.json`; report SHA256 `5b8167b91e5e1a5923640416eaf6a3993de8352cb9da1e9b52cec1152a7848c5`.

| Sample | Submit→verified join (s) | Reopen→verified results (s) | Audit logical bytes | Raw logical bytes | Raw event logical bytes | Request attempts / valid replies |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1.120693000 | 0.959749958 | 95373 | 2824597 | 2644629 | 106 / 103 |
| 2 | 1.707061042 | 0.383850750 | 95374 | 2814344 | 2634184 | 105 / 103 |
| 3 | 1.158775291 | 0.311812458 | 95373 | 2826277 | 2646309 | 105 / 103 |

- Sample 1: session `/tmp/bbwh-a18mq0q5`; audit SHA256 `d38a1ce56533fc8e5c5e9547fd3a2d6be9eead1822515135c529e180b50c7b12`; allocated audit/raw bytes 98304/3698688.
- Sample 2: session `/tmp/bbwh-mbtzpw7g`; audit SHA256 `60a768fb19dbf065c9d4827a9aac17230bbd63946d4b2f9cd74dddbfde5dc6a8`; allocated audit/raw bytes 135168/3665920.
- Sample 3: session `/tmp/bbwh-z2egw2co`; audit SHA256 `b406949a1351b2547bd88853ff01621d5d668fac4d335ecaab890d16a74b0e1a`; allocated audit/raw bytes 151552/3702784.

Every sample passed. Readiness connection failures are included in request attempts;
valid JSON replies include service refusals, and task/result oracles separately
require success. No failed sample, timeout or outlier is omitted. Initial timing
runs overlapped development qualification on this host; they are observations of
complete work, not an isolated machine microbenchmark. Final executable samples
follow separately, preserving the initial samples.

### Qualified source checkpoint binary

Source: `7b99b6c`; executable SHA256 `67b18deecd83e88a850e313fdda2836d09b8cb2afeea48ab9f9a6f67a1a460de`.

Capture: `context/workflows-implementation-2026-10-15/benchmark-debug-final/report.json`; report SHA256 `0643c9eaae755263419541250f7b61d449d78df2349f9ae2f157a51735a31159`.

| Sample | Submit→verified join (s) | Reopen→verified results (s) | Audit logical bytes | Raw logical bytes | Raw event logical bytes | Request attempts / valid replies |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1.667329417 | 0.667025417 | 95374 | 2815661 | 2635565 | 106 / 103 |
| 2 | 1.172391459 | 0.294631250 | 95373 | 2825516 | 2645484 | 105 / 103 |
| 3 | 1.168975458 | 0.375964041 | 95373 | 2825325 | 2645357 | 105 / 103 |

- Sample 1: session `/tmp/bbwh-py6ppm4n`; audit SHA256 `3c8c5402bd537c3887af5fab0120ca33b79572339e1af9e32906acde19bf07bc`; allocated audit/raw bytes 135168/3649536.
- Sample 2: session `/tmp/bbwh-265u5si1`; audit SHA256 `28d0a31b7085d34902cbfb8682d60b4c0f087739182d33ddae215991f4f77512`; allocated audit/raw bytes 151552/3698688.
- Sample 3: session `/tmp/bbwh-ax2jz0de`; audit SHA256 `a5272e18b19be096cd9b339af8e9a0d0f2564a71bddc4721ca2d75c4cfe5a8bf`; allocated audit/raw bytes 151552/3698688.

All three final samples passed. This run followed the completed local gate on an
ordinary development host, alongside branch publication. It does not isolate
background load. Earlier samples remain in place; these two builds are not a
controlled performance comparison.

## Remaining owning work

- arconaut-45a.2: task-local participants/context/config, standing scopes, aggregate permits and protected resource cleanup (remaining F1/F2).
- arconaut-45a.3: durable segment decisions, explicit reconciliation/release recovery, registered saved-state coverage and frontier-scaled reopen (F4 and the F5 recovery boundary).
- arconaut-45a.4: recorded Lua continuations and exact trace/input/manifest matching (F6; depends on both preceding owners).
- arconaut-45a.5: required complete-work performance attribution/tuning through the composed repair campaign (F12).
- arconaut-45a.6: qualified Linux ASAN symbolizer path, so future fault reports include source frames.

The current receipt cache has a resident metadata bound; task/join state still
grows with the session, and WorkflowStore restore currently reads full retained
history rather than using registered saved coverage. A crash-released effect
remains unresolved and fences this first host read-only. No automatic replay,
proven-unreleased successor command, model loop or Lua continuation is advertised.
The broad workflow epic stays open; later F7–F11 capabilities retain their design
and owning sequence. Required end-to-end tuning remains delivery work.
