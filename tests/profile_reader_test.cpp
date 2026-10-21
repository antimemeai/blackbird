// Specification, model and property oracles for the data-only profile reader.
#include "blackbird/provider_profile.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
using namespace blackbird;
namespace {
void check(bool ok, const std::string &reason) {
  if (!ok)
    throw std::runtime_error(reason);
}
Value number(const char *text) { return Value{Number{std::string_view{text}}}; }
Value object(Value::Object fields) { return Value{std::move(fields)}; }
Value array(Value::Array items) { return Value{std::move(items)}; }
ProfileResult<Value> read(std::string_view text, ProfileReaderLimits limits = {}) {
  return read_profile_text({text, "case.lua"}, limits);
}
std::string shown(const ProfileResult<Value> &result) {
  if (result.has_value())
    return "accepted " + format_value(result.value()).value();
  return result.error().message();
}

struct Accept {
  std::string_view text;
  Value expected;
};
void accept_cases() {
  const std::vector<Accept> cases{
      {"return {}", object({})},
      {"return {};", object({})},
      {"return{a=1}", object({{"a", number("1")}})},
      {"return { a = 1, b = \"x\", c = true, d = false }",
       object({{"a", number("1")},
               {"b", Value{"x"}},
               {"c", Value{true}},
               {"d", Value{false}}})},
      {"return { \"a\", 'b', }", array({Value{"a"}, Value{"b"}})},
      {"return { [\"key with space\"] = 1; _x1 = 2; }",
       object({{"key with space", number("1")}, {"_x1", number("2")}})},
      {"-- head\nreturn { --[[ block ]] a = 1, --[==[ x ]] ]==] b = 2 } -- tail",
       object({{"a", number("1")}, {"b", number("2")}})},
      {"return { 0, 7, -3, 1.5, -0.25, 1e3, 2E-2, 18446744073709551615, "
       "18446744073709551616, -0 }",
       array({number("0"), number("7"), number("-3"), number("1.5"), number("-0.25"),
              number("1e3"), number("2E-2"), number("18446744073709551615"),
              number("18446744073709551616"), number("0")})},
      {R"lua(return { "\a\b\f\n\r\t\v\\\"\'", "\65\066\0671", "\x41\u{48}\u{10FFFF}",
"a\z
   b", "line\
continued" })lua",
       array({Value{"\a\b\f\n\r\t\v\\\"'"},
              Value{"AB"
                    "C1"},
              Value{"AH\xF4\x8F\xBF\xBF"}, Value{"ab"}, Value{"line\ncontinued"}})},
      {"return { [[x]], [==[a]]b]==], [[\nfirst newline skipped]], [[tab\there]] }",
       array({Value{"x"}, Value{"a]]b"}, Value{"first newline skipped"},
              Value{"tab\there"}})},
      {"return { a = { b = { c = { \"d\" } } } }",
       object({{"a", object({{"b", object({{"c", array({Value{"d"}})}})}})}})},
      {"return {\r\n  a = [[x\r\ny]],\r\n}\r\n", object({{"a", Value{"x\ny"}}})},
      {"return { \"\xC3\xA9t\xC3\xA9\" } -- \xE2\x9C\x93 comment",
       array({Value{"\xC3\xA9t\xC3\xA9"}})},
      {"return { [ [[k]] ] = 1 }", object({{"k", number("1")}})},
      {"return { \"\\u{000000000041}\\u{0000000000000000E9}\" }",
       array({Value{"A\xC3\xA9"}})},
  };
  for (const auto &c : cases) {
    const auto result = read(c.text);
    check(result.has_value() && result.value() == c.expected,
          "accept: " + std::string{c.text} + " -> " + shown(result));
  }
}

bool error_message_shape() {
  return ProfileError{"p.lua", 3, 7, "nil", "`nil` is not a data value"}.message() ==
             "p.lua:3:7: nil: `nil` is not a data value" &&
         ProfileError{"p.lua", 0, 0, "io", "cannot open the file"}.message() ==
             "p.lua: io: cannot open the file";
}

struct Refuse {
  std::string text;
  std::string construct;
  std::string reason;
  std::string marker; // column of its first occurrence; line 1 unless given below
  std::size_t line = 1;
  std::size_t column = 0;
};
void refuse_cases() {
  std::string deep = "return ";
  for (int i = 0; i < 33; ++i)
    deep += "{";
  const std::vector<Refuse> cases{
      {"return { f = function() end }", "function",
       "function constructors are not data", "function"},
      {"return { x = print(\"hi\") }", "call", "call of `print` is not data", "print"},
      {"return { x = require \"os\" }", "call", "call of `require` is not data",
       "require"},
      {"return { x = foo { } }", "call", "call of `foo` is not data", "foo"},
      {"return { x = a:b() }", "call", "method call on `a` is not data", "a:"},
      {"return setmetatable({}, {})", "call", "call of `setmetatable` is not data",
       "setmetatable"},
      {"return { x = os.execute }", "global", "global reference `os` is not data",
       "os"},
      {"return { y }", "global", "global reference `y` is not data", "y"},
      {"return { x = 1 + 2 }", "operator", "operator `+` is not data", "+"},
      {"return { x = \"a\" .. \"b\" }", "operator", "operator `..` is not data", ".."},
      {"return { x = - 1 }", "operator", "operator `-` is not data", "-"},
      {"return { x = -\"a\" }", "operator", "operator `-` is not data", "-"},
      {"return { x = not true }", "operator", "operator `not` is not data", "not"},
      {"return { x = #\"abc\" }", "operator", "operator `#` is not data", "#"},
      {"return { x = 1 == 1 }", "operator", "operator `==` is not data", "=="},
      {"return { x = true and false }", "operator", "operator `and` is not data",
       "and"},
      {"return { 1 -2 }", "operator", "operator `-` is not data", "-"},
      {"return {} .. {}", "operator", "operator `..` is not data", ".."},
      {"return { x = nil }", "nil", "`nil` is not a data value", "nil"},
      {"return { nil }", "nil", "`nil` is not a data value", "nil"},
      {"return { __index = {} }", "metatable",
       "field name `__index` is reserved for metatables", "__index"},
      {"return { [\"__gc\"] = 1 }", "metatable",
       "field name `__gc` is reserved for metatables", "["},
      {"return { x = \"abc }", "string", "unterminated string", "\""},
      {"return { x = \"ab\ncd\" }", "string", "unterminated string", "\""},
      {"return { x = [[abc }", "string", "unterminated long string", "[["},
      {"return { x = \"\\q\" }", "string", "invalid escape `\\q`", "\\"},
      {"return { x = \"\\256\" }", "string", "escape `\\256` is larger than 255", "\\"},
      {"return { x = \"\\xZZ\" }", "string",
       "escape `\\x` needs two hexadecimal digits", "\\"},
      {"return { x = \"\\u{D800}\" }", "string",
       "escape `\\u{D800}` is not a Unicode scalar value", "\\"},
      {"return { x = \"\\u{110000}\" }", "string",
       "escape `\\u{110000}` is not a Unicode scalar value", "\\"},
      {"return { x = \"\\u48\" }", "string", "escape `\\u` needs `{hex}`", "\\"},
      {"return { x = \"\\255\" }", "string", "string is not valid UTF-8", "\""},
      {"return { x = \"a\x01"
       "b\" }",
       "string", "raw control byte 0x01 in a string; use an escape", "\x01"},
      {"return { x = \"\xff\" }", "encoding", "invalid UTF-8 byte sequence", "\xff"},
      {"return { x = 1 } -- \xC3\x28", "encoding", "invalid UTF-8 byte sequence",
       "\xC3"},
      {"\xEF\xBB\xBFreturn {}", "encoding", "byte-order mark is not accepted", "\xEF"},
      {deep, "depth", "tables nest deeper than 32", "", 1, 40},
      {"return { 0x10 }", "numeral", "hexadecimal numerals are not accepted", "0x"},
      {"return { 3x }", "numeral", "malformed numeral `3x`", "3x"},
      {"return { 007 }", "numeral", "numeral `007` has a leading zero", "007"},
      {"return { 1. }", "numeral", "malformed numeral `1.`", "1."},
      {"return { 1..2 }", "numeral", "malformed numeral `1..2`", "1."},
      {"return { 1e+ }", "numeral", "malformed numeral `1e+`", "1e"},
      {"return { .5 }", "numeral", "numeral `.5` needs a digit before the point", ".5"},
      {"return { 1e400 }", "numeral",
       "numeral `1e400` is outside the range of a Lua float", "1e400"},
      {"return { -2e-400 }", "numeral",
       "numeral `-2e-400` is outside the range of a Lua float", "-2e"},
      {"return { 1, a = 2 }", "table",
       "a table mixes positional items and named fields", "a ="},
      {"return { a = 2, 1 }", "table",
       "a table mixes positional items and named fields", "1 }"},
      {"return { a = 1, a = 2 }", "table", "duplicate field `a`", "a = 2"},
      {"return { [1] = 2 }", "table", "only string keys are accepted in `[...]`", "["},
      {"return { a = 1 b = 2 }", "table", "expected `,`, `;` or `}`", "b ="},
      {"return { \"a\" \"b\" }", "table", "expected `,`, `;` or `}`", "\"b"},
      {"return { a = 1", "table", "unterminated table", "", 1, 15},
      {"return { [\"a\" = 1 }", "table", "expected `]` after the key", "= 1"},
      {"return { end = 1 }", "keyword", "reserved word `end` is not data", "end"},
      {"return { x = goto }", "keyword", "reserved word `goto` is not data", "goto"},
      {"return { ... }", "vararg", "`...` is not data", "..."},
      {"return { (1) }", "parenthesis", "parenthesized expressions are not data", "("},
      {"local t = {} return t", "statement",
       "only `return <table>` is accepted; found `local`", "local"},
      {"t = {}", "statement", "only `return <table>` is accepted; found `t`", "t"},
      {"", "statement", "only `return <table>` is accepted; found end of file", "", 1,
       1},
      {"#!/bin/lua\nreturn {}", "statement",
       "only `return <table>` is accepted; found `#`", "#"},
      {"return 5", "return", "the returned value must be a table", "5"},
      {"return", "return", "the returned value must be a table", "", 1, 7},
      {"return {} {}", "return", "content after the returned table", "{}", 1, 11},
      {"return { x = 1 } return {}", "return", "content after the returned table",
       "return {}", 1, 18},
      {"return { x = 1 }\n--[[ unterminated", "comment", "unterminated long comment",
       "", 2, 1},
      {"return { x = @ }", "character", "unexpected character `@`", "@"},
      {"return { x = 1 }\n\n  return", "return", "content after the returned table", "",
       3, 3},
  };
  for (const auto &c : cases) {
    const auto result = read(c.text);
    std::size_t column = c.column;
    if (column == 0) {
      const auto at = c.text.find(c.marker);
      check(at != std::string::npos, "bad fixture marker for " + c.text);
      column = at + 1;
    }
    const ProfileError expected{"case.lua", c.line, column, c.construct, c.reason};
    check(!result.has_value() && result.error() == expected,
          "refuse: " + c.text + " -> " + shown(result) + " (expected " +
              expected.message() + ")");
    check(std::ranges::find(profile_reader_constructs, c.construct) !=
              std::end(profile_reader_constructs),
          "fixture construct outside the closed set: " + c.construct);
  }
  // Limits are exact and positioned.
  const auto large =
      read("return { \"abcdef\" }", {.bytes = 10, .depth = 32, .values = 9});
  check(!large.has_value() &&
            large.error() ==
                ProfileError{"case.lua", 0, 0, "size", "file is larger than 10 bytes"},
        "size limit: " + shown(large));
  const auto many =
      read("return { 1, 2, 3 }", {.bytes = 100, .depth = 32, .values = 3});
  check(!many.has_value() && many.error() == ProfileError{"case.lua", 1, 16, "size",
                                                          "more than 3 values"},
        "value limit: " + shown(many));
  check(error_message_shape(), "message shape");
}
// Deterministic generators shared by the property and model oracles.
class Generator {
public:
  explicit Generator(std::uint64_t seed) : random_(seed) {}
  std::size_t below(std::size_t n) {
    return std::uniform_int_distribution<std::size_t>{0, n - 1}(random_);
  }
  std::string utf8_string() {
    std::string out;
    const auto length = below(12);
    for (std::size_t i = 0; i < length; ++i) {
      switch (below(6)) {
      case 0:
        out.push_back(static_cast<char>(below(32))); // control bytes, NUL included
        break;
      case 1:
        out += std::string{"\"\\'[]=-\n"}.substr(below(8), 1);
        break;
      case 2:
        out += "\xC3\xA9"; // é
        break;
      case 3:
        out += "\xF0\x9F\x9B\xA9"; // four-byte scalar
        break;
      default:
        out.push_back(static_cast<char>('a' + below(26)));
      }
    }
    return out;
  }
  std::string key() {
    if (below(3) == 0)
      return "k" + utf8_string();
    std::string name(1, static_cast<char>('a' + below(26)));
    name += std::to_string(below(1000));
    return name;
  }
  Number number() {
    switch (below(4)) {
    case 0:
      return Number{static_cast<std::int64_t>(below(2000000)) - 1000000};
    case 1:
      return Number{std::numeric_limits<std::uint64_t>::max() - below(1000)};
    case 2:
      return Number{Decimal{below(2) == 0,
                            static_cast<std::int64_t>(below(60)) - 30,
                            {static_cast<std::uint32_t>(1 + below(999999998))}}};
    default:
      return Number{std::int64_t{std::numeric_limits<std::int64_t>::min() +
                                 static_cast<std::int64_t>(below(1000))}};
    }
  }
  // Containers are never empty: `{}` reads as an empty object by definition.
  Value value(std::size_t depth) {
    const auto kind = depth >= 4 ? below(3) : below(5);
    if (kind == 0)
      return Value{utf8_string()};
    if (kind == 1)
      return Value{number()};
    if (kind == 2)
      return Value{below(2) == 0};
    return container(depth, kind == 3);
  }
  Value container(std::size_t depth, bool as_array) {
    const auto count = 1 + below(4);
    if (as_array) {
      Value::Array items;
      for (std::size_t i = 0; i < count; ++i)
        items.push_back(value(depth + 1));
      return Value{std::move(items)};
    }
    Value::Object fields;
    for (std::size_t i = 0; i < count; ++i) {
      auto name = key();
      if (std::ranges::none_of(fields, [&](const auto &f) { return f.first == name; }))
        fields.emplace_back(std::move(name), value(depth + 1));
    }
    return Value{std::move(fields)};
  }
  std::mt19937_64 &engine() { return random_; }

private:
  std::mt19937_64 random_;
};

// format_value's Lua-shaped text, read back, is the same Value (round trip).
void roundtrip() {
  // Every zero numeral, signed or not, in any form, is one canonical zero: equal to 0,
  // presented as `0`, and stable through format and read.
  for (const std::string zero :
       {"0", "-0", "0.0", "-0.0", "0e3", "-0e3", "-0.000", "0.000e-5"}) {
    const auto text = "return { " + zero + " }";
    const auto first = read(text);
    check(first.has_value() && first.value() == array({number("0")}) &&
              format_value(first.value()).value() == "{0}",
          "zero " + zero + " -> " + shown(first));
    const auto again = read("return " + format_value(first.value()).value());
    check(again.has_value() && again.value() == first.value(),
          "zero round trip " + zero);
  }
  // The 128-character numeral bound holds for the presented form too: dropping the
  // point for `e-25`, or padding `e16` with zeros, may lengthen a numeral past it
  // (arconaut-c6d). Such a value is refused; one presented in 128 characters reads.
  const std::string ones(128, '1');
  for (const auto &[numeral, accepted] : std::vector<std::pair<std::string, bool>>{
           {ones.substr(0, 104) + "." + ones.substr(0, 20), true},
           {ones.substr(0, 100) + "." + ones.substr(0, 25), false},
           {"-" + ones.substr(0, 100) + "." + ones.substr(0, 25), false},
           {ones.substr(0, 112) + "e16", true},
           {ones.substr(0, 113) + "e16", false}}) {
    const auto first = read("return { " + numeral + " }");
    check(first.has_value() == accepted &&
              (accepted || first.error().subject == "numeral"),
          "presented numeral bound " + numeral + " -> " + shown(first));
    if (accepted) {
      const auto again = read("return " + format_value(first.value()).value());
      check(again.has_value() && again.value() == first.value(),
            "presented numeral round trip " + numeral + " -> " + shown(again));
    }
  }
  Generator g{0x5eed};
  for (int i = 0; i < 4000; ++i) {
    const auto v = g.container(0, g.below(2) == 0);
    const auto text = "return " + format_value(v).value();
    const auto result = read(text);
    check(result.has_value() && result.value() == v,
          "round trip: " + text + " -> " + shown(result));
  }
}

// Real Lua 5.4, with an empty environment, is the reference model for accepted texts.
Value from_lua(lua_State *lua, int index) {
  switch (lua_type(lua, index)) {
  case LUA_TBOOLEAN:
    return Value{lua_toboolean(lua, index) != 0};
  case LUA_TSTRING: {
    std::size_t size = 0;
    const char *bytes = lua_tolstring(lua, index, &size);
    return Value{std::string{bytes, size}};
  }
  case LUA_TNUMBER:
    if (lua_isinteger(lua, index) != 0)
      return Value{Number{static_cast<std::int64_t>(lua_tointeger(lua, index))}};
    return Value{Number{static_cast<double>(lua_tonumber(lua, index))}};
  case LUA_TTABLE: {
    const auto length = static_cast<std::size_t>(lua_rawlen(lua, index));
    Value::Array items;
    Value::Object fields;
    lua_pushnil(lua);
    while (lua_next(lua, index < 0 ? index - 1 : index) != 0) {
      if (lua_type(lua, -2) == LUA_TSTRING) {
        std::size_t size = 0;
        const char *bytes = lua_tolstring(lua, -2, &size);
        fields.emplace_back(std::string{bytes, size}, from_lua(lua, lua_gettop(lua)));
      }
      lua_pop(lua, 1);
    }
    for (std::size_t i = 1; i <= length; ++i) {
      lua_rawgeti(lua, index, static_cast<lua_Integer>(i));
      items.push_back(from_lua(lua, lua_gettop(lua)));
      lua_pop(lua, 1);
    }
    check(items.empty() || fields.empty(), "Lua model produced a mixed table");
    if (!items.empty())
      return Value{std::move(items)};
    std::ranges::sort(fields, {}, &Value::Object::value_type::first);
    return Value{std::move(fields)};
  }
  default:
    throw std::runtime_error("Lua model produced a non-data value");
  }
}
Value sorted(const Value &v) {
  if (const auto *fields = std::get_if<Value::Object>(&v.value())) {
    Value::Object out;
    for (const auto &[k, x] : *fields)
      out.emplace_back(k, sorted(x));
    std::ranges::sort(out, {}, &Value::Object::value_type::first);
    return Value{std::move(out)};
  }
  if (const auto *items = std::get_if<Value::Array>(&v.value())) {
    Value::Array out;
    for (const auto &x : *items)
      out.push_back(sorted(x));
    return Value{std::move(out)};
  }
  return v;
}
// Numbers agree as Lua reads them: Lua integers are integer storage with the same
// value; Lua floats are decimal storage with the same binary64 value.
bool same(const Value &ours, const Value &model) {
  if (const auto *n = std::get_if<Number>(&ours.value())) {
    const auto *m = std::get_if<Number>(&model.value());
    if (!m)
      return false;
    const bool integer = std::holds_alternative<std::int64_t>(n->storage()) ||
                         std::holds_alternative<std::uint64_t>(n->storage());
    if (const auto *i = std::get_if<std::int64_t>(&m->storage()))
      return integer && n->text() == std::to_string(*i);
    return !integer &&
           std::strtod(n->text().c_str(), nullptr) == std::get<double>(m->storage());
  }
  if (ours.value().index() != model.value().index())
    return false;
  if (const auto *a = std::get_if<Value::Array>(&ours.value())) {
    const auto &b = model.array();
    if (a->size() != b.size())
      return false;
    for (std::size_t i = 0; i < a->size(); ++i)
      if (!same((*a)[i], b[i]))
        return false;
    return true;
  }
  if (const auto *a = std::get_if<Value::Object>(&ours.value())) {
    const auto &b = model.object();
    if (a->size() != b.size())
      return false;
    for (std::size_t i = 0; i < a->size(); ++i)
      if ((*a)[i].first != b[i].first || !same((*a)[i].second, b[i].second))
        return false;
    return true;
  }
  return ours == model;
}
Value lua_model(lua_State *lua, const std::string &text) {
  const int base = lua_gettop(lua);
  check(luaL_loadbufferx(lua, text.data(), text.size(), "=model", "t") == LUA_OK,
        "Lua refused an accepted text: " + text);
  lua_newtable(lua); // empty _ENV: nothing global is reachable
  check(lua_setupvalue(lua, -2, 1) != nullptr, "chunk has no _ENV upvalue");
  check(lua_pcall(lua, 0, 1, 0) == LUA_OK, "Lua failed on an accepted text: " + text);
  auto result = from_lua(lua, lua_gettop(lua));
  lua_settop(lua, base);
  return result;
}
void model_oracle() {
  lua_State *lua = luaL_newstate();
  check(lua != nullptr, "Lua state");
  std::vector<std::string> texts{
      R"lua(return { "\a\b\f\n\r\t\v\\\"\'", "\65\066\0671", "\x41\u{48}\u{7FF}\u{FFFF}" })lua",
      "return { \"a\\z  \n\n  b\", \"x\\\ny\", \"x\\\r\ny\", 'q\"' }",
      "return { [[x]], [==[a]]b]==], [[\nskip]], [[\r\nskip]], [=[\n\nkeep]=] }",
      "return {\r\n a = [[l1\r\nl2\n\rl3\rl4]] }",
      "return { 0, 7, -3, 1.5, -0.25, 1e3, 2E-2, 9007199254740993, "
      "9223372036854775807, 9223372036854775808, -9223372036854775808, "
      "18446744073709551616, 1e308, 2.5e-310, -0, -0.0, 0.0, -0e3, 0e0 }",
      "return { \"\\u{000000000041}\", \"\\u{00000000E9}\" }",
      "return { a = 1; [\"b c\"] = { 'x', }, d = { e = { f = true } }, g = false, }",
      "--[==[ long\n comment ]==] return { --x\n y = 1 } -- t",
  };
  Generator g{0xa11ce};
  for (int i = 0; i < 1500; ++i)
    texts.push_back("return " + format_value(g.container(0, g.below(2) == 0)).value());
  for (const auto &text : texts) {
    const auto ours = read(text);
    check(ours.has_value(), "model fixture refused: " + text + " -> " + shown(ours));
    const auto model = lua_model(lua, text);
    check(same(sorted(ours.value()), model), "reader and Lua disagree on " + text +
                                                 ": " +
                                                 format_value(ours.value()).value() +
                                                 " vs " + format_value(model).value());
  }
  lua_close(lua);
}

