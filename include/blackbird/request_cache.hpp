#pragma once
#include "blackbird/value.hpp"
namespace blackbird {
enum class PromptCacheMode { provider_default, stable_prefix, disabled };
PromptCacheMode prompt_cache_mode(const Value &value);
struct RequestCacheSetup {
  PromptCacheMode mode = PromptCacheMode::provider_default;
  bool route_supported = false;
  std::string_view provider;
  std::size_t reusable_input_items = 0;
};
// Local native assembly metadata is retained with the final native request.
void prepare_request_cache(Value &request, const RequestCacheSetup &setup);
// Called only by the external Responses encoder, after tool value conversion.
void export_request_cache(Value &request);
} // namespace blackbird
