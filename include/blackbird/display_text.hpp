#pragma once
#include "blackbird/chat_view.hpp"
#include <span>

namespace blackbird {
struct ByteOffset {
  std::size_t value = 0;
};
struct CellCount {
  std::size_t value = 0;
};
struct DisplayScalar {
  char32_t scalar;
  std::size_t bytes;
  int cells;
};
// Locale scalar decoding is owned here; a scalar width is not a cluster width.
DisplayScalar display_scalar(std::string_view text);
struct DisplayCluster {
  ByteOffset begin, end;
  CellCount cells;
  char32_t scalar = U' ';
  std::size_t first_bytes = 0;
  Ink ink = Ink::normal;
  bool replacement = false;

private:
  friend class DisplayCursor;
  std::size_t run = 0, offset = 0;
};
// Borrowed forward cursor. Styled runs never introduce segmentation boundaries.
// Input is terminal-safe UTF-8; terminal_lines owns source sanitization.
class DisplayCursor {
public:
  explicit DisplayCursor(std::string_view text) : text_(text) {}
  explicit DisplayCursor(const ChatRow &row) : spans_(row), styled_(true) {}
  bool next(DisplayCluster &cluster);
  // cluster comes from this cursor; output must not alias the borrowed source.
  void append(std::string &out, const DisplayCluster &cluster,
              bool tail_only = false) const;

private:
  struct Position {
    std::size_t run = 0, offset = 0, total = 0;
  };
  std::string_view text_;
  std::span<const ChatSpan> spans_;
  bool styled_ = false;
  Position position_;
  std::string_view remaining(Position &position) const;
  void advance(Position &position, std::size_t bytes) const;
  DisplayScalar scalar(Position position) const;
};
CellCount display_cells(std::string_view text);
// A cluster wider than the whole destination row has a one-cell visible fallback.
void display_fit(DisplayCluster &cluster, CellCount columns);
struct DisplayPosition {
  std::size_t row = 0;
  CellCount column;
};
// A byte offset inside a displayed cluster uses that cluster's trailing edge.
DisplayPosition display_position(std::string_view text, ByteOffset source,
                                 CellCount columns);
} // namespace blackbird
