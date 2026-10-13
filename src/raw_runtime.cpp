#include "blackbird/raw_runtime.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/provider_original.hpp"
#include "blackbird/raw_audit.hpp"
#include "blackbird/raw_auth.hpp"
#include "blackbird/raw_authored.hpp"
#include "blackbird/raw_command.hpp"
#include "blackbird/raw_context.hpp"
#include "blackbird/raw_operation.hpp"
#include "blackbird/raw_participants.hpp"
#include "blackbird/raw_provider.hpp"
#include "blackbird/raw_query.hpp"
#include "blackbird/raw_retained.hpp"
#include "blackbird/raw_sampler.hpp"
#include "blackbird/raw_storage.hpp"
#include "blackbird/raw_tasks.hpp"
#include "blackbird/raw_turn.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <poll.h>
#include <set>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace blackbird {
namespace {
template <class T> T take(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
void take(Result<void> result) {
  if (!result.has_value())
    throw result.error();
}
template <class T> ByteView bytes(const T &value) noexcept {
  return std::as_bytes(std::span{&value, std::size_t{1}});
}
struct Descriptor {
  Descriptor() = default;
  explicit Descriptor(int descriptor) : value(descriptor) {}
  ~Descriptor() {
    if (value >= 0)
      ::close(value);
  }
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
  Descriptor(Descriptor &&other) noexcept : value(std::exchange(other.value, -1)) {}
  Descriptor &operator=(Descriptor &&other) noexcept {
    if (this != &other) {
      if (value >= 0)
        ::close(value);
      value = std::exchange(other.value, -1);
    }
    return *this;
  }
  int value = -1;
};
constexpr RawCapacities capacities{64 * 1024 * 1024, 256 * 1024, 128 * 1024};
constexpr RawCapacities recorder_capacities{16, 16, 128 * 1024};
constexpr std::size_t registration_size = 80;
constexpr std::size_t max_lanes = 256;
constexpr std::uint32_t failure_site = 8000;
constexpr std::array<std::byte, 12> bootstrap_marker{
    std::byte{'B'}, std::byte{'B'}, std::byte{'R'}, std::byte{'A'},
    std::byte{'W'}, std::byte{'B'}, std::byte{'O'}, std::byte{'O'},
    std::byte{1},   std::byte{0},   std::byte{0},   std::byte{0}};
constexpr std::uint64_t ingest_failure_size =
    raw_envelope_size + sizeof(ErrorCode) + sizeof(std::int64_t);
struct RestoreErrno {
  int saved = errno;
  ~RestoreErrno() { errno = saved; }
};
constexpr std::uint64_t failure_size =
    raw_envelope_size + sizeof(ErrorCode) + sizeof(std::int64_t) +
    3 * sizeof(std::uint64_t) + sizeof(bool) + 2 * sizeof(IdentityBytes) +
    sizeof(RawPartition) + 2 * sizeof(std::uint64_t) + sizeof(bool);
struct FailureSource {
  IdentityBytes journal{}, lane{};
  RawPartition partition = RawPartition::data;
  std::uint64_t start = 0, end = 0;
  bool acquired = false;
};
struct Registration {
  IdentityBytes epoch{};
  RawCapacities sizes{};
  std::array<char, 32> name{};
  bool closed = false;
};
std::array<std::byte, registration_size> encode_registration(const Registration &lane) {
  std::array<std::byte, registration_size> output{};
  std::memcpy(output.data(), lane.epoch.data(), 16);
  std::memcpy(output.data() + 16, lane.sizes.data(), 24);
  std::memcpy(output.data() + 40, lane.name.data(), 32);
  output[72] = lane.closed ? std::byte{1} : std::byte{0};
  return output;
}
Registration decode_registration(ByteView input) {
  if (input.size() != registration_size)
    throw Error{ErrorCode::corrupt};
  Registration lane;
  std::memcpy(lane.epoch.data(), input.data(), 16);
  std::memcpy(lane.sizes.data(), input.data() + 16, 24);
  std::memcpy(lane.name.data(), input.data() + 40, 32);
  if (lane.name[0] != '/' || lane.name[1] != 'b' || lane.name[2] != 'b')
    throw Error{ErrorCode::corrupt};
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 12; ++i) {
    const auto value = std::to_integer<unsigned>(lane.epoch[i]);
    if (lane.name[3 + 2 * i] != hex[value >> 4] ||
        lane.name[4 + 2 * i] != hex[value & 15U])
      throw Error{ErrorCode::corrupt};
  }
  for (std::size_t i = 27; i < lane.name.size(); ++i)
    if (lane.name[i] != 0)
      throw Error{ErrorCode::corrupt};
  if (input[72] != std::byte{0} && input[72] != std::byte{1})
    throw Error{ErrorCode::corrupt};
  lane.closed = input[72] == std::byte{1};
  for (std::size_t i = 73; i < input.size(); ++i)
    if (input[i] != std::byte{0})
      throw Error{ErrorCode::corrupt};
  return lane;
}
Registration new_registration(RawCapacities sizes = capacities) {
  Registration lane;
  lane.sizes = sizes;
  if (::getentropy(lane.epoch.data(), lane.epoch.size()) != 0)
    throw Error{ErrorCode::io, errno};
  constexpr char hex[] = "0123456789abcdef";
  lane.name[0] = '/';
  lane.name[1] = 'b';
  lane.name[2] = 'b';
  for (std::size_t i = 0; i < 12; ++i) {
    const auto value = std::to_integer<unsigned>(lane.epoch[i]);
    lane.name[3 + 2 * i] = hex[value >> 4];
    lane.name[4 + 2 * i] = hex[value & 15U];
  }
  return lane;
}
void write_all(int descriptor, ByteView input) {
  std::size_t offset = 0;
  while (offset < input.size()) {
    const auto count =
        ::write(descriptor, input.data() + offset, input.size() - offset);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      throw Error{ErrorCode::io, errno};
    }
    if (count == 0)
      throw Error{ErrorCode::io};
    offset += static_cast<std::size_t>(count);
  }
}
void sync_file(int descriptor) {
  if (::fsync(descriptor) != 0)
    throw Error{ErrorCode::io, errno};
}
std::vector<std::byte> read_file(const std::string &path, std::size_t maximum) {
  Descriptor file{::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
  if (file.value < 0)
    throw Error{ErrorCode::io, errno};
  struct stat info{};
  if (::fstat(file.value, &info) != 0)
    throw Error{ErrorCode::io, errno};
  if (!S_ISREG(info.st_mode) || info.st_size < 0 ||
      static_cast<std::uint64_t>(info.st_size) > maximum)
    throw Error{ErrorCode::capacity};
  std::vector<std::byte> output(static_cast<std::size_t>(info.st_size));
  std::size_t offset = 0;
  while (offset < output.size()) {
    const auto count =
        ::read(file.value, output.data() + offset, output.size() - offset);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      throw Error{ErrorCode::io, errno};
    }
    if (count == 0)
      throw Error{ErrorCode::incomplete};
    offset += static_cast<std::size_t>(count);
  }
  return output;
}
void write_atomic(const std::string &path, const std::string &name, ByteView body,
                  bool sync) {
  const auto nonce = new_registration(recorder_capacities);
  const auto temporary =
      path + "/" + name + ".pending." + std::string{nonce.name.data() + 1};
  Descriptor file{
      ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
  if (file.value < 0)
    throw Error{ErrorCode::io, errno};
  write_all(file.value, body);
  if (sync)
    sync_file(file.value);
  file = Descriptor{};
  if (::rename(temporary.c_str(), (path + "/" + name).c_str()) != 0)
    throw Error{ErrorCode::io, errno};
}
void publish_cursor(const std::string &path, std::uint64_t segment,
                    const RawAuditCursor &preserved) {
  std::array<std::byte, 40> publication{};
  std::memcpy(publication.data(), &segment, 8);
  std::memcpy(publication.data() + 8, preserved.journal.bytes().data(), 16);
  std::memcpy(publication.data() + 24, &preserved.sequence, 8);
  std::memcpy(publication.data() + 32, &preserved.offset, 8);
  write_atomic(path, "published", publication, false);
}
void register_lane(const std::string &path, const Registration &registration) {
  const auto name = std::string{registration.closed ? "closed." : "lane."} +
                    std::string{registration.name.data() + 1};
  const auto independent =
      std::string{registration.closed ? "retired." : "registered."} +
      std::string{registration.name.data() + 1};
  if (std::filesystem::exists(std::filesystem::path{path} / name) ||
      std::filesystem::exists(std::filesystem::path{path} / independent))
    throw Error{ErrorCode::conflict};
  write_atomic(path, independent, encode_registration(registration), true);
  write_atomic(path, name, encode_registration(registration), true);
  auto directory = take(NativeJournalDirectory::open(path));
  take(directory.synchronize_directory(SyncStrength::data));
}
std::optional<std::vector<std::byte>> metadata_copy(const std::string &path,
                                                    std::size_t maximum) {
  try {
    return read_file(path, maximum);
  } catch (const Error &error) {
    // Missing or structurally damaged bytes may have an independent copy.
    // An unreadable file never becomes permission to infer its contents.
    if ((error.code == ErrorCode::io && error.detail == ENOENT) ||
        error.code == ErrorCode::capacity || error.code == ErrorCode::incomplete)
      return std::nullopt;
    throw;
  }
}
std::optional<Registration> registration_copy(const std::filesystem::path &path,
                                              const std::string &name, bool closed) {
  const auto encoded = metadata_copy(path.string(), registration_size);
  if (!encoded)
    return std::nullopt;
  try {
    const auto registration = decode_registration(*encoded);
    if (registration.closed != closed ||
        std::string{registration.name.data() + 1} != name)
      throw Error{ErrorCode::corrupt};
    return registration;
  } catch (const Error &error) {
    if (error.code == ErrorCode::corrupt)
      return std::nullopt;
    throw;
  }
}
Registration recover_registration(const std::string &path, const std::string &name,
                                  bool closed) {
  const auto primary = registration_copy(
      path + "/" + (closed ? "closed." : "lane.") + name, name, closed);
  const auto independent = registration_copy(
      path + "/" + (closed ? "retired." : "registered.") + name, name, closed);
  if (!primary && !independent)
    throw Error{ErrorCode::corrupt};
  if (primary && independent &&
      encode_registration(*primary) != encode_registration(*independent))
    throw Error{ErrorCode::conflict};
  return primary ? *primary : *independent;
}
std::optional<JournalHeader> header_copy(const std::string &path) {
  const auto encoded = metadata_copy(path, journal_header_size);
  if (!encoded)
    return std::nullopt;
  const auto decoded = decode_journal_header(*encoded);
  if (!decoded.has_value())
    return std::nullopt;
  return decoded.value();
}
void finish_closed_unlinks(const std::filesystem::path &path) {
  for (const auto &file : std::filesystem::directory_iterator{path}) {
    const auto name = file.path().filename().string();
    if (name.find(".pending.") != std::string::npos)
      continue;
    std::string lane_name;
    if (name.starts_with("lane."))
      lane_name = name.substr(5);
    else if (name.starts_with("registered.")) {
      lane_name = name.substr(11);
      if (std::filesystem::exists(path / ("lane." + lane_name)))
        continue;
    } else
      continue;
    const auto registration = recover_registration(path.string(), lane_name, false);
    if (std::filesystem::exists(path / ("closed." + lane_name)) ||
        std::filesystem::exists(path / ("retired." + lane_name))) {
      const auto closed = recover_registration(path.string(), lane_name, true);
      if (closed.epoch != registration.epoch || closed.sizes != registration.sizes ||
          closed.name != registration.name)
        throw Error{ErrorCode::conflict};
    }
    const auto unlinked = RawChannel::unlink(registration.name.data());
    if (!unlinked.has_value() &&
        !(unlinked.error().code == ErrorCode::io && unlinked.error().detail == ENOENT))
      throw unlinked.error();
  }
}
void write_seed(const std::string &path, std::uint64_t segment,
                std::span<const RawOrigin> prefixes) {
  std::vector<std::byte> output(8 + prefixes.size() * raw_origin_size);
  const auto count = static_cast<std::uint32_t>(prefixes.size());
  std::memcpy(output.data(), &count, 4);
  std::size_t offset = 8;
  for (const auto &prefix : prefixes) {
    std::memcpy(output.data() + offset, prefix.lane.data(), 16);
    std::memcpy(output.data() + offset + 16, &prefix.partition, 4);
    std::memcpy(output.data() + offset + 24, &prefix.source_start, 8);
    offset += raw_origin_size;
  }
  const auto checksum = crc32c(ByteView{output}.subspan(8));
  std::memcpy(output.data() + 4, &checksum, 4);
  write_atomic(path, "seed." + std::to_string(segment), output, true);
}
std::vector<RawOrigin> read_seed(const std::string &path, std::uint64_t segment,
                                 std::size_t max_entries) {
  const auto input = read_file(path + "/seed." + std::to_string(segment),
                               8 + max_entries * raw_origin_size);
  if (input.size() < 8)
    throw Error{ErrorCode::corrupt};
  std::uint32_t count = 0, checksum = 0;
  std::memcpy(&count, input.data(), 4);
  std::memcpy(&checksum, input.data() + 4, 4);
  if (input.size() != 8 + static_cast<std::size_t>(count) * raw_origin_size ||
      checksum != crc32c(ByteView{input}.subspan(8)))
    throw Error{ErrorCode::corrupt};
  std::vector<RawOrigin> output;
  output.reserve(count);
  for (std::size_t offset = 8; offset < input.size(); offset += raw_origin_size) {
    RawOrigin prefix{};
    std::memcpy(prefix.lane.data(), input.data() + offset, 16);
    std::memcpy(&prefix.partition, input.data() + offset + 16, 4);
    std::memcpy(&prefix.source_start, input.data() + offset + 24, 8);
    std::uint32_t reserved = 0;
    std::memcpy(&reserved, input.data() + offset + 20, 4);
    if (reserved != 0)
      throw Error{ErrorCode::corrupt};
    output.push_back(prefix);
  }
  return output;
}
void write_description(int descriptor, std::string_view text) {
  const auto length = static_cast<std::uint32_t>(text.size());
  write_all(descriptor, bytes(length));
  write_all(descriptor, std::as_bytes(std::span{text.data(), text.size()}));
}
void write_descriptions(const std::string &path) {
  Descriptor file{::open((path + "/descriptions").c_str(),
                         O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
  if (file.value < 0)
    throw Error{ErrorCode::io, errno};
  constexpr std::array<char, 8> magic{'B', 'B', 'R', 'A', 'W', 'A', 'B', 'I'};
  write_all(file.value, std::as_bytes(std::span{magic}));
  const std::array<std::uint32_t, 18> abi{2,
                                          sizeof(std::uint32_t),
                                          sizeof(std::uint64_t),
                                          sizeof(std::size_t),
                                          sizeof(clockid_t),
                                          sizeof(int),
                                          sizeof(time_t),
                                          sizeof(long),
                                          sizeof(bool),
                                          sizeof(ErrorCode),
                                          sizeof(RawRegionOutcome),
                                          sizeof(RawObservationKind),
                                          sizeof(RawRegionEnd),
                                          sizeof(RawPartition),
                                          sizeof(ssize_t),
                                          sizeof(pid_t),
                                          sizeof(uid_t),
                                          sizeof(short)};
  write_all(file.value, std::as_bytes(std::span{abi}));
  write_description(file.value, BLACKBIRD_RAW_BUILD_ID);
  write_description(file.value, __VERSION__);
  write_description(file.value,
                    "native little-endian; no struct padding; envelope=u32 length,u32 "
                    "site,u64 ordinal; origin=lane[16],u32 partition,u32 zero,u64 "
                    "source_start; source strings=size_t length,exact bytes");
  write_description(file.value,
                    "clock=clockid_t method,int return,int errno,time_t sec,long "
                    "nsec,bool cpu_read,clockid_t method,int return,int errno,time_t "
                    "sec,long nsec; errno meaningful on failure only");
  const std::array<clockid_t, 2> clocks{CLOCK_MONOTONIC, CLOCK_THREAD_CPUTIME_ID};
  write_all(file.value, std::as_bytes(std::span{clocks}));
  const auto region_count =
      static_cast<std::uint32_t>(raw_region_descriptions().size());
  write_all(file.value, bytes(region_count));
  for (const auto &region : raw_region_descriptions()) {
    const std::array<std::uint32_t, 5> sites{
        region.begin, region.end, region.observation, region.linkage, region.outcome};
    write_all(file.value, std::as_bytes(std::span{sites}));
    write_description(file.value, region.name);
    write_description(file.value, region.source);
  }
  write_description(
      file.value,
      "region BEGIN=clock,int uncaught; END=u64 begin,clock,int uncaught,u8 reason; "
      "OBS=u64 begin,u32 kind,u64 history,u64 bytes,u64 sequence; LINK=u64 "
      "begin,size_t revision_length,revision,size_t attempt_length,attempt; "
      "OUTCOME=u64 begin,u32 outcome");
  write_description(
      file.value,
      "outcome1=ready,2=compact,3=full_replay,4=admitted,5=success,6=rejected,7="
      "uncertain,8=returned,9=exception,10=flushed,11=small_frame,12=queued,13="
      "dispatch; "
      "observation1=request_bytes,2=source_bytes,3=encoded_event_bytes,4=journal_state;"
      " end1=explicit_finish,2=scope_exit; partition1=data,2=control,3=failure");
  write_description(
      file.value,
      "1100 worker dispatch=clock;1101 worker start=origin_lane[16],u64 "
      "dispatch_ordinal,clock;1110 region Error=u64 begin,ErrorCode code,i64 "
      "detail;1200 prepared request=size_t "
      "revision_length,revision,generation[16],size_t packet_length,exact existing "
      "BBM2 packet;1201 owner provider "
      "dispatch=attempt[16],invocation[16],generation[16],u64 ordinal,size_t "
      "retry_length,retry;1202 observed "
      "response=attempt[16],invocation[16],generation[16],u64 provider_call_ordinal");
  write_description(file.value,
                    "8000 recorder failure=ErrorCode code,i64 detail,u64 stage,u64 "
                    "physical_start,u64 written,bool "
                    "sync_attempted,journal[16],source_lane[16],RawPartition "
                    "partition,u64 source_start,u64 source_end,bool "
                    "source_acquired;stage1=append,2=open_recover,3=source_read,4="
                    "reclaim;8001 emitter ingest failure=ErrorCode code,i64 detail");
  write_description(file.value, raw_provider_description());
  write_description(file.value, raw_command_description());
  write_description(file.value, raw_sampler_description());
  write_description(file.value, raw_authored_description());
  write_description(file.value, raw_auth_description());
  write_description(file.value, raw_tasks_description());
  write_description(file.value, provider_original_description());
  write_description(file.value, raw_participants_description());
  write_description(file.value, raw_query_description());
  write_description(file.value, raw_turn_description());
  write_description(file.value, raw_context_description());
  write_description(file.value, raw_operation_description());
  write_description(file.value, raw_storage_description());
  write_description(file.value, raw_retained_description());
  for (unsigned code = 0; code <= static_cast<unsigned>(ErrorCode::provider_rate_limit);
       ++code) {
    const auto value = static_cast<ErrorCode>(code);
    write_all(file.value, bytes(value));
    write_description(file.value, error_name(value));
  }
  sync_file(file.value);
}
std::array<int, 2> make_pipe() {
  const std::lock_guard spawn_lock{detail::process_spawn_mutex};
  std::array<int, 2> output{};
  if (::pipe(output.data()) != 0)
    throw Error{ErrorCode::io, errno};
  for (int descriptor : output) {
    if (::fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0) {
      const auto error = errno;
      ::close(output[0]);
      ::close(output[1]);
      throw Error{ErrorCode::io, error};
    }
  }
  return output;
}
std::array<int, 2> control_pair() {
  const std::lock_guard spawn_lock{detail::process_spawn_mutex};
  std::array<int, 2> output{};
  if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, output.data()) != 0)
    throw Error{ErrorCode::io, errno};
  for (int descriptor : output) {
    if (::fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0) {
      const auto error = errno;
      ::close(output[0]);
      ::close(output[1]);
      throw Error{ErrorCode::io, error};
    }
  }
  return output;
}
void check_wait_control(const RawWaitControl &control) {
  std::array<pollfd, 2> watches{};
  std::size_t count = 0;
  for (int endpoint : control.cancellation_fds) {
    if (endpoint < -1)
      throw Error{ErrorCode::io, EBADF};
    if (endpoint >= 0)
      watches[count++] = {endpoint, POLLIN | POLLHUP, 0};
  }
  if (count != 0) {
    int result;
    do {
      result = ::poll(watches.data(), static_cast<nfds_t>(count), 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
      throw Error{ErrorCode::io, errno};
    for (std::size_t i = 0; i < count; ++i) {
      if ((watches[i].revents & POLLNVAL) != 0)
        throw Error{ErrorCode::io, EBADF};
      if (watches[i].revents != 0)
        throw Error{ErrorCode::interrupted, ECANCELED};
    }
  }
  if (control.deadline && std::chrono::steady_clock::now() >= *control.deadline)
    throw Error{ErrorCode::interrupted, ETIMEDOUT};
}
void wait_ready(std::span<pollfd> watches, const RawWaitControl &control) {
  if (watches.size() > 2)
    throw Error{ErrorCode::capacity};
  for (;;) {
    check_wait_control(control);
    std::array<pollfd, 4> all{};
    std::copy(watches.begin(), watches.end(), all.begin());
    std::size_t count = watches.size();
    for (int endpoint : control.cancellation_fds)
      if (endpoint >= 0)
        all[count++] = {endpoint, POLLIN | POLLHUP, 0};
    int timeout = -1;
    if (control.deadline) {
      const auto remaining = *control.deadline - std::chrono::steady_clock::now();
      if (remaining <= std::chrono::steady_clock::duration::zero())
        throw Error{ErrorCode::interrupted, ETIMEDOUT};
      const auto milliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
      timeout = milliseconds.count() >= std::numeric_limits<int>::max()
                    ? std::numeric_limits<int>::max()
                    : static_cast<int>(milliseconds.count()) + 1;
    }
    const auto result = ::poll(all.data(), static_cast<nfds_t>(count), timeout);
    if (result < 0) {
      if (errno == EINTR)
        continue;
      throw Error{ErrorCode::io, errno};
    }
    check_wait_control(control);
    if (result == 0)
      continue;
    bool ready = false;
    for (std::size_t i = 0; i < watches.size(); ++i) {
      watches[i].revents = all[i].revents;
      if ((all[i].revents & POLLNVAL) != 0)
        throw Error{ErrorCode::io, EBADF};
      ready = ready || all[i].revents != 0;
    }
    if (ready)
      return;
  }
}
void send_registration(int descriptor, const Registration &lane,
                       std::array<int, 2> endpoints, int flags = 0) {
  auto body = encode_registration(lane);
  iovec vector{body.data(), body.size()};
  alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(2 * sizeof(int))> control{};
  msghdr message{};
  message.msg_iov = &vector;
  message.msg_iovlen = 1;
  message.msg_control = control.data();
  message.msg_controllen = control.size();
  auto *header = CMSG_FIRSTHDR(&message);
  header->cmsg_level = SOL_SOCKET;
  header->cmsg_type = SCM_RIGHTS;
  header->cmsg_len = CMSG_LEN(2 * sizeof(int));
  std::memcpy(CMSG_DATA(header), endpoints.data(), 2 * sizeof(int));
  ssize_t count;
  do {
    count = ::sendmsg(descriptor, &message, flags);
  } while (count < 0 && errno == EINTR && flags == 0);
  if (count != static_cast<ssize_t>(body.size()))
    throw Error{ErrorCode::io, count < 0 ? errno : 0};
}
struct Received {
  std::array<std::byte, registration_size> body{};
  std::array<Descriptor, 2> endpoints{};
  ssize_t count = 0;
};
Received receive_control(int descriptor) {
  Received received;
  iovec vector{received.body.data(), received.body.size()};
  alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(2 * sizeof(int))> control{};
  msghdr message{};
  message.msg_iov = &vector;
  message.msg_iovlen = 1;
  message.msg_control = control.data();
  message.msg_controllen = control.size();
  do {
    received.count = ::recvmsg(descriptor, &message, 0);
  } while (received.count < 0 && errno == EINTR);
  if (received.count < 0)
    throw Error{ErrorCode::io, errno};
  if ((message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0)
    throw Error{ErrorCode::corrupt};
  if (auto *header = CMSG_FIRSTHDR(&message); header != nullptr) {
    if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
        header->cmsg_len != CMSG_LEN(2 * sizeof(int)))
      throw Error{ErrorCode::corrupt};
    std::array<int, 2> values{};
    std::memcpy(values.data(), CMSG_DATA(header), sizeof(values));
    received.endpoints[0] = Descriptor{values[0]};
    received.endpoints[1] = Descriptor{values[1]};
  }
  return received;
}
struct RecorderLane {
  Registration registration;
  RawChannel channel;
  Descriptor producer_endpoint, recorder_endpoint;
  // Only orphan recovery assigns this after the dead-owner/catalogue fence.
  bool quiescent = false;
  Result<void> acknowledge(RawPartition partition, std::uint64_t end) {
    return quiescent ? channel.acknowledge_quiescent(partition, end)
                     : channel.acknowledge(partition, end, recorder_endpoint.value);
  }
};
volatile sig_atomic_t child_notice = -1;
volatile sig_atomic_t recorder_stop = 0;
extern "C" void child_changed(int) {
  const int saved = errno;
  if (child_notice >= 0) {
    const char byte = 0;
    (void)::write(child_notice, &byte, 1);
  }
  errno = saved;
}
extern "C" void recorder_signal(int signal) {
  recorder_stop = signal == SIGTERM ? 2 : 1;
}
void failure(RawProducer &producer, Error error, const RawAuditProgress &progress,
             std::uint64_t stage, const FailureSource &source) {
  const std::array parts{bytes(error.code),
                         bytes(error.detail),
                         bytes(stage),
                         bytes(progress.start_offset),
                         bytes(progress.written_bytes),
                         bytes(progress.sync_attempted),
                         ByteView{source.journal},
                         ByteView{source.lane},
                         bytes(source.partition),
                         bytes(source.start),
                         bytes(source.end),
                         bytes(source.acquired)};
  take(producer.emit(failure_site, parts, RawPartition::failure));
}
std::unique_ptr<RawAudit> open_audit(NativeJournalDirectory &directory,
                                     const std::string &path, JournalHeader initial,
                                     std::vector<RecorderLane> &lanes,
                                     std::uint64_t &current_segment) {
  auto header = initial;
  std::vector<RawOrigin> inherited;
  std::unique_ptr<RawAudit> audit;
  const auto prefix_limit = (max_lanes + lanes.size() + 1) * 3;
  for (std::uint64_t segment = 0;; ++segment) {
    current_segment = segment;
    const auto name = "events." + std::to_string(segment);
    if (segment != 0) {
      const auto file_path = std::filesystem::path{path} / name;
      if (std::filesystem::exists(file_path)) {
        auto seeds = read_seed(path, segment, prefix_limit);
        for (const auto &seed : seeds)
          if (audit->preserved(seed.lane, seed.partition) != seed.source_start)
            throw Error{ErrorCode::conflict};
        inherited = std::move(seeds);
        std::array<std::byte, journal_header_size> encoded{};
        Descriptor file{::open(file_path.c_str(), O_RDONLY | O_CLOEXEC)};
        if (file.value < 0 || ::read(file.value, encoded.data(), encoded.size()) !=
                                  static_cast<ssize_t>(encoded.size()))
          throw Error{ErrorCode::io, errno};
        const auto observed = take(decode_journal_header(encoded));
        if (observed.environment != initial.environment ||
            observed.predecessor != header.predecessor ||
            observed.limits != initial.limits ||
            observed.issuer_namespace != initial.issuer_namespace)
          throw Error{ErrorCode::conflict};
        header = observed;
      } else {
        write_seed(path, segment, inherited);
        take(directory.synchronize_directory(SyncStrength::data));
      }
    }
    audit = take(RawAudit::open(directory, name, header, prefix_limit,
                                SyncStrength::data, inherited));
    inherited.clear();
    for (auto &lane : lanes) {
      for (auto partition :
           {RawPartition::data, RawPartition::control, RawPartition::failure}) {
        const auto end = audit->preserved(lane.registration.epoch, partition);
        const auto cursor = take(lane.channel.cursors(partition));
        if (end > cursor.published)
          throw Error{ErrorCode::conflict};
        if (end > cursor.acknowledged)
          take(lane.acknowledge(partition, end));
        if (end != 0)
          inherited.push_back({lane.registration.epoch, partition, end});
      }
    }
    const bool successor = std::filesystem::exists(
        std::filesystem::path{path} / ("events." + std::to_string(segment + 1)));
    const bool sealed = std::filesystem::exists(std::filesystem::path{path} /
                                                ("sealed." + std::to_string(segment)));
    if (!audit->recovery_error() && !successor && !sealed) {
      for (auto &lane : lanes)
        for (auto partition :
             {RawPartition::data, RawPartition::control, RawPartition::failure})
          if (take(lane.channel.cursors(partition)).acknowledged >
              audit->preserved(lane.registration.epoch, partition))
            throw Error{ErrorCode::conflict};
      return audit;
    }
    const auto prior = audit->cursor();
    IdentityBytes next{};
    if (::getentropy(next.data(), next.size()) != 0)
      throw Error{ErrorCode::io, errno};
    header =
        JournalHeader{initial.environment, take(AuditStreamId::from_bytes(next)),
                      initial.issuer_namespace, initial.limits,
                      JournalPredecessor{prior.journal, prior.sequence, prior.offset}};
  }
}
int record(const std::string &path, JournalHeader header,
           std::vector<RecorderLane> &lanes, bool drain, int control = -1) {
  recorder_stop = drain ? 2 : 0;
  struct sigaction action{};
  action.sa_handler = recorder_signal;
  sigemptyset(&action.sa_mask);
  if (::sigaction(SIGUSR1, &action, nullptr) != 0 ||
      ::sigaction(SIGTERM, &action, nullptr) != 0)
    return 70;
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGUSR1);
  sigaddset(&signals, SIGTERM);
  if (::sigprocmask(SIG_UNBLOCK, &signals, nullptr) != 0)
    return 70;
  auto &failure_lane = lanes.front();
  const auto cursor = failure_lane.channel.cursors(RawPartition::failure);
  if (!cursor.has_value() || cursor.value().published % failure_size != 0)
    return 70;
  RawProducer failure_producer{
      failure_lane.channel,
      RawProducer::Start{failure_lane.producer_endpoint.value,
                         cursor.value().published / failure_size + 1}};
  bool held = false;
  FailureSource source;
  source.journal = header.journal.bytes();
  RawAuditProgress progress{};
  std::uint64_t stage = 2;
  const auto admit_failure = [&] {
    take(failure_producer.reserve(RawPartition::failure, failure_size));
    held = true;
  };
  const auto release_failure = [&] {
    failure_producer.release_reservation(RawPartition::failure);
    held = false;
  };
  try {
    admit_failure();
    auto directory = take(NativeJournalDirectory::open(path));
    std::uint64_t segment = 0;
    auto audit = open_audit(directory, path, header, lanes, segment);
    source.journal = audit->header().journal.bytes();
    // Recovery may reclaim an already synced batch without another append.
    // Publish that preserved cursor even when every source is already drained.
    publish_cursor(path, segment, audit->cursor());
    std::vector<pollfd> watches;
    watches.reserve(max_lanes + 1);
    const auto rebuild_watches = [&] {
      watches.clear();
      if (control >= 0)
        watches.push_back({control, POLLIN, 0});
      for (const auto &lane : lanes)
        watches.push_back({lane.recorder_endpoint.value, POLLIN, 0});
    };
    rebuild_watches();
    release_failure();
    const auto lifecycle = [&]() {
      if (control < 0)
        return false;
      source = {audit->header().journal.bytes(), {}, RawPartition::data, 0, 0, false};
      stage = 5;
      progress = {};
      admit_failure();
      pollfd watch{control, POLLIN, 0};
      int ready;
      do {
        ready = ::poll(&watch, 1, 0);
      } while (ready < 0 && errno == EINTR);
      if (ready < 0)
        throw Error{ErrorCode::io, errno};
      if (ready == 0) {
        release_failure();
        return false;
      }
      if ((watch.revents & POLLIN) == 0)
        throw Error{ErrorCode::io, EPIPE};
      auto message = receive_control(control);
      if (message.count == static_cast<ssize_t>(registration_size)) {
        stage = 6;
        const auto registration = decode_registration(message.body);
        source.lane = registration.epoch;
        if (registration.closed || lanes.size() == max_lanes ||
            lanes.capacity() < max_lanes || message.endpoints[0].value < 0 ||
            message.endpoints[1].value < 0 ||
            std::any_of(
                lanes.begin(), lanes.end(),
                [&](const auto &lane) {
                  return lane.registration.epoch == registration.epoch;
                }))
          throw Error{ErrorCode::conflict};
        auto channel = take(RawChannel::open(registration.name.data(),
                                             registration.epoch, registration.sizes));
        // The failure producer borrows front.channel: capacity was reserved in
        // the supervisor before fork, and ordinary lifecycle never moves front.
        lanes.push_back({registration, std::move(channel),
                         std::move(message.endpoints[0]),
                         std::move(message.endpoints[1])});
      } else if (message.count == 17 && message.body[0] == std::byte{2}) {
        stage = 7;
        IdentityBytes epoch{};
        std::memcpy(epoch.data(), message.body.data() + 1, epoch.size());
        source.lane = epoch;
        const auto found =
            std::find_if(lanes.begin(), lanes.end(), [&](const auto &lane) {
              return lane.registration.epoch == epoch;
            });
        if (found == lanes.end() || found == lanes.begin())
          throw Error{ErrorCode::invalid_identity};
        for (auto partition :
             {RawPartition::data, RawPartition::control, RawPartition::failure}) {
          const auto current = take(found->channel.cursors(partition));
          if (current.published != current.acknowledged ||
              current.acknowledged != audit->preserved(epoch, partition))
            throw Error{ErrorCode::busy};
        }
        if (segment == UINT64_MAX)
          throw Error{ErrorCode::overflow};
        const auto next_name = "events." + std::to_string(segment + 1);
        const auto existing = directory.open_existing(next_name, FileAccess::read_only);
        if (existing.has_value())
          throw Error{ErrorCode::conflict};
        if (existing.error() != Error{ErrorCode::io, ENOENT})
          throw existing.error();
        auto closure = found->registration;
        closure.closed = true;
        register_lane(path, closure);
        write_atomic(path, "sealed." + std::to_string(segment), {}, true);
        take(directory.synchronize_directory(SyncStrength::data));
        std::vector<RawOrigin> inherited;
        inherited.reserve(lanes.size() * 3);
        // Include the retiring lane until the successful reply authorizes the
        // supervisor to unlink it. A lost reply can restart with that lane.
        for (const auto &lane : lanes)
          for (auto partition :
               {RawPartition::data, RawPartition::control, RawPartition::failure}) {
            const auto end = audit->preserved(lane.registration.epoch, partition);
            if (end != 0)
              inherited.push_back({lane.registration.epoch, partition, end});
          }
        const auto prior = audit->cursor();
        IdentityBytes next{};
        if (::getentropy(next.data(), next.size()) != 0)
          throw Error{ErrorCode::io, errno};
        const JournalHeader successor{
            header.environment, take(AuditStreamId::from_bytes(next)),
            header.issuer_namespace, header.limits,
            JournalPredecessor{prior.journal, prior.sequence, prior.offset}};
        write_seed(path, segment + 1, inherited);
        take(directory.synchronize_directory(SyncStrength::data));
        auto next_audit =
            take(RawAudit::open(directory, next_name, successor, (max_lanes + 1) * 3,
                                SyncStrength::data, inherited));
        ++segment;
        audit = std::move(next_audit);
        source.journal = audit->header().journal.bytes();
        publish_cursor(path, segment, audit->cursor());
        lanes.erase(found);
      } else {
        throw Error{ErrorCode::corrupt};
      }
      rebuild_watches();
      stage = 8;
      const std::byte accepted{1};
      ssize_t replied;
      do {
        replied = ::send(control, &accepted, sizeof(accepted), 0);
      } while (replied < 0 && errno == EINTR);
      if (replied != static_cast<ssize_t>(sizeof(accepted)))
        throw Error{ErrorCode::io, replied < 0 ? errno : 0};
      release_failure();
      return true;
    };
    std::size_t first_lane = 0;
    for (;;) {
      bool pending = false;
      bool changed = false;
      for (std::size_t visited = 0; visited < lanes.size(); ++visited) {
        {
          auto &lane = lanes[(first_lane + visited) % lanes.size()];
          // A finite snapshot per context bounds each tour. Failure and control
          // are visited before data, with at most one batch per partition.
          for (auto partition :
               {RawPartition::failure, RawPartition::control, RawPartition::data}) {
            const auto current = take(lane.channel.cursors(partition));
            if (current.published == current.acknowledged)
              continue;
            pending = true;
            source = {audit->header().journal.bytes(),
                      lane.registration.epoch,
                      partition,
                      current.acknowledged,
                      current.published,
                      true};
            stage = 3;
            progress = {};
            admit_failure();
            auto reading =
                lane.channel.read(partition, current.acknowledged, 256 * 1024);
            if (!reading.has_value() && reading.error().code == ErrorCode::capacity)
              reading = lane.channel.read(partition, current.acknowledged,
                                          header.limits.max_payload - raw_origin_size);
            auto read = take(std::move(reading));
            std::array<RawAuditRecord, 2048> records{};
            std::size_t record_count = 0;
            std::size_t offset = 0;
            while (offset < read.bytes.size() && record_count < records.size()) {
              std::uint32_t length = 0;
              std::memcpy(&length, read.bytes.data() + offset, sizeof(length));
              if (length < raw_envelope_size || length > read.bytes.size() - offset)
                throw Error{ErrorCode::corrupt};
              records[record_count++] = {
                  {lane.registration.epoch, partition, read.start + offset},
                  ByteView{read.bytes}.subspan(offset, length)};
              offset += length;
            }
            source.end = read.start + offset;
            stage = 1;
            auto appended = audit->append(std::span{records}.first(record_count));
            if (!appended.has_value()) {
              failure(failure_producer, appended.error(), audit->progress(), stage,
                      source);
              held = false;
              return 70;
            }
            stage = 4;
            progress = audit->progress();
            publish_cursor(path, segment, audit->cursor());
            take(lane.acknowledge(partition, source.end));
            release_failure();
          }
        }
        if (recorder_stop == 1)
          return 0;
        // The current lane reference and borrowed read spans have ceased use.
        if (lifecycle()) {
          first_lane %= lanes.size();
          changed = true;
          break;
        }
      }
      if (changed)
        continue;
      if (recorder_stop == 2 && !pending)
        return 0;
      first_lane = (first_lane + 1) % lanes.size();
      if (pending)
        continue;
      for (auto &lane : lanes) {
        take(raw_drain_notices(lane.recorder_endpoint.value));
        take(lane.channel.arm_data());
      }
      bool ready = false;
      for (auto &lane : lanes)
        for (auto partition :
             {RawPartition::data, RawPartition::control, RawPartition::failure}) {
          const auto current = take(lane.channel.cursors(partition));
          ready = ready || current.published != current.acknowledged;
        }
      if (ready)
        continue;
      if (recorder_stop != 0)
        continue;
      if (::poll(watches.data(), static_cast<nfds_t>(watches.size()), -1) < 0 &&
          errno != EINTR)
        throw Error{ErrorCode::io, errno};
    }
  } catch (const Error &error) {
    // The startup/read/reclaim failure uses the credit held before its attempt.
    if (!held) {
      const auto reserved =
          failure_producer.reserve(RawPartition::failure, failure_size);
      held = reserved.has_value();
    }
    if (held) {
      try {
        failure(failure_producer, error, progress, stage, source);
      } catch (const Error &) {
      }
    }
    return 70;
  } catch (const std::bad_alloc &) {
    if (!held) {
      const auto reserved =
          failure_producer.reserve(RawPartition::failure, failure_size);
      held = reserved.has_value();
    }
    if (held) {
      try {
        failure(failure_producer, {ErrorCode::allocation}, progress, stage, source);
      } catch (const Error &) {
      }
    }
    return 70;
  }
}
struct SupervisorEndpoints {
  int control, life, notification, death, notification_read;
};
pid_t start_recorder(const std::string &path, JournalHeader header,
                     std::vector<RecorderLane> &lanes, SupervisorEndpoints endpoints,
                     Descriptor &recorder_control, bool drain = false) {
  recorder_control = Descriptor{};
  const auto pair = control_pair();
  Descriptor parent_control{pair[0]}, child_control{pair[1]};
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGUSR1);
  sigaddset(&signals, SIGTERM);
  if (::sigprocmask(SIG_BLOCK, &signals, nullptr) != 0)
    throw Error{ErrorCode::io, errno};
  const auto child = ::fork();
  if (child < 0)
    throw Error{ErrorCode::io, errno};
  if (child == 0) {
    ::close(parent_control.value);
    ::close(endpoints.control);
    ::close(endpoints.life);
    ::close(endpoints.notification);
    ::close(endpoints.death);
    ::close(endpoints.notification_read);
    child_notice = -1;
    ::signal(SIGCHLD, SIG_DFL);
    ::_exit(record(path, header, lanes, drain, child_control.value));
  }
  recorder_control = std::move(parent_control);
  return child;
}
void stop_recorder(pid_t child, int signal) {
  if (child <= 0)
    return;
  if (::kill(child, signal) != 0 && errno != ESRCH)
    throw Error{ErrorCode::io, errno};
  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno != EINTR && errno != ECHILD)
      throw Error{ErrorCode::io, errno};
    if (errno == ECHILD)
      break;
  }
}
struct RecorderStatus {
  pid_t recorder;
  bool fenced;
};
void reply(int descriptor, RecorderStatus status, Error error = {ErrorCode::io, 0},
           bool ok = true) {
  std::array<std::int64_t, 5> body{status.recorder, status.fenced ? 1 : 0, ok ? 0 : 1,
                                   static_cast<std::int64_t>(error.code), error.detail};
  ssize_t count;
  do {
    count = ::send(descriptor, body.data(), sizeof(body), 0);
  } while (count < 0 && errno == EINTR);
  if (count != static_cast<ssize_t>(sizeof(body)))
    throw Error{ErrorCode::io, errno};
}
struct SupervisorDescriptors {
  Descriptor control, life, death, catalogue;
};
int supervise(const std::string &path, JournalHeader header,
              SupervisorDescriptors descriptors, RecorderLane failure_lane) {
  try {
    auto &control = descriptors.control;
    auto &life = descriptors.life;
    auto &death = descriptors.death;
    auto &catalogue = descriptors.catalogue;
    // Keep the exclusive catalogue lease until this supervisor exits.
    (void)catalogue;
    std::vector<RecorderLane> lanes;
    lanes.reserve(max_lanes);
    lanes.push_back(std::move(failure_lane));
    const auto notification = make_pipe();
    Descriptor notice_read{notification[0]}, notice_write{notification[1]};
    for (const auto fd : notification) {
      const int flags = ::fcntl(fd, F_GETFL);
      if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw Error{ErrorCode::io, errno};
    }
    child_notice = notice_write.value;
    struct sigaction action{};
    action.sa_handler = child_changed;
    sigemptyset(&action.sa_mask);
    if (::sigaction(SIGCHLD, &action, nullptr) != 0)
      throw Error{ErrorCode::io, errno};
    const SupervisorEndpoints endpoints{control.value, life.value, notice_write.value,
                                        death.value, notice_read.value};
    Descriptor recorder_control;
    pid_t recorder = start_recorder(path, header, lanes, endpoints, recorder_control);
    bool fenced = false;
    bool recovery_used = false;
    const auto recorder_died = [&](int status) {
      recorder = 0;
      if (WIFEXITED(status) && WEXITSTATUS(status) == 70 && recovery_used) {
        fenced = true;
        for (auto &lane : lanes)
          (void)raw_send_notice(lane.recorder_endpoint.value);
      } else {
        recovery_used = WIFEXITED(status) && WEXITSTATUS(status) == 70;
        recorder = start_recorder(path, header, lanes, endpoints, recorder_control);
      }
    };
    const auto recorder_request = [&](const Registration *registration,
                                      const std::array<int, 2> &transferred,
                                      const IdentityBytes &retiring) {
      if (recorder <= 0 || fenced)
        throw Error{ErrorCode::audit_unavailable};
      if (registration) {
        send_registration(recorder_control.value, *registration, transferred);
      } else {
        std::array<std::byte, 17> request{};
        request[0] = std::byte{2};
        std::memcpy(request.data() + 1, retiring.data(), retiring.size());
        ssize_t sent;
        do {
          sent = ::send(recorder_control.value, request.data(), request.size(), 0);
        } while (sent < 0 && errno == EINTR);
        if (sent != static_cast<ssize_t>(request.size()))
          throw Error{ErrorCode::io, sent < 0 ? errno : 0};
      }
      std::array<pollfd, 2> waiting{
          {{recorder_control.value, POLLIN, 0}, {notice_read.value, POLLIN, 0}}};
      for (;;) {
        if (::poll(waiting.data(), static_cast<nfds_t>(waiting.size()), -1) < 0) {
          if (errno == EINTR)
            continue;
          throw Error{ErrorCode::io, errno};
        }
        if (waiting[1].revents != 0) {
          std::array<char, 64> discard{};
          while (::read(notice_read.value, discard.data(), discard.size()) > 0) {
          }
          int status = 0;
          pid_t dead;
          do {
            dead = ::waitpid(recorder, &status, WNOHANG);
          } while (dead < 0 && errno == EINTR);
          if (dead == recorder) {
            recorder_died(status);
            throw Error{ErrorCode::audit_unavailable, status};
          }
          if (dead < 0)
            throw Error{ErrorCode::io, errno};
        }
        if ((waiting[0].revents & POLLNVAL) != 0)
          throw Error{ErrorCode::io, EBADF};
        if ((waiting[0].revents & POLLIN) != 0) {
          std::array<std::byte, 2> accepted{};
          ssize_t received;
          do {
            received =
                ::recv(recorder_control.value, accepted.data(), accepted.size(), 0);
          } while (received < 0 && errno == EINTR);
          if (received == 0 ||
              (received < 0 && (errno == EPIPE || errno == ECONNRESET))) {
            // Peer closure can precede SIGCHLD. Wait for the independent child
            // notice once; EOF is not a malformed reply or a new poll loop.
            waiting[0].fd = -1;
            continue;
          }
          if (received < 0)
            throw Error{ErrorCode::io, errno};
          if (received != 1 || accepted[0] != std::byte{1})
            throw Error{ErrorCode::corrupt};
          return;
        }
        if ((waiting[0].revents & (POLLHUP | POLLERR)) != 0)
          waiting[0].fd = -1;
      }
    };
    std::array<pollfd, 3> watches{{{control.value, POLLIN, 0},
                                   {life.value, POLLIN | POLLHUP, 0},
                                   {notice_read.value, POLLIN, 0}}};
    for (;;) {
      if (::poll(watches.data(), watches.size(), -1) < 0) {
        if (errno == EINTR)
          continue;
        throw Error{ErrorCode::io, errno};
      }
      if (watches[2].revents != 0) {
        std::array<char, 64> discard{};
        while (::read(notice_read.value, discard.data(), discard.size()) > 0) {
        }
        int status = 0;
        const auto dead = ::waitpid(recorder, &status, WNOHANG);
        if (dead == recorder) {
          recorder_died(status);
        }
      }
      if (watches[1].revents != 0) {
        if (recorder > 0)
          stop_recorder(recorder, SIGTERM);
        else if (!fenced)
          stop_recorder(
              start_recorder(path, header, lanes, endpoints, recorder_control, true),
              SIGTERM);
        bool complete = true;
        for (auto &lane : lanes)
          for (auto partition :
               {RawPartition::data, RawPartition::control, RawPartition::failure}) {
            const auto current = take(lane.channel.cursors(partition));
            complete = complete && current.published == current.acknowledged;
          }
        if (complete) {
          write_atomic(path, "closed", {}, true);
          auto directory = take(NativeJournalDirectory::open(path));
          take(directory.synchronize_directory(SyncStrength::data));
          for (auto &lane : lanes)
            take(RawChannel::unlink(lane.registration.name.data()));
        }
        return complete ? 0 : 70;
      }
      if (watches[0].revents != 0) {
        auto message = receive_control(control.value);
        try {
          if (message.count == 1) {
            if (message.body[0] == std::byte{1}) {
              if (recorder > 0)
                stop_recorder(recorder, SIGUSR1);
              recorder = 0;
              // Explicit recovery supplies a fresh failure lane. Old failures
              // remain registered and drain before their originals are reclaimed.
              if (lanes.size() == max_lanes)
                throw Error{ErrorCode::capacity};
              auto registration = new_registration(lanes.front().registration.sizes);
              auto channel = take(RawChannel::create(
                  registration.name.data(), registration.epoch, registration.sizes));
              const auto recovery_endpoints = take(raw_socket_pair());
              RecorderLane recovery_lane{registration, std::move(channel),
                                         Descriptor{recovery_endpoints[0]},
                                         Descriptor{recovery_endpoints[1]}};
              register_lane(path, registration);
              lanes.insert(lanes.begin(), std::move(recovery_lane));
              recovery_used = true;
              fenced = false;
              recorder =
                  start_recorder(path, header, lanes, endpoints, recorder_control);
            }
          } else if (message.count == 17 && message.body[0] == std::byte{2}) {
            IdentityBytes epoch{};
            std::memcpy(epoch.data(), message.body.data() + 1, 16);
            auto found =
                std::find_if(lanes.begin(), lanes.end(), [&](const auto &lane) {
                  return lane.registration.epoch == epoch;
                });
            if (found == lanes.end() || found == lanes.begin())
              throw Error{ErrorCode::invalid_identity};
            for (auto partition :
                 {RawPartition::data, RawPartition::control, RawPartition::failure}) {
              const auto current = take(found->channel.cursors(partition));
              if (current.published != current.acknowledged)
                throw Error{ErrorCode::busy};
            }
            recorder_request(nullptr, {}, epoch);
            take(RawChannel::unlink(found->registration.name.data()));
            lanes.erase(found);
          } else if (message.count == static_cast<ssize_t>(registration_size)) {
            if (fenced || lanes.size() == max_lanes)
              throw Error{ErrorCode::capacity};
            const auto registration = decode_registration(message.body);
            if (message.endpoints[0].value < 0 || message.endpoints[1].value < 0)
              throw Error{ErrorCode::corrupt};
            auto channel = take(RawChannel::open(
                registration.name.data(), registration.epoch, registration.sizes));
            register_lane(path, registration);
            lanes.push_back({registration, std::move(channel),
                             std::move(message.endpoints[0]),
                             std::move(message.endpoints[1])});
            const auto &added = lanes.back();
            recorder_request(
                &added.registration,
                {added.producer_endpoint.value, added.recorder_endpoint.value}, {});
          } else
            throw Error{ErrorCode::corrupt};
          reply(control.value, {recorder, fenced});
        } catch (const Error &error) {
          fenced = true;
          reply(control.value, {recorder, true}, error, false);
        }
      }
    }
  } catch (const Error &) {
    return 70;
  }
}
void recover_orphans(const std::filesystem::path &base, std::uint64_t failure_bytes) {
  if (!std::filesystem::exists(base))
    return;
  for (const auto &entry : std::filesystem::directory_iterator{base}) {
    if (!std::filesystem::is_directory(entry.symlink_status()))
      continue;
    const auto path = entry.path().string();
    if (std::filesystem::exists(entry.path() / "closed")) {
      (void)read_file(path + "/closed", 0);
      finish_closed_unlinks(entry.path());
      continue;
    }
    const bool active = std::filesystem::exists(entry.path() / "active");
    if (!active && std::filesystem::exists(entry.path() / "bootstrap")) {
      const auto marker = read_file(path + "/bootstrap", bootstrap_marker.size());
      if (!std::equal(marker.begin(), marker.end(), bootstrap_marker.begin(),
                      bootstrap_marker.end()))
        throw Error{ErrorCode::corrupt};
      std::set<std::string> initial_names;
      for (const auto &file : std::filesystem::directory_iterator{entry.path()}) {
        const auto name = file.path().filename().string();
        if (name.starts_with("events."))
          throw Error{ErrorCode::audit_unavailable};
        if (name.find(".pending.") != std::string::npos)
          continue;
        if (name.starts_with("lane."))
          initial_names.insert(name.substr(5));
        else if (name.starts_with("registered."))
          initial_names.insert(name.substr(11));
      }
      for (const auto &name : initial_names) {
        if (name != entry.path().filename().string())
          throw Error{ErrorCode::audit_unavailable};
        const auto registration = recover_registration(path, name, false);
        auto channel = take(RawChannel::open(registration.name.data(),
                                             registration.epoch, registration.sizes));
        for (auto partition :
             {RawPartition::data, RawPartition::control, RawPartition::failure})
          if (take(channel.cursors(partition)).published != 0)
            throw Error{ErrorCode::audit_unavailable};
      }
      // No recorder segment, later registration or published source can be
      // explained by incomplete setup. Do not treat lost activation as empty.
      continue;
    }
    Descriptor lease{
        ::open((path + "/lanes").c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW)};
    if (lease.value < 0) {
      if (errno == ENOENT && !active)
        continue;
      throw Error{ErrorCode::io, errno};
    }
    if (::flock(lease.value, LOCK_EX | LOCK_NB) != 0) {
      if (errno == EWOULDBLOCK)
        continue;
      throw Error{ErrorCode::io, errno};
    }
    if (!std::filesystem::exists(entry.path() / "owner") && !active)
      continue;
    const auto owner = read_file(path + "/owner", sizeof(pid_t));
    if (owner.size() != sizeof(pid_t))
      throw Error{ErrorCode::corrupt};
    pid_t original = 0;
    std::memcpy(&original, owner.data(), sizeof(original));
    if (original <= 0)
      throw Error{ErrorCode::corrupt};
    // A live or reused PID makes this epoch ambiguous; never adopt that process.
    if (::kill(original, 0) == 0 || errno == EPERM)
      continue;
    if (errno != ESRCH)
      throw Error{ErrorCode::io, errno};
    const auto primary_header = header_copy(path + "/lanes");
    const auto independent_header =
        active ? header_copy(path + "/active") : std::optional<JournalHeader>{};
    if (!primary_header && !independent_header)
      throw Error{ErrorCode::corrupt};
    if (primary_header && independent_header && *primary_header != *independent_header)
      throw Error{ErrorCode::conflict};
    const auto header = primary_header ? *primary_header : *independent_header;
    std::vector<Registration> registrations;
    registrations.reserve(max_lanes);
    bool initial_found = false;
    for (const auto &file : std::filesystem::directory_iterator{entry.path()}) {
      const auto name = file.path().filename().string();
      if (name.find(".pending.") != std::string::npos)
        continue;
      std::string lane_name;
      if (name.starts_with("lane."))
        lane_name = name.substr(5);
      else if (name.starts_with("registered.")) {
        lane_name = name.substr(11);
        if (std::filesystem::exists(entry.path() / ("lane." + lane_name)))
          continue;
      } else
        continue;
      const auto registration = recover_registration(path, lane_name, false);
      initial_found = initial_found || lane_name == entry.path().filename().string();
      if (std::filesystem::exists(entry.path() / ("closed." + lane_name)) ||
          std::filesystem::exists(entry.path() / ("retired." + lane_name))) {
        const auto closed = recover_registration(path, lane_name, true);
        if (!closed.closed || closed.epoch != registration.epoch ||
            closed.sizes != registration.sizes || closed.name != registration.name)
          throw Error{ErrorCode::conflict};
        continue;
      }
      if (registrations.size() == max_lanes)
        throw Error{ErrorCode::capacity};
      registrations.push_back(registration);
    }
    if (active && !initial_found)
      throw Error{ErrorCode::corrupt};
    const auto prepaid_failures =
        static_cast<std::uint64_t>(registrations.size()) * failure_size;
    const auto required_batch =
        (static_cast<std::uint64_t>(registrations.size()) + 1) *
            (failure_size + raw_origin_size + journal_frame_header_size) +
        56;
    if (header.limits.max_payload < failure_size + raw_origin_size ||
        header.limits.max_batch_bytes < required_batch)
      throw Error{ErrorCode::capacity};
    auto registration = new_registration(
        {16, 16, std::max(failure_bytes, prepaid_failures + failure_size)});
    auto channel = take(RawChannel::create(registration.name.data(), registration.epoch,
                                           registration.sizes));
    const auto endpoints = take(raw_socket_pair());
    register_lane(path, registration);
    std::vector<RecorderLane> lanes;
    lanes.reserve(registrations.size() + 1);
    lanes.push_back({registration, std::move(channel), Descriptor{endpoints[0]},
                     Descriptor{endpoints[1]}});
    RawProducer reporter{lanes.front().channel, lanes.front().producer_endpoint.value};
    take(reporter.reserve(RawPartition::failure, prepaid_failures));
    bool missing = false;
    for (const auto &source : registrations) {
      auto mapped = RawChannel::open(source.name.data(), source.epoch, source.sizes);
      if (!mapped.has_value()) {
        failure(
            reporter, mapped.error(), {}, 2,
            {header.journal.bytes(), source.epoch, RawPartition::data, 0, 0, false});
        missing = true;
        continue;
      }
      // The dead owner and acquired catalogue lease fence these publications.
      // A drain-only recorder needs no source wake endpoints.
      lanes.push_back(
          {source, std::move(mapped).value(), Descriptor{}, Descriptor{}, true});
    }
    reporter.release_reservation(RawPartition::failure);
    const auto child = ::fork();
    if (child < 0)
      throw Error{ErrorCode::io, errno};
    if (child == 0) {
      child_notice = -1;
      ::signal(SIGCHLD, SIG_DFL);
      ::_exit(record(path, header, lanes, true));
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
      if (errno != EINTR)
        throw Error{ErrorCode::io, errno};
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
      throw Error{ErrorCode::audit_unavailable};
    if (missing)
      throw Error{ErrorCode::audit_unavailable};
    write_atomic(path, "closed", {}, true);
    auto directory = take(NativeJournalDirectory::open(path));
    take(directory.synchronize_directory(SyncStrength::data));
    finish_closed_unlinks(entry.path());
  }
}
thread_local RawThreadContext *bound_context = nullptr;
thread_local RawThreadHandle *bound_handle = nullptr;
thread_local RawWaitControl bound_wait_control{};
} // namespace

