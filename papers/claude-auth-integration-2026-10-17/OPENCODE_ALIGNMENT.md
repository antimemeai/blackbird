# Match the published OpenCode authorization-code flow

Operator2026-10-17 explicitly resumes work and requests matching a published
implementation instead of mixing reference versions. Use the actual npm artifact
`opencode-anthropic-auth@0.0.13`, `package/index.mjs`, as the single request
contract. This is study material, not a dependency or executable integration.

Its authorize/exchange/refresh functions use the same public client and scopes,
`https://claude.ai/oauth/authorize?code=true`, the
`https://console.anthropic.com/oauth/code/callback` callback,
`https://console.anthropic.com/v1/oauth/token` endpoint, JSON grants with only
explicit Content-Type, and the PKCE verifier as the OAuth state. Match those
fields exactly while keeping Blackbird's hidden input, callback state validation,
credential ownership, cancellation and refresh failure fences. Keep OpenAI's
independent state/nonce behavior unchanged. Do not rewrite the HTTP stack or add
a library. The remaining curl-versus-fetch transport difference is explicit.

First change the independent fixture to demand this exact published contract;
capture actual failure. Then make the bounded production changes, run the same
fixture green, rebuild native auth and perform a fresh operator consent. The
earlier Pi-derived Accept header correction is superseded for this pinned flow.
HTTP 429 remains an observed result until a real exchange succeeds. No automatic
replay of old authorization codes or change to separately billed API access.

Messages adaptation, source review fixes and host installation remain in the
existing campaign. This auth change does not copy the plugin's unrelated prompt,
tool-name or stream-rewriting code. Live inference is a separate next check after
credentials are actually saved.

## Direct result

The owning fixture failed before the production change with
`Anthropic OAuth state must match pinned OpenCode PKCE verifier` (0.78 seconds).
The bounded independent comparison found one stale Accept assertion in the
refresh-failure fixture; it was corrected to the published one-header contract.
Debug `provider_oauth` and `provider_auth_raw` then passed 2/2 in1.95 seconds.
The native auth and main binaries were rebuilt. All original state/PKCE rejection,
rotation/cooldown/unknown and private raw-capture checks remain active.

A fresh dedicated Terminal sign-in was opened against this binary. Its recorded
exit was1; no owned Anthropic account is present. The window was closed before
its final error line could be read, so this attempt is not labeled another429.
No inference request or successful live login is claimed.

The operator subsequently asks for Anthropic's documented path. Its
[Claude Code authentication guide](https://code.claude.com/docs/en/authentication#generate-a-long-lived-token)
documents `claude setup-token` and `CLAUDE_CODE_OAUTH_TOKEN` for Claude Code
subscription automation. Its [API guide](https://platform.claude.com/docs/en/manage-claude/authentication)
documents API keys/federation/App Attest for native clients. These are distinct
interfaces; that documentation does not qualify this independent OAuth flow or
explain its observed failures. No billing route or credential source is changed
silently while answering that question.

The [Agent SDK overview](https://code.claude.com/docs/en/agent-sdk/overview#get-started)
explicitly directs third-party products to API-key authentication unless Anthropic
has approved offering claude.ai login and subscription rate limits. Matching the
published plugin does not establish that approval or official API support. For
Blackbird's own tool loop, the documented route is API authentication; wrapping
the Claude Code CLI uses its agent loop and changes the integration boundary.
