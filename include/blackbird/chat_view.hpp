#pragma once
#include "blackbird/raw_ui.hpp"
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace blackbird {
enum class ChatKind { user, assistant, notice, tool, error, summary };
enum class Ink : std::uint8_t {
  normal,
  muted,
  user,
  assistant,
  heading,
  code,
  keyword,
  string,
  success,
  failure,
  selected,
  border
};
struct ChatSpan {
  std::string text;
  Ink ink = Ink::normal;
};
using ChatRow = std::vector<ChatSpan>;
std::string chat_plain(const ChatRow &row);

// Bounded derived display. No durable state or audit authority.
class ChatView {
public:
  explicit ChatView(bool capture = false) : raw_capture_(capture) {}
  void append(ChatKind kind, std::string_view text, bool merge = false,
              RawEventRef source = {});
  void begin(ChatKind kind);
  void stream(std::string_view text);
  ChatView &operator+=(std::string_view notice);
  void clear();
  std::size_t size() const noexcept { return bytes_; }
  const std::vector<ChatRow> &rows(std::size_t width);
  std::uint64_t generation() const noexcept { return generation_; }
  std::uint64_t reflows() const noexcept { return reflows_; }

private:
  struct Block {
    ChatKind kind;
    std::string text;
    bool dirty = true;
    std::size_t committed = 0, stable_rows = 0, line_bytes = 0;
    bool initialized = false, in_code = false;
    RawEventRef identity{};
  };
  bool raw_capture_ = false;
  std::deque<Block> blocks_;
  std::vector<ChatRow> rows_;
  std::size_t bytes_ = 0, width_ = 0;
  bool dirty_ = true, clipped_ = false;
  ChatKind stream_kind_ = ChatKind::assistant;
  std::uint64_t reflows_ = 0, generation_ = 0;
  std::size_t newlines_ = 0;
  void trim(RawControlToken *credit = nullptr);
};

// Compact cells: scalar inline, complex cluster tail in a per-buffer arena.
struct ChatCell {
  char32_t scalar = U' ';
  std::uint32_t tail = 0;
  std::uint16_t length = 0;
  Ink ink = Ink::normal;
  std::uint8_t width = 1;
};
static_assert(sizeof(ChatCell) <= 16);
struct ChatGrid {
  std::size_t width = 0, height = 0;
  std::vector<ChatCell> cells;
  std::string tails;
  void reset(std::size_t columns, std::size_t rows);
  void line(std::size_t row, const ChatRow &content, std::size_t column = 0);
};
// Bound only for a grid owned by reset/line: each live tail has a distinct arena
// extent.
std::uint64_t raw_ui_owned_grid_bound(const ChatGrid &grid);
// The returned packet is a complete frame delta. Commit only after successful I/O.
class ChatPainter {
public:
  const std::string &prepare(const ChatGrid &next, std::size_t cursor_row,
                             std::size_t cursor_column, bool full = false);
  void commit(const ChatGrid &next);
  void invalidate() noexcept { valid_ = false; }
  std::size_t touched() const noexcept { return touched_; }

private:
  ChatGrid previous_;
  std::string packet_;
  std::size_t cursor_row_ = 0, cursor_column_ = 0, touched_ = 0;
  bool valid_ = false;
};
// Single-writer nonblocking terminal packet. Caller retains packet storage until
// pending() is false. A partial escape sequence is never replaced mid-packet.
class ChatOutput {
public:
  ChatOutput() = default;
  ~ChatOutput();
  ChatOutput(const ChatOutput &) = delete;
  ChatOutput &operator=(const ChatOutput &) = delete;
  void raw_capture(bool enabled) noexcept { raw_capture_ = enabled; }
  void attach(int descriptor);
  void detach();
  void start(std::string_view packet, RawUiPacket raw = {});
  bool flush();
  bool pending() const noexcept { return offset_ < packet_.size(); }

private:
  bool raw_capture_ = false;
  RawUiOwner owner_;
  RawEventRef attach_{};
  std::optional<RawControlToken> cleanup_;
  RawUiPacket raw_{};
  int descriptor_ = -1, flags_ = 0;
  std::string_view packet_;
  std::size_t offset_ = 0;
};
} // namespace blackbird
