// Units and classes (unit S2, CONTEXT_LIFECYCLE.md §3, §5.1-§5.4, §11.1).
// Oracles:
// - specification: each production append label has its design §5.2 class; a
//   repaired round [call][operator][output] is selectable without its instruction;
//   operator.beads is a result; a spoofed class is refused;
// - model: over generated sessions the store's units equal the generator's own
//   record of every unit (handle, class, entries, open, superseded, pinned), and
//   expand() equals an independent evaluation of the selection over those units;
// - property: no expansion holds an open, protected or pinned unit, except a
//   superseded instruction copy, and manage accepts every expansion as it is;
// - metamorphic: units and pins are equal live, after snapshot restore, after an
//   older-slot fallback, from a snapshot without a classes section, from a version-1
//   snapshot and after whole-history playback.
// Built with -DS2_RED_OLD_CODE, only the cases the old API can express compile; they
// fail on the old code (partial-tool-group, mandatory-entry).
#include "blackbird/context.hpp"
#include "blackbird/context_cut.hpp"
#include "blackbird/journal.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/saved_state.hpp"
#if !defined(S2_RED_OLD_CODE)
#include "blackbird/context_units.hpp"
#endif
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>

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
  explicit Session(const fs::path &directory) : path(directory) {
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
  void open_root() {
    context.reset();
    log.reset();
    root.reset();
    root = unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                                          NativeJournalDirectory::open(path.string()))),
                                      "audit", header, capacity));
    unwrap(root->confirm_recovery());
    Settled custody;
    unwrap(root->reconcile(custody));
    log = std::make_unique<AuditLog>(*root);
  }
  void reopen(ContextOpen open = {}) {
    open_root();
    context = std::make_unique<ContextStore>(*log, open);
  }
};
fs::path scratch() {
  std::string pattern = (fs::temp_directory_path() / "blackbird-units-XXXXXX").string();
  check(::mkdtemp(pattern.data()) != nullptr, "mkdtemp");
  return fs::canonical(pattern);
}
Value message(std::string text, std::string_view role) {
  return Value::object(
      {{"role", Value{std::string{role}}}, {"content", Value{std::move(text)}}});
}
Value call(std::string id, std::string name = "exec") {
  return Value::object({{"type", Value{"function_call"}},
                        {"call_id", Value{std::move(id)}},
                        {"name", Value{std::move(name)}},
                        {"arguments", Value{"{}"}}});
}
Value output(std::string id, Value body = Value{"ok"}) {
  return Value::object({{"type", Value{"function_call_output"}},
                        {"call_id", Value{std::move(id)}},
                        {"output", std::move(body)}});
}
Value::Array entries_of(const ContextStore &context) {
  return field(context.view(), "entries").array();
}
std::string id_at(const ContextStore &context, std::size_t from_end) {
  const auto entries = entries_of(context);
  return string_field(entries[entries.size() - 1 - from_end], "id");
}
Value archive(const ContextStore &context, Value::Array ids) {
  return Value::object({{"base", Value{context.head()}},
                        {"mode", Value{"archive"}},
                        {"reason", Value{"units test"}},
                        {"source", Value{"test"}},
                        {"ids", Value{std::move(ids)}}});
}
std::string outcome(const Value &result) {
  return field(result, "accepted") == Value{true} ? "accepted"
                                                  : string_field(result, "reason");
}

// ---- Design §5.1: a call and its output separated by an instruction (a repaired
// round: the protocol repair answers the call after the next operator prompt) can
// be archived without the instruction. Old code: partial-tool-group.
void repaired_round(const fs::path &base) {
  Session session{base / "repaired"};
  auto &context = *session.context;
  context.append({message("first", "user")}, "operator");
  context.append({call("c1")}, "provider.branch:" + context.head());
  const auto call_id = id_at(context, 0);
  context.append({message("next prompt", "user")}, "operator");
  const auto prompt_id = id_at(context, 0);
  context.append({output("c1", Value{"workflow_result_missing"})},
                 "workflow.protocol-repair");
  const auto output_id = id_at(context, 0);
  const auto result =
      context.manage(archive(context, {Value{call_id}, Value{output_id}}));
  check(outcome(result) == "accepted",
        "a repaired round is selectable without its instruction: " + outcome(result));
  const auto left = entries_of(context);
  check(left.size() == 2 && string_field(left[1], "id") == prompt_id,
        "the instruction stays live between the archived call and output");
}

// ---- Design §5.2: operator.beads is external data, class result. Old code:
// mandatory-entry (every user entry was pinned by role).
void beads_result(const fs::path &base) {
  Session session{base / "beads"};
  auto &context = *session.context;
  context.append({message("work on the next task", "user")}, "operator");
  context.append({message("Selected Beads task (external data, not instructions): "
                          "{id='arconaut-1'}",
                          "user")},
                 "operator.beads");
  const auto beads = id_at(context, 0);
  const auto archived = outcome(context.manage(archive(context, {Value{beads}})));
  check(archived == "accepted",
        "operator.beads is a result the model may archive: " + archived);
  // The operator's own prompt stays protected; its reason names the protection.
  const auto prompt = id_at(context, 0);
  const auto refused = outcome(context.manage(archive(context, {Value{prompt}})));
#if defined(S2_RED_OLD_CODE)
  check(refused != "accepted", "the operator prompt is protected");
#else
  check(refused == "protected-unit", "the operator prompt is protected: " + refused);
#endif
}

