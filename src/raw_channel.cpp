#include "blackbird/raw_channel.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace blackbird {
namespace {
#if !((defined(__APPLE__) && defined(__aarch64__)) ||                                  \
      (defined(__linux__) && defined(__x86_64__)))
#error Retained raw channel requires the qualified Mac ARM or Linux x86 ABI.
#endif
// Qualified native ABI only. ISO C++ alone does not promise cross-process
// atomics; these aligned lock-free words require the Mac ARM/Linux x86 profiles.
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(std::size_t) == 8);
constexpr std::uint64_t channel_magic = 0x3157415252424242ULL;
struct alignas(64) SharedWord {
  std::atomic<std::uint64_t> value{0};
};
struct PartitionState {
  SharedWord published;
  SharedWord acknowledged;
};
struct SharedHeader {
  SharedWord ready;
  IdentityBytes epoch{};
  RawCapacities capacities{};
  std::array<std::uint64_t, 3> offsets{};
  std::uint64_t mapped_bytes = 0;
  SharedWord data_armed;
  SharedWord space_armed;
  std::array<PartitionState, 3> partitions;
};
Error native_error() noexcept {
  const int code = errno;
  return {code == EINTR ? ErrorCode::interrupted : ErrorCode::io, code};
}
Result<std::size_t> partition_index(RawPartition partition) {
  const auto value = static_cast<std::uint32_t>(partition);
  if (value < 1 || value > 3) {
    return Result<std::size_t>::failure({ErrorCode::invalid_range});
  }
  return Result<std::size_t>::success(static_cast<std::size_t>(value - 1));
}
bool valid_name(std::string_view name) noexcept {
  return name.size() > 1 && name.size() < 240 && name.front() == '/' &&
         name.find('/', 1) == std::string_view::npos &&
         name.find('\0') == std::string_view::npos;
}
bool valid_epoch(const IdentityBytes &epoch) noexcept {
  return std::any_of(epoch.begin(), epoch.end(),
                     [](std::byte value) { return value != std::byte{0}; });
}
Result<std::size_t> mapping_size(RawCapacities capacities) {
  std::uint64_t total = sizeof(SharedHeader);
  const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());
  for (const auto capacity : capacities) {
    if (capacity < raw_envelope_size || capacity > maximum - total) {
      return Result<std::size_t>::failure({ErrorCode::capacity});
    }
    total += capacity;
  }
  return Result<std::size_t>::success(static_cast<std::size_t>(total));
}
void ring_copy_out(const SharedHeader &header, std::size_t index,
                   std::uint64_t position, MutableByteView output) {
  const auto offset = position % header.capacities[index];
  const auto first = std::min<std::size_t>(
      output.size(), static_cast<std::size_t>(header.capacities[index] - offset));
  const auto *base =
      reinterpret_cast<const std::byte *>(&header) + header.offsets[index];
  if (first != 0) {
    std::memcpy(output.data(), base + offset, first);
  }
  if (first != output.size()) {
    std::memcpy(output.data() + first, base, output.size() - first);
  }
}
void ring_copy_in(SharedHeader &header, std::size_t index, std::uint64_t position,
                  ByteView input) {
  const auto offset = position % header.capacities[index];
  const auto first = std::min<std::size_t>(
      input.size(), static_cast<std::size_t>(header.capacities[index] - offset));
  auto *base = reinterpret_cast<std::byte *>(&header) + header.offsets[index];
  if (first != 0) {
    std::memcpy(base + offset, input.data(), first);
  }
  if (first != input.size()) {
    std::memcpy(base, input.data() + first, input.size() - first);
  }
}
Result<RawCursors> load_cursors(const SharedHeader &header, std::size_t index) {
  const auto &state = header.partitions[index];
  const auto acknowledged = state.acknowledged.value.load(std::memory_order_acquire);
  const auto published = state.published.value.load(std::memory_order_acquire);
  if (acknowledged > published || published - acknowledged > header.capacities[index]) {
    return Result<RawCursors>::failure({ErrorCode::corrupt});
  }
  return Result<RawCursors>::success({published, acknowledged});
}
Result<std::uint32_t> record_size(const SharedHeader &header, std::size_t index,
                                  std::uint64_t position, std::uint64_t end) {
  if (position > end || end - position < raw_envelope_size) {
    return Result<std::uint32_t>::failure({ErrorCode::corrupt});
  }
  std::array<std::byte, sizeof(std::uint32_t)> bytes{};
  ring_copy_out(header, index, position, bytes);
  std::uint32_t length = 0;
  std::memcpy(&length, bytes.data(), sizeof(length));
  if (length < raw_envelope_size || length > header.capacities[index] ||
      length > end - position) {
    return Result<std::uint32_t>::failure({ErrorCode::corrupt});
  }
  return Result<std::uint32_t>::success(length);
}
Result<void> boundary(const SharedHeader &header, std::size_t index, RawCursors cursors,
                      std::uint64_t wanted) {
  if (wanted < cursors.acknowledged || wanted > cursors.published) {
    return Result<void>::failure({ErrorCode::invalid_range});
  }
  auto current = cursors.acknowledged;
  while (current < wanted) {
    const auto size = record_size(header, index, current, cursors.published);
    if (!size.has_value()) {
      return Result<void>::failure(size.error());
    }
    current += size.value();
  }
  return current == wanted ? Result<void>::success()
                           : Result<void>::failure({ErrorCode::invalid_range});
}
} // namespace

