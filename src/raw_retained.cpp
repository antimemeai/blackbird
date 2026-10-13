#include "blackbird/raw_retained.hpp"
#include "blackbird/archive_catalog.hpp"
#include "blackbird/retained_state.hpp"

#include <algorithm>
#include <cerrno>
#include <unistd.h>

namespace blackbird {
namespace {
thread_local unsigned char retained_issuer_thread;
struct PreserveErrno {
  int value = errno;
  ~PreserveErrno() { errno = value; }
};
using detail::raw_native_field;
RawEventRef emit(std::uint32_t site, RawEventRef parent,
                 std::span<const ByteView> arguments,
                 RawControlToken &credit) noexcept {
  const PreserveErrno guard;
  std::array<ByteView, 40> fields{};
  std::size_t count = 0;
  if (parent.ordinal) {
    fields[count++] = parent.lane;
    fields[count++] = raw_native_field(parent.ordinal);
  }
  if (arguments.size() > fields.size() - count) {
    raw_fence({ErrorCode::capacity});
    return {};
  }
  std::copy(arguments.begin(), arguments.end(),
            fields.begin() + static_cast<std::ptrdiff_t>(count));
  count += arguments.size();
  auto result =
      raw_emit(site, std::span{fields}.first(count), RawPartition::control, &credit);
  if (!result.has_value()) {
    raw_fence(result.error());
    return {};
  }
  return {raw_lane_epoch(), result.value()};
}
[[noreturn]] void custody_failure(const RawProductStorageOwner &owner,
                                  bool exclusive = false) noexcept {
  // No SPSC write through an unrelated thread's rebind of a living issuer.
  if (exclusive || owner.permits_current())
    raw_fence({ErrorCode::wrong_incarnation});
  ::_exit(74);
}
RawControlToken grant(const RawRetainedOrigin &origin, std::uint64_t amount) {
  if (!origin.loan && !origin.owner.permits_current())
    throw Error{ErrorCode::wrong_incarnation};
  if (origin.input == RawRetainedOrigin::Input::no_new && !origin.loan)
    throw Error{ErrorCode::capacity};
  auto result =
      origin.loan ? origin.loan->split(origin.root, amount) : raw_claim_control(amount);
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
} // namespace

RawRetainedSettlementLoan::RawRetainedSettlementLoan(const void *root,
                                                     RawThreadHandle issuer,
                                                     RawControlToken credit,
                                                     RawProductStorageOwner owner)
    : root_(root), issuer_(std::move(issuer)), owner_(std::move(owner)),
      credit_(std::move(credit)), original_issuer_(owner_.permits_current()),
      issuing_thread_(&retained_issuer_thread), issuing_process_(::getpid()) {}
Result<std::unique_ptr<RawRetainedSettlementLoan>>
RawRetainedSettlementLoan::prepare(const void *root, std::uint64_t control,
                                   RawProductStorageOwner owner) {
  if (!owner.enabled())
    return Result<std::unique_ptr<RawRetainedSettlementLoan>>::success({});
  if (!owner.permits_current())
    return Result<std::unique_ptr<RawRetainedSettlementLoan>>::failure(
        {ErrorCode::wrong_incarnation});
  return prepare_authorized(root, control, std::move(owner));
}
Result<std::unique_ptr<RawRetainedSettlementLoan>>
RawRetainedSettlementLoan::prepare_authorized(const void *root, std::uint64_t control,
                                              RawProductStorageOwner owner) {
  using Answer = Result<std::unique_ptr<RawRetainedSettlementLoan>>;
  const PreserveErrno guard;
  if (!raw_enabled())
    return Answer::success({});
  try {
    auto issuer = raw_current_thread();
    if (!root || !issuer || !owner.enabled() || !owner.permits_thread())
      return Answer::failure({ErrorCode::wrong_incarnation});
    auto credit = raw_claim_control(control);
    if (!credit.has_value())
      return Answer::failure(credit.error());
    return Answer::success(
        std::unique_ptr<RawRetainedSettlementLoan>{new RawRetainedSettlementLoan{
            root, std::move(issuer), std::move(credit).value(), std::move(owner)}});
  } catch (const std::bad_alloc &) {
    return Answer::failure({ErrorCode::allocation});
  }
}
RawRetainedSettlementLoan::~RawRetainedSettlementLoan() {
  if (!credit_)
    return;
  if (permits_release()) {
    credit_.reset();
    return;
  }
  if (!joined_ || !owner_.permits_thread())
    custody_failure(owner_);
  RawThreadBinding binding{issuer_};
  credit_.reset();
}
bool RawRetainedSettlementLoan::permits_current(const void *root) const noexcept {
  return root == root_ && raw_current_thread() == issuer_ && owner_.permits_thread() &&
         (joined_ || owner_.permits_current());
}
bool RawRetainedSettlementLoan::permits_release() const noexcept {
  return permits_current(root_) ||
         (original_issuer_ && raw_current_thread() == issuer_ &&
          issuing_thread_ == &retained_issuer_thread && issuing_process_ == ::getpid());
}
Result<RawControlToken> RawRetainedSettlementLoan::split(const void *root,
                                                         std::uint64_t amount) {
  if (!credit_ || !permits_current(root))
    return Result<RawControlToken>::failure({ErrorCode::wrong_incarnation});
  return credit_->split(amount);
}

RawRetainedOrigin RawRetainedReadOrigin::snapshot() const {
  if (!owner_.enabled())
    return {};
  // A foreign thread must never inspect a concurrently changed borrowed pointer.
  if (!owner_.permits_origin_read())
    throw Error{ErrorCode::wrong_incarnation};
  auto selected = origin;
  if (selected.loan ? !selected.loan->permits_current(selected.root)
                    : !selected.owner.permits_current())
    throw Error{ErrorCode::wrong_incarnation};
  return selected;
}

RawRetainedStopBundle::~RawRetainedStopBundle() {
  // The semantic bank enforces actual joined/authorizing-thread proof before this
  // binding can touch either bank. Never bind a still-live issuer speculatively.
  if (!semantic && !physical)
    return;
  verify_retirement();
  semantic.reset();
  if (physical) {
    RawThreadBinding binding{issuer};
    physical->finish();
    physical.reset();
  }
}
void RawRetainedStopBundle::verify_retirement() const noexcept {
  if (!(semantic && semantic->permits_release()) &&
      (!owner.permits_thread() ||
       (raw_current_thread() != issuer && !borrower_joined_)))
    custody_failure(owner);
}
void RawRetainedStopBundle::joined() noexcept {
  borrower_joined_ = true;
  if (semantic)
    semantic->joined();
  if (physical)
    physical->joined();
}
void RawRetainedStopBundle::started() noexcept {
  borrower_joined_ = false;
  if (semantic)
    semantic->started();
  if (physical)
    physical->started();
}
void RawRetainedStopBundle::never_started() noexcept { joined(); }

RawRetainedPair::RawRetainedPair(const RawRetainedOrigin &origin,
                                 RawRetainedPairSites sites, std::uint64_t credit,
                                 std::span<const ByteView> arguments)
    : origin_(origin), sites_(sites) {
  const PreserveErrno guard;
  if (!origin.selected && !origin.loan)
    return;
  if (!raw_enabled())
    throw Error{ErrorCode::wrong_incarnation};
  credit_.emplace(grant(origin, credit));
  request_ = emit(sites.request, origin.parent, arguments, *credit_);
  if (!request_.ordinal)
    throw Error{ErrorCode::audit_unavailable};
}
RawRetainedPair::~RawRetainedPair() { finish(3); }
RawRetainedOrigin RawRetainedPair::child() const noexcept {
  auto result = origin_;
  result.parent = request_;
  return result;
}
void RawRetainedPair::finish(std::uint8_t branch, std::span<const ByteView> fields,
                             std::optional<Error> error) noexcept {
  if (finished_)
    return;
  const PreserveErrno guard;
  finished_ = true;
  if (!credit_)
    return;
  if (origin_.loan ? !origin_.loan->permits_current(origin_.root)
                   : !origin_.owner.permits_current())
    custody_failure(origin_.owner);
  std::array<ByteView, 37> arguments{};
  if (fields.size() > arguments.size() - 3) {
    raw_fence({ErrorCode::capacity});
    return;
  }
  std::copy(fields.begin(), fields.end(), arguments.begin());
  auto count = fields.size();
  arguments[count++] = raw_native_field(branch);
  if (error) {
    arguments[count++] = raw_native_field(error->code);
    arguments[count++] = raw_native_field(error->detail);
  }
  (void)emit(sites_.returned, request_, std::span{arguments}.first(count), *credit_);
}
RawEventRef raw_retained_event(const RawRetainedOrigin &origin,
                               RawRetainedEventSite site,
                               std::span<const ByteView> fields) {
  const PreserveErrno guard;
  if (!origin.selected && !origin.loan)
    return {};
  if (!raw_enabled())
    throw Error{ErrorCode::wrong_incarnation};
  auto capacity = grant(origin, site.credit);
  auto result = emit(site.site, origin.parent, fields, capacity);
  if (!result.ordinal)
    throw Error{ErrorCode::audit_unavailable};
  return result;
}
RawRetainedEvent::RawRetainedEvent(RawRetainedOrigin origin, RawRetainedEventSite site)
    : origin_(std::move(origin)), site_(site.site) {
  if (origin_.selected || origin_.loan)
    credit_.emplace(grant(origin_, site.credit));
}
void RawRetainedEvent::publish(std::span<const ByteView> fields) noexcept {
  if (!credit_)
    return;
  if (origin_.loan ? !origin_.loan->permits_current(origin_.root)
                   : !origin_.owner.permits_current())
    custody_failure(origin_.owner);
  (void)emit(site_, origin_.parent, fields, *credit_);
  credit_.reset();
}
RawRetainedOriginScope::RawRetainedOriginScope(RawRetainedOrigin &owner,
                                               RawRetainedOrigin selected) noexcept
    : owner_(owner), previous_(owner) {
  owner_ = std::move(selected);
}
RawRetainedOriginScope::~RawRetainedOriginScope() { owner_ = previous_; }

RawStorageSelection RetainedState::raw_storage_selection() const noexcept {
  RawStorageSelection selected;
  selected.root = this;
  selected.owner = raw_storage_owner_;
  if (journal_)
    selected.journal = journal_->file_;
  if (archive_)
    selected.archive = std::shared_ptr<JournalFile>{archive_, archive_->file_.get()};
  return selected;
}

RawRetainedScope::RawRetainedScope(RetainedState &root,
                                   RawRetainedSettlementLoan &semantic,
                                   RawStorageSettlementLoan *physical,
                                   RawEventRef parent, RawRetainedOrigin::Input input)
    : selected_root_(root), semantic_(semantic) {
  if (!semantic.permits_current(&root))
    throw Error{ErrorCode::wrong_incarnation};
  if (root.query_index_)
    throw Error{ErrorCode::wrong_incarnation};
  archive_ = root.archive_;
  RawRetainedOrigin origin;
  origin.parent = parent;
  origin.selected = true;
  origin.owner = root.raw_storage_owner_;
  origin.root = &root;
  origin.loan = &semantic;
  origin.input = input;
  // The physical scope refuses an exhausted or mismatched loan before origins
  // change. It selects the files actually current at this invocation.
  if (physical)
    storage_.emplace(*physical, root.raw_storage_selection());
  root_.emplace(root.raw_origin_, origin);
  journal_.emplace(root.journal_->raw_origin_, origin);
  if (archive_)
    catalog_.emplace(archive_->raw_origin_, origin);
}
RawRetainedScope::~RawRetainedScope() {
  if (!semantic_.permits_current(&selected_root_))
    custody_failure(selected_root_.raw_storage_owner_);
  // A successful save can replace the catalog during this scope. Restore the
  // previous objects first, then remove the borrowed loan from that new catalog.
  catalog_.reset();
  journal_.reset();
  root_.reset();
  if (selected_root_.archive_ && selected_root_.archive_ != archive_)
    selected_root_.archive_->raw_origin_ = selected_root_.raw_origin_;
}
void RawRetainedScope::error(Error value) noexcept {
  if (storage_)
    storage_->error(value);
}
void RawRetainedScope::suspend() {
  if (!semantic_.permits_current(&selected_root_))
    throw Error{ErrorCode::wrong_incarnation};
  if (suspended_root_ || !selected_root_.raw_origin_.loan)
    throw Error{ErrorCode::wrong_incarnation};
  suspended_root_ = selected_root_.raw_origin_;
  suspended_journal_ = selected_root_.journal_->raw_origin_;
  suspended_archive_ = selected_root_.archive_;
  if (suspended_archive_)
    suspended_catalog_ = suspended_archive_->raw_origin_;
  storage_.reset();
  auto ordinary = *suspended_root_;
  ordinary.loan = nullptr;
  ordinary.input = RawRetainedOrigin::Input::ordinary;
  selected_root_.raw_origin_ = ordinary;
  selected_root_.journal_->raw_origin_ = ordinary;
  if (suspended_archive_)
    suspended_archive_->raw_origin_ = ordinary;
}
Result<void> RawRetainedScope::resume(RawStorageSettlementLoan *physical) {
  if (!semantic_.permits_current(&selected_root_))
    return Result<void>::failure({ErrorCode::wrong_incarnation});
  if (!suspended_root_ || !suspended_journal_)
    return Result<void>::failure({ErrorCode::wrong_incarnation});
  try {
    if (physical)
      storage_.emplace(*physical, selected_root_.raw_storage_selection());
  } catch (const Error &error) {
    return Result<void>::failure(error);
  }
  selected_root_.raw_origin_ = *suspended_root_;
  selected_root_.journal_->raw_origin_ = *suspended_journal_;
  if (selected_root_.archive_) {
    selected_root_.archive_->raw_origin_ =
        selected_root_.archive_ == suspended_archive_ && suspended_catalog_
            ? *suspended_catalog_
            : *suspended_root_;
  }
  suspended_root_.reset();
  suspended_journal_.reset();
  suspended_catalog_.reset();
  suspended_archive_.reset();
  return Result<void>::success();
}

RawRetainedExec::RawRetainedExec(RetainedState &root, RawEventRef parent, Kind kind)
    : root_(root), parent_(parent), kind_(kind),
      acknowledged_{{{root.journal_->header().environment, root.cursor().journal,
                      FrameKind::semantic, 0, 0, 0, 0, 0},
                     {root.journal_->header().environment, root.cursor().journal,
                      FrameKind::semantic, 0, 0, 0, 0, 0},
                     {root.journal_->header().environment, root.cursor().journal,
                      FrameKind::semantic, 0, 0, 0, 0, 0}}} {}
RawRetainedExec::~RawRetainedExec() { leave(); }
Result<void> RawRetainedExec::prepare(std::span<const std::vector<std::byte>> encoded) {
  if (!root_.raw_storage_owner_.enabled())
    return Result<void>::success();
  if (bundle_ || encoded.size() != 3 || encoded[2].size() != 56 ||
      encoded[0].size() > UINT32_MAX || encoded[1].size() > UINT32_MAX)
    return Result<void>::failure({ErrorCode::invalid_range});
  if (!root_.raw_storage_owner_.permits_current() || root_.query_index_)
    return Result<void>::failure({ErrorCode::wrong_incarnation});
  try {
    auto bundle = std::make_unique<RawRetainedStopBundle>();
    bundle->issuer = raw_current_thread();
    bundle->owner = root_.raw_storage_owner_;
    const auto invocation = static_cast<std::uint64_t>(encoded[1].size());
    const auto decision = static_cast<std::uint64_t>(encoded[0].size());
    RawStorageLoanBudget budget{
        RawStorageLoanKind::selected_exec,
        1324,
        12,
        3,
        24,
        root_.capacity_.max_records > UINT32_MAX / 6
            ? UINT32_MAX
            : static_cast<std::uint64_t>(root_.capacity_.max_records) * 6,
        504 + 3 * invocation + decision,
        5 +
            3 * ((invocation + 32) / raw_storage_io_limit +
                 ((invocation + 32) % raw_storage_io_limit != 0)) +
            (decision + 32) / raw_storage_io_limit +
            ((decision + 32) % raw_storage_io_limit != 0)};
    if (!full_exec())
      budget = {RawStorageLoanKind::adapter_receipt,
                420,
                4,
                1,
                4,
                budget.catalog_maximum,
                56,
                1};
    auto physical = RawStorageSettlementLoan::prepare(&root_, root_.raw_storage_owner_,
                                                      budget, parent_);
    if (!physical.has_value())
      return Result<void>::failure(physical.error());
    bundle->physical = std::move(physical).value();
    auto semantic = RawRetainedSettlementLoan::prepare(
        &root_, full_exec() ? 188868 : 33887, root_.raw_storage_owner_);
    if (!semantic.has_value())
      return Result<void>::failure(semantic.error());
    bundle->semantic = std::move(semantic).value();
    if (!full_exec()) {
      auto terminal = std::make_unique<RawRetainedStopBundle>();
      terminal->issuer = raw_current_thread();
      terminal->owner = root_.raw_storage_owner_;
      RawStorageLoanBudget terminal_budget{
          RawStorageLoanKind::adapter_terminal,
          8576,
          4,
          2,
          14,
          budget.catalog_maximum,
          2 * (invocation + 176),
          2 * (2 + (invocation + 32) / raw_storage_io_limit +
               ((invocation + 32) % raw_storage_io_limit != 0))};
      auto terminal_physical = RawStorageSettlementLoan::prepare(
          &root_, root_.raw_storage_owner_, terminal_budget, parent_);
      if (!terminal_physical.has_value())
        return Result<void>::failure(terminal_physical.error());
      terminal->physical = std::move(terminal_physical).value();
      auto terminal_semantic =
          RawRetainedSettlementLoan::prepare(&root_, 107639, root_.raw_storage_owner_);
      if (!terminal_semantic.has_value())
        return Result<void>::failure(terminal_semantic.error());
      terminal->semantic = std::move(terminal_semantic).value();
      terminal_ = std::move(terminal);
    }
    bundle_ = std::move(bundle);
    return Result<void>::success();
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
void RawRetainedExec::enter(RawEventRef parent, RawRetainedOrigin::Input input) {
  if (!bundle_)
    return;
  if (!accepted_ || scope_)
    throw Error{ErrorCode::wrong_incarnation};
  resume_error_.reset();
  scope_.emplace(root_, *bundle_->semantic, bundle_->physical.get(), parent, input);
}
void RawRetainedExec::leave() noexcept {
  terminal_scope_.reset();
  scope_.reset();
}
void RawRetainedExec::enter_terminal(RawEventRef parent) {
  if (full_exec()) {
    if (!scope_)
      enter(parent, RawRetainedOrigin::Input::no_new);
    return;
  }
  if (!terminal_)
    return;
  if (!accepted_)
    throw Error{ErrorCode::wrong_incarnation};
  if (!terminal_scope_)
    terminal_scope_.emplace(root_, *terminal_->semantic, terminal_->physical.get(),
                            parent);
}
void RawRetainedExec::suspend() {
  if (scope_)
    scope_->suspend();
}
void RawRetainedExec::resume() {
  if (!scope_)
    return;
  const auto result = scope_->resume(bundle_->physical.get());
  if (!result.has_value())
    resume_error_ = result.error();
}
Result<void> RawRetainedExec::receipt_permission() const {
  return resume_error_ ? Result<void>::failure(*resume_error_)
                       : Result<void>::success();
}
Result<std::unique_ptr<RawRetainedJobLoan>>
RawRetainedExec::prepare_job(const RawThreadHandle &child) {
  using Answer = Result<std::unique_ptr<RawRetainedJobLoan>>;
  if (!root_.raw_storage_owner_.enabled())
    return Answer::success({});
  if (!accepted_ || !full_exec())
    return Answer::failure({ErrorCode::wrong_incarnation});
  return RawRetainedJobLoan::prepare(root_, child, acknowledged_[1].payload_size,
                                     parent_);
}
RawRetainedJobLoan::RawRetainedJobLoan(RetainedState &root,
                                       std::unique_ptr<RawRetainedStopBundle> bundle,
                                       std::uint32_t invocation, RawEventRef parent)
    : root_(root), lifetime_(root.lifetime()), bundle_(std::move(bundle)),
      invocation_(invocation), parent_(parent),
      terminal_{root.journal_->header().environment,
                root.cursor().journal,
                FrameKind::semantic,
                0,
                0,
                0,
                0,
                0} {}
RawRetainedJobLoan::~RawRetainedJobLoan() { leave(); }
Result<std::unique_ptr<RawRetainedJobLoan>>
RawRetainedJobLoan::prepare(RetainedState &root, const RawThreadHandle &child,
                            std::uint32_t invocation, RawEventRef parent) {
  using Answer = Result<std::unique_ptr<RawRetainedJobLoan>>;
  if (!root.raw_storage_owner_.enabled())
    return Answer::success({});
  if (!root.raw_storage_owner_.permits_current() || !child || root.query_index_)
    return Answer::failure({ErrorCode::wrong_incarnation});
  std::unique_ptr<RawRetainedStopBundle> bundle;
  try {
    bundle = std::make_unique<RawRetainedStopBundle>();
    bundle->issuer = child;
    bundle->owner = root.raw_storage_owner_;
    RawStorageLoanBudget budget{
        RawStorageLoanKind::joined_terminal,
        static_cast<std::uint64_t>(root.journal_limits().max_payload) + 384,
        4,
        1,
        7,
        root.capacity_.max_records > UINT32_MAX / 6
            ? UINT32_MAX
            : static_cast<std::uint64_t>(root.capacity_.max_records) * 6,
        static_cast<std::uint64_t>(invocation) + 176,
        2 + (static_cast<std::uint64_t>(invocation) + 32) / raw_storage_io_limit +
            ((static_cast<std::uint64_t>(invocation) + 32) % raw_storage_io_limit !=
             0)};
    auto physical =
        root.raw_storage_owner_.prepare_joined_loan(child, &root, budget, parent);
    if (!physical.has_value())
      return Answer::failure(physical.error());
    bundle->physical = std::move(physical).value();
    bundle->never_started();
    {
      RawThreadBinding binding{child};
      auto semantic = RawRetainedSettlementLoan::prepare_authorized(
          &root, 55741, root.raw_storage_owner_);
      if (!semantic.has_value())
        return Answer::failure(semantic.error());
      bundle->semantic = std::move(semantic).value();
      bundle->semantic->never_started();
    }
    return Answer::success(std::unique_ptr<RawRetainedJobLoan>{
        new RawRetainedJobLoan{root, std::move(bundle), invocation, parent}});
  } catch (const Error &error) {
    if (bundle)
      bundle->never_started();
    return Answer::failure(error);
  } catch (const std::bad_alloc &) {
    if (bundle)
      bundle->never_started();
    return Answer::failure({ErrorCode::allocation});
  }
}
void RawRetainedJobLoan::started() noexcept { bundle_->started(); }
void RawRetainedJobLoan::joined() noexcept { bundle_->joined(); }
void RawRetainedJobLoan::never_started() noexcept { bundle_->never_started(); }
void RawRetainedJobLoan::enter(RawEventRef parent) {
  if (lifetime_.expired() || scope_)
    throw Error{ErrorCode::wrong_incarnation};
  scope_.emplace(root_, *bundle_->semantic, bundle_->physical.get(), parent);
}
void RawRetainedJobLoan::leave() noexcept { scope_.reset(); }
Result<std::unique_ptr<RawRetainedJobLoan>> RawRetainedJobLoan::renew() {
  if (lifetime_.expired())
    return Result<std::unique_ptr<RawRetainedJobLoan>>::failure(
        {ErrorCode::wrong_incarnation});
  return prepare(root_, bundle_->issuer, invocation_, parent_);
}

Result<std::unique_ptr<RawRetainedStopBundle>>
RetainedState::prepare_raw_stop(JournalCapacity credit, RawStorageHandoff *handoff,
                                RawThreadHandle issuer) {
  using Answer = Result<std::unique_ptr<RawRetainedStopBundle>>;
  if (!raw_storage_owner_.enabled())
    return Answer::success({});
  if (!raw_storage_owner_.permits_current() || query_index_)
    return Answer::failure({ErrorCode::wrong_incarnation});
  if (!issuer)
    issuer = raw_current_thread();
  std::unique_ptr<RawRetainedStopBundle> bundle;
  try {
    bundle = std::make_unique<RawRetainedStopBundle>();
    bundle->issuer = issuer;
    bundle->owner = raw_storage_owner_;
    RawStorageLoanBudget budget;
    budget.write_bytes = credit.max_file_bytes;
    budget.batches = 16;
    budget.submissions = 4;
    budget.catalog_finds = 8;
    // A persistent loan survives ordinary checkpoint replacement. Pay the
    // existing catalog format/factory bound, never the old catalog's count.
    budget.catalog_maximum =
        capacity_.max_records > UINT32_MAX / 6
            ? UINT32_MAX
            : static_cast<std::uint64_t>(capacity_.max_records) * 6;
    auto physical = handoff ? handoff->prepare_loan(this, budget, raw_origin_.parent)
                            : RawStorageSettlementLoan::prepare(
                                  this, raw_storage_owner_, budget, raw_origin_.parent);
    if (!physical.has_value())
      return Answer::failure(physical.error());
    bundle->physical = std::move(physical).value();
    {
      // Only the explicit never-started child can differ from the authorized
      // parent. The handoff admitted its physical bank under that same proof.
      RawThreadBinding binding{issuer};
      auto semantic = RawRetainedSettlementLoan::prepare_authorized(this, 78536,
                                                                    raw_storage_owner_);
      if (!semantic.has_value()) {
        bundle->never_started();
        return Answer::failure(semantic.error());
      }
      bundle->semantic = std::move(semantic).value();
    }
    return Answer::success(std::move(bundle));
  } catch (const Error &error) {
    if (bundle)
      bundle->never_started();
    return Answer::failure(error);
  } catch (const std::bad_alloc &) {
    if (bundle)
      bundle->never_started();
    return Answer::failure({ErrorCode::allocation});
  }
}

Result<void> RetainedState::restore_raw_owner() {
  if (!raw_storage_owner_.enabled())
    return Result<void>::success();
  if (!raw_storage_owner_.permits_current())
    return Result<void>::failure({ErrorCode::wrong_incarnation});
  if (settlement_credit_) {
    auto next = prepare_raw_stop(*settlement_credit_);
    if (!next.has_value()) {
      raw_owner_blocked_ = true;
      raw_origin_.input = RawRetainedOrigin::Input::no_new;
      raw_origin_.loan = nullptr;
      journal_->raw_origin_ = raw_origin_;
      if (archive_)
        archive_->raw_origin_ = raw_origin_;
      return Result<void>::failure(next.error());
    }
    raw_stop_ = std::move(next).value();
  } else {
    raw_stop_.reset();
  }
  raw_joined_owner_.reset();
  raw_owner_blocked_ = false;
  raw_origin_.input = RawRetainedOrigin::Input::ordinary;
  raw_origin_.loan = nullptr;
  journal_->raw_origin_ = raw_origin_;
  if (archive_)
    archive_->raw_origin_ = raw_origin_;
  return Result<void>::success();
}

Result<std::unique_ptr<RawRetainedTransfer>>
RetainedState::prepare_raw_transfer(const RawThreadHandle &child,
                                    RawWorkerDispatch dispatch) {
  using Answer = Result<std::unique_ptr<RawRetainedTransfer>>;
  if (!raw_storage_owner_.enabled())
    return Answer::success({});
  if (!raw_storage_owner_.permits_current())
    return Answer::failure({ErrorCode::audit_unavailable});
  if (raw_owner_blocked_) {
    auto resumed = restore_raw_owner();
    if (!resumed.has_value())
      return Answer::failure(resumed.error());
  }
  auto handoff = raw_storage_owner_.prepare_handoff(child, dispatch);
  if (!handoff.has_value())
    return Answer::failure(handoff.error());
  std::unique_ptr<RawRetainedStopBundle> candidate;
  if (settlement_credit_) {
    auto prepared = prepare_raw_stop(*settlement_credit_, handoff.value().get(), child);
    if (!prepared.has_value())
      return Answer::failure(prepared.error());
    candidate = std::move(prepared).value();
  }
  try {
    return Answer::success(std::unique_ptr<RawRetainedTransfer>{new RawRetainedTransfer{
        *this, child, std::move(handoff).value(), std::move(candidate)}});
  } catch (const std::bad_alloc &) {
    if (candidate)
      candidate->never_started();
    return Answer::failure({ErrorCode::allocation});
  }
}

RawRetainedTransfer::RawRetainedTransfer(
    RetainedState &root, RawThreadHandle child,
    std::unique_ptr<RawStorageHandoff> storage,
    std::unique_ptr<RawRetainedStopBundle> candidate)
    : root_(root), parent_(raw_current_thread()), child_(std::move(child)),
      storage_(std::move(storage)), candidate_(std::move(candidate)),
      parent_thread_(&retained_issuer_thread), parent_process_(::getpid()) {}
RawRetainedTransfer::~RawRetainedTransfer() {
  if (launched_ && !joined_)
    custody_failure(root_.raw_storage_owner_,
                    raw_current_thread() == parent_ &&
                        parent_thread_ == &retained_issuer_thread &&
                        parent_process_ == ::getpid());
  if (!launched_)
    rollback_launch();
}
void RawRetainedTransfer::stage_before_launch() noexcept {
  if (staged_ || launched_ || raw_current_thread() != parent_ ||
      !root_.raw_storage_owner_.permits_current())
    custody_failure(root_.raw_storage_owner_);
  previous_origin_ = root_.raw_origin_;
  previous_ = std::move(root_.raw_stop_);
  root_.raw_stop_ = std::move(candidate_);
  staged_ = true;
}
void RawRetainedTransfer::commit_launch() noexcept {
  if (!staged_ || launched_ || raw_current_thread() != parent_)
    custody_failure(root_.raw_storage_owner_);
  launched_ = true;
  // The child may already own lexical product authority. The old MAIN grants
  // retire solely under their immutable original issuing-thread proof; MAIN was
  // not joined, and this release grants no ordinary Root access.
  previous_.reset();
}
void RawRetainedTransfer::rollback_launch() noexcept {
  if (launched_)
    custody_failure(root_.raw_storage_owner_);
  if (staged_) {
    candidate_ = std::move(root_.raw_stop_);
    root_.raw_stop_ = std::move(previous_);
    root_.raw_origin_ = previous_origin_;
    staged_ = false;
  }
  if (candidate_)
    candidate_->never_started();
  candidate_.reset();
  if (storage_)
    storage_->finish();
}
Result<void> RawRetainedTransfer::joined() {
  if (!launched_ || joined_ || raw_current_thread() != parent_ ||
      !root_.raw_storage_owner_.permits_current())
    return Result<void>::failure({ErrorCode::wrong_incarnation});
  joined_ = true;
  if (root_.raw_stop_)
    root_.raw_stop_->joined();
  root_.raw_joined_owner_ = child_;
  return root_.restore_raw_owner();
}
std::string_view raw_retained_description() noexcept {
  return R"raw(BBRAWABI v1 native64 little-endian. Raw envelope length:u32/site:u32/ordinal:u64.
Ref=lane16+ordinal8. Request has actual originating Ref only if owned; request native fields follow.
Return has its own actual request Ref, then reached native fields, branch:u8, and owned ErrorCode:u8/detail:i64 only on branch1/handled5.
Branches: success0, owned Error1, thrown allocation2 without invented Error, unknown exception/unfinished3, optional absent4, handled owned Error5.
Native wrappers preserve thrown exceptions versus business Result failures. No padded structs.
Header: environment16/journal16/issuer:u64/payload:u32/batch:u32/predecessor:bool/[journal16/validated_sequence:u64/validated_end_offset:u64].
Cursor: journal16/sequence:u64/end_offset:u64. Page: offset:u64/generation:u64/checksum:u32.
Physical: environment16/journal16/kind:u16/sequence:u64/batchfirst:u64/payload_offset:u64/payload_size:u32/frame_checksum:u32.
Selector: environment16/root16/active16/generation:u64. Application/operation typed identities16. Source/Record: journal16/sequence:u64.
Error fields exist only when an actual native Error is owned. Optional metadata is omitted, never fabricated zero.
Head factory and member return omit attempted when no transaction outflag is owned; actual transaction3248 owns attempted/published/intransaction.
Slot loaders4 absent or5 owned handled error preserve actual ignored business outcome. Checked payload methods1 direct2 archive3 lazy; archive/lazy return omit writer state not owned by that closure.
Frame3162 copies the same native Physical fields at actual prepared/parsed/checkpoint selection.
|3100 append args|expectedcursor32/S8/E8/diagnostic1/settlement1/protectedpresence1/ownedcredit16|107|
|3101 append custody|actualcursor32/prepared1/pending1/protectedpresence1/ownedremainingcredit16|91|
|3102 pending original|actual ProvisionalCapture origin16/first8/length8/purpose2|74|
|3103 prepared/published selection|actual stage1/counter8/queryroot20/prepared1/pending1|71, max2 perappend|
|3104/3105 Retained create/open|request create Header81/cap16/method1; open also use_scan1/allow_full_replay1/max_full_replay_bytes8; return cursor32/state1/usedhint1/compact1/reconciled1/branch1/ownedError9|maximum148+86=234|
|RetainedState indexed attachment3106/3107|maxbytes8; result actualroot20/attached1/branch1/error9|48+71=119 plus actual populate children|
|RetainedState actual owner teardown3108/3109|currentcursor32; reachedwriterstate1/pending1/prepared1/branch1/error9|72+53=125, separately held before construction; no ordinary queries on cleanup issuer|
|3110/3111 save_current_state|request cursor32/savedpresence1/ownedslot4/compact1; return selectedboundary32/slot4/archived_facts8/archived_records8/proposed_selection_accepted1/branch1/ownedError9|78+103=181|
|3112 actual encoder header selection|already computed native kind2/schema8/dependencies.size8|58|
|3113 actual decoder header selection|existing decoder.number locals schema8/kind8/count8|64|
|3114 source staged|actual SourceRef24/payload_offset8/payload_size4|76|
|3115/3116 apply|request actualrecord24/evidence1/dependencies.size8; return branch1/ownedError9/root20/counter8|73+78=151|
|3117 actual dependency|actual SourceRef24 at existing dependency loop|64|
|3118 actual existing/uncertain match|actual RecordRef24/evidence1 on successful existing()/existing_uncertain() result only; never generic lookup or public inspection|65|
|3119/3120 submit|request actual settlement1; return existing1/ownedRef24/evidence1/branch1/ownedError9|41+76=117|
|3121/3122 retain rejection|request expectedcursor32/S8/E8/actualreason9; return prefixretained1/pending1/state1/branch1/ownedError9|97+53=150|
|3123 actual source group|existing offset8/count8|56|
|3124/3125 reserve identity|request namespace8/cursor8/limit8/highwater8/state1; return issuedID16/namespace8/cursor8/limit8/branch1/ownedError9|73+90=163|
|protect/refresh settlement3126/3127|actualcredit16; return actualpresent1/credit16/branch1/error9|56+67=123; protect successful scope also holds release3128/3129=40+50=90|
|RetainedState replay3130/3131|currentprefix32/captureonly.size8/maintenanceend8; result root20/counter8/recordingfailed1/prepared1/reconciled1/branch1/error9|88+81=169 plus actual batches/reads/apply|
|replay batch3132/3133|batchfirst8/actualfirstindex8/endindex8/rollbackcounter8/root20; return actualaccepted1/counter8/root20/facts.size8/sources.size8/branch1/error9|92+95=187 plus children|
|confirm/reconcile3134/3135|currentcursor32/currentreconciled1; result cursor32/state1/reconciled1/recordingfailed1/branch1/error9|73+85=158 plus staged replay/verifier|
|actual verifier3136/3137|actual attempts.size8; native Result branch/error|48+50=98; actual each selected AttemptState metadata separately before callback|
|3138/3139 decision getter|request actualID16; return sixIDs96/planned.size8/continuation.size8/branch1/ownedError9|56+162=218|
|3140/3141 lookup|actual family1/kind2/identity16/second16; return actualRef24/evidence1/branch1/ownedError9|75+75=150|
|3142/3143 attempt getter|request actualattemptID16; return Admission IDs48/input.size8/input_from_invocation1, opened1, observationpresence1 and actualphase1/disposition1/size8 when owned, receiptpresence1 and failurepresence1/actualError9 when owned, evidence1/reconcile1, branch1/ownedouterError9|56+132=188|
|fact/factpage3144/3145|firstordinal8/actual limit8; native branch/error and actual selected result count8|56+58=114; per actual returned factRef24/evidence1 =65|
|history snapshot3146/3147|nativecount8/savedpresence1/currentboundary32; native branch/error and actual selected boundary32/tail.size8|81+90=171; closure owns file/catalog/root authority, not dead RetainedState|
|history reader invocation3148/3149|actual creationR24/ordinal8; native branch/error with factRef24/evidence1|72+75=147, then actual catalog/packet children; bounded page<=16|
|source resolver3150/3151|actualSourceRef24; native branch/error and actualreturnedbytecount8|64+58=122 plus selected descriptor/packet children|
|retained dispatch3152/3153|actualattempt16; returneddispatched1/effectbranch1/effectError9/recordingbranch1/recordingError9/outerbranch1/outerError9|56+71=127; deactivate selected cleanup loans at real body boundary, restore before receipt|
|3154 actual public fact/page selection|actualRef24/evidence1 on successful fact() or FactHistory::page() result selection only, associated with that public request; never existing()/existing_uncertain()/lookup()/history closure construction|65 per actual public result; ZERO in selected stop/Job/receipt/exec topology|
|3156/3157 unresolved attempts|request actualcursor32; return result.size8/branch1/ownedError9|72+58=130|
|3158/3159 invocation getter|request ID16; return threeIDs48/input.size8/branch1/ownedError9|56+106=162|
|3160/3161 framed append|drafts.size8/currentcursor32/state1; return actualcursor32/state1/branch1/ownedError9|81+83=164|
|3162 actual prepared/parsed/hint frame|already-owned PhysicalJournalRecord66|106|
|3163 actual commit|existing first8/drafts.size8/encoded.size8/actualbusinesscrc4|68|
|3164/3165 actual checked packet|record66/boundary32/method1; direct return acquiredvector.size8 only on success, ownedstate1 on both Result branches; archive/lazy omit unowned state; branch1/ownedError9|139+59=198|
|3166/3167 Framed create/open|request create Header81/cap16/strength1/method1; open also use_scan1/resumepresence1/ownedresume(cursor32/indexed_records8/crc4)/selectorpresence1; return cursor32/state1/usedhint1/archived_records8/branch1/ownedError9|maximum186+92=278|
|FramedJournal scan/restage3168/3169|actualcursor32/propagatenativereaderrors1/inrestage1; return actualcursor32/state1/diagnostic_offset8/available_end8/problem presence1/problem9|74+109=183 plus actual frame reads/parse selections|
|selected recovered prefix3170/3171|actualpredecessor32; result actualcursor32/state1/branch1/error9|72+83=155|
|physical reject batch3172/3173|actualbatchfirst8/actualreason9; result actualcursor32/state1/diagnostic_offset8/available_end8/branch1/error9|57+99=156|
|3174/3175 reader creation, two native variants|archive: request boundary32, return created1/state1/branch1/ownedError9; payload: request actualrecord66/boundary32/method1, return created1/branch1/ownedError9|archive72+52=124 /payload139+51=190|
|original range3176/3177|actualoffset8/requestedlength8; return nativeobservedextent8/actualreturnedbytes.size8/branch1/error9|56+66=122; acquired original bytes already physical capture|
|3178/3179 Framed confirm/reconcile|request cursor32/state1/actual-called-method1; return cursor32/state1/diagnostic_offset8/available_end8/problem_presence1/ownedproblem9/branch1/ownedAPIError9|74+109=183|
|RecoveryIndex open3180/3181|maxbytes8; result actualend8/branch1/error9|48+58=106|
|RecoveryIndex find3182/3183|actualroot20/key40; result found1/actualvalue8/branch1/error9|100+59=159 plus reached page/selection children|
|RecoveryIndex put3184/3185|actualroot20/key40/value8; result actualnewroot20/branch1/error9|108+70=178 plus reached page writes/splits|
|RecoveryIndex read3186/3187|actualPageRef20; nativeleaf1/decodedcount8/branch1/error9|60+59=119; raw native page bytes belong physical capture|
|RecoveryIndex write3188/3189|actualend8/nodeleaf1/entries.size8/children.size8; actualRef20/newend8/branch1/error9|65+78=143 plus actual address burn68 paid before write|
|RecoveryIndex actualselected entry3190 / actualchild3191 / actualsplit3192|nativeentry key40/value8; childPageRef20; splitseparator key40/value8/left20/right20|88 /60 /128; copy reached existing product values|
|RecoveryIndex range3193/3194|actualroot20/lowerkey40/limit8; actual remaininglimit8/branch1/error9|108+58=166 plus actual page/callback children|
|RecoveryIndex callback3195/3196|actualentry key40/value8; actual bool1/branch1/error9|88+51=139; prepay before callback, stop does no extra read|
|RecoveryIndex sync3197/3198|nativeactualend8; nativeResult branch/error|48+50=98 plus native sync|
|3199 actual index address burn|already-created actualPageRef20/reachednewend8 BEFORE native write, prepaid before end_ mutation|68|
|3200/3201 catalog publish/merge|request Header81/boundary32/input.size8/previouspresence1/ownedpreviouscount8; result written_count8/branch1/ownedError9|170+58=228|
|ArchiveCatalog open3202/3203|Header81/boundary32/maximum_entries8; result actualcount8/branch1/error9|161+58=219; file native header/extent/read physical capture|
|3204/3205 catalog find|actual key40/boundary32; actualowned PhysicalRecord66/branch1/ownedError9|112+116=228|
|3206/3207 catalog entry|actual ordinal8; actualkey40/PhysicalRecord66/branch1/ownedError9|48+156=204|
|ArchiveCatalog selected output3208|actual outputordinal8/key40/PhysicalRecord66|154 before existing encoder/write, no metrics traversal|
|saved load3220/3221|actual journalcursor32/capacity16/beforeScan1; return nativeboundary32/slot4/factcount8/issuer8/unresolved.size8/branch1/error9|89+110=199, plus at most two slot calls|
|saved slot3222/3223|actualslot4/beforeScan1; same selected return fields|45+110=155; actual swallowed native errors recorded on that branch before ignored return|
|saved publish3224/3225|actualsavedboundary32/factcount8/issuer8/slot4/unresolved.size8; return actualboundary32/slot4/branch1/error9|100+86=186 plus real packet/header/anchor/native effects|
|saved root selection3226|actualboundary32/slot4/factcount8/issuer8/unresolved.size8|100, at actual selection before native in-memory swap/retirement|
|saved unresolved record3227|actual factRef24/evidence1|65, in existing selected unresolved loop only|
|scan checkpoint load3228/3229|actualslot4; result actualcursor32/records.size8/slot4/branch1/error9|44+94=138; actual record children106 at existing parse loop|
|scan checkpoint publish3230/3231|actualcursor32/records.size8/checkpoint_attempt8/strength1; selected slot is not yet owned at entry, omitted; return actualcursor32/used1/checkpoint_attempt8/[actual selectedslot4]/branch1/error9|93+95=188 plus bounded existing records/native publication|
|3232/3233 actual resident retirement|BEFORE actual clear: boundary32/facts.size8/sources.size8/physicalrecords.size8; AFTER actual clear: archived_facts8/archived_records8/journal_archived_records8/tail_facts.size8/tail_sources.size8/physicalrecords.size8|96 before /88 after|
|head prepare/open3240/3241|actualHeader81/strength1; result actualselector56/healthy1/initialprepared1/attempted1/branch1/error9|122+109=231|
|head publish-initial3242/3243|actualselector56/strength1; same reached return fields|97+109=206|
|head replace3244/3245|actualexpectedselector56/nextID16/strength1; same actualselected return fields|113+109=222|
|3246/3247 Head read_selector|request expectedroot environment16/journal16; return actualselector56/branch1/ownedError9|72+106=178|
|head reached transaction3248|actualhealthy1/initialprepared1/attempted1/published1/intransaction1/selection56|101, before actual attempted effect and final state uses held bank|
|3250/3251 Head write_selector|request actualselector56/strength1; return actual existing attempted outflag1/branch1/ownedError9|97+51=148|
|3252/3253 current_programs|request savedpresence1/ownedboundary32; return actualpackets.size8/branch1/ownedError9|73+58=131|
|RetainedEnvironment create/open3260/3261|actualHeader81/rootcapacity16/bounds48; return actualcursor32/state1/headselector56/busy1/continuationrequired1/branch1/error9|185+141=326 plus actual chain/header/journal children|
|environment submit/proposal3262/3263|actualexpectedcursor32/sources.size8/events.size8; result actualcursor32/existing1/capturepresence1/actualcapture34/events.size8/branch1/error9|88+126=214; each real result Submission66 if needed|
|environment selected segment/header3264/3265|actualjournalID16; result actualHeader81/branch1/error9|56+131=187 plus native header inspection|
|environment selected continuation3266|actualoriginHeader81/originpolicy16/selectedpredecessor32|169, copy values existing selected load logic already owns|
|environment maintenance replay3267/3268|actualHeader81/originpolicy16; return actualend8/captureonly.size8/queryroot20/counter8/branch1/error9|137+94=231 plus selected children|
|environment stage original3269/3270|actualmarkerRef24/ProvisionalCapture34/originHeader81/policy16; return actualnativecapturefirst8/length8/branch1/error9|195+66=261 plus actual selected capture chunks|
|environment provisional capture3271|actualcaptureRef24/actualnativeoffset8/nativepayload_size4/actualpurpose2|82, at existing chunk selection, bytes are physical acquired originals|
|environment confirm/reconcile3272/3273|actualcursor32/state1; result actualcursor32/headhealthy1/continuationrequired1/branch1/error9|73+84=157 plus historical journals/verifier/semanticchildren|
|3286 actual selected AttemptState/verifier input|native82-byte maximum state, copied in actual existing loop, no extra iteration|122|
)raw";
}
} // namespace blackbird
