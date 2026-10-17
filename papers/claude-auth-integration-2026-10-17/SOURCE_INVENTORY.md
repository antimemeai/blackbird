# Existing Claude authentication and provider support

Read-only source inventory, 2026-10-17. No credentials, provider caches, account
status, login or inference were inspected or invoked. No tests were rerun for
this inventory. The source studied is already on master at `74a42f4`; the root
agent is separately integrating the qualified TUI branch.

## What already exists

| Surface | Actual behavior | Source |
| --- | --- | --- |
| Owned Anthropic login | `blackbird auth login anthropic ACCOUNT` prints a browser authorization URL, generates PKCE S256/state, accepts hidden `code#state` or callback parameters, checks state, exchanges JSON for tokens and commits the selected account. | [browser flow](../../src/provider_oauth.cpp), [registry](../../src/provider_auth.cpp) |
| Owned key | `blackbird auth key anthropic ACCOUNT` accepts hidden terminal input or stdin. Without an owned selection, the direct provider may use `ANTHROPIC_API_KEY`. | [auth CLI](../../src/provider_oauth.cpp), [request resolution](../../src/provider_auth.cpp) |
| Selection and logout | `auth use anthropic ACCOUNT`; `auth logout anthropic ACCOUNT`. Explicit failed/logged-out selection blocks ambient-key fallback. Logout is local deletion, not remote revocation. | [auth owner](../../src/provider_auth.cpp) |
| Persistence and refresh | Owned binary credential store, directory 0700/file 0600, no-follow checks, atomic replacement, thread/process locking, account/selection revision checks. Expiring Anthropic OAuth tokens refresh with JSON. Unknown refresh requires reauthorization; transient failures retain tokens with cooldown. | [auth owner](../../src/provider_auth.cpp), [API](../../include/blackbird/provider_auth.hpp) |
| Direct Anthropic requests | Provider `anthropic` uses `https://api.anthropic.com/v1/messages`. API keys use `x-api-key`; OAuth uses Bearer. Context-only colleagues and participants can use it. | [request transport](../../src/provider_auth.cpp), [colleague adapter](../../src/colleague.cpp) |
| Existing Claude CLI bridge | Provider `claude` runs installed `claude --print --output-format json`, selected model/context, no tools, no session persistence, empty settings sources and strict empty MCP configuration. The installed CLI owns its authentication; Blackbird does not import that credential cache. | [colleague adapter](../../src/colleague.cpp) |
| Metadata exposure | `/auth`, `/auth status`, `/auth providers`, model `provider_auth_status` and Lua calls expose nonsecret local metadata; they do not probe or refresh the account. | [usage](../../docs/USING_BLACKBIRD.md), [API](../../include/blackbird/provider_auth.hpp) |

`anthropic` and `claude` are distinct transports. Selecting an owned Anthropic
account does not configure the Claude CLI bridge. Existing CLI authentication
does not select an owned account.

The direct colleague Messages adapter submits one user prompt, system instructions,
`max_tokens=8192`, `stream=false`, and no tools. It accepts text plus optional
thinking blocks and requires `end_turn`/`stop_sequence`. That is useful selected
context collaboration, not a Claude main coding agent.

## Main coding route is a separate gap

[Main startup](../../src/main.cpp), `validate_route`, explicitly rejects protocols
other than `responses` and `responses-portable`. Anthropic's registered protocol
is `messages`; therefore `--provider anthropic --model ...` currently emits
“Main coding requires a Responses provider.” Main coding uses OpenAI/OpenRouter
and registered Responses routes, not the existing Anthropic Messages adapter.

If the intended addition is Claude as the principal tool-using coding model,
authentication is already machinery we can reuse; the new work is a reviewed
Messages conversation/streaming/tool-use adapter integrated with the retained
engine. Removing the startup guard alone would not implement that behavior.

## Existing direct checks and their limits

