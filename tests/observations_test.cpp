#include "blackbird/coding.hpp"
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace blackbird;
template <class T> T id(unsigned char n) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{n};
  return unwrap(T::from_bytes(bytes));
}
Value num(std::size_t n) { return Value{Number{std::to_string(n)}}; }
void check(bool good, const char *message) {
  if (!good)
    throw std::runtime_error(message);
}
class Provider final : public CodingProvider {
public:
  std::vector<Value> requests;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &) override {
    requests.push_back(request);
    const auto text =
        Value::object({{"type", Value{"output_text"}}, {"text", Value{"done"}}});
    const auto message =
        Value::object({{"type", Value{"message"}},
                       {"role", Value{"assistant"}},
                       {"id", Value{"m" + std::to_string(requests.size())}},
                       {"content", Value{Value::Array{text}}}});
    return Value::object({{"output", Value{Value::Array{message}}}});
  }
};
Value variables(AuditLog &log, const std::string &name = "") {
  auto query = Value::object({{"variables", Value{true}},
                              {"cursor", num(0)},
                              {"count", num(64)},
                              {"scan", num(256)}});
  if (!name.empty())
    query.object().emplace_back("variable", Value{name});
  return log.trajectory(query);
}
int main() {
  char path[] = "/tmp/blackbird-observations-XXXXXX";
  const auto *directory = mkdtemp(path);
  if (!directory)
    return 2;
  try {
    const JournalHeader header{id<EnvironmentId>(1),
                               id<AuditStreamId>(2),
                               3,
                               {1024 * 1024, 4 * 1024 * 1024},
                               std::nullopt};
    const JournalCapacity cap{32 * 1024 * 1024, 20000};
    Value frozen;
    std::size_t end = 0;
    std::size_t legacy_record = 0;
    const auto legacy_time =
        Value::object({{"utc_ns", num(2000)}, {"clock_id", Value{"legacy.fixture"}}});
    {
      auto root =
          unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                           NativeJournalDirectory::open(directory))),
                                       "audit", header, cap));
      AuditLog log{*root};
      std::int64_t step = 0;
      Observations observations{log, [&]() {
                                  ++step;
                                  return ObservationTime{1000 - step * 10, step * 100,
                                                         7};
                                }};
      const auto a = observations.sample("experiment.temperature", Value{Number{"0.2"}},
                                         Value::object({{"kind", Value{"fixture"}}}));
      const auto b = observations.sample("experiment.temperature", Value{Number{"0.7"}},
                                         Value::object({{"kind", Value{"fixture"}}}));
      check(field(field(a, "time"), "utc_ns").number().text() == "980" &&
                field(field(b, "time"), "utc_ns").number().text() == "970",
            "wall clock adjustment altered");
      check(field(field(a, "time"), "monotonic_ns").number().text() == "200" &&
                field(field(b, "time"), "monotonic_ns").number().text() == "300",
            "monotonic readings lost");
      check(string_field(field(a, "time"), "synchronization") == "unknown" &&
                field(field(a, "time"), "sampling_span_ns").number().text() == "7",
            "clock precision fabricated");
      auto page = variables(log, "experiment.temperature");
      check(field(page, "events").array().size() == 2, "extensible variable absent");
      check(field(field(page, "events").array()[0], "record") == field(a, "record"),
            "sample locator wrong");
      const auto plot = correlation_plot(page);
      check(plot.find("experiment.temperature") != std::string::npos &&
                plot.find("no intervals inferred") != std::string::npos &&
                plot.find("synchronization unknown") != std::string::npos,
            "plot lost uncertainty/lanes");
      Observations second{log, [&]() { return ObservationTime{2000, 400, 9}; }};
      const auto c = second.sample("experiment.temperature", Value{Number{"0.8"}},
                                   Value::object({}));
      check(string_field(field(c, "time"), "clock_id") !=
                string_field(field(a, "time"), "clock_id"),
            "clock domains conflated");
      check(correlation_plot(variables(log)).find("clocks=2") != std::string::npos,
            "multiple clocks hidden");
      const auto invalid_before = root->fact_count();
      bool refused = false;
      try {
        observations.sample("bad name", Value{}, Value::object({}));
      } catch (const Error &e) {
        refused = e.code == ErrorCode::invalid_range;
      }
      check(refused && root->fact_count() == invalid_before,
            "invalid sample published");
      end = root->fact_count();
      frozen = variables(log);
      observations.sample("experiment.later", Value{true}, Value::object({}));
      auto pinned = log.trajectory(Value::object({{"variables", Value{true}},
                                                  {"cursor", num(0)},
                                                  {"count", num(64)},
                                                  {"scan", num(256)},
                                                  {"end", num(end)}}));
      check(pinned == frozen, "later variable leaked into pinned prefix");
      ContextStore context{log};
      Provider provider;
      CodingEngine engine{log, context, provider, "fake"};
      engine.turn(
          {"",
           R"(blackbird.request({instructions="DOCTRINE_A"}); blackbird.request({instructions="DOCTRINE_A"}); blackbird.request({instructions="DOCTRINE_B"}))"});
      check(provider.requests.size() == 3 &&
                string_field(provider.requests[0], "instructions") == "DOCTRINE_A" &&
                string_field(provider.requests[2], "instructions") == "DOCTRINE_B",
            "observer changed doctrine");
      const auto doctrines =
          field(variables(log, "doctrine.effective"), "events").array();
      check(doctrines.size() == 3, "doctrine observations missing");
      const auto first =
          string_field(field(doctrines[0], "value"), "content_observation");
      check(first ==
                    string_field(field(doctrines[1], "value"), "content_observation") &&
                first !=
                    string_field(field(doctrines[2], "value"), "content_observation"),
            "content equality observation wrong");
      check(string_field(field(doctrines[0], "source"), "field") == "instructions",
            "source selection missing");
      const auto source_attempt =
          string_field(field(doctrines[0], "source"), "attempt");
      const auto linked =
          log.trajectory(Value::object({{"attempt", Value{source_attempt}},
                                        {"cursor", num(0)},
                                        {"count", num(64)},
                                        {"scan", num(256)}}));
      bool found_doctrine = false, found_result = false;
      for (const auto &row : field(linked, "events").array()) {
        if (row.find("variable"))
          found_doctrine = true;
        if (const auto *label = row.find("label");
            label && *label == Value{"operation.result"}) {
          check(string_field(row, "operation") == "provider" &&
                    string_field(field(row, "metadata"), "attempt") == source_attempt,
                "actual operation result lost its request/source link");
          check(!row.find("time") && !row.find("duration_ns"),
                "operation producer writes projected time or duration");
          found_result = true;
        }
      }
      check(found_doctrine && found_result, "actual operation/source links absent");
      // Preserve the old packet layout solely as a compatibility fixture. New
      // operation hooks emit native clocks; the existing reader still reads old
      // scalar timing fields after reopen.
      log.original({"operation.result", unwrap(encode_packet_string(Value{true})),
                    Value::object({{"operation", Value{"legacy.fixture"}},
                                   {"time", legacy_time},
                                   {"duration_ns", num(17)}})});
      legacy_record = root->fact_count() - 1;
      const auto exact_end = root->fact_count();
      const auto shared = engine.operator_call(
          "variables_read",
          Value::object(
              {{"query", Value::object({{"variable", Value{"doctrine.effective"}},
                                        {"end", num(exact_end)},
                                        {"scan", num(256)}})}}));
      check(field(shared, "events").array().size() == 3,
            "shared variable query differs");
      const auto cursor = root->fact_count();
      for (unsigned i = 0; i < 20; ++i)
        log.record(ApplicationChannel::log, Value::object({{"label", Value{"noise"}}}));
      const auto empty = log.trajectory(Value::object(
          {{"variables", Value{true}}, {"cursor", num(cursor)}, {"scan", num(5)}}));
      check(field(empty, "events").array().empty() &&
                field(empty, "scanned").number().text() == "5",
            "filtered query exceeded scan bound");
      check(correlation_plot(
                Value::object({{"events", Value{Value::Array{Value::object({})}}}}))
                    .find("undated=1") != std::string::npos,
            "legacy untimed records invented timestamps");
      check(
          correlation_plot(
              Value::object(
                  {{"events",
                    Value{Value::Array{Value::object(
                        {{"time", Value::object({{"clock_id", Value{"old"}},
                                                 {"utc_ns", Value{"invalid"}}})}})}}}}))
                  .find("undated=1") != std::string::npos,
          "invalid clock metadata presented as time");
    }
    {
      auto root =
          unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                                         NativeJournalDirectory::open(directory))),
                                     "audit", header, cap));
      unwrap(root->confirm_recovery());
      AuditLog log{*root};
      const auto pinned = log.trajectory(Value::object({{"variables", Value{true}},
                                                        {"cursor", num(0)},
                                                        {"count", num(64)},
                                                        {"scan", num(256)},
                                                        {"end", num(end)}}));
      check(pinned == frozen, "variable history changed on reopen");
      const auto legacy = log.trajectory(Value::object(
          {{"cursor", num(legacy_record)}, {"count", num(1)}, {"scan", num(1)}}));
      const auto &rows = field(legacy, "events").array();
      check(rows.size() == 1 && field(rows.front(), "time") == legacy_time &&
                field(rows.front(), "duration_ns") == num(17) &&
                string_field(rows.front(), "operation") == "legacy.fixture" &&
                correlation_plot(legacy).find("work.legacy.fixture |") !=
                    std::string::npos,
            "legacy timing metadata is no longer readable after reopen");
    }
    std::filesystem::remove_all(directory);
    return 0;
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  std::filesystem::remove_all(directory);
  return 1;
}
