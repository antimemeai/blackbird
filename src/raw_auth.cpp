#include "blackbird/raw_auth.hpp"

#include "blackbird/journal.hpp"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <limits>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

namespace blackbird {
namespace {
using detail::raw_native_field;
using detail::raw_string_field;
struct ErrnoGuard {
  int value = errno;
  ~ErrnoGuard() { errno = value; }
};
constexpr std::uint64_t ref_size = sizeof(IdentityBytes) + sizeof(std::uint64_t);
constexpr std::uint64_t terminal_size = raw_envelope_size + ref_size +
                                        2 * sizeof(std::uint8_t) + sizeof(ErrorCode) +
                                        sizeof(std::int64_t);
constexpr std::uint64_t scalar_size =
    raw_envelope_size + ref_size + sizeof(std::uint8_t) + sizeof(std::int64_t);
constexpr std::uint64_t boolean_size =
    raw_envelope_size + ref_size + sizeof(std::uint8_t) + sizeof(bool);
constexpr std::uint64_t branch_size =
    raw_envelope_size + ref_size + sizeof(std::uint8_t);
constexpr std::uint64_t exchange_size = 2 * raw_envelope_size + 3 * ref_size +
                                        2 * sizeof(std::uint8_t) + sizeof(ErrorCode) +
                                        sizeof(std::int64_t);
constexpr std::uint64_t root_credit =
    raw_envelope_size + 2 * sizeof(std::uint8_t) + sizeof(bool) + terminal_size;
constexpr std::uint64_t parent_credit = root_credit + ref_size;
constexpr std::uint64_t selected_child_credit = parent_credit + boolean_size;
constexpr std::uint64_t browser_save_credit =
    parent_credit + 2 * sizeof(std::int64_t) + 6 * scalar_size + raw_auth_save_credit;
constexpr std::uint64_t callback_size = 2 * (branch_size + sizeof(std::uint8_t));
std::uint64_t add(std::uint64_t left, std::uint64_t right) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    throw Error{ErrorCode::overflow};
  return left + right;
}
std::uint64_t begin_size(const RawAuthEntry &entry) {
  return raw_envelope_size + 2 * sizeof(std::uint8_t) + sizeof(bool) +
         (entry.parent && entry.parent->reference.ordinal ? ref_size : 0) +
         (entry.expected ? 2 * sizeof(std::int64_t) : 0);
}
std::uint64_t action_credit(const RawAuthEntry &entry) {
  const auto base = begin_size(entry) + terminal_size;
  switch (entry.action) {
  case RawAuthAction::descriptor:
  case RawAuthAction::catalog:
  case RawAuthAction::status:
    return base;
  case RawAuthAction::registration:
    return base + 2 * scalar_size;
  case RawAuthAction::binding:
    return base + scalar_size;
  case RawAuthAction::selected:
    return base + boolean_size;
  case RawAuthAction::set_key:
  case RawAuthAction::logout:
    return base + 2 * scalar_size + raw_auth_save_credit;
  case RawAuthAction::use:
    return base + scalar_size + raw_auth_save_credit;
  case RawAuthAction::add_provider:
    return base + raw_auth_save_credit;
  case RawAuthAction::save_oauth:
    return base +
           (entry.expected && entry.expected->selection_revision < 0 ? 6 : 7) *
               scalar_size +
           raw_auth_save_credit;
  case RawAuthAction::device_begin:
    return base + 7 * scalar_size + raw_auth_save_credit + exchange_size;
  case RawAuthAction::device_poll:
    return base + 12 * scalar_size + 2 * raw_auth_save_credit + exchange_size +
           branch_size;
  case RawAuthAction::resolve_owned:
    return base + (entry.binding_value ? 9 : 7) * scalar_size +
           2 * raw_auth_save_credit + exchange_size + 3 * branch_size;
  case RawAuthAction::resolve_codex:
    return base + boolean_size + (entry.codex_probe ? selected_child_credit : 0) +
           5 * exchange_size + branch_size;
  case RawAuthAction::browser_login:
    return base + parent_credit + (parent_credit + 2 * scalar_size) +
           browser_save_credit + 2 * scalar_size + 4 * exchange_size +
           3 * callback_size;
  case RawAuthAction::command:
    return base + (entry.secret_prompt ? callback_size : 0);
  }
  throw Error{ErrorCode::invalid_range};
}
std::uint64_t input_size(RawAuthLabels labels) {
  return add(
      add(raw_envelope_size + ref_size + sizeof(std::uint8_t) + 2 * sizeof(std::size_t),
          labels.provider.size()),
      labels.account.size());
}
std::uint64_t action_data(const RawAuthEntry &entry) {
  if (entry.action == RawAuthAction::catalog || entry.action == RawAuthAction::status)
    return 0;
  auto bytes = input_size(entry.labels);
  if (entry.action == RawAuthAction::binding)
    bytes = add(bytes, add(raw_envelope_size + ref_size + 5 * sizeof(std::size_t) +
                               1048576 + 80,
                           entry.labels.provider.size()));
  if (entry.action == RawAuthAction::resolve_owned)
    bytes = add(bytes, raw_envelope_size + ref_size + sizeof(std::uint8_t) +
                           sizeof(std::size_t) + 1048576 + 20);
  if (entry.action == RawAuthAction::browser_login)
    bytes = add(bytes, add(input_size({entry.labels.provider, {}}),
                           2 * input_size(entry.labels)));
  if (entry.action == RawAuthAction::resolve_codex && entry.codex_probe)
    bytes = add(bytes, input_size({"openai", {}}));
  return bytes;
}
RawCapacityClaim require_claim(Result<RawCapacityClaim> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
std::uint64_t publish(std::uint32_t site, std::span<const ByteView> fields,
                      RawCapacityClaim &claim,
                      RawPartition partition = RawPartition::control) {
  const auto result = raw_emit(site, fields, partition, &claim);
  if (!result.has_value()) {
    raw_fence(result.error());
    throw result.error();
  }
  return result.value();
}
void terminal(std::uint32_t site, std::span<const ByteView> fields,
              RawCapacityClaim &claim) noexcept {
  const auto result = raw_emit(site, fields, RawPartition::control, &claim);
  if (!result.has_value())
    raw_fence(result.error());
}
} // namespace

RawAuthTrace::RawAuthTrace(RawAuthEntry entry) : entry_(entry) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return;
  const auto bytes = action_credit(entry);
  control_.emplace(require_claim(entry.parent && entry.parent->control
                                     ? entry.parent->control->split(bytes)
                                     : raw_claim_control(bytes)));
  terminal_.emplace(require_claim(control_->split(terminal_size)));
}
RawAuthTrace::~RawAuthTrace() { finish(6); }
std::uint64_t RawAuthTrace::emit(std::uint32_t site, std::span<const ByteView> fields,
                                 RawCapacityClaim &claim) {
  return publish(site, fields, claim,
                 data_ && &claim == &*data_ ? RawPartition::data
                                            : RawPartition::control);
}
void RawAuthTrace::start(std::uint64_t extra_data) {
  const ErrnoGuard guard;
  if (!control_)
    return;
  const bool parent = entry_.parent && entry_.parent->reference.ordinal;
  const auto action = static_cast<std::uint8_t>(entry_.action);
  const std::uint8_t expected = entry_.expected ? 1 : entry_.binding_value ? 2 : 0;
  std::array<ByteView, 7> fields{};
  std::size_t count = 0;
  fields[count++] = raw_native_field(action);
  fields[count++] = raw_native_field(parent);
  if (parent) {
    fields[count++] = entry_.parent->reference.lane;
    fields[count++] = raw_native_field(entry_.parent->reference.ordinal);
  }
  fields[count++] = raw_native_field(expected);
  if (entry_.expected) {
    fields[count++] = raw_native_field(entry_.expected->account_revision);
    fields[count++] = raw_native_field(entry_.expected->selection_revision);
  }
  action_ = {raw_lane_epoch(), emit(1600, std::span{fields}.first(count), *control_)};
  const auto bytes = add(action_data(entry_), extra_data);
  if (!bytes)
    return;
  data_.emplace(require_claim(entry_.parent && entry_.parent->data
                                  ? entry_.parent->data->split(bytes)
                                  : raw_claim_data(bytes)));
  const std::uint8_t kind = 0;
  const auto provider_size = entry_.labels.provider.size(),
             account_size = entry_.labels.account.size();
  const std::array input{ByteView{action_.lane},
                         raw_native_field(action_.ordinal),
                         raw_native_field(kind),
                         raw_native_field(provider_size),
                         raw_string_field(entry_.labels.provider),
                         raw_native_field(account_size),
                         raw_string_field(entry_.labels.account)};
  (void)emit(1601, input, *data_);
}
RawAuthParent RawAuthTrace::child() noexcept {
  return {action_, control_ ? &*control_ : nullptr, data_ ? &*data_ : nullptr};
}
RawControlClaim RawAuthTrace::split(std::uint64_t bytes) {
  if (!control_)
    throw Error{ErrorCode::invalid_range};
  return require_claim(control_->split(bytes));
}
void RawAuthTrace::scalar(RawAuthField field, std::int64_t value) {
  const ErrnoGuard guard;
  if (!control_)
    return;
  const auto tag = static_cast<std::uint8_t>(field);
  const std::array fields{ByteView{action_.lane}, raw_native_field(action_.ordinal),
                          raw_native_field(tag), raw_native_field(value)};
  (void)emit(1613, fields, *control_);
}
void RawAuthTrace::boolean(std::uint8_t field, bool value) {
  const ErrnoGuard guard;
  if (!control_)
    return;
  const std::array fields{ByteView{action_.lane}, raw_native_field(action_.ordinal),
                          raw_native_field(field), raw_native_field(value)};
  (void)emit(1614, fields, *control_);
}
void RawAuthTrace::branch(RawAuthSource source) {
  const ErrnoGuard guard;
  if (!control_)
    return;
  const std::array fields{ByteView{action_.lane}, raw_native_field(action_.ordinal),
                          raw_native_field(source.value)};
  (void)emit(source.site, fields, *control_);
}
void RawAuthTrace::callback(std::uint8_t operation, std::uint8_t boundary) {
  const ErrnoGuard guard;
  if (!control_)
    return;
  const std::array fields{ByteView{action_.lane}, raw_native_field(action_.ordinal),
                          raw_native_field(operation), raw_native_field(boundary)};
  (void)emit(1611, fields, *control_);
}
void RawAuthTrace::selected_account(std::string_view account) {
  const ErrnoGuard guard;
  if (!data_)
    return;
  const std::uint8_t kind = 1;
  const auto size = account.size();
  const std::array fields{ByteView{action_.lane}, raw_native_field(action_.ordinal),
                          raw_native_field(kind), raw_native_field(size),
                          raw_string_field(account)};
  (void)emit(1601, fields, *data_);
}
void RawAuthTrace::binding_strings(std::span<const std::string_view, 5> values) {
  const ErrnoGuard guard;
  if (!data_)
    return;
  std::array<std::size_t, 5> sizes{};
  std::array<ByteView, 12> fields{};
  fields[0] = action_.lane;
  fields[1] = raw_native_field(action_.ordinal);
  for (std::size_t index = 0; index < values.size(); ++index) {
    sizes[index] = values[index].size();
    fields[2 + 2 * index] = raw_native_field(sizes[index]);
    fields[3 + 2 * index] = raw_string_field(values[index]);
  }
  (void)emit(1602, fields, *data_);
}
void RawAuthTrace::descriptor_strings(std::span<const std::string_view, 4> values) {
  const ErrnoGuard guard;
  if (!data_)
    return;
  std::array<std::size_t, 4> sizes{};
  std::array<ByteView, 10> fields{};
  fields[0] = action_.lane;
  fields[1] = raw_native_field(action_.ordinal);
  for (std::size_t index = 0; index < values.size(); ++index) {
    sizes[index] = values[index].size();
    fields[2 + 2 * index] = raw_native_field(sizes[index]);
    fields[3 + 2 * index] = raw_string_field(values[index]);
  }
  (void)emit(1620, fields, *data_);
}
void RawAuthTrace::finish(std::uint8_t branch, const Error *error) noexcept {
  const ErrnoGuard guard;
  if (!terminal_)
    return;
  if (action_.ordinal) {
    std::array fields{ByteView{action_.lane},
                      raw_native_field(action_.ordinal),
                      raw_native_field(branch),
                      raw_native_field(stage_),
                      error ? raw_native_field(error->code) : ByteView{},
                      error ? raw_native_field(error->detail) : ByteView{}};
    terminal(1608, std::span{fields}.first(error ? 6 : 4), *terminal_);
  }
  data_.reset();
  control_.reset();
  terminal_.reset();
}
RawAuthExchange::RawAuthExchange(RawAuthTrace &action, RawAuthExchangeSites sites)
    : action_(&action), begin_site_(sites.begin), return_site_(sites.returned) {
  if (action.enabled())
    credit_.emplace(action.split(exchange_size));
}
RawAuthExchange::~RawAuthExchange() { finish(6); }
void RawAuthExchange::start(std::uint8_t operation) {
  const ErrnoGuard guard;
  if (!credit_)
    return;
  const auto parent = action_->reference();
  const std::array fields{ByteView{parent.lane}, raw_native_field(parent.ordinal),
                          raw_native_field(operation)};
  request_ = {raw_lane_epoch(), publish(begin_site_, fields, *credit_)};
}
void RawAuthExchange::returned(const int *status) noexcept {
  const ErrnoGuard guard;
  if (!credit_ || !request_.ordinal)
    return;
  const auto parent = action_->reference();
  const std::uint8_t branch = 1;
  const std::array fields{
      ByteView{parent.lane},    raw_native_field(parent.ordinal),
      ByteView{request_.lane},  raw_native_field(request_.ordinal),
      raw_native_field(branch), status ? raw_native_field(*status) : ByteView{}};
  terminal(return_site_, std::span{fields}.first(status ? 6 : 5), *credit_);
  credit_.reset();
}
void RawAuthExchange::finish(std::uint8_t branch, const Error *error) noexcept {
  const ErrnoGuard guard;
  if (!credit_ || !request_.ordinal)
    return;
  const auto parent = action_->reference();
  const std::array fields{ByteView{parent.lane},
                          raw_native_field(parent.ordinal),
                          ByteView{request_.lane},
                          raw_native_field(request_.ordinal),
                          raw_native_field(branch),
                          error ? raw_native_field(error->code) : ByteView{},
                          error ? raw_native_field(error->detail) : ByteView{}};
  terminal(return_site_, std::span{fields}.first(error ? 7 : 5), *credit_);
  credit_.reset();
}
RawAuthSave::RawAuthSave(RawAuthTrace *action) : action_(action) {
  if (action && action->enabled())
    credit_.emplace(action->split(raw_auth_save_credit));
}
RawAuthSave::~RawAuthSave() { finish(6); }
void RawAuthSave::start() {
  const ErrnoGuard guard;
  if (!credit_)
    return;
  const auto parent = action_->reference();
  const auto mutation = static_cast<std::uint8_t>(action_->action());
  const std::array fields{ByteView{parent.lane}, raw_native_field(parent.ordinal),
                          raw_native_field(mutation)};
  request_ = {raw_lane_epoch(), publish(1605, fields, *credit_)};
}
void RawAuthSave::finish(std::uint8_t branch, const Error *error) noexcept {
  const ErrnoGuard guard;
  if (!credit_ || !request_.ordinal)
    return;
  const auto parent = action_->reference();
  const std::array fields{ByteView{parent.lane},
                          raw_native_field(parent.ordinal),
                          ByteView{request_.lane},
                          raw_native_field(request_.ordinal),
                          raw_native_field(stage_),
                          raw_native_field(branch),
                          error ? raw_native_field(error->code) : ByteView{},
                          error ? raw_native_field(error->detail) : ByteView{}};
  terminal(1606, std::span{fields}.first(error ? 8 : 6), *credit_);
  credit_.reset();
}
std::string_view raw_auth_description() noexcept {
  return "auth1600 BEGIN: action u8,parent bool,[parent lane16/ordinal u64],expected "
         "kind u8,[offered account/selection i64]; auth1601 INPUT: action Ref,kind "
         "u8,native lengths and nonsecret label bytes; auth1602 BINDING: action "
         "Ref,five native length/string fields; auth1603/1604/1607 source: action "
         "Ref,branch u8; auth1605 SAVE_BEGIN: action Ref,mutation u8; auth1606 "
         "SAVE_RETURN: action/save Ref,completed stage u8,branch u8,[ErrorCode/i64]; "
         "auth1608 END: action Ref,branch u8,source stage u8,[ErrorCode/i64]; "
         "branches1 return,2 Error,3 allocation,4 capacity,5 unknown,6 unresolved; "
         "auth1609/1610 private HTTP whole begin/return: action Ref,operation u8 / "
         "action/request Ref,branch,[int status on return or ErrorCode/i64 on Error]; "
         "auth1611 callback boundary: action Ref,operation u8,boundary u8; auth1612 "
         "actual auth/application Ref pair; auth1613 acquired native scalar: action "
         "Ref,field u8,i64; auth1614 acquired native bool: action Ref,field u8,bool; "
         "auth1615/1616 private whole operation and auth1617/1618 private file "
         "begin/return use HTTP layout without status; auth1619 standalone purpose: "
         "native length/static bytes; auth1620 public descriptor: action Ref,four "
         "native length/string fields. No private payloads. Save stages0 entered,1 "
         "encoded,2 sizechecked,3 entropy,4 opened,5 written,6 filesynced,7 renamed,8 "
         "dirsynced.\n"
         " Action tags1 descriptor,2 catalog,3 status,4 registration,5 binding,6 "
         "selected,7 set_key,8 use,9 logout,10 add_provider,11 save_oauth,12 "
         "device_begin,13 device_poll,14 owned_resolution,15 Codex_resolution,16 "
         "browser_login,17 command. INPUT kind0 provider/account; kind1 selected "
         "account. BINDING strings provider/account/kind/base_url/protocol. Offered "
         "descriptor strings id/base_url/protocol/environment NAME. Scalar tags1 "
         "account_revision,2 selection_revision,3 expected_account,4 "
         "expected_selection,5 token_clock,6 token_expiry,7 token_expires_at,8 "
         "device_expiry,9 device_interval,10 pending_selection,11 current_selection,12 "
         "poll_expiry,13 poll_check_next,14 poll_interval,15 poll_slow_interval,16 "
         "poll_clamped_interval,17 poll_next,18 device_expiry_clock,19 "
         "device_next_clock,20 poll_clock,21 retry_clock,22 poll_wait,23 "
         "refresh_expiry,24 refresh_expiry_clock,25 refresh_after,26 "
         "refresh_retry_clock,27 refresh_next,28 binding_expected_revision,29 "
         "binding_current_revision,30 binding_revision,31 registration_revision,32 "
         "registration_selection,33 browser_revision,34 browser_clock. Bool tags1 "
         "selected,2 Codex refresh. HTTP operation tags1 device_begin,2 device_poll,3 "
         "refresh,4 browser_token,5 JWKS. Private operation tags1 initialize RPC,2 "
         "initialized notification,3 auth RPC,4 PKCE,5 JWT. File stages1 before,2 "
         "latest. Callback boundaries1 enter,2 return,3 refuse; browser operations1 "
         "initial notice,2 whole native callback wait,3 paste notice,4 secret input,5 "
         "final notice; command operation1 secret input. Refresh dispositions1 "
         "accepted,2 unknown,3 retry,4 reauthorize; device outcomes1 selection "
         "conflict,2 expired,3 too early,4 transient HTTP,5 pending,6 slow_down,7 "
         "denied,8 ready; owned outcome1 resolved; Codex outcomes1 owned selection,2 "
         "compatibility. Terminal source stages100 entered,109 returned,otherwise "
         "named scalar expression being acquired.\n";
}

