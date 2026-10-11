# One turn-owner plan attack

2026-10-11. Read the complete turn-owner subplan against actual CodingEngine::turn,
protect_workflow, context_budget/budget_view/constructor restoration,
validate_context_budget/budget_number, validate_protocol, TaskRun, the three
private operation body call sites, exec launch/wait and poll_commands settlement.
Also read ContextStore's real finish/publication/history paths, TaskState's native
ID validation, RawThreadBinding/current-handle/held claims and retained_append
prepayment. This is this conceptual owner's ONE whole-plan attack. No source,
compiler, test or extra review panel was introduced by this review.

## Finding 1: proposal's child view uses the effective revision

The plan reserves82 DATA for the proposal's child1907 because the newly issued
revision has32 bytes. Actual context_budget stages pending_budget_revision_,
then calls budget_view. That view uses budget_revision_, the still effective
revision, which the constructor copies from restored program packets without a
32-byte bound. The child therefore needs50+actual effective revision bytes, not
50+the newly issued pending revision. At a restored revision longer than32 the
planned split cannot publish the child's actual request; pending proposal state
has already transferred by then.

Before the proposal's pending-state transfer, hold the checked dynamic child
request amount using the actual effective revision span. Keep staged1909's80
CONTROL maximum: that record describes the newly issued pending ID. Do not
replace the effective revision in the child view with the pending revision or
add a restore-time32-byte restriction. Neither is the existing business result.

Direct oracle: append a legitimate context-budget-effective-v1 program packet
with a valid policy and a distinct513-byte revision, construct the real engine,
then make a valid proposal during a real turn. Compare both full1907 revision
bodies/parents: proposal and child view still reference that exact effective
revision;1909 references the different actual issued32-byte pending revision.
A grant too small for the child's checked actual body must refuse before pending
transfer, with no staged state/event. A sufficient grant must not invent a
capacity failure after staging. The usual32-byte-only fixture misses this.

## Finding 2: policy validation and observation have different acquired fields

The one listed1908 shape unconditionally contains input_bytes and triggered.
validate_context_budget, used by propose/restore, does not compute either. It
checks the native enabled representation and parses proposed target/trigger;
input encoding occurs later. budget_view then computes triggered against the
still effective policy. Combining that child result with proposed thresholds
would describe a policy state the business code never evaluated. Filling zero
input_bytes or false triggered merely to satisfy the listed fixed shape would
also invent measurements.

Specify1908 native bodies by the actual1907 method. Propose/restore validation
copies its actual enabled/parsed threshold fields, without input/triggered.
Read/observe copies budget_view's actual enabled, input bytes and triggered;
trigger is absent when short-circuited and target is absent because that view
never parses it. The actual method in the parent request selects the body;
there is no need for another metric encoder, traversal or native counter.
The existing68 allowance conservatively fits these method-specific bodies,
so119/318 need not grow for this correction.

Do not rely only on a successful helper return to obtain parsed values. Valid
thresholds followed by empty-reason refusal already acquire native values but
never return a validation result. Fill a native out object/owner guard as the
existing business parsing succeeds, and publish the actually acquired fields
from held credit on the Error path too. If enabled cannot be acquired as bool,
there is no invented boolean field. This needs no permanent cache or second
Number.text/from_chars pass.

Direct oracles: effective enabled=true/trigger100, proposed enabled=false/
trigger900/target450, and independently known input bytes above100. Validation
contains the proposed fields without input/triggered; child observation contains
actual effective triggered=true, never a hybrid with the proposed thresholds.
After activation, a disabled view has no parsed trigger or target. An otherwise
valid proposal with empty reason emits actual parsed threshold fields and its
real invalid_range terminal, with no pending transfer or successful projection.
Check original native field bodies, not the presentation Value reconstructed by
a fixture.

## Required integration of the already known Context acceptance finding

The [Context plan attack](2026-10-11-context-owner-raw-plan-attack.md) already
found the acceptance distinction. The turn paragraph's current "accepted Context
settlement" and singular "actual acceptance flag" must name both concrete flags
before source release. Pending-settlement acceptance retires the managed child;
it cannot authorize pending program/tool/policy swaps. A separate actual
program-boundary acceptance flag authorizes those already prepared noexcept
swaps, including when history pinning subsequently throws.

Perform all accepted native swaps even on that later failure, once, before any
fallible presentation or emission helper can interrupt activation. Preserve
separate flags for actual program acceptance, pending settlement and performed
activation; no parsing of an accepted(result) projection or inferred cursor.
Use the Context attack's genuine four cuts: managed rejection accepted before
program append/pin failure; atomic context+program accepted then pin failure;
no-pending program accepted then pin failure; and prepublication refusal. Compare
accepted retained packets with live native engine policy/tools/program, then
reopen without another semantic append or dispatch. This is integration of that
known finding, not another Context review or a new discovery claim.

## Other claims inspected

Root body maxima131/89/65/72/81/108 match the selected native fields. Context
head is actually validated/reconstructed from issued32-byte application IDs;
activation revisions describe newly issued IDs. The turn's six finite states,
three activation records and two selected top-level placeholder request/return
pairs give1346; the separate Context9198 bank gives10544. Source1902 DATA is
separately paid before its copy. Failure before workflow begin must not invoke
finish(false); a live root owner outlives Running's flag/continuation cleanup.

Context9198 already includes actual semantic2286 and16*432 retained regions,
including possible issuer refills/rejection paths. Optional checkpoint cannot
spend an unpaid read/save after accepted stop append. The root loans these
actual same-producer banks explicitly; no canceled fresh admission or metrics
TLS parent guess is proposed. Source retention has a distinct request/return
before the VM source association;1505 does not replace it.

Protocol99/75 arithmetic fits the parented/unparented native request/terminal.
The conflicting call ID is the existing branch's string; no second validation
walk is needed. Its exact DATA admission failure is an actual admission error,
not a fabricated valid/conflict record. Placeholder count is the actual returned
array size and says nothing about remote effect completion.

Task IDs really are canonical t+positive-u64, with maximum21 bytes. Native
binding85 plus two activity/notification cycles132 gives349. Stage task objects,
map node and projections before accepted attempt append; do not allocate the
old TaskRun strings or map node after acceptance. There are exactly three
private operation-body call sites for the explicit activity pointer change.

A persistent job's132 terminal bank and issuer handle must move into its staged
node before wait can call poll_commands. Accepted command settlement selects the
same node and native disposition; background release is not a process terminal.
Move/extract the node into local ownership before erasure/presentation, keeping
its task strings, held notification credit and original producer handle alive.
The current persistent callback can throw after acceptance; the proposed guarded
notification should record actual Error/allocation/unknown and swallow exactly
as nonpersistent TaskRun does. One final native activity branch, no callback
retry, no fresh claim after accepted terminal and no producer use after lifetime.
RawThreadHandle retains owned runtime state; borrowed producers still need their
stated external lifetime, and claims must die before handles.

## Disposition

Resolve the two policy findings and explicitly integrate the known separate
Context acceptance flags in the concrete turn plan. Then genuine red/source/
green and this unit's ONE code/oracle attack proceed after root's checkpoint.
The review asserts no further defect in the inspected finite banks, task handoff,
protocol placement or source/turn lifetime. Provider adapter source remains held.

The turn subplan now resolves both policy findings: child1907 pays for the
actual effective revision before pending transfer;1908 has distinct validation
and observation bodies with guarded partial native fields on failure. It also
names all three Context integration flags above. The specified513-byte restored
revision and contrasting-policy oracles remain implementation work, alongside
the actual history-pin cuts. This records plan resolution, not source completion.
