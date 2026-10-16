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
  std::array<std::byte, 4> header{};
  const auto size = static_cast<std::uint32_t>(bytes.size());
  for (unsigned i = 0; i < 4; ++i)
    header[i] = std::byte{static_cast<unsigned char>(size >> ((3 - i) * 8))};
  write_all(output_, header);
  write_all(output_, bytes);
}
void ApplicationConnection::error(std::string_view id, std::string_view message) {
  send(Value::object({{"type", Value{"error"}},
                      {"id", Value{std::string{id}}},
                      {"message", Value{std::string{message}}}}));
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
    const auto ready = ::poll(&descriptor, 1, wait ? -1 : 0);
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
  return *packet;
}
void ApplicationConnection::command(Value packet) {
  std::string id;
  try {
    id = text(packet, "id");
    if (id.empty() || id.size() > 128)
      throw Error{ErrorCode::invalid_range};
    const auto &type = text(packet, "type");
    if (type == "turn" || type == "steer") {
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
  }
}
int ApplicationConnection::run(CodingEngine &engine, AuditLog &log,
                               const SessionSettings &settings,
                               const std::filesystem::path &session) {
  std::string active_id, complete;
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
  send(Value::object({{"type", Value{"ready"}},
                      {"id", Value{open_id_}},
                      {"protocol", Value{Number{1}}},
                      {"model", Value{settings.model}},
                      {"session", Value{session.string()}}}));
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
        error(active_id, error_name(failure.code));
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
