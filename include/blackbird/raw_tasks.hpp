#pragma once
#include "blackbird/raw_provider.hpp"
#include <utility>
namespace blackbird {
enum class RawTaskAction : std::uint8_t {
  prepare = 1,
  read,
  snapshot,
  restore,
  replay,
  edit,
  commit,
  replay_prepare,
  replay_commit
};
struct RawTaskOrigin {
  std::optional<RawEventRef> parent;
  bool replay = false;
  RawControlToken *credit = nullptr;
  RawCapacityClaim *data = nullptr;
  RawControlToken *borrowed_control = nullptr;
};
// Stack-owned finite outcomes. Parent/credit are supplied by actual owner calls.
class RawTaskTrace {
public:
  RawTaskTrace(RawTaskAction action, std::uint64_t revision, RawTaskOrigin origin,
               std::uint64_t credit);
  explicit RawTaskTrace(std::string_view operator_command);
  ~RawTaskTrace();
  RawTaskTrace(const RawTaskTrace &) = delete;
  RawTaskTrace &operator=(const RawTaskTrace &) = delete;
  RawEventRef reference() const noexcept { return request_; }
  bool enabled() const noexcept { return credit_.has_value(); }
  RawTaskOrigin child(bool replay = false) const noexcept;
  void stage(std::uint8_t value) noexcept { stage_ = value; }
  void finish(std::uint8_t branch = 0, std::optional<Error> error = {}) noexcept;
  RawEventRef event(std::uint32_t site, RawEventRef parent,
                    std::span<const ByteView> fields = {}) noexcept;
  void bytes(std::string_view source, RawCapacityClaim &credit) noexcept;
  std::optional<RawControlToken> split(std::uint64_t amount);
  template <class F> decltype(auto) run(F &&body) {
    try {
      return std::forward<F>(body)();
    } catch (const Error &error) {
      finish(3, error);
      throw;
    } catch (const std::bad_alloc &) {
      finish(4);
      throw;
    } catch (...) {
      finish(5);
      throw;
    }
  }

private:
  RawEventRef publish(std::uint32_t site, std::span<const ByteView> fields,
                      RawPartition partition, RawControlToken *credit) noexcept;
  RawEventRef request_{};
  std::optional<RawControlToken> credit_;
  std::uint8_t stage_ = 0;
  bool operator_ = false;
  RawCapacityClaim *borrowed_data_ = nullptr;
  RawControlToken *borrowed_control_ = nullptr;
};
std::string_view raw_tasks_description() noexcept;
} // namespace blackbird
