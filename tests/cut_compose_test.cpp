// Cut composition (unit S3a, CONTEXT_LIFECYCLE.md §5.5, §5.6, §6, §7.1, §7.3).
// Oracles:
// - model (removal): over generated sessions on every shipped profile row (and
//   synthetic variants of their placement, boundary and reasoning fields), the
//   entries the frame leaves equal, in order, every entry minus the chosen units,
//   minus the entries §5.6 sweeps at or after p, minus the reasoning §6.2 drops,
//   plus the notes, summaries, inserts, restored originals and boundary at their
//   anchors. The expectation, p and every refusal come from a reference written
//   here, independently of the composers;
// - property (prefix): the request exported through the real adapter before and
//   after a cut is byte-identical before p (system/instructions, tools, and every
//   rendered block from an entry before p);
// - adapter export: every composed result exports through export_anthropic_request,
//   export_provider_request and export_registered_responses_request without error,
//   pairing holds, and the reasoning the row keeps is what reaches the wire;
// - specification (§6.2): kept and dropped reasoning per at_cut on hand contexts,
//   inserted and re-inserted entries counting after p; rebind and prefix rules;
// - specification (§7.1): turn state on rendered messages;
// - persistence: composed frames applied through ContextStore::cut survive
//   snapshot restore and whole-history playback identically, chapters and units
//   (with unit keys) included.
#include "blackbird/anthropic_provider.hpp"
#include "blackbird/context.hpp"
#include "blackbird/context_compose.hpp"
#include "blackbird/journal.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/provider_profile.hpp"
#include "blackbird/responses_provider.hpp"
#include "blackbird/saved_context.hpp"
#include "blackbird/saved_state.hpp"
#include "blackbird/turn_state.hpp"
#include <algorithm>
#include <chrono>
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
void check(bool condition, const std::string &claim) {
  if (!condition)
    throw std::runtime_error(claim);
}
std::string text_at(const Value &v, std::initializer_list<std::string_view> path) {
  const Value *at = &v;
  for (const auto key : path) {
    at = at->find(key);
    check(at != nullptr, "profile field missing: " + std::string{key});
  }
  return at->string();
}
// A copy of a profile with one field set (path of object keys).
Value with(const Value &profile, std::initializer_list<std::string_view> path,
           const Value &value) {
  Value out = profile;
  Value *at = &out;
  const auto keys = std::vector<std::string_view>{path};
  for (std::size_t i = 0; i < keys.size(); ++i) {
    auto &fields = at->object();
    auto found = std::find_if(fields.begin(), fields.end(),
                              [&](const auto &f) { return f.first == keys[i]; });
    if (found == fields.end()) {
      fields.emplace_back(std::string{keys[i]},
                          i + 1 == keys.size() ? Value{} : Value::object({}));
      found = std::prev(fields.end());
    }
    if (i + 1 == keys.size())
      found->second = value;
    at = &found->second;
  }
  return out;
}
std::string dump(const Value &value) { return unwrap(dump_json(value)); }
const std::string *text_member(const Value &v, std::string_view key) {
  const auto *f = v.find(key);
  return f ? std::get_if<std::string>(&f->value()) : nullptr;
}
bool type_is(const Value &item, std::string_view type) {
  const auto *t = text_member(item, "type");
  return t && *t == type;
}
// The text of a message item: a string or the concatenated text parts.
std::string item_text(const Value &item) {
  const auto *content = item.find("content");
  if (!content)
    return {};
  if (const auto *s = std::get_if<std::string>(&content->value()))
    return *s;
  std::string out;
  if (const auto *parts = std::get_if<Value::Array>(&content->value()))
    for (const auto &part : *parts)
      if (const auto *t = text_member(part, "text"))
        out += *t;
  return out;
}

// ---- Families of provider-native items.
enum class Family : std::uint8_t { anthropic, responses, portable };
std::string family_name(Family f) {
  return f == Family::anthropic   ? "anthropic"
         : f == Family::responses ? "responses"
                                  : "portable";
}

// ---- A pure context: entries with ref IDs and the classes of their records.
struct Fixture {
  Family family = Family::anthropic;
  Value::Array entries;
  ClassMap classes;
  std::set<ContextRef> pins;
  std::uint64_t counter = 1000;
  unsigned calls = 0, groups = 0, texts = 0;
  std::map<std::string, Value> originals; // every entry ever created, by ID
  IdentityBytes next_record() {
    IdentityBytes bytes{};
    bytes[0] = std::byte{0x42};
    ++counter;
    for (unsigned i = 0; i < 8; ++i)
      bytes[8 + i] = static_cast<std::byte>((counter >> (8 * i)) & 255U);
    return bytes;
  }
  ContextRef add(std::vector<Value> items, std::vector<UnitClass> cls) {
    const auto record = next_record();
    Value::Array array{items.begin(), items.end()};
    classes.set(record, record_classes(std::move(cls), array));
    for (std::uint32_t i = 0; i < items.size(); ++i) {
      const auto id = entry_id({record, i});
      entries.push_back(Value::object({{"id", Value{id}}, {"item", items[i]}}));
      originals.emplace(id, items[i]);
    }
    return {record, 0};
  }
  ClassResolver resolver() const {
    return [this](const IdentityBytes &record) -> const RecordClasses & {
      const auto *found = classes.find(record);
      if (!found)
        throw std::runtime_error("fixture record without classes");
      return *found;
    };
  }
  std::vector<Unit> live_units() const { return units(entries, resolver(), pins); }
  std::string text() { return "t" + std::to_string(texts++); }
  // Items.
  Value prompt() {
    return Value::object({{"role", Value{"user"}}, {"content", Value{text()}}});
  }
  Value developer(std::string body) {
    return Value::object(
        {{"role", Value{"developer"}}, {"content", Value{std::move(body)}}});
  }
  std::string group() { return "msg_" + std::to_string(groups); }
  Value reasoning() {
    if (family == Family::anthropic)
      return Value::object({{"type", Value{"reasoning"}},
                            {"anthropic_message_id", Value{group()}},
                            {"anthropic_content",
                             Value::object({{"type", Value{"thinking"}},
                                            {"thinking", Value{"thought " + text()}},
                                            {"signature", Value{"sig-" + text()}}})}});
    if (family == Family::responses)
      return Value::object({{"type", Value{"reasoning"}},
                            {"encrypted_content", Value{"enc-" + text()}},
                            {"summary", Value{Value::Array{}}}});
    return Value::object(
        {{"type", Value{"reasoning"}}, {"summary", Value{Value::Array{}}}});
  }
  Value answer(std::string body) {
    auto item = Value::object(
        {{"type", Value{"message"}},
         {"role", Value{"assistant"}},
         {"content",
          Value{Value::Array{Value::object(
              {{"type", Value{"output_text"}}, {"text", Value{std::move(body)}}})}}}});
    if (family == Family::anthropic)
      item.object().emplace_back("anthropic_message_id", Value{group()});
    return item;
  }
  Value call(const std::string &name, std::string &id) {
    id = "call_" + std::to_string(calls++);
    auto item = Value::object(
        {{"type", Value{"function_call"}},
         {"call_id", Value{id}},
         {"name", Value{name}},
         {"arguments", Value::object({{"cmd", Value{"run " + text()}}})}});
    if (family == Family::anthropic)
      item.object().emplace_back("anthropic_message_id", Value{group()});
    return item;
  }
  Value output(const std::string &id, bool failed = false) {
    if (failed)
      return Value::object(
          {{"type", Value{"function_call_output"}},
           {"call_id", Value{id}},
           {"output", Value::object({{"exit_code", Value{Number{std::uint64_t{2}}}},
                                     {"stdout", Value{text()}}})}});
    return Value::object({{"type", Value{"function_call_output"}},
                          {"call_id", Value{id}},
                          {"output", Value{"out " + text()}}});
  }
  // Records.
  void add_prompt() { add({prompt()}, {UnitClass::instruction}); }
  void add_state() {
    add({developer("CONTEXT_SIZE input_bytes=" + text())}, {UnitClass::state});
  }
  // A provider round with reasoning, text and calls; its outputs follow, each its own
  // record, unless open (the last call stays unanswered).
  std::vector<std::string> add_round(bool with_reasoning, bool with_text,
                                     const std::vector<std::string> &names,
                                     bool open = false, bool failed = false) {
    ++groups;
    std::vector<Value> items;
    if (with_reasoning)
      items.push_back(reasoning());
    if (with_text || names.empty())
      items.push_back(answer("says " + text()));
    std::vector<std::string> ids;
    for (const auto &name : names) {
      std::string id;
      items.push_back(call(name, id));
      ids.push_back(id);
    }
    add(items, {UnitClass::round});
    for (std::size_t i = 0; i < ids.size(); ++i)
      if (!(open && i + 1 == ids.size()))
        add({output(ids[i], failed)}, {UnitClass::result});
    return ids;
  }
  Value request(const Value::Array &list) const {
    const std::string tools = "t1";
    Value::Array input;
    input.reserve(list.size());
    for (const auto &e : list)
      input.push_back(*e.find("item"));
    const auto tool = [&](const std::string &name) {
      return Value::object(
          {{"type", Value{"function"}},
           {"name", Value{name}},
           {"description", Value{"tool " + name + " " + tools}},
           {"parameters", Value::object({{"type", Value{"object"}},
                                         {"properties", Value::object({})}})}});
    };
    return Value::object(
        {{"model", Value{family == Family::anthropic ? "claude-opus-5-5" : "gpt-6"}},
         {"instructions", Value{"be careful"}},
         {"reasoning", Value::object({{"effort", Value{"high"}}})},
         {"input", Value{std::move(input)}},
         {"tools",
          Value{Value::Array{tool("exec"), tool("read_file"), tool("context_view")}}},
         {"parallel_tool_calls", Value{false}},
         {"include", Value{Value::Array{Value{"reasoning.encrypted_content"}}}},
         {"store", Value{false}},
         {"stream", Value{true}}});
  }
};

// The provider-visible export of entries and its rendered positions, per family.
struct Wire {
  Value exported;
  std::vector<RenderedMessage> rendered; // Anthropic messages, or one per item
};
Wire export_wire(const Fixture &fixture, const Value::Array &entries) {
  const auto request = fixture.request(entries);
  Wire wire;
  if (fixture.family == Family::anthropic) {
    wire.exported = export_anthropic_request(request);
    wire.rendered = anthropic_render(field(request, "input").array());
  } else {
    const bool portable = fixture.family == Family::portable;
    wire.exported = export_registered_responses_request(request, portable);
    if (!portable)
      (void)export_provider_request(request); // the built-in OpenAI/OpenRouter path
    wire.rendered = responses_render(field(request, "input").array(), portable);
  }
  return wire;
}
std::size_t wire_reasoning(const Fixture &fixture, const Wire &wire) {
  std::size_t count = 0;
  if (fixture.family == Family::anthropic) {
    for (const auto &message : field(wire.exported, "messages").array())
      for (const auto &block : field(message, "content").array())
        if (type_is(block, "thinking") || type_is(block, "redacted_thinking"))
          ++count;
  } else
    for (const auto &item : field(wire.exported, "input").array())
      if (type_is(item, "reasoning"))
        ++count;
  return count;
}
// Prefix property: every provider-visible byte before p is identical.
void same_prefix(const Fixture &fixture, const Wire &before, const Wire &after,
                 std::size_t p, const std::string &what) {
  if (fixture.family == Family::anthropic) {
    for (const auto key : {"model", "system", "tools"})
      check(dump(field(before.exported, key)) == dump(field(after.exported, key)),
            what + ": " + key + " changed");
    const auto &b = field(before.exported, "messages").array();
    const auto &a = field(after.exported, "messages").array();
    for (std::size_t m = 0; m < before.rendered.size(); ++m) {
      const auto &positions = before.rendered[m].positions;
      std::size_t blocks = 0;
      while (blocks < positions.size() && positions[blocks] < p)
        ++blocks;
      if (blocks == 0)
        return;
      check(m < a.size() && field(a[m], "role") == field(b[m], "role"),
            what + ": message " + std::to_string(m) + " before p changed role");
      const auto &bb = field(b[m], "content").array();
      const auto &ab = field(a[m], "content").array();
      check(ab.size() >= blocks, what + ": blocks before p lost");
      for (std::size_t i = 0; i < blocks; ++i)
        check(dump(bb[i]) == dump(ab[i]),
              what + ": block before p differs in message " + std::to_string(m));
      if (blocks < positions.size())
        return;
    }
    return;
  }
  for (const auto key : {"model", "instructions", "tools"})
    check(dump(field(before.exported, key)) == dump(field(after.exported, key)),
          what + ": " + key + " changed");
  const auto &b = field(before.exported, "input").array();
  const auto &a = field(after.exported, "input").array();
  for (std::size_t i = 0; i < std::min(p, b.size()); ++i)
    check(i < a.size() && dump(b[i]) == dump(a[i]),
          what + ": input item " + std::to_string(i) + " before p differs");
}

