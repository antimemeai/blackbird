#pragma once
#include "blackbird/command_center.hpp"
#include "blackbird/tui_lua.hpp"

namespace blackbird {
struct InstrumentSource {
  std::shared_ptr<const tui::lua::Script> script;
  std::string path;
  RawEventRef original{}, origin{};
  bool preview = false;
};
class TuiInstrument {
public:
  static Result<std::unique_ptr<TuiInstrument>>
  create(CommandCenterScope scope, std::shared_ptr<CommandJobs> commands,
         InstrumentSource initial);
  ~TuiInstrument();
  TuiInstrument(const TuiInstrument &) = delete;
  TuiInstrument &operator=(const TuiInstrument &) = delete;
  void toggle(RawEventRef origin = {});
  void first_page(RawEventRef origin = {});
  bool open() const noexcept;
  bool focused() const noexcept;
  void focus_editor() noexcept;
  void focus_view() noexcept;
  Result<void> source(InstrumentSource proposal, bool capture);
  bool refresh(bool capture);
  Result<void> act(tui::InspectorAction action, RawEventRef origin = {});
  void page(bool forward, RawEventRef origin = {});
  Result<void> pin(std::size_t slot, RawEventRef origin, bool capture);
  Result<void> request_interrupt(RawEventRef origin, bool capture);
  Result<tui::InspectorLayout> render(ChatGrid &grid, tui::Rect bounds);
  std::string_view notice() const noexcept;
  const tui::SceneHost &host() const noexcept;
  std::uint64_t effective_generation() const noexcept;

private:
  struct State;
  explicit TuiInstrument(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};
} // namespace blackbird
