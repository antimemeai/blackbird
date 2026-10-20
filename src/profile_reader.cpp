// Native reader of the data-only Lua table-constructor subset (CONTEXT_LIFECYCLE
// §15.1). It interprets nothing: there is no environment, no name lookup and no
// evaluation. Every construct outside the subset is refused with its position and exact
// reason.
#include "blackbird/provider_profile.hpp"
#include <array>
#include <charconv>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <new>
#include <set>
#include <system_error>
#include <utility>

namespace blackbird {
std::string ProfileError::message() const {
  std::string out = file;
  if (line != 0)
    out += ":" + std::to_string(line) + ":" + std::to_string(column);
  return out + ": " + subject + ": " + reason;
}
namespace {
struct Refusal {
  std::size_t line;
  std::size_t column;
  std::string construct;
  std::string reason;
};
struct Position {
  std::size_t offset = 0;
  std::size_t line = 1;
  std::size_t column = 1;
};
enum class TokenKind : std::uint8_t { name, keyword, string, number, punct, end };
struct Token {
  TokenKind kind = TokenKind::end;
  std::string text; // spelling; decoded bytes for a string
  Position at;
};
constexpr std::array<std::string_view, 22> keywords{
    "and",      "break",  "do",   "else", "elseif", "end",  "false", "for",
    "function", "goto",   "if",   "in",   "local",  "nil",  "not",   "or",
    "repeat",   "return", "then", "true", "until",  "while"};
// Longest first, so a prefix never shadows a longer token.
constexpr std::array<std::string_view, 33> puncts{
    "...", "<<", ">>", "//", "==", "~=", "<=", ">=", "::", "..", "+",
    "-",   "*",  "/",  "%",  "^",  "#",  "&",  "~",  "|",  "<",  ">",
    "=",   "(",  ")",  "{",  "}",  "[",  "]",  ";",  ":",  ",",  "."};
constexpr std::array<std::string_view, 21> operators{
    "+",  "-",  "*",  "/",  "//", "%", "^",  "#", "&",  "~", "|",
    "<<", ">>", "..", "==", "~=", "<", "<=", ">", ">=", "::"};

bool name_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool digit(char c) { return c >= '0' && c <= '9'; }
bool name_char(char c) { return name_start(c) || digit(c); }
bool hex_digit(char c) {
  return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
unsigned hex_value(char c) {
  if (digit(c))
    return static_cast<unsigned>(c - '0');
  if (c >= 'a' && c <= 'f')
    return static_cast<unsigned>(c - 'a' + 10);
  return static_cast<unsigned>(c - 'A' + 10);
}
bool is_operator(const Token &t) {
  if (t.kind == TokenKind::keyword)
    return t.text == "and" || t.text == "or" || t.text == "not";
  if (t.kind != TokenKind::punct)
    return false;
  for (const auto op : operators)
    if (t.text == op)
      return true;
  return false;
}
void append_utf8(std::string &out, std::uint32_t scalar) {
  if (scalar < 0x80) {
    out.push_back(static_cast<char>(scalar));
  } else if (scalar < 0x800) {
    out.push_back(static_cast<char>(0xc0U | (scalar >> 6U)));
    out.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
  } else if (scalar < 0x10000) {
    out.push_back(static_cast<char>(0xe0U | (scalar >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU)));
    out.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
  } else {
    out.push_back(static_cast<char>(0xf0U | (scalar >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 0x3fU)));
    out.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU)));
    out.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
  }
}
std::string byte_text(unsigned char b) {
  std::array<char, 8> text{};
  (void)std::snprintf(text.data(), text.size(), "0x%02X", static_cast<unsigned>(b));
  return text.data();
}

class Reader {
public:
  Reader(std::string_view text, ProfileReaderLimits limits)
      : text_(text), limits_(limits) {}

