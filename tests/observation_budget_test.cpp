#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include <fstream>
#include <iostream>
#include <source_location>
#include <unistd.h>
using namespace blackbird;
// arconaut-o17: a program that names no observation budget (ultracode, custom Lua)
// still gets observations shaped at creation; an explicit request for more, and the
// operator's own call, stay exact. Expected values derive from the specification:
// 8192 bytes split 2048 head + 6144 tail; read_file 16384 head-first. T1 shapes every
// other tool only where its result reaches the model: a program's own calls of those
// tools return whole results, whatever budget keys they pass.
namespace {
void check(bool condition,
           std::source_location where = std::source_location::current()) {
  if (!condition) {
    std::cerr << "observation budget line " << where.line() << '\n';
    throw std::runtime_error("observation budget line " + std::to_string(where.line()));
  }
}
template <class T> T id(unsigned char byte) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(byte);
  return unwrap(T::from_bytes(bytes));
}
class Unused final : public CodingProvider {
public:
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw Error{ErrorCode::unsupported};
  }
};
std::uint64_t count(const Value &result, std::string_view key) {
  return std::stoull(field(result, key).number().text());
}
// 100000 bytes of one letter, no newline: every cut lands exactly on its budget.
void split(const Value &r, std::uint64_t head, std::uint64_t tail_start) {
  check(count(r, "output_bytes") == 100000);
  check(count(r, "returned_bytes") == head + (100000 - tail_start));
  check(count(r, "byte_start") == 0 && count(r, "byte_end") == head);
  check(count(r, "tail_byte_start") == tail_start &&
        count(r, "tail_byte_end") == 100000);
  const auto &gap = field(r, "omitted_range");
  check(count(gap, "start") == head && count(gap, "end") == tail_start);
  const auto &next = field(field(r, "next_read"), "arguments");
  check(field(next, "output_ref") == field(r, "output_ref"));
  check(count(next, "byte_start") == head && count(next, "byte_end") == head + 8192);
}
} // namespace
int main() {
  char name[] = "/tmp/arco-observation-XXXXXX";
  char scratch[] = "/tmp/arco-observation-file-XXXXXX";
  if (!mkdtemp(name) || !mkdtemp(scratch))
    return 1;
  const std::filesystem::path path{name}, files{scratch};
  try {
    const auto file = (files / "large").string();
    std::ofstream{file, std::ios::binary} << std::string(40000, 'F');
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                         NativeJournalDirectory::open(path.string()))),
                                     "audit",
                                     {id<EnvironmentId>(1),
                                      id<AuditStreamId>(2),
                                      7,
                                      {4 * 1024 * 1024, 16 * 1024 * 1024},
                                      std::nullopt},
                                     {64 * 1024 * 1024, 20000}));
    AuditLog log{*root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "test-model"};
    engine.display = [](std::string_view v) { std::cerr << v; };
    const std::string program = R"lua(
local function sh(letter, extra)
  local script = "head -c 100000 /dev/zero | tr '\\000' " .. letter
  local args = {argv = blackbird.array({'/bin/sh', '-c', script})}
  for key, value in pairs(extra or {}) do args[key] = value end
  return blackbird.call('exec', args)
end
local function job(letter)
  return sh(letter, {yield_ms = 0}).job_id
end
-- Sequential locals: the operation records below are read in this order.
local e = sh('A')
local more = sh('A', {output_max_bytes = 200000})
local head = sh('A', {output_head_bytes = 10000})
local results = {
  exec = e,
  more = more,
  head = head,
  bare = blackbird.call('read_process_output', {output_ref = e.output_ref}),
  ranged = blackbird.call('read_process_output', {output_ref = e.output_ref,
    range = {mode = 'bytes', start = 0, ['end'] = 50000}}),
  file = blackbird.call('read_file', {path = FILE}),
}
for key, extra in pairs({foreground = {}, split = {output_max_bytes = 4096,
    output_head_bytes = 1024}, zero = {output_max_bytes = 0}}) do
  local args = {op = 'foreground', job_id = job('B')}
  for k, v in pairs(extra) do args[k] = v end
  results[key] = blackbird.call('process', args)
end
-- Non-exec tools (T1): a program's own lua call returns its 40000 bytes whole; budget
-- keys are kept out of the code's view and change nothing.
local function text(extra)
  local args = {code = "return string.rep('L', 40000)"}
  for k, v in pairs(extra or {}) do args[k] = v end
  return blackbird.call('lua', args)
