#include "blackbird/participants.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/tools.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <condition_variable>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
namespace blackbird {
namespace {
constexpr std::size_t max_runs = 32, max_messages = 64, max_inbox = 16;
constexpr std::size_t max_events = 1024, max_capture_bytes = 8 * 1024 * 1024;
constexpr std::uint64_t worker_credit = 5279, cleanup_credit = 706;
using detail::raw_native_field;
using detail::raw_string_field;
template <class T> ByteView f(const T &value) noexcept {
  return raw_native_field(value);
}
ByteView text(std::string_view value) noexcept { return raw_string_field(value); }
RawEventRef event(RawParticipantBank &bank, std::uint32_t site, RawEventRef parent,
                  std::initializer_list<ByteView> fields) noexcept {
  return bank.linked(site, parent, {fields.begin(), fields.size()});
}
void set(Value &value, std::string_view key, Value replacement) {
  for (auto &[name, item] : value.object())
    if (name == key) {
      item = std::move(replacement);
      return;
    }
  value.object().emplace_back(key, std::move(replacement));
}
void erase(Value &value, std::string_view key) {
  std::erase_if(value.object(), [&](const auto &entry) { return entry.first == key; });
}
void bounded(std::string_view value, std::size_t limit) {
  if (value.empty() || value.size() > limit ||
      value.find('\0') != std::string_view::npos)
    throw Error{ErrorCode::invalid_range};
}
unsigned integer(const Value &value) {
  const auto value_text = value.number().text();
  unsigned out = 0;
  const auto parsed =
      std::from_chars(value_text.data(), value_text.data() + value_text.size(), out);
  if (parsed.ec != std::errc{} || parsed.ptr != value_text.data() + value_text.size())
    throw Error{ErrorCode::invalid_range};
  return out;
}
Error exception_error() noexcept {
  try {
    throw;
  } catch (const Error &failure) {
    return failure;
  } catch (const std::bad_alloc &) {
    return {ErrorCode::allocation};
  } catch (...) {
    return {ErrorCode::external_unknown};
  }
}
template <class Body>
Value action(RawParticipantAction kind, std::uint64_t bytes,
             RawParticipantOrigin origin, Body &&body) {
  RawParticipantTrace trace{kind, bytes, origin};
  try {
    auto result = body(trace);
    trace.finish(1);
    return result;
  } catch (...) {
    trace.finish(2, exception_error());
    throw;
  }
}
Value projection(const Value &result) {
  if (unwrap(encode_packet_string(result)).size() <= 65536)
    return result;
  return Value::object(
      {{"status", field(result, "status")},
       {"remote_disposition", field(result, "remote_disposition")},
       {"text", result.find("text")
                    ? Value{field(result, "text").string().substr(0, 8192)}
                    : Value{}},
       {"details",
        Value{"Read retained participant result original; projection truncated"}}});
}
void close_native(RawParticipantBank &bank, RawEventRef parent, int fd,
                  std::uint8_t reason) noexcept {
  if (fd < 0)
    return;
  const int saved = errno;
  const auto request = event(bank, 2124, parent, {f(fd), f(reason)});
  const int result = ::close(fd), observed = result < 0 ? errno : 0;
  (void)event(bank, 2125, request, {f(result), f(observed)});
  errno = saved;
}
int control_native(RawParticipantBank &bank, RawEventRef parent, int fd, int command,
                   int flags) {
  const std::uint8_t method = 2;
  const auto request =
      event(bank, 2120, parent, {f(method), f(fd), f(command), f(flags)});
  const int result = ::fcntl(fd, command, flags), observed = result < 0 ? errno : 0;
  const bool descriptors = false;
  (void)event(bank, 2121, request, {f(method), f(result), f(observed), f(descriptors)});
  if (result < 0)
    throw Error{ErrorCode::io, observed};
  return result;
}
} // namespace
struct Participants::Impl {
  struct PreparedDirection {
    Value prepared, delivery;
    std::optional<ProviderOriginalReference> source;
    std::optional<SourceReference> retained;
    RawParticipantOrigin origin;
    RawEventRef offer;
  };
  struct Receipt {
    Value arguments;
    RawEventRef offer;
  };
  struct Capture;
  struct Run {
    // Handles precede all claims: claims die before their producer mappings.
    RawThreadHandle setup_handle, worker_handle;
    RawParticipantBank cleanup, lifetime;
    RawEventRef reference, entry, end;
    Value row;
    std::array<Value, max_inbox + 1> dropped;
    Value failure_state{"unknown"}, cancelled_state{"cancelled"},
        completed_state{"completed"};
    Value failure_result{Value::object(
        {{"status", Value{"failed"}}, {"remote_disposition", Value{"unknown"}}})};
    Value stop_value{true};
    std::atomic_bool stop{false}, endpoint_borrowed{false};
    bool sealed = false, finished = false, cleanup_closed = false,
         directions_closed = false;
    int activation = -1, observer = -1;
    std::array<std::unique_ptr<PreparedDirection>, max_inbox> inbox;
    std::size_t head = 0, queued = 0;
    std::map<std::string, Receipt> receipts;
    std::unique_ptr<Capture> emergency;
    std::jthread worker;
    Run() {
      for (std::size_t i = 0; i < dropped.size(); ++i)
        dropped[i] = Value{Number{i}};
    }
  };
  struct Capture {
    Run *run;
    std::string label;
    Value metadata;
    RawEventRef prepared, handoff;
    std::optional<ProviderOwnedOriginal> original;
    std::unique_ptr<Capture> next;
  };
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::map<std::string, std::unique_ptr<Run>> runs;
  std::unique_ptr<Capture> captures;
  Capture *tail = nullptr;
  std::size_t capture_bytes = 0, capture_count = 0;
  unsigned concurrency = 2;
  bool dirty = false, draining = false, owner_callback_active = false;
  Retain retain;
  Save save;
  Publish publish;
  Impl(Retain r, Save s, Publish p)
      : retain(std::move(r)), save(std::move(s)), publish(std::move(p)) {}
  struct OwnerCallback {
    bool &active;
    explicit OwnerCallback(bool &value) : active(value) { active = true; }
    ~OwnerCallback() { active = false; }
  };
  void entry(RawParticipantAction kind, RawParticipantOrigin origin = {}) const {
    if (!owner_callback_active)
      return;
    RawParticipantTrace trace{kind, 142, origin};
    const Error failure{ErrorCode::busy};
    trace.finish(2, failure);
    throw failure;
  }
  bool cancelled(const std::function<bool()> &callback) {
    if (!callback)
      return false;
    const OwnerCallback scope{owner_callback_active};
    return callback();
  }
  Run &find(const Value &args) {
    const auto found = runs.find(string_field(args, "run_id"));
    if (found == runs.end())
      throw Error{ErrorCode::invalid_range};
    return *found->second;
  }
  Value snapshot() const {
    Value::Array rows;
    for (const auto &[id, run] : runs) {
      (void)id;
      rows.push_back(run->row);
    }
    return Value::object({{"label", Value{"participants-state-v1"}},
                          {"concurrency", Value{Number{concurrency}}},
                          {"max_runs", Value{Number{max_runs}}},
                          {"runs", Value{std::move(rows)}}});
  }
  bool debt_locked() const noexcept {
    if (captures || dirty)
      return true;
    for (const auto &[id, run] : runs) {
      (void)id;
      if (run->emergency)
        return true;
    }
    return false;
  }
  void state(Run &run, RawParticipantBank &bank, std::uint8_t branch) noexcept {
    const bool stopped = run.stop.load();
    (void)event(bank, 2106, run.reference,
                {f(branch), f(run.finished), f(run.sealed), f(stopped), f(run.queued),
                 f(run.receipts.size()), f(dirty)});
  }
  enum class PublicationOwner { worker, owner };
  void notify(const Value &row, RawParticipantBank &bank, RawEventRef parent,
              bool guarded = false,
              PublicationOwner owner = PublicationOwner::worker) noexcept {
    const bool present = static_cast<bool>(publish);
    const auto request = event(bank, 2130, parent, {f(present)});
    std::uint8_t branch = present ? 1 : 2;
    std::optional<Error> failure;
    if (present)
      try {
        std::optional<Value> activity;
        {
          std::unique_lock lock{mutex, std::defer_lock};
          if (guarded)
            lock.lock();
          activity.emplace(
              Value::object({{"attempt", field(row, "run_id")},
                             {"run_id", field(row, "run_id")},
                             {"task_id", field(row, "task_id")},
                             {"phase", field(row, "state")},
                             {"operation", Value{"participant"}},
                             {"cancel_requested", field(row, "cancel_requested")}}));
        }
        if (owner == PublicationOwner::worker)
          publish(*activity);
        else {
          const OwnerCallback scope{owner_callback_active};
          publish(*activity);
        }
      } catch (...) {
        branch = 3;
        failure = exception_error();
      }
    if (failure)
      (void)event(bank, 2131, request,
                  {f(branch), f(failure->code), f(failure->detail)});
    else
      (void)event(bank, 2131, request, {f(branch)});
  }
  // The sole endpoint is taken under the product guard. Native shutdown affects
  // inherited aliases too; close alone cannot wake a stopped pre-exec holder.
  void stop(Run &run, RawParticipantBank &bank, RawEventRef parent,
            std::uint8_t source) noexcept {
    int endpoint = -1;
    {
      std::lock_guard lock{mutex};
      const bool was = run.stop.exchange(true);
      if (!was) {
        set(run.row, "cancel_requested", std::move(run.stop_value));
        dirty = true;
        endpoint = std::exchange(run.activation, -1);
      }
      const bool stopped = run.stop.load();
      const auto stopped_ref = event(bank, 2117, parent,
                                     {f(run.reference.lane), f(run.reference.ordinal),
                                      f(source), f(stopped), f(run.finished)});
      parent = stopped_ref;
    }
    changed.notify_all();
    if (endpoint >= 0) {
      const int saved = errno, how = SHUT_WR;
      const auto request = event(bank, 2122, parent, {f(endpoint), f(how)});
      const int result = ::shutdown(endpoint, how), observed = result < 0 ? errno : 0;
      (void)event(bank, 2123, request, {f(result), f(observed)});
      // Closing this descriptor cannot revoke an inherited alias. A failed
      // shutdown cannot promise cancellation to an already dispatched borrower.
      if (result < 0 && run.endpoint_borrowed.load(std::memory_order_acquire))
        ::_exit(70);
      close_native(bank, run.reference, endpoint, 1);
      errno = saved;
    }
  }
  void stop_all(RawParticipantBank &bank, RawEventRef parent,
                std::uint8_t source) noexcept {
    // Owner-only map iteration. No allocating selection and no wait under guard.
    for (auto &[id, run] : runs) {
      (void)id;
      if (!run->cleanup_closed)
        stop(*run, bank, parent, source);
    }
  }
  void setup(Run &run, RawParticipantBank &bank, RawEventRef parent) {
    const std::lock_guard spawn{detail::process_spawn_mutex};
    const std::uint8_t method = 1;
    const int domain = AF_UNIX, type = SOCK_STREAM, protocol = 0;
    const auto request =
        event(bank, 2120, parent, {f(method), f(domain), f(type), f(protocol)});
    std::array<int, 2> sockets{-1, -1};
    const int result = ::socketpair(domain, type, protocol, sockets.data()),
              observed = result < 0 ? errno : 0;
    const bool valid = result == 0;
    if (valid)
      (void)event(bank, 2121, request,
                  {f(method), f(result), f(observed), f(valid), f(sockets)});
    else
      (void)event(bank, 2121, request, {f(method), f(result), f(observed), f(valid)});
    if (!valid)
      throw Error{ErrorCode::io, observed};
    run.activation = sockets[0];
    run.observer = sockets[1];
    for (int *fd : {&run.activation, &run.observer}) {
      // Always duplicate: one portable CLOEXEC operation, stable >=4 descriptor.
      const int replacement = control_native(bank, parent, *fd, F_DUPFD_CLOEXEC, 4);
      close_native(bank, parent, *fd, 3);
      *fd = replacement;
      const int flags = control_native(bank, parent, *fd, F_GETFL, 0);
      (void)control_native(bank, parent, *fd, F_SETFL, flags | O_NONBLOCK);
    }
  }
  void withdraw(Run &run, RawParticipantBank &bank, std::uint8_t branch) noexcept {
    if (run.directions_closed)
      return;
    run.directions_closed = true;
    const auto count = run.queued;
    while (run.queued != 0) {
      auto direction = std::move(run.inbox[run.head]);
      run.head = (run.head + 1) % max_inbox;
      --run.queued;
      const bool stopped = run.stop.load();
      (void)event(bank, 2116, direction->offer,
                  {f(run.reference.lane), f(run.reference.ordinal), f(branch),
                   f(run.queued), f(stopped)});
    }
    set(run.row, "dropped_directions", std::move(run.dropped[count]));
  }
  void join_native(Run &run, RawParticipantBank &bank, RawEventRef parent) {
    const bool joinable = run.worker.joinable();
    const auto request =
        event(bank, 2127, parent,
              {f(run.reference.lane), f(run.reference.ordinal), f(joinable)});
    std::uint8_t branch = joinable ? 1 : 2;
    try {
      if (joinable) {
        run.worker.join();
        run.endpoint_borrowed.store(false, std::memory_order_release);
      }
    } catch (...) {
      const auto failure = exception_error();
      branch = 3;
      (void)event(bank, 2128, request, {f(branch), f(failure.code), f(failure.detail)});
      throw;
    }
    (void)event(bank, 2128, request, {f(branch)});
  }
  void cleanup_run(Run &run) {
    if (run.cleanup_closed)
      return;
    RawThreadBinding binding{run.setup_handle};
    RawParticipantTrace trace{
        RawParticipantAction::cleanup, std::move(run.cleanup), {{run.reference}, {}}};
    auto &bank = trace.bank();
    std::optional<Error> failure;
    std::uint8_t branch = run.worker.joinable() ? 1 : 2;
    stop(run, bank, trace.reference(), 5);
    try {
      join_native(run, bank, trace.reference());
    } catch (...) {
      failure = exception_error();
      branch = 3;
    }
    // Observer stays live through every borrowed worker wait and native join.
    if (!failure) {
      close_native(bank, run.reference, std::exchange(run.observer, -1), 2);
      close_native(bank, run.reference, std::exchange(run.activation, -1), 1);
      run.cleanup_closed = true;
    }
    bool pending;
    {
      std::lock_guard lock{mutex};
      pending = debt_locked();
    }
    const bool ended = run.end.ordinal != 0;
    if (ended) {
      if (failure)
        (void)event(bank, 2136, trace.reference(),
                    {f(run.reference.lane), f(run.reference.ordinal), f(ended),
                     f(run.end.lane), f(run.end.ordinal), f(pending), f(branch),
                     f(failure->code), f(failure->detail)});
      else
        (void)event(bank, 2136, trace.reference(),
                    {f(run.reference.lane), f(run.reference.ordinal), f(ended),
                     f(run.end.lane), f(run.end.ordinal), f(pending), f(branch)});
    } else {
      if (failure)
        (void)event(bank, 2136, trace.reference(),
                    {f(run.reference.lane), f(run.reference.ordinal), f(ended),
                     f(pending), f(branch), f(failure->code), f(failure->detail)});
      else
        (void)event(bank, 2136, trace.reference(),
                    {f(run.reference.lane), f(run.reference.ordinal), f(ended),
                     f(pending), f(branch)});
    }
    trace.finish(failure ? 2 : 1, failure);
    if (failure)
      throw *failure;
  }
  struct Receiver final : ProviderOriginalCapture {
    Impl &pool;
    Run &run;
    const PreparedDirection &direction;
    RawEventRef transport;
    bool pending = false;
    Receiver(Impl &p, Run &r, const PreparedDirection &d, RawEventRef t)
        : pool(p), run(r), direction(d), transport(t) {}
    struct Ticket final : ProviderOriginalTicket {
      Receiver &receiver;
      std::unique_ptr<Capture> node;
      std::shared_ptr<ProviderOriginalStorage> storage;
      RawParticipantBank bank;
      Ticket(Receiver &r, std::unique_ptr<Capture> n,
             std::shared_ptr<ProviderOriginalStorage> s, RawParticipantBank b)
          : receiver(r), node(std::move(n)), storage(std::move(s)), bank(std::move(b)) {
      }
      ~Ticket() override {
        if (node) {
          const std::uint8_t branch = 4;
          (void)event(bank, 2108, node->prepared, {f(branch)});
          receiver.pending = false;
        }
      }
      std::shared_ptr<ProviderOriginalStorage> take_storage() noexcept override {
        return std::move(storage);
      }
      ProviderCaptureDisposition
      complete(ProviderOwnedOriginal &&original) noexcept override {
        auto &destination = receiver.pool;
        auto &participant = receiver.run;
        node->original.emplace(std::move(original));
        const auto &source = node->original->reference();
        const std::uint8_t handoff_branch = source ? 1 : 2;
        if (source) {
          const auto kind = node->original->kind();
          const auto used = node->original->observed_used();
          const bool present = used.has_value();
          if (used)
            node->handoff = event(bank, 2108, node->prepared,
                                  {f(handoff_branch), f(source->event.lane),
                                   f(source->event.ordinal), f(source->partition),
                                   f(source->record_offset), f(source->bytes), f(kind),
                                   f(present), f(*used)});
          else
            node->handoff = event(bank, 2108, node->prepared,
                                  {f(handoff_branch), f(source->event.lane),
                                   f(source->event.ordinal), f(source->partition),
                                   f(source->record_offset), f(source->bytes), f(kind),
                                   f(present)});
        } else
          node->handoff = event(bank, 2108, node->prepared, {f(handoff_branch)});
        ProviderCaptureDisposition outcome{ProviderCaptureState::accepted, {}};
        bool overflow;
        {
          std::lock_guard lock{destination.mutex};
          const auto bytes = node->original->bytes().size();
          overflow =
              !node->original->owns_bytes() ||
              node->original->kind() == ProviderOriginalExtentKind::unknown_used ||
              destination.capture_count >= max_events ||
              bytes > max_capture_bytes - destination.capture_bytes;
          if (overflow) {
            // One physical source per worker; prepare rejects while emergency is
            // occupied, so this already acquired extent always has its own slot.
            participant.emergency = std::move(node);
            outcome = {ProviderCaptureState::overflow, Error{ErrorCode::capacity}};
          } else {
            auto *last = node.get();
            if (destination.tail)
              destination.tail->next = std::move(node);
            else
              destination.captures = std::move(node);
            destination.tail = last;
            destination.capture_bytes += bytes;
            ++destination.capture_count;
          }
          const std::uint8_t branch = overflow ? 2 : 1;
          const bool stopped = participant.stop.load();
          const auto parent =
              overflow ? participant.emergency->handoff : destination.tail->handoff;
          if (outcome.error)
            (void)event(bank, 2109, parent,
                        {f(destination.capture_bytes), f(destination.capture_count),
                         f(branch), f(stopped), f(outcome.error->code),
                         f(outcome.error->detail)});
          else
            (void)event(bank, 2109, parent,
                        {f(destination.capture_bytes), f(destination.capture_count),
                         f(branch), f(stopped)});
        }
        receiver.pending = false;
        if (overflow)
          destination.stop(participant, participant.lifetime, receiver.transport, 2);
        destination.changed.notify_all();
        return outcome;
      }
    };
    Result<std::unique_ptr<ProviderOriginalTicket>>
    prepare(ProviderOriginalRequest request) override {
      RawParticipantBank bank{171};
      const auto role = static_cast<std::uint8_t>(request.role),
                 mode = static_cast<std::uint8_t>(request.mode);
      const auto label_bytes = request.label.size();
      const auto maximum_bytes = request.maximum_bytes;
      const std::array fields{f(role), f(label_bytes), text(request.label),
                              f(maximum_bytes), f(mode)};
      const auto prepared =
          raw_participant_data(2107, transport, fields, 58 + request.label.size());
      bool claimed = false;
      try {
        if (pending)
          throw Error{ErrorCode::busy};
        {
          std::lock_guard lock{pool.mutex};
          if (run.stop.load() || run.emergency || pool.capture_count >= max_events)
            throw Error{run.stop.load() ? ErrorCode::interrupted : ErrorCode::capacity};
        }
        pending = true;
        claimed = true;
        std::optional<Value> metadata;
        {
          std::lock_guard lock{pool.mutex};
          metadata.emplace(Value::object({{"run_id", field(run.row, "run_id")},
                                          {"request_id", direction.delivery}}));
        }
        auto node = std::make_unique<Capture>(
            Capture{&run,
                    "participant." + std::string{request.label},
                    std::move(*metadata),
                    prepared,
                    {},
                    {},
                    {}});
        auto storage = std::make_shared<ProviderOriginalStorage>();
        if (request.mode == ProviderOriginalMode::native_extent)
          storage->bytes.resize(request.maximum_bytes);
        std::unique_ptr<ProviderOriginalTicket> result = std::make_unique<Ticket>(
            *this, std::move(node), std::move(storage), std::move(bank));
        return Result<std::unique_ptr<ProviderOriginalTicket>>::success(
            std::move(result));
      } catch (...) {
        if (claimed)
          pending = false;
        const auto failure = exception_error();
        const std::uint8_t branch = 3;
        (void)event(bank, 2108, prepared,
                    {f(branch), f(failure.code), f(failure.detail)});
        return Result<std::unique_ptr<ProviderOriginalTicket>>::failure(failure);
      }
    }
  };
  void work(Run &run, std::unique_ptr<PreparedDirection> direction,
            const ParticipantTransport &transport) noexcept {
    RawThreadBinding binding{run.worker_handle};
    RawWaitScope waits{{{run.observer, -1}, {}}};
    auto &bank = run.lifetime;
    const bool initial_stop = run.stop.load();
    {
      std::lock_guard lock{mutex};
      run.entry = event(bank, 2103, run.reference,
                        {f(direction->offer.lane), f(direction->offer.ordinal),
                         f(run.queued), f(initial_stop)});
    }
    std::optional<Error> failure;
    try {
      for (;;) {
        RawParticipantBank execution{342};
        const bool identities = direction->origin.identities.has_value(),
                   source = direction->source.has_value();
        std::array<ByteView, 10> request_fields{};
        std::size_t count = 0;
        request_fields[count++] = f(direction->offer.lane);
        request_fields[count++] = f(direction->offer.ordinal);
        request_fields[count++] = f(identities);
        if (identities) {
          request_fields[count++] = f(direction->origin.identities->attempt);
          request_fields[count++] = f(direction->origin.identities->invocation);
          request_fields[count++] = f(direction->origin.identities->generation);
        }
        request_fields[count++] = f(source);
        if (source) {
          request_fields[count++] = f(direction->source->event.lane);
          request_fields[count++] = f(direction->source->event.ordinal);
        }
        const auto dispatched =
            execution.linked(2104, run.entry, std::span{request_fields}.first(count));
        Receiver receiver{*this, run, *direction, dispatched};
        RawProviderOwner owner{};
        if (identities)
          owner = {direction->origin.identities->attempt,
                   direction->origin.identities->invocation,
                   direction->origin.identities->generation, dispatched};
        RawAdapterContext context{identities ? &owner : nullptr,
                                  dispatched.ordinal ? std::optional{dispatched}
                                                     : std::nullopt,
                                  &receiver, nullptr};
        std::optional<Value> result;
        std::uint8_t transport_branch = 1, stage = 1;
        try {
          if (run.stop.load())
            throw Error{ErrorCode::interrupted};
          // Capture is presentation only. Source leases own and publish originals.
          result.emplace(call_prepared_colleague(
              direction->prepared, [](std::string_view, std::string_view) {},
              [&](const Value &prepared, const ColleagueCapture &capture,
                  const RawAdapterContext *actual_context) {
                if (run.stop.load())
                  throw Error{ErrorCode::interrupted};
                return transport(
                    prepared, capture, [&] { return run.stop.load(); }, actual_context);
              },
              &context, direction->source));
        } catch (...) {
          const auto error = exception_error();
          transport_branch = 2;
          const bool stopped = run.stop.load();
          (void)event(execution, 2105, dispatched,
                      {f(transport_branch), f(stage), f(stopped), f(error.code),
                       f(error.detail)});
          throw;
        }
        const bool stopped = run.stop.load();
        (void)event(execution, 2105, dispatched,
                    {f(transport_branch), f(stage), f(stopped)});
        auto display = projection(*result);
        Value publication;
        {
          std::lock_guard lock{mutex};
          set(run.row, "result", std::move(display));
          set(run.row, "last_delivery", direction->delivery);
          const auto status = string_field(*result, "status"),
                     remote = string_field(*result, "remote_disposition");
          run.finished = run.stop.load() || status != "completed";
          set(run.row, "state",
              Value{remote == "unknown" ? "unknown"
                    : run.stop.load()   ? "cancelled"
                    : run.finished      ? "failed"
                                        : "waiting"});
          if (run.finished) {
            erase(run.row, "request");
            withdraw(run, bank, 2);
          }
          dirty = true;
          state(run, execution, 1);
          publication = run.row;
        }
        notify(publication, execution, dispatched);
        changed.notify_all();
        {
          std::unique_lock lock{mutex};
          if (run.finished)
            break;
          changed.wait(
              lock, [&] { return run.stop.load() || run.sealed || run.queued != 0; });
          if (run.stop.load() || run.queued == 0) {
            run.finished = true;
            set(run.row, "state",
                run.stop.load() ? std::move(run.cancelled_state)
                                : std::move(run.completed_state));
            erase(run.row, "request");
            withdraw(run, bank, 2);
            dirty = true;
            break;
          }
          direction = std::move(run.inbox[run.head]);
          run.head = (run.head + 1) % max_inbox;
          --run.queued;
          const std::uint8_t transferred = 1;
          const bool stop_observed = run.stop.load();
          (void)event(bank, 2116, direction->offer,
                      {f(run.reference.lane), f(run.reference.ordinal), f(transferred),
                       f(run.queued), f(stop_observed)});
          set(run.row, "state", Value{"running"});
          dirty = true;
        }
      }
    } catch (...) {
      failure = exception_error();
      std::lock_guard lock{mutex};
      set(run.row, "state", std::move(run.failure_state));
      set(run.row, "result", std::move(run.failure_result));
      set(run.row, "last_delivery", std::move(direction->delivery));
      run.finished = true;
      erase(run.row, "request");
      withdraw(run, bank, 2);
      dirty = true;
    }
    {
      std::lock_guard lock{mutex};
      state(run, bank, failure ? 3 : 2);
    }
    // Terminal presentation can allocate/fail, but notify catches it and emits
    // its held result. Native cleanup/END below do not construct failure Values.
    notify(run.row, bank, run.reference, true);
    {
      std::lock_guard lock{mutex};
      const bool stopped = run.stop.load(),
                 emergency = static_cast<bool>(run.emergency);
      const std::uint8_t branch = failure ? 2 : 1;
      if (failure)
        run.end =
            event(bank, 2126, run.reference,
                  {f(stopped), f(run.finished), f(run.sealed), f(run.queued),
                   f(emergency), f(branch), f(failure->code), f(failure->detail)});
      else
        run.end = event(bank, 2126, run.reference,
                        {f(stopped), f(run.finished), f(run.sealed), f(run.queued),
                         f(emergency), f(branch)});
    }
    changed.notify_all();
  }
  std::optional<SourceReference> retain_original(RawParticipantBank &bank,
                                                 RawEventRef parent,
                                                 const ProviderOwnedOriginal &original,
                                                 std::string_view label,
                                                 const Value &metadata) {
    const auto &source = original.reference();
    if (raw_enabled() && !source)
      throw Error{ErrorCode::incomplete};
    const ProviderOriginalReference absent{
        {}, RawPartition::data, 0, original.bytes().size()};
    const auto &physical = source ? *source : absent;
    const auto kind = original.kind();
    const auto request =
        event(bank, 2111, parent,
              {f(physical.event.lane), f(physical.event.ordinal), f(physical.partition),
               f(physical.record_offset), f(physical.bytes), f(kind)});
    try {
      const auto result = [&] {
        const OwnerCallback scope{owner_callback_active};
        return retain({label, original.bytes(), metadata});
      }();
      const std::uint8_t branch = 1;
      const bool present = result.has_value();
      if (result)
        (void)event(
            bank, 2112, request,
            {f(branch), f(present), f(result->journal.bytes()), f(result->sequence)});
      else
        (void)event(bank, 2112, request, {f(branch), f(present)});
      return result;
    } catch (...) {
      const auto failure = exception_error();
      const std::uint8_t branch = 2;
      const bool present = false;
      (void)event(bank, 2112, request,
                  {f(branch), f(present), f(failure.code), f(failure.detail)});
      throw;
    }
  }
  void save_snapshot(RawParticipantBank &bank, RawEventRef parent) {
    RawEventRef request;
    Value saved;
    {
      std::lock_guard lock{mutex};
      if (!dirty)
        return;
      request = event(bank, 2113, parent, {f(runs.size()), f(concurrency), f(dirty)});
    }
    try {
      {
        std::lock_guard lock{mutex};
        saved = snapshot();
        dirty = false;
      }
      const auto identity = [&] {
        const OwnerCallback scope{owner_callback_active};
        return save(saved);
      }();
      const std::uint8_t branch = 1;
      const bool present = identity.has_value();
      if (identity)
        (void)event(bank, 2114, request, {f(branch), f(present), f(identity->bytes())});
      else
        (void)event(bank, 2114, request, {f(branch), f(present)});
    } catch (...) {
      const auto failure = exception_error();
      const std::uint8_t branch = 2;
      const bool present = false;
      (void)event(bank, 2114, request,
                  {f(branch), f(present), f(failure.code), f(failure.detail)});
      {
        std::lock_guard lock{mutex};
        dirty = true;
      }
      stop_all(bank, request, 4);
      throw;
    }
  }
};
Participants::Participants(const Value &saved, Retain retain, Save save,
                           Publish publish)
    : impl_(std::make_unique<Impl>(std::move(retain), std::move(save),
                                   std::move(publish))) {
  if (saved == Value{})
    return;
  RawParticipantTrace trace{RawParticipantAction::restore, 142};
  try {
    impl_->concurrency = integer(field(saved, "concurrency"));
    if (impl_->concurrency < 1 || impl_->concurrency > 16 ||
        field(saved, "runs").array().size() > max_runs)
      throw Error{ErrorCode::corrupt};
    for (const auto &row : field(saved, "runs").array()) {
      auto run = std::make_unique<Impl::Run>();
      run->row = row;
      run->finished = true;
      run->sealed = true;
      run->cleanup_closed = true;
      const auto &id = string_field(row, "run_id"),
                 &previous = string_field(row, "state");
      bounded(id, 128);
      const bool interrupted = previous == "running" || previous == "waiting" ||
                               previous == "cancel_requested";
      const std::uint8_t branch = interrupted ? 1 : 2;
      const auto id_bytes = id.size(), previous_bytes = previous.size();
      const std::array fields{f(id_bytes),    text(id),  f(previous_bytes),
                              text(previous), f(branch), f(run->finished),
                              f(run->sealed)};
      run->reference = raw_participant_data(2129, trace.reference(), fields,
                                            59 + id.size() + previous.size());
      if (interrupted) {
        set(run->row, "state", Value{"unknown"});
        set(run->row, "reopened", Value{true});
        impl_->dirty = true;
      }
      erase(run->row, "request");
      if (!impl_->runs.emplace(id, std::move(run)).second)
        throw Error{ErrorCode::corrupt};
    }
    trace.finish(1);
  } catch (...) {
    trace.finish(2, exception_error());
    throw;
  }
  drain();
}
Participants::~Participants() {
  try {
    shutdown();
  } catch (...) {
  }
}
bool Participants::active() const {
  std::lock_guard lock{impl_->mutex};
  for (const auto &[id, run] : impl_->runs) {
    (void)id;
    if (!run->finished)
      return true;
  }
  return false;
}
bool Participants::debt() const {
  std::lock_guard lock{impl_->mutex};
  return impl_->debt_locked();
}
void Participants::drain() {
  impl_->entry(RawParticipantAction::drain);
  if (impl_->draining)
    throw Error{ErrorCode::busy};
  impl_->draining = true;
  struct Guard {
    bool &value;
    ~Guard() { value = false; }
  } guard{impl_->draining};
  RawParticipantTrace trace{RawParticipantAction::drain, 142};
  try {
    for (;;) {
      // No claim moves to a worker; selection happens only after owner admission.
      RawParticipantBank bank{8408};
      Impl::Capture *capture = nullptr;
      Impl::Run *emergency = nullptr;
      {
        std::lock_guard lock{impl_->mutex};
        capture = impl_->captures.get();
        if (!capture)
          for (auto &[id, run] : impl_->runs) {
            (void)id;
            if (run->emergency) {
              capture = run->emergency.get();
              emergency = run.get();
              break;
            }
          }
        if (!capture)
          break;
        (void)event(bank, 2110, capture->handoff,
                    {f(impl_->capture_count), f(impl_->capture_bytes)});
      }
      if (!capture->original)
        throw Error{ErrorCode::corrupt};
      const auto &original = *capture->original;
      if (!original.owns_bytes() ||
          original.kind() != ProviderOriginalExtentKind::exact_used ||
          (raw_enabled() && !original.reference())) {
        const std::uint8_t branch = 1;
        const Error failure{ErrorCode::incomplete};
        (void)event(bank, 2135, trace.reference(),
                    {f(capture->handoff.lane), f(capture->handoff.ordinal),
                     f(original.bytes().size()), f(branch), f(failure.code),
                     f(failure.detail)});
        impl_->stop_all(bank, trace.reference(), 3);
        throw failure;
      }
      try {
        (void)impl_->retain_original(bank, capture->handoff, original, capture->label,
                                     capture->metadata);
      } catch (...) {
        impl_->stop_all(bank, capture->handoff, 3);
        throw;
      }
      {
        std::lock_guard lock{impl_->mutex};
        if (emergency)
          emergency->emergency.reset();
        else {
          const auto bytes = original.bytes().size();
          impl_->captures = std::move(capture->next);
          impl_->capture_bytes -= bytes;
          --impl_->capture_count;
          if (!impl_->captures)
            impl_->tail = nullptr;
        }
      }
    }
    RawParticipantBank save_bank{8312};
    impl_->save_snapshot(save_bank, trace.reference());
    trace.finish(1);
  } catch (...) {
    trace.finish(2, exception_error());
    throw;
  }
}
Value Participants::configure(const Value &args) {
  impl_->entry(RawParticipantAction::configure);
  drain();
  return action(
      RawParticipantAction::configure, 8502, {}, [&](RawParticipantTrace &trace) {
        const auto count = integer(field(args, "concurrency"));
        if (count < 1 || count > 16 || active())
          throw Error{ErrorCode::invalid_range};
        {
          std::lock_guard lock{impl_->mutex};
          const auto old = impl_->concurrency;
          (void)event(trace.bank(), 2133, trace.reference(), {f(old), f(count)});
          impl_->concurrency = count;
          impl_->dirty = true;
        }
        impl_->save_snapshot(trace.bank(), trace.reference());
        std::lock_guard lock{impl_->mutex};
        return Value::object({{"concurrency", Value{Number{count}}},
                              {"max_runs", Value{Number{max_runs}}},
                              {"runs", field(impl_->snapshot(), "runs")}});
      });
}
Value Participants::start(std::string id, const Value &args,
                          const ParticipantTransport &transport,
                          RawParticipantOrigin origin) {
  impl_->entry(RawParticipantAction::start, origin);
  drain();
  return action(
      RawParticipantAction::start, 10706, origin, [&](RawParticipantTrace &trace) {
        auto &bank = trace.bank();
        trace.stage(1);
        bounded(id, 128);
        const auto &request = field(args, "request");
        const auto task = args.find("task_id") ? string_field(args, "task_id") : "";
        if (task.size() > 128 || !transport)
          throw Error{ErrorCode::invalid_range};
        {
          std::lock_guard lock{impl_->mutex};
          std::size_t live = 0;
          for (const auto &[key, run] : impl_->runs) {
            (void)key;
            live += run->finished ? 0U : 1U;
          }
          if (impl_->runs.size() >= max_runs || live >= impl_->concurrency)
            throw Error{ErrorCode::capacity};
          if (impl_->runs.contains(id))
            throw Error{ErrorCode::conflict};
        }
        auto direction = std::make_unique<Impl::PreparedDirection>(
            Impl::PreparedDirection{prepare_colleague(request),
                                    field(request, "request_id"),
                                    {},
                                    {},
                                    origin,
                                    trace.reference()});
        auto run = std::make_unique<Impl::Run>();
        run->row = Value::object({{"run_id", Value{id}},
                                  {"state", Value{"running"}},
                                  {"cancel_requested", Value{false}},
                                  {"task_id", Value{task}},
                                  {"provider", field(request, "provider")},
                                  {"model", field(request, "model")},
                                  {"from", field(request, "from")},
                                  {"to", field(request, "to")},
                                  {"request", request},
                                  {"result", Value{}},
                                  {"last_delivery", Value{}},
                                  {"dropped_directions", Value{Number{0}}}});
        (void)run->row.object(); // Keep the mutable owner row private across copies.
        run->setup_handle = raw_current_thread();
        run->cleanup = bank.split(cleanup_credit);
        run->worker_handle = unwrap(raw_prepare_thread());
        IdentityBytes worker_lane{};
        {
          RawThreadBinding binding{run->worker_handle};
          worker_lane = raw_lane_epoch();
          run->lifetime = RawParticipantBank{worker_credit};
        }
        const auto result =
            Value::object({{"run_id", Value{id}}, {"state", Value{"running"}}});
        // Stage the actual map node before the acknowledged admission append.
        std::map<std::string, std::unique_ptr<Impl::Run>> staged;
        auto *pointer = run.get();
        staged.emplace(id, std::move(run));
        auto node = staged.extract(id);
        try {
          impl_->setup(*pointer, bank, trace.reference());
          pointer->reference =
              event(bank, 2102, trace.reference(),
                    {f(id.size()), text(id), f(task.size()), text(task), f(worker_lane),
                     f(impl_->concurrency)});
          RawAdapterContext context{nullptr,
                                    trace.reference().ordinal
                                        ? std::optional{trace.reference()}
                                        : std::nullopt,
                                    nullptr, nullptr};
          auto original = provider_encode_original(
              direction->prepared,
              {ProviderOriginalRole::prepared_admission,
               ProviderOriginalMode::codec_adopt, ValueLimits{}.bytes, "admission"},
              &context);
          direction->source = original.reference();
          const auto metadata = Value::object(
              {{"run_id", Value{id}}, {"request_id", direction->delivery}});
          trace.stage(2);
          direction->retained = impl_->retain_original(
              bank, trace.reference(), original, "participant.admission", metadata);
          {
            std::lock_guard lock{impl_->mutex};
            auto inserted = impl_->runs.insert(std::move(node));
            if (!inserted.inserted) {
              node = std::move(inserted.node);
              throw Error{ErrorCode::conflict};
            }
            impl_->dirty = true;
          }
          trace.stage(3);
          impl_->save_snapshot(bank, trace.reference());
          impl_->notify(pointer->row, bank, pointer->reference, false,
                        Impl::PublicationOwner::owner);
          // Thread construction may fail after the accepted admission. The staged
          // Run remains truthful and its held native cleanup remains available.
          {
            std::lock_guard lock{impl_->mutex};
            if (pointer->finished || pointer->stop.load() || pointer->cleanup_closed)
              throw Error{ErrorCode::busy};
          }
          // Publish before the new thread can borrow the observer. A failed
          // constructor creates no borrower and is handled by ordinary cleanup.
          pointer->endpoint_borrowed.store(true, std::memory_order_release);
          try {
            pointer->worker = std::jthread(
                [this, pointer, direction = std::move(direction), transport]() mutable {
                  impl_->work(*pointer, std::move(direction), transport);
                });
          } catch (...) {
            pointer->endpoint_borrowed.store(false, std::memory_order_release);
            throw;
          }
        } catch (...) {
          const auto failure = exception_error();
          {
            std::lock_guard lock{impl_->mutex};
            set(pointer->row, "state", std::move(pointer->failure_state));
            pointer->finished = true;
            impl_->dirty = true;
          }
          impl_->cleanup_run(*pointer);
          throw failure;
        }
        return result;
      });
}
Value Participants::read(const Value &args) {
  impl_->entry(RawParticipantAction::read);
  drain();
  return action(RawParticipantAction::read, 2830, {}, [&](RawParticipantTrace &trace) {
    std::lock_guard lock{impl_->mutex};
    const auto project = [&](Impl::Run &run, bool concise) {
      const bool stopped = run.stop.load();
      (void)event(trace.bank(), 2132, trace.reference(),
                  {f(run.reference.lane), f(run.reference.ordinal), f(run.queued),
                   f(run.receipts.size()), f(run.sealed), f(run.finished), f(stopped),
                   f(impl_->dirty)});
      auto row = run.row;
      set(row, "queued_directions", Value{Number{run.queued}});
      set(row, "accepted_directions", Value{Number{run.receipts.size()}});
      set(row, "sealed", Value{run.sealed});
      erase(row, "request");
      if (concise)
        erase(row, "result");
      return row;
    };
    if (args.find("run_id"))
      return project(impl_->find(args), false);
    Value::Array rows;
    for (const auto &[id, run] : impl_->runs) {
      (void)id;
      rows.push_back(project(*run, true));
    }
    return Value::object({{"concurrency", Value{Number{impl_->concurrency}}},
                          {"max_runs", Value{Number{max_runs}}},
                          {"runs", Value{std::move(rows)}}});
  });
}
Value Participants::send(const Value &args, RawParticipantOrigin origin) {
  impl_->entry(RawParticipantAction::send, origin);
  drain();
  return action(
      RawParticipantAction::send, 393, origin, [&](RawParticipantTrace &trace) {
        auto &bank = trace.bank();
        const auto &run_id = string_field(args, "run_id"),
                   &message = string_field(args, "message_id"),
                   &from = string_field(args, "from"),
                   &body = string_field(args, "text");
        const auto run_bytes = run_id.size(), message_bytes = message.size(),
                   from_bytes = from.size(), body_bytes = body.size();
        const std::array fields{f(run_bytes),  text(run_id),  f(message_bytes),
                                text(message), f(from_bytes), text(from),
                                f(body_bytes), text(body)};
        const auto offered = raw_participant_data(2115, trace.reference(), fields,
                                                  72 + run_id.size() + message.size() +
                                                      from.size() + body.size());
        Impl::Run *run = nullptr;
        const auto returned = [&](std::uint8_t branch,
                                  const std::optional<RawEventRef> &matched,
                                  std::optional<Error> failure = {}) {
          const bool present = matched.has_value();
          const auto queued = run ? run->queued : 0,
                     count = run ? run->receipts.size() : 0;
          if (matched)
            (void)event(bank, 2118, offered,
                        {f(branch), f(present), f(matched->lane), f(matched->ordinal),
                         f(queued), f(count)});
          else if (failure)
            (void)event(bank, 2118, offered,
                        {f(branch), f(present), f(queued), f(count), f(failure->code),
                         f(failure->detail)});
          else
            (void)event(bank, 2118, offered,
                        {f(branch), f(present), f(queued), f(count)});
        };
        try {
          if (args.object().size() != 4)
            throw Error{ErrorCode::unsupported};
          for (const auto &[key, value] : args.object()) {
            (void)value;
            if (key != "run_id" && key != "message_id" && key != "from" &&
                key != "text")
              throw Error{ErrorCode::unsupported};
          }
          bounded(message, 128);
          bounded(from, 128);
          bounded(body, 2048);
          Value request;
          {
            std::lock_guard lock{impl_->mutex};
            run = &impl_->find(args);
            if (const auto receipt = run->receipts.find(message);
                receipt != run->receipts.end()) {
              for (const auto key : {"run_id", "message_id", "from", "text"})
                if (field(receipt->second.arguments, key) != field(args, key))
                  throw Error{ErrorCode::conflict};
              returned(2, receipt->second.offer);
              return Value::object({{"message_id", Value{message}},
                                    {"accepted", Value{true}},
                                    {"duplicate", Value{true}}});
            }
            if (run->finished || run->sealed || run->stop.load())
              throw Error{ErrorCode::busy};
            if (run->queued >= max_inbox || run->receipts.size() >= max_messages)
              throw Error{ErrorCode::capacity};
            request = field(run->row, "request");
            set(request, "request_id",
                Value{string_field(request, "request_id") + ":" + message});
            set(request, "task",
                Value{string_field(request, "task") + "\nAddressed direction from " +
                      from + ":\n" + body});
            if (const auto *result = run->row.find("result");
                result && result->find("text")) {
              auto context = field(request, "context");
              context.array().push_back(
                  Value::object({{"id", Value{"participant-prior-answer:" + run_id}},
                                 {"text", field(*result, "text")}}));
              set(request, "context", std::move(context));
            }
          }
          auto direction = std::make_unique<Impl::PreparedDirection>(
              Impl::PreparedDirection{prepare_colleague(request),
                                      field(request, "request_id"),
                                      {},
                                      {},
                                      origin,
                                      offered});
          std::map<std::string, Impl::Receipt> receipts;
          receipts.emplace(message, Impl::Receipt{args, offered});
          auto receipt = receipts.extract(message);
          auto result = Value::object({{"message_id", Value{message}},
                                       {"run_id", Value{run_id}},
                                       {"accepted", Value{true}},
                                       {"boundary", Value{"after current request"}}});
          const auto metadata = Value::object({{"run_id", Value{run_id}},
                                               {"request_id", direction->delivery},
                                               {"message", args}});
          RawAdapterContext context{
              nullptr, offered.ordinal ? std::optional{offered} : std::nullopt, nullptr,
              nullptr};
          auto original = provider_encode_original(
              direction->prepared,
              {ProviderOriginalRole::prepared_admission,
               ProviderOriginalMode::codec_adopt, ValueLimits{}.bytes, "admission"},
              &context);
          direction->source = original.reference();
          direction->retained = impl_->retain_original(
              bank, offered, original, "participant.admission", metadata);
          {
            std::lock_guard lock{impl_->mutex};
            if (run->finished || run->sealed || run->stop.load())
              throw Error{ErrorCode::busy};
            const auto inserted = run->receipts.insert(std::move(receipt));
            if (!inserted.inserted)
              throw Error{ErrorCode::conflict};
            run->inbox[(run->head + run->queued) % max_inbox] = std::move(direction);
            ++run->queued;
            returned(1, {});
          }
          impl_->changed.notify_all();
          return result;
        } catch (...) {
          const auto failure = exception_error();
          {
            std::lock_guard lock{impl_->mutex};
            returned(3, {}, failure);
          }
          throw;
        }
      });
}
Value Participants::cancel(const Value &args) {
  impl_->entry(RawParticipantAction::cancel);
  drain();
  return action(RawParticipantAction::cancel, 8801, {},
                [&](RawParticipantTrace &trace) {
                  Impl::Run *run;
                  {
                    std::lock_guard lock{impl_->mutex};
                    run = &impl_->find(args);
                  }
                  impl_->stop(*run, trace.bank(), trace.reference(), 1);
                  impl_->save_snapshot(trace.bank(), trace.reference());
                  Value row;
                  {
                    std::lock_guard lock{impl_->mutex};
                    row = run->row;
                  }
                  impl_->notify(row, trace.bank(), trace.reference(), false,
                                Impl::PublicationOwner::owner);
                  return row;
                });
}
Value Participants::await(const Value &args, const std::function<bool()> &cancelled) {
  impl_->entry(RawParticipantAction::await);
  return action(RawParticipantAction::await, 142, {}, [&](RawParticipantTrace &) {
    const auto ms =
        args.find("timeout_ms") ? integer(field(args, "timeout_ms")) : 30000U;
    if (ms < 1 || ms > 3600000)
      throw Error{ErrorCode::invalid_range};
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{ms};
    bool timed_out = false;
    for (;;) {
      drain();
      {
        std::unique_lock lock{impl_->mutex};
        auto &run = impl_->find(args);
        if (run.finished ||
            (string_field(run.row, "state") == "waiting" && run.queued == 0))
          break;
        if (std::chrono::steady_clock::now() >= deadline) {
          timed_out = true;
          break;
        }
        impl_->changed.wait_for(lock, std::chrono::milliseconds{20});
      }
      if (impl_->cancelled(cancelled))
        throw Error{ErrorCode::interrupted};
    }
    auto row = read(args);
    set(row, "await_timed_out", Value{timed_out});
    return row;
  });
}
Value Participants::join(const Value &args, const std::function<bool()> &cancelled) {
  impl_->entry(RawParticipantAction::join);
  return action(RawParticipantAction::join, 6158, {}, [&](RawParticipantTrace &trace) {
    std::array<Impl::Run *, max_runs> selected{};
    std::size_t count = 0;
    {
      std::lock_guard lock{impl_->mutex};
      const auto &ids = field(args, "run_ids").array();
      if (ids.empty() || ids.size() > max_runs)
        throw Error{ErrorCode::invalid_range};
      for (const auto &id : ids) {
        const auto found = impl_->runs.find(id.string());
        if (found == impl_->runs.end())
          throw Error{ErrorCode::invalid_range};
        auto *run = found->second.get();
        if (std::find(selected.begin(),
                      selected.begin() + static_cast<std::ptrdiff_t>(count),
                      run) != selected.begin() + static_cast<std::ptrdiff_t>(count))
          throw Error{ErrorCode::conflict};
        selected[count++] = run;
      }
      for (std::size_t i = 0; i < count; ++i) {
        auto &run = *selected[i];
        run.sealed = true;
        (void)event(trace.bank(), 2119, trace.reference(),
                    {f(run.reference.lane), f(run.reference.ordinal), f(run.sealed),
                     f(run.queued)});
      }
    }
    impl_->changed.notify_all();
    for (;;) {
      drain();
      bool finished = true;
      {
        std::unique_lock lock{impl_->mutex};
        for (std::size_t i = 0; i < count; ++i)
          finished &= selected[i]->finished;
        if (finished)
          break;
        impl_->changed.wait_for(lock, std::chrono::milliseconds{20});
      }
      if (impl_->cancelled(cancelled))
        throw Error{ErrorCode::interrupted};
    }
    for (std::size_t i = 0; i < count; ++i)
      impl_->join_native(*selected[i], trace.bank(), trace.reference());
    drain();
    Value::Array rows;
    for (std::size_t i = 0; i < count; ++i)
      rows.push_back(
          read(Value::object({{"run_id", field(selected[i]->row, "run_id")}})));
    return Value::object({{"runs", Value{std::move(rows)}}});
  });
}
Value Participants::archive(const Value &args) {
  impl_->entry(RawParticipantAction::archive);
  drain();
  return action(RawParticipantAction::archive, 8642, {},
                [&](RawParticipantTrace &trace) {
                  Impl::Run *run;
                  {
                    std::lock_guard lock{impl_->mutex};
                    run = &impl_->find(args);
                    if (!run->finished)
                      throw Error{ErrorCode::busy};
                  }
                  auto result = Value::object({{"archived", field(args, "run_id")}});
                  impl_->join_native(*run, trace.bank(), trace.reference());
                  impl_->cleanup_run(*run);
                  {
                    std::lock_guard lock{impl_->mutex};
                    const std::uint8_t branch = 1;
                    (void)event(trace.bank(), 2134, trace.reference(),
                                {f(run->reference.lane), f(run->reference.ordinal),
                                 f(branch), f(impl_->runs.size())});
                    impl_->runs.erase(string_field(args, "run_id"));
                    impl_->dirty = true;
                  }
                  impl_->save_snapshot(trace.bank(), trace.reference());
                  return result;
                });
}
void Participants::shutdown() {
  // No ordinary admission, snapshot, byte drain or presentation here. Every Run
  // has its actual setup-lane bank before publication, even if its worker has
  // published a business terminal but remains inside the final callback.
  std::optional<Error> failure;
  for (auto &[id, run] : impl_->runs) {
    (void)id;
    try {
      impl_->cleanup_run(*run);
    } catch (...) {
      failure = exception_error();
    }
  }
  if (failure)
    throw *failure;
}
} // namespace blackbird
