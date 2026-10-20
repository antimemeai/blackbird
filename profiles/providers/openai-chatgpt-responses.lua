-- Profile `openai-chatgpt-responses`: OpenAI Responses through the ChatGPT Codex
-- compatibility route only (https://chatgpt.com/backend-api/codex, src/openai.cpp:157-163).
-- The owned OpenAI route on https://api.openai.com/v1 (src/provider_auth.cpp:287) is not
-- profiled and uses conservative. Cited rows wait for probe rd7.2; until it runs every row
-- stays cited or conservative (plan §4.2).
return {
  schema = "blackbird.provider-profile/1",
  id = "openai-chatgpt-responses",
  revision = 1,
  match = {
    provider = "openai",
    protocol = "responses",
    base_url = "https://chatgpt.com/backend-api/codex",
  },
  sources = {
    reasoning = {
      url = "https://developers.openai.com/api/docs/guides/reasoning",
      read = "2026-10-19",
      title = "Reasoning guide",
    },
  },
  reasoning = {
    at_cut = "keep",
    at_rebind = "drop_foreign",
    side_request_reasoning = "keep",
    context = "default",
    basis = {
      at_cut = "cited:reasoning",
      at_rebind = "conservative:probe rd7.2 case 3 pending",
      side_request_reasoning = "conservative:a side request repeats the main request",
      context = "conservative:matches today: the request sends only reasoning.effort (src/coding.cpp:4359-4363)",
    },
  },
  turn = {
    start = "user_text",
    close = "none",
    open_turn = { "retain_current_turn", "boundary" },
    boundary = {
      form = "user_note",
      enabled = false,
      basis = "conservative:probe rd7.2 cases 5 and 6 pending",
    },
    basis = "cited:reasoning",
  },
  request_state = {
    placement = "request_local",
    basis = "conservative:matches today: OpenAiCodingProvider keeps retain_request_state() false (include/blackbird/coding.hpp:37), so the state item goes at the end of each request's input only (src/coding.cpp:4489-4493)",
  },
  archival = { mode = "allowed", basis = "cited:reasoning" },
}