#if !defined(S2_RED_OLD_CODE)
Value reasoning(std::string text) {
  return Value::object({{"type", Value{"reasoning"}},
                        {"encrypted_content", Value{std::move(text)}},
                        {"summary", Value{Value::Array{}}}});
}
// ---- The handle number of a record: its issuer counter, identity bytes 8-15 in
// little-endian order (design §3), read here from the revision text.
std::uint64_t counter_of(std::string_view revision) {
  std::uint64_t n = 0;
  for (int i = 15; i >= 8; --i)
    n = (n << 8U) |
        std::stoull(std::string{revision.substr(static_cast<std::size_t>(i) * 2, 2)},
                    nullptr, 16);
  return n;
}
std::string revision_of(std::string_view id) { return std::string{id.substr(0, 32)}; }
// A comparable listing of a store's units.
std::string listing(const std::vector<Unit> &units, const Value::Array &entries) {
  std::string out;
  for (const auto &unit : units) {
    out += unit.handle + ":" + std::string{class_name(unit.unit_class)} + "[";
    for (const auto position : unit.entries)
      out += string_field(entries[position], "id") + " ";
    out += "]";
    if (unit.open)
      out += " open";
    if (unit.superseded)
      out += " superseded";
    if (unit.pinned)
      out += " pinned";
    out += "\n";
  }
  return out;
}
std::string listing(const ContextStore &context) {
  return listing(context.units(), entries_of(context));
}
std::string probe_listing(Session &session, bool whole) {
  ContextStore other{*session.log, ContextOpen{.whole_history = whole, .probe = true}};
  return listing(other);
}
const Unit &unit_of(const std::vector<Unit> &units, const Value::Array &entries,
                    const std::string &id) {
  for (const auto &unit : units)
    for (const auto position : unit.entries)
      if (string_field(entries[position], "id") == id)
        return unit;
  throw std::runtime_error("no unit holds " + id);
}
UnitClass class_of(const ContextStore &context, const std::string &id) {
  const auto entries = entries_of(context);
  return unit_of(context.units(), entries, id).unit_class;
}

// ---- Design §5.2: every production append label and its class.
void label_classes(const fs::path &base) {
  Session session{base / "labels"};
  auto &context = *session.context;
  struct Row {
    std::string label;
    Value::Array items;
    UnitClass expected;
  };
  const std::vector<Row> rows{
      {"operator", {message("prompt", "user")}, UnitClass::instruction},
      {"rrc:00112233445566778899aabbccddeeff",
       {message("continue", "user")},
       UnitClass::instruction},
      {"backstop.instructions",
       {message("brief", "developer")},
       UnitClass::instruction},
      {"provider.branch:x", {reasoning("r"), call("b1")}, UnitClass::round},
      {"lua.synthetic", {output("b1")}, UnitClass::round}, // joins b1's round
      {"lua.synthetic", {call("l1", "read_file")}, UnitClass::round},
      {"workflow.protocol-repair", {output("l1")}, UnitClass::round},
      {"lua.synthetic", {message("program note", "developer")}, UnitClass::result},
      {"workflow-continuation",
       {message("Registered workflow w completed", "developer")},
       UnitClass::result},
      {"operator.beads", {message("Selected Beads task", "user")}, UnitClass::result},
      {"application.observations",
       {message("Host page observations", "user")},
       UnitClass::result},
      {"provider.request-state", {message("state", "developer")}, UnitClass::state},
      {"fixture-unknown", {message("by role", "user")}, UnitClass::instruction},
      {"fixture-unknown", {message("by role", "assistant")}, UnitClass::round},
      {"fixture-unknown", {message("by role", "tool")}, UnitClass::result},
  };
  for (const auto &row : rows) {
    context.append(row.items, row.label);
    const auto id = id_at(context, 0);
    check(class_of(context, id) == row.expected,
          row.label + " appends class " + std::string{class_name(row.expected)} +
              ", not " + std::string{class_name(class_of(context, id))});
  }
  context.append({call("s1")}, "provider.branch:y");
  context.append({output("s1")}, "stop.linkage");
  check(class_of(context, id_at(context, 0)) == UnitClass::round,
        "stop.linkage outputs join their call's round");
  // A declared class: instruction or result, from Lua only.
  context.append({message("declared brief", "assistant")}, "lua.synthetic",
                 UnitClass::instruction);
  check(class_of(context, id_at(context, 0)) == UnitClass::instruction,
        "a declared instruction is an instruction");
  context.append({message("declared result", "user")}, "lua.synthetic",
                 UnitClass::result);
  check(class_of(context, id_at(context, 0)) == UnitClass::result,
        "a declared result is a result, whatever its role");
  // A managed summary is class summary.
  const auto content_of = [](const Value &e) {
    const auto *content = field(e, "item").find("content");
    return content ? content->string() : std::string{};
  };
  const auto continuation = [&] {
    for (const auto &e : entries_of(context))
      if (content_of(e) == "program note")
        return string_field(e, "id");
    throw std::runtime_error("no program note");
  }();
  check(outcome(context.manage(Value::object(
            {{"base", Value{context.head()}},
             {"mode", Value{"summarize"}},
             {"reason", Value{"t"}},
             {"source", Value{"test"}},
             {"ids", Value{Value::Array{Value{continuation}}}},
             {"summary", message("summary", "developer")}}))) == "accepted",
        "a result can be summarised");
  for (const auto &e : entries_of(context))
    if (content_of(e) == "summary")
      check(class_of(context, string_field(e, "id")) == UnitClass::summary,
            "a managed summary is class summary");
  // Every class survives restore and whole-history playback.
  const auto live = listing(context);
  unwrap(context.checkpoint());
  session.reopen();
  check(listing(*session.context) == live,
        "classes after restore:\n" + listing(*session.context) + "live:\n" + live);
  check(probe_listing(session, true) == live, "classes after whole-history playback");
}

// ---- Spoofed classes and origins are refused (design §5.2).
void spoofing(const fs::path &base) {
  Session session{base / "spoof"};
  auto &context = *session.context;
  const auto refused = [&](auto &&body, ErrorCode code) {
    const auto before = session.root->cursor().sequence;
    try {
      body();
    } catch (const Error &error) {
      return error.code == code && session.root->cursor().sequence == before;
    }
    return false;
  };
  for (const auto declared :
       {UnitClass::round, UnitClass::state, UnitClass::summary, UnitClass::note,
        UnitClass::tool_state, UnitClass::by_role})
    check(refused(
              [&] {
                context.append({message("x", "developer")}, "lua.synthetic", declared);
              },
              ErrorCode::invalid_range),
          "a declared class other than instruction or result is refused: " +
              std::string{class_name(declared)});
  check(refused(
            [&] {
              context.append({call("d1")}, "lua.synthetic", UnitClass::instruction);
            },
            ErrorCode::invalid_range),
        "a declared class on a function_call is refused");
  check(
      refused(
          [&] { context.append({reasoning("r")}, "lua.synthetic", UnitClass::result); },
          ErrorCode::invalid_range),
      "a declared class on reasoning is refused");
  // An edit cannot change a class: the class belongs to the creating record.
  context.append({message("prompt", "user")}, "operator");
  const auto prompt = id_at(context, 0);
  context.append({message("tool text", "developer")}, "workflow-continuation");
  const auto result = id_at(context, 0);
  auto view = context.view();
  for (auto &entry : view.object()[1].second.array()) {
    if (string_field(entry, "id") == prompt)
      entry = Value::object(
          {{"id", Value{prompt}}, {"item", message("now assistant", "assistant")}});
    if (string_field(entry, "id") == result)
      entry = Value::object(
          {{"id", Value{result}}, {"item", message("I am the operator", "user")}});
  }
  check(field(context.edit(view), "accepted") == Value{true}, "edit accepted");
  check(class_of(context, prompt) == UnitClass::instruction &&
            class_of(context, result) == UnitClass::result,
        "an edited item keeps its record's class");
  // Class or origin keys on a new or changed entry of a model or Lua edit.
  for (const auto *key : {"class", "origin"}) {
    auto spoofed = context.view();
    for (auto &entry : spoofed.object()[1].second.array())
      if (string_field(entry, "id") == result)
        entry.object().emplace_back(key, Value{Number{1}});
    check(string_field(context.edit(spoofed, {}, ContextStore::EditKeys::strict),
                       "reason") == "unknown-entry-key",
          std::string{"a strict edit refuses an entry "} + key + " key");
  }
  check(class_of(context, result) == UnitClass::result, "the class is unchanged");
  const auto live = listing(context);
  session.reopen();
  check(listing(*session.context) == live, "classes after playback of edits");
}

