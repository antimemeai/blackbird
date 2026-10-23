# Native application host tools: implementation review

2026-10-23. One independent source review of the version-1 unit in
`docs/APPLICATION_HOST_TOOLS.md`; base `a94d094`, proposal `8b0066f`. Reviewer
inspected the actual native catalog, CodingEngine, application connection and
new direct tests. Oxcart separately accepts the proposed wire vocabulary and
owns browser operations/its adapter. No dependency adopted.

The first attempted Kimi review did not run: CLI 2.1.1 returned subscription
HTTP403 (`provider.auth_error`, no current Kimi Code access). Original invocation,
result and stderr are retained in `context/kimi-review/run-5cvwhakf/`; no access
repair, account change or review-completed claim. One independent Codex source
review replaced it, as the doctrine's implementation review. It did not execute
tests or edit source. The owning lane executes the direct cases below.

## Findings and direct repairs

1. A valid success frame followed immediately by an oversized/malformed frame
   could be lost by the still-draining pump. Although its bytes survived, the
   outstanding callback threw and the known successful operation became unknown.
   The direct case writes the success frame and bad header together, then checks
   the first native attempt's terminal success. Stop draining when a result has
   been accepted; settle it before reading another frame.
2. A schema-valid outgoing call larger than the 4 MiB control envelope could fit
   native operation admission. Encoding refused before any send, yet generic
   capacity handling marked it unknown and stopped the session. Direct case:
   a 4 MiB string argument must produce a definite local not-started refusal,
   no host call, and a corrected subsequent call must complete. Return a native
   error result for the local frame refusal, preserving unknown for actual
   transport uncertainty. This does not cap captured browser files.
3. Integer validation inspected formatted text for `.eE`, rejecting exact
   integral decimal/exponent values such as `1e17` despite advertising an integer
   provider schema. Validate Number's native representation; normalized Decimal
   exponent >=0 is integral, finite floating values must equal their truncation.
   Test exact large positive/negative and above-uint64 integers, while continuing
   to reject fractions and coercion.

Each direct regression is run RED before its owning source fix. These fixes and
one affected recheck exhaust the two-layer hardening scope. No rereview, new
tribunal or assurance layer is requested. Local Debug/ASan and the required
source hook qualify the changed implementation; results and unavailable host
checks are recorded with delivery, not inferred from this review.
