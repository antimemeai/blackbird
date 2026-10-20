// Specification oracles for profile schema, layering, selection and digests, plus the
// repository check over the shipped profile set and today's provider decisions.
#include "blackbird/anthropic_provider.hpp"
#include "blackbird/openrouter.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/provider_profile.hpp"
#include "blackbird/responses_provider.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
using namespace blackbird;
namespace {
void check(bool ok, const std::string &reason) {
  if (!ok)
    throw std::runtime_error(reason);
}
template <class T> std::string shown(const ProfileResult<T> &result) {
  if (result.has_value())
    return "accepted";
  return result.error().message();
}
Value parse(std::string_view text) {
  auto read = read_profile_text({text, "case.lua"});
  check(read.has_value(), "fixture does not read: " + std::string{text} + " -> " +
                              (read.has_value() ? "" : read.error().message()));
  return std::move(read).value();
}
ProfileResult<Value> shipped_layer(std::string_view text) {
  const auto v = parse(text);
  return validate_profile({v, ProfileLayerKind::shipped, "case.lua"});
}
ProfileResult<Value> override_layer(std::string_view text) {
  const auto v = parse(text);
  return validate_profile({v, ProfileLayerKind::override_layer, "case.lua"});
}
const Value &at(const Value &v, std::string_view dotted) {
  const Value *cursor = &v;
  std::string_view rest = dotted;
  while (!rest.empty()) {
    const auto dot = rest.find('.');
    const auto key = rest.substr(0, dot);
    cursor = cursor->find(key);
    check(cursor != nullptr, "missing " + std::string{dotted});
    rest = dot == std::string_view::npos ? std::string_view{} : rest.substr(dot + 1);
  }
  return *cursor;
}
std::string text_at(const Value &v, std::string_view dotted) {
  return at(v, dotted).string();
}
// The basis recorded for a leaf, by dotted path ("turn.close"); absent when none.
std::optional<std::string> basis_of(const Value &v, std::string_view leaf_path) {
  const auto dot = leaf_path.rfind('.');
  const auto *r = dot == std::string_view::npos ? &v : &at(v, leaf_path.substr(0, dot));
  const auto leaf =
      dot == std::string_view::npos ? leaf_path : leaf_path.substr(dot + 1);
  const auto *b = r->find("basis");
  if (!b)
    return std::nullopt;
  const auto *entry = b->find(leaf);
  if (!entry)
    return std::nullopt;
  return entry->string();
}

constexpr std::string_view conservative_text = R"lua(return {
  schema = "blackbird.provider-profile/1",
  id = "conservative",
  revision = 1,
  match = {},
  pairing = { validator = "codec", basis = "conservative" },
  reasoning = {
    at_cut = "keep",
    at_rebind = "drop_foreign",
    side_request_reasoning = "keep",
    basis = "conservative",
  },
  turn = {
    start = "user_message_without_results",
    close = "none",
    open_turn = { "closed_only" },
    boundary = { form = "user_note", enabled = false },
    basis = "conservative",
  },
  request_state = { placement = "request_local", basis = "conservative" },
  note = { role = "developer", placement = "after_tool_results", basis = "conservative" },
  tool_restriction = { mode = "palette_only", basis = "conservative" },
  tool_change = { mode = "cut", basis = "conservative" },
  archival = { mode = "allowed", basis = "conservative" },
  edit_acceptance = { in_place = "unknown", basis = "conservative" },
  cache = { capability = "none", basis = "conservative" },
  retry = {
    http = { 408, 429, 500, 502, 503, 504 },
    transport = { 5, 6, 7, 16, 18, 28, 52, 55, 56, 92, 95, 96 },
    rate_limit_requires_retry_after = false,
    basis = "conservative",
  },
}
)lua";

