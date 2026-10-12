# P2 direct recheck: orphan reclamation of an armed dead producer

Genuine retained red: /tmp/blackbird-participants-ZHi0sq. The forwarded native
shutdown returned -1/EBADF and owner exited70. All exact owned holder processes
were killed; actual shared catalogue-lease acquisition proves their inherited
lease descriptors were released before ordinary RawRuntime::open.

The new reporter's unacknowledged8000 is invalid_range/detail0/stage4 (native
read-only diagnostic context/participant-orphan-diagnostic.log). Worker CONTROL
P=A256510, owner CONTROL P1936/A519. record appended/reclaimed the worker's batch,
then failed before preserving the owner's failed2123.

src/raw_runtime.cpp recover_orphans creates source RecorderLanes with absent
producer/recorder endpoints only after catalogue lease and dead owner fence.
record stage4 calls RawChannel::acknowledge on these -1 endpoints. The parked
worker had armed space. src/raw_channel.cpp stores A=end and exchanges that arm,
then raw_send_notice(-1) returns invalid_range. This is a recovery defect; it
cannot be repaired by resuming an inherited alias or changing the participant's
native outcome.

Proposed narrow transfer: include/blackbird/raw_channel.hpp and src/raw_channel.cpp
acknowledgment declarations/body, plus RecorderLane and both open_audit recovery-prefix/record acknowledgment callsites.
Add explicit acknowledge_quiescent(partition,end), documented as requiring
fenced dead producers and no live borrowed endpoint. Share existing partition,
full-event boundary, monotonic cursor and acknowledgment state advancement in
one implementation. Ordinary acknowledge keeps all current notification and
invalid-endpoint errors. Quiescent advancement clears the obsolete space arm
without inventing a notification. Do not silently accept negative FDs generally,
swallow invalid_range, clear an arm before validating the boundary, or touch
producer/writer transforms. RecorderLane carries a default-false quiescent flag. Only recover_orphans sets
that flag true for old source lanes after the dead-owner/exclusive-catalogue
fence. Both open_audit prefix recovery and record post-publication call the
lane helper that selects the explicit method; no negative-FD or drain inference.
Normal supervisor drain still notifies its live source endpoints.

The retained real run is the strongest direct red. Recheck the unchanged native
failed-shutdown fixture after this correction: exact2122 request/native fd/how,
linked2123 actual -1/EBADF, exit70 before any join/workerEND/ownerEND, ordinary
reopen closes old run and exposes each original once. Keep the existing channel
boundary/notification checks; any added primitive fixture must distinguish the
ordinary armed-invalid-FD error from explicitly quiescent cursor advancement
and rejected partial/out-of-range boundaries, rather than duplicate end-to-end
preservation testing. Parent coordinates transfer from the command/channel owner
and chooses integration/static profiles. No runtime source edit before release.

Parent granted this exact transfer after inspecting both callsites. Before helper
changes, context/participant-armed-reclaim-red.cpp directly exercises existing
RawChannel: actual16-byte CONTROL event, actual space arm, acknowledge16/-1. It
must surface invalid_range after A advances, reproducing the missed native wake
condition without source interception. No production code changed for this red.
The candidate adds acknowledge_quiescent and one private Result<bool> cursor/arm
advance helper, preserving existing boundary errors and ordinary wake failure.
The native participant fixture will exercise both quiescent prefix and new batch
reclamation through ordinary reopen, while its primitive cut retains ordinary
armed/-1 refusal plus rejected non-boundary/past-P acknowledgments.

Qualification: genuine armed/-1 baseline exited1 with invalid_range, P=A16.
Explicit per-lane quiescence and the shared primitive are implemented in the
transferred windows. Original retained orphan and fresh failed-shutdown/reopen
checks both pass; selected five debug cases pass9.73s. See the existing whole
participant code-attack report for complete finding/recheck disposition. This
addition is the concrete P2 recheck finding, not a new review layer.
