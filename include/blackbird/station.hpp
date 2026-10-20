#pragma once
#include "blackbird/context.hpp"
#include <set>
namespace blackbird {
struct RawAdapterContext;
// Single native session owner. File adapters are trusted local operator inputs.
class StationStore {
public:
  explicit StationStore(AuditLog &log, const RawAdapterContext *capture = nullptr);
  bool control(const Value &command, const RawAdapterContext *capture = nullptr,
               RawEventRef *actual_owner = nullptr);
  bool admit(const Value &event, const RawAdapterContext *capture = nullptr,
             RawEventRef *actual_owner = nullptr);
  void pause(std::string_view reason, const RawAdapterContext *capture = nullptr);
  // A supervisor stop (SIGTERM): recorded on the log channel, no state change, so
  // the station resumes where it was on reopen. Operator interrupts pause.
  void supervisor_stop(std::string_view reason,
                       const RawAdapterContext *capture = nullptr);
  void finish(const Value &event, bool returned,
              const RawAdapterContext *capture = nullptr);
  std::string prompt(const Value &event, const RawAdapterContext *capture = nullptr,
                     RawEventRef *actual_source = nullptr) const;
  Value view(const RawAdapterContext *capture = nullptr,
             RawEventRef *actual_owner = nullptr) const;
  bool paused() const noexcept { return paused_; }
  bool stopped() const noexcept { return stopped_; }

private:
  AuditLog &log_;
  bool paused_ = false, stopped_ = false;
  std::string steer_;
  std::set<std::string> controls_;
  std::map<std::pair<std::string, std::string>, std::string> events_;
  void apply_control(const Value &command);
};
} // namespace blackbird
