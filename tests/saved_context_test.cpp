// Saved context and recovery (unit S1, CONTEXT_LIFECYCLE.md §4.4-§4.5).
// Oracle: the context a reopen establishes equals the live context it saved
// (specification: "live state equals reopened state"), never an empty head:
// - every core field and the registered test section is corrupted in turn, and
//   each refusal (by the loader or by the store) enters whole-history playback,
//   reported by ContextStore::open_report;
// - a version-1 snapshot the old loader accepted but the store refused (the old
//   silent empty context, state tribunal H1);
// - a 32 MiB context snapshots and restores (arconaut-yc4), with refs and with
//   every entry inline;
// - a frame that does not apply opens the session read-only: no context write,
//   no snapshot; an approved repair frame lets playback continue.
// Build with -DS1_RED_OLD_CODE against the pre-S1 tree for the cases that use
// only the old interface (the red run).
#include "blackbird/context.hpp"
#include "blackbird/saved_state.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
#if !defined(S1_RED_OLD_CODE)
#include "blackbird/context_cut.hpp"
#include "blackbird/saved_context.hpp"
#endif

using namespace blackbird;
namespace {
namespace fs = std::filesystem;
int failures = 0;
void check(bool condition, const std::string &claim) {
  if (!condition)
    throw std::runtime_error(claim);
}
template <class T> T identity(unsigned n) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(n);
  return unwrap(T::from_bytes(bytes));
}
Value item(std::string text, std::string_view role = "assistant") {
  return Value::object(
      {{"role", Value{std::string{role}}}, {"content", Value{std::move(text)}}});
}
const JournalHeader header{identity<EnvironmentId>(1),
                           identity<AuditStreamId>(2),
                           3,
                           {32 * 1024 * 1024, 96 * 1024 * 1024},
                           std::nullopt};
constexpr JournalCapacity capacity{512ULL * 1024 * 1024, 200000};
struct Settled final : CustodyVerifier {
  Result<void> verify(std::span<const AttemptState> attempts) override {
    return attempts.empty() ? Result<void>::success()
                            : Result<void>::failure({ErrorCode::conflict});
  }
};
struct Session {
  fs::path path;
  std::unique_ptr<RetainedState> root;
  std::unique_ptr<AuditLog> log;
  std::unique_ptr<ContextStore> context;
  explicit Session(const fs::path &directory, bool create = true) : path(directory) {
    if (!create) {
      open_root();
      return;
    }
    fs::create_directories(path);
    fs::permissions(path, fs::perms::owner_all);
    root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                         NativeJournalDirectory::open(path.string()))),
                                     "audit", header, capacity));
    log = std::make_unique<AuditLog>(*root);
    // Saved identity gives a snapshot its semantic coverage, so reopen is compact.
    log->record(ApplicationChannel::program,
                Value::object({{"label", Value{"session.identity"}}}));
    context = std::make_unique<ContextStore>(*log);
  }
  void close() {
    context.reset();
    log.reset();
    root.reset();
  }
  void open_root() {
    close();
    root = unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                                          NativeJournalDirectory::open(path.string()))),
                                      "audit", header, capacity));
    unwrap(root->confirm_recovery());
    Settled custody;
    unwrap(root->reconcile(custody));
    log = std::make_unique<AuditLog>(*root);
  }
  void reopen() {
    open_root();
    context = std::make_unique<ContextStore>(*log);
  }
};
fs::path scratch() {
  std::string pattern =
      (fs::temp_directory_path() / "blackbird-saved-context-XXXXXX").string();
  check(::mkdtemp(pattern.data()) != nullptr, "mkdtemp");
  return fs::canonical(pattern);
}
Value with(Value object, std::string_view key, std::optional<Value> value) {
  std::erase_if(object.object(), [&](const auto &pair) { return pair.first == key; });
  if (value)
    object.object().emplace_back(std::string{key}, std::move(*value));
  return object;
}
// Writes the context into both slots, so no good slot is left to fall back to.
void save_both(Session &session, const Value &context) {
  unwrap(session.root->save_current_state(context));
  unwrap(session.root->save_current_state(context));
}
// A small session whose last write is a snapshot: entries from three appends, an
// edited entry and a managed summary.
struct Fixture {
  fs::path template_path;
  Value view;
  std::string head;
};
Fixture build(const fs::path &path) {
  Session session{path};
  auto &context = *session.context;
  context.append({item("question", "user"), item("first answer")}, "fixture");
  // Separate records: two assistant answers of one record are one round (unit S2),
  // and the summary below takes one whole unit.
  context.append({item("second answer")}, "fixture");
  context.append({item("third answer")}, "fixture");
  auto edited = context.view();
  auto &entries = edited.object()[1].second.array();
  entries[1] =
      Value::object({{"id", field(entries[1], "id")}, {"item", item("edited")}});
  check(field(context.edit(edited), "accepted") == Value{true}, "fixture edit");
  const auto summarized =
      string_field(field(context.view(), "entries").array()[3], "id");
  check(field(context.manage(
                  Value::object({{"base", Value{context.head()}},
                                 {"mode", Value{"summarize"}},
                                 {"reason", Value{"fixture"}},
                                 {"source", Value{"test"}},
                                 {"ids", Value{Value::Array{Value{summarized}}}},
                                 {"summary", item("summary", "developer")}})),
              "accepted") == Value{true},
        "fixture summary");
  unwrap(context.checkpoint());
  return {path, context.view(), context.head()};
}
fs::path copy_of(const Fixture &fixture, const fs::path &base, std::string_view name) {
  const auto path = base / std::string{name};
  fs::copy(fixture.template_path, path, fs::copy_options::recursive);
  return path;
}
// The reopened context equals the saved one and is not empty.
void require_restored(Session &session, const Fixture &fixture,
                      std::string_view label) {
  check(!session.context->head().empty(),
        std::string{label} + ": reopened with an empty head");
  check(session.context->head() == fixture.head &&
            session.context->view() == fixture.view,
        std::string{label} + ": reopened context differs from the saved one");
}

