// Context persistence (unit S1, CONTEXT_LIFECYCLE.md §4.2-§4.5).
// Oracles:
// - model: an independent list model applying the same frames agrees with
//   apply_frame, refusals included;
// - specification: each design §4.3 refusal and each malformed frame is corrupt;
// - existing re-insertion keeps the ID, the original and the creating record;
// - metamorphic: over generated sessions, after every operation, the live state
//   equals snapshot restore plus suffix playback equals whole-history playback,
//   across reopens of the journal;
// - a maintenance checkpoint fired inside the cut writer's record call leaves the
//   state consistent (ASan on Linux catches a dangling reference);
// - the format-level record is written once, and a higher level is refused;
// - units and their classes (unit S2) agree live, restored and played back.
#include "blackbird/context.hpp"
#include "blackbird/context_cut.hpp"
#include "blackbird/context_units.hpp"
#include "blackbird/journal.hpp"
#include "blackbird/saved_state.hpp"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <list>
#include <random>
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
template <class F> bool refused(F &&body) {
  try {
    body();
  } catch (const Error &error) {
    return error.code == ErrorCode::corrupt;
  }
  return false;
}
template <class T> T identity(unsigned n) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(n);
  bytes[15] = static_cast<std::byte>(n >> 8);
  return unwrap(T::from_bytes(bytes));
}
std::string hex(const IdentityBytes &bytes) { return hex_identity(bytes); }
// The ref an entry ID names; a test fixture never holds another ID.
ContextRef ref_of(std::string_view id) {
  const auto ref = parse_entry_id(id);
  if (!ref)
    throw std::runtime_error("entry ID without a ref: " + std::string{id});
  return *ref;
}
IdentityBytes revision_bytes(unsigned n) {
  IdentityBytes bytes{};
  bytes[3] = static_cast<std::byte>(n & 255U);
  bytes[4] = static_cast<std::byte>(n >> 8);
  bytes[9] = std::byte{0x5a};
  return bytes;
}
Value item(std::string_view text, std::string_view role = "assistant") {
  return Value::object(
      {{"role", Value{std::string{role}}}, {"content", Value{std::string{text}}}});
}
Value entry(const ContextRef &ref, Value body) {
  return Value::object({{"id", Value{entry_id(ref)}}, {"item", std::move(body)}});
}

