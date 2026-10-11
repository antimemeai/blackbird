#pragma once

#include "blackbird/raw_channel.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <time.h>

namespace blackbird {

// These are native clock return fields, never scalar timestamps or durations.
struct RawClockReading {
  clockid_t method{};
  int result{};
  int error{};
  timespec value{};
};
struct RawClockSample {
  RawClockReading monotonic;
  RawClockReading thread_cpu;
  bool cpu_read{};
};
RawClockSample raw_local_clock(bool cpu) noexcept;

struct RawRegionSites {
  std::uint32_t begin, end, observation, linkage, outcome;
  std::string_view name, source;
};
namespace raw_regions {
inline constexpr RawRegionSites startup_initialize{
    1000, 1001, 1002, 1003, 1004, "startup.initialize", "src/main.cpp:run_session"};
inline constexpr RawRegionSites startup_restore{
    1010, 1011, 1012, 1013, 1014, "startup.restore", "src/main.cpp:open"};
inline constexpr RawRegionSites terminal_first_frame{1020,
                                                     1021,
                                                     1022,
                                                     1023,
                                                     1024,
                                                     "startup.terminal-first-frame",
                                                     "src/terminal.cpp:run"};
inline constexpr RawRegionSites command_perform{
    1030, 1031, 1032, 1033, 1034, "command.perform", "src/terminal.cpp:worker"};
inline constexpr RawRegionSites command_render{
    1040, 1041, 1042, 1043, 1044, "command.render", "src/terminal.cpp:frame"};
inline constexpr RawRegionSites command_input_save{
    1050, 1051, 1052, 1053, 1054, "command.input-save", "src/terminal.cpp:input"};
inline constexpr RawRegionSites command_admission{
    1060, 1061, 1062, 1063, 1064, "command.admission", "src/coding.cpp:operation"};
inline constexpr RawRegionSites provider_prepare{
    1070, 1071, 1072, 1073, 1074, "provider.prepare", "src/coding.cpp:request"};
inline constexpr RawRegionSites provider_transport{1080,
                                                   1081,
                                                   1082,
                                                   1083,
                                                   1084,
                                                   "provider.transport",
                                                   "src/coding.cpp:provider.respond"};
inline constexpr RawRegionSites retained_append{1090,
                                                1091,
                                                1092,
                                                1093,
                                                1094,
                                                "retained.append",
                                                "src/retained_state.cpp:append_impl"};
inline constexpr std::array definitions{
    startup_initialize, startup_restore,    terminal_first_frame, command_perform,
    command_render,     command_input_save, command_admission,    provider_prepare,
    provider_transport, retained_append};
} // namespace raw_regions

// Outcomes are owner branch facts. End of scope is never inferred success.
enum class RawRegionOutcome : std::uint32_t {
  ready = 1,
  compact,
  full_replay,
  admitted,
  success,
  rejected,
  uncertain,
  returned,
  exception,
  flushed,
  small_frame,
  queued,
  dispatch
};
enum class RawObservationKind : std::uint32_t {
  request_bytes = 1,
  source_bytes,
  encoded_event_bytes,
  journal_state
};
struct RawRegionObservation {
  RawObservationKind kind;
  std::size_t history{}, bytes{};
  std::uint64_t sequence{};
};
struct RawRegionLinkage {
  ByteView revision, attempt;
};
enum class RawRegionEnd : std::uint8_t { explicit_finish = 1, scope_exit = 2 };
struct RawRegionOptions {
  bool cpu = true;
};

// A joined command worker lends its fixed settlement bank only to retained_append.
// Nested scopes restore the previous selection. No claim or wait occurs here.
class RawRegionPrepaidScope {
public:
  explicit RawRegionPrepaidScope(RawControlClaim &bank) noexcept;
  ~RawRegionPrepaidScope();
  RawRegionPrepaidScope(const RawRegionPrepaidScope &) = delete;
  RawRegionPrepaidScope &operator=(const RawRegionPrepaidScope &) = delete;

private:
  RawControlClaim *previous_;
};

// One execution context owns each region. Its end and one branch-outcome record
// have prepaid control credit before begin publication. No region migrates threads.
class RawRegion {
public:
  explicit RawRegion(const RawRegionSites &sites, RawRegionOptions options = {});
  ~RawRegion() { finish(RawRegionEnd::scope_exit); }
  RawRegion(const RawRegion &) = delete;
  RawRegion &operator=(const RawRegion &) = delete;
  RawRegion(RawRegion &&) = delete;
  RawRegion &operator=(RawRegion &&) = delete;
  bool enabled() const noexcept { return claim_.has_value(); }
  std::uint64_t begin_ordinal() const noexcept { return begin_; }
  void observe(const RawRegionObservation &observation);
  void linkage(const RawRegionLinkage &linkage);
  void outcome(RawRegionOutcome outcome) noexcept;
  void error(Error error) noexcept;
  void finish(RawRegionEnd reason = RawRegionEnd::explicit_finish) noexcept;

private:
  const RawRegionSites *sites_;
  RawRegionOptions options_;
  std::optional<RawControlClaim> claim_;
  std::uint64_t begin_ = 0;
  bool outcome_written_ = false;
  bool error_written_ = false;
  bool prepaid_ = false;
};

// Immutable descriptions registered once by runtime setup. No writer lookup.
std::span<const RawRegionSites> raw_region_descriptions() noexcept;

struct RawWorkerDispatch {
  IdentityBytes lane;
  std::uint64_t ordinal;
};
RawWorkerDispatch raw_worker_dispatch();
void raw_worker_started(const RawWorkerDispatch &dispatch);

} // namespace blackbird