end
results.lua = text()
results.lua_more = text({output_max_bytes = 50000})
results.lua_split = text({output_max_bytes = 5000, output_head_bytes = 1000})
results.lua_head = text({output_head_bytes = 10000})
results.lua_first = text({output_max_bytes = 3000})
results.lua_zero = text({output_max_bytes = 0})
results.lua_bad = text({output_max_bytes = 10, output_head_bytes = 11})
return results
)lua";
    engine.turn({"", "FILE=" + unwrap(format_value(Value{file})) + "\n" + program});
    // A copy: each later operator_call replaces the engine's workflow result.
    const auto result = engine.workflow_result();
    check(result.find("exec") != nullptr); // the program ran to completion
    const auto &exec = field(result, "exec");
    split(exec, 2048, 93856);
    split(field(result, "bare"), 2048, 93856);
    // A head alone keeps the default 6144-byte tail after it.
    split(field(result, "head"), 10000, 93856);
    const auto &more = field(result, "more");
    check(count(more, "returned_bytes") == 100000 && !more.find("next_read"));
    const auto &ranged = field(result, "ranged");
    check(count(ranged, "returned_bytes") == 50000 &&
          string_field(ranged, "output") == std::string(50000, 'A'));
    const auto &read = field(result, "file");
    check(string_field(read, "content") == std::string(16384, 'F'));
    check(count(read, "file_bytes") == 40000 && count(read, "omitted_bytes") == 23616);
    const auto &next_range = field(read, "next_range");
    check(count(next_range, "start") == 16384 && count(next_range, "end") == 32768);
    const auto &foreground = field(result, "foreground");
    split(foreground, 2048, 93856);
    check(string_field(foreground, "state") == "completed" &&
          count(foreground, "exit_code") == 0 &&
          field(foreground, "job_id") == field(foreground, "output_ref"));
    split(field(result, "split"), 1024, 96928);
    // Generic shaping of a non-exec tool, exactly by the rule.
    for (const auto *key : {"lua", "lua_more", "lua_split", "lua_head", "lua_first",
                            "lua_zero", "lua_bad"})
      check(field(result, key) == Value{std::string(40000, 'L')});
    const auto &zero = field(result, "zero");
    check(string_field(zero, "output").empty() && count(zero, "returned_bytes") == 0 &&
          count(zero, "omitted_bytes") == 100000);
    const auto &zero_next = field(field(zero, "next_read"), "arguments");
    check(count(zero_next, "byte_start") == 0 && count(zero_next, "byte_end") == 8192);
    // The operation record names the keys Blackbird supplied, and only those.
    std::vector<Value> supplied;
    for (const auto &fact : root->committed_facts())
      if (const auto *d = std::get_if<DecisionEvent>(&fact.event.body)) {
        const auto meta = unwrap(read_packet(d->continuation));
        if (const auto *op = meta.find("operation"); op && *op == Value{"exec"}) {
          const auto *keys = meta.find("native_defaults");
          supplied.push_back(keys ? *keys : Value{});
        }
      }
    const Value both{
        Value::Array{Value{"output_max_bytes"}, Value{"output_head_bytes"}}};
    const Value budget_only{Value::Array{Value{"output_max_bytes"}}};
    check(supplied.size() == 6);
    check(supplied[0] == both && supplied[1] == Value{} && supplied[2] == budget_only);
    for (std::size_t n = 3; n < 6; ++n)
      check(supplied[n] == both); // background launches of the foreground jobs
    // The lua calls: the default names both keys; explicit keys are the program's.
    std::vector<Value> lua_supplied;
    for (const auto &fact : root->committed_facts())
      if (const auto *d = std::get_if<DecisionEvent>(&fact.event.body)) {
        const auto meta = unwrap(read_packet(d->continuation));
        if (const auto *op = meta.find("operation"); op && *op == Value{"lua"}) {
          const auto *keys = meta.find("native_defaults");
          lua_supplied.push_back(keys ? *keys : Value{});
        }
      }
    check(lua_supplied.size() == 7);
    for (const auto &keys : lua_supplied)
      check(keys == Value{}); // Blackbird supplies no budget to a program's own call
    // The operator's own call is not a model observation and stays complete.
    const auto script = Value{"head -c 100000 /dev/zero | tr '\\000' A"};
    const auto argv = Value{Value::Array{Value{"/bin/sh"}, Value{"-c"}, script}};
    const auto exact_exec =
        engine.operator_call("exec", Value::object({{"argv", argv}}));
    check(string_field(exact_exec, "output") == std::string(100000, 'A') &&
          !exact_exec.find("next_read"));
    const auto exact_read = engine.operator_call(
        "read_process_output",
        Value::object({{"output_ref", field(exec, "output_ref")}}));
    check(string_field(exact_read, "output") == std::string(100000, 'A'));
    const auto exact_file =
        engine.operator_call("read_file", Value::object({{"path", Value{file}}}));
    check(string_field(exact_file, "content") == std::string(40000, 'F'));
    const auto started = engine.operator_call(
        "exec", Value::object({{"argv", argv}, {"yield_ms", Value{Number{0}}}}));
    const auto exact_foreground = engine.operator_call(
        "process", Value::object({{"op", Value{"foreground"}},
                                  {"job_id", field(started, "job_id")}}));
    check(string_field(exact_foreground, "output") == std::string(100000, 'A'));
    // A program the operator runs is a program again: its nested calls are shaped.
    const auto nested = engine.operator_call(
        "lua", Value::object(
                   {{"code",
                     Value{"return blackbird.call('exec', {argv = blackbird.array({"
                           "'/bin/sh', '-c', \"head -c 100000 /dev/zero | tr '\\\\000' "
                           "A\"})})"}}}));
    split(nested, 2048, 93856);
    // The operator's own lua call stays exact; a lua call nested in it is the
    // program's own and whole too.
    const auto code = Value{"return string.rep('L', 40000)"};
    check(engine.operator_call("lua", Value::object({{"code", code}})) ==
          Value{std::string(40000, 'L')});
    const auto inner = engine.operator_call(
        "lua", Value::object({{"code", Value{"return blackbird.call('lua', {code = "
                                             "\"return string.rep('L', 40000)\"})"}}}));
    check(inner == Value{std::string(40000, 'L')});
    std::filesystem::remove_all(path);
    std::filesystem::remove_all(files);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  } catch (const Error &e) {
    std::cerr << "error " << static_cast<int>(e.code) << '\n';
  }
  std::filesystem::remove_all(path);
  std::filesystem::remove_all(files);
  return 1;
}