// Version-1 snapshots the old loader accepted and the old ContextStore refused,
// leaving an empty context (src/context.cpp:367-376 then coding.cpp:2654-2655).
void v1_store_faults(const fs::path &base) {
  const auto fixture = build(base / "v1-template");
  // The version-1 shape the old checkpoint wrote: entries and each live entry's
  // exact original.
  const auto v1 = [&](Session &session) {
    Value::Array originals;
    for (const auto &entry : field(fixture.view, "entries").array())
      for (const auto &original : session.context->originals().array())
        if (field(original, "id") == field(entry, "id"))
          originals.push_back(original);
    return Value::object({{"version", Value{Number{"1"}}},
                          {"base", Value{fixture.head}},
                          {"entries", field(fixture.view, "entries")},
                          {"live_originals", Value{std::move(originals)}}});
  };
  struct Case {
    const char *name;
    std::function<Value(Value)> corrupt;
  };
  const std::vector<Case> cases{
      {"entry not an object",
       [](Value context) {
         auto entries = field(context, "entries").array();
         entries.push_back(Value{"not an entry"});
         return with(std::move(context), "entries", Value{std::move(entries)});
       }},
      {"duplicate live original",
       [](Value context) {
         auto originals = field(context, "live_originals").array();
         originals.push_back(originals.front());
         return with(std::move(context), "live_originals", Value{std::move(originals)});
       }},
      {"entry with no original",
       [](Value context) {
         auto entries = field(context, "entries").array();
         entries.push_back(Value::object(
             {{"id", Value{std::string(32, 'f') + ".0"}}, {"item", item("ghost")}}));
         return with(std::move(context), "entries", Value{std::move(entries)});
       }},
  };
  for (const auto &one : cases) {
    Session session{copy_of(fixture, base, std::string{"v1-"} + one.name), false};
    session.context = std::make_unique<ContextStore>(*session.log);
    save_both(session, one.corrupt(v1(session)));
    session.reopen();
    require_restored(session, fixture, std::string{"v1 "} + one.name);
#if !defined(S1_RED_OLD_CODE)
    // The loader now makes the store's v1 checks (A-M6): both slots refused there.
    const auto &report = session.context->open_report();
    check(report.playback == ContextOpenReport::Playback::whole_history &&
              report.slot_refusals.size() == 2 && report.recovery(),
          std::string{"v1 "} + one.name + ": no whole-history recovery reported");
#endif
  }
  // A well-formed version-1 snapshot still restores (sessions written before S1).
  Session session{copy_of(fixture, base, "v1-good"), false};
  session.context = std::make_unique<ContextStore>(*session.log);
  save_both(session, v1(session));
  session.reopen();
  require_restored(session, fixture, "v1 good");
  check(session.root->compact_recovery(), "a good v1 snapshot restores compactly");
}

// arconaut-yc4: a 32 MiB context snapshots and restores; then with every entry
// differing from its original (all written inline, streamed one packet each).
void large_snapshot(const fs::path &base) {
  Session session{base / "large"};
  constexpr std::size_t entries = 256, size = 128 * 1024;
  for (std::size_t i = 0; i < entries; ++i)
    session.context->append(
        {item(std::string(size, static_cast<char>('a' + i % 26)) + std::to_string(i))},
        "large");
  const auto timed = [](const char *what, auto &&body) {
    const auto start = std::chrono::steady_clock::now();
    body();
    std::cout << "  " << what << " "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count()
              << " ms\n";
  };
  const auto state_bytes = [&] {
    return fs::file_size(
        session.path /
        ("audit.state." + std::to_string(session.root->saved_state()->slot)));
  };
  timed("32 MiB checkpoint (refs)", [&] {
    const auto saved = session.context->checkpoint();
    check(saved.has_value(),
          std::string{"32 MiB checkpoint failed: "} +
              (saved.has_value() ? "" : error_name(saved.error().code)));
  });
  std::cout << "  state file " << state_bytes() << " bytes\n";
  const auto view = session.context->view();
  timed("32 MiB reopen (refs)", [&] { session.reopen(); });
  check(session.context->view() == view, "32 MiB context restores from refs");
  check(session.root->compact_recovery(), "32 MiB restore is compact");
#if !defined(S1_RED_OLD_CODE)
  // Every entry replaced by cut frames (an edit record would carry the whole
  // 32 MiB candidate, over one packet's limit), 64 entries per frame.
  const auto live = field(session.context->view(), "entries").array();
  for (std::size_t first = 0; first < live.size(); first += 64) {
    CutFrame frame;
    frame.kind = CutKind::archival;
    frame.changes = cut_edit;
    for (std::size_t i = first; i < std::min(first + 64, live.size()); ++i) {
      const auto ref = parse_entry_id(string_field(live[i], "id"));
      if (!ref)
        throw std::runtime_error("large fixture entry IDs are refs");
      frame.replace.push_back(
          {*ref, item(string_field(field(live[i], "item"), "content") + " edited")});
    }
    (void)session.context->cut(frame);
  }
  timed("32 MiB checkpoint (inline)", [&] {
    const auto saved = session.context->checkpoint();
    check(saved.has_value(), "32 MiB inline checkpoint failed");
  });
  std::cout << "  state file " << state_bytes() << " bytes\n";
  const auto inline_view = session.context->view();
  timed("32 MiB reopen (inline)", [&] { session.reopen(); });
  check(session.context->view() == inline_view, "32 MiB inline context restores");
#endif
}

#if !defined(S1_RED_OLD_CODE)
// The saved context without the keys RetainedState adds at every save.
Value core(Value context) {
  std::erase_if(context.object(), [](const auto &pair) {
    return pair.first == "physical_records" || pair.first == "archive_entries" ||
           pair.first == "semantic_coverage";
  });
  return context;
}
const SavedSectionRegistration test_section{
    "test.section", [](const Value &value) {
      const auto *n = value.find("n");
      return std::holds_alternative<Value::Object>(value.value()) && n &&
                     std::holds_alternative<Number>(n->value())
                 ? Result<void>::success()
                 : Result<void>::failure({ErrorCode::corrupt});
    }};

