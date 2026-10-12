#pragma once
#include "blackbird/context.hpp"
#include "blackbird/provider_original.hpp"
#include <filesystem>
#include <functional>
namespace blackbird {
struct CapturedOutput {
  std::string label;
  std::string bytes;
};
class LocalTools {
public:
  using Observer = std::function<void(std::string_view, std::string_view)>;
  explicit LocalTools(Observer observer = {}) : observer_(std::move(observer)) {}
  std::function<bool()> cancelled;
  Value run(std::string_view name, const Value &arguments,
            const RawAdapterContext *raw_context = nullptr,
            RawControlToken *native_return = nullptr);
  const std::vector<CapturedOutput> &captured() const noexcept { return captured_; }

private:
  std::vector<CapturedOutput> captured_;
  Observer observer_;
  void capture(std::string label, std::string raw);
};
std::string read_file(const std::filesystem::path &path,
                      std::size_t limit = 16 * 1024 * 1024);
void write_file(const std::filesystem::path &path, std::string_view bytes);
Value process_output_presentation(const std::string &raw, const Value &args);
Value tool_definitions();
} // namespace blackbird
