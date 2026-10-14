// Bounded, local candidate checkout ownership. No provider or activation authority.
#include "blackbird/context.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_file.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/raw_session.hpp"

#include "native_process.hpp"
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <sys/file.h>
#include <sys/random.h>
#include <utility>

using blackbird::RawAdapterContext;
using blackbird::RawEventRef;
using blackbird::RawOperationBranch;
using blackbird::RawSessionCall;
using blackbird::Value;
using blackbird::detail::raw_native_field;
using blackbird::detail::raw_string_field;
namespace fs = std::filesystem;
namespace {
bool selected(const RawAdapterContext *context) {
  return context && context->parent && context->parent->ordinal &&
         blackbird::raw_enabled();
}
std::uint64_t bound(std::uint64_t base, std::uint64_t suffix) {
  return blackbird::raw_session_bound(base, suffix);
}
std::string path_text(const fs::path &path, const RawAdapterContext *context) {
  const auto size = path.native().size();
  RawSessionCall trace{3940, 3941, context, std::array{raw_native_field(size)}};
  return trace.run([&] {
    auto credit = trace.bytes_credit(size);
    auto result = path.string();
    (void)trace.bytes(3942, result, credit);
    return result;
  });
}
template <class F>
fs::path constructed_path(const RawAdapterContext *context, std::uint64_t size,
                          F &&body) {
  RawSessionCall trace{3949, 3950, context};
  return trace.run([&] {
    auto credit = trace.source_credit(size);
    auto path = std::forward<F>(body)();
    (void)trace.source(7, path.native(), credit, 3907);
    return path;
  });
}
RawEventRef argument_bytes(RawSessionCall &trace, std::uint32_t site, std::size_t index,
                           std::string_view bytes) {
  auto credit = trace.bytes_credit(bound(8, bytes.size()));
  const auto size = bytes.size();
  if (!credit)
    return {};
  return blackbird::unwrap(blackbird::raw_publish_linked(
      site, trace.reference(),
      std::array{raw_native_field(index), raw_native_field(size),
                 raw_string_field(bytes)},
      blackbird::RawPartition::data, *credit));
}
RawEventRef argument(RawSessionCall &trace, std::uint32_t site, std::size_t index,
                     const std::string &operand) {
  return argument_bytes(trace, site, index, {operand.data(), operand.size() + 1});
}
RawEventRef argument(RawSessionCall &trace, std::uint32_t site, std::size_t index,
                     const char *operand) {
  return argument_bytes(trace, site, index, {operand, std::strlen(operand) + 1});
}
void predicate(const RawAdapterContext *context, std::uint8_t kind, bool result) {
  if (!context || !context->parent.has_value() || !context->parent->ordinal ||
      !blackbird::raw_enabled())
    return;
  auto credit = blackbird::unwrap(blackbird::raw_claim_control(44));
  const bool index = false, source = false;
  blackbird::unwrap(blackbird::raw_publish_linked(
      3908, *context->parent,
      std::array{raw_native_field(kind), raw_native_field(result),
                 raw_native_field(index), raw_native_field(source)},
      blackbird::RawPartition::control, credit));
}
bool observed(const RawAdapterContext *context, std::uint8_t kind, bool result) {
  predicate(context, kind, result);
  return result;
}
template <class F>
auto filesystem(const RawAdapterContext *context, std::uint8_t method,
                const fs::path &first, const fs::path *second, F &&body,
                std::span<const blackbird::ByteView> operands = {}) {
  using Return = std::invoke_result_t<F>;
  constexpr std::uint64_t extra = 10;
  std::array<blackbird::ByteView, 3> arguments{};
  if (operands.size() > 2)
    throw blackbird::Error{blackbird::ErrorCode::capacity};
  arguments[0] = raw_native_field(method);
  std::copy(operands.begin(), operands.end(), arguments.begin() + 1);
  RawSessionCall trace{3938, 3939, context,
                       std::span{arguments}.first(1 + operands.size()), extra};
  return trace.run([&] {
    (void)trace.source(1, first.native(), 3907);
    if (second)
      (void)trace.source(2, second->native(), 3907);
    const bool native_error = false, returned = true;
    try {
      if constexpr (std::is_void_v<Return>) {
        std::forward<F>(body)();
        trace.finish(
            RawOperationBranch::returned, nullptr,
            std::array{raw_native_field(native_error), raw_native_field(returned)});
        trace.require_capture();
      } else {
        auto result = std::forward<F>(body)();
        if constexpr (std::is_same_v<Return, bool>) {
          trace.finish(RawOperationBranch::returned, nullptr,
                       std::array{raw_native_field(native_error),
                                  raw_native_field(returned),
                                  raw_native_field(result)});
        } else {
          const auto type = result.type();
          const auto permissions = result.permissions();
          trace.finish(RawOperationBranch::returned, nullptr,
                       std::array{raw_native_field(native_error),
                                  raw_native_field(returned), raw_native_field(type),
                                  raw_native_field(permissions)});
        }
        trace.require_capture();
        return result;
      }
    } catch (const fs::filesystem_error &error) {
      const bool failed = true, completed = false;
      const int code = error.code().value();
      trace.finish(RawOperationBranch::standard, nullptr,
                   std::array{raw_native_field(failed), raw_native_field(code),
                              raw_native_field(completed)});
      trace.require_capture();
      throw;
    }
  });
}
fs::path canonical(const fs::path &path, const RawAdapterContext *context) {
  if (!selected(context))
    return fs::canonical(path);
  const std::uint8_t method = 1;
  RawSessionCall trace{3938, 3939, context, std::array{raw_native_field(method)}, 35};
  return trace.run([&] {
    (void)trace.source(1, path.native(), 3907);
    const auto origin = trace.context();
    std::error_code error;
    RawEventRef acquired{};
    auto result = blackbird::raw_file_canonical(path, error, &origin, &acquired);
    trace.require_capture();
    if (error) {
      const bool native_error = true, completed = false;
      const int code = error.value();
      trace.finish(RawOperationBranch::standard, nullptr,
                   std::array{raw_native_field(native_error), raw_native_field(code),
                              raw_native_field(completed)});
      trace.require_capture();
#if defined(__linux__)
      throw fs::filesystem_error("cannot make canonical path", path, error);
#else
      throw fs::filesystem_error("in canonical", path, error);
#endif
    }
    const bool native_error = false, completed = true;
    const auto size = result.native().size();
    trace.finish(RawOperationBranch::returned, nullptr,
                 std::array{raw_native_field(native_error), raw_native_field(completed),
                            blackbird::ByteView{acquired.lane},
                            raw_native_field(acquired.ordinal),
                            raw_native_field(size)});
    trace.require_capture();
    return result;
  });
}
bool exists(const fs::path &path, const RawAdapterContext *context) {
  return filesystem(context, 3, path, nullptr, [&] { return fs::exists(path); });
}
bool symlink(const fs::path &path, const RawAdapterContext *context) {
  const auto status =
      filesystem(context, 4, path, nullptr, [&] { return fs::symlink_status(path); });
  const auto type = status.type();
  const auto permissions = status.permissions();
  return filesystem(
      context, 5, path, nullptr, [&] { return fs::is_symlink(status); },
      std::array{raw_native_field(type), raw_native_field(permissions)});
}
void directories(const fs::path &path, const RawAdapterContext *context) {
  (void)filesystem(context, 7, path, nullptr,
                   [&] { return fs::create_directories(path); });
}
void candidate_rename(const fs::path &from, const fs::path &to,
                      const RawAdapterContext *context) {
  filesystem(context, 8, from, &to, [&] { fs::rename(from, to); });
}
// Each POSIX call pays its own literal native return, without imposing the C++
// caller schema. The passed function is the existing call, not a metric hook.
template <class F>
auto native_call(const RawAdapterContext *context, blackbird::RawCallSites sites,
                 std::span<const blackbird::ByteView> fields, F &&body) {
  blackbird::RawCaptureFailureScope capture{selected(context)};
  blackbird::RawEmission emission;
  if (context && context->parent.has_value() && context->parent->ordinal &&
      blackbird::raw_enabled()) {
    std::array<blackbird::ByteView, 32> parts{};
    if (fields.size() > 30) {
      blackbird::raw_capture_failed({blackbird::ErrorCode::capacity});
      throw blackbird::Error{blackbird::ErrorCode::capacity};
    }
    parts[0] = context->parent->lane;
    parts[1] = raw_native_field(context->parent->ordinal);
    std::copy(fields.begin(), fields.end(), parts.begin() + 2);
    const auto actual = std::span{parts}.first(fields.size() + 2);
    using Return = std::invoke_result_t<F, RawEventRef>;
    emission.start(
        sites, bound(blackbird::raw_record_bytes(actual), 45 + sizeof(Return)), actual);
  }
  auto result = std::forward<F>(body)(emission.reference());
  const int error = errno;
  const bool failed = result < 0;
  std::array returned{raw_native_field(result), raw_native_field(failed),
                      raw_native_field(error)};
  emission.close_linked(std::span{returned}.first(failed ? 3 : 2));
  capture.require();
  return result;
}
class Descriptor {
  int fd_ = -1;
  RawEventRef opened_{};
  std::optional<blackbird::RawControlToken> acquired_, closing_;
  blackbird::RawEmission close_;

public:
  explicit Descriptor(const RawAdapterContext *context) {
    if (selected(context)) {
      auto credit = blackbird::unwrap(blackbird::raw_claim_control(98));
      closing_.emplace(blackbird::unwrap(credit.split(53)));
      acquired_.emplace(std::move(credit));
    }
  }
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
  ~Descriptor() { (void)close(); }
  int get() const noexcept { return fd_; }
  void accept(int descriptor, RawEventRef opened) noexcept {
    fd_ = descriptor;
    opened_ = opened;
    if (fd_ >= 0 && closing_) {
      const auto adopted = close_.adopt(opened, 3935, std::move(*closing_));
      if (!adopted.has_value())
        blackbird::raw_fence(adopted.error());
      closing_.reset();
    }
  }
  void observed(std::uint8_t kind) {
    if (fd_ < 0 || !acquired_)
      return;
    const auto result = blackbird::raw_publish_linked(
        3934, opened_, std::array{raw_native_field(fd_), raw_native_field(kind)},
        blackbird::RawPartition::control, *acquired_);
    if (!result.has_value())
      throw result.error();
    acquired_.reset();
  }
  int close() noexcept {
    if (fd_ < 0)
      return 0;
    const int descriptor = std::exchange(fd_, -1);
    const int result = ::close(descriptor), error = errno;
    const bool failed = result < 0;
    std::array fields{raw_native_field(descriptor), raw_native_field(result),
                      raw_native_field(failed), raw_native_field(error)};
    close_.close_linked(std::span{fields}.first(failed ? 4 : 3));
    return result;
  }
};

Value text(const std::string &s) { return Value{s}; }
Value num(std::size_t n) { return Value{blackbird::Number{n}}; }
[[noreturn]] void refuse(const std::string &s) { throw std::runtime_error(s); }
std::string dump(const Value &value, const RawAdapterContext *context = nullptr,
                 RawEventRef *original = nullptr) {
  RawSessionCall trace{3943, 3944, context};
  return trace.run([&] {
    auto credit = trace.source_credit(16 * 1024 * 1024);
    auto result = blackbird::encode_packet_string(value);
    if (!result.has_value())
      refuse("binary encoding failed");
    auto output = std::move(result).value();
    const auto reference = trace.source(1, output, credit, 3912);
    if (original)
      *original = reference;
    return output;
  });
}
Value read(const fs::path &path, const RawAdapterContext *context = nullptr) {
  const std::uint8_t role = 1;
  const auto size = path.native().size();
  RawSessionCall trace{3903, 3904, context,
                       std::array{raw_native_field(role), raw_native_field(size)}, 33};
  return trace.run([&] {
    std::string bytes;
    RawEventRef acquired{};
    if (trace.enabled()) {
      const auto origin = trace.context();
      try {
        bytes = blackbird::raw_file_read(path, SIZE_MAX, &origin, &acquired);
      } catch (const blackbird::Error &error) {
        trace.require_capture();
        if (error.code != blackbird::ErrorCode::io)
          throw;
        refuse("cannot read " + path_text(path, &origin));
      }
    } else {
      std::ifstream file(path, std::ios::binary);
      if (!file)
        refuse("cannot read " + path.string());
      bytes.assign(std::istreambuf_iterator<char>(file), {});
    }
    trace.require_capture();
    auto result = blackbird::decode_packet_string(bytes);
    if (!result.has_value())
      refuse("invalid native packet: " + path_text(path, context));
    const bool present = acquired.ordinal != 0;
    const auto count = bytes.size();
    trace.finish(
        RawOperationBranch::returned, nullptr,
        std::array{raw_native_field(present), blackbird::ByteView{acquired.lane},
                   raw_native_field(acquired.ordinal), raw_native_field(count)});
    trace.require_capture();
    return std::move(result).value();
  });
}
const Value &get(const Value &j, std::string_view k) {
  auto v = j.find(k);
  if (!v)
    refuse("missing " + std::string(k));
  return *v;
}
std::string str(const Value &j, std::string_view k) { return get(j, k).string(); }
std::string field(const Value &value, std::string_view key,
                  const RawAdapterContext *context, std::uint8_t kind,
                  std::uint32_t site = 3907) {
  RawSessionCall trace{3951, 3952, context};
  return trace.run([&] {
    const auto &native = get(value, key).string();
    (void)trace.source(kind, native, site);
    return native;
  });
}
std::size_t integer(const Value &j, std::string_view k,
                    const RawAdapterContext *context) {
  RawSessionCall trace{3957, 3958, context};
  return trace.run([&] {
    const auto &s = get(j, k).number().text();
    (void)trace.source(20, s, 3907);
    auto observed = trace.control(40 + sizeof(std::errc) + sizeof(const char *) +
                                  sizeof(std::size_t));
    std::size_t n = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), n);
    (void)trace.event(3959,
                      std::array{raw_native_field(result.ec),
                                 raw_native_field(result.ptr), raw_native_field(n)},
                      observed);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size())
      refuse("invalid integer");
    return n;
  });
}
void set(Value &j, std::string k, Value v) {
  for (auto &[key, value] : j.object())
    if (key == k) {
      value = std::move(v);
      return;
    }
  j.object().emplace_back(std::move(k), std::move(v));
}
Value &member(Value &j, const std::string &k) {
  for (auto &[key, value] : j.object())
    if (key == k)
      return value;
  refuse("missing " + k);
}
class Lock {
  Descriptor descriptor_;
  RawAdapterContext origin_;

public:
  explicit Lock(const fs::path &path, bool attempt = false,
                const RawAdapterContext *context = nullptr)
      : descriptor_(context), origin_(context ? *context : RawAdapterContext{}) {
    RawSessionCall trace{3945, 3946, context};
    trace.run([&] {
      origin_ = trace.context();
      const auto original =
          trace.source(1, {path.c_str(), path.native().size() + 1}, 3907);
      const std::uint8_t method = 1;
      const int flags = O_CREAT | O_RDWR | O_CLOEXEC;
      const mode_t mode = 0600;
      const auto fd = native_call(
          &origin_, {3930, 3931},
          std::array{raw_native_field(method), raw_native_field(flags),
                     raw_native_field(mode), blackbird::ByteView{original.lane},
                     raw_native_field(original.ordinal)},
          [&](RawEventRef request) {
            const int result = ::open(path.c_str(), flags, mode);
            descriptor_.accept(result, request);
            return result;
          });
      if (fd < 0)
        refuse("lock open failed");
      descriptor_.observed(1);
      const int operation = LOCK_EX | (attempt ? LOCK_NB : 0);
      if (native_call(&origin_, {3936, 3937},
                      std::array{raw_native_field(fd), raw_native_field(operation)},
                      [&](RawEventRef) { return ::flock(fd, operation); }) != 0) {
        (void)descriptor_.close();
        refuse("busy lease");
      }
    });
  }
  Lock(const Lock &) = delete;
  Lock &operator=(const Lock &) = delete;
  void unlock(const RawAdapterContext *context = nullptr) {
    const int fd = descriptor_.get(), operation = LOCK_UN;
    if (native_call(context ? context : &origin_, {3936, 3937},
                    std::array{raw_native_field(fd), raw_native_field(operation)},
                    [&](RawEventRef) { return ::flock(fd, operation); }) != 0)
      refuse("unlock failed");
  }
  void lock(const RawAdapterContext *context = nullptr) {
    const int fd = descriptor_.get(), operation = LOCK_EX;
    if (native_call(context ? context : &origin_, {3936, 3937},
                    std::array{raw_native_field(fd), raw_native_field(operation)},
                    [&](RawEventRef) { return ::flock(fd, operation); }) != 0)
      refuse("lock failed");
  }
};
void write(const fs::path &destination, std::string_view bytes,
           const RawAdapterContext *context = nullptr,
           RawEventRef *body_reference = nullptr, RawEventRef existing = {}) {
  RawSessionCall trace{3947, 3948, context};
  trace.run([&] {
    const auto origin = trace.context();
    const auto original = existing.ordinal ? existing : trace.source(2, bytes, 3912);
    if (body_reference)
      *body_reference = original;
    auto template_credit = trace.source_credit(bound(destination.native().size(), 12));
    auto pattern = path_text(destination, &origin) + ".tmp.XXXXXX";
    std::vector<char> path(pattern.begin(), pattern.end());
    path.push_back(0);
    const auto template_source =
        trace.source(3, {path.data(), path.size()}, template_credit, 3907);
    auto created_credit = trace.source_credit(path.size());
    auto parent_credit = trace.source_credit(bound(destination.native().size(), 1));
    Descriptor descriptor{&origin};
    const std::uint8_t temporary = 2;
    const int fd = native_call(&origin, {3930, 3931},
                               std::array{raw_native_field(temporary),
                                          blackbird::ByteView{template_source.lane},
                                          raw_native_field(template_source.ordinal)},
                               [&](RawEventRef request) {
                                 const int result = ::mkstemp(path.data());
                                 descriptor.accept(result, request);
                                 return result;
                               });
    if (fd < 0)
      refuse("write open failed");
    descriptor.observed(2);
    std::size_t off = 0;
    while (off < bytes.size()) {
      const auto amount = bytes.size() - off;
      const auto count = native_call(
          &origin, {3932, 3933},
          std::array{raw_native_field(fd), blackbird::ByteView{original.lane},
                     raw_native_field(original.ordinal), raw_native_field(off),
                     raw_native_field(amount)},
          [&](RawEventRef) { return ::write(fd, bytes.data() + off, amount); });
      if (count < 0 && errno == EINTR)
        continue;
      if (count <= 0) {
        (void)descriptor.close();
        refuse("write failed");
      }
      off += static_cast<std::size_t>(count);
    }
    const std::uint8_t sync = 3;
    const bool synced =
        native_call(&origin, {3930, 3931},
                    std::array{raw_native_field(sync), raw_native_field(fd)},
                    [&](RawEventRef) { return ::fsync(fd); }) == 0;
    (void)descriptor.close();
    trace.require_capture();
    if (!synced)
      refuse("publish failed");
    const auto created = trace.source(4, {path.data(), std::strlen(path.data()) + 1},
                                      created_credit, 3907);
    const auto target =
        trace.source(5, {destination.c_str(), destination.native().size() + 1}, 3907);
    const std::uint8_t publish = 4;
    if (native_call(
            &origin, {3930, 3931},
            std::array{raw_native_field(publish), blackbird::ByteView{created.lane},
                       raw_native_field(created.ordinal),
                       blackbird::ByteView{target.lane},
                       raw_native_field(target.ordinal)},
            [&](RawEventRef) { return ::rename(path.data(), destination.c_str()); }) !=
        0)
      refuse("publish failed");
    const auto parent = constructed_path(&origin, destination.native().size(),
                                         [&] { return destination.parent_path(); });
    const auto parent_source = trace.source(
        6, {parent.c_str(), parent.native().size() + 1}, parent_credit, 3907);
    Descriptor directory{&origin};
    const int flags = O_RDONLY | O_CLOEXEC;
    const std::uint8_t open_directory = 5;
    const int directory_fd = native_call(
        &origin, {3930, 3931},
        std::array{raw_native_field(open_directory), raw_native_field(flags),
                   blackbird::ByteView{parent_source.lane},
                   raw_native_field(parent_source.ordinal)},
        [&](RawEventRef request) {
          const int result = ::open(parent.c_str(), flags);
          directory.accept(result, request);
          return result;
        });
    if (directory_fd < 0)
      refuse("directory open failed");
    directory.observed(3);
    const int result =
        native_call(&origin, {3930, 3931},
                    std::array{raw_native_field(sync), raw_native_field(directory_fd)},
                    [&](RawEventRef) { return ::fsync(directory_fd); });
    (void)directory.close();
    trace.require_capture();
    if (result != 0)
      refuse("directory sync failed");
  });
}
std::string git(const fs::path &repo, std::vector<std::string> args,
                const RawAdapterContext *context = nullptr) {
  const auto argc = bound(args.size(), 3);
  RawSessionCall trace{3914, 3915, context, std::array{raw_native_field(argc)}, 39};
  return trace.run([&] {
    const auto origin = trace.context();
    args.insert(args.begin(), {"git", "-C", path_text(repo, &origin)});
    for (std::size_t index = 0; index < args.size(); ++index)
      (void)argument(trace, 3916, index, args[index]);
    auto output_credit = trace.source_credit(1024 * 1024);
    blackbird::detail::Child child;
    if (trace.enabled())
      child.raw_capture(trace.reference(), true, false);
    auto child_credit = trace.control(64);
    child.start(std::move(args), {}, true);
    const auto child_ref = child.raw_reference();
    (void)trace.event(3917,
                      std::array{blackbird::ByteView{child_ref.lane},
                                 raw_native_field(child_ref.ordinal)},
                      child_credit);
    child.close_input();
    auto out = child.collect(blackbird::detail::Clock::now() + std::chrono::seconds(60),
                             1024 * 1024);
    trace.require_capture();
    (void)trace.source(3, out, output_credit, 3912);
    const bool acquired = true, exit = false;
    const auto count = out.size();
    trace.finish(RawOperationBranch::returned, nullptr,
                 std::array{raw_native_field(acquired),
                            blackbird::ByteView{child_ref.lane},
                            raw_native_field(child_ref.ordinal), raw_native_field(exit),
                            raw_native_field(acquired), raw_native_field(count)});
    trace.require_capture();
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
      out.pop_back();
    return out;
  });
}
void name(const std::string &s) {
  if (s.empty() || s.size() > 80 || s.front() == '-' ||
      s.find("..") != std::string::npos)
    refuse("invalid candidate name");
  for (char c : s)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
      refuse("invalid candidate name");
}
fs::path inside(const fs::path &base, const std::string &s,
                const RawAdapterContext *context = nullptr) {
  fs::path p(s);
  if (p.empty() || p.is_absolute())
    refuse("relative file required");
  for (const auto &part : p)
    if (part == ".." || part == ".git")
      refuse("unsafe file");
  auto b = canonical(base, context);
  auto v = canonical(base / p, context);
  auto rel = v.lexically_relative(b);
  if (rel.empty() || *rel.begin() == ".." ||
      !filesystem(context, 6, v, nullptr, [&] { return fs::is_regular_file(v); }))
    refuse("file outside checkout");
  return v;
}
void stopped(const Value &r, const RawAdapterContext *context) {
  if (field(r, "stopped", context, 1, 3922) != "all checkout programs/builds stopped")
    refuse("explicit stopped evidence required");
  if (field(r, "evidence", context, 2, 3922).empty())
    refuse("nonempty observed evidence required");
}

