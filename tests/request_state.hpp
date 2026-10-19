#pragma once
#include "blackbird/context.hpp"
#include "blackbird/foundation.hpp"
#include "blackbird/value.hpp"
#include <string>
#include <string_view>
#include <variant>

namespace blackbird::test {
// Every assembled request ends with the developer operational-state item that
// carries CONTEXT_SIZE (a1v C2). Mocks require it and examine the conversation
// before it, so layout checks keep their meaning.
inline Value::Array request_conversation(const Value &request) {
  auto input = field(request, "input").array();
  const auto *role = input.empty() ? nullptr : input.back().find("role");
  const auto *content = input.empty() ? nullptr : input.back().find("content");
  if (!role || *role != Value{"developer"} || !content ||
      !std::holds_alternative<std::string>(content->value()) ||
      std::string_view{content->string()}.find("CONTEXT_SIZE input_bytes=") ==
          std::string_view::npos)
    throw Error{ErrorCode::corrupt};
  input.pop_back();
  return input;
}
inline std::string request_state_text(const Value &request) {
  (void)request_conversation(request); // Requires the state item.
  return field(request, "input").array().back().find("content")->string();
}
} // namespace blackbird::test