struct Refusal {
  std::string text;
  std::string subject;
  std::string reason;
  bool override_layer = false;
};
void schema() {
  const std::string head = "schema = \"blackbird.provider-profile/1\", id = \"p\", "
                           "revision = 1, match = { provider = \"x\" }, ";
  const std::string over = "schema = \"blackbird.provider-profile-override/1\", "
                           "id = \"o\", revision = 1, target = \"p\", ";
  const std::vector<Refusal> cases{
      {"return { " + head + "colour = 1 }", "colour", "unknown field"},
      {"return { " + head + "turn = { stat = \"x\" } }", "turn.stat", "unknown field"},
      {"return { schema = \"blackbird.provider-profile/2\", id = \"p\", revision = 1, "
       "match = { provider = \"x\" } }",
       "schema", "must be `blackbird.provider-profile/1`"},
      {"return { " + head + "reasoning = { at_cut = \"sometimes\" } }",
       "reasoning.at_cut",
       "`sometimes` is not one of drop_after_change, keep, must_keep, "
       "keep_whole_or_drop"},
      {"return { " + head + "turn = { boundary = { enabled = \"yes\" } } }",
       "turn.boundary.enabled", "must be a boolean"},
      {"return { " + head + "retry = { http = { 408, 99 } } }", "retry.http[2]",
       "must be an integer from 100 to 599"},
      {"return { schema = \"blackbird.provider-profile/1\", id = \"p\", revision = "
       "1.5, "
       "match = { provider = \"x\" } }",
       "revision", "must be an integer from 1 to 1000000000"},
      {"return { schema = \"blackbird.provider-profile/1\", id = \"Bad Id\", "
       "revision = 1, match = { provider = \"x\" } }",
       "id",
       "must be an identifier: lowercase letters, digits, `-` and `_`, starting with a "
       "letter"},
      {"return { " + head + "turn = { close = \"none\", basis = \"guess\" } }",
       "turn.basis",
       "`guess` is not a basis: expected cited, probed, conservative or unobserved, "
       "optionally followed by `:detail`"},
      {"return { " + head + "turn = { close = \"none\", basis = \"cited\" } }",
       "turn.basis", "`cited` needs a detail: `cited:<source>`"},
      {"return { " + head +
           "turn = { close = \"none\", basis = { clse = \"conservative\" } } }",
       "turn.basis.clse", "names no field of turn"},
      {"return { " + head +
           "turn = { close = \"none\", basis = { start = \"conservative\" } } }",
       "turn.basis.start", "names a field this layer does not set"},
      {"return { " + head +
           "turn = { open_turn = { \"closed_only\", \"closed_only\" } } }",
       "turn.open_turn", "lists `closed_only` twice"},
      {"return { " + head + "turn = { open_turn = {} } }", "turn.open_turn",
       "must not be empty"},
      {"return { " + head + "turn = { \"x\" } }", "turn",
       "must be a table of named fields"},
      {"return { " + head + "turn = \"none\" }", "turn",
       "must be a table of named fields"},
      {"return { " + head + "retry = { http = 408 } }", "retry.http", "must be a list"},
      {"return { " + head + "note = { role = 1 } }", "note.role", "must be a string"},
      {"return { schema = \"blackbird.provider-profile/1\", id = \"conservative\", "
       "revision = 1, match = { provider = \"x\" } }",
       "match", "the conservative profile must match everything: leave match empty"},
      {"return { schema = \"blackbird.provider-profile/1\", id = \"p\", revision = 1, "
       "match = {} }",
       "match", "only the conservative profile may have an empty match"},
      {"return { schema = \"blackbird.provider-profile/1\", id = \"p\", revision = 1, "
       "match = { models = { \"m\" }, model_prefixes = { \"m\" } } }",
       "match", "set models or model_prefixes, not both"},
      {"return { schema = \"blackbird.provider-profile/1\", id = \"p\", revision = 1, "
       "match = { base_url = \"http://x.example/v1\" } }",
       "match.base_url",
       "must be a normalized https URL: lowercase scheme and host, no default port, no "
       "trailing `/`"},
      {"return { schema = \"blackbird.provider-profile/1\", id = \"p\", revision = 1, "
       "match = { protocol = \"carrier-pigeon\" } }",
       "match.protocol",
       "`carrier-pigeon` is not one of messages, responses, responses-portable, chat"},
      {"return { schema = \"blackbird.provider-profile/1\", id = \"p\", revision = 1 }",
       "match", "required"},
      {"return { schema = \"blackbird.provider-profile/1\", revision = 1, "
       "match = { provider = \"x\" } }",
       "id", "required"},
      {"return { " + head + "sources = { thinking = { read = \"2026-10-19\" } } }",
       "sources.thinking", "needs at least one of url or path"},
      {"return { " + head +
           "sources = { s = { url = \"https://s.example\", path = \"papers/a.md\", "
           "read = "
           "\"2026-10-19\" } } }",
       "sources.s", "set url or path, not both"},
      {"return { " + head +
           "sources = { s = { path = \"/etc/passwd\", read = \"2026-10-19\" } } }",
       "sources.s.path",
       "must be a relative repository path: no leading `/`, no `.` or `..` component, "
       "no "
       "control characters"},
      {"return { " + head +
           "sources = { s = { path = \"papers/../../x\", read = \"2026-10-19\" } } }",
       "sources.s.path",
       "must be a relative repository path: no leading `/`, no `.` or `..` component, "
       "no "
       "control characters"},
      {"return { " + head +
           "sources = { s = { path = \"papers/a\\nb.md\", read = \"2026-10-19\" } } }",
       "sources.s.path",
       "must be a relative repository path: no leading `/`, no `.` or `..` component, "
       "no "
       "control characters"},
      {"return { " + head +
           "sources = { thinking = { url = \"https://x.example/a\", read = "
           "\"2026-13-01\" } } }",
       "sources.thinking.read", "must be a date YYYY-MM-DD"},
      {"return { " + head + "errors = { { class = \"overloaded\" } } }", "errors[1]",
       "needs at least one of status, event, error_type, code or contains"},
      {"return { " + head + "errors = { { status = 529 } } }", "errors[1].class",
       "required"},
      {"return { " + head +
           "errors = { { class = \"overloaded\", contains = \"\" } } }",
       "errors[1].contains", "must not be empty"},
      {"return { " + head +
           "errors = { { class = \"context_overflow\", status = 400, extract = { "
           "input_tokens = \"\" } } } }",
       "errors[1].extract.input_tokens", "must not be empty"},
      {"return { " + head +
           "turn = { boundary = { form = \"user_note\", basis = \"conservative\" }, "
           "basis = \"cited:nosuch\" } }",
       "turn.basis", "covers no field this layer sets"},
      {"return { " + head +
           "turn = { boundary = { form = \"user_note\", basis = \"conservative\" }, "
           "basis = { boundary = \"unobserved\" } } }",
       "turn.basis.boundary", "covers no field this layer sets"},
      {"return { schema = \"blackbird.provider-profile/1\", id = \"p\", revision = 1, "
       "match = { base_url = \"https://X.example/v1\" } }",
       "match.base_url",
       "must be a normalized https URL: lowercase scheme and host, no default port, no "
       "trailing `/`"},
      {"return { schema = \"blackbird.provider-profile/1\", id = \"p\", revision = 1, "
       "match = { base_url = \"https://x.example:443/v1\" } }",
       "match.base_url",
       "must be a normalized https URL: lowercase scheme and host, no default port, no "
       "trailing `/`"},
      {"return { " + head + "output = { max_tokens = 0 } }", "output.max_tokens",
       "must be an integer from 1 to 10000000"},
      {"return { " + head + "cache = { read_multiplier = 200 } }",
       "cache.read_multiplier", "must be a number from 0 to 100"},
      {"return { " + over + "match = { provider = \"x\" } }", "match",
       "an override cannot change match", true},
      {"return { schema = \"blackbird.provider-profile-override/1\", id = \"o\", "
       "revision = 1 }",
       "target", "required", true},
      {"return { schema = \"blackbird.provider-profile/1\", id = \"o\", revision = 1, "
       "target = \"p\" }",
       "schema", "must be `blackbird.provider-profile-override/1`", true},
  };
  for (const auto &c : cases) {
    const auto result =
        c.override_layer ? override_layer(c.text) : shipped_layer(c.text);
    const ProfileError expected{"case.lua", 0, 0, c.subject, c.reason};
    check(!result.has_value() && result.error() == expected,
          "schema refusal: " + c.text + " -> " + shown(result) + " (expected " +
              expected.message() + ")");
  }
  // A Value built outside the reader may repeat a key; it is refused, never ignored.
  auto repeated =
      parse("return { schema = \"blackbird.provider-profile/1\", id = \"p\", "
            "revision = 1, match = { provider = \"x\" }, turn = { close = "
            "\"none\" } }");
  for (auto &[key, value] : repeated.object())
    if (key == "turn")
      value.object().emplace_back("close", Value{"reply_request"});
  const auto twice =
      validate_profile({repeated, ProfileLayerKind::shipped, "case.lua"});
  check(!twice.has_value() &&
            twice.error() ==
                ProfileError{"case.lua", 0, 0, "turn.close", "duplicate field"},
        "duplicate key: " + shown(twice));
  // A sparse layer and a complete profile are both accepted at the layer level.
  check(shipped_layer("return { " + head + "turn = { close = \"none\" } }").has_value(),
        "sparse shipped layer");
  check(shipped_layer(conservative_text).has_value(), "conservative fixture");
  // `{}` is typed by the schema: an empty list and an empty map are both accepted.
  const auto typed =
      shipped_layer("return { " + head + "errors = {}, stop_reasons = {} }");
  check(typed.has_value() &&
            std::holds_alternative<Value::Array>(at(typed.value(), "errors").value()),
        "empty table typed as a list: " + shown(typed));
}

