#include "blackbird/json.hpp"
#include "blackbird/responses_provider.hpp"
#include <algorithm>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <unistd.h>
using namespace blackbird;
namespace {
void check(bool value,
           std::source_location location = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("Responses contract " + std::to_string(location.line()));
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
Value request() {
  return unwrap(
      parse_json(R"({"model":"ultramarine-1-flash","stream":true,"store":false,
    "reasoning":{"effort":"max"},"include":["reasoning.encrypted_content"],
    "tools":[{"type":"function","name":"optional","parameters":{"type":"object",
      "properties":{"path":{"type":"string"}},"required":[]}},
      {"type":"function","name":"explicit","strict":true,"parameters":{}}],
    "input":[{"role":"developer","content":"state"},
      {"type":"reasoning","id":"r1","summary":[{"type":"summary_text","text":"reason"}]},
      {"type":"function_call","call_id":"c1","name":"read_file","arguments":{"path":"README.md"}},
      {"type":"function_call_output","call_id":"c1","output":{"content":"file"}}]})"));
}
void exports() {
  const auto native = request();
  auto wire = export_registered_responses_request(native, true);
  check(field(wire, "include").array().empty());
  check(field(field(wire, "tools").array()[0], "strict") == Value{false});
  check(field(field(wire, "tools").array()[1], "strict") == Value{true});
  check(field(field(wire, "tools").array()[0], "parameters") ==
        field(field(native, "tools").array()[0], "parameters"));
  const auto &input = field(wire, "input").array();
  check(string_field(input[0], "role") == "system");
  check(input[1] == field(native, "input").array()[1]);
  check(string_field(input[2], "arguments") == "{\"path\":\"README.md\"}");
  check(string_field(input[3], "output") == "{\"content\":\"file\"}");
  check(field(native, "include").array().size() == 1);
  check(string_field(field(native, "input").array()[0], "role") == "developer");
  wire = export_registered_responses_request(native, false);
  check(field(wire, "include") == field(native, "include"));
  check(!field(wire, "tools").array()[0].find("strict"));
  check(string_field(field(wire, "input").array()[0], "role") == "developer");
  for (const auto effort : {"medium", "xhigh", "none"}) {
    auto changed = request();
    for (auto &[key, value] : changed.object())
      if (key == "reasoning")
        value = Value::object({{"effort", Value{effort}}});
    rejects([&] { (void)export_registered_responses_request(changed, true); },
            ErrorCode::unsupported);
  }
  auto changed = request();
  for (auto &[key, value] : changed.object())
    if (key == "input")
      value.array()[1].object().emplace_back("encrypted_content", Value{"opaque"});
  rejects([&] { (void)export_registered_responses_request(changed, true); },
          ErrorCode::unsupported);
  changed = request();
  for (auto &[key, value] : changed.object())
    if (key == "store")
      value = Value{true};
  rejects([&] { (void)export_registered_responses_request(changed, true); },
          ErrorCode::unsupported);
}
constexpr std::string_view completed =
    "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\","
    "\"model\":\"ultramarine-1-flash\",\"output\":[{\"type\":\"function_call\","
    "\"status\":\"completed\",\"call_id\":\"c1\",\"name\":\"read_file\","
    "\"arguments\":\"{\\\"path\\\":\\\"README.md\\\"}\"}]}}\n\n";
void streams() {
  check(registered_responses_completed_response(completed).has_value());
  for (const auto tail :
       {"data: {broken}\n\n", "data: {", "data: [DONE]\n\ndata: {}\n\n"})
    check(!registered_responses_completed_response(std::string{completed} + tail)
               .has_value());
  for (const auto type : {"response.failed", "response.incomplete", "error"})
    check(!registered_responses_completed_response("data: {\"type\":\"" +
                                                   std::string{type} + "\"}\n\n")
               .has_value());
  for (const auto status : {"in_progress", "incomplete"}) {
    std::string stream{completed};
    const auto start = stream.find("\"status\":\"completed\",\"call_id\"");
    check(start != std::string::npos);
    stream.replace(start, 20, "\"status\":\"" + std::string{status} + "\"");
    check(!registered_responses_completed_response(stream).has_value());
  }
  const auto contradiction =
      "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\","
      "\"error\":{\"code\":\"bad\"},\"output\":[]}}\n\n";
  check(!registered_responses_completed_response(contradiction).has_value());
}
void transport(const std::filesystem::path &directory) {
  RegisteredResponsesConfig config;
  config.provider = "paper";
  config.auth.directory = directory;
  ProviderAuth auth{config.auth};
  auth.add_provider("paper", "https://api.paperinstruments.com/v1",
                    "responses-portable", "PAPER_TEST_KEY");
  auth.set_key("paper", "fixture", "fixture-secret");
  unsigned calls = 0;
  config.auth.http = [&](const ProviderHttpRequest &wire,
                         ProviderHttpResponseLease &body) {
    ++calls;
    check(wire.url == "https://api.paperinstruments.com/v1/responses");
    const auto sent = unwrap(parse_json(wire.body));
    check(string_field(field(sent, "input").array()[0], "role") == "system");
    check(field(sent, "include").array().empty());
    auto extent = body.prepare(completed.size());
    std::copy(completed.begin(), completed.end(), extent.begin());
    body.commit(completed.size());
    return 200;
  };
  RegisteredResponsesCodingProvider provider{config};
  check(provider.provider_identity() == "paper");
  provider.begin_request_group(request(), nullptr);
  check(!provider.supports_prompt_cache_policy());
  check(string_field(provider.respond(request(), {}), "model") ==
        "ultramarine-1-flash");
  check(calls == 1);
  auth.set_key("paper", "other", "other-secret");
  rejects([&] { (void)provider.respond(request(), {}); }, ErrorCode::conflict);
  check(calls == 1);
  provider.end_request_group();
  auth.add_provider("chat-only", "https://example.com/v1", "chat", "CHAT_KEY");
  config.provider = "chat-only";
  RegisteredResponsesCodingProvider chat{config};
  rejects([&] { chat.begin_request_group(request(), nullptr); },
          ErrorCode::unsupported);
}
} // namespace
int main() {
  char pattern[] = "/tmp/blackbird-responses-XXXXXX";
  const auto *path = ::mkdtemp(pattern);
  if (!path)
    return 2;
  try {
    exports();
    streams();
    transport(std::filesystem::path{path} / "auth");
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
