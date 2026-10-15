#include "blackbird/json.hpp"
#include "blackbird/request_cache.hpp"
#include <algorithm>
#include <stdexcept>

namespace blackbird {
namespace {
[[noreturn]] void bad() { throw Error{ErrorCode::corrupt}; }
[[noreturn]] void full() { throw Error{ErrorCode::capacity}; }
bool digit(char c) { return c >= '0' && c <= '9'; }
// Strict scalar-value UTF-8, rejecting overlong forms and surrogate encodings.
bool utf8(std::string_view s) {
  for (std::size_t i = 0; i < s.size();) {
    const auto first = static_cast<unsigned char>(s[i++]);
    if (first < 128)
      continue;
    unsigned count = 0, scalar = 0, minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      count = 1;
      scalar = first & 31U;
      minimum = 128;
    } else if (first >= 0xe0 && first <= 0xef) {
      count = 2;
      scalar = first & 15U;
      minimum = 2048;
    } else if (first >= 0xf0 && first <= 0xf4) {
      count = 3;
      scalar = first & 7U;
      minimum = 65536;
    } else
      return false;
    if (s.size() - i < count)
      return false;
    for (unsigned j = 0; j < count; ++j) {
      const auto next = static_cast<unsigned char>(s[i++]);
      if ((next & 0xc0U) != 0x80U)
        return false;
      scalar = (scalar << 6U) | (next & 63U);
    }
    if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
      return false;
  }
  return true;
}
void scalar_utf8(std::string &out, unsigned scalar) {
  if (scalar < 128)
    out.push_back(static_cast<char>(scalar));
  else if (scalar < 2048) {
    out.push_back(static_cast<char>(0xc0U | (scalar >> 6U)));
    out.push_back(static_cast<char>(0x80U | (scalar & 63U)));
  } else if (scalar < 65536) {
    out.push_back(static_cast<char>(0xe0U | (scalar >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 63U)));
    out.push_back(static_cast<char>(0x80U | (scalar & 63U)));
  } else {
    out.push_back(static_cast<char>(0xf0U | (scalar >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 63U)));
    out.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 63U)));
    out.push_back(static_cast<char>(0x80U | (scalar & 63U)));
  }
}
class Parser {
public:
  Parser(std::string_view input, ValueLimits bounds,
         std::span<const std::string_view> omit = {})
      : text(input), limits(bounds), omitted(omit) {}
  Value run() {
    if (text.size() > limits.bytes)
      full();
    auto result = item(0);
    space();
    if (pos != text.size())
      bad();
    return result;
  }

private:
  std::string_view text;
  ValueLimits limits;
  std::span<const std::string_view> omitted;
  std::size_t pos = 0, nodes = 0;
  void space() {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' ||
                                 text[pos] == '\r' || text[pos] == '\t'))
      ++pos;
  }
  char take() {
    if (pos == text.size())
      bad();
    return text[pos++];
  }
  bool eat(char c) {
    if (pos < text.size() && text[pos] == c) {
      ++pos;
      return true;
    }
    return false;
  }
  unsigned hex() {
    unsigned value = 0;
    for (unsigned i = 0; i < 4; ++i) {
      const char c = take();
      unsigned d = 0;
      if (digit(c))
        d = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f')
        d = static_cast<unsigned>(c - 'a') + 10U;
      else if (c >= 'A' && c <= 'F')
        d = static_cast<unsigned>(c - 'A') + 10U;
      else
        bad();
      value = (value << 4U) | d;
    }
    return value;
  }
  std::string string(bool retain = true) {
    if (take() != '"')
      bad();
    // Validate the common unescaped case directly in retained input. Escaped
    // strings use the same scalar decoder as before (including discarded ones).
    const auto start = pos;
    bool non_ascii = false;
    while (pos < text.size()) {
      // Fixed-size reductions are vectorizable without reading past input.
      // A mixed block falls back to the scalar decoder below.
      if (text.size() - pos >= 16) {
        unsigned special = 0;
        for (std::size_t i = 0; i < 16; ++i) {
          const auto byte = static_cast<unsigned char>(text[pos + i]);
          special |= static_cast<unsigned>(byte < 32 || byte >= 128 || byte == '"' ||
                                           byte == '\\');
        }
        if (special == 0) {
          pos += 16;
          continue;
        }
      }
      const auto c = static_cast<unsigned char>(text[pos]);
      if (c == '"') {
        const auto value = text.substr(start, pos - start);
        ++pos;
        if (non_ascii && !utf8(value))
          bad();
        return retain ? std::string{value} : std::string{};
      }
      if (c < 32)
        bad();
      if (c == '\\')
        break;
      non_ascii = non_ascii || c >= 128;
      ++pos;
    }
    pos = start;
    std::string out;
    while (true) {
      const auto c = take();
      if (c == '"')
        break;
      if (static_cast<unsigned char>(c) < 32)
        bad();
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      switch (take()) {
      case '"':
        out.push_back('"');
        break;
      case '\\':
        out.push_back('\\');
        break;
      case '/':
        out.push_back('/');
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'u': {
        auto scalar = hex();
        if (scalar >= 0xd800 && scalar <= 0xdbff) {
          if (take() != '\\' || take() != 'u')
            bad();
          const auto low = hex();
          if (low < 0xdc00 || low > 0xdfff)
            bad();
          scalar = 0x10000U + ((scalar - 0xd800U) << 10U) + low - 0xdc00U;
        } else if (scalar >= 0xdc00 && scalar <= 0xdfff)
          bad();
        scalar_utf8(out, scalar);
        break;
      }
      default:
        bad();
      }
    }
    if (!utf8(out))
      bad();
    return retain ? std::move(out) : std::string{};
  }
  Value number(bool retain) {
    const auto start = pos;
    (void)eat('-');
    if (!eat('0')) {
      if (pos == text.size() || text[pos] < '1' || text[pos] > '9')
        bad();
      while (pos < text.size() && digit(text[pos]))
        ++pos;
    }
    if (eat('.')) {
      const auto before = pos;
      while (pos < text.size() && digit(text[pos]))
        ++pos;
      if (before == pos)
        bad();
    }
    if (eat('e') || eat('E')) {
      if (!eat('+'))
        (void)eat('-');
      const auto before = pos;
      while (pos < text.size() && digit(text[pos]))
        ++pos;
      if (before == pos)
        bad();
    }
    return retain ? Value{Number{std::string{text.substr(start, pos - start)}}}
                  : Value{};
  }
  Value item(std::size_t depth, bool retain = true) {
    if (depth > limits.depth || nodes == limits.nodes)
      full();
    ++nodes;
    space();
    if (pos == text.size())
      bad();
    if (text[pos] == '"')
      return Value{string(retain)};
    if (eat('[')) {
      Value::Array values;
      space();
      if (eat(']'))
        return Value{std::move(values)};
      do {
        auto value = item(depth + 1, retain);
        if (retain)
          values.push_back(std::move(value));
        space();
        if (eat(']'))
          return Value{std::move(values)};
      } while (eat(','));
      bad();
    }
    if (eat('{')) {
      Value::Object fields;
      space();
      if (eat('}'))
        return Value::object(std::move(fields));
      do {
        space();
        auto key = string();
        for (const auto &field : fields)
          if (field.first == key)
            bad();
        space();
        if (!eat(':'))
          bad();
        const bool keep =
            retain && !(depth == 0 && std::find(omitted.begin(), omitted.end(), key) !=
                                          omitted.end());
        auto value = item(depth + 1, keep);
        fields.emplace_back(std::move(key), keep ? std::move(value) : Value{});
        space();
        if (eat('}'))
          return Value::object(std::move(fields));
      } while (eat(','));
      bad();
    }
    for (const auto literal : {std::string_view{"true"}, std::string_view{"false"},
                               std::string_view{"null"}}) {
      if (text.substr(pos, literal.size()) == literal) {
        pos += literal.size();
        return literal == "null" ? Value{} : Value{literal == "true"};
      }
    }
    return number(retain);
  }
};
class Writer {
public:
  explicit Writer(ValueLimits bounds) : limits(bounds) {}
  std::string run(const Value &value) {
    item(value, 0);
    return std::move(out);
  }

private:
  ValueLimits limits;
  std::size_t nodes = 0;
  std::string out;
  void add(std::string_view s) {
    if (s.size() > limits.bytes - out.size())
      full();
    out.append(s);
  }
  void string(std::string_view s) {
    if (!utf8(s))
      bad();
    add("\"");
    constexpr char hex[] = "0123456789abcdef";
    for (const auto c : s) {
      if (c == '"')
        add("\\\"");
      else if (c == '\\')
        add("\\\\");
      else if (static_cast<unsigned char>(c) < 32) {
        const auto n = static_cast<unsigned char>(c);
        const char escape[] = {'\\', 'u', '0', '0', hex[n >> 4U], hex[n & 15U]};
        add(std::string_view{escape, 6});
      } else
        add(std::string_view{&c, 1});
    }
    add("\"");
  }
  void item(const Value &j, std::size_t depth) {
    if (depth > limits.depth || nodes == limits.nodes)
      full();
    ++nodes;
    std::visit(
        [&](const auto &v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, std::nullptr_t>)
            add("null");
          else if constexpr (std::is_same_v<T, bool>)
            add(v ? "true" : "false");
          else if constexpr (std::is_same_v<T, Number>) {
            add(v.text());
          } else if constexpr (std::is_same_v<T, std::string>) {
            if (valid_utf8(v))
              string(v);
            else {
              constexpr char digits[] = "0123456789abcdef";
              std::string hex;
              if (v.size() > limits.bytes / 2)
                full();
              hex.reserve(v.size() * 2);
              for (const char character : v) {
                const auto byte = static_cast<unsigned char>(character);
                hex += digits[byte >> 4];
                hex += digits[byte & 15];
              }
              item(Value::object(
                       {{"encoding", Value{"hex"}}, {"bytes", Value{std::move(hex)}}}),
                   depth);
            }
          } else if constexpr (std::is_same_v<T, Value::Array>) {
            add("[");
            bool first = true;
            for (const auto &child : v) {
              if (!first)
                add(",");
              first = false;
              item(child, depth + 1);
            }
            add("]");
          } else {
            add("{");
            bool first = true;
            for (std::size_t i = 0; i < v.size(); ++i) {
              for (std::size_t k = 0; k < i; ++k)
                if (v[k].first == v[i].first)
                  bad();
              if (!first)
                add(",");
              first = false;
              string(v[i].first);
              add(":");
              item(v[i].second, depth + 1);
            }
            add("}");
          }
        },
        j.value());
  }
};
} // namespace
Result<Value> parse_json(std::string_view input, ValueLimits limits) {
  try {
    return Result<Value>::success(Parser{input, limits}.run());
  } catch (const Error &e) {
    return Result<Value>::failure(e);
  } catch (const std::bad_alloc &) {
    return Result<Value>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Result<Value>::failure({ErrorCode::capacity});
  }
}
Result<Value> parse_json_projection(std::string_view input,
                                    std::span<const std::string_view> omitted,
                                    ValueLimits limits) {
  try {
    return Result<Value>::success(Parser{input, limits, omitted}.run());
  } catch (const Error &e) {
    return Result<Value>::failure(e);
  } catch (const std::bad_alloc &) {
    return Result<Value>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Result<Value>::failure({ErrorCode::capacity});
  }
}
Result<std::string> dump_json(const Value &value, ValueLimits limits) {
  try {
    return Result<std::string>::success(Writer{limits}.run(value));
  } catch (const Error &e) {
    return Result<std::string>::failure(e);
  } catch (const std::bad_alloc &) {
    return Result<std::string>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Result<std::string>::failure({ErrorCode::capacity});
  }
}
Value import_provider_response(Value response) {
  auto *output = response.find("output");
  if (!output || !std::holds_alternative<Value::Array>(output->value()))
    return response;
  for (auto &entry : response.object())
    if (entry.first == "output")
      for (auto &item : entry.second.array()) {
        const auto *type = item.find("type");
        if (!type || *type != Value{"function_call"})
          continue;
        for (auto &[key, value] : item.object())
          if (key == "arguments" &&
              std::holds_alternative<std::string>(value.value())) {
            auto parsed = parse_json(value.string());
            if (!parsed.has_value())
              throw parsed.error();
            value = std::move(parsed).value();
          }
      }
  return response;
}
Value export_provider_request(Value request) {
  for (auto &[key, input] : request.object())
    if (key == "input" && std::holds_alternative<Value::Array>(input.value()))
      for (auto &item : input.array()) {
        const auto *type = item.find("type");
        if (!type)
          continue;
        const bool output = *type == Value{"function_call_output"};
        const bool call = *type == Value{"function_call"};
        const auto *parts_choice = item.find("blackbird_output_parts");
        const bool explicit_parts =
            output && parts_choice && *parts_choice == Value{true};
        if (parts_choice && !explicit_parts)
          throw Error{ErrorCode::invalid_range};
        if (explicit_parts) {
          const auto *parts = item.find("output");
          if (!parts || !std::holds_alternative<Value::Array>(parts->value()))
            throw Error{ErrorCode::invalid_range};
          for (const auto &part : parts->array()) {
            const auto *kind = part.find("type");
            if (!kind ||
                (*kind != Value{"input_text"} && *kind != Value{"input_image"} &&
                 *kind != Value{"input_file"}))
              throw Error{ErrorCode::unsupported};
            if (*kind == Value{"input_text"}) {
              const auto *text = part.find("text");
              if (!text || !std::holds_alternative<std::string>(text->value()))
                throw Error{ErrorCode::invalid_range};
            }
          }
        }
        for (auto &[field, value] : item.object()) {
          if (!((output && field == "output") || (call && field == "arguments")))
            continue;
          if (!std::holds_alternative<std::string>(value.value()) &&
              !(output && explicit_parts)) {
            auto encoded = dump_json(value);
            if (!encoded.has_value())
              throw encoded.error();
            value = Value{std::move(encoded).value()};
          }
        }
        std::erase_if(item.object(), [](const auto &entry) {
          return entry.first == "blackbird_output_parts";
        });
      }
  export_request_cache(request);
  return request;
}
} // namespace blackbird
