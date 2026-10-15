# Independent native fuzz target challenge — 2026-10-15

Reviewer: `/root/tui_perf_deltas`, independent of root's target authorship. Scope:
`tests/tui_fuzz.cpp` (retained inactive candidate) and its CMake target/link/seed-test
registration, read against actual Composer, ChatGrid/ChatPainter and SceneHost
source. One source challenge; root owns fixes and one direct recheck. No review
of the review. No production changes or local mutations/fuzzing were performed.

## P2: the armed-target oracle accepts unintended disarming

At reviewed lines101–102, `expected_arm` is reset whenever
the implementation reports `!host.armed()`. At
reviewed lines178–179, the take decision is checked
against `old_armed`, another value read from that same implementation. These
checks can detect a returned target changing identity, but cannot detect an arm
being incorrectly discarded by a harmless publication, draft edit, or refused
operation. For example, arm the initially selected row, publish the same rows in
a different order while retaining that key/target, then consume. A broken publish
that always clears `armed_` passes: the next iteration discards the independent
expected target and accepts take failure. This matters directly to the stated
armed-target/reorder invariant; it is a target oracle defect, not an observed
product defect.

Keep `expected_arm` independent of `host.armed()`. Update it only for specified
input transitions: successful arming, selection changing to another key,
publication removing the armed key/target, and consumption. In this fixed single
collection/fixed target-identity grammar, row presence and selected-key changes
are enough to derive disarming. Assert `host.armed() == expected_arm.has_value()`
after every operation; use the expected target's presence for take decisions.
A fixed arm→reorder-preserving-target→take sequence should reach successful
consumption, alongside existing removal/refusal sequences. No mutation campaign
or additional test framework is needed for that direct recheck.

## Other reviewed boundaries

The target calls Composer parsing/state methods, text/grid composition, painter
prepare/commit, and the in-memory SceneHost only. Returned actions are discarded;
there is no TerminalUI dispatch, shell/editor invocation, workflow evaluation,
provider call or network request. Locale setup/stdin/stderr and the fuzzer's
own crash/core handling remain ordinary process facilities. Normal AFL forkserver
execution gives each input a fresh process; no persistent-loop claim appears.
The target consumes stdin, not an `@@` filename argument.

The explicit 8192-byte input bound, width1–120, height1–16 and scene limits8/8/4096
bound the grammar. Byte255 is a timeout opcode, rather than a raw byte fed to
Composer. Composer's own1MiB text capacity can still be reached through
editing/yank operations; an8192-byte input is not an8192-byte output or constant
runtime guarantee. This is a useful stress path, not a reason to hide timeouts.
Timeouts and resource failures need retained input/replay classification in the
runner rather than being declared confirmed product bugs merely from a counter.

Cursor/state roundtrip, expiry/idempotence, grid continuation/tail bounds,
unchanged painter cells, candidate-copy independence, refusal state preservation
and draft CAS checks are grounded in the exercised APIs. The scene selection
publication check is deliberately membership-only; it does not qualify the full
nearest-surviving-neighbor policy. This limited grammar likewise does not cover
node identity/kind replacement, concurrent owners or the actual terminal run loop.

The CMake executable links the same owned native library and registers only
`--seeds` with a15-second fixture guard. No external corpus, mutable baseline,
dependency or fuzz campaign is launched from ordinary CTest. Inputs over8192 and
unknown modes return2 before target execution; the runner should cap AFL input
size accordingly. Assertion aborts and uncaught native/sanitizer failures remain
visible to AFL; the target does not rewrite or suppress input. The runner, not
this in-memory target, must retain the original crash/hang files and build/tool
identity for later replay.

Direct fixed-source replay: `build/debug/tui_fuzz --seeds` exits0 on the available
Mac Debug binary. No compiler, sanitizer qualification or Linux AFL result is
claimed by this reviewer. This report contains one P2 target finding and no
newly demonstrated production bug.
