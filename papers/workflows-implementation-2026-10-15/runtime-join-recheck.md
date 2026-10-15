# Runtime join composition — single focused recheck

2026-10-15. Rechecked only RJ1/RJ2 from
[the independent runtime join attack](runtime-join-code-review.md). No builds,
source/index/common-file edits, broader new attack, or third assurance layer.

**RJ1 resolved in source.** `join()` now gathers and sorts completed-member
acceptance order and applies all corresponding observations to an unpublished
map-node reducer before the initial store transaction. It reserves every waiter
vector and dirty-worklist entry before commit. After accepted creation its
`publish` lambda performs the existing staged node transfer, reserved pointer
pushes and dirty flags; no result reads, result construction, observation history
allocation or sorting remain in that publication step. Keys were checked under
the sole runtime owner, so the referenced task entries cannot disappear during
this operation. The stored initial snapshot remains empty intentionally: reopen
reconstructs its derived observations from retained task revisions, while live
publication uses the already prepared equivalent reducer. Reference:
`src/workflow_runtime.cpp:321–366`.

Creation's accepted-then-error path uses the store revision fence to publish that
same prepared node/adjacency before propagating the failure and closing admission.
A pre-acceptance failure leaves the candidate unpublished. In the existing
single-owner, synchronous WorkflowStore contract, only this transaction can advance
the store to its expected next revision during the call. This closes the missing
map/partial adjacency retry history as well as the allocation history.

**RJ2 resolved in source.** `settle_joins()` prepares accepted reducer and serialized
state before transaction. If `transact` throws after the WorkflowStore publishes
the expected new revision, the catch block adopts the same prepared reducer with
its noexcept move before rethrowing. It does not ask the predicate to choose
again. The dirty entry remains, but the next service pass explicitly skips a
new selection transaction when that reducer already has committed selection and
then removes the entry. Thus readback exposes the retained choice and does not
retry a changed-base request under `join-select:J`. Pre-acceptance failure leaves
the original reducer unchanged. Reference:
`src/workflow_runtime.cpp:253–280`; the relevant WorkflowStore contract remains
`src/workflow_store.cpp:525–546`.

No RJ1/RJ2 residual repair identified. Direct composed runtime/host cases and
accepted-publication failure injection remain root-owned qualification work;
source inspection alone does not establish their execution outcome. This ends
the requested focused recheck.
