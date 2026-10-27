#pragma once
#include "blackbird/coding.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/turn_state.hpp"
namespace blackbird {
struct RegisteredResponsesConfig {
  std::string provider;
  ProviderAuthConfig auth;
  int timeout_seconds = 180;
};
// Registered stateless Responses routes; builtin OpenAI/OpenRouter keep their adapters.
class RegisteredResponsesCodingProvider final : public CodingProvider {
public:
  explicit RegisteredResponsesCodingProvider(RegisteredResponsesConfig config);
  std::string_view provider_identity() const noexcept override {
    return config_.provider;
  }
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
  RegisteredResponsesConfig config_;
  Value binding_;
  bool group_ = false, responding_ = false;
};
Value export_registered_responses_request(Value request, bool portable);
// The provider-visible messages of a Responses input (design §7.1): one per item, in
// order, as every Responses export (export_provider_request, the registered and
// portable routes) sends them; portable routes send developer items as system.
std::vector<RenderedMessage> responses_render(const Value::Array &input, bool portable);
Result<Value>
registered_responses_completed_response(std::string_view stream,
                                        const RawAdapterContext *context = nullptr);
} // namespace blackbird
