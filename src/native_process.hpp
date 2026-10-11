#pragma once
#include "blackbird/foundation.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/raw_provider.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#ifndef F_SETNOSIGPIPE
#include <pthread.h>
#endif

extern char **environ;
namespace blackbird::detail {
[[noreturn]] inline void fail(ErrorCode code, std::int64_t detail = 0) {
  throw Error{code, detail};
}
using Clock = std::chrono::steady_clock;
#ifndef F_SETNOSIGPIPE
// Suppress only this thread's new SIGPIPE while writing an owned child pipe.
class PipeSignalGuard {
public:
  PipeSignalGuard() {
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    const int rc = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (rc != 0)
      fail(ErrorCode::io, rc);
    sigset_t pending;
    sigpending(&pending);
    was_pending = sigismember(&pending, SIGPIPE) == 1;
  }
  ~PipeSignalGuard() {
    if (!was_pending) {
      timespec zero{};
      while (sigtimedwait(&blocked, nullptr, &zero) < 0 && errno == EINTR) {
      }
    }
    (void)pthread_sigmask(SIG_SETMASK, &previous, nullptr);
  }

private:
  sigset_t blocked{}, previous{};
  bool was_pending = false;
};
#endif
class Child {
public:
  std::function<void(std::string_view)> output_observer;
  std::function<bool()> cancelled;
  std::function<void(std::string_view)> error_observer;
  std::string error_bytes;
  std::filesystem::path cwd;
  std::vector<std::string> child_environment;
  bool separate_error = false;
  bool spawned() const noexcept { return group > 0; }
  Error deadline_error{ErrorCode::io, ETIMEDOUT};
  Child() = default;
  void raw_capture(RawEventRef request, bool stdout_bytes = true,
                   bool stderr_bytes = false) {
    raw_options = RawCaptureOptions{request, stdout_bytes, stderr_bytes};
  }
  RawEventRef raw_reference() const noexcept {
    return trace ? trace->reference() : RawEventRef{};
  }
  Child(const Child &) = delete;
  Child &operator=(const Child &) = delete;
  ~Child() {
    close_input();
    if (output >= 0)
      close_owned(output, 1);
    if (error_output >= 0)
      close_owned(error_output, 2);
    // collect() observes with WNOWAIT; pin the leader through the final group action.
    if (group > 0) {
      const int result = ::kill(-group, SIGKILL);
      const int error = errno;
      if (trace)
        trace->signal(group, SIGKILL, result, error);
    }
    if (pid > 0) {
      int status = 0;
      pid_t result;
      do {
        result = ::waitpid(pid, &status, 0);
      } while (result < 0 && errno == EINTR);
      const int error = errno;
      if (trace)
        trace->reap(pid, result, error, result == pid, status);
    }
    if (group > 0)
      --owned_children;
  }
  void start(std::vector<std::string> args,
             const std::filesystem::path &codex_home = {}, bool merge_error = false) {
    try {
      if (raw_options)
        trace.emplace(raw_options->request, raw_options->stdout_bytes,
                      raw_options->stderr_bytes, merge_error, separate_error,
                      deadline_error);
      if (cancelled && cancelled())
        fail(ErrorCode::interrupted);
      const std::lock_guard spawn_lock{process_spawn_mutex};
      std::vector<char *> argv;
      for (auto &arg : args)
        argv.push_back(arg.data());
      argv.push_back(nullptr);
      std::vector<std::string> environment;
      std::vector<char *> envp;
      if (!child_environment.empty()) {
        environment = child_environment;
        for (auto &entry : environment)
          envp.push_back(entry.data());
        envp.push_back(nullptr);
      } else if (!codex_home.empty()) {
        for (char **entry = environ; *entry != nullptr; ++entry)
          if (!std::string_view{*entry}.starts_with("CODEX_HOME="))
            environment.emplace_back(*entry);
        environment.push_back("CODEX_HOME=" + codex_home.string());
        for (auto &entry : environment)
          envp.push_back(entry.data());
        envp.push_back(nullptr);
      }
      int in[2]{-1, -1}, out[2]{-1, -1}, err[2]{-1, -1};
      // Normalize descriptors above stdio even if the parent started with closed stdio.
      auto pipe = [this](int (&fds)[2]) {
        native_stage = 1;
        if (::pipe(fds) != 0)
          fail(ErrorCode::io, errno);
        for (auto &fd : fds) {
          native_stage = 2;
          const int owned = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
          if (owned < 0) {
            const int e = errno;
            for (int old : fds)
              if (old >= 0)
                close_owned(old, 3);
            fds[0] = fds[1] = -1;
            if (trace)
              trace->spawn(2, -1, e, -1, -1);
            fail(ErrorCode::io, e);
          }
          close_owned(fd, 3);
          fd = owned;
        }
      };
      try {
        pipe(in);
        pipe(out);
        if (separate_error)
          pipe(err);
      } catch (const Error &error) {
        for (int &fd : in)
          if (fd >= 0)
            close_owned(fd, 3);
        for (int &fd : out)
          if (fd >= 0)
            close_owned(fd, 3);
        for (int &fd : err)
          if (fd >= 0)
            close_owned(fd, 3);
        if (trace)
          trace->spawn(1, -1, static_cast<int>(error.detail), -1, -1);
        throw;
      }
      posix_spawn_file_actions_t actions;
      posix_spawnattr_t attributes;
      native_stage = 4;
      int rc = ::posix_spawn_file_actions_init(&actions);
      int spawn_errno = errno;
      if (rc == 0) {
        native_stage = 5;
        rc = ::posix_spawnattr_init(&attributes);
        spawn_errno = errno;
        if (rc == 0) {
          native_stage = 6;
          rc = ::posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
          spawn_errno = errno;
          if (rc == 0) {
            native_stage = 7;
            rc = ::posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
            spawn_errno = errno;
          }
          if (rc == 0) {
            native_stage = 8;
            rc = separate_error ? ::posix_spawn_file_actions_adddup2(&actions, err[1],
                                                                     STDERR_FILENO)
                 : merge_error ? ::posix_spawn_file_actions_adddup2(&actions, out[1],
                                                                    STDERR_FILENO)
                               : ::posix_spawn_file_actions_addopen(
                                     &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
            spawn_errno = errno;
          }
          if (rc == 0 && !cwd.empty()) {
            native_stage = 9;
#ifdef __APPLE__
            rc = ::posix_spawn_file_actions_addchdir(&actions, cwd.c_str());
#else
            rc = ::posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
#endif
            spawn_errno = errno;
          }
          if (rc == 0) {
            native_stage = 10;
            rc = ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
            spawn_errno = errno;
          }
          if (rc == 0) {
            native_stage = 11;
            rc = ::posix_spawnattr_setpgroup(&attributes, 0);
            spawn_errno = errno;
          }
          if (rc == 0) {
            native_stage = 12;
            rc = ::posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(),
                                envp.empty() ? environ : envp.data());
            spawn_errno = errno;
          }
          (void)::posix_spawnattr_destroy(&attributes);
        }
        (void)::posix_spawn_file_actions_destroy(&actions);
      }
      if (trace)
        trace->spawn(native_stage, rc, spawn_errno, rc == 0 ? pid : -1,
                     rc == 0 ? pid : -1);
      close_owned(in[0], 3);
      close_owned(out[1], 3);
      if (err[1] >= 0)
        close_owned(err[1], 3);
      if (rc != 0) {
        close_owned(in[1], 3);
        close_owned(out[0], 3);
        if (err[0] >= 0)
          close_owned(err[0], 3);
        pid = -1;
        fail(ErrorCode::io, rc);
      }
      group = pid;
      ++owned_children;
      input = in[1];
      output = out[0];
      error_output = err[0];
      if (error_output >= 0 && ::fcntl(error_output, F_SETFL, O_NONBLOCK) < 0)
        fail(ErrorCode::io, errno);
      // Socket-style SIGPIPE suppression exists for pipes on the qualified Mac profile.
#ifdef F_SETNOSIGPIPE
      if (::fcntl(input, F_SETNOSIGPIPE, 1) < 0)
        fail(ErrorCode::io, errno);
#endif
      if (::fcntl(input, F_SETFL, O_NONBLOCK) < 0 ||
          ::fcntl(output, F_SETFL, O_NONBLOCK) < 0)
        fail(ErrorCode::io, errno);
    } catch (const Error &error) {
      if (trace)
        trace->refuse(1, native_stage, error);
      throw;
    }
  }
  void close_input() {
    if (input >= 0) {
      close_owned(input, 0);
    }
  }
  void send(std::string_view bytes, Clock::time_point deadline) {
    try {
#ifndef F_SETNOSIGPIPE
      PipeSignalGuard signals;
#endif
      while (!bytes.empty()) {
        wait(input, POLLOUT, deadline);
        RawWaitControl control = raw_wait_control();
        control.deadline = deadline;
        RawWaitScope scope{control};
        auto permit = trace
                          ? trace->permit(raw_envelope_size + 24 + sizeof(std::size_t) +
                                          sizeof(ssize_t) + sizeof(int))
                          : RawNativePermit{};
        const auto requested = bytes.size();
        const auto n = ::write(input, bytes.data(), requested);
        const int error = errno;
        if (trace)
          trace->write(permit, requested, n, error);
        if (n > 0)
          bytes.remove_prefix(static_cast<std::size_t>(n));
        else if (n < 0 && errno != EINTR && errno != EAGAIN)
          fail(ErrorCode::io, errno);
      }
    } catch (const Error &error) {
      if (trace)
        trace->refuse(2, 0, error);
      throw;
    }
  }
  std::string line(Clock::time_point deadline, std::size_t limit) {
    try {
      while (true) {
        const auto end = pending.find('\n');
        if (end != std::string::npos) {
          auto s = pending.substr(0, end);
          pending.erase(0, end + 1);
          return s;
        }
        if (!read(deadline, limit))
          fail(ErrorCode::incomplete);
      }
    } catch (const Error &error) {
      if (trace)
        trace->refuse(4, 0, error);
      throw;
    }
  }
  std::string collect(Clock::time_point deadline, std::size_t limit,
                      int *exit_code = nullptr) {
    try {
      while (read(deadline, limit)) {
      }
      siginfo_t observed{};
      while (true) {
        if (cancelled && cancelled())
          fail(ErrorCode::interrupted);
        // Observe exit without reaping: the zombie leader pins its PID/group name
        // until owned group cleanup. Never kill a possibly recycled PGID.
        RawWaitControl control = raw_wait_control();
        control.deadline = deadline;
        RawWaitScope scope{control};
        auto permit =
            trace ? trace->permit(raw_envelope_size + 24 + 1 + 2 * sizeof(int) +
                                  sizeof(pid_t) + sizeof(uid_t) + 2 * sizeof(int))
                  : RawNativePermit{};
        const int rc = ::waitid(P_PID, static_cast<id_t>(pid), &observed,
                                WEXITED | WNOHANG | WNOWAIT);
        const int error = errno;
        if (trace)
          trace->wait(permit, observed, rc, error);
        if (rc == 0 && observed.si_pid == pid &&
            (observed.si_code == CLD_EXITED || observed.si_code == CLD_KILLED ||
             observed.si_code == CLD_DUMPED))
          break;
        if (rc == 0 && observed.si_pid != 0 &&
            (observed.si_code == CLD_STOPPED || observed.si_code == CLD_CONTINUED)) {
          siginfo_t state{};
          (void)::waitid(P_PID, static_cast<id_t>(pid), &state,
                         WSTOPPED | WCONTINUED | WNOHANG);
        }
        if (rc < 0 && errno != EINTR)
          fail(ErrorCode::io, errno);
        if (Clock::now() >= deadline)
          throw deadline_error;
        (void)::poll(nullptr, 0, 10);
      }
      const int code = observed.si_code == CLD_EXITED ? observed.si_status
                                                      : 128 + observed.si_status;
      if (exit_code != nullptr) {
        *exit_code = code;
        return std::move(pending);
      }
      if (observed.si_code != CLD_EXITED || code != 0)
        fail(ErrorCode::io, observed.si_code == CLD_EXITED ? code : -1);
      return std::move(pending);
    } catch (const Error &error) {
      if (trace)
        trace->refuse(3, 0, error);
      throw;
    }
  }

