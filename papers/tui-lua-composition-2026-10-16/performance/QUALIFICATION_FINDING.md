# Linux actual-main qualification: transient ready text

The isolated after build106/106 succeeded, and native20-case tui_snapshots passed.
The first actual-main Linux retained-result check failed after8.38s, at its existing
4s expectation of INSTRUMENT_FIXTURE_READY. Original source/archive34c5..., native
binary, build/check logs and all driver artifacts are retained. No timing ran.

The failure frame has persistent Turn completed status, A_ROW_04..A_ROW_23,
A_SETTLED_EXIT_7 and B_PINNED_ORIGINAL; the first four A rows and the earlier
workflow readiness print are above the viewport. The private effect file contains
LAUNCH_A and LAUNCH_B, and native-written identities contains the two actual
returned IDs. The fixture intentionally emits24 A rows to establish nonzero
reading position, so reducing output would discard useful coverage. Completion
is visible while the transient readiness print is not.

Narrow correction: retain exact geometry/output/readiness guards. After submitting
fixture, require the persistent completed-turn status, B's actual output marker and
the existing private native-returned identity file. Parse/validate the same full IDs
and later exact effect Counter. Drop the assumption that an earlier print remains
onscreen while independent native stdout continues arriving. The first old-main
Ctrl-] literal RED remains unchanged and precedes all work. Do not widen deadlines,
repeat unchanged cases, resize away the evidence or infer application handling
from display status. Root must release tracked edits while the source hook runs.

Any overlay/requalification must retain this initial failure/archive identity and
name the actual fixture source separately from the unchanged native binary inputs.
