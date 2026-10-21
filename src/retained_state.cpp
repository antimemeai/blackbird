#include "blackbird/retained_state.hpp"
#include "blackbird/archive_catalog.hpp"
#include "blackbird/context.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/retained_proposal.hpp"
#include "blackbird/saved_state.hpp"
#include "blackbird/tasks.hpp"
#include <charconv>
#include <map>
#include <set>

#include <algorithm>
#include <chrono>
#include <type_traits>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>

namespace blackbird {
namespace {
template <typename Resolve>
void resolve_admission_input(RetainedEvent &event, Resolve resolve) {
  auto *admission = std::get_if<AttemptAdmissionEvent>(&event.body);
  if (!admission || !admission->input_from_invocation)
    return;
  auto invocation = resolve(admission->invocation);
  if (!invocation || invocation->invocation != admission->invocation)
    throw Error{ErrorCode::corrupt};
  if (!admission->input.empty() && admission->input != invocation->input)
    throw Error{ErrorCode::conflict};
  admission->input = invocation->input;
}
struct EventKey {
  RetainedKind kind;
  IdentityBytes identity;
  bool operator==(const EventKey &) const = default;
};
std::optional<EventKey> key(const RetainedBody &body,
                            std::uint64_t issuer_namespace = 0) {
  return std::visit(
      [&](const auto &event) -> std::optional<EventKey> {
        using T = std::decay_t<decltype(event)>;
        const auto kind = retained_kind(body);
        if constexpr (std::is_same_v<T, DecisionEvent>) {
          return EventKey{kind, event.decision.bytes()};
        } else if constexpr (std::is_same_v<T, InvocationEvent>) {
          return EventKey{kind, event.invocation.bytes()};
        } else if constexpr (std::is_same_v<T, AttemptAdmissionEvent> ||
                             std::is_same_v<T, AttemptOpenEvent> ||
                             std::is_same_v<T, RetryEvent> ||
                             std::is_same_v<T, AdapterReceiptEvent>) {
          return EventKey{kind, event.attempt.bytes()};
        } else if constexpr (std::is_same_v<T, ComplaintEvent>) {
          return EventKey{kind, event.complaint.bytes()};
        } else if constexpr (std::is_same_v<T, ApplicationRecordEvent>) {
          return EventKey{kind, event.identity.bytes()};
        } else if constexpr (std::is_same_v<T, IssuerReservationEvent>) {
          IdentityBytes bytes{};
          auto counter = event.counter;
          for (std::size_t index = 0; index < 8; ++index) {
            bytes[index] = static_cast<std::byte>(issuer_namespace & 255);
            issuer_namespace >>= 8;
          }
          for (std::size_t index = 8; index < 16; ++index) {
            bytes[index] = static_cast<std::byte>(counter & 255);
            counter >>= 8;
          }
          return EventKey{kind, bytes};
        } else {
          return std::nullopt;
        }
      },
      body);
}
RecoveryKey query_key(unsigned char family, RetainedKind kind,
                      const IdentityBytes &identity = {},
                      const IdentityBytes &second = {}) {
  RecoveryKey result{};
  result[0] = static_cast<std::byte>(family);
  result[1] = static_cast<std::byte>(kind);
  std::copy(identity.begin(), identity.end(), result.begin() + 2);
  std::copy(second.begin(), second.end(), result.begin() + 18);
  return result;
}
RecoveryKey ordinal_key(unsigned char family, const IdentityBytes &identity,
                        std::uint64_t ordinal) {
  RecoveryKey result{};
  result[0] = static_cast<std::byte>(family);
  std::copy(identity.begin(), identity.end(), result.begin() + 2);
  for (std::size_t i = 0; i < 8; ++i) {
    result[25 - i] = static_cast<std::byte>(ordinal & 255);
    ordinal >>= 8;
  }
  return result;
}
struct FactPosition {
  std::uint64_t issuer_namespace;
  std::size_t ordinal;
};
std::vector<RecoveryKey> fact_keys(const RetainedFact &fact, FactPosition position) {
  std::vector<RecoveryKey> result;
  if (const auto identity = key(fact.event.body, position.issuer_namespace))
    result.push_back(query_key(1, identity->kind, identity->identity));
  result.push_back(ordinal_key(6, {}, position.ordinal));
  std::visit(
      [&](const auto &body) {
        using T = std::decay_t<decltype(body)>;
        const auto kind = retained_kind(fact.event.body);
        if constexpr (std::is_same_v<T, AttemptObservationEvent>) {
          result.push_back(query_key(2, kind, body.attempt.bytes()));
          if (body.phase == AttemptPhase::terminal)
            result.push_back(query_key(1, kind, body.attempt.bytes()));
        }
        if constexpr (std::is_same_v<T, AttemptAdmissionEvent>)
          result.push_back(query_key(3, kind, body.invocation.bytes()));
        if constexpr (std::is_same_v<T, RetryEvent>)
          result.push_back(
              query_key(4, kind, body.decision.bytes(), body.invocation.bytes()));
        if constexpr (std::is_same_v<T, AttemptOpenEvent> ||
                      std::is_same_v<T, AttemptObservationEvent> ||
                      std::is_same_v<T, AdapterReceiptEvent>)
          result.push_back(ordinal_key(9, body.attempt.bytes(), position.ordinal));
      },
      fact.event.body);
  return result;
}
template <typename T, typename Predicate>
const T *find_event(const SharedSequence<RetainedFact> &facts, Predicate predicate) {
  for (auto it = facts.rbegin(); it != facts.rend(); ++it) {
    if (const auto *event = std::get_if<T>(&it->event.body);
        event && predicate(*event)) {
      return event;
    }
  }
  return nullptr;
}
bool plans(const DecisionEvent &decision, InvocationId invocation) {
  return std::find(decision.planned_invocations.begin(),
                   decision.planned_invocations.end(),
                   invocation) != decision.planned_invocations.end();
}
class TransactionGuard {
public:
  explicit TransactionGuard(bool &active) : active_(active) { active_ = true; }
  ~TransactionGuard() { active_ = false; }
  TransactionGuard(const TransactionGuard &) = delete;
  TransactionGuard &operator=(const TransactionGuard &) = delete;

private:
  bool &active_;
};
} // namespace

struct RetainedState::ReplayIndex {
  struct FactHash {
    std::size_t operator()(const EventKey &k) const noexcept {
      std::size_t h = static_cast<std::size_t>(k.kind);
      for (auto byte : k.identity)
        h = (h * 131) ^ std::to_integer<unsigned char>(byte);
      return h;
    }
  };
  struct SourceHash {
    std::size_t operator()(const SourceReference &k) const noexcept {
      std::size_t h = static_cast<std::size_t>(k.sequence);
      for (auto byte : k.journal.bytes())
        h = (h * 131) ^ std::to_integer<unsigned char>(byte);
      return h;
    }
  };
  std::unordered_map<EventKey, std::size_t, FactHash> facts;
  std::unordered_set<SourceReference, SourceHash> sources;
};
void RetainedState::Snapshot::swap(Snapshot &other) noexcept {
  std::swap(archived_facts, other.archived_facts);
  std::swap(archived_records, other.archived_records);
  std::swap(query_root, other.query_root);
  facts.swap(other.facts);
  sources.swap(other.sources);
  uncertain_facts.swap(other.uncertain_facts);
  provisional_originals.swap(other.provisional_originals);
  std::swap(issuer_namespace, other.issuer_namespace);
  std::swap(counter, other.counter);
}
RetainedState::~RetainedState() {
  if (raw_storage_owner_.enabled() && !raw_storage_owner_.permits_current())
    ::_exit(74);
  if (raw_stop_)
    raw_stop_->verify_retirement();
  // Surviving file readers retain authority and actual file identity, never a
  // borrowed stack loan or a pointer they may dereference into this Root.
  if (journal_)
    journal_->raw_origin_.loan = nullptr;
  if (archive_)
    archive_->raw_origin_.loan = nullptr;
  if (raw_end_ && journal_) {
    const auto final_cursor = cursor();
    const auto final_state = state();
    const bool pending = pending_proposal_.has_value();
    const bool prepared = prepared_.has_value();
    raw_end_->finish(final_cursor, final_state, pending, prepared);
  }
}
RetainedState::RetainedState(std::unique_ptr<JournalDirectory> directory,
                             JournalCapacity capacity)
    : saved_load_(std::make_unique<SavedStateLoad>()), directory_(std::move(directory)),
      capacity_(capacity), max_history_entries_(capacity.max_records) {
  committed_.facts.reserve(std::min<std::size_t>(capacity.max_records, 64));
  committed_.sources.reserve(std::min<std::size_t>(capacity.max_records, 64));
}
Result<std::unique_ptr<RetainedState>>
RetainedState::allocate(std::unique_ptr<JournalDirectory> directory,
                        JournalCapacity capacity) {
  if (!directory || capacity.max_records == 0 ||
      capacity.max_records > std::vector<RetainedFact>{}.max_size() ||
      capacity.max_records > std::vector<PhysicalJournalRecord>{}.max_size()) {
    return Result<std::unique_ptr<RetainedState>>::failure({ErrorCode::invalid_range});
  }
  try {
    return Result<std::unique_ptr<RetainedState>>::success(
        std::unique_ptr<RetainedState>{
            new RetainedState{std::move(directory), capacity}});
  } catch (const std::bad_alloc &) {
    return Result<std::unique_ptr<RetainedState>>::failure({ErrorCode::allocation});
  }
}
Result<std::unique_ptr<RetainedState>>
RetainedState::create(std::unique_ptr<JournalDirectory> directory,
                      std::string_view name, JournalHeader header,
                      JournalCapacity capacity, RawProductStorageOwner raw_owner) {
  RawRetainedOrigin factory_origin;
  factory_origin.selected = raw_owner.enabled();
  factory_origin.owner = raw_owner;
  factory_origin.parent = raw_owner.reference();
  const std::uint8_t method = 1;
  RawRetainedFields arguments;
  arguments.header(header);
  arguments.native(capacity.max_file_bytes);
  arguments.native(capacity.max_records);
  arguments.native(method);
  RawRetainedPair trace{factory_origin, {3104, 3105}, 234, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::unique_ptr<RetainedState>> {
    // The linked-chain factory will supply validated predecessors. This root
    // factory must not accidentally publish a standalone continuation as history.
    if (header.predecessor) {
      return Result<std::unique_ptr<RetainedState>>::failure({ErrorCode::unsupported});
    }
    auto allocated = allocate(std::move(directory), capacity);
    if (!allocated.has_value()) {
      return allocated;
    }
    auto owner = std::move(allocated).value();
    owner->raw_storage_owner_ = std::move(raw_owner);
    owner->raw_origin_ = trace.child();
    owner->raw_origin_.root = owner.get();
    auto end =
        RawRetainedOwnerEnd::prepare(owner->raw_storage_owner_, trace.reference());
    if (!end.has_value())
      return Result<std::unique_ptr<RetainedState>>::failure(end.error());
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    if (now >= 0)
      (void)owner->expire_diagnostics(static_cast<std::uint64_t>(now));
    auto journal = FramedJournal::create(*owner->directory_, name, header, capacity,
                                         SyncStrength::full, owner->raw_origin_);
    if (!journal.has_value()) {
      return Result<std::unique_ptr<RetainedState>>::failure(journal.error());
    }
    owner->journal_ = std::move(journal).value();
    owner->journal_name_ = name;
    owner->journal_->raw_origin_ = owner->raw_origin_;
    owner->committed_.issuer_namespace = header.issuer_namespace;
    owner->pending_namespace_ = header.issuer_namespace;
    owner->raw_end_ = std::move(end).value();
    return Result<std::unique_ptr<RetainedState>>::success(std::move(owner));
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &selected = *answer.value();
    const auto current = selected.cursor();
    const auto state = selected.state();
    const bool hint = selected.used_scan_checkpoint();
    returned.cursor(current);
    returned.native(state);
    returned.native(hint);
    returned.native(selected.compact_);
    returned.native(selected.reconciled_);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
void RetainedState::note_catalogue_fault(unsigned slot, Error error) {
  auto &faults = saved_load_->catalogue_faults;
  if (std::none_of(faults.begin(), faults.end(),
                   [&](const auto &fault) { return fault.first == slot; }))
    faults.emplace_back(slot, error);
}
Result<std::unique_ptr<RetainedState>>
RetainedState::open(std::unique_ptr<JournalDirectory> directory, std::string_view name,
                    JournalHeader header, JournalCapacity capacity,
                    bool use_scan_checkpoint, bool allow_full_replay,
                    std::uint64_t max_full_replay_bytes,
                    RawProductStorageOwner raw_owner) {
  RawRetainedOrigin factory_origin;
  factory_origin.selected = raw_owner.enabled();
  factory_origin.owner = raw_owner;
  factory_origin.parent = raw_owner.reference();
  const std::uint8_t method = 2;
  RawRetainedFields arguments;
  arguments.header(header);
  arguments.native(capacity.max_file_bytes);
  arguments.native(capacity.max_records);
  arguments.native(method);
  arguments.native(use_scan_checkpoint);
  arguments.native(allow_full_replay);
  arguments.native(max_full_replay_bytes);
  RawRetainedPair trace{factory_origin, {3104, 3105}, 234, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::unique_ptr<RetainedState>> {
    if (header.predecessor) {
      return Result<std::unique_ptr<RetainedState>>::failure({ErrorCode::unsupported});
    }
    auto allocated = allocate(std::move(directory), capacity);
    if (!allocated.has_value()) {
      return allocated;
    }
    auto owner = std::move(allocated).value();
    owner->raw_storage_owner_ = std::move(raw_owner);
    owner->raw_origin_ = trace.child();
    owner->raw_origin_.root = owner.get();
    auto end =
        RawRetainedOwnerEnd::prepare(owner->raw_storage_owner_, trace.reference());
    if (!end.has_value())
      return Result<std::unique_ptr<RetainedState>>::failure(end.error());
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    if (now >= 0)
      (void)owner->expire_diagnostics(static_cast<std::uint64_t>(now));
    FramedJournal::ResumeSelector selector;
    if (!use_scan_checkpoint || !allow_full_replay ||
        max_full_replay_bytes != UINT64_MAX)
      selector = [&](FramedJournal &locked) -> Result<std::optional<JournalResume>> {
        const bool replay_allowed =
            allow_full_replay &&
            locked.recovery_report().available_end <= max_full_replay_bytes;
        auto saved = load_saved_state(*owner->directory_, name, locked, capacity, true,
                                      owner->raw_origin_, owner->saved_load_.get());
        if (!saved.has_value())
          return Result<std::optional<JournalResume>>::failure(saved.error());
        auto &saved_value = saved.value();
        if (saved_value) {
          auto catalog = ArchiveCatalog::open(
              *owner->directory_,
              std::string{name} + ".archive." +
                  std::to_string(saved_value.value().slot),
              header, saved_value.value().boundary,
              static_cast<std::uint64_t>(capacity.max_records) * 6, owner->raw_origin_);
          if (catalog.has_value()) {
            owner->saved_ =
                std::make_unique<SavedState>(std::move(saved_value.value()));
            owner->archive_ = std::move(catalog).value();
          } else {
            owner->note_catalogue_fault(saved_value.value().slot, catalog.error());
            // The older accepted slot with its own catalogue still resumes compactly
            // (as the store falls back to it when it refuses the newer one).
            auto &older = owner->saved_load_->alternate;
            if (older) {
              auto older_catalog = ArchiveCatalog::open(
                  *owner->directory_,
                  std::string{name} + ".archive." + std::to_string(older->slot), header,
                  older->boundary, static_cast<std::uint64_t>(capacity.max_records) * 6,
                  owner->raw_origin_);
              if (older_catalog.has_value()) {
                owner->saved_ = std::make_unique<SavedState>(std::move(*older));
                owner->archive_ = std::move(older_catalog).value();
                older.reset();
              } else
                owner->note_catalogue_fault(older->slot, older_catalog.error());
            }
          }
        }
        if (owner->saved_ && owner->archive_ && owner->saved_->unresolved.empty()) {
          const auto *coverage = owner->saved_->context.find("semantic_coverage");
          const auto *physical = owner->saved_->context.find("physical_records");
          if (coverage && std::holds_alternative<std::string>(coverage->value()) &&
              coverage->string() == "settled-no-station-v1" && physical &&
              std::holds_alternative<Number>(physical->value())) {
            const auto &text = physical->number().text();
            std::uint64_t records = 0;
            const auto parsed =
                std::from_chars(text.data(), text.data() + text.size(), records);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                records > capacity.max_records || records < owner->saved_->fact_count)
              return replay_allowed
                         ? Result<std::optional<JournalResume>>::success(std::nullopt)
                         : Result<std::optional<JournalResume>>::failure(
                               {ErrorCode::unsupported});
            constexpr auto length = journal_frame_header_size + 24;
            auto anchor = locked.read_original_range(
                owner->saved_->boundary.end_offset - length, length);
            if (!anchor.has_value())
              return Result<std::optional<JournalResume>>::failure(anchor.error());
            owner->compact_ = true;
            return Result<std::optional<JournalResume>>::success(JournalResume{
                owner->saved_->boundary, static_cast<std::size_t>(records),
                crc32c(anchor.value().bytes)});
          }
        }
        return replay_allowed
                   ? Result<std::optional<JournalResume>>::success(std::nullopt)
                   : Result<std::optional<JournalResume>>::failure(
                         {ErrorCode::unsupported});
      };
    auto journal = FramedJournal::open(
        *owner->directory_, name, header, capacity, SyncStrength::full,
        use_scan_checkpoint && allow_full_replay && max_full_replay_bytes == UINT64_MAX,
        std::nullopt, std::move(selector), owner->raw_origin_);
    if (!journal.has_value()) {
      return Result<std::unique_ptr<RetainedState>>::failure(journal.error());
    }
    owner->journal_ = std::move(journal).value();
    owner->journal_name_ = name;
    owner->journal_->raw_origin_ = owner->raw_origin_;
    if (owner->archive_)
      owner->archive_->raw_origin_ = owner->raw_origin_;
    owner->pending_namespace_ = header.issuer_namespace;
    owner->reconciled_ = false;
    owner->live_from_sequence_ = owner->journal_->cursor().sequence + 1;
    auto selected_saved = owner->compact_ ? nullptr : std::move(owner->saved_);
    auto selected_archive = owner->compact_ ? nullptr : std::move(owner->archive_);
    const auto rebuilt = owner->rebuild();
    if (!rebuilt.has_value()) {
      return Result<std::unique_ptr<RetainedState>>::failure(rebuilt.error());
    }
    if (owner->compact_) {
      // The already selected generation remains the semantic base.
    } else if (selected_saved &&
               selected_saved->boundary.sequence <=
                   owner->journal_->cursor().sequence &&
               selected_saved->boundary.end_offset <=
                   owner->journal_->cursor().end_offset) {
      owner->saved_ = std::move(selected_saved);
      owner->archive_ = std::move(selected_archive);
    } else {
      auto saved =
          load_saved_state(*owner->directory_, name, *owner->journal_, capacity, false,
                           owner->raw_origin_, owner->saved_load_.get());
      if (!saved.has_value())
        return Result<std::unique_ptr<RetainedState>>::failure(saved.error());
      auto &saved_value = saved.value();
      if (saved_value) {
        owner->saved_ = std::make_unique<SavedState>(std::move(saved_value.value()));
        auto archive = ArchiveCatalog::open(
            *owner->directory_,
            std::string{name} + ".archive." + std::to_string(owner->saved_->slot),
            header, owner->saved_->boundary,
            static_cast<std::uint64_t>(capacity.max_records) * 6, owner->raw_origin_);
        if (archive.has_value())
          owner->archive_ = std::move(archive).value();
        else
          // The slot is still used (its lookups scan the resident journal).
          owner->note_catalogue_fault(owner->saved_->slot, archive.error());
      }
    }
    owner->raw_end_ = std::move(end).value();
    return Result<std::unique_ptr<RetainedState>>::success(std::move(owner));
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &selected = *answer.value();
    const auto current = selected.cursor();
    const auto state = selected.state();
    const bool hint = selected.used_scan_checkpoint();
    returned.cursor(current);
    returned.native(state);
    returned.native(hint);
    returned.native(selected.compact_);
    returned.native(selected.reconciled_);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
void RetainedState::index_fact(Snapshot &snapshot, std::size_t ordinal) {
  if (!query_index_)
    return;
  RawRetainedOriginScope query_origin{query_index_->raw_origin_, raw_origin_};
  const auto &fact = snapshot.facts[ordinal];
  const auto *origin = segment(fact.record.journal);
  if (!origin)
    throw Error{ErrorCode::corrupt};
  for (const auto &k :
       fact_keys(fact, {.issuer_namespace = origin->header().issuer_namespace,
                        .ordinal = ordinal})) {
    auto root = query_index_->put(snapshot.query_root, k, ordinal);
    if (!root.has_value())
      throw root.error();
    snapshot.query_root = root.value();
  }
}
void RetainedState::index_source(Snapshot &snapshot, std::size_t ordinal) {
  if (!query_index_)
    return;
  RawRetainedOriginScope query_origin{query_index_->raw_origin_, raw_origin_};
  const auto &source = snapshot.sources[ordinal];
  auto root = query_index_->put(snapshot.query_root,
                                ordinal_key(5, source.journal.bytes(), source.sequence),
                                ordinal);
  if (!root.has_value())
    throw root.error();
  snapshot.query_root = root.value();
}
void RetainedState::populate_index(Snapshot &snapshot) {
  snapshot.query_root = {};
  for (std::size_t i = 0; i < snapshot.facts.size(); ++i)
    index_fact(snapshot, i);
  for (std::size_t i = 0; i < snapshot.sources.size(); ++i)
    index_source(snapshot, i);
}
Result<void> RetainedState::enable_indexed_queries(std::unique_ptr<JournalFile> file,
                                                   std::uint64_t max_bytes) {
  RawRetainedFields arguments;
  arguments.native(max_bytes);
  RawRetainedPair trace{raw_origin_, {3106, 3107}, 119, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    if (compact_)
      return Result<void>::failure({ErrorCode::unsupported});
    if (in_transaction_ || query_index_)
      return Result<void>::failure({ErrorCode::busy});
    auto opened = RecoveryIndex::open(std::move(file), max_bytes, trace.child());
    if (!opened.has_value())
      return Result<void>::failure(opened.error());
    query_index_ = std::move(opened).value();
    try {
      populate_index(committed_);
      if (prepared_)
        populate_index(*prepared_);
      return Result<void>::success();
    } catch (const Error &error) {
      query_index_.reset();
      committed_.query_root = {};
      if (prepared_)
        prepared_->query_root = {};
      return Result<void>::failure(error);
    } catch (const std::bad_alloc &) {
      query_index_.reset();
      committed_.query_root = {};
      if (prepared_)
        prepared_->query_root = {};
      return Result<void>::failure({ErrorCode::allocation});
    }
  });
  const bool attached = bool(query_index_);
  RawRetainedFields returned;
  returned.page(committed_.query_root);
  returned.native(attached);
  trace.result(answer, returned.view());
  return answer;
}
std::optional<RetainedFact> RetainedState::lookup(const Snapshot &snapshot,
                                                  unsigned char family,
                                                  RetainedKind kind,
                                                  const IdentityBytes &identity,
                                                  const IdentityBytes &second) const {
  RawRetainedFields arguments;
  arguments.native(family);
  arguments.native(kind);
  arguments.add(identity);
  arguments.add(second);
  RawRetainedPair trace{raw_origin_, {3140, 3141}, 150, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope journal_scope{journal_->raw_origin_, trace.child()};
  std::optional<RawRetainedOriginScope> catalog_scope;
  if (archive_)
    catalog_scope.emplace(archive_->raw_origin_, trace.child());
  std::optional<RawRetainedOriginScope> query_origin;
  if (query_index_)
    query_origin.emplace(query_index_->raw_origin_, trace.child());
  auto answer = trace.run([&]() -> std::optional<RetainedFact> {
    const auto wanted = query_key(family, kind, identity, second);
    if (query_index_) {
      auto found = query_index_->find(snapshot.query_root, wanted);
      if (!found.has_value())
        throw found.error();
      const auto &found_value = found.value();
      if (!found_value)
        return std::nullopt;
      if (found_value.value() >= snapshot.facts.size())
        throw Error{ErrorCode::corrupt};
      return snapshot.facts[static_cast<std::size_t>(found_value.value())];
    }
    for (std::size_t i = snapshot.facts.size(); i > 0; --i) {
      const auto &fact = snapshot.facts[i - 1];
      if (archive_ && saved_ && fact.record.journal == saved_->boundary.journal &&
          fact.record.sequence <= saved_->boundary.sequence)
        continue;
      const auto *origin = segment(fact.record.journal);
      if (!origin)
        throw Error{ErrorCode::corrupt};
      if (retained_kind(fact.event.body) != kind)
        continue;
      if (family == 1) {
        if (const auto k = key(fact.event.body, origin->header().issuer_namespace);
            k && k->identity == identity)
          return fact;
        if (const auto *body = std::get_if<AttemptObservationEvent>(&fact.event.body);
            body && body->phase == AttemptPhase::terminal &&
            body->attempt.bytes() == identity)
          return fact;
      } else if (family == 2) {
        if (const auto *body = std::get_if<AttemptObservationEvent>(&fact.event.body);
            body && body->attempt.bytes() == identity)
          return fact;
      } else if (family == 3) {
        if (const auto *body = std::get_if<AttemptAdmissionEvent>(&fact.event.body);
            body && body->invocation.bytes() == identity)
          return fact;
      } else if (family == 4) {
        if (const auto *body = std::get_if<RetryEvent>(&fact.event.body);
            body && body->decision.bytes() == identity &&
            body->invocation.bytes() == second)
          return fact;
      }
    }
    if (archive_ && saved_) {
      auto selected = archive_->find(wanted);
      if (!selected.has_value())
        throw selected.error();
      const auto &selected_value = selected.value();
      if (!selected_value)
        return std::nullopt;
      auto payload =
          journal_->read_catalog_payload(selected_value.value(), saved_->boundary);
      if (!payload.has_value())
        throw payload.error();
      auto event = decode_retained_event(
          payload.value(), journal_->header().limits.max_payload, &raw_origin_);
      if (!event.has_value())
        throw event.error();
      resolve_admission_input(event.value(), [&](InvocationId identity) {
        const auto invocation =
            lookup(snapshot, 1, RetainedKind::invocation, identity.bytes());
        return invocation ? std::optional<InvocationEvent>{std::get<InvocationEvent>(
                                invocation->event.body)}
                          : std::nullopt;
      });
      RetainedFact result{
          {selected_value.value().journal, selected_value.value().sequence},
          std::move(event).value(),
          selected_value.value().sequence >= live_from_sequence_
              ? RetainedEvidence::live
              : RetainedEvidence::recovered};
      const auto keys =
          fact_keys(result, {.issuer_namespace = journal_->header().issuer_namespace,
                             .ordinal = 0});
      if (std::find(keys.begin(), keys.end(), wanted) == keys.end())
        throw Error{ErrorCode::corrupt};
      // A live in-process checkpoint does not turn an old live admission into a
      // recovered one. Until compact restore is active, preserve resident evidence.
      const auto resident = std::lower_bound(
          snapshot.facts.begin(), snapshot.facts.end(), result.record.sequence,
          [](const auto &fact, std::uint64_t sequence) {
            return fact.record.sequence < sequence;
          });
      if (resident != snapshot.facts.end() && resident->record == result.record)
        result.evidence = resident->evidence;
      return result;
    }
    return std::nullopt;
  });
  RawRetainedFields returned;
  if (answer) {
    returned.reference(answer->record);
    returned.native(answer->evidence);
  }
  trace.finish(0, returned.view());
  return answer;
}
Result<RetainedFact> RetainedState::fact(std::size_t ordinal) const {
  if (raw_storage_owner_.enabled() && !raw_storage_owner_.permits_origin_read())
    return Result<RetainedFact>::failure({ErrorCode::wrong_incarnation});
  if (raw_origin_.loan)
    return Result<RetainedFact>::failure({ErrorCode::capacity});
  const std::size_t limit = 1;
  RawRetainedFields arguments;
  arguments.native(ordinal);
  arguments.native(limit);
  RawRetainedPair trace{raw_origin_, {3144, 3145}, 114, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope journal_scope{journal_->raw_origin_, trace.child()};
  RawRetainedEvent selection{trace.child(), {3154, 65}};
  auto answer = trace.run([&]() -> Result<RetainedFact> {
    try {
      if (ordinal >= fact_count())
        return Result<RetainedFact>::failure({ErrorCode::invalid_range});
      if (archive_ && saved_ && ordinal < saved_->fact_count) {
        auto found = archive_->find(ordinal_key(6, {}, ordinal));
        if (!found.has_value())
          return Result<RetainedFact>::failure(found.error());
        const auto &found_value = found.value();
        if (!found_value)
          return Result<RetainedFact>::failure({ErrorCode::corrupt});
        auto payload =
            journal_->read_catalog_payload(found_value.value(), saved_->boundary);
        if (!payload.has_value())
          return Result<RetainedFact>::failure(payload.error());
        auto decoded = decode_retained_event(
            payload.value(), journal_->header().limits.max_payload, &raw_origin_);
        if (!decoded.has_value())
          return Result<RetainedFact>::failure(decoded.error());
        resolve_admission_input(decoded.value(), [&](InvocationId identity) {
          const auto invocation =
              lookup(committed_, 1, RetainedKind::invocation, identity.bytes());
          return invocation ? std::optional<InvocationEvent>{std::get<InvocationEvent>(
                                  invocation->event.body)}
                            : std::nullopt;
        });
        return Result<RetainedFact>::success(
            {{found_value.value().journal, found_value.value().sequence},
             std::move(decoded).value(),
             compact_ ? (found_value.value().sequence >= live_from_sequence_
                             ? RetainedEvidence::live
                             : RetainedEvidence::recovered)
                      : committed_.facts[ordinal].evidence});
      }
      if (query_index_) {
        auto found =
            query_index_->find(committed_.query_root, ordinal_key(6, {}, ordinal));
        if (!found.has_value())
          return Result<RetainedFact>::failure(found.error());
        const auto &found_value = found.value();
        if (!found_value || found_value.value() != ordinal)
          return Result<RetainedFact>::failure({ErrorCode::corrupt});
      }
      return Result<RetainedFact>::success(
          committed_.facts[ordinal - committed_.archived_facts]);
    } catch (const Error &error) {
      return Result<RetainedFact>::failure(error);
    } catch (const std::bad_alloc &) {
      return Result<RetainedFact>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const std::size_t count = 1;
    returned.native(count);
    RawRetainedFields selected;
    selected.reference(answer.value().record);
    selected.native(answer.value().evidence);
    selection.publish(selected.view());
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<std::vector<RetainedFact>>
RetainedState::FactHistory::page(std::size_t first, std::size_t limit) const {
  const auto origin = raw_origin ? raw_origin->snapshot() : RawRetainedOrigin{};
  if (origin.loan)
    return Result<std::vector<RetainedFact>>::failure({ErrorCode::capacity});
  RawRetainedFields arguments;
  arguments.native(first);
  arguments.native(limit);
  RawRetainedPair trace{origin, {3144, 3145}, 114, arguments.view()};
  std::optional<RawRetainedOriginScope> origin_scope;
  if (raw_origin)
    origin_scope.emplace(raw_origin->origin, trace.child());
  auto answer = trace.run([&]() -> Result<std::vector<RetainedFact>> {
    if (!limit || limit > 16 || first > count || !read)
      return Result<std::vector<RetainedFact>>::failure({ErrorCode::invalid_range});
    try {
      std::vector<RetainedFact> page;
      const auto end = first + std::min(limit, count - first);
      page.reserve(end - first);
      for (auto i = first; i < end; ++i) {
        RawRetainedEvent selection{trace.child(), {3154, 65}};
        auto loaded = read(i);
        if (!loaded.has_value())
          return Result<std::vector<RetainedFact>>::failure(loaded.error());
        const auto &selected = loaded.value();
        RawRetainedFields fields;
        fields.reference(selected.record);
        fields.native(selected.evidence);
        selection.publish(fields.view());
        page.push_back(std::move(loaded).value());
      }
      return Result<std::vector<RetainedFact>>::success(std::move(page));
    } catch (const std::bad_alloc &) {
      return Result<std::vector<RetainedFact>>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto size = answer.value().size();
    returned.native(size);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<std::vector<RetainedFact>> RetainedState::fact_page(std::size_t first,
                                                           std::size_t limit) const {
  return FactHistory{fact_count(),
                     [this](std::size_t i) {
                       const auto selected = journal_->raw_reader_origin_->snapshot();
                       RawRetainedOriginScope origin_scope{raw_origin_, selected};
                       return fact(i);
                     },
                     journal_->raw_reader_origin_}
      .page(first, limit);
}
RetainedState::FactHistory RetainedState::history_snapshot() const {
  const auto native_count = fact_count();
  const bool saved_present = bool(saved_);
  const auto boundary = saved_ ? saved_->boundary : cursor();
  RawRetainedFields arguments;
  arguments.native(native_count);
  arguments.native(saved_present);
  arguments.cursor(boundary);
  RawRetainedPair trace{raw_origin_, {3146, 3147}, 171, arguments.view()};
  RawRetainedOriginScope journal_origin{journal_->raw_origin_, trace.child()};
  std::size_t selected_tail = 0;
  auto answer = trace.run([&]() -> FactHistory {
    const auto archived =
        archive_ && saved_ ? static_cast<std::size_t>(saved_->fact_count) : 0;
    if (!compact_ && archived > committed_.facts.size())
      throw Error{ErrorCode::corrupt};
    const auto tail_start = compact_ ? archived - committed_.archived_facts : archived;
    const auto tail = committed_.facts;
    selected_tail = tail.size();
    if ((compact_ && archived < committed_.archived_facts) || tail_start > tail.size())
      throw Error{ErrorCode::corrupt};
    const auto limit = journal_->header().limits.max_payload;
    auto reader = journal_->archive_reader(saved_ ? saved_->boundary : cursor());
    return {
        archived + tail.size() - tail_start,
        [archive = archive_, archived, tail, tail_start, reader, limit,
         live_from = live_from_sequence_, origin = journal_->raw_reader_origin_,
         creation = trace.reference()](std::size_t ordinal) -> Result<RetainedFact> {
          const auto selected = origin->snapshot();
          if (selected.loan)
            return Result<RetainedFact>::failure({ErrorCode::capacity});
          RawRetainedFields read_arguments;
          read_arguments.add(creation.lane);
          read_arguments.native(creation.ordinal);
          read_arguments.native(ordinal);
          RawRetainedPair read_trace{
              selected, {3148, 3149}, 147, read_arguments.view()};
          RawRetainedOriginScope read_origin{origin->origin, read_trace.child()};
          std::optional<RawRetainedOriginScope> catalog_origin;
          if (archive)
            catalog_origin.emplace(archive->raw_origin_, read_trace.child());
          auto read_answer = read_trace.run([&]() -> Result<RetainedFact> {
            try {
              if (ordinal >= archived) {
                if (ordinal - archived >= tail.size() - tail_start)
                  return Result<RetainedFact>::failure({ErrorCode::invalid_range});
                return Result<RetainedFact>::success(
                    tail[tail_start + ordinal - archived]);
              }
              auto found = archive->find(ordinal_key(6, {}, ordinal));
              if (!found.has_value())
                return Result<RetainedFact>::failure(found.error());
              const auto &found_value = found.value();
              if (!found_value)
                return Result<RetainedFact>::failure({ErrorCode::corrupt});
              auto body = reader(found_value.value());
              if (!body.has_value())
                return Result<RetainedFact>::failure(body.error());
              auto decoded =
                  decode_retained_event(body.value(), limit, &origin->origin);
              if (!decoded.has_value())
                return Result<RetainedFact>::failure(decoded.error());
              resolve_admission_input(
                  decoded.value(),
                  [&](InvocationId identity) -> std::optional<InvocationEvent> {
                    auto descriptor = archive->find(
                        query_key(1, RetainedKind::invocation, identity.bytes()));
                    if (!descriptor.has_value())
                      throw descriptor.error();
                    if (!descriptor.value())
                      return std::nullopt;
                    auto input = reader(*descriptor.value());
                    if (!input.has_value())
                      throw input.error();
                    auto invocation =
                        decode_retained_event(input.value(), limit, &origin->origin);
                    if (!invocation.has_value())
                      throw invocation.error();
                    const auto *body =
                        std::get_if<InvocationEvent>(&invocation.value().body);
                    if (!body)
                      throw Error{ErrorCode::corrupt};
                    return *body;
                  });
              return Result<RetainedFact>::success(
                  {{found_value.value().journal, found_value.value().sequence},
                   std::move(decoded).value(),
                   found_value.value().sequence >= live_from
                       ? RetainedEvidence::live
                       : RetainedEvidence::recovered});
            } catch (const Error &error) {
              return Result<RetainedFact>::failure(error);
            } catch (const std::bad_alloc &) {
              return Result<RetainedFact>::failure({ErrorCode::allocation});
            }
          });
          RawRetainedFields read_returned;
          if (read_answer.has_value()) {
            read_returned.reference(read_answer.value().record);
            read_returned.native(read_answer.value().evidence);
            read_trace.result(read_answer, read_returned.view());
          } else
            read_trace.result(read_answer);
          return read_answer;
        },
        journal_->raw_reader_origin_};
  });
  RawRetainedFields returned;
  returned.cursor(boundary);
  returned.native(selected_tail);
  trace.finish(0, returned.view());
  return answer;
}
const RetainedState::Snapshot &RetainedState::visible() const noexcept {
  return prepared_ ? *prepared_ : committed_;
}
bool RetainedState::has_identity(const RetainedEvent &event) {
  const auto *observation = std::get_if<AttemptObservationEvent>(&event.body);
  return key(event.body).has_value() ||
         (observation && observation->phase == AttemptPhase::terminal);
}
std::optional<RetainedFact>
RetainedState::existing(const Snapshot &snapshot, const RetainedEvent &event,
                        std::optional<std::uint64_t> issuer_namespace,
                        const ReplayIndex *index) const {
  RawRetainedEvent selected{raw_origin_, {3118, 65}};
  auto answer = [&]() -> std::optional<RetainedFact> {
    const auto identity =
        key(event.body, issuer_namespace.value_or(snapshot.issuer_namespace));
    if (query_index_ || archive_) {
      if (identity)
        return lookup(snapshot, 1, identity->kind, identity->identity);
      if (const auto *observation = std::get_if<AttemptObservationEvent>(&event.body);
          observation && observation->phase == AttemptPhase::terminal)
        return lookup(snapshot, 1, retained_kind(event.body),
                      observation->attempt.bytes());
      if (query_index_)
        return std::nullopt;
    }
    if (identity && index) {
      const auto found = index->facts.find(*identity);
      return found == index->facts.end()
                 ? std::nullopt
                 : std::optional<RetainedFact>{snapshot.facts[found->second]};
    }
    if (identity) {
      for (const auto &fact : snapshot.facts) {
        const auto *origin = segment(fact.record.journal);
        if (origin &&
            key(fact.event.body, origin->header().issuer_namespace) == identity) {
          return fact;
        }
      }
    }
    if (const auto *observation = std::get_if<AttemptObservationEvent>(&event.body);
        observation && observation->phase == AttemptPhase::terminal) {
      for (auto it = snapshot.facts.rbegin(); it != snapshot.facts.rend(); ++it) {
        const auto *previous = std::get_if<AttemptObservationEvent>(&it->event.body);
        if (previous && previous->attempt == observation->attempt &&
            previous->phase == AttemptPhase::terminal) {
          return *it;
        }
      }
    }
    return std::nullopt;
  }();
  if (answer) {
    RawRetainedFields fields;
    fields.reference(answer->record);
    fields.native(answer->evidence);
    selected.publish(fields.view());
  }
  return answer;
}
const RetainedFact *
RetainedState::existing_uncertain(const Snapshot &snapshot, const RetainedEvent &event,
                                  std::optional<std::uint64_t> issuer_namespace) const {
  RawRetainedEvent selected{raw_origin_, {3118, 65}};
  auto answer = [&]() -> const RetainedFact * {
    const auto identity =
        key(event.body, issuer_namespace.value_or(snapshot.issuer_namespace));
    for (const auto &fact : snapshot.uncertain_facts) {
      const auto *origin = segment(fact.record.journal);
      if (identity && origin &&
          key(fact.event.body, origin->header().issuer_namespace) == identity)
        return &fact;
      const auto *observation = std::get_if<AttemptObservationEvent>(&event.body);
      const auto *previous = std::get_if<AttemptObservationEvent>(&fact.event.body);
      if (observation && previous && observation->phase == AttemptPhase::terminal &&
          previous->phase == AttemptPhase::terminal &&
          observation->attempt == previous->attempt)
        return &fact;
      if (!has_identity(event) && fact.event == event)
        return &fact;
    }
    return nullptr;
  }();
  if (answer) {
    RawRetainedFields fields;
    fields.reference(answer->record);
    fields.native(answer->evidence);
    selected.publish(fields.view());
  }
  return answer;
}
FramedJournal *RetainedState::segment(AuditStreamId identity) const noexcept {
  if (journal_ && journal_->header().journal == identity)
    return journal_.get();
  for (const auto &journal : historical_)
    if (journal->header().journal == identity)
      return journal.get();
  return nullptr;
}
bool RetainedState::has_room(const Snapshot &snapshot,
                             std::size_t count) const noexcept {
  auto remaining = max_history_entries_;
  const std::array sizes{snapshot.archived_records, snapshot.facts.size(),
                         snapshot.sources.size(), snapshot.uncertain_facts.size(),
                         snapshot.provisional_originals.size()};
  for (const auto size : sizes) {
    if (size > remaining)
      return false;
    remaining -= size;
  }
  return count <= remaining;
}
Result<void> RetainedState::apply(Snapshot &snapshot, RetainedEvent event,
                                  RecordReference record, RetainedEvidence evidence,
                                  ReplayIndex *index) {
  const auto dependencies = event.dependencies.size();
  RawRetainedFields arguments;
  arguments.reference(record);
  arguments.native(evidence);
  arguments.native(dependencies);
  RawRetainedPair trace{raw_origin_, {3115, 3116}, 151, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    resolve_admission_input(event, [&](InvocationId identity) {
      const auto invocation =
          lookup(snapshot, 1, RetainedKind::invocation, identity.bytes());
      return invocation ? std::optional<InvocationEvent>{std::get<InvocationEvent>(
                              invocation->event.body)}
                        : std::nullopt;
    });
    if (std::holds_alternative<RecoveryChoiceEvent>(event.body) ||
        std::holds_alternative<ProvisionalCaptureEvent>(event.body)) {
      return Result<void>::failure({ErrorCode::unsupported});
    }
    if (const auto previous = existing(snapshot, event, std::nullopt, index)) {
      return previous->event == event ? Result<void>::success()
                                      : Result<void>::failure({ErrorCode::conflict});
    }
    if (existing_uncertain(snapshot, event))
      return Result<void>::failure({ErrorCode::conflict});
    if (!has_room(snapshot, 1))
      return Result<void>::failure({ErrorCode::capacity});
    for (const auto &dependency : event.dependencies) {
      RawRetainedFields selected;
      selected.reference(dependency);
      (void)raw_retained_event(raw_origin_, {3117, 64}, selected.view());
      if (archive_ && saved_ && dependency.journal == saved_->boundary.journal &&
          dependency.sequence <= saved_->boundary.sequence) {
        auto found = archive_->find(
            ordinal_key(5, dependency.journal.bytes(), dependency.sequence));
        if (!found.has_value())
          return Result<void>::failure(found.error());
        const auto &found_value = found.value();
        if (!found_value)
          return Result<void>::failure({ErrorCode::conflict});
        continue;
      }
      if (query_index_) {
        auto found = query_index_->find(
            snapshot.query_root,
            ordinal_key(5, dependency.journal.bytes(), dependency.sequence));
        if (!found.has_value())
          return Result<void>::failure(found.error());
        const auto &found_value = found.value();
        if (!found_value)
          return Result<void>::failure({ErrorCode::conflict});
        if (found_value.value() >= snapshot.sources.size())
          return Result<void>::failure({ErrorCode::corrupt});
        continue;
      }
      if (index) {
        if (!index->sources.contains(dependency))
          return Result<void>::failure({ErrorCode::conflict});
        continue;
      }
      const auto source = std::find_if(
          snapshot.sources.begin(), snapshot.sources.end(), [&](const auto &candidate) {
            return candidate.journal == dependency.journal &&
                   candidate.sequence == dependency.sequence;
          });
      if (source == snapshot.sources.end()) {
        return Result<void>::failure({ErrorCode::conflict});
      }
    }
    const auto get =
        [&]<typename T>(unsigned char family, RetainedKind kind,
                        const IdentityBytes &identity,
                        const IdentityBytes &second = {}) -> std::optional<T> {
      const auto fact = lookup(snapshot, family, kind, identity, second);
      if (!fact)
        return std::nullopt;
      const auto *body = std::get_if<T>(&fact->event.body);
      return body ? std::optional<T>{*body} : std::nullopt;
    };
    const auto validated = std::visit(
        [&](const auto &body) -> Result<void> {
          using T = std::decay_t<decltype(body)>;
          if constexpr (std::is_same_v<T, IssuerReservationEvent>) {
            if (body.counter <= snapshot.counter) {
              return Result<void>::failure({ErrorCode::conflict});
            }
            snapshot.counter = body.counter;
          } else if constexpr (std::is_same_v<T, DecisionEvent>) {
            for (std::size_t i = 0; i < body.planned_invocations.size(); ++i) {
              for (std::size_t j = i + 1; j < body.planned_invocations.size(); ++j) {
                if (body.planned_invocations[i] == body.planned_invocations[j]) {
                  return Result<void>::failure({ErrorCode::invalid_range});
                }
              }
            }
          } else if constexpr (std::is_same_v<T, InvocationEvent>) {
            const auto decision = get.template operator()<DecisionEvent>(
                1, RetainedKind::decision, body.decision.bytes());
            if (!decision || !plans(*decision, body.invocation)) {
              return Result<void>::failure({ErrorCode::conflict});
            }
          } else if constexpr (std::is_same_v<T, AttemptAdmissionEvent> ||
                               std::is_same_v<T, RetryEvent>) {
            const auto invocation = get.template operator()<InvocationEvent>(
                1, RetainedKind::invocation, body.invocation.bytes());
            const auto decision = get.template operator()<DecisionEvent>(
                1, RetainedKind::decision, body.decision.bytes());
            if (!invocation || !decision || !plans(*decision, body.invocation)) {
              return Result<void>::failure({ErrorCode::conflict});
            }
            const auto prior = get.template operator()<AttemptAdmissionEvent>(
                3, RetainedKind::attempt_admitted, body.invocation.bytes());
            if constexpr (std::is_same_v<T, RetryEvent>) {
              const auto used = get.template operator()<AttemptAdmissionEvent>(
                  1, RetainedKind::attempt_admitted, body.attempt.bytes());
              const auto prior_retry = get.template operator()<RetryEvent>(
                  4, RetainedKind::retry, body.decision.bytes(),
                  body.invocation.bytes());
              if (!prior || used || prior_retry ||
                  body.decision == invocation->decision) {
                return Result<void>::failure({ErrorCode::conflict});
              }
            } else {
              if (prior) {
                const auto retry = get.template operator()<RetryEvent>(
                    1, RetainedKind::retry, body.attempt.bytes());
                if (!retry || retry->invocation != body.invocation ||
                    retry->decision != body.decision) {
                  return Result<void>::failure({ErrorCode::conflict});
                }
              } else if (body.decision != invocation->decision) {
                return Result<void>::failure({ErrorCode::conflict});
              }
            }
          } else if constexpr (std::is_same_v<T, AdapterReceiptEvent>) {
            const auto opened = get.template operator()<AttemptOpenEvent>(
                1, RetainedKind::attempt_open, body.attempt.bytes());
            if (!opened) {
              return Result<void>::failure({ErrorCode::conflict});
            }
          } else if constexpr (std::is_same_v<T, AttemptOpenEvent> ||
                               std::is_same_v<T, AttemptObservationEvent>) {
            const auto admission = get.template operator()<AttemptAdmissionEvent>(
                1, RetainedKind::attempt_admitted, body.attempt.bytes());
            const auto opened = get.template operator()<AttemptOpenEvent>(
                1, RetainedKind::attempt_open, body.attempt.bytes());
            const auto observation = get.template operator()<AttemptObservationEvent>(
                2, RetainedKind::observation, body.attempt.bytes());
            if (!admission ||
                (observation && observation->phase == AttemptPhase::terminal)) {
              return Result<void>::failure({ErrorCode::conflict});
            }
            if constexpr (std::is_same_v<T, AttemptOpenEvent>) {
              if (opened) {
                return Result<void>::failure({ErrorCode::conflict});
              }
            } else {
              if (!opened && !(body.phase == AttemptPhase::terminal &&
                               (body.disposition == AttemptDisposition::cancellation ||
                                body.disposition == AttemptDisposition::unknown))) {
                return Result<void>::failure({ErrorCode::conflict});
              }
              if (observation && observation->phase == AttemptPhase::settling &&
                  body.phase == AttemptPhase::running) {
                return Result<void>::failure({ErrorCode::conflict});
              }
            }
          }
          return Result<void>::success();
        },
        event.body);
    if (!validated.has_value()) {
      return validated;
    }
    snapshot.facts.push_back({record, std::move(event), evidence});
    index_fact(snapshot, snapshot.facts.size() - 1);
    if (index) {
      const auto *origin = segment(record.journal);
      if (const auto identity =
              key(snapshot.facts.back().event.body, origin->header().issuer_namespace))
        index->facts.emplace(*identity, snapshot.facts.size() - 1);
    }
    return Result<void>::success();
  });
  RawRetainedFields returned;
  returned.page(snapshot.query_root);
  returned.native(snapshot.counter);
  trace.result(answer, returned.view());
  return answer;
}

Result<void> RetainedState::rebuild() {
  try {
    auto &staging = prepared_.emplace(Snapshot{});
    staging.issuer_namespace = journal_->header().issuer_namespace;
    if (compact_) {
      staging.counter = saved_->issuer_counter;
      staging.archived_facts = static_cast<std::size_t>(saved_->fact_count);
      staging.archived_records =
          journal_->usage().indexed_records - journal_->staged_records().size();
    }
    return replay(staging, *journal_);
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
Result<void> RetainedState::replay(Snapshot &staging, FramedJournal &journal,
                                   std::span<const SourceReference> capture_only,
                                   std::uint64_t maintenance_end) {
  const auto current_prefix = journal.cursor();
  const auto capture_count = capture_only.size();
  RawRetainedFields arguments;
  arguments.cursor(current_prefix);
  arguments.native(capture_count);
  arguments.native(maintenance_end);
  RawRetainedPair trace{raw_origin_, {3130, 3131}, 169, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope journal_origin{journal.raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    try {
      ReplayIndex lookup;
      if (!query_index_)
        for (std::size_t i = 0; i < staging.facts.size(); ++i) {
          const auto &fact = staging.facts[i];
          const auto *origin = segment(fact.record.journal);
          if (const auto identity =
                  key(fact.event.body, origin->header().issuer_namespace))
            lookup.facts.emplace(*identity, i);
        }
      if (!query_index_)
        for (const auto &source : staging.sources)
          lookup.sources.insert({source.journal, source.sequence});
      const auto records = journal.staged_records();
      std::size_t first = 0;
      while (first < records.size()) {
        std::size_t end = first + 1;
        while (end < records.size() &&
               records[end].batch_first == records[first].batch_first) {
          ++end;
        }
        const auto batch_first = records[first].batch_first;
        RawRetainedFields batch_arguments;
        batch_arguments.native(batch_first);
        batch_arguments.native(first);
        batch_arguments.native(end);
        batch_arguments.native(staging.counter);
        batch_arguments.page(staging.query_root);
        RawRetainedPair batch_trace{
            raw_origin_, {3132, 3133}, 187, batch_arguments.view()};
        RawRetainedOriginScope batch_origin{raw_origin_, batch_trace.child()};
        RawRetainedOriginScope batch_journal{journal.raw_origin_, batch_trace.child()};
        auto batch = batch_trace.run([&]() -> Result<bool> {
          // Replay is prepared state. This path only appends sources/facts and
          // advances counter: preserve a batch checkpoint, never clone the payload
          // prefix. The guard restores the valid prefix on rejection or any return.
          struct BatchRollback {
            Snapshot &snapshot;
            RetainedState &owner;
            ReplayIndex &lookup;
            std::size_t facts;
            std::size_t sources;
            std::uint64_t counter;
            RecoveryPageRef root;
            bool accepted = false;
            ~BatchRollback() {
              if (accepted)
                return;
              while (snapshot.facts.size() > facts) {
                const auto &fact = snapshot.facts.back();
                const auto *origin = owner.segment(fact.record.journal);
                if (const auto identity =
                        key(fact.event.body, origin->header().issuer_namespace))
                  lookup.facts.erase(*identity);
                snapshot.facts.pop_back();
              }
              while (snapshot.sources.size() > sources) {
                const auto &source = snapshot.sources.back();
                lookup.sources.erase({source.journal, source.sequence});
                snapshot.sources.pop_back();
              }
              snapshot.counter = counter;
              snapshot.query_root = root;
            }
          } rollback{staging,
                     *this,
                     lookup,
                     staging.facts.size(),
                     staging.sources.size(),
                     staging.counter,
                     staging.query_root};
          for (std::size_t index = first; index < end; ++index) {
            if (records[index].kind == FrameKind::source) {
              const SourceReference reference{records[index].journal,
                                              records[index].sequence};
              if (std::find(capture_only.begin(), capture_only.end(), reference) !=
                  capture_only.end())
                continue;
              if (!has_room(staging, 1))
                return Result<bool>::failure({ErrorCode::capacity});
              const auto checked = journal.read_payload(records[index]);
              if (!checked.has_value()) {
                return Result<bool>::failure(checked.error());
              }
              staging.sources.push_back(records[index]);
              index_source(staging, staging.sources.size() - 1);
              if (!query_index_)
                lookup.sources.insert(
                    {records[index].journal, records[index].sequence});
            }
          }
          std::optional<Error> invalid;
          for (std::size_t index = first; index < end; ++index) {
            if (records[index].kind != FrameKind::semantic ||
                records[index].sequence <= maintenance_end) {
              continue;
            }
            auto payload = journal.read_payload(records[index]);
            if (!payload.has_value()) {
              return Result<bool>::failure(payload.error());
            }
            auto decoded = decode_retained_event(
                payload.value(), journal.header().limits.max_payload, &raw_origin_);
            if (!decoded.has_value()) {
              if (decoded.error().code == ErrorCode::allocation) {
                return Result<bool>::failure(decoded.error());
              }
              invalid = decoded.error();
              break;
            }
            if (auto *application =
                    std::get_if<ApplicationRecordEvent>(&decoded.value().body)) {
              auto reader = journal.payload_reader(records[index]);
              if (!reader.has_value())
                return Result<bool>::failure(reader.error());
              const auto offset =
                  std::size_t{30} + decoded.value().dependencies.size() * 24;
              const auto size = application->payload.size();
              if (offset > payload.value().size() ||
                  size != payload.value().size() - offset)
                return Result<bool>::failure({ErrorCode::corrupt});
              application->payload = ImmutableBytes::cold(
                  size, [reader = std::move(reader).value(), offset, size]() {
                    auto bytes = reader();
                    if (!bytes.has_value())
                      return bytes;
                    if (offset > bytes.value().size() ||
                        size != bytes.value().size() - offset)
                      return Result<std::vector<std::byte>>::failure(
                          {ErrorCode::corrupt});
                    bytes.value().erase(bytes.value().begin(),
                                        bytes.value().begin() +
                                            static_cast<std::ptrdiff_t>(offset));
                    return bytes;
                  });
            }
            const auto applied = apply(
                staging, std::move(decoded).value(),
                {records[index].journal, records[index].sequence},
                RetainedEvidence::recovered_pending, query_index_ ? nullptr : &lookup);
            if (!applied.has_value()) {
              if (applied.error().code == ErrorCode::capacity)
                return Result<bool>::failure(applied.error());
              invalid = applied.error();
              break;
            }
          }
          if (invalid) {
            const auto rejected =
                journal.reject_recovery_batch(records[first].batch_first, *invalid);
            if (!rejected.has_value()) {
              return Result<bool>::failure(rejected.error());
            }
            return Result<bool>::success(false);
          }
          rollback.accepted = true;
          return Result<bool>::success(true);
        });
        const bool accepted = batch.has_value() && batch.value();
        const auto actual_facts = staging.facts.size(),
                   actual_sources = staging.sources.size();
        RawRetainedFields batch_returned;
        batch_returned.native(accepted);
        batch_returned.native(staging.counter);
        batch_returned.page(staging.query_root);
        batch_returned.native(actual_facts);
        batch_returned.native(actual_sources);
        batch_trace.result(batch, batch_returned.view());
        if (!batch.has_value())
          return Result<void>::failure(batch.error());
        if (!batch.value())
          break;
        first = end;
      }
      for (const auto &record : journal.pending_records()) {
        if (record.kind != FrameKind::semantic) {
          continue;
        }
        auto bytes = journal.read_payload(record);
        if (!bytes.has_value()) {
          return Result<void>::failure(bytes.error());
        }
        auto decoded = decode_retained_event(
            bytes.value(), journal.header().limits.max_payload, &raw_origin_);
        if (decoded.has_value()) {
          if (const auto *reservation =
                  std::get_if<IssuerReservationEvent>(&decoded.value().body)) {
            if (journal.header().issuer_namespace == pending_namespace_)
              pending_counter_ = std::max(pending_counter_, reservation->counter);
          }
        } else if (decoded.error().code == ErrorCode::allocation) {
          return Result<void>::failure(decoded.error());
        }
      }
      return Result<void>::success();
    } catch (const Error &error) {
      return Result<void>::failure(error);
    } catch (const std::bad_alloc &) {
      return Result<void>::failure({ErrorCode::allocation});
    }
  });
  const bool prepared = prepared_.has_value();
  RawRetainedFields returned;
  returned.page(staging.query_root);
  returned.native(staging.counter);
  returned.native(recording_failed_);
  returned.native(prepared);
  returned.native(reconciled_);
  trace.result(answer, returned.view());
  return answer;
}

RetainedState::SettlementScope::~SettlementScope() {
  if (!accepted_)
    return;
  owner_.settlement_credit_.reset();
  owner_.raw_stop_.reset();
  if (raw_end_)
    raw_end_->finish();
}
Result<std::unique_ptr<RetainedState::SettlementScope>>
RetainedState::protect_settlement(JournalCapacity credit) {
  RawRetainedFields arguments;
  arguments.native(credit.max_file_bytes);
  arguments.native(credit.max_records);
  RawRetainedPair trace{raw_origin_, {3126, 3127}, 123, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<std::unique_ptr<SettlementScope>> {
    using Outcome = Result<std::unique_ptr<SettlementScope>>;
    if (in_transaction_ || settlement_credit_)
      return Outcome::failure({ErrorCode::busy});
    if (state() != JournalWriterState::live)
      return Outcome::failure({ErrorCode::audit_unavailable});
    if (credit.max_file_bytes == 0 || credit.max_records == 0)
      return Outcome::failure({ErrorCode::invalid_range});
    const auto usage = journal_usage();
    if (usage.extent_error)
      return Outcome::failure(*usage.extent_error);
    const auto bytes = usage.remaining_bytes();
    if (!bytes || credit.max_file_bytes > *bytes ||
        credit.max_records > usage.remaining_records())
      return Outcome::failure({ErrorCode::capacity});
    try {
      auto end =
          RawRetainedSettlementEnd::prepare(raw_storage_owner_, trace.reference());
      if (!end.has_value())
        return Outcome::failure(end.error());
      auto scope = std::unique_ptr<SettlementScope>{new SettlementScope{*this}};
      auto raw = prepare_raw_stop(credit);
      if (!raw.has_value())
        return Outcome::failure(raw.error());
      raw_stop_ = std::move(raw).value();
      settlement_credit_ = credit;
      scope->raw_end_ = std::move(end).value();
      scope->accepted_ = true;
      return Outcome::success(std::move(scope));
    } catch (const std::bad_alloc &) {
      return Outcome::failure({ErrorCode::allocation});
    }
  });
  const bool present = settlement_credit_.has_value();
  RawRetainedFields returned;
  returned.native(present);
  if (settlement_credit_) {
    returned.native(settlement_credit_->max_file_bytes);
    returned.native(settlement_credit_->max_records);
  }
  trace.result(answer, returned.view());
  return answer;
}
Result<void> RetainedState::refresh_settlement(JournalCapacity credit) {
  RawRetainedFields arguments;
  arguments.native(credit.max_file_bytes);
  arguments.native(credit.max_records);
  RawRetainedPair trace{raw_origin_, {3126, 3127}, 123, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    if (!settlement_credit_ || in_transaction_)
      return Result<void>::failure({ErrorCode::busy});
    const auto usage = journal_usage();
    if (usage.extent_error)
      return Result<void>::failure(*usage.extent_error);
    const auto bytes = usage.remaining_bytes();
    if (!bytes || credit.max_file_bytes > *bytes ||
        credit.max_records > usage.remaining_records())
      return Result<void>::failure({ErrorCode::capacity});
    auto next = prepare_raw_stop(credit);
    if (!next.has_value())
      return Result<void>::failure(next.error());
    raw_stop_ = std::move(next).value();
    settlement_credit_ = credit;
    return Result<void>::success();
  });
  const bool present = settlement_credit_.has_value();
  RawRetainedFields returned;
  returned.native(present);
  if (settlement_credit_) {
    returned.native(settlement_credit_->max_file_bytes);
    returned.native(settlement_credit_->max_records);
  }
  trace.result(answer, returned.view());
  return answer;
}
Result<Submission>
RetainedState::submit_settlement(const RetainedEvent &event,
                                 std::span<PhysicalJournalRecord> acknowledged,
                                 bool *proposed_batch_acknowledged) {
  if (proposed_batch_acknowledged)
    *proposed_batch_acknowledged = false;
  const auto *observation = std::get_if<AttemptObservationEvent>(&event.body);
  if (!std::holds_alternative<AdapterReceiptEvent>(event.body) &&
      !(observation && observation->phase == AttemptPhase::terminal))
    return Result<Submission>::failure({ErrorCode::invalid_range});
  return submit_impl(event, true, acknowledged, proposed_batch_acknowledged);
}

Result<JournalCursor>
RetainedState::append(JournalCursor expected, std::span<const ByteView> sources,
                      std::span<const RetainedEvent> events,
                      std::span<PhysicalJournalRecord> acknowledged,
                      bool *proposed_batch_acknowledged) {
  if (proposed_batch_acknowledged)
    *proposed_batch_acknowledged = false;
  return append_impl(expected, sources, events, false, false, acknowledged,
                     proposed_batch_acknowledged);
}
Result<JournalCursor> RetainedState::append_settlement(
    JournalCursor expected, std::span<const RetainedEvent> events,
    std::span<PhysicalJournalRecord> acknowledged, bool *proposed_batch_acknowledged) {
  if (proposed_batch_acknowledged)
    *proposed_batch_acknowledged = false;
  if (events.size() < 2)
    return Result<JournalCursor>::failure({ErrorCode::invalid_range});
  const auto *record = std::get_if<ApplicationRecordEvent>(&events.front().body);
  if (!record || record->channel != ApplicationChannel::program)
    return Result<JournalCursor>::failure({ErrorCode::invalid_range});
  for (std::size_t index = 1; index < events.size(); ++index) {
    const auto *observation = std::get_if<AttemptObservationEvent>(&events[index].body);
    if (!observation || observation->phase != AttemptPhase::terminal)
      return Result<JournalCursor>::failure({ErrorCode::invalid_range});
  }
  return append_impl(expected, {}, events, false, true, acknowledged,
                     proposed_batch_acknowledged);
}
Result<JournalCursor> RetainedState::append_exec(JournalCursor expected,
                                                 std::span<const RetainedEvent> events,
                                                 RawRetainedExec &raw_exec) {
  raw_exec.accepted_ = false;
  raw_exec.attempt_.reset();
  auto result = append_impl(expected, {}, events, false, false, raw_exec.acknowledged_,
                            &raw_exec.accepted_, &raw_exec);
  if (raw_exec.accepted_)
    raw_exec.attempt_ = std::get<AttemptAdmissionEvent>(events[2].body).attempt;
  return result;
}
Result<JournalCursor> RetainedState::append_impl(
    JournalCursor expected, std::span<const ByteView> sources,
    std::span<const RetainedEvent> events, bool diagnostic, bool settlement,
    std::span<PhysicalJournalRecord> acknowledged, bool *proposed_batch_acknowledged,
    RawRetainedExec *raw_exec) try {
  auto *const raw_bank = raw_region_prepaid_bank();
  RawRegion timing{
      raw_regions::retained_append,
      {.retained_sources = sources.size(), .retained_events = events.size()}};
  RawRegionPrepaidScope pause{nullptr};
  auto origin = raw_origin_;
  origin.parent = {timing.lane_epoch(), timing.begin_ordinal()};
  const auto selected_catalog = archive_;
  RawRetainedFinally restore_catalog{[&]() noexcept {
    if (archive_ && archive_ != selected_catalog)
      archive_->raw_origin_ = raw_origin_;
  }};
  RawRetainedOriginScope owner_origin{raw_origin_, origin};
  RawRetainedOriginScope journal_origin{journal_->raw_origin_, origin};
  std::optional<RawRetainedOriginScope> catalog_origin;
  if (selected_catalog)
    catalog_origin.emplace(selected_catalog->raw_origin_, origin);
  RawRetainedEvent custody{origin, {3101, 91}};
  RawRetainedFinally final_custody{[&] {
    const auto actual = cursor();
    const bool prepared = prepared_.has_value();
    const bool pending = pending_proposal_.has_value();
    const bool protected_credit = settlement_credit_.has_value();
    RawRetainedFields fields;
    fields.cursor(actual);
    fields.native(prepared);
    fields.native(pending);
    fields.native(protected_credit);
    if (settlement_credit_) {
      fields.native(settlement_credit_->max_file_bytes);
      fields.native(settlement_credit_->max_records);
    }
    custody.publish(fields.view());
  }};
  const auto source_count = sources.size(), event_count = events.size();
  const bool protected_credit = settlement_credit_.has_value();
  RawRetainedFields arguments;
  arguments.cursor(expected);
  arguments.native(source_count);
  arguments.native(event_count);
  arguments.native(diagnostic);
  arguments.native(settlement);
  arguments.native(protected_credit);
  if (settlement_credit_) {
    arguments.native(settlement_credit_->max_file_bytes);
    arguments.native(settlement_credit_->max_records);
  }
  (void)raw_retained_event(origin, {3100, 107}, arguments.view());
  auto rejected = [&](Error error) {
    timing.error(error);
    timing.outcome(RawRegionOutcome::rejected);
    return Result<JournalCursor>::failure(error);
  };
  if ((proposed_batch_acknowledged &&
       (acknowledged.size() < sources.size() ||
        acknowledged.size() - sources.size() != events.size())) ||
      (!proposed_batch_acknowledged && !acknowledged.empty()))
    return rejected({ErrorCode::invalid_range});
  if (raw_exec) {
    if (&raw_exec->root_ != this || !sources.empty() || events.size() != 3 ||
        !std::holds_alternative<DecisionEvent>(events[0].body) ||
        !std::holds_alternative<InvocationEvent>(events[1].body) ||
        !std::holds_alternative<AttemptAdmissionEvent>(events[2].body) ||
        !std::get<AttemptAdmissionEvent>(events[2].body).input_from_invocation)
      return rejected({ErrorCode::invalid_range});
  }
  timing.observe({RawObservationKind::journal_state,
                  journal_->physical_records().size(), 0, expected.sequence});
  settlement = settlement || maintenance_;
  if (in_transaction_) {
    return rejected({ErrorCode::busy});
  }
  if (state() != JournalWriterState::live) {
    return rejected({ErrorCode::audit_unavailable});
  }
  // Publication failure cannot turn a compact suffix into unlimited resident history.
  // Checkpoint runs before admission; already admitted settlement remains writable.
  if (compact_ && saved_ && !settlement && !diagnostic && !checkpoint_running_) {
    auto tail_bytes = cursor().end_offset - saved_->boundary.end_offset;
    if (maintenance_checkpoint_ && cursor().sequence >= maintenance_retry_sequence_ &&
        (committed_.facts.size() + committed_.sources.size() >= 128 ||
         tail_bytes >= 8 * 1024 * 1024)) {
      checkpoint_running_ = true;
      struct Reset {
        bool &flag;
        ~Reset() { flag = false; }
      } reset{checkpoint_running_};
      const RawEventRef maintenance_origin{timing.lane_epoch(), timing.begin_ordinal()};
      std::optional<Error> retarget_error;
      auto maintained = [&]() -> Result<void> {
        // Mandatory cleanup never begins an optional checkpoint acquisition.
        // Ordinary exec pauses only its borrowed banks; the actual checkpoint
        // keeps its ordinary caller admission and can replace the catalog.
        if (raw_origin_.selected &&
            raw_origin_.input == RawRetainedOrigin::Input::no_new)
          return Result<void>::failure({ErrorCode::capacity});
        RawStorageLoanPause physical_pause{raw_storage_selection()};
        const auto borrowed = raw_origin_;
        auto ordinary = borrowed;
        ordinary.loan = nullptr;
        ordinary.input = RawRetainedOrigin::Input::ordinary;
        const auto previous_archive = archive_;
        RawRetainedFinally restore_current_catalog{[&]() noexcept {
          if (archive_ && archive_ != previous_archive)
            archive_->raw_origin_ = borrowed;
        }};
        RawRetainedOriginScope ordinary_root{raw_origin_, ordinary};
        RawRetainedOriginScope ordinary_journal{journal_->raw_origin_, ordinary};
        std::optional<RawRetainedOriginScope> ordinary_catalog;
        if (previous_archive)
          ordinary_catalog.emplace(previous_archive->raw_origin_, ordinary);
        try {
          auto actual = maintenance_checkpoint_(maintenance_origin);
          const auto restored = physical_pause.restore(
              raw_storage_selection(), archive_ ? archive_->count() : 0);
          if (!restored.has_value())
            retarget_error = restored.error();
          // Preserve an actual callback error. A successful callback followed
          // by a refused retarget cannot authorize the subsequent acquisition.
          return actual.has_value() ? restored : actual;
        } catch (...) {
          (void)physical_pause.restore(raw_storage_selection(),
                                       archive_ ? archive_->count() : 0);
          throw;
        }
      }();
      maintenance_error_ = maintained.has_value()
                               ? std::nullopt
                               : std::optional<Error>{maintained.error()};
      if (retarget_error)
        return rejected(maintained.has_value() ? *retarget_error : maintained.error());
      maintenance_retry_sequence_ =
          maintained.has_value()
              ? 0
              : (cursor().sequence > UINT64_MAX - 64 ? UINT64_MAX
                                                     : cursor().sequence + 64);
    }
    const auto tail_count = committed_.facts.size() + committed_.sources.size();
    const auto credit = settlement_credit_.value_or(JournalCapacity{0, 0});
    if (tail_count > 1024 || sources.size() > 1024 - tail_count ||
        events.size() > 1024 - tail_count - sources.size() ||
        credit.max_records > 1024 - tail_count - sources.size() - events.size()) {
      maintenance_error_ = Error{ErrorCode::capacity};
      return rejected(*maintenance_error_);
    }
  }
  const auto current = cursor();
  const bool stale = current.journal != expected.journal ||
                     current.sequence != expected.sequence ||
                     current.end_offset != expected.end_offset;
  if (sources.empty() && events.empty()) {
    return rejected({ErrorCode::invalid_range});
  }
  if (journal_->usage().indexed_records > capacity_.max_records)
    return rejected({ErrorCode::capacity});
  const auto remaining = capacity_.max_records - journal_->usage().indexed_records;
  if (sources.size() > remaining || events.size() > remaining - sources.size()) {
    return rejected({ErrorCode::capacity});
  }
  const auto credit = settlement_credit_.value_or(JournalCapacity{0, 0});
  const auto count = sources.size() + events.size(); // Sum already bounded above.
  if (settlement_credit_) {
    if (settlement ? count > credit.max_records
                   : credit.max_records > remaining - count)
      return rejected({ErrorCode::capacity});
  }
  try {
    std::vector<std::vector<std::byte>> encoded;
    encoded.reserve(events.size());
    for (const auto &event : events) {
      auto bytes = encode_retained_event(event, journal_->header().limits.max_payload,
                                         &raw_origin_);
      if (!bytes.has_value()) {
        return rejected(bytes.error());
      }
      timing.observe({RawObservationKind::encoded_event_bytes, 0, bytes.value().size(),
                      expected.sequence});
      encoded.push_back(std::move(bytes).value());
    }
    if (raw_exec) {
      const auto grant = raw_exec->prepare(encoded);
      if (!grant.has_value())
        return rejected(grant.error());
    }
    if (compact_ && saved_ && !settlement && !diagnostic && !checkpoint_running_) {
      constexpr std::uint64_t bound = 32 * 1024 * 1024;
      auto used = cursor().end_offset - saved_->boundary.end_offset;
      auto charge = [&](std::uint64_t n) {
        if (used > bound || n > bound - used)
          return false;
        used += n;
        return true;
      };
      bool room = charge(56) && charge(credit.max_file_bytes);
      for (const auto source : sources)
        room = room && charge(source.size()) && charge(journal_frame_header_size);
      for (const auto &event : encoded)
        room = room && charge(event.size()) && charge(journal_frame_header_size);
      if (!room) {
        maintenance_error_ = Error{ErrorCode::capacity};
        return rejected(*maintenance_error_);
      }
    }
    if (stale) {
      if (!diagnostic) {
        const auto captured = retain_rejection(
            expected, sources, encoded, {ErrorCode::conflict}, nullptr, raw_bank);
        if (!captured.has_value()) {
          return rejected(captured.error());
        }
      }
      return rejected({ErrorCode::conflict});
    }
    if (sources.size() >= UINT64_MAX - current.sequence ||
        events.size() >= UINT64_MAX - current.sequence - sources.size()) {
      return rejected({ErrorCode::overflow});
    }
    auto candidate = committed_;
    auto offset = current.end_offset;
    auto sequence = current.sequence + 1;
    std::size_t batch_bytes = 56;
    const auto limits = journal_->header().limits;
    std::vector<JournalDraft> drafts;
    const auto add_size = [&](std::size_t size) {
      if (size > limits.max_payload || size > limits.max_batch_bytes - 32 ||
          batch_bytes > limits.max_batch_bytes - 32 - size) {
        return false;
      }
      batch_bytes += 32 + size;
      return true;
    };
    for (const auto source : sources) {
      timing.observe(
          {RawObservationKind::source_bytes, 0, source.size(), expected.sequence});
      if (!has_room(candidate, 1))
        return rejected({ErrorCode::capacity});
      if (!add_size(source.size())) {
        return rejected({ErrorCode::capacity});
      }
      candidate.sources.push_back({journal_->header().environment, current.journal,
                                   FrameKind::source, sequence++, current.sequence + 1,
                                   offset + journal_frame_header_size,
                                   static_cast<std::uint32_t>(source.size())});
      const auto &selected_source = candidate.sources.back();
      RawRetainedFields selected;
      selected.reference(selected_source);
      selected.native(selected_source.payload_offset);
      selected.native(selected_source.payload_size);
      (void)raw_retained_event(origin, {3114, 76}, selected.view());
      index_source(candidate, candidate.sources.size() - 1);
      offset += journal_frame_header_size + source.size();
      drafts.push_back({FrameKind::source, source});
    }
    std::optional<Error> invalid;
    const RetainedEvent *conflict = nullptr;
    for (std::size_t index = 0; index < events.size(); ++index) {
      if (!add_size(encoded[index].size())) {
        return rejected({ErrorCode::capacity});
      }
      const auto applied =
          apply(candidate, events[index], {current.journal, sequence++},
                RetainedEvidence::uncertain);
      if (!applied.has_value()) {
        invalid = applied.error();
        auto previous = existing(candidate, events[index]);
        if (!previous)
          if (const auto *uncertain = existing_uncertain(candidate, events[index]))
            previous = *uncertain;
        if (previous && previous->event != events[index])
          conflict = &events[index];
        break;
      }
      drafts.push_back({FrameKind::semantic, encoded[index]});
    }
    if (invalid) {
      if (invalid->code == ErrorCode::capacity)
        return rejected(*invalid);
      if (!diagnostic) {
        const auto retained =
            retain_rejection(expected, sources, encoded, *invalid, conflict, raw_bank);
        if (!retained.has_value()) {
          return rejected(retained.error());
        }
      }
      return rejected(*invalid);
    }
    if (current.end_offset > capacity_.max_file_bytes ||
        batch_bytes > capacity_.max_file_bytes - current.end_offset)
      return rejected({ErrorCode::capacity});
    if (settlement_credit_) {
      const auto bytes_left = capacity_.max_file_bytes - current.end_offset;
      if (settlement ? batch_bytes > credit.max_file_bytes
                     : credit.max_file_bytes > bytes_left - batch_bytes)
        return rejected({ErrorCode::capacity});
    }
    if (!diagnostic) {
      auto proposal = encode_retained_proposal(expected, sources, encoded,
                                               capacity_.max_file_bytes);
      if (!proposal.has_value()) {
        return rejected(proposal.error());
      }
      const auto length = proposal.value().size();
      pending_proposal_.emplace(
          PendingProposal{{current.journal, current.sequence + 1, length,
                           CapturePurpose::ordinary_submission},
                          std::move(proposal).value()});
      const auto &capture = pending_proposal_->capture;
      RawRetainedFields selected;
      selected.add(capture.origin.bytes());
      selected.native(capture.first_sequence);
      selected.native(capture.proposal_length);
      selected.native(capture.purpose);
      (void)raw_retained_event(origin, {3102, 74}, selected.view());
    }
    TransactionGuard mutation{in_transaction_};
    const auto source_base = committed_.sources.size();
    const auto physical_base = journal_->physical_records().size();
    RawRetainedEvent prepared_event{origin, {3103, 71}};
    RawRetainedEvent published_event{origin, {3103, 71}};
    prepared_ = std::move(candidate);
    {
      const std::uint8_t stage = 1;
      const bool prepared = true, pending = pending_proposal_.has_value();
      RawRetainedFields selected;
      selected.native(stage);
      selected.native(prepared_->counter);
      selected.native(prepared_->query_root.offset);
      selected.native(prepared_->query_root.generation);
      selected.native(prepared_->query_root.checksum);
      selected.native(prepared);
      selected.native(pending);
      prepared_event.publish(selected.view());
    }
    const auto written = journal_->append(drafts);
    if (!written.has_value()) {
      timing.error(written.error());
      timing.outcome(state() == JournalWriterState::poisoned
                         ? RawRegionOutcome::uncertain
                         : RawRegionOutcome::rejected);
      if (state() != JournalWriterState::poisoned) {
        prepared_.reset();
        if (!diagnostic) {
          pending_proposal_.reset();
        }
      }
      return written;
    }
    for (std::size_t i = committed_.facts.size(); i < prepared_->facts.size(); ++i) {
      auto &fact = prepared_->facts[i];
      if (fact.evidence == RetainedEvidence::uncertain) {
        fact.evidence = RetainedEvidence::live;
      }
    }
    const auto physical = journal_->physical_records();
    // This output belongs only to the caller's actual proposed batch. Diagnostic
    // child appends receive neither this span nor its acceptance flag.
    if (proposed_batch_acknowledged) {
      for (std::size_t index = 0; index < acknowledged.size(); ++index)
        acknowledged[index] = physical[physical_base + index];
      *proposed_batch_acknowledged = true;
    }
    for (std::size_t index = 0; index < sources.size(); ++index) {
      prepared_->sources[source_base + index] = physical[physical_base + index];
    }
    if (settlement && settlement_credit_) {
      settlement_credit_->max_file_bytes -= batch_bytes;
      settlement_credit_->max_records -= count;
    }
    committed_.swap(*prepared_);
    prepared_.reset();
    if (!diagnostic) {
      pending_proposal_.reset();
    }
    {
      const std::uint8_t stage = 2;
      const bool prepared = false, pending = pending_proposal_.has_value();
      RawRetainedFields selected;
      selected.native(stage);
      selected.native(committed_.counter);
      selected.native(committed_.query_root.offset);
      selected.native(committed_.query_root.generation);
      selected.native(committed_.query_root.checksum);
      selected.native(prepared);
      selected.native(pending);
      published_event.publish(selected.view());
    }
    timing.outcome(RawRegionOutcome::success);
    return written;
  } catch (const Error &error) {
    return rejected(error);
  } catch (const std::bad_alloc &) {
    return rejected({ErrorCode::allocation});
  }
} catch (const Error &error) {
  return Result<JournalCursor>::failure(error);
} catch (const std::bad_alloc &) {
  return Result<JournalCursor>::failure({ErrorCode::allocation});
}

Result<void> RetainedState::retain_rejection(
    JournalCursor expected, std::span<const ByteView> sources,
    std::span<const std::vector<std::byte>> events, Error reason,
    const RetainedEvent *conflict, RawCapacityClaim *raw_bank) {
  const auto source_count = sources.size(), event_count = events.size();
  RawRetainedFields arguments;
  arguments.cursor(expected);
  arguments.native(source_count);
  arguments.native(event_count);
  arguments.native(reason.code);
  arguments.native(reason.detail);
  RawRetainedPair trace{raw_origin_, {3121, 3122}, 150, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  bool prefix_retained = false;
  auto answer = trace.run([&]() -> Result<void> {
    // Exact diagnostic proposal: ARPROP01, expected journal/sequence/end,
    // source count + length/bytes, then event count + length/encoded event bytes.

    try {
      const auto limits = journal_->header().limits;
      const auto chunk_limit = limits.max_payload;
      RetainedBody body = RejectedSubmissionEvent{reason};
      if (conflict) {
        const auto identity = key(conflict->body);
        if (identity && (identity->kind == RetainedKind::decision ||
                         identity->kind == RetainedKind::invocation ||
                         identity->kind == RetainedKind::attempt_admitted)) {
          body = IdentityConflictEvent{identity->kind, identity->identity, {}};
        }
      }
      std::array retained{RetainedEvent{{}, std::move(body)}};
      const auto empty_marker =
          encode_retained_event(retained[0], chunk_limit, &raw_origin_);
      if (!empty_marker.has_value()) {
        return Result<void>::failure(empty_marker.error());
      }
      const auto max_references = (chunk_limit - empty_marker.value().size()) / 24;
      const auto limit = std::min<std::uint64_t>(
          capacity_.max_file_bytes,
          static_cast<std::uint64_t>(max_references) * chunk_limit);
      auto serialized = encode_retained_proposal(expected, sources, events, limit);
      if (!serialized.has_value()) {
        return Result<void>::failure(serialized.error());
      }
      auto proposal = std::move(serialized).value();
      std::vector<ByteView> chunks;
      std::vector<SourceReference> references;
      const auto current = cursor();
      for (std::size_t offset = 0; offset < proposal.size();) {
        const auto count = std::min<std::size_t>(chunk_limit, proposal.size() - offset);
        references.push_back({current.journal, 1});
        chunks.push_back(ByteView{proposal}.subspan(offset, count));
        offset += count;
      }
      retained[0].dependencies = std::move(references);
      const auto marker = encode_retained_event(retained[0], chunk_limit, &raw_origin_);
      if (!marker.has_value()) {
        return Result<void>::failure(marker.error());
      }
      const auto source_bytes = static_cast<std::uint64_t>(proposal.size()) +
                                journal_frame_header_size * chunks.size();
      const auto marker_bytes = journal_frame_header_size + marker.value().size() + 56;
      const bool together = source_bytes + marker_bytes <= limits.max_batch_bytes;
      std::vector<std::size_t> groups;
      if (!together) {
        std::size_t count = 0;
        std::size_t batch_bytes = 56;
        for (const auto chunk : chunks) {
          const auto frame_bytes = journal_frame_header_size + chunk.size();
          if (frame_bytes > limits.max_batch_bytes - batch_bytes) {
            groups.push_back(count);
            count = 0;
            batch_bytes = 56;
          }
          ++count;
          batch_bytes += frame_bytes;
        }
        groups.push_back(count);
      }
      const auto frames = chunks.size() + groups.size() + 2;
      if (frames > UINT64_MAX - current.sequence) {
        return Result<void>::failure({ErrorCode::overflow});
      }
      const auto remaining = capacity_.max_records - journal_->usage().indexed_records;
      const auto total_bytes = source_bytes + marker_bytes + 56 * groups.size();
      if (chunks.size() >= remaining ||
          total_bytes > capacity_.max_file_bytes - current.end_offset) {
        return Result<void>::failure({ErrorCode::capacity});
      }
      auto sequence = current.sequence + 1;
      // Views into proposal remain valid when its allocation moves into the owner.
      // Inner source-only/marker append_impl calls never replace this outer packet.
      pending_proposal_.emplace(
          PendingProposal{{current.journal, sequence, proposal.size(),
                           CapturePurpose::rejected_proposal},
                          std::move(proposal)});
      if (together) {
        for (auto &reference : retained[0].dependencies) {
          reference.sequence = sequence++;
        }
        const auto result = [&] {
          RawRegionPrepaidScope selected{raw_bank};
          return append_impl(current, chunks, retained, true);
        }();
        if (result.has_value() || state() != JournalWriterState::poisoned) {
          pending_proposal_.reset();
        }
        return result.has_value() ? Result<void>::success()
                                  : Result<void>::failure(result.error());
      }
      std::size_t index = 0;
      for (const auto count : groups) {
        for (std::size_t offset = 0; offset < count; ++offset) {
          retained[0].dependencies[index++].sequence = sequence++;
        }
        ++sequence; // Every intermediate commit consumes a sequence too.
      }
      index = 0;
      for (const auto count : groups) {
        RawRetainedFields selected_group;
        selected_group.native(index);
        selected_group.native(count);
        (void)raw_retained_event(raw_origin_, {3123, 56}, selected_group.view());
        const auto result = [&] {
          RawRegionPrepaidScope selected{raw_bank};
          return append_impl(cursor(), std::span{chunks}.subspan(index, count), {},
                             true);
        }();
        if (!result.has_value()) {
          recording_failed_ = prefix_retained;
          if (!prefix_retained && state() != JournalWriterState::poisoned) {
            pending_proposal_.reset();
          }
          return Result<void>::failure(result.error());
        }
        prefix_retained = true;
        index += count;
      }
      const auto result = [&] {
        RawRegionPrepaidScope selected{raw_bank};
        return append_impl(cursor(), {}, retained, true);
      }();
      if (!result.has_value()) {
        recording_failed_ = true;
      } else {
        pending_proposal_.reset();
      }
      return result.has_value() ? Result<void>::success()
                                : Result<void>::failure(result.error());
    } catch (const std::bad_alloc &) {
      if (prefix_retained) {
        recording_failed_ = true;
      } else if (state() != JournalWriterState::poisoned) {
        pending_proposal_.reset();
      }
      return Result<void>::failure({ErrorCode::allocation});
    }
  });
  const bool pending = pending_proposal_.has_value();
  const auto current_state = state();
  RawRetainedFields returned;
  returned.native(prefix_retained);
  returned.native(pending);
  returned.native(current_state);
  if (answer.has_value())
    trace.finish(0, returned.view());
  else
    trace.finish(1, returned.view(), answer.error());
  return answer;
}

Result<Submission> RetainedState::submit(const RetainedEvent &event) {
  return submit_impl(event, false);
}
Result<Submission>
RetainedState::submit_impl(const RetainedEvent &proposal, bool settlement,
                           std::span<PhysicalJournalRecord> acknowledged,
                           bool *proposed_batch_acknowledged) {
  RawRetainedFields arguments;
  arguments.native(settlement);
  RawRetainedPair trace{raw_origin_, {3119, 3120}, 117, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<Submission> {
    if ((proposed_batch_acknowledged && acknowledged.size() != 1) ||
        (!proposed_batch_acknowledged && !acknowledged.empty()))
      return Result<Submission>::failure({ErrorCode::invalid_range});
    try {
      auto event = proposal;
      resolve_admission_input(event, [&](InvocationId identity) {
        const auto invocation =
            lookup(visible(), 1, RetainedKind::invocation, identity.bytes());
        return invocation ? std::optional<InvocationEvent>{std::get<InvocationEvent>(
                                invocation->event.body)}
                          : std::nullopt;
      });
      if (const auto previous = existing(visible(), event);
          previous && previous->event == event) {
        return Result<Submission>::success(
            {true, previous->record, previous->evidence});
      }
      if (const auto *previous = existing_uncertain(visible(), event);
          previous && previous->event == event) {
        return Result<Submission>::success(
            {true, previous->record, RetainedEvidence::uncertain});
      }
      const auto current = cursor();
      const std::array events{event};
      const auto written = append_impl(current, {}, events, false, settlement,
                                       acknowledged, proposed_batch_acknowledged);
      if (!written.has_value()) {
        return Result<Submission>::failure(written.error());
      }
      return Result<Submission>::success(
          {false, {current.journal, current.sequence + 1}, RetainedEvidence::live});
    } catch (const Error &error) {
      return Result<Submission>::failure(error);
    } catch (const std::bad_alloc &) {
      return Result<Submission>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &value = answer.value();
    returned.native(value.existing);
    returned.reference(value.record);
    returned.native(value.evidence);
  }
  trace.result(answer, returned.view());
  return answer;
}
Result<AttemptState> RetainedState::attempt(OperationAttemptId identity) const {
  RawRetainedFields arguments;
  arguments.add(identity.bytes());
  RawRetainedPair trace{raw_origin_, {3142, 3143}, 188, arguments.view()};
  const auto selected_catalog = archive_;
  RawRetainedFinally restore_catalog{[&]() noexcept {
    if (archive_ && archive_ != selected_catalog)
      archive_->raw_origin_ = raw_origin_;
  }};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope journal_scope{journal_->raw_origin_, trace.child()};
  std::optional<RawRetainedOriginScope> catalog_scope;
  if (selected_catalog)
    catalog_scope.emplace(selected_catalog->raw_origin_, trace.child());
  std::optional<RawRetainedOriginScope> query_origin;
  if (query_index_)
    query_origin.emplace(query_index_->raw_origin_, trace.child());
  auto answer = trace.run([&]() -> Result<AttemptState> {
    try {
      const auto &snapshot = visible();
      if (query_index_ || archive_) {
        const auto admission =
            lookup(snapshot, 1, RetainedKind::attempt_admitted, identity.bytes());
        if (!admission)
          return Result<AttemptState>::failure({ErrorCode::stale_handle});
        const auto opened =
            lookup(snapshot, 1, RetainedKind::attempt_open, identity.bytes());
        const auto observed =
            lookup(snapshot, 2, RetainedKind::observation, identity.bytes());
        const auto receipt =
            lookup(snapshot, 1, RetainedKind::adapter_receipt, identity.bytes());
        auto evidence = admission->evidence;
        const bool uncertain_observation =
            observed && observed->evidence == RetainedEvidence::uncertain;
        if (query_index_) {
          auto inspected = query_index_->range(
              snapshot.query_root, ordinal_key(9, identity.bytes(), 0),
              snapshot.facts.size(), [&](RecoveryIndexEntry entry) -> Result<bool> {
                if (entry.key[0] != std::byte{9} ||
                    !std::equal(identity.bytes().begin(), identity.bytes().end(),
                                entry.key.begin() + 2))
                  return Result<bool>::success(false);
                if (entry.value >= snapshot.facts.size())
                  return Result<bool>::failure({ErrorCode::corrupt});
                const auto &related =
                    snapshot.facts[static_cast<std::size_t>(entry.value)];
                if (related.evidence == RetainedEvidence::uncertain ||
                    (evidence == RetainedEvidence::live &&
                     related.evidence != RetainedEvidence::live))
                  evidence = related.evidence;
                return Result<bool>::success(true);
              });
          if (!inspected.has_value())
            return Result<AttemptState>::failure(inspected.error());
        } else {
          for (const auto &related : snapshot.facts) {
            const bool matches = std::visit(
                [&](const auto &body) {
                  using T = std::decay_t<decltype(body)>;
                  if constexpr (std::is_same_v<T, AttemptOpenEvent> ||
                                std::is_same_v<T, AttemptObservationEvent> ||
                                std::is_same_v<T, AdapterReceiptEvent>)
                    return body.attempt == identity;
                  return false;
                },
                related.event.body);
            if (matches && (related.evidence == RetainedEvidence::uncertain ||
                            (evidence == RetainedEvidence::live &&
                             related.evidence != RetainedEvidence::live)))
              evidence = related.evidence;
          }
        }
        const auto *observation =
            observed ? std::get_if<AttemptObservationEvent>(&observed->event.body)
                     : nullptr;
        const bool terminal = observation && !uncertain_observation &&
                              observation->phase == AttemptPhase::terminal;
        return Result<AttemptState>::success(
            {std::get<AttemptAdmissionEvent>(admission->event.body), opened.has_value(),
             observation ? std::optional<AttemptObservationEvent>{*observation}
                         : std::nullopt,
             receipt ? std::optional<AdapterReceiptEvent>{std::get<AdapterReceiptEvent>(
                           receipt->event.body)}
                     : std::nullopt,
             evidence,
             (!reconciled_ || recording_failed_ ||
              evidence == RetainedEvidence::uncertain) &&
                 !terminal});
      }
      for (const auto &fact : snapshot.facts) {
        const auto *admitted = std::get_if<AttemptAdmissionEvent>(&fact.event.body);
        if (!admitted || admitted->attempt != identity) {
          continue;
        }
        const auto *opened =
            find_event<AttemptOpenEvent>(snapshot.facts, [&](const auto &value) {
              return value.attempt == identity;
            });
        const auto *observed =
            find_event<AttemptObservationEvent>(snapshot.facts, [&](const auto &value) {
              return value.attempt == identity;
            });
        auto evidence = fact.evidence;
        bool uncertain_observation = false;
        for (const auto &related : snapshot.facts) {
          const bool matches = std::visit(
              [&](const auto &event) {
                using T = std::decay_t<decltype(event)>;
                if constexpr (std::is_same_v<T, AttemptOpenEvent> ||
                              std::is_same_v<T, AttemptObservationEvent> ||
                              std::is_same_v<T, AdapterReceiptEvent>) {
                  return event.attempt == identity;
                }
                return false;
              },
              related.event.body);
          if (!matches) {
            continue;
          }
          if (related.evidence == RetainedEvidence::uncertain ||
              (evidence == RetainedEvidence::live &&
               related.evidence != RetainedEvidence::live)) {
            evidence = related.evidence;
          }
          if (std::get_if<AttemptObservationEvent>(&related.event.body) == observed) {
            uncertain_observation = related.evidence == RetainedEvidence::uncertain;
          }
        }
        const bool terminal = observed && !uncertain_observation &&
                              observed->phase == AttemptPhase::terminal;
        const auto *receipt =
            find_event<AdapterReceiptEvent>(snapshot.facts, [&](const auto &value) {
              return value.attempt == identity;
            });
        return Result<AttemptState>::success(
            {*admitted, opened != nullptr,
             observed ? std::optional<AttemptObservationEvent>{*observed}
                      : std::nullopt,
             receipt ? std::optional<AdapterReceiptEvent>{*receipt} : std::nullopt,
             evidence,
             (!reconciled_ || recording_failed_ ||
              evidence == RetainedEvidence::uncertain) &&
                 !terminal});
      }
      return Result<AttemptState>::failure({ErrorCode::stale_handle});
    } catch (const Error &error) {
      return Result<AttemptState>::failure(error);
    } catch (const std::bad_alloc &) {
      return Result<AttemptState>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &value = answer.value();
    const auto input = value.admission.input.size();
    const bool observed = value.observation.has_value(),
               receipt = value.receipt.has_value();
    returned.add(value.admission.attempt.bytes());
    returned.add(value.admission.invocation.bytes());
    returned.add(value.admission.decision.bytes());
    returned.native(input);
    returned.native(value.admission.input_from_invocation);
    returned.native(value.opened);
    returned.native(observed);
    std::size_t observation_size = 0;
    if (value.observation) {
      observation_size = value.observation->observation.size();
      returned.native(value.observation->phase);
      returned.native(value.observation->disposition);
      returned.native(observation_size);
    }
    returned.native(receipt);
    const bool failure = value.receipt && value.receipt->failure.has_value();
    if (value.receipt) {
      returned.native(failure);
      if (value.receipt->failure) {
        returned.native(value.receipt->failure->code);
        returned.native(value.receipt->failure->detail);
      }
    }
    returned.native(value.evidence);
    returned.native(value.reconciliation_required);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<DecisionEvent> RetainedState::decision(DecisionId identity) const {
  RawRetainedFields arguments;
  arguments.add(identity.bytes());
  RawRetainedPair trace{raw_origin_, {3138, 3139}, 218, arguments.view()};
  const auto selected_catalog = archive_;
  RawRetainedFinally restore_catalog{[&]() noexcept {
    if (archive_ && archive_ != selected_catalog)
      archive_->raw_origin_ = raw_origin_;
  }};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope journal_scope{journal_->raw_origin_, trace.child()};
  std::optional<RawRetainedOriginScope> catalog_scope;
  if (selected_catalog)
    catalog_scope.emplace(selected_catalog->raw_origin_, trace.child());
  auto answer = trace.run([&]() -> Result<DecisionEvent> {
    try {
      auto fact = lookup(visible(), 1, RetainedKind::decision, identity.bytes());
      if (!fact)
        return Result<DecisionEvent>::failure({ErrorCode::stale_handle});
      const auto *body = std::get_if<DecisionEvent>(&fact->event.body);
      if (!body)
        return Result<DecisionEvent>::failure({ErrorCode::corrupt});
      return Result<DecisionEvent>::success(*body);
    } catch (const Error &error) {
      return Result<DecisionEvent>::failure(error);
    } catch (const std::bad_alloc &) {
      return Result<DecisionEvent>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &value = answer.value();
    returned.add(value.decision.bytes());
    returned.add(value.actor.bytes());
    returned.add(value.conversation.bytes());
    returned.add(value.workflow.bytes());
    returned.add(value.definition.bytes());
    returned.add(value.context.bytes());
    const auto planned = value.planned_invocations.size(),
               continuation = value.continuation.size();
    returned.native(planned);
    returned.native(continuation);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<InvocationEvent> RetainedState::invocation(InvocationId identity) const {
  RawRetainedFields arguments;
  arguments.add(identity.bytes());
  RawRetainedPair trace{raw_origin_, {3158, 3159}, 162, arguments.view()};
  const auto selected_catalog = archive_;
  RawRetainedFinally restore_catalog{[&]() noexcept {
    if (archive_ && archive_ != selected_catalog)
      archive_->raw_origin_ = raw_origin_;
  }};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope journal_scope{journal_->raw_origin_, trace.child()};
  std::optional<RawRetainedOriginScope> catalog_scope;
  if (selected_catalog)
    catalog_scope.emplace(selected_catalog->raw_origin_, trace.child());
  auto answer = trace.run([&]() -> Result<InvocationEvent> {
    try {
      auto fact = lookup(visible(), 1, RetainedKind::invocation, identity.bytes());
      if (!fact)
        return Result<InvocationEvent>::failure({ErrorCode::stale_handle});
      const auto *body = std::get_if<InvocationEvent>(&fact->event.body);
      if (!body)
        return Result<InvocationEvent>::failure({ErrorCode::corrupt});
      return Result<InvocationEvent>::success(*body);
    } catch (const Error &error) {
      return Result<InvocationEvent>::failure(error);
    } catch (const std::bad_alloc &) {
      return Result<InvocationEvent>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &value = answer.value();
    const auto input = value.input.size();
    returned.add(value.invocation.bytes());
    returned.add(value.decision.bytes());
    returned.add(value.definition.bytes());
    returned.native(input);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<std::vector<AttemptState>> RetainedState::unresolved_attempts() const {
  const auto current = cursor();
  RawRetainedFields arguments;
  arguments.cursor(current);
  RawRetainedPair trace{raw_origin_, {3156, 3157}, 130, arguments.view()};
  const auto selected_catalog = archive_;
  RawRetainedFinally restore_catalog{[&]() noexcept {
    if (archive_ && archive_ != selected_catalog)
      archive_->raw_origin_ = raw_origin_;
  }};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope journal_scope{journal_->raw_origin_, trace.child()};
  std::optional<RawRetainedOriginScope> catalog_scope;
  if (selected_catalog)
    catalog_scope.emplace(selected_catalog->raw_origin_, trace.child());
  auto answer = trace.run([&]() -> Result<std::vector<AttemptState>> {
    try {
      std::vector<AttemptState> result;
      const auto add = [&](const RetainedFact &fact) {
        if (const auto *admitted =
                std::get_if<AttemptAdmissionEvent>(&fact.event.body)) {
          auto inspected = attempt(admitted->attempt);
          if (!inspected.has_value())
            throw inspected.error();
          if (!inspected.value().observation ||
              inspected.value().observation->phase != AttemptPhase::terminal)
            result.push_back(std::move(inspected).value());
        }
      };
      // The root closure contains precisely the nonterminal admissions at its
      // boundary. An accepted suffix can close them; always apply the disk/tail
      // transition predicates, never treat closure membership as dispatch rights.
      if (saved_)
        for (const auto &fact : saved_->unresolved)
          add(fact);
      for (const auto &fact : visible().facts) {
        if (saved_ && fact.record.journal == saved_->boundary.journal &&
            fact.record.sequence <= saved_->boundary.sequence)
          continue;
        add(fact);
      }
      return Result<std::vector<AttemptState>>::success(std::move(result));
    } catch (const Error &error) {
      return Result<std::vector<AttemptState>>::failure(error);
    } catch (const std::bad_alloc &) {
      return Result<std::vector<AttemptState>>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto count = answer.value().size();
    returned.native(count);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<DispatchReport> RetainedState::dispatch(OperationAttemptId identity,
                                               EffectBoundary &boundary) {
  return dispatch(identity, boundary, nullptr);
}
Result<DispatchReport> RetainedState::dispatch(OperationAttemptId identity,
                                               EffectBoundary &boundary,
                                               RawRetainedExec *raw_exec) {
  RawRetainedFields arguments;
  arguments.add(identity.bytes());
  RawRetainedPair trace{raw_origin_, {3152, 3153}, 127, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<DispatchReport> {
    if (raw_exec && (&raw_exec->root_ != this || !raw_exec->accepted_ ||
                     !raw_exec->attempt_ || *raw_exec->attempt_ != identity))
      return Result<DispatchReport>::failure({ErrorCode::wrong_incarnation});
    if (state() != JournalWriterState::live || in_transaction_) {
      return Result<DispatchReport>::failure({ErrorCode::audit_unavailable});
    }
    auto checked = attempt(identity);
    if (!checked.has_value()) {
      return Result<DispatchReport>::failure(checked.error());
    }
    auto owned = std::move(checked).value();
    if (owned.opened || owned.evidence != RetainedEvidence::live ||
        (owned.observation && owned.observation->phase == AttemptPhase::terminal)) {
      return Result<DispatchReport>::success(
          {false, Result<void>::success(), Result<void>::success()});
    }
    std::optional<DecisionEvent> checkpoint;
    try {
      const auto fact = lookup(committed_, 1, RetainedKind::decision,
                               owned.admission.decision.bytes());
      if (fact)
        if (const auto *body = std::get_if<DecisionEvent>(&fact->event.body))
          checkpoint = *body;
    } catch (const Error &error) {
      return Result<DispatchReport>::failure(error);
    }
    if (!checkpoint) {
      return Result<DispatchReport>::failure({ErrorCode::corrupt});
    }
    const auto actor = checkpoint->actor;
    const auto admitted = submit({{}, AttemptOpenEvent{identity}});
    if (!admitted.has_value()) {
      return Result<DispatchReport>::failure(admitted.error());
    }
    // Open is published before calling the adapter: nested calls and restart can
    // never repeat this attempt. Input is owned across any adapter reentry.
    const auto effect = boundary.dispatch({journal_->header().environment, actor,
                                           owned.admission.invocation, identity,
                                           owned.admission.input});
    std::optional<RawRetainedScope> receipt_scope;
    auto permission =
        raw_exec ? raw_exec->receipt_permission() : Result<void>::success();
    if (permission.has_value() && raw_exec && !raw_exec->full_exec() &&
        raw_exec->bundle_) {
      try {
        receipt_scope.emplace(*this, *raw_exec->bundle_->semantic,
                              raw_exec->bundle_->physical.get(), trace.reference());
      } catch (const Error &error) {
        permission = Result<void>::failure(error);
      }
    }
    const auto recorded =
        !permission.has_value()
            ? Result<Submission>::failure(permission.error())
            : submit_settlement(
                  {{},
                   AdapterReceiptEvent{identity,
                                       effect.has_value()
                                           ? std::nullopt
                                           : std::optional<Error>{effect.error()}}});
    if (!recorded.has_value()) {
      recording_failed_ = true;
    }
    return Result<DispatchReport>::success(
        {true, effect,
         recorded.has_value() ? Result<void>::success()
                              : Result<void>::failure(recorded.error())});
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &value = answer.value();
    const bool effect_ok = value.effect.has_value(),
               recording_ok = value.recording.has_value();
    returned.native(value.dispatched);
    returned.native(effect_ok);
    if (!effect_ok) {
      returned.native(value.effect.error().code);
      returned.native(value.effect.error().detail);
    }
    returned.native(recording_ok);
    if (!recording_ok) {
      returned.native(value.recording.error().code);
      returned.native(value.recording.error().detail);
    }
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<std::vector<std::byte>> RetainedState::source(SourceReference reference) {
  RawRetainedFields arguments;
  arguments.reference(reference);
  RawRetainedPair trace{raw_origin_, {3150, 3151}, 122, arguments.view()};
  const auto selected_catalog = archive_;
  RawRetainedFinally restore_catalog{[&]() noexcept {
    if (archive_ && archive_ != selected_catalog)
      archive_->raw_origin_ = raw_origin_;
  }};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope journal_scope{journal_->raw_origin_, trace.child()};
  std::optional<RawRetainedOriginScope> catalog_scope;
  if (selected_catalog)
    catalog_scope.emplace(selected_catalog->raw_origin_, trace.child());
  std::optional<RawRetainedOriginScope> query_origin;
  if (query_index_)
    query_origin.emplace(query_index_->raw_origin_, trace.child());
  auto answer = trace.run([&]() -> Result<std::vector<std::byte>> {
    if (archive_ && saved_ && reference.journal == saved_->boundary.journal &&
        reference.sequence <= saved_->boundary.sequence) {
      auto found =
          archive_->find(ordinal_key(5, reference.journal.bytes(), reference.sequence));
      if (!found.has_value())
        return Result<std::vector<std::byte>>::failure(found.error());
      const auto &found_value = found.value();
      if (found_value)
        return journal_->read_catalog_payload(found_value.value(), saved_->boundary);
      return Result<std::vector<std::byte>>::failure({ErrorCode::stale_handle});
    }
    if (query_index_) {
      const auto k = ordinal_key(5, reference.journal.bytes(), reference.sequence);
      auto found = query_index_->find(committed_.query_root, k);
      if (!found.has_value())
        return Result<std::vector<std::byte>>::failure(found.error());
      const auto &found_value = found.value();
      if (found_value) {
        if (found_value.value() >= committed_.sources.size())
          return Result<std::vector<std::byte>>::failure({ErrorCode::corrupt});
        const auto &record =
            committed_.sources[static_cast<std::size_t>(found_value.value())];
        if (record.journal != reference.journal ||
            record.sequence != reference.sequence)
          return Result<std::vector<std::byte>>::failure({ErrorCode::corrupt});
        auto *origin = segment(reference.journal);
        return origin
                   ? origin->read_payload(record)
                   : Result<std::vector<std::byte>>::failure({ErrorCode::stale_handle});
      }
      auto pending = query_index_->find(visible().query_root, k);
      if (!pending.has_value())
        return Result<std::vector<std::byte>>::failure(pending.error());
      return Result<std::vector<std::byte>>::failure(
          {pending.value() ? ErrorCode::audit_unavailable : ErrorCode::stale_handle});
    }
    for (const auto &record : committed_.sources) {
      if (record.journal == reference.journal &&
          record.sequence == reference.sequence) {
        auto *origin = segment(reference.journal);
        return origin
                   ? origin->read_payload(record)
                   : Result<std::vector<std::byte>>::failure({ErrorCode::stale_handle});
      }
    }
    for (const auto &record : visible().sources) {
      if (record.journal == reference.journal &&
          record.sequence == reference.sequence) {
        return Result<std::vector<std::byte>>::failure({ErrorCode::audit_unavailable});
      }
    }
    return Result<std::vector<std::byte>>::failure({ErrorCode::stale_handle});
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto count = answer.value().size();
    returned.native(count);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<void> RetainedState::confirm_recovery() {
  const auto current = cursor();
  RawRetainedFields arguments;
  arguments.cursor(current);
  arguments.native(reconciled_);
  RawRetainedPair trace{raw_origin_, {3134, 3135}, 158, arguments.view()};
  const auto selected_catalog = archive_;
  RawRetainedFinally restore_catalog{[&]() noexcept {
    if (archive_ && archive_ != selected_catalog)
      archive_->raw_origin_ = raw_origin_;
  }};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope journal_scope{journal_->raw_origin_, trace.child()};
  std::optional<RawRetainedOriginScope> catalog_scope;
  if (selected_catalog)
    catalog_scope.emplace(selected_catalog->raw_origin_, trace.child());
  auto answer = trace.run([&]() -> Result<void> {
    if (state() != JournalWriterState::recovery_pending || !prepared_) {
      return Result<void>::failure({ErrorCode::audit_unavailable});
    }
    const auto synchronized = journal_->confirm_recovery();
    if (!synchronized.has_value()) {
      return synchronized;
    }
    for (auto &fact : prepared_->facts) {
      fact.evidence = RetainedEvidence::recovered;
    }
    committed_.swap(*prepared_);
    prepared_.reset();
    return Result<void>::success();
  });
  const auto final_cursor = cursor();
  const auto final_state = state();
  RawRetainedFields returned;
  returned.cursor(final_cursor);
  returned.native(final_state);
  returned.native(reconciled_);
  returned.native(recording_failed_);
  trace.result(answer, returned.view());
  return answer;
}
Result<void> RetainedState::reconcile(CustodyVerifier &verifier) {
  const auto current = cursor();
  RawRetainedFields arguments;
  arguments.cursor(current);
  arguments.native(reconciled_);
  RawRetainedPair trace{raw_origin_, {3134, 3135}, 158, arguments.view()};
  const auto selected_catalog = archive_;
  RawRetainedFinally restore_catalog{[&]() noexcept {
    if (archive_ && archive_ != selected_catalog)
      archive_->raw_origin_ = raw_origin_;
  }};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope journal_scope{journal_->raw_origin_, trace.child()};
  std::optional<RawRetainedOriginScope> catalog_scope;
  if (selected_catalog)
    catalog_scope.emplace(selected_catalog->raw_origin_, trace.child());
  auto answer = trace.run([&]() -> Result<void> {
    if (state() != JournalWriterState::recovered) {
      return Result<void>::failure({ErrorCode::audit_unavailable});
    }
    try {
      auto unresolved = unresolved_attempts();
      if (!unresolved.has_value())
        return Result<void>::failure(unresolved.error());
      std::vector<AttemptState> attempts;
      for (auto &attempt : unresolved.value())
        if (attempt.reconciliation_required) {
          const auto input_size = attempt.admission.input.size();
          const bool observation = attempt.observation.has_value(),
                     receipt = attempt.receipt.has_value();
          const bool failed_receipt = receipt && attempt.receipt->failure.has_value();
          RawRetainedFields selected;
          selected.add(attempt.admission.attempt.bytes());
          selected.add(attempt.admission.invocation.bytes());
          selected.add(attempt.admission.decision.bytes());
          selected.native(input_size);
          selected.native(attempt.admission.input_from_invocation);
          selected.native(attempt.opened);
          selected.native(observation);
          std::size_t observation_size = 0;
          if (observation) {
            observation_size = attempt.observation->observation.size();
            selected.native(attempt.observation->phase);
            selected.native(attempt.observation->disposition);
            selected.native(observation_size);
          }
          selected.native(receipt);
          if (receipt) {
            selected.native(failed_receipt);
            if (failed_receipt) {
              selected.native(attempt.receipt->failure->code);
              selected.native(attempt.receipt->failure->detail);
            }
          }
          selected.native(attempt.evidence);
          selected.native(attempt.reconciliation_required);
          (void)raw_retained_event(raw_origin_, {3286, 122}, selected.view());
          attempts.push_back(std::move(attempt));
        }
      const auto attempt_count = attempts.size();
      RawRetainedFields verifier_arguments;
      verifier_arguments.native(attempt_count);
      RawRetainedPair verifier_trace{
          raw_origin_, {3136, 3137}, 98, verifier_arguments.view()};
      const auto verified =
          verifier_trace.run([&] { return verifier.verify(attempts); });
      verifier_trace.result(verified);
      if (!verified.has_value()) {
        return verified;
      }
      const auto resumed = journal_->resume_after_reconciliation();
      if (resumed.has_value()) {
        reconciled_ = true;
      }
      return resumed;
    } catch (const std::bad_alloc &) {
      return Result<void>::failure({ErrorCode::allocation});
    }
  });
  const auto final_cursor = cursor();
  const auto final_state = state();
  RawRetainedFields returned;
  returned.cursor(final_cursor);
  returned.native(final_state);
  returned.native(reconciled_);
  returned.native(recording_failed_);
  trace.result(answer, returned.view());
  return answer;
}
std::uint64_t RetainedState::issuer_counter() const noexcept {
  return std::max(pending_namespace_ == visible().issuer_namespace ? pending_counter_
                                                                   : 0,
                  visible().counter);
}
Result<IdentityBytes> RetainedState::reserve_identity() {
  const auto ns_current = journal_->header().issuer_namespace;
  const auto highwater = issuer_counter();
  const auto writer_state = state();
  RawRetainedFields arguments;
  arguments.native(ns_current);
  arguments.native(allocation_cursor_);
  arguments.native(allocation_limit_);
  arguments.native(highwater);
  arguments.native(writer_state);
  RawRetainedPair trace{raw_origin_, {3124, 3125}, 163, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<IdentityBytes> {
    // Cached durable space is not permission to bypass recovery/poison fences.
    if (in_transaction_) {
      return Result<IdentityBytes>::failure({ErrorCode::busy});
    }
    if (state() != JournalWriterState::live) {
      return Result<IdentityBytes>::failure({ErrorCode::audit_unavailable});
    }
    if (allocation_namespace_ != ns_current || allocation_limit_ != highwater ||
        allocation_cursor_ == allocation_limit_) {
      // External reservations, reopen and namespace changes burn cached space.
      if (highwater == UINT64_MAX) {
        return Result<IdentityBytes>::failure({ErrorCode::overflow});
      }
      constexpr std::uint64_t range_size = 1024;
      const auto limit = highwater + std::min(range_size, UINT64_MAX - highwater);
      const auto reserved = submit({{}, IssuerReservationEvent{limit}});
      if (!reserved.has_value()) {
        return Result<IdentityBytes>::failure(reserved.error());
      }
      // A storage/custody callback may close admission during an acknowledged
      // refill. Durable space is burned, not permission for an ID to escape.
      if (state() != JournalWriterState::live) {
        return Result<IdentityBytes>::failure({ErrorCode::audit_unavailable});
      }
      // Publish volatile allocation only after ordinary durable acknowledgement.
      allocation_namespace_ = ns_current;
      allocation_cursor_ = highwater;
      allocation_limit_ = limit;
    }
    const auto counter = ++allocation_cursor_; // cursor < limit, including at MAX.
    IdentityBytes bytes{};
    auto ns = journal_->header().issuer_namespace;
    auto value = counter;
    for (std::size_t index = 0; index < 8; ++index) {
      bytes[index] = static_cast<std::byte>(ns & 255);
      bytes[index + 8] = static_cast<std::byte>(value & 255);
      ns >>= 8;
      value >>= 8;
    }
    return Result<IdentityBytes>::success(bytes);
  });
  RawRetainedFields returned;
  if (answer.has_value())
    returned.add(answer.value());
  returned.native(allocation_namespace_);
  returned.native(allocation_cursor_);
  returned.native(allocation_limit_);
  trace.result(answer, returned.view());
  return answer;
}
// Reduce native effective program packets, not staged proposals. The workflow
// record may omit unchanged program_config; fold that field rather than losing it.
Value::Array RetainedState::current_programs() const {
  const bool saved_present = bool(saved_);
  RawRetainedFields arguments;
  arguments.native(saved_present);
  if (saved_)
    arguments.cursor(saved_->boundary);
  RawRetainedPair trace{raw_origin_, {3252, 3253}, 131, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Value::Array {
    std::map<std::string, Value> latest;
    TaskState tasks;
    bool have_tasks = false;
    Value config;
    Value budget;
    std::uint64_t boundary = 0;
    if (saved_ && std::holds_alternative<Value::Array>(saved_->programs.value())) {
      boundary = saved_->boundary.sequence;
      for (const auto &packet : saved_->programs.array()) {
        const auto &label = string_field(packet, "label");
        latest[label] = packet;
        if (label == "task-state-v1") {
          tasks.restore(packet);
          have_tasks = true;
        }
        if (label == "workflow-config-effective-v1")
          if (const auto *value = packet.find("program_config"))
            config = *value;
        if (label == "context-budget-effective-v1")
          budget = packet;
      }
    }
    for (const auto &fact : committed_.facts) {
      if (fact.record.journal == cursor().journal && fact.record.sequence <= boundary)
        continue;
      const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
      if (!record || record->channel != ApplicationChannel::program)
        continue;
      auto packet = unwrap(read_packet(record->payload));
      const auto *label = packet.find("label");
      if (!label || !std::holds_alternative<std::string>(label->value()))
        continue;
      const auto &name = label->string();
      if (name == "task-delta-v1") {
        tasks.replay(packet);
        have_tasks = true;
      } else if (name == "session.identity") {
        if (!latest.contains(name))
          latest[name] = packet;
      } else if (name == "session.settings" || name == "session.restart" ||
                 name == "session.recovery" || name.starts_with("station.") ||
                 name == "participants-state-v1" || name == "provider-profile-set-v1" ||
                 name == "provider-profile-override-v1" ||
                 name == "context-format-level" || name == "context-pins-v1")
        latest[name] = packet;
      else if (name == "workflow-config-effective-v1") {
        if (const auto *value = packet.find("program_config"))
          config = *value;
        if (config != Value{} && !packet.find("program_config")) {
          packet.object().emplace_back("program_config", config);
          const auto prior = latest.find(name);
          if (prior != latest.end())
            packet.object().emplace_back("program_revision",
                                         field(prior->second, "program_revision"));
        }
        latest[name] = packet;
        budget = Value::object({{"label", Value{"context-budget-effective-v1"}},
                                {"policy", field(packet, "policy")},
                                {"revision", field(packet, "revision")}});
      } else if (name == "context-budget-effective-v1")
        budget = packet;
    }
    Value::Array packets;
    for (const auto &[name, packet] : latest) {
      if (name != "context-budget-effective-v1" && name != "task-state-v1")
        packets.push_back(packet);
    }
    if (have_tasks)
      packets.push_back(tasks.snapshot());
    // Policy chronology is independent of the workflow/tool generation.
    if (budget != Value{})
      packets.push_back(std::move(budget));
    return packets;
  });
  const auto packet_count = answer.size();
  RawRetainedFields returned;
  returned.native(packet_count);
  trace.finish(0, returned.view());
  return answer;
}
Result<void> RetainedState::save_current_state(const Value &context) {
  const auto current = cursor();
  const bool saved_present = bool(saved_);
  RawRetainedFields arguments;
  arguments.cursor(current);
  arguments.native(saved_present);
  if (saved_)
    arguments.native(saved_->slot);
  arguments.native(compact_);
  RawRetainedPair trace{raw_origin_, {3110, 3111}, 181, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  bool selection_accepted = false;
  auto answer = trace.run([&]() -> Result<void> {
    if (state() != JournalWriterState::live || in_transaction_ || prepared_ ||
        !reconciled_ || !historical_.empty())
      return Result<void>::failure({ErrorCode::audit_unavailable});
    (void)directory_->reclaim_publication_scratch(journal_name_);
    try {
      SavedState next{cursor(),
                      fact_count(),
                      issuer_counter(),
                      context,
                      Value{current_programs()},
                      {},
                      saved_ ? saved_->slot ^ 1U : 0U};
      std::set<IdentityBytes> attempts, decisions, invocations;
      std::map<IdentityBytes, RecordReference> observations;
      for (const auto &fact : committed_.facts) {
        const auto *admission = std::get_if<AttemptAdmissionEvent>(&fact.event.body);
        if (!admission)
          continue;
        auto checked = attempt(admission->attempt);
        if (!checked.has_value())
          return Result<void>::failure(checked.error());
        const auto &checked_value = checked.value();
        // Live unresolved operations matter too: reconciled_ isn't terminality.
        if (!checked_value.observation ||
            checked_value.observation.value().phase != AttemptPhase::terminal) {
          attempts.insert(admission->attempt.bytes());
          decisions.insert(admission->decision.bytes());
          invocations.insert(admission->invocation.bytes());
        }
      }
      // An invocation can name an earlier decision than a retry admission. Include
      // that original decision in the unresolved transitive closure as well.
      for (const auto &fact : committed_.facts)
        if (const auto *invocation = std::get_if<InvocationEvent>(&fact.event.body);
            invocation && invocations.contains(invocation->invocation.bytes()))
          decisions.insert(invocation->decision.bytes());
      for (const auto &fact : committed_.facts) {
        if (const auto *observation =
                std::get_if<AttemptObservationEvent>(&fact.event.body);
            observation && attempts.contains(observation->attempt.bytes()))
          observations.insert_or_assign(observation->attempt.bytes(), fact.record);
      }
      for (const auto &fact : committed_.facts) {
        const bool keep = std::visit(
            [&](const auto &body) {
              using T = std::decay_t<decltype(body)>;
              if constexpr (std::is_same_v<T, AttemptObservationEvent>) {
                const auto found = observations.find(body.attempt.bytes());
                return found != observations.end() && found->second == fact.record;
              } else if constexpr (std::is_same_v<T, AttemptAdmissionEvent> ||
                                   std::is_same_v<T, AttemptOpenEvent> ||
                                   std::is_same_v<T, AttemptObservationEvent> ||
                                   std::is_same_v<T, AdapterReceiptEvent> ||
                                   std::is_same_v<T, RetryEvent>)
                return attempts.contains(body.attempt.bytes());
              else if constexpr (std::is_same_v<T, DecisionEvent>)
                return decisions.contains(body.decision.bytes());
              else if constexpr (std::is_same_v<T, InvocationEvent>)
                return invocations.contains(body.invocation.bytes());
              else
                return false;
            },
            fact.event.body);
        if (keep)
          next.unresolved.push_back(fact);
      }
      // Historical catalog is derived disk data, never a vector in SavedState.
      std::map<RecoveryKey, PhysicalJournalRecord> locators;
      const auto physical = journal_->physical_records();
      for (std::size_t ordinal = 0; ordinal < committed_.facts.size(); ++ordinal) {
        const auto &fact = committed_.facts[ordinal];
        const auto found =
            std::lower_bound(physical.begin(), physical.end(), fact.record.sequence,
                             [](const auto &record, std::uint64_t sequence) {
                               return record.sequence < sequence;
                             });
        if (found == physical.end() || found->sequence != fact.record.sequence ||
            found->kind != FrameKind::semantic)
          return Result<void>::failure({ErrorCode::corrupt});
        for (const auto &k :
             fact_keys(fact, {.issuer_namespace = journal_->header().issuer_namespace,
                              .ordinal = committed_.archived_facts + ordinal}))
          locators.insert_or_assign(k, *found);
      }
      for (const auto &source : committed_.sources)
        locators.insert_or_assign(
            ordinal_key(5, source.journal.bytes(), source.sequence), source);
      std::vector<ArchiveEntry> entries;
      entries.reserve(locators.size());
      for (const auto &[k, record] : locators)
        entries.push_back({k, record});
      const auto catalog_name = journal_name_ + ".archive." + std::to_string(next.slot);
      auto indexed = ArchiveCatalog::publish_merge(
          *directory_, catalog_name, journal_->header(), next.boundary,
          compact_ ? archive_.get() : nullptr, entries, raw_origin_);
      if (!indexed.has_value())
        return Result<void>::failure(indexed.error());
      next.context.object().emplace_back(
          "physical_records", Value{Number{journal_->usage().indexed_records}});
      next.context.object().emplace_back("archive_entries",
                                         Value{Number{indexed.value()}});
      bool supported = next.unresolved.empty();
      bool identity_saved = false;
      for (const auto &packet : next.programs.array()) {
        const auto &label = string_field(packet, "label");
        if (label == "session.identity")
          identity_saved = true;
        if ((label.starts_with("station.") &&
             !(label == "station.profile" &&
               string_field(packet, "adapter").empty())) ||
            (label == "session.restart" && string_field(packet, "state") == "pending"))
          supported = false;
      }
      if (supported && identity_saved)
        next.context.object().emplace_back("semantic_coverage",
                                           Value{"settled-no-station-v1"});
      auto opened = ArchiveCatalog::open(
          *directory_, catalog_name, journal_->header(), next.boundary,
          static_cast<std::uint64_t>(capacity_.max_records) * 6, raw_origin_);
      if (!opened.has_value())
        return Result<void>::failure(opened.error());
      // Publication cannot be followed by an allocation that leaves the selected
      // disk generation newer than the live semantic owner. Prepare both owners
      // before the native slot replacement and directory acknowledgment.
      auto selected_archive =
          std::shared_ptr<ArchiveCatalog>{std::move(opened).value()};
      auto selected_saved = std::make_unique<SavedState>(std::move(next));
      RawRetainedEvent selected_event{raw_origin_, {3226, 100}};
      std::optional<RawRetainedEvent> retired_before, retired_after;
      if (compact_ && selected_saved->unresolved.empty()) {
        retired_before.emplace(raw_origin_, RawRetainedEventSite{3232, 96});
        retired_after.emplace(raw_origin_, RawRetainedEventSite{3233, 88});
      }
      auto published =
          publish_saved_state(*directory_, journal_name_, *journal_, *selected_saved,
                              selected_saved->slot, raw_origin_);
      if (!published.has_value())
        return published;
      selection_accepted = true;
      const auto unresolved_count = selected_saved->unresolved.size();
      RawRetainedFields selected;
      selected.cursor(selected_saved->boundary);
      selected.native(selected_saved->slot);
      selected.native(selected_saved->fact_count);
      selected.native(selected_saved->issuer_counter);
      selected.native(unresolved_count);
      selected_event.publish(selected.view());
      selected_archive->raw_origin_ = raw_origin_;
      archive_ = std::move(selected_archive);
      saved_ = std::move(selected_saved);
      saved_load_->alternate.reset(); // that slot now holds this snapshot
      maintenance_error_.reset();
      maintenance_retry_sequence_ = 0;
      if (compact_ && saved_->unresolved.empty()) {
        const auto before_facts = committed_.facts.size(),
                   before_sources = committed_.sources.size();
        const auto before_records = journal_->records_.size();
        RawRetainedFields before;
        before.cursor(saved_->boundary);
        before.native(before_facts);
        before.native(before_sources);
        before.native(before_records);
        retired_before->publish(before.view());
        committed_.archived_facts = static_cast<std::size_t>(saved_->fact_count);
        committed_.archived_records = journal_->usage().indexed_records;
        committed_.facts.clear();
        committed_.sources.clear();
        journal_->archived_records_ += journal_->records_.size();
        journal_->records_.clear();
        const auto after_facts = committed_.facts.size(),
                   after_sources = committed_.sources.size();
        const auto after_records = journal_->records_.size();
        RawRetainedFields after;
        after.native(committed_.archived_facts);
        after.native(committed_.archived_records);
        after.native(journal_->archived_records_);
        after.native(after_facts);
        after.native(after_sources);
        after.native(after_records);
        retired_after->publish(after.view());
      }
      return Result<void>::success();
    } catch (const Error &error) {
      return Result<void>::failure(error);
    } catch (const std::bad_alloc &) {
      return Result<void>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (saved_) {
    returned.cursor(saved_->boundary);
    returned.native(saved_->slot);
    returned.native(committed_.archived_facts);
    returned.native(committed_.archived_records);
  }
  returned.native(selection_accepted);
  trace.result(answer, returned.view());
  return answer;
}
} // namespace blackbird
