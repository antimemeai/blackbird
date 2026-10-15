#include "blackbird/workflow_executor.hpp"
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

namespace blackbird {
namespace {
constexpr auto no_slot = std::numeric_limits<std::size_t>::max();

class OwnerOperation {
public:
  explicit OwnerOperation(bool &active) : active_(active) { active_ = true; }
  ~OwnerOperation() { active_ = false; }
  OwnerOperation(const OwnerOperation &) = delete;
  OwnerOperation &operator=(const OwnerOperation &) = delete;

private:
  bool &active_;
};
} // namespace

Result<WorkflowExecutorPayloadRef> WorkflowExecutorPayload::copy(ByteView bytes,
                                                                 std::size_t limit) {
  auto acquired = ByteBuffer::copy(bytes, limit);
  if (!acquired.has_value())
    return Result<WorkflowExecutorPayloadRef>::failure(acquired.error());
  try {
    return Result<WorkflowExecutorPayloadRef>::success(WorkflowExecutorPayloadRef{
        new WorkflowExecutorPayload{std::move(acquired).value()}});
  } catch (const std::bad_alloc &) {
    return Result<WorkflowExecutorPayloadRef>::failure({ErrorCode::allocation});
  }
}

struct WorkflowExecutor::Impl {
  enum class Phase : std::uint8_t { reserved, queued, running, completed };
  struct Task {
    WorkflowExecutorInput input;
    Leaf leaf;
    std::stop_source stop;
    WorkflowExecutorCompletion completion;
    Phase phase = Phase::reserved;

    Task(WorkflowExecutorHandle handle, WorkflowExecutorKey key,
         WorkflowExecutorInput provided, Leaf work)
        : input(std::move(provided)), leaf(std::move(work)),
          completion{handle, key, {}, false, false} {}
  };
  struct Slot {
    std::shared_ptr<Task> task;
    std::uint64_t generation = 1;
    // A slot belongs to exactly one free, ready or completed list.
    std::size_t next = no_slot, previous = no_slot;
  };
  struct Queue {
    std::size_t head = no_slot, tail = no_slot;
  };

  HandleDomain domain;
  std::thread::id owner = std::this_thread::get_id();
  Notify notify;
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::vector<Slot> slots;
  std::vector<std::jthread> workers;
  std::size_t free_head = 0;
  Queue ready, completed;
  WorkflowExecutorState counts;
  bool owner_active = false, stopping = false;

  Impl(HandleDomain identity, WorkflowExecutorLimits limits, Notify wake)
      : domain(identity), notify(std::move(wake)) {
    if (limits.workers == 0 || limits.live_capacity == 0 ||
        limits.workers > limits.live_capacity ||
        limits.live_capacity > slots.max_size() || limits.workers > workers.max_size())
      throw Error{ErrorCode::capacity};
    slots.resize(limits.live_capacity);
    for (std::size_t index = 0; index < slots.size(); ++index)
      slots[index].next = index + 1 < slots.size() ? index + 1 : no_slot;
    workers.reserve(limits.workers);
    try {
      for (std::size_t index = 0; index < limits.workers; ++index)
        workers.emplace_back([this] { work(); });
    } catch (...) {
      stop_all();
      throw;
    }
  }

  bool control_allowed() const noexcept {
    return std::this_thread::get_id() == owner && !owner_active;
  }

  void wake_owner() const noexcept {
    if (!notify)
      return;
    try {
      notify();
    } catch (...) {
      // A failed wake cannot erase/replace the already preserved completion.
    }
  }

  void append(Queue &queue, std::size_t index) noexcept {
    auto &slot = slots[index];
    slot.next = no_slot;
    slot.previous = queue.tail;
    if (queue.tail == no_slot)
      queue.head = index;
    else
      slots[queue.tail].next = index;
    queue.tail = index;
  }

  void remove(Queue &queue, std::size_t index) noexcept {
    auto &slot = slots[index];
    if (slot.previous == no_slot)
      queue.head = slot.next;
    else
      slots[slot.previous].next = slot.next;
    if (slot.next == no_slot)
      queue.tail = slot.previous;
    else
      slots[slot.next].previous = slot.previous;
    slot.next = no_slot;
    slot.previous = no_slot;
  }

