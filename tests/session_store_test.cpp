#include "blackbird/coding.hpp"
#include <chrono>
#include <iostream>
#include <unistd.h>
using namespace blackbird;
template <class T> T id(unsigned char n) {
  IdentityBytes b{};
  b[0] = std::byte{n};
  return unwrap(T::from_bytes(b));
}
void check(bool good) {
  if (!good)
    throw Error{ErrorCode::corrupt};
}
int main() {
  char name[] = "/tmp/arco-session-XXXXXX";
  const auto path = mkdtemp(name);
  if (!path)
    return 2;
  const JournalHeader header{id<EnvironmentId>(1),
                             id<AuditStreamId>(2),
                             3,
                             {1024 * 1024, 4 * 1024 * 1024},
                             std::nullopt};
  const JournalCapacity capacity{16 * 1024 * 1024, 10000};
  std::string actor, conversation, workflow;
  try {
    {
      auto root =
          unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                           unwrap(NativeJournalDirectory::open(path))),
                                       "audit", header, capacity));
      AuditLog log{*root};
      ContextStore context{log};
      log.record(ApplicationChannel::program,
                 Value::object({{"label", Value{"session.settings"}},
                                {"model", Value{"legacy"}},
                                {"effort", Value{"medium"}},
                                {"workflow", Value{"/absolute/legacy.lua"}}}));
      SessionStore store{log};
      check(store.settings().provider == "openai");
      store.save({"chosen", "high", "/absolute/turn.lua", "My session"});
      const auto count = root->committed_facts().size();
      bool refused = false;
      try {
        store.save({"invalid", "nonsense", "/absolute/turn.lua"});
      } catch (const Error &e) {
        refused = e.code == ErrorCode::invalid_range;
      }
      check(refused && root->committed_facts().size() == count &&
            store.settings().model == "chosen");
      refused = false;
      try {
        store.save({std::string(1025, 'x'), "high", "/absolute/turn.lua"});
      } catch (const Error &e) {
        refused = e.code == ErrorCode::invalid_range;
      }
      check(refused && root->committed_facts().size() == count);
      const auto identity = session_identity(log);
      actor = hex_identity(identity.actor.bytes());
      conversation = hex_identity(identity.conversation.bytes());
      workflow = hex_identity(identity.workflow.bytes());
      check(session_identity(log).conversation == identity.conversation);
      save_session_info(path, store.settings(), identity);
      const auto metadata_path = std::filesystem::path{path} / "session-info.bbm";
      const auto old_time =
          std::filesystem::file_time_type::clock::now() - std::chrono::hours{1};
      std::filesystem::last_write_time(metadata_path, old_time);
      const auto unchanged_time = std::filesystem::last_write_time(metadata_path);
      save_session_info(path, store.settings(), identity);
      check(std::filesystem::last_write_time(metadata_path) == unchanged_time);
      refused = false;
      try {
        store.save({"chosen", "high", "/absolute/turn.lua", std::string(129, 'x')});
      } catch (const Error &e) {
        refused = e.code == ErrorCode::invalid_range;
      }
      check(refused && store.settings().name == "My session");
      const auto audit_before = read_file(std::filesystem::path{path} / "audit");
      auto listed = list_sessions(path);
      check(field(listed, "sessions").array().size() == 1 &&
            string_field(field(listed, "sessions").array()[0], "metadata_status") ==
                "snapshot");
      check(string_field(field(field(listed, "sessions").array()[0], "configuration"),
                         "name") == "My session");
      write_file(std::filesystem::path{path} / "session-info.bbm",
                 std::string(1024 * 1024 + 1, 'x'));
      listed = list_sessions(path);
      check(string_field(field(listed, "sessions").array()[0], "metadata_status") ==
            "damaged");
      check(read_file(std::filesystem::path{path} / "audit") == audit_before);
      save_session_info(path, store.settings(), identity);
      auto routed = store.settings();
      routed.provider = "openrouter";
      routed.model = "vendor/chosen";
      store.save(routed);
      save_session_info(path, routed, identity);
      const auto routed_sessions = list_sessions(path);
      check(string_field(
                field(field(routed_sessions, "sessions").array()[0], "configuration"),
                "provider") == "openrouter");
      auto portable = routed;
      portable.provider = "paper";
      portable.model = "ultramarine-1-flash";
      portable.effort = "max";
      store.save(portable);
      save_session_info(path, portable, identity);
      check(SessionStore{log}.settings() == portable);
      const auto portable_sessions = list_sessions(path);
      check(string_field(
                field(field(portable_sessions, "sessions").array()[0], "configuration"),
                "provider") == "paper");
      auto pending = portable;
      pending.effort = "unrecognized-intent";
      pending.effort_pending = true;
      store.save(pending);
      save_session_info(path, pending, identity);
      check(SessionStore{log}.settings() == pending);
      const auto pending_snapshot = unwrap(decode_packet_string(
          read_file(std::filesystem::path{path} / "session-info.bbm")));
      check(field(pending_snapshot, "effort_pending") == Value{true});
      check(string_field(field(field(list_sessions(path), "sessions").array()[0],
                               "configuration"),
                         "effort") == "unrecognized-intent");
      store.save(routed);
      store.restart("Retain this note");
      const auto &fact = root->committed_facts().back();
      const auto &record = std::get<ApplicationRecordEvent>(fact.event.body);
      const auto packet = unwrap(read_packet(record.payload));
      const auto origin = "rrc:" + string_field(packet, "token");
      // Simulate a crash after the context append, before consuming the intent.
      context.append(
          {Value::object({{"role", Value{"user"}},
                          {"content", Value{"continue\nRetain this note"}}})},
          origin);
    }
    {
      auto root =
          unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(
                                         unwrap(NativeJournalDirectory::open(path))),
                                     "audit", header, capacity));
      unwrap(root->confirm_recovery());
      recover_coding_session(*root);
      AuditLog log{*root};
      ContextStore context{log};
      SessionStore store{log};
      check(store.settings() == SessionSettings{"vendor/chosen", "high",
                                                "/absolute/turn.lua", "My session",
                                                "openrouter"});
      const auto identity = session_identity(log);
      check(actor == hex_identity(identity.actor.bytes()) &&
            conversation == hex_identity(identity.conversation.bytes()) &&
            workflow == hex_identity(identity.workflow.bytes()));
      check(context.items().size() == 1 && store.resume(context) &&
            context.items().size() == 1);
      check(!store.resume(context));
      SessionStore replay{log};
      check(!replay.resume(context));
    }
    std::filesystem::remove_all(path);
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
}