namespace {
struct AuthFd {
  int value = -1;
  explicit AuthFd(int fd = -1) : value(fd) {}
  ~AuthFd() {
    if (value >= 0)
      (void)::close(value);
  }
  AuthFd(const AuthFd &) = delete;
  AuthFd &operator=(const AuthFd &) = delete;
  int release() noexcept { return std::exchange(value, -1); }
};
void native_ok(bool success) {
  if (!success)
    throw Error{ErrorCode::io, errno};
}
void directory_mode(int fd, bool final) {
  struct stat state{};
  native_ok(::fstat(fd, &state) == 0);
  if (!S_ISDIR(state.st_mode))
    throw Error{ErrorCode::conflict};
  const auto uid = ::geteuid();
  if (final) {
    if (state.st_uid != uid || (state.st_mode & 07777) != 0700)
      throw Error{ErrorCode::conflict};
  } else if ((state.st_uid != 0 && state.st_uid != uid) ||
             ((state.st_mode & 0022) != 0 &&
              !(state.st_uid == 0 && (state.st_mode & S_ISVTX) != 0)))
    throw Error{ErrorCode::conflict};
}
void valid_path(const std::filesystem::path &path) {
  if (!path.is_absolute() || path == path.root_path())
    throw Error{ErrorCode::invalid_range};
  for (const auto &part : path.relative_path())
    if (part == "." || part == ".." || part.empty())
      throw Error{ErrorCode::invalid_range};
}
struct AuthBoundary {
  struct stat anchor{};
  std::vector<std::filesystem::path> tail;
};
AuthBoundary credential_boundary(const std::filesystem::path &path) {
  AuthFd current{::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
  native_ok(current.value >= 0);
  const auto components = path.relative_path();
  auto iterator = components.begin();
  for (; iterator != components.end(); ++iterator) {
    AuthFd next{::openat(current.value, iterator->c_str(),
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    if (next.value < 0) {
      if (errno == ENOENT)
        break;
      throw Error{errno == ELOOP || errno == ENOTDIR ? ErrorCode::conflict
                                                     : ErrorCode::io,
                  errno};
    }
    directory_mode(next.value, false);
    (void)::close(current.value);
    current.value = next.release();
  }
  AuthBoundary boundary;
  native_ok(::fstat(current.value, &boundary.anchor) == 0);
  for (; iterator != components.end(); ++iterator)
    boundary.tail.push_back(*iterator);
  return boundary;
}
void exclude_credentials(int directory, const AuthBoundary &boundary,
                         std::filesystem::path::iterator next,
                         const std::filesystem::path::iterator &end) {
  struct stat current{};
  native_ok(::fstat(directory, &current) == 0);
  if (current.st_dev != boundary.anchor.st_dev ||
      current.st_ino != boundary.anchor.st_ino)
    return;
  for (const auto &part : boundary.tail) {
    if (next == end || *next != part)
      return;
    ++next;
  }
  throw Error{ErrorCode::conflict};
}
int directory_walk(const std::filesystem::path &path, const AuthBoundary &boundary) {
  AuthFd current{::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
  native_ok(current.value >= 0);
  directory_mode(current.value, false);
  const auto components = path.relative_path();
  for (auto iterator = components.begin(); iterator != components.end(); ++iterator) {
    const auto &component = *iterator;
    exclude_credentials(current.value, boundary, iterator, components.end());
    int next = ::openat(current.value, component.c_str(),
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (next < 0 && errno == ENOENT) {
      if (::mkdirat(current.value, component.c_str(), 0700) != 0 && errno != EEXIST)
        throw Error{ErrorCode::io, errno};
      next = ::openat(current.value, component.c_str(),
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    }
    if (next < 0)
      throw Error{errno == ELOOP || errno == ENOTDIR ? ErrorCode::conflict
                                                     : ErrorCode::io,
                  errno};
    AuthFd checked{next};
    directory_mode(next, std::next(iterator) == components.end());
    (void)::close(current.value);
    current.value = checked.release();
  }
  exclude_credentials(current.value, boundary, components.end(), components.end());
  return current.release();
}
void final_directory(int parent, const char *name, AuthFd &result) {
  result.value =
      ::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (result.value < 0)
    throw Error{errno == ELOOP || errno == ENOTDIR ? ErrorCode::conflict
                                                   : ErrorCode::io,
                errno};
  directory_mode(result.value, true);
}
template <class T>
void header_field(std::array<std::byte, 40> &body, std::size_t offset, T value) {
  std::memcpy(body.data() + offset, &value, sizeof(value));
}
template <class T>
T header_read(const std::array<std::byte, 40> &body, std::size_t offset) {
  T value{};
  std::memcpy(&value, body.data() + offset, sizeof(value));
  return value;
}
constexpr std::array<char, 8> auth_magic{'B', 'B', 'A', 'U', 'T', 'H', '1', '\0'};
EnvironmentId read_environment(int root) {
  AuthFd environment;
  final_directory(root, "environment", environment);
  AuthFd file{
      ::openat(environment.value, "identity", O_RDONLY | O_NOFOLLOW | O_CLOEXEC)};
  if (file.value < 0)
    throw Error{errno == ENOENT  ? ErrorCode::incomplete
                : errno == ELOOP ? ErrorCode::conflict
                                 : ErrorCode::io,
                errno};
  struct stat state{};
  native_ok(::fstat(file.value, &state) == 0);
  if (!S_ISREG(state.st_mode) || state.st_uid != ::geteuid() || state.st_nlink != 1 ||
      (state.st_mode & 07777) != 0600)
    throw Error{ErrorCode::conflict};
  if (state.st_size != 40)
    throw Error{ErrorCode::corrupt};
  std::array<std::byte, 40> body{};
  std::size_t done = 0;
  while (done < body.size()) {
    const auto count = ::read(file.value, body.data() + done, body.size() - done);
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0)
      throw Error{ErrorCode::io, errno};
    if (count == 0)
      throw Error{ErrorCode::corrupt};
    done += static_cast<std::size_t>(count);
  }
  if (std::memcmp(body.data(), auth_magic.data(), auth_magic.size()) != 0 ||
      header_read<std::uint16_t>(body, 8) != 1 ||
      header_read<std::uint16_t>(body, 10) != 40 ||
      header_read<std::uint32_t>(body, 12) != 0x01020304 ||
      header_read<std::uint32_t>(body, 36) != 0 ||
      header_read<std::uint32_t>(body, 32) != crc32c(ByteView{body}.first(32)))
    throw Error{ErrorCode::corrupt};
  IdentityBytes identity{};
  std::memcpy(identity.data(), body.data() + 16, identity.size());
  const auto parsed = EnvironmentId::from_bytes(identity);
  if (!parsed.has_value())
    throw Error{ErrorCode::corrupt};
  return parsed.value();
}
void create_environment(int root) {
  IdentityBytes random{};
  native_ok(::getentropy(random.data(), random.size()) == 0);
  const auto identity = EnvironmentId::from_bytes(random);
  if (!identity.has_value())
    throw identity.error();
  std::string scratch = ".environment.";
  constexpr char hex[] = "0123456789abcdef";
  for (const auto byte : random) {
    const auto value = std::to_integer<unsigned char>(byte);
    scratch += hex[value >> 4U];
    scratch += hex[value & 15U];
  }
  native_ok(::mkdirat(root, scratch.c_str(), 0700) == 0);
  bool published = false;
  try {
    AuthFd directory;
    final_directory(root, scratch.c_str(), directory);
    AuthFd file{::openat(directory.value, "identity",
                         O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
    native_ok(file.value >= 0);
    std::array<std::byte, 40> body{};
    std::memcpy(body.data(), auth_magic.data(), auth_magic.size());
    header_field<std::uint16_t>(body, 8, 1);
    header_field<std::uint16_t>(body, 10, 40);
    header_field<std::uint32_t>(body, 12, 0x01020304);
    std::memcpy(body.data() + 16, random.data(), random.size());
    header_field<std::uint32_t>(body, 32, crc32c(ByteView{body}.first(32)));
    std::size_t done = 0;
    while (done < body.size()) {
      const auto count = ::write(file.value, body.data() + done, body.size() - done);
      if (count < 0 && errno == EINTR)
        continue;
      if (count <= 0)
        throw Error{ErrorCode::io, errno};
      done += static_cast<std::size_t>(count);
    }
    native_ok(::fsync(file.value) == 0);
    native_ok(::fsync(directory.value) == 0);
#if defined(__APPLE__)
    const auto renamed =
        ::renameatx_np(root, scratch.c_str(), root, "environment", RENAME_EXCL);
#else
    const auto renamed =
        ::syscall(SYS_renameat2, root, scratch.c_str(), root, "environment", 1U);
#endif
    if (renamed == 0) {
      published = true;
      native_ok(::fsync(root) == 0);
    } else if (errno != EEXIST && errno != ENOTEMPTY)
      throw Error{ErrorCode::io, errno};
    if (!published) {
      native_ok(::unlinkat(directory.value, "identity", 0) == 0);
      native_ok(::unlinkat(root, scratch.c_str(), AT_REMOVEDIR) == 0);
    }
  } catch (...) {
    if (!published) {
      AuthFd directory{::openat(root, scratch.c_str(),
                                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
      if (directory.value >= 0)
        (void)::unlinkat(directory.value, "identity", 0);
      (void)::unlinkat(root, scratch.c_str(), AT_REMOVEDIR);
    }
    throw;
  }
}
} // namespace
Result<std::unique_ptr<RawAuthEnvironment>>
RawAuthEnvironment::open(const ProviderAuthConfig &config) {
  try {
    if (config.directory.empty())
      throw Error{ErrorCode::invalid_range};
    auto path = config.metrics_directory;
    if (path.empty()) {
      if (const auto *setting = std::getenv("BLACKBIRD_AUTH_METRICS_HOME");
          setting && *setting)
        path = setting;
      else
        path = config.directory.parent_path() /
               (config.directory.filename().string() + ".metrics");
    }
    path = std::filesystem::absolute(path);
    const auto credentials = std::filesystem::absolute(config.directory);
    valid_path(path);
    valid_path(credentials);
    const auto mismatch =
        std::mismatch(path.begin(), path.end(), credentials.begin(), credentials.end());
    if (mismatch.second == credentials.end())
      throw Error{ErrorCode::conflict};
    const auto boundary = credential_boundary(credentials);
    AuthFd root{directory_walk(path, boundary)};
    AuthFd environment{::openat(root.value, "environment",
                                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    if (environment.value < 0) {
      if (errno != ENOENT)
        throw Error{errno == ELOOP || errno == ENOTDIR ? ErrorCode::conflict
                                                       : ErrorCode::io,
                    errno};
      create_environment(root.value);
    }
    const auto identity = read_environment(root.value);
    if (::mkdirat(root.value, "raw", 0700) != 0 && errno != EEXIST)
      throw Error{ErrorCode::io, errno};
    AuthFd raw;
    final_directory(root.value, "raw", raw);
    return Result<std::unique_ptr<RawAuthEnvironment>>::success(
        std::unique_ptr<RawAuthEnvironment>{
            new RawAuthEnvironment{root.release(), identity}});
  } catch (const Error &error) {
    return Result<std::unique_ptr<RawAuthEnvironment>>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<std::unique_ptr<RawAuthEnvironment>>::failure(
        {ErrorCode::allocation});
  } catch (const std::filesystem::filesystem_error &error) {
    return Result<std::unique_ptr<RawAuthEnvironment>>::failure(
        {ErrorCode::io, error.code().value()});
  }
}
RawAuthEnvironment::~RawAuthEnvironment() {
  if (directory_ >= 0)
    (void)::close(directory_);
}
std::string RawAuthEnvironment::runtime_directory() const {
#if defined(__APPLE__)
  std::array<char, PATH_MAX> path{};
  native_ok(::fcntl(directory_, F_GETPATH, path.data()) == 0);
  std::string result{path.data()};
  verify_runtime_directory(result);
  return result;
#else
  return "/proc/self/fd/" + std::to_string(directory_);
#endif
}
void RawAuthEnvironment::verify_runtime_directory(std::string_view path) const {
  const ErrnoGuard guard;
  directory_mode(directory_, true);
#if defined(__APPLE__)
  const std::string owned_path{path};
  struct stat held{}, offered{}, opened{}, after{};
  native_ok(::fstat(directory_, &held) == 0);
  native_ok(::lstat(owned_path.c_str(), &offered) == 0);
  AuthFd candidate{
      ::open(owned_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
  native_ok(candidate.value >= 0);
  directory_mode(candidate.value, true);
  native_ok(::fstat(candidate.value, &opened) == 0);
  native_ok(::lstat(owned_path.c_str(), &after) == 0);
  for (const auto *state : {&offered, &opened, &after})
    if (state->st_dev != held.st_dev || state->st_ino != held.st_ino ||
        state->st_uid != held.st_uid || state->st_mode != held.st_mode)
      throw Error{ErrorCode::conflict};
#else
  const auto expected = "/proc/self/fd/" + std::to_string(directory_);
  if (path != expected)
    throw Error{ErrorCode::conflict};
#endif
}
} // namespace blackbird
