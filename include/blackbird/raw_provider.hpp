#pragma once

#include "blackbird/provider_timeouts.hpp"
#include "blackbird/raw_runtime.hpp"

#include <optional>
#include <string_view>
#include <sys/wait.h>

namespace blackbird {

struct RawEventRef {
  IdentityBytes lane{};
  std::uint64_t ordinal = 0;
};
struct RawProviderOwner {
  IdentityBytes attempt{}, invocation{}, generation{};
  RawEventRef dispatch;
};

class RawProviderTrace {
public:
  RawProviderTrace(const RawProviderOwner *owner, ProviderTimeouts timeouts,
                   const RawEventRef *auth_resolution = nullptr);
  ~RawProviderTrace();
  RawProviderTrace(const RawProviderTrace &) = delete;
  RawProviderTrace &operator=(const RawProviderTrace &) = delete;
  RawEventRef reference() const noexcept { return request_; }
  void body(std::string_view method, std::string_view url, std::string_view bytes,
            RawCapacityClaim *credit = nullptr);
  void http(RawEventRef child, int status);
  void finish(std::uint8_t branch, std::uint8_t stage = 0, Error error = {}) noexcept;

private:
  RawEventRef request_;
  std::optional<RawControlToken> terminal_;
};

namespace detail {
template <class T> ByteView raw_native_field(const T &value) noexcept {
  return std::as_bytes(std::span{&value, std::size_t{1}});
}
inline ByteView raw_string_field(std::string_view value) noexcept {
  return std::as_bytes(std::span{value.data(), value.size()});
}
struct RawNativePermit {
  std::optional<RawControlToken> control;
  std::optional<RawCapacityClaim> data;
};
enum class RawReadChannel : std::uint8_t { stdout_pipe = 1, stderr_pipe = 2 };

// Explicit owner selection; private authorization children are not opted in.
class RawChildTrace {
public:
  RawChildTrace(RawEventRef request, bool capture_stdout, bool capture_stderr,
                bool merge_error, bool separate_error, Error deadline_error);
  ~RawChildTrace();
  RawChildTrace(const RawChildTrace &) = delete;
  RawChildTrace &operator=(const RawChildTrace &) = delete;
  RawEventRef reference() const noexcept { return child_; }
  bool captures(std::uint8_t channel) const noexcept;
  RawNativePermit permit(std::uint64_t bytes);
  RawNativePermit read_permit(RawReadChannel channel, std::size_t requested);
  void spawn(std::uint8_t stage, int result, int error, pid_t pid,
             pid_t group) noexcept;
  void poll(RawNativePermit &permit, std::uint8_t channel, int fd, short events,
            int timeout, int result, short revents, int error) noexcept;
  void write(RawNativePermit &permit, std::size_t requested, ssize_t result,
             int error) noexcept;
  std::optional<RawEventRef> read(RawNativePermit &permit, std::uint8_t channel,
                                  std::size_t requested, ssize_t result, int error,
                                  const char *bytes) noexcept;
  void wait(RawNativePermit &permit, const siginfo_t &observed, int result,
            int error) noexcept;
  // One final nonblocking custody check, paid before child acquisition.
  void cleanup_wait(const siginfo_t &observed, int result, int error) noexcept;
  void close(std::uint8_t channel, int fd, int result, int error) noexcept;
  void signal(pid_t group, int signal, int result, int error) noexcept;
  void reap(pid_t pid, pid_t result, int error, bool status_valid, int status) noexcept;
  void refuse(std::uint8_t operation, std::uint8_t stage, Error error) noexcept;

private:
  void terminal(std::uint32_t site, std::span<const ByteView> fields) noexcept;
  RawEventRef child_;
  std::optional<RawControlToken> cleanup_;
  bool stdout_ = false, stderr_ = false, spawn_written_ = false,
       refusal_written_ = false;
  Error deadline_error_;
};
} // namespace detail

std::string_view raw_provider_description() noexcept;

} // namespace blackbird
