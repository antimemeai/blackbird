#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace blackbird::tui {
enum class ViewRoute : std::uint8_t {
  conversation,
  command_runs,
  command_center,
  result_comparison
};
struct ViewDescription {
  ViewRoute route;
  std::string_view action, label, description, command, binding;
};
inline constexpr std::array view_descriptions{
    ViewDescription{ViewRoute::conversation, "view.conversation", "Conversation",
                    "Return to the conversation and draft", "", ""},
    ViewDescription{ViewRoute::command_runs, "view.command-runs", "Command runs",
                    "Inspect command state and retained output", "/jobs", "Ctrl-O"},
    ViewDescription{ViewRoute::command_center, "view.command-center", "Command center",
                    "Inspect current-session tasks, participants and command runs",
                    "/center", "Ctrl-X"},
    ViewDescription{ViewRoute::result_comparison, "view.result-comparison",
                    "Result comparison", "Compare pinned output from command runs",
                    "/instrument", "Ctrl-]"}};
} // namespace blackbird::tui
