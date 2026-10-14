# Native Beads and Station fault selection repair

The optimized Beads fixture asks `dladdr` for an inline `RawBeadsArgv::push`
frame that need not exist. Station asks for libc++'s `__tree`, which does not
identify libstdc++ allocation. Replace these fixture predicates; production
code, compiler choices, exception classes and behavioral assertions stay intact.

Beads prepares the existing 8192-byte body before arming. In `BeadsAdapter::run`,
the first allocation of at least that size copies the body for the existing
`body == "-"` validation. The second copies it during body argv acquisition,
after executable, JSON option, actor, operation and title. Fail that second
native allocation. The existing preserved terminal must still report allocation,
stage2 and exactly five completed elements, with every prefix original and no
failed element or mutation child. A changed allocation sequence fails those
assertions rather than silently moving the cut.

Station's control cut fails the next allocation after a new program application
record becomes committed. `AuditLog::record` creates its identity string before submission;
`RetainedState::append_impl` publishes the committed snapshot after the journal
acknowledges the write. The remaining return/emission path allocates nothing.
The next allocation is the real control-set insertion. Existing disk assertions
must show the acknowledged record, allocation at stage6, zero controls/events,
no accepted return and no applied pause.

For Station prompt, count allocations in one actual `format_value(input)` call
on the selected native library before arming. During the actual prompt call,
skip that many allocations and fail the next allocation. Validation and raw
capture before formatting allocate nothing with the thread already bound. This
removes names and library growth-size assumptions. Existing disk assertions
must independently show the complete literal formatter original, allocation at
stage3, no whole-prompt source and no returned source reference.

Run only `beads_raw` and `station_raw` in Release and ASan/UBSan on macOS and
Linux, plus changed-TU formatting/static checks coordinated by the root owner.
One owning-unit code recheck attacks selector location and exception cleanup.
No optimization, production test hooks, noinline attributes, suppressed checks,
new dependencies or full-suite reruns belong to this repair.

The first macOS Release run passes Beads but the initial Station selector
(`committed fact count increased`) cuts an earlier issuer reservation produced
by `AuditLog::issue`. Its existing semantic-record assertion fails. Narrow the
selector to the new committed program application record, ignoring the issuer
reservation. The allocator reads the native event variant/channel without
allocating; the preserved semantic packet and exact stage6 remain its location
oracle. Rerun Station for this actual correction; Beads needs no further change.
