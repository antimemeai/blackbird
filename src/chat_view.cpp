#include "blackbird/chat_view.hpp"
#include "blackbird/display_text.hpp"
#include "blackbird/terminal.hpp"
#include "blackbird/workflows.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <clocale>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <unistd.h>

namespace blackbird {
namespace {
constexpr std::size_t display_limit = 64 * 1024;
constexpr std::size_t line_limit = 2048;
std::string_view color(Ink ink) {
  constexpr std::array colors{"\x1b[0m",
                              "\x1b[0;38;2;160;173;188m",
                              "\x1b[1;38;2;222;179;113m",
                              "\x1b[1;38;2;114;215;211m",
                              "\x1b[1;38;2;232;227;217m",
                              "\x1b[0;38;2;188;168;239;48;2;37;40;51m",
                              "\x1b[1;38;2;188;168;239;48;2;37;40;51m",
                              "\x1b[0;38;2;155;213;172;48;2;37;40;51m",
                              "\x1b[0;38;2;155;213;172m",
                              "\x1b[1;38;2;242;137;137m",
                              "\x1b[0;1;38;2;114;215;211m",
                              "\x1b[0;38;2;85;142;151m"};
  return colors[static_cast<std::size_t>(ink)];
}
void utf8(std::string &out, char32_t cp) {
  if (cp < 0x80)
    out += static_cast<char>(cp);
  else if (cp < 0x800) {
    out += static_cast<char>(0xc0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 63));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xe0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 63));
    out += static_cast<char>(0x80 | (cp & 63));
  } else {
    out += static_cast<char>(0xf0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 63));
    out += static_cast<char>(0x80 | ((cp >> 6) & 63));
    out += static_cast<char>(0x80 | (cp & 63));
  }
}
ChatRow inline_text(std::string_view text, Ink base) {
  ChatRow out;
  std::string pending;
  Ink current = base;
  auto flush = [&] {
    if (!pending.empty()) {
      out.push_back({std::move(pending), current});
      pending.clear();
    }
  };
  bool bold = false, code = false;
  while (!text.empty()) {
    if (text.starts_with("**") && !code) {
      flush();
      bold = !bold;
      current = bold ? Ink::heading : base;
      text.remove_prefix(2);
    } else if (text.front() == '`') {
      flush();
      code = !code;
      current = code ? Ink::code : (bold ? Ink::heading : base);
      text.remove_prefix(1);
    } else {
      pending += text.front();
      text.remove_prefix(1);
    }
  }
  flush();
  return out;
}
ChatRow syntax(std::string_view text) {
  ChatRow out;
  std::size_t pos = 0;
  constexpr std::array words{"auto", "const", "return", "if",       "else",
                             "for",  "while", "class",  "struct",   "void",
                             "int",  "bool",  "local",  "function", "end",
                             "true", "false", "nil",    "nullptr"};
  while (pos < text.size()) {
    const auto begin = pos;
    Ink ink = Ink::code;
    if (text[pos] == '"' || text[pos] == '\'') {
      const auto quote = text[pos++];
      while (pos < text.size()) {
        const auto c = text[pos++];
        if (c == '\\' && pos < text.size())
          ++pos;
        else if (c == quote)
          break;
      }
      ink = Ink::string;
    } else if ((text[pos] >= 'a' && text[pos] <= 'z') ||
               (text[pos] >= 'A' && text[pos] <= 'Z') || text[pos] == '_') {
      do {
        ++pos;
      } while (pos < text.size() &&
               ((text[pos] >= 'a' && text[pos] <= 'z') ||
                (text[pos] >= 'A' && text[pos] <= 'Z') ||
                (text[pos] >= '0' && text[pos] <= '9') || text[pos] == '_'));
      const auto word = text.substr(begin, pos - begin);
      if (std::find(words.begin(), words.end(), word) != words.end())
        ink = Ink::keyword;
    } else
      pos += display_scalar(text.substr(pos)).bytes;
    if (!out.empty() && out.back().ink == ink)
      out.back().text.append(text.substr(begin, pos - begin));
    else
      out.push_back({std::string{text.substr(begin, pos - begin)}, ink});
  }
  return out;
}
void wrap(std::vector<ChatRow> &rows, const ChatRow &content, std::size_t width,
          Ink rail) {
  ChatRow row{{"  │ ", rail}};
  std::size_t cells = 0;
  std::string pending;
  Ink previous = Ink::normal;
  auto flush = [&] {
    if (!pending.empty()) {
      row.push_back({std::move(pending), previous});
      pending.clear();
    }
  };
  DisplayCursor cursor{content};
  DisplayCluster cluster;
  while (cursor.next(cluster)) {
    if (!cluster.cells.value)
      continue;
    display_fit(cluster, {width});
    if (cluster.ink != previous) {
      flush();
      previous = cluster.ink;
    }
    if (cells && cells + cluster.cells.value > width) {
      flush();
      rows.push_back(std::move(row));
      row = {{"  │ ", rail}};
      cells = 0;
    }
    cursor.append(pending, cluster);
    cells += cluster.cells.value;
  }
  flush();
  rows.push_back(std::move(row));
}
bool equal(const ChatGrid &a, const ChatGrid &b, std::size_t i) {
  const auto &x = a.cells[i], &y = b.cells[i];
  return x.scalar == y.scalar && x.ink == y.ink && x.width == y.width &&
         std::string_view{a.tails}.substr(x.tail, x.length) ==
             std::string_view{b.tails}.substr(y.tail, y.length);
}
} // namespace
std::string chat_plain(const ChatRow &row) {
  std::string text;
  for (const auto &s : row)
    text += s.text;
  return text;
}
void ChatView::trim(RawControlToken *credit) {
  auto retire = [&](const Block &block, std::uint8_t reason, std::size_t offset,
                    std::size_t length) {
    (void)raw_ui_link(4032, block.identity,
                      std::array{detail::raw_native_field(reason),
                                 detail::raw_native_field(offset),
                                 detail::raw_native_field(length),
                                 detail::raw_native_field(generation_)},
                      credit);
  };
  // A front cut removes the head a fold measured from; display restarts plain.
  auto forget_fold = [](Block &block) {
    block.head_end = block.hidden = block.folded_bytes = 0;
    block.fence_parity = false;
  };
  bool removed = false;
  if (newlines_ > 2048 && !blocks_.empty()) {
    while (blocks_.size() > 1 && newlines_ > 1536) {
      newlines_ -= static_cast<std::size_t>(
          std::count(blocks_.front().text.begin(), blocks_.front().text.end(), '\n'));
      bytes_ -= blocks_.front().text.size();
      retire(blocks_.front(), 1, 0, blocks_.front().text.size());
      blocks_.pop_front();
      removed = true;
    }
    if (newlines_ > 2048) {
      auto &text = blocks_.front().text;
      std::size_t cut = 0;
      while (newlines_ > 1536) {
        cut = text.find('\n', cut) + 1;
        --newlines_;
      }
      retire(blocks_.front(), 2, 0, cut);
      text.erase(0, cut);
      forget_fold(blocks_.front());
      bytes_ = text.size();
      removed = true;
    }
  }
  while (blocks_.size() > 1 && (bytes_ > display_limit || blocks_.size() > 1024)) {
    bytes_ -= blocks_.front().text.size();
    newlines_ -= static_cast<std::size_t>(
        std::count(blocks_.front().text.begin(), blocks_.front().text.end(), '\n'));
    retire(blocks_.front(), 1, 0, blocks_.front().text.size());
    blocks_.pop_front();
    removed = true;
  }
  if (bytes_ > display_limit && !blocks_.empty()) {
    auto &s = blocks_.front().text;
    auto cut = s.size() - display_limit * 3 / 4;
    while (cut < s.size() && (static_cast<unsigned char>(s[cut]) & 0xc0) == 0x80)
      ++cut;
    newlines_ -= static_cast<std::size_t>(
        std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(cut), '\n'));
    retire(blocks_.front(), 2, 0, cut);
    s.erase(0, cut);
    forget_fold(blocks_.front());
    bytes_ = s.size();
    removed = true;
  }
  if (removed) {
    width_ = 0;
    ++generation_;
    clipped_ = true;
    dirty_ = true;
  }
}
void ChatView::append(ChatKind kind, std::string_view text, bool merge,
                      RawEventRef source) {
  const auto size = text.size();
  const auto ranges = raw_ui_add(1027, size / line_limit);
  // Ranges: 1025 block retirements, one prefix cut, line skips and one fold.
  const auto credit =
      raw_ui_add(raw_ui_multiply(raw_ui_add(ranges, 1), 65), raw_ui_error_bytes + 72);
  RawUiTrace trace{
      raw_capture_,
      {4030, 4033},
      std::array{detail::raw_native_field(kind), detail::raw_native_field(merge),
                 ByteView{source.lane}, detail::raw_native_field(source.ordinal),
                 detail::raw_native_field(size), detail::raw_string_field(text)},
      credit,
      true};
  trace.run([&] {
    try {
      if (!merge || blocks_.empty() || blocks_.back().kind != kind) {
        blocks_.push_back({kind, {}, true, 0, 0, 0, false, false, trace.reference()});
        ++serial_;
      }
      auto &block = blocks_.back();
      bool changed = block.dirty;
      std::optional<std::size_t> skipped;
      auto close_skip = [&](std::size_t end) {
        if (!skipped)
          return;
        const std::uint8_t reason = 4;
        const auto length = end - *skipped;
        (void)raw_ui_link(4032, trace.reference(),
                          std::array{detail::raw_native_field(reason),
                                     detail::raw_native_field(*skipped),
                                     detail::raw_native_field(length),
                                     detail::raw_native_field(generation_)},
                          trace.credit());
        skipped.reset();
      };
      for (std::size_t index = 0; index < text.size(); ++index) {
        const char c = text[index];
        if (c == '\n') {
          close_skip(index);
          block.text += c;
          ++bytes_;
          block.line_bytes = 0;
          ++newlines_;
          changed = true;
        } else if (block.line_bytes < line_limit ||
                   (block.line_bytes < line_limit + 4 &&
                    (static_cast<unsigned char>(c) & 0xc0) == 0x80)) {
          close_skip(index);
          block.text += c;
          ++bytes_;
          ++block.line_bytes;
          changed = true;
        } else {
          if (!skipped)
            skipped = index;
          if (!clipped_) {
            clipped_ = true;
            width_ = 0;
            changed = true;
          }
        }
      }
      close_skip(text.size());
      block.dirty = block.dirty || changed;
      dirty_ = dirty_ || changed;
      if (block.kind == ChatKind::tool && changed)
        fold(block, trace.credit());
      const auto identity = block.identity;
      const auto current = block.text.size();
      (void)trace.event(4031, std::array{ByteView{identity.lane},
                                         detail::raw_native_field(identity.ordinal),
                                         detail::raw_native_field(current)});
      trim(trace.credit());
      trace.close();
    } catch (...) {
      const auto count = blocks_.size();
      (void)trace.event(4073, std::array{detail::raw_native_field(count),
                                         detail::raw_native_field(bytes_),
                                         detail::raw_native_field(newlines_),
                                         detail::raw_native_field(generation_)});
      throw;
    }
  });
}
void ChatView::fold(Block &block, RawControlToken *credit) {
  constexpr std::size_t head_lines = 4, tail_lines = 3; // Header + 3, and 3.
  const std::string_view text{block.text};
  if (!block.head_end) {
    std::size_t end = 0;
    for (std::size_t n = 0; n < head_lines; ++n) {
      const auto lf = text.find('\n', end);
      if (lf == std::string_view::npos)
        return;
      end = lf + 1;
    }
    block.head_end = end;
  }
  // The tail is the last complete lines plus any unfinished one: it starts after
  // the (tail_lines + 1)th newline from the end. Only those bytes are scanned.
  std::size_t tail = text.size();
  for (std::size_t n = 0; n <= tail_lines; ++n) {
    if (tail <= block.head_end)
      return;
    const auto lf = text.rfind('\n', tail - 1);
    if (lf == std::string_view::npos || lf < block.head_end)
      return;
    tail = lf;
  }
  ++tail;
  const auto middle = text.substr(block.head_end, tail - block.head_end);
  const auto lines =
      static_cast<std::size_t>(std::count(middle.begin(), middle.end(), '\n'));
  // The elision row must stand for at least two lines the first time.
  if (lines < (block.hidden ? 1U : 2U))
    return;
  // Fences hidden in the middle still toggle code state for the tail.
  for (std::size_t at = 0; at < middle.size();) {
    if (middle.substr(at).starts_with("```"))
      block.fence_parity = !block.fence_parity;
    const auto lf = middle.find('\n', at);
    if (lf == std::string_view::npos)
      break;
    at = lf + 1;
  }
  // Coordinates are the block's display stream: bytes that entered block.text
  // (line-clipped bytes never do), with earlier folded ranges still counted.
  const std::uint8_t reason = 5;
  const auto offset = block.head_end + block.folded_bytes, length = middle.size();
  (void)raw_ui_link(4032, block.identity,
                    std::array{detail::raw_native_field(reason),
                               detail::raw_native_field(offset),
                               detail::raw_native_field(length),
                               detail::raw_native_field(generation_)},
                    credit);
  block.text.erase(block.head_end, length);
  block.hidden += lines;
  block.folded_bytes += length;
  bytes_ -= length;
  newlines_ -= lines;
  block.restart = block.dirty = dirty_ = true;
}
void ChatView::tool_start(std::string_view header, RawEventRef source) {
  append(ChatKind::tool, std::string{header} + "\n", false, source);
  tool_header_ = header;
  tool_block_ = serial_;
}
void ChatView::tool_output(std::string_view text, RawEventRef source) {
  constexpr std::string_view background = "\n[command ";
  if (const auto close = text.find("]\n");
      text.starts_with(background) && close != std::string_view::npos) {
    const auto job = text.substr(background.size(), close - background.size());
    const auto body = text.substr(close + 2);
    if (!blocks_.empty() && serial_ == background_block_ && job == background_job_) {
      append(ChatKind::tool, body, true, source);
      return;
    }
    append(ChatKind::tool, "[command " + std::string{job} + "]\n" + std::string{body},
           false, source);
    background_job_ = job;
    background_block_ = serial_;
    return;
  }
  const auto marker = text.starts_with("\n[") ? text.substr(1) : text;
  if ((blocks_.empty() || serial_ != tool_block_) && !tool_header_.empty()) {
    append(ChatKind::tool, tool_header_ + "\n" + std::string{marker}, false, source);
    tool_block_ = serial_;
    return;
  }
  const bool line_start = blocks_.empty() || blocks_.back().text.empty() ||
                          blocks_.back().text.back() == '\n';
  append(ChatKind::tool, line_start ? marker : text, true, source);
}
void ChatView::begin(ChatKind kind) {
  stream_kind_ = kind;
  append(kind, {});
}
void ChatView::stream(std::string_view text) { append(stream_kind_, text, true); }
ChatView &ChatView::operator+=(std::string_view notice) {
  append(ChatKind::notice, notice, true);
  return *this;
}
void ChatView::clear() {
  const auto count = blocks_.size();
  RawUiTrace trace{raw_capture_,
                   {4034, 4033},
                   std::array{detail::raw_native_field(count),
                              detail::raw_native_field(bytes_),
                              detail::raw_native_field(generation_)},
                   raw_ui_add(raw_ui_multiply(count, 65), 40 + raw_ui_error_bytes)};
  const std::uint8_t reason = 3;
  const std::size_t offset = 0;
  for (const auto &block : blocks_) {
    const auto size = block.text.size();
    (void)raw_ui_link(4032, block.identity,
                      std::array{detail::raw_native_field(reason),
                                 detail::raw_native_field(offset),
                                 detail::raw_native_field(size),
                                 detail::raw_native_field(generation_)},
                      trace.credit());
  }
  blocks_.clear();
  rows_.clear();
  bytes_ = 0;
  newlines_ = 0;
  clipped_ = false;
  ++generation_;
  width_ = 0;
  dirty_ = true;
  trace.finish();
}
const std::vector<ChatRow> &ChatView::rows(std::size_t width) {
  width = std::max<std::size_t>(width, 8);
  if (width != width_) {
    rows_.clear();
    if (clipped_)
      rows_.push_back(
          {{"  Display clipped; full content remains in audit.", Ink::muted}});
    for (auto &b : blocks_) {
      b.dirty = true;
      b.initialized = false;
      b.committed = 0;
      b.in_code = false;
    }
    width_ = width;
    dirty_ = true;
  }
  if (!dirty_)
    return rows_;
  for (auto &b : blocks_) {
    if (!b.dirty)
      continue;
    ++reflows_;
    const auto rail = b.kind == ChatKind::user        ? Ink::user
                      : b.kind == ChatKind::error     ? Ink::failure
                      : b.kind == ChatKind::assistant ? Ink::assistant
                                                      : Ink::muted;
    // A fold changed text before the committed offset: redraw this (last) block.
    if (b.restart && b.initialized) {
      rows_.resize(b.first_row);
      b.initialized = b.in_code = false;
      b.committed = 0;
    }
    b.restart = false;
    if (!b.initialized) {
      b.first_row = rows_.size();
      if (!rows_.empty())
        rows_.push_back({});
      const auto label = b.kind == ChatKind::user        ? "YOU"
                         : b.kind == ChatKind::assistant ? "BLACKBIRD"
                         : b.kind == ChatKind::tool      ? "ACTIVITY"
                         : b.kind == ChatKind::error     ? "ERROR"
                         : b.kind == ChatKind::summary   ? "TURN"
                                                         : "";
      if (label[0])
        rows_.push_back({{"  " + std::string{label}, rail}});
      b.stable_rows = rows_.size();
      b.initialized = true;
    }
    rows_.resize(b.stable_rows);
    auto remaining = std::string_view{b.text}.substr(b.committed);
    while (!remaining.empty()) {
      const auto lf = remaining.find('\n');
      const auto length = lf == std::string_view::npos ? remaining.size() : lf;
      const auto raw = remaining.substr(0, length);
      const auto safe = terminal_lines(raw, display_limit).front();
      bool code = b.in_code;
      const auto registry = displayed_workflows();
      if (b.kind == ChatKind::user && registry) {
        wrap(rows_, terminal_powerword_lines(raw, display_limit).front(), width - 4,
             rail);
      } else if (safe.starts_with("```")) {
        if (!code)
          wrap(rows_, {{"─ " + safe.substr(3) + " ─", Ink::code}}, width - 4, rail);
        code = !code;
      } else if (code)
        wrap(rows_, syntax(safe), width - 4, rail);
      else if (safe.starts_with("#")) {
        const auto start = safe.find_first_not_of("# ");
        wrap(rows_,
             {{start == std::string::npos ? "" : safe.substr(start), Ink::heading}},
             std::min<std::size_t>(width - 4, 92), rail);
      } else
        wrap(rows_,
             inline_text(safe, b.kind == ChatKind::summary ? Ink::muted
                               : b.kind == ChatKind::error ? Ink::failure
                                                           : Ink::normal),
             std::min<std::size_t>(width - 4, 92), rail);
      if (lf == std::string_view::npos)
        break;
      b.committed += length + 1;
      if (b.hidden && b.committed == b.head_end) {
        wrap(rows_,
             {{"┆ " + std::to_string(b.hidden) + " more lines · Ctrl-O inspects",
               Ink::muted}},
             width - 4, rail);
        code = code != b.fence_parity;
      }
      b.stable_rows = rows_.size();
      b.in_code = code;
      remaining.remove_prefix(length + 1);
    }
    b.dirty = false;
  }
  dirty_ = false;
  return rows_;
}
void ChatGrid::reset(std::size_t columns, std::size_t rows) {
  if (!columns || !rows || columns > 4096 || rows > 4096 ||
      rows > 1024 * 1024 / columns)
    throw std::length_error("terminal geometry exceeds bounded grid");
  width = columns;
  height = rows;
  cells.resize(width * height);
  std::fill(cells.begin(), cells.end(), ChatCell{});
  tails.clear();
}
void ChatGrid::line(std::size_t row, const ChatRow &content, std::size_t column) {
  if (row >= height || column >= width)
    return;
  DisplayCursor cursor{content};
  DisplayCluster cluster;
  while (cursor.next(cluster)) {
    const auto cells_used = cluster.cells.value;
    if (!cells_used)
      continue;
    if (column + cells_used > width)
      return;
    const auto erase_footprint = [&](std::size_t x) {
      auto &old = cells[row * width + x];
      if (old.width == 0 && x > 0)
        cells[row * width + x - 1] = ChatCell{};
      if (old.width == 2 && x + 1 < width)
        cells[row * width + x + 1] = ChatCell{};
      old = ChatCell{};
    };
    erase_footprint(column);
    if (cells_used == 2)
      erase_footprint(column + 1);
    auto &cell = cells[row * width + column];
    cell.scalar = cluster.scalar;
    cell.ink = cluster.ink;
    cell.width = static_cast<std::uint8_t>(cells_used);
    cell.tail = static_cast<std::uint32_t>(tails.size());
    cursor.append(tails, cluster, true);
    cell.length = static_cast<std::uint16_t>(tails.size() - cell.tail);
    column += cells_used;
    if (cells_used == 2) {
      cells[row * width + column - 1] = ChatCell{};
      cells[row * width + column - 1].width = 0;
    }
  }
}
std::uint64_t raw_ui_owned_grid_bound(const ChatGrid &grid) {
  constexpr std::uint64_t max_color =
      sizeof("\x1b[0;38;2;188;168;239;48;2;37;40;51m") - 1;
  constexpr std::uint64_t digits = std::numeric_limits<std::size_t>::digits10 + 1;
  constexpr std::uint64_t mode_bytes =
      sizeof("\x1b[?2026h\x1b[?25l\x1b[0m\x1b[;H\x1b[?25h\x1b[?2026l") - 1;
  return raw_ui_add(
      raw_ui_add(raw_ui_multiply(grid.cells.size(), 4 + max_color), grid.tails.size()),
      raw_ui_add(raw_ui_multiply(grid.height, 4 + 2 * digits),
                 mode_bytes + 2 * digits + sizeof("\x1b[?7l\x1b[?7h\x1b[0m\x1b[2J") -
                     1));
}
const std::string &ChatPainter::prepare(const ChatGrid &next, std::size_t cursor_row,
                                        std::size_t cursor_column, bool full,
                                        ChatPaintPolicy policy) {
  if (!next.width || !next.height)
    throw std::logic_error("cannot paint an empty grid");
  packet_.clear();
  touched_ = 0;
  cursor_row = std::min(cursor_row, next.height - 1);
  cursor_column = std::min(cursor_column, next.width - 1);
  const bool all = full || policy == ChatPaintPolicy::full || !valid_ ||
                   previous_.width != next.width || previous_.height != next.height;
  for (std::size_t y = 0; y < next.height; ++y) {
    std::size_t first = next.width, last = 0;
    for (std::size_t x = 0; x < next.width; ++x)
      if (all || !equal(next, previous_, y * next.width + x)) {
        first = std::min(first, x);
        last = x + 1;
      }
    if (first == next.width)
      continue;
    if (first && (next.cells[y * next.width + first].width == 0 ||
                  (!all && previous_.cells[y * next.width + first].width == 0)))
      --first;
    if (last < next.width &&
        (next.cells[y * next.width + last].width == 0 ||
         (!all && previous_.cells[y * next.width + last].width == 0)))
      ++last;
    if (packet_.empty()) {
      packet_ = "\x1b[?2026h\x1b[?25l";
      if (policy == ChatPaintPolicy::full)
        packet_ += "\x1b[0m\x1b[2J";
    }
    std::size_t erase_from = next.width;
    if (policy == ChatPaintPolicy::erase) {
      auto tail = next.width;
      while (tail) {
        const auto &cell = next.cells[y * next.width + tail - 1];
        if (cell.scalar != U' ' || cell.length || cell.width != 1 ||
            cell.ink != Ink::normal)
          break;
        --tail;
      }
      if (last > tail)
        erase_from = std::max(first, tail);
    }
    packet_ += "\x1b[" + std::to_string(y + 1) + ";" + std::to_string(first + 1) + "H";
    Ink active = Ink::normal;
    bool styled = false;
    for (auto x = first; x < std::min(last, erase_from); ++x) {
      const auto &cell = next.cells[y * next.width + x];
      ++touched_;
      if (!cell.width)
        continue;
      if (!styled || active != cell.ink) {
        styled = true;
        packet_ += color(cell.ink);
        active = cell.ink;
      }
      const bool corner = policy == ChatPaintPolicy::corner && y + 1 == next.height &&
                          x + cell.width == next.width;
      if (corner)
        packet_ += "\x1b[?7l";
      utf8(packet_, cell.scalar);
      packet_.append(next.tails, cell.tail, cell.length);
      if (corner)
        packet_ += "\x1b[?7h";
    }
    if (erase_from < next.width) {
      packet_ += color(Ink::normal);
      packet_ += "\x1b[" + std::to_string(y + 1) + ";" +
                 std::to_string(erase_from + 1) + "H\x1b[K";
      touched_ += next.width - erase_from;
    }
  }
  if (!packet_.empty() || !valid_ || cursor_row != cursor_row_ ||
      cursor_column != cursor_column_) {
    if (packet_.empty())
      packet_ = "\x1b[?2026h";
    packet_ += "\x1b[0m\x1b[" + std::to_string(cursor_row + 1) + ";" +
               std::to_string(cursor_column + 1) + "H\x1b[?25h\x1b[?2026l";
  }
  prepared_row_ = cursor_row;
  prepared_column_ = cursor_column;
  return packet_;
}
void ChatPainter::commit(const ChatGrid &next) {
  previous_ = next;
  cursor_row_ = prepared_row_;
  cursor_column_ = prepared_column_;
  valid_ = true;
}
void ChatPainter::commit_swap(ChatGrid &next) noexcept {
  using std::swap;
  swap(previous_, next);
  cursor_row_ = prepared_row_;
  cursor_column_ = prepared_column_;
  valid_ = true;
}
ChatOutput::~ChatOutput() {
  owner_.require_cleanup_thread();
  detach();
}
void ChatOutput::detach() {
  RawUiOwnerBinding binding{owner_};
  if (raw_.terminal) {
    RawUiOwnerBinding packet_binding{raw_.owner};
    const std::uint8_t branch = 4;
    (void)raw_ui_link(
        4042, raw_.frame,
        std::array{detail::raw_native_field(branch), detail::raw_native_field(offset_)},
        &*raw_.terminal);
    raw_.terminal.reset();
  }
  if (descriptor_ >= 0) {
    const auto request = raw_ui_link(4047, attach_,
                                     std::array{detail::raw_native_field(descriptor_),
                                                detail::raw_native_field(flags_),
                                                detail::raw_native_field(offset_)},
                                     cleanup_ ? &*cleanup_ : nullptr);
    const int result = ::fcntl(descriptor_, F_SETFL, flags_);
    const int error = errno;
    const bool failed = result < 0;
    if (failed)
      (void)raw_ui_link(4061, request,
                        std::array{detail::raw_native_field(result),
                                   detail::raw_native_field(failed),
                                   detail::raw_native_field(error)},
                        cleanup_ ? &*cleanup_ : nullptr);
    else
      (void)raw_ui_link(4061, request,
                        std::array{detail::raw_native_field(result),
                                   detail::raw_native_field(failed)},
                        cleanup_ ? &*cleanup_ : nullptr);
    errno = error;
  }
  cleanup_.reset();
  descriptor_ = -1;
  packet_ = {};
  offset_ = 0;
}
void ChatOutput::attach(int descriptor) {
  if (descriptor_ >= 0)
    throw std::logic_error("output already attached");
  auto owner = RawUiOwner{raw_capture_};
  RawUiOwnerBinding binding{owner};
  RawUiTrace trace{raw_capture_,
                   {4046, 4062},
                   std::array{detail::raw_native_field(descriptor)},
                   378};
  trace.run([&] {
    auto native_flags = [&](int operation, int argument) {
      const auto request =
          trace.event(4060, std::array{detail::raw_native_field(descriptor),
                                       detail::raw_native_field(operation),
                                       detail::raw_native_field(argument)});
      const int result = ::fcntl(descriptor, operation, argument);
      const int error = errno;
      const bool failed = result < 0;
      if (failed)
        (void)raw_ui_link(4061, request,
                          std::array{detail::raw_native_field(result),
                                     detail::raw_native_field(failed),
                                     detail::raw_native_field(error)},
                          trace.credit());
      else
        (void)raw_ui_link(4061, request,
                          std::array{detail::raw_native_field(result),
                                     detail::raw_native_field(failed)},
                          trace.credit());
      errno = error;
      return result;
    };
    const int flags = native_flags(F_GETFL, 0);
    if (flags < 0 || native_flags(F_SETFL, flags | O_NONBLOCK) < 0)
      throw std::runtime_error("cannot configure terminal output");
    auto cleanup = trace.split(105);
    owner_ = std::move(owner);
    descriptor_ = descriptor;
    flags_ = flags;
    attach_ = trace.reference();
    cleanup_ = std::move(cleanup);
    trace.finish();
  });
}
void ChatOutput::start(std::string_view packet, RawUiPacket raw) {
  owner_.require_thread();
  RawUiOwnerBinding binding{raw.owner};
  if (pending())
    throw std::logic_error("cannot replace an in-flight terminal packet");
  if ((owner_.selected() || (raw_capture_ && raw_enabled())) && !raw.terminal)
    throw Error{ErrorCode::invalid_identity};
  packet_ = packet;
  offset_ = 0;
  raw_ = std::move(raw);
  if (raw_.terminal) {
    const std::uint8_t branch = 2;
    (void)raw_ui_link(
        4042, raw_.frame,
        std::array{detail::raw_native_field(branch), detail::raw_native_field(offset_)},
        &*raw_.terminal);
  }
}
bool ChatOutput::flush() {
  owner_.require_thread();
  RawUiOwnerBinding binding{raw_.owner};
  for (unsigned call = 0; pending() && call < 8; ++call) {
    auto credit = raw_ui_control(raw_capture_, 113);
    const auto requested = std::min<std::size_t>(packet_.size() - offset_, 8192);
    const auto request = raw_ui_link(4044, raw_.frame,
                                     std::array{detail::raw_native_field(descriptor_),
                                                detail::raw_native_field(offset_),
                                                detail::raw_native_field(requested)},
                                     credit ? &*credit : nullptr);
    const auto count = ::write(descriptor_, packet_.data() + offset_, requested);
    const int error = errno;
    const bool failed = count < 0;
    if (failed)
      (void)raw_ui_link(4045, request,
                        std::array{detail::raw_native_field(count),
                                   detail::raw_native_field(failed),
                                   detail::raw_native_field(error)},
                        credit ? &*credit : nullptr);
    else
      (void)raw_ui_link(
          4045, request,
          std::array{detail::raw_native_field(count), detail::raw_native_field(failed)},
          credit ? &*credit : nullptr);
    errno = error;
    if (count > 0)
      offset_ += static_cast<std::size_t>(count);
    else if (count < 0 && error == EINTR)
      continue;
    else if (count < 0 && (error == EAGAIN || error == EWOULDBLOCK))
      return false;
    else
      throw std::runtime_error("terminal output transport failed");
  }
  const bool complete = !pending();
  if (complete && raw_.terminal) {
    const std::uint8_t branch = 3;
    (void)raw_ui_link(
        4042, raw_.frame,
        std::array{detail::raw_native_field(branch), detail::raw_native_field(offset_)},
        &*raw_.terminal);
    raw_.terminal.reset();
  }
  return complete;
}
void ChatPresenter::require_idle() const {
  if (state_ != State::idle)
    throw std::logic_error("cannot replace a prepared or writing terminal frame");
}
ChatGrid &ChatPresenter::grid() {
  require_idle();
  return grid_;
}
std::string &ChatPresenter::prepare(std::size_t cursor_row, std::size_t cursor_column,
                                    bool full, ChatPaintPolicy policy) {
  require_idle();
  // Return the previous packet's capacity before constructing the next delta.
  packet_.swap(painter_.packet_);
  (void)painter_.prepare(grid_, cursor_row, cursor_column, full, policy);
  packet_.swap(painter_.packet_);
  staged_ = {grid_.height, grid_.width, std::min(cursor_row, grid_.height - 1),
             std::min(cursor_column, grid_.width - 1), false};
  state_ = State::prepared;
  return packet_;
}
std::string &ChatPresenter::prepare_tiny(std::size_t rows, std::size_t columns,
                                         std::size_t cursor_row,
                                         std::size_t cursor_column) {
  require_idle();
  if (!rows || !columns || cursor_row >= rows || cursor_column >= columns)
    throw std::logic_error("invalid tiny terminal geometry");
  packet_.clear();
  staged_ = {rows, columns, cursor_row, cursor_column, true};
  state_ = State::prepared;
  return packet_;
}
void ChatPresenter::start(RawUiPacket raw) {
  if (state_ != State::prepared || packet_.empty())
    throw std::logic_error("terminal frame is not prepared for output");
  output_.start(packet_, std::move(raw));
  state_ = State::writing;
}
bool ChatPresenter::flush() {
  if (state_ != State::writing)
    return false;
  try {
    if (!output_.flush())
      return false;
  } catch (...) {
    detach();
    throw;
  }
  if (staged_.tiny)
    painter_.invalidate();
  else
    painter_.commit_swap(grid_);
  committed_ = staged_;
  state_ = State::idle;
  return true;
}
void ChatPresenter::detach() {
  output_.detach();
  painter_.invalidate();
  committed_ = {};
  state_ = State::idle;
}
void ChatPresenter::abandon() {
  if (state_ == State::writing)
    detach();
  else
    state_ = State::idle;
}
} // namespace blackbird
