#include "blackbird/terminal.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

using namespace blackbird;
namespace {
thread_local bool pause_allocation = false;
std::atomic_bool entered = false, released = false;
void *allocate(std::size_t size) {
  if (pause_allocation) {
    pause_allocation = false;
    entered.store(true);
    while (!released.load())
      std::this_thread::yield();
  }
  if (auto *pointer = std::malloc(size ? size : 1))
    return pointer;
  throw std::bad_alloc{};
}
void require(bool good, const char *message) {
  if (!good)
    throw std::runtime_error{message};
}
} // namespace
void *operator new(std::size_t size) { return allocate(size); }
void *operator new[](std::size_t size) { return allocate(size); }
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept {
  ::operator delete[](pointer);
}
int main() {
  try {
    TerminalUI ui{"Publication contention"};
    TaskState tasks;
    ui.tasks(tasks.snapshot());
    auto prepared = tasks.prepare(Value::object(
        {{"op_id", Value{"actual-publication"}},
         {"base", Value{Number{0}}},
         {"ops", Value{Value::Array{Value::object(
                     {{"op", Value{"add"}}, {"title", Value{"Actual resident"}}})}}}}));
    tasks.commit(std::move(prepared));
    const auto snapshot = tasks.snapshot();
    std::exception_ptr failure;
    std::jthread holder{[&] {
      try {
        // With capture disabled, UI ingress makes no allocation before taking
        // its actual task/message mutex. Restore allocates its new resident map.
        pause_allocation = true;
        ui.tasks(snapshot);
      } catch (...) {
        failure = std::current_exception();
      }
    }};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!entered.load() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    const bool paused = entered.load();
    const auto observation = ui.try_observe_tasks({});
    released.store(true);
    holder.join();
    if (failure)
      std::rethrow_exception(failure);
    require(paused && observation.publication == TaskPublicationStatus::busy,
            "actual UI publication guard returns Busy without waiting");
    const auto ready = ui.try_observe_tasks({});
    require(ready.publication == TaskPublicationStatus::ready &&
                ready.value.row_count == 1 && ready.value.revision == 1 &&
                ready.value.rows[0].title.view() == "Actual resident",
            "released actual publication becomes the exact bounded observation");
    std::cout << "PASS task publication contention\n";
  } catch (...) {
    released.store(true);
    std::cerr << "FAIL task publication contention\n";
    return 1;
  }
}
