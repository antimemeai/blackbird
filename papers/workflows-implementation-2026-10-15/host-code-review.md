# F1 host transport: independent code attack

2026-10-15. First independent attack of `workflow_host.cpp`, public header, `workflow_host_test.cpp` and `host-plan.md`, including newly added stale endpoint cleanup. Read-only; no product calls/builds/index edits. Root owns integration and fixes, followed by one focused recheck.

## H1 — High: crash before chmod leaves an owned socket that recovery refuses

The constructor binds the pathname before changing its permissions to 0600. Under the ordinary 0022 umask the just-bound socket can have mode 0755. A kill after bind and before `fchmodat` leaves exactly that owned socket behind. [`remove_stale_endpoint()`](../../src/workflow_host.cpp:435) requires `(st_mode & 077) == 0` before attempting its refused-connection check, so it rejects the host's own legitimate crash residue with invalid_identity. The new owner cannot restart through the intended stale cleanup path despite holding the exclusive journal owner.

**Minimal repair:** either create the socket with private permissions from its first pathname visibility under a qualified process-wide mechanism, or permit cleanup of a same-user socket in the already verified private session directory irrespective of its pre-chmod mode, while still requiring refused connection, unchanged inode and actual exclusive journal ownership. Do not widen active-client endpoint trust merely to repair stale cleanup.

**Direct checks:** bind a Unix socket under umask 0022, close its listener without chmod, and prove exclusive-owner stale cleanup removes it. Also check mode0600 stale success, live socket busy refusal, absent endpoint false and non-socket/symlink refusal. Current tests exercise constructor collision and final cleanup but never call the new stale cleanup method.

## H2 — Medium: inode check and unlink are separate operations

[`remove_stale_endpoint()`](../../src/workflow_host.cpp:443) checks the current inode, then separately calls `unlinkat`. `unlink_owned()` has the same check-then-unlink shape. A same-user process can replace the pathname between these calls, causing cleanup to remove a replacement file/socket. The existing replacement-inode test replaces before the check, so it does not establish the stronger header/subplan claim that cleanup unlinks only the observed owned inode under concurrent replacement.

**Concrete schedule:** original stale socket passes the second `fstatat`; another process renames it away and writes `workflow.sock` as an operator file; cleanup's `unlinkat` deletes the new file. The journal owner lock serializes cooperating hosts, but does not itself serialize arbitrary filesystem operations by tools or the operator.

**Minimal repair:** state and enforce an exclusive endpoint-directory mutation contract for all supported participants, limiting the inode-preservation claim accordingly, or use a platform-qualified claim/quarantine-and-verify cleanup procedure that never deletes a mismatched claimed inode and leaves/restores it visibly when pathname races occur. Do not claim an atomic inode-conditional unlink from two syscalls. Also keep the held directory identity consistent with the pathname used for bind/connect if directory relocation/replacement is supported; otherwise name that stable-directory requirement.

**Direct check:** interpose the relevant filesystem boundary to replace the endpoint after successful identity observation and before unlink, then verify replacement preservation under the selected supported contract. If arbitrary same-user mutation is explicitly excluded, a new test should instead verify the enforced cooperating namespace rule and the claim should be narrowed; no speculative security subsystem is needed.

## Boundaries that withstand this attack

- Partial requests do not dispatch before the bounded body is complete. Existing forked-client tests cover binary bytes, repeated detach/reconnect, independent progress beside a partial sender, malformed lengths and exact native errors.
- Client memory is byte/count bounded at frame admission; slow clients have absolute finite deadlines. Synchronous business dispatch must remain short owner work; native blocking effects belong in the leaf executor.
- Dispatch exceptions produce an explicit failure/unknown response rather than success. Transport reply failure cannot prove a side effect did not happen; root submission identity supplies retry semantics.
- `wake()` retries EINTR, treats EAGAIN as an already-readable notification, and records other send failures before shutting down the receiver to wake poll independently. The interposed-send test directly checks the sole failed wake faults the owner. No worker enters ledger/Lua.
- Same-user/private-directory checks, close-on-exec descriptors serialized against owned spawn, process/thread owner checks and child-process cleanup refusal are concrete mechanisms. The host itself is transport and does not fabricate journal ownership or cross-host takeover.
- Stop mode drains existing replies and closes incomplete submissions without converting them to commands. Ordinary UI/client disconnect does not stop the owner.

## Recheck scope

After fixes, inspect only pre-chmod crash cleanup and the exact endpoint namespace/inode cleanup guarantee plus their direct cases. Root owns all builds and host-source/test edits. No second broad host attack or third assurance layer.
