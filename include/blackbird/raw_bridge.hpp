#pragma once
#include "blackbird/raw_caller.hpp"
#include <atomic>
#include <mutex>
namespace blackbird {
// A synchronous native frame pays all reached fixed events and its terminal
// before dispatch. It holds its creating issuer through protected Lua calls.
class RawBridgeTrace {
public:
  RawBridgeTrace() = default;
  ~RawBridgeTrace();
  RawBridgeTrace(RawBridgeTrace &&other) noexcept;
  RawBridgeTrace(const RawBridgeTrace &) = delete;
  RawBridgeTrace &operator=(const RawBridgeTrace &) = delete;
  void start(std::uint32_t request, std::uint32_t terminal, std::uint64_t amount,
             bool selected, std::span<const ByteView> fields, bool finite = false);
  bool enabled() const noexcept { return emission_.enabled(); }
  RawEventRef reference() const noexcept { return emission_.reference(); }
  void stage(std::uint8_t value) noexcept { stage_ = value; }
  RawEventRef event(std::uint32_t site, std::span<const ByteView> fields);
  RawEventRef linked(std::uint32_t site, RawEventRef parent,
                     std::span<const ByteView> fields);
  std::optional<RawCapacityClaim> data(std::uint64_t amount) const;
  void original(std::uint32_t site, std::span<const ByteView> fields,
                RawCapacityClaim *bank);
  void finish(RawOperationBranch branch, const Error *error,
              std::span<const ByteView> fields) noexcept;

private:
  RawThreadHandle issuer_;
  RawEmission emission_;
  std::uint32_t terminal_ = 0;
  std::uint8_t stage_ = 0;
};
class RawModuleIssuer {
public:
  static std::shared_ptr<RawModuleIssuer> prepare();
  RawThreadHandle handle;
  std::mutex mutex;
  std::atomic<bool> closing{false};
};
struct RawBridgeObservation {
  std::uint32_t site;
  std::uint8_t stage;
};
struct RawModuleReturn {
  const int *success = nullptr;
  const int *type = nullptr;
  const void *const *value = nullptr;
};
class RawModuleToken {
public:
  RawModuleToken(std::shared_ptr<RawModuleIssuer> issuer,
                 std::span<const ByteView> fields,
                 std::optional<std::string_view> name);
  ~RawModuleToken();
  RawModuleToken(RawModuleToken &&other) noexcept;
  RawModuleToken(const RawModuleToken &) = delete;
  RawModuleToken &operator=(const RawModuleToken &) = delete;
  bool active() const noexcept { return active_; }
  RawEventRef reference() const noexcept { return trace_.reference(); }
  bool closing() const noexcept { return issuer_ && issuer_->closing.load(); }
  RawEventRef event(RawBridgeObservation observation, std::span<const ByteView> fields);
  RawEventRef linked(std::uint32_t site, RawEventRef parent,
                     std::span<const ByteView> fields);
  void chunk(std::string_view original, std::string_view native);
  void inner_status(int status) noexcept;
  void finish(std::uint8_t reason, RawOperationBranch branch,
              const Error *error = nullptr, RawModuleReturn outcome = {}) noexcept;
  void failed(Error error) noexcept { failure_ = error; }

private:
  std::shared_ptr<RawModuleIssuer> issuer_;
  RawBridgeTrace trace_;
  std::optional<Error> failure_;
  std::optional<int> inner_status_;
  bool active_ = true;
  void finish_bound(std::uint8_t reason, RawOperationBranch branch, const Error *,
                    RawModuleReturn) noexcept;
};
std::string_view raw_bridge_description() noexcept;
} // namespace blackbird
