#include "blackbird/raw_channel.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <exception>
#include <iostream>
#include <poll.h>
#include <source_location>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace blackbird;

namespace {
void check(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
template <typename T>
T take(Result<T> result,
       std::source_location location = std::source_location::current()) {
  if (!result.has_value()) {
    throw std::runtime_error("transport failure at line " +
                             std::to_string(location.line()) + " code " +
                             std::to_string(static_cast<int>(result.error().code)) +
                             " detail " + std::to_string(result.error().detail));
  }
  return std::move(result).value();
}
void take(Result<void> result,
          std::source_location location = std::source_location::current()) {
  if (!result.has_value()) {
    throw std::runtime_error("transport failure at line " +
                             std::to_string(location.line()) + " code " +
                             std::to_string(static_cast<int>(result.error().code)) +
                             " detail " + std::to_string(result.error().detail));
  }
}
IdentityBytes identity(std::uint8_t salt) {
  IdentityBytes value{};
  for (std::size_t index = 0; index < value.size(); ++index) {
    value[index] = std::byte{static_cast<std::uint8_t>(salt + index)};
  }
  return value;
}
struct NamedLane {
  std::string name;
  IdentityBytes epoch = identity(31);
  RawCapacities capacities;
  explicit NamedLane(RawCapacities sizes) : capacities(sizes) {
    static std::uint32_t sequence = 0;
    name = "/bbraw" + std::to_string(::getpid()) + "x" + std::to_string(++sequence);
  }
  ~NamedLane() { (void)RawChannel::unlink(name); }
  RawChannel create() { return take(RawChannel::create(name, epoch, capacities)); }
  RawChannel open() { return take(RawChannel::open(name, epoch, capacities)); }
};
struct Endpoints {
  std::array<int, 2> descriptors = take(raw_socket_pair());
  ~Endpoints() {
    for (const int descriptor : descriptors) {
      if (descriptor != -1) {
        (void)::close(descriptor);
      }
    }
  }
};
struct BodySpec {
  std::size_t count;
  std::uint64_t seed;
};
std::vector<std::byte> body(BodySpec spec) {
  const auto [count, seed] = spec;
  std::vector<std::byte> bytes(count);
  for (std::size_t index = 0; index < count; ++index) {
    bytes[index] = std::byte{
        static_cast<std::uint8_t>((seed * 73 + index * 29 + (index >> 2)) & 255)};
  }
  return bytes;
}
// Independent format oracle: assemble little-endian fields byte-by-byte rather
// than sharing production's memcpy/native-field code or parsing its output.
template <unsigned Width>
void append_unsigned(std::vector<std::byte> &bytes, std::uint64_t value) {
  for (unsigned index = 0; index < Width; ++index) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(value & 255)});
    value >>= 8;
  }
}
std::vector<std::byte> expected(std::uint32_t site, ByteView bytes,
                                std::uint64_t ordinal) {
  std::vector<std::byte> result;
  append_unsigned<4>(result, bytes.size() + 16);
  append_unsigned<4>(result, site);
  append_unsigned<8>(result, ordinal);
  result.insert(result.end(), bytes.begin(), bytes.end());
  return result;
}
std::uint64_t publish(RawProducer &producer, std::uint32_t site, ByteView bytes,
                      RawPartition partition = RawPartition::data) {
  take(producer.reserve(partition, bytes.size() + 16));
  const std::array<ByteView, 3> parts{bytes.first(bytes.size() / 3),
                                      bytes.subspan(bytes.size() / 3, bytes.size() / 3),
                                      bytes.subspan(2 * (bytes.size() / 3))};
  return take(producer.emit(site, parts, partition));
}
bool readable(int descriptor) {
  pollfd polling{descriptor, POLLIN, 0};
  const int ready = ::poll(&polling, 1, 1000);
  check(ready >= 0, "poll failed");
  return ready == 1 && (polling.revents & POLLIN) != 0;
}

