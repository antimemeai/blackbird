#include "blackbird/retained_environment.hpp"
#include "blackbird/retained_proposal.hpp"

#include <algorithm>
#include <string_view>

namespace blackbird {
namespace {
class BorrowedDirectory final : public JournalDirectory {
public:
  explicit BorrowedDirectory(JournalDirectory &directory) : directory_(directory) {}
  Result<std::unique_ptr<JournalFile>>
  create_exclusive(std::string_view name) override {
    return directory_.create_exclusive(name);
  }
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view name,
                                                     FileAccess access) override {
    return directory_.open_existing(name, access);
  }
  Result<void> synchronize_directory(SyncStrength strength) override {
    return directory_.synchronize_directory(strength);
  }
  Result<void> replace_file(std::string_view from, std::string_view to) override {
    return directory_.replace_file(from, to);
  }

private:
  JournalDirectory &directory_;
};
std::array<char, 40> journal_name(AuditStreamId identity) noexcept {
  std::array<char, 40> name{};
  constexpr std::string_view prefix = "journal.";
  constexpr std::string_view hex = "0123456789abcdef";
  std::copy(prefix.begin(), prefix.end(), name.begin());
  for (std::size_t i = 0; i < identity.bytes().size(); ++i) {
    const auto byte = std::to_integer<unsigned int>(identity.bytes()[i]);
    name[8 + 2 * i] = hex[byte >> 4];
    name[9 + 2 * i] = hex[byte & 15];
  }
  return name;
}
bool capacity_valid(JournalCapacity capacity) noexcept {
  return capacity.max_file_bytes >= journal_header_size + 88 &&
         capacity.max_file_bytes <= INT64_MAX && capacity.max_records > 0 &&
         capacity.max_records <= std::vector<PhysicalJournalRecord>{}.max_size();
}
Result<void> validate(JournalHeader root, JournalCapacity policy,
                      EnvironmentBounds bounds) {
  const auto header = encode_journal_header(root);
  if (!header.has_value())
    return Result<void>::failure(header.error());
  if (root.predecessor || !capacity_valid(policy) || !capacity_valid(bounds.segment) ||
      bounds.max_segments == 0 || bounds.max_history_entries == 0 ||
      bounds.max_capture_bytes == 0 || bounds.max_capture_bytes > INT64_MAX ||
      bounds.max_capture_bytes > std::vector<std::byte>{}.max_size() ||
      bounds.max_segments > std::vector<std::unique_ptr<FramedJournal>>{}.max_size() ||
      bounds.max_segments > std::vector<JournalHeader>{}.max_size() ||
      bounds.max_history_entries > std::vector<RetainedFact>{}.max_size() ||
      bounds.max_history_entries > std::vector<ProvisionalOriginal>{}.max_size() ||
      bounds.max_history_entries > std::vector<PhysicalJournalRecord>{}.max_size())
    return Result<void>::failure({ErrorCode::invalid_range});
  if (root.limits.max_payload > bounds.framing.max_payload ||
      root.limits.max_batch_bytes > bounds.framing.max_batch_bytes ||
      policy.max_file_bytes > bounds.segment.max_file_bytes ||
      policy.max_records > bounds.segment.max_records)
    return Result<void>::failure({ErrorCode::capacity});
  return Result<void>::success();
}
Result<JournalCapacity> capacity_from(RetainedCapacityDeclaration wire,
                                      EnvironmentBounds bounds) {
  if (wire.max_file_bytes > bounds.segment.max_file_bytes ||
      wire.max_records > SIZE_MAX || wire.max_records > bounds.segment.max_records)
    return Result<JournalCapacity>::failure({ErrorCode::capacity});
  const JournalCapacity capacity{wire.max_file_bytes,
                                 static_cast<std::size_t>(wire.max_records)};
  return capacity_valid(capacity)
             ? Result<JournalCapacity>::success(capacity)
             : Result<JournalCapacity>::failure({ErrorCode::corrupt});
}
Result<JournalHeader> inspect_header(JournalDirectory &directory,
                                     AuditStreamId identity, EnvironmentBounds bounds,
                                     const RawRetainedOrigin &raw_origin) {
  RawRetainedFields arguments;
  arguments.add(identity.bytes());
  RawRetainedPair trace{raw_origin, {3264, 3265}, 187, arguments.view()};
  auto answer = trace.run([&]() -> Result<JournalHeader> {
    const auto name = journal_name(identity);
    auto opened =
        directory.open_existing({name.data(), name.size()}, FileAccess::read_only);
    if (!opened.has_value())
      return Result<JournalHeader>::failure(opened.error());
    auto file = std::move(opened).value();
    std::array<std::byte, journal_header_size> bytes{};
    std::size_t done = 0;
    unsigned int interruptions = 0;
    while (done < bytes.size()) {
      const auto result = file->read_at(done, MutableByteView{bytes}.subspan(done));
      if (!result.has_value()) {
        if (result.error().code == ErrorCode::interrupted && ++interruptions < 8)
          continue;
        return Result<JournalHeader>::failure(result.error());
      }
      if (result.value() == 0 || result.value() > bytes.size() - done)
        return Result<JournalHeader>::failure({ErrorCode::incomplete});
      done += result.value();
      interruptions = 0;
    }
    auto header = decode_journal_header(bytes);
    if (!header.has_value())
      return header;
    if (header.value().journal != identity)
      return Result<JournalHeader>::failure({ErrorCode::conflict});
    if (header.value().limits.max_payload > bounds.framing.max_payload ||
        header.value().limits.max_batch_bytes > bounds.framing.max_batch_bytes)
      return Result<JournalHeader>::failure({ErrorCode::capacity});
    return header;
  });
  RawRetainedFields returned;
  if (answer.has_value())
    returned.header(answer.value());
  trace.result(answer, returned.view());
  return answer;
}
struct BusyGuard {
  explicit BusyGuard(bool &busy) : busy_(busy) { busy_ = true; }
  ~BusyGuard() { busy_ = false; }
  BusyGuard(const BusyGuard &) = delete;
  BusyGuard &operator=(const BusyGuard &) = delete;

private:
  bool &busy_;
};
} // namespace

