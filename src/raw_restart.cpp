#include "blackbird/raw_restart.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_file.hpp"
#include <cerrno>
#include <unistd.h>
namespace blackbird {
RawSessionCall raw_restart_call(RetainedState &root, std::uint8_t method,
                                const RawAdapterContext *context, RawCallSites sites,
                                std::uint64_t terminal_extra) {
  const auto cursor = root.cursor();
  return RawSessionCall{sites.request, sites.terminal, context,
                        std::array{detail::raw_native_field(method),
                                   ByteView{cursor.journal.bytes()},
                                   detail::raw_native_field(cursor.sequence),
                                   detail::raw_native_field(cursor.end_offset)},
                        terminal_extra};
}
RawEventRef raw_restart_source(RawSessionCall &call, std::uint8_t kind,
                               std::string_view text,
                               std::optional<RawCapacityClaim> &credit) {
  return call.source(kind, text, credit, 4402);
}
RawEventRef raw_restart_source(RawSessionCall &call, std::uint8_t kind,
                               std::string_view text) {
  auto credit = call.source_credit(text.size());
  return raw_restart_source(call, kind, text, credit);
}
std::string raw_restart_hex(RawSessionCall &call, const IdentityBytes &identity) {
  auto credit = call.source_credit(32);
  const auto text = hex_identity(identity);
  (void)raw_restart_source(call, 13, text, credit);
  return text;
}
Result<Value> raw_restart_packet(RawSessionCall &call, std::uint8_t kind,
                                 const ImmutableBytes &payload) {
  const ValueLimits limits;
  if (payload.size() > limits.bytes)
    return Result<Value>::failure({ErrorCode::capacity});
  auto credit = call.source_credit(payload.size());
  auto acquired = payload.read();
  if (!acquired.has_value())
    return Result<Value>::failure(acquired.error());
  const std::string_view text{reinterpret_cast<const char *>(acquired.value().data()),
                              acquired.value().size()};
  (void)raw_restart_source(call, kind, text, credit);
  return decode_packet(acquired.value(), limits);
}
void raw_restart_result(RawSessionCall &call, const Result<void> &result) {
  call.require_capture();
  const bool accepted = result.has_value();
  if (accepted)
    call.finish(RawOperationBranch::returned, nullptr,
                std::array{detail::raw_native_field(accepted)});
  else {
    const auto &error = result.error();
    call.finish(RawOperationBranch::returned, nullptr,
                std::array{detail::raw_native_field(accepted),
                           detail::raw_native_field(error.code),
                           detail::raw_native_field(error.detail)});
  }
  call.require_capture();
}
std::string raw_restart_argument(RawSessionCall &call, std::size_t index,
                                 std::string_view value) {
  const auto extent = raw_session_bound(value.size(), 1);
  auto credit = call.bytes_credit(raw_session_bound(8, extent));
  if (credit) {
    (void)raw_session_record(
        4466, call.reference(),
        std::array{detail::raw_native_field(index), detail::raw_native_field(extent),
                   ByteView{reinterpret_cast<const std::byte *>(value.data()), extent}},
        RawPartition::data, &*credit);
    call.require_capture();
  }
  return std::string{value};
}
std::string raw_restart_path(RawSessionCall &call, const std::filesystem::path &path) {
  auto credit = call.source_credit(path.native().size());
  const auto text = path.string();
  (void)raw_restart_source(call, 6, text, credit);
  return text;
}
std::filesystem::path raw_restart_canonical(RawSessionCall &owner,
                                            const std::filesystem::path &path) {
  const auto source = raw_restart_source(owner, 6, path.native());
  const auto parent = owner.context();
  const std::uint8_t method = 1;
  RawSessionCall call{4462, 4463, &parent,
                      std::array{detail::raw_native_field(method),
                                 ByteView{source.lane},
                                 detail::raw_native_field(source.ordinal)},
                      36};
  return call.run([&] {
    std::error_code error;
    RawEventRef acquired{};
    const auto context = call.context();
    auto returned = raw_file_weak_canonical(path, error, &context, &acquired);
    const int value = error.value();
    const auto *category = &error.category();
    call.require_capture();
    if (error)
      call.finish(RawOperationBranch::standard, nullptr,
                  std::array{detail::raw_native_field(value),
                             detail::raw_native_field(category)});
    else
      call.finish(RawOperationBranch::returned, nullptr,
                  std::array{detail::raw_native_field(value),
                             detail::raw_native_field(category),
                             ByteView{acquired.lane},
                             detail::raw_native_field(acquired.ordinal)});
    call.require_capture();
    if (error)
      throw std::filesystem::filesystem_error("weakly_canonical", path, error);
    return returned;
  });
}
bool raw_restart_regular(RawSessionCall &owner, const std::filesystem::path &path) {
  const auto source = raw_restart_source(owner, 6, path.native());
  const auto parent = owner.context();
  const std::uint8_t method = 2;
  RawSessionCall call{4462, 4463, &parent,
                      std::array{detail::raw_native_field(method),
                                 ByteView{source.lane},
                                 detail::raw_native_field(source.ordinal)},
                      13};
  return call.run([&] {
    std::error_code error;
    const bool returned = std::filesystem::is_regular_file(path, error);
    const int value = error.value();
    const auto *category = &error.category();
    call.require_capture();
    if (error && error != std::errc::no_such_file_or_directory &&
        error != std::errc::not_a_directory) {
      call.finish(RawOperationBranch::standard, nullptr,
                  std::array{detail::raw_native_field(value),
                             detail::raw_native_field(category)});
      call.require_capture();
      throw std::filesystem::filesystem_error("is_regular_file", path, error);
    }
    call.finish(RawOperationBranch::returned, nullptr,
                std::array{detail::raw_native_field(value),
                           detail::raw_native_field(category),
                           detail::raw_native_field(returned)});
    call.require_capture();
    return returned;
  });
}
void raw_restart_directory(RawSessionCall &call, std::string &path) {
  const auto extent = raw_session_bound(path.size(), 1);
  auto before = call.bytes_credit(raw_session_bound(0, extent));
  auto after = call.bytes_credit(raw_session_bound(5, extent));
  const auto request = raw_session_record(
      4464, call.reference(),
      std::array{detail::raw_native_field(extent),
                 ByteView{reinterpret_cast<const std::byte *>(path.c_str()), extent}},
      RawPartition::data, before ? &*before : nullptr);
  call.require_capture();
  const bool returned = ::mkdtemp(path.data()) != nullptr;
  const int error = errno;
  if (returned)
    (void)raw_session_record(
        4465, request,
        std::array{detail::raw_native_field(returned), detail::raw_native_field(extent),
                   ByteView{reinterpret_cast<const std::byte *>(path.c_str()), extent}},
        RawPartition::data, after ? &*after : nullptr);
  else
    (void)raw_session_record(
        4465, request,
        std::array{detail::raw_native_field(returned), detail::raw_native_field(error),
                   detail::raw_native_field(extent),
                   ByteView{reinterpret_cast<const std::byte *>(path.c_str()), extent}},
        RawPartition::data, after ? &*after : nullptr);
  call.require_capture();
  errno = error;
  if (!returned)
    throw Error{ErrorCode::io, error};
}
void RawSuccessorHandoff::select(RawSessionCall &call) {
  if (!call.enabled())
    return;
  if (bank_)
    throw Error{ErrorCode::conflict};
  if (::getpid() != receiver_process_)
    throw Error{ErrorCode::wrong_incarnation};
  auto issuer = raw_current_thread();
  if (!issuer)
    throw Error{ErrorCode::wrong_incarnation};
  bank_ = call.control(137);
  issuer_ = std::move(issuer);
  selection_ = call.reference();
  process_ = ::getpid();
  creator_ = std::this_thread::get_id();
  joined_ = false;
}
RawSuccessorHandoff::RawSuccessorHandoff()
    : receiver_process_(::getpid()), receiver_thread_(std::this_thread::get_id()) {}
bool RawSuccessorHandoff::receiver() const noexcept {
  return ::getpid() == receiver_process_ &&
         std::this_thread::get_id() == receiver_thread_ &&
         (creator_ == receiver_thread_ || joined_);
}
Result<void> RawSuccessorHandoff::joined() noexcept {
  if (::getpid() != receiver_process_ || std::this_thread::get_id() != receiver_thread_)
    return Result<void>::failure({ErrorCode::wrong_incarnation});
  if (bank_)
    joined_ = true;
  return Result<void>::success();
}
Result<void> RawSuccessorHandoff::retire(std::uint8_t reason) noexcept {
  const int saved = errno;
  auto result = Result<void>::success();
  if (bank_) {
    if (::getpid() != process_ || !receiver())
      return Result<void>::failure({ErrorCode::wrong_incarnation});
    {
      std::optional<RawThreadBinding> binding;
      if (raw_current_thread() != issuer_)
        binding.emplace(issuer_);
      const auto captured = raw_publish_linked(
          4482, selection_, std::array{detail::raw_native_field(reason)},
          RawPartition::control, *bank_);
      if (!captured.has_value())
        result = Result<void>::failure(captured.error());
      bank_.reset();
    }
  }
  bank_.reset();
  issuer_.reset();
  selection_ = {};
  exec_ = {};
  errno = saved;
  return result;
}
RawSuccessorHandoff::~RawSuccessorHandoff() {
  if (bank_ && !receiver())
    std::terminate();
  (void)retire(3);
}
void RawSuccessorHandoff::consume() { unwrap(retire(1)); }
void RawSuccessorHandoff::prepare_exec(std::size_t count) {
  if (!bank_)
    return;
  if (exec_.ordinal)
    throw Error{ErrorCode::conflict};
  if (::getpid() != process_ || !receiver())
    throw Error{ErrorCode::wrong_incarnation};
  std::optional<RawThreadBinding> binding;
  if (raw_current_thread() != issuer_)
    binding.emplace(issuer_);
  exec_ = unwrap(raw_publish_linked(4480, selection_,
                                    std::array{detail::raw_native_field(count)},
                                    RawPartition::control, *bank_));
}
int RawSuccessorHandoff::execute(std::span<char *> arguments) {
  struct NativeErrno {
    int value = errno;
    ~NativeErrno() { errno = value; }
  } native_error;
  if (arguments.empty() || arguments.back() != nullptr)
    throw Error{ErrorCode::invalid_range};
  auto *const bank = bank_ ? &*bank_ : nullptr;
  if (!bank) {
    const int returned = ::execvp(arguments.front(), arguments.data());
    native_error.value = errno;
    return returned;
  }
  if (::getpid() != process_ || !receiver())
    throw Error{ErrorCode::wrong_incarnation};
  std::optional<RawThreadBinding> binding;
  if (raw_current_thread() != issuer_)
    binding.emplace(issuer_);
  if (!exec_.ordinal)
    prepare_exec(arguments.size() - 1);
  const int returned = ::execvp(arguments.front(), arguments.data());
  const int error = errno;
  native_error.value = error;
  const auto captured = raw_publish_linked(
      4481, exec_,
      std::array{detail::raw_native_field(returned), detail::raw_native_field(error)},
      RawPartition::control, *bank);
  const auto retired = retire(2);
  errno = error;
  (void)unwrap(captured);
  unwrap(retired);
  return returned;
}
std::string_view raw_restart_description() noexcept {
  return "restart4400-4499;E16;R=lane16,ordinal8;native,no padding;"
         "4400:parentR,u8 "
         "method(schedule1,resume2,restore3),cursor(journal16,sequence8,end8);"
         "4401:end;4402:R,u8 role,size_t "
         "N,original;role1note2token3origin4message5state"
         "6path7argv8operation9metadata10legacy11observation12marker13formattedID14admi"
         "ttedInput;"
         "4403:R,u8 "
         "predicate,bool;kind1pending2unresolved3resumePending4originMatch5injected;"
         "4404:R,nativeID16;4405:R,size_t N,locator;4406:R,u8 mutation;4407:R,bool "
         "resumed;"
         "4420:parentR,u8 method,generation16;4421:end;4422:R,u8 "
         "predicate,bool;4423:R,u8 mutation;"
         "4440:parentR,native RecoveryMode,cursor;4441:end;4442:R,size_t "
         "attempts;4443:end,[bool Result.has_value,[ErrorCode,i64 if false]];"
         "4444:R,attempt16,decision16,invocation16,bool opened;"
         "4445:end,[bool Result.has_value,[ErrorCode,i64 if false]];"
         "4446:R,u8 kind,acquired native identities/extents or failed Result "
         "ErrorCode,i64;4447:R,u8 predicate,bool;"
         "4448:R,u8 mutation;4449:R,native collected attempt16;4450:R,cursor,size_t "
         "count;4451:end,[returned cursor];"
         "4460:parentR,u8 handoff;4461:end;4462:R,u8 filesystem,operandR;"
         "4463:end,[native error_code.value,category pointer,[method1 returned path "
         "bodyR|method2 returned bool]];"
         "4464:R,size_t CStringN,mutable template;4465:requestR,bool result,[errno if "
         "false],"
         "size_t CStringN,actual changed template;4466:R,size_t index,CStringN,argv "
         "bytes;"
         "4467:R,size_t count,bool exec,bool plain;4480:R,size_t argc;"
         "4481:execR,int rc,errno;4482:R,u8 retirement(consumed1,failed2,abandoned3)";
}
} // namespace blackbird
