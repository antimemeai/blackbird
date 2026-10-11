#include "blackbird/raw_sampler.hpp"

#include <charconv>
#include <cstdio>
#include <limits>
#include <string_view>

namespace {
using namespace blackbird;
int failed(Error error) {
  std::fprintf(stderr, "resource sampler: %s (%lld)\n", error_name(error.code),
               static_cast<long long>(error.detail));
  return 1;
}
template <class T> Result<T> integer(std::string_view text) {
  T value{};
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    return Result<T>::failure({ErrorCode::invalid_range});
  return Result<T>::success(value);
}
Result<EnvironmentId> environment(std::string_view text) {
  if (text.size() != 32)
    return Result<EnvironmentId>::failure({ErrorCode::invalid_identity});
  IdentityBytes bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    unsigned value = 0;
    const auto part = text.substr(i * 2, 2);
    const auto parsed = std::from_chars(part.data(), part.data() + 2, value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != part.data() + 2)
      return Result<EnvironmentId>::failure({ErrorCode::invalid_identity});
    bytes[i] = std::byte{static_cast<unsigned char>(value)};
  }
  return EnvironmentId::from_bytes(bytes);
}
} // namespace
int main(int argc, char **argv) {
  using namespace blackbird;
  if (argc < 5) {
    std::fprintf(stderr,
                 "usage: blackbird-resource-sampler once SESSION ENVHEX "
                 "CONTROLLER PID METHOD | pipe SESSION ENVHEX CONTROL_FD [STOP_FD]\n");
    return 2;
  }
  try {
    const std::string_view operation{argv[1]};
    if ((operation == "once" && argc != 7) ||
        (operation == "pipe" && argc != 5 && argc != 6) ||
        (operation != "once" && operation != "pipe"))
      return 2;
    const auto env = environment(argv[3]);
    if (!env.has_value())
      return failed(env.error());
    RawSamplerOptions options;
    int control_fd = -1;
    RawSamplerRequest request{};
    if (operation == "pipe") {
      const auto descriptor = integer<int>(argv[4]);
      if (!descriptor.has_value() || descriptor.value() < 0)
        return 2;
      control_fd = descriptor.value();
      if (argc == 6) {
        const auto stop = integer<int>(argv[5]);
        if (!stop.has_value() || stop.value() < 0)
          return 2;
        options.control.cancellation_fds[0] = stop.value();
      }
    } else {
      const auto controller = integer<std::uint64_t>(argv[4]);
      const auto pid = integer<pid_t>(argv[5]);
      const auto method = integer<std::uint32_t>(argv[6]);
      if (!controller.has_value() || !pid.has_value() || !method.has_value())
        return 2;
      request = {controller.value(), pid.value(), method.value()};
    }
    auto runtime_result = RawRuntime::open(argv[2], env.value());
    if (!runtime_result.has_value())
      return failed(runtime_result.error());
    auto runtime = std::move(runtime_result).value();
    const auto prepared = runtime->prepare_thread(options.control);
    if (!prepared.has_value())
      return failed(prepared.error());
    std::optional<Error> error;
    {
      RawThreadBinding binding{prepared.value()};
      auto opened = RawResourceSampler::open(options);
      if (!opened.has_value()) {
        error = opened.error();
      } else {
        auto sampler = std::move(opened).value();
        const auto acquired =
            operation == "pipe" ? sampler->run(control_fd) : sampler->acquire(request);
        if (!acquired.has_value())
          error = acquired.error();
        const auto finished = sampler->finish();
        if (!finished.has_value())
          error = finished.error();
      }
    }
    // Release registration before RawRuntime's orderly supervisor/drain wait.
    // The caller's borrowed control/stop fds remain live throughout this scope.
    return error.has_value() ? failed(*error) : 0;
  } catch (const std::bad_alloc &) {
    return failed({ErrorCode::allocation});
  }
}
