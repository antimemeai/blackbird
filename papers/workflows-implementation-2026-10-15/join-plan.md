# F3 native structured joins — pre-code plan

Own a pure single-owner join reducer, separate from scheduler, ledger and effects.
Grounding: reviewed concurrent/durable design's structured joins and owner-acceptance
cutoff; Restate durable selection retains completion order; Argo separates stopping
admission from drain. Prior studies and exact sources are in
`papers/workflows-2026-10-14/durable-workflows.md` and the two bounded design reviews.

The reducer retains pinned descriptor, explicit bounded membership, observation
revision history and typed timeout. Pure `propose` produces a read-only selection;
`prepare_acceptance` stages a complete unpublished accepted candidate before
durability. The owner commits its snapshot and adopts with noexcept move; direct
live `accept_committed` instead requires stop/recovery if postcommit apply fails.
The owner persists the exact native Value selection/snapshot through BBM. Proposal
acceptance rejects stale state or altered selection. No join calls providers,
cancels siblings, closes a producer or claims execution settlement.

Observation identity is member plus result revision. Exact redelivery is idempotent;
changed same identity conflicts. Increasing result revisions remain retained and
cannot rewrite a committed selection. Observation finality is explicit and separate
from outcome or physical settlement. A final unknown is result_ready for first/all/
final collect; an unresolved unknown has final=false. Success modes require typed
success refined by their pinned predicate. Membership add and close are explicit
observer-local inputs. All/final collect and impossibility require closed membership.
Closed quorum uses the exact upper bound from final nonqualifying outcomes plus
unknown/unseen possible members; it need not await every member when the finite
bound already proves impossibility. Open membership has no implicit finite bound. First/quorum may
freeze early. Bounded collect here is final collection, not a streaming subscriber.

Selection uses retained owner acceptance sequence; identity only breaks a shared
acceptance batch. A deadline excludes completion at/after cutoff independently of
timer delivery; only a typed observed timeout can produce timeout selection.
Clock epoch mismatch refuses; cross-incarnation timer remapping is the durable
owner's separately qualified recovery policy, not implicit arithmetic here.
No-cutoff joins can explicitly use unmeasured acceptance `{sequence>0,0,0}`,
ordering by retained owner sequence without any physical clock comparison. All
observations consistently use untimed ordering or one nonzero measured epoch.
Untimed timestamps with nonzero ns and measured/untimed mixtures refuse. This
supports durable cross-process no-deadline joins without fabricating monotonic
time continuity. Cutoff joins still require a nonzero measured epoch.
Pure pinned predicates are caller-supplied and never serialized as closures.

Direct tests precede implementation: all modes, failure/unknown, out-of-order
arrival, stable ties, closed/open finality, impossible quorum, timeout delay/cutoff,
duplicate/conflicting/revised observations, stale proposal, winner immutability,
no committed selection from proposal alone, BBM snapshot/reopen and corruption.
Bounds cover membership and retained observations; no unbounded collect/history.
No libraries or suppressions. Root owns CMake, build, durable storage, shared journal
and final code attack/recheck; this lane writes only its four assigned files.

Actual integrated reopen exposed a format boundary: WorkflowStore reorders object
keys and BBM normalizes nonnegative signed integers to its unsigned wire tag.
Restore therefore compares objects independent of key order while refusing
duplicate keys, preserves array order, and compares numeric native wire forms:
equal nonnegative signed/unsigned integers, distinct real/integer tags, exact real
bits and Decimal fields. Direct regressions permute nested objects and BBM-reopen
signed ns0/results; duplicate keys, integer-to-real and real sign-bit changes
refuse. These fixes answer the actual failing integration case, not a new review.
