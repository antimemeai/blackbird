# Integrated native workflow runtime direct tests

2026-10-15. Written before expanding `tests/workflow_runtime_test.cpp`. Own only that test and this plan; root owns runtime implementation, all builds, shared files and commits.

Repair the baseline fixture to create mode0700 session directories using `mkdtemp`, and label create/start/settle/shutdown/reopen stages in failures. Preserve exact result/submission idempotency and ordinary reopen checks.

Use this owned test executable as a native child fixture. Each child records one launch and a per-task ready marker after opening its named FIFO, writes a known output prefix, then blocks on a parent-controlled FIFO gate. Two ready children before either gate opens directly establish simultaneous native executions; their markers and retained invocation/attempt IDs establish effects. Cancel a queued third child and verify it never writes its marker or launch. Release the two running children, check exact full originals and one launch each, then reopen without repeating effects.

Exercise actual local read/write/edit through runtime tasks: bounded presentation against full original bytes, before/proposed write/edit originals, exact file effects, duplicate-edit conflict without a write, input size refusal before launch and original size refusal rather than truncation. Run cancellation after an owned child emits its prefix; inspect retained cancellation intent and terminal attempt observation, exact partial original and single external launch.

Verify healthy scope exit without explicit runtime shutdown retains completed output: wait on executor notification without pumping the owner, let the runtime destructor join/drain, and inspect/reopen the resulting task. Separately protect one running task's actual result credit, consume ordinary journal headroom with bounded original-source appends until capacity refuses without mutation, then release the child and require its atomic result plus native terminal observation to commit using reserved capacity.

Create pending and already-completed joins through runtime `join`/`join-read`, preserve immutable selected task-cell/revision references despite later completions, reject conflicting reuse of join identity, and reopen the same selection from native records. Gates and retained state are direct oracles; bounded fixture deadlines only prevent a broken test hanging. No provider/account calls, benchmark claims, unrelated tests or additional assurance layer. Root runs these source-grounded cases after integration.

Before the additional writer-fault case, extend this same unit with a small native
directory/file forwarding decorator that fails synchronization on demand. Keep
two native children gated (one after emitting a large prefix), cancel a separate
queued task to produce a completed slot without returning a physical worker,
and keep another task queued; inject the result append failure before draining.
This schedule avoids a race in which a normally completed child would release
its worker to the queued task before the owner sees its result. Require mandatory
population shutdown, no queued effect, closed admission, recovery-required state,
and unchanged completion debt through repeated failed cancel/shutdown controls.
The public failed-writer facade cannot expose an uncommitted original, so this
case asserts preservation of unresolved debt rather than claiming a direct byte
oracle for the second child's still-uncommitted partial output. Exact partial
bytes are covered by the healthy-writer cancellation case and executor retention
tests. No generalized fault framework or product API change is needed.
