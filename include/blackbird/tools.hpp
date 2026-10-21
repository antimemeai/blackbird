#pragma once
#include "blackbird/context.hpp"
#include "blackbird/file_effect.hpp"
#include "blackbird/provider_original.hpp"
#include <filesystem>
#include <functional>
namespace blackbird {
struct RawFileInput;
class RawFileToolTrace;
struct CapturedOutput {
  std::string label;
  std::string bytes;
};
class LocalTools {
public:
  using Observer = std::function<void(std::string_view, std::string_view)>;
  explicit LocalTools(Observer observer = {},
                      std::size_t original_limit = 16 * 1024 * 1024)
      : observer_(std::move(observer)), original_limit_(original_limit) {}
  std::function<bool()> cancelled;
  Value run(std::string_view name, const Value &arguments,
            const RawAdapterContext *raw_context = nullptr,
            RawControlToken *native_return = nullptr,
            FileEffectObservation *file_effect = nullptr);
  const std::vector<CapturedOutput> &captured() const noexcept { return captured_; }

private:
  std::vector<CapturedOutput> captured_;
  Observer observer_;
  std::size_t original_limit_;
  void capture(std::string label, std::string raw, RawFileToolTrace * = nullptr);
};
std::string read_file_prefix(const std::filesystem::path &, std::size_t,
                             const RawAdapterContext * = nullptr,
                             RawEventRef *actual_owner = nullptr);
std::string read_file(const std::filesystem::path &path,
                      std::size_t limit = 16 * 1024 * 1024);
void write_file(const std::filesystem::path &path, std::string_view bytes);
void write_file_observed(const std::filesystem::path &, std::string_view,
                         FileEffectObservation *);
std::string read_file(const std::filesystem::path &, std::size_t,
                      const RawAdapterContext *, RawEventRef *actual_owner = nullptr);
void write_file(const std::filesystem::path &, std::string_view,
                const RawAdapterContext *, const RawFileInput * = nullptr,
                FileEffectObservation * = nullptr);
std::size_t observation_prefix_size(std::string_view raw, std::size_t budget);
// Generic shaping at creation (design §9) uses exec's budget semantics over a
// result's text: 16384 bytes, 4096 head + 12288 tail by default; output_max_bytes
// alone is head-first; output_head_bytes alone keeps the default tail after it.
struct ResultShape {
  std::size_t max_bytes = 16384;
  std::optional<std::size_t> head_bytes = 4096; // Absent: head-first.
};
// Reads output_max_bytes/output_head_bytes from keys (other keys are ignored);
// values are integers 0..16777216 and a head never exceeds its budget.
ResultShape result_shape(const Value &keys);
// The text a provider receives for a tool result: a UTF-8 string as is, any other
// value as its JSON. Shaping and result_ref pages measure the same bytes.
inline constexpr ValueLimits result_text_limits{96 * 1024 * 1024};
std::string result_text(const Value &result);
// text unchanged within its budget; otherwise head, one marker line naming the
// omitted half-open byte range and how to read it, and tail. Cuts keep UTF-8
// characters and, in JSON text, escape sequences whole.
std::string generic_presentation(std::string_view text, bool json,
                                 const ResultShape &shape, std::string_view result_ref);
// Shapes a result whose original is retained under result_ref, where it becomes
// visible to the model (T1). presented is the result itself when its text is within
// the budget or the cut would not be shorter, else the cut string; when no text can
// be produced it is an error result naming the reason. summary is the shape applied:
// {max_bytes, head_bytes (absent head-first), tail_bytes, text_bytes, and whole=true
// or omitted={start,end} with result_ref}, or {error} when no text exists.
struct ShapedResult {
  Value presented, summary;
};
ShapedResult shape_result(const Value &result, const ResultShape &shape,
                          std::string_view result_ref);
Value process_output_presentation(const std::string &raw, const Value &args);
Value file_content_presentation(const std::string &raw, const Value &args,
                                RawFileToolTrace *trace = nullptr);
Value tool_definitions();
} // namespace blackbird
