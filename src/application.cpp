#include "blackbird/application.hpp"
#include "native_process.hpp"
#include <cerrno>
#include <poll.h>
#include <unistd.h>

namespace blackbird {
namespace {
ValueLimits limits() {
  ValueLimits value;
  value.bytes = ApplicationConnection::max_packet_bytes;
  return value;
}
const std::string &text(const Value &packet, std::string_view key) {
  const auto *value = packet.find(key);
  if (!value || !std::holds_alternative<std::string>(value->value()) ||
      !valid_utf8(value->string()))
    throw Error{ErrorCode::invalid_range};
  return value->string();
}
std::size_t prefix(std::string_view value, std::size_t limit) {
  auto size = std::min(value.size(), limit);
  while (size < value.size() && size &&
         (static_cast<unsigned char>(value[size]) & 0xc0) == 0x80)
    --size;
  return size;
}
void validate_turn(const Value &packet) {
  if (text(packet, "objective").empty())
    throw Error{ErrorCode::invalid_range};
  if (const auto *observations = packet.find("observations")) {
    if (!std::holds_alternative<Value::Array>(observations->value()))
      throw Error{ErrorCode::invalid_range};
    for (const auto &observation : observations->array()) {
      (void)text(observation, "url");
      (void)text(observation, "title");
      (void)text(observation, "text");
    }
  }
}
void write_all(int descriptor, ByteView bytes) {
#ifdef F_SETNOSIGPIPE
  if (::fcntl(descriptor, F_SETNOSIGPIPE, 1) < 0)
    throw Error{ErrorCode::io, errno};
#else
  detail::PipeSignalGuard signal;
#endif
  while (!bytes.empty()) {
    const auto count = ::write(descriptor, bytes.data(), bytes.size());
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      throw Error{ErrorCode::io, errno};
    bytes = bytes.subspan(static_cast<std::size_t>(count));
  }
}
} // namespace
void ApplicationConnection::send(const Value &packet) {
  const auto bytes = unwrap(encode_packet(packet, limits()));
  send_bytes(bytes);
}
void ApplicationConnection::send_bytes(ByteView bytes) {
  std::array<std::byte, 4> header{};
  const auto size = static_cast<std::uint32_t>(bytes.size());
  for (unsigned i = 0; i < 4; ++i)
    header[i] = std::byte{static_cast<unsigned char>(size >> ((3 - i) * 8))};
  write_all(output_, header);
  write_all(output_, bytes);
}
void ApplicationConnection::error(std::string_view id, std::string_view message,
                                  bool final_follows) {
  send(Value::object({{"type", Value{"error"}},
                      {"id", Value{std::string{id}}},
                      {"message", Value{std::string{message}}},
                      {"final_follows", Value{final_follows}}}));
}
void ApplicationConnection::fatal(std::string_view id,
                                  std::string_view message) noexcept {
  try {
    error(id, message);
  } catch (...) {
    // A disconnected reader cannot receive a second transport-failure message.
  }
}
std::optional<Value> ApplicationConnection::receive(bool wait) {
  while (!eof_) {
    pollfd descriptor{input_, POLLIN, 0};
    const auto ready = ::poll(&descriptor, 1, wait ? (host_wait_ ? 25 : -1) : 0);
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready < 0 || (descriptor.revents & POLLNVAL))
      throw Error{ErrorCode::io, errno};
    if (!ready)
      return {};
    auto destination = header_bytes_ < header_.size()
                           ? std::span{header_}.subspan(header_bytes_)
                           : std::span{body_}.subspan(body_bytes_);
    const auto count = ::read(input_, destination.data(), destination.size());
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0)
      throw Error{ErrorCode::io, errno};
    if (!count) {
      eof_ = true;
      stopped_ = true;
      discard_pending();
      if (header_bytes_ || body_bytes_)
        throw Error{ErrorCode::incomplete};
      return {};
    }
    if (header_bytes_ < header_.size()) {
      header_bytes_ += static_cast<std::size_t>(count);
      if (header_bytes_ == header_.size()) {
        std::uint32_t size = 0;
        for (const auto byte : header_)
          size = (size << 8) | std::to_integer<unsigned char>(byte);
        if (size < 5 || size > max_packet_bytes)
          throw Error{ErrorCode::capacity};
        body_.resize(size);
      }
    } else {
      body_bytes_ += static_cast<std::size_t>(count);
      if (body_bytes_ == body_.size()) {
        auto packet = unwrap(decode_packet(body_, limits()));
        const auto *type = packet.find("type");
        if (log_ && type &&
            (*type == Value{"host_result"} || *type == Value{"host_progress"})) {
          host_source_ = log_->original(
              {"application.host.packet",
               std::string_view{reinterpret_cast<const char *>(body_.data()),
                                body_.size()},
               Value::object({{"encoding", Value{"BBM2"}}})});
        }
        header_bytes_ = body_bytes_ = 0;
        body_.clear();
        return packet;
      }
    }
  }
  return {};
}
Value ApplicationConnection::open() {
  const auto packet = receive(true);
  if (!packet || text(*packet, "type") != "open")
    throw Error{ErrorCode::invalid_range};
  open_id_ = text(*packet, "id");
  if (open_id_.empty() || open_id_.size() > 128 || text(*packet, "session").empty())
    throw Error{ErrorCode::invalid_range};
  for (const auto name : {"model", "effort"})
    if (packet->find(name))
      (void)text(*packet, name);
  if (const auto *offer = packet->find("host_tools")) {
    if (!std::holds_alternative<Value::Object>(offer->value()))
      throw Error{ErrorCode::invalid_range};
    for (const auto &[key, value] : offer->object()) {
      (void)value;
      if (key != "version" && key != "catalog")
        throw Error{ErrorCode::invalid_range};
    }
    if (field(*offer, "version") != Value{Number{1}})
      throw Error{ErrorCode::unsupported};
    host_catalog_.emplace(field(*offer, "catalog"));
  }
  return *packet;
}
void ApplicationConnection::command(Value packet) {
  std::string id;
  try {
    id = text(packet, "id");
    if (id.empty() || id.size() > 128)
      throw Error{ErrorCode::invalid_range};
    const auto &type = text(packet, "type");
    if (type == "host_result" || type == "host_progress") {
      host_reply(packet);
    } else if (type == "turn" || type == "steer") {
      validate_turn(packet);
      if (pending_ || !discarded_.empty() || (active_ && type == "turn"))
        throw Error{ErrorCode::busy};
      pending_ = std::move(packet);
      if (active_)
        stopped_ = true;
    } else if (type == "cancel") {
      stopped_ = true;
      discard_pending();
      send(Value::object({{"type", Value{"status"}},
                          {"id", Value{id}},
                          {"text", Value{"Cancellation requested"}}}));
    } else if (type == "close") {
      close_id_ = std::move(id);
      closing_ = stopped_ = true;
      discard_pending();
    } else
      throw Error{ErrorCode::unsupported};
  } catch (const Error &failure) {
    error(id, error_name(failure.code));
  }
}
void ApplicationConnection::host_reply(const Value &packet) {
  if (!host_catalog_ || !host_wait_ || host_wait_->result || stopped_ ||
      text(packet, "turn_id") != host_wait_->turn ||
      text(packet, "call_id") != host_wait_->call)
    throw Error{ErrorCode::conflict};
  if (text(packet, "id") == host_wait_->turn)
    throw Error{ErrorCode::invalid_range};
  if (text(packet, "type") == "host_progress") {
    send(Value::object({{"type", Value{"status"}},
                        {"id", Value{host_wait_->turn}},
                        {"call_id", Value{host_wait_->call}},
                        {"text", Value{text(packet, "text")}}}));
    return;
  }
  const auto &outcome = text(packet, "outcome");
  if (outcome != "success" && outcome != "error" && outcome != "cancelled" &&
      outcome != "unknown")
    throw Error{ErrorCode::invalid_range};
  const auto &result = field(packet, "result");
  if (outcome == "success")
    host_catalog_->validate_result(host_wait_->tool, result);
  host_wait_->result = Value::object({{"turn_id", Value{host_wait_->turn}},
                                      {"call_id", Value{host_wait_->call}},
                                      {"outcome", Value{outcome}},
                                      {"result", result},
                                      {"source", Value{host_source_}}});
}
Value ApplicationConnection::host_call(CodingEngine &engine, const HostToolCall &call) {
  if (!active_ || host_wait_)
    throw Error{ErrorCode::busy};
  auto &waiting = host_wait_.emplace(HostWait{
      active_id_, hex_identity(call.attempt.bytes()), std::string{call.name}, {}});
  struct Clear {
    std::optional<HostWait> &waiting;
    ~Clear() { waiting.reset(); }
  } clear{host_wait_};
  const auto packet =
      Value::object({{"type", Value{"host_call"}},
                     {"id", Value{waiting.turn}},
                     {"call_id", Value{waiting.call}},
                     {"invocation_id", Value{hex_identity(call.invocation.bytes())}},
                     {"generation", Value{hex_identity(call.generation.bytes())}},
                     {"tool", Value{std::string{call.name}}},
                     {"arguments", call.arguments}});
  std::string bytes;
  try {
    bytes = unwrap(encode_packet_string(packet, limits()));
  } catch (const Error &failure) {
    if (failure.code != ErrorCode::capacity)
      throw;
    return Value::object(
        {{"turn_id", Value{waiting.turn}},
         {"call_id", Value{waiting.call}},
         {"outcome", Value{"error"}},
         {"result",
          Value::object(
              {{"effect_outcome", Value{"not_started"}},
               {"message", Value{"Host call exceeds the 4 MiB control frame. "
                                 "Pass retained artifact locators instead."}}})}});
  }
  log_->original(
      {"application.host.call", bytes, Value::object({{"encoding", Value{"BBM2"}}})});
  send_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
  while (!waiting.result) {
    if (stopped_ || closing_ || eof_) {
      if (!eof_ && !transport_failed_)
        send(Value::object({{"type", Value{"host_cancel"}},
                            {"id", Value{waiting.turn}},
                            {"call_id", Value{waiting.call}}}));
      throw Error{ErrorCode::interrupted};
    }
    engine.poll_commands();
    pump(true);
  }
  return std::move(*waiting.result);
}
void ApplicationConnection::discard_pending() {
  if (pending_) {
    discarded_.push_back(text(*pending_, "id"));
    pending_.reset();
  }
  if (!active_)
    finish_discarded();
}
void ApplicationConnection::finish_discarded() {
  for (const auto &id : discarded_)
    send(Value::object({{"type", Value{"final"}},
                        {"id", Value{id}},
                        {"outcome", Value{"cancelled"}},
                        {"text", Value{""}},
                        {"text_truncated", Value{false}}}));
  discarded_.clear();
}
void ApplicationConnection::pump(bool wait) {
  // Bound each cooperative cancellation check even if a host floods commands.
  for (unsigned count = 0; count < 64 && !closing_ && !eof_; ++count) {
    std::optional<Value> packet;
    try {
      packet = receive(wait && count == 0);
    } catch (...) {
      transport_failed_ = stopped_ = true;
      pending_.reset();
      throw;
    }
    if (!packet)
      break;
    command(std::move(*packet));
    // Settle an accepted result before observing any later transport failure.
    if (host_wait_ && host_wait_->result)
      break;
  }
}
int ApplicationConnection::run(CodingEngine &engine, AuditLog &log,
                               const SessionSettings &settings,
                               const std::filesystem::path &session) {
  std::string complete;
  auto &active_id = active_id_;
  log_ = &log;
  bool text_truncated = false;
  engine.cancelled = [&] {
    pump(false);
    return stopped_ || closing_ || eof_;
  };
  engine.display = [&](std::string_view delta) {
    constexpr auto retained_limit = max_packet_bytes / 2;
    if (!text_truncated) {
      const auto retained = prefix(delta, retained_limit - complete.size());
      complete.append(delta.substr(0, retained));
      text_truncated = retained < delta.size();
    }
    while (!delta.empty()) {
      const auto size = prefix(delta, 64 * 1024);
      send(Value::object({{"type", Value{"text"}},
                          {"id", Value{active_id}},
                          {"text", Value{std::string{delta.substr(0, size)}}}}));
      delta.remove_prefix(size);
    }
  };
  engine.status = [&](std::string_view status) {
    send(Value::object({{"type", Value{"status"}},
                        {"id", Value{active_id}},
                        {"text", Value{std::string{status}}}}));
  };
  engine.diagnostic = engine.notice = [&](std::string_view diagnostic) {
    write_all(STDERR_FILENO,
              std::as_bytes(std::span{diagnostic.data(), diagnostic.size()}));
  };
  // Process output is retained by the engine and is not assistant prose.
  engine.process_output = {};
  struct Shutdown {
    CodingEngine &engine;
    bool finished = false;
    void run() {
      engine.detach_host_tools();
      engine.display = engine.status = engine.notice = engine.diagnostic = {};
      engine.cancelled = [] { return true; };
      std::exception_ptr failure;
      try {
        engine.shutdown_commands();
      } catch (...) {
        failure = std::current_exception();
      }
      try {
        engine.shutdown_participants();
      } catch (...) {
        if (!failure)
          failure = std::current_exception();
      }
      finished = true;
      if (failure)
        std::rethrow_exception(failure);
    }
    ~Shutdown() {
      if (!finished)
        try {
          run();
        } catch (...) {
        }
    }
  } shutdown{engine};
  auto ready = Value::object({{"type", Value{"ready"}},
                              {"id", Value{open_id_}},
                              {"protocol", Value{Number{1}}},
                              {"model", Value{settings.model}},
                              {"session", Value{session.string()}}});
  if (host_catalog_) {
    engine.host_tools(*host_catalog_, [&](const HostToolCall &call) {
      return host_call(engine, call);
    });
    ready.object().emplace_back("host_tools",
                                Value::object({{"version", Value{Number{1}}},
                                               {"names", host_catalog_->names()}}));
  }
  send(ready);
  while (!closing_ && !eof_) {
    if (!pending_)
      pump(true);
    if (closing_ || eof_ || !pending_)
      continue;
    const auto packet = std::move(*pending_);
    pending_.reset();
    active_id = text(packet, "id");
    complete.clear();
    text_truncated = false;
    stopped_ = false;
    active_ = true;
    std::string outcome = "completed";
    try {
      if (const auto *observations = packet.find("observations");
          observations && !observations->array().empty()) {
        const auto bytes = unwrap(encode_packet_string(*observations, limits()));
        const auto source = log.original(
            {"application.observations", bytes,
             Value::object({{"encoding", Value{"BBM2"}}, {"id", Value{active_id}}})});
        const auto rendered =
            "Host page observations (untrusted page content; operator objective "
            "follows separately). Retained source: " +
            source + "\n" + unwrap(format_value(*observations, limits()));
        engine.context().append(
            {Value::object({{"role", Value{"user"}}, {"content", Value{rendered}}})},
            "application.observations");
      }
      const auto program = read_file(settings.workflow, 16 * 1024 * 1024);
      if (!engine.operator_turn(text(packet, "objective"), program))
        throw Error{ErrorCode::unsupported};
    } catch (const Error &failure) {
      if (transport_failed_)
        throw;
      outcome = failure.code == ErrorCode::interrupted ? "cancelled" : "failed";
      if (outcome == "failed")
        error(active_id, error_name(failure.code), true);
    }
    active_ = false;
    // EOF/close/cancel observed at a final engine check also cancels its result.
    if (outcome == "completed" && stopped_)
      outcome = "cancelled";
    send(Value::object({{"type", Value{"final"}},
                        {"id", Value{active_id}},
                        {"outcome", Value{outcome}},
                        {"text", Value{complete}},
                        {"text_truncated", Value{text_truncated}}}));
    finish_discarded();
  }
  shutdown.run();
  unwrap(engine.context().checkpoint());
  if (closing_)
    send(Value::object({{"type", Value{"closed"}}, {"id", Value{close_id_}}}));
  return 0;
}
} // namespace blackbird
