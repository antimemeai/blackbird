#pragma once

#include "blackbird/raw_runtime.hpp"

#include <memory>
#include <time.h>

namespace blackbird {
enum class RawSamplerMethod : std::uint32_t { mac_rusage = 1, linux_stat = 2 };
struct RawSamplerRequest {
  std::uint64_t controller = 0;
  pid_t pid = 0;
  std::uint32_t method = 0;
};
inline constexpr std::size_t raw_sampler_request_bytes = 32;
std::array<std::byte, raw_sampler_request_bytes>
encode_raw_sampler_request(const RawSamplerRequest &request) noexcept;
struct RawSamplerOptions {
  RawWaitControl control = raw_wait_control();
  clockid_t clock = CLOCK_MONOTONIC;
  // Setup only. Linux always verifies procfs on the opened descriptor.
  std::string_view proc_root = "/proc";
};

// A separate acquisition owner. Caller binds its lane before open and retains
// that binding through finish/destruction. Pipes/options.control are borrowed;
// the caller retains their lifetime. There is no cadence or target-process hook.
class RawResourceSampler {
public:
  static Result<std::unique_ptr<RawResourceSampler>>
  open(RawSamplerOptions options = {});
  ~RawResourceSampler();
  RawResourceSampler(const RawResourceSampler &) = delete;
  RawResourceSampler &operator=(const RawResourceSampler &) = delete;
  Result<void> acquire(const RawSamplerRequest &request);
  // Explicit pipe input; invalid/partial frame ends this session. Native target
  // failures have their own outcome and do not prevent later commands.
  Result<void> run(int control_fd);
#if defined(__linux__)
  // Descriptor-relative seam: caller holds an already-attached PID directory.
  // Production attaches below its verified procfs root; this bypass exists for
  // direct anchoring fixtures. This method never owns/closes the supplied fd.
  Result<void> acquire_directory(const RawSamplerRequest &request, int pid_directory);
#endif
  // Publishes closes/END. RawRuntime's later orderly destruction can still wait
  // for recorder drain; stopping acquisition does not bound that wait.
  Result<void> finish();

private:
  class State;
  explicit RawResourceSampler(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};
std::string_view raw_sampler_description() noexcept;
} // namespace blackbird
