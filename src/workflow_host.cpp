#include "blackbird/workflow_host.hpp"
#include "blackbird/context.hpp"
#include "blackbird/process_lifetime.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <climits>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace blackbird {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::string_view socket_name = "workflow.sock";
class Descriptor {
public:
  explicit Descriptor(int fd = -1) noexcept : fd_(fd) {}
  ~Descriptor() { reset(); }
  Descriptor(Descriptor &&other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  Descriptor &operator=(Descriptor &&other) noexcept {
    if (this != &other) {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
  int get() const noexcept { return fd_; }
  void reset() noexcept {
    if (fd_ >= 0) {
      const int saved = errno;
      ::close(fd_);
      errno = saved;
      fd_ = -1;
    }
  }

private:
  int fd_;
};
int configure(int fd) {
  if (fd < 0)
    throw Error{ErrorCode::io, errno};
  Descriptor held{fd};
  const int descriptor_flags = ::fcntl(fd, F_GETFD);
  const int status_flags = ::fcntl(fd, F_GETFL);
  if (descriptor_flags < 0 || status_flags < 0 ||
      ::fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0 ||
      ::fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) < 0)
    throw Error{ErrorCode::io, errno};
  const int result = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
  if (result < 0)
    throw Error{ErrorCode::io, errno};
  return result;
}
Descriptor socket_descriptor() {
  const std::lock_guard spawn{detail::process_spawn_mutex};
  Descriptor socket{configure(::socket(AF_UNIX, SOCK_STREAM, 0))};
#ifdef SO_NOSIGPIPE
  const int enabled = 1;
  if (::setsockopt(socket.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) !=
      0)
    throw Error{ErrorCode::io, errno};
#endif
  return socket;
}
Descriptor private_directory(const std::filesystem::path &session) {
  const std::lock_guard spawn{detail::process_spawn_mutex};
  Descriptor directory{
      configure(::open(session.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW))};
  struct stat observed{};
  if (::fstat(directory.get(), &observed) != 0)
    throw Error{ErrorCode::io, errno};
  if (!S_ISDIR(observed.st_mode) || observed.st_uid != ::geteuid() ||
      (observed.st_mode & 077) != 0)
    throw Error{ErrorCode::invalid_range};
  return directory;
}
sockaddr_un address(const std::filesystem::path &endpoint) {
  sockaddr_un result{};
  result.sun_family = AF_UNIX;
  const auto &name = endpoint.native();
  if (name.empty() || name.find('\0') != std::string::npos ||
      name.size() >= sizeof(result.sun_path))
    throw Error{ErrorCode::invalid_range};
  std::copy(name.begin(), name.end(), result.sun_path);
  return result;
}
void same_user(int fd) {
#if defined(__linux__)
  ucred credentials{};
  socklen_t size = sizeof(credentials);
  if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0)
    throw Error{ErrorCode::io, errno};
  if (size != sizeof(credentials) || credentials.uid != ::geteuid())
    throw Error{ErrorCode::invalid_identity};
#else
  uid_t user = 0;
  gid_t group = 0;
  if (::getpeereid(fd, &user, &group) != 0)
    throw Error{ErrorCode::io, errno};
  if (user != ::geteuid())
    throw Error{ErrorCode::invalid_identity};
#endif
}
ssize_t send_bytes(int fd, ByteView bytes) {
#ifdef MSG_NOSIGNAL
  return ::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
#else
  return ::send(fd, bytes.data(), bytes.size(), 0);
#endif
}
std::uint32_t length(const std::array<std::byte, 4> &header) {
  std::uint32_t result = 0;
  for (unsigned shift = 0; shift < 32; shift += 8)
    result |= std::to_integer<std::uint32_t>(header[shift / 8]) << shift;
  return result;
}
std::vector<std::byte> frame(const Value &value, std::size_t maximum) {
  const auto packet = unwrap(encode_packet(value, {.bytes = maximum}));
  if (packet.size() > UINT32_MAX)
    throw Error{ErrorCode::capacity};
  std::vector<std::byte> result;
  result.reserve(packet.size() + 4);
  const auto size = static_cast<std::uint32_t>(packet.size());
  for (unsigned shift = 0; shift < 32; shift += 8)
    result.push_back(static_cast<std::byte>((size >> shift) & 255));
  result.insert(result.end(), packet.begin(), packet.end());
  return result;
}
Value error_value(Error error) {
  return Value::object({{"ok", Value{false}},
                        {"code", Value{Number{static_cast<unsigned>(error.code)}}},
                        {"detail", Value{Number{error.detail}}}});
}
void wait_ready(int fd, short event, Clock::time_point deadline) {
  for (;;) {
    const auto now = Clock::now();
    if (now >= deadline)
      throw Error{ErrorCode::interrupted, ETIMEDOUT};
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
    const auto timeout =
        static_cast<int>(std::min<std::int64_t>(remaining.count(), INT_MAX));
    pollfd entry{fd, event, 0};
    const int result = ::poll(&entry, 1, timeout);
    if (result < 0 && errno == EINTR)
      continue;
    if (result < 0)
      throw Error{ErrorCode::io, errno};
    if (result > 0) {
      if (entry.revents & POLLNVAL)
        throw Error{ErrorCode::io, EBADF};
      return;
    }
  }
}
template <class Integer> Integer native_integer(const Value &value) {
  const auto *number = std::get_if<Number>(&value.value());
  if (!number)
    throw Error{ErrorCode::corrupt};
  const auto digits = number->text();
  Integer result = 0;
  const auto parsed =
      std::from_chars(digits.data(), digits.data() + digits.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size())
    throw Error{ErrorCode::corrupt};
  return result;
}
} // namespace

