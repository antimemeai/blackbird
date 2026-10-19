# Campaign defects: handoff (2026-10-18, paused by the operator)

The work is defined in [CAMPAIGN_DEFECTS_SUBPLAN.md](CAMPAIGN_DEFECTS_SUBPLAN.md). The
operator's request was "fix those defects first, then if mimo needs more time it needs
more time w/e, 40 runs is good, 200 more is better." The operator stopped for a break
with the units partly landed. Nothing is merged into master. Nothing of ours is
running: every agent is idle, and leaked test processes were killed by PID.

Operator steering during the session:
- Units run **one at a time** on the laptop. Resume each only when the previous one
  completes.
- The load average is not CPU load. CPU stayed about 60% idle while the load average
  read 100 to 170, because idle system daemons sit in the runnable state. Check CPU
  idle and the stall monitor before blaming load.

## Where every unit is

| Unit | Bead | Branch / worktree (`context/…`) | State |
| --- | --- | --- | --- |
| A provider deadlines | `arconaut-pyg`, `arconaut-3nx` | `work/defect-pyg-3nx-2026-10-18` / `defect-pyg-3nx` | **Done.** `496ff29`, pushed; hook 264/264 |
| C test process trees | `arconaut-j1l` | `work/defect-j1l-2026-10-18` / `defect-j1l` | **Done.** `651e0be`, pushed; hook 264/264 |
| G implicit copy | `arconaut-vv6` | `work/defect-vv6-2026-10-18` / `defect-vv6` | **Done.** `2610af9`, pushed; hook 263/263 |
| F provider settings | `arconaut-5st` | `work/defect-5st-2026-10-18` / `defect-5st` | **Staged, uncommitted** (23 files). The hook failed 4 times because of the host stalls (below). Each failing test behaves the same on the vv6 baseline. Commit message: `context/COMMIT_MSG.txt` in its worktree |
| B command output | `arconaut-3o2` | `work/defect-3o2-2026-10-18` / `defect-3o2` | **Staged, uncommitted** (10 files); paused before its hook commit. Message: `context/COMMIT_MSG.txt` |
| D linker pin | `arconaut-e5g` | `work/defect-e5g-2026-10-18` / `defect-e5g` | **Staged, uncommitted** (6 files); paused before its hook commit. Message: `context/COMMIT_MSG.txt` |
| E Mac TSan | `arconaut-0zi` | not started | Waits for C and for a host without process-table stalls |
| — raw terminal records | `arconaut-e8n` (P1, new) | not started | 15 completed requests lack their 1302/1303 records |

The integration branch `work/campaign-defects-2026-10-18` is at local `eb277f8`, which
merges j1l and vv6 into `48b340c`. Those merge commits did not run the gate, because
the installed hook is pre-commit only. Origin still has `48b340c` (this handoff commit
moves it).

Merging pyg-3nx conflicted in one place: `CMakeLists.txt`, in the `child_custody`
foreach. Resolve it as
`foreach(custody_case … raw-cleanup stall)` + `blackbird_add_test(…)`. j1l makes
configure fail on any plain `add_test`. The merge was aborted for the break, so redo
it.

## Next steps, in order, one at a time

1. **Settle the host stalls first.** Otherwise every full gate fails at random. See
   "Host finding" below.
2. Commit 5st, 3o2 and e5g through
   `~/projects/blackbird_runs/bin/hook-lock git commit -F <msg>` in their worktrees,
   then push.
   - Each staged worktree keeps its commit message in its ignored
     `context/COMMIT_MSG.txt`.
3. Merge pyg-3nx, 5st, 3o2 and e5g into the integration branch, resolving as above.
   Run `scripts/rigor check debug` once on the merged tree, plus `scripts/check-linux`
   (debug/release/asan) on Neuroses.
4. One independent review round of the merged diff, then fix its findings (layer 2;
   no third layer).
