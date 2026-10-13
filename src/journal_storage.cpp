#include "blackbird/journal_storage.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace blackbird {
namespace {
Error native_error(int code) noexcept {
  if (code == EINTR) {
    return {ErrorCode::interrupted, code};
  }
  if (code == ENOTSUP) {
    return {ErrorCode::unsupported, code};
  }
  return {ErrorCode::io, code};
}
bool valid_io(std::uint64_t offset, std::size_t size) noexcept {
  static_assert(std::numeric_limits<off_t>::is_signed);
  const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());
  return offset <= maximum && size <= maximum - offset &&
         size <= static_cast<std::size_t>(std::numeric_limits<ssize_t>::max());
}
Result<void> synchronize(RawStorageAction &action, int descriptor,
                         SyncStrength strength) {
  const int status = RawStorageNative::sync(action, descriptor, strength);
  if (status == -1) {
    return Result<void>::failure(native_error(errno));
  }
  return Result<void>::success();
}
Result<void> restricted_descriptor(RawStorageAction &action, int descriptor,
                                   bool directory) {
  struct stat metadata{};
  if (RawStorageNative::stat(action, descriptor, metadata) == -1) {
    return Result<void>::failure(native_error(errno));
  }
  if ((directory ? !S_ISDIR(metadata.st_mode) : !S_ISREG(metadata.st_mode)) ||
      metadata.st_uid != RawStorageNative::uid(action) ||
      (metadata.st_mode & 0077) != 0) {
    return Result<void>::failure({ErrorCode::io, EPERM});
  }
  return Result<void>::success();
}
bool valid_component(std::string_view name) noexcept {
  return !name.empty() && name != "." && name != ".." &&
         name.find('/') == std::string_view::npos &&
         name.find('\0') == std::string_view::npos;
}
} // namespace

