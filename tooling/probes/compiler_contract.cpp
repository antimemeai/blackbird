#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <span>
#include <thread>

static_assert(__cplusplus >= 202302L);
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
