# Owned Anthropic OAuth seam results

Initial qualification, 2026-10-17: replaced the success-only Anthropic fragment in
`tests/provider_oauth_test.cpp` with direct private-fixture qualification. Existing
production behavior conforms; no auth implementation change or RED claim.

The browser flow checks the exact authorization origin, client, callback, scope,
state and S256 challenge, then checks the actual JSON authorization-code grant
and its verifier/challenge linkage. The selected account sends the unchanged
Messages payload with OAuth Bearer, version, beta and CLI headers; it does not
send an API-key header. Two fake-clock refreshes check JSON grants, persisted
rotating refresh tokens, and subsequent use of each new access token. The actual
application response bytes are checked as well.

Wrong-state, explicit denial and empty-code callbacks perform no token exchange
and leave the existing temporary credential file byte-for-byte unchanged. The
prior account remains usable. Synthetic ambient API-key fallback is present
during the failure cases: 429 and 503 hold a 60-second cooldown without repeated
HTTP, recover exactly at its boundary, and perform no premature application
request. Terminal 400 becomes `reauthorize`; malformed success and transport
unknown become `refresh_unknown`. All three block subsequent refresh/application
requests, including after time advances, rather than falling back to a key.

Commands executed after acquiring shared Debug build ownership:

```sh
/opt/homebrew/Cellar/cmake/4.3.0/bin/cmake --build build/debug --target provider_oauth_test -j 4
/opt/homebrew/Cellar/cmake/4.3.0/bin/ctest --test-dir build/debug -R '^provider_oauth$' --output-on-failure
```

Build succeeded. `provider_oauth`: **1/1 passed**, 1.18 seconds (1.24 seconds CTest
total). `clang-format` applied to the changed source and `git diff --check` passed.
Shared Debug build ownership was then released to the main-adapter owner. Root
owns the independent integrated source review and required source gate.

These checks use only private temporary fixture state and injected transport.
Live owned-account validation is a separate campaign result; this lane did not
inspect real credentials or invoke a live provider.

## Narrow compatibility remediation

After the live token exchange returned 429, the primary-source comparison in
[LIVE_RATE_LIMIT](LIVE_RATE_LIMIT.md) identified the missing `code=true` authorize
parameter and missing Accept JSON on token code/refresh POSTs. The owning fixture
was extended before production changes. Its actual RED was **1/1 failed**, 1.04
seconds, with `Anthropic manual authorization missing code=true` (CTest exit 8).

The production correction adds exactly those three request fields for Anthropic.
It preserves client, scopes, endpoints, JSON grant bodies, state/PKCE checks,
revision fencing and the absence of automatic authorization-code replay. After
formatting, the existing Debug `provider_oauth_test` and `blackbird-auth` targets
both built successfully. The same focused CTest was **1/1 passed**, 1.07 seconds
(1.08 seconds total). `git diff --check` passed. Debug build ownership was released
to the adapter owner, and root was notified that the updated auth binary was
available for one fresh live consent.

This RED/GREEN qualifies the specified request shape and retained failure
semantics. It does not establish the cause of, or cure for, the live 429.
