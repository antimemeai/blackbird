# External observer termination race

First matched attempt ended after44 process samples (43ok, one collector failure):
addressed80x24 trial4. All ten native PREP/READY frames and INPUT1–12 were present,
including the latest geometry11 after drain. The original terminal ends/events,
clocks, source, binary and sample remain. The collector did not preserve that
child's final return code; no claim that it was zero is made.

The actual failure occurred0.7ms after pressure release, not at the5s deadline.
`until(lambda: child.poll() is not None, ...)` first checked that predicate, then
separately checked child.poll to break on exit. A child exiting between these two
polls made the desired terminal state trigger the error path. The wait therefore
raised 'native fixture did not exit cleanly' precisely when it had observed the
child stop, before saving the actual return code. This is a direct source defect;
the failure's timing and branch identify that premature break.

Correction: `until` has one success predicate and its existing5s deadline. Remove
the independent child-exit early break. This also lets final queued events/PTY
bytes be read after native exit instead of treating process termination as an
absence of final observations. A real failed/dead child still fails the same
predicate/deadline, and the final explicit exit-code-zero check remains. Record
child.returncode in both batch and PTY finally blocks after cleanup, so failures
also preserve actual natural or forced termination. No deadline or frame/input
assertion is weakened, no native implementation changes, and no failed original
is overwritten.

Draft corrected source is observer-exit-correction.py under ignored context while
the source gate keeps tracked files stable. Root must release that constraint
before the correction lands in scripts/bench-terminal-edge. Same-unit review
covers this concrete correction; no new panel. Exercise one actual development
qualification of the corrected observer (count1,iterations4) before any corrected
matched round. Preserve the failed original44-sample interval separately. A new
30-trial common round, if root authorizes it after qualification, is necessary
because the first observer aborted; it is not a repeat to improve a valid result.
