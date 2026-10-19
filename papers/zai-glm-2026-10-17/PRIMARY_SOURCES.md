# Z.ai / Zhipu GLM provider: primary sources

Research read 2026-10-17. Documentation and reference-code research only; no
live account probe, no network call to any Z.ai inference endpoint. Official
pages were fetched as raw Mintlify Markdown (`https://docs.z.ai/<path>.md`,
`https://docs.bigmodel.cn/<path>.md`); line references below for official docs
are to those Markdown files as served on the read date. Reference-code
citations are `path:line` under `/Users/patrickbeam/projects/blackbird/quarantine/`.

Labels used throughout:

- **[official]** stated in docs.z.ai / docs.bigmodel.cn.
- **[ref]** behaviour encoded in a reference implementation (may be tested
  against the live service, but not vendor-documented).
- **[reverse-engineered]** derived by a third party from a vendor client binary
  or traffic; not documented by the vendor.

## Summary of recommended integration facts

1. **Four regional origins x three protocols.** [official]
   | Purpose | International (z.ai) | China (bigmodel.cn) |
   | - | - | - |
   | Standard pay-as-you-go Chat Completions | `https://api.z.ai/api/paas/v4` | `https://open.bigmodel.cn/api/paas/v4` |
   | Coding Plan Chat Completions | `https://api.z.ai/api/coding/paas/v4` | `https://open.bigmodel.cn/api/coding/paas/v4` |
   | Anthropic Messages (general; plan quota for plan keys) | `https://api.z.ai/api/anthropic` (POST `/v1/messages`) | `https://open.bigmodel.cn/api/anthropic` |
   | OpenAI Responses (general; plan quota for plan keys) | `https://api.z.ai/api/v1` (POST `/responses`) | `https://open.bigmodel.cn/api/v1` |
2. **Auth is plain HTTP Bearer** with the `id.secret` key. JWT signing is an
   optional documented alternative, not required. The Anthropic route also
   accepts `x-api-key` (BigModel curl example). [official]
3. **The key type decides billing, not the URL**, for keys created on/after
   2026-10-16: Standard keys are pay-as-you-go everywhere; Individual/Team
   Coding Plan keys draw plan quota. Pre-2026-10-16 "Compatible" keys bill by
   endpoint (`/api/paas/v4` = balance; `/api/coding/paas/v4`, `/api/anthropic`,
   `/api/v1` = plan quota if the account ever had a plan). [official]
4. **Coding Plan terms restrict use to an enumerated list of supported tools.**
   Blackbird is not on that list (Pi, OpenCode, Cline, Roo, Kilo, Crush, Goose,
   Codex, Claude Code, Droid, Cursor, TRAE, Qoder, Eigent, ZCode, OpenClaw,
   Hermes Agent, AutoClaw, SillyTavern are). Terms forbid using plan quota for
   "general-purpose API access ... directly invoking model APIs from your own
   applications" absent a written agreement; detected unsupported use "may" restrict
   benefits; >3 violations may ban. A Blackbird Coding Plan route is therefore a
   terms risk the operator must accept explicitly; the Standard-key route is not.
5. **No official OAuth/device/browser login exists for third parties.** The only
   sign-in flow (oh-my-pi `zai-coding-plan`) reuses ZCode's OAuth client
   `client_P8X5CMWmlaRO9gyO-KSqtg` against `chat.z.ai`, then mints a durable
   `id.secret` API key through undocumented business endpoints. It is
   reverse-engineered, yields an API key (not a bearer session), and has no
   refresh. Recommendation for Blackbird: `login none`, `blackbird auth key`.
6. **Main coding loop is wire-feasible today via Responses** (`/api/v1/responses`;
   documented SSE event set, `reasoning.effort`, `function_call` /
   `function_call_output`, plaintext `reasoning` input/output items,
   `input_tokens_details.cached_tokens`). Documented divergences: default
   `store=false`, no `data: [DONE]`, no cancel, `tool_choice` only `none|auto`,
   string error codes. Responses output carries plaintext `reasoning_text`, not
   `encrypted_content`. Whether Blackbird's `responses-portable` adapter
   tolerates those divergences is unverified (see Unknowns).
7. **Current models (2026-10-17):** `glm-5.3` (text, 1M ctx, 128K out, thinking
   forced, `reasoning_effort` low|high|max, default max), `glm-5.3-flash` /
   `glm-5.3-flashx` (multimodal, 1M / 128K, forced thinking). Coding Plan serves
   only GLM-5.3 and GLM-5.3-Flash; GLM-5.2/5.1 requests are rerouted to GLM-5.3,
   GLM-4.7 to GLM-5.3-Flash. Chat API enum still lists glm-5.2, 5.1, 5, 4.7, 4.6.
8. **Chat Completions specifics:** `thinking: {type, clear_thinking}`;
   `reasoning_effort`; `tool_stream: true` needed for incremental tool-argument
   streaming; `tool_choice` only `auto`; `max_tokens` max 131072; stream deltas
   carry `reasoning_content`; usage `prompt_tokens_details.cached_tokens` in the
   final chunk; finish reasons include `sensitive`, `network_error`,
   `model_context_window_exceeded`; mid-stream failures surface only via
   `finish_reason`, not an error body.
9. **Errors:** HTTP status plus business `error.code` (string, e.g. `"1302"`).
   429 covers rate limit (1302), overload (1305), plan quota windows
   (1308/1310/1316-1321), plan expired (1309), model not in plan (1311),
   fair-use throttle (1313), and **1113 insufficient balance**. No Retry-After
   header is documented anywhere; reset times appear only in message text
   (`{next_flush_time}`), timezone-naive Beijing time per [ref].

## 1. API surfaces, Coding Plan scope, auth

### Base URLs [official]

- Standard API: "Z.ai Platform's general API endpoint is ... `https://api.z.ai/api/paas/v4`"
  (`docs.z.ai/api-reference/introduction.md` L15-18); BigModel
  `https://open.bigmodel.cn/api/paas/v4` (`docs.bigmodel.cn/cn/api/introduction.md` L18).
  OpenAPI `servers: https://api.z.ai/api`, path `/paas/v4/chat/completions`
  (`docs.z.ai/openapi.json`). The OpenAPI spec has no Responses or Anthropic paths.
- Coding Plan endpoint table, identical on Quick Start and Tool Integration
  (`docs.z.ai/devpack/quick-start.md` L65-71; `devpack/tool/others.md` L107-115):
  Anthropic Messages `https://api.z.ai/api/anthropic`; OpenAI Chat Completions
  `https://api.z.ai/api/coding/paas/v4`; OpenAI Responses `https://api.z.ai/api/v1`.
  China mirror table: `docs.bigmodel.cn/cn/coding-plan/tool/others.md` L125-127,
  `cn/coding-plan/quick-start.md` L84-86 (`open.bigmodel.cn/api/{anthropic,coding/paas/v4,v1}`).
- Codex: "Codex requires a dedicated OpenAI Responses protocol endpoint:
  `https://api.z.ai/api/v1`", config `wire_api = "responses"`,
  `experimental_bearer_token` (`devpack/tool/codex.md` L11-13, L140-151).
- GLM-5.3 model page lists the same three protocols for the "Model API"
  (`guides/llm/glm-5.3.md` L55-61) and adds: "If you have previously subscribed
  to a GLM Coding Plan, including an expired subscription, you can currently
  access the model API only through the OpenAI Chat Completion-compatible
  protocol." (L66). Meaning for Standard keys on plan-holding accounts is
  ambiguous; see Unknowns.
- BigModel Anthropic route: `base_url` `https://open.bigmodel.cn/api/anthropic`,
  curl `POST .../api/anthropic/v1/messages` with `x-api-key`
  (`docs.bigmodel.cn/cn/guide/develop/claude/introduction.md` L37, L82-84).