Value effective_of(std::string_view shipped_text) {
  const auto base = shipped_layer(conservative_text).value();
  const auto layer = shipped_layer(shipped_text);
  check(layer.has_value(), "fixture layer: " + shown(layer));
  return merge_override(base, layer.value());
}
void effective() {
  const std::string head =
      "return { schema = \"blackbird.provider-profile/1\", id = \"p\", "
      "revision = 1, ";
  struct Case {
    std::string text;
    std::string subject;
    std::string reason;
  };
  const std::vector<Case> cases{
      {head +
           "match = { provider = \"x\" }, reasoning = { at_cut = \"drop_after_change\" "
           "}, turn = { open_turn = { \"retain_current_turn\" } } }",
       "turn.open_turn",
       "`retain_current_turn` contradicts reasoning.at_cut `drop_after_change`"},
      {head + "match = { provider = \"x\" }, turn = { boundary = { form = "
              "\"reply_then_note\" } } }",
       "turn.boundary.form", "`reply_then_note` requires turn.close `reply_request`"},
      {head + "match = { protocol = \"messages\" }, request_state = { placement = "
              "\"retained_in_place\" }, turn = { boundary = { form = "
              "\"request_local_user\" } } }",
       "turn.boundary.form",
       "`request_local_user` requires request_state.placement `request_local`"},
      {head + "match = { protocol = \"messages\" }, tool_restriction = { mode = "
              "\"allowed_tools\" } }",
       "tool_restriction.mode", "`allowed_tools` requires match.protocol `responses`"},
      {head + "match = { protocol = \"responses\" }, tool_change = { mode = "
              "\"appended_system_message\" } }",
       "tool_change.mode",
       "`appended_system_message` requires match.protocol `messages`"},
      {head + "match = { protocol = \"responses\" }, pairing = { validator = "
              "\"anthropic-messages\" } }",
       "pairing.validator", "`anthropic-messages` requires match.protocol `messages`"},
      {head + "match = { protocol = \"responses\" }, note = { role = "
              "\"developer_as_user_text\" } }",
       "note.role", "`developer_as_user_text` requires match.protocol `messages`"},
      {head + "match = { protocol = \"messages\" }, reasoning = { context = "
              "\"current_turn\" } }",
       "reasoning.context", "`current_turn` requires match.protocol `responses`"},
      {head + "match = { provider = \"x\" }, turn = { close = \"none\", basis = "
              "\"cited:nosuch\" } }",
       "turn.basis.close", "`cited:nosuch` names no entry in sources"},
      {head + "match = { provider = \"x\" }, cache = { capability = \"none\", ttl = "
              "\"30m\" } }",
       "cache.ttl", "contradicts cache.capability `none`"},
      {head +
           "match = { provider = \"x\" }, cache = { capability = \"none\", affinity = "
           "{ header = \"session-id\" } } }",
       "cache.affinity", "contradicts cache.capability `none`"},
      {head + "match = { provider = \"x\" }, cache = { capability = \"implicit\", "
              "breakpoints = { \"moving_tail\" } } }",
       "cache.breakpoints", "requires cache.capability `explicit`"},
  };
  for (const auto &c : cases) {
    const auto result = validate_effective(effective_of(c.text), "case.lua");
    const ProfileError expected{"case.lua", 0, 0, c.subject, c.reason};
    check(!result.has_value() && result.error() == expected,
          "effective refusal: " + c.text + " -> " + shown(result) + " (expected " +
              expected.message() + ")");
  }
  // A complete profile missing a required family is refused; the fixture is complete.
  check(
      validate_effective(shipped_layer(conservative_text).value(), "c.lua").has_value(),
      "conservative fixture is complete");
  const auto without_retry = shipped_layer(
      "return { schema = \"blackbird.provider-profile/1\", id = \"conservative\", "
      "revision = 1, match = {}, pairing = { validator = \"codec\" }, reasoning = { "
      "at_cut = \"keep\", at_rebind = \"drop_foreign\", side_request_reasoning = "
      "\"keep\" }, turn = { start = \"user_text\", close = \"none\", open_turn = { "
      "\"closed_only\" }, boundary = { form = \"user_note\", enabled = false } }, "
      "request_state = { placement = \"request_local\" }, note = { role = "
      "\"developer\", placement = \"after_tool_results\" }, tool_restriction = { mode "
      "= "
      "\"palette_only\" }, tool_change = { mode = \"cut\" }, archival = { mode = "
      "\"allowed\" }, edit_acceptance = { in_place = \"unknown\" }, cache = { "
      "capability = \"none\" } }");
  check(without_retry.has_value(), "layer without retry");
  const auto incomplete = validate_effective(without_retry.value(), "c.lua");
  check(!incomplete.has_value() &&
            incomplete.error() == ProfileError{"c.lua", 0, 0, "retry", "required"},
        "missing family: " + shown(incomplete));
  // A cited basis naming a present source is accepted.
  const auto cited = validate_effective(
      effective_of(head +
                   "match = { provider = \"x\" }, sources = { guide = { url = "
                   "\"https://x.example/guide\", read = \"2026-10-19\" } }, turn = "
                   "{ close = \"none\", basis = \"cited:guide\" } }"),
      "case.lua");
  check(cited.has_value(), "present cited source: " + shown(cited));
  // A community observation is cited through a repository-path source with a note.
  const auto community = validate_effective(
      effective_of(head +
                   "match = { provider = \"x\" }, sources = { reports = { path = "
                   "\"papers/clm-a1v-2026-10-19/OVERFLOW_STUDY.md\", read = "
                   "\"2026-10-19\", note = \"community: pi\" } }, turn = { close = "
                   "\"none\", basis = \"cited:reports\" } }"),
      "case.lua");
  check(community.has_value(), "path source: " + shown(community));
}

