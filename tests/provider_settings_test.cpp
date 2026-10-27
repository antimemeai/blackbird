#include "blackbird/anthropic_provider.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/provider_settings.hpp"
#include <algorithm>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace blackbird;
namespace {
void check(bool value,
           std::source_location location = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("provider settings contract " +
                             std::to_string(location.line()));
}
bool contains(const std::optional<std::string> &text, std::string_view part) {
  return text && text->find(part) != std::string::npos;
}
// Expected accepted efforts, written from the vendor documents independently of the
// table (papers/provider-settings-2026-10-18/). A model of "" means any model that
// no per-model rule names.
struct Expected {
  std::string_view base_url, model, efforts;
};
constexpr std::array expected{
    Expected{"https://api.openai.com/v1", "gpt-6-luna",
             "none minimal low medium high xhigh max"},
    Expected{"https://api.openai.com/v1", "gpt-6-astra",
             "minimal low medium high xhigh max"},
    Expected{"https://api.openai.com/v1", "gpt-6.1-sol", "low medium high xhigh max"},
    Expected{"https://chatgpt.com/backend-api/codex", "gpt-6.1-sol",
             "low medium high xhigh max"},
    Expected{"https://api.anthropic.com/v1", "claude-opus-5-5",
             "low medium high xhigh max"},
    Expected{"https://api.anthropic.com/v1", "claude-opus-4-6", "low medium high max"},
    Expected{"https://api.anthropic.com/v1", "claude-sonnet-4-6",
             "low medium high max"},
    Expected{"https://api.anthropic.com/v1", "claude-mythos-preview",
             "low medium high max"},
    Expected{"https://api.anthropic.com/v1", "claude-opus-4-5-20251101",
             "low medium high"},
    Expected{"https://openrouter.ai/api/v1", "openai/gpt-6-luna",
             "none minimal low medium high xhigh max"},
    Expected{"https://api.paperinstruments.com/v1", "ultramarine-1-flash",
             "low high max"},
    Expected{"https://api.xiaomimimo.com/v1", "mimo-v2.6-pro-ultraspeed",
             "none minimal low medium high xhigh max ultra"},
    Expected{"https://token-plan-cn.xiaomimimo.com/v1", "mimo-v2.6-pro",
             "none minimal low medium high xhigh max ultra"},
    Expected{"https://token-plan-sgp.xiaomimimo.com/v1", "mimo-v2.6-flash",
             "none minimal low medium high xhigh max ultra"},
    Expected{"https://token-plan-ams.xiaomimimo.com/v1", "mimo-v2.5",
             "none minimal low medium high xhigh max ultra"}};
bool listed(std::string_view efforts, std::string_view name) {
  std::size_t start = 0;
  while (start <= efforts.size()) {
    const auto end = std::min(efforts.find(' ', start), efforts.size());
    if (efforts.substr(start, end - start) == name)
      return true;
    start = end + 1;
  }
  return false;
}
void efforts() {
  for (const auto effort : all_efforts)
    check(parse_effort(effort_name(effort)) == effort);
  for (const auto text : {"", "LOW", "extreme", "persistent", "high "})
    check(!parse_effort(text));
  for (const auto &endpoint : provider_settings()) {
    check(!endpoint.sources.empty() && !endpoint.efforts.empty());
    for (const auto &source : endpoint.sources)
      check(source.url.starts_with("https://") && source.read == "2026-10-18");
    check(!endpoint.base_url.ends_with('/'));
    check(endpoint_settings(endpoint.protocol, endpoint.base_url) == &endpoint);
    // Every documented endpoint has an independent expectation.
    check(std::ranges::any_of(
        expected, [&](const Expected &e) { return e.base_url == endpoint.base_url; }));
  }
  for (const auto &want : expected) {
    const auto *endpoint = [&]() -> const EndpointSettings * {
      for (const auto &candidate : provider_settings())
        if (candidate.base_url == want.base_url)
          return &candidate;
      return nullptr;
    }();
    check(endpoint != nullptr);
    for (const auto effort : all_efforts) {
      const auto refusal = refuse_launch(
          {endpoint->protocol, endpoint->base_url, want.model, effort_name(effort)});
      if (listed(want.efforts, effort_name(effort))) {
        check(!refusal);
        continue;
      }
      // The refusal names the endpoint, the exact accepted list and the document.
      std::string accepted;
      for (const auto other : all_efforts)
        if (listed(want.efforts, effort_name(other)))
          accepted += (accepted.empty() ? "" : ", ") + std::string{effort_name(other)};
      check(contains(refusal, "Effort '" + std::string{effort_name(effort)} + "'"));
      check(contains(refusal, endpoint->name));
      check(contains(refusal, "Accepted: " + accepted + "."));
      check(contains(refusal, endpoint->sources.front().url));
    }
    check(contains(
        refuse_launch({endpoint->protocol, endpoint->base_url, want.model, "extreme"}),
        "Effort 'extreme'"));
  }
  // MiMo's refusal says which documented levels are the same behavior.
  check(contains(refuse_launch({"responses", "https://api.xiaomimimo.com/v1",
                                "mimo-v2.6-pro", "persistent"}),
                 "every other level as the same thinking on"));
}
void models() {
  const auto refusal =
      refuse_launch({"responses", "https://token-plan-sgp.xiaomimimo.com/v1",
                     "mimo-v2.6-pro-ultraspeed", "high"});
  check(contains(refusal, "Model 'mimo-v2.6-pro-ultraspeed' is not available on MiMo "
                          "Token Plan (Singapore)"));
  check(contains(refusal, "Not supported model mimo-v2.6-pro-ultraspeed"));
  check(contains(refusal, "Documented models: mimo-v2.6-pro, mimo-v2.6-flash, "
                          "mimo-v2.5-pro, mimo-v2.5."));
  check(!refuse_launch({"responses", "https://api.xiaomimimo.com/v1",
                        "mimo-v2.6-pro-ultraspeed", "high"}));
  check(contains(
      refuse_launch({"responses-portable", "https://api.paperinstruments.com/v1",
                     "ultramarine-2", "max"}),
      "Documented models: ultramarine-1-flash."));
  // Open model lists accept any model name.
  check(!refuse_launch({"responses", "https://api.openai.com/v1", "gpt-7", "medium"}));
}
void fallback() {
  // An endpoint the table does not describe keeps the wire-protocol rule, and a
  // documented URL under another protocol is not that documented endpoint.
  for (const auto *url :
       {"https://example.com/v1", "https://api.paperinstruments.com/v1"})
    for (const auto effort : all_efforts) {
      const auto name = effort_name(effort);
      const bool responses = listed("low medium high xhigh max", name);
      check(!refuse_launch({"responses", url, "any", name}) == responses);
    }
  for (const auto effort : all_efforts) {
    const auto name = effort_name(effort);
    const bool portable = listed("low high max", name);
    const auto refusal =
        refuse_launch({"responses-portable", "https://example.com/v1", "any", name});
    check(!refusal == portable);
    if (refusal)
      check(contains(refusal, "wire rule accepts: low, high, max."));
  }
  check(refuse_launch({"chat", "https://example.com/v1", "any", "high"}).has_value());
}
void anthropic_route() {
  // The adapter's own check reads the same table.
  validate_anthropic_route("claude-opus-4-6", "max");
  for (const auto effort : {"xhigh", "none", "ultra"})
    try {
      validate_anthropic_route("claude-opus-4-6", effort);
      check(false);
    } catch (const Error &error) {
      check(error.code == ErrorCode::unsupported);
    }
}
void http_errors() {
  check(provider_http_status({ErrorCode::external_unknown, 400}) == 400);
  check(provider_http_status({ErrorCode::external_unknown, 503}) == 503);
  check(provider_http_status({ErrorCode::provider_auth, 401}) == 401);
  check(provider_http_status({ErrorCode::provider_rate_limit, 429}) == 429);
  for (const Error error :
       {Error{ErrorCode::external_unknown, 0}, Error{ErrorCode::external_unknown, 200},
        Error{ErrorCode::provider_auth, -400},
        Error{ErrorCode::provider_rate_limit, -429},
        Error{ErrorCode::provider_transport, 28}})
    check(!provider_http_status(error));
  // MiMo's retained 2026-10-17 body as curl delivered it, with the status trailer.
  constexpr std::string_view mimo = "{\n    \"error\": {\n        \"code\": \"400\",\n"
                                    "        \"message\": \"Not supported model "
                                    "mimo-v2.6-pro-ultraspeed\"\n    }\n}\n400";
  check(provider_error_message(mimo, 400) ==
        "Not supported model mimo-v2.6-pro-ultraspeed");
  // A trailer for another status is not Blackbird's: the bytes are not JSON.
  check(!provider_error_message(mimo, 401));
  check(
      provider_error_message(
          R"({"error":{"type":"invalid_request_error","code":"unsupported_parameter",)"
          R"("message":"Background execution is not supported.","param":"background"}})",
          400) == "Background execution is not supported. (type: "
                  "invalid_request_error, code: unsupported_parameter, param: "
                  "background)");
  check(provider_error_message(R"({"type":"error","error":{"type":"not_found_error",)"
                               R"("message":"Not found."},"request_id":"req_1"})",
                               404) == "Not found. (type: not_found_error)");
  check(provider_error_message(R"({"error":{"code":402,"message":"Out of credits"}})",
                               402) == "Out of credits");
  check(provider_error_message(R"({"detail":"Unsupported model"})", 400) ==
        "Unsupported model");
  check(provider_error_message(R"({"error":"bad key"})", 401) == "bad key");
  check(provider_error_message("{\"message\":\"two\\nlines\\u001b[31m\"}", 400) ==
        "two lines [31m");
  for (const auto body : {"<html>Bad Gateway</html>", "", "[]", R"({"error":{}})",
                          R"({"detail":[{"msg":"x"}]})"})
    check(!provider_error_message(body, 502));
  check(!provider_error_message(R"({"error":"x"})", 99));
  // Long messages end on a whole UTF-8 scalar.
  std::string long_text;
  while (long_text.size() < 1500)
    long_text += "\xC3\xA9";
  const auto bounded =
      provider_error_message(R"({"error":{"message":")" + long_text + "\"}}", 400);
  check(bounded.has_value());
  if (bounded) {
    check(bounded->ends_with("...") && bounded->size() <= 1027);
    check((bounded->size() - 3) % 2 == 0);
  }
}
} // namespace
int main() {
  try {
    efforts();
    models();
    fallback();
    anthropic_route();
    http_errors();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
    return 1;
  }
}
