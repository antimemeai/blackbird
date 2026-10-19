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
Value process_output_presentation(const std::string &raw, const Value &args);
Value file_content_presentation(const std::string &raw, const Value &args,
                                RawFileToolTrace *trace = nullptr);
Value tool_definitions();
} // namespace blackbird
