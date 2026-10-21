#pragma once
#include "blackbird/foundation.hpp"
#include "blackbird/value.hpp"
#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The cut and repair context frames (CONTEXT_LIFECYCLE.md §4.2-§4.5) and the one
// apply function that the frame writer and playback share (§4.3).
namespace blackbird {
// One context entry: the 16-byte identity of the record that created it and the
// entry's ordinal there, the entry ID "<32 lowercase hex>.<decimal ordinal>". On
// the wire a ref is the native pair [identity, ordinal], never ID text.
struct ContextRef {
  IdentityBytes record{};
  std::uint32_t ordinal = 0;
  auto operator<=>(const ContextRef &) const = default;
};
// The ref an entry ID names; empty for any other text (canonical form only).
std::optional<ContextRef> parse_entry_id(std::string_view id) noexcept;
std::string entry_id(const ContextRef &ref);
Value encode_ref(const ContextRef &ref);
// Throws Error{corrupt} unless the value is exactly [identity, u32 ordinal].
ContextRef decode_ref(const Value &value);

enum class CutOp : std::uint8_t { cut, repair };
enum class CutKind : std::uint8_t { start, archival, rebind, prefix, recovery };
// Bits of CutFrame::changes.
enum CutChange : std::uint8_t {
  cut_archive = 1,
  cut_edit = 2,
  cut_restore = 4,
  cut_summary = 8,
  cut_compaction = 16,
  cut_boundary = 32
};
enum class CutSource : std::uint8_t {
  model,
  step,
  operator_,
  recovery,
  rebind,
  prefix,
  start
};
struct CutInsert {
  std::optional<ContextRef> anchor; // empty: position 0 ("start")
  // A fresh index into originals, or an entry this frame removed.
  std::variant<std::uint32_t, ContextRef> entry;
};
struct CutReplace {
  ContextRef ref;
  Value item;
};
struct CutOriginal {
  Value item;
  std::uint8_t entry_class = 0; // S2 gives the byte its meaning
};
// Entry count and CRC32C over the ordered refs are the playback check;
// encoded_bytes is recorded only.
struct CutResult {
  std::uint64_t count = 0;
  std::uint32_t crc32c = 0;
  std::uint64_t encoded_bytes = 0;
};
struct CutSizes {
  std::uint64_t before = 0, prefix = 0;
};
struct CutCause {
  CutSource source = CutSource::operator_;
  std::string attempt, stage; // 32 lowercase hex, or empty
  bool forced = false;
};
struct CutFrame {
  CutOp op = CutOp::cut;
  std::string revision, base, observed;
  bool accepted = true;
  // An accepted=false frame carries its reason; its other fields belong to the
  // unit that stages and settles (S3b) and are not read here.
  std::string reason;
  CutKind kind = CutKind::start;
  std::uint8_t changes = 0;
  Value chapter; // object or null; S3a owns its fields
  Value refused; // array; S3b
  Value pending; // object or null; S3b
  std::vector<ContextRef> archived, swept, reasoning;
  std::vector<CutReplace> replace;
  std::vector<CutInsert> insert;
  std::vector<CutOriginal> originals;
  std::vector<std::string> skipped; // repair: the context revisions playback skips
  std::optional<CutResult> result;
  CutSizes sizes;
  CutCause cause;
};
inline constexpr std::string_view cut_format = "cut-delta-v1";
inline constexpr std::string_view repair_format = "repair-v1";
Value encode_cut_frame(const CutFrame &frame);
// Throws Error{corrupt} for any field that is missing, of the wrong type, or
// outside its closed set.
CutFrame decode_cut_frame(const Value &packet);
// Count and CRC32C over the refs of entries ({id, item} objects) in order. With
// encoded, also the BBM2 bytes of their items. Throws corrupt for an entry
// without a ref ID.
CutResult measure_entries(const Value::Array &entries, bool encoded = false);
// Design §4.3 steps 1-6: remove, replace, insert, check the recorded result
// (when the frame has one) and unique IDs. Throws Error{corrupt} at the first
// rule that does not hold. Step 7 (every ID has an original) is the caller's.
Value::Array apply_frame(const CutFrame &frame, const Value::Array &entries);
} // namespace blackbird
