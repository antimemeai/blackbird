#include "blackbird/context.hpp"
#include "blackbird/tools.hpp"
#include "blackbird/workflow_host.hpp"
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace blackbird;
namespace {
thread_local bool fail_wake_send = false;
}
extern "C" ssize_t send(int fd, const void *bytes, std::size_t size, int flags) {
  if (fail_wake_send) {
    fail_wake_send = false;
    errno = EIO;
    return -1;
  }
  using Send = ssize_t (*)(int, const void *, std::size_t, int);
  static auto actual = reinterpret_cast<Send>(::dlsym(RTLD_NEXT, "send"));
  if (!actual) {
    errno = ENOSYS;
    return -1;
  }
  return actual(fd, bytes, size, flags);
}
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class Action> bool refuses(Action &&action) {
  try {
    action();
  } catch (const Error &) {
    return true;
  }
  return false;
}
Value command(std::string value) {
  return Value::object({{"command", Value{std::move(value)}}});
}
int connect_raw(const std::filesystem::path &path) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  check(fd >= 0, "fixture socket failed");
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  const auto name = path.string();
  check(name.size() < sizeof(address.sun_path), "fixture path overflow");
  std::copy(name.begin(), name.end(), address.sun_path);
  if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) !=
      0) {
    ::close(fd);
    throw std::runtime_error("fixture connect failed");
  }
  return fd;
}
void exercise(const std::filesystem::path &directory) {
  int ready[2]{-1, -1};
  check(::pipe(ready) == 0, "ready pipe failed");
  const auto child = ::fork();
  check(child >= 0, "host fork failed");
  if (child == 0) {
    ::close(ready[0]);
    try {
      bool stop = false;
      std::atomic_bool worker_event{false};
      bool observed_wake = false;
      WorkflowHost *owner = nullptr;
      WorkflowHost host{directory,
                        [&](const Value &request) {
                          const auto *value = request.find("command");
                          if (value && *value == Value{"stop"})
                            stop = true;
                          if (value && *value == Value{"fail"})
                            throw Error{ErrorCode::conflict, 42};
                          if (value && *value == Value{"wake"}) {
                            std::thread worker{[&] {
                              worker_event.store(true);
                              owner->wake();
                            }};
                            worker.join();
                          }
                          return Value::object({{"pid", Value{Number{::getpid()}}},
                                                {"wake", Value{observed_wake}},
                                                {"echo", request}});
                        },
                        [&] {
                          if (worker_event.load())
                            observed_wake = true;
                        },
                        {.frame_bytes = 1024 * 1024,
                         .clients = 8,
                         .pending_bytes = 4 * 1024 * 1024,
                         .client_timeout = std::chrono::seconds{3}}};
      owner = &host;
      const char byte = 'R';
      check(::write(ready[1], &byte, 1) == 1, "ready write failed");
      ::close(ready[1]);
      host.serve([&] { return stop; });
    } catch (...) {
      ::_exit(5);
    }
    ::_exit(0);
  }
  ::close(ready[1]);
  char byte = 0;
  check(::read(ready[0], &byte, 1) == 1 && byte == 'R', "host failed to start");
  ::close(ready[0]);
  try {
    const std::string binary{"A\0\xff\x1bZ", 5};
    const auto input = Value::object({{"payload", Value{binary}}});
    const auto response = WorkflowHost::request(directory, input);
    check(field(response, "echo") == input &&
              field(response, "pid") == Value{Number{child}},
          "native binary payload or host identity changed");
    std::string large(300000, '\0');
    for (std::size_t index = 0; index < large.size(); ++index)
      large[index] = static_cast<char>(index % 256);
    const auto large_input = Value::object({{"payload", Value{std::move(large)}}});
    check(field(WorkflowHost::request(directory, large_input), "echo") == large_input,
          "multi-visit partial body/reply changed bytes");
    check(refuses([&] {
            WorkflowHost second{directory, [](const Value &v) { return v; }};
          }),
          "second owner replaced active endpoint");
    check(WorkflowHost::request(directory, input) == response,
          "failed second bind removed first endpoint");
    const int slow = connect_raw(WorkflowHost::endpoint(directory));
    const std::array<char, 2> prefix{16, 0};
    check(::write(slow, prefix.data(), prefix.size()) == 2,
          "partial header write failed");
    check(field(WorkflowHost::request(directory, input), "echo") == input,
          "partial client blocked independent request");
    ::close(slow);
    const int malformed = connect_raw(WorkflowHost::endpoint(directory));
    const std::array<unsigned char, 4> huge{255, 255, 255, 255};
    check(::write(malformed, huge.data(), huge.size()) == 4, "malformed write failed");
    ::close(malformed);
    check(field(WorkflowHost::request(directory, input), "echo") == input,
          "malformed frame killed host");
    bool typed_error = false;
    try {
      WorkflowHost::request(directory, command("fail"));
    } catch (const Error &error) {
      typed_error = error == Error{ErrorCode::conflict, 42};
    }
    check(typed_error, "dispatch error lost native code/detail");
    WorkflowHost::request(directory, command("wake"));
    check(field(WorkflowHost::request(directory, command("probe")), "wake") ==
              Value{true},
          "worker wake did not reach owner tick");
    for (int iteration = 0; iteration < 3; ++iteration) {
      const auto client = ::fork();
      check(client >= 0, "client fork failed");
      if (client == 0) {
        try {
          const auto reply = WorkflowHost::request(directory, input);
          ::_exit(field(reply, "pid") == Value{Number{child}} ? 0 : 6);
        } catch (...) {
          ::_exit(7);
        }
      }
      int status = 0;
      check(::waitpid(client, &status, 0) == client && WIFEXITED(status) &&
                WEXITSTATUS(status) == 0,
            "independent client could not detach/reconnect");
    }
    WorkflowHost::request(directory, command("stop"));
  } catch (...) {
    ::kill(child, SIGKILL);
    int status = 0;
    ::waitpid(child, &status, 0);
    throw;
  }
  int status = 0;
  check(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0,
        "host did not stop explicitly");
  check(!std::filesystem::exists(WorkflowHost::endpoint(directory)),
        "owned endpoint not unlinked");
}
void paths(const std::filesystem::path &directory) {
  const auto path = WorkflowHost::endpoint(directory);
  std::filesystem::create_symlink(directory / "missing", path);
  check(refuses(
            [&] { WorkflowHost host{directory, [](const Value &v) { return v; }}; }) &&
            std::filesystem::is_symlink(path) &&
            refuses([&] { (void)WorkflowHost::remove_stale_endpoint(directory); }),
        "symlink endpoint accepted or removed");
  std::filesystem::remove(path);
  write_file(path, "operator file");
  check(refuses(
            [&] { WorkflowHost host{directory, [](const Value &v) { return v; }}; }) &&
            read_file(path) == "operator file" &&
            refuses([&] { (void)WorkflowHost::remove_stale_endpoint(directory); }),
        "regular endpoint accepted or removed");
  std::filesystem::remove(path);
  {
    WorkflowHost host{directory, [](const Value &v) { return v; }};
    std::filesystem::remove(path);
    write_file(path, "replacement inode");
  }
  check(read_file(path) == "replacement inode",
        "destructor unlinked replacement inode");
  std::filesystem::remove(path);
  ::chmod(directory.c_str(), 0755);
  check(
      refuses([&] { WorkflowHost host{directory, [](const Value &v) { return v; }}; }),
      "public session directory accepted");
  ::chmod(directory.c_str(), 0700);
  check(refuses([&] {
          WorkflowHost host{directory / std::string(120, 'x'),
                            [](const Value &v) { return v; }};
        }),
        "oversized socket path accepted");
}
void stale_endpoints(const std::filesystem::path &directory) {
  const auto path = WorkflowHost::endpoint(directory);
  check(!WorkflowHost::remove_stale_endpoint(directory),
        "absent socket cleanup failed");
  for (const mode_t mode : {mode_t{0755}, mode_t{0600}}) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    check(fd >= 0, "stale fixture socket failed");
    sockaddr_un endpoint{};
    endpoint.sun_family = AF_UNIX;
    const auto name = path.string();
    check(name.size() < sizeof(endpoint.sun_path), "stale fixture path overflow");
    std::copy(name.begin(), name.end(), endpoint.sun_path);
    const int bound =
        ::bind(fd, reinterpret_cast<const sockaddr *>(&endpoint), sizeof(endpoint));
    ::close(fd);
    check(bound == 0 && ::chmod(path.c_str(), mode) == 0, "stale fixture bind failed");
    check(WorkflowHost::remove_stale_endpoint(directory) &&
              !std::filesystem::exists(path),
          "owned stale socket not recovered");
  }
  {
    WorkflowHost host{directory, [](const Value &value) { return value; }};
    struct stat before{};
    check(::lstat(path.c_str(), &before) == 0, "live socket stat failed");
    check(refuses([&] { (void)WorkflowHost::remove_stale_endpoint(directory); }),
          "active endpoint cleaned as stale");
    struct stat after{};
    check(::lstat(path.c_str(), &after) == 0 && before.st_dev == after.st_dev &&
              before.st_ino == after.st_ino,
          "active endpoint inode changed");
  }
}
void failed_wake(const std::filesystem::path &directory) {
  std::mutex mutex;
  std::condition_variable changed;
  bool ready = false;
  WorkflowHost host{directory, [](const Value &value) { return value; },
                    [&] {
                      const std::lock_guard lock{mutex};
                      ready = true;
                      changed.notify_one();
                    }};
  std::thread worker{[&] {
    {
      std::unique_lock lock{mutex};
      changed.wait(lock, [&] { return ready; });
    }
    fail_wake_send = true;
    host.wake();
  }};
  bool failed = false;
  try {
    host.serve([] { return false; });
  } catch (const Error &error) {
    failed = error == Error{ErrorCode::io, EIO};
  }
  worker.join();
  check(failed, "failed sole completion wake did not fault host");
}
} // namespace
int main() {
  char pattern[] = "/tmp/bb-workflow-host-XXXXXX";
  const auto *directory = ::mkdtemp(pattern);
  if (!directory)
    return 2;
  try {
    exercise(directory);
    paths(directory);
    stale_endpoints(directory);
    failed_wake(directory);
    std::filesystem::remove_all(directory);
    std::cout << "workflow host: native frames, clients, detach, owner bind, wake, "
                 "malformed requests and inode cleanup passed\n";
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
    std::filesystem::remove_all(directory);
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    std::filesystem::remove_all(directory);
    return 1;
  }
}
