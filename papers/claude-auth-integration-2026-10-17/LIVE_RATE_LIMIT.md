# Browser token exchange returned 429

2026-10-17. Root reports completed browser consent and pasted callback, followed
by native auth exit 1 and `provider_rate_limit (429)`; no owned Anthropic account
was saved. This lane read source/public artifacts only. That status does not
distinguish transient quota, front-door compatibility rejection or another
service restriction. No live retry, credential/cache read or Terminal inspection
was performed here.

## Concrete request comparison

At the failed live attempt, Blackbird's [browser builder](../../src/provider_oauth.cpp#L605) used
`https://claude.ai/oauth/authorize` and the manual callback
`https://platform.claude.com/oauth/code/callback`. Its URL at line 627 omitted
`code=true`. Its [token exchange](../../src/provider_oauth.cpp#L711) sent the six
JSON fields grant type, client ID, code, state, callback and verifier, with only
an explicit JSON Content-Type. Token endpoint and client are
`https://platform.claude.com/v1/oauth/token` and
`9d1c250a-e61b-44d9-88ed-5944d1962f5e`. Refresh also uses JSON.

The [current Pi source](https://github.com/earendil-works/pi/blob/8efd314fa3d3232ce197d8d7a6a5660530b9d83e/packages/ai/src/auth/oauth/anthropic.ts#L12)
has exactly that endpoint, client, authorize origin and copy-code callback. Its
copy-code builder at lines 185–218 includes `code=true`; its shared token POST at
lines 70–86 uses JSON plus Accept JSON, no explicit User-Agent, and no retry.
It requests additional session/MCP/file scopes. The legacy Pi source retained
under `quarantine/pi-agent-rust/legacy_pi_mono_code/pi-mono/packages/ai/src/utils/oauth/anthropic.ts`
also uses JSON and `code=true`, with older console host names. Current acquired
OpenCode commit `2006259a02a87edf9e37f253cbddf3188309026b` has no subscription
OAuth in `packages/core/src/plugin/provider/anthropic.ts`; it is not evidence of
a working current token exchange.

The [preserved official Claude Code 2.1.286 distribution](../2026-09-30-claude-code-distribution-acquisition.md)
was read as bytes, never executed. For the macOS ARM64 artifact, SHA-256
`75e3016e9d2570767b08e43a7467d4817a4f149232c169ca295f2c95fef21433`, embedded
JavaScript provides reproducible byte locations:

| Byte location | Actual vendor behavior |
| --- | --- |
| 176233164 | Same token endpoint/client/manual callback; Claude subscription authorize origin is now `https://claude.com/cai/oauth/authorize`. |
| 179353400–179354710 | `x9n` builds the authorize URL, always adding `code=true`, state and S256 challenge. |
| 179354670 | `ajr` sends the authorization-code grant as JSON, explicit JSON Content-Type, 30-second timeout. |
| 179355481 | `Pde` sends JSON refresh grant and includes its requested scope. |
| 177728091 | `ht` is the bundled Axios client. |
| 177691467 / 177703833 | Bundled Axios version 1.15.2 and HTTP adapter default `axios/1.15.2` User-Agent; the token call itself sets no Claude CLI User-Agent. |

These vendor and current Pi implementations contradict the historical claim that
JSON encoding itself must be replaced with form encoding to cure this failure.
They also do not justify adding an invented Claude CLI User-Agent to the token
request. Actual wire behavior was not observed, so shared-client customization
outside the inspected call remains a limit of the static comparison.

## Retry and diagnosis

The primary [OpenCode report #18329](https://github.com/anomalyco/opencode/issues/18329)
describes this same consent-success/token-429 sequence, sometimes without a
Retry-After header. Author reports and proposed UA/form changes conflict; the
linked change was not accepted upstream. This is a diagnostic lead, not a
verified explanation of this host's response.

Official [Claude API error guidance](https://platform.claude.com/docs/en/api/errors)
and [rate-limit guidance](https://platform.claude.com/docs/en/api/rate-limits)
describe bounded SDK backoff and honoring Retry-After for API requests. They also
describe 429 variants that do not recover through ordinary retries. These docs
do not establish authorization-code consumption semantics for the consumer
OAuth endpoint. [RFC 6585 §4](https://www.rfc-editor.org/rfc/rfc6585.html#section-4)
allows Retry-After on 429; it does not make every POST replay safe.

[RFC 6749 §4.1.2](https://www.rfc-editor.org/rfc/rfc6749#section-4.1.2) specifies
short-lived, single-use authorization codes; replay can invalidate issued tokens.
[RFC 9110 §9.2.2](https://www.rfc-editor.org/rfc/rfc9110.html#section-9.2.2)
requires knowledge of safe semantics or non-application before automatically
repeating a non-idempotent request. A token-exchange timeout/unknown result must
therefore never enter a generic retry loop. A received 429 can support an
operator-visible delay/fresh-login recommendation; automatic reuse of the same
code needs endpoint-specific non-consumption evidence absent from this study.

Blackbird currently returns only HTTP status/body; [its curl transport](../../src/provider_auth.cpp#L512)
does not collect response headers. The auth CLI then reports a generic status,
so this attempt's request ID/Retry-After/error category are unavailable. A narrow
follow-up can retain allowlisted private auth response metadata and display safe
status, request ID and Retry-After to the operator, without printing callback
codes, grants, tokens or arbitrary response bodies into chat/audit.

The directly grounded compatibility corrections are `code=true` on Anthropic
authorization and Accept JSON on its token POSTs. Root subsequently authorized
these narrow changes; [the owning result](SEAM_RESULTS.md) records actual
RED/GREEN and the rebuilt auth executable. Neither correction has been shown to
cure the observed 429. JSON, state/PKCE/revision checks and no blind replay are
retained; scopes and identity strings are unchanged. Any subsequent fresh login
must be assessed as a live result, separately from the deterministic fixture
result.
