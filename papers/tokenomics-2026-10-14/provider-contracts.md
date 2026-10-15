# Provider contracts for tokenomics

Observed **2026-10-14** from current official documentation; research only. These
are vendor contracts/descriptions, not measurements of Blackbird's accounts.
No inference, account probes, sign-ins, dependency adoption or source changes
were performed. OpenAI research followed the OpenAI Docs skill: topic/model
search, then fetched official documentation before repository inspection.

## Actual routes and the qualification boundary

The registry in `src/provider_auth.cpp:275` declares the following routes.
`src/colleague.cpp:249` builds direct context-only colleague requests; the main
coding loop remains OpenAI. The requested colleague model is supplied by its
caller, so the registry does not establish a default Claude/Grok/Kimi/MiMo model.

| Blackbird route | Origin / format | Billing boundary to preserve |
| --- | --- | --- |
| OpenAI owned key | `api.openai.com/v1/responses` | Paid API account/project |
| OpenAI owned OAuth | Same public Responses endpoint | ChatGPT-plan permission and account-specific access |
| OpenAI compatibility | `chatgpt.com/backend-api/codex/responses` | Existing Codex session; public API docs do not qualify this backend |
| `anthropic` | `api.anthropic.com/v1/messages` | API key or owned OAuth; these have different qualification status |
| `claude` | Installed Claude CLI | Separate external application and its selected billing path |
| `kimi` | `api.kimi.com/coding/v1/messages` | Kimi Code membership route |
| `moonshot` | `api.moonshot.ai/v1/chat/completions` | Open Platform paid API; separate from Kimi Code |
| `grok` | `api.x.ai/v1/chat/completions` | API key or owned device OAuth; do not infer equal entitlement |
| `mimo` | `api.xiaomimimo.com/v1/chat/completions` | Fixed origin; current Token Plan docs specify different origins |
| `openrouter` / custom | Selected origin and protocol | Downstream model/provider/account pricing remains unqualified here |

Local evidence is configuration and request construction, not server acceptance.
See `docs/USING_BLACKBIRD.md:627`, `src/openai.cpp:293` and
`src/provider_auth.cpp:1538`. In particular, a credential labelled OAuth, an
installed CLI or a model catalog entry does not establish working access.

## OpenAI: exact model `gpt-6.1-sol`

### Public API caching and schema

