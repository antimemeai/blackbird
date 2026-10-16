# TUI collective bug queue

Operator2026-10-15: save ordinary findings for collective action; interrupt for
egregious faults. Performance measurement and input generation do not authorize
chasing every newly observed defect. Preserve originals and continue the tranche.

Beads is the issue queue. Use label `tui-campaign-bug` for TUI product/tooling
findings and the retained AFL inbox. A bug entry includes exact source/run/tool
identity, actual result versus expectation, original reproducer/output location,
scope and severity. Keep unconfirmed timeouts, environment failures and actual
product defects distinct. Several inputs may represent one bug; a crash count
is not a count of distinct product defects.

```sh
bd list --label tui-campaign-bug --status open --json
```

Existing observations remain queued: `arconaut-xbf` (intermittent quit during
native-call settlement) and `arconaut-ew5` (incomplete Mac Release/ASan linking).
Neither is repaired by a passing fixture or a later benchmark. New performance
failures keep failed trials in the dataset and refuse a misleading numeric delta.
`arconaut-f8f` retains the paired round's startup/paste tail outliers; source
causation is unknown, and all120 observed processes complete normally.
`arconaut-3xf` retains the later slash `/cancel` exec-settlement observation, and
`arconaut-3wt` the raw-recorder explicit-recovery timeout. They prevented source
publication of the helper/target candidate, without demonstrating source causation
or data loss. `arconaut-cfj` retains repeated Mac Ninja cache recovery and unchanged
source recompilation during the frame-presenter tranche. Its final source hook
passes145/147: `arconaut-rln` records the coding-test timeout with no stage trace;
`arconaut-xbf` records another existing quit-during-exec failure. The presenter
source was retained [inactive](../papers/tui-next-host-2026-10-15/SOURCE_STATUS.md),
including the concrete wait-admission/tick cleanup repair. `arconaut-0uq`
holds the launched AFL run's untriaged inbox; it is left alone for collective work.

The AFL run manifest identifies its native queue, crashes, hangs and logs in an
owned persistent Neuroses directory. Those files are the untriaged input inbox,
with a Beads follow-up for collective inspection. The latest operator direction
is to launch through Python and leave it alone: no polling/triage/minimization,
automatic source repair or findings inspection during the running campaign.

An egregious fault would justify interruption, for example demonstrated destructive
effects, lost originals, or a broad repeatable crash/hang on routine input. A
fuzzer's crash/timeout counter alone does not establish that. Routine findings
remain queued for the subsequent collective action rather than expanding the
current unit's hardening scope.

The operator then reopens both final test failures. Test grouping and current-call
synchronization are repaired, with actual busy-shell cleanup and all existing
step deadlines retained. Linux Release/ASan13/13 each and Mac9/9 selected PTY
episodes pass. Source is restored in the working tree, with publication blocked
by the Mac Debug linker (`arconaut-ew5`). Current [repair results](../papers/tui-settlement-repair-2026-10-15/RESULTS.md)
keep the historical native-stall cause distinct from repaired fixture defects.

Current source676120c passes157/157 plus full C++/Lua analysis. The exact
Ninja invalid-record/recovery loop (`arconaut-cfj`) and coding aggregate-test
shape (`arconaut-rln`) are repaired and closed; native presenter slice rhi.3.3 is
closed. Source is committed. Historical native quit/linker causes remain open:
passing current fixtures does not identify those earlier waits. [Build and delivery](../papers/mac-link-repair-2026-10-15/RESULTS.md)
records the direct cache proof, same-tool repair and actual source gate.

The Ultramarine screenshot issues are repaired in source65e5877: arconaut-zh2
(bounds incorrectly becoming a command lifetime limit and misleading refusal
semantics), arconaut-6ao (plain-text joiners undercounting cells), and arconaut-gbu
(the existing recorder fixture's post-parent-death cleanup race). Final Debug
160/160 and full C++/Lua analysis pass. [Repair results](../papers/ultramarine-repair-2026-10-15/RESULTS.md)
retain the actual causes, red/green and sanitizer/terminal checks. That tranche's
120/120 matched runs remain mixed; wide startup p95/max worsen and stay in the
existing arconaut-f8f observation. No broader program closure or AFL inspection.
