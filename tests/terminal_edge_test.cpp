#include "blackbird/chat_view.hpp"
#include <clocale>
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace blackbird;
namespace {
void check(bool yes, const char *message) {
  if (!yes)
    throw std::runtime_error{message};
}
std::size_t occurrences(std::string_view text, std::string_view needle) {
  std::size_t count = 0, at = 0;
  while ((at = text.find(needle, at)) != std::string_view::npos) {
    ++count;
    at += needle.size();
  }
  return count;
}
void erase() {
  ChatPainter painter;
  ChatGrid grid;
  grid.reset(12, 3);
  for (std::size_t y = 0; y < grid.height; ++y)
    grid.line(y, {{"ABCDEFGHIJKL", Ink::normal}});
  (void)painter.prepare(grid, 0, 0);
  painter.commit(grid);
  grid.reset(12, 3);
  grid.line(0, {{"0abc", Ink::normal}});
  grid.line(1, {{"1abc", Ink::normal}});
  grid.line(2, {{"2abc        ", Ink::code}});
  const auto packet = painter.prepare(grid, 0, 0, false, ChatPaintPolicy::erase);
  check(occurrences(packet, "\x1b[K") == 2,
        "normal physical tails erase; colored tail must retain actual ink");
  check(packet.find("2abc        ") != std::string::npos,
        "colored spaces are not replaced with unqualified background erase");
  check(packet.size() <= raw_ui_owned_grid_bound(grid),
        "literal erase packet fits raw owned-grid bound");
}
void corner() {
  ChatGrid grid;
  grid.reset(4, 2);
  grid.line(1, {{"界", Ink::normal}}, 2);
  ChatPainter painter;
  const auto packet = painter.prepare(grid, 0, 0, false, ChatPaintPolicy::corner);
  check(packet.find("\x1b[?7l界\x1b[?7h") != std::string::npos,
        "complete wide footprint at bottom-right is protected and wrap restored");
  check(occurrences(packet, "\x1b[?7l") == 1 && occurrences(packet, "\x1b[?7h") == 1,
        "only one corner bracket");
  check(packet.size() <= raw_ui_owned_grid_bound(grid),
        "literal corner packet fits raw owned-grid bound");
  grid.reset(1, 1);
  grid.line(0, {{"é", Ink::normal}});
  const auto tiny = painter.prepare(grid, 0, 0, false, ChatPaintPolicy::corner);
  check(tiny.find("\x1b[?7lé\x1b[?7h") != std::string::npos,
        "one-cell combining footprint protected as whole cluster");
}
void full() {
  ChatGrid grid;
  grid.reset(5, 2);
  ChatPainter painter;
  (void)painter.prepare(grid, 0, 0);
  painter.commit(grid);
  const auto packet = painter.prepare(grid, 0, 0, false, ChatPaintPolicy::full);
  check(packet.find("\x1b[2J") != std::string::npos,
        "full reference erases physical viewport on every request");
  check(painter.touched() == 10,
        "full reference redraws every physical cell even on unchanged plane");
  check(packet.size() <= raw_ui_owned_grid_bound(grid),
        "literal full packet fits raw owned-grid bound");
}
void addressed() {
  ChatGrid grid;
  grid.reset(4, 2);
  grid.line(0, {{"ABCD", Ink::normal}});
  grid.line(1, {{"EFGH", Ink::normal}});
  ChatPainter painter;
  const auto packet = painter.prepare(grid, 0, 0, false, ChatPaintPolicy::addressed);
  check(packet.find("ABCD") != std::string::npos &&
            packet.find("EFGH") != std::string::npos,
        "addressed plane includes final physical column and bottom-right");
  check(packet.find("\x1b[?7l") == std::string::npos,
        "addressed policy does not mutate autowrap");
  painter.commit(grid);
  check(painter.prepare(grid, 0, 0).empty(),
        "unchanged plane emits no unsolicited frame");
}
} // namespace
int main(int argc, char **argv) {
  try {
    std::setlocale(LC_ALL, "");
    const std::string_view name = argc > 1 ? argv[1] : "erase";
    if (name == "erase")
      erase();
    else if (name == "corner")
      corner();
    else if (name == "full")
      full();
    else if (name == "addressed")
      addressed();
    else
      throw std::runtime_error{"unknown edge case"};
    std::cout << name << " native edge contract passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
