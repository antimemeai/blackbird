#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <stdexcept>
#include <string_view>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace blackbird;
namespace {
using Clock = std::chrono::steady_clock;
void check(bool good, const char *message) {
  if (!good)
    throw std::runtime_error(message);
}
template <class T> T identity(unsigned char value) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{value};
  return unwrap(T::from_bytes(bytes));
}
OperationAttemptId attempt_id(std::string_view text) {
  check(text.size() == 32, "job identity is not a native attempt identity");
  IdentityBytes bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i)
    bytes[i] = static_cast<std::byte>(
        std::stoul(std::string{text.substr(i * 2, 2)}, nullptr, 16));
  return unwrap(OperationAttemptId::from_bytes(bytes));
}
void write_all(int fd, std::string_view bytes) {
  while (!bytes.empty()) {
    const auto count = ::write(fd, bytes.data(), bytes.size());
    if (count < 0 && errno == EINTR)
      continue;
    check(count > 0, "fixture write failed");
    bytes.remove_prefix(static_cast<std::size_t>(count));
  }
}
void append_file(const std::filesystem::path &path, std::string_view bytes) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
  check(fd >= 0, "fixture marker open failed");
  try {
    write_all(fd, bytes);
  } catch (...) {
    (void)::close(fd);
    throw;
  }
  check(::close(fd) == 0, "fixture marker close failed");
}
const std::string prefix{"A\0\xff\xc3", 4};
const std::string suffix{"\xa9Z", 2};
int fixture(const std::filesystem::path &directory, std::string_view mode) {
  if (mode == "pty") {
    termios settings{};
    check(::tcgetattr(STDIN_FILENO, &settings) == 0, "fixture has no private PTY");
    settings.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON));
    settings.c_oflag &= static_cast<tcflag_t>(~OPOST);
    settings.c_cc[VMIN] = 1;
    settings.c_cc[VTIME] = 0;
    check(::tcsetattr(STDIN_FILENO, TCSANOW, &settings) == 0,
          "fixture PTY configuration failed");
  }
  append_file(directory / "launches", "launch\n");
  write_file(directory / "pid", std::to_string(::getpid()));
  if (mode == "quiet-large") {
    write_all(STDOUT_FILENO, std::string(65536, 'R'));
    while (!std::filesystem::exists(directory / "release"))
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    append_file(directory / "effects", "forbidden\n");
    return 0;
  }
  if (mode == "unbounded") {
    // Never ends on its own: only the harness's output budget can stop it.
    const std::string bytes(8192, 'U');
    for (;;)
      write_all(STDOUT_FILENO, bytes);
  }
  if (mode == "flood") {
    while (!std::filesystem::exists(directory / "release"))
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    const std::string bytes(8192, 'Q');
    for (unsigned i = 0; i < 256; ++i)
      write_all(STDOUT_FILENO, bytes);
    return 0;
  }
  write_all(STDOUT_FILENO, prefix);
  while (!std::filesystem::exists(directory / "release"))
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  if (mode == "pty") {
    std::string input;
    for (;;) {
      char byte = 0;
      const auto count = ::read(STDIN_FILENO, &byte, 1);
      if (count < 0 && errno == EINTR)
        continue;
      check(count == 1, "fixture input closed unexpectedly");
      input.push_back(byte);
      if (byte == '\n')
        break;
    }
    termios settings{};
    check(::tcgetattr(STDIN_FILENO, &settings) == 0,
          "fixture PTY configuration disappeared");
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 1;
    check(::tcsetattr(STDIN_FILENO, TCSANOW, &settings) == 0,
          "fixture extra-input observation failed");
    for (;;) {
      char extra[128];
      const auto count = ::read(STDIN_FILENO, extra, sizeof(extra));
      if (count < 0 && errno == EINTR)
        continue;
      check(count >= 0, "fixture extra-input read failed");
      if (count == 0)
        break;
      input.append(extra, static_cast<std::size_t>(count));
    }
    write_file(directory / "input", input);
    write_all(STDOUT_FILENO, input);
  }
  write_all(STDOUT_FILENO, suffix);
  append_file(directory / "effects", "end\n");
  return 7;
}
class Provider final : public CodingProvider {
public:
  unsigned calls = 0;
  std::function<void()> during;
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    ++calls;
    if (during)
      during();
    return Value::object({{"output", Value{Value::Array{}}}});
  }
};
struct Harness {
  std::unique_ptr<RetainedState> root;
  AuditLog log;
  ContextStore context;
  Provider provider;
  CodingEngine engine;
  explicit Harness(const std::filesystem::path &directory,
                   JournalCapacity capacity = {32 * 1024 * 1024, 20000})
      : root(unwrap(RetainedState::create(
            std::make_unique<NativeJournalDirectory>(
                unwrap(NativeJournalDirectory::open(directory.string()))),
            "audit",
            {identity<EnvironmentId>(1),
             identity<AuditStreamId>(2),
             3,
             {1024 * 1024, 4 * 1024 * 1024},
             std::nullopt},
            capacity))),
        log(*root), context(log), engine(log, context, provider, "fixture") {}
};
Value process(std::string_view operation, std::string_view job) {
  return Value::object(
      {{"op", Value{std::string{operation}}}, {"job_id", Value{std::string{job}}}});
}
Value launch_arguments(const std::string &executable,
                       const std::filesystem::path &directory,
                       std::string_view mode = "pipe", unsigned timeout = 10) {
  return Value::object(
      {{"argv",
        Value{Value::Array{Value{executable}, Value{"--fixture"},
                           Value{directory.string()}, Value{std::string{mode}}}}},
       {"yield_ms", Value{Number{0}}},
       {"timeout_seconds", Value{Number{timeout}}},
       {"pty", Value{mode == "pty"}}});
}
template <class Predicate> void await(Harness &h, Predicate ready, const char *why) {
  const auto deadline = Clock::now() + std::chrono::seconds{5};
  while (!ready()) {
    check(Clock::now() < deadline, why);
    h.engine.poll_commands();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  h.engine.poll_commands();
}
bool terminal(Harness &h, const std::string &job) {
  const auto state = unwrap(h.root->attempt(attempt_id(job)));
  return state.observation && state.observation->phase == AttemptPhase::terminal;
}
AttemptDisposition terminal_disposition(Harness &h, const std::string &job) {
  const auto state = unwrap(h.root->attempt(attempt_id(job)));
  if (!state.observation || state.observation->phase != AttemptPhase::terminal)
    throw std::runtime_error("command has no retained terminal observation");
  return state.observation->disposition;
}
std::size_t terminal_count(Harness &h, const std::string &job) {
  std::size_t count = 0;
  for (const auto &fact : h.root->committed_facts())
    if (const auto *event = std::get_if<AttemptObservationEvent>(&fact.event.body);
        event && event->attempt == attempt_id(job) &&
        event->phase == AttemptPhase::terminal)
      ++count;
  return count;
}
std::string retained_bytes(Harness &h, const std::string &job) {
  std::string bytes;
  for (const auto &fact : h.root->committed_facts()) {
    const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
    if (!record || record->channel != ApplicationChannel::log)
      continue;
    const auto packet = unwrap(read_packet(record->payload));
    const auto *label = packet.find("label");
    if (!label || *label != Value{"process.output"})
      continue;
    const auto &metadata = field(packet, "metadata");
    const auto *attempt = metadata.find("attempt");
    if (!attempt || *attempt != Value{job})
      continue;
    for (const auto reference : fact.event.dependencies) {
      const auto source = unwrap(h.root->source(reference));
      bytes.append(reinterpret_cast<const char *>(source.data()), source.size());
    }
  }
  return bytes;
}
void same_command(Harness &h, const std::filesystem::path &directory,
                  const std::string &executable) {
  const auto task = h.engine.tasks().operator_command("add KEEP_TASK_QUEUED");
  const auto task_id = field(field(task, "changed").array()[0], "id");
  auto args = launch_arguments(executable, directory);
  args.object().emplace_back("task_id", task_id);
  const auto started = h.engine.operator_call("exec", args);
  const auto job = string_field(started, "job_id");
  check(string_field(started, "output_ref") == job && !started.find("exit_code"),
        "background launch fabricated exit or changed output identity");
  await(
      h, [&] { return std::filesystem::exists(directory / "pid"); },
      "background launch never started");
  const auto pid = static_cast<pid_t>(std::stol(read_file(directory / "pid")));
  check(::kill(pid, 0) == 0 && !terminal(h, job),
        "background return killed or settled the child");
  check(h.root->protected_settlement().has_value(), "job lost its terminal reserve");
  const auto task_snapshot = h.engine.tasks().snapshot();
  h.engine.operator_call("write_file",
                         Value::object({{"path", Value{(directory / "other").string()}},
                                        {"content", Value{"other turn"}}}));
  bool failed = false;
  try {
    h.engine.turn({"", "error('intentional unrelated workflow failure')"});
  } catch (const Error &) {
    failed = true;
  }
  check(failed && h.root->protected_settlement().has_value() && !terminal(h, job),
        "unrelated failed workflow consumed job custody/reserve");
  for (unsigned i = 0; i < 3; ++i) {
    auto wait = process("foreground", job);
    wait.object().emplace_back("yield_ms", Value{Number{1}});
    const auto waiting = h.engine.operator_call("process", wait);
    check(!waiting.find("exit_code") && !terminal(h, job) && ::kill(pid, 0) == 0,
          "foreground wait timeout stopped or settled command");
  }
  for (const bool restart : {false, true}) {
    bool refused = false;
    try {
      if (restart)
        h.engine.validate_restart();
      else
        h.engine.validate_session_switch();
    } catch (const Error &error) {
      refused = error.code == ErrorCode::busy;
    }
    check(refused, "live command did not block restart/session switch");
  }
  write_file(directory / "release", "go");
  await(h, [&] { return terminal(h, job); }, "command terminal was never retained");
  const auto done = h.engine.operator_call("process", process("foreground", job));
  check(field(done, "exit_code") == Value{Number{7}}, "foreground lost actual exit");
  check(read_file(directory / "launches") == "launch\n" &&
            read_file(directory / "effects") == "end\n" &&
            read_file(directory / "pid") == std::to_string(pid),
        "foreground relaunched or duplicated external effects");
  check(retained_bytes(h, job) == prefix + suffix,
        "binary bytes lost or duplicated across foreground waits");
  check(terminal_count(h, job) == 1 &&
            terminal_disposition(h, job) == AttemptDisposition::failure,
        "wrong or duplicate command terminal disposition");
  check(h.engine.tasks().snapshot() == task_snapshot,
        "command completion modified model-authored task state");
  check(!h.root->protected_settlement().has_value(), "quiescent reserve leaked");
  h.engine.validate_restart();
  h.engine.validate_session_switch();
  check(h.provider.calls == 0, "native commands unexpectedly called provider");
}
void sustainable_foreground(Harness &h, const std::filesystem::path &directory,
                            const std::string &executable) {
  std::vector<std::string> ids;
  Value original_presentation;
  for (unsigned i = 0; i < 40; ++i) {
    const auto child = directory / std::to_string(i);
    std::filesystem::create_directory(child);
    write_file(child / "release", "go");
    auto arguments = launch_arguments(executable, child);
    std::erase_if(arguments.object(),
                  [](const auto &entry) { return entry.first == "yield_ms"; });
    const auto done = h.engine.operator_call("exec", arguments);
    check(field(done, "exit_code") == Value{Number{7}},
          "ordinary foreground execution stopped at a lifetime record quota");
    const auto id = string_field(done, "output_ref");
    if (i == 0)
      original_presentation = field(done, "output");
    ids.push_back(id);
    check(terminal_count(h, id) == 1 && retained_bytes(h, id) == prefix + suffix &&
              read_file(child / "launches") == "launch\n" &&
              read_file(child / "effects") == "end\n",
          "sustained foreground execution lost originals or duplicated effects");
  }
  const auto listing =
      h.engine.operator_call("process", Value::object({{"op", Value{"read"}}}));
  check(field(listing, "jobs").array().size() == 32,
        "completed-job retirement changed the declared resident bound");
  check(std::none_of(field(listing, "jobs").array().begin(),
                     field(listing, "jobs").array().end(),
                     [&](const Value &row) {
                       return string_field(row, "job_id") == ids.front();
                     }),
        "oldest foreground projection stayed resident after admission pressure");
  const auto old = h.engine.operator_call(
      "read_process_output", Value::object({{"output_ref", Value{ids.front()}}}));
  check(field(old, "output") == original_presentation &&
            field(old, "output_bytes") == Value{Number{6}} &&
            field(old, "returned_bytes") == Value{Number{6}} &&
            retained_bytes(h, ids.front()) == prefix + suffix &&
            terminal_count(h, ids.front()) == 1,
        "retired job output_ref stopped reading its original output");
  check(h.provider.calls == 0 && !h.root->protected_settlement().has_value(),
        "foreground retirement called provider or retained stale terminal debt");
}
void deadline(Harness &h, const std::filesystem::path &directory,
              const std::string &executable) {
  const auto began = Clock::now();
  const auto started = h.engine.operator_call(
      "exec", launch_arguments(executable, directory, "pipe", 1));
  const auto job = string_field(started, "job_id");
  await(
      h, [&] { return std::filesystem::exists(directory / "pid"); },
      "deadline fixture never started");
  for (unsigned i = 0; i < 12 && !terminal(h, job); ++i) {
    auto wait = process("foreground", job);
    wait.object().emplace_back("yield_ms", Value{Number{100}});
    (void)h.engine.operator_call("process", wait);
  }
  await(h, [&] { return terminal(h, job); }, "foreground renewed command deadline");
  check(Clock::now() - began < std::chrono::seconds{4},
        "foreground renewed original one-second lifetime");
  check(!std::filesystem::exists(directory / "effects") &&
            read_file(directory / "launches") == "launch\n" &&
            terminal_count(h, job) == 1 &&
            terminal_disposition(h, job) == AttemptDisposition::unknown,
        "deadline fabricated completion or replayed command");
}
void pty_delivery(Harness &h, const std::filesystem::path &directory,
                  const std::string &executable) {
  const auto started =
      h.engine.operator_call("exec", launch_arguments(executable, directory, "pty"));
  const auto job = string_field(started, "job_id");
  await(
      h, [&] { return std::filesystem::exists(directory / "pid"); },
      "PTY fixture never started");
  auto input = process("input", job);
  input.object().emplace_back("request_id", Value{"one-input"});
  input.object().emplace_back("bytes", Value{"hello\n"});
  const auto first = h.engine.operator_call("process", input);
  check(!first.find("error"), "first PTY input refused");
  const auto duplicate = h.engine.operator_call("process", input);
  check(!duplicate.find("error"), "same delivery identity refused");
  auto conflicting = input;
  for (auto &[key, value] : conflicting.object())
    if (key == "bytes")
      value = Value{"different\n"};
  bool conflict = false;
  try {
    const auto result = h.engine.operator_call("process", conflicting);
    conflict = result.find("error") && field(result, "error") == Value{"conflict"};
  } catch (const Error &error) {
    conflict = error.code == ErrorCode::conflict;
  }
  check(conflict, "delivery identity reuse permitted different effects");
  bool admitted = false;
  for (const auto &fact : h.root->committed_facts())
    if (const auto *event = std::get_if<InvocationEvent>(&fact.event.body)) {
      const auto packet = decode_packet(event->input);
      if (packet.has_value()) {
        const auto *request = packet.value().find("request_id");
        admitted |= request && *request == Value{"one-input"} &&
                    field(packet.value(), "bytes") == Value{"hello\n"};
      }
    }
  check(admitted, "PTY input lacks retained exact admission");
  write_file(directory / "release", "go");
  await(h, [&] { return terminal(h, job); }, "PTY input fixture never settled");
  check(read_file(directory / "input") == "hello\n" &&
            retained_bytes(h, job) == prefix + "hello\n" + suffix &&
            read_file(directory / "launches") == "launch\n" &&
            terminal_count(h, job) == 1,
        "PTY input repeated, conflicted, or lost retained bytes");
}
void capture_refusal(Harness &h, const std::filesystem::path &directory,
                     const std::string &executable) {
  h.engine.diagnostic = [](std::string_view text) { std::cerr << text; };
  const auto started =
      h.engine.operator_call("exec", launch_arguments(executable, directory, "flood"));
  const auto job = string_field(started, "job_id");
  await(
      h, [&] { return std::filesystem::exists(directory / "pid"); },
      "capacity fixture never started");
  write_file(directory / "release", "go");
  bool refused = false;
  std::optional<Error> first_poll_error;
  std::optional<Error> poll_error;
  std::size_t poll_errors = 0;
  const auto expires = Clock::now() + std::chrono::seconds{5};
  while (!terminal(h, job) && Clock::now() < expires) {
    try {
      h.engine.poll_commands();
    } catch (const Error &error) {
      if (!first_poll_error) {
        first_poll_error = error;
        const auto reserve = h.root->protected_settlement();
        std::cerr << "capacity first_error=" << error_name(error.code) << ':'
                  << error.detail
                  << " root_state=" << static_cast<unsigned>(h.root->state())
                  << " root_offset=" << h.root->cursor().end_offset;
        if (reserve)
          std::cerr << " reserve_bytes=" << reserve->max_file_bytes
                    << " reserve_records=" << reserve->max_records;
        else
          std::cerr << " reserve=none";
        std::cerr << "\nfirst_error job snapshot: "
                  << unwrap(format_value(h.engine.command_jobs()->read(Value::object(
                         {{"job_id", Value{job}}, {"count", Value{Number{0}}}}))))
                  << '\n';
      }
      refused |= error.code == ErrorCode::capacity;
      poll_error = error;
      ++poll_errors;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  if (!refused || !terminal(h, job)) {
    std::cerr << "capacity diagnostic: refused=" << refused
              << " terminal=" << terminal(h, job) << " poll_errors=" << poll_errors;
    if (poll_error)
      std::cerr << " last_error=" << error_name(poll_error->code) << ':'
                << poll_error->detail;
    if (first_poll_error)
      std::cerr << " first_error=" << error_name(first_poll_error->code) << ':'
                << first_poll_error->detail;
    std::cerr << "\njob snapshot: "
              << unwrap(format_value(h.engine.command_jobs()->read(Value::object(
                     {{"job_id", Value{job}}, {"count", Value{Number{0}}}}))))
              << '\n';
  }
  check(refused && terminal(h, job), "capture refusal lost reserved terminal outcome");
  check(terminal_disposition(h, job) == AttemptDisposition::unknown &&
            terminal_count(h, job) == 1,
        "capture refusal fabricated success or duplicate settlement");
  check(read_file(directory / "launches") == "launch\n" && h.provider.calls == 0,
        "capture refusal replayed command or called provider");
  try {
    h.engine.shutdown_commands();
  } catch (const Error &error) {
    check(error.code == ErrorCode::capacity, "shutdown replaced capture failure");
  }
  const auto pid = static_cast<pid_t>(std::stol(read_file(directory / "pid")));
  check(::kill(pid, 0) < 0 && errno == ESRCH,
        "capture-failed command still alive after shutdown");
}
void quiet_provider(Harness &h, const std::filesystem::path &directory,
                    const std::string &executable) {
  h.engine.cancelled = [] { return false; };
  const auto started =
      h.engine.operator_call("exec", launch_arguments(executable, directory));
  const auto job = string_field(started, "job_id");
  await(
      h, [&] { return std::filesystem::exists(directory / "pid"); },
      "quiet-provider fixture never started");
  h.provider.during = [&] {
    const auto context = h.context.head();
    write_file(directory / "release", "go");
    const auto expires = Clock::now() + std::chrono::seconds{3};
    while (!terminal(h, job)) {
      check(Clock::now() < expires, "quiet provider ticks did not retain completion");
      check(h.provider.cancelled && !h.provider.cancelled(),
            "quiet-provider tick falsely cancelled inference");
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    check(h.context.head() == context, "command pump edited live provider context");
  };
  h.engine.turn({"", "return blackbird.request()"});
  check(h.provider.calls == 1 && terminal_count(h, job) == 1 &&
            retained_bytes(h, job) == prefix + suffix &&
            read_file(directory / "launches") == "launch\n",
        "quiet-provider pumping lost command identity/output");
}
void control_capture_refusal(Harness &h, const std::filesystem::path &directory,
                             const std::string &executable) {
  const auto started = h.engine.operator_call(
      "exec", launch_arguments(executable, directory, "quiet-large"));
  const auto job = string_field(started, "job_id");
  await(
      h,
      [&] {
        return std::filesystem::exists(directory / "pid") &&
               field(h.engine.command_jobs()->read(Value::object(
                         {{"job_id", Value{job}}, {"count", Value{Number{0}}}})),
                     "retained_bytes") == Value{Number{65536}};
      },
      "control-capacity fixture never reached retained quiet state");
  const auto pid = static_cast<pid_t>(std::stol(read_file(directory / "pid")));
  check(!terminal(h, job) && ::kill(pid, 0) == 0,
        "control-capacity fixture settled before read refusal");
  const auto usage = h.root->journal_usage();
  const auto remaining = usage.remaining_bytes();
  const auto reserve = h.root->protected_settlement();
  if (!remaining || !reserve || *remaining <= reserve->max_file_bytes + 21 * 1024)
    throw std::runtime_error("control-capacity padding has insufficient starting room");
  const auto padding =
      static_cast<std::size_t>(*remaining - reserve->max_file_bytes - 21 * 1024);
  h.log.original({"fixture.padding", std::string(padding, 'P'), Value::object({})});
  const auto before = h.root->journal_usage();
  auto read = process("read", job);
  read.object().emplace_back("count", Value{Number{65536}});
  bool refused = false;
  try {
    const auto result = h.engine.operator_call("process", read);
    refused = result.find("error") && field(result, "error") == Value{"capacity"};
  } catch (const Error &error) {
    refused = error.code == ErrorCode::capacity;
    if (!refused)
      std::cerr << "control-capacity unexpected_error=" << error_name(error.code) << ':'
                << error.detail << '\n';
  }
  if (!refused || !terminal(h, job)) {
    std::cerr << "control-capacity before_remaining="
              << before.remaining_bytes().value_or(0)
              << " before_reserved=" << reserve->max_file_bytes
              << " padding=" << padding << " refused=" << refused
              << " terminal=" << terminal(h, job)
              << " state=" << static_cast<unsigned>(h.root->state()) << '\n';
  }
  check(refused && terminal(h, job) && terminal_count(h, job) == 1 &&
            terminal_disposition(h, job) == AttemptDisposition::unknown,
        "process result capture refusal did not settle original launch unknown");
  check(::kill(pid, 0) < 0 && errno == ESRCH,
        "process result capture refusal returned with live child");
  write_file(directory / "release", "go");
  check(!std::filesystem::exists(directory / "effects") &&
            read_file(directory / "launches") == "launch\n" &&
            retained_bytes(h, job) == std::string(65536, 'R') && h.provider.calls == 0,
        "control result refusal lost originals, replayed, or allowed forbidden effect");
}
// Incident arconaut-3o2: a continued (compact-restored) session whose turn runs a
// command that never stops writing. The command must end at its output budget
// with a bounded tool result, and the same turn must go on to its next call.
class FloodTurnProvider final : public CodingProvider {
public:
  Value flood_arguments;
  std::filesystem::path marker;
  unsigned calls = 0;
  std::string second_input;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &) override {
    ++calls;
    Value::Array output;
    if (calls == 1)
      output.push_back(
          Value::object({{"type", Value{"function_call"}},
                         {"call_id", Value{"flood"}},
                         {"name", Value{"exec"}},
                         {"arguments", Value{unwrap(dump_json(flood_arguments))}}}));
    if (calls == 2) {
      second_input = unwrap(dump_json(field(request, "input")));
      output.push_back(Value::object(
          {{"type", Value{"function_call"}},
           {"call_id", Value{"after"}},
           {"name", Value{"write_file"}},
           {"arguments",
            Value{unwrap(dump_json(Value::object(
                {{"path", Value{marker.string()}}, {"content", Value{"next"}}})))}}}));
    }
    return Value::object({{"output", Value{std::move(output)}}});
  }
};
struct NoCustody final : CustodyVerifier {
  Result<void> verify(std::span<const AttemptState> attempts) override {
    return attempts.empty() ? Result<void>::success()
                            : Result<void>::failure({ErrorCode::external_unknown});
  }
};
void output_budget(const std::filesystem::path &directory,
                   const std::string &executable) {
  const auto session = directory / "session";
  std::filesystem::create_directories(session);
  std::filesystem::permissions(session, std::filesystem::perms::owner_all);
  auto storage = [&] {
    return std::make_unique<NativeJournalDirectory>(
        unwrap(NativeJournalDirectory::open(session.string())));
  };
  const JournalHeader header{identity<EnvironmentId>(1),
                             identity<AuditStreamId>(2),
                             3,
                             {32 * 1024 * 1024, 96 * 1024 * 1024},
                             std::nullopt};
  // The product's own audit limits and capacity (main.cpp), not test-sized ones.
  const JournalCapacity capacity{512ULL * 1024 * 1024, 200000};
  {
    auto root = unwrap(RetainedState::create(storage(), "audit", header, capacity));
    AuditLog log{*root};
    ContextStore context{log};
    Provider provider;
    CodingEngine engine{log, context, provider, "fixture"};
    context.append(
        {Value::object({{"role", Value{"user"}}, {"content", Value{"first turn"}}})},
        "first");
    unwrap(context.checkpoint());
  }
  auto root =
      unwrap(RetainedState::open(storage(), "audit", header, capacity, false, false));
  check(root->compact_recovery(), "settled session did not reopen compact");
  unwrap(root->confirm_recovery());
  NoCustody custody;
  unwrap(root->reconcile(custody));
  AuditLog log{*root};
  ContextStore context{log};
  unsigned checkpoints = 0;
  root->set_maintenance_checkpoint([&](const RawEventRef &parent) {
    ++checkpoints;
    return context.checkpoint({parent});
  });
  FloodTurnProvider provider;
  const auto flood = directory / "flood";
  std::filesystem::create_directories(flood);
  provider.flood_arguments = Value::object(
      {{"argv", Value{Value::Array{Value{executable}, Value{"--fixture"},
                                   Value{flood.string()}, Value{"unbounded"}}}},
       {"timeout_seconds", Value{Number{60}}},
       // As the default turn program (programs/turn.lua) presents exec output.
       {"output_max_bytes", Value{Number{16384}}}});
  provider.marker = directory / "after";
  CodingEngine engine{log, context, provider, "fixture"};
  const auto audit_before = std::filesystem::file_size(session / "audit");
  const auto started = Clock::now();
  engine.turn(
      {"continue",
       "for step = 1, 8 do local response = blackbird.request() local calls = 0 "
       "for _, item in ipairs(response.output) do if item.type == 'function_call' "
       "then calls = calls + 1 local result = blackbird.call(item.name, "
       "item.arguments) blackbird.append({{type = 'function_call_output', call_id = "
       "item.call_id, output = result}}) end end if calls == 0 then return end end"});
  const auto elapsed = Clock::now() - started;
  check(provider.calls == 3 && read_file(provider.marker) == "next",
        "turn did not continue to the call after the flooding command");
  Value shown;
  for (const auto &item : unwrap(parse_json(provider.second_input)).array())
    if (item.find("call_id") && field(item, "call_id") == Value{"flood"} &&
        field(item, "type") == Value{"function_call_output"})
      shown = field(item, "output");
  check(std::holds_alternative<Value::Object>(shown.value()),
        "flooding command returned no tool result to the model");
  check(field(shown, "output_limit_exceeded") == Value{true} &&
            field(shown, "output_limit_bytes") == Value{Number{1048576}} &&
            field(shown, "error") == Value{"output_limit"} &&
            field(shown, "effect_outcome") == Value{"unknown"} &&
            field(shown, "stop_requested") == Value{true} &&
            field(shown, "returned_bytes") == Value{Number{16384}},
        "flooding command did not return an explicit bounded output-limit result");
  const auto pid = static_cast<pid_t>(std::stol(read_file(flood / "pid")));
  check(::kill(pid, 0) < 0 && errno == ESRCH, "flooding command outlived its budget");
  // Reading stopped within one read past the budget, and every byte read is
  // retained: the audited original agrees with the reported extents.
  const auto received = std::stoull(field(shown, "received_bytes").number().text());
  check(received > 1048576 && received <= 1048576 + 8192 &&
            field(shown, "output_bytes") == field(shown, "received_bytes") &&
            field(shown, "retained_bytes") == field(shown, "received_bytes"),
        "command read past its budget or lost bytes it read");
  // The whole turn's audit growth is the bounded output plus ordinary turn records.
  const auto audit_growth =
      std::filesystem::file_size(session / "audit") - audit_before;
  check(audit_growth < 1048576 + 512 * 1024,
        "audit growth is not bounded by the budget");
  const auto original = engine.operator_call(
      "read_process_output", Value::object({{"output_ref", field(shown, "output_ref")},
                                            {"output_max_bytes", Value{Number{0}}}}));
  check(field(original, "output_bytes") == field(shown, "received_bytes"),
        "audited original differs from the bytes read");
  // A continued session cannot retire its tail while the command is unresolved;
  // that must not turn every retained chunk into another full checkpoint.
  check(checkpoints <= 4, "unretirable tail re-ran a checkpoint on every append");
  check(elapsed < std::chrono::seconds{30}, "bounded flood took unbounded time");
}
} // namespace
int main(int argc, char **argv) {
  std::string scenario_name = "setup";
  try {
    if (argc == 4 && std::string_view{argv[1]} == "--fixture") {
      scenario_name = "child fixture " + std::string{argv[3]};
      return fixture(argv[2], argv[3]);
    }
    char pattern[] = "/tmp/bb-command-engine-XXXXXX";
    const auto *temporary = ::mkdtemp(pattern);
    check(temporary != nullptr, "test directory creation failed");
    const std::filesystem::path directory{temporary};
    struct Cleanup {
      std::filesystem::path directory;
      ~Cleanup() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
      }
    } cleanup{directory};
    const auto executable = std::filesystem::absolute(argv[0]).string();
    if (argc == 2 && std::string_view{argv[1]} == "--output-budget") {
      scenario_name = "output-budget";
      output_budget(directory, executable);
      return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--retention") {
      scenario_name = "retention";
      Harness h{directory};
      sustainable_foreground(h, directory, executable);
      return 0;
    }
    for (const auto *scenario : {"identity", "deadline", "pty", "capacity",
                                 "quiet-provider", "control-capacity"}) {
      scenario_name = scenario;
      const auto path = directory / scenario;
      std::filesystem::create_directories(path);
      std::filesystem::permissions(path, std::filesystem::perms::owner_all);
      Harness h{path, (std::string_view{scenario} == "capacity" ||
                       std::string_view{scenario} == "control-capacity")
                          ? JournalCapacity{256 * 1024, 20000}
                          : JournalCapacity{32 * 1024 * 1024, 20000}};
      if (std::string_view{scenario} == "identity")
        same_command(h, path, executable);
      else if (std::string_view{scenario} == "deadline")
        deadline(h, path, executable);
      else if (std::string_view{scenario} == "pty")
        pty_delivery(h, path, executable);
      else if (std::string_view{scenario} == "capacity")
        capture_refusal(h, path, executable);
      else if (std::string_view{scenario} == "control-capacity")
        control_capture_refusal(h, path, executable);
      else
        quiet_provider(h, path, executable);
    }
  } catch (const Error &error) {
    std::cerr << scenario_name << ": " << error_name(error.code) << ':' << error.detail
              << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << scenario_name << ": " << error.what() << '\n';
    return 1;
  }
}
