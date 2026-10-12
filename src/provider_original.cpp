#include "blackbird/provider_original.hpp"
#include "blackbird/packet.hpp"
#include <array>
#include <cerrno>
#include <limits>
#include <stdexcept>

namespace blackbird {
namespace {
using detail::raw_native_field;
using detail::raw_string_field;
constexpr auto ref_bytes = provider_ref_bytes;
constexpr auto phase_begin_bytes = provider_phase_begin_credit;
constexpr auto phase_end_bytes = provider_phase_end_credit;
struct ErrnoGuard {
  int value = errno;
  ~ErrnoGuard() { errno = value; }
};
template <class T> T require(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
std::uint64_t checked(std::size_t size, std::uint64_t overhead) {
  if (size > std::numeric_limits<std::uint32_t>::max() - overhead)
    throw Error{ErrorCode::overflow};
  return static_cast<std::uint64_t>(size) + overhead;
}
std::uint64_t emit(std::uint32_t site, std::span<const ByteView> parts,
                   RawPartition partition, RawCapacityClaim *claim) {
  const auto result = raw_emit(site, parts, partition, claim);
  if (!result.has_value()) {
    raw_fence(result.error());
    throw result.error();
  }
  return result.value();
}
void finish_disposition(const ProviderCaptureDisposition &disposition) {
  if (disposition.error)
    throw *disposition.error;
  if (disposition.state != ProviderCaptureState::accepted)
    throw Error{disposition.state == ProviderCaptureState::overflow
                    ? ErrorCode::capacity
                    : ErrorCode::interrupted};
}
} // namespace
RawProviderPhase::RawProviderPhase(std::uint64_t extra) {
  const ErrnoGuard guard;
  if (extra > UINT64_MAX - phase_begin_bytes - phase_end_bytes)
    throw Error{ErrorCode::overflow};
  if (raw_enabled())
    credit_.emplace(
        require(raw_claim_control(phase_begin_bytes + phase_end_bytes + extra)));
}
RawProviderPhase::RawProviderPhase(std::optional<RawControlToken> credit) noexcept
    : credit_(std::move(credit)) {}
RawProviderPhase::~RawProviderPhase() { finish(6); }
void RawProviderPhase::start(std::uint8_t kind, const RawAdapterContext *context) {
  const ErrnoGuard guard;
  if (!credit_)
    return;
  const bool parent = context && context->parent && context->parent->ordinal;
  const bool owner = context && context->owner;
  std::array<ByteView, 8> fields{};
  std::size_t count = 0;
  fields[count++] = raw_native_field(kind);
  fields[count++] = raw_native_field(parent);
  if (parent) {
    fields[count++] = context->parent->lane;
    fields[count++] = raw_native_field(context->parent->ordinal);
  }
  fields[count++] = raw_native_field(owner);
  if (owner) {
    fields[count++] = context->owner->attempt;
    fields[count++] = context->owner->invocation;
    fields[count++] = context->owner->generation;
  }
  phase_ = {raw_lane_epoch(), blackbird::emit(1320, std::span{fields}.first(count),
                                              RawPartition::control, &*credit_)};
}
void RawProviderPhase::emit(std::uint32_t site,
                            std::span<const ByteView> fields) noexcept {
  const ErrnoGuard guard;
  if (!credit_ || !phase_.ordinal)
    return;
  const auto result = raw_emit(site, fields, RawPartition::control, &*credit_);
  if (!result.has_value())
    raw_fence(result.error());
}
void RawProviderPhase::link(RawEventRef actual) noexcept {
  if (!actual.ordinal)
    return;
  const std::array parts{ByteView{phase_.lane}, raw_native_field(phase_.ordinal),
                         ByteView{actual.lane}, raw_native_field(actual.ordinal)};
  emit(1324, parts);
}
void RawProviderPhase::scalar(std::uint8_t stage, std::uint8_t variant,
                              std::span<const ByteView> fields) noexcept {
  std::array<ByteView, 8> parts{};
  if (fields.size() > parts.size() - 4) {
    raw_fence({ErrorCode::overflow});
    return;
  }
  parts[0] = phase_.lane;
  parts[1] = raw_native_field(phase_.ordinal);
  parts[2] = raw_native_field(stage);
  parts[3] = raw_native_field(variant);
  std::copy(fields.begin(), fields.end(), parts.begin() + 4);
  emit(1323, std::span{parts}.first(fields.size() + 4));
}
void RawProviderPhase::finish(std::uint8_t branch, const Error *error) noexcept {
  const ErrnoGuard guard;
  if (credit_ && phase_.ordinal) {
    std::array<ByteView, 6> fields{ByteView{phase_.lane},
                                   raw_native_field(phase_.ordinal),
                                   raw_native_field(branch),
                                   raw_native_field(stage_),
                                   {},
                                   {}};
    std::size_t count = 4;
    if (error) {
      fields[count++] = raw_native_field(error->code);
      fields[count++] = raw_native_field(error->detail);
    }
    emit(1321, std::span{fields}.first(count));
  }
  credit_.reset();
}
std::optional<RawControlToken> RawProviderPhase::split(std::uint64_t bytes) {
  if (!credit_)
    return std::nullopt;
  return require(credit_->split(bytes));
}
RawProviderAcquisition::RawProviderAcquisition(
    std::optional<RawControlToken> credit) noexcept
    : credit_(std::move(credit)) {}
RawProviderAcquisition::~RawProviderAcquisition() { finish(6); }
void RawProviderAcquisition::start(RawEventRef request,
                                   std::optional<RawEventRef> child) {
  const ErrnoGuard guard;
  callback_ = !child.has_value();
  if (!credit_)
    return;
  const std::size_t maximum = 16 * 1024 * 1024;
  std::array<ByteView, 4> fields{
      ByteView{request.lane}, raw_native_field(request.ordinal), {}, {}};
  std::size_t n = 2;
  if (child) {
    fields[n++] = child->lane;
    fields[n++] = raw_native_field(child->ordinal);
  } else
    fields[n++] = raw_native_field(maximum);
  request_ = {raw_lane_epoch(),
              blackbird::emit(callback_ ? 1310 : 1314, std::span{fields}.first(n),
                              RawPartition::control, &*credit_)};
}
void RawProviderAcquisition::finish(std::uint8_t branch, const Error *error,
                                    const int *status) noexcept {
  const ErrnoGuard guard;
  if (credit_ && request_.ordinal) {
    const bool present = status != nullptr;
    std::array<ByteView, 7> fields{ByteView{request_.lane},
                                   raw_native_field(request_.ordinal),
                                   raw_native_field(branch),
                                   {},
                                   {},
                                   {},
                                   {}};
    std::size_t n = 3;
    if (callback_)
      fields[n++] = raw_native_field(present);
    if (status)
      fields[n++] = raw_native_field(*status);
    if (error) {
      fields[n++] = raw_native_field(error->code);
      fields[n++] = raw_native_field(error->detail);
    }
    const auto result = raw_emit(callback_ ? 1311 : 1315, std::span{fields}.first(n),
                                 RawPartition::control, &*credit_);
    if (!result.has_value())
      raw_fence(result.error());
  }
  credit_.reset();
}
ProviderOwnedOriginal::ProviderOwnedOriginal(
    Storage storage, std::optional<ProviderOriginalReference> reference,
    ProviderOriginalExtentKind kind, std::optional<std::size_t> used) noexcept
    : storage_(std::move(storage)), reference_(reference), kind_(kind),
      observed_used_(used) {}
std::string_view ProviderOwnedOriginal::bytes() const noexcept {
  std::string_view result;
  if (const auto *local = std::get_if<std::string>(&storage_))
    result = *local;
  else if (const auto *shared =
               std::get_if<std::shared_ptr<const ProviderOriginalStorage>>(&storage_);
           shared && *shared)
    result = (*shared)->bytes;
  if (kind_ == ProviderOriginalExtentKind::exact_used && observed_used_)
    return result.substr(0, *observed_used_);
  return result;
}
bool ProviderOwnedOriginal::owns_bytes() const noexcept {
  return storage_.index() != 0;
}
const std::optional<ProviderOriginalReference> &
ProviderOwnedOriginal::reference() const noexcept {
  return reference_;
}
ProviderOriginalExtentKind ProviderOwnedOriginal::kind() const noexcept {
  return kind_;
}
std::optional<std::size_t> ProviderOwnedOriginal::observed_used() const noexcept {
  return observed_used_;
}
ProviderOriginalLease::ProviderOriginalLease(ProviderOriginalRequest request,
                                             RawEventRef source,
                                             ProviderOriginalCapture *capture,
                                             bool capture_raw)
    : request_(request), source_(source), raw_(capture_raw) {
  const ErrnoGuard guard;
  if (capture) {
    ticket_ = require(capture->prepare(request));
    shared_ = ticket_->take_storage();
    if (!shared_)
      throw Error{ErrorCode::conflict};
  }
  if (request.mode == ProviderOriginalMode::native_extent) {
    storage().resize(request.maximum_bytes);
    prepared_ = true;
  }
  if (capture_raw && raw_enabled()) {
    const std::uint64_t overhead =
        request.mode == ProviderOriginalMode::callback_extent
            ? raw_envelope_size + ref_bytes + 1 + 2 * sizeof(std::size_t) + sizeof(bool)
            : raw_envelope_size + ref_bytes + 1 + sizeof(std::size_t);
    data_.emplace(require(raw_claim_data(checked(request.maximum_bytes, overhead))));
  }
}
ProviderOriginalLease::ProviderOriginalLease(ProviderOriginalLease &&other) noexcept
    : request_(other.request_), source_(other.source_),
      ticket_(std::move(other.ticket_)), shared_(std::move(other.shared_)),
      local_(std::move(other.local_)), data_(std::move(other.data_)),
      reference_(other.reference_), kind_(other.kind_), used_(other.used_),
      prepared_(std::exchange(other.prepared_, false)),
      acquired_(std::exchange(other.acquired_, false)),
      handed_(std::exchange(other.handed_, true)), raw_(other.raw_),
      disposition_(other.disposition_) {}
ProviderOriginalLease::~ProviderOriginalLease() {
  unknown();
  (void)complete();
}
std::string &ProviderOriginalLease::storage() noexcept {
  return shared_ ? shared_->bytes : local_;
}
std::span<char> ProviderOriginalLease::writable() noexcept {
  auto &s = storage();
  return {s.data(), s.size()};
}
void ProviderOriginalLease::prepare(std::size_t requested) {
  if (prepared_ || request_.mode != ProviderOriginalMode::callback_extent ||
      requested > request_.maximum_bytes)
    throw Error{ErrorCode::invalid_range};
  storage().resize(requested);
  prepared_ = true;
}
void ProviderOriginalLease::publish(ProviderOriginalExtentKind kind,
                                    std::optional<std::size_t> used) {
  const ErrnoGuard guard;
  kind_ = kind;
  used_ = used;
  acquired_ = true;
  if (!raw_ || !raw_enabled())
    return;
  const auto requested = storage().size();
  const auto count = used ? *used : requested;
  const bool present = used.has_value();
  std::array<ByteView, 7> fields{ByteView{source_.lane},
                                 raw_native_field(source_.ordinal),
                                 raw_native_field(kind),
                                 raw_native_field(requested),
                                 raw_native_field(present),
                                 {},
                                 {}};
  std::size_t n = 5;
  if (used)
    fields[n++] = raw_native_field(*used);
  fields[n++] = raw_string_field(std::string_view{storage()}.substr(0, count));
  const auto ordinal = blackbird::emit(1312, std::span{fields}.first(n),
                                       RawPartition::data, data_ ? &*data_ : nullptr);
  reference_ = ProviderOriginalReference{
      {raw_lane_epoch(), ordinal},
      RawPartition::data,
      used ? raw_envelope_size + ref_bytes + 1 + 2 * sizeof(std::size_t) + sizeof(bool)
           : raw_envelope_size + ref_bytes + 1 + sizeof(std::size_t) + sizeof(bool),
      count};
  data_.reset();
}
void ProviderOriginalLease::commit(std::size_t used) {
  if (!prepared_ || acquired_ ||
      request_.mode != ProviderOriginalMode::callback_extent || used > storage().size())
    throw Error{ErrorCode::invalid_range};
  publish(ProviderOriginalExtentKind::exact_used, used);
}
void ProviderOriginalLease::adopt(std::string bytes) {
  const ErrnoGuard guard;
  if (prepared_ || acquired_ || request_.mode != ProviderOriginalMode::codec_adopt ||
      bytes.size() > request_.maximum_bytes)
    throw Error{ErrorCode::invalid_range};
  storage() = std::move(bytes);
  prepared_ = acquired_ = true;
  used_ = storage().size();
  if (!raw_ || !raw_enabled())
    return;
  const auto size = storage().size();
  const std::array fields{ByteView{source_.lane}, raw_native_field(source_.ordinal),
                          raw_native_field(request_.role), raw_native_field(size),
                          raw_string_field(storage())};
  const auto ordinal =
      blackbird::emit(1322, fields, RawPartition::data, data_ ? &*data_ : nullptr);
  reference_ =
      ProviderOriginalReference{{raw_lane_epoch(), ordinal},
                                RawPartition::data,
                                raw_envelope_size + ref_bytes + 1 + sizeof(std::size_t),
                                size};
  data_.reset();
}
void ProviderOriginalLease::native(std::optional<ProviderOriginalReference> reference,
                                   std::size_t used) noexcept {
  reference_ = reference;
  used_ = used;
  acquired_ = true;
}
void ProviderOriginalLease::unknown() noexcept {
  if (request_.mode != ProviderOriginalMode::callback_extent || !prepared_ || acquired_)
    return;
  try {
    publish(ProviderOriginalExtentKind::unknown_used, std::nullopt);
  } catch (const Error &error) {
    raw_fence(error);
  } catch (...) {
    raw_fence({ErrorCode::external_unknown});
  }
}
std::string_view ProviderOriginalLease::bytes() const noexcept {
  std::string_view result = shared_ ? shared_->bytes : local_;
  return used_ ? result.substr(0, *used_) : result;
}
const std::optional<ProviderOriginalReference> &
ProviderOriginalLease::reference() const noexcept {
  return reference_;
}
ProviderCaptureDisposition ProviderOriginalLease::complete() noexcept {
  if (!acquired_ || handed_)
    return disposition_;
  handed_ = true;
  if (ticket_) {
    ProviderOwnedOriginal::Storage body{
        std::shared_ptr<const ProviderOriginalStorage>{std::move(shared_)}};
    disposition_ = ticket_->complete(
        ProviderOwnedOriginal{std::move(body), reference_, kind_, used_});
    ticket_.reset();
  }
  return disposition_;
}
ProviderOwnedOriginal ProviderOriginalLease::take_original() {
  if (!acquired_)
    throw Error{ErrorCode::conflict};
  ProviderOwnedOriginal::Storage body;
  if (shared_)
    body = std::shared_ptr<const ProviderOriginalStorage>{shared_};
  else
    body = std::move(local_);
  ProviderOwnedOriginal result{std::move(body), reference_, kind_, used_};
  finish_disposition(complete());
  return result;
}
std::string ProviderOriginalLease::response_body() {
  auto original = take_original();
  if (ticket_ || shared_)
    return std::string{original.bytes()};
  // Shared original may already have moved into the receiver; take_original pins
  // it through the explicit product copy. Default response moves its string.
  if (auto *local = std::get_if<std::string>(&original.storage_)) {
    if (original.observed_used_)
      local->resize(*original.observed_used_);
    return std::move(*local);
  }
  return std::string{original.bytes()};
}
std::span<char> ProviderHttpResponseLease::prepare(std::size_t requested) {
  source_->prepare(requested);
  return source_->writable();
}
void ProviderHttpResponseLease::commit(std::size_t actual_used) {
  source_->commit(actual_used);
}
ProviderOwnedOriginal provider_encode_original(const Value &value,
                                               ProviderOriginalRequest request,
                                               const RawAdapterContext *context,
                                               ValueLimits limits) {
  RawProviderPhase phase;
  try {
    phase.start(11, context);
    phase.stage(70);
    if (request.mode != ProviderOriginalMode::codec_adopt ||
        request.maximum_bytes != limits.bytes)
      throw Error{ErrorCode::invalid_range};
    ProviderOriginalLease lease{request, phase.reference(),
                                context ? context->originals : nullptr};
    auto encoded = encode_packet_string(value, limits);
    if (!encoded.has_value())
      throw encoded.error();
    lease.adopt(std::move(encoded).value());
    auto result = lease.take_original();
    phase.finish(1);
    return result;
  } catch (const Error &error) {
    phase.finish(2, &error);
    throw;
  } catch (const std::bad_alloc &) {
    phase.finish(3);
    throw;
  } catch (const std::length_error &) {
    phase.finish(4);
    throw;
  } catch (...) {
    phase.finish(5);
    throw;
  }
}
std::string_view provider_original_description() noexcept {
  return "1310 injected callback invoke=Ref application request,size_t maximum;"
         "1311 callback return=Ref invoke,u8 branch,bool status_present,[int actual "
         "status when returned],[ErrorCode,i64 detail only original Error];"
         "1312 acquired initialized extent=Ref application request,u8 extent_kind(1 "
         "exact_used,2 unknown_used),size_t requested,bool used_present,[size_t "
         "observed_used only exact_used],original initialized extent bytes (observed "
         "prefix for exact_used);unknown_used is unresolved native snapshot,not "
         "complete body;"
         "1314 native response acquisition begin=Ref application request,Ref "
         "child;1315 response acquisition end=Ref begin,u8 branch,[ErrorCode,i64 "
         "detail only original Error];"
         "1320 semantic phase begin=u8 kind(1 assembly,2 retry_group,3 attempt,4 "
         "retry_wait,5 adapter,6 decode,7 preview_feed,8 presentation,9 receipt,10 "
         "acceptance,11 colleague_prepare,12 colleague_transport,13 SSE_event,14 "
         "preview_event),bool parent_present,[Ref parent],bool "
         "owner_present,[attempt16,invocation16,generation16];"
         "1321 semantic terminal=Ref begin,u8 branch(1 returned,2 original Error,3 "
         "allocation,4 length,5 unknown exception,6 unresolved scope),u8 "
         "stage,[ErrorCode,i64 detail only original Error];"
         "1322 existing business original=Ref phase,u8 role(1 request_packet,2 "
         "prepared_admission,3 upstream_packet,4 decoded_packet,5 wrapper_packet,6 "
         "native_stdout,7 native_stderr,8 callback_body,9 public_prompt,10 "
         "public_instructions,11 standalone_request),size_t bytes,original bytes;"
         "1323 acquired branch/scalar=Ref phase,u8 stage,u8 variant(0 no fields,1 "
         "bool,2 unsigned,3 size_t,4 int,5 size_t converter ordinal+std::errc "
         "(stage47),6 two size_t,7 revision string size_t+bytes,8 identity16),selected "
         "native fields;"
         "1324 actual source link=Ref semantic phase,Ref actual source;1325 retry "
         "policy=Ref group,actual retryGroupId16,unsigned max_attempts,unsigned "
         "base_ms,unsigned cap_ms;"
         "1326 actual retry decision=Ref group,unsigned ordinal,Ref prior "
         "attempt,unsigned delay_ms,bool eligible;1327 native clock=Ref wait,clockid_t "
         "method,int result,int errno,time_t seconds,long nanoseconds;"
         "Ref=lane16,u64 ordinal;only acquired optional fields;errno unchanged;no "
         "credentials or writer transformations;";
}
} // namespace blackbird
