#include "blackbird/json.hpp"
#include "blackbird/openrouter.hpp"
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
    throw std::runtime_error("OpenRouter contract " + std::to_string(location.line()));
}
constexpr std::string_view completed =
    "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\","
    "\"model\":\"vendor/actual\",\"output\":[{\"type\":\"reasoning\",\"id\":\"r\","
    "\"encrypted_content\":\"opaque==\",\"signature\":\"signed\",\"format\":"
    "\"fixture\"}],"
    "\"usage\":{\"input_tokens\":100,\"output_tokens\":2,\"cost\":0.01}}}\n\n";
void streams() {
  const std::string stream = std::string{completed} +
                             "data: "
                             "{\"openrouter_metadata\":{\"requested\":\"vendor/"
                             "requested\",\"future\":true}}\n\n"
                             "data: [DONE]\n\n";
  auto result = openrouter_completed_response(stream);
  check(result.has_value());
  check(string_field(result.value(), "model") == "vendor/actual");
  check(field(field(result.value(), "openrouter_metadata"), "future") == Value{true});
  check(string_field(field(result.value(), "output").array()[0], "signature") ==
        "signed");
  ResponsePreview preview;
  for (const auto c : stream)
    check(preview.feed(std::string_view{&c, 1}).empty());
  for (const auto tail :
       {"data: {broken}\n\n", "data: {\"type\":\"response.completed\"}\n\n",
        "data: {\"type\":\"response.output_text.delta\"}\n\n",
        "data: [DONE]\n\ndata: {}\n\n", "data: {"})
    check(!openrouter_completed_response(std::string{completed} + tail).has_value());
  check(!openrouter_completed_response("data: [DONE]\n\n").has_value());
  check(!openrouter_completed_response(completed).has_value());
  const auto plain =
      openrouter_completed_response("data: "
                                    "{\"error_type\":\"permission_denied\",\"error\":{"
                                    "\"code\":\"server_error\"}}\n\n");
  check(!plain.has_value() && plain.error().code == ErrorCode::provider_auth);
  for (const auto type :
       {"response.failed", "response.error", "error", "response.incomplete"}) {
    const auto failed =
        "data: {\"type\":\"" + std::string{type} +
        "\",\"response\":{\"status\":\"failed\",\"error_type\":"
        "\"authentication\",\"error\":{\"code\":\"server_error\"}}}\n\n";
    const auto failure = openrouter_completed_response(failed);
    check(!failure.has_value());
    if (std::string_view{type} != "response.incomplete")
      check(failure.error().code == ErrorCode::provider_auth);
  }
}
void transport(const std::filesystem::path &directory) {
  OpenRouterConfig config;
  config.auth.directory = directory;
  unsigned requests = 0;
  config.auth.http = [&](const ProviderHttpRequest &wire,
                         ProviderHttpResponseLease &body) {
    ++requests;
    check(wire.url == "https://openrouter.ai/api/v1/responses");
    check(std::find(wire.headers.begin(), wire.headers.end(),
                    std::pair<std::string, std::string>{
                        "X-OpenRouter-Cache", "false"}) != wire.headers.end());
    check(std::find(wire.headers.begin(), wire.headers.end(),
                    std::pair<std::string, std::string>{
                        "X-OpenRouter-Metadata", "enabled"}) != wire.headers.end());
    const auto request = unwrap(parse_json(wire.body));
    check(field(field(request, "provider"), "require_parameters") == Value{true});
    check(string_field(request, "model") == "vendor/requested");
    check(string_field(field(request, "input").array()[0], "arguments") == "{\"x\":1}");
    const auto bytes = std::string{completed} + "data: [DONE]\n\n";
    auto extent = body.prepare(bytes.size());
    std::copy(bytes.begin(), bytes.end(), extent.begin());
    body.commit(bytes.size());
    return 200;
  };
  ProviderAuth auth{config.auth};
  auth.set_key("openrouter", "fixture", "fixture-secret");
  auto request = Value::object(
      {{"model", Value{"vendor/requested"}},
       {"store", Value{false}},
       {"stream", Value{true}},
       {"input", Value{Value::Array{Value::object(
                     {{"type", Value{"function_call"}},
                      {"call_id", Value{"c"}},
                      {"arguments", Value::object({{"x", Value{Number{1}}}})}})}}}});
  const auto original = Value{request};
  OpenRouterCodingProvider provider{config};
  provider.begin_request_group(request, nullptr);
  check(provider.supports_prompt_cache_policy());
  auto response = provider.respond(request, {});
  check(string_field(response, "model") == "vendor/actual");
  check(request == original && requests == 1);
  auth.set_key("openrouter", "other", "different-secret");
  bool conflict = false;
  try {
    (void)provider.respond(request, {});
  } catch (const Error &error) {
    conflict = error.code == ErrorCode::conflict;
  }
  check(conflict && requests == 1);
  provider.end_request_group();
  check(!provider.supports_prompt_cache_policy());
}
} // namespace
int main() {
  char pattern[] = "/tmp/blackbird-openrouter-XXXXXX";
  const auto *path = ::mkdtemp(pattern);
  if (!path)
    return 2;
  try {
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
