// CTest case launcher: runs one registered case and takes every process the
// case started down with it. cmake/BlackbirdTests.cmake puts it in front of
// every case; the rationale and limits are in tooling/README.md.
//
// CTest's own timeout kill (kwsys) stops the case's direct child and every
// process it can find through `ps` parent links. A daemon that has already
// been reparented to init (tui-test's session daemon) is not linked, and when
// the case's own process has exited CTest kills nothing at all, while it
// waits for the output pipe that a surviving child still holds.
//
// This launcher starts the case in its own process group, marks the case's
// environment with a unique BLACKBIRD_TEST_CASE entry, and identifies the
// case's processes by
//   - that entry in a process's initial environment (it survives setsid and
//     reparenting; tui-test passes its environment to the terminal target),
//   - membership of the case's process group (it survives reparenting),
//   - a parent, process group or session that is already identified.
// A descendant that both clears its environment and leaves the case's process
// group before its parent exits is not identified.
// It ends the case's tree when CTest signals the timeout (TIMEOUT_SIGNAL_NAME
// SIGTERM), when the terminal interrupts it, when its runner dies, and when
// the case exits while processes it started are still alive. That last case
// is reported and fails the case.
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <random>
#include <set>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#else
#error "case_launcher reads the process table on macOS and Linux only"
#endif

extern char **environ;

namespace {
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr std::string_view case_variable = "BLACKBIRD_TEST_CASE";
// Processes the case started may take a moment to finish after it exits.
constexpr milliseconds settle_limit{5000};
// After CTest's timeout signal the case gets this long to clean up itself.
constexpr milliseconds forward_limit{4000};
constexpr milliseconds kill_limit{5000};

struct Root {
  pid_t pid = -1;
  bool reaped = false;
  int status = 0;
};

struct Process {
  pid_t pid = 0;
  pid_t ppid = 0;
  pid_t pgid = 0;
  pid_t sid = 0;
  bool zombie = false;
  bool marked = false;
  std::string command;
};

int wake[2]{-1, -1};
volatile std::sig_atomic_t received = 0;

void on_signal(int number) {
  const int saved = errno;
  if (number != SIGCHLD)
    received = number;
  (void)::write(wake[1], "x", 1);
  errno = saved;
}

void drain() {
  char buffer[64];
  while (::read(wake[0], buffer, sizeof(buffer)) > 0) {
  }
}

void nap(milliseconds limit) {
  pollfd descriptor{wake[0], POLLIN, 0};
  (void)::poll(&descriptor, 1, static_cast<int>(limit.count()));
  drain();
}

[[noreturn]] void fail(const char *what) {
  std::fprintf(stderr, "case_launcher: %s: %s\n", what, std::strerror(errno));
  std::_Exit(125);
}

#if defined(__APPLE__)
// Reads argv and the initial environment through KERN_PROCARGS2. A process
// whose arguments cannot be read (another user, exiting) yields nothing.
void arguments(Process &process, std::string_view marker) {
  static std::vector<char> buffer = [] {
    int limit = 0;
    std::size_t size = sizeof(limit);
    int name[2]{CTL_KERN, KERN_ARGMAX};
    if (::sysctl(name, 2, &limit, &size, nullptr, 0) != 0 || limit <= 0)
      fail("KERN_ARGMAX");
    return std::vector<char>(static_cast<std::size_t>(limit));
  }();
  int name[3]{CTL_KERN, KERN_PROCARGS2, process.pid};
  std::size_t size = buffer.size();
  if (::sysctl(name, 3, buffer.data(), &size, nullptr, 0) != 0 || size < sizeof(int))
    return;
  int count = 0;
  std::memcpy(&count, buffer.data(), sizeof(count));
  std::size_t at = sizeof(count);
  while (at < size && buffer[at] != '\0') // executable path
    ++at;
  while (at < size && buffer[at] == '\0')
    ++at;
  for (int index = 0; at < size; ++index) {
    const auto *end =
        static_cast<const char *>(std::memchr(buffer.data() + at, '\0', size - at));
    const std::string_view entry{
        buffer.data() + at, end == nullptr
                                ? size - at
                                : static_cast<std::size_t>(end - (buffer.data() + at))};
    if (index >= count && entry.empty())
      break;
    if (index < count) {
      if (!process.command.empty())
        process.command += ' ';
      process.command += entry;
    } else if (entry == marker) {
      process.marked = true;
    }
    at += entry.size() + 1;
  }
}

// The process list comes from libproc. sysctl(KERN_PROC), which ps and CTest's
// own kill use, was measured blocking uninterruptibly for minutes on this
// host under load while proc_listallpids and PROC_PIDTBSDINFO answered in
// milliseconds.
std::vector<Process> table(std::string_view marker) {
  std::vector<pid_t> pids;
  for (int attempt = 0;; ++attempt) {
    const int count = ::proc_listallpids(nullptr, 0);
    if (count <= 0)
      fail("proc_listallpids");
    pids.resize(static_cast<std::size_t>(count) + 64);
    const int found =
        ::proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    if (found <= 0)
      fail("proc_listallpids");
    if (static_cast<std::size_t>(found) < pids.size() || attempt == 16) {
      pids.resize(static_cast<std::size_t>(found));
      break;
    }
  }
  const uid_t user = ::getuid();
  std::vector<Process> result;
  result.reserve(pids.size());
  for (const pid_t pid : pids) {
    proc_bsdinfo info{};
    if (::proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) !=
            static_cast<int>(sizeof(info)) ||
        info.pbi_uid != user)
      continue; // exited, or another user's
    Process process;
    process.pid = pid;
    process.ppid = static_cast<pid_t>(info.pbi_ppid);
    process.pgid = static_cast<pid_t>(info.pbi_pgid);
    process.sid = ::getsid(pid);
    process.zombie = info.pbi_status == SZOMB;
    if (!process.zombie)
      arguments(process, marker);
    result.push_back(std::move(process));
  }
  return result;
}
#else
std::vector<std::string> fields(const std::string &path) {
  std::ifstream input{path, std::ios::binary};
  const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
  std::vector<std::string> result;
  std::size_t begin = 0;
  while (begin < bytes.size()) {
    const auto end = bytes.find('\0', begin);
    const auto stop = end == std::string::npos ? bytes.size() : end;
    result.emplace_back(bytes, begin, stop - begin);
    begin = stop + 1;
  }
  return result;
}

