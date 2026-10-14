#include "blackbird/raw_program.hpp"
#include <algorithm>
#include <cerrno>

namespace blackbird {
namespace {
using detail::raw_native_field;
template <class T> T take(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
struct PreserveErrno {
  int saved = errno;
  ~PreserveErrno() { errno = saved; }
};
} // namespace
RawProgramCall::RawProgramCall(std::uint8_t method, const RawProgramContext *context) {
  if (!context || !raw_enabled())
    return;
  const bool parent = context->parent && context->parent->ordinal;
  const bool generation = context->generation.has_value();
  std::array<ByteView, 6> fields{};
  std::size_t count = 0;
  fields[count++] = raw_native_field(method);
  fields[count++] = raw_native_field(parent);
  if (parent) {
    fields[count++] = context->parent->lane;
    fields[count++] = raw_native_field(context->parent->ordinal);
  }
  fields[count++] = raw_native_field(generation);
  if (generation)
    fields[count++] = *context->generation;
  begin(3800, 3801, std::span{fields}.first(count), 0);
}
RawProgramCall::RawProgramCall(std::uint32_t request, std::uint32_t terminal,
                               const RawProgramContext *context,
                               std::span<const ByteView> fields,
                               std::uint64_t terminal_extra) {
  if (!context || !context->parent || !context->parent->ordinal || !raw_enabled())
    return;
  std::array<ByteView, 20> body{};
  const bool present = true;
  const std::size_t prefix = request == 3810 ? 3 : 2;
  if (fields.size() > body.size() - prefix) {
    raw_capture_failed({ErrorCode::capacity});
    throw Error{ErrorCode::capacity};
  }
  if (prefix == 3)
    body[0] = raw_native_field(present);
  body[prefix - 2] = context->parent->lane;
  body[prefix - 1] = raw_native_field(context->parent->ordinal);
  std::copy(fields.begin(), fields.end(),
            body.begin() + static_cast<std::ptrdiff_t>(prefix));
  begin(request, terminal, std::span{body}.first(prefix + fields.size()),
        terminal_extra);
}
void RawProgramCall::begin(std::uint32_t site, std::uint32_t terminal,
                           std::span<const ByteView> fields,
                           std::uint64_t terminal_extra) {
  const PreserveErrno preserved;
  const auto request_bytes = raw_record_bytes(fields);
  if (terminal_extra > UINT32_MAX - 52 ||
      request_bytes > UINT32_MAX - 52 - terminal_extra) {
    raw_capture_failed({ErrorCode::overflow});
    throw Error{ErrorCode::overflow};
  }
  emission_.start({site, terminal}, request_bytes + 52 + terminal_extra, fields);
}
RawProgramCall::~RawProgramCall() { finish(RawOperationBranch::unfinished); }
std::optional<RawControlToken> RawProgramCall::control(std::uint64_t bytes) const {
  const PreserveErrno preserved;
  if (!enabled())
    return {};
  if (bytes > UINT32_MAX) {
    raw_capture_failed({ErrorCode::overflow});
    throw Error{ErrorCode::overflow};
  }
  return take(raw_claim_control(bytes));
}
std::optional<RawCapacityClaim> RawProgramCall::data(std::uint64_t bytes) const {
  const PreserveErrno preserved;
  if (!enabled())
    return {};
  if (bytes > UINT32_MAX) {
    raw_capture_failed({ErrorCode::overflow});
    throw Error{ErrorCode::overflow};
  }
  return take(raw_claim_data(bytes));
}
void RawProgramCall::event(std::uint32_t site, std::span<const ByteView> fields,
                           std::optional<RawControlToken> &credit) noexcept {
  if (credit)
    (void)raw_publish_linked(site, reference(), fields, RawPartition::control, *credit);
}
void RawProgramCall::bytes(std::uint32_t site, std::string_view value,
                           std::optional<RawCapacityClaim> &credit) noexcept {
  const auto size = value.size();
  if (credit)
    (void)raw_publish_linked(
        site, reference(),
        std::array{raw_native_field(size), detail::raw_string_field(value)},
        RawPartition::data, *credit);
}
void RawProgramCall::encoded(std::uint8_t kind, std::string_view value,
                             std::optional<RawCapacityClaim> &credit) noexcept {
  const auto size = value.size();
  if (credit)
    (void)raw_publish_linked(3803, reference(),
                             std::array{raw_native_field(kind), raw_native_field(size),
                                        detail::raw_string_field(value)},
                             RawPartition::data, *credit);
}
void RawProgramCall::revision(std::uint8_t kind, std::string_view value) {
  auto credit = control(49 + value.size());
  const auto size = value.size();
  event(3802,
        std::array{raw_native_field(kind), raw_native_field(size),
                   detail::raw_string_field(value)},
        credit);
}
void RawProgramCall::validation(std::uint8_t kind, std::size_t size,
                                std::optional<std::size_t> index) {
  auto credit = control(index ? 58 : 50);
  const bool present = index.has_value();
  std::array fields{raw_native_field(kind), raw_native_field(size),
                    raw_native_field(present), ByteView{}};
  if (index)
    fields[3] = raw_native_field(*index);
  event(3806, std::span{fields}.first(index ? 4 : 3), credit);
}
void RawProgramCall::finish(RawOperationBranch branch, const Error *error,
                            std::span<const ByteView> extra) noexcept {
  emission_.finish(branch, stage_, error, extra);
}
std::shared_ptr<RawProgramIssuer> RawProgramIssuer::prepare() {
  const PreserveErrno preserved;
  auto handle = take(raw_prepare_thread());
  if (!handle)
    throw Error{ErrorCode::unsupported};
  auto issuer = std::make_shared<RawProgramIssuer>();
  issuer->handle = std::move(handle);
  return issuer;
}
RawProgramLifetime::RawProgramLifetime(std::shared_ptr<RawProgramIssuer> issuer,
                                       RawEventRef parent, bool queued,
                                       std::uint8_t kind)
    : issuer_(std::move(issuer)), parent_(parent), kind_(kind), queued_(queued) {
  if (!issuer_)
    return;
  const PreserveErrno preserved;
  const std::lock_guard lock{issuer_->mutex};
  RawThreadBinding binding{issuer_->handle};
  auto grant = raw_try_claim_control(queued ? 173 : 213);
  if (!grant.has_value()) {
    raw_capture_failed(grant.error());
    throw grant.error();
  }
  credit_.emplace(std::move(grant).value());
}
RawProgramLifetime::~RawProgramLifetime() {
  finish(queued_ ? 3 : 4, nullptr, {}, RawOperationBranch::unfinished);
}
RawProgramLifetime::RawProgramLifetime(RawProgramLifetime &&other) noexcept
    : issuer_(std::move(other.issuer_)), credit_(std::move(other.credit_)),
      parent_(other.parent_), request_(other.request_), kind_(other.kind_),
      queued_(other.queued_) {
  other.credit_.reset();
}
RawProgramLifetime &RawProgramLifetime::operator=(RawProgramLifetime &&other) noexcept {
  if (this != &other) {
    finish(queued_ ? 3 : 4, nullptr, {}, RawOperationBranch::unfinished);
    issuer_ = std::move(other.issuer_);
    credit_ = std::move(other.credit_);
    parent_ = other.parent_;
    request_ = other.request_;
    kind_ = other.kind_;
    queued_ = other.queued_;
    other.credit_.reset();
  }
  return *this;
}
RawEventRef RawProgramLifetime::emit(std::uint32_t site, RawEventRef parent,
                                     std::span<const ByteView> fields) noexcept {
  if (!credit_ || !issuer_)
    return {};
  const std::lock_guard lock{issuer_->mutex};
  RawThreadBinding binding{issuer_->handle};
  const auto result =
      raw_publish_linked(site, parent, fields, RawPartition::control, *credit_);
  return result.has_value() ? result.value() : RawEventRef{};
}
void RawProgramLifetime::generation(IdentityBytes generation) noexcept {
  request_ =
      emit(3830, parent_, std::array{raw_native_field(kind_), ByteView{generation}});
}
void RawProgramLifetime::queued(std::size_t count) noexcept {
  request_ = emit(3833, parent_, std::array{raw_native_field(count)});
}
void RawProgramLifetime::installed(std::string_view revision) noexcept {
  const auto size = revision.size();
  (void)emit(3831, request_,
             std::array{raw_native_field(size), detail::raw_string_field(revision)});
}
void RawProgramLifetime::installed(std::size_t count) noexcept {
  (void)emit(3834, request_, std::array{raw_native_field(count)});
}
void RawProgramLifetime::finish(std::uint8_t reason, const Error *error,
                                std::optional<RawEventRef> related,
                                RawOperationBranch branch) noexcept {
  const PreserveErrno preserved;
  if (!credit_)
    return;
  const bool present = related && related->ordinal;
  const bool failed = error != nullptr;
  std::array<ByteView, 9> fields{};
  std::size_t count = 0;
  if (queued_)
    fields[count++] = raw_native_field(branch);
  fields[count++] = raw_native_field(reason);
  fields[count++] = raw_native_field(present);
  if (present) {
    fields[count++] = related->lane;
    fields[count++] = raw_native_field(related->ordinal);
  }
  fields[count++] = raw_native_field(failed);
  if (error) {
    fields[count++] = raw_native_field(error->code);
    fields[count++] = raw_native_field(error->detail);
  }
  if (request_.ordinal)
    (void)emit(queued_ ? 3835 : 3832, request_, std::span{fields}.first(count));
  if (issuer_) {
    const std::lock_guard lock{issuer_->mutex};
    RawThreadBinding binding{issuer_->handle};
    credit_.reset();
  } else {
    credit_.reset();
  }
}
std::string_view raw_program_description() noexcept {
  return "program3800-3835;E16;R24;G16;native no padding;"
         "3800:u8 method,bool parent,[R],bool generation,[G];3801:end;"
         "end:R,u8 RawOperationBranch,u8 stage,bool Error,[ErrorCode,i64];"
         "3802:R,u8 kind,size_t N,revision;3803:R,u8 kind,size_t N,original BBM;"
         "3804:R,u8 kind,size_t bytes,nodes,depth;3805:end,[bool has_value];"
         "3806:R,u8 kind,size_t count,bool index,[size_t index];"
         "3807:R,u8 kind,size_t source;3808:end,[bool valid];"
         "3809:R,u8 kind,G;3810:bool parent,[R],size_t revision,revision,size_t input;"
         "3811:R,size_t N,input;3812:R,u8 route,bool index,[size_t];"
         "3813:end,[bool selected];3814:R,size_t N,trigger;"
         "3815:R,u8 lookup;3816:end;3817:R,size_t N,name;"
         "3820:selectionR,G,unsigned depth,count;3821:end,unsigned depth,count;"
         "3822:R,attempt16,invocation16,generation16;3823:R,operationR,size_t "
         "N,revision;"
         "3824:R,unsigned depth,count;3825:turnR,bool draining,size_t queued;3826:end;"
         "3827:operationR,size_t N,revision,size_t source;3828:end;"
         "3830:callR,u8 kind,G;3831:R,size_t N,revision;"
         "3832:R,u8 reason,bool activation,[R],bool Error,[ErrorCode,i64];"
         "3833:selectionR,size_t queued;3834:R,size_t queued;"
         "3835:R,u8 branch,u8 reason,bool execution,[R],bool Error,[ErrorCode,i64];"
         "3836:invokeR,size_t source;3837:end;3838:R,size_t N,original wrapped source;"
         "3840:VM R,u8 phase,bool captured,int registryRef;"
         "3841:end,[int LuaStatus,bool "
         "captured];phase0prepare1restore2release3errorText;"
         "authored3880=step,3881=calls,3882=restarting";
}
} // namespace blackbird
