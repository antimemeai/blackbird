#pragma once
#include "blackbird/raw_runtime.hpp"

namespace blackbird {
struct RawAuthoredReference {
  IdentityBytes lane{};
  std::uint64_t ordinal = 0;
};
struct RawAuthoredOwner {
  IdentityBytes generation;
  RawAuthoredReference vm;
  bool enabled;
  std::optional<Error> admission_failure{};
};
// Claim before acquiring arguments. A failed claim closes this owner's authored
// admission and is reflected by its held VM END, without an invented call event.
Result<std::optional<RawControlToken>> raw_authored_admit(RawAuthoredOwner &owner);
Result<std::uint64_t> raw_authored_emit(RawAuthoredOwner &, std::int64_t site,
                                        std::int64_t value);
Result<std::uint64_t> raw_authored_emit(RawAuthoredOwner &, std::int64_t site,
                                        double value);
Result<std::uint64_t> raw_authored_emit(RawAuthoredOwner &, std::int64_t site,
                                        bool value);
Result<std::uint64_t> raw_authored_emit(RawAuthoredOwner &, std::int64_t site,
                                        ByteView value);
RawAuthoredReference raw_authored_publish(std::uint32_t site,
                                          std::span<const ByteView> fields,
                                          RawControlToken *credit = nullptr);
std::string_view raw_authored_description() noexcept;
} // namespace blackbird
