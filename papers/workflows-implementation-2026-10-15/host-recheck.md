# Host H1/H2 focused recheck

Scope: the two findings in `host-code-review.md`, their direct regressions, and
the explicit ownership contract. Source inspection only; root owns builds. No
new assurance layer or unrelated host review.

* **H1 resolved.** `src/workflow_host.cpp:426` now accepts any mode on a socket
  owned by the effective uid inside the separately checked private directory.
  Socket type, refused connection, and matching device/inode remain required.
  Thus the bind-before-chmod crash seam no longer strands an owned mode0755
  socket. `tests/workflow_host_test.cpp:229` directly checks stale0755 and
  stale0600 removal, absence, and active-socket refusal with inode preservation.
  The existing path cases also invoke cleanup against symlinks and regular files
  and verify that neither is removed.
* **H2 resolved by an honest, narrower authority contract.**
  `include/blackbird/workflow_host.hpp:14` requires the retained journal owner to
  hold exclusive endpoint namespace authority through destruction.
  `include/blackbird/workflow_host.hpp:37` explicitly excludes concurrent
  same-user mutation and says the inode recheck is not atomic unlink-if-inode.
  `papers/workflows-implementation-2026-10-15/host-plan.md:35` distinguishes this
  caller precondition from permissions and advisory journal locking. The source
  still detects replacements preceding its final recheck; it no longer claims
  to protect the final check/unlink gap from an actor violating that precondition.

No remaining H1/H2 defect found. This does not establish protection against an
arbitrary same-user actor mutating an exclusively owned namespace, which is
explicitly outside the API contract.
