# F1 revisioned application-cell store subplan

2026-10-15. Owning lane: workflow store. All writes are confined to the isolated
`blackbird-workflows` checkout. Root owns shared build configuration, builds,
journal/beads and integration. No library adoption.

## Purpose and invariant

Provide typed workflow application cells and one retained atomic transition over
their expected read revisions, all changed cells and optional decision metadata.
One thread/process owns the store. A stale read rejects the whole proposal before
retention; an exact repeated submission returns its original commit; changed
inputs under that submission conflict. Store code dispatches no external effects.
The program record is a native BBM2 delta in the existing AuditLog/RetainedState,
never a separate database or JSON representation.

## Native mechanism

Use typed cell IDs, schemas and revisions. Creation requires an absent revision
zero read; existing cells increment revision and keep their schema. Every write
requires a unique matching read. Stage only changed map nodes and the submission
receipt before the audit append, then publish without allocation after a successful
append. Owned Value payloads use their existing immutable sharing/copy-on-write
behavior. Exact canonical native request comparison reads the retained original
packet through its fact locator; old request payloads need not remain resident.

Expose a reducer with explicit delta replay and snapshot/restore so the root can
extend current_programs/saved-state label reduction. The store restores a supported
snapshot plus suffix when available; without an integrated snapshot label it uses
checked historical fact replay and makes no bounded-reopen performance claim.
Dedup locators remain current state until a separately designed archival index;
capacity refuses rather than evicts identities and accidentally repeats work.

The owning host also supplies optional native lifecycle events. One append commits
the cell delta first and those events after it. The delta stores physical event
references, not duplicated effect input/result bodies. Fresh Decision/Invocation/
Admission/Open/Receipt/Observation variants are supported; preexisting identities,
duplicate kinds/IDs in the batch and unsupported variants refuse. This preserves
contiguous semantic fact ordinals. Retry/other variants require an explicit future
query extension before admission. Exact duplicate comparison fetches original
typed facts and compares their complete native encodings.

Existing source grounding: AuditLog::record commits one ApplicationRecordEvent;
RetainedState publishes only committed facts and exact duplicate records;
TaskState demonstrates changed-node preparation and replay validation; Value owns
shared storage and detaches exposed mutable containers. The current saved-state
reducer knows a fixed label list and must explicitly incorporate these cells.

## Direct oracles

Write specification cases before source: two-cell stale read changes neither cell
nor decision record; successful multi-cell change produces one program record;
identical and reordered-map submissions return the original commit without a new
record; changed payload/decision conflicts; reopen produces exact cells/receipts;
wrong thread/process refuses; caller mutation cannot change retained state;
failed audit sync publishes no cells; actual committed-but-unacknowledged bytes
are interpreted on reopen rather than replanned. Check snapshot plus suffix with
the same exact result oracle. Use a native memory JournalDirectory fault fixture
for sync/append failures, not test-only production callbacks.

No builds run in this lane. Root runs affected compilation/tests and the strict
source gate. One code attack, then one focused recheck/fix total; no third layer.
