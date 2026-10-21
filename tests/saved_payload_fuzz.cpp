// libFuzzer entry over the state-file body (unit S1): decode_saved_payload, the
// file-version-2 parser the loader uses, then validate_saved_context. The input is
// [u8 file version][u32 context size][u32 program size][body]. Oracle: no crash or
// sanitizer report; a malformed body is Error{corrupt}, a packet past its limits or in
// an unread form keeps its own error (capacity, unsupported); a body that decodes
// re-encodes (encode_saved_payload) to one that decodes to the same context and
// programs, and the validator gives the same verdict for both.
#include "blackbird/saved_context.hpp"
#include "blackbird/saved_state.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <span>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
  using namespace blackbird;
  if (size < 9 || size > 262144)
    return 0;
  const auto read32 = [&](std::size_t at) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
      value |= static_cast<std::uint64_t>(data[at + i]) << (8 * i);
    return value;
  };
  const std::uint64_t version = data[0] % 3;
  const auto body = std::as_bytes(std::span{data + 9, size - 9});
  const auto payload = decode_saved_payload(body, version, read32(1), read32(5));
  if (!payload.has_value()) {
    const auto code = payload.error().code;
    if (code != ErrorCode::corrupt && code != ErrorCode::allocation &&
        code != ErrorCode::capacity && code != ErrorCode::unsupported)
      std::abort();
    return 0;
  }
  // A context object with a repeated key has no single reading to compare.
  {
    std::set<std::string_view> keys;
    for (const auto &[key, value] : payload.value().context.object())
      if (!keys.insert(key).second)
        return 0;
  }
  const auto verdict = validate_saved_context(payload.value().context);
  if (!verdict.has_value() && verdict.error().code != ErrorCode::corrupt)
    std::abort();
  const auto encoded = encode_saved_payload(payload.value());
  if (!encoded.has_value())
    return 0; // a value over one packet's limit is the writer's refusal, not a fault
  const auto again =
      decode_saved_payload(encoded.value().bytes, 2, encoded.value().context_size,
                           encoded.value().program_size);
  if (!again.has_value() || again.value().end != encoded.value().bytes.size() ||
      !(again.value().programs == payload.value().programs))
    std::abort();
  // The streamed lists move to the end of the context object; compare by key.
  for (const auto &[key, value] : payload.value().context.object()) {
    const auto *other = again.value().context.find(key);
    if (!other || !(*other == value))
      std::abort();
  }
  if (again.value().context.object().size() !=
          payload.value().context.object().size() ||
      validate_saved_context(again.value().context).has_value() != verdict.has_value())
    std::abort();
  return 0;
}
