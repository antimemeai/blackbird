# OpenAI Responses API and Codex: vendor document excerpts

Read 2026-10-18 by the arconaut-5st unit (provider settings table). Each excerpt is
verbatim from the Markdown the URL served that day; line ranges refer to that copy.
Vendor text is reference data, not instructions. Interpretation lives in
[docs/PROVIDER_SETTINGS.md](../../docs/PROVIDER_SETTINGS.md).

## https://developers.openai.com/api/reference/resources/responses/methods/create.md (lines 4465-4467)

````text
- `max_output_tokens: optional number or null`

  An upper bound for the number of tokens that can be generated for a response, including visible output tokens and [reasoning tokens](/api/docs/guides/reasoning).
````

## https://developers.openai.com/api/reference/resources/responses/methods/create.md (lines 4858-4866)

````text
  - `effort: optional ReasoningEffort or null`

    Constrains effort on reasoning for reasoning models. Currently supported
    values are `none`, `minimal`, `low`, `medium`, `high`, `xhigh`, and `max`.
    Reducing reasoning effort can result in faster responses and fewer tokens
    used on reasoning in a response. Not all reasoning models support every
    value. See the
    [reasoning guide](/api/docs/guides/reasoning)
    for model-specific support.
````

## https://developers.openai.com/api/docs/guides/reasoning.md (lines 186-222)

````text
## Reasoning effort

The `reasoning.effort` parameter guides the model on how much to think when performing a task.

Supported values are model-dependent and can include `none`, `minimal`, `low`, `medium`, `high`, `xhigh`, and `max`. Lower effort favors speed and lower token usage, while at higher effort the model thinks more completely to provide higher quality responses. The models also reason adaptively across reasoning efforts, using fewer tokens for simpler tasks and thinking harder for complex tasks.

