#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include <iostream>
#include <unistd.h>
using namespace blackbird;
void check(bool good) {
  if (!good)
    throw Error{ErrorCode::corrupt};
}
template <class T> T id(unsigned char n) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{n};
  return unwrap(T::from_bytes(bytes));
}
class Provider final : public CodingProvider {
public:
  Value request;
  int calls = 0;
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    ++calls;
    if (calls == 1)
      return Value::object(
          {{"output", Value{Value::Array{Value::object(
                          {{"type", Value{"function_call"}},
                           {"call_id", Value{"model-colleague"}},
                           {"name", Value{"colleague"}},
                           {"arguments", Value{unwrap(dump_json(request))}}})}}}});
    return unwrap(parse_json(
        R"({"output":[{"type":"message","role":"assistant","id":"m1","content":[{"type":"output_text","text":"done"}]}]})"));
  }
};
int main() {
  char name[] = "/tmp/blackbird-colleague-engine-XXXXXX";
  const auto directory = mkdtemp(name);
  if (!directory)
    return 2;
  try {
    const JournalHeader header{id<EnvironmentId>(1),
                               id<AuditStreamId>(2),
                               3,
                               {1024 * 1024, 4 * 1024 * 1024},
                               std::nullopt};
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                         NativeJournalDirectory::open(directory))),
                                     "audit", header, {32 * 1024 * 1024, 20000}));
    AuditLog log{*root};
    ContextStore context{log};
    Provider provider;
    CodingEngine engine{log, context, provider, "fake"};
    engine.diagnostic = [](std::string_view text) { std::cerr << text; };
    const auto request = unwrap(parse_json(
        R"({"request_id":"one","from":"main","to":"reviewer","provider":"claude","model":"sonnet","task":"Review selected code","context":[{"id":"source","text":"SELECTED_ONLY"}],"profile":{"name":"context-only","provenance":"test","timeout_seconds":2,"tools":"none","requests":1}})"));
    context.append({Value::object({{"role", Value{"user"}},
                                   {"content", Value{"PRIVATE_NOT_SELECTED"}}})},
                   "test");
    int dispatches = 0;
    bool unknown = false;
    engine.colleague_transport = [&](const Value &prepared,
                                     const ColleagueCapture &capture,
                                     const RawAdapterContext *) {
      ++dispatches;
      const auto &prompt = field(prepared, "prompt").string();
      check(prompt.find("SELECTED_ONLY") != std::string::npos &&
            prompt.find("PRIVATE_NOT_SELECTED") == std::string::npos);
      capture("raw", "captured-byte");
      if (unknown)
        throw Error{ErrorCode::interrupted};
      return unwrap(parse_json(
          R"({"exit_code":0,"output":"{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"source finding\",\"modelUsage\":{\"actual-claude\":{}}}"})"));
    };
    const auto result = engine.operator_call("colleague", request);
    check(dispatches == 1 && string_field(result, "text") == "source finding" &&
          string_field(result, "actual_model") == "actual-claude");
    const auto encoded = unwrap(dump_json(request));
    const auto lua =
        "return blackbird.colleague(blackbird.decode([=[" + encoded + "]=]))";
    engine.turn({"", lua});
    check(dispatches == 2 && engine.workflow_result() == result);
    provider.request = request;
    engine.turn(
        {"request a colleague",
         R"(for i=1,2 do local r=blackbird.request(); for _,o in ipairs(r.output) do if o.type=="function_call" then blackbird.append({{type="function_call_output",call_id=o.call_id,output=blackbird.encode(blackbird.call(o.name,o.arguments))}}) end end end)"});
    check(dispatches == 3 && provider.calls == 2);
    auto invalid = request;
    for (auto &[key, value] : invalid.object())
      if (key == "provider")
        value = Value{"missing"};
    check(string_field(engine.operator_call("colleague", invalid), "status") ==
              "refused" &&
          dispatches == 3);
    engine.participant_transport =
        [&](const Value &prepared, const ColleagueCapture &capture,
            const std::function<bool()> &, const RawAdapterContext *context) {
          return engine.colleague_transport(prepared, capture, context);
        };
    const auto task = engine.tasks().operator_command("add WORK_STAYS_QUEUED");
    const auto task_id = field(field(task, "changed").array()[0], "id");
    const auto start = engine.operator_call(
        "participant_start",
        Value::object({{"request", request}, {"task_id", task_id}}));
    const auto run_id = string_field(start, "run_id");
    bool busy = false;
    try {
      engine.validate_restart();
    } catch (const Error &e) {
      busy = e.code == ErrorCode::busy;
    }
    check(busy);
    engine.turn(
        {"", "return blackbird.participants.join({run_ids={'" + run_id + "'}})"});
    const auto &joined = field(engine.workflow_result(), "runs").array();
    check(joined.size() == 1 && string_field(joined[0], "state") == "completed");
    check(string_field(field(engine.tasks().read(), "items").array()[0], "status") ==
          "queued");
    engine.validate_restart();
    check(string_field(engine.operator_call("participant_read",
                                            Value::object({{"run_id", Value{run_id}}})),
                       "state") == "completed");
    unwrap(context.checkpoint());
    bool participant_saved = false;
    for (const auto &packet : root->current_programs())
      if (string_field(packet, "label") == "participants-state-v1") {
        participant_saved = true;
        check(string_field(field(packet, "runs").array()[0], "state") == "completed");
      }
    check(participant_saved);
    unknown = true;
    const auto lost = engine.operator_call("colleague", request);
    check(string_field(lost, "remote_disposition") == "unknown" && dispatches == 5);
    const auto unresolved = root->unresolved_attempts();
    check(unresolved.has_value() && unresolved.value().empty());
    bool saw_unknown = false, saw_raw = false;
    for (const auto &fact : root->committed_facts()) {
      if (const auto *observation =
              std::get_if<AttemptObservationEvent>(&fact.event.body))
        saw_unknown |= observation->disposition == AttemptDisposition::unknown;
      if (const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body)) {
        const auto packet = unwrap(read_packet(record->payload));
        if (const auto *label = packet.find("label"))
          saw_raw |= label->string() == "colleague.raw";
      }
    }
    check(saw_unknown && saw_raw);
    engine.colleague_transport = [&](const Value &, const ColleagueCapture &capture,
                                     const RawAdapterContext *) -> Value {
      ++dispatches;
      capture("raw", "ACTUAL_DISPATCH_THEN_ALLOCATION");
      throw std::bad_alloc{};
    };
    const auto allocation = engine.operator_call("colleague", request);
    check(string_field(allocation, "error") == "allocation" && dispatches == 6);
    std::optional<AttemptObservationEvent> last_terminal;
    for (const auto &fact : root->committed_facts())
      if (const auto *observed = std::get_if<AttemptObservationEvent>(&fact.event.body);
          observed && observed->phase == AttemptPhase::terminal)
        last_terminal = *observed;
    check(last_terminal && last_terminal->disposition == AttemptDisposition::unknown &&
          unwrap(root->unresolved_attempts()).empty() && dispatches == 6);
    std::filesystem::remove_all(directory);
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << '\n';
    std::filesystem::remove_all(directory);
    return 1;
  }
}
