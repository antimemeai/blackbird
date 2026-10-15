#pragma once
#include "blackbird/context.hpp"
#include <filesystem>
namespace blackbird {
struct RawAdapterContext;
std::filesystem::path default_session_directory(const std::filesystem::path &home);
struct SessionSettings {
  std::string model = "gpt-6.1-sol", effort = "medium", workflow;
  std::string name{};
  std::string provider = "openai";
  bool operator==(const SessionSettings &) const = default;
};
struct SessionIdentity {
  ParticipantId actor;
  ConversationId conversation;
  WorkflowId workflow;
};
SessionIdentity session_identity(AuditLog &log, const RawAdapterContext * = nullptr);
void save_session_info(const std::filesystem::path &directory,
                       const SessionSettings &settings, const SessionIdentity &identity,
                       const RawAdapterContext * = nullptr);
Value list_sessions(const std::filesystem::path &root,
                    const RawAdapterContext * = nullptr,
                    RawEventRef *completion = nullptr);
class SessionStore {
public:
  explicit SessionStore(AuditLog &log, const RawAdapterContext * = nullptr);
  const SessionSettings &settings() const noexcept { return settings_; }
  void save(const SessionSettings &value, const RawAdapterContext * = nullptr);
  void restart(std::string_view note, const RawAdapterContext * = nullptr);
  bool resume(ContextStore &context, const RawAdapterContext * = nullptr);

private:
  AuditLog &log_;
  SessionSettings settings_;
  Value restart_;
};
} // namespace blackbird
