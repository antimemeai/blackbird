#pragma once
#include "blackbird/raw_caller.hpp"
#include <filesystem>
#include <sys/stat.h>
namespace blackbird {
// Whole synchronous session/input owner; native sources are explicit fields.
class RawSessionCall {
public:
  RawSessionCall(std::uint32_t request, std::uint32_t terminal,
                 const RawAdapterContext *context,
                 std::span<const ByteView> fields = {},
                 std::uint64_t terminal_extra = 0)
      : failure_(context && context->parent && raw_enabled()),
        trace_({request, terminal}, context, fields, terminal_extra) {}
  RawEventRef reference() const noexcept { return trace_.reference(); }
  RawAdapterContext context() const noexcept { return trace_.context(); }
  bool enabled() const noexcept { return trace_.enabled(); }
  void stage(std::uint8_t value) noexcept { trace_.stage(value); }
  void require_capture() const { failure_.require(); }
  std::optional<RawCapacityClaim> source_credit(std::uint64_t bound) const;
  std::optional<RawCapacityClaim> bytes_credit(std::uint64_t bound) const;
  RawEventRef bytes(std::uint32_t site, std::string_view value,
                    std::optional<RawCapacityClaim> &credit) const;
  std::optional<RawControlToken> control(std::uint64_t bytes) const {
    return trace_.control(bytes);
  }
  RawEventRef source(std::uint8_t kind, std::string_view bytes,
                     std::optional<RawCapacityClaim> &credit,
                     std::uint32_t site = 4212) const;
  RawEventRef source(std::uint8_t kind, std::string_view bytes,
                     std::uint32_t site = 4212) const;
  RawEventRef event(std::uint32_t site, std::span<const ByteView> fields,
                    std::optional<RawControlToken> &credit) const;
  void finish(RawOperationBranch branch, const Error *error = nullptr,
              std::span<const ByteView> fields = {}) noexcept {
    trace_.finish(branch, error, fields);
  }
  template <class F> decltype(auto) run(F &&body) {
    struct Finish {
      RawCaptureFailureScope &scope;
      ~Finish() { scope.finish(); }
    } finish{failure_};
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      trace_.run([&] {
        std::forward<F>(body)();
        require_capture();
      });
      require_capture();
    } else {
      auto result = trace_.run([&] {
        auto acquired = std::forward<F>(body)();
        require_capture();
        return acquired;
      });
      require_capture();
      return result;
    }
  }

private:
  RawCaptureFailureScope failure_;
  RawCallerTrace trace_;
};
std::uint64_t raw_session_bound(std::uint64_t bytes, std::uint64_t suffix);
std::uint64_t raw_session_product(std::uint64_t bytes, std::uint64_t count);
RawEventRef raw_session_record(std::uint32_t site, RawEventRef parent,
                               std::span<const ByteView> fields, RawPartition partition,
                               RawCapacityClaim *credit);
enum class RawSessionStat : std::uint8_t { lstat = 1, stat = 2 };
int raw_session_stat(const RawAdapterContext *, RawSessionStat,
                     const std::filesystem::path &, struct stat &);
std::string_view raw_session_description() noexcept;
} // namespace blackbird
