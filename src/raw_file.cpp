#include "blackbird/raw_file.hpp"
#include "blackbird/process_lifetime.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <limits.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace blackbird {
namespace {
using detail::raw_native_field;
template <class T> T take(Result<T> value) {
  if (!value.has_value())
    throw value.error();
  return std::move(value).value();
}
struct ErrnoGuard {
  int value = errno;
  ~ErrnoGuard() { errno = value; }
};
struct Fields {
  std::array<ByteView, 32> fields{};
  std::size_t count = 0;
  void bytes(ByteView value) { fields.at(count++) = value; }
  template <class T> void native(const T &value) { bytes(raw_native_field(value)); }
  void ref(const RawEventRef &value) {
    bytes(value.lane);
    native(value.ordinal);
  }
  std::span<const ByteView> view() const noexcept {
    return std::span{fields}.first(count);
  }
};
ByteView bytes(std::string_view value) noexcept {
  return std::as_bytes(std::span{value.data(), value.size()});
}
ByteView operand(const char *value) noexcept {
  return std::as_bytes(std::span{value, std::strlen(value) + 1});
}
std::uint64_t add(std::uint64_t a, std::size_t b) {
  if (b > UINT64_MAX - a)
    throw Error{ErrorCode::overflow};
  return a + b;
}
RawFileBranch file_branch(RawOperationBranch branch) noexcept {
  switch (branch) {
  case RawOperationBranch::returned:
    return RawFileBranch::returned;
  case RawOperationBranch::error:
    return RawFileBranch::error;
  case RawOperationBranch::allocation:
    return RawFileBranch::allocation;
  case RawOperationBranch::length:
  case RawOperationBranch::standard:
    return RawFileBranch::standard;
  case RawOperationBranch::nonstandard:
    return RawFileBranch::nonstandard;
  default:
    return RawFileBranch::unfinished;
  }
}
Fields terminal_fields(const RawEventRef &request, const RawFileBranch &branch,
                       const Error *error = nullptr, const int *filesystem = nullptr,
                       const std::size_t *count = nullptr) {
  Fields fields;
  fields.ref(request);
  fields.native(branch);
  if (error) {
    fields.native(error->code);
    fields.native(error->detail);
  } else if (filesystem)
    fields.native(*filesystem);
  else if (count)
    fields.native(*count);
  return fields;
}
void terminal(std::uint32_t site, RawEventRef request, RawFileBranch branch,
              RawControlToken &bank, const Error *error = nullptr,
              const int *filesystem = nullptr,
              const std::size_t *count = nullptr) noexcept {
  const auto fields = terminal_fields(request, branch, error, filesystem, count);
  (void)raw_publish(site, fields.view(), RawPartition::control, bank);
}
class Helper {
public:
  Helper(const RawAdapterContext *context, RawFileMethod method, std::size_t count) {
    if (!context || !context->parent || !raw_enabled())
      return;
    issuer_ = raw_current_thread();
    emission_.prepare(method == RawFileMethod::canonical ||
                              method == RawFileMethod::absolute ||
                              method == RawFileMethod::weak_canonical
                          ? 103
                          : 99);
    Fields fields;
    fields.ref(*context->parent);
    fields.native(method);
    fields.native(count);
    emission_.begin({3410, 3411}, fields.view());
  }
  ~Helper() { finish(RawFileBranch::unfinished); }
  bool enabled() const noexcept { return emission_.enabled(); }
  const RawEventRef &ref() const noexcept { return emission_.reference(); }
  void finish(RawOperationBranch branch, const Error *error = nullptr) noexcept {
    finish(file_branch(branch), error);
  }
  void finish_filesystem(int code) noexcept {
    finish(RawFileBranch::filesystem, nullptr, &code);
  }
  template <class F> decltype(auto) run(F &&body) {
    return raw_call_run(*this, std::forward<F>(body));
  }
  void count(std::size_t value) noexcept { count_ = value; }
  void flag(bool value) noexcept { flag_ = value; }
  void error_code(const std::error_code &value) noexcept {
    native_error_ = value.value();
  }

private:
  void finish(RawFileBranch branch, const Error *error = nullptr,
              const int *fs = nullptr) noexcept {
    if (!emission_.enabled())
      return;
    const auto request = ref();
    if (branch != RawFileBranch::returned) {
      const auto fields = terminal_fields(request, branch, error, fs);
      emission_.close(fields.view());
    } else {
      const ErrnoGuard preserved;
      const bool failed = native_error_.value_or(0) != 0;
      Fields fields;
      fields.ref(request);
      fields.native(branch);
      if (flag_)
        fields.native(*flag_);
      else
        fields.native(count_);
      if (native_error_) {
        fields.native(failed);
        if (failed)
          fields.native(*native_error_);
      }
      emission_.close(fields.view());
    }
  }
  RawThreadHandle issuer_;
  RawEmission emission_;
  std::size_t count_ = 0;
  std::optional<bool> flag_;
  std::optional<int> native_error_;
};
enum class Native : std::uint8_t {
  open = 1,
  read = 2,
  stat = 3,
  cwd = 4,
  canonical = 5,
  temporary = 6,
  mode = 7,
  write = 8,
  sync = 9,
  close = 10,
  rename = 11,
  unlink = 12,
  lstat = 13,
  link = 14,
  cloexec = 15
};
class Call {
public:
  Call(Helper &owner, std::uint32_t site, Native method, RawEventRef resource,
       std::uint64_t request_bytes, bool data, std::uint64_t output_bytes = 0)
      : helper_(owner.ref()), resource_(resource.ordinal ? resource : owner.ref()),
        site_(site), method_(method), request_data_(data) {
    if (!owner.enabled())
      return;
    const std::uint64_t returned =
        method == Native::read || method == Native::write || method == Native::link ? 53
        : method == Native::cwd || method == Native::canonical                      ? 46
        : method == Native::stat || method == Native::lstat
            ? 45 + sizeof(mode_t) + sizeof(off_t)
            : 49;
    control_.emplace(
        take(raw_claim_control(data ? returned : request_bytes + returned)));
    if (data)
      request_.emplace(take(raw_claim_data(request_bytes)));
    if (output_bytes)
      output_.emplace(take(raw_claim_data(output_bytes)));
  }
  Fields request() const {
    Fields fields;
    fields.ref(helper_);
    fields.ref(resource_);
    fields.native(method_);
    return fields;
  }
  RawEventRef begin(Fields &fields) {
    if (!control_)
      return {};
    if (request_data_) {
      if (!request_)
        throw Error{ErrorCode::wrong_incarnation};
      actual_ = take(raw_publish(site_, fields.view(), RawPartition::data, *request_));
    } else
      actual_ =
          take(raw_publish(site_, fields.view(), RawPartition::control, *control_));
    request_.reset();
    return actual_;
  }
  template <class T> void returned(T result, int error) {
    if (!control_)
      return;
    const bool failed = result < 0;
    Fields fields;
    fields.ref(actual_);
    fields.native(result);
    fields.native(failed);
    if (failed)
      fields.native(error);
    capture(site_ + 1, fields, RawPartition::control, *control_);
    control_.reset();
  }
  void pointer(bool present, int error) {
    if (!control_)
      return;
    const bool failed = !present;
    Fields fields;
    fields.ref(actual_);
    fields.native(present);
    fields.native(failed);
    if (failed)
      fields.native(error);
    capture(site_ + 1, fields, RawPartition::control, *control_);
    control_.reset();
  }
  void status(int result, int error, const struct stat &status) {
    if (!control_)
      return;
    const bool failed = result < 0;
    Fields fields;
    fields.ref(actual_);
    fields.native(result);
    fields.native(failed);
    if (failed)
      fields.native(error);
    else {
      fields.native(status.st_mode);
      fields.native(status.st_size);
    }
    capture(site_ + 1, fields, RawPartition::control, *control_);
    control_.reset();
  }
  RawEventRef body(std::uint8_t role, ByteView data,
                   std::optional<std::size_t> used = {}, bool cstring = false) {
    if (!output_)
      return {};
    const auto extent = data.size();
    Fields fields;
    fields.ref(actual_);
    fields.native(role);
    fields.native(extent);
    const bool known = used.has_value();
    if (cstring) {
      fields.native(known);
      if (used)
        fields.native(*used);
    }
    fields.bytes(data);
    auto ref = capture(3407, fields, RawPartition::data, *output_);
    output_.reset();
    return ref;
  }
  RawEventRef ref() const noexcept { return actual_; }
  void resource(RawEventRef value) noexcept { resource_ = value; }
  void check() const {
    if (capture_error_)
      throw *capture_error_;
  }

private:
  RawEventRef capture(std::uint32_t site, const Fields &fields, RawPartition partition,
                      RawCapacityClaim &bank) noexcept {
    const ErrnoGuard preserved;
    const auto emitted = raw_publish(site, fields.view(), partition, bank);
    if (emitted.has_value())
      return emitted.value();
    if (!capture_error_)
      capture_error_ = emitted.error();
    return {};
  }
  std::optional<Error> capture_error_;
  RawEventRef helper_{}, resource_{}, actual_{};
  std::uint32_t site_;
  Native method_;
  bool request_data_;
  std::optional<RawControlToken> control_;
  std::optional<RawCapacityClaim> request_, output_;
};
// A successful fd and the actual mutated name enter this guard before capture.
class Resource {
public:
  Resource(Helper &owner, char *temporary = nullptr, std::size_t extent = 0)
      : owner_(owner), temporary_(temporary) {
    if (!owner.enabled())
      return;
    close_.emplace(take(raw_claim_control(184)));
    if (temporary) {
      unlink_.emplace(take(raw_claim_control(74)));
      unlink_data_.emplace(take(raw_claim_data(add(98, extent))));
    }
  }
  ~Resource() {
    const ErrnoGuard preserved;
    if (fd_ >= 0)
      (void)close_attempt();
    unlink_attempt();
    if (close_ && acquired_.ordinal) {
      Fields fields;
      fields.ref(acquired_);
      fields.native(ending_);
      capture(3444, fields, RawPartition::control, *close_);
    }
  }
  void acquired(int fd, RawEventRef ref) noexcept {
    fd_ = fd;
    acquired_ = ref;
    named_ = temporary_ != nullptr;
  }
  void output(RawEventRef ref) noexcept { output_ = ref; }
  void renamed() noexcept {
    named_ = false;
    ending_ = 2;
    unlink_.reset();
    unlink_data_.reset();
  }
  RawEventRef ref() const noexcept { return acquired_; }
  int close() {
    const int result = close_attempt(), error = errno;
    if (capture_error_)
      throw *capture_error_;
    errno = error;
    return result;
  }

private:
  RawEventRef capture(std::uint32_t site, const Fields &fields, RawPartition partition,
                      RawCapacityClaim &bank) noexcept {
    const ErrnoGuard preserved;
    auto emitted = raw_publish(site, fields.view(), partition, bank);
    if (emitted.has_value())
      return emitted.value();
    if (!capture_error_)
      capture_error_ = emitted.error();
    return {};
  }
  void returned(std::uint32_t site, RawEventRef request, int result, int error,
                RawControlToken &bank) noexcept {
    const bool acquired_request = request.ordinal != 0, failed = result < 0;
    Fields fields;
    fields.ref(acquired_);
    fields.native(acquired_request);
    if (acquired_request)
      fields.ref(request);
    fields.native(result);
    fields.native(failed);
    if (failed)
      fields.native(error);
    capture(site, fields, RawPartition::control, bank);
  }
  int close_attempt() noexcept {
    if (fd_ < 0)
      return 0;
    const int fd = fd_;
    RawEventRef request{};
    if (close_) {
      const auto method = Native::close;
      Fields fields;
      fields.ref(owner_.ref());
      fields.ref(acquired_);
      fields.native(method);
      fields.native(fd);
      request = capture(3438, fields, RawPartition::control, *close_);
    }
    const int result = ::close(fd), error = errno;
    fd_ = -1;
    if (close_)
      returned(3439, request, result, error, *close_);
    errno = error;
    return result;
  }
  void unlink_attempt() noexcept {
    if (!named_)
      return;
    RawEventRef request{};
    if (unlink_ && unlink_data_) {
      const auto method = Native::unlink;
      const bool captured = output_.ordinal != 0;
      const auto native_path = operand(temporary_);
      const auto extent = native_path.size();
      Fields fields;
      fields.ref(owner_.ref());
      fields.ref(acquired_);
      fields.native(method);
      fields.native(captured);
      if (captured)
        fields.ref(output_);
      fields.native(extent);
      fields.bytes(native_path);
      request = capture(3442, fields, RawPartition::data, *unlink_data_);
    }
    const int result = ::unlink(temporary_), error = errno;
    named_ = false;
    ending_ = 3;
    if (unlink_)
      returned(3443, request, result, error, *unlink_);
    errno = error;
  }
  Helper &owner_;
  char *temporary_;
  int fd_ = -1;
  bool named_ = false;
  std::uint8_t ending_ = 1;
  RawEventRef acquired_{}, output_{};
  std::optional<Error> capture_error_;
  std::optional<RawControlToken> close_, unlink_;
  std::optional<RawCapacityClaim> unlink_data_;
};
int open_read(Helper &helper, Resource &resource, const std::filesystem::path &path) {
  const auto path_bytes = operand(path.c_str());
  const auto extent = path_bytes.size();
  const int flags = O_RDONLY | O_CLOEXEC;
  const bool mode = false;
  Call opening{helper, 3420, Native::open, {}, add(78, extent), true};
  auto fields = opening.request();
  fields.native(flags);
  fields.native(mode);
  fields.native(extent);
  fields.bytes(path_bytes);
  opening.begin(fields);
  const int fd = ::open(path.c_str(), flags), error = errno;
  if (fd >= 0)
    resource.acquired(fd, opening.ref());
  opening.returned(fd, error);
  opening.check();
  if (fd < 0)
    throw Error{ErrorCode::io, error};
  return fd;
}
int status(Helper &owner, const std::filesystem::path &path, struct stat &result,
           bool link = false) {
  const auto data = operand(path.c_str());
  const auto extent = data.size();
  Call call{owner, link ? 3445u : 3424u, link ? Native::lstat : Native::stat,
            {},    add(73, extent),      true};
  auto fields = call.request();
  fields.native(extent);
  fields.bytes(data);
  call.begin(fields);
  const int answer =
                link ? ::lstat(path.c_str(), &result) : ::stat(path.c_str(), &result),
            error = errno;
  call.status(answer, error, result);
  call.check();
  errno = error;
  return answer;
}
bool exists(Helper &owner, const std::filesystem::path &path, std::error_code &error) {
  struct stat value{};
  if (status(owner, path, value) == 0) {
    error.clear();
    return true;
  }
  if (errno == ENOENT || errno == ENOTDIR) {
    error.clear();
    return false;
  }
  error.assign(errno, std::generic_category());
  return false;
}
std::filesystem::path absolute(Helper &owner, const std::filesystem::path &path,
                               std::error_code &error,
                               std::optional<RawCapacityClaim> *returned = nullptr) {
  if (path.is_absolute()) {
    if (returned) {
      returned->reset();
      returned->emplace(take(raw_claim_data(add(49, path.native().size()))));
    }
    error.clear();
    return path;
  }
#if defined(__linux__)
  if (path.empty()) {
    error.assign(EINVAL, std::generic_category());
    return {};
  }
#endif
  for (std::size_t capacity = 8192;;) {
    std::vector<char> output(capacity, '\0');
    Call call{owner, 3426, Native::cwd, {}, 73, false, add(58, capacity)};
    auto fields = call.request();
    fields.native(capacity);
    call.begin(fields);
    const auto *result = ::getcwd(output.data(), capacity);
    const int native_error = errno;
    if (result) {
      const auto data = operand(output.data());
      call.body(2, data, data.size(), true);
      call.pointer(true, native_error);
      call.check();
      if (returned) {
        // Actual cwd CString includes NUL; the existing path join may add one
        // separator, then the actual already-owned relative native operand.
        const auto capacity_needed = add(add(49, data.size() - 1), 1);
        const auto maximum = add(capacity_needed, path.native().size());
        returned->reset();
        returned->emplace(take(raw_claim_data(maximum)));
      }
      error.clear();
      return std::filesystem::path{output.data()} / path;
    }
    call.body(2, std::as_bytes(std::span{output}), {}, true);
    call.pointer(false, native_error);
    call.check();
    if (native_error != ERANGE) {
      error.assign(native_error, std::generic_category());
      return {};
    }
    if (capacity > SIZE_MAX / 2)
      throw Error{ErrorCode::overflow};
    capacity *= 2;
  }
}
std::filesystem::path canonical(Helper &owner, const std::filesystem::path &path,
                                std::error_code &error) {
  auto target = absolute(owner, path, error);
  if (error)
    return {};
  std::array<char, PATH_MAX> output{};
  const auto native_path = operand(target.c_str());
  const auto extent = native_path.size();
  const auto capacity = output.size();
  Call call{owner,           3428, Native::canonical, {},
            add(81, extent), true, add(58, capacity)};
  auto fields = call.request();
  fields.native(extent);
  fields.bytes(native_path);
  fields.native(capacity);
  call.begin(fields);
  const auto *result = ::realpath(target.c_str(), output.data());
  const int native_error = errno;
  if (result) {
    const auto data = operand(output.data());
    call.body(3, data, data.size(), true);
    call.pointer(true, native_error);
    call.check();
    error.clear();
    return std::filesystem::path{output.data()};
  }
  call.body(3, std::as_bytes(std::span{output}), {}, true);
  call.pointer(false, native_error);
  call.check();
#if !defined(__linux__)
  error.assign(native_error, std::generic_category());
  return {};
#else
  if (native_error != ENAMETOOLONG) {
    error.assign(native_error, std::generic_category());
    return {};
  }
  if (!exists(owner, target, error)) {
    if (!error)
      error.assign(ENOENT, std::generic_category());
    return {};
  }
  auto resolved = target.root_path();
  std::deque<std::filesystem::path> pending;
  for (const auto &component : target.relative_path())
    pending.push_back(component);
  int links = 40;
  while (!pending.empty() && !error) {
    auto component = std::move(pending.front());
    pending.pop_front();
    if (component.empty())
      continue;
    if (component == ".") {
      struct stat value{};
      if (status(owner, resolved, value) != 0)
        error.assign(errno, std::generic_category());
      else if (!S_ISDIR(value.st_mode))
        error.assign(ENOTDIR, std::generic_category());
    } else if (component == "..") {
      auto parent = resolved.parent_path();
      resolved = parent.empty() ? target.root_path() : std::move(parent);
    } else {
      resolved /= component;
      struct stat value{};
      if (status(owner, resolved, value, true) != 0) {
        error.assign(errno, std::generic_category());
        break;
      }
      if (!S_ISLNK(value.st_mode))
        continue;
      struct stat sizing{};
      if (status(owner, resolved, sizing, true) != 0) {
        error.assign(errno, std::generic_category());
        break;
      }
      if (!S_ISLNK(sizing.st_mode)) {
        error.assign(EINVAL, std::generic_category());
        break;
      }
      std::string link(
          sizing.st_size ? static_cast<std::size_t>(sizing.st_size) + 1 : 128, '\0');
      while (true) {
        const auto operand_bytes = operand(resolved.c_str());
        const auto operand_size = operand_bytes.size();
        const auto buffer_size = link.size();
        Call readlink{owner,
                      3447,
                      Native::link,
                      {},
                      add(81, operand_size),
                      true,
                      add(49, buffer_size)};
        auto args = readlink.request();
        args.native(operand_size);
        args.bytes(operand_bytes);
        args.native(buffer_size);
        readlink.begin(args);
        const auto count = ::readlink(resolved.c_str(), link.data(), buffer_size);
        const int err = errno;
        if (count >= 0)
          readlink.body(5, std::as_bytes(std::span{link.data(),
                                                   static_cast<std::size_t>(count)}));
        readlink.returned(count, err);
        readlink.check();
        if (count < 0) {
          error.assign(err, std::generic_category());
          break;
        }
        if (static_cast<std::size_t>(count) == buffer_size) {
          if (buffer_size > 4096) {
            error.assign(ENAMETOOLONG, std::generic_category());
            break;
          }
          link.resize(buffer_size * 2);
          continue;
        }
        link.resize(static_cast<std::size_t>(count));
        break;
      }
      if (error)
        break;
      if (--links == 0) {
        error.assign(ELOOP, std::generic_category());
        break;
      }
      std::filesystem::path linked{link};
      if (linked.is_absolute()) {
        resolved = linked.root_path();
        linked = linked.relative_path();
      } else
        resolved = resolved.parent_path();
      pending.insert(pending.begin(), linked.begin(), linked.end());
    }
  }
  if (error || !exists(owner, resolved, error))
    resolved.clear();
  return resolved;
#endif
}
} // namespace

