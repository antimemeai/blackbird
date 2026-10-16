#include "blackbird/coding.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_file.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <new>
#include <unistd.h>

namespace {
std::atomic_bool injection_enabled{false}, injected{false};
std::atomic_uint allocation_faults{0};
unsigned faults_per_rename = 1;
std::atomic_uint replacements{0};
const char *destination = nullptr;
using Rename = int (*)(const char *, const char *);
Rename native_rename = nullptr;
void *allocate_fixture_bytes(std::size_t bytes) {
  auto remaining = allocation_faults.load();
  while (remaining != 0 &&
         !allocation_faults.compare_exchange_weak(remaining, remaining - 1)) {
  }
  if (remaining != 0) {
    injected = true;
    throw std::bad_alloc{};
  }
  if (void *memory = std::malloc(bytes ? bytes : 1))
    return memory;
  throw std::bad_alloc{};
}
} // namespace
void *operator new(std::size_t bytes) { return allocate_fixture_bytes(bytes); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
extern "C" int rename(const char *source, const char *target) {
  const int result = native_rename(source, target);
  if (result == 0 && destination && std::strcmp(target, destination) == 0) {
    ++replacements;
    if (injection_enabled)
      allocation_faults = faults_per_rename;
  }
  return result;
}
using namespace blackbird;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class T> T identity(unsigned char value) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{value};
  return unwrap(T::from_bytes(bytes));
}
struct NoProvider final : CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw std::runtime_error("fixture must not dispatch provider");
  }
};
struct DirectoryCleanup {
  std::filesystem::path path;
  ~DirectoryCleanup() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};
