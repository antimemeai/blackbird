# Repair ordinary command use and pane rendering

2026-10-15. Operator asks to sort the reported session defects under BLACKBIRD.md.
Baseline9cea416 (native delivery676120c). Exact historical session remains read-only.

## Theory and concrete purpose

A coding instrument must support sustained command use while bounding resident
observations. Historical originals belong to retained audit; completed workers
need not occupy every live control slot forever. Releasing a resident observation
must preserve original output and launch identity, and must never discard a live
process, an unsettled result, undelivered bytes or an accepted control delivery.
An attempt admitted to the audit is not evidence that its external command began.
Human and model observations must distinguish refused launch, actual started work,
and unknown outcome after execution or capture loss. Presentation must compose
clipped independent panes and faithfully reproduce the staged screen across
partial writes, resizes and normal narrow geometry.

These follow INTERACTION_THEORY's representations/feedback/continuity rules and
TUI_DOCTRINE's truthful state and bounded work. The goal is sustainable actual use
and actionable failure, not a larger arbitrary registry or a prettier error alone.

## Owners and sequence

1. Command owner studies acquired process-lifecycle references and current registry,
   writes COMMANDS.md policy/plan; root challenges before implementation.
2. Independent failure researcher studies actual provider-visible observations,
   admission and capture boundaries. Root writes/implements the reviewed outcome
   contract in coding.cpp and direct engine/provider tests.
3. Pane owner reconstructs historical raw geometry/output, reproduces an actual
   defect on current source and writes PANES.md before repair. Distinguish any old
   executable defect from one still present. No speculative renderer rewrite.
4. Root integrates and independently challenges code/oracles once (layer2), fixes
   direct findings, performs final source gate and publishes checkpoint/push.

## Direct oracles and resources

Command checks run >32 sequential foreground executions with observed exits,
retained first-output lookup, bounded resident records and no duplicate launch.
Capacity checks keep live/unsettled/control/output owners intact and prove a refused
command did not reach worker/spawn. Deterministic provider tests require exact
error cause/launch status in function-call output and failure in retained attempt,
without sticky capacity.stop for an ordinary registry refusal. Real audit exhaustion,
post-effect uncertainty and interruption retain existing stop and cleanup contracts.

Rendering checks use captured fault-triggering geometry/content, owned VT replay
and readable screen expectations, plus installed Microsoft tui-test integration
and existing PTY/styled regressions. Exact comparisons catch the reported stray
text, rather than checking only that a task title occurs somewhere.

Use Mac Debug targeted builds and direct tests while implementing; Linux Release
and ASan/UBSan changed-path checks for process/raw/UI behavior; required installed
full Debug source hook and owned C++/Lua analysis for the delivered source. Preserve
existing fixture deadlines. No laptop mutation, no new dependency, no live provider
calls or original-session recovery, no user proposal build replay. AFL stays untouched.

Capture matched Linux Release before/after sources, binaries, raw PTY performance
samples and derived deltas after builds/checks. Reuse the existing observer and
initial30 trials per variant/geometry. Add a sustained-command scenario only if
it directly measures this changed behavior; retain every attempt/failure. No new
speed claim from cross-host or unmatched samples. Ordinary extra findings stay
queued. Work has two remediation/recheck layers; no review-of-review or extra
qualification cycle. Preserve exact original records, source snapshots and logs.

Layer2 concrete finding: a zero-width scalar need not have Grapheme_Cluster_Break
Extend. The renderer must not carry pictographic eligibility across ZWSP/LRM or
Extend after the ZWJ. Both independent terminal engines confirm the non-joining
cell positions. Pin GCB Extend alongside Extended_Pictographic, add literal
barrier/repeated-leading-joiner cases and fix the same streaming-oracle assumption.
This stays within the discovered width contract, not a whole grapheme rewrite.

Profile integration findings: Clang18 rejects std::array CTAD for384 property
ranges because its implementation instantiates a fold beyond the existing
expression-depth bound. Declare the known array dimensions explicitly; preserve
compiler flags. The new40-child retention workload exhausts the existing native
TSan aggregate case deadlines. Register it as separate native/engine60second
cases (forty cold instrumented children); retain original30/45second suites and
per-operation deadlines. The new provider fixture also needs the source working
directory for its real turn program; declare that CTest property explicitly.
These are corrections to the changed unit, not a timeout increase for old cases.

The final normal gate passes159/160 and catches existing raw_runtime line300:
cleanup requires killing a recorder after its supervisor has died and its control
socket closed. The recorder may already exit. Correct test ownership/order:
observe the supervisor's SIGSTOP through waitpid WUNTRACED (not merely successful
signal submission), preserve the actual status-send kernel barrier, kill the
recorder while its stopped parent pins the unreaped child, then kill supervisor.
The pending RPC must still return audit_unavailable. No production change,
deadline relaxation or unchanged gate retry. Independent RAW_FIXTURE.md challenge
confirms separate sockets, SIGCHLD disposition and final reap ownership. Check
this actual fault case on Mac and Linux Release, then finish the installed gate.
