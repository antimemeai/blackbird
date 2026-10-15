# F3 join reducer: independent code attack

2026-10-15. Layer-one source review of `workflow_join.hpp`, `workflow_join.cpp`, `workflow_join_test.cpp`, `join-plan.md` and the integrated `PLAN.md` in the isolated workflow checkout. No build/test/index writer, product calls or join-source edits. This is one attack; the owner integrates findings, then one focused recheck follows.

## J1 — High: closed quorum can wait when satisfying it is already impossible

[`propose()`](../../src/workflow_join.cpp:279) only emits impossibility after `all_final`: every member has a terminal observation. The quorum success check at line 306 counts qualified results, then the final impossibility branch at line 315 again requires all members terminal.

**Failure schedule:** closed membership `{a,b,c}`, quorum 2. Observe final failure for a and final cancellation for b. Member c is unknown or never returns. At most one member can ever qualify, so the quorum is impossible now. `propose()` instead remains empty forever. Closed quorum 4 over three members has the same defect even before any result arrives. This can strand a parent and its cancellation/repair policy on a branch that cannot produce the requested outcome.

**Minimal repair:** derive the maximum achievable qualifying count from retained final outcomes plus still-possible members. For closed membership, emit impossible once that upper bound is below k, independently of unknown members finishing. Unknown results still count as possible where qualification could become known; final failed/cancelled/nonqualifying members do not. Preserve open-membership behavior until closure or an explicit finite producer bound. Apply equivalent early impossibility to modes where their declared semantics allow it; `all`/final collection should continue to require all final rows.

**Direct check:** quorum 2 with two final failures and one unknown must propose impossible immediately, and snapshot/restore must preserve that decision. A closed cardinality smaller than k must also propose impossible. An open expansion in the same state must remain pending.

## J2 — High: a custom predicate can turn typed failure into successful modes

[`qualifies()`](../../src/workflow_join.cpp:254) returns the custom predicate directly, replacing rather than refining typed success. [`propose()`](../../src/workflow_join.cpp:283) only checks `terminal`, so failed/cancelled rows pass an always-true predicate. Consequently `first_success` can choose a failure, `quorum` can count cancelled workers as qualifying successes, and `all_success` at line 300 can report satisfied with an actual failure in its selected rows.

**Failure schedule:** choose `all_success`, pin a pure predicate returning true (for example, schema presence), observe a failed member whose result still contains valid diagnostic fields and two successful members. All three qualify; the join reports satisfied. The integration may advance publication despite an explicitly failed prerequisite. The `select` mode already provides the appropriate surface for arbitrary terminal predicates.

**Minimal repair:** for `all_success`, `first_success` and success quorum, require `outcome == success` and then the optional predicate. Keep `first` as any terminal result and `select` as its explicitly declared arbitrary matching predicate. If a distinct general-purpose quorum over arbitrary terminal conditions is intended, expose/document that separately instead of letting `all_success` lose its typed meaning.

**Direct check:** an always-true pinned predicate must not make failure/cancellation satisfy `first_success` or `all_success`; a deliberately failure-matching `select` should still work. Combine this with J1 so nonqualifying final results reduce a closed quorum's potential.

## J3 — Medium: absent a deadline, mixed clock epochs bypass the declared contract

[`compatible_order()`](../../src/workflow_join.cpp:120) returns true whenever epochs differ. [`validate_acceptance()`](../../src/workflow_join.cpp:193) rejects an epoch mismatch only against a configured cutoff; a join without a cutoff has no epoch consistency check. [`earlier()`](../../src/workflow_join.cpp:113) then orders solely by sequence and member ID, including equal sequences from different epochs.

**Failure schedule:** no-deadline `first_success`; observe b with sequence 1/epoch 1/time 100, then a with sequence 1/epoch 2/time -100. Both observations are accepted as though they shared one acceptance batch, and a wins by name. `join-plan.md` explicitly promises clock-epoch mismatch refusal; the implementation instead silently compares unrelated incarnations. The descriptor does not carry an explicit globally ordered cross-epoch profile authorizing this behavior.

**Minimal repair:** pin the join's acceptance clock epoch on creation/first observation (also timeout) and reject other epochs, including during restore. Alternatively define and implement a deliberate cross-epoch profile with a retained global sequence domain and exact batch identity, but do not inherit it from a matching integer sequence. Recovered timer remapping remains the owner's explicit protocol.

**Direct check:** mixed epochs must fail without a cutoff too; equal sequence may only form a tie batch within the same epoch/timestamp. Verify snapshot/restore rejects the mismatched history.

## J4 — Integration obligation: acceptance can allocate/reinvoke a predicate after durable commit

[`accept_committed()`](../../src/workflow_join.cpp:326) recomputes `propose()` after the owner has durably stored the selected proposal, then copies the selection into its optional at line 333. Both paths allocate; predicate invocation can also throw. The current methods generally leave the old reducer state intact on those failures, which is good, but they do not provide a nonthrowing apply step after the external commit.

**Failure schedule:** `propose()` succeeds, root commits the selection, then memory allocation fails in recomputation/copy. The owner has a selected journal frontier while its live join remains unselected. Continuing ordinary dispatch from that object would violate committed-selection authority. Retry must not ask a model to choose again or select newly arrived winners.

**Required integration treatment:** either prepare the complete in-memory publication before durable commit and adopt it without allocation/predicate recomputation, or explicitly poison/stop the live owner after postcommit apply failure and recover that same committed selection before further dispatch. `PLAN.md` should choose one. This need not add another ledger or review layer. A pure predicate is an authored contract; making it total under arbitrary allocation failure cannot be assumed from purity alone.

## Source/test observations that withstand this attack

- Proposal does not expose committed selection; stale base revisions and differing candidates reject. A committed choice remains immutable after newer observations, and idempotent acceptance is checked.
- Exact observation redelivery is idempotent while same-identity changed payloads conflict. Unknown stays distinct from terminal failure/cancellation. History/member counts are bounded.
- All/final collection preserve declared member order. Early quorum/first selection sorts retained acceptance order with a stable identity tie rule; ordinary deadline cutoff excludes equality and delayed timeout delivery does not admit late results.
- Member add/close are observer-local, not calls into a producer; after a selection they refuse mutation. No join operation calls providers, cancels children or claims their settlement.
- BBM snapshot coverage includes frozen choices, later revisions, pending unknowns and timeout observations. Current corruption tests are deliberately narrow and do not cover J3's plausible but invalid cross-epoch history.
- `add_member`/`observe` append before incrementing revision, and proposal construction is pure with respect to reducer state. Allocation/exception behavior should be handled at the integrated commit frontier as described in J4, not papered over with a generic caught-success result.

## Focused recheck

Recheck J1 potential-count impossibility, J2 typed success filtering, J3 epoch/domain rule and J4's selected commit/publication failure policy only after owner changes. Root retains sole build/test/index ownership. No broad second attack, no review-of-review or third assurance layer.
