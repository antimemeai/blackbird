// Native fixture only: reach the actual libc++ shared-library stdio acquisition.
#include "raw_file_stdio.hpp"
#include <cerrno>
#include <cstdio>
#include <dlfcn.h>
namespace {
std::size_t acquire(void *bytes, std::size_t size, std::size_t count, FILE *file) {
#if defined(__APPLE__)
  return ::fread(bytes, size, count, file);
#else
  using Acquire = std::size_t (*)(void *, std::size_t, std::size_t, FILE *);
  static const auto native = reinterpret_cast<Acquire>(::dlsym(RTLD_NEXT, "fread"));
  return native(bytes, size, count, file);
#endif
}
std::size_t acquired_fread(void *bytes, std::size_t size, std::size_t count,
                           FILE *file) {
  using Before = std::size_t (*)(const FileStdioRequest &);
  using After = void (*)(const FileStdioReturn &);
  using Observe = void (*)(int, void *, std::size_t);
  static const auto before =
      reinterpret_cast<Before>(::dlsym(RTLD_DEFAULT, "bb_file_before_stdio"));
  static const auto after =
      reinterpret_cast<After>(::dlsym(RTLD_DEFAULT, "bb_file_after_stdio"));
  static const auto observe =
      reinterpret_cast<Observe>(::dlsym(RTLD_DEFAULT, "bb_file_observed_stdio"));
  std::size_t total = 0;
  do {
    const FileStdioRequest request{::fileno(file), size, count - total};
    const auto requested = before ? before(request) : request.count;
    auto *next = total ? static_cast<unsigned char *>(bytes) + total * size
                       : static_cast<unsigned char *>(bytes);
    const auto result = acquire(next, size, requested, file);
    const int error = errno;
    if (after)
      after(FileStdioReturn{result, error, ::ferror(file)});
    if (observe && result)
      observe(::fileno(file), next, result * size);
    errno = error;
    total += result;
    // A deliberately shortened first acquisition is followed by actual libc
    // failure inside this same stream read. Bulk-reading libc++ must observe
    // the positive prefix and genuine FILE error, rather than an invented EOF.
    if (!requested || result != requested || requested == request.count)
      break;
  } while (total < count);
  return total;
}
#if defined(__APPLE__)
__attribute__((used, section("__DATA,__interpose"))) const struct {
  decltype(&::fread) replacement;
  decltype(&::fread) original;
} acquisition{acquired_fread, ::fread};
#endif
} // namespace
#if !defined(__APPLE__)
extern "C" std::size_t fread(void *bytes, std::size_t size, std::size_t count,
                             FILE *file) {
  return acquired_fread(bytes, size, count, file);
}
#endif
