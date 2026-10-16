#pragma once
#include "blackbird/raw_provider.hpp"
#include <array>
#include <optional>
#include <string_view>
namespace blackbird {
struct CommandObservationId {
  std::array<char, 32> bytes{};
  std::string_view view() const noexcept { return {bytes.data(), bytes.size()}; }
  bool operator==(const CommandObservationId &) const = default;
};
enum class CommandObservationStatus : std::uint8_t {
  ready,
  busy,
  invalid_identity,
  invalid_range,
  unavailable
};
enum class CommandObservationSelection : std::uint8_t {
  not_requested,
  resident,
  not_resident
};
enum class CommandObservationPhase : std::uint8_t {
  starting,
  running,
  stopped,
  draining,
  settling,
  settled
};
enum class CommandObservationCustody : std::uint8_t {
  not_launched,
  owned,
  reaped,
  unavailable,
  pending
};
struct CommandObservationRow {
  CommandObservationId id;
  CommandObservationPhase phase = CommandObservationPhase::starting;
  CommandObservationCustody custody = CommandObservationCustody::not_launched;
  std::uint64_t admission_order = 0;
  std::size_t received = 0, retained = 0;
  bool pty = false, leader_exited = false, output_closed = false;
  bool worker_finished = false, terminal_retained = false, capture_complete = false;
  bool stop_requested = false, timed_out = false, has_exit = false, sealed = false;
  int exit_code = 0, signal = 0, failure = 0;
  RawEventRef launch{}, terminal{};
};
struct CommandObservationSource {
  RawEventRef original{};
  std::size_t chunk_begin = 0, offset = 0, length = 0;
};
enum class CommandObservationPageEnd : std::uint8_t {
  frontier,
  byte_limit,
  chunk_limit
};
struct CommandObservationWindow {
  std::array<std::byte, 4096> bytes{};
  std::array<CommandObservationSource, 16> sources{};
  std::size_t size = 0, source_count = 0;
  std::size_t requested = 0, begin = 0, next = 0;
  std::size_t cached_begin = 0, retained_end = 0, received_end = 0;
  bool omitted_prefix = false, provenance_complete = true;
  CommandObservationPageEnd end = CommandObservationPageEnd::frontier;
};
struct CommandObservationQuery {
  std::optional<CommandObservationId> after, selected;
  std::size_t row_limit = 8, byte_limit = 4096, chunk_limit = 16, offset = 0;
};
struct CommandObservation {
  CommandObservationStatus status = CommandObservationStatus::unavailable;
  std::array<CommandObservationRow, 8> rows{};
  std::size_t row_count = 0;
  std::optional<CommandObservationId> next;
  bool more_rows = false;
  CommandObservationSelection selection = CommandObservationSelection::not_requested;
  CommandObservationRow detail{};
  CommandObservationWindow window{};
  // Actual bounded query coverage, not a producer-side metric stream.
  std::size_t examined_rows = 0, examined_chunks = 0, seek_steps = 0;
};
} // namespace blackbird
