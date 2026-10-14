#include "blackbird/raw_provider.hpp"

#include <array>
#include <cerrno>

namespace blackbird {
namespace {
using detail::raw_native_field;
using detail::raw_string_field;
struct ErrnoGuard {
  int value = errno;
  ~ErrnoGuard() { errno = value; }
};
void require(Result<void> result) {
  if (!result.has_value())
    throw result.error();
}
RawControlToken claim(std::uint64_t bytes) {
  auto result = raw_claim_control(bytes);
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
std::uint64_t data(std::uint32_t site, std::span<const ByteView> fields) {
  std::uint64_t bytes = raw_envelope_size;
  for (const auto field : fields) {
    if (field.size() > UINT32_MAX - bytes)
      throw Error{ErrorCode::overflow};
    bytes += field.size();
  }
  require(raw_wait_data(bytes));
  const auto result = raw_emit(site, fields);
  if (!result.has_value())
    throw result.error();
  return result.value();
}
void immediate(std::uint32_t site, std::span<const ByteView> fields,
               RawControlToken *credit) noexcept {
  const auto result = raw_emit(
      site, fields, credit ? RawPartition::control : RawPartition::data, credit);
  if (!result.has_value())
    raw_fence(result.error());
}
constexpr std::uint64_t ref_bytes = sizeof(IdentityBytes) + sizeof(std::uint64_t);
constexpr std::uint64_t close_bytes =
    raw_envelope_size + ref_bytes + 1 + 3 * sizeof(int);
constexpr std::uint64_t spawn_bytes =
    raw_envelope_size + ref_bytes + 1 + 2 * sizeof(int) + 2 * sizeof(pid_t);
constexpr std::uint64_t refusal_bytes =
    raw_envelope_size + ref_bytes + 2 + sizeof(ErrorCode) + sizeof(std::int64_t);
constexpr std::uint64_t cleanup_bytes =
    12 * close_bytes + raw_envelope_size + ref_bytes + sizeof(pid_t) + 3 * sizeof(int) +
    raw_envelope_size + ref_bytes + 2 * sizeof(pid_t) + sizeof(int) + sizeof(bool) +
    sizeof(int) + raw_envelope_size + ref_bytes + 1 + spawn_bytes + refusal_bytes;
constexpr std::uint64_t read_bytes = raw_envelope_size + ref_bytes + 1 +
                                     sizeof(std::size_t) + sizeof(ssize_t) +
                                     sizeof(int);
} // namespace

RawProviderTrace::RawProviderTrace(const RawProviderOwner *owner, int timeout_seconds,
                                   const RawEventRef *auth_resolution) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return;
  terminal_.emplace(claim(raw_envelope_size + ref_bytes + 2 + sizeof(ErrorCode) +
                          sizeof(std::int64_t) + raw_envelope_size + 2 * ref_bytes +
                          sizeof(int)));
  std::optional<RawControlToken> link;
  if (auth_resolution && auth_resolution->ordinal)
    link.emplace(claim(raw_envelope_size + 2 * ref_bytes));
  if (owner && !owner->dispatch.ordinal)
    owner = nullptr;
  const auto present = static_cast<std::uint8_t>(owner != nullptr);
  std::array<ByteView, 8> fields{};
  fields[0] = raw_native_field(present);
  std::size_t count = 1;
  if (owner) {
    fields[count++] = owner->attempt;
    fields[count++] = owner->invocation;
    fields[count++] = owner->generation;
    fields[count++] = owner->dispatch.lane;
    fields[count++] = raw_native_field(owner->dispatch.ordinal);
  }
  fields[count++] = raw_native_field(timeout_seconds);
  request_ = {raw_lane_epoch(), data(1300, std::span{fields}.first(count))};
  if (link) {
    const std::array linked{
        ByteView{auth_resolution->lane}, raw_native_field(auth_resolution->ordinal),
        ByteView{request_.lane}, raw_native_field(request_.ordinal)};
    immediate(1612, linked, &*link);
  }
}
RawProviderTrace::~RawProviderTrace() { finish(6); }
void RawProviderTrace::body(std::string_view method, std::string_view url,
                            std::string_view bytes, RawCapacityClaim *credit) {
  const ErrnoGuard guard;
  if (!terminal_)
    return;
  const auto method_size = method.size(), url_size = url.size(),
             body_size = bytes.size();
  const std::array fields{
      ByteView{request_.lane},       raw_native_field(request_.ordinal),
      raw_native_field(method_size), raw_string_field(method),
      raw_native_field(url_size),    raw_string_field(url),
      raw_native_field(body_size),   raw_string_field(bytes)};
  if (credit) {
    const auto result = raw_emit(1301, fields, RawPartition::data, credit);
    if (!result.has_value()) {
      raw_fence(result.error());
      throw result.error();
    }
  } else
    (void)data(1301, fields);
}
void RawProviderTrace::http(RawEventRef child, int status) {
  const ErrnoGuard guard;
  if (!terminal_)
    return;
  const std::array fields{ByteView{request_.lane}, raw_native_field(request_.ordinal),
                          ByteView{child.lane}, raw_native_field(child.ordinal),
                          raw_native_field(status)};
  immediate(1302, fields, &*terminal_);
}
void RawProviderTrace::finish(std::uint8_t branch, std::uint8_t stage,
                              Error error) noexcept {
  const ErrnoGuard guard;
  if (!terminal_)
    return;
  const std::array fields{
      ByteView{request_.lane},      raw_native_field(request_.ordinal),
      raw_native_field(branch),     raw_native_field(stage),
      raw_native_field(error.code), raw_native_field(error.detail)};
  immediate(1303, fields, &*terminal_);
  terminal_.reset();
}

namespace detail {
RawChildTrace::RawChildTrace(RawEventRef request, bool capture_stdout,
                             bool capture_stderr, bool merge_error, bool separate_error,
                             Error deadline_error)
    : stdout_(capture_stdout), stderr_(capture_stderr),
      deadline_error_(deadline_error) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return;
  cleanup_.emplace(claim(cleanup_bytes));
  const std::array fields{
      ByteView{request.lane},           raw_native_field(request.ordinal),
      raw_native_field(merge_error),    raw_native_field(separate_error),
      raw_native_field(capture_stdout), raw_native_field(capture_stderr)};
  child_ = {raw_lane_epoch(), data(1400, fields)};
}
RawChildTrace::~RawChildTrace() {
  const std::uint8_t branch = 1;
  const std::array fields{ByteView{child_.lane}, raw_native_field(child_.ordinal),
                          raw_native_field(branch)};
  terminal(1409, fields);
}
bool RawChildTrace::captures(std::uint8_t channel) const noexcept {
  return channel == 1 ? stdout_ : channel == 2 && stderr_;
}
RawNativePermit RawChildTrace::permit(std::uint64_t bytes) {
  const ErrnoGuard guard;
  RawNativePermit result;
  if (cleanup_) {
    RawCaptureFailureScope admission{true};
    try {
      result.control.emplace(claim(bytes));
    } catch (const Error &error) {
      if (error.code == ErrorCode::interrupted && error.detail == ETIMEDOUT) {
        (void)admission.consume(error);
        throw deadline_error_;
      }
      throw;
    }
  }
  return result;
}
RawNativePermit RawChildTrace::read_permit(RawReadChannel channel,
                                           std::size_t requested) {
  auto result = permit(read_bytes);
  if (cleanup_ && captures(static_cast<std::uint8_t>(channel))) {
    RawCaptureFailureScope admission{true};
    auto ready = raw_claim_data(read_bytes + requested);
    if (!ready.has_value()) {
      const auto error = ready.error();
      if (error.code == ErrorCode::interrupted && error.detail == ETIMEDOUT) {
        (void)admission.consume(error);
        throw deadline_error_;
      }
      throw error;
    }
    result.data.emplace(std::move(ready).value());
  }
  return result;
}
void RawChildTrace::terminal(std::uint32_t site,
                             std::span<const ByteView> fields) noexcept {
  const ErrnoGuard guard;
  if (cleanup_)
    immediate(site, fields, &*cleanup_);
}
void RawChildTrace::spawn(std::uint8_t stage, int result, int error, pid_t pid,
                          pid_t group) noexcept {
  if (spawn_written_)
    return;
  spawn_written_ = true;
  const std::array fields{ByteView{child_.lane},   raw_native_field(child_.ordinal),
                          raw_native_field(stage), raw_native_field(result),
                          raw_native_field(error), raw_native_field(pid),
                          raw_native_field(group)};
  terminal(1401, fields);
}
void RawChildTrace::poll(RawNativePermit &credit, std::uint8_t channel, int fd,
                         short events, int timeout, int result, short revents,
                         int error) noexcept {
  const ErrnoGuard guard;
  if (!credit.control)
    return;
  const std::array fields{ByteView{child_.lane},     raw_native_field(child_.ordinal),
                          raw_native_field(channel), raw_native_field(fd),
                          raw_native_field(events),  raw_native_field(timeout),
                          raw_native_field(result),  raw_native_field(revents),
                          raw_native_field(error)};
  immediate(1402, fields, &*credit.control);
}
void RawChildTrace::write(RawNativePermit &credit, std::size_t requested,
                          ssize_t result, int error) noexcept {
  const ErrnoGuard guard;
  if (!credit.control)
    return;
  const std::array fields{ByteView{child_.lane}, raw_native_field(child_.ordinal),
                          raw_native_field(requested), raw_native_field(result),
                          raw_native_field(error)};
  immediate(1403, fields, &*credit.control);
}
std::optional<RawEventRef> RawChildTrace::read(RawNativePermit &credit,
                                               std::uint8_t channel,
                                               std::size_t requested, ssize_t result,
                                               int error, const char *bytes) noexcept {
  const ErrnoGuard guard;
  if (!credit.control)
    return std::nullopt;
  std::array fields{ByteView{child_.lane},
                    raw_native_field(child_.ordinal),
                    raw_native_field(channel),
                    raw_native_field(requested),
                    raw_native_field(result),
                    raw_native_field(error),
                    ByteView{}};
  if (result > 0 && captures(channel)) {
    fields.back() = std::as_bytes(std::span{bytes, static_cast<std::size_t>(result)});
    const auto published = raw_emit(1404, fields, RawPartition::data,
                                    credit.data ? &*credit.data : nullptr);
    credit.data.reset();
    credit.control.reset();
    if (!published.has_value()) {
      raw_fence(published.error());
      return std::nullopt;
    }
    return RawEventRef{raw_lane_epoch(), published.value()};
  } else {
    immediate(1404, std::span{fields}.first(6), &*credit.control);
    credit.data.reset();
    credit.control.reset();
  }
  return std::nullopt;
}
void RawChildTrace::wait(RawNativePermit &credit, const siginfo_t &observed, int result,
                         int error) noexcept {
  const ErrnoGuard guard;
  if (!credit.control)
    return;
  const std::uint8_t method = 1;
  const std::array fields{ByteView{child_.lane},
                          raw_native_field(child_.ordinal),
                          raw_native_field(method),
                          raw_native_field(result),
                          raw_native_field(error),
                          raw_native_field(observed.si_pid),
                          raw_native_field(observed.si_uid),
                          raw_native_field(observed.si_code),
                          raw_native_field(observed.si_status)};
  immediate(1405, fields, &*credit.control);
}
void RawChildTrace::close(std::uint8_t channel, int fd, int result,
                          int error) noexcept {
  const std::array fields{ByteView{child_.lane},     raw_native_field(child_.ordinal),
                          raw_native_field(channel), raw_native_field(fd),
                          raw_native_field(result),  raw_native_field(error)};
  terminal(1406, fields);
}
void RawChildTrace::signal(pid_t group, int signal, int result, int error) noexcept {
  const std::array fields{ByteView{child_.lane},    raw_native_field(child_.ordinal),
                          raw_native_field(group),  raw_native_field(signal),
                          raw_native_field(result), raw_native_field(error)};
  terminal(1407, fields);
}
void RawChildTrace::reap(pid_t pid, pid_t result, int error, bool valid,
                         int status) noexcept {
  const std::array fields{ByteView{child_.lane},   raw_native_field(child_.ordinal),
                          raw_native_field(pid),   raw_native_field(result),
                          raw_native_field(error), raw_native_field(valid),
                          raw_native_field(status)};
  terminal(1408, fields);
}
void RawChildTrace::refuse(std::uint8_t operation, std::uint8_t stage,
                           Error error) noexcept {
  if (refusal_written_)
    return;
  refusal_written_ = true;
  const std::array fields{
      ByteView{child_.lane},        raw_native_field(child_.ordinal),
      raw_native_field(operation),  raw_native_field(stage),
      raw_native_field(error.code), raw_native_field(error.detail)};
  terminal(1410, fields);
}
} // namespace detail

