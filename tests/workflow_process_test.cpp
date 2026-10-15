#include "blackbird/context.hpp"
#include "blackbird/json.hpp"
#include "blackbird/workflow_host.hpp"
#include <cerrno>
#include <charconv>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace blackbird;
namespace {
using Clock = std::chrono::steady_clock;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
std::string contents(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
void touch(const std::filesystem::path &path) {
  std::ofstream output{path};
  check(bool(output), "fixture touch failed");
}
class Process {
public:
  Process(std::vector<std::string> arguments, const std::filesystem::path &output) {
    std::vector<char *> argv;
    for (auto &argument : arguments)
      argv.push_back(argument.data());
    argv.push_back(nullptr);
    const int fd = ::open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    check(fd >= 0, "fixture log open failed");
    pid_ = ::fork();
    if (pid_ == 0) {
      if (::dup2(fd, STDOUT_FILENO) < 0 || ::dup2(fd, STDERR_FILENO) < 0)
        _exit(126);
      ::close(fd);
      ::execv(argv.front(), argv.data());
      _exit(127);
    }
    ::close(fd);
    check(pid_ > 0, "fixture fork failed");
  }
  ~Process() {
    if (pid_ > 0) {
      (void)::kill(pid_, SIGKILL);
      int status = 0;
      while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }
  Process(const Process &) = delete;
  Process &operator=(const Process &) = delete;
  int wait() {
    const auto end = Clock::now() + std::chrono::seconds{30};
    while (Clock::now() < end) {
      int status = 0;
      const auto result = ::waitpid(pid_, &status, WNOHANG);
      if (result == pid_) {
        pid_ = -1;
        return status;
      }
      check(result == 0 || (result < 0 && errno == EINTR), "fixture waitpid failed");
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    throw std::runtime_error("fixture process did not exit");
  }
  void terminate() {
    check(::kill(pid_, SIGTERM) == 0, "fixture SIGTERM failed");
    const int status = wait();
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "owned signal shutdown failed");
  }
  void crash() {
    check(::kill(pid_, SIGKILL) == 0, "fixture kill failed");
    const int status = wait();
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
          "fixture server did not crash");
  }

private:
  pid_t pid_ = -1;
};
Value operation(std::string name) {
  return Value::object({{"op", Value{std::move(name)}}});
}
Value rpc(const std::filesystem::path &session, const Value &request) {
  return WorkflowHost::request(session, request, std::chrono::seconds{10});
}
Value named(std::string op, const char *name) {
  return Value::object({{"op", Value{std::move(op)}}, {"id", Value{name}}});
}
void ready(const std::filesystem::path &session) {
  const auto end = Clock::now() + std::chrono::seconds{30};
  while (Clock::now() < end) {
    try {
      (void)WorkflowHost::request(session, operation("status"),
                                  std::chrono::milliseconds{200});
      return;
    } catch (const Error &) {
      std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
  }
  throw std::runtime_error("workflow process never became ready");
}
Value settled(const std::filesystem::path &session, const char *name) {
  const auto end = Clock::now() + std::chrono::seconds{15};
  while (Clock::now() < end) {
    auto row = rpc(session, named("read", name));
    if (string_field(row, "outcome") != "pending")
      return row;
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  throw std::runtime_error("process task did not settle");
}
void clean_exit(Process &process, const std::filesystem::path &session) {
  (void)rpc(session, operation("shutdown"));
  const int status = process.wait();
  check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "server clean shutdown failed");
}
int effect(int argc, char **argv) {
  check(argc == 5, "bad effect fixture arguments");
  const int fd = ::open(argv[2], O_WRONLY | O_CREAT | O_APPEND, 0600);
  check(fd >= 0, "effect open failed");
  const auto written = ::write(fd, "x", 1);
  ::close(fd);
  check(written == 1, "effect write failed");
  {
    const auto temporary = std::string{argv[4]} + ".pending";
    {
      std::ofstream marker{temporary};
      marker << ::getpid();
      marker.close();
      check(bool(marker), "fixture PID marker failed");
    }
    std::filesystem::rename(temporary, argv[4]);
  }
  const auto end = Clock::now() + std::chrono::seconds{20};
  while (!std::filesystem::exists(argv[3]) && Clock::now() < end)
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  if (!std::filesystem::exists(argv[3])) {
    std::cerr << "fixture gate timed out\n";
    return 1;
  }
  std::cout << "fixture-complete\n";
  return 0;
}
struct Executables {
  std::string host;
  std::string fixture;
};
void exercise(const Executables &executables, const std::filesystem::path &directory) {
  const auto session = directory / "session";
  const auto launch = [&] {
    return std::vector<std::string>{executables.host, "--serve", "--session",
                                    session.string()};
  };
  const auto echo =
      Value::object({{"op", Value{"start"}},
                     {"id", Value{"exact"}},
                     {"tool", Value{"echo"}},
                     {"arguments", Value::object({{"answer", Value{"yes"}}})}});
  const std::array<const char *, 2> uncertain_names{"uncertain-one", "uncertain-two"};
  std::array<std::string, 2> uncertain_invocations;
  std::array<std::string, 2> uncertain_attempts;
  std::string invocation;
  {
    Process host{launch(), directory / "host-initial.log"};
    ready(session);
    Process second{launch(), directory / "second.log"};
    const int second_status = second.wait();
    check(WIFEXITED(second_status) && WEXITSTATUS(second_status) != 0,
          "second session owner accepted");
    (void)rpc(session, operation("status"));
    Process client{{executables.host, "--session", session.string(), "--request",
                    unwrap(dump_json(echo))},
                   directory / "client.json"};
    const int client_status = client.wait();
    check(WIFEXITED(client_status) && WEXITSTATUS(client_status) == 0,
          "explicit JSON client failed");
    (void)unwrap(parse_json(contents(directory / "client.json")));
    const auto result = settled(session, "exact");
    check(string_field(result, "outcome") == "success", "echo process result failed");
    check(field(field(result, "result"), "value") == field(echo, "arguments"),
          "process result lost exact value");
    invocation = string_field(result, "invocation");
    check(string_field(rpc(session, echo), "invocation") == invocation,
          "duplicate client created new invocation");
    (void)rpc(session,
              Value::object(
                  {{"op", Value{"start"}},
                   {"id", Value{"child-signal"}},
                   {"tool", Value{"exec"}},
                   {"arguments",
                    Value::object({{"command", Value{"kill -TERM $$; exit 99"}}})}}));
    const auto binary = directory / "binary-original";
    const std::string original{"\0\xff\x10", 3};
    {
      std::ofstream output{binary, std::ios::binary};
      output.write(original.data(), static_cast<std::streamsize>(original.size()));
      check(bool(output), "binary original fixture failed");
    }
    const auto binary_request = Value::object(
        {{"op", Value{"start"}},
         {"id", Value{"binary"}},
         {"tool", Value{"read_file"}},
         {"arguments", Value::object({{"path", Value{binary.string()}}})}});
    (void)rpc(session, binary_request);
    const auto native_binary = settled(session, "binary");
    const auto &native_originals =
        field(field(native_binary, "result"), "originals").array();
    check(native_originals.size() == 1 &&
              string_field(native_originals.front(), "bytes") == original,
          "native original bytes changed");
    Process binary_client{{executables.host, "--session", session.string(), "--request",
                           unwrap(dump_json(named("read", "binary")))},
                          directory / "binary-client.json"};
    const int binary_status = binary_client.wait();
    check(WIFEXITED(binary_status) && WEXITSTATUS(binary_status) == 0,
          "binary result JSON client failed");
    const auto exported =
        unwrap(parse_json(contents(directory / "binary-client.json")));
    const auto &exported_originals =
        field(field(exported, "result"), "originals").array();
    check(exported_originals.size() == 1, "JSON export lost original entry");
    const auto &encoded = field(exported_originals.front(), "bytes");
    check(string_field(encoded, "encoding") == "hex" &&
              string_field(encoded, "bytes") == "00ff10",
          "wrong binary JSON export envelope");
    const auto &hex = string_field(encoded, "bytes");
    std::string decoded;
    for (std::size_t index = 0; index < hex.size(); index += 2) {
      unsigned value = 0;
      const auto parsed =
          std::from_chars(hex.data() + index, hex.data() + index + 2, value, 16);
      check(parsed.ec == std::errc{} && parsed.ptr == hex.data() + index + 2,
            "invalid exported hexadecimal bytes");
      decoded += static_cast<char>(value);
    }
    check(decoded == original, "JSON export did not roundtrip exact original");
    const auto signalled = settled(session, "child-signal");
    check(field(field(field(signalled, "result"), "value"), "exit_code") ==
              Value{Number{128 + SIGTERM}},
          "child inherited blocked service signal mask");
    host.terminate();
  }
  {
    Process host{launch(), directory / "host-before-crash.log"};
    ready(session);
    check(string_field(rpc(session, named("read", "exact")), "invocation") ==
              invocation,
          "clean process reopen lost exact identity");
    std::array<std::filesystem::path, 2> effects;
    std::array<std::filesystem::path, 2> gates;
    std::array<std::filesystem::path, 2> markers;
    std::array<Value, 2> requests;
    std::array<pid_t, 2> effect_pids{-1, -1};
    for (std::size_t index = 0; index < uncertain_names.size(); ++index) {
      const auto suffix = std::to_string(index);
      effects[index] = directory / ("effects-" + suffix);
      gates[index] = directory / ("release-" + suffix);
      markers[index] = directory / ("effect-ready-" + suffix);
      Value::Array argv{Value{executables.fixture}, Value{"--effect"},
                        Value{effects[index].string()}, Value{gates[index].string()},
                        Value{markers[index].string()}};
      requests[index] = Value::object(
          {{"op", Value{"start"}},
           {"id", Value{uncertain_names[index]}},
           {"tool", Value{"exec"}},
           {"arguments", Value::object({{"argv", Value{std::move(argv)}}})}});
      const auto started = rpc(session, requests[index]);
      uncertain_invocations[index] = string_field(started, "invocation");
      uncertain_attempts[index] = string_field(started, "attempt");
    }
    check(uncertain_invocations[0] != uncertain_invocations[1] &&
              uncertain_attempts[0] != uncertain_attempts[1],
          "concurrent tasks did not receive distinct effect identities");
    const auto end = Clock::now() + std::chrono::seconds{15};
    while ((!std::filesystem::exists(markers[0]) ||
            !std::filesystem::exists(markers[1])) &&
           Clock::now() < end)
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    for (std::size_t index = 0; index < uncertain_names.size(); ++index) {
      check(std::filesystem::exists(markers[index]),
            "both concurrent independent effects must happen before owner crash");
      check(contents(effects[index]) == "x",
            "concurrent effect count before crash wrong");
      const auto duplicate = rpc(session, requests[index]);
      check(
          string_field(duplicate, "invocation") == uncertain_invocations[index] &&
              string_field(duplicate, "attempt") == uncertain_attempts[index] &&
              string_field(duplicate, "outcome") == "pending",
          "pending duplicate changed concurrent task identity or settled before gate");
      check(contents(effects[index]) == "x",
            "pending duplicate executed concurrent effect twice");
      const auto marker_bytes = contents(markers[index]);
      const auto parsed = std::from_chars(marker_bytes.data(),
                                          marker_bytes.data() + marker_bytes.size(),
                                          effect_pids[index]);
      check(parsed.ec == std::errc{} &&
                parsed.ptr == marker_bytes.data() + marker_bytes.size() &&
                effect_pids[index] > 0,
            "invalid concurrent fixture process identity");
    }
    check(effect_pids[0] != effect_pids[1],
          "concurrent fixtures shared process identity");
    host.crash();
    for (const auto &gate : gates)
      touch(gate);
    const auto exit_deadline = Clock::now() + std::chrono::seconds{30};
    std::array<bool, 2> gone{false, false};
    while ((!gone[0] || !gone[1]) && Clock::now() < exit_deadline) {
      // Observation only. The product never adopts these recovered processes or
      // sends them controls. Each fixture finishes through its own explicit gate.
      for (std::size_t index = 0; index < effect_pids.size(); ++index)
        if (!gone[index] && ::kill(effect_pids[index], 0) != 0 && errno == ESRCH)
          gone[index] = true;
      if (!gone[0] || !gone[1])
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    check(gone[0] && gone[1], "both gated fixtures must exit after owner crash");
    for (const auto &effect_file : effects)
      check(contents(effect_file) == "x", "concurrent effect count after crash wrong");
  }
  {
    Process host{launch(), directory / "host-recovered.log"};
    ready(session);
    const auto status = rpc(session, operation("status"));
    check(field(status, "recovery_required") == Value{true},
          "released recovered attempt silently acknowledged");
    for (std::size_t index = 0; index < uncertain_names.size(); ++index) {
      const auto old = rpc(session, named("read", uncertain_names[index]));
      check(string_field(old, "invocation") == uncertain_invocations[index] &&
                string_field(old, "attempt") == uncertain_attempts[index],
            "concurrent recovery lost exact attempt identity");
      check(string_field(old, "execution_state") == "unknown_after_recovery" &&
                string_field(old, "recovered_outcome") == "unknown" &&
                field(old, "executor_absent") == Value{true},
            "concurrent recovered attempt not exposed as unknown without executor");
      check(contents(directory / ("effects-" + std::to_string(index))) == "x",
            "recovery replayed concurrent external effect");
    }
    bool refused = false;
    try {
      (void)rpc(session, Value::object({{"op", Value{"start"}},
                                        {"id", Value{"new"}},
                                        {"tool", Value{"echo"}},
                                        {"arguments", Value{}}}));
    } catch (const Error &) {
      refused = true;
    }
    check(refused, "unreconciled root admitted independent effects");
    clean_exit(host, session);
    for (std::size_t index = 0; index < uncertain_names.size(); ++index)
      check(contents(directory / ("effects-" + std::to_string(index))) == "x",
            "read-only shutdown replayed concurrent effect");
  }
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 1 && std::string_view{argv[1]} == "--effect")
    return effect(argc, argv);
  std::filesystem::path directory;
  try {
    check(argc == 2, "workflow executable path required");
    std::array<char, 64> pattern{};
    const std::string prefix = "/tmp/bb-workflow-process-XXXXXX";
    std::copy(prefix.begin(), prefix.end(), pattern.begin());
    check(::mkdtemp(pattern.data()) != nullptr, "mkdtemp failed");
    directory = pattern.data();
    exercise(Executables{.host = std::filesystem::absolute(argv[1]).string(),
                         .fixture = std::filesystem::absolute(argv[0]).string()},
             directory);
    std::filesystem::remove_all(directory);
    std::cout
        << "native workflow process ownership, exact result and crash recovery pass\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  std::cerr << "process fixture retained at " << directory << '\n';
  return 1;
}
