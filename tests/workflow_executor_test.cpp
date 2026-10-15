#include "blackbird/workflow_executor.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

using namespace blackbird;
namespace {
void check(bool condition,
           std::source_location where = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error("executor oracle line " + std::to_string(where.line()));
}
template <typename T> T take(Result<T> result) {
  check(result.has_value());
  return std::move(result).value();
}
void good(Result<void> result) { check(result.has_value()); }
template <typename T> T identity(unsigned char number) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{number};
  return take(T::from_bytes(bytes));
}
HandleDomain domain() {
  return {identity<EnvironmentId>(1), identity<ProcessIncarnationId>(2),
          identity<RegistryId>(3)};
}
WorkflowExecutorKey key(unsigned char number) {
  return {identity<InvocationId>(number), identity<OperationAttemptId>(number)};
}
WorkflowExecutorPayloadRef bytes(std::string_view text) {
  return take(WorkflowExecutorPayload::copy(
      std::as_bytes(std::span{text.data(), text.size()}), text.size()));
}
WorkflowExecutorInput input() {
  return {bytes("source"), bytes("context"), bytes("input")};
}
WorkflowExecutor::RetainAdmission admission(std::thread::id owner) {
  return [owner](const WorkflowExecutorHandle &, const WorkflowExecutorKey &,
                 const WorkflowExecutorInput &) {
    check(std::this_thread::get_id() == owner);
    return Result<void>::success();
  };
}
struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  std::size_t entered = 0, active = 0, peak = 0;
  bool released = false;
  void enter() {
    std::unique_lock lock{mutex};
    ++entered;
    ++active;
    peak = std::max(peak, active);
    changed.notify_all();
    check(changed.wait_for(lock, std::chrono::seconds{4}, [&] { return released; }));
    --active;
  }
  void wait(std::size_t count) {
    std::unique_lock lock{mutex};
    check(changed.wait_for(lock, std::chrono::seconds{2},
                           [&] { return entered == count; }));
  }
  void release() {
    {
      std::lock_guard lock{mutex};
      released = true;
    }
    changed.notify_all();
  }
};
void completed(WorkflowExecutor &pool, std::size_t count) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
  while (take(pool.state()).completed != count) {
    check(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
}

void concurrency_and_cancellation() {
  const auto owner = std::this_thread::get_id();
  Gate gate;
  std::atomic_size_t admitted{0}, invoked{0};
  WorkflowExecutor pool{domain(), {2, 3}};
  const auto output = bytes(std::string_view{"exact\0bytes", 11});
  const auto retain = [&](const WorkflowExecutorHandle &, const WorkflowExecutorKey &,
                          const WorkflowExecutorInput &provided) {
    check(std::this_thread::get_id() == owner);
    check(provided.source && provided.context && provided.input);
    admitted.fetch_add(1);
    return Result<void>::success();
  };
  const auto leaf = [&](const WorkflowExecutorInput &, const std::stop_token &) {
    check(std::this_thread::get_id() != owner);
    check(admitted.load() >= 1);
    invoked.fetch_add(1);
    gate.enter();
    return WorkflowLeafResult{WorkflowExecutionOutcome::success, output, {}};
  };
  const auto first = take(pool.submit(key(10), input(), leaf, retain));
  const auto second = take(pool.submit(key(11), input(), leaf, retain));
  gate.wait(2);
  const auto third = take(pool.submit(key(12), input(), leaf, retain));
  const auto initial = take(pool.state());
  check(initial.live == 3 && initial.running == 2 && initial.queued == 1);
  const auto excess = pool.submit(key(13), input(), leaf, retain);
  check(!excess.has_value() && excess.error().code == ErrorCode::capacity);
  check(admitted.load() == 3);
  check(take(pool.cancel(third)) == WorkflowCancelObservation::prevented_invocation);
  check(take(pool.cancel(first)) == WorkflowCancelObservation::requested_running);
  auto wrong_incarnation = second;
  wrong_incarnation.domain.incarnation = identity<ProcessIncarnationId>(99);
  const auto rejected = pool.cancel(wrong_incarnation);
  check(!rejected.has_value() && rejected.error().code == ErrorCode::wrong_incarnation);
  gate.release();
  good(pool.shutdown());
  check(invoked.load() == 2 && gate.peak == 2 && gate.active == 0);
  std::size_t observed = 0;
  check(take(pool.drain([&](const WorkflowExecutorCompletion &completion) {
          check(std::this_thread::get_id() == owner);
          ++observed;
          if (completion.handle == third) {
            check(!completion.invoked && completion.cancellation_requested &&
                  completion.result.outcome == WorkflowExecutionOutcome::cancelled &&
                  !completion.result.output);
          } else {
            check(completion.invoked &&
                  completion.result.outcome == WorkflowExecutionOutcome::success &&
                  completion.result.output == output);
            if (completion.handle == first)
              check(completion.cancellation_requested);
          }
          return Result<void>::success();
        })) == 3);
  const auto final = take(pool.state());
  check(observed == 3 && final.live == 0 && final.running == 0 &&
        final.completed == 0 && !final.accepting);
}

void admission_and_completion_retries() {
  const auto owner = std::this_thread::get_id();
  WorkflowExecutor pool{domain(), {1, 1}};
  std::atomic_size_t calls{0};
  const auto original_error =
      std::make_exception_ptr(std::runtime_error{"retained exact failure"});
  const auto output = bytes("partial output survives failure");
  const auto leaf = [&](const WorkflowExecutorInput &, const std::stop_token &) {
    calls.fetch_add(1);
    return WorkflowLeafResult{WorkflowExecutionOutcome::failure, output,
                              original_error};
  };
  const auto refused =
      pool.submit(key(20), input(), leaf,
                  [](const WorkflowExecutorHandle &, const WorkflowExecutorKey &,
                     const WorkflowExecutorInput &) {
                    return Result<void>::failure({ErrorCode::io, 71});
                  });
  check(!refused.has_value() && refused.error() == Error{ErrorCode::io, 71});
  check(take(pool.state()).live == 0 && calls.load() == 0);
  bool admission_threw = false;
  try {
    (void)pool.submit(key(20), input(), leaf,
                      [](const WorkflowExecutorHandle &, const WorkflowExecutorKey &,
                         const WorkflowExecutorInput &) -> Result<void> {
                        throw std::runtime_error{"admission failed"};
                      });
  } catch (const std::runtime_error &) {
    admission_threw = true;
  }
  check(admission_threw && take(pool.state()).live == 0 && calls.load() == 0);
  const auto first = take(pool.submit(key(20), input(), leaf, admission(owner)));
  completed(pool, 1);
  const auto publication_failed = pool.drain([](const WorkflowExecutorCompletion &) {
    return Result<void>::failure({ErrorCode::io, 72});
  });
  check(!publication_failed.has_value() &&
        publication_failed.error() == Error{ErrorCode::io, 72});
  bool publication_threw = false;
  try {
    (void)pool.drain([](const WorkflowExecutorCompletion &) -> Result<void> {
      throw std::runtime_error{"publication failed"};
    });
  } catch (const std::runtime_error &) {
    publication_threw = true;
  }
  check(publication_threw && take(pool.state()).completed == 1);
  const auto full = pool.submit(key(21), input(), leaf, admission(owner));
  check(!full.has_value() && full.error().code == ErrorCode::capacity);
  check(take(pool.cancel(first)) == WorkflowCancelObservation::already_completed);
  check(take(pool.drain([&](const WorkflowExecutorCompletion &completion) {
          check(completion.handle == first && completion.invoked &&
                !completion.cancellation_requested &&
                completion.result.outcome == WorkflowExecutionOutcome::failure &&
                completion.result.output == output &&
                completion.result.exception == original_error);
          return Result<void>::success();
        })) == 1);

  Gate gate;
  bool late_stop = true;
  const auto second = take(pool.submit(
      key(21), input(),
      [&](const WorkflowExecutorInput &, const std::stop_token &stop) {
        gate.enter();
        late_stop = stop.stop_requested();
        return WorkflowLeafResult{};
      },
      admission(owner)));
  gate.wait(1);
  check(second.slot == first.slot && second.generation != first.generation);
  const auto stale = pool.cancel(first);
  check(!stale.has_value() && stale.error().code == ErrorCode::stale_handle);
  gate.release();
  completed(pool, 1);
  check(!late_stop);
  check(take(pool.drain([](const WorkflowExecutorCompletion &completion) {
          check(completion.invoked && !completion.cancellation_requested);
          return Result<void>::success();
        })) == 1);
  good(pool.shutdown());
  check(calls.load() == 1);
}

void owner_boundary_and_exact_exception() {
  const auto owner = std::this_thread::get_id();
  WorkflowExecutor pool{domain(), {1, 2}};
  const auto retained = admission(owner);
  std::atomic_bool reentry_rejected{false};
  const auto first = take(pool.submit(
      key(30), input(),
      [&](const WorkflowExecutorInput &,
          const std::stop_token &) -> WorkflowLeafResult {
        const auto denied = pool.state();
        reentry_rejected.store(!denied.has_value() &&
                               denied.error().code == ErrorCode::busy);
        throw std::string{"opaque thrown value"};
      },
      [&](const WorkflowExecutorHandle &, const WorkflowExecutorKey &,
          const WorkflowExecutorInput &) {
        const auto denied = pool.shutdown();
        check(!denied.has_value() && denied.error().code == ErrorCode::busy);
        return Result<void>::success();
      }));
  completed(pool, 1);
  bool denied_all = false;
  std::jthread other{[&] {
    const auto submitted = pool.submit(
        key(31), input(),
        [](const WorkflowExecutorInput &, const std::stop_token &) {
          return WorkflowLeafResult{};
        },
        retained);
    const auto cancelled = pool.cancel(first);
    const auto drained = pool.drain([](const WorkflowExecutorCompletion &) {
      throw std::runtime_error{"must not publish off owner"};
      return Result<void>::success();
    });
    const auto stopped = pool.shutdown();
    denied_all = !submitted.has_value() && !cancelled.has_value() &&
                 !drained.has_value() && !stopped.has_value();
  }};
  other.join();
  check(denied_all && reentry_rejected.load());
  good(pool.shutdown());
  check(take(pool.drain([&](const WorkflowExecutorCompletion &completion) {
          check(std::this_thread::get_id() == owner && completion.invoked &&
                completion.result.outcome == WorkflowExecutionOutcome::unknown &&
                completion.result.exception);
          bool exact = false;
          try {
            std::rethrow_exception(completion.result.exception);
          } catch (const std::string &text) {
            exact = text == "opaque thrown value";
          }
          check(exact);
          return Result<void>::success();
        })) == 1);
}

void shutdown_requests_and_joins() {
  WorkflowExecutor pool{domain(), {1, 2}};
  const auto owner = std::this_thread::get_id();
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, left = false, stop_callback_observed = false;
  const auto output = bytes("unknown external effect");
  const auto first = take(pool.submit(
      key(40), input(),
      [&](const WorkflowExecutorInput &, const std::stop_token &stop) {
        std::stop_callback notify{stop, [&] {
                                    // Cancellation invokes this on the owner. A
                                    // held executor mutex would deadlock state().
                                    check(take(pool.state()).live == 2);
                                    stop_callback_observed = true;
                                    changed.notify_all();
                                  }};
        std::unique_lock lock{mutex};
        entered = true;
        changed.notify_all();
        check(changed.wait_for(lock, std::chrono::seconds{4},
                               [&] { return stop.stop_requested(); }));
        left = true;
        return WorkflowLeafResult{WorkflowExecutionOutcome::unknown, output, {}};
      },
      admission(owner)));
  {
    std::unique_lock lock{mutex};
    check(changed.wait_for(lock, std::chrono::seconds{2}, [&] { return entered; }));
  }
  const auto second = take(pool.submit(
      key(41), input(),
      [](const WorkflowExecutorInput &, const std::stop_token &) -> WorkflowLeafResult {
        throw std::runtime_error{"queued leaf must not start during shutdown"};
      },
      admission(owner)));
  good(pool.shutdown());
  check(left && stop_callback_observed);
  check(take(pool.state()).running == 0 && take(pool.state()).completed == 2);
  check(take(pool.drain([&](const WorkflowExecutorCompletion &completion) {
          if (completion.handle == first)
            check(completion.invoked && completion.cancellation_requested &&
                  completion.result.outcome == WorkflowExecutionOutcome::unknown &&
                  completion.result.output == output);
          else
            check(completion.handle == second && !completion.invoked &&
                  completion.result.outcome == WorkflowExecutionOutcome::cancelled);
          return Result<void>::success();
        })) == 2);
  good(pool.shutdown());
}

void failed_wake_preserves_completion() {
  const auto owner = std::this_thread::get_id();
  std::atomic_size_t wakes{0};
  std::atomic_bool worker_woke{false};
  WorkflowExecutor pool{domain(), {1, 1}, [&] {
                          worker_woke.store(std::this_thread::get_id() != owner);
                          wakes.fetch_add(1);
                          throw 77;
                        }};
  const auto output = bytes("completion survives failed wake");
  (void)take(pool.submit(
      key(50), input(),
      [&](const WorkflowExecutorInput &, const std::stop_token &) {
        return WorkflowLeafResult{WorkflowExecutionOutcome::success, output, {}};
      },
      admission(owner)));
  completed(pool, 1);
  good(pool.shutdown());
  check(wakes.load() == 1 && worker_woke.load());
  check(take(pool.drain([&](const WorkflowExecutorCompletion &completion) {
          check(std::this_thread::get_id() == owner && completion.invoked &&
                completion.result.outcome == WorkflowExecutionOutcome::success &&
                completion.result.output == output && !completion.result.exception);
          return Result<void>::success();
        })) == 1);
}

void payload_freezes_external_bytes() {
  static_assert(!std::is_copy_constructible_v<WorkflowExecutorPayload>);
  static_assert(!std::is_move_constructible_v<WorkflowExecutorPayload>);
  static_assert(!std::is_copy_assignable_v<WorkflowExecutorPayload>);
  static_assert(!std::is_move_assignable_v<WorkflowExecutorPayload>);
  static_assert(!std::is_constructible_v<WorkflowExecutorPayload, ByteBuffer>);
  auto external = std::string{"retained source"};
  const auto frozen = bytes(external);
  external.assign(external.size(), 'x');
  const auto expected = std::string_view{"retained source"};
  check(std::ranges::equal(frozen->view(),
                           std::as_bytes(std::span{expected.data(), expected.size()})));
  const auto refused =
      WorkflowExecutorPayload::copy(frozen->view(), frozen->size() - 1);
  check(!refused.has_value());
}
} // namespace

int main() {
  try {
    concurrency_and_cancellation();
    admission_and_completion_retries();
    owner_boundary_and_exact_exception();
    shutdown_requests_and_joins();
    failed_wake_preserves_completion();
    payload_freezes_external_bytes();
    std::cout << "workflow executor oracles passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
