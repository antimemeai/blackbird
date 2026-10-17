#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <numeric>
#include <span>
#include <stdexcept>
#include <thread>

#ifndef __STRICT_ANSI__
#error "compiler contract requires ISO C++26 without GNU extensions"
#endif
#ifndef _LIBCPP_VERSION
#error "compiler contract requires libc++"
#endif

static_assert(__cplusplus > 202302L); // Clang 23 reports 202400L for C++26.
static_assert(std::saturating_add<std::int8_t>(100, 100) == 127);
static_assert(std::saturating_sub<std::uint8_t>(3, 5) == 0);
static_assert(std::saturating_cast<std::uint8_t>(300) == 255);
static_assert(std::saturating_cast<std::int8_t>(-300) == -128);
static_assert(std::byteswap(std::uint32_t{0x01020304}) == 0x04030201);

int main() {
  const std::expected<int, int> accepted{42};
  const std::expected<int, int> rejected{std::unexpected{7}};
  if (!accepted.has_value() || *accepted != 42 || rejected.has_value() ||
      rejected.error() != 7) {
    return 3;
  }
  const std::array<unsigned char, 4> bytes{0, 255, 13, 10};
  const std::span<const unsigned char> view{bytes};
  if (view.size() != 4 || view[0] != 0 || view[1] != 255) {
    return 1;
  }
  if (view.at(1) != 255) {
    return 4;
  }
  try {
    (void)view.at(4);
    return 5;
  } catch (const std::out_of_range &) {
  }
  std::mutex mutex;
  std::size_t count = 0;
  auto increment = [&]() {
    for (int i = 0; i < 1000; ++i) {
      const std::lock_guard lock{mutex};
      ++count;
    }
  };
  {
    const std::jthread first{increment};
    const std::jthread second{increment};
  }
  return count == 2000 ? 0 : 2;
}
