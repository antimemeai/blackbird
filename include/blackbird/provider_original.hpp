#pragma once

#include "blackbird/raw_provider.hpp"
#include "blackbird/value.hpp"
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace blackbird {
inline constexpr std::uint64_t provider_ref_bytes =
    sizeof(IdentityBytes) + sizeof(std::uint64_t);
inline constexpr std::uint64_t provider_phase_begin_credit =
    raw_envelope_size + sizeof(std::uint8_t) + 2 * sizeof(bool) + provider_ref_bytes +
    3 * sizeof(IdentityBytes);
inline constexpr std::uint64_t provider_phase_end_credit =
    raw_envelope_size + provider_ref_bytes + 2 * sizeof(std::uint8_t) +
    sizeof(ErrorCode) + sizeof(std::int64_t);
inline constexpr std::uint64_t provider_phase_credit =
    provider_phase_begin_credit + provider_phase_end_credit;
inline constexpr std::uint64_t provider_link_credit =
    raw_envelope_size + 2 * provider_ref_bytes;
inline constexpr std::uint64_t provider_scalar_credit =
    raw_envelope_size + provider_ref_bytes + 2 * sizeof(std::uint8_t);
inline constexpr std::uint64_t provider_callback_credit =
    raw_envelope_size + provider_ref_bytes + sizeof(std::size_t) +
    provider_phase_end_credit;
inline constexpr std::uint64_t provider_native_acquisition_credit =
    provider_link_credit + raw_envelope_size + provider_ref_bytes +
    sizeof(std::uint8_t) + sizeof(ErrorCode) + sizeof(std::int64_t);
inline constexpr std::uint64_t provider_adapter_extra =
    2 * provider_link_credit + provider_native_acquisition_credit;
enum class ProviderOriginalRole : std::uint8_t {
  request_packet = 1,
  prepared_admission = 2,
  upstream_packet = 3,
  decoded_packet = 4,
  wrapper_packet = 5,
  native_stdout = 6,
  native_stderr = 7,
  callback_body = 8,
  public_prompt = 9,
  public_instructions = 10,
  standalone_request = 11
};
enum class ProviderOriginalMode : std::uint8_t {
  native_extent = 1,
  callback_extent = 2,
  codec_adopt = 3
};
enum class ProviderOriginalExtentKind : std::uint8_t {
  exact_used = 1,
  unknown_used = 2
};
struct ProviderOriginalRequest {
  ProviderOriginalRole role;
  ProviderOriginalMode mode;
  std::size_t maximum_bytes;
  // Actual existing business label; receiver stages its copy before acquisition.
  // This borrow ends when prepare returns. No role-to-label dictionary.
  std::string_view label;
};
struct ProviderOriginalReference {
  RawEventRef event;
  RawPartition partition;    // Actual native u32, not a compact replacement enum.
  std::size_t record_offset; // Includes the raw record's 16-byte envelope.
  std::size_t bytes; // Published range; unknown_used does not assert observed use.
};
// Allocated once before acquisition in participant capture mode. Native extent
// mode initializes exactly its requested extent; callback starts empty and its
// prepare initializes exactly requested bytes; codec adopts ONE encoder string.
struct ProviderOriginalStorage {
  std::string bytes;
};
class ProviderOwnedOriginal {
public:
  using Storage = std::variant<std::monostate, std::string,
                               std::shared_ptr<const ProviderOriginalStorage>>;
  ProviderOwnedOriginal(Storage storage,
                        std::optional<ProviderOriginalReference> reference,
                        ProviderOriginalExtentKind kind,
                        std::optional<std::size_t> observed_used) noexcept;
  ProviderOwnedOriginal(ProviderOwnedOriginal &&) noexcept = default;
  ProviderOwnedOriginal &operator=(ProviderOwnedOriginal &&) noexcept = default;
  ProviderOwnedOriginal(const ProviderOwnedOriginal &) = delete;
  ProviderOwnedOriginal &operator=(const ProviderOwnedOriginal &) = delete;
  // exact_used returns ONLY observed prefix, even if initialized backing extent
  // is larger; unknown_used returns the whole initialized snapshot extent.
  std::string_view bytes() const noexcept;
  bool owns_bytes() const noexcept;
  const std::optional<ProviderOriginalReference> &reference() const noexcept;
  ProviderOriginalExtentKind kind() const noexcept;
  std::optional<std::size_t> observed_used() const noexcept;
  // Monostate means reference-only unresolved semantic byte retention. Empty
  // owned string is distinct and can be a legitimate exact zero-byte original.
  // Empty reference means raw inactive; never synthesize a published ordinal0.
private:
  friend class ProviderOriginalLease;
  Storage storage_;
  std::optional<ProviderOriginalReference> reference_;
  ProviderOriginalExtentKind kind_;
  std::optional<std::size_t> observed_used_;
};
enum class ProviderCaptureState : std::uint8_t {
  accepted = 1,
  overflow = 2,
  stopped = 3
};
struct ProviderCaptureDisposition {
  ProviderCaptureState state;
  std::optional<Error> error;
};
class ProviderOriginalTicket {
public:
  virtual ~ProviderOriginalTicket() = default;
  // Moves the already allocated storage control block to the synchronous source
  // BEFORE raw admission. No shared_ptr copy at read/publication. Node is staged
  // separately from this ticket and receives immutable storage at complete.
  virtual std::shared_ptr<ProviderOriginalStorage> take_storage() noexcept = 0;
  // Last synchronous business borrow must have ended. Always transfer/link the
  // separately staged node first; noexcept, no allocation or raw wait. Ticket
  // destruction releases only issuer claims, never a node already linked.
  virtual ProviderCaptureDisposition
  complete(ProviderOwnedOriginal &&original) noexcept = 0;
};
class ProviderOriginalCapture {
public:
  virtual ~ProviderOriginalCapture() = default;
  virtual Result<std::unique_ptr<ProviderOriginalTicket>>
  prepare(ProviderOriginalRequest request) = 0;
};
// Explicit native adapter call context. Borrowed for the synchronous call; its
// receiver/owner remain alive through every lease. Optional parent is actual,
// never inferred from a recent record. Existing labels are passed to prepare.
struct RawProviderReceipt {
  std::optional<RawEventRef> request;
  std::optional<RawEventRef> response;
};
class RawProviderPhase;
struct RawProviderOperationContext {
  RawProviderOwner owner;
  RawProviderPhase *attempt_phase = nullptr; // Borrowed synchronous request owner.
  std::optional<RawEventRef> parent;
  RawProviderReceipt receipt;
  std::optional<ProviderOriginalReference> result;
};
struct RawAdapterContext {
  const RawProviderOwner *owner = nullptr;
  std::optional<RawEventRef> parent;
  ProviderOriginalCapture *originals = nullptr;
  RawProviderReceipt *receipt = nullptr;
};
// Issuer source lease is private to the provider source implementation. It holds
// source DATA/control, optional ticket, and the original. It exposes the original
// reference after publication even without a queue ticket (owner admission codec
// path), so PreparedDirection can carry that actual source Ref. Claims remain on
// the issuer; the optional queue ticket is not required for raw publication.
class RawProviderPhase {
public:
  explicit RawProviderPhase(std::uint64_t extra_control = 0);
  explicit RawProviderPhase(std::optional<RawControlToken> credit) noexcept;
  ~RawProviderPhase();
  RawProviderPhase(const RawProviderPhase &) = delete;
  RawProviderPhase &operator=(const RawProviderPhase &) = delete;
  void start(std::uint8_t kind, const RawAdapterContext *context = nullptr);
  RawEventRef reference() const noexcept { return phase_; }
  bool enabled() const noexcept { return credit_.has_value(); }
  void stage(std::uint8_t value) noexcept { stage_ = value; }
  void link(RawEventRef actual) noexcept;
  void scalar(std::uint8_t stage, std::uint8_t variant,
              std::span<const ByteView> fields = {}) noexcept;
  void finish(std::uint8_t branch, const Error *error = nullptr) noexcept;
  std::optional<RawControlToken> split(std::uint64_t bytes);
  void emit(std::uint32_t site, std::span<const ByteView> fields) noexcept;
  template <class F> auto run(F &&body) {
    try {
      if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
        std::forward<F>(body)();
        finish(1);
      } else {
        auto result = std::forward<F>(body)();
        finish(1);
        return result;
      }
    } catch (const Error &error) {
      finish(2, &error);
      throw;
    } catch (const std::bad_alloc &) {
      finish(3);
      throw;
    } catch (const std::length_error &) {
      finish(4);
      throw;
    } catch (...) {
      finish(5);
      throw;
    }
  }

private:
  std::optional<RawControlToken> credit_;
  RawEventRef phase_;
  std::uint8_t stage_ = 0;
};