void export_items(Family family, const Value::Array &items);
std::string shape_text(const Value::Array &entries);
// ---- The reference (§5.5, §5.6, §6.2), written over position lists.
bool is_note(const Value &item, std::string_view marker) {
  return item_text(item).starts_with(marker);
}
bool assistant_side(const Value &item) {
  const auto *role = text_member(item, "role");
  return (role && *role == "assistant") || type_is(item, "function_call") ||
         type_is(item, "reasoning");
}
bool user_entry(const Value &item) {
  const auto *role = text_member(item, "role");
  return role && *role == "user";
}
// What the reference expects a cut to write. Rows: an archive note's rows by handle,
// the IDs each lists (or, for an absorbed row, the row text itself).
struct Expectation {
  std::optional<std::string> refusal;
  std::size_t p = 0;
  std::vector<std::string> tokens; // ids and new tokens, in order
  std::set<ContextRef> archived, swept, reasoning;
  std::map<std::string, std::vector<std::string>> note_rows; // handle -> entry IDs
  std::vector<std::string> absorbed_rows;
  // Replacements: an edit's item, or a rewritten note's remaining rows.
  std::map<ContextRef, Value> replaced_items;
  std::map<ContextRef, std::vector<std::string>> rewritten_notes;
  std::size_t reasoning_left = 0;
};
const Value &item_at(const Value::Array &entries, std::size_t i) {
  return *entries[i].find("item");
}
ContextRef ref_at(const Value::Array &entries, std::size_t i) {
  const auto *id = text_member(entries[i], "id");
  const auto ref = id ? parse_entry_id(*id) : std::nullopt;
  check(ref.has_value(), "a fixture entry without a ref ID");
  return ref.value_or(ContextRef{});
}
std::string id_at(const Value::Array &entries, std::size_t i) {
  return *text_member(entries[i], "id");
}
// The survivors as a list of positions; every placement rule below reads it.
std::vector<std::size_t> survivors(const std::vector<bool> &gone) {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < gone.size(); ++i)
    if (!gone[i])
      out.push_back(i);
  return out;
}
// Whether a placement right after the k-th survivor (k = -1: the head) would split a
// call from its output, or follow a call still waiting for one.
bool splits_group(const Value::Array &entries, const std::vector<std::size_t> &alive,
                  long k) {
  std::set<std::string> before, after;
  for (long j = 0; j < static_cast<long>(alive.size()); ++j) {
    const auto &item = item_at(entries, alive[static_cast<std::size_t>(j)]);
    const auto *id = text_member(item, "call_id");
    if (!id)
      continue;
    if (j <= k && type_is(item, "function_call"))
      before.insert(*id);
    if (j <= k && type_is(item, "function_call_output"))
      before.erase(*id);
    if (j > k && type_is(item, "function_call_output"))
      after.insert(*id);
  }
  // A call before the place is open there when its output comes after, or never.
  return !before.empty();
}
// Design §5.5 placement over the survivors; returns the survivor index the note goes
// after (-1: head), or empty when it cannot be placed.
std::optional<long> ref_place(const Value::Array &entries,
                              const std::vector<std::size_t> &alive, std::size_t natural,
                              std::string_view placement) {
  long k = -1;
  while (k + 1 < static_cast<long>(alive.size()) &&
         alive[static_cast<std::size_t>(k + 1)] < natural)
    ++k;
  const auto last = static_cast<long>(alive.size()) - 1;
  const auto at = [&](long j) -> const Value & {
    return item_at(entries, alive[static_cast<std::size_t>(j)]);
  };
  // Never between a call and its output: after the outputs of any open call.
  const auto out_of_group = [&] {
    while (k < last && splits_group(entries, alive, k))
      ++k;
  };
  out_of_group();
  if (placement == "after_user_turn_before_assistant") {
    while (k < last && !assistant_side(at(k + 1)))
      ++k;
    out_of_group();
  } else if (placement == "before_next_user_entry") {
    while (true) {
      while (k < last && !user_entry(at(k + 1)))
        ++k;
      if (k < 0 || !type_is(at(k), "function_call_output"))
        break;
      if (k == last)
        return std::nullopt; // no user entry left to go after
      ++k;                   // after that user entry
    }
    out_of_group();
  }
  return k;
}

struct RestoreSource {
  std::string handle;
  std::vector<Value> items;
  std::vector<UnitClass> classes;
  bool differs = false;
};

// The reference, from the design's text (§3 p, §4.3 anchors, §5.5 notes and restore,
// §5.6 sweeps, §6.2 reasoning, §7.3 boundary). Choices the design leaves to the
// writer and both sides must make the same way are marked "shared:".
Expectation reference(const Fixture &fixture, const std::vector<Unit> &units,
                      const Value &profile, const CutRequest &request,
                      const std::vector<RestoreSource> &restores) {
  Expectation out;
  const auto &entries = fixture.entries;
  const auto n = entries.size();
  const auto at_cut = text_at(profile, {"reasoning", "at_cut"});
  const auto placement = text_at(profile, {"note", "placement"});
  const bool refused_mode =
      text_at(profile, {"archival", "mode"}) == "refused_until_probed";
  const bool retaining =
      text_at(profile, {"request_state", "placement"}) == "retained_in_place";
  const bool drop = at_cut == "drop_after_change";
  const auto refuse = [&](std::string reason) {
    out.refusal = std::move(reason);
    return out;
  };
  std::map<std::size_t, std::size_t> unit_of; // position -> unit index
  for (std::size_t u = 0; u < units.size(); ++u)
    for (const auto pos : units[u].entries)
      unit_of[pos] = u;
  const auto first_of = [&](std::size_t u) { return units[u].entries.front(); };
  const auto last_of = [&](std::size_t u) { return units[u].entries.back(); };
  const auto class_at = [&](std::size_t pos) { return units[unit_of[pos]].unit_class; };
  // §5.2: the newest boundary is never selectable.
  std::optional<std::size_t> newest;
  for (std::size_t i = 0; i < n; ++i)
    if (class_at(i) == UnitClass::note && is_note(item_at(entries, i), boundary_marker))
      newest = i;
  if (request.archive.empty() && request.summaries.empty() && restores.empty() &&
      request.edits.empty() && !request.boundary)
    return refuse("empty");
  // §6.2: refused_until_probed refuses every kind that removes or replaces.
  const bool removing =
      !request.archive.empty() || !request.summaries.empty() ||
      std::any_of(request.edits.begin(), request.edits.end(),
                  [](const CutEdit &e) { return e.op != CutEdit::Op::insert; });
  if (refused_mode && (removing || request.kind == CutKind::recovery ||
                       std::any_of(restores.begin(), restores.end(),
                                   [](const auto &r) { return r.differs; })))
    return refuse("archival-refused");
  if (request.boundary &&
      !std::get<bool>(profile.find("turn")->find("boundary")->find("enabled")->value()))
    return refuse("boundary-disabled");
  // Under drop_after_change a reasoning-only unit would come back as nothing.
  for (const auto &r : restores)
    if (drop && std::all_of(r.items.begin(), r.items.end(), [](const Value &item) {
          return type_is(item, "reasoning");
        }))
      return refuse("restore-reasoning-only");
  std::vector<bool> gone(n, false);
  std::vector<std::size_t> chosen, moved; // shared: refusal order archive, summary, edits
  std::size_t p = n;
  const auto take = [&](std::size_t u) -> std::optional<std::string> {
    const auto &unit = units[u];
    if (unit.open)
      return "open";
    if (unit.ambiguous)
      return "ambiguous";
    if (request.protect && protected_unit(unit))
      return "protected";
    if (newest && std::find(unit.entries.begin(), unit.entries.end(), *newest) !=
                      unit.entries.end())
      return "protected";
    for (const auto pos : unit.entries) {
      if (gone[pos])
        return "duplicate-unit";
      gone[pos] = true;
    }
    p = std::min(p, first_of(u));
    return std::nullopt;
  };
  for (const auto u : request.archive) {
    if (auto why = take(u))
      return refuse(*why);
    chosen.push_back(u);
  }
  for (const auto &s : request.summaries)
    if (auto why = take(s.unit))
      return refuse(*why);
  for (const auto &edit : request.edits)
    if (edit.op == CutEdit::Op::remove || edit.op == CutEdit::Op::move) {
      if (auto why = take(edit.unit))
        return refuse(*why);
      (edit.op == CutEdit::Op::remove ? chosen : moved).push_back(edit.unit);
    }
  std::map<std::size_t, Value> replaced;
  for (const auto &edit : request.edits) {
    if (edit.op == CutEdit::Op::replace) {
      std::size_t pos = n;
      for (std::size_t i = 0; i < n; ++i)
        if (ref_at(entries, i) == edit.entry)
          pos = i;
      if (pos == n || gone[pos])
        return refuse("absent");
      if (request.protect && protected_unit(units[unit_of[pos]]))
        return refuse("protected");
      if (type_is(item_at(entries, pos), "reasoning"))
        return refuse("reasoning-edit");
      if (replaced.contains(pos))
        return refuse("duplicate-entry");
      replaced.emplace(pos, edit.item);
      out.replaced_items.emplace(ref_at(entries, pos), edit.item);
      p = std::min(p, pos);
    }
    if (edit.op == CutEdit::Op::insert || edit.op == CutEdit::Op::move)
      p = std::min(p, edit.after ? last_of(*edit.after) + 1 : 0);
  }
  // §5.5: a restore rewrites the live note whose rows list it; p moves to the note.
  const auto rows_of = [&](std::size_t pos) {
    std::vector<std::string> rows;
    const auto text = item_text(item_at(entries, pos));
    std::size_t start = 0, line = 0;
    while (start <= text.size()) {
      const auto end = text.find('\n', start);
      const auto piece =
          text.substr(start, end == std::string::npos ? std::string::npos : end - start);
      if (line++ >= 2 && !piece.empty())
        rows.push_back(piece);
      if (end == std::string::npos)
        break;
      start = end + 1;
    }
    return rows;
  };
  const auto row_handle = [](const std::string &row) { return row.substr(0, row.find(' ')); };
  const auto archive_note_at = [&](std::size_t i) {
    return class_at(i) == UnitClass::note && is_note(item_at(entries, i), archive_note_marker);
  };
  std::vector<std::optional<std::size_t>> restore_note;
  for (const auto &r : restores) {
    std::optional<std::size_t> note;
    for (std::size_t i = 0; i < n && !note; ++i)
      if (!gone[i] && archive_note_at(i))
        for (const auto &row : rows_of(i))
          if (row_handle(row) == r.handle)
            note = i;
    restore_note.push_back(note);
    p = std::min(p, note ? *note : n);
  }
  out.p = p;
  // §5.6 sweeps: at or after p, whole units. shared: a unit the frame replaces stays.
  const bool archival =
      request.kind == CutKind::archival || request.kind == CutKind::recovery;
  const bool writes_note = !chosen.empty();
  std::vector<std::size_t> absorbed;
  if (archival)
    for (std::size_t u = 0; u < units.size(); ++u) {
      const auto &unit = units[u];
      if (unit.pinned || unit.open || first_of(u) < p ||
          std::any_of(unit.entries.begin(), unit.entries.end(),
                      [&](std::size_t pos) { return gone[pos] || replaced.contains(pos); }))
        continue;
      bool sweep = false;
      const auto &first = item_at(entries, first_of(u));
      if (unit.unit_class == UnitClass::state)
        sweep = true;
      else if (unit.unit_class == UnitClass::note && is_note(first, archive_note_marker))
        sweep = writes_note;
      else if (unit.unit_class == UnitClass::note && is_note(first, boundary_marker))
        sweep = request.boundary || first_of(u) != newest;
      else if (unit.unit_class == UnitClass::round && retaining) {
        bool any = false, only = true;
        for (const auto pos : unit.entries) {
          const auto &item = item_at(entries, pos);
          if (!type_is(item, "function_call"))
            continue;
          any = true;
          const auto &name = *text_member(item, "name");
          const auto *args = item.find("arguments");
          only = only && (name == "context_view" || name == "context_stats" ||
                          (name == "context_budget" && !(args && args->find("proposal"))));
        }
        sweep = any && only;
      }
      if (!sweep)
        continue;
      if (unit.unit_class == UnitClass::note && is_note(first, archive_note_marker))
        absorbed.push_back(first_of(u));
      for (const auto pos : unit.entries) {
        gone[pos] = true;
        out.swept.insert(ref_at(entries, pos));
      }
    }
  std::set<std::string> restored_handles;
  for (const auto &r : restores)
    restored_handles.insert(r.handle);
  for (std::size_t k = 0; k < restores.size(); ++k) {
    const auto note = restore_note[k];
    // shared: a note the same frame's edit replaces keeps the edit.
    if (!note || gone[*note] || replaced.contains(*note) ||
        out.rewritten_notes.contains(ref_at(entries, *note)))
      continue;
    std::vector<std::string> rows;
    for (const auto &row : rows_of(*note))
      if (!restored_handles.contains(row_handle(row)))
        rows.push_back(row);
    if (rows.empty()) {
      gone[*note] = true;
      out.swept.insert(ref_at(entries, *note));
    } else
      out.rewritten_notes.emplace(ref_at(entries, *note), rows);
  }
  // §6.2: drop_after_change drops reasoning at or after p, moved and restored included.
  for (const auto u : chosen)
    for (const auto pos : units[u].entries)
      out.archived.insert(ref_at(entries, pos));
  for (const auto &s : request.summaries)
    for (const auto pos : units[s.unit].entries)
      out.archived.insert(ref_at(entries, pos));
  std::set<std::size_t> moved_drop;
  for (const auto u : moved)
    for (const auto pos : units[u].entries)
      if (drop && type_is(item_at(entries, pos), "reasoning")) {
        out.reasoning.insert(ref_at(entries, pos));
        moved_drop.insert(pos);
      } else
        out.archived.insert(ref_at(entries, pos));
  if (drop)
    for (std::size_t i = p; i < n; ++i)
      if (!gone[i] && type_is(item_at(entries, i), "reasoning")) {
        gone[i] = true;
        out.reasoning.insert(ref_at(entries, i));
      }
  const auto alive = survivors(gone);
  // The survivor index at or before an input position (§4.3 step 4); -1: head.
  const auto at_or_before = [&](std::optional<std::size_t> pos) -> long {
    if (!pos)
      return -1;
    long k = -1;
    while (k + 1 < static_cast<long>(alive.size()) &&
           alive[static_cast<std::size_t>(k + 1)] <= *pos)
      ++k;
    return k;
  };
  // New entries, each placed after a survivor (-1: head). shared: on one survivor
  // they chain by the position they stand for, then restores, summaries, edits, note.
  struct Placed {
    long after;
    std::pair<std::size_t, int> key;
    std::string token;
  };
  std::vector<Placed> placed;
  for (std::size_t k = 0; k < restores.size(); ++k) {
    // §5.5/§5.7: back in place of the note that lists it: after the note if it
    // stays, where it was if it leaves; without a live note, at the tail.
    const auto note = restore_note[k];
    const long after = note ? at_or_before(*note) : static_cast<long>(alive.size()) - 1;
    for (std::size_t j = 0; j < restores[k].items.size(); ++j)
      if (!(drop && type_is(restores[k].items[j], "reasoning")))
        placed.push_back({after,
                          {note ? *note + 1 : n, 0},
                          "new:restore:" + restores[k].handle + ":" + std::to_string(j)});
  }
  for (const auto &s : request.summaries) {
    const auto at = ref_place(entries, alive, first_of(s.unit), placement);
    if (!at)
      return refuse("note-placement");
    placed.push_back({*at, {first_of(s.unit), 1}, "new:summary"});
  }
  std::size_t inserts = 0;
  for (const auto &edit : request.edits) {
    if (edit.op != CutEdit::Op::insert && edit.op != CutEdit::Op::move)
      continue;
    const auto natural =
        edit.after ? std::optional{last_of(*edit.after)} : std::optional<std::size_t>{};
    const auto after = at_or_before(natural);
    const std::pair<std::size_t, int> key{natural ? *natural + 1 : 0, 2};
    if (edit.op == CutEdit::Op::insert)
      placed.push_back({after, key, "new:insert:" + std::to_string(inserts++)});
    else
      for (const auto pos : units[edit.unit].entries)
        if (!moved_drop.contains(pos))
          placed.push_back({after, key, id_at(entries, pos)});
  }
  if (writes_note) {
    std::size_t first = n;
    for (const auto u : chosen)
      first = std::min(first, first_of(u));
    const auto at = ref_place(entries, alive, first, placement);
    if (!at)
      return refuse("note-placement");
    placed.push_back({*at, {first, 3}, "new:note"});
    for (const auto u : chosen) {
      std::vector<std::string> ids;
      for (const auto pos : units[u].entries)
        ids.push_back(id_at(entries, pos));
      out.note_rows[units[u].handle] = ids;
    }
    for (const auto pos : absorbed)
      for (const auto &row : rows_of(pos))
        if (!restored_handles.contains(row_handle(row)))
          out.absorbed_rows.push_back(row);
  }
  // A placement inside a call group, or after a call still waiting, is refused.
  for (const auto &item : placed)
    if (splits_group(entries, alive, item.after))
      return refuse("inside-tool-group");
  const bool boundary_entry =
      request.boundary &&
      text_at(profile, {"turn", "boundary", "form"}) != "request_local_user";
  if (boundary_entry && splits_group(entries, alive, static_cast<long>(alive.size()) - 1))
    return refuse("inside-tool-group");
  std::stable_sort(placed.begin(), placed.end(),
                   [](const Placed &a, const Placed &b) { return a.key < b.key; });
  const auto emit_after = [&](long k) {
    for (const auto &item : placed)
      if (item.after == k)
        out.tokens.push_back(item.token);
  };
  emit_after(-1);
  for (long k = 0; k < static_cast<long>(alive.size()); ++k) {
    out.tokens.push_back(id_at(entries, alive[static_cast<std::size_t>(k)]));
    emit_after(k);
  }
  // §7.3: the boundary is the tail, after everything.
  if (boundary_entry)
    out.tokens.push_back("new:boundary");
  for (const auto pos : alive)
    if (type_is(item_at(entries, pos), "reasoning"))
      ++out.reasoning_left;
  for (const auto u : moved)
    for (const auto pos : units[u].entries)
      if (!moved_drop.contains(pos) && type_is(item_at(entries, pos), "reasoning"))
        ++out.reasoning_left;
  for (const auto &r : restores)
    for (const auto &item : r.items)
      if (!drop && type_is(item, "reasoning"))
        ++out.reasoning_left;
  // The codec judges the expected items (placeholder note text: only roles and
  // types matter to it). Model and Lua edits may produce input it refuses, and so may
  // a context such edits left starting with something other than an instruction;
  // Blackbird's own placements never break it on a context that starts with one.
  {
    const auto note_role_text = text_at(profile, {"note", "role"});
    const auto role = note_role_text == "developer_as_user_text" ? std::string{"developer"}
                                                                 : note_role_text;
    const auto placeholder = [](std::string r) {
      return Value::object({{"type", Value{"message"}},
                            {"role", Value{std::move(r)}},
                            {"content", Value{"placeholder"}}});
    };
    std::map<std::string, Value> by_id;
    for (std::size_t i = 0; i < n; ++i)
      by_id.emplace(id_at(entries, i), replaced.contains(i) ? replaced.at(i)
                                                             : item_at(entries, i));
    std::vector<const Value *> insert_items;
    for (const auto &edit : request.edits)
      if (edit.op == CutEdit::Op::insert)
        insert_items.push_back(&edit.item);
    Value::Array items;
    for (const auto &token : out.tokens) {
      if (!token.starts_with("new:"))
        items.push_back(by_id.at(token));
      else if (token == "new:note" || token == "new:summary")
        items.push_back(placeholder(role));
      else if (token == "new:boundary")
        items.push_back(placeholder(
            text_at(profile, {"turn", "boundary", "form"}) == "system_message" ? "system"
                                                                               : "user"));
      else if (token.starts_with("new:insert:"))
        items.push_back(*insert_items[std::stoul(token.substr(11))]);
      else {
        const auto rest = token.substr(12);
        const auto colon = rest.rfind(':');
        for (const auto &r : restores)
          if (r.handle == rest.substr(0, colon))
            items.push_back(r.items[std::stoul(rest.substr(colon + 1))]);
      }
    }
    try {
      export_items(fixture.family, items);
    } catch (const Error &) {
      const bool normal_start = n == 0 || class_at(0) == UnitClass::instruction;
      if (request.edits.empty() && normal_start)
        throw std::runtime_error("a Blackbird placement breaks the codec on a context "
                                 "that starts with an instruction" +
                                 shape_text(entries));
      return refuse("invalid-tool-protocol");
    }
  }
  return out;
}

