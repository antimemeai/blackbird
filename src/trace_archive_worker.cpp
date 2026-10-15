#include "blackbird/foundation.hpp"
#include "blackbird/trace_archive.hpp"
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
namespace blackbird {
std::unique_ptr<TraceArchiveWorker>
TraceArchiveWorker::start(const std::filesystem::path &session) {
  if (!std::filesystem::exists(session / "trace-archive.bbm"))
    return {};
  auto owner = std::unique_ptr<TraceArchiveWorker>{new TraceArchiveWorker{}};
  int endpoints[2];
  if (::pipe(endpoints) != 0)
    throw Error{ErrorCode::io, errno};
  for (int &fd : endpoints) {
    const int normalized = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (normalized < 0) {
      const int error = errno;
      (void)::close(endpoints[0]);
      (void)::close(endpoints[1]);
      throw Error{ErrorCode::io, error};
    }
    (void)::close(fd);
    fd = normalized;
  }
  posix_spawn_file_actions_t actions;
  int code = ::posix_spawn_file_actions_init(&actions);
  if (code == 0) {
    posix_spawnattr_t attributes;
    code = ::posix_spawnattr_init(&attributes);
    const bool attributes_ready = code == 0;
    if (code == 0)
      code = ::posix_spawnattr_setflags(&attributes,
                                        POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK);
    sigset_t mask;
    sigemptyset(&mask);
    if (code == 0)
      code = ::posix_spawnattr_setsigmask(&attributes, &mask);
    if (code == 0)
      code = ::posix_spawnattr_setpgroup(&attributes, 0);
    if (code == 0)
      code = ::posix_spawn_file_actions_adddup2(&actions, endpoints[0], STDIN_FILENO);
    if (code == 0)
      code = ::posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                                O_WRONLY, 0);
    if (code == 0)
      code = ::posix_spawn_file_actions_addopen(
          &actions, STDERR_FILENO, (session / "trace-archive.log").c_str(),
          O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW, 0600);
    pid_t process = -1;
    std::string executable = BLACKBIRD_ARCHIVE_EXECUTABLE;
    std::string operation = "watch", path = session.string();
    char *args[]{executable.data(), operation.data(), path.data(), nullptr};
    if (code == 0)
      code = ::posix_spawn(&process, executable.c_str(), &actions, &attributes, args,
                           environ);
    if (attributes_ready)
      (void)::posix_spawnattr_destroy(&attributes);
    (void)::posix_spawn_file_actions_destroy(&actions);
    (void)::close(endpoints[0]);
    if (code == 0) {
      owner->input_ = endpoints[1];
      owner->process_ = process;
      return owner;
    }
  } else
    (void)::close(endpoints[0]);
  (void)::close(endpoints[1]);
  throw Error{ErrorCode::io, code};
}
TraceArchiveWorker::~TraceArchiveWorker() {
  if (input_ >= 0)
    (void)::close(input_);
  if (process_ > 0)
    while (::waitpid(process_, nullptr, 0) < 0 && errno == EINTR) {
    }
}
} // namespace blackbird
