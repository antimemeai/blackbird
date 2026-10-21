#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include <charconv>
#include <iostream>
#include <source_location>
#include <unistd.h>
using namespace blackbird;
namespace {
void check(bool condition,
           std::source_location where = std::source_location::current()) {
  if (!condition) {
    std::cerr << "economical observations line " << where.line() << '\n';
    throw std::runtime_error("economical observations line " +
                             std::to_string(where.line()));
  }
}
template <class T> T id(unsigned char byte) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(byte);
  return unwrap(T::from_bytes(bytes));
}
class Provider final : public CodingProvider {
public:
  std::string file, marker, build;
  std::size_t calls = 0;
  Value snapshot, output_ref, result_ref;
  std::size_t result_bytes = 0;
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
      call("exec",
           Value::object({{"command", Value{"printf X >> '" + marker + "'; cat '" +
                                            build + "'; exit 7"}}}));
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
    } else if (calls == 2) {
      std::size_t results = 0;
      Value next_file_range;
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
          if (const auto *next = result.find("next_range"))
            next_file_range = *next;
        } else if (call_id == "1") {
          const auto &shown = string_field(result, "output");
          check(shown.starts_with("build start\n"));
          check(shown.ends_with("error: trailing failure\n"));
          check(shown.find("NEEDED_MIDDLE=exact") == std::string::npos);
          check(field(result, "output_bytes") == Value{Number{1200058}});
          check(field(result, "returned_bytes") == Value{Number{8192}});
          check(field(result, "next_offset") == Value{Number{2048}});
          check(field(result, "omitted_prefix_bytes") == Value{Number{0}});
          check(field(field(result, "omitted_range"), "start") == Value{Number{2048}});
          check(field(field(result, "omitted_range"), "end") == Value{Number{1193914}});
          check(field(result, "exit_code") == Value{Number{7}});
          output_ref = field(result, "output_ref");
          const auto &next = field(result, "next_read");
          check(string_field(next, "tool") == "read_process_output");
          check(field(field(next, "arguments"), "output_ref") == output_ref);
          check(field(field(next, "arguments"), "byte_start") == Value{Number{2048}});
          check(field(field(next, "arguments"), "byte_end") == Value{Number{10240}});
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
        } else if (call_id == "6") {
          check(string_field(result, "content").size() == 40000);
        } else if (call_id == "9") {
          // Custom Lua naming no budget still gets o17's 16384-byte read; the lua
          // tool's own result, whose JSON text is over 16384 bytes, is then shaped
          // generically (T1): its first 4096 and last 12288 bytes around a marker
          // that names the omitted range and the result_ref of the whole original.
          check(std::holds_alternative<std::string>(result.value()));
          const auto &shown = result.string();
          const std::string opening = R"({"byte_end":16384,"byte_start":0,"content":")";
          const auto head = opening + std::string(4096 - opening.size(), 'A');
          const std::string lead = "\n[... bytes [4096,";
          check(shown.starts_with(head + lead));
          std::size_t tail_start = 0, total = 0;
          const auto *cursor = shown.data() + head.size() + lead.size();
          const auto *last = shown.data() + shown.size();
          cursor = std::from_chars(cursor, last, tail_start).ptr;
          const std::string of = ") of ";
          check(std::string_view{cursor, of.size()} == of);
          cursor = std::from_chars(cursor + of.size(), last, total).ptr;
          const std::string named = " omitted; read_process_output with result_ref ";
          check(std::string_view{cursor, named.size()} == named);
          const std::string ref{cursor + named.size(), 32};
          const auto rest = " and range {mode: bytes, start: 4096, end: " +
                            std::to_string(tail_start) +
                            "} pages them, 8192 bytes each with next_read ...]\n";
          check(std::string_view{cursor + named.size() + 32, rest.size()} == rest);
          const std::string_view tail{cursor + named.size() + 32 + rest.size()};
          check(total - tail_start == 12288 && tail.size() == 12288);
          check(tail.starts_with(std::string(64, 'A')) && tail.ends_with("}}"));
          result_ref = Value{ref};
          result_bytes = total;
        } else if (call_id == "7") {
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
      check(string_field(next_file_range, "mode") == "bytes");
      check(field(next_file_range, "start") == Value{Number{16384}});
      check(field(next_file_range, "end") == Value{Number{32768}});
      const auto recovery_args =
          Value::object({{"output_ref", output_ref},
                         {"range", Value::object({{"mode", Value{"lines"}},
                                                  {"start", Value{Number{3}}},
                                                  {"end", Value{Number{3}}}})}});
      output.push_back(
          Value::object({{"type", Value{"function_call"}},
                         {"call_id", Value{"recover-middle"}},
                         {"name", Value{"read_process_output"}},
                         {"arguments", Value{unwrap(dump_json(recovery_args))}}}));
      // The whole original behind the shaped lua result, as one page: fetch-back is
      // 8192 bytes by default, so the whole needs an explicit output_max_bytes.
      const auto whole_args = Value::object(
          {{"result_ref", result_ref},
           {"output_max_bytes", Value{Number{result_bytes}}},
           {"range", Value::object({{"mode", Value{"bytes"}},
                                    {"start", Value{Number{0}}},
                                    {"end", Value{Number{result_bytes}}}})}});
      output.push_back(
          Value::object({{"type", Value{"function_call"}},
                         {"call_id", Value{"recover-result"}},
                         {"name", Value{"read_process_output"}},
                         {"arguments", Value{unwrap(dump_json(whole_args))}}}));
    } else {
      check(calls == 3);
      bool recovered = false, whole = false;
      for (const auto &item : field(request, "input").array()) {
        const auto *type = item.find("type");
        if (type && *type == Value{"function_call_output"} &&
            string_field(item, "call_id") == "recover-result") {
          const auto &result = field(item, "output");
          check(field(result, "output_bytes") == Value{Number{result_bytes}});
          check(field(result, "returned_bytes") == Value{Number{result_bytes}});
          check(field(result, "result_ref") == result_ref && !result.find("next_read"));
          const auto original = unwrap(parse_json(string_field(result, "output")));
          check(string_field(original, "content") == std::string(16384, 'A'));
          check(field(original, "file_bytes") == Value{Number{40000}});
          check(field(original, "omitted_bytes") == Value{Number{23616}});
          whole = true;
          continue;
        }
        if (!type || *type != Value{"function_call_output"} ||
            string_field(item, "call_id") != "recover-middle")
          continue;
        const auto &result = field(item, "output");
        check(string_field(result, "output") == "NEEDED_MIDDLE=exact\n");
        check(field(result, "byte_start") == Value{Number{600013}});
        check(field(result, "byte_end") == Value{Number{600033}});
        recovered = true;
      }
      check(recovered && whole);
    }
    return Value::object({{"output", Value{std::move(output)}}});
  }
};
void exercise(const std::string &path) {
  const JournalHeader header{id<EnvironmentId>(1),
                             id<AuditStreamId>(2),
                             7,
                             {4 * 1024 * 1024, 16 * 1024 * 1024},
                             std::nullopt};
  const JournalCapacity capacity{64 * 1024 * 1024, 20000};
  const auto file = path + "/source", marker = path + "/effect";
  std::string original(40000, 'A');
  original.replace(25000, 21, "NEEDED_CONSTRAINT=YES");
  write_file(file, original);
  Provider provider;
  provider.file = file;
  provider.marker = marker;
  provider.build = path + "/build-output";
  const auto build_output = "build start\n" + std::string(600000, 'A') +
                            "\nNEEDED_MIDDLE=exact\n" + std::string(600000, 'B') +
                            "\nerror: trailing failure\n";
  write_file(provider.build, build_output);
  {
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                         unwrap(NativeJournalDirectory::open(path))),
                                     "audit", header, capacity));
    AuditLog log{*root};
    ContextStore context{log};
    CodingEngine engine{log, context, provider, "test"};
    engine.turn({"inspect useful facts", read_file("programs/turn.lua")});
    check(provider.calls == 3 && read_file(marker) == "X");
    check(std::filesystem::remove(provider.build));
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
    check(provider.calls == 3 && read_file(marker) == "X");
    const auto output_repaired = engine.operator_call(
        "read_process_output", Value::object({{"output_ref", provider.output_ref}}));
    check(string_field(output_repaired, "output") == build_output);
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
