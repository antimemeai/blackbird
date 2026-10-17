# Exact process-delivery observation results

2026-10-16. The new borrowed `CommandJobs::try_observe_delivery` API reads one
actual job/request pair under try-lock, with no allocation, Value/mailbox walk,
capture, drain, dispatch or settlement. It reports queued/selected promises,
queued/consumed/failed deliveries, actual typed operation/signal, native syscall
facts and original raw references. It explicitly cannot expose CodingEngine's
semantic operation locator. Absent job/key includes retirement or unresolved
history and never grants replay permission. Physical incarnation is process-local;
the bridge must bind actual environment/conversation and accepted invocation scope.

Reservation/Delivery cache native fields only at the existing owner transitions.
Signal receipts hold at most3 actual group/getpgid/leader-call facts, using existing
raw2020/2021 returns. Signal routing, syscall count, raw claims, recording fences
and ordinary process responses are unchanged. `done` remains existing owner
bookkeeping, distinct from kernel success and application interruption. Cleanup
records abandoned delivery separately from worker consumption.

The worker previously wrote launch_ref outside the mutex used by current readers.
Publishing that single reference under the existing mutex, after capture, removes
the startup race needed by the new reader and the existing command observation.
The lifecycle oracle reads during actual worker startup without a fake startup
gate. Root's single implementation challenge requested `lookup_count` rather than
mislabeling find-call counts as comparator steps, and explicit process-local/
borrowed-lifetime documentation; both are implemented. Root completed that single
source/oracle challenge with no remaining defect, checking actual noexcept raw
calls, first-reference semantics and mutex coverage of all new fields.

## Direct results

All exact logs are ignored under
`context/tui-lua-composition-2026-10-16/delivery/`.

| Check | Result and log |
| --- | --- |
| Compiled unavailable-seam behavioral RED | Actual child ready and mailbox request accepted; semantic exits1 at real reservation observation assertion. `old-red.log`, `red-build.log` |
| New permanent owner cases | semantic/raw/errors/busy/lifecycle GREEN5/5 in private qualified Debug link; `final-private-green.log` and `reviewed-private-checks.log` |
| Standard current CMake build | command_delivery_test, command_jobs_test, command_mailbox_test, command_observation_test exit0; `debug-build.log` |
| Standard affected CTests | new5 + command_jobs1 + mailbox5 + observation5 GREEN16/16,23.88s; `debug-checks.log`; unchanged20s/30s deadlines |
| Actual owning source/test clang-tidy | exit0, no diagnostics; `reviewed-tidy.log`; newly registered actual CMake test entry independently clean in `cmake-test-tidy.log` |
| Scoped ASan+UBSan | Selected delivery5/mailbox5/observation5 GREEN15/15,19.28s; `asan-configure.log`, `asan-build.log`, `asan-checks.log`; no diagnostics or deadline changes |
| Scoped TSan | Same15 owner cases GREEN15/15,40.06s, including actual startup/read and worker/retirement schedules; `tsan-configure.log`, `tsan-build.log`, `tsan-checks.log`; no diagnostics or deadline changes |

Oracles exercise real worker SIGINT plus independent child acknowledgement,
reservation→selection without dispatch, repeated effect-free reads under allocation
denial, duplicate/conflict/fresh-key behavior, actual mutex contention, group kill
errors and three-call verification fallback, queued delivery abandoned on actual
native-wait failure, target exit before dispatch, receipt retirement and wrong-owner
incarnation. Genuine RawRuntime records are independently decoded to resolve exact
2013/2015 originals and2020/2021 syscall facts; capture-disabled refs stay absent.

Setup failures are preserved and are not RED: Mac sigemptyset macro namespace
syntax (`red-build-setup-failure.log`), a local test variable redefinition while
adding the abandonment schedule (`semantic-abandoned-build.log`, `tidy.log`), and
an unchanged command_jobs private relink lacking its expected sibling fixture
(`reviewed-private-checks.log`: other15 pass). The ordinary CMake layout supplies
that fixture and passes all16. No deadlines, assertions or capture constraints
were loosened to clear a failure.

Profile: actual pinned Homebrew Clang23.1.2, arm64, SDK26.2 at the configured
MacOSX.sdk path, C++20, Debug/BLACKBIRD_DEBUG=1, instrumentation none. Private
commands copy actual source-TU flags; standard build uses the current CMake
targets. `source-profile.json` records exact owned source hashes and compiler/SDK
output. Sanitizer profiles use those same compiler/SDK/Debug settings with
`-fsanitize=address,undefined -fno-sanitize-recover=all` or `-fsanitize=thread`,
respectively, and frame pointers. Both presets were configured with the explicit
qualified Xcode SDK path; exact owning-TU commands are saved in
`sanitizer-compile-commands.json`. Builds ran sequentially with parallel3, then
exactly15 CTests per profile. No suppression, retry, watchdog or product-source
change was needed for sanitizer qualification.

Shared Debug, ASan and TSan Ninja leases are released; no lane checks remain
active. Root's single source/oracle review is complete. Root owns the integrated
source hook and later matched performance; no performance/fuzz/artwork work ran.
