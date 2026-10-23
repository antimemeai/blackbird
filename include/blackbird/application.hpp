#pragma once
#include "blackbird/coding.hpp"

namespace blackbird {
// One trusted native host owns one semantic session. Descriptors are borrowed.
// Stdout contains only length-prefixed BBM2 packets.
class ApplicationConnection {
public:
  static constexpr std::size_t max_packet_bytes = 4 * 1024 * 1024;
  struct Descriptors {
    int input = 0;
    int output = 1;
  };
  ApplicationConnection() = default;
  explicit ApplicationConnection(Descriptors descriptors)
      : input_(descriptors.input), output_(descriptors.output) {}
  Value open();
  int run(CodingEngine &engine, AuditLog &log, const SessionSettings &settings,
          const std::filesystem::path &session);
  void error(std::string_view id, std::string_view message, bool final_follows = false);
  void fatal(std::string_view id, std::string_view message) noexcept;

private:
  int input_ = 0, output_ = 1;
  std::array<std::byte, 4> header_{};
  std::size_t header_bytes_ = 0, body_bytes_ = 0;
  std::vector<std::byte> body_;
  bool eof_ = false, closing_ = false, stopped_ = false, active_ = false;
  bool transport_failed_ = false;
  std::string open_id_, close_id_;
  std::optional<Value> pending_;
  std::optional<HostToolCatalog> host_catalog_;
  struct HostWait {
    std::string turn, call, tool;
    std::optional<Value> result;
  };
  std::optional<HostWait> host_wait_;
  AuditLog *log_ = nullptr;
  std::string host_source_;
  std::string active_id_;
  Value host_call(CodingEngine &engine, const HostToolCall &call);
  void host_reply(const Value &packet);
  std::vector<std::string> discarded_;
  std::optional<Value> receive(bool wait);
  void send(const Value &packet);
  void send_bytes(ByteView bytes);
  void command(Value packet);
  void pump(bool wait);
  void discard_pending();
  void finish_discarded();
};
} // namespace blackbird
