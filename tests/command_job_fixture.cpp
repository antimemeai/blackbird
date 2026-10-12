#include <array>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
namespace {
using Path = std::filesystem::path;
void write_all(std::string_view bytes) {
  while (!bytes.empty()) {
    const auto count = ::write(STDOUT_FILENO, bytes.data(), bytes.size());
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      throw std::runtime_error("fixture output refused");
    bytes.remove_prefix(static_cast<std::size_t>(count));
  }
}
void save(const Path &path, std::string_view bytes) {
  auto pending = path;
  pending += ".pending." + std::to_string(::getpid());
  std::ofstream stream{pending, std::ios::binary};
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  if (!stream)
    throw std::runtime_error("fixture marker refused");
  std::filesystem::rename(pending, path);
}
void gate(const Path &path) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
  while (!std::filesystem::exists(path)) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("fixture gate expired");
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
}
std::string line() {
  std::string result;
  while (result.size() < 1024) {
    char byte{};
    const auto count = ::read(STDIN_FILENO, &byte, 1);
    if (count < 0 && errno == EINTR)
      continue;
    if (count != 1)
      throw std::runtime_error("fixture input refused");
    if (byte == '\n')
      return result;
    result.push_back(byte);
  }
  throw std::runtime_error("fixture input too long");
}
std::string identity() {
  return "pid=" + std::to_string(::getpid()) + "\npgid=" + std::to_string(::getpgrp()) +
         "\nsid=" + std::to_string(::getsid(0)) +
         "\nforeground=" + std::to_string(::tcgetpgrp(STDIN_FILENO)) +
         "\ntty=" + std::to_string(::isatty(STDIN_FILENO)) + "\n";
}
void record_winsize(const Path &path) {
  struct winsize size{};
  if (::ioctl(STDIN_FILENO, TIOCGWINSZ, &size) != 0)
    throw std::runtime_error("fixture private tty size unavailable");
  save(path, "rows=" + std::to_string(size.ws_row) +
                 "\ncols=" + std::to_string(size.ws_col) + "\n");
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      return 90;
    const std::string mode{argv[1]};
    const Path root{argv[2]};
    std::ofstream counter{root / "launches", std::ios::binary | std::ios::app};
    counter.put('L');
    counter.close();
    if (!counter)
      return 91;
    save(root / "identity", identity());
    if (mode == "identity") {
      write_all("PID:" + std::to_string(::getpid()) + "\n");
      write_all(std::string_view{"\xe2", 1});
      save(root / "ready", "ready");
      gate(root / "one");
      write_all(std::string_view{"\x82", 1});
      save(root / "middle", "middle");
      gate(root / "two");
      write_all(std::string_view{"\xac\0\nZ", 4});
      return 7;
    }
    if (mode == "eof-first") {
      (void)::close(STDOUT_FILENO);
      (void)::close(STDERR_FILENO);
      save(root / "ready", "output closed");
      gate(root / "finish");
      return 31;
    }
    if (mode == "exit-first") {
      const auto child = ::fork();
      if (child < 0)
        return 92;
      if (child == 0) {
        save(root / "descendant-ready", "writer held");
        gate(root / "tail");
        write_all(std::string_view{"TAIL\0", 5});
        ::_exit(0);
      }
      save(root / "ready", "leader exiting");
      return 23;
    }
    if (mode == "pty") {
      record_winsize(root / "initial-size");
      write_all("READY\n");
      save(root / "ready", "private tty ready");
      const auto first = line();
      write_all("GOT:" + first + "\n");
      save(root / "first-read", first);
      gate(root / "check-size");
      const auto size_deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds{4};
      while (true) {
        struct winsize size{};
        if (::ioctl(STDIN_FILENO, TIOCGWINSZ, &size) != 0)
          throw std::runtime_error("fixture resized tty unavailable");
        if (size.ws_row == 37 && size.ws_col == 113) {
          record_winsize(root / "resized");
          break;
        }
        if (std::chrono::steady_clock::now() >= size_deadline)
          throw std::runtime_error("fixture resize never reached tty");
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
      }
      const auto second = line();
      save(root / "second-read", second);
      write_all("SECOND:" + second + "\n");
      return first == "hello" && second == "end" ? 0 : 83;
    }
    if (mode == "raw-input" || mode == "gated-raw-input") {
      struct termios settings{};
      if (::tcgetattr(STDIN_FILENO, &settings) != 0)
        throw std::runtime_error("fixture raw tty unavailable");
      ::cfmakeraw(&settings);
      if (::tcsetattr(STDIN_FILENO, TCSANOW, &settings) != 0)
        throw std::runtime_error("fixture raw tty configuration refused");
      write_all("READY\n");
      save(root / "ready", "raw tty ready");
      if (mode == "gated-raw-input") {
        gate(root / "read-input");
        record_winsize(root / "resized");
      }
      std::string received(65536, '\0');
      std::size_t offset = 0;
      while (offset < received.size()) {
        const auto count =
            ::read(STDIN_FILENO, received.data() + offset, received.size() - offset);
        if (count < 0 && errno == EINTR)
          continue;
        if (count <= 0)
          throw std::runtime_error("fixture raw input refused");
        offset += static_cast<std::size_t>(count);
      }
      save(root / "received", received);
      save(root / "received-identity", identity());
      write_all("DONE\n");
      return 0;
    }
    if (mode == "binary") {
      write_all(std::string_view{"A\0B\xff\xe2\x82\xac\n", 8});
      return 7;
    }
    if (mode == "limit-cross") {
      std::array<char, 8192> block{};
      block.fill('Q');
      for (std::size_t n = 0; n < 2048; ++n)
        write_all(std::string_view{block.data(), block.size()});
      write_all(std::string_view{"X\0Y\xff", 4});
      return 0;
    }
    if (mode == "exit143")
      return 143;
    if (mode == "gate" || mode == "deadline") {
      write_all("READY\n");
      save(root / "ready", "running");
      gate(root / "finish");
      write_all("DONE\n");
      return 0;
    }
    if (mode == "flood" || mode == "gated-flood") {
      save(root / "ready", "flood starting");
      if (mode == "gated-flood")
        gate(root / "start");
      std::array<char, 8192> block{};
      block.fill('F');
      for (std::size_t n = 0; n < 1024; ++n) {
        write_all(std::string_view{block.data(), block.size()});
        save(root / "produced", std::to_string((n + 1) * block.size()));
      }
      save(root / "finished-writing", "all written");
      gate(root / "finish");
      return 0;
    }
    if (mode == "closed-stdio") {
      char byte{};
      const auto count = ::read(STDIN_FILENO, &byte, 0);
      save(root / "stdin-valid", count == 0 ? "yes" : "no");
      write_all("STDIO\n");
      return 0;
    }
    return 93;
  } catch (const std::exception &error) {
    std::cerr << "command fixture: " << error.what() << '\n';
    return 94;
  }
}
