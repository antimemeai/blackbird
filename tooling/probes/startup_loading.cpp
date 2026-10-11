#include "blackbird/json.hpp"
// Private-fixture restoration and actual CodingEngine admission; deterministic
// local provider only, with known success and no external service or side effect.
#include "blackbird/coding.hpp"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
using namespace blackbird;
template <class T> T fixed(unsigned char n) {
  IdentityBytes b{};
  b[0] = std::byte{n};
  return unwrap(T::from_bytes(b));
}
struct ProbeIO {
  std::uint64_t read_bytes = 0, write_bytes = 0, sync_calls = 0;
  std::vector<std::pair<std::uint64_t, std::uint64_t>> forbidden;
  std::size_t forbidden_attempts = 0;
};
class ProbeFile final : public JournalFile {
public:
  ProbeFile(std::unique_ptr<JournalFile> file, ProbeIO &io)
      : file_(std::move(file)), io_(io) {}
  void release_writer() noexcept override { file_->release_writer(); }
  Result<std::size_t> read_at(std::uint64_t offset, MutableByteView bytes) override {
    // Development oracle: a fast reopen must succeed even when archived rejected
    // bodies are unreadable. These ranges refer only to the private audit file.
    if (audit_ && !bytes.empty())
      for (const auto &[begin, end] : io_.forbidden)
        if (offset < end && (offset >= begin || bytes.size() > begin - offset)) {
          ++io_.forbidden_attempts;
          return Result<std::size_t>::failure({ErrorCode::io});
        }
    auto result = file_->read_at(offset, bytes);
    if (result.has_value())
      io_.read_bytes += result.value();
    return result;
  }
  Result<std::size_t> write_at(std::uint64_t offset, ByteView bytes) override {
    auto result = file_->write_at(offset, bytes);
    if (result.has_value())
      io_.write_bytes += result.value();
    return result;
  }
  Result<std::uint64_t> extent() override { return file_->extent(); }
  Result<void> synchronize(SyncStrength strength) override {
    ++io_.sync_calls;
    return file_->synchronize(strength);
  }
  Result<void> lock_writer() override { return file_->lock_writer(); }
  void audit(bool value) noexcept { audit_ = value; }

private:
  std::unique_ptr<JournalFile> file_;
  ProbeIO &io_;
  bool audit_ = false;
};
class ProbeDirectory final : public JournalDirectory {
public:
  ProbeDirectory(std::unique_ptr<JournalDirectory> directory, ProbeIO &io)
      : directory_(std::move(directory)), io_(io) {}
  Result<std::unique_ptr<JournalFile>>
  create_exclusive(std::string_view name) override {
    return wrap(directory_->create_exclusive(name));
  }
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view name,
                                                     FileAccess access) override {
    return wrap(directory_->open_existing(name, access), name == "audit");
  }
  Result<void> synchronize_directory(SyncStrength strength) override {
    ++io_.sync_calls;
    return directory_->synchronize_directory(strength);
  }
  Result<void> replace_file(std::string_view from, std::string_view to) override {
    return directory_->replace_file(from, to);
  }

private:
  Result<std::unique_ptr<JournalFile>> wrap(Result<std::unique_ptr<JournalFile>> result,
                                            bool audit = false) {
    if (!result.has_value())
      return result;
    auto wrapped = std::make_unique<ProbeFile>(std::move(result).value(), io_);
    wrapped->audit(audit);
    return Result<std::unique_ptr<JournalFile>>::success(std::move(wrapped));
  }
  std::unique_ptr<JournalDirectory> directory_;
  ProbeIO &io_;
};
struct MemorySample {
  std::uint64_t resident = 0, footprint = 0, virtual_bytes = 0;
};
MemorySample memory_sample() {
#if defined(__APPLE__)
  task_vm_info_data_t info{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info),
                &count) == KERN_SUCCESS)
    return {info.resident_size, info.phys_footprint, info.virtual_size};
