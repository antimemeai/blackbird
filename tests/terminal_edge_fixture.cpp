#include "blackbird/chat_view.hpp"
#include "blackbird/terminal_custody.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

namespace {
using blackbird::ChatGrid;
using blackbird::ChatPainter;
using blackbird::ChatPaintPolicy;
using blackbird::ChatPresenter;
using blackbird::Ink;

struct Geometry {
  std::size_t columns = 101, rows = 30;
};
struct Options {
  ChatPaintPolicy policy = ChatPaintPolicy::addressed;
  std::string events_path, batch;
  std::optional<int> events_fd;
  Geometry geometry;
  std::size_t iterations = 200;
  bool legacy = false;
};
std::size_t positive(std::string_view text) {
  std::size_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !value)
    throw std::runtime_error("expected a positive integer");
  return value;
}
Options parse(int argc, char **argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument{argv[i]};
    if (argument == "--legacy" || argument == "--legacynegativecrop") {
      options.legacy = true;
      continue;
    }
    if (i + 1 >= argc)
      throw std::runtime_error("missing option value");
    const std::string_view value{argv[++i]};
    if (argument == "--strategy") {
      if (value == "addressed")
        options.policy = ChatPaintPolicy::addressed;
      else if (value == "erase")
        options.policy = ChatPaintPolicy::erase;
      else if (value == "corner")
        options.policy = ChatPaintPolicy::corner;
      else if (value == "full")
        options.policy = ChatPaintPolicy::full;
      else
        throw std::runtime_error("unknown strategy");
    } else if (argument == "--events") {
      options.events_path = value;
    } else if (argument == "--events-fd") {
      int fd = -1;
      const auto parsed =
          std::from_chars(value.data(), value.data() + value.size(), fd);
      if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
          fd < 0)
        throw std::runtime_error("invalid event descriptor");
      options.events_fd = fd;
    } else if (argument == "--batch") {
      if (value != "all" && value != "sparse" && value != "stream" &&
          value != "blank" && value != "style" && value != "unicode" &&
          value != "resize")
        throw std::runtime_error("unknown batch scenario");
      options.batch = value;
    } else if (argument == "--iterations") {
      options.iterations = positive(value);
    } else if (argument == "--cols") {
      options.geometry.columns = positive(value);
    } else if (argument == "--rows") {
      options.geometry.rows = positive(value);
    } else {
      throw std::runtime_error("unknown option");
    }
  }
  if (!options.events_path.empty() && options.events_fd)
    throw std::runtime_error("choose one event destination");
  if (options.batch.empty() && options.events_path.empty() && !options.events_fd)
    throw std::runtime_error("interactive mode requires an event destination");
  return options;
}

