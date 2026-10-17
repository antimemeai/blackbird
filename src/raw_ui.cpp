#include "blackbird/raw_ui.hpp"
#include "blackbird/raw_caller.hpp"
#include <algorithm>
#include <cerrno>
#include <unistd.h>
namespace blackbird {
namespace {
thread_local RawControlToken *ui_control = nullptr;
struct ErrnoGuard {
  int value = errno;
  ~ErrnoGuard() { errno = value; }
};
constexpr std::uint8_t ui_branch(RawOperationBranch branch) noexcept {
  switch (branch) {
  case RawOperationBranch::returned:
    return 0;
  case RawOperationBranch::error:
    return 1;
  case RawOperationBranch::allocation:
    return 2;
  case RawOperationBranch::length:
    return 3;
  case RawOperationBranch::standard:
    return 4;
  case RawOperationBranch::nonstandard:
    return 5;
  default:
    return 6;
  }
}
constexpr std::uint64_t frame_outcome =
    raw_envelope_size + 24 + 1 + sizeof(std::size_t);
constexpr std::uint64_t frame_request =
    raw_envelope_size + 4 * sizeof(std::size_t) + sizeof(bool);
constexpr std::uint64_t frame_credit =
    frame_request + 3 * frame_outcome + raw_ui_error_bytes;
} // namespace
bool raw_ui_source_valid(RawEventRef source) noexcept {
  return raw_reference_valid(source);
}
std::uint64_t raw_ui_add(std::uint64_t left, std::uint64_t right) {
  if (right > UINT64_MAX - left)
    throw Error{ErrorCode::overflow};
  return left + right;
}
std::uint64_t raw_ui_multiply(std::uint64_t left, std::uint64_t right) {
  if (left && right > UINT64_MAX / left)
    throw Error{ErrorCode::overflow};
  return left * right;
}
std::uint64_t raw_ui_record_bytes(std::span<const ByteView> fields) {
  return raw_record_bytes(fields);
}
std::optional<RawControlToken> raw_ui_control(bool enabled, std::uint64_t bytes,
                                              RawWaitControl wait) {
  const ErrnoGuard guard;
  if (!enabled || !raw_enabled())
    return {};
  auto result = ui_control ? ui_control->split(bytes) : raw_claim_control(bytes, wait);
  if (!result.has_value()) {
    raw_capture_failed(result.error());
    throw result.error();
  }
  return std::move(result).value();
}
std::optional<RawCapacityClaim> raw_ui_data(bool enabled, std::uint64_t bytes,
                                            RawWaitControl wait) {
  const ErrnoGuard guard;
  if (!enabled || !raw_enabled())
    return {};
  auto result = raw_claim_data(bytes, wait);
  if (!result.has_value()) {
    raw_capture_failed(result.error());
    throw result.error();
  }
  return std::move(result).value();
}
RawEventRef raw_ui_emit(std::uint32_t site, std::span<const ByteView> fields,
                        RawPartition partition, RawControlToken *credit,
                        Error *failure) noexcept {
  if (!credit)
    return {};
  const auto emitted = raw_publish(site, fields, partition, *credit);
  if (!emitted.has_value()) {
    if (failure)
      *failure = emitted.error();
    return {};
  }
  return emitted.value();
}
RawEventRef raw_ui_link(std::uint32_t site, RawEventRef parent,
                        std::span<const ByteView> fields,
                        RawControlToken *credit) noexcept {
  if (!credit)
    return {};
  const auto emitted =
      raw_publish_linked(site, parent, fields, RawPartition::control, *credit);
  return emitted.has_value() ? emitted.value() : RawEventRef{};
}
RawUiBurst::RawUiBurst(RawControlToken *control) noexcept
    : previous_control_(ui_control) {
  ui_control = control;
}
RawUiBurst::~RawUiBurst() { ui_control = previous_control_; }
RawUiTrace::RawUiTrace(bool enabled, RawUiSites sites, std::span<const ByteView> fields,
                       std::uint64_t credit, bool data_request, RawWaitControl wait,
                       RawCapacityClaim *preheld_data)
    : error_site_(sites.terminal) {
  const ErrnoGuard guard;
  auto control = raw_ui_control(enabled, credit, wait);
  if (!control)
    return;
  Error failure{ErrorCode::audit_unavailable};
  RawEventRef request{};
  if (data_request) {
    const auto bytes = raw_ui_record_bytes(fields);
    std::optional<RawCapacityClaim> data;
    if (preheld_data) {
      auto acquired = preheld_data->split(bytes);
      if (!acquired.has_value()) {
        raw_capture_failed(acquired.error());
        throw acquired.error();
      }
      data.emplace(std::move(acquired).value());
    } else
      data = raw_ui_data(true, bytes, wait);
    request = raw_ui_emit(sites.request, fields, RawPartition::data,
                          data ? &*data : nullptr, &failure);
  } else {
    request =
        raw_ui_emit(sites.request, fields, RawPartition::control, &*control, &failure);
  }
  if (!request.ordinal)
    throw failure;
  const auto adopted = emission_.adopt(request, sites.terminal, std::move(*control));
  if (!adopted.has_value())
    throw adopted.error();
}
RawUiTrace::~RawUiTrace() { finish(6); }
RawEventRef RawUiTrace::event(std::uint32_t site,
                              std::span<const ByteView> fields) noexcept {
  return raw_ui_link(site, reference(), fields, credit());
}
void RawUiTrace::finish(std::uint8_t branch, std::optional<Error> error) noexcept {
  if (!enabled())
    return;
  std::array<ByteView, 6> fields{};
  std::size_t count = 2;
  fields[0] = detail::raw_native_field(branch);
  fields[1] = detail::raw_native_field(stage_);
  if (error_site_ == 4009 || error_site_ == 4084)
    fields[count++] = detail::raw_native_field(progress_);
  if (error_site_ == 4084)
    fields[count++] = detail::raw_native_field(extent_);
  if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  emission_.close_linked(std::span{fields}.first(count));
}
void RawUiTrace::finish(RawOperationBranch branch, const Error *error) noexcept {
  finish(ui_branch(branch), error ? std::optional{*error} : std::nullopt);
}
void RawUiTrace::finish_system(int code) noexcept {
  std::array<ByteView, 5> fields{};
  std::size_t count = 2;
  const std::uint8_t branch = 7;
  fields[0] = detail::raw_native_field(branch);
  fields[1] = detail::raw_native_field(stage_);
  if (error_site_ == 4009 || error_site_ == 4084)
    fields[count++] = detail::raw_native_field(progress_);
  if (error_site_ == 4084)
    fields[count++] = detail::raw_native_field(extent_);
  fields[count++] = detail::raw_native_field(code);
  emission_.close_linked(std::span{fields}.first(count));
}
void RawUiTrace::capture_exception(std::uint32_t site) noexcept {
  struct ActualCaught {
    RawUiTrace &trace;
    std::uint32_t site;
    std::uint8_t stage;
    std::size_t progress, extent;
    void finish(RawOperationBranch branch, const Error *error = nullptr) noexcept {
      const auto tag = ui_branch(branch);
      std::array<ByteView, 6> fields{};
      std::size_t count = 2;
      fields[0] = detail::raw_native_field(tag);
      fields[1] = detail::raw_native_field(stage);
      if (site == 4084) {
        fields[count++] = detail::raw_native_field(progress);
        fields[count++] = detail::raw_native_field(extent);
      }
      if (error) {
        fields[count++] = detail::raw_native_field(error->code);
        fields[count++] = detail::raw_native_field(error->detail);
      }
      (void)trace.event(site, std::span{fields}.first(count));
    }
    void finish_system(int code) noexcept {
      const std::uint8_t tag = 7;
      std::array<ByteView, 5> fields{};
      std::size_t count = 2;
      fields[0] = detail::raw_native_field(tag);
      fields[1] = detail::raw_native_field(stage);
      if (site == 4084) {
        fields[count++] = detail::raw_native_field(progress);
        fields[count++] = detail::raw_native_field(extent);
      }
      fields[count++] = detail::raw_native_field(code);
      (void)trace.event(site, std::span{fields}.first(count));
    }
  } actual{*this, site, stage_, progress_, extent_};
  try {
    raw_call_run<false>(actual, [] { throw; });
  } catch (...) {
    // The enclosing business catch still owns the actual exception. This call
    // publishes its intermediate outcome without ending the surrounding owner.
  }
}
std::optional<RawControlToken> RawUiTrace::split(std::uint64_t bytes) {
  if (!enabled())
    return {};
  auto result = credit()->split(bytes);
  if (!result.has_value()) {
    raw_capture_failed(result.error());
    throw result.error();
  }
  return std::move(result).value();
}
namespace {
struct OriginFields {
  std::array<bool, 3> present;
  std::array<ByteView, 10> body{};
  std::size_t count = 0;
  explicit OriginFields(const RawUiValueOrigin &origin)
      : present{origin.source.has_value(), origin.fact.has_value(),
                origin.revision.has_value()} {
    body[count++] = detail::raw_native_field(origin.kind);
    body[count++] = detail::raw_native_field(present[0]);
    if (origin.source) {
      body[count++] = origin.source->lane;
      body[count++] = detail::raw_native_field(origin.source->ordinal);
    }
    body[count++] = detail::raw_native_field(present[1]);
    if (origin.fact)
      body[count++] = origin.fact->bytes();
    body[count++] = detail::raw_native_field(present[2]);
    if (origin.revision)
      body[count++] = detail::raw_native_field(*origin.revision);
  }
  std::span<const ByteView> fields() const noexcept {
    return std::span{body}.first(count);
  }
};
} // namespace
RawUiTrace raw_ui_value_trace(bool enabled, const RawUiValueOrigin &origin) {
  // Max origin68 + actual terminal51 + optional ordinary notify106.
  return RawUiTrace(enabled, {4050, 4051}, OriginFields{origin}.fields(), 225, false,
                    RawWaitControl{});
}
void raw_ui_notify(RawUiTrace &trace, int descriptor) noexcept {
  if (descriptor < 0)
    return;
  const char byte = 1;
  const std::size_t requested = 1;
  const auto request =
      trace.event(4003, std::array{detail::raw_native_field(descriptor),
                                   detail::raw_native_field(requested),
                                   detail::raw_native_field(byte)});
  const auto result = ::write(descriptor, &byte, 1);
  const int error = errno;
  const bool failed = result < 0;
  if (failed)
    (void)raw_ui_link(4004, request,
                      std::array{detail::raw_native_field(result),
                                 detail::raw_native_field(failed),
                                 detail::raw_native_field(error)},
                      trace.credit());
  else
    (void)raw_ui_link(
        4004, request,
        std::array{detail::raw_native_field(result), detail::raw_native_field(failed)},
        trace.credit());
  errno = error;
}
RawUiOwner::RawUiOwner(bool selected) {
  if (!selected || !raw_enabled())
    return;
  issuer_ = raw_current_thread();
  if (!issuer_)
    throw Error{ErrorCode::invalid_identity};
  thread_ = std::this_thread::get_id();
}
void RawUiOwner::require_thread() const {
  if (issuer_ && thread_ != std::this_thread::get_id())
    throw Error{ErrorCode::invalid_identity};
}
void RawUiOwner::require_cleanup_thread() const noexcept {
  if (issuer_ && thread_ != std::this_thread::get_id())
    std::terminate();
}
RawUiOwnerBinding::RawUiOwnerBinding(const RawUiOwner &owner) {
  owner.require_thread();
  if (owner.selected())
    binding_.emplace(owner.issuer());
}
RawUiPacket::RawUiPacket(RawUiOwner issuer, RawEventRef source,
                         std::optional<RawControlToken> credit) noexcept
    : owner(std::move(issuer)), frame(source), terminal(std::move(credit)) {}
RawUiPacket::RawUiPacket(RawUiPacket &&other) noexcept
    : owner(std::move(other.owner)), frame(other.frame),
      terminal(std::move(other.terminal)) {
  other.terminal.reset();
}
RawUiPacket &RawUiPacket::operator=(RawUiPacket &&other) noexcept {
  if (this != &other) {
    retire();
    owner = std::move(other.owner);
    frame = other.frame;
    terminal = std::move(other.terminal);
    other.terminal.reset();
  }
  return *this;
}
RawUiPacket::~RawUiPacket() { retire(); }
void RawUiPacket::retire() noexcept {
  if (!terminal)
    return;
  owner.require_cleanup_thread();
  RawUiOwnerBinding binding{owner};
  const std::uint8_t branch = 4;
  const std::size_t offset = 0;
  (void)raw_ui_link(
      4042, frame,
      std::array{detail::raw_native_field(branch), detail::raw_native_field(offset)},
      &*terminal);
  terminal.reset();
}
RawUiFrame::RawUiFrame(bool enabled, RawUiGeometry geometry, std::uint64_t packet_bound,
                       RawCapacityClaim *preheld_data)
    : owner_(enabled),
      trace_(enabled, {4040, 4043},
             std::array{detail::raw_native_field(geometry.width),
                        detail::raw_native_field(geometry.height),
                        detail::raw_native_field(geometry.cursor_row),
                        detail::raw_native_field(geometry.cursor_column),
                        detail::raw_native_field(enabled)},
             frame_credit) {
  trace_.run([&] {
    const auto amount =
        raw_ui_add(raw_envelope_size + 24 + sizeof(std::size_t), packet_bound);
    if (trace_.enabled() && preheld_data) {
      auto split = preheld_data->split(amount);
      if (!split.has_value()) {
        raw_capture_failed(split.error());
        throw split.error();
      }
      data_.emplace(std::move(split).value());
    } else
      data_ = raw_ui_data(trace_.enabled(), amount);
  });
}
RawUiFrame::RawUiFrame(bool enabled, std::size_t bound, RawCapacityClaim *preheld_data)
    : RawUiFrame(enabled, {}, bound, preheld_data) {}
RawUiFrame::~RawUiFrame() {
  owner_.require_cleanup_thread();
  RawUiOwnerBinding binding{owner_};
  if (captured_ && trace_.enabled())
    discard();
  else
    trace_.finish(6);
}
void RawUiFrame::capture(std::string_view packet) {
  RawUiOwnerBinding binding{owner_};
  if (!data_)
    return;
  const auto ref = trace_.reference();
  const auto size = packet.size();
  Error failure{ErrorCode::audit_unavailable};
  const auto emitted = raw_ui_emit(
      4041,
      std::array{ByteView{ref.lane}, detail::raw_native_field(ref.ordinal),
                 detail::raw_native_field(size), detail::raw_string_field(packet)},
      RawPartition::data, &*data_, &failure);
  data_.reset();
  if (!emitted.ordinal) {
    failure_ = failure;
    trace_.finish(1, failure);
    return;
  }
  captured_ = true;
  const std::uint8_t branch = 1;
  const std::size_t offset = 0;
  (void)trace_.event(4042, std::array{detail::raw_native_field(branch),
                                      detail::raw_native_field(offset)});
}
void RawUiFrame::discard() {
  RawUiOwnerBinding binding{owner_};
  const std::uint8_t branch = 4;
  const std::size_t offset = 0;
  (void)trace_.event(4042, std::array{detail::raw_native_field(branch),
                                      detail::raw_native_field(offset)});
  trace_.close();
}
RawUiPacket RawUiFrame::handoff() {
  RawUiOwnerBinding binding{owner_};
  if (failure_)
    throw *failure_;
  if (trace_.enabled() && !captured_) {
    const Error error{ErrorCode::invalid_identity};
    trace_.finish(1, error);
    throw error;
  }
  auto terminal = trace_.split(2 * frame_outcome);
  const auto ref = trace_.reference();
  trace_.close();
  return {owner_, ref, std::move(terminal)};
}
std::string_view raw_ui_description() noexcept {
  return "instrument4090-4098;R=lane16,u64 ordinal;S=size_t N,bytes;"
         "D=environment16,conversation16,7u64(view-owner,view-incarnation,source,"
         "request,scene,input,query);T=3S(owner,kind,key),u64 incarnation;"
         "K=inbox16,u64 owner,u64 trigger,u32 ordinal;"
         "4090:originalR,inputR,S path,bool preview,u64 generation;"
         "4091:environment16,conversation16,inputR,size_t slot,S job,7size_t("
         "requested,begin,next,size,cached-begin,retained-end,received-end),"
         "2bool(omitted-prefix,provenance-complete),u8 end,exact size bytes,"
         "size_t sources,each(R,3size_t chunk-begin/offset/length);"
         "4092:D,source-proposalR,fileR,inputR,size_t rows,each(2S key/label,T),"
         "2each(pinR,S title);"
         "4093:inputR,D,u8 Completion,ErrorCode,i64,S diagnostic,size_t accepted,"
         "size_t handlers,each S,bool candidate,[candidate:2u64 view,u64 base,"
         "size_t nodes,each(S key,bool parent,[S parent],u8 kind,bool focus),"
         "size_t collections,each(2S node/title,u8 notice,S notice,size_t items,"
         "each(2S key/label,bool target,[3S owner/kind/key,u64 incarnation])),"
         "size_t inspectors,each(2S node/title,bool target,[3S owner/kind/key,"
         "u64 incarnation],u8 notice,S notice,u64 offset,size_t lines,each("
         "size_t spans,each(S text,u8 Ink))),bool draft,[u64 base,S text,size_t "
         "cursor]];"
         "4094:outcomeR,inputR,D,bool accepted,ErrorCode,i64;"
         "4095:K,D,u64 acceptance-order,inputR,T,5S(handler,verb,request-id,op,signal);"
         "4096:forwardR,u8 BridgeDisposition,bool forward-called,bool "
         "error,ErrorCode,i64;"
         "4097:D,inputR,source-proposalR,fileR,T,S handler,K;"
         "4098:u8 disposition(unknown-on-runtime-exit1),size_t remaining,"
         "each(K,D,u64 acceptance-order,inputR,T,2S handler/verb);"
         "all instrument sites DATA except4094/4096 CONTROL;no struct padding;"
         "terminal4100-4179;4100:parentR,u8 "
         "custody(acquire1,resume2,suspend3,cleanup4,wake6,install8,diagnostic9);"
         "4101:caller end(R,u8 RawOperationBranch,u8 stage,bool Error,[ErrorCode,i64]);"
         "4102:R,int fd;4103:R,int result,bool failed,[int errno on failure|four "
         "tcflag_t,bool line,[cc_t line on Linux],c_cc array,two speed_t on success];"
         "4104:R,int fd,action,four tcflag_t,bool line,[cc_t line on Linux],c_cc "
         "array,two speed_t;4105:R,int result,bool failed,[int errno];"
         "4106:R,u8 method,bool active;4107:custodyR,actual Frame-requestR(no copied "
         "packet);"
         "4108/4115:R,nfds_t count,each int fd/short events,int "
         "timeout;4109/4116:R,int result,bool failed,[int errno|each short revents on "
         "nonnegative];"
         "4110:R,u8 method(pipe1,fcntl2,close3),[fcntl:int fd,command,arg|close:int "
         "fd];"
         "4111:R,int result,bool failed,[int errno on failure|pipe:two int fd on "
         "success];4112:R,u8 method,int fd,bool owned;"
         "4113:R,int fd,queue;4114:R,int result,bool failed,[int errno];4117:R,int "
         "fd,size_t requested;4118:R,ssize_t result,bool failed,[int errno];"
         "4119:readR,size_t N,actual positive bytes;4120:parentR,size_t draftN,actual "
         "draft original(DATA request);4121:existing UI outcome;"
         "4122:R,u8 role,bool present,[size_t N,actual bytes];CString "
         "roles1/2/3/4/5/8/9 include existing NUL;counted "
         "roles6/7/"
         "10;"
         "roles1tempEnv2template3createdPath4editor5command6returnedDraft7readChunk8dir"
         "ectory9what10diagnostic;"
         "4123:R,u8 "
         "native(mkstemp1,fcntl2,write3,close4,spawn5,wait6,open7,fstat8,read9,"
         "unlink10,stat11,kill12);"
         "mkstemp:templateR;fcntl:int fd,command,arg;write:int fd,inputR,size_t "
         "offset,N;close:int fd;"
         "spawn:commandR,NUL /bin/sh,sh,-c,environ pointer;wait:pid_t child,int "
         "options;open:pathR,int flags;fstat:int fd;read:int fd,size_t "
         "requested;unlink/stat:pathR;kill:pid_t child,int signal;"
         "4124:R,native return,bool failed,[int errno on failed except spawn "
         "rc-is-error];spawn:[pid_t only success];wait:[int rawStatus if "
         "positive];fstat:[mode_t,off_t on success];stat:[mode_t on success];"
         "4125:R,bool accepted,bool draftSource,[sourceR],u8 messageToken;4126:R,int "
         "localeCategory,NUL char;4127:R,bool present,char* opaque;4128:R,pid_t,bool "
         "owned,reaped;"
         "ui4000-4199;E16;R=lane16,ordinal8;native fields,host order,no padding;"
         "generic outcome:R,u8 "
         "branch(return0,Error1,allocation2,length3,standard4,nonstandard5,abandoned6,"
         "system7),u8 stage,[ErrorCode,i64 detail only Error],[int error_code only "
         "system];"
         "4000:Kind,Ink,size_t text,original bytes;4001:ingressR,targetR,u8 "
         "insert1/coalesce2,size_t size;4002:generic outcome;"
         "4003:ingressR,int fd,size_t request,char byte;4004:writeR,ssize_t "
         "result,bool error,[int errno];"
         "4005:empty batch request;4008:batchR,size_t count,firstR,lastR;4009:R,u8 "
         "branch,u8 stage,size_t actually applied,[ErrorCode,i64 detail only Error];"
         "4006:batchR,enqueueR,lastR,Kind,Ink,size_t text;4007:generic outcome;"
         "4010:actual_feedR,size_t index,InputAction,size_t cursor,size_t "
         "draft,[size_t text,bytes for submit/choose/select_view];4011:actionR,u8 "
         "branch,targetR;4012:generic outcome;"
         "4013:actionR,u8 queue,sourceR,size_t offset,size_t bytes;4014:queueR,u8 "
         "branch,size_t index;4015:queueR,u8 branch,size_t index,targetR;4016:generic "
         "outcome;"
         "4017:actual_actionR;4018:sourceR,size_t offset,length;"
         "4076:sourceR,dispatchR;4077:generic outcome(stage1 admission,stage2 "
         "perform,stage3 completion);4078:generic actual perform exception;"
         "4020:u8 load/save,size_t path,bytes;4021:stateR,size_t bytes,original native "
         "packet;4022:stateR,u8 branch,size_t cursor;4023:generic outcome;"
         "4030:ChatKind,bool merge,sourceR,size_t "
         "text,bytes;4031:appendR,blockR,size_t result;4032:blockR,u8 reason,size_t "
         "offset,size_t length,u64 generation;4033:generic outcome;4034:size_t "
         "blocks,size_t bytes,u64 generation;"
         "4040:size_t width,height,cursor_row,cursor_column,bool "
         "owner;4041:frameR,size_t bytes,actual packet;4042:frameR,u8 "
         "constructed1/handoff2/complete3/retire4,size_t offset;4043:generic outcome;"
         "4044:frameR,int fd,size_t offset,size_t requested;4045:writeR,ssize_t "
         "result,bool error,[int errno];"
         "4046:int descriptor;4047:attachR,int fd,int flags,size_t offset;4048:int "
         "fd,unsigned long ioctl;4049:requestR,int result,bool valid,[ushort "
         "rows,columns on success],[int errno on failure];"
         "4050:u8 origin_kind,bool source,[R],bool fact,[application16],bool "
         "revision,[u64 revision];4051:generic outcome;4052:loadR,u8 field,size_t "
         "index,size_t bytes,decoder return bytes;4070:launchR,bool "
         "accepted;4071:actionR,size_t bytes,actual draft;4080:bool "
         "keyboard,[actual_keyboardR],size_t index,char "
         "input;4082:stateR;4083:requestR,bool exists,int error_code;4085:int "
         "fd,size_t requested;4086:batchR,native_readR,ssize_t result;4084:batchR,u8 "
         "branch,u8 stage,size_t processed,size_t extent,[ErrorCode,i64 detail only "
         "Error],[int error_code only system];4073:appendR,size_t "
         "blocks,bytes,newlines,u64 generation;4074:ushort rows,columns;4075:generic "
         "outcome;"
         "4087:u64 view_owner,incarnation,originR,u64 observation_generation,bool "
         "adopted,has_error,ErrorCode,i64 detail(error slots meaningful only "
         "has_error);"
         "then optional afterID,selectedID(each bool present,[32IDbytes]);size_t "
         "row_limit,byte_limit,chunk_limit,requested_offset;"
         "u8 status,size_t row_count,optional nextID,bool more_rows,row_count rows,u8 "
         "selection,[detail row only selection1];"
         "row=32IDbytes,u8 phase,custody,u64 admission_order,size_t "
         "received,retained;bool "
         "pty,leader_exited,output_closed,worker_finished,terminal_retained,capture_"
         "complete,stop_requested,timed_out,has_exit,sealed;int "
         "exit_code,signal,failure;launchR,terminalR;"
         "window=size_t "
         "size,source_count,requested,begin,next,cached_begin,retained_end,received_"
         "end;bool omitted_prefix,provenance_complete;u8 "
         "page_end;source_count(originalR,size_t chunk_begin,offset,length);size_t "
         "examined_rows,examined_chunks,seek_steps;"
         "status ready0,busy1,invalid_identity2,invalid_range3,unavailable4;selection "
         "not_requested0,resident1,not_resident2;phase "
         "starting0,running1,stopped2,draining3,settling4,settled5;custody "
         "not_launched0,owned1,reaped2,unavailable3,pending4;page_end "
         "frontier0,byte_limit1,chunk_limit2;no window byte duplication,ordered "
         "extents name actual native2008 originals;"
         "4060:attachR,int fd,int operation,int argument;4061:requestR,int result,bool "
         "error,[int errno];4062:generic outcome";
}
} // namespace blackbird
