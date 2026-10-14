# Qualification repairs and affected recheck — 2026-10-14

The concrete qualification defects are repaired. All 14 affected cases have final
passing results on macOS Release, macOS ASan/UBSan, Linux Release and Linux
ASan/UBSan: 56 affected case/profile outcomes. Only relevant failed or changed
paths were rerun. Existing assertions about native originals, effects, error
classes, actual PID/one launch, cleanup and ownership remain. The flawed command
throughput predicate is explicitly replaced by its designed saturation/control
contract and a stronger exact transferred-byte disk oracle.

Production changes are two small corrections: keep File's borrowed `failed`
field alive through emission_.close, and directly include <cstring> for Candidate
std::strlen. Neither adds a transformation, allocation, timer, framework, buffer,
recorder policy or optimization. The File correction removes repeated sanitizer
failures in its callers. Candidate now builds on the qualified Linux profile.

Fixture corrections:

- Beads selects the actual large argv-body acquisition without a backtrace name;
  exact five prefix literals, stage/argc, no extra child and no effect are checked.
- Station selects a committed ApplicationRecordEvent/program, ignoring the earlier
  issuer reservation. Prompt selection counts the actual formatter's allocations
  on the current standard library. Existing semantic-record/stage/state/original
  assertions locate both native failure points independently.
- UI retains the issuing RawThreadHandle until its TLS loan and credit destruct.
  Linux red trace is retained and symbolized: std::get_temporary_buffer in palette
  stable_sort uses nothrow new, which the fixture failed to pair with its custom
  free. Matching scalar/array nothrow allocator/deallocator overloads resolve it.
  No sanitizer suppression or product allocator change.
- Candidate's previously unrun GNU long-CWD fixture assumed canonical success;
  actual libstdc++ canonical returns ENAMETOOLONG. It now requires the ordinary
  native error and matching selected error, exact uncapped cwd CString including
  NUL, File terminal and linked caller standard exception, with no returned path.
  Separate named File/operation enum constants preserve their different wire tags.
- PTY waits for the actual rendered composer after child reap. Pressure establishes
  explicit refusal plus outer backpressure from a512 KiB source, then checks
  background/stop/reap. It records transferred/unwritten counts and does not claim
  all 512 KiB consumed within 30 s. The original 30 s progress watchdog stays. The added
  whole-record reader proved too slow for the fixture; the existing direct closed-
  journal pattern replaces that one byte oracle. Every physically transferred
  keyboard byte must equal disk originals, with exact request/return links.

Final Mac pressure observations: Release 316,670bytes at 7.589 s and ASan 316,542bytes
at 27.032 s establish saturation. Requested 524,288bytes, unwritten 207,618/207,746.
All 319,234/319,106 transferred keyboard bytes (including other commands/controls)
are compared to disk. Linux finalPTY cases pass 7.15 s Release/26.85 s ASan. These
are test observations, not new throughput guarantees or capacity selections.

Native profiles use unchanged LLVM 23.1.2/macOS ARM64 and Clang 18/libstdc++ 13/Linux
x86_64 configurations. ASan+UBSan remains enabled in the sanitizer modes. Changed
C++ formatting and all six changed translation units' clang-tidy checks pass;
changed Python syntax and relevant diff/local-link checks pass. Independent
bounded source reviews find no remaining lifecycle/oracle issue. Failed initial
repair attempts remain in logs: Station initially selected an issuer reservation;
Candidate initially assumed long-path success then confused File tag 0 with caller
tag 1; the command fixture initially imposed throughput then used an inadequate
whole-audit consumer. The corrected native outcomes above replace those provisional
results. No unrelated transport/reader tests or performance measurements rerun.

Seven scoped defect beads 834/90u/bho/iva/fir/0oa/ji3 close after their affected
checks. Broader nls.2 stays in progress; owner-cost/capacity/global SystemA work
and deferred eval/trajectory/multiplayer boundaries remain. The operator explicitly
requests JUST impacted tests, superseding the blanket full source gate for this
repair checkpoint. No 131-test/all 194-TU gate or whole Lua pass is claimed/repeated.
Checkpoint/push the work branch; no new main merge was requested.

[Every affected case/profile result](qualification-repair-2026-10-14/affected-case-results.csv),
[repair plan](2026-10-14-qualification-repair-plan.md),
[native fault selectors](2026-10-14-native-fault-selection-repair.md),
[UI fixture lifetime/allocation](2026-10-14-ui-fixture-repair.md) and
[PTY ordering/saturation/originals](2026-10-14-command-pty-repair.md) preserve detail.
Full builds, original/recheck diagnostics and native captures remain in ignored
context/qualification-repair-2026-10-14. Native workers finish and the owned
retained Linux scratch is removed after final results are captured.