  std::shared_ptr<Task> release_slot(std::size_t index) noexcept {
    auto &slot = slots[index];
    auto task = std::move(slot.task);
    --counts.live;
    if (slot.generation == UINT64_MAX) {
      slot.generation = 0;
    } else {
      ++slot.generation;
      slot.next = free_head;
      free_head = index;
    }
    return task;
  }

  Result<std::size_t> validate(const WorkflowExecutorHandle &handle) const {
    if (handle.domain.environment != domain.environment)
      return Result<std::size_t>::failure({ErrorCode::wrong_environment});
    if (handle.domain.incarnation != domain.incarnation)
      return Result<std::size_t>::failure({ErrorCode::wrong_incarnation});
    if (handle.domain.registry != domain.registry)
      return Result<std::size_t>::failure({ErrorCode::wrong_registry});
    if (handle.slot >= slots.size() || !slots[handle.slot].task ||
        slots[handle.slot].generation != handle.generation)
      return Result<std::size_t>::failure({ErrorCode::stale_handle});
    return Result<std::size_t>::success(handle.slot);
  }

  void complete_queued(std::size_t index) noexcept {
    auto &task = *slots[index].task;
    remove(ready, index);
    --counts.queued;
    task.completion.cancellation_requested = true;
    task.completion.result.outcome = WorkflowExecutionOutcome::cancelled;
    task.phase = Phase::completed;
    ++counts.completed;
    append(completed, index);
  }

  void work() {
    for (;;) {
      std::shared_ptr<Task> task;
      std::size_t index = no_slot;
      {
        std::unique_lock lock{mutex};
        changed.wait(lock, [&] { return stopping || ready.head != no_slot; });
        if (stopping)
          return;
        index = ready.head;
        task = slots[index].task;
        remove(ready, index);
        --counts.queued;
        ++counts.running;
        task->phase = Phase::running;
        task->completion.invoked = true;
      }
      WorkflowLeafResult result;
      try {
        result = task->leaf(task->input, task->stop.get_token());
      } catch (...) {
        result.outcome = WorkflowExecutionOutcome::unknown;
        result.exception = std::current_exception();
      }
      {
        std::lock_guard lock{mutex};
        task->completion.result = std::move(result);
        task->phase = Phase::completed;
        --counts.running;
        ++counts.completed;
        append(completed, index);
      }
      wake_owner();
    }
  }