void capacity_and_exact_bytes() {
  NamedLane lane{{97, 113, 71}};
  auto producer_mapping = lane.create();
  auto recorder_mapping = lane.open();
  RawProducer producer{producer_mapping};
  const auto source = body({28, 12});
  const std::array<ByteView, 1> parts{source};
  check(!producer.emit(7, parts).has_value(), "unpaid emission succeeded");
  check(take(recorder_mapping.cursors(RawPartition::data)).published == 0,
        "rejected record published bytes");
  take(producer.reserve(RawPartition::data, 44));
  const auto rejected_zero = producer.emit(0, parts);
  check(!rejected_zero.has_value() &&
            rejected_zero.error().code == ErrorCode::invalid_identity,
        "zero site accepted or returned wrong error");
  const auto after_zero = take(recorder_mapping.cursors(RawPartition::data));
  check(after_zero.published == 0 && after_zero.acknowledged == 0,
        "zero rejection moved cursors");
  check(take(producer.available(RawPartition::data)) == 53,
        "zero rejection changed prepaid credit");
  check(take(producer.emit(0xCAFE1234U, parts)) == 1,
        "zero rejection consumed ordinal or reservation");
  auto first = take(recorder_mapping.read(RawPartition::data, 0, 97));
  check(first.bytes == expected(0xCAFE1234U, source, 1), "raw bytes/envelope changed");
  check(first.start == 0 && first.end == 44, "read cursor mismatch");
  check(!recorder_mapping.read(RawPartition::data, 0, 43).has_value(),
        "partial record read succeeded");
  check(!recorder_mapping.read(RawPartition::data, 1, 97).has_value(),
        "interior read cursor accepted");
  check(!recorder_mapping.acknowledge(RawPartition::data, 43).has_value(),
        "interior reclaim accepted");
  check(!recorder_mapping.acknowledge(RawPartition::data, 45).has_value(),
        "unpublished reclaim accepted");
  check(take(producer.available(RawPartition::data)) == 53, "read reclaimed bytes");
  const auto second_body = body({35, 92});
  check(publish(producer, 9, second_body) == 2, "ordinal not increasing");
  check(take(producer.available(RawPartition::data)) == 2, "fullness not byte-exact");
  check(!producer.reserve(RawPartition::data, 16).has_value(), "full lane admitted");
  check(!producer.reserve(RawPartition::data, UINT64_MAX).has_value(),
        "reservation overflow admitted");
  auto prefix = take(recorder_mapping.read(RawPartition::data, 0, 94));
  check(prefix.end == 44 && prefix.bytes == first.bytes, "finite prefix split record");
  take(recorder_mapping.acknowledge(RawPartition::data, 44));
  check(!recorder_mapping.read(RawPartition::data, 0, 97).has_value(),
        "reclaimed range remained readable");
  const auto third_body = body({11, 51});
  check(publish(producer, 17, third_body) == 3, "wrap ordinal wrong");
  auto wrapped = take(recorder_mapping.read(RawPartition::data, 44, 97));
  auto combined = expected(9, second_body, 2);
  const auto third = expected(17, third_body, 3);
  combined.insert(combined.end(), third.begin(), third.end());
  check(wrapped.bytes == combined && wrapped.end == 122, "wrapped raw bytes changed");
  take(recorder_mapping.acknowledge(RawPartition::data, wrapped.end));
  check(take(producer.available(RawPartition::data)) == 97, "reclaim credit wrong");
  check(!RawChannel::open(lane.name, identity(32), lane.capacities).has_value(),
        "wrong epoch accepted");
  check(!RawChannel::open(lane.name, lane.epoch, {98, 113, 71}).has_value(),
        "wrong capacities accepted");
  check(!RawChannel::create(lane.name, lane.epoch, lane.capacities).has_value(),
        "live named object overwritten");
  check(!RawChannel::create("not-posix", lane.epoch, lane.capacities).has_value(),
        "invalid memory name accepted");
  check(!RawChannel::create("/bbsmall", lane.epoch, {15, 16, 16}).has_value(),
        "sub-envelope capacity accepted");
}