// ---- The model: entries as a list of (ID text, item), written independently of
// apply_frame from design §4.3, with string IDs and std::list positions.
struct Model {
  std::list<std::pair<std::string, Value>> entries;
};
// The model's own reading of design §4.2: an entry ID is "<32 hex>.<decimal>", the
// CRC input is the 16 identity bytes then the ordinal as 4 little-endian bytes.
std::array<std::byte, 20> model_ref_bytes(const std::string &id) {
  std::array<std::byte, 20> bytes{};
  for (std::size_t i = 0; i < 16; ++i)
    bytes[i] = static_cast<std::byte>(std::stoul(id.substr(i * 2, 2), nullptr, 16));
  const auto ordinal = std::stoul(id.substr(33));
  for (unsigned i = 0; i < 4; ++i)
    bytes[16 + i] = static_cast<std::byte>((ordinal >> (8 * i)) & 255U);
  return bytes;
}
std::optional<Model> model_apply(const Model &before, const CutFrame &frame) {
  if (!frame.accepted)
    return std::nullopt; // a refused or staged frame applies nothing
  Model model = before;
  {
    std::set<std::string> input;
    for (const auto &e : model.entries)
      if (!input.insert(e.first).second)
        return std::nullopt; // two entries under one ID
  }
  std::set<std::string> removed;
  std::map<std::string, Value> taken;
  for (const auto *list : {&frame.archived, &frame.swept, &frame.reasoning})
    for (const auto &ref : *list) {
      const auto id = entry_id(ref);
      const auto at = std::find_if(model.entries.begin(), model.entries.end(),
                                   [&](const auto &e) { return e.first == id; });
      if (at == model.entries.end() || !removed.insert(id).second)
        return std::nullopt;
    }
  for (auto at = model.entries.begin(); at != model.entries.end();)
    if (removed.contains(at->first)) {
      taken.emplace(at->first, at->second);
      at = model.entries.erase(at);
    } else
      ++at;
  std::set<std::string> replaced;
  for (const auto &r : frame.replace) {
    const auto id = entry_id(r.ref);
    const auto at = std::find_if(model.entries.begin(), model.entries.end(),
                                 [&](const auto &e) { return e.first == id; });
    if (at == model.entries.end() || !replaced.insert(id).second)
      return std::nullopt;
    at->second = r.item;
  }
  std::set<std::string> anchors, used_existing;
  std::set<std::uint32_t> used_fresh;
  for (const auto &insert : frame.insert) {
    const auto anchor = insert.anchor ? entry_id(*insert.anchor) : std::string{"start"};
    if (!anchors.insert(anchor).second)
      return std::nullopt;
    auto at = model.entries.begin();
    if (insert.anchor) {
      at = std::find_if(model.entries.begin(), model.entries.end(),
                        [&](const auto &e) { return e.first == anchor; });
      if (at == model.entries.end())
        return std::nullopt;
      ++at;
    }
    if (const auto *fresh = std::get_if<std::uint32_t>(&insert.entry)) {
      if (*fresh >= frame.originals.size() || !used_fresh.insert(*fresh).second)
        return std::nullopt;
      model.entries.insert(at, {frame.revision + "." + std::to_string(*fresh),
                                frame.originals[*fresh].item});
    } else {
      const auto id = entry_id(std::get<ContextRef>(insert.entry));
      if (!taken.contains(id) || !used_existing.insert(id).second)
        return std::nullopt;
      model.entries.insert(at, {id, taken.at(id)});
    }
  }
  if (used_fresh.size() != frame.originals.size())
    return std::nullopt;
  std::set<std::string> ids;
  for (const auto &e : model.entries)
    if (!ids.insert(e.first).second)
      return std::nullopt;
  if (frame.result) {
    std::uint32_t crc = 0;
    for (const auto &e : model.entries)
      crc = crc32c(model_ref_bytes(e.first), crc);
    if (frame.result->count != model.entries.size() || frame.result->crc32c != crc)
      return std::nullopt;
  }
  return model;
}
Value::Array model_entries(const Model &model) {
  Value::Array out;
  for (const auto &[id, body] : model.entries)
    out.push_back(Value::object({{"id", Value{id}}, {"item", body}}));
  return out;
}

