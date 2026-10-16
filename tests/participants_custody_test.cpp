#include "blackbird/json.hpp"
#include "blackbird/participants.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace blackbird;
namespace {
void check(bool good, const char *message) {
  if (!good)
    throw std::runtime_error{message};
}
void until(const std::atomic_bool &flag) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while (!flag.load()) {
    check(std::chrono::steady_clock::now() < deadline, "schedule barrier expired");
    std::this_thread::yield();
  }
}
Value args() { return Value::object({{"run_id", Value{"run"}}}); }
enum class Scenario { exact, throwing, unknown };
void exercise(Scenario scenario) {
  constexpr std::size_t size = 9 * 1024 * 1024;
  std::string expected(size, 'X');
  for (std::size_t i = 0; i < size; i += 4093)
    expected[i] = static_cast<char>('a' + (i % 26));
  const auto owner = std::this_thread::get_id();
  std::atomic_bool first_entered = false, release_first = false, waiting = false,
                   release_waiting = false, terminal = false;
  bool arm_save = false, fail_retention = scenario == Scenario::throwing;
  unsigned calls = 0, prior_retained = 0, large_attempts = 0, large_retained = 0;
  std::weak_ptr<const ProviderOriginalStorage> observed_storage;
  Participants pool{
      Value{},
      [&](const OriginalCapture &capture) {
        check(std::this_thread::get_id() == owner, "retention left owner thread");
        if (capture.label == "participant.prior") {
          check(capture.bytes == "ordinary original", "prior original changed");
          ++prior_retained;
        }
        if (capture.label == "participant.late") {
          ++large_attempts;
          check(capture.bytes == expected, "late exact original changed");
          if (fail_retention)
            throw Error{ErrorCode::io};
          ++large_retained;
        }
        return std::optional<SourceReference>{};
      },
      [&](const Value &) {
        if (arm_save) {
          arm_save = false;
          release_waiting = true;
          until(terminal);
        }
        return std::optional<ApplicationRecordId>{};
      },
      [&](const Value &activity) {
        const auto phase = string_field(activity, "phase");
        if (phase == "waiting") {
          waiting = true;
          until(release_waiting);
        } else if (phase != "running")
          terminal = true;
      }};
  const auto request = unwrap(parse_json(
      R"({"request_id":"initial","from":"main","to":"alpha","provider":"openai","model":"fake","task":"source review","context":[],"profile":{"name":"context-only","provenance":"test","timeout_seconds":3,"tools":"none","requests":1}})"));
  pool.start(
      "run", Value::object({{"request", request}}),
      [&](const Value &, const ColleagueCapture &, const std::function<bool()> &,
          const RawAdapterContext *context) -> Value {
        if (++calls == 1) {
          auto ticket = unwrap(context->originals->prepare(
              {ProviderOriginalRole::callback_body, ProviderOriginalMode::codec_adopt,
               17, "prior"}));
          check(ticket->complete(
                          ProviderOwnedOriginal{std::string{"ordinary original"},
                                                {},
                                                ProviderOriginalExtentKind::exact_used,
                                                17})
                        .state == ProviderCaptureState::accepted,
                "ordinary original not accepted");
          first_entered = true;
          until(release_first);
          return unwrap(parse_json(
              R"({"status":"completed","model":"actual","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"answer"}]}]})"));
        }
        auto ticket = unwrap(context->originals->prepare(
            {ProviderOriginalRole::callback_body, ProviderOriginalMode::codec_adopt,
             size, "late"}));
        auto storage = std::make_shared<ProviderOriginalStorage>();
        storage->bytes = expected;
        observed_storage = storage;
        const bool unknown = scenario == Scenario::unknown;
        const auto disposition = ticket->complete(ProviderOwnedOriginal{
            std::shared_ptr<const ProviderOriginalStorage>{std::move(storage)},
            {},
            unknown ? ProviderOriginalExtentKind::unknown_used
                    : ProviderOriginalExtentKind::exact_used,
            unknown ? std::nullopt : std::optional<std::size_t>{size}});
        check(disposition.state == ProviderCaptureState::overflow,
              "late original did not use emergency owner");
        throw Error{ErrorCode::capacity};
      });
  until(first_entered);
  pool.send(Value::object({{"run_id", Value{"run"}},
                           {"message_id", Value{"next"}},
                           {"from", Value{"operator"}},
                           {"text", Value{"next direction"}}}));
  release_first = true;
  until(waiting);
  arm_save = true;
  std::optional<Error> failure;
  try {
    pool.archive(args());
  } catch (const Error &error) {
    failure = error;
  }
  check(calls == 2 && prior_retained == 1 && !pool.active(),
        "prior original or exact transport count changed");
  if (scenario == Scenario::exact) {
    check(!failure && large_retained == 1 && large_attempts == 1,
          "archive destroyed late original without retaining exact bytes once");
    check(field(pool.read(), "runs").array().empty() && !pool.debt(),
          "successful archive left row or debt");
    check(observed_storage.expired(), "retained extent owner was not released");
  } else {
    check(failure &&
              failure->code == (scenario == Scenario::throwing ? ErrorCode::io
                                                               : ErrorCode::incomplete),
          "archive falsely succeeded with unresolved late original");
    check(pool.debt() && !observed_storage.expired(),
          "failed archive discarded original owner or debt");
    pool.shutdown();
    check(pool.debt() && !observed_storage.expired(),
          "native shutdown discarded semantic debt");
    if (scenario == Scenario::throwing) {
      check(large_attempts == 1 && large_retained == 0,
            "failed retention unexpectedly retried");
      fail_retention = false;
      pool.drain();
      check(large_attempts == 2 && large_retained == 1,
            "explicit drain lost or duplicated original");
      check(string_field(pool.read(args()), "run_id") == "run",
            "failed archive removed run");
      pool.archive(args());
      check(field(pool.read(), "runs").array().empty() && !pool.debt(),
            "resolved archive did not finish");
      check(observed_storage.expired(), "drained original owner survived");
    } else {
      bool incomplete = false;
      try {
        pool.archive(args());
      } catch (const Error &error) {
        incomplete = error.code == ErrorCode::incomplete;
      }
      check(incomplete && pool.debt() && !observed_storage.expired() &&
                large_attempts == 0,
            "unknown-used original was retained, erased or falsely resolved");
    }
  }
  pool.shutdown();
  check(calls == 2 && prior_retained == 1, "cleanup retried worker or prior original");
}
} // namespace
int main(int argc, char **argv) {
  try {
    const std::string_view selected = argc == 2 ? argv[1] : "all";
    for (const auto &[name, scenario] : {std::pair{"exact", Scenario::exact},
                                         {"throwing", Scenario::throwing},
                                         {"unknown", Scenario::unknown}})
      if (selected == "all" || selected == name) {
        exercise(scenario);
        std::cout << name << ": exact custody assertions passed\n";
      }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << '\n';
    return 1;
  }
}
