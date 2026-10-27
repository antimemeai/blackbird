#include "blackbird/anthropic_provider.hpp"
#include "blackbird/json.hpp"
#include "blackbird/provider_settings.hpp"
#include "blackbird/raw_auth.hpp"
#include <charconv>
#include <chrono>
#include <limits>
#include <set>
namespace blackbird {
namespace {
void put(Value &value, std::string_view name, Value next) {
  for (auto &[key, old] : value.object())
    if (key == name) {
      old = std::move(next);
      return;
    }
  value.object().emplace_back(name, std::move(next));
}
std::uint64_t natural(const Value &value) {
  const auto *number = std::get_if<Number>(&value.value());
  if (!number)
    throw Error{ErrorCode::corrupt};
  const auto text = number->text();
  std::uint64_t result = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    throw Error{ErrorCode::corrupt};
  return result;
}
std::string optional_text(const Value &value, std::string_view key) {
  const auto *found = value.find(key);
  if (!found)
    return {};
  const auto *text = std::get_if<std::string>(&found->value());
  if (!text)
    throw Error{ErrorCode::corrupt};
  return *text;
}
Value text_block(std::string text) {
  return Value::object({{"type", Value{"text"}}, {"text", Value{std::move(text)}}});
}
void validate_thinking(const Value &block) {
  const auto &kind = string_field(block, "type");
  if (kind == "thinking") {
    (void)string_field(block, "thinking");
    if (string_field(block, "signature").empty())
      throw Error{ErrorCode::incomplete};
  } else if (kind == "redacted_thinking") {
    if (string_field(block, "data").empty())
      throw Error{ErrorCode::incomplete};
  } else
    throw Error{ErrorCode::unsupported};
}
struct BlockProjection {
  const Value &item;
  Value block;
};
Value native_block(BlockProjection input) {
  if (const auto *original = input.item.find("anthropic_content")) {
    // Semantic context edits must not silently disagree with a retained block.
    const auto &projected = input.block;
    for (const auto &[name, value] : projected.object())
      if (field(*original, name) != value)
        throw Error{ErrorCode::conflict};
    return *original;
  }
  return std::move(input.block);
}
class MessageStream {
public:
  explicit MessageStream(ValueLimits limits) : limits_(limits) {}
  void event(const Value &event) {
    if (++events_ > limits_.nodes)
      throw Error{ErrorCode::capacity};
    const auto &kind = string_field(event, "type");
    if (kind == "error")
      throw Error{ErrorCode::external_unknown};
    if (kind == "ping")
      return;
    const bool known = kind == "message_start" || kind == "content_block_start" ||
                       kind == "content_block_delta" || kind == "content_block_stop" ||
                       kind == "message_delta" || kind == "message_stop";
    // Future envelope events carry no recognized semantic state. Unknown blocks
    // and deltas inside the recognized lifecycle are refused below.
    if (!known)
      return;
    if (stopped_)
      throw Error{ErrorCode::corrupt};
    if (kind == "message_start") {
      if (started_)
        throw Error{ErrorCode::conflict};
      message_ = field(event, "message");
      if (string_field(message_, "type") != "message" ||
          string_field(message_, "role") != "assistant" ||
          string_field(message_, "id").empty() ||
          !field(message_, "content").array().empty())
        throw Error{ErrorCode::corrupt};
      (void)string_field(message_, "model");
      if (const auto *usage = message_.find("usage"))
        merge_usage(*usage);
      started_ = true;
      return;
    }
    if (!started_)
      throw Error{ErrorCode::corrupt};
    if (kind == "content_block_start") {
      if (open_ || delta_ || natural(field(event, "index")) != blocks_.size() ||
          blocks_.size() >= limits_.nodes)
        throw Error{ErrorCode::corrupt};
      auto block = field(event, "content_block");
      const auto &type = string_field(block, "type");
      content_text_.clear();
      signature_.clear();
      if (type == "text")
        content_text_ = string_field(block, "text");
      else if (type == "thinking") {
        content_text_ = string_field(block, "thinking");
        signature_ = string_field(block, "signature");
      } else if (type == "redacted_thinking")
        validate_thinking(block);
      else if (type == "tool_use") {
        const auto &id = string_field(block, "id");
        if (id.empty() || string_field(block, "name").empty() ||
            !calls_.insert(id).second)
          throw Error{ErrorCode::conflict};
        (void)field(block, "input").object();
      } else
        throw Error{ErrorCode::unsupported};
      blocks_.push_back(std::move(block));
      partial_.clear();
      input_delta_ = false;
      open_ = true;
      return;
    }
    if (kind == "content_block_delta" || kind == "content_block_stop") {
      if (!open_ || natural(field(event, "index")) != blocks_.size() - 1)
        throw Error{ErrorCode::corrupt};
      auto &block = blocks_.back();
      const auto type = string_field(block, "type");
      if (kind == "content_block_stop") {
        if (type == "tool_use" && input_delta_) {
          if (!field(block, "input").object().empty())
            throw Error{ErrorCode::conflict};
          auto input = unwrap(parse_json(partial_, limits_));
          (void)input.object();
          put(block, "input", std::move(input));
        }
        if (type == "text")
          put(block, "text", Value{std::move(content_text_)});
        else if (type == "thinking") {
          put(block, "thinking", Value{std::move(content_text_)});
          put(block, "signature", Value{std::move(signature_)});
          validate_thinking(block);
        }
        open_ = false;
        return;
      }
      const auto &delta = field(event, "delta");
      const auto &change = string_field(delta, "type");
      if (type == "tool_use" && change == "input_json_delta") {
        const auto &fragment = string_field(delta, "partial_json");
        if (fragment.size() > limits_.bytes - partial_.size())
          throw Error{ErrorCode::capacity};
        partial_ += fragment;
        input_delta_ = true;
      } else {
        std::string_view target;
        if (type == "text" && change == "text_delta")
          target = "text";
        else if (type == "thinking" && change == "thinking_delta")
          target = "thinking";
        else if (type == "thinking" && change == "signature_delta")
          target = "signature";
        else
          throw Error{ErrorCode::unsupported};
        auto &text = target == "signature" ? signature_ : content_text_;
        const auto &fragment = string_field(delta, target);
        if (fragment.size() > limits_.bytes - text.size())
          throw Error{ErrorCode::capacity};
        text += fragment;
      }
      return;
    }
    if (open_)
      throw Error{ErrorCode::incomplete};
    if (kind == "message_delta") {
      const auto &delta = field(event, "delta");
      if (const auto *reason = delta.find("stop_reason");
          reason && *reason != Value{}) {
        if (!stop_reason_.empty() && *reason != Value{stop_reason_})
          throw Error{ErrorCode::conflict};
        stop_reason_ = reason->string();
      }
      if (const auto *usage = event.find("usage"))
        merge_usage(*usage);
      delta_ = true;
    } else {
      if (!delta_ || stop_reason_.empty())
        throw Error{ErrorCode::incomplete};
      stopped_ = true;
    }
  }
  Value finish() {
    if (!started_ || !stopped_ || open_)
      throw Error{ErrorCode::incomplete};
    if (stop_reason_ != "end_turn" && stop_reason_ != "tool_use" &&
        stop_reason_ != "stop_sequence" && stop_reason_ != "refusal")
      throw Error{ErrorCode::incomplete};
    if ((stop_reason_ == "tool_use") != !calls_.empty())
      throw Error{ErrorCode::corrupt};
    Value::Array output;
    const auto id = string_field(message_, "id");
    std::size_t index = 0;
    for (auto &block : blocks_) {
      const auto &type = string_field(block, "type");
      Value item;
      if (type == "text") {
        item = Value::object(
            {{"type", Value{"message"}},
             {"id", Value{id + ":" + std::to_string(index)}},
             {"role", Value{"assistant"}},
             {"content",
              Value{Value::Array{Value::object({{"type", Value{"output_text"}},
                                                {"text", field(block, "text")}})}}}});
      } else if (type == "tool_use") {
        item = Value::object({{"type", Value{"function_call"}},
                              {"call_id", field(block, "id")},
                              {"name", field(block, "name")},
                              {"arguments", field(block, "input")}});
      } else
        item = Value::object(
            {{"type", Value{"reasoning"}}, {"summary", Value{Value::Array{}}}});
      item.object().emplace_back("anthropic_message_id", Value{id});
      item.object().emplace_back("anthropic_content", std::move(block));
      output.push_back(std::move(item));
      ++index;
    }
    Value::Object normalized;
    std::uint64_t input = 0, cached = 0, output_tokens = 0;
    for (const auto key :
         {"input_tokens", "cache_creation_input_tokens", "cache_read_input_tokens"})
      if (const auto *value = usage_.find(key)) {
        const auto tokens = natural(*value);
        if (tokens > std::numeric_limits<std::uint64_t>::max() - input)
          throw Error{ErrorCode::overflow};
        input += tokens;
        if (std::string_view{key} == "cache_read_input_tokens")
          cached = tokens;
      }
    if (const auto *value = usage_.find("output_tokens"))
      output_tokens = natural(*value);
    if (output_tokens > std::numeric_limits<std::uint64_t>::max() - input)
      throw Error{ErrorCode::overflow};
    if (usage_.find("input_tokens"))
      normalized.emplace_back("input_tokens", Value{Number{input}});
    if (usage_.find("output_tokens"))
      normalized.emplace_back("output_tokens", Value{Number{output_tokens}});
    if (usage_.find("input_tokens") && usage_.find("output_tokens"))
      normalized.emplace_back("total_tokens", Value{Number{input + output_tokens}});
    if (usage_.find("cache_read_input_tokens"))
      normalized.emplace_back(
          "input_tokens_details",
          Value::object({{"cached_tokens", Value{Number{cached}}}}));
    return Value::object({{"id", Value{id}},
                          {"model", field(message_, "model")},
                          {"output", Value{std::move(output)}},
                          {"usage", Value::object(std::move(normalized))}});
  }

private:
  void merge_usage(const Value &usage) {
    for (const auto &[key, value] : usage.object()) {
      if (key == "input_tokens" || key == "output_tokens" ||
          key == "cache_creation_input_tokens" || key == "cache_read_input_tokens") {
        (void)natural(value);
        put(usage_, key, value);
      }
    }
  }
  ValueLimits limits_;
  Value message_, usage_{Value::object({})};
  Value::Array blocks_;
  std::set<std::string> calls_;
  std::string partial_, stop_reason_, content_text_, signature_;
  std::size_t events_ = 0;
  bool started_ = false, stopped_ = false, open_ = false, delta_ = false,
       input_delta_ = false;
};
} // namespace
void validate_anthropic_route(std::string_view model, std::string_view effort) {
  if (model.empty() || model.size() > 256)
    throw Error{ErrorCode::invalid_range};
  // Documented efforts and per-model exclusions live in the provider settings table.
  if (refuse_launch({"messages", "https://api.anthropic.com/v1", model, effort}))
    throw Error{ErrorCode::unsupported};
}
Value export_anthropic_request(const Value &request, std::uint64_t max_tokens,
                               bool oauth) {
  for (const auto &[key, value] : request.object()) {
    (void)value;
    if (key != "model" && key != "instructions" && key != "input" && key != "tools" &&
        key != "reasoning" && key != "include" && key != "store" && key != "stream" &&
        key != "parallel_tool_calls" && key != "max_output_tokens" &&
        key != "previous_response_id" && key != "temperature" && key != "top_p" &&
        key != "stop" && key != "tool_choice")
      throw Error{ErrorCode::unsupported};
  }
  if (field(request, "store") != Value{false} ||
      field(request, "stream") != Value{true} ||
      (request.find("previous_response_id") &&
       field(request, "previous_response_id") != Value{}))
    throw Error{ErrorCode::unsupported};
  if (const auto *configured = request.find("max_output_tokens"))
    max_tokens = natural(*configured);
  if (max_tokens == 0)
    throw Error{ErrorCode::invalid_range};
  const auto &model = string_field(request, "model");
  const auto &effort = string_field(field(request, "reasoning"), "effort");
  validate_anthropic_route(model, effort);
  for (const auto &[key, value] : field(request, "reasoning").object()) {
    (void)value;
    if (key != "effort")
      throw Error{ErrorCode::unsupported};
  }
  if (const auto *included = request.find("include"))
    for (const auto &part : included->array())
      if (part != Value{"reasoning.encrypted_content"})
        throw Error{ErrorCode::unsupported};
  Value::Array tools;
  for (const auto &tool : field(request, "tools").array()) {
    if (string_field(tool, "type") != "function")
      throw Error{ErrorCode::unsupported};
    auto native = Value::object(
        {{"name", field(tool, "name")}, {"input_schema", field(tool, "parameters")}});
    if (const auto *description = tool.find("description"))
      native.object().emplace_back("description", *description);
    if (const auto *strict = tool.find("strict"))
      native.object().emplace_back("strict", *strict);
    tools.push_back(std::move(native));
  }
  Value::Array messages;
  std::string last_group;
  auto append = [&](std::string_view role, Value block, const std::string &group) {
    if (messages.empty() || string_field(messages.back(), "role") != role ||
        (role == "assistant" && last_group != group)) {
      messages.push_back(Value::object(
          {{"role", Value{std::string{role}}}, {"content", Value{Value::Array{}}}}));
    }
    for (auto &[key, value] : messages.back().object())
      if (key == "content") {
        value.array().push_back(std::move(block));
        break;
      }
    last_group = group;
  };
  std::set<std::string> seen, pending;
  bool results_started = false;
  for (const auto &item : field(request, "input").array()) {
    const auto type = optional_text(item, "type");
    const auto group = optional_text(item, "anthropic_message_id");
    if (const auto *native = item.find("anthropic_content")) {
      if (group.empty())
        throw Error{ErrorCode::conflict};
      const auto &kind = string_field(*native, "type");
      const auto role = optional_text(item, "role");
      if ((kind == "text" &&
           (role != "assistant" || (!type.empty() && type != "message"))) ||
          (kind == "tool_use" && (type != "function_call" || !role.empty())) ||
          ((kind == "thinking" || kind == "redacted_thinking") &&
           (type != "reasoning" || !role.empty())))
        throw Error{ErrorCode::conflict};
      if (kind == "text")
        if (const auto *parts =
                std::get_if<Value::Array>(&field(item, "content").value());
            parts && parts->size() != 1)
          throw Error{ErrorCode::conflict};
    }
    if (type == "function_call") {
      if (!pending.empty() && (results_started || last_group != group))
        throw Error{ErrorCode::conflict};
      const auto &id = string_field(item, "call_id");
      if (id.empty() || string_field(item, "name").empty() || !seen.insert(id).second)
        throw Error{ErrorCode::conflict};
      (void)field(item, "arguments").object();
      pending.insert(id);
      results_started = false;
      append("assistant",
             native_block({item, Value::object({{"type", Value{"tool_use"}},
                                                {"id", Value{id}},
                                                {"name", field(item, "name")},
                                                {"input", field(item, "arguments")}})}),
             group);
    } else if (type == "function_call_output") {
      if (pending.erase(string_field(item, "call_id")) != 1)
        throw Error{ErrorCode::conflict};
      if (item.find("blackbird_output_parts"))
        throw Error{ErrorCode::unsupported};
      const auto &output = field(item, "output");
      auto result =
          Value::object({{"type", Value{"tool_result"}},
                         {"tool_use_id", field(item, "call_id")},
                         {"content", std::holds_alternative<std::string>(output.value())
                                         ? output
                                         : Value{unwrap(dump_json(output))}}});
      if (const auto *error = output.find("error"); error && *error != Value{})
        result.object().emplace_back("is_error", Value{true});
      append("user", std::move(result), {});
      results_started = true;
    } else if (type == "reasoning") {
      if (!pending.empty() && (results_started || last_group != group))
        throw Error{ErrorCode::conflict};
      const auto *block = item.find("anthropic_content");
      if (!block || group.empty())
        throw Error{ErrorCode::unsupported};
      validate_thinking(*block);
      append("assistant", *block, group);
    } else if (type.empty() || type == "message") {
      const auto &role = string_field(item, "role");
      if (!pending.empty() &&
          (role != "assistant" || results_started || last_group != group))
        throw Error{ErrorCode::conflict};
      if (role != "assistant" && role != "user" && role != "developer" &&
          role != "system")
        throw Error{ErrorCode::unsupported};
      const auto &content = field(item, "content");
      if (const auto *text = std::get_if<std::string>(&content.value())) {
        const auto prefix = role == "developer" || role == "system"
                                ? "[Blackbird " + role + " guidance]\n"
                                : std::string{};
        append(role == "assistant" ? "assistant" : "user",
               native_block({item, text_block(prefix + *text)}), group);
      } else {
        for (const auto &part : content.array()) {
          const auto &kind = string_field(part, "type");
          if (kind != "input_text" && kind != "output_text" && kind != "refusal")
            throw Error{ErrorCode::unsupported};
          append(
              role == "assistant" ? "assistant" : "user",
              native_block({item, text_block(string_field(
                                      part, kind == "refusal" ? "refusal" : "text"))}),
              group);
        }
      }
    } else
      throw Error{ErrorCode::unsupported};
  }
  if (!pending.empty() || messages.empty() ||
      string_field(messages.front(), "role") != "user")
    throw Error{ErrorCode::conflict};
  std::string instructions = string_field(request, "instructions");
  if (oauth)
    instructions =
        "You are Claude Code, Anthropic's official CLI for Claude.\n" + instructions;
  auto result = Value::object(
      {{"model", Value{model}},
       {"system", Value{std::move(instructions)}},
       {"messages", Value{std::move(messages)}},
       {"tools", Value{std::move(tools)}},
       {"max_tokens", Value{Number{max_tokens}}},
       {"stream", Value{true}},
       {"tool_choice", Value::object({{"type", Value{"auto"}},
                                      {"disable_parallel_tool_use", Value{true}}})}});
  for (const auto key : {"temperature", "top_p"})
    if (const auto *value = request.find(key))
      put(result, key, *value);
  if (const auto *value = request.find("stop"))
    put(result, "stop_sequences", *value);
  if (const auto *parallel = request.find("parallel_tool_calls")) {
    const auto *enabled = std::get_if<bool>(&parallel->value());
    if (!enabled)
      throw Error{ErrorCode::invalid_range};
    put(result, "tool_choice",
        Value::object({{"type", Value{"auto"}},
                       {"disable_parallel_tool_use", Value{!*enabled}}}));
  }
  if (const auto *choice = request.find("tool_choice")) {
    if (*choice != Value{"auto"} && *choice != Value{"none"})
      throw Error{ErrorCode::unsupported};
    for (auto &[key, value] : result.object())
      if (key == "tool_choice")
        put(value, "type", *choice);
  }
  // Models before adaptive thinking require an explicit budget. Their old
  // family names are exact prefixes; unknown/new models use adaptive thinking.
  const bool manual =
      model.starts_with("claude-sonnet-4-5") || model.starts_with("claude-haiku-4-5") ||
      model.starts_with("claude-opus-4-5") ||
      model.starts_with("claude-sonnet-4-2025") ||
      model.starts_with("claude-opus-4-2025") || model.starts_with("claude-3-7-sonnet");
  if (manual) {
    const std::uint64_t desired = effort == "low"      ? 1024
                                  : effort == "medium" ? 4096
                                  : effort == "high"   ? 8192
                                  : effort == "xhigh"  ? 12288
                                                       : 16384;
    if (max_tokens <= 1024)
      throw Error{ErrorCode::invalid_range};
    put(result, "thinking",
        Value::object(
            {{"type", Value{"enabled"}},
             {"budget_tokens", Value{Number{std::min(desired, max_tokens - 1)}}}}));
    if (model.starts_with("claude-opus-4-5"))
      put(result, "output_config", Value::object({{"effort", Value{effort}}}));
  } else {
    put(result, "thinking", Value::object({{"type", Value{"adaptive"}}}));
    put(result, "output_config", Value::object({{"effort", Value{effort}}}));
  }
  return result;
}
Result<Value> anthropic_completed_response(std::string_view stream, ValueLimits limits,
                                           const RawAdapterContext *context) {
  try {
    RawProviderPhase phase{provider_link_credit};
    return phase.run([&]() -> Result<Value> {
      phase.start(6, context);
      if (context && context->receipt && context->receipt->response)
        phase.link(*context->receipt->response);
      if (stream.size() > limits.bytes)
        throw Error{ErrorCode::capacity};
      MessageStream message{limits};
      std::string data, name;
      while (!stream.empty()) {
        const auto end = stream.find('\n');
        if (end == std::string_view::npos)
          throw Error{ErrorCode::incomplete};
        auto line = stream.substr(0, end);
        stream.remove_prefix(end + 1);
        if (!line.empty() && line.back() == '\r')
          line.remove_suffix(1);
        if (line.empty()) {
          if (!data.empty()) {
            auto event = unwrap(parse_json(data, limits));
            if (!name.empty() && name != string_field(event, "type"))
              throw Error{ErrorCode::corrupt};
            message.event(event);
            data.clear();
          }
          name.clear();
        } else if (line.starts_with("data:")) {
          line.remove_prefix(5);
          if (line.starts_with(' '))
            line.remove_prefix(1);
          if (!data.empty())
            data += '\n';
          data.append(line);
        } else if (line.starts_with("event:")) {
          line.remove_prefix(6);
          if (line.starts_with(' '))
            line.remove_prefix(1);
          name = line;
        }
      }
      if (!data.empty() || !name.empty())
        throw Error{ErrorCode::incomplete};
      return Result<Value>::success(message.finish());
    });
  } catch (const Error &error) {
    return Result<Value>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<Value>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Result<Value>::failure({ErrorCode::capacity});
  } catch (const std::bad_variant_access &) {
    return Result<Value>::failure({ErrorCode::corrupt});
  }
}
AnthropicCodingProvider::AnthropicCodingProvider(AnthropicConfig config)
    : config_(std::move(config)) {}
void AnthropicCodingProvider::begin_request_group(const Value &request,
                                                  const RawAdapterContext *context) {
  if (group_ || responding_)
    throw Error{ErrorCode::busy};
  RawProviderPhase phase;
  phase.run([&] {
    phase.start(5, context);
    auto config = config_.auth;
    config.cancelled = cancelled;
    ProviderAuth auth{std::move(config)};
    const RawAuthParent parent{phase.reference()};
    binding_ = auth.binding("anthropic", &parent);
    if (string_field(binding_, "protocol") != "messages")
      throw Error{ErrorCode::unsupported};
    const auto &model = string_field(request, "model");
    if (model.empty() || model.size() > 256)
      throw Error{ErrorCode::invalid_range};
    group_ = true;
  });
}
void AnthropicCodingProvider::end_request_group() noexcept { group_ = false; }
Value AnthropicCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture) {
  return respond(request, capture, nullptr, nullptr);
}
Value AnthropicCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture,
    const RawProviderOwner *owner) {
  return respond(request, capture, owner, nullptr);
}
Value AnthropicCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture,
    const RawProviderOwner *owner, const RawAdapterContext *context) {
  if (responding_)
    throw Error{ErrorCode::busy};
  const bool temporary = !group_;
  if (temporary)
    begin_request_group(request, context);
  struct End {
    AnthropicCodingProvider *provider;
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
    const auto wire = export_anthropic_request(
        request, config_.max_tokens, string_field(binding_, "kind") == "oauth");
    const auto response =
        auth.request("anthropic", "messages", wire, config_.timeout_seconds, capture,
                     &binding_, owner, context);
    return unwrap(anthropic_completed_response(response.body, {}, context));
  });
}
} // namespace blackbird
