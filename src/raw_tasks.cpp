#include "blackbird/raw_tasks.hpp"
#include <algorithm>
#include <cerrno>
namespace blackbird {
namespace {
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
std::optional<RawControlToken> claim(std::uint64_t bytes, RawControlToken *parent) {
  if (!raw_enabled())
    return {};
  auto held = parent ? parent->split(bytes) : raw_claim_control(bytes);
  if (!held.has_value()) {
    raw_capture_failed(held.error());
    throw held.error();
  }
  return std::move(held).value();
}
} // namespace
RawTaskTrace::RawTaskTrace(RawTaskAction action, std::uint64_t revision,
                           RawTaskOrigin origin, std::uint64_t credit) {
  const ErrnoGuard guard;
  const bool parent = origin.parent.has_value();
  borrowed_data_ = origin.data;
  borrowed_control_ = origin.borrowed_control;
  credit_ = claim(credit + (parent ? 24 : 0),
                  origin.credit ? origin.credit : borrowed_control_);
  if (!credit_.has_value())
    return;
  const auto tag = static_cast<std::uint8_t>(action);
  if (origin.parent.has_value()) {
    const auto &ref = *origin.parent;
    const std::array fields{detail::raw_native_field(tag),
                            detail::raw_native_field(parent), ByteView{ref.lane},
                            detail::raw_native_field(ref.ordinal),
                            detail::raw_native_field(revision)};
    request_ = publish(1700, fields, RawPartition::control, &*credit_);
  } else {
    const std::array fields{detail::raw_native_field(tag),
                            detail::raw_native_field(parent),
                            detail::raw_native_field(revision)};
    request_ = publish(1700, fields, RawPartition::control, &*credit_);
  }
}
RawTaskTrace::RawTaskTrace(std::string_view command) : operator_(true) {
  const ErrnoGuard guard;
  credit_ = claim(50, nullptr);
  if (!credit_.has_value())
    return;
  // No text has been acquired and no request published if DATA admission fails.
  const auto size = command.size();
  if (size > UINT32_MAX - raw_envelope_size - sizeof(size))
    throw Error{ErrorCode::overflow};
  auto held = raw_claim_data(raw_envelope_size + sizeof(size) + size);
  if (!held.has_value()) {
    credit_.reset();
    throw held.error();
  }
  auto data = std::move(held).value();
  const std::array fields{detail::raw_native_field(size),
                          detail::raw_string_field(command)};
  request_ = publish(1712, fields, RawPartition::data, &data);
}
RawTaskTrace::~RawTaskTrace() { finish(5); }
RawEventRef RawTaskTrace::publish(std::uint32_t site, std::span<const ByteView> fields,
                                  RawPartition partition,
                                  RawControlToken *credit) noexcept {
  const ErrnoGuard guard;
  if (!credit_.has_value())
    return {};
  auto emitted = raw_emit(site, fields, partition, credit);
  if (!emitted.has_value()) {
    raw_fence(emitted.error());
    return {};
  }
  return {raw_lane_epoch(), emitted.value()};
}
RawEventRef RawTaskTrace::event(std::uint32_t site, RawEventRef parent,
                                std::span<const ByteView> fields) noexcept {
  const ErrnoGuard guard;
  if (!credit_.has_value())
    return {};
  std::array<ByteView, 16> body{};
  if (fields.size() > body.size() - 2) {
    raw_fence({ErrorCode::capacity});
    return {};
  }
  body[0] = parent.lane;
  body[1] = detail::raw_native_field(parent.ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  return publish(site, std::span{body}.first(fields.size() + 2), RawPartition::control,
                 &*credit_);
}
void RawTaskTrace::finish(std::uint8_t branch, std::optional<Error> error) noexcept {
  const ErrnoGuard guard;
  if (!credit_.has_value())
    return;
  std::array<ByteView, 4> fields{};
  std::size_t count = 1;
  fields[0] = detail::raw_native_field(branch);
  if (!operator_)
    fields[count++] = detail::raw_native_field(stage_);
  if (error.has_value()) {
    fields[count++] = detail::raw_native_field(error->code);
    fields[count++] = detail::raw_native_field(error->detail);
  }
  (void)event(operator_ ? 1713 : 1711, request_, std::span{fields}.first(count));
  credit_.reset();
}
void RawTaskTrace::bytes(std::string_view source, RawCapacityClaim &credit) noexcept {
  const auto size = source.size();
  const std::array fields{
      ByteView{request_.lane}, detail::raw_native_field(request_.ordinal),
      detail::raw_native_field(size), detail::raw_string_field(source)};
  (void)publish(1701, fields, RawPartition::data, &credit);
}
RawTaskOrigin RawTaskTrace::child(bool replay) const noexcept {
  return {enabled() ? std::optional<RawEventRef>{request_} : std::nullopt, replay,
          nullptr, borrowed_data_, borrowed_control_};
}
std::optional<RawControlToken> RawTaskTrace::split(std::uint64_t amount) {
  if (!credit_.has_value())
    return {};
  auto child = credit_->split(amount);
  if (!child.has_value()) {
    raw_capture_failed(child.error());
    throw child.error();
  }
  return std::move(child).value();
}
std::string_view raw_tasks_description() noexcept {
  return "task1700-1713;E16;R=lane16,ordinal8;native fields,host byte order,no padding;"
         "1700:u8 action(prepare1,read2,snapshot3,restore4,replay5,edit6,commit7,"
         "replay_prepare8,replay_commit9),bool parent,[R],u64 revision;"
         "1701:R,size_t bytes,original business packet bytes;"
         "1702:R,u64 revision,u64 next,size_t changed,size_t removed,bool order,"
         "[size_t order],size_t receipts,size_t populations[5];"
         "1703:R,u64 revision;1704:editR,prepareR,application16;"
         "1705:publicationR,u8 branch,[ErrorCode,i64 detail];"
         "1706:R,u64 revision,u64 next,size_t items,size_t order,size_t receipts,"
         "size_t populations[5];1707:R,u64 revision,size_t total,size_t populations[5],"
         "bool conflict,[u64 offset,size_t cursor,size_t rows];"
         "1708:R,u64 revision,u64 next,size_t rows,size_t order,size_t receipts;"
         "1709:editR;1710:callbackR,u8 branch,[ErrorCode,i64 detail];"
         "1711:R,u8 branch(returned0,duplicate1,conflict2,Error3,allocation4,unknown5),"
         "u8 reached stage,[ErrorCode,i64 detail];1712:size_t bytes,operator text;"
         "1713:operatorR,u8 branch,[ErrorCode,i64 detail];"
         "stages:0 entry,1 validation,2 existing encoding,3 staging,4 append,"
         "5 commit,6 callback,7 projection,8 restore validation;"
         "populations=queued,active,blocked,done,dropped;1710 branch uses same tags";
}
} // namespace blackbird
