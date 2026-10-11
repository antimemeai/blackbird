#include "blackbird/local_timing.hpp"
#include "blackbird/raw_runtime.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <exception>
#include <span>

namespace blackbird {
namespace {
struct RestoreErrno {
  int saved = errno;
  ~RestoreErrno() { errno = saved; }
};
template <class T> ByteView native_bytes(const T &value) noexcept {
  return std::as_bytes(std::span{&value, std::size_t{1}});
}
constexpr std::uint64_t clock_body_bytes =
    2 * (sizeof(clockid_t) + 2 * sizeof(int) + sizeof(time_t) + sizeof(long)) +
    sizeof(bool);
constexpr std::uint64_t begin_record_bytes =
    raw_envelope_size + clock_body_bytes + sizeof(int);
constexpr std::uint64_t end_record_bytes = raw_envelope_size + sizeof(std::uint64_t) +
                                           clock_body_bytes + sizeof(int) +
                                           sizeof(RawRegionEnd);
constexpr std::uint64_t outcome_record_bytes =
    raw_envelope_size + sizeof(std::uint64_t) + sizeof(RawRegionOutcome);
constexpr std::uint64_t error_record_bytes = raw_envelope_size + sizeof(std::uint64_t) +
                                             sizeof(ErrorCode) + sizeof(std::int64_t);
auto clock_fields(const RawClockSample &clock) noexcept {
  return std::array{native_bytes(clock.monotonic.method),
                    native_bytes(clock.monotonic.result),
                    native_bytes(clock.monotonic.error),
                    native_bytes(clock.monotonic.value.tv_sec),
                    native_bytes(clock.monotonic.value.tv_nsec),
                    native_bytes(clock.cpu_read),
                    native_bytes(clock.thread_cpu.method),
                    native_bytes(clock.thread_cpu.result),
                    native_bytes(clock.thread_cpu.error),
                    native_bytes(clock.thread_cpu.value.tv_sec),
                    native_bytes(clock.thread_cpu.value.tv_nsec)};
}
void require_emission(Result<std::uint64_t> emitted) {
  if (!emitted.has_value())
    throw emitted.error();
}
void terminal_emission(Result<std::uint64_t> emitted) noexcept {
  if (!emitted.has_value())
    raw_fence(emitted.error());
}
void require_data_admission(std::uint64_t bytes) {
  const RestoreErrno restore;
  const auto admitted = raw_wait_data(bytes);
  if (!admitted.has_value()) {
    if (admitted.error().code != ErrorCode::interrupted)
      raw_fence(admitted.error());
    throw admitted.error();
  }
}
void require_data_admission(std::span<const ByteView> fields) {
  std::uint64_t bytes = raw_envelope_size;
  for (const auto field : fields) {
    if (field.size() > UINT32_MAX - bytes) {
      raw_fence({ErrorCode::overflow});
      throw Error{ErrorCode::overflow};
    }
    bytes += field.size();
  }
  require_data_admission(bytes);
}
} // namespace

RawRegion::RawRegion(const RawRegionSites &sites, RawRegionOptions options)
    : sites_(&sites), options_(options) {
  const RestoreErrno restore;
  if (!raw_enabled())
    return;
  auto held =
      raw_claim_control(end_record_bytes + outcome_record_bytes + error_record_bytes);
  if (!held.has_value()) {
    if (held.error().code != ErrorCode::interrupted)
      raw_fence(held.error());
    throw held.error();
  }
  claim_.emplace(std::move(held).value());
  require_data_admission(begin_record_bytes);
  const auto clock = raw_local_clock(options_.cpu);
  const auto fields = clock_fields(clock);
  const auto exceptions = std::uncaught_exceptions();
  std::array<ByteView, 12> body{};
  std::copy(fields.begin(), fields.end(), body.begin());
  body.back() = native_bytes(exceptions);
  auto emitted = raw_emit(sites_->begin, body, RawPartition::data);
  if (!emitted.has_value())
    throw emitted.error();
  begin_ = emitted.value();
}
void RawRegion::observe(const RawRegionObservation &observation) {
  const RestoreErrno restore;
  if (!enabled())
    return;
  const std::array fields{native_bytes(begin_), native_bytes(observation.kind),
                          native_bytes(observation.history),
                          native_bytes(observation.bytes),
                          native_bytes(observation.sequence)};
  require_data_admission(fields);
  require_emission(raw_emit(sites_->observation, fields, RawPartition::data));
}
void RawRegion::linkage(const RawRegionLinkage &linkage) {
  const RestoreErrno restore;
  if (!enabled())
    return;
  const auto revision_bytes = linkage.revision.size();
  const auto attempt_bytes = linkage.attempt.size();
  const std::array fields{native_bytes(begin_), native_bytes(revision_bytes),
                          linkage.revision, native_bytes(attempt_bytes),
                          linkage.attempt};
  require_data_admission(fields);
  require_emission(raw_emit(sites_->linkage, fields, RawPartition::data));
}
void RawRegion::outcome(RawRegionOutcome outcome) noexcept {
  const RestoreErrno restore;
  if (!claim_.has_value())
    return;
  if (outcome_written_) {
    raw_fence({ErrorCode::conflict, sites_->outcome});
    return;
  }
  outcome_written_ = true;
  const std::array fields{native_bytes(begin_), native_bytes(outcome)};
  terminal_emission(raw_emit(sites_->outcome, fields, RawPartition::control, &*claim_));
}
void RawRegion::error(Error error) noexcept {
  const RestoreErrno restore;
  if (!claim_.has_value())
    return;
  if (error_written_) {
    raw_fence({ErrorCode::conflict, 1110});
    return;
  }
  error_written_ = true;
  const std::array fields{native_bytes(begin_), native_bytes(error.code),
                          native_bytes(error.detail)};
  terminal_emission(raw_emit(1110, fields, RawPartition::control, &*claim_));
}
void RawRegion::finish(RawRegionEnd reason) noexcept {
  const RestoreErrno restore;
  if (!claim_.has_value())
    return;
  const auto clock = raw_local_clock(options_.cpu);
  const auto fields = clock_fields(clock);
  const auto exceptions = std::uncaught_exceptions();
  std::array<ByteView, 14> body{};
  body.front() = native_bytes(begin_);
  std::copy(fields.begin(), fields.end(), body.begin() + 1);
  body[12] = native_bytes(exceptions);
  body.back() = native_bytes(reason);
  terminal_emission(raw_emit(sites_->end, body, RawPartition::control, &*claim_));
  claim_.reset();
}
std::span<const RawRegionSites> raw_region_descriptions() noexcept {
  return raw_regions::definitions;
}
RawWorkerDispatch raw_worker_dispatch() {
  const RestoreErrno restore;
  if (!raw_enabled())
    return {};
  require_data_admission(raw_envelope_size + clock_body_bytes);
  const auto lane = raw_lane_epoch();
  const auto clock = raw_local_clock(false);
  const auto fields = clock_fields(clock);
  auto emitted = raw_emit(1100, fields);
  if (!emitted.has_value())
    throw emitted.error();
  return {lane, emitted.value()};
}
void raw_worker_started(const RawWorkerDispatch &dispatch) {
  const RestoreErrno restore;
  if (!raw_enabled())
    return;
  require_data_admission(raw_envelope_size + sizeof(IdentityBytes) +
                         sizeof(std::uint64_t) + clock_body_bytes);
  const auto clock = raw_local_clock(false);
  const auto fields = clock_fields(clock);
  std::array<ByteView, 13> body{};
  body[0] = dispatch.lane;
  body[1] = native_bytes(dispatch.ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  require_emission(raw_emit(1101, body));
}
} // namespace blackbird
