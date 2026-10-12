#include "blackbird/raw_participants.hpp"
#include <algorithm>
#include <cerrno>
namespace blackbird {
namespace {
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
RawEventRef publish(std::uint32_t site, std::span<const ByteView> fields,
                    RawPartition partition, RawCapacityClaim *credit) noexcept {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  const auto emitted = raw_emit(site, fields, partition, credit);
  if (!emitted.has_value()) {
    raw_fence(emitted.error());
    return {};
  }
  return {raw_lane_epoch(), emitted.value()};
}
} // namespace
RawParticipantBank::RawParticipantBank(std::uint64_t bytes) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return;
  auto claim = raw_claim_control(bytes);
  if (!claim.has_value())
    throw claim.error();
  credit_.emplace(std::move(claim).value());
}
RawParticipantBank::RawParticipantBank(std::optional<RawControlToken> credit) noexcept
    : credit_(std::move(credit)) {}
RawParticipantBank RawParticipantBank::split(std::uint64_t bytes) {
  if (!credit_)
    return {};
  auto claim = credit_->split(bytes);
  if (!claim.has_value())
    throw claim.error();
  return RawParticipantBank{std::move(claim).value()};
}
RawEventRef RawParticipantBank::emit(std::uint32_t site,
                                     std::span<const ByteView> fields) noexcept {
  if (!credit_)
    return {};
  return publish(site, fields, RawPartition::control, &*credit_);
}
RawEventRef RawParticipantBank::linked(std::uint32_t site, RawEventRef parent,
                                       std::span<const ByteView> fields) noexcept {
  std::array<ByteView, 24> body{};
  if (fields.size() > body.size() - 2) {
    raw_fence({ErrorCode::capacity});
    return {};
  }
  body[0] = parent.lane;
  body[1] = detail::raw_native_field(parent.ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  return emit(site, std::span{body}.first(fields.size() + 2));
}
RawParticipantTrace::RawParticipantTrace(RawParticipantAction action,
                                         std::uint64_t bytes,
                                         RawParticipantOrigin origin)
    : bank_(bytes) {
  begin(action, origin);
}
RawParticipantTrace::RawParticipantTrace(RawParticipantAction action,
                                         RawParticipantBank bank,
                                         RawParticipantOrigin origin)
    : bank_(std::move(bank)) {
  begin(action, origin);
}
void RawParticipantTrace::begin(RawParticipantAction action,
                                const RawParticipantOrigin &origin) {
  const auto tag = static_cast<std::uint8_t>(action);
  const bool parent = origin.parent.has_value(),
             identities = origin.identities.has_value();
  std::array<ByteView, 8> fields{};
  std::size_t count = 0;
  fields[count++] = detail::raw_native_field(tag);
  fields[count++] = detail::raw_native_field(parent);
  if (parent) {
    fields[count++] = origin.parent->lane;
    fields[count++] = detail::raw_native_field(origin.parent->ordinal);
  }
  fields[count++] = detail::raw_native_field(identities);
  if (identities) {
    fields[count++] = origin.identities->attempt;
    fields[count++] = origin.identities->invocation;
    fields[count++] = origin.identities->generation;
  }
  request_ = bank_.emit(2100, std::span{fields}.first(count));
}
RawParticipantTrace::~RawParticipantTrace() { finish(5); }
void RawParticipantTrace::finish(std::uint8_t branch,
                                 std::optional<Error> error) noexcept {
  if (closed_)
    return;
  closed_ = true;
  std::array<ByteView, 4> fields{};
  fields[0] = detail::raw_native_field(branch);
  fields[1] = detail::raw_native_field(stage_);
  std::size_t count = 2;
  if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  (void)bank_.linked(2101, request_, std::span{fields}.first(count));
}
RawEventRef raw_participant_data(std::uint32_t site, RawEventRef parent,
                                 std::span<const ByteView> fields,
                                 std::uint64_t bytes) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  auto held = raw_claim_data(bytes);
  if (!held.has_value())
    throw held.error();
  auto credit = std::move(held).value();
  std::array<ByteView, 24> body{};
  if (fields.size() > body.size() - 2)
    throw Error{ErrorCode::capacity};
  body[0] = parent.lane;
  body[1] = detail::raw_native_field(parent.ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  return publish(site, std::span{body}.first(fields.size() + 2), RawPartition::data,
                 &credit);
}
std::string_view raw_participants_description() noexcept {
  return "participants2100-2136;E16;R=lane16,ordinal8;host native fields,no padding;"
         "2100:u8 action(configure1,start2,read3,send4,cancel5,await6,join7,archive8,"
         "drain9,shutdown10,restore11,cleanup12),bool parent,[R],bool identities,"
         "[attempt16,invocation16,generation16];2101:R,u8 branch,u8 stage,[Error];"
         "2102:startR,Text run,Text task,worker-lane16,unsigned concurrency;"
         "2103:RunR,admissionR,size_t inbox,bool stop;2104:workerR,admissionR,"
         "bool identities,[three16],bool source,[sourceR];2105:R,u8 branch,u8 stage,"
         "bool stop,[Error];2106:RunR,u8 branch,bool finished,bool sealed,bool stop,"
         "size_t inbox,size_t receipts,bool dirty;2107:transportR,u8 role,Text label,"
         "size_t maximum,u8 mode;2108:prepareR,u8 branch,[sourceR,u32 partition,"
         "size_t offset,size_t bytes,u8 extent,bool observed,[size_t used]],[Error];"
         "2109:handoffR,size_t capture_bytes,size_t captures,u8 branch,bool "
         "stop,[Error];"
         "2110:handoffR,size_t captures,size_t bytes;2111:handoffR,sourceR,"
         "u32 partition,size_t offset,size_t bytes,u8 exact_extent;"
         "2112:retainR,u8 branch,bool semantic_source,[journal16,u64 sequence],[Error];"
         "2113:actionR,size_t runs,unsigned concurrency,bool dirty;"
         "2114:saveR,u8 branch,bool identity,[application16],[Error];"
         "2115:actionR,Text run,Text message,Text from,Text text;"
         "2116:offerR,RunR,u8 branch,size_t inbox,bool stop;"
         "2117:parentR,RunR,u8 source,bool stop,bool finished;"
         "2118:offerR,u8 branch,bool matched,[offerR],size_t inbox,size_t "
         "receipts,[Error];"
         "2119:joinR,RunR,bool sealed,size_t inbox;2120:startR,u8 method,"
         "int args[3](socketpair:domain,type,protocol;fcntl:fd,command,flags);"
         "2121:R,u8 method,int result,int errno,bool fds,[int fds[2]];"
         "2122:stopR,int fd,int how;2123:R,int result,int errno;"
         "2124:Run/startR,int fd,u8 reason;2125:R,int result,int errno;"
         "2126:RunR,bool stop,bool finished,bool sealed,size_t inbox,bool emergency,"
         "u8 branch,[Error];2127:actionR,RunR,bool joinable;2128:R,u8 branch,[Error];"
         "2129:restoreR,Text run,Text old_state,u8 branch,bool finished,bool sealed;"
         "2130:parentR,bool present;2131:R,u8 branch,[Error];"
         "2132:readR,Run/rowR,size_t inbox,size_t receipts,bool sealed,bool finished,"
         "bool stop,bool dirty;2133:R,unsigned old,unsigned new;"
         "2134:R,RunR,u8 branch,size_t runs;2135:drainR,handoffR,size_t bytes,"
         "u8 branch,[Error];2136:cleanupR,RunR,bool worker_END,[R],bool semantic_debt,"
         "u8 branch,[Error];Text=size_t,original bytes;Error=ErrorCode:u8,i64 detail";
}
} // namespace blackbird
