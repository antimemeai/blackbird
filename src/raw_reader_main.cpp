#include "blackbird/raw_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace {
using namespace blackbird;
int failed(Error error) {
  std::fprintf(stderr, "raw reader: %s (%lld)\n", error_name(error.code),
               static_cast<long long>(error.detail));
  return 1;
}
bool write_all(ByteView body) {
  while (!body.empty()) {
    const auto count = ::write(STDOUT_FILENO, body.data(), body.size());
    if (count < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (count == 0)
      return false;
    body = body.subspan(static_cast<std::size_t>(count));
  }
  return true;
}
void put(MutableByteView output, std::uint64_t value) {
  for (auto &byte : output) {
    byte = std::byte{static_cast<unsigned char>(value & 255U)};
    value >>= 8;
  }
}
Result<RawRunCursor> read_cursor(const char *path) {
  const int descriptor = ::open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (descriptor < 0)
    return Result<RawRunCursor>::failure({ErrorCode::io, errno});
  std::array<std::byte, raw_run_cursor_size + 1> body{};
  std::size_t offset = 0;
  std::optional<Error> error;
  while (offset < body.size()) {
    const auto count = ::read(descriptor, body.data() + offset, body.size() - offset);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      error = Error{ErrorCode::io, errno};
      break;
    }
    if (count == 0)
      break;
    offset += static_cast<std::size_t>(count);
  }
  (void)::close(descriptor);
  if (error.has_value())
    return Result<RawRunCursor>::failure(*error);
  return decode_raw_run_cursor(ByteView{body}.first(offset));
}
int records(const char *path, std::optional<std::string_view> resume_path) {
  std::optional<RawRunCursor> cursor;
  if (resume_path.has_value()) {
    const auto decoded = read_cursor(resume_path->data());
    if (!decoded.has_value())
      return failed(decoded.error());
    cursor = decoded.value();
  }
  auto answer = RawRunReader::open(path, cursor);
  if (!answer.has_value())
    return failed(answer.error());
  auto reader = std::move(answer).value();
  // Stream v1: magic[8], version u32, description bytes u32, exact descriptions;
  // record tag1, next cursor[64], origin[32], raw size u32, raw bytes;
  // EOF tag0 and final cursor[64]. Integers little-endian, no padding.
  std::array<std::byte, 16> header{};
  std::memcpy(header.data(), "BBRAWOUT", 8);
  put(MutableByteView{header}.subspan(8, 4), 1);
  put(MutableByteView{header}.subspan(12, 4), reader->snapshot().descriptions.size());
  if (!write_all(header) || !write_all(reader->snapshot().descriptions))
    return failed({ErrorCode::io, errno});
  std::size_t largest = 0;
  for (const auto &segment : reader->snapshot().segments)
    largest =
        std::max(largest, static_cast<std::size_t>(segment.header.limits.max_payload));
  std::vector<std::byte> output(largest);
  for (;;) {
    const auto read = reader->read(output);
    if (!read.has_value())
      return failed(read.error());
    const auto &record = read.value();
    if (!record.has_value()) {
      const std::array tag{std::byte{0}};
      const auto checkpoint = encode_raw_run_cursor(reader->cursor());
      if (!write_all(tag) || !write_all(checkpoint))
        return failed({ErrorCode::io, errno});
      return 0;
    }
    std::array<std::byte, 1 + raw_run_cursor_size + raw_origin_size + 4> prefix{};
    prefix[0] = std::byte{1};
    const auto checkpoint = encode_raw_run_cursor(record->next);
    std::copy(checkpoint.begin(), checkpoint.end(), prefix.begin() + 1);
    auto origin =
        MutableByteView{prefix}.subspan(1 + raw_run_cursor_size, raw_origin_size);
    std::copy(record->origin.lane.begin(), record->origin.lane.end(), origin.begin());
    put(origin.subspan(16, 4), static_cast<std::uint32_t>(record->origin.partition));
    put(origin.subspan(24, 8), record->origin.source_start);
    put(MutableByteView{prefix}.last(4), record->raw.size());
    if (!write_all(prefix) || !write_all(record->raw))
      return failed({ErrorCode::io, errno});
  }
}
} // namespace
int main(int argc, char **argv) {
  using namespace blackbird;
  if (argc < 3 || argc > 4) {
    std::fprintf(stderr, "usage: blackbird-raw-reader list SESSION | descriptions RUN "
                         "| records RUN [CURSOR_FILE]\n");
    return 2;
  }
  try {
    const std::string_view operation{argv[1]};
    if (operation == "records")
      return records(argv[2], argc == 4 ? std::optional<std::string_view>{argv[3]}
                                        : std::nullopt);
    if (argc != 3)
      return 2;
    if (operation == "list") {
      const auto entries = discover_raw_runs(argv[2]);
      if (!entries.has_value())
        return failed(entries.error());
      for (const auto &entry : entries.value()) {
        if (entry.error.has_value())
          std::printf("%s error %s %lld\n", entry.name.c_str(),
                      error_name(entry.error->code),
                      static_cast<long long>(entry.error->detail));
        else if (entry.snapshot.has_value())
          std::printf("%s %s %zu\n", entry.name.c_str(),
                      entry.snapshot->closed ? "closed" : "open",
                      entry.snapshot->segments.size());
      }
      return 0;
    }
    if (operation == "descriptions") {
      const auto reader = RawRunReader::open(argv[2]);
      if (!reader.has_value())
        return failed(reader.error());
      return write_all(reader.value()->snapshot().descriptions)
                 ? 0
                 : failed({ErrorCode::io, errno});
    }
    return 2;
  } catch (const std::bad_alloc &) {
    return failed({ErrorCode::allocation});
  }
}