RawControlToken RawFileToolTrace::control(std::uint64_t amount) {
  return take(raw_claim_control(amount));
}
RawFileToolTrace::RawFileToolTrace(const RawAdapterContext *context,
                                   RawFileMethod method) {
  if (!context || !context->parent || !raw_enabled())
    return;
  issuer_ = raw_current_thread();
  emission_.prepare(91);
  Fields fields;
  fields.ref(*context->parent);
  fields.native(method);
  emission_.begin({3400, 3401}, fields.view());
  const auto request = emission_.reference();
  selected_ = *context;
  selected_.parent = request;
}
RawFileToolTrace::~RawFileToolTrace() { finish(RawFileBranch::unfinished); }
const RawEventRef &RawFileToolTrace::parent() const noexcept {
  if (!selected_.parent)
    std::terminate();
  return *selected_.parent;
}
const RawAdapterContext *RawFileToolTrace::context() const noexcept {
  return emission_.enabled() ? &selected_ : nullptr;
}
void RawFileToolTrace::finish(RawFileBranch branch, const Error *error,
                              const int *fs) noexcept {
  if (!emission_.enabled())
    return;
  const auto fields =
      terminal_fields(parent(), branch, error, fs,
                      branch == RawFileBranch::returned ? &result_count_ : nullptr);
  emission_.close(fields.view());
}
void RawFileToolTrace::finish(RawOperationBranch branch, const Error *error) noexcept {
  finish(file_branch(branch), error);
}
void RawFileToolTrace::finish_filesystem(int code) noexcept {
  finish(RawFileBranch::filesystem, nullptr, &code);
}
void RawFileToolTrace::observer_finish(RawEventRef request, RawOperationBranch branch,
                                       RawControlToken &bank,
                                       const Error *error) noexcept {
  observer_end(request, file_branch(branch), bank, error);
}
void RawFileToolTrace::argument(std::uint8_t kind, std::string_view value) {
  if (!emission_.enabled())
    return;
  const auto size = value.size();
  auto bank = take(raw_claim_data(add(49, size)));
  Fields fields;
  fields.ref(parent());
  fields.native(kind);
  fields.native(size);
  fields.bytes(bytes(value));
  (void)take(raw_publish(3402, fields.view(), RawPartition::data, bank));
}
void RawFileToolTrace::range(std::uint8_t mode, std::size_t first, std::size_t last) {
  if (!emission_.enabled())
    return;
  auto bank = take(raw_claim_control(57));
  Fields fields;
  fields.ref(parent());
  fields.native(mode);
  fields.native(first);
  fields.native(last);
  (void)take(raw_publish(3403, fields.view(), RawPartition::control, bank));
}
void RawFileToolTrace::selection(std::uint8_t step, RawEventRef source,
                                 std::size_t length,
                                 std::optional<std::size_t> position) {
  if (!emission_.enabled())
    return;
  const bool located = position.has_value();
  auto bank = take(raw_claim_control(82));
  Fields fields;
  fields.ref(parent());
  fields.native(step);
  fields.ref(source);
  fields.native(length);
  fields.native(located);
  if (position)
    fields.native(*position);
  (void)take(raw_publish(3404, fields.view(), RawPartition::control, bank));
}
std::optional<RawCapacityClaim> RawFileToolTrace::prepare_proposal(std::size_t extent) {
  if (!emission_.enabled())
    return std::nullopt;
  return take(raw_claim_data(add(49, extent)));
}
RawFileInput RawFileToolTrace::proposed(std::string_view value,
                                        std::optional<RawCapacityClaim> &bank) {
  if (!emission_.enabled())
    return {{}, value.size()};
  const auto role = std::uint8_t{6};
  const auto size = value.size();
  if (!bank)
    throw Error{ErrorCode::invalid_range};
  Fields fields;
  fields.ref(parent());
  fields.native(role);
  fields.native(size);
  fields.bytes(bytes(value));
  const RawFileInput input{
      take(raw_publish(3407, fields.view(), RawPartition::data, *bank)), size};
  bank.reset();
  return input;
}
RawEventRef RawFileToolTrace::observer_begin(bool present, RawControlToken &bank) {
  Fields fields;
  fields.ref(parent());
  fields.native(present);
  return take(raw_publish(3405, fields.view(), RawPartition::control, bank));
}
void RawFileToolTrace::observer_end(RawEventRef request, RawFileBranch branch,
                                    RawControlToken &bank, const Error *error,
                                    const int *fs) noexcept {
  terminal(3406, request, branch, bank, error, fs);
}
std::string raw_file_read_prefix(const std::filesystem::path &path, std::size_t count,
                                 const RawAdapterContext *context,
                                 RawEventRef *actual_owner) {
  if (actual_owner)
    *actual_owner = {};
  Helper helper{context, RawFileMethod::prefix, count};
  if (actual_owner)
    *actual_owner = helper.ref();
  return helper.run([&] {
    Resource resource{helper};
    const int fd = open_read(helper, resource, path);
    std::string out;
    std::array<char, 8192> buffer{};
    while (out.size() < count) {
      const auto requested = std::min(buffer.size(), count - out.size());
      Call reading{helper, 3422,  Native::read,      resource.ref(),
                   77,     false, add(49, requested)};
      auto arguments = reading.request();
      arguments.native(fd);
      arguments.native(requested);
      reading.begin(arguments);
      const auto acquired = ::read(fd, buffer.data(), requested);
      const int error = errno;
      if (acquired > 0)
        (void)reading.body(1, std::as_bytes(std::span{
                                  buffer.data(), static_cast<std::size_t>(acquired)}));
      reading.returned(acquired, error);
      reading.check();
      if (acquired < 0 && error == EINTR)
        continue;
      if (acquired < 0)
        throw Error{ErrorCode::io, error};
      if (acquired == 0)
        throw Error{ErrorCode::incomplete};
      out.append(buffer.data(), static_cast<std::size_t>(acquired));
    }
    helper.count(out.size());
    return out;
  });
}
std::string raw_file_read(const std::filesystem::path &path, std::size_t limit,
                          const RawAdapterContext *context, RawEventRef *owner) {
  Helper helper{context, RawFileMethod::read, limit};
  return helper.run([&]() -> std::string {
    Resource resource{helper};
    const int fd = open_read(helper, resource, path);
    std::string out;
    std::array<char, 8192> buffer{};
    std::size_t filled = 0;
    while (true) {
      const auto requested = buffer.size() - filled;
      Call reading{helper, 3422,  Native::read,      resource.ref(),
                   77,     false, add(49, requested)};
      auto args = reading.request();
      args.native(fd);
      args.native(requested);
      reading.begin(args);
      const auto count = ::read(fd, buffer.data() + filled, requested);
      const int native_error = errno;
      if (count > 0)
        reading.body(1, std::as_bytes(std::span{buffer.data() + filled,
                                                static_cast<std::size_t>(count)}));
      reading.returned(count, native_error);
      reading.check();
#if defined(__linux__)
      if (count < 0 && native_error == EINTR)
        continue;
      if (count < 0)
        throw Error{ErrorCode::io};
#endif
      if (count > 0)
        filled += static_cast<std::size_t>(count);
      if (filled == buffer.size() || count <= 0) {
        if (filled > limit - out.size())
          throw Error{ErrorCode::capacity};
        out.append(buffer.data(), filled);
        filled = 0;
      }
      if (count <= 0)
        break;
    }
    helper.count(out.size());
    if (owner)
      *owner = helper.ref();
    return out;
  });
}
void raw_file_write(const std::filesystem::path &requested, std::string_view content,
                    const RawAdapterContext *context, const RawFileInput *provided,
                    FileEffectObservation *effect) {
  Helper helper{context, RawFileMethod::write, content.size()};
  helper.run([&] {
    RawFileInput input = provided ? *provided : RawFileInput{};
    if (helper.enabled() && !input.body.ordinal) {
      const auto role = std::uint8_t{6};
      const auto length = content.size();
      auto bank = take(raw_claim_data(add(49, length)));
      Fields fields;
      fields.ref(helper.ref());
      fields.native(role);
      fields.native(length);
      fields.bytes(bytes(content));
      input = {take(raw_publish(3407, fields.view(), RawPartition::data, bank)),
               length};
    }
    if (provided && helper.enabled() &&
        (!input.body.ordinal || input.length != content.size()))
      throw Error{ErrorCode::invalid_range};
    std::error_code error;
    const bool present = exists(helper, requested, error);
    if (error)
      throw Error{ErrorCode::io, error.value()};
    auto path = present ? canonical(helper, requested, error)
                        : absolute(helper, requested, error);
    if (error)
      throw std::filesystem::filesystem_error{present ? "canonical" : "absolute",
                                              requested, error};
    auto name_text = path.string() + ".arco-XXXXXX";
    std::vector<char> name(name_text.begin(), name_text.end());
    name.push_back('\0');
    Resource resource{helper, name.data(), name.size()};
    const auto extent = name.size();
    Call creating{helper,          3430, Native::temporary, {},
                  add(73, extent), true, add(58, extent)};
    auto args = creating.request();
    args.native(extent);
    args.bytes(std::as_bytes(std::span{name}));
    creating.begin(args);
    Call normalize{helper, 3490, Native::cloexec, {}, 77, false};
    int fd = -1;
    {
      const std::lock_guard spawn_lock{detail::process_spawn_mutex};
      fd = ::mkstemp(name.data());
      const int native_error = errno;
      if (fd >= 0)
        resource.acquired(fd, creating.ref());
      try {
        if (fd >= 0) {
          const auto data = operand(name.data());
          resource.output(creating.body(4, data, data.size(), true));
          creating.returned(fd, native_error);
          creating.check();
          const int command = F_SETFD, flag = FD_CLOEXEC;
          normalize.resource(resource.ref());
          auto request = normalize.request();
          request.native(fd);
          request.native(command);
          request.native(flag);
          normalize.begin(request);
          const int result = ::fcntl(fd, command, flag), error_number = errno;
          normalize.returned(result, error_number);
          normalize.check();
          if (result < 0)
            throw Error{ErrorCode::io, error_number};
        } else {
          creating.body(4, std::as_bytes(std::span{name}), {}, true);
          creating.returned(fd, native_error);
          creating.check();
          throw Error{ErrorCode::io, native_error};
        }
      } catch (...) {
        if (fd >= 0) {
          try {
            (void)resource.close();
          } catch (...) {
          }
        }
        throw;
      }
    }
    struct stat prior{};
    if (present && status(helper, path, prior) != 0)
      throw Error{ErrorCode::io, errno};
    const mode_t mode = static_cast<mode_t>(present ? prior.st_mode & 0777 : 0644);
    Call permissions{helper, 3432, Native::mode, resource.ref(), 69 + sizeof(mode),
                     false};
    auto mode_fields = permissions.request();
    mode_fields.native(fd);
    mode_fields.native(mode);
    permissions.begin(mode_fields);
    const int mode_result = ::fchmod(fd, mode), mode_error = errno;
    permissions.returned(mode_result, mode_error);
    permissions.check();
    if (mode_result != 0)
      throw Error{ErrorCode::io, mode_error};
    std::size_t offset = 0;
    while (offset < content.size()) {
      const auto remaining = content.size() - offset;
      Call writing{helper, 3434, Native::write, resource.ref(), 109, false};
      auto write_args = writing.request();
      write_args.native(fd);
      write_args.native(remaining);
      write_args.ref(input.body);
      write_args.native(offset);
      writing.begin(write_args);
      const auto count = ::write(fd, content.data() + offset, remaining);
      const int err = errno;
      writing.returned(count, err);
      writing.check();
      if (count < 0) {
        if (err == EINTR)
          continue;
        throw Error{ErrorCode::io, err};
      }
      if (count == 0)
        throw Error{ErrorCode::io};
      offset += static_cast<std::size_t>(count);
    }
    Call syncing{helper, 3436, Native::sync, resource.ref(), 69, false};
    auto sync_fields = syncing.request();
    sync_fields.native(fd);
    syncing.begin(sync_fields);
    const int synced = ::fsync(fd), sync_error = errno;
    syncing.returned(synced, sync_error);
    syncing.check();
    if (synced != 0)
      throw Error{ErrorCode::io, sync_error};
    if (resource.close() != 0)
      throw Error{ErrorCode::io, errno};
    const auto old_path = operand(name.data()), new_path = operand(path.c_str());
    const auto old_size = old_path.size(), new_size = new_path.size();
    Call renaming{
        helper, 3440, Native::rename, resource.ref(), add(add(81, old_size), new_size),
        true};
    auto rename_args = renaming.request();
    rename_args.native(old_size);
    rename_args.bytes(old_path);
    rename_args.native(new_size);
    rename_args.bytes(new_path);
    renaming.begin(rename_args);
    const int renamed = ::rename(name.data(), path.c_str()), rename_error = errno;
    if (renamed == 0) {
      if (effect)
        effect->replaced();
      resource.renamed();
    }
    renaming.returned(renamed, rename_error);
    renaming.check();
    if (renamed != 0)
      throw Error{ErrorCode::io, rename_error};
    helper.count(offset);
  });
}
bool raw_file_exists(const std::filesystem::path &path, std::error_code &error,
                     const RawAdapterContext *context) {
  if (!context || !context->parent || !raw_enabled())
    return std::filesystem::exists(path, error);
  Helper helper{context, RawFileMethod::exists, path.native().size()};
  return helper.run([&] {
    const bool result = exists(helper, path, error);
    helper.flag(result);
    helper.error_code(error);
    return result;
  });
}
std::filesystem::path raw_file_canonical(const std::filesystem::path &path,
                                         std::error_code &error,
                                         const RawAdapterContext *context,
                                         RawEventRef *actual_owner) {
  if (actual_owner)
    *actual_owner = {};
  if (!context || !context->parent || !raw_enabled())
    return std::filesystem::canonical(path, error);
  Helper helper{context, RawFileMethod::canonical, path.native().size()};
  if (actual_owner)
    *actual_owner = helper.ref();
  return helper.run([&] {
    auto result = canonical(helper, path, error);
    helper.count(result.native().size());
    helper.error_code(error);
    return result;
  });
}
std::filesystem::path raw_file_absolute(const std::filesystem::path &path,
                                        std::error_code &error,
                                        const RawAdapterContext *context,
                                        RawEventRef *returned_body) {
  if (returned_body)
    *returned_body = {};
  if (!context || !context->parent || !raw_enabled())
    return std::filesystem::absolute(path, error);
  Helper helper{context, RawFileMethod::absolute, path.native().size()};
  return helper.run([&] {
    // Empty error-code returns are completed path values too. Larger successful
    // copy/join acquires its exact source-derived bound before that operation.
    std::optional<RawCapacityClaim> bank{take(raw_claim_data(49))};
    auto result = absolute(helper, path, error, &bank);
    const auto &native = result.native();
    const auto extent = native.size();
    const auto role = std::uint8_t{7};
    Fields fields;
    fields.ref(helper.ref());
    fields.native(role);
    fields.native(extent);
    fields.bytes(bytes(native));
    if (!bank)
      throw Error{ErrorCode::wrong_incarnation};
    const auto ref = take(raw_publish(3407, fields.view(), RawPartition::data, *bank));
    bank.reset();
    if (returned_body)
      *returned_body = ref;
    helper.count(extent);
    helper.error_code(error);
    return result;
  });
}
std::filesystem::path raw_file_weak_canonical(const std::filesystem::path &path,
                                              std::error_code &error,
                                              const RawAdapterContext *context,
                                              RawEventRef *returned_body) {
  if (returned_body)
    *returned_body = {};
  if (!context || !context->parent || !raw_enabled())
    return std::filesystem::weakly_canonical(path, error);
  Helper helper{context, RawFileMethod::weak_canonical, path.native().size()};
  return helper.run([&] {
    auto input = take(raw_claim_data(add(49, path.native().size())));
    const std::uint8_t input_role = 8;
    const auto input_size = path.native().size();
    Fields input_fields;
    input_fields.ref(helper.ref());
    input_fields.native(input_role);
    input_fields.native(input_size);
    input_fields.bytes(bytes(path.native()));
    (void)take(raw_publish(3407, input_fields.view(), RawPartition::data, input));
    // The platform library's weak-prefix algorithm, using the same owned native
    // status/canonical acquisitions. Missing suffixes are not canonicalized.
    auto known_exists = [&](const std::filesystem::path &operand_path) {
      struct stat value{};
      if (status(helper, operand_path, value) == 0) {
        error.clear();
        return true;
      }
      const int native_error = errno;
      if (native_error == ENOENT || native_error == ENOTDIR) {
        error.clear();
        return false;
      }
#if defined(__linux__)
      // GCC status returns file_type::unknown (a known existing status) here.
      if (native_error == EOVERFLOW) {
        error.clear();
        return true;
      }
#endif
      error.assign(native_error, std::generic_category());
      return false;
    };
    std::filesystem::path prefix;
    std::vector<std::filesystem::path> suffix;
#if defined(__linux__)
    if (known_exists(path))
      prefix = path;
    else if (!error) {
      auto iter = path.begin();
      const auto end = path.end();
      for (; iter != end; ++iter) {
        auto candidate = prefix / *iter;
        if (!known_exists(candidate))
          break;
        prefix = std::move(candidate);
      }
      if (!error)
        for (; iter != end; ++iter)
          suffix.push_back(*iter);
    }
#else
    prefix = path;
    while (!prefix.empty() && !known_exists(prefix) && !error) {
      suffix.push_back(prefix.filename());
      prefix = prefix.parent_path();
    }
    std::reverse(suffix.begin(), suffix.end());
#endif
    if (error) {
      helper.count(0);
      helper.error_code(error);
      return std::filesystem::path{};
    }
#if defined(__linux__)
    if (!prefix.empty())
#endif
      prefix = canonical(helper, prefix, error);
    if (error) {
      helper.count(0);
      helper.error_code(error);
      return std::filesystem::path{};
    }
    // Native resolved prefix bytes were preserved before canonical constructed
    // its path. Fund the SAME suffix joins and normalization before they run.
    const auto bound =
        add(add(49, prefix.native().size()), add(1, path.native().size()));
    auto bank = take(raw_claim_data(bound));
    for (const auto &component : suffix)
      prefix /= component;
    auto result = suffix.empty() ? std::move(prefix) : prefix.lexically_normal();
    const auto extent = result.native().size();
    const std::uint8_t role = 7;
    Fields fields;
    fields.ref(helper.ref());
    fields.native(role);
    fields.native(extent);
    fields.bytes(bytes(result.native()));
    const auto ref = take(raw_publish(3407, fields.view(), RawPartition::data, bank));
    if (returned_body)
      *returned_body = ref;
    helper.count(extent);
    helper.error_code(error);
    return result;
  });
}
std::string_view raw_file_description() noexcept {
  return "BBRAW FILE v1 sites3400-3499; envelope length:u32/site:u32/ordinal:u64, no "
         "clock. "
         "3400 tool(parentR24,methodu8 "
         "read1/write2/edit3/canonical4/exists5/absolute6/prefix7/weakCanonical8);3401 "
         "terminal(toolR24,branchu8,[nativecountsize8 on success|Errorcode1/detail8 "
         "only owned Error|filesystem_error nativeint4]);"
         "3402 arg(toolR24,kindu8 path0/old1/new2/content3,extent8,whole borrowed "
         "business bytes);3403 "
         "range(toolR24,modeu8,first8,last8);"
         "3404 "
         "selection(toolR24,stepu8,actualbody/"
         "helperR24,length8,positionpresentbool,[position8]);"
         "3405 observer(toolR24,presentbool);3406 terminal same native branch variants;"
         "3407 original(nativeRequestR24,roleu8,extent8,[usedknownbool,[usedlength8] "
         "only "
         "roles2cwd/3realpath/4mkstemp],literalbytes);role1read/5readlink/6proposed/"
         "7absolute/weak-return,8weak-operand "
         "known extent;"
         "prefix7 requests only remaining exact prefix, no fstat/trailing read; "
         "3410 helper(parentR24,methodu8,limit/count8);3411 success count8 or "
         "existsbool, canonical/exists/absolute also "
         "nativeerrorpresentbool/[error_code int]; "
         "exception terminal variants as3401;"
         "Native requests helperR24/resourceR24/methodu8, resource-less proposals name "
         "helperR. "
         "3420 open(flagsint,modepresentbool,[mode_t],CStringextent8,operand including "
         "firstNUL);3421 resultint,errnopresentbool,[nativeerrno int];"
         "3422 read(fdint,requested8);3423 ssize_t,errnopresentbool,[nativeerrno "
         "int];positive3407 before limit/append;"
         "3424 stat/3445 lstat(CStringextent8,operand);3425/3446 "
         "resultint,errnopresentbool,[errno int on failure|native st_mode/st_size on "
         "success];"
         "3426 getcwd(capacity8);3427 returnedpointerbool,errnopresentbool,[errno int];"
         "3428 realpath(CStringextent8,operand,capacity8);3429 "
         "returnedpointerbool,errnopresentbool,[errno int];"
         "3430 mkstemp(initializedextent8,mutable storage including NUL);3431 "
         "resultint,errnopresentbool,[errno int];"
         "3432 fchmod(fdint,mode_t);3433 resultint,errnopresentbool,[errno int];"
         "3434 write(fdint,remaining8,inputR24,businessoffset8);3435 "
         "ssize_t,errnopresentbool,[errno int];"
         "3436 fsync(fdint);3437 resultint,errnopresentbool,[errno int];3438 "
         "close(fdint);3439 "
         "actualresourceR24,requestpresentbool,[actualrequestR24],resultint,"
         "errnopresentbool,[errno int];"
         "3440 rename(twoCStringextents8/operands);3441 "
         "resultint,errnopresentbool,[errno int];"
         "3442 "
         "cleanupunlink(actualoutputRpresentbool,[outputR24],CStringextent8,actual "
         "mutated operand);3443 "
         "actualresourceR24,requestpresentbool,[actualrequestR24],resultint,"
         "errnopresentbool,[errno int];"
         "3444 resourceEND(acquiredopenR24,ownershipEndu8 "
         "readclose1/rename2/unlinkattempt3), lexical end never kernel success;"
         "3447 readlink(CStringextent8,operand,capacity8);3448 "
         "ssize_t,errnopresentbool,[errno int];"
         "3490 fcntl(fdint,commandint,argumentint);3491 "
         "resultint,errnopresentbool,[errno int]. "
         "Ordinary native returns start actualrequestR24; cleanup returns name "
         "realresourceR and optional acknowledged request. Explicit parent only; "
         "default "
         "helpers optout; no normalization/projection/padding/derived time.";
}
} // namespace blackbird
