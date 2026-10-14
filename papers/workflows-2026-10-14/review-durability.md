# Layer-one durability attack — 2026-10-14

Reviewed `docs/CONCURRENT_DURABLE_WORKFLOWS.md` completely, including the source,
retention, control, schedule and artifact sections. This is one independent design
attack, not an implementation review. No design or product edits were made.
P1 means a missing rule can violate a core stated guarantee; P2 means a narrower
semantic ambiguity needs resolution before its owning implementation. These are
concrete permitted histories, not claims that existing code already implements
the proposed failure.

The explicit-segment substrate, compound durable decisions, selected join outcomes,
result/settlement distinction and capability-based external recovery are sound
directions. The proposal correctly avoids claiming exactly-once arbitrary effects.
The following repairs make its boundary rules sufficiently precise to implement.

## D1 — P1: pre-release recovery can launch work cancelled after planning

**Section:** Durable decision and effect protocol, lines 201–223; cancellation,
lines 284–286; activation, lines 491–500.

**History:** Decision D atomically plans commands A and B. A is released and fails.
The owner commits scope cancellation, stops affected admission and prepares to
leave B unreleased. The controller dies. The recovery table currently directs
all after-commit/before-release commands to dispatch through a fresh owner release.
Following that row literally launches B although cancellation committed first.
The same history applies to a pause or successful migration before B's release.

“Eligible attempts” does not define whether eligibility is the committed planning
bit or a fresh guard; the table makes the latter mandatory interpretation unclear.

**Minimal repair:** release is a guarded retained transition on the current
unreleased attempt: current scope/run eligibility, owner epoch, effective pinned
manifest, required current resource/lease grants and cancellation/pause state must
all permit dispatch. A prior decision's eligibility is necessary, never sufficient.
Recovery restores commands without automatically releasing them. If cancellation
wins the release guard, the unreleased command gets a known-not-dispatched outcome.
If release won, later cancellation must handle possible dispatch uncertainty.
Command cancellation cannot silently change an already released attempt's identity.

**Direct check:** inject death after cancellation commit but before any B release;
independently count B effects as zero. Also reverse the order and require honest
uncertainty when release committed first.

## D2 — P1: referenced source/input/result bytes need an ordered retention barrier

**Section:** Durable decision/effect protocol, lines 201–214; manifests,
lines 479–481; persistence/originals, lines 561–589.

**History:** a manifest identifies immutable Lua source S; a result identifies
original bytes R. Their bytes have been emitted into the separate recorder or
prepared immutable-payload path but remain pending. A semantic decision commits
only their IDs/digests, selects R, and releases a publishing effect. The recorder
dies before persisting S/R, followed by controller restart. The semantic ledger
now claims an effective source and selected result whose bytes cannot be reopened.
Immutability of a reference does not establish durability of its target. Allocated
capture credit establishes capacity, not completion of retention.

The source-ACK section explicitly includes source bytes, which is good. The
same obligation is not stated as the precondition for decisions, results, manifest
activation and effect release referencing bytes in separately written stores.

**Minimal repair:** define the referenced-byte retention ordering: every required
manifest/input/original/state blob reaches its actual retained boundary before
the semantic commit that depends on it becomes externally observable or eligible
for dispatch. A prepared transaction may carry both writes if recovery treats
the whole unit atomically. If stores are separate, commit references only after
the requisite store completion and retain them as live roots. Reopen validates
referenced storage identity/length/completeness; missing bytes block/reconcile the
affected decision rather than substituting current checkout bytes or using a
digest as the original. Audit-store completion may have a different approved
failure scope, but that scope cannot silently weaken workflow-source recovery.

**Direct check:** kill the recorder/payload writer while source or result bytes
are pending, then request decision publication/release and restart. Neither a
lost referenced source nor an unretained selected result may authorize the effect.

## D3 — P1: a cumulative cursor ACK can acknowledge an unretained earlier event

**Section:** Durable messages/events/waits, lines 338–357.

**History:** a feed has offsets 7 and 8. Parallel acquisition delivers 8 first;
the owner durably accepts event 8 and its bytes. The adapter acknowledges cursor
8, satisfying the current per-event rule that acknowledgment follows retained
acceptance. Event 7 has not reached retained acceptance. Crash and reconnect from
ACK cursor 8 permanently skip 7. All acknowledged event-8 bytes survived; the
acknowledgment nevertheless discarded other covered work.

The current adapter capabilities specify cursor replayability and ACK
idempotency/queryability, but not cumulative versus individual acknowledgment.

**Minimal repair:** an ACK's declared coverage set/range is part of its contract.
Per-item ACK follows retained acceptance of that item. A cumulative frontier can
advance only through a contiguous prefix whose every item has been retained,
or through retained explicit gap/skip observations under a source-supported
contract. Persist pending out-of-order acceptance and the acknowledged frontier
separately. Batched ACK covers all covered events and their bytes, not just the
highest item. Source acknowledgment itself retains request/outcome uncertainty.

**Direct check:** accept offset 8 before 7, crash around ACK, reconnect and
independently assert that 7 remains deliverable/unacknowledged until retained.

## D4 — P2: deadlines and first-result selection lack one adjudication order

**Section:** Durable joins, lines 250–265; time, lines 375–390.

**History:** join deadline is monotonic t=10. The owner is delayed. A provider
result is queued at t=11 while the timer service has not enqueued its firing yet.
The owner handles success first and selects it. Another execution of the same
admitted contract queues the timeout first and selects timeout. Both follow
owner arrival order, but the first accepts an outcome beyond the stated deadline.
Another ambiguity is batch identity ordering versus previously assigned arrival
sequence: batching must not rewrite what “first” means.