std::vector<Process> table(std::string_view marker) {
  std::vector<Process> result;
  DIR *directory = ::opendir("/proc");
  if (directory == nullptr)
    fail("/proc");
  while (const dirent *entry = ::readdir(directory)) {
    const std::string_view name{entry->d_name};
    if (name.empty() || !std::all_of(name.begin(), name.end(),
                                     [](char c) { return c >= '0' && c <= '9'; }))
      continue;
    const std::string base = "/proc/" + std::string{name};
    struct stat owner{};
    if (::stat(base.c_str(), &owner) != 0 || owner.st_uid != ::getuid())
      continue;
    std::ifstream input{base + "/stat"};
    const std::string line{std::istreambuf_iterator<char>{input}, {}};
    const auto paren = line.rfind(')');
    if (paren == std::string::npos)
      continue;
    Process process;
    process.pid = static_cast<pid_t>(std::stol(std::string{name}));
    char state = 0;
    long ppid = 0, pgid = 0, sid = 0;
    if (std::sscanf(line.c_str() + paren + 1, " %c %ld %ld %ld", &state, &ppid, &pgid,
                    &sid) != 4)
      continue;
    process.ppid = static_cast<pid_t>(ppid);
    process.pgid = static_cast<pid_t>(pgid);
    process.sid = static_cast<pid_t>(sid);
    process.zombie = state == 'Z' || state == 'X';
    for (const auto &argument : fields(base + "/cmdline")) {
      if (!process.command.empty())
        process.command += ' ';
      process.command += argument;
    }
    for (const auto &variable : fields(base + "/environ"))
      if (variable == marker)
        process.marked = true;
    result.push_back(std::move(process));
  }
  ::closedir(directory);
  return result;
}
#endif

