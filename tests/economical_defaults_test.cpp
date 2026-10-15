#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include <iostream>
#include <source_location>
#include <unistd.h>
using namespace blackbird;
namespace {
void check(bool condition,
           std::source_location where = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error("economical observations line " +
                             std::to_string(where.line()));
}
template <class T> T id(unsigned char byte) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(byte);
  return unwrap(T::from_bytes(bytes));
}
class Provider final : public CodingProvider {
public:
  std::string file, marker;
  std::size_t calls = 0;
  Value snapshot;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &) override {
    ++calls;
    Value::Array output;
    if (calls == 1) {
      const auto call = [&](const char *name, const Value &args) {
        output.push_back(
            Value::object({{"type", Value{"function_call"}},
                           {"call_id", Value{std::to_string(output.size())}},
                           {"name", Value{name}},
                           {"arguments", Value{unwrap(dump_json(args))}}}));
      };
      call("read_file", Value::object({{"path", Value{file}}}));
      call("exec", Value::object({{"command", Value{"printf X >> '" + marker +
                                                    "'; cat '" + file + "'"}}}));
      call("read_file",
           Value::object({{"path", Value{file}},
                          {"range", Value::object({{"mode", Value{"bytes"}},
                                                   {"start", Value{Number{0}}},
                                                   {"end", Value{Number{24000}}}})}}));
      call("read_file", Value::object({{"path", Value{file}},
                                       {"output_max_bytes", Value{Number{0}}}}));
      call("read_file", Value::object({{"path", Value{file}},
                                       {"output_max_bytes", Value{Number{30000}}}}));
      call("read_file", Value::object({{"path", Value{file}},
                                       {"byte_start", Value{Number{0}}},
                                       {"byte_end", Value{Number{24000}}}}));
      call("read_file",
           Value::object({{"path", Value{file}},
                          {"range", Value::object({{"mode", Value{"lines"}},
                                                   {"start", Value{Number{1}}},
                                                   {"end", Value{Number{1}}}})}}));
      call("exec", Value::object({{"command", Value{"printf Z"}},
                                  {"output_max_bytes", Value{Number{0}}}}));
      call("exec", Value::object({{"command", Value{"cat '" + file + "'"}},
                                  {"output_max_bytes", Value{Number{20000}}}}));
      call("lua",
           Value::object({{"code", Value{"return blackbird.call('read_file', {path=" +
                                         unwrap(dump_json(Value{file})) + "})"}}}));
    } else {
      std::size_t results = 0;
      for (const auto &item : field(request, "input").array()) {
        const auto *type = item.find("type");
        if (!type || *type != Value{"function_call_output"})
          continue;
        const auto &result = field(item, "output");
        const auto &call_id = string_field(item, "call_id");
        if (call_id == "0") {
          check(string_field(result, "content") == std::string(16384, 'A'));
          check(field(result, "file_bytes") == Value{Number{40000}});
          check(field(result, "omitted_bytes") == Value{Number{23616}});
          snapshot = field(result, "snapshot");
        } else if (call_id == "1") {
          check(string_field(result, "output") == std::string(16384, 'A'));
          check(field(result, "omitted_bytes") == Value{Number{23616}});
          check(field(result, "exit_code") == Value{Number{0}});
        } else if (call_id == "2") {
          check(string_field(result, "content") == std::string(24000, 'A'));
        } else if (call_id == "3") {
          check(string_field(result, "content").empty());
          check(field(result, "omitted_bytes") == Value{Number{40000}});
        } else if (call_id == "4") {
          check(string_field(result, "content").size() == 30000);
          check(string_field(result, "content").substr(25000, 21) ==
                "NEEDED_CONSTRAINT=YES");
          check(field(result, "omitted_bytes") == Value{Number{10000}});
        } else if (call_id == "5") {
          check(string_field(result, "content") == std::string(24000, 'A'));
        } else if (call_id == "6" || call_id == "9") {
          check(string_field(result, "content").size() == 40000);
        } else if (call_id == "7") {
          check(string_field(result, "output").empty());
          check(field(result, "omitted_bytes") == Value{Number{1}});
        } else if (call_id == "8") {
          check(string_field(result, "output") == std::string(20000, 'A'));
          check(field(result, "omitted_bytes") == Value{Number{20000}});
        } else {
          check(false);
        }
        ++results;
      }
      check(results == 10);
    }
    return Value::object({{"output", Value{std::move(output)}}});
  }
};
void exercise(const std::string &path) {
  const JournalHeader header{id<EnvironmentId>(1),
                             id<AuditStreamId>(2),
                             7,
                             {1024 * 1024, 4 * 1024 * 1024},
                             std::nullopt};
  const JournalCapacity capacity{64 * 1024 * 1024, 20000};
  const auto file = path + "/source", marker = path + "/effect";
  std::string original(40000, 'A');
  original.replace(25000, 21, "NEEDED_CONSTRAINT=YES");
  write_file(file, original);
  Provider provider;
  provider.file = file;
  provider.marker = marker;
  {
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                         unwrap(NativeJournalDirectory::open(path))),
                                     "audit", header, capacity));
    AuditLog log{*root};
    ContextStore context{log};
    CodingEngine engine{log, context, provider, "test"};
    engine.turn({"inspect useful facts", read_file("programs/turn.lua")});
    check(provider.calls == 2 && read_file(marker) == "X");
    write_file(file, "replacement must not be returned");
    const auto repaired = engine.operator_call(
        "read_file",
        Value::object({{"snapshot", provider.snapshot},
                       {"range", Value::object({{"mode", Value{"bytes"}},
                                                {"start", Value{Number{25000}}},
                                                {"end", Value{Number{25021}}}})}}));
    check(string_field(repaired, "content") == "NEEDED_CONSTRAINT=YES");
    check(field(repaired, "snapshot") == provider.snapshot);
    check(std::filesystem::remove(file));
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
    CodingEngine engine{log, context, provider, "test"};
    const auto repaired = engine.operator_call(
        "read_file", Value::object({{"snapshot", provider.snapshot}}));
    check(string_field(repaired, "content") == original);
    check(field(repaired, "snapshot") == provider.snapshot);
    check(provider.calls == 2 && read_file(marker) == "X");
    const auto conflict = engine.operator_call(
        "read_file",
        Value::object({{"snapshot", provider.snapshot}, {"path", Value{marker}}}));
    check(conflict.find("error"));
    const auto bad_range = engine.operator_call(
        "read_file",
        Value::object({{"snapshot", provider.snapshot},
                       {"range", Value::object({{"mode", Value{"bytes"}},
                                                {"start", Value{Number{20}}},
                                                {"end", Value{Number{10}}}})}}));
    check(string_field(bad_range, "error") == "invalid_range");
    check(field(field(bad_range, "example"), "snapshot") == provider.snapshot);
    check(engine.operator_call("read_file", Value::object({})).find("error"));
    for (const auto *bad : {"0", "-1", "1.5"}) {
      auto malformed = Value::object({{"journal", field(provider.snapshot, "journal")},
                                      {"sequence", Value{Number{bad}}}});
      check(engine.operator_call("read_file", Value::object({{"snapshot", malformed}}))
                .find("error"));
    }
    for (const auto &malformed :
         Value::Array{Value::object({{"journal", Value{"not-an-identity"}},
                                     {"sequence", Value{Number{1}}}}),
                      Value::object({{"journal", Value{Number{3}}},
                                     {"sequence", Value{Number{1}}}}),
                      Value::object({{"journal", field(provider.snapshot, "journal")},
                                     {"sequence", Value{"1"}}}),
                      Value::object({{"journal", field(provider.snapshot, "journal")},
                                     {"sequence", Value{Number{1}}},
                                     {"extra", Value{true}}}),
                      Value::object({}), Value{}}) {
      check(engine.operator_call("read_file", Value::object({{"snapshot", malformed}}))
                .find("error"));
    }
    auto unknown = provider.snapshot;
    for (auto &[key, value] : unknown.object())
      if (key == "sequence")
        value = Value{Number{UINT64_MAX}};
    check(engine.operator_call("read_file", Value::object({{"snapshot", unknown}}))
              .find("error"));
    check(engine.operator_call("read_file", Value::object({{"snapshot", Value{false}}}))
              .find("error"));
  }
}
} // namespace
int main() {
  char name[] = "/tmp/blackbird-economy-XXXXXX";
  const auto *path = mkdtemp(name);
  if (!path)
    return 2;
  try {
    exercise(path);
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << '\n';
    std::filesystem::remove_all(path);
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
  std::filesystem::remove_all(path);
}
