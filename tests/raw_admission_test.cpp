#include "blackbird/raw_runtime.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
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
std::atomic<int> lane_endpoint{-1};
std::atomic<unsigned> lane_polls{0};
std::atomic<bool> observe_lane{false};
using NativePoll = int (*)(pollfd *, nfds_t, int);
using NativeSendmsg = ssize_t (*)(int, const msghdr *, int);
NativePoll native_poll = nullptr;
NativeSendmsg native_sendmsg = nullptr;
struct RegistrationFault {
  std::atomic<bool> fail{false};
  std::array<char, 32> name{};
};
RegistrationFault *registration_fault = nullptr;
} // namespace
// Forward the actual syscalls. SCM_RIGHTS supplies the exact registered producer
// endpoint; a control RPC or gate poll cannot satisfy this lane-space cut.
extern "C" ssize_t sendmsg(int descriptor, const msghdr *message, int flags) {
  if (observe_lane.load() && message->msg_iovlen == 1 &&
      message->msg_iov[0].iov_len == 80 && message->msg_controllen != 0) {
    const auto *header = CMSG_FIRSTHDR(message);
    if (header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
        header->cmsg_len == CMSG_LEN(2 * sizeof(int))) {
      int endpoint = -1;
      std::memcpy(&endpoint, CMSG_DATA(header), sizeof(endpoint));
      lane_endpoint.store(endpoint);
    }
  }
  return native_sendmsg(descriptor, message, flags);
}
extern "C" int poll(pollfd *watches, nfds_t count, int timeout) {
  if (observe_lane.load() && count >= 2 && timeout != 0 &&
      watches[0].fd == lane_endpoint.load() && watches[0].events == POLLIN)
    lane_polls.fetch_add(1);
  return native_poll(watches, count, timeout);
}
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
std::string registered_name(const IdentityBytes &epoch) {
  std::string name = "/bb";
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 12; ++i) {
    const auto value = std::to_integer<unsigned>(epoch[i]);
    name += hex[value >> 4];
    name += hex[value & 15U];
  }
  return name;
}
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
    auto data_claim = take(raw_claim_data(32));
    Pipe cancellation;
    cancellation.notify();
    RawWaitScope scope{cancellation.control()};
    const auto refused_data = raw_claim_data(16);
    CHECK(!refused_data.has_value());
    CHECK(refused_data.error() == (Error{ErrorCode::interrupted, ECANCELED}));
    CHECK(data_claim.remaining() == 32);
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
    CHECK(take(raw_emit(9108, parts, RawPartition::data, &data_claim)) == 2);
    CHECK(data_claim.remaining() == 0);
    cancellation.consume();
    take(channel.acknowledge(RawPartition::data, 32));
    take(raw_wait_data(64));
    std::array<std::byte, 48> full{};
    const std::array<ByteView, 1> full_parts{full};
    CHECK(take(raw_emit(9101, full_parts)) == 3);
    cancellation.notify();
    interruption(raw_wait_data(16), ECANCELED);
    CHECK(take(channel.cursors(RawPartition::data)).published == 96);
    CHECK(take(channel.cursors(RawPartition::data)).acknowledged == 32);
    CHECK(take(channel.cursors(RawPartition::failure)).published == 0);
  }
  take(RawChannel::unlink(name));
}
void nonblocking_cleanup_credit() {
  const auto name = "/bb-try-control-" + std::to_string(::getpid());
  IdentityBytes epoch{};
  epoch[0] = std::byte{7};
  auto channel = take(RawChannel::create(name, epoch, {64, 64, 64}));
  {
    RawProducer producer{channel};
    RawThreadBinding binding{producer, epoch};
    Pipe cancellation;
    cancellation.notify();
    auto control = cancellation.control();
    control.deadline = std::chrono::steady_clock::now();
    RawWaitScope canceled{control};
    auto held = take(raw_try_claim_control(32));
    CHECK(held.remaining() == 32);
    std::array<std::byte, 16> body{};
    body[0] = std::byte{255};
    const std::array<ByteView, 1> parts{body};
    CHECK(take(raw_emit(9140, parts, RawPartition::control)) == 1);
    const auto refused = raw_try_claim_control(16);
    CHECK(!refused.has_value() && refused.error().code == ErrorCode::capacity);
    CHECK(held.remaining() == 32);
    const auto cursor = take(channel.cursors(RawPartition::control));
    CHECK(cursor.published == 32 && cursor.acknowledged == 0);
    CHECK(take(raw_emit(9141, {}, RawPartition::control, &held)) == 2);
    CHECK(take(raw_emit(9142, {}, RawPartition::control, &held)) == 3);
    CHECK(held.remaining() == 0);
    const auto records = take(channel.read(RawPartition::control, 0, 64));
    CHECK(records.bytes.size() == 64);
    CHECK(std::equal(body.begin(), body.end(), records.bytes.begin() + 16));
    CHECK(take(channel.cursors(RawPartition::failure)).published == 0);
    raw_fence({ErrorCode::io, EIO});
    const auto fenced = raw_try_claim_control(16);
    CHECK(!fenced.has_value() && fenced.error().code == ErrorCode::audit_unavailable);
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
      auto held_data = take(raw_claim_data(32));
      registration_fault->fail.store(true);
      const auto result = runtime->prepare_thread();
      CHECK(!result.has_value());
      CHECK(result.error() == (Error{ErrorCode::io, ENOSPC}));
      CHECK(registration_fault->name[0] == '/');
      take(RawChannel::unlink(registration_fault->name.data()));
      CHECK(take(runtime->status()).fenced);
      const auto data = raw_wait_data(16);
      const auto control = raw_claim_control(16);
      const auto data_claim = raw_claim_data(16);
      CHECK(!data_claim.has_value() &&
            data_claim.error().code == ErrorCode::audit_unavailable);
      CHECK(!data.has_value() && data.error().code == ErrorCode::audit_unavailable);
      CHECK(!control.has_value() &&
            control.error().code == ErrorCode::audit_unavailable);
      CHECK(terminal.remaining() == 32);
      const std::array<std::byte, 16> body{};
      const std::array<ByteView, 1> parts{body};
      CHECK(take(raw_emit(9107, parts, RawPartition::control, &terminal)) == 1);
      CHECK(take(raw_emit(9109, parts, RawPartition::data, &held_data)) == 2);
      CHECK(held_data.remaining() == 0);
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
void actual_owner_grant_bounds() {
  Directory directory;
  IdentityBytes identity{};
  identity[0] = std::byte{4};
  auto runtime = take(RawRuntime::open(directory.name.data(),
                                       take(EnvironmentId::from_bytes(identity)),
                                       {{128, 128, 128}, 111}));
  auto context = take(runtime->prepare_thread());
  RawThreadBinding binding{context};
  const auto epoch = raw_lane_epoch();
  auto channel = take(RawChannel::open(registered_name(epoch), epoch, {128, 128, 128}));
  auto data = take(raw_claim_data(96));
  auto control = take(raw_claim_control(96));
  const auto deadline = [] {
    return RawWaitControl{
        {-1, -1}, std::chrono::steady_clock::now() + std::chrono::milliseconds{20}};
  };
  const auto refused_data = raw_claim_data(64, deadline());
  const auto refused_control = raw_claim_control(64, deadline());
  const auto refused_wait = raw_wait_data(64, deadline());
  const auto capacity = [](const auto &result, const char *site) {
    if (!result.has_value() && result.error().code == ErrorCode::capacity)
      return true;
    std::fprintf(stderr, "%s refused with code=%d detail=%lld, expected capacity\n",
                 site, result.has_value() ? -1 : static_cast<int>(result.error().code),
                 result.has_value() ? 0LL
                                    : static_cast<long long>(result.error().detail));
    return false;
  };
  const bool data_refused = capacity(refused_data, "owned data");
  const bool control_refused = capacity(refused_control, "owned control");
  const bool wait_refused = capacity(refused_wait, "data wait");
  CHECK(data_refused && control_refused && wait_refused);
  CHECK(data.remaining() == 96 && control.remaining() == 96);
  for (const auto partition :
       {RawPartition::data, RawPartition::control, RawPartition::failure}) {
    const auto cursor = take(channel.cursors(partition));
    CHECK(cursor.published == 0 && cursor.acknowledged == 0);
  }
  std::array<std::byte, 16> original{};
  const std::array<ByteView, 1> parts{original};
  CHECK(take(raw_emit(9114, parts, RawPartition::data, &data)) == 1);
  CHECK(take(raw_emit(9115, parts, RawPartition::control, &control)) == 2);
}
void actual_data_claim_waits() {
  Directory directory;
  Pipe cancellation, source;
  IdentityBytes environment{};
  environment[0] = std::byte{3};
  constexpr RawCapacities capacities{64, 64, 128};
  auto runtime = take(RawRuntime::open(directory.name.data(),
                                       take(EnvironmentId::from_bytes(environment)),
                                       {capacities, 111}));
  lane_endpoint.store(-1);
  lane_polls.store(0);
  observe_lane.store(true);
  auto context = take(runtime->prepare_thread());
  CHECK(lane_endpoint.load() >= 0);
  IdentityBytes epoch{};
  {
    RawThreadBinding binding{context};
    epoch = raw_lane_epoch();
  }
  const auto status = take(runtime->status());
  CHECK(::kill(status.recorder, SIGSTOP) == 0);
  std::atomic<unsigned> phase{0}, acquisitions{0};
  std::array<std::byte, 16> original{};
  for (std::size_t i = 0; i < original.size(); ++i)
    original[i] = std::byte{static_cast<unsigned char>(i * 17U)};
  const std::array<ByteView, 1> parts{original};
  std::array<std::byte, 48> source_bytes{};
  std::copy(original.begin(), original.end(), source_bytes.begin());
  CHECK(::write(source.descriptors[1], source_bytes.data(), source_bytes.size()) == 48);
  std::thread worker{[&] {
    RawThreadBinding binding{context};
    auto terminal = take(raw_claim_control(32));
    auto held = take(raw_claim_data(32));
    CHECK(take(raw_emit(9110, parts)) == 1);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
    const auto timed = raw_claim_data(16, {{-1, -1}, deadline});
    CHECK(!timed.has_value() &&
          timed.error() == (Error{ErrorCode::interrupted, ETIMEDOUT}));
    CHECK(lane_polls.load() != 0);
    lane_polls.store(0);
    phase.store(1, std::memory_order_release);
    const auto canceled = raw_claim_data(16, cancellation.control());
    CHECK(!canceled.has_value() &&
          canceled.error() == (Error{ErrorCode::interrupted, ECANCELED}));
    CHECK(held.remaining() == 32 && terminal.remaining() == 32);
    CHECK(take(raw_emit(9111, parts, RawPartition::data, &held)) == 2);
    CHECK(take(raw_emit(9112, parts, RawPartition::control, &terminal)) == 3);
    lane_polls.store(0);
    phase.store(2, std::memory_order_release);
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
    auto granted = take(raw_claim_data(64, {{-1, -1}, deadline}));
    std::array<std::byte, 48> acquired{};
    CHECK(::read(source.descriptors[0], acquired.data(), acquired.size()) == 48);
    acquisitions.fetch_add(1);
    CHECK(acquired == source_bytes);
    const std::array<ByteView, 1> acquired_parts{acquired};
    CHECK(take(raw_emit(9113, acquired_parts, RawPartition::data, &granted)) == 4);
    CHECK(granted.remaining() == 0);
  }};
  until([&] {
    return phase.load(std::memory_order_acquire) == 1 && lane_polls.load() != 0;
  });
  CHECK(acquisitions.load() == 0);
  int unread = -1;
  CHECK(::ioctl(source.descriptors[0], FIONREAD, &unread) == 0 && unread == 48);
  cancellation.notify();
  until([&] {
    return phase.load(std::memory_order_acquire) == 2 && lane_polls.load() != 0;
  });
  CHECK(acquisitions.load() == 0);
  CHECK(::ioctl(source.descriptors[0], FIONREAD, &unread) == 0 && unread == 48);
  std::string name = "/bb";
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 12; ++i) {
    const auto value = std::to_integer<unsigned>(epoch[i]);
    name += hex[value >> 4];
    name += hex[value & 15U];
  }
  auto channel = take(RawChannel::open(name, epoch, capacities));
  const auto data = take(channel.cursors(RawPartition::data));
  CHECK(data.published == 64 && data.acknowledged == 0);
  CHECK(take(channel.cursors(RawPartition::control)).published == 32);
  CHECK(take(channel.cursors(RawPartition::failure)).published == 0);
  // Independent fixed envelope bytes for ordinals1/2, followed by originals.
  std::vector<std::byte> expected;
  for (const auto [site, ordinal] : {std::pair{9110U, 1U}, std::pair{9111U, 2U}}) {
    for (const auto [value, width] :
         {std::pair{32ULL, 4U}, std::pair{static_cast<unsigned long long>(site), 4U},
          std::pair{static_cast<unsigned long long>(ordinal), 8U}})
      for (unsigned i = 0; i < width; ++i)
        expected.push_back(
            std::byte{static_cast<unsigned char>((value >> (i * 8U)) & 255U)});
    expected.insert(expected.end(), original.begin(), original.end());
  }
  CHECK(take(channel.read(RawPartition::data, 0, 64)).bytes == expected);
  CHECK(::kill(status.recorder, SIGCONT) == 0);
  worker.join();
  CHECK(acquisitions.load() == 1);
  CHECK(::ioctl(source.descriptors[0], FIONREAD, &unread) == 0 && unread == 0);
  observe_lane.store(false);
  context.reset();
  CHECK(take(channel.cursors(RawPartition::data)).acknowledged == 128);
  CHECK(take(channel.cursors(RawPartition::control)).acknowledged == 32);
}
} // namespace
int main() {
  native_poll = reinterpret_cast<NativePoll>(::dlsym(RTLD_NEXT, "poll"));
  native_sendmsg = reinterpret_cast<NativeSendmsg>(::dlsym(RTLD_NEXT, "sendmsg"));
  CHECK(native_poll != nullptr && native_sendmsg != nullptr);
  cancellation_preserves_credit();
  nonblocking_cleanup_credit();
  scope_and_deadline();
  actual_rpc_cancellation();
  actual_full_lane_waits();
  actual_registration_error_detail();
  actual_owner_grant_bounds();
  actual_data_claim_waits();
}
