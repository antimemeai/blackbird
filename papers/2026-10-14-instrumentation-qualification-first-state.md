# Instrumentation qualification: first state — 2026-10-14

The first state is **not green**. Current source 200fc5e has a production
borrowed-field lifetime defect, a Linux Candidate build defect, an unresolved
command progress failure, and native fixture deficiencies. No optimization,
product repair, compiler/assertion relaxation, new dependency, provider request
or eval work was performed. This is the operator-requested report before repair
and performance discussion, not completion of the broader nls.2 census.

Tested source 200fc5e8c16449281dab4c7b4c6d63eb0018aaf4; HEAD 33e78d1 contains
publication documentation only. Existing131 Debug/all 194 owned C++ TU checks
remain their earlier host-specific results and were not rerun. Fresh selected
builds use macOS ARM64 LLVM 23.1.2/libc++, Release/OFF and Debug ASan+UBSan/ON;
Neuroses Linux x86_64 uses Clang 18.1.3/libstdc++13 and the same two modes.
Lua 5.4.8 is the existing selected embedding. Aggregate compilers never exceed6,
and each buildtree has one Ninja writer. Measurements run after local builds
and test fixtures finish; other operator/system activity is uncontrolled.

| Profile | Selected cases | Passed | Failed | Whole CTest elapsed |
|---|---:|---:|---:|---:|
| macOS Release |40|39|1|89.51s|
| macOS ASan/UBSan |40|29|11|464.34s|
| Linux Release, independently buildable owners |39|37|2|33.37s|
| Linux ASan/UBSan, independently buildable owners |39|28|11|313.68s|

Candidate is excluded from the two Linux case counts because its executable
fails to build; both intended Candidate profile checks remain blocked. All
158 selected CTest invocations completed. No case timeout was expanded and no
failure is converted to a pass. A failed native harness can abort before its
later subcases; continuation details and unresolved gaps are stated below.

The existing cases directly compare native originals, emitted event fields,
parent/child references, actual effects/errors and disk-preserved bytes. Raw
admission covers cancellation/deadline before acquisition, surviving prepaid
credit, full-lane waiting and refused registration/owner grants. Channel cases
cover exact envelopes, wrap, partitions, loans, broken/full doorbells, process
death, variable-length records and concurrent publication/reclaim. Recorder
cuts exercise partial append, failed sync, synced-before-publication, published
prefix, two waiting owners and hot/cold source fairness. Runtime/metadata cover
holder/recorder death, restart, missing/damaged/conflicting independent metadata
and foreign symlinks. Reader cases preserve files unchanged, reject malformed
published prefixes and resume native cursors. Named owners cover successful
and failed effects, allocation/native failure cuts, capture refusal and cleanup.
PTY/Station command cases inspect actual PID, one launch, exact input/effect
bytes, foreground/background ownership and terminal receipt.

## Findings and beads

1. **arconaut-834: production File field lifetime.** Helper::finish in
   src/raw_file.cpp declares `failed` inside the native_error_ block, borrows
   it in Fields, leaves the block, then calls emission_.close. Both sanitizers
   read a poisoned one-byte `failed` at line144, offset1440, during ring copy.
   Mac traces identify the exact production call. It causes failures in
   raw_file, session_raw, backstop_raw, candidate_raw, main_input_native,
   beads_raw, station_main_raw and station_main_native; Linux reproduces the
   same poisoned variable in its seven buildable corresponding cases.
   Station command diagnostic output also identifies this File path. These
   are repeated consequences of one defect, not independent fixes per surface.
2. **arconaut-90u: Candidate Linux build.** src/candidate.cpp uses std::strlen
   at lines73/501 without including <cstring>. Clang 18/libstdc++13 rejects it.
   The original whole selected build failed before CTest. Independent targets
   subsequently ran; Candidate's actual Linux long-CWD case remains untested.
3. **arconaut-0oa: command composer/pressure progress.** Both ASan profiles
   fail command_jobs_pty; original Mac run misses COMPOSER_RESTORED. One Mac
   diagnostic replay gets past that point, then fails the existing30-second
   condition while sending512 KiB to a non-reading PTY child. No ASan diagnostic
   appears in its retained PTY output. Cause remains unresolved; the timeout
   does not establish whether the defect is scheduling, harness or product.
   Station command failures are now attributed to834 by retained diagnostics.
