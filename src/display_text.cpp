#include "blackbird/display_text.hpp"
#include "unicode_emoji.hpp"
#include "unicode_pictographic.hpp"
#include <algorithm>
#include <array>
#include <cwchar>

namespace blackbird {
DisplayScalar display_scalar(std::string_view text) {
  if (text.empty())
    return {0, 0, -1};
  const auto byte = static_cast<unsigned char>(text.front());
  if (byte >= 32 && byte < 127)
    return {byte, 1, 1};
  wchar_t wide{};
  std::mbstate_t state{};
  const auto count = std::mbrtowc(&wide, text.data(), text.size(), &state);
  if (count == 0 || count > text.size())
    return {byte, 1, -1};
  return {static_cast<char32_t>(wide), count, ::wcwidth(wide)};
}
std::string_view DisplayCursor::remaining(Position &position) const {
  if (!styled_)
    return text_.substr(position.offset);
  while (position.run < spans_.size() &&
         position.offset == spans_[position.run].text.size()) {
    ++position.run;
    position.offset = 0;
  }
  return position.run == spans_.size()
             ? std::string_view{}
             : std::string_view{spans_[position.run].text}.substr(position.offset);
}
void DisplayCursor::advance(Position &position, std::size_t bytes) const {
  while (bytes) {
    const auto fragment = remaining(position);
    const auto count = std::min(bytes, fragment.size());
    position.offset += count;
    position.total += count;
    bytes -= count;
  }
}
DisplayScalar DisplayCursor::scalar(Position position) const {
  auto fragment = remaining(position);
  if (fragment.size() >= 4 || !styled_)
    return display_scalar(fragment);
  std::array<char, 4> bytes{};
  std::size_t size = 0;
  while (size < bytes.size() && !fragment.empty()) {
    const auto count = std::min(fragment.size(), bytes.size() - size);
    std::copy_n(fragment.data(), count, bytes.data() + size);
    size += count;
    advance(position, count);
    fragment = remaining(position);
  }
  return display_scalar({bytes.data(), size});
}
bool DisplayCursor::next(DisplayCluster &out) {
  if (remaining(position_).empty())
    return false;
  const auto begin = position_;
  auto first = scalar(position_);
  advance(position_, first.bytes);
  std::size_t cells = static_cast<std::size_t>(std::max(0, first.cells));
  bool pictograph = detail::extended_pictographic(first.scalar);
  bool join = false;
  bool regional = first.scalar >= 0x1f1e6 && first.scalar <= 0x1f1ff;
  auto emoji_previous = first.scalar;
  bool modifier_base = detail::emoji_modifier_base(emoji_previous);
  // An unattached zero-advance scalar is omitted, never a fabricated blank.
  if (cells) {
    while (!remaining(position_).empty()) {
      const auto r = scalar(position_);
      if (r.scalar == U'\n' || r.scalar == U'\t')
        break;
      const bool region = r.scalar >= 0x1f1e6 && r.scalar <= 0x1f1ff;
      const bool modifier = r.scalar >= 0x1f3fb && r.scalar <= 0x1f3ff;
      const bool joined = join && detail::extended_pictographic(r.scalar);
      if (r.cells > 0 && !(modifier && modifier_base) && !joined &&
          !(region && regional))
        break;
      if (joined || (region && regional) || (modifier && modifier_base) ||
          (r.scalar == 0xfe0f && detail::emoji_base(emoji_previous)))
        cells = 2;
      advance(position_, r.bytes);
      join = r.scalar == 0x200d && pictograph;
      if (r.scalar == 0x200d)
        pictograph = false;
      else if (joined)
        pictograph = true;
      else if (!detail::grapheme_extend(r.scalar))
        pictograph = false;
      // The selected Unicode peer ignores ZWSP rather than storing a base.
      // Other emitted tails (including VS16) replace immediate emoji authority.
      // A bare keycap mark retains the base's existing presentation width.
      if (r.scalar != 0x200b) {
        emoji_previous = r.scalar;
        modifier_base = detail::emoji_modifier_base(emoji_previous);
      }
      regional = region && !regional;
    }
  }
  out = {};
  out.begin = {begin.total};
  out.end = {position_.total};
  out.scalar = first.scalar;
  out.first_bytes = first.bytes;
  out.cells = {cells};
  out.ink = styled_ ? spans_[begin.run].ink : Ink::normal;
  out.run = begin.run;
  out.offset = begin.offset;
  if (position_.total - begin.total > 4096) {
    out.replacement = true;
    out.scalar = U'�';
    out.cells = {1};
  }
  return true;
}
void DisplayCursor::append(std::string &out, const DisplayCluster &cluster,
                           bool tail_only) const {
  if (!cluster.cells.value)
    return;
  if (cluster.replacement) {
    if (!tail_only)
      out += "�";
    return;
  }
  Position position{cluster.run, cluster.offset, cluster.begin.value};
  if (tail_only)
    advance(position, cluster.first_bytes);
  std::size_t bytes = cluster.end.value - position.total;
  while (bytes) {
    const auto fragment = remaining(position);
    const auto count = std::min(bytes, fragment.size());
    out.append(fragment.substr(0, count));
    advance(position, count);
    bytes -= count;
  }
}
CellCount display_cells(std::string_view text) {
  DisplayCursor cursor{text};
  DisplayCluster cluster;
  CellCount result;
  while (cursor.next(cluster))
    result.value += cluster.cells.value;
  return result;
}
void display_fit(DisplayCluster &cluster, CellCount columns) {
  if (cluster.cells.value > std::max<std::size_t>(1, columns.value)) {
    cluster.replacement = true;
    cluster.scalar = U'�';
    cluster.cells = {1};
  }
}
DisplayPosition display_position(std::string_view text, ByteOffset source,
                                 CellCount columns) {
  DisplayCursor cursor{text};
  DisplayCluster cluster;
  DisplayPosition point;
  columns.value = std::max<std::size_t>(1, columns.value);
  while (cursor.next(cluster)) {
    if (source.value <= cluster.begin.value)
      break;
    if (cluster.scalar == U'\n') {
      ++point.row;
      point.column.value = 0;
    } else {
      display_fit(cluster, columns);
      if (point.column.value &&
          point.column.value + cluster.cells.value > columns.value) {
        ++point.row;
        point.column.value = 0;
      }
      point.column.value += cluster.cells.value;
    }
    if (source.value <= cluster.end.value)
      break;
  }
  return point;
}
} // namespace blackbird
