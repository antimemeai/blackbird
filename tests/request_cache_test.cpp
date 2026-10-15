#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/request_cache.hpp"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <unistd.h>
using namespace blackbird;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("request cache contract " + std::to_string(where.line()));
}
template <class T> T id(unsigned char byte) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(byte);
  return unwrap(T::from_bytes(bytes));
}
class Provider final : public CodingProvider {
public:
  Value native, wire;
  bool supported = true;
  unsigned calls = 0;
  std::string_view provider_identity() const noexcept override {
    return "openai-codex";
  }
  bool supports_prompt_cache_policy() const noexcept override { return supported; }
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &) override {
    ++calls;
    native = request;
    wire = export_provider_request(request);
    return Value::object({{"output", Value{Value::Array{}}}});
  }
};
void native_data() {
  const auto data = unwrap(
      parse_json(R"([{"type":"input_text","text":"alpha","cache_control":"data"}])"));
  auto request = Value::object(
      {{"model", Value{"gpt-6.1-sol"}},
       {"input",
        Value{Value::Array{Value::object({{"type", Value{"function_call_output"}},
                                          {"call_id", Value{"c"}},
                                          {"output", data}})}}}});
  const auto original = Value{request};
  auto wire = export_provider_request(request);
  const auto &encoded = field(field(wire, "input").array()[0], "output");
  check(std::holds_alternative<std::string>(encoded.value()));
  check(unwrap(parse_json(encoded.string())) == data);
  check(request == original);
  for (const auto mode : {PromptCacheMode::stable_prefix, PromptCacheMode::disabled}) {
    auto configured = request;
    prepare_request_cache(configured, {mode, true, "openai-codex", 1});
    wire = export_provider_request(configured);
    check(!wire.find("blackbird_cache_policy"));
  }
  auto explicit_request = request;
  for (auto &[key, input] : explicit_request.object())
    if (key == "input")
      input.array()[0].object().emplace_back("blackbird_output_parts", Value{true});
  wire = export_provider_request(explicit_request);
  check(field(field(wire, "input").array()[0], "output") == data);
  check(!field(wire, "input").array()[0].find("blackbird_output_parts"));
  bool conflict = false;
  try {
    prepare_request_cache(explicit_request,
                          {PromptCacheMode::disabled, true, "openai-codex", 1});
  } catch (const Error &error) {
    conflict = error.code == ErrorCode::conflict;
  }
  check(conflict);
}
void exercise(const std::filesystem::path &path) {
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                       NativeJournalDirectory::open(path.string()))),
                                   "audit",
                                   {id<EnvironmentId>(1),
                                    id<AuditStreamId>(2),
                                    3,
                                    {1024 * 1024, 4 * 1024 * 1024},
                                    std::nullopt},
                                   {64 * 1024 * 1024, 20000}));
  AuditLog log{*root};
  ContextStore context{log};
  Provider provider;
  CodingEngine engine{log, context, provider, "gpt-6.1-sol"};
  engine.turn({"", R"(
    assert(blackbird.call('context_budget',{proposal={enabled=true,trigger_bytes=100,
      target_bytes=50,reason='specification'}}).staged)
  )"});
  constexpr std::string_view request = R"(
    blackbird.request({instructions='unchanged instruction',cache_policy='stable_prefix',
      input={{type='function_call',call_id='c',name='fixture',arguments={x=1}},
             {type='function_call_output',call_id='c',output={answer='CACHED_PREFIX'}}}})
  )";
  engine.turn({"", request});
  check(string_field(provider.native, "instructions") == "unchanged instruction");
  const auto &items = field(provider.native, "input").array();
  check(items.size() == 3 && string_field(items.back(), "role") == "developer");
  check(string_field(items.back(), "content").find("CONTEXT_BUDGET") !=
        std::string::npos);
  check(context.items().empty());
  check(!provider.wire.find("blackbird_cache_policy") &&
        !provider.wire.find("cache_policy"));
  check(field(field(provider.wire, "prompt_cache_options"), "mode") ==
        Value{"implicit"});
  const auto &wire_items = field(provider.wire, "input").array();
  const auto &part = field(wire_items[1], "output").array()[0];
  check(field(part, "prompt_cache_breakpoint") ==
        Value::object({{"mode", Value{"explicit"}}}));
  check(unwrap(parse_json(string_field(part, "text"))) ==
        Value::object({{"answer", Value{"CACHED_PREFIX"}}}));
  const auto first_instructions = field(provider.native, "instructions");
  engine.turn({"", request});
  check(field(provider.native, "instructions") == first_instructions);
  engine.turn({"", R"(blackbird.request({instructions='unchanged instruction',
    state_context='omit',cache_policy='disabled',input={{role='user',content='x'}}}))"});
  check(field(provider.wire, "input").array().size() == 1);
  check(!provider.wire.find("state_context"));
  check(field(field(provider.wire, "prompt_cache_options"), "mode") ==
        Value{"explicit"});
  check(field(field(provider.wire, "input").array()[0], "content") == Value{"x"});
  engine.turn({"", R"(
    local config=blackbird.call('program_config',{})
    config.effective.prompt_cache='stable_prefix'
    assert(blackbird.call('program_config',{base=config.revision,
      proposal=config.effective}).staged)
    blackbird.request({input={{role='user',content='before activation'}}})
  )"});
  check(!provider.wire.find("prompt_cache_options"));
  engine.turn(
      {"", "blackbird.request({input={{role='user',content='after activation'}}})"});
  check(field(field(provider.wire, "prompt_cache_options"), "mode") ==
        Value{"implicit"});
  bool failed = false;
  try {
    engine.turn({"", R"(
      local config=blackbird.call('program_config',{})
      config.effective.prompt_cache='disabled'
      assert(blackbird.call('program_config',{proposal=config.effective}).staged)
      error('do not activate')
    )"});
  } catch (const Error &) {
    failed = true;
  }
  check(failed);
  engine.turn({"", R"(
    assert(blackbird.call('program_config',{}).effective.prompt_cache=='stable_prefix')
    blackbird.request({cache_policy='disabled',input={{role='user',content='override'}}})
  )"});
  check(field(field(provider.wire, "prompt_cache_options"), "mode") ==
        Value{"explicit"});
  const auto before = provider.calls;
  provider.supported = false;
  bool refused = false;
  try {
    engine.turn({"", "blackbird.request({cache_policy='disabled'})"});
  } catch (const Error &error) {
    refused = error.code == ErrorCode::unsupported;
  }
  check(refused && provider.calls == before);
}
void reopen(const std::filesystem::path &path) {
  auto root =
      unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                                     NativeJournalDirectory::open(path.string()))),
                                 "audit",
                                 {id<EnvironmentId>(1),
                                  id<AuditStreamId>(2),
                                  3,
                                  {1024 * 1024, 4 * 1024 * 1024},
                                  std::nullopt},
                                 {64 * 1024 * 1024, 20000}));
  unwrap(root->confirm_recovery());
  recover_coding_session(*root);
  AuditLog log{*root};
  ContextStore context{log};
  Provider provider;
  CodingEngine engine{log, context, provider, "gpt-6.1-sol"};
  engine.turn({"", R"(
    assert(blackbird.call('program_config',{}).effective.prompt_cache=='stable_prefix')
    blackbird.request({input={{role='user',content=string.rep('r',120)}}})
  )"});
  check(field(field(provider.wire, "prompt_cache_options"), "mode") ==
        Value{"implicit"});
  const auto &items = field(provider.native, "input").array();
  check(items.size() == 2 &&
        string_field(items[0], "content") == std::string(120, 'r') &&
        string_field(items[1], "role") == "developer");
  check(context.items().empty());
}
} // namespace
int main() {
  char pattern[] = "/tmp/blackbird-cache-XXXXXX";
  const auto *path = ::mkdtemp(pattern);
  if (!path)
    return 2;
  try {
    native_data();
    exercise(path);
    reopen(path);
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
