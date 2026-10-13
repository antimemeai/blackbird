#pragma once
#include "blackbird/coding.hpp"
#include "blackbird/raw_reader.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

// Independent literal records follow BBM2 and the native raw layout;
// none of these expected-byte constructors invokes the production encoder.
namespace operation_oracle {
using namespace blackbird;
inline void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error{message};
}
inline void literal(std::vector<std::byte> &out, std::string_view bytes) {
  const auto view = std::as_bytes(std::span{bytes.data(), bytes.size()});
  out.insert(out.end(), view.begin(), view.end());
}
template <unsigned Width>
inline void little(std::vector<std::byte> &out, std::uint64_t value) {
  for (unsigned index = 0; index < Width; ++index) {
    out.push_back(static_cast<std::byte>(value & 255));
    value >>= 8;
  }
}
inline void variable(std::vector<std::byte> &out, std::size_t length) {
  do {
    const auto next = length >> 7;
    out.push_back(static_cast<std::byte>((length & 127) | (next ? 128 : 0)));
    length = next;
  } while (length);
}
inline void text(std::vector<std::byte> &out, std::string_view value) {
  out.push_back(std::byte{6});
  variable(out, value.size());
  literal(out, value);
}
struct WriteInput {
  std::string_view path, content;
};
inline std::vector<std::byte> write_input(WriteInput input) {
  std::vector<std::byte> out{std::byte{'B'}, std::byte{'B'}, std::byte{'M'},
                             std::byte{2},   std::byte{10},  std::byte{2}};
  // The actual Lua business reader sorts input keys lexicographically.
  text(out, "content");
  text(out, input.content);
  out.push_back(std::byte{8});
  out.push_back(std::byte{29});
  text(out, input.path);
  return out;
}
inline std::vector<std::byte> write_result(std::string_view path) {
  std::vector<std::byte> out{std::byte{'B'}, std::byte{'B'}, std::byte{'M'},
                             std::byte{2},   std::byte{10},  std::byte{2}};
  text(out, "written");
  out.push_back(std::byte{2}); // actual true business value
  out.push_back(std::byte{8});
  out.push_back(std::byte{29});
  text(out, path);
  return out;
}
inline std::vector<std::byte> capacity_fallback() {
  // BBM2 object{error:"capacity",detail:0}; neither name is a word-table entry.
  return {
      std::byte{'B'}, std::byte{'B'}, std::byte{'M'}, std::byte{2},   std::byte{10},
      std::byte{2},   std::byte{6},   std::byte{5},   std::byte{'e'}, std::byte{'r'},
      std::byte{'r'}, std::byte{'o'}, std::byte{'r'}, std::byte{6},   std::byte{8},
      std::byte{'c'}, std::byte{'a'}, std::byte{'p'}, std::byte{'a'}, std::byte{'c'},
      std::byte{'i'}, std::byte{'t'}, std::byte{'y'}, std::byte{6},   std::byte{6},
      std::byte{'d'}, std::byte{'e'}, std::byte{'t'}, std::byte{'a'}, std::byte{'i'},
      std::byte{'l'}, std::byte{3},   std::byte{0}};
}
inline std::vector<std::byte> reservation(std::uint64_t counter) {
  std::vector<std::byte> out;
  little<2>(out, 1); // version
  little<2>(out, 1); // actual reservation kind
  little<4>(out, 0); // no source dependencies
  little<8>(out, counter);
  return out;
}
inline std::vector<std::byte> rejected_proposal(JournalCursor stale, ByteView source,
                                                std::uint64_t counter) {
  std::vector<std::byte> out;
  literal(out, "ARPROP01");
  out.insert(out.end(), stale.journal.bytes().begin(), stale.journal.bytes().end());
  little<8>(out, stale.sequence);
  little<8>(out, stale.end_offset);
  little<4>(out, 1);
  little<4>(out, source.size());
  out.insert(out.end(), source.begin(), source.end());
  little<4>(out, 1);
  little<4>(out, 16);
  const auto event = reservation(counter);
  out.insert(out.end(), event.begin(), event.end());
  return out;
}
template <class T> inline void native(std::vector<std::byte> &out, const T &field) {
  const auto bytes = std::as_bytes(std::span{&field, std::size_t{1}});
  out.insert(out.end(), bytes.begin(), bytes.end());
}
inline void native(std::vector<std::byte> &out, RawEventRef reference) {
  out.insert(out.end(), reference.lane.begin(), reference.lane.end());
  native(out, reference.ordinal);
}
inline std::vector<std::byte> original(RawEventRef request, std::uint8_t role,
                                       ByteView bytes) {
  std::vector<std::byte> result;
  native(result, request);
  native(result, role);
  native(result, bytes.size());
  result.insert(result.end(), bytes.begin(), bytes.end());
  return result;
}
struct Record {
  RawEventRef reference;
  RawPartition partition;
  std::uint32_t site;
  std::vector<std::byte> bytes;
};
inline bool same(RawEventRef left, RawEventRef right) {
  return left.lane == right.lane && left.ordinal == right.ordinal;
}
template <class T> inline T read(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "short actual native field");
  T result{};
  std::memcpy(&result, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return result;
}
inline RawEventRef reference(ByteView &bytes) {
  require(bytes.size() >= 24, "short actual Ref");
  IdentityBytes lane{};
  std::copy_n(bytes.begin(), 16, lane.begin());
  bytes = bytes.subspan(16);
  return {lane, read<std::uint64_t>(bytes)};
}
} // namespace operation_oracle