struct Directory {
  std::filesystem::path path;
  Directory() {
    std::string pattern = "/tmp/blackbird-profiles-XXXXXX";
    check(mkdtemp(pattern.data()) != nullptr, "temporary directory");
    path = pattern;
    std::filesystem::create_directory(path / "shipped");
    std::filesystem::create_directory(path / "operator");
  }
  Directory(const Directory &) = delete;
  Directory &operator=(const Directory &) = delete;
  ~Directory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  void write(const std::string &relative, std::string_view text) const {
    std::ofstream output{path / relative, std::ios::binary | std::ios::trunc};
    output << text;
    check(static_cast<bool>(output), "write fixture " + relative);
  }
  ProfileResult<ProfileSet> load() const {
    return load_profile_set({path / "shipped", path / "operator"});
  }
};
std::string shipped_profile(std::string_view id, std::string_view match,
                            std::string_view body = "") {
  return "return { schema = \"blackbird.provider-profile/1\", id = \"" +
         std::string{id} + "\", revision = 1, match = " + std::string{match} + ", " +
         std::string{body} + " }";
}
std::string override_profile(std::string_view id, std::string_view target,
                             std::string_view body) {
  return "return { schema = \"blackbird.provider-profile-override/1\", id = \"" +
         std::string{id} + "\", revision = 3, target = \"" + std::string{target} +
         "\", " + std::string{body} + " }";
}
void layering() {
  Directory d;
  d.write("shipped/conservative.lua", conservative_text);
  d.write(
      "shipped/acme.lua",
      shipped_profile("acme", "{ provider = \"acme\", protocol = \"responses\" }",
                      "turn = { start = \"user_text\", close = \"none\", open_turn = { "
                      "\"retain_current_turn\", \"boundary\" }, basis = \"unobserved\" "
                      "}, retry = { http = { 503 }, basis = \"unobserved\" }"));
  // Operator override: no basis at all, which is never refused.
  d.write(
      "operator/acme-local.lua",
      override_profile("acme-local", "acme",
                       "turn = { open_turn = { \"closed_only\" } }, tool_change = { "
                       "mode = \"cut\" }, note = { role = \"user\" }"));
  // Operator override of conservative: the base layer under every shipped profile.
  d.write(
      "operator/conservative-local.lua",
      override_profile("conservative-local", "conservative",
                       "edit_acceptance = { in_place = \"refused\" }, turn = { close "
                       "= \"reply_request\" }"));
  const auto set = d.load();
  check(set.has_value(), "layered set: " + shown(set));
  const ProfileQuery query{"acme", "responses", "https://acme.example/v1", "api_key",
                           "m"};
  const auto session_value =
      parse(override_profile("trial", "acme",
                             "note = { role = \"developer\", basis = "
                             "\"conservative:session\" }, turn = { "
                             "boundary = { enabled = false } }"));
  const auto resolved = resolve_profile(set.value(), query, &session_value);
  check(resolved.has_value(), "resolve: " + shown(resolved));
  const auto &p = resolved.value().profile;
  // Highest layer wins per leaf; records merge by key; lists replace.
  check(text_at(p, "note.role") == "developer", "session over operator");
  check(text_at(p, "note.placement") == "after_tool_results",
        "record merge keeps base");
  check(at(p, "turn.open_turn") == Value{Value::Array{Value{"closed_only"}}},
        "operator list replaces shipped list (no append)");
  check(text_at(p, "turn.start") == "user_text", "shipped over conservative");
  check(text_at(p, "turn.close") == "none",
        "shipped value kept under sparse overrides");
  check(at(p, "retry.http") == Value{Value::Array{Value{Number{503}}}},
        "shipped list replaces conservative list");
  check(at(p, "retry.transport").array().size() == 12, "conservative list kept");
  check(at(p, "turn.boundary.form") == Value{"user_note"}, "nested record merge");
  check(text_at(p, "edit_acceptance.in_place") == "refused",
        "conservative override applies under a selected shipped profile");
  check(text_at(p, "turn.close") == "none",
        "the shipped profile is above the conservative override");
  check(resolved.value().layers ==
            std::vector<std::string>{"shipped:conservative/1",
                                     "operator:conservative-local/3", "shipped:acme/1",
                                     "operator:acme-local/3", "session:trial/3"},
        "layers, lowest first");
  check(resolved.value().id == "acme" && resolved.value().revision == 1,
        "effective identity is the selected shipped profile");
  // A layer that sets a leaf takes its basis: none for the operator's open_turn,
  // the session's for note.role; untouched leaves keep the lower layer's basis.
  check(!basis_of(p, "turn.open_turn"), "operator value must not inherit a basis");
  check(basis_of(p, "turn.start") == "unobserved", "shipped basis kept");
  check(basis_of(p, "note.role") == "conservative:session", "session basis");
  check(basis_of(p, "note.placement") == "conservative", "conservative basis kept");
  const auto missing = missing_basis(p);
  std::string listed;
  for (const auto &path : missing)
    listed += " " + path;
  check(missing == std::vector<std::string>{"turn.open_turn", "turn.boundary.enabled",
                                            "tool_change.mode",
                                            "edit_acceptance.in_place"},
        "missing basis after overrides:" + listed);
  // The same query without a session override stops at the operator layer.
  const auto operator_only = resolve_profile(set.value(), query);
  check(operator_only.has_value() &&
            text_at(operator_only.value().profile, "note.role") == "user" &&
            operator_only.value().layers.size() == 4,
        "operator layer");
  // A session override naming another profile is refused.
  const auto stray =
      parse(override_profile("trial", "other", "note = { role = \"user\" }"));
  const auto refused = resolve_profile(set.value(), query, &stray);
  check(!refused.has_value() &&
            refused.error() ==
                ProfileError{"session", 0, 0, "target",
                             "`other` is not the selected profile `acme`"},
        "session target: " + shown(refused));
  // An invalid session override is refused with its reason.
  const auto bad =
      parse(override_profile("trial", "acme",
                             "reasoning = { at_cut = \"drop_after_change\" }, "
                             "turn = { open_turn = { \"retain_current_turn\" "
                             "} }"));
  const auto contradictory = resolve_profile(set.value(), query, &bad);
  check(!contradictory.has_value() && contradictory.error().subject == "turn.open_turn",
        "session override validated: " + shown(contradictory));

  // A conservative override valid alone but contradictory under acme is refused at
  // load.
  d.write("operator/conservative-local.lua",
          override_profile("conservative-local", "conservative",
                           "reasoning = { at_cut = \"drop_after_change\" }"));
  auto under = d.load();
  check(!under.has_value() &&
            under.error() ==
                ProfileError{(d.path / "operator/conservative-local.lua").string(), 0,
                             0, "turn.open_turn",
                             "`retain_current_turn` contradicts reasoning.at_cut "
                             "`drop_after_change`"},
        "conservative override validated under acme: " + shown(under));
  std::filesystem::remove(d.path / "operator/conservative-local.lua");
  // An operator directory that cannot be read is refused, not treated as absent. Root
  // ignores the mode, so the case runs only for an ordinary user.
  if (::geteuid() != 0) {
    std::filesystem::create_directories(d.path / "locked/operator");
    ::chmod((d.path / "locked").c_str(), 0);
    const auto locked =
        load_profile_set({d.path / "shipped", d.path / "locked/operator"});
    ::chmod((d.path / "locked").c_str(), 0700);
    check(!locked.has_value() &&
              locked.error() ==
                  ProfileError{(d.path / "locked/operator").string(), 0, 0, "io",
                               "profile directory is missing or unreadable"},
          "unreadable operator directory: " + shown(locked));
  }
  // B8: a dangling symbolic link at the operator directory is refused, not absent.
  std::filesystem::create_directory_symlink(d.path / "nowhere-operator",
                                            d.path / "dangling-operator");
  const auto dangling =
      load_profile_set({d.path / "shipped", d.path / "dangling-operator"});
  check(!dangling.has_value() &&
            dangling.error() ==
                ProfileError{(d.path / "dangling-operator").string(), 0, 0, "io",
                             "profile directory is a symbolic link to nothing"},
        "dangling operator directory link: " + shown(dangling));
  // B9: one bad entry is reported by its own name.
  std::filesystem::create_symlink(d.path / "nowhere.lua",
                                  d.path / "shipped/broken.lua");
  const auto bad_entry = d.load();
  check(!bad_entry.has_value() &&
            bad_entry.error() ==
                ProfileError{(d.path / "shipped/broken.lua").string(), 0, 0, "io",
                             "the entry is a dangling symbolic link or unreadable"},
        "bad entry named: " + shown(bad_entry));
  std::filesystem::remove(d.path / "shipped/broken.lua");
  // Load-time refusals: the whole set is refused, with the file and reason.
  d.write("operator/stray.lua", override_profile("stray", "nosuch", ""));
  auto load = d.load();
  check(!load.has_value() &&
            load.error() == ProfileError{(d.path / "operator/stray.lua").string(), 0, 0,
                                         "target", "`nosuch` names no shipped profile"},
        "unknown target: " + shown(load));
  std::filesystem::remove(d.path / "operator/stray.lua");
  d.write("operator/again.lua", override_profile("again", "acme", ""));
  load = d.load();
  check(!load.has_value() && load.error().subject == "target" &&
            load.error().reason == "`acme` is already overridden by acme-local.lua",
        "second override of one target: " + shown(load));
  std::filesystem::remove(d.path / "operator/again.lua");
  d.write("shipped/misnamed.lua", shipped_profile("other", "{ provider = \"o\" }"));
  load = d.load();
  check(!load.has_value() &&
            load.error() == ProfileError{(d.path / "shipped/misnamed.lua").string(), 0,
                                         0, "id",
                                         "`other` does not match the file name"},
        "id and file name: " + shown(load));
  std::filesystem::remove(d.path / "shipped/misnamed.lua");
  d.write("shipped/broken.lua", "return { x = os.exit() }");
  load = d.load();
  check(!load.has_value() && load.error().subject == "global" && load.error().line == 1,
        "reader refusal surfaces: " + shown(load));
  std::filesystem::remove(d.path / "shipped/broken.lua");
  d.write("shipped/bad.lua",
          shipped_profile("bad", "{ provider = \"b\" }",
                          "turn = { boundary = { form = \"reply_then_note\" } }"));
  load = d.load();
  check(!load.has_value() && load.error().subject == "turn.boundary.form",
        "shipped profile validated over conservative: " + shown(load));
  std::filesystem::remove(d.path / "shipped/bad.lua");
  std::filesystem::remove(d.path / "shipped/conservative.lua");
  load = d.load();
  check(!load.has_value() &&
            load.error() == ProfileError{(d.path / "shipped").string(), 0, 0,
                                         "conservative",
                                         "the shipped set has no conservative profile"},
        "conservative required: " + shown(load));
  const auto absent = load_profile_set({d.path / "nowhere", std::nullopt});
  check(!absent.has_value() &&
            absent.error() ==
                ProfileError{(d.path / "nowhere").string(), 0, 0, "io",
                             "profile directory is missing or unreadable"},
        "missing directory: " + shown(absent));
  // A missing operator directory means no overrides.
  d.write("shipped/conservative.lua", conservative_text);
  check(load_profile_set({d.path / "shipped", d.path / "no-operator"}).has_value(),
        "absent operator directory");
}

