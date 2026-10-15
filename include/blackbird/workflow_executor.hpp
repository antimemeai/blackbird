#pragma once

#include "blackbird/foundation.hpp"
#include <exception>
#include <functional>
#include <memory>
#include <stop_token>

namespace blackbird {

struct WorkflowExecutorTaskTag;
using WorkflowExecutorHandle = Handle<WorkflowExecutorTaskTag>;

struct WorkflowExecutorKey {
  InvocationId invocation;
  OperationAttemptId attempt;
  bool operator==(const WorkflowExecutorKey &) const = default;
};

// The factory acquires bytes exactly once. No mutable owner/assignment remains
// available through this type, even when a caller retains a non-const alias.
class WorkflowExecutorPayload final {
public:
  static Result<std::shared_ptr<const WorkflowExecutorPayload>> copy(ByteView bytes,
                                                                     std::size_t limit);
  WorkflowExecutorPayload(const WorkflowExecutorPayload &) = delete;
  WorkflowExecutorPayload &operator=(const WorkflowExecutorPayload &) = delete;
  WorkflowExecutorPayload(WorkflowExecutorPayload &&) = delete;
  WorkflowExecutorPayload &operator=(WorkflowExecutorPayload &&) = delete;
  ByteView view() const & noexcept { return bytes_.view(); }
  ByteView view() const && = delete;
  std::size_t size() const noexcept { return bytes_.size(); }

private:
  explicit WorkflowExecutorPayload(ByteBuffer bytes) : bytes_(std::move(bytes)) {}
  ByteBuffer bytes_;
};
using WorkflowExecutorPayloadRef = std::shared_ptr<const WorkflowExecutorPayload>;

struct WorkflowExecutorInput {
  WorkflowExecutorPayloadRef source;
  WorkflowExecutorPayloadRef context;
  WorkflowExecutorPayloadRef input;
};

enum class WorkflowExecutionOutcome : std::uint8_t {
  success,
  failure,
  cancelled,
  unknown
};

struct WorkflowLeafResult {
  WorkflowExecutionOutcome outcome = WorkflowExecutionOutcome::success;
  WorkflowExecutorPayloadRef output;
  // Thrown values are retained exactly, without formatting on the worker.
  std::exception_ptr exception;
};

struct WorkflowExecutorCompletion {
  WorkflowExecutorHandle handle;
  WorkflowExecutorKey key;
  WorkflowLeafResult result;
  bool invoked = false;
  bool cancellation_requested = false;
};

struct WorkflowExecutorLimits {
  std::size_t workers;
  // Includes queued, running and completed-but-not-yet-acknowledged tasks.
  std::size_t live_capacity;
};

struct WorkflowExecutorState {
  std::size_t live = 0;
  std::size_t queued = 0;
  std::size_t running = 0;
  std::size_t completed = 0;
  bool accepting = true;
};

enum class WorkflowCancelObservation : std::uint8_t {
  prevented_invocation,
  requested_running,
  already_completed
};

// One construction thread owns all public controls. Leaf callables cannot call
// Lua/context/retention or wait for descendant/coordinator work on this pool.
// Workers only execute leaf native work and fill reserved completion slots.
class WorkflowExecutor {
public:
  using Leaf = std::function<WorkflowLeafResult(const WorkflowExecutorInput &,
                                                const std::stop_token &)>;
  using RetainAdmission = std::function<Result<void>(const WorkflowExecutorHandle &,
                                                     const WorkflowExecutorKey &,
                                                     const WorkflowExecutorInput &)>;
  using Publish = std::function<Result<void>(const WorkflowExecutorCompletion &)>;
  // Restricted wake signal only (for example a borrowed nonblocking pipe).
  // Called without the mutex, possibly on a worker; never call Lua/retention.
  // Its borrowed resources must outlive shutdown. Throws leave results intact.
  using Notify = std::function<void()>;

  WorkflowExecutor(HandleDomain domain, WorkflowExecutorLimits limits,
                   Notify notify = {});
  ~WorkflowExecutor();
  WorkflowExecutor(const WorkflowExecutor &) = delete;
  WorkflowExecutor &operator=(const WorkflowExecutor &) = delete;

  // Reserves all pool storage before retain; only its success enables execution.
  // Duplicate live logical keys conflict; durable idempotency belongs to owner.
  Result<WorkflowExecutorHandle> submit(WorkflowExecutorKey key,
                                        WorkflowExecutorInput input, Leaf leaf,
                                        const RetainAdmission &retain);
  Result<WorkflowCancelObservation> cancel(const WorkflowExecutorHandle &handle);
  // Publish runs on the owner without the pool mutex. Failure/throw preserves
  // this completion for retry. Success releases its live capacity/generation.
  Result<std::size_t> drain(const Publish &publish);
  Result<WorkflowExecutorState> state() const;
  // Joins actual worker return; cooperative opaque code can delay this call.
  // Every completion stays drainable after shutdown; no result is overwritten.
  Result<void> shutdown();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace blackbird
