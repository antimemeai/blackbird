// Schema `blackbird.provider-profile/1`, validation, layering, selection and digests
// (CONTEXT_LIFECYCLE §15.2-§15.4). The schema is one declarative table; validation,
// basis normalization, merge, projection and the missing-basis report all walk it.
#include "blackbird/provider_profile.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <map>
#include <new>
#include <set>
#include <span>
#include <system_error>
#include <utility>

namespace blackbird {
namespace {
using namespace std::string_view_literals;

enum class Shape : std::uint8_t { text, integer, number, boolean, list, record, map };
enum class Form : std::uint8_t {
  any,
  identifier,
  url,
  link,
  repo_path,
  date,
  model,
  header
};
struct Node;
struct Field {
  std::string_view name;
  const Node *node;
  bool required = false;
};
struct Node {
  Shape shape;
  Form form = Form::any;
  std::span<const std::string_view> options{};
  std::int64_t min = 0;
  std::int64_t max = 0;
  const Node *element = nullptr;
  std::span<const Field> fields{};
  bool basis = false; // a record or map that may carry `basis`
  bool unique = false;
  bool non_empty = false;
  std::span<const std::string_view> one_of{};
  bool exclusive = false; // one_of: exactly one, not several
};

// Schema nodes live in their own namespace so local names never shadow them.
namespace node {
constexpr Node free_text{.shape = Shape::text};
constexpr Node identifier{.shape = Shape::text, .form = Form::identifier};
constexpr Node https_url{.shape = Shape::text, .form = Form::url};
constexpr Node https_link{.shape = Shape::text, .form = Form::link};
constexpr Node matcher_text{.shape = Shape::text, .non_empty = true};
constexpr Node repo_path{.shape = Shape::text, .form = Form::repo_path};
constexpr Node date{.shape = Shape::text, .form = Form::date};
constexpr Node model_name{.shape = Shape::text, .form = Form::model};
constexpr Node header_name{.shape = Shape::text, .form = Form::header};
constexpr Node boolean{.shape = Shape::boolean};
constexpr Node revision_number{.shape = Shape::integer, .min = 1, .max = 1000000000};
constexpr Node http_status{.shape = Shape::integer, .min = 100, .max = 599};
constexpr Node transport_code{.shape = Shape::integer, .min = 1, .max = 999};
constexpr Node token_count{.shape = Shape::integer, .min = 1, .max = 10000000};
constexpr Node minimum_count{.shape = Shape::integer, .min = 0, .max = 10000000};
constexpr Node multiplier{.shape = Shape::number, .min = 0, .max = 100};

#define BLACKBIRD_ENUM(name, ...)                                                      \
  constexpr std::array name##_values{__VA_ARGS__};                                     \
  constexpr Node name{.shape = Shape::text, .options = name##_values};
BLACKBIRD_ENUM(protocol, "messages"sv, "responses"sv, "responses-portable"sv, "chat"sv)
BLACKBIRD_ENUM(validator, "codec"sv, "anthropic-messages"sv, "responses"sv)
BLACKBIRD_ENUM(at_cut, "drop_after_change"sv, "keep"sv, "must_keep"sv,
               "keep_whole_or_drop"sv)
BLACKBIRD_ENUM(at_rebind, "keep_in_family"sv, "drop_foreign"sv)
BLACKBIRD_ENUM(side_reasoning, "drop_all"sv, "keep"sv)
BLACKBIRD_ENUM(reasoning_context, "default"sv, "current_turn"sv)
BLACKBIRD_ENUM(turn_start, "user_message_without_results"sv, "user_text"sv)
BLACKBIRD_ENUM(turn_close, "reply_request"sv, "none"sv)
BLACKBIRD_ENUM(open_turn_mode, "closed_only"sv, "retain_current_turn"sv, "boundary"sv,
               "any_closed_round"sv)
BLACKBIRD_ENUM(boundary_form, "user_note"sv, "reply_then_note"sv,
               "request_local_user"sv, "system_message"sv)
BLACKBIRD_ENUM(state_placement, "request_local"sv, "retained_in_place"sv)
BLACKBIRD_ENUM(note_role, "developer"sv, "developer_as_user_text"sv, "system"sv,
               "user"sv)
BLACKBIRD_ENUM(note_placement, "after_tool_results"sv,
               "after_user_turn_before_assistant"sv, "before_next_user_entry"sv)
BLACKBIRD_ENUM(restriction_mode, "palette_only"sv, "allowed_tools"sv,
               "appended_removal"sv)
BLACKBIRD_ENUM(tool_change_mode, "cut"sv, "appended_system_message"sv)
BLACKBIRD_ENUM(archival_mode, "allowed"sv, "refused_until_probed"sv)
BLACKBIRD_ENUM(edit_mode, "unknown"sv, "accepted"sv, "refused"sv)
BLACKBIRD_ENUM(cache_capability, "none"sv, "implicit"sv, "explicit"sv)
BLACKBIRD_ENUM(cache_ttl, "5m"sv, "30m"sv, "1h"sv)
BLACKBIRD_ENUM(breakpoint, "system_end"sv, "moving_tail"sv, "chapter_start"sv,
               "first_stable_item"sv)
BLACKBIRD_ENUM(error_class, "context_overflow"sv, "payload_too_large"sv, "overloaded"sv,
               "rate_limited"sv, "transient"sv)
BLACKBIRD_ENUM(stop_class, "completed"sv, "tool_calls"sv, "refusal"sv, "output_limit"sv,
               "output_window_exhausted"sv)
BLACKBIRD_ENUM(effort_level, "none"sv, "minimal"sv, "low"sv, "medium"sv, "high"sv,
               "xhigh"sv, "max"sv)
BLACKBIRD_ENUM(thinking_mode, "adaptive"sv, "budget"sv, "none"sv)
#undef BLACKBIRD_ENUM

constexpr Node model_list{
    .shape = Shape::list, .element = &model_name, .unique = true, .non_empty = true};
constexpr std::array match_fields{
    Field{"provider", &identifier}, Field{"protocol", &protocol},
    Field{"base_url", &https_url},  Field{"account_kind", &identifier},
    Field{"models", &model_list},   Field{"model_prefixes", &model_list}};
constexpr Node match{.shape = Shape::record, .fields = match_fields};

constexpr std::array source_matchers{"url"sv, "path"sv};
constexpr std::array source_fields{
    Field{"url", &https_link}, Field{"path", &repo_path}, Field{"read", &date, true},
    Field{"title", &free_text}, Field{"note", &free_text}};
constexpr Node source{.shape = Shape::record,
                      .fields = source_fields,
                      .one_of = source_matchers,
                      .exclusive = true};
constexpr Node sources{.shape = Shape::map, .element = &source};

constexpr std::array pairing_fields{Field{"validator", &validator, true}};
constexpr Node pairing{.shape = Shape::record, .fields = pairing_fields, .basis = true};

constexpr std::array reasoning_fields{
    Field{"at_cut", &at_cut, true}, Field{"at_rebind", &at_rebind, true},
    Field{"side_request_reasoning", &side_reasoning, true},
    Field{"context", &reasoning_context}};
constexpr Node reasoning{
    .shape = Shape::record, .fields = reasoning_fields, .basis = true};

constexpr std::array boundary_fields{Field{"form", &boundary_form, true},
                                     Field{"enabled", &boolean, true}};
constexpr Node boundary{
    .shape = Shape::record, .fields = boundary_fields, .basis = true};
constexpr Node open_turn_list{.shape = Shape::list,
                              .element = &open_turn_mode,
                              .unique = true,
                              .non_empty = true};
constexpr std::array turn_fields{
    Field{"start", &turn_start, true}, Field{"close", &turn_close, true},
    Field{"open_turn", &open_turn_list, true}, Field{"boundary", &boundary, true}};
constexpr Node turn{.shape = Shape::record, .fields = turn_fields, .basis = true};

constexpr std::array request_state_fields{Field{"placement", &state_placement, true}};
constexpr Node request_state{
    .shape = Shape::record, .fields = request_state_fields, .basis = true};
constexpr std::array note_fields{Field{"role", &note_role, true},
                                 Field{"placement", &note_placement, true}};
constexpr Node note{.shape = Shape::record, .fields = note_fields, .basis = true};
constexpr std::array restriction_fields{Field{"mode", &restriction_mode, true}};
constexpr Node tool_restriction{
    .shape = Shape::record, .fields = restriction_fields, .basis = true};
constexpr std::array tool_change_fields{Field{"mode", &tool_change_mode, true}};
constexpr Node tool_change{
    .shape = Shape::record, .fields = tool_change_fields, .basis = true};
constexpr std::array archival_fields{Field{"mode", &archival_mode, true}};
constexpr Node archival{
    .shape = Shape::record, .fields = archival_fields, .basis = true};
constexpr std::array edit_fields{Field{"in_place", &edit_mode, true}};
constexpr Node edit_acceptance{
    .shape = Shape::record, .fields = edit_fields, .basis = true};

constexpr std::array affinity_fields{Field{"body_field", &identifier},
                                     Field{"header", &header_name}};
constexpr Node affinity{
    .shape = Shape::record, .fields = affinity_fields, .basis = true};
constexpr Node breakpoint_list{
    .shape = Shape::list, .element = &breakpoint, .unique = true};
constexpr std::array cache_fields{Field{"capability", &cache_capability, true},
                                  Field{"ttl", &cache_ttl},
                                  Field{"breakpoints", &breakpoint_list},
                                  Field{"minimum_tokens", &minimum_count},
                                  Field{"read_multiplier", &multiplier},
                                  Field{"write_multiplier", &multiplier},
                                  Field{"affinity", &affinity}};
constexpr Node cache{.shape = Shape::record, .fields = cache_fields, .basis = true};

constexpr Node extract{.shape = Shape::map, .element = &matcher_text};
constexpr std::array error_matchers{"status"sv, "event"sv, "error_type"sv, "code"sv,
                                    "contains"sv};
constexpr std::array error_fields{
    Field{"class", &error_class, true}, Field{"status", &http_status},
    Field{"event", &matcher_text},      Field{"error_type", &matcher_text},
    Field{"code", &matcher_text},       Field{"contains", &matcher_text},
    Field{"extract", &extract}};
constexpr Node error_rule{.shape = Shape::record,
                          .fields = error_fields,
                          .basis = true,
                          .one_of = error_matchers};
constexpr Node error_list{.shape = Shape::list, .element = &error_rule};
constexpr Node stop_reasons{.shape = Shape::map, .element = &stop_class, .basis = true};

constexpr Node status_list{
    .shape = Shape::list, .element = &http_status, .unique = true};
constexpr Node transport_list{
    .shape = Shape::list, .element = &transport_code, .unique = true};
constexpr std::array retry_fields{
    Field{"http", &status_list, true}, Field{"transport", &transport_list, true},
    Field{"rate_limit_requires_retry_after", &boolean, true}};
constexpr Node retry{.shape = Shape::record, .fields = retry_fields, .basis = true};
constexpr Node effort_list{
    .shape = Shape::list, .element = &effort_level, .unique = true, .non_empty = true};
constexpr std::array effort_fields{Field{"values", &effort_list, true},
                                   Field{"thinking", &thinking_mode}};
constexpr Node effort{.shape = Shape::record, .fields = effort_fields, .basis = true};
constexpr std::array output_fields{Field{"max_tokens", &token_count, true}};
constexpr Node output{.shape = Shape::record, .fields = output_fields, .basis = true};

#define BLACKBIRD_FAMILIES                                                             \
  Field{"pairing", &pairing, true}, Field{"reasoning", &reasoning, true},              \
      Field{"turn", &turn, true}, Field{"request_state", &request_state, true},        \
      Field{"note", &note, true}, Field{"tool_restriction", &tool_restriction, true},  \
      Field{"tool_change", &tool_change, true}, Field{"archival", &archival, true},    \
      Field{"edit_acceptance", &edit_acceptance, true}, Field{"cache", &cache, true},  \
      Field{"errors", &error_list}, Field{"stop_reasons", &stop_reasons},              \
      Field{"retry", &retry, true}, Field{"effort", &effort}, Field {                  \
    "output", &output                                                                  \
  }
constexpr std::array shipped_fields{Field{"schema", &free_text, true},
                                    Field{"id", &identifier, true},
                                    Field{"revision", &revision_number, true},
                                    Field{"match", &match, true},
                                    Field{"sources", &sources},
                                    BLACKBIRD_FAMILIES};
constexpr std::array override_fields{Field{"schema", &free_text, true},
                                     Field{"id", &identifier, true},
                                     Field{"revision", &revision_number, true},
                                     Field{"target", &identifier, true},
                                     Field{"sources", &sources},
                                     BLACKBIRD_FAMILIES};
#undef BLACKBIRD_FAMILIES
constexpr Node shipped_root{.shape = Shape::record, .fields = shipped_fields};
constexpr Node override_root{.shape = Shape::record, .fields = override_fields};

} // namespace node

bool behavioural(std::string_view name) {
  return name != "schema" && name != "id" && name != "revision" && name != "match" &&
         name != "sources" && name != "target";
}
bool identity(std::string_view name) {
  return name == "schema" || name == "id" || name == "revision" || name == "target";
}

struct Invalid {
  std::string subject;
  std::string reason;
};
[[noreturn]] void invalid(std::string subject, std::string reason) {
  throw Invalid{std::move(subject), std::move(reason)};
}
std::string join(const std::string &path, std::string_view key) {
  return path.empty() ? std::string{key} : path + "." + std::string{key};
}
std::string indexed(const std::string &path, std::size_t index) {
  return path + "[" + std::to_string(index + 1) + "]";
}
const Field *find_field(const Node &node, std::string_view name) {
  for (const auto &field : node.fields)
    if (field.name == name)
      return &field;
  return nullptr;
}
const Value *lookup(const Value::Object &fields, std::string_view key) {
  for (const auto &[name, value] : fields)
    if (name == key)
      return &value;
  return nullptr;
}
// A sorted index of an object's keys: O(log n) lookups where a map can be large.
class Keys {
public:
  explicit Keys(const Value::Object *fields) : fields_(fields) {
    if (!fields_)
      return;
    order_.resize(fields_->size());
    for (std::size_t i = 0; i < order_.size(); ++i)
      order_[i] = i;
    std::ranges::sort(order_, {}, [&](std::size_t i) -> const std::string & {
      return (*fields_)[i].first;
    });
  }
  // Position of `key` in the object, if present.
  std::optional<std::size_t> position(std::string_view key) const {
    if (!fields_)
      return std::nullopt;
    const auto it = std::ranges::lower_bound(
        order_, key, {},
        [&](std::size_t i) -> std::string_view { return (*fields_)[i].first; });
    if (it == order_.end() || (*fields_)[*it].first != key)
      return std::nullopt;
    return *it;
  }
  const Value *find(std::string_view key) const {
    const auto at = position(key);
    return at ? &(*fields_)[*at].second : nullptr;
  }

private:
  const Value::Object *fields_;
  std::vector<std::size_t> order_;
};
const Value::Object *object_of(const Value *v) {
  return v ? std::get_if<Value::Object>(&v->value()) : nullptr;
}
// Values from outside the reader (a session override) may repeat a key; never ignore
// one. Sorting the keys keeps this O(n log n).
void unique_keys(const Value::Object &fields, const std::string &path) {
  std::vector<std::string_view> keys;
  keys.reserve(fields.size());
  for (const auto &entry : fields)
    keys.emplace_back(entry.first);
  std::ranges::sort(keys);
  if (const auto twice = std::ranges::adjacent_find(keys); twice != keys.end())
    invalid(join(path, *twice), "duplicate field");
}
bool printable(std::string_view text) {
  return valid_utf8(text) && std::ranges::none_of(text, [](char c) {
           const auto b = static_cast<unsigned char>(c);
           return b < 0x20 || b == 0x7f;
         });
}
bool is_identifier(std::string_view s) {
  if (s.empty() || s.size() > 64 || s[0] < 'a' || s[0] > 'z')
    return false;
  return std::ranges::all_of(s, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}
bool is_link(std::string_view s) {
  return s.starts_with("https://") && s.size() > 8 && s.size() <= 512 && printable(s) &&
         s.find_first_of(" \t") == std::string_view::npos;
}
char lowered(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}
// `https://host[:port][/path]` in normal form: scheme and host lowercase, port 443
// dropped, trailing `/` dropped. `fault` gets the exact reason on refusal.
std::optional<std::string> normal_url(std::string_view url, std::string &fault) {
  const auto quoted = "`" + std::string{url} + "`";
  constexpr std::string_view scheme = "https://";
  if (url.size() < scheme.size() ||
      !std::ranges::equal(url.substr(0, scheme.size()), scheme,
                          [](char a, char b) { return lowered(a) == b; })) {
    fault = quoted + " is not an https URL";
    return std::nullopt;
  }
  fault = quoted + " is not a well-formed https URL";
  if (url.size() > 512 || !printable(url))
    return std::nullopt;
  const auto rest = url.substr(scheme.size());
  const auto slash = rest.find('/');
  const auto authority = rest.substr(0, slash);
  auto path = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);
  const auto colon = authority.find(':');
  const auto host = authority.substr(0, colon);
  if (host.empty() || host.front() == '.' || host.front() == '-' ||
      host.back() == '.' || host.back() == '-' ||
      !std::ranges::all_of(host, [](char c) {
        const char l = lowered(c);
        return (l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '.' || l == '-';
      }))
    return std::nullopt;
  std::string out{scheme};
  for (const char c : host)
    out.push_back(lowered(c));
  if (colon != std::string_view::npos) {
    const auto port = authority.substr(colon + 1);
    unsigned number = 0;
    const auto r = std::from_chars(port.data(), port.data() + port.size(), number);
    if (port.empty() || port.size() > 5 || r.ec != std::errc{} ||
        r.ptr != port.data() + port.size() || number == 0 || number > 65535)
      return std::nullopt;
    if (number != 443)
      out += ":" + std::to_string(number);
  }
  if (path.find_first_of("?# \t") != std::string_view::npos)
    return std::nullopt;
  while (path.ends_with('/'))
    path.remove_suffix(1);
  out += path;
  return out;
}
// `papers/x.md`: relative, `/`-separated, no empty, `.` or `..` component, printable.
bool is_repo_path(std::string_view s) {
  if (s.empty() || s.size() > 512 || !printable(s) || s.front() == '/' ||
      s.find('\\') != std::string_view::npos)
    return false;
  for (std::size_t at = 0; at <= s.size();) {
    const auto end = std::min(s.find('/', at), s.size());
    const auto part = s.substr(at, end - at);
    if (part.empty() || part == "." || part == "..")
      return false;
    at = end + 1;
  }
  return true;
}
bool is_normal_url(std::string_view s) {
  std::string fault;
  const auto normal = normal_url(s, fault);
  return normal && *normal == s;
}
bool is_date(std::string_view s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-')
    return false;
  for (const std::size_t i : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U})
    if (s[i] < '0' || s[i] > '9')
      return false;
  const auto number = [&](std::size_t at, std::size_t count) {
    int n = 0;
    for (std::size_t i = at; i < at + count; ++i)
      n = n * 10 + (s[i] - '0');
    return n;
  };
  const int year = number(0, 4), month = number(5, 2), day = number(8, 2);
  constexpr std::array<int, 12> days{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12 || day < 1)
    return false;
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return day <=
         days.at(static_cast<std::size_t>(month - 1)) + (month == 2 && leap ? 1 : 0);
}
bool is_model(std::string_view s) {
  return !s.empty() && s.size() <= 256 && printable(s) &&
         s.find(' ') == std::string_view::npos;
}
bool is_header(std::string_view s) {
  return !s.empty() && s.size() <= 128 && std::ranges::all_of(s, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_';
  });
}
std::string listed(std::span<const std::string_view> values) {
  std::string out;
  for (const auto value : values)
    out += (out.empty() ? "" : ", ") + std::string{value};
  return out;
}
// "a, b or c"
std::string alternatives(std::span<const std::string_view> names) {
  std::string out;
  for (std::size_t i = 0; i < names.size(); ++i)
    out += (i == 0                  ? ""
            : i + 1 == names.size() ? " or "
                                    : ", ") +
           std::string{names[i]};
  return out;
}
std::string shown(const Value &v) {
  if (const auto *s = std::get_if<std::string>(&v.value()))
    return *s;
  if (const auto *n = std::get_if<Number>(&v.value()))
    return n->text();
  auto text = format_value(v);
  return text.has_value() ? text.value() : "?";
}
// `kind[:detail]`; the reason names the exact fault.
void check_basis(const std::string &path, const Value &v) {
  const auto *text = std::get_if<std::string>(&v.value());
  if (!text)
    invalid(path, "must be a basis string or a table of field = basis");
  const auto colon = text->find(':');
  const auto kind = std::string_view{*text}.substr(0, colon);
  if (kind != "cited" && kind != "probed" && kind != "conservative" &&
      kind != "unobserved")
    invalid(path, "`" + *text +
                      "` is not a basis: expected cited, probed, conservative or "
                      "unobserved, optionally followed by `:detail`");
  if (colon == std::string::npos) {
    if (kind == "cited")
      invalid(path, "`cited` needs a detail: `cited:<source>`");
    if (kind == "probed")
      invalid(path, "`probed` needs a detail: `probed:<probe>`");
    return;
  }
  const auto detail = std::string_view{*text}.substr(colon + 1);
  if (detail.empty())
    invalid(path, "`" + *text + "` has an empty detail");
  if (detail.size() > 512 || !printable(detail))
    invalid(path, "`" + *text + "` detail must be printable text of at most 512 bytes");
  if (kind == "cited" && !is_identifier(detail))
    invalid(path, "`" + *text + "` must name a source identifier");
}

Value leaf(const Node &node, const Value &v, const std::string &path);
Value list(const Node &node, const Value &v, const std::string &path);
// The basis a parent offers a nested table, and whether that table used it.
struct Inherited {
  std::optional<std::string> basis;
  bool used = false;
};
Value record(const Node &node, const Value &v, const std::string &path,
             Inherited &inherited, bool whole);
Value map(const Node &node, const Value &v, const std::string &path,
          Inherited &inherited);

bool covered_as_leaf(const Node &node) {
  if (node.shape == Shape::record)
    return false;
  if (node.shape == Shape::map)
    return !node.basis;
  if (node.shape == Shape::list)
    return node.element->shape != Shape::record;
  return true;
}

Value leaf(const Node &node, const Value &v, const std::string &path) {
  switch (node.shape) {
  case Shape::text: {
    const auto *s = std::get_if<std::string>(&v.value());
    if (!s)
      invalid(path, "must be a string");
    if (node.non_empty && s->empty())
      invalid(path, "must not be empty");
    if (!node.options.empty() &&
        std::ranges::find(node.options, *s) == node.options.end())
      invalid(path, "`" + *s + "` is not one of " + listed(node.options));
    switch (node.form) {
    case Form::any:
      if (!valid_utf8(*s) || std::ranges::any_of(*s, [](char c) {
            const auto b = static_cast<unsigned char>(c);
            return (b < 0x20 && c != '\n' && c != '\t') || b == 0x7f;
          }))
        invalid(path, "must be printable text");
      break;
    case Form::identifier:
      if (!is_identifier(*s))
        invalid(path, "must be an identifier: lowercase letters, digits, `-` and `_`, "
                      "starting with a letter");
      break;
    case Form::url:
      if (!is_normal_url(*s))
        invalid(path, "must be a normalized https URL: lowercase scheme and host, no "
                      "default port, no trailing `/`");
      break;
    case Form::repo_path:
      if (!is_repo_path(*s))
        invalid(path,
                "must be a relative repository path: no leading `/`, no `.` or `..` "
                "component, no control characters");
      break;
    case Form::link:
      if (!is_link(*s))
        invalid(path, "must be an https URL");
      break;
    case Form::date:
      if (!is_date(*s))
        invalid(path, "must be a date YYYY-MM-DD");
      break;
    case Form::model:
      if (!is_model(*s))
        invalid(path, "must be a model name without spaces");
      break;
    case Form::header:
      if (!is_header(*s))
        invalid(path, "must be a header name of letters, digits, `-` and `_`");
      break;
    }
    return v;
  }
  case Shape::integer: {
    const auto *n = std::get_if<Number>(&v.value());
    bool ok = false;
    if (n) {
      if (const auto *i = std::get_if<std::int64_t>(&n->storage()))
        ok = *i >= node.min && *i <= node.max;
      else if (const auto *u = std::get_if<std::uint64_t>(&n->storage()))
        ok = *u >= static_cast<std::uint64_t>(node.min) &&
             *u <= static_cast<std::uint64_t>(node.max);
    }
    if (!ok)
      invalid(path, "must be an integer from " + std::to_string(node.min) + " to " +
                        std::to_string(node.max));
    return v;
  }
  case Shape::number: {
    const auto *n = std::get_if<Number>(&v.value());
    const double d = n ? std::strtod(n->text().c_str(), nullptr) : -1.0;
    if (!n ||
        !(d >= static_cast<double>(node.min) && d <= static_cast<double>(node.max)))
      invalid(path, "must be a number from " + std::to_string(node.min) + " to " +
                        std::to_string(node.max));
    return v;
  }
  case Shape::boolean:
    if (!std::holds_alternative<bool>(v.value()))
      invalid(path, "must be a boolean");
    return v;
  case Shape::list:
    return list(node, v, path);
  case Shape::record:
  case Shape::map:
    break;
  }
  invalid(path, "schema node is not a leaf");
}
// `{}` reads as an empty object; where the schema wants a list it is an empty list.
Value list(const Node &node, const Value &v, const std::string &path) {
  const auto *items = std::get_if<Value::Array>(&v.value());
  const auto *fields = std::get_if<Value::Object>(&v.value());
  if (!items && !(fields && fields->empty()))
    invalid(path, "must be a list");
  Value::Array out;
  std::set<std::string> seen; // scalar lists are homogeneous: the text is the value
  if (items) {
    for (std::size_t i = 0; i < items->size(); ++i) {
      const auto &item = (*items)[i];
      if (node.element->shape == Shape::record) {
        Inherited none;
        out.push_back(record(*node.element, item, indexed(path, i), none, true));
      } else {
        out.push_back(leaf(*node.element, item, indexed(path, i)));
      }
      if (node.unique && !seen.insert(shown(out.back())).second)
        invalid(path, "lists `" + shown(item) + "` twice");
    }
  }
  if (node.non_empty && out.empty())
    invalid(path, "must not be empty");
  return Value{std::move(out)};
}
// A table's own basis: a string for every field, or field -> basis entries. Every
// basis must cover at least one field the layer sets, so none is silently dropped.
struct Basis {
  std::optional<std::string> own;
  bool own_used = false;
  Value::Object table;
  std::vector<bool> table_used;
};
Basis read_basis(const Node &node, const Value::Object &fields,
                 const std::string &path) {
  Basis basis;
  const auto *b = lookup(fields, "basis");
  if (!b)
    return basis;
  const auto where = join(path, "basis");
  if (!node.basis)
    invalid(where, "unknown field");
  if (std::holds_alternative<std::string>(b->value())) {
    check_basis(where, *b);
    basis.own = b->string();
    return basis;
  }
  const auto *table = std::get_if<Value::Object>(&b->value());
  if (!table)
    invalid(where, "must be a basis string or a table of field = basis");
  unique_keys(*table, where);
  const Keys present{&fields};
  for (const auto &[key, value] : *table) {
    if (node.shape == Shape::record && !find_field(node, key))
      invalid(join(where, key), "names no field of " + path);
    check_basis(join(where, key), value);
    if (!present.find(key))
      invalid(join(where, key), "names a field this layer does not set");
    basis.table.emplace_back(key, value);
  }
  basis.table_used.assign(basis.table.size(), false);
  return basis;
}
// Which basis covers `key`: a table entry, the table's own string, or the parent's.
struct Cover {
  std::optional<std::string> basis;
  enum class From : std::uint8_t { none, table, own, inherited } from = From::none;
  std::size_t index = 0;
};
Cover cover(const Basis &basis, const Keys &table, const Inherited &inherited,
            std::string_view key) {
  if (const auto i = table.position(key))
    return {basis.table[*i].second.string(), Cover::From::table, *i};
  if (basis.own)
    return {basis.own, Cover::From::own, 0};
  if (inherited.basis)
    return {inherited.basis, Cover::From::inherited, 0};
  return {};
}
void mark(Basis &basis, Inherited &inherited, const Cover &c) {
  if (c.from == Cover::From::table)
    basis.table_used[c.index] = true;
  else if (c.from == Cover::From::own)
    basis.own_used = true;
  else if (c.from == Cover::From::inherited)
    inherited.used = true;
}
void all_used(const Basis &basis, const std::string &path) {
  const auto where = join(path, "basis");
  if (basis.own && !basis.own_used)
    invalid(where, "covers no field this layer sets");
  for (std::size_t i = 0; i < basis.table.size(); ++i)
    if (!basis.table_used[i])
      invalid(join(where, basis.table[i].first), "covers no field this layer sets");
}
Value record(const Node &node, const Value &v, const std::string &path,
             Inherited &inherited, bool whole) {
  const auto *fields = std::get_if<Value::Object>(&v.value());
  if (!fields)
    invalid(path.empty() ? "profile" : path, "must be a table of named fields");
  unique_keys(*fields, path);
  auto basis = read_basis(node, *fields, path);
  const Keys table{&basis.table};
  for (const auto &entry : *fields)
    if (entry.first != "basis" && !find_field(node, entry.first))
      invalid(join(path, entry.first), "unknown field");
  Value::Object out;
  Value::Object recorded;
  for (const auto &field : node.fields) {
    const auto *x = lookup(*fields, field.name);
    const auto where = join(path, field.name);
    if (!x) {
      if (whole && field.required)
        invalid(where, "required");
      continue;
    }
    const auto covered = cover(basis, table, inherited, field.name);
    const auto &child = *field.node;
    if (child.shape == Shape::record || (child.shape == Shape::map && child.basis)) {
      Inherited passed{covered.basis};
      out.emplace_back(field.name, child.shape == Shape::record
                                       ? record(child, *x, where, passed, whole)
                                       : map(child, *x, where, passed));
      if (passed.used)
        mark(basis, inherited, covered);
    } else {
      if (child.shape == Shape::map) {
        Inherited none;
        out.emplace_back(field.name, map(child, *x, where, none));
      } else if (child.shape == Shape::list && child.element->shape == Shape::record) {
        out.emplace_back(field.name, list(child, *x, where));
      } else {
        out.emplace_back(field.name, leaf(child, *x, where));
      }
      if (covered_as_leaf(child) && covered.basis) {
        recorded.emplace_back(field.name, Value{*covered.basis});
        mark(basis, inherited, covered);
      }
    }
  }
  all_used(basis, path);
  if (!node.one_of.empty() && std::ranges::none_of(node.one_of, [&](auto name) {
        return lookup(*fields, name);
      }))
    invalid(path, "needs at least one of " + alternatives(node.one_of));
  if (node.exclusive && std::ranges::count_if(node.one_of, [&](auto name) {
                          return lookup(*fields, name);
                        }) > 1)
    invalid(path, "set " + alternatives(node.one_of) + ", not both");
  if (!recorded.empty())
    out.emplace_back("basis", Value{std::move(recorded)});
  return Value{std::move(out)};
}
Value map(const Node &node, const Value &v, const std::string &path,
          Inherited &inherited) {
  const auto *fields = std::get_if<Value::Object>(&v.value());
  if (!fields)
    invalid(path, "must be a table of named fields");
  unique_keys(*fields, path);
  auto basis = node.basis ? read_basis(node, *fields, path) : Basis{};
  const Keys table{&basis.table};
  Value::Object out;
  Value::Object recorded;
  for (const auto &[key, x] : *fields) {
    if (node.basis && key == "basis")
      continue;
    const auto where = join(path, key);
    if (!is_identifier(key))
      invalid(where, "must be an identifier: lowercase letters, digits, `-` and `_`, "
                     "starting with a letter");
    if (node.element->shape == Shape::record) {
      Inherited none;
      out.emplace_back(key, record(*node.element, x, where, none, true));
    } else {
      out.emplace_back(key, leaf(*node.element, x, where));
      if (const auto covered = cover(basis, table, inherited, key);
          node.basis && covered.basis) {
        recorded.emplace_back(key, Value{*covered.basis});
        mark(basis, inherited, covered);
      }
    }
  }
  if (node.basis)
    all_used(basis, path);
  if (!recorded.empty())
    out.emplace_back("basis", Value{std::move(recorded)});
  return Value{std::move(out)};
}

const Value *path_value(const Value &v, std::initializer_list<std::string_view> keys) {
  const Value *cursor = &v;
  for (const auto key : keys) {
    cursor = cursor->find(key);
    if (!cursor)
      return nullptr;
  }
  return cursor;
}
std::string text_or(const Value &v, std::initializer_list<std::string_view> keys) {
  const auto *x = path_value(v, keys);
  if (!x)
    return {};
  const auto *s = std::get_if<std::string>(&x->value());
  return s ? *s : std::string{};
}
// Codec consistency and contradictory pairs, on a complete profile.
void cross_field(const Value &p) {
  const auto protocol = text_or(p, {"match", "protocol"});
  const auto requires_protocol = [&](const std::string &subject,
                                     const std::string &value,
                                     std::string_view wanted) {
    if (protocol != wanted)
      invalid(subject,
              "`" + value + "` requires match.protocol `" + std::string{wanted} + "`");
  };
  const auto at_cut_value = text_or(p, {"reasoning", "at_cut"});
  if (const auto *modes = path_value(p, {"turn", "open_turn"});
      modes && at_cut_value == "drop_after_change")
    for (const auto &mode : modes->array())
      if (mode == Value{"retain_current_turn"})
        invalid(
            "turn.open_turn",
            "`retain_current_turn` contradicts reasoning.at_cut `drop_after_change`");
  const auto form = text_or(p, {"turn", "boundary", "form"});
  if (form == "reply_then_note" && text_or(p, {"turn", "close"}) != "reply_request")
    invalid("turn.boundary.form",
            "`reply_then_note` requires turn.close `reply_request`");
  if (form == "request_local_user" &&
      text_or(p, {"request_state", "placement"}) != "request_local")
    invalid("turn.boundary.form",
            "`request_local_user` requires request_state.placement `request_local`");
  if (form == "system_message")
    requires_protocol("turn.boundary.form", form, "messages");
  const auto restriction = text_or(p, {"tool_restriction", "mode"});
  if (restriction == "allowed_tools")
    requires_protocol("tool_restriction.mode", restriction, "responses");
  if (restriction == "appended_removal")
    requires_protocol("tool_restriction.mode", restriction, "messages");
  if (const auto change = text_or(p, {"tool_change", "mode"});
      change == "appended_system_message")
    requires_protocol("tool_change.mode", change, "messages");
  const auto pairing_validator = text_or(p, {"pairing", "validator"});
  if (pairing_validator == "anthropic-messages")
    requires_protocol("pairing.validator", pairing_validator, "messages");
  if (pairing_validator == "responses" && protocol != "responses" &&
      protocol != "responses-portable")
    invalid("pairing.validator",
            "`responses` requires match.protocol `responses` or `responses-portable`");
  if (const auto role = text_or(p, {"note", "role"}); role == "developer_as_user_text")
    requires_protocol("note.role", role, "messages");
  // Cache detail only where the capability gives it meaning.
  const auto capability = text_or(p, {"cache", "capability"});
  if (const auto *c = p.find("cache")) {
    if (capability == "none")
      for (const auto name : {"ttl"sv, "breakpoints"sv, "minimum_tokens"sv,
                              "read_multiplier"sv, "write_multiplier"sv, "affinity"sv})
        if (c->find(name))
          invalid("cache." + std::string{name}, "contradicts cache.capability `none`");
    if (capability == "implicit" && c->find("breakpoints"))
      invalid("cache.breakpoints", "requires cache.capability `explicit`");
  }
  if (const auto context = text_or(p, {"reasoning", "context"});
      !context.empty() && context != "default")
    requires_protocol("reasoning.context", context, "responses");
}
// Every `cited:<source>` basis names an entry in sources.
void cited_sources(const Node &node, const Value &v, const std::string &path,
                   const Keys &available) {
  const auto *fields = std::get_if<Value::Object>(&v.value());
  if (!fields)
    return;
  if (const auto *b = lookup(*fields, "basis"); b && node.basis)
    for (const auto &[key, value] : b->object()) {
      const auto &text = value.string();
      if (text.starts_with("cited:") &&
          !available.find(std::string_view{text}.substr(6)))
        invalid(join(join(path, "basis"), key),
                "`" + text + "` names no entry in sources");
    }
  if (node.shape != Shape::record)
    return;
  for (const auto &field : node.fields) {
    const auto *x = lookup(*fields, field.name);
    if (!x || (path.empty() && !behavioural(field.name)))
      continue;
    const auto &child = *field.node;
    if (child.shape == Shape::record || (child.shape == Shape::map && child.basis))
      cited_sources(child, *x, join(path, field.name), available);
    else if (child.shape == Shape::list && child.element->shape == Shape::record)
      for (std::size_t i = 0; i < x->array().size(); ++i)
        cited_sources(*child.element, x->array()[i], indexed(join(path, field.name), i),
                      available);
  }
}

ProfileError refused(std::string_view file, const Invalid &e) {
  return ProfileError{std::string{file}, 0, 0, e.subject, e.reason};
}

// The two sides of a merge, named so they cannot be swapped by position.
struct Sides {
  const Value &base;
  const Value &layer;
};
Value merge_map(const Node &node, Sides sides) {
  const auto &base = sides.base;
  const auto &layer = sides.layer;
  Value::Object out;
  Value::Object recorded;
  const auto *b = std::get_if<Value::Object>(&base.value());
  const auto &l = layer.object();
  const Keys in_layer{&l};
  if (b)
    for (const auto &[key, value] : *b) {
      if (key == "basis" && node.basis)
        continue;
      if (!in_layer.find(key))
        out.emplace_back(key, value);
    }
  for (const auto &[key, value] : l)
    if (!(key == "basis" && node.basis))
      out.emplace_back(key, value);
  if (node.basis) {
    if (const auto *bb = b ? lookup(*b, "basis") : nullptr)
      for (const auto &[key, value] : bb->object())
        if (!in_layer.find(key))
          recorded.emplace_back(key, value);
    if (const auto *lb = in_layer.find("basis"))
      for (const auto &entry : lb->object())
        recorded.push_back(entry);
    if (!recorded.empty())
      out.emplace_back("basis", Value{std::move(recorded)});
  }
  return Value{std::move(out)};
}
Value merge_record(const Node &node, Sides sides, bool skip_identity) {
  const auto &base = sides.base;
  const auto &layer = sides.layer;
  const auto *b = std::get_if<Value::Object>(&base.value());
  const auto &l = layer.object();
  Value::Object out;
  Value::Object recorded;
  for (const auto &field : node.fields) {
    const auto *from_base = b ? lookup(*b, field.name) : nullptr;
    const auto *from_layer =
        skip_identity && identity(field.name) ? nullptr : lookup(l, field.name);
    if (!from_base && !from_layer)
      continue;
    if (!from_layer) {
      out.emplace_back(field.name, *from_base);
      continue;
    }
    const auto &child = *field.node;
    const Value empty{Value::Object{}};
    if (child.shape == Shape::record)
      out.emplace_back(
          field.name,
          merge_record(child, {from_base ? *from_base : empty, *from_layer}, false));
    else if (child.shape == Shape::map)
      out.emplace_back(field.name,
                       merge_map(child, {from_base ? *from_base : empty, *from_layer}));
    else
      out.emplace_back(field.name, *from_layer);
  }
  // A leaf the layer sets takes the layer's basis, or none; others keep the base's.
  if (const auto *bb = b ? lookup(*b, "basis") : nullptr)
    for (const auto &[key, value] : bb->object())
      if (!lookup(l, key))
        recorded.emplace_back(key, value);
  if (const auto *lb = lookup(l, "basis"))
    for (const auto &entry : lb->object())
      recorded.push_back(entry);
  if (!recorded.empty())
    out.emplace_back("basis", Value{std::move(recorded)});
  return Value{std::move(out)};
}

void missing(const Node &node, const Value &v, const std::string &path,
             std::vector<std::string> &out) {
  const auto *fields = std::get_if<Value::Object>(&v.value());
  if (!fields)
    return;
  const auto *b = lookup(*fields, "basis");
  for (const auto &field : node.fields) {
    const auto *x = lookup(*fields, field.name);
    if (!x || (path.empty() && !behavioural(field.name)))
      continue;
    const auto where = join(path, field.name);
    const auto &child = *field.node;
    if (child.shape == Shape::record) {
      missing(child, *x, where, out);
    } else if (child.shape == Shape::list && child.element->shape == Shape::record) {
      if (const auto *items = std::get_if<Value::Array>(&x->value()))
        for (std::size_t i = 0; i < items->size(); ++i)
          missing(*child.element, (*items)[i], indexed(where, i), out);
    } else if (child.shape == Shape::map && child.basis) {
      const Keys covered{object_of(x->find("basis"))};
      if (const auto *entries = std::get_if<Value::Object>(&x->value()))
        for (const auto &entry : *entries)
          if (entry.first != "basis" && !covered.find(entry.first))
            out.push_back(join(where, entry.first));
    } else if (!(b && b->find(field.name))) {
      out.push_back(where);
    }
  }
}

Value project(const Node &node, const Value &v) {
  if (node.shape == Shape::record) {
    Value::Object out;
    for (const auto &field : node.fields)
      if (const auto *x = v.find(field.name))
        out.emplace_back(field.name, project(*field.node, *x));
    return Value{std::move(out)};
  }
  if (const auto *entries = std::get_if<Value::Object>(&v.value());
      entries && node.shape == Shape::map) {
    Value::Object out;
    for (const auto &[key, x] : *entries)
      if (!(node.basis && key == "basis"))
        out.emplace_back(key, project(*node.element, x));
    return Value{std::move(out)};
  }
  if (const auto *items = std::get_if<Value::Array>(&v.value());
      items && node.shape == Shape::list && node.element->shape == Shape::record) {
    Value::Array out;
    for (const auto &x : *items)
      out.push_back(project(*node.element, x));
    return Value{std::move(out)};
  }
  return v;
}
Value behaviour(const Value &profile) {
  Value::Object out;
  for (const auto &field : node::shipped_root.fields)
    if (const auto *x = profile.find(field.name);
        x && (field.name == "schema" || behavioural(field.name)))
      out.emplace_back(field.name, project(*field.node, *x));
  return Value{std::move(out)};
}
// Canonical bytes: type tag, little-endian 64-bit lengths, object keys sorted.
void put_length(std::string &out, std::size_t n) {
  for (unsigned i = 0; i < 8; ++i)
    out.push_back(
        static_cast<char>((static_cast<std::uint64_t>(n) >> (8U * i)) & 0xffU));
}
void canonical(const Value &v, std::string &out) {
  std::visit(
      [&](const auto &x) {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
          out.push_back('z');
        } else if constexpr (std::is_same_v<T, bool>) {
          out.push_back(x ? 't' : 'f');
        } else if constexpr (std::is_same_v<T, Number>) {
          const auto text = x.text();
          out.push_back('n');
          put_length(out, text.size());
          out += text;
        } else if constexpr (std::is_same_v<T, std::string>) {
          out.push_back('s');
          put_length(out, x.size());
          out += x;
        } else if constexpr (std::is_same_v<T, Value::Array>) {
          out.push_back('a');
          put_length(out, x.size());
          for (const auto &item : x)
            canonical(item, out);
        } else {
          std::vector<const Value::Object::value_type *> sorted;
          for (const auto &entry : x)
            sorted.push_back(&entry);
          std::ranges::sort(sorted, {}, [](const auto *e) -> const std::string & {
            return e->first;
          });
          out.push_back('o');
          put_length(out, x.size());
          for (const auto *entry : sorted) {
            put_length(out, entry->first.size());
            out += entry->first;
            canonical(entry->second, out);
          }
        }
      },
      v.value());
}

// FIPS 180-4 SHA-256.
constexpr std::array<std::uint32_t, 64> sha_k{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};
constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) {
  return (x >> n) | (x << (32U - n));
}
void sha_block(std::array<std::uint32_t, 8> &h, const unsigned char *block) {
  std::array<std::uint32_t, 64> w{};
  for (std::size_t i = 0; i < 16; ++i)
    w.at(i) = (std::uint32_t{block[4 * i]} << 24U) |
              (std::uint32_t{block[(4 * i) + 1]} << 16U) |
              (std::uint32_t{block[(4 * i) + 2]} << 8U) |
              std::uint32_t{block[(4 * i) + 3]};
  for (std::size_t i = 16; i < 64; ++i) {
    const auto s0 =
        rotr(w.at(i - 15), 7) ^ rotr(w.at(i - 15), 18) ^ (w.at(i - 15) >> 3U);
    const auto s1 =
        rotr(w.at(i - 2), 17) ^ rotr(w.at(i - 2), 19) ^ (w.at(i - 2) >> 10U);
    w.at(i) = w.at(i - 16) + s0 + w.at(i - 7) + s1;
  }
  auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
  for (std::size_t i = 0; i < 64; ++i) {
    const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const auto choose = (e & f) ^ (~e & g);
    const auto t1 = k + s1 + choose + sha_k.at(i) + w.at(i);
    const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const auto majority = (a & b) ^ (a & c) ^ (b & c);
    const auto t2 = s0 + majority;
    k = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
  h[5] += f;
  h[6] += g;
  h[7] += k;
}
std::string digest_text(std::string_view domain, const Value &v) {
  std::string bytes{domain};
  bytes.push_back('\n');
  canonical(v, bytes);
  return "sha256:" + sha256_hex(bytes);
}

std::uint64_t revision_of(const Value &layer) {
  const auto *n = layer.find("revision");
  if (!n)
    return 0;
  if (const auto *u = std::get_if<std::uint64_t>(&n->number().storage()))
    return *u;
  if (const auto *i = std::get_if<std::int64_t>(&n->number().storage()))
    return static_cast<std::uint64_t>(*i);
  return 0;
}
std::string label(std::string_view layer_name, const Value &layer) {
  return std::string{layer_name} + ":" + layer.find("id")->string() + "/" +
         std::to_string(revision_of(layer));
}
struct Specificity {
  std::size_t dimensions = 0;
  int model_rank = 0;
  std::size_t prefix = 0;
  auto operator<=>(const Specificity &) const = default;
};
std::optional<Specificity> matches(const Value &layer, const ProfileQuery &q) {
  const auto *m = layer.find("match");
  if (!m)
    return std::nullopt;
  Specificity s;
  const std::array<std::pair<std::string_view, const std::string *>, 4> scalar{
      {{"provider", &q.provider},
       {"protocol", &q.protocol},
       {"base_url", &q.base_url},
       {"account_kind", &q.account_kind}}};
  for (const auto &[key, wanted] : scalar)
    if (const auto *x = m->find(key)) {
      if (x->string() != *wanted)
        return std::nullopt;
      ++s.dimensions;
    }
  if (const auto *models = m->find("models")) {
    // A name F is a family, as src/anthropic_provider.cpp:486-492 identifies one by its
    // name: the model F itself (exact), or F, `-` and any suffix such as a snapshot
    // date, ranked like a prefix of F.size() + 1. The `-` keeps `claude-opus-5-50` out
    // of the `claude-opus-5-5` family.
    for (const auto &x : models->array()) {
      const auto &family = x.string();
      if (q.model == family) {
        s.model_rank = 2;
      } else if (q.model.size() > family.size() && q.model.starts_with(family) &&
                 q.model[family.size()] == '-' && s.model_rank < 2) {
        s.model_rank = 1;
        s.prefix = std::max(s.prefix, family.size() + 1);
      }
    }
    if (s.model_rank == 0)
      return std::nullopt;
    ++s.dimensions;
  } else if (const auto *prefixes = m->find("model_prefixes")) {
    for (const auto &x : prefixes->array())
      if (q.model.starts_with(x.string()))
        s.prefix = std::max(s.prefix, x.string().size());
    if (s.prefix == 0)
      return std::nullopt;
    ++s.dimensions;
    s.model_rank = 1;
  }
  return s;
}
const ProfileFile *override_for(const ProfileSet &set, std::string_view target) {
  for (const auto &file : set.operator_overrides)
    if (file.layer.find("target")->string() == target)
      return &file;
  return nullptr;
}
// Every `*.lua` entry must be a readable regular file; a bad entry is named itself.
ProfileResult<std::vector<std::filesystem::path>>
lua_files(const std::filesystem::path &directory) {
  std::vector<std::filesystem::path> files;
  std::error_code error;
  std::filesystem::directory_iterator it{directory, error};
  for (; !error && it != std::filesystem::directory_iterator{}; it.increment(error)) {
    if (it->path().extension() != ".lua")
      continue;
    std::error_code entry_error;
    const auto status = std::filesystem::status(it->path(), entry_error);
    if (entry_error || status.type() != std::filesystem::file_type::regular)
      return std::unexpected(ProfileError{it->path().string(), 0, 0, "io",
                                          "the entry is a dangling symbolic link or "
                                          "unreadable"});
    files.push_back(it->path());
  }
  if (error)
    return std::unexpected(ProfileError{directory.string(), 0, 0, "io",
                                        "profile directory is missing or unreadable"});
  std::ranges::sort(files, {}, [](const auto &p) { return p.filename().string(); });
  return files;
}
ProfileResult<std::vector<ProfileFile>> read_directory(const std::filesystem::path &dir,
                                                       ProfileLayerKind kind) {
  std::error_code error;
  if (!std::filesystem::is_directory(dir, error))
    return std::unexpected(ProfileError{dir.string(), 0, 0, "io",
                                        "profile directory is missing or unreadable"});
  const auto files = lua_files(dir);
  if (!files.has_value())
    return std::unexpected(files.error());
  std::vector<ProfileFile> out;
  for (const auto &path : files.value()) {
    auto read = read_profile_file(path);
    if (!read.has_value())
      return std::unexpected(read.error());
    const auto file = path.string();
    auto layer = validate_profile({read.value(), kind, file});
    if (!layer.has_value())
      return std::unexpected(layer.error());
    const auto &id = layer.value().find("id")->string();
    if (id != path.stem().string())
      return std::unexpected(
          ProfileError{file, 0, 0, "id", "`" + id + "` does not match the file name"});
    out.push_back({file, std::move(layer).value()});
  }
  return out;
}
} // namespace