void model_oracle() {
  std::mt19937 random{2026102001};
  std::size_t applied = 0, refusals = 0;
  for (unsigned round = 0; round < 4000; ++round) {
    Model model;
    std::vector<ContextRef> refs;
    const auto count = random() % 9;
    for (unsigned i = 0; i < count; ++i) {
      const ContextRef ref{revision_bytes(random() % 3), static_cast<std::uint32_t>(i)};
      refs.push_back(ref);
      model.entries.emplace_back(entry_id(ref), item("e" + std::to_string(i)));
    }
    // A frame valid by construction, then (half the time) one corruption; the
    // model, not the generator, decides what a corruption leaves valid.
    std::shuffle(refs.begin(), refs.end(), random);
    CutFrame frame;
    frame.revision = hex(revision_bytes(9));
    std::size_t at = 0;
    for (auto *list : {&frame.archived, &frame.swept, &frame.reasoning})
      for (auto n = random() % 2; n > 0 && at < refs.size(); --n)
        list->push_back(refs[at++]);
    std::vector<ContextRef> removed{refs.begin(),
                                    refs.begin() + static_cast<std::ptrdiff_t>(at)};
    std::vector<ContextRef> kept{refs.begin() + static_cast<std::ptrdiff_t>(at),
                                 refs.end()};
    for (std::size_t i = 0; i < kept.size() && i < 2; ++i)
      if (random() % 2)
        frame.replace.push_back({kept[i], item("replaced" + std::to_string(i))});
    const auto originals = static_cast<std::uint32_t>(random() % 3);
    for (std::uint32_t i = 0; i < originals; ++i)
      frame.originals.push_back({item("fresh" + std::to_string(i)), 1});
    std::vector<std::optional<ContextRef>> anchors{std::nullopt};
    for (const auto &ref : kept)
      anchors.push_back(ref);
    std::vector<std::variant<std::uint32_t, ContextRef>> placed;
    for (std::uint32_t i = 0; i < originals; ++i)
      placed.emplace_back(i);
    for (const auto &ref : removed)
      if (random() % 2)
        placed.emplace_back(ref);
    std::shuffle(placed.begin(), placed.end(), random);
    for (const auto &what : placed) {
      if (anchors.empty())
        break;
      const auto pick = random() % anchors.size();
      frame.insert.push_back({anchors[pick], what});
      anchors.erase(anchors.begin() + static_cast<std::ptrdiff_t>(pick));
      // A placed entry can anchor a later insert.
      if (const auto *fresh = std::get_if<std::uint32_t>(&what))
        anchors.push_back(ContextRef{revision_bytes(9), *fresh});
      else
        anchors.push_back(std::get<ContextRef>(what));
    }
    if (random() % 2) {
      if (const auto valid = model_apply(model, frame))
        frame.result = measure_entries(model_entries(*valid));
    }
    const ContextRef absent{revision_bytes(7), 3};
    if (random() % 2)
      switch (random() % 9) {
      case 0:
        frame.archived.push_back(absent);
        break;
      case 1:
        if (!removed.empty())
          frame.swept.push_back(removed.front());
        break;
      case 2:
        if (!removed.empty())
          frame.replace.push_back({removed.front(), item("late")});
        break;
      case 3:
        if (!frame.insert.empty() && !removed.empty())
          frame.insert.back().anchor = removed.front();
        break;
      case 4:
        if (frame.insert.size() > 1)
          frame.insert.back().anchor = frame.insert.front().anchor;
        break;
      case 5:
        frame.originals.push_back({item("unused"), 0});
        break;
      case 6:
        if (!frame.insert.empty())
          frame.insert.back().entry = std::uint32_t{7};
        break;
      case 7:
        if (!kept.empty())
          frame.insert.push_back({std::nullopt, kept.front()});
        break;
      default:
        if (frame.result)
          frame.result->crc32c ^= 1U;
        else
          frame.result = CutResult{};
        break;
      }
    if (random() % 16 == 0 && !model.entries.empty())
      model.entries.push_back(model.entries.front()); // a duplicate input ID
    if (random() % 16 == 0) {
      frame.accepted = false;
      frame.reason = "staged";
    }
    const auto input = model_entries(model);
    const auto expected = model_apply(model, frame);
    std::optional<Value::Array> actual;
    bool corrupt = false;
    try {
      actual = apply_frame(frame, input);
    } catch (const Error &error) {
      check(error.code == ErrorCode::corrupt, "apply refuses only with corrupt");
      corrupt = true;
    }
    check(corrupt == !expected.has_value(),
          "round " + std::to_string(round) + ": apply " +
              (corrupt ? "refused" : "accepted") + " where the model " +
              (expected ? "accepted" : "refused"));
    if (expected) {
      check(*actual == model_entries(*expected),
            "round " + std::to_string(round) + ": apply result differs from model");
      ++applied;
    } else
      ++refusals;
  }
  check(applied > 500 && refusals > 500, "the generator reaches both outcomes (" +
                                             std::to_string(applied) + " applied, " +
                                             std::to_string(refusals) + " refused)");
  std::cout << "  model: " << applied << " applied, " << refusals << " refused\n";
}