Result<std::unique_ptr<RetainedEnvironment>>
RetainedEnvironment::create(std::unique_ptr<JournalDirectory> directory,
                            JournalHeader root, JournalCapacity root_capacity,
                            EnvironmentBounds bounds,
                            RawProductStorageOwner raw_owner) {
  RawRetainedOrigin raw_origin;
  raw_origin.owner = raw_owner;
  raw_origin.selected = raw_owner.enabled();
  RawRetainedFields arguments;
  arguments.header(root);
  arguments.native(root_capacity.max_file_bytes);
  arguments.native(root_capacity.max_records);
  arguments.native(bounds.framing.max_payload);
  arguments.native(bounds.framing.max_batch_bytes);
  arguments.native(bounds.segment.max_file_bytes);
  arguments.native(bounds.segment.max_records);
  arguments.native(bounds.max_segments);
  arguments.native(bounds.max_history_entries);
  arguments.native(bounds.max_capture_bytes);
  RawRetainedPair trace{raw_origin, {3260, 3261}, 326, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::unique_ptr<RetainedEnvironment>> {
    const auto valid = validate(root, root_capacity, bounds);
    if (!valid.has_value())
      return Result<std::unique_ptr<RetainedEnvironment>>::failure(valid.error());
    try {
      auto prepared = EnvironmentHead::prepare(std::move(directory), root,
                                               SyncStrength::full, trace.child());
      if (!prepared.has_value())
        return Result<std::unique_ptr<RetainedEnvironment>>::failure(prepared.error());
      auto owner = std::unique_ptr<RetainedEnvironment>{new RetainedEnvironment{
          std::move(prepared).value(), root, root_capacity, bounds, trace.child()}};
      const auto name = journal_name(root.journal);
      auto semantic = RetainedState::create(
          std::make_unique<BorrowedDirectory>(owner->head_->directory()),
          {name.data(), name.size()}, root, root_capacity, raw_owner);
      if (!semantic.has_value())
        return Result<std::unique_ptr<RetainedEnvironment>>::failure(semantic.error());
      owner->semantic_ = std::move(semantic).value();
      owner->semantic_->max_history_entries_ = bounds.max_history_entries;
      const auto published = owner->head_->publish_initial(SyncStrength::full);
      if (!published.has_value())
        return Result<std::unique_ptr<RetainedEnvironment>>::failure(published.error());
      return Result<std::unique_ptr<RetainedEnvironment>>::success(std::move(owner));
    } catch (const std::bad_alloc &) {
      return Result<std::unique_ptr<RetainedEnvironment>>::failure(
          {ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto current = answer.value()->cursor();
    const auto state = answer.value()->state();
    returned.cursor(current);
    returned.native(state);
    returned.selector(answer.value()->head_->selection());
    returned.native(answer.value()->busy_);
    returned.native(answer.value()->continuation_required_);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<std::unique_ptr<RetainedEnvironment>>
RetainedEnvironment::open(std::unique_ptr<JournalDirectory> directory,
                          JournalHeader root, JournalCapacity root_capacity,
                          EnvironmentBounds bounds,
                          const RawProductStorageOwner &raw_owner) {
  RawRetainedOrigin raw_origin;
  raw_origin.owner = raw_owner;
  raw_origin.selected = raw_owner.enabled();
  RawRetainedFields arguments;
  arguments.header(root);
  arguments.native(root_capacity.max_file_bytes);
  arguments.native(root_capacity.max_records);
  arguments.native(bounds.framing.max_payload);
  arguments.native(bounds.framing.max_batch_bytes);
  arguments.native(bounds.segment.max_file_bytes);
  arguments.native(bounds.segment.max_records);
  arguments.native(bounds.max_segments);
  arguments.native(bounds.max_history_entries);
  arguments.native(bounds.max_capture_bytes);
  RawRetainedPair trace{raw_origin, {3260, 3261}, 326, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::unique_ptr<RetainedEnvironment>> {
    const auto valid = validate(root, root_capacity, bounds);
    if (!valid.has_value())
      return Result<std::unique_ptr<RetainedEnvironment>>::failure(valid.error());
    try {
      auto opened = EnvironmentHead::open(std::move(directory), root,
                                          SyncStrength::full, trace.child());
      if (!opened.has_value())
        return Result<std::unique_ptr<RetainedEnvironment>>::failure(opened.error());
      auto owner = std::unique_ptr<RetainedEnvironment>{new RetainedEnvironment{
          std::move(opened).value(), root, root_capacity, bounds, trace.child()}};
      const auto loaded = owner->load_selected();
      if (!loaded.has_value())
        return Result<std::unique_ptr<RetainedEnvironment>>::failure(loaded.error());
      return Result<std::unique_ptr<RetainedEnvironment>>::success(std::move(owner));
    } catch (const std::bad_alloc &) {
      return Result<std::unique_ptr<RetainedEnvironment>>::failure(
          {ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto current = answer.value()->cursor();
    const auto state = answer.value()->state();
    returned.cursor(current);
    returned.native(state);
    returned.selector(answer.value()->head_->selection());
    returned.native(answer.value()->busy_);
    returned.native(answer.value()->continuation_required_);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<void> RetainedEnvironment::stage_original(RetainedState::Snapshot &snapshot,
                                                 ProvisionalOriginal original,
                                                 const JournalHeader &origin,
                                                 JournalCapacity policy) {
  const auto capture = original.capture;
  RawRetainedFields arguments;
  arguments.reference(original.marker);
  arguments.add(capture.origin.bytes());
  arguments.native(capture.first_sequence);
  arguments.native(capture.proposal_length);
  arguments.native(capture.purpose);
  arguments.header(origin);
  arguments.native(policy.max_file_bytes);
  arguments.native(policy.max_records);
  RawRetainedPair trace{raw_origin_, {3269, 3270}, 261, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope semantic_scope{semantic_->raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    auto packet = decode_retained_proposal(original.bytes, bounds_.max_capture_bytes);
    if (!packet.has_value())
      return Result<void>::failure(packet.error());
    const auto &proposal = packet.value();
    const bool ordinary =
        original.capture.purpose == CapturePurpose::ordinary_submission;
    if (ordinary &&
        (proposal.expected.journal != origin.journal ||
         proposal.expected.sequence == UINT64_MAX ||
         proposal.expected.sequence + 1 != original.capture.first_sequence ||
         (proposal.sources.empty() && proposal.events.empty()) ||
         proposal.sources.size() > policy.max_records ||
         proposal.events.size() > policy.max_records - proposal.sources.size()))
      return Result<void>::failure({ErrorCode::conflict});
    std::uint64_t batch_bytes = 56;
    const auto fits = [&](std::size_t size) {
      if (size > origin.limits.max_payload ||
          size > origin.limits.max_batch_bytes - 32 ||
          batch_bytes > origin.limits.max_batch_bytes - 32 - size)
        return false;
      batch_bytes += 32 + size;
      return true;
    };
    if (ordinary) {
      for (const auto &source : proposal.sources)
        if (!fits(source.size()))
          return Result<void>::failure({ErrorCode::conflict});
      if (proposal.sources.size() > UINT64_MAX - original.capture.first_sequence ||
          proposal.events.size() >
              UINT64_MAX - original.capture.first_sequence - proposal.sources.size())
        return Result<void>::failure({ErrorCode::overflow});
    }
    for (std::size_t index = 0; index < proposal.events.size(); ++index) {
      const auto &bytes = proposal.events[index];
      auto decoded =
          decode_retained_event(bytes, origin.limits.max_payload, &raw_origin_);
      if (!decoded.has_value())
        return Result<void>::failure(decoded.error());
      if (!ordinary)
        continue;
      if (!fits(bytes.size()) ||
          std::holds_alternative<RecoveryChoiceEvent>(decoded.value().body) ||
          std::holds_alternative<ProvisionalCaptureEvent>(decoded.value().body))
        return Result<void>::failure({ErrorCode::conflict});
      // Attempt-related capture indexing requires the complete checked-set and
      // custody collector. Until that next integration step, refuse explicitly.
      if (std::holds_alternative<AttemptAdmissionEvent>(decoded.value().body) ||
          std::holds_alternative<AttemptOpenEvent>(decoded.value().body) ||
          std::holds_alternative<AttemptObservationEvent>(decoded.value().body) ||
          std::holds_alternative<AdapterReceiptEvent>(decoded.value().body))
        return Result<void>::failure({ErrorCode::unsupported});
      const RecordReference predicted{origin.journal, original.capture.first_sequence +
                                                          proposal.sources.size() +
                                                          index};
      std::optional<RetainedFact> known;
      if (RetainedState::has_identity(decoded.value())) {
        known = semantic_->existing(snapshot, decoded.value(), origin.issuer_namespace);
      } else {
        for (const auto &fact : snapshot.facts)
          if (fact.record == predicted) {
            known = fact;
            break;
          }
      }
      if (known) {
        if (known->event != decoded.value())
          return Result<void>::failure({ErrorCode::conflict});
        continue;
      }
      const auto *uncertain = semantic_->existing_uncertain(snapshot, decoded.value(),
                                                            origin.issuer_namespace);
      if (uncertain && RetainedState::has_identity(decoded.value())) {
        if (uncertain->event != decoded.value())
          return Result<void>::failure({ErrorCode::conflict});
        continue;
      }
      if (!semantic_->has_room(snapshot, 1))
        return Result<void>::failure({ErrorCode::capacity});
      snapshot.uncertain_facts.push_back(
          {predicted, std::move(decoded).value(), RetainedEvidence::uncertain});
    }
    if (!semantic_->has_room(snapshot, 1))
      return Result<void>::failure({ErrorCode::capacity});
    snapshot.provisional_originals.push_back(std::move(original));
    return Result<void>::success();
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    returned.native(capture.first_sequence);
    returned.native(capture.proposal_length);
  }
  trace.result(answer, returned.view());
  return answer;
}
Result<RetainedEnvironment::MaintenanceReplay> RetainedEnvironment::replay_maintenance(
    RetainedState::Snapshot &snapshot, FramedJournal &journal,
    const JournalHeader &origin, JournalCapacity origin_policy,
    RetainedEvent choice_event) {
  RawRetainedFields arguments;
  arguments.header(origin);
  arguments.native(origin_policy.max_file_bytes);
  arguments.native(origin_policy.max_records);
  RawRetainedPair trace{raw_origin_, {3267, 3268}, 231, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope semantic_scope{semantic_->raw_origin_, trace.child()};
  RawRetainedOriginScope journal_scope{journal.raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<MaintenanceReplay> {
    const auto *choice = std::get_if<RecoveryChoiceEvent>(&choice_event.body);
    if (!choice)
      return Result<MaintenanceReplay>::failure({ErrorCode::conflict});
    if (!choice->checked_attempts.empty())
      return Result<MaintenanceReplay>::failure({ErrorCode::unsupported});
    MaintenanceReplay result;
    const auto records = journal.staged_records();
    std::vector<RetainedFact> markers;
    std::vector<ProvisionalCaptureEvent> seen;
    std::size_t index = 1;
    for (std::size_t count = 0; count < choice->required_captures.size(); ++count) {
      const auto first = index;
      while (index < records.size() && records[index].kind == FrameKind::source)
        ++index;
      if (index == first || index == records.size() ||
          records[index].kind != FrameKind::semantic)
        return Result<MaintenanceReplay>::failure({ErrorCode::conflict});
      auto payload = journal.read_payload(records[index]);
      if (!payload.has_value())
        return Result<MaintenanceReplay>::failure(payload.error());
      auto event = decode_retained_event(
          payload.value(), journal.header().limits.max_payload, &raw_origin_);
      if (!event.has_value())
        return Result<MaintenanceReplay>::failure(event.error());
      const auto *capture = std::get_if<ProvisionalCaptureEvent>(&event.value().body);
      if (!capture || capture->origin != origin.journal ||
          std::find(choice->required_captures.begin(), choice->required_captures.end(),
                    *capture) == choice->required_captures.end() ||
          std::find(seen.begin(), seen.end(), *capture) != seen.end() ||
          event.value().dependencies.size() != index - first)
        return Result<MaintenanceReplay>::failure({ErrorCode::conflict});
      for (const auto &old : snapshot.provisional_originals)
        if (old.capture == *capture)
          return Result<MaintenanceReplay>::failure({ErrorCode::conflict});
      auto remaining = bounds_.max_capture_bytes;
      for (const auto &old : snapshot.provisional_originals) {
        if (old.bytes.size() > remaining)
          return Result<MaintenanceReplay>::failure({ErrorCode::capacity});
        remaining -= old.bytes.size();
      }
      if (capture->proposal_length > remaining)
        return Result<MaintenanceReplay>::failure({ErrorCode::capacity});
      ProvisionalOriginal original{
          {journal.header().journal, records[index].sequence}, *capture, {}};
      original.bytes.reserve(static_cast<std::size_t>(capture->proposal_length));
      for (std::size_t chunk = first; chunk < index; ++chunk) {
        const SourceReference reference{records[chunk].journal,
                                        records[chunk].sequence};
        if (event.value().dependencies[chunk - first] != reference ||
            records[chunk].payload_size >
                capture->proposal_length - original.bytes.size())
          return Result<MaintenanceReplay>::failure({ErrorCode::conflict});
        const auto chunk_offset = original.bytes.size();
        RawRetainedFields selected;
        selected.reference(original.marker);
        selected.native(chunk_offset);
        selected.native(records[chunk].payload_size);
        selected.native(capture->purpose);
        (void)raw_retained_event(raw_origin_, {3271, 82}, selected.view());
        auto bytes = journal.read_payload(records[chunk]);
        if (!bytes.has_value())
          return Result<MaintenanceReplay>::failure(bytes.error());
        original.bytes.insert(original.bytes.end(), bytes.value().begin(),
                              bytes.value().end());
        result.capture_only.push_back(reference);
      }
      if (original.bytes.size() != capture->proposal_length)
        return Result<MaintenanceReplay>::failure({ErrorCode::conflict});
      const auto staged =
          stage_original(snapshot, std::move(original), origin, origin_policy);
      if (!staged.has_value())
        return Result<MaintenanceReplay>::failure(staged.error());
      seen.push_back(*capture);
      result.end = records[index].sequence;
      markers.push_back({{journal.header().journal, result.end},
                         std::move(event).value(),
                         RetainedEvidence::recovered_pending});
      ++index;
    }
    // Extra maintenance anywhere in the suffix refuses activation. Ordinary
    // malformed payloads remain the shared replay validator's batch-level concern.
    for (; index < records.size(); ++index) {
      if (records[index].kind != FrameKind::semantic)
        continue;
      auto payload = journal.read_payload(records[index]);
      if (!payload.has_value())
        return Result<MaintenanceReplay>::failure(payload.error());
      auto event = decode_retained_event(
          payload.value(), journal.header().limits.max_payload, &raw_origin_);
      if (!event.has_value()) {
        if (event.error().code == ErrorCode::allocation)
          return Result<MaintenanceReplay>::failure(event.error());
        continue;
      }
      if (std::holds_alternative<RecoveryChoiceEvent>(event.value().body) ||
          std::holds_alternative<ProvisionalCaptureEvent>(event.value().body))
        return Result<MaintenanceReplay>::failure({ErrorCode::conflict});
    }
    for (const auto &fact : snapshot.facts) {
      if (const auto *admission =
              std::get_if<AttemptAdmissionEvent>(&fact.event.body)) {
        const auto prior = semantic_->attempt(admission->attempt);
        if (!prior.has_value())
          return Result<MaintenanceReplay>::failure(prior.error());
        if (prior.value().reconciliation_required)
          return Result<MaintenanceReplay>::failure({ErrorCode::conflict});
      }
    }
    if (!semantic_->has_room(snapshot, markers.size() + 1))
      return Result<MaintenanceReplay>::failure({ErrorCode::capacity});
    snapshot.facts.push_back({{journal.header().journal, 1},
                              std::move(choice_event),
                              RetainedEvidence::recovered_pending});
    for (auto &marker : markers)
      snapshot.facts.push_back(std::move(marker));
    return Result<MaintenanceReplay>::success(std::move(result));
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto captures = answer.value().capture_only.size();
    returned.native(answer.value().end);
    returned.native(captures);
    returned.page(snapshot.query_root);
    returned.native(snapshot.counter);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<std::optional<ProposalSubmission>>
RetainedEnvironment::existing_proposal(JournalCursor expected,
                                       std::span<const ByteView> sources,
                                       std::span<const RetainedEvent> events) const {
  for (const auto &original : semantic_->visible().provisional_originals) {
    if (original.capture.purpose != CapturePurpose::ordinary_submission)
      continue;
    const auto *origin = semantic_->segment(original.capture.origin);
    if (!origin)
      return Result<std::optional<ProposalSubmission>>::failure({ErrorCode::corrupt});
    auto decoded = decode_retained_proposal(original.bytes, bounds_.max_capture_bytes);
    if (!decoded.has_value())
      return Result<std::optional<ProposalSubmission>>::failure(decoded.error());
    const auto &proposal = decoded.value();
    if (proposal.expected.journal != expected.journal ||
        proposal.expected.sequence != expected.sequence ||
        proposal.expected.end_offset != expected.end_offset ||
        proposal.sources.size() != sources.size() ||
        proposal.events.size() != events.size())
      continue;
    bool equal = true;
    for (std::size_t i = 0; i < sources.size(); ++i)
      if (!std::equal(sources[i].begin(), sources[i].end(), proposal.sources[i].begin(),
                      proposal.sources[i].end())) {
        equal = false;
        break;
      }
    for (std::size_t i = 0; equal && i < events.size(); ++i) {
      auto encoded =
          encode_retained_event(events[i], origin->header().limits.max_payload);
      if (!encoded.has_value()) {
        if (encoded.error().code == ErrorCode::allocation)
          return Result<std::optional<ProposalSubmission>>::failure(encoded.error());
        equal = false;
      } else {
        equal = encoded.value() == proposal.events[i];
      }
    }
    if (!equal)
      continue;
    ProposalSubmission result{cursor(), true, original.capture, {}};
    result.events.reserve(events.size());
    for (std::size_t i = 0; i < events.size(); ++i)
      result.events.push_back({true,
                               {original.capture.origin,
                                original.capture.first_sequence + sources.size() + i},
                               RetainedEvidence::uncertain});
    return Result<std::optional<ProposalSubmission>>::success(std::move(result));
  }
  return Result<std::optional<ProposalSubmission>>::success(std::nullopt);
}
Result<void> RetainedEnvironment::load_selected() {
  const auto selection = head_->selection();
  if (selection.generation > bounds_.max_segments)
    return Result<void>::failure({ErrorCode::capacity});
  std::vector<JournalHeader> headers;
  headers.reserve(static_cast<std::size_t>(selection.generation));
  auto identity = selection.active;
  for (;;) {
    auto inspected = inspect_header(head_->directory(), identity, bounds_, raw_origin_);
    if (!inspected.has_value())
      return Result<void>::failure(inspected.error());
    const auto header = inspected.value();
    if (header.environment != root_.environment)
      return Result<void>::failure({ErrorCode::wrong_environment});
    for (const auto &previous : headers)
      if (previous.journal == header.journal ||
          previous.issuer_namespace == header.issuer_namespace)
        return Result<void>::failure({ErrorCode::conflict});
    headers.push_back(header);
    if (header.journal == root_.journal) {
      if (header != root_ || headers.size() != selection.generation)
        return Result<void>::failure({ErrorCode::conflict});
      break;
    }
    if (!header.predecessor || headers.size() >= selection.generation)
      return Result<void>::failure({ErrorCode::conflict});
    identity = header.predecessor->journal;
  }
  std::reverse(headers.begin(), headers.end());
  auto allocated = RetainedState::allocate(
      std::make_unique<BorrowedDirectory>(head_->directory()), root_capacity_);
  if (!allocated.has_value())
    return Result<void>::failure(allocated.error());
  semantic_ = std::move(allocated).value();
  semantic_->raw_storage_owner_ = raw_origin_.owner;
  semantic_->raw_origin_ = raw_origin_;
  semantic_->raw_origin_.root = semantic_.get();
  auto end = RawRetainedOwnerEnd::prepare(raw_origin_.owner, raw_origin_.parent);
  if (!end.has_value())
    return Result<void>::failure(end.error());
  semantic_->max_history_entries_ = bounds_.max_history_entries;
  semantic_->historical_.reserve(headers.size() - 1);
  for (std::size_t i = 0; i < headers.size(); ++i) {
    const auto name = journal_name(headers[i].journal);
    auto segment = FramedJournal::open(head_->directory(), {name.data(), name.size()},
                                       headers[i], bounds_.segment, SyncStrength::full,
                                       true, {}, {}, semantic_->raw_origin_);
    if (!segment.has_value())
      return Result<void>::failure(segment.error());
    if (i + 1 < headers.size()) {
      const auto &predecessor = headers[i + 1].predecessor;
      if (!predecessor)
        return Result<void>::failure({ErrorCode::corrupt});
      RawRetainedFields selected;
      selected.header(headers[i]);
      selected.native(bounds_.segment.max_file_bytes);
      selected.native(bounds_.segment.max_records);
      selected.add(predecessor->journal.bytes());
      selected.native(predecessor->validated_sequence);
      selected.native(predecessor->validated_end_offset);
      (void)raw_retained_event(raw_origin_, {3266, 169}, selected.view());
      const auto restricted = segment.value()->select_recovered_prefix(*predecessor);
      if (!restricted.has_value())
        return restricted;
      semantic_->historical_.push_back(std::move(segment).value());
    } else {
      semantic_->journal_ = std::move(segment).value();
    }
  }
  semantic_->pending_namespace_ = headers.back().issuer_namespace;
  semantic_->reconciled_ = false;
  auto &snapshot = semantic_->prepared_.emplace(RetainedState::Snapshot{});
  auto policy = root_capacity_;
  for (std::size_t i = 0; i < headers.size(); ++i) {
    auto *journal = semantic_->segment(headers[i].journal);
    if (!journal)
      return Result<void>::failure({ErrorCode::corrupt});
    snapshot.issuer_namespace = headers[i].issuer_namespace;
    snapshot.counter = 0;
    std::uint64_t maintenance_end = 0;
    std::vector<SourceReference> capture_only;
    if (i > 0) {
      const auto &selected_predecessor = headers[i].predecessor;
      if (!selected_predecessor)
        return Result<void>::failure({ErrorCode::corrupt});
      const auto records = journal->staged_records();
      if (records.empty() || records[0].kind != FrameKind::semantic ||
          records[0].sequence != 1)
        return Result<void>::failure({ErrorCode::corrupt});
      auto payload = journal->read_payload(records[0]);
      if (!payload.has_value())
        return Result<void>::failure(payload.error());
      auto decoded = decode_retained_event(payload.value(),
                                           headers[i].limits.max_payload, &raw_origin_);
      if (!decoded.has_value())
        return Result<void>::failure(decoded.error());
      const auto *choice = std::get_if<RecoveryChoiceEvent>(&decoded.value().body);
      if (!choice || !decoded.value().dependencies.empty() ||
          choice->predecessor != *selected_predecessor ||
          choice->old_limits != headers[i - 1].limits ||
          choice->old_capacity.max_file_bytes != policy.max_file_bytes ||
          choice->old_capacity.max_records != policy.max_records)
        return Result<void>::failure({ErrorCode::conflict});
      const auto *predecessor = semantic_->segment(headers[i - 1].journal);
      if (!predecessor ||
          choice->available_end > predecessor->recovery_report().available_end ||
          choice->predecessor.validated_end_offset > choice->available_end ||
          choice->diagnostic_offset < journal_header_size ||
          choice->diagnostic_offset > choice->available_end)
        return Result<void>::failure({ErrorCode::conflict});
      auto next_policy = capacity_from(choice->new_capacity, bounds_);
      if (!next_policy.has_value())
        return Result<void>::failure(next_policy.error());
      auto maintenance = replay_maintenance(snapshot, *journal, headers[i - 1], policy,
                                            std::move(decoded).value());
      if (!maintenance.has_value())
        return Result<void>::failure(maintenance.error());
      policy = next_policy.value();
      maintenance_end = maintenance.value().end;
      capture_only = std::move(maintenance).value().capture_only;
    }
    const auto before = journal->cursor();
    const auto replayed =
        semantic_->replay(snapshot, *journal, capture_only, maintenance_end);
    if (!replayed.has_value())
      return replayed;
    if (i + 1 < headers.size() && (journal->cursor().sequence != before.sequence ||
                                   journal->cursor().end_offset != before.end_offset))
      return Result<void>::failure({ErrorCode::corrupt});
    if (journal->cursor().end_offset > policy.max_file_bytes ||
        journal->staged_records().size() > policy.max_records)
      return Result<void>::failure({ErrorCode::capacity});
  }
  semantic_->capacity_ = policy;
  semantic_->raw_end_ = std::move(end).value();
  return Result<void>::success();
}

Result<Submission> RetainedEnvironment::submit(const RetainedEvent &event) {
  if (busy_)
    return Result<Submission>::failure({ErrorCode::busy});
  if (!head_->healthy() || continuation_required_)
    return Result<Submission>::failure({ErrorCode::audit_unavailable});
  return semantic_->submit(event);
}
Result<ProposalSubmission>
RetainedEnvironment::submit_proposal(JournalCursor expected,
                                     std::span<const ByteView> sources,
                                     std::span<const RetainedEvent> events) {
  const auto source_count = sources.size(), event_count = events.size();
  RawRetainedFields arguments;
  arguments.cursor(expected);
  arguments.native(source_count);
  arguments.native(event_count);
  RawRetainedPair trace{raw_origin_, {3262, 3263}, 214, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope semantic_scope{semantic_->raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<ProposalSubmission> {
    if (busy_)
      return Result<ProposalSubmission>::failure({ErrorCode::busy});
    if (!head_->healthy() || continuation_required_)
      return Result<ProposalSubmission>::failure({ErrorCode::audit_unavailable});
    try {
      auto captured = existing_proposal(expected, sources, events);
      if (!captured.has_value())
        return Result<ProposalSubmission>::failure(captured.error());
      auto &matching_proposal = captured.value();
      if (matching_proposal.has_value())
        return Result<ProposalSubmission>::success(std::move(*matching_proposal));
      if (sources.size() > bounds_.segment.max_records ||
          events.size() > bounds_.segment.max_records - sources.size())
        return Result<ProposalSubmission>::failure({ErrorCode::capacity});
      ProposalSubmission result{cursor(), false, std::nullopt, {}};
      result.events.reserve(events.size());
      // Stage result ownership before append acknowledgement; first semantic
      // matches preserve input order even when physical duplicate frames remain.
      const auto first = cursor().sequence + sources.size() + 1;
      if (first < cursor().sequence || events.size() > UINT64_MAX - first)
        return Result<ProposalSubmission>::failure({ErrorCode::overflow});
      for (std::size_t i = 0; i < events.size(); ++i) {
        const auto previous = semantic_->existing(semantic_->visible(), events[i]);
        if (previous && previous->event == events[i]) {
          result.events.push_back({true, previous->record, previous->evidence});
          continue;
        }
        const auto *uncertain =
            semantic_->existing_uncertain(semantic_->visible(), events[i]);
        if (uncertain && uncertain->event == events[i]) {
          result.events.push_back(
              {true, uncertain->record, RetainedEvidence::uncertain});
          continue;
        }
        std::size_t ordinal = i;
        for (std::size_t j = 0; j < i; ++j)
          if (events[j] == events[i] && RetainedState::has_identity(events[i])) {
            ordinal = j;
            break;
          }
        result.events.push_back({ordinal != i,
                                 {cursor().journal, first + ordinal},
                                 RetainedEvidence::live});
      }
      if (sources.empty() && !events.empty() &&
          expected.journal == result.cursor.journal &&
          expected.sequence == result.cursor.sequence &&
          expected.end_offset == result.cursor.end_offset &&
          std::all_of(result.events.begin(), result.events.end(),
                      [](const auto &event) { return event.existing; })) {
        result.existing = true;
        return Result<ProposalSubmission>::success(std::move(result));
      }
      if (state() != JournalWriterState::live)
        return Result<ProposalSubmission>::failure({ErrorCode::audit_unavailable});
      BusyGuard guard{busy_};
      const auto written = semantic_->append(expected, sources, events);
      if (!written.has_value())
        return Result<ProposalSubmission>::failure(written.error());
      result.cursor = written.value();
      return Result<ProposalSubmission>::success(std::move(result));
    } catch (const std::bad_alloc &) {
      return Result<ProposalSubmission>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &value = answer.value();
    const bool captured = value.capture.has_value();
    const auto returned_events = value.events.size();
    returned.cursor(value.cursor);
    returned.native(value.existing);
    returned.native(captured);
    if (captured) {
      returned.add(value.capture->origin.bytes());
      returned.native(value.capture->first_sequence);
      returned.native(value.capture->proposal_length);
      returned.native(value.capture->purpose);
    }
    returned.native(returned_events);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<DispatchReport> RetainedEnvironment::dispatch(OperationAttemptId identity,
                                                     EffectBoundary &boundary) {
  if (busy_)
    return Result<DispatchReport>::failure({ErrorCode::busy});
  if (state() != JournalWriterState::live)
    return Result<DispatchReport>::failure({ErrorCode::audit_unavailable});
  return semantic_->dispatch(identity, boundary);
}
Result<AttemptState> RetainedEnvironment::attempt(OperationAttemptId identity) const {
  if (busy_)
    return Result<AttemptState>::failure({ErrorCode::busy});
  return semantic_->attempt(identity);
}
Result<std::vector<std::byte>> RetainedEnvironment::source(SourceReference reference) {
  if (busy_)
    return Result<std::vector<std::byte>>::failure({ErrorCode::busy});
  BusyGuard guard{busy_};
  const auto *origin = semantic_->segment(reference.journal);
  const auto previous =
      origin ? std::optional<JournalRecoveryReport>{origin->recovery_report()}
             : std::nullopt;
  auto result = semantic_->source(reference);
  if (!result.has_value() && origin && previous) {
    const auto &now = origin->recovery_report();
    // Reflect physical validation failure, including a historical owner which
    // was already Blocked for its selected boundary. Pure lookup/allocation
    // refusals do not change that physical evidence and do not close admission.
    if (origin->state() == JournalWriterState::poisoned ||
        now.problem != previous->problem ||
        now.diagnostic_offset != previous->diagnostic_offset)
      continuation_required_ = true;
  }
  return result;
}
Result<ObservedJournalBytes>
RetainedEnvironment::read_original_range(AuditStreamId identity, std::uint64_t offset,
                                         std::size_t length) {
  auto *journal = semantic_->segment(identity);
  return journal ? journal->read_original_range(offset, length)
                 : Result<ObservedJournalBytes>::failure({ErrorCode::stale_handle});
}
Result<void> RetainedEnvironment::confirm_recovery() {
  const auto current = cursor();
  const auto writer_state = state();
  RawRetainedFields arguments;
  arguments.cursor(current);
  arguments.native(writer_state);
  RawRetainedPair trace{raw_origin_, {3272, 3273}, 157, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope semantic_scope{semantic_->raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    if (busy_)
      return Result<void>::failure({ErrorCode::busy});
    if (!head_->healthy() || continuation_required_ ||
        semantic_->state() != JournalWriterState::recovery_pending ||
        !semantic_->prepared_)
      return Result<void>::failure({ErrorCode::audit_unavailable});
    BusyGuard guard{busy_};
    for (const auto &journal : semantic_->historical_) {
      const auto confirmed = journal->confirm_recovery();
      if (!confirmed.has_value())
        return confirmed;
    }
    return semantic_->confirm_recovery();
  });
  const auto selected_cursor = cursor();
  const bool healthy = head_->healthy();
  RawRetainedFields returned;
  returned.cursor(selected_cursor);
  returned.native(healthy);
  returned.native(continuation_required_);
  trace.result(answer, returned.view());
  return answer;
}
Result<void> RetainedEnvironment::reconcile(CustodyVerifier &verifier) {
  const auto current = cursor();
  const auto writer_state = state();
  RawRetainedFields arguments;
  arguments.cursor(current);
  arguments.native(writer_state);
  RawRetainedPair trace{raw_origin_, {3272, 3273}, 157, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedOriginScope semantic_scope{semantic_->raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    if (busy_)
      return Result<void>::failure({ErrorCode::busy});
    if (!head_->healthy() || continuation_required_)
      return Result<void>::failure({ErrorCode::audit_unavailable});
    BusyGuard guard{busy_};
    return semantic_->reconcile(verifier);
  });
  const auto selected_cursor = cursor();
  const bool healthy = head_->healthy();
  RawRetainedFields returned;
  returned.cursor(selected_cursor);
  returned.native(healthy);
  returned.native(continuation_required_);
  trace.result(answer, returned.view());
  return answer;
}
} // namespace blackbird
