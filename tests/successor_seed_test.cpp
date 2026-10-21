#include "blackbird/context.hpp"
#if !defined(S2_RED_OLD_CODE)
#include "blackbird/context_units.hpp"
#endif
#include "blackbird/journal_storage.hpp"
#include "blackbird/json.hpp"
#include <filesystem>
#include <iostream>
#include <source_location>
#include <unistd.h>
using namespace blackbird;
template <class T> T id(unsigned char n) {
  IdentityBytes b{};
  b[0] = std::byte{n};
  return unwrap(T::from_bytes(b));
}
void check(bool good, std::source_location where = std::source_location::current()) {
  if (!good) {
    std::cerr << "failed check at line " << where.line() << "\n";
    throw Error{ErrorCode::corrupt};
  }
}
Value seed() {
  return unwrap(parse_json(R"({"version":1,"source":{
    "session":"/retained/predecessor","environment":"11000000000000000000000000000000",
    "journal":"22000000000000000000000000000000","prefix_sequence":17,
    "prefix_end":8192,"context_revision":"33000000000000000000000000000000",
    "status":"unsettled","reason":"explicit capacity successor; no settlement established"},
    "entries":[
    {"id":"33000000000000000000000000000000.0","item":{"role":"developer","content":"Preserve originals; never replay uncertain effects"}},
    {"id":"33000000000000000000000000000000.1","item":{"type":"function_call","call_id":"old-call","name":"exec","arguments":"{\"command\":\"exit 99\"}"}},
    {"id":"33000000000000000000000000000000.2","item":{"type":"function_call_output","call_id":"old-call","output":"outcome unknown; consult original audit"}}]})"));
}
int main() {
  char name[] = "/tmp/arco-successor-XXXXXX";
  const auto path = mkdtemp(name);
  if (!path)
    return 2;
  const JournalHeader header{id<EnvironmentId>(4),
                             id<AuditStreamId>(5),
                             37,
                             {1024 * 1024, 4 * 1024 * 1024},
                             std::nullopt};
  const JournalCapacity capacity{8 * 1024 * 1024, 1000};
  try {
    std::string revision;
    const auto original = seed();
    {
      auto root =
          unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                           unwrap(NativeJournalDirectory::open(path))),
                                       "audit", header, capacity));
      AuditLog log{*root};
      ContextStore context{log};
      auto reject = [&](const Value &bad) {
        const auto cursor = root->cursor();
        bool refused = false;
        try {
          context.seed_successor(bad);
        } catch (const Error &) {
          refused = true;
        }
        check(refused && root->cursor().sequence == cursor.sequence &&
              root->cursor().end_offset == cursor.end_offset &&
              context.items().empty() && root->committed_facts().empty());
      };
      auto bad = original;
      bad.object().emplace_back("extra", Value{std::string(1024 * 1024, 'x')});
      reject(bad);
      auto raw = unwrap(dump_json(original));
      auto replace = [&](const std::string &from, const std::string &to) {
        auto text = raw;
        const auto at = text.find(from);
        check(at != std::string::npos);
        text.replace(at, from.size(), to);
        reject(unwrap(parse_json(text)));
      };
      replace("\"version\":1", "\"version\":2");
      replace("/retained/predecessor", "relative");
      replace("\"unsettled\"", "\"invented\"");
      replace("\"prefix_end\":8192", "\"prefix_end\":1");
      replace("33000000000000000000000000000000.1",
              "33000000000000000000000000000000.0");
      replace("\"function_call_output\"", "\"reasoning\"");
      replace("\"call_id\":\"old-call\",\"output\"",
              "\"call_id\":\"orphan\",\"output\"");
      replace("\"role\":\"developer\"", "\"role\":42");
      replace("\"type\":\"function_call\"", "\"type\":false");
      replace("\"name\":\"exec\"", "\"unused\":\"exec\"");
      replace("\"output\":", "\"missing_output\":");
      replace("\"content\":", "\"missing_content\":");
      replace("\"type\":\"function_call\"",
              "\"type\":\"function_call\",\"role\":\"user\"");
      replace("\"item\":{\"role\":\"developer\"", "\"item\":{\"type\":\"message\"");
      replace("\"arguments\":", "\"missing_arguments\":");
      auto enriched = raw;
      enriched.insert(
          enriched.size() - 2,
          R"(,{"id":"33000000000000000000000000000000.3","item":{"type":"reasoning","encrypted_content":"opaque-retained","summary":[]}})");
      ContextStore::validate_successor_seed(unwrap(parse_json(enriched)));
      auto native = raw;
      native.insert(
          native.size() - 2,
          R"(,{"id":"33000000000000000000000000000000.3","item":{"type":"reasoning","summary":[],"anthropic_message_id":"msg-original","anthropic_content":{"type":"thinking","thinking":"unchanged summary","signature":"opaque+/="}}},{"id":"33000000000000000000000000000000.4","item":{"type":"reasoning","summary":[],"anthropic_message_id":"msg-original","anthropic_content":{"type":"redacted_thinking","data":"opaque-redacted+/="}}})");
      ContextStore::validate_successor_seed(unwrap(parse_json(native)));
      for (const auto &[before, after] :
           {std::pair{std::string{"\"signature\":\"opaque+/=\""},
                      std::string{"\"signature\":\"\""}},
            std::pair{std::string{"\"data\":\"opaque-redacted+/=\""},
                      std::string{"\"data\":\"\""}},
            std::pair{std::string{"\"anthropic_message_id\":\"msg-original\""},
                      std::string{"\"anthropic_message_id\":\"\""}},
            std::pair{std::string{"\"type\":\"thinking\""},
                      std::string{"\"type\":\"text\""}}}) {
        auto malformed = native;
        malformed.replace(malformed.find(before), before.size(), after);
        reject(unwrap(parse_json(malformed)));
      }
      context.seed_successor(original);
      revision = context.head();
      check(root->committed_facts().size() == 2 && context.items().size() == 3);
      Value::Array expected;
      for (const auto &e : field(original, "entries").array())
        expected.push_back(field(e, "item"));
      check(context.items() == expected);
      const auto &event =
          std::get<ApplicationRecordEvent>(root->committed_facts()[1].event.body);
      auto packet = unwrap(read_packet(event.payload));
      check(field(field(packet, "lineage"), "source") == field(original, "source"));
      const auto &mapping = field(field(packet, "lineage"), "source_entries").array();
      const auto &fresh = field(packet, "originals").array();
      check(mapping.size() == fresh.size() && fresh.size() == 3);
      for (std::size_t i = 0; i < fresh.size(); ++i)
        check(mapping[i].string() ==
                  string_field(field(original, "entries").array()[i], "id") &&
              string_field(fresh[i], "id") != mapping[i].string());
      const auto cursor = root->cursor();
      bool refused = false;
      try {
        context.seed_successor(original);
      } catch (const Error &e) {
        refused = e.code == ErrorCode::conflict;
      }
      check(refused && root->cursor().sequence == cursor.sequence);
    }
    {
      auto root =
          unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(
                                         unwrap(NativeJournalDirectory::open(path))),
                                     "audit", header, capacity));
      unwrap(root->confirm_recovery());
      AuditLog log{*root};
      ContextStore context{log};
      check(context.head() == revision && context.items().size() == 3 &&
            root->committed_facts().size() == 2);
    }
    {
      auto root =
          unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                           unwrap(NativeJournalDirectory::open(path))),
                                       "too-small", header, {512, 1000}));
      AuditLog log{*root};
      ContextStore context{log};
      bool refused = false;
      try {
        context.seed_successor(original);
      } catch (const Error &e) {
        refused = e.code == ErrorCode::capacity;
      }
      check(refused && context.items().empty());
      for (const auto &fact : root->committed_facts())
        check(std::holds_alternative<IssuerReservationEvent>(fact.event.body));
    }
    // Unit S2 (design §5.2): a seeded context groups its entries by source round.
    // Each source revision prefix is its own round; seeded user, system and
    // developer entries are instructions; an output joins its call. The old code
    // bound every later entry of the one seed append to its first reasoning item, so
    // no source round could be archived alone (partial-round).
    const auto grouped = std::string{path} + "/grouped";
    std::filesystem::create_directory(grouped);
    std::filesystem::permissions(grouped, std::filesystem::perms::owner_all);
    {
      auto rounds = unwrap(parse_json(R"({"version":1,"source":{
        "session":"/retained/predecessor","environment":"11000000000000000000000000000000",
        "journal":"22000000000000000000000000000000","prefix_sequence":17,
        "prefix_end":8192,"context_revision":"4a000000000000000000000000000000",
        "status":"settled","reason":"successor seed"},
        "entries":[
        {"id":"44000000000000000000000000000000.0","item":{"role":"user","content":"Do the task"}},
        {"id":"45000000000000000000000000000000.0","item":{"type":"reasoning","encrypted_content":"R1","summary":[]}},
        {"id":"45000000000000000000000000000000.1","item":{"type":"function_call","call_id":"a","name":"exec","arguments":"{}"}},
        {"id":"46000000000000000000000000000000.0","item":{"type":"function_call_output","call_id":"a","output":"one"}},
        {"id":"47000000000000000000000000000000.0","item":{"role":"assistant","content":"step done"}},
        {"id":"48000000000000000000000000000000.0","item":{"role":"developer","content":"Keep going"}},
        {"id":"49000000000000000000000000000000.0","item":{"type":"reasoning","encrypted_content":"R2","summary":[]}},
        {"id":"49000000000000000000000000000000.1","item":{"type":"function_call","call_id":"b","name":"read_file","arguments":"{}"}},
        {"id":"4a000000000000000000000000000000.0","item":{"type":"function_call_output","call_id":"b","output":"two"}}]})"));
      auto root =
          unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                           NativeJournalDirectory::open(grouped))),
                                       "audit", header, capacity));
      AuditLog log{*root};
      ContextStore context{log};
      context.seed_successor(rounds);
      const auto entries = field(context.view(), "entries").array();
      const auto id = [&](std::size_t i) { return field(entries[i], "id"); };
      const auto archive = [&](Value::Array ids) {
        const auto result =
            context.manage(Value::object({{"base", Value{context.head()}},
                                          {"mode", Value{"archive"}},
                                          {"reason", Value{"seeded round"}},
                                          {"source", Value{"test"}},
                                          {"ids", Value{std::move(ids)}}}));
        return field(result, "accepted") == Value{true}
                   ? std::string{"accepted"}
                   : string_field(result, "reason");
      };
