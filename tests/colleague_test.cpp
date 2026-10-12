#include "blackbird/colleague.hpp"
#include "blackbird/json.hpp"
#include "blackbird/tools.hpp"
#include <iostream>
#include <stdexcept>
using namespace blackbird;
void check(bool condition, const char *message = "check failed") {
  if (!condition)
    throw std::runtime_error{message};
}
Value request(const char *provider = "claude") {
  auto r = unwrap(parse_json(
      R"({"request_id":"r1","from":"arco","to":"diagnostician","provider":"claude","model":"sonnet","task":"Diagnose the selected code","context":[{"id":"source:1","text":"selected bytes"}],"profile":{"name":"context-only","provenance":"operator/local","timeout_seconds":2,"tools":"none","requests":1}})"));
  for (auto &[key, value] : r.object())
    if (key == "provider")
      value = Value{provider};
  return r;
}
Value cli_result(const Value &upstream, int exit_code = 0) {
  return Value::object({{"exit_code", Value{Number{exit_code}}},
                        {"output", Value{unwrap(dump_json(upstream))}}});
}
void reported_failure_cases(const ColleagueCapture &capture) {
  const auto usage = Value::object({{"input_tokens", Value{Number{17}}}});
  const auto models = Value::object({{"claude-observed", Value::object({})}});
  const auto errors = Value{Value::Array{Value{"maximum turns exceeded"}}};
  const auto failure = Value::object({{"type", Value{"result"}},
                                      {"subtype", Value{"error_max_turns"}},
                                      {"is_error", Value{true}},
                                      {"errors", errors},
                                      {"usage", usage},
                                      {"modelUsage", models}});
  int dispatches = 0;
  const auto reply = call_colleague(
      request(), capture,
      [&](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
        ++dispatches;
        return cli_result(failure);
      });
  check(dispatches == 1 && string_field(reply, "status") == "failed" &&
            string_field(reply, "remote_disposition") == "reported_failure",
        "reported CLI error without result must remain a single known failure");
  check(string_field(reply, "error") == "cli_reported_failure" &&
            field(field(reply, "reported_error"), "errors") == errors,
        "error-result diagnostics must survive the missing success result field");
  check(field(reply, "usage") == usage && field(reply, "model_usage") == models &&
            string_field(reply, "actual_model") == "claude-observed",
        "reported failure must preserve observed usage and model metadata");

  const auto no_diagnostic = call_colleague(
      request(), capture,
      [](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
        return cli_result(
            Value::object({{"type", Value{"result"}}, {"is_error", Value{true}}}));
      });
  check(string_field(no_diagnostic, "status") == "failed" &&
            string_field(no_diagnostic, "error") == "cli_reported_failure",
        "missing diagnostics must not erase an explicit error observation");

  for (const auto &flag : {Value{}, Value{Number{1}}, Value{"true"}}) {
    const auto malformed = call_colleague(
        request(), capture,
        [&](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
          return cli_result(Value::object({{"type", Value{"result"}},
                                           {"is_error", flag},
                                           {"result", Value{"not an error flag"}}}));
        });
    check(string_field(malformed, "status") == "unknown" &&
              string_field(malformed, "remote_disposition") == "unknown",
          "nonboolean is_error cannot establish reported failure");
  }

  const auto multiple = Value::object(
      {{"claude-primary", Value::object({})}, {"claude-secondary", Value::object({})}});
  const auto multi_reply = call_colleague(
      request(), capture,
      [&](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
        auto upstream = failure;
        for (auto &[name, value] : upstream.object())
          if (name == "modelUsage")
            value = multiple;
        return cli_result(upstream);
      });
  check(string_field(multi_reply, "status") == "failed" &&
            field(multi_reply, "model_usage") == multiple &&
            field(multi_reply, "actual_model") == Value{},
        "multiple observed models cannot become the requested alias on failure");

  const auto nonzero =
      call_colleague(request(), capture,
                     [&](const Value &, const ColleagueCapture &,
                         const RawAdapterContext *) { return cli_result(failure, 1); });
  check(string_field(nonzero, "remote_disposition") == "unknown" &&
            field(nonzero, "usage") == usage &&
            string_field(nonzero, "actual_model") == "claude-observed",
        "nonzero CLI exit preserves uncertainty and available model metadata");

  const auto incomplete = call_colleague(
      request("openai"), capture,
      [&](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
        return Value::object({{"status", Value{"incomplete"}},
                              {"model", Value{"gpt-observed"}},
                              {"usage", usage}});
      });
  check(string_field(incomplete, "status") == "unknown" &&
            field(incomplete, "usage") == usage &&
            string_field(incomplete, "actual_model") == "gpt-observed",
        "incomplete received native response must preserve model and usage");

  const auto refusal = call_colleague(
      request("openai"), capture,
      [&](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
        return Value::object(
            {{"status", Value{"completed"}},
             {"model", Value{"gpt-refused"}},
             {"usage", usage},
             {"output",
              Value{Value::Array{Value::object(
                  {{"type", Value{"message"}},
                   {"role", Value{"assistant"}},
                   {"content", Value{Value::Array{
                                   Value::object({{"type", Value{"output_text"}},
                                                  {"text", Value{"partial"}}}),
                                   Value::object({{"type", Value{"refusal"}},
                                                  {"refusal", Value{"No"}}})}}}})}}}});
      });
  check(string_field(refusal, "status") == "refused" &&
            string_field(refusal, "remote_disposition") == "completed" &&
            !refusal.find("text") && field(refusal, "usage") == usage &&
            string_field(refusal, "actual_model") == "gpt-refused",
        "mixed text/refusal preserves metadata without presenting a completed answer");
  check(string_field(reply, "model_metadata_status") == "observed" &&
            string_field(multi_reply, "model_metadata_status") == "ambiguous" &&
            string_field(no_diagnostic, "model_metadata_status") == "unavailable",
        "model attribution must distinguish observed, ambiguous and unavailable");
  const auto malformed_model = call_colleague(
      request("openai"), capture,
      [&](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
        return Value::object({{"status", Value{"incomplete"}},
                              {"model", Value{Number{7}}},
                              {"usage", Value{"invalid reported usage"}}});
      });
  check(field(malformed_model, "actual_model") == Value{} &&
            string_field(malformed_model, "model_metadata_status") == "malformed" &&
            field(malformed_model, "usage") == Value{"invalid reported usage"},
        "malformed metadata stays explicit while exact reported usage survives");
}
int main() {
  try {
    std::vector<std::string> events;
    int calls = 0;
    auto capture = [&](std::string_view name, std::string_view) {
      events.emplace_back(name);
    };
    reported_failure_cases(capture);
    const auto transport = [&](const Value &prepared, const ColleagueCapture &,
                               const RawAdapterContext *) {
      ++calls;
      check(events.back() == "admission");
      check(prepared.find("prompt")->string().find("selected bytes") !=
            std::string::npos);
      return unwrap(parse_json(
          R"({"exit_code":0,"output":"{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"diagnosis\",\"modelUsage\":{\"claude-actual\":{}},\"usage\":{\"input_tokens\":12}}"})"));
    };
    auto reply = call_colleague(request(), capture, transport);
    check(reply.find("status")->string() == "completed" && calls == 1);
    check(reply.find("from")->string() == "diagnostician" &&
          reply.find("to")->string() == "arco");
    check(reply.find("actual_model")->string() == "claude-actual");
    check(reply.find("model_usage")->find("claude-actual") != nullptr);
    check(reply.find("usage")->find("input_tokens")->number().text() == "12");
    auto invalid = request("unconfigured");
    reply = call_colleague(invalid, capture, transport);
    check(reply.find("status")->string() == "refused" && calls == 1);
    auto bad_profile = request();
    for (auto &[key, value] : bad_profile.object())
      if (key == "profile")
        for (auto &[k, v] : value.object())
          if (k == "tools")
            v = Value{"shell"};
    reply = call_colleague(bad_profile, capture, transport);
    check(reply.find("status")->string() == "refused" && calls == 1);
    try {
      (void)call_colleague(
          request(),
          [](std::string_view, std::string_view) {
            throw Error{ErrorCode::audit_unavailable};
          },
          transport);
      check(false);
    } catch (const Error &) {
      check(calls == 1);
    }
    reply = call_colleague(request(), capture,
                           [&](const Value &, const ColleagueCapture &c,
                               const RawAdapterContext *) -> Value {
                             ++calls;
                             c("raw", "partial");
                             throw Error{ErrorCode::external_unknown};
                           });
    check(reply.find("status")->string() == "unknown" && calls == 2);
    check(events[events.size() - 2] == "raw");
    reply = call_colleague(
        request(), capture,
        [](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
          return unwrap(parse_json(R"({"exit_code":0,"output":"partial prose"})"));
        });
    check(reply.find("status")->string() == "unknown");
    reply = call_colleague(
        request(), capture,
        [](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
          return unwrap(parse_json(R"({"exit_code":7,"output":"refused"})"));
        });
    check(reply.find("status")->string() == "failed" &&
          reply.find("remote_disposition")->string() == "unknown");
    reply = call_colleague(
        request("openai"), capture,
        [](const Value &p, const ColleagueCapture &, const RawAdapterContext *) {
          check(p.find("upstream")->find("tools")->array().empty());
          return unwrap(parse_json(
              R"({"status":"completed","model":"gpt-actual","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"native diagnosis"}]}],"usage":{"output_tokens":3}})"));
        });
    check(reply.find("status")->string() == "completed" &&
          reply.find("actual_model")->string() == "gpt-actual");
    reply = call_colleague(Value{}, capture, transport);
    check(reply.find("status")->string() == "refused" && calls == 2);
    reply = call_colleague(
        request(), capture,
        [](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
          return unwrap(parse_json(
              R"({"exit_code":0,"output":"{\"type\":\"result\",\"is_error\":false,\"subtype\":\"success\",\"result\":7}"})"));
        });
    check(reply.find("status")->string() == "unknown");
    reply = call_colleague(
        request(), capture,
        [](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
          return unwrap(parse_json(
              R"({"exit_code":1,"output":"{\"is_error\":true,\"result\":\"OAuth expired\",\"api_error_status\":401}"})"));
        });
    check(reply.find("status")->string() == "failed" &&
          reply.find("reported_error")->find("api_error_status")->number().text() ==
              "401");
    reply = call_colleague(
        request("openai"), capture,
        [](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
          return unwrap(parse_json(
              R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"refusal","refusal":"No"}]}]})"));
        });
    check(reply.find("status")->string() == "refused" &&
          reply.find("remote_disposition")->string() == "completed");
    reply = call_colleague(
        request("openai"), capture,
        [](const Value &, const ColleagueCapture &, const RawAdapterContext *) {
          return unwrap(parse_json(
              R"({"status":"completed","output":[{"type":"function_call","name":"shell","arguments":"{}"}]})"));
        });
    check(reply.find("status")->string() == "unknown");
    // Direct installed exec seam oracle: partial output + timeout is unknown, no
    // replay.
    auto p = prepare_colleague(request());
    for (auto &[key, value] : p.object())
      if (key == "exec")
        value = unwrap(parse_json(
            R"({"argv":["/bin/sh","-c","printf partial; sleep 4"],"timeout_seconds":1})"));
    reply = call_colleague(
        request(), capture,
        [&](const Value &, const ColleagueCapture &c, const RawAdapterContext *) {
          return native_colleague_transport(p, c);
        });
    check(reply.find("status")->string() == "unknown");
    std::cout << "colleague contract checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << '\n';
    return 1;
  }
}