// Inputs that would act if Lua ran them are refused and act on nothing.
void no_execution() {
  std::string directory = "/tmp/blackbird-profile-reader-XXXXXX";
  check(mkdtemp(directory.data()) != nullptr, "temporary directory");
  const auto sentinel = directory + "/sentinel";
  struct Case {
    std::string text;
    std::string construct;
  };
  const std::vector<Case> cases{
      {"return { x = os.execute(\"touch " + sentinel + "\") }", "global"},
      {"return { x = io.open(\"" + sentinel + "\", \"w\") }", "global"},
      {"return (function() io.open(\"" + sentinel + "\", \"w\") end)()", "parenthesis"},
      {"return { x = load(\"io.open('" + sentinel + "', 'w')\")() }", "call"},
      {"return { x = require(\"os\").exit(3) }", "call"},
      {"os.exit(3) return {}", "statement"},
      {"return { x = string.rep(\"x\", 1000000000) }", "global"},
      {"return { x = (function() while true do end end)() }", "parenthesis"},
      {"return { x = dofile(\"" + sentinel + "\") }", "call"},
      {"return setmetatable({}, { __gc = function() os.exit(4) end })", "call"},
  };
  for (const auto &c : cases) {
    const auto result = read(c.text);
    check(!result.has_value() && result.error().subject == c.construct,
          "no execution: " + c.text + " -> " + shown(result));
  }
  check(!std::filesystem::exists(sentinel), "a refused profile produced an effect");
  std::filesystem::remove_all(directory);
}

