#include "blackbird/raw_runtime.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <new>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <thread>
#include <unistd.h>

namespace {
std::atomic<bool> observe_request{false}, request_queued{false};
std::atomic<int> unread_at_recovery{-1};
int reply_notice = -1;
struct RegistrationFault {
  std::atomic<bool> fail{false};
  std::array<char, 32> name{};
};
RegistrationFault *registration_fault = nullptr;
} // namespace
extern "C" ssize_t write(int descriptor, const void *data, std::size_t size) {
  if (size == 80 && registration_fault && registration_fault->fail.exchange(false)) {
    std::memcpy(registration_fault->name.data(), static_cast<const char *>(data) + 40,
                registration_fault->name.size());
    errno = ENOSPC;
    return -1;
  }
  iovec part{const_cast<void *>(data), size};
  return ::writev(descriptor, &part, 1);
}
extern "C" ssize_t send(int descriptor, const void *data, std::size_t message_size,
                        int message_flags) {
  const auto arguments = std::pair{message_size, message_flags};
  if (arguments.first == 1 && *static_cast<const std::byte *>(data) == std::byte{1}) {
    int unread = -1;
    if (::ioctl(descriptor, FIONREAD, &unread) == 0)
      unread_at_recovery.store(unread);
  }
  iovec part{const_cast<void *>(data), arguments.first};
  msghdr message{};
  message.msg_iov = &part;
  message.msg_iovlen = 1;
  const auto result = ::sendmsg(descriptor, &message, arguments.second);
  if (result == 1 && arguments.first == 1 && observe_request.load())
    request_queued.store(true);
  if (result == 40 && arguments.first == 40 && reply_notice >= 0) {
    const char notice = 1;
    if (::write(reply_notice, &notice, 1) != 1)
      std::abort();
  }
  return result;
}
namespace {
using namespace blackbird;
void check(bool valid, int line) {
  if (!valid) {
    std::fprintf(stderr, "raw_admission:%d\n", line);
    std::abort();
  }
}
#define CHECK(...) check((__VA_ARGS__), __LINE__)
template <class T> T take(Result<T> result) {
  CHECK(result.has_value());
  return std::move(result).value();
}
void take(Result<void> result) { CHECK(result.has_value()); }
struct Pipe {
  std::array<int, 2> descriptors{};
  Pipe() { CHECK(::pipe(descriptors.data()) == 0); }
  ~Pipe() {
    CHECK(::close(descriptors[0]) == 0);
    CHECK(::close(descriptors[1]) == 0);
  }
  void notify() const {
    const char value = 1;
    CHECK(::write(descriptors[1], &value, 1) == 1);
  }
  void consume() const {
    pollfd watch{descriptors[0], POLLIN, 0};
    CHECK(::poll(&watch, 1, 8000) == 1 && (watch.revents & POLLIN) != 0);
    char value = 0;
    CHECK(::read(descriptors[0], &value, 1) == 1 && value == 1);
  }
  RawWaitControl control() const { return {{descriptors[0], -1}, std::nullopt}; }
};
template <class Predicate> void until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
  while (!predicate()) {
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
}
struct Directory {
  std::array<char, 64> name{};
  Directory() {
    const std::string pattern = "/tmp/blackbird-admission-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), name.begin());
    CHECK(::mkdtemp(name.data()) != nullptr);
  }
  ~Directory() { std::filesystem::remove_all(name.data()); }
};
void interruption(Result<void> result, int detail) {
  CHECK(!result.has_value());
  CHECK(result.error() == (Error{ErrorCode::interrupted, detail}));
}
void cancellation_preserves_credit() {
  const auto name = "/bb-admission-" + std::to_string(::getpid());
  IdentityBytes epoch{};
  epoch[0] = std::byte{1};
  auto channel = take(RawChannel::create(name, epoch, {64, 64, 64}));
  {
    RawProducer producer{channel};
    RawThreadBinding binding{producer, epoch};
    auto terminal = take(raw_claim_control(32));
    Pipe cancellation;
    cancellation.notify();
    RawWaitScope scope{cancellation.control()};
    interruption(raw_wait_data(16), ECANCELED);
    const auto refused = raw_claim_control(16);
    CHECK(!refused.has_value());
    CHECK(refused.error() == (Error{ErrorCode::interrupted, ECANCELED}));
    CHECK(terminal.remaining() == 32);
    CHECK(take(channel.cursors(RawPartition::data)).published == 0);
    std::array<std::byte, 16> body{};
    const std::array<ByteView, 1> parts{body};
    CHECK(take(raw_emit(9100, parts, RawPartition::control, &terminal)) == 1);
    CHECK(terminal.remaining() == 0);
    cancellation.consume();
    take(raw_wait_data(64));
    std::array<std::byte, 48> full{};
    const std::array<ByteView, 1> full_parts{full};
    CHECK(take(raw_emit(9101, full_parts)) == 2);
    cancellation.notify();
    interruption(raw_wait_data(16), ECANCELED);
    CHECK(take(channel.cursors(RawPartition::data)).published == 64);
    CHECK(take(channel.cursors(RawPartition::data)).acknowledged == 0);
    CHECK(take(channel.cursors(RawPartition::failure)).published == 0);
  }
  take(RawChannel::unlink(name));
}
void scope_and_deadline() {
  Pipe first, second, third;
  const auto deadline = std::chrono::steady_clock::now();
  {
    auto control = first.control();
    control.deadline = deadline;
    RawWaitScope outer{control};
    {
      auto inner_control = second.control();
      inner_control.deadline = deadline + std::chrono::hours{1};
      RawWaitScope inner{inner_control};
      const auto selected = raw_wait_control();
      CHECK(selected.cancellation_fds[0] == second.descriptors[0]);
      CHECK(selected.cancellation_fds[1] == first.descriptors[0]);
      CHECK(selected.deadline == deadline);
      {
        RawWaitScope repeated{
            {{second.descriptors[0], second.descriptors[0]}, std::nullopt}};
        CHECK(raw_wait_control().cancellation_fds == selected.cancellation_fds);
      }
      bool rejected = false;
      try {
        RawWaitScope unsupported{third.control()};
      } catch (const Error &error) {
        CHECK(error.code == ErrorCode::capacity);
        rejected = true;
      }
      CHECK(rejected &&
            raw_wait_control().cancellation_fds == selected.cancellation_fds);
    }
    CHECK(raw_wait_control().cancellation_fds == control.cancellation_fds);
  }
  CHECK(!raw_wait_control().deadline);
}
void actual_rpc_cancellation() {
  Directory directory;
  Pipe responses, first_cancel, second_cancel;
  reply_notice = responses.descriptors[1];
  IdentityBytes identity{};
  identity[0] = std::byte{1};
  auto runtime = take(RawRuntime::open(directory.name.data(),
                                       take(EnvironmentId::from_bytes(identity)),
                                       {{1024, 1024, 128}, 111}));
  const auto before = take(runtime->status());
  responses.consume();
  CHECK(::kill(before.supervisor, SIGSTOP) == 0);
  observe_request.store(true);
  request_queued.store(false);
  std::optional<Error> first_error, second_error;
  std::thread first{[&] {
    const auto result = runtime->status(first_cancel.control());
    CHECK(!result.has_value());
    first_error = result.error();
  }};
  until([] { return request_queued.load(); });
  const auto gate_deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds{20};
  const auto gate_timed = runtime->status({{-1, -1}, gate_deadline});
  CHECK(!gate_timed.has_value());
  CHECK(gate_timed.error() == (Error{ErrorCode::interrupted, ETIMEDOUT}));
  std::thread second{[&] {
    const auto result = runtime->status(second_cancel.control());
    CHECK(!result.has_value());
    second_error = result.error();
  }};
  second_cancel.notify();
  second.join();
  CHECK(second_error == (Error{ErrorCode::interrupted, ECANCELED}));
  first_cancel.notify();
  first.join();
  CHECK(first_error == (Error{ErrorCode::interrupted, ECANCELED}));
  observe_request.store(false);
  CHECK(::kill(before.supervisor, SIGCONT) == 0);
  responses.consume();
  take(runtime->recover());
  CHECK(unread_at_recovery.load() == 0);
  responses.consume();
  const auto after = take(runtime->status());
  responses.consume();
  CHECK(after.recorder > 0 && after.recorder != before.recorder && !after.fenced);
  const auto expired = std::chrono::steady_clock::now() - std::chrono::seconds{1};
  const auto refused = runtime->status({{-1, -1}, expired});
  CHECK(!refused.has_value());
  CHECK(refused.error() == (Error{ErrorCode::interrupted, ETIMEDOUT}));
  CHECK(::kill(after.supervisor, SIGSTOP) == 0);
  const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds{20};
  const auto timed = runtime->status({{-1, -1}, limit});
  CHECK(!timed.has_value());
  CHECK(timed.error() == (Error{ErrorCode::interrupted, ETIMEDOUT}));
  CHECK(::kill(after.supervisor, SIGCONT) == 0);
  responses.consume();
  CHECK(take(runtime->status()).recorder == after.recorder);
  responses.consume();
  reply_notice = -1;
}
void actual_registration_error_detail() {
  void *memory = ::mmap(nullptr, sizeof(RegistrationFault), PROT_READ | PROT_WRITE,
                        MAP_ANON | MAP_SHARED, -1, 0);
  CHECK(memory != MAP_FAILED);
  registration_fault = new (memory) RegistrationFault{};
  Directory directory;
  IdentityBytes identity{};
  identity[0] = std::byte{3};
  {
    auto runtime = take(RawRuntime::open(directory.name.data(),
                                         take(EnvironmentId::from_bytes(identity)),
                                         {{1024, 1024, 128}, 111}));
    auto context = take(runtime->prepare_thread());
    {
      RawThreadBinding binding{context};
      auto terminal = take(raw_claim_control(32));
      registration_fault->fail.store(true);
      const auto result = runtime->prepare_thread();
      CHECK(!result.has_value());
      CHECK(result.error() == (Error{ErrorCode::io, ENOSPC}));
      CHECK(registration_fault->name[0] == '/');
      take(RawChannel::unlink(registration_fault->name.data()));
      CHECK(take(runtime->status()).fenced);
      const auto data = raw_wait_data(16);
      const auto control = raw_claim_control(16);
      CHECK(!data.has_value() && data.error().code == ErrorCode::audit_unavailable);
      CHECK(!control.has_value() &&
            control.error().code == ErrorCode::audit_unavailable);
      CHECK(terminal.remaining() == 32);
      const std::array<std::byte, 16> body{};
      const std::array<ByteView, 1> parts{body};
      CHECK(take(raw_emit(9107, parts, RawPartition::control, &terminal)) == 1);
    }
  }
  registration_fault->~RegistrationFault();
  registration_fault = nullptr;
  CHECK(::munmap(memory, sizeof(RegistrationFault)) == 0);
}
void actual_full_lane_waits() {
  Directory directory;
  Pipe cancellation;
  IdentityBytes identity{};
  identity[0] = std::byte{2};
  constexpr RawCapacities capacities{64, 64, 128};
  auto runtime = take(RawRuntime::open(directory.name.data(),
                                       take(EnvironmentId::from_bytes(identity)),
                                       {capacities, 111}));
  auto context = take(runtime->prepare_thread());
  const auto status = take(runtime->status());
  CHECK(::kill(status.recorder, SIGSTOP) == 0);
  // Observe an actual status send from the blocked data-admission slow path.
  observe_request.store(false);
  request_queued.store(false);
  IdentityBytes epoch{};
  std::thread worker{[&] {
    RawThreadBinding binding{context};
    epoch = raw_lane_epoch();
    auto terminal = take(raw_claim_control(32));
    std::array<std::byte, 48> data{};
    std::array<std::byte, 16> control{};
    const std::array<ByteView, 1> data_parts{data}, control_parts{control};
    CHECK(take(raw_emit(9102, data_parts)) == 1);
    CHECK(take(raw_emit(9103, control_parts, RawPartition::control)) == 2);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{20};
    interruption(raw_wait_data(16, {{-1, -1}, deadline}), ETIMEDOUT);
    deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{20};
    const auto claimed = raw_claim_control(16, {{-1, -1}, deadline});
    CHECK(!claimed.has_value());
    CHECK(claimed.error() == (Error{ErrorCode::interrupted, ETIMEDOUT}));
    CHECK(terminal.remaining() == 32);
    observe_request.store(true);
    interruption(raw_wait_data(16, cancellation.control()), ECANCELED);
    observe_request.store(false);
    CHECK(take(raw_emit(9104, control_parts, RawPartition::control, &terminal)) == 3);
    CHECK(terminal.remaining() == 0);
  }};
  until([] { return request_queued.load(); });
  cancellation.notify();
  worker.join();
  std::string name = "/bb";
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 12; ++i) {
    const auto byte = std::to_integer<unsigned>(epoch[i]);
    name += hex[byte >> 4];
    name += hex[byte & 15U];
  }
  auto channel = take(RawChannel::open(name, epoch, capacities));
  CHECK(take(channel.cursors(RawPartition::data)).published == 64);
  CHECK(take(channel.cursors(RawPartition::data)).acknowledged == 0);
  CHECK(take(channel.cursors(RawPartition::control)).published == 64);
  CHECK(take(channel.cursors(RawPartition::control)).acknowledged == 0);
  CHECK(take(channel.cursors(RawPartition::failure)).published == 0);
  CHECK(::kill(status.recorder, SIGCONT) == 0);
  context.reset();
  CHECK(take(channel.cursors(RawPartition::data)).acknowledged == 64);
  CHECK(take(channel.cursors(RawPartition::control)).acknowledged == 64);
}
} // namespace
int main() {
  cancellation_preserves_credit();
  scope_and_deadline();
  actual_rpc_cancellation();
  actual_full_lane_waits();
  actual_registration_error_detail();
}
