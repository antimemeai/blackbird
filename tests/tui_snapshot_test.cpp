#include "blackbird/chat_view.hpp"
#include "blackbird/context.hpp"
#include "blackbird/task_view.hpp"
#include "blackbird/terminal.hpp"
#include <algorithm>
#include <array>
#include <clocale>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

using namespace blackbird;
namespace {
void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}
void utf8(std::string &out, char32_t scalar) {
  if (scalar < 0x80)
    out += static_cast<char>(scalar);
  else if (scalar < 0x800) {
    out += static_cast<char>(0xc0 | (scalar >> 6));
    out += static_cast<char>(0x80 | (scalar & 63));
  } else if (scalar < 0x10000) {
    out += static_cast<char>(0xe0 | (scalar >> 12));
    out += static_cast<char>(0x80 | ((scalar >> 6) & 63));
    out += static_cast<char>(0x80 | (scalar & 63));
  } else {
    out += static_cast<char>(0xf0 | (scalar >> 18));
    out += static_cast<char>(0x80 | ((scalar >> 12) & 63));
    out += static_cast<char>(0x80 | ((scalar >> 6) & 63));
    out += static_cast<char>(0x80 | (scalar & 63));
  }
}
std::string escaped(std::string_view text) {
  std::string out;
  constexpr char digits[] = "0123456789abcdef";
  for (const char character : text) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte == '\\' || byte == '{' || byte == '}' || byte == '|') {
      out += '\\';
      out += static_cast<char>(byte);
    } else if (byte < 32 || byte == 127) {
      out += "\\x";
      out += digits[byte >> 4];
      out += digits[byte & 15];
    } else
      out += static_cast<char>(byte);
  }
  return out;
}
struct Snapshot {
  std::string name;
  ChatGrid grid;
  std::optional<std::pair<std::size_t, std::size_t>> cursor;
  std::string state;
};
std::string encode(const Snapshot &snapshot) {
  constexpr std::array names{"normal",  "muted",   "user",     "assistant",
                             "heading", "code",    "keyword",  "string",
                             "success", "failure", "selected", "border"};
  const auto &grid = snapshot.grid;
  if (grid.width == 0 || grid.height == 0)
    throw std::runtime_error("snapshot fixtures require nonzero grid geometry");
  require(grid.cells.size() == grid.width * grid.height, "invalid fixture grid");
  std::string out = "styled-grid-v1\nsize " + std::to_string(grid.width) + " " +
                    std::to_string(grid.height) + "\ncursor ";
  if (snapshot.cursor) {
    const auto [row, column] = *snapshot.cursor;
    require(row < grid.height && column < grid.width, "fixture cursor outside grid");
    out += std::to_string(row) + " " + std::to_string(column);
  } else
    out += "none";
  out += "\nstate " + escaped(snapshot.state) + "\n";
  for (std::size_t row = 0; row < grid.height; ++row) {
    out += "|";
    std::size_t column = 0;
    while (column < grid.width) {
      const auto ink = grid.cells[row * grid.width + column].ink;
      const auto index = static_cast<std::size_t>(ink);
      require(index < names.size(), "unrecognized Ink in snapshot");
      out += "{" + std::string{names[index]} + ":";
      do {
        const auto &cell = grid.cells[row * grid.width + column];
        std::string glyph;
        utf8(glyph, cell.scalar);
        require(cell.tail <= grid.tails.size() &&
                    cell.length <= grid.tails.size() - cell.tail,
                "invalid fixture cluster tail");
        glyph.append(grid.tails, cell.tail, cell.length);
        // A visible marker preserves the exact continuation cell and its Ink.
        out +=
            cell.width == 0 ? "<continuation:" + escaped(glyph) + ">" : escaped(glyph);
        ++column;
      } while (column < grid.width && grid.cells[row * grid.width + column].ink == ink);
      out += "}";
    }
    out += "|\n";
  }
  out += "cell-widths";
  for (std::size_t index = 0; index < grid.cells.size(); ++index)
    if (grid.cells[index].width != 1)
      out += " " + std::to_string(index / grid.width) + ":" +
             std::to_string(index % grid.width) + "=" +
             std::to_string(grid.cells[index].width);
  return out + "\n";
}
Snapshot rows(std::string name, const std::vector<ChatRow> &content, std::size_t width,
              std::size_t height) {
  Snapshot result{std::move(name), {}, std::nullopt, "component rows"};
  result.grid.reset(width, height);
  for (std::size_t row = 0; row < std::min(height, content.size()); ++row)
    result.grid.line(row, content[row]);
  return result;
}
InputResult feed(Composer &composer, std::string_view bytes) {
  InputResult result;
  for (const char byte : bytes) {
    auto next = composer.feed(byte);
    if (next.action != InputAction::none)
      result = std::move(next);
  }
  return result;
}
Snapshot composer_frame(std::string name, const Composer &composer, std::size_t width) {
  const auto content = terminal_powerword_lines("> " + composer.text(), width);
  const auto prefix =
      terminal_lines("> " + composer.text().substr(0, composer.cursor()), width);
  auto result = rows(std::move(name), content, width, 5);
  std::size_t columns = 0;
  std::mbstate_t state{};
  auto remain = std::string_view{prefix.back()};
  while (!remain.empty()) {
    wchar_t scalar{};
    const auto count = std::mbrtowc(&scalar, remain.data(), remain.size(), &state);
    require(count != 0 && count <= remain.size(), "invalid displayed composer UTF-8");
    columns += static_cast<std::size_t>(std::max(0, ::wcwidth(scalar)));
    remain.remove_prefix(count);
  }
  result.cursor = {prefix.size() - 1, std::min(columns, width - 1)};
  result.state = "draft=" + composer.text() +
                 " cursor_bytes=" + std::to_string(composer.cursor()) +
                 " pasted=" + (composer.pasted() ? "true" : "false");
  return result;
}
Snapshot palette_frame(std::string name, const Composer &composer) {
  std::vector<ChatRow> content;
  for (const auto &line : composer.palette_lines(6)) {
    const auto clipped = terminal_lines(line, 44).front();
    const bool selected = clipped.starts_with("> ");
    if (selected || clipped.starts_with("  /")) {
      const auto end = clipped.find(' ', 2);
      const auto split = end == std::string::npos ? clipped.size() : end;
      content.push_back(
          {{clipped.substr(0, split), selected ? Ink::selected : Ink::normal},
           {clipped.substr(split), Ink::muted}});
    } else
      content.push_back(
          {{clipped, line.starts_with("Commands") ? Ink::heading : Ink::muted}});
  }
  auto result = rows(std::move(name), content, 44, 6);
  result.state = "palette=" + std::string{composer.palette_open() ? "open" : "closed"} +
                 " hidden_draft=" + composer.text() +
                 " cursor_bytes=" + std::to_string(composer.cursor());
  return result;
}
Value task(std::string id, std::string title, std::string status,
           std::string parent = {}, std::string blocker = {}, std::string owner = {}) {
  return Value::object({{"id", Value{std::move(id)}},
                        {"title", Value{std::move(title)}},
                        {"status", Value{std::move(status)}},
                        {"parent", Value{std::move(parent)}},
                        {"blocker", Value{std::move(blocker)}},
                        {"owner", Value{std::move(owner)}}});
}
std::vector<Snapshot> scenarios() {
  std::vector<Snapshot> output;
  ChatView joiners;
  joiners.append(ChatKind::assistant, "A\u200dB\u200dC \u200d \u200d XZ");
  output.push_back(rows("conversation-plain-joiners", joiners.rows(12), 12, 5));

  ChatView conversation;
  conversation.append(ChatKind::user, "Inspect the parser.");
  conversation.append(ChatKind::assistant,
                      "# Findings\nUse `read` before changing code.\n"
                      "```cpp\nauto result = \"ready\";\n```\n");
  conversation.append(ChatKind::tool, "check-parser: exit 1");
  conversation.append(ChatKind::error, "Result remains unresolved.");
  output.push_back(rows("conversation-ordinary", conversation.rows(52), 52, 20));
  output.push_back(rows("conversation-narrow", conversation.rows(24), 24, 24));

  Composer editor;
  editor.draft("abcd\nx\n12345");
  feed(editor, "\x1b[A\x1b[A");
  require(editor.cursor() == 4, "multiline sticky-column cursor changed");
  output.push_back(composer_frame("composer-multiline", editor, 20));
  Composer pasted;
  feed(pasted, "\x1b[200~/hel\t\n界é\x1b[201~");
  require(pasted.text() == "/hel\t\n界é", "paste became a command or completion");
  output.push_back(composer_frame("composer-literal-paste", pasted, 20));
  Composer unicode;
  unicode.draft("a界éz\nx\n1234");
  feed(unicode, "\x1b[A\x1b[A");
  output.push_back(composer_frame("composer-unicode", unicode, 12));

  auto parent = task("t1", "Build panel", "active", "", "", "operator");
  parent.object().emplace_back("subtasks", Value{Number{1}});
  parent.object().emplace_back("subtasks_done", Value{Number{1}});
  const auto page = Value::object(
      {{"title", Value{"Parser work"}},
       {"bead", Value{"demo-17"}},
       {"counts", Value::object({{"done", Value{Number{0}}},
                                 {"active", Value{Number{1}}},
                                 {"blocked", Value{Number{1}}},
                                 {"queued", Value{Number{1}}}})},
       {"items", Value{Value::Array{parent, task("t2", "Read 界", "done", "t1"),
                                    task("t3", "Investigate failure", "blocked", "",
                                         "Need exact original", "Kimi"),
                                    task("t4", "Compare results", "queued")}}},
       {"next", Value{}}});
  const std::map<std::string, Value> activity{
      {"t1", Value::object({{"phase", Value{"running"}}})}};
  output.push_back(rows("tasks-ordinary", task_pane_rows(page, 36, activity), 36, 18));
  output.push_back(rows("tasks-narrow", task_pane_rows(page, 18, activity), 18, 24));
  output.push_back(rows("tasks-tiny", task_pane_rows(page, 7, activity), 7, 3));

  Composer palette;
  palette.draft("unsent operator draft");
  palette.choices(
      {{"/resume /fixture/alpha", "", "first retained session", "Session", "Alpha"},
       {"/resume /fixture/beta", "", "second retained session", "Session", "Beta"}},
      "Sessions");
  feed(palette, "\x1b[B");
  output.push_back(palette_frame("palette-selected", palette));
  const auto selected = feed(palette, "\r");
  require(selected.text == "/resume /fixture/beta" &&
              selected.action == InputAction::choose_command &&
              palette.text() == "unsent operator draft",
          "palette selection changed target or hidden draft");
  output.push_back(composer_frame("palette-return-draft", palette, 32));
  palette.choices(
      {{"/resume /fixture/alpha", "", "first retained session", "Session", "Alpha"}},
      "Sessions");
  feed(palette, "zzzz");
  output.push_back(palette_frame("palette-no-match", palette));
  return output;
}
} // namespace
int main(int argc, char **argv) {
  try {
    require(argc == 2, "usage: tui_snapshot_test OUTPUT_DIRECTORY");
    require(std::setlocale(LC_CTYPE, "en_US.UTF-8") != nullptr ||
                std::setlocale(LC_CTYPE, "C.UTF-8") != nullptr,
            "snapshot fixtures require a UTF-8 locale");
    const std::filesystem::path directory{argv[1]};
    std::filesystem::create_directories(directory);
    const auto snapshots = scenarios();
    for (const auto &snapshot : snapshots) {
      std::ofstream file{directory / (snapshot.name + ".snap"), std::ios::binary};
      require(static_cast<bool>(file), "cannot create actual snapshot");
      file << encode(snapshot);
      file.flush();
      require(static_cast<bool>(file), "cannot write actual snapshot");
    }
    std::cout << snapshots.size() << " deterministic styled snapshots\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
