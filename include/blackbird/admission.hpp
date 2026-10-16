#pragma once
#include <cstdint>
#include <optional>
namespace blackbird {
enum class AdmissionResource : std::uint8_t {
  participant_runs,
  participant_concurrency,
  participant_pending_directions,
  participant_direction_receipts,
  program_modules,
  program_encoding,
  task_items,
  task_identifiers,
  task_version
};
const char *admission_resource_name(AdmissionResource resource) noexcept;
struct AdmissionRefusal {
  AdmissionResource resource;
  std::optional<std::uint64_t> count, limit;
};
// Call-local observation issued only by the owner at a proven nonpublishing guard.
// It never classifies a raw/capture failure and grants no retry authority.
class DomainAdmission {
public:
  const std::optional<AdmissionRefusal> &refusal() const noexcept { return refusal_; }

private:
  std::optional<AdmissionRefusal> refusal_;
  void reject(AdmissionResource resource, std::optional<std::uint64_t> count = {},
              std::optional<std::uint64_t> limit = {}) noexcept {
    refusal_ = AdmissionRefusal{resource, count, limit};
  }
  friend class Participants;
  friend class TaskState;
  friend class CodingEngine;
};
} // namespace blackbird
