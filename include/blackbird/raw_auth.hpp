#pragma once

#include "blackbird/provider_auth.hpp"
#include "blackbird/raw_provider.hpp"

namespace blackbird {

enum class RawAuthAction : std::uint8_t {
  descriptor = 1,
  catalog,
  status,
  registration,
  binding,
  selected,
  set_key,
  use,
  logout,
  add_provider,
  save_oauth,
  device_begin,
  device_poll,
  resolve_owned,
  resolve_codex,
  browser_login,
  command
};
enum class RawAuthField : std::uint8_t {
  account_revision = 1,
  selection_revision,
  expected_account,
  expected_selection,
  token_clock,
  token_expiry,
  token_expires_at,
  device_expiry,
  device_interval,
  pending_selection,
  current_selection,
  poll_expiry,
  poll_check_next,
  poll_interval,
  poll_slow_interval,
  poll_clamped_interval,
  poll_next,
  device_expiry_clock,
  device_next_clock,
  poll_clock,
  retry_clock,
  poll_wait,
  refresh_expiry,
  refresh_expiry_clock,
  refresh_after,
  refresh_retry_clock,
  refresh_next,
  binding_expected_revision,
  binding_current_revision,
  binding_revision,
  registration_revision,
  registration_selection,
  browser_revision,
  browser_clock
};
struct RawAuthParent {
  RawEventRef reference;
  RawControlClaim *control = nullptr;
  RawCapacityClaim *data = nullptr;
};
struct RawAuthSource {
  std::uint32_t site;
  std::uint8_t value;
};
struct RawAuthLabels {
  std::string_view provider, account;
};
struct RawAuthEntry {
  RawAuthAction action;
  RawAuthLabels labels{};
  const RawAuthParent *parent = nullptr;
  const OAuthExpected *expected = nullptr;
  bool binding_value = false;
  bool codex_probe = false;
  bool secret_prompt = false;
};

// Stack owner: construction holds the outcome before any BEGIN is published.
// start and subsequent acquisitions are guarded by the actual caller.
class RawAuthTrace {
public:
  explicit RawAuthTrace(RawAuthEntry entry);
  ~RawAuthTrace();
  RawAuthTrace(const RawAuthTrace &) = delete;
  RawAuthTrace &operator=(const RawAuthTrace &) = delete;
  void start(std::uint64_t extra_data = 0);
  void stage(std::uint8_t value) noexcept { stage_ = value; }
  void stage(RawAuthField value) noexcept { stage_ = static_cast<std::uint8_t>(value); }
  RawEventRef reference() const noexcept { return action_; }
  RawAuthAction action() const noexcept { return entry_.action; }
  RawAuthParent child() noexcept;
  void scalar(RawAuthField field, std::int64_t value);
  void boolean(std::uint8_t field, bool value);
  void branch(RawAuthSource source);
  void callback(std::uint8_t operation, std::uint8_t boundary);
  void selected_account(std::string_view account);
  void binding_strings(std::span<const std::string_view, 5> fields);
  void descriptor_strings(std::span<const std::string_view, 4> fields);
  RawControlClaim split(std::uint64_t bytes);
  void finish(std::uint8_t branch, const Error *error = nullptr) noexcept;
  bool enabled() const noexcept { return control_.has_value(); }

private:
  std::uint64_t emit(std::uint32_t site, std::span<const ByteView> fields,
                     RawCapacityClaim &claim);
  RawAuthEntry entry_;
  RawEventRef action_;
  std::optional<RawControlClaim> control_, terminal_;
  std::optional<RawCapacityClaim> data_;
  std::uint8_t stage_ = 100;
};

// Whole private operation request/return; no credential-bearing payload capture.
struct RawAuthExchangeSites {
  std::uint32_t begin, returned;
};
class RawAuthExchange {
public:
  RawAuthExchange(RawAuthTrace &action, RawAuthExchangeSites sites);
  ~RawAuthExchange();
  RawAuthExchange(const RawAuthExchange &) = delete;
  RawAuthExchange &operator=(const RawAuthExchange &) = delete;
  void start(std::uint8_t operation);
  void returned(const int *status = nullptr) noexcept;
  void finish(std::uint8_t branch, const Error *error = nullptr) noexcept;

private:
  RawAuthTrace *action_;
  std::uint32_t begin_site_, return_site_;
  RawEventRef request_;
  std::optional<RawControlClaim> credit_;
};
inline constexpr std::uint64_t raw_auth_save_credit =
    2 * raw_envelope_size + 3 * (sizeof(IdentityBytes) + sizeof(std::uint64_t)) +
    3 * sizeof(std::uint8_t) + sizeof(ErrorCode) + sizeof(std::int64_t);

class RawAuthSave {
public:
  explicit RawAuthSave(RawAuthTrace *action);
  ~RawAuthSave();
  RawAuthSave(const RawAuthSave &) = delete;
  RawAuthSave &operator=(const RawAuthSave &) = delete;
  void start();
  void stage(std::uint8_t value) noexcept { stage_ = value; }
  void finish(std::uint8_t branch, const Error *error = nullptr) noexcept;

private:
  RawAuthTrace *action_;
  RawEventRef request_;
  std::optional<RawControlClaim> credit_;
  std::uint8_t stage_ = 0;
};

// Setup-only owner. Its directory fd must outlive all Runtime descendants using
// runtime_directory(); callers never retain a raw thread beyond this owner.
class RawAuthEnvironment {
public:
  static Result<std::unique_ptr<RawAuthEnvironment>>
  open(const ProviderAuthConfig &config);
  ~RawAuthEnvironment();
  RawAuthEnvironment(const RawAuthEnvironment &) = delete;
  RawAuthEnvironment &operator=(const RawAuthEnvironment &) = delete;
  EnvironmentId identity() const noexcept { return identity_; }
  std::string runtime_directory() const;
  void verify_runtime_directory(std::string_view path) const;

private:
  RawAuthEnvironment(int directory, EnvironmentId identity)
      : directory_(directory), identity_(identity) {}
  int directory_;
  EnvironmentId identity_;
};
std::string_view raw_auth_description() noexcept;

} // namespace blackbird
