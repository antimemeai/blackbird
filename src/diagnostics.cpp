#include "blackbird/journal_storage.hpp"
#include <cerrno>
#include <charconv>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace blackbird {
namespace {
std::optional<std::uint64_t> expiration(std::string_view name) {
  const auto dot = name.find('.');
  if (dot == std::string_view::npos || !dot || dot > 20 ||
      name.size() - dot - 1 != 32 ||
      name.substr(dot + 1).find_first_not_of("0123456789abcdef") !=
          std::string_view::npos)
    return {};
  std::uint64_t result = 0;
  auto parsed = std::from_chars(name.data(), name.data() + dot, result);
  if (parsed.ec != std::errc{} || parsed.ptr != name.data() + dot ||
      std::to_string(result) != name.substr(0, dot))
    return {};
  return result;
}
bool private_file(RawStorageAction &action, const struct stat &s) {
  return S_ISREG(s.st_mode) && s.st_uid == RawStorageNative::uid(action) &&
         !(s.st_mode & 0077) && s.st_nlink == 1;
}
RawProductStorageOwner owner_of(RawStorageResource *resource) {
  return resource ? resource->owner() : RawProductStorageOwner{};
}
std::string_view cstring(const std::string &value) {
  return {value.c_str(), value.size() + 1};
}
template <class R, class F>
R capture(RawStorageResource *resource, RawStorageActionKind kind,
          std::span<const ByteView> arguments, F &&body) {
  try {
    RawStorageAction action{owner_of(resource), kind, resource, arguments};
    try {
      auto result = body(action);
      action.finish(result.has_value() ? std::optional<Error>{} : result.error());
      return result;
    } catch (const Error &error) {
      action.finish(error);
      return R::failure(error);
    } catch (const std::bad_alloc &) {
      action.finish(Error{ErrorCode::allocation});
      return R::failure({ErrorCode::allocation});
    }
  } catch (const Error &error) {
    return R::failure(error);
  } catch (const std::bad_alloc &) {
    return R::failure({ErrorCode::allocation});
  }
}
struct FileGuard {
  int descriptor = -1;
  std::unique_ptr<RawStorageResource> raw;
  explicit FileGuard(const RawProductStorageOwner &owner)
      : raw(RawStorageResource::prepare(owner)) {}
  ~FileGuard() { close(); }
  void close() noexcept {
    if (raw)
      raw->close();
    else if (descriptor >= 0)
      ::close(descriptor);
    descriptor = -1;
  }
};
} // namespace
class DiagnosticStore {
public:
  int fd = -1;
  DIR *scan = nullptr;
  std::unique_ptr<RawStorageResource> raw, scan_raw;
  explicit DiagnosticStore(const RawProductStorageOwner &owner)
      : raw(RawStorageResource::prepare(owner)) {}
  ~DiagnosticStore() {
    if (scan && !scan_raw)
      ::closedir(scan);
    if (fd >= 0 && !raw)
      ::close(fd);
  }
  static Result<std::shared_ptr<DiagnosticStore>>
  open(RawStorageAction &action, int parent, const RawProductStorageOwner &owner) {
    if (RawStorageNative::mkdir(action, parent, {"diagnostics", 12}, mode_t{0700}) <
            0 &&
        errno != EEXIST)
      return Result<std::shared_ptr<DiagnosticStore>>::failure({ErrorCode::io, errno});
    auto result = std::make_shared<DiagnosticStore>(owner);
    result->fd =
        RawStorageNative::open(action, result->raw.get(), parent, {"diagnostics", 12},
                               O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (result->fd < 0)
      return Result<std::shared_ptr<DiagnosticStore>>::failure({ErrorCode::io, errno});
    struct stat s{};
    if (RawStorageNative::stat(action, result->fd, s) < 0)
      return Result<std::shared_ptr<DiagnosticStore>>::failure({ErrorCode::io, errno});
    if (!S_ISDIR(s.st_mode) || s.st_uid != RawStorageNative::uid(action) ||
        (s.st_mode & 0077))
      return Result<std::shared_ptr<DiagnosticStore>>::failure({ErrorCode::io, EPERM});
    return Result<std::shared_ptr<DiagnosticStore>>::success(std::move(result));
  }
  Result<ScratchCleanup> purge(RawStorageAction &action, std::uint64_t now) {
    if (!scan) {
      scan_raw = RawStorageResource::prepare(owner_of(raw.get()), true);
      action.use(raw.get());
      const int separate = RawStorageNative::open(action, scan_raw.get(), fd, {".", 2},
                                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC);
      if (separate < 0)
        return Result<ScratchCleanup>::failure({ErrorCode::io, errno});
      scan = RawStorageNative::directory(action, scan_raw.get(), separate);
      if (!scan) {
        const int error = errno;
        if (scan_raw)
          scan_raw.reset();
        else
          ::close(separate);
        return Result<ScratchCleanup>::failure({ErrorCode::io, error});
      }
    }
    ScratchCleanup result;
    while (result.scanned < 128 && result.removed < 16) {
      action.use(scan_raw.get());
      auto *entry = RawStorageNative::next(action, scan);
      if (!entry) {
        const int error = errno;
        if (scan_raw)
          scan_raw.reset();
        else
          ::closedir(scan);
        scan = nullptr;
        if (error)
          return Result<ScratchCleanup>::failure({ErrorCode::io, error});
        result.complete = true;
        break;
      }
      ++result.scanned;
      const auto expires = expiration(entry->d_name);
      if (!expires || *expires > now)
        continue;
      const std::string_view name{entry->d_name,
                                  std::char_traits<char>::length(entry->d_name) + 1};
      action.use(raw.get());
      struct stat s{};
      if (RawStorageNative::stat_at(action, fd, name, s, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno == ENOENT)
          continue;
        return Result<ScratchCleanup>::failure({ErrorCode::io, errno});
      }
      if (!private_file(action, s))
        continue;
      if (RawStorageNative::unlink(action, fd, name, 0) < 0) {
        if (errno == ENOENT)
          continue;
        return Result<ScratchCleanup>::failure({ErrorCode::io, errno});
      }
      ++result.removed;
    }
    return Result<ScratchCleanup>::success(result);
  }
};
Result<ScratchCleanup> NativeJournalDirectory::expire_diagnostics(std::uint64_t now) {
  const std::array fields{detail::raw_native_field(now)};
  return capture<Result<ScratchCleanup>>(
      raw_.get(), RawStorageActionKind::expire_diagnostics, fields,
      [&](RawStorageAction &action) {
        if (!diagnostics_) {
          auto opened =
              DiagnosticStore::open(action, descriptor_, owner_of(raw_.get()));
          if (!opened.has_value())
            return Result<ScratchCleanup>::failure(opened.error());
          diagnostics_ = std::move(opened).value();
        }
        return diagnostics_->purge(action, now);
      });
}
Result<void> NativeJournalDirectory::store_diagnostic(std::string_view name,
                                                      ByteView bytes,
                                                      std::uint64_t now) {
  const auto size = bytes.size();
  const std::array fields{detail::raw_native_field(now),
                          detail::raw_native_field(size)};
  return capture<Result<void>>(
      raw_.get(), RawStorageActionKind::store_diagnostic, fields,
      [&](RawStorageAction &action) {
        action.operand(1, detail::raw_string_field(name));
        const auto expires = expiration(name);
        if (!expires || *expires <= now || *expires - now != 30ULL * 24 * 60 * 60 ||
            bytes.size() > 65536)
          return Result<void>::failure({ErrorCode::invalid_range});
        auto purged = expire_diagnostics(now);
        if (!purged.has_value())
          return Result<void>::failure(purged.error());
        const std::string filename{name};
        action.use(diagnostics_->raw.get());
        RawStorageDiagnosticUnlink failure_unlink{action, diagnostics_->fd,
                                                  cstring(filename)};
        FileGuard file{owner_of(raw_.get())};
        action.use(diagnostics_->raw.get());
        file.descriptor = RawStorageNative::open(
            action, file.raw.get(), diagnostics_->fd, cstring(filename),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode_t{0600});
        if (file.descriptor < 0)
          return Result<void>::failure({ErrorCode::io, errno});
        std::size_t offset = 0;
        Error error{ErrorCode::io};
        while (offset < bytes.size()) {
          const auto n =
              RawStorageNative::write(action, file.descriptor, bytes.subspan(offset));
          if (n < 0) {
            if (errno == EINTR)
              continue;
            error.detail = errno;
            break;
          }
          if (!n)
            break;
          offset += static_cast<std::size_t>(n);
        }
        file.close();
        if (offset != bytes.size()) {
          failure_unlink.run();
          return Result<void>::failure(error);
        }
        return Result<void>::success();
      });
}
Result<std::vector<std::byte>>
NativeJournalDirectory::read_diagnostic(std::string_view name, std::uint64_t now) {
  using R = Result<std::vector<std::byte>>;
  const std::array fields{detail::raw_native_field(now)};
  return capture<R>(
      raw_.get(), RawStorageActionKind::read_diagnostic, fields,
      [&](RawStorageAction &action) {
        action.operand(1, detail::raw_string_field(name));
        const auto expires = expiration(name);
        if (!expires)
          return R::failure({ErrorCode::invalid_range});
        if (*expires <= now)
          return R::failure({ErrorCode::stale_handle});
        if (!diagnostics_) {
          auto opened =
              DiagnosticStore::open(action, descriptor_, owner_of(raw_.get()));
          if (!opened.has_value())
            return R::failure(opened.error());
          diagnostics_ = std::move(opened).value();
        }
        const std::string filename{name};
        FileGuard file{owner_of(raw_.get())};
        action.use(diagnostics_->raw.get());
        file.descriptor = RawStorageNative::open(action, file.raw.get(),
                                                 diagnostics_->fd, cstring(filename),
                                                 O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (file.descriptor < 0)
          return R::failure(
              {errno == ENOENT ? ErrorCode::stale_handle : ErrorCode::io, errno});
        struct stat s{};
        if (RawStorageNative::stat(action, file.descriptor, s) < 0)
          return R::failure({ErrorCode::io, errno});
        if (!private_file(action, s) || s.st_size < 0 || s.st_size > 65536)
          return R::failure({ErrorCode::io, EPERM});
        std::vector<std::byte> result(static_cast<std::size_t>(s.st_size));
        std::size_t offset = 0;
        while (offset < result.size()) {
          const auto n = RawStorageNative::read(action, file.descriptor,
                                                std::span{result}.subspan(offset));
          if (n < 0 && errno == EINTR)
            continue;
          if (n <= 0)
            return R::failure({ErrorCode::io, n < 0 ? errno : 0});
          // Each exact acquired prefix was already emitted by the native helper.
          offset += static_cast<std::size_t>(n);
        }
        file.close();
        return R::success(std::move(result));
      });
}
} // namespace blackbird
