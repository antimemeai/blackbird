#pragma once
#include "blackbird/value.hpp"
#include <chrono>
#include <filesystem>
#include <functional>

namespace blackbird {
struct WorkflowHostLimits {
  std::size_t frame_bytes = 16 * 1024 * 1024;
  std::size_t clients = 32;
  std::size_t pending_bytes = 64 * 1024 * 1024;
  std::chrono::milliseconds client_timeout{30000};
};
// Caller acquires and retains the actual session/journal owner before construction.
// That owner exclusively controls this private session endpoint namespace through
// destruction; no concurrent same-user rename/unlink/bind is permitted.
// This host owns transport only; clients never acquire that execution authority.
// Existing endpoints refuse, including stale sockets; startup never unlinks them.
class WorkflowHost {
public:
  using Dispatch = std::function<Value(const Value &)>;
  using Tick = std::function<void()>;
  using Stop = std::function<bool()>;
  WorkflowHost(const std::filesystem::path &session, Dispatch dispatch, Tick tick = {},
               WorkflowHostLimits limits = {});
  ~WorkflowHost();
  WorkflowHost(const WorkflowHost &) = delete;
  WorkflowHost &operator=(const WorkflowHost &) = delete;
  // External stop changes must wake the host. Idle serve does not poll a timer.
  void serve(const Stop &stop);
  // Thread-safe notification only. Caller must hold the host lifetime through wake.
  void wake() noexcept;
  static Value request(const std::filesystem::path &session, const Value &request,
                       std::chrono::milliseconds timeout = std::chrono::seconds{30},
                       std::size_t frame_bytes = 16 * 1024 * 1024);
  static std::filesystem::path endpoint(const std::filesystem::path &session);
  // Caller holds the exclusive journal owner AND exclusive endpoint namespace
  // authority in the private directory; concurrent same-user mutation is outside
  // this contract. Only a refused connection to the same owned socket inode
  // permits stale cleanup; active peers refuse. Socket mode need not be 0600:
  // a crash between bind/chmod can leave a wider mode inside the private directory.
  // The inode recheck detects changes but is not an atomic unlink-if-inode API.
  static bool remove_stale_endpoint(const std::filesystem::path &session);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace blackbird
