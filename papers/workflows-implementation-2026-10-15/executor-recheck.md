# WorkflowExecutor — focused E1/E2 recheck

2026-10-15. Rechecked only E1 immutable payload backing and E2 host notification
progress from [the first attack](executor-code-review.md), including their new
direct tests. No build, unrelated executor attack, source/index/CMake/common-file
edits or additional assurance layer.

**E1 resolved in source.** `WorkflowExecutorPayload` has a private owning
constructor, copies acquired source bytes into its private buffer, exposes only
const views and deletes copy/move construction and assignment. The public factory
returns an opaque const shared handle; no mutable backing owner escapes. Executor
inputs and leaf outputs now use that same handle. The original mutable
`shared_ptr<ByteBuffer>` conversion history is no longer expressible through the
API. The new direct case mutates the caller's external storage and compares actual
frozen bytes; compile-time checks establish inaccessible construction/assignment.
Factory acquisition failures remain explicit `Result` failures. Reference anchors:
`include/blackbird/workflow_executor.hpp:20–43`,
`src/workflow_executor.cpp:23–34`, `payload_freezes_external_bytes` in the executor
test. Builds/tests are root-owned and were not executed by this reviewer.

**E2 resolved at the host adapter boundary.** `WorkflowHost::wake()` retries EINTR,
treats EAGAIN/EWOULDBLOCK as an already pending socket wake, and records a permanent
send error atomically. It then shuts down the receiver side, independently waking
the owner's poll; `serve()` checks the retained fault before and after poll and
raises the actual I/O error. This does not silently turn failed notification into
successful result delivery. Its header requires the host lifetime through wake
and advertises the operation as thread-safe. `failed_wake` injects one sole send
failure from another thread with no client traffic/state polling; the owner's
actual `serve` wait must observe that I/O failure. Reference anchors:
`src/workflow_host.cpp:453–471,486–488,516–524`,
`include/blackbird/workflow_host.hpp:27–30`, and `failed_wake` in the host test.

The integrated runtime binds its executor notification to this host wake in
`src/workflow_main.cpp:146–159`. The host is constructed before the runtime, so
normal reverse destruction keeps it alive until executor shutdown joins workers.
An arbitrary throwing
`Notify` still guarantees preserved completion only, as before; the root must not
claim that arbitrary callback failures supply event-driven progress. The host
adapter closes the reported gap without polling or a second result store.

No E1/E2 residual source repair identified. The complete native checks and their
outcomes remain root qualification work.
