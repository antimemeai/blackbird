# Protected data and control capacity

2026-10-11. Short proposed primitive subplan for the auth-lock/reentrancy gap.
Design only; source waits for the current checkpoint and root review. Generalize
the existing move-only control claim rather than creating parallel claim state,
registries, queues or metric machinery. Existing control callers remain unchanged.

## Purpose and ownership

raw_wait_data checks available space but does not retain it. An owned cancellation
callback can emit after that check and consume capacity needed for a later locked
snapshot. The present control token already protects bytes against such emission.
The same mechanism must protect a data burst acquired after admission, with no
wait under the product lock and no body copied while admission waits.

One RawProducer continues to own a lane. A claim contains its producer pointer,
selected data/control partition and remaining admitted byte
count. It holds capacity only, not payload. It borrows the producer; the claim must
be destroyed before that producer/mapping is retired. No shared ownership,
allocation, callback or new identity accompanies a claim.

The producer pointer and partition must match before any envelope/body copy or
cursor/ordinal/accounting mutation. Replacing or retiring a borrowed mapping
while its producer/claims are live violates the existing owner lifetime. An epoch
copy would not make dangling/reused owner pointers safe, so it is not stored or
compared. The native pointer/remaining/partition structure is24 bytes on the
qualified64-bit profiles; calculate its actual size rather than adding a ledger.

## API and representation

Use one RawCapacityClaim implementation; preserve RawControlClaim as its alias and
RawControlToken/public raw_claim_control callers. Add raw_claim_data(amount,
RawWaitControl=raw_wait_control()) returning the same owned capacity token.
RawProducer::hold_control remains a forwarding convenience to its generalized
data/control hold. raw_emit already receives partition and optional claim; keep
that call shape. Unclaimed emit/reserve and raw_wait_data remain available.

Generalize producer held_control_ to held_[3], indexed by the existing checked
RawPartition mapping. Claim admission supports data/control. Failure stays on its
existing prepaid reserve/release path; a new generic token must not interfere
with the recorder's one-failure-slot accounting. Unknown/failure claim partitions
return invalid_range without mutation. No shared-mapping or disk ABI change.

Availability remains physical free minus ordinary reservations minus protected
claims for that partition. Validate each subtraction before performing it: P>=A,
P-A<=capacity, reserved<=physical_free, held<=physical_free-reserved. A violated
accounting condition returns conflict/corrupt under existing conventions; never
wrap, saturate and pretend capacity was admitted, or silently discard credit.

Grant only when amount<=the resulting available bytes. Adding amount to held is
then bounded by actual physical capacity; still express it with checked arithmetic
or subtraction-based guards. Consume only size<=claim.remaining and size<=held,
after owner/partition validation and the existing ordinal/publication checks.
Successful publication deducts actual record size, not the initial worst burst.
Ordinary emission respects protected held bytes. release_reservation clears only
ordinary reserved bytes; it cannot release claims. Claim destruction releases
only its own remaining bytes once, without a wait or syscall.

Move construction transfers all ownership and clears the source. Move assignment
first releases the destination's previous claim, then transfers the source, even
when the two claims select different producers/partitions. Moved-from destruction
does nothing; using a moved-from token cannot publish. Copy remains deleted.

Add split(amount) returning the same claim type for the same producer/partition.
Require a live owner and amount<=remaining. On success the original remaining
decreases by amount and the child receives exactly amount; aggregate held bytes
do not change. This neither admits additional capacity nor waits, allocates,
captures a body or publishes a record. Independent destruction/publication of
parent and child releases/consumes only their respective remaining bytes. An
oversplit returns capacity with no mutation; an inert moved-from token returns
conflict with no mutation. A live zero-byte split is inert capacity, not a grant
of new bytes. Split supplies narrowly scoped nested RawRegion settlement credits
from an owner's existing burst before entering its lock.

## Admission and failure behavior

