# Native workflow CLI code attack and fixes

2026-10-15. Root's single independent attack of owned CLI/process-test source;
this lane applies the concrete corrections, without a new review layer.

## C1: signal ownership must precede service threads

Root finding: “signal mask established AFTER RawRuntime/TraceArchiveWorker threads:
process-directed SIGTERM can select unblocked existing thread and bypass owned
waiter. Need establish mask before any service threads, then ensure child exec
resets mask appropriately (read native_process.hpp/trace archive spawn).”

`SignalMask` now starts before archive/raw setup and remains through their
teardown, so every in-process service/worker thread inherits the owned blocked
SIGINT/SIGTERM set. The synchronous waiter is the only consuming thread. The
actual owner has an explicit `RawThreadBinding`, rather than a prepared but
unbound lane handle.

Child exec is a separate required correction: current `native_process.hpp` Child
and `trace_archive_worker.cpp` spawn attributes must explicitly select an empty
signal mask with POSIX_SPAWN_SETSIGMASK. Root completed these shared-file changes.
RawRuntime's native recorder installs its own signal handler and explicitly
unblocks SIGTERM/SIGUSR1 in `record()`; its supervisor is controlled through owned
control/lifetime descriptors. No CLI change installs a global async handler.

## C2: preserve phase logs

Root finding: “test fresh server log gets truncated each reopen hiding crash
logs; use separate phase log names.”

Process fixtures now retain `host-initial.log`, `host-before-crash.log`, and
`host-recovered.log`, with separate second-owner/client output. A later recovery
cannot truncate evidence from the crashed phase.

## C3: gate timeout and fixture cleanup

Root finding: “effectfixture 20s returns success even gate absent (must fail on
timeout) cleanup ensure launched fixture ends before remove_all.”

The fixture now fails if its explicit release gate is absent after its bound.
Its readiness marker carries its actual PID. After killing the host, the test
opens that gate and observes the fixture PID's disappearance before reopening
and before removing the temporary directory. The observation sends no control,
adopts no recovered product process and grants no replay authority. Logs and
session files remain on any test failure.

Source formatted and diff whitespace checked. Build, direct process execution,
the one focused recheck remain root-owned.

The process oracle also checks an exec child self-terminates with SIGTERM (rather
than inheriting the service mask), then terminates the live host via SIGTERM and
requires a clean owner shutdown before reopening.

## C4: explicit JSON binary-value export

Root finding: dump_json rejects invalid UTF8 while native result originals contain
raw byte strings, making a successful native binary read fail its JSON client.
The CLI now recursively exports invalid-UTF8 value strings as
`{"encoding":"hex","bytes":"00ff10"}`. This is the CLI binary-value presentation
wrapper, selected only by the explicit `--request JSON` boundary; native BBM2 and
retained packets keep their exact bytes. Valid UTF8 strings remain JSON strings
(including JSON's ordinary escaping of embedded NUL). This wrapper is presentation,
not an automatically decoded native replacement or an unambiguous user-object tag.
The process test independently creates bytes00ff10, verifies native original bytes,
executes the JSON client, decodes its hex wrapper and compares all three bytes.