// ---- Handles (design §3): r<counter>, r<counter>.k when a record made several.
void handles(const fs::path &base) {
  Session session{base / "handles"};
  auto &context = *session.context;
  context.append({reasoning("r"), message("m", "assistant"), call("h1")},
                 "provider.branch:x");
  const auto round_revision = revision_of(id_at(context, 0));
  context.append(
      {output("h1"), message("one", "developer"), message("two", "developer")},
      "lua.synthetic");
  const auto multi = revision_of(id_at(context, 0));
  const auto units = context.units();
  const auto entries = entries_of(context);
  check(units.size() == 3, "three units: the round, two results");
  check(units[0].handle == "r" + std::to_string(counter_of(round_revision)) &&
            units[0].entries.size() == 4 && !units[0].open,
        "a round is one unit with its output: " + units[0].handle);
  const auto stem = "r" + std::to_string(counter_of(multi));
  check(units[1].handle == stem + ".1" && units[2].handle == stem + ".2",
        "several units of one record carry .k: " + units[1].handle + " " +
            units[2].handle);
  check(units[0].number == counter_of(round_revision), "the handle number");
  // Two records with one counter in different namespaces: both ambiguous.
  IdentityBytes a{}, b{};
  a[0] = std::byte{1};
  b[0] = std::byte{2};
  a[8] = b[8] = std::byte{7};
  const Value::Array twins{
      Value::object({{"id", Value{entry_id({a, 0})}}, {"item", message("a", "tool")}}),
      Value::object({{"id", Value{entry_id({b, 0})}}, {"item", message("b", "tool")}})};
  const RecordClasses results = record_classes({UnitClass::result}, {});
  const auto resolved = blackbird::units(
      twins, [&](const IdentityBytes &) -> const RecordClasses & { return results; });
  check(resolved.size() == 2 && resolved[0].ambiguous && resolved[1].ambiguous &&
            resolved[0].handle == "r7",
        "colliding handles are marked ambiguous");
  const auto parsed = parse_selection("r7");
  const auto *selection = std::get_if<UnitSelection>(&parsed);
  check(selection != nullptr, "r7 parses");
  const auto expansion = expand(*selection, resolved, twins, ExpandPurpose::archive);
  check(expansion.units.empty() && expansion.refused.size() == 2 &&
            expansion.refused[0].reason == "ambiguous",
        "an ambiguous handle is refused, never guessed");
}

// ---- Superseded copies (design §5.1, §23 decision 2): byte-equal instruction.
void superseded(const fs::path &base) {
  Session session{base / "superseded"};
  auto &context = *session.context;
  const auto brief = message("Ultracode brief", "developer");
  context.append({brief}, "lua.synthetic", UnitClass::instruction);
  const auto older = id_at(context, 0);
  context.append({message("work", "assistant")}, "provider.branch:x");
  context.append({brief}, "lua.synthetic", UnitClass::instruction);
  const auto newer = id_at(context, 0);
  // The same text with its keys in another order encodes differently: not a copy.
  context.append({Value::object({{"content", Value{"Ultracode brief"}},
                                 {"role", Value{"developer"}}})},
                 "lua.synthetic", UnitClass::instruction);
  const auto reordered = id_at(context, 0);
  // A result with the same bytes is not an instruction copy and supersedes nothing.
  context.append({message("Rule", "developer")}, "operator");
  const auto rule = id_at(context, 0);
  context.append({message("Rule", "developer")}, "workflow-continuation");
  auto units = context.units();
  auto entries = entries_of(context);
  check(unit_of(units, entries, older).superseded &&
            !unit_of(units, entries, newer).superseded &&
            !unit_of(units, entries, reordered).superseded &&
            !unit_of(units, entries, rule).superseded,
        "only the older byte-equal instruction is superseded");
  check(outcome(context.manage(archive(context, {Value{newer}}))) == "protected-unit",
        "the newest copy stays protected");
  auto summary = archive(context, {Value{older}});
  for (auto &[key, value] : summary.object())
    if (key == "mode")
      value = Value{"summarize"};
  summary.object().emplace_back("summary", message("s", "developer"));
  check(outcome(context.manage(summary)) == "protected-unit",
        "a superseded copy is archived, never summarised");
  check(outcome(context.manage(archive(context, {Value{older}}))) == "accepted",
        "the older copy may be archived");
  units = context.units();
  entries = entries_of(context);
  check(!unit_of(units, entries, newer).superseded, "the newest copy stays protected");
}

