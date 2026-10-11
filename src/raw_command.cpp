#include "blackbird/raw_command.hpp"
#include "blackbird/process_lifetime.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
namespace blackbird {
namespace {
thread_local RawCapacityClaim *burst_data = nullptr;
thread_local RawControlToken *burst_control = nullptr;
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
std::uint64_t record_bytes(std::span<const ByteView> fields) {
  std::uint64_t bytes = raw_envelope_size;
  for (auto field : fields) {
    if (field.size() > UINT32_MAX - bytes)
      throw Error{ErrorCode::overflow};
    bytes += field.size();
  }
  return bytes;
}
} // namespace
std::optional<RawControlToken> raw_command_claim(std::uint64_t bytes) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  auto held = burst_control ? burst_control->split(bytes) : raw_claim_control(bytes);
  if (!held.has_value())
    throw held.error();
  return std::move(held).value();
}
void raw_command_data_credit(std::uint64_t bytes) {
  const ErrnoGuard guard;
  if (!raw_enabled() || bytes == 0)
    return;
  if (burst_data) {
    if (bytes > burst_data->remaining())
      throw Error{ErrorCode::capacity};
    return;
  }
  auto admitted = raw_wait_data(bytes);
  if (!admitted.has_value())
    throw admitted.error();
}
RawEventRef raw_command_record(std::uint32_t site, std::span<const ByteView> fields,
                               RawControlToken *credit) noexcept {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  auto emitted =
      raw_emit(site, fields, credit ? RawPartition::control : RawPartition::data,
               credit ? credit : burst_data);
  if (!emitted.has_value()) {
    raw_fence(emitted.error());
    return {};
  }
  return {raw_lane_epoch(), emitted.value()};
}
RawEventRef raw_command_data(std::uint32_t site, std::span<const ByteView> fields) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  raw_command_data_credit(record_bytes(fields));
  return raw_command_record(site, fields);
}
RawEventRef raw_command_linked(std::uint32_t site, RawEventRef request,
                               std::span<const ByteView> fields,
                               RawControlToken *credit) noexcept {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  std::array<ByteView, 24> body{};
  if (fields.size() > body.size() - 2) {
    raw_fence({ErrorCode::capacity});
    return {};
  }
  body[0] = request.lane;
  body[1] = detail::raw_native_field(request.ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  return raw_command_record(site, std::span{body}.first(fields.size() + 2), credit);
}
RawEventRef raw_command_linked_data(std::uint32_t site, RawEventRef request,
                                    std::span<const ByteView> fields) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  raw_command_data_credit(record_bytes(fields) + sizeof(IdentityBytes) +
                          sizeof(std::uint64_t));
  return raw_command_linked(site, request, fields);
}
RawCommandBurstScope::RawCommandBurstScope(RawCommandBurstCredits credits) noexcept
    : previous_data_(burst_data), previous_control_(burst_control) {
  burst_data = credits.data;
  burst_control = credits.control;
}
RawCommandBurstScope::~RawCommandBurstScope() {
  burst_data = previous_data_;
  burst_control = previous_control_;
}
RawCommandWake::RawCommandWake() {
  const std::lock_guard lock{detail::process_spawn_mutex};
  if (::pipe(endpoints_.data()) != 0)
    throw Error{ErrorCode::io, errno};
  for (int &fd : endpoints_) {
    const int normalized = ::fcntl(fd, F_DUPFD_CLOEXEC, 4);
    if (normalized < 0) {
      const int error = errno;
      ::close(endpoints_[0]);
      ::close(endpoints_[1]);
      throw Error{ErrorCode::io, error};
    }
    ::close(fd);
    fd = normalized;
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      const int error = errno;
      ::close(endpoints_[0]);
      ::close(endpoints_[1]);
      throw Error{ErrorCode::io, error};
    }
  }
}
RawCommandWake::~RawCommandWake() {
  ::close(endpoints_[0]);
  ::close(endpoints_[1]);
}
void RawCommandWake::notify() noexcept {
  const ErrnoGuard guard;
  const char byte = 1;
  (void)::write(endpoints_[1], &byte, 1);
}
void RawCommandWake::clear() noexcept {
  const ErrnoGuard guard;
  std::array<char, 128> bytes{};
  while (::read(endpoints_[0], bytes.data(), bytes.size()) > 0) {
  }
}
std::string_view raw_command_description() noexcept {
  return R"(command-v1 native ABI; fields occur in the stated order. Ref=lane:16 bytes,ordinal:u64; Text=length:size_t followed by exact length bytes; Error=ErrorCode,i64 detail (native Error.detail). Requests and returns are separate events. Native errno is copied even on success and is meaningful only when the operation reports failure. Output and keyboard positive read bodies are written before interpretation/allocation/limit branches. DATA is normal; held cleanup and selected prepaid retained_append regions use CONTROL.
2000:job Text,operation:u8(1=start),owner_present:u8(0).
2001 caller variant(request Ref):branch:u8(1=admitted,2=Error,3=exception),Error,max_live:size_t,max_records:size_t. Worker variant(launch Ref):operation:u8(1),Error; the Ref identifies the variant by its referenced site.
2002:request Ref,job Text,pty:bool,lifetime:chrono::seconds::rep.
2003:launch Ref,setup_stage:u8(0=before setup,1=setup,2=spawn),result:int,errno:int,pid:pid_t,group_ready:bool.
2004:launch Ref,fd:int,requested:size_t.
2005:request Ref,result:ssize_t,errno:int,positive exact result bytes.
2006:launch Ref,group_ready:bool,helper_stage:u8,failure:int. Helper failure stages:1 argc,2 setsid,3 slave,4 controlling tty,5 dup,6 CLOEXEC,7 ready,8 argv,9 exec. Ready packet=byte1; failure packet=byte2,stage:u8,errno:int.
2007:launch Ref,fd:int,offset:size_t,requested:size_t.
2008:request Ref,result:ssize_t,errno:int,positive exact result bytes (PTY EIO remains native EIO).
2009:read Ref,job Text,offset:size_t,pending_bytes:size_t,tail_bytes:size_t,branch:u8,Error.
2010:read Ref,job Text,offset:size_t,pending_bytes:size_t. The shared original remains owned until acknowledgment.
2011:read Ref,job Text,offset:size_t,length:size_t.
2012:request Ref,retained:size_t,branch:u8(1=preserved,2=Error,3=exception),Error.
2013:eight Text fields:job,operation,delivery,input bytes,signal name,rows,columns,epoch. Final three are empty; numbers are captured at their existing native business operation. Strings are copied without formatting native Value.
2014:request Ref,branch:u8(1=accepted,2=duplicate,3=Error,4=exception,5=unresolved),written:size_t,done:bool,failed:bool,Error.
2015:request Ref,job Text,delivery Text,branch:u8(1=enqueue,2=dequeue).
2016:launch Ref,delivery Text,fd:int,written:size_t,requested:size_t. Bytes were retained in2013, never reconstructed here.
2017:request Ref,result:ssize_t,errno:int.
2018:launch Ref,delivery Text,fd:int,rows:ushort,columns:ushort. Empty delivery names initial resize.
2019:request Ref,result:int,errno:int.
2020:launch Ref,delivery Text,target:pid_t,signal:int,method:u8(1=group kill,2=leader kill,3=getpgid). Empty delivery names cleanup.
2021:request Ref,result:int,errno:int; method3 additionally returned_group:pid_t.
2022:launch Ref,type:idtype_t,selector:id_t,options:int.
2023:request Ref,result:int,errno:int,si_pid:pid_t,si_uid:uid_t,si_code:int,si_status:int. Initialized fields are not asserted valid on failure.
2024:job Text,yield_present:bool,optional yield_ms:u64,max_output:size_t,stop_on_cancel:bool.
2025:request Ref,epoch:u64,pty:bool.
2026:request Ref,epoch:u64,branch:u8(1=terminal retained,2=released,3=yield,4=cancel,5=refusal,6=exception),Error,terminal_retained:bool,leader_exited:bool,output_closed:bool.
2027:job Text,requested_epoch:u64.
2028:request Ref,current_epoch:u64,released:bool,Error.
2029:job Text,source:u8(1=explicit API stop,2=worker deadline,3=worker capture failure,4=worker Error/exception,5=caller wait cancel,6=caller capture failure,7=caller shutdown). Worker emits only before its own first sticky stop transition.
2030:launch Ref,channel:u8,fd:int,reason:u8.
2031:request Ref,result:int,errno:int. Close is attempted once.
2032:launch Ref,pid:pid_t,options:int.
2033:request Ref,result:pid_t,errno:int,status_valid:bool,status:int. Status always has a field and only status_valid makes it usable. Boundary is the complete existing EINTR-retry reap operation.
2034:launch Ref,job Text,pid:pid_t,received:size_t,failure:int,capture_complete:bool,timed_out:bool,stop:bool,leader_exited:bool,output_closed:bool,custody:u8(0=not launched,1=owned,2=reaped,3=unavailable,4=pending).
2035:worker-terminal Ref,job Text.
2036:request Ref,branch:u8(1=settled,2=Error,3=exception),Error.
2037:job/output_ref Text,mode:u8. Modes0 registry and1 resident additionally offset:u64,requested:u64; mode2 full journal read has no range fields.
2038 resident:request Ref,original read Ref,at:size_t,length:size_t. Journal:request Ref,fact journal:16 bytes,fact sequence:u64,source journal:16 bytes,source sequence:u64,length:size_t. Referenced2037 mode identifies the variant.
2039:request Ref,branch:u8(1=success,2=Error,3=exception),Error,known source/retained size:size_t.
2040:fd:int,requested:size_t (bounded128).
2041:request Ref,result:ssize_t,errno:int,positive exact result bytes.
2042:keyboard-read Ref,job Text,foreground_epoch Text,routed Text,branch:u8(1=accepted,2=refused),pending:size_t.
2043:job Text,pending input Text (bounded4096; same original job through refusal).
2044:request Ref,accepted:bool,Error.
2045:delivery request Ref (zero for initial object shutdown),job Text (empty for shutdown),operation:u8(1=configure,2=archive,3=initial orderly shutdown),branch:u8(1=success,2=Error,3=exception),Error,max_live:size_t,max_records:size_t.
2046:launch Ref,output_fd:int,output_events:short,bootstrap_fd:int,bootstrap_events:short,timeout:int.
2047:request Ref,result:int,errno:int,output_revents:short,bootstrap_revents:short.
)";
}
} // namespace blackbird
