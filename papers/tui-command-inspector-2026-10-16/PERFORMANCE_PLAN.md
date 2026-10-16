# Matched Linux Release campaign

This unit measures the command-inspector tranche against `45b1454` with the
existing actual-PTY observer. The baseline native inputs equal the qualified
prior tree `48027f980f11cfd5ce6913db9d56fe9f4f94b0d7bea23e64d14605e2e0f097d0`;
its binary is `991d5d19363f3796e44648e41beba644f090c66bc3620e6eb36b5e49fdc28d54`.
The campaign records that source mapping explicitly rather than implying a
rebuild produced identical bytes.

The previous qualified tree, build and executable remain untouched. The before
spec points at its existing immutable executable so embedded absolute archive/
job-runner paths and source defaults retain their qualified companions. CMake
names those helper targets `blackbird-archive` and `blackbird-job-runner`; the
raw recorder is in the native runtime, not a separate CMake recorder target.
Their exact paths and hashes accompany this campaign. A new persistent campaign
owns its candidate source, build, logs, sessions and terminal recordings.

The candidate uses the same Linux Clang18.1.3/libstdc++13, Lua5.4.8, Release,
BLACKBIRD_DEBUG=OFF and instrumentationnone profile. Retain the exact Lua archive
and qualified static-library hashes. Preserve every build/check failure. After
the root signals stable source, snapshot the native input scopes used by the
previous campaign, verify the baseline commit mapping, build the final candidate
and run the scoped owner/component/adapter/display/native integration checks.
Existing test budgets remain unchanged. The scoped Microsoft actual-main cases
use the already adopted pinned driver and its existing explicit installer in the
new candidate source. Keep its profile as a separately hashed check-input adjunct
outside the unchanged native-tree scope; no new driver is adopted. Another full
suite campaign is outside this Linux counterpart.

Timing starts only after the final candidate is qualified and root confirms no
owned builds/checks are running. Use the unchanged tracked prior observer and
analyzer, 30 trials per source and geometry (110x35 and80x24), serial round robin,
ordinary OS caches and no warmup exclusion:120 total processes and960 expected
endpoints. Retain every raw sample and PTY recording. Reject incomplete or changed
executable rounds through the existing observer/analyzer. Do not rerun to improve
numbers. Record shared-host load; make no exclusive-host or physical-display
latency claim. The existing startup-tail issue remains relevant.

Preparation may create the new campaign and helper scripts but must not snapshot,
build, test or time the changing final source. The root signals each final-source
and timing boundary. No AFL, artwork inspection, live provider/session or new
review layer is involved.