- [provider_oauth_test.cpp](../../tests/provider_oauth_test.cpp) exercises the
  Anthropic successful copy-code flow with synthetic HTTP, checking returned
  state and PKCE verifier/challenge and stored selection. Most adversarial
  browser/JWT cases in this file are OpenAI cases. It does not contact Anthropic.
- [provider_auth_test.cpp](../../tests/provider_auth_test.cpp) checks private
  storage, rejected inputs/origins, logout/revision fences, and single refresh
  across threads and processes. Its detailed refresh race uses OpenAI fixtures.
- [provider_auth_raw_test.cpp](../../tests/provider_auth_raw_test.cpp) saves a
  synthetic Anthropic OAuth account, advances the clock, runs the actual refresh
  resolution, and checks that the fresh Bearer token reaches the next request.
  It also checks private values do not enter raw capture. The destination in
  that refresh case is `models`, not the OAuth `messages` request branch.
- [provider_colleague_test.cpp](../../tests/provider_colleague_test.cpp) exercises
  direct Anthropic Messages request/result normalization with a synthetic API
  key, plus other providers, credential/capture separation and revision fences.
  It does not qualify current subscription OAuth inference.
- [check-provider-auth](../../scripts/check-provider-auth) uses real CLI/PTY
  processes with synthetic keys and an isolated store: echo suppression,
  interrupt restoration, metadata privacy, session nonleakage and local logout.
- [colleague_test.cpp](../../tests/colleague_test.cpp) checks Claude CLI envelope
  normalization, including authentication error results. The recorded
  [2026-10-07 CLI readiness](../2026-10-07-claude-auth-restored.md) and
  [2026-10-08 review](../2026-10-08-claude-colleague-review.md) are historical live
  CLI observations, not current account availability or owned OAuth acceptance.

[JOURNAL.md](../../JOURNAL.md), 2026-10-08/09 auth implementation/integration,
records scoped Mac release and Linux checks, source integration at `59539d3`,
and explicitly no real account login/inference validation. Current-state docs
still record operator consent/live account validation as follow-through. These
are recorded qualification results, not fresh runs performed by this inventory.

## Concrete follow-through

1. Preserve and reuse the owned account owner and CLI rather than implement a
   second credential lifecycle. Auth itself is present on master.
2. Verify present provider behavior against current primary sources before
   claiming supported subscription login. The descriptor is a historical
   OpenCode/Pi-style client flow. The implementation adds a Claude Code identity
   system prefix for OAuth and hardcodes `claude-cli/2.1.163`, `x-app: cli`, and
   `oauth-2025-04-20,claude-code-20250219` beta headers. These are compatibility
   assumptions, not live evidence that Anthropic currently accepts Blackbird.
3. If changing that path, directly test Anthropic negative callback state,
   JSON refresh fields/failure transitions, and OAuth Messages headers/body using
   synthetic account tokens. Existing checks cover parts individually, not a
   complete current browser-login-to-OAuth-Messages sequence.
4. Distinguish operator browser consent from implementation. Codes/keys belong
   only in the dedicated auth process, never in chat, a model tool argument, a
   command-line key value, or a research artifact. No Claude cache import is
   needed by the owned flow.
5. Resolve product wording with the actual intended surface: account login,
   context-only Claude collaboration, and Claude as the main coding model are
   three different claims. The first two have existing implementations; the
   third needs provider protocol work.

Useful existing commands, executed by the operator only when account setup is
the intended next step:

```sh
./scripts/blackbird auth login anthropic personal
./scripts/blackbird auth use anthropic personal
./scripts/blackbird auth status
```

`auth key anthropic personal` selects a separately issued API key instead of
subscription OAuth; do not substitute it implicitly. [Using Blackbird](../../docs/USING_BLACKBIRD.md)
already documents these commands, though its auth section still calls the
integrated work a “candidate” and its earlier colleague schema text lists only
`openai`/`claude` before the later registry expansion section.
