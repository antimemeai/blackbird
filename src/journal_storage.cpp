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
Result<void> synchronize(int descriptor, SyncStrength strength) {
  int status = 0;
#if defined(__APPLE__)
  if (strength == SyncStrength::full) {
    status = ::fcntl(descriptor, F_FULLFSYNC);
  } else {
    status = ::fsync(descriptor);
  }
#else
  (void)strength;
  status = ::fsync(descriptor);
#endif
  if (status == -1) {
    return Result<void>::failure(native_error(errno));
  }
  return Result<void>::success();
}
Result<void> restricted_descriptor(int descriptor, bool directory) {
  struct stat metadata{};
  if (::fstat(descriptor, &metadata) == -1) {
    return Result<void>::failure(native_error(errno));
  }
  if ((directory ? !S_ISDIR(metadata.st_mode) : !S_ISREG(metadata.st_mode)) ||
      metadata.st_uid != ::geteuid() || (metadata.st_mode & 0077) != 0) {
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
  // Never retry close after EINTR: it could target a reused descriptor.
  if (descriptor_ != -1) {
    (void)::close(descriptor_);
  }
}
NativeJournalFile::NativeJournalFile(NativeJournalFile &&other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)) {}
NativeJournalFile &NativeJournalFile::operator=(NativeJournalFile &&other) noexcept {
  if (this != &other) {
    if (descriptor_ != -1) {
      (void)::close(descriptor_);
    }
    descriptor_ = std::exchange(other.descriptor_, -1);
  }
  return *this;
}
Result<std::size_t> NativeJournalFile::read_at(std::uint64_t offset,
                                               MutableByteView output) {
  if (!valid_io(offset, output.size())) {
    return Result<std::size_t>::failure({ErrorCode::invalid_range});
  }
  const auto count =
      ::pread(descriptor_, output.data(), output.size(), static_cast<off_t>(offset));
  if (count == -1) {
    return Result<std::size_t>::failure(native_error(errno));
  }
  return Result<std::size_t>::success(static_cast<std::size_t>(count));
}
Result<std::size_t> NativeJournalFile::write_at(std::uint64_t offset, ByteView input) {
  if (!valid_io(offset, input.size())) {
    return Result<std::size_t>::failure({ErrorCode::invalid_range});
  }
  const auto count =
      ::pwrite(descriptor_, input.data(), input.size(), static_cast<off_t>(offset));
  if (count == -1) {
    return Result<std::size_t>::failure(native_error(errno));
  }
  return Result<std::size_t>::success(static_cast<std::size_t>(count));
}
Result<std::uint64_t> NativeJournalFile::extent() {
  struct stat metadata{};
  if (::fstat(descriptor_, &metadata) == -1) {
    return Result<std::uint64_t>::failure(native_error(errno));
  }
  if (metadata.st_size < 0) {
    return Result<std::uint64_t>::failure({ErrorCode::invalid_range});
  }
  return Result<std::uint64_t>::success(static_cast<std::uint64_t>(metadata.st_size));
}
Result<void> NativeJournalFile::synchronize(SyncStrength strength) {
  return blackbird::synchronize(descriptor_, strength);
}
Result<void> NativeJournalFile::lock_writer() {
  if (::flock(descriptor_, LOCK_EX | LOCK_NB) == -1) {
    const auto detail = errno;
    if (detail == EWOULDBLOCK) {
      return Result<void>::failure({ErrorCode::busy, detail});
    }
    return Result<void>::failure(native_error(detail));
  }
  return Result<void>::success();
}

void NativeJournalFile::release_writer() noexcept {
  // Keep the selected descriptor alive for readers, but not the writer lease.
  while (::flock(descriptor_, LOCK_UN) == -1 && errno == EINTR) {
  }
}

