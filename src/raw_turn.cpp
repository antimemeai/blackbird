#include "blackbird/raw_turn.hpp"
#include <algorithm>
#include <cerrno>
namespace blackbird {
namespace {
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
std::optional<RawControlToken> claim(std::uint64_t bytes) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  auto result = raw_claim_control(bytes);
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
auto clocks(const RawClockSample &clock) noexcept {
  return std::array{detail::raw_native_field(clock.monotonic.method),
                    detail::raw_native_field(clock.monotonic.result),
                    detail::raw_native_field(clock.monotonic.error),
                    detail::raw_native_field(clock.monotonic.value.tv_sec),
                    detail::raw_native_field(clock.monotonic.value.tv_nsec),
                    detail::raw_native_field(clock.cpu_read),
                    detail::raw_native_field(clock.thread_cpu.method),
                    detail::raw_native_field(clock.thread_cpu.result),
                    detail::raw_native_field(clock.thread_cpu.error),
                    detail::raw_native_field(clock.thread_cpu.value.tv_sec),
                    detail::raw_native_field(clock.thread_cpu.value.tv_nsec)};
}
RawEventRef publish(std::uint32_t site, std::span<const ByteView> fields,
                    RawCapacityClaim *credit, RawPartition partition) noexcept {
  const ErrnoGuard guard;
  if (!credit)
    return {};
  const auto result = raw_emit(site, fields, partition, credit);
  if (!result.has_value()) {
    raw_fence(result.error());
    return {};
  }
  return {raw_lane_epoch(), result.value()};
}
RawEventRef linked(std::uint32_t site, RawEventRef parent,
                   std::span<const ByteView> fields, RawCapacityClaim *credit,
                   RawPartition partition) noexcept {
  std::array<ByteView, 24> body{};
  if (fields.size() > body.size() - 2) {
    raw_fence({ErrorCode::capacity});
    return {};
  }
  body[0] = parent.lane;
  body[1] = detail::raw_native_field(parent.ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  return publish(site, std::span{body}.first(fields.size() + 2), credit, partition);
}
RawEventRef dynamic(std::uint32_t site, std::span<const ByteView> fields) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  std::uint64_t bytes = raw_envelope_size;
  for (const auto field : fields) {
    if (field.size() > UINT32_MAX - bytes)
      throw Error{ErrorCode::overflow};
    bytes += field.size();
  }
  auto result = raw_claim_data(bytes);
  if (!result.has_value())
    throw result.error();
  return publish(site, fields, &result.value(), RawPartition::data);
}
} // namespace
RawTaskActivityTrace::RawTaskActivityTrace() : credit_(claim(349)) {}
void RawTaskActivityTrace::binding(RawEventRef operation, OperationAttemptId attempt,
                                   std::string_view task) noexcept {
  const auto length = task.size();
  const std::array fields{ByteView{operation.lane},
                          detail::raw_native_field(operation.ordinal),
                          ByteView{attempt.bytes()}, detail::raw_native_field(length),
                          detail::raw_string_field(task)};
  binding_ =
      publish(1915, fields, credit_ ? &*credit_ : nullptr, RawPartition::control);
}
RawEventRef RawTaskActivityTrace::activity(std::uint8_t branch) noexcept {
  const std::array fields{detail::raw_native_field(branch)};
  return linked(1916, binding_, fields, credit_ ? &*credit_ : nullptr,
                RawPartition::control);
}
RawEventRef RawTaskActivityTrace::notification(RawEventRef activity,
                                               bool present) noexcept {
  const std::array fields{detail::raw_native_field(present)};
  return linked(1920, activity, fields, credit_ ? &*credit_ : nullptr,
                RawPartition::control);
}
void RawTaskActivityTrace::notified(RawEventRef notification, std::uint8_t branch,
                                    std::optional<Error> error) noexcept {
  std::array<ByteView, 3> fields{};
  fields[0] = detail::raw_native_field(branch);
  std::size_t count = 1;
  if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  (void)linked(1917, notification, std::span{fields}.first(count),
               credit_ ? &*credit_ : nullptr, RawPartition::control);
}
RawPolicyTrace::RawPolicyTrace(std::uint8_t method, std::string_view revision,
                               std::uint64_t credit, std::optional<RawEventRef> parent,
                               RawControlToken *bank) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return;
  auto held = bank ? bank->split(credit) : raw_claim_control(credit);
  if (!held.has_value())
    throw held.error();
  credit_.emplace(std::move(held).value());
  const bool present = parent.has_value();
  const auto length = revision.size();
  std::array<ByteView, 6> fields{};
  std::size_t count = 0;
  fields[count++] = detail::raw_native_field(method);
  fields[count++] = detail::raw_native_field(present);
  if (parent) {
    fields[count++] = parent->lane;
    fields[count++] = detail::raw_native_field(parent->ordinal);
  }
  fields[count++] = detail::raw_native_field(length);
  fields[count++] = detail::raw_string_field(revision);
  request_ = dynamic(1907, std::span{fields}.first(count));
}
RawPolicyTrace::~RawPolicyTrace() { finish(4); }
void RawPolicyTrace::event(std::uint32_t site,
                           std::span<const ByteView> fields) noexcept {
  (void)linked(site, request_, fields, credit_ ? &*credit_ : nullptr,
               RawPartition::control);
}
void RawPolicyTrace::validation(const RawPolicyFields &values) noexcept {
  std::array<ByteView, 6> fields{};
  const bool enabled = values.enabled.has_value(), target = values.target.has_value(),
             trigger = values.trigger.has_value();
  std::size_t count = 0;
  fields[count++] = detail::raw_native_field(enabled);
  if (values.enabled)
    fields[count++] = detail::raw_native_field(*values.enabled);
  fields[count++] = detail::raw_native_field(target);
  if (values.target)
    fields[count++] = detail::raw_native_field(*values.target);
  fields[count++] = detail::raw_native_field(trigger);
  if (values.trigger)
    fields[count++] = detail::raw_native_field(*values.trigger);
  event(1908, std::span{fields}.first(count));
}
void RawPolicyTrace::observation(bool enabled, std::optional<std::size_t> trigger,
                                 std::size_t input, bool triggered) noexcept {
  const bool present = trigger.has_value();
  std::array<ByteView, 5> fields{};
  std::size_t count = 0;
  fields[count++] = detail::raw_native_field(enabled);
  fields[count++] = detail::raw_native_field(present);
  if (trigger)
    fields[count++] = detail::raw_native_field(*trigger);
  fields[count++] = detail::raw_native_field(input);
  fields[count++] = detail::raw_native_field(triggered);
  event(1908, std::span{fields}.first(count));
}
void RawPolicyTrace::staged(std::string_view revision) noexcept {
  const auto length = revision.size();
  const std::array fields{detail::raw_native_field(length),
                          detail::raw_string_field(revision)};
  event(1909, fields);
}
void RawPolicyTrace::finish(std::uint8_t branch, std::optional<Error> error) noexcept {
  if (finished_)
    return;
  std::array<ByteView, 3> fields{};
  fields[0] = detail::raw_native_field(branch);
  std::size_t count = 1;
  if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  event(1910, std::span{fields}.first(count));
  finished_ = true;
}
RawProtocolTrace::RawProtocolTrace(std::size_t rows, std::optional<RawEventRef> parent)
    : credit_(claim(parent ? 99 : 75)) {
  if (!credit_)
    return;
  const bool present = parent.has_value();
  std::array<ByteView, 4> fields{};
  std::size_t count = 0;
  fields[count++] = detail::raw_native_field(present);
  if (parent) {
    fields[count++] = parent->lane;
    fields[count++] = detail::raw_native_field(parent->ordinal);
  }
  fields[count++] = detail::raw_native_field(rows);
  request_ =
      publish(1911, std::span{fields}.first(count), &*credit_, RawPartition::control);
}
RawProtocolTrace::~RawProtocolTrace() { finish(3); }
void RawProtocolTrace::conflict(std::string_view call) {
  if (!credit_)
    return;
  const auto length = call.size();
  const std::array fields{
      ByteView{request_.lane}, detail::raw_native_field(request_.ordinal),
      detail::raw_native_field(length), detail::raw_string_field(call)};
  (void)dynamic(1913, fields);
}
void RawProtocolTrace::finish(std::uint8_t branch,
                              std::optional<Error> error) noexcept {
  if (finished_)
    return;
  std::array<ByteView, 3> fields{};
  fields[0] = detail::raw_native_field(branch);
  std::size_t count = 1;
  if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  (void)linked(1912, request_, std::span{fields}.first(count),
               credit_ ? &*credit_ : nullptr, RawPartition::control);
  finished_ = true;
}
RawTurnTrace::RawTurnTrace(bool running, bool backstop,
                           DefinitionGenerationId generation, std::string_view head)
    : credit_(claim(10544)) {
  const ErrnoGuard guard;
  if (!credit_)
    return;
  auto semantic = credit_->split(2286);
  if (!semantic.has_value())
    throw semantic.error();
  context_.emplace(std::move(semantic).value());
  auto regions = credit_->split(6912);
  if (!regions.has_value())
    throw regions.error();
  regions_.emplace(std::move(regions).value());
  const auto length = head.size();
  const auto clock = raw_local_clock(true);
  const auto readings = clocks(clock);
  std::array<ByteView, 16> fields{};
  fields[0] = detail::raw_native_field(running);
  fields[1] = detail::raw_native_field(backstop);
  fields[2] = generation.bytes();
  fields[3] = detail::raw_native_field(length);
  fields[4] = detail::raw_string_field(head);
  std::copy(readings.begin(), readings.end(), fields.begin() + 5);
  request_ = publish(1900, fields, &*credit_, RawPartition::control);
}
RawTurnTrace::~RawTurnTrace() { finish(4, 0); }
RawControlToken *RawTurnTrace::context_bank() noexcept {
  return context_ ? &*context_ : nullptr;
}
RawControlToken *RawTurnTrace::region_bank() noexcept {
  return regions_ ? &*regions_ : nullptr;
}
void RawTurnTrace::event(std::uint32_t site, RawEventRef parent,
                         std::span<const ByteView> fields) noexcept {
  if (!credit_)
    return;
  std::array<ByteView, 24> body{};
  if (fields.size() > body.size() - 2) {
    raw_fence({ErrorCode::capacity});
    return;
  }
  body[0] = parent.lane;
  body[1] = detail::raw_native_field(parent.ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  (void)publish(site, std::span{body}.first(fields.size() + 2), &*credit_,
                RawPartition::control);
}
void RawTurnTrace::admission(bool existed, JournalCapacity limits,
                             JournalCursor cursor) noexcept {
  const std::array fields{detail::raw_native_field(existed),
                          detail::raw_native_field(limits.max_file_bytes),
                          detail::raw_native_field(limits.max_records),
                          ByteView{cursor.journal.bytes()},
                          detail::raw_native_field(cursor.sequence),
                          detail::raw_native_field(cursor.end_offset)};
  event(1901, request_, fields);
}
void RawTurnTrace::state(std::uint8_t stage, const RawTurnState &state) noexcept {
  const std::array fields{detail::raw_native_field(stage),
                          detail::raw_native_field(state.running),
                          detail::raw_native_field(state.backstop),
                          detail::raw_native_field(state.capacity),
                          detail::raw_native_field(state.budget),
                          detail::raw_native_field(state.program),
                          detail::raw_native_field(state.tools),
                          detail::raw_native_field(state.workflows),
                          detail::raw_native_field(state.workflow_depth),
                          detail::raw_native_field(state.workflow_invocations),
                          detail::raw_native_field(state.operation_depth),
                          detail::raw_native_field(state.unretained_bytes)};
  event(1904, request_, fields);
}
void RawTurnTrace::activation(std::uint8_t kind, std::string_view revision) noexcept {
  const auto length = revision.size();
  const std::array fields{detail::raw_native_field(kind),
                          detail::raw_native_field(length),
                          detail::raw_string_field(revision)};
  event(1905, request_, fields);
}
RawEventRef RawTurnTrace::source_request(DefinitionGenerationId generation,
                                         std::string_view source) {
  const ErrnoGuard guard;
  if (!credit_)
    return {};
  if (source.size() > UINT32_MAX - 64)
    throw Error{ErrorCode::overflow};
  auto data = raw_claim_data(64 + source.size());
  if (!data.has_value())
    throw data.error();
  const auto length = source.size();
  const std::array fields{
      ByteView{request_.lane}, detail::raw_native_field(request_.ordinal),
      ByteView{generation.bytes()}, detail::raw_native_field(length),
      detail::raw_string_field(source)};
  return publish(1902, fields, &data.value(), RawPartition::data);
}
void RawTurnTrace::source_return(RawEventRef request, std::uint8_t branch,
                                 std::optional<SourceReference> source,
                                 std::optional<Error> error) noexcept {
  std::array<ByteView, 3> fields{};
  fields[0] = detail::raw_native_field(branch);
  std::size_t count = 1;
  if (source) {
    fields[count++] = source->journal.bytes();
    fields[count++] = detail::raw_native_field(source->sequence);
  } else if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  event(1903, request, std::span{fields}.first(count));
}
RawEventRef RawTurnTrace::placeholder_request(bool interrupted,
                                              std::string_view head) noexcept {
  if (!credit_)
    return {};
  const auto length = head.size();
  const std::array fields{
      ByteView{request_.lane}, detail::raw_native_field(request_.ordinal),
      detail::raw_native_field(interrupted), detail::raw_native_field(length),
      detail::raw_string_field(head)};
  return publish(1918, fields, &*credit_, RawPartition::control);
}
void RawTurnTrace::placeholder_return(RawEventRef request, RawPlaceholderOutcome branch,
                                      std::size_t rows,
                                      std::optional<Error> error) noexcept {
  std::array<ByteView, 3> fields{};
  fields[0] = detail::raw_native_field(branch);
  std::size_t count = 1;
  if (branch == RawPlaceholderOutcome::returned)
    fields[count++] = detail::raw_native_field(rows);
  else if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  event(1914, request, std::span{fields}.first(count));
}
void RawTurnTrace::finish(std::uint8_t branch, std::uint8_t stage,
                          std::optional<Error> error) noexcept {
  const ErrnoGuard guard;
  if (finished_)
    return;
  std::array<ByteView, 15> fields{};
  fields[0] = detail::raw_native_field(branch);
  fields[1] = detail::raw_native_field(stage);
  std::size_t count = 2;
  if (error) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  const auto clock = credit_ ? raw_local_clock(true) : RawClockSample{};
  const auto readings = clocks(clock);
  std::copy(readings.begin(), readings.end(), fields.begin() + count);
  event(1906, request_, std::span{fields}.first(count + readings.size()));
  finished_ = true;
}
std::string_view raw_turn_description() noexcept {
  return "turn1900-1920;E16;R=lane16,ordinalu64;native fields,no padding;"
         "1900:bool running,backstop,generation16,size_t head_length,head bytes,"
         "native clocks;1901:turnR,bool existing_settlement,u64 max_file_bytes,"
         "max_records,cursor journal16,sequenceu64,end_offsetu64;"
         "1902:turnR,generation16,size_t length,source bytes;1903:sourceR,u8 branch,"
         "success source journal16,sequenceu64 OR ErrorCode,i64;"
         "1904:turnR,u8 stage(reset0,begin1,evaluate2,settle3,cancel_clear4,closed5),"
         "bool running,backstop,capacity,pending_budget,program,tools,workflows,"
         "unsigned workflow_depth,invocations,size_t operation_depth,unretained;"
         "1905:turnR,u8 kind(program0,tool1,budget2),size_t length,revision bytes;"
         "1906:turnR,u8 branch(returned0,refused1,Error2,allocation3,unknown4),"
         "u8 reached_stage,[ErrorCode,i64],native clocks;"
         "1918:turnR,bool interrupted,size_t head_length,head bytes;"
         "1914:placeholderR,u8 branch(returned0,Error1,allocation2,unknown3),"
         "success size_t rows OR ErrorCode,i64;"
         "1907:u8 method(read0,propose1,observe2,restore3),bool parent,[R],"
         "size_t revision_length,revision bytes;1908:policyR,validation:bool "
         "enabled_present,[bool enabled],bool target_present,[size_t target],"
         "bool trigger_present,[size_t trigger];observation:bool enabled,"
         "bool trigger_present,[size_t trigger],size_t input,bool triggered;"
         "1909:policyR,size_t length,revision bytes;1910:policyR,u8 branch"
         "(returned0,stale1,Error2,allocation3,unknown4),[ErrorCode,i64];"
         "1911:bool parent,[R],size_t rows;1912:protocolR,u8 branch(valid0,"
         "Error1,allocation2,unknown3),[ErrorCode,i64];"
         "1913:protocolR,size_t call_id_length,call_id bytes;"
         "1915:operationR,attempt16,size_t task_length,task bytes;"
         "1916:bindingR,u8 branch(running0,finished1,failed2,unknown3);"
         "1920:activityR,bool observer_present;1917:notificationR,u8 branch"
         "(absent0,returned1,Error2,allocation3,unknown4),[ErrorCode,i64];"
         "native clocks:clockid_t method,"
         "int result,error,time_t seconds,long nanoseconds,bool cpu_read,"
         "clockid_t cpu_method,int result,error,time_t seconds,long nanoseconds;"
         "requests/results distinct;no duration arithmetic";
}
} // namespace blackbird
