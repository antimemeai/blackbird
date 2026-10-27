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
std::vector<RenderedMessage> responses_render(const Value::Array &input,
                                              bool portable) {
  // Every Responses export sends the items as they are, one input item each, never
  // merged or reordered; a portable route sends developer items as system.
  std::vector<RenderedMessage> out;
  out.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    const auto &item = input[i];
    const auto *type = item.find("type");
    const auto *kind = type ? std::get_if<std::string>(&type->value()) : nullptr;
    RenderedMessage message;
    message.positions.push_back(i);
    if (kind && *kind == "function_call") {
      message.role = RenderedMessage::Role::assistant;
      message.calls = true;
    } else if (kind && *kind == "function_call_output") {
      message.role = RenderedMessage::Role::tool;
      message.results = true;
    } else if (kind && *kind == "reasoning") {
      message.role = RenderedMessage::Role::reasoning;
    } else if (!kind || *kind == "message") {
      const auto &role = string_field(item, "role");
      if (role == "user")
        message.role = RenderedMessage::Role::user;
      else if (role == "assistant")
        message.role = RenderedMessage::Role::assistant;
      else if (role == "system" || (role == "developer" && portable))
        message.role = RenderedMessage::Role::system;
      else if (role == "developer")
        message.role = RenderedMessage::Role::developer;
      else
        throw Error{ErrorCode::unsupported};
      const auto &content = field(item, "content");
      if (std::holds_alternative<std::string>(content.value()))
        message.text = true;
      else
        for (const auto &part : content.array())
          if (const auto *t = part.find("type");
              t && (*t == Value{"input_text"} || *t == Value{"output_text"}))
            message.text = true;
    } else
      throw Error{ErrorCode::unsupported};
    out.push_back(std::move(message));
  }
  return out;
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
  if (config_.timeout_seconds < 1 || config_.timeout_seconds > 3600)
    throw Error{ErrorCode::invalid_range};
  auto control = raw_wait_control();
  control.deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{config_.timeout_seconds};
  RawWaitScope deadline{control};
  RawProviderPhase phase;
  return phase.run([&] {
    phase.start(5, context);
    auto config = config_.auth;
    config.cancelled = cancelled;
    ProviderAuth auth{std::move(config)};
    const auto wire = export_registered_responses_request(
        request, string_field(binding_, "protocol") == "responses-portable");
    const auto response =
        auth.request(config_.provider, "responses", wire, config_.timeout_seconds,
                     capture, &binding_, owner, context);
    auto result =
        unwrap(registered_responses_completed_response(response.body, context));
    phase.finish(1);
    return result;
  });
}
} // namespace blackbird
