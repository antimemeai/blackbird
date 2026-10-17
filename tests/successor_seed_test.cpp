#include "blackbird/context.hpp"
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
    std::filesystem::remove_all(path);
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
}