4. **arconaut-bho: Beads fault selection.** Optimized Mac/Linux tests cannot
   reach the argv allocation cut: it requires a dladdr RawBeadsArgv::push stack
   symbol. Release has zero such symbols, Debug 23. The inline template has no
   required runtime frame. Preserve the assertion and replace fault selection.
   Unchanged cache_sharing/grant_boundaries after that failure passed via direct
   context-owned continuations on both Release hosts. The failed allocation
   subcase remains untested in Release; ASan stops earlier on834.
5. **arconaut-iva: Station fault selection.** Linux injection requires the
   libc++ `__tree` symbol, so its libstdc++ set allocation cannot match. Both
   Linux profiles fail this assertion. Unchanged later native_bound_refusal and
   native_post_request_refusal groups passed in a Linux Release continuation.
   Allocation/prompt subcases hidden by the failure remain unqualified.
6. **arconaut-fir: UI fixture credit lifetime.** Mac ui_raw holds thread-local
   fault credit/loan beyond RawThreadBinding's producer teardown. TLS destruction
   reads the freed RawProducer through RawCapacityClaim::~RawCapacityClaim.
   This is the fixture's deliberate completion-refusal setup lifetime error,
   not the production File borrowed-field defect.
7. **arconaut-ji3: Linux UI allocation mismatch.** Linux ui_raw reports
   operator-new versus free on a200-byte allocation. Its custom allocator
   machinery differs across qualified standard libraries; actual allocation
   origin is unresolved because the remote external symbolizer path is invalid.
   The existing runner removed the scratch executable before addr2line could
   inspect it. Retain the offsets/error and symbolize the owning case during
   repair. No sanitizer suppression was added.

The Linux archive script omits scripts/check-main-input. This run uses an
ignored test-packaging copy with that exact source included and repository root
set explicitly. It removes wrapper campaign cutoffs, retaining existing native
case watchdogs. To exercise both profiles despite CTest failures, the copy
continues after each CTest and records PROFILE_CTEST_RESULT. Its inherited
success footer is misleading and **does not describe the result**: both actual
CTest exits are8. Counts here come from the original individual case results.
Source packaging and remote diagnostic retention also need correction before
the next qualification run. Neither change is represented as product repair.

## Actual runtime and startup observations

Fourteen unchanged-production raw workloads pass: **218,946 unique events**
are checked against independent literal envelopes/bodies on closed disk runs.
They cover64/256/4096-byte bodies, three bursts each, paced emissions, a
concurrent resumable reader checking 20,000 events, and actual SIGSTOP-established
full-buffer refusal/SIGCONT admission. Deliberate1 MiB DATA grants force real
wrap and preservation waits. Complete [raw workload results](2026-10-14-raw-runtime-workloads.md)
include each trial, raw clocks, CPU units, first events, source and limits.

Burst prepaid emit medians are42 ns for64/256 B and208–250 ns for4096 B. The empty
clock-pair median is41 ns, so small-body figures sit at observer resolution;
do not infer an intrinsic42 ns cost. Paced emit medians are15–24 µs. Maximum
emit is15.48 ms; maximum capacity claim wait is72.80 ms. Final-event preserved-ACK
observation lag spans4.50–22.88 ms in bursts, including observer/scheduling delay.
Full-pipeline body throughput spans32.82–189.91 MB/s across these finite trials.
All tails are retained. This is not production sizing or a sustained throughput
or latency guarantee. Recorder CPU counters are converted by the measured
Mach timebase125/3; the historical raw driver CPU label was wrong and is
preserved with the correction in the measurement report.

The existing PTY startup driver ran30 starts per executable, serial round robin,
with fresh private sessions,110×35 PTYs, ordinary caches and actual terminal
query replies. A complete input frame and then a rendered unsubmitted marker
are the endpoints. All60 starts pass, exit zero and need no forced shutdown;
no inference prompt was submitted and no sample was retried/excluded.

| Release executable | Startup p50 ms | p90 ms | Max ms | Input p50 ms | p90 ms | Max ms |
|---|---:|---:|---:|---:|---:|---:|
| Retained previous segment |130.514|207.426|289.531|1.941|4.991|15.643|
| Current final segment |129.579|230.407|551.415|2.730|5.561|17.066|

Startup median remains about 130 ms here; the larger current maximum is retained.
The retained executable is the earlier ca6747b segment, not the exact parent of
200fc5e. This is a local generation comparison, not causal attribution to this
diff or a startup tail guarantee. Native hashes and all original PTY samples
remain in context; tracked summaries/metadata preserve the exact run selection.