// ---- Selections (design §11.1): grammar corners.
void grammar() {
  const auto error = [](std::string_view text) {
    return std::holds_alternative<UnitSelectionError>(parse_selection(text));
  };
  for (const auto *bad : {"",
                          " ",
                          ",",
                          "r",
                          "r01",
                          "r1-",
                          "r5-r2",
                          "r1-r2-r3",
                          "r1.",
                          "r1.01",
                          "1",
                          "tool=",
                          "kind=bogus",
                          "kind=",
                          "before=",
                          "before=12",
                          "before=r",
                          "r1 r2",
                          "superseded,",
                          "r1,,r2",
                          "pinned",
                          "tool=exec unknown",
                          "r-1",
                          "R1",
                          "r18446744073709551616"})
    check(error(bad), std::string{"refused selection: '"} + bad + "'");
  for (const auto *good :
       {"r0", "r12", "r4821.1", "r12-r40", " r12 , r15-r40 ", "tool=read_file",
        "kind=result before=r900", "superseded", "failed tool=exec",
        "r4821, tool=read_file superseded, kind=result before=r4900", "kind=tool_state",
        "r12-r12", "r18446744073709551615"})
    check(!error(good), std::string{"accepted selection: '"} + good + "'");
}

// ---- The generator: a session built by random production-shaped appends, with its
// own record of every unit it made.
struct ModelUnit {
  std::string handle;
  std::uint64_t number = 0;
  UnitClass unit_class = UnitClass::result;
  std::vector<std::string> ids;
  std::set<std::string> tools;
  bool failed = false;
  std::string bytes; // instruction role and text: the generator's byte equality
  bool pinned = false;
  std::size_t serial = 0;
};
ModelUnit model_unit(std::string handle, std::uint64_t number, UnitClass unit_class,
                     std::vector<std::string> ids, std::string bytes = {}) {
  ModelUnit unit;
  unit.handle = std::move(handle);
  unit.number = number;
  unit.unit_class = unit_class;
  unit.ids = std::move(ids);
  unit.bytes = std::move(bytes);
  return unit;
}
struct Generator {
  Session &session;
  std::mt19937 random;
  std::vector<ModelUnit> units;                  // in context order of first entry
  std::map<std::string, std::size_t> open_calls; // call_id -> unit serial
  std::size_t counter = 0, serials = 0;
  std::string fresh() { return "g" + std::to_string(counter++); }
  ContextStore &context() { return *session.context; }
  static std::string stem(const std::string &revision) {
    return "r" + std::to_string(counter_of(revision));
  }
  ModelUnit &by_serial(std::size_t serial) {
    for (auto &unit : units)
      if (unit.serial == serial)
        return unit;
    throw std::runtime_error("the model lost a unit");
  }
  bool open(const ModelUnit &unit) const {
    for (const auto &[c, serial] : open_calls)
      if (serial == unit.serial)
        return true;
    return false;
  }
  void add(ModelUnit unit) {
    unit.serial = serials++;
    units.push_back(std::move(unit));
  }
  // One unit for a one-entry append under label.
  void single(const Value &item, const std::string &label, UnitClass unit_class,
              std::string bytes = {}) {
    context().append({item}, label);
    const auto id = id_at(context(), 0);
    add(model_unit(stem(revision_of(id)), counter_of(revision_of(id)), unit_class, {id},
                   std::move(bytes)));
  }
  void step() {
    const auto kind = random() % 10;
    if (kind == 0) {
      const auto text = random() % 3 ? fresh() : std::string{"repeated rule"};
      single(message(text, "user"), "operator", UnitClass::instruction, "user:" + text);
    } else if (kind == 1 || kind == 2) {
      // A provider round: optional reasoning and message, then 0-2 calls.
      Value::Array items;
      if (random() % 2)
        items.push_back(reasoning(fresh()));
      if (random() % 2 || items.empty())
        items.push_back(message(fresh(), "assistant"));
      std::vector<std::string> calls;
      for (auto n = random() % 3; n > 0; --n) {
        calls.push_back(fresh());
        items.push_back(call(calls.back(), random() % 2 ? "exec" : "read_file"));
      }
      context().append(items, "provider.branch:" + context().head());
      const auto revision = revision_of(id_at(context(), 0));
      auto unit =
          model_unit(stem(revision), counter_of(revision), UnitClass::round, {});
      for (std::size_t i = 0; i < items.size(); ++i) {
        unit.ids.push_back(revision + "." + std::to_string(i));
        if (const auto *name = items[i].find("name"))
          unit.tools.insert(name->string());
      }
      add(std::move(unit));
      for (const auto &c : calls)
        open_calls[c] = units.back().serial;
    } else if ((kind == 3 || kind == 4) && !open_calls.empty()) {
      // Outputs for some open calls in one Lua append; some report failure.
      Value::Array items;
      std::vector<std::size_t> answered;
      for (auto it = open_calls.begin(); it != open_calls.end();) {
        if (random() % 2 || items.empty()) {
          const auto fail = random() % 4;
          items.push_back(output(
              it->first, fail == 0   ? Value::object({{"error", Value{"x"}}})
                         : fail == 1 ? Value::object({{"exit_code", Value{Number{2}}}})
                         : fail == 2 ? Value::object({{"exit_code", Value{Number{0}}}})
                                     : Value{"text"}));
          answered.push_back(it->second);
          if (fail < 2)
            by_serial(it->second).failed = true;
          it = open_calls.erase(it);
        } else
          ++it;
      }
      context().append(items, random() % 2 ? "lua.synthetic" : "stop.linkage");
      const auto revision = revision_of(id_at(context(), 0));
      for (std::size_t i = 0; i < answered.size(); ++i)
        by_serial(answered[i]).ids.push_back(revision + "." + std::to_string(i));
    } else if (kind == 5) {
      // Results: one or two developer notes in one append.
      const auto n = 1 + random() % 2;
      Value::Array items;
      for (std::size_t i = 0; i < n; ++i)
        items.push_back(message(fresh(), "developer"));
      context().append(items, random() % 2 ? "workflow-continuation" : "lua.synthetic");
      const auto revision = revision_of(id_at(context(), 0));
      for (std::size_t i = 0; i < n; ++i)
        add(model_unit(stem(revision) + (n > 1 ? "." + std::to_string(i) : ""),
                       counter_of(revision), UnitClass::result,
                       {revision + "." + std::to_string(i)}));
    } else if (kind == 6) {
      const auto text = random() % 2 ? fresh() : std::string{"brief"};
      context().append({message(text, "developer")}, "lua.synthetic",
                       UnitClass::instruction);
      const auto id = id_at(context(), 0);
      add(model_unit(stem(revision_of(id)), counter_of(revision_of(id)),
                     UnitClass::instruction, {id}, "developer:" + text));
    } else if (kind == 7)
      single(message(fresh(), "user"), "operator.beads", UnitClass::result);
    else if (kind == 8)
      single(message(fresh(), "developer"), "provider.request-state", UnitClass::state);
    else if (!units.empty()) {
      // Pin or unpin one unit by its handle.
      auto &unit = units[random() % units.size()];
      if (unit.pinned)
        (void)context().unpin(unit.handle);
      else
        (void)context().pin(unit.handle);
      unit.pinned = !unit.pinned;
    }
  }
  // The generator's superseded flag: an instruction whose bytes a later live
  // instruction unit repeats.
  bool superseded(std::size_t index) const {
    if (units[index].unit_class != UnitClass::instruction)
      return false;
    for (auto j = index + 1; j < units.size(); ++j)
      if (units[j].unit_class == UnitClass::instruction &&
          units[j].bytes == units[index].bytes)
        return true;
    return false;
  }
  std::string expected_listing() const {
    std::string out;
    for (std::size_t i = 0; i < units.size(); ++i) {
      const auto &unit = units[i];
      out += unit.handle + ":" + std::string{class_name(unit.unit_class)} + "[";
      for (const auto &id : unit.ids)
        out += id + " ";
      out += "]";
      if (open(unit))
        out += " open";
      if (superseded(i))
        out += " superseded";
      if (unit.pinned)
        out += " pinned";
      out += "\n";
    }
    return out;
  }
  // Removes archived units from the model, by their first entry IDs.
  void archived(const std::set<std::string> &ids) {
    std::erase_if(units,
                  [&](const ModelUnit &unit) { return ids.contains(unit.ids[0]); });
  }
};
// The model's evaluation of one selection item, from design §11.1.
struct ModelItem {
  enum class Kind { handle, range, filter } kind = Kind::filter;
  std::string handle;
  std::uint64_t first = 0, last = 0;
  std::optional<std::string> tool;
  std::optional<std::string> kind_name;
  bool superseded = false, failed = false;
  std::optional<std::uint64_t> before;
};
struct ModelSelection {
  std::string text;
  std::vector<ModelItem> items;
};
std::pair<std::vector<std::size_t>, std::vector<std::pair<std::string, std::string>>>
model_expand(const Generator &g, const ModelSelection &selection,
             ExpandPurpose purpose) {
  std::set<std::size_t> matched;
  std::vector<std::string> absent;
  for (const auto &item : selection.items) {
    bool any = false;
    for (std::size_t i = 0; i < g.units.size(); ++i) {
      const auto &unit = g.units[i];
      bool hit = false;
      if (item.kind == ModelItem::Kind::handle)
        hit = unit.handle == item.handle;
      else if (item.kind == ModelItem::Kind::range)
        hit = unit.number >= item.first && unit.number <= item.last;
      else {
        hit = (!item.superseded || g.superseded(i)) && (!item.failed || unit.failed);
        if (item.tool)
          hit = hit && unit.tools.contains(*item.tool);
        if (item.kind_name)
          hit = hit && *item.kind_name == class_name(unit.unit_class);
        if (item.before)
          hit = hit && unit.number < *item.before;
      }
      if (hit) {
        matched.insert(i);
        any = true;
      }
    }
    if (item.kind == ModelItem::Kind::handle && !any &&
        std::find(absent.begin(), absent.end(), item.handle) == absent.end())
      absent.push_back(item.handle);
  }
  std::vector<std::size_t> selected;
  std::vector<std::pair<std::string, std::string>> refused;
  for (const auto i : matched) {
    const auto &unit = g.units[i];
    const bool protected_class =
        (unit.unit_class == UnitClass::instruction && !g.superseded(i)) ||
        unit.unit_class == UnitClass::tool_state;
    std::string reason;
    if (purpose == ExpandPurpose::archive) {
      if (g.open(unit))
        reason = "open";
      else if (protected_class)
        reason = "protected";
      else if (unit.pinned)
        reason = "pinned";
    } else if (purpose == ExpandPurpose::unpin && !unit.pinned)
      reason = "not-pinned";
    if (reason.empty())
      selected.push_back(i);
    else
      refused.emplace_back(unit.handle, reason);
  }
  for (const auto &handle : absent)
    refused.emplace_back(handle, "absent");
  return {selected, refused};
}
ModelSelection random_selection(const Generator &g, std::mt19937 &random) {
  ModelSelection selection;
  const auto handle_of = [&] {
    if (g.units.empty() || random() % 5 == 0)
      return std::string{"r999999"};
    return g.units[random() % g.units.size()].handle;
  };
  for (auto n = 1 + random() % 3; n > 0; --n) {
    ModelItem item;
    std::string text;
    switch (random() % 3) {
    case 0:
      item.kind = ModelItem::Kind::handle;
      item.handle = handle_of();
      text = item.handle;
      break;
    case 1: {
      item.kind = ModelItem::Kind::range;
      auto a = g.units.empty() ? 0 : g.units[random() % g.units.size()].number;
      auto b = g.units.empty() ? 0 : g.units[random() % g.units.size()].number;
      if (a > b)
        std::swap(a, b);
      item.first = a;
      item.last = b;
      text = "r" + std::to_string(a) + "-r" + std::to_string(b);
      break;
    }
    default: {
      std::vector<std::string> terms;
      if (random() % 3 == 0) {
        item.tool = random() % 2 ? "exec" : "read_file";
        terms.push_back("tool=" + *item.tool);
      }
      if (random() % 3 == 0) {
        constexpr std::array<const char *, 5> kinds{"instruction", "round", "result",
                                                    "state", "summary"};
        item.kind_name = kinds[random() % kinds.size()];
        terms.push_back("kind=" + *item.kind_name);
      }
      if (random() % 4 == 0) {
        item.superseded = true;
        terms.push_back("superseded");
      }
      if (random() % 4 == 0) {
        item.failed = true;
        terms.push_back("failed");
      }
      if (random() % 3 == 0 || terms.empty()) {
        item.before = g.units.empty() ? 1 : g.units[random() % g.units.size()].number;
        terms.push_back("before=r" + std::to_string(*item.before));
      }
      std::shuffle(terms.begin(), terms.end(), random);
      for (std::size_t i = 0; i < terms.size(); ++i)
        text += (i ? " " : "") + terms[i];
    }
    }
    selection.items.push_back(item);
    selection.text += (selection.text.empty() ? "" : ", ") + text;
  }
  return selection;
}

