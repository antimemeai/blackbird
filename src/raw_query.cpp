#include "blackbird/raw_query.hpp"
#include <algorithm>
#include <cerrno>
namespace blackbird {
namespace {
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
std::optional<RawControlToken> claim(std::uint64_t bytes) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  auto result = raw_claim_control(bytes);
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
RawEventRef publish(std::uint32_t site, std::span<const ByteView> fields,
                    std::optional<RawControlToken> &credit) noexcept {
  const ErrnoGuard guard;
  if (!credit)
    return {};
  const auto result = raw_emit(site, fields, RawPartition::control, &*credit);
  if (!result.has_value())
    return {};
  return {raw_lane_epoch(), result.value()};
}
RawEventRef linked(std::uint32_t site, RawEventRef parent,
                   std::span<const ByteView> fields,
                   std::optional<RawControlToken> &credit) noexcept {
  const ErrnoGuard guard;
  if (!credit)
    return {};
  std::array<ByteView, 24> body{};
  if (fields.size() > body.size() - 2) {
    raw_fence({ErrorCode::capacity});
    return {};
  }
  body[0] = parent.lane;
  body[1] = detail::raw_native_field(parent.ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  return publish(site, std::span{body}.first(fields.size() + 2), credit);
}
} // namespace
RawQueryTrace::RawQueryTrace(RawQueryMethod method,
                             std::optional<OperationAttemptId> attempt,
                             JournalCursor cursor, std::size_t facts)
    : credit_(claim(783)) {
  const ErrnoGuard guard;
  if (!credit_)
    return;
  if (method != RawQueryMethod::inspect) {
    auto data = raw_claim_data(112);
    if (!data.has_value())
      throw data.error();
    variable_credit_.emplace(std::move(data).value());
  }
  const auto tag = static_cast<std::uint8_t>(method);
  const bool present = attempt.has_value();
  std::array<ByteView, 7> fields{};
  std::size_t count = 0;
  fields[count++] = detail::raw_native_field(tag);
  fields[count++] = detail::raw_native_field(present);
  if (attempt)
    fields[count++] = attempt->bytes();
  fields[count++] = cursor.journal.bytes();
  fields[count++] = detail::raw_native_field(cursor.sequence);
  fields[count++] = detail::raw_native_field(cursor.end_offset);
  fields[count++] = detail::raw_native_field(facts);
  request_ = publish(2300, std::span{fields}.first(count), credit_);
}
RawQueryTrace::~RawQueryTrace() { failure(3); }
void RawQueryTrace::event(std::uint32_t site,
                          std::span<const ByteView> fields) noexcept {
  (void)linked(site, request_, fields, credit_);
}
void RawQueryTrace::identity_field(RawQueryField field,
                                   const IdentityBytes &value) noexcept {
  const auto tag = static_cast<std::uint8_t>(field);
  const std::array fields{detail::raw_native_field(tag), ByteView{value}};
  event(2301, fields);
}
void RawQueryTrace::variable(std::string_view value) noexcept {
  const ErrnoGuard guard;
  if (!variable_credit_)
    return;
  const auto length = value.size();
  const std::array fields{
      ByteView{request_.lane}, detail::raw_native_field(request_.ordinal),
      detail::raw_native_field(length), detail::raw_string_field(value)};
  (void)raw_emit(2305, fields, RawPartition::data, &*variable_credit_);
}
void RawQueryTrace::returned(std::uint8_t shape,
                             std::span<const std::size_t> values) noexcept {
  if (finished_)
    return;
  const std::uint8_t branch = 0;
  std::array<ByteView, 7> fields{};
  if (values.size() > fields.size() - 2) {
    raw_fence({ErrorCode::capacity});
    failure(3);
    return;
  }
  fields[0] = detail::raw_native_field(branch);
  fields[1] = detail::raw_native_field(shape);
  for (std::size_t i = 0; i < values.size(); ++i)
    fields[i + 2] = detail::raw_native_field(values[i]);
  event(2306, std::span{fields}.first(values.size() + 2));
  finished_ = true;
}
void RawQueryTrace::failure(std::uint8_t branch, std::optional<Error> error) noexcept {
  if (finished_)
    return;
  const std::uint8_t shape = 0;
  std::array<ByteView, 5> fields{};
  fields[0] = detail::raw_native_field(branch);
  fields[1] = detail::raw_native_field(shape);
  std::size_t count = 2;
  if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  fields[count++] = detail::raw_native_field(stage_);
  event(2306, std::span{fields}.first(count));
  finished_ = true;
}
RawQueryLookup::RawQueryLookup(RawEventRef parent, RawQueryLookupKind kind,
                               std::span<const ByteView> fields, std::uint64_t credit)
    : credit_(claim(credit)) {
  const auto tag = static_cast<std::uint8_t>(kind);
  std::array<ByteView, 8> body{};
  if (fields.size() > body.size() - 1)
    throw Error{ErrorCode::capacity};
  body[0] = detail::raw_native_field(tag);
  std::copy(fields.begin(), fields.end(), body.begin() + 1);
  request_ = linked(2302, parent, std::span{body}.first(fields.size() + 1), credit_);
}
RawQueryLookup::~RawQueryLookup() { failure(3); }
void RawQueryLookup::returned(std::span<const ByteView> fields) noexcept {
  if (finished_)
    return;
  const std::uint8_t branch = 0;
  std::array<ByteView, 20> body{};
  if (fields.size() > body.size() - 1) {
    raw_fence({ErrorCode::capacity});
    failure(3);
    return;
  }
  body[0] = detail::raw_native_field(branch);
  std::copy(fields.begin(), fields.end(), body.begin() + 1);
  (void)linked(2303, request_, std::span{body}.first(fields.size() + 1), credit_);
  finished_ = true;
}
void RawQueryLookup::fact(const RetainedFact &fact, RetainedKind kind) noexcept {
  const std::array fields{ByteView{fact.record.journal.bytes()},
                          detail::raw_native_field(fact.record.sequence),
                          detail::raw_native_field(kind),
                          detail::raw_native_field(fact.evidence)};
  returned(fields);
}
void RawQueryLookup::attempt(const AttemptState &state) noexcept {
  const bool observation = state.observation.has_value();
  const bool receipt = state.receipt.has_value();
  const bool failure = receipt && state.receipt->failure.has_value();
  std::array<ByteView, 14> fields{};
  std::size_t count = 0;
  fields[count++] = state.admission.attempt.bytes();
  fields[count++] = state.admission.invocation.bytes();
  fields[count++] = state.admission.decision.bytes();
  fields[count++] = detail::raw_native_field(state.opened);
  fields[count++] = detail::raw_native_field(observation);
  if (observation) {
    fields[count++] = detail::raw_native_field(state.observation->phase);
    fields[count++] = detail::raw_native_field(state.observation->disposition);
  }
  fields[count++] = detail::raw_native_field(receipt);
  if (state.receipt) {
    fields[count++] = detail::raw_native_field(failure);
    if (state.receipt->failure) {
      fields[count++] = detail::raw_native_field(state.receipt->failure->code);
      fields[count++] = detail::raw_native_field(state.receipt->failure->detail);
    }
  }
  fields[count++] = detail::raw_native_field(state.evidence);
  fields[count++] = detail::raw_native_field(state.reconciliation_required);
  returned(std::span{fields}.first(count));
}
void RawQueryLookup::bytes(std::size_t length) noexcept {
  const std::array fields{detail::raw_native_field(length)};
  returned(fields);
}
void RawQueryLookup::filter(std::uint8_t branch) noexcept {
  const std::array fields{detail::raw_native_field(branch)};
  (void)linked(2304, request_, fields, credit_);
}
void RawQueryLookup::failure(std::uint8_t branch, std::optional<Error> error) noexcept {
  if (finished_)
    return;
  std::array<ByteView, 3> fields{};
  fields[0] = detail::raw_native_field(branch);
  std::size_t count = 1;
  if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  (void)linked(2303, request_, std::span{fields}.first(count), credit_);
  finished_ = true;
}
RawQueryPacket::RawQueryPacket(std::uint8_t method, RawEventRef query,
                               RecordReference record, std::size_t input,
                               ValueLimits limits, std::optional<RawEventRef> prior)
    : credit_(claim(prior ? 173 : 149)) {
  const bool present = prior.has_value();
  std::array<ByteView, 12> fields{};
  std::size_t count = 0;
  fields[count++] = detail::raw_native_field(method);
  fields[count++] = query.lane;
  fields[count++] = detail::raw_native_field(query.ordinal);
  fields[count++] = record.journal.bytes();
  fields[count++] = detail::raw_native_field(record.sequence);
  fields[count++] = detail::raw_native_field(input);
  fields[count++] = detail::raw_native_field(limits.bytes);
  fields[count++] = detail::raw_native_field(limits.nodes);
  fields[count++] = detail::raw_native_field(limits.depth);
  fields[count++] = detail::raw_native_field(present);
  if (prior) {
    fields[count++] = prior->lane;
    fields[count++] = detail::raw_native_field(prior->ordinal);
  }
  request_ = publish(2307, std::span{fields}.first(count), credit_);
}
RawQueryPacket::~RawQueryPacket() { finish(3); }
void RawQueryPacket::finish(std::uint8_t branch, std::optional<Error> error) noexcept {
  if (finished_)
    return;
  std::array<ByteView, 4> fields{};
  fields[0] = detail::raw_native_field(branch);
  fields[1] = detail::raw_native_field(stage_);
  std::size_t count = 2;
  if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  (void)linked(2308, request_, std::span{fields}.first(count), credit_);
  finished_ = true;
}
std::string_view raw_query_description() noexcept {
  return "query2300-2308;E16;R=lane16,ordinalu64;native host fields,no padding;"
         "2300:u8 method(inspect1,trajectory2,variables3),bool attempt,[attempt16],"
         "cursor journal16,sequenceu64,end_offsetu64,fact_count:size_t;"
         "2301:queryR,u8 "
         "field(end1,count2,scan3,cursor4,limit5,record6,source7,offset8,"
         "variables9,packet10,attempt11),native size_t/bool/attempt16;"
         "2302:ownerR,u8 kind(fact1,attempt2,source3,payload4,diagnostic5),"
         "fact:size_t index;attempt:ID16;source:journal16,sequenceu64;"
         "payload:record journal16,sequenceu64;diagnostic:record journal16,sequenceu64,"
         "metadata_readR,nowu64;2303:lookupR,u8 branch(returned0,Error1,allocation2,"
         "unknown3),fact:record "
         "journal16,sequenceu64,RetainedKindu16,RetainedEvidenceu8;"
         "attempt:attempt16,invocation16,decision16,bool opened,bool observation,"
         "[AttemptPhaseu8,AttemptDispositionu8],bool receipt,[bool "
         "failure,[ErrorCode,i64]],"
         "RetainedEvidenceu8,bool reconciliation;bytes:size_t;Error:ErrorCode,i64;"
         "2304:fact_readR,u8 branch(selected1,skip_variable2,skip_attempt3);"
         "2305:queryR,size_t length,validated variable bytes;2306:queryR,u8 branch,"
         "u8 shape(page1,hex2,packet3,trajectory4),success size_t fields:"
         "page(end,next,rows),hex(end,bytes,offset,next,length),packet(end,bytes,"
         "expanded),"
         "trajectory(end,cursor,next,rows);failure:[ErrorCode,i64],u8 stage;"
         "2307:u8 method(read_packet1,decode_packet2),queryR,record "
         "journal16,sequenceu64,"
         "size_t input,ValueLimits size_t bytes,nodes,depth,bool prior_read,[R];"
         "2308:helperR,u8 branch,u8 stage(size_check0,read1,decode2),[ErrorCode,i64];"
         "query stages:entry0,validation1,lookup2,summary3,result4;"
         "requests/results distinct;no physical fields inferred;diagnostic partial "
         "bytes"
         "require native storage capture";
}
} // namespace blackbird