// The tokens of an applied result, for comparison with the reference.
struct Applied {
  const Value::Array &before, &after;
};
std::vector<std::string> tokens_of(Applied applied, const CutRequest &request,
                                   const std::vector<RestoreSource> &restores) {
  const auto &before = applied.before;
  const auto &after = applied.after;
  std::set<std::string> old;
  for (const auto &e : before)
    old.insert(*text_member(e, "id"));
  std::vector<std::string> out;
  std::vector<bool> used_insert;
  std::vector<std::vector<bool>> used_restore;
  for (const auto &r : restores)
    used_restore.emplace_back(r.items.size(), false);
  std::vector<const Value *> insert_items;
  for (const auto &edit : request.edits)
    if (edit.op == CutEdit::Op::insert)
      insert_items.push_back(&edit.item);
  used_insert.assign(insert_items.size(), false);
  for (const auto &e : after) {
    const auto &id = *text_member(e, "id");
    if (old.contains(id)) {
      out.push_back(id);
      continue;
    }
    const auto &item = *e.find("item");
    // Requested items first (a restored unit may itself be a summary or a note).
    std::string token;
    for (std::size_t k = 0; k < insert_items.size() && token.empty(); ++k)
      if (!used_insert[k] && *insert_items[k] == item) {
        used_insert[k] = true;
        token = "new:insert:" + std::to_string(k);
      }
    for (std::size_t k = 0; k < restores.size() && token.empty(); ++k)
      for (std::size_t j = 0; j < restores[k].items.size() && token.empty(); ++j)
        if (!used_restore[k][j] && restores[k].items[j] == item) {
          used_restore[k][j] = true;
          token = "new:restore:" + restores[k].handle + ":" + std::to_string(j);
        }
    if (token.empty())
      token = is_note(item, archive_note_marker) ? "new:note"
              : is_note(item, boundary_marker)   ? "new:boundary"
              : is_note(item, summary_marker)    ? "new:summary"
                                                 : "new:unknown";
    out.push_back(token);
  }
  return out;
}
std::string joined(const std::vector<std::string> &tokens) {
  std::string out;
  for (const auto &t : tokens)
    out += (out.empty() ? "" : " ") + t;
  return out;
}

// ---- Profiles.
struct Row {
  std::string name;
  Value profile;
  std::vector<Family> families;
};
std::vector<Row> profile_rows(const fs::path &directory) {
  const auto set = load_profile_set({directory, std::nullopt});
  check(set.has_value(), "shipped profiles load");
  const auto effective = effective_profiles(*set);
  check(effective.has_value(), "shipped profiles resolve");
  std::vector<Row> rows;
  for (const auto &e : *effective) {
    const auto protocol = e.profile.find("match")->find("protocol");
    std::vector<Family> families;
    if (!protocol)
      families = {Family::anthropic, Family::responses, Family::portable};
    else if (protocol->string() == "messages")
      families = {Family::anthropic};
    else
      families = {Family::responses};
    rows.push_back({e.id, e.profile, families});
  }
  return rows;
}
// Synthetic variants exercise every placement, boundary form and reasoning rule on
// the families a shipped row serves.
std::vector<Row> variant_rows(const std::vector<Row> &shipped) {
  std::vector<Row> out = shipped;
  for (const auto &row : shipped) {
    const bool messages = row.families == std::vector<Family>{Family::anthropic};
    for (const auto *placement :
         {"after_user_turn_before_assistant", "before_next_user_entry"})
      out.push_back({row.name + "+" + placement,
                     with(row.profile, {"note", "placement"}, Value{placement}),
                     row.families});
    auto enabled = with(row.profile, {"turn", "boundary", "enabled"}, Value{true});
    out.push_back({row.name + "+boundary", enabled, row.families});
    out.push_back(
        {row.name + "+retaining",
         with(row.profile, {"request_state", "placement"}, Value{"retained_in_place"}),
         row.families});
    out.push_back({row.name + "+must_keep",
                   with(row.profile, {"reasoning", "at_cut"}, Value{"must_keep"}),
                   row.families});
    if (!messages) {
      out.push_back(
          {row.name + "+drop",
           with(row.profile, {"reasoning", "at_cut"}, Value{"drop_after_change"}),
           row.families});
      out.push_back(
          {row.name + "+refused",
           with(row.profile, {"archival", "mode"}, Value{"refused_until_probed"}),
           row.families});
    }
  }
  return out;
}

