# Provider routing, prompt caching and usage

Blackbird's main coding loop can use OpenAI, OpenRouter or registered Responses routes. Choose the
provider explicitly; changing a model string alone does not switch accounts or
billing routes. Native context-only colleagues keep their existing provider registry.

## Paper Instruments and registered Responses

Register an HTTPS origin, wire dialect and environment-variable name, then store
the key through the private auth prompt:

```sh
./scripts/blackbird auth add-provider paper https://api.paperinstruments.com/v1 responses-portable paper_api_key
./scripts/blackbird auth key paper default
./scripts/blackbird --provider paper --model ultramarine-1-flash
```

`auth key` prompts without echo; it also accepts stdin. An existing key in a
`.env` file must be imported privately or exported to the named environment
variable; the launcher does not source `.env`. The auth store retains imported
keys, so later launches need no environment key. With no selected owned account,
the registered environment variable is used. Broken/logged-out selections block
fallback, as with other owned providers.

Use `responses` for a provider accepting Blackbird's normal OpenAI Responses
fields. `responses-portable` sends developer messages as system messages,
requests plaintext reasoning and accepts only `low`, `high` or `max` effort.
The ordinary initial `medium` is unsupported, so the interactive harness opens
and asks which supported effort you intended. No inference runs until you answer;
quit/reopen preserves an unanswered choice. Explicit valid `--effort` or saved
settings skip the question. The plain command loop accepts `/effort LEVEL`;
one-shot/application calls need a supported effort before starting.
The portable adapter refuses encrypted history instead of discarding it; begin a
fresh session when moving from a provider whose history contains encrypted items.
Plaintext reasoning and tool call/result IDs are replayed with the full history.
Function tools without an explicit `strict` choice use `strict:false`, preserving
optional arguments instead of letting the service normalize them to mandatory ones.
Local function tools use string arguments/results at the wire boundary. Provider
limits on models, modalities and other optional fields still apply. The dialect
does not promise compatibility with every Responses implementation.
Successor seeds currently require encrypted reasoning in the shared context
validator; a seed containing Paper plaintext reasoning is refused. Ordinary
conversation replay, context editing, reopen and fresh `/new` sessions work.

Provider/model/effort persist across reopen, native restart and `/new`. Chat and
Messages registrations remain available for colleagues. A main route with an
unsupported protocol is refused. An unsupported interactive effort remains
pending until explicitly answered; it is never saved as accepted configuration. No
request changes the provider or credentials automatically. Custom main routes
use provider-default caching; explicit OpenAI/OpenRouter cache policies remain
limited to those qualified routes. `BLACKBIRD_RESPONSES_CURL` selects a transport
executable for deterministic/local checks; ordinary runs use system curl.

Paper's [Responses contract](https://docs.paperinstruments.com/docs/ultramarine/responses)
documents the model, effort values, plaintext reasoning and unsupported fields.
Source grounding and direct qualification are recorded in
[the provider plan](../papers/paper-provider-2026-10-15/PLAN.md) and
[the qualification results](../papers/paper-provider-2026-10-15/QUALIFICATION.md).

## OpenRouter main coding

Use an existing OpenRouter key through the owned authentication interface:

```sh
BLACKBIRD_EXECUTABLE="$PWD/build/debug/blackbird" ./scripts/blackbird auth key openrouter default
BLACKBIRD_EXECUTABLE="$PWD/build/debug/blackbird" ./scripts/blackbird \
  --session context/router-session --provider openrouter --model openai/gpt-6.1-sol
```

The auth command prompts privately; never put a key in a chat command or argument.
Alternatively, with no selected owned account, the adapter uses the existing
`OPENROUTER_API_KEY` environment variable. A selected logged-out/broken account
does not fall back to another credential. Provider/model choices are retained
through reopen, native restart and `/new`; `/session` displays them. A provider
switch requires an explicit model. Older settings default to OpenAI.

The adapter sends full required history with `store:false`, preserves opaque
reasoning and assistant phases, requires parameter support for coding tools,
requests routing metadata and disables whole-response caching. Prefix caching
remains independent. A valid complete response is required before tool dispatch;
failed/incomplete/malformed streams retain their observed originals. The default
Lua program refuses namespaced/custom tool execution rather than guessing a local
tool; custom programs can handle retained structures deliberately.

Existing OpenAI paths remain available. No automatic model, account or paid-route
fallback is added. `BLACKBIRD_OPENROUTER_CURL` selects a transport executable for
deterministic/local transport checks; ordinary use keeps the existing system curl.

## Prompt policy and changing state

Request-local Lua choices:

```lua
local result = blackbird.request({cache_policy = "stable_prefix"})
-- Or: cache_policy = "provider_default" / "disabled"
-- state_context = "omit" deliberately omits the automatic task/budget message.
```

