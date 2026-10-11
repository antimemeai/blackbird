#pragma once

#include "blackbird/raw_channel.hpp"

#include <memory>
#include <string_view>
#include <sys/types.h>

namespace blackbird {

class RawThreadContext;
class RawRuntime;
using RawThreadHandle = std::shared_ptr<RawThreadContext>;
using RawControlToken = RawControlClaim;
struct RawRuntimeStatus {
  pid_t supervisor;
  pid_t recorder;
  bool fenced;
};
struct RawRuntimeOptions {
  RawCapacities producer_capacities{64 * 1024 * 1024, 256 * 1024, 128 * 1024};
  std::uint64_t recorder_failure_bytes = 128 * 1024;
};

// Registration and allocation happen before a worker starts. The handle owns
// its mapping and endpoint; binding selects its sole execution owner.
Result<RawThreadHandle> raw_prepare_thread();
class RawThreadBinding {
public:
  explicit RawThreadBinding(RawThreadHandle context);
  // Native embedding seam: the caller has already registered this lane and
  // owns its producer for the binding's whole lifetime.
  RawThreadBinding(RawProducer &producer, IdentityBytes epoch);
  ~RawThreadBinding();
  RawThreadBinding(const RawThreadBinding &) = delete;
  RawThreadBinding &operator=(const RawThreadBinding &) = delete;

private:
  RawThreadHandle context_;
  RawThreadContext *previous_;
};

class RawRuntime {
public:
  static Result<std::unique_ptr<RawRuntime>> open(std::string_view directory,
                                                  EnvironmentId environment,
                                                  RawRuntimeOptions options = {});
  ~RawRuntime();
  RawRuntime(const RawRuntime &) = delete;
  RawRuntime &operator=(const RawRuntime &) = delete;
  Result<RawThreadHandle> prepare_thread();
  Result<void> recover();
  Result<RawRuntimeStatus> status();

private:
  class State;
  explicit RawRuntime(std::shared_ptr<State> state);
  std::shared_ptr<State> state_;
  friend class RawThreadContext;
  friend Result<RawThreadHandle> raw_prepare_thread();
};

bool raw_enabled() noexcept;
IdentityBytes raw_lane_epoch() noexcept;
Result<RawControlToken> raw_claim_control(std::uint64_t bytes);
// Safe boundary only, before acquiring a new measurement or entering an owned
// lock. The sole producer cannot consume the checked capacity concurrently.
Result<void> raw_wait_data(std::uint64_t bytes);
Result<std::uint64_t> raw_emit(std::uint32_t site, std::span<const ByteView> parts,
                               RawPartition partition = RawPartition::data,
                               RawControlToken *claim = nullptr) noexcept;
// A terminal failure closes admission and emits the original error fields into
// the independent failure partition. It never retries a published event.
void raw_fence(Error error) noexcept;

} // namespace blackbird