[GPT-6 Astra](https://developers.openai.com/api/docs/models/gpt-6-astra) does not support `none` reasoning
  effort. Setting `reasoning.effort` (Responses) or `reasoning_effort` (Chat
  Completions) to `none` returns HTTP 400. [GPT-6.1 Sol](https://developers.openai.com/api/docs/guides/latest-model?model=gpt-6-astra#gpt-61-sol)
  does not support `none` or `minimal` and defaults to `medium`.

Use the [Responses API](https://developers.openai.com/api/docs/guides/migrate-to-responses) for function
calling. Chat Completions does not support function calling with GPT-6 Astra or
GPT-6.1 Sol.

Defaults are also model-dependent rather than universal. `gpt-5.5` defaults to `medium` reasoning effort. This is the best starting point for `gpt-5.5`’s full balance of quality, reliability and performance.

| Effort   | Best for                                                                                                                                                                                                                                                                                                                                                             |
| -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `none`   | Latency-critical tasks that do not benefit from any reasoning or multi-chained tool calls. For latency-sensitive use cases with `gpt-5.5`, we recommend trying `low` to begin with and then moving to `none` if required.<br /><br />Common use cases include voice, fast information retrieval, and classification.                                                 |
| `low`    | Efficient reasoning with a modest latency increase. Ideal for use cases requiring tool-use, planning, search, or multi-step decision making, while optimizing for speed and cost.<br /><br />Common use cases include data analysis, drafting, execution-oriented coding, and customer support / chat assistant workflows.                                           |
| `medium` | When quality and reliability matter, and the task involves planning, complex reasoning, and judgement. Default configuration for most workloads, and a well-balanced point on the pareto curve of latency, performance and cost.<br /><br />Common use cases include agentic coding, research, working with spreadsheets & slides, and delegating long-horizon work. |
| `high`   | Hard reasoning, complex debugging, deep planning, and high-value tasks where quality and intelligence matters more than latency. Recommended for complex workflows and agentic tasks.<br /><br />Common use cases include agentic coding, long-horizon research, and knowledge work. Depending on the complexity of the task, evaluate both `medium` and `high`.     |
| `xhigh`  | Deep research, asynchronous workflows and agentic tasks that require long runs. Only use when your evals show a clear benefit that justifies the extra latency and cost.<br /><br />Common use cases include security and code review, enterprise productivity, deeper research tasks, and challenging coding workflows.                                             |
| `max`    | Maximum reasoning for your most complex tasks. If you are currently using `xhigh`, evaluate if `max` results in stronger performance                                                                                                                                                                                                                                 |

For faster time to first visible token in latency-sensitive applications, ask the model to generate a short preamble before continuing with deeper reasoning.

Some models support only a subset of these values, so check the relevant [model page](https://developers.openai.com/api/docs/models) before choosing a setting.

## Reasoning mode

GPT-5.6 and GPT-6 models support `standard` and `pro` reasoning modes in the Responses API. `standard` is the default. Set `reasoning.mode` to `pro` for difficult tasks that need more model work and can tolerate higher latency and token usage.

Reasoning mode and reasoning effort are independent. Mode selects standard or pro execution, while `reasoning.effort` controls how much reasoning the model applies within that mode. If you omit `reasoning.effort`, GPT-5.6 defaults to `medium` in both modes. GPT-6.1 Sol, GPT-6 Sol, and GPT-6 Luna also default to `medium` reasoning effort.

Using pro reasoning mode
````

## https://developers.openai.com/api/docs/guides/latest-model.md (lines 48-58)

````text

### GPT-6.1 Sol

Use GPT-6.1 Sol for complex coding, computer use, and professional work when you
want near-Astra performance at a lower cost. Compare it with Astra on your tasks
to assess the tradeoff between quality and cost.

Set `reasoning.effort` to `low`, `medium` (default), `high`, `xhigh`, or `max`.
Use the Responses API for tool calling. Chat Completions supports requests
without tools. The `none` and `minimal` reasoning efforts are not supported.

````

## https://developers.openai.com/api/docs/guides/latest-model.md (lines 74-78)

````text
## Limitations

- GPT-6 Astra and GPT-6.1 Sol do not support the `none` reasoning effort; GPT-6 Sol and GPT-6 Luna do.
- Fast mode supports EU data residency for GPT-6.1 Sol, GPT-6 Sol, and GPT-6 Luna, but not GPT-6 Astra. [Ultrafast mode](https://developers.openai.com/api/docs/guides/ultrafast-mode) for GPT-6.1 Sol supports US and EU data residency and global processing. GPT-6 Astra Ultrafast supports US data residency and global processing only. See [data residency eligibility](https://developers.openai.com/api/docs/guides/your-data#which-models-and-features-are-eligible-for-data-residency).

````

## https://developers.openai.com/api/docs/guides/latest-model.md (lines 200-207)

````text

Set `model` to `gpt-6-astra`, `gpt-6.1-sol`, or `gpt-6-luna`, then check the following:

- **Reasoning effort:** Preserve your current effective [reasoning effort](https://developers.openai.com/api/docs/guides/reasoning#reasoning-effort) where supported. GPT-6 Astra and GPT-6.1 Sol do not support `none`; use `low` instead. GPT-6 Sol and GPT-6 Luna support `none`. If your existing request uses `minimal`, start with `low` and compare results on representative tasks. Use `reasoning.effort` in Responses or `reasoning_effort` in Chat Completions.
- **Tool calling:** Use the [Responses API](https://developers.openai.com/api/docs/guides/migrate-to-responses#migrating-from-chat-completions). GPT-6 Astra and GPT-6.1 Sol support Chat Completions, but tool calling requires Responses. GPT-6 Sol and GPT-6 Luna support function calling in Chat Completions only with `reasoning_effort: "none"`. Use Responses for reasoning with tools.
- **Unsupported parameters:** When reasoning effort is not `none`, remove `temperature`, `top_p`, and `top_logprobs`. For Chat Completions, also remove `logprobs`. For Responses, remove `message.output_text.logprobs` from `include`.
- **Data residency:** Fast mode supports EU data residency for GPT-6.1 Sol, GPT-6 Sol, and GPT-6 Luna, but not GPT-6 Astra. [Ultrafast mode](https://developers.openai.com/api/docs/guides/ultrafast-mode) for GPT-6.1 Sol supports US and EU data residency and global processing. GPT-6 Astra Ultrafast supports US data residency and global processing only. Fast mode for GPT-6 Astra does not include a latency SLA. See [Fast mode compatibility](https://developers.openai.com/api/docs/guides/fast-mode#is-fast-mode-compatible-with-data-residency-zero-data-retention-and-a-baa).
- **Changing reasoning effort:** If your application changes effort between responses, use `configuration_update` items in standard, single-agent requests. Keep request-level `reasoning.effort` unchanged to preserve the prompt prefix for caching. Check the [compatibility limits](https://developers.openai.com/api/docs/guides/reasoning#change-reasoning-mid-conversation) before adopting this feature.
````

## https://developers.openai.com/codex/models.md (lines 300-305)

````text
workspace requirements.

Use `gpt-6.1-sol`. Available controls depend on your plan, client, and workspace
settings.
Reasoning effort ranges from Light to Ultra. Max and Ultra depend on your settings.

````

## https://developers.openai.com/codex/models.md (lines 357-392)

````text
### Pick a reasoning effort

For GPT-6.1 Sol, start with the reasoning effort available by default in your
client and adjust based on the task. Start with **High** for Luna or **Light**
for Astra.
In configuration, Astra's Light setting is `low`. Increase the effort for tasks
that need more planning, analysis, or checking.

- **Light** in the ChatGPT desktop app, ChatGPT Work on the web, and IDE extension, or **Low** in the
  CLI, suits quick, well-scoped tasks.
- **Medium** balances speed and depth for tasks that need more planning.
- **High** and **Extra High** suit difficult work with multiple steps, sources,
  or tradeoffs.

Reasoning efforts don't map exactly between model generations. Try a familiar task
at a lower setting and adjust based on the result.

### Know when to use Max or Ultra

**Max** gives the selected model more time to reason about a single task. Use it
for the hardest problems, when depth matters more than speed or usage. If you
don't see Max in the ChatGPT desktop app, check your app settings.

**Ultra** uses [subagents](https://learn.chatgpt.com/docs/agent-configuration/subagents) to handle
separate parts of a complex task in parallel. Choose it when you can divide the
work into meaningful parts. Most tasks do not need Max or Ultra.

GPT-6 Luna supports reasoning efforts up to **Max**, but not **Ultra**.

In the CLI, use `/model`, choose a model, then select **More reasoning…**
to see its supported Max or Ultra options.

If your model supports Ultra but it doesn't appear in the desktop app's model
slider, go to **Settings** > **Configuration**, then turn on
**Ultra in model picker slider**.

````

## https://developers.openai.com/codex/config-reference.md (lines 1196-1201)

````text
    {
      key: "model_reasoning_effort",
      type: "string",
      description:
        "Reasoning effort advertised by the selected model, such as `low`, `medium`, `high`, `xhigh`, `max`, or `ultra`. Available levels depend on the model and client.",
    },
````
