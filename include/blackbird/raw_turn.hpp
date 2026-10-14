#pragma once
#include "blackbird/local_timing.hpp"
#include "blackbird/raw_provider.hpp"
#include "blackbird/retained_state.hpp"
namespace blackbird {
struct RawTurnState {
  bool running{}, backstop{}, capacity{}, budget{}, program{}, tools{}, workflows{};
  unsigned workflow_depth{}, workflow_invocations{};
  std::size_t operation_depth{}, unretained_bytes{};
};
struct RawPolicyFields {
  std::optional<bool> enabled;
  std::optional<std::size_t> target, trigger;
};
enum class RawPlaceholderOutcome : std::uint8_t {
  returned,
  error,
  allocation,
  unknown
};
class RawTaskActivityTrace {
public:
  RawTaskActivityTrace();
  RawTaskActivityTrace(const RawTaskActivityTrace &) = delete;
  RawTaskActivityTrace &operator=(const RawTaskActivityTrace &) = delete;
  void binding(RawEventRef operation, OperationAttemptId attempt,
               std::string_view task) noexcept;
  RawEventRef activity(std::uint8_t branch) noexcept;
  RawEventRef notification(RawEventRef activity, bool present) noexcept;
  void notified(RawEventRef notification, std::uint8_t branch,
                std::optional<Error> error = {}) noexcept;

private:
  std::optional<RawControlToken> credit_;
  RawEventRef binding_{};
};
class RawPolicyTrace {
public:
  RawPolicyTrace(std::uint8_t method, std::string_view revision, std::uint64_t credit,
                 std::optional<RawEventRef> parent = {},
                 RawControlToken *bank = nullptr);
  ~RawPolicyTrace();
  RawPolicyTrace(const RawPolicyTrace &) = delete;
  RawPolicyTrace &operator=(const RawPolicyTrace &) = delete;
  RawEventRef reference() const noexcept { return request_; }
  RawControlToken *bank() noexcept { return credit_ ? &*credit_ : nullptr; }
  void validation(const RawPolicyFields &fields) noexcept;
  void observation(bool enabled, std::optional<std::size_t> trigger, std::size_t input,
                   bool triggered) noexcept;
  void staged(std::string_view revision) noexcept;
  void finish(std::uint8_t branch, std::optional<Error> error = {}) noexcept;
  template <class F> decltype(auto) run(F &&body) {
    try {
      return std::forward<F>(body)();
    } catch (const Error &error) {
      finish(2, error);
      throw;
    } catch (const std::bad_alloc &) {
      finish(3);
      throw;
    } catch (...) {
      finish(4);
      throw;
    }
  }

private:
  void event(std::uint32_t site, std::span<const ByteView> fields) noexcept;
  std::optional<RawControlToken> credit_;
  RawEventRef request_{};
  bool finished_ = false;
};
class RawProtocolTrace {
public:
  explicit RawProtocolTrace(std::size_t rows, std::optional<RawEventRef> parent = {});
  ~RawProtocolTrace();
  RawProtocolTrace(const RawProtocolTrace &) = delete;
  RawProtocolTrace &operator=(const RawProtocolTrace &) = delete;
  void conflict(std::string_view call);
  void finish(std::uint8_t branch, std::optional<Error> error = {}) noexcept;
  template <class F> void run(F &&body) {
    try {
      std::forward<F>(body)();
      finish(0);
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
  bool finished_ = false;
};
class RawTurnTrace {
public:
  RawTurnTrace(bool running, bool backstop, DefinitionGenerationId generation,
               std::string_view head);
  ~RawTurnTrace();
  RawTurnTrace(const RawTurnTrace &) = delete;
  RawTurnTrace &operator=(const RawTurnTrace &) = delete;
  RawEventRef reference() const noexcept { return request_; }
  void admission(bool settlement_existed, JournalCapacity limits,
                 JournalCursor cursor) noexcept;
  void state(std::uint8_t stage, const RawTurnState &state) noexcept;
  RawEventRef activation(std::uint8_t kind, std::string_view revision) noexcept;
  RawEventRef source_request(DefinitionGenerationId generation,
                             std::string_view source);
  void source_return(RawEventRef request, std::uint8_t branch,
                     std::optional<SourceReference> source = {},
                     std::optional<Error> error = {}) noexcept;
  RawEventRef placeholder_request(bool interrupted, std::string_view head) noexcept;
  void placeholder_return(RawEventRef request, RawPlaceholderOutcome branch,
                          std::size_t rows = 0,
                          std::optional<Error> error = {}) noexcept;
  void finish(std::uint8_t branch, std::uint8_t stage,
              std::optional<Error> error = {}) noexcept;
  RawControlToken *context_bank() noexcept;
  RawControlToken *region_bank() noexcept;

private:
  void event(std::uint32_t site, RawEventRef parent,
             std::span<const ByteView> fields) noexcept;
  std::optional<RawControlToken> credit_, context_, regions_;
  RawEventRef request_{};
  bool finished_ = false;
};
std::string_view raw_turn_description() noexcept;
} // namespace blackbird
