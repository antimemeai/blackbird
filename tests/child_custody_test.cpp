#include "../src/native_process.hpp"
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

using namespace blackbird;
namespace {
int signal_count = 0;
pid_t signal_target = 0;
bool forbid_signal = false;
using Kill = int (*)(pid_t, int);
Kill native_kill = nullptr;
using Wait = int (*)(idtype_t, id_t, siginfo_t *, int);
Wait native_wait = nullptr;
bool refuse_observation = false;
unsigned refused_observations = 0;
void require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}
template <class T> T take(Result<T> result) {
  require(result.has_value(), "native fixture operation succeeds");
  return std::move(result).value();
}
void take(Result<void> result) {
  require(result.has_value(), "native fixture operation succeeds");
}
void policy(int flags, void (*handler)(int)) {
  struct sigaction action{};
  action.sa_handler = handler;
  action.sa_flags = flags;
  sigemptyset(&action.sa_mask);
  require(sigaction(SIGCHLD, &action, nullptr) == 0, "set private SIGCHLD policy");
}
void default_child() {
  pid_t leader = -1;
  {
    detail::Child child;
    child.start({"/bin/sh", "-c", "echo $$; printf 'owned-output'; exit 7"});
    child.close_input();
    leader = static_cast<pid_t>(
        std::stoi(child.line(detail::Clock::now() + std::chrono::seconds(3), 4096)));
    int status = -1;
    require(child.collect(detail::Clock::now() + std::chrono::seconds(3), 4096,
                          &status) == "owned-output",
            "exact actual child output");
    require(status == 7 && child.spawned(), "actual nonzero exit retained");
    require(detail::owned_children.load() == 1, "unreaped child remains owned");
  }
  require(signal_count == 1 && signal_target == -leader,
          "real cleanup targets the acquired group");
  int status = 0;
  require(waitpid(leader, &status, WNOHANG) == -1 && errno == ECHILD,
          "normal cleanup really reaped its leader");
  require(detail::locally_quiescent(), "normal cleanup releases custody");
}
void refused_policy(bool ignored) {
  policy(ignored ? 0 : SA_NOCLDWAIT, ignored ? SIG_IGN : SIG_DFL);
  bool rejected = false;
  {
    detail::Child child;
    try {
      child.start({"/usr/bin/true"});
    } catch (const Error &error) {
      rejected = error.code == ErrorCode::unsupported;
    }
    require(rejected && !child.spawned(), "unsafe parent policy rejected prelaunch");
  }
  struct sigaction current{};
  require(sigaction(SIGCHLD, nullptr, &current) == 0, "read unchanged policy");
  require(ignored ? current.sa_handler == SIG_IGN
                  : (current.sa_flags & SA_NOCLDWAIT) != 0,
          "host policy was not silently normalized");
  require(signal_count == 0 && detail::owned_children.load() == 0,
          "refusal launched no owned work");
  require(detail::locally_quiescent(), "prelaunch refusal creates no cleanup debt");
}
void lost_child(bool collect) {
  forbid_signal = true;
  {
    detail::Child child;
    child.start({"/bin/sh", "-c", "echo $$"});
    child.close_input();
    const auto pid = static_cast<pid_t>(
        std::stoi(child.line(detail::Clock::now() + std::chrono::seconds(3), 4096)));
    int status = -1;
    pid_t result;
    do {
      result = waitpid(pid, &status, 0);
    } while (result < 0 && errno == EINTR);
    require(result == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "external reap of exact acquired real child");
    require(detail::owned_children.load() == 1, "owner not yet informed of reap");
    if (collect) {
      bool unavailable = false;
      try {
        (void)child.collect(detail::Clock::now() + std::chrono::seconds(3), 4096);
      } catch (const Error &error) {
        unavailable = error.code == ErrorCode::io && error.detail == ECHILD;
      }
      require(unavailable, "actual ECHILD reaches caller");
      require(child.spawned(), "lost custody does not erase historical launch");
      require(detail::uncontained_exec.load(), "loss immediately fences handoff");
    }
  }
  require(signal_count == 0, "lost custody prohibits every later group signal");
  require(detail::owned_children.load() == 0 && !detail::locally_quiescent(),
          "counter release does not claim cleanup after ownership loss");
}
void cleanup_error() {
  forbid_signal = true;
  pid_t leader = -1;
  {
    detail::Child child;
    child.start({"/bin/sh", "-c", "echo $$; exec sleep 3"});
    child.close_input();
    leader = static_cast<pid_t>(
        std::stoi(child.line(detail::Clock::now() + std::chrono::seconds(3), 4096)));
    refuse_observation = true;
  }
  const bool fenced = !detail::locally_quiescent();
  // Fixture cleanup can only signal if this exact direct child is still unreaped.
  siginfo_t observed{};
  const int owned = native_wait(P_PID, static_cast<id_t>(leader), &observed,
                                WEXITED | WNOHANG | WNOWAIT);
  if (owned == 0) {
    (void)native_kill(-leader, SIGKILL);
    int status = 0;
    pid_t result;
    do {
      result = waitpid(leader, &status, 0);
    } while (result < 0 && errno == EINTR);
    require(result == leader, "fixture reaps only its still-owned child");
  } else
    require(errno == ECHILD, "fixture already reaped by original owner");
  require(refused_observations == 1 && signal_count == 0,
          "one failed custody observation forbids every later signal");
  require(detail::owned_children.load() == 0 && fenced,
          "failed final custody observation keeps uncertainty after owner release");
}
template <class T> T field(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "complete raw native field");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
void raw_cleanup() {
  const auto name = "/bb-child-custody-" + std::to_string(::getpid());
  IdentityBytes lane{};
  lane[0] = std::byte{19};
  auto channel = take(RawChannel::create(name, lane, {65536, 4096, 4096}));
  RawProducer producer{channel};
  pid_t leader = -1;
  {
    RawThreadBinding binding{producer, lane};
    std::optional<RawControlClaim> pressure;
    {
      detail::Child child;
      child.raw_capture({});
      child.start({"/bin/sh", "-c", "echo $$"});
      child.close_input();
      leader = static_cast<pid_t>(std::stoi(
          child.collect(detail::Clock::now() + std::chrono::seconds(3), 4096)));
      pressure.emplace(
          take(producer.hold_control(take(producer.available(RawPartition::control)))));
      require(take(producer.available(RawPartition::control)) == 0,
              "all ordinary control capacity withheld before cleanup");
    }
    require(signal_count == 1 && signal_target == -leader,
            "prepaid cleanup still signals acquired group");
  }
  auto output = take(channel.read(RawPartition::control, 0, 4096));
  take(RawChannel::unlink(name));
  ByteView bytes{output.bytes};
  unsigned waits = 0;
  bool waited = false, signalled = false, reaped = false;
  while (!bytes.empty()) {
    const auto length = field<std::uint32_t>(bytes);
    const auto site = field<std::uint32_t>(bytes);
    (void)field<std::uint64_t>(bytes);
    require(length >= 16 && length - 16 <= bytes.size(), "complete raw frame");
    ByteView body = bytes.first(length - 16);
    bytes = bytes.subspan(length - 16);
    require(body.size() >= 24, "raw child reference present");
    body = body.subspan(24);
    if (site == 1405) {
      ++waits;
      require(field<std::uint8_t>(body) == 1 && field<int>(body) == 0,
              "actual waitid success captured");
      (void)field<int>(body);
      const auto observed = field<pid_t>(body);
      if (observed == 0) {
        waited = false;
        continue;
      }
      require(observed == leader, "actual observed owned leader");
      (void)field<uid_t>(body);
      require(field<int>(body) == CLD_EXITED && field<int>(body) == 0,
              "actual exit observation retained");
      waited = true;
    } else if (site == 1407) {
      require(waits >= 2 && waited && field<pid_t>(body) == leader &&
                  field<int>(body) == SIGKILL,
              "collect and final custody waits precede actual group signal");
      signalled = true;
    } else if (site == 1408) {
      require(signalled && field<pid_t>(body) == leader && field<pid_t>(body) == leader,
              "actual reap follows group cleanup");
      reaped = true;
    }
  }
  require(waits >= 2 && reaped && detail::locally_quiescent(),
          "final wait/signal/reap preserved without new control admission");
}
} // namespace
extern "C" int kill(pid_t target, int signal) {
  ++signal_count;
  signal_target = target;
  if (forbid_signal) {
    errno = EPERM;
    return -1;
  }
  return native_kill(target, signal);
}
extern "C" int waitid(idtype_t type, id_t child, siginfo_t *observed, int options) {
  if (refuse_observation) {
    refuse_observation = false;
    ++refused_observations;
    *observed = {};
    errno = EINTR;
    return -1;
  }
  return native_wait(type, child, observed, options);
}
int main(int argc, char **argv) {
  require(argc == 2, "one private mode required");
  native_kill = reinterpret_cast<Kill>(dlsym(RTLD_NEXT, "kill"));
  native_wait = reinterpret_cast<Wait>(dlsym(RTLD_NEXT, "waitid"));
  require(native_kill != nullptr && native_wait != nullptr,
          "resolve native calls for owned successful case");
  policy(0, SIG_DFL);
  if (std::strcmp(argv[1], "default") == 0)
    default_child();
  else if (std::strcmp(argv[1], "ignored") == 0)
    refused_policy(true);
  else if (std::strcmp(argv[1], "no-cldwait") == 0)
    refused_policy(false);
  else if (std::strcmp(argv[1], "lost-collect") == 0)
    lost_child(true);
  else if (std::strcmp(argv[1], "lost-destructor") == 0)
    lost_child(false);
  else if (std::strcmp(argv[1], "cleanup-error") == 0)
    cleanup_error();
  else if (std::strcmp(argv[1], "raw-cleanup") == 0)
    raw_cleanup();
  else
    require(false, "known mode required");
  std::printf("PASS: %s\n", argv[1]);
}