RawWaitControl raw_wait_control() noexcept { return bound_wait_control; }
RawWaitScope::RawWaitScope(RawWaitControl control) : previous_(bound_wait_control) {
  if (control.cancellation_fds[0] == control.cancellation_fds[1])
    control.cancellation_fds[1] = -1;
  for (int endpoint : previous_.cancellation_fds) {
    if (endpoint < 0 ||
        std::find(control.cancellation_fds.begin(), control.cancellation_fds.end(),
                  endpoint) != control.cancellation_fds.end())
      continue;
    auto unused =
        std::find(control.cancellation_fds.begin(), control.cancellation_fds.end(), -1);
    if (unused == control.cancellation_fds.end())
      throw Error{ErrorCode::capacity};
    *unused = endpoint;
  }
  if (previous_.deadline &&
      (!control.deadline || *previous_.deadline < *control.deadline))
    control.deadline = previous_.deadline;
  bound_wait_control = control;
}
RawWaitScope::~RawWaitScope() { bound_wait_control = previous_; }

class RawRuntime::State {
public:
  State(Descriptor control, Descriptor life, Descriptor death, pid_t supervisor,
        RawCapacities sizes)
      : control_(std::move(control)), life_(std::move(life)), death_(std::move(death)),
        supervisor_(supervisor), sizes_(sizes) {
    const auto endpoints = make_pipe();
    gate_read_ = Descriptor{endpoints[0]};
    gate_write_ = Descriptor{endpoints[1]};
    for (int endpoint : endpoints)
      if (::fcntl(endpoint, F_SETFL, O_NONBLOCK) < 0)
        throw Error{ErrorCode::io, errno};
    const char token = 1;
    write_all(gate_write_.value, std::as_bytes(std::span{&token, std::size_t{1}}));
  }
  ~State() {
    life_ = Descriptor{};
    int status = 0;
    while (::waitpid(supervisor_, &status, 0) < 0 && errno == EINTR) {
    }
  }
  RawRuntimeStatus exchange(std::optional<Registration> registration,
                            std::array<int, 2> endpoints = {}, bool recover = false,
                            std::optional<IdentityBytes> closure = {},
                            RawWaitControl wait = raw_wait_control()) {
    acquire_gate(wait);
    struct Release {
      State &state;
      ~Release() { state.release_gate(); }
    } release{*this};
    // A cancelled request can still receive a reply. Drain it under the same
    // ownership before issuing another request, so replies cannot shift owners.
    if (pending_response_)
      (void)receive_response(wait);
    for (;;) {
      check_wait_control(wait);
      try {
        if (closure) {
          std::array<std::byte, 17> message{};
          message[0] = std::byte{2};
          std::memcpy(message.data() + 1, closure->data(), 16);
          const auto count =
              ::send(control_.value, message.data(), message.size(), MSG_DONTWAIT);
          if (count != static_cast<ssize_t>(message.size()))
            throw Error{ErrorCode::io, count < 0 ? errno : 0};
        } else if (registration)
          send_registration(control_.value, *registration, endpoints, MSG_DONTWAIT);
        else {
          const std::byte command = recover ? std::byte{1} : std::byte{0};
          const auto count = ::send(control_.value, &command, 1, MSG_DONTWAIT);
          if (count != 1)
            throw Error{ErrorCode::io, count < 0 ? errno : 0};
        }
        pending_response_ = true;
        break;
      } catch (const Error &error) {
        if (error.code != ErrorCode::io ||
            (error.detail != EAGAIN && error.detail != EWOULDBLOCK &&
             error.detail != EINTR))
          throw;
        std::array<pollfd, 2> writable{
            {{control_.value, POLLOUT, 0}, {death_.value, POLLIN | POLLHUP, 0}}};
        wait_ready(writable, wait);
        if (writable[1].revents != 0)
          throw Error{ErrorCode::audit_unavailable};
      }
    }
    const auto response = receive_response(wait);
    if (response[2] != 0)
      throw Error{static_cast<ErrorCode>(response[3]), response[4]};
    return {supervisor_, static_cast<pid_t>(response[0]), response[1] != 0};
  }
  void acquire_gate(const RawWaitControl &wait) {
    for (;;) {
      check_wait_control(wait);
      if (const auto error = gate_error_.load())
        throw Error{ErrorCode::io, error};
      char token = 0;
      const auto count = ::read(gate_read_.value, &token, 1);
      if (count == 1) {
        if (gate_owned_.exchange(true, std::memory_order_acquire))
          throw Error{ErrorCode::corrupt};
        if (token != 1) {
          release_gate();
          throw Error{ErrorCode::corrupt};
        }
        return;
      }
      if (count < 0 && errno == EINTR)
        continue;
      if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
        throw Error{ErrorCode::io, count < 0 ? errno : EPIPE};
      std::array<pollfd, 2> watches{{{gate_read_.value, POLLIN | POLLHUP, 0},
                                     {death_.value, POLLIN | POLLHUP, 0}}};
      wait_ready(watches, wait);
      if (watches[1].revents != 0)
        throw Error{ErrorCode::audit_unavailable};
    }
  }
  void release_gate() noexcept {
    const RestoreErrno restore;
    const char token = 1;
    gate_owned_.store(false, std::memory_order_release);
    ssize_t count;
    do {
      count = ::write(gate_write_.value, &token, 1);
    } while (count < 0 && errno == EINTR);
    if (count != 1) {
      gate_error_.store(count < 0 ? errno : EIO);
      // No token can be handed on. Closing the sole write endpoint wakes every
      // pending gate waiter; each returns the explicit gate error.
      gate_write_ = Descriptor{};
    }
  }
  std::array<std::int64_t, 5> receive_response(const RawWaitControl &wait) {
    std::array<std::int64_t, 5> response{};
    // Setup RPC watches supervisor death independently of borrowed lane FDs.
    std::array<pollfd, 2> response_watch{
        {{control_.value, POLLIN, 0}, {death_.value, POLLIN | POLLHUP, 0}}};
    for (;;) {
      wait_ready(response_watch, wait);
      if (response_watch[1].revents != 0)
        throw Error{ErrorCode::audit_unavailable};
      if (response_watch[0].revents != 0) {
        iovec part{response.data(), sizeof(response)};
        msghdr reply{};
        reply.msg_iov = &part;
        reply.msg_iovlen = 1;
        const auto count = ::recvmsg(control_.value, &reply, MSG_DONTWAIT);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
          continue;
        if (count < 0)
          throw Error{ErrorCode::io, errno};
        pending_response_ = false;
        if (count != static_cast<ssize_t>(sizeof(response)) ||
            (reply.msg_flags & MSG_TRUNC) != 0)
          throw Error{ErrorCode::corrupt};
        break;
      }
    }
    if (response[0] < 0 || response[0] > std::numeric_limits<pid_t>::max() ||
        (response[1] != 0 && response[1] != 1) ||
        (response[2] != 0 && response[2] != 1) || response[3] < 0 ||
        response[3] > static_cast<std::int64_t>(ErrorCode::provider_rate_limit))
      throw Error{ErrorCode::corrupt};
    known_fenced_.store(response[1] != 0, std::memory_order_release);
    return response;
  }
  Descriptor gate_read_, gate_write_;
  std::atomic<int> gate_error_{0};
  std::atomic<bool> gate_owned_{false};
  std::atomic<bool> known_fenced_{false};
  bool pending_response_ = false;
  Descriptor control_, life_, death_;
  pid_t supervisor_;
  RawCapacities sizes_;
};
class RawThreadContext {
public:
  RawThreadContext(RawProducer &producer, IdentityBytes epoch)
      : producer_(&producer), epoch_(epoch) {
    take(producer_->reserve(RawPartition::failure, ingest_failure_size));
  }
  RawThreadContext(std::shared_ptr<RawRuntime::State> state, RawChannel channel,
                   Descriptor endpoint)
      : state_(std::move(state)),
        channel_(std::make_unique<RawChannel>(std::move(channel))),
        endpoint_(std::move(endpoint)),
        owned_producer_(std::make_unique<RawProducer>(*channel_, endpoint_.value)),
        producer_(owned_producer_.get()), epoch_(channel_->epoch()) {
    take(producer_->reserve(RawPartition::failure, ingest_failure_size));
  }
  ~RawThreadContext() {
    if (!fenced_)
      producer_->release_reservation(RawPartition::failure);
    if (!state_)
      return;
    try {
      const auto control = raw_wait_control();
      for (;;) {
        check_wait_control(control);
        take(raw_drain_notices(endpoint_.value));
        take(channel_->arm_space());
        bool complete = true;
        for (auto partition :
             {RawPartition::data, RawPartition::control, RawPartition::failure}) {
          const auto current = take(channel_->cursors(partition));
          complete = complete && current.published == current.acknowledged;
        }
        if (complete) {
          (void)state_->exchange(std::nullopt, {}, false, epoch_, control);
          return;
        }
        const auto status = state_->exchange(std::nullopt, {}, false, {}, control);
        if (status.fenced)
          return;
        std::array<pollfd, 2> watches{{{endpoint_.value, POLLIN, 0},
                                       {state_->death_.value, POLLIN | POLLHUP, 0}}};
        wait_ready(watches, control);
        if (watches[1].revents != 0)
          return;
      }
    } catch (const Error &) {
    } catch (const std::bad_alloc &) {
    }
  }
  std::shared_ptr<RawRuntime::State> state_;
  std::unique_ptr<RawChannel> channel_;
  Descriptor endpoint_;
  std::unique_ptr<RawProducer> owned_producer_;
  RawProducer *producer_;
  IdentityBytes epoch_;
  bool fenced_ = false;
};
Result<RawSignalPublisher> RawSignalPublisher::prepare(const RawThreadHandle &context,
                                                       RawControlToken &claim) {
  if (!context || context.get() != bound_context)
    return Result<RawSignalPublisher>::failure({ErrorCode::conflict});
  auto control = context->producer_->prepare_native(RawPartition::control, &claim);
  if (!control.has_value())
    return Result<RawSignalPublisher>::failure(control.error());
  auto failure = context->producer_->prepare_native(RawPartition::failure);
  if (!failure.has_value())
    return Result<RawSignalPublisher>::failure(failure.error());
  RawSignalPublisher publisher;
  publisher.control_ = control.value();
  publisher.failure_ = failure.value();
  publisher.fenced_ = &context->fenced_;
  return Result<RawSignalPublisher>::success(publisher);
}
RawNativePublication RawSignalPublisher::emit(std::uint32_t site,
                                              const RawNativePart *parts,
                                              std::size_t count) noexcept {
  if (fenced_ == nullptr)
    return {0, {}, {}, true, false};
  const auto result = control_.emit(site, parts, count);
  if ((!result.published || result.notice_failed) && !*fenced_) {
    *fenced_ = true;
    const Error error = result.published ? result.notice_error : result.error;
    const RawNativePart failure_parts[]{{&error.code, sizeof(error.code)},
                                        {&error.detail, sizeof(error.detail)}};
    (void)failure_.emit(8001, failure_parts, 2);
  }
  return result;
}
RawRuntime::RawRuntime(std::shared_ptr<State> state) : state_(std::move(state)) {}
RawRuntime::~RawRuntime() = default;
Result<std::unique_ptr<RawRuntime>> RawRuntime::open(std::string_view directory,
                                                     EnvironmentId environment,
                                                     RawRuntimeOptions options) {
  try {
    const auto largest =
        std::max({options.producer_capacities[0], options.producer_capacities[1],
                  options.producer_capacities[2], options.recorder_failure_bytes});
    constexpr std::uint64_t frame_allowance =
        2048 * (raw_origin_size + journal_frame_header_size) + 56;
    if (largest > UINT32_MAX - frame_allowance ||
        options.recorder_failure_bytes < failure_size)
      throw Error{ErrorCode::capacity};
    recover_orphans(std::filesystem::path{directory} / "raw",
                    options.recorder_failure_bytes);
    const auto registration =
        new_registration({16, 16, options.recorder_failure_bytes});
    const auto run_name = std::string{registration.name.data() + 1};
    const auto path = (std::filesystem::path{directory} / "raw" / run_name).string();
    std::filesystem::create_directories(path);
    if (::chmod(path.c_str(), 0700) != 0)
      throw Error{ErrorCode::io, errno};
    write_atomic(path, "bootstrap", bootstrap_marker, true);
    Descriptor catalogue{
        ::open((path + "/lanes").c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
    if (catalogue.value < 0 || ::flock(catalogue.value, LOCK_EX | LOCK_NB) != 0)
      throw Error{ErrorCode::io, errno};
    IdentityBytes stream{};
    std::uint64_t issuer = 0;
    if (::getentropy(stream.data(), stream.size()) != 0 ||
        ::getentropy(&issuer, sizeof(issuer)) != 0 || issuer == 0)
      throw Error{ErrorCode::io, errno};
    const JournalHeader header{environment,
                               take(AuditStreamId::from_bytes(stream)),
                               issuer,
                               {static_cast<std::uint32_t>(largest + raw_origin_size),
                                static_cast<std::uint32_t>(largest + frame_allowance)},
                               std::nullopt};
    write_all(catalogue.value, take(encode_journal_header(header)));
    const auto owner = ::getpid();
    write_atomic(path, "owner", bytes(owner), true);
    write_descriptions(path);
    auto failure_channel = take(RawChannel::create(
        registration.name.data(), registration.epoch, registration.sizes));
    const auto failure_endpoints = take(raw_socket_pair());
    RecorderLane failure_lane{registration, std::move(failure_channel),
                              Descriptor{failure_endpoints[0]},
                              Descriptor{failure_endpoints[1]}};
    register_lane(path, registration);
    sync_file(catalogue.value);
    auto native_directory = take(NativeJournalDirectory::open(path));
    take(native_directory.synchronize_directory(SyncStrength::data));
    write_atomic(path, "active", take(encode_journal_header(header)), true);
    take(native_directory.synchronize_directory(SyncStrength::data));
    const auto control = control_pair();
    Descriptor parent_control{control[0]}, child_control{control[1]};
    const auto life = make_pipe();
    Descriptor life_read{life[0]}, life_write{life[1]};
    const auto death = make_pipe();
    Descriptor death_read{death[0]}, death_write{death[1]};
    const auto supervisor = ::fork();
    if (supervisor < 0)
      throw Error{ErrorCode::io, errno};
    if (supervisor == 0) {
      parent_control = Descriptor{};
      life_write = Descriptor{};
      death_read = Descriptor{};
      ::_exit(supervise(path, header,
                        {std::move(child_control), std::move(life_read),
                         std::move(death_write), std::move(catalogue)},
                        std::move(failure_lane)));
    }
    child_control = Descriptor{};
    life_read = Descriptor{};
    catalogue = Descriptor{};
    death_write = Descriptor{};
    auto state = std::make_shared<State>(std::move(parent_control),
                                         std::move(life_write), std::move(death_read),
                                         supervisor, options.producer_capacities);
    return Result<std::unique_ptr<RawRuntime>>::success(
        std::unique_ptr<RawRuntime>{new RawRuntime{std::move(state)}});
  } catch (const Error &error) {
    return Result<std::unique_ptr<RawRuntime>>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<std::unique_ptr<RawRuntime>>::failure({ErrorCode::allocation});
  } catch (const std::filesystem::filesystem_error &error) {
    return Result<std::unique_ptr<RawRuntime>>::failure(
        {ErrorCode::io, error.code().value()});
  }
}
Result<RawThreadHandle> RawRuntime::prepare_thread(RawWaitControl control) {
  try {
    check_wait_control(control);
    if (state_->known_fenced_.load(std::memory_order_acquire))
      throw Error{ErrorCode::audit_unavailable};
    auto registration = new_registration(state_->sizes_);
    auto channel = take(RawChannel::create(registration.name.data(), registration.epoch,
                                           registration.sizes));
    const auto endpoints = take(raw_socket_pair());
    Descriptor producer{endpoints[0]}, recorder{endpoints[1]};
    (void)state_->exchange(registration, endpoints, false, {}, control);
    return Result<RawThreadHandle>::success(std::make_shared<RawThreadContext>(
        state_, std::move(channel), std::move(producer)));
  } catch (const Error &error) {
    return Result<RawThreadHandle>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<RawThreadHandle>::failure({ErrorCode::allocation});
  }
}
Result<void> RawRuntime::recover(RawWaitControl control) {
  try {
    (void)state_->exchange(std::nullopt, {}, true, {}, control);
    return Result<void>::success();
  } catch (const Error &error) {
    return Result<void>::failure(error);
  }
}
Result<RawRuntimeStatus> RawRuntime::status(RawWaitControl control) {
  try {
    return Result<RawRuntimeStatus>::success(
        state_->exchange(std::nullopt, {}, false, {}, control));
  } catch (const Error &error) {
    return Result<RawRuntimeStatus>::failure(error);
  }
}
Result<RawThreadHandle> raw_prepare_thread() {
  if (bound_context == nullptr)
    return Result<RawThreadHandle>::success({});
  if (!bound_context->state_)
    return Result<RawThreadHandle>::failure({ErrorCode::unsupported});
  RawRuntime runtime{bound_context->state_};
  return runtime.prepare_thread();
}
RawThreadBinding::RawThreadBinding(RawThreadHandle context)
    : context_(std::move(context)), previous_(bound_context),
      previous_handle_(bound_handle) {
  bound_context = context_.get();
  bound_handle = &context_;
}
RawThreadBinding::RawThreadBinding(RawProducer &producer, IdentityBytes epoch)
    : RawThreadBinding(std::make_shared<RawThreadContext>(producer, epoch)) {}
RawThreadBinding::~RawThreadBinding() {
  bound_context = previous_;
  bound_handle = previous_handle_;
}
RawThreadHandle raw_current_thread() noexcept {
  return bound_handle == nullptr ? RawThreadHandle{} : *bound_handle;
}
bool raw_enabled() noexcept { return bound_context != nullptr; }
IdentityBytes raw_lane_epoch() noexcept {
  return bound_context == nullptr ? IdentityBytes{} : bound_context->epoch_;
}
namespace {
Result<RawCapacityClaim> claim_capacity(std::uint64_t amount, RawPartition partition,
                                        RawWaitControl control) {
  if (bound_context == nullptr || bound_context->fenced_)
    return Result<RawCapacityClaim>::failure({ErrorCode::audit_unavailable});
  try {
    for (;;) {
      check_wait_control(control);
      if (bound_context->state_ &&
          bound_context->state_->known_fenced_.load(std::memory_order_acquire))
        return Result<RawCapacityClaim>::failure({ErrorCode::audit_unavailable});
      const auto maximum = bound_context->producer_->maximum_grant(partition);
      if (!maximum.has_value())
        return Result<RawCapacityClaim>::failure(maximum.error());
      if (amount > maximum.value())
        return Result<RawCapacityClaim>::failure({ErrorCode::capacity});
      auto held = bound_context->producer_->hold_capacity(partition, amount);
      if (held.has_value() || held.error().code != ErrorCode::capacity ||
          !bound_context->state_)
        return held;
      const auto drained = raw_drain_notices(bound_context->endpoint_.value);
      if (!drained.has_value())
        return Result<RawCapacityClaim>::failure(drained.error());
      const auto armed = bound_context->channel_->arm_space();
      if (!armed.has_value())
        return Result<RawCapacityClaim>::failure(armed.error());
      held = bound_context->producer_->hold_capacity(partition, amount);
      if (held.has_value() || held.error().code != ErrorCode::capacity)
        return held;
      try {
        const auto status =
            bound_context->state_->exchange(std::nullopt, {}, false, {}, control);
        if (status.fenced)
          return Result<RawCapacityClaim>::failure({ErrorCode::audit_unavailable});
      } catch (const Error &error) {
        return Result<RawCapacityClaim>::failure(error);
      }
      std::array<pollfd, 2> watches{
          {{bound_context->endpoint_.value, POLLIN, 0},
           {bound_context->state_->death_.value, POLLIN | POLLHUP, 0}}};
      wait_ready(watches, control);
      if (watches[1].revents != 0)
        return Result<RawCapacityClaim>::failure({ErrorCode::audit_unavailable});
    }
  } catch (const Error &error) {
    return Result<RawCapacityClaim>::failure(error);
  }
}
} // namespace
Result<RawControlToken> raw_claim_control(std::uint64_t amount,
                                          RawWaitControl control) {
  return claim_capacity(amount, RawPartition::control, control);
}
Result<RawControlToken> raw_try_claim_control(std::uint64_t amount) noexcept {
  if (bound_context == nullptr || bound_context->fenced_ ||
      (bound_context->state_ &&
       bound_context->state_->known_fenced_.load(std::memory_order_acquire)))
    return Result<RawControlToken>::failure({ErrorCode::audit_unavailable});
  return bound_context->producer_->hold_capacity(RawPartition::control, amount);
}
Result<RawCapacityClaim> raw_claim_data(std::uint64_t amount, RawWaitControl control) {
  return claim_capacity(amount, RawPartition::data, control);
}
Result<void> raw_wait_data(std::uint64_t amount, RawWaitControl control) {
  if (bound_context == nullptr)
    return Result<void>::success();
  auto &context = *bound_context;
  if (context.fenced_)
    return Result<void>::failure({ErrorCode::audit_unavailable});
  const auto admission = context.producer_->admission_ready(RawPartition::data);
  if (!admission.has_value())
    return admission;
  try {
    for (;;) {
      check_wait_control(control);
      if (context.state_ &&
          context.state_->known_fenced_.load(std::memory_order_acquire))
        return Result<void>::failure({ErrorCode::audit_unavailable});
      const auto maximum = context.producer_->maximum_grant(RawPartition::data);
      if (!maximum.has_value())
        return Result<void>::failure(maximum.error());
      if (amount > maximum.value())
        return Result<void>::failure({ErrorCode::capacity});
      const auto free = context.producer_->available(RawPartition::data);
      if (!free.has_value())
        return Result<void>::failure(free.error());
      if (free.value() >= amount)
        return Result<void>::success();
      if (!context.state_)
        return Result<void>::failure({ErrorCode::capacity});
      const auto drained = raw_drain_notices(context.endpoint_.value);
      if (!drained.has_value())
        return drained;
      const auto armed = context.channel_->arm_space();
      if (!armed.has_value())
        return armed;
      const auto rechecked = context.producer_->available(RawPartition::data);
      if (!rechecked.has_value())
        return Result<void>::failure(rechecked.error());
      if (rechecked.value() >= amount)
        return Result<void>::success();
      try {
        const auto status =
            context.state_->exchange(std::nullopt, {}, false, {}, control);
        if (status.fenced)
          return Result<void>::failure({ErrorCode::audit_unavailable});
      } catch (const Error &error) {
        return Result<void>::failure(error);
      }
      std::array<pollfd, 2> watches{
          {{context.endpoint_.value, POLLIN, 0},
           {context.state_->death_.value, POLLIN | POLLHUP, 0}}};
      wait_ready(watches, control);
      if (watches[1].revents != 0)
        return Result<void>::failure({ErrorCode::audit_unavailable});
    }
  } catch (const Error &error) {
    return Result<void>::failure(error);
  }
}
void raw_fence(Error error) noexcept {
  const RestoreErrno restore;
  if (bound_context == nullptr || bound_context->fenced_)
    return;
  bound_context->fenced_ = true;
  auto &producer = *bound_context->producer_;
  const std::array parts{bytes(error.code), bytes(error.detail)};
  const auto emitted = producer.emit(8001, parts, RawPartition::failure);
  (void)emitted;
}
Result<std::uint64_t> raw_emit(std::uint32_t site, std::span<const ByteView> parts,
                               RawPartition partition,
                               RawControlToken *claim) noexcept {
  const RestoreErrno restore;
  if (bound_context == nullptr)
    return Result<std::uint64_t>::success(0);
  auto &context = *bound_context;
  if (context.fenced_ && claim == nullptr)
    return Result<std::uint64_t>::failure({ErrorCode::audit_unavailable});
  std::uint64_t amount = raw_envelope_size;
  for (const auto part : parts) {
    if (part.size() > UINT32_MAX - amount) {
      raw_fence({ErrorCode::overflow});
      return Result<std::uint64_t>::failure({ErrorCode::overflow});
    }
    amount += part.size();
  }
  if (claim == nullptr) {
    const auto reserved = context.producer_->reserve(partition, amount);
    if (!reserved.has_value()) {
      raw_fence(reserved.error());
      return Result<std::uint64_t>::failure(reserved.error());
    }
  }
  auto emitted = context.producer_->emit(site, parts, partition, claim);
  if (!emitted.has_value())
    raw_fence(emitted.error());
  if (const auto notice = context.producer_->notification_error())
    raw_fence(*notice);
  return emitted;
}

} // namespace blackbird