// ---- Each design §4.3 refusal, and malformed frames, are corrupt.
void refusals() {
  const ContextRef a{revision_bytes(1), 0}, b{revision_bytes(1), 1},
      c{revision_bytes(2), 0}, absent{revision_bytes(6), 0};
  const Value::Array entries{entry(a, item("a")), entry(b, item("b")),
                             entry(c, item("c"))};
  const auto base = [] {
    CutFrame f;
    f.revision = hex(revision_bytes(9));
    return f;
  };
  struct Case {
    const char *name;
    std::function<void(CutFrame &)> make;
  };
  const std::vector<Case> cases{
      {"archived and swept overlap",
       [&](CutFrame &f) {
         f.archived = {a};
         f.swept = {a};
       }},
      {"reasoning names an absent ref", [&](CutFrame &f) { f.reasoning = {absent}; }},
      {"archived lists a ref twice", [&](CutFrame &f) { f.archived = {b, b}; }},
      {"replace names an absent ref",
       [&](CutFrame &f) { f.replace = {{absent, item("x")}}; }},
      {"replace names a removed ref",
       [&](CutFrame &f) {
         f.archived = {a};
         f.replace = {{a, item("x")}};
       }},
      {"replace names a ref twice",
       [&](CutFrame &f) { f.replace = {{a, item("x")}, {a, item("y")}}; }},
      {"replace item is not an object",
       [&](CutFrame &f) { f.replace = {{a, Value{"text"}}}; }},
      {"insert anchor absent",
       [&](CutFrame &f) {
         f.originals = {{item("n"), 0}};
         f.insert = {{absent, std::uint32_t{0}}};
       }},
      {"insert anchor removed by the frame",
       [&](CutFrame &f) {
         f.archived = {c};
         f.originals = {{item("n"), 0}};
         f.insert = {{c, std::uint32_t{0}}};
       }},
      {"two inserts name one anchor",
       [&](CutFrame &f) {
         f.originals = {{item("n"), 0}, {item("m"), 0}};
         f.insert = {{a, std::uint32_t{0}}, {a, std::uint32_t{1}}};
       }},
      {"two inserts at start",
       [&](CutFrame &f) {
         f.originals = {{item("n"), 0}, {item("m"), 0}};
         f.insert = {{std::nullopt, std::uint32_t{0}},
                     {std::nullopt, std::uint32_t{1}}};
       }},
      {"fresh index out of range",
       [&](CutFrame &f) {
         f.originals = {{item("n"), 0}};
         f.insert = {{a, std::uint32_t{1}}};
       }},
      {"fresh original inserted twice",
       [&](CutFrame &f) {
         f.originals = {{item("n"), 0}};
         f.insert = {{a, std::uint32_t{0}}, {b, std::uint32_t{0}}};
       }},
      {"fresh original never inserted",
       [&](CutFrame &f) { f.originals = {{item("n"), 0}}; }},
      {"existing not removed by the frame",
       [&](CutFrame &f) { f.insert = {{std::nullopt, a}}; }},
      {"existing re-inserted twice",
       [&](CutFrame &f) {
         f.archived = {a};
         f.insert = {{std::nullopt, a}, {c, a}};
       }},
      {"result count differs",
       [&](CutFrame &f) {
         f.result = measure_entries(entries);
         ++f.result->count;
       }},
      {"result CRC32C differs",
       [&](CutFrame &f) {
         f.result = measure_entries(entries);
         f.result->crc32c ^= 0x80000000U;
       }},
      {"fresh ID collides with a present entry",
       [&](CutFrame &f) {
         f.revision = hex(revision_bytes(1));
         f.originals = {{item("n"), 0}};
         f.insert = {{c, std::uint32_t{0}}};
       }},
      {"a refused frame does not apply",
       [&](CutFrame &f) {
         f.accepted = false;
         f.reason = "staged";
       }},
  };
  for (const auto &one : cases) {
    auto frame = base();
    one.make(frame);
    check(refused([&] { (void)apply_frame(frame, entries); }),
          std::string{"not refused: "} + one.name);
  }
  // The positive control: the same rules accept a well-formed frame, and the
  // writer's chained form for a removed natural anchor.
  auto frame = base();
  frame.archived = {c};
  frame.originals = {{item("note"), 2}, {item("boundary"), 3}};
  frame.insert = {{b, std::uint32_t{0}},
                  {ContextRef{revision_bytes(9), 0}, std::uint32_t{1}},
                  {std::nullopt, c}};
  const auto next = apply_frame(frame, entries);
  check(next.size() == 5 && string_field(next[0], "id") == entry_id(c) &&
            string_field(next[3], "id") == hex(revision_bytes(9)) + ".0" &&
            string_field(next[4], "id") == hex(revision_bytes(9)) + ".1",
        "a well-formed frame applies in listed order");
  // Decode refusals: every field has a closed shape.
  frame.result = measure_entries(next);
  frame.base = frame.observed = hex(revision_bytes(1));
  const auto packet = encode_cut_frame(frame);
  check(apply_frame(decode_cut_frame(packet), entries) == next,
        "encode and decode round trip applies identically");
  const auto mutate = [&](std::string_view key, Value value) {
    auto copy = packet;
    bool found = false;
    for (auto &[k, v] : copy.object())
      if (k == key) {
        v = value;
        found = true;
      }
    if (!found)
      copy.object().emplace_back(std::string{key}, value);
    return copy;
  };
  const std::vector<std::pair<const char *, Value>> malformed{
      {"unknown op", mutate("op", Value{"splice"})},
      {"wrong format", mutate("format", Value{"cut-delta-v0"})},
      {"unknown kind", mutate("kind", Value{"sideways"})},
      {"duplicate change",
       mutate("changes", Value{Value::Array{Value{"archive"}, Value{"archive"}}})},
      {"unknown change", mutate("changes", Value{Value::Array{Value{"shrink"}}})},
      {"unknown key", mutate("extra", Value{true})},
      {"ref as ID text", mutate("archived", Value{Value::Array{Value{entry_id(c)}}})},
      {"ref ordinal over u32",
       mutate("archived", Value{Value::Array{Value{
                              Value::Array{Value{hex(revision_bytes(2))},
                                           Value{Number{std::uint64_t{1} << 32U}}}}}})},
      {"revision not hex", mutate("revision", Value{"not-a-revision"})},
      {"accepted not bool", mutate("accepted", Value{"yes"})},
      {"result missing",
       [&] {
         auto copy = packet;
         std::erase_if(copy.object(),
                       [](const auto &p) { return p.first == "result"; });
         return copy;
       }()},
      {"class over a byte",
       mutate("originals",
              Value{Value::Array{Value::object(
                  {{"item", item("x")}, {"class", Value{Number{256}}}})}})},
      {"insert with fresh and existing",
       mutate("insert",
              Value{Value::Array{Value::object({{"anchor", Value{"start"}},
                                                {"fresh", Value{Number{0}}},
                                                {"existing", encode_ref(c)}})}})},
      {"chapter ordinal zero",
       mutate("chapter", Value::object({{"ordinal", Value{Number{0}}},
                                        {"previous", Value{}},
                                        {"binding", Value::object({})}}))},
  };
  for (const auto &[name, bad] : malformed)
    check(refused([&] { (void)decode_cut_frame(bad); }),
          std::string{"decode accepted: "} + name);
  CutFrame repair;
  repair.op = CutOp::repair;
  repair.revision = repair.base = repair.observed = hex(revision_bytes(4));
  repair.skipped = {hex(revision_bytes(5))};
  repair.result = CutResult{};
  const auto repair_packet = encode_cut_frame(repair);
  (void)decode_cut_frame(repair_packet);
  auto empty_skip = repair;
  empty_skip.skipped.clear();
  check(refused([&] { (void)decode_cut_frame(encode_cut_frame(empty_skip)); }),
        "a repair frame must skip something");
  auto model_cause = repair;
  model_cause.cause.source = CutSource::model;
  check(refused([&] { (void)decode_cut_frame(encode_cut_frame(model_cause)); }),
        "only the operator approves a repair frame");
  auto moving = repair;
  moving.insert = {{std::nullopt, c}};
  check(refused([&] { (void)decode_cut_frame(encode_cut_frame(moving)); }),
        "a repair frame re-inserts nothing it removed");
}