// ---- Bindings.
ChapterBinding binding_for(const Fixture &fixture, const Value &profile,
                           const std::string &model = {}) {
  ChapterBinding b;
  const bool anthropic = fixture.family == Family::anthropic;
  b.provider = anthropic ? "anthropic" : "openai";
  b.protocol = anthropic                            ? "messages"
               : fixture.family == Family::portable ? "responses-portable"
                                                    : "responses";
  b.base_url = anthropic ? "https://api.anthropic.com" : "https://api.example.com/v1";
  b.account = "default";
  b.model = model.empty() ? (anthropic ? "claude-opus-5-5" : "gpt-6") : model;
  b.profile_id = profile.find("id")->string();
  b.profile_revision = 1;
  b.profile_digest = profile_digest(profile);
  b.tools_digest = tools_digest(field(fixture.request({}), "tools"));
  b.instructions_digest = instructions_digest("be careful");
  return b;
}
// The codec's check, as S3b binds it: the family's own export of the items.
void export_items(Family family, const Value::Array &items) {
  Fixture shell;
  shell.family = family;
  Value::Array entries;
  for (std::size_t i = 0; i < items.size(); ++i)
    entries.push_back(Value::object(
        {{"id", Value{entry_id({IdentityBytes{}, static_cast<std::uint32_t>(i)})}},
         {"item", items[i]}}));
  (void)export_wire(shell, entries);
}
CutInput input_for(const Fixture &fixture, const std::vector<Unit> &units,
                   const Value &profile, const ChapterState &chapter,
                   const IdentityBytes &revision) {
  const auto family = fixture.family;
  return CutInput{fixture.entries,
                  units,
                  profile,
                  chapter,
                  revision,
                  CutSource::model,
                  {},
                  {},
                  {},
                  {},
                  [family](const Value::Array &items) { export_items(family, items); }};
}

// ---- Generated sessions: the removal and prefix oracles and the export oracle.
struct Tally {
  std::map<std::string, std::size_t> reasons, accepted;
  std::size_t cuts = 0, refusals = 0, expected_refusals = 0, removed = 0, swept = 0,
              dropped = 0, rows = 0;
};
void apply_into(Fixture &fixture, ChapterState &chapter, const CutFrame &frame) {
  fixture.entries = apply_frame(frame, fixture.entries);
  const auto packet = encode_cut_frame(frame);
  check(encode_cut_frame(decode_cut_frame(packet)) == packet,
        "a composed frame decodes back to itself");
  const auto parsed = parse_entry_id(frame.revision + ".0");
  check(parsed.has_value(), "a composed frame's revision is a record identity");
  const auto record = parsed.value_or(ContextRef{}).record;
  if (!frame.originals.empty())
    fixture.classes.set(record, derive_classes(packet));
  for (std::uint32_t i = 0; i < frame.originals.size(); ++i)
    fixture.originals.emplace(entry_id({record, i}), frame.originals[i].item);
  advance_chapter(chapter, frame);
}
// One line per entry: type, role, call, group, text head (failure reports).
std::string shape_text(const Value::Array &entries) {
  std::string shape;
  for (const auto &e : entries) {
    const auto &item = *e.find("item");
    const auto *t = text_member(item, "type");
    const auto *r = text_member(item, "role");
    const auto *c = text_member(item, "call_id");
    const auto *g = text_member(item, "anthropic_message_id");
    shape += "\n    " + std::string{t ? *t : "-"} + " " + (r ? *r : "-") + " " +
             (c ? *c : "-") + " " + (g ? *g : "-") + " " +
             item_text(item).substr(0, 30);
  }
  return shape;
}
struct Archived {
  std::string handle;
  std::vector<std::string> ids;
};
void generated(const Row &row, Family family, unsigned seed, Tally &tally) {
  std::mt19937 random{seed * 7919U + static_cast<unsigned>(family) * 31U +
                      static_cast<unsigned>(row.name.size())};
  Fixture fixture;
  fixture.family = family;
  ChapterState chapter;
  const auto &profile = row.profile;
  const auto where = row.name + "/" + family_name(family) + "/" + std::to_string(seed);
  fixture.add_prompt();
  {
    const auto units = fixture.live_units();
    const auto revision = fixture.next_record();
    auto start = compose_start(input_for(fixture, units, profile, chapter, revision),
                               binding_for(fixture, profile));
    check(start.has_value(), where + ": start composes");
    apply_into(fixture, chapter, start->frame);
  }
  std::vector<Archived> history;
  bool pending = false; // an unanswered call at the tail
  std::string pending_call;
  const std::vector<std::string> names{"exec", "read_file", "context_view",
                                       "context_stats", "context_budget"};
  for (unsigned step = 0; step < 60; ++step) {
    const auto choice = random() % 14;
    if (pending) {
      // Answer the open call first (an Anthropic context allows nothing else there).
      fixture.add({fixture.output(pending_call)}, {UnitClass::result});
      pending = false;
      continue;
    }
    if (choice == 0) {
      fixture.add_prompt();
      continue;
    }
    if (choice <= 3) {
      std::vector<std::string> calls;
      for (auto c = random() % 3; c > 0; --c)
        calls.push_back(names[random() % names.size()]);
      const bool open = !calls.empty() && random() % 6 == 0;
      const auto ids = fixture.add_round(random() % 10 < 7, random() % 2, calls, open,
                                         random() % 5 == 0);
      pending = open;
      if (open)
        pending_call = ids.back();
      continue;
    }
    if (choice == 4) {
      fixture.add_state();
      continue;
    }
    if (choice == 5) {
      if (random() % 3 == 0) {
        // A unit that ends in reasoning (a truncated response): text then reasoning,
        // or reasoning alone.
        ++fixture.groups;
        std::vector<Value> items;
        if (random() % 2)
          items.push_back(fixture.answer("cut short " + fixture.text()));
        items.push_back(fixture.reasoning());
        fixture.add(items, {UnitClass::round});
      } else
        fixture.add_round(random() % 2, true, {});
      continue;
    }
    if (choice == 6 && fixture.family != Family::anthropic) {
      // A repaired round: an instruction between a call and its output.
      std::string id;
      ++fixture.groups;
      fixture.add({fixture.call("exec", id)}, {UnitClass::round});
      fixture.add_prompt();
      fixture.add({fixture.output(id)}, {UnitClass::result});
      continue;
    }
    if (choice == 7) {
      auto units = fixture.live_units();
      if (!units.empty()) {
        const auto &pick = units[random() % units.size()];
        if (!pick.open)
          fixture.pins.insert(pick.key);
      }
      continue;
    }
    // A cut.
    const auto units = fixture.live_units();
    Wire before_wire;
    try {
      before_wire = export_wire(fixture, fixture.entries);
    } catch (const Error &error) {
      throw std::runtime_error(where + " step " + std::to_string(step) +
                               ": the fixture does not export before the cut: " +
                               std::string{error_name(error.code)} +
                               shape_text(fixture.entries));
    }
    CutRequest request;
    std::vector<RestoreSource> sources;
    std::vector<CutRestore> restores;
    std::optional<std::size_t> restored_index; // the history row a restore takes
    std::vector<std::size_t> selectable;
    for (std::size_t u = 0; u < units.size(); ++u)
      if (!units[u].open && !protected_unit(units[u]) && !units[u].ambiguous)
        selectable.push_back(u);
    const auto pick_some = [&](std::size_t most) {
      std::vector<std::size_t> out;
      for (auto k = 1 + random() % most; k > 0 && !selectable.empty(); --k) {
        const auto u = selectable[random() % selectable.size()];
        if (std::find(out.begin(), out.end(), u) == out.end())
          out.push_back(u);
      }
      return out;
    };
    const auto kind = random() % 10;
    const bool harness = kind >= 8;
    if (!harness) {
      // Several change kinds in one request (a merged proposal), each by chance.
      const auto mix = 1U + random() % 15U; // bits: archive, summary, restore, edit
      if ((mix & 1U) || kind <= 2)
        request.archive = pick_some(3);
      if ((mix & 2U) && !selectable.empty())
        request.summaries.push_back(
            {selectable[random() % selectable.size()], "summary " + fixture.text()});
      if ((mix & 4U) && !history.empty()) {
        const auto k = random() % history.size();
        RestoreSource source{history[k].handle, {}, {}, false};
        for (const auto &id : history[k].ids) {
          source.items.push_back(fixture.originals.at(id));
          const auto ref = *parse_entry_id(id);
          source.classes.push_back(fixture.classes.find(ref.record)->at(ref.ordinal));
        }
        sources.push_back(source);
        restores.push_back({source.handle, source.items, source.classes, false});
        restored_index = k;
      }
      for (auto e = (mix & 8U) ? 1 + random() % 2 : 0; e > 0 && !selectable.empty(); --e) {
        CutEdit edit;
        const auto op = random() % 4;
        const auto u = selectable[random() % selectable.size()];
        edit.op = static_cast<CutEdit::Op>(op);
        edit.unit = u;
        if (edit.op == CutEdit::Op::replace) {
          const auto pos = units[u].entries[random() % units[u].entries.size()];
          edit.entry = ref_at(fixture.entries, pos);
          auto item = *fixture.entries[pos].find("item");
          if (type_is(item, "function_call_output"))
            item = fixture.output(*text_member(item, "call_id"));
          else if (!type_is(item, "function_call") && !type_is(item, "reasoning")) {
            Value::Object kept;
            for (const auto &[key, value] : item.object())
              if (key != "content" && key != "anthropic_content")
                kept.emplace_back(key, value);
            kept.emplace_back("content", Value{"edited " + fixture.text()});
            item = Value{std::move(kept)};
          }
          edit.item = item;
        }
        if (edit.op == CutEdit::Op::insert || edit.op == CutEdit::Op::move) {
          if (random() % 4 != 0) {
            const auto anchor = random() % units.size();
            if (anchor != u)
              edit.after = anchor;
          }
          edit.item = fixture.developer("inserted " + fixture.text());
        }
        request.edits.push_back(edit);
      }
      request.boundary = random() % 4 == 0;
      request.restores = restores;
    }
    const auto revision = fixture.next_record();
    const auto input = input_for(fixture, units, profile, chapter, revision);
    ComposeResult composed;
    Expectation expected;
    std::string what = where + " step " + std::to_string(step);
    if (!harness) {
      expected = reference(fixture, units, profile, request, sources);
      composed = compose_cut(input, request);
      what += " cut";
    } else {
      // A rebind changes any of model, account, provider and base URL within the
      // family; a prefix cut changes the tools.
      auto next = chapter.binding;
      const bool forced = random() % 5 == 0;
      if (kind == 8) {
        const auto change = 1U + random() % 15U;
        if (change & 1U)
          next.model += "-x";
        if (change & 2U)
          next.account += "-other";
        if (change & 4U)
          next.provider += "-route";
        if (change & 8U)
          next.base_url += "/other";
        composed = compose_rebind(input, next, forced);
      } else {
        next.tools_digest = "sha256:" + std::to_string(step);
        composed = compose_prefix(input, next, forced);
      }
      what += kind == 8 ? " rebind" : " prefix";
      // The reference, from design §6.2-§6.4: only reasoning leaves. A rebind drops
      // another family's reasoning; keep_in_family keeps its own on the same route
      // (provider and base URL), drop_foreign only on the same route and account;
      // forced drops all. A prefix cut puts p at the tools level. drop_after_change
      // then drops every reasoning entry at or after p; must_keep refuses any drop.
      const auto at_cut = text_at(profile, {"reasoning", "at_cut"});
      const auto at_rebind = text_at(profile, {"reasoning", "at_rebind"});
      const auto &old = chapter.binding;
      const bool same_route = next.provider == old.provider && next.base_url == old.base_url;
      const bool own_kept =
          at_rebind == "keep_in_family" ? same_route : same_route && next.account == old.account;
      expected.p = kind == 9 ? 0 : fixture.entries.size();
      std::vector<bool> dropped(fixture.entries.size(), false);
      for (std::size_t i = 0; i < fixture.entries.size(); ++i) {
        const auto &item = item_at(fixture.entries, i);
        if (!type_is(item, "reasoning"))
          continue;
        if (forced || (kind == 8 && !own_kept)) {
          dropped[i] = true;
          expected.p = std::min(expected.p, i);
        }
      }
      if (at_cut == "drop_after_change")
        for (std::size_t i = expected.p; i < fixture.entries.size(); ++i)
          if (type_is(item_at(fixture.entries, i), "reasoning"))
            dropped[i] = true;
      for (std::size_t i = 0; i < fixture.entries.size(); ++i)
        if (dropped[i])
          expected.reasoning.insert(ref_at(fixture.entries, i));
        else {
          expected.tokens.push_back(*text_member(fixture.entries[i], "id"));
          expected.reasoning_left +=
              type_is(item_at(fixture.entries, i), "reasoning") ? 1 : 0;
        }
      if (at_cut == "must_keep" && !expected.reasoning.empty())
        expected.refusal = "must-keep-reasoning";
    }
    if (expected.refusal) {
      ++tally.expected_refusals;
      ++tally.reasons[*expected.refusal];
      check(!composed.has_value() && composed.error().reason == *expected.refusal,
            what + ": expected refusal " + *expected.refusal + ", got " +
                (composed.has_value() ? "a frame" : composed.error().reason));
      continue;
    }
    if (!composed.has_value()) {
      std::string asked = " archive";
      for (const auto u : request.archive)
        asked += " " + units[u].handle;
      asked += " summaries " + std::to_string(request.summaries.size()) + " restores " +
               std::to_string(request.restores.size()) + " edits";
      for (const auto &edit : request.edits)
        asked +=
            " op" + std::to_string(static_cast<int>(edit.op)) + ":" +
            units[edit.unit].handle +
            (edit.after ? "->" + units[*edit.after].handle : std::string{"->start"});
      asked += request.boundary ? " boundary" : "";
      for (const auto &r : request.restores) {
        Value::Array shown;
        for (const auto &item : r.items)
          shown.push_back(Value::object(
              {{"id", Value{std::string(32, '0') + ".0"}}, {"item", item}}));
        asked += "\n  restore " + r.handle + shape_text(shown);
      }
      auto unchecked = input;
      unchecked.validate = {};
      const auto raw = compose_cut(unchecked, request);
      if (raw.has_value())
        asked += "\n  unchecked result:" +
                 shape_text(apply_frame(raw->frame, fixture.entries)) + "\n" +
                 dump(encode_cut_frame(raw->frame));
      throw std::runtime_error(what + ": refused " + composed.error().reason + asked +
                               shape_text(fixture.entries));
    }
    const auto &frame = composed->frame;
    check(composed->p == expected.p, what + ": p " + std::to_string(composed->p) +
                                         " expected " + std::to_string(expected.p));
    Value::Array after;
    try {
      after = apply_frame(frame, fixture.entries);
    } catch (const Error &error) {
      throw std::runtime_error(
          what + ": the frame does not apply: " + std::string{error_name(error.code)} +
          shape_text(fixture.entries) + "\n" + dump(encode_cut_frame(frame)));
    }
    // The composer's result is the writer's (count, CRC32C and bytes of what applies).
    check(frame.result && frame.result->count == measure_entries(after, true).count &&
              frame.result->crc32c == measure_entries(after, true).crc32c &&
              frame.result->encoded_bytes == measure_entries(after, true).encoded_bytes,
          what + ": the composed result differs from the applied entries' measure");
    const auto tokens = tokens_of({fixture.entries, after}, request, sources);
    check(tokens == expected.tokens, what + ": result\n  got      " + joined(tokens) +
                                         "\n  expected " + joined(expected.tokens));
    check(std::set<ContextRef>(frame.reasoning.begin(), frame.reasoning.end()) ==
              expected.reasoning,
          what + ": dropped reasoning differs from §6.2");
    if (!harness) {
      check(std::set<ContextRef>(frame.swept.begin(), frame.swept.end()) ==
                expected.swept,
            what + ": swept entries differ from §5.6");
      check(std::set<ContextRef>(frame.archived.begin(), frame.archived.end()) ==
                expected.archived,
            what + ": archived entries differ");
      // The new note's rows: each archived unit with its entry IDs, then the rows of
      // the notes it absorbed, verbatim.
      for (const auto &e : after) {
        const auto &item = *e.find("item");
        const bool restored_item =
            std::any_of(sources.begin(), sources.end(), [&](const RestoreSource &r) {
              return std::find(r.items.begin(), r.items.end(), item) != r.items.end();
            });
        if (!is_note(item, archive_note_marker) || restored_item ||
            fixture.originals.contains(*text_member(e, "id")))
          continue;
        std::map<std::string, std::vector<std::string>> rows;
        std::vector<std::string> carried;
        for (const auto &line : note_rows(item)) {
          const auto handle = line.substr(0, line.find(' '));
          if (expected.note_rows.contains(handle) && !rows.contains(handle)) {
            std::vector<std::string> ids;
            const auto at = line.find("  ids ");
            std::string rest = at == std::string::npos ? "" : line.substr(at + 6);
            for (std::size_t from = 0; from < rest.size();) {
              const auto space = rest.find(' ', from);
              ids.push_back(rest.substr(from, space - from));
              from = space == std::string::npos ? rest.size() : space + 1;
            }
            rows[handle] = ids;
          } else
            carried.push_back(line);
        }
        check(rows == expected.note_rows && carried == expected.absorbed_rows,
              what + ": the note's rows differ\n" + item_text(item));
      }
      // Replacements: an edit's item exactly; a rewritten note keeps its remaining rows
      // and its header's call names an entry that is still archived.
      std::size_t notes_rewritten = 0;
      for (const auto &r : frame.replace) {
        if (const auto found = expected.replaced_items.find(r.ref);
            found != expected.replaced_items.end()) {
          check(r.item == found->second, what + ": a replace differs from the edit");
          continue;
        }
        const auto found = expected.rewritten_notes.find(r.ref);
        check(found != expected.rewritten_notes.end(), what + ": an unexpected replace");
        ++notes_rewritten;
        check(note_rows(r.item) == found->second,
              what + ": a rewritten note's rows differ\n" + item_text(r.item));
        const auto text = item_text(r.item);
        const auto header = text.substr(0, text.find('\n', text.find('\n') + 1));
        bool named = false;
        for (const auto &line : found->second)
          if (const auto at = line.find("  ids "); at != std::string::npos)
            named = named || header.find("\"" + line.substr(at + 6, line.find(' ', at + 6) -
                                                                        (at + 6)) +
                                         "\"") != std::string::npos;
        check(named, what + ": the rewritten note's header names no listed entry\n" + text);
      }
      check(notes_rewritten + expected.replaced_items.size() == frame.replace.size() &&
                notes_rewritten == expected.rewritten_notes.size(),
            what + ": the replacements differ in number");
    } else
      check(frame.swept.empty() && frame.archived.empty() && frame.insert.empty() &&
                frame.replace.empty() && frame.originals.empty(),
            what + ": a harness cut removes only reasoning");
    // Sweeps and drops never touch an entry before p.
    for (const auto *list : {&frame.swept, &frame.reasoning})
      for (const auto &ref : *list)
        for (std::size_t i = 0; i < expected.p; ++i)
          check(ref_at(fixture.entries, i) != ref, what + ": removal before p");
    // The adapters accept the result, keep pairing and carry the kept reasoning.
    Wire after_wire;
    try {
      after_wire = export_wire(fixture, after);
    } catch (const Error &error) {
      throw std::runtime_error(what + ": the adapter refused the result: " +
                               std::string{error_name(error.code)} + shape_text(after));
    }
    // Pairing: every output follows its own call once; no call the input answered
    // is left open.
    {
      const auto faults = [](const Value::Array &list) {
        std::set<std::string> open, seen;
        std::size_t bad = 0;
        for (const auto &e : list) {
          const auto &item = *e.find("item");
          const auto *id = text_member(item, "call_id");
          if (!id)
            continue;
          if (type_is(item, "function_call")) {
            bad += seen.insert(*id).second ? 0 : 1;
            open.insert(*id);
          } else if (type_is(item, "function_call_output"))
            bad += open.erase(*id) == 1 ? 0 : 1;
        }
        return std::pair{bad, open};
      };
      const auto [had_bad, had_open] = faults(fixture.entries);
      const auto [has_bad, has_open] = faults(after);
      check(has_bad <= had_bad && std::includes(had_open.begin(), had_open.end(),
                                                has_open.begin(), has_open.end()),
            what + ": pairing broken by the cut");
    }
    check(wire_reasoning(fixture, after_wire) == expected.reasoning_left,
          what + ": reasoning on the wire " +
              std::to_string(wire_reasoning(fixture, after_wire)) + " expected " +
              std::to_string(expected.reasoning_left));
    same_prefix(fixture, before_wire, after_wire, composed->p, what);
    if (restored_index)
      history.erase(history.begin() + static_cast<std::ptrdiff_t>(*restored_index));
    // History for later restores: archived units (not moves, not summaries).
    std::set<std::size_t> moved;
    for (const auto &edit : request.edits)
      if (edit.op == CutEdit::Op::move)
        moved.insert(edit.unit);
    for (const auto u : request.archive) {
      Archived a{units[u].handle, {}};
      for (const auto pos : units[u].entries)
        a.ids.push_back(*text_member(fixture.entries[pos], "id"));
      history.push_back(a);
    }
    for (const auto &edit : request.edits)
      if (edit.op == CutEdit::Op::remove) {
        Archived a{units[edit.unit].handle, {}};
        for (const auto pos : units[edit.unit].entries)
          a.ids.push_back(*text_member(fixture.entries[pos], "id"));
        history.push_back(a);
      }
    ++tally.cuts;
    for (std::size_t bit = 0; bit < 6; ++bit)
      if (frame.changes & (1U << bit))
        ++tally.accepted[std::to_string(bit)];
    ++tally.accepted[frame.kind == CutKind::archival ? "archival"
                     : frame.kind == CutKind::rebind ? "rebind"
                                                     : "prefix"];
    tally.removed += frame.archived.size();
    tally.swept += frame.swept.size();
    tally.dropped += frame.reasoning.size();
    // Pins on units that left lapse, as the store's do.
    try {
      apply_into(fixture, chapter, frame);
    } catch (const Error &error) {
      throw std::runtime_error(
          what + ": the frame does not record: " + std::string{error_name(error.code)});
    }
    std::set<ContextRef> live;
    for (const auto &u : fixture.live_units())
      live.insert(u.key);
    std::erase_if(fixture.pins, [&](const ContextRef &r) { return !live.contains(r); });
  }
}
void removal_and_prefix(const fs::path &directory) {
  Tally tally;
  for (const auto &row : variant_rows(profile_rows(directory))) {
    ++tally.rows;
    for (const auto family : row.families)
      for (unsigned seed = 0; seed < 6; ++seed)
        generated(row, family, seed, tally);
  }
  check(tally.cuts > 1000 && tally.swept > 0 && tally.dropped > 0 &&
            tally.expected_refusals > 0,
        "the generator reaches cuts, sweeps, drops and refusals");
  std::cout << "  " << tally.rows << " rows, " << tally.cuts << " cuts compared, "
            << tally.expected_refusals << " refusals as expected, " << tally.removed
            << " archived, " << tally.swept << " swept, " << tally.dropped
            << " reasoning dropped\n   refusals:";
  for (const auto &[reason, count] : tally.reasons)
    std::cout << " " << reason << "=" << count;
  std::cout << "\n   accepted (change bits 0 archive 1 edit 2 restore 3 summary 5 "
               "boundary):";
  for (const auto &[kind, count] : tally.accepted)
    std::cout << " " << kind << "=" << count;
  std::cout << "\n";
}

