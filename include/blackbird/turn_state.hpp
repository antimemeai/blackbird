#pragma once
#include "blackbird/value.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Provider turn state read from the wire (CONTEXT_LIFECYCLE.md §7.1). It is evaluated
// on the message sequence a codec renders from the input items, after merging, never
// on stored entries: each codec exposes the pure render its export uses
// (anthropic_render, responses_render), and turn_state applies the profile's
// turn.start rule to it.
namespace blackbird {
// One provider-visible message and the input positions it was rendered from.
struct RenderedMessage {
  enum class Role : std::uint8_t {
    user,
    assistant,
    system,
    developer,
    tool,
    reasoning
  };
  Role role = Role::user;
  bool calls = false;   // a tool_use block or a function_call item
  bool results = false; // a tool_result block or a function_call_output item
  bool text = false;    // a text block or text content
  // The input position of each block, in order; a Responses item is one block.
  std::vector<std::size_t> positions;
  bool operator==(const RenderedMessage &) const = default;
};
struct TurnState {
  // The last message is an assistant message without calls, or a user message that
  // starts a turn under the profile's rule. An empty input is closed.
  bool closed = true;
  // The input position where the current turn starts: the first position of the
  // latest turn-start message (0 when no message starts a turn).
  std::size_t current = 0;
  // The index of that message, or messages.size() when none starts a turn.
  std::size_t start_message = 0;
};
// Whether a rendered message starts a turn under turn.start:
// user_message_without_results: a user message that holds no tool result;
// user_text: a user message that holds text.
bool turn_start(const RenderedMessage &message, std::string_view rule) noexcept;
// Reads turn.start from the effective profile; throws Error{invalid_range} for a
// profile without one.
TurnState turn_state(std::span<const RenderedMessage> messages, const Value &profile);
} // namespace blackbird