// The live processes of a case: those carrying its marker, those in the
// process group the launcher created for it (anchor; POSIX does not reuse a
// pid while a process group with that ID exists), and those linked by parent,
// process group or session to one already identified. The launcher and its
// ancestors are never selected.
std::vector<Process> members(std::string_view marker, pid_t anchor) {
  const auto all = table(marker);
  const pid_t self = ::getpid();
  std::set<pid_t> excluded{self, 0, 1};
  for (pid_t at = ::getppid(); at > 1 && excluded.insert(at).second;) {
    const auto parent = std::find_if(all.begin(), all.end(),
                                     [at](const Process &p) { return p.pid == at; });
    at = parent == all.end() ? 0 : parent->ppid;
  }
  std::set<pid_t> chosen;
  for (bool grew = true; grew;) {
    grew = false;
    for (const auto &process : all) {
      if (excluded.contains(process.pid) || chosen.contains(process.pid))
        continue;
      if (process.marked || process.ppid == self ||
          (anchor > 1 && process.pgid == anchor) || chosen.contains(process.ppid) ||
          chosen.contains(process.pgid) || chosen.contains(process.sid)) {
        chosen.insert(process.pid);
        grew = true;
      }
    }
  }
  std::vector<Process> result;
  for (const auto &process : all)
    if (chosen.contains(process.pid) && !process.zombie)
      result.push_back(process);
  return result;
}

void report(const char *heading, const std::vector<Process> &processes) {
  std::fprintf(stderr, "case_launcher: %s %zu process%s:\n", heading, processes.size(),
               processes.size() == 1 ? "" : "es");
  for (const auto &process : processes) {
    std::string command = process.command.empty() ? "?" : process.command;
    if (command.size() > 400)
      command = command.substr(0, 400) + "...";
    std::fprintf(stderr, "  pid %d ppid %d: %s\n", static_cast<int>(process.pid),
                 static_cast<int>(process.ppid), command.c_str());
  }
  std::fflush(stderr);
}

// Kills every identified process and repeats until a scan finds none. A child
// started between a scan and the kill inherits the marker and process group
// and is found by the next scan. Nothing is left stopped if the launcher is
// itself killed part way. Returns the processes still alive at the kill limit.
std::vector<Process> end_tree(std::string_view marker, Root &root) {
  const auto deadline = Clock::now() + kill_limit;
  for (;;) {
    auto left = members(marker, root.pid);
    for (const auto &process : left)
      (void)::kill(process.pid, SIGKILL);
    if (!root.reaped && ::waitpid(root.pid, &root.status, WNOHANG) == root.pid)
      root.reaped = true;
    if ((left.empty() && root.reaped) || Clock::now() >= deadline)
      return left;
    nap(milliseconds{20});
  }
}

[[noreturn]] void finish(const Root &root, bool leaked) {
  const int status = root.status;
  std::fflush(nullptr);
  if (WIFSIGNALED(status)) {
    // Report the case's own signal to CTest without leaving a core file.
    const rlimit none{0, 0};
    (void)::setrlimit(RLIMIT_CORE, &none);
    const int number = WTERMSIG(status);
    (void)::signal(number, SIG_DFL);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, number);
    (void)::sigprocmask(SIG_UNBLOCK, &set, nullptr);
    (void)::raise(number);
  }
  const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
  std::_Exit(leaked && code == 0 ? 1 : code);
}

[[noreturn]] void terminate(int number, std::string_view marker, Root &root) {
  // CTest's timeout signal goes to the case's own process, which may clean up;
  // the rest of its tree is ended below. A terminal or session signal goes to
  // the case's whole process group, as the terminal would have sent it.
  if (number == SIGTERM && !root.reaped)
    (void)::kill(root.pid, SIGTERM);
  else if (number != SIGTERM)
    (void)::killpg(root.pid, number);
  const auto deadline = Clock::now() + forward_limit;
  while (!root.reaped && Clock::now() < deadline) {
    if (::waitpid(root.pid, &root.status, WNOHANG) == root.pid)
      root.reaped = true;
    else
      nap(milliseconds{50});
  }
  const auto alive = members(marker, root.pid);
  if (!alive.empty())
    report(number == SIGTERM ? "timeout signal: ending" : "interrupted: ending", alive);
  const auto left = end_tree(marker, root);
  if (!left.empty())
    report("could not end", left);
  std::fflush(nullptr);
  (void)::signal(number, SIG_DFL);
  (void)::raise(number);
  std::_Exit(128 + number);
}

