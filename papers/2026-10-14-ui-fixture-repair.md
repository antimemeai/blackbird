# UI fixture repair

The first-state run found two distinct UI test defects. Repair only their owning
fixture and run `ui_raw` in macOS/Linux Release and ASan/UBSan. Keep production
emission, all native effect/disk assertions, and watchdogs unchanged. No performance
changes or broader gate are part of this unit.

## Producer and borrowed credit lifetime (arconaut-fir)

`actual_terminal_queue(UiCut::completion)` deliberately leaves a zero-credit
`RawUiBurst` active after its perform callback returns. The actual terminal worker
then refuses its stage-three complete publication with `ErrorCode::capacity`.
The old independent TLS bank and loan outlived the worker's `RawThreadBinding`;
bank destruction read an already freed producer through `RawCapacityClaim`.

Keep one TLS fixture aggregate with members in this order: shared issuing thread
context, bank, loan. Capture the existing `raw_current_thread()` handle before
claiming the zero-credit bank. Reverse member destruction removes the loan,
releases bank credit while its producer is alive, then destroys that retained
context. The existing assertion still requires the real worker's stage-three
capacity refusal after join, exactly one native effect, a preserved failure event,
and no successful complete-message original. No detached claim, leak or suppression.

## Linux allocation mismatch (arconaut-ji3)

The original Linux ASan run reports operator-new versus free on 200 bytes, but
its symbolizer was invalid and the runner removed the executable. Preserve the
new remote executable and symbolize the direct failing stack before editing the
allocator. The fixture replaces throwing scalar/array new and delete only.
Qualified libstdc++13 `bits/stl_tempbuf.h` obtains temporary buffers through
nothrow new and releases them with ordinary delete. `palette_matches` uses
`std::stable_sort`; this is a source-grounded hypothesis pending that actual trace.

If the direct stack confirms this family, add nothrow scalar/array new and delete
overloads to the fixture, forwarding through its existing malloc/free pair and
allocation fault selection. No aligned allocation call was found in the owning
product path; do not add unused families or disable sanitizer mismatch detection.

Root owns the shared Ninja trees and the six-worker total. This unit launches no
builds or native tests until assigned workers. Finish with the affected native
case results, changed-TU format/lint and one bounded source/oracle recheck.

## Implemented lifetime change and source review

The fixture aggregate now retains the shared issuing context before its bank and
loan members. Root independently reviewed that ownership and reverse destruction
order; no findings. The original capacity, stage-three, native-effect and preserved
disk assertions are unchanged. Changed-file formatting and whitespace checks pass.

The first affected macOS Release invocation passes `ui_raw` in 0.42 seconds
(`context/qualification-repair-2026-10-14/release-tests.log`). Root runs this
fixture within its affected case selection; no unrelated suite is requested here.

The lifetime-only macOS ASan/UBSan invocation passes in 6.83 seconds. The Linux
lifetime-only ASan invocation reproduces the allocation mismatch in 2.85 seconds;
its owned scratch `/tmp/arconaut-linux-repair-vzoRozsR` and executable are retained.
`addr2line -C -f -i` on that exact executable confirms allocation by nothrow new,
`std::get_temporary_buffer<unsigned long>` (`stl_tempbuf.h:113`), `std::__stable_sort`
and `palette_matches` (`terminal.cpp:202`), entered from the original
`review_palette_and_holder_cuts` (`ui_raw_test.cpp:821`). Its free stack passes
through `std::__detail::__return_temporary_buffer` and the fixture's ordinary
`operator delete` (`ui_raw_test.cpp:47`). The symbolized frames and original ASan
trace are retained in `context/qualification-repair-2026-10-14/linux-ui-symbolized.txt`
and `context/linux/run-dhkzxcfd/checks.log`. No source attribution rests on the
invalid external-symbolizer warning.

The fixture now provides nothrow scalar/array allocation and deallocation overloads.
They forward through its existing throwing scalar allocator/free pair, converting
`std::bad_alloc` to null only in the nothrow entrypoint. Existing thread-local fault
selection still applies. Formatting and whitespace checks pass; post-change profile
results and TU diagnostics follow root's coordinated affected-case runs.

## Final affected case results

Root independently reviewed the four new nothrow entrypoints: scalar bad allocation
becomes null; array allocation delegates to the same scalar path; deallocations use
the same free pair. No findings. This is the one bounded source review; no broader
review or additional unrelated native run follows.

| Platform/profile | Final `ui_raw` result | Seconds |
|---|---|---:|
| macOS Release | PASS | 0.39 |
| macOS ASan/UBSan | PASS | 6.80 |
| Linux ASan/UBSan | PASS | 3.92 |

These post-change invocations run only the owning `ui_raw` case. Each target-only
rebuild succeeds; all owned build/native processes finish. Sanitizer settings remain
unchanged and no suppressions are added. Full existing fixture groups execute,
including the actual stage-three completion refusal and unchanged exact-byte/effect
checks. Logs are `ui-{release,asan,linux-asan}-final-{build,tests}.log` under
`context/qualification-repair-2026-10-14`. Linux Release and changed-TU diagnostics
are coordinated by root in the affected repair matrix and reported there.
