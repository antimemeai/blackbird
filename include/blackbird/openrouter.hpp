#pragma once
#include "blackbird/coding.hpp"
#include "blackbird/provider_auth.hpp"
namespace blackbird {
struct OpenRouterConfig {
  ProviderAuthConfig auth;
  int timeout_seconds = 180;
};
// OpenRouter's main coding path uses stateless Responses; colleague Chat is separate.
class OpenRouterCodingProvider final : public CodingProvider {
public:
  explicit OpenRouterCodingProvider(OpenRouterConfig config = {});
  std::string_view provider_identity() const noexcept override {
    return "openrouter-responses";
  }
  void begin_request_group(const Value &, const RawAdapterContext *) override;
  void end_request_group() noexcept override;
  const Value *group_binding() const noexcept override {
    return group_ ? &binding_ : nullptr;
  }
  bool supports_prompt_cache_policy() const noexcept override { return group_; }
  Value respond(const Value &, const std::function<void(std::string_view)> &) override;
  Value respond(const Value &, const std::function<void(std::string_view)> &,
                const RawProviderOwner *) override;
  Value respond(const Value &, const std::function<void(std::string_view)> &,
                const RawProviderOwner *, const RawAdapterContext *) override;

private:
  OpenRouterConfig config_;
  Value binding_;
  bool group_ = false;
  bool responding_ = false;
};
Value export_openrouter_request(Value request);
Result<Value> openrouter_completed_response(std::string_view stream,
                                            const RawAdapterContext *context = nullptr);
} // namespace blackbird
