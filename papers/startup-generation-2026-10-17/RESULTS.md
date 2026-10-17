# Startup repair: diagnosis and direct checks

Work in progress on `work/startup-generation-2026-10-17`, based on `f4bd931`.
The [plan](PLAN.md) includes the operator's correction: ask about incompatible
effort inside the opened harness instead of choosing an answer or aborting.

The reported `session-Thz5eK` directory is empty. Its launcher had already made
a fresh private directory; missing session selection was not the cause. The
checkout selected an October8 `blackbird-ui` whose help lacks `--provider`.
The checkout's native Release executable is October14 and also predates Paper.
The October17 installed generation `generation-z8hyFGNP`, source `9e9dad5`, has
the current provider support. No operator state is removed or automatically resumed.

The native startup driver previously set an executable override in every ordinary
launch, so it could not expose the bad default selection. Its new isolated source
checkout has deliberately stale native/UI siblings and a managed installed
generation that uses the actual native test executable. Registration and account
storage are synthetic; a substituted transport records any forbidden dispatch.

The direct driver first exposed a second actual defect: Paper rejects the generic
inherited `medium` effort. After adding an explicit valid effort only to the positive
control, the default checkout invocation fails with `STALE_CHECKOUT_blackbird-ui`.
After changing launcher selection alone, the same default invocation instead
reaches native code and fails with `Portable Responses requires effort low, high
or max`. These are separate failures, not a session recovery problem. Native
`--model` without a value also emits the same misleading blanket recovery advice.

Ignored originals: `context/startup-generation-2026-10-17/{red.log,
selection-red.log,default-effort-red.log,diagnostic-red.log}`. The owner's question
tests and final qualification results are recorded below when completed.

Generation selection now follows the managed default installation from a source
checkout; explicit executable overrides win. Packaged launchers remain pinned to
their generation. Without an owned default installation, a checkout uses its
native Release executable and never an old UI sibling. Install/update remains an
explicit build/activation operation. No new dependency, provider traffic, auth
cache import, billing change or interaction with the existing AFL campaign occurs.

The existing installer transaction driver passes with the changed launcher,
including custom prefixes, exact arguments/cwd, generation switch, rollback and
preservation of an unrelated launcher. Its original output is retained in
`context/startup-generation-2026-10-17/installer-green.log`.

Queue `arconaut-k9r`: the legacy `scripts/benchmark-startup` hashes a hardcoded
`blackbird-ui` while invoking the launcher, so its metadata can describe a
different binary. Do not use it for this repair. The retained paired native PTY
observer instead names and hashes both actual installed Release executables.

The owner's [question results](QUESTION_RESULTS.md) record actual PTY RED and
focused GREEN, pending-state replay, the added readable expectation and the two
corrected test-oracle assumptions. Its [independent launcher review](LAUNCHER_REVIEW.md)
finds no defect. Root's single integrated question review finds two continuation
details: delayed `continue` must retain resume classification in the dispatch
capture, and the plain command loop must run its deferred initial continuation
once after a valid answer. Both are returned to the conceptual owner for the
bounded fix/recheck; no additional general review is added.

Both continuation findings are fixed. The added plain native RED shows answer
confirmation with no continuation before the fix; the same oracle now requires
one continuation, retained unanswered intent and no provider dispatch. Post-review
focused C++23 Debug checks pass4/4 in10.33s. Required full Mac source gate and
scoped Linux Release/ASan checks follow on this integrated source.

The first full Mac gate passes259/263 in461.90s. Four existing integration
oracles still require the superseded throw/retain-old-effort policy:
`anthropic_native`, `registered_responses_native`, `rrc`, `main_input_raw`.
Update these to inspect pending configuration, explicitly answer before further
work, retain pending across reopen and prove zero extra requests/effects. The raw
input episode includes both exact effort inputs and retains malformed-name and
unknown-command checks. No production change or assertion/deadline suppression
is required. The same four cases plus startup pass5/5 in24.32s. Original full
gate is `context/startup-generation-2026-10-17/source-gate.log`; owning recheck is
`gate-oracle-recheck.log` in that directory.

Scoped Linux Release8/8 passes2.70s and ASan/UBSan8/8 passes11.44s, under the
qualified LLVM23.1.4/libc++23.1.4 profile. Original logs are in
`context/linux/run-0ha1czgb/`. The full Mac gate reruns because its test sources
changed; Linux separately checks the four newly reconciled integration oracles.
