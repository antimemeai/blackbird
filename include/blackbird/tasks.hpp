#pragma once
#include "blackbird/context.hpp"
#include "blackbird/raw_tasks.hpp"
#include <array>
#include <deque>
#include <set>

namespace blackbird {
struct RawTaskPresentation {
  std::optional<RawEventRef> source;
  std::optional<ApplicationRecordId> fact;
  std::optional<std::uint64_t> revision;
};
// Current task projection only; history belongs to the retained journal.
class TaskState {
public:
  static constexpr std::size_t max_items = 2048;
  static constexpr std::size_t max_batch = 64;
  static constexpr std::size_t max_page = 64;
  static constexpr std::size_t retry_window = 16;
  struct Prepared {
    std::map<std::string, Value> changed;
    std::vector<std::string> removed;
    std::optional<std::vector<std::string>> order;
    std::deque<Value> receipts;
    std::array<std::size_t, 5> counts{};
    std::map<std::string, std::array<std::size_t, 2>> rollups;
    std::uint64_t revision = 0, next = 1;
    std::string title, bead;
    Value packet, result;
    std::optional<RawEventRef> raw_prepare;
  };
  Prepared prepare(const Value &request, RawTaskOrigin origin = {}) const;
  void commit(Prepared &&prepared, RawTaskOrigin origin = {});
  void replay(const Value &packet, RawTaskOrigin origin = {});
  void restore(const Value &snapshot, RawTaskOrigin origin = {});
  Value snapshot(RawTaskOrigin origin = {}, RawEventRef *completion = nullptr) const;
  Value read(const Value &query = Value::object({}), RawTaskOrigin origin = {}) const;
  const Value *item(std::string_view id) const;
  std::uint64_t revision() const noexcept { return revision_; }
  bool empty() const noexcept { return items_.empty(); }
  std::size_t size() const noexcept { return items_.size(); }
  std::optional<std::size_t> position(std::string_view id) const;

private:
  std::map<std::string, Value> items_;
  std::vector<std::string> order_;
  std::deque<Value> receipts_;
  std::array<std::size_t, 5> counts_{};
  std::map<std::string, std::array<std::size_t, 2>> rollups_;
  std::uint64_t revision_ = 0, next_ = 1;
  std::string title_ = "Work", bead_;
};

class TaskStore {
public:
  explicit TaskStore(AuditLog &log);
  Value read(const Value &query = Value::object({}), RawTaskOrigin origin = {}) const {
    return state_.read(query, origin);
  }
  Value edit(const Value &request, RawTaskOrigin origin = {});
  Value operator_command(std::string_view command);
  Value snapshot(RawEventRef *completion = nullptr) const {
    return state_.snapshot({}, completion);
  }
  RawTaskPresentation presentation_origin() const noexcept {
    return active_presentation_ ? *active_presentation_ : RawTaskPresentation{};
  }
  const TaskState &state() const noexcept { return state_; }
  std::function<void(const Value &)> changed;

private:
  AuditLog &log_;
  TaskState state_;
  const RawTaskPresentation *active_presentation_ = nullptr;
};
} // namespace blackbird