// ---- Model oracle for units() and expand(), and the protection property, over
// generated sessions; archiving each expansion through manage keeps them going.
void generated(const fs::path &base) {
  std::size_t expansions = 0, archived = 0, nonempty = 0, refusals = 0;
  for (unsigned seed = 0; seed < 8; ++seed) {
    Session session{base / ("generated-" + std::to_string(seed))};
    Generator g{session, std::mt19937{4242U + seed}, {}, {}, 0, 0};
    std::mt19937 random{99U + seed};
    for (unsigned step = 0; step < 90; ++step) {
      g.step();
      auto &context = g.context();
      const auto entries = entries_of(context);
      const auto units = context.units();
      check(listing(units, entries) == g.expected_listing(),
            "seed " + std::to_string(seed) + " step " + std::to_string(step) +
                ": units differ from the model\nstore:\n" + listing(units, entries) +
                "model:\n" + g.expected_listing());
      // The units partition the entries.
      std::vector<int> owner(entries.size(), -1);
      for (std::size_t u = 0; u < units.size(); ++u)
        for (const auto position : units[u].entries) {
          check(owner[position] == -1, "an entry in two units");
          owner[position] = static_cast<int>(u);
        }
      check(std::find(owner.begin(), owner.end(), -1) == owner.end(),
            "an entry in no unit");
      for (const auto purpose :
           {ExpandPurpose::archive, ExpandPurpose::pin, ExpandPurpose::unpin}) {
        const auto chosen = random_selection(g, random);
        const auto parsed = parse_selection(chosen.text);
        const auto *selection = std::get_if<UnitSelection>(&parsed);
        check(selection != nullptr, "generated selection parses: " + chosen.text);
        const auto expansion = expand(*selection, units, entries, purpose);
        const auto [expected, expected_refused] = model_expand(g, chosen, purpose);
        std::vector<std::pair<std::string, std::string>> got_refused;
        for (const auto &r : expansion.refused)
          got_refused.emplace_back(r.unit, r.reason);
        check(expansion.units == expected && got_refused == expected_refused,
              "expand differs from the model for '" + chosen.text + "'");
        ++expansions;
        refusals += got_refused.size();
        if (purpose != ExpandPurpose::archive || expansion.units.empty())
          continue;
        ++nonempty;
        // The property: nothing open, protected or pinned is ever expanded, except
        // a superseded instruction copy.
        Value::Array ids;
        std::set<std::string> gone;
        for (const auto u : expansion.units) {
          const auto &unit = units[u];
          check(!unit.open && !unit.pinned &&
                    (unit.unit_class != UnitClass::instruction || unit.superseded) &&
                    unit.unit_class != UnitClass::tool_state,
                "an expansion holds a protected unit: " + unit.handle);
          for (const auto position : unit.entries) {
            ids.push_back(field(entries[position], "id"));
            gone.insert(string_field(entries[position], "id"));
          }
        }
        // Outside a workflow a publication needs a complete tool protocol, so the
        // expansion is archived only when no call is open.
        if (g.open_calls.empty() && random() % 2 == 0) {
          const auto result = context.manage(archive(context, ids));
          check(outcome(result) == "accepted", "manage refuses an expansion of '" +
                                                   chosen.text +
                                                   "': " + outcome(result));
          g.archived(gone);
          ++archived;
          break; // the context changed; the units are stale
        }
      }
    }
    // Persistence of units and pins: restore and whole-history playback.
    const auto live = listing(g.context());
    unwrap(g.context().checkpoint());
    session.reopen();
    check(listing(*session.context) == live, "units after restore");
    check(probe_listing(session, true) == live, "units after whole-history playback");
  }
  check(expansions > 600 && archived > 40 && nonempty > 80 && refusals > 100,
        "generated coverage: " + std::to_string(expansions) + " expansions, " +
            std::to_string(nonempty) + " nonempty, " + std::to_string(archived) +
            " archived, " + std::to_string(refusals) + " refusals");
  std::cout << "generated: " << expansions << " expansions, " << nonempty
            << " nonempty, " << archived << " archived, " << refusals << " refusals\n";
}