// ---- Specification (§6.2): reasoning per at_cut on a hand context.
struct Hand {
  Fixture fixture;
  ChapterState chapter;
  std::vector<std::string> r; // reasoning IDs of rounds 1..3
};
Hand hand(Family family, const Value &profile) {
  Hand h;
  h.fixture.family = family;
  h.fixture.add_prompt();
  for (int round = 0; round < 3; ++round) {
    h.fixture.add_round(true, round == 2,
                        round == 2 ? std::vector<std::string>{}
                                   : std::vector<std::string>{"exec"});
  }
  for (const auto &e : h.fixture.entries)
    if (type_is(*e.find("item"), "reasoning"))
      h.r.push_back(*text_member(e, "id"));
  const auto units = h.fixture.live_units();
  auto start = compose_start(
      input_for(h.fixture, units, profile, h.chapter, h.fixture.next_record()),
      binding_for(h.fixture, profile));
  check(start.has_value(), "hand start");
  apply_into(h.fixture, h.chapter, start->frame);
  return h;
}
std::set<std::string> reasoning_ids(const Value::Array &entries) {
  std::set<std::string> out;
  for (const auto &e : entries)
    if (type_is(*e.find("item"), "reasoning"))
      out.insert(*text_member(e, "id"));
  return out;
}
std::size_t reasoning_items(const Value::Array &entries) {
  return reasoning_ids(entries).size();
}
void reasoning_spec(const fs::path &directory) {
  const auto rows = profile_rows(directory);
  const auto row_named = [&](std::string_view id) {
    for (const auto &r : rows)
      if (r.name == id)
        return r.profile;
    throw std::runtime_error("no row " + std::string{id});
  };
  const auto prefix_checked = row_named("anthropic-messages-prefix-checked");
  const auto older = row_named("anthropic-messages");
  const auto openai = row_named("openai-chatgpt-responses");
  // The round-2 unit is units[2] (prompt, round 1, round 2, round 3).
  for (const auto &[profile, family, dropping] :
       std::vector<std::tuple<Value, Family, bool>>{
           {prefix_checked, Family::anthropic, true},
           {older, Family::anthropic, false},
           {openai, Family::responses, false}}) {
    const auto id = profile.find("id")->string();
    // Archive round 2: p at its first entry; R1 stays; R3 drops under
    // drop_after_change.
    {
      auto h = hand(family, profile);
      const auto units = h.fixture.live_units();
      check(units.size() == 4 && units[2].unit_class == UnitClass::round,
            id + ": hand units");
      auto cut = compose_archive(
          input_for(h.fixture, units, profile, h.chapter, h.fixture.next_record()),
          {2});
      check(cut.has_value(), id + ": archive composes");
      const auto after = apply_frame(cut->frame, h.fixture.entries);
      const auto left = reasoning_ids(after);
      check(left.contains(h.r[0]) && !left.contains(h.r[1]),
            id + ": reasoning before p kept, the archived round's gone");
      check(left.contains(h.r[2]) == !dropping,
            id + ": reasoning after p " + (dropping ? "dropped" : "kept"));
      check(cut->frame.reasoning.size() == (dropping ? 1U : 0U),
            id + ": the reasoning list names exactly the dropped entry");
      // Restore round 2 from its originals: restored reasoning counts after p.
      apply_into(h.fixture, h.chapter, cut->frame);
      const auto units2 = h.fixture.live_units();
      std::vector<Value> items;
      std::vector<UnitClass> classes;
      // Items of round 2 (its record and its output record), from the originals.
      const auto record = parse_entry_id(h.r[1])->record;
      for (const auto &[eid, item] : h.fixture.originals) {
        const auto ref = *parse_entry_id(eid);
        if (ref.record == record) {
          items.push_back(item);
          classes.push_back(UnitClass::round);
        }
      }
      for (const auto &[eid, item] : h.fixture.originals)
        if (type_is(item, "function_call_output") &&
            *text_member(item, "call_id") == *text_member(items.back(), "call_id")) {
          items.push_back(item);
          classes.push_back(UnitClass::result);
        }
      auto restored = compose_restore(
          input_for(h.fixture, units2, profile, h.chapter, h.fixture.next_record()),
          {{units[2].handle, items, classes, false}});
      check(restored.has_value(), id + ": restore composes");
      const auto back = apply_frame(restored->frame, h.fixture.entries);
      std::size_t restored_reasoning = 0;
      for (const auto &o : restored->frame.originals)
        restored_reasoning += type_is(o.item, "reasoning") ? 1 : 0;
      check(restored_reasoning == (dropping ? 0U : 1U),
            id + ": restored reasoning " + (dropping ? "dropped" : "kept"));
      check(reasoning_items(back) == (dropping ? 1U : 3U),
            id + ": reasoning after the restore");
    }
    // Move round 1 after round 3 (a Lua reorder): its reasoning was re-inserted, so
    // it counts after p.
    {
      auto h = hand(family, profile);
      const auto units = h.fixture.live_units();
      CutEdit move;
      move.op = CutEdit::Op::move;
      move.unit = 1;
      move.after = 3;
      auto cut = compose_edit(
          input_for(h.fixture, units, profile, h.chapter, h.fixture.next_record()),
          {move});
      check(cut.has_value(), id + ": move composes");
      const auto after = apply_frame(cut->frame, h.fixture.entries);
      const auto left = reasoning_ids(after);
      check(left.contains(h.r[0]) == !dropping && left.contains(h.r[1]) == !dropping &&
                left.contains(h.r[2]) == !dropping,
            id + ": every reasoning at or after p, the moved one included");
      check(cut->p == units[1].entries.front(), id + ": p at the moved unit");
      // The moved unit keeps its entry IDs.
      for (const auto pos : units[1].entries) {
        const auto moved_id = *text_member(h.fixture.entries[pos], "id");
        const bool kept = std::any_of(after.begin(), after.end(), [&](const Value &e) {
          return *text_member(e, "id") == moved_id;
        });
        check(kept ||
                  (dropping && type_is(item_at(h.fixture.entries, pos), "reasoning")),
              id + ": a moved entry keeps its ID");
      }
    }
    // Prefix: p at the tools level.
    {
      auto h = hand(family, profile);
      const auto units = h.fixture.live_units();
      auto next = h.chapter.binding;
      next.tools_digest = "sha256:changed";
      auto cut = compose_prefix(
          input_for(h.fixture, units, profile, h.chapter, h.fixture.next_record()),
          next);
      check(cut.has_value() && cut->p == 0 && cut->frame.sizes.prefix == 0,
            id + ": prefix cut at p 0");
      check(reasoning_items(apply_frame(cut->frame, h.fixture.entries)) ==
                (dropping ? 0U : 3U),
            id + ": prefix cut reasoning");
      check(cut->frame.kind == CutKind::prefix &&
                decode_binding(field(cut->frame.chapter, "binding")) == next,
            id + ": the prefix frame binds the new digests");
      auto forced = compose_prefix(
          input_for(h.fixture, units, profile, h.chapter, h.fixture.next_record()),
          next, true);
      check(forced.has_value() && forced->frame.cause.forced &&
                reasoning_items(apply_frame(forced->frame, h.fixture.entries)) == 0,
            id + ": the forced form drops all reasoning");
      auto same = compose_rebind(
          input_for(h.fixture, units, profile, h.chapter, h.fixture.next_record()),
          h.chapter.binding);
      check(!same.has_value() && same.error().reason == "binding-unchanged",
            id + ": a rebind needs a changed binding");
    }
  }
  // Rebind across families: foreign reasoning is dropped, own family kept
  // (keep_in_family); drop_foreign also drops own-family reasoning under another
  // account.
  {
    auto h = hand(Family::anthropic, older);
    h.fixture.family = Family::responses;
    h.fixture.add_round(true, false,
                        {"exec"}); // a Responses round after an Anthropic chapter
    const auto units = h.fixture.live_units();
    auto next = binding_for(h.fixture, openai);
    auto to_openai = compose_rebind(
        input_for(h.fixture, units, openai, h.chapter, h.fixture.next_record()), next);
    check(to_openai.has_value(), "cross-family rebind composes");
    const auto after = apply_frame(to_openai->frame, h.fixture.entries);
    // drop_foreign under another provider route: the Anthropic reasoning is foreign,
    // and own-family reasoning made under the old route cannot be trusted either.
    check(reasoning_items(after) == 0, "a cross-route drop_foreign rebind drops all");
    check(to_openai->p == units[1].entries.front(), "p at the first foreign reasoning");
    (void)export_provider_request(h.fixture.request(after));
    auto same_family = h.chapter.binding;
    same_family.model = "claude-mythos-5-1";
    auto in_family = compose_rebind(
        input_for(h.fixture, units, older, h.chapter, h.fixture.next_record()),
        same_family);
    check(
        in_family.has_value() &&
            reasoning_items(apply_frame(in_family->frame, h.fixture.entries)) == 3,
        "keep_in_family keeps Anthropic reasoning across a model change and drops the "
        "Responses one");
    auto h2 = hand(Family::responses, openai);
    const auto units2 = h2.fixture.live_units();
    auto other = h2.chapter.binding;
    other.account = "second";
    auto moved_account = compose_rebind(
        input_for(h2.fixture, units2, openai, h2.chapter, h2.fixture.next_record()),
        other);
    check(moved_account.has_value() &&
              reasoning_items(apply_frame(moved_account->frame, h2.fixture.entries)) ==
                  0,
          "drop_foreign drops own-family reasoning under another account");
    auto model_only = h2.chapter.binding;
    model_only.model = "gpt-6-mini";
    auto kept = compose_rebind(
        input_for(h2.fixture, units2, openai, h2.chapter, h2.fixture.next_record()),
        model_only);
    check(kept.has_value() &&
              reasoning_items(apply_frame(kept->frame, h2.fixture.entries)) == 3,
          "drop_foreign keeps own-family reasoning across a model change");
    // keep_in_family is one provider route: another provider or base URL of the same
    // protocol drops its reasoning (layer 1, A-L4).
    auto h3 = hand(Family::anthropic, older);
    const auto units3 = h3.fixture.live_units();
    for (const auto field_name : {"provider", "base_url"}) {
      auto route = h3.chapter.binding;
      (field_name == std::string_view{"provider"} ? route.provider : route.base_url) +=
          "-other";
      auto moved = compose_rebind(
          input_for(h3.fixture, units3, older, h3.chapter, h3.fixture.next_record()), route);
      check(moved.has_value() &&
                reasoning_items(apply_frame(moved->frame, h3.fixture.entries)) == 0,
            std::string{"keep_in_family drops reasoning under another "} + field_name);
    }
    auto account3 = h3.chapter.binding;
    account3.account = "second";
    auto same_route = compose_rebind(
        input_for(h3.fixture, units3, older, h3.chapter, h3.fixture.next_record()), account3);
    check(same_route.has_value() &&
              reasoning_items(apply_frame(same_route->frame, h3.fixture.entries)) == 3,
          "keep_in_family keeps its reasoning under another account");
  }
  // must_keep refuses a forced drop; refused_until_probed refuses removal kinds.
  {
    const auto must = with(openai, {"reasoning", "at_cut"}, Value{"must_keep"});
    auto h = hand(Family::responses, must);
    const auto units = h.fixture.live_units();
    auto next = h.chapter.binding;
    next.tools_digest = "sha256:other";
    auto forced = compose_prefix(
        input_for(h.fixture, units, must, h.chapter, h.fixture.next_record()), next,
        true);
    check(!forced.has_value() && forced.error().reason == "must-keep-reasoning",
          "must_keep refuses dropping reasoning");
    // Design §6.2: on must_keep "a cut that would drop reasoning on a retained
    // assistant message is refused", so an unforced rebind that would drop it (another
    // account under drop_foreign) is refused too (layer 1, A-M3).
    auto other = h.chapter.binding;
    other.account = "second";
    auto unforced = compose_rebind(
        input_for(h.fixture, units, must, h.chapter, h.fixture.next_record()), other);
    check(!unforced.has_value() && unforced.error().reason == "must-keep-reasoning",
          "must_keep refuses an unforced rebind that drops reasoning");
    const auto refused =
        with(openai, {"archival", "mode"}, Value{"refused_until_probed"});
    auto h2 = hand(Family::responses, refused);
    const auto units2 = h2.fixture.live_units();
    const auto in =
        input_for(h2.fixture, units2, refused, h2.chapter, h2.fixture.next_record());
    check(compose_archive(in, {2}).error().reason == "archival-refused" &&
              compose_summary(in, {2, "s"}).error().reason == "archival-refused",
          "refused_until_probed refuses archive and summary");
    CutEdit insert;
    insert.op = CutEdit::Op::insert;
    insert.item = h2.fixture.developer("note to self");
    check(compose_edit(in, {insert}).has_value(),
          "refused_until_probed still allows an insert");
  }
  // A reasoning item is never edited; an inserted call is refused.
  {
    auto h = hand(Family::anthropic, prefix_checked);
    const auto units = h.fixture.live_units();
    const auto in =
        input_for(h.fixture, units, prefix_checked, h.chapter, h.fixture.next_record());
    CutEdit replace;
    replace.op = CutEdit::Op::replace;
    replace.entry = *parse_entry_id(h.r[0]);
    replace.item = item_at(h.fixture.entries, units[1].entries.front());
    check(compose_edit(in, {replace}).error().reason == "reasoning-edit",
          "a reasoning replace is refused");
    CutEdit insert;
    insert.op = CutEdit::Op::insert;
    std::string id;
    insert.item = h.fixture.call("exec", id);
    check(compose_edit(in, {insert}).error().reason == "invalid-item",
          "an inserted call is refused");
    CutEdit protected_remove;
    protected_remove.op = CutEdit::Op::remove;
    protected_remove.unit = 0;
    check(compose_edit(in, {protected_remove}).error().reason == "protected",
          "the operator prompt is protected from model edits");
  }
}