  Value chunk() {
    check_encoding();
    next();
    if (!(current_.kind == TokenKind::keyword && current_.text == "return"))
      refuse(current_.at, "statement",
             "only `return <table>` is accepted; found " + describe(current_));
    next();
    if (current_.kind == TokenKind::end)
      refuse(current_.at, "return", "the returned value must be a table");
    if (!(current_.kind == TokenKind::punct && current_.text == "{")) {
      const auto at = current_.at;
      (void)value(0);
      refuse(at, "return", "the returned value must be a table");
    }
    auto result = value(0);
    if (current_.kind == TokenKind::punct && current_.text == ";")
      next();
    if (current_.kind != TokenKind::end) {
      after_value();
      refuse(current_.at, "return", "content after the returned table");
    }
    return result;
  }

private:
  [[noreturn]] static void refuse(Position at, std::string construct,
                                  std::string reason) {
    throw Refusal{at.line, at.column, std::move(construct), std::move(reason)};
  }
  static std::string describe(const Token &t) {
    if (t.kind == TokenKind::end)
      return "end of file";
    if (t.kind == TokenKind::string)
      return "a string";
    return "`" + t.text + "`";
  }
  // Every byte must be UTF-8, comments included. Positions count bytes, as advance().
  void check_encoding() const {
    Position at;
    if (text_.starts_with("\xEF\xBB\xBF"))
      refuse(at, "encoding", "byte-order mark is not accepted");
    for (std::size_t i = 0; i < text_.size();) {
      const auto first = static_cast<unsigned char>(text_[i]);
      std::size_t length = 1;
      if (first >= 0xc2 && first <= 0xdf)
        length = 2;
      else if (first >= 0xe0 && first <= 0xef)
        length = 3;
      else if (first >= 0xf0 && first <= 0xf4)
        length = 4;
      else if (first >= 0x80)
        refuse(at, "encoding", "invalid UTF-8 byte sequence");
      if (text_.size() - i < length || !valid_utf8(text_.substr(i, length)))
        refuse(at, "encoding", "invalid UTF-8 byte sequence");
      if (first == '\n' || first == '\r') {
        if (i + 1 < text_.size() && (text_[i + 1] == '\n' || text_[i + 1] == '\r') &&
            text_[i + 1] != text_[i])
          ++length;
        ++at.line;
        at.column = 1;
      } else {
        at.column += length;
      }
      i += length;
      at.offset = i;
    }
  }
  bool more() const { return pos_.offset < text_.size(); }
  char peek(std::size_t ahead = 0) const {
    return pos_.offset + ahead < text_.size() ? text_[pos_.offset + ahead] : '\0';
  }
  bool has(std::size_t ahead) const { return pos_.offset + ahead < text_.size(); }
  // Advances one byte; a line ends at \n, \r, \r\n or \n\r, as in Lua.
  void advance() {
    const char c = text_[pos_.offset++];
    if (c == '\n' || c == '\r') {
      if (more() && (peek() == '\n' || peek() == '\r') && peek() != c)
        ++pos_.offset;
      ++pos_.line;
      pos_.column = 1;
    } else {
      ++pos_.column;
    }
  }
  // Counts the `=` of a long bracket at the cursor; -1 when it is not one.
  long long_bracket() const {
    if (peek() != '[')
      return -1;
    std::size_t level = 1;
    while (has(level) && peek(level) == '=')
      ++level;
    return has(level) && peek(level) == '[' ? static_cast<long>(level - 1) : -1;
  }
  std::string long_text(long level, Position start, std::string_view what) {
    for (long i = 0; i < level + 2; ++i)
      advance();
    if (more() && (peek() == '\n' || peek() == '\r'))
      advance();
    std::string out;
    while (true) {
      if (!more())
        refuse(start, what == "comment" ? "comment" : "string",
               "unterminated long " + std::string{what});
      const char c = peek();
      if (c == ']') {
        long close = 1;
        while (has(static_cast<std::size_t>(close)) &&
               peek(static_cast<std::size_t>(close)) == '=')
          ++close;
        if (close - 1 == level && has(static_cast<std::size_t>(close)) &&
            peek(static_cast<std::size_t>(close)) == ']') {
          for (long i = 0; i < level + 2; ++i)
            advance();
          return out;
        }
      }
      if (c == '\n' || c == '\r') {
        advance();
        out.push_back('\n');
        continue;
      }
      if (what == "string") {
        const auto b = static_cast<unsigned char>(c);
        if ((b < 0x20 && c != '\t') || b == 0x7f)
          refuse(pos_, "string",
                 "raw control byte " + byte_text(b) + " in a string; use an escape");
      }
      out.push_back(c);
      advance();
    }
  }
  void skip_space() {
    while (more()) {
      const char c = peek();
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
        advance();
      } else if (c == '-' && peek(1) == '-') {
        const auto start = pos_;
        advance();
        advance();
        if (const auto level = long_bracket(); level >= 0) {
          (void)long_text(level, start, "comment");
        } else {
          while (more() && peek() != '\n' && peek() != '\r')
            advance();
        }
      } else {
        return;
      }
    }
  }
  void next() {
    skip_space();
    current_ = Token{TokenKind::end, {}, pos_};
    if (!more())
      return;
    const char c = peek();
    if (name_start(c)) {
      std::string name;
      while (more() && name_char(peek())) {
        name.push_back(peek());
        advance();
      }
      current_.kind = TokenKind::name;
      for (const auto keyword : keywords)
        if (name == keyword)
          current_.kind = TokenKind::keyword;
      current_.text = std::move(name);
      return;
    }
    if (digit(c) || (c == '.' && digit(peek(1)))) {
      current_.kind = TokenKind::number;
      current_.text = numeral();
      return;
    }
    if (c == '"' || c == '\'') {
      current_.kind = TokenKind::string;
      current_.text = short_string();
      return;
    }
    if (const auto level = long_bracket(); level >= 0) {
      current_.kind = TokenKind::string;
      current_.text = long_text(level, pos_, "string");
      return;
    }
    for (const auto p : puncts)
      if (text_.substr(pos_.offset).starts_with(p)) {
        current_.kind = TokenKind::punct;
        current_.text = std::string{p};
        for (std::size_t j = 0; j < p.size(); ++j)
          advance();
        return;
      }
    const auto b = static_cast<unsigned char>(c);
    if (b < 0x20 || b >= 0x7f)
      refuse(pos_, "character", "unexpected byte " + byte_text(b));
    refuse(pos_, "character", "unexpected character `" + std::string{c} + "`");
  }
  // Scans like Lua's read_numeral, then accepts only the decimal subset.
  std::string numeral() {
    const auto start = pos_;
    std::string text;
    while (more()) {
      const char c = peek();
      if ((c == '+' || c == '-') && !text.empty() &&
          (text.back() == 'e' || text.back() == 'E') &&
          !(text.starts_with("0x") || text.starts_with("0X"))) {
        text.push_back(c);
        advance();
      } else if (name_char(c) || c == '.') {
        text.push_back(c);
        advance();
      } else {
        break;
      }
      if (text.size() > 128)
        refuse(start, "numeral", "numeral longer than 128 characters");
    }
    if (text.starts_with("0x") || text.starts_with("0X"))
      refuse(start, "numeral", "hexadecimal numerals are not accepted");
    if (text.starts_with("."))
      refuse(start, "numeral", "numeral `" + text + "` needs a digit before the point");
    std::size_t i = 0;
    while (i < text.size() && digit(text[i]))
      ++i;
    if (i > 1 && text[0] == '0')
      refuse(start, "numeral", "numeral `" + text + "` has a leading zero");
    if (i < text.size() && text[i] == '.') {
      const auto fraction = ++i;
      while (i < text.size() && digit(text[i]))
        ++i;
      if (i == fraction)
        refuse(start, "numeral", "malformed numeral `" + text + "`");
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
      ++i;
      if (i < text.size() && (text[i] == '+' || text[i] == '-'))
        ++i;
      const auto exponent = i;
      while (i < text.size() && digit(text[i]))
        ++i;
      if (i == exponent)
        refuse(start, "numeral", "malformed numeral `" + text + "`");
    }
    if (i != text.size())
      refuse(start, "numeral", "malformed numeral `" + text + "`");
    return text;
  }
  std::string short_string() {
    const auto start = pos_;
    const char quote = peek();
    advance();
    std::string out;
    while (true) {
      if (!more() || peek() == '\n' || peek() == '\r')
        refuse(start, "string", "unterminated string");
      const char c = peek();
      if (c == quote) {
        advance();
        break;
      }
      if (c != '\\') {
        const auto b = static_cast<unsigned char>(c);
        if ((b < 0x20 && c != '\t') || b == 0x7f)
          refuse(pos_, "string",
                 "raw control byte " + byte_text(b) + " in a string; use an escape");
        out.push_back(c);
        advance();
        continue;
      }
      escape(out);
    }
    if (!valid_utf8(out))
      refuse(start, "string", "string is not valid UTF-8");
    return out;
  }
  void escape(std::string &out) {
    const auto at = pos_;
    advance();
    if (!more())
      refuse(at, "string", "unterminated string");
    const char c = peek();
    static constexpr std::string_view simple = "abfnrtv\\\"'";
    static constexpr std::string_view bytes = "\a\b\f\n\r\t\v\\\"'";
    if (const auto found = simple.find(c); found != std::string_view::npos) {
      out.push_back(bytes[found]);
      advance();
    } else if (c == '\n' || c == '\r') {
      out.push_back('\n');
      advance();
    } else if (c == 'x') {
      advance();
      if (!hex_digit(peek()) || !hex_digit(peek(1)))
        refuse(at, "string", "escape `\\x` needs two hexadecimal digits");
      out.push_back(static_cast<char>((hex_value(peek()) << 4U) | hex_value(peek(1))));
      advance();
      advance();
    } else if (c == 'z') {
      advance();
      while (more() && (peek() == ' ' || peek() == '\t' || peek() == '\n' ||
                        peek() == '\r' || peek() == '\v' || peek() == '\f'))
        advance();
    } else if (digit(c)) {
      unsigned value = 0;
      std::string spelled;
      for (int i = 0; i < 3 && more() && digit(peek()); ++i) {
        value = value * 10 + static_cast<unsigned>(peek() - '0');
        spelled.push_back(peek());
        advance();
      }
      if (value > 255)
        refuse(at, "string", "escape `\\" + spelled + "` is larger than 255");
      out.push_back(static_cast<char>(value));
    } else if (c == 'u') {
      advance();
      if (peek() != '{')
        refuse(at, "string", "escape `\\u` needs `{hex}`");
      advance();
      // Any number of hex digits, leading zeros included, as Lua reads them; the
      // value saturates once it is past every Unicode scalar.
      std::uint64_t scalar = 0;
      std::string spelled;
      while (more() && hex_digit(peek())) {
        if (scalar <= 0x10ffff)
          scalar = (scalar << 4U) | hex_value(peek());
        if (spelled.size() < 64)
          spelled.push_back(peek());
        advance();
      }
      if (spelled.empty() || peek() != '}')
        refuse(at, "string", "escape `\\u` needs `{hex}`");
      advance();
      if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
        refuse(at, "string",
               "escape `\\u{" + spelled + "}` is not a Unicode scalar value");
      append_utf8(out, static_cast<std::uint32_t>(scalar));
    } else {
      const auto b = static_cast<unsigned char>(c);
      refuse(at, "string",
             b < 0x20 || b >= 0x7f ? "invalid escape before byte " + byte_text(b)
                                   : "invalid escape `\\" + std::string{c} + "`");
    }
  }
  // A token after a complete value: an operator there makes an expression.
  void after_value() const {
    if (is_operator(current_))
      refuse(current_.at, "operator", "operator `" + current_.text + "` is not data");
  }
  // Lua's subtypes: an integer numeral within int64 is integer storage; any other
  // numeral (a point, an exponent, or an integer past int64) is a float, stored as an
  // exact decimal. A float outside binary64 is refused. Every zero is one canonical
  // zero, unsigned: integer 0, or decimal 0 for a float zero.
  Number number_value(std::string_view spelled, bool negative, Position at) {
    std::string text = negative ? "-" : "";
    text += spelled;
    const bool integer = spelled.find_first_of(".eE") == std::string_view::npos;
    if (integer) {
      std::uint64_t magnitude = 0;
      const auto r =
          std::from_chars(spelled.data(), spelled.data() + spelled.size(), magnitude);
      if (r.ec == std::errc{} && r.ptr == spelled.data() + spelled.size() &&
          magnitude <= static_cast<std::uint64_t>(INT64_MAX))
        return Number{negative ? -static_cast<std::int64_t>(magnitude)
                               : static_cast<std::int64_t>(magnitude)};
      text += "e0"; // past int64: Lua reads a float
    }
    double binary64 = 0;
    const auto r =
        std::from_chars(spelled.data(), spelled.data() + spelled.size(), binary64);
    if (r.ec == std::errc::result_out_of_range)
      refuse(at, "numeral",
             "numeral `" + std::string{negative ? "-" : ""} + std::string{spelled} +
                 "` is outside the range of a Lua float");
    auto parsed = parse_number(text);
    if (!parsed.has_value())
      refuse(at, "numeral", "malformed numeral `" + text + "`");
    if (const auto *d = std::get_if<Decimal>(&parsed.value().storage());
        d && d->limbs.size() == 1 && d->limbs[0] == 0)
      return Number{Decimal{false, 0, {0}}};
    return std::move(parsed).value();
  }
  void count_value(Position at) {
    if (++values_ > limits_.values)
      refuse(at, "size", "more than " + std::to_string(limits_.values) + " values");
  }
  Value value(std::size_t depth) {
    const auto at = current_.at;
    count_value(at);
    const auto token = current_;
    switch (token.kind) {
    case TokenKind::end:
      refuse(at, "table", "unexpected end of file");
    case TokenKind::string:
      next();
      return Value{token.text};
    case TokenKind::number: {
      auto number = number_value(token.text, false, at);
      next();
      return Value{std::move(number)};
    }
    case TokenKind::keyword:
      if (token.text == "true" || token.text == "false") {
        next();
        return Value{token.text == "true"};
      }
      if (token.text == "nil")
        refuse(at, "nil", "`nil` is not a data value");
      if (token.text == "function")
        refuse(at, "function", "function constructors are not data");
      if (token.text == "not")
        refuse(at, "operator", "operator `not` is not data");
      refuse(at, "keyword", "reserved word `" + token.text + "` is not data");
    case TokenKind::name: {
      next();
      if (current_.kind == TokenKind::punct && current_.text == ":")
        refuse(at, "call", "method call on `" + token.text + "` is not data");
      if ((current_.kind == TokenKind::punct &&
           (current_.text == "(" || current_.text == "{")) ||
          current_.kind == TokenKind::string)
        refuse(at, "call", "call of `" + token.text + "` is not data");
      refuse(at, "global", "global reference `" + token.text + "` is not data");
    }
    case TokenKind::punct:
      break;
    }
    if (token.text == "{")
      return table(depth + 1);
    if (token.text == "-" && pos_.offset == at.offset + 1 && more() && digit(peek())) {
      next();
      auto number = number_value(current_.text, true, at);
      next();
      return Value{std::move(number)};
    }
    if (token.text == "...")
      refuse(at, "vararg", "`...` is not data");
    if (token.text == "(")
      refuse(at, "parenthesis", "parenthesized expressions are not data");
    if (is_operator(token))
      refuse(at, "operator", "operator `" + token.text + "` is not data");
    refuse(at, "table", "unexpected `" + token.text + "`");
  }
  static void field_name(std::string_view name, Position at) {
    if (name.starts_with("__"))
      refuse(at, "metatable",
             "field name `" + std::string{name} + "` is reserved for metatables");
  }
  Value table(std::size_t depth) {
    const auto open = current_.at;
    if (depth > limits_.depth)
      refuse(open, "depth", "tables nest deeper than " + std::to_string(limits_.depth));
    next();
    Value::Array items;
    Value::Object fields;
    std::set<std::string> keys;
    while (!(current_.kind == TokenKind::punct && current_.text == "}")) {
      const auto at = current_.at;
      std::optional<std::string> key;
      if (current_.kind == TokenKind::punct && current_.text == "[") {
        next();
        if (current_.kind != TokenKind::string) {
          if (current_.kind == TokenKind::number)
            refuse(at, "table", "only string keys are accepted in `[...]`");
          (void)value(depth);
          refuse(at, "table", "only string keys are accepted in `[...]`");
        }
        key = current_.text;
        next();
        if (!(current_.kind == TokenKind::punct && current_.text == "]")) {
          after_value();
          refuse(current_.at, "table", "expected `]` after the key");
        }
        next();
        if (!(current_.kind == TokenKind::punct && current_.text == "="))
          refuse(current_.at, "table", "expected `=` after the key");
        next();
      } else if (current_.kind == TokenKind::name) {
        // A name is a field only when `=` follows; otherwise it is an expression.
        const auto saved_pos = pos_;
        const auto saved = current_;
        next();
        if (current_.kind == TokenKind::punct && current_.text == "=") {
          key = saved.text;
          next();
        } else {
          pos_ = saved_pos;
          current_ = saved;
        }
      } else if (current_.kind == TokenKind::keyword && current_.text != "true" &&
                 current_.text != "false" && current_.text != "nil" &&
                 current_.text != "function" && current_.text != "not") {
        refuse(at, "keyword", "reserved word `" + current_.text + "` is not data");
      }
      if (key) {
        field_name(*key, at);
        if (!items.empty())
          refuse(at, "table", "a table mixes positional items and named fields");
        if (!keys.insert(*key).second)
          refuse(at, "table", "duplicate field `" + *key + "`");
        auto v = value(depth);
        fields.emplace_back(std::move(*key), std::move(v));
      } else {
        if (!fields.empty())
          refuse(at, "table", "a table mixes positional items and named fields");
        items.push_back(value(depth));
      }
      if (current_.kind == TokenKind::punct &&
          (current_.text == "," || current_.text == ";")) {
        next();
        continue;
      }
      if (current_.kind == TokenKind::punct && current_.text == "}")
        break;
      after_value();
      refuse(current_.at, "table",
             current_.kind == TokenKind::end ? "unterminated table"
                                             : "expected `,`, `;` or `}`");
    }
    next();
    if (!items.empty())
      return Value{std::move(items)};
    return Value{std::move(fields)};
  }

  std::string_view text_;
  ProfileReaderLimits limits_;
  Position pos_;
  Token current_;
  std::size_t values_ = 0;
};
} // namespace

