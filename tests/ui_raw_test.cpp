#include "blackbird/raw_reader.hpp"
#include "blackbird/sprite.hpp"
#include "blackbird/terminal.hpp"
#include "blackbird/workflows.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <new>
#include <poll.h>
#include <signal.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace blackbird;
namespace {
thread_local std::size_t fail_allocation_after = 0;
thread_local bool allocation_failed = false;
struct WriteObservation {
  std::size_t requested = 0;
  ssize_t result = 0;
  int error = 0;
  std::array<char, 8192> bytes{};
};
thread_local int observed_descriptor = -1;
thread_local unsigned write_fault_phase = 0;
thread_local std::size_t observed_writes = 0;
thread_local std::array<WriteObservation, 32> write_observations{};
} // namespace
void *operator new(std::size_t size) {
  if (fail_allocation_after && --fail_allocation_after == 0) {
    allocation_failed = true;
    throw std::bad_alloc{};
  }
  if (void *memory = std::malloc(size ? size : 1))
    return memory;
  throw std::bad_alloc{};
}
void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
  try {
    return ::operator new(size);
  } catch (const std::bad_alloc &) {
    return nullptr;
  }
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete(void *memory, const std::nothrow_t &) noexcept {
  ::operator delete(memory);
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void *operator new[](std::size_t size, const std::nothrow_t &mode) noexcept {
  return ::operator new(size, mode);
}
void operator delete[](void *memory) noexcept { ::operator delete(memory); }
void operator delete[](void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}
void operator delete[](void *memory, const std::nothrow_t &) noexcept {
  ::operator delete(memory);
}
extern "C" ssize_t write(int descriptor, const void *bytes, std::size_t size) {
  using Native = ssize_t (*)(int, const void *, std::size_t);
  static auto native_write = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "write"));
  if (!native_write)
    std::abort();
  if (descriptor != observed_descriptor)
    return native_write(descriptor, bytes, size);
  if (size > 8192 || observed_writes >= write_observations.size())
    std::abort();
  auto &observation = write_observations[observed_writes++];
  observation.requested = size;
  std::memcpy(observation.bytes.data(), bytes, size);
  ssize_t result;
  if (write_fault_phase == 1) {
    errno = EINTR;
    result = -1;
    ++write_fault_phase;
  } else if (write_fault_phase == 2) {
    result = native_write(descriptor, bytes, std::min<std::size_t>(2, size));
    ++write_fault_phase;
  } else if (write_fault_phase == 3) {
    errno = EAGAIN;
    result = -1;
    ++write_fault_phase;
  } else
    result = native_write(descriptor, bytes, size);
  observation.result = result;
  observation.error = errno;
  return result;
}
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class T> T native(ByteView &body) {
  require(body.size() >= sizeof(T), "complete independent native field");
  T value{};
  std::memcpy(&value, body.data(), sizeof(value));
  body = body.subspan(sizeof(T));
  return value;
}
using Key = std::pair<IdentityBytes, std::uint64_t>;
struct Record {
  std::uint32_t site;
  std::vector<std::byte> bytes;
};
using Records = std::map<Key, Record>;
Key reference(ByteView &body) {
  const auto lane = native<IdentityBytes>(body);
  return {lane, native<std::uint64_t>(body)};
}
Records records(const std::string &directory) {
  const auto runs = unwrap(discover_raw_runs(directory));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "real recorder closes native UI run");
  auto reader = unwrap(RawRunReader::open(directory + "/raw/" + runs.front().name));
  std::vector<std::byte> buffer(4 * 1024 * 1024);
  Records all;
  while (const auto event = unwrap(reader->read(buffer))) {
    auto body = event->raw;
    (void)native<std::uint32_t>(body);
    const auto site = native<std::uint32_t>(body);
    const auto ordinal = native<std::uint64_t>(body);
    require(site != 8001, "native owner does not fence ingress");
    require(all.emplace(Key{event->origin.lane, ordinal},
                        Record{site, {body.begin(), body.end()}})
                .second,
            "native UI origins unique");
  }
  return all;
}
std::string literal(ByteView &body) {
  const auto count = native<std::size_t>(body);
  require(count <= body.size(), "native original string exactbound");
  std::string value{reinterpret_cast<const char *>(body.data()), count};
  body = body.subspan(count);
  return value;
}
std::string physical_bytes(const std::string &path) {
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  require(descriptor >= 0, "independent native physical file open");
  std::string bytes;
  char chunk[8192];
  while (true) {
    const auto count = ::read(descriptor, chunk, sizeof(chunk));
    if (count < 0 && errno == EINTR)
      continue;
    require(count >= 0, "independent native physical file read");
    if (!count)
      break;
    bytes.append(chunk, static_cast<std::size_t>(count));
  }
  require(::close(descriptor) == 0, "independent native physical file close");
  return bytes;
}
void direct_owners() {
  char temporary[] = "/tmp/blackbird-ui-native-XXXXXX";
  require(::mkdtemp(temporary), "private nativeUI directory");
  const std::string directory{temporary};
  IdentityBytes identity{};
  identity[0] = std::byte{71};
  const std::string binary{"X\0\xffY\n", 5};
  RawEventRef frame{};
  {
    auto runtime = unwrap(
        RawRuntime::open(directory, unwrap(EnvironmentId::from_bytes(identity)),
                         {{64 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    TerminalUI ui{"native fixture"};
    ui.raw_capture(true);
    ui.text(binary);
    ui.text("second nativefragment");
    TaskState native_tasks;
    RawEventRef snapshot_source{};
    const auto snapshot = native_tasks.snapshot({}, &snapshot_source);
    require(snapshot_source.ordinal != 0,
            "actual native task snapshot completion source");
    ui.tasks(snapshot,
             RawUiValueOrigin{2, snapshot_source, {}, native_tasks.revision()});
    ChatView view{true};
    view.append(ChatKind::assistant, binary);
    view.append(ChatKind::notice, std::string(4096, 'L'));
    view.clear();
    Composer composer;
    composer.draft("native input");
    auto data = unwrap(raw_claim_data(composer.capture_bound(1)));
    auto control = unwrap(raw_claim_control(169));
    RawUiInput input{true, {}, 0, &data, &control, {}};
    const auto submitted = composer.feed('\r', &input);
    require(submitted.action == InputAction::submit &&
                submitted.text == "native input" && submitted.source.record.ordinal,
            "realComposer originalbeforehistory mutation");
    int pipe[2];
    require(::pipe(pipe) == 0, "real native outputpipe");
    {
      ChatOutput output;
      output.raw_capture(true);
      output.attach(pipe[1]);
      RawUiFrame capture{true, binary.size()};
      frame = capture.reference();
      capture.capture(binary);
      output.start(binary, capture.handoff());
      require(output.flush(), "real binary nativewrite completes");
    }
    char received[5];
    require(::read(pipe[0], received, sizeof(received)) == 5 &&
                std::memcmp(received, binary.data(), 5) == 0,
            "realpipe bytes equal independentbinary source");
    ::close(pipe[0]);
    ::close(pipe[1]);
    TerminalState state;
    state.draft = binary;
    state.cursor = 0;
    state.history = {"retainedhistory"};
    state.queued = {"pending"};
    save_terminal_state(directory + "/ui-state.bbm", state, true);
    const auto restored = load_terminal_state(directory + "/ui-state.bbm", true);
    require(restored.draft == binary && restored.history == state.history &&
                restored.queued == state.queued,
            "native persistence/reopen exact state");
  }
  const auto physical = physical_bytes(directory + "/ui-state.bbm");
  const auto all = records(directory);
  bool message = false, frame_body = false, write = false, submit = false,
       decoded = false, retire = false;
  std::size_t packets = 0;
  for (const auto &[key, record] : all) {
    ByteView body = record.bytes;
    if (record.site == 4000) {
      (void)native<int>(body);
      (void)native<std::uint8_t>(body);
      if (literal(body) == binary) {
        require(body.empty(), "exactmessage field count");
        message = true;
      }
    }
    if (record.site == 4041 && reference(body) == Key{frame.lane, frame.ordinal}) {
      require(literal(body) == binary && body.empty(),
              "native completed packet originalunchanged");
      frame_body = true;
    }
    if (record.site == 4044 && reference(body) == Key{frame.lane, frame.ordinal}) {
      (void)native<int>(body);
      require(native<std::size_t>(body) == 0 && native<std::size_t>(body) == 5 &&
                  body.empty(),
              "actualwrite exactoffset/requested bound");
      write = true;
    }
    if (record.site == 4010) {
      (void)reference(body);
      require(native<std::size_t>(body) == 0, "actualinput byte index");
      require(native<int>(body) == static_cast<int>(InputAction::submit),
              "actualsubmit action");
      (void)native<std::size_t>(body);
      (void)native<std::size_t>(body);
      require(literal(body) == "native input" && body.empty(),
              "actualsubmit originalbeforehistory");
      submit = true;
    }
    if (record.site == 4052) {
      (void)reference(body);
      const auto kind = native<std::uint8_t>(body);
      (void)native<std::size_t>(body);
      if (kind == 1) {
        require(literal(body) == binary && body.empty(),
                "native decoder originalbytes");
        decoded = true;
      }
    }
    if (record.site == 4021) {
      (void)reference(body);
      require(literal(body) == physical && body.empty(),
              "raw existing encoder/read packet equals independent physical bytes");
      ++packets;
    }
    if (record.site == 4032) {
      (void)reference(body);
      const auto reason = native<std::uint8_t>(body);
      if (reason == 4)
        retire = true;
    }
    (void)key;
  }
  require(message && frame_body && write && submit && decoded && retire && packets == 2,
          "actual source-to-disk nativeowner surfaces");
  std::filesystem::remove_all(directory);
}
void native_failures() {
  char temporary[] = "/tmp/blackbird-ui-fault-XXXXXX";
  require(::mkdtemp(temporary), "private native faultdirectory");
  const std::string directory{temporary};
  IdentityBytes identity{};
  identity[0] = std::byte{72};
  RawEventRef output_frame{}, closed_frame{};
  std::array<WriteObservation, 32> observations{};
  std::size_t count = 0;
  const std::string packet{"a\0\xfe"
                           "BC",
                           5};
  std::size_t partial_presentation = 0;
  {
    auto runtime = unwrap(
        RawRuntime::open(directory, unwrap(EnvironmentId::from_bytes(identity)),
                         {{64 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    TerminalUI ui{"native failures"};
    ui.raw_capture(true);
    const std::string refused(100, 'M');
    allocation_failed = false;
    fail_allocation_after = 1;
    bool failed = false;
    try {
      ui.notice(refused);
    } catch (const std::bad_alloc &) {
      failed = true;
    }
    fail_allocation_after = 0;
    require(failed && allocation_failed,
            "actualmessage allocationfailsafteroriginalcapture");
    ui.notice("later works");
    bool missing = false;
    try {
      ui.sessions(Value::object({{"sessions", Value{Value::Array{}}}}));
    } catch (const Error &error) {
      missing = error.code == ErrorCode::invalid_identity;
    }
    require(missing, "unobservedstructuredinputrefusedbeforequeueacceptance");
    for (const RawEventRef invalid :
         {RawEventRef{{}, 1}, RawEventRef{raw_lane_epoch(), 0}}) {
      missing = false;
      try {
        ui.sessions(Value::object({{"sessions", Value{Value::Array{}}}}),
                    RawUiValueOrigin{1, invalid, {}, {}});
      } catch (const Error &error) {
        missing = error.code == ErrorCode::invalid_identity;
      }
      require(missing, "providedzeroidentityrefusedbeforequeueacceptance");
    }
    Composer composer;
    const std::string draft(96, 'H');
    composer.draft(draft);
    auto data = unwrap(raw_claim_data(composer.capture_bound(1)));
    auto control = unwrap(raw_claim_control(169));
    RawUiInput input{true, {}, 0, &data, &control, {}};
    allocation_failed = false;
    fail_allocation_after = 2;
    failed = false;
    try {
      (void)composer.feed('\r', &input);
    } catch (const std::bad_alloc &) {
      failed = true;
    }
    fail_allocation_after = 0;
    require(failed && allocation_failed && composer.text() == draft,
            "actualhistoryallocationfailswithoutclearingnativeprompt");
    ChatView view{true};
    const std::string long_text(500, '@');
    allocation_failed = false;
    fail_allocation_after = 4;
    failed = false;
    try {
      view.append(ChatKind::assistant, long_text);
    } catch (const std::bad_alloc &) {
      failed = true;
    }
    fail_allocation_after = 0;
    require(failed && allocation_failed, "actualpresentationallocationfails");
    partial_presentation = view.size();
    std::size_t actual_characters = 0;
    for (const auto &row : view.rows(80)) {
      const auto plain = chat_plain(row);
      actual_characters +=
          static_cast<std::size_t>(std::count(plain.begin(), plain.end(), '@'));
    }
    require(actual_characters == partial_presentation && actual_characters < 500,
            "independentnativepresentation containsactualacceptedprefix");
    int pipe[2];
    require(::pipe(pipe) == 0, "faultoutput realpipe");
    {
      ChatOutput output;
      output.raw_capture(true);
      output.attach(pipe[1]);
      RawUiFrame capture{true, packet.size()};
      output_frame = capture.reference();
      capture.capture(packet);
      output.start(packet, capture.handoff());
      observed_descriptor = pipe[1];
      observed_writes = 0;
      write_fault_phase = 1;
      require(!output.flush() && output.pending(),
              "actualpartialprefixandEAGAINpreserveinflightpacket");
      require(output.flush() && !output.pending(), "actualremainingprefixcompletes");
      observed_descriptor = -1;
      write_fault_phase = 0;
      observations = write_observations;
      count = observed_writes;
    }
    char actual[5];
    require(::read(pipe[0], actual, sizeof(actual)) == 5 &&
                std::memcmp(actual, packet.data(), 5) == 0,
            "forwardedpartialnativebytescompleteexactlyonce");
    ::close(pipe[0]);
    ::close(pipe[1]);
    require(count == 4 && observations[0].result == -1 &&
                observations[0].error == EINTR && observations[1].result == 2 &&
                observations[2].result == -1 && observations[2].error == EAGAIN &&
                observations[3].result == 3,
            "independentnativefaultsequence");
    int bad[2];
    require(::pipe(bad) == 0, "realclosedoutputpipe");
    {
      ChatOutput output;
      output.raw_capture(true);
      output.attach(bad[1]);
      RawUiFrame capture{true, packet.size()};
      closed_frame = capture.reference();
      capture.capture(packet);
      output.start(packet, capture.handoff());
      ::close(bad[1]);
      failed = false;
      try {
        (void)output.flush();
      } catch (const std::runtime_error &) {
        failed = true;
      }
      require(failed && output.pending(), "realEBADFdoesnotfabricatefullframe");
    }
    ::close(bad[0]);
  }
  const auto all = records(directory);
  bool message_original = false, message_failure = false, submit_original = false,
       submit_failure = false, missing_failure = false, presentation_failure = false,
       partial_snapshot = false, closed_error = false, closed_retired = false;
  std::map<Key, std::pair<std::size_t, std::size_t>> output_requests;
  std::vector<std::pair<std::size_t, ssize_t>> actual_results;
  for (const auto &[key, record] : all) {
    ByteView body = record.bytes;
    if (record.site == 4000) {
      (void)native<int>(body);
      (void)native<std::uint8_t>(body);
      if (literal(body) == std::string(100, 'M'))
        message_original = true;
    }
    if (record.site == 4002) {
      (void)reference(body);
      if (native<std::uint8_t>(body) == 2)
        message_failure = true;
    }
    if (record.site == 4051) {
      (void)reference(body);
      const auto branch = native<std::uint8_t>(body);
      (void)native<std::uint8_t>(body);
      if (branch == 1) {
        missing_failure = native<ErrorCode>(body) == ErrorCode::invalid_identity;
      }
    }
    if (record.site == 4010) {
      (void)reference(body);
      (void)native<std::size_t>(body);
      if (native<int>(body) == static_cast<int>(InputAction::submit)) {
        (void)native<std::size_t>(body);
        (void)native<std::size_t>(body);
        if (literal(body) == std::string(96, 'H'))
          submit_original = true;
      }
    }
    if (record.site == 4012) {
      (void)reference(body);
      if (native<std::uint8_t>(body) == 2)
        submit_failure = true;
    }
    if (record.site == 4033) {
      (void)reference(body);
      if (native<std::uint8_t>(body) == 2)
        presentation_failure = true;
    }
    if (record.site == 4073) {
      (void)reference(body);
      (void)native<std::size_t>(body);
      partial_snapshot = native<std::size_t>(body) == partial_presentation;
    }
    if (record.site == 4044) {
      const auto parent = reference(body);
      (void)native<int>(body);
      const auto offset = native<std::size_t>(body), amount = native<std::size_t>(body);
      if (parent == Key{output_frame.lane, output_frame.ordinal})
        output_requests.emplace(key, std::pair{offset, amount});
    }
    if (record.site == 4045) {
      const auto parent = reference(body);
      const auto result = native<ssize_t>(body);
      const auto failed = native<bool>(body);
      if (const auto request = output_requests.find(parent);
          request != output_requests.end()) {
        actual_results.push_back({request->second.first, result});
        const auto index = actual_results.size() - 1;
        require(index < count &&
                    request->second.second == observations[index].requested &&
                    result == observations[index].result,
                "rawnativearguments/results equalindependentforwardingobservation");
        if (failed)
          require(native<int>(body) == observations[index].error,
                  "rawnativeerrno equalsactualreturnerror");
        require(body.empty(), "nativewrite exactoptionalvariant");
      } else if (failed && native<int>(body) == EBADF)
        closed_error = true;
    }
    if (record.site == 4042 &&
        reference(body) == Key{closed_frame.lane, closed_frame.ordinal}) {
      const auto branch = native<std::uint8_t>(body);
      require(branch != 3, "closednativeframe nevercompletes");
      if (branch == 4)
        closed_retired = true;
    }
  }
  require(message_original && message_failure && submit_original && submit_failure &&
              missing_failure && presentation_failure && partial_snapshot &&
              closed_error && closed_retired,
          "actualownerfailure original/outcome preservation");
  require(actual_results ==
              std::vector<std::pair<std::size_t, ssize_t>>{
                  {0, -1}, {0, 2}, {2, -1}, {2, 3}},
          "nativeoffsetadvancesonlybyactualpositiveprefix");
  std::filesystem::remove_all(directory);
}
void full_control_cleanup() {
  char temporary[] = "/tmp/blackbird-ui-full-XXXXXX";
  require(::mkdtemp(temporary), "privatefullCTRLdirectory");
  const std::string directory{temporary};
  IdentityBytes identity{};
  identity[0] = std::byte{73};
  RawEventRef frame{};
  {
    auto runtime =
        unwrap(RawRuntime::open(directory, unwrap(EnvironmentId::from_bytes(identity)),
                                {{1024 * 1024, 8192, 4096}, 4096}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    int pipe[2];
    require(::pipe(pipe) == 0, "realfullCTRLpipe");
    const int original = ::fcntl(pipe[1], F_GETFL);
    {
      ChatOutput output;
      output.raw_capture(true);
      output.attach(pipe[1]);
      RawUiFrame capture{true, 3};
      frame = capture.reference();
      capture.capture("end");
      output.start("end", capture.handoff());
      const auto status = unwrap(runtime->status());
      require(::kill(status.recorder, SIGSTOP) == 0, "actualrecorderstop");
      const std::uint8_t literal = 9;
      while (true) {
        auto credit = raw_try_claim_control(17);
        if (!credit.has_value()) {
          require(credit.error().code == ErrorCode::capacity,
                  "actualCTRLcapacityrefusal");
          break;
        }
        auto held = std::move(credit).value();
        (void)unwrap(raw_emit(
            7999, std::array{std::as_bytes(std::span{&literal, std::size_t{1}})},
            RawPartition::control, &held));
      }
      const auto refused = raw_try_claim_control(113);
      require(!refused.has_value() && refused.error().code == ErrorCode::capacity,
              "ordinarynativewritebankcannotadmit");
      output.detach();
      require(::fcntl(pipe[1], F_GETFL) == original,
              "actualprepaidflagrestorationwithoutnewCTRLadmission");
      require(::kill(status.recorder, SIGCONT) == 0, "actualrecorderresume");
    }
    ::close(pipe[0]);
    ::close(pipe[1]);
  }
  const auto all = records(directory);
  bool retired = false, restored = false;
  for (const auto &[key, record] : all) {
    ByteView body = record.bytes;
    if (record.site == 4042 && reference(body) == Key{frame.lane, frame.ordinal}) {
      const auto branch = native<std::uint8_t>(body);
      if (branch == 4) {
        require(native<std::size_t>(body) == 0,
                "unwrittennativepacketretirementexactoffset");
        retired = true;
      }
    }
    if (record.site == 4061) {
      const auto parent = reference(body);
      const auto request = all.find(parent);
      if (request != all.end() && request->second.site == 4047) {
        require(native<int>(body) == 0 && !native<bool>(body) && body.empty(),
                "prepaidcleanup actualnativefcntlreturn");
        restored = true;
      }
    }
    (void)key;
  }
  require(retired && restored,
          "fullordinaryCTRL cannotpreventheldpacket/resourcecleanup");
  std::filesystem::remove_all(directory);
}

void actual_pipe_pressure() {
  char temporary[] = "/tmp/blackbird-ui-pressure-XXXXXX";
  require(::mkdtemp(temporary), "private kernel pressure directory");
  const std::string directory{temporary};
  IdentityBytes identity{};
  identity[0] = std::byte{75};
  RawEventRef frame{}, rejected{}, unadopted{};
  {
    auto runtime =
        unwrap(RawRuntime::open(directory, unwrap(EnvironmentId::from_bytes(identity)),
                                {{1024 * 1024, 128 * 1024, 4096}, 4096}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    int pipe[2];
    require(::pipe(pipe) == 0, "real pressure pipe");
    {
      ChatOutput output;
      output.raw_capture(true);
      output.attach(pipe[1]);
      const std::array<char, 8192> filling{};
      ssize_t filled;
      do {
        filled = ::write(pipe[1], filling.data(), filling.size());
      } while (filled > 0);
      require(filled < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
              "native pipe actually reaches kernel pressure");
      RawUiFrame capture{true, 3};
      frame = capture.reference();
      capture.capture("end");
      output.start("end", capture.handoff());
      require(!output.flush() && output.pending(),
              "genuine kernel EAGAIN preserves pending packet");
      {
        RawUiFrame incoming{true, 3};
        rejected = incoming.reference();
        incoming.capture("new");
        bool refused = false;
        try {
          output.start("new", incoming.handoff());
        } catch (const std::logic_error &) {
          refused = true;
        }
        require(refused, "native in-flight packet cannot be replaced");
      }
      {
        RawUiFrame incoming{true, 3};
        unadopted = incoming.reference();
        incoming.capture("nil");
        auto owned = incoming.handoff();
        (void)owned;
      }
      require(::fcntl(pipe[0], F_SETFL, O_NONBLOCK) == 0, "native drain nonblocking");
      char bytes[8192];
      while (::read(pipe[0], bytes, sizeof(bytes)) > 0) {
      }
      require(errno == EAGAIN || errno == EWOULDBLOCK,
              "native pipe fully drained before retry");
      require(output.flush() && !output.pending(),
              "genuine kernel pressure recovery completes existing packet");
      require(::read(pipe[0], bytes, sizeof(bytes)) == 3 &&
                  std::memcmp(bytes, "end", 3) == 0,
              "native packet delivered after kernel pressure");
    }
    ::close(pipe[0]);
    ::close(pipe[1]);
  }
  const auto all = records(directory);
  bool pressure = false, rejection = false, abandonment = false;
  for (const auto &[key, record] : all) {
    (void)key;
    ByteView body = record.bytes;
    if (record.site == 4045) {
      const auto request = all.find(reference(body));
      if (request != all.end() && request->second.site == 4044) {
        ByteView arguments = request->second.bytes;
        if (reference(arguments) == Key{frame.lane, frame.ordinal} &&
            native<ssize_t>(body) < 0 && native<bool>(body)) {
          const auto error = native<int>(body);
          pressure = error == EAGAIN || error == EWOULDBLOCK;
        }
      }
    }
    if (record.site == 4042) {
      const auto source = reference(body);
      const auto branch = native<std::uint8_t>(body);
      if (branch == 4 && (source == Key{rejected.lane, rejected.ordinal} ||
                          source == Key{unadopted.lane, unadopted.ordinal})) {
        require(native<std::size_t>(body) == 0 && body.empty(),
                "unadopted paid packet retires at exact zero native offset");
        if (source == Key{rejected.lane, rejected.ordinal})
          rejection = true;
        else
          abandonment = true;
      }
    }
  }
  require(
      pressure && rejection && abandonment,
      "actual kernel pressure and lexical rejected/unused packet retirement preserved");
  std::filesystem::remove_all(directory);
}

void native_graphics_path_bound() {
  const char *old = ::getenv("BLACKBIRD_SPRITE_ASSET");
  const std::optional<std::string> saved =
      old ? std::optional<std::string>{old} : std::nullopt;
  struct Restore {
    std::optional<std::string> value;
    ~Restore() {
      if (value)
        ::setenv("BLACKBIRD_SPRITE_ASSET", value->c_str(), 1);
      else
        ::unsetenv("BLACKBIRD_SPRITE_ASSET");
    }
  } restore{saved};
  require(::setenv("BLACKBIRD_SPRITE_ASSET", "abc", 1) == 0,
          "native valid graphics source");
  require(blackbird_graphics_load() == "\x1b_Ga=t,f=100,t=f,i=72171,q=2;YWJj\x1b\\",
          "existing native graphics encoder exact packet");
  const std::string oversized(3073, 'x');
  require(::setenv("BLACKBIRD_SPRITE_ASSET", oversized.c_str(), 1) == 0,
          "native changed graphics source");
  bool refused = false;
  try {
    (void)blackbird_graphics_load();
  } catch (const std::length_error &) {
    refused = true;
  }
  require(refused,
          "actual graphics source bound enforced before native packet construction");
}

void repeated_native_history() {
  char temporary[] = "/tmp/blackbird-ui-history-XXXXXX";
  require(::mkdtemp(temporary), "private native repeated history directory");
  const std::string directory{temporary};
  IdentityBytes identity{};
  identity[0] = std::byte{76};
  const std::string prompt(1024 * 1024, 'h');
  {
    auto runtime = unwrap(
        RawRuntime::open(directory, unwrap(EnvironmentId::from_bytes(identity)),
                         {{64 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    Composer composer;
    composer.draft(prompt);
    auto feed = [&](char byte) {
      auto data = unwrap(raw_claim_data(composer.capture_bound(1)));
      auto control = unwrap(raw_claim_control(raw_ui_feed_credit));
      RawUiInput source{true, {}, 0, &data, &control, {}};
      return composer.feed(byte, &source);
    };
    for (unsigned submit = 0; submit < 4; ++submit) {
      if (submit)
        (void)feed('\x10');
      const auto actual = feed('\r');
      require(actual.action == InputAction::submit && actual.text == prompt,
              "native repeated recalled draft remains exact");
    }
  }
  const auto all = records(directory);
  unsigned submitted = 0;
  for (const auto &[key, record] : all) {
    (void)key;
    if (record.site != 4010)
      continue;
    ByteView body = record.bytes;
    (void)reference(body);
    (void)native<std::size_t>(body);
    const auto action = native<int>(body);
    (void)native<std::size_t>(body);
    (void)native<std::size_t>(body);
    if (action == static_cast<int>(InputAction::submit)) {
      require(literal(body) == prompt && body.empty(),
              "each full recalled native source has its own preserved bytes");
      ++submitted;
    }
  }
  require(submitted == 4,
          "source-derived semantic admission preserves repeated full history outputs");
  std::filesystem::remove_all(directory);
}

void review_palette_and_holder_cuts() {
  const std::string prefix(64, 'p'), alias(64, 'a');
  const std::string chosen = "/" + prefix + alias + " ";
  auto definition =
      Value::object({{"name", Value{"long-command"}},
                     {"description", Value{"native longest accepted alias"}},
                     {"source", Value{"return args"}},
                     {"aliases", Value{Value::Array{Value{alias}}}},
                     {"bare", Value{false}},
                     {"powerwords", Value{Value::Array{}}}});
  auto registry = std::make_shared<WorkflowRegistry>(
      Value::object({{"workflow_prefix", Value{prefix}},
                     {"workflows", Value{Value::Array{definition}}}}),
      "ui-longest");
  publish_workflows({});
  char temporary[] = "/tmp/blackbird-ui-review-XXXXXX";
  require(::mkdtemp(temporary), "private review UI directory");
  const std::string directory{temporary};
  IdentityBytes identity{};
  identity[0] = std::byte{77};
  RawEventRef captured{}, unused{}, missing{};
  {
    auto runtime = unwrap(
        RawRuntime::open(directory, unwrap(EnvironmentId::from_bytes(identity)),
                         {{64 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    auto original = unwrap(runtime->prepare_thread());
    auto other = unwrap(runtime->prepare_thread());
    auto output = std::make_unique<ChatOutput>();
    std::optional<RawUiPacket> abandoned;
    int pipe[2];
    require(::pipe(pipe) == 0, "cold owner native pipe");
    const int flags = ::fcntl(pipe[1], F_GETFL);
    {
      RawThreadBinding binding{original};
      auto feed = [](Composer &composer, char byte) {
        auto data = unwrap(raw_claim_data(composer.capture_bound(1)));
        auto control = unwrap(raw_claim_control(raw_ui_feed_credit));
        RawUiInput input{true, {}, 0, &data, &control, {}};
        return composer.feed(byte, &input);
      };
      Composer builtin;
      (void)feed(builtin, '\x14');
      require(feed(builtin, '\r').text == "/help",
              "empty native palette chooses exact help");
      publish_workflows(registry);
      Composer longest;
      (void)feed(longest, '\x14');
      for (const char byte : alias)
        (void)feed(longest, byte);
      const auto result = feed(longest, '\r');
      require(result.action == InputAction::choose_command && result.text == chosen,
              "maximum validated native workflow alias is fully acquired");
      output->raw_capture(true);
      output->attach(pipe[1]);
      RawUiFrame frame{true, 4};
      captured = frame.reference();
      frame.capture("kept");
      output->start("kept", frame.handoff());
      RawUiFrame unadopted{true, 3};
      unused = unadopted.reference();
      unadopted.capture("raw");
      abandoned.emplace(unadopted.handoff());
      RawUiFrame absent{true, 3};
      missing = absent.reference();
      bool refused = false;
      try {
        (void)absent.handoff();
      } catch (const Error &error) {
        refused = error.code == ErrorCode::invalid_identity;
      }
      require(refused, "selected handoff refuses missing actual original");
    }
    bool cross_thread_refused = false;
    std::jthread foreign([&] {
      try {
        (void)output->flush();
      } catch (const Error &error) {
        cross_thread_refused = error.code == ErrorCode::invalid_identity;
      }
    });
    foreign.join();
    require(cross_thread_refused, "selected foreign thread refuses before write");
    require(::fcntl(pipe[0], F_SETFL, O_NONBLOCK) == 0,
            "independent native empty pipe check");
    char byte;
    require(::read(pipe[0], &byte, 1) < 0 && errno == EAGAIN,
            "foreign thread and missing capture never wrote native bytes");
    {
      RawThreadBinding binding{other};
      output->detach();
      abandoned.reset();
      require(raw_current_thread() == other,
              "original-issuer cleanup restores distinct prior binding");
      require(::fcntl(pipe[1], F_GETFL) == flags, "cold native attach flags restored");
    }
    bool cold_source_refused = false;
    try {
      output->start("uncaptured");
    } catch (const Error &error) {
      cold_source_refused = error.code == ErrorCode::invalid_identity;
    }
    require(cold_source_refused,
            "selected cold output still refuses an uncaptured packet");
    output.reset();
    ::close(pipe[0]);
    ::close(pipe[1]);
  }
  publish_workflows({});
  const auto all = records(directory);
  unsigned help = 0, long_command = 0, retired = 0, restoration = 0;
  bool absent_refused = false;
  for (const auto &[key, record] : all) {
    ByteView body = record.bytes;
    if (record.site == 4010) {
      (void)reference(body);
      (void)native<std::size_t>(body);
      const auto action = native<int>(body);
      (void)native<std::size_t>(body);
      (void)native<std::size_t>(body);
      if (action == static_cast<int>(InputAction::choose_command)) {
        const auto text = literal(body);
        help += static_cast<unsigned>(text == "/help");
        long_command += static_cast<unsigned>(text == chosen);
      }
    }
    if (record.site == 4042) {
      const auto frame = reference(body);
      const auto branch = native<std::uint8_t>(body);
      if (branch == 4 && (frame == Key{captured.lane, captured.ordinal} ||
                          frame == Key{unused.lane, unused.ordinal})) {
        require(key.first == frame.first && native<std::size_t>(body) == 0,
                "cold packet retirement uses original lane and exact unwritten offset");
        ++retired;
      }
    }
    if (record.site == 4043 && reference(body) == Key{missing.lane, missing.ordinal}) {
      require(native<std::uint8_t>(body) == 1,
              "missing capture has actual Error outcome");
      absent_refused = true;
    }
    if (record.site == 4041)
      require(reference(body) != Key{missing.lane, missing.ordinal},
              "missing capture has no fabricated original");
    if (record.site == 4047) {
      const auto attach = reference(body);
      require(key.first == attach.first && key.first == captured.lane,
              "cold restoration request belongs to actual creating issuer");
      ++restoration;
    }
  }
  require(help == 1 && long_command == 1 && retired == 2 && restoration == 1 &&
              absent_refused,
          "exact native palette/holder/missing-capture source-to-disk outcomes");
  std::filesystem::remove_all(directory);
}

void review_capture_origin() {
  char temporary[] = "/tmp/blackbird-ui-origin-XXXXXX";
  require(::mkdtemp(temporary), "private actual origin directory");
  const std::string directory{temporary};
  IdentityBytes identity{};
  identity[0] = std::byte{78};
  {
    auto runtime =
        unwrap(RawRuntime::open(directory, unwrap(EnvironmentId::from_bytes(identity)),
                                {{1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    TerminalUI ui{"origin"};
    ui.raw_capture(true);
    {
      RawCaptureFailureScope capture{true};
      auto bank = unwrap(raw_claim_control(1));
      RawUiBurst burst{&bank};
      bool refused = false;
      try {
        ui.text("not accepted");
      } catch (const Error &error) {
        refused = error.code == ErrorCode::capacity;
      }
      require(refused && capture.error() &&
                  capture.error()->code == ErrorCode::capacity,
              "final borrowed UI refusal carries actual capture origin");
    }
    TerminalState state;
    state.draft = "unchanged";
    {
      RawCaptureFailureScope capture{true};
      bool refused = false;
      try {
        save_terminal_state(directory + "/absent.bbm", state, true);
      } catch (const Error &error) {
        refused = error.code == ErrorCode::capacity;
      }
      require(refused && capture.error(),
              "actual save source admission carries origin");
      require(!std::filesystem::exists(directory + "/absent.bbm"),
              "save raw refusal occurs before native file write");
    }
    const int file =
        ::open((directory + "/damaged.bbm").c_str(), O_CREAT | O_WRONLY, 0600);
    require(file >= 0 && ::write(file, "broken", 6) == 6 && ::close(file) == 0,
            "native corrupt-state acquisition source");
    {
      RawCaptureFailureScope capture{true};
      bool refused = false;
      try {
        (void)load_terminal_state(directory + "/damaged.bbm", true);
      } catch (const Error &error) {
        refused = error.code == ErrorCode::capacity;
      }
      require(refused && capture.error(),
              "actual load pre-acquisition bound refusal carries origin");
    }
    {
      RawCaptureFailureScope capture{true};
      bool damaged = false;
      try {
        (void)load_terminal_state(directory + "/damaged.bbm");
      } catch (const Error &) {
        damaged = true;
      }
      require(damaged && !capture.error(),
              "ordinary damaged metadata remains a business failure");
    }
  }
  const auto all = records(directory);
  for (const auto &[key, record] : all) {
    (void)key;
    require(record.site != 4001 && record.site != 4044 && record.site != 4076,
            "refused native UI operations never accept queue, transport or dispatch");
  }
  std::filesystem::remove_all(directory);
}

enum class UiCut { none, completion, load, save, damaged };
void actual_terminal_queue(UiCut cut = UiCut::none) {
  const bool completion_refusal = cut == UiCut::completion;
  const bool load_refusal = cut == UiCut::load;
  const bool save_refusal = cut == UiCut::save;
  char temporary[] = "/tmp/blackbird-ui-pty-XXXXXX";
  require(::mkdtemp(temporary), "private actualPTYdirectory");
  const std::string directory{temporary};
  int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  require(master >= 0 && ::grantpt(master) == 0 && ::unlockpt(master) == 0,
          "realnativePTYmaster");
  const char *name = ::ptsname(master);
  require(name, "nativePTYslavepath");
  const int slave = ::open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
  require(slave >= 0, "actualPTYslave");
  winsize size{};
  size.ws_row = 35;
  size.ws_col = 110;
  require(::ioctl(slave, TIOCSWINSZ, &size) == 0, "actualnativePTYdimensions");
  int ready[2], gate[2];
  require(::pipe(ready) == 0 && ::pipe(gate) == 0, "actualworkerfixturegates");
  const auto child = ::fork();
  require(child >= 0, "realUIprocess");
  if (!child) {
    ::close(ready[0]);
    ::close(gate[1]);
    ::close(master);
    if (::dup2(slave, STDIN_FILENO) < 0 || ::dup2(slave, STDOUT_FILENO) < 0)
      ::_exit(80);
    ::close(slave);
    ::setenv("BLACKBIRD_MASCOT", "0", 1);
    int status = 0;
    RawCaptureFailureScope owner_capture{true};
    try {
      IdentityBytes id{};
      id[0] = std::byte{74};
      auto runtime =
          unwrap(RawRuntime::open(directory, unwrap(EnvironmentId::from_bytes(id)),
                                  {{load_refusal   ? 1024ULL * 1024
                                    : save_refusal ? 8ULL * 1024 * 1024
                                                   : 256ULL * 1024 * 1024,
                                    1024 * 1024, 128 * 1024},
                                   128 * 1024}));
      if (load_refusal || cut == UiCut::damaged) {
        const int file =
            ::open((directory + "/ui-state.bbm").c_str(), O_CREAT | O_WRONLY, 0600);
        require(file >= 0 && ::write(file, "broken", 6) == 6 && ::close(file) == 0,
                "actual UI corrupt state native source");
      }
      RawThreadBinding binding{unwrap(runtime->prepare_thread())};
      TerminalUI ui{"actual native UI"};
      ui.raw_capture(true);
      std::atomic_bool cancelled{false};
      ui.run(
          [&](std::string_view input) {
            require(input == "first", "canceledpendingpromptneverdispatches");
            ui.text(std::string_view{"worker\0\xff", 8});
            const char byte = 1;
            require(::write(ready[1], &byte, 1) == 1, "nativeworkerready");
            if (completion_refusal) {
              // Actual loan split rejects the final complete post on its worker.
              // Retain the issuing producer until TLS releases its loan/credit.
              struct CompletionRefusal {
                RawThreadHandle context;
                std::optional<RawControlToken> exhausted;
                std::optional<RawUiBurst> loan;
              };
              static thread_local CompletionRefusal refusal;
              refusal.context = raw_current_thread();
              refusal.exhausted.emplace(unwrap(raw_claim_control(0)));
              refusal.loan.emplace(&*refusal.exhausted);
              return;
            }
            char returned;
            require(::read(gate[0], &returned, 1) == 1, "actualworkerunblocked");
            require(cancelled.load(), "actualcancelrequestreachesworker");
            ui.text("worker released");
          },
          cancelled, {}, {}, directory + "/ui-state.bbm");
      require(!completion_refusal && !load_refusal && !save_refusal,
              "refused native admission must propagate to UI owner");
    } catch (const Error &error) {
      if ((completion_refusal || load_refusal || save_refusal) &&
          error.code == ErrorCode::capacity) {
        if (!owner_capture.error() ||
            owner_capture.error()->code != ErrorCode::capacity)
          status = 2;
        else
          status = 0;
      } else {
        std::cerr << "PTYUI Error " << error_name(error.code) << " " << error.detail
                  << '\n';
        status = 1;
      }
    } catch (const std::exception &error) {
      std::cerr << "PTYUI " << error.what() << '\n';
      status = 1;
    }
    ::close(ready[1]);
    ::close(gate[0]);
    ::_exit(status);
  }
  ::close(slave);
  ::close(ready[1]);
  ::close(gate[0]);
  struct Cleanup {
    pid_t child;
    int master, ready, gate;
    bool joined = false;
    ~Cleanup() {
      if (!joined) {
        ::kill(child, SIGKILL);
        int status;
        (void)::waitpid(child, &status, 0);
      }
      ::close(master);
      ::close(ready);
      ::close(gate);
    }
  } cleanup{child, master, ready[0], gate[1]};
  std::string observed;
  const char *phase = "first frame";
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  auto drain = [&] {
    pollfd descriptor{master, POLLIN, 0};
    if (::poll(&descriptor, 1, 20) > 0 && (descriptor.revents & POLLIN)) {
      char bytes[8192];
      const auto count = ::read(master, bytes, sizeof(bytes));
      if (count > 0)
        observed.append(bytes, static_cast<std::size_t>(count));
    }
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error(
          std::string{"nativePTYownerwatchdog: "} + phase + " output=" +
          observed.substr(observed.size() > 1200 ? observed.size() - 1200 : 0));
  };
  auto verify_state_failure = [&](bool raw_refusal) {
    phase = "actual selected state failure join";
    int status = 0;
    while (::waitpid(child, &status, WNOHANG) == 0)
      drain();
    cleanup.joined = true;
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "actual state failure survives and propagates before new work");
    char effect;
    require(::read(ready[0], &effect, 1) == 0,
            "state refusal or damaged-state quit dispatches no worker effect");
    bool outcome = false, advisory = false;
    for (const auto &[key, record] : records(directory)) {
      (void)key;
      ByteView body = record.bytes;
      require(record.site != 4076, "state refusal never dispatches a native worker");
      if (record.site == 4023) {
        (void)reference(body);
        const auto branch = native<std::uint8_t>(body);
        (void)native<std::uint8_t>(body);
        if (raw_refusal && branch == 1 &&
            native<ErrorCode>(body) == ErrorCode::capacity)
          outcome = true;
      }
      if (record.site == 4000) {
        (void)native<int>(body);
        (void)native<std::uint8_t>(body);
        const auto text = literal(body);
        if (text.find("UI state damaged/unreadable") != std::string::npos ||
            text.find("UI state save failed") != std::string::npos)
          advisory = true;
      }
      if (load_refusal)
        require(record.site != 4044,
                "raw load refusal performs no later native output write");
    }
    require(
        raw_refusal ? outcome && !advisory : advisory,
        "raw state refusal and ordinary damaged fallback are distinct actual outcomes");
    if (save_refusal)
      require(!std::filesystem::exists(directory + "/ui-state.bbm"),
              "UI save raw refusal occurs before file creation");
    std::filesystem::remove_all(directory);
  };
  if (load_refusal) {
    verify_state_failure(true);
    return;
  }
  while (observed.find("\x1b[?2026l") == std::string::npos)
    drain();
  if (cut == UiCut::damaged) {
    require(::write(master, "/quit\r", 6) == 6,
            "ordinary damaged-state explicit native quit");
    verify_state_failure(false);
    return;
  }
  require(::write(master, "first\r", 6) == 6, "actualnativefirstsubmit");
  if (save_refusal) {
    verify_state_failure(true);
    return;
  }
  phase = "worker start";
  pollfd announcement{ready[0], POLLIN, 0};
  require(::poll(&announcement, 1, 5000) > 0, "nativeworkerstartwatchdog");
  char announced;
  require(::read(ready[0], &announced, 1) == 1, "actualworkerrunning");
  if (completion_refusal) {
    phase = "actual completion refusal join";
    int status = 0;
    while (::waitpid(child, &status, WNOHANG) == 0)
      drain();
    cleanup.joined = true;
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "completion refusal survives and propagates after native worker join");
    require(::read(ready[0], &announced, 1) == 0,
            "refused completion worker performed exactly one native effect");
    bool failed = false;
    for (const auto &[key, record] : records(directory)) {
      (void)key;
      ByteView body = record.bytes;
      if (record.site == 4077) {
        (void)reference(body);
        require(native<std::uint8_t>(body) == 1 && native<std::uint8_t>(body) == 3 &&
                    native<ErrorCode>(body) == ErrorCode::capacity,
                "paid actual worker stage3 completion refusal outcome");
        failed = true;
      }
      if (record.site == 4000)
        require(
            native<int>(body) != 11,
            "refused completion never emits a successful complete message original");
    }
    require(failed, "actual completion refusal remains disk preserved");
    std::filesystem::remove_all(directory);
    return;
  }
  require(::write(master, "queued-input\r", 13) == 13, "actualnativequeuedsubmit");
  phase = "queued persistence";
  auto saved = [&](const std::vector<std::string> &expected) {
    try {
      return load_terminal_state(directory + "/ui-state.bbm").queued == expected;
    } catch (const Error &) {
      return false;
    }
  };
  while (!saved({"queued-input"}))
    drain();
  require(::write(master, "/cancel\r", 8) == 8, "actualnativecancelcommand");
  phase = "cancel persistence";
  while (!saved({}))
    drain();
  require(::write(gate[1], "R", 1) == 1, "nativeworkerreleaseaftercancel");
  phase = "worker output";
  while (observed.find("worker released") == std::string::npos)
    drain();
  require(::write(master, "/quit\r", 6) == 6, "actualnativequit");
  phase = "quit";
  int status = 0;
  while (::waitpid(child, &status, WNOHANG) == 0)
    drain();
  cleanup.joined = true;
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "actualTUIworkerqueuecancelquitcompletes");
  const auto all = records(directory);
  bool queued = false, withdrawn = false, drained = false, applied = false,
       frames = false, keyboard = false, worker = false, complete = false;
  std::optional<IdentityBytes> worker_lane;
  for (const auto &[key, record] : all)
    if (record.site == 4076)
      worker_lane = key.first;
  require(worker_lane.has_value(), "actual worker native execution source");
  std::map<Key, Key> prompt_sources;
  for (const auto &[key, record] : all) {
    ByteView body = record.bytes;
    if (record.site == 4000 && key.first == *worker_lane) {
      const auto kind = native<int>(body);
      (void)native<std::uint8_t>(body);
      const auto text = literal(body);
      if (text == "worker released")
        worker = true;
      if (kind == 11) {
        require(text.empty() && body.empty(),
                "actual worker completion native field shape");
        complete = true;
      }
    }
    if (record.site == 4077) {
      (void)reference(body);
      require(native<std::uint8_t>(body) == 0,
              "native cancellation does not relabel successful worker as admission "
              "failure");
    }
    if (record.site == 4010) {
      const auto feed = reference(body);
      const auto source = all.find(feed);
      require(source != all.end() && source->second.site == 4080,
              "actualcookedresultlinksactualfeedowner");
      (void)native<std::size_t>(body);
      const auto action = native<int>(body);
      (void)native<std::size_t>(body);
      (void)native<std::size_t>(body);
      if (action == static_cast<int>(InputAction::submit) &&
          literal(body) == "queued-input")
        prompt_sources.emplace(key, Key{});
    }
    if (record.site == 4013) {
      const auto action = reference(body);
      (void)native<std::uint8_t>(body);
      const auto source = reference(body);
      const auto offset = native<std::size_t>(body), length = native<std::size_t>(body);
      if (prompt_sources.contains(source)) {
        require(action == source && offset == 0 && length == 12,
                "actualqueuedsourceexactnativeoriginalrange");
        prompt_sources[source] = key;
        queued = true;
      }
    }
    if (record.site == 4015) {
      const auto source = reference(body);
      const auto branch = native<std::uint8_t>(body);
      for (const auto &[original, queue] : prompt_sources) {
        (void)original;
        if (source == queue) {
          require(branch == 2, "actualcanceledpendingpromptwithdrawswithoutdequeue");
          withdrawn = true;
        }
      }
    }
    if (record.site == 4008)
      drained = true;
    if (record.site == 4006)
      applied = true;
    if (record.site == 4041)
      frames = true;
    if (record.site == 4086) {
      (void)reference(body);
      const auto source = reference(body);
      const auto found = all.find(source);
      require(found != all.end() && found->second.site == 2041,
              "actualUIbatchlinksnativepreservedkeyboardread");
      keyboard = true;
    }
  }
  require(queued && withdrawn && drained && applied && frames && keyboard && worker &&
              complete,
          "actualTUIingress/queues/presentation/transportsource-to-disk with whole "
          "worker binding");
  std::filesystem::remove_all(directory);
}

} // namespace
int main() {
  try {
    direct_owners();
    native_failures();
    full_control_cleanup();
    actual_pipe_pressure();
    native_graphics_path_bound();
    repeated_native_history();
    review_palette_and_holder_cuts();
    review_capture_origin();
    actual_terminal_queue();
    actual_terminal_queue(UiCut::completion);
    actual_terminal_queue(UiCut::load);
    actual_terminal_queue(UiCut::save);
    actual_terminal_queue(UiCut::damaged);
    std::cout << "native UI direct owner source-to-disk checks passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << "UI native Error " << error_name(error.code) << " " << error.detail
              << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
