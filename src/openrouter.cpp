#include "blackbird/openrouter.hpp"
#include "blackbird/json.hpp"
#include "blackbird/raw_auth.hpp"
#include "blackbird/raw_caller.hpp"
#include <chrono>
#include <set>
namespace blackbird {
namespace {
void put(Value &value, std::string_view name, Value field) {
  for (auto &[key, old] : value.object())
    if (key == name) {
      old = std::move(field);
      return;
    }
  value.object().emplace_back(name, std::move(field));
}
std::string text(const Value &value, std::string_view name) {
  const auto *field = value.find(name);
  if (!field || *field == Value{})
    return {};
  if (const auto *string = std::get_if<std::string>(&field->value()))
    return *string;
  throw Error{ErrorCode::corrupt};
}
[[noreturn]] void stream_error(const Value &event) {
  const auto *response = event.find("response");
  const auto kind = text(response ? *response : event, "error_type");
  if (kind == "authentication" || kind == "permission_denied" ||
      kind == "payment_required")
    throw Error{ErrorCode::provider_auth};
  // Canonical stream failures stay terminal. HTTP/transport retries are separate;
  // server_error alone cannot distinguish auth from overload.
  throw Error{ErrorCode::external_unknown};
}
} // namespace
OpenRouterCodingProvider::OpenRouterCodingProvider(OpenRouterConfig config)
    : config_(std::move(config)) {
  config_.auth.openrouter_metadata = true;
  config_.auth.openrouter_no_response_cache = true;
}
void OpenRouterCodingProvider::begin_request_group(const Value &request,
                                                   const RawAdapterContext *context) {
  if (group_ || responding_)
    throw Error{ErrorCode::busy};
  RawProviderPhase phase;
  phase.run([&] {
    phase.start(5, context);
    const auto &model = string_field(request, "model");
    if (model.empty() || model.size() > 256 || model.find('/') == std::string::npos)
      throw Error{ErrorCode::invalid_range};
    ProviderAuth auth{config_.auth};
    const RawAuthParent parent{phase.reference()};
    binding_ = auth.binding("openrouter", &parent);
    if (string_field(binding_, "base_url") != "https://openrouter.ai/api/v1")
      throw Error{ErrorCode::conflict};
    group_ = true;
    phase.finish(1);
  });
}
void OpenRouterCodingProvider::end_request_group() noexcept { group_ = false; }
Value export_openrouter_request(Value request) {
  if (field(request, "store") != Value{false} ||
      (request.find("previous_response_id") &&
       !std::holds_alternative<std::nullptr_t>(
           field(request, "previous_response_id").value())))
    throw Error{ErrorCode::unsupported};
  auto routing =
      request.find("provider") ? field(request, "provider") : Value::object({});
  if (const auto *required = routing.find("require_parameters");
      required && *required != Value{true})
    throw Error{ErrorCode::unsupported};
  put(routing, "require_parameters", Value{true});
  put(request, "provider", std::move(routing));
  if (const auto *session = request.find("session_id");
      session && (session->string().empty() || session->string().size() > 256))
    throw Error{ErrorCode::invalid_range};
  // Preserve all provider-produced fields. Authored assistant views require a
  // completed message envelope; generated IDs describe this submitted view only.
  auto &input = [&]() -> Value & {
    for (auto &[key, value] : request.object())
      if (key == "input")
        return value;
    throw Error{ErrorCode::invalid_range};
  }();
  std::size_t ordinal = 0;
  for (auto &item : input.array()) {
    if (text(item, "role") == "assistant") {
      if (!item.find("id"))
        put(item, "id", Value{"blackbird_view_" + std::to_string(ordinal)});
      if (!item.find("status"))
        put(item, "status", Value{"completed"});
      if (!item.find("type"))
        put(item, "type", Value{"message"});
      if (const auto *content = item.find("content");
          content && std::holds_alternative<std::string>(content->value()))
        put(item, "content",
            Value{Value::Array{
                Value::object({{"type", Value{"output_text"}}, {"text", *content}})}});
    }
    ++ordinal;
  }
  return export_provider_request(std::move(request));
}
Result<Value> openrouter_completed_response(std::string_view stream,
                                            const RawAdapterContext *context) {
  try {
    if (stream.size() > ValueLimits{}.bytes)
      throw Error{ErrorCode::capacity};
    std::string data, canonical;
    Value metadata, tail;
    bool completed = false, done = false;
    auto event = [&] {
      if (data.empty())
        return;
      if (data == "[DONE]") {
        if (done || !completed)
          throw Error{ErrorCode::incomplete};
        done = true;
        data.clear();
        return;
      }
      if (done)
        throw Error{ErrorCode::corrupt};
      auto parsed = unwrap(parse_json(data));
      const auto type = text(parsed, "type");
      if (type == "error" || type == "response.error" || type == "response.failed" ||
          (type.empty() && parsed.find("error") && field(parsed, "error") != Value{}))
        stream_error(parsed);
      if (type == "response.incomplete")
        throw Error{ErrorCode::incomplete};
      if (const auto *router = parsed.find("openrouter_metadata")) {
        if (metadata != Value{} && metadata != *router)
          throw Error{ErrorCode::conflict};
        metadata = *router;
        if (type.empty()) {
          if (!completed || tail != Value{} || parsed.find("output"))
            throw Error{ErrorCode::corrupt};
          if (const auto *error = parsed.find("error"); error && *error != Value{})
            stream_error(parsed);
          tail = parsed;
          data.clear();
          return;
        }
      }
      if (completed || type.empty())
        throw Error{ErrorCode::corrupt};
      if (type == "response.completed")
        completed = true;
      canonical += "data: " + data + "\n\n";
      data.clear();
    };
    while (!stream.empty()) {
      const auto end = stream.find('\n');
      if (end == std::string_view::npos)
        throw Error{ErrorCode::incomplete};
      auto line = stream.substr(0, end);
      stream.remove_prefix(end + 1);
      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
      if (line.empty())
        event();
      else if (line.starts_with("data:")) {
        line.remove_prefix(5);
        if (line.starts_with(' '))
          line.remove_prefix(1);
        if (!data.empty())
          data += '\n';
        data += line;
      }
    }
    if (!data.empty() || !completed || !done)
      throw Error{ErrorCode::incomplete};
    auto response = unwrap(completed_response(canonical, {}, context));
    if (const auto *error = response.find("error"); error && *error != Value{})
      stream_error(response);
    if (const auto *error_type = response.find("error_type");
        error_type && *error_type != Value{} && *error_type != Value{""})
      stream_error(response);
    std::set<std::string> call_ids;
    for (const auto &item : field(response, "output").array()) {
      const auto type = text(item, "type");
      if (type == "function_call" || type == "custom_tool_call") {
        if (const auto *status = item.find("status");
            status && *status != Value{"completed"})
          throw Error{ErrorCode::incomplete};
        const auto call_id = text(item, "call_id");
        if (call_id.empty() || text(item, "name").empty() ||
            !call_ids.insert(call_id).second)
          throw Error{ErrorCode::corrupt};
        if (type == "function_call") {
          const auto *arguments = item.find("arguments");
          if (!arguments || !std::holds_alternative<std::string>(arguments->value()))
            throw Error{ErrorCode::corrupt};
        }
      }
    }
    if (metadata != Value{}) {
      if (const auto *reported = response.find("openrouter_metadata");
          reported && *reported != metadata)
        throw Error{ErrorCode::conflict};
      put(response, "openrouter_metadata", std::move(metadata));
    }
    if (tail != Value{})
      put(response, "openrouter_tail", std::move(tail));
    return Result<Value>::success(std::move(response));
  } catch (const Error &error) {
    return Result<Value>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<Value>::failure({ErrorCode::allocation});
  }
}
Value OpenRouterCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture) {
  return respond(request, capture, nullptr, nullptr);
}
Value OpenRouterCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture,
    const RawProviderOwner *owner) {
  return respond(request, capture, owner, nullptr);
}
Value OpenRouterCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture,
    const RawProviderOwner *owner, const RawAdapterContext *context) {
  if (responding_)
    throw Error{ErrorCode::busy};
  const bool temporary = !group_;
  if (temporary)
    begin_request_group(request, context);
  struct End {
    OpenRouterCodingProvider *provider;
    bool &responding;
    ~End() {
      responding = false;
      if (provider)
        provider->end_request_group();
    }
  } end{temporary ? this : nullptr, responding_};
  responding_ = true;
  if (!valid_provider_timeouts(config_.timeouts))
    throw Error{ErrorCode::invalid_range};
  ProviderDeadline deadline{config_.timeouts.request_seconds};
  try {
    RawProviderPhase phase;
    return phase.run([&] {
      phase.start(5, context);
      auto config = config_.auth;
      config.cancelled = cancelled;
      ProviderAuth auth{std::move(config)};
      const auto response =
          auth.request("openrouter", "responses", export_openrouter_request(request),
                       config_.timeouts, capture, &binding_, owner, context);
      auto result = unwrap(openrouter_completed_response(response.body, context));
      phase.finish(1);
      return result;
    });
  } catch (const Error &error) {
    throw deadline.business(error);
  }
}
} // namespace blackbird
