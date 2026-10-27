#pragma once
#include <algorithm>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace blackbird {
// Optional transport diagnostics, not a durability boundary for the response.
// At most one block is pending; a crash may discard that block. Completion and
// ordinary transport errors flush it explicitly. Never retry an uncertain sink.
class StreamCapture {
public:
  static constexpr std::size_t block_bytes = 64 * 1024;
  explicit StreamCapture(std::function<void(std::string_view)> sink)
      : sink_(std::move(sink)) {
    pending_.reserve(block_bytes);
  }
  void append(std::string_view bytes) {
    if (failed_)
      throw std::logic_error("stream capture sink already failed");
    while (!bytes.empty()) {
      const auto count = std::min(block_bytes - pending_.size(), bytes.size());
      pending_.append(bytes.substr(0, count));
      bytes.remove_prefix(count);
      if (pending_.size() == block_bytes)
        flush();
    }
  }
  void flush() {
    if (failed_ || pending_.empty())
      return;
    try {
      sink_(pending_);
    } catch (...) {
      failed_ = true;
      throw;
    }
    pending_.clear();
    flushed_ = true;
  }
  std::size_t pending_bytes() const { return pending_.size(); }
  // The whole stream so far while no block has been flushed, such as a short
  // provider error body; nullopt once earlier bytes left for the sink.
  std::optional<std::string_view> whole() const {
    if (flushed_)
      return std::nullopt;
    return std::string_view{pending_};
  }

private:
  std::function<void(std::string_view)> sink_;
  std::string pending_;
  bool failed_ = false, flushed_ = false;
};
} // namespace blackbird
