# Command center: matched Linux Release measurements

Source `d0e3b2efa68d1dc3b4310ee06f483a11d6d44786` follows baseline `f2ab70b`
(native52f9103). The [delivery mapping](DELIVERY.json) verifies all432 committed
native inputs and one driver adjunct match the built/measured tree
`a019f4e4d6e1206b06c068903c3313c9d526bbe24c2eb9e36ee17b639ed3a0cb`.
Candidate executable SHA256 is
`4af92b452b5bd6a19345d2a402f6d7227d84f546126ab6914873748629db0b8e`.

The baseline uses its prior qualified executable at its original retained path,
including its embedded archive/job-runner paths; neither that tree nor its builds
were changed. Its native tree is `a23d707` and binary SHA256
`0e8e1b513bfc95ab1da7ee3b45e3735011bcc1f204bd75d11b7c3209034a8ac6`.
Full hashes and companion identities remain in [profile](profile.json),
[baseline mapping](baseline-source.json) and qualified baseline provenance.
Both use Clang18.1.3/libstdc++13/Lua5.4.8 Release, DebugOFF, instrumentation none.

One serial round-robin round ran08:34:16–08:34:29UTC on2026-10-16 after all owned
builds/checks stopped. Every one of120 processes and960 endpoints completed
normally:30 trials per source at110×35 and80×24. No warmup/tail exclusion or repeat
to improve results. [Metadata](metadata.json), [all samples](samples.jsonl),
[exact spec](spec.json), unchanged observer/report and [full deltas](deltas.md)
retain the actual conditions and quantiles.

Results are mixed (p95 lower11/endpoints higher5):

* Wide startup p95:111.492→29.310ms; narrow startup26.978→82.450ms.
* Wide paste p95:54.852→47.926ms; narrow paste64.275→50.669ms.
* Wide palette-next maximum:1.338→13.575ms; narrow paste maximum88.470→104.886ms.

These are parent PTY-read/scheduling/parsing endpoints, not physical display
latency. At30 trials p99 is the maximum. The host was shared and not isolated;
this round does not identify a source cause. Common scenarios never open the
center, so these deltas do not measure its selection/refresh latency or establish
blanket nonregression. Existing arconaut-f8f retains the tails for collective work.

The exact candidate passed [75 selected Linux Release checks](scoped-release-checks.log)
in51.48s. [Build provenance](after-build-provenance.json) records exact commands,
profile, inputs and executable. The pinned Microsoft development executable was
explicitly installed; its [install log](driver-install.log) and hashed driver
profile adjunct accompany the source. Local preparation initially read Lua hashes
from an older top-level schema, then reused the same owned prepared directory
with the actual nested profile; [setup failure](prepare-setup-failure.log) records
that pre-build error, not a failed measurement or product bug.

All120 terminal originals remain in ignored
context/tui-command-center-2026-10-16/performance/perf-round/terminal-output and
the persistent remote campaign `tui-command-center-20261016-_ycd6jjo`.
Native raw sessions also remain in that remote campaign. This folder's
Python observer/report copies are unchanged measured study artifacts, not active
production/tool additions. No AFL outputs were inspected or altered.