class Events {
public:
  explicit Events(const Options &options) {
    if (!options.events_path.empty()) {
      fd_ = ::open(options.events_path.c_str(),
                   O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
      if (fd_ < 0)
        throw std::runtime_error("cannot open event destination");
      owned_ = true;
    } else {
      fd_ = options.events_fd.value_or(STDOUT_FILENO);
    }
  }
  ~Events() {
    if (owned_)
      (void)::close(fd_);
  }
  Events(const Events &) = delete;
  Events &operator=(const Events &) = delete;
  template <typename... Values> void emit(std::string_view kind, Values... values) {
    std::string record{kind};
    ((record += " " + std::to_string(values)), ...);
    record += '\n';
    write(record);
  }
  void batch(std::string_view scenario, std::size_t iterations,
             const std::array<std::uint64_t, 5> &values) {
    std::string record = "BATCH " + std::string{scenario} + " " +
                         std::to_string(iterations) + " " + std::to_string(iterations);
    for (const auto value : values)
      record += " " + std::to_string(value);
    record += '\n';
    write(record);
  }

private:
  void write(std::string_view record) const {
    while (!record.empty()) {
      const auto count = ::write(fd_, record.data(), record.size());
      if (count > 0)
        record.remove_prefix(static_cast<std::size_t>(count));
      else if (count < 0 && errno == EINTR)
        continue;
      else
        throw std::runtime_error("event write failed");
    }
  }
  int fd_ = -1;
  bool owned_ = false;
};

std::uint64_t timestamp(clockid_t clock) {
  timespec now{};
  if (::clock_gettime(clock, &now) != 0 || now.tv_sec < 0 || now.tv_nsec < 0)
    throw std::runtime_error("clock read failed");
  return static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL +
         static_cast<std::uint64_t>(now.tv_nsec);
}
Geometry physical_geometry(bool legacy) {
  winsize size{};
  if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || !size.ws_col || !size.ws_row)
    throw std::runtime_error("cannot read positive terminal geometry");
  Geometry result{size.ws_col, size.ws_row};
  if (legacy && result.columns > 1)
    --result.columns;
  return result;
}
void scene(ChatGrid &grid, char kind, Geometry geometry, std::size_t advance = 0) {
  grid.reset(geometry.columns, geometry.rows);
  if (kind == 's') {
    for (std::size_t row = 0; row < grid.height; ++row) {
      const auto letter = static_cast<char>('A' + ((row + advance) % 26));
      std::string text(grid.width, letter);
      text.back() = static_cast<char>('a' + ((row + advance) % 26));
      grid.line(row, {{std::move(text), Ink::normal}});
    }
  } else if (kind == 'b' || kind == 'c') {
    const Ink ink = kind == 'c' ? Ink::code : Ink::normal;
    for (std::size_t row = 0; row < grid.height; ++row) {
      std::string text(grid.width, ' ');
      constexpr std::string_view suffix = "abc";
      text[0] = static_cast<char>('0' + row % 10);
      for (std::size_t column = 1; column < std::min<std::size_t>(grid.width, 4);
           ++column)
        text[column] = suffix[column - 1];
      grid.line(row, {{std::move(text), ink}});
    }
  } else if (kind == 'u') {
    grid.line(0, {{"U", Ink::normal}});
    if (grid.width >= 2)
      grid.line(0, {{"界", Ink::normal}}, grid.width - 2);
    grid.line(1, {{"e\xcc\x81", Ink::normal}}, grid.width - 1);
    grid.line(2, {{"界", Ink::normal}}, grid.width - 1);
    if (grid.width >= 2)
      grid.line(3, {{"界", Ink::code}}, grid.width - 2);
  } else if (kind == 'o') {
    for (std::size_t row = 0; row < grid.height; ++row) {
      grid.line(row, {{"O", Ink::normal}});
      if (grid.width >= 2)
        grid.line(row, {{"n", Ink::normal}}, grid.width - 2);
      grid.line(row, {{"z", Ink::normal}}, grid.width - 1);
    }
  } else {
    throw std::runtime_error("unknown scene");
  }
}
void batch(const Options &options, Events &events, std::string_view scenario) {
  ChatPainter painter;
  ChatGrid grid;
  grid.reset(options.geometry.columns, options.geometry.rows);
  std::uint64_t bytes = 0;
  const auto wall_before = timestamp(CLOCK_MONOTONIC);
  const auto cpu_before = timestamp(CLOCK_THREAD_CPUTIME_ID);
  for (std::size_t frame = 0; frame < options.iterations; ++frame) {
    bool recover = false;
    if (scenario == "sparse") {
      const auto cell = frame % grid.cells.size();
      const auto letter = static_cast<char>('a' + ((frame / grid.cells.size()) % 26));
      grid.line(cell / grid.width, {{std::string(1, letter), Ink::normal}},
                cell % grid.width);
    } else if (scenario == "stream") {
      scene(grid, 's', options.geometry, frame);
    } else if (scenario == "blank") {
      scene(grid, frame % 2 ? 'b' : 's', options.geometry);
    } else if (scenario == "style") {
      scene(grid, frame % 2 ? 'b' : 'c', options.geometry);
    } else if (scenario == "unicode") {
      scene(grid, frame % 2 ? 'o' : 'u', options.geometry);
    } else if (scenario == "resize") {
      auto geometry = options.geometry;
      if (frame % 2) {
        geometry.columns = std::min<std::size_t>(geometry.columns, 32);
        geometry.rows = std::min<std::size_t>(geometry.rows, 12);
      }
      scene(grid, 's', geometry);
      recover = true;
    }
    const auto &packet = painter.prepare(grid, 0, 0, recover, options.policy);
    if (packet.size() > std::numeric_limits<std::uint64_t>::max() - bytes)
      throw std::runtime_error("packet byte sum overflow");
    bytes += packet.size();
    painter.commit(grid);
  }
  const auto cpu_after = timestamp(CLOCK_THREAD_CPUTIME_ID);
  const auto wall_after = timestamp(CLOCK_MONOTONIC);
  events.batch(scenario, options.iterations,
               {wall_before, wall_after, cpu_before, cpu_after, bytes});
}

