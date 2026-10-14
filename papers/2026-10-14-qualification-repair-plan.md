# Qualification repair plan — 2026-10-14

Operator requests fixes and only impacted test reruns. No optimization, weakened native semantic
assertions, larger deadlines, source-wide gate or new dependency. Existing
qualification report 0423991 is pushed before repair. Direct failures are the red
cases; preserve their native bytes/effects/error/lifetime oracles.

Root owns production File field lifetime and Candidate Linux header ownership.
File Fields borrows each native field until emission_.close copies it; `failed`
currently leaves scope earlier. Move that value into the returned terminal scope
that encloses close, preserving every conditional field and errno/error branch.
Inspect other local fields in Helper's returned/error/unfinished paths for the
same lifetime rule. Add Candidate's direct <cstring> include for std::strlen.
No recorder/producer protocol or capture payload changes.

Independent owners repair Beads/Station allocation fault selection, UI fixture
credit/allocation lifetimes, and command PTY harness ordering/readiness. Each
writes its short source-grounded plan before editing and preserves independent
original disk/effect/stage assertions. Linux UI allocation source is symbolized
before changing allocator families. No production noinline or sanitizer suppressions.

Root coordinates one Ninja writer per tree and at most 6 compiler/static workers.
Run impacted native tests on macOS Release and ASan/UBSan, Linux Release/ASan,
and changed C++ formatting/TU diagnostics; Python syntax for changed drivers.
File callers: raw_file/stdio, session_raw, backstop_raw, candidate_raw,
main_input_native, beads_raw, station_main_raw/native, command_jobs_station.
Fixture owners: ui_raw, beads_raw, station_raw and command_jobs_pty. Include
main_input_raw where actual CLI bootstrap File paths change. Do not repeat
already-settled unrelated transport/reader/operation/storage or performance runs.
Existing source-wide precommit gate is overridden per the operator's explicit
just-impacted-tests instruction; report exactly which affected checks ran.

A single independent source review of these concrete lifetime/selector/ordering
changes feeds their owner, then affected recheck/fixes. No assurance panels or
new campaign. Close actual resolved bugs, retain any failure plainly, checkpoint
and push branch with supported bead backup. No main merge was requested for
this new repair segment.


## Linux Candidate fixture exposed after compilation

The repaired include reaches a previously unrun long-current-directory case.
Its ordinary libstdc++ 13 filesystem::canonical(".") reference returns
ENAMETOOLONG, matching the qualified File plan's realpath/fallback behavior;
the fixture incorrectly assumes success. Correct the oracle to the actual
native failure. Independently verify the full, uncapped current-directory
CString against the literal descriptor-created path, then require matching
native error, File terminal and Candidate standard-exception terminal. Do not
change canonical business semantics or fabricate a returned path. Run only
candidate_raw on Linux Release/ASan; the Linux-only branch is not executed on
macOS. Diagnose/compile the changed owning TU and preserve cleanup.

Pressure test analysis also rejects an invented fixed-input throughput contract.
The corrected case establishes actual queue refusal plus backpressure from a
512 KiB source, reports transferred/unwritten extents, then checks reserved
lifecycle control and exact disk originals for every transferred keyboard byte.
Existing native effect/ownership assertions and the 30-second progress watchdog
remain; no writer optimization or suppressed sanitizer is involved.


The first revised Candidate case rejected the File terminal because its fixture
used the caller's returned tag 1 for File, whose separate enum returned tag is0.
Use the actual named RawFileBranch/RawOperationBranch constants and rerun only
Candidate. The native record/error checks remain exact; no product behavior or
oracle condition is relaxed.