// Arbitrary bytes, token soup and mutated shipped profiles: never a crash, a refusal
// always names a closed-set construct at a real position, and accepted input re-reads
// to itself through format_value.
void property(const ProfileResult<Value> &result, std::string_view input) {
  if (result.has_value()) {
    const auto &v = result.value();
    check(std::holds_alternative<Value::Object>(v.value()) ||
              std::holds_alternative<Value::Array>(v.value()),
          "accepted a non-table");
    const auto again = read("return " + format_value(v).value());
    check(again.has_value() && again.value() == v, "accepted value does not re-read");
    return;
  }
  const auto &e = result.error();
  check(std::ranges::find(profile_reader_constructs, e.subject) !=
            std::end(profile_reader_constructs),
        "construct outside the closed set: " + e.subject);
  check(!e.reason.empty(), "empty reason");
  const auto lines = static_cast<std::size_t>(std::ranges::count(input, '\n') +
                                              std::ranges::count(input, '\r')) +
                     1;
  check(e.subject == "size" || (e.line >= 1 && e.line <= lines && e.column >= 1 &&
                                e.column <= input.size() + 1),
        "refusal position out of range: " + e.message());
}
void generated(const std::filesystem::path &shipped) {
  Generator g{0xf022};
  auto &random = g.engine();
  for (int i = 0; i < 6000; ++i) {
    std::string bytes(g.below(200), '\0');
    for (auto &b : bytes)
      b = static_cast<char>(random() & 0xffU);
    property(read(bytes), bytes);
  }
  const std::vector<std::string> soup{
      "return", " ",        "{",    "}",     "=",         ",",     ";",       "a",
      "_b",     "\"s\"",    "'t'",  "[[x]]", "[==[y]==]", "--",    "--[[c]]", "\n",
      "\r",     "1",        "0",    "-",     ".",         "..",    "...",     "(",
      ")",      "function", "end",  "nil",   "true",      "false", "[",       "]",
      "\\",     "\"\\",     "\xff", "0x",    "e",         "e+",    "__index", "\"\\u{",
      "\"\\z",  "#",        "not",  ":",     "goto"};
  for (int i = 0; i < 20000; ++i) {
    std::string text = g.below(4) == 0 ? "" : "return ";
    const auto count = g.below(24);
    for (std::size_t j = 0; j < count; ++j)
      text += soup[g.below(soup.size())];
    property(read(text), text);
  }
  std::vector<std::string> profiles;
  for (const auto &entry : std::filesystem::directory_iterator{shipped})
    if (entry.path().extension() == ".lua") {
      std::ifstream input{entry.path(), std::ios::binary};
      profiles.emplace_back(std::istreambuf_iterator<char>{input},
                            std::istreambuf_iterator<char>{});
    }
  check(!profiles.empty(), "no shipped profiles to mutate");
  for (const auto &profile : profiles)
    check(read(profile).has_value(), "shipped profile refused by the reader");
  for (int i = 0; i < 20000; ++i) {
    auto text = profiles[g.below(profiles.size())];
    const auto edits = 1 + g.below(4);
    for (std::size_t j = 0; j < edits && !text.empty(); ++j) {
      const auto at = g.below(text.size());
      switch (g.below(4)) {
      case 0:
        text[at] = static_cast<char>(random() & 0xffU);
        break;
      case 1:
        text.erase(at, 1 + g.below(8));
        break;
      case 2:
        text.insert(at, soup[g.below(soup.size())]);
        break;
      default:
        text.resize(at);
      }
    }
    property(read(text), text);
  }
  // Unbounded nesting is refused at the limit, never by exhausting the stack.
  const std::string deep = "return " + std::string(1000000, '{');
  const auto result = read(deep);
  check(!result.has_value() && result.error().subject == "depth", "deep nesting");
}
} // namespace
int main(int argc, char **argv) {
  try {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "grammar") {
      accept_cases();
      refuse_cases();
    } else if (mode == "roundtrip") {
      roundtrip();
    } else if (mode == "lua_model") {
      model_oracle();
    } else if (mode == "no_execution") {
      no_execution();
    } else if (mode == "generated" && argc > 2) {
      generated(argv[2]);
    } else {
      std::cerr << "usage: profile_reader_test grammar|roundtrip|lua_model|"
                   "no_execution|generated PROFILES\n";
      return 2;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
