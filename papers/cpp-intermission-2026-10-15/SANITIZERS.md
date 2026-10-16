# Scoped sanitizer checks

Qualified macOS Clang23, Debug C++20. Existing `build/asan` instruments address
and undefined behavior with no recovery; existing `build/tsan` instruments data
races. Builds use3 workers and tests run sequentially. No configuration, deadline,
source, index, suppression, fuzzing or artwork change belongs to this check lane.

ASan/UBSan:35/35 pass in55.19s. Scope: participant custody3 + existing raw1,
child custody7, mailbox5, domain admission6, file effect8, display native + both
independent terminal backends3, chat_view1 and terminal1. This includes final
persistent-applied file fencing and all8 actual file-effect scenarios.

Initial ASan build had compiled coding.cpp before root's final persistent-file
fix (object23:35:14, source23:35:46). Before any sanitizer test, an incremental
requested-target rebuild captured that actual source/header change. Both build
logs are retained; no obsolete test pass is claimed.

TSan:8/9 pass; total98.50s. Participant custody exact/throwing/unknown and mailbox
identity/history/reserved_ids/reserved_bytes/transient pass. Existing aggregate
participants_raw exceeds its unchanged60s deadline (60.42s), last output reports
completed exact9MiB overlapping-ticket case. No sanitizer race report appeared.
The next unknown-used9MiB phase or intervening cleanup did not print completion.
This log alone does not distinguish instrumentation/load cost from a stalled
phase. ASan's same full raw fixture passed17.89s. Initial failure remains explicit; the final-source isolated counterpart below
passes under the same deadline. No unchanged full sanitizer suite repeated.

Exact logs remain under ignored context/cpp-intermission-2026-10-15/sanitizers/: asan-build.log,
asan-file-fix-rebuild.log, asan-checks.log, tsan-build.log, tsan-checks.log.
ASan selection:
`^(participants_custody_.*|participants_raw|child_custody_.*|command_mailbox_.*|domain_admission_.*|file_effect_.*|display_text(_(ghostty|alacritty))?|chat_view|terminal)$`
TSan selection:
`^(participants_custody_.*|participants_raw|command_mailbox_.*)$`

## Final integration changes and affected rechecks

The first normal Debug gate exposed integration contracts requiring source or
fixture fixes: terminal styled-SGR representation, native provider child cleanup,
and bounded applied-file recovery raw original role. After those concrete fixes,
ASan/UBSan rebuilt the7 affected targets and passed22/22 in87.46s: provider_raw,
child custody7, workflows, display_text/native+Ghostty+Alacritty, terminal,
file effect8 and operation_raw. The earlier35-case log stays intact; unaffected
cases were not repeated. Full operation_raw includes independent literal exact
success/fallback bytes, actual post-rename allocation and native receipt EIO.

Then participants_raw_test was rebuilt against final native source. With root's
other local builds/checks paused, the one isolated participants_raw CTest passed
57.63s (57.64 total) under its unchanged60s deadline, without a TSan diagnostic.
Initial concurrent-source-gate timeout60.42s remains recorded. Final source and
load conditions differ, so the pass does not prove one causal explanation for
the earlier timeout; the aggregate fixture has little timing headroom. New
participant custody3 and mailbox5 had already passed TSan; root selected only
the affected integration checks above rather than repeating unchanged cases.

Final logs: asan-final-integration-build.log,
asan-final-integration-checks.log, tsan-final-source-build.log,
tsan-raw-isolated.log. No build/check is left running by this lane, no sanitizer
suppression or deadline changed, and no tracked source/docs/index edits occurred
in the final sanitizer turn. Root owns the final full source gate/publication.

After the complete native lint findings, mailbox5 passes ASan/UBSan15.48s and
TSan21.26s; file-effect8 passes ASan/UBSan3.56s with the shared allocation-fault
helper. Both fixes retain the physical failure conditions and original oracles.
The subsequently discovered selector-custody unit passes ASan/UBSan5/5 in18.07s
(existing command_jobs, command_raw and new3) and TSannew3/3 in5.03s. Owning
translation units analyze clean. Exact logs remain under mailbox/, file-effect/
and child/ in the same ignored campaign context; see WAIT_SELECTOR.md.
