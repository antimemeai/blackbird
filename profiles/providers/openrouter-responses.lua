-- Profile `openrouter-responses`: OpenRouter's stateless Responses route
-- (OpenRouterCodingProvider). It holds only match and sources; every behavioural row is
-- the conservative profile's until a later unit adds the fields it consumes.
return {
  schema = "blackbird.provider-profile/1",
  id = "openrouter-responses",
  revision = 1,
  match = { provider = "openrouter", protocol = "responses" },
  sources = {
    transforms = {
      url = "https://openrouter.ai/docs/guides/features/message-transforms",
      read = "2026-10-19",
      title = "Message transforms",
    },
    caching = {
      url = "https://openrouter.ai/docs/guides/best-practices/prompt-caching",
      read = "2026-10-19",
      title = "Prompt caching",
    },
  },
}
