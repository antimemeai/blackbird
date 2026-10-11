#include "blackbird/raw_runtime.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/raw_audit.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <poll.h>
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
  if (lane.name[0] != '/' || lane.name.back() != 0)
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
  if (std::filesystem::exists(std::filesystem::path{path} / name))
    throw Error{ErrorCode::conflict};
  write_atomic(path, name, encode_registration(registration), true);
  auto directory = take(NativeJournalDirectory::open(path));
  take(directory.synchronize_directory(SyncStrength::data));
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
  const std::array<std::uint32_t, 14> abi{1,
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
                                          sizeof(RawPartition)};
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
  for (unsigned code = 0; code <= static_cast<unsigned>(ErrorCode::provider_rate_limit);
       ++code) {
    const auto value = static_cast<ErrorCode>(code);
    write_all(file.value, bytes(value));
    write_description(file.value, error_name(value));
  }
  sync_file(file.value);
}
std::array<int, 2> make_pipe() {
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
void send_registration(int descriptor, const Registration &lane,
                       std::array<int, 2> endpoints) {
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
    count = ::sendmsg(descriptor, &message, 0);
  } while (count < 0 && errno == EINTR);
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
          take(lane.channel.acknowledge(partition, end, lane.recorder_endpoint.value));
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
           std::vector<RecorderLane> &lanes, bool drain) {
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
    release_failure();
    std::vector<pollfd> watches;
    watches.reserve(lanes.size());
    for (const auto &lane : lanes)
      watches.push_back({lane.recorder_endpoint.value, POLLIN, 0});
    std::size_t first_lane = 0;
    for (;;) {
      bool pending = false;
      for (std::size_t visited = 0; visited < lanes.size(); ++visited) {
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
          auto reading = lane.channel.read(partition, current.acknowledged, 256 * 1024);
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
          take(lane.channel.acknowledge(partition, source.end,
                                        lane.recorder_endpoint.value));
          release_failure();
        }
        if (recorder_stop == 1)
          return 0;
      }
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
  int control, life, notification, death;
};
pid_t start_recorder(const std::string &path, JournalHeader header,
                     std::vector<RecorderLane> &lanes, SupervisorEndpoints endpoints,
                     bool drain = false) {
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
    ::close(endpoints.control);
    ::close(endpoints.life);
    ::close(endpoints.notification);
    ::close(endpoints.death);
    child_notice = -1;
    ::signal(SIGCHLD, SIG_DFL);
    ::_exit(record(path, header, lanes, drain));
  }
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
  std::array<std::int64_t, 4> body{status.recorder, status.fenced ? 1 : 0, ok ? 0 : 1,
                                   static_cast<std::int64_t>(error.code)};
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
                                        death.value};
    pid_t recorder = start_recorder(path, header, lanes, endpoints);
    bool fenced = false;
    bool recovery_used = false;
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
          recorder = 0;
          if (WIFEXITED(status) && WEXITSTATUS(status) == 70 && recovery_used) {
            fenced = true;
            for (auto &lane : lanes)
              (void)raw_send_notice(lane.recorder_endpoint.value);
          } else {
            recovery_used = WIFEXITED(status) && WEXITSTATUS(status) == 70;
            recorder = start_recorder(path, header, lanes, endpoints);
          }
        }
      }
      if (watches[1].revents != 0) {
        if (recorder > 0)
          stop_recorder(recorder, SIGTERM);
        else if (!fenced)
          stop_recorder(start_recorder(path, header, lanes, endpoints, true), SIGTERM);
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
              recorder = start_recorder(path, header, lanes, endpoints);
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
            if (recorder > 0)
              stop_recorder(recorder, SIGUSR1);
            recorder = 0;
            auto closure = found->registration;
            closure.closed = true;
            register_lane(path, closure);
            std::uint64_t segment = 0;
            while (std::filesystem::exists(std::filesystem::path{path} /
                                           ("events." + std::to_string(segment + 1))))
              ++segment;
            write_atomic(path, "sealed." + std::to_string(segment), {}, true);
            auto directory = take(NativeJournalDirectory::open(path));
            take(directory.synchronize_directory(SyncStrength::data));
            take(RawChannel::unlink(closure.name.data()));
            lanes.erase(found);
            recorder = start_recorder(path, header, lanes, endpoints);
          } else if (message.count == static_cast<ssize_t>(registration_size)) {
            if (fenced || lanes.size() == max_lanes)
              throw Error{ErrorCode::capacity};
            const auto registration = decode_registration(message.body);
            if (message.endpoints[0].value < 0 || message.endpoints[1].value < 0)
              throw Error{ErrorCode::corrupt};
            auto channel = take(RawChannel::open(
                registration.name.data(), registration.epoch, registration.sizes));
            if (recorder > 0)
              stop_recorder(recorder, SIGUSR1);
            recorder = 0;
            register_lane(path, registration);
            lanes.push_back({registration, std::move(channel),
                             std::move(message.endpoints[0]),
                             std::move(message.endpoints[1])});
            recorder = start_recorder(path, header, lanes, endpoints);
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
    if (!entry.is_directory())
      continue;
    const auto path = entry.path().string();
    if (std::filesystem::exists(entry.path() / "closed"))
      continue;
    Descriptor lease{
        ::open((path + "/lanes").c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW)};
    if (lease.value < 0) {
      if (errno == ENOENT)
        continue;
      throw Error{ErrorCode::io, errno};
    }
    if (::flock(lease.value, LOCK_EX | LOCK_NB) != 0) {
      if (errno == EWOULDBLOCK)
        continue;
      throw Error{ErrorCode::io, errno};
    }
    if (!std::filesystem::exists(entry.path() / "owner"))
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
    const auto encoded = read_file(path + "/lanes", journal_header_size);
    const auto header = take(decode_journal_header(encoded));
    std::vector<RecorderLane> lanes;
    lanes.reserve(max_lanes + 1);
    std::vector<Registration> missing;
    for (const auto &file : std::filesystem::directory_iterator{entry.path()}) {
      const auto name = file.path().filename().string();
      if (!name.starts_with("lane.") || name.find(".pending.") != std::string::npos)
        continue;
      const auto registration =
          decode_registration(read_file(file.path().string(), registration_size));
      const auto closure =
          entry.path() / ("closed." + std::string{registration.name.data() + 1});
      if (std::filesystem::exists(closure)) {
        auto closed =
            decode_registration(read_file(closure.string(), registration_size));
        if (!closed.closed || closed.epoch != registration.epoch ||
            closed.sizes != registration.sizes || closed.name != registration.name)
          throw Error{ErrorCode::conflict};
        continue;
      }
      auto mapped = RawChannel::open(registration.name.data(), registration.epoch,
                                     registration.sizes);
      if (!mapped.has_value()) {
        if (mapped.error().code == ErrorCode::io && mapped.error().detail == ENOENT) {
          missing.push_back(registration);
          continue;
        }
        throw mapped.error();
      }
      const auto endpoints = take(raw_socket_pair());
      lanes.push_back({registration, std::move(mapped).value(),
                       Descriptor{endpoints[0]}, Descriptor{endpoints[1]}});
    }
    auto registration = new_registration({16, 16, failure_bytes});
    auto channel = take(RawChannel::create(registration.name.data(), registration.epoch,
                                           registration.sizes));
    const auto endpoints = take(raw_socket_pair());
    register_lane(path, registration);
    lanes.insert(lanes.begin(), {registration, std::move(channel),
                                 Descriptor{endpoints[0]}, Descriptor{endpoints[1]}});
    RawProducer reporter{lanes.front().channel, lanes.front().producer_endpoint.value};
    for (const auto &absent : missing) {
      take(reporter.reserve(RawPartition::failure, failure_size));
      failure(reporter, {ErrorCode::io, ENOENT}, {}, 2,
              {header.journal.bytes(), absent.epoch, RawPartition::data, 0, 0, false});
    }
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
    write_atomic(path, "closed", {}, true);
    auto directory = take(NativeJournalDirectory::open(path));
    take(directory.synchronize_directory(SyncStrength::data));
    for (const auto &lane : lanes)
      take(RawChannel::unlink(lane.registration.name.data()));
  }
}
thread_local RawThreadContext *bound_context = nullptr;
} // namespace

