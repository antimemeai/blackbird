#include "blackbird/command_jobs.hpp"
#include "blackbird/foundation.hpp"
#include "blackbird/json.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/raw_retained.hpp"
#include "blackbird/tools.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <utility>
extern char **environ;
namespace blackbird {
namespace {
using detail::raw_native_field;
using detail::raw_string_field;
using Clock = std::chrono::steady_clock;
constexpr std::size_t pending_limit = 1024 * 1024;
constexpr std::size_t output_limit = 16 * 1024 * 1024;
// Default per-command output budget. At most one 8KiB read past it is retained:
// about 129 chunks (two audit records each), a quarter of a continued session's
// 1024-record resident tail, which cannot retire while the command is unresolved.
constexpr std::size_t default_output_budget = 1024 * 1024;
constexpr std::size_t input_limit = 65536;
std::uint64_t delivery_incarnation() {
  static std::atomic<std::uint64_t> next{1};
  auto current = next.load();
  for (;;) {
    if (current == std::numeric_limits<std::uint64_t>::max())
      throw Error{ErrorCode::overflow};
    if (next.compare_exchange_weak(current, current + 1))
      return current;
  }
}
CommandDeliveryOperation delivery_operation(std::string_view operation) noexcept {
  return operation == "input"    ? CommandDeliveryOperation::input
         : operation == "resize" ? CommandDeliveryOperation::resize
         : operation == "signal" ? CommandDeliveryOperation::signal
                                 : CommandDeliveryOperation::absent;
}
CommandDeliverySignal delivery_signal(std::string_view name) noexcept {
  return name == "stop"        ? CommandDeliverySignal::stop
         : name == "continue"  ? CommandDeliverySignal::resume
         : name == "interrupt" ? CommandDeliverySignal::interrupt
         : name == "terminate" ? CommandDeliverySignal::terminate
         : name == "kill"      ? CommandDeliverySignal::kill
                               : CommandDeliverySignal::unsupported;
}
std::uint64_t integer(const Value &v, std::string_view key, std::uint64_t fallback) {
  const auto *p = v.find(key);
  if (!p)
    return fallback;
  if (!std::holds_alternative<Number>(p->value()))
    throw Error{ErrorCode::invalid_range};
  const auto &s = p->number().text();
  std::uint64_t n = 0;
  const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
  if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
    throw Error{ErrorCode::invalid_range};
  return n;
}
void validate_control(const Value &value, bool mailbox) {
  if (!std::holds_alternative<Value::Object>(value.value()))
    throw Error{ErrorCode::invalid_range};
  const auto *operation = value.find("op");
  if (!operation || !std::holds_alternative<std::string>(operation->value()))
    throw Error{ErrorCode::invalid_range};
  const auto &op = operation->string();
  if (op != "read" && op != "foreground" && op != "background" && op != "input" &&
      op != "resize" && op != "signal" && op != "stop" && op != "archive" &&
      op != "configure")
    throw Error{ErrorCode::unsupported};
  if (mailbox && op == "foreground")
    throw Error{ErrorCode::busy};
  for (const auto &[key, item] : value.object()) {
    const bool allowed =
        key == "op" || key == "job_id" || key == "request_id" ||
        (op == "read" && (key == "count" || key == "offset")) ||
        (op == "foreground" && (key == "yield_ms" || key == "output_max_bytes")) ||
        (op == "background" && key == "wait_epoch") ||
        (op == "input" && key == "bytes") ||
        (op == "resize" && (key == "rows" || key == "columns")) ||
        (op == "signal" && key == "signal") ||
        (op == "configure" && (key == "max_live" || key == "max_records"));
    if (!allowed)
      throw Error{ErrorCode::invalid_range};
    if (key == "op" || key == "job_id" || key == "request_id" || key == "bytes" ||
        key == "signal") {
      if (!std::holds_alternative<std::string>(item.value()))
        throw Error{ErrorCode::invalid_range};
      const auto bound = key == "bytes"        ? input_limit
                         : key == "request_id" ? 128U
                         : key == "job_id"     ? 32U
                                               : 16U;
      if (item.string().size() > bound)
        throw Error{ErrorCode::invalid_range};
    } else {
      const auto bound = key == "count"                      ? 65536ULL
                         : key == "yield_ms"                 ? 3600000ULL
                         : key == "output_max_bytes"         ? output_limit
                         : key == "rows" || key == "columns" ? 512ULL
                         : key == "max_live"                 ? 64ULL
                         : key == "max_records"              ? 256ULL
                                                             : UINT64_MAX;
      if (integer(value, key, 0) > bound)
        throw Error{ErrorCode::invalid_range};
    }
  }
  if (op == "input" && !value.find("bytes"))
    throw Error{ErrorCode::invalid_range};
  if (op == "signal" && !value.find("signal"))
    throw Error{ErrorCode::invalid_range};
  if (!mailbox && (op == "input" || op == "resize" || op == "signal") &&
      (!value.find("request_id") || string_field(value, "request_id").empty()))
    throw Error{ErrorCode::invalid_range};
}
bool same_request(const Value &left, const Value &right) {
  if (left.value().index() != right.value().index())
    return false;
  if (std::holds_alternative<Value::Object>(left.value())) {
    if (left.object().size() != right.object().size())
      return false;
    for (const auto &[key, value] : left.object()) {
      const auto *other = right.find(key);
      if (!other || !same_request(value, *other))
        return false;
    }
    return true;
  }
  if (std::holds_alternative<Value::Array>(left.value())) {
    if (left.array().size() != right.array().size())
      return false;
    for (std::size_t i = 0; i < left.array().size(); ++i)
      if (!same_request(left.array()[i], right.array()[i]))
        return false;
    return true;
  }
  return left == right;
}
bool boolean(const Value &v, std::string_view key, bool fallback = false) {
  const auto *p = v.find(key);
  if (!p)
    return fallback;
  if (!std::holds_alternative<bool>(p->value()))
    throw Error{ErrorCode::invalid_range};
  return std::get<bool>(p->value());
}
void put(Value &v, std::string key, Value item) {
  for (auto &[old, value] : v.object())
    if (old == key) {
      value = std::move(item);
      return;
    }
  v.object().emplace_back(std::move(key), std::move(item));
}
struct Fd {
  int value = -1;
  RawEventRef *launch = nullptr;
  std::optional<RawControlToken> *credit = nullptr;
  std::uint8_t channel = 0;
  Fd() = default;
  explicit Fd(int descriptor) : value(descriptor) {}
  ~Fd() { close(); }
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;
  void close() noexcept {
    if (value >= 0) {
      const int fd = std::exchange(value, -1);
      const std::uint8_t reason = 1;
      RawEventRef request;
      auto *held = credit && *credit ? &**credit : nullptr;
      if (launch)
        request = raw_command_linked(2030, *launch,
                                     std::array{raw_native_field(channel),
                                                raw_native_field(fd),
                                                raw_native_field(reason)},
                                     held);
      const int result = ::close(fd), error = errno;
      if (launch)
        (void)raw_command_linked(
            2031, request,
            std::array{raw_native_field(result), raw_native_field(error)}, held);
    }
  }
};
void pipe_fds(Fd &reader, Fd &writer) {
  int fds[2];
  if (::pipe(fds) != 0)
    throw Error{ErrorCode::io, errno};
  reader.value = fds[0];
  writer.value = fds[1];
  for (auto *fd : {&reader, &writer}) {
    const int normalized = ::fcntl(fd->value, F_DUPFD_CLOEXEC, 4);
    if (normalized < 0)
      throw Error{ErrorCode::io, errno};
    fd->close();
    fd->value = normalized;
  }
}
void nonblocking(int descriptor) {
  const int flags = ::fcntl(descriptor, F_GETFL);
  if (flags < 0 || ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0)
    throw Error{ErrorCode::io, errno};
}
struct Launch {
  std::vector<std::string> argv;
  bool pty = false;
  std::chrono::seconds lifetime{120};
  std::size_t output_budget = default_output_budget;
};
Launch parse_launch(const Value &arguments) {
  if (!std::holds_alternative<Value::Object>(arguments.value()))
    throw Error{ErrorCode::invalid_range};
  Launch out;
  out.pty = boolean(arguments, "pty");
  const auto seconds = integer(arguments, "timeout_seconds", 120);
  if (seconds < 1 || seconds > 3600)
    throw Error{ErrorCode::invalid_range};
  out.lifetime = std::chrono::seconds{seconds};
  const auto budget = integer(arguments, "output_limit_bytes", default_output_budget);
  if (budget < 1 || budget > output_limit)
    throw Error{ErrorCode::invalid_range};
  out.output_budget = static_cast<std::size_t>(budget);
  if (const auto *argv = arguments.find("argv")) {
    for (const auto &arg : argv->array()) {
      if (arg.string().find('\0') != std::string::npos)
        throw Error{ErrorCode::invalid_range};
      out.argv.push_back(arg.string());
    }
  }
  if (out.argv.empty()) {
    const auto command = string_field(arguments, "command");
    if (command.find('\0') != std::string::npos)
      throw Error{ErrorCode::invalid_range};
    out.argv = {"/bin/sh", "-c", command};
  }
  (void)integer(arguments, "output_max_bytes", output_limit);
  if (integer(arguments, "yield_ms", 0) > 3600000)
    throw Error{ErrorCode::invalid_range};
  return out;
}
class ControlTrace {
public:
  explicit ControlTrace(const Value &arguments) {
    if (!raw_enabled())
      return;
    credit_ = raw_command_claim(60);
    // Existing request strings are copied, never encoded as Value.
    auto text = [&](std::string_view key) -> std::string_view {
      const auto *value = arguments.find(key);
      if (!value)
        return {};
      if (std::holds_alternative<std::string>(value->value()))
        return value->string();
      return {};
    };
    const auto job = text("job_id"), operation = text("op"),
               delivery = text("request_id");
    const auto job_size = job.size(), operation_size = operation.size(),
               delivery_size = delivery.size();
    const auto bytes = text("bytes"), signal = text("signal");
    // Numeric requests are captured at their existing business-native parse sites.
    const std::string_view rows{}, columns{}, epoch{};
    const auto byte_size = bytes.size(), signal_size = signal.size(),
               rows_size = rows.size(), columns_size = columns.size(),
               epoch_size = epoch.size();
    request_ = raw_command_data(
        2013, std::array{raw_native_field(job_size), raw_string_field(job),
                         raw_native_field(operation_size), raw_string_field(operation),
                         raw_native_field(delivery_size), raw_string_field(delivery),
                         raw_native_field(byte_size), raw_string_field(bytes),
                         raw_native_field(signal_size), raw_string_field(signal),
                         raw_native_field(rows_size), raw_string_field(rows),
                         raw_native_field(columns_size), raw_string_field(columns),
                         raw_native_field(epoch_size), raw_string_field(epoch)});
  }
  RawEventRef reference() const noexcept { return request_; }
  ~ControlTrace() {
    if (!finished_)
      finish(5, 0, false, false, {ErrorCode::audit_unavailable});
  }
  void finish(std::uint8_t branch, std::size_t written, bool done, bool failed,
              Error error = {}) noexcept {
    if (finished_)
      return;
    finished_ = true;
    if (credit_)
      (void)raw_command_linked(
          2014, request_,
          std::array{raw_native_field(branch), raw_native_field(written),
                     raw_native_field(done), raw_native_field(failed),
                     raw_native_field(error.code), raw_native_field(error.detail)},
          &*credit_);
  }

private:
  RawEventRef request_;
  std::optional<RawControlToken> credit_;
  bool finished_ = false;
};
} // namespace
struct CommandJobs::Impl {
  struct Delivery {
    Value request;
    const std::string &bytes() const { return string_field(request, "bytes"); }
    std::size_t written = 0;
    bool done = false;
    bool failed = false;
    CommandDeliveryOperation operation = CommandDeliveryOperation::absent;
    CommandDeliverySignal signal = CommandDeliverySignal::absent;
    CommandDeliveryConsumption consumption = CommandDeliveryConsumption::pending;
    RawEventRef admission{}, enqueue{}, selection{}, control{};
    std::array<CommandDeliveryCall, 3> calls{};
    std::size_t call_count = 0;
  };
  struct Reservation {
    std::shared_ptr<const Value> request;
    std::size_t bytes = 0;
    bool selected = false;
    CommandDeliveryOperation operation = CommandDeliveryOperation::absent;
    CommandDeliverySignal signal = CommandDeliverySignal::absent;
    RawEventRef admission{}, enqueue{}, selection{};
  };
  struct Chunk {
    std::size_t offset;
    std::shared_ptr<const std::string> bytes;
    RawEventRef original;
  };
  enum class Custody : std::uint8_t {
    not_launched,
    owned,
    reaped,
    unavailable,
    pending
  };
  struct Job {
    std::string id;
    std::uint64_t admission_order = 0;
    Launch launch;
    RawThreadHandle raw_context;
    std::unique_ptr<RawRetainedJobLoan> retained_loan;
    bool retained_loan_used = false;
    std::optional<RawControlToken> worker_credit, retry_credit, semantic_credit,
        caller_stop_credit;
    RawEventRef request, launch_ref, terminal_ref;
    std::optional<RawCommandWake> stop_wake;
    Custody custody = Custody::not_launched;
    bool counted = false;
    pid_t pid = -1;
    bool ready = false, stopped = false, leader_exited = false;
    bool output_closed = false, finished = false, terminal_sent = false;
    bool terminal_retained = false, stop = false, timed_out = false;
    std::size_t received = 0, retained = 0, pending_bytes = 0, tail_bytes = 0;
    std::size_t input_bytes = 0, input_history_bytes = 0;
    std::size_t reserved_bytes = 0, selected_bytes = 0;
    int exit_code = 0, signal = 0, failure = 0;
    std::string termination = "unobserved";
    bool capture_complete = true, capture_refused = false;
    std::deque<Chunk> pending, tail;
    std::map<std::string, Delivery, std::less<>> deliveries;
    std::map<std::string, Reservation, std::less<>> reservations;
    std::deque<std::string> input, signals;
    Clock::time_point deadline;
    Value terminal;
    std::uint64_t release_epoch = 0;
    unsigned short rows = 24, columns = 80;
    bool resize_pending = false;
    // Joined before the wake, its claims, and its producer are destroyed.
    std::jthread worker;
    ~Job() {
      if (worker.joinable())
        worker.join();
      if (retained_loan)
        retained_loan->joined();
    }
  };
  mutable std::mutex mutex;
  const std::uint64_t incarnation = delivery_incarnation();
  std::condition_variable condition;
  RawThreadHandle cleanup_context;
  std::optional<RawControlToken> shutdown_credit;
  std::map<std::string, std::shared_ptr<Job>, std::less<>> jobs;
  // Every access to cleanup producer/claims is serialized by mutex.
  void caller_stop(Job &job, std::uint8_t source) noexcept {
    RawThreadBinding owner{cleanup_context};
    if (!job.stop && !job.finished) {
      if (cleanup_context && job.caller_stop_credit) {
        const auto length = job.id.size();
        (void)raw_command_record(2029,
                                 std::array{raw_native_field(length),
                                            raw_string_field(job.id),
                                            raw_native_field(source)},
                                 &*job.caller_stop_credit);
      }
      job.stop = true;
      if (job.stop_wake)
        job.stop_wake->notify(job.caller_stop_credit ? &*job.caller_stop_credit
                                                     : nullptr);
    }
    job.caller_stop_credit.reset();
  }
  struct Mailbox {
    std::shared_ptr<const Value> value;
    RawEventRef source;
  };
  std::deque<Mailbox> mailbox;
  std::string waiting;
  std::uint64_t epoch = 0, request_counter = 0, admission_counter = 0;
  bool text_input = false;
  std::size_t max_live = 8, max_records = 32;
  Job &find(const std::string &id) const {
    const auto i = jobs.find(id);
    if (i == jobs.end())
      throw Error{ErrorCode::invalid_range};
    return *i->second;
  }
  std::size_t debt_locked() const {
    return static_cast<std::size_t>(
        std::count_if(jobs.begin(), jobs.end(), [](const auto &entry) {
          return !entry.second->terminal_retained || !entry.second->pending.empty() ||
                 !entry.second->reservations.empty() ||
                 entry.second->custody == Custody::pending ||
                 entry.second->custody == Custody::owned;
        }));
  }
  bool retirable(const Job &job) const {
    return job.finished && job.terminal_retained && job.pending.empty() &&
           job.custody != Custody::pending && job.custody != Custody::owned &&
           waiting != job.id && job.reservations.empty() &&
           std::none_of(job.deliveries.begin(), job.deliveries.end(),
                        [](const auto &entry) { return !entry.second.done; }) &&
           std::none_of(mailbox.begin(), mailbox.end(), [&](const Mailbox &entry) {
             return string_field(*entry.value, "job_id") == job.id;
           });
  }
  static void release_reservation(Job &job, const std::string &request) noexcept {
    const auto found = job.reservations.find(request);
    if (found == job.reservations.end())
      return;
    job.reserved_bytes -= found->second.bytes;
    if (found->second.selected)
      job.selected_bytes -= found->second.bytes;
    job.reservations.erase(found);
  }
  enum class Admission { ready, wait_pending_input, refuse_history };
  static Admission admission(const Job &job, std::size_t bytes,
                             const Reservation *own = nullptr,
                             std::size_t selected = 0) noexcept {
    const auto other_controls = job.reservations.size() - (own ? 1U : 0U);
    const auto own_bytes = own ? own->bytes : 0;
    const auto own_selected = own && own->selected ? own_bytes : 0;
    if (job.deliveries.size() + other_controls >= 4096 ||
        bytes >
            output_limit - job.input_history_bytes - (job.reserved_bytes - own_bytes))
      return Admission::refuse_history;
    if (bytes >
        input_limit - job.input_bytes - (job.selected_bytes - own_selected) - selected)
      return Admission::wait_pending_input;
    return Admission::ready;
  }
  struct View {
    std::size_t max_output = 0, offset = 0;
    bool output = false;
  };
  Value snapshot(const Job &j, View view) const {
    const auto max_output = view.max_output, offset = view.offset;
    Value result =
        Value::object({{"job_id", Value{j.id}},
                       {"output_ref", Value{j.id}},
                       {"state", Value{j.terminal_retained ? "completed"
                                       : j.finished        ? "settling"
                                       : j.leader_exited   ? "draining"
                                       : j.stopped         ? "stopped"
                                       : j.ready           ? "running"
                                                           : "starting"}},
                       {"pty", Value{j.launch.pty}},
                       {"launches", Value{Number{1}}},
                       {"received_bytes", Value{Number{j.received}}},
                       {"retained_bytes", Value{Number{j.retained}}},
                       {"sealed", Value{j.terminal_retained && j.capture_complete &&
                                        j.custody == Custody::reaped}},
                       {"native_custody_pending", Value{j.custody == Custody::pending ||
                                                        j.custody == Custody::owned}},
                       {"leader_exited", Value{j.leader_exited}},
                       {"output_closed", Value{j.output_closed}}});
    if (j.pid > 0)
      put(result, "pid", Value{Number{j.pid}});
    if (j.terminal_retained)
      for (const auto &[key, value] : j.terminal.object())
        put(result, key, value);
    if (view.output) {
      const auto first = j.tail.empty() ? j.retained : j.tail.front().offset;
      const auto begin = std::min(j.retained, std::max(offset, first));
      std::string bytes;
      for (const auto &chunk : j.tail) {
        const auto end = std::min(chunk.offset + chunk.bytes->size(), j.retained);
        if (end <= begin || bytes.size() >= max_output)
          continue;
        const auto at = std::max(chunk.offset, begin);
        bytes.append(*chunk.bytes, at - chunk.offset,
                     std::min(end - at, max_output - bytes.size()));
      }
      const auto shown = process_output_presentation(bytes, Value::object({}));
      put(result, "output", field(shown, "output"));
      put(result, "returned_bytes", Value{Number{bytes.size()}});
      put(result, "omitted_bytes", Value{Number{j.retained - bytes.size()}});
      put(result, "byte_start", Value{Number{begin}});
      put(result, "next_offset", Value{Number{begin + bytes.size()}});
      put(result, "omitted_prefix_bytes",
          Value{Number{begin - std::min(offset, begin)}});
    }
    return result;
  }
  void run(const std::shared_ptr<Job> &job) noexcept;
};
void CommandJobs::Impl::run(const std::shared_ptr<Job> &job) noexcept {
  RawThreadBinding raw_binding{job->raw_context};
  RawWaitScope raw_wait{
      {{job->stop_wake ? job->stop_wake->read_fd() : -1, -1}, job->deadline}};
  sigset_t blocked;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGWINCH);
  (void)::pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
  Fd input_read, input_write, output_read, output_write, boot_read, boot_write;
  pid_t pid = -1;
  bool group_ready = false, reaped = false;
  bool channel_closed = false;
  const auto deadline = job->deadline;
  std::optional<Clock::time_point> stopping;
  bool killed = false, spawn_written = false;
  std::uint8_t setup_stage = 0;
  std::array<unsigned char, sizeof(int) + 3> bootstrap{};
  std::size_t bootstrap_size = 0;
  auto *held = job->worker_credit ? &*job->worker_credit : nullptr;
  // Called with mutex held. A sticky stop has at most one false->true transition.
  auto worker_stop = [&](std::uint8_t source) {
    if (!job->stop) {
      const auto length = job->id.size();
      (void)raw_command_record(2029,
                               std::array{raw_native_field(length),
                                          raw_string_field(job->id),
                                          raw_native_field(source)},
                               held);
      job->stop = true;
      job->caller_stop_credit.reset();
    }
  };
  auto signal_owned = [&](int number, RawControlToken *credit,
                          std::string_view delivery = {},
                          Delivery *observed = nullptr) {
    if (pid <= 0 || reaped)
      return;
    const auto length = delivery.size();
    auto operation = [&](std::uint8_t method, pid_t target) {
      const auto request = raw_command_linked(
          2020, job->launch_ref,
          std::array{raw_native_field(length), raw_string_field(delivery),
                     raw_native_field(target), raw_native_field(number),
                     raw_native_field(method)},
          credit);
      const int result =
          method == 3 ? static_cast<int>(::getpgid(target)) : ::kill(target, number);
      const int error = errno;
      RawEventRef returned;
      if (method == 3) {
        const auto group = static_cast<pid_t>(result);
        returned = raw_command_linked(2021, request,
                                      std::array{raw_native_field(result),
                                                 raw_native_field(error),
                                                 raw_native_field(group)},
                                      credit);
      } else {
        returned = raw_command_linked(
            2021, request,
            std::array{raw_native_field(result), raw_native_field(error)}, credit);
      }
      // Only an explicit delivery passes observed, while holding mutex. Cleanup
      // calls retain their existing empty-key routing and do not alter a receipt.
      if (observed && observed->call_count < observed->calls.size())
        observed->calls[observed->call_count++] = {method, target,  number,  result,
                                                   error,  request, returned};
      return result;
    };
    if (!group_ready || operation(1, -pid) != 0 || operation(3, pid) != pid)
      (void)operation(2, pid);
  };
  try {
    const auto launch_id_size = job->id.size();
    const auto lifetime = job->launch.lifetime.count();
    const auto launch_ref = raw_command_linked(
        2002, job->request,
        std::array{raw_native_field(launch_id_size), raw_string_field(job->id),
                   raw_native_field(job->launch.pty), raw_native_field(lifetime)},
        held);
    {
      const std::lock_guard lock{mutex};
      job->launch_ref = launch_ref;
    }
    std::uint8_t channel = 0;
    for (auto *fd : {&input_read, &input_write, &output_read, &output_write, &boot_read,
                     &boot_write}) {
      fd->launch = &job->launch_ref;
      fd->credit = &job->worker_credit;
      fd->channel = channel++;
    }
    std::vector<std::string> arguments = job->launch.argv;
    {
      const std::lock_guard spawn_lock{detail::process_spawn_mutex};
      setup_stage = 1;
      std::string slave;
      if (job->launch.pty) {
        Fd master{::posix_openpt(O_RDWR | O_NOCTTY)};
        master.launch = &job->launch_ref;
        master.credit = &job->worker_credit;
        master.channel = 6;
        if (master.value < 0 || ::grantpt(master.value) != 0 ||
            ::unlockpt(master.value) != 0)
          throw Error{ErrorCode::io, errno};
        const char *name = ::ptsname(master.value);
        if (!name)
          throw Error{ErrorCode::io, errno};
        slave = name;
        output_read.value = ::fcntl(master.value, F_DUPFD_CLOEXEC, 4);
        if (output_read.value < 0)
          throw Error{ErrorCode::io, errno};
        input_write.value = ::fcntl(output_read.value, F_DUPFD_CLOEXEC, 4);
        if (input_write.value < 0)
          throw Error{ErrorCode::io, errno};
        pipe_fds(boot_read, boot_write);
        arguments.insert(arguments.begin(), slave);
        arguments.insert(arguments.begin(), BLACKBIRD_JOB_RUNNER);
      } else {
        pipe_fds(input_read, input_write);
        pipe_fds(output_read, output_write);
      }
      nonblocking(output_read.value);
      nonblocking(input_write.value);
      if (boot_read.value >= 0)
        nonblocking(boot_read.value);
#ifdef F_SETNOSIGPIPE
      if (::fcntl(input_write.value, F_SETNOSIGPIPE, 1) < 0)
        throw Error{ErrorCode::io, errno};
#endif
      posix_spawn_file_actions_t actions;
      posix_spawnattr_t attributes;
      int rc = ::posix_spawn_file_actions_init(&actions);
      if (rc != 0)
        throw Error{ErrorCode::io, rc};
      struct Actions {
        posix_spawn_file_actions_t &value;
        ~Actions() { (void)::posix_spawn_file_actions_destroy(&value); }
      } cleanup_actions{actions};
      rc = ::posix_spawnattr_init(&attributes);
      if (rc != 0)
        throw Error{ErrorCode::io, rc};
      struct Attributes {
        posix_spawnattr_t &value;
        ~Attributes() { (void)::posix_spawnattr_destroy(&value); }
      } cleanup_attributes{attributes};
      auto check = [](int error) {
        if (error != 0)
          throw Error{ErrorCode::io, error};
      };
      sigset_t empty, defaults;
      sigemptyset(&empty);
      sigemptyset(&defaults);
      for (int number : {SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD, SIGTSTP,
                         SIGTTIN, SIGTTOU, SIGUSR1, SIGUSR2, SIGWINCH})
        sigaddset(&defaults, number);
      check(::posix_spawnattr_setsigmask(&attributes, &empty));
      check(::posix_spawnattr_setsigdefault(&attributes, &defaults));
      short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
      if (!job->launch.pty) {
        flags = static_cast<short>(flags | POSIX_SPAWN_SETPGROUP);
        check(::posix_spawnattr_setpgroup(&attributes, 0));
        check(::posix_spawn_file_actions_adddup2(&actions, input_read.value, 0));
        check(::posix_spawn_file_actions_adddup2(&actions, output_write.value, 1));
        check(::posix_spawn_file_actions_adddup2(&actions, output_write.value, 2));
      } else {
        check(::posix_spawn_file_actions_adddup2(&actions, boot_write.value, 3));
        for (int destination = 0; destination < 3; ++destination)
          check(::posix_spawn_file_actions_addopen(&actions, destination, "/dev/null",
                                                   destination ? O_WRONLY : O_RDONLY,
                                                   0));
      }
      check(::posix_spawnattr_setflags(&attributes, flags));
      std::vector<char *> argv;
      for (auto &argument : arguments)
        argv.push_back(argument.data());
      argv.push_back(nullptr);
      setup_stage = 2;
      rc = ::posix_spawnp(&pid, argv.front(), &actions, &attributes, argv.data(),
                          environ);
      const int spawn_error = errno;
      const std::uint8_t stage = setup_stage;
      spawn_written = true;
      const bool spawned_group = !job->launch.pty && rc == 0;
      (void)raw_command_linked(2003, job->launch_ref,
                               std::array{raw_native_field(stage), raw_native_field(rc),
                                          raw_native_field(spawn_error),
                                          raw_native_field(pid),
                                          raw_native_field(spawned_group)},
                               held);
      check(rc);
      group_ready = !job->launch.pty;
      ++detail::owned_children;
      {
        const std::lock_guard lock{mutex};
        job->counted = true;
        job->custody = Custody::owned;
      }
      detail::uncontained_exec.store(true);
      input_read.close();
      output_write.close();
      boot_write.close();
      if (!job->launch.pty)
        input_write.close();
    }
    {
      const std::lock_guard lock{mutex};
      job->pid = pid;
      job->ready = group_ready;
      condition.notify_all();
    }
    while (true) {
      bool stop = false;
      bool exited = false;
      std::size_t available = 0;
      {
        const std::lock_guard lock{mutex};
        if (Clock::now() >= deadline) {
          worker_stop(2);
          job->timed_out = true;
          if (job->stop_wake)
            job->stop_wake->notify(held);
        }
        stop = job->stop;
        exited = job->leader_exited;
        available = pending_limit - std::min(pending_limit, job->pending_bytes);
      }
      if (stop && !stopping) {
        stopping = Clock::now();
        signal_owned(SIGTERM, held);
      }
      if (stopping && !killed &&
          Clock::now() - *stopping >= std::chrono::milliseconds{200}) {
        signal_owned(SIGKILL, held);
        killed = true;
      }
      if (boot_read.value >= 0) {
        auto read_credit = raw_command_claim(104);
        raw_command_data_credit(105);
        const auto requested = bootstrap.size() - bootstrap_size;
        const auto request = raw_command_linked(
            2004, job->launch_ref,
            std::array{raw_native_field(boot_read.value), raw_native_field(requested)},
            read_credit ? &*read_credit : nullptr);
        const auto n =
            ::read(boot_read.value, bootstrap.data() + bootstrap_size, requested);
        const int read_error = errno;
        const auto literal =
            n > 0 ? std::as_bytes(std::span{bootstrap.data() + bootstrap_size,
                                            static_cast<std::size_t>(n)})
                  : ByteView{};
        (void)raw_command_linked(
            2005, request,
            std::array{raw_native_field(n), raw_native_field(read_error), literal},
            n > 0         ? nullptr
            : read_credit ? &*read_credit
                          : nullptr);
        if (n > 0)
          bootstrap_size += static_cast<std::size_t>(n);
        else if (n == 0) {
          if (!group_ready || bootstrap_size != 0)
            throw Error{ErrorCode::io, EPROTO};
          boot_read.close();
        } else if (errno != EAGAIN && errno != EINTR)
          throw Error{ErrorCode::io, errno};
        if (bootstrap_size && bootstrap[0] == 1) {
          if (group_ready)
            throw Error{ErrorCode::io, EPROTO};
          group_ready = true;
          const std::uint8_t native_stage = 0;
          const int failure = 0;
          (void)raw_command_linked(2006, job->launch_ref,
                                   std::array{raw_native_field(group_ready),
                                              raw_native_field(native_stage),
                                              raw_native_field(failure)});
          --bootstrap_size;
          std::memmove(bootstrap.data(), bootstrap.data() + 1, bootstrap_size);
          const std::lock_guard lock{mutex};
          job->ready = true;
          job->resize_pending = true;
        }
        if (bootstrap_size) {
          if (bootstrap[0] != 2)
            throw Error{ErrorCode::io, EPROTO};
          if (bootstrap_size >= sizeof(int) + 2) {
            int failure = EIO;
            const auto native_stage = bootstrap[1];
            std::memcpy(&failure, bootstrap.data() + 2, sizeof(failure));
            (void)raw_command_linked(2006, job->launch_ref,
                                     std::array{raw_native_field(group_ready),
                                                raw_native_field(native_stage),
                                                raw_native_field(failure)});
            const std::lock_guard lock{mutex};
            job->failure = failure > 0 ? failure : EPROTO;
            worker_stop(3);
            boot_read.close();
          }
        }
      }

      auto control_credit = raw_command_claim(1055);
      auto *control_held = control_credit ? &*control_credit : nullptr;
      {
        const std::lock_guard lock{mutex};
        if (group_ready && job->launch.pty && job->resize_pending) {
          winsize size{job->rows, job->columns, 0, 0};
          const std::size_t delivery_size = 0;
          const auto request = raw_command_linked(
              2018, job->launch_ref,
              std::array{raw_native_field(delivery_size), ByteView{},
                         raw_native_field(output_read.value),
                         raw_native_field(size.ws_row), raw_native_field(size.ws_col)},
              control_held);
          const int rc = ::ioctl(output_read.value, TIOCSWINSZ, &size), error = errno;
          (void)raw_command_linked(
              2019, request, std::array{raw_native_field(rc), raw_native_field(error)},
              control_held);
          if (rc != 0)
            job->failure = error;
          job->resize_pending = false;
        }
        if (!job->signals.empty()) {
          auto &delivery = job->deliveries.at(job->signals.front());
          const auto name = string_field(delivery.request, "signal");
          const int number = name == "stop"        ? SIGSTOP
                             : name == "continue"  ? SIGCONT
                             : name == "interrupt" ? SIGINT
                             : name == "terminate" ? SIGTERM
                                                   : SIGKILL;
          delivery.failed = job->leader_exited;
          if (!delivery.failed)
            signal_owned(number, control_held, job->signals.front(), &delivery);
          delivery.consumption = CommandDeliveryConsumption::worker;
          delivery.done = true;
          job->signals.pop_front();
        }
        if (!job->input.empty()) {
          auto &delivery = job->deliveries.at(job->input.front());
          const auto op = string_field(delivery.request, "op");
          if (op == "input" && group_ready) {
            const auto remaining = delivery.bytes().size() - delivery.written;
            const auto &delivery_id = job->input.front();
            const auto delivery_size = delivery_id.size();
            const auto request =
                raw_command_linked(2016, job->launch_ref,
                                   std::array{raw_native_field(delivery_size),
                                              raw_string_field(delivery_id),
                                              raw_native_field(input_write.value),
                                              raw_native_field(delivery.written),
                                              raw_native_field(remaining)},
                                   control_held);
            const auto n =
                ::write(input_write.value, delivery.bytes().data() + delivery.written,
                        remaining);
            const int error = errno;
            (void)raw_command_linked(
                2017, request, std::array{raw_native_field(n), raw_native_field(error)},
                control_held);
            if (n > 0) {
              const auto size = static_cast<std::size_t>(n);
              delivery.written += size;
              job->input_bytes -= size;
            } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
              delivery.failed = true;
              job->input_bytes -= remaining;
            }
            delivery.done =
                delivery.failed || delivery.written == delivery.bytes().size();
            if (delivery.done)
              delivery.consumption = CommandDeliveryConsumption::worker;
          } else if (op == "resize" && group_ready) {
            winsize size{
                static_cast<unsigned short>(integer(delivery.request, "rows", 24)),
                static_cast<unsigned short>(integer(delivery.request, "columns", 80)),
                0, 0};
            const auto &delivery_id = job->input.front();
            const auto delivery_size = delivery_id.size();
            const auto request =
                raw_command_linked(2018, job->launch_ref,
                                   std::array{raw_native_field(delivery_size),
                                              raw_string_field(delivery_id),
                                              raw_native_field(output_read.value),
                                              raw_native_field(size.ws_row),
                                              raw_native_field(size.ws_col)},
                                   control_held);
            const int rc = ::ioctl(output_read.value, TIOCSWINSZ, &size), error = errno;
            (void)raw_command_linked(
                2019, request,
                std::array{raw_native_field(rc), raw_native_field(error)},
                control_held);
            delivery.failed = rc != 0;
            delivery.done = true;
            delivery.consumption = CommandDeliveryConsumption::worker;
          }
          if (delivery.done)
            job->input.pop_front();
        }
      }

      if (!channel_closed && available >= 8192) {
        char bytes[8192];
        auto read_credit = raw_command_claim(112);
        raw_command_data_credit(8244 + 114);
        const std::size_t requested = sizeof(bytes);
        const auto offset = job->received;
        const auto request = raw_command_linked(
            2007, job->launch_ref,
            std::array{raw_native_field(output_read.value), raw_native_field(offset),
                       raw_native_field(requested)},
            read_credit ? &*read_credit : nullptr);
        const auto n = ::read(output_read.value, bytes, sizeof(bytes));
        const int error = errno;
        const auto literal =
            n > 0 ? std::as_bytes(std::span{bytes, static_cast<std::size_t>(n)})
                  : ByteView{};
        const auto original = raw_command_linked(
            2008, request,
            std::array{raw_native_field(n), raw_native_field(error), literal},
            n > 0         ? nullptr
            : read_credit ? &*read_credit
                          : nullptr);
        if (n > 0) {
          auto chunk =
              std::make_shared<const std::string>(bytes, static_cast<std::size_t>(n));
          const std::lock_guard lock{mutex};
          const auto budget = job->launch.output_budget;
          if (chunk->size() > budget - std::min(budget, job->received)) {
            job->capture_complete = false;
            job->failure = EFBIG;
            worker_stop(3);
          }
          {
            const Chunk entry{job->received, std::move(chunk), original};
            job->received += entry.bytes->size();
            job->pending_bytes += entry.bytes->size();
            job->tail_bytes += entry.bytes->size();
            job->pending.push_back(entry);
            job->tail.push_back(entry);
            while (job->tail_bytes > pending_limit && job->tail.size() > 1) {
              job->tail_bytes -= job->tail.front().bytes->size();
              job->tail.pop_front();
            }
            const auto id_size = job->id.size();
            const std::uint8_t branch = job->failure == EFBIG ? 2 : 1;
            const Error error_fields =
                job->failure == EFBIG ? Error{ErrorCode::capacity, EFBIG} : Error{};
            (void)raw_command_linked(
                2009, original,
                std::array{raw_native_field(id_size), raw_string_field(job->id),
                           raw_native_field(entry.offset),
                           raw_native_field(job->pending_bytes),
                           raw_native_field(job->tail_bytes), raw_native_field(branch),
                           raw_native_field(error_fields.code),
                           raw_native_field(error_fields.detail)});
            condition.notify_all();
          }
          if (job->failure == EFBIG) {
            channel_closed = true;
            output_read.close();
            job->output_closed = true;
            if (job->stop_wake)
              job->stop_wake->notify(held);
          }
        } else if (n == 0 ||
                   (n < 0 && errno == EIO && job->launch.pty && group_ready)) {
          channel_closed = true;
          output_read.close();
          const std::lock_guard lock{mutex};
          job->output_closed = true;
        } else if (n < 0 && errno != EAGAIN && errno != EINTR &&
                   !(errno == EIO && job->launch.pty && !group_ready)) {
          throw Error{ErrorCode::io, errno};
        }
      }
      auto observe_wait = [&](siginfo_t &observed, int options) {
        auto credit = raw_command_claim(116);
        const auto type = P_PID;
        const auto selector = static_cast<id_t>(pid);
        const auto request = raw_command_linked(2022, job->launch_ref,
                                                std::array{raw_native_field(type),
                                                           raw_native_field(selector),
                                                           raw_native_field(options)},
                                                credit ? &*credit : nullptr);
        const int result = ::waitid(type, selector, &observed, options), error = errno;
        (void)raw_command_linked(2023, request,
                                 std::array{raw_native_field(result),
                                            raw_native_field(error),
                                            raw_native_field(observed.si_pid),
                                            raw_native_field(observed.si_uid),
                                            raw_native_field(observed.si_code),
                                            raw_native_field(observed.si_status)},
                                 credit ? &*credit : nullptr);
        // A status-only selector can report ECHILD for our unreaped zombie.
        // Only an exit-inclusive query can revoke final-reap custody.
        if (result < 0 && error == ECHILD && (options & WEXITED) != 0) {
          reaped = true;
          const std::lock_guard lock{mutex};
          job->custody = Custody::unavailable;
        }
        return result;
      };
      if (!exited) {
        siginfo_t observed{};
        const int rc = observe_wait(observed, WEXITED | WNOHANG | WNOWAIT);
        if (rc < 0 && errno != EINTR)
          throw Error{ErrorCode::io, errno};
        if (rc == 0 && observed.si_pid == pid &&
            (observed.si_code == CLD_EXITED || observed.si_code == CLD_KILLED ||
             observed.si_code == CLD_DUMPED)) {
          const std::lock_guard lock{mutex};
          job->leader_exited = true;
          job->termination = observed.si_code == CLD_EXITED ? "exit" : "signal";
          job->signal = observed.si_code == CLD_EXITED ? 0 : observed.si_status;
          job->exit_code = observed.si_code == CLD_EXITED ? observed.si_status
                                                          : 128 + observed.si_status;
          exited = true;
        } else {
          siginfo_t state{};
          if (observe_wait(state, WSTOPPED | WCONTINUED | WNOHANG) == 0 &&
              state.si_pid != 0 &&
              (state.si_code == CLD_STOPPED || state.si_code == CLD_CONTINUED)) {
            const std::lock_guard lock{mutex};
            job->stopped = state.si_code == CLD_STOPPED;
          }
        }
      }
      if (stopping && Clock::now() - *stopping >= std::chrono::milliseconds{500}) {
        if (!channel_closed) {
          channel_closed = true;
          output_read.close();
          const std::lock_guard lock{mutex};
          job->capture_complete = false;
          job->output_closed = true;
        }
      }
      if (exited && channel_closed && boot_read.value < 0)
        break;
      pollfd descriptors[2]{{available >= 8192 ? output_read.value : -1, POLLIN, 0},
                            {boot_read.value, POLLIN, 0}};
      auto poll_credit = raw_command_claim(108);
      const int timeout = 10;
      const auto request =
          raw_command_linked(2046, job->launch_ref,
                             std::array{raw_native_field(descriptors[0].fd),
                                        raw_native_field(descriptors[0].events),
                                        raw_native_field(descriptors[1].fd),
                                        raw_native_field(descriptors[1].events),
                                        raw_native_field(timeout)},
                             poll_credit ? &*poll_credit : nullptr);
      const int result = ::poll(descriptors, 2, timeout), error = errno;
      (void)raw_command_linked(2047, request,
                               std::array{raw_native_field(result),
                                          raw_native_field(error),
                                          raw_native_field(descriptors[0].revents),
                                          raw_native_field(descriptors[1].revents)},
                               poll_credit ? &*poll_credit : nullptr);
    }
  } catch (const Error &error) {
    if (!spawn_written) {
      const int result = -1, native_error = static_cast<int>(error.detail);
      (void)raw_command_linked(
          2003, job->launch_ref,
          std::array{raw_native_field(setup_stage), raw_native_field(result),
                     raw_native_field(native_error), raw_native_field(pid),
                     raw_native_field(group_ready)},
          held);
      spawn_written = true;
    }
    const std::uint8_t operation = 1;
    (void)raw_command_linked(2001, job->launch_ref,
                             std::array{raw_native_field(operation),
                                        raw_native_field(error.code),
                                        raw_native_field(error.detail)},
                             held);
    const std::lock_guard lock{mutex};
    if (error.code == ErrorCode::interrupted) {
      worker_stop(error.detail == ETIMEDOUT ? 2 : 4);
      if (error.detail == ETIMEDOUT)
        job->timed_out = true;
    }
    worker_stop(4);
    job->failure = static_cast<int>(error.detail ? error.detail : EIO);
    job->capture_complete = false;
  } catch (...) {
    const std::lock_guard lock{mutex};
    worker_stop(4);
    job->failure = ENOMEM;
    job->capture_complete = false;
  }
  if (pid > 0 && !reaped) {
    signal_owned(SIGKILL, held);
    const int options = 0;
    const auto request = raw_command_linked(
        2032, job->launch_ref,
        std::array{raw_native_field(pid), raw_native_field(options)}, held);
    int status = 0;
    pid_t result;
    do {
      result = ::waitpid(pid, &status, options);
    } while (result < 0 && errno == EINTR);
    const int error = errno;
    const bool valid = result == pid;
    (void)raw_command_linked(
        2033, request,
        std::array{raw_native_field(result), raw_native_field(error),
                   raw_native_field(valid), raw_native_field(status)},
        held);
    reaped = valid;
    const std::lock_guard lock{mutex};
    job->custody = valid                           ? Custody::reaped
                   : result < 0 && error == ECHILD ? Custody::unavailable
                                                   : Custody::pending;
    if (!valid) {
      job->failure = error;
      job->capture_complete = false;
    }
  }
  {
    const std::lock_guard lock{mutex};
    if (job->counted && job->custody != Custody::pending) {
      --detail::owned_children;
      job->counted = false;
    }
  }
  for (auto *fd : {&input_read, &input_write, &output_read, &output_write, &boot_read,
                   &boot_write})
    fd->close();
  const std::lock_guard lock{mutex};
  const auto id_size = job->id.size();
  job->terminal_ref = raw_command_linked(
      2034, job->launch_ref,
      std::array{
          raw_native_field(id_size), raw_string_field(job->id), raw_native_field(pid),
          raw_native_field(job->received), raw_native_field(job->failure),
          raw_native_field(job->capture_complete), raw_native_field(job->timed_out),
          raw_native_field(job->stop), raw_native_field(job->leader_exited),
          raw_native_field(job->output_closed), raw_native_field(job->custody)},
      held);
  job->pid = pid;
  for (auto &[id, delivery] : job->deliveries) {
    (void)id;
    if (!delivery.done) {
      delivery.failed = true;
      delivery.done = true;
      delivery.consumption = CommandDeliveryConsumption::abandoned;
    }
  }
  job->finished = true;
  job->caller_stop_credit.reset();
  condition.notify_all();
}
CommandJobs::CommandJobs() : impl_(std::make_unique<Impl>()) {}
CommandJobs::~CommandJobs() {
  shutdown();
  for (const auto &[id, job] : impl_->jobs) {
    (void)id;
    if (job->custody == Impl::Custody::pending) {
      RawThreadBinding binding{job->raw_context};
      raw_fence({ErrorCode::audit_unavailable, job->failure});
    }
  }
}
void CommandJobs::start(const std::string &id, const Value &arguments,
                        RawRetainedExec *admission, CommandStartStatus *status) {
  if (status)
    *status = {};
  const auto id_size = id.size();
  const std::uint8_t operation = 1, owner_present = 0;
  auto admission_credit = raw_command_claim(66);
  const auto request = raw_command_data(
      2000, std::array{raw_native_field(id_size), raw_string_field(id),
                       raw_native_field(operation), raw_native_field(owner_present)});
  // Surplus preparation must retire after any product lock has unwound.
  RawThreadHandle cleanup_candidate;
  struct RetiredJob {
    std::shared_ptr<Impl::Job> job;
    ~RetiredJob() {
      // finished is published just before the worker returns. Join explicitly
      // before releasing this owner, so the worker cannot destroy its own Job.
      if (job && job->worker.joinable())
        job->worker.join();
      if (job && job->retained_loan)
        job->retained_loan->joined();
    }
  } retired;
  try {
    auto launch = parse_launch(arguments);
    if (id.size() != 32 ||
        id.find_first_not_of("0123456789abcdef") != std::string::npos)
      throw Error{ErrorCode::invalid_range};
    auto job = std::make_shared<Impl::Job>();
    job->id = id;
    job->launch = std::move(launch);
    job->deadline = Clock::now() + job->launch.lifetime;
    job->request = request;
    bool prepare_cleanup = false;
    {
      const std::lock_guard lock{impl_->mutex};
      prepare_cleanup = !impl_->cleanup_context && raw_enabled();
    }
    if (prepare_cleanup)
      cleanup_candidate = unwrap(raw_prepare_thread());
    job->raw_context = unwrap(raw_prepare_thread());
    if (admission)
      job->retained_loan = unwrap(admission->prepare_job(job->raw_context));
    {
      RawThreadBinding binding{job->raw_context};
      job->stop_wake.emplace(RawCommandWake::Mode::one_shot);
      if (job->raw_context) {
        auto bank = raw_command_claim(raw_command_initial_credit);
        if (!bank)
          throw Error{ErrorCode::audit_unavailable};
        job->worker_credit.emplace(unwrap(bank->split(raw_command_worker_credit)));
        job->retry_credit.emplace(unwrap(bank->split(raw_command_retry_credit)));
        job->semantic_credit.emplace(unwrap(bank->split(raw_command_semantic_credit)));
      }
    }
    sigset_t previous, blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGWINCH);
    const int mask_error = ::pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (mask_error)
      throw Error{ErrorCode::io, mask_error};
    struct RestoreMask {
      sigset_t previous;
      ~RestoreMask() { (void)::pthread_sigmask(SIG_SETMASK, &previous, nullptr); }
    } restore{previous};
    const std::lock_guard lock{impl_->mutex};
    if (impl_->jobs.contains(id))
      throw Error{ErrorCode::conflict};
    const auto live = impl_->debt_locked();
    if (status) {
      status->live = live;
      status->resident = impl_->jobs.size();
      status->max_live = impl_->max_live;
      status->max_records = impl_->max_records;
    }
    if (live >= impl_->max_live) {
      if (status)
        status->refusal = CommandStartStatus::Refusal::live_limit;
      throw Error{ErrorCode::capacity};
    }
    if (impl_->admission_counter == std::numeric_limits<std::uint64_t>::max())
      throw Error{ErrorCode::capacity};
    if (impl_->jobs.size() >= impl_->max_records) {
      auto oldest = impl_->jobs.end();
      for (auto candidate = impl_->jobs.begin(); candidate != impl_->jobs.end();
           ++candidate)
        if (impl_->retirable(*candidate->second) &&
            (oldest == impl_->jobs.end() ||
             candidate->second->admission_order < oldest->second->admission_order))
          oldest = candidate;
      if (oldest == impl_->jobs.end()) {
        if (status)
          status->refusal = CommandStartStatus::Refusal::resident_limit;
        throw Error{ErrorCode::capacity};
      }
      const auto length = oldest->first.size();
      auto retirement_credit = raw_command_claim(75 + length);
      const std::uint8_t retirement_operation = 4, branch = 1;
      const Error error{};
      (void)raw_command_linked(
          2045, request,
          std::array{raw_native_field(length), raw_string_field(oldest->first),
                     raw_native_field(retirement_operation), raw_native_field(branch),
                     raw_native_field(error.code), raw_native_field(error.detail),
                     raw_native_field(impl_->max_live),
                     raw_native_field(impl_->max_records)},
          retirement_credit ? &*retirement_credit : nullptr);
      retired.job = oldest->second;
      impl_->jobs.erase(oldest);
    }
    job->admission_order = ++impl_->admission_counter;
    struct PendingOwnerCredit {
      Impl::Job &job;
      bool committed = false;
      ~PendingOwnerCredit() {
        if (!committed)
          job.caller_stop_credit.reset();
      }
    } pending_owner{*job};
    if (!impl_->cleanup_context && cleanup_candidate) {
      RawThreadBinding owner{cleanup_candidate};
      auto credit = unwrap(raw_try_claim_control(75));
      impl_->cleanup_context = std::move(cleanup_candidate);
      impl_->shutdown_credit.emplace(std::move(credit));
    }
    if (impl_->cleanup_context) {
      RawThreadBinding owner{impl_->cleanup_context};
      job->caller_stop_credit.emplace(
          unwrap(raw_try_claim_control(raw_command_caller_stop_credit)));
    }
    impl_->jobs.emplace(id, job);
    try {
      if (job->retained_loan)
        job->retained_loan->started();
      job->stop_wake->publish();
      job->worker = std::jthread([state = impl_.get(), job] { state->run(job); });
      if (status)
        status->worker_started = true;
    } catch (...) {
      if (job->retained_loan)
        job->retained_loan->never_started();
      job->stop_wake->retire();
      impl_->jobs.erase(id);
      throw;
    }
    pending_owner.committed = true;
    const std::uint8_t branch = 1;
    const Error error{};
    (void)raw_command_linked(
        2001, job->request,
        std::array{raw_native_field(branch), raw_native_field(error.code),
                   raw_native_field(error.detail), raw_native_field(impl_->max_live),
                   raw_native_field(impl_->max_records)},
        admission_credit ? &*admission_credit : nullptr);
  } catch (const Error &error) {
    const std::lock_guard refusal_lock{impl_->mutex};
    const std::uint8_t branch = 2;
    (void)raw_command_linked(
        2001, request,
        std::array{raw_native_field(branch), raw_native_field(error.code),
                   raw_native_field(error.detail), raw_native_field(impl_->max_live),
                   raw_native_field(impl_->max_records)},
        admission_credit ? &*admission_credit : nullptr);
    throw;
  } catch (...) {
    const std::lock_guard refusal_lock{impl_->mutex};
    const std::uint8_t branch = 3;
    const Error error{ErrorCode::allocation};
    (void)raw_command_linked(
        2001, request,
        std::array{raw_native_field(branch), raw_native_field(error.code),
                   raw_native_field(error.detail), raw_native_field(impl_->max_live),
                   raw_native_field(impl_->max_records)},
        admission_credit ? &*admission_credit : nullptr);
    throw;
  }
}
Value CommandJobs::wait(const std::string &id, JobWaitOptions options,
                        const std::function<void()> &tick,
                        const std::function<bool()> &cancelled) {
  std::optional<RawControlToken> wait_credit;
  RawEventRef request{};
  std::uint64_t epoch = 0;
  bool request_admitted = false, finished = false, stopped_joined = false;
  auto stop_and_join = [&] {
    if (stopped_joined)
      return;
    std::shared_ptr<Impl::Job> stopped;
    {
      const std::lock_guard lock{impl_->mutex};
      const auto found = impl_->jobs.find(id);
      if (found == impl_->jobs.end())
        return;
      stopped = found->second;
      // A published job belongs to this selected wait even if its first raw
      // admission or tick is interrupted. Stop uses the already held credit.
      impl_->caller_stop(*stopped, 5);
    }
    if (stopped->worker.joinable())
      stopped->worker.join();
    if (stopped->retained_loan)
      stopped->retained_loan->joined();
    stopped_joined = true;
  };
  auto finish = [&](const Impl::Job *job, std::uint8_t branch, Error error = {}) {
    if (finished)
      return;
    finished = true;
    const bool terminal_retained = job && job->terminal_retained;
    const bool leader_exited = job && job->leader_exited;
    const bool output_closed = job && job->output_closed;
    (void)raw_command_linked(
        2026, request,
        std::array{raw_native_field(epoch), raw_native_field(branch),
                   raw_native_field(error.code), raw_native_field(error.detail),
                   raw_native_field(terminal_retained), raw_native_field(leader_exited),
                   raw_native_field(output_closed)},
        wait_credit ? &*wait_credit : nullptr);
  };
  auto failure = [&](std::uint8_t branch, Error error) {
    if (!request_admitted)
      return;
    const std::lock_guard lock{impl_->mutex};
    const auto found = impl_->jobs.find(id);
    finish(found == impl_->jobs.end() ? nullptr : found->second.get(), branch, error);
  };
  try {
    wait_credit = raw_command_claim(110);
    const auto id_size = id.size();
    const bool yield_present = options.yield_ms.has_value();
    const auto yield_ms = options.yield_ms.value_or(0);
    std::array<ByteView, 6> fields{raw_native_field(id_size),
                                   raw_string_field(id),
                                   raw_native_field(yield_present),
                                   raw_native_field(yield_ms),
                                   raw_native_field(options.max_output),
                                   raw_native_field(options.stop_on_cancel)};
    if (!yield_present) {
      fields[3] = fields[4];
      fields[4] = fields[5];
    }
    request = raw_command_data(2024, std::span{fields}.first(yield_present ? 6 : 5));
    request_admitted = true;
    {
      const std::lock_guard lock{impl_->mutex};
      auto &job = impl_->find(id);
      if (!impl_->waiting.empty())
        throw Error{ErrorCode::busy};
      impl_->waiting = id;
      epoch = ++impl_->epoch;
      impl_->text_input = job.launch.pty;
      (void)raw_command_linked(
          2025, request,
          std::array{raw_native_field(epoch), raw_native_field(job.launch.pty)},
          wait_credit ? &*wait_credit : nullptr);
    }
    struct Release {
      Impl &state;
      ~Release() {
        const std::lock_guard lock{state.mutex};
        state.waiting.clear();
        state.text_input = false;
      }
    } release{*impl_};
    const auto deadline =
        options.yield_ms ? Clock::now() + std::chrono::milliseconds{*options.yield_ms}
                         : Clock::time_point::max();
    while (true) {
      tick();
      if (cancelled()) {
        if (options.stop_on_cancel) {
          stop_and_join();
          // Cleanup progresses independently of capture backpressure; now retain
          // that command's actual terminal before propagating turn cancellation.
          for (unsigned pass = 0; pass < 4; ++pass)
            tick();
        }
        throw Error{ErrorCode::interrupted};
      }
      std::unique_lock lock{impl_->mutex};
      auto &job = impl_->find(id);
      if (job.terminal_retained || job.release_epoch == epoch ||
          Clock::now() >= deadline) {
        auto result = impl_->snapshot(job, {options.max_output, 0, true});
        finish(&job, job.terminal_retained ? 1 : job.release_epoch == epoch ? 2 : 3);
        return result;
      }
      impl_->condition.wait_for(lock, std::chrono::milliseconds{10});
    }
  } catch (const Error &error) {
    if (options.stop_on_cancel && error.code == ErrorCode::interrupted)
      stop_and_join();
    failure(error.code == ErrorCode::interrupted ? 4 : 5, error);
    throw;
  } catch (const std::bad_alloc &) {
    failure(6, {ErrorCode::allocation});
    throw;
  } catch (...) {
    failure(6, {ErrorCode::external_unknown});
    throw;
  }
}