// ---- Pins (design §5.4): latest-wins record, protection, settlement recheck.
void pins(const fs::path &base) {
  Session session{base / "pins"};
  auto &context = *session.context;
  context.append({message("note one", "developer")}, "workflow-continuation");
  const auto one = id_at(context, 0);
  context.append({message("note two", "developer")}, "workflow-continuation");
  const auto two = id_at(context, 0);
  context.append({message("note three", "developer")}, "workflow-continuation");
  const auto three = id_at(context, 0);
  const auto handle_of = [&](const std::string &id) {
    return unit_of(context.units(), entries_of(context), id).handle;
  };
  const auto h1 = handle_of(one), h2 = handle_of(two);
  const auto pinned = context.pin(h1 + ", r999999");
  check(field(pinned, "accepted").array().size() == 1 &&
            string_field(field(pinned, "refused").array()[0], "reason") == "absent",
        "pin accepts live units and refuses absent handles");
  check(outcome(context.manage(archive(context, {Value{one}}))) == "pinned-unit",
        "a pinned unit is not archived");
  check(context.units()[0].pinned, "the unit shows pinned");
  // A pin added after staging: the settlement keeps the unit and says so.
  context.begin_workflow();
  check(field(context.manage(archive(context, {Value{two}, Value{three}})), "staged") ==
            Value{true},
        "an archive stages in a workflow");
  (void)context.pin(h2);
  const auto settled = context.finish_workflow(true);
  check(field(settled, "accepted") == Value{true} && entries_of(context).size() == 2 &&
            id_at(context, 0) == two,
        "the unit pinned after staging stays; the rest is archived");
  const auto *refused = settled.find("refused");
  check(refused && refused->array().size() == 1 &&
            string_field(refused->array()[0], "unit") == h2 &&
            string_field(refused->array()[0], "reason") == "pinned",
        "the settlement lists the pinned unit as refused");
  // Every staged unit pinned since: nothing is left to archive.
  context.begin_workflow();
  (void)context.unpin(h2);
  check(field(context.manage(archive(context, {Value{two}})), "staged") == Value{true},
        "an unpinned unit stages");
  (void)context.pin(h2);
  check(outcome(context.finish_workflow(true)) == "pinned",
        "a settlement with every unit pinned is refused");
  const auto unpinned = context.unpin(h1 + ", " + h1);
  check(field(unpinned, "accepted").array().size() == 1, "unpin releases once");
  check(string_field(field(context.unpin(h1), "refused").array()[0], "reason") ==
            "not-pinned",
        "unpin of a unit not pinned is refused");
  // Pins live in current_programs: restore, older slot and whole history agree.
  const auto live = listing(context);
  unwrap(context.checkpoint());
  session.reopen();
  check(listing(*session.context) == live,
        "pins after restore:\n" + listing(*session.context) + "live:\n" + live);
  check(probe_listing(session, true) == live, "pins after whole-history playback");
  check(outcome(session.context->manage(archive(*session.context, {Value{two}}))) ==
            "pinned-unit",
        "a pin protects after reopen");
}