5. Unit E (TSan) and `arconaut-e8n`.
6. Campaign 2, in `~/projects/blackbird_runs/2026-10-18-defects/`:
   - `tasks/` holds t01, t02, t04–t07 and the new t09–t19.
   - The five "helpful" tasks (t20–t24: adversarial reviews of the pyg/3nx, 3o2 and
     j1l patches, a TSan timing audit, a raw stream-gap tool) are not written yet.
     Seed them from the merged tree.
   - Plan: about 22 tasks × {luna high/xhigh, Paper max, Paper high, MiMo high} ×
     {Release, ASan+UBSan}, plus extras, for about 200 runs.
   - Runner: `bin/queue-worker CAMPAIGN QUEUE TAG` pulls lines
     `NAME PROVIDER MODEL EFFORT BUILD TASK CONTINUES [SEED]`.
   - Set `BLACKBIRD_CHECKOUT=<merged worktree>` and `PACK_RUNS=1`; with `PACK_RUNS=1`,
     each finished run's `session/` is packed losslessly by `bin/pack-run`.
   - With the pyg fix, MiMo and Paper-max requests are no longer cut at 180 s. MiMo
     gets the time it needs.

## Host finding: intermittent process-table stalls

`~/projects/blackbird_runs/bin/proc-stall-watch LOG` times `ps -A` every 10 s. It
logged 5 s, 13 s and 17 s listings at 22:39–22:41Z, during the hook run in which 53 of
264 tests failed: timeouts, `io:60`, `corrupt:0`, and "reader did not consume exact
input". The log is `~/projects/blackbird_runs/proc-stalls.log`.
- The j1l agent independently measured `sysctl(KERN_PROC)` blocking for 1 to 10
  minutes, one call 585 s, while libproc answered in about 2 ms.
- In all three stall samples, the top user process was Task Manager TMOG (PID 10529,
  32–61% CPU); WindowServer was at 54–92%, and Activity Monitor was also open.
- **Hypothesis, not proven:** a process monitor that repeatedly scans every process
  and thread contends for the kernel process-list lock that fork, exec, wait and `ps`
  need.
- **Proposed check:** quit TMOG and Activity Monitor for about 15 minutes, run the
  stall monitor and one full suite, and compare.

Other sessions were also running heavy work: the verbs corpus builds, rapid-mlx, and
fcc_rfc_analysis.

## Unit summaries (from the agents' reports)

**A, pyg + 3nx (`496ff29`).**
- `include/blackbird/provider_timeouts.hpp` holds the defaults:
  - stall 120 s;
  - ceiling 3600 s;
  - limit 86400 s;
  - curl `--max-time` = ceiling + 30 s.
- They come from `~/projects/blackbird_runs/2026-10-17-cpp26/STREAM_TIMING.md`. Over
  762 requests, the longest wait without bytes was 11.5 s, and luna emits a reasoning
  item every 3.5–4.3 s while thinking.
- `Child` counts stall time only while it polls output.
- A stall raises `provider_transport:28` and is retried. The ceiling raises
  `incomplete:60` and is not retried. `interrupted` was avoided because it means
  operator cancellation elsewhere in the code.
- `ProviderDeadline` scopes consume exactly their own expiry marker, the same rule as
  `RawChildTrace::permit`. This fixes 3nx; the old marker also poisoned the Lua
  runtime's `admission_failure`.
- `--stall-timeout` / `--request-timeout` work for every provider and carry through
  `/new`, `/resume` and restart.
- Raw event 1300 gains `stall_timeout_seconds`.
- Not changed: the decision-model transport keeps its own curl.

**C, j1l (`651e0be`).**
- CTest 4.3.0 kills only the descendants that are linked by parent at the moment of a
  timeout. It kills nothing when its direct child has already exited. It uses
  SIGKILL, so fixture cleanup never runs.
- `tests/case_launcher.cpp` runs every case through `cmake/BlackbirdTests.cmake`
  `blackbird_add_test`:
  - each case gets its own process group and a `BLACKBIRD_TEST_CASE` marker;
  - CTest's `TIMEOUT_SIGNAL_NAME SIGTERM` triggers cleanup, with a 60 s grace period;
  - the launcher also cleans up on runner death;
  - leftovers after a case exits fail that case.
- `rigor`/`check-linux` fail on orphans that carry `BLACKBIRD_TEST_RUN`.
- The tui fixtures stop their own daemons (`scripts/tui_session.py`).
- New test `case_containment`.
- Known limit: a descendant that clears its environment and leaves the process group
  before its parent exits is not caught.

**G, vv6 (`2610af9`).** `CodingProvider` declares protected, defaulted copy and move.
`-Wdeprecated` joins the warning set, and the full debug build is clean.