struct Request {
  std::uint64_t sequence;
  Geometry geometry;
  char kind;
  bool recover;
};
void interactive(const Options &options, Events &events) {
  blackbird::TerminalMode mode{nullptr};
  ChatPresenter presenter;
  presenter.attach(STDOUT_FILENO);
  std::uint64_t sequence = 0, active_sequence = 0;
  char last_scene = 's';
  std::optional<Request> latest{
      Request{0, physical_geometry(options.legacy), 's', false}};
  bool stopping = false;
  const auto progress = [&] {
    if (presenter.flush()) {
      const auto &geometry = presenter.geometry();
      events.emit("READY", active_sequence, geometry.columns, geometry.rows);
    }
  };
  for (;;) {
    if (!presenter.pending() && latest) {
      const Request request = *latest;
      latest.reset();
      scene(presenter.grid(), request.kind, request.geometry);
      const auto wall_before = timestamp(CLOCK_MONOTONIC);
      const auto cpu_before = timestamp(CLOCK_THREAD_CPUTIME_ID);
      auto &packet = presenter.prepare(0, 0, request.recover, options.policy);
      if (packet.empty())
        packet = "\x1b[?2026h\x1b[1;1H\x1b[?2026l";
      const auto cpu_after = timestamp(CLOCK_THREAD_CPUTIME_ID);
      const auto wall_after = timestamp(CLOCK_MONOTONIC);
      events.emit("PREP", request.sequence, wall_before, wall_after, cpu_before,
                  cpu_after, packet.size());
      active_sequence = request.sequence;
      presenter.start();
      progress();
      if (presenter.pending())
        events.emit("PENDING", active_sequence);
    }
    if (stopping && !presenter.pending() && !latest)
      break;
    std::array<pollfd, 2> descriptors{
        {{stopping ? -1 : STDIN_FILENO, POLLIN, 0},
         {presenter.pending() ? STDOUT_FILENO : -1, POLLOUT, 0}}};
    const int ready = ::poll(descriptors.data(), descriptors.size(), -1);
    if (ready < 0) {
      if (errno == EINTR)
        continue;
      throw std::runtime_error("fixture poll failed");
    }
    if (descriptors[0].revents & (POLLIN | POLLHUP)) {
      unsigned char byte = 0;
      const auto count = ::read(STDIN_FILENO, &byte, 1);
      if (count == 1) {
        if (sequence == std::numeric_limits<std::uint64_t>::max())
          throw std::runtime_error("input sequence exhausted");
        ++sequence;
        events.emit("INPUT", sequence, static_cast<unsigned>(byte));
        if (byte == 'q') {
          stopping = true;
        } else if (byte == 's' || byte == 'b' || byte == 'c' || byte == 'u' ||
                   byte == 'o' || byte == 'r') {
          if (byte != 'r')
            last_scene = static_cast<char>(byte);
          latest = Request{sequence, physical_geometry(options.legacy), last_scene,
                           byte == 'r'};
        }
      } else if (count == 0) {
        stopping = true;
      } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
        throw std::runtime_error("fixture input failed");
      }
    }
    if (descriptors[0].revents & (POLLERR | POLLNVAL))
      throw std::runtime_error("fixture input descriptor failed");
    if (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL))
      throw std::runtime_error("fixture output descriptor failed");
    if (presenter.pending() && (descriptors[1].revents & POLLOUT))
      progress();
  }
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (!std::setlocale(LC_CTYPE, ""))
      throw std::runtime_error("cannot set locale");
    const auto options = parse(argc, argv);
    Events events{options};
    if (options.batch.empty()) {
      interactive(options, events);
    } else if (options.batch == "all") {
      for (const auto scenario :
           {"sparse", "stream", "blank", "style", "unicode", "resize"})
        batch(options, events, scenario);
    } else {
      batch(options, events, options.batch);
    }
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "terminal_edge_fixture: %s\n", error.what());
    return 1;
  }
}
