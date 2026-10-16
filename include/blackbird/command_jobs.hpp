#pragma once
#include "blackbird/raw_command.hpp"
#include "blackbird/value.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace blackbird {
class RawRetainedExec;
class RawRetainedJobLoan;
struct JobWaitOptions {
  std::optional<std::uint64_t> yield_ms;
  std::size_t max_output = 16 * 1024 * 1024;
  bool stop_on_cancel = false;
};
struct JobEvent {
  std::string job_id;
  std::shared_ptr<const std::string> bytes;
  std::size_t offset = 0;
  Value result;
  RawEventRef original;
};
struct CommandStartStatus {
  enum class Refusal { none, live_limit, resident_limit };
  Refusal refusal = Refusal::none;
  std::size_t live = 0, resident = 0, max_live = 0, max_records = 0;
  bool worker_started = false;
};
// One process/collector owner per job. Only the engine drains into retained state.
// Thread-safe request/read are the UI mailbox and projection, not engine access.
class CommandJobs {
public:
  CommandJobs();
  ~CommandJobs();
  CommandJobs(const CommandJobs &) = delete;
  CommandJobs &operator=(const CommandJobs &) = delete;
  // IDs come from retained attempt admission, never a recycled resident slot.
  // On pressure only fully settled, unreferenced recent projections retire;
  // original output/attempt history remains the retained owner's responsibility.
  // A throwing call may have published a worker; status distinguishes that
  // boundary and actual registry-limit refusal from raw/admission failures.
  void start(const std::string &id, const Value &arguments,
             RawRetainedExec *admission = nullptr,
             CommandStartStatus *status = nullptr);
  Value wait(const std::string &id, JobWaitOptions options,
             const std::function<void()> &tick, const std::function<bool()> &cancelled);
  Value read(const Value &arguments) const;
  Value control(const Value &arguments);
  void request(Value arguments);
  std::vector<Value> requests();
  std::vector<JobEvent> drain();
  void retained(const std::string &id, std::size_t end);
  std::size_t retained_end(const std::string &id) const;
  void capture_failed(const std::string &id);
  void capture_failed_all();
  // Joins before binding the retained worker producer for one semantic attempt.
  void settlement(const JobEvent &event, const std::function<void()> &append);
  void settlement(const JobEvent &event,
                  const std::function<void(RawRetainedJobLoan *)> &append);
  void settled(const std::string &id, const Value &result);
  bool contains(const std::string &id) const;
  bool active() const;
  std::size_t debt() const;
  Value foreground() const;
  void stop_all() noexcept;
  void shutdown() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace blackbird
