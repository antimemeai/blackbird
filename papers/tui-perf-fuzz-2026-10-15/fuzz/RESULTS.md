# Native target and Python launcher qualification

2026-10-15. The remote instrumented target build and fixed seeds pass. Python launch
confirms both AFL jobs, then leaves them alone. The full Mac source gate passes145/147 with two failures; the helper/harness source remains an inactive local candidate. No fuzz
counter, inspected finding or qualified source adoption is claimed.

AFL++5.03c source is pinned at dbaf11913c1b2702dee5b4d3dcfffd52f1defe50.
Original3,463,566-byte archive SHA256 is
`d5f4f72c830526654bebbc637f6164a93c22d7309ce8ba73979abadc42ce5974`.
The LLVM18 development tool build/install completes in an owned Neuroses prefix.
`afl-fuzz -h` reports5.03c and exits1, so the initial installer wrapper reports
failure despite the successful upstream build/install; the actual banner and
executable presence establish the distinction. No global compiler/tool policy,
cloud worker or production dependency is changed.

The native harness directly exercises in-memory Composer, rendering/painter and
SceneHost APIs. Fixed seed replay passes on Mac Debug before and after the
independent correction. Focused clang-tidy initially rejects adjacent same-type
helper parameters; remove that unnecessary wrapper and rerun cleanly. No warning
is suppressed. The normal CTest case runs deterministic seeds, not input mutation.

The [independent challenge](REVIEW.md) finds one P2 oracle gap: deriving expected
arming from the implementation hides unintended disarming. The final harness
keeps expected arming independent of the host flag, changes it only for specified
arm/selection/removal/consume operations, and compares the flag after each step.
Fixed cases retain an arm through reorder, draft edits and a refused publication,
consume exactly once, and disarm on removal. This corrects a harness oracle; it
does not demonstrate or repair a production defect.

The Python launcher persists unique local/remote identity before starting two
user services. Target/source/tool hashes, exact argv, resource bounds and output
locations accompany launch. It checks only initial native fuzzer-state creation
and then exits. The final operator instruction is to leave AFL alone: no case
inspection, ongoing polling, minimization or automatic repair in this unit.
Native queue/crash/hang files remain the collective inbox. The [launch record](LAUNCH.json) binds run `a7057dd991c5`, tool/target hashes,
exact argv and retained output locations. The [native source record](NATIVE_SOURCE.json)
binds its base commit plus exact overlay digest. Each mode has an initial10million
execution budget, low priority, one CPU and1GiB. No cases or counters are inspected.
`arconaut-0uq` is the collective inbox.

The required full source gate passes145 cases, including `tui_fuzz_seeds`, and
fails existing `terminal_pty` at Ctrl-C exec settlement plus a60.69second
`raw_recorder_cuts` timeout at explicit recovery. Total317.48seconds. The hook
stops before full owned C++/Lua analysis and creates no source commit. Focused
harness clang-tidy and Mac fixed seeds pass; no suppressed diagnostic or enlarged
deadline. `arconaut-3xf` and `arconaut-3wt` retain the two observations, with no
source-cause/data-loss claim or unchanged rerun.

Per the operator, keep ordinary findings queued and move on. Restore the original
CMake wiring and retain the experimental target/helpers under ignored context.
[Source status](../SOURCE_STATUS.md) and [candidate identity](CANDIDATE_SOURCES.json)
record where their exact bytes are held. Only documentation/data is published;
no source-gate bypass, main integration or running executable replacement.
