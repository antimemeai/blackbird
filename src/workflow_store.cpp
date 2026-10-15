#include "blackbird/workflow_store.hpp"
#include "blackbird/saved_state.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <unistd.h>

namespace blackbird {
namespace {
constexpr std::string_view delta_label = "workflow-cells-delta-v1";
constexpr std::string_view state_label = "workflow-cells-state-v1";
Value number(std::uint64_t value) { return Value{Number{value}}; }
std::uint64_t integer(const Value &value) {
  const auto *numeric = std::get_if<Number>(&value.value());
  if (!numeric)
    throw Error{ErrorCode::invalid_range};
  const auto text = numeric->text();
  std::uint64_t result = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    throw Error{ErrorCode::invalid_range};
  return result;
}
void keys(const Value &value, std::initializer_list<std::string_view> names) {
  const auto &object = value.object();
  if (object.size() != names.size())
    throw Error{ErrorCode::invalid_range};
  std::set<std::string_view> seen;
  for (const auto &[key, item] : object) {
    (void)item;
    if (std::find(names.begin(), names.end(), key) == names.end() ||
        !seen.insert(key).second)
      throw Error{ErrorCode::invalid_range};
  }
}
void text(std::string_view value, std::size_t bound) {
  if (value.empty() || value.size() > bound ||
      value.find('\0') != std::string_view::npos || !valid_utf8(value))
    throw Error{ErrorCode::invalid_range};
}
template <class Identity> Identity parse_identity(const Value &value) {
  const auto &encoded = value.string();
  if (encoded.size() != 32)
    throw Error{ErrorCode::invalid_identity};
  IdentityBytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto nibble = [](char digit) -> unsigned char {
      if (digit >= '0' && digit <= '9')
        return static_cast<unsigned char>(digit - '0');
      if (digit >= 'a' && digit <= 'f')
        return static_cast<unsigned char>(digit - 'a' + 10);
      throw Error{ErrorCode::invalid_identity};
    };
    bytes[index] = static_cast<std::byte>(16 * nibble(encoded[index * 2]) +
                                          nibble(encoded[index * 2 + 1]));
  }
  return unwrap(Identity::from_bytes(bytes));
}
Value canonical_impl(const Value &value, std::size_t depth, std::size_t &nodes) {
  if (depth > ValueLimits{}.depth || ++nodes > ValueLimits{}.nodes)
    throw Error{ErrorCode::capacity};
  if (const auto *array = std::get_if<Value::Array>(&value.value())) {
    if (array->size() > ValueLimits{}.nodes - nodes)
      throw Error{ErrorCode::capacity};
    Value::Array result;
    result.reserve(array->size());
    for (const auto &child : *array)
      result.push_back(canonical_impl(child, depth + 1, nodes));
    return Value{std::move(result)};
  }
  if (const auto *object = std::get_if<Value::Object>(&value.value())) {
    if (object->size() > ValueLimits{}.nodes - nodes)
      throw Error{ErrorCode::capacity};
    Value::Object result;
    result.reserve(object->size());
    for (const auto &[key, child] : *object)
      result.emplace_back(key, canonical_impl(child, depth + 1, nodes));
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
      return left.first < right.first;
    });
    for (std::size_t index = 1; index < result.size(); ++index)
      if (result[index - 1].first == result[index].first)
        throw Error{ErrorCode::invalid_range};
    return Value::object(std::move(result));
  }
  return value;
}
Value canonical(const Value &value) {
  std::size_t nodes = 0;
  return canonical_impl(value, 0, nodes);
}
WorkflowCellSchema read_schema(const Value &value) {
  keys(value, {"name", "version"});
  auto name = string_field(value, "name");
  text(name, 128);
  const auto version = integer(field(value, "version"));
  if (!version || version > UINT32_MAX)
    throw Error{ErrorCode::invalid_range};
  return {std::move(name), static_cast<std::uint32_t>(version)};
}
Value schema_value(const WorkflowCellSchema &schema) {
  text(schema.name, 128);
  if (!schema.version)
    throw Error{ErrorCode::invalid_range};
  return Value::object(
      {{"name", Value{schema.name}}, {"version", number(schema.version)}});
}
Value reference_value(RecordReference record) {
  return Value::object({{"journal", Value{hex_identity(record.journal.bytes())}},
                        {"sequence", number(record.sequence)}});
}
RecordReference read_reference(const Value &value) {
  keys(value, {"journal", "sequence"});
  const auto sequence = integer(field(value, "sequence"));
  if (!sequence)
    throw Error{ErrorCode::invalid_range};
  return {parse_identity<AuditStreamId>(field(value, "journal")), sequence};
}
Value request_value(const WorkflowTransaction &transaction) {
  text(transaction.submission, 256);
  auto reads = transaction.reads;
  auto writes = transaction.writes;
  const auto by_identity = [](const auto &left, const auto &right) {
    return left.id < right.id;
  };
  std::sort(reads.begin(), reads.end(), by_identity);
  std::sort(writes.begin(), writes.end(), by_identity);
  Value::Array read_values, write_values;
  read_values.reserve(reads.size());
  write_values.reserve(writes.size());
  for (const auto &read : reads)
    read_values.push_back(Value::object({{"id", Value{hex_identity(read.id.bytes())}},
                                         {"revision", number(read.revision.value)}}));
  for (const auto &write : writes)
    write_values.push_back(Value::object({{"id", Value{hex_identity(write.id.bytes())}},
                                          {"schema", schema_value(write.schema)},
                                          {"value", write.value}}));
  return canonical(Value::object(
      {{"submission", Value{transaction.submission}},
       {"reads", Value{std::move(read_values)}},
       {"writes", Value{std::move(write_values)}},
       {"decision_present", Value{transaction.decision.has_value()}},
       {"decision", transaction.decision ? *transaction.decision : Value{}}}));
}
void validate_references(const Value &events, RecordReference record,
                         std::size_t maximum) {
  if (events.array().size() > maximum ||
      events.array().size() > UINT64_MAX - record.sequence)
    throw Error{ErrorCode::invalid_range};
  for (std::size_t index = 0; index < events.array().size(); ++index) {
    const auto expected = read_reference(events.array()[index]);
    if (expected.journal != record.journal ||
        expected.sequence != record.sequence + index + 1)
      throw Error{ErrorCode::invalid_range};
  }
}
template <class T> void require_absent(const Result<T> &result) {
  if (result.has_value())
    throw Error{ErrorCode::conflict};
  if (result.error().code != ErrorCode::stale_handle)
    throw result.error();
}
void fresh_events(RetainedState &root, std::span<const RetainedEvent> events) {
  std::set<std::pair<RetainedKind, IdentityBytes>> selected;
  for (const auto &event : events) {
    const auto kind = retained_kind(event.body);
    const auto identity = std::visit(
        [&](const auto &body) -> IdentityBytes {
          using T = std::decay_t<decltype(body)>;
          if constexpr (std::is_same_v<T, DecisionEvent>) {
            require_absent(root.decision(body.decision));
            return body.decision.bytes();
          } else if constexpr (std::is_same_v<T, InvocationEvent>) {
            require_absent(root.invocation(body.invocation));
            return body.invocation.bytes();
          } else if constexpr (std::is_same_v<T, AttemptAdmissionEvent>) {
            require_absent(root.attempt(body.attempt));
            return body.attempt.bytes();
          } else if constexpr (std::is_same_v<T, AttemptOpenEvent> ||
                               std::is_same_v<T, AdapterReceiptEvent> ||
                               std::is_same_v<T, AttemptObservationEvent>) {
            const auto attempt = root.attempt(body.attempt);
            if (attempt.has_value()) {
              const auto &current = attempt.value();
              bool exists = false;
              if constexpr (std::is_same_v<T, AttemptOpenEvent>)
                exists = current.opened;
              else if constexpr (std::is_same_v<T, AdapterReceiptEvent>)
                exists = current.receipt.has_value();
              else
                exists = current.observation &&
                         current.observation->phase == AttemptPhase::terminal;
              if (exists)
                throw Error{ErrorCode::conflict};
            } else if (attempt.error().code != ErrorCode::stale_handle)
              throw attempt.error();
            return body.attempt.bytes();
          } else
            throw Error{ErrorCode::unsupported};
        },
        event.body);
    if (!selected.emplace(kind, identity).second)
      throw Error{ErrorCode::conflict};
  }
}
bool label_is(const Value &packet, std::string_view name) {
  const auto *label = packet.find("label");
  return label && std::holds_alternative<std::string>(label->value()) &&
         label->string() == name;
}
} // namespace

