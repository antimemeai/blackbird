#include "blackbird/local_timing.hpp"

#include <cerrno>

namespace blackbird {
RawClockSample raw_local_clock(bool cpu) noexcept {
  const int caller_errno = errno;
  RawClockSample sample;
  sample.monotonic.method = CLOCK_MONOTONIC;
  sample.monotonic.result =
      clock_gettime(sample.monotonic.method, &sample.monotonic.value);
  sample.monotonic.error = errno;
  sample.cpu_read = cpu;
  if (cpu) {
    sample.thread_cpu.method = CLOCK_THREAD_CPUTIME_ID;
    sample.thread_cpu.result =
        clock_gettime(sample.thread_cpu.method, &sample.thread_cpu.value);
    sample.thread_cpu.error = errno;
  }
  errno = caller_errno;
  return sample;
}

} // namespace blackbird
