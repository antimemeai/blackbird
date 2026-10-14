#include "blackbird/raw_bridge.hpp"
#include <algorithm>
#include <cerrno>
#include <utility>
namespace blackbird {
namespace {
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
template <class T> T take(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
} // namespace
RawBridgeTrace::RawBridgeTrace(RawBridgeTrace &&other) noexcept
    : issuer_(std::move(other.issuer_)), emission_(std::move(other.emission_)),
      terminal_(other.terminal_), stage_(other.stage_) {
  other.terminal_ = 0;
}
RawBridgeTrace::~RawBridgeTrace() {
  const bool available = false;
  if (terminal_ == 4825) {
    const auto reason = std::uint8_t{1};
    finish(RawOperationBranch::unfinished, nullptr,
           std::array{detail::raw_native_field(reason),
                      detail::raw_native_field(available),
                      detail::raw_native_field(available),
                      detail::raw_native_field(available)});
  } else
    finish(RawOperationBranch::unfinished, nullptr,
           std::array{detail::raw_native_field(available)});
}
void RawBridgeTrace::start(std::uint32_t request, std::uint32_t terminal,
                           std::uint64_t amount, bool selected,
                           std::span<const ByteView> fields, bool finite) {
  const ErrnoGuard saved;
  if (!selected)
    return;
  issuer_ = raw_current_thread();
  terminal_ = terminal;
  emission_.start({request, terminal}, amount, fields,
                  finite ? RawAdmission::finite : RawAdmission::waiting);
}
RawEventRef RawBridgeTrace::event(std::uint32_t site,
                                  std::span<const ByteView> fields) {
  return linked(site, reference(), fields);
}
RawEventRef RawBridgeTrace::linked(std::uint32_t site, RawEventRef parent,
                                   std::span<const ByteView> fields) {
  if (!enabled())
    return {};
  return take(raw_publish_linked(site, parent, fields, RawPartition::control,
                                 *emission_.bank()));
}
std::optional<RawCapacityClaim> RawBridgeTrace::data(std::uint64_t amount) const {
  const ErrnoGuard saved;
  if (!enabled())
    return {};
  if (amount > UINT32_MAX) {
    raw_capture_failed({ErrorCode::overflow});
    throw Error{ErrorCode::overflow};
  }
  return take(raw_claim_data(amount));
}
void RawBridgeTrace::original(std::uint32_t site, std::span<const ByteView> fields,
                              RawCapacityClaim *bank) {
  if (!enabled() || !bank)
    return;
  (void)take(raw_publish_linked(site, reference(), fields, RawPartition::data, *bank));
}
void RawBridgeTrace::finish(RawOperationBranch branch, const Error *error,
                            std::span<const ByteView> fields) noexcept {
  emission_.finish(branch, stage_, error, fields);
}
std::shared_ptr<RawModuleIssuer> RawModuleIssuer::prepare() {
  const ErrnoGuard saved;
  auto handle = take(raw_prepare_thread());
  if (!handle)
    throw Error{ErrorCode::unsupported};
  auto issuer = std::make_shared<RawModuleIssuer>();
  issuer->handle = std::move(handle);
  return issuer;
}
RawModuleToken::RawModuleToken(std::shared_ptr<RawModuleIssuer> issuer,
                               std::span<const ByteView> fields,
                               std::optional<std::string_view> name)
    : issuer_(std::move(issuer)) {
  if (!issuer_)
    return;
  const ErrnoGuard saved;
  const std::lock_guard lock{issuer_->mutex};
  RawThreadBinding binding{issuer_->handle};
  try {
    trace_.start(4820, 4825, 800, true, fields, true);
    if (name) {
      if (name->size() > UINT32_MAX - 48) {
        raw_capture_failed({ErrorCode::overflow});
        throw Error{ErrorCode::overflow};
      }
      auto grant = raw_try_claim_data(48 + name->size());
      if (!grant.has_value()) {
        raw_capture_failed(grant.error());
        throw grant.error();
      }
      auto credit = std::move(grant).value();
      const auto size = name->size();
      trace_.original(
          4821,
          std::array{detail::raw_native_field(size), detail::raw_string_field(*name)},
          &credit);
    }
  } catch (const Error &error) {
    finish_bound(4, RawOperationBranch::error, &error, {});
    throw;
  } catch (...) {
    finish_bound(4, RawOperationBranch::nonstandard, nullptr, {});
    throw;
  }
}
RawModuleToken::~RawModuleToken() {
  finish(closing() ? 2 : 1, RawOperationBranch::unfinished);
}
RawModuleToken::RawModuleToken(RawModuleToken &&other) noexcept
    : issuer_(std::move(other.issuer_)), trace_(std::move(other.trace_)),
      failure_(other.failure_), inner_status_(other.inner_status_),
      active_(std::exchange(other.active_, false)) {}
RawEventRef RawModuleToken::event(RawBridgeObservation observation,
                                  std::span<const ByteView> fields) {
  if (!active_)
    throw Error{ErrorCode::invalid_range};
  if (!issuer_)
    return {};
  const ErrnoGuard saved;
  const std::lock_guard lock{issuer_->mutex};
  RawThreadBinding binding{issuer_->handle};
  trace_.stage(observation.stage);
  return trace_.event(observation.site, fields);
}
RawEventRef RawModuleToken::linked(std::uint32_t site, RawEventRef parent,
                                   std::span<const ByteView> fields) {
  if (!active_)
    throw Error{ErrorCode::invalid_range};
  if (!issuer_)
    return {};
  const ErrnoGuard saved;
  const std::lock_guard lock{issuer_->mutex};
  RawThreadBinding binding{issuer_->handle};
  return trace_.linked(site, parent, fields);
}
void RawModuleToken::chunk(std::string_view original, std::string_view native) {
  if (!active_)
    throw Error{ErrorCode::invalid_range};
  if (!issuer_)
    return;
  const ErrnoGuard saved;
  const std::lock_guard lock{issuer_->mutex};
  RawThreadBinding binding{issuer_->handle};
  trace_.stage(3);
  if (original.size() > UINT32_MAX - 57) {
    raw_capture_failed({ErrorCode::overflow});
    throw Error{ErrorCode::overflow};
  }
  auto grant = raw_try_claim_data(57 + original.size());
  if (!grant.has_value()) {
    raw_capture_failed(grant.error());
    throw grant.error();
  }
  auto credit = std::move(grant).value();
  const auto offered = original.size(), used = native.size();
  trace_.original(4826,
                  std::array{detail::raw_native_field(offered),
                             detail::raw_native_field(used),
                             detail::raw_string_field(native)},
                  &credit);
}
void RawModuleToken::inner_status(int status) noexcept {
  if (!issuer_) {
    inner_status_ = status;
    return;
  }
  const std::lock_guard lock{issuer_->mutex};
  inner_status_ = status;
}
void RawModuleToken::finish_bound(std::uint8_t reason, RawOperationBranch branch,
                                  const Error *error,
                                  RawModuleReturn outcome) noexcept {
  if (!active_)
    return;
  active_ = false;
  if (reason == 0)
    trace_.stage(8);
  if (!error && failure_)
    error = &*failure_;
  const bool observed = outcome.success != nullptr,
             returned = outcome.type && outcome.value,
             inner = inner_status_.has_value();
  std::array<ByteView, 8> fields{};
  std::size_t count = 0;
  fields[count++] = detail::raw_native_field(reason);
  fields[count++] = detail::raw_native_field(observed);
  if (observed)
    fields[count++] = detail::raw_native_field(*outcome.success);
  fields[count++] = detail::raw_native_field(returned);
  if (returned) {
    fields[count++] = detail::raw_native_field(*outcome.type);
    fields[count++] = detail::raw_native_field(*outcome.value);
  }
  fields[count++] = detail::raw_native_field(inner);
  if (inner)
    fields[count++] = detail::raw_native_field(*inner_status_);
  trace_.finish(branch, error, std::span{fields}.first(count));
}
void RawModuleToken::finish(std::uint8_t reason, RawOperationBranch branch,
                            const Error *error, RawModuleReturn outcome) noexcept {
  if (!active_)
    return;
  if (!issuer_) {
    active_ = false;
    return;
  }
  const ErrnoGuard saved;
  const std::lock_guard lock{issuer_->mutex};
  RawThreadBinding binding{issuer_->handle};
  finish_bound(reason, branch, error, outcome);
}
std::string_view raw_bridge_description() noexcept {
  return "bridge4800-4899;E16;R=lane16,u64 ordinal;native,no padding;"
         "4800:VM R,G16,int Op,argc,bool operation,[R],bool module,[R];"
         "4801:R,u8 branch,stage,bool Error,[ErrorCode,i64],bool returns,[int];"
         "4802:R,u8 role,size_t N,original "
         "bytes;role1callName2JSON3restore4display5error;"
         "4803:R,u8 mode(result0,error1,array2),int stackBase;4804:R,int "
         "LuaStatus,stackTop;"
         "4820:VM R,G16,int argc,inputType,bool parentModule,[R],bool operation,[R];"
         "4821:R,size_t N,name bytes;4822:R,u8 branch(cache1,cycle2),bool;"
         "4823:R,u8 mutation(loadingTrue1,loadingNil2,cache3),int type,pointer;"
         "4824:R,actual1506 R;4825:R,u8 branch,stage,bool Error,[ErrorCode,i64],u8 "
         "reason,"
         "bool pcall,[int LuaBoolean],bool value,[int type,pointer],bool inner,[int "
         "LuaStatus];"
         "4826:R,size_t LuaExtent,CStringExtent,native CString includingNUL;"
         "4830:R,actual1506 R,functionPointer,environmentPointer,int upvalueIndex;"
         "4831:R,bool name,[size_t CStringExtent,native CString includingNUL];"
         "4832:R,u8 mode3,int stackBase;4833:R,int LuaStatus,stackTop;"
         "4834:R,actual1517 R,functionPointer";
}
} // namespace blackbird
