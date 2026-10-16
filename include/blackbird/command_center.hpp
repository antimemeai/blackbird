#pragma once
#include "blackbird/command_jobs.hpp"
#include "blackbird/participant_observation.hpp"
#include "blackbird/raw_ui.hpp"
#include "blackbird/task_observation.hpp"
#include "blackbird/tui_inspector.hpp"
#include <functional>

namespace blackbird {
struct CommandCenterScope {
  EnvironmentId environment;
  ConversationId conversation;
};
enum class TaskPublicationStatus : std::uint8_t { ready, busy, uninitialized, stale };
struct TaskPublishedObservation {
  TaskPublicationStatus publication = TaskPublicationStatus::uninitialized;
  TaskObservation value;
  RawUiValueOrigin origin{};
  RawEventRef ingress{};
};
struct CommandCenterSources {
  std::function<TaskPublishedObservation(const TaskObservationQuery &)> tasks;
  ParticipantObservationReader participants;
  CommandJobs *commands = nullptr;
};
struct CommandCenterContext {
  CommandCenterScope scope;
  ParticipantObservationReader participants;
};
// Read-only current-session adapter. Sources are queried only by refresh,
// outside input and painting; controls own no execution authority.
class CommandCenter {
public:
  explicit CommandCenter(CommandCenterScope scope);
  ~CommandCenter();
  CommandCenter(const CommandCenter &) = delete;
  CommandCenter &operator=(const CommandCenter &) = delete;
  void toggle(RawEventRef origin = {});
  void first_page(RawEventRef origin = {});
  bool open() const noexcept { return open_; }
  bool focused() const noexcept { return focused_; }
  void focus_editor() noexcept { focused_ = false; }
  bool needs_refresh() const noexcept { return refresh_; }
  bool refresh(const CommandCenterSources &sources, bool capture);
  Result<void> act(tui::InspectorAction action, RawEventRef origin = {});
  void page(bool forward, RawEventRef origin = {});
  Result<tui::InspectorLayout> render(ChatGrid &grid, tui::Rect bounds);
  std::string_view notice() const noexcept;
  const tui::SceneHost &host() const noexcept { return host_; }
  RawEventRef applied() const noexcept { return applied_; }

private:
  struct State;
  CommandCenterScope scope_;
  tui::SceneHost host_;
  tui::CollectionInspector controls_;
  std::unique_ptr<State> state_;
  bool open_ = false, focused_ = false, refresh_ = true;
  RawEventRef origin_{}, applied_{};
};
} // namespace blackbird
