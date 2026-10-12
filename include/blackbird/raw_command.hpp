#pragma once
#include "blackbird/raw_provider.hpp"
#include <csignal>
#include <optional>
namespace blackbird {
inline constexpr std::uint64_t raw_command_worker_credit = 2350;
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
inline constexpr std::uint64_t raw_wake_setup_credit = 1076;
inline constexpr std::uint64_t raw_wake_rearm_credit = 1468;
inline constexpr std::uint64_t raw_wake_lifecycle_credit = 99;
inline constexpr std::uint64_t raw_wake_notify_credit = 96;
inline constexpr std::uint64_t raw_wake_single_notify_credit = 188;
inline constexpr std::uint64_t raw_command_caller_stop_credit =
    57 + raw_wake_single_notify_credit;
// Sticky socket shutdown. Only a joined owner may replace its incarnation.
class RawCommandWake {
public:
  enum class Mode : std::uint8_t { reusable, one_shot, signal };
  explicit RawCommandWake(Mode mode = Mode::reusable);
  ~RawCommandWake();
  RawCommandWake(const RawCommandWake &) = delete;
  RawCommandWake &operator=(const RawCommandWake &) = delete;
  int read_fd() const noexcept { return endpoints_[0]; }
  int write_fd() const noexcept { return endpoints_[1]; }
  void notify(RawControlToken *credit = nullptr,
              std::atomic_bool *sticky = nullptr) noexcept;
  Result<void> clear() noexcept;
  // Lifecycle policy, set before dispatch/publication and retired after joins.
  void publish() noexcept { borrowed_.store(true, std::memory_order_release); }
  void retire() noexcept { borrowed_.store(false, std::memory_order_release); }

private:
  friend class RawCommandInterrupt;
  enum class State : std::uint8_t {
    armed,
    notifying,
    notified,
    rearming,
    failed,
    closed
  };
  RawThreadHandle owner_;
  std::optional<RawControlToken> notify_credit_, final_credit_;
  RawSignalPublisher publisher_;
  Mode mode_;
  std::atomic<State> state_{State::rearming};
  std::atomic_bool borrowed_{false};
  int endpoints_[2]{-1, -1};
  unsigned char incarnation_lane_[16]{}, publisher_lane_[16]{};
  std::uint64_t incarnation_ordinal_ = 0;
  Error failure_{};
  int (*shutdown_)(int, int) = nullptr;
  void (*exit_)(int) = nullptr;
};
// The containing session joins all potential handler threads before destruction.
// Native handler publication itself never binds or registers a producer.
class RawCommandInterrupt {
public:
  explicit RawCommandInterrupt(std::atomic_bool &interrupted);
  ~RawCommandInterrupt();
  RawCommandInterrupt(const RawCommandInterrupt &) = delete;
  RawCommandInterrupt &operator=(const RawCommandInterrupt &) = delete;
  RawCommandWake &wake() noexcept { return *wake_; }
  Result<void> reset() noexcept;
#if defined(__linux__)
  // The qualified Linux kernel ABI has 64 signal bits; libc's sigset_t is larger.
  static_assert(NSIG == 65);
  static constexpr std::size_t signal_mask_bytes = (NSIG - 1) / 8;
  static constexpr std::size_t restorer_bytes = sizeof(void (*)());
#else
  static constexpr std::size_t signal_mask_bytes = sizeof(sigset_t);
  static constexpr std::size_t restorer_bytes = 0;
#endif
  static constexpr std::uint64_t action_credit =
      93 +
      2 * (sizeof(void (*)(int)) + sizeof(int) + signal_mask_bytes + restorer_bytes);
  static constexpr std::uint64_t mask_credit = 93 + 2 * signal_mask_bytes;

private:
  static void handler(int) noexcept;
  RawThreadHandle owner_;
  std::optional<RawControlToken> credit_;
  std::unique_ptr<RawCommandWake> wake_;
  RawCommandWake *signal_wake_ = nullptr;
  std::atomic_bool &interrupted_;
  struct sigaction previous_{};
};
std::string_view raw_command_description() noexcept;
} // namespace blackbird
