#pragma once
#include <cstddef>
// Test-only shared-library callback ABI; these values are not raw event fields.
struct FileStdioRequest {
  int fd;
  std::size_t size, count;
};
struct FileStdioReturn {
  std::size_t returned;
  int error, failed;
};