// Every core field and the test section corrupted in turn: the loader refuses,
// and the context is played back from the whole history.
void v2_loader_faults(const fs::path &base) {
  const auto fixture = [&] {
    Session session{base / "v2-template"};
    session.context->section_writers.push_back(
        {"test.section", [] { return Value::object({{"n", Value{Number{7}}}}); }});
    session.context->append({item("question", "user"), item("answer")}, "fixture");
    auto edited = session.context->view();
    auto &entries = edited.object()[1].second.array();
    entries[1] =
        Value::object({{"id", field(entries[1], "id")}, {"item", item("edited")}});
    check(field(session.context->edit(edited), "accepted") == Value{true}, "edit");
    unwrap(session.context->checkpoint());
    const auto saved = core(session.root->saved_state()->context);
    // The store's own classes section (unit S2), then the test section.
    const auto &sections = field(saved, "sections").array();
    check(field(saved, "version") == Value{Number{"2"}} &&
              !saved.find("live_originals") && sections.size() == 2 &&
              string_field(sections[0], "name") == "classes" &&
              string_field(sections[1], "name") == "test.section",
          "the fixture snapshot is version 2 with the classes and test sections");
    return Fixture{session.path, session.context->view(), session.context->head()};
  }();
  Value good;
  {
    Session session{fixture.template_path, false};
    good = core(session.root->saved_state()->context);
  }
  const auto entries = field(good, "entries").array();
  const auto with_entries = [&](Value::Array list) {
    return with(good, "entries", Value{std::move(list)});
  };
  auto bad_ref = entries;
  bad_ref[0] = Value{Value::Array{Value{std::string(32, 'a')}}};
  auto big_ordinal = entries;
  big_ordinal[0] = Value{Value::Array{Value{std::string(32, 'a')},
                                      Value{Number{std::uint64_t{1} << 33U}}}};
  auto duplicate = entries;
  duplicate.push_back(entries[0]);
  auto number_entry = entries;
  number_entry[0] = Value{Number{1}};
  auto inline_missing = entries;
  inline_missing[1] = with(inline_missing[1], "item", std::nullopt);
  auto inline_extra = entries;
  inline_extra[1] = with(inline_extra[1], "extra", Value{true});
  check(validate_saved_context(with(good, "entries", Value{inline_extra})).has_value(),
        "an inline entry's legacy key is carried, not refused");
  auto inline_bad_id = entries;
  inline_bad_id[1] = with(inline_bad_id[1], "id", Value{"not-an-id"});
  const auto section = [&](Value value) {
    return with(good, "sections", Value{Value::Array{std::move(value)}});
  };
  const std::vector<std::pair<const char *, Value>> cases{
      {"version missing", with(good, "version", std::nullopt)},
      {"version 3", with(good, "version", Value{Number{3}})},
      {"version as text", with(good, "version", Value{"2"})},
      {"base missing", with(good, "base", std::nullopt)},
      {"base short", with(good, "base", Value{"abc"})},
      {"base uppercase", with(good, "base", Value{std::string(32, 'A')})},
      {"entries missing", with(good, "entries", std::nullopt)},
      {"entries not an array", with(good, "entries", Value{"x"})},
      {"ref not a pair", with_entries(bad_ref)},
      {"ref ordinal over u32", with_entries(big_ordinal)},
      {"duplicate entry", with_entries(duplicate)},
      {"entry a number", with_entries(number_entry)},
      {"inline entry without item", with_entries(inline_missing)},
      {"inline entry with a bad ID", with_entries(inline_bad_id)},
      {"sections missing", with(good, "sections", std::nullopt)},
      {"sections not an array", with(good, "sections", Value{Number{1}})},
      {"section without a name", section(Value::object({{"value", Value{}}}))},
      {"section with an extra key",
       section(Value::object({{"name", Value{"test.section"}},
                              {"value", Value::object({{"n", Value{Number{1}}}})},
                              {"x", Value{}}}))},
      {"unregistered section",
       section(Value::object({{"name", Value{"nobody.owns"}}, {"value", Value{}}}))},
      {"test section value refused by its validator",
       section(Value::object({{"name", Value{"test.section"}},
                              {"value", Value::object({{"n", Value{"seven"}}})}}))},
      {"duplicate section",
       with(good, "sections",
            Value{Value::Array{field(good, "sections").array()[0],
                               field(good, "sections").array()[0]}})},
      {"unknown key", with(good, "chapter", Value::object({}))},
      {"live_originals in version 2",
       with(good, "live_originals", Value{Value::Array{}})},
  };
  for (const auto &[name, corrupted] : cases) {
    const auto valid = validate_saved_context(corrupted);
    check(!valid.has_value() && valid.error().code == ErrorCode::corrupt,
          std::string{"validator accepted: "} + name);
    Session session{copy_of(fixture, base, std::string{"v2-"} + name), false};
    save_both(session, corrupted);
    session.reopen();
    require_restored(session, fixture, name);
    // Both slots refused by the loader, each with the validator's own error.
    const auto &report = session.context->open_report();
    check(report.playback == ContextOpenReport::Playback::whole_history &&
              !report.snapshot_refusal && report.slot_refusals.size() == 2 &&
              report.slot_refusals[0].second == valid.error() &&
              report.slot_refusals[1].second == valid.error(),
          std::string{name} + ": the loader's refusal is not the one reported");
  }
  // The good snapshot round-trips, the section with it.
  Session session{copy_of(fixture, base, "v2-good"), false};
  session.context = std::make_unique<ContextStore>(*session.log);
  check(session.root->compact_recovery() && session.context->open_report().playback ==
                                                ContextOpenReport::Playback::suffix,
        "the good snapshot restores compactly");
  require_restored(session, fixture, "v2 good");
  const auto *restored = session.context->restored_section("test.section");
  check(restored && *restored == Value::object({{"n", Value{Number{7}}}}),
        "the test section's value is restored for its owner");
}

