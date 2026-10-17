#pragma once
#include "blackbird/provider_auth.hpp"
#include "blackbird/provider_original.hpp"
#include "blackbird/value.hpp"
#include <filesystem>
#include <functional>

namespace blackbird {
struct RawProviderOwner;
// Credentials are deliberately absent from the request/response audit interface.
struct OpenAiConfig {
  std::string codex_executable = "codex";
  std::string curl_executable = "/usr/bin/curl";
  std::filesystem::path codex_home;
  std::size_t response_limit = 16 * 1024 * 1024;
  int timeout_seconds = 180;
  std::function<void(std::string_view)> response_observer;
  std::function<bool()> cancelled;
  const RawProviderOwner *raw_owner = nullptr;
  const RawAdapterContext *raw_context = nullptr;
  ProviderAuthConfig owned_auth;
};
class OpenAiLogin {
public:
  OpenAiLogin(OpenAiLogin &&) noexcept = default;
  OpenAiLogin &operator=(OpenAiLogin &&) noexcept = default;
  OpenAiLogin(const OpenAiLogin &) = delete;
  OpenAiLogin &operator=(const OpenAiLogin &) = delete;

private:
  struct Credentials {
    std::string token, account;
  };
  explicit OpenAiLogin(Credentials credentials)
      : access_token_(std::move(credentials.token)),
        account_id_(std::move(credentials.account)) {}
  RawEventRef resolution_;
  std::string access_token_, account_id_;
  Value binding_;
  bool owned_auth_ = false;
  friend Result<OpenAiLogin> codex_login(const OpenAiConfig &, bool, const Value *);
  friend Result<std::string> openai_http(const OpenAiConfig &, const OpenAiLogin &,
                                         std::string_view, const Value *);
};
// Metadata-only request-group identity; no refresh or account probe.
Result<Value> openai_request_binding(const OpenAiConfig &config);
// Compatibility renewal stays with Codex; owned accounts use ProviderAuth.
Result<OpenAiLogin> codex_login(const OpenAiConfig &config, bool refresh = false,
                                const Value *expected_binding = nullptr);
// Bound owned Responses route or fixed Codex compatibility endpoint.
Result<std::string> openai_http(const OpenAiConfig &config, const OpenAiLogin &login,
                                std::string_view route, const Value *request = nullptr);
struct TextPreview {
  std::string item_id;
  std::string text;
};
// Presentation only: never publishes context or dispatches a streamed call.
class ResponsePreview {
public:
  std::vector<TextPreview> feed(std::string_view bytes,
                                const RawAdapterContext *context = nullptr);

private:
  std::string pending_, data_;
  std::string anthropic_message_;
  std::vector<bool> anthropic_text_;
  std::size_t bytes_ = 0;
};
Result<Value> completed_response(std::string_view event_stream, ValueLimits limits = {},
                                 const RawAdapterContext *context = nullptr);
} // namespace blackbird