CommandDeliveryObservation
CommandJobs::try_observe_delivery(const CommandDeliveryQuery &query) const noexcept {
  CommandDeliveryObservation out;
  out.incarnation = impl_->incarnation;
  if (query.request.empty() || query.request.size() > out.request.size() ||
      !std::all_of(query.job.bytes.begin(), query.job.bytes.end(), [](char byte) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
      })) {
    out.status = CommandDeliveryStatus::invalid_identity;
    return out;
  }
  if (query.incarnation && query.incarnation != out.incarnation) {
    out.status = CommandDeliveryStatus::scope_mismatch;
    return out;
  }
  out.job = query.job;
  out.request_size = query.request.size();
  std::copy(query.request.begin(), query.request.end(), out.request.begin());
  try {
    const std::unique_lock lock{impl_->mutex, std::try_to_lock};
    if (!lock.owns_lock()) {
      out.status = CommandDeliveryStatus::busy;
      return out;
    }
    out.status = CommandDeliveryStatus::ready;
    ++out.lookup_count;
    const auto found = impl_->jobs.find(query.job.view());
    if (found == impl_->jobs.end())
      return out;
    const auto &job = *found->second;
    out.launch = job.launch_ref;
    ++out.lookup_count;
    const auto delivery = job.deliveries.find(query.request);
    if (delivery != job.deliveries.end()) {
      const auto &value = delivery->second;
      out.stage = value.failed ? CommandDeliveryStage::failed
                  : value.done ? CommandDeliveryStage::consumed
                               : CommandDeliveryStage::queued;
      out.operation = value.operation;
      out.signal = value.signal;
      out.consumption = value.consumption;
      out.written = value.written;
      out.done = value.done;
      out.failed = value.failed;
      out.admission = value.admission;
      out.enqueue = value.enqueue;
      out.selection = value.selection;
      out.control = value.control;
      out.calls = value.calls;
      out.call_count = value.call_count;
      return out;
    }
    ++out.lookup_count;
    const auto promised = job.reservations.find(query.request);
    if (promised == job.reservations.end()) {
      out.stage = CommandDeliveryStage::key_not_resident;
      return out;
    }
    const auto &value = promised->second;
    out.stage = value.selected ? CommandDeliveryStage::selected
                               : CommandDeliveryStage::reserved;
    out.operation = value.operation;
    out.signal = value.signal;
    out.admission = value.admission;
    out.enqueue = value.enqueue;
    out.selection = value.selection;
  } catch (...) {
    out.status = CommandDeliveryStatus::unavailable;
  }
  return out;
}