Allocation/copy/RMW/syscall/wake counts, real owner-loop costs, heavy/reopened
sessions, production buffer sizing, long sustained disk runs and Linux raw
workload performance remain unmeasured. Process-shared behavior is attacked by
native process/death/exact-byte cases; no TSan or mutation claim is made here.
SystemA Value/radix/ByteBuffer global hooks remain implementation scope, not a
measurement result. Evals, trajectory work and multiplayer remain deferred.

Repair planning should first remove the production File borrowed-field defect
and correct Candidate's direct include. Then fix direct fixture fault selection/
lifetime and diagnose remaining PTY/Linux UI outcomes. Recheck those affected
paths and unexecuted subcases. Performance changes, capacity tuning and broader
workloads wait for discussion of this state. No repair began during this run.

## Every selected CTest result

| Case | macOS Release | macOS ASan/UBSan | Linux Release | Linux ASan/UBSan |
|---|---|---|---|---|
| operation_raw | PASS | PASS | PASS | PASS |
| storage_raw | PASS | PASS | PASS | PASS |
| raw_retained_saved | PASS | PASS | PASS | PASS |
| raw_retained_native | PASS | PASS | PASS | PASS |
| raw_retained_caller | PASS | PASS | PASS | PASS |
| decision_raw | PASS | PASS | PASS | PASS |
| git_raw | PASS | PASS | PASS | PASS |
| ui_raw | PASS | FAIL | PASS | FAIL |
| raw_file | PASS | FAIL | PASS | FAIL |
| raw_file_stdio | PASS | PASS | PASS | PASS |
| program_workflow_raw | PASS | PASS | PASS | PASS |
| bridge_module_raw | PASS | PASS | PASS | PASS |
| session_raw | PASS | FAIL | PASS | FAIL |
| complaint_raw | PASS | PASS | PASS | PASS |
| restart_recovery_raw | PASS | PASS | PASS | PASS |
| backstop_raw | PASS | FAIL | PASS | FAIL |
| candidate_raw | PASS | FAIL | BUILD BLOCKED | BUILD BLOCKED |
| terminal_custody_raw | PASS | PASS | PASS | PASS |
| plain_output_raw | PASS | PASS | PASS | PASS |
| terminal_custody_native | PASS | PASS | PASS | PASS |
| main_input_raw | PASS | PASS | PASS | PASS |
| main_input_native | PASS | FAIL | PASS | FAIL |
| beads_raw | FAIL | FAIL | FAIL | FAIL |
| station_raw | PASS | PASS | FAIL | FAIL |
| station_main_raw | PASS | FAIL | PASS | FAIL |
| station_main_native | PASS | FAIL | PASS | FAIL |
| raw_admission | PASS | PASS | PASS | PASS |
| raw_clock | PASS | PASS | PASS | PASS |
| raw_channel | PASS | PASS | PASS | PASS |
| raw_audit | PASS | PASS | PASS | PASS |
| raw_runtime | PASS | PASS | PASS | PASS |
| raw_metadata | PASS | PASS | PASS | PASS |
| raw_reader | PASS | PASS | PASS | PASS |
| raw_recorder_cuts | PASS | PASS | PASS | PASS |
| raw_command_wake | PASS | PASS | PASS | PASS |
| tasks_raw | PASS | PASS | PASS | PASS |
| command_jobs_engine | PASS | PASS | PASS | PASS |
| session_recovery | PASS | PASS | PASS | PASS |
| command_jobs_pty | PASS | FAIL | PASS | FAIL |
| command_jobs_station | PASS | FAIL | PASS | FAIL |

CSV [case results/timings](qualification-2026-10-14/native-case-results.csv),
[original failure excerpts](qualification-2026-10-14/failure-excerpts.txt),
[written scope](qualification-2026-10-14/PLAN.md),
[startup summary](qualification-2026-10-14/startup-summary.json) and
[startup metadata](qualification-2026-10-14/startup-metadata.json) are tracked.
Complete build/CTest logs, original sanitizer traces, continuation drivers,
command diagnosis audit/effect/PTY logs and all startup samples remain in ignored
context/qualification-2026-10-14 and context/linux/run-{x4cccbzl,uqp62j5c}.
All owned workers/processes completed; both remote scratch directories were
removed by the runner. No source gate is rerun for these research/report files.
