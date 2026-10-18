#pragma once

#include "blackbird/foundation.hpp"
#include "blackbird/raw_runtime.hpp"

#include <cerrno>
#include <chrono>

namespace blackbird {
// Provider request deadlines, in seconds. Every provider transport has two:
// - stall: the response stream sent no bytes for stall_seconds while Blackbird was
//   waiting on it. Only time spent waiting on the child's output counts; Blackbird's
//   own processing and capture admission between reads do not. A stall is a
//   transient transport fault (provider_stall_error) and the retry loop retries it.
// - request: the total ceiling for the whole request, raw-capture waits included.
//   Reaching it is final (provider_ceiling_error): the identical request would reach
//   it again, so it is never retried.
// curl's own --max-time is request + provider_curl_backstop_seconds, so Blackbird's
// deadline always fires first and curl's is only a backstop.
// Defaults: over all 762 provider requests of the 2026-10-17 campaign
// (~/projects/blackbird_runs/2026-10-17-cpp26/STREAM_TIMING.md) the longest wait
// without a byte was 11.5 s (time to first byte) and the longest mid-stream gap
// 10.1 s, so a 120 s stall is about 10x the worst observed. All 31 cut requests
// were still streaming when the old fixed 180 s total cap killed them.
inline constexpr int provider_stall_timeout_default = 120;
inline constexpr int provider_request_timeout_default = 3600;
inline constexpr int provider_timeout_limit = 86400;
inline constexpr int provider_curl_backstop_seconds = 30;
struct ProviderTimeouts {
  int stall_seconds = provider_stall_timeout_default; // 0: no stall check
  int request_seconds = provider_request_timeout_default;
};
constexpr bool valid_provider_timeouts(ProviderTimeouts timeouts) noexcept {
  return timeouts.request_seconds >= 1 &&
         timeouts.request_seconds <= provider_timeout_limit &&
         timeouts.stall_seconds >= 0 &&
         timeouts.stall_seconds <= provider_timeout_limit;
}
// curl exit 28 (operation timed out) keeps its existing transient classification.
inline constexpr Error provider_stall_error{ErrorCode::provider_transport, 28};
inline constexpr Error provider_ceiling_error{ErrorCode::incomplete, ETIMEDOUT};
// One provider layer's ownership of the request ceiling: it bounds every raw wait
// in its scope (never later than an inherited deadline) and gives the layer its own
// capture-failure scope. business() turns this deadline's own expiry
// (interrupted/ETIMEDOUT at or after it) into provider_ceiling_error and consumes
// exactly that identical capture marker. Every other error, ECANCELED and any
// earlier marker still propagate when the scope ends.
class ProviderDeadline {
public:
  explicit ProviderDeadline(int request_seconds);
  ProviderDeadline(const ProviderDeadline &) = delete;
  ProviderDeadline &operator=(const ProviderDeadline &) = delete;
  std::chrono::steady_clock::time_point at() const noexcept { return at_; }
  Error business(Error error) noexcept;

private:
  RawCaptureFailureScope admission_;
  std::chrono::steady_clock::time_point at_;
  RawWaitScope wait_;
};
} // namespace blackbird