CommandObservation
CommandJobs::try_observe(const CommandObservationQuery &query) const noexcept {
  CommandObservation out;
  const auto valid = [](const std::optional<CommandObservationId> &id) noexcept {
    return !id || std::all_of(id->bytes.begin(), id->bytes.end(), [](char byte) {
      return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    });
  };
  if (!valid(query.after) || !valid(query.selected)) {
    out.status = CommandObservationStatus::invalid_identity;
    return out;
  }
  if (query.row_limit > out.rows.size() || query.byte_limit > out.window.bytes.size() ||
      query.chunk_limit > out.window.sources.size()) {
    out.status = CommandObservationStatus::invalid_range;
    return out;
  }
  try {
    const std::unique_lock lock{impl_->mutex, std::try_to_lock};
    if (!lock.owns_lock()) {
      out.status = CommandObservationStatus::busy;
      return out;
    }
    const auto project = [&](const Impl::Job &job) noexcept {
      CommandObservationRow row;
      std::copy_n(job.id.begin(), row.id.bytes.size(), row.id.bytes.begin());
      row.admission_order = job.admission_order;
      row.phase = job.terminal_retained ? CommandObservationPhase::settled
                  : job.finished        ? CommandObservationPhase::settling
                  : job.leader_exited   ? CommandObservationPhase::draining
                  : job.stopped         ? CommandObservationPhase::stopped
                  : job.ready           ? CommandObservationPhase::running
                                        : CommandObservationPhase::starting;
      switch (job.custody) {
      case Impl::Custody::not_launched:
        row.custody = CommandObservationCustody::not_launched;
        break;
      case Impl::Custody::owned:
        row.custody = CommandObservationCustody::owned;
        break;
      case Impl::Custody::reaped:
        row.custody = CommandObservationCustody::reaped;
        break;
      case Impl::Custody::unavailable:
        row.custody = CommandObservationCustody::unavailable;
        break;
      case Impl::Custody::pending:
        row.custody = CommandObservationCustody::pending;
        break;
      }
      row.received = job.received;
      row.retained = job.retained;
      row.pty = job.launch.pty;
      row.leader_exited = job.leader_exited;
      row.output_closed = job.output_closed;
      row.worker_finished = job.finished;
      row.terminal_retained = job.terminal_retained;
      row.capture_complete = job.capture_complete;
      row.stop_requested = job.stop;
      row.timed_out = job.timed_out;
      row.has_exit = job.leader_exited;
      row.sealed = job.terminal_retained && job.capture_complete &&
                   job.custody == Impl::Custody::reaped;
      row.exit_code = job.exit_code;
      row.signal = job.signal;
      row.failure = job.failure;
      row.launch = job.launch_ref;
      row.terminal = job.terminal_ref;
      ++out.examined_rows;
      return row;
    };
    auto entry = query.after ? impl_->jobs.upper_bound(query.after->view())
                             : impl_->jobs.begin();
    while (entry != impl_->jobs.end() && out.row_count < query.row_limit) {
      out.rows[out.row_count++] = project(*entry->second);
      ++entry;
    }
    out.more_rows = entry != impl_->jobs.end();
    if (out.more_rows && out.row_count)
      out.next = out.rows[out.row_count - 1].id;
    out.status = CommandObservationStatus::ready;
    if (!query.selected)
      return out;
    const auto selected = impl_->jobs.find(query.selected->view());
    if (selected == impl_->jobs.end()) {
      out.selection = CommandObservationSelection::not_resident;
      return out;
    }
    out.selection = CommandObservationSelection::resident;
    const auto &job = *selected->second;
    out.detail = project(job);
    auto &window = out.window;
    window.requested = query.offset;
    window.cached_begin = job.tail.empty() ? job.retained : job.tail.front().offset;
    window.retained_end = job.retained;
    window.received_end = job.received;
    if (query.offset > job.retained) {
      out.status = CommandObservationStatus::invalid_range;
      return out;
    }
    window.begin = std::min(job.retained, std::max(query.offset, window.cached_begin));
    window.next = window.begin;
    window.omitted_prefix = window.begin > query.offset;
    auto chunk = std::lower_bound(
        job.tail.begin(), job.tail.end(), window.begin,
        [&](const Impl::Chunk &candidate, std::size_t offset) noexcept {
          ++out.seek_steps;
          return candidate.offset + candidate.bytes->size() <= offset;
        });
    while (window.next < job.retained && chunk != job.tail.end()) {
      if (window.size == query.byte_limit) {
        window.end = CommandObservationPageEnd::byte_limit;
        break;
      }
      if (window.source_count == query.chunk_limit) {
        window.end = CommandObservationPageEnd::chunk_limit;
        break;
      }
      ++out.examined_chunks;
      const auto at = window.next - chunk->offset;
      const auto length =
          std::min({chunk->bytes->size() - at, query.byte_limit - window.size,
                    job.retained - window.next});
      std::memcpy(window.bytes.data() + window.size, chunk->bytes->data() + at, length);
      window.sources[window.source_count++] = {chunk->original, chunk->offset, at,
                                               length};
      if (chunk->original.ordinal == 0)
        window.provenance_complete = false;
      window.size += length;
      window.next += length;
      ++chunk;
    }
    return out;
  } catch (...) {
    // A failed native mutex acquisition has no useful projection or side effect.
    out.status = CommandObservationStatus::unavailable;
    return out;
  }
}
Value CommandJobs::read(const Value &arguments) const {
  auto credit = raw_command_claim(58);
  const auto id =
      arguments.find("job_id") ? string_field(arguments, "job_id") : std::string{};
  const auto requested = integer(arguments, "count", 65536),
             offset = integer(arguments, "offset", 0);
  const auto id_size = id.size();
  const std::uint8_t mode = id.empty() ? 0 : 1;
  const auto request = raw_command_data(
      2037, std::array{raw_native_field(id_size), raw_string_field(id),
                       raw_native_field(mode), raw_native_field(offset),
                       raw_native_field(requested)});
  std::size_t retained_size = 0;
  auto finish = [&](std::uint8_t branch, Error error = {}) {
    (void)raw_command_linked(
        2039, request,
        std::array{raw_native_field(branch), raw_native_field(error.code),
                   raw_native_field(error.detail), raw_native_field(retained_size)},
        credit ? &*credit : nullptr);
  };
  try {
    if (id.empty()) {
      const std::lock_guard lock{impl_->mutex};
      Value::Array rows;
      for (const auto &[key, job] : impl_->jobs) {
        (void)key;
        rows.push_back(impl_->snapshot(*job, {}));
      }
      auto result = Value::object({{"jobs", Value{std::move(rows)}},
                                   {"live", Value{Number{impl_->debt_locked()}}},
                                   {"max_live", Value{Number{impl_->max_live}}},
                                   {"max_records", Value{Number{impl_->max_records}}}});
      finish(1);
      return result;
    }
    struct Selected {
      std::shared_ptr<const std::string> bytes;
      RawEventRef original;
      std::size_t chunk_offset, at, length;
    };
    std::vector<Selected> selected;
    Value result;
    std::size_t begin = 0;
    {
      const std::lock_guard lock{impl_->mutex};
      const auto &job = impl_->find(id);
      if (requested > 65536 || offset > job.retained)
        throw Error{ErrorCode::invalid_range};
      result = impl_->snapshot(job, {});
      retained_size = job.retained;
      const auto first = job.tail.empty() ? job.retained : job.tail.front().offset;
      begin = std::min(job.retained, std::max(static_cast<std::size_t>(offset), first));
      std::size_t selected_bytes = 0;
      for (const auto &chunk : job.tail) {
        const auto end = std::min(chunk.offset + chunk.bytes->size(), job.retained);
        if (end <= begin || selected_bytes >= requested)
          continue;
        const auto at = std::max(chunk.offset, begin);
        const auto length =
            std::min(end - at, static_cast<std::size_t>(requested) - selected_bytes);
        selected.push_back({chunk.bytes, chunk.original, chunk.offset, at, length});
        selected_bytes += length;
      }
    }
    // The same shared originals are read outside the product lock. No history copy.
    std::string bytes;
    for (const auto &source : selected) {
      (void)raw_command_linked_data(
          2038, request,
          std::array{ByteView{source.original.lane},
                     raw_native_field(source.original.ordinal),
                     raw_native_field(source.at), raw_native_field(source.length)});
      bytes.append(*source.bytes, source.at - source.chunk_offset, source.length);
    }
    const auto shown = process_output_presentation(bytes, Value::object({}));
    put(result, "output", field(shown, "output"));
    put(result, "returned_bytes", Value{Number{bytes.size()}});
    put(result, "omitted_bytes", Value{Number{retained_size - bytes.size()}});
    put(result, "byte_start", Value{Number{begin}});
    put(result, "next_offset", Value{Number{begin + bytes.size()}});
    put(result, "omitted_prefix_bytes",
        Value{Number{begin - std::min(static_cast<std::size_t>(offset), begin)}});
    finish(1);
    return result;
  } catch (const Error &error) {
    finish(2, error);
    throw;
  } catch (...) {
    finish(3, {ErrorCode::allocation});
    throw;
  }
}
Value CommandJobs::control(const Value &arguments) {
  ControlTrace trace{arguments};
  auto owner_text = [&](std::string_view key) -> std::string_view {
    const auto *value = arguments.find(key);
    return value && std::holds_alternative<std::string>(value->value())
               ? std::string_view{value->string()}
               : std::string_view{};
  };
  const auto operation = owner_text("op"), owner_job = owner_text("job_id");
  std::optional<RawControlToken> ownership_credit;
  if (operation == "configure" || operation == "archive")
    ownership_credit = raw_command_claim(75 + owner_job.size());
  const std::uint8_t owner_operation = operation == "configure" ? 1 : 2;
  bool ownership_finished = false;
  auto ownership = [&](std::uint8_t branch, Error error = {}) {
    if (!ownership_credit || ownership_finished)
      return;
    ownership_finished = true;
    const auto length = owner_job.size();
    const std::lock_guard owner_lock{impl_->mutex};
    (void)raw_command_linked(
        2045, trace.reference(),
        std::array{raw_native_field(length), raw_string_field(owner_job),
                   raw_native_field(owner_operation), raw_native_field(branch),
                   raw_native_field(error.code), raw_native_field(error.detail),
                   raw_native_field(impl_->max_live),
                   raw_native_field(impl_->max_records)},
        &*ownership_credit);
  };
  try {
    validate_control(arguments, false);
    const auto op = string_field(arguments, "op");
    if (op == "read") {
      auto result = read(arguments);
      trace.finish(1, 0, true, false);
      return result;
    }
    if (op == "stop")
      raw_command_data_credit(57);
    std::optional<RawControlToken> stop_credit;
    if (op == "stop")
      stop_credit = raw_command_claim(raw_wake_single_notify_credit);
    std::optional<RawControlToken> background_credit;
    if (op == "background") {
      background_credit = raw_command_claim(58);
      raw_command_data_credit(64);
    }
    std::unique_lock lock{impl_->mutex};
    if (op == "configure") {
      if (impl_->debt_locked())
        throw Error{ErrorCode::busy};
      const auto live = integer(arguments, "max_live", impl_->max_live);
      const auto records = integer(arguments, "max_records", impl_->max_records);
      if (live < 1 || live > 64 || records < live || records > 256 ||
          records < impl_->jobs.size())
        throw Error{ErrorCode::invalid_range};
      impl_->max_live = static_cast<std::size_t>(live);
      impl_->max_records = static_cast<std::size_t>(records);
      lock.unlock();
      ownership(1);
      auto result = Value::object({{"configured", Value{true}}});
      trace.finish(1, 0, true, false);
      return result;
    }
    const auto id = string_field(arguments, "job_id");
    auto &job = impl_->find(id);
    if (op == "archive") {
      if (!impl_->retirable(job))
        throw Error{ErrorCode::busy};
      auto retained = impl_->jobs.at(id);
      impl_->jobs.erase(id);
      lock.unlock();
      if (retained->worker.joinable())
        retained->worker.join();
      if (retained->retained_loan)
        retained->retained_loan->joined();
      ownership(1);
      auto result = Value::object({{"archived", Value{true}}, {"job_id", Value{id}}});
      trace.finish(1, 0, true, false);
      return result;
    }
    if (op == "background") {
      const auto requested = integer(arguments, "wait_epoch", impl_->epoch);
      const auto id_size = id.size();
      const auto background_request = raw_command_record(
          2027, std::array{raw_native_field(id_size), raw_string_field(id),
                           raw_native_field(requested)});
      const bool released = impl_->waiting == id && requested == impl_->epoch;
      if (released)
        job.release_epoch = requested;
      impl_->condition.notify_all();
      const Error error{};
      (void)raw_command_linked(
          2028, background_request,
          std::array{raw_native_field(impl_->epoch), raw_native_field(released),
                     raw_native_field(error.code), raw_native_field(error.detail)},
          background_credit ? &*background_credit : nullptr);
      auto result =
          Value::object({{"job_id", Value{id}}, {"released", Value{released}}});
      trace.finish(1, 0, released, false);
      return result;
    }
    if (op == "stop") {
      const auto id_size = id.size();
      const std::uint8_t source = 1;
      (void)raw_command_record(2029, std::array{raw_native_field(id_size),
                                                raw_string_field(id),
                                                raw_native_field(source)});
      job.stop = true;
      job.caller_stop_credit.reset();
      if (job.stop_wake)
        job.stop_wake->notify(stop_credit ? &*stop_credit : nullptr);
      impl_->condition.notify_all();
      auto result =
          Value::object({{"job_id", Value{id}}, {"stop_requested", Value{true}}});
      trace.finish(1, 0, true, false);
      return result;
    }
    const auto request = string_field(arguments, "request_id");
    if (request.empty() || request.size() > 128)
      throw Error{ErrorCode::invalid_range};
    const auto promised = job.reservations.find(request);
    if (promised != job.reservations.end() &&
        !same_request(*promised->second.request, arguments))
      throw Error{ErrorCode::conflict};
    // A selected promise remains charged until dispatch either succeeds or
    // explicitly refuses. A conflicting caller cannot withdraw its owner.
    struct Release {
      Impl::Job &job;
      const std::string &request;
      bool promised;
      ~Release() {
        if (promised)
          Impl::release_reservation(job, request);
      }
    } release{job, request, promised != job.reservations.end()};
    if (const auto existing = job.deliveries.find(request);
        existing != job.deliveries.end()) {
      if (!same_request(existing->second.request, arguments))
        throw Error{ErrorCode::conflict};
      const auto &delivery = existing->second;
      auto result =
          Value::object({{"job_id", Value{id}},
                         {"request_id", Value{request}},
                         {"written_bytes", Value{Number{delivery.written}}},
                         {"status", Value{delivery.failed ? "partial_or_unknown"
                                          : delivery.done ? "delivered"
                                                          : "queued"}},
                         {"duplicate", Value{true}}});
      trace.finish(2, delivery.written, delivery.done, delivery.failed);
      return result;
    }
    if (job.finished)
      throw Error{ErrorCode::busy};
    Impl::Delivery delivery{arguments};
    delivery.operation = delivery_operation(op);
    delivery.signal = op == "signal"
                          ? delivery_signal(string_field(arguments, "signal"))
                          : CommandDeliverySignal::absent;
    delivery.control = trace.reference();
    if (release.promised) {
      delivery.admission = promised->second.admission;
      delivery.enqueue = promised->second.enqueue;
      delivery.selection = promised->second.selection;
    }
    const auto bytes = op == "input" ? delivery.bytes().size() : 0;
    switch (
        Impl::admission(job, bytes, release.promised ? &promised->second : nullptr)) {
    case Impl::Admission::ready:
      break;
    case Impl::Admission::wait_pending_input:
    case Impl::Admission::refuse_history:
      throw Error{ErrorCode::capacity};
    }
    if (op == "input") {
      if (!job.launch.pty)
        throw Error{ErrorCode::unsupported};
      delivery.done = delivery.bytes().empty();
      if (delivery.done)
        delivery.consumption = CommandDeliveryConsumption::immediate;
    } else if (op == "resize") {
      const auto rows = integer(arguments, "rows", 24);
      const auto columns = integer(arguments, "columns", 80);
      if (!job.launch.pty || rows < 1 || columns < 1 || rows > 512 || columns > 512)
        throw Error{ErrorCode::invalid_range};

    } else if (op == "signal") {
      const auto name = string_field(arguments, "signal");
      const int number = name == "stop"        ? SIGSTOP
                         : name == "continue"  ? SIGCONT
                         : name == "interrupt" ? SIGINT
                         : name == "terminate" ? SIGTERM
                         : name == "kill"      ? SIGKILL
                                               : 0;
      if (!number || job.pid <= 0 || job.leader_exited)
        throw Error{ErrorCode::busy};

    } else {
      throw Error{ErrorCode::unsupported};
    }
    const bool done = delivery.done;
    auto [inserted, fresh] = job.deliveries.emplace(request, std::move(delivery));
    (void)fresh;
    try {
      if (!done) {
        if (op == "signal")
          job.signals.push_back(request);
        else
          job.input.push_back(request);
      }
    } catch (...) {
      job.deliveries.erase(inserted);
      throw;
    }
    if (op == "input") {
      job.input_bytes += inserted->second.bytes().size();
      job.input_history_bytes += inserted->second.bytes().size();
    }
    impl_->condition.notify_all();
    auto result = Value::object({{"job_id", Value{id}},
                                 {"request_id", Value{request}},
                                 {"status", Value{done ? "delivered" : "queued"}},
                                 {"written_bytes", Value{Number{0}}},
                                 {"duplicate", Value{false}}});
    trace.finish(1, 0, done, false);
    return result;
  } catch (const Error &error) {
    ownership(2, error);
    trace.finish(3, 0, false, true, error);
    throw;
  } catch (const std::bad_alloc &) {
    ownership(3, {ErrorCode::allocation});
    trace.finish(4, 0, false, true, {ErrorCode::allocation});
    throw;
  } catch (...) {
    ownership(3, {ErrorCode::external_unknown});
    trace.finish(4, 0, false, true, {ErrorCode::external_unknown});
    throw;
  }
}
void CommandJobs::request(Value arguments) {
  ControlTrace trace{arguments};
  try {
    validate_control(arguments, true);
    raw_command_data_credit(145);
    const std::lock_guard lock{impl_->mutex};
    const auto op = string_field(arguments, "op");
    const bool urgent = op == "background" || op == "stop";
    if (impl_->mailbox.size() >= (urgent ? 64U : 60U))
      throw Error{ErrorCode::capacity};
    auto id =
        arguments.find("job_id") ? string_field(arguments, "job_id") : std::string{};
    if (id.empty()) {
      id = impl_->waiting;
      if (id.empty())
        throw Error{ErrorCode::busy};
      put(arguments, "job_id", Value{id});
    }
    auto &job = impl_->find(id);
    if (string_field(arguments, "op") == "input" &&
        (!job.launch.pty || string_field(arguments, "bytes").size() > input_limit))
      throw Error{ErrorCode::invalid_range};
    if (!arguments.find("request_id"))
      put(arguments, "request_id",
          Value{id + ":ui:" + std::to_string(++impl_->request_counter)});
    if (string_field(arguments, "op") == "background" && !arguments.find("wait_epoch"))
      put(arguments, "wait_epoch", Value{Number{impl_->epoch}});
    const bool control = op == "input" || op == "resize" || op == "signal";
    const auto bytes = op == "input" ? string_field(arguments, "bytes").size() : 0;
    std::shared_ptr<const Value> owned;
    bool reserve_new = false;
    if (control) {
      const auto &delivery = string_field(arguments, "request_id");
      if (delivery.empty())
        throw Error{ErrorCode::invalid_range};
      const auto existing = job.deliveries.find(delivery);
      const auto promised = job.reservations.find(delivery);
      if (existing != job.deliveries.end()) {
        if (!same_request(existing->second.request, arguments))
          throw Error{ErrorCode::conflict};
      } else if (promised != job.reservations.end()) {
        if (!same_request(*promised->second.request, arguments))
          throw Error{ErrorCode::conflict};
        owned = promised->second.request;
      } else {
        if (job.finished)
          throw Error{ErrorCode::busy};
        switch (Impl::admission(job, bytes)) {
        case Impl::Admission::ready:
        case Impl::Admission::wait_pending_input:
          break;
        case Impl::Admission::refuse_history:
          throw Error{ErrorCode::capacity};
        }
        reserve_new = true;
      }
    }
    if (!owned)
      owned = std::make_shared<const Value>(std::move(arguments));
    const auto &delivery = string_field(*owned, "request_id"),
               &owned_id = string_field(*owned, "job_id");
    if (reserve_new) {
      Impl::Reservation reservation{owned, bytes, false};
      reservation.operation = delivery_operation(op);
      reservation.signal = op == "signal"
                               ? delivery_signal(string_field(*owned, "signal"))
                               : CommandDeliverySignal::absent;
      reservation.admission = trace.reference();
      job.reservations.emplace(delivery, std::move(reservation));
      job.reserved_bytes += bytes;
    }
    const auto id_size = owned_id.size(), delivery_size = delivery.size();
    const std::uint8_t branch = 1;
    const auto source = raw_command_linked(
        2015, trace.reference(),
        std::array{raw_native_field(id_size), raw_string_field(owned_id),
                   raw_native_field(delivery_size), raw_string_field(delivery),
                   raw_native_field(branch)});
    try {
      impl_->mailbox.push_back({std::move(owned), source});
    } catch (...) {
      if (reserve_new)
        Impl::release_reservation(job, delivery);
      throw;
    }
    if (reserve_new)
      job.reservations.find(delivery)->second.enqueue = source;
    impl_->condition.notify_all();
    trace.finish(1, 0, false, false);
  } catch (const Error &error) {
    trace.finish(3, 0, false, true, error);
    throw;
  } catch (...) {
    trace.finish(4, 0, false, true, {ErrorCode::allocation});
    throw;
  }
}
std::vector<Value> CommandJobs::requests() {
  std::size_t mailbox_count;
  {
    const std::lock_guard lock{impl_->mutex};
    mailbox_count = impl_->mailbox.size();
  }
  raw_command_data_credit(mailbox_count * 145);
  const std::lock_guard lock{impl_->mutex};
  std::vector<Value> out;
  out.reserve(mailbox_count);
  std::vector<std::size_t> selected;
  selected.reserve(mailbox_count);
  std::map<std::string, std::size_t> selected_bytes;
  std::vector<const Impl::Reservation *> charged;
  charged.reserve(mailbox_count);
  std::size_t index = 0;
  for (const auto &entry : impl_->mailbox) {
    if (index >= mailbox_count)
      break;
    const auto current = index++;
    const auto id = string_field(*entry.value, "job_id");
    auto &job = impl_->find(id);
    const auto op = string_field(*entry.value, "op");
    const bool control = op == "input" || op == "resize" || op == "signal";
    const auto promised =
        control ? job.reservations.find(string_field(*entry.value, "request_id"))
                : job.reservations.end();
    if (promised != job.reservations.end() && !promised->second.selected &&
        std::find(charged.begin(), charged.end(), &promised->second) == charged.end()) {
      const auto bytes = promised->second.bytes;
      if (!job.finished) {
        const auto admission =
            Impl::admission(job, bytes, &promised->second, selected_bytes[id]);
        switch (admission) {
        case Impl::Admission::wait_pending_input:
          continue;
        case Impl::Admission::ready:
        case Impl::Admission::refuse_history:
          // A permanent refusal must reach control and produce explicit
          // feedback; no history state is allowed to strand a queue entry.
          break;
        }
      }
      selected_bytes[id] += bytes;
      charged.push_back(&promised->second);
    }
    out.push_back(*entry.value);
    selected.push_back(current);
  }
  // All allocating result construction finishes before accepted entries move.
  // Promises remain live after selection, until explicit control dispatch.
  index = 0;
  std::size_t selection = 0;
  for (auto entry = impl_->mailbox.begin();
       entry != impl_->mailbox.end() && selection < selected.size();) {
    if (index++ != selected[selection]) {
      ++entry;
      continue;
    }
    ++selection;
    const auto &id = string_field(*entry->value, "job_id");
    auto &job = impl_->find(id);
    const auto &delivery = string_field(*entry->value, "request_id");
    const auto op = string_field(*entry->value, "op");
    const bool control = op == "input" || op == "resize" || op == "signal";
    const auto promised =
        control ? job.reservations.find(delivery) : job.reservations.end();
    const bool first_selection =
        promised != job.reservations.end() && !promised->second.selected;
    if (first_selection) {
      promised->second.selected = true;
      job.selected_bytes += promised->second.bytes;
    }
    const auto id_size = id.size(), delivery_size = delivery.size();
    const std::uint8_t branch = 2;
    const auto source = raw_command_linked(
        2015, entry->source,
        std::array{raw_native_field(id_size), raw_string_field(id),
                   raw_native_field(delivery_size), raw_string_field(delivery),
                   raw_native_field(branch)});
    if (first_selection)
      promised->second.selection = source;
    entry = impl_->mailbox.erase(entry);
  }
  return out;
}
std::vector<JobEvent> CommandJobs::drain() {
  std::map<std::string, std::size_t> selected;
  std::size_t selected_count = 0;
  {
    const std::lock_guard lock{impl_->mutex};
    for (const auto &[id, job] : impl_->jobs) {
      selected[id] = std::min<std::size_t>(64, job->pending.size());
      selected_count += selected[id];
    }
  }
  raw_command_data_credit(selected_count * 96);
  const std::lock_guard lock{impl_->mutex};
  std::vector<JobEvent> out;
  for (const auto &[id, job] : impl_->jobs) {
    std::size_t count = 0;
    for (const auto &entry : job->pending) {
      if (count++ == selected[id])
        break;
      const auto id_size = id.size();
      (void)raw_command_linked(2010, entry.original,
                               std::array{raw_native_field(id_size),
                                          raw_string_field(id),
                                          raw_native_field(entry.offset),
                                          raw_native_field(job->pending_bytes)});
      out.push_back({id, entry.bytes, entry.offset, Value{}, entry.original});
    }
    if (job->finished && (job->pending.empty() || job->capture_refused) &&
        !job->terminal_retained) {
      auto result = Value::object(
          {{"job_id", Value{id}},
           {"output_ref", Value{id}},
           {"termination", Value{job->termination}},
           {"signal", Value{Number{job->signal}}},
           {"output_bytes", Value{Number{job->received}}},
           {"capture_complete", Value{job->capture_complete}},
           {"timed_out", Value{job->timed_out}},
           {"stop_requested", Value{job->stop}},
           {"effect_outcome",
            Value{job->stop || job->failure || !job->capture_complete ? "unknown"
                                                                      : "observed"}}});
      if (job->leader_exited)
        put(result, "exit_code", Value{Number{job->exit_code}});
      if (job->failure)
        put(result, "error", Value{"io"});
      if (job->failure || job->timed_out) {
        put(result, "error", Value{"io"});
        put(result, "error_detail",
            Value{Number{job->timed_out ? ETIMEDOUT : job->failure}});
      }
      if (job->failure == EFBIG) {
        // Reading stopped at the budget; the process group was stopped with it.
        put(result, "error", Value{"output_limit"});
        put(result, "output_limit_exceeded", Value{true});
        put(result, "output_limit_bytes", Value{Number{job->launch.output_budget}});
        put(result, "message",
            Value{"Output exceeded output_limit_bytes; the command was stopped and "
                  "its effects are unknown. All bytes read are retained "
                  "(output_ref). Redirect large output to a file and inspect it, "
                  "or rerun with a larger output_limit_bytes."});
      }
      if (!job->terminal_sent) {
        job->terminal = std::move(result);
        job->terminal_sent = true;
      }
      out.push_back({id, {}, job->received, job->terminal, job->terminal_ref});
    }
  }
  impl_->condition.notify_all();
  return out;
}
void CommandJobs::retained(const std::string &id, std::size_t end) {
  const std::lock_guard lock{impl_->mutex};
  auto &job = impl_->find(id);
  if (end < job.retained || end > job.received)
    throw Error{ErrorCode::conflict};
  std::size_t acknowledged = job.retained;
  for (const auto &chunk : job.pending) {
    if (acknowledged == end)
      break;
    if (chunk.offset != acknowledged || chunk.bytes->size() > end - acknowledged)
      throw Error{ErrorCode::conflict};
    acknowledged += chunk.bytes->size();
  }
  if (acknowledged != end)
    throw Error{ErrorCode::conflict};
  while (!job.pending.empty() && job.pending.front().offset < end) {
    job.pending_bytes -= job.pending.front().bytes->size();
    job.pending.pop_front();
  }
  job.retained = end;
  impl_->condition.notify_all();
}
std::size_t CommandJobs::retained_end(const std::string &id) const {
  const std::lock_guard lock{impl_->mutex};
  return impl_->find(id).retained;
}
void CommandJobs::capture_failed_all() {
  const std::lock_guard lock{impl_->mutex};
  for (auto &[id, job] : impl_->jobs) {
    (void)id;
    if (!job->terminal_retained) {
      job->capture_complete = false;
      job->capture_refused = true;
      impl_->caller_stop(*job, 6);
    }
  }
  impl_->condition.notify_all();
}
void CommandJobs::capture_failed(const std::string &id) {
  const std::lock_guard lock{impl_->mutex};
  auto &job = impl_->find(id);
  job.capture_complete = false;
  job.capture_refused = true;
  impl_->caller_stop(job, 6);
  impl_->condition.notify_all();
}
void CommandJobs::settlement(const JobEvent &event,
                             const std::function<void()> &append) {
  settlement(event, [&](RawRetainedJobLoan *) { append(); });
}
void CommandJobs::settlement(const JobEvent &event,
                             const std::function<void(RawRetainedJobLoan *)> &append) {
  std::shared_ptr<Impl::Job> job;
  {
    const std::lock_guard lock{impl_->mutex};
    job = impl_->jobs.at(event.job_id);
    if (!job->finished || (!job->pending.empty() && !job->capture_refused) ||
        job->terminal_retained)
      throw Error{ErrorCode::busy};
  }
  if (job->worker.joinable())
    job->worker.join();
  if (job->retained_loan) {
    job->retained_loan->joined();
    if (job->retained_loan_used)
      job->retained_loan = unwrap(job->retained_loan->renew());
  }
  // Prepare the final business projection before the authoritative append.
  auto staged_result = event.result;
  RawThreadBinding binding{job->raw_context};
  auto credit = std::move(job->semantic_credit);
  job->semantic_credit.reset();
  if (!credit && raw_enabled())
    credit = raw_command_claim(raw_command_semantic_credit);
  std::optional<RawControlToken> regions;
  if (credit)
    regions.emplace(unwrap(credit->split(raw_command_region_credit)));
  std::optional<RawRegionPrepaidScope> scope;
  if (regions)
    scope.emplace(*regions);
  const auto id_size = event.job_id.size();
  const auto request = raw_command_linked(
      2035, event.original,
      std::array{raw_native_field(id_size), raw_string_field(event.job_id)},
      credit ? &*credit : nullptr);
  std::uint8_t branch = 1;
  Error error{};
  struct Leave {
    RawRetainedJobLoan *loan;
    ~Leave() {
      if (loan)
        loan->leave();
    }
  } leave{job->retained_loan.get()};
  const auto accept = [&]() noexcept {
    const std::lock_guard lock{impl_->mutex};
    job->terminal.swap(staged_result);
    job->terminal_retained = true;
  };
  try {
    if (job->retained_loan) {
      job->retained_loan_used = true;
      job->retained_loan->enter(request);
    }
    append(job->retained_loan.get());
    accept();
  } catch (const Error &failure) {
    if (job->retained_loan && job->retained_loan->accepted())
      accept();
    branch = 2;
    error = failure;
    (void)raw_command_linked(2036, request,
                             std::array{raw_native_field(branch),
                                        raw_native_field(error.code),
                                        raw_native_field(error.detail)},
                             credit ? &*credit : nullptr);
    throw;
  } catch (...) {
    if (job->retained_loan && job->retained_loan->accepted())
      accept();
    branch = 3;
    error = {ErrorCode::allocation};
    (void)raw_command_linked(2036, request,
                             std::array{raw_native_field(branch),
                                        raw_native_field(error.code),
                                        raw_native_field(error.detail)},
                             credit ? &*credit : nullptr);
    throw;
  }
  (void)raw_command_linked(2036, request,
                           std::array{raw_native_field(branch),
                                      raw_native_field(error.code),
                                      raw_native_field(error.detail)},
                           credit ? &*credit : nullptr);
}
void CommandJobs::settled(const std::string &id, const Value &result) {
  const std::lock_guard lock{impl_->mutex};
  auto &job = impl_->find(id);
  if (!job.finished)
    throw Error{ErrorCode::busy};
  job.terminal = result;
  job.terminal_retained = true;
  impl_->condition.notify_all();
}
bool CommandJobs::contains(const std::string &id) const {
  const std::lock_guard lock{impl_->mutex};
  return impl_->jobs.contains(id);
}
bool CommandJobs::active() const { return debt() != 0; }
std::size_t CommandJobs::debt() const {
  const std::lock_guard lock{impl_->mutex};
  return impl_->debt_locked();
}
Value CommandJobs::foreground() const {
  const std::lock_guard lock{impl_->mutex};
  return Value::object({{"job_id", Value{impl_->waiting}},
                        {"wait_epoch", Value{Number{impl_->epoch}}},
                        {"pty", Value{impl_->text_input}}});
}
void CommandJobs::stop_all() noexcept {
  const std::lock_guard lock{impl_->mutex};
  for (auto &[id, job] : impl_->jobs) {
    (void)id;
    if (!job->finished) {
      impl_->caller_stop(*job, 7);
    }
  }
  impl_->condition.notify_all();
}
void CommandJobs::shutdown() noexcept {
  stop_all();
  Error shutdown_error{};
  // Registry mutation belongs to this owner; worker completion is followed by join.
  for (const auto &[id, job] : impl_->jobs) {
    (void)id;
    if (job->worker.joinable())
      job->worker.join();
    if (job->retained_loan)
      job->retained_loan->joined();
    if (job->custody != Impl::Custody::pending)
      continue;
    RawThreadBinding binding{job->raw_context};
    try {
      auto credit = std::move(job->retry_credit);
      job->retry_credit.reset();
      if (!credit && raw_enabled())
        credit = raw_command_claim(raw_command_retry_credit);
      auto *held = credit ? &*credit : nullptr;
      const auto type = P_PID;
      const auto selector = static_cast<id_t>(job->pid);
      const int options = WEXITED | WNOHANG | WNOWAIT;
      siginfo_t observed{};
      const auto probe = raw_command_linked(2022, job->launch_ref,
                                            std::array{raw_native_field(type),
                                                       raw_native_field(selector),
                                                       raw_native_field(options)},
                                            held);
      const int result = ::waitid(type, selector, &observed, options), error = errno;
      (void)raw_command_linked(
          2023, probe,
          std::array{
              raw_native_field(result), raw_native_field(error),
              raw_native_field(observed.si_pid), raw_native_field(observed.si_uid),
              raw_native_field(observed.si_code), raw_native_field(observed.si_status)},
          held);
      if (result == 0) {
        const int number = SIGKILL;
        const std::size_t delivery_size = 0;
        auto native_signal = [&](std::uint8_t method, pid_t target) {
          const auto request = raw_command_linked(
              2020, job->launch_ref,
              std::array{raw_native_field(delivery_size), ByteView{},
                         raw_native_field(target), raw_native_field(number),
                         raw_native_field(method)},
              held);
          const int rc = method == 3 ? static_cast<int>(::getpgid(target))
                                     : ::kill(target, number);
          const int signal_error = errno;
          if (method == 3) {
            const auto group = static_cast<pid_t>(rc);
            (void)raw_command_linked(2021, request,
                                     std::array{raw_native_field(rc),
                                                raw_native_field(signal_error),
                                                raw_native_field(group)},
                                     held);
          } else
            (void)raw_command_linked(
                2021, request,
                std::array{raw_native_field(rc), raw_native_field(signal_error)}, held);
          return rc;
        };
        if (!job->ready || native_signal(1, -job->pid) != 0 ||
            native_signal(3, job->pid) != job->pid)
          (void)native_signal(2, job->pid);
        const int reap_options = 0;
        const auto request = raw_command_linked(
            2032, job->launch_ref,
            std::array{raw_native_field(job->pid), raw_native_field(reap_options)},
            held);
        int status = 0;
        pid_t reaped;
        do {
          reaped = ::waitpid(job->pid, &status, reap_options);
        } while (reaped < 0 && errno == EINTR);
        const int reap_error = errno;
        const bool valid = reaped == job->pid;
        (void)raw_command_linked(
            2033, request,
            std::array{raw_native_field(reaped), raw_native_field(reap_error),
                       raw_native_field(valid), raw_native_field(status)},
            held);
        const std::lock_guard lock{impl_->mutex};
        if (valid)
          job->custody = Impl::Custody::reaped;
        else if (reaped < 0 && reap_error == ECHILD)
          job->custody = Impl::Custody::unavailable;
        else
          job->failure = reap_error;
      } else {
        const std::lock_guard lock{impl_->mutex};
        if (error == ECHILD)
          job->custody = Impl::Custody::unavailable;
        else
          job->failure = error;
      }
      const std::lock_guard lock{impl_->mutex};
      if (job->counted && job->custody != Impl::Custody::pending) {
        --detail::owned_children;
        job->counted = false;
      }
      const auto id_size = job->id.size();
      job->terminal_ref = raw_command_linked(
          2034, job->launch_ref,
          std::array{
              raw_native_field(id_size), raw_string_field(job->id),
              raw_native_field(job->pid), raw_native_field(job->received),
              raw_native_field(job->failure), raw_native_field(job->capture_complete),
              raw_native_field(job->timed_out), raw_native_field(job->stop),
              raw_native_field(job->leader_exited),
              raw_native_field(job->output_closed), raw_native_field(job->custody)},
          held);
      if (job->custody == Impl::Custody::pending && shutdown_error.code == ErrorCode{})
        shutdown_error = {ErrorCode::io, job->failure};
    } catch (const Error &error) {
      if (shutdown_error.code == ErrorCode{})
        shutdown_error = error;
      if (error.code != ErrorCode::interrupted)
        raw_fence(error);
    } catch (...) {
      if (shutdown_error.code == ErrorCode{})
        shutdown_error = {ErrorCode::allocation};
      raw_fence({ErrorCode::allocation});
    }
  }
  const std::lock_guard lock{impl_->mutex};
  if (impl_->cleanup_context && impl_->shutdown_credit) {
    RawThreadBinding owner{impl_->cleanup_context};
    const std::size_t empty = 0;
    const std::uint8_t operation = 3;
    const std::uint8_t branch = shutdown_error.code == ErrorCode{} ? 1 : 2;
    (void)raw_command_linked(
        2045, {},
        std::array{raw_native_field(empty), raw_native_field(operation),
                   raw_native_field(branch), raw_native_field(shutdown_error.code),
                   raw_native_field(shutdown_error.detail),
                   raw_native_field(impl_->max_live),
                   raw_native_field(impl_->max_records)},
        &*impl_->shutdown_credit);
    impl_->shutdown_credit.reset();
  }
}
} // namespace blackbird
