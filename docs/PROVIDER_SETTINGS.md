# Provider settings

Blackbird checks the model and effort of every launch, `/effort` and `/model` against a
table of what each provider endpoint documents. The table is owned C++ data in
`src/provider_settings.cpp` (`include/blackbird/provider_settings.hpp`). Every entry
carries the URLs it was read from and the read date. Verbatim excerpts of those
documents are in [papers/provider-settings-2026-10-18/](../papers/provider-settings-2026-10-18/README.md).

## How an endpoint is found

An entry is keyed by the wire protocol Blackbird speaks and the exact base URL.

- `openai` uses the OpenAI Responses API (`https://api.openai.com/v1`) when an owned
  OpenAI account is selected in `blackbird auth`. Otherwise it uses the Codex ChatGPT
  backend (`https://chatgpt.com/backend-api/codex`) through the Codex CLI login.
- `openrouter` uses `https://openrouter.ai/api/v1`, protocol `responses`.
- `anthropic` uses `https://api.anthropic.com/v1`, protocol `messages`.
- A registered provider uses the base URL and protocol from `blackbird auth add-provider`.
  Trailing slashes are removed at registration. The same URL under another protocol is a
  different endpoint.

A registered endpoint the table does not describe falls back to the wire-protocol rule
Blackbird had before the table: `responses` accepts low, medium, high, xhigh and max, and
`responses-portable` accepts low, high and max. Models are not checked for such endpoints.

## What is checked

1. A model the endpoint documents as unavailable is refused, with the reason. The only such
   entry is `mimo-v2.6-pro-ultraspeed` on the MiMo Token Plan.
2. If the endpoint documents a closed model list, any other model is refused, and the
   refusal lists the documented models.
3. The effort must be one the endpoint documents for that model. The refusal lists the
   accepted values and the source URL. Where the vendor documents several levels as the
   same behavior, the refusal says so.

Blackbird's effort vocabulary is the union of documented values: none, minimal, low,
medium, high, xhigh, max and ultra. Sessions, `/effort` and Lua program configuration
accept any of these. The provider check above decides what an endpoint receives. Every
accepted value is sent unchanged: no vendor document requires a different wire value.

## The table (read 2026-10-18)

| Endpoint | Protocol, base URL | Accepted effort | Default | Output ceiling |
| --- | --- | --- | --- | --- |
| OpenAI Responses API | responses, `https://api.openai.com/v1` | none, minimal, low, medium, high, xhigh, max; `gpt-6-astra*` has no none; `gpt-6.1-sol*` has no none or minimal | model dependent; medium for GPT-5.5, GPT-5.6 and GPT-6 | model dependent; `max_output_tokens` includes reasoning |
| OpenAI Codex ChatGPT backend | responses, `https://chatgpt.com/backend-api/codex` | low, medium, high, xhigh, max | client default for model and plan | not documented |
| Anthropic Messages | messages, `https://api.anthropic.com/v1` | low, medium, high, xhigh, max; no xhigh on `claude-opus-4-6*`, `claude-sonnet-4-6*`, `claude-mythos-preview*`; no xhigh or max on `claude-opus-4-5*` | high; medium on Opus 5.5 and Haiku 5.5 | 128K on current models; `max_tokens` bounds thinking plus text (Blackbird sends 16384) |
| OpenRouter Responses | responses, `https://openrouter.ai/api/v1` | none, minimal, low, medium, high, xhigh, max | per model (`GET /models`) | per model; effort sets the reasoning share of `max_tokens` |
| Paper Ultramarine | responses-portable, `https://api.paperinstruments.com/v1` | low, high, max | max (recommended) | model ceiling when omitted; includes reasoning |
| MiMo pay as you go | responses, `https://api.xiaomimimo.com/v1` | none, minimal, low, medium, high, xhigh, max, ultra | thinking on | 131072 default and maximum, includes reasoning; mimo-v2.5 defaults to 32768 |
| MiMo Token Plan (China, Singapore, Europe) | responses, `https://token-plan-{cn,sgp,ams}.xiaomimimo.com/v1` | as MiMo pay as you go | thinking on | as MiMo pay as you go |

