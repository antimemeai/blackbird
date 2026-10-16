#include "blackbird/coding.hpp"
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace blackbird;
namespace {
template <class T> T id(unsigned char n) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{n};
  return unwrap(T::from_bytes(bytes));
}
class UnusedProvider final : public CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw Error{ErrorCode::unsupported};
  }
};
void require(bool good) {
  if (!good)
    throw Error{ErrorCode::corrupt};
}
} // namespace
int main() {
  char pattern[] = "/tmp/blackbird-empty-task-XXXXXX";
  const auto *created = ::mkdtemp(pattern);
  if (!created)
    return 1;
  const std::filesystem::path path{created};
  try {
    const JournalHeader header{id<EnvironmentId>(1),
                               id<AuditStreamId>(2),
                               1,
                               {1024 * 1024, 4 * 1024 * 1024},
                               std::nullopt};
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                         NativeJournalDirectory::open(path.string()))),
                                     "audit", header, {64 * 1024 * 1024, 10000}));
    AuditLog log{*root};
    ContextStore context{log};
    UnusedProvider provider;
    CodingEngine engine{log, context, provider, "test"};
    auto arguments =
        Value::object({{"task_id", Value{""}},
                       {"command", Value{"printf 'empty optional task executes'"}},
                       {"argv", Value{Value::Array{}}},
                       {"timeout_seconds", Value{Number{120}}},
                       {"yield_ms", Value{Number{1000}}},
                       {"pty", Value{false}},
                       {"output_max_bytes", Value{Number{180}}}});
    const auto result = engine.operator_call("exec", arguments);
    require(string_field(result, "output") == "empty optional task executes");
    require(field(result, "exit_code").number().text() == "0");
    const auto marker = (path / "must-not-execute").string();
    for (auto &[key, value] : arguments.object()) {
      if (key == "task_id")
        value = Value{"missing-real-task"};
      if (key == "command")
        value = Value{"touch '" + marker + "'"};
    }
    bool refused = false;
    try {
      (void)engine.operator_call("exec", arguments);
    } catch (const Error &error) {
      refused = error.code == ErrorCode::invalid_range;
    }
    require(refused && !std::filesystem::exists(marker));
    std::cout
        << "Empty optional exec task runs; nonempty unknown task never dispatches\n";
  } catch (const Error &error) {
    std::cerr << "empty exec task: " << error_name(error.code) << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
  std::filesystem::remove_all(path);
  return 0;
}
