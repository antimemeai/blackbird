#pragma once
#include "blackbird/raw_command.hpp"
#include "blackbird/raw_session.hpp"
#include "blackbird/raw_ui.hpp"
#include <poll.h>
#include <termios.h>
namespace blackbird {
class TerminalMode {
public:
  explicit TerminalMode(const RawAdapterContext *context = nullptr);
  ~TerminalMode();
  void suspend();
  void resume();
  TerminalMode(const TerminalMode &) = delete;
  TerminalMode &operator=(const TerminalMode &) = delete;

private:
  RawUiOwner owner_;
  RawAdapterContext context_{};
  termios before_{};
  bool active_ = false;
  std::optional<RawControlToken> cleanup_;
  std::optional<RawCapacityClaim> exit_data_;
};
// The containing UI retains this object until the existing participant join.
class TerminalResize {
public:
  explicit TerminalResize(const RawAdapterContext *context);
  ~TerminalResize();
  TerminalResize(const TerminalResize &) = delete;
  TerminalResize &operator=(const TerminalResize &) = delete;
  int read_fd() const noexcept { return wake_->read_fd(); }
  void install();
  void observed();
  void uninstall() noexcept;

private:
  static void handler(int) noexcept;
  RawUiOwner owner_;
  RawAdapterContext context_{};
  std::optional<RawControlToken> credit_;
  std::unique_ptr<RawCommandWake> wake_;
  std::atomic_bool resized_{false};
  struct sigaction previous_{};
  bool installed_ = false;
};
// Actual pipe owner. All locked setup and lexical cleanup is prepaid.
class TerminalNotifications {
public:
  explicit TerminalNotifications(const RawAdapterContext *context);
  ~TerminalNotifications();
  int read_fd() const noexcept { return fds_[0]; }
  int write_fd() const noexcept { return fds_[1]; }

private:
  RawUiOwner owner_;
  RawEventRef parent_{};
  int fds_[2]{-1, -1};
  std::optional<RawControlToken> cleanup_;
};
int terminal_native_poll(const RawAdapterContext *context, std::span<pollfd> fds,
                         int timeout, bool control = false);
void terminal_notification_drain(const RawAdapterContext *context, int fd);
void terminal_native_flush(const RawAdapterContext *context);
void terminal_native_locale(const RawAdapterContext *context);
} // namespace blackbird
