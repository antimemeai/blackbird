#pragma once

#include "blackbird/local_timing.hpp"
#include "blackbird/provider_original.hpp"
#include "blackbird/retained_state.hpp"
#include <exception>
#include <stdexcept>

namespace blackbird {
// Native variants describe the actual C++ return/throw, not inferred success.
enum class RawOperationBranch : std::uint8_t {
  absent = 0,
  returned = 1,
  refused = 2,
  error = 3,
  allocation = 4,
  length = 5,
  standard = 6,
  nonstandard = 7,
  unfinished = 8
};
enum class RawOperationOriginal : std::uint8_t {
  input = 0,
  primary = 1,
  fallback = 2,
  capacity_message = 3
};
Result<std::uint64_t> raw_operation_append_credit(JournalLimits limits,
                                                  JournalCapacity capacity) noexcept;

class RawOperationTrace {
public:
  explicit RawOperationTrace(const RetainedState &root);
  ~RawOperationTrace();
  RawOperationTrace(const RawOperationTrace &) = delete;
  RawOperationTrace &operator=(const RawOperationTrace &) = delete;
  bool enabled() const noexcept { return owner_.has_value(); }
  RawEventRef reference() const noexcept { return request_; }
  RawControlClaim *retained_bank() noexcept { return regions_ ? &*regions_ : nullptr; }
  void stage(std::uint8_t value) noexcept { stage_ = value; }
  void disposition(AttemptDisposition value) noexcept { disposition_ = value; }
  void start(std::optional<RawEventRef> parent, DefinitionGenerationId generation,
             std::size_t depth, bool pumping, bool capacity_stopped);
  void name(std::string_view name);
  // All selected bodies begin with this exact owner's native Ref.
  void event(std::uint32_t site, std::span<const ByteView> fields);
  void identity(std::uint8_t kind, const IdentityBytes &identity);
  std::optional<ProviderOriginalReference>
  original(RawOperationOriginal role, ByteView bytes, RawCapacityClaim *data,
           std::optional<RawEventRef> provider = {});
  void source(RawOperationOriginal role,
              const std::optional<ProviderOriginalReference> &original);
  void region(RawEventRef region);
  void callback_return(std::uint8_t kind, RawOperationBranch branch,
                       const Error *error = nullptr, const bool *value = nullptr);
  template <class F>
  decltype(auto) callback(std::uint8_t kind, bool present, F &&body) {
    const std::array fields{detail::raw_native_field(kind),
                            detail::raw_native_field(present)};
    event(2415, fields);
    using Return = std::invoke_result_t<F>;
    static_assert(std::is_void_v<Return> || std::is_same_v<Return, bool>);
    auto invoke = [&]() -> Return {
      try {
        if constexpr (std::is_void_v<Return>) {
          if (present)
            std::forward<F>(body)();
        } else
          return present && std::forward<F>(body)();
      } catch (const Error &error) {
        callback_return(kind, RawOperationBranch::error, &error);
        throw;
      } catch (const std::bad_alloc &) {
        callback_return(kind, RawOperationBranch::allocation);
        throw;
      } catch (const std::length_error &) {
        callback_return(kind, RawOperationBranch::length);
        throw;
      } catch (const std::exception &) {
        callback_return(kind, RawOperationBranch::standard);
        throw;
      } catch (...) {
        callback_return(kind, RawOperationBranch::nonstandard);
        throw;
      }
    };
    if constexpr (std::is_void_v<Return>) {
      invoke();
      callback_return(kind, present ? RawOperationBranch::returned
                                    : RawOperationBranch::absent);
    } else {
      const bool result = invoke();
      callback_return(
          kind, present ? RawOperationBranch::returned : RawOperationBranch::absent,
          nullptr, present ? &result : nullptr);
      return result;
    }
  }
  void finish(RawOperationBranch branch, const Error *error = nullptr) noexcept;
  template <class F> decltype(auto) run(F &&body) {
    try {
      if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
        std::forward<F>(body)();
        finish(RawOperationBranch::returned);
      } else {
        auto result = std::forward<F>(body)();
        finish(RawOperationBranch::returned);
        return result;
      }
    } catch (const Error &error) {
      finish(RawOperationBranch::error, &error);
      throw;
    } catch (const std::bad_alloc &) {
      finish(RawOperationBranch::allocation);
      throw;
    } catch (const std::length_error &) {
      finish(RawOperationBranch::length);
      throw;
    } catch (const std::exception &) {
      finish(RawOperationBranch::standard);
      throw;
    } catch (...) {
      finish(RawOperationBranch::nonstandard);
      throw;
    }
  }

private:
  std::optional<RawControlClaim> owner_, regions_;
  RawEventRef request_;
  std::uint8_t stage_ = 0;
  std::optional<AttemptDisposition> disposition_;
};
std::string_view raw_operation_description() noexcept;
} // namespace blackbird
