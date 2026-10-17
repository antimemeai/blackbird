#pragma once
#include "blackbird/command_jobs.hpp"
#include "blackbird/tui_inspector.hpp"

namespace blackbird {
// Conversation adapter; only local presentation state. No domain actions.
// Immovable because the reusable control borrows its SceneHost.
class CommandInspector {
public:
  CommandInspector();
  CommandInspector(const CommandInspector &) = delete;
  CommandInspector &operator=(const CommandInspector &) = delete;
  void toggle(RawEventRef origin = {});
  bool open() const noexcept { return open_; }
  bool focused() const noexcept { return focused_; }
  void focus_editor() noexcept { focused_ = false; }
  void focus_view() noexcept { focused_ = open_; }
  void first_page(RawEventRef origin = {});
  bool needs_refresh() const noexcept { return refresh_; }
  bool refresh(CommandJobs *commands, bool capture);
  Result<void> act(tui::InspectorAction action);
  void page(bool forward);
  Result<tui::InspectorLayout> render(ChatGrid &grid, tui::Rect bounds);
  std::string_view notice() const noexcept;
  const tui::SceneHost &host() const noexcept { return host_; }
  RawEventRef applied() const noexcept { return applied_; }

private:
  enum class Deferred { none, owner_busy, recording, unavailable, projection };
  tui::SceneHost host_;
  tui::CollectionInspector controls_;
  std::optional<CommandObservation> observation_;
  std::array<std::optional<CommandObservationId>, 33> pages_{};
  std::size_t page_ = 0, offset_ = 0;
  std::optional<CommandObservationId> selected_;
  std::array<std::byte, 8192> last_wire_{};
  std::size_t last_wire_size_ = 0;
  std::uint64_t generation_ = 0;
  bool open_ = false, focused_ = false, refresh_ = true, last_capture_ = false;
  Deferred deferred_ = Deferred::none;
  RawEventRef origin_{}, applied_{};
};
} // namespace blackbird