Reuse the current cancel/deadline-aware admission loop for both selected
partitions, including arming space, rechecking actual available bytes, supervisor
status/death and waiting on the actual lane endpoint. raw_claim_control semantics
and exact errors stay unchanged. The data wrapper selects data capacity and the
same explicit/inherited RawWaitControl. No additional timer, gate or wait loop.

Cancellation/expiry before grant returns the existing interrupted/native detail,
including when capacity is free. It consumes no payload, cursor, reservation or
ordinal and does not fence emission. An amount larger than the selected lane
capacity refuses before acquisition. Once granted, the owner uses the prepaid
claim in a callback-free acquire/publish interval and through bounded locked
bursts. Prepaid records remain publishable after cancellation or the existing
ingestion fence; that capability does not authorize new acquisitions after refusal.

The same ordinal-overflow margin continues to cover physical capacity, ordinary
reservations and protected claims across every partition. Near-overflow rejects
new admission; already held records finish. Do not add a second overflow policy.
The actual producer/context must outlive all tokens. Claim transfer does not
transfer lane execution ownership or authorize concurrent emission.

## Direct source-before-green oracles

Use independent native envelopes and actual available/cursor observations. Reuse
the current tests' sole-owner channel fixtures and actual runtime cancellation
seams; do not add a token accounting shadow ledger merely to certify this token.

| Case | Independent expected result |
| --- | --- |
|Reentrant ordinary emission|Data capacity128; hold96; the real business callback emits a32-byte ordinary record. Available becomes0. The actual locked snapshot publishes its literal96-byte record from the original claim without new admission/wait; exact two records and ordinals.|
|Partition independence|Held data does not reduce control availability; held control does not reduce data. Ordinary reserves and multiple claims in one partition sum to exactly the physical bound. Failure reserve remains independent.|
|Free/oversized/checked aggregate|Exact-fit grants; capacity+1 and UINT64_MAX refuse; invalid partitions refuse; no cursor/ordinal/held mutation or arithmetic wrap. Byte availability equals the directly observed remaining physical window.|
|Owner/partition mismatch|Use a claim on another producer or wrong partition. Return conflict before copies; both source cursors and claim.remaining stay unchanged. Mapping/producer retirement while tokens remain live is outside the lifetime contract.|
|Move and release|Move construction leaves source inert. Assignment releases only destination's previous remaining credit, then adopts source. Destruction restores exactly unused capacity, with existing published records untouched. Exercise different producers/partitions.|
|Split|Split a literal admitted burst into exact parent/child byte counts with aggregate available unchanged. Publish independently and destroy in both orders; only unused respective bytes return. Move the child, test moved/inert/oversplit refusals without mutation, and owner/partition mismatch before publication. No admission/wait occurs during split.|
|Ordinary reservation release|Clearing a transient reservation leaves protected credit held; the claim still emits the literal. A claim release cannot clear the failure reservation.|
|Canceled free/full data admission|Sticky cancel before grant refuses with unchanged native detail/cursors. Actual full registered data lane with recorder stopped/supervisor running reaches identified space admission; cancellation/expiry wakes it. Existing claims and prepaid terminal records still publish.|
|Overflow/fence|Arrange the existing near-overflow ordinal boundary. New data/control claims reject under the same policy while the already admitted literal terminal/burst finishes. Known ingestion fencing closes new grant; existing valid token remains usable.|
|No body during wait|Place literal source acquisition after actual grant in the native fixture. While data claim admission is parked, no source read/acquisition occurs and no metric body is copied. Releasing space permits the one literal acquisition/publication.|

The existing control tests are compatibility checks, not an invitation to rerun
the settled primitive model as a new campaign. The owning direct data cases are
the necessary reds. After review implement the single generalized mechanism,
fix exposed defects, run direct greens and one code/oracle attack with its
finding-driven recheck under the existing two-layer limit. The root owns aggregate
source/profile gates. This note does not yet authorize auth source or claim auth
burst sizes are finished.