- BigModel Responses: "Response API 的基址是 `https://open.bigmodel.cn/api/v1`，不是对话补全使用的
  `https://open.bigmodel.cn/api/paas/v4`。请求超时为 7200 秒。"
  (`cn/guide/develop/responses/introduction.md` L87); curl `POST /api/v1/responses`
  with `Authorization: Bearer` (L147-148).
- Claude Code configuration: `ANTHROPIC_AUTH_TOKEN` + `ANTHROPIC_BASE_URL=https://api.z.ai/api/anthropic`,
  `API_TIMEOUT_MS=3000000`, model names `glm-5.3[1m]` / `glm-5.3-flash[1m]`
  (`devpack/tool/claude.md` L94-124). `[1m]` is a Claude Code selector suffix,
  "not an inference id" [ref: `oh-my-pi/packages/catalog/src/compat/rules/runtime/behavior.kdl:152-153`].

### Which endpoints the Coding Plan key uses [official]

- "API Keys for Individual Plans, Team Plans, and the standard model API are now
  managed separately ... not interchangeable" (`devpack/quick-start.md` L29-31).
- Key Management Guide, published 2026-10-16 (`devpack/notice/api-key-management.md`):
  "For Standard and Coding Plan keys created from October 16 onward, the key type
  determines how calls are billed ... Changing the endpoint does not change the
  key's billing type." (L21); "using a Standard API Key in a coding tool does not
  consume your Coding Plan quota" (L23). Compatible (pre-10-16) keys: `/api/paas/v4`
  = balance; `/api/coding/paas/v4`, `/api/anthropic`, `/api/v1` = plan quota if
  the account ever had a plan, "Calls stop when the plan expires" (L39-83).
  China equivalent: `cn/coding-plan/notice/api-key-management.md` L53-58.
- FAQ: "Users subscribed to the Coding Plan can only make calls via the plan's
  quota in supported tools. API calls outside the plan are not available." and
  for 1113 after purchase: wrong tool, wrong base URL (Claude Code/Goose
  `/api/anthropic`, others `/api/coding/paas/v4`), or model other than
  GLM-5.3/GLM-5.3-Flash (`devpack/faq.md` L56-61, L197-208).
- "Incorrect endpoint configuration will result in inability to use GLM Coding
  Plan subscription quota." (`devpack/tool/others.md` L123-125).

### Terms restricting Coding Plan use [official]

- `legal-agreement/subscription-terms.md` §4.2 (L36-40): quota "is only used
  within officially supported tools. If the system detects usage through
  unauthorized or unsupported tools (such as SDK-based access or other
  third-party integrations), some subscription benefits may be restricted";
  "You shall not use the GLM Coding Plan quota for general-purpose API access or
  any scenarios outside such tools, including but not limited to directly
  invoking model APIs from your own applications, bots, websites, SaaS products
  or other systems, unless you have entered into a separate written agreement";
  no resale/proxy. §4.3 (L42-46) personal use only; suspicion of "bulk or
  automated usage on behalf of others" permits quota reduction/termination.
- Usage Policy (`devpack/usage-policy.md` L23-27): "Use limited to supported
  tools ... Violations ... may trigger risk control measures, including rate
  limiting, account freezing ... Accounts with more than three violations may be
  banned." China: `cn/coding-plan/usage-notes.md` L19-31 (adds permanent-ban
  criteria: resale, relay/中转, arbitrage).
- Supported list (`devpack/tool/others.md` L13-105): coding agents ZCode, Claude
  Code, Claude for IDE, Codex, OpenCode, Pi, Cursor, Cline, TRAE, Qoder, Droid,
  Kilo Code, Roo Code, Crush, Goose, Eigent; general-purpose (best-effort,
  "may face temporary rate limits") AutoClaw, OpenClaw, Hermes Agent, SillyTavern.
  China list adds CodeBuddy, Lingma, MonkeyCode, WorkBuddy, Cherry Studio
  (`cn/coding-plan/tool/others.md`). Blackbird is on neither list.

### Auth header [official]

- "The Z.AI API uses the standard HTTP Bearer ... `Authorization: Bearer ZAI_API_KEY`"
  (`api-reference/introduction.md` L25-34); OpenAPI `securitySchemes.bearerAuth`
  `scheme: bearer`. BigModel identical (`cn/api/introduction.md` L27-37).
- JWT is optional: "JWT Token Authentication ... suitable for scenarios requiring
  higher security" (`guides/develop/http/introduction.md` L77-110;
  `cn/guide/develop/http/introduction.md` L83-115). Construction: split key on
  `.` into `id`, `secret`; payload `{"api_key": id, "exp": now_ms + ttl_ms,
  "timestamp": now_ms}` (milliseconds); HS256 with `secret`; JOSE header
  `{"alg":"HS256","sign_type":"SIGN"}`. Error 1003 "Authentication Token
  expired" (401) applies to this mode (`api-reference/api-code.md` L14).
- Key format: `<id>.<secret>`, no `sk-` prefix [ref: `oh-my-pi/docs/providers.md:396`;
  `oh-my-pi/packages/catalog/src/compat/rules/auth/zhipu-coding-plan.kdl:6`].
  Note oh-my-pi's z.ai paste prompt uses placeholder `sk-...`
  (`rules/auth/zai.kdl:6`), contradicting its own minted 49-char `id.secret` key
  (`packages/ai/src/registry/oauth/zai.ts:141-142,198`).
- Quota endpoint (undocumented): `GET https://api.z.ai/api/monitor/usage/quota/limit`
  with the raw key as `Authorization` (no `Bearer`), plus `/api/monitor/usage/model-usage`
  [ref: `oh-my-pi/packages/ai/src/usage/zai.ts:15-17,208-221`].

## 2. OAuth / device / browser login

### Official status

No official third-party OAuth, device flow, or browser login is documented on
docs.z.ai or docs.bigmodel.cn (full indexes `docs.z.ai/llms.txt`,
`docs.bigmodel.cn/llms.txt` read 2026-10-17). The only official sign-in mention
is ZCode's own UI: "click **Get API Key** or **Use Subscription**, to be
redirected to the login page, after which the key will be automatically
populated" (`devpack/tool/zcode.md` L21). That confirms the ZCode flow ends in
an API key; it documents no endpoints. BigModel's Managed Agents Vault OAuth
(`docs.bigmodel.cn/llms.txt` "创建 Credential") is for agent-held MCP
credentials, not client login.

### oh-my-pi `zai-coding-plan` flow [ref, reverse-engineered from ZCode]

- Rule: `oh-my-pi/packages/catalog/src/compat/rules/auth/zai-coding-plan.kdl:1-33`.
  Implementation: `packages/ai/src/registry/oauth/zai.ts:1-205`, wired as hook
  `zai-mint-key` (`packages/ai/src/registry/hooks/oauth-code.ts:11`). Tests:
  `packages/ai/test/zai-oauth.test.ts:10-16,133-164,401-438`.
- Authorize: `GET https://chat.z.ai/api/oauth/authorize?client_id=client_P8X5CMWmlaRO9gyO-KSqtg&response_type=code&redirect_uri=zcode://zai-auth/callback&state=<random>`;
  **no PKCE, no scope** (kdl:4-6,12; test L153-159). Loopback redirects are
  rejected by Z.ai's allowlist for this client (#10745); only ZCode's
  `zcode://` scheme validates. oh-my-pi temporarily registers the `zcode` URL
  scheme handler or accepts a pasted `code#state` / redirect URL (kdl:7-13;
  test L133-191). Its `docs/provider-quirks.md:2032` still states the stale
  `http://127.0.0.1:9999/callback`.
- Token: `POST https://zcode.z.ai/api/v1/oauth/token`, JSON body
  `{"provider":"zai","code","redirect_uri","state"}` (non-RFC). Response envelope
  `{code:0,msg,data:{token:<zcode jwt>, zai:{access_token}, user:{email,id}}}`
  (kdl:14-27; test L77-85, L423-428).
- Business login: `POST https://api.z.ai/api/auth/z/login` `{"token":<zai access_token>}`
  -> `{code:200,success:true,data:{access_token}}` (zai.ts:21-23,113-126; test L192).
