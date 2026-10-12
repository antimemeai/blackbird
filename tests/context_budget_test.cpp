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
  bool expect_trigger = false;
  int calls = 0;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    if (calls == 1) {
      require(string_field(request, "model") == "request-override");
      require(string_field(request, "instructions").find("request-override") !=
              std::string::npos);
    }
    require((string_field(request, "instructions").find("CONTEXT_BUDGET") !=
             std::string::npos) == expect_trigger);
    if (calls == 2)
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
      assert(v.effective.enabled==false and arco.json.encode(v.pending)=='null')
      local r=arco.call('context_budget',{proposal={base=v.revision,enabled=true,trigger_bytes=100,target_bytes=50,reason='direct oracle'}})
      assert(r.staged and not r.effective.enabled and r.pending.enabled and r.pending_revision~=v.revision)
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
