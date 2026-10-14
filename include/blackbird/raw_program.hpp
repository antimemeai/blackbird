#pragma once
#include "blackbird/raw_caller.hpp"
#include <mutex>

namespace blackbird {
struct RawProgramContext {
  std::optional<RawEventRef> parent;
  std::optional<IdentityBytes> generation;
};
class RawProgramCall {
public:
  RawProgramCall(std::uint8_t method, const RawProgramContext *context);
  RawProgramCall(std::uint32_t request, std::uint32_t terminal,
                 const RawProgramContext *context, std::span<const ByteView> fields,
                 std::uint64_t terminal_extra = 0);
  ~RawProgramCall();
  RawProgramCall(const RawProgramCall &) = delete;
  RawProgramCall &operator=(const RawProgramCall &) = delete;
  bool enabled() const noexcept { return emission_.enabled(); }
  RawEventRef reference() const noexcept { return emission_.reference(); }
  RawProgramContext context() const noexcept { return {reference(), {}}; }
  void stage(std::uint8_t stage) noexcept { stage_ = stage; }
  std::optional<RawControlToken> control(std::uint64_t amount) const;
  std::optional<RawCapacityClaim> data(std::uint64_t amount) const;
  void event(std::uint32_t site, std::span<const ByteView> fields,
             std::optional<RawControlToken> &credit) noexcept;
  void bytes(std::uint32_t site, std::string_view bytes,
             std::optional<RawCapacityClaim> &credit) noexcept;
  void encoded(std::uint8_t kind, std::string_view bytes,
               std::optional<RawCapacityClaim> &credit) noexcept;
  void revision(std::uint8_t kind, std::string_view revision);
  void validation(std::uint8_t kind, std::size_t size,
                  std::optional<std::size_t> index = {});
  void finish(RawOperationBranch branch, const Error *error = nullptr,
              std::span<const ByteView> extra = {}) noexcept;
  template <class F> decltype(auto) run(F &&body) {
    return raw_call_run(*this, std::forward<F>(body));
  }

private:
  void begin(std::uint32_t site, std::uint32_t terminal,
             std::span<const ByteView> fields, std::uint64_t terminal_extra);
  RawEmission emission_;
  std::uint8_t stage_ = 0;
};

// This issuer is dedicated to serialized program-lifetime emission. It is never
// borrowed from an active Main/worker lane. Only immediate paid publication and
// finite try-admission run under mutex; no ordinary capacity wait can hold it.
class RawProgramIssuer {
public:
  static std::shared_ptr<RawProgramIssuer> prepare();
  RawThreadHandle handle;
  std::mutex mutex;
};
class RawProgramLifetime {
public:
  RawProgramLifetime() = default;
  RawProgramLifetime(std::shared_ptr<RawProgramIssuer> issuer, RawEventRef parent,
                     bool queued, std::uint8_t kind = 0);
  ~RawProgramLifetime();
  RawProgramLifetime(RawProgramLifetime &&other) noexcept;
  RawProgramLifetime &operator=(RawProgramLifetime &&other) noexcept;
  RawProgramLifetime(const RawProgramLifetime &) = delete;
  RawProgramLifetime &operator=(const RawProgramLifetime &) = delete;
  void generation(IdentityBytes generation) noexcept;
  void queued(std::size_t count) noexcept;
  void installed(std::string_view revision) noexcept;
  void installed(std::size_t count) noexcept;
  void finish(std::uint8_t reason, const Error *error = nullptr,
              std::optional<RawEventRef> related = {},
              RawOperationBranch branch = RawOperationBranch::returned) noexcept;

private:
  RawEventRef emit(std::uint32_t site, RawEventRef parent,
                   std::span<const ByteView> fields) noexcept;
  std::shared_ptr<RawProgramIssuer> issuer_;
  std::optional<RawControlToken> credit_;
  RawEventRef parent_{}, request_{};
  std::uint8_t kind_ = 0;
  bool queued_ = false;
};
std::string_view raw_program_description() noexcept;
} // namespace blackbird
