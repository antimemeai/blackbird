#include "blackbird/chat_view.hpp"
#include "blackbird/terminal.hpp"
#include <clocale>
#include <fstream>
#include <iostream>
#include <string>
using namespace blackbird;
int main(int argc, char **argv) {
  if (!std::setlocale(LC_CTYPE, "en_US.UTF-8") && !std::setlocale(LC_CTYPE, "C.UTF-8"))
    return 2;
  ChatGrid grid;
  grid.reset(24, 24);
  struct Case {
    const char *name;
    ChatRow text;
    std::size_t x;
  };
  std::string huge = "👩";
  for (unsigned i = 0; i < 2100; ++i)
    huge += "́";
  const Case cases[] = {
      {"invalid VS16 base", {{"A️X", Ink::normal}}, 1},
      {"invalid modifier base", {{"A🏽X", Ink::normal}}, 3},
      {"invalid keycap base", {{"A⃣X", Ink::normal}}, 1},
      {"leading combining", {{"́X", Ink::normal}}, 0},
      {"leading format", {{"​X", Ink::normal}}, 0},
      {"style split combining", {{"e", Ink::heading}, {"́X", Ink::normal}}, 1},
      {"wide replacement", {{huge + "X", Ink::normal}}, 1},
      {"valid emoji", {{"👩‍💻X", Ink::normal}}, 2},
      {"valid modifier", {{"👍🏽X", Ink::normal}}, 2},
      {"valid keycap", {{"1️⃣X", Ink::normal}}, 2},
      {"valid text selector", {{"☀︎X", Ink::normal}}, 1},
      {"style split emoji", {{"👩‍", Ink::heading}, {"💻X", Ink::normal}}, 2},
  };
  unsigned failures = 0;
  auto check = [&](bool ok, const char *why) {
    if (!ok) {
      std::cerr << "FAIL " << why << '\n';
      ++failures;
    }
  };
  for (std::size_t i = 0; i < std::size(cases); ++i) {
    grid.line(i, cases[i].text);
    check(grid.cells[i * grid.width + cases[i].x].scalar == U'X', cases[i].name);
  }
  check(grid.cells[6 * grid.width].width == 1, "replacement footprint is one");
  const std::string joined = "👩‍💻";
  for (std::size_t split = 0; split <= joined.size(); ++split) {
    grid.line(12, {{joined.substr(0, split), Ink::heading},
                   {joined.substr(split) + "X", Ink::normal}});
    check(grid.cells[12 * grid.width + 2].scalar == U'X',
          "style boundaries preserve byte/cell map");
  }
  grid.line(15, {{"  │ ", Ink::border}, {"́X", Ink::normal}});
  check(grid.cells[15 * grid.width + 4].scalar == U'X',
        "combining tail after rail does not invent cell");
  const auto caret = terminal_caret("> 👩‍💻X", {6}, {20});
  check(caret.row == 0 && caret.column.value == 4, "full cluster caret at inner byte");
  const auto wrapped_caret = terminal_caret("> 👩‍💻X", {14}, {4});
  check(wrapped_caret.row == 1 && wrapped_caret.column.value == 1,
        "wrap and caret share extent");
  const auto replacement_caret = terminal_caret(huge + "X", {huge.size()}, {1});
  check(replacement_caret.row == 0 && replacement_caret.column.value == 1,
        "replacement controls caret");
  const auto escaped_caret = terminal_caret("\t\x01X", {2}, {4});
  check(escaped_caret.row == 1 && escaped_caret.column.value == 2,
        "sanitizer maps source byte cursor");
  check(terminal_byte_at_column("\t\x01X", {6}).value == 2,
        "vertical inverse preserves raw source byte offsets");
  check(terminal_byte_at_column("👩‍💻X", {2}).value == 11,
        "vertical inverse uses full cluster boundary");
  const auto wrapped = terminal_lines("👩‍💻X", 2);
  check(wrapped.size() == 2 && wrapped[0] == "👩‍💻" && wrapped[1] == "X",
        "wrap keeps joined emoji intact");
  check(terminal_lines("👩‍💻X", 1) == std::vector<std::string>{"�", "X"},
        "one-cell row has visible wide fallback");
  const auto narrow_caret = terminal_caret("👩‍💻X", {11}, {1});
  check(narrow_caret.row == 0 && narrow_caret.column.value == 1,
        "one-cell row fallback controls caret");
  const auto newline_caret = terminal_caret("A\n", {2}, {1});
  check(newline_caret.row == 1 && newline_caret.column.value == 0,
        "trailing newline maps to empty row");
  grid.line(13, {{huge + "X", Ink::normal}}, 22);
  check(grid.cells[13 * grid.width + 22].scalar == U'�' &&
            grid.cells[13 * grid.width + 23].scalar == U'X',
        "last-cell replacement");
  grid.line(14, {{"👩‍💻", Ink::normal}});
  grid.line(14, {{huge + "X", Ink::normal}});
  check(grid.cells[14 * grid.width + 1].scalar == U'X' &&
            grid.cells[14 * grid.width + 1].width == 1,
        "overwrite former continuation");
  const auto oversized_lines = terminal_lines(huge + "X", 1);
  check(oversized_lines == std::vector<std::string>{"�", "X"},
        "replacement extent controls wrap");
  grid.line(16, {{"1️X", Ink::normal}});
  grid.line(17, {{"#️X", Ink::normal}});
  grid.line(18, {{"*️X", Ink::normal}});
  for (std::size_t row = 0; row < std::size(cases); ++row)
    grid.line(row, {{"│", Ink::border}}, 20);
  grid.line(20, {{"👍​🏽X", Ink::normal}});
  grid.line(21, {{"👍️🏽X", Ink::normal}});
  grid.line(22, {{"👩‍1⃣X", Ink::normal}});
  check(grid.cells[20 * grid.width + 2].scalar == U'X',
        "named Unicode profile ignores ZWSP for modifier admission");
  check(grid.cells[21 * grid.width + 4].scalar == U'X',
        "VS16 clears immediate modifier base");
  check(grid.cells[22 * grid.width + 3].scalar == U'X',
        "bare keycap suffix does not promote digit");
  grid.line(23, {{"DONE", Ink::normal}});
  if (argc == 2) {
    ChatPainter painter;
    std::ofstream out(argv[1], std::ios::binary);
    out << "\x1b[?2027h" << painter.prepare(grid, 23, 4, true);
    if (!out)
      return 2;
  }
  return failures ? 1 : 0;
}
