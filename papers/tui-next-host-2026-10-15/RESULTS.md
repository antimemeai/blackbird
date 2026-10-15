# Owned native terminal frames

The [source plan](SOURCE_PLAN.md) was implemented in an **inactive, unqualified
source candidate**. The final source hook failed145/147; no product source is
published by this checkpoint. The [source status](SOURCE_STATUS.md) and
[exact retained identities](CANDIDATE_SOURCES.json) describe restoration.

The candidate implements `ChatPresenter` in the
actual terminal loop. One immovable owner contains the final output string,
staged grid/cursor/geometry and borrowing writer. Normal, graphics and tiny-screen
packets use that lifetime. Full output swaps the matching grid into the painter
and publishes its cursor; partial output retains the exact suffix. Graphics
placement/load observations publish through the same completion service, including
editor drain. Native input, cancellation, polling and one-frame admission remain.

The implementation removes the production post-output viewport copy and the
graphics text-prefix copy. It retains two viewport grids, reusable packet storage
and existing original capture. This is a source-level accounting result, not a
measured whole-application speedup. No new worker, dependency or domain owner is
introduced. Lua composition and shared application views remain later units.

## Direct results

- Mac Debug presenter pipe and raw UI checks pass: exact owned suffixes, refusal
  to replace an in-flight packet, short inline storage, every byte cut through
  CSI/UTF-8 with EINTR/short write/EAGAIN, completion once, abandoned cursor state,
  allocation failure before admission and allocation-denied final grid publication.
  The allocation result excludes enabled capture's own work.
- The VT oracle reads actual presenter output and checks specified cells, ink and
  cursor through glyph changes, resize and tiny→normal. The first explicit
  narrow-row expectation omitted width clipping; correcting that expectation
  passes the direct recheck without changing product source or timeouts.
- Actual TerminalUI backpressure passes on Mac Debug: the UI reports native
  EAGAIN, the local worker acknowledges Ctrl-C before stdout draining resumes,
  and the eventual 36-row frame contains the exact draft, cursor and scrolled
  ROW034–063. Exit preserves the draft's byte cursor and restores terminal input
  mode. This isolates stdout blocking; it does not establish responsiveness under
  raw-capture capacity exhaustion or arbitrary domain callbacks.
- The presenter overlay `a12e20b4…` on Linux Clang18/libstdc++13 Debug, Release and
  ASan/UBSan passes all nine affected tests per profile (27/27),
  including Microsoft and readable screen regressions, the new blocked-output
  fixture, native renderer/composer, PTY/tiny and raw UI paths. Profile totals are
  15.78s, 13.85s and 23.99s respectively. The owned remote workspace is cleaned up.

One independent product challenge and the fixture's independent challenge found
no actionable issue. [Implementation notes](IMPLEMENTATION.md) record the bounded
work. Actual source-gate and performance outcomes follow below.

## Cancellation boundary repair

The first full Mac gate passes146/147 in278.18s; the existing slash `/cancel`
exec-settlement marker fails at its original3-second deadline. Full owned lint
does not run after that failed CTest command and no source commit is produced.

A [narrow source diagnosis and repair](CANCEL_BOUNDARY.md) covers a concrete
missed-stop obligation: raw admission or `tick()` can interrupt a selected wait
before its old cancellation branch. Both deterministic tests fail on the old
implementation because its child remains owned when wait returns. The repaired
Debug `command_raw` passes6.43s, including existing yield/background/raw cases.
The repaired overlay `7df6ab79…` also passes Linux Release `terminal_pty` and
`command_raw` (2/2 in10.47s). The latest historical PTY failure's exact raw path
was not recovered; these results establish the repaired boundary rather than
every prior Stopping observation's cause.

The repair uses the existing prepaid specific-job stop and joins outside the
registry mutex before propagating the original interruption. Refused wait
admission emits no invented request. It adds stack state on normal waits and no
per-tour allocation, worker, queue or syscall. All existing PTY assertions and
deadlines remain. Final ASan and full source-gate outcomes follow.

Final Linux ASan/UBSan passes `terminal_pty`, `command_raw` and
`command_jobs_engine` (3/3 in28.91s). The Mac full Debug recheck passes147/147 in
306.79s, including the unchanged slash-cancel episode. Full C++ analysis then
reports one test-helper diagnostic (swappable int/char parameters); the
`read_control(int)` replacement passes focused clang-tidy without suppression.
This changes only the blocked fixture after measurements, leaving all measured
production inputs unchanged. Lua analysis is not reached in that gate.

The mandatory final source hook fails145/147 in372.14s: `coding` times out60.11s
with no stage output (`arconaut-rln`), and `terminal_pty` fails its existing
quit-during-exec assertion at line460, through `exit_code()` line409, in11.18s
(`arconaut-xbf`). The earlier slash-cancel episode passes. No exact stage or
causal source attribution is established for either final failure. C++/Lua
analysis does not run after failed CTest; neither a complete source gate nor a
clean full-analysis result is claimed.

The two-layer bound is exhausted. Preserve all12 changed source files, their
archive/patch, exact base and logs under ignored context; restore active source
to `d987f6f` and remove the candidate native executable from the build target.
There is no unchanged retry, timeout increase, suppression or source-hook bypass.
`arconaut-rhi.3.3` remains blocked; publication here contains evidence only.

## Tranche performance

The [intermediate presenter round](performance/RESULTS.md) and
[final repaired-source round](performance/FINAL_RESULTS.md) each retain120 normal
processes and960 endpoint observations, with no exclusions or changed binaries.
Final short-input p50 changes2.389→2.481ms wide and1.401→1.390ms narrow. All
endpoint medians, quantiles and maxima remain in the linked tables, including
mixed and worsened tails. Shared-host activity is uncontrolled; neither a
whole-application speedup nor source causation is established. Both exact source
overlays and binary identities remain separate, and ordinary tail observations
stay in arconaut-f8f.

## Retained observations

`arconaut-cfj` records repeated serialized Mac Ninja premature-EOF recovery and
unchanged-source recompilation. No tool-policy/cache repair was attempted. An
initial Linux invocation used a nonexistent target name; the corrected actual
target completed the profiles above, with the failed invocation retained.
Existing cancellation-settlement, recorder-recovery, linker and performance-tail
findings remain in the [collective queue](../../docs/TUI_BUG_QUEUE.md); a later
pass alone does not repair them. The launched AFL work and its outputs were left
untouched throughout this tranche.

The independent [next-component source map](../tui-next-components-2026-10-15/SOURCE_MAP.md)
records the actual renderer/domain/VM gaps for later planning; it implements no
new host, command center or composition runtime.
