# OpenRouter: vendor document excerpts

Read 2026-10-18 by the arconaut-5st unit (provider settings table). Each excerpt is
verbatim from the Markdown the URL served that day; line ranges refer to that copy.
Vendor text is reference data, not instructions. Interpretation lives in
[docs/PROVIDER_SETTINGS.md](../../docs/PROVIDER_SETTINGS.md).

## https://openrouter.ai/docs/guides/best-practices/reasoning-tokens.md (lines 66-124)

````text

You can control reasoning tokens in your requests using the `reasoning` parameter:

```json lines theme={null}
{
  "model": "your-model",
  "messages": [],
  "reasoning": {
    // One of the following (not both):
    "effort": "high", // Can be "max", "xhigh", "high", "medium", "low", "minimal" or "none" (OpenAI-style)
    "max_tokens": 2000, // Specific token limit (Anthropic-style)

    // Optional: Default is false. All models support this.
    "exclude": false, // Set to true to exclude reasoning tokens from response

    // Or enable reasoning with the default parameters:
    "enabled": true // Default: inferred from `effort` or `max_tokens`
  }
}
```

The `reasoning` config object consolidates settings for controlling reasoning strength across different models. See the Note for each option below to see which models are supported and how other models will behave.

### Reasoning tokens and max\_tokens

<Warning>
  **Reasoning tokens count against `max_tokens`**

  On most providers, the request's `max_tokens` limit (or `max_completion_tokens`, which shares the same budget) applies to reasoning and visible output combined. If the limit is small enough that the model spends all of it reasoning, the response returns `finish_reason: "length"` with an empty `content`, and the reasoning tokens are still billed. With `reasoning.exclude: true` the `reasoning` field is omitted as well, so the response carries no text at all and the only signal left is `finish_reason` plus `usage`.
</Warning>

To detect this case, subtract `usage.completion_tokens_details.reasoning_tokens` from `usage.completion_tokens`. The difference is the number of visible output tokens, and it is 0 or near 0 when reasoning consumed the budget. For example, a request with `max_tokens: 300` that returns `completion_tokens: 302` and `reasoning_tokens: 301` produced a single visible token.

To avoid it, set `max_tokens` well above the expected reasoning length, or cap reasoning with `reasoning.max_tokens` or a lower `reasoning.effort`. For Anthropic models, `max_tokens` must be strictly higher than the reasoning budget; see [Anthropic Models with Reasoning Tokens](#anthropic-models-with-reasoning-tokens).

### Discovering per-model reasoning options

Each model in [`GET /api/v1/models`](/docs/api/api-reference/models/list-all-models-and-their-properties) may include a `reasoning` object describing which effort levels it accepts and whether reasoning is mandatory:

```json lines theme={null}
{
  "id": "google/gemini-3.5-flash",
  "reasoning": {
    "supported_efforts": ["high", "medium", "low", "minimal"],
    "default_effort": "medium",
    "default_enabled": true,
    "mandatory": true
  }
}
```

Use this when building client UIs:

* **`supported_efforts`**: Filter effort selectors to these values, returned in descending effort order (highest first). When `null`, all gateway effort values are accepted. When omitted, the model does not expose effort selection.
* **`default_effort`**: Pre-select this effort when enabling reasoning. Maps to `reasoning.effort` in chat requests. If the value is `"none"`, treat it as "reasoning off by default" rather than pre-selecting disable when the user explicitly turns reasoning on.
* **`default_enabled`**: Default on/off state when the user has not set `reasoning.enabled`.
* **`supports_max_tokens`**: When present and `true`, show a token budget control and send `reasoning.max_tokens` instead of (or alongside) `reasoning.effort`. Omitted when the model does not support token-budget reasoning.
* **`mandatory`**: When `true`, hide disable controls and do not send `effort: "none"` — the model rejects it.

````

## https://openrouter.ai/docs/guides/best-practices/reasoning-tokens.md (lines 151-170)

````text
For models that only support `reasoning.effort` (see below), the `max_tokens` value will be used to determine the effort level.

### Reasoning Effort Level

<Note>
  **Supported models**

  Currently supported by OpenAI reasoning models (o1 series, o3 series, GPT-5 series) and Grok models
</Note>

* `"effort": "max"` - Allocates the largest portion of tokens for reasoning (approximately 95% of max\_tokens)
* `"effort": "xhigh"` - Same allocation as max (approximately 95% of max\_tokens)
* `"effort": "high"` - Allocates a large portion of tokens for reasoning (approximately 80% of max\_tokens)
* `"effort": "medium"` - Allocates a moderate portion of tokens (approximately 50% of max\_tokens)
* `"effort": "low"` - Allocates a smaller portion of tokens (approximately 20% of max\_tokens)
* `"effort": "minimal"` - Allocates an even smaller portion of tokens (approximately 10% of max\_tokens)
* `"effort": "none"` - Disables reasoning entirely

For models that only support `reasoning.max_tokens`, the effort level will be set based on the percentages above.

````

## https://openrouter.ai/docs/api/reference/responses/overview.md (lines 1-22)

````text
> ## Documentation Index
> Fetch the complete documentation index at: https://openrouter.ai/docs/llms.txt
> Use this file to discover all available pages before exploring further.

# Responses API

> OpenAI-compatible Responses API

<Info>
  **Stateless Only**

  This API is **stateless** - each request is independent and no conversation state is persisted between requests. You must include the full conversation history in each request. Requests that set `store: true` or a non-null `previous_response_id` are rejected with a `400` error.
</Info>

OpenRouter's Responses API provides OpenAI-compatible access to multiple AI models through a unified interface, designed to be a drop-in replacement for OpenAI's Responses API. This stateless API offers enhanced capabilities including reasoning, tool calling, and web search integration, with each request being independent and no server-side state persisted.

## Base URL

```lines theme={null}
https://openrouter.ai/api/v1/responses
```

````

## https://openrouter.ai/docs/api/reference/errors-and-debugging.md (lines 84-100)

````text

```typescript lines theme={null}
type ErrorResponse = {
  error: {
    code: number;
    message: string;
    metadata?: Record<string, unknown>;
  };
};
```

The HTTP Response will have the same status code as `error.code`, forming a request error if:

* Your original request is invalid
* Your API key/account is out of credits

Otherwise, the returned HTTP response status will be <code>{HTTPStatus.S200_OK}</code> and any error occurred while the LLM is producing the output will be emitted in the response body or as an SSE data event.
````