**F, 5st (staged).**
- `src/provider_settings.cpp` and `include/blackbird/provider_settings.hpp` hold a
  constexpr table keyed by protocol and exact base URL. It covers:
  - OpenAI API;
  - Codex backend;
  - Anthropic;
  - OpenRouter;
  - Paper;
  - MiMo pay-as-you-go;
  - MiMo Token Plan cn/sgp/ams.
- Each entry records efforts and their meaning, equivalent levels, models, plan
  restrictions (Token Plan rejects ultraspeed), output ceilings, unsupported
  parameters, and doc URLs read on 2026-10-18. Doc excerpts are in
  `papers/provider-settings-2026-10-18/`; the description is in
  `docs/PROVIDER_SETTINGS.md`.
- `--effort`, `/effort` and `/model` are validated against the table, and a refusal
  lists the accepted values.
- HTTP 4xx messages reach the console (`[provider HTTP 400: …]`) and a
  `provider.error` audit entry.
- The effort vocabulary widens to none / minimal / low / medium / high / xhigh / max /
  ultra.
- Operator decisions recorded in the report:
  - Codex ultra is refused;
  - unknown models are refused on endpoints with a closed model list;
  - MiMo v2.5 models retire on 2026-10-21;
  - `claude-mythos-preview` xhigh is now refused.
- Tests: the new `provider_settings` test, and an `http_400` mode in
  `scripts/check-registered-responses`. One of the two allowed live calls was used.

**B, 3o2 (staged).**
- The 9.5 GB was not command output, which was only about 24 MB. It came from 436
  retained-state checkpoints at about 20 MB of raw each.
- In a reopened (compact) session, a checkpoint cannot free the tail while an
  operation is unresolved, and its retry counter was reset after each one, so every
  append re-ran a checkpoint.
- The `capacity:0` was the 1024-record resident-tail limit.
- Fix:
  - a checkpoint gate (`retained_state.cpp`);
  - a new `exec` argument `output_limit_bytes`, default 1 MiB, which the model may
    raise up to 16 MiB. Past the limit the command is stopped and the result is
    `error=output_limit`; the turn continues and every byte read is kept.
- New test `command_output_budget_engine`, red in both ways it should be.
- Follow-ups:
  - `arconaut-avo`: the tail can still fill while an operation is unresolved. This is
    a design decision for the operator.
  - `arconaut-ulx`: about 20 MB of raw per checkpoint, growing with archive size.

**D, e5g (staged).**
- `/usr/bin/ld` is an xcrun shim (ld-1167.5 with the Command Line Tools, ld-1230.1
  with Xcode). Anaconda's ld64-711 came first on PATH.
- All six presets pass
  `--ld-path=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld`.
- `local-profile.json` records ld-1230.1. `rigor` doctor and every build check the
  linker's identity and that clang actually selects it.
- New test `rigor_linker`. The fuzz profile links and `bytes_fuzz` passes.
- The operator's five ld crash snapshots in `/private/tmp` are untouched.

## Other new beads this session

| Bead | Priority | Finding |
| --- | --- | --- |
| `arconaut-e8n` | P1 | 15 completed requests are missing their raw 1302/1303 records |
| `arconaut-jaw` | P2 | `candidate` fails "unknown side effect not replayed" intermittently |
| `arconaut-avo` | P2 | Resident-tail capacity design; discuss with the operator |
| `arconaut-ulx` | P3 | Raw volume per checkpoint |

## Trace archive

- `~/projects/blackbird_runs/2026-10-17-cpp26/`: all 39 runs packed. Each has
  `session.tar.zst` and `session.sha256`; every pack was verified by a streamed
  byte-exact round trip. Size 18 GB → 5.3 GB.
- Restore a run with
  `tar -xf RUN/session.tar.zst --use-compress-program=unzstd -C RUN`.
- `STREAM_TIMING.md` and its analyzer scripts (in `bin/stream-timing/`) are there too.
- Runner tools in `bin/`:
  - `run-one`, now taking `BLACKBIRD_CHECKOUT` and `PACK_RUNS`;
  - `queue-worker`;
  - `pack-run` and `pack-verify.py`;
  - `hook-lock`;
  - `proc-stall-watch`;
  - `collect-run`, `continue-run`, `confine.sb`.
- `continue-run` needs an unpacked `session/`.