void control_credit_and_partitions() {
  NamedLane lane{{64, 113, 71}};
  auto channel = lane.create();
  RawProducer producer{channel};
  const auto terminal_body = body({45, 8});
  auto claim = take(producer.hold_control(61));
  check(take(producer.available(RawPartition::control)) == 52, "claim credit wrong");
  check(!producer.reserve(RawPartition::control, 53).has_value(),
        "ordinary emission stole terminal credit");
  check(take(channel.cursors(RawPartition::control)).published == 0,
        "held claim created physical hole");
  const auto ordinary_body = body({17, 6});
  check(publish(producer, 33, ordinary_body, RawPartition::control) == 1,
        "ordinary control blocked behind held claim");
  const std::array<ByteView, 1> terminal_parts{terminal_body};
  check(!producer.emit(34, terminal_parts, RawPartition::data, &claim).has_value(),
        "control credit used on data lane");
  check(take(producer.emit(34, terminal_parts, RawPartition::control, &claim)) == 2,
        "held terminal emission failed");
  check(claim.remaining() == 0, "claim not consumed exactly");
  auto control = take(channel.read(RawPartition::control, 0, 113));
  auto combined = expected(33, ordinary_body, 1);
  const auto terminal = expected(34, terminal_body, 2);
  combined.insert(combined.end(), terminal.begin(), terminal.end());
  check(control.bytes == combined, "terminal ordering/raw bytes wrong");
  check(take(producer.available(RawPartition::control)) == 19,
        "held credit double counted");
  const auto failure_body = body({55, 119});
  check(publish(producer, 49, failure_body, RawPartition::failure) == 3,
        "independent failure partition unavailable");
  check(take(channel.read(RawPartition::failure, 0, 71)).bytes ==
            expected(49, failure_body, 3),
        "failure bytes changed");
  take(channel.acknowledge(RawPartition::control, control.end));
  {
    auto initial = take(producer.hold_control(80));
    auto moved = std::move(initial);
    check(moved.remaining() == 80, "claim move lost credit");
    auto replacement = take(producer.hold_control(20));
    replacement = std::move(moved);
    check(take(producer.available(RawPartition::control)) == 33,
          "claim assignment failed to release old credit");
  }
  check(take(producer.available(RawPartition::control)) == 113,
        "claim destruction leaked credit");
  take(producer.reserve(RawPartition::data, 64));
  producer.release_reservation(RawPartition::data);
  check(take(producer.available(RawPartition::data)) == 64,
        "release reservation leaked credit");
  RawProducer exhausted{channel, RawProducer::Start{-1, UINT64_MAX}};
  const auto refused = exhausted.reserve(RawPartition::data, 16);
  check(!refused.has_value() && refused.error().code == ErrorCode::overflow,
        "exhausted ordinal granted acquisition credit");
  check(!exhausted.hold_control(16).has_value(),
        "exhausted ordinal granted held credit");
  check(!exhausted.emit(1, {}).has_value(), "ordinal wrapped");
}

void epoch_closing_preserves_held_credit() {
  NamedLane lane{{64, 64, 64}};
  auto channel = lane.create();
  // Full-capacity margin is twelve minimum-size records. This direct start
  // exercises the boundary without emitting2^64 bytes or rebasing live cursors.
  constexpr auto initial_ordinal = UINT64_MAX - 12;
  RawProducer producer{channel, RawProducer::Start{-1, initial_ordinal}};
  auto terminal = take(producer.hold_control(32));
  take(producer.reserve(RawPartition::data, 16));
  check(take(producer.emit(401, {})) == initial_ordinal, "near-boundary ordinal wrong");
  const auto refused = producer.reserve(RawPartition::data, 16);
  check(!refused.has_value() && refused.error().code == ErrorCode::overflow,
        "new acquisition admitted inside epoch-closing margin");
  const auto refused_claim = producer.hold_control(16);
  check(!refused_claim.has_value() && refused_claim.error().code == ErrorCode::overflow,
        "new terminal claim admitted inside epoch-closing margin");
  check(terminal.remaining() == 32, "closing admission consumed existing held credit");
  check(take(producer.emit(402, {}, RawPartition::control, &terminal)) ==
            initial_ordinal + 1,
        "closing admission refused first prepaid terminal");
  check(take(producer.emit(403, {}, RawPartition::control, &terminal)) ==
            initial_ordinal + 2,
        "closing admission refused second prepaid terminal");
  const auto ordinary = take(channel.read(RawPartition::data, 0, 64));
  const auto terminals = take(channel.read(RawPartition::control, 0, 64));
  check(ordinary.bytes == expected(401, {}, initial_ordinal),
        "closing changed raw ordinary");
  auto combined = expected(402, {}, initial_ordinal + 1);
  const auto final = expected(403, {}, initial_ordinal + 2);
  combined.insert(combined.end(), final.begin(), final.end());
  check(terminals.bytes == combined && terminal.remaining() == 0,
        "closing changed held raw terminals");
  take(channel.acknowledge(RawPartition::data, ordinary.end));
  take(channel.acknowledge(RawPartition::control, terminals.end));
  check(!producer.reserve(RawPartition::data, 16).has_value(),
        "drain silently reopened exhausted epoch");
}

