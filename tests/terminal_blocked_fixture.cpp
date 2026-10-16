#include "blackbird/terminal.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace {
int observations = -1;
std::atomic_bool reported_blocked = false;
using NativeWrite = ssize_t (*)(int, const void *, std::size_t);
NativeWrite native_write() {
  static const auto function =
      reinterpret_cast<NativeWrite>(::dlsym(RTLD_NEXT, "write"));
  if (!function)
    std::abort();
  return function;
}
void observe(char event) {
  if (native_write()(observations, &event, 1) != 1)
    throw std::runtime_error("fixture observation pipe failed");
}
char read_control(int descriptor) {
  char value{};
  ssize_t count;
  do {
    count = ::read(descriptor, &value, 1);
  } while (count < 0 && errno == EINTR);
  if (count != 1)
    throw std::runtime_error("fixture control pipe failed");
  return value;
}
} // namespace

// This executable observes actual native backpressure without changing product
// transport or making stdout progress to acknowledge cancellation.
extern "C" ssize_t write(int descriptor, const void *bytes, std::size_t size) {
  const auto count = native_write()(descriptor, bytes, size);
  const int error = errno;
  if (descriptor == STDOUT_FILENO && count < 0 &&
      (error == EAGAIN || error == EWOULDBLOCK) && !reported_blocked.exchange(true)) {
    constexpr char event = 'B';
    if (native_write()(observations, &event, 1) != 1)
      std::abort();
  }
  errno = error;
  return count;
}

int main(int argc, char **argv) {
  try {
    if (argc != 4 && argc != 5)
      throw std::runtime_error("fixture requires observation/control/state paths");
    const bool pane = argc == 5 && std::string_view{argv[4]} == "pane";
    const bool inspecting = argc == 5 && std::string_view{argv[4]} == "inspector";
    const bool centered = argc == 5 && std::string_view{argv[4]} == "center";
    std::shared_ptr<blackbird::CommandJobs> commands;
    if (inspecting) {
      commands = std::make_shared<blackbird::CommandJobs>();
      commands->start(
          std::string(32, '8'),
          blackbird::Value::object(
              {{"argv", blackbird::Value{blackbird::Value::Array{
                            blackbird::Value{"/bin/sh"}, blackbird::Value{"-c"},
                            blackbird::Value{"printf 'INSPECT_OUTPUT\\n'"}}}}}));
      bool settled = false;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
      while (!settled) {
        for (const auto &event : commands->drain()) {
          if (event.bytes)
            commands->retained(event.job_id, event.offset + event.bytes->size());
          else {
            commands->settled(event.job_id, event.result);
            settled = true;
          }
        }
        if (std::chrono::steady_clock::now() >= deadline)
          throw std::runtime_error("inspector fixture command did not settle");
        std::this_thread::yield();
      }
    }
    observations = std::stoi(argv[1]);
    const int controls = std::stoi(argv[2]);
    std::atomic_bool cancelled = false, finished = false;
    blackbird::TerminalUI ui{"Blocked output fixture"};
    std::optional<blackbird::CommandCenterContext> center_context;
    if (centered) {
      blackbird::IdentityBytes environment{}, conversation{};
      environment[0] = std::byte{1};
      conversation[0] = std::byte{2};
      center_context.emplace(blackbird::CommandCenterContext{
          {blackbird::unwrap(blackbird::EnvironmentId::from_bytes(environment)),
           blackbird::unwrap(blackbird::ConversationId::from_bytes(conversation))},
          {}});
      blackbird::TaskState tasks;
      auto prepared = tasks.prepare(blackbird::Value::object(
          {{"op_id", blackbird::Value{"blocked-fixture"}},
           {"base", blackbird::Value{blackbird::Number{0}}},
           {"ops", blackbird::Value{blackbird::Value::Array{blackbird::Value::object(
                       {{"op", blackbird::Value{"add"}},
                        {"title", blackbird::Value{"Inspect blocked work"}},
                        {"status", blackbird::Value{"blocked"}},
                        {"blocker", blackbird::Value{"Await exact source"}}})}}}}));
      tasks.commit(std::move(prepared));
      ui.tasks(tasks.snapshot(), {2, {}, {}, tasks.revision()});
    }
    ui.run(
        [&](std::string_view source) {
          if (source != "fixture")
            throw std::runtime_error("unexpected fixture invocation");
          std::string transcript;
          for (unsigned row = 0; row < 80; ++row) {
            const auto number = std::to_string(row);
            transcript += "ROW_" + std::string(3 - number.size(), '0') + number;
            if (pane)
              for (unsigned part = 0; part < 20; ++part)
                transcript += " \u200d \u200d ";
            transcript += "\n";
          }
          ui.text(transcript);
          observe('S');
          if (read_control(controls) != 'P')
            throw std::runtime_error("fixture expected publication control");
          reported_blocked.store(false);
          ui.title("Blocked presentation");
          const auto deadline =
              std::chrono::steady_clock::now() + std::chrono::seconds{5};
          while (!cancelled.load()) {
            if (std::chrono::steady_clock::now() >= deadline)
              throw std::runtime_error("native cancellation was not observed");
            pollfd control{controls, POLLIN, 0};
            const auto ready = ::poll(&control, 1, 10);
            if ((ready < 0 && errno != EINTR) || (ready > 0 && control.revents != 0))
              throw std::runtime_error("fixture control closed before cancellation");
          }
          observe('C');
          if (read_control(controls) != 'F')
            throw std::runtime_error("fixture expected finish control");
          finished.store(true);
        },
        cancelled, [&] { return finished.load(); }, {}, argv[3], {}, commands, -1, {},
        nullptr, center_context);
    const auto state = blackbird::load_terminal_state(argv[3]);
    if (state.draft != "typed 界 tail" || state.cursor != state.draft.size())
      throw std::runtime_error("blocked native draft/cursor was not retained");
    observe('D');
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