// Shapes the validator accepts that do not resolve against the journal: the store
// refuses and plays back the whole history through the archive catalogue.
void v2_store_faults(const fs::path &base) {
  const auto fixture = build(base / "v2-store-template");
  Value good;
  {
    Session session{fixture.template_path, false};
    good = core(session.root->saved_state()->context);
  }
  const auto entries = field(good, "entries").array();
  // The fixture's edited entry is inline; the rest, its managed summary included,
  // are refs to their creating records.
  check(std::count_if(entries.begin(), entries.end(),
                      [](const Value &entry) {
                        return std::holds_alternative<Value::Object>(entry.value());
                      }) == 1,
        "only the edited entry is written inline");
  auto unknown_record = entries;
  unknown_record.push_back(
      Value{Value::Array{Value{std::string(32, 'e')}, Value{Number{0}}}});
  auto ordinal_beyond = entries;
  {
    auto pair = ordinal_beyond[0].array();
    pair[1] = Value{Number{40}};
    ordinal_beyond[0] = Value{std::move(pair)};
  }
  auto inline_ghost = entries;
  inline_ghost.push_back(Value::object(
      {{"id", Value{std::string(32, 'd') + ".3"}}, {"item", item("ghost")}}));
  const std::vector<std::pair<const char *, Value>> cases{
      {"ref to an unknown record", with(good, "entries", Value{unknown_record})},
      {"ref ordinal beyond its record", with(good, "entries", Value{ordinal_beyond})},
      {"inline entry with no original", with(good, "entries", Value{inline_ghost})},
  };
  for (const auto &[name, corrupted] : cases) {
    check(validate_saved_context(corrupted).has_value(),
          std::string{"validator refused a well-formed shape: "} + name);
    Session session{copy_of(fixture, base, std::string{"store-"} + name), false};
    save_both(session, corrupted);
    session.reopen();
    check(session.root->compact_recovery(), std::string{name} + ": loader accepted");
    require_restored(session, fixture, name);
    const auto &report = session.context->open_report();
    check(report.playback == ContextOpenReport::Playback::whole_history &&
              report.snapshot_refusal && report.refused_by_store,
          std::string{name} + ": no store refusal reported");
  }
  // Over the automatic bound, whole-history playback is refused, not guessed.
  Session session{copy_of(fixture, base, "store-bound"), false};
  save_both(session, with(good, "entries", Value{unknown_record}));
  session.open_root();
  bool refused = false;
  try {
    ContextStore bounded{*session.log, ContextOpen{.whole_history_limit = 1024}};
  } catch (const Error &error) {
    refused = error.code == ErrorCode::unsupported;
  }
  check(refused, "whole-history playback beyond the bound is refused");
}

// A frame that does not apply: the read-only state and the repair frame.
void read_only(const fs::path &base) {
  Session session{base / "read-only"};
  session.context->append({item("question", "user"), item("answer")}, "fixture");
  unwrap(session.context->checkpoint());
  session.context->append({item("tail answer")}, "fixture");
  const auto good_view = session.context->view();
  const auto good_head = session.context->head();
  // A cut frame whose recorded result does not match its entries.
  const auto bad_identity = session.log->issue();
  CutFrame bad;
  bad.revision = hex_identity(bad_identity.bytes());
  bad.base = bad.observed = good_head;
  bad.originals = {{item("never applies"), 0}};
  bad.insert = {{std::nullopt, std::uint32_t{0}}};
  bad.result = CutResult{99, 0, 0};
  (void)session.log->record(bad_identity, ApplicationChannel::context,
                            encode_cut_frame(bad));
  // A later frame chained on the bad one.
  const auto later_identity = session.log->issue();
  const auto later = hex_identity(later_identity.bytes());
  (void)session.log->record(
      later_identity, ApplicationChannel::context,
      Value::object({{"op", Value{"append"}},
                     {"base", Value{bad.revision}},
                     {"observed", Value{bad.revision}},
                     {"revision", Value{later}},
                     {"accepted", Value{true}},
                     {"origin", Value{"later"}},
                     {"format", Value{"append-delta-v1"}},
                     {"originals", Value{Value::Array{Value::object(
                                       {{"id", Value{later + ".0"}},
                                        {"item", item("kept by the repair")}})}}}}));
  const auto boundary = session.root->saved_state()->boundary;
  session.reopen();
  auto &context = *session.context;
  const auto &report = context.open_report();
  check(context.read_only() && report.read_only &&
            report.bad_revision == bad.revision &&
            report.bad_error.code == ErrorCode::corrupt && report.after_bad == 1,
        "the bad frame opens the session read-only and is named");
  check(context.view() == good_view && context.head() == good_head,
        "playback stops at the last good frame");
  check(!context.checkpoint().has_value() &&
            session.root->saved_state()->boundary.sequence == boundary.sequence,
        "no snapshot in the read-only state");
  bool refused = false;
  try {
    context.append({item("x")}, "refused");
  } catch (const Error &error) {
    refused = error.code == ErrorCode::audit_unavailable;
  }
  check(refused && context.view() == good_view, "no context write in read-only state");
  const auto originals = context.skipped_originals();
  check(originals.size() == 2, "the skipped frames' originals are offered");
  {
    ContextStore whole{*session.log, ContextOpen{.whole_history = true, .probe = true}};
    check(whole.read_only() && whole.view() == good_view,
          "whole-history playback stops at the same frame");
  }
  const auto outcome = context.repair(Value::Array{Value{later + ".0"}});
  check(field(outcome, "accepted") == Value{true} && !context.read_only(),
        "the approved repair frame is accepted");
  auto expected = good_view;
  expected.object()[1].second.array().push_back(
      Value::object({{"id", Value{string_field(outcome, "revision") + ".0"}},
                     {"item", item("kept by the repair")}}));
  check(field(context.view(), "entries") == field(expected, "entries"),
        "the repair keeps the picked original after the last good entry");
  context.append({item("after repair")}, "fixture");
  const auto repaired = context.view();
  {
    ContextStore whole{*session.log, ContextOpen{.whole_history = true, .probe = true}};
    check(!whole.read_only() && whole.view() == repaired &&
              whole.open_report().skipped == 2,
          "whole-history playback skips the named frames and continues");
  }
  session.reopen();
  check(!session.context->read_only() && session.context->view() == repaired,
        "the reopened session is writable and equal");
  unwrap(session.context->checkpoint());
  session.reopen();
  check(session.context->view() == repaired &&
            session.context->open_report().playback ==
                ContextOpenReport::Playback::suffix,
        "snapshots resume after the repair");
  // An undecodable frame: read-only too, and it offers nothing to copy.
  const auto garbage_identity = session.log->issue();
  (void)session.log->record(
      garbage_identity, ApplicationChannel::context,
      Value::object({{"op", Value{"cut"}},
                     {"revision", Value{hex_identity(garbage_identity.bytes())}},
                     {"observed", Value{session.context->head()}},
                     {"accepted", Value{true}}}));
  session.reopen();
  check(session.context->read_only() && session.context->skipped_originals().empty() &&
            session.context->view() == repaired,
        "a malformed cut frame opens read-only");
}