- Key mint (all `Authorization: Bearer <biz token>`): `GET /api/biz/customer/getCustomerInfo`
  (default org/project) -> `GET|POST /api/biz/v1/organization/{org}/projects/{proj}/api_keys`
  (find/create key named `oh-my-pi`) -> `GET .../api_keys/copy/{apiKey}` for the
  unmasked `secretKey`; credential = `${apiKey}.${secretKey}` (zai.ts:144-199;
  test L414-437).
- Lifetime/refresh: `expires "never"`, `refresh "none"`; stored as the `zai`
  API-key credential (kdl:23-32; test L408-410 asserts `refresh: ""`,
  `expires: 8.64e15`). The result is a durable API key, not a bearer session.
- Changelog: "Fixed `/login zai` ... updated authentication flow, including
  local desktop sign-in, remote paste-code completion, and the configurable
  `ZAI_OAUTH_REDIRECT_URI`" (`packages/catalog/CHANGELOG.md:365`); Windows/Linux
  `zcode://` handler fixes (`packages/ai/CHANGELOG.md:338,387`).
- `zhipu-coding-plan-login.test.ts` is **not** OAuth: it is an API-key paste
  validated by `POST https://open.bigmodel.cn/api/coding/paas/v4/chat/completions`
  with `Authorization: Bearer`, body `{model:"glm-5.1", messages:[{role:"user",content:"ping"}], max_tokens:1, temperature:0}`
  (`packages/ai/test/zhipu-coding-plan-login.test.ts:8-38`;
  rule `rules/auth/zhipu-coding-plan.kdl:1-9`). z.ai paste login validates against
  `https://api.z.ai/api/coding/paas/v4` with `glm-5.2` (`rules/auth/zai.kdl:3-8`).

### Third-party ZCode protocol write-up [reverse-engineered]

`https://cdn.jsdelivr.net/npm/pi-zcode-provider@0.1.0/PROTOCOL.md` (from
ZCode-3.10.1 AppImage) corroborates the same chain (L41-54) with a different
redirect `https://zcode.z.ai/app/oauth/login?redirect=zcode://oauth/callback`,
says the business token is "HS512 JWT, long-lived, no `exp`" (L48-49), and
describes a polling flow: `POST https://zcode.z.ai/api/v1/oauth/cli/init`
`{"provider":"zai"}` with a client-chosen random Bearer poll token ->
`{flow_id, authorize_url, expires_at, poll_interval_sec}`; poll
`GET .../oauth/cli/poll/{flow_id}` until `status:"ready"`; flow expires after
300 s (L60-80). It also notes server-toggleable request signing
(`x-client-sig`/`x-client-pow`) currently disabled (L95-98). Unverified by
this research; not used by any quarantined reference.

Assessment: using ZCode's client id from Blackbird is impersonation of a vendor
client, and the minted key then draws plan quota from an unlisted tool. Both
compound the §1 terms risk.

## 3. Models, limits, thinking, tools, caching

### Model IDs and limits [official]

- Release notes (`release-notes/new-released.md` L11-57): GLM-5.3-Flash
  2026-08-26; GLM-5.3 2026-08-18; GLM-5.2 2026-06-16 ("1M lossless context");
  GLM-5.1 2026-04-07; GLM-5 2026-02-12; GLM-4.7 2025-12-22; GLM-4.6 2025-09-30 (200K).
- Chat Completions `model` enum: `glm-5.3` (default), `glm-5.2`, `glm-5.1`,
  `glm-5`, `glm-4.7`, `glm-4.6`; vision schema `glm-5.3-flashx` (default),
  `glm-5.3-flash` (`api-reference/llm/chat-completion.md` L201-215, L489-499).
- GLM-5.3: "text-only inputs, with a 1M-token context window and a maximum
  output length of 128K tokens" (`guides/llm/glm-5.3.md` L27). GLM-5.3-Flash/FlashX:
  Video/Image/Text/File in, 1M ctx, 128K out; FlashX "not yet available on the plan"
  (`guides/vlm/glm-5.3-flash.md` L28-61). GLM-5.2: Text, 1M, 128K
  (`guides/llm/glm-5.2.md` L14-30). `max_tokens` maximum 131072 for 5.3/5.2/5.1/5/4.7/4.6
  (`chat-completion.md` L401-409).
- Coding Plan models: "All plans support GLM-5.3, GLM-5.3-Flash. Requests for
  GLM-5.2/GLM-5.1 will be automatically routed to GLM-5.3, requests for GLM-4.7
  will automatically be routed to GLM-5.3-Flash." (`devpack/overview.md` L66-69).
  Cline example context: "`glm-5.2` is `1000000`; other models `200000`"
  (`devpack/tool/others.md` L147, older text); latest-model guide says 1000000
  for 5.3/5.3-flash (`devpack/latest-model.md` L133). Codex catalog example uses
  `context_window: 1048576` for glm-5.3 (`devpack/tool/codex.md` L118-119).
- Plan quota: credit-based, 5-hour and weekly windows (Lite 2,000/10,000;
  Pro 12,000/60,000; Max 28,000/140,000); credits = (input x mult + cached x mult +
  output x mult)/10,000; GLM-5.3 multipliers 6.9 / 1.7 / 24; GLM-5.3-Flash
  2.3 / 0.56 / 8; off-peak 50% (`devpack/overview.md` L77-157).
- Sampling: `temperature` [0.0, 1.0], default 1.0; `top_p` [0.01, 1.0], default 0.95;
  `do_sample` default true; `stop` "only one stop word is supported"
  (`chat-completion.md` L337-344, L378-399, L442-449).

### Thinking controls [official]

- `thinking.type`: `enabled|disabled`, default `enabled`; "`GLM-5.3`
  `GLM-5.3-FLASH` series can only be enabled ... GLM-5.2 GLM-5.1 GLM-5 GLM-4.6
  ... automatically determine whether to think, while GLM-4.7 will think
  compulsorily" (`chat-completion.md` L747-765). Sending `disabled` to GLM-5.3
  fails: "Otherwise, the request will fail." (`guides/llm/glm-5.3.md` L36);
  "an error will occur" (`guides/capabilities/thinking.md` L21).
- `thinking.clear_thinking` (bool, default true): true drops prior-turn
  `reasoning_content`; false retains it and requires forwarding "the full,
  unmodified, and correctly ordered historical `reasoning_content`"
  (`chat-completion.md` L766-775). Preserved Thinking "is enabled by default on
  the Coding Plan endpoint and disabled by default on the standard API endpoint"
  (`guides/capabilities/thinking-mode.md` L37-41).
- Interleaved thinking with tools: "thinking blocks should be explicitly
  preserved and returned together with the tool results"; example replays
  assistant `{"content", "reasoning_content", "tool_calls"}`
  (`thinking-mode.md` L21-27, L105-107).
- `reasoning_effort`: enum `max|xhigh|high|medium|low|minimal|none`, default
  `max`, "takes effect when `thinking` is enabled", GLM-5.2+. GLM-5.3 and
  GLM-5.3-Flash accept only `low|high|max`. GLM-5.2: `none|minimal` skip
  thinking; `low|medium` -> `high`; `xhigh` -> `max` (`chat-completion.md` L358-377;
  `guides/llm/glm-5.3.md` L28-33).
- Anthropic/Codex effort normalisation on the Coding Plan (`devpack/latest-model.md`
  L87-104): thinking unset/`enabled`/`adaptive` -> max; `disabled`/`none`/`off`
  -> low ("still performs lightweight thinking"); effort `minimal|light|low`
  -> low; `medium|high` -> high; `xhigh|max|ultra` -> max; unknown -> max.
  "Claude Code uses `thinking.type` and `output_config.effort`; Codex uses
  `reasoning.effort`." Priority: explicit effort > thinking toggle > default.
  This table is the GLM-5.3 Coding Plan behaviour and differs from the Chat
  Completions statement that `disabled` errors on GLM-5.3.

### Streaming, tools, caching [official]

