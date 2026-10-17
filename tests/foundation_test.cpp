#include "blackbird/foundation.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
std::atomic<bool> fail_allocation{false};
}

// Actual allocator failure at the next ordinary C++ allocation, confined to tests.
void *operator new(std::size_t size) {
  if (fail_allocation.exchange(false)) {
    throw std::bad_alloc{};
  }
  if (void *pointer = std::malloc(size == 0 ? 1 : size)) {
    return pointer;
  }
  throw std::bad_alloc{};
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept {
  ::operator delete[](pointer);
}

namespace {
using namespace blackbird;

void check(bool condition, const char *expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "foundation_test.cpp:%d: %s\n", line, expression);
    throw std::runtime_error{"direct assertion failed"};
  }
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

template <typename T> T require(Result<T> result) {
  CHECK(result.has_value());
  return std::move(result).value();
}
void require(Result<void> result) { CHECK(result.has_value()); }
template <typename T> void error_is(const Result<T> &result, ErrorCode expected) {
  CHECK(!result.has_value());
  CHECK(result.error().code == expected);
}
template <typename T> T identity(unsigned char seed) {
  IdentityBytes bytes{};
  bytes[15] = std::byte{seed};
  return require(T::from_bytes(bytes));
}
HandleDomain handle_domain(unsigned char registry) {
  return {identity<EnvironmentId>(1), identity<ProcessIncarnationId>(2),
          identity<RegistryId>(registry)};
}
ClockDomain clock_domain(unsigned char clock) {
  return {identity<EnvironmentId>(1), identity<ProcessIncarnationId>(2),
          identity<ClockId>(clock)};
}
struct ProcessHandleTag {};

void identities() {
  static_assert(!std::is_convertible_v<ParticipantId, ConversationId>);
  static_assert(!std::is_convertible_v<ParticipantId, IdentityBytes>);
  static_assert(!std::is_convertible_v<IdentityBytes, ParticipantId>);
  static_assert(!std::is_convertible_v<ParticipantId, bool>);
  static_assert(!std::is_convertible_v<MonotonicInstant, std::int64_t>);
  static_assert(!std::is_convertible_v<std::int64_t, MonotonicInstant>);
  static_assert(!std::is_default_constructible_v<EnvironmentId>);
  IdentityBytes known{};
  known[0] = std::byte{255};
  known[7] = std::byte{13};
  known[15] = std::byte{42};
  const auto value = require(ParticipantId::from_bytes(known));
  CHECK(value.bytes() == known);
  CHECK(value == require(ParticipantId::from_bytes(known)));
  CHECK(value != identity<ParticipantId>(42));
  error_is(ParticipantId::from_bytes({}), ErrorCode::invalid_identity);
  error_is(AuditSequence::from(0), ErrorCode::invalid_identity);
  CHECK(require(require(AuditSequence::from(8)).next()).value() == 9);
  error_is(require(AuditSequence::from(UINT64_MAX)).next(), ErrorCode::overflow);
}

void handles() {
  auto registry =
      require(HandleRegistry<ProcessHandleTag>::create(handle_domain(3), 1));
  const auto first = require(registry.acquire());
  CHECK(first.slot == 0 && first.generation == 1);
  CHECK(require(registry.validate(first)) == 0);
  error_is(registry.acquire(), ErrorCode::capacity);
  auto foreign = first;
  foreign.domain.environment = identity<EnvironmentId>(9);
  error_is(registry.validate(foreign), ErrorCode::wrong_environment);
  error_is(registry.release(foreign), ErrorCode::wrong_environment);
  CHECK(require(registry.validate(first)) == 0);
  foreign = first;
  foreign.domain.incarnation = identity<ProcessIncarnationId>(9);
  error_is(registry.validate(foreign), ErrorCode::wrong_incarnation);
  foreign = first;
  foreign.domain.registry = identity<RegistryId>(9);
  error_is(registry.validate(foreign), ErrorCode::wrong_registry);
  foreign = first;
  foreign.slot = SIZE_MAX;
  error_is(registry.validate(foreign), ErrorCode::stale_handle);
  error_is(registry.release(foreign), ErrorCode::stale_handle);
  foreign.slot = 1;
  error_is(registry.validate(foreign), ErrorCode::stale_handle);
  foreign = first;
  foreign.generation = 0;
  error_is(registry.release(foreign), ErrorCode::stale_handle);
  CHECK(require(registry.validate(first)) == 0);
  auto other = require(HandleRegistry<ProcessHandleTag>::create(handle_domain(4), 1));
  const auto other_handle = require(other.acquire());
  error_is(registry.validate(other_handle), ErrorCode::wrong_registry);
  error_is(other.validate(first), ErrorCode::wrong_registry);
  require(registry.release(first));
  error_is(registry.validate(first), ErrorCode::stale_handle);
  const auto second = require(registry.acquire());
  CHECK(second.slot == first.slot);
  CHECK(second.generation == first.generation + 1);
  error_is(registry.release(first), ErrorCode::stale_handle);
  CHECK(require(registry.validate(second)) == 0);
  auto moved = std::move(registry);
  CHECK(require(moved.validate(second)) == 0);
  // Deliberately test the documented rejection by a moved-from registry.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  error_is(registry.validate(second), ErrorCode::stale_registry);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  error_is(registry.acquire(), ErrorCode::stale_registry);
  auto exhausted = require(
      HandleRegistry<ProcessHandleTag>::create(handle_domain(5), 1, UINT64_MAX));
  const auto last = require(exhausted.acquire());
  require(exhausted.release(last));
  error_is(exhausted.acquire(), ErrorCode::capacity);
  error_is(exhausted.validate(last), ErrorCode::stale_handle);
  error_is(HandleRegistry<ProcessHandleTag>::create(handle_domain(6), 0),
           ErrorCode::capacity);
  error_is(HandleRegistry<ProcessHandleTag>::create(handle_domain(6), 1, 0),
           ErrorCode::invalid_identity);
  require(moved.release(second));
  for (unsigned int round = 0; round < 200; ++round) {
    const auto current = require(moved.acquire());
    CHECK(current.slot == 0 &&
          current.generation == static_cast<std::uint64_t>(round) + 3);
    require(moved.release(current));
    error_is(moved.validate(current), ErrorCode::stale_handle);
  }
}

void byte_values() {
  std::array<std::byte, 4> source{std::byte{0}, std::byte{255}, std::byte{13},
                                  std::byte{10}};
  auto buffer = require(ByteBuffer::copy(source, 4));
  source[1] = std::byte{1};
  CHECK(buffer.view().size() == 4);
  CHECK(buffer.view()[0] == std::byte{0});
  CHECK(buffer.view()[1] == std::byte{255});
  CHECK(buffer.view()[2] == std::byte{13});
  CHECK(buffer.view()[3] == std::byte{10});
  CHECK(require(slice(buffer.view(), 1, 2))[1] == std::byte{13});
  CHECK(require(slice(buffer.view(), 4, 0)).empty());
  CHECK(require(ByteBuffer::copy({}, 0)).size() == 0);
  error_is(ByteBuffer::copy(source, 3), ErrorCode::capacity);
  error_is(slice(buffer.view(), SIZE_MAX, 1), ErrorCode::invalid_range);
  error_is(slice(buffer.view(), 1, SIZE_MAX), ErrorCode::invalid_range);
  for (std::size_t offset = 0; offset < 7; ++offset) {
    for (std::size_t length = 0; length < 7; ++length) {
      const auto result = slice(buffer.view(), offset, length);
      const bool permitted = offset <= 4 && length <= 4 - offset;
      CHECK(result.has_value() == permitted);
      if (permitted) {
        CHECK(result.value().size() == length);
        for (std::size_t index = 0; index < length; ++index) {
          CHECK(result.value()[index] == buffer.view()[offset + index]);
        }
      } else {
        CHECK(result.error().code == ErrorCode::invalid_range);
      }
    }
  }
  fail_allocation.store(true);
  const auto failed_copy = ByteBuffer::copy(source, 4);
  CHECK(!fail_allocation.load());
  error_is(failed_copy, ErrorCode::allocation);
  fail_allocation.store(true);
  const auto failed_registry =
      HandleRegistry<ProcessHandleTag>::create(handle_domain(9), 4);
  CHECK(!fail_allocation.load());
  error_is(failed_registry, ErrorCode::allocation);
}

void time_values() {
  const auto domain = clock_domain(3);
  const MonotonicInstant start{domain, 100};
  CHECK(require(elapsed(start, {domain, 125})).nanoseconds == 25);
  error_is(elapsed(start, {clock_domain(4), 125}), ErrorCode::wrong_clock);
  auto restarted = domain;
  restarted.incarnation = identity<ProcessIncarnationId>(8);
  error_is(elapsed(start, {restarted, 125}), ErrorCode::wrong_clock);
  error_is(elapsed(start, {domain, 99}), ErrorCode::clock_regressed);
  CHECK(require(elapsed({domain, INT64_MIN}, {domain, INT64_MAX})).nanoseconds ==
        UINT64_MAX);
  const auto deadline = require(Deadline::after(start, Duration{25}));
  CHECK(!require(deadline.due({domain, 124})));
  CHECK(require(deadline.due({domain, 125})));
  CHECK(require(deadline.due({domain, 126})));
  error_is(deadline.due({domain, 99}), ErrorCode::clock_regressed);
  error_is(deadline.due({clock_domain(5), 126}), ErrorCode::wrong_clock);
  CHECK(require(require(Deadline::after(start, Duration{0})).due(start)));
  error_is(Deadline::after({domain, INT64_MAX}, Duration{1}), ErrorCode::overflow);
  CHECK(require(Deadline::after({domain, INT64_MIN}, Duration{UINT64_MAX}))
            .instant()
            .nanoseconds == INT64_MAX);
  CHECK(require(Deadline::after({domain, -10}, Duration{9})).instant().nanoseconds ==
        -1);
  CHECK(require(Deadline::after({domain, -10}, Duration{10})).instant().nanoseconds ==
        0);
}

class ScriptedClock final : public ClockSource {
public:
  explicit ScriptedClock(ClockDomain domain) : domain_(domain) {}
  Result<ClockSample> observe() override {
    static constexpr std::array<std::int64_t, 3> walls{1000, -9000, 50000};
    static constexpr std::array<std::int64_t, 3> ticks{100, 110, 125};
    const auto index = next_++;
    if (index >= walls.size()) {
      return Result<ClockSample>::failure({ErrorCode::unsupported});
    }
    return ClockSample::from({domain_, walls[index]}, {domain_, ticks[index]});
  }

private:
  ClockDomain domain_;
  std::size_t next_ = 0;
};

void clocks() {
  const auto domain = clock_domain(3);
  ScriptedClock source{domain};
  const auto first = require(source.observe());
  const auto middle = require(source.observe());
  const auto last = require(source.observe());
  CHECK(middle.wall.nanoseconds == -9000);
  CHECK(last.wall.nanoseconds == 50000);
  CHECK(require(elapsed(first.monotonic, last.monotonic)).nanoseconds == 25);
  const auto deadline = require(Deadline::after(first.monotonic, Duration{25}));
  CHECK(!require(deadline.due(middle.monotonic)));
  CHECK(require(deadline.due(last.monotonic)));
  error_is(ClockSample::from({domain, 0}, {clock_domain(4), 0}),
           ErrorCode::wrong_clock);
  NativeClock native{domain};
  for (int sample = 0; sample < 3; ++sample) {
    const auto wall_before = std::chrono::system_clock::now();
    const auto mono_before = std::chrono::steady_clock::now();
    const auto observed = require(native.observe());
    const auto mono_after = std::chrono::steady_clock::now();
    const auto wall_after = std::chrono::system_clock::now();
    CHECK(observed.monotonic.domain == domain);
    CHECK(observed.wall.domain == domain);
    CHECK(std::chrono::duration_cast<std::chrono::nanoseconds>(
              mono_before.time_since_epoch())
              .count() <= observed.monotonic.nanoseconds);
    CHECK(observed.monotonic.nanoseconds <=
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              mono_after.time_since_epoch())
              .count());
    const auto before_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               wall_before.time_since_epoch())
                               .count();
    const auto after_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              wall_after.time_since_epoch())
                              .count();
    if (after_ns < before_ns || observed.wall.nanoseconds < before_ns ||
        observed.wall.nanoseconds > after_ns) {
      throw std::runtime_error{"inconclusive native wall bracket: clock step or "
                               "incorrect clock wiring; qualification not passed"};
    }
    volatile std::uint64_t work = 0;
    for (std::uint64_t i = 0; i < 10000; ++i) {
      work = work + i;
    }
    CHECK(work == 49995000);
  }
}

