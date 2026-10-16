#include "blackbird/context.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/tui_action_bridge.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace ui = blackbird::tui::lua;
namespace {
using Path = std::filesystem::path;
void check(bool value, const char *claim) {
  if (!value)
    throw std::runtime_error{claim};
}
template <class F> void until(F predicate, const char *claim) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!predicate()) {
    check(std::chrono::steady_clock::now() < end, claim);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
}
template <class T>
std::optional<T> inbox_result(Result<T> result, const char *operation) {
  if (result.has_value())
    return std::move(result).value();
  const auto error = result.error();
  if (error.code == ErrorCode::busy)
    return std::nullopt;
  throw std::runtime_error{std::string{operation} + ": native " +
                           std::string{error_name(error.code)} +
                           " detail=" + std::to_string(error.detail)};
}
template <class T, class F> T await_inbox(F attempt, const char *operation) {
  std::optional<T> ready;
  until(
      [&] {
        ready = inbox_result(attempt(), operation);
        return ready.has_value();
      },
      operation);
  if (!ready)
    throw std::runtime_error{"inbox polling completed without a result"};
  return std::move(*ready);
}
template <class Id> Id id(unsigned first) {
  IdentityBytes bytes{};
  bytes.front() = static_cast<std::byte>(first);
  return unwrap(Id::from_bytes(bytes));
}
template <std::size_t N> ui::Text<N> text(std::string_view value) {
  check(value.size() <= N, "fixture text fits actual native capacity");
  ui::Text<N> result;
  result.size = value.size();
  std::copy(value.begin(), value.end(), result.bytes.begin());
  return result;
}
ui::BridgeScope scope() { return {id<EnvironmentId>(81), id<ConversationId>(82)}; }
std::string owner(const ui::BridgeScope &value) {
  return hex_identity(value.environment.bytes()) + "/" +
         hex_identity(value.conversation.bytes());
}
std::string request_id(const ui::ActionKey &key) {
  std::ostringstream out;
  out << "bb-ui:" << std::hex << std::setfill('0');
  for (const auto byte : key.inbox)
    out << std::setw(2) << std::to_integer<unsigned>(byte);
  out << ':' << std::setw(16) << key.owner << ':' << std::setw(16) << key.trigger << ':'
      << std::setw(8) << key.ordinal;
  return out.str();
}
struct Directory {
  Path path;
  Directory() {
    char name[] = "/tmp/blackbird-bridge-XXXXXX";
    check(::mkdtemp(name) != nullptr, "private bridge directory");
    path = name;
  }
  ~Directory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
struct Fixture {
  std::shared_ptr<CommandJobs> jobs = std::make_shared<CommandJobs>();
  std::string key = std::string(32, 'a');
  CommandObservationId job;
  explicit Fixture(const Path &executable) {
    job.bytes.fill('a');
    jobs->start(key,
                Value::object({{"argv", Value{Value::Array{Value{executable.string()},
                                                           Value{"--child"}}}},
                               {"timeout_seconds", Value{Number{20}}}}));
    bool output = false;
    until(
        [&] {
          for (const auto &event : jobs->drain())
            if (event.bytes) {
              output = *event.bytes == "R";
              jobs->retained(event.job_id, event.offset + event.bytes->size());
            }
          return output;
        },
        "actual bridge fixture child ready");
  }
  ~Fixture() { jobs->shutdown(); }
  ui::FixedTarget target() const {
    const auto query = jobs->try_observe_delivery({0, job, "bootstrap"});
    check(query.incarnation != 0, "actual process owner incarnation available");
    return {text<256>(owner(scope())), text<32>("command"), text<128>(key),
            query.incarnation};
  }
  void finish() {
    (void)jobs->control(Value::object({{"op", Value{"stop"}}, {"job_id", Value{key}}}));
    bool terminal = false;
    until(
        [&] {
          for (const auto &event : jobs->drain())
            if (event.bytes)
              jobs->retained(event.job_id, event.offset + event.bytes->size());
            else {
              jobs->settled(event.job_id, event.result);
              terminal = true;
            }
          return terminal;
        },
        "actual target exits and retained terminal settles");
  }
};
struct Producer {
  std::shared_ptr<ui::ActionInbox> inbox;
  std::unique_ptr<ui::PresentationOwner> vm;
  ui::Dependencies stamp{
      scope().environment, scope().conversation, {5, 1}, 1, 1, 0, 0, 0};
  Producer() {
    IdentityBytes identity{};
    identity.front() = std::byte{83};
    inbox = unwrap(ui::ActionInbox::create(identity));
    vm = unwrap(ui::PresentationOwner::create(inbox));
  }
  void submit(const ui::Trigger &trigger, const ui::FixedTarget &target,
              std::string_view verb = "interrupt") {
    const auto source = unwrap(ui::Script::create(
        verb == "pair"        ? "return {compose=function() end,interrupt=function() "
                                "ui.request('interrupt'); ui.request('interrupt') end}"
        : verb == "interrupt" ? "return {compose=function() end,interrupt=function() "
                                "ui.request('interrupt') end}"
                              : "return {compose=function() end,interrupt=function() "
                                "ui.request('unsupported') end}"));
    const ui::HandlerRequest request{source, stamp, text<64>("interrupt"),
                                     target, {},    trigger};
    until([&] { return vm->try_handle(request) == ui::SubmitStatus::accepted; },
          "actual Lua handler admitted");
    ui::Outcome result{stamp};
    until([&] { return vm->try_take(result) == ui::PollStatus::ready; },
          "actual Lua handler completed");
    check(result.status == ui::Completion::handler_finished &&
              result.accepted_requests == (verb == "pair" ? 2U : 1U),
          "actual handler accepts its finite immutable native envelopes");
  }
  ui::Trigger accept(const ui::FixedTarget &target,
                     std::string_view verb = "interrupt") {
    // A busy mint has not advanced the native trigger counter or accepted input.
    const auto trigger = await_inbox<ui::Trigger>(
        [&] { return inbox->new_trigger(); }, "ActionInbox::new_trigger stays busy");
    submit(trigger, target, verb);
    return trigger;
  }
  ui::ActionPage read(const char *operation) const {
    return await_inbox<ui::ActionPage>([&] { return inbox->try_read(); }, operation);
  }
  bool empty(const char *operation) const {
    const auto page = inbox_result(inbox->try_read(), operation);
    if (!page)
      return false;
    return page->size == 0;
  }
};
std::unique_ptr<ui::NativeActionBridge> bridge(Fixture &fixture, Producer &producer) {
  auto result = ui::NativeActionBridge::create(scope(), fixture.jobs, producer.inbox);
  check(result.has_value(), "native bridge admits actual accepted inbox and runtime");
  return std::move(result).value();
}
ui::BridgeSnapshot snapshot(const ui::NativeActionBridge &bridge, std::size_t count) {
  ui::BridgeSnapshot result;
  until(
      [&] {
        result = bridge.try_snapshot();
        return result.status == ui::BridgeSnapshotStatus::ready && result.size >= count;
      },
      "actual bridge publishes finite recent receipt");
  return result;
}
const ui::BridgeReceipt &last(const ui::BridgeSnapshot &value) {
  check(value.size && value.size <= value.entries.size(),
        "actual recent receipt exists");
  const auto &entry = value.entries[value.size - 1];
  if (!entry)
    throw std::runtime_error{"actual occupied receipt slot"};
  return *entry;
}
void handoff(const Path &executable) {
  Fixture fixture{executable};
  Producer producer;
  const auto target = fixture.target();
  const auto trigger = producer.accept(target);
  const auto pending = producer.read("ActionInbox::try_read before bridge creation");
  check(pending.size == 1, "real inbox accepted before refusing-scaffold RED");
  auto consumer = bridge(fixture, producer);
  check(consumer->owner().view() == owner(scope()) &&
            consumer->incarnation() == target.incarnation,
        "bridge binds actual typed scope and physical process owner before input");
  auto receipt = snapshot(*consumer, 1);
  check(last(receipt).envelope.key == trigger.key() &&
            last(receipt).request_id.view() == request_id(trigger.key()) &&
            last(receipt).disposition == ui::BridgeDisposition::admitted &&
            last(receipt).forward_called,
        "exact whole native key maps to one actual mailbox admission");
  until([&] { return producer.empty("ActionInbox::try_read acknowledgment"); },
        "bridge acknowledges only after preserving actual receipt");
  const auto selected = fixture.jobs->requests();
  check(selected.size() == 1 &&
            string_field(selected.front(), "job_id") == fixture.key &&
            string_field(selected.front(), "request_id") == request_id(trigger.key()) &&
            string_field(selected.front(), "signal") == "interrupt",
        "actual mailbox receives frozen target and exact derived request once");
  // Destroy the originating VM, then redeliver its original native token.
  producer.vm.reset();
  producer.vm = unwrap(ui::PresentationOwner::create(producer.inbox));
  producer.submit(trigger, target);
  check(producer.read("ActionInbox::try_read after token replay").size == 0 &&
            fixture.jobs->requests().empty(),
        "same token after acknowledgment/reload cannot resend the domain request");
  (void)fixture.jobs->control(selected.front());
  until(
      [&] {
        receipt = snapshot(*consumer, 1);
        const auto &entry = last(receipt);
        return entry.delivery_known &&
               entry.delivery_status == CommandDeliveryStatus::ready &&
               entry.delivery.stage == CommandDeliveryStage::consumed;
      },
      "bridge reconciles actual worker consumption using original key");
  check(last(receipt).delivery.call_count == 2 && fixture.jobs->requests().empty(),
        "receipt refresh observes actual native signal calls without dispatch");
  producer.submit(trigger, target, "pair");
  receipt = snapshot(*consumer, 2);
  check(last(receipt).envelope.key == trigger.key(1) &&
            last(receipt).request_id.view() == request_id(trigger.key(1)),
        "second operation ordinal in one trigger has a distinct whole-key identity");
  const auto ordinal = fixture.jobs->requests();
  check(ordinal.size() == 1 &&
            string_field(ordinal.front(), "request_id") == request_id(trigger.key(1)),
        "old ordinal remains deduplicated while new ordinal forwards once");
  const auto next = producer.accept(target);
  receipt = snapshot(*consumer, 3);
  check(next.key() != trigger.key() && last(receipt).envelope.key == next.key() &&
            last(receipt).request_id.view() != request_id(trigger.key()),
        "two deliberate triggers remain distinct domain invocations");
  check(fixture.jobs->requests().size() == 1, "fresh trigger forwards exactly once");
}
void reject(const Path &executable) {
  Fixture fixture{executable};
  Producer producer;
  auto consumer = bridge(fixture, producer);
  const auto actual = fixture.target();
  auto changed = actual;
  changed.owner = text<256>("unrelated-owner");
  (void)producer.accept(changed);
  changed = actual;
  ++changed.incarnation;
  (void)producer.accept(changed);
  changed = actual;
  changed.key = text<128>(std::string(32, 'b'));
  (void)producer.accept(changed);
  changed = actual;
  changed.kind = text<32>("participant");
  (void)producer.accept(changed);
  (void)producer.accept(actual, "unsupported");
  producer.stamp.conversation = id<ConversationId>(84);
  (void)producer.accept(actual);
  auto result = snapshot(*consumer, 6);
  for (std::size_t n = 0; n < result.size; ++n) {
    const auto &entry = result.entries[n];
    check(entry && entry->disposition == ui::BridgeDisposition::refused &&
              !entry->forward_called && entry->has_error,
          "wrong owner/incarnation/target/kind/verb/scope refuses before mailbox");
  }
  check(fixture.jobs->requests().empty(),
        "rejected action never falls back to live neighbor");
  fixture.finish();
  producer.stamp.conversation = scope().conversation;
  (void)producer.accept(actual);
  result = snapshot(*consumer, 7);
  check(last(result).disposition == ui::BridgeDisposition::refused &&
            !last(result).forward_called && fixture.jobs->requests().empty(),
        "actually exited target refuses instead of selecting replacement");
}
void recent(const Path &executable) {
  Fixture fixture{executable};
  Producer producer;
  auto consumer = bridge(fixture, producer);
  const auto target = fixture.target();
  std::vector<ui::Trigger> triggers;
  for (std::size_t n = 0; n < 20; ++n) {
    triggers.push_back(producer.accept(target));
    until([&] { return producer.empty("ActionInbox::try_read recent acknowledgment"); },
          "each admitted receipt preserved before accepting successor");
  }
  const auto result = snapshot(*consumer, 16);
  check(result.size == 16 && result.more_before &&
            last(result).envelope.key == triggers.back().key() &&
            result.entries.front() &&
            result.entries.front()->envelope.key == triggers[4].key(),
        "recent16 explicitly retires oldest derived rows in acceptance order");
  producer.submit(triggers.front(), target);
  check(producer.read("ActionInbox::try_read retired token replay").size == 0,
        "retired cache row never authorizes original token replay");
  const auto selected = fixture.jobs->requests();
  check(selected.size() == 20, "exactly twenty actual distinct mailbox handoffs");
  for (std::size_t n = 0; n < selected.size(); ++n)
    check(string_field(selected[n], "request_id") == request_id(triggers[n].key()),
          "whole-key identity and native acceptance order survive cache retirement");
}
void stop(const Path &executable) {
  Fixture fixture{executable};
  Producer producer;
  auto consumer = bridge(fixture, producer);
  consumer->request_stop();
  until([&] { return consumer->quiescent(); }, "cooperative native worker stop joins");
  (void)producer.accept(fixture.target());
  const auto state = consumer->try_snapshot();
  check(state.status == ui::BridgeSnapshotStatus::ready && state.stopped &&
            state.size == 0 && fixture.jobs->requests().empty(),
        "stopped bridge cannot dispatch a later accepted action");
  consumer.reset();
  check(producer.read("ActionInbox::try_read after bridge destruction").size == 1,
        "bridge destruction retains independently owned undispatched envelope");
}
template <class T> T take(ByteView &bytes) {
  check(bytes.size() >= sizeof(T), "independent raw native field complete");
  T result{};
  std::memcpy(&result, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return result;
}
std::string_view take_text(ByteView &bytes) {
  const auto size = take<std::size_t>(bytes);
  check(size <= bytes.size(), "independent raw text extent complete");
  const std::string_view result{reinterpret_cast<const char *>(bytes.data()), size};
  bytes = bytes.subspan(size);
  return result;
}
RawEventRef take_ref(ByteView &bytes) {
  return {take<IdentityBytes>(bytes), take<std::uint64_t>(bytes)};
}
bool same(const RawEventRef &a, const RawEventRef &b) {
  return a.lane == b.lane && a.ordinal == b.ordinal;
}
void raw(const Path &executable) {
  Directory directory;
  auto runtime = unwrap(RawRuntime::open(directory.path.string(), scope().environment));
  std::optional<RawThreadBinding> binding;
  binding.emplace(unwrap(runtime->prepare_thread()));
  std::optional<ui::BridgeReceipt> receipt;
  {
    Fixture fixture{executable};
    Producer producer;
    const auto target = fixture.target();
    (void)producer.accept(target);
    auto consumer = bridge(fixture, producer);
    receipt = last(snapshot(*consumer, 1));
    const auto selected = fixture.jobs->requests();
    check(selected.size() == 1, "actual raw bridge forwards one command request");
    (void)fixture.jobs->control(selected.front());
    until(
        [&] {
          receipt = last(snapshot(*consumer, 1));
          return receipt->delivery_known &&
                 receipt->delivery.stage == CommandDeliveryStage::consumed;
        },
        "actual raw native receipt reconciles without mailbox replay");
  }
  binding.reset();
  runtime.reset();
  check(receipt.has_value(), "actual raw receipt preserved after owner retirement");
  const auto &observed = *receipt;
  const auto runs = unwrap(discover_raw_runs(directory.path.string()));
  check(runs.size() == 1, "one genuine private raw bridge run");
  auto reader =
      unwrap(RawRunReader::open((directory.path / "raw" / runs.front().name).string()));
  std::vector<std::byte> storage(1024 * 1024);
  bool forward = false, returned = false, admitted = false;
  std::size_t forwards = 0;
  std::map<std::pair<IdentityBytes, std::uint64_t>, std::uint32_t> sites;
  std::map<IdentityBytes, RawEventRef> starts;
  while (const auto record = unwrap(reader->read(storage))) {
    auto bytes = record->raw;
    (void)take<std::uint32_t>(bytes);
    const auto site = take<std::uint32_t>(bytes);
    const RawEventRef reference{record->origin.lane, take<std::uint64_t>(bytes)};
    sites.emplace(std::pair{reference.lane, reference.ordinal}, site);
    if (site == 1101) {
      starts.emplace(reference.lane, take_ref(bytes));
    } else if (site == 4095) {
      ++forwards;
      check(same(reference, observed.forward) &&
                take<IdentityBytes>(bytes) == observed.envelope.key.inbox &&
                take<std::uint64_t>(bytes) == observed.envelope.key.owner &&
                take<std::uint64_t>(bytes) == observed.envelope.key.trigger &&
                take<std::uint32_t>(bytes) == observed.envelope.key.ordinal &&
                take<IdentityBytes>(bytes) == scope().environment.bytes() &&
                take<IdentityBytes>(bytes) == scope().conversation.bytes(),
            "forward original contains entire actual invocation key and scope");
      const auto &stamp = observed.envelope.stamp;
      for (const auto expected :
           {stamp.view.owner, stamp.view.incarnation, stamp.source_generation,
            stamp.request_sequence, stamp.scene_revision, stamp.local_input_revision,
            stamp.query_revision, observed.envelope.acceptance_order})
        check(take<std::uint64_t>(bytes) == expected,
              "exact accepted dependency originals");
      check(same(take_ref(bytes), observed.envelope.input_origin) &&
                take_text(bytes) == owner(scope()) && take_text(bytes) == "command" &&
                take_text(bytes) == std::string(32, 'a') &&
                take<std::uint64_t>(bytes) == observed.envelope.target.incarnation &&
                take_text(bytes) == "interrupt" && take_text(bytes) == "interrupt" &&
                take_text(bytes) == observed.request_id.view() &&
                take_text(bytes) == "signal" && take_text(bytes) == "interrupt" &&
                bytes.empty(),
            "raw forward preserves actual envelope and exact process arguments");
      forward = true;
    } else if (site == 4096) {
      check(same(reference, observed.returned) &&
                same(take_ref(bytes), observed.forward) &&
                take<ui::BridgeDisposition>(bytes) == ui::BridgeDisposition::admitted &&
                take<bool>(bytes) && !take<bool>(bytes),
            "native return resolves the actual one-time forward and acceptance");
      (void)take<ErrorCode>(bytes);
      (void)take<std::int64_t>(bytes);
      check(bytes.empty(), "fixed native return slots decode independently");
      returned = true;
    } else if (site == 2013 && same(reference, observed.delivery.admission)) {
      check(take_text(bytes) == std::string(32, 'a') && take_text(bytes) == "signal" &&
                take_text(bytes) == observed.request_id.view(),
            "bridge-derived request names actual CommandJobs original admission");
      admitted = true;
    }
  }
  check(forward && returned && admitted && forwards == 1,
        "actual raw bridge and domain links exist once without observation replay");
  const auto started = starts.find(observed.forward.lane);
  check(started != starts.end() &&
            sites.contains({started->second.lane, started->second.ordinal}) &&
            sites.at({started->second.lane, started->second.ordinal}) == 1100,
        "actual forwarding worker start links to its pre-spawn native dispatcher");
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view{argv[1]} == "--child") {
      (void)::signal(SIGINT, SIG_IGN);
      if (::write(STDOUT_FILENO, "R", 1) != 1)
        return 91;
      for (;;)
        (void)::pause();
    }
    check(argc == 2, "one native bridge case");
    const std::string_view mode{argv[1]};
    const auto executable = std::filesystem::absolute(argv[0]);
    if (mode == "handoff")
      handoff(executable);
    else if (mode == "reject")
      reject(executable);
    else if (mode == "recent")
      recent(executable);
    else if (mode == "stop")
      stop(executable);
    else if (mode == "raw")
      raw(executable);
    else
      throw std::runtime_error{"unknown bridge case"};
    std::cout << "PASS " << mode << '\n';
    return 0;
  } catch (const Error &error) {
    std::cerr << "FAIL native " << error_name(error.code) << " detail=" << error.detail
              << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
