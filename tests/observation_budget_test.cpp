#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include <fstream>
#include <iostream>
#include <source_location>
#include <unistd.h>
using namespace blackbird;
// arconaut-o17: a program that names no observation budget (ultracode, custom Lua)
// still gets observations shaped at creation; an explicit request for more, and the
// operator's own inspection, stay exact.
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
std::size_t count(const Value &result, std::string_view key) {
  return std::stoull(field(result, key).number().text());
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
local function sh(script, extra)
  local args = {argv = blackbird.array({'/bin/sh', '-c', script})}
  for key, value in pairs(extra or {}) do args[key] = value end
  return blackbird.call('exec', args)
end
local A = "head -c 100000 /dev/zero | tr '\\000' A"
local e = sh(A)
local more = sh(A, {output_max_bytes = 200000})
local background = sh("sleep 0.2; head -c 100000 /dev/zero | tr '\\000' B", {yield_ms = 0})
return {
  exec = e,
  more = more,
  bare = blackbird.call('read_process_output', {output_ref = e.output_ref}),
  ranged = blackbird.call('read_process_output', {output_ref = e.output_ref,
    range = {mode = 'bytes', start = 0, ['end'] = 50000}}),
  file = blackbird.call('read_file', {path = FILE}),
  foreground = blackbird.call('process', {op = 'foreground', job_id = background.job_id}),
}
)lua";
    engine.turn({"", "FILE=" + unwrap(format_value(Value{file})) + "\n" + program});
    const auto &result = engine.workflow_result();
    const auto &exec = field(result, "exec");
    check(count(exec, "output_bytes") == 100000);
    check(count(exec, "returned_bytes") <= 8192);
    check(exec.find("next_read") != nullptr);
    check(count(field(result, "more"), "returned_bytes") == 100000);
    check(count(field(result, "bare"), "returned_bytes") <= 8192);
    check(count(field(result, "ranged"), "returned_bytes") == 50000);
    const auto &read = field(result, "file");
    check(string_field(read, "content").size() <= 16384);
    check(count(read, "file_bytes") == 40000);
    const auto &foreground = field(result, "foreground");
    check(count(foreground, "output_bytes") == 100000);
    check(count(foreground, "returned_bytes") <= 8192);
    check(foreground.find("next_read") != nullptr);
    // The operator's own inspection is not a model observation.
    const auto exact = engine.operator_call(
        "read_process_output",
        Value::object({{"output_ref", field(exec, "output_ref")}}));
    check(string_field(exact, "output") == std::string(100000, 'A'));
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
