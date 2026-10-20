// libFuzzer entry over the profile reader. Oracle: no crash or sanitizer report, a
// refusal names a closed-set construct, and an accepted table re-reads to itself.
#include "blackbird/provider_profile.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
  if (size > 65536)
    return 0;
  std::string text;
  text.reserve(size);
  for (std::size_t i = 0; i < size; ++i)
    text.push_back(static_cast<char>(data[i]));
  const auto result = blackbird::read_profile_text({text, "fuzz.lua"});
  if (!result.has_value()) {
    const auto &subject = result.error().subject;
    if (std::find(std::begin(blackbird::profile_reader_constructs),
                  std::end(blackbird::profile_reader_constructs),
                  subject) == std::end(blackbird::profile_reader_constructs) ||
        result.error().reason.empty())
      std::abort();
    return 0;
  }
  const auto formatted = blackbird::format_value(result.value());
  if (!formatted.has_value())
    std::abort();
  const auto again = blackbird::read_profile_text({"return " + formatted.value(), "f"});
  if (!again.has_value() || !(again.value() == result.value()))
    std::abort();
  return 0;
}