void match() {
  Directory d;
  d.write("shipped/conservative.lua", conservative_text);
  d.write("shipped/p1.lua", shipped_profile("p1", "{ provider = \"x\" }"));
  d.write("shipped/p2.lua",
          shipped_profile("p2", "{ provider = \"x\", protocol = \"responses\" }"));
  d.write("shipped/p3.lua",
          shipped_profile("p3", "{ provider = \"x\", protocol = \"responses\", "
                                "model_prefixes = { \"m-\" } }"));
  d.write("shipped/p4.lua",
          shipped_profile("p4", "{ provider = \"x\", protocol = \"responses\", "
                                "model_prefixes = { \"m-1\", \"z-\" } }"));
  d.write("shipped/p5.lua",
          shipped_profile("p5", "{ provider = \"x\", protocol = \"responses\", "
                                "models = { \"m-1-a\" } }"));
  d.write("shipped/p6.lua",
          shipped_profile("p6", "{ provider = \"x\", account_kind = \"k\" }"));
  d.write("shipped/p7.lua",
          shipped_profile(
              "p7", "{ provider = \"x\", protocol = \"responses\", "
                    "base_url = \"https://x.example/v1\", account_kind = \"k\" }"));
  // A `models` entry names a family: the name itself, or the name, `-`, any suffix.
  d.write("shipped/p8.lua",
          shipped_profile("p8", "{ provider = \"x\", protocol = \"responses\", "
                                "models = { \"fam-5-5\" } }"));
  d.write("shipped/p9.lua",
          shipped_profile("p9", "{ provider = \"x\", protocol = \"responses\", "
                                "models = { \"fam-5-5-20261001\" } }"));
  const auto set = d.load();
  check(set.has_value(), "match set: " + shown(set));
  const auto pick = [&](const ProfileQuery &q) {
    const auto selected = select_profile(set.value(), q);
    check(selected.has_value(), "select: " + shown(selected));
    return text_at(selected.value()->layer, "id");
  };
  check(pick({"x", "responses", "", "", "m-1-a"}) == "p5", "exact model beats prefix");
  check(pick({"x", "responses", "", "", "m-1-b"}) == "p4", "longer prefix wins");
  check(pick({"x", "responses", "", "", "m-2"}) == "p3", "prefix beats no model");
  check(pick({"x", "responses", "", "", "q"}) == "p2", "two dimensions beat one");
  check(pick({"x", "messages", "", "", "m-1-a"}) == "p1", "protocol must match");
  check(pick({"y", "responses", "", "", "m-1-a"}) == "conservative", "fallback");
  check(pick({"x", "responses", "https://x.example/v1", "k", "q"}) == "p7",
        "four dimensions win");
  check(pick({"x", "responses", "", "", "fam-5-5"}) == "p8", "family name, exact");
  check(pick({"x", "responses", "", "", "fam-5-5-20260101"}) == "p8",
        "dated variant of a family");
  check(pick({"x", "responses", "", "", "fam-5-5-latest"}) == "p8",
        "any suffixed variant of a family");
  check(pick({"x", "responses", "", "", "fam-5-5-20261001"}) == "p9",
        "an exact listing beats a family match");
  check(pick({"x", "responses", "", "", "fam-5-50"}) == "p2",
        "an unrelated name sharing characters is not the family");
  // p2 (provider, protocol) and p6 (provider, account kind) tie for this query.
  const auto tie = select_profile(set.value(), {"x", "responses", "", "k", "q"});
  check(!tie.has_value() && tie.error().subject == "match" &&
            tie.error().reason == "`p2` and `p6` match equally specifically",
        "ambiguous match: " + shown(tie));
  // The query's base URL is normalized: scheme and host case, default port, trailing
  // `/`.
  check(pick({"x", "responses", "https://x.example/v1/", "k", "q"}) == "p7",
        "trailing slash");
  check(pick({"x", "responses", "HTTPS://X.Example:443/v1", "k", "q"}) == "p7",
        "scheme, host case and default port");
  const auto other_port = select_profile(
      set.value(), {"x", "responses", "https://x.example:8443/v1", "k", "q"});
  check(!other_port.has_value() && other_port.error().subject == "match",
        "another port is another URL, so p2/p6 tie: " + shown(other_port));
  const auto path_case =
      select_profile(set.value(), {"x", "responses", "https://x.example/V1", "k", "q"});
  check(!path_case.has_value(), "the path keeps its case");
  for (const auto &[url, reason] : std::vector<std::pair<std::string, std::string>>{
           {"http://x.example/v1", "`http://x.example/v1` is not an https URL"},
           {"https:///v1", "`https:///v1` is not a well-formed https URL"},
           {"https://x.example:0/v1",
            "`https://x.example:0/v1` is not a well-formed https URL"},
           {"https://x.example/v1?a=b",
            "`https://x.example/v1?a=b` is not a well-formed https URL"},
           {"https://x ample/v1",
            "`https://x ample/v1` is not a well-formed https URL"}}) {
    const auto bad = select_profile(set.value(), {"x", "responses", url, "k", "q"});
    check(!bad.has_value() &&
              bad.error() == ProfileError{"query", 0, 0, "query.base_url", reason},
          "malformed query base URL " + url + ": " + shown(bad));
  }
}

