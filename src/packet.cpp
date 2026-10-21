#include "blackbird/packet.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <string_view>

namespace blackbird {
namespace {
// Wire IDs are array index + 1. Never reorder/remove entries in this version.
constexpr std::string_view words[] = {"label",
                                      "metadata",
                                      "time",
                                      "clock_id",
                                      "utc_ns",
                                      "monotonic_ns",
                                      "sampling_span_ns",
                                      "synchronization",
                                      "variable",
                                      "value",
                                      "source",
                                      "sample_id",
                                      "observation_status",
                                      "kind",
                                      "attempt",
                                      "invocation",
                                      "operation",
                                      "revision",
                                      "generation",
                                      "duration_ns",
                                      "input_binding",
                                      "retry_group",
                                      "ordinal",
                                      "field",
                                      "identity_scope",
                                      "content_observation",
                                      "repository",
                                      "requested_path",
                                      "path",
                                      "exit_code",
                                      "commit",
                                      "parents",
                                      "author_unix_seconds",
                                      "committer_unix_seconds",
                                      "host",
                                      "pid",
                                      "scope",
                                      "diagnostic_file",
                                      "expires_at",
                                      "bytes",
                                      "available",
                                      "storage_error",
                                      "complaint",
                                      "activation",
                                      "unknown",
                                      "observed",
                                      "unavailable",
                                      "variable.sample",
                                      "clock.domain",
                                      "doctrine.effective",
                                      "git.head",
                                      "git.commit",
                                      "native_request_field",
                                      "instructions",
                                      "git_cli",
                                      "operation.result",
                                      "provider.request",
                                      "provider.stream",
                                      "process.output",
                                      "invocation-v1",
                                      "attempt-invocation-v1",
                                      "turn-boundary",
                                      "result_id",
                                      "result_record"};
enum Tag : unsigned char {
  null_value = 0,
  false_value = 1,
  true_value = 2,
  unsigned_integer = 3,
  negative_integer = 4,
  decimal = 5,
  text = 6,
  identity = 7,
  word = 8,
  array = 9,
  object = 10,
  real = 11
};
std::uint64_t word_id(std::string_view s) {
  for (std::size_t i = 0; i < std::size(words); ++i)
    if (words[i] == s)
      return i + 1;
  return 0;
}
int hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}
class Encoder {
public:
  // count_only: the size the encoding would have, with nothing built.
  explicit Encoder(ValueLimits limits, bool count_only = false)
      : limits_(limits), count_only_(count_only) {}
  // Appends to target (from its current end) instead of an owned buffer.
  Encoder(ValueLimits limits, std::vector<std::byte> &target)
      : limits_(limits), out_(&target), base_(target.size()) {}
  void byte(unsigned char b) {
    if (size() == limits_.bytes)
      throw Error{ErrorCode::capacity};
    if (count_only_)
      ++counted_;
    else
      out_->push_back(static_cast<std::byte>(b));
  }
  std::size_t size() const noexcept {
    return count_only_ ? counted_ : out_->size() - base_;
  }
  void varint(std::uint64_t n) {
    do {
      const auto b = static_cast<unsigned char>(n & 127);
      n >>= 7;
      byte(static_cast<unsigned char>(b | (n ? 128 : 0)));
    } while (n);
  }
  void raw_text(std::string_view s) {
    varint(s.size());
    if (s.size() > limits_.bytes - size())
      throw Error{ErrorCode::capacity};
    if (count_only_) {
      counted_ += s.size();
      return;
    }
    const auto data = std::as_bytes(std::span{s.data(), s.size()});
    out_->insert(out_->end(), data.begin(), data.end());
  }
  void string(std::string_view s) {
    if (const auto id = word_id(s)) {
      byte(word);
      varint(id);
      return;
    }
    if (s.size() == 32 &&
        std::all_of(s.begin(), s.end(), [](char c) { return hex_digit(c) >= 0; })) {
      byte(identity);
      for (std::size_t i = 0; i < 32; i += 2)
        byte(static_cast<unsigned char>(16 * hex_digit(s[i]) + hex_digit(s[i + 1])));
      return;
    }
    byte(text);
    raw_text(s);
  }
  void value(const Value &v, std::size_t depth) {
    if (depth > limits_.depth || ++nodes_ > limits_.nodes)
      throw Error{ErrorCode::capacity};
    std::visit(
        [&](const auto &x) {
          using T = std::decay_t<decltype(x)>;
          if constexpr (std::is_same_v<T, std::nullptr_t>)
            byte(null_value);
          else if constexpr (std::is_same_v<T, bool>)
            byte(x ? true_value : false_value);
          else if constexpr (std::is_same_v<T, std::string>)
            string(x);
          else if constexpr (std::is_same_v<T, Number>) {
            std::visit(
                [&](const auto &n) {
                  using N = std::decay_t<decltype(n)>;
                  if constexpr (std::is_same_v<N, std::uint64_t>) {
                    byte(unsigned_integer);
                    varint(n);
                  } else if constexpr (std::is_same_v<N, std::int64_t>) {
                    byte(n < 0 ? negative_integer : unsigned_integer);
                    varint(n < 0 ? static_cast<std::uint64_t>(-(n + 1)) + 1
                                 : static_cast<std::uint64_t>(n));
                  } else if constexpr (std::is_same_v<N, double>) {
                    byte(real);
                    const auto bits = std::bit_cast<std::uint64_t>(n);
                    for (unsigned i = 0; i < 8; ++i)
                      byte(static_cast<unsigned char>(bits >> (i * 8)));
                  } else {
                    byte(decimal);
                    byte(n.negative ? 1 : 0);
                    varint(n.exponent < 0
                               ? (static_cast<std::uint64_t>(-(n.exponent + 1)) * 2 + 1)
                               : static_cast<std::uint64_t>(n.exponent) * 2);
                    varint(n.limbs.size());
                    for (const auto limb : n.limbs)
                      for (unsigned i = 0; i < 4; ++i)
                        byte(static_cast<unsigned char>(limb >> (i * 8)));
                  }
                },
                x.storage());
          } else if constexpr (std::is_same_v<T, Value::Array>) {
            byte(array);
            varint(x.size());
            for (const auto &child : x)
              value(child, depth + 1);
          } else {
            byte(object);
            varint(x.size());
            for (const auto &[key, child] : x) {
              string(key);
              value(child, depth + 1);
            }
          }
        },
        v.value());
  }
  std::vector<std::byte> finish() { return std::move(own_); }

private:
  ValueLimits limits_;
  bool count_only_ = false;
  std::size_t counted_ = 0;
  std::vector<std::byte> own_;
  std::vector<std::byte> *out_ = &own_;
  std::size_t base_ = 0;
  std::size_t nodes_ = 0;
};
class Decoder {
public:
  Decoder(ByteView data, ValueLimits limits,
          std::span<const std::string_view> omitted = {})
      : data_(data), limits_(limits), omitted_(omitted) {}
  unsigned char byte() {
    if (at_ == data_.size())
      throw Error{ErrorCode::corrupt};
    return std::to_integer<unsigned char>(data_[at_++]);
  }
  std::uint64_t varint() {
    std::uint64_t n = 0;
    for (unsigned i = 0; i < 10; ++i) {
      const auto b = byte();
      if (i == 9 && b > 1)
        throw Error{ErrorCode::corrupt};
      n |= static_cast<std::uint64_t>(b & 127) << (7 * i);
      if (!(b & 128)) {
        if (i && b == 0)
          throw Error{ErrorCode::corrupt};
        return n;
      }
    }
    throw Error{ErrorCode::corrupt};
  }
  std::string raw_text(bool retain = true) {
    const auto n = varint();
    if (n > data_.size() - at_)
      throw Error{ErrorCode::corrupt};
    const auto size = static_cast<std::size_t>(n);
    std::string s;
    if (retain)
      s.assign(reinterpret_cast<const char *>(data_.data() + at_), size);
    at_ += size;
    return s;
  }
  std::string string(unsigned char tag, bool retain = true) {
    if (tag == text)
      return raw_text(retain);
    if (tag == word) {
      const auto n = varint();
      if (!n || n > std::size(words))
        throw Error{ErrorCode::corrupt};
      return retain ? std::string{words[n - 1]} : std::string{};
    }
    if (tag == identity) {
      constexpr char hex[] = "0123456789abcdef";
      std::string s;
      s.reserve(32);
      for (unsigned i = 0; i < 16; ++i) {
        const auto b = byte();
        if (retain) {
          s += hex[b >> 4];
          s += hex[b & 15];
        }
      }
      return s;
    }
    throw Error{ErrorCode::corrupt};
  }
  Value value(std::size_t depth, bool retain = true) {
    if (depth > limits_.depth || ++nodes_ > limits_.nodes)
      throw Error{ErrorCode::capacity};
    const auto tag = byte();
    switch (tag) {
    case null_value:
      return Value{};
    case false_value:
      return Value{false};
    case true_value:
      return Value{true};
    case unsigned_integer:
      return Value{Number{varint()}};
    case negative_integer: {
      const auto n = varint();
      if (!n || n > (std::uint64_t{1} << 63))
        throw Error{ErrorCode::corrupt};
      const auto signed_value =
          n == (std::uint64_t{1} << 63) ? INT64_MIN : -static_cast<std::int64_t>(n);
      return Value{Number{signed_value}};
    }
    case real: {
      std::uint64_t bits = 0;
      for (unsigned i = 0; i < 8; ++i)
        bits |= static_cast<std::uint64_t>(byte()) << (i * 8);
      return Value{Number{std::bit_cast<double>(bits)}};
    }
    case decimal: {
      const auto sign = byte();
      if (sign > 1)
        throw Error{ErrorCode::corrupt};
      const auto zigzag = varint();
      const auto exponent = zigzag & 1 ? -static_cast<std::int64_t>(zigzag >> 1) - 1
                                       : static_cast<std::int64_t>(zigzag >> 1);
      const auto count = varint();
      if (!count || count > (data_.size() - at_) / 4)
        throw Error{ErrorCode::corrupt};
      Decimal n{sign != 0, exponent, {}};
      if (retain)
        n.limbs.reserve(static_cast<std::size_t>(count));
      std::uint32_t last = 0;
      for (std::uint64_t at = 0; at < count; ++at) {
        std::uint32_t limb = 0;
        for (unsigned i = 0; i < 4; ++i)
          limb |= static_cast<std::uint32_t>(byte()) << (i * 8);
        if (limb >= 1000000000)
          throw Error{ErrorCode::corrupt};
        last = limb;
        if (retain)
          n.limbs.push_back(limb);
      }
      if (count > 1 && last == 0)
        throw Error{ErrorCode::corrupt};
      if (!retain)
        return Value{};
      return Value{Number{std::move(n)}};
    }
    case text:
    case word:
    case identity:
      return retain ? Value{string(tag)} : (void(string(tag, false)), Value{});
    case array:
    case object: {
      const auto count = varint();
      // Every child consumes a node and at least one byte (keys consume another).
      if (count > limits_.nodes - nodes_)
        throw Error{ErrorCode::capacity};
      if (count > (data_.size() - at_) / (tag == object ? 2 : 1))
        throw Error{ErrorCode::corrupt};
      if (tag == array) {
        Value::Array out;
        if (retain)
          out.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
          auto child = value(depth + 1, retain);
          if (retain)
            out.push_back(std::move(child));
        }
        return Value{std::move(out)};
      }
      Value::Object out;
      if (retain)
        out.reserve(static_cast<std::size_t>(count));
      for (std::uint64_t i = 0; i < count; ++i) {
        auto key = string(byte());
        const bool keep =
            retain && !(depth == 0 && std::find(omitted_.begin(), omitted_.end(),
                                                key) != omitted_.end());
        auto child = value(depth + 1, keep);
        if (retain)
          out.emplace_back(std::move(key), keep ? std::move(child) : Value{});
      }
      return Value::object(std::move(out));
    }
    default:
      throw Error{ErrorCode::corrupt};
    }
  }
  bool complete() const { return at_ == data_.size(); }

private:
  ByteView data_;
  ValueLimits limits_;
  std::span<const std::string_view> omitted_;
  std::size_t at_ = 0, nodes_ = 0;
};
} // namespace
Result<std::vector<std::byte>> encode_packet(const Value &value, ValueLimits limits) {
  try {
    Encoder e{limits};
    for (const auto b : {'B', 'B', 'M', '\2'})
      e.byte(static_cast<unsigned char>(b));
    e.value(value, 0);
    return Result<std::vector<std::byte>>::success(e.finish());
  } catch (const Error &e) {
    return Result<std::vector<std::byte>>::failure(e);
  } catch (const std::bad_alloc &) {
    return Result<std::vector<std::byte>>::failure({ErrorCode::allocation});
  }
}
Result<std::size_t> encode_packet_into(std::vector<std::byte> &out, const Value &value,
                                       ValueLimits limits) {
  const auto start = out.size();
  try {
    Encoder e{limits, out};
    for (const auto b : {'B', 'B', 'M', '\2'})
      e.byte(static_cast<unsigned char>(b));
    e.value(value, 0);
    return Result<std::size_t>::success(e.size());
  } catch (const Error &e) {
    out.resize(start);
    return Result<std::size_t>::failure(e);
  } catch (const std::bad_alloc &) {
    out.resize(start);
    return Result<std::size_t>::failure({ErrorCode::allocation});
  }
}
Result<std::size_t> encoded_packet_size(const Value &value, ValueLimits limits) {
  try {
    Encoder e{limits, true};
    for (const auto b : {'B', 'B', 'M', '\2'})
      e.byte(static_cast<unsigned char>(b));
    e.value(value, 0);
    return Result<std::size_t>::success(e.size());
  } catch (const Error &e) {
    return Result<std::size_t>::failure(e);
  } catch (const std::bad_alloc &) {
    return Result<std::size_t>::failure({ErrorCode::allocation});
  }
}
Result<Value> decode_packet_projection(ByteView bytes,
                                       std::span<const std::string_view> omitted,
                                       ValueLimits limits) {
  try {
    if (bytes.size() > limits.bytes)
      return Result<Value>::failure({ErrorCode::capacity});
    if (bytes.size() < 4 || bytes[0] != std::byte{'B'} || bytes[1] != std::byte{'B'} ||
        bytes[2] != std::byte{'M'})
      return Result<Value>::failure({ErrorCode::corrupt});
    if (bytes[3] != std::byte{2})
      return Result<Value>::failure({ErrorCode::unsupported});
    Decoder d{bytes.subspan(4), limits, omitted};
    auto value = d.value(0);
    if (!d.complete())
      return Result<Value>::failure({ErrorCode::corrupt});
    return Result<Value>::success(std::move(value));
  } catch (const Error &e) {
    return Result<Value>::failure(e);
  } catch (const std::bad_alloc &) {
    return Result<Value>::failure({ErrorCode::allocation});
  }
}
Result<Value> decode_packet(ByteView bytes, ValueLimits limits) {
  return decode_packet_projection(bytes, {}, limits);
}
Result<std::string> encode_packet_string(const Value &value, ValueLimits limits) {
  auto bytes = encode_packet(value, limits);
  if (!bytes.has_value())
    return Result<std::string>::failure(bytes.error());
  try {
    return Result<std::string>::success(std::string{
        reinterpret_cast<const char *>(bytes.value().data()), bytes.value().size()});
  } catch (const std::bad_alloc &) {
    return Result<std::string>::failure({ErrorCode::allocation});
  }
}
Result<Value> decode_packet_value(const Value &bytes, ValueLimits limits) {
  try {
    if (std::holds_alternative<std::string>(bytes.value()))
      return decode_packet_string(bytes.string(), limits);
    const auto *encoding = bytes.find("encoding"), *data = bytes.find("bytes");
    if (!encoding || *encoding != Value{"hex"} || !data ||
        !std::holds_alternative<std::string>(data->value()))
      return Result<Value>::failure({ErrorCode::invalid_range});
    const auto &text = data->string();
    if (text.size() % 2 || text.size() / 2 > limits.bytes)
      return Result<Value>::failure({ErrorCode::capacity});
    std::vector<std::byte> raw;
    raw.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
      unsigned number = 0;
      const auto parsed =
          std::from_chars(text.data() + i, text.data() + i + 2, number, 16);
      if (parsed.ec != std::errc{} || parsed.ptr != text.data() + i + 2)
        return Result<Value>::failure({ErrorCode::corrupt});
      raw.push_back(static_cast<std::byte>(number));
    }
    return decode_packet(raw, limits);
  } catch (const std::bad_alloc &) {
    return Result<Value>::failure({ErrorCode::allocation});
  }
}
Result<Value> decode_packet_string(std::string_view bytes, ValueLimits limits) {
  return decode_packet(std::as_bytes(std::span{bytes.data(), bytes.size()}), limits);
}
} // namespace blackbird
