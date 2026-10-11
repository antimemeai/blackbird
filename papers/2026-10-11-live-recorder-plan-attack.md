# Live recorder registration: one plan attack

2026-10-11. Read the command subplan's native registration stall extension against
raw_runtime.cpp's record tour, open_audit chain reconstruction, supervisor child
notice/reply paths, registration copies and closure handling, and RawAudit's
bounded prefix/seed/open behavior. This is the ONE plan attack for the concrete
history-sized registration defect; integrated code/oracle review follows green.

The native evidence identifies repeated full recovery rather than a producer
mutex deadlock. Keeping the recorder alive removes that dependency without
weakening recovery. The plan is released for source with the following existing
ownership constraints made explicit in implementation.

The recorder's failure producer borrows lanes.front().channel. Reserve max_lanes
before admitting additions; do not relocate that object while the producer lives,
and never retire its front failure lane through ordinary lifecycle messages.
Process a lifecycle message only after the current lane reference and all read
spans have ceased use. Addition/erase changes the bounded lane/watch vectors, so
restart iteration from valid indices; do not continue with an invalidated vector
reference. The child holds111 failure bytes before receive/validation/allocation,
updates the actual current journal and metadata failure stage, and releases only
after successful reply. These are the plan's finite-boundary/owned-failure rules,
not a new logger or second error reply.

A lost retirement reply cannot authorize supervisor unlink. Including the retiring
lane's actual P/A-preserved ends in the immediate successor seed makes a restart
with the supervisor's still-owned lane safe. Later seeds can discard older closed
lanes because each predecessor is sealed and validated during real recovery.
The existing open_audit prefix limit (max_lanes + lanes.size() +1)*3 and reader's
separate max_origins bound accommodate the described max_lanes+1 seed. Derive
predecessor identity/sequence/offset from the live cursor, synchronize closure,
seal, seed and new segment before successful reply, and retain the mapped lane
through every failed operation. No old sealed file is scanned during a successful
ordinary live add/retire; actual death still follows the existing full scan.

The PID-stability red is a direct native lifecycle oracle, not a timing benchmark.
The pending actual-child-death cut checks the exchange's independent wakeup and
ownership. The original pressure PTY cut supplies the product behavior oracle
with the same512KiB input and existing observation bounds. These address distinct
fault classes. No timeout increase, transformed writer event, new campaign timer,
review of this review or additional assurance panel is required.
