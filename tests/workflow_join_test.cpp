#include "blackbird/packet.hpp"
#include "blackbird/workflow_join.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace blackbird;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class T> T take(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
void done(Result<void> result) {
  if (!result.has_value())
    throw result.error();
}
Value &edit(Value &object, std::string_view key) {
  for (auto &[name, value] : object.object())
    if (name == key)
      return value;
  throw std::runtime_error("missing fixture field");
}
WorkflowJoinDescriptor descriptor(WorkflowJoinMode mode) {
  WorkflowJoinDescriptor result;
  result.id = "join";
  result.mode = mode;
  result.members = {"b", "a", "c"};
  result.quorum = 2;
  return result;
}
WorkflowJoinObservation observation(std::string member, std::uint64_t sequence,
                                    WorkflowJoinOutcome outcome,
                                    std::uint64_t revision = 1) {
  return {std::move(member), revision,
          outcome,           {sequence, 1, 5},
          Value{"original"}, outcome != WorkflowJoinOutcome::unknown};
}
WorkflowJoinSelection proposal(const WorkflowJoin &join) {
  auto result = take(join.propose());
  if (!result.has_value())
    throw std::runtime_error("missing proposal");
  return *result;
}
WorkflowJoinSelection committed_choice(const WorkflowJoin &join) {
  const auto &selection = join.committed_selection();
  if (!selection.has_value())
    throw std::runtime_error("missing committed selection");
  return *selection;
}
void all_modes() {
  auto all = take(WorkflowJoin::create(descriptor(WorkflowJoinMode::all)));
  done(all.observe(observation("a", 2, WorkflowJoinOutcome::failure)));
  done(all.observe(observation("b", 1, WorkflowJoinOutcome::success)));
  done(all.observe(observation("c", 3, WorkflowJoinOutcome::unknown)));
  check(!take(all.propose()).has_value(), "unknown counted as terminal");
  done(all.observe(observation("c", 4, WorkflowJoinOutcome::cancelled, 2)));
  const auto selected = proposal(all);
  check(selected.reason() == WorkflowJoinReason::satisfied &&
            selected.observations().size() == 3 &&
            selected.observations()[0].member == "b" &&
            selected.observations()[1].member == "a" &&
            selected.observations()[2].outcome == WorkflowJoinOutcome::cancelled,
        "all did not preserve member order and typed outcomes");
  auto required = take(WorkflowJoin::create(descriptor(WorkflowJoinMode::all_success)));
  done(required.observe(observation("a", 1, WorkflowJoinOutcome::failure)));
  done(required.observe(observation("b", 2, WorkflowJoinOutcome::success)));
  check(!take(required.propose()).has_value(), "all_success fabricated finality");
  done(required.observe(observation("c", 3, WorkflowJoinOutcome::success)));
  check(proposal(required).reason() == WorkflowJoinReason::impossible,
        "all_success accepted failed member");
}
void racing_modes() {
  auto first = take(WorkflowJoin::create(descriptor(WorkflowJoinMode::first)));
  done(first.observe(observation("a", 8, WorkflowJoinOutcome::success)));
  done(first.observe(observation("c", 2, WorkflowJoinOutcome::failure)));
  check(proposal(first).observations()[0].member == "c", "first used delivery order");
  auto success =
      take(WorkflowJoin::create(descriptor(WorkflowJoinMode::first_success)));
  done(success.observe(observation("c", 1, WorkflowJoinOutcome::failure)));
  done(success.observe(observation("b", 4, WorkflowJoinOutcome::success)));
  done(success.observe(observation("a", 4, WorkflowJoinOutcome::success)));
  check(proposal(success).observations()[0].member == "a", "batch tie was unstable");
  auto quorum = take(WorkflowJoin::create(descriptor(WorkflowJoinMode::quorum)));
  done(quorum.observe(observation("a", 6, WorkflowJoinOutcome::success)));
  done(quorum.observe(observation("b", 2, WorkflowJoinOutcome::success)));
  done(quorum.observe(observation("c", 1, WorkflowJoinOutcome::failure)));
  const auto selected = proposal(quorum);
  check(selected.observations().size() == 2 &&
            selected.observations()[0].member == "b" &&
            selected.observations()[1].member == "a",
        "quorum order or predicate");
}
void commit_and_revisions() {
  auto join = take(WorkflowJoin::create(descriptor(WorkflowJoinMode::first_success)));
  const auto first = observation("b", 1, WorkflowJoinOutcome::success);
  done(join.observe(first));
  const auto prepared = proposal(join);
  check(!join.committed_selection().has_value(),
        "proposal exposed committed selection");
  const auto prior = join.revision();
  done(join.observe(first));
  check(join.revision() == prior, "exact duplicate changed revision");
  auto conflict = first;
  conflict.result = Value{"changed"};
  check(!join.observe(conflict).has_value(), "conflicting duplicate accepted");
  done(join.observe(observation("a", 2, WorkflowJoinOutcome::success)));
  check(!join.accept_committed(prepared).has_value(), "stale proposal committed");
  const auto selected = proposal(join);
  done(join.accept_committed(selected));
  done(join.accept_committed(selected));
  done(join.observe(observation("b", 3, WorkflowJoinOutcome::failure, 2)));
  check(committed_choice(join) == selected && proposal(join) == selected,
        "revision rewrote selected history");
  check(!join.observe(observation("a", 4, WorkflowJoinOutcome::success, 0)).has_value(),
        "zero result revision accepted");
}
void membership_and_collection() {
  auto config = descriptor(WorkflowJoinMode::quorum);
  config.membership_closed = false;
  auto join = take(WorkflowJoin::create(config));
  done(join.observe(observation("a", 1, WorkflowJoinOutcome::failure)));
  done(join.observe(observation("b", 2, WorkflowJoinOutcome::cancelled)));
  done(join.observe(observation("c", 3, WorkflowJoinOutcome::failure)));
  check(!take(join.propose()).has_value(), "open quorum became impossible");
  done(join.add_member("d"));
  check(!join.add_member("d").has_value(), "duplicate member accepted");
  done(join.close_membership());
  check(!join.add_member("e").has_value(), "closed membership changed");
  check(proposal(join).reason() == WorkflowJoinReason::impossible,
        "closed finite potential did not prove impossibility");
  done(join.observe(observation("d", 4, WorkflowJoinOutcome::failure)));
  check(proposal(join).reason() == WorkflowJoinReason::impossible,
        "closed failed quorum did not finish");
  auto collect_config = descriptor(WorkflowJoinMode::collect);
  collect_config.membership_closed = false;
  auto collect = take(WorkflowJoin::create(collect_config));
  for (const auto &member : collect_config.members)
    done(collect.observe(observation(member, 1, WorkflowJoinOutcome::success)));
  check(!take(collect.propose()).has_value(), "collect closed shared producer");
  done(collect.close_membership());
  check(proposal(collect).observations().size() == 3, "collect missing final rows");
  config.members = {"a", "a"};
  check(!WorkflowJoin::create(config).has_value(), "duplicate configured keys");
  config = descriptor(WorkflowJoinMode::collect);
  config.collect_limit = 2;
  check(!WorkflowJoin::create(config).has_value(), "collect bound not enforced");
}
void deadlines_and_select() {
  auto config = descriptor(WorkflowJoinMode::select);
  config.cutoff = WorkflowJoinCutoff{1, 10};
  auto join = take(WorkflowJoin::create(config));
  auto late = observation("a", 1, WorkflowJoinOutcome::success);
  late.acceptance.monotonic_ns = 11;
  done(join.observe(late));
  check(!take(join.propose()).has_value(), "late success beat delayed timeout");
  check(!join.observe_timeout({2, 1, 9}).has_value(), "early timer accepted");
  done(join.observe_timeout({2, 1, 12}));
  check(proposal(join).reason() == WorkflowJoinReason::timed_out &&
            proposal(join).timeout().has_value(),
        "typed timeout missing");
  auto early = take(WorkflowJoin::create(config));
  auto completed = observation("c", 2, WorkflowJoinOutcome::cancelled);
  completed.acceptance.monotonic_ns = 9;
  done(early.observe_timeout({3, 1, 12}));
  done(early.observe(completed));
  check(proposal(early).reason() == WorkflowJoinReason::satisfied &&
            proposal(early).observations()[0].outcome == WorkflowJoinOutcome::cancelled,
        "timely completion lost to timer delivery order");
  auto wrong = observation("a", 4, WorkflowJoinOutcome::success);
  wrong.acceptance.clock_epoch = 2;
  check(!early.observe(wrong).has_value(), "mixed epoch compared");
}
void predicates_and_snapshot() {
  auto config = descriptor(WorkflowJoinMode::quorum);
  config.predicate_revision = "original-v1";
  auto predicate = [](const WorkflowJoinObservation &row) {
    return row.outcome == WorkflowJoinOutcome::success &&
           row.result == Value{"original"};
  };
  check(!WorkflowJoin::create(config).has_value(), "missing pinned predicate accepted");
  auto join = take(WorkflowJoin::create(config, predicate));
  done(join.observe(observation("a", 2, WorkflowJoinOutcome::success)));
  done(join.observe(observation("b", 1, WorkflowJoinOutcome::success)));
  const auto selected = proposal(join);
  done(join.accept_committed(selected));
  done(join.observe(observation("b", 3, WorkflowJoinOutcome::failure, 2)));
  const auto bytes = take(encode_packet(join.snapshot()));
  auto restored = take(WorkflowJoin::restore(take(decode_packet(bytes)), predicate));
  check(restored.snapshot() == join.snapshot() &&
            committed_choice(restored) == selected,
        "BBM restore changed frozen selection or history");
  check(!WorkflowJoin::restore(join.snapshot()).has_value(),
        "restore lost predicate contract");
  check(!WorkflowJoin::restore(Value{"corrupt"}, predicate).has_value(),
        "invalid snapshot accepted");
}
void boundaries() {
  auto config = descriptor(WorkflowJoinMode::first_success);
  config.members = {"a"};
  config.max_observations = 1;
  auto limited = take(WorkflowJoin::create(config));
  const auto accepted = observation("a", 1, WorkflowJoinOutcome::failure);
  done(limited.observe(accepted));
  done(limited.observe(accepted));
  check(!limited.observe(observation("a", 2, WorkflowJoinOutcome::success, 2))
             .has_value(),
        "observation history exceeded capacity");
  check(proposal(limited).reason() == WorkflowJoinReason::impossible,
        "closed first_success ignored final failure");
  auto other_config = config;
  other_config.id = "other";
  auto other = take(WorkflowJoin::create(other_config));
  done(other.observe(accepted));
  check(!other.accept_committed(proposal(limited)).has_value(),
        "selection crossed join identity");
  auto deadline_config = config;
  deadline_config.mode = WorkflowJoinMode::select;
  deadline_config.cutoff = WorkflowJoinCutoff{1, 10};
  auto delayed = take(WorkflowJoin::create(deadline_config));
  auto late = observation("a", 1, WorkflowJoinOutcome::success);
  late.acceptance.monotonic_ns = 10;
  done(delayed.observe(late));
  check(!take(delayed.propose()).has_value(),
        "cutoff equality fabricated impossibility");
  done(delayed.observe_timeout({2, 1, 10}));
  const auto timed_out = proposal(delayed);
  done(delayed.accept_committed(timed_out));
  const auto restored = take(WorkflowJoin::restore(delayed.snapshot()));
  check(committed_choice(restored) == timed_out,
        "typed timeout did not survive restore");
  auto open_config = config;
  open_config.membership_closed = false;
  auto open = take(WorkflowJoin::create(open_config));
  done(open.observe(observation("a", 1, WorkflowJoinOutcome::success)));
  done(open.accept_committed(proposal(open)));
  check(!open.add_member("later").has_value(), "selected observer membership changed");
  auto pending = take(WorkflowJoin::create(descriptor(WorkflowJoinMode::all)));
  done(pending.observe(observation("a", 1, WorkflowJoinOutcome::unknown)));
  check(take(WorkflowJoin::restore(pending.snapshot())).snapshot() ==
            pending.snapshot(),
        "pending unresolved snapshot changed");
}
void finite_quorum_and_success_predicates() {
  auto config = descriptor(WorkflowJoinMode::quorum);
  auto join = take(WorkflowJoin::create(config));
  done(join.observe(observation("a", 1, WorkflowJoinOutcome::failure)));
  done(join.observe(observation("b", 2, WorkflowJoinOutcome::cancelled)));
  check(proposal(join).reason() == WorkflowJoinReason::impossible,
        "finite closed bound waited for unseen member");
  auto candidate = take(join.prepare_acceptance(proposal(join)));
  join = std::move(candidate);
  check(committed_choice(take(WorkflowJoin::restore(join.snapshot()))) ==
            committed_choice(join),
        "finite impossible selection changed on restore");
  config.quorum = 4;
  check(proposal(take(WorkflowJoin::create(config))).reason() ==
            WorkflowJoinReason::impossible,
        "quorum above cardinality remained pending");
  config.membership_closed = false;
  check(!take(take(WorkflowJoin::create(config)).propose()).has_value(),
        "open expansion fabricated a finite bound");
  for (const auto mode : {WorkflowJoinMode::all_success,
                          WorkflowJoinMode::first_success, WorkflowJoinMode::quorum}) {
    auto guarded = descriptor(mode);
    guarded.members = {"a"};
    guarded.quorum = 1;
    guarded.predicate_revision = "always-true";
    auto success =
        take(WorkflowJoin::create(guarded, [](const auto &) { return true; }));
    done(success.observe(observation("a", 1, WorkflowJoinOutcome::failure)));
    check(proposal(success).reason() == WorkflowJoinReason::impossible,
          "custom predicate erased typed failure");
  }
  auto select_config = descriptor(WorkflowJoinMode::select);
  select_config.predicate_revision = "failure-v1";
  auto selected = take(WorkflowJoin::create(select_config, [](const auto &row) {
    return row.outcome == WorkflowJoinOutcome::failure;
  }));
  done(selected.observe(observation("a", 1, WorkflowJoinOutcome::failure)));
  check(proposal(selected).reason() == WorkflowJoinReason::satisfied,
        "typed select could not observe requested failure");
}
void epochs_and_prepared_publication() {
  auto join = take(WorkflowJoin::create(descriptor(WorkflowJoinMode::first_success)));
  done(join.observe(observation("a", 1, WorkflowJoinOutcome::success)));
  auto other_epoch = observation("b", 1, WorkflowJoinOutcome::success);
  other_epoch.acceptance.clock_epoch = 2;
  const auto rejected = join.observe(other_epoch);
  check(!rejected.has_value() && rejected.error().code == ErrorCode::wrong_clock,
        "no-deadline join accepted another epoch");
  done(join.observe(observation("b", 2, WorkflowJoinOutcome::success)));
  auto corrupt = join.snapshot();
  auto &rows = edit(corrupt, "history").array();
  edit(edit(edit(rows[1], "observation"), "acceptance"), "epoch") = Value{Number{2}};
  check(!WorkflowJoin::restore(corrupt).has_value(),
        "restore accepted mixed clock epochs");
  bool committed = false;
  std::size_t predicate_calls = 0;
  auto config = descriptor(WorkflowJoinMode::first_success);
  config.predicate_revision = "pure-v1";
  auto staged = take(WorkflowJoin::create(config, [&](const auto &) {
    check(!committed, "postcommit publication reinvoked predicate");
    ++predicate_calls;
    return true;
  }));
  done(staged.observe(observation("a", 1, WorkflowJoinOutcome::success)));
  const auto selection = proposal(staged);
  auto prepared = take(staged.prepare_acceptance(selection));
  const auto bytes = take(encode_packet(prepared.snapshot()));
  check(!bytes.empty() && !staged.committed_selection().has_value(),
        "prepared candidate exposed live committed selection");
  const auto count = predicate_calls;
  committed = true;
  staged = std::move(prepared);
  check(predicate_calls == count && committed_choice(staged) == selection,
        "noexcept adoption changed selection or invoked predicate");
}
void final_unknown_results() {
  for (const auto mode :
       {WorkflowJoinMode::all, WorkflowJoinMode::first, WorkflowJoinMode::collect}) {
    auto config = descriptor(mode);
    config.members = {"a"};
    auto join = take(WorkflowJoin::create(config));
    auto unknown = observation("a", 1, WorkflowJoinOutcome::unknown);
    done(join.observe(unknown));
    check(!take(join.propose()).has_value(), "unresolved observation finalized");
    unknown.result_revision = 2;
    unknown.acceptance.sequence = 2;
    unknown.final = true;
    done(join.observe(unknown));
    const auto selection = proposal(join);
    check(selection.reason() == WorkflowJoinReason::satisfied &&
              selection.observations()[0].outcome == WorkflowJoinOutcome::unknown &&
              selection.observations()[0].final,
          "final unknown was confused with running work");
  }
}
void untimed_durable_order() {
  auto config = descriptor(WorkflowJoinMode::all);
  auto join = take(WorkflowJoin::create(config));
  auto first = observation("a", 8, WorkflowJoinOutcome::success);
  first.acceptance = {8, 0, 0};
  done(join.observe(first));
  auto reopened = take(
      WorkflowJoin::restore(take(decode_packet(take(encode_packet(join.snapshot()))))));
  auto prior = observation("b", 3, WorkflowJoinOutcome::success);
  prior.acceptance = {3, 0, 0};
  done(reopened.observe(prior));
  auto final = observation("c", 12, WorkflowJoinOutcome::success);
  final.acceptance = {12, 0, 0};
  done(reopened.observe(final));
  auto accepted = take(reopened.prepare_acceptance(proposal(reopened)));
  reopened = std::move(accepted);
  check(take(WorkflowJoin::restore(reopened.snapshot())).snapshot() ==
            reopened.snapshot(),
        "untimed committed selection changed on reopen");
  const auto measured = observation("a", 13, WorkflowJoinOutcome::success, 2);
  check(!reopened.observe(measured).has_value(),
        "measured input mixed into untimed join");
  auto invalid = first;
  invalid.result_revision = 2;
  invalid.acceptance = {14, 0, 1};
  check(!reopened.observe(invalid).has_value(), "unmeasured observation claimed ns");
  auto timed_config = config;
  timed_config.cutoff = WorkflowJoinCutoff{1, 20};
  auto timed = take(WorkflowJoin::create(timed_config));
  check(!timed.observe(first).has_value(), "untimed observation compared to cutoff");
  auto race_config = descriptor(WorkflowJoinMode::first_success);
  auto race = take(WorkflowJoin::create(race_config));
  done(race.observe(first));
  done(race.observe(prior));
  check(proposal(race).observations()[0].member == "b",
        "untimed selection ignored retained acceptance order");
}
void permute_objects(Value &value) {
  if (std::holds_alternative<Value::Object>(value.value())) {
    auto &object = value.object();
    std::reverse(object.begin(), object.end());
    for (auto &[key, child] : object) {
      (void)key;
      permute_objects(child);
    }
  } else if (std::holds_alternative<Value::Array>(value.value())) {
    for (auto &child : value.array())
      permute_objects(child);
  }
}
void object_order_restore() {
  auto config = descriptor(WorkflowJoinMode::first_success);
  auto join = take(WorkflowJoin::create(config));
  auto row = observation("a", 1, WorkflowJoinOutcome::success);
  row.result = Value::object({{"z", Value{Number{9}}}, {"a", Value{"bytes"}}});
  done(join.observe(row));
  auto accepted = take(join.prepare_acceptance(proposal(join)));
  join = std::move(accepted);
  auto reordered = join.snapshot();
  permute_objects(reordered);
  auto restored =
      take(WorkflowJoin::restore(take(decode_packet(take(encode_packet(reordered))))));
  check(committed_choice(restored).observations()[0].member == "a",
        "reordered persisted object changed winner");
  auto duplicate = reordered;
  const auto repeated = duplicate.object().front();
  duplicate.object().push_back(repeated);
  check(!WorkflowJoin::restore(duplicate).has_value(),
        "duplicate snapshot keys accepted");
  auto nested = reordered;
  auto &selection = edit(nested, "selection");
  const auto repeated_selection = selection.object().front();
  selection.object().push_back(repeated_selection);
  check(!WorkflowJoin::restore(nested).has_value(),
        "duplicate selection keys accepted");
}
void numeric_wire_restore() {
  auto join = take(WorkflowJoin::create(descriptor(WorkflowJoinMode::first_success)));
  auto row = observation("a", 1, WorkflowJoinOutcome::success);
  row.acceptance.monotonic_ns = 0;
  row.result = Value{Value::Array{
      Value{Number{std::int64_t{0}}}, Value{Number{std::uint64_t{42}}},
      Value{Number{std::int64_t{-1}}}, Value{Number{1.0}}, Value{Number{-0.0}}}};
  done(join.observe(row));
  auto candidate = take(join.prepare_acceptance(proposal(join)));
  join = std::move(candidate);
  auto restored = take(
      WorkflowJoin::restore(take(decode_packet(take(encode_packet(join.snapshot()))))));
  check(take(encode_packet(committed_choice(restored).observations()[0].result)) ==
            take(encode_packet(row.result)),
        "numeric native wire forms changed on reopen");
  auto wrong_type = join.snapshot();
  auto &selection_rows = edit(edit(wrong_type, "selection"), "observations").array();
  edit(selection_rows[0], "result").array()[0] = Value{Number{0.0}};
  check(!WorkflowJoin::restore(wrong_type).has_value(),
        "real result replaced integer wire type");
  auto wrong_zero = join.snapshot();
  auto &zero_rows = edit(edit(wrong_zero, "selection"), "observations").array();
  edit(zero_rows[0], "result").array()[4] = Value{Number{0.0}};
  check(!WorkflowJoin::restore(wrong_zero).has_value(),
        "real sign-bit mutation was ignored");
}
} // namespace
int main() {
  try {
    all_modes();
    racing_modes();
    commit_and_revisions();
    membership_and_collection();
    deadlines_and_select();
    predicates_and_snapshot();
    boundaries();
    finite_quorum_and_success_predicates();
    epochs_and_prepared_publication();
    final_unknown_results();
    untimed_durable_order();
    object_order_restore();
    numeric_wire_restore();
    std::cout << "workflow joins passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
