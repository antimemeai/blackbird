# Two test repairs, source restored, Mac source gate blocked

The operator reopened the failures and asked what inactive source meant. The
previous checkpoint had saved the implementation, then restored baseline source
after the mandatory gate failed. No deployment or feature flag was involved.
This turn verifies and restores all12 files from that archive, then repairs the
test structure and synchronization below. **The implementation and repairs remain
in the working tree, uncommitted; they have not been set aside again.**

## Coding timeout

Retained synthetic fixture timestamps show progression through independent
capacity/retry/output groups into ordinary coding before the historical60.11s
cutoff. The instrumented monolith passes16.48s and reaches ordinary coding at
13.814s; command cancellation completes and subsequent phases continue. This
supports separating cumulative independent work, rather than inventing a command
deadlock or raising a timeout. It does not identify the host's original slowdown.

`coding_capacity` and `coding_routing` now own their independent fixtures.
`coding` retains file selection, retained output and its dependent shared-journal
workflow. `coding_stream_capture` keeps the100000-fragment/two-block and actual
interruption checks while dropping duplicate capacity/routing fixture execution.
Every distinct assertion remains; all four cases have60second limits. Stage
entry diagnostics survive an external timeout. Unknown modes return2.
[Source diagnosis and plan](CODING_TIMEOUT.md).

## PTY synchronization and finite busy quit

The normal recovered-draft checks previously searched a persistent screen for
`[exit 0]`, which can belong to an earlier invocation. They now wait for distinct
split-literal markers printed after the current exec returns. Exactly-once effect,
restoration and normal exit assertions remain. A separate busy-quit scenario
waits for actual shell READY output, checks its PID is alive, sends Ctrl-Q and
requires application exit plus shell leader/group disappearance inside the same
five-second window. Historical native-stall attribution remains unknown.

The corrected whole driver passed46.3665s despite every individual deadline
passing, exceeding its old30second CTest process limit. Register its nine
independent episodes separately with30seconds each, keeping state/restart/reopen
steps together and every original3/5second step limit. Default manual all-episode
execution remains. Failures retain their fixture directory for causal diagnosis.
[Exact scope and source grounding](QUIT.md).

## Direct results

| Check | Actual result |
| --- | --- |
| Linux Clang18/libstdc++13 Release |13/13,12.28s|
| Linux ASan/UBSan |13/13,27.43s|
| Mac Debug PTY selected episodes, exact retained candidate binary |9/9; each below30s; state episode23.8772s|
| Instrumented Mac coding monolith, before decomposition |1/1,16.48s|
| Changed coding test, focused clang-tidy |Pass, no suppression|
| Required full Mac source gate |Not run: Debug link blocked in Apple ld|

Linux cases include four coding modes and all nine independently selected PTY
episodes, including actual busy-shell termination. Both profiles build the changed
source. The owned Linux workspace is cleaned up. Mac PTY uses native SHA256
`f0811c272105382feff06020e94acf1410d27ad8dc23304f6edbb3dba967939b`;
production inputs match the original candidate, while the Python driver changes.
New Mac coding modes compile but do not link, so no local run of those new modes
is claimed. Root's one source challenge finds no removed distinct assertion or
cross-episode dependency; no additional review hierarchy is introduced.

## Remaining host block and retained source

The local build finishes its object/static-library work and stops before the
coding executable link completes. A direct invocation of the exact configured
link plus diagnostic `-v` also waits inside Xcode's actual `ld`, not the compiler
or test. Known-process metadata shows negligible linker CPU use. No loader
override environment variables are set. A targeted sample probe also fails to
return. These observations establish a tool/host block, not its kernel cause.

Interrupt the owning Ninja command and terminate only the owned diagnostic
compiler/parent; send SIGKILL to its exact identified linkers and sample probe.
Some native processes remain pending in the OS despite the signal request;
do not claim their disappearance or terminate unrelated processes/services.
`arconaut-ew5` retains this Debug extension of the existing Mac linker issue.
Do not retry unchanged links, switch linkers, change tool policy, relax checks,
restart the host or bypass the source hook by implication.

The [14-file manifest](CANDIDATE_SOURCES.json) records current hashes, modes,
sizes, exact base and the complete ignored overlay archive. Original presenter
archives and both performance datasets stay unchanged. No new production inputs
were added by these test repairs, so existing measured identities/results remain
applicable without relabeling or an extra performance round. AFL jobs and output
remain untouched. Source publication resumes at the required gate when the host
linker works; no new operator approval is needed for that already authorized step.

Original logs: `context/tui-next-host-2026-10-15/reopen-*`,
`context/tui-settlement-repair-2026-10-15/terminal-*.log`, and
`context/linux/run-f1bp48bv/checks.log`. No complete current Mac source-gate or
full C++/Lua-analysis pass is claimed. This checkpoint publishes checked text,
results and identities only; current source remains available for inspection.