std::string_view raw_provider_description() noexcept {
  return "1300 application request begin=u8 "
         "owner_present,[attempt16,invocation16,generation16,dispatch Ref],int timeout;"
         "1301 application external body=Ref,size_t method_length,method,size_t "
         "url_length,url,size_t body_length,body;"
         "1302 existing HTTP parse=Ref request,Ref child,int status;"
         "1303 adapter terminal=Ref,u8 branch(1 returned,2 Error,3 allocation,4 "
         "capacity,5 unknown_exception,6 unresolved_scope),u8 stage,ErrorCode,i64 "
         "detail;"
         "1400 child begin=Ref request,bool merge_error,bool separate_error,bool "
         "capture_stdout,bool capture_stderr;"
         "1401 spawn/setup operation return=Ref child,u8 native_stage,int result,int "
         "errno,pid_t pid,pid_t group;"
         "1402 poll return=Ref,u8 channel,int fd,short events,int timeout,int "
         "result,short revents,int errno;"
         "1403 write return=Ref,size_t requested,ssize_t result,int errno(no input "
         "bytes);"
         "1404 read return=Ref,u8 channel,size_t requested,ssize_t result,int "
         "errno,[positive original selected-channel bytes];"
         "1405 waitid return=Ref,u8 method,int result,int errno,pid_t si_pid,uid_t "
         "si_uid,int si_code,int si_status;"
         "1406 close return=Ref,u8 channel,int fd,int result,int errno;"
         "1407 group signal return=Ref,pid_t group,int signal,int result,int errno;"
         "1408 final reap operation return=Ref,pid_t pid,pid_t result,int errno,bool "
         "status_valid,int status(no intermediate EINTR retry site);"
         "1409 child released=Ref,u8 branch(1 owner_cleanup);"
         "1410 operation refused=Ref,u8 operation,u8 stage,ErrorCode,i64 detail;"
         "Ref=lane_epoch16,u64 ordinal;channel=0 stdin,1 stdout,2 stderr,3 temporary "
         "endpoint;"
         "errno copied unchanged,meaningful on native failure;no "
         "credentials/config/argv/environment capture;";
}
} // namespace blackbird
