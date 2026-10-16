#include "blackbird/command_jobs.hpp"
#include "blackbird/tools.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <termios.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
using Path = std::filesystem::path;
constexpr std::size_t chunk = 65536, history = 16 * 1024 * 1024;
void check(bool good, const char *message) {
  if (!good)
    throw std::runtime_error{message};
}
template <class F> void until(F predicate, const char *message) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!predicate()) {
    check(std::chrono::steady_clock::now() < deadline, message);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
}
void file(const Path &path, std::string_view value) {
  std::ofstream stream{path};
  stream << value;
  check(static_cast<bool>(stream), "fixture write failed");
}
std::size_t count(const Path &root) {
  std::ifstream stream{root / "count"};
  std::size_t result = 0;
  stream >> result;
  return result;
}
int child(const Path &root) {
  termios mode{};
  check(::tcgetattr(0, &mode) == 0, "child PTY attributes unavailable");
  ::cfmakeraw(&mode);
  check(::tcsetattr(0, TCSANOW, &mode) == 0, "child raw mode failed");
  file(root / "ready", "ready");
  until([&] { return std::filesystem::exists(root / "read"); }, "reader gate expired");
  std::array<char, 8192> bytes{};
  std::size_t received = 0, published = 0;
  for (;;) {
    const auto size = ::read(0, bytes.data(), bytes.size());
    if (size <= 0)
      return 0;
    for (std::ptrdiff_t i = 0; i < size; ++i)
      check(bytes[static_cast<std::size_t>(i)] == 'A', "child received changed bytes");
    received += static_cast<std::size_t>(size);
    // Wake the collector's normal output poll after each real read, rather
    // than measuring its idle10ms cadence across16 MiB of PTY writes.
    check(::write(1, ".", 1) == 1, "child read acknowledgement failed");
    if (received / chunk > published / chunk) {
      file(root / "count-next", std::to_string(received));
      std::filesystem::rename(root / "count-next", root / "count");
      published = received;
    }
  }
}
struct Fixture {
  Path root;
  CommandJobs jobs;
  const std::string id = std::string(32, 'a'), other = std::string(32, 'b');
  unsigned serial = 0;
  explicit Fixture(const Path &executable, bool reading = true, bool tight = false) {
    auto name = (std::filesystem::temp_directory_path() / "bb-mailbox-XXXXXX").string();
    const auto created = ::mkdtemp(name.data());
    check(created != nullptr, "create fixture directory");
    root = created;
    if (tight)
      (void)jobs.control(Value::object({{"op", Value{"configure"}},
                                        {"max_live", Value{Number{1}}},
                                        {"max_records", Value{Number{1}}}}));
    if (reading)
      file(root / "read", "read");
    jobs.start(
        id, Value::object(
                {{"argv", Value{Value::Array{Value{executable.string()},
                                             Value{"--child"}, Value{root.string()}}}},
                 {"pty", Value{true}},
                 {"timeout_seconds", Value{Number{30}}}}));
    until([&] { return std::filesystem::exists(root / "ready"); }, "child not ready");
  }
  ~Fixture() {
    jobs.shutdown();
    if (!std::uncaught_exceptions()) {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    } else
      std::cerr << "retained fixture " << root << '\n';
  }
  Value input(std::string request, std::size_t bytes = 0,
              const std::string &job = {}) const {
    return Value::object({{"op", Value{"input"}},
                          {"job_id", Value{job.empty() ? id : job}},
                          {"request_id", Value{std::move(request)}},
                          {"bytes", Value{std::string(bytes, 'A')}}});
  }
  void filled_ids(unsigned size) {
    for (unsigned i = 0; i < size; ++i)
      (void)jobs.control(input("id-" + std::to_string(i)));
  }
  void filled_bytes(std::size_t bytes) {
    for (std::size_t at = 0; at < bytes; at += chunk) {
      const auto request = input("bytes-" + std::to_string(++serial), chunk);
      (void)jobs.control(request);
      const auto expected = at + chunk;
      until([&] { return count(root) >= expected; },
            "reader did not consume exact input");
      check(string_field(jobs.control(request), "status") == "delivered",
            "read original was not delivered");
    }
  }
  void refused(const Value &request) {
    bool capacity = false;
    try {
      jobs.request(request);
    } catch (const Error &error) {
      capacity = error.code == ErrorCode::capacity;
    }
    check(capacity, "permanent mailbox budget accepted an undeliverable control");
  }
  void duplicates(const Value &request) {
    jobs.request(request);
    const auto batch = jobs.requests();
    check(batch.size() == 1, "exact duplicate was deferred at permanent limit");
    check(field(jobs.control(batch.front()), "duplicate") == Value{true},
          "exact retained duplicate was not replayed");
    auto conflict = request;
    for (auto &[key, value] : conflict.object())
      if (key == "bytes")
        value = Value{"different"};
    bool conflicting = false;
    try {
      jobs.request(conflict);
    } catch (const Error &error) {
      conflicting = error.code == ErrorCode::conflict;
    }
    check(conflicting, "conflicting mailbox identity was accepted");
  }
  void urgent() {
    jobs.request(Value::object({{"op", Value{"background"}}, {"job_id", Value{id}}}));
    jobs.request(Value::object({{"op", Value{"stop"}}, {"job_id", Value{id}}}));
    const auto batch = jobs.requests();
    check(batch.size() == 2 && string_field(batch[0], "op") == "background" &&
              string_field(batch[1], "op") == "stop",
          "permanent delivery history stranded urgent controls");
    for (const auto &request : batch)
      (void)jobs.control(request);
  }
};
void identity(const Path &executable) {
  Fixture f{executable};
  f.filled_ids(4096);
  f.refused(f.input("overflow", 1));
  f.duplicates(f.input("id-0"));
  check(f.jobs.requests().empty() && f.jobs.active(),
        "refusal queued work or killed job");
  f.urgent();
}
void byte_history(const Path &executable) {
  Fixture f{executable};
  f.filled_bytes(history);
  f.refused(f.input("overflow", 1));
  f.duplicates(f.input("bytes-1", chunk));
  check(count(f.root) == history && f.jobs.active(),
        "history refusal changed prior bytes");
  f.jobs.request(Value::object({{"op", Value{"signal"}},
                                {"job_id", Value{f.id}},
                                {"request_id", Value{"no-input-bytes"}},
                                {"signal", Value{"continue"}}}));
  const auto selected = f.jobs.requests();
  check(selected.size() == 1, "input byte history refused a zero-byte signal");
  (void)f.jobs.control(selected.front());
  f.urgent();
}
void reservations(const Path &executable, bool bytes) {
  Fixture f{executable};
  if (bytes)
    f.filled_bytes(history - chunk);
  else
    f.filled_ids(4094);
  const auto first = f.input("promised-first", bytes ? chunk / 2 : 0),
             second = f.input("promised-second", bytes ? chunk / 2 : 0);
  f.jobs.request(first);
  f.jobs.request(first); // Same promise consumes one identity/byte charge.
  f.jobs.request(second);
  const auto conflicting = f.input("promised-first", 1);
  bool conflict = false;
  try {
    f.jobs.request(conflicting);
  } catch (const Error &error) {
    conflict = error.code == ErrorCode::conflict;
  }
  check(conflict, "conflicting queued reservation was accepted");
  conflict = false;
  try {
    (void)f.jobs.control(conflicting);
  } catch (const Error &error) {
    conflict = error.code == ErrorCode::conflict;
  }
  check(conflict, "conflicting direct call withdrew a mailbox promise");
  f.refused(f.input("queued-overflow", 1));
  const auto selected = f.jobs.requests();
  check(selected.size() == 3, "reserved controls did not select together");
  f.refused(f.input("selected-overflow", 1));
  bool direct_capacity = false;
  try {
    (void)f.jobs.control(f.input("direct-overflow", 1));
  } catch (const Error &error) {
    direct_capacity = error.code == ErrorCode::capacity;
  }
  check(direct_capacity, "direct control consumed a selected mailbox promise");
  unsigned duplicate = 0;
  for (const auto &request : selected)
    duplicate += field(f.jobs.control(request), "duplicate") == Value{true};
  check(duplicate == 1 && f.jobs.requests().empty(), "reserved duplicate wrote twice");
  if (bytes)
    until([&] { return count(f.root) == history; },
          "promised exact bytes did not arrive");
  if (!bytes) {
    Fixture finished{executable, true, true};
    finished.jobs.request(finished.input("selected-before-exit"));
    const auto pending = finished.jobs.requests();
    check(pending.size() == 1, "terminal custody fixture did not select control");
    (void)finished.jobs.control(
        Value::object({{"op", Value{"stop"}}, {"job_id", Value{finished.id}}}));
    const auto terminal = finished.jobs.wait(
        finished.id, {},
        [&] {
          for (const auto &event : finished.jobs.drain())
            if (event.bytes)
              finished.jobs.retained(event.job_id, event.offset + event.bytes->size());
            else
              finished.jobs.settled(event.job_id, event.result);
        },
        [] { return false; });
    check(terminal.find("termination") && finished.jobs.debt() == 1,
          "terminal retention erased undispatched control debt");
    bool busy = false;
    try {
      (void)finished.jobs.control(
          Value::object({{"op", Value{"archive"}}, {"job_id", Value{finished.id}}}));
    } catch (const Error &error) {
      busy = error.code == ErrorCode::busy;
    }
    check(busy, "archive erased selected control owner");
    const auto next = Value::object(
        {{"argv", Value{Value::Array{Value{"/bin/sleep"}, Value{"30"}}}}});
    CommandStartStatus status;
    bool capacity = false;
    try {
      finished.jobs.start(finished.other, next, nullptr, &status);
    } catch (const Error &error) {
      capacity = error.code == ErrorCode::capacity;
    }
    check(capacity &&
              (status.refusal == CommandStartStatus::Refusal::live_limit ||
               status.refusal == CommandStartStatus::Refusal::resident_limit) &&
              !status.worker_started && finished.jobs.contains(finished.id),
          "automatic retirement erased selected control owner");
    busy = false;
    try {
      (void)finished.jobs.control(pending.front());
    } catch (const Error &error) {
      busy = error.code == ErrorCode::busy;
    }
    check(busy && finished.jobs.debt() == 0,
          "explicit terminal refusal did not release selected promise");
    finished.jobs.start(finished.other, next);
    check(!finished.jobs.contains(finished.id),
          "resolved selected control prevented normal retirement");
  }
}
void transient(const Path &executable) {
  Fixture f{executable, false};
  const auto first = f.input("blocked", chunk);
  (void)f.jobs.control(first);
  until(
      [&] {
        const auto result = f.jobs.control(first);
        const auto written =
            std::stoull(field(result, "written_bytes").number().text());
        return written > 0 && written < chunk;
      },
      "did not observe partially written real PTY input");
  f.jobs.request(f.input("pending", chunk));
  check(f.jobs.requests().empty(), "transient input budget was oversubscribed");
  f.jobs.start(
      f.other,
      Value::object({{"argv", Value{Value::Array{Value{"/bin/sleep"}, Value{"30"}}}},
                     {"pty", Value{true}}}));
  f.jobs.request(f.input("unrelated", 0, f.other));
  f.jobs.request(Value::object({{"op", Value{"background"}}, {"job_id", Value{f.id}}}));
  auto selected = f.jobs.requests();
  check(selected.size() == 2, "transient wait blocked unrelated or urgent work");
  for (const auto &request : selected)
    (void)f.jobs.control(request);
  file(f.root / "read", "read");
  until(
      [&] {
        selected = f.jobs.requests();
        return !selected.empty();
      },
      "pending input did not progress after reader release");
  check(selected.size() == 1 &&
            string_field(selected.front(), "request_id") == "pending",
        "wrong deferred input selected");
  f.jobs.request(f.input("after-selected", chunk));
  check(f.jobs.requests().empty(),
        "later batch consumed selected pending-input reservation");
  bool capacity = false;
  try {
    (void)f.jobs.control(f.input("direct-after-selected", 1));
  } catch (const Error &error) {
    capacity = error.code == ErrorCode::capacity;
  }
  check(capacity, "direct input consumed selected pending-input reservation");
  (void)f.jobs.control(selected.front());
  until([&] { return count(f.root) == 2 * chunk; },
        "deferred bytes lost or duplicated");
  until(
      [&] {
        selected = f.jobs.requests();
        return !selected.empty();
      },
      "later batch did not progress after selected input drained");
  check(selected.size() == 1 &&
            string_field(selected.front(), "request_id") == "after-selected",
        "later pending reservation changed identity");
  (void)f.jobs.control(selected.front());
  until([&] { return count(f.root) == 3 * chunk; },
        "later reserved bytes lost or duplicated");
  f.jobs.request(Value::object({{"op", Value{"stop"}}, {"job_id", Value{f.id}}}));
  selected = f.jobs.requests();
  check(selected.size() == 1 && string_field(selected.front(), "op") == "stop",
        "urgent stop was stranded");
  (void)f.jobs.control(selected.front());
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc == 3 && std::string_view{argv[1]} == "--child")
      return child(argv[2]);
    check(argc == 2, "select identity/history/reserved_ids/reserved_bytes/transient");
    const auto executable = std::filesystem::canonical(argv[0]);
    const std::string_view mode = argv[1];
    if (mode == "identity")
      identity(executable);
    else if (mode == "history")
      byte_history(executable);
    else if (mode == "reserved_ids" || mode == "reserved_bytes")
      reservations(executable, mode == "reserved_bytes");
    else if (mode == "transient")
      transient(executable);
    else
      check(false, "unknown mailbox test case");
    std::cout << mode << ": mailbox ownership/progress assertions passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << '\n';
    return 1;
  }
}
