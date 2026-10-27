#include "blackbird/turn_state.hpp"
#include "blackbird/foundation.hpp"
#include <string>

namespace blackbird {
bool turn_start(const RenderedMessage &message, std::string_view rule) noexcept {
  if (message.role != RenderedMessage::Role::user)
    return false;
  if (rule == "user_message_without_results")
    return !message.results;
  if (rule == "user_text")
    return message.text;
  return false;
}
TurnState turn_state(std::span<const RenderedMessage> messages, const Value &profile) {
  const auto *turn = profile.find("turn");
  const auto *start = turn ? turn->find("start") : nullptr;
  const auto *rule = start ? std::get_if<std::string>(&start->value()) : nullptr;
  if (!rule || (*rule != "user_message_without_results" && *rule != "user_text"))
    throw Error{ErrorCode::invalid_range};
  TurnState state;
  state.start_message = messages.size();
  for (std::size_t i = messages.size(); i > 0; --i)
    if (turn_start(messages[i - 1], *rule)) {
      state.start_message = i - 1;
      const auto &positions = messages[i - 1].positions;
      state.current = positions.empty() ? 0 : positions.front();
      break;
    }
  if (messages.empty())
    return state;
  const auto &last = messages.back();
  state.closed = (last.role == RenderedMessage::Role::assistant && !last.calls) ||
                 turn_start(last, *rule);
  return state;
}
} // namespace blackbird