// ---- Turn state (§7.1) on rendered messages.
void turn_states(const fs::path &directory) {
  const auto rows = profile_rows(directory);
  Value anthropic, openai;
  for (const auto &r : rows) {
    if (r.name == "anthropic-messages-prefix-checked")
      anthropic = r.profile;
    if (r.name == "openai-chatgpt-responses")
      openai = r.profile;
  }
  const auto state_of = [](const Fixture &f, const Value &profile) {
    const auto request = f.request(f.entries);
    const auto &input = field(request, "input").array();
    const auto rendered = f.family == Family::anthropic
                              ? anthropic_render(input)
                              : responses_render(input, false);
    return turn_state(rendered, profile);
  };
  // An operator prompt merged after trailing tool results stays open on Anthropic.
  Fixture a;
  a.add_prompt();
  a.add_round(true, false, {"exec"});
  a.add_prompt();
  auto s = state_of(a, anthropic);
  check(!s.closed, "a prompt merged after tool results is open on Anthropic");
  const auto rendered = anthropic_render(field(a.request(a.entries), "input").array());
  check(rendered.size() == 3 && rendered.back().results && rendered.back().text,
        "the prompt renders into the tool-result message");
  check(s.current == 0, "the current turn starts at the first prompt");
  // The closing reply, then a separate prompt: closed.
  a.add_round(false, true, {});
  check(state_of(a, anthropic).closed, "the closing reply closes the tail");
  a.add_prompt();
  s = state_of(a, anthropic);
  check(s.closed && s.current == a.entries.size() - 1,
        "a prompt after the closing reply starts a turn and is closed");
  // OpenAI: a user note after tool output meets user_text.
  Fixture o;
  o.family = Family::responses;
  o.add_prompt();
  o.add_round(true, false, {"exec"});
  check(!state_of(o, openai).closed, "tool output at the tail is open");
  o.add({Value::object({{"role", Value{"user"}},
                        {"content", Value{std::string{boundary_marker} + " x"}}})},
        {UnitClass::note});
  s = state_of(o, openai);
  check(s.closed && s.current == o.entries.size() - 1, "user_note meets user_text");
  // A developer item on Responses is not a user message: open.
  o.add_round(true, false, {"exec"});
  o.add_state();
  check(!state_of(o, openai).closed, "a developer item does not start a turn");
  // A Responses reasoning item at the tail is not an assistant message: open.
  Fixture q;
  q.family = Family::responses;
  q.add_prompt();
  q.add({q.reasoning()}, {UnitClass::round});
  check(!state_of(q, openai).closed, "a trailing reasoning item is open");
  check(turn_state({}, openai).closed, "an empty input is closed");
  // The boundary forms meet their rules: reply_then_note after a closing reply on
  // Anthropic, user_note on OpenAI, through the composers.
  for (const auto &[profile, family] : std::vector<std::pair<Value, Family>>{
           {anthropic, Family::anthropic}, {openai, Family::responses}}) {
    const auto enabled = with(profile, {"turn", "boundary", "enabled"}, Value{true});
    Fixture f;
    f.family = family;
    f.add_prompt();
    f.add_round(true, false, {"exec"});
    f.add_round(true, false, {"read_file"});
    if (family == Family::anthropic)
      f.add_round(false, true, {}); // the closing reply (S3b requests it)
    ChapterState chapter;
    auto units = f.live_units();
    auto start = compose_start(input_for(f, units, enabled, chapter, f.next_record()),
                               binding_for(f, enabled));
    apply_into(f, chapter, start->frame);
    units = f.live_units();
    auto cut = compose_archive(input_for(f, units, enabled, chapter, f.next_record()),
                               {1}, true);
    check(cut.has_value(), "an archive with a boundary composes");
    apply_into(f, chapter, cut->frame);
    check(state_of(f, enabled).closed &&
              state_of(f, enabled).current == f.entries.size() - 1,
          "the boundary is a turn start under " + text_at(enabled, {"turn", "start"}));
    check(cut->frame.changes & cut_boundary, "the frame names the boundary change");
    const auto &tail = *f.entries.back().find("item");
    check(is_note(tail, boundary_marker) && user_entry(tail),
          "the boundary is a user note");
  }
}

