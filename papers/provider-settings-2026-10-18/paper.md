# Paper Instruments Ultramarine: vendor document excerpts

Read 2026-10-18 by the arconaut-5st unit (provider settings table). Each excerpt is
verbatim from the Markdown the URL served that day; line ranges refer to that copy.
Vendor text is reference data, not instructions. Interpretation lives in
[docs/PROVIDER_SETTINGS.md](../../docs/PROVIDER_SETTINGS.md).

## https://docs.paperinstruments.com/docs/ultramarine/responses.md (lines 1-12)

````text
# Responses (/docs/ultramarine/responses)

```http
POST https://api.paperinstruments.com/v1/responses
Authorization: Bearer $PAPERINSTRUMENTS_API_KEY
Content-Type: application/json
```

Use the OpenAI SDK's `responses` client. Requests use the
[Responses format](https://developers.openai.com/api/reference/resources/responses/methods/create)
with the limits below. Unsupported fields return `400`; `error.param` identifies
the field.
````

## https://docs.paperinstruments.com/docs/ultramarine/responses.md (lines 180-230)

````text
With `store: false`, pass the full history on each follow-up using
`result.to_input_list()` in Python or `result.history` in Node.js.
Keep `include` empty because Ultramarine rejects encrypted reasoning.
Use the agent's `instructions` field for guidance; `developer` messages are unsupported.
The examples disable OpenAI tracing, so you only need your Paper Instruments API key.

## Parameters

| Parameter                                               | Accepted values                                                                                                                     |
| ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `model`                                                 | Required. `ultramarine-1-flash`.                                                                                                    |
| `input`                                                 | A string or an array of Responses input items. Default: `[]`. See [input and tools](#input-and-tools).                              |
| `instructions`                                          | Optional string. Send instructions again on subsequent requests.                                                                    |
| `reasoning.effort`                                      | `low`, `high`, or `max`. Default: `max`, which we recommend for best results. Reasoning cannot be disabled.                         |
| `max_output_tokens`                                     | Positive integer, including reasoning. Omit to use the model's output ceiling.                                                      |
| `store`                                                 | Boolean. &#x2A;*Default: `false`**, unlike OpenAI's `true`. Set `true` to save a response for retrieval and continuation.           |
| `previous_response_id`                                  | ID of a stored response from your organization. Requires `store: true`.                                                             |
| `stream`                                                | Boolean. Default: `false`. Use `true` for typed server-sent events.                                                                 |
| `tools`                                                 | Array of `function`, `mcp`, or `web_search` definitions. See below.                                                                 |
| `tool_choice`                                           | `auto`, `none`, `required`, or `{"type":"function","name":"your_function"}`. Named choices must match a supplied function.          |
| `parallel_tool_calls`                                   | Boolean.                                                                                                                            |
| `max_tool_calls`                                        | Integer, 1–15; default: `4`. Only accepted with MCP or web search.                                                                  |
| `text.format`                                           | `{"type":"text"}`, `{"type":"json_object"}`, or `json_schema` with `name`, `schema`, optional `description`, and optional `strict`. |
| `temperature`                                           | Number, 0–2. Default and recommended: `1`.                                                                                          |
| `top_p`                                                 | Number, 0–1. Default and recommended: `0.95`.                                                                                       |
| `metadata`                                              | Up to 16 string pairs, or 15 when using `previous_response_id`. Keys: at most 64 characters; values: at most 512.                   |
| `prompt_cache_key`                                      | Optional string cache hint. Cache hits are not guaranteed.                                                                          |
| `background`, `truncation`, `include`, `stream_options` | Only `false`, `"disabled"`, `[]`, and `{}`, respectively. Omit these fields unless needed.                                          |

Request bodies are limited to 20 MiB. Each request produces one response.
Responses does not accept `top_k`; omit it entirely.

## Input and tools

Message roles are `system`, `user`, and `assistant`. Text can be a string or
`input_text` parts:

```json
{
  "input": [
    {"role": "user", "content": [{"type": "input_text", "text": "Explain EBITDA."}]}
  ]
}
```

You can replay assistant `output_text` and `refusal` parts, plaintext reasoning
before its assistant message or function call, and `function_call` items.
Images, audio, files, encrypted reasoning, and
item references are unsupported.

Function definitions use the flat Responses shape:
````

## https://docs.paperinstruments.com/docs/ultramarine/responses.md (lines 345-410)

````text
    console.log(event.response.status, event.response.usage);
  }
}
```

Check `status`, including after HTTP `200`. Output limits return `incomplete`
with `incomplete_details.reason: "max_output_tokens"`, including when the context
window fills first. A function call cut off this way is `incomplete`; do not run it. A failure can return
`failed` with an `error`, or interrupt a stream. A closed stream without a
terminal event does not establish completion.

Read `usage.input_tokens`, `usage.input_tokens_details.cached_tokens`,
`usage.output_tokens`, `usage.output_tokens_details.reasoning_tokens`, and
`usage.total_tokens`. Cached input is a subset of input; reasoning is a subset
of output. Stream usage is in the terminal response event; unknown usage is
`null`. Stored retrieval returns the same generation's usage.

## Conversation caching

You can use the optional `x-session-id` header for sticky routing. See
[Sticky sessions](/docs/ultramarine/sticky-sessions) for SDK examples, routing
without a session header, and cache behavior.

## Unsupported features and errors

Errors use the OpenAI envelope. Ultramarine's error
codes and validation rules below are not all standard OpenAI Responses behavior.
Errors also include `error.request_id`; responses carry `x-request-id`.

| Request                                                                                                                          | HTTP status / `error.code`          | `error.param`                             |
| -------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------- | ----------------------------------------- |
| `background: true`                                                                                                               | `400 / unsupported_parameter`       | `background`                              |
| `conversation`, non-empty `include`, encrypted reasoning, automatic truncation, non-empty `stream_options`                       | `400 / unsupported_parameter`       | The field or indexed item.                |
| `developer` messages, non-text content, unsupported input items                                                                  | `400 / unsupported_parameter`       | The input field, such as `input[0].role`. |
| `file_search`, `code_interpreter`, `image_generation`, or other unsupported tool types                                           | `400 / unsupported_parameter`       | `tools[i].type`                           |
| Multiple MCP servers, approval callbacks, extra web-search options, or deferred tool loading                                     | `400 / unsupported_parameter`       | `tools` or the tool field.                |
| Chat Completions fields (`messages`, `reasoning_effort`, `response_format`, `max_tokens`, `n`), `state`, or other unknown fields | `400 / unsupported_parameter`       | The unsupported field.                    |
| Unsupported effort, out-of-range values, malformed schemas, or unmatched function results                                        | `400 / invalid_value`               | The invalid field.                        |
| `previous_response_id` with `store: false`                                                                                       | `400 / invalid_value`               | `store`                                   |
| `max_tool_calls` without MCP or web search                                                                                       | `400 / unsupported_parameter`       | `max_tool_calls`                          |
| Response listing, input-item listing, or cancellation                                                                            | `400 / unsupported_parameter`       | `response_id`                             |
| Compaction or input-token counting                                                                                               | `400 / unsupported_parameter`       | `operation`                               |
| Invalid, expired, deleted, unstored, or inaccessible response ID                                                                 | `404 / response_not_found`          | `response_id` or `previous_response_id`   |
| Hosted tools unavailable                                                                                                         | `503 / hosted_tools_not_configured` | `null`                                    |
| Storage or hosted execution unavailable                                                                                          | `503`                               | `null`                                    |

For example, `background: true` returns:

```json
{
  "error": {
    "type": "invalid_request_error",
    "code": "unsupported_parameter",
    "message": "Background execution is not supported. Set background=false; use stream=true for incremental output.",
    "param": "background",
    "request_id": "req_..."
  }
}
```

A hosted-tool call limit returns `status: "incomplete"` with
`incomplete_details.reason: null` and the extension
`incomplete_details.provider_reason: "max_tool_calls"`.
See [Errors](/docs/ultramarine/errors) for authentication, billing, rate limits,
upstream failures, and retry handling.
````

## https://docs.paperinstruments.com/docs/ultramarine/default-sampling-config.md (lines 1-45)

````text
# Default Sampling Config (/docs/ultramarine/default-sampling-config)

> **Important: use the recommended defaults**
>
> Use `temperature=1` and `top_p=0.95`. For Chat Completions and Messages, use
> `top_k=-1` (disabled). Omit sampling fields to use these defaults.

## Sampling defaults by API

| Setting       | Chat Completions | Responses          | Messages        |
| ------------- | ---------------- | ------------------ | --------------- |
| `temperature` | `1`              | `1`                | `1`             |
| `top_p`       | `0.95`           | `0.95`             | `0.95`          |
| `top_k`       | `-1` (disabled)  | Not accepted; omit | `-1` (disabled) |

These are both the API defaults and our recommended values. For Chat
Completions and Messages, `top_k=0` also disables top-k filtering. Responses
does not accept `top_k`; leave it out of Responses requests.

Chat Completions also accepts `min_p`, `presence_penalty`, `frequency_penalty`,
`repetition_penalty`, `logit_bias`, and `seed`. Leave these unset to start;
tune them only after testing on your tasks. See the
[Chat Completions parameters](/docs/ultramarine/chat-completions#parameters)
for the supported controls.

## Reasoning effort

We default to `max` thinking (reasoning effort) and recommend it for best results.
Use the field for your API:

| API                                                              | Field                  | Default and recommended value |
| ---------------------------------------------------------------- | ---------------------- | ----------------------------- |
| [Chat Completions](/docs/ultramarine/chat-completions#reasoning) | `reasoning_effort`     | `"max"`                       |
| [Responses](/docs/ultramarine/responses#parameters)              | `reasoning.effort`     | `"max"`                       |
| [Messages](/docs/ultramarine/messages#parameters)                | `output_config.effort` | `"max"`                       |

Test `high` or `low` if you need to use fewer reasoning tokens. Ultramarine
requires reasoning; you cannot disable it.

For prompt examples and the code-execution environment, see
[Prompting and packages](/docs/ultramarine/prompting-and-packages).
````