#if !defined(S2_RED_OLD_CODE)
      // Units: instruction 0, round {1,2,3}, round 4, instruction 5, round {6,7,8}.
      const auto units = context.units();
      const std::vector<std::pair<UnitClass, std::vector<std::size_t>>> expected{
          {UnitClass::instruction, {0}},
          {UnitClass::round, {1, 2, 3}},
          {UnitClass::round, {4}},
          {UnitClass::instruction, {5}},
          {UnitClass::round, {6, 7, 8}}};
      check(units.size() == expected.size());
      const auto stem = units[0].handle;
      for (std::size_t i = 0; i < units.size(); ++i)
        check(units[i].unit_class == expected[i].first &&
              units[i].entries == expected[i].second &&
              units[i].handle == stem.substr(0, stem.find('.')) + "." +
                                     std::to_string(expected[i].second.front()));
#endif
      check(archive({id(0)}) != "accepted"); // a seeded user entry is an instruction
      check(archive({id(1), id(2)}) == "partial-tool-group"); // its output stays
      if (const auto first = archive({id(1), id(2), id(3)}); first != "accepted") {
        std::cerr << "a seeded source round is not archivable alone: " << first << "\n";
        check(false);
      }
      check(archive({id(7), id(8)}) == "partial-round"); // reasoning 6 binds 7
      check(archive({id(4)}) == "accepted");
      check(archive({id(6), id(7), id(8)}) == "accepted");
      check(field(context.view(), "entries").array().size() == 2);
    }
    // Layer 1 (A-M1): a seed of a seeded session. Its source IDs share one prefix
    // (the predecessor's seed record), so only the unit each entry names keeps the
    // inherited rounds apart; a seed without units groups by that prefix.
    for (const bool named : {true, false}) {
      const auto dir = std::string{path} + (named ? "/units" : "/prefix");
      std::filesystem::create_directory(dir);
      std::filesystem::permissions(dir, std::filesystem::perms::owner_all);
      const std::string p = "50000000000000000000000000000000";
      const auto unit = [&](const char *key) {
        return named ? std::string{R"(,"unit":")"} + p + "." + key + "\"" : "";
      };
      auto again = unwrap(parse_json(
          std::string{R"({"version":1,"source":{
        "session":"/retained/successor","environment":"11000000000000000000000000000000",
        "journal":"22000000000000000000000000000000","prefix_sequence":3,
        "prefix_end":8192,"context_revision":"50000000000000000000000000000000",
        "status":"settled","reason":"successor of a successor"},
        "entries":[
        {"id":")"} +
          p + R"(.0","item":{"role":"user","content":"Do the task"})" + unit("0") +
          R"(},
        {"id":")" +
          p +
          R"(.1","item":{"type":"reasoning","encrypted_content":"R1","summary":[]})" +
          unit("1") + R"(},
        {"id":")" +
          p +
          R"(.2","item":{"type":"function_call","call_id":"a","name":"exec","arguments":"{}"})" +
          unit("1") + R"(},
        {"id":")" +
          p +
          R"(.3","item":{"type":"function_call_output","call_id":"a","output":"one"})" +
          unit("1") + R"(},
        {"id":")" +
          p +
          R"(.4","item":{"type":"reasoning","encrypted_content":"R2","summary":[]})" +
          unit("4") + R"(},
        {"id":")" +
          p +
          R"(.5","item":{"type":"function_call","call_id":"b","name":"exec","arguments":"{}"})" +
          unit("4") + R"(},
        {"id":")" +
          p +
          R"(.6","item":{"type":"function_call_output","call_id":"b","output":"two"})" +
          unit("4") + R"(}]})"));
      auto root =
          unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                           unwrap(NativeJournalDirectory::open(dir))),
                                       "audit", header, capacity));
      AuditLog log{*root};
      ContextStore context{log};
      context.seed_successor(again);
      const auto entries = field(context.view(), "entries").array();
      const auto result = context.manage(Value::object(
          {{"base", Value{context.head()}},
           {"mode", Value{"archive"}},
           {"reason", Value{"inherited round"}},
           {"source", Value{"test"}},
           {"ids", Value{Value::Array{field(entries[1], "id"), field(entries[2], "id"),
                                      field(entries[3], "id")}}}}));
      const auto reason = field(result, "accepted") == Value{true}
                              ? std::string{"accepted"}
                              : string_field(result, "reason");
      if (reason != (named ? "accepted" : "partial-round")) {
        std::cerr << (named ? "a named" : "an unnamed")
                  << " inherited round archived alone: " << reason << "\n";
        check(false);
      }
    }
    std::filesystem::remove_all(path);
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
}