  std::string take_partial() noexcept { return std::move(pending); }

private:
  struct RawCaptureOptions {
    RawEventRef request;
    bool stdout_bytes, stderr_bytes;
  };
  std::optional<RawCaptureOptions> raw_options;
  std::optional<RawChildTrace> trace;
  pid_t pid = -1, group = -1;
  std::uint8_t native_stage = 0;
  void close_owned(int &fd, std::uint8_t channel) noexcept {
    const int result = ::close(fd);
    const int error = errno;
    if (trace)
      trace->close(channel, fd, result, error);
    fd = -1;
  }
  int input = -1, output = -1, error_output = -1;
  bool stdout_done = false;
  std::string pending;
  void wait(int fd, short events, Clock::time_point deadline) {
    if (cancelled && cancelled())
      fail(ErrorCode::interrupted);
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now())
            .count();
    if (remaining <= 0)
      throw deadline_error;
    pollfd descriptor{fd, events, 0};
    RawWaitControl control = raw_wait_control();
    control.deadline = deadline;
    RawWaitScope scope{control};
    const auto timeout = static_cast<int>(std::min<std::int64_t>(remaining, 100));
    auto permit = trace ? trace->permit(raw_envelope_size + 24 + 1 + 4 * sizeof(int) +
                                        2 * sizeof(short))
                        : RawNativePermit{};
    const int rc = ::poll(&descriptor, 1, timeout);
    const int error = errno;
    if (trace)
      trace->poll(permit,
                  fd == input    ? 0
                  : fd == output ? 1
                                 : 2,
                  fd, events, timeout, rc, descriptor.revents, error);
    if (rc < 0 && errno != EINTR)
      fail(ErrorCode::io, errno);
    if (rc > 0 && (descriptor.revents & POLLNVAL) != 0)
      fail(ErrorCode::io);
  }
  bool read(Clock::time_point deadline, std::size_t limit) {
    if (error_output >= 0) {
      wait(error_output, POLLIN, deadline);
      char b[8192];
      RawWaitControl control = raw_wait_control();
      control.deadline = deadline;
      RawWaitScope scope{control};
      auto permit = trace ? trace->read_permit(RawReadChannel::stderr_pipe, sizeof(b))
                          : RawNativePermit{};
      const auto n = ::read(error_output, b, sizeof(b));
      const int error = errno;
      if (trace)
        trace->read(permit, 2, sizeof(b), n, error, b);
      if (n == 0) {
        close_owned(error_output, 2);
      } else if (n > 0) {
        if (error_observer)
          error_observer(std::string_view{b, static_cast<std::size_t>(n)});
        if (error_bytes.size() + static_cast<std::size_t>(n) > limit)
          fail(ErrorCode::capacity);
        error_bytes.append(b, static_cast<std::size_t>(n));
      } else if (errno != EAGAIN && errno != EINTR)
        fail(ErrorCode::io, errno);
    }
    if (stdout_done)
      return error_output >= 0;
    wait(output, POLLIN, deadline);
    char bytes[8192];
    RawWaitControl control = raw_wait_control();
    control.deadline = deadline;
    RawWaitScope scope{control};
    auto permit = trace ? trace->read_permit(RawReadChannel::stdout_pipe, sizeof(bytes))
                        : RawNativePermit{};
    const auto n = ::read(output, bytes, sizeof(bytes));
    const int error = errno;
    if (trace)
      trace->read(permit, 1, sizeof(bytes), n, error, bytes);
    if (n == 0) {
      stdout_done = true;
      return error_output >= 0;
    }
    if (n < 0) {
      if (errno != EAGAIN && errno != EINTR)
        fail(ErrorCode::io, errno);
      return true;
    }
    const auto count = static_cast<std::size_t>(n);
    if (output_observer)
      output_observer(std::string_view{bytes, count});
    if (pending.size() > limit || count > limit - pending.size())
      fail(ErrorCode::capacity);
    pending.append(bytes, count);
    return true;
  }
};
} // namespace blackbird::detail
