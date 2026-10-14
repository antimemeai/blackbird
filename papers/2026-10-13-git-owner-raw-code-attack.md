# Git owner's one code and oracle attack

2026-10-13. One confined review of the Git unit after its owner's normal and
native-cut green handoff. Read complete `tests/git_raw_test.cpp`, the actual
Observations Git method/header and parsing helpers, Coding's explicit Git context,
the declared plan/dispositions, and Child's reached start/collect/destruction
paths. The shared lexical caller helper was reviewed with Decision; this pass
checks its actual Git use and does not reopen unrelated helper or Child units.
No build, production edit, new qualification or additional reviewer was used.

No concrete production defect was established in these confined changed paths.
One concrete oracle correction is required before this unit's finite recheck.

## G1 — assert the actual invocation terminals and acquired exit records

`tests/git_raw_test.cpp:125` records3704 only by its parent and ordinal. It does
not inspect the branch, stage, ErrorPresent/code/detail, terminal width, or count
duplicate terminals. `ends[invocation]` silently replaces a duplicate. A wrong
successful invocation terminal for the cancelled start, a malformed Error terminal,
or two terminals can therefore pass this test. The whole terminal assertions at
line124 do not repair that separate per-invocation claim.

The native cases parse any3706 that exists at line133, but never count or require
that record for each successfully collected child. Omitting the nonzero native
exit record in empty/nonrepository cases would still pass lines136–138. The
normal public Coding case counts three3706 records, which covers its three zero
exits, not these nonzero paths. Its generic3701 count also does not establish the
invocation outcome bytes.

Tighten the same existing oracle, without another oracle layer: assert exactly
one3704 for each3703; parse its complete literal shared-terminal variant. Current
normal/nonzero-native-exit children return normally from `collect(...,&code)`:
their invocation branch is returned1, stage0, ErrorPresent=false. Cancelled start
has error3, stage0, ErrorPresent=true, interrupted/detail0. Sample-clock bad_alloc
occurs after both completed invocation bodies, so their terminals still return;
the whole3701 is allocation4 at stage5. Assert that exact outer allocation stage
as well, currently unexamined when ErrorPresent=false.

Require one3706 for every actual successful collect, including nonzero Git exit,
with both actual parent refs and the independent native exit code. Cancellation
before spawn/collect must have none. Reject duplicates with checked insertion.
Verify the child/ref lane when comparing cleanup and terminal ordinals so an
ordinal comparison cannot silently compare unrelated producers. Keep the existing
native stdout/stderr, argv, acquired path and zero-child pre-effect checks.

The trigger is an integration mistake that writes the wrong per-child terminal or
omits a reached nonzero exit while preserving the outer returned sample. The
required oracle directly observes those current native records; no mutation
campaign, new child implementation or live account is needed.

## Source and coverage assessment

`src/observations.cpp:145–152` borrows/validates path before acquiring its owned
copy, pays48+actual size and captures it before count parsing. Invalid path stays
in Operation17's original; it does not fabricate an acquired path. Per-element
argv records at lines164–182 pay56+actual extent before construction, and the
existing bounded count formatter pays56+2 before to_string. Its body is captured
before the move. No joined command, environment or second formatter is emitted.

Each synchronous invocation creates its child inside the callable passed to the
paid3703/3704 scope. Child cleanup therefore precedes returned/thrown3704. Both
streams are selected explicitly at lines191–194; native1404 preserves bytes before
the existing collection/parsing/sample work. The68-byte3706 bank is held before
collecting the actual exit. Cancellation before start still owns the Child's
native lifecycle refusal/release, with no invented PID or successful exit.

The45-byte3707 bank is paid before the single actual HEAD predicate, reached only
after exit0. The cached boolean is reused in business sample/history decisions.
History's native bytes are retained before line/field parsing. Default direct
Observations calls remain unselected; Coding at `src/coding.cpp:4044–4048` passes
the actual current operation reference synchronously.

The fixture has a genuine unchanged public Coding invocation, independent known
commit/history literals, separately executed native stdout/stderr/error outcomes,
actual argv originals, cancelled start, zero/empty/nonrepository/invalid/default
paths, a real sample-clock bad_alloc after the child result, and an exact
one-byte-short original-path bank with zero reached child start. That is useful
direct owner coverage. This review does not claim forwarding syscall-fault,
arbitrary history-corruption, producer contention, performance or full native
Child qualification from those Git cases.

Resolve G1 in the same fixture and run the owner's one finite recheck. No second
code panel or review of the repaired oracle is requested.

Root G1 disposition: the SAME owner fixture now decodes each3704 exactly, asserts
returned43/cancel Error52 with stage0, one terminal per actual invocation, one3706
per actual collect including nonzero exit, none on refused start, allocation outer
stage5, and child/invocation same-lane cleanup ordering. Direct finite recheck passes
in context/caller-owner-source/manual/git-finite-recheck.log. No production fix
was required; no rereview follows.


Final source-gate static plan: path_admission/owner_cases accept same-typed
fixture directory and Git repository references positionally. Introduce one
private GitFixturePaths aggregate borrowing the two named references; both
helpers and their two designated callers keep exact same paths/operations/checks.
No payload/path copies, production change or new oracle. Closed-run snapshot
guards are separately corrected under the shared explicit-borrow invariant.
Recheck the same fixture TU, then the required whole source hook.

Same final source-gate static disposition (2026-10-14): both Git closed-run guards
now check single-run size before front(), borrow the same const run/snapshot,
preserve the existing error condition where present, explicitly reject absence,
and require the same closed flag. The discovered vector owns these references
throughout the reader block; no snapshot/body copy, reader-path change or weakened
byte/outcome assertion. Root's private GitFixturePaths signature correction is
retained. The same Git fixture TU clang-tidy recheck is GREEN
(context/preserved-run-guard-finish/git-static.log), with dry formatting/diff
checks clean. No new review or native-suite repeat; the preceding full Debug gate
passed131/131 tests and Root owns its required installed retry.
