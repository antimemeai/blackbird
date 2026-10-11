#include "blackbird/raw_sampler.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <libproc.h>
#include <mach/mach_time.h>
#include <sys/resource.h>
#else
#include <linux/magic.h>
#include <sys/vfs.h>
#endif

namespace blackbird {
namespace {
static_assert(sizeof(int) == 4 && sizeof(pid_t) == 4 && sizeof(clockid_t) == 4);
static_assert(sizeof(std::size_t) == 8 && sizeof(ssize_t) == 8 && sizeof(long) == 8 &&
              sizeof(time_t) == 8);
constexpr std::uint64_t envelope = raw_envelope_size, reference_bytes = 24;
constexpr std::uint64_t end_bytes = envelope + reference_bytes + 4 + 1 + 8;
constexpr std::uint64_t refusal_bytes = envelope + reference_bytes + 1 + 8;
constexpr std::uint64_t clock_pair_bytes = 108;
#if defined(__linux__)
constexpr std::uint64_t close_bytes = 92;
constexpr std::size_t read_bound = 4096;
#endif
struct Ref {
  IdentityBytes lane{};
  std::uint64_t ordinal = 0;
};
struct OwnedFd {
  int value = -1;
  Ref opened;
};
template <class T> ByteView native(const T &value) noexcept {
  return std::as_bytes(std::span{&value, std::size_t{1}});
}
void require(Result<void> answer) {
  if (!answer.has_value())
    throw answer.error();
}
RawControlToken claim(std::uint64_t bytes, const RawWaitControl &control) {
  auto answer = raw_claim_control(bytes, control);
  if (!answer.has_value())
    throw answer.error();
  return std::move(answer).value();
}
Ref emit(std::uint32_t site, std::span<const ByteView> fields,
         RawControlToken *credit = nullptr) {
  const auto answer = raw_emit(
      site, fields, credit ? RawPartition::control : RawPartition::data, credit);
  if (!answer.has_value())
    throw answer.error();
  return {raw_lane_epoch(), answer.value()};
}
void refusal(Ref owner, Error error, RawControlToken &credit) {
  const std::array fields{ByteView{owner.lane}, native(owner.ordinal),
                          native(error.code), native(error.detail)};
  (void)emit(7006, fields, &credit);
}
void end(std::uint32_t site, Ref owner, std::optional<Error> error,
         RawControlToken &credit) {
  const std::uint32_t outcome = error.has_value() ? 1U : 0U;
  if (error.has_value()) {
    const std::array fields{ByteView{owner.lane}, native(owner.ordinal),
                            native(outcome), native(error->code),
                            native(error->detail)};
    (void)emit(site, fields, &credit);
  } else {
    const std::array fields{ByteView{owner.lane}, native(owner.ordinal),
                            native(outcome)};
    (void)emit(site, fields, &credit);
  }
}
Result<void> close_fd(OwnedFd &fd, RawControlToken &credit) noexcept {
  if (fd.value < 0)
    return Result<void>::success();
  Ref requested;
  try {
    const std::array fields{ByteView{fd.opened.lane}, native(fd.opened.ordinal),
                            native(fd.value)};
    requested = emit(7023, fields, &credit);
  } catch (const Error &error) {
    const int descriptor = std::exchange(fd.value, -1);
    (void)::close(descriptor);
    raw_fence(error);
    return Result<void>::failure(error);
  }
  const int descriptor = std::exchange(fd.value, -1);
  const int result = ::close(descriptor), error = errno;
  try {
    if (result < 0) {
      const std::array fields{ByteView{requested.lane}, native(requested.ordinal),
                              native(result), native(error)};
      (void)emit(7024, fields, &credit);
    } else {
      const std::array fields{ByteView{requested.lane}, native(requested.ordinal),
                              native(result)};
      (void)emit(7024, fields, &credit);
    }
  } catch (const Error &caught) {
    raw_fence(caught);
    return Result<void>::failure(caught);
  }
  return result < 0 ? Result<void>::failure({ErrorCode::io, error})
                    : Result<void>::success();
}
#if defined(__linux__)
void open_return(std::uint32_t site, Ref request, int result, int error,
                 RawControlToken &credit) {
  if (result < 0) {
    const std::array fields{ByteView{request.lane}, native(request.ordinal),
                            native(result), native(error)};
    (void)emit(site, fields, &credit);
    throw Error{ErrorCode::io, error};
  }
  const std::array fields{ByteView{request.lane}, native(request.ordinal),
                          native(result)};
  (void)emit(site, fields, &credit);
}
#endif
void acquire_clock(Ref owner, clockid_t method, std::uint32_t role,
                   RawControlToken &credit) {
  const std::array request_fields{ByteView{owner.lane}, native(owner.ordinal),
                                  native(method), native(role)};
  const auto requested = emit(7009, request_fields, &credit);
  timespec observed{};
  const int result = ::clock_gettime(method, &observed), error = errno;
  if (result < 0) {
    const std::array fields{ByteView{requested.lane}, native(requested.ordinal),
                            native(result), native(error)};
    (void)emit(7010, fields, &credit);
  } else {
    const std::array fields{ByteView{requested.lane}, native(requested.ordinal),
                            native(result), native(observed.tv_sec),
                            native(observed.tv_nsec)};
    (void)emit(7010, fields, &credit);
  }
}
Ref read_return(std::uint32_t site, Ref request, ssize_t result, int error,
                ByteView bytes, RawControlToken &credit, bool data) {
  if (result < 0) {
    const std::array fields{ByteView{request.lane}, native(request.ordinal),
                            native(result), native(error)};
    return emit(site, fields, &credit);
  } else {
    const std::array fields{ByteView{request.lane}, native(request.ordinal),
                            native(result), bytes};
    return emit(site, fields, result > 0 && data ? nullptr : &credit);
  }
}
void wait_pipe(int fd, const RawWaitControl &control) {
  std::array<pollfd, 3> watches{{{fd, POLLIN | POLLHUP, 0},
                                 {control.cancellation_fds[0], POLLIN | POLLHUP, 0},
                                 {control.cancellation_fds[1], POLLIN | POLLHUP, 0}}};
  for (;;) {
    int timeout = -1;
    if (control.deadline.has_value()) {
      const auto remaining = *control.deadline - std::chrono::steady_clock::now();
      if (remaining <= std::chrono::steady_clock::duration::zero())
        throw Error{ErrorCode::interrupted, ETIMEDOUT};
      const auto milliseconds =
          std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
      timeout = milliseconds > INT_MAX ? INT_MAX : static_cast<int>(milliseconds);
    }
    const int result = ::poll(watches.data(), watches.size(), timeout), error = errno;
    if (result < 0 && error == EINTR)
      continue;
    if (result < 0)
      throw Error{ErrorCode::io, error};
    if (result == 0)
      continue;
    if (watches[1].revents != 0 || watches[2].revents != 0)
      throw Error{ErrorCode::interrupted, ECANCELED};
    if ((watches[0].revents & POLLNVAL) != 0)
      throw Error{ErrorCode::io, EBADF};
    if (watches[0].revents != 0)
      return;
  }
}
RawSamplerRequest decode(ByteView wire) {
  if (wire.size() != raw_sampler_request_bytes ||
      std::memcmp(wire.data(), "BBRSSCTL", 8) != 0)
    throw Error{ErrorCode::corrupt};
  std::uint32_t version = 0, method = 0, reserved = 0;
  RawSamplerRequest request{};
  std::memcpy(&version, wire.data() + 8, 4);
  std::memcpy(&method, wire.data() + 12, 4);
  std::memcpy(&request.controller, wire.data() + 16, 8);
  std::memcpy(&request.pid, wire.data() + 24, 4);
  std::memcpy(&reserved, wire.data() + 28, 4);
  if (version != 1 || reserved != 0)
    throw Error{ErrorCode::unsupported};
  request.method = method;
  return request;
}
#if defined(__APPLE__)
constexpr std::uint32_t supported_methods = 1;
constexpr auto supported_method =
    static_cast<std::uint32_t>(RawSamplerMethod::mac_rusage);
constexpr std::uint64_t setup_credit = 214;
// 102 terminal + 49 completed control-read remainder + 49 validation + 470 acquisition.
constexpr std::uint64_t minimum_control = 670, minimum_data = 140;
#else
constexpr std::uint32_t supported_methods = 2;
constexpr auto supported_method =
    static_cast<std::uint32_t>(RawSamplerMethod::linux_stat);
constexpr std::uint64_t setup_credit = 706;
// 206 setup remainder + 49 completed control-read remainder + 294 opened-command
// remainder + 316 next read burst. Other branches require less simultaneous credit.
constexpr std::uint64_t minimum_control = 865, minimum_data = 4144;
static_assert(sizeof(struct statfs) == 120);
#endif
} // namespace

std::array<std::byte, raw_sampler_request_bytes>
encode_raw_sampler_request(const RawSamplerRequest &request) noexcept {
  std::array<std::byte, raw_sampler_request_bytes> wire{};
  std::memcpy(wire.data(), "BBRSSCTL", 8);
  const std::uint32_t version = 1, method = static_cast<std::uint32_t>(request.method);
  std::memcpy(wire.data() + 8, &version, 4);
  std::memcpy(wire.data() + 12, &method, 4);
  std::memcpy(wire.data() + 16, &request.controller, 8);
  std::memcpy(wire.data() + 24, &request.pid, 4);
  return wire;
}

class RawResourceSampler::State {
public:
  explicit State(RawSamplerOptions options)
      : control_(options.control), clock_(options.clock) {}
  ~State() { (void)finish(); }
  void initialize(std::string_view proc_path) {
    if (!raw_enabled())
      throw Error{ErrorCode::audit_unavailable};
    // Materialize setup path before any native setup fact. No later allocation.
#if defined(__linux__)
    if (proc_path.find('\0') != std::string_view::npos)
      throw Error{ErrorCode::invalid_range};
    const std::string path{proc_path};
#else
    (void)proc_path;
#endif
    require(raw_wait_data(minimum_data, control_));
    // Qualify the largest simultaneous held budget before any setup fact. A
    // physically smaller control lane must fail rather than wait on itself.
    {
      auto capacity = claim(minimum_control, control_);
    }
    terminal_.emplace(claim(setup_credit, control_));
    const std::array fields{native(supported_methods)};
    session_ = emit(7000, fields, &*terminal_);
#if defined(__APPLE__)
    const std::array request_fields{ByteView{session_.lane}, native(session_.ordinal)};
    const auto requested = emit(7025, request_fields, &*terminal_);
    mach_timebase_info_data_t observed{};
    const auto result = ::mach_timebase_info(&observed);
    if (result == KERN_SUCCESS) {
      const std::array returned{ByteView{requested.lane}, native(requested.ordinal),
                                native(result), native(observed.numer),
                                native(observed.denom)};
      (void)emit(7026, returned, &*terminal_);
    } else {
      const std::array returned{ByteView{requested.lane}, native(requested.ordinal),
                                native(result)};
      (void)emit(7026, returned, &*terminal_);
      throw Error{ErrorCode::io, result};
    }
#else
    const int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    const std::array root_fields{ByteView{session_.lane}, native(session_.ordinal),
                                 native(flags)};
    root_.opened = emit(7011, root_fields, &*terminal_);
    root_.value = ::open(path.c_str(), flags);
    const int root_error = errno;
    open_return(7012, root_.opened, root_.value, root_error, *terminal_);
    const std::array fs_fields{ByteView{root_.opened.lane},
                               native(root_.opened.ordinal), native(root_.value)};
    const auto requested = emit(7013, fs_fields, &*terminal_);
    struct statfs observed{};
    const int result = ::fstatfs(root_.value, &observed), error = errno;
    if (result < 0) {
      const std::array returned{ByteView{requested.lane}, native(requested.ordinal),
                                native(result), native(error)};
      (void)emit(7014, returned, &*terminal_);
      throw Error{ErrorCode::io, error};
    }
    const std::array returned{ByteView{requested.lane},
                              native(requested.ordinal),
                              native(result),
                              native(observed.f_type),
                              native(observed.f_bsize),
                              native(observed.f_blocks),
                              native(observed.f_bfree),
                              native(observed.f_bavail),
                              native(observed.f_files),
                              native(observed.f_ffree),
                              native(observed.f_fsid.__val[0]),
                              native(observed.f_fsid.__val[1]),
                              native(observed.f_namelen),
                              native(observed.f_frsize),
                              native(observed.f_flags),
                              native(observed.f_spare[0]),
                              native(observed.f_spare[1]),
                              native(observed.f_spare[2]),
                              native(observed.f_spare[3])};
    (void)emit(7014, returned, &*terminal_);
    if (observed.f_type != PROC_SUPER_MAGIC)
      throw Error{ErrorCode::wrong_environment, observed.f_type};
    constant(_SC_CLK_TCK);
    constant(_SC_PAGESIZE);
#endif
  }
  Result<void> acquire(const RawSamplerRequest &request, Ref parent, int held_pid = -1,
                       bool direct = false) {
    if (closed_)
      return Result<void>::failure({ErrorCode::conflict});
    if (admission_closed_)
      return Result<void>::failure(
          failure_.value_or(Error{ErrorCode::audit_unavailable}));
    std::optional<RawControlToken> validation;
    try {
      validation.emplace(claim(refusal_bytes + (direct ? 56 : 0), control_));
      if (direct) {
        const auto method = static_cast<std::uint32_t>(request.method);
        const std::array fields{ByteView{parent.lane}, native(parent.ordinal),
                                native(request.controller), native(request.pid),
                                native(method)};
        parent = emit(7027, fields, &*validation);
      }
      if (request.method != supported_method || request.pid <= 0) {
        const Error error{request.method != supported_method
                              ? ErrorCode::unsupported
                              : ErrorCode::invalid_range};
        refusal(parent, error, *validation);
        return Result<void>::failure(error);
      }
      // Retain rejection credit until BEGIN, including canceled resource admission.
    } catch (const Error &error) {
      owner_failed(error);
      return Result<void>::failure(error);
    }
    std::optional<RawControlToken> credit;
    Ref owner;
    OwnedFd pid, file;
    std::optional<Error> error;
    try {
#if defined(__APPLE__)
      (void)held_pid;
      credit.emplace(claim(
          56 + end_bytes + refusal_bytes + 48 + 48 + 2 * clock_pair_bytes, control_));
      require(raw_wait_data(140, control_));
#else
      credit.emplace(
          claim(56 + (held_pid < 0 ? 2 * 96 + 2 * close_bytes : 96 + close_bytes) +
                    end_bytes + refusal_bytes,
                control_));
#endif
      const auto native_method = static_cast<std::uint32_t>(request.method);
      const std::array begin_fields{ByteView{parent.lane}, native(parent.ordinal),
                                    native(request.controller), native(request.pid),
                                    native(native_method)};
      owner = emit(7004, begin_fields, &*credit);
      validation.reset();
#if defined(__APPLE__)
      acquire_clock(owner, clock_, 0, *credit);
      const int flavor = RUSAGE_INFO_V0;
      const std::array request_fields{ByteView{owner.lane}, native(owner.ordinal),
                                      native(request.pid), native(flavor)};
      const auto requested = emit(7021, request_fields, &*credit);
      rusage_info_v0 observed{};
      static_assert(sizeof(observed) == 96);
      const int result = ::proc_pid_rusage(
          request.pid, flavor, reinterpret_cast<rusage_info_t *>(&observed));
      const int native_error = errno;
      if (result < 0) {
        const std::array returned{ByteView{requested.lane}, native(requested.ordinal),
                                  native(result), native(native_error)};
        (void)emit(7022, returned, &*credit);
        error = Error{ErrorCode::io, native_error};
      } else {
        const std::array returned{ByteView{requested.lane},
                                  native(requested.ordinal),
                                  native(result),
                                  ByteView{std::as_bytes(std::span{observed.ri_uuid})},
                                  native(observed.ri_user_time),
                                  native(observed.ri_system_time),
                                  native(observed.ri_pkg_idle_wkups),
                                  native(observed.ri_interrupt_wkups),
                                  native(observed.ri_pageins),
                                  native(observed.ri_wired_size),
                                  native(observed.ri_resident_size),
                                  native(observed.ri_phys_footprint),
                                  native(observed.ri_proc_start_abstime),
                                  native(observed.ri_proc_exit_abstime)};
        (void)emit(7022, returned);
      }
      acquire_clock(owner, clock_, 1, *credit);
#else
      int directory = held_pid;
      if (directory < 0) {
        std::array<char, 32> name{};
        const auto formatted =
            std::to_chars(name.data(), name.data() + name.size() - 1, request.pid);
        if (formatted.ec != std::errc{})
          throw Error{ErrorCode::overflow};
        const int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
        const std::array requested{ByteView{owner.lane}, native(owner.ordinal),
                                   native(request.pid), native(flags)};
        pid.opened = emit(7015, requested, &*credit);
        pid.value = ::openat(root_.value, name.data(), flags);
        const int native_error = errno;
        open_return(7016, pid.opened, pid.value, native_error, *credit);
        directory = pid.value;
      }
      const int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC;
      const std::uint32_t file_method = 1;
      const auto attachment = held_pid < 0 ? pid.opened : owner;
      const std::array requested{ByteView{attachment.lane}, native(attachment.ordinal),
                                 native(file_method), native(flags)};
      file.opened = emit(7017, requested, &*credit);
      file.value = ::openat(directory, "stat", flags);
      const int native_error = errno;
      open_return(7018, file.opened, file.value, native_error, *credit);
      for (;;) {
        auto read_credit = claim(48 + 52 + 2 * clock_pair_bytes, control_);
        require(raw_wait_data(envelope + reference_bytes + sizeof(ssize_t) + read_bound,
                              control_));
        acquire_clock(owner, clock_, 0, read_credit);
        const std::array read_fields{ByteView{file.opened.lane},
                                     native(file.opened.ordinal), native(read_bound)};
        const auto read_request = emit(7019, read_fields, &read_credit);
        std::array<std::byte, read_bound> observed{};
        const auto result = ::read(file.value, observed.data(), observed.size());
        const int read_error = errno;
        const auto bytes = ByteView{observed}.first(
            result > 0 ? static_cast<std::size_t>(result) : std::size_t{0});
        (void)read_return(7020, read_request, result, read_error, bytes, read_credit,
                          true);
        acquire_clock(owner, clock_, 1, read_credit);
        if (result == 0)
          break;
        if (result < 0 && read_error != EINTR)
          throw Error{ErrorCode::io, read_error};
      }
#endif
    } catch (const Error &caught) {
      error = caught;
    }
    try {
      if (credit.has_value()) {
        const auto file_closed = close_fd(file, *credit);
        const auto pid_closed = close_fd(pid, *credit);
        if (!file_closed.has_value())
          error = file_closed.error();
        if (!pid_closed.has_value())
          error = pid_closed.error();
        if (owner.ordinal != 0) {
          if (error.has_value())
            refusal(owner, *error, *credit);
          end(7005, owner, error, *credit);
        }
      }
    } catch (const Error &caught) {
      error = caught;
    }
    if (error.has_value()) {
      if (owner.ordinal == 0 && validation.has_value()) {
        try {
          refusal(parent, *error, *validation);
        } catch (const Error &caught) {
          raw_fence(caught);
        }
      }
      if (owner.ordinal == 0 || error->code == ErrorCode::interrupted ||
          error->code == ErrorCode::audit_unavailable)
        owner_failed(*error);
      return Result<void>::failure(*error);
    }
    return Result<void>::success();
  }
  Result<void> run(int fd) {
    if (closed_)
      return Result<void>::failure({ErrorCode::conflict});
    Ref last = session_;
    try {
      struct stat mode{};
      if (::fstat(fd, &mode) != 0)
        throw Error{ErrorCode::io, errno};
      if (!S_ISFIFO(mode.st_mode))
        throw Error{ErrorCode::invalid_range};
      std::array<std::byte, raw_sampler_request_bytes> frame{};
      std::size_t received = 0;
      for (;;) {
        auto credit = claim(108 + refusal_bytes, control_);
        wait_pipe(fd, control_);
        const auto requested_size = frame.size() - received;
        const std::array fields{native(fd), native(requested_size)};
        const auto requested = emit(7002, fields, &credit);
        std::array<std::byte, raw_sampler_request_bytes> observed{};
        const auto result = ::read(fd, observed.data(), requested_size);
        const int error = errno;
        const auto bytes = ByteView{observed}.first(
            result > 0 ? static_cast<std::size_t>(result) : std::size_t{0});
        last = read_return(7003, requested, result, error, bytes, credit, false);
        if (result < 0) {
          if (error == EINTR)
            continue;
          throw Error{ErrorCode::io, error};
        }
        if (result == 0) {
          if (received != 0) {
            const Error partial{ErrorCode::incomplete,
                                static_cast<std::int64_t>(received)};
            refusal(last, partial, credit);
            failure_ = partial;
          }
          break;
        }
        std::copy(bytes.begin(), bytes.end(),
                  frame.begin() + static_cast<std::ptrdiff_t>(received));
        received += bytes.size();
        if (received != frame.size())
          continue;
        RawSamplerRequest request{};
        try {
          request = decode(frame);
          if (request.method != supported_method || request.pid <= 0)
            throw Error{request.method != supported_method ? ErrorCode::unsupported
                                                           : ErrorCode::invalid_range};
        } catch (const Error &invalid) {
          refusal(last, invalid, credit);
          failure_ = invalid;
          break;
        }
        const auto acquired = acquire(request, last);
        received = 0;
        if (!acquired.has_value() && admission_closed_)
          break;
      }
    } catch (const Error &error) {
      owner_failed(error);
    }
    const auto ended = finish();
    if (!ended.has_value())
      return ended;
    if (failure_.has_value())
      return Result<void>::failure(*failure_);
    return Result<void>::success();
  }
  Result<void> finish() noexcept {
    if (closed_)
      return Result<void>::success();
    closed_ = true;
    if (!terminal_.has_value())
      return Result<void>::success();
    try {
      const auto closed = close_fd(root_, *terminal_);
      if (!closed.has_value())
        failure_ = closed.error();
      if (session_.ordinal != 0)
        end(7001, session_, failure_, *terminal_);
      terminal_.reset();
      return closed;
    } catch (const Error &error) {
      if (root_.value >= 0) {
        (void)::close(root_.value);
        root_.value = -1;
      }
      raw_fence(error);
      terminal_.reset();
      return Result<void>::failure(error);
    }
  }
  void owner_failed(Error error) noexcept {
    failure_ = error;
    if (error.code == ErrorCode::interrupted ||
        error.code == ErrorCode::audit_unavailable)
      admission_closed_ = true;
    if (terminal_.has_value() && session_.ordinal != 0 && !refused_) {
      refused_ = true;
      try {
        refusal(session_, error, *terminal_);
      } catch (const Error &caught) {
        raw_fence(caught);
      }
    }
  }
  Ref session() const noexcept { return session_; }

private:
#if defined(__linux__)
  void constant(int selector) {
    const std::array fields{ByteView{session_.lane}, native(session_.ordinal),
                            native(selector)};
    const auto requested = emit(7007, fields, &*terminal_);
    errno = 0;
    const long result = ::sysconf(selector);
    const int error = errno;
    if (result == -1) {
      const std::array returned{ByteView{requested.lane}, native(requested.ordinal),
                                native(result), native(error)};
      (void)emit(7008, returned, &*terminal_);
    } else {
      const std::array returned{ByteView{requested.lane}, native(requested.ordinal),
                                native(result)};
      (void)emit(7008, returned, &*terminal_);
    }
  }
#endif
  RawWaitControl control_;
  clockid_t clock_;
  Ref session_;
  OwnedFd root_;
  std::optional<RawControlToken> terminal_;
  std::optional<Error> failure_;
  bool closed_ = false, refused_ = false, admission_closed_ = false;
};

RawResourceSampler::RawResourceSampler(std::unique_ptr<State> state)
    : state_(std::move(state)) {}
RawResourceSampler::~RawResourceSampler() = default;
Result<std::unique_ptr<RawResourceSampler>>
RawResourceSampler::open(RawSamplerOptions options) {
  try {
    auto sampler = std::unique_ptr<RawResourceSampler>{
        new RawResourceSampler{std::make_unique<State>(options)}};
    try {
      sampler->state_->initialize(options.proc_root);
    } catch (const Error &error) {
      sampler->state_->owner_failed(error);
      (void)sampler->state_->finish();
      return Result<std::unique_ptr<RawResourceSampler>>::failure(error);
    }
    return Result<std::unique_ptr<RawResourceSampler>>::success(std::move(sampler));
  } catch (const std::bad_alloc &) {
    return Result<std::unique_ptr<RawResourceSampler>>::failure(
        {ErrorCode::allocation});
  }
}
Result<void> RawResourceSampler::acquire(const RawSamplerRequest &request) {
  return state_->acquire(request, state_->session(), -1, true);
}
Result<void> RawResourceSampler::run(int control_fd) { return state_->run(control_fd); }
#if defined(__linux__)
Result<void> RawResourceSampler::acquire_directory(const RawSamplerRequest &request,
                                                   int pid_directory) {
  if (pid_directory < 0)
    return Result<void>::failure({ErrorCode::invalid_range});
  return state_->acquire(request, state_->session(), pid_directory, true);
}
#endif
Result<void> RawResourceSampler::finish() { return state_->finish(); }
std::string_view raw_sampler_description() noexcept {
  return "sampler7000-7031 v1; host-native little-endian; ref=lane16,u64 ordinal; "
         "7000 u32 methods;7001/7005 ref,u32 outcome(0 complete/1 failed),failure-only "
         "u8 ErrorCode,i64 detail;7002 int fd,size_t max;7003/7020 ref,ssize_t return,"
         "positive-only exact returned bytes,negative-only int errno;7004 ref,u64 "
         "controller,"
         "pid_t,u32 method(1 mac rusage0/2 linux stat);7006 ref,u8 ErrorCode,i64 "
         "detail;"
         "7007 ref,int sysconf selector;7008 ref,long result,-1-only int errno;"
         "7009 ref,clockid_t,u32 role(0 before/1 after);7010 ref,int return,"
         "success-only time_t sec,long nsec,failure-only int errno;"
         "7011 ref,int flags;7012/7016/7018 ref,int fd,negative-only int errno;"
         "7013 ref,int fd;7014 ref,int return,success-only all native Linux statfs "
         "fields "
         "type,bsize,blocks,bfree,bavail,files,ffree,fsid "
         "int[2],namelen,frsize,flags,spare long[4],"
         "failure-only int errno;7015 ref,pid_t,int flags;7017 ref,u32 file(1 "
         "stat),int flags;"
         "7019 ref,size_t max4096;7021 ref,pid_t,int flavor;7022 ref,int "
         "return,success-only "
         "uuid16,u64 "
         "user_time,system_time,pkg_idle_wkups,interrupt_wkups,pageins,wired_size,"
         "resident_size,phys_footprint,proc_start_abstime,proc_exit_abstime;negative-"
         "only int errno;"
         "7023 ref,int fd;7024 ref,int return,negative-only int errno;7025 ref;"
         "7026 ref,kern_return_t,success-only u32 numer,u32 denom;7027 direct.request "
         "ref,u64 controller,pid_t,u32 method;7028-7031 reserved;"
         "control32=BBRSSCTL8,u32 version1,u32 method,u64 controller,pid_t i32,u32 "
         "zero;"
         "int,pid_t,clockid_t,kern_return_t,u32=4;size_t,ssize_t,long,time_t,u64=8;"
         "mac CPU/start/exit values are raw Mach absolute units,UUID executable "
         "identity;"
         "Linux stat bytes unparsed;sysconf tick/page constants unconverted;"
         "procfs checked via held root fstatfs,PID directory held for one explicit "
         "command;"
         "closes link original open request;commands by PID observe current "
         "incarnation";
}
} // namespace blackbird