#endif
  return {};
}
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  ProbeIO io;
  try {
    const auto start = std::chrono::steady_clock::now();
    const std::filesystem::path path{argv[2]};
    std::filesystem::create_directories(path);
    JournalHeader h{fixed<EnvironmentId>(1),
                    fixed<AuditStreamId>(2),
                    3,
                    {2 * 1024 * 1024, 4 * 1024 * 1024},
                    std::nullopt};
    JournalCapacity cap{1024ULL * 1024 * 1024, 200000};
    auto directory = std::make_unique<ProbeDirectory>(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(path.string()))),
        io);
    const std::string_view mode{argv[1]};
    const bool create = mode == "create";
    const bool indexed = mode == "open-index";
    if (mode == "open-forbid") {
      if (argc != 4)
        return 2;
      std::ifstream ranges_file{argv[3]};
      const std::string text{std::istreambuf_iterator<char>{ranges_file}, {}};
      const auto ranges = unwrap(parse_json(text));
      for (const auto &range : ranges.array()) {
        if (range.array().size() != 2)
          throw Error{ErrorCode::invalid_range};
        const auto begin = std::stoull(range.array()[0].number().text());
        const auto end = std::stoull(range.array()[1].number().text());
        if (begin >= end)
          throw Error{ErrorCode::invalid_range};
        io.forbidden.emplace_back(begin, end);
      }
      if (io.forbidden.empty())
        throw Error{ErrorCode::invalid_range};
    }
    if (!create) {
      std::array<std::byte, journal_header_size> bytes{};
      std::ifstream input{path / "audit", std::ios::binary};
      input.read(reinterpret_cast<char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
      if (!input)
        throw Error{ErrorCode::incomplete};
      h = unwrap(decode_journal_header(bytes));
    }
    auto root =
        create ? unwrap(RetainedState::create(std::move(directory), "audit", h, cap))
               : unwrap(RetainedState::open(std::move(directory), "audit", h, cap,
                                            indexed));
    if (!create) {
      unwrap(root->confirm_recovery());
      struct EmptyCustody final : CustodyVerifier {
        Result<void> verify(std::span<const AttemptState> attempts) override {
          return attempts.empty() ? Result<void>::success()
                                  : Result<void>::failure({ErrorCode::conflict});
        }
      } custody;
      unwrap(root->reconcile(custody));
    }
    const auto initial_bytes = std::filesystem::file_size(path / "audit");
    const auto initial_facts = root->fact_count();
    std::size_t resident_payload = 0, cold_payload = 0;
    for (const auto &fact : root->tail_facts())
      if (const auto *app = std::get_if<ApplicationRecordEvent>(&fact.event.body)) {
        resident_payload += app->payload.resident_bytes();
        if (app->payload.is_cold())
          cold_payload += app->payload.size();
      }
    std::cerr << "resident_application_bytes=" << resident_payload
              << " cold_application_bytes=" << cold_payload << '\n';
    const auto replay = std::chrono::steady_clock::now();
    const auto replay_io = io;
    const double replay_cpu_ms =
        1000.0 * static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
    AuditLog log{*root};
    ContextStore context{log};
    if (create) {
      context.append(
          {Value::object(
              {{"role", Value{"user"}},
               {"content",
                Value{argc > 5 ? std::string(
                                     static_cast<std::size_t>(std::stoul(argv[5])), 'l')
                               : std::string{"fixture"}}}})},
          "fixture");
      const std::string payload(
          argc > 4 ? static_cast<std::size_t>(std::stoul(argv[4])) : 512 * 1024, 'x');
      const unsigned count =
          argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 640;
      for (unsigned i = 0; i < count; ++i) {
        const auto identity = log.issue();
        log.record(identity, ApplicationChannel::context,
                   Value::object({{"op", Value{"edit"}},
                                  {"revision", Value{hex_identity(identity.bytes())}},
                                  {"observed", Value{context.head()}},
                                  {"accepted", Value{false}},
                                  {"candidate", Value{payload}},
                                  {"entries", Value{Value::Array{}}}}));
      }
      std::cout << "created_bytes=" << std::filesystem::file_size(path / "audit")
                << " facts=" << root->fact_count() << '\n';
      return 0;
    }
    if (mode == "save-current") {
      unwrap(context.checkpoint());
      std::cout << "saved_current_bytes=" << io.write_bytes
                << " read_bytes=" << io.read_bytes << " syncs=" << io.sync_calls
                << '\n';
      return 0;
    }
    if (mode == "checkpoint") {
      unwrap(root->publish_scan_checkpoint());
      std::cout << "checkpoint_bytes=" << io.write_bytes
                << " read_bytes=" << io.read_bytes << " syncs=" << io.sync_calls
                << '\n';
      return 0;
    }
    const auto restored = std::chrono::steady_clock::now();
    const auto items = context.items(); // actual usable live prompt, not a banner
    const auto ready = std::chrono::steady_clock::now();
    const auto prompt_io = io;
    const auto prompt_memory = memory_sample();
    const auto serialized = unwrap(dump_json(Value{items}));
    struct LocalProvider final : CodingProvider {
      RetainedState &root;
      ProbeIO &io;
      ProbeIO readiness_io;
      MemorySample readiness_memory;
      double readiness_cpu_ms = 0;
      std::chrono::steady_clock::time_point admitted;
      std::size_t input_bytes = 0;
      std::size_t first_fact;
      std::optional<AttemptAdmissionEvent> admission;
      explicit LocalProvider(RetainedState &r, ProbeIO &p)
          : root(r), io(p), first_fact(r.tail_facts().size()) {}
      std::string_view provider_identity() const noexcept override {
        return "deterministic-local-fixture";
      }
      Value respond(const Value &request,
                    const std::function<void(std::string_view)> &capture) override {
        admitted = std::chrono::steady_clock::now(); // before any probe validation
        readiness_io = io;
        readiness_memory = memory_sample();
        readiness_cpu_ms = 1000.0 * static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
        // CodingEngine has prepared and durably admitted/opened the actual
        // request. This provider performs no external effect and returns known
        // success, which the engine settles normally (never a fabricated unknown).
        const auto encoded = unwrap(dump_json(request));
        ImmutableBytes expected{
            reinterpret_cast<const std::byte *>(encoded.data()),
            reinterpret_cast<const std::byte *>(encoded.data() + encoded.size())};
        const DecisionEvent *decision = nullptr;
        const InvocationEvent *invocation = nullptr;
        bool opened = false;
        const auto facts = root.tail_facts();
        for (std::size_t i = first_fact; i < facts.size(); ++i) {
          const auto &body = facts[i].event.body;
          if (const auto *a = std::get_if<AttemptAdmissionEvent>(&body);
              a && a->input == expected) {
            if (admission)
              throw Error{ErrorCode::conflict};
            admission = *a;
          }
        }
        if (!admission)
          throw Error{ErrorCode::corrupt};
        for (std::size_t i = first_fact; i < facts.size(); ++i) {
          const auto &body = facts[i].event.body;
          if (const auto *d = std::get_if<DecisionEvent>(&body);
              d && d->decision == admission->decision)
            decision = d;
          if (const auto *v = std::get_if<InvocationEvent>(&body);
              v && v->invocation == admission->invocation)
            invocation = v;
          if (const auto *o = std::get_if<AttemptOpenEvent>(&body);
              o && o->attempt == admission->attempt)
            opened = true;
        }
        if (!decision || !invocation || !opened || invocation->input != expected ||
            invocation->decision != decision->decision ||
            invocation->definition != decision->definition ||
            decision->planned_invocations !=
                std::vector<InvocationId>{invocation->invocation})
          throw Error{ErrorCode::corrupt};
        const auto meta = unwrap(
            parse_json({reinterpret_cast<const char *>(decision->continuation.data()),
                        decision->continuation.size()}));
        if (string_field(meta, "operation") != "provider" || meta.find("input") ||
            string_field(meta, "input_binding") != "invocation-v1" ||
            string_field(meta, "invocation") !=
                hex_identity(invocation->invocation.bytes()) ||
            string_field(meta, "revision") != hex_identity(decision->context.bytes()) ||
            string_field(meta, "generation") !=
                hex_identity(decision->definition.bytes()))
          throw Error{ErrorCode::corrupt};
        input_bytes = unwrap(dump_json(field(request, "input"))).size();
        auto response = Value::object({{"output", Value{Value::Array{}}}});
        capture(unwrap(dump_json(response)));
        return response;
      }
    } provider{*root, io};
    CodingEngine engine{log, context, provider, "deterministic-fixture"};
    engine.turn({"", "return blackbird.request()"});
    if (!provider.admission)
      throw Error{ErrorCode::corrupt};
    const auto settled = unwrap(root->attempt(provider.admission->attempt));
    if (!settled.opened || settled.reconciliation_required || !settled.observation ||
        settled.observation->phase != AttemptPhase::terminal ||
        settled.observation->disposition != AttemptDisposition::success)
      throw Error{ErrorCode::corrupt};
    if (io.forbidden_attempts != 0)
      throw Error{ErrorCode::corrupt};
    if (mode == "migrate")
      unwrap(context.checkpoint());
    const auto admitted = provider.admitted;
    if (admitted == std::chrono::steady_clock::time_point{})
      throw Error{ErrorCode::corrupt};
    auto ms = [&](auto t) {
      return std::chrono::duration<double, std::milli>(t - start).count();
    };
    std::cout << "initial_bytes=" << initial_bytes << " initial_facts=" << initial_facts
              << " bytes=" << std::filesystem::file_size(path / "audit")
              << " facts=" << root->fact_count() << " live_bytes=" << serialized.size()
              << " request_input_bytes=" << provider.input_bytes
              << " replay_ms=" << ms(replay) << " restored_ms=" << ms(restored)
              << " prompt_ms=" << ms(ready) << " admitted_ms=" << ms(admitted)
              << " compact=" << root->compact_recovery()
              << " saved_current=" << (root->saved_state() != nullptr)
              << " indexed=" << root->used_scan_checkpoint()
              << " replay_read_bytes=" << replay_io.read_bytes
              << " replay_cpu_ms=" << replay_cpu_ms
              << " prompt_read_bytes=" << prompt_io.read_bytes
              << " readiness_read_bytes=" << provider.readiness_io.read_bytes
              << " readiness_syncs=" << provider.readiness_io.sync_calls
              << " readiness_cpu_ms=" << provider.readiness_cpu_ms
              << " prompt_rss=" << prompt_memory.resident
              << " prompt_footprint=" << prompt_memory.footprint
              << " readiness_rss=" << provider.readiness_memory.resident
              << " readiness_footprint=" << provider.readiness_memory.footprint
              << " readiness_virtual=" << provider.readiness_memory.virtual_bytes
              << '\n';
  } catch (const Error &e) {
    std::cerr << "error=" << error_name(e.code) << " detail=" << e.detail
              << " forbidden_attempts=" << io.forbidden_attempts << '\n';
    return 1;
  }
}