Result<std::array<int, 2>> raw_socket_pair() {
  std::array<int, 2> endpoints{-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, endpoints.data()) == -1) {
    return Result<std::array<int, 2>>::failure(native_error());
  }
  for (const int endpoint : endpoints) {
    const int flags = ::fcntl(endpoint, F_GETFL);
    if (flags == -1 || ::fcntl(endpoint, F_SETFL, flags | O_NONBLOCK) == -1 ||
        ::fcntl(endpoint, F_SETFD, FD_CLOEXEC) == -1) {
      const auto error = native_error();
      for (const int descriptor : endpoints) {
        (void)::close(descriptor);
      }
      return Result<std::array<int, 2>>::failure(error);
    }
#if defined(__APPLE__)
    const int enabled = 1;
    if (::setsockopt(endpoint, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) ==
        -1) {
      const auto error = native_error();
      for (const int descriptor : endpoints) {
        (void)::close(descriptor);
      }
      return Result<std::array<int, 2>>::failure(error);
    }
#endif
  }
  return Result<std::array<int, 2>>::success(endpoints);
}
Result<void> raw_send_notice(int endpoint) {
  if (endpoint < 0) {
    return Result<void>::failure({ErrorCode::invalid_range});
  }
  constexpr std::byte notice{1};
  const auto sent = ::send(endpoint, &notice, 1, MSG_DONTWAIT | MSG_NOSIGNAL);
  if (sent == 1 || (sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
    return Result<void>::success();
  }
  return sent == -1 ? Result<void>::failure(native_error())
                    : Result<void>::failure({ErrorCode::io});
}
Result<void> raw_drain_notices(int endpoint) {
  if (endpoint < 0) {
    return Result<void>::failure({ErrorCode::invalid_range});
  }
  std::array<std::byte, 256> notices{};
  for (;;) {
    const auto count = ::recv(endpoint, notices.data(), notices.size(), MSG_DONTWAIT);
    if (count > 0) {
      continue;
    }
    if (count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return Result<void>::success();
    }
    return count == -1 ? Result<void>::failure(native_error())
                       : Result<void>::failure({ErrorCode::incomplete});
  }
}

Result<RawChannel> RawChannel::create(std::string_view name, IdentityBytes epoch,
                                      RawCapacities capacities) {
  const auto size = mapping_size(capacities);
  if (!valid_name(name) || !valid_epoch(epoch)) {
    return Result<RawChannel>::failure({ErrorCode::invalid_identity});
  }
  if (!size.has_value()) {
    return Result<RawChannel>::failure(size.error());
  }
  std::string owned_name;
  try {
    owned_name = name;
  } catch (const std::bad_alloc &) {
    return Result<RawChannel>::failure({ErrorCode::allocation});
  }
  const int descriptor =
      ::shm_open(owned_name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
  if (descriptor == -1) {
    return Result<RawChannel>::failure(native_error());
  }
  if (::ftruncate(descriptor, static_cast<off_t>(size.value())) == -1) {
    const auto error = native_error();
    (void)::close(descriptor);
    (void)::shm_unlink(owned_name.c_str());
    return Result<RawChannel>::failure(error);
  }
  void *mapping =
      ::mmap(nullptr, size.value(), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
  const auto map_error = native_error();
  (void)::close(descriptor);
  if (mapping == MAP_FAILED) {
    (void)::shm_unlink(owned_name.c_str());
    return Result<RawChannel>::failure(map_error);
  }
  auto *header = new (mapping) SharedHeader{};
  header->epoch = epoch;
  header->capacities = capacities;
  std::uint64_t offset = sizeof(SharedHeader);
  for (std::size_t index = 0; index < capacities.size(); ++index) {
    header->offsets[index] = offset;
    offset += capacities[index];
  }
  header->mapped_bytes = size.value();
  header->ready.value.store(channel_magic, std::memory_order_release);
  return Result<RawChannel>::success(RawChannel{mapping, size.value()});
}
Result<RawChannel> RawChannel::open(std::string_view name, IdentityBytes epoch,
                                    RawCapacities capacities) {
  const auto size = mapping_size(capacities);
  if (!valid_name(name) || !valid_epoch(epoch)) {
    return Result<RawChannel>::failure({ErrorCode::invalid_identity});
  }
  if (!size.has_value()) {
    return Result<RawChannel>::failure(size.error());
  }
  std::string owned_name;
  try {
    owned_name = name;
  } catch (const std::bad_alloc &) {
    return Result<RawChannel>::failure({ErrorCode::allocation});
  }
  const int descriptor = ::shm_open(owned_name.c_str(), O_RDWR, 0);
  if (descriptor == -1) {
    return Result<RawChannel>::failure(native_error());
  }
  struct stat metadata{};
  if (::fstat(descriptor, &metadata) == -1) {
    const auto error = native_error();
    (void)::close(descriptor);
    return Result<RawChannel>::failure(error);
  }
  std::uint64_t backing_bytes = size.value();
#if defined(__APPLE__)
  // Darwin fstat reports the page-rounded shm object size after ftruncate.
  const auto page_size = ::sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    (void)::close(descriptor);
    return Result<RawChannel>::failure({ErrorCode::unsupported});
  }
  const auto page_bytes = static_cast<std::uint64_t>(page_size);
  backing_bytes = ((backing_bytes + page_bytes - 1) / page_bytes) * page_bytes;
#endif
  if (metadata.st_size < 0 ||
      static_cast<std::uint64_t>(metadata.st_size) != backing_bytes ||
      metadata.st_uid != ::geteuid() || (metadata.st_mode & 0077) != 0) {
    (void)::close(descriptor);
    return Result<RawChannel>::failure({ErrorCode::conflict});
  }
  void *mapping =
      ::mmap(nullptr, size.value(), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
  const auto map_error = native_error();
  (void)::close(descriptor);
  if (mapping == MAP_FAILED) {
    return Result<RawChannel>::failure(map_error);
  }
  const auto &header = *static_cast<const SharedHeader *>(mapping);
  const auto ready = header.ready.value.load(std::memory_order_acquire);
  if (ready != channel_magic) {
    (void)::munmap(mapping, size.value());
    return Result<RawChannel>::failure(
        {ready == 0 ? ErrorCode::incomplete : ErrorCode::conflict});
  }
  std::uint64_t offset = sizeof(SharedHeader);
  bool correct_offsets = true;
  for (std::size_t index = 0; index < capacities.size(); ++index) {
    correct_offsets = correct_offsets && header.offsets[index] == offset;
    offset += capacities[index];
  }
  if (header.epoch != epoch || header.capacities != capacities ||
      header.mapped_bytes != size.value() || !correct_offsets) {
    (void)::munmap(mapping, size.value());
    return Result<RawChannel>::failure({ErrorCode::conflict});
  }
  return Result<RawChannel>::success(RawChannel{mapping, size.value()});
}
Result<void> RawChannel::unlink(std::string_view name) {
  if (!valid_name(name)) {
    return Result<void>::failure({ErrorCode::invalid_identity});
  }
  try {
    const std::string owned_name{name};
    return ::shm_unlink(owned_name.c_str()) == 0
               ? Result<void>::success()
               : Result<void>::failure(native_error());
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
RawChannel::~RawChannel() {
  if (mapping_ != nullptr) {
    (void)::munmap(mapping_, mapped_bytes_);
  }
}
RawChannel::RawChannel(RawChannel &&other) noexcept
    : mapping_(std::exchange(other.mapping_, nullptr)),
      mapped_bytes_(std::exchange(other.mapped_bytes_, 0)) {}
RawChannel &RawChannel::operator=(RawChannel &&other) noexcept {
  if (this != &other) {
    if (mapping_ != nullptr) {
      (void)::munmap(mapping_, mapped_bytes_);
    }
    mapping_ = std::exchange(other.mapping_, nullptr);
    mapped_bytes_ = std::exchange(other.mapped_bytes_, 0);
  }
  return *this;
}
const IdentityBytes &RawChannel::epoch() const noexcept {
  return static_cast<const SharedHeader *>(mapping_)->epoch;
}
RawCapacities RawChannel::capacities() const noexcept {
  return static_cast<const SharedHeader *>(mapping_)->capacities;
}
Result<RawCursors> RawChannel::cursors(RawPartition partition) const {
  const auto index = partition_index(partition);
  if (mapping_ == nullptr || !index.has_value()) {
    return Result<RawCursors>::failure({ErrorCode::invalid_range});
  }
  return load_cursors(*static_cast<const SharedHeader *>(mapping_), index.value());
}
Result<RawRead> RawChannel::read(RawPartition partition, std::uint64_t start,
                                 std::size_t limit) const {
  const auto index = partition_index(partition);
  const auto cursor = cursors(partition);
  if (!index.has_value() || !cursor.has_value()) {
    return Result<RawRead>::failure(!index.has_value() ? index.error()
                                                       : cursor.error());
  }
  const auto &header = *static_cast<const SharedHeader *>(mapping_);
  const auto checked = boundary(header, index.value(), cursor.value(), start);
  if (!checked.has_value()) {
    return Result<RawRead>::failure(checked.error());
  }
  auto end = start;
  while (end < cursor.value().published) {
    const auto length =
        record_size(header, index.value(), end, cursor.value().published);
    if (!length.has_value()) {
      return Result<RawRead>::failure(length.error());
    }
    if (length.value() > limit - static_cast<std::size_t>(end - start)) {
      if (end == start) {
        return Result<RawRead>::failure({ErrorCode::capacity});
      }
      break;
    }
    end += length.value();
  }
  try {
    RawRead result{start, end,
                   std::vector<std::byte>(static_cast<std::size_t>(end - start))};
    ring_copy_out(header, index.value(), start, result.bytes);
    return Result<RawRead>::success(std::move(result));
  } catch (const std::bad_alloc &) {
    return Result<RawRead>::failure({ErrorCode::allocation});
  }
}
Result<void> RawChannel::acknowledge(RawPartition partition, std::uint64_t end,
                                     int recorder_endpoint) {
  const auto index = partition_index(partition);
  const auto cursor = cursors(partition);
  if (!index.has_value() || !cursor.has_value()) {
    return Result<void>::failure(!index.has_value() ? index.error() : cursor.error());
  }
  auto &header = *static_cast<SharedHeader *>(mapping_);
  const auto checked = boundary(header, index.value(), cursor.value(), end);
  if (!checked.has_value()) {
    return checked;
  }
  header.partitions[index.value()].acknowledged.value.store(end,
                                                            std::memory_order_release);
  const auto armed = header.space_armed.value.exchange(0, std::memory_order_acq_rel);
  return armed == 0 ? Result<void>::success() : raw_send_notice(recorder_endpoint);
}
Result<void> RawChannel::arm_data() {
  if (mapping_ == nullptr) {
    return Result<void>::failure({ErrorCode::invalid_range});
  }
  auto &header = *static_cast<SharedHeader *>(mapping_);
  (void)header.data_armed.value.exchange(1, std::memory_order_acq_rel);
  return Result<void>::success();
}
Result<void> RawChannel::arm_space() {
  if (mapping_ == nullptr) {
    return Result<void>::failure({ErrorCode::invalid_range});
  }
  auto &header = *static_cast<SharedHeader *>(mapping_);
  (void)header.space_armed.value.exchange(1, std::memory_order_acq_rel);
  return Result<void>::success();
}

RawProducer::RawProducer(RawChannel &channel, int producer_endpoint)
    : RawProducer(channel, Start{producer_endpoint, 1}) {}
RawProducer::RawProducer(RawChannel &channel, Start start)
    : channel_(&channel), endpoint_(start.producer_endpoint),
      next_ordinal_(start.next_ordinal) {
  if (channel.mapping_ != nullptr) {
    const auto &header = *static_cast<const SharedHeader *>(channel.mapping_);
    for (std::size_t index = 0; index < published_.size(); ++index) {
      published_[index] =
          header.partitions[index].published.value.load(std::memory_order_acquire);
    }
  }
}
Result<std::uint64_t> RawProducer::available(RawPartition partition) const {
  const auto index = partition_index(partition);
  const auto cursor = channel_->cursors(partition);
  if (!index.has_value() || !cursor.has_value()) {
    return Result<std::uint64_t>::failure(!index.has_value() ? index.error()
                                                             : cursor.error());
  }
  if (cursor.value().published != published_[index.value()]) {
    return Result<std::uint64_t>::failure({ErrorCode::conflict});
  }
  const auto capacity = channel_->capacities()[index.value()];
  const auto free = capacity - (cursor.value().published - cursor.value().acknowledged);
  const auto held = index.value() == 1 ? held_control_ : 0;
  if (reserved_[index.value()] > free || held > free - reserved_[index.value()]) {
    return Result<std::uint64_t>::failure({ErrorCode::corrupt});
  }
  return Result<std::uint64_t>::success(free - reserved_[index.value()] - held);
}
Result<void> RawProducer::reserve(RawPartition partition, std::uint64_t bytes) {
  const auto admission = admission_ready(partition);
  if (!admission.has_value()) {
    return admission;
  }
  const auto free = available(partition);
  if (!free.has_value()) {
    return Result<void>::failure(free.error());
  }
  if (bytes > free.value()) {
    return Result<void>::failure({ErrorCode::capacity});
  }
  reserved_[partition_index(partition).value()] += bytes;
  return Result<void>::success();
}
void RawProducer::release_reservation(RawPartition partition) noexcept {
  const auto index = partition_index(partition);
  if (index.has_value()) {
    reserved_[index.value()] = 0;
  }
}
Result<RawControlClaim> RawProducer::hold_control(std::uint64_t bytes) {
  const auto admission = admission_ready(RawPartition::control);
  if (!admission.has_value()) {
    return Result<RawControlClaim>::failure(admission.error());
  }
  const auto free = available(RawPartition::control);
  if (!free.has_value()) {
    return Result<RawControlClaim>::failure(free.error());
  }
  if (bytes > free.value()) {
    return Result<RawControlClaim>::failure({ErrorCode::capacity});
  }
  held_control_ += bytes;
  return Result<RawControlClaim>::success(RawControlClaim{this, bytes});
}
Result<void> RawProducer::admission_ready(RawPartition partition) const {
  const auto index = partition_index(partition);
  if (!index.has_value() || channel_->mapping_ == nullptr) {
    return Result<void>::failure({ErrorCode::invalid_range});
  }
  const auto capacities = channel_->capacities();
  std::uint64_t event_margin = 0;
  for (std::size_t partition_slot = 0; partition_slot < capacities.size();
       ++partition_slot) {
    const auto capacity = capacities[partition_slot];
    // Every complete event occupies at least16 bytes. The full-capacity margin
    // covers all existing reservations and held claims in all partitions.
    event_margin += capacity / raw_envelope_size;
    if (published_[partition_slot] > UINT64_MAX - capacity) {
      return Result<void>::failure({ErrorCode::overflow});
    }
  }
  if (next_ordinal_ == 0 || UINT64_MAX - next_ordinal_ < event_margin) {
    return Result<void>::failure({ErrorCode::overflow});
  }
  return Result<void>::success();
}
Result<std::uint64_t> RawProducer::emit(std::uint32_t site_id,
                                        std::span<const ByteView> raw_parts,
                                        RawPartition partition,
                                        RawControlClaim *claim) {
  if (site_id == 0) {
    return Result<std::uint64_t>::failure({ErrorCode::invalid_identity});
  }
  const auto index = partition_index(partition);
  if (!index.has_value() || channel_->mapping_ == nullptr) {
    return Result<std::uint64_t>::failure({ErrorCode::invalid_range});
  }
  if (next_ordinal_ == 0 || next_ordinal_ == UINT64_MAX) {
    return Result<std::uint64_t>::failure({ErrorCode::overflow});
  }
  std::uint64_t size = raw_envelope_size;
  for (const auto part : raw_parts) {
    if (part.size() > UINT32_MAX - size) {
      return Result<std::uint64_t>::failure({ErrorCode::overflow});
    }
    size += part.size();
  }
  auto &header = *static_cast<SharedHeader *>(channel_->mapping_);
  const auto position = published_[index.value()];
  if (size > header.capacities[index.value()]) {
    return Result<std::uint64_t>::failure({ErrorCode::capacity});
  }
  if (size > UINT64_MAX - position) {
    return Result<std::uint64_t>::failure({ErrorCode::overflow});
  }
  if (claim != nullptr) {
    if (partition != RawPartition::control || claim->owner_ != this) {
      return Result<std::uint64_t>::failure({ErrorCode::conflict});
    }
    if (size > claim->remaining_) {
      return Result<std::uint64_t>::failure({ErrorCode::capacity});
    }
  } else if (size > reserved_[index.value()]) {
    return Result<std::uint64_t>::failure({ErrorCode::capacity});
  }
  // Ownership/admission are caller obligations; validate publication before any
  // copies to fail closed on an accidentally concurrent producer attachment.
  if (header.partitions[index.value()].published.value.load(
          std::memory_order_relaxed) != position) {
    return Result<std::uint64_t>::failure({ErrorCode::conflict});
  }
  const auto length = static_cast<std::uint32_t>(size);
  const auto ordinal = next_ordinal_;
  std::array<std::byte, raw_envelope_size> envelope{};
  std::memcpy(envelope.data(), &length, sizeof(length));
  std::memcpy(envelope.data() + 4, &site_id, sizeof(site_id));
  std::memcpy(envelope.data() + 8, &ordinal, sizeof(ordinal));
  ring_copy_in(header, index.value(), position, envelope);
  auto body_position = position + raw_envelope_size;
  for (const auto part : raw_parts) {
    ring_copy_in(header, index.value(), body_position, part);
    body_position += part.size();
  }
  if (claim != nullptr) {
    claim->remaining_ -= size;
    held_control_ -= size;
  } else {
    reserved_[index.value()] -= size;
  }
  published_[index.value()] = position + size;
  ++next_ordinal_;
  header.partitions[index.value()].published.value.store(position + size,
                                                         std::memory_order_release);
  const auto armed = header.data_armed.value.exchange(0, std::memory_order_acq_rel);
  if (armed != 0) {
    const auto notice = raw_send_notice(endpoint_);
    if (!notice.has_value()) {
      notice_error_ = notice.error();
    }
  }
  return Result<std::uint64_t>::success(ordinal);
}
RawControlClaim::~RawControlClaim() {
  if (owner_ != nullptr) {
    owner_->held_control_ -= remaining_;
  }
}
RawControlClaim::RawControlClaim(RawControlClaim &&other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      remaining_(std::exchange(other.remaining_, 0)) {}
RawControlClaim &RawControlClaim::operator=(RawControlClaim &&other) noexcept {
  if (this != &other) {
    if (owner_ != nullptr) {
      owner_->held_control_ -= remaining_;
    }
    owner_ = std::exchange(other.owner_, nullptr);
    remaining_ = std::exchange(other.remaining_, 0);
  }
  return *this;
}
} // namespace blackbird