// A-H1: one entry rule. A new strict edit refuses an unknown key; an entry that
// already carries one (an earlier edit, a v1 snapshot) is saved inline and restored.
void entry_keys(const fs::path &base) {
  Session session{base / "entry-keys"};
  auto &context = *session.context;
  context.append({item("first"), item("second")}, "fixture");
  const auto view = context.view();
  const auto with_entry = [&](std::size_t at, Value entry) {
    auto candidate = view;
    candidate.object()[1].second.array()[at] = std::move(entry);
    return candidate;
  };
  const auto &live = field(view, "entries").array();
  const auto noted = with(live[0], "note", Value{"x"});
  auto refused = context.edit(with_entry(0, noted), {}, ContextStore::EditKeys::strict);
  check(field(refused, "accepted") == Value{false} &&
            field(refused, "reason") == Value{"unknown-entry-key"} &&
            string_field(refused, "detail").find("key note") != std::string::npos,
        "a strict edit with an unknown key is refused, naming it");
  refused = context.edit(with_entry(1, with(live[1], "ancestry", Value{"s"})), {},
                         ContextStore::EditKeys::strict);
  check(field(refused, "reason") == Value{"unknown-entry-key"},
        "a strict edit with a non-array ancestry is refused");
  check(field(context.edit(with_entry(0, noted)), "accepted") == Value{true},
        "the store itself carries legacy keys verbatim");
  check(field(context.edit(context.view(), {}, ContextStore::EditKeys::strict),
              "accepted") == Value{true},
        "a strict edit passes an unchanged entry with a legacy key through");
  {
    auto changed = context.view();
    auto &entry = changed.object()[1].second.array()[0];
    entry = with(entry, "item", item("changed"));
    const auto outcome = context.edit(changed, {}, ContextStore::EditKeys::strict);
    check(field(outcome, "reason") == Value{"unknown-entry-key"},
          "a strict edit refuses a changed entry that keeps an unknown key");
  }
  const auto carried = context.view();
  const auto saved = context.checkpoint();
  check(saved.has_value(), "an entry with a carried key checkpoints");
  session.reopen();
  check(session.context->view() == carried && session.context->open_report().playback ==
                                                  ContextOpenReport::Playback::suffix,
        "and restores exactly from the snapshot");
  // A v1 snapshot whose entry carries a key the v2 inline rule once refused.
  Session v1{base / "entry-keys-v1"};
  v1.context->append({item("v1 entry")}, "fixture");
  auto entry = field(v1.context->view(), "entries").array()[0];
  const auto original = entry;
  entry = with(entry, "origin", Value{"legacy"});
  check(
      field(v1.context->edit(Value::object({{"base", Value{v1.context->head()}},
                                            {"entries", Value{Value::Array{entry}}}})),
            "accepted") == Value{true},
      "fixture edit");
  const auto expected = v1.context->view();
  save_both(v1, Value::object({{"version", Value{Number{"1"}}},
                               {"base", Value{v1.context->head()}},
                               {"entries", field(expected, "entries")},
                               {"live_originals", Value{Value::Array{original}}}}));
  v1.reopen();
  check(v1.context->view() == expected, "the v1 snapshot with a legacy key restores");
  // A write first: a checkpoint at the same boundary as both v1 slots ties, and a
  // tie keeps slot 0.
  v1.log->record(ApplicationChannel::program,
                 Value::object({{"label", Value{"mark"}}}));
  check(v1.context->checkpoint().has_value(), "and checkpoints as v2");
  v1.reopen();
  check(v1.context->view() == expected, "and reopens from the v2 snapshot (view)");
  check(field(v1.root->saved_state()->context, "version") == Value{Number{"2"}},
        "and reopens from the v2 snapshot (version " +
            unwrap(format_value(field(v1.root->saved_state()->context, "version"))) +
            ")");
}

// A-M4: "equals its original" is exact encoded-byte equality. A number edited to
// another representation is saved inline and restored as written; an entry whose
// keys a full-candidate edit reorders is inline too (the recorded cost); an
// unchanged entry stays a ref.
void byte_equality(const fs::path &base) {
  Session session{base / "byte-equality"};
  auto &context = *session.context;
  context.append({Value::object({{"role", Value{"assistant"}},
                                 {"content", Value{"n"}},
                                 {"n", Value{Number{std::uint64_t{1}}}}}),
                  item("untouched"), item("reordered")},
                 "fixture");
  auto view = context.view();
  auto &entries = view.object()[1].second.array();
  entries[0] = with(entries[0], "item",
                    with(field(entries[0], "item"), "n", Value{Number{1.0}}));
  const auto reordered = field(entries[2], "item");
  entries[2] = with(entries[2], "item",
                    Value::object({{"content", field(reordered, "content")},
                                   {"role", field(reordered, "role")}}));
  check(field(context.edit(view), "accepted") == Value{true}, "fixture edit");
  const auto expected = context.view();
  unwrap(context.checkpoint());
  const auto &saved = field(session.root->saved_state()->context, "entries").array();
  check(std::holds_alternative<Value::Object>(saved[0].value()) &&
            std::holds_alternative<Value::Array>(saved[1].value()) &&
            std::holds_alternative<Value::Object>(saved[2].value()),
        "number representation and key order make an entry inline; unchanged is a ref");
  session.reopen();
  const auto restored = session.context->view();
  check(unwrap(encode_packet(restored)) == unwrap(encode_packet(expected)),
        "the restored context encodes byte for byte like the live one");
}