class Manager {
  fs::path root;
  RawAdapterContext capture_;
  Lock global;
  Value state;

public:
  explicit Manager(fs::path p, const RawAdapterContext *capture = nullptr)
      : root(std::move(p)), capture_(capture ? *capture : RawAdapterContext{}),
        global(constructed_path(&capture_, bound(root.native().size(), 5),
                                [&] { return root / "lock"; }),
               false, &capture_) {
    if (exists(root / "state.bbm", &capture_))
      state = read(root / "state.bbm", &capture_);
  }
  fs::path repo() { return str(state, "repo"); }
  fs::path checkout(std::size_t i) {
    auto path = constructed_path(&capture_, bound(root.native().size(), 26),
                                 [&] { return root / ("slot-" + std::to_string(i)); });
    if (symlink(path, &capture_))
      refuse("slot symlink refused; primary must not be aliased");
    if (exists(path, &capture_)) {
      auto actual = ::canonical(path, &capture_);
      if (actual == ::canonical(repo(), &capture_))
        refuse("slot cannot be primary checkout");
      auto list = git(repo(), {"worktree", "list", "--porcelain"}, &capture_);
      if (("\n" + list + "\n")
              .find("\nworktree " + path_text(actual, &capture_) + "\n") ==
          std::string::npos)
        refuse("slot is not an owned linked worktree");
    }
    return path;
  }
  Value &slot(std::size_t i) {
    auto &s = member(state, "slots").array();
    if (i >= s.size())
      refuse("slot outside configured pool");
    return s[i];
  }
  Value &candidate(const std::string &n) {
    for (auto &c : member(state, "candidates").array())
      if (str(c, "name") == n)
        return c;
    refuse("unknown candidate");
  }
  // Return locator. A new immutable snapshot is durable BEFORE any dispatched effect.
  std::string save(const std::string &action, const Value &evidence,
                   RawEventRef *actual_record = nullptr) {
    const auto size = action.size();
    RawSessionCall trace{3910, 3911, &capture_,
                         std::array{raw_native_field(size), raw_string_field(action)},
                         49};
    const auto previous = capture_;
    capture_ = trace.context();
    struct Restore {
      RawAdapterContext &current;
      RawAdapterContext previous;
      ~Restore() { current = previous; }
    } restore{capture_, previous};
    return trace.run([&] {
      directories(root / "records", &capture_);
      auto pattern_credit = trace.source_credit(bound(root.native().size(), 22));
      auto pattern = (root / "records" / "event-XXXXXX").string();
      std::vector<char> path(pattern.begin(), pattern.end());
      path.push_back(0);
      const auto original =
          trace.source(1, {path.data(), path.size()}, pattern_credit, 3907);
      auto id_credit = trace.source_credit(path.size());
      Descriptor descriptor{&capture_};
      const std::uint8_t temporary = 2;
      const int fd = native_call(&capture_, {3930, 3931},
                                 std::array{raw_native_field(temporary),
                                            blackbird::ByteView{original.lane},
                                            raw_native_field(original.ordinal)},
                                 [&](RawEventRef request) {
                                   const int result = ::mkstemp(path.data());
                                   descriptor.accept(result, request);
                                   return result;
                                 });
      if (fd < 0)
        refuse("event allocation failed");
      descriptor.observed(4);
      (void)descriptor.close();
      trace.require_capture();
      const std::string id = fs::path(path.data()).filename().string();
      (void)trace.source(2, id, id_credit, 3907);
      const auto *old = state.find("record");
      Value parent = old ? *old : Value{};
      const auto prior = old && std::holds_alternative<std::string>(old->value())
                             ? trace.source(3, old->string(), 3907)
                             : RawEventRef{};
      auto link_credit = trace.control(113);
      set(state, "record", text(id));
      RawEventRef record_body{}, state_body{};
      const auto record = dump(Value::object({{"parent", parent},
                                              {"action", text(action)},
                                              {"evidence", evidence},
                                              {"state", state}}),
                               &capture_, &record_body);
      write(path.data(), record, &capture_, nullptr, record_body);
      const auto current = dump(state, &capture_, &state_body);
      write(root / "state.bbm", current, &capture_, nullptr, state_body);
      if (actual_record)
        *actual_record = record_body;
      const bool completed = true;
      trace.finish(RawOperationBranch::returned, nullptr,
                   std::array{raw_native_field(completed),
                              blackbird::ByteView{record_body.lane},
                              raw_native_field(record_body.ordinal),
                              blackbird::ByteView{state_body.lane},
                              raw_native_field(state_body.ordinal)});
      (void)trace.event(
          3913,
          std::array{blackbird::ByteView{prior.lane}, raw_native_field(prior.ordinal),
                     raw_native_field(completed), blackbird::ByteView{record_body.lane},
                     raw_native_field(record_body.ordinal),
                     blackbird::ByteView{state_body.lane},
                     raw_native_field(state_body.ordinal)},
          link_credit);
      trace.require_capture();
      return id;
    });
  }
  void clean(std::size_t i) {
    if (observed(&capture_, 7,
                 !git(checkout(i), {"status", "--porcelain"}, &capture_).empty()))
      refuse("dirty checkout: checkpoint named files first");
  }
  void leased(std::size_t i) {
    const bool accepted = str(slot(i), "status") == "leased";
    predicate(&capture_, 1, accepted);
    if (!accepted)
      refuse("lease unknown/not settled; no replay");
  }
  Value perform(const std::string &cmd, const Value &r) {
    const auto length = cmd.size();
    RawSessionCall trace{3905, 3906, &capture_,
                         std::array{raw_native_field(length), raw_string_field(cmd)},
                         1};
    const auto previous = capture_;
    capture_ = trace.context();
    struct Restore {
      RawAdapterContext &current;
      RawAdapterContext previous;
      ~Restore() { current = previous; }
    } restore{capture_, previous};
    return trace.run([&] {
      auto result = perform_body(cmd, r);
      const bool completed = true;
      trace.finish(RawOperationBranch::returned, nullptr,
                   std::array{raw_native_field(completed)});
      trace.require_capture();
      return result;
    });
  }
  Value perform_body(const std::string &cmd, const Value &r) {
    if (cmd == "init") {
      if (!std::holds_alternative<std::nullptr_t>(state.value()))
        refuse("already initialized");
      auto cap = integer(r, "cap", &capture_);
      auto limit = r.find("limit") ? integer(r, "limit", &capture_) : std::size_t{4};
      if (observed(&capture_, 2, cap < 1 || cap > limit))
        refuse("cap exceeds configured limit (pilot default 4; explicit limit "
               "configurable)");
      auto primary = canonical(str(r, "repo"), &capture_);
      primary = git(primary, {"rev-parse", "--show-toplevel"}, &capture_);
      if (observed(&capture_, 3, canonical(root, &capture_) == primary))
        refuse("pool cannot be primary checkout");
      Value::Array slots;
      for (std::size_t i = 0; i < cap; ++i)
        slots.push_back(
            Value::object({{"status", text("empty")}, {"candidate", text("")}}));
      state = Value::object({{"repo", text(path_text(primary, &capture_))},
                             {"slots", Value{slots}},
                             {"candidates", Value{Value::Array{}}}});
      save(cmd, r);
      return state;
    }
    if (!state.find("repo"))
      refuse("init required");
    if (cmd == "status")
      return state;
    if (cmd == "record") {
      for (const auto &k : {"question", "action", "outcome", "references"})
        if (!r.find(k))
          refuse("question/action/outcome/references required");
      auto id = save("experiment.record", r);
      return Value::object(
          {{"record", text(id)},
           {"path", text(path_text(root / "records" / id, &capture_))}});
    }
    if (cmd == "enqueue") {
      auto n = field(r, "name", &capture_, 10);
      name(n);
      for (auto &c : member(state, "candidates").array())
        if (observed(&capture_, 4, str(c, "name") == n))
          refuse("duplicate candidate");
      // Freeze exact starting source; reject branch-name ambiguity on later dispatch.
      auto base =
          git(repo(),
              {"rev-parse", "--verify", field(r, "base", &capture_, 11) + "^{commit}"},
              &capture_);
      for (const auto &k : {"hypothesis", "discriminator", "identities", "baseline"})
        if (!r.find(k) || std::holds_alternative<std::nullptr_t>(get(r, k).value()) ||
            (std::holds_alternative<std::string>(get(r, k).value()) &&
             get(r, k).string().empty()))
          refuse("missing experiment declaration");
      auto branch = "candidate/" + n;
      if (!git(repo(), {"branch", "--list", branch}, &capture_).empty())
        refuse("branch already exists; choose a new candidate identity");
      member(state, "candidates")
          .array()
          .push_back(Value::object({{"name", text(n)},
                                    {"branch", text(branch)},
                                    {"base", text(base)},
                                    {"status", text("queued")},
                                    {"declaration", r},
                                    {"checkpoint", Value{}},
                                    {"artifacts", Value{Value::Array{}}},
                                    {"activation", Value{}}}));
      save(cmd, r);
      return candidate(n);
    }
    if (cmd == "dispatch") {
      std::size_t i = 0;
      for (; i < get(state, "slots").array().size(); ++i)
        if (str(slot(i), "status") == "empty")
          break;
      if (observed(&capture_, 5, i == get(state, "slots").array().size()))
        return Value::object(
            {{"queued", Value{true}}, {"reason", text("pool full; cap unchanged")}});
      Value *c = nullptr;
      for (auto &v : member(state, "candidates").array())
        if (str(v, "status") == "queued") {
          c = &v;
          break;
        }
      if (observed(&capture_, 6, !c))
        return Value::object(
            {{"queued", Value{false}}, {"reason", text("queue empty")}});
      Lock held(constructed_path(
                    &capture_, bound(root.native().size(), 31),
                    [&] { return root / ("slot-" + std::to_string(i) + ".lock"); }),
                true, &capture_);
      if (exists(checkout(i), &capture_))
        clean(i);
      auto n = str(*c, "name"), branch = str(*c, "branch"), base = str(*c, "base");
      set(slot(i), "candidate", text(n));
      set(slot(i), "status", text("unknown"));
      set(*c, "status", text("transition"));
      save("dispatch.intent", r);
      if (!exists(checkout(i), &capture_))
        git(repo(),
            {"worktree", "add", "-b", branch, path_text(checkout(i), &capture_), base},
            &capture_);
      else
        git(checkout(i), {"checkout", "-b", branch, base}, &capture_);
      set(slot(i), "status", text("leased"));
      set(candidate(n), "status", text("leased"));
      save("dispatch.observed", r);
      return Value::object({{"slot", num(i)},
                            {"path", text(path_text(checkout(i), &capture_))},
                            {"candidate", candidate(n)}});
    }
    auto i = integer(r, "slot", &capture_);
    Lock held(constructed_path(
                  &capture_, bound(root.native().size(), 31),
                  [&] { return root / ("slot-" + std::to_string(i) + ".lock"); }),
              true, &capture_);
    auto &s = slot(i);
    auto n = str(s, "candidate");
    if (n.empty())
      refuse("empty slot");
    if (cmd == "abandon") {
      stopped(r, &capture_);
      if (str(s, "status") != "unknown")
        refuse("abandon only uncertain transitions");
      auto raw = constructed_path(&capture_, bound(root.native().size(), 26),
                                  [&] { return root / ("slot-" + std::to_string(i)); });
      if (symlink(raw, &capture_))
        refuse("never reconcile a slot alias");
      if (exists(raw, &capture_)) {
        if (field(r, "reconcile", &capture_, 13) == "retain clean prior checkout") {
          clean(i); // Ownership and clean original branch independently observed.
          if (git(raw, {"branch", "--show-current"}, &capture_) !=
                  field(r, "observed_branch", &capture_, 14) ||
              git(raw, {"rev-parse", "HEAD"}, &capture_) !=
                  field(r, "observed_head", &capture_, 15))
            refuse("reconciliation identity mismatch");
        } else if (field(r, "reconcile", &capture_, 13) ==
                   "quarantine partial directory without execution") {
          // Explicit caller choice, never a retry or destructive cleanup. Raw
          // contents survive; stale Git registration may need separately observed
          // repair. Do not claim this is a newly certified execution checkout.
          auto id = save("abandon.quarantine.intent", r);
          directories(root / "quarantine", &capture_);
          auto destination = root / "quarantine" / id;
          candidate_rename(raw, destination, &capture_);
          Descriptor directory{&capture_};
          const auto parent =
              constructed_path(&capture_, destination.native().size(),
                               [&] { return destination.parent_path(); });
          RawSessionCall parent_original{3951, 3952, &capture_};
          const auto parent_source = parent_original.run([&] {
            return parent_original.source(
                6, {parent.c_str(), parent.native().size() + 1}, 3907);
          });
          const int flags = O_RDONLY | O_CLOEXEC;
          const std::uint8_t method = 5;
          const int fd =
              native_call(&capture_, {3930, 3931},
                          std::array{raw_native_field(method), raw_native_field(flags),
                                     blackbird::ByteView{parent_source.lane},
                                     raw_native_field(parent_source.ordinal)},
                          [&](RawEventRef request) {
                            const int result = ::open(parent.c_str(), flags);
                            directory.accept(result, request);
                            return result;
                          });
          if (fd < 0)
            refuse("quarantine directory open failed");
          directory.observed(5);
          const std::uint8_t sync = 3;
          const int synced =
              native_call(&capture_, {3930, 3931},
                          std::array{raw_native_field(sync), raw_native_field(fd)},
                          [&](RawEventRef) { return ::fsync(fd); });
          (void)directory.close();
          if (synced != 0)
            refuse("quarantine directory sync failed; outcome unknown");
          set(s, "quarantine", text(path_text(destination, &capture_)));
        } else
          refuse("present checkout needs observed reconciliation or preservation");
      }
      auto &c = candidate(n);
      // A missing checkout is observed, not a license to retry the effect.
      auto branches = git(repo(), {"branch", "--list", str(c, "branch")}, &capture_);
      if (!branches.empty())
        set(c, "checkpoint",
            text(git(repo(), {"rev-parse", str(c, "branch")}, &capture_)));
      set(c, "status", text("abandoned transition; retained source/records"));
      set(s, "status", text("empty"));
      set(s, "candidate", text(""));
      save("abandon.observed", r);
      return c;
    }
    if (cmd == "settle") {
      stopped(r, &capture_);
      if (git(checkout(i), {"branch", "--show-current"}, &capture_) !=
          str(candidate(n), "branch"))
        refuse("branch mismatch; inspect transition, do not replay");
      set(s, "status", text("leased"));
      set(candidate(n), "status", text("leased"));
      save("settle.observed", r);
      return s;
    }
    leased(i);
    if (cmd == "run") {
      auto seconds = integer(r, "seconds", &capture_);
      if (seconds < 1 || seconds > 3600)
        refuse("seconds must be 1..3600");
      std::vector<std::string> args{"/bin/sh", "-c",
                                    "cd -- \"$1\" && shift && exec \"$@\"",
                                    "candidate-run", path_text(checkout(i), &capture_)};
      for (auto &v : get(r, "argv").array())
        args.push_back(v.string());
      if (args.size() == 5)
        refuse("nonempty argv required");
      const auto argc = args.size();
      const std::size_t limit = 16 * 1024 * 1024;
      RawSessionCall run_trace{3918, 3919, &capture_,
                               std::array{raw_native_field(seconds),
                                          raw_native_field(limit),
                                          raw_native_field(argc)},
                               38};
      return run_trace.run([&] {
        const auto previous = capture_;
        capture_ = run_trace.context();
        struct Restore {
          RawAdapterContext &current;
          RawAdapterContext previous;
          ~Restore() { current = previous; }
        } restore{capture_, previous};
        auto intent_credit = run_trace.control(73),
             observation_phase = run_trace.control(73);
        set(s, "status", text("running"));
        set(candidate(n), "status", text("running"));
        RawEventRef intent{};
        const auto id = save("run.intent", r, &intent);
        const std::uint8_t intent_phase = 1;
        (void)run_trace.event(
            3920,
            std::array{raw_native_field(intent_phase), blackbird::ByteView{intent.lane},
                       raw_native_field(intent.ordinal), raw_native_field(i)},
            intent_credit);
        global.unlock(&capture_);
        for (std::size_t index = 0; index < args.size(); ++index)
          (void)argument(run_trace, 3916, index, args[index]);
        auto output_credit = run_trace.source_credit(limit);
        auto observation_credit = run_trace.control(73);
        auto child_credit = run_trace.control(64);
        std::optional<blackbird::detail::Child> owned_child{std::in_place};
        auto &child = *owned_child;
        if (run_trace.enabled())
          child.raw_capture(run_trace.reference(), true, false);
        std::string output;
        bool partial = false, observed = false;
        int code = 0;
        try {
          child.start(std::move(args), {}, true);
          const auto child_ref = child.raw_reference();
          (void)run_trace.event(3917,
                                std::array{blackbird::ByteView{child_ref.lane},
                                           raw_native_field(child_ref.ordinal)},
                                child_credit);
          child.close_input();
          output = child.collect(blackbird::detail::Clock::now() +
                                     std::chrono::seconds(seconds),
                                 limit, &code);
          observed = true;
        } catch (...) {
          // This check runs while Child still owns its funded stop/join. A raw
          // failure escapes and destroys it before any ordinary replacement save.
          run_trace.require_capture();
          output = child.take_partial();
          partial = true;
        }
        run_trace.require_capture();
        const auto original =
            run_trace.source(partial ? 4 : 3, output, output_credit, 3912);
        const auto count = output.size();
        (void)run_trace.event(
            3921,
            std::array{raw_native_field(partial), blackbird::ByteView{original.lane},
                       raw_native_field(original.ordinal), raw_native_field(count)},
            observation_credit);
        write(root / "records" / (id + ".output"), output, &capture_, nullptr,
              original);
        Value outcome =
            observed
                ? Value::object({{"exit_code", num(static_cast<std::size_t>(code))},
                                 {"outcome", text("observed leader exit; descendants "
                                                  "not certified stopped")}})
                : Value::object({{"outcome", text("unknown; no automatic replay")}});
        global.lock(&capture_);
        state = read(root / "state.bbm", &capture_);
        set(slot(i), "status", text("unknown"));
        set(slot(i), "last_run", text(id));
        set(candidate(n), "status", text("unknown"));
        RawEventRef observation{};
        save("run.observation", outcome, &observation);
        const std::uint8_t observed_phase = 2;
        (void)run_trace.event(3920,
                              std::array{raw_native_field(observed_phase),
                                         blackbird::ByteView{observation.lane},
                                         raw_native_field(observation.ordinal),
                                         raw_native_field(i)},
                              observation_phase);
        auto result = Value::object(
            {{"run", text(id)},
             {"output",
              text(path_text(root / "records" / (id + ".output"), &capture_))},
             {"observation", outcome}});
        // The original child lives through observation/result construction. Finish
        // its paid native join/close before the invocation's returned terminal.
        owned_child.reset();
        const bool output_present = true;
        if (observed)
          run_trace.finish(
              RawOperationBranch::returned, nullptr,
              std::array{raw_native_field(observed), raw_native_field(code),
                         raw_native_field(output_present),
                         blackbird::ByteView{original.lane},
                         raw_native_field(original.ordinal), raw_native_field(count)});
        else
          run_trace.finish(
              RawOperationBranch::returned, nullptr,
              std::array{raw_native_field(observed), raw_native_field(output_present),
                         blackbird::ByteView{original.lane},
                         raw_native_field(original.ordinal), raw_native_field(count)});
        run_trace.require_capture();
        return result;
      });
    }
    stopped(r, &capture_);
    auto &c = candidate(n);
    if (cmd == "checkpoint") {
      auto files = get(r, "files").array();
      if (observed(&capture_, 8,
                   !git(checkout(i), {"diff", "--cached", "--name-only"}, &capture_)
                        .empty()))
        refuse("preexisting staged changes: inspect and unstage explicitly first");
      for (auto &v : files)
        (void)inside(checkout(i), v.string(), &capture_);
      set(s, "status", text("unknown"));
      save("checkpoint.intent", r);
      if (!files.empty()) {
        std::vector<std::string> args{"add", "--"};
        for (auto &v : files)
          args.push_back(v.string());
        git(checkout(i), args, &capture_);
        if (observed(&capture_, 8,
                     !git(checkout(i), {"diff", "--cached", "--name-only"}, &capture_)
                          .empty()))
          git(checkout(i), {"commit", "-m", field(r, "message", &capture_, 12)},
              &capture_);
      }
      clean(i);
      set(c, "checkpoint", text(git(checkout(i), {"rev-parse", "HEAD"}, &capture_)));
      set(s, "status", text("leased"));
      save("checkpoint.observed", r);
      return c;
    }
    if (cmd == "artifact") {
      auto file = inside(checkout(i), str(r, "file"), &capture_);
      auto hash =
          git(checkout(i), {"hash-object", path_text(file, &capture_)}, &capture_);
      directories(root / "artifacts", &capture_);
      auto destination = root / "artifacts" / hash;
      if (!exists(destination, &capture_)) {
        blackbird::RawCaptureFailureScope artifact{selected(&capture_)};
        std::string bytes;
        try {
          bytes = blackbird::raw_file_read(file, SIZE_MAX, &capture_);
        } catch (const blackbird::Error &error) {
          artifact.require();
          if (error.code != blackbird::ErrorCode::io)
            throw;
        }
        write(destination, bytes, &capture_);
      }
      member(c, "artifacts")
          .array()
          .push_back(
              Value::object({{"file", text(path_text(file, &capture_))},
                             {"content", text(hash)},
                             {"retained", text(path_text(destination, &capture_))}}));
      save(cmd, r);
      return c;
    }
    if (cmd == "activation") {
      if (std::holds_alternative<std::nullptr_t>(get(c, "checkpoint").value()))
        refuse("checkpoint before activation record");
      if (field(r, "source_commit", &capture_, 3, 3922) != str(c, "checkpoint"))
        refuse("activation source mismatch");
      std::uint8_t evidence_kind = 4;
      for (const auto &k : {"qualification", "effective_program", "request", "mode"})
        if (field(r, k, &capture_, evidence_kind++, 3922).empty())
          refuse("activation evidence missing");
      // Evidence is observed by caller, not inferred from commit or self-rated.
      set(c, "activation", r);
      save("activation.observed", r);
      return c;
    }
    if (cmd == "release" || cmd == "retire") {
      clean(i);
      if (!c.find("checkpoint") ||
          !std::holds_alternative<std::string>(get(c, "checkpoint").value()) ||
          str(c, "checkpoint") != git(checkout(i), {"rev-parse", "HEAD"}, &capture_))
        refuse("record checkpoint before release");
      if (str(r, "artifacts") != "all relevant artifacts retained")
        refuse("retain declared artifacts first");
      set(s, "status", text("unknown"));
      save(cmd + ".intent", r);
      if (cmd == "retire")
        git(repo(), {"worktree", "remove", path_text(checkout(i), &capture_)},
            &capture_);
      set(c, "status", text("archived"));
      set(s, "status", text("empty"));
      set(s, "candidate", text(""));
      save(cmd + ".observed", r);
      return c;
    }
    refuse("unknown command");
  }
};
} // namespace
class CandidateCli {
  blackbird::RawEmission emission_;

public:
  explicit CandidateCli(int argc) {
    emission_.start({3900, 3902}, 77, std::array{raw_native_field(argc)});
  }
  RawAdapterContext context() const {
    return {nullptr, emission_.reference(), nullptr, nullptr};
  }
  void finish(RawOperationBranch branch,
              const blackbird::Error *error = nullptr) noexcept {
    const bool success = branch == RawOperationBranch::returned;
    const int exit = success ? 0 : 1;
    const std::array fields{raw_native_field(success), raw_native_field(exit)};
    emission_.finish(branch, 0, error, std::span{fields}.first(success ? 2 : 1));
  }
};
int main(int argc, char **argv) {
  try {
    if (argc != 4)
      refuse("usage: blackbird-candidate POOL COMMAND REQUEST.bbm (or explicit JSON "
             "interchange REQUEST.json)");
    fs::path root = fs::absolute(argv[1]);
    fs::create_directories(root);
    // Bootstrap needs a private recorder directory; the existing pool and its
    // permissions retain their original business behavior.
    const auto raw_root = root / ".raw-candidate";
    if (::mkdir(raw_root.c_str(), 0700) != 0 && errno != EEXIST)
      throw blackbird::Error{blackbird::ErrorCode::io, errno};
    struct stat raw_status{};
    if (::lstat(raw_root.c_str(), &raw_status) != 0)
      throw blackbird::Error{blackbird::ErrorCode::io, errno};
    if (!S_ISDIR(raw_status.st_mode) || raw_status.st_uid != ::geteuid() ||
        (raw_status.st_mode & 0777) != 0700)
      refuse("private owned .raw-candidate directory required");
    blackbird::IdentityBytes identity{};
    if (::getentropy(identity.data(), identity.size()) != 0)
      throw blackbird::Error{blackbird::ErrorCode::io, errno};
    const auto environment =
        blackbird::unwrap(blackbird::EnvironmentId::from_bytes(identity));
    auto runtime =
        blackbird::unwrap(blackbird::RawRuntime::open(raw_root.string(), environment));
    auto handle = blackbird::unwrap(runtime->prepare_thread());
    blackbird::RawThreadBinding binding{handle};
    blackbird::RawCaptureFailureScope capture{true};
    CandidateCli cli{argc};
    blackbird::raw_call_run(cli, [&] {
      const auto context = cli.context();
      RawSessionCall arguments{3953, 3954, &context};
      arguments.run([&] {
        for (int index = 0; index < argc; ++index)
          (void)argument(arguments, 3901, static_cast<std::size_t>(index), argv[index]);
      });
      Manager manager(root, &context);
      const fs::path request_path{argv[3]};
      const bool json_interchange = request_path.extension() == ".json";
      Value request;
      if (json_interchange) {
        const std::uint8_t role = 2;
        const auto path_size = request_path.native().size();
        RawSessionCall input{
            3903, 3904, &context,
            std::array{raw_native_field(role), raw_native_field(path_size)}, 33};
        request = input.run([&] {
          const auto origin = input.context();
          std::string bytes;
          RawEventRef acquired{};
          try {
            bytes =
                blackbird::raw_file_read(request_path, SIZE_MAX, &origin, &acquired);
          } catch (const blackbird::Error &error) {
            input.require_capture();
            if (error.code != blackbird::ErrorCode::io)
              throw;
            refuse("cannot read import");
          }
          input.require_capture();
          auto parsed = blackbird::parse_json(bytes);
          if (!parsed.has_value())
            throw parsed.error();
          const bool present = acquired.ordinal != 0;
          const auto count = bytes.size();
          input.finish(
              RawOperationBranch::returned, nullptr,
              std::array{raw_native_field(present), blackbird::ByteView{acquired.lane},
                         raw_native_field(acquired.ordinal), raw_native_field(count)});
          input.require_capture();
          return std::move(parsed).value();
        });
      } else
        request = read(request_path, &context);
      auto result = manager.perform(argv[2], request);
      RawSessionCall output{3955, 3956, &context};
      output.run([&] {
        auto credit = output.source_credit(16 * 1024 * 1024);
        std::string bytes;
        if (json_interchange) {
          auto encoded = blackbird::dump_json(result);
          if (!encoded.has_value())
            throw encoded.error();
          bytes = std::move(encoded).value();
        } else {
          auto encoded = blackbird::encode_packet_string(result);
          if (!encoded.has_value())
            refuse("binary encoding failed");
          bytes = std::move(encoded).value();
        }
        (void)output.source(json_interchange ? 6 : 5, bytes, credit, 3912);
        if (json_interchange)
          std::cout << bytes << '\n';
        else
          std::cout << bytes;
      });
      capture.require();
    });
    capture.require();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  } catch (const blackbird::Error &error) {
    std::cerr << "native operation failed; inspect recorded transition: "
              << static_cast<int>(error.code) << '\n';
  }
  return 1;
}