// ---- Persistence of classes (plan §6.1): a snapshot without the classes section
// (an S1 session) and a version-1 snapshot derive the same units; so does the
// older slot when the newer one is refused.
void older_sessions(const fs::path &base) {
  Session session{base / "older"};
  auto &context = *session.context;
  context.append({message("prompt", "user")}, "operator");
  context.append({reasoning("r"), call("o1")}, "provider.branch:x");
  context.append({output("o1")}, "lua.synthetic");
  context.append({message("brief", "assistant")}, "lua.synthetic",
                 UnitClass::instruction);
  context.append({message("beads", "user")}, "operator.beads");
  const auto live = listing(context);
  unwrap(context.checkpoint());
  const auto saved = session.root->saved_state()->context;
  // The section is written, and only for live records.
  bool has_classes = false;
  for (const auto &section : field(saved, "sections").array())
    has_classes |= string_field(section, "name") == "classes";
  check(has_classes, "the snapshot carries the classes section");
  // An S1 snapshot: no classes section.
  auto stripped = saved;
  for (auto &[key, value] : stripped.object())
    if (key == "sections")
      value = Value{Value::Array{}};
  unwrap(session.root->save_current_state(stripped));
  unwrap(session.root->save_current_state(stripped));
  session.reopen();
  check(session.context->open_report().playback == ContextOpenReport::Playback::suffix,
        "the S1 snapshot restores");
  check(listing(*session.context) == live,
        "units derived without a classes section:\n" + listing(*session.context));
  // A version-1 snapshot: entries and live originals inline.
  Value::Array originals;
  for (const auto &entry : entries_of(*session.context))
    for (const auto &original : session.context->originals().array())
      if (field(original, "id") == field(entry, "id"))
        originals.push_back(original);
  const auto v1 = Value::object({{"version", Value{Number{"1"}}},
                                 {"base", Value{session.context->head()}},
                                 {"entries", Value{entries_of(*session.context)}},
                                 {"live_originals", Value{std::move(originals)}}});
  unwrap(session.root->save_current_state(v1));
  unwrap(session.root->save_current_state(v1));
  session.reopen();
  check(session.root->compact_recovery(), "the v1 snapshot restores compactly");
  check(listing(*session.context) == live,
        "units derived from a v1 snapshot:\n" + listing(*session.context));
  // Older-slot fallback: a good snapshot and a suffix, then a refused newer slot.
  unwrap(session.context->checkpoint());
  session.context->append({message("suffix brief", "assistant")}, "lua.synthetic",
                          UnitClass::instruction);
  session.context->append({message("suffix note", "developer")},
                          "workflow-continuation");
  const auto with_suffix = listing(*session.context);
  auto refused = session.root->saved_state()->context;
  for (auto &[key, value] : refused.object())
    if (key == "version")
      value = Value{Number{"9"}};
  unwrap(session.root->save_current_state(refused));
  session.reopen();
  const auto &report = session.context->open_report();
  check(report.slot_refusals.size() == 1 &&
            report.playback == ContextOpenReport::Playback::suffix,
        "the older slot is restored and its suffix played back");
  check(listing(*session.context) == with_suffix, "units after older-slot fallback");
  check(probe_listing(session, true) == with_suffix,
        "units after whole-history playback");
}

// ---- The RRC check's lookup (design §19): whether a live entry's creating record
// carries a label, read by identity lookup, so a compact reopen whose tail no
// longer holds the record still finds it; a record with no live entry does not count.
void live_origins(const fs::path &base) {
  Session session{base / "origin"};
  auto &context = *session.context;
  context.append({message("first", "user")}, "operator");
  context.append({message("continue", "user")}, "rrc:token-a");
  const auto note = id_at(context, 0);
  context.append({message("later", "assistant")}, "provider.branch:x");
  check(context.live_origin("rrc:token-a") && !context.live_origin("rrc:token-b") &&
            !context.live_origin("rrc:token"),
        "the live note's label is found, and only that label");
  unwrap(context.checkpoint());
  session.reopen();
  check(session.root->compact_recovery(), "the fixture reopens compactly");
  for (const auto &fact : session.root->tail_facts())
    if (const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body))
      check(hex_identity(record->identity.bytes()) != revision_of(note),
            "the compact tail no longer holds the note's record");
  check(session.context->live_origin("rrc:token-a"),
        "the note is found by identity lookup after a compact reopen");
  auto view = session.context->view();
  std::erase_if(view.object()[1].second.array(),
                [&](const Value &e) { return string_field(e, "id") == note; });
  check(field(session.context->edit(view), "accepted") == Value{true}, "edit");
  check(!session.context->live_origin("rrc:token-a"),
        "a record with no live entry does not count");
}

// ---- Layer 1 (A-M2): a lua.synthetic record written before S2 carries no class and
// is classed by role, so the pre-S2 ultracode brief (a developer message) stays an
// instruction; an S2 append records its class, so its default result is archivable.
void pre_s2_lua(const fs::path &base) {
  Session session{base / "pre-s2-lua"};
  auto &context = *session.context;
  context.append({message("first", "user")}, "operator");
  // The append a88c1c2 recorded for blackbird.append({{role='developer',...}}).
  const auto identity = session.log->issue();
  const auto revision = hex_identity(identity.bytes());
  (void)session.log->record(
      identity, ApplicationChannel::context,
      Value::object(
          {{"op", Value{"append"}},
           {"base", Value{context.head()}},
           {"observed", Value{context.head()}},
           {"revision", Value{revision}},
           {"accepted", Value{true}},
           {"origin", Value{"lua.synthetic"}},
           {"format", Value{"append-delta-v1"}},
           {"originals",
            Value{Value::Array{Value::object(
                {{"id", Value{revision + ".0"}},
                 {"item", message("Ultracode: old brief", "developer")}})}}}}));
  session.reopen();
  auto &reopened = *session.context;
  check(outcome(reopened.manage(archive(reopened, {Value{revision + ".0"}}))) ==
            "protected-unit",
        "a pre-S2 lua.synthetic developer brief stays protected");
  reopened.append({message("program note", "developer")}, "lua.synthetic");
  const auto note = id_at(reopened, 0);
  check(outcome(reopened.manage(archive(reopened, {Value{note}}))) == "accepted",
        "an S2 lua.synthetic default append is a result");
  bool recorded = false;
  for (const auto &fact : session.root->tail_facts())
    if (const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
        record && hex_identity(record->identity.bytes()) == revision_of(note)) {
      const auto packet = unwrap(read_packet(record->payload));
      recorded = packet.find("class") &&
                 *packet.find("class") ==
                     Value{Number{static_cast<std::uint8_t>(UnitClass::result)}};
    }
  check(recorded, "the S2 append records its class");
}

