#include "blackbird/command_jobs.hpp"
#include "blackbird/raw_command.hpp"
#include "blackbird/tools.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <dlfcn.h>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char **environ;
using namespace blackbird;
namespace {
std::atomic<unsigned> resource_normalizations{0};
std::atomic<int> resource_limit_error{0};
std::atomic<bool> resource_descriptor_ready{false};
std::atomic<int> resource_descriptor_result{0}, resource_descriptor_error{0};
struct rlimit selected_resource_limit{};
struct rlimit restored_resource_limit{};
using Path = std::filesystem::path;
void require(bool value, std::string_view claim) {
  if (!value)
    throw std::runtime_error(std::string{claim});
}
std::string job_id(unsigned n) {
  auto value = std::to_string(n);
  return std::string(32 - value.size(), '0') + value;
}
struct Temporary {
  Path root;
  Temporary() {
    auto name =
        (std::filesystem::temp_directory_path() / "bb-job-test-XXXXXX").string();
    const auto created = ::mkdtemp(name.data());
    require(created != nullptr, "create private fixture directory");
    root = created;
  }
  ~Temporary() {
    if (std::uncaught_exceptions()) {
      std::cerr << "retained failed native fixture " << root << '\n';
      return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};
void release(const Path &path) { write_file(path, "release"); }
std::string contents(const Path &path) { return read_file(path); }
struct Fixture {
  Path executable;
  Path root;
};
struct LaunchOptions {
  std::string mode;
  bool pty = false;
  unsigned seconds = 5;
};
Value arguments(const Fixture &fixture, const LaunchOptions &options) {
  return Value::object(
      {{"argv", Value{Value::Array{Value{fixture.executable.string()},
                                   Value{options.mode}, Value{fixture.root.string()}}}},
       {"pty", Value{options.pty}},
       {"timeout_seconds", Value{Number{options.seconds}}}});
}
Fixture fixture(Fixture settings) {
  std::filesystem::create_directory(settings.root);
  return settings;
}
struct ControlOptions {
  std::string op;
  std::string request;
  std::string bytes{};
};
struct Evidence {
  CommandJobs jobs;
  std::map<std::string, std::string> originals;
  std::map<std::string, unsigned> terminals;
  std::map<std::string, Path> roots;
  void start(const std::string &id, const Fixture &child,
             const LaunchOptions &options) {
    roots[id] = child.root;
    jobs.start(id, arguments(child, options));
  }
  void pump() {
    for (auto &event : jobs.drain()) {
      if (event.bytes) {
        require(terminals[event.job_id] == 0, "no captured bytes after terminal event");
        auto &original = originals[event.job_id];
        require(event.offset == original.size(),
                "capture offsets conserve exact bytes");
        original += *event.bytes;
        jobs.retained(event.job_id, event.offset + event.bytes->size());
      } else {
        require(++terminals[event.job_id] == 1, "one terminal event per launch");
        jobs.settled(event.job_id, event.result);
      }
    }
  }
  Value wait(const std::string &id, std::optional<std::uint64_t> ms = {}) {
    return jobs.wait(id, {ms, 65536, false}, [&] { pump(); }, [] { return false; });
  }
  Value control(const std::string &id, const ControlOptions &options) {
    auto request = Value::object({{"op", Value{options.op}},
                                  {"job_id", Value{id}},
                                  {"request_id", Value{options.request}}});
    if (options.op == "input")
      request.object().emplace_back("bytes", Value{options.bytes});
    return jobs.control(request);
  }
  void until(const std::string &id, const std::function<bool()> &predicate,
             std::string_view milestone) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{4};
    while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        const auto state = jobs.read(Value::object({{"job_id", Value{id}}}));
        throw std::runtime_error(
            "fixture handshake bounded: " + roots.at(id).string() + " milestone=" +
            std::string{milestone} + " state=" + string_field(state, "state") +
            " received=" + field(state, "received_bytes").number().text());
      }
      (void)wait(id, 10);
    }
  }
};
void expect_exit(const Value &result, std::string_view expected) {
  const auto *status = result.find("exit_code");
  require(status != nullptr, "terminal result must contain independently known exit");
  const auto actual = status->number().text();
  if (actual != expected)
    throw std::runtime_error("child exit expected " + std::string{expected} +
                             "; observed " + actual);
}
long metadata(const Path &path, std::string_view key) {
  std::ifstream input{path};
  std::string item;
  const auto prefix = std::string{key} + "=";
  while (std::getline(input, item))
    if (item.starts_with(prefix))
      return std::stol(item.substr(prefix.size()));
  throw std::runtime_error("missing fixture identity field: " + std::string{key});
}
int closed_stdio(const Fixture &settings) {
  const auto &executable = settings.executable;
  const auto &root = settings.root;
  try {
    Evidence e;
    for (const bool pty : {false, true}) {
      const auto child =
          fixture({executable, root / (pty ? "pty-child" : "pipe-child")});
      const auto id = job_id(pty ? 91 : 90);
      e.start(id, child, {"closed-stdio", pty});
      expect_exit(e.wait(id), "0");
      require(e.originals[id] == (pty ? "STDIO\r\n" : "STDIO\n"),
              "closed parent stdio normalizes child output");
      require(contents(child.root / "stdin-valid") == "yes",
              "closed parent stdio still gives child valid stdin");
      require(contents(child.root / "launches") == "L", "closed stdio launches once");
    }
    e.jobs.shutdown();
    return 0;
  } catch (...) {
    return 1;
  }
}
int resource_failure(const Fixture &settings) {
  const auto &executable = settings.executable;
  const auto &root = settings.root;
  try {
    Evidence e;
    const auto child = fixture({executable, root / "resource-child"});
    struct rlimit original{};
    require(::getrlimit(RLIMIT_NOFILE, &original) == 0, "read fd resource bound");
    struct Restore {
      struct rlimit original;
      ~Restore() { (void)::setrlimit(RLIMIT_NOFILE, &original); }
    } restore{original};
    const auto id = job_id(92);
    const auto request = arguments(child, {"gate"});
    e.roots[id] = child.root;
    auto exhausted = original;
    // CTest may inherit its log descriptor in addition to stdio. Leave two
    // slots for sanitizer probes; complete normalized exec pipes cannot fit.
    exhausted.rlim_cur = 6;
    require(::setrlimit(RLIMIT_NOFILE, &exhausted) == 0, "exhaust new fd allocation");
    int probe[2];
    require(::pipe(probe) == 0, "sanitizer has two descriptor slots before refusal");
    (void)::close(probe[0]);
    (void)::close(probe[1]);
    bool predispatch_refused = false;
    try {
      RawCommandWake wake{RawCommandWake::Mode::one_shot};
    } catch (const Error &error) {
      predispatch_refused = error == Error{ErrorCode::io, EMFILE};
    }
    require(
        predispatch_refused && !std::filesystem::exists(child.root / "launches"),
        "native wake allocation refusal occurs before dispatch without child effects");
    require(::setrlimit(RLIMIT_NOFILE, &original) == 0,
            "restore capacity for native wake preparation");
    selected_resource_limit = exhausted;
    restored_resource_limit = original;
    resource_normalizations.store(2);
    e.jobs.start(id, request);
    e.jobs.shutdown(); // Join and release temporary descriptors before observations.
    require(resource_normalizations.load() == 0 && !resource_descriptor_ready.load() &&
                resource_limit_error.load() == 0 &&
                resource_descriptor_result.load() == -1 &&
                resource_descriptor_error.load() == EMFILE,
            "actual descriptor normalization fails under the real bound after both "
            "wake endpoints");
    require(::setrlimit(RLIMIT_NOFILE, &original) == 0, "restore fd resource bound");
    const auto result = e.wait(id);
    require(result.find("error") != nullptr &&
                string_field(result, "effect_outcome") == "unknown" &&
                !result.find("exit_code") &&
                field(result, "error_detail") == Value{Number{EMFILE}} &&
                e.terminals[id] == 1,
            "fd allocation failure settles once without fabricating child exit");
    require(!std::filesystem::exists(child.root / "launches") && !e.jobs.active(),
            "allocation failure has zero independent launch effects or custody debt");
    e.jobs.shutdown();
    return 0;
  } catch (const Error &error) {
    std::cerr << "resource child native error " << static_cast<unsigned>(error.code)
              << " detail " << error.detail << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "resource child: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "resource child: unexpected non-native failure\n";
    return 1;
  }
}
void check_subprocess(const Path &test, const Fixture &child, std::string mode) {
  auto executable = test.string();
  auto root = child.root.string();
  std::array<char *, 4> argv{executable.data(), mode.data(), root.data(), nullptr};
  posix_spawn_file_actions_t actions;
  require(::posix_spawn_file_actions_init(&actions) == 0, "closed stdio spawn setup");
  if (mode == "--closed-stdio")
    for (int fd = 0; fd < 3; ++fd)
      require(::posix_spawn_file_actions_addclose(&actions, fd) == 0,
              "close parent stdio in independent test process");
  pid_t pid = -1;
  const auto launched =
      ::posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv.data(), environ);
  (void)::posix_spawn_file_actions_destroy(&actions);
  require(launched == 0, "spawn independent closed-stdio test");
  int status{};
  pid_t reaped;
  do {
    reaped = ::waitpid(pid, &status, 0);
  } while (reaped < 0 && errno == EINTR);
  require(reaped == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "independent " + mode + " child byte/endpoint oracle passes");
}
} // namespace
extern "C" int fcntl(int descriptor, int command, ...) {
  using Native = int (*)(int, int, ...);
  static auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "fcntl"));
  if (!native)
    std::abort();
  const bool no_argument = command == F_GETFL || command == F_GETFD ||
                           command == F_GETOWN