void wake_directions_and_failures() {
  NamedLane lane{{64, 64, 64}};
  auto producer_mapping = lane.create();
  auto recorder_mapping = lane.open();
  Endpoints sockets;
  RawProducer producer{producer_mapping, sockets.descriptors[0]};
  take(raw_drain_notices(sockets.descriptors[1]));
  take(recorder_mapping.arm_data());
  check(take(recorder_mapping.cursors(RawPartition::data)).published == 0,
        "empty post-arm recheck failed");
  const auto bytes = body({48, 2});
  check(publish(producer, 78, bytes) == 1, "armed publication failed");
  check(readable(sockets.descriptors[1]), "armed publication lost wake");
  check(!producer.notification_error(), "healthy notice reported failure");
  take(raw_drain_notices(sockets.descriptors[1]));
  take(raw_drain_notices(sockets.descriptors[0]));
  take(producer_mapping.arm_space());
  check(take(producer.available(RawPartition::data)) == 0,
        "space recheck missed full lane");
  take(recorder_mapping.acknowledge(RawPartition::data, 64, sockets.descriptors[1]));
  check(readable(sockets.descriptors[0]), "reverse reclaim lost wake");
  take(raw_drain_notices(sockets.descriptors[0]));
  check(take(producer.available(RawPartition::data)) == 64, "reverse credit wrong");
  // Full doorbell is a pending notice, not loss. Fill without using the helper
  // so this test can observe the actual EAGAIN boundary independently.
  const std::array<std::byte, 4096> flood{};
  for (;;) {
    const auto count = ::send(sockets.descriptors[0], flood.data(), flood.size(),
                              MSG_DONTWAIT | MSG_NOSIGNAL);
    if (count == -1) {
      check(errno == EAGAIN || errno == EWOULDBLOCK, "doorbell fill failed");
      break;
    }
    check(count > 0, "doorbell fill made no progress");
  }
  take(recorder_mapping.arm_data());
  check(publish(producer, 79, bytes) == 2, "full doorbell rejected publication");
  check(!producer.notification_error(), "full notice misclassified as hard failure");
  check(readable(sockets.descriptors[1]), "full doorbell not readable");
  check(take(recorder_mapping.read(RawPartition::data, 64, 64)).bytes ==
            expected(79, bytes, 2),
        "full doorbell lost raw record");
  take(raw_drain_notices(sockets.descriptors[1]));
  take(recorder_mapping.acknowledge(RawPartition::data, 128));
  (void)::close(sockets.descriptors[1]);
  sockets.descriptors[1] = -1;
  take(recorder_mapping.arm_data());
  check(publish(producer, 80, bytes) == 3,
        "failed doorbell returned uncommitted emission");
  check(producer.notification_error().has_value(), "broken doorbell failure lost");
  check(take(recorder_mapping.read(RawPartition::data, 128, 64)).bytes ==
            expected(80, bytes, 3),
        "broken doorbell discarded original");
  producer.clear_notification_error();
  check(!producer.notification_error(), "notice error clear failed");
}

void process_death_retention() {
  NamedLane lane{{128, 64, 64}};
  const auto bytes = body({63, 71});
  const pid_t producer = ::fork();
  check(producer != -1, "fork producer failed");
  if (producer == 0) {
    try {
      auto channel = lane.create();
      RawProducer owner{channel};
      (void)publish(owner, 101, bytes);
      // Leave no live mapping, then exit without unlinking the named object.
      channel = take(RawChannel::open(lane.name, lane.epoch, lane.capacities));
      ::_exit(0);
    } catch (...) {
      ::_exit(1);
    }
  }
  int status = 0;
  check(::waitpid(producer, &status, 0) == producer && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0,
        "producer death setup failed");
  const pid_t recorder = ::fork();
  check(recorder != -1, "fork recorder failed");
  if (recorder == 0) {
    try {
      auto channel = lane.open();
      auto read = take(channel.read(RawPartition::data, 0, 128));
      check(read.bytes == expected(101, bytes, 1), "dead producer raw bytes wrong");
      // Reader-private copies do not acknowledge. Replacement must see original.
      ::_exit(0);
    } catch (...) {
      ::_exit(1);
    }
  }
  check(::waitpid(recorder, &status, 0) == recorder && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0,
        "recorder death read failed");
  auto reopened = lane.open();
  check(take(reopened.cursors(RawPartition::data)).acknowledged == 0,
        "reader death reclaimed original");
  check(take(reopened.read(RawPartition::data, 0, 128)).bytes ==
            expected(101, bytes, 1),
        "both deaths lost named-memory original");
  take(reopened.acknowledge(RawPartition::data, 79));
  take(RawChannel::unlink(lane.name));
  check(!RawChannel::open(lane.name, lane.epoch, lane.capacities).has_value(),
        "explicit unlink did not remove discovery name");
  check(take(reopened.cursors(RawPartition::data)).acknowledged == 79,
        "unlink invalidated live mapping");
}