// ---- Templates (§5.5, §7.3) and the note's place and role.
void templates(const fs::path &directory) {
  check(size_text(600) == "0.6K" && size_text(8192) == "8.0K" &&
            size_text(1258291) == "1.2M",
        "size text as the design's examples");
  const auto note = archive_note_text(
      {{"r4821", "exec", "cmake --build build/debug", "exit=0", 8192, 1258291,
        {"aa.1", "bb.0"}},
       {"r4902", "message", "The replay loop", "", 600, std::nullopt, {"cc.2"}}});
  // The read-back call is today's context_inspect with its required query (layer 1,
  // A-M2); the call text is written out here, independently of the template.
  const std::string expected_call =
      "context_inspect {\"query\": {\"kind\": \"originals\", \"entry\": \"aa.1\", "
      "\"format\": \"json\"}}";
  check(note.starts_with(std::string{archive_note_marker} +
                         " These units left your live context; nothing was summarised.\n"
                         "Read one with " +
                         expected_call + ", giving an entry ID from its row.\n"),
        "the archive note header: " + note);
  check(note.find("ids aa.1 bb.0") != std::string::npos &&
            note.find("ids cc.2") != std::string::npos,
        "each row lists its entry IDs: " + note);
  check(note.find("r4821  exec") != std::string::npos &&
            note.find("\"cmake --build build/debug\"") != std::string::npos &&
            note.find("exit=0") != std::string::npos &&
            note.find("8.0K of 1.2M") != std::string::npos &&
            note.find("0.6K") != std::string::npos,
        "the archive note rows");
  const auto boundary = boundary_text("r4810", "r5100.0");
  check(
      boundary ==
          std::string{boundary_marker} +
              " This note is from Blackbird, not the operator. Blackbird closed the "
              "turn "
              "here so that earlier rounds could leave your context. Your task is "
              "unchanged: continue from where you were. The operator's request is "
              "r4810; the archive note r5100.0 lists what left. No reply to this note "
              "is needed.",
      "the boundary text: " + boundary);
  // A 60-byte head is cut on a character boundary.
  const auto rows = profile_rows(directory);
  for (const auto &row : rows) {
    for (const auto family : row.families) {
      Fixture f;
      f.family = family;
      ChapterState chapter;
      f.add_prompt();
      ++f.groups;
      std::string id;
      auto call = f.call("exec", id);
      for (auto &[key, value] : call.object())
        if (key == "arguments")
          value = Value::object(
              {{"cmd", Value{std::string(58, 'a') + "\xc3\xa9\xc3\xa9 tail"}}});
      f.add({call}, {UnitClass::round});
      f.add({f.output(id, true)}, {UnitClass::result});
      f.add_round(false, true, {});
      auto units = f.live_units();
      auto start =
          compose_start(input_for(f, units, row.profile, chapter, f.next_record()),
                        binding_for(f, row.profile));
      apply_into(f, chapter, start->frame);
      units = f.live_units();
      const auto revision = f.next_record();
      auto cut =
          compose_archive(input_for(f, units, row.profile, chapter, revision), {1});
      check(cut.has_value(), row.name + ": archive composes");
      check(cut->frame.originals.size() == 1, row.name + ": one note");
      const auto &item = cut->frame.originals[0].item;
      const auto role = text_at(row.profile, {"note", "role"});
      const auto expected_role =
          role == "developer_as_user_text" ? std::string{"developer"} : role;
      check(*text_member(item, "role") == expected_role, row.name + ": note role");
      check(cut->frame.originals[0].entry_class ==
                static_cast<std::uint8_t>(UnitClass::note),
            row.name + ": note class");
      const auto text = item_text(item);
      check(text.find("\"" + std::string(58, 'a') + "\xc3\xa9\xe2\x80\xa6\"") !=
                std::string::npos,
            row.name + ": the head stops before a split character: " + text);
      check(text.find("failed") != std::string::npos, row.name + ": the failed flag");
      check(text.find(units[1].handle + "  exec") != std::string::npos,
            row.name + ": the row names the unit");
      const auto first_id = *text_member(f.entries[units[1].entries.front()], "id");
      check(text.find("{\"query\": {\"kind\": \"originals\", \"entry\": \"" + first_id +
                      "\"") != std::string::npos,
            row.name + ": the header's call names the unit's first entry: " + text);
      apply_into(f, chapter, cut->frame);
      const auto after_units = f.live_units();
      const auto note_unit =
          std::find_if(after_units.begin(), after_units.end(),
                       [](const Unit &u) { return u.unit_class == UnitClass::note; });
      check(note_unit != after_units.end() &&
                note_unit->handle == "r" + std::to_string(handle_number(revision)),
            row.name + ": the note's handle is the frame's record");
      check(chapter.table.size() == 2 && chapter.table.back().ordinal == 2 &&
                chapter.table.back().kind == CutKind::archival,
            row.name + ": an archival cut starts chapter 2");
    }
  }
}

// ---- Unit keys: one frame restoring two rounds keeps them separate units.
void unit_keys(const fs::path &directory) {
  const auto rows = profile_rows(directory);
  const auto &profile = rows.front().profile; // conservative: keeps reasoning
  Fixture f;
  f.family = Family::responses;
  ChapterState chapter;
  f.add_prompt();
  f.add_round(true, false, {"exec"});
  f.add_round(true, false, {"read_file"});
  f.add_round(false, true, {});
  auto units = f.live_units();
  auto start = compose_start(input_for(f, units, profile, chapter, f.next_record()),
                             binding_for(f, profile));
  apply_into(f, chapter, start->frame);
  units = f.live_units();
  std::vector<CutRestore> restores;
  for (const std::size_t u : {std::size_t{1}, std::size_t{2}}) {
    CutRestore r{units[u].handle, {}, {}, false};
    for (const auto pos : units[u].entries) {
      r.items.push_back(item_at(f.entries, pos));
      const auto ref = ref_at(f.entries, pos);
      r.classes.push_back(f.classes.find(ref.record)->at(ref.ordinal));
    }
    restores.push_back(r);
  }
  auto archive =
      compose_archive(input_for(f, units, profile, chapter, f.next_record()), {1, 2});
  apply_into(f, chapter, archive->frame);
  units = f.live_units();
  auto restore =
      compose_restore(input_for(f, units, profile, chapter, f.next_record()), restores);
  check(restore.has_value(), "a two-unit restore composes");
  const auto &originals = restore->frame.originals;
  check(originals.size() == 6 && !originals[0].unit && originals[3].unit == 3U,
        "the second restored round names its unit key");
  const auto packet = encode_cut_frame(restore->frame);
  check(decode_cut_frame(packet).originals[3].unit == 3U, "the key round-trips");
  apply_into(f, chapter, restore->frame);
  std::size_t rounds = 0;
  for (const auto &u : f.live_units())
    rounds += u.unit_class == UnitClass::round && !u.open ? 1 : 0;
  check(rounds == 3, "both restored rounds are their own units (and the reply)");
  // Malformed keys are corrupt.
  for (const auto bad : {std::uint32_t{4}, std::uint32_t{1}}) {
    auto frame = restore->frame;
    frame.originals[2].unit = bad; // 4: after its ordinal; 1: not the first of a unit
    if (bad == 1)
      frame.originals[3].unit.reset(), frame.originals[2].unit = 1;
    bool refused = false;
    try {
      (void)derive_classes(encode_cut_frame(frame));
      (void)decode_cut_frame(encode_cut_frame(frame));
    } catch (const Error &error) {
      refused = error.code == ErrorCode::corrupt;
    }
    check(refused, "a malformed unit key is corrupt: " + std::to_string(bad));
  }
}

// ---- Chapter section.
void chapter_section_case(const fs::path &) {
  ChapterState state;
  ChapterBinding b{"anthropic", "messages",        "https://api.anthropic.com",
                   "default",   "claude-opus-5-5", "anthropic-messages-prefix-checked",
                   1,           "sha256:aa",       "sha256:bb",
                   "sha256:cc"};
  CutFrame start;
  start.kind = CutKind::start;
  start.revision = std::string(31, '0') + "1";
  start.chapter = Value::object({{"ordinal", Value{Number{1}}},
                                 {"previous", Value{}},
                                 {"binding", encode_binding(b)}});
  advance_chapter(state, start);
  CutFrame next = start;
  next.kind = CutKind::archival;
  next.revision = std::string(31, '0') + "2";
  next.chapter = Value::object({{"ordinal", Value{Number{2}}},
                                {"previous", Value{start.revision}},
                                {"binding", encode_binding(b)}});
  advance_chapter(state, next);
  check(state.table.size() == 2 && state.binding == b, "two chapters");
  const auto section = chapter_section_value(state);
  check(read_chapter_section(section) == state, "the section round-trips");
  check(validate_chapter_section(section).has_value() &&
            saved_section_registered(chapter_section),
        "the chapter section is registered and valid");
  auto skip = next;
  skip.revision = std::string(31, '0') + "3";
  skip.chapter = Value::object({{"ordinal", Value{Number{4}}},
                                {"previous", Value{next.revision}},
                                {"binding", encode_binding(b)}});
  bool refused = false;
  try {
    advance_chapter(state, skip);
  } catch (const Error &error) {
    refused = error.code == ErrorCode::corrupt;
  }
  check(refused, "a chapter that skips an ordinal is corrupt");
  auto bad = section;
  for (auto &[key, value] : bad.object())
    if (key == "table")
      value.array().pop_back();
  check(validate_chapter_section(Value::object({{"binding", encode_binding(b)},
                                                {"table", Value{Value::Array{}}}}))
                .has_value() == false,
        "an empty table is refused");
  auto mismatched = encode_binding(b);
  mismatched.object().emplace_back("extra", Value{"x"});
  refused = false;
  try {
    (void)decode_binding(mismatched);
  } catch (const Error &error) {
    refused = error.code == ErrorCode::corrupt;
  }
  check(refused, "a binding with an unknown field is corrupt");
}

