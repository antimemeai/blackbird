#pragma once
#include "blackbird/coding.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/turn_state.hpp"
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
// The top-level `system` text the export sends: the request's instructions, after the
// OAuth identity line on an OAuth account. The chapter binding's instructions_digest
// covers this text, so an API-key/OAuth switch is a prefix change (design §4.1).
std::string anthropic_system_text(std::string_view instructions, bool oauth);
// The provider-visible messages the export renders from input items, with the input
// position of every block (design §7.1). Throws as the export does for input it
// refuses.
std::vector<RenderedMessage> anthropic_render(const Value::Array &input);
Result<Value> anthropic_completed_response(std::string_view stream,
                                           ValueLimits limits = {},
                                           const RawAdapterContext *context = nullptr);
} // namespace blackbird
