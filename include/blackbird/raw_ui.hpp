#pragma once
#include "blackbird/raw_caller.hpp"
#include "blackbird/raw_provider.hpp"
#include "blackbird/retained_events.hpp"
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
namespace blackbird {
// Offset and length address the original text payload, after decoding the
// original-bearing site; they never address its raw record header.
struct RawUiSource {
  RawEventRef record{};
  std::size_t offset = 0, length = 0;
};
struct RawUiInput {
  bool enabled = false;
  std::optional<RawEventRef> keyboard;
  std::size_t index = 0;
  RawCapacityClaim *data = nullptr;
  RawControlToken *control = nullptr;
  RawUiSource acquired{};
  RawEventRef feed{};
};
struct RawUiValueOrigin {
  std::uint8_t kind = 0;
  std::optional<RawEventRef> source;
  std::optional<ApplicationRecordId> fact;
  std::optional<std::uint64_t> revision;
};
inline constexpr std::uint64_t raw_ui_error_bytes =
    raw_envelope_size + 24 + 2 + sizeof(ErrorCode) + sizeof(std::int64_t);
inline constexpr std::uint64_t raw_ui_ref_bytes = 24;
inline constexpr std::uint64_t raw_ui_prompt_withdraw_credit =
    raw_envelope_size + 2 * 24 + 1 + sizeof(std::size_t);
inline constexpr std::uint64_t raw_ui_feed_credit =
    raw_envelope_size + sizeof(bool) + 24 + sizeof(std::size_t) + sizeof(char) +
    raw_envelope_size + 24 + 3 * sizeof(std::size_t) + sizeof(int) + raw_ui_error_bytes;
inline constexpr std::uint64_t raw_ui_handler_credit =
    raw_envelope_size + 24 + raw_envelope_size + 2 * 24 + 1;
inline constexpr std::uint64_t raw_ui_enqueue_credit =
    raw_envelope_size + 2 * 24 + 1 + 2 * sizeof(std::size_t) + raw_ui_error_bytes;
inline constexpr std::uint64_t raw_ui_keyboard_credit =
    128 * (raw_ui_feed_credit + raw_ui_handler_credit + raw_ui_enqueue_credit +
           raw_ui_prompt_withdraw_credit) +
    256 * raw_ui_prompt_withdraw_credit;
inline constexpr std::uint64_t raw_ui_keyboard_batch_credit =
    raw_envelope_size + sizeof(int) + sizeof(std::size_t) + raw_envelope_size + 2 * 24 +
    sizeof(ssize_t) + raw_ui_error_bytes + 2 * sizeof(std::size_t);
bool raw_ui_source_valid(RawEventRef source) noexcept;
std::uint64_t raw_ui_record_bytes(std::span<const ByteView> fields);
std::optional<RawControlToken> raw_ui_control(bool enabled, std::uint64_t bytes,
                                              RawWaitControl wait = raw_wait_control());
std::optional<RawCapacityClaim> raw_ui_data(bool enabled, std::uint64_t bytes,
                                            RawWaitControl wait = raw_wait_control());
RawEventRef raw_ui_emit(std::uint32_t site, std::span<const ByteView> fields,
                        RawPartition partition, RawControlToken *credit,
                        Error *failure = nullptr) noexcept;
RawEventRef raw_ui_link(std::uint32_t site, RawEventRef parent,
                        std::span<const ByteView> fields,
                        RawControlToken *credit) noexcept;
class RawUiBurst {
public:
  explicit RawUiBurst(RawControlToken *control) noexcept;
  ~RawUiBurst();
  RawUiBurst(const RawUiBurst &) = delete;
  RawUiBurst &operator=(const RawUiBurst &) = delete;

private:
  RawControlToken *previous_control_;
};
// Request and finite actual outcomes on the sole actual caller producer.
struct RawUiSites {
  std::uint32_t request, terminal;
};
class RawUiTrace {
public:
  RawUiTrace(bool enabled, RawUiSites sites, std::span<const ByteView> fields,
             std::uint64_t credit, bool data_request = false,
             RawWaitControl wait = raw_wait_control(),
             RawCapacityClaim *preheld_data = nullptr);
  ~RawUiTrace();
  RawUiTrace(const RawUiTrace &) = delete;
  RawUiTrace &operator=(const RawUiTrace &) = delete;
  RawEventRef reference() const noexcept { return emission_.reference(); }
  bool enabled() const noexcept { return emission_.enabled(); }
  RawControlToken *credit() noexcept { return emission_.bank(); }
  void stage(std::uint8_t value) noexcept { stage_ = value; }
  void progress(std::size_t value) noexcept { progress_ = value; }
  void extent(std::size_t value) noexcept { extent_ = value; }
  RawEventRef event(std::uint32_t site, std::span<const ByteView> fields) noexcept;
  void finish(std::uint8_t branch = 0, std::optional<Error> error = {}) noexcept;
  void finish(RawOperationBranch branch, const Error *error = nullptr) noexcept;
  void capture_exception(std::uint32_t site) noexcept;
  void finish_system(int code) noexcept;
  void close() noexcept { emission_.release(); }
  std::optional<RawControlToken> split(std::uint64_t bytes);
  template <class F> decltype(auto) run(F &&body) {
    return raw_call_run<false>(*this, std::forward<F>(body));
  }

private:
  RawEmission emission_;
  std::uint32_t error_site_;
  std::uint8_t stage_ = 0;
  std::size_t progress_ = 0, extent_ = 0;
};
RawUiTrace raw_ui_value_trace(bool enabled, const RawUiValueOrigin &origin);
void raw_ui_notify(RawUiTrace &trace, int descriptor) noexcept;
// Selected holders belong to their creating thread. The retained handle keeps
// that exact producer alive; temporary binding restores any previous issuer.
class RawUiOwner {
public:
  RawUiOwner() = default;
  explicit RawUiOwner(bool selected);
  void require_thread() const;
  void require_cleanup_thread() const noexcept;
  bool selected() const noexcept { return static_cast<bool>(issuer_); }
  RawThreadHandle issuer() const noexcept { return issuer_; }

private:
  RawThreadHandle issuer_;
  std::thread::id thread_;
};
class RawUiOwnerBinding {
public:
  explicit RawUiOwnerBinding(const RawUiOwner &owner);

private:
  std::optional<RawThreadBinding> binding_;
};
class ChatOutput;
class RawUiPacket {
public:
  RawUiPacket() = default;
  RawUiPacket(RawUiPacket &&other) noexcept;
  RawUiPacket &operator=(RawUiPacket &&other) noexcept;
  ~RawUiPacket();

private:
  friend class RawUiFrame;
  friend class ChatOutput;
  RawUiPacket(RawUiOwner issuer, RawEventRef source,
              std::optional<RawControlToken> credit) noexcept;
  void retire() noexcept;
  RawUiOwner owner;
  RawEventRef frame{};
  std::optional<RawControlToken> terminal;
};
struct RawUiGeometry {
  std::size_t width = 0, height = 0, cursor_row = 0, cursor_column = 0;
};
class RawUiFrame {
public:
  RawUiFrame(bool enabled, RawUiGeometry geometry, std::uint64_t packet_bound,
             RawCapacityClaim *preheld_data = nullptr);
  RawUiFrame(bool enabled, std::size_t packet_bound,
             RawCapacityClaim *preheld_data = nullptr);
  RawEventRef reference() const noexcept { return trace_.reference(); }
  ~RawUiFrame();
  void capture(std::string_view packet);
  RawUiPacket handoff();
  void discard();
  template <class F> decltype(auto) run(F &&body) {
    RawUiOwnerBinding binding{owner_};
    return trace_.run(std::forward<F>(body));
  }

private:
  RawUiOwner owner_;
  bool captured_ = false;
  RawUiTrace trace_;
  std::optional<RawCapacityClaim> data_;
  std::optional<Error> failure_;
};
std::uint64_t raw_ui_add(std::uint64_t left, std::uint64_t right);
std::uint64_t raw_ui_multiply(std::uint64_t left, std::uint64_t right);
std::string_view raw_ui_description() noexcept;
} // namespace blackbird