ProfileResult<Value> validate_profile(ProfileLayerInput input) {
  const bool shipped = input.kind == ProfileLayerKind::shipped;
  try {
    const auto *fields = std::get_if<Value::Object>(&input.layer.value());
    if (!fields)
      invalid("profile", "must be a table of named fields");
    const auto expected =
        shipped ? provider_profile_schema : provider_profile_override_schema;
    const auto *schema = lookup(*fields, "schema");
    if (!schema)
      invalid("schema", "required");
    if (*schema != Value{std::string{expected}})
      invalid("schema", "must be `" + std::string{expected} + "`");
    if (!shipped && lookup(*fields, "match"))
      invalid("match", "an override cannot change match");
    Inherited none;
    auto out = record(shipped ? node::shipped_root : node::override_root, input.layer,
                      "", none, false);
    for (const auto name : {"id"sv, "revision"sv, shipped ? "match"sv : "target"sv})
      if (!lookup(*fields, name))
        invalid(std::string{name}, "required");
    if (shipped) {
      const auto &m = out.find("match")->object();
      const bool conservative = out.find("id")->string() == conservative_profile_id;
      if (conservative && !m.empty())
        invalid("match", "the conservative profile must match everything: leave match "
                         "empty");
      if (!conservative && m.empty())
        invalid("match", "only the conservative profile may have an empty match");
      if (lookup(m, "models") && lookup(m, "model_prefixes"))
        invalid("match", "set models or model_prefixes, not both");
    }
    return out;
  } catch (const Invalid &e) {
    return std::unexpected(refused(input.file, e));
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        ProfileError{std::string{input.file}, 0, 0, "profile", "out of memory"});
  }
}