ProfileResult<Value> read_profile_text(ProfileSource source,
                                       ProfileReaderLimits limits) {
  const std::string file{source.file};
  if (source.text.size() > limits.bytes)
    return std::unexpected(
        ProfileError{file, 0, 0, "size",
                     "file is larger than " + std::to_string(limits.bytes) + " bytes"});
  try {
    Reader reader{source.text, limits};
    return reader.chunk();
  } catch (const Refusal &refusal) {
    return std::unexpected(ProfileError{file, refusal.line, refusal.column,
                                        refusal.construct, refusal.reason});
  } catch (const std::bad_alloc &) {
    return std::unexpected(ProfileError{file, 0, 0, "size", "out of memory"});
  }
}

ProfileResult<Value> read_profile_file(const std::filesystem::path &path,
                                       ProfileReaderLimits limits) {
  const auto file = path.string();
  std::ifstream input{path, std::ios::binary};
  if (!input)
    return std::unexpected(ProfileError{file, 0, 0, "io", "cannot open the file"});
  std::string text;
  std::array<char, 4096> buffer{};
  while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
    text.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
    if (text.size() > limits.bytes)
      return std::unexpected(ProfileError{file, 0, 0, "size",
                                          "file is larger than " +
                                              std::to_string(limits.bytes) + " bytes"});
  }
  if (input.bad())
    return std::unexpected(ProfileError{file, 0, 0, "io", "cannot read the file"});
  return read_profile_text({text, file}, limits);
}
} // namespace blackbird
