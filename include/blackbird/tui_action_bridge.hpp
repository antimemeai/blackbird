#pragma once
#include "blackbird/command_jobs.hpp"
#include "blackbird/tui_lua.hpp"
namespace blackbird::tui::lua {
struct BridgeScope {
  EnvironmentId environment;
  ConversationId conversation;
};
enum class BridgeDisposition : std::uint8_t { admitted = 1, refused = 2, unknown = 3 };
struct BridgeReceipt {
  explicit BridgeReceipt(ActionEnvelope accepted) : envelope(std::move(accepted)) {}
  ActionEnvelope envelope;
  Text<128> request_id;
  CommandObservationId job;
  BridgeDisposition disposition = BridgeDisposition::unknown;
  bool forward_called = false, has_error = false;
  Error error{ErrorCode::external_unknown};
  RawEventRef forward{}, returned{};
  CommandDeliveryStatus delivery_status = CommandDeliveryStatus::unavailable;
  bool delivery_known = false;
  CommandDeliveryObservation delivery;
};
enum class BridgeSnapshotStatus : std::uint8_t { ready, busy, unavailable };
struct BridgeSnapshot {
  BridgeSnapshotStatus status = BridgeSnapshotStatus::unavailable;
  std::array<std::optional<BridgeReceipt>, 16> entries{};
  std::size_t size = 0;
  bool more_before = false, stopped = false;
};
// Sole native consumer of this inbox. Own one bridge for the runtime scope,
// independently of any view/VM reload. A cache row is presentation, never replay
// authority or durable recovery history. The shared inbox retains unattempted work.
class NativeActionBridge {
public:
  static Result<std::unique_ptr<NativeActionBridge>>
  create(BridgeScope scope, std::shared_ptr<CommandJobs> commands,
         std::shared_ptr<ActionInbox> inbox);
  ~NativeActionBridge();
  NativeActionBridge(const NativeActionBridge &) = delete;
  NativeActionBridge &operator=(const NativeActionBridge &) = delete;
  const Text<256> &owner() const noexcept;
  std::uint64_t incarnation() const noexcept;
  void notify() noexcept;
  BridgeSnapshot try_snapshot() const noexcept;
  void request_stop() noexcept;
  bool quiescent() const noexcept;

private:
  struct State;
  explicit NativeActionBridge(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};
} // namespace blackbird::tui::lua
