#pragma once
#include "blackbird/provider_original.hpp"
#include "blackbird/value.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace blackbird {
struct RawProviderOwner;
struct RawAuthParent;
struct RawEventRef;
// Secret-bearing inputs and replies never enter the model/tool/audit interface.
struct ProviderHttpRequest {
  std::string url, method = "POST", body;
  std::vector<std::pair<std::string, std::string>> headers;
  int timeout_seconds = 30;
};
struct ProviderHttpResponse {
  int status = 0;
  std::string body;
};
using ProviderHttp =
    std::function<int(const ProviderHttpRequest &, ProviderHttpResponseLease &)>;
struct ProviderAuthConfig {
  std::filesystem::path directory;
  std::filesystem::path metrics_directory;
  std::string curl_executable = "/usr/bin/curl";
  std::string openssl_executable = "/usr/bin/openssl";
  std::function<bool()> cancelled;
  ProviderHttp http; // Deterministic transport injection, not a model setting.
  std::function<std::int64_t()> now;
  bool openrouter_metadata = false;
  bool openrouter_no_response_cache = false;
};
ProviderHttpResponse
provider_http(const ProviderHttpRequest &request, const ProviderAuthConfig &config = {},
              const std::function<void(std::string_view)> &observer = {},
              const RawProviderOwner *raw_owner = nullptr, bool application = false,
              const RawEventRef *auth_resolution = nullptr,
              const RawAdapterContext *raw_context = nullptr);
void require_provider_success(int status);
// HTTP status of a provider reply that require_provider_success refused; none for
// transport, stream or refresh failures. Error itself carries no text.
std::optional<int> provider_http_status(const Error &error) noexcept;
// One display line from a provider's JSON error body: error.message (OpenAI, Paper,
// MiMo, OpenRouter, Anthropic envelopes), a string error, message or detail. Strips
// Blackbird's own curl "\n<status>" trailer. None when the body is not such JSON.
std::optional<std::string> provider_error_message(std::string_view body, int status);

struct OAuthExpected {
  std::int64_t account_revision = -2;
  std::int64_t selection_revision = -1;
};
struct OpenAIIdentity {
  std::string_view token;
  std::string_view client;
  std::string_view nonce;
};
class ProviderAuth {
public:
  explicit ProviderAuth(ProviderAuthConfig config = {});
  // Metadata only. Does not refresh, probe an account, or expose tokens.
  Value catalog() const;
  Value status() const;
  bool selected(std::string_view provider, const RawAuthParent *parent = nullptr) const;
  void set_key(std::string_view provider, std::string_view account, std::string key,
               const RawAuthParent *parent = nullptr);
  void use(std::string_view provider, std::string_view account,
           const RawAuthParent *parent = nullptr);
  void logout(std::string_view provider, std::string_view account,
              const RawAuthParent *parent = nullptr);
  void add_provider(const std::string &id, std::string base_url,
                    const std::string &protocol, const std::string &environment,
                    const RawAuthParent *parent = nullptr);
  Value descriptor(std::string_view provider,
                   const RawAuthParent *parent = nullptr) const;
  Value registration(std::string_view provider, std::string_view account,
                     const RawAuthParent *parent = nullptr) const;
  Value binding(std::string_view provider, const RawAuthParent *parent = nullptr) const;
  // One OAuth transaction at a time per account. Returned device data is safe
  // for the operator, not the token/device_code. Poll resumes private state.
  Value device_begin(std::string_view provider, std::string_view account,
                     const RawAuthParent *parent = nullptr);
  Value device_poll(std::string_view provider, std::string_view account,
                    const RawAuthParent *parent = nullptr);
  void browser_login(std::string_view provider, std::string_view account,
                     const std::function<void(std::string_view)> &notice,
                     const std::function<std::string()> &read_secret,
                     const RawAuthParent *parent = nullptr);
  // Browser flow implemented separately; commits only a complete token record.
  void save_oauth(std::string_view provider, std::string_view account,
                  const Value &tokens, std::string client_id, std::string token_url,
                  Value metadata = Value::object({}), OAuthExpected expected = {},
                  const RawAuthParent *parent = nullptr);
  ProviderHttpResponse
  request(std::string_view provider, std::string_view route, const Value &body,
          int timeout_seconds,
          const std::function<void(std::string_view)> &observer = {},
          const Value *expected_binding = nullptr,
          const RawProviderOwner *raw_owner = nullptr,
          const RawAdapterContext *raw_context = nullptr);
  std::filesystem::path directory() const;
  const ProviderAuthConfig &config() const { return config_; }

private:
  ProviderAuthConfig config_;
};
// Dedicated operator process; keys/callback codes never go through chat history.
int provider_auth_cli(int argc, char **argv);
// Direct deterministic checks of PKCE/JWT validation use these owning helpers.
std::string auth_pkce_challenge(std::string_view verifier,
                                const ProviderAuthConfig &config = {});
Value auth_validate_openai_identity(OpenAIIdentity identity, const Value &jwks,
                                    std::int64_t now,
                                    const ProviderAuthConfig &config = {});
} // namespace blackbird
