# Exit custody requires an exit-inclusive wait selector

Issue arconaut-vyi. The ordinary Linux Release command_jobs final-reap assertion
exposed a second custody defect: a status-only waitid ECHILD is treated as absence
of the direct child. [The diagnosis](COMMAND_WAIT_FILTER.md) records the actual
continued child, filtered ECHILD, later WNOWAIT exit and final stray reap.

Read [Linux v6.8 kernel/exit.c](https://github.com/torvalds/linux/blob/v6.8/kernel/exit.c#L1016-L1017):
wait_task_zombie excludes exit without WEXITED, and the ordinary zombie path can
leave no-eligible-child ECHILD even while the direct child remains unreaped. The
native selector is part of the meaning of its result. A filtered state failure
cannot revoke exit custody; an exit-inclusive ECHILD still must revoke it and
suppress any later numeric group action. The scoped implementation will require
WEXITED in observe_wait before marking total custody unavailable. Status-only
ECHILD remains recorded exactly and the ordinary next exit query resolves it.
No selector/result will be normalized, and there is no extra native production
poll/query, synthetic success, annotation framework or global custody policy.

Permanent RED first: private self-child blocks on its FIFO. A syscall seam first
observes a real exit-inclusive noevent query, releases the child, and observes its
real exited state with WNOWAIT to keep its PID pinned. Only then can the worker
make its filtered state query. Linux uses the actual filtered ECHILD. Mac may
return that valid Linux result only after independently proving the same actual
exited-child pin. Require exactly one actual native final reap, no stray child,
restored owned counter and native_custody_reaped=true. Arbitrary exec intentionally
sets uncontained_exec globally; the test must not erase or misinterpret that
separate containment policy.

A separate lost case externally reaps this same acquired child, then requires an
actual exit-inclusive ECHILD to revoke authority and emit no later kill or bogus
successful reap. A raw-active filtered counterpart uses the existing RawRuntime
and requires actual recorded status-only ECHILD, subsequent exited WNOWAIT and
exact successful final2033 reap. The fixed source must pass the same checks; root
performs one concrete independent challenge within this implementation layer.
Root owns CMake/gates/journal/queue; no old child assertion is weakened.

## Direct implementation and results

The permanent test is `tests/command_wait_custody_test.cpp` with three private
modes: filtered, filtered-raw, lost. Both filtered modes fail the correct physical
owner assertion against the unchanged implementation, while actual exit-inclusive
ECHILD loss succeeds as the counterpart. This RED holds on both Mac's explicitly
permitted Linux-return model and the actual Linux Clang18 Release profile; Linux
uses the actual kernel's filtered ECHILD, without substituting a native result.
The old fixture's stranded child is safely reaped after observing its actual
WNOWAIT pin, before reporting the RED failure, so the regression itself leaves no
child behind.

The product change only requires `(options & WEXITED) != 0` before ECHILD can mark
worker custody unavailable. The filtered query/result remains in raw records,
and the existing next exit query observes/reaps the child. Existing command_jobs'
final no-direct-child assertion remains unchanged. The new physical oracle also
requires sealed=true and restored native owner count; the raw counterpart decodes
actual2022/2023 selectors/results and exact2033 final native reap. The loss
counterpart actually externally reaps its acquired child and records a real
exit-inclusive ECHILD; a signal seam refuses any later numeric signal instead of
risking another process. It requires no bogus successful owner reap/sealed state.

Debug affected5 pass8.94s, including existing command_jobs and command_raw plus
new3. Direct clang-tidy on the test initially flags repeated optional access in
the raw snapshot predicate; an explicit stable reference/null branch preserves
the oracle and cleans the owning TU. The source TU is also clean. The final new3
Debug recheck passes0.86s. ASan/UBSan affected5 completes successfully; TSannew3
and root's independent concrete challenge are still in progress as this entry is
written. Exact logs/source copies are ignored under the same child context:
wait-selector-{red,linux-red,debug,debug-final,asan,tsan} and matching build/tidy
files. No source gate, commit, main merge, user session, provider or AFL action has
been performed by this lane.

Final affected checks: ASan/UBSan5/5 pass18.07s and TSannew3/3 pass5.03s. Both
owning-TU clang-tidy runs are clean. All Ninja leases are released; product source
and permanent fixture are stable for root's independent review and final
cross-platform/source gate. The actual qualified Linux old source's failure is
retained as RED, not overwritten with a diagnostic pass claim.

Root's single independent code/oracle review found no defect: actual exited-child
pin, selector-specific compatibility simulation, positive owner-only native reap,
real unfiltered lost-child negative control and raw observation sequence were
examined. The public sealed assertion includes terminal retention, complete
capture and actual reaped custody. No further review panel is required.
