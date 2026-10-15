# WorkflowRuntime integration code attack

2026-10-15. One independent code attack of `workflow_runtime.cpp` and its direct
test, with supporting executor/LocalTools source inspection. No product edits or
builds/tests in this review lane. Root fixes these findings, then one focused
recheck/fix; no third layer. Findings below concern the currently advertised
native task path, not a requirement to implement all later workflow families.

## R1 — P1: result capacity/representation can discard acquired originals after effects

**Source:** `src/workflow_runtime.cpp:232–250`, `src/workflow_runtime.cpp:284–294`;
`src/tools.cpp:225–228`, `src/tools.cpp:336–355`.

**Concrete schedule:** Read a 9 MiB ordinary ASCII file. LocalTools retains its
9 MiB original and returns approximately 9 MiB presented content. Runtime copies
the original into `{value,originals}` and encodes that combined value with the
16 MiB ValueLimits, so capacity fails after the read. A successful command with
similar output has the same issue. An allocation failure after a successful
write/edit likewise occurs after the effect. Executor preserves an exception_ptr,
but the LocalTools stack and its acquired originals are destroyed. Tick retains
only an error/opaque exception and can no longer recover those originals from
the completion. Optional raw capture is not a semantic result reference, and the
direct runtime test runs with capture=nullptr. Native std::exception messages
also become the fixed text `opaque native exception` rather than owner-read
diagnostics.

**Minimal owning remedy:** Acquire the bounded result/original handoff resources
before release. Preserve originals in separately owned buffers/references as they
are acquired, independent of later Value construction/formatting. Result cells
should reference those retained bytes, rather than require two full copies in
one bounded metadata packet. A bounded terminal failure/unknown record must remain
funded if ordinary result retention fails. Owner conversion should preserve known
native Error and available std::exception details; opaque values may remain
explicitly opaque. Do not report this path's outputs preserved merely because
the executor completion slot is reserved.

**Direct case:** A file/output above half the metadata limit plus a post-effect
allocation failure, with exact independently known original bytes and effect
count. Require either exact retained originals or explicit incomplete/gap state,
never a successful generic capture claim.

## R2 — P1: retention failure does not stop admission/execution, and blocks stop requests

**Source:** `src/workflow_runtime.cpp:276–323`, `src/workflow_runtime.cpp:341–370`.

**Concrete schedule:** Worker A completes; its completion transaction fails due
to disk/capture capacity or writer failure. Tick throws through `unwrap(result)`
without setting `stopped`, requesting all worker stops or holding a sticky runtime
failure. Already queued workers B/C can continue to perform ordinary effects.
The next `request({op='cancel'...})` or `request({op='shutdown'})` calls tick before
examining the operation and throws at A again, so the ordinary control endpoint
cannot reach the mandatory stop path. The executor correctly preserves A, but
the runtime does not contain the effect-producing population.

**Minimal remedy:** Latch the first retention failure and close new admission;
request native stops independently of writer health. Dispatch mandatory shutdown/
cancel controls before an ordinary drain that can throw. Keep pending completions
and exact capture ownership visible for retry/reconciliation; ordinary unrelated
requests must not reset the sticky failure. The caller's final cleanup path must
still join every owned worker before log/raw owners disappear.

**Direct case:** Inject failure retaining A while B is gated before a visible
effect; send stop through the real request path and verify B is stopped or its
actually released/unknown outcome remains explicit, with no new admissions.

## R3 — P1: cancellation controls have effects before retained control intent

**Source:** `src/workflow_runtime.cpp:357–367`, `src/workflow_runtime.cpp:334–339`.

**Concrete schedule:** Cancel a running `exec`. Runtime calls executor.cancel,
which requests stop and lets LocalTools/Child send termination controls. Only the
eventual terminal completion stores `cancel_requested=true`. Crash before that
completion loses the cancellation request/cause/identity. There is no retained
control admission at all, although later signals/cancellation have the same
effect-boundary requirement as launch. Ordinary explicit shutdown also starts
stop actions without a recorded intent when storage is healthy.

