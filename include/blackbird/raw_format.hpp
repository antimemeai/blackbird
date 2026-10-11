#pragma once
#include "blackbird/foundation.hpp"
#include <cstdint>
#include <span>

namespace blackbird {
enum class RawPartition : std::uint32_t { data = 1, control = 2, failure = 3 };
inline constexpr std::size_t raw_envelope_size = 16;
inline constexpr std::size_t raw_origin_size = 32;
struct RawOrigin {
  IdentityBytes lane;
  RawPartition partition;
  std::uint64_t source_start;
  bool operator==(const RawOrigin &) const = default;
};
struct RawAuditRecord {
  RawOrigin origin;
  ByteView raw;
};
} // namespace blackbird