For GPT-5.6 and later, the cacheable prefix minimum is 1,024 visible tokens.
Implicit mode writes at an eligible message boundary; a stable prefix followed
by changing content needs its own explicit boundary. Put
`prompt_cache_breakpoint: {"mode":"explicit"}` on a supported input content
block. Top-level `instructions` cannot hold one. `prompt_cache_options.mode`
selects `implicit` or `explicit`; explicit mode with no markers does not cache.
At most four writes are available; implicit mode consumes one slot. TTL is at
least 30 minutes after write/reuse. On this family `prompt_cache_key` optionally
separates accounting, rather than being necessary for cache routing. Cached
inputs still count toward TPM. Earlier models need separate policies.
[Prompt caching](https://developers.openai.com/api/docs/guides/prompt-caching).

The Responses create schema independently confirms `prompt_cache_options`
(`mode`, `ttl`, `prewarm`, `comparison_response_id`), with default/only TTL
`"30m"`. Breakpoints appear on supported input text/image/file blocks and
structured function/custom-tool output content. A tool-output string therefore
does not itself provide a content-block marker location. Response `usage` can
be absent/null; when present the schema includes:

```text
input_tokens
input_tokens_details.cached_tokens
input_tokens_details.cache_write_tokens
output_tokens
output_tokens_details.reasoning_tokens
total_tokens
```

These are public-schema facts, not a claim that either Blackbird subscription
route accepts every field or returns all counters.
[Responses create reference](https://developers.openai.com/api/reference/resources/responses/methods/create).

There are unresolved documentation disagreements:

- The caching guide describes first two/latest fifty explicit lookup boundaries,
  plus up to twenty earlier eligible endings in implicit mode; the create
  reference says latest eighty boundaries without a content-block lookback limit.
- The reference still describes `prompt_cache_key` as a cache-hit optimization
  and says `prompt_cache_retention: "24h"` for GPT-5.5 and “future models,” while
  the guide makes the GPT-5.6+ distinction above.

The reference defers to the guide for current details. The proposed implementation
should retain recent intended boundaries and actual request/response bytes;
long-window behavior remains unqualified.
[Guide](https://developers.openai.com/api/docs/guides/prompt-caching),
[reference](https://developers.openai.com/api/reference/resources/responses/methods/create).

### Prices and the equation they support

Standard public API text prices per million tokens are **$2 input, $0.10 cached
input, $2.50 cache write, $10 output** for `gpt-6.1-sol`. Writes replace ordinary
input pricing for those tokens; they are not an additional charge on those same
input tokens. Prompts above 272,000 input tokens charge the full request at 2×
input/cache rates and 1.5× output. Fast is 2× Standard, Ultrafast 6×; Batch/Flex
are 50% below Standard; regional processing can add 10%. Do not apply a tier
multiplier merely because it was requested: retain the effective response tier.
[GPT-6.1 Sol](https://developers.openai.com/api/docs/models/gpt-6.1-sol),
[pricing](https://developers.openai.com/api/docs/pricing).

For a completed ordinary Standard text request below the long-context threshold,
let `I=input_tokens`, `R=cached_tokens`, `W=cache_write_tokens`,
`O=output_tokens`; then `U=I-R-W` and the derived estimate is:

```text
USD = [2 U + 0.10 R + 2.50 W + 10 O] / 1,000,000
```

Do not add reasoning tokens again to inclusive output. For an identical prefix
of length `P`, one write and `n-1` complete reads cost
`P × [2.50 + 0.10(n-1)] / 1e6`, versus `P × 2n / 1e6` without caching.
This comparison assumes successful reuse, no changing suffix/output differences,
the same tier and no additional tools; it is not a Blackbird savings result.
The equation is an inference from the quoted rates and usage partition, not an
invoice reconciliation.

Paid API rate limits depend on organization/project, model and usage tier;
requests and tokens have distinct limits and some models share a bucket.
Responses can expose request/token remaining/reset headers and `Retry-After`.
Quota/spend exhaustion is different from a temporary rate limit; unsuccessful
requests also consume per-minute capacity. The current guide uses Build/Launch/
Grow tiers, so older numbered-tier tables should not be copied into configuration.
No account-specific limit was obtained.
[Rate limits](https://developers.openai.com/api/docs/guides/rate-limits).

### ChatGPT-plan and Codex paths

Official plan inference uses an authorized OAuth bearer token against the
**public** Responses endpoint, explicitly excluding `backend-api` for this flow.
The account-specific models endpoint and a completed inference are stronger
access observations than a bundled catalog. A stream may start and subsequently
fail with `subscription_sharing_usage_limit_exceeded` or
`subscription_sharing_usage_unavailable`; success requires `response.completed`.
[Models and inference](https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference).

The plan preview requires `store:false`, `stream:true` and complete needed
history in an input array. HTTP `previous_response_id` is unsupported, as are
`prompt_cache_retention`, `max_output_tokens`, `truncation` and several other
paid-API parameters. Function/custom tools must be namespaced or supplied through
`additional_tools`; Responses `tool_search` and top-level
`programmatic_tool_calling` are unsupported. The unsupported-field list does not
establish positive support for all newer caching controls.
[Preview limitations](https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations).

A valid identity token alone is insufficient: granted
`chatgpt.tokens.use.direct` permission is required.
[Sign-in](https://developers.openai.com/siwc/token-sharing-open-source/sign-in).
For Plus, the five-hour allowance is shared across apps; that five-hour limit
does not apply to Pro. App access/credits are managed in ChatGPT settings.
[Accounts and sessions](https://developers.openai.com/siwc/token-sharing-open-source/profiles-and-sessions).

**Unknown:** how cached reads/writes translate into this operator's plan allowance,
credits or legacy Codex usage, and whether the actual chosen route accepts the
new cache controls. Public API dollar estimates must be labelled hypothetical
for these routes. No silent conversion to an API-key billing path is justified.

## Anthropic: paid API versus subscription authentication

Claude caching supports top-level `cache_control:{"type":"ephemeral"}` for a
moving automatic boundary, or markers on content blocks. It caches the full
`tools`, `system`, `messages` prefix in that order. Four breakpoints and a
twenty-block lookup window apply. Default TTL is five minutes; `ttl:"1h"` is
available. Lifetime starts at request start and reads refresh it. Concurrent
calls cannot reuse a new entry until its first response begins. Minimums vary:
Sonnet 4.6 needs 1,024 tokens, Opus 4.6 and Haiku 4.5 need 4,096; do not assign a
universal threshold. Changing tools/system/earlier blocks invalidates downstream
prefixes. Thinking and effort changes have model-specific effects.
[Prompt caching](https://platform.claude.com/docs/en/build-with-claude/prompt-caching).

For the concrete comparison model `claude-sonnet-4-6`, published USD/MTok rates
are input **$3**, five-minute write **$3.75**, one-hour write **$6**, read **$0.30**,
output **$15**. Cache-read multipliers differ on newer models; 10% is not universal.
Regional and Batch modifiers require their own accounting.
[Pricing](https://platform.claude.com/docs/en/about-claude/pricing).

Claude's usage partition differs from OpenAI: total input equals
`input_tokens + cache_creation_input_tokens + cache_read_input_tokens`.
`cache_creation.ephemeral_5m_input_tokens` and
`cache_creation.ephemeral_1h_input_tokens` split writes by TTL.
`output_tokens` is the inclusive billed total; its optional thinking breakdown
does not add another output charge. Token counts need not match visible text.
[Messages reference](https://platform.claude.com/docs/en/api/typescript/messages).

For Sonnet 4.6, derive the ordinary API estimate with uncached `U`, five-minute
write `W5`, one-hour write `W1`, read `R`, inclusive output `O`:

```text
USD = [3 U + 3.75 W5 + 6 W1 + 0.30 R + 15 O] / 1,000,000
total_input = U + W5 + W1 + R
```

API rate limits distinguish RPM, input TPM and output TPM. For most current
Claude models, input TPM counts uncached input and cache creation but excludes
cache reads; Haiku 3.5 is a documented exception. Limits depend on organization,
tier and model class; acceleration limits and spend-limit failures also exist.
Respect a temporary rate-limit `retry-after`; an enforced spend limit can have
the same error type without that header and will not recover by blind retries.
[Rate limits](https://platform.claude.com/docs/en/api/rate-limits).

The official Claude Code authentication guidance says product developers should
use API keys/cloud credentials. It does not permit third-party applications to
offer Claude.ai login or route through users' Free/Pro/Max credentials. It
separately permits users to sign into an unmodified Claude Code binary.
Accordingly, the direct owned `anthropic` OAuth flow is not qualified by these
public contracts; the `claude` CLI bridge is a distinct route. Sending
Claude-Code-shaped headers/instructions does not establish entitlement or
subscription billing semantics.
[Authentication and credential use](https://code.claude.com/docs/en/legal-and-compliance#authentication-and-credential-use).

## Kimi Code and Moonshot are separate contracts

Official Kimi documentation separates subscription Kimi Code (`api.kimi.com/coding/`
China; `api.kimi.ai/coding/` overseas, Anthropic compatible) from paid Open
Platform (`api.moonshot.ai/v1` overseas). Match each key to its platform.
[Kimi FAQ](https://www.kimi.com/en/help/kimi-code/faq).
Current CLI configuration also shows `api.kimi.com/coding/v1`; the differing
documented base-path conventions require protocol-specific endpoint checks,
not a guessed redirect.
[Configuration](https://www.kimi.com/code/docs/en/kimi-code-cli/configuration/config-files).

New membership plans keep a rolling five-hour window and monthly quota but remove
the weekly window; legacy subscriptions keep their existing rules. CLI, Desktop,
VS Code and third-party requests share quota. Extra Usage is a separately enabled
fallback balance; documentation says it can continue when subscription quota is
exhausted. No operator plan type or Extra Usage setting was inspected.
[Membership](https://www.kimi.com/code/docs/en/kimi-code/membership.html).

For the **Open Platform**, current K3 caching is implicit, with `5m` default or
`1h` TTL and organization isolation. Reads renew the original TTL. Writes are
now a separate category; K2.7/K2.6 do not support that category. Chat usage has
`prompt_tokens_details.cached_tokens` and `.cache_write_tokens`; Responses uses
the corresponding `input_tokens_details` fields; Messages splits reads/writes
from `input_tokens`. Messages `cache_control` is top-level only; content markers
are ignored. The reviewed mainland `kimi-k3` example quotes RMB/MTok 20 uncached,
20 five-minute write, 40 one-hour write and 2 read, with mutually exclusive input
categories. It does not establish the overseas rate or Code-membership quota
conversion. Preserve raw fields and select the exact endpoint/model tariff.
[Context caching](https://platform.kimi.com/docs/guide/context-caching),
[billing distinction](https://platform.kimi.com/docs/pricing/chat).

## Grok / xAI

For Blackbird's Chat format, reuse routing is documented through a stable
`x-grok-conv-id` header. `prompt_cache_key` provides equivalent routing on
Responses. Preserve the prefix and append new content. This is a provider-specific
routing control; it is not the current GPT-6.1 Sol key contract.
[Maximizing hits](https://docs.x.ai/developers/advanced-api-usage/prompt-caching/maximizing-cache-hits).

Chat reports `usage.prompt_tokens_details.cached_tokens`; Responses reports
`usage.input_tokens_details.cached_tokens`. Reasoning is billed at output rates.
Long-context pricing considers total prompt length including cached tokens.
[Usage/pricing](https://docs.x.ai/developers/advanced-api-usage/prompt-caching/usage-and-pricing).
Cache eviction can occur at any time; no guaranteed TTL is published in this
guide.
[Caching FAQ](https://docs.x.ai/developers/advanced-api-usage/prompt-caching/best-practices).

As a dated example, `grok-4.7` public API rates per MTok are $2 input/$0.50
cached/$6 output below the 200k long-context boundary and $4/$1/$12 at long
context. The page's table says ≥200k while prose says “exceeds”; exact-boundary
behavior is unresolved. Standard global cost is consequently of the form
`[pU (I-R) + pR R + pO O]/1e6`, with the selected context band and service tier.
Priority and US-region multipliers are separate.
[Pricing](https://docs.x.ai/developers/pricing).

API limits are per model/team tier and include RPS/TPM; cached, output and
reasoning tokens count toward TPM.
[Rate limits](https://docs.x.ai/developers/rate-limits).
Official Grok Build docs describe browser sign-in and an API-key alternative,
but the sources reviewed do not qualify Blackbird's device client, scopes or
subscription-to-public-API allowance. Keep device OAuth access and API-key
prices separate.
[Build overview](https://docs.x.ai/build/overview).

## MiMo: current documentation exposes a routing mismatch

MiMo's live documentation now redirects to `mimo.mi.com`; its
[official index](https://mimo.mi.com/llms.txt) exposes directly readable Markdown.
Ordinary pay-as-you-go uses a normal Open Platform key/account balance, separately
from Token Plan. Published overseas `mimo-v2.6-pro` rates per MTok are $0.0036
cached input/$0.435 uncached/$0.87 output; `mimo-v2.6-flash` is
$0.0028/$0.14/$0.28. Cache writes are currently free for a limited time.
These are dated tariffs, not a permanent no-write-fee rule. V2.5 model retirement
is announced for October 21, 2026 at 10:00 Beijing time; no model switch is selected
by this report.
[API pricing](https://mimo.mi.com/static/docs/price/pay-as-you-go.md).

Token Plan instead requires dedicated `tp-`/`ttp-` keys and the regional
`token-plan-{cn,sgp,ams}.xiaomimimo.com/v1` OpenAI-compatible origins, with
the console determining the correct cluster. Ordinary and plan keys are not
interchangeable. Examples use the `api-key` header.
[Quick access](https://mimo.mi.com/static/docs/tokenplan/Token%20Plan/quick-access.md).
This conflicts with Blackbird's fixed `api.xiaomimimo.com/v1` builtin and generic
Bearer header. Its documentation's claim to accept Token Plan keys is therefore
insufficient to establish current working plan access. A custom provider origin
alone may not solve the header difference. Actual server acceptance is untested.

Token Plan uses Credits: for `mimo-v2.6-pro`, cached input costs 2.5 Credits/token,
uncached 300 and output 600; Flash uses 2/100/200. The documented night window
uses a 0.8× consumption coefficient. Quota exhaustion suspends service rather
than silently taking ordinary account balance.
[Plan pricing](https://mimo.mi.com/static/docs/price/token-plan.md).
Thus the derived Pro quota equation is `2.5R + 300(I-R) + 600O` Credits before
applicable plan modifiers, not the paid API dollar equation.

The Chat schema includes inclusive `prompt_tokens`, `completion_tokens`, and
`prompt_tokens_details.cached_tokens`.
[Chat reference](https://mimo.mi.com/static/docs/api/chat/openai-api.md).
The rate page describes account/model aggregation across all API keys and lists
100 RPM/10M TPM for V2.6 Pro/Flash, with separate concurrency/load behavior.
It does not establish a guaranteed cache TTL, minimum prefix, explicit-marker
contract, or a cache-read exclusion from TPM.
[Rate limits](https://mimo.mi.com/static/docs/api/guidance/rate-limit.md).

## Consequences for the native proposal

1. Represent provider, protocol, origin, credential/account route, requested and
   observed model, effective tier and tariff date separately. Do not collapse
   subscription quota, API dollars, prompt bytes and supplied tokens into one
   “cost” counter.
2. Preserve originals and newly documented usage fields. An absent write counter
   is unknown until its route/schema is qualified; it is not automatically zero.
   Anthropic-style `input_tokens` is not OpenAI-style total input. Run arithmetic
   and tariff projections in the tertiary consumer, never producer/recorder.
3. Study splitting stable instructions from changing CURRENT_TASKS/CONTEXT_BUDGET
   before placing markers. Public schemas permit useful structures, but the
   native inventory's owned/compatibility conversion difference needs resolution
   before claiming the final wire prefix is stable on both paths.
4. Keep cache policy provider/model/route specific. Qualification should answer
   the exact new-field acceptance, usage partition and marker behavior for the
   selected account; this pass authorizes no live calls. Document/schema conflicts
   remain explicit inputs to that work.
5. Compare full workflow quality, ordinary/write/read/output tokens, latency,
   retries and actual billing/quota observations independently. Prewarming,
   compaction and extra routing requests consume resources too. None is selected
   merely to raise a cache-hit percentage.

Only this report was written. Sources were read through official search/open and
unauthenticated public-document retrieval; no reference implementation acquired.
