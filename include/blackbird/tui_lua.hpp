#pragma once
#include "blackbird/raw_provider.hpp"
#include "blackbird/tui_components.hpp"
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace blackbird::tui::lua {
inline constexpr Limits scene_limits{4, 32768, 196608};
inline constexpr std::size_t source_limit = 65536, heap_limit = 16 * 1024 * 1024;
inline constexpr std::uint64_t instruction_limit = 100000;
template <std::size_t N> struct Text {
  std::array<char, N> bytes{};
  std::size_t size = 0;
  std::string_view view() const noexcept { return {bytes.data(), size}; }
  bool operator==(const Text &) const = default;
};
struct Dependencies {
  EnvironmentId environment;
  ConversationId conversation;
  ViewIdentity view;
  std::uint64_t source_generation = 0, request_sequence = 0;
  std::uint64_t scene_revision = 0, local_input_revision = 0, query_revision = 0;
  bool operator==(const Dependencies &) const = default;
};
struct FixedTarget {
  Text<256> owner;
  Text<32> kind;
  Text<128> key;
  std::uint64_t incarnation = 0;
  bool operator==(const FixedTarget &) const = default;
};
struct InputRow {
  Text<128> key;
  Text<512> label;
  FixedTarget target;
};
struct PresentationInput {
  Dependencies stamp;
  std::array<InputRow, 8> rows{};
  std::size_t row_count = 0;
  std::array<std::array<std::byte, 4096>, 2> detail{};
  std::array<std::size_t, 2> detail_size{};
  std::array<Text<512>, 2> detail_titles{};
};
class Script {
public:
  static Result<std::shared_ptr<const Script>> create(std::string source);
  std::string_view source() const noexcept { return source_; }

private:
  explicit Script(std::string source) : source_(std::move(source)) {}
  std::string source_;
};
struct ActionKey {
  IdentityBytes inbox{};
  std::uint64_t owner = 0, trigger = 0;
  std::uint32_t ordinal = 0;
  bool operator==(const ActionKey &) const = default;
};
class Trigger {
public:
  ActionKey key(std::uint32_t ordinal = 0) const noexcept;

private:
  friend class ActionInbox;
  struct State;
  explicit Trigger(std::shared_ptr<State> state) : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
};
struct ActionEnvelope {
  ActionKey key;
  Dependencies stamp;
  std::uint64_t acceptance_order = 0;
  RawEventRef input_origin{};
  FixedTarget target;
  Text<64> handler, verb;
};
struct ActionPage {
  std::array<std::optional<ActionEnvelope>, 8> entries{};
  std::size_t size = 0;
};
namespace detail {
struct Invocation;
}
class ActionInbox {
public:
  static Result<std::shared_ptr<ActionInbox>> create(IdentityBytes incarnation);
  Result<Trigger> new_trigger() noexcept;
  Result<ActionPage> try_read() const noexcept;
  // Consumer must own the envelope and actual domain reference/unknown disposition.
  Result<void> acknowledge(ActionKey key) noexcept;
  ~ActionInbox();

private:
  friend struct detail::Invocation;
  friend class PresentationOwner;
  struct State;
  explicit ActionInbox(std::unique_ptr<State> state);
  Result<void> accept(const ActionEnvelope &envelope, const Trigger &trigger) noexcept;
  bool owns(const Trigger &trigger) const noexcept;
  std::unique_ptr<State> state_;
};
struct ComposeRequest {
  std::shared_ptr<const Script> script;
  PresentationInput input;
  bool preview = false;
};
struct HandlerRequest {
  std::shared_ptr<const Script> script;
  Dependencies stamp;
  Text<64> name;
  FixedTarget frozen_target;
  RawEventRef input_origin;
  Trigger trigger;
};
enum class Completion : std::uint8_t {
  candidate,
  handler_finished,
  script_error,
  native_failure,
  allocation,
  instruction_budget,
  retired,
  stopped
};
struct Outcome {
  explicit Outcome(Dependencies dependencies) : stamp(std::move(dependencies)) {}
  Dependencies stamp;
  Completion status = Completion::native_failure;
  std::optional<SceneCandidate> candidate;
  Error native_error{ErrorCode::external_unknown};
  Text<512> diagnostic;
  std::size_t accepted_requests = 0;
  std::array<Text<64>, 8> handlers{};
  std::size_t handler_count = 0;
};
enum class SubmitStatus : std::uint8_t { accepted, busy, full, stopped, invalid };
enum class PollStatus : std::uint8_t { ready, empty, busy, stopped };
struct Options {
  std::size_t heap_bytes = heap_limit;
  std::uint64_t instructions = instruction_limit;
};
class PresentationOwner {
public:
  static Result<std::unique_ptr<PresentationOwner>>
  create(std::shared_ptr<ActionInbox> inbox, Options options = {});
  ~PresentationOwner();
  PresentationOwner(const PresentationOwner &) = delete;
  PresentationOwner &operator=(const PresentationOwner &) = delete;
  SubmitStatus try_compose(ComposeRequest request) noexcept;
  SubmitStatus try_handle(HandlerRequest request) noexcept;
  PollStatus try_take(Outcome &outcome) noexcept;
  void request_stop() noexcept;
  bool quiescent() const noexcept;

private:
  struct State;
  explicit PresentationOwner(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};
// Caller performs any application capture admission before this bounded adoption.
Result<void> publish(const Outcome &outcome, const Dependencies &current,
                     SceneHost &host);
} // namespace blackbird::tui::lua
