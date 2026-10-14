#pragma once
#include "blackbird/raw_session.hpp"
#include <poll.h>
namespace blackbird {
enum class RawMainInputKind : std::uint8_t { once = 1, plain = 2, tui = 3, resume = 4 };
ssize_t raw_main_read(const RawAdapterContext *, int fd, void *buffer,
                      std::size_t size);
int raw_main_poll(const RawAdapterContext *, pollfd &, int timeout);
void raw_main_branch(RawSessionCall &, std::uint8_t branch);
void raw_main_control(const RawAdapterContext *, std::uint8_t kind,
                      std::string_view original);
std::string_view raw_input_description() noexcept;
} // namespace blackbird