std::string digest_of(std::string_view shipped_text) {
  return profile_digest(effective_of(shipped_text));
}
void digest() {
  // FIPS 180-4 / NIST CAVP short-message vectors.
  check(sha256_hex("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256 empty");
  check(sha256_hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256 abc");
  check(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "sha256 two blocks");
  check(sha256_hex(std::string(1000000, 'a')) ==
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "sha256 million");
  check(sha256_hex(std::string(64, 'x')) != sha256_hex(std::string(65, 'x')),
        "length sensitivity");
  // Padding boundaries; expected values from Python's hashlib (independent model).
  const std::vector<std::pair<std::size_t, std::string_view>> boundaries{
      {55, "e7313d333c272e639f790978283f9eb392e843d0f29b7016828bb1daa4aac70b"},
      {63, "81c80242132f230c3bd41b3e63bbcff16107339549214a99614ff26664625055"},
      {64, "39e3d7b6b5d075d37d053ad89b24b41bef4f3c29760c84447cab3f3be1882241"},
      {119, "9ce7368e4daf32341631b492e80359dc9f594b48453cd0dd5bf0b19279cc177e"},
      {120, "7836b787757e95e58b3ca5aec90b1b004e8deba1e50e9675af9cabf1a13a04b5"}};
  for (const auto &[length, expected] : boundaries) {
    std::string bytes;
    for (std::size_t i = 0; i < length; ++i)
      bytes.push_back(static_cast<char>(((i * 7) + 3) % 256));
    check(sha256_hex(bytes) == expected,
          "sha256 at " + std::to_string(length) + " bytes");
  }

  const std::string head =
      "return { schema = \"blackbird.provider-profile/1\", id = \"p\", "
      "revision = 1, match = { provider = \"x\" }, ";
  const auto base = digest_of(head + "sources = { g = { url = \"https://x.example/g\", "
                                     "read = \"2026-10-19\" } }, turn = { close = "
                                     "\"none\", start = \"user_text\", basis = "
                                     "\"cited:g\" } }");
  check(base.starts_with("sha256:") && base.size() == 7 + 64, "digest form");
  // Provenance, identity and match edits leave the behavioural digest unchanged.
  const std::vector<std::string> same{
      head +
          "sources = { g = { url = \"https://x.example/g\", read = \"2026-10-19\" } }, "
          "turn = { close = \"none\", start = \"user_text\", basis = \"unobserved\" } "
          "}",
      head +
          "sources = { g = { url = \"https://y.example/other\", read = \"2026-10-20\" "
          "} }, turn = { close = \"none\", start = \"user_text\", basis = \"cited:g\" "
          "} }",
      "return { schema = \"blackbird.provider-profile/1\", id = \"q\", revision = 9, "
      "match = { provider = \"z\", models = { \"m\" } }, sources = { g = { url = "
      "\"https://x.example/g\", read = \"2026-10-19\" } }, turn = { close = \"none\", "
      "start = \"user_text\", basis = \"cited:g\" } }",
      head + "turn = { start = \"user_text\", close = \"none\" } }",
  };
  for (const auto &text : same)
    check(digest_of(text) == base, "digest changed by a non-behavioural edit: " + text);
  // Behavioural edits change it.
  const std::vector<std::string> different{
      head + "turn = { close = \"reply_request\", start = \"user_text\" } }",
      head + "turn = { close = \"none\", start = \"user_message_without_results\" } }",
      head +
          "turn = { close = \"none\", start = \"user_text\" }, retry = { http = { 500 "
          "} } }",
      head + "turn = { close = \"none\", start = \"user_text\", open_turn = { "
             "\"boundary\", \"closed_only\" } } }",
      head + "turn = { close = \"none\", start = \"user_text\", open_turn = { "
             "\"closed_only\", \"boundary\" } } }",
      head + "turn = { close = \"none\", start = \"user_text\" }, errors = { { class = "
             "\"overloaded\", status = 529 } } }",
  };
  std::vector<std::string> seen{base};
  for (const auto &text : different) {
    const auto d = digest_of(text);
    check(std::ranges::find(seen, d) == seen.end(),
          "digest insensitive to a behavioural edit: " + text);
    seen.push_back(d);
  }
  // A basis-only difference digests alike.
  check(digest_of(head + "output = { max_tokens = 16384 } }") ==
            digest_of(head +
                      "output = { max_tokens = 16384, basis = \"conservative\" } }"),
        "basis-only difference");
  // Regression pin (not a specification oracle) for the canonical encoding: P1b
  // compares digests across hosts, so an encoding change must be deliberate.
  const auto pinned = profile_digest(shipped_layer(conservative_text).value());
  check(pinned ==
            "sha256:7474e6f725ac4fb722bf3bc54506d7cf4e344b39852cc01471506f73412487de",
        "canonical encoding changed: " + pinned);

  // Set digest: sensitive to behaviour and match, not to provenance.
  Directory d;
  d.write("shipped/conservative.lua", conservative_text);
  d.write("shipped/a.lua",
          shipped_profile("a", "{ provider = \"a\" }",
                          "turn = { close = \"none\", basis = \"unobserved\" }"));
  const auto first = d.load().value().digest;
  check(first.starts_with("sha256:"), "set digest form");
  // The content digest covers everything, provenance included, for trial records.
  const auto content = d.load().value().content_digest;
  check(content.starts_with("sha256:") && content != first, "content digest form");
  d.write("shipped/a.lua",
          shipped_profile("a", "{ provider = \"a\" }", "turn = { close = \"none\" }"));
  check(d.load().value().digest == first, "set digest changed by a basis edit");
  check(d.load().value().content_digest != content,
        "content digest ignores a basis edit");
  const auto unbased = d.load().value().content_digest;
  d.write("shipped/a.lua",
          shipped_profile("a", "{ provider = \"a\" }",
                          "sources = { s = { url = \"https://s.example\", "
                          "read = \"2026-10-19\" } }, turn = { close = "
                          "\"none\" }"));
  check(d.load().value().digest == first, "set digest changed by a sources edit");
  check(d.load().value().content_digest != unbased, "content digest ignores sources");
  const auto sourced = d.load().value().content_digest;
  d.write("shipped/a.lua",
          "return { schema = \"blackbird.provider-profile/1\", id = \"a\", "
          "revision = 2, match = { provider = \"a\" }, sources = { s = { "
          "url = \"https://s.example\", read = \"2026-10-19\" } }, turn = "
          "{ close = \"none\" } }");
  check(d.load().value().digest == first, "set digest changed by a revision edit");
  check(d.load().value().content_digest != sourced, "content digest ignores revision");
  d.write("shipped/a.lua",
          shipped_profile("a", "{ provider = \"b\" }", "turn = { close = \"none\" }"));
  const auto rematched = d.load().value().digest;
  check(rematched != first, "set digest ignores match");
  d.write("operator/a-local.lua",
          override_profile("a-local", "a", "turn = { close = \"none\" }"));
  check(d.load().value().digest != rematched, "set digest ignores operator overrides");

  // Profiles are native Values that round-trip through BBM2.
  const auto profile = effective_of(head + "turn = { close = \"none\" } }");
  const auto packet = encode_packet(profile);
  check(packet.has_value(), "BBM2 encode");
  const auto decoded = decode_packet(packet.value());
  check(decoded.has_value() && decoded.value() == profile, "BBM2 round trip");
}

// Repository test over profiles/providers: every file loads, every shipped profile is
// valid over conservative, every behavioural leaf carries a basis, and the rows that
// reproduce today's hard-coded decisions agree with the code that makes them.
void shipped(const std::filesystem::path &directory) {
  const auto set = load_profile_set({directory, std::nullopt});
  check(set.has_value(), "shipped set: " + shown(set));
  std::vector<const ProfileFile *> all{&set.value().conservative};
  for (const auto &file : set.value().shipped)
    all.push_back(&file);
  for (const auto *file : all) {
    auto profile = set.value().conservative.layer;
    if (file != &set.value().conservative)
      profile = merge_override(profile, file->layer);
    const auto missing = missing_basis(profile);
    std::string list;
    for (const auto &path : missing)
      list += " " + path;
    check(missing.empty(), file->file + " lacks a basis for:" + list);
    // A repository-path source must name a file in this checkout.
    if (const auto *sources = file->layer.find("sources"))
      for (const auto &[name, source] : sources->object())
        if (const auto *path = source.find("path"))
          check(std::filesystem::is_regular_file(directory.parent_path().parent_path() /
                                                 path->string()),
                file->file + ": source " + name + " names a missing file " +
                    path->string());
  }
  const auto resolve = [&](const ProfileQuery &q) {
    const auto r = resolve_profile(set.value(), q);
    check(r.has_value(), "resolve shipped: " + shown(r));
    return r.value();
  };
  const auto anthropic55 =
      resolve({"anthropic", "messages", "https://api.anthropic.com/v1", "oauth",
               "claude-opus-5-5"});
  const auto anthropic45 =
      resolve({"anthropic", "messages", "https://api.anthropic.com/v1", "oauth",
               "claude-sonnet-4-5-20250929"});
  const auto openai =
      resolve({"openai", "responses", "https://chatgpt.com/backend-api/codex",
               "codex_chatgpt", "gpt-6.1-sol"});
  const auto openrouter =
      resolve({"openrouter", "responses", "https://openrouter.ai/api/v1", "api_key",
               "openai/gpt-6.1-sol"});
  const auto registered =
      resolve({"paper", "responses-portable", "https://paper.example/v1", "api_key",
               "ultramarine"});
  check(anthropic55.id == "anthropic-messages-prefix-checked", "Anthropic 5.5 profile");
  check(anthropic45.id == "anthropic-messages", "Anthropic earlier profile");
  check(openai.id == "openai-chatgpt-responses", "OpenAI ChatGPT profile");
  check(openrouter.id == "openrouter-responses", "OpenRouter profile");
  check(registered.id == "conservative", "registered routes use conservative");
  // Plan §1.4: these Anthropic rows ship with basis `unobserved`.
  for (const auto &[record, leaf] : std::vector<std::pair<std::string, std::string>>{
           {"turn", "close"},
           {"turn", "start"},
           {"reasoning", "at_cut"},
           {"request_state", "placement"},
           {"reasoning", "side_request_reasoning"}}) {
    const auto b = basis_of(anthropic55.profile, record + "." + leaf);
    check(b && b->starts_with("unobserved"),
          "Anthropic " + record + "." + leaf + " must be unobserved");
  }
  check(text_at(anthropic55.profile, "reasoning.at_cut") == "drop_after_change",
        "prefix-checked models drop reasoning after a change");
  check(text_at(anthropic45.profile, "reasoning.at_cut") == "keep",
        "earlier Anthropic models keep reasoning");
  // No row is enabled or probed before its probe (plan §1.4).
  for (const auto *p :
       {&anthropic55, &anthropic45, &openai, &openrouter, &registered}) {
    check(at(p->profile, "turn.boundary.enabled") == Value{false},
          p->id + ": boundary enabled before its probe");
    check(text_at(p->profile, "tool_restriction.mode") == "palette_only",
          p->id + ": tool restriction before its probe");
  }
  // Today's request-state placement (CodingProvider::retain_request_state) is the row.
  AnthropicCodingProvider anthropic_provider;
  OpenAiCodingProvider openai_provider;
  OpenRouterCodingProvider openrouter_provider;
  RegisteredResponsesCodingProvider registered_provider{{"paper", {}, 180}};
  const auto placement = [](const CodingProvider &provider) {
    return provider.retain_request_state() ? "retained_in_place" : "request_local";
  };
  check(text_at(anthropic55.profile, "request_state.placement") ==
            placement(anthropic_provider),
        "Anthropic placement differs from today's provider");
  check(text_at(anthropic45.profile, "request_state.placement") ==
            placement(anthropic_provider),
        "Anthropic placement differs from today's provider");
  check(text_at(openai.profile, "request_state.placement") ==
            placement(openai_provider),
        "OpenAI placement differs from today's provider");
  check(text_at(openrouter.profile, "request_state.placement") ==
            placement(openrouter_provider),
        "OpenRouter placement differs from today's provider");
  check(text_at(registered.profile, "request_state.placement") ==
            placement(registered_provider),
        "registered placement differs from today's provider");
  // Queries come from real bindings (ProviderAuth::binding,
  // src/provider_auth.cpp:790-810; the ChatGPT route's binding,
  // src/openai.cpp:157-163). OpenRouter's descriptor names `chat`, but its main-coding
  // codec is Responses (src/main.cpp:528-532).
  Directory home;
  ProviderAuthConfig auth_config;
  auth_config.directory = home.path / "auth";
  const ProviderAuth auth{auth_config};
  const auto openrouter_binding = auth.binding("openrouter");
  check(text_at(openrouter_binding, "protocol") == "chat",
        "OpenRouter's binding names its descriptor protocol");
  const auto from_binding = [&](const Value &binding, std::string_view model) {
    const auto q = profile_query(binding, model);
    check(q.has_value(), "query from binding: " + shown(q));
    return q.value();
  };
  const auto openrouter_query = from_binding(openrouter_binding, "openai/gpt-6.1-sol");
  check(openrouter_query.protocol == "responses" &&
            openrouter_query.account_kind == "environment" &&
            openrouter_query.base_url == "https://openrouter.ai/api/v1",
        "OpenRouter query fields");
  check(resolve(openrouter_query).id == "openrouter-responses",
        "the real OpenRouter binding selects its profile");
  check(resolve({"openrouter", "chat", "https://openrouter.ai/api/v1", "environment",
                 "openai/gpt-6.1-sol"})
                .id == "conservative",
        "the raw descriptor protocol would miss the OpenRouter profile");
  check(resolve(from_binding(auth.binding("anthropic"), "claude-opus-5-5")).id ==
            "anthropic-messages-prefix-checked",
        "the real Anthropic binding selects its profile");
  const auto owned_query = from_binding(auth.binding("openai"), "gpt-6.1-sol");
  check(owned_query.base_url == "https://api.openai.com/v1" &&
            owned_query.protocol == "responses" &&
            resolve(owned_query).id == "conservative",
        "the owned OpenAI route is unprofiled and uses conservative");
  const auto chatgpt_binding =
      Value::object({{"provider", Value{"openai"}},
                     {"account", Value{"acct"}},
                     {"revision", Value{Number{std::string_view{"-1"}}}},
                     {"kind", Value{"codex_chatgpt"}},
                     {"base_url", Value{"https://chatgpt.com/backend-api/codex"}},
                     {"protocol", Value{"responses"}}});
  const auto chatgpt_query = from_binding(chatgpt_binding, "gpt-6.1-sol");
  check(chatgpt_query.account_kind == "codex_chatgpt" &&
            resolve(chatgpt_query).id == "openai-chatgpt-responses",
        "the ChatGPT binding selects its profile");
  const auto registered_binding =
      Value::object({{"provider", Value{"paper"}},
                     {"account", Value{"main"}},
                     {"revision", Value{Number{1}}},
                     {"kind", Value{"api_key"}},
                     {"base_url", Value{"https://Paper.Example/v1/"}},
                     {"protocol", Value{"responses-portable"}}});
  const auto registered_query = from_binding(registered_binding, "ultramarine");
  check(registered_query.protocol == "responses-portable" &&
            registered_query.base_url == "https://paper.example/v1" &&
            resolve(registered_query).id == "conservative",
        "a registered Responses binding");
  auto chat_binding = registered_binding;
  for (auto &[key, value] : chat_binding.object())
    if (key == "protocol")
      value = Value{"chat"};
  const auto chat = profile_query(chat_binding, "m");
  check(!chat.has_value() &&
            chat.error() ==
                ProfileError{"binding", 0, 0, "binding.protocol",
                             "`chat` is not a main-coding codec: main coding uses "
                             "Responses or native Anthropic Messages"},
        "a chat binding is refused: " + shown(chat));
  // Design §7.4: OpenAI and Anthropic list `boundary` while it is disabled.
  for (const auto *p : {&anthropic55, &anthropic45, &openai}) {
    const auto &modes = at(p->profile, "turn.open_turn").array();
    check(std::ranges::find(modes, Value{"boundary"}) != modes.end(),
          p->id + " must list boundary in open_turn");
  }
  // Anthropic selection (owner decision): prefix-checked is the default for Anthropic
  // Messages; only older families that do not run the prefix check select
  // anthropic-messages, each with its suffixed variants. Unknown families are safe.
  const auto anthropic_model = [&](std::string_view model) {
    return resolve({"anthropic", "messages", "https://api.anthropic.com/v1", "oauth",
                    std::string{model}})
        .id;
  };
  for (const std::string family :
       {"claude-fable-5-1", "claude-opus-5-5", "claude-sonnet-5-5", "claude-haiku-5-5"})
    for (const std::string suffix : {"", "-20261001", "-latest"})
      check(anthropic_model(family + suffix) == "anthropic-messages-prefix-checked",
            family + suffix + " must be prefix-checked");
  for (const std::string family :
       {"claude-mythos-5-1", "claude-opus-4-8", "claude-opus-4-7", "claude-opus-4-6",
        "claude-opus-4-5", "claude-opus-4-1", "claude-sonnet-4-6", "claude-sonnet-4-5",
        "claude-haiku-4-5", "claude-3-7-sonnet"})
    for (const std::string suffix : {"", "-20250929"})
      check(anthropic_model(family + suffix) == "anthropic-messages",
            family + suffix + " is an older family");
  for (const std::string unknown :
       {"claude-opus-6-0", "claude-opus-5-50", "claude-fable-6", "claude-opus-4-10",
        "claude-sonnet-4-50", "claude-unknown"})
    check(anthropic_model(unknown) == "anthropic-messages-prefix-checked",
          unknown + " is unknown and must land on the prefix-checked side");
  // The effective profile and the set are BBM2-encodable.
  check(encode_packet(anthropic55.profile).has_value(), "BBM2 encode shipped profile");
  check(set.value().digest.starts_with("sha256:"), "shipped set digest");
  std::cout << "shipped profile set " << set.value().digest << '\n';
}
} // namespace
// P1b's records and presentation over a layered set: the content digest moves with
// provenance where the behavioural one does not; the leaves are exactly the
// behavioural leaves, their basis empty exactly where missing_basis names them; the set
// record carries every layer as loaded and survives BBM2; the request record and the
// session-override lookup are exact.
void records() {
  Directory d;
  d.write("shipped/conservative.lua", conservative_text);
  const auto acme_body =
      std::string{"errors = { { class = \"overloaded\", status = 529, basis = "
                  "\"probed:fixture\" } }, stop_reasons = { end_turn = \"completed\", "
                  "max_tokens = \"output_limit\", basis = { end_turn = "
                  "\"probed:fixture\" } }"};
  d.write("shipped/acme.lua",
          shipped_profile("acme", "{ provider = \"acme\", protocol = \"responses\" }",
                          acme_body));
  d.write(
      "operator/acme-local.lua",
      override_profile("acme-local", "acme", "turn = { close = \"reply_request\" }"));
  const auto set = d.load();
  check(set.has_value(), "records set: " + shown(set));
  const auto &s = set.value();
  // Content digest: whole layer, provenance included.
  const auto acme = parse(shipped_profile(
      "acme", "{ provider = \"acme\", protocol = \"responses\" }", acme_body));
  const auto rebased = parse(
      shipped_profile("acme", "{ provider = \"acme\", protocol = \"responses\" }",
                      std::string{acme_body}.replace(acme_body.find("probed:fixture"),
                                                     14, "probed:another")));
  check(profile_digest(acme) == profile_digest(rebased),
        "a basis edit moved the behavioural digest");
  check(profile_content_digest(acme) != profile_content_digest(rebased),
        "a basis edit must move the content digest");
  check(profile_content_digest(acme) == profile_content_digest(parse(shipped_profile(
                                            "acme",
                                            "{ provider = \"acme\", protocol = "
                                            "\"responses\" }",
                                            acme_body))) &&
            profile_content_digest(acme) != profile_digest(acme),
        "content digest stability and domain");
  // Resolution through a binding, and every shipped profile resolved.
  const auto binding = Value::object({{"provider", Value{"acme"}},
                                      {"account", Value{"a"}},
                                      {"revision", Value{Number{1}}},
                                      {"kind", Value{"api_key"}},
                                      {"base_url", Value{"https://Acme.Example/v1/"}},
                                      {"protocol", Value{"responses"}}});
  const auto via_binding = resolve_binding(s, binding, "m");
  const auto direct = resolve_profile(
      s, {"acme", "responses", "https://acme.example/v1", "api_key", "m"});
  check(via_binding.has_value() && direct.has_value() &&
            via_binding.value().digest == direct.value().digest &&
            via_binding.value().layers ==
                std::vector<std::string>{"shipped:conservative/1", "shipped:acme/1",
                                         "operator:acme-local/3"},
        "resolve_binding: " + shown(via_binding));
  auto chat = binding;
  chat.object().back().second = Value{"chat"};
  check(!resolve_binding(s, chat, "m").has_value(), "a chat binding must be refused");
  const auto all = effective_profiles(s);
  check(all.has_value() && all.value().size() == 2 &&
            all.value()[0].id == "conservative" && all.value()[1].id == "acme" &&
            all.value()[1].digest == direct.value().digest &&
            all.value()[0].digest ==
                resolve_profile(s, {"other", "responses", "", "api_key", "m"})
                    .value()
                    .digest,
        "effective_profiles");
  // Leaves: the behavioural leaves; basis empty exactly where missing_basis says.
  const auto &profile = direct.value().profile;
  const auto leaves = profile_leaves(profile);
  const auto missing = missing_basis(profile);
  std::vector<std::string> unbased;
  for (const auto &leaf : leaves)
    if (leaf.basis.empty())
      unbased.push_back(leaf.path);
  check(unbased == missing, "leaves without basis differ from missing_basis");
  const auto find = [&](std::string_view path) -> const ProfileLeaf & {
    for (const auto &leaf : leaves)
      if (leaf.path == path)
        return leaf;
    throw std::runtime_error("no leaf " + std::string{path});
  };
  check(find("turn.close").value == Value{"reply_request"} &&
            find("turn.close").basis.empty() &&
            find("turn.start").basis == "conservative" &&
            find("turn.boundary.enabled").value == Value{false} &&
            find("errors[1].status").value == Value{Number{529}} &&
            find("errors[1].status").basis == "probed:fixture" &&
            find("stop_reasons.end_turn").basis == "probed:fixture" &&
            find("stop_reasons.max_tokens").basis.empty() &&
            find("retry.http").value.array().size() == 6,
        "leaf values and bases");
  for (const auto &leaf : leaves)
    check(!leaf.path.starts_with("match") && !leaf.path.starts_with("sources") &&
              !leaf.path.ends_with("basis") && leaf.path != "id" &&
              leaf.path != "schema" && leaf.path != "revision",
          "a non-behavioural leaf: " + leaf.path);
  // The set record: every layer as loaded, both set digests, survives BBM2.
  const ProfileDirectories where{d.path / "shipped", d.path / "operator"};
  const auto record = profile_set_record(s, where);
  check(
      record.find("label")->string() == profile_set_record_label &&
          record.find("digest")->string() == s.digest &&
          record.find("content_digest")->string() == s.content_digest &&
          record.find("shipped_directory")->string() == (d.path / "shipped").string() &&
          record.find("operator_directory")->string() == (d.path / "operator").string(),
      "set record fields");
  const auto &layers = record.find("layers")->array();
  check(layers.size() == 3, "set record layers");
  const std::vector<const ProfileFile *> files{&s.conservative, &s.shipped[0],
                                               &s.operator_overrides[0]};
  for (std::size_t i = 0; i < layers.size(); ++i) {
    const auto &entry = layers[i];
    const auto &layer = files[i]->layer;
    check(*entry.find("id") == *layer.find("id") &&
              entry.find("layer")->string() == (i == 2 ? "operator" : "shipped") &&
              (i == 2) == (entry.find("target") != nullptr) &&
              entry.find("digest")->string() == profile_digest(layer) &&
              entry.find("content_digest")->string() == profile_content_digest(layer) &&
              *entry.find("body") == layer,
          "set record layer " + std::to_string(i));
  }
  const auto bytes = encode_packet(record);
  check(bytes.has_value(), "set record encodes");
  const auto decoded = decode_packet(bytes.value());
  check(decoded.has_value() && decoded.value() == record, "set record BBM2 round trip");
  // The request record: the route's non-secret fields only, the request's own model.
  const auto route = route_query(binding, "m");
  check(route.has_value() && route.value().fallback.empty(), "route query");
  const auto route_digest = binding_digest(route.value().query);
  auto renamed = binding;
  renamed.object()[1].second =
      Value{"another-account"}; // the account is not a route field
  check(binding_digest(route_query(renamed, "other-model").value().query) ==
                route_digest &&
            binding_digest({"acme", "responses", "https://acme.example/v1", "oauth",
                            "m"}) != route_digest &&
            route_digest.starts_with("sha256:"),
        "binding digest covers provider, protocol, base_url and account_kind only");
  const auto request = profile_request_record(direct.value(), route.value());
  check(request == Value::object(
                       {{"id", Value{"acme"}},
                        {"revision", Value{Number{1}}},
                        {"digest", Value{direct.value().digest}},
                        {"layers", Value{Value::Array{Value{"shipped:conservative/1"},
                                                      Value{"shipped:acme/1"},
                                                      Value{"operator:acme-local/3"}}}},
                        {"model", Value{"m"}},
                        {"binding", Value{route_digest}}}),
        "request record");
  // A base URL that cannot be normalized falls back to conservative, saying why
  // (owner decision 2026-10-20); profile_query still refuses it.
  for (const auto &[url, fault] : std::vector<std::pair<std::string, std::string>>{
           {"https://[::1]:8443/v1", "binding.base_url: "},
           {"https://my_host.example/v1", "binding.base_url: "},
           {"https://acme.example/" + std::string(600, 'p'), "binding.base_url: "}}) {
    auto odd = binding;
    odd.object()[4].second = Value{url};
    const auto fallback = route_query(odd, "m");
    check(
        fallback.has_value() && fallback.value().fallback.starts_with(fault) &&
            fallback.value().fallback.ends_with("; the conservative profile applies") &&
            fallback.value().query.base_url == url,
        "fallback route for " + url.substr(0, 40) + ": " + shown(fallback));
    check(!profile_query(odd, "m").has_value(), "profile_query still refuses " + url);
    const auto resolved = resolve_binding(s, odd, "m");
    check(resolved.has_value() && resolved.value().id == "conservative" &&
              resolved.value().fallback == fallback.value().fallback &&
              resolved.value().layers ==
                  std::vector<std::string>{"shipped:conservative/1"},
          "fallback resolution: " + shown(resolved));
    // Per request the record is bounded: a code, at most 256 bytes of the URL, and its
    // full length; the full reason belongs to the set record once.
    const auto recorded = profile_request_record(resolved.value(), fallback.value());
    const auto *bounded = recorded.find("fallback");
    check(bounded &&
              *bounded ==
                  Value::object(
                      {{"code", Value{"base_url_malformed"}},
                       {"base_url",
                        Value{url.substr(0, std::min<std::size_t>(url.size(), 256))}},
                       {"base_url_bytes", Value{Number{url.size()}}}}),
          "the bounded fallback is recorded for " + url.substr(0, 40));
  }
  {
    auto plain = binding;
    plain.object()[4].second = Value{"http://acme.example/v1"};
    const auto route_plain = route_query(plain, "m");
    check(route_plain.has_value() &&
              route_plain.value().fallback_code == "base_url_not_https" &&
              route_plain.value().fallback ==
                  "binding.base_url: `http://acme.example/v1` is not an https URL; the "
                  "conservative profile applies",
          "a non-https route: " + shown(route_plain));
  }
  // The session override: absent, the latest of several, or refused without a value.
  const auto labelled = [](Value body) {
    return Value::object({{"label", Value{std::string{profile_override_record_label}}},
                          {"override", std::move(body)}});
  };
  check(session_override({record}).value() == std::nullopt, "no session override");
  const auto first = labelled(Value{"first"});
  const auto second = labelled(Value{"second"});
  const auto latest = session_override({first, record, second});
  check(latest.has_value() && latest.value() == Value{"second"}, "latest override");
  const auto bare =
      Value::object({{"label", Value{std::string{profile_override_record_label}}}});
  const auto refused = session_override({bare});
  check(!refused.has_value() &&
            refused.error() == ProfileError{std::string{profile_override_record_label},
                                            0, 0, "override", "required"},
        "an override record without a value: " + shown(refused));
}
int main(int argc, char **argv) {
  try {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "schema")
      schema();
    else if (mode == "effective")
      effective();
    else if (mode == "layering")
      layering();
    else if (mode == "match")
      match();
    else if (mode == "digest")
      digest();
    else if (mode == "records")
      records();
    else if (mode == "shipped" && argc > 2)
      shipped(argv[2]);
    else {
      std::cerr << "usage: provider_profile_test "
                   "schema|effective|layering|match|digest|records|shipped PROFILES\n";
      return 2;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
