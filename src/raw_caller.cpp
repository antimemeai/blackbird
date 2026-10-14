#include "blackbird/raw_caller.hpp"
#include <algorithm>
#include <cerrno>

namespace blackbird {
namespace {
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
constexpr std::uint64_t linked_size = raw_envelope_size + provider_ref_bytes;
constexpr std::uint64_t terminal_size = linked_size + 2 * sizeof(std::uint8_t) +
                                        sizeof(bool) + sizeof(ErrorCode) +
                                        sizeof(std::int64_t);
template <class T> T require(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
std::uint64_t add(std::uint64_t left, std::uint64_t right) {
  if (right > UINT32_MAX || left > UINT32_MAX - right) {
    raw_capture_failed({ErrorCode::overflow});
    throw Error{ErrorCode::overflow};
  }
  return left + right;
}
} // namespace
bool raw_reference_valid(RawEventRef reference) noexcept {
  return reference.ordinal != 0 &&
         std::any_of(reference.lane.begin(), reference.lane.end(),
                     [](std::byte byte) { return byte != std::byte{}; });
}
std::uint64_t raw_record_bytes(std::span<const ByteView> fields) {
  const ErrnoGuard guard;
  std::uint64_t amount = raw_envelope_size;
  for (const auto field : fields)
    amount = add(amount, field.size());
  return amount;
}
Result<RawEventRef> raw_publish(std::uint32_t site, std::span<const ByteView> fields,
                                RawPartition partition,
                                RawCapacityClaim &bank) noexcept {
  const ErrnoGuard guard;
  const auto result = raw_emit(site, fields, partition, &bank);
  if (!result.has_value() || result.value() == 0) {
    const auto error =
        result.has_value() ? Error{ErrorCode::audit_unavailable} : result.error();
    raw_fence(error);
    return Result<RawEventRef>::failure(error);
  }
  return Result<RawEventRef>::success({raw_lane_epoch(), result.value()});
}
Result<RawEventRef> raw_publish_linked(std::uint32_t site, RawEventRef parent,
                                       std::span<const ByteView> fields,
                                       RawPartition partition,
                                       RawCapacityClaim &bank) noexcept {
  const ErrnoGuard guard;
  std::array<ByteView, 32> body{};
  if (fields.size() > body.size() - 2) {
    raw_fence({ErrorCode::capacity});
    return Result<RawEventRef>::failure({ErrorCode::capacity});
  }
  body[0] = parent.lane;
  body[1] = detail::raw_native_field(parent.ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  return raw_publish(site, std::span{body}.first(fields.size() + 2), partition, bank);
}
RawEmission::RawEmission(RawEmission &&other) noexcept
    : bank_(std::move(other.bank_)), request_(other.request_),
      terminal_(other.terminal_) {
  const ErrnoGuard guard;
  other.bank_.reset();
  other.request_ = {};
  other.terminal_ = 0;
}
void RawEmission::prepare(std::uint64_t amount, RawAdmission admission) {
  const ErrnoGuard guard;
  if (bank_) {
    raw_capture_failed({ErrorCode::busy});
    throw Error{ErrorCode::busy};
  }
  if (amount > UINT32_MAX) {
    raw_capture_failed({ErrorCode::overflow});
    throw Error{ErrorCode::overflow};
  }
  request_ = {};
  terminal_ = 0;
  auto granted = admission == RawAdmission::finite ? raw_try_claim_control(amount)
                                                   : raw_claim_control(amount);
  if (!granted.has_value()) {
    raw_capture_failed(granted.error());
    throw granted.error();
  }
  bank_.emplace(std::move(granted).value());
}
void RawEmission::begin(RawCallSites sites, std::span<const ByteView> fields) {
  const ErrnoGuard guard;
  if (!bank_ || enabled()) {
    raw_capture_failed({ErrorCode::busy});
    throw Error{ErrorCode::busy};
  }
  const auto result = raw_publish(sites.request, fields, RawPartition::control, *bank_);
  if (!result.has_value()) {
    bank_.reset();
    throw result.error();
  }
  request_ = result.value();
  terminal_ = sites.terminal;
}
void RawEmission::start(RawCallSites sites, std::uint64_t amount,
                        std::span<const ByteView> fields, RawAdmission admission) {
  const ErrnoGuard guard;
  prepare(amount, admission);
  begin(sites, fields);
}
Result<void> RawEmission::adopt(RawEventRef request, std::uint32_t terminal,
                                RawControlToken &&bank) noexcept {
  const ErrnoGuard guard;
  if (bank_ || request.ordinal == 0) {
    const Error error{ErrorCode::invalid_range};
    raw_capture_failed(error);
    return Result<void>::failure(error);
  }
  bank_.emplace(std::move(bank));
  request_ = request;
  terminal_ = terminal;
  return Result<void>::success();
}
void RawEmission::close(std::span<const ByteView> fields) noexcept {
  const ErrnoGuard guard;
  if (bank_ && request_.ordinal)
    (void)raw_publish(terminal_, fields, RawPartition::control, *bank_);
  bank_.reset();
}
void RawEmission::close_linked(std::span<const ByteView> fields) noexcept {
  const ErrnoGuard guard;
  if (bank_ && request_.ordinal)
    (void)raw_publish_linked(terminal_, request_, fields, RawPartition::control,
                             *bank_);
  bank_.reset();
}
void RawEmission::release() noexcept {
  const ErrnoGuard guard;
  bank_.reset();
}
void RawEmission::finish(RawOperationBranch branch, std::uint8_t stage,
                         const Error *error, std::span<const ByteView> extra) noexcept {
  const ErrnoGuard guard;
  if (!enabled())
    return;
  const bool present = error != nullptr;
  std::array<ByteView, 30> fields{};
  std::size_t count = 0;
  fields[count++] = detail::raw_native_field(branch);
  fields[count++] = detail::raw_native_field(stage);
  fields[count++] = detail::raw_native_field(present);
  if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  if (extra.size() > fields.size() - count) {
    raw_fence({ErrorCode::capacity});
    bank_.reset();
    return;
  }
  std::copy(extra.begin(), extra.end(),
            fields.begin() + static_cast<std::ptrdiff_t>(count));
  close_linked(std::span{fields}.first(count + extra.size()));
}
RawCallerTrace::RawCallerTrace(RawCallSites sites, const RawAdapterContext *context,
                               std::span<const ByteView> fields,
                               std::uint64_t terminal_extra) {
  const ErrnoGuard guard;
  if (!context || !context->parent || !raw_enabled())
    return;
  if (!raw_reference_valid(*context->parent)) {
    const Error error{ErrorCode::invalid_identity};
    raw_capture_failed(error);
    raw_fence(error);
    throw error;
  }
  std::array<ByteView, 32> body{};
  if (fields.size() > body.size() - 2) {
    raw_capture_failed({ErrorCode::capacity});
    throw Error{ErrorCode::capacity};
  }
  body[0] = context->parent->lane;
  body[1] = detail::raw_native_field(context->parent->ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  const auto request = std::span{body}.first(fields.size() + 2);
  emission_.start(sites,
                  add(raw_record_bytes(request), add(terminal_size, terminal_extra)),
                  request);
}
RawCallerTrace::~RawCallerTrace() { finish(RawOperationBranch::unfinished); }
std::optional<RawCapacityClaim> RawCallerTrace::data(std::uint64_t bytes) const {
  const ErrnoGuard guard;
  if (!enabled())
    return {};
  if (bytes > UINT32_MAX) {
    raw_capture_failed({ErrorCode::overflow});
    throw Error{ErrorCode::overflow};
  }
  return require(raw_claim_data(bytes));
}
std::optional<RawControlToken> RawCallerTrace::control(std::uint64_t bytes) const {
  const ErrnoGuard guard;
  if (!enabled())
    return {};
  if (bytes > UINT32_MAX) {
    raw_capture_failed({ErrorCode::overflow});
    throw Error{ErrorCode::overflow};
  }
  return require(raw_claim_control(bytes));
}
std::optional<Error> RawCallerTrace::publish(std::uint32_t site, RawEventRef parent,
                                             std::span<const ByteView> fields,
                                             RawPartition partition,
                                             RawControlToken &credit,
                                             RawEventRef *output) noexcept {
  const auto result = raw_publish_linked(site, parent, fields, partition, credit);
  if (!result.has_value())
    return result.error();
  if (output)
    *output = result.value();
  return {};
}
void RawCallerTrace::bytes(std::uint32_t site, std::string_view value,
                           std::optional<RawCapacityClaim> &credit) noexcept {
  if (!enabled() || !credit)
    return;
  const auto size = value.size();
  (void)publish(
      site, reference(),
      std::array{detail::raw_native_field(size), detail::raw_string_field(value)},
      RawPartition::data, *credit);
}
void RawCallerTrace::event(std::uint32_t site, std::span<const ByteView> fields,
                           std::optional<RawControlToken> &credit) noexcept {
  if (enabled() && credit)
    (void)publish(site, reference(), fields, RawPartition::control, *credit);
}
void RawCallerTrace::data_event(std::uint32_t site, std::span<const ByteView> fields,
                                std::optional<RawCapacityClaim> &credit) noexcept {
  if (enabled() && credit)
    (void)publish(site, reference(), fields, RawPartition::data, *credit);
}
void RawCallerTrace::finish(RawOperationBranch branch, const Error *error,
                            std::span<const ByteView> fields) noexcept {
  emission_.finish(branch, stage_, error, fields);
}
std::string_view raw_caller_description() noexcept {
  return "caller3600-3611,3700-3707;E16;R=lane16,u64 ordinal;native,no padding;"
         "end:R,u8 RawOperationBranch,u8 stage,bool Error,[ErrorCode,i64],"
         "site-specific returned fields;3600:parentR,size_t requestLimit,responseLimit;"
         "3601:end;3602:R,size_t N,original request bytes;"
         "3603:R,childR,int exit;3604:R;3605:end,[int status on returned];"
         "3606:R,u8 channel,bool child,[childR],size_t bytes,bool observer;"
         "3607:end;3608:R,size_t bytes;3609:end,[bool Result.has_value on returned];"
         "3610:R;3611:end;3700:parentR;3701:end;3702:R,size_t N,path bytes;"
         "3703:R,u8 kind,size_t argc;3704:end;3705:R,size_t index,size_t N,argv bytes;"
         "3706:R,childR,int exit;3707:R,int exit,bool oid;"
         "decisionStage1validation2encoding3credential4config5start/send6collect"
         "7exit8HTTP9parse10validate11result;gitStage1validation2path3historyCount"
         "4repository5HEAD6history7parse/sample8return;"
         "observerChannel1response2request;gitKind1repository2HEAD3history";
}
} // namespace blackbird