// A-M3 and A-M6: every refused slot is reported; the other slot is used before
// whole-history playback, by the loader and by the store.
void slot_fallback(const fs::path &base) {
  const auto fixture = [&] {
    Session session{base / "slots-template"};
    session.context->append({item("question", "user"), item("answer")}, "fixture");
    unwrap(session.context->checkpoint()); // the older, good slot
    session.context->append({item("tail")}, "fixture");
    return Fixture{session.path, session.context->view(), session.context->head()};
  }();
  // The newer slot refused by the loader: the older slot is restored, and reported.
  {
    Session session{copy_of(fixture, base, "slots-loader"), false};
    unwrap(session.root->save_current_state(
        Value::object({{"version", Value{Number{"2"}}}, {"base", Value{"bad"}}})));
    const auto refused_slot = session.root->saved_state()->slot;
    session.reopen();
    const auto &report = session.context->open_report();
    require_restored(session, fixture, "newer slot refused by the loader");
    check(report.playback == ContextOpenReport::Playback::suffix && report.recovery() &&
              report.slot_refusals.size() == 1 &&
              report.slot_refusals[0].first == refused_slot &&
              report.slot_refusals[0].second.detail ==
                  static_cast<std::int64_t>(SavedContextFault::base),
          "the refused newer slot is reported while the older one is used");
  }
  // The selected slot's archive catalogue does not open: the older slot with its
  // own catalogue is used, and the fault is reported against the newer slot.
  {
    Session session{copy_of(fixture, base, "slots-catalogue"), false};
    const auto slot = session.root->saved_state()->slot;
    session.close();
    fs::resize_file(session.path / ("audit.archive." + std::to_string(slot)), 5);
    session.reopen();
    const auto &report = session.context->open_report();
    require_restored(session, fixture, "catalogue unavailable");
    check(report.recovery() && report.slot_refusals.empty() &&
              report.catalogue_faults.size() == 1 &&
              report.catalogue_faults[0].first == slot && report.restored_slot &&
              *report.restored_slot != slot && session.root->compact_recovery(),
          "a catalogue fault is reported, and the older slot used");
  }
  // Both catalogues fail: the selected slot is still used (full replay) and is
  // reported as a catalogue fault, never as a refused slot (recheck-A N1).
  {
    Session session{copy_of(fixture, base, "slots-catalogues"), false};
    const auto slot = session.root->saved_state()->slot;
    session.close();
    for (const auto *name : {"audit.archive.0", "audit.archive.1"})
      fs::resize_file(session.path / name, 5);
    session.reopen();
    const auto &report = session.context->open_report();
    require_restored(session, fixture, "both catalogues unavailable");
    check(report.slot_refusals.empty() && report.catalogue_faults.size() == 2 &&
              report.restored_slot == slot &&
              report.playback == ContextOpenReport::Playback::suffix,
          "a used slot whose catalogue does not open is a catalogue fault, not a "
          "refusal");
  }
  // The newer slot passes the loader but not the store; over the whole-history
  // bound the older slot still restores (A-M6).
  {
    Session session{copy_of(fixture, base, "slots-store"), false};
    auto bad = session.root->saved_state()->context;
    std::erase_if(bad.object(), [](const auto &pair) {
      return pair.first == "physical_records" || pair.first == "archive_entries" ||
             pair.first == "semantic_coverage";
    });
    auto entries = field(bad, "entries").array();
    entries.push_back(
        Value{Value::Array{Value{std::string(32, 'e')}, Value{Number{0}}}});
    unwrap(session.root->save_current_state(with(bad, "entries", Value{entries})));
    session.open_root();
    ContextStore store{*session.log, ContextOpen{.whole_history_limit = 1024}};
    const auto &report = store.open_report();
    check(store.view() == fixture.view && report.older_slot &&
              report.refused_by_store &&
              report.playback == ContextOpenReport::Playback::suffix,
          "the store falls back to the older slot before whole history");
  }
}

// B-M1: the repair frame is written outside the resident-tail cap.
void read_only_at_cap(const fs::path &base) {
  Session session{base / "read-only-cap"};
  session.context->append({item("question", "user"), item("answer")}, "fixture");
  unwrap(session.context->checkpoint());
  const auto bad_identity = session.log->issue();
  CutFrame bad;
  bad.revision = hex_identity(bad_identity.bytes());
  bad.base = bad.observed = session.context->head();
  bad.result = CutResult{99, 0, 0};
  (void)session.log->record(bad_identity, ApplicationChannel::context,
                            encode_cut_frame(bad));
  session.reopen();
  check(session.root->compact_recovery() && session.context->read_only(),
        "a compact read-only session");
  // Fill the resident tail until ordinary writes are refused.
  bool capped = false;
  for (unsigned i = 0; i < 1200 && !capped; ++i)
    try {
      session.log->record(ApplicationChannel::log,
                          Value::object({{"label", Value{"filler"}}}));
    } catch (const Error &error) {
      capped = error.code == ErrorCode::capacity;
    }
  check(capped, "the tail reaches its cap");
  const auto outcome = session.context->repair({});
  check(field(outcome, "accepted") == Value{true} && !session.context->read_only(),
        "the repair is accepted at the cap");
  check(session.root->saved_state()->boundary.sequence ==
            session.root->cursor().sequence,
        "snapshots resume at once, clearing the tail");
  session.reopen();
  check(!session.context->read_only(), "the reopened session is writable");
}

// B-M2: a skipped frame's originals are not resolvable, behind a snapshot too, and
// whole-history playback agrees with snapshot restore.
void skipped_originals(const fs::path &base) {
  Session session{base / "skipped"};
  session.context->append({item("question", "user")}, "fixture");
  unwrap(session.context->checkpoint());
  const auto bad_identity = session.log->issue();
  CutFrame bad;
  bad.revision = hex_identity(bad_identity.bytes());
  bad.base = bad.observed = session.context->head();
  bad.originals = {{item("never applied"), 0}};
  bad.insert = {{std::nullopt, std::uint32_t{0}}};
  bad.result = CutResult{99, 0, 0};
  (void)session.log->record(bad_identity, ApplicationChannel::context,
                            encode_cut_frame(bad));
  session.reopen();
  (void)session.context->repair({});
  unwrap(session.context->checkpoint());
  session.reopen();
  bool refused = false;
  try {
    session.context->restore(bad.revision + ".0");
  } catch (const Error &error) {
    refused = error.code == ErrorCode::invalid_identity;
  }
  check(refused, "restore of a skipped frame's original is refused");
  for (const auto &original : session.context->originals().array())
    check(string_field(original, "id").substr(0, 32) != bad.revision,
          "a skipped frame's originals are not listed");
  ContextStore whole{*session.log, ContextOpen{.whole_history = true, .probe = true}};
  check(!whole.read_only() && whole.view() == session.context->view() &&
            whole.originals() == session.context->originals(),
        "whole-history playback agrees with snapshot restore");
}

