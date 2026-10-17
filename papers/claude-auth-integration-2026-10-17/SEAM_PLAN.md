# Owned Anthropic OAuth seam qualification

The existing browser login, account owner and Messages request path already
implement the intended auth behavior. Strengthen their direct deterministic
qualification before the native main-model adapter depends on them; do not
invent a product defect or a RED claim for conforming existing behavior.

Replace the success-only Anthropic fragment in `provider_oauth_test.cpp` with
private temporary fixtures that check the actual browser grant and Messages
request together: authorization URL/state/PKCE, JSON token exchange, selected
owned account, Bearer/beta/version headers, preserved application payload, and
two clock-driven refreshes proving refresh-token rotation is persisted.

Reject wrong-state, denied and empty-code callbacks before HTTP and preserve the
existing fixture credential bytes. Check 429/503 refresh cooldown with no repeated
HTTP or environment fallback, retry at the boundary, terminal denial requiring
reauthorization, and malformed/unknown refresh results blocking further use.
All credentials and environment fallbacks are synthetic test values. No real
account files, sign-in, provider requests, caches or operator settings are used.

Run only the existing `provider_oauth` target/CTest case, after coordinating the
shared Debug build. Root owns integrated review, journal and full source gate.

The first live consent completed but token exchange returned 429. The subsequent
primary-source comparison identifies two narrow compatibility differences:
Anthropic's authorize URL needs the copy-code `code=true` parameter seen in both
vendor/current Pi builders, and JSON token code/refresh POSTs should explicitly
accept JSON as current Pi does. Extend the owning fixture first, record actual
RED, then add those three request fields and run GREEN. Preserve all existing
client/scope/origin/state/PKCE/revision checks and single-attempt behavior. This
aligns request shape; it does not establish the cause of, or cure for, the live
429. Root owns one subsequent fresh consent with the rebuilt auth executable.