`provider_default` is the default and preserves authored upstream controls.
`stable_prefix` adds one explicit boundary at the latest eligible text part before
the automatic state message, retaining implicit caching. Native tool results are
encoded once at the external boundary before a result string becomes a text part.
`disabled` uses explicit-only caching with no markers. Explicit native policies
reject conflicting authored cache controls and unsupported model/account routes;
they never silently change the model, credentials or billing destination.

The initial explicit-policy support is deliberately limited to `gpt-6.1-sol` on
the owned public OpenAI API-key route, and `openai/gpt-6.1-sol` on OpenRouter.
Subscription/compatibility and other models retain provider defaults or directly
authored request fields; this implementation does not establish their cache-control
acceptance. No paid prewarm or keepalive runs. A marker does not prove a cache hit.

Retain a policy through the existing program configuration tool:

```lua
local config = blackbird.call("program_config", {})
local proposal = config.effective
proposal.prompt_cache = "stable_prefix"
return blackbird.call("program_config", {base = config.revision, proposal = proposal})
```

A successful workflow boundary activates it. Failure/cancel keeps the effective
policy. Request-local `cache_policy` overrides the retained default. Main provider
selection stays in session settings; it is not an unannounced program override.

Task/budget facts now appear in one request-local trailing developer message,
after complete selected input and tool groups. They do not accumulate in retained
context. `state_context=auto` is the default; `omit` is explicit model policy.
Budget bytes still describe selected native input before adding that message,
including any Lua input override. They are neither tokens nor final wire bytes.
Retries retain one assembled request; the next request/restart derives fresh
state. Historical request inspection preserves the actual native request and
external bytes, while context inspection shows retained items.

For deliberate external tool-content arrays, an authored native
`function_call_output` item may set `blackbird_output_parts=true`; its `output`
must then be a supported typed text/image/file content array. The discriminator
is removed by the external encoder. Ordinary native data, including arrays that
look like content blocks or contain marker-shaped keys, remains JSON text at that
boundary. Do not use this opt-in for ordinary application data.

## Read usage independently

The [default coding workflow](ECONOMICAL_DEFAULTS.md) returns bounded file/command
observations when callers supply no explicit limit or file range. File results
include omission counts and a retained snapshot for exact later recovery. Custom
Lua can compose reads/loops and return selected useful facts with those recovery
handles; it retains full reads by default. Model/effort, cache policy and context
compaction choices remain explicit. This reduces specific returned payloads; net
token, cost and task-quality improvements have not been measured.

```sh
build/debug/blackbird-token-usage context/router-session
build/debug/blackbird-token-usage context/router-session --max-records 100000 --max-bytes 67108864
```

The native tertiary reader opens pinned preserved raw prefixes without taking the
semantic writer/recovery lock. It shows reported counters, known subtotals,
unknown/invalid consumption, main/colleague/decision scopes and source locators.
Copies of the same response are not independent calls; failed or locally rejected
responses can still have reported usage. A known local refusal differs from a
dispatched request with missing usage. Alternative Claude per-model scopes are
not added to its aggregate.

Generic numeric costs retain an unspecified currency unless the actual route
establishes OpenRouter USD or an explicit USD field supplies its unit. Invalid
charge metadata does not invalidate independently valid token counters. Conflicting
terminal usage is marked invalid; incomplete usage remains partial and cannot
produce a confident price estimate.

Observed OpenRouter account charges stay separate from upstream breakdowns and
optional list-price estimates. Supply explicit dated USD/million rates with:

```sh
build/debug/blackbird-token-usage context/router-session \
  --price 2026-10-14 openrouter openai/gpt-6.1-sol - 2 10 0.10 2.50
```

This is an example tariff input, not an invoice or a guaranteed current rate.
Use the actual model/provider/tier/date; `-` means absent tier/cache rate.
The estimate refuses missing/contradictory partitions and unsupported TTL-specific
write pricing rather than inventing zeroes. Inclusive reasoning is not charged
twice. Public API estimates do not determine subscription allowance.

Coverage is explicitly raw-prefix-only, never a complete session bill. Missing
runs, older/unlinked/unsupported/corrupt data and scan limits remain visible.
`--max-bytes` bounds decoded scan work; the existing raw reader also validates
its pinned audit prefix, so this is not a total filesystem-I/O limit. Original
locators point into the preserved run; later bytes require a new snapshot.
`/stats` remains selected last-response information in the main process.

The [execution plan](TOKENOMICS_IMPLEMENTATION.md),
[provider research](../papers/tokenomics-2026-10-14/provider-contracts.md) and
[OpenRouter contract](../papers/tokenomics-2026-10-14/openrouter-contract.md) and
[qualification record](../papers/tokenomics-2026-10-14/qualification.md)
record the contracts and qualification boundaries. No cache-saving or general
task-quality result is established by deterministic protocol checks alone.
