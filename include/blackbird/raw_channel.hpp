#pragma once

#include "blackbird/foundation.hpp"
#include "blackbird/raw_format.hpp"

#include <array>
#include <atomic>
#include <optional>
#include <string_view>
#include <sys/types.h>

namespace blackbird {

using RawCapacities = std::array<std::uint64_t, 3>;
struct RawCursors {
  std::uint64_t published;
  std::uint64_t acknowledged;
};
struct RawRead {
  std::uint64_t start;
  std::uint64_t end;
  std::vector<std::byte> bytes;
};

// Setup owns descriptor lifetime. The transport borrows an endpoint; launcher
// copies must survive recorder replacement. Neither endpoint identifies death.
Result<std::array<int, 2>> raw_socket_pair();
Result<void> raw_drain_notices(int endpoint);
Result<void> raw_send_notice(int endpoint);

class RawProducer;
class RawCapacityClaim;
using RawControlClaim = RawCapacityClaim;

// Prepared outside a signal handler. The native publication path calls only
// lock-free atomics and POSIX memcpy/send; it has no standard-library wrappers.
struct RawNativePart {
  const void *bytes;
  std::size_t size;
};
struct RawNativePublication {
  std::uint64_t ordinal = 0;
  Error error{};
  Error notice_error{};
  bool published = false;
  bool notice_failed = false;
};
class RawNativePublisher {
public:
  RawNativePublication emit(std::uint32_t site, const RawNativePart *parts,
                            std::size_t count) noexcept;

private:
  friend class RawProducer;
  struct Record {
    std::uint32_t length, site;
    std::uint64_t ordinal;
  };
  RawNativePublication begin(const Record &record) noexcept;
  RawNativePublication finish(const Record &record) noexcept;
  unsigned char *ring_ = nullptr;
  std::uint64_t capacity_ = 0;
  std::uint64_t *published_ = nullptr, *ordinal_ = nullptr;
  std::uint64_t *remaining_ = nullptr, *held_ = nullptr;
  std::atomic<std::uint64_t> *publication_ = nullptr, *armed_ = nullptr;
  int endpoint_ = -1;
  ssize_t (*send_)(int, const void *, std::size_t, int) = nullptr;
};

// One execution owner publishes; one independently fenced recorder reclaims.
// Mapping destruction only unmaps. Explicit unlink follows registered closure.
class RawChannel {
public:
  static Result<RawChannel> create(std::string_view name, IdentityBytes epoch,
                                   RawCapacities capacities);
  static Result<RawChannel> open(std::string_view name, IdentityBytes epoch,
                                 RawCapacities capacities);
  static Result<void> unlink(std::string_view name);
  ~RawChannel();
  RawChannel(const RawChannel &) = delete;
  RawChannel &operator=(const RawChannel &) = delete;
  RawChannel(RawChannel &&other) noexcept;
  RawChannel &operator=(RawChannel &&other) noexcept;
  const IdentityBytes &epoch() const noexcept;
  RawCapacities capacities() const noexcept;
  Result<RawCursors> cursors(RawPartition partition) const;
  // Copies only a complete prefix, unchanged, into recorder-owned storage.
  // A limit smaller than the first event returns capacity, never a partial event.
  Result<RawRead> read(RawPartition partition, std::uint64_t start,
                       std::size_t limit) const;
  // Caller has preserved [D,end) on disk. end must be an event boundary.
  // Notification failure does not roll back D; inspect cursors before retrying.
  Result<void> acknowledge(RawPartition partition, std::uint64_t end,
                           int recorder_endpoint = -1);
  // Only after every producer is fenced dead: validate/preserve the same event
  // boundary, clear an obsolete space arm, and omit a wake to the dead producer.
  Result<void> acknowledge_quiescent(RawPartition partition, std::uint64_t end);
  // Drain old notices BEFORE arming. After arming, recheck all publication
  // cursors; sleep only if empty, without draining again. Runtime also watches
  // producer death, covering death between publication and notice.
  Result<void> arm_data();
  Result<void> arm_space();

private:
  friend class RawProducer;
  Result<bool> advance_acknowledgment(RawPartition partition, std::uint64_t end);
  RawChannel(void *mapping, std::size_t mapped_bytes)
      : mapping_(mapping), mapped_bytes_(mapped_bytes) {}
  void *mapping_ = nullptr;
  std::size_t mapped_bytes_ = 0;
};

// Capacity only: no physical cursor reservation, no unpublished hole. A claim
// must not outlive its producer. It can remain held through asynchronous yields.
class RawCapacityClaim {
public:
  ~RawCapacityClaim();
  RawCapacityClaim(const RawCapacityClaim &) = delete;
  RawCapacityClaim &operator=(const RawCapacityClaim &) = delete;
  RawCapacityClaim(RawCapacityClaim &&other) noexcept;
  RawCapacityClaim &operator=(RawCapacityClaim &&other) noexcept;
  std::uint64_t remaining() const noexcept { return remaining_; }
  // Transfer existing credit; no admission, allocation or aggregate change.
  Result<RawCapacityClaim> split(std::uint64_t amount);

private:
  friend class RawProducer;
  RawCapacityClaim(RawProducer *owner, RawPartition partition, std::uint64_t bytes)
      : owner_(owner), remaining_(bytes), partition_(partition) {}
  RawProducer *owner_;
  std::uint64_t remaining_;
  RawPartition partition_;
};

// Caller fences a single producer, keeps channel alive, and uses one execution
// context. reserve/hold_capacity happen at safe boundaries before owned locks.
class RawProducer {
public:
  struct Start {
    int producer_endpoint = -1;
    std::uint64_t next_ordinal = 1;
  };
  explicit RawProducer(RawChannel &channel, int producer_endpoint = -1);
  RawProducer(RawChannel &channel, Start start);
  RawProducer(const RawProducer &) = delete;
  RawProducer &operator=(const RawProducer &) = delete;
  RawProducer(RawProducer &&) = delete;
  RawProducer &operator=(RawProducer &&) = delete;
  Result<std::uint64_t> available(RawPartition partition) const;
  // Sole-owner reservations/claims cannot be released by waiting for a recorder.
  Result<std::uint64_t> maximum_grant(RawPartition partition) const;
  // Epoch admission is separate from physical free bytes. Check before
  // acquisition, even when a previous owner region already holds terminals.
  Result<void> admission_ready(RawPartition partition) const;
  // Overflow closes NEW admission with a full-capacity margin. Existing prepaid
  // and held work can finish; owner then drains/closes and registers a new epoch.
  Result<void> reserve(RawPartition partition, std::uint64_t bytes);
  void release_reservation(RawPartition partition) noexcept;
  Result<RawCapacityClaim> hold_capacity(RawPartition partition, std::uint64_t bytes);
  Result<RawControlClaim> hold_control(std::uint64_t bytes);
  // Only raw byte copies and native envelope fields; no allocation, locking,
  // wait, transform or syscall unless an armed consumer requires a doorbell.
  // Success always means published, even if notification fails. That distinct
  // error is retained in notification_error(), to fence runtime admission.
  Result<std::uint64_t> emit(std::uint32_t site_id, std::span<const ByteView> raw_parts,
                             RawPartition partition = RawPartition::data,
                             RawControlClaim *claim = nullptr);
  // Owner retains the producer and credit through every native use. Preparation
  // is ordinary owner work; emit on the returned POD view is signal-safe.
  Result<RawNativePublisher> prepare_native(RawPartition partition,
                                            RawControlClaim *claim = nullptr);
  std::optional<Error> notification_error() const noexcept { return notice_error_; }
  void clear_notification_error() noexcept { notice_error_.reset(); }

private:
  friend class RawCapacityClaim;
  RawChannel *channel_;
  int endpoint_;
  std::array<std::uint64_t, 3> published_{};
  std::array<std::uint64_t, 3> reserved_{};
  std::array<std::uint64_t, 3> held_{};
  std::uint64_t next_ordinal_;
  std::optional<Error> notice_error_;
};

} // namespace blackbird
