#include "blackbird/context.hpp"
#include "blackbird/tui_lua.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <clocale>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
using namespace blackbird;
namespace ui = blackbird::tui::lua;
namespace {
using Clock = std::chrono::steady_clock;
std::atomic_bool deny_allocations = false, pause_allocation = false;
std::atomic_bool allocation_entered = false, release_allocation = false;
void *allocate(std::size_t size) {
  if (pause_allocation.exchange(false)) {
    allocation_entered = true;
    while (!release_allocation.load())
      std::this_thread::yield();
  }
  if (deny_allocations.load())
    throw std::bad_alloc{};
  if (auto *pointer = std::malloc(size ? size : 1))
    return pointer;
  throw std::bad_alloc{};
}
struct AllocationReset {
  ~AllocationReset() {
    deny_allocations = false;
    release_allocation = true;
    pause_allocation = false;
  }
};
void require(bool value, const char *claim) {
  if (!value)
    throw std::runtime_error{claim};
}
template <class F> void until(F predicate, const char *claim) {
  const auto deadline = Clock::now() + std::chrono::seconds{5};
  while (!predicate()) {
    require(Clock::now() < deadline, claim);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
}
template <class Id> Id id(unsigned byte) {
  IdentityBytes bytes{};
  bytes.front() = static_cast<std::byte>(byte);
  return unwrap(Id::from_bytes(bytes));
}
template <std::size_t N> ui::Text<N> text(std::string_view value) {
  require(value.size() <= N, "private fixture string fits actual public capacity");
  ui::Text<N> result;
  result.size = value.size();
  std::copy(value.begin(), value.end(), result.bytes.begin());
  return result;
}
ui::Dependencies stamp(std::uint64_t sequence = 1) {
  return {
      id<EnvironmentId>(111), id<ConversationId>(112), {3, 1}, 1, sequence, 0, 0, 0};
}
ui::FixedTarget target(std::string_view key = "actual/target") {
  return {text<256>("actual-owner"), text<32>("fixture"), text<128>(key), 7};
}
ui::PresentationInput input(std::uint64_t sequence = 1) {
  ui::PresentationInput result{stamp(sequence)};
  result.row_count = 8;
  result.detail_titles = {text<512>("A_TITLE"), text<512>("B_TITLE")};
  for (std::size_t n = 0; n < 8; ++n) {
    const auto key = "row/" + std::to_string(n);
    result.rows[n] = {text<128>(key), text<512>("label " + std::to_string(n)),
                      target(key)};
  }
  constexpr char left[] = "LEFT\x1b[31m\0END";
  const std::array<std::string_view, 2> windows{
      std::string_view{left, sizeof(left) - 1}, "RIGHT_LITERAL"};
  for (std::size_t n = 0; n < windows.size(); ++n) {
    result.detail_size[n] = windows[n].size();
    std::copy(windows[n].begin(), windows[n].end(),
              reinterpret_cast<char *>(result.detail[n].data()));
  }
  return result;
}
std::shared_ptr<ui::ActionInbox> inbox() {
  IdentityBytes identity{};
  identity.front() = std::byte{113};
  return unwrap(ui::ActionInbox::create(identity));
}
std::shared_ptr<const ui::Script> script(std::string source) {
  return unwrap(ui::Script::create(std::move(source)));
}
constexpr std::string_view valid = R"lua(return {
 compose=function()
  local c=ui.collection('work','OWNED')
  for i=1,ui.rows() do ui.item(c,ui.key(i),ui.label(i),i) end
  ui.inspector('detail',ui.detail_title(1)..' | '..ui.detail_title(2),ui.detail(1)..'\n'..ui.detail(2))
 end,
 interrupt=function() return ui.request('interrupt') end
})lua";
std::unique_ptr<ui::PresentationOwner>
owner(const std::shared_ptr<ui::ActionInbox> &actions, ui::Options options = {}) {
  return unwrap(ui::PresentationOwner::create(actions, options));
}
void submit(ui::PresentationOwner &worker, ui::ComposeRequest request) {
  until(
      [&] {
        const auto status = worker.try_compose(request);
        return status == ui::SubmitStatus::accepted;
      },
      "finite owner compose admission");
}
void submit(ui::PresentationOwner &worker, ui::HandlerRequest request) {
  until(
      [&] {
        const auto status = worker.try_handle(request);
        return status == ui::SubmitStatus::accepted;
      },
      "finite owner handler admission");
}
ui::Outcome take(ui::PresentationOwner &worker,
                 const ui::Dependencies &dependencies = stamp()) {
  ui::Outcome result{dependencies};
  until([&] { return worker.try_take(result) == ui::PollStatus::ready; },
        "finite owner completion");
  return result;
}
std::string detail(const ui::Outcome &result) {
  if (!result.candidate)
    throw std::runtime_error{"candidate required before detail access"};
  std::string shown;
  for (const auto &inspector : result.candidate->inspectors)
    for (const auto &row : inspector.lines) {
      for (const auto &span : row)
        shown += span.text;
      shown += '\n';
    }
  return shown;
}
ui::ActionPage read(const ui::ActionInbox &actions) {
  std::optional<ui::ActionPage> result;
  until(
      [&] {
        auto observed = actions.try_read();
        if (observed.has_value())
          result = observed.value();
        return result.has_value();
      },
      "finite inbox snapshot");
  if (!result)
    throw std::runtime_error{"actual inbox observation"};
  return *result;
}
void semantic() {
  auto actions = inbox();
  auto worker = owner(actions);
  const auto source = script(std::string{valid});
  submit(*worker, {source, input()});
  const auto result = take(*worker);
  require(result.status == ui::Completion::candidate && result.candidate.has_value(),
          "new-contract RED: valid script must produce actual native candidate");
  if (!result.candidate)
    throw std::runtime_error{"actual candidate required"};
  const auto &candidate = *result.candidate;
  require(
      candidate.collections.size() == 1 &&
          candidate.collections.front().items.size() == 8 &&
          candidate.collections.front().items[3].target ==
              std::optional<tui::TargetRef>{{"actual-owner", "fixture", "row/3", 7}} &&
          detail(result).find("LEFT\\x1b[31m\\x00END") != std::string::npos &&
          detail(result).find("RIGHT_LITERAL") != std::string::npos &&
          candidate.inspectors.front().title == "A_TITLE | B_TITLE" &&
          result.handler_count == 1 && result.handlers[0].view() == "interrupt" &&
          !candidate.draft,
      "eight actual input targets and literal two-window detail survive typed "
      "construction");
  auto host = unwrap(tui::SceneHost::create(stamp().view, ui::scene_limits));
  auto stale = stamp();
  ++stale.local_input_revision;
  const auto original = host.snapshot();
  require(!ui::publish(result, stale, host).has_value() && host.snapshot() == original,
          "stale native input revision cannot publish delayed composition");
  unwrap(ui::publish(result, stamp(), host));
  require(host.snapshot()->collections.front().items.size() == 8,
          "compatible candidate publishes through actual SceneHost");
  auto other = stamp();
  ++other.source_generation;
  require(!ui::publish(result, other, host).has_value(),
          "retired source generation cannot attach candidate to replacement");
  require(read(*actions).size == 0, "composition has no action admission");
}
void phases() {
  auto actions = inbox();
  auto worker = owner(actions);
  for (const auto source :
       {"pcall(function() ui.request('bad') end); return {compose=function() "
        "ui.collection('work','x') end}",
        "return {compose=function() pcall(function() ui.request('bad') end); "
        "ui.collection('work','x') end}",
        "return {compose=function() local c=ui.collection('work','x'); "
        "pcall(function() ui.item(c,'bad','bad',9) end) end}"}) {
    submit(*worker, {script(source), input()});
    const auto result = take(*worker);
    require(
        result.status == ui::Completion::native_failure && !result.candidate &&
            read(*actions).size == 0,
        "caught module/compose/constructor failures stay native and admit no action");
  }
  submit(*worker, {script("return {compose=function() pcall(function() "
                          "ui.request('preview') end) end}"),
                   input(), true});
  require(take(*worker).status == ui::Completion::native_failure &&
              read(*actions).size == 0,
          "preview phase cannot request actions");
}
ui::HandlerRequest handler(std::shared_ptr<const ui::Script> source,
                           const ui::Trigger &trigger,
                           std::string_view name = "interrupt",
                           std::string_view key = "frozen") {
  return {std::move(source), stamp(), text<64>(name), target(key), {{}, 19}, trigger};
}
void actions() {
  auto retained = inbox();
  auto worker = owner(retained);
  const auto source = script(std::string{valid});
  const auto trigger = unwrap(retained->new_trigger());
  const auto request = handler(source, trigger);
  submit(*worker, request);
  auto result = take(*worker);
  auto page = read(*retained);
  require(
      result.status == ui::Completion::handler_finished &&
          result.accepted_requests == 1 && page.size == 1 &&
          page.entries[0].has_value(),
      "named handler appends one accepted envelope without claiming domain success");
  const auto &first_entry = page.entries[0];
  if (!first_entry)
    throw std::runtime_error{"accepted actual envelope"};
  const auto accepted = *first_entry;
  require(accepted.target == target("frozen") && accepted.verb.view() == "interrupt" &&
              accepted.key == trigger.key() && accepted.input_origin.ordinal == 19,
          "actual frozen target/key/origin remain owned");
  submit(*worker, request);
  result = take(*worker);
  require(result.status == ui::Completion::handler_finished &&
              read(*retained).size == 1,
          "same native trigger redelivery is idempotent");
  submit(*worker, handler(source, trigger, "interrupt", "changed"));
  result = take(*worker);
  require(result.status == ui::Completion::native_failure &&
              result.native_error.code == ErrorCode::conflict &&
              read(*retained).size == 1,
          "same key changed frozen target conflicts before new acceptance");
  auto fail = script("return {compose=function() end,interrupt=function() "
                     "ui.request('interrupt'); error('later') end}");
  submit(*worker, handler(fail, unwrap(retained->new_trigger())));
  result = take(*worker);
  require(result.status == ui::Completion::script_error &&
              result.accepted_requests == 1 && read(*retained).size == 2,
          "accepted request survives a later actual Lua error");
  for (unsigned n = 2; n < 8; ++n) {
    submit(*worker, handler(source, unwrap(retained->new_trigger())));
    require(take(*worker).status == ui::Completion::handler_finished,
            "finite remaining inbox acceptance");
  }
  submit(*worker, handler(source, unwrap(retained->new_trigger())));
  result = take(*worker);
  require(result.status == ui::Completion::native_failure &&
              result.native_error.code == ErrorCode::capacity &&
              read(*retained).size == 8,
          "inbox saturation refuses without eviction");
  unwrap(retained->acknowledge(accepted.key));
  submit(*worker, request);
  require(take(*worker).status == ui::Completion::handler_finished &&
              read(*retained).size == 7,
          "same retained token after native handoff never re-enqueues accepted action");
  submit(*worker, handler(source, trigger, "interrupt", "changed"));
  require(take(*worker).native_error.code == ErrorCode::conflict &&
              read(*retained).size == 7,
          "changed envelope conflicts even after native acknowledgment");
  std::uint64_t previous_order = 0;
  for (unsigned n = 0; n < 12; ++n) {
    const auto next = unwrap(retained->new_trigger());
    submit(*worker, handler(source, next));
    require(take(*worker).status == ui::Completion::handler_finished,
            "more than eight sequential genuine handoffs progress");
    const auto pending = read(*retained);
    require(pending.size == 8 && pending.entries[7].has_value(),
            "recycled slot has an actual ordered acceptance");
    const auto &last_entry = pending.entries[7];
    if (!last_entry)
      throw std::runtime_error{"pending acceptance required"};
    require(last_entry->key == next.key() &&
                last_entry->acceptance_order > previous_order,
            "recycled slots are projected in actual acceptance order");
    previous_order = last_entry->acceptance_order;
    unwrap(retained->acknowledge(next.key()));
  }
  auto other = owner(inbox());
  require(other->try_handle(request) == ui::SubmitStatus::invalid,
          "caller-reused identity bytes cannot transplant actual native token owner");
  worker.reset();
  require(read(*retained).size == 7,
          "caller-owned pending entries survive actual worker retirement");
}
void budgets() {
  auto actions = inbox();
  auto worker = owner(actions);
  for (const auto source : {"return {compose=function() while true do end end}",
                            "return {compose=function() while true do pcall(function() "
                            "while true do end end) end end}"}) {
    submit(*worker, {script(source), input()});
    const auto result = take(*worker);
    require(result.status == ui::Completion::instruction_budget && !result.candidate &&
                read(*actions).size == 0,
            "ordinary and nested-pcall loops return actual owner Budget");
  }
  const auto scratch = script("leak=(leak or 0)+1; return {compose=function() "
                              "assert(leak==1); ui.collection('work','fresh') end}");
  for (unsigned n = 0; n < 2; ++n) {
    submit(*worker, {scratch, input(n + 1)});
    require(take(*worker).status == ui::Completion::candidate,
            "fresh invocation scratch cannot leak globals across successful calls");
  }
}
void profile() {
  auto worker = owner(inbox());
  const auto source = script(
      R"lua(assert(setmetatable==nil and getmetatable==nil and debug==nil and package==nil and require==nil and load==nil and loadfile==nil and dofile==nil and coroutine==nil and xpcall==nil and print==nil and warn==nil and collectgarbage==nil and table==nil and math==nil)
assert(string.gsub==nil and string.find==nil and string.match==nil and string.gmatch==nil and string.format==nil and string.dump==nil)
assert(('x').gsub==nil and ('x').find==nil and ('x').format==nil)
assert(string.upper('abc')=='ABC' and string.sub('abcdef',2,4)=='bcd' and string.byte('A')==65 and string.char(65,66)=='AB' and string.reverse('ab')=='ba')
return {compose=function() local ok=pcall(function() error('local') end); assert(not ok); local c=ui.collection('work','profile'); ui.item(c,ui.key(1),ui.label(1),1); ui.inspector('detail','literal',ui.detail(1)) end})lua");
  submit(*worker, {source, input()});
  require(take(*worker).status == ui::Completion::candidate,
          "actual initial profile removes finalizer/native/metatable/string-index "
          "bypass paths");
}
void capacity() {
  auto worker = owner(inbox());
  auto source =
      script("return {compose=function() ui.collection('a','x'); "
             "ui.collection('b','x'); ui.collection('c','x'); ui.collection('d','x'); "
             "pcall(function() ui.collection('e','x') end) end}");
  submit(*worker, {source, input()});
  auto result = take(*worker);
  require(result.status == ui::Completion::native_failure &&
              result.native_error.code == ErrorCode::capacity && !result.candidate,
          "caught constructor overcapacity cannot emit partial scene");
  auto value = input();
  for (auto &window : value.detail)
    window.fill(std::byte{'\n'});
  value.detail_size = {4096, 4096};
  submit(*worker, {script(std::string{valid}), value});
  result = take(*worker);
  require(result.status == ui::Completion::candidate && result.candidate &&
              result.candidate->inspectors.front().lines.size() == 8194,
          "two full newline windows fit actual adopted row/span budget");
  for (auto &window : value.detail)
    window.fill(std::byte{0});
  submit(*worker, {script(std::string{valid}), value});
  result = take(*worker);
  require(result.status == ui::Completion::candidate && detail(result).size() == 32770,
          "two full control-byte windows keep exact finite escape expansion");
  require(!ui::Script::create(std::string(ui::source_limit + 1, 'x')).has_value(),
          "source length is checked before VM compilation");
}
void allocations() {
  auto actions = inbox();
  auto tiny = owner(actions, {1, ui::instruction_limit});
  submit(*tiny, {script(std::string{valid}), input()});
  require(take(*tiny).status == ui::Completion::allocation,
          "real Lua allocator refuses VM setup within configured heap");
  auto worker = owner(actions);
  submit(
      *worker,
      {script("return {compose=function() local s='x'; while true do s=s..s end end}"),
       input()});
  require(take(*worker).status == ui::Completion::allocation,
          "real heap growth refusal remains allocation failure");
  const auto source = script(std::string{valid});
  const auto value = input();
  const AllocationReset reset;
  deny_allocations = true;
  submit(*worker, {source, value});
  auto result = take(*worker);
  deny_allocations = false;
  require(result.status == ui::Completion::allocation && !result.candidate,
          "native builder allocation denial is retained without Lua-longjmp owners");
}
void lifecycle() {
  {
    auto idle = owner(inbox());
    idle->request_stop();
    until([&] { return idle->quiescent(); },
          "immediate idle stop cannot lose worker wakeup");
  }
  auto actions = inbox();
  auto worker = owner(actions);
  const auto source = script(std::string{valid});
  // Pause a real native builder allocation after all mailbox request values exist.
  auto first = input(1), second = input(2), latest = input(3);
  const AllocationReset reset;
  allocation_entered = release_allocation = false;
  pause_allocation = true;
  submit(*worker, {source, first});
  until([] { return allocation_entered.load(); },
        "real builder allocation reached off mailbox lock");
  submit(*worker, {source, second});
  submit(*worker, {source, latest});
  release_allocation = true;
  const auto old = take(*worker);
  require(old.status == ui::Completion::retired && old.stamp.request_sequence == 1,
          "superseded active invocation finishes with its original identity");
  const auto next = take(*worker);
  require(next.status == ui::Completion::candidate && next.stamp.request_sequence == 3,
          "only latest unstarted composition survives coalescing");
  // The final Script reference belongs to the active job. Its destruction
  // follows publication of the ready outcome and proves it is left unread.
  auto completed_source = script(std::string{valid});
  const std::weak_ptr<const ui::Script> completed = completed_source;
  auto a = handler(std::move(completed_source), unwrap(actions->new_trigger()));
  a.stamp = stamp(10);
  submit(*worker, std::move(a));
  until([&] { return completed.expired(); },
        "completed handler outcome remains unread after source retirement");
  auto b = handler(source, unwrap(actions->new_trigger()));
  b.stamp = stamp(12);
  auto c = handler(source, unwrap(actions->new_trigger()));
  c.stamp = stamp(13);
  submit(*worker, {source, input(11)});
  submit(*worker, b);
  submit(*worker, c);
  const auto first_handler = take(*worker);
  require(first_handler.status == ui::Completion::handler_finished &&
              first_handler.stamp.request_sequence == 10,
          "unread completed discrete outcome survives later compose/handler admission");
  std::array<std::uint64_t, 3> delivered{};
  for (std::size_t n = 0; n < delivered.size(); ++n) {
    const auto result = take(*worker);
    delivered[n] = result.stamp.request_sequence;
    require(result.status == (delivered[n] == 11 ? ui::Completion::candidate
                                                 : ui::Completion::handler_finished),
            "distinct pending source retains actual completion kind");
  }
  require(std::count(delivered.begin(), delivered.end(), 11) == 1 &&
              std::count(delivered.begin(), delivered.end(), 12) == 1 &&
              std::count(delivered.begin(), delivered.end(), 13) == 1 &&
              std::find(delivered.begin(), delivered.end(), 12) <
                  std::find(delivered.begin(), delivered.end(), 13) &&
              read(*actions).size == 3,
          "distinct handler successors remain FIFO beside independent composition");
  // Stop with one actual callback in flight and two discrete requests pending.
  const auto gated =
      script("return {compose=function() end,interrupt=function() "
             "ui.collection('gate','gate'); ui.request('interrupt') end}");
  auto active = handler(gated, unwrap(actions->new_trigger()));
  active.stamp = stamp(21);
  auto pending_b = handler(source, unwrap(actions->new_trigger()));
  pending_b.stamp = stamp(22);
  auto pending_c = handler(source, unwrap(actions->new_trigger()));
  pending_c.stamp = stamp(23);
  allocation_entered = release_allocation = false;
  pause_allocation = true;
  submit(*worker, active);
  until([] { return allocation_entered.load(); },
        "actual active handler builder pauses before stop");
  submit(*worker, pending_b);
  submit(*worker, pending_c);
  worker->request_stop();
  release_allocation = true;
  until([&] { return worker->quiescent(); }, "actual worker quiescence before join");
  for (std::uint64_t sequence = 21; sequence <= 23; ++sequence) {
    const auto result = take(*worker);
    require(result.status == ui::Completion::stopped &&
                result.stamp.request_sequence == sequence && !result.accepted_requests,
            "active and pending stopped discrete outcomes remain exactly stamped and "
            "drainable");
  }
  require(worker->try_compose({source, input(24)}) == ui::SubmitStatus::stopped,
          "stop rejects new composition admission");
  worker.reset();
  require(read(*actions).size == 3,
          "actual joined owner cannot retire accepted inbox facts");
}
} // namespace
void *operator new(std::size_t size) { return allocate(size); }
void *operator new[](std::size_t size) { return allocate(size); }
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { std::free(pointer); }
int main(int argc, char **argv) {
  try {
    require(std::setlocale(LC_ALL, "") != nullptr, "actual text locale");
    require(argc == 2,
            "select "
            "semantic/phases/actions/budgets/profile/capacity/allocations/lifecycle");
    const std::string_view mode = argv[1];
    if (mode == "semantic")
      semantic();
    else if (mode == "phases")
      phases();
    else if (mode == "actions")
      actions();
    else if (mode == "budgets")
      budgets();
    else if (mode == "profile")
      profile();
    else if (mode == "capacity")
      capacity();
    else if (mode == "allocations")
      allocations();
    else if (mode == "lifecycle")
      lifecycle();
    else
      require(false, "known owner oracle");
    std::cout << mode << ": native Lua owner assertions passed\n";
  } catch (const Error &error) {
    deny_allocations = false;
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
    return 1;
  } catch (const std::exception &error) {
    deny_allocations = false;
    release_allocation = true;
    std::cerr << error.what() << '\n';
    return 1;
  }
}
