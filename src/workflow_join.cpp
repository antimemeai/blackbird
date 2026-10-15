#include "blackbird/workflow_join.hpp"
#include <algorithm>
#include <bit>
#include <map>
#include <set>

namespace blackbird {
namespace {
Value number(std::uint64_t input) { return Value{Number{input}}; }
const Value &required(const Value &value, std::string_view key) {
  const auto *found = value.find(key);
  if (!found)
    throw Error{ErrorCode::corrupt};
  return *found;
}
std::uint64_t unsigned_number(const Value &value) {
  const auto *numeric = std::get_if<Number>(&value.value());
  if (!numeric)
    throw Error{ErrorCode::corrupt};
  if (const auto *positive = std::get_if<std::uint64_t>(&numeric->storage()))
    return *positive;
  if (const auto *signed_value = std::get_if<std::int64_t>(&numeric->storage());
      signed_value && *signed_value >= 0)
    return static_cast<std::uint64_t>(*signed_value);
  throw Error{ErrorCode::corrupt};
}
std::int64_t signed_number(const Value &value) {
  const auto *numeric = std::get_if<Number>(&value.value());
  if (!numeric)
    throw Error{ErrorCode::corrupt};
  if (const auto *signed_value = std::get_if<std::int64_t>(&numeric->storage()))
    return *signed_value;
  if (const auto *positive = std::get_if<std::uint64_t>(&numeric->storage());
      positive && *positive <= static_cast<std::uint64_t>(INT64_MAX))
    return static_cast<std::int64_t>(*positive);
  throw Error{ErrorCode::corrupt};
}
std::size_t size_number(const Value &value) {
  const auto result = unsigned_number(value);
  if (result > SIZE_MAX)
    throw Error{ErrorCode::corrupt};
  return static_cast<std::size_t>(result);
}
bool valid_key(const std::string &key) {
  return !key.empty() && key.find('\0') == std::string::npos && valid_utf8(key);
}
bool same_number(const Number &left, const Number &right) {
  if (const auto *integer = std::get_if<std::int64_t>(&left.storage())) {
    if (const auto *other = std::get_if<std::int64_t>(&right.storage()))
      return *integer == *other;
    const auto *other = std::get_if<std::uint64_t>(&right.storage());
    return other && *integer >= 0 && static_cast<std::uint64_t>(*integer) == *other;
  }
  if (const auto *integer = std::get_if<std::uint64_t>(&left.storage())) {
    if (const auto *other = std::get_if<std::uint64_t>(&right.storage()))
      return *integer == *other;
    const auto *other = std::get_if<std::int64_t>(&right.storage());
    return other && *other >= 0 && *integer == static_cast<std::uint64_t>(*other);
  }
  if (const auto *real = std::get_if<double>(&left.storage())) {
    const auto *other = std::get_if<double>(&right.storage());
    return other &&
           std::bit_cast<std::uint64_t>(*real) == std::bit_cast<std::uint64_t>(*other);
  }
  return left.storage() == right.storage();
}
bool same_value(const Value &left, const Value &right) {
  if (const auto *object = std::get_if<Value::Object>(&left.value())) {
    const auto *other = std::get_if<Value::Object>(&right.value());
    if (!other || object->size() != other->size())
      return false;
    std::map<std::string, const Value *> members;
    for (const auto &[key, child] : *object)
      if (!members.emplace(key, &child).second)
        return false;
    std::set<std::string> seen;
    for (const auto &[key, child] : *other) {
      const auto found = members.find(key);
      if (!seen.insert(key).second || found == members.end() ||
          !same_value(*found->second, child))
        return false;
    }
    return true;
  }
  if (const auto *array = std::get_if<Value::Array>(&left.value())) {
    const auto *other = std::get_if<Value::Array>(&right.value());
    return other && array->size() == other->size() &&
           std::equal(array->begin(), array->end(), other->begin(), same_value);
  }
  if (const auto *number_value = std::get_if<Number>(&left.value())) {
    const auto *other = std::get_if<Number>(&right.value());
    return other && same_number(*number_value, *other);
  }
  return left == right;
}
bool terminal(const WorkflowJoinObservation &row) { return row.final; }
Value acceptance_value(const WorkflowJoinAcceptance &input) {
  return Value::object({{"sequence", number(input.sequence)},
                        {"epoch", number(input.clock_epoch)},
                        {"ns", Value{Number{input.monotonic_ns}}}});
}
WorkflowJoinAcceptance acceptance_from(const Value &value) {
  return {unsigned_number(required(value, "sequence")),
          unsigned_number(required(value, "epoch")),
          signed_number(required(value, "ns"))};
}
Value observation_value(const WorkflowJoinObservation &input) {
  return Value::object({{"member", Value{input.member}},
                        {"revision", number(input.result_revision)},
                        {"outcome", number(static_cast<std::uint64_t>(input.outcome))},
                        {"acceptance", acceptance_value(input.acceptance)},
                        {"result", input.result},
                        {"final", Value{input.final}}});
}
WorkflowJoinObservation observation_from(const Value &value) {
  const auto outcome = unsigned_number(required(value, "outcome"));
  if (outcome > static_cast<std::uint64_t>(WorkflowJoinOutcome::unknown))
    throw Error{ErrorCode::corrupt};
  return {required(value, "member").string(),
          unsigned_number(required(value, "revision")),
          static_cast<WorkflowJoinOutcome>(outcome),
          acceptance_from(required(value, "acceptance")),
          required(value, "result"),
          std::get<bool>(required(value, "final").value())};
}
Value descriptor_value(const WorkflowJoinDescriptor &input) {
  Value::Array members;
  for (const auto &member : input.members)
    members.emplace_back(member);
  Value cutoff;
  if (input.cutoff.has_value())
    cutoff = Value::object({{"epoch", number(input.cutoff.value().clock_epoch)},
                            {"ns", Value{Number{input.cutoff.value().monotonic_ns}}}});
  return Value::object({{"id", Value{input.id}},
                        {"mode", number(static_cast<std::uint64_t>(input.mode))},
                        {"members", Value{std::move(members)}},
                        {"closed", Value{input.membership_closed}},
                        {"max_members", number(input.max_members)},
                        {"max_observations", number(input.max_observations)},
                        {"quorum", number(input.quorum)},
                        {"collect_limit", number(input.collect_limit)},
                        {"predicate", Value{input.predicate_revision}},
                        {"cutoff", std::move(cutoff)}});
}
WorkflowJoinDescriptor descriptor_from(const Value &value) {
  WorkflowJoinDescriptor result;
  result.id = required(value, "id").string();
  const auto mode = unsigned_number(required(value, "mode"));
  if (mode > static_cast<std::uint64_t>(WorkflowJoinMode::collect))
    throw Error{ErrorCode::corrupt};
  result.mode = static_cast<WorkflowJoinMode>(mode);
  for (const auto &member : required(value, "members").array())
    result.members.push_back(member.string());
  result.membership_closed = std::get<bool>(required(value, "closed").value());
  result.max_members = size_number(required(value, "max_members"));
  result.max_observations = size_number(required(value, "max_observations"));
  result.quorum = size_number(required(value, "quorum"));
  result.collect_limit = size_number(required(value, "collect_limit"));
  result.predicate_revision = required(value, "predicate").string();
  const auto &cutoff = required(value, "cutoff");
  if (!std::holds_alternative<std::nullptr_t>(cutoff.value()))
    result.cutoff = WorkflowJoinCutoff{unsigned_number(required(cutoff, "epoch")),
                                       signed_number(required(cutoff, "ns"))};
  return result;
}
bool earlier(const WorkflowJoinObservation &left,
             const WorkflowJoinObservation &right) {
  if (left.acceptance.sequence != right.acceptance.sequence)
    return left.acceptance.sequence < right.acceptance.sequence;
  return left.member < right.member;
}
bool compatible_order(const WorkflowJoinAcceptance &left,
                      const WorkflowJoinAcceptance &right) {
  if (left.clock_epoch != right.clock_epoch)
    return false;
  if (left.sequence == right.sequence)
    return left.monotonic_ns == right.monotonic_ns;
  return left.sequence < right.sequence ? left.monotonic_ns <= right.monotonic_ns
                                        : left.monotonic_ns >= right.monotonic_ns;
}
} // namespace

WorkflowJoinSelection::WorkflowJoinSelection(
    std::string join_id, std::uint64_t revision, WorkflowJoinReason reason,
    std::vector<WorkflowJoinObservation> observations,
    std::optional<WorkflowJoinAcceptance> timeout)
    : join_id_(std::move(join_id)), base_revision_(revision), reason_(reason),
      observations_(std::move(observations)), timeout_(timeout) {}
Value WorkflowJoinSelection::snapshot() const {
  Value::Array rows;
  for (const auto &row : observations_)
    rows.push_back(observation_value(row));
  return Value::object(
      {{"join", Value{join_id_}},
       {"base", number(base_revision_)},
       {"reason", number(static_cast<std::uint64_t>(reason_))},
       {"observations", Value{std::move(rows)}},
       {"timeout",
        timeout_.has_value() ? acceptance_value(timeout_.value()) : Value{}}});
}
WorkflowJoin::WorkflowJoin(WorkflowJoinDescriptor descriptor, Predicate predicate)
    : descriptor_(std::move(descriptor)), predicate_(std::move(predicate)) {}
Result<WorkflowJoin> WorkflowJoin::create(WorkflowJoinDescriptor descriptor,
                                          Predicate predicate) {
  if (!valid_key(descriptor.id) || descriptor.max_members == 0 ||
      descriptor.max_observations == 0 ||
      descriptor.members.size() > descriptor.max_members || descriptor.quorum == 0 ||
      descriptor.collect_limit == 0 ||
      static_cast<std::uint64_t>(descriptor.mode) >
          static_cast<std::uint64_t>(WorkflowJoinMode::collect) ||
      (descriptor.cutoff.has_value() && descriptor.cutoff.value().clock_epoch == 0) ||
      (static_cast<bool>(predicate) != !descriptor.predicate_revision.empty()))
    return Result<WorkflowJoin>::failure({ErrorCode::invalid_range});
  std::set<std::string> keys;
  for (const auto &member : descriptor.members)
    if (!valid_key(member) || !keys.insert(member).second)
      return Result<WorkflowJoin>::failure({ErrorCode::conflict});
  if (descriptor.mode == WorkflowJoinMode::collect &&
      descriptor.members.size() > descriptor.collect_limit)
    return Result<WorkflowJoin>::failure({ErrorCode::capacity});
  return Result<WorkflowJoin>::success(
      WorkflowJoin{std::move(descriptor), std::move(predicate)});
}
Result<void> WorkflowJoin::add_member(std::string member) {
  if (selection_.has_value() || descriptor_.membership_closed || !valid_key(member) ||
      std::find(descriptor_.members.begin(), descriptor_.members.end(), member) !=
          descriptor_.members.end())
    return Result<void>::failure({ErrorCode::conflict});
  if (revision_ == UINT64_MAX ||
      descriptor_.members.size() >= descriptor_.max_members ||
      (descriptor_.mode == WorkflowJoinMode::collect &&
       descriptor_.members.size() >= descriptor_.collect_limit))
    return Result<void>::failure({ErrorCode::capacity});
  descriptor_.members.push_back(std::move(member));
  ++revision_;
  return Result<void>::success();
}
Result<void> WorkflowJoin::close_membership() {
  if (descriptor_.membership_closed)
    return Result<void>::success();
  if (selection_.has_value())
    return Result<void>::failure({ErrorCode::conflict});
  if (revision_ == UINT64_MAX)
    return Result<void>::failure({ErrorCode::overflow});
  descriptor_.membership_closed = true;
  ++revision_;
  return Result<void>::success();
}
Result<void>
WorkflowJoin::validate_acceptance(const WorkflowJoinAcceptance &acceptance) const {
  if (acceptance.sequence == 0 ||
      (acceptance.clock_epoch == 0 && acceptance.monotonic_ns != 0))
    return Result<void>::failure({ErrorCode::invalid_identity});
  if (descriptor_.cutoff.has_value() &&
      descriptor_.cutoff.value().clock_epoch != acceptance.clock_epoch)
    return Result<void>::failure({ErrorCode::wrong_clock});
  if (!history_.empty() &&
      history_.front().observation.acceptance.clock_epoch != acceptance.clock_epoch)
    return Result<void>::failure({ErrorCode::wrong_clock});
  for (const auto &record : history_)
    if (!compatible_order(record.observation.acceptance, acceptance))
      return Result<void>::failure({ErrorCode::clock_regressed});
  if (timeout_.has_value() && !compatible_order(timeout_.value(), acceptance))
    return Result<void>::failure({ErrorCode::clock_regressed});
  return Result<void>::success();
}
Result<void> WorkflowJoin::observe(WorkflowJoinObservation observation) {
  if (observation.result_revision == 0 ||
      (!observation.final && observation.outcome != WorkflowJoinOutcome::unknown) ||
      static_cast<std::uint64_t>(observation.outcome) >
          static_cast<std::uint64_t>(WorkflowJoinOutcome::unknown) ||
      std::find(descriptor_.members.begin(), descriptor_.members.end(),
                observation.member) == descriptor_.members.end())
    return Result<void>::failure({ErrorCode::invalid_identity});
  std::uint64_t latest = 0;
  for (const auto &record : history_) {
    const auto &old = record.observation;
    if (old.member == observation.member) {
      if (old.result_revision == observation.result_revision)
        return old == observation ? Result<void>::success()
                                  : Result<void>::failure({ErrorCode::conflict});
      latest = std::max(latest, old.result_revision);
    }
  }
  if (observation.result_revision <= latest)
    return Result<void>::failure({ErrorCode::conflict});
  const auto accepted = validate_acceptance(observation.acceptance);
  if (!accepted.has_value())
    return accepted;
  if (revision_ == UINT64_MAX || history_.size() >= descriptor_.max_observations)
    return Result<void>::failure({ErrorCode::capacity});
  history_.push_back({std::move(observation), revision_ + 1});
  ++revision_;
  return Result<void>::success();
}
Result<void> WorkflowJoin::observe_timeout(WorkflowJoinAcceptance acceptance) {
  if (timeout_.has_value())
    return timeout_.value() == acceptance
               ? Result<void>::success()
               : Result<void>::failure({ErrorCode::conflict});
  if (!descriptor_.cutoff.has_value())
    return Result<void>::failure({ErrorCode::unsupported});
  const auto accepted = validate_acceptance(acceptance);
  if (!accepted.has_value())
    return accepted;
  if (acceptance.monotonic_ns < descriptor_.cutoff.value().monotonic_ns)
    return Result<void>::failure({ErrorCode::invalid_range});
  if (revision_ == UINT64_MAX)
    return Result<void>::failure({ErrorCode::overflow});
  timeout_ = acceptance;
  timeout_revision_ = ++revision_;
  return Result<void>::success();
}
bool WorkflowJoin::qualifies(const WorkflowJoinObservation &observation) const {
  if (descriptor_.mode == WorkflowJoinMode::select)
    return predicate_ ? predicate_(observation) : terminal(observation);
  return observation.outcome == WorkflowJoinOutcome::success &&
         (!predicate_ || predicate_(observation));
}
bool WorkflowJoin::timely(const WorkflowJoinObservation &observation) const {
  return !descriptor_.cutoff.has_value() ||
         observation.acceptance.monotonic_ns < descriptor_.cutoff.value().monotonic_ns;
}
std::vector<WorkflowJoinObservation> WorkflowJoin::current_observations() const {
  std::map<std::string, const WorkflowJoinObservation *> latest;
  for (const auto &record : history_)
    latest[record.observation.member] = &record.observation;
  std::vector<WorkflowJoinObservation> result;
  for (const auto &member : descriptor_.members) {
    const auto found = latest.find(member);
    if (found != latest.end())
      result.push_back(*found->second);
  }
  return result;
}
Result<std::optional<WorkflowJoinSelection>> WorkflowJoin::propose() const {
  using Proposal = Result<std::optional<WorkflowJoinSelection>>;
  if (selection_.has_value())
    return Proposal::success(selection_);
  auto current = current_observations();
  const auto all_final = descriptor_.membership_closed &&
                         current.size() == descriptor_.members.size() &&
                         std::all_of(current.begin(), current.end(), terminal);
  std::vector<WorkflowJoinObservation> qualified;
  for (const auto &row : current) {
    const bool any_terminal =
        descriptor_.mode == WorkflowJoinMode::first ||
        (descriptor_.mode == WorkflowJoinMode::select && !predicate_);
    if (terminal(row) && timely(row) && (any_terminal || qualifies(row)))
      qualified.push_back(row);
  }
  const auto complete = [&](WorkflowJoinReason reason,
                            std::vector<WorkflowJoinObservation> rows) {
    return Proposal::success(WorkflowJoinSelection{
        descriptor_.id, revision_, reason, std::move(rows),
        reason == WorkflowJoinReason::timed_out ? timeout_ : std::nullopt});
  };
  const bool all_mode = descriptor_.mode == WorkflowJoinMode::all ||
                        descriptor_.mode == WorkflowJoinMode::all_success ||
                        descriptor_.mode == WorkflowJoinMode::collect;
  if (all_mode && all_final &&
      std::all_of(current.begin(), current.end(),
                  [&](const auto &row) { return timely(row); })) {
    const bool success = descriptor_.mode != WorkflowJoinMode::all_success ||
                         qualified.size() == current.size();
    return complete(success ? WorkflowJoinReason::satisfied
                            : WorkflowJoinReason::impossible,
                    std::move(current));
  }
  if (!all_mode) {
    const auto needed =
        descriptor_.mode == WorkflowJoinMode::quorum ? descriptor_.quorum : 1;
    if (qualified.size() >= needed) {
      std::sort(qualified.begin(), qualified.end(), earlier);
      qualified.resize(needed);
      return complete(WorkflowJoinReason::satisfied, std::move(qualified));
    }
  }
  if (descriptor_.mode == WorkflowJoinMode::quorum && descriptor_.membership_closed) {
    const auto excluded = static_cast<std::size_t>(
        std::count_if(current.begin(), current.end(), [&](const auto &row) {
          return terminal(row) && !qualifies(row);
        }));
    // Each unseen/unknown member contributes at most one possible success. Late
    // success alone does not fabricate timeout: only a typed timer can time out.
    const auto possible = descriptor_.members.size() - excluded;
    if (possible < descriptor_.quorum)
      return complete(WorkflowJoinReason::impossible, std::move(current));
  }
  if (timeout_.has_value())
    return complete(WorkflowJoinReason::timed_out, {});
  if (all_final && std::all_of(current.begin(), current.end(),
                               [&](const auto &row) { return timely(row); }))
    return complete(WorkflowJoinReason::impossible, std::move(current));
  return Proposal::success(std::nullopt);
}
Result<void> WorkflowJoin::accept_committed(const WorkflowJoinSelection &selection) {
  if (selection_.has_value())
    return selection_.value() == selection
               ? Result<void>::success()
               : Result<void>::failure({ErrorCode::conflict});
  if (selection.base_revision() != revision_)
    return Result<void>::failure({ErrorCode::conflict});
  const auto candidate = propose();
  if (!candidate.has_value())
    return Result<void>::failure(candidate.error());
  const auto &choice = candidate.value();
  if (!choice.has_value())
    return Result<void>::failure({ErrorCode::conflict});
  if (*choice != selection)
    return Result<void>::failure({ErrorCode::conflict});
  if (revision_ == UINT64_MAX)
    return Result<void>::failure({ErrorCode::overflow});
  selection_ = selection;
  ++revision_;
  return Result<void>::success();
}
Result<WorkflowJoin>
WorkflowJoin::prepare_acceptance(const WorkflowJoinSelection &selection) const {
  try {
    auto candidate = *this;
    const auto accepted = candidate.accept_committed(selection);
    if (!accepted.has_value())
      return Result<WorkflowJoin>::failure(accepted.error());
    return Result<WorkflowJoin>::success(std::move(candidate));
  } catch (const std::bad_alloc &) {
    return Result<WorkflowJoin>::failure({ErrorCode::allocation});
  }
}
static_assert(std::is_nothrow_move_constructible_v<WorkflowJoin>);
static_assert(std::is_nothrow_move_assignable_v<WorkflowJoin>);
Value WorkflowJoin::snapshot() const {
  Value::Array history;
  for (const auto &record : history_)
    history.push_back(
        Value::object({{"state_revision", number(record.state_revision)},
                       {"observation", observation_value(record.observation)}}));
  return Value::object(
      {{"version", number(1)},
       {"descriptor", descriptor_value(descriptor_)},
       {"revision", number(revision_)},
       {"history", Value{std::move(history)}},
       {"timeout", timeout_.has_value() ? acceptance_value(timeout_.value()) : Value{}},
       {"timeout_revision", number(timeout_revision_)},
       {"selection",
        selection_.has_value() ? selection_.value().snapshot() : Value{}}});
}
Result<WorkflowJoin> WorkflowJoin::restore(const Value &snapshot, Predicate predicate) {
  try {
    if (unsigned_number(required(snapshot, "version")) != 1)
      return Result<WorkflowJoin>::failure({ErrorCode::unsupported});
    auto created =
        create(descriptor_from(required(snapshot, "descriptor")), std::move(predicate));
    if (!created.has_value())
      return created;
    auto result = std::move(created).value();
    const auto revision = unsigned_number(required(snapshot, "revision"));
    for (const auto &record : required(snapshot, "history").array()) {
      const auto state_revision = unsigned_number(required(record, "state_revision"));
      if (state_revision <= result.revision_ || state_revision > revision)
        throw Error{ErrorCode::corrupt};
      const auto old_size = result.history_.size();
      const auto observed =
          result.observe(observation_from(required(record, "observation")));
      if (!observed.has_value())
        throw observed.error();
      if (result.history_.size() != old_size + 1)
        throw Error{ErrorCode::corrupt};
      result.history_.back().state_revision = state_revision;
      result.revision_ = state_revision;
    }
    const auto timeout_revision =
        unsigned_number(required(snapshot, "timeout_revision"));
    const auto &timeout = required(snapshot, "timeout");
    if (!std::holds_alternative<std::nullptr_t>(timeout.value())) {
      if (timeout_revision == 0 || timeout_revision > revision)
        throw Error{ErrorCode::corrupt};
      if (std::any_of(result.history_.begin(), result.history_.end(),
                      [&](const auto &record) {
                        return record.state_revision == timeout_revision;
                      }))
        throw Error{ErrorCode::corrupt};
      result.revision_ = timeout_revision - 1;
      const auto observed = result.observe_timeout(acceptance_from(timeout));
      if (!observed.has_value())
        throw observed.error();
      result.timeout_revision_ = timeout_revision;
    } else if (timeout_revision != 0) {
      throw Error{ErrorCode::corrupt};
    }
    result.revision_ = revision;
    const auto &selection = required(snapshot, "selection");
    if (!std::holds_alternative<std::nullptr_t>(selection.value())) {
      const auto base = unsigned_number(required(selection, "base"));
      if (base >= revision)
        throw Error{ErrorCode::corrupt};
      auto basis = result;
      std::erase_if(basis.history_,
                    [&](const auto &record) { return record.state_revision > base; });
      if (basis.timeout_revision_ > base) {
        basis.timeout_.reset();
        basis.timeout_revision_ = 0;
      }
      basis.revision_ = base;
      const auto candidate = basis.propose();
      if (!candidate.has_value())
        throw Error{ErrorCode::corrupt};
      const auto &choice = candidate.value();
      if (!choice.has_value())
        throw Error{ErrorCode::corrupt};
      if (!same_value(choice->snapshot(), selection))
        throw Error{ErrorCode::corrupt};
      result.selection_ = *choice;
    }
    if (!same_value(result.snapshot(), snapshot))
      throw Error{ErrorCode::corrupt};
    return Result<WorkflowJoin>::success(std::move(result));
  } catch (const Error &error) {
    return Result<WorkflowJoin>::failure(error);
  } catch (const std::bad_variant_access &) {
    return Result<WorkflowJoin>::failure({ErrorCode::corrupt});
  }
}
} // namespace blackbird