void variable_record_model() {
  NamedLane lane{{257, 113, 97}};
  auto channel = lane.create();
  auto reader = lane.open();
  RawProducer producer{channel};
  std::array<std::vector<std::byte>, 3> queued;
  std::array<std::uint64_t, 3> starts{};
  std::uint64_t ordinal = 1;
  std::uint32_t random = 0xBAD5EEDU;
  for (std::uint32_t iteration = 0; iteration < 5000; ++iteration) {
    random = random * 1664525U + 1013904223U;
    const auto index = static_cast<std::size_t>((random >> 24) % 3);
    const auto partition = static_cast<RawPartition>(index + 1);
    const auto bytes = body({(random >> 9) % 48, iteration + 211});
    const auto size = bytes.size() + 16;
    const auto free = take(producer.available(partition));
    if (size > free || (random & 7) == 0) {
      auto copied = take(reader.read(partition, starts[index], lane.capacities[index]));
      check(copied.bytes == queued[index], "variable-byte model mismatch");
      check(copied.end - copied.start == queued[index].size(), "model cursor mismatch");
      take(reader.acknowledge(partition, copied.end));
      starts[index] = copied.end;
      queued[index].clear();
    }
    check(publish(producer, iteration + 19, bytes, partition) == ordinal,
          "model ordinal mismatch");
    const auto record = expected(iteration + 19, bytes, ordinal++);
    queued[index].insert(queued[index].end(), record.begin(), record.end());
  }
  for (std::size_t index = 0; index < queued.size(); ++index) {
    const auto partition = static_cast<RawPartition>(index + 1);
    check(take(reader.read(partition, starts[index], lane.capacities[index])).bytes ==
              queued[index],
          "model final prefix mismatch");
  }
}

void concurrent_publication_reclaim() {
  NamedLane lane{{1021, 64, 64}};
  auto writer_mapping = lane.create();
  auto reader_mapping = lane.open();
  std::atomic<bool> failed{false};
  std::exception_ptr reader_error;
  constexpr std::uint32_t count = 10000;
  std::thread reader{[&] {
    try {
      std::uint64_t position = 0;
      for (std::uint32_t index = 0; index < count; ++index) {
        const auto bytes = body({index % 61, index + 6});
        const auto record = expected(index + 60, bytes, index + 1);
        RawRead copied{0, 0, {}};
        do {
          copied =
              take(reader_mapping.read(RawPartition::data, position, record.size()));
          if (failed.load(std::memory_order_acquire)) {
            return;
          }
          if (copied.bytes.empty()) {
            std::this_thread::yield();
          }
        } while (copied.bytes.empty());
        check(copied.bytes == record, "concurrent raw event torn/overwritten");
        position = copied.end;
        take(reader_mapping.acknowledge(RawPartition::data, position));
      }
    } catch (...) {
      reader_error = std::current_exception();
      failed.store(true, std::memory_order_release);
    }
  }};
  try {
    RawProducer producer{writer_mapping};
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto bytes = body({index % 61, index + 6});
      while (take(producer.available(RawPartition::data)) < bytes.size() + 16) {
        check(!failed.load(std::memory_order_acquire),
              "reader failed during producer wait");
        std::this_thread::yield();
      }
      check(publish(producer, index + 60, bytes) == index + 1,
            "concurrent ordinal mismatch");
    }
  } catch (...) {
    failed.store(true, std::memory_order_release);
    reader.join();
    throw;
  }
  reader.join();
  if (reader_error) {
    std::rethrow_exception(reader_error);
  }
  const auto cursor = take(writer_mapping.cursors(RawPartition::data));
  check(cursor.published == cursor.acknowledged, "concurrent final reclaim incomplete");
}
} // namespace

int main() {
  try {
    capacity_and_exact_bytes();
    control_credit_and_partitions();
    epoch_closing_preserves_held_credit();
    wake_directions_and_failures();
    process_death_retention();
    variable_record_model();
    concurrent_publication_reclaim();
    std::cout << "raw channel tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
