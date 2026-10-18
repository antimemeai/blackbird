#include "blackbird/responses_provider.hpp"
#include "blackbird/json.hpp"
#include "blackbird/raw_auth.hpp"
#include <chrono>
#include <set>
namespace blackbird {
namespace {
void replace(Value &value, std::string_view name, Value next) {
  for (auto &[key, old] : value.object())
    if (key == name) {
      old = std::move(next);
      return;
    }
  value.object().emplace_back(name, std::move(next));
}
} // namespace
Value export_registered_responses_request(Value request, bool portable) {
  if (field(request, "store") != Value{false} ||
      field(request, "stream") != Value{true} ||
      (request.find("previous_response_id") &&
       field(request, "previous_response_id") != Value{}))
    throw Error{ErrorCode::unsupported};
  if (portable) {
    if (const auto *reasoning = request.find("reasoning")) {
      const auto *effort = reasoning->find("effort");
      if (effort && *effort != Value{"low"} && *effort != Value{"high"} &&
          *effort != Value{"max"})
        throw Error{ErrorCode::unsupported};
    }
    if (const auto *include = request.find("include"))
      for (const auto &item : include->array())
        if (item != Value{"reasoning.encrypted_content"})
          throw Error{ErrorCode::unsupported};
    replace(request, "include", Value{Value::Array{}});
    // Paper normalizes omitted strict to strict schemas, making optional native
    // arguments mandatory. Preserve the authored schema unless strict was explicit.
    for (auto &[key, input] : request.object()) {
      if (key == "tools")
        for (auto &tool : input.array())
          if (string_field(tool, "type") == "function" && !tool.find("strict"))
            replace(tool, "strict", Value{false});
      if (key == "input")
        for (auto &item : input.array()) {
          if (const auto *encrypted = item.find("encrypted_content");
              encrypted && *encrypted != Value{} && *encrypted != Value{""})
            throw Error{ErrorCode::unsupported};
          if (const auto *role = item.find("role"); role && *role == Value{"developer"})
            replace(item, "role", Value{"system"});
        }
    }
  }
  return export_provider_request(std::move(request));
}
Result<Value>
registered_responses_completed_response(std::string_view stream,
                                        const RawAdapterContext *context) {
  auto result = completed_response(stream, {}, context);
  if (!result.has_value())
    return result;
  try {
    const auto &response = result.value();
    if (const auto *error = response.find("error"); error && *error != Value{})
      throw Error{ErrorCode::external_unknown};
    std::set<std::string> calls;
    for (const auto &item : field(response, "output").array()) {
      const auto &type = string_field(item, "type");
      if (type != "function_call" && type != "custom_tool_call")
        continue;
      if (const auto *status = item.find("status");
          status && *status != Value{"completed"})
        throw Error{ErrorCode::incomplete};
      const auto &id = string_field(item, "call_id");
      if (id.empty() || string_field(item, "name").empty() || !calls.insert(id).second)
        throw Error{ErrorCode::corrupt};
      if (type == "function_call") {
        const auto arguments = unwrap(parse_json(field(item, "arguments").string()));
        (void)arguments.object();
      }
    }
    return result;
  } catch (const Error &error) {
    return Result<Value>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<Value>::failure({ErrorCode::allocation});
  }
}
RegisteredResponsesCodingProvider::RegisteredResponsesCodingProvider(
    RegisteredResponsesConfig config)
    : config_(std::move(config)) {}
void RegisteredResponsesCodingProvider::begin_request_group(
    const Value &, const RawAdapterContext *context) {
  if (group_ || responding_)
    throw Error{ErrorCode::busy};
  RawProviderPhase phase;
  phase.run([&] {
    phase.start(5, context);
    ProviderAuth auth{config_.auth};
    const RawAuthParent parent{phase.reference()};
    binding_ = auth.binding(config_.provider, &parent);
    const auto &protocol = string_field(binding_, "protocol");
    if (protocol != "responses" && protocol != "responses-portable")
      throw Error{ErrorCode::unsupported};
    group_ = true;
    phase.finish(1);
  });
}
void RegisteredResponsesCodingProvider::end_request_group() noexcept { group_ = false; }
Value RegisteredResponsesCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture) {
  return respond(request, capture, nullptr, nullptr);
}
Value RegisteredResponsesCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture,
    const RawProviderOwner *owner) {
  return respond(request, capture, owner, nullptr);
}
Value RegisteredResponsesCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture,
    const RawProviderOwner *owner, const RawAdapterContext *context) {
  if (responding_)
    throw Error{ErrorCode::busy};
  const bool temporary = !group_;
  if (temporary)
    begin_request_group(request, context);
  struct End {
    RegisteredResponsesCodingProvider *provider;
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
      const auto wire = export_registered_responses_request(
          request, string_field(binding_, "protocol") == "responses-portable");
      const auto response =
          auth.request(config_.provider, "responses", wire, config_.timeouts, capture,
                       &binding_, owner, context);
      auto result =
          unwrap(registered_responses_completed_response(response.body, context));
      phase.finish(1);
      return result;
    });
  } catch (const Error &error) {
    throw deadline.business(error);
  }
}
} // namespace blackbird
