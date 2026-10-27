#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace blackbird {
// Blackbird's effort vocabulary: every reasoning effort some supported endpoint
// documents. Which values an endpoint accepts lives in the settings table.
enum class Effort : std::uint8_t {
  none,
  minimal,
  low,
  medium,
  high,
  xhigh,
  max,
  ultra
};
inline constexpr std::array all_efforts{Effort::none,   Effort::minimal, Effort::low,
                                        Effort::medium, Effort::high,    Effort::xhigh,
                                        Effort::max,    Effort::ultra};
std::optional<Effort> parse_effort(std::string_view text) noexcept;
std::string_view effort_name(Effort effort) noexcept;

// A vendor document and the date Blackbird's table was checked against it.
struct DocSource {
  std::string_view url;
  std::string_view read;
};
struct EffortSupport {
  Effort effort;
  std::string_view meaning; // What the vendor documents this value doing.
};
// Efforts a model family does not support; model names match by prefix.
struct ModelEffortRule {
  std::string_view model_prefix;
  std::span<const Effort> unsupported;
  DocSource source;
};
// A model the endpoint refuses although another document lists it.
struct ModelRestriction {
  std::string_view model;
  std::string_view reason;
  DocSource source;
};
struct OutputCeiling {
  std::uint64_t tokens = 0; // 0: no fixed number documented for the endpoint.
  bool includes_reasoning = false;
  std::string_view note;
};
// One documented endpoint, keyed by the wire protocol Blackbird speaks to it and
// its exact base URL (registered URLs are stored without a trailing slash).
struct EndpointSettings {
  std::string_view name{};
  std::string_view protocol{};
  std::string_view base_url{};
  std::span<const EffortSupport> efforts{};
  std::string_view default_effort{};
  std::string_view equivalence{}; // Levels the vendor documents as identical.
  std::span<const ModelEffortRule> model_efforts{};
  std::span<const std::string_view> models{}; // Closed documented list; empty = open.
  DocSource model_source{};                   // Where that list is documented.
  std::span<const ModelRestriction> restricted{};
  OutputCeiling output{};
  std::span<const std::string_view> unsupported_parameters{};
  std::span<const DocSource> sources{};
};
std::span<const EndpointSettings> provider_settings() noexcept;
const EndpointSettings *endpoint_settings(std::string_view protocol,
                                          std::string_view base_url) noexcept;
// Wire-protocol rule for an endpoint the table does not describe.
std::span<const Effort> protocol_efforts(std::string_view protocol) noexcept;
// Efforts the endpoint accepts for this model, in vocabulary order.
std::vector<Effort> accepted_efforts(const EndpointSettings &endpoint,
                                     std::string_view model);
// The endpoint a launch will call and the model and effort chosen for it.
struct LaunchChoice {
  std::string_view protocol, base_url, model, effort;
};
// None when the endpoint accepts the choice; otherwise the operator-facing refusal
// naming the accepted values and the document.
std::optional<std::string> refuse_launch(const LaunchChoice &choice);
// Base URL of the Codex ChatGPT backend used by the builtin openai provider when no
// owned OpenAI account is selected.
inline constexpr std::string_view codex_chatgpt_base_url =
    "https://chatgpt.com/backend-api/codex";
} // namespace blackbird
