#pragma once
#include "blackbird/local_timing.hpp"
#include "blackbird/raw_provider.hpp"
#include "blackbird/retained_state.hpp"
#include <type_traits>
namespace blackbird {
struct RawContextOrigin {
  RawEventRef parent{};
  RawControlToken *control = nullptr;
  RawControlToken *retained_regions = nullptr;
  enum class Input : std::uint8_t { fresh, no_new };
  Input input = Input::fresh;
};
struct WorkflowAcceptance {
  bool context_settlement_accepted = false;
  bool boundary_program_accepted = false;
};
enum class RawContextAction : std::uint8_t {
  reconstruct,
  checkpoint,
  view,
  stats,
  items,
  append,
  validate_seed,
  seed,
  edit,
  restore,
  originals,
  manage,
  inspect,
  budget,
  pending,
  begin,
  finish,
  reject,
  publish,
  original,
  cut,
  repair,
  pins,
  units,
  origins,
  instructions
};
struct RawContextState {
  std::string_view head;
  std::size_t entries, originals, captured;
  bool workflow, pending, archive_loaded;
};
struct RawContextPublication {
  RawEventRef owner;
  RawControlToken *control;
};
RawEventRef raw_context_publication(RawContextPublication publication,
                                    ApplicationRecordId identity,
                                    ApplicationChannel channel,
                                    JournalCursor cursor) noexcept;
void raw_context_publication_return(RawContextPublication publication,
                                    RawEventRef request, const RecordReference *record,
                                    const JournalCursor *cursor,
                                    const Error *error) noexcept;
class RawContextTrace {
public:
  RawContextTrace(RawContextAction action, std::uint64_t credit,
                  RawContextOrigin origin, const RawContextState *state = nullptr);
  ~RawContextTrace();
  RawContextTrace(const RawContextTrace &) = delete;
  RawContextTrace &operator=(const RawContextTrace &) = delete;
  RawEventRef reference() const noexcept { return request_; }
  RawContextOrigin child() noexcept;
  RawContextOrigin fresh() noexcept;
  RawContextPublication publication() noexcept;
  RawControlToken *bank() noexcept { return credit_ ? &*credit_ : nullptr; }
  void event(std::uint32_t site, std::span<const ByteView> fields) noexcept;
  RawEventRef request(std::uint32_t site, std::span<const ByteView> fields) noexcept;
  void reply(std::uint32_t site, RawEventRef request,
             std::span<const ByteView> fields) noexcept;
  template <class F> void protection(F &&body) {
    const std::uint8_t kind = 0;
    const auto reference = request(1804, std::array{detail::raw_native_field(kind)});
    try {
      std::forward<F>(body)(reference);
      protection_return(reference, 0);
    } catch (const Error &error) {
      protection_return(reference, 1, error);
      throw;
    } catch (const std::bad_alloc &) {
      protection_return(reference, 2);
      throw;
    } catch (...) {
      protection_return(reference, 3);
      throw;
    }
  }
  void state(std::uint8_t stage, const RawContextState &state) noexcept;
  void rows(std::uint8_t variant, std::size_t count) noexcept;
  std::optional<RawCapacityClaim> data_credit(std::uint64_t bytes);
  std::optional<RawControlToken> control_credit(std::uint64_t bytes);
  void data(std::uint32_t site, std::span<const ByteView> fields,
            RawCapacityClaim *credit) noexcept;
  void control(std::uint32_t site, std::span<const ByteView> fields,
               RawControlToken *credit) noexcept;
  void label(std::uint8_t kind, std::string_view bytes, bool cleanup_literal = false);
  void finish(std::uint8_t branch, std::optional<Error> error = {}) noexcept;
  template <class F> decltype(auto) run(F &&body) {
    try {
      if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
        std::forward<F>(body)();
        finish(branch_);
        return;
      } else {
        auto result = std::forward<F>(body)();
        finish(branch_);
        return result;
      }
    } catch (const Error &error) {
      finish(3, error);
      throw;
    } catch (const std::bad_alloc &) {
      finish(4);
      throw;
    } catch (...) {
      finish(5);
      throw;
    }
  }
  void branch(std::uint8_t branch) noexcept { branch_ = branch; }
  void stage(std::uint8_t stage) noexcept { stage_ = stage; }
  RawContextOrigin::Input input() const noexcept { return origin_.input; }

private:
  void protection_return(RawEventRef request, std::uint8_t branch,
                         std::optional<Error> error = {}) noexcept;
  RawContextOrigin origin_;
  std::optional<RawControlToken> credit_;
  std::optional<RawRegionPrepaidScope> regions_;
  RawEventRef request_{};
  std::uint8_t branch_ = 0, stage_ = 0;
  bool finished_ = false;
};
struct RawContextSource {
  std::uint8_t purpose;
  bool retained = false;
  std::optional<RecordReference> record;
  std::optional<ApplicationRecordId> application;
  ApplicationChannel channel = ApplicationChannel::context;
  std::size_t index = 0;
};
struct RawContextPairSites {
  std::uint32_t request, returned;
};
class RawContextPair {
public:
  RawContextPair(RawContextOrigin origin, std::uint64_t credit,
                 RawContextPairSites sites, std::span<const ByteView> fields);
  ~RawContextPair();
  RawContextPair(const RawContextPair &) = delete;
  RawContextPair &operator=(const RawContextPair &) = delete;
  RawEventRef reference() const noexcept { return request_; }
  void finish(std::uint8_t branch, std::span<const ByteView> fields = {}) noexcept;
  template <class F> decltype(auto) run(F &&body) {
    try {
      return std::forward<F>(body)();
    } catch (const Error &error) {
      failure(error);
      throw;
    } catch (const std::bad_alloc &) {
      failure({ErrorCode::allocation});
      throw;
    } catch (...) {
      failure({ErrorCode::external_unknown});
      throw;
    }
  }

private:
  void failure(Error error) noexcept;
  std::optional<RawControlToken> credit_;
  RawEventRef request_{};
  std::uint32_t return_site_;
  bool finished_ = false;
};
std::vector<std::byte> raw_context_read(const ImmutableBytes &payload,
                                        RawContextSource source,
                                        RawContextOrigin origin);
std::string_view raw_context_description() noexcept;
} // namespace blackbird
