#include "blackbird/local_timing.hpp"

#include <cerrno>
#include <stdexcept>

namespace {
void check(bool condition) {
  if (!condition)
    throw std::runtime_error("native raw clock contract failed");
}
bool before(const timespec &a, const timespec &b) {
  return a.tv_sec < b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_nsec <= b.tv_nsec);
}
} // namespace
int main() {
  timespec first{}, last{};
  check(::clock_gettime(CLOCK_MONOTONIC, &first) == 0);
  errno = EDOM;
  const auto measured = blackbird::raw_local_clock(true);
  check(errno == EDOM);
  check(::clock_gettime(CLOCK_MONOTONIC, &last) == 0);
  check(measured.monotonic.method == CLOCK_MONOTONIC &&
        measured.monotonic.result == 0 && before(first, measured.monotonic.value) &&
        before(measured.monotonic.value, last));
  check(measured.cpu_read && measured.thread_cpu.method == CLOCK_THREAD_CPUTIME_ID &&
        measured.thread_cpu.result == 0 && measured.thread_cpu.value.tv_sec >= 0 &&
        measured.thread_cpu.value.tv_nsec >= 0 &&
        measured.thread_cpu.value.tv_nsec < 1000000000L);
  errno = EINVAL;
  const auto unclocked_cpu = blackbird::raw_local_clock(false);
  check(errno == EINVAL && !unclocked_cpu.cpu_read &&
        unclocked_cpu.thread_cpu.result == 0 && unclocked_cpu.thread_cpu.error == 0 &&
        unclocked_cpu.thread_cpu.value.tv_sec == 0 &&
        unclocked_cpu.thread_cpu.value.tv_nsec == 0);
}
