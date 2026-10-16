# Recorder stop-cut fixture repair

The installed206-test gate timed out at raw_recorder_cuts after its explicit
recovery progress marker. The existing issue is arconaut-3wt, not a new duplicate.
One actual unchanged stop-only run passed, then an authorized finite64-episode
scratch diagnostic reproduced a block at trial24 after24 successful trials.
No load or product-runtime diagnosis follows from the original coarse marker.

Finite samples prove this cycle: test parent96176 waits in thread-context
retirement response, its supervisor96180 waits for the recorder's retirement
reply, and recorder96181 waits in the fixture's sigsuspend for a stop signal that
the parent cannot yet issue. The original interposed send selector accepts any
successful one-byte value1 from the selected recorder. RawChannel acknowledgment
uses that same byte on a SOCK_STREAM notice endpoint. Actual recorder lifecycle
acknowledgment uses its separate SOCK_DGRAM control endpoint. A space notice can
therefore arm the artificial before-poll stop cut before retirement is complete.
This is a fixture synchronization bug; no runtime fix or timing waiver is planned.

Root approved the fixture-only plan before tracked editing: select the actual
native socket type and arm only its successful DGRAM lifecycle reply; keep STREAM
notices flowing. Strengthen the fixture by arming the real first producer lane's
space notice before its first actual literal, under the selected recorder PID.
Require an actual STREAM notice, exactly one DGRAM cut arm, original exact native
wake and complete literal recovery/graceful drain. Preserve a deterministic old-
selector scratch behavioral RED using the same60s budget, then run current GREEN.
Do not change any existing assertions, watchdogs, CMake or runtime source. Root
owns one independent review and normal integrated gate after GREEN.

The original gate log, exact diagnostic source/build/archive identities, all
trial logs and finite three-process samples are retained in ignored
context/tui-command-inspector-2026-10-16/recorder-recovery. The blocked diagnostic
was stopped from further trials at its first timeout. Its sampled exact recorder
was sent the intended USR1 signal only to release its test-only wait and permit
owned normal cleanup; this manual release is not a passing diagnostic result.

The strengthened permanent oracle obtains the real first lane and arms its
actual space notification before emitting the first literal. It waits for that
selected recorder's successful STREAM send and requires zero stop-cut arms before
issuing synchronous retirement; the later successful DGRAM acknowledgment must
arm the cut exactly once. A scratch copy retaining the old all-one-byte selector
compiled successfully and failed this actual wrong-source-arm assertion with
SIGABRT(-6), without a missing symbol or altered watchdog. Source, command,
archive identity and failure exit are in old-selector-red-metadata.json/.log.

Current fixture-only Debug target built successfully. The complete existing
raw_recorder_cuts CTest passed (0.58s), including all native cuts, fairness and
both graceful/explicit recovery episodes. Owning clang-tidy passed. Root's one
independent source/oracle review found no issue: native socket classification is
before forwarding, restores errno, accepts only successful DGRAM arms, and the
negative real STREAM control precedes true producer retirement. Existing exact
wake, literal, closure and new-recorder assertions remain intact. The direct
channel mapping registers no additional producer and cannot replace retirement.
No runtime source, CMake, timeout, original assertion or suppression was changed.
Root owns final gate and issue closure; no additional review panel is needed.

Affected Linux Release counterpart also passed: this exact strengthened fixture
compiled with all production warnings against the unchanged qualified foundation,
then its full native fixture exited0 in0.215s. The separate scratch directory,
exact source/archive/executable hashes, command and output are preserved in
recorder-recovery/linux-{metadata.json,result.json,checks.log,build.log}. This is
the fixture-native portability check, not a substitute for root's integrated
current-source Linux qualification. No qualified old tree/build was modified.

Root additionally reports the performance lane's actual final-candidate Linux
fixture overlay passed raw_recorder_cuts1/1 in0.17s against its final Release
foundation, without changing the product executable. That integrated candidate
check is distinct from the earlier owner scratch counterpart. No further owner
build/check is needed; source and paper are stable for root's installed hook.
