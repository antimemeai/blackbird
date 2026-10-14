#include "blackbird/terminal_custody.hpp"
#include "blackbird/chat_view.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/terminal.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace blackbird {
namespace {
using detail::raw_native_field;
constexpr std::uint64_t linked = raw_envelope_size + 24;
constexpr std::uint64_t integer_return =
    linked + sizeof(int) + sizeof(bool) + sizeof(int);
constexpr std::uint64_t count_return =
    linked + sizeof(ssize_t) + sizeof(bool) + sizeof(int);
bool selected_context(const RawAdapterContext *context) {
  if (!context || !context->parent || !raw_enabled())
    return false;
  if (!raw_reference_valid(*context->parent)) {
    const int saved = errno;
    const Error error{ErrorCode::invalid_identity};
    raw_capture_failed(error);
    raw_fence(error);
    errno = saved;
    throw error;
  }
  return true;
}
#if defined(__linux__)
constexpr std::uint64_t line_bytes = sizeof(cc_t);
#else
constexpr std::uint64_t line_bytes = 0;
#endif
constexpr std::uint64_t termios_bytes = 4 * sizeof(tcflag_t) + sizeof(bool) +
                                        line_bytes + sizeof(termios{}.c_cc) +
                                        2 * sizeof(speed_t);
constexpr std::uint64_t set_bytes =
    linked + 2 * sizeof(int) + termios_bytes + integer_return;
constexpr std::uint64_t state_bytes = linked + sizeof(std::uint8_t) + sizeof(bool);
constexpr std::string_view enter_screen = "\x18\x1b[?1049h\x1b[?2004h";
constexpr std::string_view leave_screen = "\x18\x1b_Ga=d,d=I,i=72171,q=2;\x1b\\\x1b[?"
                                          "2026l\x1b[0m\x1b[?2004l\x1b[?1049l\x1b[?25h";
// Native field suppliers only. RawEmission owns all request/end/credit policy.
RawEventRef publish(std::uint32_t site, RawEventRef parent,
                    std::span<const ByteView> fields, RawControlToken *bank) noexcept {
  if (!bank)
    return {};
  auto result = raw_publish_linked(site, parent, fields, RawPartition::control, *bank);
  return result.has_value() ? result.value() : RawEventRef{};
}
template <class T>
void returned(std::uint32_t site, RawEventRef request, T result, int error,
              RawControlToken *bank) noexcept {
  const bool failed = result < 0;
  if (failed)
    (void)publish(site, request,
                  std::array{raw_native_field(result), raw_native_field(failed),
                             raw_native_field(error)},
                  bank);
  else
    (void)publish(site, request,
                  std::array{raw_native_field(result), raw_native_field(failed)}, bank);
  errno = error;
}
auto termios_fields(const termios &mode) noexcept {
#if defined(__linux__)
  static constexpr bool has_line = true;
  return std::array{raw_native_field(mode.c_iflag), raw_native_field(mode.c_oflag),
                    raw_native_field(mode.c_cflag), raw_native_field(mode.c_lflag),
                    raw_native_field(has_line),     raw_native_field(mode.c_line),
                    raw_native_field(mode.c_cc),    raw_native_field(mode.c_ispeed),
                    raw_native_field(mode.c_ospeed)};
#else
  static constexpr bool has_line = false;
  return std::array{raw_native_field(mode.c_iflag),  raw_native_field(mode.c_oflag),
                    raw_native_field(mode.c_cflag),  raw_native_field(mode.c_lflag),
                    raw_native_field(has_line),      raw_native_field(mode.c_cc),
                    raw_native_field(mode.c_ispeed), raw_native_field(mode.c_ospeed)};
#endif
}
int set_mode(RawEventRef parent, const termios &mode, RawControlToken *bank) noexcept {
  const int fd = STDIN_FILENO, action = TCSANOW;
  const auto native = termios_fields(mode);
  std::array<ByteView, native.size() + 2> fields{};
  fields[0] = raw_native_field(fd);
  fields[1] = raw_native_field(action);
  std::copy(native.begin(), native.end(), fields.begin() + 2);
  const auto request = publish(4104, parent, fields, bank);
  const int result = ::tcsetattr(fd, action, &mode), error = errno;
  returned(4105, request, result, error, bank);
  return result;
}
void custody(RawEventRef parent, std::uint8_t method, bool active,
             RawControlToken *bank) noexcept {
  (void)publish(4106, parent,
                std::array{raw_native_field(method), raw_native_field(active)}, bank);
}
void control_packet(std::string_view bytes, bool selected,
                    const RawAdapterContext *context,
                    RawCapacityClaim *preheld_data = nullptr, bool cleanup = false) {
  auto link_credit = raw_ui_control(selected, linked + 24);
  RawUiFrame source{selected, bytes.size(), preheld_data};
  source.capture(bytes);
  const auto frame = source.reference();
  (void)publish(4107, context ? context->parent.value_or(RawEventRef{}) : RawEventRef{},
                std::array{ByteView{frame.lane}, raw_native_field(frame.ordinal)},
                link_credit ? &*link_credit : nullptr);
  ChatOutput writer;
  writer.raw_capture(selected);
  writer.attach(STDOUT_FILENO);
  writer.start(bytes, source.handoff());
  if (cleanup) {
    (void)writer.flush(); // One existing batch: at most eight actual writes.
    return;               // Actual remainder retirement and flags restoration are paid.
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
  while (!writer.flush()) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("Terminal control handoff timed out");
    pollfd fd{STDOUT_FILENO, POLLOUT, 0};
    (void)terminal_native_poll(context, std::span{&fd, std::size_t{1}}, 10, true);
  }
}
constexpr std::uint64_t pipe_request = linked + sizeof(std::uint8_t);
constexpr std::uint64_t pipe_return = integer_return + 2 * sizeof(int);
constexpr std::uint64_t fcntl_bytes =
    linked + sizeof(std::uint8_t) + 3 * sizeof(int) + integer_return;
constexpr std::uint64_t close_bytes = linked + sizeof(std::uint8_t) + sizeof(int) +
                                      integer_return + linked + sizeof(std::uint8_t) +
                                      sizeof(int) + sizeof(bool);
int native_fcntl(RawEventRef parent, int fd, int command, int argument,
                 RawControlToken *bank) noexcept {
  const std::uint8_t method = 2;
  const auto request =
      publish(4110, parent,
              std::array{raw_native_field(method), raw_native_field(fd),
                         raw_native_field(command), raw_native_field(argument)},
              bank);
  const int result = ::fcntl(fd, command, argument), error = errno;
  returned(4111, request, result, error, bank);
  return result;
}
void descriptor(RawEventRef parent, std::uint8_t method, int fd, bool owned,
                RawControlToken *bank) noexcept {
  (void)publish(4112, parent,
                std::array{raw_native_field(method), raw_native_field(fd),
                           raw_native_field(owned)},
                bank);
}
void native_close(RawEventRef parent, int &fd, RawControlToken *bank) noexcept {
  if (fd < 0)
    return;
  const std::uint8_t method = 3;
  const auto request = publish(
      4110, parent, std::array{raw_native_field(method), raw_native_field(fd)}, bank);
  const int result = ::close(fd), error = errno;
  returned(4111, request, result, error, bank);
  descriptor(parent, method, fd, false, bank);
  fd = -1;
}
std::atomic<TerminalResize *> resize_target{nullptr};
static_assert(decltype(resize_target)::is_always_lock_free);
} // namespace
TerminalMode::TerminalMode(const RawAdapterContext *context)
    : owner_(context && context->parent && raw_enabled()),
      context_(context ? *context : RawAdapterContext{}) {
  const std::uint8_t method = 1;
  RawSessionCall call{4100, 4101, context, std::array{raw_native_field(method)}};
  bool rollback_attempted = false;
  const auto rollback = [&] {
    if (!active_ || rollback_attempted)
      return;
    rollback_attempted = true;
    const int saved = errno;
    if (set_mode(call.reference(), before_, cleanup_ ? &*cleanup_ : nullptr) == 0)
      active_ = false;
    custody(call.reference(), method, active_, cleanup_ ? &*cleanup_ : nullptr);
    errno = saved;
  };
  try {
    call.run([&] {
      const int fd = STDIN_FILENO;
      auto read_credit =
          call.control(linked + sizeof(fd) + integer_return + termios_bytes);
      // Cleanup exists before the first mutation. All fields have finite native bounds.
      cleanup_ = call.control(linked + 1 + 52 + set_bytes + state_bytes + 247 + 378 +
                              8 * 113 + linked + 24);
      exit_data_ = call.bytes_credit(leave_screen.size());
      const auto request =
          publish(4102, call.reference(), std::array{raw_native_field(fd)},
                  read_credit ? &*read_credit : nullptr);
      const int result = ::tcgetattr(fd, &before_), error = errno;
      if (result < 0)
        returned(4103, request, result, error, read_credit ? &*read_credit : nullptr);
      else {
        const bool failed = false;
        const auto native = termios_fields(before_);
        std::array<ByteView, native.size() + 2> fields{};
        fields[0] = raw_native_field(result);
        fields[1] = raw_native_field(failed);
        std::copy(native.begin(), native.end(), fields.begin() + 2);
        (void)publish(4103, request, fields, read_credit ? &*read_credit : nullptr);
        errno = error;
      }
      if (result != 0)
        throw std::runtime_error("Cannot read terminal mode");
      call.require_capture();
      context_ = call.context();
      try {
        resume();
      } catch (...) {
        rollback();
        throw;
      }
    });
  } catch (...) {
    rollback();
    throw;
  }
}
void TerminalMode::resume() {
  owner_.require_thread();
  RawUiOwnerBinding binding{owner_};
  const std::uint8_t method = 2;
  RawSessionCall call{4100, 4101, &context_, std::array{raw_native_field(method)}};
  call.run([&] {
    auto credit = call.control(set_bytes + state_bytes);
    auto raw = before_;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | IEXTEN | ISIG));
    raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
    raw.c_cc[VMIN] = raw.c_cc[VTIME] = 0;
    if (set_mode(call.reference(), raw, credit ? &*credit : nullptr) != 0)
      throw std::runtime_error("Cannot set terminal mode");
    active_ = true;
    custody(call.reference(), method, active_, credit ? &*credit : nullptr);
    call.require_capture();
    const auto source = call.context();
    control_packet(enter_screen, call.enabled(), &source);
  });
}
void TerminalMode::suspend() {
  owner_.require_thread();
  RawUiOwnerBinding binding{owner_};
  const std::uint8_t method = 3;
  RawSessionCall call{4100, 4101, &context_, std::array{raw_native_field(method)}};
  call.run([&] {
    if (!active_)
      return;
    auto credit = call.control(set_bytes + state_bytes);
    const auto source = call.context();
    control_packet(leave_screen, call.enabled(), &source);
    if (set_mode(call.reference(), before_, credit ? &*credit : nullptr) != 0)
      throw std::runtime_error("Cannot restore terminal for editor");
    active_ = false;
    custody(call.reference(), method, active_, credit ? &*credit : nullptr);
  });
}
TerminalMode::~TerminalMode() {
  owner_.require_cleanup_thread();
  RawUiOwnerBinding binding{owner_};
  const std::uint8_t method = 4;
  const auto parent = context_.parent.value_or(RawEventRef{});
  RawEmission terminal;
  const auto request = publish(4100, parent, std::array{raw_native_field(method)},
                               cleanup_ ? &*cleanup_ : nullptr);
  if (cleanup_)
    (void)terminal.adopt(request, 4101, std::move(*cleanup_));
  cleanup_.reset();
  RawOperationBranch branch = RawOperationBranch::returned;
  std::optional<Error> failure;
  if (!active_) {
    custody(request, method, active_, terminal.bank());
    terminal.finish(RawOperationBranch::returned, 0);
    return;
  }
  try {
    RawUiBurst burst{terminal.bank()};
    control_packet(leave_screen, terminal.enabled(), &context_,
                   exit_data_ ? &*exit_data_ : nullptr, true);
  } catch (const Error &error) {
    branch = RawOperationBranch::error;
    failure = error;
  } catch (const std::bad_alloc &) {
    branch = RawOperationBranch::allocation;
  } catch (const std::length_error &) {
    branch = RawOperationBranch::length;
  } catch (const std::exception &) {
    branch = RawOperationBranch::standard;
  } catch (...) {
    branch = RawOperationBranch::nonstandard;
  }
  // Output failure cannot prevent restoration. No admission or retry here.
  const int result = set_mode(request, before_, terminal.bank());
  if (result == 0)
    active_ = false;
  else if (!failure) {
    branch = RawOperationBranch::error;
    failure = Error{ErrorCode::io, errno};
  }
  custody(request, method, active_, terminal.bank());
  terminal.finish(branch, 0, failure ? &*failure : nullptr);
}
TerminalNotifications::TerminalNotifications(const RawAdapterContext *context)
    : owner_(context && context->parent && raw_enabled()) {
  const std::uint8_t method = 6;
  RawSessionCall call{4100, 4101, context, std::array{raw_native_field(method)}};
  try {
    call.run([&] {
      cleanup_ = call.control(2 * close_bytes);
      auto credit =
          call.control(pipe_request + pipe_return + 6 * fcntl_bytes + 2 * close_bytes +
                       4 * (linked + 1 + sizeof(int) + sizeof(bool)));
      parent_ = call.reference();
      try {
        const std::lock_guard lock{detail::process_spawn_mutex};
        const std::uint8_t operation = 1;
        const auto request =
            publish(4110, parent_, std::array{raw_native_field(operation)},
                    credit ? &*credit : nullptr);
        const int result = ::pipe(fds_), error = errno;
        const bool failed = result < 0;
        if (failed)
          returned(4111, request, result, error, credit ? &*credit : nullptr);
        else
          (void)publish(4111, request,
                        std::array{raw_native_field(result), raw_native_field(failed),
                                   raw_native_field(fds_)},
                        credit ? &*credit : nullptr);
        if (failed)
          throw Error{ErrorCode::io, error};
        for (auto &fd : fds_) {
          descriptor(parent_, 1, fd, true, credit ? &*credit : nullptr);
          if (fd < 3) {
            const int replacement = native_fcntl(parent_, fd, F_DUPFD_CLOEXEC, 3,
                                                 credit ? &*credit : nullptr);
            if (replacement < 0)
              throw Error{ErrorCode::io, errno};
            native_close(parent_, fd, credit ? &*credit : nullptr);
            fd = replacement;
            descriptor(parent_, 2, fd, true, credit ? &*credit : nullptr);
          }
          if (native_fcntl(parent_, fd, F_SETFD, FD_CLOEXEC,
                           credit ? &*credit : nullptr) < 0 ||
              native_fcntl(parent_, fd, F_SETFL, O_NONBLOCK,
                           credit ? &*credit : nullptr) < 0)
            throw Error{ErrorCode::io, errno};
        }
      } catch (...) {
        for (auto &fd : fds_)
          native_close(parent_, fd, cleanup_ ? &*cleanup_ : nullptr);
        throw;
      }
    });
  } catch (...) {
    for (auto &fd : fds_)
      native_close(parent_, fd, cleanup_ ? &*cleanup_ : nullptr);
    throw;
  }
}
TerminalNotifications::~TerminalNotifications() {
  owner_.require_cleanup_thread();
  RawUiOwnerBinding binding{owner_};
  for (auto &fd : fds_)
    native_close(parent_, fd, cleanup_ ? &*cleanup_ : nullptr);
}
TerminalResize::TerminalResize(const RawAdapterContext *context)
    : owner_(context && context->parent && raw_enabled()) {
  RawThreadBinding binding{owner_.selected() ? owner_.issuer() : RawThreadHandle{}};
  const std::uint8_t method = 6;
  RawSessionCall call{4100, 4101, context, std::array{raw_native_field(method)}};
  call.run([&] {
    credit_ = call.control(2 * RawCommandInterrupt::action_credit +
                           2 * RawCommandInterrupt::mask_credit);
    wake_ = std::make_unique<RawCommandWake>(RawCommandWake::Mode::signal);
    context_ = call.context();
  });
}
void TerminalResize::install() {
  owner_.require_thread();
  RawThreadBinding binding{owner_.selected() ? owner_.issuer() : RawThreadHandle{}};
  const std::uint8_t method = 8;
  RawSessionCall call{4100, 4101, &context_, std::array{raw_native_field(method)}};
  call.run([&] {
    TerminalResize *expected = nullptr;
    if (!resize_target.compare_exchange_strong(expected, this,
                                               std::memory_order_acq_rel))
      throw Error{ErrorCode::busy};
    wake_->publish();
    struct sigaction action{};
    action.sa_handler = &TerminalResize::handler;
    sigemptyset(&action.sa_mask);
    const auto installed = raw_command_action(
        SIGWINCH, action, previous_, wake_->reference(), credit_ ? &*credit_ : nullptr);
    if (installed.result < 0) {
      resize_target.store(nullptr, std::memory_order_release);
      wake_->retire();
      throw Error{ErrorCode::io, installed.error};
    }
    installed_ = true;
  });
}
void TerminalResize::handler(int) noexcept {
  const int saved = errno;
  if (auto *owner = resize_target.load(std::memory_order_acquire)) {
    owner->resized_.store(true, std::memory_order_release);
    owner->wake_->notify();
  }
  errno = saved;
}
void TerminalResize::observed() {
  owner_.require_thread();
  RawThreadBinding binding{owner_.selected() ? owner_.issuer() : RawThreadHandle{}};
  resized_.store(false, std::memory_order_release);
  auto cleared = wake_->clear();
  if (!cleared.has_value()) {
    if (cleared.error().code == ErrorCode::capacity ||
        cleared.error().code == ErrorCode::audit_unavailable)
      raw_capture_failed(cleared.error());
    throw cleared.error();
  }
  if (resized_.load(std::memory_order_acquire))
    wake_->notify();
}
void TerminalResize::uninstall() noexcept {
  if (!installed_)
    return;
  owner_.require_cleanup_thread();
  RawThreadBinding binding{owner_.selected() ? owner_.issuer() : RawThreadHandle{}};
  resize_target.store(nullptr, std::memory_order_release);
  struct sigaction discarded{};
  const auto result =
      raw_command_action(SIGWINCH, previous_, discarded, wake_->reference(),
                         credit_ ? &*credit_ : nullptr);
  if (result.result < 0)
    raw_fence({ErrorCode::io, result.error});
  installed_ = false;
}
TerminalResize::~TerminalResize() {
  uninstall();
  if (!wake_)
    return;
  owner_.require_cleanup_thread();
  RawThreadBinding binding{owner_.selected() ? owner_.issuer() : RawThreadHandle{}};
  const auto incarnation = wake_->reference();
  sigset_t blocked{}, previous{}, discarded{};
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGWINCH);
  if (raw_command_mask(SIG_BLOCK, blocked, previous, incarnation,
                       credit_ ? &*credit_ : nullptr) != 0)
    std::terminate();
  wake_->retire(); // Existing containing participant join has completed.
  wake_.reset();
  if (raw_command_mask(SIG_SETMASK, previous, discarded, incarnation,
                       credit_ ? &*credit_ : nullptr) != 0)
    std::terminate();
}
int terminal_native_poll(const RawAdapterContext *context, std::span<pollfd> fds,
                         int timeout, bool control) {
  const bool selected = selected_context(context);
  RawCaptureFailureScope failure{selected};
  const auto count = static_cast<nfds_t>(fds.size());
  auto credit = raw_ui_control(selected, 2 * linked + sizeof(count) +
                                             (fds.size() + 1) * sizeof(int) +
                                             2 * fds.size() * sizeof(short) +
                                             sizeof(int) + sizeof(bool) + sizeof(int));
  std::array<ByteView, 11> fields{};
  if (fds.size() > 4)
    throw Error{ErrorCode::invalid_range};
  std::size_t used = 0;
  fields[used++] = raw_native_field(count);
  for (const auto &fd : fds) {
    fields[used++] = raw_native_field(fd.fd);
    fields[used++] = raw_native_field(fd.events);
  }
  fields[used++] = raw_native_field(timeout);
  const auto request =
      publish(control ? 4108 : 4115,
              context ? context->parent.value_or(RawEventRef{}) : RawEventRef{},
              std::span{fields}.first(used), credit ? &*credit : nullptr);
  const int result = ::poll(fds.data(), count, timeout), error = errno;
  const bool failed = result < 0;
  used = 0;
  fields[used++] = raw_native_field(result);
  fields[used++] = raw_native_field(failed);
  if (failed)
    fields[used++] = raw_native_field(error);
  else
    for (const auto &fd : fds)
      fields[used++] = raw_native_field(fd.revents);
  (void)publish(control ? 4109 : 4116, request, std::span{fields}.first(used),
                credit ? &*credit : nullptr);
  errno = error;
  failure.require();
  failure.finish();
  return result;
}
void terminal_notification_drain(const RawAdapterContext *context, int fd) {
  const bool selected = selected_context(context);
  RawCaptureFailureScope failure{selected};
  const auto parent = context ? context->parent.value_or(RawEventRef{}) : RawEventRef{};
  std::array<char, 128> bytes{};
  while (true) {
    auto credit = raw_ui_control(selected, linked + sizeof(fd) + sizeof(std::size_t) +
                                               count_return);
    auto data = raw_ui_data(selected, linked + sizeof(std::size_t) + bytes.size());
    const auto requested = bytes.size();
    const auto request = publish(
        4117, parent, std::array{raw_native_field(fd), raw_native_field(requested)},
        credit ? &*credit : nullptr);
    const auto result = ::read(fd, bytes.data(), bytes.size());
    const int error = errno;
    returned(4118, request, result, error, credit ? &*credit : nullptr);
    if (result > 0 && data) {
      const auto size = static_cast<std::size_t>(result);
      (void)raw_publish_linked(
          4119, request,
          std::array{raw_native_field(size),
                     detail::raw_string_field({bytes.data(), size})},
          RawPartition::data, *data);
    }
    failure.require();
    if (result <= 0)
      break;
  }
  failure.finish();
}
void terminal_native_flush(const RawAdapterContext *context) {
  const bool selected = selected_context(context);
  RawCaptureFailureScope failure{selected};
  auto credit = raw_ui_control(selected, linked + 2 * sizeof(int) + integer_return);
  const int fd = STDIN_FILENO, queue = TCIFLUSH;
  const auto request =
      publish(4113, context ? context->parent.value_or(RawEventRef{}) : RawEventRef{},
              std::array{raw_native_field(fd), raw_native_field(queue)},
              credit ? &*credit : nullptr);
  const int result = ::tcflush(fd, queue), error = errno;
  returned(4114, request, result, error, credit ? &*credit : nullptr);
  failure.require();
  failure.finish();
}
void terminal_native_locale(const RawAdapterContext *context) {
  const bool selected = selected_context(context);
  auto credit = raw_ui_control(selected, 2 * linked + sizeof(int) + sizeof(char) +
                                             sizeof(char *) + sizeof(bool));
  const int category = LC_CTYPE;
  const char selection = '\0';
  const auto request =
      publish(4126, context ? context->parent.value_or(RawEventRef{}) : RawEventRef{},
              std::array{raw_native_field(category), raw_native_field(selection)},
              credit ? &*credit : nullptr);
  char *actual = std::setlocale(category, "");
  const bool present = actual != nullptr;
  (void)publish(4127, request,
                std::array{raw_native_field(present), raw_native_field(actual)},
                credit ? &*credit : nullptr);
}
namespace {
RawEventRef editor_request(RawEventRef parent, std::uint8_t method,
                           std::span<const ByteView> arguments,
                           RawControlToken *credit) noexcept {
  std::array<ByteView, 9> fields{};
  fields[0] = raw_native_field(method);
  std::copy(arguments.begin(), arguments.end(), fields.begin() + 1);
  return publish(4123, parent, std::span{fields}.first(arguments.size() + 1), credit);
}
RawEventRef editor_source(RawEventRef parent, std::uint8_t role, bool present,
                          std::string_view bytes,
                          std::optional<RawCapacityClaim> &credit) noexcept {
  if (!credit)
    return {};
  const auto size = bytes.size();
  const std::array fields{raw_native_field(role), raw_native_field(present),
                          present ? raw_native_field(size) : ByteView{},
                          present ? detail::raw_string_field(bytes) : ByteView{}};
  auto emitted = raw_publish_linked(
      4122, parent, std::span{fields}.first(present ? fields.size() : 2),
      RawPartition::data, *credit);
  credit.reset();
  return emitted.has_value() ? emitted.value() : RawEventRef{};
}
constexpr std::uint64_t editor_close_bytes =
    linked + 1 + sizeof(int) + integer_return + linked + 1 + sizeof(int) + sizeof(bool);
constexpr std::uint64_t editor_unlink_bytes =
    linked + 1 + 24 + integer_return + linked + 1 + sizeof(int) + sizeof(bool);
struct EditorCleanup {
  RawUiOwner owner;
  RawEventRef parent{}, path_source{};
  std::optional<RawControlToken> bank;
  std::string path;
  int fd = -1;
  bool owned = false;
  void close() noexcept {
    if (fd < 0)
      return;
    const auto request = editor_request(parent, 4, std::array{raw_native_field(fd)},
                                        bank ? &*bank : nullptr);
    const int result = ::close(fd), error = errno;
    returned(4124, request, result, error, bank ? &*bank : nullptr);
    descriptor(parent, 4, fd, false, bank ? &*bank : nullptr);
    fd = -1;
  }
  ~EditorCleanup() {
    owner.require_cleanup_thread();
    RawUiOwnerBinding binding{owner};
    close();
    if (owned) {
      const auto request = editor_request(
          parent, 10,
          std::array{ByteView{path_source.lane}, raw_native_field(path_source.ordinal)},
          bank ? &*bank : nullptr);
      const int result = ::unlink(path.c_str()), error = errno;
      returned(4124, request, result, error, bank ? &*bank : nullptr);
      descriptor(parent, 10, -1, false, bank ? &*bank : nullptr);
    }
  }
};
constexpr std::uint64_t child_state_bytes = linked + sizeof(pid_t) + 2 * sizeof(bool);
constexpr std::uint64_t child_cleanup_bytes =
    2 * RawCommandInterrupt::mask_credit + linked + 1 + sizeof(pid_t) + sizeof(int) +
    integer_return + linked + 1 + sizeof(pid_t) + sizeof(int) + linked + sizeof(pid_t) +
    sizeof(bool) + sizeof(int) + 2 * child_state_bytes;
struct EditorChild {
  RawEventRef parent;
  std::optional<RawControlToken> bank;
  pid_t pid = 0;
  bool owned = false, reaped = false;
  void state() noexcept {
    (void)publish(4128, parent,
                  std::array{raw_native_field(pid), raw_native_field(owned),
                             raw_native_field(reaped)},
                  bank ? &*bank : nullptr);
  }
  ~EditorChild() {
    if (pid <= 0)
      return;
    const int saved_error = errno;
    if (owned) {
      sigset_t all{}, previous{}, discarded{};
      sigfillset(&all);
      const int blocked =
          bank ? raw_command_mask(SIG_BLOCK, all, previous, parent, &*bank)
               : ::pthread_sigmask(SIG_BLOCK, &all, &previous);
      if (blocked == 0) {
        const int signal = SIGKILL;
        const auto killing = editor_request(
            parent, 12, std::array{raw_native_field(pid), raw_native_field(signal)},
            bank ? &*bank : nullptr);
        const int killed = ::kill(pid, signal), kill_error = errno;
        returned(4124, killing, killed, kill_error, bank ? &*bank : nullptr);
        const int options = 0;
        const auto waiting = editor_request(
            parent, 6, std::array{raw_native_field(pid), raw_native_field(options)},
            bank ? &*bank : nullptr);
        int status{};
        const auto waited = ::waitpid(pid, &status, options);
        const int wait_error = errno;
        const bool failed = waited < 0;
        if (waited > 0) {
          owned = false;
          reaped = true;
          (void)publish(4124, waiting,
                        std::array{raw_native_field(waited), raw_native_field(failed),
                                   raw_native_field(status)},
                        bank ? &*bank : nullptr);
        } else {
          if (waited < 0 && wait_error == ECHILD)
            owned = false;
          returned(4124, waiting, waited, wait_error, bank ? &*bank : nullptr);
        }
        if (bank)
          (void)raw_command_mask(SIG_SETMASK, previous, discarded, parent, &*bank);
        else
          (void)::pthread_sigmask(SIG_SETMASK, &previous, &discarded);
      }
      // A failed block/wait is an actual pending owner, never a fictional reap.
    }
    state();
    errno = saved_error;
  }
};
} // namespace
EditorResult edit_terminal_draft(std::string_view draft,
                                 const RawAdapterContext *context) {
  constexpr std::size_t limit = 1024 * 1024;
  const bool selected = selected_context(context);
  RawCaptureFailureScope failure{selected};
  const auto parent = context ? context->parent.value_or(RawEventRef{}) : RawEventRef{};
  const auto size = draft.size();
  // Actual original is the DATA request; shared UI adapter adopts that request.
  RawUiTrace trace{selected,
                   {4120, 4121},
                   std::array{ByteView{parent.lane}, raw_native_field(parent.ordinal),
                              raw_native_field(size), detail::raw_string_field(draft)},
                   raw_ui_error_bytes + linked + 2 * sizeof(bool) + 24 +
                       sizeof(std::uint8_t),
                   true};
  try {
    auto acquired = trace.run([&]() -> EditorResult {
      auto completed = [&]() -> EditorResult {
        // Cleanup is inside the paid outcome boundary, including returned allocation.
        EditorCleanup cleanup{RawUiOwner{trace.enabled()},
                              trace.reference(),
                              {},
                              raw_ui_control(trace.enabled(), 3 * editor_close_bytes +
                                                                  editor_unlink_bytes),
                              {},
                              -1,
                              false};
        auto result = [&](bool accepted, std::string text, std::string_view message,
                          std::uint8_t token, RawEventRef source = {}) {
          EditorResult value{accepted, std::move(text), std::string{message}};
          const bool has_source = accepted;
          const std::array fields{
              raw_native_field(accepted), raw_native_field(has_source),
              has_source ? ByteView{source.lane} : ByteView{},
              has_source ? raw_native_field(source.ordinal) : ByteView{},
              raw_native_field(token)};
          (void)trace.event(4125, fields);
          failure.require();
          return value;
        };
        if (draft.size() > limit)
          return result(false, {}, "Draft exceeds editor limit.", 1);
        auto &path = cleanup.path;
        auto &fd = cleanup.fd;
        const char *environment = nullptr;
        for (const char *name : {"TMPDIR", "TMP", "TEMP", "TEMPDIR"}) {
          environment = std::getenv(name);
          if (environment)
            break;
        }
        const std::string_view base{environment ? environment : "/tmp"};
        auto temp_source = raw_ui_data(trace.enabled(), raw_ui_add(51, base.size()));
        auto template_data = raw_ui_data(
            trace.enabled(),
            raw_ui_add(51, raw_ui_add(base.size(), sizeof("arco-draft-XXXXXX"))));
        (void)editor_source(trace.reference(), 1, environment != nullptr,
                            {base.data(), base.size() + 1}, temp_source);
        failure.require();
        auto directory_data = raw_ui_data(trace.enabled(), raw_ui_add(51, base.size()));
        std::filesystem::path directory{base};
        struct stat directory_info{};
        const auto directory_source = editor_source(
            trace.reference(), 8, true,
            {directory.c_str(), directory.native().size() + 1}, directory_data);
        auto directory_credit =
            raw_ui_control(trace.enabled(), linked + 1 + 24 + integer_return +
                                                sizeof(directory_info.st_mode));
        const auto directory_request =
            editor_request(trace.reference(), 11,
                           std::array{ByteView{directory_source.lane},
                                      raw_native_field(directory_source.ordinal)},
                           directory_credit ? &*directory_credit : nullptr);
        const int directory_result = ::stat(directory.c_str(), &directory_info),
                  directory_error = errno;
        if (directory_result < 0)
          returned(4124, directory_request, directory_result, directory_error,
                   directory_credit ? &*directory_credit : nullptr);
        else {
          const bool failed = false;
          (void)publish(4124, directory_request,
                        std::array{raw_native_field(directory_result),
                                   raw_native_field(failed),
                                   raw_native_field(directory_info.st_mode)},
                        directory_credit ? &*directory_credit : nullptr);
        }
        failure.require();
        if (directory_result != 0 || !S_ISDIR(directory_info.st_mode))
          throw std::filesystem::filesystem_error(
              "temp_directory_path", directory,
              std::error_code{directory_result < 0 ? directory_error : ENOTDIR,
                              std::generic_category()});
        path = (directory / "arco-draft-XXXXXX").string();
        const auto template_source = editor_source(
            trace.reference(), 2, true, {path.c_str(), path.size() + 1}, template_data);
        auto path_data = raw_ui_data(trace.enabled(), raw_ui_add(51, path.size()));
        auto create_credit = raw_ui_control(
            trace.enabled(), linked + 1 + 24 + integer_return +
                                 2 * (linked + 1 + 3 * sizeof(int) + integer_return));
        failure.require();
        {
          const std::lock_guard lock{detail::process_spawn_mutex};
          const auto request =
              editor_request(trace.reference(), 1,
                             std::array{ByteView{template_source.lane},
                                        raw_native_field(template_source.ordinal)},
                             create_credit ? &*create_credit : nullptr);
          fd = ::mkstemp(path.data());
          const int error = errno;
          cleanup.owned = fd >= 0; // Install actual ownership before publication.
          returned(4124, request, fd, error, create_credit ? &*create_credit : nullptr);
          cleanup.path_source = editor_source(
              trace.reference(), 3, true, {path.c_str(), path.size() + 1}, path_data);
          if (fd < 0)
            throw std::runtime_error("Cannot create private editor draft");
          if (fd < 3) {
            const int operation = F_DUPFD_CLOEXEC, argument = 3;
            const auto normalizing = editor_request(
                trace.reference(), 2,
                std::array{raw_native_field(fd), raw_native_field(operation),
                           raw_native_field(argument)},
                create_credit ? &*create_credit : nullptr);
            const int replacement = ::fcntl(fd, operation, argument),
                      native_error = errno;
            returned(4124, normalizing, replacement, native_error,
                     create_credit ? &*create_credit : nullptr);
            if (replacement < 0)
              throw std::runtime_error("Cannot normalize editor draft descriptor");
            cleanup.close();
            fd = replacement;
          }
          const int operation = F_SETFD, argument = FD_CLOEXEC;
          const auto protecting = editor_request(
              trace.reference(), 2,
              std::array{raw_native_field(fd), raw_native_field(operation),
                         raw_native_field(argument)},
              create_credit ? &*create_credit : nullptr);
          const int protected_fd = ::fcntl(fd, operation, argument),
                    native_error = errno;
          returned(4124, protecting, protected_fd, native_error,
                   create_credit ? &*create_credit : nullptr);
          if (protected_fd < 0)
            throw std::runtime_error("Cannot protect editor draft descriptor");
        }
        failure.require();
        auto remaining = draft;
        while (!remaining.empty()) {
          auto credit = raw_ui_control(trace.enabled(), linked + 1 + sizeof(int) + 24 +
                                                            2 * sizeof(std::size_t) +
                                                            count_return);
          const auto input = trace.reference();
          const auto offset = draft.size() - remaining.size(),
                     requested = remaining.size();
          const auto request = editor_request(
              trace.reference(), 3,
              std::array{raw_native_field(fd), ByteView{input.lane},
                         raw_native_field(input.ordinal), raw_native_field(offset),
                         raw_native_field(requested)},
              credit ? &*credit : nullptr);
          const auto count = ::write(fd, remaining.data(), remaining.size());
          const int error = errno;
          returned(4124, request, count, error, credit ? &*credit : nullptr);
          failure.require();
          if (count < 0 && error == EINTR)
            continue;
          if (count <= 0)
            throw std::runtime_error("Cannot write editor draft");
          remaining.remove_prefix(static_cast<std::size_t>(count));
        }
        cleanup.close();
        failure.require();
        const char *editor = std::getenv("VISUAL");
        if (!editor || !*editor)
          editor = std::getenv("EDITOR");
        if (!editor || !*editor)
          editor = "vi";
        const std::string_view editor_view{editor};
        auto editor_data =
            raw_ui_data(trace.enabled(), raw_ui_add(51, editor_view.size()));
        const auto quote_bound = raw_ui_add(2, raw_ui_multiply(4, path.size()));
        auto command_data = raw_ui_data(
            trace.enabled(),
            raw_ui_add(51, raw_ui_add(6, raw_ui_add(editor_view.size(), quote_bound))));
        (void)editor_source(trace.reference(), 4, true,
                            {editor_view.data(), editor_view.size() + 1}, editor_data);
        failure.require();
        std::string quoted = "'";
        for (const char byte : path)
          quoted += byte == '\'' ? "'\\''" : std::string(1, byte);
        quoted += '\'';
        std::string command = "exec " + std::string{editor_view} + " " + quoted;
        const auto command_source =
            editor_source(trace.reference(), 5, true,
                          {command.c_str(), command.size() + 1}, command_data);
        std::array<char *, 4> args{const_cast<char *>("sh"), const_cast<char *>("-c"),
                                   command.data(), nullptr};
        EditorChild child_owner{trace.reference(),
                                raw_ui_control(trace.enabled(), child_cleanup_bytes)};
        pid_t child{};
        auto launch_credit = raw_ui_control(
            trace.enabled(), linked + 1 + 24 + sizeof("/bin/sh") + sizeof("sh") +
                                 sizeof("-c") + sizeof(environ) + linked + sizeof(int) +
                                 sizeof(bool) + sizeof(pid_t));
        failure.require();
        const auto launch_request = editor_request(
            trace.reference(), 5,
            std::array{ByteView{command_source.lane},
                       raw_native_field(command_source.ordinal),
                       detail::raw_string_field(
                           std::string_view{"/bin/sh", sizeof("/bin/sh")}),
                       detail::raw_string_field(std::string_view{"sh", sizeof("sh")}),
                       detail::raw_string_field(std::string_view{"-c", sizeof("-c")}),
                       raw_native_field(environ)},
            launch_credit ? &*launch_credit : nullptr);
        const int launched = [&] {
          const std::lock_guard lock{detail::process_spawn_mutex};
          return ::posix_spawn(&child, "/bin/sh", nullptr, nullptr, args.data(),
                               environ);
        }();
        if (launched == 0) {
          child_owner.pid = child;
          child_owner.owned = true;
        }
        const bool launch_failed = launched != 0;
        if (launch_failed)
          (void)publish(
              4124, launch_request,
              std::array{raw_native_field(launched), raw_native_field(launch_failed)},
              launch_credit ? &*launch_credit : nullptr);
        else
          (void)publish(4124, launch_request,
                        std::array{raw_native_field(launched),
                                   raw_native_field(launch_failed),
                                   raw_native_field(child)},
                        launch_credit ? &*launch_credit : nullptr);
        if (launched == 0)
          child_owner.state();
        failure.require();
        if (launch_failed)
          return result(false, {}, "Editor could not start; draft retained.", 2);
        int status{};
        pid_t waited;
        do {
          const int options = 0;
          auto credit = raw_ui_control(
              trace.enabled(), linked + 1 + sizeof(child) + sizeof(options) + linked +
                                   sizeof(pid_t) + sizeof(bool) + sizeof(int));
          const auto request = editor_request(
              trace.reference(), 6,
              std::array{raw_native_field(child), raw_native_field(options)},
              credit ? &*credit : nullptr);
          waited = ::waitpid(child, &status, options);
          const int error = errno;
          if (waited > 0) {
            child_owner.owned = false;
            child_owner.reaped = true;
          } else if (waited < 0 && error == ECHILD)
            child_owner.owned = false;
          const bool failed = waited < 0;
          if (failed)
            returned(4124, request, waited, error, credit ? &*credit : nullptr);
          else if (waited > 0)
            (void)publish(4124, request,
                          std::array{raw_native_field(waited), raw_native_field(failed),
                                     raw_native_field(status)},
                          credit ? &*credit : nullptr);
          else
            (void)publish(
                4124, request,
                std::array{raw_native_field(waited), raw_native_field(failed)},
                credit ? &*credit : nullptr);
          errno = error;
          failure.require();
        } while (waited < 0 && errno == EINTR);
        if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
          return result(false, {}, "Editor cancelled or failed; draft retained.", 3);
        const auto path_source = cleanup.path_source;
        const int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
        auto open_credit = raw_ui_control(
            trace.enabled(), linked + 1 + 24 + sizeof(flags) + integer_return);
        const auto opening = editor_request(
            trace.reference(), 7,
            std::array{ByteView{path_source.lane},
                       raw_native_field(path_source.ordinal), raw_native_field(flags)},
            open_credit ? &*open_credit : nullptr);
        fd = ::open(path.c_str(), flags);
        const int open_error = errno;
        returned(4124, opening, fd, open_error, open_credit ? &*open_credit : nullptr);
        failure.require();
        if (fd < 0)
          return result(
              false, {},
              "Editor draft unavailable or not a regular file; original retained.", 4);
        struct stat info{};
        auto stat_credit = raw_ui_control(
            trace.enabled(), linked + 1 + sizeof(fd) + integer_return +
                                 sizeof(info.st_mode) + sizeof(info.st_size));
        const auto inspecting =
            editor_request(trace.reference(), 8, std::array{raw_native_field(fd)},
                           stat_credit ? &*stat_credit : nullptr);
        const int inspected = ::fstat(fd, &info), stat_error = errno;
        if (inspected < 0)
          returned(4124, inspecting, inspected, stat_error,
                   stat_credit ? &*stat_credit : nullptr);
        else {
          const bool failed = false;
          (void)publish(4124, inspecting,
                        std::array{raw_native_field(inspected),
                                   raw_native_field(failed),
                                   raw_native_field(info.st_mode),
                                   raw_native_field(info.st_size)},
                        stat_credit ? &*stat_credit : nullptr);
        }
        failure.require();
        if (inspected != 0 || !S_ISREG(info.st_mode))
          return result(
              false, {},
              "Editor draft unavailable or not a regular file; original retained.", 4);
        auto final_data = raw_ui_data(trace.enabled(), raw_ui_add(50, limit));
        std::string edited;
        std::array<char, 4096> bytes{};
        while (true) {
          const auto requested = bytes.size();
          auto credit =
              raw_ui_control(trace.enabled(), linked + 1 + sizeof(fd) +
                                                  sizeof(requested) + count_return);
          auto chunk_data = raw_ui_data(trace.enabled(), raw_ui_add(50, bytes.size()));
          const auto reading = editor_request(
              trace.reference(), 9,
              std::array{raw_native_field(fd), raw_native_field(requested)},
              credit ? &*credit : nullptr);
          const auto count = ::read(fd, bytes.data(), bytes.size());
          const int error = errno;
          returned(4124, reading, count, error, credit ? &*credit : nullptr);
          if (count > 0)
            (void)editor_source(reading, 7, true,
                                {bytes.data(), static_cast<std::size_t>(count)},
                                chunk_data);
          failure.require();
          if (count < 0 && error == EINTR)
            continue;
          if (count < 0)
            return result(false, {}, "Cannot read editor draft; original retained.", 5);
          if (count == 0)
            break;
          const auto extent = static_cast<std::size_t>(count);
          if (extent > limit - edited.size())
            return result(false, {}, "Editor draft exceeds 1 MiB; original retained.",
                          6);
          edited.append(bytes.data(), extent);
        }
        const auto final_source =
            editor_source(trace.reference(), 6, true, edited, final_data);
        failure.require();
        return result(true, std::move(edited),
                      "Editor returned; Enter sends explicitly.", 7, final_source);
      }();
      failure.require();
      return completed;
    });
    trace.finish();
    failure.require();
    failure.finish();
    return acquired;
  } catch (const std::exception &error) {
    failure.require();
    const RawAdapterContext diagnostic_context{
        nullptr,
        trace.enabled() || trace.reference().ordinal ? std::optional{trace.reference()}
                                                     : std::nullopt,
        nullptr, nullptr};
    const std::uint8_t method = 9;
    RawSessionCall diagnostic{4100, 4101, &diagnostic_context,
                              std::array{raw_native_field(method)}};
    auto returned_diagnostic = diagnostic.run([&] {
      const std::string_view message{error.what()};
      constexpr std::string_view suffix = "; original draft retained.";
      auto original_credit =
          raw_ui_data(diagnostic.enabled(), raw_ui_add(51, message.size()));
      auto message_credit =
          raw_ui_data(diagnostic.enabled(),
                      raw_ui_add(50, raw_ui_add(message.size(), suffix.size())));
      (void)editor_source(diagnostic.reference(), 9, true,
                          {message.data(), message.size() + 1}, original_credit);
      diagnostic.require_capture();
      EditorResult value{false, {}, std::string{message} + std::string{suffix}};
      (void)editor_source(diagnostic.reference(), 10, true, value.message,
                          message_credit);
      diagnostic.require_capture();
      return value;
    });
    failure.require();
    failure.finish();
    return returned_diagnostic;
  }
}
} // namespace blackbird
