#pragma once

#include "blackbird/raw_operation.hpp"
#include <filesystem>

namespace blackbird {
bool raw_reference_valid(RawEventRef reference) noexcept;
// Shared raw mechanics. Callers supply literal native parts and their own wire
// layout; publication neither interprets nor transforms their contents.
std::uint64_t raw_record_bytes(std::span<const ByteView> fields);
Result<RawEventRef> raw_publish(std::uint32_t site, std::span<const ByteView> fields,
                                RawPartition partition,
                                RawCapacityClaim &bank) noexcept;
Result<RawEventRef> raw_publish_linked(std::uint32_t site, RawEventRef parent,
                                       std::span<const ByteView> fields,
                                       RawPartition partition,
                                       RawCapacityClaim &bank) noexcept;
enum class RawAdmission : std::uint8_t { waiting, finite };
struct RawCallSites {
  std::uint32_t request, terminal;
};
class RawEmission {
public:
  RawEmission() = default;
  RawEmission(RawEmission &&other) noexcept;
  RawEmission(const RawEmission &) = delete;
  RawEmission &operator=(const RawEmission &) = delete;
  RawEmission &operator=(RawEmission &&) = delete;
  // A prepared bank is not an emitted request. Its destruction releases space;
  // the surface alone decides the fields of an unfinished terminal event.
  void prepare(std::uint64_t amount, RawAdmission admission = RawAdmission::waiting);
  void begin(RawCallSites sites, std::span<const ByteView> fields);
  void start(RawCallSites sites, std::uint64_t amount, std::span<const ByteView> fields,
             RawAdmission admission = RawAdmission::waiting);
  Result<void> adopt(RawEventRef request, std::uint32_t terminal,
                     RawControlToken &&bank) noexcept;
  bool enabled() const noexcept { return bank_.has_value() && request_.ordinal != 0; }
  const RawEventRef &reference() const noexcept { return request_; }
  RawControlToken *bank() noexcept { return bank_ ? &*bank_ : nullptr; }
  void close(std::span<const ByteView> complete_fields) noexcept;
  void close_linked(std::span<const ByteView> fields) noexcept;
  void release() noexcept;
  void finish(RawOperationBranch branch, std::uint8_t stage,
              const Error *error = nullptr,
              std::span<const ByteView> fields = {}) noexcept;

private:
  std::optional<RawControlToken> bank_;
  RawEventRef request_{};
  std::uint32_t terminal_ = 0;
};
// Compile-time dispatch to the surface's actual terminal fields. File owners
// supply finish_filesystem; ordinary owners retain the standard exception tag.
template <bool FinishReturned = true, class Owner, class F>
std::invoke_result_t<F> raw_call_run(Owner &owner, F &&body) {
  using Return = std::invoke_result_t<F>;
  try {
    if constexpr (std::is_void_v<Return>) {
      std::forward<F>(body)();
      if constexpr (FinishReturned)
        owner.finish(RawOperationBranch::returned);
    } else {
      decltype(auto) result = std::forward<F>(body)();
      if constexpr (FinishReturned)
        owner.finish(RawOperationBranch::returned);
      if constexpr (std::is_rvalue_reference_v<Return>)
        return std::move(result);
      else
        return result;
    }
  } catch (const Error &error) {
    owner.finish(RawOperationBranch::error, &error);
    throw;
  } catch (const std::bad_alloc &) {
    owner.finish(RawOperationBranch::allocation);
    throw;
  } catch (const std::length_error &) {
    owner.finish(RawOperationBranch::length);
    throw;
  } catch (const std::filesystem::filesystem_error &error) {
    if constexpr (requires { owner.finish_filesystem(error.code().value()); })
      owner.finish_filesystem(error.code().value());
    else if constexpr (requires { owner.finish_system(error.code().value()); })
      owner.finish_system(error.code().value());
    else
      owner.finish(RawOperationBranch::standard);
    throw;
  } catch (const std::system_error &error) {
    if constexpr (requires { owner.finish_system(error.code().value()); })
      owner.finish_system(error.code().value());
    else
      owner.finish(RawOperationBranch::standard);
    throw;
  } catch (const std::exception &) {
    owner.finish(RawOperationBranch::standard);
    throw;
  } catch (...) {
    owner.finish(RawOperationBranch::nonstandard);
    throw;
  }
}
// Immediate native request/terminal emission. Each owner supplies static sites
// and its declared native fields; no Values, formatting or metric processing.
class RawCallerTrace {
public:
  RawCallerTrace(RawCallSites sites, const RawAdapterContext *context,
                 std::span<const ByteView> fields = {},
                 std::uint64_t terminal_extra = 0);
  ~RawCallerTrace();
  RawCallerTrace(const RawCallerTrace &) = delete;
  RawCallerTrace &operator=(const RawCallerTrace &) = delete;
  bool enabled() const noexcept { return emission_.enabled(); }
  RawEventRef reference() const noexcept { return emission_.reference(); }
  RawAdapterContext context() const noexcept {
    return {nullptr, enabled() ? std::optional{reference()} : std::nullopt, nullptr,
            nullptr};
  }
  void stage(std::uint8_t value) noexcept { stage_ = value; }
  std::optional<RawCapacityClaim> data(std::uint64_t bytes) const;
  std::optional<RawControlToken> control(std::uint64_t bytes) const;
  void bytes(std::uint32_t site, std::string_view value,
             std::optional<RawCapacityClaim> &credit) noexcept;
  void event(std::uint32_t site, std::span<const ByteView> fields,
             std::optional<RawControlToken> &credit) noexcept;
  void data_event(std::uint32_t site, std::span<const ByteView> fields,
                  std::optional<RawCapacityClaim> &credit) noexcept;
  void finish(RawOperationBranch branch, const Error *error = nullptr,
              std::span<const ByteView> fields = {}) noexcept;
  template <class F> decltype(auto) run(F &&body) {
    return raw_call_run(*this, std::forward<F>(body));
  }

private:
  std::optional<Error> publish(std::uint32_t site, RawEventRef parent,
                               std::span<const ByteView> fields, RawPartition partition,
                               RawControlToken &credit,
                               RawEventRef *output = nullptr) noexcept;
  RawEmission emission_;
  std::uint8_t stage_ = 0;
};
std::string_view raw_caller_description() noexcept;
} // namespace blackbird
