# Xiaomi MiMo (pay as you go and Token Plan): vendor document excerpts

Read 2026-10-18 by the arconaut-5st unit (provider settings table). Each excerpt is
verbatim from the Markdown the URL served that day; line ranges refer to that copy.
Vendor text is reference data, not instructions. Interpretation lives in
[docs/PROVIDER_SETTINGS.md](../../docs/PROVIDER_SETTINGS.md).

## https://mimo.mi.com/static/docs/api/chat/responses.md (lines 1-20)

````text
# OpenAI Responses API Compatibility

{/* feishu-style:text-align:left */}
MiMo provides a calling interface compatible with the OpenAI Responses API format. This document covers request parameters, response schemas and code examples.

{/* feishu-style:text-align:left */}
**Compatibility Notes & Limitations:** 

{/* feishu-style:text-align:left */}
This interface aligns with the OpenAI Responses API specification to facilitate quick integration for developers. Only the parameters documented here will be processed normally; undefined parameters will be filtered out and may cause request errors. See below for specific behavioral differences.

- **Incompatible parameters**: Fields such as `background`, `previous_response_id`, and `context_management` are not currently supported. Carrying these parameters in a request will be ignored or trigger an error.

- **Reasoning level control**: `reasoning.effort` controls model reasoning. `none` disables thinking; Every other level enables thinking with identical behavior — The reasoning intensity is not differentiated at this stage. 

## Request Address

```bash
https://api.xiaomimimo.com/v1/responses
```
````

## https://mimo.mi.com/static/docs/api/chat/responses.md (lines 1463-1510)

