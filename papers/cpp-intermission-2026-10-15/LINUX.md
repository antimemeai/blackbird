# Ordinary Linux Release repair counterpart

The existing `scripts/check-linux` runner built the ten owning targets and ran
all34 selected native cases successfully, with no compile diagnostics or test
failures. CTest execution took9.12seconds. This is the ordinary Linux Release
counterpart; it does not claim ASan/TSan, physical terminal engines or performance.

Observed host profile: Linux6.8.0-146-generic x86_64, glibc2.39, Ubuntu
Clang18.1.3, libstdc++13, CMake3.28.3, Ninja1.11.1. The runner used its declared
`linux-clang18` profile, Release, BLACKBIRD_DEBUG=OFF, no instrumentation, and
owned Lua5.4.8 built from the existing checksum-qualified source archive. No
terminal driver was installed or invoked because the anchored regex selects
native cases only. Existing runner resource bounds were retained.

Requested targets: participants_custody_test, participants_raw_test,
child_custody_test, command_mailbox_test, domain_admission_test, file_effect_test,
display_text_test, chat_view_test, terminal_test and program_workflow_raw_test.

Exact selection:

```text
^(participants_custody_.*|participants_raw|child_custody_.*|command_mailbox_.*|domain_admission_.*|file_effect_.*|display_text|chat_view|terminal|program_workflow_raw)$
```

The full invocation is retained as `command.txt`; driver output is `driver.log`;
the complete source-build/profile/CTest output is `checks.log`, copied unchanged
from `context/linux/run-5u24xdyk/checks.log`. It includes the latest persistent
file-effect fence cases. The runner exited0 after removing its owned ephemeral
`/tmp/arconaut-linux-jZP05sk9` workspace; no cleanup warning or cleanup-error file
was emitted. No unrelated campaign, service, provider, original session, user
artwork or AFL output was inspected or touched by this check.

This initial check preceded subsequent integration/lint corrections and the
eighth child-custody discovery; it does not qualify the final timed binary.
The final source and binary are identified by the performance build manifest.

Final source counterpart after integration corrections:38/38 pass, including
workflows, provider_raw, operation_raw and terminal_blocked, with the same Linux
Release profile. Final logs/provenance reside in ignored performance/; publishable
build/source metadata accompany the performance result. First incremental overlay
preserved archive timestamps and reused stale terminal/coding objects; new tests
exposed that setup defect. Preserve that failed build/selection separately. Set
current timestamps on exactly the seven changed inputs, rebuild actual native
objects and re-run the same38 cases; all pass. No failed/stale binary was timed.

Final delivered native tree48027f980f11cfd5ce6913db9d56fe9f4f94b0d7bea23e64d14605e2e0f097d0
passes43/43 in18.91s after the mailbox/fixture lint fixes and selector-custody
repair. Existing command_jobs exact final-reap and command_raw pass alongside
new filtered, filtered-raw and genuine-loss cases. Exact final source/build
metadata and direct CTest output are published with [performance](performance/README.md).
This result supersedes the earlier limited checks for the timed binary; earlier
actual failures and stale-build diagnostics remain preserved.
