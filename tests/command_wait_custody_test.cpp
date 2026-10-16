#include "blackbird/command_jobs.hpp"
#include "blackbird/context.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/raw_reader.hpp"
#include <atomic>
#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace blackbird;
namespace {
using Clock = std::chrono::steady_clock;
using NativeWait = int (*)(idtype_t, id_t, siginfo_t *, int);
using NativeReap = pid_t (*)(pid_t, int *, int);
using NativeKill = int (*)(pid_t, int);
NativeWait native_wait = nullptr;
NativeReap native_reap = nullptr;
NativeKill native_kill = nullptr;
std::filesystem::path gate;
std::atomic<pid_t> watched{-1};
std::atomic_bool cut{false}, pinned{false}, externally_reaped{false};
std::atomic_bool lost_case{false};
std::atomic_uint filtered_losses{0}, exit_losses{0}, owner_reaps{0}, unsafe_signals{0};
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char temporary[] = "/tmp/blackbird-wait-selector-XXXXXX";
    require(::mkdtemp(temporary) != nullptr, "private selector fixture");
    path = temporary;
  }
  ~Directory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};
int child(const std::filesystem::path &directory) {
  const int input = ::open((directory / "gate").c_str(), O_RDWR);
  if (input < 0)
    return 91;
  const int ready = ::open((directory / "ready").c_str(), O_CREAT | O_WRONLY, 0600);
  if (ready < 0)
    return 92;
  (void)::close(ready);
  char release = 0;
  ssize_t count;
  do {
    count = ::read(input, &release, 1);
  } while (count < 0 && errno == EINTR);
  (void)::close(input);
  return count == 1 && release == 'X' ? 0 : 93;
}
void pin_exit(pid_t pid) {
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  while (!std::filesystem::exists(gate.parent_path() / "ready")) {
    require(Clock::now() < deadline, "actual child reaches FIFO gate");
    std::this_thread::yield();
  }
  const int release = ::open(gate.c_str(), O_WRONLY | O_NONBLOCK);
  require(release >= 0 && ::write(release, "X", 1) == 1,
          "release only acquired private child");
  require(::close(release) == 0, "close private release endpoint");
  siginfo_t exited{};
  do {
    require(native_wait(P_PID, static_cast<id_t>(pid), &exited,
                        WEXITED | WNOHANG | WNOWAIT) == 0,
            "real exit observation preserves acquired child pin");
    if (exited.si_pid == pid)
      break;
    require(Clock::now() < deadline, "actual child exits within fixture deadline");
    std::this_thread::yield();
  } while (true);
  require(exited.si_code == CLD_EXITED && exited.si_status == 0,
          "actual held child exited successfully");
  pinned = true;
  if (lost_case) {
    int status = -1;
    require(native_reap(pid, &status, 0) == pid && WIFEXITED(status) &&
                WEXITSTATUS(status) == 0,
            "external fixture reaps exactly its own acquired child");
    externally_reaped = true;
  }
}
template <class T> T field(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "complete raw selector field");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
std::pair<IdentityBytes, std::uint64_t> reference(ByteView &bytes) {
  const auto lane = field<IdentityBytes>(bytes);
  return {lane, field<std::uint64_t>(bytes)};
}
void retained_waits(const std::filesystem::path &directory, pid_t pid) {
  const auto runs = unwrap(discover_raw_runs(directory.string()));
  require(runs.size() == 1, "one actual raw run");
  const auto &snapshot = runs[0].snapshot;
  if (!snapshot)
    throw std::runtime_error("raw run snapshot is unavailable");
  require(snapshot->closed, "actual raw run is closed");
  auto reader = unwrap(RawRunReader::open((directory / "raw" / runs[0].name).string()));
  std::vector<std::byte> storage(1024 * 1024);
  std::map<std::pair<IdentityBytes, std::uint64_t>, int> selectors;
  bool filtered = false, exited_after = false;
  unsigned reaps = 0;
  while (const auto record = unwrap(reader->read(storage))) {
    auto bytes = record->raw;
    (void)field<std::uint32_t>(bytes);
    const auto site = field<std::uint32_t>(bytes);
    const auto ordinal = field<std::uint64_t>(bytes);
    if (site == 2022) {
      (void)reference(bytes);
      const auto type = field<idtype_t>(bytes);
      const auto selected = field<id_t>(bytes);
      const auto options = field<int>(bytes);
      if (type == P_PID && selected == static_cast<id_t>(pid))
        selectors[{record->origin.lane, ordinal}] = options;
    } else if (site == 2023) {
      const auto query = reference(bytes);
      const auto selected = selectors.find(query);
      if (selected == selectors.end())
        continue;
      const auto result = field<int>(bytes), error = field<int>(bytes);
      const auto observed = field<pid_t>(bytes);
      (void)field<uid_t>(bytes);
      const auto code = field<int>(bytes), status = field<int>(bytes);
      if ((selected->second & WEXITED) == 0 && result == -1 && error == ECHILD)
        filtered = true;
      if (filtered && (selected->second & WEXITED) != 0 && result == 0 &&
          observed == pid && code == CLD_EXITED && status == 0)
        exited_after = true;
    } else if (site == 2033) {
      (void)reference(bytes);
      const auto result = field<pid_t>(bytes);
      (void)field<int>(bytes);
      const auto valid = field<bool>(bytes);
      const auto status = field<int>(bytes);
      if (valid && result == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0)
        ++reaps;
    }
  }
  require(filtered && exited_after && reaps == 1,
          "raw exact filtered ECHILD, later exit observation and one real reap");
}
void scenario(const std::filesystem::path &executable, bool lost, bool raw) {
  Directory directory;
  gate = directory.path / "gate";
  require(::mkfifo(gate.c_str(), 0600) == 0, "private child release FIFO");
  lost_case = lost;
  std::unique_ptr<RawRuntime> runtime;
  std::optional<RawThreadBinding> binding;
  if (raw) {
    IdentityBytes identity{};
    identity[0] = std::byte{51};
    runtime = unwrap(RawRuntime::open(directory.path.string(),
                                      unwrap(EnvironmentId::from_bytes(identity))));
    binding.emplace(unwrap(runtime->prepare_thread()));
  }
  const std::string id(32, '7');
  Value state;
  {
    CommandJobs jobs;
    jobs.start(
        id,
        Value::object(
            {{"argv", Value{Value::Array{Value{executable.string()}, Value{"--child"},
                                         Value{directory.path.string()}}}},
             {"timeout_seconds", Value{Number{10}}}}));
    const auto pump = [&] {
      for (const auto &event : jobs.drain())
        if (event.bytes)
          jobs.retained(event.job_id, event.offset + event.bytes->size());
        else
          jobs.settled(event.job_id, event.result);
    };
    (void)jobs.wait(id, {10000, 4096, false}, pump, [] { return false; });
    jobs.shutdown();
    pump();
    state = jobs.read(Value::object({{"job_id", Value{id}}}));
  }
  binding.reset();
  runtime.reset();
  const auto pid = watched.load();
  require(pid > 0 && cut && pinned, "real child exit crosses selected wait boundary");
  // Preserve fixture hygiene even when the old owner fails the oracle below.
  siginfo_t remaining{};
  const int check = native_wait(P_PID, static_cast<id_t>(pid), &remaining,
                                WEXITED | WNOHANG | WNOWAIT);
  bool fixture_reaped = false;
  if (check == 0 && remaining.si_pid == pid) {
    int status = 0;
    require(native_reap(pid, &status, 0) == pid, "clean only stranded owned fixture");
    fixture_reaped = true;
  } else
    require(check == -1 && errno == ECHILD, "no live or unrelated fixture remains");
  require(detail::owned_children.load() == 0,
          "owned child counter released exactly once");
  require(unsafe_signals == 0, "actual lost custody prohibits later numeric signals");
  if (lost) {
    require(externally_reaped && exit_losses != 0 && owner_reaps == 0 &&
                field(state, "sealed") == Value{false} && !fixture_reaped,
            "real exit-inclusive ECHILD revokes custody without invented native reap");
  } else {
    require(filtered_losses != 0 && owner_reaps == 1 && !fixture_reaped &&
                field(state, "sealed") == Value{true} &&
                field(state, "native_custody_pending") == Value{false},
            "filtered ECHILD preserves authority until exact final native reap");
    if (raw)
      retained_waits(directory.path, pid);
  }
}
} // namespace
extern "C" int waitid(idtype_t type, id_t selector, siginfo_t *observed, int options) {
  const int result = native_wait(type, selector, observed, options);
  const int error = errno;
  if (type == P_PID && (options & WEXITED) != 0 && result == 0 &&
      observed->si_pid == 0 && !cut.exchange(true)) {
    const auto pid = static_cast<pid_t>(selector);
    watched = pid;
    pin_exit(pid);
  }
  if (type == P_PID && selector == static_cast<id_t>(watched.load())) {
    if ((options & WEXITED) == 0 && pinned) {
#ifdef __APPLE__
      // Reproduce Linux's permitted filtered error only after a real exited pin.
      if (!lost_case) {
        *observed = {};
        errno = ECHILD;
        ++filtered_losses;
        return -1;
      }
#endif
      if (result == -1 && error == ECHILD)
        ++filtered_losses;
    }
    if ((options & WEXITED) != 0 && result == -1 && error == ECHILD)
      ++exit_losses;
  }
  errno = error;
  return result;
}
extern "C" pid_t waitpid(pid_t selector, int *status, int options) {
  const auto result = native_reap(selector, status, options);
  const int error = errno;
  const auto pid = watched.load();
  if (pid > 0 && selector == pid && result == pid)
    ++owner_reaps;
  errno = error;
  return result;
}
extern "C" int kill(pid_t target, int signal) {
  const auto pid = watched.load();
  if (externally_reaped && (target == pid || target == -pid)) {
    ++unsafe_signals;
    errno = ESRCH;
    return -1;
  }
  return native_kill(target, signal);
}
int main(int argc, char **argv) {
  try {
    if (argc == 3 && std::string_view{argv[1]} == "--child")
      return child(argv[2]);
    require(argc == 2, "one private wait selector mode");
    native_wait = reinterpret_cast<NativeWait>(::dlsym(RTLD_NEXT, "waitid"));
    native_reap = reinterpret_cast<NativeReap>(::dlsym(RTLD_NEXT, "waitpid"));
    native_kill = reinterpret_cast<NativeKill>(::dlsym(RTLD_NEXT, "kill"));
    require(native_wait && native_reap && native_kill,
            "actual native syscall adapters");
    const std::string_view mode{argv[1]};
    require(mode == "filtered" || mode == "filtered-raw" || mode == "lost",
            "known selector fixture mode");
    scenario(std::filesystem::absolute(argv[0]), mode == "lost",
             mode == "filtered-raw");
    std::cout << "PASS " << mode << '\n';
  } catch (const Error &error) {
    std::cerr << "FAIL native " << error_name(error.code) << ':' << error.detail
              << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
