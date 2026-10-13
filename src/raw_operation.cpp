#include "blackbird/raw_operation.hpp"
#include "blackbird/raw_runtime.hpp"
#include <algorithm>
#include <array>
#include <cerrno>

namespace blackbird {
namespace {
using detail::raw_native_field;
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
constexpr std::uint64_t E = raw_envelope_size, R = provider_ref_bytes,
                        ID = sizeof(IdentityBytes),
                        error_bytes = sizeof(ErrorCode) + sizeof(std::int64_t),
                        count_bytes = sizeof(std::size_t), flag = sizeof(bool),
                        byte = sizeof(std::uint8_t);
constexpr std::uint64_t clock_bytes =
    2 * (sizeof(clockid_t) + 2 * sizeof(int) + sizeof(time_t) + sizeof(long)) + flag;
constexpr std::uint64_t original_ref = R + sizeof(RawPartition) + 2 * count_bytes;
constexpr std::uint64_t fixed_credit =
    (E + flag + R + ID + count_bytes + 2 * flag + clock_bytes) + (E + R + ID) +
    (E + R + byte + flag + error_bytes) + 5 * (E + R + byte + ID) +
    (E + R + byte + ID + 2 * sizeof(std::uint64_t) + count_bytes) +
    (E + R + 2 * byte + ID + 2 * sizeof(std::uint64_t)) +
    (E + R + 4 * ID + count_bytes) + (E + R + byte + flag + error_bytes) +
    (E + R + 3 * flag + 2 * error_bytes) + 4 * (E + R + byte + flag + original_ref) +
    (E + R + byte + flag + R) +
    2 * ((E + R + byte + ID + ID + 2 * sizeof(std::uint64_t)) +
         (E + R + 2 * byte + ID + sizeof(std::uint64_t))) +
    (E + R + sizeof(AttemptDisposition) + 4 * flag + error_bytes) +
    2 * ((E + R + 2 * flag + ID + sizeof(AttemptPhase) + sizeof(AttemptDisposition) +
          count_bytes) +
         (E + R + byte + flag + ID + sizeof(std::uint64_t) +
          sizeof(RetainedEvidence))) +
    7 * ((E + R + byte + flag) + (E + R + 2 * byte + error_bytes)) +
    (E + R + 2 * byte + 2 * flag + error_bytes + sizeof(AttemptDisposition) +
     clock_bytes) +
    3 * ((E + R + byte + 3 * count_bytes) + (E + R + 2 * byte + error_bytes)) +
    (E + R + byte + count_bytes + 53) + (E + R + byte + count_bytes + 90);
static_assert(fixed_credit == 3290);
std::uint64_t add(std::uint64_t a, std::uint64_t b) {
  if (b > UINT64_MAX - a)
    throw Error{ErrorCode::overflow};
  return a + b;
}
std::uint64_t multiply(std::uint64_t a, std::uint64_t b) {
  if (a && b > UINT64_MAX / a)
    throw Error{ErrorCode::overflow};
  return a * b;
}
std::uint64_t ceil_div(std::uint64_t a, std::uint64_t b) {
  return a / b + static_cast<std::uint64_t>(a % b != 0);
}
template <class T> T take(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
auto clock_fields(const RawClockSample &clock) noexcept {
  return std::array{raw_native_field(clock.monotonic.method),
                    raw_native_field(clock.monotonic.result),
                    raw_native_field(clock.monotonic.error),
                    raw_native_field(clock.monotonic.value.tv_sec),
                    raw_native_field(clock.monotonic.value.tv_nsec),
                    raw_native_field(clock.cpu_read),
                    raw_native_field(clock.thread_cpu.method),
                    raw_native_field(clock.thread_cpu.result),
                    raw_native_field(clock.thread_cpu.error),
                    raw_native_field(clock.thread_cpu.value.tv_sec),
                    raw_native_field(clock.thread_cpu.value.tv_nsec)};
}
} // namespace
Result<std::uint64_t> raw_operation_append_credit(JournalLimits limits,
                                                  JournalCapacity capacity) noexcept {
  try {
    const std::uint64_t M = limits.max_payload, N = limits.max_batch_bytes,
                        B = ValueLimits{}.bytes;
    if (!M || N < add(M, 88))
      return Result<std::uint64_t>::failure({ErrorCode::invalid_range});
    const auto L =
        M < 20 ? 0 : std::min(capacity.max_file_bytes, multiply(M, (M - 20) / 24));
    struct Shape {
      std::uint64_t count, sources, events, proposal;
    };
    const std::array shapes{Shape{5, 0, 1, 68},
                            Shape{1, 0, 3,
                                  add(60, add(std::min(M, add(B, 128)),
                                              add(std::min(M, add(B, 60)),
                                                  std::min(M, std::uint64_t{56}))))},
                            Shape{1, 0, 1, 76},
                            Shape{1, 0, 1, 88},
                            Shape{1, 1, 1, add(56, add(B, std::min(M, add(B, 54))))},
                            Shape{1, 1, 1, add(146, std::min(M, std::uint64_t{129}))},
                            Shape{3, 0, 1, add(52, std::min(M, std::uint64_t{112}))}};
    std::uint64_t total = 0;
    for (const auto shape : shapes) {
      const auto q = std::min(shape.proposal, L);
      std::uint64_t diagnostic = 0;
      if (q >= 48) {
        const auto C = ceil_div(q, M), K = (N - 56) / (M + 32), G = ceil_div(C, K);
        if (add(q, add(multiply(56, C), 118)) <= N)
          diagnostic = take(raw_retained_region_credit(static_cast<std::size_t>(C), 1));
        else
          diagnostic = add(multiply(take(raw_retained_region_credit(0, 0)), add(G, 1)),
                           multiply(take(raw_retained_region_credit(1, 0)) -
                                        take(raw_retained_region_credit(0, 0)),
                                    add(C, 1)));
      }
      total = add(
          total, multiply(shape.count, add(take(raw_retained_region_credit(
                                               static_cast<std::size_t>(shape.sources),
                                               static_cast<std::size_t>(shape.events))),
                                           diagnostic)));
    }
    return Result<std::uint64_t>::success(total);
  } catch (const Error &error) {
    return Result<std::uint64_t>::failure(error);
  }
}
RawOperationTrace::RawOperationTrace(const RetainedState &root) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return;
  const auto regions =
      take(raw_operation_append_credit(root.journal_limits(), root.journal_capacity()));
  auto bank = take(raw_claim_control(add(fixed_credit, regions)));
  owner_.emplace(take(bank.split(fixed_credit)));
  regions_.emplace(std::move(bank));
}
RawOperationTrace::~RawOperationTrace() { finish(RawOperationBranch::unfinished); }
void RawOperationTrace::start(std::optional<RawEventRef> parent,
                              DefinitionGenerationId generation, std::size_t depth,
                              bool pumping, bool stopped) {
  const ErrnoGuard guard;
  if (!owner_)
    return;
  const bool present = parent && parent->ordinal;
  std::array<ByteView, 19> fields{};
  std::size_t n = 0;
  fields[n++] = raw_native_field(present);
  if (present) {
    fields[n++] = parent->lane;
    fields[n++] = raw_native_field(parent->ordinal);
  }
  fields[n++] = generation.bytes();
  fields[n++] = raw_native_field(depth);
  fields[n++] = raw_native_field(pumping);
  fields[n++] = raw_native_field(stopped);
  const auto clock = raw_local_clock(true);
  const auto clocks = clock_fields(clock);
  std::copy(clocks.begin(), clocks.end(),
            fields.begin() + static_cast<std::ptrdiff_t>(n));
  n += clocks.size();
  request_ = {raw_lane_epoch(), take(raw_emit(2400, std::span{fields}.first(n),
                                              RawPartition::control, &*owner_))};
}
void RawOperationTrace::event(std::uint32_t site, std::span<const ByteView> fields) {
  const ErrnoGuard guard;
  if (!owner_ || !request_.ordinal)
    return;
  std::array<ByteView, 18> body{};
  if (fields.size() > body.size() - 2)
    throw Error{ErrorCode::overflow};
  body[0] = request_.lane;
  body[1] = raw_native_field(request_.ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  (void)take(raw_emit(site, std::span{body}.first(fields.size() + 2),
                      RawPartition::control, &*owner_));
}
void RawOperationTrace::name(std::string_view value) {
  const ErrnoGuard guard;
  if (!owner_)
    return;
  if (value.size() > UINT32_MAX - 48)
    throw Error{ErrorCode::overflow};
  auto credit = take(raw_claim_data(48 + value.size()));
  const auto length = value.size();
  const std::array fields{ByteView{request_.lane}, raw_native_field(request_.ordinal),
                          raw_native_field(length), detail::raw_string_field(value)};
  (void)take(raw_emit(2401, fields, RawPartition::data, &credit));
}
void RawOperationTrace::identity(std::uint8_t kind, const IdentityBytes &value) {
  const std::array fields{raw_native_field(kind), ByteView{value}};
  event(2403, fields);
}
std::optional<ProviderOriginalReference>
RawOperationTrace::original(RawOperationOriginal role, ByteView bytes,
                            RawCapacityClaim *data,
                            std::optional<RawEventRef> provider) {
  const ErrnoGuard guard;
  if (!owner_)
    return {};
  const auto ref = provider ? *provider : request_;
  const auto kind =
      provider ? static_cast<std::uint8_t>(ProviderOriginalRole::upstream_packet)
               : static_cast<std::uint8_t>(role);
  const auto length = bytes.size();
  if (length > UINT32_MAX - 49)
    throw Error{ErrorCode::overflow};
  const std::array fields{ByteView{ref.lane}, raw_native_field(ref.ordinal),
                          raw_native_field(kind), raw_native_field(length), bytes};
  const auto partition = data ? RawPartition::data : RawPartition::control;
  const auto ordinal =
      take(raw_emit(provider ? 1322 : 2402, fields, partition, data ? data : &*owner_));
  return ProviderOriginalReference{{raw_lane_epoch(), ordinal}, partition, 49, length};
}
void RawOperationTrace::source(
    RawOperationOriginal role,
    const std::optional<ProviderOriginalReference> &original) {
  const auto kind = static_cast<std::uint8_t>(role);
  const bool present = original.has_value();
  std::array<ByteView, 7> fields{
      raw_native_field(kind), raw_native_field(present), {}, {}, {}, {}, {}};
  std::size_t n = 2;
  if (original) {
    fields[n++] = original->event.lane;
    fields[n++] = raw_native_field(original->event.ordinal);
    fields[n++] = raw_native_field(original->partition);
    fields[n++] = raw_native_field(original->record_offset);
    fields[n++] = raw_native_field(original->bytes);
  }
  event(2409, std::span{fields}.first(n));
}
void RawOperationTrace::region(RawEventRef region) {
  const auto kind = std::uint8_t{4};
  const bool present = region.ordinal != 0;
  std::array<ByteView, 4> fields{raw_native_field(kind), raw_native_field(present),
                                 ByteView{region.lane},
                                 raw_native_field(region.ordinal)};
  event(2409, std::span{fields}.first(present ? 4 : 2));
}
void RawOperationTrace::callback_return(std::uint8_t kind, RawOperationBranch branch,
                                        const Error *error, const bool *value) {
  std::array<ByteView, 4> fields{
      raw_native_field(kind), raw_native_field(branch), {}, {}};
  std::size_t n = 2;
  if (error) {
    fields[n++] = raw_native_field(error->code);
    fields[n++] = raw_native_field(error->detail);
  }
  if (value)
    fields[n++] = raw_native_field(*value);
  event(2416, std::span{fields}.first(n));
}
void RawOperationTrace::finish(RawOperationBranch branch, const Error *error) noexcept {
  const ErrnoGuard guard;
  if (!owner_ || !request_.ordinal)
    return;
  const bool failed = error != nullptr, selected = disposition_.has_value();
  const auto clock = raw_local_clock(true);
  const auto clocks = clock_fields(clock);
  std::array<ByteView, 22> fields{};
  std::size_t n = 0;
  fields[n++] = request_.lane;
  fields[n++] = raw_native_field(request_.ordinal);
  fields[n++] = raw_native_field(branch);
  fields[n++] = raw_native_field(stage_);
  fields[n++] = raw_native_field(failed);
  if (error) {
    fields[n++] = raw_native_field(error->code);
    fields[n++] = raw_native_field(error->detail);
  }
  fields[n++] = raw_native_field(selected);
  if (selected)
    fields[n++] = raw_native_field(*disposition_);
  std::copy(clocks.begin(), clocks.end(),
            fields.begin() + static_cast<std::ptrdiff_t>(n));
  n += clocks.size();
  const auto emitted =
      raw_emit(2417, std::span{fields}.first(n), RawPartition::control, &*owner_);
  if (!emitted.has_value())
    raw_fence(emitted.error());
  owner_.reset();
}
std::string_view raw_operation_description() noexcept {
  return "2400 whole operation request=bool parent,[R],generation16,size_t depth,bool "
         "pumping,bool stopped,native clocks;"
         "2401 name=operationR,size_t length,exact name;2402 original=operationR,u8 "
         "role,size_t length,exact bytes;"
         "2403 identity=operationR,u8 kind,ID16;2404 admission=operationR,u8 "
         "kind,Cursor32,size_t events;"
         "2405 admission return=operationR,u8 kind,u8 native branch,[Cursor32 or "
         "Error];"
         "2406 effect "
         "request=operationR,environment16,actor16,invocation16,attempt16,size_t input;"
         "2407 effect return=operationR,u8 branch,bool Error,[Error];2408 dispatch "
         "report=operationR,bool dispatched,bool effect_ok,bool "
         "recording_ok,[effectError],[recordingError];"
         "2409 link=operationR,u8 role,bool present,[originalR,RawPartitionu32,size_t "
         "offset,size_t bytes or regionR];"
         "2410 retention=operationR,u8 kind,application16,Cursor32;2411 retention "
         "return=operationR,u8 kind,u8 branch,[SourceReference24 or Error];"
         "2412 disposition=operationR,AttemptDispositionu8,bool timedout,bool "
         "persistent,bool adaptercapacity,bool Error,[Error];"
         "2413 settlement=operationR,bool settlement,bool "
         "fallback,attempt16,AttemptPhaseu8,AttemptDispositionu8,size_t observation;"
         "2414 settlement return=operationR,u8 branch,[bool "
         "existing,RecordReference24,RetainedEvidenceu8 or Error];"
         "2415 callback=operationR,u8 kind,bool present;2416 callback "
         "return=operationR,u8 kind,u8 branch,[cancelledbool or Error];"
         "2417 terminal=operationR,u8 branch,u8 stage,bool Error,[Error],bool "
         "disposition,[AttemptDispositionu8],native clocks;"
         "2418 encoder return=operationR,u8 role,u8 branch,[size_t bytes or "
         "Error];2419 encoder request=operationR,u8 role,size_t bytes,size_t "
         "nodes,size_t depth;"
         "2420 dispatch request=operationR,actual attempt16;"
         "2421 dispatch outcome=operationR,u8 actual exception/returned Result,bool "
         "Error,[Error];"
         "R=lane16+u64 ordinal;Error=ErrorCodeu8+i64;clocks=clockid_t,int result,int "
         "errno,time_t sec,long nsec,bool cpu_read,thread CPU same fields;"
         "native "
         "branches=absent0,returned1,refused2,Error3,bad_alloc4,length_error5,std6,"
         "nonstd7,unfinished8;clock values meaningful only on native success";
}
} // namespace blackbird