Result<NativeJournalDirectory> NativeJournalDirectory::open(std::string_view path) {
  if (path.empty() || path.find('\0') != std::string_view::npos) {
    return Result<NativeJournalDirectory>::failure({ErrorCode::invalid_range});
  }
  try {
    const std::string name{path};
    const auto descriptor =
        ::open(name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (descriptor == -1) {
      return Result<NativeJournalDirectory>::failure(native_error(errno));
    }
    const auto checked = restricted_descriptor(descriptor, true);
    if (!checked.has_value()) {
      (void)::close(descriptor);
      return Result<NativeJournalDirectory>::failure(checked.error());
    }
    return Result<NativeJournalDirectory>::success(NativeJournalDirectory{descriptor});
  } catch (const std::bad_alloc &) {
    return Result<NativeJournalDirectory>::failure({ErrorCode::allocation});
  }
}
NativeJournalDirectory::~NativeJournalDirectory() {
  if (cleanup_stream_)
    ::closedir(static_cast<DIR *>(cleanup_stream_));
  if (descriptor_ != -1) {
    (void)::close(descriptor_);
  }
}
Result<NativeJournalDirectory> NativeJournalDirectory::open_at(int directory,
                                                               std::string_view name) {
  if (!valid_component(name))
    return Result<NativeJournalDirectory>::failure({ErrorCode::invalid_range});
  try {
    const std::string component{name};
    const int descriptor = ::openat(directory, component.c_str(),
                                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (descriptor < 0)
      return Result<NativeJournalDirectory>::failure(native_error(errno));
    const auto checked = restricted_descriptor(descriptor, true);
    if (!checked.has_value()) {
      (void)::close(descriptor);
      return Result<NativeJournalDirectory>::failure(checked.error());
    }
    return Result<NativeJournalDirectory>::success(NativeJournalDirectory{descriptor});
  } catch (const std::bad_alloc &) {
    return Result<NativeJournalDirectory>::failure({ErrorCode::allocation});
  }
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
      diagnostics_(std::move(other.diagnostics_)) {}
NativeJournalDirectory &
NativeJournalDirectory::operator=(NativeJournalDirectory &&other) noexcept {
  if (this != &other) {
    if (cleanup_stream_)
      ::closedir(static_cast<DIR *>(cleanup_stream_));
    cleanup_stream_ = std::exchange(other.cleanup_stream_, nullptr);
    cleanup_journal_ = std::move(other.cleanup_journal_);
    cleanup_status_ = other.cleanup_status_;
    diagnostics_ = std::move(other.diagnostics_);
    if (descriptor_ != -1) {
      (void)::close(descriptor_);
    }
    descriptor_ = std::exchange(other.descriptor_, -1);
  }
  return *this;
}
Result<std::unique_ptr<JournalFile>>
NativeJournalDirectory::open_file(std::string_view name, int flags) {
  if (!valid_component(name)) {
    return Result<std::unique_ptr<JournalFile>>::failure({ErrorCode::invalid_range});
  }
  int descriptor = -1;
  try {
    const std::string component{name};
    // Reject FIFOs without waiting for their peer before file-type validation.
    descriptor = ::openat(descriptor_, component.c_str(),
                          flags | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (descriptor == -1) {
      const auto detail = errno;
      return Result<std::unique_ptr<JournalFile>>::failure(
          detail == EEXIST ? Error{ErrorCode::conflict, detail} : native_error(detail));
    }
    const auto checked = restricted_descriptor(descriptor, false);
    if (!checked.has_value()) {
      (void)::close(descriptor);
      return Result<std::unique_ptr<JournalFile>>::failure(checked.error());
    }
    return Result<std::unique_ptr<JournalFile>>::success(
        std::unique_ptr<JournalFile>{new NativeJournalFile{descriptor}});
  } catch (const std::bad_alloc &) {
    if (descriptor != -1) {
      (void)::close(descriptor);
    }
    return Result<std::unique_ptr<JournalFile>>::failure({ErrorCode::allocation});
  }
}
Result<std::unique_ptr<JournalFile>>
NativeJournalDirectory::create_exclusive(std::string_view name) {
  return open_file(name, O_RDWR | O_CREAT | O_EXCL);
}
Result<std::unique_ptr<JournalFile>>
NativeJournalDirectory::open_existing(std::string_view name, FileAccess access) {
  return open_file(name, access == FileAccess::read_only ? O_RDONLY : O_RDWR);
}
Result<void> NativeJournalDirectory::synchronize_directory(SyncStrength strength) {
  return blackbird::synchronize(descriptor_, strength);
}
Result<void> NativeJournalDirectory::replace_file(std::string_view from,
                                                  std::string_view to) {
  if (!valid_component(from) || !valid_component(to) || from == to) {
    return Result<void>::failure({ErrorCode::invalid_range});
  }
  try {
    const std::string source{from}, destination{to};
    struct stat original{}, target{};
    if (::fstatat(descriptor_, source.c_str(), &original, AT_SYMLINK_NOFOLLOW) == -1) {
      return Result<void>::failure(native_error(errno));
    }
    const auto restricted = [](const struct stat &metadata) {
      return S_ISREG(metadata.st_mode) && metadata.st_uid == ::geteuid() &&
             (metadata.st_mode & 0077) == 0;
    };
    if (!restricted(original)) {
      return Result<void>::failure({ErrorCode::io, EPERM});
    }
    if (::fstatat(descriptor_, destination.c_str(), &target, AT_SYMLINK_NOFOLLOW) ==
        0) {
      if (!restricted(target)) {
        return Result<void>::failure({ErrorCode::io, EPERM});
      }
      if (target.st_dev == original.st_dev && target.st_ino == original.st_ino) {
        return Result<void>::failure({ErrorCode::conflict});
      }
    } else if (errno != ENOENT) {
      return Result<void>::failure(native_error(errno));
    }
    if (::renameat(descriptor_, source.c_str(), descriptor_, destination.c_str()) ==
        -1) {
      return Result<void>::failure(native_error(errno));
    }
    return Result<void>::success();
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
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
  if (!valid_component(journal) || max_scan == 0 || max_scan > 128 || max_remove == 0 ||
      max_remove > 16)
    return Answer::failure({ErrorCode::invalid_range});
  try {
    const std::string base{journal};
    if (cleanup_journal_ != base) {
      if (cleanup_stream_)
        ::closedir(static_cast<DIR *>(cleanup_stream_));
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
        for (const auto prefix :
             {".state.0.tmp.", ".state.1.tmp.", ".archive.0.tmp.", ".archive.1.tmp."}) {
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
      const int fd = ::openat(descriptor_, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
      if (fd == -1)
        return Answer::failure(native_error(errno));
      DIR *raw = ::fdopendir(fd);
      if (!raw) {
        const auto error = native_error(errno);
        ::close(fd);
        return Answer::failure(error);
      }
      cleanup_stream_ = raw;
    }
    auto *stream = static_cast<DIR *>(cleanup_stream_);
    ScratchCleanup result;
    while (result.scanned < max_scan && result.removed < max_remove) {
      errno = 0;
      const auto *entry = ::readdir(stream);
      if (!entry) {
        if (errno)
          return Answer::failure(native_error(errno));
        result.complete = true;
        ::closedir(stream);
        cleanup_stream_ = nullptr;
        break;
      }
      ++result.scanned;
      if (!matches(entry->d_name))
        continue;
      struct stat metadata{};
      if (::fstatat(descriptor_, entry->d_name, &metadata, AT_SYMLINK_NOFOLLOW) == -1) {
        if (errno == ENOENT)
          continue;
        return Answer::failure(native_error(errno));
      }
      if (!S_ISREG(metadata.st_mode) || metadata.st_uid != ::geteuid() ||
          (metadata.st_mode & 0077) != 0 || metadata.st_nlink != 1)
        continue;
      const int candidate =
          ::openat(descriptor_, entry->d_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
      if (candidate == -1) {
        if (errno == ENOENT || errno == ELOOP)
          continue;
        return Answer::failure(native_error(errno));
      }
      struct Close {
        int fd;
        ~Close() { ::close(fd); }
      } close_candidate{candidate};
      if (::flock(candidate, LOCK_EX | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
          continue;
        return Answer::failure(native_error(errno));
      }
      struct stat opened{}, current{};
      if (::fstat(candidate, &opened) == -1)
        return Answer::failure(native_error(errno));
      if (::fstatat(descriptor_, entry->d_name, &current, AT_SYMLINK_NOFOLLOW) == -1) {
        if (errno == ENOENT)
          continue;
        return Answer::failure(native_error(errno));
      }
      if (opened.st_dev != metadata.st_dev || opened.st_ino != metadata.st_ino ||
          current.st_dev != opened.st_dev || current.st_ino != opened.st_ino)
        continue;
      // A real journal is not debris, even if it occupies a reserved-looking
      // name. Preserve its signature without requiring a valid remainder/CRC.
      char signature[8]{};
      const auto read = ::pread(candidate, signature, sizeof(signature), 0);
      if (read == -1)
        return Answer::failure(native_error(errno));
      if (read == 8 && std::string_view{signature, sizeof(signature)} == "ARCOJ001")
        continue;
      if (::unlinkat(descriptor_, entry->d_name, 0) == -1) {
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
}

} // namespace blackbird
