#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include <iostream>
#include <unistd.h>
using namespace blackbird;
void require(bool b) {
  if (!b)
    throw std::runtime_error("context budget oracle failed");
}
template <class T> T id(unsigned char n) {
  IdentityBytes b{};
  b[0] = std::byte{n};
  return unwrap(T::from_bytes(b));
}
class Observer final : public CodingProvider {
public:
  bool expect_trigger = false, expect_size = true, retain = false, scripted = true;
  int calls = 0;
  std::string last_state;
  bool retain_request_state() const noexcept override { return retain; }
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    const auto &items = field(request, "input").array();
    const bool stateful = !items.empty() && items.back().find("role") &&
                          field(items.back(), "role") == Value{"developer"};
    last_state = stateful ? string_field(items.back(), "content") : std::string{};
    // a1v C2: the size line reports exactly the conversation it rides on.
    const bool sized =
        last_state.find("CONTEXT_SIZE input_bytes=") != std::string::npos;
    require(sized == expect_size);
    if (sized) {
      const Value::Array conversation(items.begin(), items.end() - 1);
      require(
          last_state.find(
              "CONTEXT_SIZE input_bytes=" +
              std::to_string(unwrap(encode_packet_string(Value{conversation})).size()) +
              " entries=" + std::to_string(conversation.size()) + " ") !=
          std::string::npos);
    }
    if (scripted && calls == 1) {
      require(string_field(request, "model") == "request-override");
      require(string_field(request, "instructions").find("request-override") ==
              std::string::npos);
    }
    require(string_field(request, "instructions").find("CONTEXT_BUDGET") ==
            std::string::npos);
    const auto &input = field(request, "input").array();
    const bool has_state =
        !input.empty() && input.back().find("role") &&
        field(input.back(), "role") == Value{"developer"} &&
        string_field(input.back(), "content").find("CONTEXT_BUDGET") !=
            std::string::npos;
    require(has_state == expect_trigger);
    if (scripted && calls == 1)
      require(string_field(input.back(), "content").find("request-override") !=
              std::string::npos);
    if (scripted && calls == 2)
      require(unwrap(dump_json(field(request, "input"))).find("REPAIR_REQUIRED_42") !=
              std::string::npos);
    auto response = Value::object({{"output", Value{Value::Array{}}}});
    capture(unwrap(dump_json(response)));
    return response;
  }
};
int main() {
  char name[] = "/tmp/arco-budget-XXXXXX";
  auto made = mkdtemp(name);
  if (!made)
    return 1;
  const std::filesystem::path path{made};
  try {
    Value expected_policy;
    {
      auto root = unwrap(
          RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                    NativeJournalDirectory::open(path.string()))),
                                "audit",
                                {id<EnvironmentId>(1),
                                 id<AuditStreamId>(2),
                                 7,
                                 {1024 * 1024, 4 * 1024 * 1024},
                                 std::nullopt},
                                {64 * 1024 * 1024, 20000}));
      AuditLog log{*root};
      ContextStore context{log};
      Observer provider;
      CodingEngine engine{log, context, provider, "test-model"};
      engine.display = [](std::string_view v) { std::cerr << v; };
      engine.turn({"", R"(
      local v=arco.call('context_budget',{})
      local n=arco.json.encode
      assert(v.effective.enabled==true and n(v.effective.trigger_bytes)=='393216' and n(v.effective.target_bytes)=='196608' and n(v.pending)=='null')
      local r=arco.call('context_budget',{proposal={base=v.revision,enabled=true,trigger_bytes=100,target_bytes=50,reason='direct oracle'}})
      assert(r.staged and n(r.effective.trigger_bytes)=='393216' and r.pending.enabled and n(r.pending.trigger_bytes)=='100' and r.pending_revision~=v.revision)
      for _,bad in ipairs({{}, {enabled=true,trigger_bytes=100,target_bytes=0,reason='bad'}, {enabled=true,trigger_bytes=100.5,target_bytes=50,reason='bad'}, {enabled='yes',trigger_bytes=100,target_bytes=50,reason='bad'}, {enabled=true,trigger_bytes=100,target_bytes=50,reason=''}}) do
        assert(not arco.call('context_budget',{proposal=bad}).staged)
      end
      assert(not arco.call('context_budget',{proposal={base='stale',enabled=false,trigger_bytes=100,target_bytes=50,reason='stale'}}).staged)
      assert(not arco.call('context_budget',{proposal={enabled=true,trigger_bytes=20,target_bytes=30,reason='invalid'}}).staged)
    )"});
      require(std::get<bool>(field(field(engine.stats(), "context_budget"), "effective")
                                 .find("enabled")
                                 ->value()));
      try {
        engine.turn(
            {"", "arco.call('context_budget',{proposal={enabled=false,trigger_bytes="
                 "100,target_bytes=50,reason='cancel'}}); error('failed program')"});
        require(false);
      } catch (const Error &) {
      }
      require(std::get<bool>(field(field(engine.stats(), "context_budget"), "effective")
                                 .find("enabled")
                                 ->value()));
      bool paused = false;
      engine.cancelled = [&] { return paused; };
      engine.operation_completed = [&](std::string_view v) {
        if (v.starts_with("context_budget"))
          paused = true;
      };
      try {
        engine.turn(
            {"", "assert(arco.call('context_budget',{proposal={enabled=false,trigger_"
                 "bytes=100,target_bytes=50,reason='operator pause'}}).staged)"});
        require(false);
      } catch (const Error &e) {
        require(e.code == ErrorCode::interrupted);
      }
      engine.cancelled = {};
      engine.operation_completed = {};
      require(std::get<bool>(field(field(engine.stats(), "context_budget"), "effective")
                                 .find("enabled")
                                 ->value()));
      require(field(field(engine.stats(), "context_budget"), "pending") == Value{});
      context.append({Value::object({{"role", Value{"assistant"}},
                                     {"content", Value{"REPAIR_REQUIRED_42 " +
                                                       std::string(300, 'x')}}})},
                     "oracle");
      const auto original =
          string_field(field(context.view(), "entries").array().back(), "id");
      const auto before_combined = context.view();
      engine.operation_completed = [&](std::string_view v) {
        if (v.starts_with("context_budget"))
          context.protect = [](const Value::Array &, const Value &,
                               const RawEventRef &) {
            throw Error{ErrorCode::capacity};
          };
      };
      try {
        engine.turn(
            {"",
             "local v=arco.context(); "
             "assert(arco.manage({base=v.base,mode='archive',ids={'" +
                 original +
                 "'},reason='combined fault',source='oracle'}).staged); "
                 "assert(arco.call('context_budget',{proposal={enabled=false,trigger_"
                 "bytes=200,target_bytes=100,reason='combined fault'}}).staged)"});
        require(false);
      } catch (const Error &e) {
        require(e.code == ErrorCode::capacity);
      }
      engine.operation_completed = {};
      require(context.view() == before_combined);
      require(std::get<bool>(field(field(engine.stats(), "context_budget"), "effective")
                                 .find("enabled")
                                 ->value()));
      require(field(field(engine.stats(), "context_budget"), "pending") == Value{});
      provider.expect_trigger = true;
      engine.turn({"", "arco.request({model='request-override'})"});
      engine.turn({"", "local v=arco.context(); local "
                       "r=arco.manage({base=v.base,mode='summarize',ids={'" +
                           original +
                           "'},reason='intentional "
                           "omission',source='oracle',summary={role='assistant',"
                           "content='short'}}); assert(r.staged)"});
      require(unwrap(dump_json(context.view())).find("REPAIR_REQUIRED_42") ==
              std::string::npos);
      engine.turn(
          {"", "local p=arco.inspect({kind='originals',entry='" + original +
                   "',limit=512}); assert(p.total_bytes); local v=arco.context(); "
                   "local r=arco.manage({base=v.base,mode='restore',ids={'" +
                   original +
                   "'},reason='repair omission',source='bounded original'}); "
                   "assert(r.staged)"});
      engine.turn({"", "arco.request()"});
      require(provider.calls == 2);
      engine.display = [](std::string_view) {
        throw std::runtime_error("notification failure");
      };
      engine.turn(
          {"",
           "local v=arco.context(); "
           "assert(arco.manage({base=v.base,mode='archive',ids={'" +
               original +
               "'},reason='combined publication',source='oracle'}).staged); "
               "assert(arco.call('context_budget',{proposal={enabled=false,trigger_"
               "bytes=200,target_bytes=100,reason='combined publication'}}).staged)"});
      require(unwrap(dump_json(context.view())).find("REPAIR_REQUIRED_42") ==
              std::string::npos);
      require(
          !std::get<bool>(field(field(engine.stats(), "context_budget"), "effective")
                              .find("enabled")
                              ->value()));
      CodingEngine reopened{log, context, provider, "other-model"};
      require(field(field(reopened.stats(), "context_budget"), "effective") ==
              field(field(engine.stats(), "context_budget"), "effective"));
      expected_policy = field(field(engine.stats(), "context_budget"), "effective");
    }
    {
      auto root =
          unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                                         NativeJournalDirectory::open(path.string()))),
                                     "audit",
                                     {id<EnvironmentId>(1),
                                      id<AuditStreamId>(2),
                                      7,
                                      {1024 * 1024, 4 * 1024 * 1024},
                                      std::nullopt},
                                     {64 * 1024 * 1024, 20000}));
      unwrap(root->confirm_recovery());
      recover_coding_session(*root);
      AuditLog log{*root};
      ContextStore context{log};
      Observer provider;
      CodingEngine engine{log, context, provider, "reopened-model"};
      require(field(field(engine.stats(), "context_budget"), "effective") ==
              expected_policy);
      require(field(field(engine.stats(), "context_budget"), "pending") == Value{});
      // The disabled policy still reports its size; omit drops all state; a
      // provider retaining request state gets no item when nothing else rides.
      provider.scripted = false;
      engine.turn({"", "arco.request()"});
      require(provider.last_state.find("budget=off") != std::string::npos);
      provider.expect_size = false;
      engine.turn({"", "arco.request({state_context='omit'})"});
      require(provider.last_state.empty());
      provider.retain = true;
      engine.turn({"", "arco.request()"});
      require(provider.last_state.empty() && provider.calls == 3);
      // With a retaining provider the line rides with the trigger, carrying the
      // effective trigger and target.
      engine.turn({"", "assert(arco.call('context_budget',{proposal={enabled=true,"
                       "trigger_bytes=30,target_bytes=10,reason='retain oracle'}})"
                       ".staged)"});
      provider.expect_size = provider.expect_trigger = true;
      engine.turn({"", "arco.request()"});
      require(provider.last_state.find(" trigger_bytes=30 target_bytes=10 ") !=
              std::string::npos);
      // A recorded built-in policy (pre-a1v "initial-disabled") was never a
      // session choice: reopening resolves it to the current enabled default.
      log.record(
          log.issue(), ApplicationChannel::program,
          Value::object(
              {{"label", Value{"context-budget-effective-v1"}},
               {"policy", Value::object({{"enabled", Value{false}},
                                         {"trigger_bytes", Value{Number{"262144"}}},
                                         {"target_bytes", Value{Number{"131072"}}},
                                         {"reason", Value{"opt-in"}}})},
               {"revision", Value{"initial-disabled"}}}));
      CodingEngine legacy{log, context, provider, "legacy-model"};
      const auto restored_policy =
          field(field(legacy.stats(), "context_budget"), "effective");
      require(std::get<bool>(field(restored_policy, "enabled").value()) &&
              field(restored_policy, "trigger_bytes").number().text() == "393216");
    }
    std::filesystem::remove_all(path);
    std::cout << "context budget boundary/trigger/repair/reopen passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << '\n';
    return 1;
  }
}
