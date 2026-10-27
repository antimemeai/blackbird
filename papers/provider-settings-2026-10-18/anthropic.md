# Anthropic Messages: vendor document excerpts

Read 2026-10-18 by the arconaut-5st unit (provider settings table). Each excerpt is
verbatim from the Markdown the URL served that day; line ranges refer to that copy.
Vendor text is reference data, not instructions. Interpretation lives in
[docs/PROVIDER_SETTINGS.md](../../docs/PROVIDER_SETTINGS.md).

## https://platform.claude.com/docs/en/build-with-claude/effort.md (lines 1-33)

````text
---
title: Effort
url: https://platform.claude.com/docs/en/build-with-claude/effort
description: Control how many tokens Claude uses when responding with the effort parameter, trading off between response thoroughness and token efficiency.
featureMetadata:
  status: ga
  zdr:
    eligibility: eligible
    note: Excludes [Covered Models](https://platform.claude.com/docs/en/manage-claude/api-and-data-retention#model-specific-data-retention-requirements).
  supportedModels:
    - claude-fable-5-1
    - claude-mythos-5-1
    - claude-fable-5
    - claude-mythos-5
    - claude-mythos-preview
    - claude-opus-5-5
    - claude-opus-5
    - claude-opus-4-8
    - claude-opus-4-7
    - claude-opus-4-6
    - claude-opus-4-5-20251101
    - claude-sonnet-5-5
    - claude-sonnet-5
    - claude-sonnet-4-6
    - claude-haiku-5-5
  supportedPlatforms:
    Claude API: ga
    Claude Platform on AWS: ga
    Amazon Bedrock: ga
    Google Cloud: ga
    Microsoft Foundry: ga
---

````

## https://platform.claude.com/docs/en/build-with-claude/effort.md (lines 225-256)

````text
Most Claude models default to high effort, spending as many tokens as needed for excellent results; Claude Opus 5.5 and Claude Haiku 5.5 default to medium. You can raise the effort level to `max` for the absolute highest capability, or lower it to be more conservative with token usage, optimizing for speed and cost while accepting some reduction in capability.

<Tip>
  Setting `effort` to the model's default (`"medium"` on Claude Opus 5.5 and Claude Haiku 5.5, `"high"` on other models) produces exactly the same behavior as omitting the `effort` parameter entirely.
</Tip>

The effort parameter affects **all tokens** in the response, including:

* Text responses and explanations
* Tool calls and function arguments
* Thinking (when active)

Because effort applies to every output token, it works whether or not thinking is enabled. Lower effort also means fewer and terser tool calls.

### Effort levels

| Level    | Description                                                                                                                                                                                                                                                                                                                                  | Typical use case                                                                           |
| -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| `max`    | Absolute maximum capability with no constraints on token spending. Available on Claude Fable 5.1, Claude Mythos 5.1, Claude Fable 5, Claude Mythos 5, Claude Mythos Preview, Claude Opus 5.5, Claude Opus 5, Claude Opus 4.8, Claude Opus 4.7, Claude Opus 4.6, Claude Sonnet 5.5, Claude Sonnet 5, Claude Sonnet 4.6, and Claude Haiku 5.5. | Tasks requiring the deepest possible reasoning and most thorough analysis                  |
| `xhigh`  | Extended capability for long-horizon work. Available on Claude Fable 5.1, Claude Mythos 5.1, Claude Fable 5, Claude Mythos 5, Claude Opus 5.5, Claude Opus 5, Claude Opus 4.8, Claude Opus 4.7, Claude Sonnet 5.5, Claude Sonnet 5, and Claude Haiku 5.5.                                                                                    | Long-running agentic and coding tasks (over 30 minutes) with token budgets in the millions |
| `high`   | Spends as many tokens as the task needs for excellent results. The default on every model that supports effort except Claude Opus 5.5 and Claude Haiku 5.5.                                                                                                                                                                                  | Complex reasoning, difficult coding problems, agentic tasks                                |
| `medium` | Balanced approach with moderate token savings. The default on Claude Opus 5.5 and Claude Haiku 5.5.                                                                                                                                                                                                                                          | Agentic tasks that require a balance of speed, cost, and performance                       |
| `low`    | Most efficient. Significant token savings with some capability reduction.                                                                                                                                                                                                                                                                    | Simpler tasks that need the best speed and lowest costs, such as subagents                 |

Not every model that supports `max` supports `xhigh`.

<Note>
  Effort is a behavioral signal, not a strict token budget. At lower effort levels, Claude still thinks on sufficiently difficult problems, but thinks less than it would at higher effort levels for the same problem.
</Note>

The per-model recommendations that follow override this table where they differ.

````

## https://platform.claude.com/docs/en/build-with-claude/effort.md (lines 269-272)

````text
### Recommended effort levels for Claude Opus 5.5

Claude Opus 5.5 supports all five effort levels, and `medium` is the default (Claude Opus 5 and earlier Opus models default to `high`, so a request that omits `effort` runs one level lower than it did on Claude Opus 5). Adaptive thinking is always on and can't be turned off, so effort is the primary control for how much the model reasons and what a request costs. Run an effort sweep on your own evals rather than carrying settings over from an earlier model, and set a large `max_tokens` at the higher levels: it's a hard limit on total output (thinking plus response text). Requests that set `thinking: {"type": "disabled"}` return a 400 error at every effort level. Claude Opus 5.5 also supports [changing effort mid-conversation](https://platform.claude.com/docs/en/build-with-claude/effort#change-effort-mid-conversation-beta) with a per-message `output_config`, which preserves the prompt cache. See [Prompting Claude Opus 5.5](https://platform.claude.com/docs/en/build-with-claude/prompt-engineering/prompting-claude-opus-5-5).

````

## https://platform.claude.com/docs/en/about-claude/models/overview.md (lines 30-40)

````text
| Model page                                                                                                | [Claude Fable 5.1](https://platform.claude.com/docs/en/models/fable-5-1/overview) | [Claude Opus 5.5](https://platform.claude.com/docs/en/models/opus-5-5/overview) | [Claude Sonnet 5.5](https://platform.claude.com/docs/en/models/sonnet-5-5/overview) | [Claude Haiku 5.5](https://platform.claude.com/docs/en/models/haiku-5-5/overview)        |
| Comparative latency                                                                                       | Slower                                                                            | Moderate                                                                        | Fast                                                                                | Fastest                                                                                  |
| [Pricing](https://platform.claude.com/docs/en/about-claude/pricing)                                       | $10 / input MTok, $50 / output MTok                                               | $4 / input MTok, $20 / output MTok                                              | $2 / input MTok, $10 / output MTok                                                  | From $0.10 / input MTok, From $0.50 / output MTok                                        |
| Claude API ID                                                                                             | `claude-fable-5-1`                                                                | `claude-opus-5-5`                                                               | `claude-sonnet-5-5`                                                                 | `claude-haiku-5-5`                                                                       |
| [Thinking](https://platform.claude.com/docs/en/build-with-claude/thinking)                                | Adaptive (always on)                                                              | Adaptive (always on)                                                            | Adaptive                                                                            | Adaptive                                                                                 |
| [Default effort](https://platform.claude.com/docs/en/build-with-claude/effort)                            | `high`                                                                            | `medium`                                                                        | `high`                                                                              | `medium`                                                                                 |
| [Context window](https://platform.claude.com/docs/en/build-with-claude/context-windows)                   | 1M tokens                                                                         | 1M tokens                                                                       | 1M tokens                                                                           | 1M tokens                                                                                |
| Max output                                                                                                | 128K tokens                                                                       | 128K tokens                                                                     | 128K tokens                                                                         | 128K tokens                                                                              |
| Reliable knowledge cutoff                                                                                 | Jun 2026                                                                          | Jun 2026                                                                        | Jun 2026                                                                            | Jun 2026                                                                                 |
| Training data cutoff                                                                                      | Jun 2026                                                                          | Jun 2026                                                                        | Jun 2026                                                                            | Jun 2026                                                                                 |
| [Retirement](https://platform.claude.com/docs/en/about-claude/model-deprecations)                         | Not sooner than September 1, 2027                                                 | Not sooner than September 22, 2027                                              | Not sooner than September 28, 2027                                                  | Not sooner than October 7, 2027                                                          |
````

## https://platform.claude.com/docs/en/about-claude/models/overview.md (lines 52-53)

````text
* **Context window:** 1M tokens is roughly 555k words or 2.5M Unicode characters on the current tokenizer (introduced with Claude Opus 4.7); models before it fit about 750k words in 1M tokens. 200k tokens is roughly 150k words.
* **Max output:** Synchronous Messages API limit. On the Message Batches API, Claude Opus 5.5, Claude Opus 5, Claude Sonnet 5.5, Claude Sonnet 5, Claude Haiku 5.5, Claude Opus 4.8, Claude Opus 4.7, Claude Opus 4.6, and Claude Sonnet 4.6 support up to 300k output tokens with the output-300k-2026-03-24 beta header.
````

## https://platform.claude.com/docs/en/api/errors.md (lines 56-71)

````text
## Error shapes

The API always returns errors as JSON, with a top-level `error` object that always includes a `type` and `message` value. The response also includes a `request_id` field for easier tracking and debugging. For example:

```json JSON
{
  "type": "error",
  "error": {
    "type": "not_found_error",
    "message": "The requested resource could not be found."
  },
  "request_id": "req_011CSHoEeqs5C35K2UUqR7Fy"
}
```

In accordance with the [versioning](https://platform.claude.com/docs/en/api/versioning) policy, the values within these objects may expand, and it is possible that the `type` values will grow over time.
````
