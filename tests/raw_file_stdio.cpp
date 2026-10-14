// Native fixture only: reach the actual libc++ shared-library stdio acquisition.
#include "raw_file_stdio.hpp"
#include <cerrno>
#include <cstdio>
#include <dlfcn.h>
#if !defined(__APPLE__)
#error "Darwin stdio fixture uses dyld interposition"
#endif
namespace {
std::size_t acquired_fread(void *bytes, std::size_t size, std::size_t count,
                           FILE *file) {
  using Before = std::size_t (*)(const FileStdioRequest &);
  using After = void (*)(const FileStdioReturn &);
  static const auto before =
      reinterpret_cast<Before>(::dlsym(RTLD_DEFAULT, "bb_file_before_stdio"));
  static const auto after =
      reinterpret_cast<After>(::dlsym(RTLD_DEFAULT, "bb_file_after_stdio"));
  const FileStdioRequest request{::fileno(file), size, count};
  const auto requested = before ? before(request) : count;
  const auto result = ::fread(bytes, size, requested, file);
  const int error = errno;
  if (after)
    after(FileStdioReturn{result, error, ::ferror(file)});
  using Observe = void (*)(int, void *, std::size_t);
  static const auto observe =
      reinterpret_cast<Observe>(::dlsym(RTLD_DEFAULT, "bb_file_observed_stdio"));
  if (observe && result)
    observe(::fileno(file), bytes, result * size);
  errno = error;
  return result;
}
__attribute__((used, section("__DATA,__interpose"))) const struct {
  decltype(&::fread) replacement;
  decltype(&::fread) original;
} acquisition{acquired_fread, ::fread};
} // namespace
