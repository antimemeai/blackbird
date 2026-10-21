// libFuzzer entry over the decode of cut and repair frames from journal bytes
// (unit S1). Oracle: no crash or sanitizer report; a refusal is Error{corrupt} and
// nothing else; an accepted frame re-encodes to a packet that decodes to the same
// frame; apply_frame over fixed entries, with and without the recorded result,
// either applies or refuses with corrupt. The seeds (scripts/check-linux) name the
// fixed entries' refs, with a correct CRC32C, so every apply step is reached.
#include "blackbird/context_cut.hpp"
#include "blackbird/packet.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace {
using namespace blackbird;
bool corrupt_only(const Error &error) noexcept {
  return error.code == ErrorCode::corrupt;
}
Value::Array fixed_entries() {
  Value::Array entries;
  for (unsigned i = 0; i < 3; ++i) {
    IdentityBytes record{};
    record[0] = std::byte{0x11};
    entries.push_back(Value::object(
        {{"id", Value{entry_id({record, i})}},
         {"item", Value::object({{"role", Value{"assistant"}},
                                 {"content", Value{std::to_string(i)}}})}}));
  }
  return entries;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
  if (size > 65536)
    return 0;
  const auto bytes = std::as_bytes(std::span{data, size});
  const auto packet = decode_packet(bytes);
  if (!packet.has_value())
    return 0;
  CutFrame frame;
  try {
    frame = decode_cut_frame(packet.value());
  } catch (const Error &error) {
    if (!corrupt_only(error))
      std::abort();
    return 0;
  } catch (...) {
    std::abort();
  }
  try {
    const auto again = encode_cut_frame(decode_cut_frame(encode_cut_frame(frame)));
    if (!(again == encode_cut_frame(frame)))
      std::abort();
  } catch (...) {
    std::abort();
  }
  // Apply with the recorded result (step 6 checked) and without it, so a frame
  // whose CRC32C does not match still reaches steps 1-5.
  auto open = frame;
  open.result.reset();
  for (const auto *candidate : {&frame, &open}) {
    try {
      (void)apply_frame(*candidate, fixed_entries());
    } catch (const Error &error) {
      if (!corrupt_only(error))
        std::abort();
    } catch (...) {
      std::abort();
    }
  }
  return 0;
}
