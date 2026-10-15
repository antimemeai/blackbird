#include "blackbird/request_cache.hpp"
#include "blackbird/context.hpp"
#include <algorithm>
#include <charconv>
namespace blackbird {
namespace {
constexpr std::string_view local_field = "blackbird_cache_policy";
void put(Value &value, std::string_view key, Value field) {
  for (auto &[name, old] : value.object())
    if (name == key) {
      old = std::move(field);
      return;
    }
  value.object().emplace_back(key, std::move(field));
}
bool part_markers(const Value &content) {
  const auto *parts = std::get_if<Value::Array>(&content.value());
  return parts && std::any_of(parts->begin(), parts->end(), [](const Value &part) {
           return part.find("prompt_cache_breakpoint") || part.find("cache_control");
         });
}
bool marker(const Value &input) {
  for (const auto &item : input.array()) {
    const auto *type = item.find("type");
    if (type && *type == Value{"function_call"})
      continue;
    if (type && *type == Value{"function_call_output"}) {
      const auto *choice = item.find("blackbird_output_parts");
      const auto *output = item.find("output");
      if (choice && *choice == Value{true} && output && part_markers(*output))
        return true;
    } else if (const auto *content = item.find("content");
               content && part_markers(*content))
      return true;
  }
  return false;
}
bool mark_parts(Value &content) {
  auto *array = std::get_if<Value::Array>(&content.value());
  if (!array)
    return false;
  for (auto it = content.array().rbegin(); it != content.array().rend(); ++it) {
    const auto *type = it->find("type");
    if (type && *type == Value{"input_text"} && it->find("text")) {
      put(*it, "prompt_cache_breakpoint", Value::object({{"mode", Value{"explicit"}}}));
      return true;
    }
  }
  return false;
}
bool mark_item(Value &item) {
  auto *content = [&]() -> Value * {
    const auto *type = item.find("type");
    const auto field_name =
        type && *type == Value{"function_call_output"} ? "output" : "content";
    for (auto &[name, field] : item.object())
      if (name == field_name)
        return &field;
    return nullptr;
  }();
  if (!content)
    return false;
  if (std::holds_alternative<std::string>(content->value())) {
    const auto *role = item.find("role");
    if (role && *role == Value{"assistant"})
      return false;
    *content = Value{Value::Array{
        Value::object({{"type", Value{"input_text"}}, {"text", *content}})}};
  }
  return mark_parts(*content);
}
} // namespace
PromptCacheMode prompt_cache_mode(const Value &value) {
  if (value == Value{"provider_default"})
    return PromptCacheMode::provider_default;
  if (value == Value{"stable_prefix"})
    return PromptCacheMode::stable_prefix;
  if (value == Value{"disabled"})
    return PromptCacheMode::disabled;
  throw Error{ErrorCode::invalid_range};
}
void prepare_request_cache(Value &request, const RequestCacheSetup &setup) {
  if (request.find(local_field))
    throw Error{ErrorCode::invalid_range};
  if (setup.mode == PromptCacheMode::provider_default)
    return;
  const auto &model = string_field(request, "model");
  const bool model_supported =
      setup.provider == "openrouter-responses"
          ? model == "openai/gpt-6.1-sol"
          : setup.provider == "openai-codex" && model == "gpt-6.1-sol";
  if (!setup.route_supported || !model_supported)
    throw Error{ErrorCode::unsupported};
  if (request.find("prompt_cache_options") || request.find("prompt_cache_retention") ||
      request.find("cache_control") || marker(field(request, "input")))
    throw Error{ErrorCode::conflict};
  request.object().emplace_back(
      local_field,
      Value::object(
          {{"mode", Value{setup.mode == PromptCacheMode::stable_prefix ? "stable_prefix"
                                                                       : "disabled"}},
           {"reusable_input_items", Value{Number{setup.reusable_input_items}}}}));
}
void export_request_cache(Value &request) {
  const auto *local = request.find(local_field);
  if (!local)
    return;
  const auto policy = *local;
  std::erase_if(request.object(),
                [](const auto &entry) { return entry.first == local_field; });
  const auto mode = prompt_cache_mode(field(policy, "mode"));
  put(request, "prompt_cache_options",
      Value::object(
          {{"mode", Value{mode == PromptCacheMode::disabled ? "explicit" : "implicit"}},
           {"ttl", Value{"30m"}}}));
  if (mode == PromptCacheMode::disabled)
    return;
  const auto count_text = field(policy, "reusable_input_items").number().text();
  std::size_t count = 0;
  const auto converted =
      std::from_chars(count_text.data(), count_text.data() + count_text.size(), count);
  if (converted.ec != std::errc{} ||
      converted.ptr != count_text.data() + count_text.size())
    throw Error{ErrorCode::invalid_range};
  for (auto &[name, input] : request.object())
    if (name == "input") {
      auto &items = input.array();
      if (count > items.size())
        throw Error{ErrorCode::invalid_range};
      while (count)
        if (mark_item(items[--count]))
          return;
    }
  throw Error{ErrorCode::unsupported};
}
} // namespace blackbird