  void stop_all() {
    {
      std::lock_guard lock{mutex};
      stopping = true;
      counts.accepting = false;
    }
    changed.notify_all();
    for (std::size_t index = 0; index < slots.size(); ++index) {
      std::shared_ptr<Task> task;
      bool completed_now = false;
      {
        std::lock_guard lock{mutex};
        const auto &current = slots[index].task;
        if (current &&
            (current->phase == Phase::queued || current->phase == Phase::running)) {
          task = current;
          if (task->phase == Phase::queued) {
            complete_queued(index);
            completed_now = true;
          } else {
            task->completion.cancellation_requested = true;
          }
        }
      }
      // request_stop may run user stop callbacks synchronously. Never hold mutex.
      if (task)
        task->stop.request_stop();
      if (completed_now)
        wake_owner();
    }
    for (auto &worker : workers)
      if (worker.joinable())
        worker.join();
  }
};

WorkflowExecutor::WorkflowExecutor(HandleDomain domain, WorkflowExecutorLimits limits,
                                   Notify notify)
    : impl_(std::make_unique<Impl>(domain, limits, std::move(notify))) {}

WorkflowExecutor::~WorkflowExecutor() { impl_->stop_all(); }

Result<WorkflowExecutorHandle> WorkflowExecutor::submit(WorkflowExecutorKey key,
                                                        WorkflowExecutorInput input,
                                                        Leaf leaf,
                                                        const RetainAdmission &retain) {
  if (!impl_->control_allowed())
    return Result<WorkflowExecutorHandle>::failure({ErrorCode::busy});
  OwnerOperation operation{impl_->owner_active};
  if (!leaf || !retain)
    return Result<WorkflowExecutorHandle>::failure({ErrorCode::invalid_range});

  std::size_t index = no_slot;
  std::uint64_t generation = 0;
  {
    std::lock_guard lock{impl_->mutex};
    if (impl_->stopping)
      return Result<WorkflowExecutorHandle>::failure({ErrorCode::busy});
    for (const auto &slot : impl_->slots)
      if (slot.task && slot.task->completion.key == key)
        return Result<WorkflowExecutorHandle>::failure({ErrorCode::conflict});
    if (impl_->free_head == no_slot)
      return Result<WorkflowExecutorHandle>::failure({ErrorCode::capacity});
    index = impl_->free_head;
    generation = impl_->slots[index].generation;
  }
  const WorkflowExecutorHandle handle{impl_->domain, index, generation};
  std::shared_ptr<Impl::Task> task;
  try {
    task = std::make_shared<Impl::Task>(handle, key, std::move(input), std::move(leaf));
  } catch (const std::bad_alloc &) {
    return Result<WorkflowExecutorHandle>::failure({ErrorCode::allocation});
  }
  {
    std::lock_guard lock{impl_->mutex};
    auto &slot = impl_->slots[index];
    impl_->free_head = slot.next;
    slot.next = no_slot;
    slot.task = task;
    ++impl_->counts.live;
  }
  Result<void> retained = Result<void>::success();
  try {
    retained = retain(handle, key, task->input);
  } catch (...) {
    {
      std::lock_guard lock{impl_->mutex};
      (void)impl_->release_slot(index);
    }
    throw;
  }
  if (!retained.has_value()) {
    {
      std::lock_guard lock{impl_->mutex};
      (void)impl_->release_slot(index);
    }
    return Result<WorkflowExecutorHandle>::failure(retained.error());
  }
  {
    std::lock_guard lock{impl_->mutex};
    task->phase = Impl::Phase::queued;
    impl_->append(impl_->ready, index);
    ++impl_->counts.queued;
  }
  impl_->changed.notify_one();
  return Result<WorkflowExecutorHandle>::success(handle);
}

Result<WorkflowCancelObservation>
WorkflowExecutor::cancel(const WorkflowExecutorHandle &handle) {
  if (!impl_->control_allowed())
    return Result<WorkflowCancelObservation>::failure({ErrorCode::busy});
  OwnerOperation operation{impl_->owner_active};
  std::shared_ptr<Impl::Task> task;
  bool prevented = false;
  {
    std::lock_guard lock{impl_->mutex};
    const auto checked = impl_->validate(handle);
    if (!checked.has_value())
      return Result<WorkflowCancelObservation>::failure(checked.error());
    task = impl_->slots[checked.value()].task;
    if (task->phase == Impl::Phase::completed)
      return Result<WorkflowCancelObservation>::success(
          WorkflowCancelObservation::already_completed);
    if (task->phase == Impl::Phase::queued) {
      impl_->complete_queued(checked.value());
      prevented = true;
    } else {
      task->completion.cancellation_requested = true;
    }
  }
  task->stop.request_stop();
  if (prevented)
    impl_->wake_owner();
  return Result<WorkflowCancelObservation>::success(
      prevented ? WorkflowCancelObservation::prevented_invocation
                : WorkflowCancelObservation::requested_running);
}

Result<std::size_t> WorkflowExecutor::drain(const Publish &publish) {
  if (!impl_->control_allowed())
    return Result<std::size_t>::failure({ErrorCode::busy});
  OwnerOperation operation{impl_->owner_active};
  if (!publish)
    return Result<std::size_t>::failure({ErrorCode::invalid_range});
  std::size_t delivered = 0;
  for (;;) {
    std::shared_ptr<Impl::Task> task;
    std::size_t index = no_slot;
    {
      std::lock_guard lock{impl_->mutex};
      if (impl_->completed.head == no_slot)
        return Result<std::size_t>::success(delivered);
      index = impl_->completed.head;
      task = impl_->slots[index].task;
    }
    const auto published = publish(task->completion);
    if (!published.has_value())
      return Result<std::size_t>::failure(published.error());
    {
      std::lock_guard lock{impl_->mutex};
      impl_->remove(impl_->completed, index);
      --impl_->counts.completed;
      (void)impl_->release_slot(index);
    }
    ++delivered;
  }
}

Result<WorkflowExecutorState> WorkflowExecutor::state() const {
  if (std::this_thread::get_id() != impl_->owner)
    return Result<WorkflowExecutorState>::failure({ErrorCode::busy});
  std::lock_guard lock{impl_->mutex};
  return Result<WorkflowExecutorState>::success(impl_->counts);
}

Result<void> WorkflowExecutor::shutdown() {
  if (!impl_->control_allowed())
    return Result<void>::failure({ErrorCode::busy});
  OwnerOperation operation{impl_->owner_active};
  impl_->stop_all();
  return Result<void>::success();
}
} // namespace blackbird