````text
  },
  {
    "name": "max_output_tokens",
    "type": "integer",
    "isBold": true,
    "required": false,
    "description": "An upper bound for the number of tokens that can be generated for a response, including visible output tokens and reasoning tokens.<br /><ul class=\\"schema-list\\"><li class=\\"schema-list-item\\"><code class=\\"schema-inline-code\\">mimo-v2.6-flash</code>: default <code class=\\"schema-inline-code\\">131072</code></li><li class=\\"schema-list-item\\"><code class=\\"schema-inline-code\\">mimo-v2.6-pro</code>: default <code class=\\"schema-inline-code\\">131072</code></li><li class=\\"schema-list-item\\"><code class=\\"schema-inline-code\\">mimo-v2.6-pro-ultraspeed</code>: default <code class=\\"schema-inline-code\\">131072</code></li><li class=\\"schema-list-item\\"><code class=\\"schema-inline-code\\">mimo-v2.5-pro</code>: default <code class=\\"schema-inline-code\\">131072</code></li><li class=\\"schema-list-item\\"><code class=\\"schema-inline-code\\">mimo-v2.5</code>: default <code class=\\"schema-inline-code\\">32768</code></li></ul>Required range: <code class=\\"schema-inline-code\\">[1, 131072]</code>"
  },
  {
    "name": "model",
    "type": "string",
    "isBold": true,
    "required": true,
    "description": "Model ID used to generate the response.<br />Available options: <code class=\\"schema-inline-code\\">mimo-v2.6-flash</code>, <code class=\\"schema-inline-code\\">mimo-v2.6-pro</code>, <code class=\\"schema-inline-code\\">mimo-v2.6-pro-ultraspeed</code>, <code class=\\"schema-inline-code\\">mimo-v2.5-pro</code>, <code class=\\"schema-inline-code\\">mimo-v2.5</code>"
  },
  {
    "name": "stream",
    "type": "boolean",
    "isBold": true,
    "required": false,
    "defaultValue": "false",
    "description": "If set to true, the model response data will be streamed to the client as it is generated using server-sent events."
  },
  {
    "name": "reasoning",
    "type": "object",
    "isBold": true,
    "required": false,
    "description": "Configuration options for reasoning models.<br /><blockquote class=\\"schema-blockquote\\">Note: During the multi-turn tool calls process in thinking mode, the model returns the reasoning content alongside the tool calls field. To continue the conversation, it is recommended to keep all previous reasoning content in the <code class=\\"schema-inline-code\\">input</code> array for each subsequent request to achieve the best performance.</blockquote><blockquote class=\\"schema-blockquote\\">In thinking mode, the <code class=\\"schema-inline-code\\">mimo-v2.6-flash</code>, <code class=\\"schema-inline-code\\">mimo-v2.6-pro</code>, <code class=\\"schema-inline-code\\">mimo-v2.6-pro-ultraspeed</code>, <code class=\\"schema-inline-code\\">mimo-v2.5-pro</code> and <code class=\\"schema-inline-code\\">mimo-v2.5</code> models do not support customizing the <code class=\\"schema-inline-code\\">temperature</code> and <code class=\\"schema-inline-code\\">top_p</code> parameters. Even if these parameters are passed in, the actual effective values will be forcibly set by the model to its recommended default values of <code class=\\"schema-inline-code\\">1.0</code> and <code class=\\"schema-inline-code\\">0.95</code>.</blockquote>",
    "children": [
      {
        "name": "effort",
        "type": "string",
        "isBold": true,
        "required": true,
        "description": "Constrains effort on reasoning for reasoning models. Reducing reasoning effort can result in faster responses and fewer tokens used on reasoning in a response.<br /><blockquote class=\\"schema-blockquote\\">Custom tuning of reasoning effort is currently unsupported. When set to <code class=\\"schema-inline-code\\">none</code>, reasoning is disabled; all other valid values map to enabled reasoning. The value <code class=\\"schema-inline-code\\">minimal</code> is mapped to <code class=\\"schema-inline-code\\">low</code>. Values <code class=\\"schema-inline-code\\">xhigh</code>, <code class=\\"schema-inline-code\\">max</code> and <code class=\\"schema-inline-code\\">ultra</code> are mapped to <code class=\\"schema-inline-code\\">high</code>.</blockquote><ul class=\\"schema-list\\"><li class=\\"schema-list-item\\"><code class=\\"schema-inline-code\\">mimo-v2.6-flash</code>, <code class=\\"schema-inline-code\\">mimo-v2.6-pro</code>, <code class=\\"schema-inline-code\\">mimo-v2.6-pro-ultraspeed</code>, <code class=\\"schema-inline-code\\">mimo-v2.5-pro</code>, <code class=\\"schema-inline-code\\">mimo-v2.5</code>: default <code class=\\"schema-inline-code\\">enabled</code></li></ul>Available options: <code class=\\"schema-inline-code\\">none</code>, <code class=\\"schema-inline-code\\">minimal</code>, <code class=\\"schema-inline-code\\">low</code>, <code class=\\"schema-inline-code\\">medium</code>, <code class=\\"schema-inline-code\\">high</code>, <code class=\\"schema-inline-code\\">xhigh</code>, <code class=\\"schema-inline-code\\">max</code>, <code class=\\"schema-inline-code\\">ultra</code>"
      }
    ]
  },
  {
    "name": "temperature",
    "type": "number",
    "isBold": true,
    "required": false,
    "description": "What sampling temperature to use, between 0 and 1.5. Higher values like 0.8 will make the output more random, while lower values like 0.2 will make it more focused and deterministic. We generally recommend altering this or <code class=\\"schema-inline-code\\">top_p</code> but not both.<br /><blockquote class=\\"schema-blockquote\\">In thinking mode, the <code class=\\"schema-inline-code\\">mimo-v2.6-flash</code>, <code class=\\"schema-inline-code\\">mimo-v2.6-pro</code>, <code class=\\"schema-inline-code\\">mimo-v2.6-pro-ultraspeed</code>, <code class=\\"schema-inline-code\\">mimo-v2.5-pro</code> and <code class=\\"schema-inline-code\\">mimo-v2.5</code> models do not support customizing the <code class=\\"schema-inline-code\\">temperature</code> parameter. Even if this parameter is passed in, it will be forcibly overridden and take effect with the model's recommended default value of <code class=\\"schema-inline-code\\">1.0</code>.</blockquote><ul class=\\"schema-list\\"><li class=\\"schema-list-item\\"><code class=\\"schema-inline-code\\">mimo-v2.6-flash</code>, <code class=\\"schema-inline-code\\">mimo-v2.6-pro</code>, <code class=\\"schema-inline-code\\">mimo-v2.6-pro-ultraspeed</code>, <code class=\\"schema-inline-code\\">mimo-v2.5-pro</code>, <code class=\\"schema-inline-code\\">mimo-v2.5</code>: default <code class=\\"schema-inline-code\\">1.0</code></li></ul>Required range: <code class=\\"schema-inline-code\\">[0, 1.5]</code>"
  },
  {
    "name": "text",
````

## https://mimo.mi.com/static/docs/tokenplan/Token%20Plan/quick-access.md (lines 20-45)

````text
</div>

- **API Key**: On the [Token Plan ](https://platform.xiaomimimo.com/#/console/plan-manage)page, obtain your exclusive API Key (in the format of `tp-xxxxx `or `ttp-xxxxx`).

- **Base URL**: You will need to configure one of the following Base URLs in your AI programming tool later (**The protocol varies by tool, and the Base URL shall be subject to what is displayed on the** [**Token Plan**](https://platform.xiaomimimo.com/#/console/plan-manage) **page**). For specific operations, please refer to the user guide document of the corresponding AI programming tool.

   - **OpenAI Compatible Protocol**

      - China Cluster: `https://token-plan-cn.xiaomimimo.com/v1`

      - Singapore Cluster: `https://token-plan-sgp.xiaomimimo.com/v1`

      - European Cluster: `https://token-plan-ams.xiaomimimo.com/v1`

   - **Anthropic Compatibility Protocol**

      - China Cluster: `https://token-plan-cn.xiaomimimo.com/anthropic`

      - Singapore Cluster: `https://token-plan-sgp.xiaomimimo.com/anthropic`

      - European Cluster: `https://token-plan-ams.xiaomimimo.com/anthropic`

<div className='mdx-highlight mdx-highlight-warning' data-highlight-icon='warning'>

The API Key for the Token Plan (`tp-xxxxx `or `ttp-xxxxx`) and the API Key for pay-as-you-go API calls (`sk-xxxxx`) are independent of each other and cannot be used interchangeably.

````

## https://mimo.mi.com/static/docs/tokenplan/Token%20Plan/quick-access.md (lines 155-165)

````text
{/* feishu-style:text-align:left */}
Answer: The API Key format for Token Plan is `tp-xxxxx `or `ttp-xxxxx`, which is only applicable to the Token Plan subscription service; the API Key format for pay-as-you-go API calls is `sk-xxxxx`, which is used for pay-as-you-go billing. The two are independent of each other and cannot be used interchangeably.

{/* feishu-style:text-align:left */}
**Question: What is the difference between the Base URL of Token Plan and the Base URL of pay-as-you-go API calls?** 

{/* feishu-style:text-align:left */}
Answer: The Base URL format of the Token Plan varies, subject to [Token Plan ](https://platform.xiaomimimo.com/#/console/plan-manage)page display.

{/* feishu-style:text-align:left */}
**Question: Can the API Key still be used after the subscription expires?** 
````

## https://mimo.mi.com/static/docs/quick-start/faq/token-plan/Plans&Pricing.md (lines 1-25)

````text
# Plans and Pricing

- **What packages are available for the Token Plan?** 

{/* feishu-style:text-align:left */}
We currently offer four subscription plans: Lite (available for individual users only), Standard, Pro, and Max. Both monthly and annual auto-renewal subscriptions are supported, while team plans additionally support one-time purchases for custom durations (1/3/6/12 months). Each plan comes with distinct usage quotas and benefits.

- **Which models does Token Plan support?** 

{/* feishu-style:text-align:left */}
Both the individual and team versions support a total of **6** **models, including the MiMo-V2.6 series**, which are accessible across all subscription tiers.

> mimo-v2.6-pro: Flagship Full-Modal Inference Model
>
> mimo-v2.6-flash: An Ultra-Full-Modal Inference Model with Extreme Speed
>
> mimo-v2.5-asr: Speech Recognition Model
>
> mimo-v2.5-tts/ v2.5-tts-voiceclone / v2.5-tts-voicedesign: Speech synthesis model (limited-time free)

<div className='mdx-highlight mdx-highlight-info' data-highlight-icon='info'>

**The Individual edition additionally supports mimo-v2.5-pro and mimo-v2.5.Both of these two models will be officially taken offline at 10:00 on October 21, 2026 Beijing Time, and it is recommended to switch to the new version of the models as soon as possible.**

</div>
````

## https://mimo.mi.com/static/docs/tokenplan/Token%20Plan/subscription.md (lines 1-18)

````text
# Individual Subscription

{/* feishu-style:text-align:left */}
Token Plan Individual Edition is an exclusive subscription solution launched for individual developers in AI programming scenarios, allowing you to use the cost-effective subscription resource package to call MiMo's flagship large language model in various mainstream AI development tools.

## Core Strengths

- **Covers flagship models** — all plans support mimo-v2.6-pro, mimo-v2.6-flash, mimo-v2.5-pro, mimo-v2.5, mimo-v2.5-asr, mimo-v2.5-tts-voiceclone, mimo-v2.5-tts-voicedesign, mimo-v2.5-tts, a total of 8 models. It adopts a token conversion mechanism with transparent and controllable quotas.

- **Flexible Subscription Plan** — Four-tier packages to meet various development needs of individual developers

- **Multi-ecosystem Out Of The Box** — Compatible with mainstream development toolchains including OpenCode, OpenClaw, Claude Code, etc.

<div className='mdx-highlight mdx-highlight-info' data-highlight-icon='info'>

**The two models mimo-v2.5-pro and mimo-v2.5 will be officially taken offline at 10:00 (Beijing time) on October 21, 2026. It is recommended to switch to the new version of the models as soon as possible.**

</div>
````

## https://mimo.mi.com/static/docs/api/guidance/error-codes.md (lines 1-60)

````text
# Error Codes

{/* feishu-style:text-align:left */}
When using API calls to the MiMo model, common error codes and solutions are as follows:

<table>
<colgroup>
<col style="width: 20.6%" />
<col style="width: 35.88%" />
<col style="width: 43.519999999999996%" />
</colgroup>
<thead>
<tr>
<th><span style="display: inline-block; width: 100%; text-align: left;">**Error Code**</span></th>
<th><span style="display: inline-block; width: 100%; text-align: left;">**Causes**</span></th>
<th><span style="display: inline-block; width: 100%; text-align: left;">**Solutions**</span></th>
</tr>
</thead>
<tbody>
<tr>
<td><span style="display: inline-block; width: 100%; text-align: left;">400 - Invalid Format</span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">Invalid request format</span></td>
<td><ul><li>Check if the JSON format is correct</li><li>Check if all required parameters are included</li><li>Check if parameter values are within the valid range</li><li>Check if the message format meets the interface requirements</li><li>Check if the model exists</li><li>Check if the fields are entered correctly</li><li>Check multimodal file input for compliance with format, size and other restrictions.</li><li>Check if multimodal file input is publicly accessible</li><li>In multi-turn conversations under thinking mode, the `reasoning_content` field must be fully passed back to the API.</li></ul></td>
</tr>
<tr>
<td><span style="display: inline-block; width: 100%; text-align: left;">401 - Authentication Fails</span></td>
<td><ul><li>Missing or invalid API Key, or incorrect Authorization request header format</li><li>API Key that mixes Token Plan and Pay-as-you-go API</li></ul></td>
<td><ul><li>Check if the API key and request header format are correct</li><li>Check if a dedicated Base URL and API Key are used when using the Token Plan</li></ul></td>
</tr>
<tr>
<td><span style="display: inline-block; width: 100%; text-align: left;">402 - Insufficient Balance</span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">Insufficient account balance</span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">Check your account balance and recharge in a timely manner</span></td>
</tr>
<tr>
<td><span style="display: inline-block; width: 100%; text-align: left;">403 - Forbidden Access </span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">The service is currently not available in the current region, or the API Key has been restricted by risk control </span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">Create a new API Key and pay attention to the security of input content</span></td>
</tr>
<tr>
<td><span style="display: inline-block; width: 100%; text-align: left;">404 - Not Found</span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">The requested endpoint or model does not support image input capability</span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">Verify that the model / endpoint being used supports image input capability</span></td>
</tr>
<tr>
<td><span style="display: inline-block; width: 100%; text-align: left;">421 - Content Filter</span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">Content moderation and blocking</span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">Avoid entering unsafe or sensitive content</span></td>
</tr>
<tr>
<td><span style="display: inline-block; width: 100%; text-align: left;">429 - Too Many Requests</span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">Requests are too frequent, or the quota of Token Plan has been exhausted</span></td>
<td><ul><li>Implement exponential backoff and retry logic, or reduce the request frequency</li><li>Upgrade the Token Plan package or switch to pay-as-you-go API</li></ul></td>
</tr>
<tr>
<td><span style="display: inline-block; width: 100%; text-align: left;">500 - Server Error</span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">Our server encounters an issue</span></td>
<td><span style="display: inline-block; width: 100%; text-align: left;">Please try again later, or contact us for resolution</span></td>
</tr>
<tr>
````
## Observed: Token Plan refusal of mimo-v2.6-pro-ultraspeed

Retained response body from campaign run `r2-t04-mimo` (2026-10-17, provider `mimotp`,
base URL `https://token-plan-sgp.xiaomimimo.com/v1`, HTTP 400), as stored in
`~/projects/blackbird_runs/2026-10-17-cpp26/r2-t04-mimo/session/diagnostics/`:

````text
{
    "error": {
        "code": "400",
        "message": "Not supported model mimo-v2.6-pro-ultraspeed"
    }
}
````

Live recheck 2026-10-18 through the same registered provider, before the settings
table refused the model at launch: the console printed
`[provider HTTP 400: Not supported model mimo-v2.6-pro-ultraspeed]`.
The Token Plan model list above does not include ultraspeed; the Responses reference
does.