This is not an argument for guessing global physical completion order; local
deadline eligibility also needs a declared rule.

**Minimal repair:** pin an explicit selection cutoff/order profile: for example,
native owner acceptance time/sequence determines eligibility against the named
clock deadline, with stable tie handling; alternatively declare event-race semantics
where timeout acceptance rather than nominal due time is the cutoff. Retain that
acceptance/sequence and timer observation. A delayed timer cannot accidentally
change profiles. Sort by the retained selection order; identity breaks actual
ties, not different accepted sequences. Recovery uses the same profile and
committed winners.

**Direct check:** pause timer enqueue and owner handling independently across the
deadline, including equal-clock readings; assert the chosen profile, not whichever
callback happens to run first.

## D5 — P2: keyed replay must detect omitted historical commands, not only mismatches

**Section:** Recorded-command functions, lines 143–157; migration, lines 503–508.

**History:** history contains keyed commands A and B; B is still pending. A
replacement allowed through replay-compatible migration emits A with matching
inputs, omits B and emits new C. A dictionary keyed lookup finds A correctly;
C has no recorded entry and reaches the fresh-call seam. No encountered key
mismatched. C can dispatch before the frontend notices that B was removed, leaving
an unmatched pending obligation. A function that returns after A may never encounter
another command on which to report divergence. Reusing a key in multiple dynamic
locations creates the analogous aliasing problem.

The requirement to expose the first changed command is good, but a keyed lookup
alone cannot find an absent command or prove a complete matching historical prefix.

**Minimal repair:** define an explicit replay frontier/completeness check. Before
new dispatch or successful return, every required historical command through that
frontier must be accounted for exactly once by identity and semantic signature.
Omitted, duplicate, reordered-when-order-sensitive and type-changed commands
refuse. A migration may retire/transfer B only in its explicit obligation map,
not by omitting the call. Checkpoint source/state retains the covered frontier
and pending-handle roots; partial branch reexecution uses new lineage and declared
reuse, not silent same-key substitution.

**Direct check:** migrate A/B to A/C and A-only with B pending, and duplicate a
dynamic key. No C effect or apparent completed scope may precede resolution of B.

## D6 — P2: stale-generation result capture must survive loss of publication authority

**Section:** Participant incarnation checks, lines 471–475; activation,
lines 491–501; originals, lines 577–589.

**History:** migration records an old remote request as uncertain and transfers
its observation obligation through a supported adapter. The new epoch activates.
The old provider callback finally brings exact response/tool bytes. A blanket
stale-callback guard rejects it before original retention, or treats “cannot
publish” as prohibiting settlement of the old attempt. The new owner permanently
loses an observation that would reconcile the uncertain old effect.

The design rightly forbids stale dispatch/publication. It needs to distinguish
that authority from attributed capture and reconciliation of already admitted work.

**Minimal repair:** retain every acquired late observation under its original
attempt/incarnation regardless of current publication authority. The current
owner may use it to settle/reconcile that original attempt through a guarded
transition. It cannot resume old Lua, dispatch tool calls or publish into the new
context/run without a separate current authorization. Unknown/unmatched callback
identities remain auditable refusals rather than silently discarded bytes.

**Direct check:** activate a new epoch, deliver old-generation provider completion
and tool bytes, and verify retained originals/old-attempt disposition while no
old-generation tool or new-context publication occurs.

## D7 — P2: force-execute needs a distinct actual-execution contract

**Section:** Idempotent submission, lines 110–115; artifact demand,
lines 411–438.

**History:** an action has a cached result and an equivalent in-flight producer.
The operator asks force-execute to check a suspected nondeterministic compiler.
The implementation bypasses the artifact cache but returns the existing
same-submission run or joins the equivalent producer. No new compiler executes.
Alternatively, it changes the invocation identity and accidentally repeats a
nonrepeatable publication attached to that action. Both interpretations fit the
unspecified force control.

**Minimal repair:** distinguish cache bypass, in-flight coalescing permission,
fresh invocation requirement and authorized reexecution of effectful work.
Retain the requested mode and resulting producer/attempt. Default force on a
repeatable producer means an actual fresh eligible attempt; submission-key
deduplication cannot swallow it. Downstream irreversible publication is separately
admitted and never follows merely from force-building the artifact.

**Direct check:** cache a producer, keep an equivalent one in flight, then force
an actual execution; independently count the expected fresh producer and zero
unrequested publishing effects.

## Checked boundaries without additional findings

The design already requires complete state/commands/consumed inbox/waits in one
committed decision, records winners before exposing selection or loser controls,
keeps changed defaults pending through affected-work conclusion, refuses to treat
a checkpoint as that conclusion, and explicitly distinguishes local drain from
unknown remote disposition. Those contracts should be preserved.

Schedules retain timezone-data revision and fold/gap policy; the owning unit still
needs retained actual rule bytes/validity and exact recurrence semantics. D2's
retention rule covers the bytes. An owned TZif mechanism can use IANA data without
selecting a library. Keyed stream correlation, late-data/retraction policy and
artifact input/environment validity are appropriately explicit; no additional
dataflow/artifact fault was established beyond D2 and D7.

No cross-outpost speculation is needed to repair these findings. Group commit
and pure planning remain compatible with a durable release frontier. One focused
recheck of integrated repairs is sufficient; do not create another assurance layer.
