#pragma once
#include "blackbird/provider_original.hpp"
#include "blackbird/raw_caller.hpp"
#include <filesystem>

namespace blackbird {
enum class RawFileMethod : std::uint8_t {
  read = 1,
  write = 2,
  edit = 3,
  canonical = 4,
  exists = 5,
  absolute = 6,
  prefix = 7,
  weak_canonical = 8
};
enum class RawFileBranch : std::uint8_t {
  returned = 0,
  error = 1,
  allocation = 2,
  filesystem = 3,
  standard = 4,
  nonstandard = 5,
  unfinished = 6
};
struct RawFileInput {
  RawEventRef body;
  std::size_t length = 0;
};
class RawFileToolTrace {
public:
  RawFileToolTrace(const RawAdapterContext *, RawFileMethod);
  ~RawFileToolTrace();
  RawFileToolTrace(const RawFileToolTrace &) = delete;
  RawFileToolTrace &operator=(const RawFileToolTrace &) = delete;
  const RawAdapterContext *context() const noexcept;
  void argument(std::uint8_t kind, std::string_view);
  void range(std::uint8_t mode, std::size_t first, std::size_t last);
  void selection(std::uint8_t step, RawEventRef, std::size_t length,
                 std::optional<std::size_t> position = {});
  std::optional<RawCapacityClaim> prepare_proposal(std::size_t);
  RawFileInput proposed(std::string_view, std::optional<RawCapacityClaim> &);
  void result(std::size_t count) noexcept { result_count_ = count; }
  void finish(RawOperationBranch, const Error * = nullptr) noexcept;
  void finish_filesystem(int) noexcept;
  template <class F> decltype(auto) run(F &&body) {
    return raw_call_run(*this, std::forward<F>(body));
  }
  template <class F> void observer(bool present, F &&body) {
    if (!emission_.enabled()) {
      if (present)
        std::forward<F>(body)();
      return;
    }
    auto bank = control(91);
    const auto request = observer_begin(present, bank);
    struct Observer {
      RawFileToolTrace &trace;
      RawEventRef request;
      RawControlToken &bank;
      void finish(RawOperationBranch branch, const Error *error = nullptr) noexcept {
        trace.observer_finish(request, branch, bank, error);
      }
      void finish_filesystem(int code) noexcept {
        trace.observer_end(request, RawFileBranch::filesystem, bank, nullptr, &code);
      }
    } observer{*this, request, bank};
    raw_call_run(observer, [&] {
      if (present)
        std::forward<F>(body)();
    });
  }

private:
  static RawControlToken control(std::uint64_t);
  const RawEventRef &parent() const noexcept;
  void finish(RawFileBranch, const Error * = nullptr, const int * = nullptr) noexcept;
  void observer_finish(RawEventRef, RawOperationBranch, RawControlToken &,
                       const Error *) noexcept;
  RawEventRef observer_begin(bool, RawControlToken &);
  void observer_end(RawEventRef, RawFileBranch, RawControlToken &,
                    const Error * = nullptr, const int * = nullptr) noexcept;
  RawThreadHandle issuer_;
  RawEmission emission_;
  RawAdapterContext selected_{};
  std::size_t result_count_ = 0;
};
std::string raw_file_read_prefix(const std::filesystem::path &, std::size_t,
                                 const RawAdapterContext * = nullptr,
                                 RawEventRef *actual_owner = nullptr);
std::string raw_file_read(const std::filesystem::path &, std::size_t,
                          const RawAdapterContext *, RawEventRef * = nullptr);
void raw_file_write(const std::filesystem::path &, std::string_view,
                    const RawAdapterContext *, const RawFileInput * = nullptr);
bool raw_file_exists(const std::filesystem::path &, std::error_code &,
                     const RawAdapterContext * = nullptr);
std::filesystem::path raw_file_canonical(const std::filesystem::path &,
                                         std::error_code &,
                                         const RawAdapterContext * = nullptr,
                                         RawEventRef *actual_owner = nullptr);
std::filesystem::path raw_file_absolute(const std::filesystem::path &,
                                        std::error_code &,
                                        const RawAdapterContext * = nullptr,
                                        RawEventRef *returned_body = nullptr);
std::filesystem::path raw_file_weak_canonical(const std::filesystem::path &,
                                              std::error_code &,
                                              const RawAdapterContext * = nullptr,
                                              RawEventRef *returned_body = nullptr);
std::string_view raw_file_description() noexcept;
} // namespace blackbird
