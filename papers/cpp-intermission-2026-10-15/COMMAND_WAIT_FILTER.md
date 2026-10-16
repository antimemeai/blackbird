# Linux filtered wait ECHILD loses a still-owned command child

Root subsequently escalated this demonstrated custody defect to P1 arconaut-vyi
and authorized the eighth repair before publication. The diagnosis below records
its initial scope recommendation; implementation and permanent checks follow
[WAIT_SELECTOR.md](WAIT_SELECTOR.md).

New finding from the added ordinary Linux Release command_jobs qualification,
not an established regression from the scoped Child fix. The original private
failed fixture `/tmp/bb-job-test-0ww3Ba` is preserved on the owned campaign host.
The test's final requirement that every direct child has been reaped failed;
that assertion was not weakened or repeated unchanged to seek a pass.

An isolated diagnostic copies the exact qualified remote test source, records
actual waitid/waitpid results into a bounded preallocated array, then prints its
actual final wait result and its own fixture PID mapping. It uses the configured
Clang18 Release flags, the same foundation archive and same command_job_fixture.
It neither substitutes syscall returns nor edits qualified source or binaries.
The diagnostic reproduces the failure in one run:

```
FINAL_WAIT result=1166051 errno=2 status=0 owned=0 uncontained=1
continued fixture/job8 leader PID=1166051
legacy Child PID=1166106 (exact positive reap recorded separately)
waitid(1166051,WEXITED|WNOHANG|WNOWAIT): result0, observed0
waitid(1166051,WSTOPPED|WCONTINUED|WNOHANG): result-1, errnoECHILD
waitid(1166051,WEXITED|WNOHANG|WNOWAIT): result0, observed1166051, CLD_EXITED0
final waitpid(-1,WNOHANG): result1166051, exit0
```

The final wait's errno2 is stale on its successful positive return; it is not an
error claim. This identifies the stranded child as CommandJobs' continued job,
not native Child. CommandJobs' observe_wait currently interprets **every** ECHILD
as lost custody, sets local reaped=true, and suppresses its final authoritative
waitpid. The status-only ECHILD arrives after the leader exits between its two
different wait selectors; a subsequent exit-inclusive WNOWAIT still observes the
same unreaped child. The fixture's final catch-all query actually reaps it.

Read primary [Linux v6.8 kernel/exit.c](https://github.com/torvalds/linux/blob/v6.8/kernel/exit.c#L1016-L1017):
wait_task_zombie returns without reporting a zombie when WEXITED is absent;
wait_consider_task returns this result for the ordinary single-thread zombie,
leaving the no-eligible-child error. A status-only selector therefore cannot
establish absence of owned exit custody. This pinned source read is explanatory;
the diagnostic records the actual qualified host behavior without claiming its
running kernel is precisely that upstream revision. No kernel code is adopted.

The source section is unchanged by this campaign's mailbox edit. Recommended
separate P2 bug: filtered status ECHILD must not revoke exit/reap authority;
confirm ownership through an exit-inclusive wait and preserve actual ECHILD loss
there. Decisive permanent RED should force the real child's exit between the
initial exit query and status query, then require the exact owned final reap.
Do not change the final no-direct-child assertion. No product repair or new
campaign has been started by this diagnosis; root owns severity/scheduling.

Exact diagnostic source, compiler arguments and source/archive/helper hashes,
build/run logs are ignored under
`context/cpp-intermission-2026-10-15/child/linux-command-*`. The temporary diagnostic
and its fresh failed fixture are retained under the same owned remote campaign
(`diagnostic-child-briilj92`, `/tmp/bb-job-test-USkoFs`). Only those owned fixtures
were read. No AFL/global process inspection, native user session, provider,
main integration, qualified-source edit or Ninja build was involved.
