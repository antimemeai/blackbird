# New runtime join composition — independent code attack

2026-10-15. Reviewed only the newly added runtime join composition:
`src/workflow_runtime.cpp` join restoration/adjacency, creation, task observation,
dirty worklist, selection commit/adoption and retained acceptance sequence.
Consulted `WorkflowStore::transact` for the exact commit/publication contract and
the current runtime tests. No root-source edits, builds, index writes, renewed
executor attack or re-opening of the pure reducer's focused recheck.

## RJ1 — P1: durable join creation can publish only part of its live adjacency

**Anchors:** `src/workflow_runtime.cpp:311–343`, particularly durable creation
at 322–326 followed by 330–341; existing-ID return at 305–309.

**Concrete history:** create join J over several pending/finished tasks. The join
cell commits, J is inserted into the live map and marked dirty. The loop attaches
some tasks' waiter links; an allocation in `read(task)`, `completed.emplace_back`,
result-reference construction or reducer observation then throws. The request
fails, but the live join remains only partly linked/observed. A retry with the
same J descriptor finds the existing map entry and merely returns its snapshot.
No repair attaches omitted members. When an omitted pending task later completes,
its empty waiter list does not notify J. J can remain pending forever, even though
the task's exact result is retained. A ready-task join can likewise select from
an incomplete retained-result subset after its dirty entry is serviced.

Pre-reserving waiter capacity and dirty-vector capacity is useful, but the
remaining postcommit allocations and calls still prevent atomic live publication.
The problem also includes an accepted store commit followed by a thrown append
result before inserting the live map node: the cell exists without its live join.

**Minimal repair:** stage the entire reducer with all currently retained member
observations and the ready-result ordering before the creation transaction;
preallocate every adjacency/worklist/map resource. Commit the staged snapshot,
then publish only with nonthrowing node transfer/moves and reserved pointer pushes.
If any actual postcommit publication cannot be made nonthrowing, mark live state
unavailable/stop admission and reconstruct J from the retained cell and task cells
before inspection or further selection. Existing-ID retry must not silently accept
an incompletely installed join. This is one publication invariant, not a second
ledger.

**Direct oracle:** inject allocation failure after durable creation and at each
postcommit member-install/ready-observation boundary. Retain exact J identity,
retry/read, finish the formerly omitted task, and require either correct selection
from the complete retained batch or an explicit stopped/recovery-required owner.
Never return a usable partial join frontier.

## RJ2 — P2: a selected durable cell can remain reported unselected after append error

**Anchors:** `src/workflow_runtime.cpp:250–270,241–248,624–628`;
`src/workflow_store.cpp:525–546`.

**Concrete history:** the owner prepares accepted candidate S and `store.transact`
durably accepts/publishes the selected join cell, but the append then reports an
error. The store deliberately publishes accepted state before propagating that
error. `settle_joins` therefore exits before the noexcept `join.reducer = ...`.
The dirty live reducer remains unselected although its retained cell contains S.
`tick` closes admission but later join-read still renders the unselected live
snapshot. Retrying selection reads the new cell revision and reconstructs a
different request under fixed submission `join-select:J`, which conflicts with
the accepted original submission. It cannot repair itself through that retry path.

This does not by itself duplicate an effect: admission is closed on the reported
error. It does leave durable selection authority inconsistent with the exposed
live query and can strand progress until an explicit full reopen.

**Minimal repair:** after an accepted-but-error result, adopt the already prepared
candidate only if the retained selected cell exactly matches it, or poison the
live join/owner and refuse ordinary join reads/selections until rebuilding the
committed S. Do not recompute another choice or blindly resubmit against a new
base under the same submission key. Keep actual write uncertainty distinct from
known accepted publication. RJ1's common publication treatment can cover this path.

**Direct oracle:** inject an append error after acceptance of `join-select:J`;
verify the same stored winner is subsequently exposed, or queries explicitly
require recovery. No false unselected snapshot or changed selection is acceptable.

## Behaviors checked without another established fault

Pending restored joins rebuild observations from member task cells sorted by
retained accepted sequence; they do not substitute process-monotonic time. The
new untimed reducer profile makes `{sequence,0,0}` explicit rather than fabricated
time continuity. Task result sequence is committed in the same task transaction.
Global store revision gaps from other transactions do not change order.

Selection preparation and snapshot allocation happen before its own transaction;
successful postcommit adoption uses the reducer's noexcept move. Selected rows
contain exact task-cell/revision references instead of cloned outputs. Selection
is checked only once per dirty join per normal owner drain. Task-map/join-map node
addresses remain stable, and reserved dirty capacity plus per-join dirty flags
avoid duplicate worklist entries in normal operation.

If task-result commit succeeds but an observation update fails partway through
its waiter list, the executor completion is retained for retry. Prior task outcome
and original attempt terminal record are checked on retry; already applied join
observations are exact idempotent redeliveries. Handles/output/outstanding count
are released only after every waiter observation succeeds. This is distinct from
RJ1, which has no completion slot driving retry of incomplete join installation.

Reopen work is proportional to current cells plus relevant join membership and
sorting; per-completion work follows task adjacency, not a whole-graph scan.
The pure reducer still scans its bounded local history/current membership; no
measured asymptotic or latency improvement was established by source review.

The runtime fixture currently exercises task result/identity/reopen but does not
yet exercise the new join API. Root should add direct composed cases for creation
over already-ready tasks, pending registration followed by completion, first-result
retained order, selected and pending reopen, and the two fault histories above.
The pure reducer tests cannot establish adjacency or native commit behavior.

Root integrates these findings; one focused recheck covers only their repairs.
No third assurance layer or renewed review of settled components is requested.