#if defined(__APPLE__)
                           || command == F_FULLFSYNC
#endif
      ;
  int argument = 0;
  if (!no_argument) {
    va_list arguments;
    va_start(arguments, command);
    argument = va_arg(arguments, int);
    va_end(arguments);
  }
  const bool selected = command == F_DUPFD_CLOEXEC && argument == 4 &&
                        resource_descriptor_ready.exchange(false);
  if (selected && ::setrlimit(RLIMIT_NOFILE, &selected_resource_limit) != 0)
    resource_limit_error.store(errno);
  const int result =
      no_argument ? native(descriptor, command) : native(descriptor, command, argument);
  const int error = errno;
  if (selected) {
    resource_descriptor_result.store(result);
    resource_descriptor_error.store(error);
    // Keep the genuine native refusal, then return descriptor headroom before
    // C++ diagnostics whose memory probes themselves need a native pipe.
    if (::setrlimit(RLIMIT_NOFILE, &restored_resource_limit) != 0)
      resource_limit_error.store(errno);
  }
  if (command == F_SETFL && result == 0 && resource_normalizations.load() != 0 &&
      resource_normalizations.fetch_sub(1) == 1)
    resource_descriptor_ready.store(true);
  errno = error;
  return result;
}
int main(int argc, char **argv) {
  try {
    const auto test = std::filesystem::absolute(argv[0]);
    const auto executable = test.parent_path() / "command_job_fixture";
    require(std::filesystem::is_regular_file(executable), "owned child fixture exists");
    if (argc == 3 && std::string_view{argv[1]} == "--closed-stdio")
      return closed_stdio({executable, Path{argv[2]}});
    if (argc == 3 && std::string_view{argv[1]} == "--resource-failure")
      return resource_failure({executable, Path{argv[2]}});
    Temporary temporary;
    Evidence e;
    const auto first = fixture({executable, temporary.root / "identity"});
    const auto a = job_id(1);
    e.start(a, first, {"identity"});
    require(!e.wait(a, 0).find("exit_code"), "yield cannot fabricate exit status");
    e.until(
        a,
        [&] {
          return std::filesystem::exists(first.root / "ready") &&
                 e.originals[a].ends_with(std::string_view{"\xe2", 1});
        },
        "identity first split byte");
    const auto pid = metadata(first.root / "identity", "pid");
    require(pid > 0 && metadata(first.root / "identity", "pgid") == pid,
            "ordinary pipe child has isolated process group");
    const auto prefix = "PID:" + std::to_string(pid) + "\n";
    require(e.originals[a] == prefix + std::string{"\xe2", 1},
            "first incomplete UTF8 fragment is retained without normalization");
    for (unsigned n = 0; n < 8; ++n)
      require(!e.wait(a, 0).find("exit_code"), "repeated yields preserve live command");
    release(first.root / "one");
    e.until(
        a,
        [&] {
          return std::filesystem::exists(first.root / "middle") &&
                 e.originals[a].ends_with(std::string_view{"\xe2\x82", 2});
        },
        "identity second split byte");
    release(first.root / "two");
    expect_exit(e.wait(a), "7");
    require(e.originals[a] == prefix + std::string{"\xe2\x82\xac\0\nZ", 6},
            "split UTF8 and embedded NUL conserved exactly across waits");
    require(contents(first.root / "launches") == "L",
            "external launch counter proves foreground never relaunches");
    require(e.terminals[a] == 1 && !e.jobs.active(), "one settled launch without debt");

    const auto terminal = fixture({executable, temporary.root / "pty"});
    const auto b = job_id(2);
    e.start(b, terminal, {"pty", true});
    e.until(
        b, [&] { return std::filesystem::exists(terminal.root / "ready"); },
        "PTY ready");
    const auto terminal_pid = metadata(terminal.root / "identity", "pid");
    require(terminal_pid == metadata(terminal.root / "identity", "pgid") &&
                terminal_pid == metadata(terminal.root / "identity", "sid") &&
                terminal_pid == metadata(terminal.root / "identity", "foreground") &&
                metadata(terminal.root / "identity", "tty") == 1,
            "private PTY owns its session, group and terminal foreground");
    const auto input = e.control(b, {"input", "once", "hello\n"});
    const auto duplicate = e.control(b, {"input", "once", "hello\n"});
    require(string_field(input, "request_id") == string_field(duplicate, "request_id"),
            "input request identity stable");
    bool conflict = false;
    try {
      (void)e.control(b, {"input", "once", "different\n"});
    } catch (const Error &error) {
      conflict = error.code == ErrorCode::conflict;
    }
    require(conflict, "changed duplicate conflicts before effect");
    e.until(
        b, [&] { return std::filesystem::exists(terminal.root / "first-read"); },
        "PTY first line consumed");
    const auto first_read = contents(terminal.root / "first-read");
    require(first_read == "hello", "first exact input; observed " +
                                       std::to_string(first_read.size()) + " bytes [" +
                                       first_read + "]");
    (void)e.jobs.control(Value::object({{"op", Value{"resize"}},
                                        {"job_id", Value{b}},
                                        {"request_id", Value{"resize-once"}},
                                        {"rows", Value{Number{37}}},
                                        {"columns", Value{Number{113}}}}));
    release(terminal.root / "check-size");
    (void)e.control(b, {"input", "finish-input", "end\n"});
    expect_exit(e.wait(b), "0");
    require(metadata(terminal.root / "resized", "rows") == 37 &&
                metadata(terminal.root / "resized", "cols") == 113,
            "resize reaches private tty with independent ioctl oracle");
    require(contents(terminal.root / "second-read") == "end",
            "duplicate input cannot repeat the prefix as the next child line");
    require(e.originals[b].find("GOT:hello\r\n") != std::string::npos &&
                e.originals[b].find("SECOND:end\r\n") != std::string::npos,
            "captured PTY bytes retain canonical output CRLF");
    require(e.originals[b].find("different") == std::string::npos &&
                contents(terminal.root / "launches") == "L",
            "conflict cannot reach private PTY or cause relaunch");

    const auto eof = fixture({executable, temporary.root / "eof-first"});
    const auto c = job_id(3);
    e.start(c, eof, {"eof-first"});
    e.until(
        c, [&] { return std::filesystem::exists(eof.root / "ready"); },
        "EOF before exit gate");
    require(!e.wait(c, 30).find("exit_code"), "known output EOF is not process exit");
    release(eof.root / "finish");
    expect_exit(e.wait(c), "31");
    require(e.originals[c].empty(), "EOF-first fixture invents no output");

    const auto tail = fixture({executable, temporary.root / "exit-first"});
    const auto d = job_id(4);
    e.start(d, tail, {"exit-first"});
    e.until(
        d, [&] { return std::filesystem::exists(tail.root / "descendant-ready"); },
        "descendant writer ready");
    require(!e.wait(d, 30).find("exit_code"), "exiting leader cannot seal held writer");
    release(tail.root / "tail");
    expect_exit(e.wait(d), "23");
    require(e.originals[d] == std::string{"TAIL\0", 5},
            "descendant binary tail retained before terminal event");

    const auto background = fixture({executable, temporary.root / "background"});
    const auto f = job_id(5);
    e.start(f, background, {"gate"});
    bool requested = false;
    Value prior_background;
    auto tick = [&] {
      if (!requested && std::filesystem::exists(background.root / "ready")) {
        requested = true;
        e.jobs.request(
            Value::object({{"op", Value{"background"}}, {"job_id", Value{f}}}));
      }
      for (auto &request : e.jobs.requests()) {
        prior_background = request;
        (void)e.jobs.control(request);
      }
      e.pump();
    };
    const auto released =
        e.jobs.wait(f, {{}, 65536, false}, tick, [] { return false; });
    require(requested && !released.find("exit_code"),
            "mailbox background releases only wait after actual child readiness");
    require(contents(background.root / "launches") == "L", "background launch once");
    bool stale_checked = false;
    bool fresh_checked = false;
    auto second_tick = [&] {
      if (!stale_checked) {
        const auto current = e.jobs.foreground();
        require(field(current, "wait_epoch") != field(prior_background, "wait_epoch"),
                "second waiter captures a different foreground epoch");
        const auto response = e.jobs.control(prior_background);
        require(field(response, "released") == Value{false},
                "prior wait background cannot release newer wait for same job");
        stale_checked = true;
      } else if (!fresh_checked) {
        e.jobs.request(
            Value::object({{"op", Value{"background"}}, {"job_id", Value{f}}}));
        const auto requests = e.jobs.requests();
        require(requests.size() == 1, "new wait receives exactly one fresh request");
        require(field(requests.front(), "wait_epoch") !=
                    field(prior_background, "wait_epoch"),
                "fresh background captures the current epoch");
        const auto response = e.jobs.control(requests.front());
        require(field(response, "released") == Value{true},
                "fresh captured background releases its exact wait");
        fresh_checked = true;
      }
      e.pump();
    };
    require(!e.jobs.wait(f, {1000, 65536, false}, second_tick, [] { return false; })
                    .find("exit_code") &&
                stale_checked && fresh_checked,
            "new waiter survives stale control boundary then accepts fresh release");
    release(background.root / "finish");
    expect_exit(e.wait(f), "0");
    require(e.originals[f] == "READY\nDONE\n",
            "same job progresses after mailbox release");

    const auto stopped = fixture({executable, temporary.root / "stop"});
    const auto s = job_id(6);
    e.start(s, stopped, {"gate"});
    e.until(
        s, [&] { return std::filesystem::exists(stopped.root / "ready"); },
        "stop fixture ready");
    (void)e.control(s, {"stop", "stop", {}});
    const auto stopped_result = e.wait(s);
    require(string_field(stopped_result, "effect_outcome") == "unknown",
            "stop preserves arbitrary-effect uncertainty");
    require(!e.jobs.active(), "stopped root collected and terminal retained");

    const auto deadline = fixture({executable, temporary.root / "deadline"});
    const auto t = job_id(7);
    const auto started = std::chrono::steady_clock::now();
    e.start(t, deadline, {"deadline", false, 1});
    e.until(
        t, [&] { return std::filesystem::exists(deadline.root / "ready"); },
        "deadline fixture ready");
    Value timed;
    do {
      timed = e.wait(t, 100);
      require(std::chrono::steady_clock::now() - started < std::chrono::seconds{3},
              "repeated waits cannot extend one-second command lifetime");
    } while (e.jobs.active());
    require(field(timed, "timed_out") == Value{true},
            "actual lifetime timeout recorded");
    require(string_field(timed, "effect_outcome") == "unknown" && e.terminals[t] == 1 &&
                contents(deadline.root / "launches") == "L",
            "absolute lifetime expires once with no effect replay");

    const auto continued = fixture({executable, temporary.root / "continued"});
    const auto u = job_id(8);
    e.start(u, continued, {"gate"});
    e.until(
        u, [&] { return std::filesystem::exists(continued.root / "ready"); },
        "continuation fixture ready");
    (void)e.jobs.control(Value::object({{"op", Value{"signal"}},
                                        {"job_id", Value{u}},
                                        {"request_id", Value{"suspend-once"}},
                                        {"signal", Value{"stop"}}}));
    e.until(
        u,
        [&] {
          return string_field(e.jobs.read(Value::object({{"job_id", Value{u}}})),
                              "state") == "stopped";
        },
        "observed SIGSTOP");
    release(continued.root / "finish");
    require(!e.wait(u, 30).find("exit_code") &&
                string_field(e.jobs.read(Value::object({{"job_id", Value{u}}})),
                             "state") == "stopped",
            "reattaching observation never silently continues a stopped process");
    (void)e.jobs.control(Value::object({{"op", Value{"signal"}},
                                        {"job_id", Value{u}},
                                        {"request_id", Value{"continue-once"}},
                                        {"signal", Value{"continue"}}}));
    expect_exit(e.wait(u), "0");
    require(e.originals[u] == "READY\nDONE\n" &&
                contents(continued.root / "launches") == "L",
            "explicit continuation resumes the same stopped child");

    const auto blocked_input =
        fixture({executable, temporary.root / "blocked-pty-input"});
    const auto blocked_id = job_id(15);
    e.start(blocked_id, blocked_input, {"raw-input", true});
    e.until(
        blocked_id,
        [&] { return std::filesystem::exists(blocked_input.root / "ready"); },
        "raw input fixture ready");
    const auto blocked_pid = metadata(blocked_input.root / "identity", "pid");
    (void)e.jobs.control(Value::object({{"op", Value{"signal"}},
                                        {"job_id", Value{blocked_id}},
                                        {"request_id", Value{"stop-before-input"}},
                                        {"signal", Value{"stop"}}}));
    e.until(
        blocked_id,
        [&] {
          return string_field(
                     e.jobs.read(Value::object({{"job_id", Value{blocked_id}}})),
                     "state") == "stopped";
        },
        "raw input child observed stopped");
    std::string payload(65536, '\0');
    for (std::size_t n = 0; n < payload.size(); ++n)
      payload[n] = static_cast<char>(n % 251);
    for (const auto *op : {"input", "resize"}) {
      auto malformed = Value::object({{"op", Value{op}},
                                      {"job_id", Value{blocked_id}},
                                      {"request_id", Value{"oversized-extra"}},
                                      {"bytes", Value{payload}}});
      if (std::string_view{op} == "input")
        malformed.object().emplace_back("padding", Value{payload});
      bool refused = false;
      try {
        (void)e.jobs.control(malformed);
      } catch (const Error &error) {
        refused = error.code == ErrorCode::invalid_range;
      }
      require(refused, "irrelevant retained request payload refused before effect");
      refused = false;
      try {
        e.jobs.request(malformed);
      } catch (const Error &error) {
        refused = error.code == ErrorCode::invalid_range;
      }
      require(refused, "irrelevant mailbox payload refused before retention");
    }
    const ControlOptions blocked_delivery{"input", "blocked-input-once", payload};
    (void)e.control(blocked_id, blocked_delivery);
    e.until(
        blocked_id,
        [&] {
          const auto receipt = e.control(blocked_id, blocked_delivery);
          const auto written =
              std::stoull(field(receipt, "written_bytes").number().text());
          require(field(receipt, "duplicate") == Value{true},
                  "blocked input polls preserve one delivery identity");
          return written > 0 && written < payload.size();
        },
        "stopped tty input has independently reported partial prefix");
    (void)e.jobs.control(Value::object({{"op", Value{"signal"}},
                                        {"job_id", Value{blocked_id}},
                                        {"request_id", Value{"continue-past-input"}},
                                        {"signal", Value{"continue"}}}));
    expect_exit(e.wait(blocked_id), "0");
    require(
        contents(blocked_input.root / "received") == payload &&
            metadata(blocked_input.root / "received-identity", "pid") == blocked_pid &&
            contents(blocked_input.root / "launches") == "L" &&
            e.originals[blocked_id] == "READY\nDONE\n",
        "continue bypasses blocked input and same child receives exact binary bytes");
    const auto blocked_receipt = e.control(blocked_id, blocked_delivery);
    require(field(blocked_receipt, "duplicate") == Value{true} &&
                field(blocked_receipt, "written_bytes") == Value{Number{65536}} &&
                string_field(blocked_receipt, "status") == "delivered",
            "partial input delivery completes once without replaying accepted prefix");

    const auto normal = fixture({executable, temporary.root / "exit143"});
    const auto v = job_id(9);
    e.start(v, normal, {"exit143"});
    const auto normal_result = e.wait(v);
    expect_exit(normal_result, "143");
    require(string_field(normal_result, "termination") == "exit" &&
                field(normal_result, "signal") == Value{Number{0}},
            "normal exit143 preserves native termination kind");
    const auto signaled = fixture({executable, temporary.root / "signal"});
    const auto w = job_id(10);
    e.start(w, signaled, {"gate"});
    e.until(
        w, [&] { return std::filesystem::exists(signaled.root / "ready"); },
        "SIGTERM fixture ready");
    (void)e.jobs.control(Value::object({{"op", Value{"signal"}},
                                        {"job_id", Value{w}},
                                        {"request_id", Value{"terminate-once"}},
                                        {"signal", Value{"terminate"}}}));
    const auto signal_result = e.wait(w);
    require(string_field(signal_result, "termination") == "signal" &&
                field(signal_result, "signal") == Value{Number{SIGTERM}},
            "SIGTERM cannot collapse into normal exit143");

    const auto flooded = fixture({executable, temporary.root / "fullqueue"});
    const auto x = job_id(11);
    e.start(x, flooded, {"flood"});
    const auto full_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (field(e.jobs.read(Value::object(
                     {{"job_id", Value{x}}, {"count", Value{Number{0}}}})),
                 "received_bytes") != Value{Number{1024 * 1024}}) {
      require(std::chrono::steady_clock::now() < full_deadline,
              "independently observed one MiB queue saturation is bounded");
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    require(e.originals[x].empty(), "full queue case has no owner consumption");
    (void)e.control(x, {"stop", "fullqueue-stop", {}});
    const auto stop_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (string_field(e.jobs.read(Value::object(
                            {{"job_id", Value{x}}, {"count", Value{Number{0}}}})),
                        "state") != "settling") {
      require(std::chrono::steady_clock::now() < stop_deadline,
              "full queue worker stops without audit owner consumption");
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    const auto flood_result = e.wait(x);
    require(string_field(flood_result, "effect_outcome") == "unknown" &&
                field(flood_result, "capture_complete") == Value{false},
            "full-queue drain exhaustion records incomplete uncertain capture");
    require(e.originals[x].size() == 1024 * 1024 &&
                std::all_of(e.originals[x].begin(), e.originals[x].end(),
                            [](char byte) { return byte == 'F'; }),
            "full queue retains the exact known prefix before settlement");

    for (const bool pty : {false, true}) {
      const auto missing = job_id(pty ? 13 : 12);
      const auto path = temporary.root / "does-not-exist";
      e.start(missing, {path, temporary.root}, {"gate", pty});
      const auto failed = e.wait(missing);
      require(failed.find("error") != nullptr &&
                  string_field(failed, "effect_outcome") == "unknown",
              "missing target produces one explicit startup failure");
      require(e.originals[missing].empty() && e.terminals[missing] == 1,
              "failed pipe or PTY exec cannot invent output or retry");
    }

    bool stale = false;
    try {
      (void)e.control(job_id(99), {"input", "wrong", "X"});
    } catch (const Error &error) {
      stale = error.code == ErrorCode::invalid_range;
    }
    require(stale, "unknown job cannot target another process");
    const auto invalid = fixture({executable, temporary.root / "invalid"});
    auto bad = arguments(invalid, {"gate"});
    for (auto &[key, item] : bad.object())
      if (key == "pty")
        item = Value{"not-a-boolean"};
    bool rejected = false;
    try {
      e.jobs.start(job_id(98), bad);
    } catch (const Error &) {
      rejected = true;
    }
    require(rejected && !std::filesystem::exists(invalid.root / "launches"),
            "invalid transport arguments reject before external launch effect");
    const auto legacy = fixture({executable, temporary.root / "legacy-concurrent"});
    const auto concurrent = fixture({executable, temporary.root / "job-concurrent"});
    Value legacy_result;
    std::exception_ptr legacy_failure;
    std::jthread legacy_thread([&] {
      try {
        LocalTools tools;
        legacy_result = tools.run("exec", arguments(legacy, {"closed-stdio"}));
      } catch (...) {
        legacy_failure = std::current_exception();
      }
    });
    const auto concurrent_id = job_id(14);
    e.start(concurrent_id, concurrent, {"closed-stdio"});
    expect_exit(e.wait(concurrent_id), "0");
    legacy_thread.join();
    if (legacy_failure)
      std::rethrow_exception(legacy_failure);
    expect_exit(legacy_result, "0");
    require(e.originals[concurrent_id] == "STDIO\n" &&
                contents(legacy.root / "launches") == "L" &&
                contents(concurrent.root / "launches") == "L",
            "legacy and job concurrent launches preserve isolated capture and EOF");
    check_subprocess(test, fixture({executable, temporary.root / "closed"}),
                     "--closed-stdio");
    check_subprocess(test, fixture({executable, temporary.root / "resource"}),
                     "--resource-failure");
    e.jobs.shutdown();
    require(::waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD,
            "every owned direct child has exactly one authoritative reap");
    std::cout
        << "Command jobs: external launch/PID, exact split binary bytes, private "
           "PTY/input dedup, gated exit/EOF, background, stop, immutable deadline, "
           "closed stdio and reap oracles pass\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << "native command jobs error " << static_cast<unsigned>(error.code)
              << " detail " << error.detail << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