class ShortStorage final : public Storage {
public:
  Result<std::size_t> read_at(std::uint64_t offset, MutableByteView output) override {
    if (offset != 7 || output.size() < 2) {
      return Result<std::size_t>::failure({ErrorCode::invalid_range});
    }
    output[0] = std::byte{0};
    output[1] = std::byte{255};
    return Result<std::size_t>::success(2);
  }
  Result<std::size_t> write_at(std::uint64_t offset, ByteView input) override {
    CHECK(offset == 7);
    CHECK(input.size() == 4);
    return Result<std::size_t>::success(2);
  }
  Result<std::uint64_t> extent() override { return Result<std::uint64_t>::success(9); }
  Result<void> synchronize(SyncStrength strength) override {
    return Result<void>::failure({strength == SyncStrength::full
                                      ? ErrorCode::unsupported
                                      : ErrorCode::interrupted,
                                  4});
  }
};
class CountedEffect final : public EffectBoundary {
public:
  Result<void> dispatch(const EffectIntent &intent) override {
    CHECK(intent.environment == identity<EnvironmentId>(1));
    CHECK(intent.actor == identity<ParticipantId>(2));
    CHECK(intent.invocation == identity<InvocationId>(3));
    CHECK(intent.attempt == identity<OperationAttemptId>(4));
    CHECK(intent.input.size() == 4 && intent.input[1] == std::byte{255});
    ++dispatches;
    return Result<void>::failure({ErrorCode::external_unknown});
  }
  unsigned int dispatches = 0;
};

