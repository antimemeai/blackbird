# WorkflowExecutor — layer-one independent code attack

2026-10-15. Read executor header, implementation, complete tests, executor pre-code
plan and root `PLAN.md` in the isolated workflow checkout. No build, source/index,
CMake or shared-file changes. This is the first attack; one finding-focused
recheck follows owner remediation.

## E1 — P1: const shared ownership does not freeze the byte backing

**Anchors:** `include/blackbird/workflow_executor.hpp:20–24,34–38`;
`src/workflow_executor.cpp:257,271,171–178`; existing
`include/blackbird/foundation.hpp`'s `ByteBuffer` declaration.

The executor promises immutable source/context/input/output through
`std::shared_ptr<const ByteBuffer>`. `ByteBuffer` has implicitly generated public
copy/move assignment. A const shared pointer can alias an object also held by a
mutable shared pointer; it does not make that object immutable.

**Concrete history:** construct `auto backing = make_shared<ByteBuffer>(...)`;
implicitly convert it into `WorkflowExecutorInput::input`; retain admission and
start the leaf. The owner then assigns another `ByteBuffer` through `*backing`.
The leaf reads different bytes from those admitted, or a previously borrowed
`ByteView` points into the destroyed vector. Concurrent access is a data race and
can become use-after-free. The analogous leaf-returned mutable alias can change
the output while its completion waits for successful publication.

The current tests' helper uses `make_shared<const ByteBuffer>`, avoiding the fault
in fixtures while not establishing the API's stated guarantee. Comparing output
pointer identity does not establish stable admitted/returned bytes.

**Minimal repair:** use an opaque immutable bytes handle whose factory owns a
fresh backing and never exposes mutable ownership, or freeze/copy bytes into
executor-owned truly const backing before admission and before result publication.
The result path must handle any required allocation without losing the completion
or fabricating failure/nonexecution. An opaque owned immutable factory can avoid
the extra result copy/allocation. Merely changing an accessor's const qualification
or documenting that `ByteBuffer::view` is const does not close the alias.

**Direct oracle:** retain a mutable source alias, admit the task, mutate that
alias after a gate, and prove actual leaf bytes still equal admitted originals.
Return an output with a mutable alias, mutate it before drain/retry, and prove
the preserved completion remains the original bytes. Exercise the public API,
not a helper that accidentally constructs only immutable objects.

## E2 — P2: failed wake preserves storage but supplies no event-driven progress

**Anchors:** `src/workflow_executor.cpp:77–87,184`;
`include/blackbird/workflow_executor.hpp:80–83`;
`tests/workflow_executor_test.cpp`, `failed_wake_preserves_completion`;
root `PLAN.md`, host polling sockets and completion pipe without busy polling.

**Concrete history:** the owner's event loop waits for socket/pipe readiness. Its
only leaf completes; `Notify` throws before signaling. `wake_owner` suppresses
the exception and leaves no retry/level-triggered indication outside the in-memory
completed list. No client sends another request. The result is preserved but the
owner can sleep indefinitely, so successors never advance. The fixture manually
polls `state()`, calls shutdown and drains, demonstrating storage retention rather
than event-driven delivery.

The executor header correctly promises only preservation on a throwing notifier.
This is therefore an integration-contract gap, not a claim that suppressing a
notification exception corrupts the result. The root's event-driven host cannot
infer progress from that narrower guarantee.

**Minimal repair:** make the notification integration a qualified persistent/
level-triggered wake mechanism with explicit failure behavior. For example, a
nonblocking wake-pipe adapter must distinguish EINTR retry, EAGAIN with already
pending readability, and permanent failure that makes the owner observe a fault/
hangup. If arbitrary failing callbacks remain supported, document preservation
only and require a separate nonbusy fallback progress/failure route in the host.
Also state that multiple workers can invoke `Notify` concurrently; its target
must be thread-safe and outlive all workers. No callback may call owner controls
or destroy the executor.

**Direct oracle:** with no client traffic or state polling, complete one leaf and
inject the supported wake failure. The actual owner wait must either wake to
drain the exact completion or observe the declared fatal channel condition.
Retain the existing throw-preserves-result test for its narrower claim.

## Boundaries checked without additional established defects

The ready/completion lists and counts are mutex-protected. Worker completion
becomes immutable before owner publication; cancel/shutdown do not subsequently
rewrite a completed result. Slot generations change only on acknowledged release,
while the worker's shared task keeps its old data alive through notification.
Failed/throwing admission never queues the task. Failed/throwing publication
leaves its slot/completion available. Queued cancellation removes the task before
workers can take it; running cancellation requests stop without changing a late
success into cancellation. Stop callbacks and publish/retain run outside the pool
mutex; owner/reentry guards reject nested controls. Shutdown actually joins workers
and preserves drainable completions; noncooperative leaves can delay it honestly.

No additional concrete race/list corruption was established in these paths.
Destructor/thread/lifetime discipline remains a caller contract: public controls
belong to construction thread, borrowed notify resources outlive shutdown, and
native leaves must not wait for descendant work on this leaf pool. This unit does
not implement coordinator continuation parking, process containment, durable
replay or external cancellation guarantees. The root plan correctly leaves those
to the scheduler/owner/adapters. No third assurance layer is requested.
