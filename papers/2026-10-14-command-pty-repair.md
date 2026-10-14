# Command PTY fixture repair

The failed composer observation follows actual child reap, which does not establish
that the UI has reconciled its foreground attachment or completed the owning turn.
`TerminalUI::run` observes the command lease before reading each keyboard batch;
the child can exit after that observation. The fixture must wait for the actual
rendered composer mode before submitting its next local command. The command
design already distinguishes leader exit, stream closure, retained result and
attachment state. Keep the independent child PID, one launch, ordered effects and
exact original input assertions.

The pressure writer spent five milliseconds draining after every write attempt,
including successful writes and EAGAIN. Readiness scheduling removes that fixture
pacing but the first affected ASan recheck still advanced only 380,030 of 524,288
bytes before its 30-second limit (144,258 remained). The source/design specifies
bounded pending input, explicit refusal and reserved lifecycle progress; it does
not require consuming 512 KiB of keyboard input within 30 seconds. Parsing the
retained 358,026-byte output takes 0.221–0.234 CPU seconds with the existing Screen
and 4/64 KiB chunks, so the parser alone does not explain the native workload time.

The operator-approved repair replaces that throughput predicate with the actual
operation contract: offer the same 512 KiB source until both actual UI input refusal
and outer PTY backpressure establish saturation, then send lifecycle controls.
The original 30 seconds remains the maximum saturation-progress watchdog. Record
requested, transferred and unwritten extents explicitly; do not claim the entire
source was transferred. Preserve queue refusal, background/stop/reap, independent
PID/one-launch and exact input/effect assertions. Compare every transferred outer
keyboard byte with site2041 originals on the closed disk run. The first added
whole-native-reader oracle exceeded its existing 45-second consumer watchdog in
ASan after saturation/refusal/background/stop/reap/quit/modes had succeeded;
saturation was established after 316,670 bytes in 24.249 seconds. Replace that
inadequate consumer mechanism with the existing `check-main-input` direct
ARCOJ001/ARJ1 kind4 decoding pattern, retaining exact frame/envelope extents,
duplicate rejection and 2040 request links. No reader, product persistence, raw
capture, buffer, batching or performance optimization is planned.

Execute a syntax check and one affected Release/ASan native recheck on each
qualified platform under root resource coordination. The failed throughput result
remains recorded; the new oracle establishes saturation and control behavior, not
sanitized keyboard throughput. Do not widen the saturation or lifecycle watchdogs
to claim a pass.

## Affected macOS recheck

Only `command_jobs_pty` was rerun after the final direct disk oracle replacement.
Both current Release and ASan/UBSan executables pass every original command,
effect/input/PID/one-launch, cleanup/modes and held-open plain-stdin assertion,
plus exact preservation of every transferred outer keyboard byte. No product
source or watchdog was changed by this repair.

| Profile | Requested pressure bytes | Transferred | Unwritten | Saturation observed after | All keyboard bytes checked on disk |
| --- | ---: | ---: | ---: | ---: | ---: |
| macOS Release | 524,288 | 316,670 | 207,618 | 7.589 s | 319,234 |
| macOS ASan/UBSan | 524,288 | 316,542 | 207,746 | 27.032 s | 319,106 |

Captures are under `context/qualification-repair-2026-10-14/command-pty-{release,asan}-direct`;
the corresponding `.log` files retain exit-zero completion. The earlier fixed-flood
and added-reader failures remain in their original logs. Root owns source review,
Linux rechecks and the repair checkpoint; these two passes make no throughput or
reader-performance claim.
