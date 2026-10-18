#include "blackbird/coding.hpp"
#include "blackbird/raw_reader.hpp"
namespace {
unsigned provider_calls = 0;
class StationFixtureProvider final : public blackbird::CodingProvider {
public:
  explicit StationFixtureProvider(const blackbird::OpenAiConfig & = {}) {}
  blackbird::Value respond(const blackbird::Value &,
                           const std::function<void(std::string_view)> &) override {
    ++provider_calls;
    throw blackbird::Error{blackbird::ErrorCode::provider_transport};
  }
};
} // namespace
#define main station_embedded_main
#define OpenAiCodingProvider StationFixtureProvider
#include "../src/main.cpp"
#undef OpenAiCodingProvider
#undef main
#include <cstdarg>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
namespace {
using NativeOpen = int (*)(const char *, int, ...);
using NativeRead = ssize_t (*)(int, void *, std::size_t);
using NativeClose = int (*)(int);
NativeOpen actual_open = nullptr;
NativeRead actual_read = nullptr;
NativeClose actual_close = nullptr;
std::string selected_workflow;
int selected_fd = -1;
bool selected = false, cut_hit = false;
unsigned workflow_opens = 0, enabled_opens = 0, observed_reads = 0;
std::array<std::optional<blackbird::RawCapacityClaim>, 187> holds;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error{message};
}
void sync_data() {
  constexpr char digits[] = "0123456789abcdef";
  const auto lane = blackbird::raw_lane_epoch();
  std::string name = "/bb";
  for (std::size_t index = 0; index < 12; ++index) {
    const auto byte = std::to_integer<unsigned char>(lane[index]);
    name += digits[byte >> 4U];
    name += digits[byte & 15U];
  }
  auto channel = blackbird::unwrap(blackbird::RawChannel::open(
      name, lane, {256 * 1024 * 1024, 1024 * 1024, 128 * 1024}));
  for (;;) {
    const auto cursor =
        blackbird::unwrap(channel.cursors(blackbird::RawPartition::data));
    if (cursor.published == cursor.acknowledged)
      return;
    std::this_thread::yield();
  }
}
void hold_data() {
  // For256MiB the exact greedy maximum is64 +63 +15+15+15+15 =187.
  std::size_t used = 0;
  for (const std::uint64_t amount :
       {4ULL * 1024 * 1024, 65536ULL, 4096ULL, 256ULL, 16ULL, 1ULL})
    for (;;) {
      auto grant = blackbird::raw_try_claim_data(amount);
      if (!grant.has_value()) {
        require(grant.error() == blackbird::Error{blackbird::ErrorCode::capacity},
                "actual native DATA floor");
        break;
      }
      require(used < holds.size(), "derived finite grant slots");
      holds[used++].emplace(std::move(grant).value());
    }
}
template <class T> T take(blackbird::ByteView &body) {
  require(body.size() >= sizeof(T), "complete native Station cut field");
  T value{};
  std::memcpy(&value, body.data(), sizeof(T));
  body = body.subspan(sizeof(T));
  return value;
}
struct Row {
  blackbird::RawEventRef key;
  std::uint32_t site;
  std::vector<std::byte> bytes;
};
std::vector<Row> preserved(const std::string &session) {
  using namespace blackbird;
  std::vector<Row> rows;
  const auto runs = unwrap(discover_raw_runs(session));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "actual refused Main recorder closed");
  auto reader = unwrap(RawRunReader::open(session + "/raw/" + runs.front().name));
  std::vector<std::byte> buffer(4 * 1024 * 1024);
  while (auto row = unwrap(reader->read(buffer))) {
    auto body = row->raw;
    (void)take<std::uint32_t>(body);
    const auto site = take<std::uint32_t>(body);
    const auto ordinal = take<std::uint64_t>(body);
    require(site != 8000 && site != 8001,
            "admission refusal is not transport/acquisition corruption");
    rows.push_back({{row->origin.lane, ordinal}, site, {body.begin(), body.end()}});
  }
  return rows;
}
blackbird::RawEventRef reference(blackbird::ByteView &body) {
  const auto lane = take<blackbird::IdentityBytes>(body);
  return {lane, take<std::uint64_t>(body)};
}
bool same(blackbird::RawEventRef a, blackbird::RawEventRef b) {
  return a.lane == b.lane && a.ordinal == b.ordinal;
}
void native_refusal() {
  using namespace blackbird;
  char temporary[] = "/tmp/blackbird-station-main-cut-XXXXXX";
  require(::mkdtemp(temporary), "private native Station Main cut");
  const std::string directory{temporary};
  const auto session = directory + "/session", adapter = directory + "/adapter";
  const auto effect = directory + "/must-not-run";
  selected_workflow = directory + "/workflow.lua";
  std::filesystem::create_directory(adapter);
  const std::string workflow = "blackbird.call('write_file',{path='" + effect +
                               "',content='unreached-native-effect'}); "
                               "blackbird.call('restart',{note='missed cut'})";
  write_file(selected_workflow, workflow);
  write_file(adapter + "/events.json",
             "[{\"source\":\"native-refusal-source\",\"id\":\"native-refusal-id\","
             "\"cursor\":\"actual-cursor\",\"prompt\":\"actual-prompt\"}]");
  std::array<std::string, 7> args{"blackbird",       "--session", session, "--workflow",
                                  selected_workflow, "--station", adapter};
  std::array<char *, 7> argv{};
  for (std::size_t index = 0; index < argv.size(); ++index)
    argv[index] = args[index].data();
  selected = true;
  const auto exit = station_embedded_main(static_cast<int>(argv.size()), argv.data());
  selected = false;
  std::cerr << "native cut diagnostic: opens=" << workflow_opens
            << " enabled=" << enabled_opens << " reads=" << observed_reads
            << " hit=" << cut_hit << "\n";
  require(exit == 1 && cut_hit && provider_calls == 0 &&
              !std::filesystem::exists(effect),
          "actual acquired-open source bound refuses before read/execution/provider");
  for (const auto &hold : holds)
    require(!hold, "actual native close releases the DATA loan before Main catch");
  const auto rows = preserved(session);
  RawEventRef admission{}, station{}, workflow_read{};
  unsigned finishes = 0, pauses = 0, publications = 0;
  bool acquired_prompt = false, acquired_open = false, capacity_terminal = false;
  for (const auto &row : rows) {
    ByteView body = row.bytes;
    if (row.site == 4620)
      station = row.key;
    if (row.site == 4600) {
      (void)reference(body);
      const auto method = take<std::uint8_t>(body);
      if (method == 3)
        admission = row.key;
      finishes += method == 4;
      pauses += method == 5;
    }
    publications += row.site == 4632;
    require(row.site != 4637 && row.site != 4638,
            "refused acquired workflow never becomes execution request/return");
    if (row.site == 4607) {
      (void)reference(body);
      const auto count = take<std::size_t>(body);
      require(count == body.size(), "actual prior prompt original extent");
      const std::string prompt{reinterpret_cast<const char *>(body.data()),
                               body.size()};
      acquired_prompt = prompt.find("native-refusal-source") != std::string::npos &&
                        prompt.find("actual-prompt") != std::string::npos;
    }
    if (row.site == 3420 && row.bytes.size() >= selected_workflow.size() + 1 &&
        std::memcmp(row.bytes.data() + row.bytes.size() - selected_workflow.size() - 1,
                    selected_workflow.c_str(), selected_workflow.size() + 1) == 0) {
      for (const auto &returned : rows)
        if (returned.site == 3421) {
          ByteView actual = returned.bytes;
          if (same(reference(actual), row.key))
            acquired_open =
                take<int>(actual) >= 0 && !take<bool>(actual) && actual.empty();
        }
      workflow_read = row.key;
    }
    if (row.site == 4621 && same(reference(body), station)) {
      require(take<std::uint8_t>(body) ==
                      static_cast<std::uint8_t>(RawOperationBranch::error) &&
                  take<std::uint8_t>(body) == 12 && take<bool>(body) &&
                  take<ErrorCode>(body) == ErrorCode::capacity &&
                  take<std::int64_t>(body) == 0 && body.empty(),
              "actual whole Station native capacity terminal at workflow acquisition");
      capacity_terminal = true;
    }
  }
  require(admission.ordinal && acquired_prompt && acquired_open &&
              workflow_read.ordinal && observed_reads == 0 && finishes == 0 &&
              pauses == 0 && publications == 2 && capacity_terminal,
          "actual original survives refusal; neither business catch performs "
          "finish/pause/new projection");
  const auto status = unwrap(decode_packet_string(
      read_file(session + "/station-status.bbm", 16 * 1024 * 1024)));
  require(
      string_field(status, "phase") == "busy",
      "previous actual busy projection survives raw refusal without blocked fallback");
  std::filesystem::remove_all(directory);
}
} // namespace
extern "C" int open(const char *path, int flags, ...) {
  mode_t mode{};
  bool has_mode = (flags & O_CREAT) != 0;
#if defined(__linux__)
  has_mode = has_mode || (flags & O_TMPFILE) == O_TMPFILE;
#endif
  if (has_mode) {
    va_list args;
    va_start(args, flags);
    mode = static_cast<mode_t>(va_arg(args, int));
    va_end(args);
  }
  const int fd = has_mode ? actual_open(path, flags, mode) : actual_open(path, flags);
  if (selected && selected_workflow == path) {
    ++workflow_opens;
    enabled_opens += blackbird::raw_enabled();
    if (fd >= 0 && blackbird::raw_enabled() && workflow_opens == 2) {
      selected_fd = fd;
      sync_data();
      hold_data();
      cut_hit = true;
    }
  }
  return fd;
}
extern "C" ssize_t read(int fd, void *buffer, std::size_t size) {
  if (fd == selected_fd)
    ++observed_reads;
  const auto count = actual_read(fd, buffer, size);
  const auto error = errno;
  errno = error;
  return count;
}
extern "C" int close(int fd) {
  const int result = actual_close(fd), error = errno;
  if (cut_hit && fd == selected_fd) {
    selected_fd = -1;
    for (auto &hold : holds)
      hold.reset();
  }
  errno = error;
  return result;
}
int main() {
  try {
    actual_open = reinterpret_cast<NativeOpen>(::dlsym(RTLD_NEXT, "open"));
    actual_read = reinterpret_cast<NativeRead>(::dlsym(RTLD_NEXT, "read"));
    actual_close = reinterpret_cast<NativeClose>(::dlsym(RTLD_NEXT, "close"));
    require(actual_open && actual_read && actual_close, "native libc forwarding seams");
    native_refusal();
    std::cout << "actual Station Main admission/source/catch oracle passed\n";
    return 0;
  } catch (const blackbird::Error &error) {
    std::cerr << blackbird::error_name(error.code) << ' ' << error.detail << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