struct WorkflowHost::Impl {
  struct Client {
    Descriptor fd;
    std::array<std::byte, 4> header{};
    std::size_t header_read = 0, body_read = 0, written = 0;
    std::vector<std::byte> body;
    std::shared_ptr<const std::vector<std::byte>> output;
    std::size_t charged = 0;
    Clock::time_point deadline;
  };
  std::filesystem::path path;
  Dispatch dispatch;
  Tick tick;
  WorkflowHostLimits limits;
  std::thread::id thread = std::this_thread::get_id();
  pid_t process = ::getpid();
  Descriptor directory, listener, wake_read, wake_write;
  dev_t device = 0;
  ino_t inode = 0;
  bool bound = false, serving = false;
  std::atomic_int wake_fault{0};
  std::size_t pending = 0;
  std::vector<Client> clients;
  std::shared_ptr<const std::vector<std::byte>> capacity_error, malformed_error,
      unknown_error;
  Impl(const std::filesystem::path &session, Dispatch handler, Tick owner_tick,
       WorkflowHostLimits policy)
      : path(WorkflowHost::endpoint(session)), dispatch(std::move(handler)),
        tick(std::move(owner_tick)), limits(policy),
        directory(private_directory(session)), listener(socket_descriptor()) {
    if (!dispatch || limits.frame_bytes < 256 || limits.frame_bytes > UINT32_MAX ||
        !limits.clients || limits.pending_bytes < limits.frame_bytes ||
        limits.client_timeout <= std::chrono::milliseconds::zero() ||
        limits.client_timeout > std::chrono::hours{24})
      throw Error{ErrorCode::invalid_range};
    const auto endpoint = address(path);
    struct stat previous{};
    if (::fstatat(directory.get(), socket_name.data(), &previous,
                  AT_SYMLINK_NOFOLLOW) == 0)
      throw Error{ErrorCode::conflict};
    if (errno != ENOENT)
      throw Error{ErrorCode::io, errno};
    clients.reserve(limits.clients);
    capacity_error = std::make_shared<const std::vector<std::byte>>(
        frame(error_value({ErrorCode::capacity}), limits.frame_bytes));
    malformed_error = std::make_shared<const std::vector<std::byte>>(
        frame(error_value({ErrorCode::corrupt}), limits.frame_bytes));
    unknown_error = std::make_shared<const std::vector<std::byte>>(
        frame(error_value({ErrorCode::external_unknown}), limits.frame_bytes));
    {
      const std::lock_guard spawn{detail::process_spawn_mutex};
      std::array<int, 2> endpoints{-1, -1};
      if (::socketpair(AF_UNIX, SOCK_STREAM, 0, endpoints.data()) != 0)
        throw Error{ErrorCode::io, errno};
      Descriptor write{endpoints[1]};
      wake_read = Descriptor{configure(endpoints[0])};
      const int duplicate = ::dup(write.get());
      wake_write = Descriptor{configure(duplicate)};
#ifdef SO_NOSIGPIPE
      const int enabled = 1;
      if (::setsockopt(wake_write.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                       sizeof(enabled)) != 0)
        throw Error{ErrorCode::io, errno};
#endif
    }
    if (::bind(listener.get(), reinterpret_cast<const sockaddr *>(&endpoint),
               sizeof(endpoint)) != 0)
      throw Error{ErrorCode::io, errno};
    if (::fstatat(directory.get(), socket_name.data(), &previous,
                  AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISSOCK(previous.st_mode) || previous.st_uid != ::geteuid())
      throw Error{ErrorCode::io, errno};
    device = previous.st_dev;
    inode = previous.st_ino;
    bound = true;
    try {
      if (::fchmodat(directory.get(), socket_name.data(), 0600, 0) != 0 ||
          ::listen(listener.get(), 32) != 0)
        throw Error{ErrorCode::io, errno};
    } catch (...) {
      unlink_owned();
      throw;
    }
  }
  ~Impl() { unlink_owned(); }
  void owner() const {
    if (thread != std::this_thread::get_id() || process != ::getpid())
      throw Error{ErrorCode::wrong_incarnation};
  }
  void unlink_owned() noexcept {
    if (!bound || process != ::getpid())
      return;
    struct stat actual{};
    if (::fstatat(directory.get(), socket_name.data(), &actual, AT_SYMLINK_NOFOLLOW) ==
            0 &&
        S_ISSOCK(actual.st_mode) && actual.st_uid == ::geteuid() &&
        actual.st_dev == device && actual.st_ino == inode)
      ::unlinkat(directory.get(), socket_name.data(), 0);
    bound = false;
  }
  void release_input(Client &client) noexcept {
    pending -= client.body.size();
    std::vector<std::byte>{}.swap(client.body);
  }
  void error(Client &client, std::shared_ptr<const std::vector<std::byte>> output) {
    release_input(client);
    client.output = std::move(output);
    client.written = 0;
  }
  void reply(Client &client, const Value &value) {
    auto encoded = frame(value, limits.frame_bytes);
    if (encoded.size() > limits.pending_bytes - pending) {
      error(client, capacity_error);
      return;
    }
    auto output = std::make_shared<const std::vector<std::byte>>(std::move(encoded));
    client.charged = output->size();
    pending += client.charged;
    client.output = std::move(output);
  }
  void accept_clients() {
    for (std::size_t accepted = 0; accepted < limits.clients; ++accepted) {
      Descriptor incoming;
      {
        const std::lock_guard spawn{detail::process_spawn_mutex};
        const int fd = ::accept(listener.get(), nullptr, nullptr);
        if (fd < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return;
          throw Error{ErrorCode::io, errno};
        }
        incoming = Descriptor{configure(fd)};
      }
#ifdef SO_NOSIGPIPE
      const int enabled = 1;
      if (::setsockopt(incoming.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                       sizeof(enabled)) != 0)
        continue;
#endif
      try {
        same_user(incoming.get());
      } catch (const Error &) {
        continue;
      }
      if (clients.size() >= limits.clients) {
        (void)send_bytes(incoming.get(), *capacity_error);
        continue;
      }
      clients.push_back({std::move(incoming),
                         {},
                         0,
                         0,
                         0,
                         {},
                         {},
                         0,
                         Clock::now() + limits.client_timeout});
    }
  }
  void read_client(Client &client) {
    if (client.header_read < client.header.size()) {
      const auto received =
          ::read(client.fd.get(), client.header.data() + client.header_read,
                 client.header.size() - client.header_read);
      if (received <= 0) {
        if (received == 0 ||
            (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
          client.fd.reset();
        return;
      }
      client.header_read += static_cast<std::size_t>(received);
      if (client.header_read != client.header.size())
        return;
      const auto size = length(client.header);
      if (!size || size > limits.frame_bytes) {
        error(client, malformed_error);
        return;
      }
      if (size > limits.pending_bytes - pending) {
        error(client, capacity_error);
        return;
      }
      client.body.resize(size);
      pending += size;
    }
    const auto received =
        ::read(client.fd.get(), client.body.data() + client.body_read,
               std::min<std::size_t>(65536, client.body.size() - client.body_read));
    if (received <= 0) {
      if (received == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        client.fd.reset();
      return;
    }
    client.body_read += static_cast<std::size_t>(received);
    if (client.body_read != client.body.size())
      return;
    auto request = decode_packet(client.body, {.bytes = limits.frame_bytes});
    release_input(client);
    if (!request.has_value()) {
      error(client, malformed_error);
      return;
    }
    try {
      auto result = dispatch(request.value());
      reply(client, Value::object({{"ok", Value{true}}, {"value", std::move(result)}}));
    } catch (const Error &failure) {
      try {
        reply(client, error_value(failure));
      } catch (...) {
        error(client, unknown_error);
      }
    } catch (...) {
      error(client, unknown_error);
    }
    if (tick)
      tick();
  }
  void write_client(Client &client) {
    const auto bytes = ByteView{*client.output}.subspan(client.written);
    const auto written = send_bytes(
        client.fd.get(), bytes.first(std::min<std::size_t>(bytes.size(), 65536)));
    if (written <= 0) {
      if (written == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        client.fd.reset();
      return;
    }
    client.written += static_cast<std::size_t>(written);
    if (client.written == client.output->size())
      client.fd.reset();
  }
  void reap() {
    for (const auto &client : clients)
      if (client.fd.get() < 0)
        pending -= client.body.size() + client.charged;
    std::erase_if(clients, [](const auto &client) { return client.fd.get() < 0; });
  }
};

std::filesystem::path WorkflowHost::endpoint(const std::filesystem::path &session) {
  return std::filesystem::absolute(session) / socket_name;
}
WorkflowHost::WorkflowHost(const std::filesystem::path &session, Dispatch dispatch,
                           Tick tick, WorkflowHostLimits limits)
    : impl_(std::make_unique<Impl>(session, std::move(dispatch), std::move(tick),
                                   limits)) {}
WorkflowHost::~WorkflowHost() = default;
bool WorkflowHost::remove_stale_endpoint(const std::filesystem::path &session) {
  auto directory = private_directory(session);
  const auto target = address(endpoint(session));
  struct stat original{};
  if (::fstatat(directory.get(), socket_name.data(), &original, AT_SYMLINK_NOFOLLOW) !=
      0) {
    if (errno == ENOENT)
      return false;
    throw Error{ErrorCode::io, errno};
  }
  if (!S_ISSOCK(original.st_mode) || original.st_uid != ::geteuid())
    throw Error{ErrorCode::invalid_identity};
  auto probe = socket_descriptor();
  if (::connect(probe.get(), reinterpret_cast<const sockaddr *>(&target),
                sizeof(target)) == 0)
    throw Error{ErrorCode::busy};
  if (errno != ECONNREFUSED)
    throw Error{ErrorCode::io, errno};
  struct stat current{};
  if (::fstatat(directory.get(), socket_name.data(), &current, AT_SYMLINK_NOFOLLOW) !=
      0)
    throw Error{ErrorCode::io, errno};
  if (!S_ISSOCK(current.st_mode) || current.st_uid != original.st_uid ||
      current.st_dev != original.st_dev || current.st_ino != original.st_ino)
    throw Error{ErrorCode::conflict};
  if (::unlinkat(directory.get(), socket_name.data(), 0) != 0)
    throw Error{ErrorCode::io, errno};
  return true;
}
void WorkflowHost::wake() noexcept {
  if (impl_->process != ::getpid())
    return;
  const std::byte byte{1};
  const int saved = errno;
  ssize_t result = 0;
  do {
    result = send_bytes(impl_->wake_write.get(), {&byte, 1});
  } while (result < 0 && errno == EINTR);
  if (result != 1 && (result >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK))) {
    const int failure = result < 0 ? errno : EIO;
    int clear = 0;
    impl_->wake_fault.compare_exchange_strong(clear, failure);
    // Receiver shutdown independently wakes poll without repeating a failed
    // send. It also works if only the sending descriptor was lost. No writer access.
    (void)::shutdown(impl_->wake_read.get(), SHUT_RDWR);
  }
  errno = saved;
}
void WorkflowHost::serve(const Stop &stop) {
  impl_->owner();
  if (!stop || impl_->serving)
    throw Error{ErrorCode::busy};
  impl_->serving = true;
  struct Reset {
    bool &serving;
    ~Reset() { serving = false; }
  } reset{impl_->serving};
  bool closing = false;
  std::vector<pollfd> descriptors;
  descriptors.reserve(impl_->limits.clients + 2);
  if (impl_->tick)
    impl_->tick();
  for (;;) {
    if (const auto failure = impl_->wake_fault.load())
      throw Error{ErrorCode::io, failure};
    closing = closing || stop();
    if (closing) {
      for (auto &client : impl_->clients)
        if (!client.output)
          client.fd.reset();
      impl_->reap();
      if (impl_->clients.empty())
        return;
    }
    descriptors.clear();
    descriptors.push_back(
        {impl_->listener.get(), static_cast<short>(closing ? 0 : POLLIN), 0});
    descriptors.push_back({impl_->wake_read.get(), POLLIN, 0});
    for (const auto &client : impl_->clients)
      descriptors.push_back(
          {client.fd.get(), static_cast<short>(client.output ? POLLOUT : POLLIN), 0});
    const auto count = impl_->clients.size();
    int timeout = -1;
    const auto now = Clock::now();
    for (const auto &client : impl_->clients) {
      const auto remaining =
          std::chrono::ceil<std::chrono::milliseconds>(client.deadline - now);
      const auto finite =
          static_cast<int>(std::clamp<std::int64_t>(remaining.count(), 0, INT_MAX));
      timeout = timeout < 0 ? finite : std::min(timeout, finite);
    }
    const int ready =
        ::poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()), timeout);
    if (ready < 0 && errno != EINTR)
      throw Error{ErrorCode::io, errno};
    if (ready >= 0) {
      if (const auto failure = impl_->wake_fault.load())
        throw Error{ErrorCode::io, failure};
      if (descriptors[1].revents & POLLIN) {
        std::array<std::byte, 4096> bytes{};
        (void)::read(impl_->wake_read.get(), bytes.data(), bytes.size());
      }
      if (!closing && descriptors[0].revents & POLLIN)
        impl_->accept_clients();
      for (std::size_t index = 0; index < count; ++index) {
        auto &client = impl_->clients[index];
        const auto events = descriptors[index + 2].revents;
        if (Clock::now() >= client.deadline || (events & POLLNVAL)) {
          client.fd.reset();
          continue;
        }
        if (client.output && (events & (POLLOUT | POLLHUP | POLLERR)))
          impl_->write_client(client);
        else if (!client.output && (events & (POLLIN | POLLHUP | POLLERR))) {
          try {
            impl_->read_client(client);
          } catch (const std::bad_alloc &) {
            impl_->error(client, impl_->capacity_error);
          }
        }
      }
    }
    impl_->reap();
    if (impl_->tick)
      impl_->tick();
  }
}
Value WorkflowHost::request(const std::filesystem::path &session, const Value &request,
                            std::chrono::milliseconds timeout,
                            std::size_t frame_bytes) {
  if (timeout <= std::chrono::milliseconds::zero() ||
      timeout > std::chrono::hours{24} || frame_bytes < 256 || frame_bytes > UINT32_MAX)
    throw Error{ErrorCode::invalid_range};
  auto directory = private_directory(session);
  const auto path = endpoint(session);
  const auto target = address(path);
  struct stat endpoint_stat{};
  if (::fstatat(directory.get(), socket_name.data(), &endpoint_stat,
                AT_SYMLINK_NOFOLLOW) != 0)
    throw Error{ErrorCode::io, errno};
  if (!S_ISSOCK(endpoint_stat.st_mode) || endpoint_stat.st_uid != ::geteuid() ||
      (endpoint_stat.st_mode & 077) != 0)
    throw Error{ErrorCode::invalid_identity};
  auto socket = socket_descriptor();
  const auto deadline = Clock::now() + timeout;
  if (::connect(socket.get(), reinterpret_cast<const sockaddr *>(&target),
                sizeof(target)) != 0) {
    if (errno != EINPROGRESS)
      throw Error{ErrorCode::io, errno};
    wait_ready(socket.get(), POLLOUT, deadline);
    int failure = 0;
    socklen_t size = sizeof(failure);
    if (::getsockopt(socket.get(), SOL_SOCKET, SO_ERROR, &failure, &size) != 0)
      throw Error{ErrorCode::io, errno};
    if (failure)
      throw Error{ErrorCode::io, failure};
  }
  same_user(socket.get());
  const auto outgoing = frame(request, frame_bytes);
  std::size_t written = 0;
  while (written < outgoing.size()) {
    wait_ready(socket.get(), POLLOUT, deadline);
    const auto count = send_bytes(socket.get(), ByteView{outgoing}.subspan(written));
    if (count > 0)
      written += static_cast<std::size_t>(count);
    else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
      throw Error{ErrorCode::io, count < 0 ? errno : EPIPE};
  }
  const auto read_exact = [&](MutableByteView destination) {
    std::size_t received = 0;
    while (received < destination.size()) {
      wait_ready(socket.get(), POLLIN, deadline);
      const auto count = ::read(socket.get(), destination.data() + received,
                                destination.size() - received);
      if (count > 0)
        received += static_cast<std::size_t>(count);
      else if (count == 0)
        throw Error{ErrorCode::incomplete};
      else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        throw Error{ErrorCode::io, errno};
    }
  };
  std::array<std::byte, 4> incoming{};
  read_exact(incoming);
  const auto size = length(incoming);
  if (!size || size > frame_bytes)
    throw Error{ErrorCode::capacity};
  std::vector<std::byte> bytes(size);
  read_exact(bytes);
  const auto reply = unwrap(decode_packet(bytes, {.bytes = frame_bytes}));
  const auto *object = std::get_if<Value::Object>(&reply.value());
  const auto *ok = reply.find("ok");
  if (!object || !ok)
    throw Error{ErrorCode::corrupt};
  if (const auto *success = std::get_if<bool>(&ok->value())) {
    if (*success) {
      const auto *value = reply.find("value");
      if (!value || object->size() != 2)
        throw Error{ErrorCode::corrupt};
      return *value;
    }
    const auto *code_value = reply.find("code");
    const auto *detail = reply.find("detail");
    if (!code_value || !detail || object->size() != 3)
      throw Error{ErrorCode::corrupt};
    const auto code = native_integer<unsigned>(*code_value);
    if (code > static_cast<unsigned>(ErrorCode::provider_rate_limit))
      throw Error{ErrorCode::corrupt};
    throw Error{static_cast<ErrorCode>(code), native_integer<std::int64_t>(*detail)};
  }
  throw Error{ErrorCode::corrupt};
}
} // namespace blackbird