ProfileResult<void> validate_effective(const Value &profile, std::string_view file) {
  try {
    Inherited none;
    const auto normalized = record(node::shipped_root, profile, "", none, true);
    cross_field(normalized);
    const Keys sources{object_of(normalized.find("sources"))};
    cited_sources(node::shipped_root, normalized, "", sources);
    return {};
  } catch (const Invalid &e) {
    return std::unexpected(refused(file, e));
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        ProfileError{std::string{file}, 0, 0, "profile", "out of memory"});
  }
}

Value merge_override(const Value &base, const Value &layer) {
  const auto *schema = layer.find("schema");
  const bool override_layer =
      schema && *schema == Value{std::string{provider_profile_override_schema}};
  return merge_record(node::shipped_root, {base, layer}, override_layer);
}

std::vector<std::string> missing_basis(const Value &profile) {
  std::vector<std::string> out;
  missing(node::shipped_root, profile, "", out);
  return out;
}

std::string profile_digest(const Value &profile) {
  return digest_text("blackbird.provider-profile-digest/1", behaviour(profile));
}

std::string sha256_hex(std::string_view bytes) {
  std::array<std::uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const auto *data = reinterpret_cast<const unsigned char *>(bytes.data());
  const std::size_t whole = bytes.size() - (bytes.size() % 64);
  for (std::size_t at = 0; at < whole; at += 64)
    sha_block(h, data + at);
  // The tail, the 0x80 marker and the big-endian bit length fill one or two blocks.
  std::array<unsigned char, 128> tail{};
  const std::size_t rest = bytes.size() - whole;
  std::copy_n(data + whole, rest, tail.begin());
  tail.at(rest) = 0x80;
  const std::size_t blocks = rest < 56 ? 1 : 2;
  const std::uint64_t bits = static_cast<std::uint64_t>(bytes.size()) * 8U;
  for (std::size_t i = 0; i < 8; ++i)
    tail.at((blocks * 64) - 1 - i) =
        static_cast<unsigned char>((bits >> (8U * i)) & 0xffU);
  for (std::size_t i = 0; i < blocks; ++i)
    sha_block(h, tail.data() + (i * 64));
  static constexpr std::string_view hex = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (const auto word : h)
    for (int shift = 28; shift >= 0; shift -= 4)
      out.push_back(hex[(word >> static_cast<unsigned>(shift)) & 0xfU]);
  return out;
}