// ---- Persistence through the store: live = restore + suffix = whole history.
const JournalHeader header{
    unwrap(EnvironmentId::from_bytes(IdentityBytes{std::byte{1}})),
    unwrap(AuditStreamId::from_bytes(IdentityBytes{std::byte{2}})),
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
Value units_text(const ContextStore &context) {
  const auto entries = field(context.view(), "entries").array();
  std::string out;
  for (const auto &unit : context.units()) {
    out += unit.handle + ":" + std::string{class_name(unit.unit_class)};
    for (const auto position : unit.entries)
      out += " " + string_field(entries[position], "id");
    out += "\n";
  }
  return Value{std::move(out)};
}
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
    log->record(ApplicationChannel::program,
                Value::object({{"label", Value{"session.identity"}}}));
    context = std::make_unique<ContextStore>(*log);
  }
  void reopen() {
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
    context = std::make_unique<ContextStore>(*log);
  }
  static Value state(const ContextStore &c) {
    return Value::object(
        {{"view", c.view()},
         {"units", units_text(c)},
         {"chapter",
          c.chapter().started() ? chapter_section_value(c.chapter()) : Value{}}});
  }
  Value probe(bool whole) const {
    ContextStore other{*log, ContextOpen{.whole_history = whole, .probe = true}};
    return state(other);
  }
};
// The context head a composer reads (layer 1, B2); layer 0 had no such field.
void set_head(CutInput &input, const std::string &head) {
#ifndef S3A_LAYER0
  input.head = head;
#else
  (void)input;
  (void)head;
#endif
}
std::string unhex(std::string_view hex) {
  std::string out;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
    out.push_back(static_cast<char>(std::stoi(std::string{hex.substr(i, 2)}, nullptr, 16)));
  return out;
}
// Runs the read-back call a note or summary names, with its own query, once for every
// entry ID the text lists, against the current tool's handler (ContextStore::inspect,
// which context_inspect dispatches to): each must return that entry's original. Returns
// how many listed IDs were read back.
std::size_t run_inspect_call(const ContextStore &context, const std::string &text,
                             const std::map<std::string, Value> &originals) {
  const auto at = text.find("context_inspect {");
  check(at != std::string::npos, "the text names a context_inspect call:\n" + text);
  const auto open = at + std::string_view{"context_inspect "}.size();
  const auto close = text.find("}}", open);
  check(close != std::string::npos, "the call is complete");
  const auto call = unwrap(parse_json(text.substr(open, close + 2 - open)));
  const auto &query = field(call, "query");
  std::size_t read = 0;
  for (const auto &[id, item] : originals) {
    if (text.find(id) == std::string::npos)
      continue;
    Value::Object asked;
    for (const auto &[key, value] : query.object())
      asked.emplace_back(key, key == "entry" ? Value{id} : value);
    const auto page = context.inspect(Value{std::move(asked)});
    const auto original = unwrap(parse_json(unhex(string_field(page, "bytes"))));
    check(field(original, "id") == Value{id} && field(original, "item") == item,
          "the call reads back the original of " + id);
    ++read;
  }
  return read;
}
fs::path scratch() {
  std::string pattern =
      (fs::temp_directory_path() / "blackbird-compose-XXXXXX").string();
  check(::mkdtemp(pattern.data()) != nullptr, "mkdtemp");
  return fs::canonical(pattern);
}
std::uint64_t format_level_of(const Session &session) {
  std::uint64_t level = 0;
  for (const auto &program : session.root->current_programs())
    if (string_field(program, "label") == "context-format-level")
      level = std::get<std::uint64_t>(field(program, "level").number().storage());
  return level;
}
void persistence(const fs::path &directory) {
  const auto base = scratch();
  const auto rows = profile_rows(directory);
  std::size_t compared = 0;
  for (const auto &row : rows) {
    const auto family = row.families.front();
    Session session{base / row.name};
    Fixture items; // item builder only
    items.family = family;
    auto &context = *session.context;
    const auto compose_with =
        [&](const std::function<ComposeResult(const CutInput &)> &make) {
          const auto entries = field(session.context->view(), "entries").array();
          const auto units = session.context->units();
          const auto identity = session.log->issue();
          CutInput in{
              entries,
              units,
              row.profile,
              session.context->chapter(),
              identity.bytes(),
              CutSource::model,
              {},
              {},
              {},
              {},
              [family](const Value::Array &list) { export_items(family, list); }};
          set_head(in, session.context->head());
          auto composed = make(in);
          check(composed.has_value(),
                row.name + ": composes: " +
                    (composed.has_value() ? "" : composed.error().reason));
          (void)session.context->cut(composed->frame, identity);
        };
    const auto compare = [&](const std::string &what) {
      const auto live = Session::state(*session.context);
      check(session.probe(false) == live, row.name + " " + what + ": restore differs");
      check(session.probe(true) == live, row.name + " " + what + ": playback differs");
      ++compared;
    };
    context.append({items.prompt()}, "operator");
    const auto round = [&](bool reasoning, const std::string &name) {
      ++items.groups;
      std::string id;
      Value::Array round_items;
      if (reasoning)
        round_items.push_back(items.reasoning());
      round_items.push_back(items.call(name, id));
      session.context->append(round_items, "provider.branch:x");
      session.context->append({items.output(id)}, "workflow-continuation");
    };
    round(true, "exec");
    session.context->append({items.developer("CONTEXT_SIZE 1")},
                            "provider.request-state");
    round(true, "read_file");
    round(false, "exec");
    compose_with([&](const CutInput &in) {
      return compose_start(in, binding_for(items, row.profile));
    });
    compare("start");
    // A chapter frame is level 2 even without unit keys: an S1/S2 binary (level 1)
    // would drop the chapter table (layer 1, B1).
    check(format_level_of(session) == 2, row.name + ": a chapter frame is level 2");
    unwrap(session.context->checkpoint());
    // The two rounds archived next, as their originals, for one restore frame.
    std::vector<CutRestore> restores;
    {
      const auto entries = field(session.context->view(), "entries").array();
      for (const auto &u : session.context->units()) {
        if (u.handle != session.context->units()[1].handle &&
            u.handle != session.context->units()[3].handle)
          continue;
        CutRestore r{u.handle, {}, {}, false};
        for (const auto pos : u.entries) {
          const auto &item = field(entries[pos], "item");
          r.items.push_back(item);
          r.classes.push_back(type_is(item, "function_call_output") ? UnitClass::result
                                                                    : UnitClass::round);
        }
        restores.push_back(r);
      }
      check(restores.size() == 2, row.name + ": two rounds to archive");
    }
    std::map<std::string, Value> archived_items; // entry ID -> item, before the archive
    {
      const auto entries = field(session.context->view(), "entries").array();
      for (const auto &u : session.context->units())
        if (u.handle == restores[0].handle || u.handle == restores[1].handle)
          for (const auto pos : u.entries)
            archived_items.emplace(string_field(entries[pos], "id"),
                                   field(entries[pos], "item"));
    }
    compose_with([&](const CutInput &in) { return compose_archive(in, {1, 3}); });
    compare("archive");
    // Every call the note names returns the archived entry's original (A-M2).
    {
      std::string note;
      for (const auto &e : field(session.context->view(), "entries").array())
        if (item_text(field(e, "item")).starts_with(archive_note_marker))
          note = item_text(field(e, "item"));
      check(run_inspect_call(*session.context, note, archived_items) == archived_items.size(),
            row.name + ": the note's calls read back every archived entry\n" + note);
    }
    session.reopen();
    compare("reopen");
    compose_with([&](const CutInput &in) { return compose_restore(in, restores); });
    compare("restore");
    check(format_level_of(session) == 2,
          row.name + ": a frame with unit keys is level 2");
    // A summary, an edit (insert and replace) and, where the row's boundary can be
    // enabled, a boundary: each a frame that persists.
    round(true, "read_file");
    compose_with([&](const CutInput &in) {
      const auto &live = in.units;
      std::size_t pick = live.size();
      for (std::size_t u = live.size(); u > 0 && pick == live.size(); --u)
        if (live[u - 1].unit_class == UnitClass::round && !live[u - 1].open)
          pick = u - 1;
      for (const auto pos : live[pick].entries)
        archived_items.emplace(string_field(in.entries[pos], "id"),
                               field(in.entries[pos], "item"));
      return compose_summary(in, {pick, "read the file; nothing found"});
    });
    compare("summary");
    {
      std::string summary;
      for (const auto &e : field(session.context->view(), "entries").array())
        if (item_text(field(e, "item")).starts_with(summary_marker))
          summary = item_text(field(e, "item"));
      check(run_inspect_call(*session.context, summary, archived_items) >= 2,
            row.name + ": the summary's calls read back its entries\n" + summary);
    }
    compose_with([&](const CutInput &in) {
      CutEdit insert;
      insert.op = CutEdit::Op::insert;
      insert.after = 0;
      insert.item = items.developer("a note the model keeps");
      return compose_edit(in, {insert});
    });
    compare("edit");
    if (row.name != "conservative") {
      session.context->append({items.prompt()}, "operator");
      const auto enabled =
          with(row.profile, {"turn", "boundary", "enabled"}, Value{true});
      round(false, "exec");
      const auto entries = field(session.context->view(), "entries").array();
      const auto live = session.context->units();
      const auto identity = session.log->issue();
      CutInput in{entries,
                  live,
                  enabled,
                  session.context->chapter(),
                  identity.bytes(),
                  CutSource::model,
                  {},
                  {},
                  {},
                  {},
                  [family](const Value::Array &list) { export_items(family, list); }};
      set_head(in, session.context->head());
      auto composed = compose_archive(in, {live.size() - 1}, true);
      check(composed.has_value(),
            row.name + ": a boundary composes: " +
                (composed.has_value() ? "" : composed.error().reason));
      (void)session.context->cut(composed->frame, identity);
      compare("boundary");
    }
    session.context->append({items.developer("CONTEXT_SIZE 2")},
                            "provider.request-state");
    auto next = session.context->chapter().binding;
    next.tools_digest = "sha256:new-tools";
    compose_with([&](const CutInput &in) { return compose_prefix(in, next); });
    compare("prefix");
    unwrap(session.context->checkpoint());
    session.reopen();
    compare("final reopen");
    check(session.context->chapter().table.size() ==
                  (row.name == "conservative" ? 6U : 7U) &&
              session.context->chapter().binding == next,
          row.name + ": one chapter per cut, the last bound to the new tools");
  }
  std::error_code ignored;
  fs::remove_all(base, ignored);
  std::cout << "  persistence: " << compared << " three-way comparisons\n";
}

// ---- Cost: composing over a large context holds refs, not copies.
void measure(const fs::path &directory) {
  const auto rows = profile_rows(directory);
  for (const auto &row : rows) {
    if (row.name != "anthropic-messages-prefix-checked")
      continue;
    Fixture f;
    ChapterState chapter;
    f.add_prompt();
    for (int i = 0; i < 4000; ++i)
      f.add_round(true, i % 3 == 0, {"exec"});
    auto units = f.live_units();
    auto start =
        compose_start(input_for(f, units, row.profile, chapter, f.next_record()),
                      binding_for(f, row.profile));
    apply_into(f, chapter, start->frame);
    units = f.live_units();
    std::vector<std::size_t> chosen;
    for (std::size_t u = 10; u < 400; u += 2)
      chosen.push_back(u);
    // Without the codec check, then with it (S3b binds the route's render).
    auto plain = input_for(f, units, row.profile, chapter, f.next_record());
    plain.validate = {};
    const auto t0 = std::chrono::steady_clock::now();
    auto cut = compose_archive(plain, chosen);
    const auto t1 = std::chrono::steady_clock::now();
    const auto checked = compose_archive(
        input_for(f, units, row.profile, chapter, f.next_record()), chosen);
    const auto t1b = std::chrono::steady_clock::now();
    check(checked.has_value(), "the checked compose accepts");
    const auto t1c = std::chrono::steady_clock::now();
    const auto after = apply_frame(cut->frame, f.entries);
    const auto t2 = std::chrono::steady_clock::now();
    const auto apply_ms = std::chrono::duration<double, std::milli>(t2 - t1c).count();
    const auto packet = unwrap(encode_packet_string(encode_cut_frame(cut->frame)));
    std::cout << "  measure: " << f.entries.size() << " entries, " << chosen.size()
              << " units archived: compose "
              << std::chrono::duration<double, std::milli>(t1 - t0).count()
              << " ms (with the codec check "
              << std::chrono::duration<double, std::milli>(t1b - t1).count()
              << " ms), apply " << apply_ms << " ms, frame " << packet.size()
              << " bytes (context " << cut->frame.sizes.before << " bytes), "
              << after.size() << " left\n";
  }
}

int run(std::string_view name, const std::function<void()> &body) {
  try {
    body();
    std::cout << "PASS " << name << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cout << "FAIL " << name << ": " << error.what() << '\n';
  } catch (const Error &error) {
    std::cout << "FAIL " << name << ": " << error_name(error.code) << " ("
              << error.detail << ")\n";
  }
  return 1;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: cut_compose_test CASE PROFILE_DIRECTORY\n";
    return 2;
  }
  const std::string_view which = argv[1];
  const fs::path directory = argv[2];
  const std::map<std::string_view, std::function<void(const fs::path &)>> cases{
      {"turn_state", turn_states},   {"removal", removal_and_prefix},
      {"reasoning", reasoning_spec}, {"templates", templates},
      {"unit_keys", unit_keys},      {"chapter", chapter_section_case},
      {"persistence", persistence},  {"measure", measure}};
  const auto found = cases.find(which);
  if (found == cases.end()) {
    std::cerr << "unknown case\n";
    return 2;
  }
  return run(which, [&] { found->second(directory); });
}
