#include "blackbird/raw_command.hpp"
#include "blackbird/context.hpp"
#include "blackbird/process_lifetime.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
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
namespace {
using detail::raw_native_field;
struct WakePair {
  int fds[2]{-1, -1};
  RawEventRef incarnation;
};
struct WakeReturn {
  int result;
  int error;
};
RawEventRef wake_ref(const unsigned char *lane, std::uint64_t ordinal) noexcept {
  RawEventRef ref{};
  ::memcpy(ref.lane.data(), lane, sizeof(ref.lane));
  ref.ordinal = ordinal;
  return ref;
}
WakeReturn wake_close(int fd, RawEventRef incarnation,
                      RawControlToken *credit) noexcept {
  const auto request =
      raw_command_linked(2054, incarnation, std::array{raw_native_field(fd)}, credit);
  const int result = ::close(fd);
  const int error = errno;
  (void)raw_command_linked(
      2055, request, std::array{raw_native_field(result), raw_native_field(error)},
      credit);
  return {result, error};
}
int wake_fcntl(int fd, int command, int argument, RawEventRef incarnation,
               RawControlToken *credit) {
  const auto request =
      raw_command_linked(2052, incarnation,
                         std::array{raw_native_field(fd), raw_native_field(command),
                                    raw_native_field(argument)},
                         credit);
  const int result = ::fcntl(fd, command, argument);
  const int error = errno;
  (void)raw_command_linked(
      2053, request, std::array{raw_native_field(result), raw_native_field(error)},
      credit);
  if (result < 0)
    throw Error{ErrorCode::io, error};
  return result;
}
WakePair wake_pair(RawEventRef parent, RawControlToken *credit) {
  WakePair pair;
  constexpr int domain = AF_UNIX, type = SOCK_STREAM, protocol = 0;
  pair.incarnation =
      raw_command_linked(2050, parent,
                         std::array{raw_native_field(domain), raw_native_field(type),
                                    raw_native_field(protocol)},
                         credit);
  try {
    {
      const std::lock_guard lock{detail::process_spawn_mutex};
      const int result = ::socketpair(domain, type, protocol, pair.fds);
      const int error = errno;
      (void)raw_command_linked(2051, pair.incarnation,
                               std::array{raw_native_field(result),
                                          raw_native_field(error),
                                          raw_native_field(pair.fds)},
                               credit);
      if (result < 0)
        throw Error{ErrorCode::io, error};
      for (int &fd : pair.fds) {
        const int original = fd;
        fd = wake_fcntl(original, F_DUPFD_CLOEXEC, 4, pair.incarnation, credit);
        const auto closed = wake_close(original, pair.incarnation, credit);
        if (closed.result < 0)
          throw Error{ErrorCode::io, closed.error};
      }
    }
    for (const int fd : pair.fds) {
      const int flags = wake_fcntl(fd, F_GETFL, 0, pair.incarnation, credit);
      (void)wake_fcntl(fd, F_SETFL, flags | O_NONBLOCK, pair.incarnation, credit);
    }
    return pair;
  } catch (...) {
    for (int &fd : pair.fds) {
      if (fd >= 0) {
        const int owned = fd;
        fd = -1;
        (void)wake_close(owned, pair.incarnation, credit);
      }
    }
    throw;
  }
}
void wake_terminal(RawEventRef request, std::uint8_t branch, Error error,
                   RawControlToken *credit) noexcept {
  (void)raw_command_linked(2061, request,
                           std::array{raw_native_field(branch),
                                      raw_native_field(error.code),
                                      raw_native_field(error.detail)},
                           credit);
}
} // namespace
RawCommandWake::RawCommandWake(Mode mode) : mode_(mode) {
  static_assert(std::atomic<State>::is_always_lock_free);
  static_assert(std::atomic_bool::is_always_lock_free);
  shutdown_ = &::shutdown;
  exit_ = &::_exit;
  owner_ = mode == Mode::signal ? unwrap(raw_prepare_thread()) : raw_current_thread();
  RawThreadBinding owner{owner_};
  constexpr auto final_bytes = raw_wake_lifecycle_credit + 184;
  std::optional<RawControlToken> bank;
  if (raw_enabled())
    bank.emplace(unwrap(
        raw_claim_control(raw_wake_setup_credit + final_bytes +
                          (mode == Mode::one_shot ? 0 : raw_wake_notify_credit))));
  if (bank) {
    final_credit_.emplace(unwrap(bank->split(final_bytes)));
    if (mode != Mode::one_shot)
      notify_credit_.emplace(unwrap(bank->split(raw_wake_notify_credit)));
  }
  const auto pair = wake_pair({}, bank ? &*bank : nullptr);
  endpoints_[0] = pair.fds[0];
  endpoints_[1] = pair.fds[1];
  ::memcpy(incarnation_lane_, pair.incarnation.lane.data(), sizeof(incarnation_lane_));
  incarnation_ordinal_ = pair.incarnation.ordinal;
  const auto lane = raw_lane_epoch();
  ::memcpy(publisher_lane_, lane.data(), sizeof(publisher_lane_));
  if (notify_credit_)
    try {
      publisher_ = unwrap(RawSignalPublisher::prepare(owner_, *notify_credit_));
    } catch (...) {
      for (int &fd : endpoints_) {
        const int owned = fd;
        fd = -1;
        (void)wake_close(owned, pair.incarnation, bank ? &*bank : nullptr);
      }
      throw;
    }
  state_.store(State::armed, std::memory_order_release);
}
RawEventRef RawCommandWake::reference() const noexcept {
  return wake_ref(incarnation_lane_, incarnation_ordinal_);
}
RawCommandWake::~RawCommandWake() {
  const ErrnoGuard guard;
  // Every containing owner joins before this destructor; shutdown failure with
  // potential borrowers already ended the process instead of entering a join.
  retire();
  state_.store(State::closed, std::memory_order_release);
  RawThreadBinding owner{owner_};
  const auto incarnation = wake_ref(incarnation_lane_, incarnation_ordinal_);
  const std::uint8_t operation = 2;
  const auto request = raw_command_linked(
      2060, incarnation,
      std::array{raw_native_field(operation), raw_native_field(endpoints_)},
      final_credit_ ? &*final_credit_ : nullptr);
  Error failure{};
  bool failed = false;
  for (int &fd : endpoints_) {
    if (fd >= 0) {
      const int owned = fd;
      fd = -1;
      const auto closed =
          wake_close(owned, incarnation, final_credit_ ? &*final_credit_ : nullptr);
      if (closed.result < 0) {
        failure = {ErrorCode::io, closed.error};
        failed = true;
      }
    }
  }
  wake_terminal(request, failed ? 2 : 1, failure,
                final_credit_ ? &*final_credit_ : nullptr);
}
void RawCommandWake::notify(RawControlToken *credit,
                            std::atomic_bool *sticky) noexcept {
  const ErrnoGuard guard;
  State expected = State::armed;
  if (!state_.compare_exchange_strong(expected, State::notifying,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    return;
  if (sticky != nullptr)
    sticky->store(true, std::memory_order_relaxed);
  RawSignalPublisher selected = publisher_;
  unsigned char lane[16]{};
  ::memcpy(lane, publisher_lane_, sizeof(lane));
  if (mode_ == Mode::one_shot && raw_enabled()) {
    if (credit == nullptr)
      exit_(70);
    auto prepared = RawSignalPublisher::prepare(raw_current_thread(), *credit);
    if (!prepared.has_value())
      exit_(70);
    selected = prepared.value();
    const auto current = raw_lane_epoch();
    ::memcpy(lane, current.data(), sizeof(lane));
  }
  const int fd = endpoints_[1], how = SHUT_WR;
  const RawNativePart request_parts[]{
      {incarnation_lane_, sizeof(incarnation_lane_)},
      {&incarnation_ordinal_, sizeof(incarnation_ordinal_)},
      {&fd, sizeof(fd)},
      {&how, sizeof(how)}};
  const auto request = selected.emit(2056, request_parts, 4);
  if (!request.published)
    exit_(70);
  const int result = shutdown_(fd, how);
  const int error = errno;
  const RawNativePart return_parts[]{{lane, sizeof(lane)},
                                     {&request.ordinal, sizeof(request.ordinal)},
                                     {&result, sizeof(result)},
                                     {&error, sizeof(error)}};
  const auto returned = selected.emit(2057, return_parts, 4);
  if (!returned.published)
    exit_(70);
  if (result < 0) {
    failure_ = {ErrorCode::io, error};
    state_.store(State::failed, std::memory_order_release);
    if (borrowed_.load(std::memory_order_acquire))
      exit_(70);
    return;
  }
  if (mode_ == Mode::one_shot) {
    const RawNativePart close_parts[]{
        {incarnation_lane_, sizeof(incarnation_lane_)},
        {&incarnation_ordinal_, sizeof(incarnation_ordinal_)},
        {&fd, sizeof(fd)}};
    const auto closing = selected.emit(2054, close_parts, 3);
    if (!closing.published)
      exit_(70);
    endpoints_[1] = -1;
    const int closed = ::close(fd);
    const int close_error = errno;
    const RawNativePart closed_parts[]{{lane, sizeof(lane)},
                                       {&closing.ordinal, sizeof(closing.ordinal)},
                                       {&closed, sizeof(closed)},
                                       {&close_error, sizeof(close_error)}};
    if (!selected.emit(2055, closed_parts, 4).published)
      exit_(70);
    if (closed < 0)
      failure_ = {ErrorCode::io, close_error};
  }
  state_.store(State::notified, std::memory_order_release);
}
Result<void> RawCommandWake::clear(RawControlToken *preheld) noexcept {
  if (preheld && (mode_ != Mode::reusable || raw_current_thread() != owner_))
    return Result<void>::failure({ErrorCode::wrong_incarnation});
  return clear_paid(preheld, preheld);
}
Result<void> RawCommandWake::clear_paid(RawControlToken *lifecycle_preheld,
                                        RawControlToken *owner_preheld) noexcept {
  const ErrnoGuard guard;
  std::optional<RawControlToken> lifecycle;
  if (raw_enabled()) {
    auto admitted = lifecycle_preheld
                        ? lifecycle_preheld->split(raw_wake_lifecycle_credit)
                        : raw_try_claim_control(raw_wake_lifecycle_credit);
    if (!admitted.has_value())
      return Result<void>::failure(admitted.error());
    lifecycle.emplace(std::move(admitted).value());
  }
  const std::uint8_t operation = 1;
  auto previous = state_.load(std::memory_order_acquire);
  if (mode_ == Mode::one_shot ||
      (previous != State::armed && previous != State::notified) ||
      !state_.compare_exchange_strong(previous, State::rearming,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire)) {
    // A busy caller never reads another incarnation owner's mutable metadata.
    // Zero Ref and -1 endpoints explicitly mean the gate did not grant a view.
    const int unobserved[2]{-1, -1};
    const auto request = raw_command_linked(
        2060, {}, std::array{raw_native_field(operation), raw_native_field(unobserved)},
        lifecycle ? &*lifecycle : nullptr);
    const auto error = previous == State::failed ? failure_ : Error{ErrorCode::busy};
    wake_terminal(request, 2, error, lifecycle ? &*lifecycle : nullptr);
    return Result<void>::failure(error);
  }
  const auto old_incarnation = wake_ref(incarnation_lane_, incarnation_ordinal_);
  const auto request = raw_command_linked(
      2060, old_incarnation,
      std::array{raw_native_field(operation), raw_native_field(endpoints_)},
      lifecycle ? &*lifecycle : nullptr);
  Error failure{};
  bool effects = false;
  try {
    {
      RawThreadBinding owner{owner_};
      const bool replace = notify_credit_ && notify_credit_->remaining() == 0;
      std::optional<RawControlToken> bank;
      if (raw_enabled()) {
        const auto amount =
            raw_wake_rearm_credit + (replace ? raw_wake_notify_credit : 0);
        auto admitted = owner_preheld ? owner_preheld->split(amount)
                                      : raw_try_claim_control(amount);
        if (!admitted.has_value())
          throw admitted.error();
        bank.emplace(std::move(admitted).value());
      }
      effects = true;
      const auto pair = wake_pair(old_incarnation, bank ? &*bank : nullptr);
      try {
        const std::lock_guard lock{detail::process_spawn_mutex};
        for (std::size_t index = 0; index < 2; ++index) {
          const auto replacing =
              raw_command_linked(2058, pair.incarnation,
                                 std::array{raw_native_field(pair.fds[index]),
                                            raw_native_field(endpoints_[index])},
                                 bank ? &*bank : nullptr);
          const int result = ::dup2(pair.fds[index], endpoints_[index]);
          const int error = errno;
          (void)raw_command_linked(
              2059, replacing,
              std::array{raw_native_field(result), raw_native_field(error)},
              bank ? &*bank : nullptr);
          if (result < 0)
            throw Error{ErrorCode::io, error};
          (void)wake_fcntl(endpoints_[index], F_SETFD, FD_CLOEXEC, pair.incarnation,
                           bank ? &*bank : nullptr);
        }
      } catch (...) {
        for (const int fd : pair.fds)
          (void)wake_close(fd, pair.incarnation, bank ? &*bank : nullptr);
        throw;
      }
      for (const int fd : pair.fds) {
        const auto closed = wake_close(fd, pair.incarnation, bank ? &*bank : nullptr);
        if (closed.result < 0)
          failure = {ErrorCode::io, closed.error};
      }
      if (failure != Error{})
        throw failure;
      if (replace) {
        if (!bank)
          throw Error{ErrorCode::audit_unavailable};
        notify_credit_.emplace(unwrap(bank->split(raw_wake_notify_credit)));
      }
      ::memcpy(incarnation_lane_, pair.incarnation.lane.data(),
               sizeof(incarnation_lane_));
      incarnation_ordinal_ = pair.incarnation.ordinal;
      if (notify_credit_)
        publisher_ = unwrap(RawSignalPublisher::prepare(owner_, *notify_credit_));
    }
    state_.store(State::armed, std::memory_order_release);
    wake_terminal(request, 1, {}, lifecycle ? &*lifecycle : nullptr);
    return Result<void>::success();
  } catch (const Error &error) {
    failure = error;
  } catch (...) {
    failure = {ErrorCode::external_unknown};
  }
  if (effects) {
    failure_ = failure;
    state_.store(State::failed, std::memory_order_release);
  } else
    state_.store(previous, std::memory_order_release);
  wake_terminal(request, 2, failure, lifecycle ? &*lifecycle : nullptr);
  return Result<void>::failure(failure);
}
namespace {
std::atomic<RawCommandInterrupt *> interrupt_owner{nullptr};
static_assert(decltype(interrupt_owner)::is_always_lock_free);
ByteView wake_native_mask(const sigset_t &mask) noexcept {
  return std::as_bytes(std::span{&mask, std::size_t{1}})
      .first(RawCommandInterrupt::signal_mask_bytes);
}
auto wake_native_action(const struct sigaction &action) noexcept {
#if defined(__linux__)
  return std::array{raw_native_field(action.sa_handler),
                    raw_native_field(action.sa_flags), wake_native_mask(action.sa_mask),
                    raw_native_field(action.sa_restorer)};
#else
  return std::array{raw_native_field(action.sa_handler),
                    raw_native_field(action.sa_flags),
                    wake_native_mask(action.sa_mask)};
#endif
}
} // namespace
RawCommandSignalResult raw_command_action(int signal, const struct sigaction &action,
                                          struct sigaction &previous,
                                          RawEventRef incarnation,
                                          RawControlToken *credit) noexcept {
  const auto requested = wake_native_action(action);
  std::array<ByteView, requested.size() + 1> request_fields{};
  request_fields[0] = raw_native_field(signal);
  std::copy(requested.begin(), requested.end(), request_fields.begin() + 1);
  const auto request = raw_command_linked(2062, incarnation, request_fields, credit);
  const int result = ::sigaction(signal, &action, &previous);
  const int error = errno;
  const bool acquired = result == 0;
  std::array<ByteView, requested.size() + 3> returned{};
  returned[0] = raw_native_field(result);
  returned[1] = raw_native_field(error);
  returned[2] = raw_native_field(acquired);
  if (acquired) {
    const auto fields = wake_native_action(previous);
    std::copy(fields.begin(), fields.end(), returned.begin() + 3);
  }
  (void)raw_command_linked(
      2063, request, std::span{returned}.first(acquired ? returned.size() : 3), credit);
  return {result, error};
}
int raw_command_mask(int how, const sigset_t &requested, sigset_t &previous,
                     RawEventRef incarnation, RawControlToken *credit) noexcept {
  const auto request = raw_command_linked(
      2064, incarnation, std::array{raw_native_field(how), wake_native_mask(requested)},
      credit);
  const int result = ::pthread_sigmask(how, &requested, &previous);
  const int error = errno;
  const bool acquired = result == 0;
  const std::array returned{raw_native_field(result), raw_native_field(error),
                            raw_native_field(acquired),
                            acquired ? wake_native_mask(previous) : ByteView{}};
  (void)raw_command_linked(
      2065, request, std::span{returned}.first(acquired ? returned.size() : 3), credit);
  return result;
}
RawCommandInterrupt::RawCommandInterrupt(std::atomic_bool &interrupted)
    : owner_(raw_current_thread()), interrupted_(interrupted) {
  try {
    credit_ = raw_command_claim(2 * action_credit + 2 * mask_credit);
    reset_lifecycle_credit_ = raw_command_claim(raw_wake_lifecycle_credit);
    wake_ = std::make_unique<RawCommandWake>(RawCommandWake::Mode::signal);
    {
      RawThreadBinding wake_owner{wake_->owner_};
      reset_wake_credit_ =
          raw_command_claim(raw_wake_rearm_credit + raw_wake_notify_credit);
    }
    signal_wake_ = wake_.get();
    RawCommandInterrupt *expected = nullptr;
    if (!interrupt_owner.compare_exchange_strong(expected, this,
                                                 std::memory_order_acq_rel))
      throw Error{ErrorCode::busy};
    wake_->publish();
    struct sigaction action{};
    action.sa_handler = &RawCommandInterrupt::handler;
    sigemptyset(&action.sa_mask);
    const auto incarnation =
        wake_ref(wake_->incarnation_lane_, wake_->incarnation_ordinal_);
    const auto installed = raw_command_action(SIGINT, action, previous_, incarnation,
                                              credit_ ? &*credit_ : nullptr);
    if (installed.result < 0) {
      interrupt_owner.store(nullptr, std::memory_order_release);
      wake_->retire();
      throw Error{ErrorCode::io, installed.error};
    }
  } catch (...) {
    release_reset();
    throw;
  }
}
RawCommandInterrupt::~RawCommandInterrupt() {
  const ErrnoGuard guard;
  // The containing owner has joined every thread that could have loaded this
  // handler target. Blocking the final owner prevents its own handler crossing
  // target removal, restoration, and endpoint destruction.
  RawThreadBinding owner{owner_};
  const auto incarnation =
      wake_ref(wake_->incarnation_lane_, wake_->incarnation_ordinal_);
  sigset_t blocked{}, previous{}, discarded{};
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGINT);
  if (raw_command_mask(SIG_BLOCK, blocked, previous, incarnation,
                       credit_ ? &*credit_ : nullptr) != 0)
    ::_exit(70);
  interrupt_owner.store(nullptr, std::memory_order_release);
  struct sigaction returned{};
  if (raw_command_action(SIGINT, previous_, returned, incarnation,
                         credit_ ? &*credit_ : nullptr)
          .result < 0)
    ::_exit(70);
  wake_->retire();
  release_reset();
  wake_.reset();
  if (raw_command_mask(SIG_SETMASK, previous, discarded, incarnation,
                       credit_ ? &*credit_ : nullptr) != 0)
    ::_exit(70);
}
void RawCommandInterrupt::handler(int) noexcept {
  const int saved = errno;
  auto *owner = interrupt_owner.load(std::memory_order_acquire);
  if (owner != nullptr) {
    owner->interrupted_.store(true, std::memory_order_relaxed);
    owner->signal_wake_->notify(nullptr, &owner->interrupted_);
  }
  errno = saved;
}
void RawCommandInterrupt::release_reset() noexcept {
  RawThreadBinding caller{owner_};
  reset_lifecycle_credit_.reset();
  if (wake_) {
    RawThreadBinding wake_owner{wake_->owner_};
    reset_wake_credit_.reset();
  }
}
Result<void> RawCommandInterrupt::prepare_reset() noexcept {
  RawThreadBinding caller{owner_};
  if (!raw_enabled())
    return Result<void>::success();
  if (!reset_lifecycle_credit_) {
    auto acquired = raw_try_claim_control(raw_wake_lifecycle_credit);
    if (!acquired.has_value())
      return Result<void>::failure(acquired.error());
    reset_lifecycle_credit_.emplace(std::move(acquired).value());
  }
  RawThreadBinding wake_owner{wake_->owner_};
  if (!reset_wake_credit_) {
    auto acquired =
        raw_try_claim_control(raw_wake_rearm_credit + raw_wake_notify_credit);
    if (!acquired.has_value())
      return Result<void>::failure(acquired.error());
    reset_wake_credit_.emplace(std::move(acquired).value());
  }
  return Result<void>::success();
}
Result<void> RawCommandInterrupt::reset() noexcept {
  const ErrnoGuard guard;
  RawThreadBinding caller{owner_};
  auto ready = prepare_reset();
  if (!ready.has_value())
    return ready;
  const bool previous = interrupted_.exchange(false, std::memory_order_relaxed);
  auto result =
      wake_->clear_paid(reset_lifecycle_credit_ ? &*reset_lifecycle_credit_ : nullptr,
                        reset_wake_credit_ ? &*reset_wake_credit_ : nullptr);
  release_reset();
  if (result.has_value())
    result = prepare_reset(); // Next admission is only after actual native rearm.
  if (!result.has_value() && (previous || result.error().code == ErrorCode::busy))
    interrupted_.store(true, std::memory_order_relaxed);
  if (interrupted_.load(std::memory_order_relaxed))
    wake_->notify(nullptr, &interrupted_);
  return result;
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
2045:delivery/admission request Ref (zero for initial object shutdown),job Text (empty for shutdown),operation:u8(1=configure,2=archive,3=initial orderly shutdown,4=automatic settled retirement),branch:u8(1=success,2=Error,3=exception),Error,max_live:size_t,max_records:size_t.
2046:launch Ref,output_fd:int,output_events:short,bootstrap_fd:int,bootstrap_events:short,timeout:int.
2047:request Ref,result:int,errno:int,output_revents:short,bootstrap_revents:short.
2050:parent Ref,domain:int,type:int,protocol:int. Socket incarnation begins here.
2051:request Ref,result:int,errno:int,returned_fds:int[2].
2052:incarnation Ref,fd:int,command:int,argument:int.
2053:request Ref,result:int,errno:int.
2054:incarnation Ref,fd:int.
2055:request Ref,result:int,errno:int. Close is attempted once.
2056:incarnation Ref,fd:int,how:int. One gate winner issues SHUT_WR per incarnation.
2057:request Ref,result:int,errno:int. Native failure with potential borrowers ends the owner process after this literal return; no orderly join is claimed.
2058:incarnation Ref,source:int,target:int. Actual dup2 replaces a joined incarnation at stable descriptor numbers.
2059:request Ref,result:int,errno:int.
2060:old incarnation Ref,operation:u8(1=reset,2=retire),fds:int[2]. Busy reset has zero Ref and {-1,-1}: its gate granted no metadata view.
2061:request Ref,branch:u8(1=returned,2=Error),Error. Capacity refusal before any descriptor effect preserves the old incarnation.
2062:incarnation Ref,signal:int,handler:function pointer,flags:int,native signal mask bytes,restorer:function pointer on Linux. Mask is4 native bytes on Darwin,8 on the qualified Linux kernel ABI; no padding or unused libc words.
2063:request Ref,result:int,errno:int,previous-present:bool,then successful previous action members in2062 order without signal. No fields are read from a failed acquisition.
2064:incarnation Ref,how:int,exact requested native signal mask bytes in2062 layout.
2065:request Ref,result:int,errno:int,previous-present:bool,then successful previous native signal mask bytes. pthread result is authoritative.
)";
}
} // namespace blackbird