ProfileResult<std::string> normalize_base_url(std::string_view url) {
  std::string fault;
  auto normal = normal_url(url, fault);
  if (!normal)
    return std::unexpected(ProfileError{"", 0, 0, "base_url", fault});
  return std::move(*normal);
}

// Mirrors the main-coding route choice (src/main.cpp:466-545): OpenAI's ChatGPT route
// and OpenRouter always send Responses (OpenRouter's descriptor says `chat`); native
// Anthropic sends Messages; a registered route must be a Responses route.
ProfileResult<ProfileQuery> profile_query(const Value &binding,
                                          std::string_view model) {
  const auto field = [&](std::string_view key) -> const std::string * {
    const auto *x = binding.find(key);
    return x ? std::get_if<std::string>(&x->value()) : nullptr;
  };
  for (const auto key : {"provider"sv, "kind"sv, "base_url"sv, "protocol"sv})
    if (!field(key))
      return std::unexpected(
          ProfileError{"binding", 0, 0, "binding." + std::string{key}, "required"});
  ProfileQuery query;
  query.provider = *field("provider");
  query.account_kind = *field("kind");
  query.model = std::string{model};
  const auto &protocol = *field("protocol");
  if (query.provider == "openai" || query.provider == "openrouter")
    query.protocol = "responses";
  else if (query.provider == "anthropic" && protocol == "messages")
    query.protocol = "messages";
  else if (query.provider != "anthropic" &&
           (protocol == "responses" || protocol == "responses-portable"))
    query.protocol = protocol;
  else
    return std::unexpected(
        ProfileError{"binding", 0, 0, "binding.protocol",
                     "`" + protocol +
                         "` is not a main-coding codec: main coding uses Responses or "
                         "native Anthropic Messages"});
  std::string fault;
  auto normal = normal_url(*field("base_url"), fault);
  if (!normal)
    return std::unexpected(ProfileError{"binding", 0, 0, "binding.base_url", fault});
  query.base_url = std::move(*normal);
  return query;
}