std::string token() {
  std::random_device device;
  std::string result = std::to_string(::getpid()) + '-';
  constexpr std::string_view hex = "0123456789abcdef";
  for (int index = 0; index < 16; ++index)
    result += hex[device() % hex.size()];
  return result;
}

int orphans(std::string_view entry) {
  const auto found = members(entry, -1);
  if (found.empty())
    return 0;
  report("left behind by the suite", found);
  Root none{-1, true, 0};
  const auto left = end_tree(entry, none);
  if (!left.empty())
    report("could not end", left);
  return 1;
}
} // namespace

int main(int argc, char **argv) {
  if (argc == 3 && std::string_view{argv[1]} == "--orphans")
    return orphans(argv[2]);
  if (argc < 3 || std::string_view{argv[1]} != "--") {
    std::fprintf(stderr, "usage: case_launcher -- PROGRAM [ARGS...]\n"
                         "       case_launcher --orphans NAME=VALUE\n");
    return 125;
  }
  const std::string value = token();
  const std::string marker = std::string{case_variable} + '=' + value;
  if (::setenv(std::string{case_variable}.c_str(), value.c_str(), 1) != 0)
    fail("setenv");
  if (::pipe(wake) != 0)
    fail("pipe");
  for (const int descriptor : wake)
    if (::fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0 ||
        ::fcntl(descriptor, F_SETFL, O_NONBLOCK) != 0)
      fail("fcntl");
  struct sigaction action{};
  action.sa_handler = on_signal;
  sigemptyset(&action.sa_mask);
  for (const int number : {SIGCHLD, SIGTERM, SIGINT, SIGHUP, SIGQUIT})
    if (::sigaction(number, &action, nullptr) != 0)
      fail("sigaction");
  const pid_t parent = ::getppid();
  // The case gets its own process group; descendants that do not leave it
  // remain identifiable after their parents exit.
  posix_spawnattr_t attributes;
  if (::posix_spawnattr_init(&attributes) != 0 ||
      ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) != 0 ||
      ::posix_spawnattr_setpgroup(&attributes, 0) != 0)
    fail("posix_spawnattr");
  Root root;
  const int error =
      ::posix_spawnp(&root.pid, argv[2], nullptr, &attributes, argv + 2, environ);
  (void)::posix_spawnattr_destroy(&attributes);
  if (error != 0) {
    errno = error;
    std::fprintf(stderr, "case_launcher: cannot start %s: %s\n", argv[2],
                 std::strerror(error));
    return 127;
  }
  // Reports must not kill the launcher once a dead runner closed the pipe.
  (void)::signal(SIGPIPE, SIG_IGN);
  for (;;) {
    if (received != 0)
      terminate(received, marker, root);
    if (::waitpid(root.pid, &root.status, WNOHANG) == root.pid) {
      root.reaped = true;
      break;
    }
    if (::getppid() != parent) {
      (void)end_tree(marker, root);
      return 1;
    }
    nap(milliseconds{200});
  }
  const auto deadline = Clock::now() + settle_limit;
  for (;;) {
    if (received != 0)
      terminate(received, marker, root);
    if (::getppid() != parent) {
      (void)end_tree(marker, root);
      return 1;
    }
    const auto alive = members(marker, root.pid);
    if (alive.empty())
      finish(root, false);
    if (Clock::now() >= deadline) {
      report("case exited and left", alive);
      const auto left = end_tree(marker, root);
      if (!left.empty())
        report("could not end", left);
      finish(root, true);
    }
    nap(milliseconds{50});
  }
}
