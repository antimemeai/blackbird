# WorkflowRuntime focused code recheck

2026-10-15. One focused recheck of original R1–R6 schedules after root's remedies.
No new review layer, join-family review, edits to runtime or builds in this lane.

R2–R5 now address the original structural failures: completion retention failures
close admission, stop/join native workers and retain the undrained completion;
ordinary controls retain intent before stop, with emergency shutdown on failed
intent retention; destruction attempts healthy retained drain before owners end;
failed release is inspectable as executor-absent startup/retention debt rather than
a live phantom worker. Terminal identities are preissued and protected journal
space covers their atomic cell/observation batch. Recovered work has no revived
worker. The following original-finding residuals remain on source inspected here.

## R1 residual: bounded capture gap needs explicit byte-coverage metadata

The shared Output now owns LocalTools/result before release and preserves already
captured originals across owner encoding/append failures. Selected 1MiB native
original bounds and 4MiB result allowance avoid the original 9MiB doubled-result
failure schedule. This is a meaningful repair.

Allocation failure between acquiring a local string and LocalTools::capture's
successful ownership insertion can still destroy those bytes on worker unwind.
That is within the explicitly acknowledged initial native facade limitation, not
a reason to demand later station custody now. However its retained result currently
contains an originals array plus unknown disposition without an explicit
byte-coverage gap. Minimal remedy: emit `originals_complete=false` or an equivalent
explicit capture-gap marker whenever output.error or completion.exception exists.
Do not infer universal completeness from the success path; full lossless handoff
remains later machinery. The original report required an explicit incomplete/gap
state in this case, rather than a generic preserved-output claim.

The owner exception conversion also still collapses available std::exception
information into `opaque native exception`. Minimal remedy: catch std::exception
before the opaque catch and retain a bounded what() diagnostic, marking truncation
if bounded. This metadata is owner formatting; preserve the executor exception_ptr
until settlement acknowledgement as presently done.

## R6 residual: direct integration fault oracles still pending

The inspected runtime direct test still establishes echo identity/result/reopen.
The new real CLI process oracle independently counts a gated exec effect, verifies
no replay/read-only recovered uncertainty, client detach and actual signal shutdown;
the host unit exercises failed sole wake. These expand effect/lifecycle evidence.
They do not yet inject owner terminal retention failure, partial native output,
or post-effect allocation failure through WorkflowRuntime. Root's planned bounded
result-credit/fault cases remain necessary evidence for the original R1/R2/R4
claims. Tests need only cover this bounded native facade, not universal losslessness
or all later workflow families. No additional review-of-tests layer is requested.

Root applied the listed minimal R1 corrections after this focused report:
`originals_complete=false` on error/exception and bounded std::exception what()
diagnostics (4096-byte bound). No further review layer is requested. R6 direct
fault/partial-output oracle implementation and execution remain root-owned.
