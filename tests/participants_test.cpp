#include "blackbird/json.hpp"
#include "blackbird/participants.hpp"
#include "blackbird/tools.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
using namespace blackbird;
void check(bool good) {
  if (!good)
    throw Error{ErrorCode::corrupt};
}
Value request(const std::string &who) {
  auto value = unwrap(parse_json(
      R"({"request_id":"initial","from":"main","to":"alpha","provider":"openai","model":"fake","task":"source review","context":[{"id":"s1","text":"selected"}],"profile":{"name":"context-only","provenance":"test","timeout_seconds":3,"tools":"none","requests":1}})"));
  for (auto &[name, item] : value.object())
    if (name == "to")
      item = Value{who};
  return value;
}
Value args(std::string id) { return Value::object({{"run_id", Value{std::move(id)}}}); }
Value send(std::string run, std::string message, std::string text = "direction") {
  return Value::object({{"run_id", Value{std::move(run)}},
                        {"message_id", Value{std::move(message)}},
                        {"from", Value{"operator"}},
                        {"text", Value{std::move(text)}}});
}
int main() {
  try {
    const auto owner = std::this_thread::get_id();
    Value saved;
    std::vector<std::string> admissions;
    std::mutex gate;
    std::condition_variable cv;
    int first_calls = 0, total_calls = 0;
    bool alpha_release = false, beta_release = false;
    Participants pool{Value{},
                      [&](const OriginalCapture &capture) {
                        const auto label = capture.label, raw = capture.bytes;
                        check(std::this_thread::get_id() == owner && !label.empty() &&
                              !raw.empty());
                        if (label == "participant.admission")
                          admissions.emplace_back(raw);
                        return std::optional<SourceReference>{};
                      },
                      [&](const Value &packet) {
                        check(std::this_thread::get_id() == owner);
                        saved = packet;
                        return std::optional<ApplicationRecordId>{};
                      }};
    const ParticipantTransport transport =
        [&](const Value &prepared, const ColleagueCapture &capture,
            const std::function<bool()> &cancelled, const RawAdapterContext *) {
          const auto &r = field(prepared, "request");
          const auto who = string_field(r, "to");
          const auto delivery = string_field(r, "request_id");
          {
            std::unique_lock lock{gate};
            ++total_calls;
            if (delivery == "initial") {
              ++first_calls;
              cv.notify_all();
              const auto deadline =
                  std::chrono::steady_clock::now() + std::chrono::seconds{4};
              check(cv.wait_until(lock, deadline, [&] {
                return who == "alpha" ? alpha_release : beta_release;
              }));
            }
          }
          if (cancelled())
            throw Error{ErrorCode::interrupted};
          if (delivery != "initial")
            check(string_field(r, "task").find("direction") != std::string::npos);
          capture("raw", "captured provider bytes");
          const auto text = Value::object(
              {{"type", Value{"output_text"}}, {"text", Value{"answer:" + delivery}}});
          const auto message = Value::object({{"type", Value{"message"}},
                                              {"role", Value{"assistant"}},
                                              {"content", Value{Value::Array{text}}}});
          return Value::object({{"status", Value{"completed"}},
                                {"model", Value{"actual"}},
                                {"output", Value{Value::Array{message}}}});
        };
    pool.start("r1", Value::object({{"request", request("alpha")}}), transport);
    pool.start("r2", Value::object({{"request", request("beta")}}), transport);
    {
      std::unique_lock lock{gate};
      check(
          cv.wait_for(lock, std::chrono::seconds{2}, [&] { return first_calls == 2; }));
    }
    check(admissions.size() == 2 && pool.active());
    bool cap = false;
    try {
      pool.start("r3", Value::object({{"request", request("third")}}), transport);
    } catch (const Error &e) {
      cap = e.code == ErrorCode::capacity;
    }
    check(cap && admissions.size() == 2);
    auto timed_args = args("r2");
    timed_args.object().emplace_back("timeout_ms", Value{Number{"1"}});
    check(field(pool.await(timed_args), "await_timed_out") == Value{true});
    const auto crash_snapshot = saved;
    check(field(pool.send(send("r1", "d1")), "accepted") == Value{true});
    check(field(pool.send(send("r1", "d1")), "duplicate") == Value{true});
    auto reordered = send("r1", "d1");
    std::reverse(reordered.object().begin(), reordered.object().end());
    check(field(pool.send(reordered), "duplicate") == Value{true});
    bool conflict = false;
    try {
      pool.send(send("r1", "d1", "different"));
    } catch (const Error &e) {
      conflict = e.code == ErrorCode::conflict;
    }
    check(conflict);
    for (int i = 0; i < 16; ++i)
      pool.send(send("r2", "queued" + std::to_string(i)));
    cap = false;
    try {
      pool.send(send("r2", "overflow"));
    } catch (const Error &e) {
      cap = e.code == ErrorCode::capacity;
    }
    check(cap);
    pool.cancel(args("r2"));
    const auto requested = pool.read(args("r2"));
    check(field(requested, "cancel_requested") == Value{true} &&
          string_field(requested, "state") == "running");
    {
      std::lock_guard lock{gate};
      alpha_release = true;
      beta_release = true;
    }
    cv.notify_all();
    const auto joined = pool.join(
        Value::object({{"run_ids", Value{Value::Array{Value{"r2"}, Value{"r1"}}}}}));
    const auto &rows = field(joined, "runs").array();
    check(string_field(rows[0], "run_id") == "r2" &&
          string_field(rows[0], "state") == "unknown");
    check(string_field(rows[1], "run_id") == "r1" &&
          string_field(rows[1], "state") == "completed");
    check(string_field(field(rows[1], "result"), "text") == "answer:initial:d1" &&
          !pool.active());
    check(total_calls == 3); // Accepted beta directions were cancelled before dispatch.
    Participants reopened{
        crash_snapshot,
        [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
        [](const Value &) { return std::optional<ApplicationRecordId>{}; }};
    check(!reopened.active() &&
          string_field(reopened.read(args("r1")), "state") == "unknown");
    check(string_field(reopened.read(args("r2")), "state") == "unknown" &&
          total_calls == 3);
    pool.archive(args("r1"));
    pool.configure(Value::object({{"concurrency", Value{Number{"3"}}}}));
    check(field(pool.read(), "runs").array().size() == 1);
    Participants bounded_capture{
        Value{},
        [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
        [](const Value &) { return std::optional<ApplicationRecordId>{}; }};
    bounded_capture.start(
        "large", Value::object({{"request", request("large")}}),
        [](const Value &, const ColleagueCapture &capture,
           const std::function<bool()> &, const RawAdapterContext *) -> Value {
          capture("raw", "synchronous preview");
          throw Error{ErrorCode::capacity}; // actual acquired-source overflow covered
                                            // in raw fixture
        });
    const auto large = bounded_capture.join(
        Value::object({{"run_ids", Value{Value::Array{Value{"large"}}}}}));
    check(string_field(field(large, "runs").array()[0], "state") == "unknown");
    Participants failed_snapshot{
        Value{},
        [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
        [](const Value &) -> std::optional<ApplicationRecordId> {
          throw Error{ErrorCode::io};
        }};
    bool failed_before_launch = false;
    int launches = 0;
    try {
      failed_snapshot.start("no-save", Value::object({{"request", request("no-save")}}),
                            [&](const Value &, const ColleagueCapture &,
                                const std::function<bool()> &,
                                const RawAdapterContext *) {
                              ++launches;
                              return Value{};
                            });
    } catch (const Error &error) {
      failed_before_launch = error.code == ErrorCode::io;
    }
    check(failed_before_launch && !failed_snapshot.active() && launches == 0);
    int blocked_dispatches = 0;
    Participants failed{
        Value{},
        [](const OriginalCapture &) -> std::optional<SourceReference> {
          throw Error{ErrorCode::io};
        },
        [](const Value &) { return std::optional<ApplicationRecordId>{}; }};
    bool refused = false;
    try {
      failed.start("blocked", Value::object({{"request", request("blocked")}}),
                   [&](const Value &, const ColleagueCapture &,
                       const std::function<bool()> &, const RawAdapterContext *) {
                     ++blocked_dispatches;
                     return Value{};
                   });
    } catch (const Error &e) {
      refused = e.code == ErrorCode::io;
    }
    check(refused && blocked_dispatches == 0 && !failed.active());
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << '\n';
    return 1;
  }
}