ProfileResult<ProfileSet> load_profile_set(const ProfileDirectories &directories) {
  try {
    auto shipped = read_directory(directories.shipped, ProfileLayerKind::shipped);
    if (!shipped.has_value())
      return std::unexpected(shipped.error());
    ProfileSet set;
    bool found = false;
    for (auto &file : shipped.value()) {
      if (file.layer.find("id")->string() == conservative_profile_id) {
        set.conservative = std::move(file);
        found = true;
      } else {
        set.shipped.push_back(std::move(file));
      }
    }
    if (!found)
      return std::unexpected(
          ProfileError{directories.shipped.string(), 0, 0, "conservative",
                       "the shipped set has no conservative profile"});
    if (auto ok = validate_effective(set.conservative.layer, set.conservative.file);
        !ok.has_value())
      return std::unexpected(ok.error());
    for (const auto &file : set.shipped)
      if (auto ok = validate_effective(
              merge_override(set.conservative.layer, file.layer), file.file);
          !ok.has_value())
        return std::unexpected(ok.error());
    if (directories.operator_overrides) {
      const auto &dir = *directories.operator_overrides;
      std::error_code error;
      const auto link = std::filesystem::symlink_status(dir, error);
      if (!error && link.type() == std::filesystem::file_type::symlink &&
          !std::filesystem::exists(dir, error) && !error)
        return std::unexpected(
            ProfileError{dir.string(), 0, 0, "io",
                         "profile directory is a symbolic link to nothing"});
      error.clear();
      const auto status = std::filesystem::status(dir, error);
      const bool absent = error == std::errc::no_such_file_or_directory;
      if (error && !absent)
        return std::unexpected(ProfileError{
            dir.string(), 0, 0, "io", "profile directory is missing or unreadable"});
      if (!absent) {
        if (status.type() != std::filesystem::file_type::directory)
          return std::unexpected(ProfileError{
              dir.string(), 0, 0, "io", "profile directory is missing or unreadable"});
        auto overrides = read_directory(dir, ProfileLayerKind::override_layer);
        if (!overrides.has_value())
          return std::unexpected(overrides.error());
        std::map<std::string, std::string, std::less<>> targets;
        for (auto &file : overrides.value()) {
          const auto &target = file.layer.find("target")->string();
          const bool known =
              target == conservative_profile_id ||
              std::ranges::any_of(set.shipped, [&](const ProfileFile &candidate) {
                return candidate.layer.find("id")->string() == target;
              });
          if (!known)
            return std::unexpected(
                ProfileError{file.file, 0, 0, "target",
                             "`" + target + "` names no shipped profile"});
          const auto name = std::filesystem::path{file.file}.filename().string();
          if (const auto [at, inserted] = targets.emplace(target, name); !inserted)
            return std::unexpected(ProfileError{
                file.file, 0, 0, "target",
                "`" + target + "` is already overridden by " + at->second});
          set.operator_overrides.push_back(std::move(file));
        }
        std::ranges::sort(set.operator_overrides, {},
                          [](const ProfileFile &f) -> const std::string & {
                            return f.layer.find("target")->string();
                          });
      }
    }
    // Every effective profile an override can reach is validated: the conservative
    // override under conservative and under every shipped profile.
    const ProfileFile *base_override = override_for(set, conservative_profile_id);
    auto base = set.conservative.layer;
    if (base_override) {
      base = merge_override(base, base_override->layer);
      if (auto ok = validate_effective(base, base_override->file); !ok.has_value())
        return std::unexpected(ok.error());
    }
    for (const auto &file : set.shipped) {
      auto effective = merge_override(base, file.layer);
      if (base_override)
        if (auto ok = validate_effective(effective, base_override->file);
            !ok.has_value())
          return std::unexpected(ok.error());
      if (const auto *own = override_for(set, file.layer.find("id")->string()))
        if (auto ok =
                validate_effective(merge_override(effective, own->layer), own->file);
            !ok.has_value())
          return std::unexpected(ok.error());
    }
    Value::Array profiles;
    std::vector<const ProfileFile *> all{&set.conservative};
    for (const auto &file : set.shipped)
      all.push_back(&file);
    std::ranges::sort(all, {}, [](const auto *f) -> const std::string & {
      return f->layer.find("id")->string();
    });
    for (const auto *file : all)
      profiles.push_back(
          Value::object({{"id", *file->layer.find("id")},
                         {"match", *file->layer.find("match")},
                         {"digest", Value{profile_digest(file->layer)}}}));
    Value::Array overrides;
    for (const auto &file : set.operator_overrides)
      overrides.push_back(
          Value::object({{"id", *file.layer.find("id")},
                         {"target", *file.layer.find("target")},
                         {"digest", Value{profile_digest(file.layer)}}}));
    set.digest =
        digest_text("blackbird.provider-profile-set/1",
                    Value::object({{"profiles", Value{std::move(profiles)}},
                                   {"overrides", Value{std::move(overrides)}}}));
    // Everything, provenance included: basis, sources, revisions and file contents as
    // loaded, for the trial record of what ran.
    Value::Array whole_profiles;
    for (const auto *file : all)
      whole_profiles.push_back(file->layer);
    Value::Array whole_overrides;
    for (const auto &file : set.operator_overrides)
      whole_overrides.push_back(file.layer);
    set.content_digest =
        digest_text("blackbird.provider-profile-set-content/1",
                    Value::object({{"profiles", Value{std::move(whole_profiles)}},
                                   {"overrides", Value{std::move(whole_overrides)}}}));
    return set;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        ProfileError{directories.shipped.string(), 0, 0, "profile", "out of memory"});
  }
}