// B-M3: a bad first context frame over the empty head, then /repair, reopens
// writable: the empty base is explicit in the frame.
void empty_head_repair(const fs::path &base) {
  Session session{base / "empty-head"};
  session.log->record(ApplicationChannel::program,
                      Value::object({{"label", Value{"filler"}}}));
  const auto bad_identity = session.log->issue();
  CutFrame bad;
  bad.revision = hex_identity(bad_identity.bytes());
  bad.result = CutResult{1, 0, 0}; // empty base and observed: the empty head
  (void)session.log->record(bad_identity, ApplicationChannel::context,
                            encode_cut_frame(bad));
  session.reopen();
  check(session.context->read_only() && session.context->head().empty(),
        "the first frame does not apply");
  (void)session.context->repair({});
  session.reopen();
  check(!session.context->read_only() && !session.context->head().empty(),
        "the repaired session reopens writable");
  // Playback across the bad frame and the repair frame, not only the snapshot the
  // repair let the store write.
  {
    ContextStore whole{*session.log, ContextOpen{.whole_history = true, .probe = true}};
    check(!whole.read_only() && whole.view() == session.context->view(),
          "whole-history playback across the repair frame is writable and equal");
  }
  session.context->append({item("after repair")}, "fixture");
}

// B-M4: picks keep tool calls paired.
void repair_pairing(const fs::path &base) {
  Session session{base / "pairing"};
  session.context->append({item("question", "user")}, "fixture");
  const auto head = session.context->head();
  const auto bad_identity = session.log->issue();
  CutFrame bad;
  bad.revision = hex_identity(bad_identity.bytes());
  bad.base = bad.observed = head;
  bad.result = CutResult{99, 0, 0};
  (void)session.log->record(bad_identity, ApplicationChannel::context,
                            encode_cut_frame(bad));
  const auto later = [&](std::string_view after, Value one) {
    const auto identity = session.log->issue();
    const auto revision = hex_identity(identity.bytes());
    (void)session.log->record(
        identity, ApplicationChannel::context,
        Value::object({{"op", Value{"append"}},
                       {"base", Value{std::string{after}}},
                       {"observed", Value{std::string{after}}},
                       {"revision", Value{revision}},
                       {"accepted", Value{true}},
                       {"origin", Value{"later"}},
                       {"format", Value{"append-delta-v1"}},
                       {"originals", Value{Value::Array{Value::object(
                                         {{"id", Value{revision + ".0"}},
                                          {"item", std::move(one)}})}}}}));
    return revision;
  };
  const auto call = later(bad.revision, Value::object({{"type", Value{"function_call"}},
                                                       {"call_id", Value{"c1"}},
                                                       {"name", Value{"exec"}},
                                                       {"arguments", Value{"{}"}}}));
  const auto output =
      later(call, Value::object({{"type", Value{"function_call_output"}},
                                 {"call_id", Value{"c1"}},
                                 {"output", Value{"x"}}}));
  session.reopen();
  const auto preview = session.context->repair_preview();
  const auto &offered = field(preview, "originals").array();
  check(offered.size() == 2 && field(preview, "skipped").array().size() == 3 &&
            field(offered[0], "pairs") == Value{false} &&
            field(offered[1], "pairs") == Value{false},
        "the preview lists the skipped originals and marks unpaired picks");
  bool refused = false;
  try {
    (void)session.context->repair(Value::Array{Value{output + ".0"}});
  } catch (const Error &error) {
    refused = error.code == ErrorCode::conflict &&
              error.detail == ContextStore::repair_unpaired;
  }
  check(refused && session.context->read_only(), "an orphan output pick is refused");
  (void)session.context->repair(Value::Array{Value{call + ".0"}, Value{output + ".0"}});
  check(!session.context->read_only() &&
            field(session.context->view(), "entries").array().size() == 3,
        "the call and its output together are accepted");
}

// The empty context checkpoints as a valid empty snapshot: no false recovery.
void empty_checkpoint(const fs::path &base) {
  Session session{base / "empty-checkpoint"};
  check(session.context->head().empty() && session.context->checkpoint().has_value(),
        "an empty context checkpoints");
  session.reopen();
  check(!session.context->open_report().recovery() &&
            session.context->open_report().playback ==
                ContextOpenReport::Playback::suffix,
        "and reopens from it with nothing to report");
}

// R1 (pre-S1): over the replay bound, a newer slot whose catalogue does not open
// gives way to the older slot with its own catalogue, still compact.
void older_catalogue(const fs::path &base) {
  Session session{base / "older-catalogue"};
  session.context->append({item("question", "user")}, "fixture");
  unwrap(session.context->checkpoint());
  const auto older = session.root->saved_state()->slot;
  session.context->append({item("answer")}, "fixture");
  unwrap(session.context->checkpoint());
  const auto newer = session.root->saved_state()->slot;
  session.context->append({item("tail")}, "fixture");
  const auto expected = session.context->view();
  session.close();
  fs::resize_file(session.path / ("audit.archive." + std::to_string(newer)), 5);
  session.root = unwrap(
      RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                              NativeJournalDirectory::open(session.path.string()))),
                          "audit", header, capacity, false, true, 1024));
  unwrap(session.root->confirm_recovery());
  Settled custody;
  unwrap(session.root->reconcile(custody));
  session.log = std::make_unique<AuditLog>(*session.root);
  session.context = std::make_unique<ContextStore>(*session.log);
  const auto &report = session.context->open_report();
  check(session.root->compact_recovery() &&
            session.root->saved_state()->slot == older &&
            session.context->view() == expected && report.restored_slot == older &&
            report.catalogue_faults.size() == 1 &&
            report.catalogue_faults[0].first == newer,
        "the older slot and its catalogue are used over the replay bound");
}

// A-M3: every store refusal keeps its slot; both slots refused both reach the
// report.
void both_store_refused(const fs::path &base) {
  const auto fixture = build(base / "both-refused-template");
  Session session{copy_of(fixture, base, "both-refused"), false};
  auto bad = core(session.root->saved_state()->context);
  auto entries = field(bad, "entries").array();
  entries.push_back(Value{Value::Array{Value{std::string(32, 'e')}, Value{Number{0}}}});
  save_both(session, with(bad, "entries", Value{entries}));
  session.reopen();
  const auto &report = session.context->open_report();
  require_restored(session, fixture, "both slots refused by the store");
  check(report.store_refusals.size() == 2 &&
            report.store_refusals[0].first != report.store_refusals[1].first &&
            report.playback == ContextOpenReport::Playback::whole_history,
        "both store refusals are kept, each with its slot");
}