// ---- A session directory with one journal.
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
// Units and their classes (unit S2) as text: they persist with the context.
Value units_text(const ContextStore &context) {
  const auto entries = field(context.view(), "entries").array();
  std::string out;
  for (const auto &unit : context.units()) {
    out += unit.handle + ":" + std::string{class_name(unit.unit_class)};
    for (const auto position : unit.entries)
      out += " " + string_field(entries[position], "id");
    out += unit.open ? " open" : "";
    out += unit.superseded ? " superseded" : "";
    out += unit.pinned ? " pinned\n" : "\n";
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
    // Saved identity gives a snapshot its semantic coverage, so reopen is compact.
    log->record(ApplicationChannel::program,
                Value::object({{"label", Value{"session.identity"}}}));
    context = std::make_unique<ContextStore>(*log);
  }
  void reopen(ContextOpen open = {}) {
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
    context = std::make_unique<ContextStore>(*log, open);
  }
  Value probe(bool whole) const {
    ContextStore other{*log, ContextOpen{.whole_history = whole, .probe = true}};
    return Value::object({{"view", other.view()},
                          {"originals", other.originals()},
                          {"units", units_text(other)},
                          {"read_only", Value{other.read_only()}}});
  }
};
fs::path scratch() {
  std::string pattern = (fs::temp_directory_path() / "blackbird-cut-XXXXXX").string();
  check(::mkdtemp(pattern.data()) != nullptr, "mkdtemp");
  return fs::canonical(pattern);
}
std::vector<ContextRef> live_refs(const ContextStore &context) {
  std::vector<ContextRef> refs;
  for (const auto &e : field(context.view(), "entries").array())
    refs.push_back(ref_of(string_field(e, "id")));
  return refs;
}
Value original_of(const ContextStore &context, const std::string &id) {
  const auto page = context.inspect(Value::object(
      {{"kind", Value{"originals"}}, {"entry", Value{id}}, {"format", Value{"json"}}}));
  return field(page, "bytes");
}

