# One capacity-claim code and oracle attack

Inspect the implemented generalized claim against the reviewed partition/split
plan and actual sole-producer/runtime admission loops. Existing owner pointer,
partition and remaining bytes are sufficient: split transfers protected capacity
without a physical hole or another admission, and aggregate accounting is local
to the sole producer. This is the unit's ONE code/oracle attack.

## R1: a waiting owner cannot release its own held capacity

`claim_capacity` compares the requested amount with physical lane capacity before
waiting. That is insufficient after protected claims. With capacity128, a held96
and P==A, a request64 cannot ever succeed while that same owner is blocked: the
recorder has no published bytes to reclaim and cannot release the held96.
An owner deadline changes the eventual error to ETIMEDOUT; it does not make this
a legitimate recorder wait. The same boundary applies to nested control claims
and `raw_wait_data` while protected data is held.

Expose the actual owner's checked maximum additional grant,
capacity-reserved-held. Refuse `capacity` before a wait if a request exceeds it.
Do not infer that bound from separately read availability and pending bytes: an
acknowledgement between those reads can underestimate the invariant maximum.
Use the existing owner fields, with no new counter or shared representation.
The actual runtime oracle must first show the current deadline error, then the
direct capacity refusal under held credit and an empty published lane. Keep the
claim intact and acquire no source/body. Exercise data, control and data wait.

## R2: new lint suppressions violate the working requirement

The new moved-from and invalid-enum cases introduced NOLINT annotations. The
operator explicitly forbids adding rule suppressions. Retain the valid direct
oracles, express ownership transfer and fixed-width native enum inputs without
those annotations, and run the normal diagnostics. Existing historical
annotations are not permission for new ones.

## Checked sound paths and limits

Grant validates selected data/control partition and available bytes, accounting
deduction is checked before copying, ordinary reservation release leaves claims
held, and split/move/destruction affect only the transferred remaining credit.
The direct literal tests attack real nested ordinary emission, both split
publication orders, destruction orders, owner/partition mismatch, exact/oversized
grants and cancellation/fence continuation. Runtime source acquisition occurs
after the identified registered endpoint space wait and actual grant. Its native
SCM_RIGHTS endpoint identity distinguishes the wait from gate/status RPC polls.

Implement the two findings and directly recheck them, along with the affected
greens and normal source gate. Do not add another assurance panel or a review of
this review. Borrowed producer destruction while claims remain live continues to
violate the explicit lifetime contract; extra epoch fields cannot cure a dangling
producer pointer.