**Minimal remedy:** Give ordinary controls stable request identities and retain
their exact scope/task/revision/cause before changing the native stop token.
Record accepted versus prevented-invocation/requested-running/already-completed
observations separately. Mandatory emergency cleanup remains usable on an
unwritable ledger, with the unrecorded interval/failure exposed rather than
pretending it was an ordinary acknowledged control.

## R4 — P1: destructor joins but drops undrained completion/original ownership

**Source:** default `WorkflowRuntime::~WorkflowRuntime` at line331; member order
in Impl; WorkflowExecutor destructor/stop_all.

**Concrete schedule:** A valid task has completed into its reserved executor slot,
but no later request/tick occurs. Runtime scope exits on a host transport error
or ordinary caller unwinding without an explicit successful shutdown. The default
destructor invokes executor destruction, joins workers and destroys every queued
completion without retaining the result/originals or terminal observation, even
when the audit is writable. It leaves an admitted pending/released attempt and
loses process-owned result bytes. The unused `closed` field does not enforce an
actual completed handoff/drain.

**Minimal remedy:** Make runtime destruction/outer-host ownership a mandatory
two-phase stop, join and owner drain while log/raw capture are still alive. The
destructor can perform a bounded best-effort version with explicit failure state,
but cannot claim a normal finished lifecycle while simply discarding healthy
retention debt. If the API requires explicit close instead, enforce that
precondition and give the executable an unconditional cleanup guard on every
exit path; retain the actual failure/unknown disposition when drain cannot work.

**Direct case:** Complete a gated child and leave scope without another request;
reopen and check exact output/terminal state. Repeat through an exception exit.

## R5 — P2: failed release leaves a process-local phantom pending task

**Source:** admission callback at `src/workflow_runtime.cpp:256–272`.

**Concrete schedule:** Cell/native admission commits and task indices are inserted.
root.dispatch then fails before allowing the executor queue, or its receipt
record fails. RetainAdmission returns failure and executor releases its reserved
slot without invoking the leaf, correctly. Runtime leaves the task cell pending,
no handle and no completion source. An identical start returns that pending row
forever. This can occur without process restart and does not require replay.

**Minimal remedy:** Preserve an explicit startup/release debt state. When the
executor guarantees invocation was prevented, record that actual fact using the
protected terminal path; otherwise expose the release uncertainty and close
affected admission. Do not silently delete the committed admission or retry it.
An inspectable pending task must identify the actual remaining owner/obligation.

## R6 — P2: existing echo-only test does not attack the integrated effect boundary

**Source:** `tests/workflow_runtime_test.cpp`.

The current test establishes exact echo output, duplicate logical identity and
settled reopen, which are useful. It does not count a native external effect,
exercise cancellation/partial output, crash around release/result, result pressure
or an unwritable writer. It therefore cannot establish the broader native tool
and shutdown/recovery claims. Add the direct cases attached to R1–R5 as repairs
to this existing oracle family, with independent child/file effects and bytes.
No extra review-of-tests or new certification loop is requested.

## Correct mechanisms and deliberate limits

- The normal successful release order is right: executor reserves a slot, the
  callback commits cell+typed admission, root.dispatch consumes release, and only
  callback success permits worker queueing. No leaf executes before retention.
- Task-map node extraction preserves the mapped object's address across insertion;
  the staged attempt pointer is not dangling merely because its node is moved.
- Workers bind independent raw lanes and do not write RetainedState/ContextStore.
- Recovered rows have no recreated executor handles, so no automatic replay occurs.
  F4 durable continuation/recovery is not implemented by this source. Its pending
  display should explicitly identify recovered/no-live-owner state so clients do
  not wait for an executor that does not exist.
- LocalTools exec owns only its declared child/group cleanup. Its own source says
  escaped descendants are uncontained. A successful exit should not be advertised
  as universal process containment, workspace release or known remote disposition.
- Complete generation manifests, general tool-using providers, scope resources and
  durable joins remain later units. This review does not make them prerequisites
  for a correctly bounded and honestly reported first native path.