void seams() {
  ShortStorage backend;
  Storage &storage = backend;
  std::array<std::byte, 4> bytes{};
  CHECK(require(storage.read_at(7, bytes)) == 2);
  CHECK(bytes[0] == std::byte{0} && bytes[1] == std::byte{255});
  CHECK(require(storage.write_at(7, bytes)) == 2);
  CHECK(require(storage.extent()) == 9);
  error_is(storage.synchronize(SyncStrength::full), ErrorCode::unsupported);
  const auto interrupted = storage.synchronize(SyncStrength::data);
  error_is(interrupted, ErrorCode::interrupted);
  CHECK(interrupted.error().detail == 4);
  CountedEffect effect;
  error_is(effect.dispatch({identity<EnvironmentId>(1), identity<ParticipantId>(2),
                            identity<InvocationId>(3), identity<OperationAttemptId>(4),
                            bytes}),
           ErrorCode::external_unknown);
  CHECK(effect.dispatches == 1);
}

void immutable_reads() {
  const std::array<std::byte, 4> bytes{std::byte{0}, std::byte{255}, std::byte{13},
                                       std::byte{10}};
  const auto buffer = require(ByteBuffer::copy(bytes, 4));
  const auto id = identity<ParticipantId>(7);
  std::array<std::uint64_t, 2> totals{};
  auto reader = [&](std::size_t index) {
    for (int iteration = 0; iteration < 1000; ++iteration) {
      totals[index] += std::to_integer<unsigned int>(buffer.view()[1]);
      totals[index] += std::to_integer<unsigned int>(id.bytes()[15]);
    }
  };
  {
    const std::jthread first{reader, 0};
    const std::jthread second{reader, 1};
  }
  CHECK(totals[0] == 262000);
  CHECK(totals[1] == 262000);
}
} // namespace

int main() {
  struct Case {
    const char *name;
    void (*run)();
  };
  const std::array cases{Case{"identity", identities},
                         Case{"handles", handles},
                         Case{"bytes", byte_values},
                         Case{"time", time_values},
                         Case{"clocks", clocks},
                         Case{"seams", seams},
                         Case{"immutable_reads", immutable_reads}};
  unsigned int failures = 0;
  for (const auto &[name, test] : cases) {
    try {
      test();
      std::printf("PASS %s\n", name);
    } catch (const std::exception &error) {
      ++failures;
      std::fprintf(stderr, "FAIL %s: %s\n", name, error.what());
    }
  }
  return failures == 0 ? 0 : 1;
}