class RawRuntime::State {
public:
  State(Descriptor control, Descriptor life, Descriptor death, pid_t supervisor,
        RawCapacities sizes)
      : control_(std::move(control)), life_(std::move(life)), death_(std::move(death)),
        supervisor_(supervisor), sizes_(sizes) {}
  ~State() {
    life_ = Descriptor{};
    int status = 0;
    while (::waitpid(supervisor_, &status, 0) < 0 && errno == EINTR) {
    }
  }
  RawRuntimeStatus exchange(std::optional<Registration> registration,
                            std::array<int, 2> endpoints = {}, bool recover = false,
                            std::optional<IdentityBytes> closure = {}) {
    std::lock_guard lock{mutex_};
    pollfd watch{control_.value, POLLOUT, 0};
    if (::poll(&watch, 1, 0) < 0)
      throw Error{ErrorCode::io, errno};
    if (closure) {
      std::array<std::byte, 17> message{};
      message[0] = std::byte{2};
      std::memcpy(message.data() + 1, closure->data(), 16);
      if (::send(control_.value, message.data(), message.size(), 0) !=
          static_cast<ssize_t>(message.size()))
        throw Error{ErrorCode::io, errno};
    } else if (registration)
      send_registration(control_.value, *registration, endpoints);
    else {
      const std::byte command = recover ? std::byte{1} : std::byte{0};
      if (::send(control_.value, &command, 1, 0) != 1)
        throw Error{ErrorCode::io, errno};
    }
    std::array<std::int64_t, 4> response{};
    // Setup RPC watches supervisor death independently of borrowed lane FDs.
    std::array<pollfd, 2> response_watch{
        {{control_.value, POLLIN, 0}, {death_.value, POLLIN | POLLHUP, 0}}};
    for (;;) {
      const auto result = ::poll(response_watch.data(), response_watch.size(), -1);
      if (result < 0) {
        if (errno == EINTR)
          continue;
        throw Error{ErrorCode::io, errno};
      }
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
        if (count != static_cast<ssize_t>(sizeof(response)))
          throw Error{ErrorCode::corrupt};
        break;
      }
    }
    if (response[2] != 0)
      throw Error{static_cast<ErrorCode>(response[3])};
    return {supervisor_, static_cast<pid_t>(response[0]), response[1] != 0};
  }
  std::mutex mutex_;
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
      for (;;) {
        take(raw_drain_notices(endpoint_.value));
        take(channel_->arm_space());
        bool complete = true;
        for (auto partition :
             {RawPartition::data, RawPartition::control, RawPartition::failure}) {
          const auto current = take(channel_->cursors(partition));
          complete = complete && current.published == current.acknowledged;
        }
        if (complete) {
          (void)state_->exchange(std::nullopt, {}, false, epoch_);
          return;
        }
        const auto status = state_->exchange(std::nullopt);
        if (status.fenced)
          return;
        std::array<pollfd, 2> watches{{{endpoint_.value, POLLIN, 0},
                                       {state_->death_.value, POLLIN | POLLHUP, 0}}};
        if (::poll(watches.data(), watches.size(), -1) < 0) {
          if (errno == EINTR)
            continue;
          return;
        }
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
Result<RawThreadHandle> RawRuntime::prepare_thread() {
  try {
    auto registration = new_registration(state_->sizes_);
    auto channel = take(RawChannel::create(registration.name.data(), registration.epoch,
                                           registration.sizes));
    const auto endpoints = take(raw_socket_pair());
    Descriptor producer{endpoints[0]}, recorder{endpoints[1]};
    (void)state_->exchange(registration, endpoints);
    return Result<RawThreadHandle>::success(std::make_shared<RawThreadContext>(
        state_, std::move(channel), std::move(producer)));
  } catch (const Error &error) {
    return Result<RawThreadHandle>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<RawThreadHandle>::failure({ErrorCode::allocation});
  }
}
Result<void> RawRuntime::recover() {
  try {
    (void)state_->exchange(std::nullopt, {}, true);
    return Result<void>::success();
  } catch (const Error &error) {
    return Result<void>::failure(error);
  }
}
Result<RawRuntimeStatus> RawRuntime::status() {
  try {
    return Result<RawRuntimeStatus>::success(state_->exchange(std::nullopt));
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
    : context_(std::move(context)), previous_(bound_context) {
  bound_context = context_.get();
}
RawThreadBinding::RawThreadBinding(RawProducer &producer, IdentityBytes epoch)
    : RawThreadBinding(std::make_shared<RawThreadContext>(producer, epoch)) {}
RawThreadBinding::~RawThreadBinding() { bound_context = previous_; }
bool raw_enabled() noexcept { return bound_context != nullptr; }
IdentityBytes raw_lane_epoch() noexcept {
  return bound_context == nullptr ? IdentityBytes{} : bound_context->epoch_;
}
Result<RawControlToken> raw_claim_control(std::uint64_t amount) {
  if (bound_context == nullptr || bound_context->fenced_)
    return Result<RawControlToken>::failure({ErrorCode::audit_unavailable});
  for (;;) {
    auto held = bound_context->producer_->hold_control(amount);
    if (held.has_value() || held.error().code != ErrorCode::capacity ||
        !bound_context->state_)
      return held;
    if (amount > bound_context->channel_->capacities()[1])
      return held;
    const auto drained = raw_drain_notices(bound_context->endpoint_.value);
    if (!drained.has_value())
      return Result<RawControlToken>::failure(drained.error());
    const auto armed = bound_context->channel_->arm_space();
    if (!armed.has_value())
      return Result<RawControlToken>::failure(armed.error());
    held = bound_context->producer_->hold_control(amount);
    if (held.has_value() || held.error().code != ErrorCode::capacity)
      return held;
    try {
      const auto status = bound_context->state_->exchange(std::nullopt);
      if (status.fenced)
        return Result<RawControlToken>::failure({ErrorCode::audit_unavailable});
    } catch (const Error &error) {
      return Result<RawControlToken>::failure(error);
    }
    std::array<pollfd, 2> watches{
        {{bound_context->endpoint_.value, POLLIN, 0},
         {bound_context->state_->death_.value, POLLIN | POLLHUP, 0}}};
    if (::poll(watches.data(), watches.size(), -1) < 0) {
      if (errno == EINTR)
        continue;
      return Result<RawControlToken>::failure({ErrorCode::io, errno});
    }
    if (watches[1].revents != 0)
      return Result<RawControlToken>::failure({ErrorCode::audit_unavailable});
  }
}
Result<void> raw_wait_data(std::uint64_t amount) {
  if (bound_context == nullptr)
    return Result<void>::success();
  auto &context = *bound_context;
  if (context.fenced_)
    return Result<void>::failure({ErrorCode::audit_unavailable});
  const auto admission = context.producer_->admission_ready(RawPartition::data);
  if (!admission.has_value())
    return admission;
  for (;;) {
    const auto free = context.producer_->available(RawPartition::data);
    if (!free.has_value())
      return Result<void>::failure(free.error());
    if (free.value() >= amount)
      return Result<void>::success();
    if (!context.state_ || amount > context.channel_->capacities()[0])
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
      const auto status = context.state_->exchange(std::nullopt);
      if (status.fenced)
        return Result<void>::failure({ErrorCode::audit_unavailable});
    } catch (const Error &error) {
      return Result<void>::failure(error);
    }
    std::array<pollfd, 2> watches{
        {{context.endpoint_.value, POLLIN, 0},
         {context.state_->death_.value, POLLIN | POLLHUP, 0}}};
    if (::poll(watches.data(), watches.size(), -1) < 0) {
      if (errno == EINTR)
        continue;
      return Result<void>::failure({ErrorCode::io, errno});
    }
    if (watches[1].revents != 0)
      return Result<void>::failure({ErrorCode::audit_unavailable});
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
