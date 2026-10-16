# Native action bridge results

2026-10-16. NativeActionBridge connects the existing phase-checked ActionInbox to
the existing CommandJobs signal mailbox. One worker belongs to the runtime scope,
outlives presentation view/VM reloads, validates the frozen target and forwards
one exact whole-ActionKey request ID. It keeps a fixed recent16 presentation cache
and queries actual delivery facts without dispatching. It creates no operation
ledger, semantic locator or signal implementation.

The constructor prepares the dedicated stop wake and raw worker producer before
input. Actual raw dispatch/start links identify that producer. Before forwarding,
the worker captures the immutable envelope and exact signal arguments at4095 DATA
and prepays4096 CONTROL. It preserves an admitted/refused/unknown native receipt
before acknowledging the inbox. Raw failure preserves the original key, stops new
dispatch and never resends. Stop cancels raw waits, joins the worker and preserves
unattempted envelopes in the independently owned inbox. Recent-cache retirement
does not grant permission to repeat a request.

## Direct results

Exact logs are ignored under
`context/tui-lua-composition-2026-10-16/bridge/`.

| Check | Result |
| --- | --- |
| Compiled refusing-seam RED | Actual child ready and Lua handler accepted a real inbox entry; create seam exits1 at native bridge/runtime assertion. `old-red.log`, `red-build-02.log` |
| Permanent cases | handoff/reject/recent/stop/raw GREEN5/5 in qualified private link. `green-build.log`, `green-checks.log` |
| Whole-key ordinal oracle | Original ordinal0 remains deduplicated; ordinal1 in the same trigger receives its distinct exact request. `ordinal-build.log`, `ordinal-green.log` |
| Root's single source/oracle challenge | One finding: missing raw dispatch/start lineage; no effect/dedup defect found. Permanent raw oracle first exits1 against unchanged source. `lineage-red-build.log`, `lineage-red.log` |
| Review fix | Actual RawWorkerDispatch emitted before spawn and raw_worker_started emitted on the bound producer before inbox processing; raw oracle resolves forwarding lane1101→actual1100 dispatcher |
| Standard current CMake build/check | Target build0 and owning5/5 CTests GREEN1.30s, unchanged watchdogs. `debug-build.log`, `debug-checks.log` |
| Final actual-TU clang-tidy | Both owning source/test TUs exit0 with no diagnostics; `final-tidy.log` |
| Private ASan+UBSan | Build0; initial handoff/recent/stop/raw GREEN4, reject SIGABRT from uncaught native Error without sanitizer diagnostic. Diagnostic-only Error catch added; only failed reject rebuilt/reran GREEN. Original instability remains unexplained, not a clean original5 claim. `asan/results.json`, `asan/reject.log`, `asan/reject-diagnostic.log` |
| Private TSan | Build0, all5/5 GREEN9.08s; no race diagnostics. `tsan/results.json` and per-case logs |

Initial test setup omitted the Lua module's required compose function and failed
before bridge creation. That failure is preserved in `red-setup-01.log` and is not
counted as behavioral RED. No assertion or deadline was loosened.

The handoff oracle uses a real fixture process, actual Lua handler acceptance,
actual mailbox selection and actual CommandJobs control/worker consumption.
Repeated snapshots never enqueue; the original native token stays deduplicated
after inbox acknowledgment and VM replacement. Fresh triggers and ordinal1 remain
distinct. Other cases reject wrong scope/owner/incarnation/kind/verb, missing and
actually exited targets; retain the newest16 of20 receipts in native acceptance
order; and preserve a later accepted envelope after bridge stop/destruction.

The raw oracle independently decodes actual4095 native fields,4096 return,
CommandJobs2013 admission and1100/1101 worker lineage. The fixture has no physical
keyboard original: its input-origin Ref is honestly absent and preserved as such.
Actual terminal/input interaction is owned by the separate instrument adapter.

The lost-return/reload oracle discards VM presentation ownership, reconciles the
original native key and verifies no resubmission. It does not inject arbitrary OS
acknowledgment loss or qualify durable recovery. There is no globally exactly-once
or restart guarantee; missing/retired owner history remains unknown. Consumed
delivery and successful kernel return never assert the application handled SIGINT.

Profile: pinned Clang23.1.2, arm64, SDK26.2, C++20, Debug/BLACKBIRD_DEBUG=1,
instrumentation none, Lua5.4.8. Private builds use the already qualified library
clones plus the qualified current delivery foundation clone; standard CMake builds
use current sources. `actual-compile-commands.json` and build logs retain exact
flags. Root owns integrated sanitizer/source-hook/performance coordination. Shared
Debug lease was released after successful checks; no performance or fuzz work ran.

Private sanitizer commands follow the already qualified owner profiles, compiling
the actual bridge/test and stable src/tui_lua.cpp against three APFS-cloned native
archives per profile. `asan/identities.json` and `tsan/identities.json` preserve
exact bridge/VM/header, archive, Lua and job-runner hashes, commands, binary hashes
and options. ASan+UBSan uses halt_on_error and fno-sanitize-recover=all; TSan uses
halt_on_error=1:exitcode=66. All five cases keep the20s watchdog. No shared Ninja,
shared build-directory or product changes were made for these profiles.

The sole test-source change during qualification adds an Error diagnostic catch;
it changes no assertion, effect or timeout. `asan/diagnostic-source.json` records
its exact test/binary hashes. The initial native Error code was lost to the old
uncaught-exception path; its original SIGABRT/log is preserved. A possible fixture
new_trigger/try_read busy result is a hypothesis, not an established cause. This
ordinary oracle instability remains a follow-up finding; no suppression or blind
repeat was used to relabel the original profile clean. All lane processes and
private checks are now stopped.
The initial SIGABRT left one private fixture child. Cleanup matched only this
profile's exact executable-plus-`--child` argv, sent SIGTERM to that owned child
and verified no matching child remained; TSan had none. The exact cleanup is
saved in `private-resource-cleanup.json`. No unrelated process was inspected or
signalled.

## Final-hook nonblocking fixture correction

The final full hook passed251/252 and failed recent with native busy. The
pre-correction test and SHA256 are retained under ignored
`bridge/hook-busy-repair/`. One-shot new_trigger and try_read unwraps incorrectly
treated documented try-lock contention as a permanent failure. The hook's
noncontextual error does not identify which exact call returned busy.

The test now waits only on ErrorCode::busy for trigger minting (before counter
advance/acceptance) and immutable reads. Read predicates treat busy as not ready
within their existing5-second deadline; standalone calls use that same finite
polling deadline. Other errors identify the operation and preserve native
code/detail. No handler or effect acceptance is repeated, and accepted_requests,
exact20 receipts/order, whole-key dedup and recent16 assertions are unchanged.

Owning Debug target build0 and five CTests passed1.54s; actual test-TU clang-tidy
returned0 with an empty log. Root read the concrete correction and found no
defect. No profile campaign was repeated, and this correction edited no product
source. Exact before/after hashes, independent test-only patch and checks are retained in
`context/tui-lua-composition-2026-10-16/bridge/hook-busy-repair/patch-manifest.json`.
This additional test-only delta must accompany publication of the existing
performance results. All lane build/check activity stopped; Debug lease returned.
