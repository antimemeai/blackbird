#include "blackbird/context.hpp"
#include "blackbird/json.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/raw_storage.hpp"
#include "blackbird/trace_archive.hpp"
#include "blackbird/workflow_host.hpp"
#include "blackbird/workflow_runtime.hpp"
#include <atomic>
#include <cerrno>
#include <csignal>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace blackbird;
namespace {
template <class T> T random_identity() {
  IdentityBytes bytes{};
  if (::getentropy(bytes.data(), bytes.size()) != 0)
    throw Error{ErrorCode::io, errno};
  return unwrap(T::from_bytes(bytes));
}
Value json_export(const Value &native) {
  if (std::holds_alternative<std::string>(native.value())) {
    if (valid_utf8(native.string()))
      return native;
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(native.string().size() * 2);
    for (const char byte : native.string()) {
      const auto value = static_cast<unsigned char>(byte);
      encoded += digits[value >> 4U];
      encoded += digits[value & 15U];
    }
    return Value::object(
        {{"encoding", Value{"hex"}}, {"bytes", Value{std::move(encoded)}}});
  }
  if (std::holds_alternative<Value::Array>(native.value())) {
    Value::Array result;
    result.reserve(native.array().size());
    for (const auto &member : native.array())
      result.push_back(json_export(member));
    return Value{std::move(result)};
  }
  if (std::holds_alternative<Value::Object>(native.value())) {
    Value::Object result;
    result.reserve(native.object().size());
    for (const auto &[name, member] : native.object())
      result.emplace_back(name, json_export(member));
    return Value{std::move(result)};
  }
  return native;
}
class EmptyCustody final : public CustodyVerifier {
public:
  Result<void> verify(std::span<const AttemptState> attempts) override {
    return attempts.empty() ? Result<void>::success()
                            : Result<void>::failure({ErrorCode::external_unknown});
  }
};
class SignalMask {
public:
  SignalMask() {
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    const int error = ::pthread_sigmask(SIG_BLOCK, &signals, &previous_);
    if (error != 0)
      throw Error{ErrorCode::io, error};
  }
  ~SignalMask() { (void)::pthread_sigmask(SIG_SETMASK, &previous_, nullptr); }
  SignalMask(const SignalMask &) = delete;
  SignalMask &operator=(const SignalMask &) = delete;
  sigset_t signals{};

private:
  sigset_t previous_{};
};
class SignalWaiter {
public:
  SignalWaiter(const sigset_t &signals, WorkflowHost &host)
      : worker_([this, signals, &host] {
          while (!closing_.load()) {
            int signal = 0;
            if (::sigwait(&signals, &signal) != 0)
              continue;
            if (!closing_.load()) {
              requested.store(true);
              host.wake();
            }
          }
        }) {}
  ~SignalWaiter() {
    closing_.store(true);
    (void)::pthread_kill(worker_.native_handle(), SIGTERM);
    worker_.join();
  }
  SignalWaiter(const SignalWaiter &) = delete;
  SignalWaiter &operator=(const SignalWaiter &) = delete;
  std::atomic_bool requested{false};

private:
  std::atomic_bool closing_{false};
  std::thread worker_;
};
void private_session(const std::filesystem::path &session) {
  if (::mkdir(session.c_str(), 0700) != 0 && errno != EEXIST)
    throw Error{ErrorCode::io, errno};
  struct stat status{};
  if (::lstat(session.c_str(), &status) != 0)
    throw Error{ErrorCode::io, errno};
  if (!S_ISDIR(status.st_mode) || status.st_uid != ::geteuid() ||
      (status.st_mode & 0077) != 0)
    throw Error{ErrorCode::conflict};
}
int serve(const std::filesystem::path &session) {
  private_session(session);
  const auto audit = session / "audit";
  const bool existing = std::filesystem::exists(audit);
  std::optional<JournalHeader> header;
  if (existing) {
    std::array<std::byte, journal_header_size> bytes{};
    std::ifstream input{audit, std::ios::binary};
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input)
      throw Error{ErrorCode::incomplete};
    header = unwrap(decode_journal_header(bytes));
    if (header->predecessor)
      throw Error{ErrorCode::unsupported};
  } else {
    std::uint64_t issuer = 0;
    if (::getentropy(&issuer, sizeof(issuer)) != 0 || issuer == 0)
      throw Error{ErrorCode::io, errno};
    header = JournalHeader{random_identity<EnvironmentId>(),
                           random_identity<AuditStreamId>(),
                           issuer,
                           {32 * 1024 * 1024, 96 * 1024 * 1024},
                           std::nullopt};
  }
  SignalMask mask;
  auto archive = TraceArchiveWorker::start(session);
  RawRuntimeOptions options;
  options.producer_capacities = {256 * 1024 * 1024, 1024 * 1024, 128 * 1024};
  auto capture =
      unwrap(RawRuntime::open(session.string(), header->environment, options));
  RawThreadBinding binding{unwrap(capture->prepare_thread())};
  std::optional<RawRegion> startup;
  startup.emplace(raw_regions::startup_initialize);
  auto storage = unwrap(RawProductStorageOwner::prepare(
      RawEventRef{startup->lane_epoch(), startup->begin_ordinal()}));
  auto directory = std::make_unique<NativeJournalDirectory>(
      unwrap(NativeJournalDirectory::open(session.string(), storage)));
  const JournalCapacity capacity{512ULL * 1024 * 1024, 200000};
  auto root =
      existing
          ? unwrap(RetainedState::open(std::move(directory), "audit", *header, capacity,
                                       false, true, 512ULL * 1024 * 1024, storage))
          : unwrap(RetainedState::create(std::move(directory), "audit", *header,
                                         capacity, storage));
  if (existing) {
    unwrap(root->confirm_recovery());
    EmptyCustody verifier;
    const auto reconciled = root->reconcile(verifier);
    if (!reconciled.has_value() &&
        reconciled.error().code != ErrorCode::external_unknown)
      throw reconciled.error();
  }
  startup.reset();
  (void)WorkflowHost::remove_stale_endpoint(session);
  AuditLog log{*root};
  WorkflowRuntime *runtime_pointer = nullptr;
  bool stop = false;
  WorkflowHost host{session,
                    [&](const Value &request) {
                      auto response = runtime_pointer->request(request);
                      if (string_field(request, "op") == "shutdown")
                        stop = true;
                      return response;
                    },
                    [&] { runtime_pointer->tick(); }};
  WorkflowRuntime runtime{log,
                          {header->environment, random_identity<ProcessIncarnationId>(),
                           random_identity<RegistryId>()},
                          {4, 32},
                          capture.get(),
                          [&] { host.wake(); }};
  runtime_pointer = &runtime;
  SignalWaiter signals{mask.signals, host};
  try {
    host.serve([&] {
      if (signals.requested.exchange(false)) {
        try {
          runtime.shutdown();
          stop = true;
        } catch (const Error &error) {
          std::cerr << "workflow shutdown pending: " << error_name(error.code) << ':'
                    << error.detail << '\n';
        }
      }
      return stop;
    });
    runtime.shutdown();
  } catch (...) {
    const auto failure = std::current_exception();
    try {
      runtime.shutdown();
    } catch (...) {
    }
    std::rethrow_exception(failure);
  }
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  try {
    std::filesystem::path session;
    std::optional<std::string> request;
    bool server = false;
    for (int index = 1; index < argc; ++index) {
      const std::string_view argument{argv[index]};
      if (argument == "--serve")
        server = true;
      else if (argument == "--session" && index + 1 < argc)
        session = argv[++index];
      else if (argument == "--request" && index + 1 < argc)
        request = argv[++index];
      else
        throw Error{ErrorCode::invalid_range};
    }
    if (session.empty() || server == request.has_value()) {
      std::cerr << "usage: blackbird-workflow --session PATH "
                   "(--serve | --request JSON)\n";
      return 2;
    }
    session = std::filesystem::absolute(session);
    if (server)
      return serve(session);
    const auto response = WorkflowHost::request(session, unwrap(parse_json(*request)));
    std::cout << unwrap(dump_json(json_export(response))) << '\n';
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  } catch (...) {
    std::cerr << "workflow host failure\n";
  }
  return 1;
}
