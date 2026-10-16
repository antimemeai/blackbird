#pragma once
#include "blackbird/context.hpp"
#include "blackbird/provider_original.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
namespace blackbird {
class Participants;
struct ParticipantObservationId {
  std::array<char, 128> bytes{};
  std::size_t size = 0;
  std::string_view view() const noexcept { return {bytes.data(), size}; }
  bool operator==(const ParticipantObservationId &) const = default;
};
struct ParticipantObservationText {
  std::array<char, 256> bytes{};
  std::size_t size = 0, total = 0;
  bool present = false, truncated = false;
  std::string_view view() const noexcept { return {bytes.data(), size}; }
};
enum class ParticipantObservationStatus : std::uint8_t {
  ready,
  busy,
  invalid_identity,
  invalid_range,
  unavailable,
  retired,
  scope_mismatch
};
enum class ParticipantObservationSelection : std::uint8_t {
  not_requested,
  resident,
  not_resident
};
enum class ParticipantObservationPhase : std::uint8_t {
  running,
  waiting,
  completed,
  cancelled,
  failed,
  unknown
};
struct ParticipantObservationRow {
  ParticipantObservationId id;
  ParticipantObservationText task_id, provider, model, from, to;
  ParticipantObservationPhase phase = ParticipantObservationPhase::unknown;
  std::size_t queued = 0, accepted = 0, dropped = 0, pending_captures = 0;
  bool stop_requested = false, sealed = false, business_finished = false;
  bool worker_started = false, worker_ended = false, worker_joined = false;
  bool cleanup_closed = false, restored = false, reopened = false;
  RawEventRef admission{}, dispatch{}, end{};
};
struct ParticipantObservationDetail {
  ParticipantObservationText initial_request, current_request, last_answered_request;
  ParticipantObservationText result_status, remote_disposition;
  std::optional<ProviderOriginalReference> initial_original, current_original,
      result_original;
  std::optional<SourceReference> initial_retained, current_retained, result_retained;
  bool result_available = false, projection_known = false, projection_truncated = false;
};
struct ParticipantObservationWindow {
  std::array<std::byte, 4096> bytes{};
  std::size_t size = 0, requested = 0, begin = 0, next = 0, total = 0;
  bool more = false;
};
struct ParticipantObservationQuery {
  // Zero accepts this reader's live incarnation. Nonzero must match exactly.
  std::uint64_t incarnation = 0;
  std::optional<ParticipantObservationId> after, selected;
  std::size_t row_limit = 8, byte_limit = 4096, offset = 0;
};
struct ParticipantObservation {
  ParticipantObservationStatus status = ParticipantObservationStatus::unavailable;
  std::uint64_t incarnation = 0;
  std::array<ParticipantObservationRow, 8> rows{};
  std::size_t row_count = 0;
  std::optional<ParticipantObservationId> next;
  bool more_rows = false;
  ParticipantObservationSelection selection =
      ParticipantObservationSelection::not_requested;
  ParticipantObservationRow detail{};
  ParticipantObservationDetail result{};
  ParticipantObservationWindow window{};
  // seek_steps counts ordered map seeks, not hidden comparator invocations.
  std::size_t examined_rows = 0, examined_fields = 0, seek_steps = 0, copied_bytes = 0;
};
class ParticipantObservationReader {
public:
  ParticipantObservationReader() = default;
  ParticipantObservation
  try_observe(const ParticipantObservationQuery &query = {}) const noexcept;

private:
  struct State;
  std::shared_ptr<State> state_;
  explicit ParticipantObservationReader(std::shared_ptr<State> state)
      : state_(std::move(state)) {}
  friend class Participants;
};
} // namespace blackbird
