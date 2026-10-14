# Main input owner's one plan review

2026-10-13. Reviewed the complete Main-input subplan against the actual Main
startup, perform/error handler, plain poll/read/line assembly, TUI run call and
typed UI/task APIs. Session-state and UI ownership remain distinct; no input,
restart, source edit or build was performed during this review.

The actual native widths in the plan are sound on the supported POSIX profiles:
read request52, negative return52, poll request50, maximum negative poll return50;
their banks precede the actual syscalls. Preserve errno immediately after each
call before emission, including EINTR. No extra timer or read is necessary.
Immutable startup1000 references and opt-out pre-runtime inspection/discovery are
the correct Main origin boundaries. Three concrete plan corrections remain.

## MI1 — the workflow fallback is an effectful condition, not a branch body

`src/main.cpp:833–838` executes the default-workflow read and
`engine.operator_turn(...)` while evaluating an `else if` condition. A scheme
that prepays/emits4205 only inside each entered branch body, as currently stated,
would pay **after** these effects. Failed workflow read, rejected selection or
actual Lua execution may therefore have no reached branch record, or encounter
the raw branch grant only after work ran. Unknown slash also evaluates this
condition before reaching its display branch.

Give this existing fallback its own reached tag before evaluating its condition.
Use an enclosing final `else` containing the same original conditional, without
changing whether the workflow is read for slash input or whether unknown slash
is displayed. Distinct postcondition outcome is allowed only from the actual
returned bool; do not classify a command in a new formatter/parser. The paid
dispatch terminal already records thrown failures.

Direct oracles: invalid workflow file, a real matched workflow with one native
effect, and unmatched slash. Require the fallback tag before the reached read/
selection/effect, its actual dispatch outcome, and absence of default workflow
read for slash input. One-byte-short branch admission must prevent the effectful
condition itself, not merely its later UI publication.

## MI2 — instrumentation failure must not initiate backstop/new work

The existing interactive Error handler at `src/main.cpp:852–872` can run backstop
for a capacity/audit-related Error when the Root remains live, the operation was
not interrupted and cancellation is false. New raw claims in input/dispatch or
selected session/file helpers introduce precisely such failures. The plan
currently promises the old error presentation/backstop without distinguishing
ordinary business failure from instrumentation refusal. Showing the old error
is compatible with behavior; starting another effectful recovery after recording
has refused admission is not.

Carry the same actual instrumentation-origin indication required by session-state
SS1 through the selected Main contexts. The handler must not run backstop or
continue accepting new selected work after that failure; propagate the actual
failure after any permissible existing error presentation. Do not guess origin
from ErrorCode alone, and do not normalize a native business read failure into
instrumentation failure. `refresh_info` at `src/main.cpp:451–459` has its own
catch-all: it also needs to rethrow selected instrumentation failure rather than
turn it into the normal advisory snapshot warning. Ordinary snapshot/business
errors retain their current warning behavior.

Direct oracle: a genuine raw pre-effect refusal with a configured fixture
backstop must produce zero backstop/provider/child effects and no next accepted
prompt. Separately, an ordinary eligible business error must still take its
existing backstop path. No new live credentials or provider accounts are needed.

## MI3 — pin the reached-branch protocol and the pre-dispatch controls

4205 is declared only as `u8 kind`, with no complete table. The public Main
chain at `src/main.cpp:525–838` contains grouped conditions and internal branches
such as runs configure/archive/read, variables/correlate and Beads select/configure.
The native protocol needs a written static tag map identifying precisely which
existing boundary each tag records before coding and before independent fixtures
can assert literal bytes. Use existing outer branches where that is the selected
site; do not invent a second classifier or a counter. State explicitly whether
inner existing branches are separate native sites or remain outside this
4205 declaration.

Also state the coverage of `/quit`, `/exit`, `/paste` and `/send`, which are
handled before `perform` at `src/main.cpp:1115–1125`. Native423x already retains
their actual transport bytes, but those controls never enter the declared4203
dispatch. Do not fabricate a dispatch terminal for them. If the unit includes
their business selection, give the actual existing reached controls their own
declared typed site/finite prepayment; otherwise record that precise boundary.
The existing per-read pending-line limit is 1MiB; `/paste` assembly has no such
total bound in the current source. Do not silently introduce one while preparing
source credits. Use the actual accumulated length and checked envelope arithmetic.

Direct expected-byte oracles should assert the pinned tag and reached native
operation, not merely that4205 exists. Split lines, unterminated EOF and paste
must compare both original native read bytes and the actual assembled prompt
without manufacturing sentinel dispatches.

These are the sole findings from this one plan review. Resolve the exact finite
interfaces and error propagation, then implement the same unit and its direct
oracles. No review of a review or workload qualification is requested.
