#include "blackbird/raw_session.hpp"
#include <algorithm>
#include <cerrno>
namespace blackbird {
namespace {
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
} // namespace
std::uint64_t raw_session_bound(std::uint64_t bytes, std::uint64_t suffix) {
  if (suffix > UINT64_MAX - bytes) {
    raw_capture_failed({ErrorCode::overflow});
    throw Error{ErrorCode::overflow};
  }
  return bytes + suffix;
}
std::uint64_t raw_session_product(std::uint64_t bytes, std::uint64_t count) {
  if (count && bytes > UINT64_MAX / count) {
    raw_capture_failed({ErrorCode::overflow});
    throw Error{ErrorCode::overflow};
  }
  return bytes * count;
}
RawEventRef raw_session_record(std::uint32_t site, RawEventRef parent,
                               std::span<const ByteView> fields, RawPartition partition,
                               RawCapacityClaim *credit) {
  const ErrnoGuard guard;
  if (!credit)
    return {};
  auto result = raw_publish_linked(site, parent, fields, partition, *credit);
  if (!result.has_value())
    throw result.error();
  return result.value();
}
std::optional<RawCapacityClaim>
RawSessionCall::source_credit(std::uint64_t bound) const {
  if (!enabled())
    return {};
  return trace_.data(raw_session_bound(49, bound));
}
std::optional<RawCapacityClaim>
RawSessionCall::bytes_credit(std::uint64_t bound) const {
  if (!enabled())
    return {};
  return trace_.data(raw_session_bound(48, bound));
}
RawEventRef RawSessionCall::bytes(std::uint32_t site, std::string_view value,
                                  std::optional<RawCapacityClaim> &credit) const {
  const auto size = value.size();
  const auto record = raw_session_record(
      site, reference(),
      std::array{detail::raw_native_field(size), detail::raw_string_field(value)},
      RawPartition::data, credit ? &*credit : nullptr);
  credit.reset();
  return record;
}

RawEventRef RawSessionCall::source(std::uint8_t kind, std::string_view bytes,
                                   std::optional<RawCapacityClaim> &credit,
                                   std::uint32_t site) const {
  const auto size = bytes.size();
  const auto record = raw_session_record(
      site, reference(),
      std::array{detail::raw_native_field(kind), detail::raw_native_field(size),
                 detail::raw_string_field(bytes)},
      RawPartition::data, credit ? &*credit : nullptr);
  credit.reset();
  return record;
}
RawEventRef RawSessionCall::source(std::uint8_t kind, std::string_view bytes,
                                   std::uint32_t site) const {
  auto credit = source_credit(bytes.size());
  return source(kind, bytes, credit, site);
}
RawEventRef RawSessionCall::event(std::uint32_t site, std::span<const ByteView> fields,
                                  std::optional<RawControlToken> &credit) const {
  return raw_session_record(site, reference(), fields, RawPartition::control,
                            credit ? &*credit : nullptr);
}
int raw_session_stat(const RawAdapterContext *context, RawSessionStat method,
                     const std::filesystem::path &path, struct stat &status) {
  if (!context || !context->parent || !raw_enabled())
    return method == RawSessionStat::lstat ? ::lstat(path.c_str(), &status)
                                           : ::stat(path.c_str(), &status);
  const auto maximum = std::max<std::uint64_t>(
      48, 44 + (method == RawSessionStat::lstat ? sizeof(status.st_mode)
                                                : sizeof(status.st_mtime)));
  auto admitted = raw_claim_control(41 + maximum);
  if (!admitted.has_value())
    throw admitted.error();
  auto credit = std::move(admitted).value();
  const auto request = raw_session_record(4240, *context->parent,
                                          std::array{detail::raw_native_field(method)},
                                          RawPartition::control, &credit);
  const int result = method == RawSessionStat::lstat ? ::lstat(path.c_str(), &status)
                                                     : ::stat(path.c_str(), &status);
  const int error = errno;
  if (result < 0)
    (void)raw_session_record(
        4241, request,
        std::array{detail::raw_native_field(result), detail::raw_native_field(error)},
        RawPartition::control, &credit);
  else if (method == RawSessionStat::lstat)
    (void)raw_session_record(4241, request,
                             std::array{detail::raw_native_field(result),
                                        detail::raw_native_field(status.st_mode)},
                             RawPartition::control, &credit);
  else
    (void)raw_session_record(4241, request,
                             std::array{detail::raw_native_field(result),
                                        detail::raw_native_field(status.st_mtime)},
                             RawPartition::control, &credit);
  errno = error;
  return result;
}
std::string_view raw_session_description() noexcept {
  return "session4210-4259;E16;R=lane16,ordinal8;native,no padding;"
         "4210:parentR,u8 action,journal16,u64 sequence,end;4211:caller end;"
         "4212/4222:R,u8 kind,size_t N,original business bytes;"
         "kind1name2model3effort4workflow5path6serialized7locator8status9quoted"
         "10resumeCommand;4213:R,size_t N,actual locator bytes;"
         "4214:R,actor16,conversation16,workflow16;4215:R,u8 assignment;"
         "4216:R,u8 predicate,bool result;4217:snapshot parentR,u8 "
         "action,actor16,conversation16,workflow16;4218:metadata parentR;4219:caller "
         "end;4220:parentR;4221:caller end;"
         "4224:R,size_t row,metadataRequestR;4225:R,size_t rows;4226:R,actual absolute "
         "path bodyR;"
         "4240:parentR,u8 method(lstat1,stat2);4241:requestR,int rc,"
         "[int errno on negative|mode_t lstat.mode on success|time_t stat.mtime];"
         "4242:parentR,u8 filesystem method;4243:caller end,[int error_code,"
         "[method8 actual path bodyR|bool predicate or iterator_end only if reached on "
         "returned]];"
         "fs1symlink2directory3iteratorOpen4iteratorIncrement5entryDirectory"
         "6absolute7normalize8weakCanonical;"
         "caller end:R,u8 RawOperationBranch,u8 stage,bool Error,[ErrorCode,i64];"
         "sessionAction1restore2save3identity4snapshot";
}
} // namespace blackbird
