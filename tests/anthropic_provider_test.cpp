#include "blackbird/anthropic_provider.hpp"
#include "blackbird/json.hpp"
#include <algorithm>
#include <fcntl.h>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
void check(bool value, std::source_location at = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("Anthropic contract " + std::to_string(at.line()));
}
template <class F> void rejects(F action, ErrorCode code) {
  try {
    action();
  } catch (const Error &error) {
    check(error.code == code);
    return;
  }
  check(false);
}
Value json(std::string_view text) { return unwrap(parse_json(text)); }
std::string event(const Value &packet) {
  return "data: " + unwrap(dump_json(packet)) + "\n\n";
}
Value request() {
  return json(R"({"model":"claude-sonnet-4-6","instructions":"Blackbird instructions",
    "stream":true,"store":false,"reasoning":{"effort":"medium"},
    "input":[{"role":"user","content":"Perform the tool"}],
    "tools":[{"type":"function","name":"read_file","description":"Read file",
      "parameters":{"type":"object","properties":{"path":{"type":"string"}},"required":[]}}]})");
}
Value::Array blocks() {
  return json(
             R"([{"type":"thinking","thinking":"Opaque summary λ\n","signature":"sig+/=\n"},
    {"type":"redacted_thinking","data":"redacted+/="},
    {"type":"text","text":"Read now"},
    {"type":"tool_use","id":"call1","name":"read_file","input":{"path":"README.md"}},
    {"type":"tool_use","id":"call2","name":"read_file","input":{"path":"AGENTS.md"}}])")
      .array();
}
std::string stream(const Value::Array &content, std::string reason = "tool_use") {
  std::string out =
      event(json(R"({"type":"message_start","message":{"id":"msg1","type":"message",
    "role":"assistant","model":"claude-sonnet-4-6","content":[],"usage":{"input_tokens":9,
    "cache_read_input_tokens":7,"cache_creation_input_tokens":3,"output_tokens":1}}})"));
  std::size_t index = 0;
  for (const auto &block : content) {
    out += event(Value::object({{"type", Value{"content_block_start"}},
                                {"index", Value{Number{index}}},
                                {"content_block", block}}));
    out += event(Value::object(
        {{"type", Value{"content_block_stop"}}, {"index", Value{Number{index++}}}}));
  }
  out += event(Value::object(
      {{"type", Value{"message_delta"}},
       {"delta", Value::object({{"stop_reason", Value{std::move(reason)}}})},
       {"usage", Value::object({{"output_tokens", Value{Number{5}}}})}}));
  out += event(json(R"({"type":"message_stop"})"));
  return out;
}
void roundtrip() {
  const auto original = blocks();
  auto response = unwrap(anthropic_completed_response(stream(original)));
  const auto &output = field(response, "output").array();
  check(output.size() == original.size());
  check(field(output[0], "anthropic_content") == original[0]);
  check(!output[0].find("encrypted_content"));
  check(field(output[1], "anthropic_content") == original[1]);
  check(field(field(response, "usage"), "input_tokens") == Value{Number{19}});
  check(field(field(response, "usage"), "output_tokens") == Value{Number{5}});
  check(field(field(response, "usage"), "total_tokens") == Value{Number{24}});
  auto next = request();
  for (auto &[key, value] : next.object())
    if (key == "input") {
      value.array().insert(value.array().end(), output.begin(), output.end());
      value.array().push_back(json(
          R"({"type":"function_call_output","call_id":"call1","output":{"content":"first"}})"));
      value.array().push_back(json(
          R"({"type":"function_call_output","call_id":"call2","output":{"error":"missing"}})"));
      value.array().push_back(
          json(R"({"role":"developer","content":"state at this position"})"));
    }
  auto wire = export_anthropic_request(next);
  const auto &messages = field(wire, "messages").array();
  check(messages.size() == 3);
  check(field(messages[1], "content") == Value{original});
  const auto &results = field(messages[2], "content").array();
  check(results.size() == 3);
  check(string_field(results[0], "tool_use_id") == "call1");
  check(string_field(results[0], "content") == "{\"content\":\"first\"}");
  check(field(results[1], "is_error") == Value{true});
  check(string_field(results[2], "text").starts_with("[Blackbird developer guidance]"));
  check(field(field(wire, "tools").array()[0], "input_schema") ==
        field(field(next, "tools").array()[0], "parameters"));
  check(!field(wire, "tools").array()[0].find("strict"));
  rejects([&] { (void)export_provider_request(next); }, ErrorCode::unsupported);
  auto foreign = request();
  for (auto &[key, value] : foreign.object())
    if (key == "input")
      value.array().push_back(
          json(R"({"type":"reasoning","encrypted_content":"foreign","summary":[]})"));
  rejects([&] { (void)export_anthropic_request(foreign); }, ErrorCode::unsupported);
  auto incomplete = request();
  for (auto &[key, value] : incomplete.object())
    if (key == "input")
      value.array().push_back(output.back());
  rejects([&] { (void)export_anthropic_request(incomplete); }, ErrorCode::conflict);
  check(
      string_field(export_anthropic_request(request(), 16384, true), "system")
          .starts_with("You are Claude Code, Anthropic's official CLI for Claude.\n"));
}
void malformed() {
  const auto valid = stream(blocks());
  check(anthropic_completed_response(valid).has_value());
  for (const auto suffix :
       {"data: {broken}\n\n", "data: {", "data: {\"type\":\"message_stop\"}\n\n",
        "data: {\"type\":\"error\"}\n\n"})
    check(!anthropic_completed_response(valid + suffix).has_value());
  check(!anthropic_completed_response(valid.substr(0, valid.size() - 1)).has_value());
  check(!anthropic_completed_response(stream(blocks(), "max_tokens")).has_value());
  check(!anthropic_completed_response(stream(blocks(), "end_turn")).has_value());
  auto duplicate = blocks();
  duplicate.push_back(duplicate.back());
  check(!anthropic_completed_response(stream(duplicate)).has_value());
  auto unsigned_thinking = blocks();
  unsigned_thinking[0] =
      json(R"({"type":"thinking","thinking":"summary","signature":""})");
  check(!anthropic_completed_response(stream(unsigned_thinking)).has_value());
  check(!anthropic_completed_response("event: message_stop\n" + valid).has_value());
  check(!anthropic_completed_response(valid, {.bytes = 10}).has_value());
}
void deltas_and_preview() {
  const auto streamed =
      event(json(
          R"({"type":"message_start","message":{"type":"message","id":"delta","role":"assistant","model":"fixture","content":[]}})")) +
      event(json(
          R"({"type":"content_block_start","index":0,"content_block":{"type":"thinking","thinking":"","signature":""}})")) +
      event(json(
          R"({"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"summary"}})")) +
      event(json(
          R"({"type":"content_block_delta","index":0,"delta":{"type":"signature_delta","signature":"opaque"}})")) +
      event(json(R"({"type":"content_block_stop","index":0})")) +
      event(json(
          R"({"type":"content_block_start","index":1,"content_block":{"type":"text","text":""}})")) +
      event(json(
          R"({"type":"content_block_delta","index":1,"delta":{"type":"text_delta","text":"hello λ"}})")) +
      event(json(R"({"type":"content_block_stop","index":1})")) +
      event(json(
          R"({"type":"content_block_start","index":2,"content_block":{"type":"tool_use","id":"c1","name":"read_file","input":{}}})")) +
      event(json(
          R"({"type":"content_block_delta","index":2,"delta":{"type":"input_json_delta","partial_json":"{\"path\":"}})")) +
      event(json(
          R"({"type":"content_block_delta","index":2,"delta":{"type":"input_json_delta","partial_json":"\"README.md\"}"}})")) +
      event(json(R"({"type":"content_block_stop","index":2})")) +
      event(json(R"({"type":"message_delta","delta":{"stop_reason":"tool_use"}})")) +
      event(json(R"({"type":"message_stop"})"));
  const auto response = unwrap(anthropic_completed_response(streamed));
  check(field(response, "usage").object().empty());
  check(string_field(field(field(response, "output").array()[0], "anthropic_content"),
                     "signature") == "opaque");
  check(field(field(response, "output").array()[2], "arguments") ==
        json(R"({"path":"README.md"})"));
  for (std::size_t chunk = 1; chunk <= 19; ++chunk) {
    ResponsePreview preview;
    std::string shown;
    for (std::size_t at = 0; at < streamed.size(); at += chunk)
      for (const auto &text :
           preview.feed(std::string_view{streamed}.substr(at, chunk))) {
        check(text.item_id == "delta:1");
        shown += text.text;
      }
    check(shown == "hello λ");
  }
}
void transport(const std::filesystem::path &directory) {
  AnthropicConfig config;
  config.auth.directory = directory;
  ProviderAuth auth{config.auth};
  auth.set_key("anthropic", "first", "fixture-secret");
  unsigned calls = 0;
  const auto bytes = stream(blocks());
  config.auth.http = [&](const ProviderHttpRequest &wire,
                         ProviderHttpResponseLease &body) {
    ++calls;
    check(wire.url == "https://api.anthropic.com/v1/messages");
    check(string_field(unwrap(parse_json(wire.body)), "model") == "claude-sonnet-4-6");
    check(std::find(wire.headers.begin(), wire.headers.end(),
                    std::pair{std::string{"x-api-key"},
                              std::string{"fixture-secret"}}) != wire.headers.end());
    auto extent = body.prepare(bytes.size());
    std::copy(bytes.begin(), bytes.end(), extent.begin());
    body.commit(bytes.size());
    return 200;
  };
  AnthropicCodingProvider provider{config};
  check(provider.retain_request_state());
  provider.begin_request_group(request(), nullptr);
  check(!provider.supports_prompt_cache_policy());
  check(field(provider.respond(request(), {}), "output").array().size() == 5);
  check(calls == 1);
  auth.set_key("anthropic", "second", "other-secret");
  rejects([&] { (void)provider.respond(request(), {}); }, ErrorCode::conflict);
  check(calls == 1);
  provider.end_request_group();
}
void mutation() {
  auto content = blocks();
  content.resize(3);
  const auto response =
      unwrap(anthropic_completed_response(stream(content, "end_turn")));
  for (unsigned mode = 0; mode < 6; ++mode) {
    auto changed = request();
    for (auto &[key, input] : changed.object())
      if (key == "input") {
        const auto &output = field(response, "output").array();
        input.array().insert(input.array().end(), output.begin(), output.end());
        auto &text = input.array().back();
        for (auto &[name, value] : text.object()) {
          if (name == "content" && mode == 0)
            value = Value{"changed text"};
          if (name == "role" && mode == 1)
            value = Value{"user"};
          if (name == "content" && mode == 2)
            value = Value{Value::Array{}};
          if (name == "anthropic_message_id" && mode == 3)
            value = Value{""};
          if (name == "content" && mode == 4)
            value.array().push_back(value.array().front());
          if (name == "content" && mode == 5)
            value = Value{"Read now"};
        }
        input.array().push_back(json(R"({"role":"user","content":"confirm"})"));
      }
    if (mode == 5) {
      const auto wire = export_anthropic_request(changed);
      check(field(field(wire, "messages").array()[1], "content") == Value{content});
    } else
      rejects([&] { (void)export_anthropic_request(changed); }, ErrorCode::conflict);
  }
}
void binding_cancel(const std::filesystem::path &directory) {
  AnthropicConfig config;
  config.auth.directory = directory;
  ProviderAuth auth{config.auth};
  auth.set_key("anthropic", "fixture", "fixture-secret");
  const int fd = ::open((directory / "lock").c_str(), O_RDWR | O_CLOEXEC);
  check(fd >= 0);
  struct Close {
    int fd;
    ~Close() { ::close(fd); }
  } close{fd};
  check(::flock(fd, LOCK_EX | LOCK_NB) == 0);
  std::jthread release{[fd] {
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    (void)::flock(fd, LOCK_UN);
  }};
  AnthropicCodingProvider provider{config};
  provider.cancelled = [] { return true; };
  rejects([&] { provider.begin_request_group(request(), nullptr); },
          ErrorCode::interrupted);
}
void effort_pairs() {
  auto selected = [](const char *model, const char *effort) {
    auto value = request();
    for (auto &[key, field] : value.object()) {
      if (key == "model")
        field = Value{model};
      if (key == "reasoning")
        field = Value::object({{"effort", Value{effort}}});
    }
    return value;
  };
  for (const auto &[model, effort] :
       {std::pair{"claude-sonnet-4-6", "xhigh"}, std::pair{"claude-opus-4-6", "xhigh"},
        std::pair{"claude-opus-4-5", "xhigh"}, std::pair{"claude-opus-4-5", "max"}})
    rejects([&] { (void)export_anthropic_request(selected(model, effort)); },
            ErrorCode::unsupported);
  for (const auto &[model, effort] :
       {std::pair{"claude-sonnet-4-6", "max"}, std::pair{"claude-opus-4-6", "max"},
        std::pair{"claude-opus-4-7", "xhigh"}, std::pair{"claude-opus-4-5", "high"}})
    check(string_field(
              field(export_anthropic_request(selected(model, effort)), "output_config"),
              "effort") == effort);
}
} // namespace
int main(int argc, char **argv) {
  char pattern[] = "/tmp/blackbird-anthropic-XXXXXX";
  const auto *path = ::mkdtemp(pattern);
  if (!path)
    return 2;
  try {
    const auto mode = argc > 1 ? std::string_view{argv[1]} : std::string_view{};
    if (mode == "mutation" || mode == "cancellation" || mode == "effort") {
      if (mode == "mutation")
        mutation();
      else if (mode == "effort")
        effort_pairs();
      else
        binding_cancel(std::filesystem::path{path} / "cancel-auth");
      std::filesystem::remove_all(path);
      return 0;
    }
    roundtrip();
    malformed();
    deltas_and_preview();
    transport(std::filesystem::path{path} / "auth");
    mutation();
    binding_cancel(std::filesystem::path{path} / "cancel-auth");
    effort_pairs();
    std::filesystem::remove_all(path);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    std::filesystem::remove_all(path);
    return 1;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
}