WorkflowCellState::WorkflowCellState(WorkflowStoreLimits limits)
    : limits_(limits), thread_(std::this_thread::get_id()), process_(::getpid()) {
  if (!limits.cells || !limits.submissions || !limits.batch)
    throw Error{ErrorCode::invalid_range};
}
void WorkflowCellState::owner() const {
  if (thread_ != std::this_thread::get_id() || process_ != ::getpid())
    throw Error{ErrorCode::wrong_incarnation};
}
std::optional<WorkflowCell> WorkflowCellState::read(WorkflowCellId id) const {
  owner();
  const auto found = cells_.find(id);
  if (found == cells_.end())
    return std::nullopt;
  return found->second;
}
std::uint64_t WorkflowCellState::revision() const {
  owner();
  return revision_;
}
WorkflowCellState::Prepared WorkflowCellState::prepare(const Value &request,
                                                       RecordReference record,
                                                       std::size_t ordinal,
                                                       const Value &events) const {
  owner();
  keys(request, {"submission", "reads", "writes", "decision_present", "decision"});
  const auto &submission = string_field(request, "submission");
  text(submission, 256);
  if (receipts_.contains(submission))
    throw Error{ErrorCode::conflict};
  if (receipts_.size() >= limits_.submissions || revision_ == UINT64_MAX)
    throw Error{ErrorCode::capacity};
  const auto decision = std::get<bool>(field(request, "decision_present").value());
  if (!decision && field(request, "decision") != Value{})
    throw Error{ErrorCode::invalid_range};
  const auto &reads = field(request, "reads").array();
  const auto &writes = field(request, "writes").array();
  if (reads.size() > limits_.batch || writes.size() > limits_.batch ||
      (writes.empty() && !decision && events.array().empty()))
    throw Error{ErrorCode::invalid_range};
  validate_references(events, record, limits_.batch);
  std::map<WorkflowCellId, std::uint64_t> expected;
  for (const auto &read : reads) {
    keys(read, {"id", "revision"});
    const auto id = parse_identity<WorkflowCellId>(field(read, "id"));
    const auto revision = integer(field(read, "revision"));
    if (!expected.emplace(id, revision).second)
      throw Error{ErrorCode::invalid_range};
    const auto current = cells_.find(id);
    if (revision != (current == cells_.end() ? 0 : current->second.revision.value))
      throw Error{ErrorCode::conflict};
  }
  Prepared prepared{{}, {}, Value{}, revision_ + 1};
  std::size_t additions = 0;
  for (const auto &write : writes) {
    keys(write, {"id", "schema", "value"});
    const auto id = parse_identity<WorkflowCellId>(field(write, "id"));
    const auto found = expected.find(id);
    if (found == expected.end() || found->second == UINT64_MAX)
      throw Error{ErrorCode::invalid_range};
    auto schema = read_schema(field(write, "schema"));
    const auto current = cells_.find(id);
    if (current != cells_.end() && current->second.schema != schema)
      throw Error{ErrorCode::conflict};
    if (current == cells_.end())
      ++additions;
    if (!prepared.changed
             .emplace(id, WorkflowCell{id,
                                       std::move(schema),
                                       {found->second + 1},
                                       field(write, "value")})
             .second)
      throw Error{ErrorCode::invalid_range};
  }
  if (additions > limits_.cells - std::min(cells_.size(), limits_.cells))
    throw Error{ErrorCode::capacity};
  prepared.receipt.emplace(submission, Receipt{prepared.revision, record, ordinal});
  prepared.packet = Value::object({{"label", Value{std::string{delta_label}}},
                                   {"revision", number(prepared.revision)},
                                   {"request", request},
                                   {"events", events}});
  return prepared;
}
void WorkflowCellState::publish(Prepared &&prepared) noexcept {
  static_assert(std::is_nothrow_move_assignable_v<WorkflowCell>);
  while (!prepared.changed.empty()) {
    auto node = prepared.changed.extract(prepared.changed.begin());
    const auto current = cells_.find(node.key());
    if (current == cells_.end())
      cells_.insert(std::move(node));
    else
      current->second = std::move(node.mapped());
  }
  receipts_.insert(prepared.receipt.extract(prepared.receipt.begin()));
  revision_ = prepared.revision;
}
void WorkflowCellState::replay(const Value &packet, RecordReference record,
                               std::size_t ordinal) {
  owner();
  try {
    keys(packet, {"label", "revision", "request", "events"});
    if (!label_is(packet, delta_label) || revision_ == UINT64_MAX ||
        integer(field(packet, "revision")) != revision_ + 1)
      throw Error{ErrorCode::corrupt};
    const auto &request = field(packet, "request");
    if (unwrap(encode_packet(request)) != unwrap(encode_packet(canonical(request))))
      throw Error{ErrorCode::corrupt};
    auto prepared = prepare(request, record, ordinal, field(packet, "events"));
    publish(std::move(prepared));
  } catch (const Error &error) {
    if (error.code == ErrorCode::allocation || error.code == ErrorCode::capacity)
      throw;
    throw Error{ErrorCode::corrupt};
  } catch (const std::bad_variant_access &) {
    throw Error{ErrorCode::corrupt};
  }
}
Value WorkflowCellState::snapshot() const {
  owner();
  Value::Array cells, receipts;
  cells.reserve(cells_.size());
  receipts.reserve(receipts_.size());
  for (const auto &[id, cell] : cells_)
    cells.push_back(Value::object({{"id", Value{hex_identity(id.bytes())}},
                                   {"schema", schema_value(cell.schema)},
                                   {"revision", number(cell.revision.value)},
                                   {"value", cell.value}}));
  for (const auto &[submission, receipt] : receipts_)
    receipts.push_back(Value::object({{"submission", Value{submission}},
                                      {"revision", number(receipt.revision)},
                                      {"record", reference_value(receipt.record)},
                                      {"ordinal", number(receipt.ordinal)}}));
  return Value::object({{"label", Value{std::string{state_label}}},
                        {"revision", number(revision_)},
                        {"cells", Value{std::move(cells)}},
                        {"receipts", Value{std::move(receipts)}}});
}
void WorkflowCellState::restore(const Value &snapshot) {
  owner();
  try {
    keys(snapshot, {"label", "revision", "cells", "receipts"});
    if (!label_is(snapshot, state_label))
      throw Error{ErrorCode::corrupt};
    WorkflowCellState candidate{limits_};
    candidate.revision_ = integer(field(snapshot, "revision"));
    const auto &cells = field(snapshot, "cells").array();
    const auto &receipts = field(snapshot, "receipts").array();
    if (cells.size() > limits_.cells || receipts.size() > limits_.submissions ||
        receipts.size() != candidate.revision_)
      throw Error{ErrorCode::corrupt};
    std::set<std::uint64_t> revisions;
    for (const auto &value : cells) {
      keys(value, {"id", "schema", "revision", "value"});
      const auto id = parse_identity<WorkflowCellId>(field(value, "id"));
      const auto revision = integer(field(value, "revision"));
      if (!revision || revision > candidate.revision_ ||
          !candidate.cells_
               .emplace(id, WorkflowCell{id,
                                         read_schema(field(value, "schema")),
                                         {revision},
                                         canonical(field(value, "value"))})
               .second)
        throw Error{ErrorCode::corrupt};
    }
    for (const auto &value : receipts) {
      keys(value, {"submission", "revision", "record", "ordinal"});
      const auto &submission = string_field(value, "submission");
      text(submission, 256);
      const auto revision = integer(field(value, "revision"));
      const auto ordinal = integer(field(value, "ordinal"));
      if (!revision || revision > candidate.revision_ || ordinal > SIZE_MAX ||
          !revisions.insert(revision).second ||
          !candidate.receipts_
               .emplace(submission,
                        Receipt{revision, read_reference(field(value, "record")),
                                static_cast<std::size_t>(ordinal)})
               .second)
        throw Error{ErrorCode::corrupt};
    }
    cells_.swap(candidate.cells_);
    receipts_.swap(candidate.receipts_);
    revision_ = candidate.revision_;
  } catch (const Error &error) {
    if (error.code == ErrorCode::allocation || error.code == ErrorCode::capacity)
      throw;
    throw Error{ErrorCode::corrupt};
  } catch (const std::bad_variant_access &) {
    throw Error{ErrorCode::corrupt};
  }
}

