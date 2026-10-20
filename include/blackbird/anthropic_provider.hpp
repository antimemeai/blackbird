#pragma once
#include "blackbird/coding.hpp"
#include "blackbird/provider_auth.hpp"
namespace blackbird {
struct AnthropicConfig {
  ProviderAuthConfig auth;
  int timeout_seconds = 180;
  std::uint64_t max_tokens = 16384;
};
// Stateless native Messages route over the existing owned account/HTTP machinery.
class AnthropicCodingProvider final : public CodingProvider {
public:
  explicit AnthropicCodingProvider(AnthropicConfig config = {});
  std::string_view provider_identity() const noexcept override { return "anthropic"; }
  bool retain_request_state() const noexcept override { return true; }
  void begin_request_group(const Value &, const RawAdapterContext *) override;
  void end_request_group() noexcept override;
  const Value *group_binding() const noexcept override {
    return group_ ? &binding_ : nullptr;
  }
  Value respond(const Value &, const std::function<void(std::string_view)> &) override;
  Value respond(const Value &, const std::function<void(std::string_view)> &,
                const RawProviderOwner *) override;
  Value respond(const Value &, const std::function<void(std::string_view)> &,
                const RawProviderOwner *, const RawAdapterContext *) override;

private:
  AnthropicConfig config_;
  Value binding_;
  bool group_ = false, responding_ = false;
};
Value export_anthropic_request(const Value &request, std::uint64_t max_tokens = 16384,
                               bool oauth = false);
void validate_anthropic_route(std::string_view model, std::string_view effort);
Result<Value> anthropic_completed_response(std::string_view stream,
                                           ValueLimits limits = {},
                                           const RawAdapterContext *context = nullptr);
} // namespace blackbird