- SSE: `choices[0].delta.content`, `choices[0].delta.reasoning_content`;
  `finish_reason` and `usage` "only appears in the last chunk"; terminator
  `data: [DONE]`. Example final chunk carries `"delta":{"role":"assistant","content":""}`
  and `usage.prompt_tokens_details.cached_tokens` (`guides/capabilities/streaming.md` L20-25, L112-122).
- `tool_stream` (bool, default false), "streaming response for Function Calls",
  GLM-5.3/5.2/5.1/5/4.7/4.6 (`chat-completion.md` L410-418); migration guide:
  enable `stream=true` and `tool_stream=true` and concatenate
  `delta.tool_calls[*].function.arguments` per `index` (`guides/overview/migrate-to-glm-new.md`
  L23-24, L87-163). The tool-stream guide says "limited to `glm-5`"
  (`guides/capabilities/stream-tool.md` L17), an internal docs inconsistency.
- `tools`: max 128 functions; types `function`, `retrieval`, `web_search`; function
  `name` `^[a-zA-Z0-9_-]+$` max 64; `name`, `description`, `parameters` all
  required (`chat-completion.md` L419-430, L977-998). `tool_choice`: "only auto
  is supported" (L431-440; `guides/capabilities/function-calling.md` L16). No
  `parallel_tool_calls`, `strict`, or `stream_options` field is documented.
- `response_format` `text|json_object` (L450-466). `request_id` 6-64 chars,
  `user_id` 6-128 chars (L467-480).
- Finish reasons: `stop`, `tool_calls`, `length`, `sensitive`,
  `model_context_window_exceeded`, `network_error` (L703-708). "When using
  streaming (SSE) calls, if the API terminates abnormally during inference, the
  above error codes will not be returned. Instead, the reason ... will be
  provided in the `finish_reason`" (`api-reference/api-code.md` L79).
- Caching: implicit/automatic, no request field; "cached token counts in
  response field `usage.prompt_tokens_details.cached_tokens`"
  (`guides/capabilities/cache.md` L7-17). Responses API adds `prompt_cache_key`
  "用于集群路由，以提高缓存命中率" and `usage.input_tokens_details.cached_tokens`,
  `output_tokens_details.reasoning_tokens` (BigModel `api-reference/response/创建-response.md`
  L180-182, L421-443).
- Response message schema (`chat-completion.md` L934-948) lists only `role`,
  `content`, `tool_calls`; `reasoning_content` is documented only in guides.

### Responses API (BigModel reference; z.ai documents the URL only) [official]

`docs.bigmodel.cn/api-reference/response/创建-response.md`:
`POST /v1/responses` on `https://open.bigmodel.cn/api` (L25-35). Request:
`model`, `input` (string or items `message|function_call|function_call_output|reasoning`),
`instructions`, `stream`, `temperature`, `top_p`, `max_output_tokens` (max 131072,
default 65536, "含回答与思维链"), `stop`, `tools` (`function|namespace|custom|web_search`),
`tool_choice` `none|auto`, `reasoning.effort` (same 7-value mapping as above,
default max), `text.format` `text|json_object`, `prompt_cache_key`,
`previous_response_id` (requires `store=true`, 7-day validity), `store` default
false (L116-190, L364-394). SSE events: `response.created|in_progress|completed|failed|incomplete`,
`response.output_item.added|done`, `response.content_part.added|done`,
`response.output_text.delta|done`, `response.function_call_arguments.delta|done`,
`response.reasoning_text.delta|done`, `response.web_search_call.*` (L258-295).
`InputReasoning`/`OutputReasoning` carry plaintext `ReasoningTextPart`
content, no encrypted field (L911-924, L1036-1052). Errors use string codes
(`invalid_request`, `context_length_exceeded`, `rate_limit_exceeded`,
`insufficient_quota`/`quota_exceeded`/`usage_limit_reached`/`usage_not_included`,
`server_is_overloaded`/`overloaded`/`slow_down`, `content_filter`/`cyber_policy`, ...)
"与对话补全的数字业务码不是同一套" (L296-345). Guide warnings: default `store=false`,
stream ends without `data: [DONE]`, no cancel (`cn/guide/develop/responses/introduction.md` L14-16, L202, L221).
No `parallel_tool_calls`, `include`, `encrypted_content`, or `truncation` field is documented.

## 4. Anthropic-compatible endpoint

[official] coverage is thin: BigModel says "某些场景下智谱与 Claude 接口仍存在差异，
但不影响整体兼容性" without enumerating differences
(`cn/guide/develop/claude/introduction.md` L29-31); examples show only
`model`, `max_tokens`, `stream`, `messages` (L82-93). z.ai has no Anthropic
compatibility reference page in `docs.z.ai/llms.txt`. Effort mapping via
`thinking.type` and `output_config.effort` is documented in
`devpack/latest-model.md` L87-104 (see §3). No official statement exists on
thinking-block `signature`, `redacted_thinking`, `cache_control`, `tool_choice`
variants, beta headers, or SSE event names for this route.

[ref] behaviour:

