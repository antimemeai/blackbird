#pragma once
// Provider profiles (docs/CONTEXT_LIFECYCLE.md §15): data-only Lua tables read by a
// native reader that executes nothing, validated natively, layered and digested.
// Profiles are native Values (BBM2-encodable); JSON is never involved.
#include "blackbird/value.hpp"
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blackbird {
// A refusal names the file, the source position (0 when none applies), the subject
// (a reader construct, or a validator field path) and the exact reason.
struct ProfileError {
  std::string file;
  std::size_t line = 0;
  std::size_t column = 0;
  std::string subject;
  std::string reason;
  // "file:line:column: subject: reason", omitting an absent position.
  std::string message() const;
  bool operator==(const ProfileError &) const = default;
};
template <class T> using ProfileResult = std::expected<T, ProfileError>;

// Reader subjects form a closed set; every refusal names one of these constructs.
inline constexpr std::string_view profile_reader_constructs[] = {
    "encoding", "size",     "statement",   "return",    "function",
    "call",     "operator", "global",      "nil",       "metatable",
    "string",   "comment",  "numeral",     "table",     "depth",
    "keyword",  "vararg",   "parenthesis", "character", "io"};

struct ProfileReaderLimits {
  std::size_t bytes = std::size_t{1} << 20U;
  std::size_t depth = 32;
  std::size_t values = 65536;
};
struct ProfileSource {
  std::string_view text;
  std::string_view file;
};
// The accepted subset: `return <table>` with optional trailing `;`; table constructors
// with `name = v`, `["string"] = v` or positional items (never mixed) separated by `,`
// or `;`; short strings with every Lua 5.4 escape; long-bracket strings; decimal
// numerals, with `-` written directly before one; `true` and `false`; `--` comments
// and long comments. An empty table reads as an empty object. Strings must be UTF-8.
// Numbers keep Lua's subtypes: an integer numeral within int64 reads as an integer; any
// other numeral (a point, an exponent, or an integer past int64) reads as an exact
// decimal, Lua's float; a float outside binary64 is refused. Every zero numeral reads
// as one unsigned zero. `\u{...}` takes any number of hex digits up to U+10FFFF.
ProfileResult<Value> read_profile_text(ProfileSource source,
                                       ProfileReaderLimits limits = {});
ProfileResult<Value> read_profile_file(const std::filesystem::path &path,
                                       ProfileReaderLimits limits = {});

inline constexpr std::string_view provider_profile_schema =
    "blackbird.provider-profile/1";
inline constexpr std::string_view provider_profile_override_schema =
    "blackbird.provider-profile-override/1";
inline constexpr std::string_view conservative_profile_id = "conservative";

enum class ProfileLayerKind : std::uint8_t { shipped, override_layer };
struct ProfileLayerInput {
  const Value &layer;
  ProfileLayerKind kind;
  std::string_view file;
};
// Shape check of one layer: unknown keys, types, enumerations, ranges, identifier and
// basis grammar. Returns the layer normalized: empty tables typed by the schema and
// every `basis` in table form (field -> basis), covering only the fields this layer
// sets. A missing basis is never refused.
ProfileResult<Value> validate_profile(ProfileLayerInput input);
// Complete-profile check of an effective profile: required fields, codec consistency,
// contradictory pairs, and `cited:` bases that name a present source.
ProfileResult<void> validate_effective(const Value &profile, std::string_view file);
// Schema-directed merge of a normalized layer over a normalized base: records and maps
// merge by key, lists replace, and a field the layer sets takes the layer's basis.
Value merge_override(const Value &base, const Value &layer);
// Behavioural leaves without a basis, as field paths ("turn.close",
// "errors[1].status").
std::vector<std::string> missing_basis(const Value &profile);
// "sha256:" + hex over the behavioural fields only: schema and every behavioural
// family; never id, revision, match, sources, target or basis.
std::string profile_digest(const Value &profile);
std::string sha256_hex(std::string_view bytes);

// What a request is matched on. Each field means exactly this:
// - provider: the session's provider name, the binding's `provider` (`openai`,
//   `anthropic`, `openrouter`, or a registered route's id);
// - protocol: the codec the main-coding route actually sends with, which is not always
//   the descriptor's protocol: `messages` (native Anthropic), `responses` (OpenAI's
//   ChatGPT route, OpenRouter, whose descriptor says `chat`, and registered `responses`
//   routes) or `responses-portable` (src/main.cpp:466-545);
// - base_url: the binding's `base_url`. Selection normalizes it (lowercase scheme and
//   host, no default port 443, no trailing `/`) and refuses a non-https or malformed
//   URL; an empty base_url constrains nothing and matches no profile that names one;
// - account_kind: the binding's `kind` (`oauth`, `api_key`, `environment`,
//   `logged_out`, `codex_chatgpt`, ...), never the account's name;
// - model: the session's model name, compared exactly or by prefix.
struct ProfileQuery {
  std::string provider;
  std::string protocol;
  std::string base_url;
  std::string account_kind;
  std::string model;
};
// The query for a binding as ProviderAuth::binding (or the ChatGPT route's
// compatibility binding) returns it. A binding whose route main coding cannot use is
// refused.
ProfileResult<ProfileQuery> profile_query(const Value &binding, std::string_view model);
// `https://host[:port][/path]` in normal form, or a refusal naming the fault.
ProfileResult<std::string> normalize_base_url(std::string_view url);
struct ProfileFile {
  std::string file;
  Value layer; // normalized
};
struct ProfileSet {
  ProfileFile conservative;
  std::vector<ProfileFile> shipped;            // other shipped profiles, sorted by id
  std::vector<ProfileFile> operator_overrides; // sorted by target
  // Behavioural: every profile's id, match and behavioural digest. A provenance edit
  // (basis, sources, revision) leaves it unchanged, so it causes no rebind.
  std::string digest;
  // Everything in every layer, provenance included: the trial record of what ran.
  std::string content_digest;
};
struct ProfileDirectories {
  std::filesystem::path shipped;
  std::optional<std::filesystem::path> operator_overrides; // absent directory: none
};
// Reads every `*.lua` file (named `<id>.lua`). Any invalid file refuses the whole set.
// An absent operator directory (ENOENT) means no overrides; any other error refuses.
// Each override is validated in every effective profile it can reach: one targeting
// conservative under every shipped profile.
ProfileResult<ProfileSet> load_profile_set(const ProfileDirectories &directories);
// The most specific matching shipped profile, or the conservative profile. Specificity:
// constrained dimensions, then an exact model over a prefix, then prefix length.
// Equal specificity is refused as ambiguous.
ProfileResult<const ProfileFile *> select_profile(const ProfileSet &set,
                                                  const ProfileQuery &query);
struct EffectiveProfile {
  std::string id;
  std::uint64_t revision = 0;
  std::string digest;
  std::vector<std::string> layers; // lowest first, "shipped:conservative/1"
  Value profile;
};
// Layers, lowest first: conservative; the operator override of conservative, which is
// the base under every shipped profile; the selected shipped profile; the operator
// override targeting it; then the session override (an override-schema Value).
ProfileResult<EffectiveProfile>
resolve_profile(const ProfileSet &set, const ProfileQuery &query,
                const Value *session_override = nullptr);
} // namespace blackbird