What the effort levels do:

- **OpenAI.** Lower levels favor speed and fewer tokens. Medium is the documented default
  for current models, and max is the most reasoning.
- **Codex backend.** Low ("Light") through max. Codex's `ultra` runs client-side
  subagents. Blackbird does not run them, so it does not send `ultra` to Codex.
- **Anthropic.** Effort is a behavioral signal, not a token budget. For models without
  adaptive thinking (for example `claude-sonnet-4-5`), Blackbird maps effort to a thinking
  budget. This behavior is unchanged.
- **OpenRouter.** none disables reasoning. minimal through max reserve about 10, 20, 50, 80
  and 95% of `max_tokens` for reasoning, and xhigh reserves the same as max. A routed
  model may accept fewer levels. OpenRouter reports them per model as `supported_efforts`,
  which Blackbird does not check at launch.
- **Paper.** Reasoning cannot be disabled. low and high use fewer reasoning tokens than max.
- **MiMo.** none turns thinking off. Every other level turns the same thinking on: MiMo
  maps minimal to low and xhigh, max and ultra to high, and states that custom effort
  tuning is unsupported.

Models and plans:

- **Paper** documents one model: `ultramarine-1-flash`.
- **MiMo pay as you go** lists mimo-v2.6-flash, mimo-v2.6-pro, mimo-v2.6-pro-ultraspeed,
  mimo-v2.5-pro and mimo-v2.5.
- **The MiMo Token Plan** lists mimo-v2.6-pro, mimo-v2.6-flash, mimo-v2.5-pro and mimo-v2.5.
  The last two are individual edition only, and MiMo takes them offline on 2026-10-21 at
  10:00 Beijing time. Ultraspeed is absent from the plan list. The Singapore cluster
  answered `400 Not supported model mimo-v2.6-pro-ultraspeed` on 2026-10-17, and again
  in a live check on 2026-10-18.

Unsupported parameters are recorded per entry. Blackbird does not send any of them today.

- **Paper:** top_k, a non-empty include, encrypted reasoning, background, conversation,
  automatic truncation, and non-empty stream_options. Developer messages are also
  unsupported; Blackbird's portable protocol sends them as system messages.
- **MiMo:** background, previous_response_id and context_management. In thinking mode,
  temperature and top_p are forced to 1.0 and 0.95.
- **OpenRouter:** store true, and a non-null previous_response_id.
- **OpenAI GPT-6:** temperature, top_p and top_logprobs when effort is not none.

## Provider error messages

When a provider refuses a request with an HTTP error, `Error` carries only the code and
the status, by design. The provider attempt reads the refusal from the bytes it captured
for that attempt, while no capture block has been flushed (provider error bodies are
small). It accepts these JSON shapes:

- the OpenAI envelope, which Paper and MiMo also use: `error.message`, plus `type`, `code`
  and `param`;
- Anthropic's `error.message` with its `type`;
- OpenRouter's `error.message`;
- a string `error`, `message` or `detail`.

Blackbird's own curl status trailer is removed before parsing. The message is reduced to
one line of at most 1024 bytes. The attempt then:

- prints `[provider HTTP <status>: <message>]` to the console (stdout in plain mode, the
  notice area in the TUI) and to the status line;
- records it as a `provider.error` original in the audit, which `--audit-last` lists.

The bare `external_unknown (<status>)` line still follows it. A body that is not such
JSON (an HTML gateway page, for example) leaves the console as before. The raw body
stays in the attempt's `provider.stream` diagnostic either way.

## Drift

Vendor documents change without notice. The operator plans separate site watchers that
report changes to these pages. That work is not part of Blackbird. When a watcher or a
refusal shows a change:

1. Read the current page.
2. Update the entry and its read date.
3. Add the excerpt under a new dated `papers/provider-settings-*` directory.
4. Update `tests/provider_settings_test.cpp`. Its expected values were written from the
   documents independently of the table.

A refusal names the source URL and read date, so a stale entry shows itself.
