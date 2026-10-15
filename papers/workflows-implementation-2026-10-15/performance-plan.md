# Native host development benchmark — pre-code plan

2026-10-15. Own `scripts/bench-workflow-host` and this plan only. Root owns all
builds, execution, qualification, source integration and shared records. No provider
or account requests, new packages or native source changes.

Use the explicitly supplied `--executable` to launch a private short-path temporary
session and real `--serve` host. A bounded pool of `--clients` external CLI workers
submits `--tasks` echo tasks via explicit JSON `--request` interchange. Create an
all join, verify actual task outcomes/arguments and stable invocation/attempt/result
identities, and check selected member order plus task revision references. Gracefully
stop the launched host; reopen the same session, verify readiness, exact selected
snapshot and retained exact task results, then stop again. Default finite run:
three samples, 32 tasks each, four concurrent CLI clients. `--workers` aliases the
client concurrency setting; current native host source fixes its own pool at four
workers/32 live slots and the helper does not pretend to reconfigure that pool.

Record every external CLI call's start/end/duration, phase, request, exact stdout/
stderr, exit status and parsed-response status in operator-selected output JSONL.
Record every sample and error in a final JSON report, including readiness, submit,
settle/join, graceful close and reopen wall times plus request/response counts.
No samples/outliers are removed. A timeout is a failed sample, never absent data.
Results include process spawn, explicit JSON CLI parsing/formatting, BBM framing,
owner/journal/native execution/raw capture, polling and Python orchestration where
the phase includes them. This is not a native hot-loop or competitive benchmark.

Study-based retention handling: RawRuntime creates `session/raw/<run>/events.*`
and registration metadata; its large producer lanes are separate POSIX named
shared-memory objects (`RawChannel::shm_open`). Do not copy preallocated mappings
or use cross-filesystem directory moves. Keep each private `/tmp` session and record
its locator. After graceful shutdown inventory regular session files with separate
logical `st_size` and allocated `st_blocks*512` totals. Hash actual audit files and
raw emitted `events.*`, outside timed host phases. Never scan/copy global shared
memory. The report states that `/tmp` records remain subject to operator/OS cleanup.

Readiness, CLI requests, task settlement and host exit have explicit finite
timeouts. On any failure retain output/report/logs/session data and terminate,
kill if necessary, and wait only the exact Popen host launched by this helper.
Each client subprocess is bounded and joined by its invocation; no process-name
kills, global signals, broad directory cleanup or unrelated native tools.

Verification for this helper: Python syntax compilation without writing bytecode,
argument/help review and relevant diff checks. Root then runs the actual finite
baseline once its native host path is green. Do not add a fresh review layer.

The helper is executable. Example root-owned baseline command (use a new output
directory and the actually qualified executable):

```sh
scripts/bench-workflow-host --executable build/debug/blackbird-workflow --output context/workflow-host-baseline-2026-10-15 --samples 3 --tasks 32 --clients 4
```

Only syntax compilation, `--help` and relevant diff checks were performed by this
lane. These do not qualify a real benchmark execution. CLI samples preserve exact
stdout/stderr as hexadecimal alongside readable text; JSON response counts include
valid service refusal responses as responses, while sample oracles still reject
them. Task and selection checks use the current native snapshot's `revision`
field spelling. Interrupted attempts are written to their sample artifact and
included in the final report rather than omitted.
