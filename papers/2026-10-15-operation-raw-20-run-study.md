# operation_raw: twenty unchanged Debug runs

2026-10-15. Operator requested exactly twenty repetitions and study of the
intermittent `operation_raw` failure. Completed twenty sequential runs: **19 pass,
1 assertion failure, 0 timeouts**. The original 120-second timeout did not recur.
The assertion failure is a scheduling assumption in the deadline test. Source
inspection also identifies a separate recorder shutdown race; these runs do not
establish that race as the cause of the earlier timeout.

## Method and results

Copied the existing Debug executable once and ran that unchanged copy twenty
times, with the build/debug working directory and the existing 120-second
per-run watchdog. No rebuild, source edit, additional repetition, or full-suite
rerun. The executable SHA256 is
`8329d038ac337116a5f7df418590512dbcf1fcb01d34d7fde1c0de4bc462d0db`.
Git base was `fa347ebaeb6586ef5c68894ac74a4ae6a03260ed`, with the staged economical
defaults candidate present. Raw runtime and operation test sources have no
changes relative to that base.

The driver timestamped stdout/stderr lines and retained per-run results. At 60
seconds it would sample the owned process tree and attempt LLDB inspection of
the recorder's stack and stop flag, before terminating only owned processes at
120 seconds. No run reached 60 seconds, so no live stack/flag capture occurred
and no diagnostic attach or watchdog termination affected a run.

| Run | Outcome | Seconds |
| --- | --- | ---: |
| 1 | Assertion failure | 40.643 |
| 2 | Pass | 46.295 |
| 3 | Pass | 45.430 |
| 4 | Pass | 41.587 |
| 5 | Pass | 28.371 |
| 6 | Pass | 27.051 |
| 7 | Pass | 28.967 |
| 8 | Pass | 28.663 |
| 9 | Pass | 27.772 |
| 10 | Pass | 34.061 |
| 11 | Pass | 41.729 |
| 12 | Pass | 28.740 |
| 13 | Pass | 27.876 |
| 14 | Pass | 27.739 |
| 15 | Pass | 33.523 |
| 16 | Pass | 47.818 |
| 17 | Pass | 43.858 |
| 18 | Pass | 34.525 |
| 19 | Pass | 28.017 |
| 20 | Pass | 29.041 |

Passing runs: minimum 27.051s, median 29.041s, maximum 47.818s. Sum of run
durations: 691.705s. Starting host load averages were 78.30/46.99/30.72; a final
observation after completion was 7.42/24.99/33.43. These are context, not an I/O
measurement or proof of why an earlier run timed out. This study ran one test at
a time; the mandatory gate runs six concurrently (`scripts/rigor`).

Local captures: `context/operation-raw-study-20261015T071956Z/`, with manifest,
copied executable, aggregate results, per-run output and timestamped phases.
Driver: `context/run-operation-raw-20.py`; progress log:
`context/operation-raw-study-driver.log`.

## Observed assertion failure: deadline expires before the blocking wait

Run 1 failed after `full DATA deadline`, reporting
`actual blocked lane-space admission interrupts before external write`.
[blocked_admission](../tests/operation_raw_test.cpp) lines 1099–1126 stops the
recorder, fills DATA, and gives the operation 20 milliseconds before requiring
all three of: refusal, a witnessed blocking lane-space poll, and no output file.
[The native poll observer](../tests/operation_raw_native.hpp) lines 401–426 sets
`space_wait` only for a nonzero-timeout poll watching both cancellation and a
STREAM endpoint. [check_wait_control](../src/raw_runtime.cpp) lines 524–548
correctly returns ETIMEDOUT if the deadline has already expired before that wait.

The retained failed fixture was
`/tmp/blackbird-operation-baseline-lWkzLd`. Reading its closed run using the
existing native `blackbird-raw-reader` produced:

- One terminal record, site 2417, ordinal 191, branch Error (3), stage 2,
  ErrorCode interrupted (13), native ETIMEDOUT (60 on this host).
- No effect-request site 2406, effect-return 2407, dispatch-outcome 2421,
  or sites 8000/8001.
- `never-written.bin` absent.

The operation therefore threw the expected Error and performed no file effect.
Given the failed conjunction, the missing condition was `space_wait`. The test
assumes that execution reaches a blocking poll within 20ms; correct earlier
deadline refusal violates that assumption. It is a test failure, not the earlier
120-second timeout. Exact parsed counts/terminal bytes are retained in
`run-01/retained-record-analysis.json` under the capture directory.

## Separate source defect: signal delivery between check and indefinite poll

[record](../src/raw_runtime.cpp) lines 1021–1024 checks `recorder_stop`, then calls
an indefinite poll. `recorder_signal` only writes that flag. A SIGTERM or SIGUSR1
handled after the check but before entering the kernel poll leaves the flag set
with no pending signal to interrupt the subsequent wait. With no other readable
endpoint, the recorder can wait indefinitely. The test's interposed poll performs
descriptor inspection before forwarding the native call, widening this window.

`stop_recorder` sends one signal and waits for the recorder. Runtime destruction
waits for the supervisor. The resulting blocked chain would be:
application destructor/waitpid → supervisor stop_recorder/waitpid → recorder
poll, with recorder_stop nonzero. A live nonzero flag at that blocked poll would
distinguish this candidate from ordinary idle polling. No such state was
captured here. The earlier last marker `provider variant 3` covers two fixture
calls and their teardown; it cannot identify this shutdown path by itself.

## Follow-through

Keep `arconaut-7qn` open. The twenty runs are complete, not a waiver of the failed
commit gate. The economical-defaults source candidate remains uncommitted and
unmerged. No timeout increase, assertion suppression, production fix, or running
executable activation was made during this study.

The concrete repair unit is to make recorder stop notification persistent across
the check/wait boundary, and make the test distinguish already-expired admission
from expiry during an actual blocked wait without relying on scheduler speed.
A direct native cut can deliver the stop signal immediately before the real poll
and require teardown to finish while preserving originals; cover graceful drain
and explicit recovery stop. Preserve exact interrupted/no-effect assertions for
the deadline cases. Those are proposed repairs, not results of this experiment.