- oh-my-pi routes `zai` to Anthropic Messages by default with
  `base-url https://api.z.ai/api/anthropic`; `glm-5.3-flash` is routed to
  Chat Completions because it is "available on Z.AI's native chat-completions
  API but not its Anthropic-compatible coding endpoint (#10539)"
  (`packages/catalog/src/compat/rules/runtime/behavior.kdl:246-250`;
  `rules/providers/zai.kdl:16-33`). This conflicts with official Claude Code
  config using `glm-5.3-flash[1m]` on `/api/anthropic` (`devpack/latest-model.md`
  L70-77); possibly fixed server-side since #10539.
- `requires-tool-result-id #true` for Anthropic tool results (`zai.kdl:36-37`).
- GLM >= 5.2 on `zai` uses thinking mode `anthropic-budget-effort` (budget plus
  effort) (`packages/catalog/src/compat/resolve.ts:1012-1014`; `zai.kdl:54-67`).
- Consecutive tool results must be grouped into one user message "needed for
  z.ai Anthropic endpoint" [ref: `pi/packages/ai/src/api/anthropic-messages.ts:1470`].
- Z.ai accepts `thinking.clear_thinking=false` on Anthropic Messages ("Z.AI
  requires this to preserve reasoning; Anthropic rejects it")
  [ref: `goose/crates/goose-provider-types/src/formats/anthropic.rs:866-871`;
  `goose/crates/goose-providers/src/declarative/definitions/zai.json:19-20`].
- goose sends `x-api-key` on this route (`goose/crates/goose-providers/src/anthropic.rs:512-514`);
  the ZCode write-up says both Bearer and `x-api-key` work [reverse-engineered,
  `pi-zcode-provider PROTOCOL.md` L27-29].
- Cross-protocol replay: oh-my-pi re-serialises preserved thinking blocks into
  `reasoning_content` when a conversation moves from Z.ai Anthropic to Z.ai
  Chat Completions, signatures being "irrelevant on this path"
  (`packages/ai/src/providers/openai-completions.ts:2504-2515`).

## 5. Errors and rate limits [official unless marked]

- Error shape: `{"error":{"code":"1214","message":"..."}}`; code is a JSON
  string in examples (`api-reference/api-code.md` L47-58, L76) although the
  OpenAPI `Error.code` schema says integer (`chat-completion.md` "Error").
- Table (`api-reference/api-code.md` L9-45; BigModel `cn/api/api-code.md` L12-45, same codes):
  500 -; 1000/1001/1003/1005 -> 401; **1113 -> 429** "Insufficient balance or no
  resource package"; 1200 -> 500; 1210-1215 -> 400 (invalid param, unknown model
  1211, unsupported call method 1212, missing 1213, invalid 1214, mutually
  exclusive 1215); 1220 -> 403; 1221/1222 -> 400; 1230/1234 -> 500 (1234 network
  error); **1261 -> 400 "Prompt too long"**; 1301 -> 400 unsafe content;
  **1302 -> 429 rate limit**; **1305 -> 429 overloaded**; 1308 -> 429 "Usage limit
  reached for `{number}` `{unit}`. Your limit will reset at `{next_flush_time}`";
  1309 plan expired; 1310 weekly/monthly limit; 1311 model not in plan; 1313
  fair-use throttle; 1314 enterprise expired; 1315 key limited to enterprise
  coding package; 1316-1321 5h/7d limits with extra-usage balance/spend caps.
  All 13xx plan codes are HTTP 429.
- Rate limits are concurrency limits per account and model; Coding Plan
  concurrency is tier-based (Max > Pro > Lite), dynamic, higher off-peak, not
  adjustable on request (`devpack/usage-policy.md` L9-21;
  `cn/api/rate-limit.md` L9-24, L58-68, L153-155). 1302 advice: reduce
  concurrency, queue; 1305: retry later with longer intervals
  (`cn/api/rate-limit.md` L95-134). `docs.z.ai/api-reference/rate-limit`
  redirects (307) to the console `https://z.ai/manage-apikey/rate-limits`.
- **Retry-After:** not mentioned in any downloaded z.ai or BigModel page
  (grep for `retry-after` returned no file).
- [ref] Reset stamps are timezone-naive Beijing time; oh-my-pi appends `+08:00`
  (`oh-my-pi/packages/catalog/src/compat/rules/runtime/behavior.kdl:117-120`;
  `packages/ai/src/oneshot-retry.ts:75`). Zhipu 429 quota text
  "429 已达到 5 小时的使用上限。您的限额将在 2026-08-06 20:06:00 重置。" (type=1308) is
  classified as quota exhaustion (rotate credential, not backoff)
  (`packages/ai/src/error/rate-limit.ts:73-80,217-221`). "1302" in stream errors
  is treated as transient (`packages/ai/src/error/retryable.ts:24-28`).
- [ref] Context overflow: pi matches `{"code":"1261","message":"Prompt too long"}`
  and CN `"Prompt exceeds max length"`, plus `model_context_window_exceeded`, and
  detects silent overflow when `input+cacheRead > contextWindow` on a `stop`
  (`pi/packages/ai/src/utils/overflow.ts:30-57,114-115,152-157`). pi maps
  `finish_reason: network_error` to a retryable error
  (`pi/packages/ai/src/api/openai-completions.ts:1570-1571`).
- [ref] GLM Coding Plan streams can idle long: oh-my-pi sets a 600 s stream-idle
  timeout for GLM >= 5 on z.ai/bigmodel hosts (`packages/catalog/src/compat/resolve.ts:246,460-466`);
  Z.ai's own Claude Code config uses `API_TIMEOUT_MS=3000000` and the Responses
  guide a 7200 s client timeout (official, §1).

## 6. Reference implementations

### oh-my-pi (`oh-my-pi/`)

- Providers: `zai` (Anthropic Messages default, Chat Completions for
  `glm-5.3-flash`; env `ZAI_API_KEY`; default `glm-5.3`; seeds glm-5.3 and
  glm-5.3-flash with context 1000000 / max 131072)
  (`packages/catalog/src/compat/rules/providers/zai.kdl:3-33`); `zhipu-coding-plan`
  (Chat Completions `https://open.bigmodel.cn/api/coding/paas/v4`; env `ZHIPU_API_KEY`;
  default `glm-5.1`; `thinking-format "zai"`, `reasoning-content-field "reasoning_content"`,
  `supports-developer-role #false`) (`rules/providers/zhipu-coding-plan.kdl:3-53`;
  `docs/provider-quirks.md:2088-2100`). General BigModel pay-as-you-go is a
  user-defined custom provider only (`docs/providers.md:379-396`).
- Auth: §2. Login order lists `zai`, `zai-coding-plan`, `zhipu-coding-plan`
  (`rules/auth/_order.kdl:4-6`).
- GLM-5.2 dialect on z.ai/zhipu: `zai-reasoning-effort-dialect`,
  `clamp-output-to-model-max`, efforts `high|max`; GLM-5.3 family efforts
  `low|high|max`, default `max`, `thinking-requires-effort` (cannot disable)
  (`rules/classes/glm.kdl:16-29,102-114`); 1M/131072 pinned for glm-5.2,
  glm-5.3, glm-5.3-flash on zai/zhipu (`glm.kdl:65-74`).
- Wire encoding: disabled -> `thinking:{type:"disabled"}` and delete
  `reasoning_effort` (`packages/ai/src/providers/openai-shared.ts:1053-1063`);
  enabled -> `thinking:{type:"enabled"}` plus `reasoning_effort` when supported
  (`openai-shared.ts:1136-1150`). `tool_stream:true` only for the GLM-5.2
  reasoning-effort dialect with tools present (`openai-shared.ts:1255-1263,1306-1321`;
  `docs/provider-endpoint-constraints.md:281-287`). Uses `max_tokens`, not
  `max_completion_tokens`, on z.ai/zhipu (`resolve.ts:427-433`); z.ai/zhipu
  flagged non-standard OpenAI (`resolve.ts:410-425`).
- Quirks: GLM-5.2 with thinking on long turns emitted tool calls with empty
  `name`, poisoning replay; sanitised before every request
  (`packages/ai/src/providers/transform-messages.ts:250-268`). Keepalive-only
  `{choices:[]}` chunks observed "against z.ai/GLM via OpenRouter" must not
  reset idle watchdogs (`openai-completions.ts:582-594`).
- Usage: §1 quota endpoint; credit-based windows (`docs/provider-quirks.md:2034-2035`).

### pi (`pi/packages/ai/`)

- `zai` = `https://api.z.ai/api/coding/paas/v4`, env `ZAI_API_KEY`;
  `zai-coding-cn` = `https://open.bigmodel.cn/api/coding/paas/v4`, env
  `ZAI_CODING_CN_API_KEY`; Chat Completions, Bearer (`src/providers/zai.ts:8-13`;
  `src/providers/zai-coding-cn.ts:8-13`). Moved off Anthropic to Chat Completions
  (`CHANGELOG.md:2040`). Default `glm-5.3` (`packages/coding-agent/src/core/model-resolver.ts:38-39`).
- Thinking: `{type:"enabled", clear_thinking:false}` or `{type:"disabled"}`
  (`src/api/openai-completions.ts:875-887`); `clear_thinking:false` added so
  replayed `reasoning_content` participates in caching (`CHANGELOG.md:619`).
  Effort maps GLM-5.2 off->`none`, high, max; GLM-5.3 low/high/max
  (`test/openai-completions-tool-choice.test.ts:398-462`).
- `tool_stream:true` with tools unless model in glm-4.5 family
  (`openai-completions.ts:851-854`; `scripts/generate-models.ts:262`).
- Compat: `supportsStore:false`, no developer role, `max_tokens`, no strict mode
  (`openai-completions.ts:1589-1663`). Reasoning read from `reasoning_content`
  / `reasoning` / `reasoning_text`, replayed under the same field
  (`openai-completions.ts:602-630,1333-1339`). Cached tokens from
  `prompt_tokens_details.cached_tokens` (`:1515-1525`). Overflow/finish handling §5.

### goose (`goose/crates/`)

- `zai`: Anthropic engine, `https://api.z.ai/api/anthropic` (overridable
  `ZAI_BASE_URL`), env `ZHIPU_API_KEY`, `preserves_thinking`, `emit_clear_thinking`
  (`goose-providers/src/declarative/definitions/zai.json:3-20`); models with
  context (glm-5.3 1000000, glm-5.1 200000, glm-4.6 204800 ...) (`zai.json:22-34`).
- `zai_coding_plan`: OpenAI engine, `https://api.z.ai/api/coding/paas/v4`, env
  `ZAI_CODING_PLAN_API_KEY`, dynamic models via `GET /models`
  (`zai_coding_plan.json:3-22`; `tests/zai_coding_plan.rs:71-83`); adds
  `tool_stream:true` when streaming (`goose-providers/src/openai.rs:420-463`);
  test requires the next turn to carry assistant `reasoning_content`
  (`tests/zai_coding_plan.rs:88-170`).
- `zhipu`: OpenAI engine, `https://open.bigmodel.cn/api/paas/v4` (`zhipu.json:3-28`).

### cline (`cline/sdk/packages/llms/`)

- `zai` (`https://api.z.ai/api/paas/v4`, China `https://open.bigmodel.cn/api/paas/v4`)
  and `zai-coding-plan` (`/api/coding/paas/v4` pair), env `ZHIPU_API_KEY`
  (`src/providers/builtins.ts:1005-1034`).
- "Native Z.AI GLM endpoints expect `thinking.type`; they do not accept the
  routed `reasoning.enabled` / `reasoning.exclude` shape" (`src/providers/routing/glm-thinking.ts:31-65`).
  No `clear_thinking`, `reasoning_effort`, or `tool_stream`.

### roo-code (`roo-code/`)

- Four API lines (international/china x coding/api) (`packages/types/src/providers/zai.ts:375-399`),
  default `international_coding` (`src/api/providers/zai.ts:27,34`).
- Sends `thinking:{type:"disabled"}` explicitly because GLM-4.7 thinks by
  default (`src/api/providers/zai.ts:43-64`); body includes
  `stream_options:{include_usage:true}`, `parallel_tool_calls` default true,
  temperature default 0.6 (`zai.ts:95-107`; `packages/types/src/providers/zai.ts:373`).
- Replay quirk: "Z.ai drops all reasoning_content when it sees a user message";
  text following tool results is merged into the last `tool` message
  (`src/api/transform/zai-format.ts:99-112,189-197`).

### continue (`continue/`)

- Thin OpenAI subclass, `https://api.z.ai/api/paas/v4/`, no thinking/tool_stream
  handling (`core/llm/llms/zAI.ts:5-10`; `packages/llm-info/src/providers/zai.ts:4-32`).

### pi-agent-rust (`pi-agent-rust/`)

- Four presets (`zai`, `zai-coding-plan`, `zhipuai`, `zhipuai-coding-plan`),
  Chat Completions, `ZHIPU_API_KEY`, no thinking controls emitted
  (`src/provider_metadata.rs:1278-1345`; `src/providers/openai.rs:236-240`).
  Its VCR cassettes (`tests/fixtures/vcr/verify_zai*_*.json`) are synthetic
  ("Simulated error 401", `chatcmpl-verify-00N`) and are not wire evidence.

### opencode / mimo-code (`opencode/packages/opencode/`, `mimo-code/packages/cli/`)

- Providers from models.dev: `zai` `https://api.z.ai/api/paas/v4`, `zai-coding-plan`
  `https://api.z.ai/api/coding/paas/v4`, `zhipuai-coding-plan`
  `https://open.bigmodel.cn/api/coding/paas/v4`, `zhipuai` `https://open.bigmodel.cn/api/paas/v4`;
  all `@ai-sdk/openai-compatible`, env `ZHIPU_API_KEY`
  (`opencode/packages/opencode/test/tool/fixtures/models-api.json:57444-57447,66953-66956,70496-70499,116900-116903`).
- Always sends `thinking:{type:"enabled", clear_thinking:false}` for zai/zhipuai
  ids (`opencode/packages/opencode/src/provider/transform.ts:1054-1062`; mimo
  `packages/cli/src/provider/transform.ts:1695-1703`); no effort variants for
  `glm` (`transform.ts:652`); reasoning replayed via `reasoning_content`
  (`transform.ts:286-316`); glm-4.6/4.7 temperature forced 1.0 (`:464-465`).
- "z.ai: can accept overflow silently" (`opencode/packages/opencode/src/provider/error.ts:30-31`);
  mimo adds `/model_context_window_exceeded/i` "z.ai non-standard finish_reason"
  (`mimo-code/packages/cli/src/provider/error.ts:196`).

### qwen-code (`qwen-code/packages/core/src/`)

- Preset env `ZAI_API_KEY`, `https://api.z.ai/api/paas/v4` and `/api/coding/paas/v4`
  (`providers/presets/zai.ts:10-39`). Its thinking toggle emits
  `extra_body.enable_thinking` (DashScope flag, not Z.ai `thinking`)
  (`providers/provider-config.ts:81-88`) — a probable bug, not a Z.ai fact.
- Flattens `reasoning:{effort}` to top-level `reasoning_effort` on z.ai/bigmodel
  hosts for GLM >= 5.2 (`core/openaiContentGenerator/provider/zai.ts:25-178`).
- Retryable codes `429, 503, 1302, 1305` (`utils/rateLimit.ts:14-16`).

### vtcode (`vtcode/crates/codegen/`)

- Default `https://api.z.ai/api/paas/v4` (`vtcode-config/src/constants/urls.rs:15-16`),
  env `ZAI_API_KEY`, overrides `ZAI_BASE_URL` (`vtcode-llm/src/providers/zai.rs:10-38`).
  Bearer plus `Accept-Language: en-US,en` (`zai.rs:146-148`; matches official curl).
- Coerces any `tool_choice` other than auto to `"auto"`: "Z.AI only supports
  \"auto\"" (`zai.rs:53-64`).
- Effort none -> `thinking:{type:"disabled"}`; else `thinking:{type:"enabled"}`
  + `reasoning_effort` low|high|max, rejects `xhigh` (`zai.rs:66-124`); sets
  `thinking.clear_thinking=false` when history has reasoning (`zai.rs:109-121`);
  `tool_stream:true` when streaming with tools (`zai.rs:135-137`).

### forgecode (`forgecode/crates/`)

- `zai` `https://api.z.ai/api/paas/v4/chat/completions`, env `ZAI_API_KEY`;
  `zai_coding` `https://api.z.ai/api/coding/paas/v4/chat/completions`, env
  `ZAI_CODING_API_KEY` (`forge_repo/src/provider/provider.json:1165-1169,1315-1319`);
  adds `Session-Id` header (`forge_repo/src/provider/openai.rs:130-145`).
- Maps `reasoning.enabled` to `thinking:{type}`; ignores effort
  (`forge_app/src/dto/openai/transformers/zai_reasoning.rs:14-44`).
- GLM stream quirk: "GLM sends malformed tool calls where subsequent chunks
  have: New/different call_id, Empty name, Partial arguments" — merged into the
  last valid call (`forge_domain/src/tools/call/tool_call.rs:117-145`). GLM
  "may send complete usage in every chunk (need to replace, not accumulate)"
  (`forge_domain/src/result_stream_ext.rs:75-90`).

### zeroclaw (`zeroclaw/crates/`) — the only JWT signer

- `zhipu_jwt_bearer` (`zeroclaw-providers/src/compatible.rs:196-240`): split key on
  first `.`; header `{"alg":"HS256","typ":"JWT","sign_type":"SIGN"}`; payload
  `{"api_key":id,"exp":now_ms+210_000,"timestamp":now_ms}` (ms); HMAC-SHA256,
  base64url no padding; `Authorization: Bearer <jwt>`, minted per request.
  Matches the official optional JWT recipe (§1). Live test
  `zeroclaw/tests/live/zai_jwt_auth.rs:16-27`. Older `glm.rs` is excluded from
  the build (`zeroclaw-providers/src/lib.rs:16`).
- `zai` slot defaults to the coding endpoint `https://api.z.ai/api/coding/paas/v4`
  (`zeroclaw-providers/src/lib.rs:175`); region enums map Cn/Global for both
  families (`zeroclaw-config/src/schema.rs:2677-2689,2779-2789`).
- `tool_stream:true` with tools for `*.z.ai` hosts only (`compatible.rs:1230-1245`).
- 429 containing 1113/1311 or "insufficient balance"/"plan does not include" is
  non-retryable (`zeroclaw-providers/src/reliable.rs:835-872`).

### openclaw (`openclaw/extensions/zai/`)

- Four URLs (coding/general x global/cn) (`model-definitions.ts:5-10`); env
  `ZAI_API_KEY` or `Z_AI_API_KEY` (`index.ts:272`); endpoint auto-detect by
  `POST {base}/chat/completions` ping, treating 404 / `1211` / `1311` as
  unsupported-model (`detect.ts:46-239`).
- Effort maps: 5.2 low/medium/high/adaptive -> high, xhigh/max -> max; 5.3
  off/minimal/low -> low, medium/high -> high, xhigh/adaptive/max -> max
  (`provider-policy-api.ts:12-27`); opt-in `thinking:{type:"enabled",clear_thinking:false}`
  (`index.ts:104-133`); `tool_stream` on by default
  (`openclaw/src/llm/providers/stream-wrappers/zai.ts:11-25`).
- Overflow text `tokens in request more than max tokens allowed` (code 1210)
  and `Prompt exceeds max length` (1261) (`index.ts:277-280`); 1311 -> billing,
  1113 -> auth, `network_error` -> timeout
  (`openclaw/src/agents/failover/message-patterns.ts:97-98,192-194`); no strict
  tools, `max_tokens`, no store, no developer role for zai
  (`openclaw/packages/ai/src/transports/openai-completions-compat.ts:167-235`).

### hermes-agent (`hermes-agent/`)

- Env `GLM_API_KEY`/`ZAI_API_KEY`/`Z_AI_API_KEY`, default `https://api.z.ai/api/paas/v4`
  (`hermes_cli/auth.py:194-195`). Probes all four endpoints because "Z.AI bills
  general/coding plans and global/China endpoints separately ('Insufficient
  balance' on the wrong one)" (`hermes_cli/auth_zai_kimi.py:36-151`).
- Retries 429 "1305"/"temporarily overloaded" on the coding endpoint with
  backoff, without key rotation (`agent/retry_utils.py:20-27,143-174`;
  `agent/error_classifier.py:153-160`).

### crush (`crush/`)

- `thinking:{type:"enabled"|"disabled"}` and always `tool_stream:true` for ZAI
  (`internal/agent/coordinator.go:672-677,1378-1382`). Endpoints come from the
  external catwalk catalog (not in checkout).

### Cross-reference consensus and disagreements

- All except goose's `zai` and oh-my-pi's `zai` use Chat Completions; nobody
  uses the Responses endpoint. Every implementation except zeroclaw sends the
  raw key as Bearer.
- `clear_thinking:false` + `reasoning_content` replay: pi, opencode, mimo,
  vtcode (conditional), openclaw (opt-in), goose (Anthropic). Roo merges
  post-tool text into the tool message to avoid a user turn dropping reasoning.
- `tool_stream:true`: pi, oh-my-pi (GLM-5.2), goose coding, vtcode, zeroclaw,
  openclaw, crush.
- Known codes handled: 1113 (zeroclaw, openclaw), 1210/1261 overflow (openclaw,
  hermes, pi), 1211/1311 (openclaw, zeroclaw), 1302 (qwen-code, oh-my-pi),
  1305 (qwen-code, hermes), 1308 (oh-my-pi by Chinese text).

## Sources

| Source | Read | Establishes |
| - | - | - |
| https://docs.z.ai/llms.txt | 2026-10-17 | Full official page index; no OAuth page; no Anthropic-compat page |
| https://docs.z.ai/api-reference/introduction.md | 2026-10-17 | Standard base URL; HTTP Bearer |
| https://docs.z.ai/openapi.json | 2026-10-17 | `servers` `https://api.z.ai/api`; paths; bearerAuth |
| https://docs.z.ai/guides/develop/http/introduction.md | 2026-10-17 | Optional JWT HS256 `sign_type: SIGN` construction |
| https://docs.z.ai/api-reference/llm/chat-completion.md | 2026-10-17 | Request/response schema, thinking, clear_thinking, reasoning_effort, tool_stream, tool_choice auto-only, finish reasons, usage |
| https://docs.z.ai/api-reference/api-code.md | 2026-10-17 | Business error codes, HTTP mapping, error shape, SSE failure via finish_reason |
| https://docs.z.ai/api-reference/rate-limit | 2026-10-17 | 307 to console page (no doc content) |
| https://docs.z.ai/guides/capabilities/thinking-mode.md | 2026-10-17 | Interleaved/preserved thinking; Coding Plan default clear_thinking=false |
| https://docs.z.ai/guides/capabilities/thinking.md | 2026-10-17 | GLM-5.3 disabling thinking errors |
| https://docs.z.ai/guides/capabilities/streaming.md | 2026-10-17 | SSE chunk format, final-chunk usage, `[DONE]` |
| https://docs.z.ai/guides/capabilities/stream-tool.md | 2026-10-17 | tool_stream delta fields (says glm-5 only) |
| https://docs.z.ai/guides/capabilities/function-calling.md | 2026-10-17 | tool_choice only auto |
| https://docs.z.ai/guides/capabilities/cache.md | 2026-10-17 | Implicit caching; cached_tokens field |
| https://docs.z.ai/guides/overview/migrate-to-glm-new.md | 2026-10-17 | GLM-5.3 limits, forced thinking, effort, tool_stream usage |
| https://docs.z.ai/guides/llm/glm-5.3.md | 2026-10-17 | GLM-5.3 limits; three protocol URLs; plan-holder Chat-only note |
| https://docs.z.ai/guides/vlm/glm-5.3-flash.md | 2026-10-17 | Flash/FlashX modality and limits; FlashX not on plan |
| https://docs.z.ai/guides/llm/glm-5.2.md | 2026-10-17 | GLM-5.2 limits |
| https://docs.z.ai/release-notes/new-released.md | 2026-10-17 | Model release dates |
| https://docs.z.ai/devpack/quick-start.md | 2026-10-17 | Coding Plan endpoint table; key separation |
| https://docs.z.ai/devpack/tool/others.md | 2026-10-17 | Supported tools list; endpoint table; wrong-endpoint warning |
| https://docs.z.ai/devpack/overview.md | 2026-10-17 | Plan models, rerouting, credit quotas |
| https://docs.z.ai/devpack/faq.md | 2026-10-17 | Plan-only-in-supported-tools; 1113 causes |
| https://docs.z.ai/devpack/usage-policy.md | 2026-10-17 | Concurrency tiers; violation handling |
| https://docs.z.ai/devpack/latest-model.md | 2026-10-17 | Per-tool endpoints; Anthropic/Codex effort normalisation table |
| https://docs.z.ai/devpack/notice/api-key-management.md | 2026-10-17 | 2026-10-16 key-type billing; Compatible-key endpoint billing |
| https://docs.z.ai/devpack/tool/codex.md | 2026-10-17 | Responses endpoint `/api/v1`, `wire_api=responses`, Bearer |
| https://docs.z.ai/devpack/tool/claude.md | 2026-10-17 | Anthropic env config, `[1m]` names, timeout |
| https://docs.z.ai/devpack/tool/zcode.md | 2026-10-17 | ZCode "Use Subscription" auto-fills an API key |
| https://docs.z.ai/devpack/tool/goose.md | 2026-10-17 | Goose via Anthropic Compatible |
| https://docs.z.ai/legal-agreement/subscription-terms.md | 2026-10-17 | §4 Usage Rules: supported tools only, no general API use |
| https://docs.bigmodel.cn/llms.txt | 2026-10-17 | China page index (Claude/Responses compat pages exist) |
| https://docs.bigmodel.cn/cn/api/introduction.md | 2026-10-17 | China base URL; Bearer |
| https://docs.bigmodel.cn/cn/guide/develop/http/introduction.md | 2026-10-17 | China JWT construction (same) |
| https://docs.bigmodel.cn/cn/guide/develop/claude/introduction.md | 2026-10-17 | China Anthropic base URL; `x-api-key`; unspecified differences |
| https://docs.bigmodel.cn/cn/guide/develop/responses/introduction.md | 2026-10-17 | Responses base `/api/v1`; store/[DONE]/cancel divergences; effort mapping |
| https://docs.bigmodel.cn/api-reference/response/创建-response.md | 2026-10-17 | Responses request/event/error schema |
| https://docs.bigmodel.cn/cn/api/api-code.md | 2026-10-17 | China error codes (same set) |
| https://docs.bigmodel.cn/cn/api/rate-limit.md | 2026-10-17 | Concurrency model; 1302/1305 handling |
| https://docs.bigmodel.cn/cn/coding-plan/tool/others.md | 2026-10-17 | China endpoint table; China supported tools |
| https://docs.bigmodel.cn/cn/coding-plan/usage-notes.md | 2026-10-17 | China usage rules and bans |
| https://docs.bigmodel.cn/cn/coding-plan/notice/api-key-management.md | 2026-10-17 | China key billing table |
| https://docs.bigmodel.cn/cn/coding-plan/faq.md, latest-model.md | 2026-10-17 | China per-tool URLs; resource-pack use via `/api/paas/v4` |
| https://cdn.jsdelivr.net/npm/pi-zcode-provider@0.1.0/PROTOCOL.md | 2026-10-17 | [reverse-engineered] ZCode OAuth, CLI polling flow, key mint |
| oh-my-pi/packages/ai/src/registry/oauth/zai.ts:1-205 | 2026-10-17 | [ref] Business-login and key-mint implementation |
| oh-my-pi/packages/catalog/src/compat/rules/auth/{zai,zai-coding-plan,zhipu-coding-plan}.kdl | 2026-10-17 | [ref] Login rules, client id, URLs, no refresh |
| oh-my-pi/packages/ai/test/{zai-oauth,zhipu-coding-plan-login}.test.ts | 2026-10-17 | [ref] Exact request sequence and bodies |
| oh-my-pi/packages/catalog/src/compat/rules/{providers/zai.kdl,providers/zhipu-coding-plan.kdl,classes/glm.kdl,runtime/behavior.kdl} | 2026-10-17 | [ref] Routing, effort ladders, limits, timezone, flash route |
| oh-my-pi/packages/ai/src/providers/{openai-shared,openai-completions,transform-messages}.ts | 2026-10-17 | [ref] thinking/tool_stream encoding, replay, malformed-call sanitising |
| oh-my-pi/packages/ai/src/error/{rate-limit,retryable}.ts; usage/zai.ts | 2026-10-17 | [ref] 1302/1308 classification; quota endpoint |
| pi/packages/ai/src/{providers/zai.ts,providers/zai-coding-cn.ts,api/openai-completions.ts,utils/overflow.ts,api/anthropic-messages.ts} | 2026-10-17 | [ref] Endpoints, clear_thinking=false, tool_stream, overflow patterns |
| goose/crates/goose-providers/src/declarative/definitions/{zai,zai_coding_plan,zhipu}.json; tests/zai_coding_plan.rs | 2026-10-17 | [ref] Anthropic route with clear_thinking; reasoning replay |
| cline/sdk/packages/llms/src/providers/{builtins.ts,routing/glm-thinking.ts} | 2026-10-17 | [ref] thinking.type only on native endpoints |
| roo-code/src/api/{providers/zai.ts,transform/zai-format.ts} | 2026-10-17 | [ref] user-message reasoning drop workaround |
| opencode/packages/opencode/src/provider/{transform,error}.ts; test/tool/fixtures/models-api.json | 2026-10-17 | [ref] four models.dev endpoints; clear_thinking=false always |
| mimo-code/packages/cli/src/provider/{transform,error}.ts | 2026-10-17 | [ref] model_context_window_exceeded as error text |
| qwen-code/packages/core/src/{providers/presets/zai.ts,core/openaiContentGenerator/provider/zai.ts,utils/rateLimit.ts} | 2026-10-17 | [ref] reasoning_effort flattening; 1302/1305 retry |
| vtcode/crates/codegen/vtcode-llm/src/providers/zai.rs | 2026-10-17 | [ref] tool_choice coerced to auto; effort; tool_stream |
| forgecode/crates/{forge_repo/src/provider/provider.json,forge_domain/src/tools/call/tool_call.rs,forge_domain/src/result_stream_ext.rs} | 2026-10-17 | [ref] malformed fragmented tool-call chunks; per-chunk cumulative usage |
| zeroclaw/crates/zeroclaw-providers/src/{compatible.rs,reliable.rs,lib.rs} | 2026-10-17 | [ref] JWT signing; non-retryable 1113/1311 |
| openclaw/extensions/zai/{index.ts,detect.ts,provider-policy-api.ts,model-definitions.ts} | 2026-10-17 | [ref] endpoint probe; effort maps; overflow/1311/1113 classification |
| hermes-agent/{hermes_cli/auth_zai_kimi.py,agent/retry_utils.py} | 2026-10-17 | [ref] four-endpoint probe; 1305 backoff |
| crush/internal/agent/coordinator.go | 2026-10-17 | [ref] thinking type; tool_stream always |

## Unknown / could not verify

- **Live acceptance of anything.** No request was sent to any Z.ai endpoint;
  every wire statement is documentation or reference code.
- **Blackbird's eligibility for Coding Plan quota.** Terms enumerate supported
  tools; Blackbird is absent. Whether Z.ai detects or enforces by client
  identity (User-Agent, headers, request signing) is undocumented. A written
  agreement is the only documented exception.
- **Responses API compatibility with Blackbird's `responses` /
  `responses-portable` adapters.** Unverified: plaintext `reasoning` items vs
  OpenAI `encrypted_content`/`summary`; absence of `[DONE]`; `store` default
  false; `tool_choice` limited to `none|auto`; string error codes; whether
  `parallel_tool_calls`, `include`, or `strict` are rejected or ignored.
  z.ai's own docs give only the base URL; schema details are from BigModel's
  reference and assumed identical for `api.z.ai/api/v1`.
- **Meaning of the GLM-5.3 note** "If you have previously subscribed to a GLM
  Coding Plan, including an expired subscription, you can currently access the
  model API only through the OpenAI Chat Completion-compatible protocol"
  (`guides/llm/glm-5.3.md` L66): whether it means Standard keys on such
  accounts cannot use `/api/v1` or `/api/anthropic` at pay-as-you-go rates.
- **Anthropic-compatible feature matrix:** support for thinking `signature`
  validation, `redacted_thinking`, `cache_control`, `tool_choice` `any`/`tool`,
  `output_config.effort` outside Claude Code, beta headers, and the exact SSE
  event set. Only reference-code behaviour exists. Whether `glm-5.3-flash`
  is now served on `/api/anthropic` (official Claude Code config says yes;
  oh-my-pi #10539 said no).
- **Contradictions inside official docs:** GLM-5.3 `thinking.type: disabled`
  "will fail" (model page) vs "converted to `low`" (Coding Plan latest-model
  table); `tool_stream` "limited to `glm-5`" (stream-tool guide) vs 5.3..4.6
  (API reference); error `code` string (examples) vs integer (schema);
  Cline context "other models 200000" vs 1M for 5.3.
- **Retry-After header** presence/semantics: undocumented. Reset times only in
  message text; timezone (Beijing) is reference-derived.
- **Reasoning effort over Chat Completions on `/api/coding/paas/v4`** for
  values outside `low|high|max` on GLM-5.3 (reject vs map) is undocumented
  for Chat Completions; the latest-model table describes mapping only for
  tool inputs generally.
- **OAuth flows** (oh-my-pi; ZCode CLI polling `zcode.z.ai/api/v1/oauth/cli/*`)
  are reverse-engineered, unofficial, tied to ZCode's client id, and could be
  revoked or start requiring `x-client-sig`/`x-client-pow` at any time.
  The business-token lifetime ("no `exp`") is a third-party claim.
- **Rate-limit numbers** for Standard API keys are only in the authenticated
  console (`z.ai/manage-apikey/rate-limits`); not readable here.
- **docs.z.ai (international) Responses and Anthropic reference pages** do not
  exist in its index; parity with BigModel is assumed, not shown.
