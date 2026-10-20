#include "blackbird/raw_input.hpp"
#include <cerrno>
#include <unistd.h>
namespace blackbird {
namespace {
template <class T> T require(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
std::optional<RawEventRef> selected(const RawAdapterContext *context) noexcept {
  if (context && context->parent && context->parent->ordinal && raw_enabled())
    return context->parent;
  return {};
}
RawControlToken control(std::uint64_t size) { return require(raw_claim_control(size)); }
} // namespace
ssize_t raw_main_read(const RawAdapterContext *context, int fd, void *buffer,
                      std::size_t size) {
  const auto parent = selected(context);
  if (!parent)
    return ::read(fd, buffer, size);
  auto lifecycle = control(104);
  auto body = require(raw_claim_data(raw_session_bound(48, size)));
  const auto request = raw_session_record(
      4230, *parent,
      std::array{detail::raw_native_field(fd), detail::raw_native_field(size)},
      RawPartition::control, &lifecycle);
  const auto result = ::read(fd, buffer, size);
  const int error = errno;
  if (result > 0) {
    const std::string_view acquired{static_cast<const char *>(buffer),
                                    static_cast<std::size_t>(result)};
    (void)raw_session_record(4231, request,
                             std::array{detail::raw_native_field(result),
                                        detail::raw_string_field(acquired)},
                             RawPartition::data, &body);
  } else if (result < 0)
    (void)raw_session_record(
        4231, request,
        std::array{detail::raw_native_field(result), detail::raw_native_field(error)},
        RawPartition::control, &lifecycle);
  else
    (void)raw_session_record(4231, request,
                             std::array{detail::raw_native_field(result)},
                             RawPartition::control, &lifecycle);
  errno = error;
  return result;
}
int raw_main_poll(const RawAdapterContext *context, pollfd &input, int timeout) {
  const auto parent = selected(context);
  if (!parent)
    return ::poll(&input, 1, timeout);
  auto lifecycle = control(100);
  const auto request =
      raw_session_record(4232, *parent,
                         std::array{detail::raw_native_field(input.fd),
                                    detail::raw_native_field(input.events),
                                    detail::raw_native_field(timeout)},
                         RawPartition::control, &lifecycle);
  const auto result = ::poll(&input, 1, timeout);
  const int error = errno;
  if (result < 0)
    (void)raw_session_record(4233, request,
                             std::array{detail::raw_native_field(result),
                                        detail::raw_native_field(error),
                                        detail::raw_native_field(input.revents)},
                             RawPartition::control, &lifecycle);
  else
    (void)raw_session_record(4233, request,
                             std::array{detail::raw_native_field(result),
                                        detail::raw_native_field(input.revents)},
                             RawPartition::control, &lifecycle);
  errno = error;
  return result;
}
void raw_main_branch(RawSessionCall &call, std::uint8_t branch) {
  auto credit = call.control(41);
  (void)call.event(4205, std::array{detail::raw_native_field(branch)}, credit);
}
void raw_main_control(const RawAdapterContext *context, std::uint8_t kind,
                      std::string_view original) {
  const auto parent = selected(context);
  if (!parent)
    return;
  auto credit = require(raw_claim_data(raw_session_bound(49, original.size())));
  const auto size = original.size();
  (void)raw_session_record(4206, *parent,
                           std::array{detail::raw_native_field(kind),
                                      detail::raw_native_field(size),
                                      detail::raw_string_field(original)},
                           RawPartition::data, &credit);
}
std::string_view raw_input_description() noexcept {
  return "main4200-4206,4230-4233;E16;R=lane16,ordinal8;native,no padding;"
         "4200:startupR,u8 kind(once1,plain2,TUI3,resume4),size_t N;"
         "4201/4204:caller end;4202:inputR,size_t N,unrendered input bytes;"
         "4203:inputR;4205:dispatchR,u8 branch;branch1help2exec/process3fg4jobs5bg"
         "6resumeContinue7tasks8runs9run/send/join10await11stop12authStatus"
         "13authProviders14authUnsupported15variables/correlate16gitUsage"
         "17gitObserve18trace19colleagues20colleague21session22name23resume24new"
         "25restart26workflow27context28sessions29checkpoint30stats31compact"
         "32inspect33originals34restore35model36effort37decision38beads39lua"
         "40workflowFallback41instrumentReloadPreview42profile;4206:startupR,u8 "
         "control(quit1,exit2,paste3,send4),"
         "size_t N,control bytes;4230:startupR,int fd,size_t requested;"
         "4231:readR,ssize_t rc,[int errno if negative|unchanged bytes if positive];"
         "4232:startupR,int fd,short events,int timeout;"
         "4233:pollR,int rc,[int errno if negative],short revents";
}
} // namespace blackbird
