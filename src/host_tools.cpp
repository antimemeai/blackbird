#include "blackbird/host_tools.hpp"
#include "blackbird/context.hpp"
#include "blackbird/packet.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace blackbird {
namespace {
void keys(const Value &value, std::initializer_list<std::string_view> allowed) {
  if (!std::holds_alternative<Value::Object>(value.value()))
    throw Error{ErrorCode::invalid_range};
  std::set<std::string_view> seen;
  for (const auto &[name, entry] : value.object()) {
    (void)entry;
    if (std::find(allowed.begin(), allowed.end(), name) == allowed.end() ||
        !seen.insert(name).second)
      throw Error{ErrorCode::invalid_range};
  }
}
void name_text(std::string_view name, std::size_t limit) {
  if (name.empty() || name.size() > limit || !valid_utf8(name))
    throw Error{ErrorCode::invalid_range};
}
void schema(const Value &value, std::size_t depth, std::size_t &nodes) {
  if (depth > 16 || ++nodes > 256)
    throw Error{ErrorCode::capacity};
  const auto &kind = string_field(value, "kind");
  if (kind == "object") {
    keys(value, {"kind", "fields"});
    std::set<std::string> names;
    const auto &fields = field(value, "fields");
    if (!std::holds_alternative<Value::Array>(fields.value()))
      throw Error{ErrorCode::invalid_range};
    for (const auto &entry : fields.array()) {
      keys(entry, {"name", "required", "schema"});
      const auto &name = string_field(entry, "name");
      name_text(name, 128);
      if (!names.insert(name).second ||
          !std::holds_alternative<bool>(field(entry, "required").value()))
        throw Error{ErrorCode::invalid_range};
      schema(field(entry, "schema"), depth + 1, nodes);
    }
  } else if (kind == "array") {
    keys(value, {"kind", "element"});
    schema(field(value, "element"), depth + 1, nodes);
  } else {
    keys(value, {"kind"});
    if (kind != "string" && kind != "integer" && kind != "number" &&
        kind != "boolean" && kind != "null")
      throw Error{ErrorCode::unsupported};
  }
}
struct SchemaValue {
  const Value &schema;
  const Value &value;
};
void validate(SchemaValue input) {
  const auto &schema = input.schema;
  const auto &value = input.value;
  const auto &kind = string_field(schema, "kind");
  if (kind == "object") {
    if (!std::holds_alternative<Value::Object>(value.value()))
      throw Error{ErrorCode::invalid_range};
    const auto &fields = field(schema, "fields").array();
    std::set<std::string_view> seen;
    for (const auto &[name, entry] : value.object()) {
      if (!seen.insert(name).second)
        throw Error{ErrorCode::invalid_range};
      const auto found = std::find_if(fields.begin(), fields.end(), [&](const auto &f) {
        return string_field(f, "name") == name;
      });
      if (found == fields.end())
        throw Error{ErrorCode::invalid_range};
      validate({.schema = field(*found, "schema"), .value = entry});
    }
    for (const auto &entry : fields)
      if (std::get<bool>(field(entry, "required").value()) &&
          !value.find(string_field(entry, "name")))
        throw Error{ErrorCode::invalid_range};
    return;
  }
  if (kind == "array") {
    if (!std::holds_alternative<Value::Array>(value.value()))
      throw Error{ErrorCode::invalid_range};
    for (const auto &entry : value.array())
      validate({.schema = field(schema, "element"), .value = entry});
    return;
  }
  bool accepted =
      (kind == "string" && std::holds_alternative<std::string>(value.value()) &&
       valid_utf8(value.string())) ||
      (kind == "boolean" && std::holds_alternative<bool>(value.value())) ||
      (kind == "null" && value == Value{}) ||
      (kind == "number" && std::holds_alternative<Number>(value.value()));
  if (kind == "integer" && std::holds_alternative<Number>(value.value()))
    accepted = std::visit(
        [](const auto &number) {
          using T = std::decay_t<decltype(number)>;
          if constexpr (std::is_same_v<T, Decimal>)
            return number.exponent >= 0;
          else if constexpr (std::is_same_v<T, double>)
            return std::trunc(number) == number;
          else
            return true;
        },
        value.number().storage());
  if (!accepted)
    throw Error{ErrorCode::invalid_range};
}
Value provider_schema(const Value &schema) {
  const auto &kind = string_field(schema, "kind");
  auto result = Value::object({{"type", Value{kind}}});
  if (kind == "object") {
    Value::Object properties;
    Value::Array required;
    for (const auto &entry : field(schema, "fields").array()) {
      const auto &name = string_field(entry, "name");
      properties.emplace_back(name, provider_schema(field(entry, "schema")));
      if (std::get<bool>(field(entry, "required").value()))
        required.emplace_back(name);
    }
    result.object().emplace_back("properties", Value::object(std::move(properties)));
    result.object().emplace_back("required", Value{std::move(required)});
    result.object().emplace_back("additionalProperties", Value{false});
  } else if (kind == "array")
    result.object().emplace_back("items", provider_schema(field(schema, "element")));
  return result;
}
} // namespace
HostToolCatalog::HostToolCatalog(Value catalog) : catalog_(std::move(catalog)) {
  if (!std::holds_alternative<Value::Array>(catalog_.value()))
    throw Error{ErrorCode::invalid_range};
  if (catalog_.array().size() > 32 || unwrap(encode_packet(catalog_)).size() > 65536)
    throw Error{ErrorCode::capacity};
  std::set<std::string> names;
  for (const auto &entry : catalog_.array()) {
    keys(entry, {"name", "description", "arguments", "result"});
    const auto &name = string_field(entry, "name");
    name_text(name, 64);
    if (name == "provider" || !names.insert(name).second ||
        !std::all_of(name.begin(), name.end(), [](char c) {
          return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        }))
      throw Error{ErrorCode::invalid_range};
    name_text(string_field(entry, "description"), 4096);
    std::size_t nodes = 0;
    schema(field(entry, "arguments"), 0, nodes);
    schema(field(entry, "result"), 0, nodes);
    if (string_field(field(entry, "arguments"), "kind") != "object")
      throw Error{ErrorCode::invalid_range};
  }
}
const Value *HostToolCatalog::find(std::string_view name) const {
  for (const auto &entry : catalog_.array())
    if (string_field(entry, "name") == name)
      return &entry;
  return nullptr;
}
Value HostToolCatalog::names() const {
  Value::Array result;
  for (const auto &entry : catalog_.array())
    result.push_back(field(entry, "name"));
  return Value{std::move(result)};
}
Value HostToolCatalog::provider_tools() const {
  Value::Array result;
  for (const auto &entry : catalog_.array())
    result.push_back(
        Value::object({{"type", Value{"function"}},
                       {"name", field(entry, "name")},
                       {"description", field(entry, "description")},
                       {"parameters", provider_schema(field(entry, "arguments"))}}));
  return Value{std::move(result)};
}
void HostToolCatalog::validate_arguments(std::string_view name,
                                         const Value &arguments) const {
  const auto *definition = find(name);
  if (!definition)
    throw Error{ErrorCode::unsupported};
  validate({.schema = field(*definition, "arguments"), .value = arguments});
}
void HostToolCatalog::validate_result(std::string_view name,
                                      const Value &result) const {
  const auto *definition = find(name);
  if (!definition)
    throw Error{ErrorCode::unsupported};
  validate({.schema = field(*definition, "result"), .value = result});
}
} // namespace blackbird