// Recheck-B N1: a call the head left unanswered before the bad frame does not block
// /repair; the preview names it and marks no pick for it.
void repair_pending_call(const fs::path &base) {
  Session session{base / "pending-call"};
  session.context->append(
      {item("question", "user"), Value::object({{"type", Value{"function_call"}},
                                                {"call_id", Value{"c1"}},
                                                {"name", Value{"exec"}},
                                                {"arguments", Value{"{}"}}})},
      "fixture");
  const auto bad_identity = session.log->issue();
  CutFrame bad;
  bad.revision = hex_identity(bad_identity.bytes());
  bad.base = bad.observed = session.context->head();
  bad.originals = {{item("note", "user"), 0}};
  bad.insert = {{std::nullopt, std::uint32_t{0}}};
  bad.result = CutResult{99, 0, 0};
  (void)session.log->record(bad_identity, ApplicationChannel::context,
                            encode_cut_frame(bad));
  session.reopen();
  const auto preview = session.context->repair_preview();
  check(field(preview, "head_faults") == Value{Value::Array{Value{"call:c1"}}} &&
            field(field(preview, "originals").array()[0], "pairs") == Value{true},
        "the preview names the head's own unanswered call and marks no pick");
  (void)session.context->repair({});
  session.reopen();
  check(!session.context->read_only(), "the repaired session reopens writable");
}

// Recheck-A N3: the payload parser keeps the error class of a decode that is not
// malformed bytes (here a value past the depth limit: capacity).
void payload_error_class(const fs::path &) {
  Value deep = Value::object({});
  for (int i = 0; i < 200; ++i)
    deep = Value{Value::Array{std::move(deep)}};
  const ValueLimits wide{std::size_t{1} << 30, 1000000, 1000};
  const auto core_bytes =
      unwrap(encode_packet(Value::object({{"version", Value{Number{"2"}}},
                                          {"base", Value{std::string(32, 'a')}},
                                          {"deep", deep}}),
                           wide));
  const auto programs = unwrap(encode_packet(Value{Value::Array{}}));
  std::vector<std::byte> body{core_bytes.begin(), core_bytes.end()};
  body.insert(body.end(), programs.begin(), programs.end());
  body.resize(body.size() + 16); // entries and sections not streamed
  const auto payload =
      decode_saved_payload(body, 2, core_bytes.size(), programs.size());
  check(!payload.has_value() && payload.error().code == ErrorCode::capacity,
        std::string{"a value past the decode limits is capacity, not corrupt ("} +
            (payload.has_value() ? "decoded" : error_name(payload.error().code)) + ")");
  // Any other decode error is malformed bytes: saved_payload_fuzz's input, whose
  // packets decode to invalid_range (arconaut-gjf), is corrupt.
  const std::vector<std::uint8_t> fuzzed{
      0x42, 0x42, 0x4d, 0x02, 0x0a, 0x02, 0x06, 0x07, 0x76, 0x65, 0x2d, 0x73, 0x69,
      0x6f, 0x6e, 0x03, 0x02, 0x06, 0x04, 0x62, 0x61, 0x73, 0x65, 0x00, 0x00, 0x42,
      0x42, 0x4d, 0x02, 0x0b, 0x01, 0x0a, 0x01, 0x06, 0x05, 0x6c, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x61, 0x62, 0x65, 0x6c, 0x06, 0x10, 0x73,
      0x65, 0x73, 0x73, 0x69, 0x53, 0x6e, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0xff, 0x4d, 0x69, 0x8b, 0x8b, 0x8b, 0x00};
  const auto malformed = decode_saved_payload(
      std::as_bytes(std::span{fuzzed.data(), fuzzed.size()}), 2, 25, 33);
  check(!malformed.has_value() && malformed.error().code == ErrorCode::corrupt,
        std::string{"a packet that decodes to invalid_range is corrupt ("} +
            (malformed.has_value() ? "decoded" : error_name(malformed.error().code)) +
            ")");
}
#endif

void run(std::string_view name, const std::function<void()> &body) {
  try {
    body();
    std::cout << "PASS " << name << '\n';
  } catch (const std::exception &error) {
    ++failures;
    std::cout << "FAIL " << name << ": " << error.what() << '\n';
  } catch (const Error &error) {
    ++failures;
    std::cout << "FAIL " << name << ": " << error_name(error.code) << " ("
              << error.detail << ")\n";
  }
}
} // namespace

int main(int argc, char **argv) {
  const auto base = scratch();
  const auto selected = [&](std::string_view name) {
    if (argc < 2)
      return true;
    for (int i = 1; i < argc; ++i)
      if (name == argv[i])
        return true;
    return false;
  };
  if (selected("v1-store-faults"))
    run("v1-store-faults", [&] { v1_store_faults(base); });
  if (selected("large-snapshot"))
    run("large-snapshot", [&] { large_snapshot(base); });
#if !defined(S1_RED_OLD_CODE)
  if (selected("v2-loader-faults"))
    run("v2-loader-faults", [&] { v2_loader_faults(base); });
  if (selected("v2-store-faults"))
    run("v2-store-faults", [&] { v2_store_faults(base); });
  if (selected("read-only"))
    run("read-only", [&] { read_only(base); });
  for (const auto &[name, body] :
       std::initializer_list<std::pair<std::string_view, void (*)(const fs::path &)>>{
           {"entry-keys", entry_keys},
           {"byte-equality", byte_equality},
           {"slot-fallback", slot_fallback},
           {"read-only-at-cap", read_only_at_cap},
           {"skipped-originals", skipped_originals},
           {"empty-head-repair", empty_head_repair},
           {"repair-pairing", repair_pairing},
           {"empty-checkpoint", empty_checkpoint},
           {"older-catalogue", older_catalogue},
           {"both-store-refused", both_store_refused},
           {"repair-pending-call", repair_pending_call},
           {"payload-error-class", payload_error_class}})
    if (selected(name))
      run(name, [&, body = body] { body(base); });
#endif
  std::error_code ignored;
  if (failures == 0)
    fs::remove_all(base, ignored);
  else
    std::cout << "Fixtures kept in " << base.string() << '\n';
  return failures == 0 ? 0 : 1;
}
