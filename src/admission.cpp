#include "blackbird/admission.hpp"
namespace blackbird {
const char *admission_resource_name(AdmissionResource resource) noexcept {
  switch (resource) {
  case AdmissionResource::participant_runs:
    return "participant_runs";
  case AdmissionResource::participant_concurrency:
    return "participant_concurrency";
  case AdmissionResource::participant_pending_directions:
    return "participant_pending_directions";
  case AdmissionResource::participant_direction_receipts:
    return "participant_direction_receipts";
  case AdmissionResource::program_modules:
    return "program_modules";
  case AdmissionResource::program_encoding:
    return "program_encoding";
  case AdmissionResource::task_items:
    return "task_items";
  case AdmissionResource::task_identifiers:
    return "task_identifiers";
  case AdmissionResource::task_version:
    return "task_version";
  }
  return "invalid_resource";
}
} // namespace blackbird