void scenario(bool edit, bool raw, bool persistent = false) {
  char temporary[] = "/tmp/blackbird-file-effect-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "private fixture directory");
  const std::filesystem::path directory{temporary};
  const DirectoryCleanup cleanup{directory};
  const auto requested = (directory / "target").string();
  write_file(requested, "before-token");
  const auto target = std::filesystem::canonical(requested).string();
  std::unique_ptr<RawRuntime> runtime;
  std::optional<RawThreadBinding> binding;
  if (raw) {
    runtime = unwrap(RawRuntime::open(directory.string(), identity<EnvironmentId>(31)));
    binding.emplace(unwrap(runtime->prepare_thread()));
  }
  auto root = unwrap(
      RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(directory.string()))),
                            "audit",
                            {identity<EnvironmentId>(31),
                             identity<AuditStreamId>(32),
                             3,
                             {1024 * 1024, 4 * 1024 * 1024},
                             std::nullopt},
                            {32 * 1024 * 1024, 20000}));
  AuditLog log{*root};
  ContextStore context{log};
  NoProvider provider;
  CodingEngine engine{log, context, provider, "fixture"};
  const auto operation = edit ? "edit_file" : "write_file";
  std::string activity;
  AttemptDisposition disposition = AttemptDisposition::unknown;
  engine.operation_outcome = [&](std::string_view text, AttemptDisposition actual) {
    if (text.starts_with(operation)) {
      activity = text;
      disposition = actual;
    }
  };
  const auto arguments = edit ? Value::object({{"path", Value{target}},
                                               {"old", Value{"token"}},
                                               {"new", Value{"replacement"}}})
                              : Value::object({{"path", Value{target}},
                                               {"content", Value{"actual effect"}}});
  destination = target.c_str();
  faults_per_rename = persistent ? 2 : 1;
  injection_enabled = true;
  Value result;
  bool stopped = false;
  try {
    result = engine.operator_call(operation, arguments);
  } catch (const Error &) {
    stopped = true;
  } catch (const std::bad_alloc &) {
    stopped = true;
  }
  injection_enabled = false;
  allocation_faults = 0;
  require(injected && replacements == 1,
          "exact one actual rename and allocation fault");
  require(read_file(target) == (edit ? "before-replacement" : "actual effect"),
          "actual destination bytes survive result failure");
  if (persistent) {
    try {
      (void)engine.operator_call(
          "write_file",
          Value::object({{"path", Value{target}}, {"content", Value{"FORBIDDEN"}}}));
    } catch (const Error &) {
    }
    require(stopped && replacements == 1 &&
                read_file(target) == (edit ? "before-replacement" : "actual effect"),
            "second result allocation failure fences later effects without replay");
    destination = nullptr;
    return;
  }
  require(!stopped, "one-shot result allocation failure recovers observation");
  require(disposition == AttemptDisposition::failure &&
              activity.find("replacement applied") != std::string::npos,
          "operator activity preserves applied replacement and result failure");
  require(result.find("error") && string_field(result, "error") == "allocation" &&
              result.find("effect_outcome") &&
              string_field(result, "effect_outcome") == "applied",
          "returned allocation error preserves known applied replacement");
  unsigned retained = 0;
  for (const auto &fact : root->committed_facts()) {
    const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
    if (!record || record->channel != ApplicationChannel::log)
      continue;
    const auto packet = unwrap(read_packet(record->payload));
    if (string_field(packet, "label") != "operation.result" ||
        string_field(field(packet, "metadata"), "operation") != operation)
      continue;
    for (const auto &dependency : fact.event.dependencies) {
      const auto encoded = unwrap(root->source(dependency));
      const auto value = decode_packet(encoded);
      if (value.has_value() && value.value().find("effect_outcome")) {
        require(string_field(value.value(), "effect_outcome") == "applied" &&
                    string_field(value.value(), "error") == "allocation",
                "retained result preserves allocation and applied independently");
        ++retained;
      }
    }
  }
  require(retained == 1, "applied result retained exactly once");
  const auto inspected =
      engine.operator_call("read_file", Value::object({{"path", Value{target}}}));
  require(inspected.find("content") &&
              string_field(inspected, "content") == read_file(target) &&
              replacements == 1,
          "later inspection succeeds without replaying replacement");
  destination = nullptr;
}
void native_observation(bool raw) {
  char temporary[] = "/tmp/blackbird-file-observation-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "private native fixture");
  const std::filesystem::path directory{temporary};
  const DirectoryCleanup cleanup{directory};
  const auto requested = directory / "target";
  write_file(requested, "before");
  const auto target = std::filesystem::canonical(requested).string();
  std::unique_ptr<RawRuntime> runtime;
  std::optional<RawThreadBinding> binding;
  RawAdapterContext adapter;
  if (raw) {
    runtime = unwrap(RawRuntime::open(directory.string(), identity<EnvironmentId>(71)));
    binding.emplace(unwrap(runtime->prepare_thread()));
    const auto parent = unwrap(raw_emit(7999, std::span<const ByteView>{}));
    adapter.parent = RawEventRef{raw_lane_epoch(), parent};
  }
  FileEffectObservation effect;
  destination = target.c_str();
  injection_enabled = true;
  bool allocation_failed = false;
  try {
    write_file(target, "applied native replacement", raw ? &adapter : nullptr, nullptr,
               &effect);
    // If the writer performs no later allocation, consume the armed failure here.
    void *memory = allocate_fixture_bytes(4096);
    std::free(memory);
  } catch (const std::bad_alloc &) {
    allocation_failed = true;
  }
  injection_enabled = false;
  allocation_faults = 0;
  require(allocation_failed && injected && effect.applied() && replacements == 1 &&
              read_file(target) == "applied native replacement",
          "native rename observation survives exact subsequent allocation failure");
  destination = nullptr;
  const auto blocked = directory / "directory-destination";
  std::filesystem::create_directory(blocked);
  write_file(blocked / "sentinel", "unchanged");
  FileEffectObservation rejected;
  bool refused = false;
  try {
    write_file(blocked, "cannot replace directory with regular file",
               raw ? &adapter : nullptr, nullptr, &rejected);
  } catch (const Error &error) {
    refused = error.code == ErrorCode::io;
  }
  require(refused && !rejected.applied() &&
              read_file(blocked / "sentinel") == "unchanged",
          "actual unsuccessful rename cannot issue applied observation");
}
} // namespace
int main(int argc, char **argv) {
  try {
    native_rename = reinterpret_cast<Rename>(::dlsym(RTLD_NEXT, "rename"));
    require(native_rename != nullptr && argc == 2,
            "resolve actual rename/private mode");
    const std::string_view mode{argv[1]};
    if (mode.starts_with("native"))
      native_observation(mode.ends_with("raw"));
    else
      scenario(mode.starts_with("edit"), mode.ends_with("raw"),
               mode.starts_with("persistent"));
    std::cout << "PASS " << mode << '\n';
  } catch (const std::exception &error) {
    injection_enabled = false;
    allocation_faults = 0;
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  } catch (const Error &error) {
    injection_enabled = false;
    allocation_faults = 0;
    std::cerr << "FAIL native " << error_name(error.code) << ':' << error.detail
              << '\n';
    return 1;
  }
}