// ---- Existing re-insertion keeps the ID, the original and the creating record.
void existing_reinsert(const fs::path &base) {
  Session session{base / "reinsert"};
  auto &context = *session.context;
  context.append({item("a"), item("b"), item("c")}, "fixture");
  context.append({item("d")}, "fixture");
  const auto refs = live_refs(context);
  const auto a = refs[0], b = refs[1], d = refs[3];
  const auto a_original = original_of(context, entry_id(a));
  const auto b_original = original_of(context, entry_id(b));
  const auto index_before = context.inspect(
      Value::object({{"kind", Value{"index"}}, {"format", Value{"json"}}}));
  CutFrame move;
  move.kind = CutKind::archival;
  move.changes = cut_archive;
  move.cause.source = CutSource::model;
  move.archived = {a};
  move.insert = {{d, a}};
  move.replace = {{b, item("b edited")}};
  (void)context.cut(move);
  const auto entries = field(context.view(), "entries").array();
  check(entries.size() == 4 && string_field(entries.back(), "id") == entry_id(a) &&
            field(entries.back(), "item") == item("a"),
        "the moved entry keeps its ID and item at its new place");
  check(original_of(context, entry_id(a)) == a_original,
        "the moved entry keeps its original");
  check(original_of(context, entry_id(b)) == b_original &&
            field(entries[0], "item") == item("b edited"),
        "replace changes the item, not the original");
  const auto index_after = context.inspect(
      Value::object({{"kind", Value{"index"}}, {"format", Value{"json"}}}));
  check(field(index_after, "bytes") == field(index_before, "bytes"),
        "no new capture: the creating records stay");
  // Fresh originals of a cut resolve by ID through playback and the archive.
  CutFrame add;
  add.kind = CutKind::archival;
  add.changes = cut_summary;
  add.originals = {{item("summary note", "developer"), 4}};
  add.insert = {{std::nullopt, std::uint32_t{0}}};
  (void)context.cut(add);
  const auto note =
      string_field(field(context.view(), "entries").array().front(), "id");
  const auto view = context.view();
  check(session.probe(false) == session.probe(true), "playback agrees after cuts");
  unwrap(session.context->checkpoint());
  // The saved entries: refs for entries equal to their originals, inline for b.
  const auto &saved = field(session.root->saved_state()->context, "entries").array();
  std::size_t inline_entries = 0;
  for (const auto &descriptor : saved)
    if (std::holds_alternative<Value::Object>(descriptor.value())) {
      ++inline_entries;
      check(string_field(descriptor, "id") == entry_id(b),
            "only the replaced entry is written inline, not " +
                string_field(descriptor, "id") + " " +
                unwrap(format_value(descriptor)) + " (b is " + entry_id(b) + ")");
    }
  check(saved.size() == 5 && inline_entries == 1, "v2 snapshot holds refs");
  session.reopen();
  check(session.context->view() == view && session.root->compact_recovery() &&
            session.context->open_report().playback ==
                ContextOpenReport::Playback::suffix,
        "compact restore from refs equals the live view");
  check(original_of(*session.context, entry_id(a)) == a_original &&
            !original_of(*session.context, note).string().empty(),
        "originals of moved and fresh entries resolve after restore");
  check(session.probe(true) == session.probe(false), "whole history equals restore");
}

