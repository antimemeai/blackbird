#pragma once
#include "blackbird/raw_provider.hpp"
#include "blackbird/retained_state.hpp"
#include <utility>
namespace blackbird {
enum class RawQueryMethod : std::uint8_t { inspect = 1, trajectory, variables };
enum class RawQueryField : std::uint8_t {
  end = 1,
  count,
  scan,
  cursor,
  limit,
  record,
  source,
  offset,
  variables,
  packet,
  attempt
};
enum class RawQueryLookupKind : std::uint8_t {
  fact = 1,
  attempt,
  source,
  payload,
  diagnostic
};
class RawQueryTrace {
public:
  RawQueryTrace(RawQueryMethod method, std::optional<OperationAttemptId> attempt,
                JournalCursor cursor, std::size_t facts);
  ~RawQueryTrace();
  RawQueryTrace(const RawQueryTrace &) = delete;
  RawQueryTrace &operator=(const RawQueryTrace &) = delete;
  RawEventRef reference() const noexcept { return request_; }
  void stage(std::uint8_t value) noexcept { stage_ = value; }
  template <class T> void field(RawQueryField field, const T &value) noexcept {
    const auto tag = static_cast<std::uint8_t>(field);
    const std::array fields{detail::raw_native_field(tag),
                            detail::raw_native_field(value)};
    event(2301, fields);
  }
  void identity_field(RawQueryField field, const IdentityBytes &value) noexcept;
  void variable(std::string_view value) noexcept;
  void returned(std::uint8_t shape, std::span<const std::size_t> fields) noexcept;
  void failure(std::uint8_t branch, std::optional<Error> error = {}) noexcept;
  template <class F> decltype(auto) run(F &&body) {
    try {
      return std::forward<F>(body)();
    } catch (const Error &error) {
      failure(1, error);
      throw;
    } catch (const std::bad_alloc &) {
      failure(2);
      throw;
    } catch (...) {
      failure(3);
      throw;
    }
  }

private:
  void event(std::uint32_t site, std::span<const ByteView> fields) noexcept;
  std::optional<RawControlToken> credit_;
  std::optional<RawCapacityClaim> variable_credit_;
  RawEventRef request_{};
  std::uint8_t stage_ = 0;
  bool finished_ = false;
};
class RawQueryLookup {
public:
  RawQueryLookup(RawEventRef parent, RawQueryLookupKind kind,
                 std::span<const ByteView> fields, std::uint64_t credit);
  ~RawQueryLookup();
  RawQueryLookup(const RawQueryLookup &) = delete;
  RawQueryLookup &operator=(const RawQueryLookup &) = delete;
  RawEventRef reference() const noexcept { return request_; }
  void fact(const RetainedFact &fact, RetainedKind kind) noexcept;
  void attempt(const AttemptState &state) noexcept;
  void bytes(std::size_t length) noexcept;
  void filter(std::uint8_t branch) noexcept;
  void failure(std::uint8_t branch, std::optional<Error> error = {}) noexcept;
  template <class F> decltype(auto) run(F &&body) {
    try {
      return std::forward<F>(body)();
    } catch (const Error &error) {
      failure(1, error);
      throw;
    } catch (const std::bad_alloc &) {
      failure(2);
      throw;
    } catch (...) {
      failure(3);
      throw;
    }
  }

private:
  void returned(std::span<const ByteView> fields) noexcept;
  std::optional<RawControlToken> credit_;
  RawEventRef request_{};
  bool finished_ = false;
};
class RawQueryPacket {
public:
  RawQueryPacket(std::uint8_t method, RawEventRef query, RecordReference record,
                 std::size_t input, ValueLimits limits,
                 std::optional<RawEventRef> prior_read = {});
  ~RawQueryPacket();
  RawQueryPacket(const RawQueryPacket &) = delete;
  RawQueryPacket &operator=(const RawQueryPacket &) = delete;
  RawEventRef reference() const noexcept { return request_; }
  void stage(std::uint8_t value) noexcept { stage_ = value; }
  void finish(std::uint8_t branch, std::optional<Error> error = {}) noexcept;
  template <class F> decltype(auto) run(F &&body) {
    try {
      return std::forward<F>(body)();
    } catch (const Error &error) {
      finish(1, error);
      throw;
    } catch (const std::bad_alloc &) {
      finish(2);
      throw;
    } catch (...) {
      finish(3);
      throw;
    }
  }

private:
  std::optional<RawControlToken> credit_;
  RawEventRef request_{};
  std::uint8_t stage_ = 0;
  bool finished_ = false;
};
std::string_view raw_query_description() noexcept;
} // namespace blackbird
