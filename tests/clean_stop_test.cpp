// Clean-stop snapshots (unit S0, arconaut-7c8), driven through the real binary.
// Oracle: the bytes on disk. After every clean exit, and after an orderly error
// exit, the newer saved-state slot's boundary equals the journal end, and the next
// open writes no session.reopen-suffix record. A restart exit is a recorded
// handoff: its successor restores compactly and reports no suffix. After a
// SIGKILL the reopen writes exactly one record naming the suffix it found. The
// saved state is read through load_saved_state over a FramedJournal; no
// ContextStore is built.
#include "blackbird/context.hpp"
#include "blackbird/journal_storage.hpp"
#include "blackbird/journal_writer.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/saved_state.hpp"
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char **environ;
using namespace blackbird;
namespace {
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
std::string executable;
fs::path workspace;
int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
std::string contents(const fs::path &path) {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
void write_text(const fs::path &path, std::string_view text) {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  output << text;
  check(bool(output), "fixture write failed: " + path.string());
}

// One case's isolated home, credentials home, fake participant CLI and workflow.
struct Fixture {
  fs::path root, home, bin, workflow;
  std::vector<std::string> extra; // additional NAME=VALUE environment entries
  explicit Fixture(std::string_view name)
      : root(workspace / name), home(root / "home"), bin(root / "bin"),
        workflow(root / "local.lua") {
    fs::create_directories(home);
    fs::create_directories(bin);
    write_text(bin / "claude", "#!/bin/sh\nsleep 30\n");
    fs::permissions(bin / "claude", fs::perms::owner_all);
    write_text(workflow, "print('LOCAL')\n");
  }
  Fixture &program(std::string_view lua) {
    write_text(workflow, lua);
    return *this;
  }
  fs::path session(std::string_view name) const { return root / name; }
  std::vector<std::string> environment() const {
    std::vector<std::string> result;
    for (char **entry = environ; *entry; ++entry) {
      const std::string_view text{*entry};
      if (text.starts_with("HOME=") || text.starts_with("PATH=") ||
          text.starts_with("BLACKBIRD_") || text.starts_with("ARCO_"))
        continue;
      result.emplace_back(text);
    }
    const char *path = std::getenv("PATH");
    result.push_back("PATH=" + bin.string() + ":" + (path ? path : "/usr/bin:/bin"));
    result.push_back("HOME=" + home.string());
    result.push_back("BLACKBIRD_AUTH_HOME=" + (root / "auth").string());
    result.push_back("BLACKBIRD_GRAPHICS=off");
    result.push_back("BLACKBIRD_JEV_ENV_FILE=" + (root / "absent-env").string());
    result.insert(result.end(), extra.begin(), extra.end());
    return result;
  }
};

// A file name for a case label, which may hold spaces and slashes.
std::string output_name(std::string_view label) {
  std::string name;
  for (const char c : label)
    name += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
  return name + ".out";
}
// How the child's standard streams are connected.
enum class Stdio : std::uint8_t {
  pipe,            // stdin from a pipe, stdout and stderr to the output file
  terminal,        // all three on a PTY (the TUI)
  readonly_stdout, // as pipe, but stdout cannot be written
};
// A child blackbird process.
class Process {
public:
  Process(const Fixture &fixture, std::vector<std::string> arguments, Stdio stdio,
          std::string_view label)
      : output_(fixture.root / output_name(label)) {
    const bool terminal = stdio == Stdio::terminal;
    arguments.insert(arguments.begin(), executable);
    auto environment = fixture.environment();
    std::vector<char *> argv, envp;
    for (auto &argument : arguments)
      argv.push_back(argument.data());
    argv.push_back(nullptr);
    for (auto &entry : environment)
      envp.push_back(entry.data());
    envp.push_back(nullptr);
    int input[2] = {-1, -1};
    std::string slave_name;
    if (terminal) {
      master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
      check(master_ >= 0 && ::grantpt(master_) == 0 && ::unlockpt(master_) == 0,
            "pty allocation failed");
      const char *name = ::ptsname(master_);
      check(name != nullptr, "ptsname failed");
      slave_name = name;
    } else
      check(::pipe(input) == 0, "stdin pipe failed");
    // The child must not inherit the pipe's write end, or it never reads EOF.
    for (const int descriptor : {input[0], input[1], master_})
      if (descriptor >= 0)
        check(::fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 0, "cloexec failed");
    const int out =
        ::open(output_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    check(out >= 0, "output file open failed");
    pid_ = ::fork();
    if (pid_ == 0) {
      if (terminal) {
        ::setsid();
        const int slave = ::open(slave_name.c_str(), O_RDWR);
        if (slave < 0)
          _exit(126);
        (void)::ioctl(slave, TIOCSCTTY, 0);
        winsize size{32, 120, 0, 0};
        (void)::ioctl(slave, TIOCSWINSZ, &size);
        if (::dup2(slave, STDIN_FILENO) < 0 || ::dup2(slave, STDOUT_FILENO) < 0 ||
            ::dup2(slave, STDERR_FILENO) < 0)
          _exit(126);
      } else {
        const int stdout_target =
            stdio == Stdio::readonly_stdout ? ::open("/dev/null", O_RDONLY) : out;
        if (stdout_target < 0 || ::dup2(input[0], STDIN_FILENO) < 0 ||
            ::dup2(stdout_target, STDOUT_FILENO) < 0 || ::dup2(out, STDERR_FILENO) < 0)
          _exit(126);
      }
      ::execve(argv.front(), argv.data(), envp.data());
      _exit(127);
    }
    ::close(out);
    check(pid_ > 0, "fork failed");
    if (!terminal) {
      ::close(input[0]);
      input_ = input[1];
    } else
      input_ = master_;
  }
  ~Process() {
    if (pid_ > 0) {
      (void)::kill(pid_, SIGKILL);
      (void)reap();
    }
    if (input_ >= 0 && input_ != master_)
      ::close(input_);
    if (master_ >= 0)
      ::close(master_);
  }
  Process(const Process &) = delete;
  Process &operator=(const Process &) = delete;

  void send(std::string_view text) {
    while (!text.empty()) {
      const auto written = ::write(input_, text.data(), text.size());
      if (written < 0 && errno == EINTR)
        continue;
      check(written > 0, "child input write failed");
      text.remove_prefix(static_cast<std::size_t>(written));
    }
  }
  void close_input() {
    if (input_ >= 0 && input_ != master_)
      ::close(input_);
    input_ = -1;
  }
  // The TUI blocks once its PTY buffer fills, so the reader drains it.
  void pump(std::chrono::milliseconds slice) {
    if (master_ < 0) {
      std::this_thread::sleep_for(slice);
      return;
    }
    pollfd ready{master_, POLLIN, 0};
    if (::poll(&ready, 1, static_cast<int>(slice.count())) > 0) {
      std::array<char, 65536> bytes{};
      const auto count = ::read(master_, bytes.data(), bytes.size());
      if (count > 0) {
        screen_.append(bytes.data(), static_cast<std::size_t>(count));
        if (screen_.size() > 4 * 1024 * 1024)
          screen_.erase(0, screen_.size() - 1024 * 1024);
      }
    }
  }
  std::string text() const { return master_ >= 0 ? screen_ : contents(output_); }
  // Waits for output, not time: returns the offset just past the first
  // occurrence of needle at or after from.
  std::size_t await_text(std::string_view needle, std::size_t from = 0) {
    const auto label = "waiting for " + std::string{needle};
    const auto deadline = Clock::now() + std::chrono::seconds{20};
    while (Clock::now() < deadline) {
      if (const auto found = text().find(needle, from); found != std::string::npos)
        return found + needle.size();
      if (!running()) {
        // The child may have written the needle just before it exited.
        if (const auto found = text().find(needle, from); found != std::string::npos)
          return found + needle.size();
        throw std::runtime_error(label + ": child exited before output\n" + text());
      }
      pump(std::chrono::milliseconds{20});
    }
    throw std::runtime_error(label + ": output deadline\n" + text());
  }
  bool running() {
    if (pid_ <= 0)
      return false;
    int status = 0;
    const auto result = ::waitpid(pid_, &status, WNOHANG);
    if (result == pid_) {
      pid_ = -1;
      status_ = status;
      return false;
    }
    return true;
  }
  // Returns the exit status; a signal death is reported as 128 + signal.
  int wait(std::string_view label) {
    const auto deadline = Clock::now() + std::chrono::seconds{60};
    while (running()) {
      check(Clock::now() < deadline, std::string{label} + ": exit deadline\n" + text());
      pump(std::chrono::milliseconds{20});
    }
    return WIFEXITED(status_) ? WEXITSTATUS(status_) : 128 + WTERMSIG(status_);
  }
  void interrupt() { check(pid_ > 0 && ::kill(pid_, SIGINT) == 0, "SIGINT failed"); }
  void kill9() {
    check(pid_ > 0 && ::kill(pid_, SIGKILL) == 0, "SIGKILL failed");
    (void)reap();
  }

private:
  int reap() {
    int status = 0;
    while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
    }
    pid_ = -1;
    status_ = status;
    return status;
  }
  fs::path output_;
  pid_t pid_ = -1;
  int input_ = -1, master_ = -1, status_ = 0;
  std::string screen_;
};

int run(const Fixture &fixture, std::string_view label,
        std::vector<std::string> arguments, std::string_view input,
        std::string *output = nullptr) {
  Process process{fixture, std::move(arguments), Stdio::pipe, label};
  process.send(input);
  process.close_input();
  const auto status = process.wait(label);
  if (output)
    *output = process.text();
  return status;
}

constexpr JournalCapacity capacity{512ULL * 1024 * 1024, 200000};
// Both notice forms name a stop that was not clean.
constexpr std::string_view reopen_notice = "the last stop was not clean";
constexpr std::string_view stale_restart_notice = "a restart was left pending";
constexpr std::string_view full_replay = "rebuilding from audit";

// The journal and both saved-state slots, read from disk without a ContextStore.
struct OnDisk {
  NativeJournalDirectory directory;
  std::unique_ptr<FramedJournal> journal;
  std::optional<SavedState> saved;
};
OnDisk read_disk(const fs::path &session) {
  auto directory = unwrap(NativeJournalDirectory::open(session.string()));
  std::array<std::byte, journal_header_size> bytes{};
  std::ifstream file{session / "audit", std::ios::binary};
  file.read(reinterpret_cast<char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  check(bool(file), "journal header read failed: " + session.string());
  const auto header = unwrap(decode_journal_header(bytes));
  auto journal = unwrap(FramedJournal::open(directory, "audit", header, capacity));
  // load_saved_state reads slot 0 and slot 1 and returns the newer valid one.
  auto saved = unwrap(load_saved_state(directory, "audit", *journal, capacity));
  return {std::move(directory), std::move(journal), std::move(saved)};
}

struct Suffix {
  std::uint64_t frames, bytes;
};
Suffix suffix(const OnDisk &disk) {
  const auto end = disk.journal->cursor();
  if (!disk.saved)
    return {end.sequence, end.end_offset - journal_header_size};
  return {end.sequence - disk.saved->boundary.sequence,
          end.end_offset - disk.saved->boundary.end_offset};
}

std::uint64_t number(const Value &record, std::string_view key) {
  const auto *metadata = record.find("metadata");
  const auto *value = metadata ? metadata->find(key) : nullptr;
  check(value && std::holds_alternative<Number>(value->value()),
        "reopen-suffix record lacks metadata." + std::string{key});
  const auto &text = value->number().text();
  std::uint64_t result = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  check(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(),
        "reopen-suffix record number " + std::string{key});
  return result;
}
// Every session.reopen-suffix record on ApplicationChannel::log, in order.
std::vector<Value> reopen_records(const fs::path &session) {
  auto disk = read_disk(session);
  std::vector<Value> result;
  // An unconfirmed open holds its scanned frames as staged records.
  check(disk.journal->physical_records().empty(), "journal open confirmed frames");
  for (const auto &record : disk.journal->staged_records()) {
    if (record.kind != FrameKind::semantic)
      continue;
    const auto payload = unwrap(disk.journal->read_payload(record));
    const auto event = unwrap(
        decode_retained_event(payload, disk.journal->header().limits.max_payload));
    const auto *application = std::get_if<ApplicationRecordEvent>(&event.body);
    if (!application || application->channel != ApplicationChannel::log)
      continue;
    const auto bytes = unwrap(application->payload.read());
    auto packet = unwrap(decode_packet(bytes));
    if (const auto *label = packet.find("label");
        label && std::holds_alternative<std::string>(label->value()) &&
        label->string() == "session.reopen-suffix")
      result.push_back(std::move(packet));
  }
  return result;
}

// Specification: the newer slot's boundary is the journal end. maybe_checkpoint
// runs only on context writes, at 64 frames or 4 MiB; these fixtures end with
// fewer context writes than that after their last snapshot, so only the stop's
// snapshot can close the gap (the old code leaves 4 to 116 frames).
void require_snapshot_at_end(const fs::path &session, std::string_view label) {
  const auto disk = read_disk(session);
  const auto end = disk.journal->cursor();
  if (!disk.saved)
    throw std::runtime_error(std::string{label} + ": no saved-state slot accepted");
  const auto &boundary = disk.saved->boundary;
  const auto gap = suffix(disk);
  check(boundary.sequence == end.sequence && boundary.end_offset == end.end_offset,
        std::string{label} + ": newer slot " + std::to_string(disk.saved->slot) +
            " boundary (sequence " + std::to_string(boundary.sequence) + ", offset " +
            std::to_string(boundary.end_offset) + ") != journal end (sequence " +
            std::to_string(end.sequence) + ", offset " +
            std::to_string(end.end_offset) + "); suffix " + std::to_string(gap.frames) +
            " frames, " + std::to_string(gap.bytes) + " bytes");
}
// The next open finds nothing to report: no new record and no notice. With
// compact, it also restores from the snapshot instead of replaying the journal.
void require_quiet_reopen(const Fixture &fixture, const fs::path &session,
                          std::string_view label, bool compact = true) {
  const auto before = reopen_records(session).size();
  std::string output;
  const auto status =
      run(fixture, std::string{label} + "-reopen",
          {"--session", session.string(), "--once", "/context"}, "", &output);
  check(status == 0, std::string{label} + ": reopen status " + std::to_string(status) +
                         "\n" + output);
  const auto after = reopen_records(session).size();
  check(after == before, std::string{label} + ": reopen wrote " +
                             std::to_string(after - before) +
                             " session.reopen-suffix records\n" + output);
  check(output.find(reopen_notice) == std::string::npos,
        std::string{label} + ": reopen showed a suffix notice\n" + output);
  check(!compact || output.find(full_replay) == std::string::npos,
        std::string{label} + ": reopen replayed the whole journal\n" + output);
}
void require_clean(const Fixture &fixture, const fs::path &session,
                   std::string_view label, bool compact = true) {
  require_snapshot_at_end(session, label);
  require_quiet_reopen(fixture, session, label, compact);
}
// The newer slot ends before the journal: the stop took no snapshot.
void require_no_exit_snapshot(const fs::path &session, std::string_view label) {
  const auto disk = read_disk(session);
  const auto end = disk.journal->cursor();
  check(!disk.saved || disk.saved->boundary.sequence != end.sequence,
        std::string{label} + ": the stop wrote a snapshot at the journal end");
}
// The next open writes exactly one reopen-suffix record naming the suffix on
// disk, restores compactly, and shows the notice; returns the record.
Value require_reported_reopen(const Fixture &fixture, const fs::path &session,
                              std::string_view label, std::string_view notice) {
  const auto before = reopen_records(session).size();
  const auto expected = suffix(read_disk(session));
  std::string output;
  const auto status =
      run(fixture, std::string{label} + "-reported",
          {"--session", session.string(), "--once", "/context"}, "", &output);
  check(status == 0, std::string{label} + ": reopen status " + std::to_string(status) +
                         "\n" + output);
  auto records = reopen_records(session);
  check(records.size() == before + 1,
        std::string{label} + ": reopen wrote " +
            std::to_string(records.size() - before) +
            " session.reopen-suffix records, expected 1\n" + output);
  auto record = std::move(records.back());
  check(number(record, "suffix_frames") == expected.frames &&
            number(record, "suffix_bytes") == expected.bytes,
        std::string{label} + ": record names " +
            std::to_string(number(record, "suffix_frames")) + " frames, " +
            std::to_string(number(record, "suffix_bytes")) +
            " bytes; disk suffix was " + std::to_string(expected.frames) + " frames, " +
            std::to_string(expected.bytes) + " bytes");
  check(output.find(notice) != std::string::npos,
        std::string{label} + ": reopen showed no notice\n" + output);
  check(output.find(full_replay) == std::string::npos,
        std::string{label} + ": reopen replayed the whole journal\n" + output);
  return record;
}
bool metadata_flag(const Value &record, std::string_view key) {
  const auto *metadata = record.find("metadata");
  const auto *value = metadata ? metadata->find(key) : nullptr;
  return value && std::holds_alternative<bool>(value->value()) &&
         std::get<bool>(value->value());
}

constexpr std::string_view participant_request =
    "{\"request\":{\"request_id\":\"r1\",\"from\":\"main\",\"to\":\"alpha\","
    "\"provider\":\"claude\",\"model\":\"fake\",\"task\":\"Review\",\"context\":"
    "[{\"id\":\"source\",\"text\":\"selected\"}],\"profile\":{\"name\":"
    "\"context-only\",\"provenance\":\"clean-stop-test\",\"timeout_seconds\":60,"
    "\"tools\":\"none\",\"requests\":1}}}";
// Starts a background job and a participant; both are still running at exit.
std::string job_and_participant_lua() {
  return "local j=blackbird.call('exec',{command='sleep 30',yield_ms=0}); "
         "local r=blackbird.participants.start(blackbird.decode([=[" +
         std::string{participant_request} +
         "]=])); print('BOTH_STARTED JOB '..j.state..' RUN '..r.run_id..' '..r.state)";
}
// The participant's own run_id is reported next to its running state.
void require_participant_running(std::string_view output, std::string_view label) {
  constexpr std::string_view prefix = " RUN ";
  bool found = false;
  for (auto at = output.find(prefix); at != std::string_view::npos && !found;
       at = output.find(prefix, at + 1)) {
    const auto id = output.substr(at + prefix.size(), 32);
    found = id.size() == 32 &&
            id.find_first_not_of("0123456789abcdef") == std::string_view::npos &&
            output.substr(at + prefix.size() + 32).starts_with(" running");
  }
  check(found,
        std::string{label} + ": no running participant run_id\n" + std::string{output});
}
const std::string append_line =
    "/lua blackbird.append({{role='user',content='CLEAN_STOP_CONTEXT'}})\n";
// Context, a background job, a running participant and a turn, then the exit.
const std::string busy =
    append_line + "/lua " + job_and_participant_lua() + "\nhello\n";

void plain_exit(std::string_view name, std::string_view last) {
  Fixture fixture{name};
  const auto session = fixture.session("s");
  std::string output;
  const auto status = run(fixture, name,
                          {"--plain", "--session", session.string(), "--workflow",
                           fixture.workflow.string()},
                          busy + std::string{last}, &output);
  check(status == 0,
        std::string{name} + ": exit status " + std::to_string(status) + "\n" + output);
  require_participant_running(output, name);
  require_clean(fixture, session, name);
}

void quit_case() { plain_exit("quit", "/quit\n"); }
void exit_case() { plain_exit("exit", "/exit\n"); }
void eof_case() { plain_exit("eof", ""); }

void once_case() {
  Fixture fixture{"once"};
  const auto session = fixture.session("s");
  check(run(fixture, "once-setup",
            {"--session", session.string(), "--workflow", fixture.workflow.string(),
             "--once", "hello"},
            "") == 0,
        "once: first run failed");
  // The second --once returns with a background job and a participant running.
  std::string output;
  const auto status = run(
      fixture, "once",
      {"--session", session.string(), "--once", "/lua " + job_and_participant_lua()},
      "", &output);
  check(status == 0, "once: exit status " + std::to_string(status) + "\n" + output);
  require_participant_running(output, "once");
  require_clean(fixture, session, "once");
}

// A restart exit leaves a pending session.restart program: a recorded handoff.
// The successor restores compactly, writes no reopen-suffix record and shows no
// notice; its own clean exit then snapshots.
void require_restart_handoff(const Fixture &fixture, const fs::path &session,
                             std::vector<std::string> successor, std::string_view label,
                             bool compact) {
  require_no_exit_snapshot(session, label);
  const auto before = reopen_records(session).size();
  std::string output;
  const auto status = run(fixture, std::string{label} + "-successor",
                          std::move(successor), "", &output);
  check(status == 0, std::string{label} + ": successor status " +
                         std::to_string(status) + "\n" + output);
  check(!compact || output.find("rebuilding from audit") == std::string::npos,
        std::string{label} + ": successor did not restore compactly\n" + output);
  check(reopen_records(session).size() == before,
        std::string{label} + ": successor wrote a reopen-suffix record\n" + output);
  check(output.find(reopen_notice) == std::string::npos,
        std::string{label} + ": successor showed a suffix notice\n" + output);
  require_clean(fixture, session, std::string{label} + " successor", compact);
}

constexpr std::string_view restart_workflow =
    "local items=blackbird.context().entries\n"
    "local last=items[#items].item.content\n"
    "if type(last)=='string' and last:find('Restart/resume note:',1,true) then\n"
    "  print('RESUMED_OK')\n"
    "else\n"
    "  blackbird.call('restart',{note='CLEAN_STOP_RESTART'})\n"
    "end\n";

void restart_and_resume_once_case() {
  Fixture fixture{"restart-once"};
  fixture.program(restart_workflow);
  const auto session = fixture.session("s");
  std::string output;
  const auto status = run(fixture, "restart-once",
                          {"--session", session.string(), "--workflow",
                           fixture.workflow.string(), "--once", "START"},
                          "", &output);
  check(status == 75, "restart exit: status " + std::to_string(status) + "\n" + output);
  require_restart_handoff(fixture, session,
                          {"--session", session.string(), "--resume-once"},
                          "restart exit 75 (--once)", true);
}

void resume_once_nothing_pending_case() {
  Fixture fixture{"resume-once-idle"};
  const auto session = fixture.session("s");
  check(run(fixture, "resume-idle-setup",
            {"--session", session.string(), "--workflow", fixture.workflow.string(),
             "--once", "hello"},
            "") == 0,
        "resume-once idle: setup failed");
  std::string output;
  const auto status =
      run(fixture, "resume-once-idle", {"--session", session.string(), "--resume-once"},
          "", &output);
  check(status == 0,
        "resume-once idle: status " + std::to_string(status) + "\n" + output);
  require_clean(fixture, session, "--resume-once with nothing pending");
}

// Past at least one maintenance checkpoint (64 frames), then /restart: the
// successor must restore from that snapshot, not replay the whole journal.
void interactive_restart_case() {
  Fixture fixture{"restart-plain"};
  const auto session = fixture.session("s");
  std::string input;
  for (int entry = 0; entry < 40; ++entry)
    input += "/lua blackbird.append({{role='user',content='ENTRY_" +
             std::to_string(entry) + "'}})\n";
  std::string output;
  const auto status = run(fixture, "restart-plain",
                          {"--plain", "--session", session.string(), "--workflow",
                           fixture.workflow.string()},
                          input + "/restart CLEAN_STOP_NOTE\n", &output);
  check(status == 75, "/restart: status " + std::to_string(status) + "\n" + output);
  {
    const auto disk = read_disk(session);
    check(disk.saved && disk.saved->boundary.sequence > 64,
          "/restart: fixture never passed a maintenance checkpoint");
  }
  require_restart_handoff(fixture, session,
                          {"--session", session.string(), "--resume-once"},
                          "/restart exit 75", true);
}

void write_atomically(const fs::path &path, std::string_view text) {
  const auto staged = path.string() + ".tmp";
  write_text(staged, text);
  fs::rename(staged, path);
}

// The station's event workflow starts a job and a participant and writes a
// marker; the stop control is written only after the marker appears.
void station_stop_case() {
  Fixture fixture{"station-stop"};
  const auto marker = fixture.root / "event-done";
  fixture.program(job_and_participant_lua() + "\nblackbird.call('write_file',{path='" +
                  marker.string() + "',content='done'})\n");
  const auto session = fixture.session("s");
  const auto adapter = fixture.root / "adapter";
  fs::create_directories(adapter);
  write_atomically(
      adapter / "events.json",
      R"([{"source":"repo","id":"e1","cursor":"c1","prompt":"inventory"}])");
  Process process{fixture,
                  {"--session", session.string(), "--workflow",
                   fixture.workflow.string(), "--station", adapter.string()},
                  Stdio::pipe,
                  "station-stop"};
  process.close_input();
  const auto deadline = Clock::now() + std::chrono::seconds{20};
  while (!fs::exists(marker)) {
    check(process.running() && Clock::now() < deadline,
          "station stop: event workflow never finished\n" + process.text());
    process.pump(std::chrono::milliseconds{20});
  }
  require_participant_running(process.text(), "station stop");
  write_atomically(adapter / "control.json", R"({"id":"stop-1","action":"stop"})");
  const auto status = process.wait("station-stop");
  check(status == 0,
        "station stop: status " + std::to_string(status) + "\n" + process.text());
  // Station programs keep the snapshot from compact restore (unchanged by S0).
  require_clean(fixture, session, "station stop", false);
}

void station_restart_case() {
  Fixture fixture{"station-restart"};
  fixture.program("blackbird.call('restart',{note='STATION_CLEAN_STOP'})\n");
  const auto session = fixture.session("s");
  const auto adapter = fixture.root / "adapter";
  fs::create_directories(adapter);
  write_atomically(
      adapter / "events.json",
      R"([{"source":"repo","id":"e1","cursor":"c1","prompt":"inventory"}])");
  std::string output;
  const auto status = run(fixture, "station-restart",
                          {"--session", session.string(), "--workflow",
                           fixture.workflow.string(), "--station", adapter.string()},
                          "", &output);
  check(status == 75,
        "station restart: status " + std::to_string(status) + "\n" + output);
  // The successor resumes the station; the stop control ends it. Station
  // programs keep a snapshot from compact restore, so only the record is checked.
  write_atomically(adapter / "control.json", R"({"id":"stop-1","action":"stop"})");
  require_restart_handoff(fixture, session,
                          {"--session", session.string(), "--resume-continue"},
                          "station restart exit 75", false);
}

// Session switches and /restart refuse while jobs or participants run
// (validate_session_switch), so these exits carry neither.
void new_session_case() {
  Fixture fixture{"new"};
  const auto session = fixture.session("s");
  std::string output;
  const auto status = run(fixture, "new",
                          {"--plain", "--session", session.string(), "--workflow",
                           fixture.workflow.string()},
                          append_line + "/new\n", &output);
  check(status == 0, "/new: status " + std::to_string(status) + "\n" + output);
  require_clean(fixture, session, "/new (old session)");
  std::vector<fs::path> fresh;
  for (const auto &entry : fs::directory_iterator{fixture.root})
    if (entry.path().filename().string().starts_with("session-"))
      fresh.push_back(entry.path());
  check(fresh.size() == 1, "/new: expected one fresh session\n" + output);
  require_clean(fixture, fresh.front(), "/new (fresh session at EOF)");
}

void resume_session_case() {
  Fixture fixture{"resume"};
  const auto origin = fixture.session("a"), destination = fixture.session("b");
  check(run(fixture, "resume-setup",
            {"--session", destination.string(), "--workflow", fixture.workflow.string(),
             "--once", "hello"},
            "") == 0,
        "/resume: destination setup failed");
  std::string output;
  const auto status =
      run(fixture, "resume",
          {"--plain", "--session", origin.string(), "--workflow",
           fixture.workflow.string()},
          append_line + "/resume " + destination.string() + "\n", &output);
  check(status == 0, "/resume: status " + std::to_string(status) + "\n" + output);
  require_clean(fixture, origin, "/resume (origin)");
  require_clean(fixture, destination, "/resume (destination at EOF)");
}

// A busy destination sends the process back to the origin, which reopens and
// writes after the switch; its exit snapshot must cover those writes.
void resume_fallback_case() {
  Fixture fixture{"resume-fallback"};
  const auto origin = fixture.session("a"), destination = fixture.session("b");
  check(run(fixture, "fallback-setup",
            {"--session", destination.string(), "--workflow", fixture.workflow.string(),
             "--once", "hello"},
            "") == 0,
        "/resume fallback: destination setup failed");
  const int held = ::open((destination / "audit").c_str(), O_RDONLY | O_CLOEXEC);
  check(held >= 0 && ::flock(held, LOCK_EX | LOCK_NB) == 0,
        "/resume fallback: destination lock failed");
  std::string output;
  const auto status =
      run(fixture, "resume-fallback",
          {"--plain", "--session", origin.string(), "--workflow",
           fixture.workflow.string()},
          append_line + "/resume " + destination.string() + "\n", &output);
  ::close(held);
  check(status == 0,
        "/resume fallback: status " + std::to_string(status) + "\n" + output);
  check(output.find("returning to the previous session") != std::string::npos,
        "/resume fallback: the destination opened\n" + output);
  require_clean(fixture, origin, "/resume fallback (origin reopened)");
}

void tui_case() {
  Fixture fixture{"tui"};
  const auto session = fixture.session("s");
  Process process{
      fixture,
      {"--session", session.string(), "--workflow", fixture.workflow.string()},
      Stdio::terminal,
      "tui"};
  auto at = process.await_text("Workflow");
  process.send("/lua blackbird.append({{role='user',content='TUI_CONTEXT'}}); "
               "print('TUI_APPENDED')\r");
  at = process.await_text("TUI_APPENDED", at);
  process.send("/lua " + job_and_participant_lua() + "\r");
  at = process.await_text("BOTH_STARTED", at);
  (void)process.await_text(" running", at);
  process.send("/quit\r");
  const auto status = process.wait("tui");
  check(status == 0, "tui: status " + std::to_string(status));
  require_clean(fixture, session, "TUI /quit");
}

// A refused invocation writes nothing: the clean session stays clean.
void bad_option_case() {
  Fixture fixture{"bad-option"};
  const auto session = fixture.session("s");
  check(run(fixture, "bad-option-setup",
            {"--plain", "--session", session.string(), "--workflow",
             fixture.workflow.string()},
            "hello\n/checkpoint\n") == 0,
        "bad option: setup failed");
  require_snapshot_at_end(session, "bad option (setup)");
  std::string output;
  const auto status =
      run(fixture, "bad-option",
          {"--session", session.string(), "--provider", "anthropic", "--once", "hello"},
          "", &output);
  check(status == 1 &&
            output.find("Changing providers requires --model") != std::string::npos,
        "bad option: status " + std::to_string(status) + "\n" + output);
  require_clean(fixture, session, "bad option");
}

// A fake provider transport: curl that prints body then the HTTP status.
void fake_provider(Fixture &fixture, std::string_view status) {
  const auto curl = fixture.bin / "curl";
  write_text(curl, "#!/bin/sh\ncat > /dev/null\n"
                   "printf '{\"error\":{\"message\":\"fixture\"}}\\n" +
                       std::string{status} + "'\n");
  fs::permissions(curl, fs::perms::owner_all);
  fixture.extra = {"OPENROUTER_API_KEY=fixture-secret",
                   "BLACKBIRD_OPENROUTER_CURL=" + curl.string()};
}
const std::vector<std::string> provider_once = {
    "--provider", "openrouter", "--model", "openai/gpt-6.1-sol", "--once", "hello"};

// An orderly error exit: the provider refuses (401), the --once turn fails with
// its attempt settled, and the exit still snapshots the live journal.
void provider_failure_case() {
  Fixture fixture{"provider-failure"};
  fake_provider(fixture, "401");
  const auto session = fixture.session("s");
  auto arguments = provider_once;
  arguments.insert(arguments.begin(), {"--session", session.string()});
  std::string output;
  const auto status = run(fixture, "provider-failure", arguments, "", &output);
  check(status == 1 && output.find("provider_auth") != std::string::npos,
        "provider failure: status " + std::to_string(status) + "\n" + output);
  require_clean(fixture, session, "failing --once provider");
}

// An exception leaves the provider attempt admitted but unsettled: SIGINT while
// the fake transport runs ends the --once turn with "interrupted". The exit
// takes no snapshot; the next open restores compactly from the older covered
// slot and reports the suffix.
void unsettled_attempt_case() {
  Fixture fixture{"unsettled-attempt"};
  const auto curl = fixture.bin / "curl";
  const auto in_flight = fixture.root / "request-in-flight";
  write_text(curl, "#!/bin/sh\ntouch '" + in_flight.string() +
                       "'\ncat > /dev/null\nsleep 30\n");
  fs::permissions(curl, fs::perms::owner_all);
  fixture.extra = {"OPENROUTER_API_KEY=fixture-secret",
                   "BLACKBIRD_OPENROUTER_CURL=" + curl.string()};
  const auto session = fixture.session("s");
  auto arguments = provider_once;
  arguments.insert(arguments.begin(), {"--session", session.string()});
  int status = 0;
  std::string output;
  {
    Process process{fixture, arguments, Stdio::pipe, "unsettled-attempt"};
    process.close_input();
    // The transport has started, so the provider attempt is admitted and open.
    const auto deadline = Clock::now() + std::chrono::seconds{20};
    while (!fs::exists(in_flight)) {
      check(process.running() && Clock::now() < deadline,
            "unsettled attempt: the provider request never started\n" + process.text());
      process.pump(std::chrono::milliseconds{20});
    }
    process.interrupt();
    status = process.wait("unsettled-attempt");
    output = process.text();
  }
  check(status == 1 && output.find("interrupted") != std::string::npos,
        "unsettled attempt: status " + std::to_string(status) + "\n" + output);
  require_no_exit_snapshot(session, "unsettled attempt");
  (void)require_reported_reopen(fixture, session, "unsettled attempt", reopen_notice);
  require_clean(fixture, session, "unsettled attempt (after recovery)");
}

// Exit 75 with no relaunch, then a plain open: the open reports the restart it
// found pending and does not settle it.
void stale_restart_case() {
  Fixture fixture{"stale-restart"};
  fixture.program(restart_workflow);
  const auto session = fixture.session("s");
  std::string output;
  const auto status = run(fixture, "stale-restart",
                          {"--session", session.string(), "--workflow",
                           fixture.workflow.string(), "--once", "START"},
                          "", &output);
  check(status == 75,
        "stale restart: status " + std::to_string(status) + "\n" + output);
  require_no_exit_snapshot(session, "stale restart (exit 75)");
  const auto record =
      require_reported_reopen(fixture, session, "stale restart", stale_restart_notice);
  check(metadata_flag(record, "pending_restart"),
        "stale restart: record lacks metadata.pending_restart");
  // That open's exit snapshots; the restart it left pending keeps the session
  // off compact restore until a resuming open consumes it.
  require_clean(fixture, session, "stale restart (after the report)", false);
}

// /restart is recorded, then the exit fails (stdout cannot be written): an
// orderly error after the restart. The next plain open reports it pending.
void restart_then_error_case() {
  Fixture fixture{"restart-error"};
  fixture.program(restart_workflow);
  const auto session = fixture.session("s");
  check(run(fixture, "restart-error-setup",
            {"--session", session.string(), "--workflow", fixture.workflow.string(),
             "--once", "/context"},
            "") == 0,
        "restart then error: setup failed");
  int status = 0;
  std::string output;
  {
    Process process{fixture,
                    {"--session", session.string(), "--once", "START"},
                    Stdio::readonly_stdout,
                    "restart-error"};
    process.close_input();
    status = process.wait("restart-error");
    output = process.text();
  }
  check(status == 1,
        "restart then error: status " + std::to_string(status) + "\n" + output);
  require_no_exit_snapshot(session, "restart then error");
  const auto record = require_reported_reopen(fixture, session, "restart then error",
                                              stale_restart_notice);
  check(metadata_flag(record, "pending_restart"),
        "restart then error: record lacks metadata.pending_restart");
}

// SIGKILL with a tail past maybe_checkpoint's 64 frames, then a refused
// invocation: the refusal must not move the saved boundary, and the next valid
// open still reports the whole tail.
void kill_long_tail_case() {
  Fixture fixture{"kill-long-tail"};
  const auto session = fixture.session("s");
  {
    Process process{fixture,
                    {"--plain", "--session", session.string(), "--workflow",
                     fixture.workflow.string()},
                    Stdio::pipe,
                    "kill-long-tail"};
    // Settings writes are not context writes, so no maintenance snapshot runs.
    std::string input;
    for (int name = 0; name < 40; ++name)
      input += "/name TAIL_" + std::to_string(name) + "\n";
    process.send(input + "/lua print('TAIL_WRITTEN')\n");
    const auto at = process.await_text("TAIL_WRITTEN");
    (void)process.await_text("blackbird> ", at);
    process.kill9();
  }
  const auto tail = suffix(read_disk(session));
  check(tail.frames >= 64,
        "kill long tail: tail of only " + std::to_string(tail.frames) + " frames");
  std::string output;
  const auto status =
      run(fixture, "kill-long-tail-refused",
          {"--session", session.string(), "--provider", "anthropic", "--once", "hello"},
          "", &output);
  check(status == 1,
        "kill long tail: refused status " + std::to_string(status) + "\n" + output);
  const auto after = suffix(read_disk(session));
  check(after.frames == tail.frames && after.bytes == tail.bytes,
        "kill long tail: the refused open moved the suffix from " +
            std::to_string(tail.frames) + " to " + std::to_string(after.frames) +
            " frames");
  (void)require_reported_reopen(fixture, session, "kill long tail", reopen_notice);
}

std::string frame(const Value &packet) {
  const auto bytes = unwrap(encode_packet(packet));
  std::string result(4, '\0');
  const auto size = static_cast<std::uint32_t>(bytes.size());
  for (std::size_t i = 0; i < 4; ++i)
    result[i] = static_cast<char>((size >> (8 * (3 - i))) & 0xffU);
  result.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  return result;
}
// Opens and closes the native application interface; returns its exit status.
int application_session(const Fixture &fixture, const fs::path &session,
                        std::string_view label, std::string &output) {
  Process process{fixture, {"--application"}, Stdio::pipe, label};
  process.send(frame(Value::object({{"type", Value{"open"}},
                                    {"id", Value{"open"}},
                                    {"session", Value{session.string()}}})));
  (void)process.await_text("ready");
  process.send(
      frame(Value::object({{"type", Value{"close"}}, {"id", Value{"close"}}})));
  (void)process.await_text("closed");
  process.close_input();
  const auto status = process.wait(label);
  output = process.text();
  return status;
}

void application_case() {
  Fixture fixture{"application"};
  const auto session = fixture.session("s");
  check(run(fixture, "application-setup",
            {"--session", session.string(), "--workflow", fixture.workflow.string(),
             "--once", "hello"},
            "") == 0,
        "application: setup failed");
  std::string output;
  const auto status = application_session(fixture, session, "application", output);
  check(status == 0, "application: status " + std::to_string(status) + "\n" + output);
  require_clean(fixture, session, "application close");
}

// The close snapshot is best-effort: its failure is reported and the close
// still completes with status 0. The slot it would write is a directory here.
void application_snapshot_failure_case() {
  Fixture fixture{"application-failure"};
  const auto session = fixture.session("s");
  check(run(fixture, "application-failure-setup",
            {"--session", session.string(), "--workflow", fixture.workflow.string(),
             "--once", "hello"},
            "") == 0,
        "application failure: setup failed");
  unsigned next = 0;
  {
    const auto disk = read_disk(session);
    check(disk.saved.has_value(), "application failure: no snapshot after setup");
    next = disk.saved ? disk.saved->slot ^ 1U : 0U;
  }
  const auto blocked = session / ("audit.state." + std::to_string(next));
  fs::remove_all(blocked);
  fs::create_directories(blocked / "occupied");
  std::string output;
  const auto status =
      application_session(fixture, session, "application-failure", output);
  check(status == 0 &&
            output.find("Application close snapshot failed") != std::string::npos,
        "application failure: status " + std::to_string(status) + "\n" + output);
  check(output.find("clean-stop snapshot failed") == std::string::npos &&
            output.find("Clean-stop snapshot failed") == std::string::npos,
        "application failure: the guard retried the close snapshot\n" + output);
  fs::remove_all(blocked);
  (void)require_reported_reopen(fixture, session, "application failure", reopen_notice);
}

// SIGKILL is unclean: the reopen writes exactly one record naming the suffix it
// found, and a clean reopen after that writes none.
void kill_case() {
  Fixture fixture{"kill"};
  const auto session = fixture.session("s");
  {
    Process process{fixture,
                    {"--plain", "--session", session.string(), "--workflow",
                     fixture.workflow.string()},
                    Stdio::pipe,
                    "kill"};
    process.send("/lua blackbird.append({{role='user',content='BEFORE_KILL'}}); "
                 "print('APPENDED_BEFORE_KILL')\n");
    const auto at = process.await_text("APPENDED_BEFORE_KILL");
    // The next prompt means the command returned and its writes are committed.
    (void)process.await_text("blackbird> ", at);
    process.kill9();
  }
  const auto expected = suffix(read_disk(session));
  check(expected.frames > 0 && expected.bytes > 0, "kill: no suffix after SIGKILL");
  check(reopen_records(session).empty(), "kill: reopen-suffix record before reopen");
  std::string output;
  const auto status =
      run(fixture, "kill-reopen", {"--session", session.string(), "--once", "/context"},
          "", &output);
  check(status == 0 && output.find("BEFORE_KILL") != std::string::npos,
        "kill: reopen status " + std::to_string(status) + "\n" + output);
  const auto records = reopen_records(session);
  check(records.size() == 1, "kill: reopen wrote " + std::to_string(records.size()) +
                                 " session.reopen-suffix records, expected 1\n" +
                                 output);
  const auto &record = records.front();
  check(number(record, "suffix_frames") == expected.frames &&
            number(record, "suffix_bytes") == expected.bytes &&
            number(record, "torn_bytes") == 0,
        "kill: record names " + std::to_string(number(record, "suffix_frames")) +
            " frames, " + std::to_string(number(record, "suffix_bytes")) +
            " bytes; disk suffix was " + std::to_string(expected.frames) + " frames, " +
            std::to_string(expected.bytes) + " bytes");
  const auto *metadata = record.find("metadata");
  const auto *snapshot = metadata ? metadata->find("snapshot") : nullptr;
  check(snapshot && std::holds_alternative<std::string>(snapshot->value()) &&
            snapshot->string() == "accepted",
        "kill: record does not name an accepted snapshot");
  check(output.find(reopen_notice) != std::string::npos,
        "kill: reopen showed no suffix notice\n" + output);
  require_clean(fixture, session, "kill (after the recovering reopen)");
}

void run_case(std::string_view name, void (*body)()) {
  try {
    body();
    std::cout << "PASS " << name << '\n';
  } catch (const std::exception &error) {
    ++failures;
    std::cout << "FAIL " << name << ": " << error.what() << '\n';
  } catch (const Error &error) {
    ++failures;
    std::cout << "FAIL " << name << ": " << error_name(error.code) << " ("
              << error.detail << ")\n";
  }
  std::cout.flush();
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: clean_stop_test BLACKBIRD [CASE...]\n";
    return 2;
  }
  executable = fs::absolute(argv[1]).string();
  std::string pattern =
      (fs::temp_directory_path() / "blackbird-clean-stop-XXXXXX").string();
  check(::mkdtemp(pattern.data()) != nullptr, "mkdtemp failed");
  workspace = fs::canonical(pattern);
  const std::array<std::pair<std::string_view, void (*)()>, 22> cases{{
      {"quit", quit_case},
      {"exit", exit_case},
      {"eof", eof_case},
      {"tui", tui_case},
      {"once", once_case},
      {"restart-once", restart_and_resume_once_case},
      {"resume-once-idle", resume_once_nothing_pending_case},
      {"restart-plain", interactive_restart_case},
      {"station-stop", station_stop_case},
      {"station-restart", station_restart_case},
      {"new", new_session_case},
      {"resume", resume_session_case},
      {"resume-fallback", resume_fallback_case},
      {"bad-option", bad_option_case},
      {"provider-failure", provider_failure_case},
      {"unsettled-attempt", unsettled_attempt_case},
      {"stale-restart", stale_restart_case},
      {"restart-error", restart_then_error_case},
      {"kill-long-tail", kill_long_tail_case},
      {"application", application_case},
      {"application-failure", application_snapshot_failure_case},
      {"kill", kill_case},
  }};
  for (const auto &[name, body] : cases) {
    bool selected = argc == 2;
    for (int i = 2; i < argc; ++i)
      selected = selected || name == argv[i];
    if (selected)
      run_case(name, body);
  }
  std::error_code ignored;
  if (failures == 0)
    fs::remove_all(workspace, ignored);
  else
    std::cout << "Fixtures kept in " << workspace.string() << '\n';
  return failures == 0 ? 0 : 1;
}
