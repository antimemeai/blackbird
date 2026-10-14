#include "blackbird/raw_plain.hpp"
#include <cerrno>
namespace blackbird {
namespace {
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
struct TerminalFields {
  RawEmission &emission;
  void finish(RawOperationBranch branch, const Error *error = nullptr) noexcept {
    emission.finish(branch, 0, error);
  }
};
} // namespace
void write_plain_native(int descriptor, std::span<const std::string_view> pieces,
                        std::span<const ByteView> fields,
                        const RawAdapterContext *context,
                        RawCapacityClaim *preheld_data,
                        RawControlToken *preheld_terminal) {
  const bool selected = context && context->parent && raw_enabled();
  RawCaptureFailureScope failure{selected};
  if (selected && !raw_reference_valid(*context->parent)) {
    const ErrnoGuard guard;
    const Error error{ErrorCode::invalid_identity};
    raw_capture_failed(error);
    raw_fence(error);
    throw error;
  }
  RawEmission emission;
  if (selected) {
    auto control =
        preheld_terminal ? preheld_terminal->split(52) : raw_claim_control(52);
    if (!control.has_value()) {
      raw_capture_failed(control.error());
      throw control.error();
    }
    const auto bytes = raw_record_bytes(fields);
    auto data = preheld_data ? preheld_data->split(bytes) : raw_claim_data(bytes);
    if (!data.has_value()) {
      raw_capture_failed(data.error());
      throw data.error();
    }
    auto credit = std::move(data).value();
    auto request = raw_publish(4340, fields, RawPartition::data, credit);
    if (!request.has_value())
      throw request.error();
    auto adopted = emission.adopt(request.value(), 4341, std::move(control).value());
    if (!adopted.has_value())
      throw adopted.error();
  }
  TerminalFields terminal{emission};
  raw_call_run(terminal, [&] {
    std::size_t offset = 0;
    for (auto remaining : pieces) {
      while (!remaining.empty()) {
        std::optional<RawControlToken> control;
        if (selected) {
          auto acquired = raw_claim_control(113);
          if (!acquired.has_value())
            throw acquired.error();
          control.emplace(std::move(acquired).value());
        }
        const auto requested = remaining.size();
        RawEventRef request{};
        if (control) {
          auto emitted =
              raw_publish_linked(4342, emission.reference(),
                                 std::array{detail::raw_native_field(descriptor),
                                            detail::raw_native_field(offset),
                                            detail::raw_native_field(requested)},
                                 RawPartition::control, *control);
          if (!emitted.has_value())
            throw emitted.error();
          request = emitted.value();
        }
        const auto result = ::write(descriptor, remaining.data(), remaining.size());
        const int error = errno;
        if (control) {
          const bool failed = result < 0;
          if (failed)
            (void)raw_publish_linked(4343, request,
                                     std::array{detail::raw_native_field(result),
                                                detail::raw_native_field(failed),
                                                detail::raw_native_field(error)},
                                     RawPartition::control, *control);
          else
            (void)raw_publish_linked(4343, request,
                                     std::array{detail::raw_native_field(result),
                                                detail::raw_native_field(failed)},
                                     RawPartition::control, *control);
        }
        errno = error;
        failure.require();
        if (result < 0 && error == EINTR)
          continue;
        if (result <= 0)
          throw Error{ErrorCode::io, result < 0 ? error : 0};
        const auto count = static_cast<std::size_t>(result);
        offset += count;
        remaining.remove_prefix(count);
      }
    }
    failure.require();
  });
  failure.require();
  failure.finish();
}
std::string_view raw_plain_description() noexcept {
  return "plain4340-4379;native,no padding;4340(DATA):parentR,int fd,u8 kind,size_t N,"
         "actual existing raw byte pieces concatenated by transport;4341:caller end;"
         "4342:packetR,int fd,size_t acceptedOffset,requested;4343:writeR,ssize_t "
         "result,bool failed,[int errno];"
         "4344:formatR,size_t N,original unrendered input;4345:parentR,int fd,u8 "
         "kind,size_t inputN;4346:caller end;"
         "caller end:R,u8 RawOperationBranch,u8 stage,bool Error,[ErrorCode,i64 "
         "detail];"
         "kind1emit2status3outcome4greeting5prompt6newline7warning";
}
} // namespace blackbird
