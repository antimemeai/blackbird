#pragma once
#include "blackbird/command_observation.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <sys/types.h>
namespace blackbird {
enum class CommandDeliveryStatus : std::uint8_t {
  ready,
  busy,
  invalid_identity,
  scope_mismatch,
  unavailable
};
enum class CommandDeliveryStage : std::uint8_t {
  // Absence includes retirement; it never proves safe-to-repeat.
  job_not_resident,
  key_not_resident,
  reserved,
  selected,
  queued,
  consumed,
  failed
};
enum class CommandDeliveryOperation : std::uint8_t { absent, input, resize, signal };
enum class CommandDeliverySignal : std::uint8_t {
  absent,
  stop,
  resume,
  interrupt,
  terminate,
  kill,
  unsupported
};
enum class CommandDeliveryConsumption : std::uint8_t {
  pending,
  immediate,
  worker,
  abandoned
};
struct CommandDeliveryCall {
  // Existing raw method numbers:1 group kill,2 leader kill,3 getpgid.
  std::uint8_t method = 0;
  pid_t target = 0;
  // errno is the actual captured value, meaningful only when result <0.
  int signal = 0, result = 0, error = 0;
  RawEventRef request{}, returned{};
};
struct CommandDeliveryQuery {
  // Process-local physical owner identity, never a restart/recovery token.
  // Caller separately checks actual environment/session and holds this owner
  // alive for the entire borrowed call; the query does not pin a Job or worker.
  // Zero accepts this live owner's incarnation; nonzero must match exactly.
  std::uint64_t incarnation = 0;
  CommandObservationId job;
  // Borrowed for this call only. Nonempty and at most128 bytes.
  std::string_view request;
};
struct CommandDeliveryObservation {
  CommandDeliveryStatus status = CommandDeliveryStatus::unavailable;
  std::uint64_t incarnation = 0;
  CommandObservationId job;
  std::array<char, 128> request{};
  std::size_t request_size = 0;
  CommandDeliveryStage stage = CommandDeliveryStage::job_not_resident;
  CommandDeliveryOperation operation = CommandDeliveryOperation::absent;
  CommandDeliverySignal signal = CommandDeliverySignal::absent;
  CommandDeliveryConsumption consumption = CommandDeliveryConsumption::pending;
  std::size_t written = 0;
  // Existing delivery bookkeeping, not verified kernel/application success.
  // consumed/worker means queue consumption; calls report actual native returns.
  bool done = false, failed = false;
  // First accepted native refs; duplicates never replace these with retry refs.
  RawEventRef admission{}, enqueue{}, selection{}, control{}, launch{};
  std::array<CommandDeliveryCall, 3> calls{};
  std::size_t call_count = 0;
  // This owner does not receive CodingEngine's retained operation locator.
  bool semantic_link_available = false;
  // Ordered map find calls, not comparator invocations or a work-time metric.
  std::size_t lookup_count = 0;
};
} // namespace blackbird
