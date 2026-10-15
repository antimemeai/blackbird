#include "blackbird/chat_view.hpp"
#include "blackbird/sprite.hpp"
#include "blackbird/terminal.hpp"
#include <cerrno>
#include <chrono>
#include <clocale>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <unistd.h>
using namespace blackbird;
void require(bool yes, const char *why) {
  if (!yes)
    throw std::runtime_error(why);
}
std::string plain(const std::vector<ChatRow> &rows) {
  std::string s;
  for (const auto &r : rows)
    s += chat_plain(r) + "\n";
  return s;
}
std::string hex(std::string_view bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (const char byte : bytes) {
    const auto c = static_cast<unsigned char>(byte);
    result += digits[c >> 4];
    result += digits[c & 15];
  }
  return result;
}
void presenter_pipe() {
  static_assert(!std::is_move_constructible_v<ChatPresenter>);
  int descriptors[2];
  require(::pipe(descriptors) == 0, "presenter pipe setup");
  (void)::fcntl(descriptors[0], F_SETFL, O_NONBLOCK);
  {
    ChatPresenter presenter;
    presenter.attach(descriptors[1]);
    auto &grid = presenter.grid();
    grid.reset(20, 4);
    grid.line(0, {{"A", Ink::normal}});
    const auto initial = presenter.prepare(2, 3);
    presenter.start();
    require(presenter.flush() && !presenter.flush(), "completion consumed once");
    require(presenter.geometry().rows == 4 && presenter.geometry().cursor_column == 3,
            "completed normal geometry");
    std::string received;
    char data[16384];
    auto count = ::read(descriptors[0], data, sizeof(data));
    require(count > 0, "initial presenter bytes");
    received.append(data, static_cast<std::size_t>(count));
    require(received == initial, "initial exact packet");
    presenter.grid().reset(20, 4);
    presenter.grid().line(0, {{"B", Ink::normal}});
    auto &packet = presenter.prepare(1, 8);
    packet.append(256 * 1024, 'q'); // Final graphics-like augmentation is owned.
    const auto expected = packet;
    presenter.start();
    require(!presenter.flush() && presenter.pending(), "owned backpressure");
    require(presenter.geometry().cursor_column == 3,
            "partial output does not publish cursor");
    bool refused = false;
    try {
      (void)presenter.prepare_tiny(2, 8, 0, 1);
    } catch (const std::logic_error &) {
      refused = true;
    }
    require(refused, "inflight frame cannot be replaced");
    received.clear();
    unsigned completions = 0;
    for (unsigned round = 0;
         round < 1000 && (presenter.pending() || received.size() < expected.size());
         ++round) {
      count = ::read(descriptors[0], data, sizeof(data));
      if (count > 0)
        received.append(data, static_cast<std::size_t>(count));
      completions += presenter.flush() ? 1U : 0U;
    }
    require(received == expected && completions == 1 &&
                presenter.geometry().cursor_column == 8,
            "owned exact suffix and matching completion");
    auto &tiny = presenter.prepare_tiny(2, 8, 0, 1);
    tiny = "x"; // Short string storage must remain inside the immovable owner.
    presenter.start();
    require(presenter.flush() && presenter.geometry().tiny,
            "tiny completion is explicit");
    require(::read(descriptors[0], data, sizeof(data)) == 1 && data[0] == 'x',
            "short owned packet");
    presenter.grid().reset(20, 4);
    require(!presenter.prepare(1, 8).empty(), "tiny invalidates normal baseline");
    presenter.abandon();
  }
  require((::fcntl(descriptors[1], F_GETFL) & O_NONBLOCK) == 0,
          "presenter restores descriptor flags");
  ::close(descriptors[0]);
  ::close(descriptors[1]);
}
int main(int argc, char **) {
  try {
    (void)std::setlocale(LC_CTYPE, "en_US.UTF-8");
    if (argc > 1) {
      int descriptors[2];
      require(::pipe(descriptors) == 0, "oracle presenter pipe");
      ChatPresenter presenter;
      presenter.attach(descriptors[1]);
      for (int i = 0; i < 8; ++i) {
        if (i == 6) {
          auto &tiny = presenter.prepare_tiny(2, 8, 0, 1);
          tiny = "\x1b[?2026h\x1b[H\x1b[2Jx\x1b[1;2H\x1b[?2026l";
          const auto expected = tiny;
          presenter.start();
          require(presenter.flush(), "tiny oracle completion");
          char data[4096];
          const auto count = ::read(descriptors[0], data, sizeof(data));
          require(count > 0, "tiny oracle bytes");
          std::cout << "8 2\n"
                    << hex(std::string_view{data, static_cast<std::size_t>(count)})
                    << "\n"
                    << hex(expected) << "\n";
          continue;
        }
        auto &frame = presenter.grid();
        frame.reset(i == 4 ? 12 : 20, 4);
        frame.line(0, {{i == 0   ? "hello long"
                        : i == 1 ? "界éx"
                        : i == 2 ? "h"
                                 : "👩‍💻🇺🇸",
                        Ink::assistant}});
        frame.line(1, {{"keyword", Ink::keyword}, {" string", Ink::string}});
        ChatPainter full;
        (void)presenter.prepare(3, 2);
        const auto cold = full.prepare(frame, 3, 2);
        const auto width = frame.width, height = frame.height;
        presenter.start();
        require(presenter.flush(), "oracle presenter completion");
        char data[4096];
        const auto count = ::read(descriptors[0], data, sizeof(data));
        require(count > 0, "oracle presenter bytes");
        const std::string_view packet{data, static_cast<std::size_t>(count)};
        std::cout << width << " " << height << "\n"
                  << hex(packet) << "\n"
                  << hex(cold) << "\n";
      }
      presenter.detach();
      ::close(descriptors[0]);
      ::close(descriptors[1]);
      return 0;
    }
    presenter_pipe();
    const auto &idle_sprite = blackbird_sprite(false, 0);
    require(&idle_sprite == &blackbird_sprite(false, 999), "idle mascot animates");
    require(blackbird_sprite(true, 0) != blackbird_sprite(true, 1),
            "busy exhaust static");
    ChatGrid sprite_grid;
    sprite_grid.reset(40, blackbird_sprite_height);
    sprite_grid.line(0, {{"HEADER", Ink::normal}});
    for (std::size_t y = 0; y < idle_sprite.size(); ++y)
      sprite_grid.line(y, {{idle_sprite[y], Ink::assistant}}, 22);
    require(sprite_grid.cells[0].scalar == U'H' && sprite_grid.cells[5].scalar == U'R',
            "sprite blit overwrites header");
    sprite_grid.line(0, {{idle_sprite[0], Ink::assistant}}, 40);
    require(sprite_grid.cells[0].scalar == U'H', "offscreen sprite corrupts cells");
    sprite_grid.reset(40, blackbird_sprite_height);
    sprite_grid.line(0, {{"界x", Ink::normal}});
    sprite_grid.line(0, {{"h", Ink::normal}}, 1);
    require(sprite_grid.cells[0].scalar == U' ' && sprite_grid.cells[0].width == 1 &&
                sprite_grid.cells[1].scalar == U'h' &&
                sprite_grid.cells[2].scalar == U'x',
            "offset blit leaves wide-cell leader");
    sprite_grid.line(0, {{"界", Ink::normal}}, 2);
    sprite_grid.line(0, {{"界", Ink::normal}}, 1);
    require(sprite_grid.cells[1].width == 2 && sprite_grid.cells[2].width == 0 &&
                sprite_grid.cells[3].width == 1,
            "overlapping wide-cell blit leaves stale continuation");
    for (const auto &[width, height] :
         std::array<std::pair<std::size_t, std::size_t>, 4>{
             {{40, 12}, {80, 18}, {80, 30}, {120, 20}}}) {
      const auto startup = blackbird_startup(width, height);
      require(startup.size() <= height, "startup art exceeds viewport height");
      for (const auto &line : startup)
        require(terminal_lines(line, width) == std::vector<std::string>{line},
                "startup art exceeds terminal cell width");
    }
    require(blackbird_startup(80, 30, false).size() == 6,
            "startup wordmark must have six rows");
    ChatView view;
    view.append(ChatKind::user, "hola");
    view.begin(ChatKind::assistant);
    view.stream("¡Hola! ¿En qué te ayudo?");
    auto text = plain(view.rows(80));
    require(text.find("YOU") != std::string::npos &&
                text.find("BLACKBIRD") != std::string::npos,
            "typed roles missing");
    require(text.find("JSON bytes") == std::string::npos, "diagnostics leak");
    const auto count = view.reflows();
    for (int i = 0; i < 100; ++i)
      (void)view.rows(80);
    require(view.reflows() == count, "idle/cache causes reflow");
    ChatView stream, cold;
    std::string markdown =
        "# Heading\n\n**bold** and `code`\n```cpp\nauto value = \"yes\";\n```\nDone.\n";
    stream.begin(ChatKind::assistant);
    for (char c : markdown) {
      stream.stream(std::string_view{&c, 1});
      cold.clear();
      cold.append(ChatKind::assistant, markdown.substr(0, stream.size()));
      const auto &a = stream.rows(40), &b = cold.rows(40);
      require(plain(a) == plain(b), "stream/cold diverge");
      require(a.size() == b.size(), "styled row count differs");
      for (std::size_t row = 0; row < a.size(); ++row) {
        require(a[row].size() == b[row].size(), "styled span count differs");
        for (std::size_t span = 0; span < a[row].size(); ++span)
          require(a[row][span].text == b[row][span].text &&
                      a[row][span].ink == b[row][span].ink,
                  "stream/cold styles diverge");
      }
    }
    ChatGrid grid;
    grid.reset(20, 4);
    grid.line(0, {{"hello", Ink::assistant}});
    ChatPainter painter;
    auto initial = painter.prepare(grid, 3, 2);
    require(!initial.empty(), "first paint missing");
    painter.commit(grid);
    require(painter.prepare(grid, 3, 2).empty(), "unchanged frame emitted");
    grid.line(0, {{"j", Ink::assistant}});
    auto delta = painter.prepare(grid, 3, 2);
    require(delta.find("hello") == std::string::npos && painter.touched() == 1,
            "damage not bounded");
    painter.commit(grid);
    require(painter.prepare(grid, 3, 2).empty(), "committed frame repeats");
    grid.reset(20, 4);
    grid.line(0, {{"界x", Ink::normal}});
    require(grid.cells[0].width == 2 && grid.cells[1].width == 0 &&
                grid.cells[2].scalar == U'x',
            "CJK cells");
    grid.reset(20, 4);
    grid.line(0, {{"éx", Ink::normal}});
    require(grid.cells[0].length == 2 && grid.cells[1].scalar == U'x',
            "combining cluster");
    grid.reset(20, 4);
    grid.line(0, {{"👩‍💻x", Ink::normal}});
    require(grid.cells[0].width == 2 && grid.cells[2].scalar == U'x', "ZWJ cluster");
    grid.reset(20, 4);
    grid.line(0, {{"🇺🇸x", Ink::normal}});
    require(grid.cells[0].width == 2 && grid.cells[2].scalar == U'x', "flag cluster");
    painter.prepare(grid, 3, 2);
    painter.commit(grid);
    grid.reset(10, 3);
    require(!painter.prepare(grid, 2, 0).empty(), "resize did not repaint");
    ChatView control;
    control.append(ChatKind::assistant, "evil\x1b[2J");
    require(plain(control.rows(80)).find('\x1b') == std::string::npos,
            "untrusted control escaped renderer");
    ChatView large;
    large.append(ChatKind::tool, std::string(2 * 1024 * 1024, 'a'));
    require(large.size() <= 1024 * 1024, "display source unbounded");
    ChatView narrow;
    narrow.append(ChatKind::assistant, "aa👩‍💻x\n```cpp\n// 界\n```");
    const auto narrow_text = plain(narrow.rows(8));
    require(narrow_text.find("👩‍💻") != std::string::npos &&
                narrow_text.find("界") != std::string::npos,
            "wrapping splits Unicode clusters");
    grid.reset(20, 4);
    grid.line(0, {{"é", Ink::normal}});
    grid.line(0, {{"h", Ink::normal}});
    require(grid.cells[0].length == 0, "overwrite preserves old cluster tail");
    std::string cluster = "e";
    for (int i = 0; i < 3000; ++i)
      cluster += "́";
    grid.line(0, {{cluster, Ink::normal}});
    require(grid.cells[0].scalar == U'�', "oversized cluster has no bounded fallback");
    ChatView newlines;
    newlines.append(ChatKind::tool, std::string(1024 * 1024, '\n'));
    require(newlines.rows(8).size() < 4096, "derived rows unbounded");
    ChatView unfinished;
    unfinished.begin(ChatKind::tool);
    for (int i = 0; i < 256; ++i) {
      unfinished.stream(std::string(1024, 'a'));
      require(unfinished.rows(80).size() < 64, "unfinished line work unbounded");
    }
    int descriptors[2];
    require(::pipe(descriptors) == 0, "pipe setup");
    (void)::fcntl(descriptors[0], F_SETFL, O_NONBLOCK);
    std::string payload(256 * 1024, 'q'), received;
    {
      ChatOutput output;
      output.attach(descriptors[1]);
      output.start(payload);
      require(!output.flush() && output.pending(),
              "backpressure did not remain pending");
      for (unsigned round = 0;
           round < 1000 && (output.pending() || received.size() < payload.size());
           ++round) {
        char data[16384];
        const auto n = ::read(descriptors[0], data, sizeof(data));
        if (n > 0)
          received.append(data, static_cast<std::size_t>(n));
        if (output.pending())
          (void)output.flush();
      }
      require(!output.pending() && received == payload,
              "partial writes corrupted bytes");
    }
    require((::fcntl(descriptors[1], F_GETFL) & O_NONBLOCK) == 0,
            "output flags not restored");
    ::close(descriptors[0]);
    ::close(descriptors[1]);
    const auto start = std::chrono::steady_clock::now();
    std::size_t bytes = 0;
    ChatGrid motion;
    motion.reset(120, 40);
    ChatPainter paint;
    motion.line(0, {{"activity 0", Ink::assistant}});
    paint.prepare(motion, 38, 3);
    paint.commit(motion);
    for (int i = 0; i < 1000; ++i) {
      motion.line(0, {{"activity " + std::to_string(i % 10), Ink::assistant}});
      bytes += paint.prepare(motion, 38, 3).size();
      paint.commit(motion);
    }
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    require(bytes < 100000, "motion writes excessive bytes");
    std::cout << "1000 120x40 motion updates: " << us << "us, " << bytes
              << " bytes; sizeof(cell)=" << sizeof(ChatCell) << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
