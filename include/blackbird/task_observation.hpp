#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace blackbird {
struct TaskObservationId {
  std::uint64_t ordinal = 0;
  bool operator==(const TaskObservationId &) const = default;
};
struct TaskObservationCursor {
  std::uint64_t revision = 0;
  std::size_t offset = 0;
  bool operator==(const TaskObservationCursor &) const = default;
};
template <std::size_t Capacity> struct TaskObservationText {
  std::array<char, Capacity> bytes{};
  std::size_t size = 0;
  std::string_view view() const noexcept { return {bytes.data(), size}; }
};
enum class TaskObservationStatus : std::uint8_t {
  ready,
  revision_conflict,
  invalid_identity,
  invalid_range,
  unavailable
};
enum class TaskObservationSelection : std::uint8_t {
  not_requested,
  resident,
  not_resident
};
enum class TaskDeclaredStatus : std::uint8_t { queued, active, blocked, done, dropped };
struct TaskObservationRow {
  TaskObservationId id;
  std::optional<TaskObservationId> parent;
  TaskDeclaredStatus declared_status = TaskDeclaredStatus::queued;
  std::uint64_t version = 0, updated = 0;
  TaskObservationText<512> title;
  TaskObservationText<128> owner;
  std::size_t subtasks = 0, subtasks_done = 0;
};
struct TaskObservationDetail {
  TaskObservationRow row;
  TaskObservationText<2048> note;
  TaskObservationText<512> blocker;
};
struct TaskObservationQuery {
  std::optional<TaskObservationCursor> cursor;
  std::optional<TaskObservationId> selected;
  std::size_t row_limit = 8;
};
struct TaskObservation {
  TaskObservationStatus status = TaskObservationStatus::unavailable;
  std::uint64_t revision = 0;
  std::size_t total = 0;
  // Existing owner populations count top-level declared tasks only.
  std::array<std::size_t, 5> counts{};
  TaskObservationText<512> title;
  TaskObservationText<128> bead;
  std::array<TaskObservationRow, 8> rows{};
  std::size_t row_count = 0;
  std::optional<TaskObservationCursor> next;
  bool more_rows = false;
  TaskObservationSelection selection = TaskObservationSelection::not_requested;
  TaskObservationDetail detail;
  // Metadata projections and actual text copied, not producer metrics.
  std::size_t examined_rows = 0, copied_text_bytes = 0;
};
} // namespace blackbird
