#pragma once
#include "blackbird/raw_provider.hpp"
namespace blackbird {
struct RawParticipantIdentities {
  IdentityBytes attempt, invocation, generation;
};
struct RawParticipantOrigin {
  std::optional<RawEventRef> parent;
  std::optional<RawParticipantIdentities> identities;
};
enum class RawParticipantAction : std::uint8_t {
  configure = 1,
  start,
  read,
  send,
  cancel,
  await,
  join,
  archive,
  drain,
  shutdown,
  restore,
  cleanup
};
// Sole issuer-lane capacity; splitting does not admit or transfer lanes.
class RawParticipantBank {
public:
  RawParticipantBank() = default;
  explicit RawParticipantBank(std::uint64_t bytes);
  explicit RawParticipantBank(std::optional<RawControlToken> credit) noexcept;
  RawParticipantBank(RawParticipantBank &&) noexcept = default;
  RawParticipantBank &operator=(RawParticipantBank &&) noexcept = default;
  RawParticipantBank(const RawParticipantBank &) = delete;
  RawParticipantBank &operator=(const RawParticipantBank &) = delete;
  RawParticipantBank split(std::uint64_t bytes);
  bool enabled() const noexcept { return credit_.has_value(); }
  RawEventRef emit(std::uint32_t site, std::span<const ByteView> fields) noexcept;
  RawEventRef linked(std::uint32_t site, RawEventRef parent,
                     std::span<const ByteView> fields) noexcept;

private:
  std::optional<RawControlToken> credit_;
};
class RawParticipantTrace {
public:
  RawParticipantTrace(RawParticipantAction action, std::uint64_t bytes,
                      RawParticipantOrigin origin = {});
  RawParticipantTrace(RawParticipantAction action, RawParticipantBank bank,
                      RawParticipantOrigin origin = {});
  ~RawParticipantTrace();
  RawParticipantTrace(const RawParticipantTrace &) = delete;
  RawParticipantTrace &operator=(const RawParticipantTrace &) = delete;
  RawEventRef reference() const noexcept { return request_; }
  RawParticipantBank &bank() noexcept { return bank_; }
  void stage(std::uint8_t value) noexcept { stage_ = value; }
  void finish(std::uint8_t branch, std::optional<Error> error = {}) noexcept;

private:
  void begin(RawParticipantAction action, const RawParticipantOrigin &origin);
  RawParticipantBank bank_;
  RawEventRef request_;
  std::uint8_t stage_ = 0;
  bool closed_ = false;
};
RawEventRef raw_participant_data(std::uint32_t site, RawEventRef parent,
                                 std::span<const ByteView> fields, std::uint64_t bytes);
std::string_view raw_participants_description() noexcept;
} // namespace blackbird
