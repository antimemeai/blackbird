#include "blackbird/context.hpp"
#include "blackbird/json.hpp"
#include <filesystem>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <unistd.h>
using namespace blackbird;
void check(bool x, std::source_location where = std::source_location::current()) {
  if (!x)
    throw std::runtime_error("retained context contract line " +
                             std::to_string(where.line()));
}
template <class T> T take(Result<T> r) {
  if (!r.has_value())
    throw r.error();
  return std::move(r).value();
}
template <class T> T id(unsigned char n) {
  IdentityBytes b{};
  b[0] = std::byte{n};
  return take(T::from_bytes(b));
}
int main() {
  char name[] = "/tmp/arco-context-XXXXXX";
  const char *path = mkdtemp(name);
  if (!path)
    return 2;
  try {
    JournalHeader h{id<EnvironmentId>(1),
                    id<AuditStreamId>(2),
                    3,
                    {1024 * 1024, 4 * 1024 * 1024},
                    std::nullopt};
    JournalCapacity cap{32 * 1024 * 1024, 4096};
    auto root =
        take(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                       take(NativeJournalDirectory::open(path))),
                                   "audit", h, cap));
    AuditLog log{*root};
    const auto app = log.issue();
    const auto packet = Value::object({{"fixture", Value{true}}});
    log.record(app, ApplicationChannel::log, packet);
    const auto before_duplicate = root->cursor();
    log.record(app, ApplicationChannel::log, packet);
    check(root->cursor().sequence == before_duplicate.sequence);
    bool conflict = false;
    try {
      log.record(app, ApplicationChannel::log,
                 Value::object({{"fixture", Value{false}}}));
    } catch (const Error &e) {
      conflict = e.code == ErrorCode::conflict;
    }
    check(conflict && root->state() == JournalWriterState::live);
    RetainedEvent literal{{},
                          ApplicationRecordEvent{id<ApplicationRecordId>(7),
                                                 ApplicationChannel::context,
                                                 {std::byte{0x41}}}};
    const auto encoded = take(encode_retained_event(literal, 64));
    check(encoded.size() == 31 && encoded[0] == std::byte{1} &&
          encoded[2] == std::byte{14} && encoded[8] == std::byte{7} &&
          encoded[24] == std::byte{2} && encoded[26] == std::byte{1} &&
          encoded[30] == std::byte{0x41});
    check(take(decode_retained_event(encoded, 64)) == literal);
    auto malformed = encoded;
    malformed[24] = std::byte{4};
    check(!decode_retained_event(malformed, 64).has_value());
    ContextStore ctx{log};
    auto sizes = ctx.stats();
    check(field(sizes, "entry_count").number().text() == "0" &&
          field(sizes, "input_bytes").number().text() == "6");
    const auto literal_item = unwrap(parse_json(R"({"text":"é\n"})"));
    ctx.append({literal_item}, "size-fixture");
    sizes = ctx.stats();
    // BBM2 header4 + object2 + key6 + text5; array wrapper2.
    check(field(sizes, "input_bytes").number().text() == "19" &&
          field(field(sizes, "entries").array()[0], "item_bytes").number().text() ==
              "17");
    auto clear_sizes = ctx.view();
    clear_sizes.object()[1].second = Value{Value::Array{}};
    check(std::get<bool>(field(ctx.edit(clear_sizes), "accepted").value()));
    check(field(ctx.stats(), "input_bytes").number().text() == "6");
    const auto original =
        parse_json(
            R"({"type":"reasoning","encrypted_content":"OPAQUE==","extension":9007199254740993})")
            .value();
    ctx.append({original}, "fixture");
    auto initial = ctx.view();
    const auto entry = initial.find("entries")->array()[0].find("id")->string();
    auto empty = Value::object(
        {{"base", *initial.find("base")}, {"entries", Value{Value::Array{}}}});
    auto edited = ctx.edit(empty);
    check(std::get<bool>(edited.find("accepted")->value()));
    auto stale = ctx.edit(empty);
    check(!std::get<bool>(stale.find("accepted")->value()));
    check(ctx.view().find("entries")->array().empty());
    auto invalid = Value::object(
        {{"base", Value{ctx.head()}}, {"entries", Value{"not an array"}}});
    check(!std::get<bool>(ctx.edit(invalid).find("accepted")->value()));
    ctx.restore(entry);
    check(ctx.items()[0] == original);
    const auto head = ctx.head();
    log.original({"tool-output",
                  R"({"op":"edit","accepted":true,"entries":[],"revision":"evil"})",
                  Value{}});
    root.reset();
    root = take(RetainedState::open(std::make_unique<NativeJournalDirectory>(
                                        take(NativeJournalDirectory::open(path))),
                                    "audit", h, cap));
    check(root->confirm_recovery().has_value());
    struct EmptyCustody final : CustodyVerifier {
      Result<void> verify(std::span<const AttemptState> attempts) override {
        return attempts.empty() ? Result<void>::success()
                                : Result<void>::failure({ErrorCode::conflict});
      }
    } custody;
    check(root->reconcile(custody).has_value());
    AuditLog replay{*root};
    ContextStore restored{replay};
    check(restored.head() == head && restored.items()[0] == original);
    // Every published revision reports its kind and encoded input size; rejected
    // and staged changes report nothing (a1v indicator source).
    std::vector<ContextChange> changes;
    std::size_t bytes_now = 6;
    restored.changed = [&](const ContextChange &change) {
      check(change.revision == restored.head() && change.bytes_before == bytes_now &&
            change.bytes_after ==
                std::stoul(field(restored.stats(), "input_bytes").number().text()));
      bytes_now = *change.bytes_after;
      changes.push_back(change);
    };
    bytes_now = std::stoul(field(restored.stats(), "input_bytes").number().text());
    const auto kinds = [&] {
      std::string out;
      for (const auto &change : changes)
        out += change.kind == ContextChangeKind::edited     ? "E"
               : change.kind == ContextChangeKind::restored ? "R"
                                                            : "M:" + change.mode;
      return out;
    };
    restored.edit(
        Value::object({{"base", Value{head}}, {"entries", Value{Value::Array{}}}}));
    restored.restore(entry);
    check(restored.items()[0] == original);
    check(kinds() == "ER");
    auto ok = [](const Value &out) {
      return std::get<bool>(field(out, "accepted").value());
    };
    // A rejected edit reports nothing; a throwing listener cannot undo a publish.
    check(!ok(restored.edit(Value::object(
        {{"base", Value{"stale"}}, {"entries", Value{Value::Array{}}}}))));
    check(kinds() == "ER");
    {
      auto recorder = std::move(restored.changed);
      restored.changed = [](const ContextChange &) {
        throw Error{ErrorCode::capacity};
      };
      const auto published = restored.edit(restored.view());
      check(ok(published) && restored.head() == string_field(published, "revision"));
      restored.changed = std::move(recorder);
    }
    auto proposal = [&](std::string mode, Value::Array ids) {
      return Value::object({{"base", Value{restored.head()}},
                            {"mode", Value{std::move(mode)}},
                            {"ids", Value{std::move(ids)}},
                            {"reason", Value{"direct oracle"}},
                            {"source", Value{"explicit test input"}}});
    };
    restored.append(
        {Value::object({{"role", Value{"user"}}, {"content", Value{"MISSION"}}}),
         Value::object({{"type", Value{"function_call"}}, {"call_id", Value{"c1"}}}),
         Value::object({{"type", Value{"function_call_output"}},
                        {"call_id", Value{"c1"}},
                        {"output", Value{"opaque é\u0000"}}})},
        "managed-fixture");
    const auto all = field(restored.view(), "entries").array();
    const auto mission = field(all[1], "id"), call = field(all[2], "id"),
               output = field(all[3], "id");
    check(!ok(restored.manage(Value{})));
    check(!ok(restored.manage(proposal("archive", {call}))));
    check(!ok(restored.manage(proposal("archive", {mission}))));
    check(!ok(restored.manage(proposal("archive", {call, call}))));
    check(!ok(restored.manage(proposal("archive", {Value{"unknown"}}))));
    check(!ok(restored.manage(proposal("select", {}))));
    auto summarize = proposal("summarize", {call, output});
    summarize.object().emplace_back(
        "summary", Value::object({{"role", Value{"developer"}},
                                  {"content", Value{"tool completed"}}}));
    bytes_now = std::stoul(field(restored.stats(), "input_bytes").number().text());
    check(ok(restored.manage(summarize)));
    check(!ok(restored.manage(summarize))); // stale
    check(kinds() == "ERM:summarize");
    const auto summarized = restored.view();
    check(!ok(restored.manage(proposal(
        "archive", {field(field(summarized, "entries").array().back(), "id")}))));
    check(field(field(summarized, "entries").array().back(), "ancestry") ==
          Value{Value::Array{call, output}});
    check(ok(restored.manage(proposal("restore", {call, output}))));
    check(restored.items()[2] == field(all[2], "item") &&
          restored.items()[3] == field(all[3], "item"));
    auto reordered = restored.view();
    std::swap(reordered.object()[1].second.array()[2],
              reordered.object()[1].second.array()[3]);
    check(ok(restored.edit(reordered))); // generic CLM is still available
    check(ok(restored.manage(proposal("restore", {call, output}))));
    check(restored.items()[2] == field(all[2], "item"));
    check(kinds() == "ERM:summarizeM:restoreEM:restore");
    const auto before_stage = restored.view();
    restored.begin_workflow();
    check(field(restored.manage(proposal("archive", {call, output})), "staged") ==
          Value{true});
    check(restored.view() == before_stage);
    check(!ok(restored.manage(proposal("archive", {call, output})))); // overlap
    check(kinds() == "ERM:summarizeM:restoreEM:restore"); // Staged reports nothing.
    restored.append({Value::object({{"type", Value{"function_call"}},
                                    {"call_id", Value{"active"}}})},
                    "active");
    restored.append({Value::object({{"type", Value{"function_call_output"}},
                                    {"call_id", Value{"active"}},
                                    {"output", Value{"settled"}}})},
                    "active-result");
    bytes_now = std::stoul(field(restored.stats(), "input_bytes").number().text());
    check(ok(restored.finish_workflow(true)));
    check(kinds() == "ERM:summarizeM:restoreEM:restoreM:archive"); // At activation.
    check(restored.items().size() == 5 &&
          string_field(restored.items()[3], "call_id") == "active");
    restored.begin_workflow();
    check(field(restored.manage(proposal("restore", {call, output})), "staged") ==
          Value{true});
    const auto cancelled_head = restored.head();
    check(!ok(restored.finish_workflow(false)) && restored.head() == cancelled_head);
    restored.begin_workflow();
    (void)restored.manage(proposal("restore", {call, output}));
    (void)restored.edit(restored.view());
    check(!ok(restored.finish_workflow(true)));
    check(ok(restored.manage(proposal("restore", {call, output}))));
    const auto inspection =
        restored.inspect(Value::object({{"kind", Value{"originals"}},
                                        {"format", Value{"json"}},
                                        {"entry", call},
                                        {"offset", Value{Number{"1"}}},
                                        {"limit", Value{Number{"7"}}}}));
    check(string_field(inspection, "bytes").size() == 14 &&
          field(inspection, "next").number().text() == "8");
    // A page cursor is only valid for the snapshot returned with its first page.
    const auto history_page = restored.inspect(
        Value::object({{"kind", Value{"history"}}, {"format", Value{"json"}}}));
    const auto history_before_reject = field(history_page, "revision");
    const auto view_before_reject = restored.view();
    auto stale_read_proposal = proposal("archive", {call, output});
    for (auto &[key, value] : stale_read_proposal.object())
      if (key == "base")
        value = Value{"obsolete"};
    check(!ok(restored.manage(stale_read_proposal)));
    check(restored.view() == view_before_reject);
    {
      // A response round is atomic: a reasoning item binds every later item of its
      // append, and with them their calls' whole groups. An OpenAI encrypted
      // reasoning item without its call is invalid input.
      restored.append(
          {Value::object(
               {{"type", Value{"reasoning"}}, {"encrypted_content", Value{"R1=="}}}),
           Value::object({{"type", Value{"function_call"}}, {"call_id", Value{"r1"}}})},
          "round-output");
      restored.append({Value::object({{"type", Value{"function_call_output"}},
                                      {"call_id", Value{"r1"}},
                                      {"output", Value{"done"}}})},
                      "round-result");
      restored.append(
          {Value::object(
               {{"type", Value{"reasoning"}}, {"encrypted_content", Value{"R2=="}}}),
           Value::object({{"role", Value{"assistant"}}, {"content", Value{"answer"}}})},
          "round-answer");
      const auto round = field(restored.view(), "entries").array();
      const auto n = round.size();
      const auto reasoning = field(round[n - 5], "id"),
                 round_call = field(round[n - 4], "id"),
                 round_output = field(round[n - 3], "id"),
                 answer_reasoning = field(round[n - 2], "id"),
                 answer = field(round[n - 1], "id");
      bytes_now = std::stoul(field(restored.stats(), "input_bytes").number().text());
      auto summary = [&](Value::Array ids) {
        auto p = proposal("summarize", std::move(ids));
        p.object().emplace_back(
            "summary",
            Value::object({{"role", Value{"assistant"}}, {"content", Value{"short"}}}));
        return p;
      };
      check(string_field(restored.manage(summary({round_call, round_output})),
                         "reason") == "partial-round");
      check(string_field(restored.manage(summary({answer})), "reason") ==
            "partial-round");
      check(string_field(restored.manage(summary({reasoning})), "reason") ==
            "partial-round");
      check(ok(restored.manage(summary({reasoning, round_call, round_output}))));
      bytes_now = std::stoul(field(restored.stats(), "input_bytes").number().text());
      check(ok(restored.manage(summary({answer_reasoning, answer}))));
      // Reasoning binds every later item of its append, not only the next one.
      restored.append(
          {Value::object(
               {{"type", Value{"reasoning"}}, {"encrypted_content", Value{"R3=="}}}),
           Value::object({{"role", Value{"assistant"}}, {"content", Value{"working"}}}),
           Value::object({{"type", Value{"function_call"}}, {"call_id", Value{"r3"}}})},
          "round-three");
      restored.append({Value::object({{"type", Value{"function_call_output"}},
                                      {"call_id", Value{"r3"}},
                                      {"output", Value{"ok"}}})},
                      "round-three-result");
      const auto three = field(restored.view(), "entries").array();
      const auto m = three.size();
      const auto r3 = field(three[m - 4], "id"), msg3 = field(three[m - 3], "id"),
                 call3 = field(three[m - 2], "id"), out3 = field(three[m - 1], "id");
      check(string_field(restored.manage(summary({r3, msg3})), "reason") ==
            "partial-round");
      check(string_field(restored.manage(summary({call3, out3})), "reason") ==
            "partial-round");
      bytes_now = std::stoul(field(restored.stats(), "input_bytes").number().text());
      check(ok(restored.manage(summary({r3, msg3, call3, out3}))));
      // A select staged in a workflow while its own call is still open (the model's
      // context_manage) keeps that call's whole round, reasoning included, at publish.
      restored.begin_workflow();
      restored.append(
          {Value::object(
               {{"type", Value{"reasoning"}}, {"encrypted_content", Value{"R4=="}}}),
           Value::object({{"type", Value{"function_call"}}, {"call_id", Value{"r4"}}})},
          "round-open");
      Value::Array keep;
      const auto before_select = field(restored.view(), "entries").array();
      for (std::size_t k = 0; k + 2 < before_select.size(); ++k)
        keep.push_back(field(before_select[k], "id"));
      check(field(restored.manage(proposal("select", keep)), "staged") == Value{true});
      restored.append({Value::object({{"type", Value{"function_call_output"}},
                                      {"call_id", Value{"r4"}},
                                      {"output", Value{"closed"}}})},
                      "round-open-result");
      bytes_now = std::stoul(field(restored.stats(), "input_bytes").number().text());
      check(ok(restored.finish_workflow(true)));
      const auto after_select = field(restored.view(), "entries").array();
      check(after_select.size() == before_select.size() + 1 &&
            string_field(field(after_select[after_select.size() - 3], "item"),
                         "type") == "reasoning");
    }
    const auto rejection_cursor = root->cursor().sequence;
    bool rejected_history_stale = false;
    try {
      (void)restored.inspect(Value::object({{"kind", Value{"history"}},
                                            {"format", Value{"json"}},
                                            {"revision", history_before_reject}}));
    } catch (const Error &e) {
      rejected_history_stale = e.code == ErrorCode::conflict;
    }
    check(rejected_history_stale && root->cursor().sequence == rejection_cursor);
    const auto inspection_head = restored.head();
    for (const auto *kind : {"originals", "history", "index"}) {
      check(restored.inspect(Value::object(
                {{"kind", Value{kind}},
                 {"revision",
                  field(restored.inspect(Value::object({{"kind", Value{kind}}})),
                        "revision")}})) ==
            restored.inspect(Value::object({{"kind", Value{kind}}})));
    }
    const auto inspect_cursor = root->cursor().sequence;
    bool bad_inspection_revision = false;
    try {
      (void)restored.inspect(Value::object({{"kind", Value{"originals"}},
                                            {"format", Value{"json"}},
                                            {"revision", Value{true}}}));
    } catch (const Error &e) {
      bad_inspection_revision = e.code == ErrorCode::corrupt;
    }
    check(bad_inspection_revision && root->cursor().sequence == inspect_cursor);
    restored.append({Value::object({{"role", Value{"assistant"}},
                                    {"content", Value{"concurrent append"}}})},
                    "inspection-concurrent-append");
    const auto changed_cursor = root->cursor().sequence;
    const auto changed_view = restored.view();
    for (const auto *kind : {"originals", "history", "index"}) {
      bool stale_inspection = false;
      try {
        (void)restored.inspect(Value::object(
            {{"kind", Value{kind}}, {"revision", Value{inspection_head}}}));
      } catch (const Error &e) {
        stale_inspection = e.code == ErrorCode::conflict;
      }
      check(stale_inspection && root->cursor().sequence == changed_cursor &&
            restored.view() == changed_view);
    }
    restored.append(
        {Value::object({{"role", Value{"user"}}, {"content", Value{"follow-up"}}})},
        "mission-followup");
    check(!ok(restored.manage(proposal("archive", {mission}))));
    restored.begin_workflow();
    restored.append({Value::object({{"type", Value{"function_call"}},
                                    {"call_id", Value{"select-active"}}})},
                    "select-current");
    Value::Array keep;
    const auto select_view = restored.view();
    for (const auto &kept : field(select_view, "entries").array()) {
      const auto *role = field(kept, "item").find("role");
      if (role && (*role == Value{"user"} || *role == Value{"developer"}))
        keep.push_back(field(kept, "id"));
    }
    check(field(restored.manage(proposal("select", keep)), "staged") == Value{true});
    restored.append({Value::object({{"type", Value{"function_call_output"}},
                                    {"call_id", Value{"select-active"}},
                                    {"output", Value{"done"}}})},
                    "select-result");
    check(ok(restored.finish_workflow(true)));
    check(string_field(restored.items().back(), "call_id") == "select-active");
    restored.begin_workflow();
    (void)restored.manage(proposal("archive", {}));
    restored.append({Value::object({{"type", Value{"function_call_output"}},
                                    {"call_id", Value{"DANGLING-SUFFIX"}},
                                    {"output", Value{"bad"}}})},
                    "malformed suffix");
    const auto malformed_suffix_view = restored.view();
    const auto suffix_outcome = restored.finish_workflow(true);
    check(!ok(suffix_outcome) &&
          string_field(suffix_outcome, "reason") == "invalid-tool-protocol");
    check(restored.view() == malformed_suffix_view);
    auto suffix_repair = restored.view();
    suffix_repair.object()[1].second.array().pop_back();
    check(ok(restored.edit(suffix_repair)));
    // Root-only projection must not remove application data named candidate.
    restored.append({Value::object({{"role", Value{"user"}},
                                    {"content", Value{"projection live"}},
                                    {"candidate", Value{"nested survives"}}})},
                    "projection");
    const auto rejected =
        restored.edit(Value::object({{"base", Value{"stale"}},
                                     {"entries", Value{Value::Array{}}},
                                     {"candidate", Value{std::string(65536, 'x')}}}));
    check(!ok(rejected));
    const auto final_head = restored.head();

    ContextStore replay_managed{replay};
    check(replay_managed.head() == final_head &&
          replay_managed.view() == restored.view());
    check(replay_managed.inspect(
              Value::object({{"kind", Value{"index"}}, {"format", Value{"json"}}})) ==
          restored.inspect(
              Value::object({{"kind", Value{"index"}}, {"format", Value{"json"}}})));
    // Native close/reopen after accepted transforms and an orphan staged proposal.
    const auto expected_view = restored.view();
    const auto expected_index = restored.inspect(
        Value::object({{"kind", Value{"index"}}, {"format", Value{"json"}}}));
    restored.begin_workflow();
    (void)restored.manage(proposal("restore", {call, output}));
    root.reset();
    root = take(RetainedState::open(std::make_unique<NativeJournalDirectory>(
                                        take(NativeJournalDirectory::open(path))),
                                    "audit", h, cap));
    check(root->confirm_recovery().has_value());
    check(root->reconcile(custody).has_value());
    AuditLog native_replay{*root};
    ContextStore native_restored{native_replay};
    check(native_restored.view() == expected_view);
    check(native_restored.inspect(
              Value::object({{"kind", Value{"index"}}, {"format", Value{"json"}}})) ==
          expected_index);
    check(native_restored.finish_workflow(true) == Value{}); // orphan never executes
    check(native_restored.items().back() == restored.items().back());
    check(string_field(native_restored.items().back(), "candidate") ==
          "nested survives");
    check(native_restored.originals() == restored.originals());
    // Compare every page, including the newly added large rejected candidate.
    std::size_t history_offset = 0;
    std::string history_revision;
    for (;;) {
      auto query =
          Value::object({{"kind", Value{"history"}},
                         {"format", Value{"json"}},
                         {"offset", Value{Number{std::to_string(history_offset)}}},
                         {"limit", Value{Number{"65536"}}}});
      if (!history_revision.empty())
        query.object().emplace_back("revision", Value{history_revision});
      const auto page = restored.inspect(query);
      check(native_restored.inspect(query) == page);
      history_revision = string_field(page, "revision");
      const auto next =
          static_cast<std::size_t>(std::stoull(field(page, "next").number().text()));
      const auto total = static_cast<std::size_t>(
          std::stoull(field(page, "total_bytes").number().text()));
      check(next > history_offset || next == total);
      if (next == total)
        break;
      history_offset = next;
    }
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << '\n';
    std::filesystem::remove_all(path);
    return 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
  std::filesystem::remove_all(path);
}