NativeJournalFile::~NativeJournalFile() {
  if (!raw_ && descriptor_ != -1)
    (void)::close(descriptor_);
}
NativeJournalFile::NativeJournalFile(NativeJournalFile &&other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)), raw_(std::move(other.raw_)) {}
NativeJournalFile &NativeJournalFile::operator=(NativeJournalFile &&other) noexcept {
  if (this != &other) {
    if (!raw_ && descriptor_ != -1)
      (void)::close(descriptor_);
    raw_ = std::move(other.raw_);
    descriptor_ = std::exchange(other.descriptor_, -1);
  }
  return *this;
}
namespace {
template <class R, class F>
R capture(const RawProductStorageOwner &owner, RawStorageActionKind kind,
          RawStorageResource *resource, std::span<const ByteView> arguments, F &&body,
          RawStorageWriteContinuation *continuation = nullptr) {
  try {
    RawStorageAction action{owner, kind, resource, arguments, continuation};
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
RawProductStorageOwner storage_owner(RawStorageResource *resource) {
  return resource ? resource->owner() : RawProductStorageOwner{};
}
std::string_view cstring(const std::string &value) {
  return {value.c_str(), value.size() + 1};
}
} // namespace
Result<std::size_t> NativeJournalFile::read_at(std::uint64_t offset,
                                               MutableByteView output) {
  const auto size = output.size();
  const std::array fields{detail::raw_native_field(offset),
                          detail::raw_native_field(size)};
  return capture<Result<std::size_t>>(
      storage_owner(raw_.get()), RawStorageActionKind::read_at, raw_.get(), fields,
      [&](RawStorageAction &action) {
        if (!valid_io(offset, size))
          return Result<std::size_t>::failure({ErrorCode::invalid_range});
        const auto count = RawStorageNative::read(action, descriptor_, output,
                                                  static_cast<off_t>(offset));
        return count < 0
                   ? Result<std::size_t>::failure(native_error(errno))
                   : Result<std::size_t>::success(static_cast<std::size_t>(count));
      });
}
Result<void>
NativeJournalFile::begin_write_batch(std::size_t bytes,
                                     RawStorageWriteContinuation &continuation) {
  try {
    if (raw_)
      raw_->begin_write(bytes, continuation);
    return Result<void>::success();
  } catch (const Error &error) {
    return Result<void>::failure(error);
  }
}
Result<std::size_t> NativeJournalFile::write_at(std::uint64_t offset, ByteView input) {
  RawStorageWriteContinuation continuation;
  auto begin = begin_write_batch(input.size(), continuation);
  if (!begin.has_value())
    return Result<std::size_t>::failure(begin.error());
  return write_at_continuing(offset, input, continuation);
}
Result<std::size_t>
NativeJournalFile::write_at_continuing(std::uint64_t offset, ByteView input,
                                       RawStorageWriteContinuation &continuation) {
  const auto size = input.size();
  const std::array fields{detail::raw_native_field(offset),
                          detail::raw_native_field(size)};
  return capture<Result<std::size_t>>(
      storage_owner(raw_.get()), RawStorageActionKind::write_at, raw_.get(), fields,
      [&](RawStorageAction &action) {
        if (!valid_io(offset, size))
          return Result<std::size_t>::failure({ErrorCode::invalid_range});
        const auto count = RawStorageNative::write(action, descriptor_, input,
                                                   static_cast<off_t>(offset));
        return count < 0
                   ? Result<std::size_t>::failure(native_error(errno))
                   : Result<std::size_t>::success(static_cast<std::size_t>(count));
      },
      &continuation);
}
Result<std::uint64_t> NativeJournalFile::extent() {
  return capture<Result<std::uint64_t>>(
      storage_owner(raw_.get()), RawStorageActionKind::extent, raw_.get(), {},
      [&](RawStorageAction &action) {
        struct stat metadata{};
        if (RawStorageNative::stat(action, descriptor_, metadata) < 0)
          return Result<std::uint64_t>::failure(native_error(errno));
        if (metadata.st_size < 0)
          return Result<std::uint64_t>::failure({ErrorCode::invalid_range});
        return Result<std::uint64_t>::success(
            static_cast<std::uint64_t>(metadata.st_size));
      });
}
Result<void> NativeJournalFile::synchronize(SyncStrength strength) {
  const std::array fields{detail::raw_native_field(strength)};
  return capture<Result<void>>(
      storage_owner(raw_.get()), RawStorageActionKind::synchronize, raw_.get(), fields,
      [&](RawStorageAction &action) {
        return blackbird::synchronize(action, descriptor_, strength);
      });
}
Result<void> NativeJournalFile::lock_writer() {
  return capture<Result<void>>(
      storage_owner(raw_.get()), RawStorageActionKind::lock_writer, raw_.get(), {},
      [&](RawStorageAction &action) {
        if (raw_)
          raw_->prepare_release();
        const auto status =
            RawStorageNative::lock(action, descriptor_, LOCK_EX | LOCK_NB);
        if (raw_)
          raw_->acquired_writer(status == 0);
        if (status < 0) {
          const auto detail = errno;
          return Result<void>::failure(detail == EWOULDBLOCK
                                           ? Error{ErrorCode::busy, detail}
                                           : native_error(detail));
        }
        return Result<void>::success();
      });
}
void NativeJournalFile::release_writer() noexcept {
  if (raw_)
    raw_->release_writer();
  else
    while (::flock(descriptor_, LOCK_UN) < 0 && errno == EINTR) {
    }
}

Result<NativeJournalDirectory>
NativeJournalDirectory::open(std::string_view path,
                             const RawProductStorageOwner &owner) {
  const bool present = false;
  const std::array fields{detail::raw_native_field(present)};
  return capture<Result<NativeJournalDirectory>>(
      owner, RawStorageActionKind::open, nullptr, fields,
      [&](RawStorageAction &action) {
        action.operand(1, detail::raw_string_field(path));
        if (path.empty() || path.find('\0') != std::string_view::npos)
          return Result<NativeJournalDirectory>::failure({ErrorCode::invalid_range});
        NativeJournalDirectory result{-1, RawStorageResource::prepare(owner)};
        const std::string name{path};
        result.descriptor_ =
            RawStorageNative::open(action, result.raw_.get(), {}, cstring(name),
                                   O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (result.descriptor_ < 0)
          return Result<NativeJournalDirectory>::failure(native_error(errno));
        auto checked = restricted_descriptor(action, result.descriptor_, true);
        if (!checked.has_value())
          return Result<NativeJournalDirectory>::failure(checked.error());
        return Result<NativeJournalDirectory>::success(std::move(result));
      });
}
NativeJournalDirectory::~NativeJournalDirectory() {
  if (cleanup_stream_ && !cleanup_raw_)
    ::closedir(static_cast<DIR *>(cleanup_stream_));
  if (descriptor_ != -1 && !raw_)
    ::close(descriptor_);
}
Result<NativeJournalDirectory>
NativeJournalDirectory::open_at(int directory, std::string_view name,
                                const RawProductStorageOwner &owner) {
  const bool present = true;
  const std::array fields{detail::raw_native_field(present),
                          detail::raw_native_field(directory)};
  return capture<Result<NativeJournalDirectory>>(
      owner, RawStorageActionKind::open_at, nullptr, fields,
      [&](RawStorageAction &action) {
        action.operand(1, detail::raw_string_field(name));
        if (!valid_component(name))
          return Result<NativeJournalDirectory>::failure({ErrorCode::invalid_range});
        NativeJournalDirectory result{-1, RawStorageResource::prepare(owner)};
        const std::string component{name};
        result.descriptor_ = RawStorageNative::open(
            action, result.raw_.get(), directory, cstring(component),
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (result.descriptor_ < 0)
          return Result<NativeJournalDirectory>::failure(native_error(errno));
        const auto checked = restricted_descriptor(action, result.descriptor_, true);
        if (!checked.has_value())
          return Result<NativeJournalDirectory>::failure(checked.error());
        return Result<NativeJournalDirectory>::success(std::move(result));
      });
}
Result<int> NativeJournalDirectory::open_child_directory(std::string_view name) const {
  if (!valid_component(name))
    return Result<int>::failure({ErrorCode::invalid_range});
  try {
    const std::string component{name};
    const int descriptor = ::openat(descriptor_, component.c_str(),
                                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (descriptor < 0)
      return Result<int>::failure(native_error(errno));
    struct stat metadata{};
    if (::fstat(descriptor, &metadata) != 0) {
      const auto error = errno;
      (void)::close(descriptor);
      return Result<int>::failure(native_error(error));
    }
    if (!S_ISDIR(metadata.st_mode) || metadata.st_uid != ::geteuid()) {
      (void)::close(descriptor);
      return Result<int>::failure({ErrorCode::io, EPERM});
    }
    return Result<int>::success(descriptor);
  } catch (const std::bad_alloc &) {
    return Result<int>::failure({ErrorCode::allocation});
  }
}
NativeJournalDirectory::NativeJournalDirectory(NativeJournalDirectory &&other) noexcept
    : cleanup_stream_(std::exchange(other.cleanup_stream_, nullptr)),
      cleanup_journal_(std::move(other.cleanup_journal_)),
      cleanup_status_(other.cleanup_status_),
      descriptor_(std::exchange(other.descriptor_, -1)),
      diagnostics_(std::move(other.diagnostics_)), raw_(std::move(other.raw_)),
      cleanup_raw_(std::move(other.cleanup_raw_)) {}
NativeJournalDirectory &
NativeJournalDirectory::operator=(NativeJournalDirectory &&other) noexcept {
  if (this != &other) {
    if (cleanup_stream_ && !cleanup_raw_)
      ::closedir(static_cast<DIR *>(cleanup_stream_));
    cleanup_stream_ = std::exchange(other.cleanup_stream_, nullptr);
    cleanup_journal_ = std::move(other.cleanup_journal_);
    cleanup_status_ = other.cleanup_status_;
    diagnostics_ = std::move(other.diagnostics_);
    if (descriptor_ != -1 && !raw_) {
      (void)::close(descriptor_);
    }
    descriptor_ = std::exchange(other.descriptor_, -1);
    raw_ = std::move(other.raw_);
    cleanup_raw_ = std::move(other.cleanup_raw_);
  }
  return *this;
}
Result<std::unique_ptr<JournalFile>>
NativeJournalDirectory::open_file(std::string_view name, int flags,
                                  RawStorageActionKind kind,
                                  std::span<const ByteView> arguments) {
  const auto owner = storage_owner(raw_.get());
  return capture<Result<std::unique_ptr<JournalFile>>>(
      owner, kind, raw_.get(), arguments, [&](RawStorageAction &action) {
        action.operand(1, detail::raw_string_field(name));
        if (!valid_component(name))
          return Result<std::unique_ptr<JournalFile>>::failure(
              {ErrorCode::invalid_range});
        auto result = std::unique_ptr<NativeJournalFile>{
            new NativeJournalFile{-1, RawStorageResource::prepare(owner)}};
        const std::string component{name};
        result->descriptor_ = RawStorageNative::open(
            action, result->raw_.get(), descriptor_, cstring(component),
            flags | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, mode_t{0600});
        if (result->descriptor_ < 0) {
          const int detail = errno;
          return Result<std::unique_ptr<JournalFile>>::failure(
              detail == EEXIST ? Error{ErrorCode::conflict, detail}
                               : native_error(detail));
        }
        const auto checked = restricted_descriptor(action, result->descriptor_, false);
        if (!checked.has_value())
          return Result<std::unique_ptr<JournalFile>>::failure(checked.error());
        return Result<std::unique_ptr<JournalFile>>::success(std::move(result));
      });
}
Result<std::unique_ptr<JournalFile>>
NativeJournalDirectory::create_exclusive(std::string_view name) {
  return open_file(name, O_RDWR | O_CREAT | O_EXCL,
                   RawStorageActionKind::create_exclusive);
}
Result<std::unique_ptr<JournalFile>>
NativeJournalDirectory::open_existing(std::string_view name, FileAccess access) {
  const std::array fields{detail::raw_native_field(access)};
  return open_file(name, access == FileAccess::read_only ? O_RDONLY : O_RDWR,
                   RawStorageActionKind::open_existing, fields);
}
Result<void> NativeJournalDirectory::synchronize_directory(SyncStrength strength) {
  const std::array fields{detail::raw_native_field(strength)};
  return capture<Result<void>>(
      storage_owner(raw_.get()), RawStorageActionKind::synchronize_directory,
      raw_.get(), fields, [&](RawStorageAction &action) {
        return blackbird::synchronize(action, descriptor_, strength);
      });
}
Result<void> NativeJournalDirectory::replace_file(std::string_view from,
                                                  std::string_view to) {
  return capture<Result<void>>(
      storage_owner(raw_.get()), RawStorageActionKind::replace_file, raw_.get(), {},
      [&](RawStorageAction &action) {
        action.operand(1, detail::raw_string_field(from));
        action.operand(2, detail::raw_string_field(to));
        if (!valid_component(from) || !valid_component(to) || from == to) {
          return Result<void>::failure({ErrorCode::invalid_range});
        }
        try {
          const std::string source{from}, destination{to};
          struct stat original{}, target{};
          if (RawStorageNative::stat_at(action, descriptor_, cstring(source), original,
                                        AT_SYMLINK_NOFOLLOW) == -1) {
            return Result<void>::failure(native_error(errno));
          }
          const auto restricted = [&](const struct stat &metadata) {
            return S_ISREG(metadata.st_mode) &&
                   metadata.st_uid == RawStorageNative::uid(action) &&
                   (metadata.st_mode & 0077) == 0;
          };
          if (!restricted(original)) {
            return Result<void>::failure({ErrorCode::io, EPERM});
          }
          if (RawStorageNative::stat_at(action, descriptor_, cstring(destination),
                                        target, AT_SYMLINK_NOFOLLOW) == 0) {
            if (!restricted(target)) {
              return Result<void>::failure({ErrorCode::io, EPERM});
            }
            if (target.st_dev == original.st_dev && target.st_ino == original.st_ino) {
              return Result<void>::failure({ErrorCode::conflict});
            }
          } else if (errno != ENOENT) {
            return Result<void>::failure(native_error(errno));
          }
          if (RawStorageNative::rename(action, descriptor_, cstring(source),
                                       descriptor_, cstring(destination)) == -1) {
            return Result<void>::failure(native_error(errno));
          }
          return Result<void>::success();
        } catch (const std::bad_alloc &) {
          return Result<void>::failure({ErrorCode::allocation});
        }
      });
}

Result<ScratchCleanup> NativeJournalDirectory::reclaim_publication_scratch(
    std::string_view journal, std::size_t max_scan, std::size_t max_remove) {
  auto result = reclaim_scratch_pass(journal, max_scan, max_remove);
  if (!result.has_value()) {
    if (!cleanup_status_.error || cleanup_status_.error->code != result.error().code ||
        cleanup_status_.error->detail != result.error().detail)
      std::fprintf(stderr, "blackbird: publication scratch cleanup: %.*s: %s (%llu)\n",
                   static_cast<int>(std::min<std::size_t>(journal.size(), 200)),
                   journal.data(), error_name(result.error().code),
                   static_cast<unsigned long long>(result.error().detail));
    cleanup_status_.error = result.error();
  }
  return result;
}
Result<ScratchCleanup> NativeJournalDirectory::reclaim_scratch_pass(
    std::string_view journal, std::size_t max_scan, std::size_t max_remove) {
  using Answer = Result<ScratchCleanup>;
  const std::array fields{detail::raw_native_field(max_scan),
                          detail::raw_native_field(max_remove)};
  return capture<Answer>(
      storage_owner(raw_.get()), RawStorageActionKind::reclaim_scratch, raw_.get(),
      fields, [&](RawStorageAction &action) {
        action.operand(1, detail::raw_string_field(journal));

        if (!valid_component(journal) || max_scan == 0 || max_scan > 128 ||
            max_remove == 0 || max_remove > 16)
          return Answer::failure({ErrorCode::invalid_range});
        try {
          const std::string base{journal};
          if (cleanup_journal_ != base) {
            if (cleanup_stream_ && !cleanup_raw_)
              ::closedir(static_cast<DIR *>(cleanup_stream_));
            cleanup_raw_.reset();
            cleanup_stream_ = nullptr;
            cleanup_journal_ = base;
          }
          const auto matches = [&](std::string_view name) {
            if (!name.starts_with(base))
              return false;
            auto suffix = name.substr(base.size());
            unsigned fields = 0;
            if (suffix.starts_with(".scan.tmp.")) {
              suffix.remove_prefix(10);
              fields = 3;
            } else {
              for (const auto prefix : {".state.0.tmp.", ".state.1.tmp.",
                                        ".archive.0.tmp.", ".archive.1.tmp."}) {
                if (suffix.starts_with(prefix)) {
                  suffix.remove_prefix(std::string_view{prefix}.size());
                  fields = 1;
                  break;
                }
              }
            }
            if (fields == 0)
              return false;
            for (unsigned i = 0; i < fields; ++i) {
              const auto dot = suffix.find('.');
              const auto number = suffix.substr(0, dot);
              if (number.empty() || number.size() > 20 ||
                  number.find_first_not_of("0123456789") != std::string_view::npos)
                return false;
              if (i + 1 == fields)
                return dot == std::string_view::npos;
              if (dot == std::string_view::npos)
                return false;
              suffix.remove_prefix(dot + 1);
            }
            return false;
          };
          // Keep the enumeration between calls; each call still inspects bounded work.
          // EOF closes it, and the next pass starts a new cycle for newer debris.
          if (!cleanup_stream_) {
            cleanup_raw_ = RawStorageResource::prepare(storage_owner(raw_.get()), true);
            action.use(raw_.get());
            const int fd =
                RawStorageNative::open(action, cleanup_raw_.get(), descriptor_,
                                       {".", 2}, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (fd == -1)
              return Answer::failure(native_error(errno));
            DIR *raw = RawStorageNative::directory(action, cleanup_raw_.get(), fd);
            if (!raw) {
              const auto error = native_error(errno);
              if (cleanup_raw_)
                cleanup_raw_.reset();
              else
                ::close(fd);
              return Answer::failure(error);
            }
            cleanup_stream_ = raw;
          }
          auto *stream = static_cast<DIR *>(cleanup_stream_);
          ScratchCleanup result;
          while (result.scanned < max_scan && result.removed < max_remove) {
            errno = 0;
            action.use(cleanup_raw_.get());
            const auto *entry = RawStorageNative::next(action, stream);
            if (!entry) {
              if (errno)
                return Answer::failure(native_error(errno));
              result.complete = true;
              if (cleanup_raw_)
                cleanup_raw_.reset();
              else
                ::closedir(stream);
              cleanup_stream_ = nullptr;
              break;
            }
            ++result.scanned;
            if (!matches(entry->d_name))
              continue;
            action.use(raw_.get());
            struct stat metadata{};
            if (RawStorageNative::stat_at(
                    action, descriptor_,
                    {entry->d_name, std::char_traits<char>::length(entry->d_name) + 1},
                    metadata, AT_SYMLINK_NOFOLLOW) == -1) {
              if (errno == ENOENT)
                continue;
              return Answer::failure(native_error(errno));
            }
            if (!S_ISREG(metadata.st_mode) ||
                metadata.st_uid != RawStorageNative::uid(action) ||
                (metadata.st_mode & 0077) != 0 || metadata.st_nlink != 1)
              continue;
            auto candidate_raw = RawStorageResource::prepare(storage_owner(raw_.get()));
            action.use(raw_.get());
            const int candidate = RawStorageNative::open(
                action, candidate_raw.get(), descriptor_,
                {entry->d_name, std::char_traits<char>::length(entry->d_name) + 1},
                O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            if (candidate == -1) {
              if (errno == ENOENT || errno == ELOOP)
                continue;
              return Answer::failure(native_error(errno));
            }
            struct Close {
              int fd;
              RawStorageResource *raw;
              ~Close() {
                if (!raw)
                  ::close(fd);
              }
            } close_candidate{candidate, candidate_raw.get()};
            if (RawStorageNative::lock(action, candidate, LOCK_EX | LOCK_NB) == -1) {
              if (errno == EWOULDBLOCK || errno == EAGAIN)
                continue;
              return Answer::failure(native_error(errno));
            }
            struct stat opened{}, current{};
            if (RawStorageNative::stat(action, candidate, opened) == -1)
              return Answer::failure(native_error(errno));
            action.use(raw_.get());
            if (RawStorageNative::stat_at(
                    action, descriptor_,
                    {entry->d_name, std::char_traits<char>::length(entry->d_name) + 1},
                    current, AT_SYMLINK_NOFOLLOW) == -1) {
              if (errno == ENOENT)
                continue;
              return Answer::failure(native_error(errno));
            }
            if (opened.st_dev != metadata.st_dev || opened.st_ino != metadata.st_ino ||
                current.st_dev != opened.st_dev || current.st_ino != opened.st_ino)
              continue;
            // A real journal is not debris, even if it occupies a reserved-looking
            // name. Preserve its signature without requiring a valid remainder/CRC.
            action.use(candidate_raw.get());
            char signature[8]{};
            const auto read = RawStorageNative::read(
                action, candidate, std::as_writable_bytes(std::span{signature}),
                off_t{0});
            if (read == -1)
              return Answer::failure(native_error(errno));
            if (read == 8 &&
                std::string_view{signature, sizeof(signature)} == "ARCOJ001")
              continue;
            action.use(raw_.get());
            if (RawStorageNative::unlink(
                    action, descriptor_,
                    {entry->d_name, std::char_traits<char>::length(entry->d_name) + 1},
                    0) == -1) {
              if (errno == ENOENT)
                continue;
              return Answer::failure(native_error(errno));
            }
            ++result.removed;
          }
          // Scratch deletion need not survive a crash; no directory fsync on the
          // foreground path. Authoritative/selected files are never candidates.
          cleanup_status_ = result;
          return Answer::success(result);
        } catch (const std::bad_alloc &) {
          return Answer::failure({ErrorCode::allocation});
        }
      });
}

} // namespace blackbird