class RawProviderAcquisition {
public:
  explicit RawProviderAcquisition(std::optional<RawControlToken> credit) noexcept;
  ~RawProviderAcquisition();
  RawProviderAcquisition(const RawProviderAcquisition &) = delete;
  RawProviderAcquisition &operator=(const RawProviderAcquisition &) = delete;
  void start(RawEventRef request, std::optional<RawEventRef> child);
  RawEventRef reference() const noexcept { return request_; }
  void finish(std::uint8_t branch, const Error *error = nullptr,
              const int *status = nullptr) noexcept;

private:
  std::optional<RawControlToken> credit_;
  RawEventRef request_;
  bool callback_ = false;
};

class ProviderOriginalLease {
public:
  ProviderOriginalLease(ProviderOriginalRequest request, RawEventRef source,
                        ProviderOriginalCapture *capture = nullptr,
                        bool capture_raw = true);
  ~ProviderOriginalLease();
  ProviderOriginalLease(const ProviderOriginalLease &) = delete;
  ProviderOriginalLease &operator=(const ProviderOriginalLease &) = delete;
  ProviderOriginalLease(ProviderOriginalLease &&other) noexcept;
  ProviderOriginalLease &operator=(ProviderOriginalLease &&) noexcept = delete;
  std::span<char> writable() noexcept;
  void prepare(std::size_t requested);
  void source(RawEventRef actual) noexcept { source_ = actual; }
  void commit(std::size_t actual_used);
  void adopt(std::string bytes);
  void native(std::optional<ProviderOriginalReference> reference,
              std::size_t actual_used) noexcept;
  std::string_view bytes() const noexcept;
  const std::optional<ProviderOriginalReference> &reference() const noexcept;
  ProviderCaptureDisposition complete() noexcept;
  ProviderOwnedOriginal take_original();
  std::string response_body();
  bool committed() const noexcept { return acquired_; }

private:
  ProviderOriginalRequest request_;
  RawEventRef source_;
  std::unique_ptr<ProviderOriginalTicket> ticket_;
  std::shared_ptr<ProviderOriginalStorage> shared_;
  std::string local_;
  std::optional<RawCapacityClaim> data_;
  std::optional<ProviderOriginalReference> reference_;
  ProviderOriginalExtentKind kind_ = ProviderOriginalExtentKind::exact_used;
  std::optional<std::size_t> used_;
  bool prepared_ = false, acquired_ = false, handed_ = false, raw_ = true;
  ProviderCaptureDisposition disposition_{ProviderCaptureState::accepted, std::nullopt};
  std::string &storage() noexcept;
  void publish(ProviderOriginalExtentKind kind, std::optional<std::size_t> used);
  void unknown() noexcept;
  friend class ProviderHttpResponseLease;
};
// ONE existing bounded business encode, published before return or capture work.
// With no receiver, result owns the encoder's actual string; owner admission can
// synchronously Retain it and carry its actual reference into PreparedDirection.
ProviderOwnedOriginal
provider_encode_original(const Value &value, ProviderOriginalRequest request,
                         const RawAdapterContext *context = nullptr,
                         ValueLimits limits = {});
std::string_view provider_original_description() noexcept;

class ProviderHttpResponseLease {
public:
  ProviderHttpResponseLease(const ProviderHttpResponseLease &) = delete;
  ProviderHttpResponseLease &operator=(const ProviderHttpResponseLease &) = delete;
  std::span<char> prepare(std::size_t requested);
  void commit(std::size_t actual_used);
  static ProviderHttpResponseLease borrow(ProviderOriginalLease &source) noexcept {
    return ProviderHttpResponseLease{source};
  }

private:
  explicit ProviderHttpResponseLease(ProviderOriginalLease &source) noexcept
      : source_(&source) {}
  ProviderOriginalLease *source_;
};
} // namespace blackbird