// ---- Generated sessions: live = snapshot + suffix = whole history.
void metamorphic(const fs::path &base) {
  std::size_t operations = 0, compared = 0;
  for (unsigned seed = 0; seed < 6; ++seed) {
    std::mt19937 random{777U + seed};
    Session session{base / ("generated-" + std::to_string(seed))};
    std::size_t counter = 0;
    const auto text = [&] { return "t" + std::to_string(counter++); };
    const auto compare = [&](const std::string &what) {
      const auto live = Value::object({{"view", session.context->view()},
                                       {"originals", session.context->originals()},
                                       {"units", units_text(*session.context)},
                                       {"read_only", Value{false}}});
      check(session.probe(false) == live,
            "seed " + std::to_string(seed) + " after " + what +
                ": snapshot restore and suffix playback differ from live");
      check(session.probe(true) == live,
            "seed " + std::to_string(seed) + " after " + what +
                ": whole-history playback differs from live");
      ++compared;
    };
    for (unsigned step = 0; step < 70; ++step) {
      auto &context = *session.context;
      const auto refs = live_refs(context);
      const auto pick = [&] { return refs[random() % refs.size()]; };
      std::string what;
      switch (refs.empty() ? 0 : random() % 9) {
      case 0:
      case 1: {
        Value::Array items;
        for (auto n = 1 + random() % 3; n > 0; --n)
          items.push_back(item(text(), random() % 3 ? "assistant" : "user"));
        context.append(std::move(items), "generated");
        what = "append";
        break;
      }
      case 2: {
        auto view = context.view();
        auto &entries = view.object()[1].second.array();
        if (random() % 2)
          entries.erase(entries.begin() +
                        static_cast<std::ptrdiff_t>(random() % entries.size()));
        else {
          auto &one = entries[random() % entries.size()];
          one = Value::object({{"id", field(one, "id")}, {"item", item(text())}});
        }
        check(field(context.edit(view), "accepted") == Value{true}, "edit accepted");
        what = "edit";
        break;
      }
      case 3: {
        const auto originals = context.originals().array();
        context.restore(string_field(originals[random() % originals.size()], "id"));
        what = "restore";
        break;
      }
      case 4: {
        // Archive one assistant entry; pinned roles are refused, as they should be.
        const auto chosen = entry_id(pick());
        (void)context.manage(
            Value::object({{"base", Value{context.head()}},
                           {"mode", Value{"archive"}},
                           {"reason", Value{"generated"}},
                           {"source", Value{"test"}},
                           {"ids", Value{Value::Array{Value{chosen}}}}}));
        what = "manage";
        break;
      }
      case 5: {
        CutFrame frame;
        frame.kind = CutKind::archival;
        frame.cause.source = CutSource::model;
        std::set<ContextRef> removed;
        for (auto n = random() % 3; n > 0; --n)
          if (const auto ref = pick(); removed.insert(ref).second)
            frame.archived.push_back(ref);
        std::vector<ContextRef> kept;
        for (const auto &ref : refs)
          if (!removed.contains(ref))
            kept.push_back(ref);
        if (!kept.empty() && random() % 2)
          frame.replace.push_back({kept[random() % kept.size()], item(text())});
        std::optional<ContextRef> anchor;
        if (!kept.empty() && random() % 3)
          anchor = kept[random() % kept.size()];
        frame.originals.push_back({item(text(), "developer"), 1});
        frame.insert.push_back({anchor, std::uint32_t{0}});
        // Move one archived entry to the start (an existing re-insert); the start
        // is free when the fresh entry has an anchor.
        if (!frame.archived.empty() && anchor && random() % 2)
          frame.insert.push_back({std::nullopt, frame.archived.front()});
        (void)context.cut(frame);
        what = "cut";
        break;
      }
      case 6:
        unwrap(context.checkpoint());
        what = "checkpoint";
        break;
      case 7: {
        // A staged managed proposal settled at the workflow boundary.
        context.begin_workflow();
        (void)context.manage(
            Value::object({{"base", Value{context.head()}},
                           {"mode", Value{"summarize"}},
                           {"reason", Value{"generated"}},
                           {"source", Value{"test"}},
                           {"ids", Value{Value::Array{Value{entry_id(pick())}}}},
                           {"summary", item(text(), "developer")}}));
        context.append({item(text())}, "during");
        (void)context.finish_workflow(true);
        what = "workflow";
        break;
      }
      default:
        session.reopen();
        what = "reopen";
        break;
      }
      ++operations;
      compare(what);
    }
    session.reopen();
    compare("final reopen");
  }
  std::cout << "  metamorphic: " << operations << " operations, " << compared
            << " three-way comparisons\n";
}

