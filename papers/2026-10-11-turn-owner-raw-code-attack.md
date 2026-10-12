# ONE turn-owner code and oracle attack

2026-10-11. Read the complete reviewed turn owner against raw_turn.hpp/cpp,
CodingEngine turn/admission/Running/rollback, context-budget validation,
staging/observation/restoration, protocol validation, staged command-task nodes,
private operation/exec handoff and accepted command settlement, and the complete
turn_raw_test fixture. Context finish/reject/append/checkpoint and actual retained
append/rejection paths were read where these supply the turn's acceptance flags
and prepaid stop operations. This is the owner's one whole code/oracle attack.
No production edits, new test run or second review panel was performed here.
Root reported native green3.38s; release/ASan and the affected static check remain
owner qualifications, not results produced by this report.

## T1: accepted task terminal follows process presentation

Production finding. poll_commands accepts the real retained terminal settlement
at coding.cpp1738–1747, then invokes process_output at1748–1754. That path also
constructs foreground and exit presentation before selecting/extracting the
CommandTaskActivity node and publishing1916/1920 at1755–1762. The reviewed task
owner requires native finished/failed/unknown capture immediately after accepted
settlement, before presentation. process_output is an arbitrary business callback;
swallowing its exceptions does not make it a bounded step or ensure it returns.
An already accepted task terminal can consequently remain unpublished while the
callback blocks, reenters product work, or terminates the process. The retained
node still has its issuer bank, but owning the bank does not publish the fact.

Extract the actual accepted node and publish its selected native terminal before
process_output or its fallible presentation construction. Preserve its held
notification request/return and once-only ownership; do not add another task map
or fresh post-settlement claim. The current task fixture tests task_activity Error
and allocation outcomes, but process_output is unset and cannot detect this order.
Use the real FIFO child/nonzero foreground settlement and an exit process_output
callback that observes the already delivered terminal task notification, or a
native callback publication/death cut comparing actual1916 before the callback.
The oracle must distinguish background running from accepted exit7 and must
fail on this source order before the correction.

## T2: child DATA refusal before pending transfer is unqualified

Oracle finding, already identified candidly by the owner. The real513-byte
restored revision/proposal test exercises successful parent and child1907.
Partial validation, stale base, disabled observation and boundary pin refusal
exercise different branches. None refuses the child1907 DATA acquisition in
context_budget between native validation/new revision creation and the moves
into pending_budget_/pending_budget_revision_ at coding.cpp1526–1527. The proposed
whole qualification therefore cannot yet claim that exact before-transfer cut.

Source inspection places the child RawPolicyTrace constructor at1525 before both
moves and before1909; its bank split happens before the DATA claim, and the
parent run catches its actual Error and emits1910 from held CONTROL. No source
defect is asserted for this arrangement. Add the direct actual-engine cut using
a native fixture boundary after parent1907/1908 and before child acquisition:
for example observe the existing business revision allocation after its actual
issue and make the inherited sticky cancellation FD ready without refusing that
business allocation. This should make the subsequent real child raw_claim_data
return interrupted/ECANCELED. Alternatively use a real stopped recorder/full
DATA wait and absolute native deadline at that same phase. Do not substitute a
standalone RawPolicyTrace constructor or manufacture an unavailable child body.
Compare the parent request's full restored revision and validation fields, one
actual parent1910 Error, no child1907/1909, no pending policy transfer/activation,
the actual turn terminal, and continued held cleanup without fencing.

## Inspected boundaries and qualification limits

The RawTurn owner outlives Running, so closed1904 precedes one1906. The explicit
Terminal guard preserves the selected branch/stage and Error. Source1902 copies
the existing source under protected DATA before retain_program;1903 separates
actual source locator, Error, allocation and unknown. Native clock fields are
copied memberwise; the fixture forwards clock_gettime and compares actual native
pairs, without using emitter output to fabricate expectations. Source/terminal
complete-body tests distinguish real oversized retention refusal from Lua error.

Policy validation stores only fields acquired by the existing parser, and
observation uses effective enabled/trigger and the existing business encoder
length. Child construction precedes pending transfer. Existing dynamic revision
DATA avoids inventing a32-byte restored bound. Protocol records copy actual row
count and conflicting call ID; the orphan oracle checks an independent literal
and full native Error. Parent refs are explicit, not nearest-event inference.

The separate context_settlement_accepted, boundary_program_accepted and
activation_applied flags drive pending retirement and once-only noexcept swaps.
The five native boundary cases distinguish nonpending/atomic accepted program,
managed rejection without accepted program, and prepublication refusal. Actual
accepted edit is used to create the rejection case; ordinary append updates the
pending expected head and would not establish that conflict. Current tests check
policy state and actual retained boundary count. They do not by themselves prove
every program/tool presentation branch or a reopen after all pin cuts.

Persistent task nodes are staged before retained attempt admission and transferred
by node handle after start; remaining claims retain the actual issuer handle.
Background release does not emit a completed task. The native exit7 test compares
actual1060 parent, native attempt/task bytes, running/failed activities, separate
notification refs and callback Error/allocation variants. Its actual129-byte
business string refusal must also remain an executed refusal in release/ASan;
this report does not replace those profile checks.

Full CONTROL/sticky cancellation uses real stopped Rec/Sup, actual exhausted
ordinary claims and the inherited cancellation endpoint. Held Context stop,
placeholder and turn records remain publishable, and the semantic stopped output
retains the literal unanswered call identity. Only one short call is tested.
The current stop bank's16*432 rejection allowance is not a general bound for
arbitrary retained append/rejection topology. Inspection found no concrete
selected cleanup route into multi-group rejection: these fresh-ID/current-cursor
stop appends suppress protection/maintenance callbacks, and native journal I/O
refusal returns directly rather than calling retain_rejection. This is therefore
not an additional asserted defect. The operation owner's separately resolved
variable-topology mechanism must not be silently claimed already implemented for
turn or Context.

Disposition: T1 source correction and genuine ordering red/green, T2 actual
before-transfer admission oracle, then owner affected recheck/profile/static.
No further turn review panel is required.

## SAME finding disposition

T1 moves the existing accepted-node extraction/native task notification before
process_output. The first ordering fixture released its FIFO child before actual
foreground entry and did not reach exit presentation; that failure was not a valid
red. The corrected real child is released only when the actual foreground owner
sets waiting. The preserved pre-fix source order, compiled in ignored context
against current unchanged interfaces, fails exactly the accepted-terminal-before-
presentation assertion (exit1). Current candidate passes the same direct fixture.
No live checkout mutation campaign or second code panel was used.

T2 now watches the actual business revision allocation after native validation,
using the native stack to distinguish it from earlier identity-issuance allocations
and allocator capacity rounding. It writes the real inherited sticky cancellation
endpoint without refusing that allocation. The actual child DATA claim fails
interrupted/ECANCELED: full restored parent revision/validation and one held1910
are exact; no child request, stage, activation or pending policy transfer occurs.
The current unpaid ordinary operation receipt then closes semantic admission;
its actual outer caller error is audit_unavailable0, distinct from the child's
Error. The held turn emits that exact failure, native clocks, and closed Running
before its terminal, with no raw fence. Operation17 owns that concrete unpaid
receipt boundary; it must update this same integration oracle when implemented.
Direct turn fixture green3.44s; supported profiles/static remain qualification.

Mac changed T1/T2 native fixture passes Release2.56s and ASan/UBSan8.48s.
Narrow tidy caught optional caller-error accesses not understood through the
fixture assertion; an explicit checked branch resolves them without changing
which actual Error/body is required. No source suppression.
