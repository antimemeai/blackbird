#include "blackbird/tui_action_bridge.hpp"
#include "blackbird/context.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/raw_ui.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
namespace blackbird::tui::lua {
namespace {
using blackbird::detail::raw_native_field;
template <std::size_t N> bool valid(const Text<N> &text) noexcept {
  return text.size && text.size <= N;
}
Text<128> request_id(const ActionKey &key) noexcept {
  Text<128> result;
  constexpr std::string_view prefix = "bb-ui:", hex = "0123456789abcdef";
  std::copy(prefix.begin(), prefix.end(), result.bytes.begin());
  result.size = prefix.size();
  for (const auto byte : key.inbox) {
    const auto value = std::to_integer<unsigned>(byte);
    result.bytes[result.size++] = hex[value >> 4];
    result.bytes[result.size++] = hex[value & 15];
  }
  const auto number = [&](std::uint64_t value, unsigned width) {
    result.bytes[result.size++] = ':';
    for (unsigned n = width; n; --n)
      result.bytes[result.size++] = hex[(value >> ((n - 1) * 4)) & 15];
  };
  number(key.owner, 16);
  number(key.trigger, 16);
  number(key.ordinal, 8);
  return result;
}
struct Wire {
  std::array<std::byte, 1024> bytes{};
  std::size_t size = 0;
  void append(ByteView value) {
    if (value.size() > bytes.size() - size)
      throw Error{ErrorCode::capacity};
    std::copy(value.begin(), value.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(size));
    size += value.size();
  }
  template <class T> void native(const T &value) {
    append(std::as_bytes(std::span{&value, 1}));
  }
  void text(std::string_view value) {
    native(value.size());
    append(std::as_bytes(std::span{value.data(), value.size()}));
  }
  void reference(const RawEventRef &value) {
    append(value.lane);
    native(value.ordinal);
  }
};
Wire encode(const BridgeReceipt &receipt) {
  const auto &envelope = receipt.envelope;
  const auto &stamp = envelope.stamp;
  Wire wire;
  wire.append(envelope.key.inbox);
  wire.native(envelope.key.owner);
  wire.native(envelope.key.trigger);
  wire.native(envelope.key.ordinal);
  wire.append(stamp.environment.bytes());
  wire.append(stamp.conversation.bytes());
  for (const auto value :
       {stamp.view.owner, stamp.view.incarnation, stamp.source_generation,
        stamp.request_sequence, stamp.scene_revision, stamp.local_input_revision,
        stamp.query_revision, envelope.acceptance_order})
    wire.native(value);
  wire.reference(envelope.input_origin);
  wire.text(envelope.target.owner.view());
  wire.text(envelope.target.kind.view());
  wire.text(envelope.target.key.view());
  wire.native(envelope.target.incarnation);
  wire.text(envelope.handler.view());
  wire.text(envelope.verb.view());
  wire.text(receipt.request_id.view());
  wire.text("signal");
  wire.text("interrupt");
  return wire;
}
constexpr std::uint64_t return_credit = raw_envelope_size + 24 +
                                        sizeof(BridgeDisposition) + 2 * sizeof(bool) +
                                        sizeof(ErrorCode) + sizeof(std::int64_t);
void failure(BridgeReceipt &receipt, BridgeDisposition disposition,
             Error error) noexcept {
  receipt.disposition = disposition;
  receipt.has_error = true;
  receipt.error = error;
}
} // namespace
struct NativeActionBridge::State {
  State(BridgeScope actual, std::shared_ptr<CommandJobs> jobs,
        std::shared_ptr<ActionInbox> actions)
      : scope(actual), commands(std::move(jobs)), inbox(std::move(actions)) {
    const auto name = hex_identity(scope.environment.bytes()) + "/" +
                      hex_identity(scope.conversation.bytes());
    owner.size = name.size();
    std::copy(name.begin(), name.end(), owner.bytes.begin());
    CommandObservationId bootstrap;
    bootstrap.bytes.fill('0');
    incarnation =
        commands->try_observe_delivery({0, bootstrap, "bridge/bootstrap"}).incarnation;
    if (!incarnation)
      throw Error{ErrorCode::incomplete};
    producer = unwrap(raw_prepare_thread());
    stop_wake = std::make_unique<RawCommandWake>(RawCommandWake::Mode::signal);
  }
  ~State() {
    request_stop();
    if (worker.joinable())
      worker.join();
    stop_wake->retire();
  }
  BridgeScope scope;
  std::shared_ptr<CommandJobs> commands;
  std::shared_ptr<ActionInbox> inbox;
  Text<256> owner;
  std::uint64_t incarnation = 0;
  RawThreadHandle producer;
  RawWorkerDispatch dispatch{};
  std::unique_ptr<RawCommandWake> stop_wake;
  mutable std::mutex mutex;
  std::condition_variable wake;
  std::array<std::optional<BridgeReceipt>, 16> recent{};
  std::size_t head = 0, count = 0;
  bool more_before = false;
  std::atomic_bool stop{false}, stopped{false};
  std::thread worker;
  void request_stop() noexcept {
    if (!stop.exchange(true, std::memory_order_acq_rel))
      stop_wake->notify();
    wake.notify_all();
  }
  void pause() {
    std::unique_lock lock{mutex};
    wake.wait_for(lock, std::chrono::milliseconds{10});
  }
  void observe(BridgeReceipt &receipt) const noexcept {
    if (validate(receipt.envelope))
      return;
    const auto value = commands->try_observe_delivery(
        {incarnation, receipt.job, receipt.request_id.view()});
    receipt.delivery_status = value.status;
    if (value.status == CommandDeliveryStatus::ready) {
      receipt.delivery = value;
      receipt.delivery_known = true;
    }
  }
  void refresh() {
    // The input owner only try-locks and copies fixed rows. No domain method
    // called here waits, allocates, captures or dispatches.
    const std::lock_guard lock{mutex};
    for (std::size_t n = 0; n < count; ++n) {
      auto &entry = recent[(head + n) % recent.size()];
      if (entry)
        observe(*entry);
    }
  }
  void preserve(BridgeReceipt receipt) {
    observe(receipt);
    const std::lock_guard lock{mutex};
    if (count == recent.size()) {
      recent[head] = std::move(receipt);
      head = (head + 1) % recent.size();
      more_before = true;
    } else {
      recent[(head + count++) % recent.size()] = std::move(receipt);
    }
  }
  std::optional<Error> validate(const ActionEnvelope &envelope) const noexcept {
    if (envelope.stamp.environment != scope.environment ||
        envelope.stamp.conversation != scope.conversation)
      return Error{ErrorCode::wrong_environment};
    if (envelope.target.owner.view() != owner.view() ||
        envelope.target.kind.view() != "command")
      return Error{ErrorCode::invalid_identity};
    if (envelope.target.incarnation != incarnation)
      return Error{ErrorCode::wrong_incarnation};
    if (envelope.verb.view() != "interrupt")
      return Error{ErrorCode::unsupported};
    if (envelope.target.key.size != 32 ||
        !std::all_of(envelope.target.key.bytes.begin(),
                     envelope.target.key.bytes.begin() + 32, [](char byte) {
                       return (byte >= '0' && byte <= '9') ||
                              (byte >= 'a' && byte <= 'f');
                     }))
      return Error{ErrorCode::invalid_identity};
    return {};
  }
  bool process(const ActionEnvelope &envelope) {
    // Accepted inbox shapes are fixed and validated by their actual native
    // producer. Defend their bounds before touching a Text view nonetheless.
    if (!valid(envelope.target.owner) || !valid(envelope.target.kind) ||
        !valid(envelope.target.key) || !valid(envelope.handler) ||
        !valid(envelope.verb))
      throw Error{ErrorCode::invalid_range};
    BridgeReceipt receipt{envelope};
    receipt.request_id = request_id(envelope.key);
    auto refusal = validate(envelope);
    if (!refusal) {
      std::copy_n(envelope.target.key.bytes.begin(), receipt.job.bytes.size(),
                  receipt.job.bytes.begin());
      CommandObservationQuery query;
      query.selected = receipt.job;
      query.row_limit = query.byte_limit = query.chunk_limit = 0;
      const auto target = commands->try_observe(query);
      if (target.status == CommandObservationStatus::busy)
        return false;
      if (target.status != CommandObservationStatus::ready)
        refusal = Error{ErrorCode::external_unknown};
      else if (target.selection != CommandObservationSelection::resident)
        refusal = Error{ErrorCode::invalid_identity};
      else if (target.detail.leader_exited || target.detail.worker_finished)
        refusal = Error{ErrorCode::busy};
    }
    RawCaptureFailureScope capture{raw_enabled()};
    std::optional<RawUiTrace> trace;
    bool interrupted_before_forward = false;
    try {
      const auto wire = encode(receipt);
      const std::array fields{ByteView{wire.bytes.data(), wire.size}};
      trace.emplace(raw_enabled(), RawUiSites{4095, 4096}, fields, return_credit, true);
      receipt.forward = trace->reference();
      if (stop.load(std::memory_order_acquire)) {
        interrupted_before_forward = true;
        failure(receipt, BridgeDisposition::refused, {ErrorCode::interrupted});
      } else if (refusal) {
        failure(receipt, BridgeDisposition::refused, *refusal);
      } else {
        auto arguments = Value::object(
            {{"op", Value{"signal"}},
             {"job_id", Value{std::string{envelope.target.key.view()}}},
             {"request_id", Value{std::string{receipt.request_id.view()}}},
             {"signal", Value{"interrupt"}}});
        receipt.forward_called = true;
        commands->request(std::move(arguments));
        capture.require();
        receipt.disposition = BridgeDisposition::admitted;
      }
    } catch (const Error &error) {
      failure(receipt,
              capture.error() ? BridgeDisposition::unknown : BridgeDisposition::refused,
              error);
      interrupted_before_forward = !receipt.forward_called &&
                                   stop.load(std::memory_order_acquire) &&
                                   error.code == ErrorCode::interrupted;
    } catch (const std::bad_alloc &) {
      failure(receipt, BridgeDisposition::refused, {ErrorCode::allocation});
    } catch (...) {
      failure(receipt, BridgeDisposition::unknown, {ErrorCode::external_unknown});
    }
    if (trace && trace->enabled()) {
      const auto code =
          receipt.has_error ? receipt.error.code : static_cast<ErrorCode>(0);
      const std::int64_t detail = receipt.has_error ? receipt.error.detail : 0;
      receipt.returned = trace->event(
          4096, std::array{raw_native_field(receipt.disposition),
                           raw_native_field(receipt.forward_called),
                           raw_native_field(receipt.has_error), raw_native_field(code),
                           raw_native_field(detail)});
      trace->close();
      if (!receipt.returned.ordinal)
        failure(receipt, BridgeDisposition::unknown, {ErrorCode::audit_unavailable});
    }
    if (interrupted_before_forward)
      return false; // Independent inbox still owns this unattempted envelope.
    preserve(std::move(receipt));
    // Once a forward was attempted, complete this finite owner handoff before
    // honoring stop. Snapshot retirement cannot orphan an unacknowledged entry.
    for (;;) {
      const auto acknowledged = inbox->acknowledge(envelope.key);
      if (acknowledged.has_value())
        break;
      if (acknowledged.error().code != ErrorCode::busy)
        throw acknowledged.error();
      std::this_thread::yield();
    }
    if (capture.error())
      stop.store(true, std::memory_order_release);
    return true;
  }
  void run() noexcept {
    try {
      RawThreadBinding binding{producer};
      RawWaitScope wait{{{stop_wake->read_fd(), -1}, {}}};
      raw_worker_started(dispatch);
      while (!stop.load(std::memory_order_acquire)) {
        refresh();
        auto page = inbox->try_read();
        if (!page.has_value()) {
          if (page.error().code != ErrorCode::busy)
            break;
          pause();
          continue;
        }
        if (!page.value().size) {
          pause();
          continue;
        }
        const auto &entry = page.value().entries.front();
        if (!entry)
          break;
        if (!process(*entry))
          pause();
      }
    } catch (...) {
      // No retry after uncertain publication. Any unacknowledged accepted
      // envelope remains in the independent inbox, with its original key.
    }
    stopped.store(true, std::memory_order_release);
  }
};
NativeActionBridge::NativeActionBridge(std::unique_ptr<State> state)
    : state_(std::move(state)) {}
NativeActionBridge::~NativeActionBridge() = default;
Result<std::unique_ptr<NativeActionBridge>>
NativeActionBridge::create(BridgeScope scope, std::shared_ptr<CommandJobs> commands,
                           std::shared_ptr<ActionInbox> inbox) {
  if (!commands || !inbox)
    return Result<std::unique_ptr<NativeActionBridge>>::failure(
        {ErrorCode::invalid_identity});
  try {
    auto state = std::make_unique<State>(scope, std::move(commands), std::move(inbox));
    auto *owner = state.get();
    state->dispatch = raw_worker_dispatch();
    state->stop_wake->publish();
    state->worker = std::thread{[owner] { owner->run(); }};
    return Result<std::unique_ptr<NativeActionBridge>>::success(
        std::unique_ptr<NativeActionBridge>{new NativeActionBridge(std::move(state))});
  } catch (const Error &error) {
    return Result<std::unique_ptr<NativeActionBridge>>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<std::unique_ptr<NativeActionBridge>>::failure(
        {ErrorCode::allocation});
  } catch (...) {
    return Result<std::unique_ptr<NativeActionBridge>>::failure(
        {ErrorCode::external_unknown});
  }
}
const Text<256> &NativeActionBridge::owner() const noexcept { return state_->owner; }
std::uint64_t NativeActionBridge::incarnation() const noexcept {
  return state_->incarnation;
}
void NativeActionBridge::notify() noexcept { state_->wake.notify_one(); }
BridgeSnapshot NativeActionBridge::try_snapshot() const noexcept {
  BridgeSnapshot result;
  try {
    const std::unique_lock lock{state_->mutex, std::try_to_lock};
    if (!lock.owns_lock()) {
      result.status = BridgeSnapshotStatus::busy;
      return result;
    }
    result.status = BridgeSnapshotStatus::ready;
    result.size = state_->count;
    result.more_before = state_->more_before;
    result.stopped = state_->stopped.load(std::memory_order_acquire);
    for (std::size_t n = 0; n < result.size; ++n)
      result.entries[n] = state_->recent[(state_->head + n) % state_->recent.size()];
  } catch (...) {
    result.status = BridgeSnapshotStatus::unavailable;
  }
  return result;
}
void NativeActionBridge::request_stop() noexcept { state_->request_stop(); }
bool NativeActionBridge::quiescent() const noexcept {
  return state_->stopped.load(std::memory_order_acquire);
}
} // namespace blackbird::tui::lua