// ---- The maintenance checkpoint fires inside the cut writer's record call.
void maintenance_inside_writer(const fs::path &base) {
  Session session{base / "maintenance"};
  session.context->append({item("a"), item("b"), item("c")}, "fixture");
  CutFrame first;
  first.kind = CutKind::archival;
  first.originals = {{item("level written here"), 0}};
  first.insert = {{std::nullopt, std::uint32_t{0}}};
  (void)session.context->cut(first); // writes the format level once
  unwrap(session.context->checkpoint());
  session.reopen();
  check(session.root->compact_recovery(), "maintenance runs in compact mode");
  for (unsigned round = 0; round < 3; ++round) {
    // 127 tail facts: the next append runs the maintenance checkpoint before it.
    while (session.root->tail_facts().size() < 127)
      session.log->record(ApplicationChannel::program,
                          Value::object({{"label", Value{"maintenance-filler"}}}));
    const auto boundary = session.root->saved_state()->boundary.sequence;
    const auto refs = live_refs(*session.context);
    CutFrame frame;
    frame.kind = CutKind::archival;
    frame.archived = {refs.front()};
    frame.replace = {{refs.back(), item("replaced " + std::to_string(round))}};
    frame.originals = {{item("fresh " + std::to_string(round)), 0}};
    frame.insert = {{refs.back(), std::uint32_t{0}}, {std::nullopt, refs.front()}};
    (void)session.context->cut(frame);
    check(session.root->saved_state()->boundary.sequence > boundary,
          "the maintenance checkpoint ran inside the writer");
    check(session.probe(false) == session.probe(true) &&
              field(session.probe(false), "view") == session.context->view(),
          "state after an in-writer checkpoint equals playback");
  }
  session.reopen();
  check(field(session.probe(true), "view") == session.context->view(),
        "reopen after in-writer checkpoints equals whole history");
}

// ---- The format-level record.
void format_level(const fs::path &base) {
  Session session{base / "level"};
  session.context->append({item("a")}, "fixture");
  // Program records labelled context-format-level, level 1, in the journal.
  const auto level_records = [&] {
    std::size_t count = 0;
    for (const auto &fact : session.root->tail_facts())
      if (const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
          record && record->channel == ApplicationChannel::program) {
        const auto packet = unwrap(read_packet(record->payload));
        if (string_field(packet, "label") == "context-format-level")
          count += field(packet, "level") == Value{Number{std::uint64_t{1}}} ? 1 : 99;
      }
    return count;
  };
  check(level_records() == 0, "no level before the first cut frame");
  CutFrame frame;
  frame.originals = {{item("n"), 0}};
  frame.insert = {{std::nullopt, std::uint32_t{0}}};
  (void)session.context->cut(frame);
  check(level_records() == 1, "the level record precedes the first cut frame");
  (void)session.context->cut(frame);
  check(level_records() == 1, "the level is written once");
  session.log->record(ApplicationChannel::program,
                      Value::object({{"label", Value{"context-format-level"}},
                                     {"level", Value{Number{std::uint64_t{2}}}}}));
  bool unsupported = false;
  try {
    ContextStore newer{*session.log, ContextOpen{.probe = true}};
  } catch (const Error &error) {
    unsupported = error.code == ErrorCode::unsupported;
  }
  check(unsupported, "a session at a higher level is refused");
}

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

int main() {
  const auto base = scratch();
  run("model-oracle", model_oracle);
  run("refusals", refusals);
  run("existing-reinsert", [&] { existing_reinsert(base); });
  run("metamorphic", [&] { metamorphic(base); });
  run("maintenance-inside-writer", [&] { maintenance_inside_writer(base); });
  run("format-level", [&] { format_level(base); });
  std::error_code ignored;
  if (failures == 0)
    fs::remove_all(base, ignored);
  else
    std::cout << "Fixtures kept in " << base.string() << '\n';
  return failures == 0 ? 0 : 1;
}
