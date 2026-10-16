#pragma once
#include <filesystem>
#include <string_view>
namespace blackbird {
struct RawAdapterContext;
struct RawFileInput;
// Destination publication is independent of result allocation/capture. Only the
// native rename owners can issue this fact; observation and publication allocate
// nothing. This says replacement happened, not parent-directory crash durability.
class FileEffectObservation {
public:
  bool applied() const noexcept { return applied_; }

private:
  bool applied_ = false;
  void replaced() noexcept { applied_ = true; }
  friend void write_file_observed(const std::filesystem::path &, std::string_view,
                                  FileEffectObservation *);
  friend void raw_file_write(const std::filesystem::path &, std::string_view,
                             const RawAdapterContext *, const RawFileInput *,
                             FileEffectObservation *);
};
} // namespace blackbird
