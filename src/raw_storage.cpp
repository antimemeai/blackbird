#include "blackbird/raw_storage.hpp"
#include "blackbird/journal_storage.hpp"
#include "blackbird/journal_writer.hpp"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/file.h>
#include <unistd.h>

namespace blackbird {
namespace {
thread_local const char issuer_thread = 0;
struct PreserveErrno {
  int value = errno;
  ~PreserveErrno() { errno = value; }
};
template <class T> ByteView field(const T &value) noexcept {
  return detail::raw_native_field(value);
}
ByteView bytes(std::string_view value) noexcept {
  return detail::raw_string_field(value);
}
template <class T> T take(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
RawControlToken claim(std::uint64_t amount) { return take(raw_claim_control(amount)); }
RawCapacityClaim data_claim(std::uint64_t amount) {
  return take(raw_claim_data(amount));
}
RawControlToken split(RawControlToken &bank, std::uint64_t amount) {
  return take(bank.split(amount));
}
RawEventRef emit(std::uint32_t site, std::span<const ByteView> body,
                 RawCapacityClaim &bank,
                 RawPartition partition = RawPartition::control) noexcept {
  const PreserveErrno guard;
  auto result = raw_emit(site, body, partition, &bank);
  return result.has_value() ? RawEventRef{raw_lane_epoch(), result.value()}
                            : RawEventRef{};
}
struct Fields {
  std::array<ByteView, 40> values{};
  std::size_t size = 0;
  void add(ByteView value) {
    if (size == values.size())
      throw Error{ErrorCode::capacity};
    values[size++] = value;
  }
  template <class T> void native(const T &value) { add(field(value)); }
  void ref(const RawEventRef &value) {
    add(value.lane);
    native(value.ordinal);
  }
  std::span<const ByteView> view() const noexcept {
    return std::span{values}.first(size);
  }
  std::uint64_t amount() const {
    std::uint64_t sum = 16;
    for (auto value : view())
      sum += value.size();
    return sum;
  }
};
void outcome(std::uint32_t site, RawEventRef request, std::optional<Error> error,
             RawControlToken &credit, std::uint8_t success = 0) noexcept {
  const auto branch = error ? std::uint8_t{1} : success;
  Fields body;
  body.ref(request);
  body.native(branch);
  if (error) {
    body.native(error->code);
    body.native(error->detail);
  }
  (void)emit(site, body.view(), credit);
}
bool add_checked(std::uint64_t &value, std::uint64_t other) noexcept {
  if (other > UINT64_MAX - value)
    return false;
  value += other;
  return true;
}
bool product_checked(std::uint64_t &value, std::uint64_t a, std::uint64_t b) noexcept {
  if (a && b > UINT64_MAX / a)
    return false;
  return add_checked(value, a * b);
}
std::uint64_t ceil_io(std::uint64_t bytes_count) noexcept {
  return bytes_count / raw_storage_io_limit + (bytes_count % raw_storage_io_limit != 0);
}
#if defined(__APPLE__)
constexpr std::uint64_t extent_credit = 258, sync_credit = 241;
#else
constexpr std::uint64_t extent_credit = 270, sync_credit = 237;
#endif
constexpr std::uint64_t read_credit = 271, write_credit = 303;
template <class T> T &required(std::optional<T> &value) noexcept {
  if (!value.has_value())
    ::_exit(74);
  return *value;
}
} // namespace

class RawStorageCleanupState {
public:
  RawThreadHandle handle;
  const pid_t process = ::getpid();
  void require_process() const noexcept {
    if (::getpid() != process)
      ::_exit(74);
  }
  std::mutex mutex;
  RawEventRef setup;
  std::optional<RawControlToken> end;
  ~RawStorageCleanupState() {
    require_process();
    if (end) {
      {
        const std::lock_guard lock{mutex};
        RawThreadBinding binding{handle};
        outcome(3047, setup, {}, *end);
        end.reset();
      }
    }
    // RawThreadContext destruction may drain. It must occur outside mutex.
    handle.reset();
  }
};
class RawStorageAuthority {
public:
  RawEventRef setup;
  const pid_t process = ::getpid();
  RawThreadHandle primary;
  const void *primary_thread = nullptr;
  std::shared_ptr<RawStorageCleanupState> cleanup;
  std::atomic<const void *> issuer{nullptr}, thread{nullptr};
  std::atomic_bool pending{false};
  bool permits() const noexcept {
    const auto current = raw_current_thread();
    return ::getpid() == process && current &&
           issuer.load(std::memory_order_acquire) == current.get() &&
           thread.load(std::memory_order_acquire) == &issuer_thread;
  }
  bool permits_thread() const noexcept {
    return ::getpid() == process && issuer.load(std::memory_order_acquire) != nullptr &&
           thread.load(std::memory_order_acquire) == &issuer_thread;
  }
  bool permits_origin_read() const noexcept {
    return permits_thread() &&
           (!pending.load(std::memory_order_acquire) ||
            issuer.load(std::memory_order_acquire) != primary.get());
  }
  [[noreturn]] void failed_custody(bool proved_issuer = false) const noexcept {
    if ((proved_issuer && ::getpid() == process) || permits())
      raw_fence({ErrorCode::wrong_incarnation});
    ::_exit(74);
  }
  void require() const {
    if (!permits())
      throw Error{ErrorCode::wrong_incarnation};
  }
  void bind(const RawThreadHandle &handle) noexcept {
    issuer.store(nullptr, std::memory_order_release);
    thread.store(&issuer_thread, std::memory_order_release);
    issuer.store(handle.get(), std::memory_order_release);
  }
};

Result<RawStorageLoanReservation>
raw_storage_loan_reservation(const RawStorageLoanBudget &b) noexcept {
  using R = Result<RawStorageLoanReservation>;
  const auto kind = static_cast<std::uint8_t>(b.kind);
  if (kind < 1 || kind > 5 || !b.batches || !b.submissions || !b.write_bytes)
    return R::failure({ErrorCode::invalid_range});
  RawStorageLoanReservation r;
  r.scopes = b.kind == RawStorageLoanKind::selected_exec ? 3 : 1;
  r.write_calls = ceil_io(b.write_bytes);
  if (!add_checked(r.write_calls, b.batches - 1))
    return R::failure({ErrorCode::overflow});
  const auto p =
      b.catalog_maximum
          ? std::min<std::uint64_t>(
                static_cast<std::uint64_t>(std::bit_width(b.catalog_maximum)) + 1, 33)
          : 0;
  if (b.catalog_finds && p > UINT64_MAX / b.catalog_finds)
    return R::failure({ErrorCode::overflow});
  r.probes = p * b.catalog_finds;
  r.control = 222 + 189 * r.scopes;
#if defined(__APPLE__)
  constexpr std::uint64_t batch = 1015, submission = 774;
#else
  constexpr std::uint64_t batch = 1047, submission = 810;
#endif
  if (!product_checked(r.control, b.batches, batch) ||
      !product_checked(r.control, b.submissions, submission) ||
      !product_checked(r.control, r.write_calls, 303) ||
      !product_checked(r.control, r.probes, 271) ||
      !product_checked(r.control, b.positive_read_calls, 271))
    return R::failure({ErrorCode::overflow});
  if (b.kind == RawStorageLoanKind::selected_exec &&
      !product_checked(r.control, b.submissions, 148))
    return R::failure({ErrorCode::overflow});
  r.data = b.write_bytes;
  if (!product_checked(r.data, r.write_calls, 86) ||
      !product_checked(r.data, r.probes, 145) ||
      !add_checked(r.data, b.positive_read_bytes) ||
      !product_checked(r.data, b.positive_read_calls, 49))
    return R::failure({ErrorCode::overflow});
  if (b.batches > UINT64_MAX / 3 - b.submissions)
    return R::failure({ErrorCode::overflow});
  return R::success(r);
}
Result<RawProductStorageOwner>
RawProductStorageOwner::prepare(std::optional<RawEventRef> parent) {
  using R = Result<RawProductStorageOwner>;
  const PreserveErrno guard;
  if (!raw_enabled())
    return R::success({});
  std::optional<RawControlToken> credit;
  RawEventRef setup;
  try {
    credit.emplace(claim(92));
    const bool present = parent.has_value();
    const std::uint8_t kind = 1;
    Fields body;
    body.native(present);
    if (parent)
      body.ref(*parent);
    body.native(kind);
    setup = emit(3000, body.view(), *credit);
    auto authority = std::make_shared<RawStorageAuthority>();
    authority->setup = setup;
    authority->primary = raw_current_thread();
    if (!authority->primary)
      throw Error{ErrorCode::wrong_incarnation};
    authority->primary_thread = &issuer_thread;
    authority->bind(authority->primary);
    auto cleanup_credit = claim(97);
    Fields request;
    request.ref(setup);
    const auto cleanup_request = emit(3045, request.view(), cleanup_credit);
    try {
      auto cleanup = std::make_shared<RawStorageCleanupState>();
      cleanup->handle = take(raw_prepare_thread());
      cleanup->setup = cleanup_request;
      {
        RawThreadBinding binding{cleanup->handle};
        cleanup->end.emplace(claim(50));
      }
      const std::uint8_t complete = 0;
      Fields returned;
      returned.ref(cleanup_request);
      returned.native(complete);
      IdentityBytes cleanup_lane;
      {
        RawThreadBinding binding{cleanup->handle};
        cleanup_lane = raw_lane_epoch();
      }
      returned.add(cleanup_lane);
      (void)emit(3046, returned.view(), cleanup_credit);
      cleanup->setup = cleanup_request;
      authority->cleanup = std::move(cleanup);
    } catch (const Error &error) {
      outcome(3046, cleanup_request, error, cleanup_credit);
      throw;
    } catch (const std::bad_alloc &) {
      outcome(3046, cleanup_request, Error{ErrorCode::allocation}, cleanup_credit);
      throw;
    }
    outcome(3001, setup, {}, *credit);
    return R::success(RawProductStorageOwner{std::move(authority)});
  } catch (const Error &error) {
    if (credit)
      outcome(3001, setup, error, *credit);
    return R::failure(error);
  } catch (const std::bad_alloc &) {
    if (credit)
      outcome(3001, setup, Error{ErrorCode::allocation}, *credit);
    return R::failure({ErrorCode::allocation});
  }
}
RawEventRef RawProductStorageOwner::reference() const noexcept {
  return authority_ ? authority_->setup : RawEventRef{};
}
bool RawProductStorageOwner::permits_current() const noexcept {
  return !authority_ || authority_->permits();
}
bool RawProductStorageOwner::permits_thread() const noexcept {
  return !authority_ || authority_->permits_thread();
}
bool RawProductStorageOwner::permits_origin_read() const noexcept {
  return !authority_ || authority_->permits_origin_read();
}
RawStorageCleanupBank::~RawStorageCleanupBank() { reset(); }
RawStorageCleanupBank::RawStorageCleanupBank(RawStorageCleanupBank &&other) noexcept {
  auto state = other.state_;
  if (state) {
    state->require_process();
    const std::lock_guard lock{state->mutex};
    state_ = std::move(other.state_);
    credit_ = std::move(other.credit_);
    other.credit_.reset();
  }
}
RawStorageCleanupBank &
RawStorageCleanupBank::operator=(RawStorageCleanupBank &&other) noexcept {
  if (this != &other) {
    reset();
    auto state = other.state_;
    if (state) {
      state->require_process();
      const std::lock_guard lock{state->mutex};
      state_ = std::move(other.state_);
      credit_ = std::move(other.credit_);
      other.credit_.reset();
    }
  }
  return *this;
}
void RawStorageCleanupBank::reset() noexcept {
  auto state = std::move(state_);
  if (!state)
    return;
  state->require_process();
  {
    const std::lock_guard lock{state->mutex};
    RawThreadBinding binding{state->handle};
    credit_.reset();
  }
}
RawStorageCleanupBank
RawStorageCleanupBank::prepare(const std::shared_ptr<RawStorageCleanupState> &state,
                               std::uint64_t amount) {
  state->require_process();
  RawStorageCleanupBank bank;
  bank.state_ = state;
  {
    const std::lock_guard lock{state->mutex};
    RawThreadBinding binding{state->handle};
    bank.credit_.emplace(take(raw_try_claim_control(amount)));
  }
  return bank;
}
RawRetainedOwnerEnd::RawRetainedOwnerEnd(RawStorageCleanupBank bank,
                                         RawEventRef factory)
    : bank_(std::move(bank)), factory_(factory) {}
RawRetainedOwnerEnd::~RawRetainedOwnerEnd() = default;
RawRetainedSettlementEnd::RawRetainedSettlementEnd(RawStorageCleanupBank bank,
                                                   RawEventRef protection)
    : bank_(std::move(bank)), protection_(protection) {}
RawRetainedSettlementEnd::~RawRetainedSettlementEnd() = default;
Result<std::unique_ptr<RawRetainedSettlementEnd>>
RawRetainedSettlementEnd::prepare(const RawProductStorageOwner &owner,
                                  RawEventRef protection) {
  using R = Result<std::unique_ptr<RawRetainedSettlementEnd>>;
  if (!owner.enabled())
    return R::success({});
  try {
    owner.authority_->require();
    auto bank = RawStorageCleanupBank::prepare(owner.authority_->cleanup, 90);
    return R::success(std::unique_ptr<RawRetainedSettlementEnd>{
        new RawRetainedSettlementEnd{std::move(bank), protection}});
  } catch (const Error &error) {
    return R::failure(error);
  } catch (const std::bad_alloc &) {
    return R::failure({ErrorCode::allocation});
  }
}
void RawRetainedSettlementEnd::finish() noexcept {
  if (finished_)
    return;
  const PreserveErrno guard;
  auto cleanup = bank_.state_;
  cleanup->require_process();
  {
    const std::lock_guard lock{cleanup->mutex};
    RawThreadBinding binding{cleanup->handle};
    Fields request;
    request.ref(protection_);
    const auto end = emit(3128, request.view(), required(bank_.credit_));
    outcome(3129, end, {}, required(bank_.credit_));
    bank_.credit_.reset();
    finished_ = true;
  }
}
Result<std::unique_ptr<RawRetainedOwnerEnd>>
RawRetainedOwnerEnd::prepare(const RawProductStorageOwner &owner, RawEventRef factory) {
  using R = Result<std::unique_ptr<RawRetainedOwnerEnd>>;
  if (!owner.enabled())
    return R::success({});
  try {
    owner.authority_->require();
    auto bank = RawStorageCleanupBank::prepare(owner.authority_->cleanup, 125);
    return R::success(std::unique_ptr<RawRetainedOwnerEnd>{
        new RawRetainedOwnerEnd{std::move(bank), factory}});
  } catch (const Error &error) {
    return R::failure(error);
  } catch (const std::bad_alloc &) {
    return R::failure({ErrorCode::allocation});
  }
}
void RawRetainedOwnerEnd::finish(const JournalCursor &cursor, JournalWriterState state,
                                 bool pending, bool prepared) noexcept {
  if (finished_)
    return;
  const PreserveErrno guard;
  auto cleanup = bank_.state_;
  cleanup->require_process();
  {
    const std::lock_guard lock{cleanup->mutex};
    RawThreadBinding binding{cleanup->handle};
    Fields request;
    request.ref(factory_);
    request.add(cursor.journal.bytes());
    request.native(cursor.sequence);
    request.native(cursor.end_offset);
    const auto end = emit(3108, request.view(), required(bank_.credit_));
    const std::uint8_t branch = 0;
    Fields returned;
    returned.ref(end);
    returned.native(state);
    returned.native(pending);
    returned.native(prepared);
    returned.native(branch);
    (void)emit(3109, returned.view(), required(bank_.credit_));
    bank_.credit_.reset();
    finished_ = true;
  }
}
RawStorageResource::RawStorageResource(std::shared_ptr<RawStorageAuthority> authority)
    : authority_(std::move(authority)) {}
std::unique_ptr<RawStorageResource>
RawStorageResource::prepare(const RawProductStorageOwner &owner, bool stream) {
  if (!owner.enabled())
    return {};
  const PreserveErrno guard;
  owner.authority_->require();
  auto result =
      std::unique_ptr<RawStorageResource>{new RawStorageResource{owner.authority_}};
  result->close_ =
      RawStorageCleanupBank::prepare(owner.authority_->cleanup, stream ? 164 : 160);
  return result;
}
RawStorageResource::~RawStorageResource() { close(); }
RawProductStorageOwner RawStorageResource::owner() const {
  return RawProductStorageOwner{authority_};
}
void RawStorageResource::attach(int fd, RawEventRef reference,
                                RawEventRef action) noexcept {
  descriptor_ = fd;
  reference_ = reference;
  action_ = action;
}
void RawStorageResource::stream(DIR *value) noexcept { stream_ = value; }
void RawStorageResource::close() noexcept {
  if (descriptor_ < 0)
    return;
  const PreserveErrno guard;
  auto state = authority_->cleanup;
  state->require_process();
  {
    const std::lock_guard lock{state->mutex};
    RawThreadBinding binding{state->handle};
    const std::uint8_t method = 1, kind = stream_ ? 2 : 1;
    Fields body;
    body.ref(action_);
    body.ref(reference_);
    body.native(method);
    body.native(kind);
    if (stream_)
      body.native(stream_);
    else
      body.native(descriptor_);
    auto request = emit(3017, body.view(), required(close_.credit_));
    const int returned = stream_ ? ::closedir(stream_) : ::close(descriptor_);
    const int error = errno;
    Fields result;
    result.ref(request);
    result.native(returned);
    const bool failed = returned < 0;
    result.native(failed);
    if (failed)
      result.native(error);
    (void)emit(3018, result.view(), required(close_.credit_));
    const std::uint8_t closure = returned < 0 ? 1 : 0;
    Fields end;
    end.ref(reference_);
    end.native(closure);
    (void)emit(3029, end.view(), required(close_.credit_));
    descriptor_ = -1;
    stream_ = nullptr;
    close_.credit_.reset();
    release_.credit_.reset();
  }
}
void RawStorageResource::prepare_release() {
  authority_->require();
  if (!release_.funded())
    release_ = RawStorageCleanupBank::prepare(authority_->cleanup, 238);
}
void RawStorageResource::acquired_writer(bool success) noexcept {
  if (success) {
    lease_ = Lease::held;
    unlock_error_.reset();
  } else if (lease_ == Lease::unheld)
    release_.reset();
}
void RawStorageResource::release_writer() noexcept {
  if (lease_ == Lease::unheld || !release_.funded())
    return;
  const PreserveErrno guard;
  auto state = authority_->cleanup;
  state->require_process();
  {
    const std::lock_guard lock{state->mutex};
    RawThreadBinding binding{state->handle};
    const auto kind = RawStorageActionKind::release_writer;
    const bool present = true;
    Fields start;
    start.ref(authority_->setup);
    start.native(kind);
    start.native(present);
    start.ref(reference_);
    const auto action = emit(3002, start.view(), required(release_.credit_));
    const std::uint8_t method = 1;
    const int operation = LOCK_UN;
    Fields request;
    request.ref(action);
    request.ref(reference_);
    request.native(method);
    request.native(descriptor_);
    request.native(operation);
    const auto call = emit(3015, request.view(), required(release_.credit_));
    const int status = ::flock(descriptor_, operation);
    const int native_errno = errno;
    const bool failed = status < 0;
    Fields result;
    result.ref(call);
    result.native(status);
    result.native(failed);
    if (failed)
      result.native(native_errno);
    (void)emit(3016, result.view(), required(release_.credit_));
    if (failed) {
      lease_ = Lease::unlock_failed;
      unlock_error_ = Error{ErrorCode::io, native_errno};
    } else {
      lease_ = Lease::unheld;
      unlock_error_.reset();
    }
    outcome(3003, action, unlock_error_, required(release_.credit_));
    release_.credit_.reset();
  }
}

RawStorageHandoff::RawStorageHandoff(std::shared_ptr<RawStorageAuthority> authority,
                                     RawThreadHandle child)
    : authority_(std::move(authority)), child_(std::move(child)) {}
Result<std::unique_ptr<RawStorageHandoff>>
RawProductStorageOwner::prepare_handoff(RawThreadHandle child,
                                        RawWorkerDispatch dispatch) const {
  using R = Result<std::unique_ptr<RawStorageHandoff>>;
  if (!enabled())
    return R::success({});
  const PreserveErrno guard;
  std::optional<RawControlToken> credit;
  RawEventRef request;
  bool reserved = false;
  try {
    authority_->require();
    credit.emplace(claim(131));
    if (!child)
      throw Error{ErrorCode::wrong_incarnation};
    IdentityBytes child_lane;
    {
      RawThreadBinding binding{child};
      child_lane = raw_lane_epoch();
    }
    const RawEventRef dispatch_ref{dispatch.lane, dispatch.ordinal};
    const std::uint8_t kind = 1;
    Fields fields;
    fields.ref(authority_->setup);
    fields.add(child_lane);
    fields.ref(dispatch_ref);
    fields.native(kind);
    request = emit(3040, fields.view(), *credit);
    if (authority_->pending.exchange(true, std::memory_order_acq_rel))
      throw Error{ErrorCode::busy};
    reserved = true;
    auto handoff = std::unique_ptr<RawStorageHandoff>{
        new RawStorageHandoff{authority_, std::move(child)}};
    handoff->preparation_ = request;
    handoff->owns_pending_ = true;
    reserved = false;
    {
      RawThreadBinding binding{handoff->child_};
      handoff->bank_.emplace(claim(188));
    }
    outcome(3041, request, {}, *credit);
    return R::success(std::move(handoff));
  } catch (const Error &error) {
    if (reserved)
      authority_->pending.store(false, std::memory_order_release);
    if (credit)
      outcome(3041, request, error, *credit);
    return R::failure(error);
  } catch (const std::bad_alloc &) {
    if (reserved)
      authority_->pending.store(false, std::memory_order_release);
    if (credit)
      outcome(3041, request, Error{ErrorCode::allocation}, *credit);
    return R::failure({ErrorCode::allocation});
  }
}
RawStorageHandoff::~RawStorageHandoff() { finish(); }
void RawStorageHandoff::enter(RawEventRef command) {
  if (::getpid() != authority_->process || active_ || finished_ ||
      raw_current_thread() != child_ ||
      authority_->issuer.load(std::memory_order_acquire) != authority_->primary.get())
    throw Error{ErrorCode::wrong_incarnation};
  Fields fields;
  fields.ref(authority_->setup);
  fields.ref(preparation_);
  fields.ref(command);
  entry_ = emit(3042, fields.view(), required(bank_));
  authority_->bind(child_);
  active_ = true;
  outcome(3043, entry_, {}, required(bank_));
}
void RawStorageHandoff::finish(std::optional<Error> error) noexcept {
  if (finished_)
    return;
  if (!authority_->permits())
    authority_->failed_custody();
  const PreserveErrno guard;
  {
    RawThreadBinding binding{child_};
    if (bank_) {
      outcome(3044, active_ ? entry_ : preparation_, error, required(bank_),
              active_ ? 0 : 2);
      bank_.reset();
    }
  }
  // Restore the parent only after lexical child custody ends. Terminal joins it
  // before parent idle product access.
  authority_->issuer.store(nullptr, std::memory_order_release);
  authority_->thread.store(authority_->primary_thread, std::memory_order_release);
  authority_->issuer.store(authority_->primary.get(), std::memory_order_release);
  if (owns_pending_) {
    authority_->pending.store(false, std::memory_order_release);
    owns_pending_ = false;
  }
  finished_ = true;
  active_ = false;
}
RawStorageAuthorityScope::RawStorageAuthorityScope(RawStorageHandoff *handoff,
                                                   RawEventRef command)
    : handoff_(handoff) {
  if (handoff_)
    handoff_->enter(command);
}
RawStorageAuthorityScope::~RawStorageAuthorityScope() {
  if (handoff_)
    handoff_->finish(error_);
}

Result<std::unique_ptr<RawStorageSettlementLoan>>
RawStorageSettlementLoan::prepare(const void *root, const RawProductStorageOwner &owner,
                                  RawStorageLoanBudget budget,
                                  std::optional<RawEventRef> parent) {
  if (!owner.enabled())
    return Result<std::unique_ptr<RawStorageSettlementLoan>>::success({});
  try {
    owner.authority()->require();
    return prepare_authorized(root, owner, budget, parent);
  } catch (const Error &error) {
    return Result<std::unique_ptr<RawStorageSettlementLoan>>::failure(error);
  }
}
Result<std::unique_ptr<RawStorageSettlementLoan>>
RawProductStorageOwner::prepare_joined_loan(RawThreadHandle child, const void *root,
                                            RawStorageLoanBudget budget,
                                            std::optional<RawEventRef> parent) const {
  using R = Result<std::unique_ptr<RawStorageSettlementLoan>>;
  if (!enabled())
    return R::success({});
  try {
    authority_->require();
    if (!child || budget.kind != RawStorageLoanKind::joined_terminal)
      throw Error{ErrorCode::wrong_incarnation};
    RawThreadBinding binding{std::move(child)};
    return RawStorageSettlementLoan::prepare_authorized(root, *this, budget, parent);
  } catch (const Error &error) {
    return R::failure(error);
  }
}
Result<std::unique_ptr<RawStorageSettlementLoan>>
RawStorageHandoff::prepare_loan(const void *root, RawStorageLoanBudget budget,
                                std::optional<RawEventRef> parent) {
  using R = Result<std::unique_ptr<RawStorageSettlementLoan>>;
  try {
    authority_->require();
    if (active_ || finished_ || budget.kind != RawStorageLoanKind::stop)
      throw Error{ErrorCode::wrong_incarnation};
    RawThreadBinding binding{child_};
    return RawStorageSettlementLoan::prepare_authorized(
        root, RawProductStorageOwner{authority_}, budget, parent);
  } catch (const Error &error) {
    return R::failure(error);
  }
}
RawStorageSettlementLoan::RawStorageSettlementLoan(const void *root,
                                                   RawProductStorageOwner owner,
                                                   RawStorageLoanBudget budget)
    : root_(root), owner_(std::move(owner)), issuer_(raw_current_thread()),
      issuing_thread_(owner_.permits_current() ? &issuer_thread : nullptr),
      issuing_process_(::getpid()), budget_(budget) {}
Result<std::unique_ptr<RawStorageSettlementLoan>>
RawStorageSettlementLoan::prepare_authorized(const void *root,
                                             const RawProductStorageOwner &owner,
                                             RawStorageLoanBudget budget,
                                             std::optional<RawEventRef> parent) {
  using R = Result<std::unique_ptr<RawStorageSettlementLoan>>;
  if (!owner.enabled())
    return R::success({});
  const PreserveErrno guard;
  std::optional<RawControlToken> setup;
  RawEventRef request;
  try {

    setup.emplace(claim(172));
    const bool present = parent.has_value();
    Fields fields;
    const auto owner_reference = owner.reference();
    fields.ref(owner_reference);
    fields.native(present);
    if (parent)
      fields.ref(*parent);
    fields.native(budget.kind);
    fields.native(budget.write_bytes);
    fields.native(budget.batches);
    fields.native(budget.submissions);
    fields.native(budget.catalog_finds);
    fields.native(budget.catalog_maximum);
    fields.native(budget.positive_read_bytes);
    fields.native(budget.positive_read_calls);
    request = emit(3033, fields.view(), *setup);
    auto reservation = take(raw_storage_loan_reservation(budget));
    if (!root || !raw_current_thread())
      throw Error{ErrorCode::wrong_incarnation};
    auto loan = std::unique_ptr<RawStorageSettlementLoan>{
        new RawStorageSettlementLoan{root, owner, budget}};
    loan->reference_ = request;
    loan->scopes_ = reservation.scopes;
    loan->extents_ = 3 * (budget.batches + budget.submissions);
    loan->syncs_ = budget.batches;
    loan->write_calls_ = reservation.write_calls;
    loan->catalog_calls_ = reservation.probes;
    loan->read_calls_ = budget.positive_read_calls;
    loan->read_bytes_ = budget.positive_read_bytes;
    loan->retargets_ = budget.kind == RawStorageLoanKind::selected_exec
                           ? static_cast<std::uint64_t>(budget.submissions)
                           : 0;
    const auto lifecycle = 50 + 189 * reservation.scopes + 148 * loan->retargets_;
    try {
      loan->control_.emplace(claim(reservation.control - 172 - lifecycle));
      loan->lifecycle_.emplace(claim(lifecycle));
      loan->writes_.emplace(
          data_claim(budget.write_bytes + 86 * reservation.write_calls));
      if (reservation.probes)
        loan->catalog_.emplace(data_claim(145 * reservation.probes));
      if (budget.positive_read_calls)
        loan->reads_.emplace(
            data_claim(budget.positive_read_bytes + 49 * budget.positive_read_calls));
    } catch (...) {
      loan->joined();
      throw;
    }
    outcome(3034, request, {}, *setup);
    return R::success(std::move(loan));
  } catch (const Error &error) {
    if (setup)
      outcome(3034, request, error, *setup);
    return R::failure(error);
  } catch (const std::bad_alloc &) {
    if (setup)
      outcome(3034, request, Error{ErrorCode::allocation}, *setup);
    return R::failure({ErrorCode::allocation});
  }
}
RawStorageSettlementLoan::~RawStorageSettlementLoan() { finish(); }
void RawStorageSettlementLoan::started() noexcept { joined_ = false; }
void RawStorageSettlementLoan::joined() noexcept { joined_ = true; }
void RawStorageSettlementLoan::finish(std::optional<Error> error) noexcept {
  if (finished_)
    return;
  const PreserveErrno guard;
  // The caller owns the issuer and releases all borrowed scopes first.
  const bool original_issuer = raw_current_thread() == issuer_ &&
                               issuing_thread_ == &issuer_thread &&
                               issuing_process_ == ::getpid();
  if (active_ || raw_current_thread() != issuer_ ||
      (!original_issuer && !owner_.permits_current() &&
       !(joined_ && owner_.permits_thread())))
    owner_.authority()->failed_custody(original_issuer);
  if (lifecycle_) {
    outcome(3038, reference_, error, required(lifecycle_));
    lifecycle_.reset();
  }
  control_.reset();
  writes_.reset();
  catalog_.reset();
  reads_.reset();
  finished_ = true;
}
std::optional<RawControlToken>
RawStorageSettlementLoan::action(RawStorageActionKind kind, bool catalog,
                                 RawCapacityClaim *&data, std::uint64_t &read_limit) {
  if (!active_ || finished_ || raw_current_thread() != issuer_ ||
      !owner_.permits_thread())
    throw Error{ErrorCode::wrong_incarnation};
  std::uint64_t amount = 0;
  switch (kind) {
  case RawStorageActionKind::extent:
    if (!extents_)
      throw Error{ErrorCode::capacity};
    --extents_;
    amount = extent_credit;
    break;
  case RawStorageActionKind::synchronize:
    if (!syncs_)
      throw Error{ErrorCode::capacity};
    --syncs_;
    amount = sync_credit;
    break;
  case RawStorageActionKind::read_at:
    if (catalog) {
      if (!catalog_calls_)
        throw Error{ErrorCode::capacity};
      --catalog_calls_;
      data = &required(catalog_);
      read_limit = 96;
    } else {
      if (!read_calls_ || !read_bytes_)
        throw Error{ErrorCode::capacity};
      --read_calls_;
      data = &required(reads_);
      read_limit = std::min(read_bytes_, raw_storage_io_limit);
    }
    amount = read_credit;
    break;
  default:
    throw Error{ErrorCode::unsupported};
  }
  return split(required(control_), amount);
}
void RawStorageResource::begin_write(std::size_t bytes_count,
                                     RawStorageWriteContinuation &continuation) {
  if (!authority_->permits_origin_read())
    throw Error{ErrorCode::wrong_incarnation};
  if (!scope_)
    return;
  auto &loan = scope_->loan_;
  if (!loan.active_ || raw_current_thread() != loan.issuer_ ||
      !loan.owner_.permits_thread())
    throw Error{ErrorCode::wrong_incarnation};
  if (continuation.slots || continuation.resource ||
      loan.batches_ == loan.budget_.batches)
    throw Error{ErrorCode::capacity};
  const auto calls = ceil_io(bytes_count);
  if (!calls || calls > loan.write_calls_)
    throw Error{ErrorCode::capacity};
  loan.write_calls_ -= calls;
  ++loan.batches_;
  continuation.slots.emplace(split(required(loan.control_), calls * write_credit));
  continuation.remaining_calls = calls;
  continuation.resource = this;
}
RawStorageLoanScope::RawStorageLoanScope(RawStorageSettlementLoan &loan,
                                         RawStorageSelection selection)
    : loan_(loan), selection_(std::move(selection)) {
  if (loan.finished_ || loan.active_ || !loan.scopes_ ||
      selection_.root != loan.root_ ||
      selection_.owner.authority() != loan.owner_.authority() ||
      raw_current_thread() != loan.issuer_ ||
      (loan.budget_.kind == RawStorageLoanKind::joined_terminal && !loan.joined_))
    throw Error{ErrorCode::wrong_incarnation};
  if (loan.joined_) {
    if (!loan.owner_.permits_thread())
      throw Error{ErrorCode::wrong_incarnation};
  } else
    loan.owner_.authority()->require();
  journal_ = selection_.journal ? selection_.journal->raw_storage_resource() : nullptr;
  archive_ = selection_.archive ? selection_.archive->raw_storage_resource() : nullptr;
  // Custom memory/file adapters opt out rather than inventing physical objects.
  if (!journal_ || journal_->authority_ != loan.owner_.authority() ||
      journal_->scope_ ||
      (archive_ &&
       (archive_->authority_ != loan.owner_.authority() || archive_->scope_)))
    throw Error{ErrorCode::wrong_incarnation};
  bank_.emplace(split(required(loan.lifecycle_), 189));
  --loan.scopes_;
  const bool present = archive_ != nullptr;
  Fields fields;
  fields.ref(loan.reference_);
  fields.ref(journal_->reference_);
  fields.native(present);
  if (archive_)
    fields.ref(archive_->reference_);
  reference_ = emit(3035, fields.view(), required(bank_));
  journal_->scope_ = this;
  if (archive_)
    archive_->scope_ = this;
  loan.active_ = true;
  outcome(3036, reference_, {}, required(bank_));
}
RawStorageLoanScope::~RawStorageLoanScope() {
  if (raw_current_thread() != loan_.issuer_ || !loan_.owner_.permits_thread())
    loan_.owner_.authority()->failed_custody();
  if (journal_ && journal_->scope_ == this)
    journal_->scope_ = nullptr;
  if (archive_ && archive_->scope_ == this)
    archive_->scope_ = nullptr;
  loan_.active_ = false;
  if (bank_) {
    outcome(3037, reference_, error_, required(bank_));
    bank_.reset();
  }
}
RawStorageLoanPause::RawStorageLoanPause(const RawStorageSelection &selection) {
  if (!selection.owner.enabled())
    return;
  if (!selection.owner.permits_origin_read())
    throw Error{ErrorCode::wrong_incarnation};
  auto *journal =
      selection.journal ? selection.journal->raw_storage_resource() : nullptr;
  if (!journal || !journal->scope_)
    return;
  auto *scope = journal->scope_;
  auto &loan = scope->loan_;
  if (!loan.active_ || scope->paused_ || selection.root != loan.root_ ||
      selection.owner.authority() != loan.owner_.authority() ||
      raw_current_thread() != loan.issuer_ || !loan.owner_.permits_current() ||
      selection.journal != scope->selection_.journal ||
      selection.archive != scope->selection_.archive)
    throw Error{ErrorCode::wrong_incarnation};
  if (loan.budget_.kind != RawStorageLoanKind::selected_exec)
    throw Error{ErrorCode::unsupported};
  if (!loan.retargets_)
    throw Error{ErrorCode::capacity};
  bank_.emplace(split(required(loan.lifecycle_), 148));
  --loan.retargets_;
  scope_ = scope;
  scope_->paused_ = true;
  scope_->journal_->scope_ = nullptr;
  if (scope_->archive_)
    scope_->archive_->scope_ = nullptr;
}
RawStorageLoanPause::~RawStorageLoanPause() {
  if (!scope_)
    return;
  auto &loan = scope_->loan_;
  if (raw_current_thread() != loan.issuer_ || !loan.owner_.permits_thread())
    loan.owner_.authority()->failed_custody();
  // An interrupted caller must not leave a borrowed native pointer installed.
  // The scope retains its old objects and emits its actual unfinished outcome.
  scope_->error({ErrorCode::wrong_incarnation});
  bank_.reset();
}
Result<void> RawStorageLoanPause::restore(RawStorageSelection selection,
                                          std::uint64_t catalog_entries) {
  if (!scope_)
    return Result<void>::success();
  const PreserveErrno guard;
  auto &scope = *scope_;
  auto &loan = scope.loan_;
  if (raw_current_thread() != loan.issuer_ || !loan.owner_.permits_thread())
    loan.owner_.authority()->failed_custody();
  auto *journal =
      selection.journal ? selection.journal->raw_storage_resource() : nullptr;
  auto *archive =
      selection.archive ? selection.archive->raw_storage_resource() : nullptr;
  const bool journal_present = journal != nullptr, archive_present = archive != nullptr;
  Fields fields;
  fields.ref(scope.reference_);
  fields.native(journal_present);
  if (journal)
    fields.ref(journal->reference_);
  fields.native(archive_present);
  if (archive)
    fields.ref(archive->reference_);
  fields.native(catalog_entries);
  const auto request = emit(3048, fields.view(), required(bank_));
  std::optional<Error> failure;
  if (!loan.active_ || !scope.paused_ || selection.root != loan.root_ ||
      selection.owner.authority() != loan.owner_.authority() || !journal ||
      journal->authority_ != loan.owner_.authority() || journal->scope_ ||
      (archive && (archive->authority_ != loan.owner_.authority() || archive->scope_)))
    failure = Error{ErrorCode::wrong_incarnation};
  else if (catalog_entries > loan.budget_.catalog_maximum ||
           (!archive && catalog_entries != 0))
    failure = Error{ErrorCode::capacity};
  if (!failure) {
    // All old references survive until their borrowed pointers are detached and
    // the reached replacement has been named in the published request.
    auto previous = std::move(scope.selection_);
    scope.selection_ = std::move(selection);
    scope.journal_ = journal;
    scope.archive_ = archive;
    journal->scope_ = &scope;
    if (archive)
      archive->scope_ = &scope;
    scope.paused_ = false;
  } else
    scope.error(*failure);
  outcome(3049, request, failure, required(bank_));
  bank_.reset();
  scope_ = nullptr;
  return failure ? Result<void>::failure(*failure) : Result<void>::success();
}
RawStorageAction::RawStorageAction(const RawProductStorageOwner &owner,
                                   RawStorageActionKind kind,
                                   RawStorageResource *resource,
                                   std::span<const ByteView> arguments,
                                   RawStorageWriteContinuation *continuation)
    : authority_(owner.authority()), resource_(resource), continuation_(continuation) {
  if (!authority_)
    return;
  const PreserveErrno guard;
  if (!authority_->permits_origin_read())
    throw Error{ErrorCode::wrong_incarnation};
  if (resource && resource->authority_ != authority_)
    throw Error{ErrorCode::wrong_incarnation};
  if (resource && resource->scope_) {
    loan_ = &resource->scope_->loan_;
    if (kind == RawStorageActionKind::write_at) {
      if (!continuation || continuation->resource != resource || !continuation->slots ||
          !continuation->remaining_calls)
        throw Error{ErrorCode::capacity};
      --continuation->remaining_calls;
      credit_.emplace(split(*continuation->slots, write_credit));
      data_ = &required(loan_->writes_);
    } else
      credit_ = loan_->action(kind, resource == resource->scope_->archive_, data_,
                              native_read_limit_);
  } else {
    authority_->require();
    std::uint64_t amount = 42 + (resource ? 24 : 0) + 50;
    for (auto value : arguments)
      if (!add_checked(amount, value.size()))
        throw Error{ErrorCode::overflow};
    credit_.emplace(claim(amount));
  }
  const bool present = resource != nullptr;
  Fields fields;
  fields.ref(authority_->setup);
  fields.native(kind);
  fields.native(present);
  if (resource)
    fields.ref(resource->reference_);
  for (auto value : arguments)
    fields.add(value);
  reference_ = emit(3002, fields.view(), required(credit_));
  if (!reference_.ordinal)
    throw Error{ErrorCode::audit_unavailable};
}
RawStorageAction::~RawStorageAction() {
  if (!finished_ && credit_)
    outcome(3003, reference_, {}, required(credit_), 2);
}
void RawStorageAction::finish(std::optional<Error> error) noexcept {
  if (finished_)
    return;
  if (credit_)
    outcome(3003, reference_, error, required(credit_));
  finished_ = true;
}
void RawStorageAction::use(RawStorageResource *resource) {
  if (resource && resource->authority_ != authority_)
    throw Error{ErrorCode::wrong_incarnation};
  resource_ = resource;
}
void RawStorageAction::operand(std::uint8_t kind, ByteView value) {
  if (!authority_)
    return;
  const PreserveErrno guard;
  const auto length = value.size();
  auto bank = data_claim(49 + length);
  Fields fields;
  fields.ref(reference_);
  fields.native(kind);
  fields.native(length);
  fields.add(value);
  (void)emit(3004, fields.view(), bank, RawPartition::data);
}
RawStorageDiagnosticUnlink::RawStorageDiagnosticUnlink(RawStorageAction &action,
                                                       int directory,
                                                       std::string_view name)
    : authority_(action.authority_), directory_(directory), name_(name) {
  if (!authority_)
    return;
  const PreserveErrno guard;
  authority_->require();
  issuer_ = raw_current_thread();
  action_ = action.reference_;
  resource_ = action.resource_ ? action.resource_->reference() : authority_->setup;
  control_.emplace(claim(49));
  data_.emplace(data_claim(81 + name.size()));
}
RawStorageDiagnosticUnlink::~RawStorageDiagnosticUnlink() {
  if (authority_ && (raw_current_thread() != issuer_ || !authority_->permits()))
    authority_->failed_custody();
}
void RawStorageDiagnosticUnlink::run() noexcept {
  if (reached_)
    return;
  reached_ = true;
  const PreserveErrno guard;
  if (authority_ && (raw_current_thread() != issuer_ || !authority_->permits()))
    authority_->failed_custody();
  RawEventRef request;
  if (authority_) {
    const std::uint8_t method = 1;
    const int flags = 0;
    const auto length = name_.size();
    Fields fields;
    fields.ref(action_);
    fields.ref(resource_);
    fields.native(method);
    fields.native(directory_);
    fields.native(flags);
    fields.native(length);
    fields.add(bytes(name_));
    request = emit(3021, fields.view(), required(data_), RawPartition::data);
    if (!request.ordinal)
      return; // No native acquisition follows failed request publication.
  }
  const int result = ::unlinkat(directory_, name_.data(), 0);
  const int native_errno = errno;
  if (authority_) {
    Fields fields;
    fields.ref(request);
    fields.native(result);
    const bool failed = result < 0;
    fields.native(failed);
    if (failed)
      fields.native(native_errno);
    (void)emit(3022, fields.view(), required(control_));
  }
}
class RawStorageCall {
public:
  RawStorageAction &action;
  std::optional<RawControlToken> own_control;
  RawControlToken *control = nullptr;
  std::optional<RawCapacityClaim> own_data;
  RawCapacityClaim *data = nullptr;
  RawEventRef request, resource;
  RawStorageCall(RawStorageAction &selected, std::uint64_t control_bytes,
                 std::uint64_t data_bytes = 0, bool open_chain = false)
      : action(selected) {
    if (!action.enabled())
      return;
    resource =
        action.resource_ ? action.resource_->reference_ : action.authority_->setup;
    if (action.loan_) {
      control = &required(action.credit_);
      data = action.data_;
      if (control->remaining() < control_bytes ||
          (data_bytes && (!data || data->remaining() < data_bytes)))
        throw Error{ErrorCode::capacity};
    } else {
      if (open_chain && action.open_credit_ &&
          action.open_credit_->remaining() >= control_bytes)
        control = &required(action.open_credit_);
      else {
        own_control.emplace(claim(control_bytes));
        control = &required(own_control);
      }
      if (data_bytes) {
        own_data.emplace(data_claim(data_bytes));
        data = &required(own_data);
      }
    }
  }
  Fields prefix(const std::uint8_t &method) const {
    Fields f;
    f.ref(action.reference_);
    f.ref(resource);
    f.native(method);
    return f;
  }
  void start(std::uint32_t site, const Fields &fields, bool input = false) {
    if (!control)
      return;
    request = emit(site, fields.view(), input ? *data : *control,
                   input ? RawPartition::data : RawPartition::control);
    if (!request.ordinal)
      throw Error{ErrorCode::audit_unavailable};
  }
  struct NativeIntReturn {
    int value;
    int error;
  };
  void returned(std::uint32_t site, NativeIntReturn result) noexcept {
    if (!control)
      return;
    Fields fields;
    fields.ref(request);
    fields.native(result.value);
    const bool failed = result.value < 0;
    fields.native(failed);
    if (failed)
      fields.native(result.error);
    (void)emit(site, fields.view(), *control);
  }
};

int RawStorageNative::open(RawStorageAction &action, RawStorageResource *target,
                           std::optional<int> directory, std::string_view name,
                           int flags, std::optional<mode_t> mode) {
  if (action.enabled())
    action.open_credit_.emplace(claim(49 + extent_credit - 116 + 109));
  const bool present = mode.has_value();
  const auto length = name.size();
  const std::uint8_t method = directory ? 2 : 1;
  RawStorageCall call{action, 49,
                      65 + (directory ? 4 : 0) + 4 + 1 + (mode ? sizeof(mode_t) : 0) +
                          8 + length,
                      true};
  auto request = call.prefix(method);
  if (directory)
    request.native(*directory);
  request.native(flags);
  request.native(present);
  if (mode)
    request.native(*mode);
  request.native(length);
  request.add(bytes(name));
  call.start(3005, request, true);
  const int result = directory ? (mode ? ::openat(*directory, name.data(), flags, *mode)
                                       : ::openat(*directory, name.data(), flags))
                               : (mode ? ::open(name.data(), flags, *mode)
                                       : ::open(name.data(), flags));
  const int native_errno = errno;
  call.returned(3006, {.value = result, .error = native_errno});
  if (result >= 0 && target) {
    target->attach(result, call.request, action.reference_);
    action.use(target);
  }
  errno = native_errno;
  return result;
}
ssize_t RawStorageNative::read(RawStorageAction &action, int descriptor,
                               MutableByteView output, std::optional<off_t> offset) {
  std::size_t count = output.size();
  if (action.enabled())
    count =
        static_cast<std::size_t>(std::min<std::uint64_t>(count, raw_storage_io_limit));
  if (action.loan_ && count > action.native_read_limit_)
    throw Error{ErrorCode::capacity};
  const bool positioned = offset.has_value();
  const std::uint8_t method = positioned ? 2 : 1;
  RawStorageCall call{action, (positioned ? 86ULL : 78ULL) + 53, 49 + count};
  auto request = call.prefix(method);
  request.native(descriptor);
  request.native(count);
  request.native(positioned);
  if (offset)
    request.native(*offset);
  call.start(3007, request);
  const ssize_t returned = offset ? ::pread(descriptor, output.data(), count, *offset)
                                  : ::read(descriptor, output.data(), count);
  const int native_errno = errno;
  if (call.control) {
    Fields fields;
    fields.ref(call.request);
    fields.native(returned);
    const bool failed = returned < 0;
    fields.native(failed);
    if (failed)
      fields.native(native_errno);
    if (returned > 0) {
      fields.add(output.first(static_cast<std::size_t>(returned)));
      (void)emit(3008, fields.view(), *call.data, RawPartition::data);
      if (action.loan_ && action.resource_ != action.resource_->scope_->archive_)
        action.loan_->read_bytes_ -= static_cast<std::uint64_t>(returned);
    } else
      (void)emit(3008, fields.view(), *call.control);
  }
  errno = native_errno;
  return returned;
}
ssize_t RawStorageNative::write(RawStorageAction &action, int descriptor,
                                ByteView input, std::optional<off_t> offset) {
  std::size_t count = input.size();
  if (action.enabled())
    count =
        static_cast<std::size_t>(std::min<std::uint64_t>(count, raw_storage_io_limit));
  const bool positioned = offset.has_value();
  const std::uint8_t method = positioned ? 2 : 1;
  auto *continuation = action.continuation_;
  bool reference = false;
  std::size_t payload_offset = 0;
  if (continuation && continuation->input.ordinal && positioned &&
      std::less_equal<const std::byte *>{}(continuation->published.data(),
                                           input.data()) &&
      std::less_equal<const std::byte *>{}(input.data(),
                                           continuation->published.data() +
                                               continuation->published.size())) {
    payload_offset =
        static_cast<std::size_t>(input.data() - continuation->published.data());
    const auto available = continuation->published.size() - payload_offset;
    if (available) {
      count = std::min(count, available);
      reference = true;
    }
  }
  RawStorageCall call{action, reference ? 171ULL : 53ULL,
                      reference ? 0 : (positioned ? 86 : 78) + count};
  auto request = call.prefix(method);
  request.native(descriptor);
  request.native(count);
  request.native(positioned);
  if (offset)
    request.native(*offset);
  if (reference) {
    request.ref(continuation->input);
    payload_offset += 86;
    request.native(payload_offset);
    call.start(3032, request);
  } else {
    request.add(input.first(count));
    call.start(3009, request, true);
    if (continuation) {
      continuation->input = call.request;
      continuation->published = input.first(count);
      continuation->offset = offset ? static_cast<std::uint64_t>(*offset) : 0;
    }
  }
  const ssize_t returned = offset ? ::pwrite(descriptor, input.data(), count, *offset)
                                  : ::write(descriptor, input.data(), count);
  const int native_errno = errno;
  if (call.control) {
    Fields fields;
    fields.ref(call.request);
    fields.native(returned);
    const bool failed = returned < 0;
    fields.native(failed);
    if (failed)
      fields.native(native_errno);
    (void)emit(3010, fields.view(), *call.control);
  }
  errno = native_errno;
  return returned;
}
namespace {
void stat_result(RawStorageCall &call, int status, int native_errno,
                 const struct stat &s) {
  if (!call.control)
    return;
  Fields fields;
  fields.ref(call.request);
  fields.native(status);
  const bool failed = status < 0;
  fields.native(failed);
  if (failed)
    fields.native(native_errno);
  else {
    fields.native(s.st_dev);
    fields.native(s.st_ino);
    fields.native(s.st_mode);
    fields.native(s.st_uid);
    fields.native(s.st_nlink);
    fields.native(s.st_size);
  }
  (void)emit(3012, fields.view(), *call.control);
}
} // namespace
int RawStorageNative::stat(RawStorageAction &action, int descriptor,
                           struct stat &output) {
  RawStorageCall call{action, 69 + extent_credit - 185, 0, true};
  const std::uint8_t method = 1;
  auto request = call.prefix(method);
  request.native(descriptor);
  call.start(3011, request);
  const int status = ::fstat(descriptor, &output), native_errno = errno;
  stat_result(call, status, native_errno, output);
  errno = native_errno;
  return status;
}
int RawStorageNative::stat_at(RawStorageAction &action, int directory,
                              std::string_view name, struct stat &output, int flags) {
  const auto length = name.size();
  RawStorageCall call{action, extent_credit - 185, 81 + length};
  const std::uint8_t method = 2;
  auto request = call.prefix(method);
  request.native(directory);
  request.native(flags);
  request.native(length);
  request.add(bytes(name));
  call.start(3011, request, true);
  const int status = ::fstatat(directory, name.data(), &output, flags),
            native_errno = errno;
  stat_result(call, status, native_errno, output);
  errno = native_errno;
  return status;
}
uid_t RawStorageNative::uid(RawStorageAction &action) {
  RawStorageCall call{action, 109, 0, true};
  const std::uint8_t method = 1;
  auto request = call.prefix(method);
  call.start(3027, request);
  const uid_t result = ::geteuid();
  if (call.control) {
    Fields returned;
    returned.ref(call.request);
    returned.native(result);
    (void)emit(3028, returned.view(), *call.control);
  }
  return result;
}
int RawStorageNative::sync(RawStorageAction &action, int descriptor,
                           SyncStrength strength) {
#if defined(__APPLE__)
  const bool full = strength == SyncStrength::full;
  const int command = F_FULLFSYNC;
#else
  const bool full = false;
#endif
  const std::uint8_t method = full ? 2 : 1;
  RawStorageCall call{action, (full ? 75ULL : 71ULL) + 49};
  auto request = call.prefix(method);
  request.native(descriptor);
  request.native(method);
#if defined(__APPLE__)
  if (full)
    request.native(command);
#endif
  request.native(strength);
  call.start(3013, request);
#if defined(__APPLE__)
  const int result = full ? ::fcntl(descriptor, F_FULLFSYNC) : ::fsync(descriptor);
#else
  const int result = ::fsync(descriptor);
#endif
  const int native_errno = errno;
  call.returned(3014, {.value = result, .error = native_errno});
  errno = native_errno;
  return result;
}
int RawStorageNative::lock(RawStorageAction &action, int descriptor, int operation) {
  RawStorageCall call{action, 122};
  const std::uint8_t method = 1;
  auto request = call.prefix(method);
  request.native(descriptor);
  request.native(operation);
  call.start(3015, request);
  const int result = ::flock(descriptor, operation), native_errno = errno;
  call.returned(3016, {.value = result, .error = native_errno});
  errno = native_errno;
  return result;
}
int RawStorageNative::rename(RawStorageAction &action, int from_directory,
                             std::string_view from, int to_directory,
                             std::string_view to) {
  const auto from_length = from.size(), to_length = to.size();
  RawStorageCall call{action, 49, 89 + from_length + to_length};
  const std::uint8_t method = 1;
  auto request = call.prefix(method);
  request.native(from_directory);
  request.native(to_directory);
  request.native(from_length);
  request.add(bytes(from));
  request.native(to_length);
  request.add(bytes(to));
  call.start(3019, request, true);
  const int result = ::renameat(from_directory, from.data(), to_directory, to.data()),
            native_errno = errno;
  call.returned(3020, {.value = result, .error = native_errno});
  errno = native_errno;
  return result;
}
int RawStorageNative::unlink(RawStorageAction &action, int directory,
                             std::string_view name, int flags) {
  const auto length = name.size();
  RawStorageCall call{action, 49, 81 + length};
  const std::uint8_t method = 1;
  auto request = call.prefix(method);
  request.native(directory);
  request.native(flags);
  request.native(length);
  request.add(bytes(name));
  call.start(3021, request, true);
  const int result = ::unlinkat(directory, name.data(), flags), native_errno = errno;
  call.returned(3022, {.value = result, .error = native_errno});
  errno = native_errno;
  return result;
}
int RawStorageNative::mkdir(RawStorageAction &action, int directory,
                            std::string_view name, mode_t mode) {
  const auto length = name.size();
  RawStorageCall call{action, 49, 77 + sizeof(mode) + length};
  const std::uint8_t method = 2;
  auto request = call.prefix(method);
  request.native(directory);
  request.native(mode);
  request.native(length);
  request.add(bytes(name));
  call.start(3021, request, true);
  const int result = ::mkdirat(directory, name.data(), mode), native_errno = errno;
  call.returned(3022, {.value = result, .error = native_errno});
  errno = native_errno;
  return result;
}
DIR *RawStorageNative::directory(RawStorageAction &action, RawStorageResource *target,
                                 int descriptor) {
  RawStorageCall call{action, 122};
  const std::uint8_t method = 1;
  auto request = call.prefix(method);
  request.native(descriptor);
  call.start(3023, request);
  DIR *result = ::fdopendir(descriptor);
  const int native_errno = errno;
  if (call.control) {
    Fields returned;
    returned.ref(call.request);
    returned.native(result);
    const bool failed = result == nullptr;
    returned.native(failed);
    if (failed)
      returned.native(native_errno);
    (void)emit(3024, returned.view(), *call.control);
  }
  if (result && target)
    target->stream(result);
  errno = native_errno;
  return result;
}
dirent *RawStorageNative::next(RawStorageAction &action, DIR *stream) {
  constexpr auto name_offset = offsetof(dirent, d_name);
  constexpr auto name_maximum =
      std::numeric_limits<decltype(dirent::d_reclen)>::max() - name_offset;
#if defined(__APPLE__)
  constexpr std::uint64_t returned_fixed = 71;
#else
  constexpr std::uint64_t returned_fixed = 69;
#endif
  RawStorageCall call{action, 73 + 46, returned_fixed + name_maximum};
  const std::uint8_t method = 1;
  auto request = call.prefix(method);
  request.native(stream);
  call.start(3025, request);
  errno = 0;
  auto *result = ::readdir(stream);
  const int native_errno = errno;
  bool malformed = false;
  std::size_t length = 0;
  if (result) {
    const auto *name = reinterpret_cast<const char *>(result) + name_offset;
    const auto extent = result->d_reclen > name_offset ? result->d_reclen - name_offset
                                                       : std::size_t{0};
    const auto *end = std::find(name, name + extent, '\0');
    length = static_cast<std::size_t>(end - name) + (end != name + extent);
    malformed = end == name + extent;
#if defined(__APPLE__)
    malformed = malformed || result->d_namlen >= extent ||
                length != static_cast<std::size_t>(result->d_namlen) + 1;
#endif
  }
  if (call.control) {
    Fields returned;
    returned.ref(call.request);
    const bool present = result != nullptr, failed = !result && native_errno != 0;
    returned.native(present);
    returned.native(failed);
    if (failed)
      returned.native(native_errno);
    if (result) {
      returned.native(result->d_ino);
#if defined(__APPLE__)
      returned.native(result->d_seekoff);
#else
      returned.native(result->d_off);
#endif
      returned.native(result->d_reclen);
#if defined(__APPLE__)
      returned.native(result->d_namlen);
#endif
      returned.native(result->d_type);
      returned.native(length);
      returned.add(
          bytes({reinterpret_cast<const char *>(result) + name_offset, length}));
      (void)emit(3026, returned.view(), *call.data, RawPartition::data);
    } else
      (void)emit(3026, returned.view(), *call.control);
  }
  errno = native_errno;
  if (malformed)
    throw Error{ErrorCode::corrupt};
  return result;
}
std::string_view raw_storage_description() noexcept {
  return
#if defined(__APPLE__)
      "storage3000-3099 v1 Mac "
      "ABI:dev_t4,ino_t8,mode_t2,uid_t4,nlink_t2,off_t8,dir_seekoff8,reclen2,namlen2,"
      "type1;"
#else
      "storage3000-3099 v1 Linux "
      "ABI:dev_t8,ino_t8,mode_t4,uid_t4,nlink_t8,off_t8,dir_off8,reclen2,type1;"
#endif
      "E16;R=lane16,ordinalu64;size_t,ssize_t,pointer8;int4;bool,u8=1;host-native "
      "fields,no padding;"
      "3000 bool parent,[R],u8 kind;3001 R,u8 outcome,[ErrorCode,i64];"
      "3002 setupR,u8 action,bool resource,[R],actual API native arguments;3003 "
      "actionR,u8 outcome(complete0,Error1,scope_exit2),[ErrorCode,i64];"
      "3004 actionR,u8 operand,size_t length,exact original bytes;"
      "Q=actionR,resourceR,u8 method;T=requestR;"
      "3005 Q,[openat int directory],int flags,bool mode,[mode_t],size_t name,exact "
      "Cstring including NUL;3006 T,int,bool failure,[int errno];"
      "3007 Q,int fd,size_t native_count,bool positioned,[off_t];3008 T,ssize_t,bool "
      "failure,[int errno],positive-only exact bytes;"
      "3009 Q,int fd,size_t native_count,bool positioned,[off_t],exact native "
      "input;3010 T,ssize_t,bool failure,[int errno];"
      "3011 Q,int fd OR int dir,int flags,size_t name,Cstring;3012 T,int,bool "
      "failure,[int errno],success-only dev_t,ino_t,mode_t,uid_t,nlink_t,off_t;"
      "3013 Q,int fd,u8 method,[int F_FULLFSYNC],SyncStrengthu8;3014 T,int,bool "
      "failure,[int errno];"
      "3015 Q,int fd,int operation;3016 T,int,bool failure,[int errno];"
      "3017 Q,u8 kind(close1,closedir2),int fd OR DIR*;3018 T,int,bool failure,[int "
      "errno];3029 resourceR,u8 closure(native_returned0,native_failed1);"
      "3019 Q,int from_dir,int to_dir,size_t from,Cstring,size_t to,Cstring;3020 "
      "T,int,bool failure,[int errno];"
      "3021 Q,int dir,int flags OR mode_t,size_t name,Cstring;3022 T,int,bool "
      "failure,[int errno];"
      "3023 Q,int fd;3024 T,DIR*,bool failure,[int errno];3025 Q,DIR*;"
      "3026 T,bool nonnull,bool errno,[int errno],nonnull-only ino_t,d_off or "
      "d_seekoff,d_reclen,[Mac d_namlen],d_type,size_t name,Cstring;"
      "3027 Q;3028 T,uid_t;3032 Q,int fd,size_t native_count,bool "
      "positioned,off_t,prior_inputR,size_t raw_payload_offset;"
      "3033 setupR,bool parent,[R],u8 loan_kind(stop1,joined_terminal2,selected_exec3,"
      "adapter_receipt4,adapter_terminal5),u64 F,size_t K,O,G,u64 A,B,N;3034 R,u8 "
      "outcome,[ErrorCode,i64];"
      "3035 loanR,journalR,bool archive,[R];3036/3037/3038 R,u8 "
      "outcome,[ErrorCode,i64];"
      "3040 setupR,child_lane16,dispatchR,u8 kind;3041 R,u8 "
      "outcome,[ErrorCode,i64];3042 setupR,handoffR,commandR;"
      "3043 R,u8 outcome,[ErrorCode,i64];3044 R,u8 "
      "outcome(entered0,Error1,never_started2),[ErrorCode,i64];"
      "3045 product_setupR;3046 R,u8 outcome,[cleanuplane16 OR ErrorCode,i64];3047 "
      "R,u8 lifetime_close,[ErrorCode,i64];"
      "3048 scopeR,bool journal_present,[journalR],bool archive_present,[archiveR],"
      "u64 actual_catalog_entries;3049 R,u8 outcome,[ErrorCode,i64];"
      "read/write native acquisition max16MiB;loans stop/joined/receipt/terminal1 or "
      "exec3 scopes;no "
      "retries imposed by instrumentation;"
      "readdir DATA max65585,name bounded by native d_reclen and Mac d_namlen,"
      "malformed original retained then productcorrupt;diagnostic failureunlink "
      "one prepaid sameissuer3021/3022 pair beforecreation;"
      "native close/unlock singly attempted;all cleanup accounting serialized on "
      "dedicated root producer";
}
} // namespace blackbird