// ---- Layer 1 (B-L3): a /repair copy of the restart note counts as the note, so a
// `continue` after the repair does not append a second one.
void repaired_note(const fs::path &base) {
  Session session{base / "repaired-note"};
  auto &context = *session.context;
  context.append({message("first", "user")}, "operator");
  const auto good = context.head();
  // A frame that does not apply, then the note chained after it.
  const auto bad_identity = session.log->issue();
  CutFrame bad;
  bad.revision = hex_identity(bad_identity.bytes());
  bad.base = bad.observed = good;
  bad.originals = {{message("never applies", "assistant"), 0}};
  bad.insert = {{std::nullopt, std::uint32_t{0}}};
  bad.result = CutResult{99, 0, 0};
  (void)session.log->record(bad_identity, ApplicationChannel::context,
                            encode_cut_frame(bad));
  const auto note_identity = session.log->issue();
  const auto note = hex_identity(note_identity.bytes());
  (void)session.log->record(
      note_identity, ApplicationChannel::context,
      Value::object(
          {{"op", Value{"append"}},
           {"base", Value{bad.revision}},
           {"observed", Value{bad.revision}},
           {"revision", Value{note}},
           {"accepted", Value{true}},
           {"origin", Value{"rrc:token-r"}},
           {"format", Value{"append-delta-v1"}},
           {"originals", Value{Value::Array{Value::object(
                             {{"id", Value{note + ".0"}},
                              {"item", message("continue note", "user")}})}}}}));
  session.reopen();
  check(session.context->read_only(), "the bad frame opens read-only");
  (void)session.context->repair(Value::Array{Value{note + ".0"}});
  check(session.context->live_origin("rrc:token-r") &&
            !session.context->live_origin("rrc:token-s"),
        "a repair copy of the note counts as the note");
}

// ---- Layer 2 (B-N1): the restart note's own frame is a skipped frame whose
// originals do not parse. After /repair copies another skipped original, the check
// for the note reads that frame and finds no match instead of throwing.
void malformed_note(const fs::path &base) {
  Session session{base / "malformed-note"};
  auto &context = *session.context;
  context.append({message("first", "user")}, "operator");
  const auto bad_identity = session.log->issue();
  CutFrame bad;
  bad.revision = hex_identity(bad_identity.bytes());
  bad.base = bad.observed = context.head();
  bad.originals = {{message("never applies", "assistant"), 0}};
  bad.insert = {{std::nullopt, std::uint32_t{0}}};
  bad.result = CutResult{99, 0, 0};
  (void)session.log->record(bad_identity, ApplicationChannel::context,
                            encode_cut_frame(bad));
  const auto note_identity = session.log->issue();
  const auto note = hex_identity(note_identity.bytes());
  (void)session.log->record(note_identity, ApplicationChannel::context,
                            Value::object({{"op", Value{"append"}},
                                           {"base", Value{bad.revision}},
                                           {"observed", Value{bad.revision}},
                                           {"revision", Value{note}},
                                           {"accepted", Value{true}},
                                           {"origin", Value{"rrc:token-m"}},
                                           {"format", Value{"append-delta-v1"}},
                                           {"originals", Value{"not a list"}}}));
  const auto later_identity = session.log->issue();
  const auto later = hex_identity(later_identity.bytes());
  (void)session.log->record(
      later_identity, ApplicationChannel::context,
      Value::object({{"op", Value{"append"}},
                     {"base", Value{note}},
                     {"observed", Value{note}},
                     {"revision", Value{later}},
                     {"accepted", Value{true}},
                     {"origin", Value{"fixture"}},
                     {"format", Value{"append-delta-v1"}},
                     {"originals", Value{Value::Array{Value::object(
                                       {{"id", Value{later + ".0"}},
                                        {"item", message("kept", "assistant")}})}}}}));
  session.reopen();
  check(session.context->read_only(), "the bad frame opens read-only");
  (void)session.context->repair(Value::Array{Value{later + ".0"}});
  bool found = true;
  try {
    found = session.context->live_origin("rrc:token-m");
  } catch (const Error &error) {
    throw std::runtime_error(std::string{"the note check throws: "} +
                             std::string{error_name(error.code)});
  } catch (const std::bad_variant_access &) {
    throw std::runtime_error("the note check throws: bad_variant_access");
  }
  check(!found, "a malformed skipped note frame does not match");
}
#endif

void run(std::string_view name, const std::function<void()> &body) {
  try {
    body();
    std::cout << "ok   " << name << '\n';
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

int main() {
  const auto base = scratch();
  run("repaired-round", [&] { repaired_round(base); });
  run("beads-result", [&] { beads_result(base); });
#if !defined(S2_RED_OLD_CODE)
  run("label-classes", [&] { label_classes(base); });
  run("spoofing", [&] { spoofing(base); });
  run("handles", [&] { handles(base); });
  run("superseded", [&] { superseded(base); });
  run("grammar", [] { grammar(); });
  run("generated", [&] { generated(base); });
  run("pins", [&] { pins(base); });
  run("older-sessions", [&] { older_sessions(base); });
  run("live-origins", [&] { live_origins(base); });
  run("pre-s2-lua", [&] { pre_s2_lua(base); });
  run("repaired-note", [&] { repaired_note(base); });
  run("malformed-note", [&] { malformed_note(base); });
#endif
  fs::remove_all(base);
  return failures == 0 ? 0 : 1;
}
