# Participant final-original custody

The audit's R1 schedule lets the first archive drain save a snapshot while a
worker publishes its last emergency original. Business completion permits a
native join; it does not permit destroying an unretained original. The real
owner is `Run::emergency`, independently of the ordinary capture queue and raw
worker cleanup. The studied RocksDB detach/free separation and Linux kref owner
rules in [REGISTRIES](../cpp-bug-class-audit-2026-10-15/REGISTRIES.md) apply here;
no dependency or capacity increase is required.

The permanent regression uses the actual capture ticket API and public Save and
Publish callbacks to force the legal late-publication schedule. Its exact-used
9 MiB payload contains a known byte pattern. Successful archive must retain that
exact pattern once, retain earlier ordinary originals once, finish both worker
calls, and remove the row with clean debt. A throwing Retain must leave the row
and debt until an explicit later owner drain succeeds; unknown-used extents must
leave the row and debt after repeated explicit archive attempts and native
shutdown. Neither failure may trigger another transport call.

First run those assertions against unchanged product libraries and preserve the
failure log. Then join and close native cleanup before a final ordinary drain;
only a joined, cleanup-closed run with no outstanding emergency or queued
original can be removed. Extract the run owner under the mutex and destroy it
outside. Likewise extract drained capture owners before releasing their payloads
outside the mutex. Keep admission/send APIs unchanged and retain the existing
owner callback reentry rule. Root adds the dedicated test target, coordinates
builds, and independently challenges this code once. Targeted native checks
precede the shared final qualification; no new broad review/fuzz campaign.

## Direct old-red

`tests/participants_custody_test.cpp` ran each scenario separately against the
unchanged Debug libraries. All three exited1 at the intended custody assertion:
exact: `archive destroyed late original without retaining exact bytes once`;
throwing and unknown: `archive falsely succeeded with unresolved late original`.
The source before edits was SHA256
`d585597ec7591263ae4b5e093a2400b645f612671b8bd84b9429fa24ac382393`;
colleague library `90dbdb08ba486b05f06201e1468f0cd15e31c6b09d033ca345e622d612a4fa49`,
foundation library `a013b56e35fa4061b6bceaad9216a4b565f137b3c4aabcb9bbd4574bc2c3a5a9`.
The exact test had SHA256
`6dec78e881534c60dd9714f19c3b6be7cfb4b399014bad8e2ce1ce22d9a5a7da`.

Build used the qualified Clang23 compiler, C++20, active Xcode SDK and `-I include`,
linking `libblackbird_colleague.a libblackbird_foundation.a` directly. No old
product rebuild or shared Ninja invocation was needed. Binary and individual
`old-red-{exact,throwing,unknown}.log` files remain under ignored
`context/cpp-intermission-2026-10-15/participants/`.

## Implemented boundary

Archive now joins and closes native cleanup, then drains originals again before
removal. `archivable_locked` requires finished business state, joined worker,
closed native cleanup, no emergency and no ordinary capture referring to that
run. Retain or unresolved-used failure throws before map removal. Only explicit
subsequent owner operations attempt retention again; the transport is never
retried. Removed Run and drained Capture owners are extracted under the guard,
with payload/raw-handle destruction after unlocking. Existing8 MiB queue and
per-run emergency bounds, owner callbacks and admission/send are unchanged.

## Green and bounded independent recheck

Root registered one CTest per explicit case and performed the single concrete
code/test recheck. It found no additional source correctness defect: post-join
inspection, retention-failure ownership and outside-lock releases fit the plan.
After root's coordinated Debug build, affected checks passed5/5 in8.26s:
`participants`0.32s, `participants_raw`7.45s,
`participants_custody_exact`0.45s, `participants_custody_throwing`0.02s and
`participants_custody_unknown`0.02s. The raw-active existing fixture checks
physical originals, native worker/cleanup events, retained extents and unresolved
semantic debt; the new deterministic fixture supplies the missing late archive
schedule. The command selected precisely these five CTests:

```sh
ctest --test-dir build/debug \
  -R '^participants(_custody_(exact|throwing|unknown)|_raw)?$' --output-on-failure
```

Complete output is ignored
`context/cpp-intermission-2026-10-15/participants/green-debug.log`.
Full shared qualification/sanitizers remain root's integration work. This lane
ran no provider calls, retry campaign, source hooks, AFL operations or broad
test suite and added no library.
