#include "blackbird/context.hpp"
#include "blackbird/json.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace blackbird;
#define CHECK(x)                                                                       \
  do {                                                                                 \
    if (!(x))                                                                          \
      throw std::runtime_error("context delta line " + std::to_string(__LINE__));      \
  } while (false)
template <class T> T id(unsigned char n) {
  IdentityBytes b{};
  b[0] = std::byte{n};
  return unwrap(T::from_bytes(b));
}
int main() {
  char pattern[] = "/tmp/blackbird-context-delta-XXXXXX";
  const auto path = mkdtemp(pattern);
  if (!path)
    return 2;
  try {
    const JournalHeader h{id<EnvironmentId>(1),
                          id<AuditStreamId>(2),
                          3,
                          {4 * 1024 * 1024, 16 * 1024 * 1024},
                          std::nullopt};
    const JournalCapacity cap{64 * 1024 * 1024, 10000};
    auto directory = [&] {
      return std::make_unique<NativeJournalDirectory>(
          unwrap(NativeJournalDirectory::open(path)));
    };
    Value final_view;
    {
      auto root = unwrap(RetainedState::create(directory(), "audit", h, cap));
      AuditLog log{*root};
      ContextStore ctx{log};
      log.retain_program("return 42", id<DefinitionGenerationId>(9));
      const auto captured_program = unwrap(root->fact(root->fact_count() - 2));
      const auto activated_program = unwrap(root->fact(root->fact_count() - 1));
      CHECK(captured_program.event.dependencies.size() == 1);
      CHECK(activated_program.record.sequence == captured_program.record.sequence + 1);
      CHECK((captured_program.event.dependencies ==
             std::vector<SourceReference>{{captured_program.record.journal,
                                           captured_program.record.sequence - 1}}));
      CHECK(unwrap(root->source(captured_program.event.dependencies[0])).size() == 9);
      ctx.append({Value::object({{"role", Value{"user"}},
                                 {"content", Value{std::string(65536, 'a')}}})},
                 "anchor");
      const auto anchor = field(ctx.view(), "entries");
      std::size_t packet_bytes = 0;
      for (unsigned i = 0; i <= 80; ++i) {
        const auto before = root->fact_count();
        const auto began = std::chrono::steady_clock::now();
        ctx.append({Value::object(
                       {{"role", Value{"assistant"}}, {"content", Value{"small"}}})},
                   "measure");
        const auto elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - began)
                                 .count();
        std::size_t current_bytes = 0, old_format_bytes = 0;
        for (std::size_t j = before; j < root->fact_count(); ++j) {
          const auto fact = unwrap(root->fact(j));
          const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
          if (!record || record->channel != ApplicationChannel::context)
            continue;
          const auto packet = unwrap(read_packet(record->payload));
          CHECK(string_field(packet, "format") == "append-delta-v1" &&
                !packet.find("entries"));
          CHECK(field(packet, "originals").array().size() == 1);
          current_bytes = record->payload.size();
          auto legacy_packet = packet;
          std::erase_if(legacy_packet.object(),
                        [](const auto &entry) { return entry.first == "format"; });
          legacy_packet.object().emplace_back("entries", field(ctx.view(), "entries"));
          old_format_bytes = unwrap(dump_json(legacy_packet)).size();
        }
        CHECK(current_bytes > 0 && current_bytes < 1024 &&
              old_format_bytes > current_bytes + 65536);
        if (i == 0)
          packet_bytes = current_bytes;
        CHECK(current_bytes == packet_bytes);
        if (i == 0 || i == 20 || i == 80)
          std::cout << "historical originals=" << i + 1
                    << " live_anchor=65536 append_packet=" << current_bytes
                    << " bytes old_format_reconstruction=" << old_format_bytes
                    << " append_ms=" << elapsed << '\n';
        auto candidate =
            Value::object({{"base", Value{ctx.head()}}, {"entries", anchor}});
        CHECK(field(ctx.edit(candidate), "accepted") == Value{true});
        CHECK(field(ctx.edit(candidate), "accepted") == Value{false});
      }
      // Allocation/protection failure before durable publication leaves presentation
      // and original indexes unchanged. Burned revision identities are permitted.
      const auto prior = ctx.view();
      const auto originals = ctx.originals();
      ctx.protect = [](const Value::Array &, const Value &, const RawEventRef &) {
        throw Error{ErrorCode::capacity};
      };
      bool failed = false;
      try {
        ctx.append({Value::object({{"role", Value{"assistant"}},
                                   {"content", Value{"not published"}}})},
                   "failure");
      } catch (const Error &e) {
        failed = e.code == ErrorCode::capacity;
      }
      CHECK(failed && ctx.view() == prior && ctx.originals() == originals);
      ctx.protect = {};
      // A legacy full append packet followed by another delta must still replay.
      const auto identity = log.issue();
      const auto revision = hex_identity(identity.bytes());
      const auto entry =
          Value::object({{"id", Value{revision + ".0"}},
                         {"item", Value::object({{"role", Value{"assistant"}},
                                                 {"content", Value{"legacy"}}})}});
      auto next = field(ctx.view(), "entries").array();
      next.push_back(entry);
      log.record(identity, ApplicationChannel::context,
                 Value::object({{"op", Value{"append"}},
                                {"base", Value{ctx.head()}},
                                {"observed", Value{ctx.head()}},
                                {"revision", Value{revision}},
                                {"accepted", Value{true}},
                                {"origin", Value{"legacy"}},
                                {"originals", Value{Value::Array{entry}}},
                                {"entries", Value{next}}}));
      ContextStore mixed{log};
      CHECK(field(mixed.view(), "entries").array() == next);
      mixed.append({Value::object({{"role", Value{"assistant"}},
                                   {"content", Value{"after legacy"}}})},
                   "delta");
      final_view = mixed.view();
    }
    {
      auto root = unwrap(RetainedState::open(directory(), "audit", h, cap));
      unwrap(root->confirm_recovery());
      AuditLog log{*root};
      ContextStore ctx{log};
      CHECK(ctx.view() == final_view);
      CHECK(ctx.originals().array().size() == 84);
      CHECK(field(ctx.inspect(Value::object({{"kind", Value{"history"}},
                                             {"format", Value{"json"}},
                                             {"limit", Value{Number{"1024"}}}})),
                  "total_bytes")
                .number()
                .text() != "0");
    }
    for (bool hybrid : {false, true}) {
      const auto bad_path = std::string{path} + (hybrid ? "/hybrid" : "/unknown");
      std::filesystem::create_directory(bad_path);
      std::filesystem::permissions(bad_path, std::filesystem::perms::owner_all);
      auto root =
          unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                           NativeJournalDirectory::open(bad_path))),
                                       "audit", h, cap));
      AuditLog log{*root};
      ContextStore ctx{log};
      ctx.append({}, "initial");
      const auto identity = log.issue();
      const auto revision = hex_identity(identity.bytes());
      auto packet =
          Value::object({{"op", Value{"append"}},
                         {"base", Value{ctx.head()}},
                         {"observed", Value{ctx.head()}},
                         {"revision", Value{revision}},
                         {"accepted", Value{true}},
                         {"originals", Value{Value::Array{}}},
                         {"format", Value{hybrid ? "append-delta-v1" : "unknown-v1"}}});
      if (hybrid)
        packet.object().emplace_back("entries", Value{Value::Array{}});
      log.record(identity, ApplicationChannel::context, packet);
      bool rejected = false;
      try {
        ContextStore invalid{log};
      } catch (const Error &e) {
        rejected = e.code == ErrorCode::corrupt;
      }
      CHECK(rejected);
    }
    std::filesystem::remove_all(path);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    std::filesystem::remove_all(path);
    return 1;
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
}
