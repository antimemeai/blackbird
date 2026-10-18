#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/responses_provider.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <stdexcept>
#include <thread>
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
// Fake curl for the provider deadline cases (arconaut-pyg): the test binary itself,
// selected by curl's first argument. Each invocation records its ordinal and argv.
constexpr std::string_view delta =
    "data: {\"type\":\"response.output_text.delta\",\"item_id\":\"m\","
    "\"delta\":\"x\"}\n\n";
constexpr std::string_view dispatched =
    "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\","
    "\"model\":\"fixture-model\",\"output\":[{\"type\":\"function_call\","
    "\"status\":\"completed\",\"call_id\":\"c1\",\"name\":\"context_stats\","
    "\"arguments\":\"{}\"}]}}\n\n";
int fake_curl(int argc, char **argv) {
  const std::string input{std::istreambuf_iterator<char>{std::cin}, {}};
  if (input.find("Authorization: Bearer fixture-secret") == std::string::npos)
    return 11;
  const char *state_text = std::getenv("BB_DEADLINE_STATE");
  const char *mode_text = std::getenv("BB_DEADLINE_MODE");
  if (state_text == nullptr || mode_text == nullptr)
    return 12;
  const std::filesystem::path state{state_text};
  const std::string_view mode{mode_text};
  unsigned ordinal = 1;
  {
    std::ifstream previous{state / "calls"};
    if (previous >> ordinal)
      ++ordinal;
  }
  std::ofstream{state / "calls"} << ordinal;
  {
    std::ofstream args{state / ("argv-" + std::to_string(ordinal))};
    for (int i = 1; i < argc; ++i)
      args << argv[i] << '\n';
  }
  const auto chunk = [](std::string_view bytes) {
    std::cout << bytes << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
  };
  if (mode == "ceiling") // steady bytes, never completes
    for (;;)
      chunk(delta);
  if (mode == "stall" && ordinal == 1) { // one chunk, then silence
    chunk(delta);
    std::this_thread::sleep_for(std::chrono::seconds{60});
    return 0;
  }
  if (mode == "slow") // 2.5 s of steady bytes: beyond the stall and the old cap
    for (int i = 0; i < 10; ++i)
      chunk(delta);
  std::cout << dispatched << "\n200" << std::flush;
  return 0;
}
struct DeadlineRun {
  std::optional<Error> failure;
  unsigned calls = 0, tools = 0;
  std::vector<std::string> statuses;
  std::vector<std::string> argv; // first invocation
  std::chrono::steady_clock::duration elapsed{};
};
DeadlineRun deadline_run(const std::filesystem::path &directory, std::string_view mode,
                         ProviderTimeouts timeouts, const std::string &executable) {
  const auto root_path = directory / std::string{mode};
  std::filesystem::create_directories(root_path / "audit");
  std::filesystem::create_directories(root_path / "state");
  std::filesystem::permissions(root_path / "audit", std::filesystem::perms::owner_all);
  check(::setenv("BB_DEADLINE_MODE", std::string{mode}.c_str(), 1) == 0 &&
        ::setenv("BB_DEADLINE_STATE", (root_path / "state").c_str(), 1) == 0);
  RegisteredResponsesConfig config;
  config.provider = "paper";
  config.auth.directory = root_path / "auth";
  config.auth.curl_executable = executable;
  config.timeouts = timeouts;
  {
    ProviderAuth auth{config.auth};
    auth.add_provider("paper", "https://api.paperinstruments.com/v1", "responses",
                      "PAPER_TEST_KEY");
    auth.set_key("paper", "fixture", "fixture-secret");
  }
  IdentityBytes environment{}, stream{};
  environment[0] = std::byte{91};
  stream[0] = std::byte{92};
  const JournalHeader header{unwrap(EnvironmentId::from_bytes(environment)),
                             unwrap(AuditStreamId::from_bytes(stream)),
                             3,
                             {4 * 1024 * 1024, 16 * 1024 * 1024},
                             std::nullopt};
  auto root = unwrap(RetainedState::create(
      std::make_unique<NativeJournalDirectory>(
          unwrap(NativeJournalDirectory::open((root_path / "audit").string()))),
      "audit", header, {64 * 1024 * 1024, 10000}));
  AuditLog log{*root};
  ContextStore context{log};
  RegisteredResponsesCodingProvider provider{config};
  CodingEngine engine{log, context, provider, "fixture-model"};
  DeadlineRun run;
  engine.status = [&](std::string_view event) {
    if (event.starts_with("provider attempt"))
      run.statuses.emplace_back(event);
  };
  engine.operation_completed = [&](std::string_view event) {
    if (event.starts_with("context_stats completed"))
      ++run.tools;
  };
  const auto started = std::chrono::steady_clock::now();
  try {
    engine.turn(
        {"deadline",
         R"(local r=arco.request({retry_policy={max_attempts=3,base_ms=0,cap_ms=0}})
for _,v in ipairs(r.output) do
  if v.type=='function_call' then
    local out=arco.call(v.name,{})
    arco.append({{type='function_call_output',call_id=v.call_id,
      output=arco.json.encode(out)}})
  end
end)"});
  } catch (const Error &error) {
    run.failure = error;
  }
  run.elapsed = std::chrono::steady_clock::now() - started;
  std::ifstream{root_path / "state" / "calls"} >> run.calls;
  std::ifstream args{root_path / "state" / "argv-1"};
  for (std::string line; std::getline(args, line);)
    run.argv.push_back(line);
  return run;
}
void deadlines(const std::filesystem::path &directory, const std::string &executable) {
  using std::chrono::milliseconds;
  // A stream that goes silent fails at the stall timeout and is retried; the retry
  // completes and its tool call runs.
  auto stall = deadline_run(directory, "stall",
                            {.stall_seconds = 1, .request_seconds = 30}, executable);
  check(!stall.failure && stall.calls == 2 && stall.tools == 1);
  check(stall.statuses.size() == 1 &&
        stall.statuses[0].starts_with("provider attempt 1 stalled") &&
        stall.statuses[0].find("(provider_transport:28)") != std::string::npos &&
        stall.statuses[0].find("retry 2/3") != std::string::npos);
  check(stall.elapsed >= milliseconds{1000} && stall.elapsed < milliseconds{6000});
  // curl's own limit is a backstop strictly beyond Blackbird's ceiling.
  const auto max_time = std::find(stall.argv.begin(), stall.argv.end(), "--max-time");
  check(max_time != stall.argv.end() && std::next(max_time) != stall.argv.end() &&
        *std::next(max_time) == std::to_string(30 + provider_curl_backstop_seconds));
  // Steady bytes for 2.5 s: longer than the 1 s stall timeout and than the old
  // fixed cap scaled the same way (180/120 of the stall, 1.5 s). One attempt.
  auto slow = deadline_run(directory, "slow",
                           {.stall_seconds = 1, .request_seconds = 30}, executable);
  check(!slow.failure && slow.calls == 1 && slow.tools == 1 && slow.statuses.empty());
  check(slow.elapsed >= milliseconds{2500});
  // Steady bytes forever reach the 2 s total ceiling, reported as such, never retried.
  auto ceiling = deadline_run(directory, "ceiling",
                              {.stall_seconds = 1, .request_seconds = 2}, executable);
  check(ceiling.failure == provider_ceiling_error && ceiling.calls == 1 &&
        ceiling.tools == 0);
  check(ceiling.statuses.size() == 1 &&
        ceiling.statuses[0] == "provider attempt 1 reached the total request ceiling "
                               "(incomplete:60); not retried");
  check(ceiling.elapsed >= milliseconds{2000} && ceiling.elapsed < milliseconds{6000});
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 1 && std::string_view{argv[1]} == "--disable")
    return fake_curl(argc, argv);
  char pattern[] = "/tmp/blackbird-responses-XXXXXX";
  const auto *path = ::mkdtemp(pattern);
  if (!path)
    return 2;
  try {
    exports();
    streams();
    transport(std::filesystem::path{path} / "auth");
    deadlines(std::filesystem::path{path} / "deadlines",
              std::filesystem::canonical(argv[0]).string());
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