ProfileResult<const ProfileFile *> select_profile(const ProfileSet &set,
                                                  const ProfileQuery &raw) {
  auto query = raw;
  if (!query.base_url.empty()) {
    std::string fault;
    auto normal = normal_url(query.base_url, fault);
    if (!normal)
      return std::unexpected(ProfileError{"query", 0, 0, "query.base_url", fault});
    query.base_url = std::move(*normal);
  }
  const ProfileFile *best = nullptr;
  const ProfileFile *tie = nullptr;
  Specificity best_score;
  for (const auto &file : set.shipped) {
    const auto score = matches(file.layer, query);
    if (!score)
      continue;
    if (!best || *score > best_score) {
      best = &file;
      best_score = *score;
      tie = nullptr;
    } else if (*score == best_score && !tie) {
      tie = &file;
    }
  }
  if (tie)
    return std::unexpected(ProfileError{
        tie->file, 0, 0, "match",
        "`" + best->layer.find("id")->string() + "` and `" +
            tie->layer.find("id")->string() + "` match equally specifically"});
  return best ? best : &set.conservative;
}

ProfileResult<EffectiveProfile> resolve_profile(const ProfileSet &set,
                                                const ProfileQuery &query,
                                                const Value *session_override) {
  try {
    auto selected = select_profile(set, query);
    if (!selected.has_value())
      return std::unexpected(selected.error());
    const auto &chosen = *selected.value();
    EffectiveProfile result;
    result.id = chosen.layer.find("id")->string();
    result.revision = revision_of(chosen.layer);
    result.profile = set.conservative.layer;
    result.layers.push_back(label("shipped", set.conservative.layer));
    if (const auto *base = override_for(set, conservative_profile_id)) {
      result.profile = merge_override(result.profile, base->layer);
      result.layers.push_back(label("operator", base->layer));
    }
    if (&chosen != &set.conservative) {
      result.profile = merge_override(result.profile, chosen.layer);
      result.layers.push_back(label("shipped", chosen.layer));
      if (const auto *own = override_for(set, result.id)) {
        result.profile = merge_override(result.profile, own->layer);
        result.layers.push_back(label("operator", own->layer));
      }
    }
    if (session_override) {
      auto layer = validate_profile(
          {*session_override, ProfileLayerKind::override_layer, "session"});
      if (!layer.has_value())
        return std::unexpected(layer.error());
      const auto &target = layer.value().find("target")->string();
      if (target != result.id)
        return std::unexpected(ProfileError{
            "session", 0, 0, "target",
            "`" + target + "` is not the selected profile `" + result.id + "`"});
      result.profile = merge_override(result.profile, layer.value());
      if (auto ok = validate_effective(result.profile, "session"); !ok.has_value())
        return std::unexpected(ok.error());
      result.layers.push_back(label("session", layer.value()));
    }
    result.digest = profile_digest(result.profile);
    return result;
  } catch (const std::bad_alloc &) {
    return std::unexpected(ProfileError{"session", 0, 0, "profile", "out of memory"});
  }
}
} // namespace blackbird