WorkflowStore::WorkflowStore(AuditLog &log, WorkflowStoreLimits limits)
    : log_(log), state_(limits) {
  std::optional<JournalCursor> boundary;
  std::size_t first_ordinal = 0;
  if (const auto *saved = log_.root().saved_state())
    for (const auto &packet : saved->programs.array())
      if (label_is(packet, state_label)) {
        if (boundary)
          throw Error{ErrorCode::corrupt};
        state_.restore(packet);
        boundary = saved->boundary;
        if (saved->fact_count > SIZE_MAX)
          throw Error{ErrorCode::capacity};
        first_ordinal = static_cast<std::size_t>(saved->fact_count);
      }
  const auto history = log_.root().history_snapshot();
  if (first_ordinal > history.count)
    throw Error{ErrorCode::corrupt};
  for (std::size_t ordinal = first_ordinal; ordinal < history.count; ++ordinal) {
    const auto fact = unwrap(history.read(ordinal));
    if (boundary && fact.record.journal == boundary->journal &&
        fact.record.sequence <= boundary->sequence)
      continue;
    const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
    if (!record || record->channel != ApplicationChannel::program)
      continue;
    const auto packet = unwrap(read_application_packet(*record));
    if (label_is(packet, delta_label))
      state_.replay(packet, fact.record, ordinal);
  }
}
std::optional<WorkflowCell> WorkflowStore::read(WorkflowCellId id) const {
  return state_.read(id);
}
std::uint64_t WorkflowStore::revision() const { return state_.revision(); }
Value WorkflowStore::snapshot() const { return state_.snapshot(); }
WorkflowCommit
WorkflowStore::transact(const WorkflowTransaction &transaction,
                        std::span<const RetainedEvent> events, bool settlement,
                        std::optional<ApplicationRecordId> reserved_record) {
  state_.owner();
  if (committing_)
    throw Error{ErrorCode::busy};
  committing_ = true;
  struct Reset {
    bool &value;
    ~Reset() { value = false; }
  } reset{committing_};
  if (events.size() > state_.limits_.batch ||
      transaction.reads.size() > state_.limits_.batch ||
      transaction.writes.size() > state_.limits_.batch)
    throw Error{ErrorCode::capacity};
  const auto request = request_value(transaction);
  const auto encoded_request = unwrap(encode_packet(request));
  const auto duplicate = state_.receipts_.find(transaction.submission);
  if (duplicate != state_.receipts_.end()) {
    const auto &receipt = duplicate->second;
    const auto fact = unwrap(log_.root().fact(receipt.ordinal));
    const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
    if (!record || record->channel != ApplicationChannel::program ||
        fact.record != receipt.record)
      throw Error{ErrorCode::corrupt};
    const auto packet = unwrap(read_application_packet(*record));
    if (!label_is(packet, delta_label) ||
        integer(field(packet, "revision")) != receipt.revision)
      throw Error{ErrorCode::corrupt};
    const auto &references = field(packet, "events").array();
    if (unwrap(encode_packet(field(packet, "request"))) != encoded_request ||
        references.size() != events.size())
      throw Error{ErrorCode::conflict};
    const auto maximum = log_.root().journal_limits().max_payload;
    for (std::size_t index = 0; index < events.size(); ++index) {
      if (index >= SIZE_MAX - receipt.ordinal)
        throw Error{ErrorCode::corrupt};
      const auto native = unwrap(log_.root().fact(receipt.ordinal + index + 1));
      if (native.record != read_reference(references[index]))
        throw Error{ErrorCode::corrupt};
      if (unwrap(encode_retained_event(native.event, maximum)) !=
          unwrap(encode_retained_event(events[index], maximum)))
        throw Error{ErrorCode::conflict};
    }
    return {true, receipt.revision, receipt.record};
  }
  // Check stale cell reads before issuing an application ID or touching native
  // identities. The provisional location is replaced before the actual append.
  const auto current = log_.root().cursor();
  if (current.sequence == UINT64_MAX ||
      events.size() > UINT64_MAX - current.sequence - 1)
    throw Error{ErrorCode::overflow};
  Value::Array references;
  references.reserve(events.size());
  for (std::size_t index = 0; index < events.size(); ++index)
    references.push_back(
        reference_value({current.journal, current.sequence + index + 2}));
  auto prepared = state_.prepare(request, {current.journal, current.sequence + 1},
                                 log_.root().fact_count(), Value{references});
  fresh_events(log_.root(), events);
  const auto identity = reserved_record ? *reserved_record : log_.issue();
  const auto expected = log_.root().cursor();
  if (expected.sequence == UINT64_MAX ||
      events.size() > UINT64_MAX - expected.sequence - 1)
    throw Error{ErrorCode::overflow};
  references.clear();
  for (std::size_t index = 0; index < events.size(); ++index)
    references.push_back(
        reference_value({expected.journal, expected.sequence + index + 2}));
  const RecordReference location{expected.journal, expected.sequence + 1};
  prepared.receipt.begin()->second.record = location;
  prepared.receipt.begin()->second.ordinal = log_.root().fact_count();
  prepared.packet.object().back().second = Value{std::move(references)};
  std::vector<RetainedEvent> batch;
  batch.reserve(events.size() + 1);
  batch.push_back({{},
                   ApplicationRecordEvent{identity, ApplicationChannel::program,
                                          unwrap(encode_packet(prepared.packet))}});
  batch.insert(batch.end(), events.begin(), events.end());
  std::vector<PhysicalJournalRecord> acknowledged(
      batch.size(), PhysicalJournalRecord{log_.root().environment(), expected.journal,
                                          FrameKind::semantic, 0, 0, 0, 0, 0});
  const auto revision = prepared.revision;
  bool accepted = false;
  auto publication = [&] {
    prepared.receipt.begin()->second.record = {acknowledged.front().journal,
                                               acknowledged.front().sequence};
    state_.publish(std::move(prepared));
  };
  try {
    const auto result =
        settlement
            ? log_.root().append_settlement(expected, batch, acknowledged, &accepted)
            : log_.root().append(expected, {}, batch, acknowledged, &accepted);
    if (accepted)
      publication();
    unwrap(result);
  } catch (...) {
    if (accepted && state_.revision_ != revision)
      publication();
    throw;
  }
  if (!accepted)
    throw Error{ErrorCode::audit_unavailable};
  return {
      false, revision, {acknowledged.front().journal, acknowledged.front().sequence}};
}
} // namespace blackbird
