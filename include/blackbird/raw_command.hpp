#pragma once
#include "blackbird/raw_provider.hpp"
#include <optional>
namespace blackbird {
inline constexpr std::uint64_t raw_command_worker_credit = 2162;
inline constexpr std::uint64_t raw_command_retry_credit = 638;
inline constexpr std::uint64_t raw_command_region_credit = 1728;
inline constexpr std::uint64_t raw_command_semantic_credit =
    130 + raw_command_region_credit;
inline constexpr std::uint64_t raw_command_initial_credit =
    raw_command_worker_credit + raw_command_retry_credit + raw_command_semantic_credit;
// Only capture/transport helpers. Bodies are native fields supplied by the owner.
std::optional<RawControlToken> raw_command_claim(std::uint64_t bytes);
void raw_command_data_credit(std::uint64_t bytes);
RawEventRef raw_command_record(std::uint32_t site, std::span<const ByteView> fields,
                               RawControlToken *credit = nullptr) noexcept;
RawEventRef raw_command_data(std::uint32_t site, std::span<const ByteView> fields);
RawEventRef raw_command_linked(std::uint32_t site, RawEventRef request,
                               std::span<const ByteView> fields,
                               RawControlToken *credit = nullptr) noexcept;
RawEventRef raw_command_linked_data(std::uint32_t site, RawEventRef request,
                                    std::span<const ByteView> fields);
// A bounded keyboard acquisition prepays its exact command routing burst.
struct RawCommandBurstCredits {
  RawCapacityClaim *data;
  RawControlToken *control;
};
class RawCommandBurstScope {
public:
  explicit RawCommandBurstScope(RawCommandBurstCredits credits) noexcept;
  ~RawCommandBurstScope();
  RawCommandBurstScope(const RawCommandBurstScope &) = delete;
  RawCommandBurstScope &operator=(const RawCommandBurstScope &) = delete;

private:
  RawCapacityClaim *previous_data_;
  RawControlToken *previous_control_;
};
// Sticky, nonblocking cancellation pipe. Only its owner clears between joined work.
class RawCommandWake {
public:
  RawCommandWake();
  ~RawCommandWake();
  RawCommandWake(const RawCommandWake &) = delete;
  RawCommandWake &operator=(const RawCommandWake &) = delete;
  int read_fd() const noexcept { return endpoints_[0]; }
  int write_fd() const noexcept { return endpoints_[1]; }
  void notify() noexcept;
  void clear() noexcept;

private:
  std::array<int, 2> endpoints_{-1, -1};
};
std::string_view raw_command_description() noexcept;
} // namespace blackbird
