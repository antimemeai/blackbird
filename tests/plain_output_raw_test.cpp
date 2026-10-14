#include "blackbird/context.hpp"
#include "blackbird/raw_plain.hpp"
#include "blackbird/raw_reader.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <new>
#include <signal.h>
#include <unistd.h>
using namespace blackbird;
namespace {
void require(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
thread_local int selected_fd = -1;
thread_local bool short_write = false, allocation_cut = false;
struct Actual {
  std::size_t requested;
  ssize_t result;
  int error;
  std::string bytes;
};
thread_local std::vector<Actual> actual;
} // namespace
void *operator new(std::size_t n) {
  if (allocation_cut) {
    allocation_cut = false;
    throw std::bad_alloc{};
  }
  if (void *p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc{};
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }
extern "C" ssize_t write(int fd, const void *p, std::size_t n) {
  using Native = ssize_t (*)(int, const void *, std::size_t);
  static auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "write"));
  if (!native)
    std::abort();
  const auto result =
      native(fd, p, fd == selected_fd && short_write ? std::min<std::size_t>(2, n) : n);
  const int error = errno;
  if (fd == selected_fd)
    actual.push_back({n, result, error, {static_cast<const char *>(p), n}});
  errno = error;
  return result;
}
namespace {
template <class T> T take(ByteView &b) {
  require(b.size() >= sizeof(T), "complete native field");
  T v{};
  std::memcpy(&v, b.data(), sizeof(v));
  b = b.subspan(sizeof(v));
  return v;
}
using Key = std::pair<IdentityBytes, std::uint64_t>;
Key ref(ByteView &b) { return {take<IdentityBytes>(b), take<std::uint64_t>(b)}; }
struct Row {
  std::uint32_t site;
  std::vector<std::byte> body;
};
using Rows = std::map<Key, Row>;
Rows preserved(const std::string &dir, bool fenced = false) {
  const auto runs = unwrap(discover_raw_runs(dir));
  require(runs.size() == 1, "one raw run");
  auto reader = unwrap(RawRunReader::open(dir + "/raw/" + runs.front().name));
  std::vector<std::byte> buffer(4 * 1024 * 1024);
  Rows rows;
  while (auto event = unwrap(reader->read(buffer))) {
    auto b = event->raw;
    (void)take<std::uint32_t>(b);
    auto site = take<std::uint32_t>(b);
    auto ordinal = take<std::uint64_t>(b);
    require(site != 8000 && (site != 8001 || fenced), "no unexpected transport fence");
    rows.emplace(Key{event->origin.lane, ordinal}, Row{site, {b.begin(), b.end()}});
  }
  return rows;
}
std::string directory() {
  char p[] = "/tmp/blackbird-plain-XXXXXX";
  require(::mkdtemp(p), "actual private directory");
  return p;
}
auto runtime(const std::string &p) {
  IdentityBytes id{};
  id[0] = std::byte{101};
  return unwrap(
      RawRuntime::open(p, unwrap(EnvironmentId::from_bytes(id)),
                       {{64 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
}
RawAdapterContext parent() {
  char c = 'P';
  auto bank = unwrap(raw_claim_control(17));
  return {nullptr,
          unwrap(raw_publish(7999, std::array{detail::raw_native_field(c)},
                             RawPartition::control, bank)),
          nullptr, nullptr};
}
void run(unsigned kind) {
  const auto p = directory();
  int fds[2]{-1, -1};
  actual.clear();
  actual.reserve(128);
  short_write = kind == 0;
  const std::string input{"a\0bc\n", 5};
  const std::string expected = kind == 3 ? "[a\\x00bc\\x0a]\n" : input + "/tail";
  bool failed = false;
  Key parent_ref{};
  {
    auto owner = runtime(p);
    RawThreadBinding binding{unwrap(owner->prepare_thread())};
    auto context = parent();
    require(::pipe(fds) == 0, "real output pipe created after recorder fork");
    selected_fd = fds[1];
    if (!context.parent)
      throw std::runtime_error("fixture parent absent");
    parent_ref = {context.parent->lane, context.parent->ordinal};
    if (kind == 4)
      context.parent->ordinal = 0;
    if (kind == 1) {
      ::close(fds[0]);
      fds[0] = -1;
    }
    try {
      if (kind == 2 || kind == 3) {
        write_plain_formatted(
            fds[1], PlainOutputKind::status, input,
            [&](std::string_view text) {
              require(text == input, "formatter receives actual original");
              if (kind == 2)
                allocation_cut = true;
              return std::string{"a\\x00bc\\x0a"} +
                     (kind == 2 ? std::string(128, 'x') : std::string{});
            },
            &context, "[", "]\n");
      } else if (kind == 5) {
        auto data = unwrap(raw_claim_data(52));
        auto terminal = unwrap(raw_claim_control(52));
        write_plain_output(fds[1], PlainOutputKind::emit,
                           std::array<std::string_view, 2>{input, "/tail"}, &context,
                           &data, &terminal);
      } else if (kind == 6) {
        auto data = unwrap(raw_claim_data(100));
        auto terminal = unwrap(raw_claim_control(51));
        write_plain_output(fds[1], PlainOutputKind::emit,
                           std::array<std::string_view, 2>{input, "/tail"}, &context,
                           &data, &terminal);
      } else
        write_plain_output(fds[1], PlainOutputKind::emit,
                           std::array<std::string_view, 2>{input, "/tail"},
                           kind == 7 ? nullptr : &context);
    } catch (const std::bad_alloc &) {
      failed = kind == 2;
    } catch (const Error &e) {
      failed = kind == 1   ? e == Error{ErrorCode::io, EPIPE}
               : kind == 4 ? e.code == ErrorCode::invalid_identity
                           : (kind == 5 || kind == 6) && e.code == ErrorCode::capacity;
    }
  }
  allocation_cut = short_write = false;
  selected_fd = -1;
  ::close(fds[1]);
  std::string physical;
  if (fds[0] >= 0) {
    std::array<char, 128> b{};
    ssize_t n;
    while ((n = ::read(fds[0], b.data(), b.size())) > 0)
      physical.append(b.data(), static_cast<std::size_t>(n));
    ::close(fds[0]);
  }
  require(failed == (kind == 1 || kind == 2 || kind == 4 || kind == 5 || kind == 6),
          "actual expected failure class");
  require(physical ==
              ((kind == 0 || kind == 3 || kind == 7) ? expected : std::string{}),
          "physical bytes exactly match business output or zero pre-effect refusal");
  const auto rows = preserved(p, kind == 4);
  unsigned packet = 0, writes = 0, format_input = 0, allocation = 0, packet_ends = 0,
           format_ends = 0, format_requests = 0;
  std::size_t accepted = 0;
  Key packet_ref{}, format_ref{};
  std::map<Key, std::size_t> requests;
  for (const auto &[key, row] : rows) {
    ByteView b = row.body;
    if (row.site == 4345) {
      require(ref(b) == parent_ref, "formatter actual parent");
      require(take<int>(b) == fds[1] &&
                  take<PlainOutputKind>(b) == PlainOutputKind::status &&
                  take<std::size_t>(b) == input.size() && b.empty(),
              "literal formatter request fields");
      format_ref = key;
      ++format_requests;
    }
    if (row.site == 4340) {
      require(ref(b) == (kind == 3 ? format_ref : parent_ref),
              "proposal actual parent");
      require(take<int>(b) == fds[1], "actual output descriptor");
      require(take<PlainOutputKind>(b) ==
                  (kind == 3 ? PlainOutputKind::status : PlainOutputKind::emit),
              "actual proposal kind");
      const auto size = take<std::size_t>(b);
      require(size == b.size() && std::string(reinterpret_cast<const char *>(b.data()),
                                              b.size()) == expected,
              "literal complete proposed bytes");
      packet_ref = key;
      ++packet;
    }
    if (row.site == 4342) {
      require(ref(b) == packet_ref, "native write links actual proposal");
      require(take<int>(b) == fds[1], "native fd");
      const auto offset = take<std::size_t>(b), requested = take<std::size_t>(b);
      require(writes < actual.size() && offset == accepted &&
                  requested == actual[writes].requested &&
                  actual[writes].bytes == expected.substr(offset, requested) &&
                  b.empty(),
              "native actual piece pointers and global accepted offsets");
      requests.emplace(key, writes++);
    }
    if (row.site == 4343) {
      const auto request = ref(b);
      const auto &native = actual.at(requests.at(request));
      const auto count = take<ssize_t>(b);
      const bool failure = take<bool>(b);
      require(count == native.result && failure == (native.result < 0),
              "native return literal");
      if (failure)
        require(take<int>(b) == native.error, "native errno literal");
      else
        accepted += static_cast<std::size_t>(count);
      require(b.empty(), "write return complete");
    }
    if (row.site == 4341) {
      require(ref(b) == packet_ref, "packet terminal actual request");
      require(take<RawOperationBranch>(b) == (kind == 1 ? RawOperationBranch::error
                                                        : RawOperationBranch::returned),
              "actual packet terminal branch");
      require(take<std::uint8_t>(b) == 0 && take<bool>(b) == (kind == 1),
              "literal terminal stage and Error presence");
      if (kind == 1)
        require(take<ErrorCode>(b) == ErrorCode::io && take<std::int64_t>(b) == EPIPE,
                "actual EPIPE terminal Error");
      require(b.empty(), "complete optional packet terminal layout");
      ++packet_ends;
    }
    if (row.site == 4344) {
      require(ref(b) == format_ref, "original links actual formatter request");
      const auto n = take<std::size_t>(b);
      require(n == input.size() && std::string(reinterpret_cast<const char *>(b.data()),
                                               b.size()) == input,
              "original before formatter");
      ++format_input;
    }
    if (row.site == 4346) {
      require(ref(b) == format_ref, "formatter terminal actual request");
      const auto branch = take<RawOperationBranch>(b);
      require(branch == (kind == 2 ? RawOperationBranch::allocation
                                   : RawOperationBranch::returned),
              "actual formatter terminal branch");
      if (branch == RawOperationBranch::allocation)
        ++allocation;
      require(take<std::uint8_t>(b) == 0 && !take<bool>(b) && b.empty(),
              "complete formatter terminal layout");
      ++format_ends;
    }
    if (kind == 7)
      require(row.site < 4340 || row.site > 4379,
              "ambient binding alone does not select plain output");
  }
  require(packet == ((kind == 0 || kind == 1 || kind == 3) ? 1u : 0u),
          "actual proposal presence");
  require(packet_ends == packet && format_ends == format_requests &&
              format_requests == ((kind == 2 || kind == 3) ? 1u : 0u),
          "exactly one terminal per actual request, none before admission");
  require(writes == ((kind == 7) ? 0u : actual.size()),
          "all actual selected writes recorded");
  require(format_input == ((kind == 2 || kind == 3) ? 1u : 0u) &&
              allocation == (kind == 2 ? 1u : 0u),
          "formatter original and allocation terminal");
  std::filesystem::remove_all(p);
}
} // namespace
int main() {
  ::signal(SIGPIPE, SIG_IGN);
  try {
    for (unsigned kind = 0; kind < 8; ++kind) {
      std::cerr << "plain case " << kind << '\n';
      run(kind);
    }
    std::cout << "plain native source-to-preserved-byte cases passed\n";
    return 0;
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << '(' << e.detail << ")\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return 1;
}
