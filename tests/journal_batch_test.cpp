#include "blackbird/journal_writer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blackbird;

namespace {
std::atomic<std::ptrdiff_t> allocation_cut{-1};
}
void *operator new(std::size_t size) {
  if (allocation_cut.load() >= 0 && allocation_cut.fetch_sub(1) == 0) {
    throw std::bad_alloc{};
  }
  if (void *pointer = std::malloc(size == 0 ? 1 : size)) {
    return pointer;
  }
  throw std::bad_alloc{};
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept {
  ::operator delete[](pointer);
}

namespace {
void check(bool condition, const char *expression, int line) {
  if (!condition) {
    throw std::runtime_error(std::to_string(line) + ": " + expression);
  }
}
#define CHECK(expression) check((expression), #expression, __LINE__)
template <typename T> T require(Result<T> result) {
  CHECK(result.has_value());
  return std::move(result).value();
}
void require(Result<void> result) { CHECK(result.has_value()); }
template <typename T> void error_is(const Result<T> &result, ErrorCode code) {
  CHECK(!result.has_value());
  CHECK(result.error().code == code);
}
template <typename T> T id(unsigned char value) {
  IdentityBytes bytes{};
  bytes[15] = std::byte{value};
  return require(T::from_bytes(bytes));
}
struct StorageState {
  std::vector<std::byte> submitted;
  std::vector<std::byte> synchronized;
  std::size_t max_write = 7;
  std::size_t max_read = SIZE_MAX;
  std::size_t writes = 0;
  std::size_t syncs = 0;
  std::size_t locks = 0;
  std::size_t reads = 0;
  std::size_t extents = 0;
  std::uint64_t read_failure_offset = UINT64_MAX;
  std::function<void()> on_read;
  std::size_t interrupt_writes = 0;
  int write_interrupt_detail = EINTR;
  std::size_t write_guard = SIZE_MAX;
  std::size_t interrupt_reads = 0;
  std::size_t interrupts_between_reads = 0;
  std::size_t interrupt_extents = 0;
  bool extent_failure = false;
  std::size_t fail_after = SIZE_MAX;
  bool sync_failure = false;
  bool zero_progress = false;
  bool zero_read = false;
  bool impossible_write = false;
  bool impossible_read = false;
  bool interrupted_sync = false;
};
class MemoryFile final : public JournalFile {
public:
  explicit MemoryFile(std::shared_ptr<StorageState> state) : state_(std::move(state)) {}
  Result<void> lock_writer() override {
    ++state_->locks;
    return Result<void>::success();
  }
  Result<std::size_t> read_at(std::uint64_t offset, MutableByteView output) override {
    ++state_->reads;
    if (state_->on_read) {
      auto callback = std::move(state_->on_read);
      callback();
    }
    if (offset >= state_->read_failure_offset) {
      return Result<std::size_t>::failure({ErrorCode::io, 5});
    }
    if (state_->interrupt_reads != 0) {
      --state_->interrupt_reads;
      return Result<std::size_t>::failure({ErrorCode::interrupted, 4});
    }
    if (state_->impossible_read)
      return Result<std::size_t>::success(output.size() + 1);
    if (state_->zero_read) {
      return Result<std::size_t>::success(0);
    }
    if (offset >= state_->submitted.size()) {
      return Result<std::size_t>::success(0);
    }
    const auto count =
        std::min({output.size(), state_->max_read,
                  state_->submitted.size() - static_cast<std::size_t>(offset)});
    std::copy_n(state_->submitted.begin() + static_cast<std::ptrdiff_t>(offset), count,
                output.begin());
    state_->interrupt_reads = state_->interrupts_between_reads;
    return Result<std::size_t>::success(count);
  }
  Result<std::size_t> write_at(std::uint64_t offset, ByteView input) override {
    ++state_->writes;
    if (state_->interrupt_writes != 0) {
      CHECK(state_->writes <= state_->write_guard);
      --state_->interrupt_writes;
      return Result<std::size_t>::failure(
          {ErrorCode::interrupted, state_->write_interrupt_detail});
    }
    if (state_->zero_progress) {
      return Result<std::size_t>::success(0);
    }
    if (state_->impossible_write) {
      return Result<std::size_t>::success(input.size() + 1);
    }
    if (offset >= state_->fail_after) {
      return Result<std::size_t>::failure({ErrorCode::io, 28});
    }
    const auto count =
        std::min({input.size(), state_->max_write,
                  state_->fail_after - static_cast<std::size_t>(offset)});
    state_->submitted.resize(static_cast<std::size_t>(offset) + count);
    std::copy_n(input.begin(), count,
                state_->submitted.begin() + static_cast<std::ptrdiff_t>(offset));
    return Result<std::size_t>::success(count);
  }
  Result<std::uint64_t> extent() override {
    ++state_->extents;
    if (state_->extent_failure)
      return Result<std::uint64_t>::failure({ErrorCode::io, 5});
    if (state_->interrupt_extents > 0) {
      --state_->interrupt_extents;
      return Result<std::uint64_t>::failure({ErrorCode::interrupted});
    }
    return Result<std::uint64_t>::success(state_->submitted.size());
  }
  Result<void> synchronize(SyncStrength strength) override {
    CHECK(strength == SyncStrength::full);
    ++state_->syncs;
    if (state_->interrupted_sync) {
      return Result<void>::failure({ErrorCode::interrupted, 4});
    }
    if (state_->sync_failure) {
      return Result<void>::failure({ErrorCode::io, 5});
    }
    auto snapshot = state_->submitted;
    state_->synchronized.swap(snapshot);
    return Result<void>::success();
  }

private:
  std::shared_ptr<StorageState> state_;
};
class MemoryDirectory final : public JournalDirectory {
public:
  std::shared_ptr<StorageState> state = std::make_shared<StorageState>();
  bool exists = false;
  bool directory_failure = false;
  std::size_t directory_syncs = 0;
  Result<std::unique_ptr<JournalFile>> create_exclusive(std::string_view) override {
    if (exists) {
      return Result<std::unique_ptr<JournalFile>>::failure({ErrorCode::conflict});
    }
    exists = true;
    return Result<std::unique_ptr<JournalFile>>::success(
        std::make_unique<MemoryFile>(state));
  }
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view,
                                                     FileAccess access) override {
    CHECK(access == FileAccess::read_write);
    if (!exists) {
      return Result<std::unique_ptr<JournalFile>>::failure({ErrorCode::io, 2});
    }
    return Result<std::unique_ptr<JournalFile>>::success(
        std::make_unique<MemoryFile>(state));
  }
  Result<void> synchronize_directory(SyncStrength strength) override {
    CHECK(strength == SyncStrength::full);
    CHECK(state->submitted == state->synchronized);
    ++directory_syncs;
    return directory_failure ? Result<void>::failure({ErrorCode::io, 5})
                             : Result<void>::success();
  }
};
JournalHeader header() {
  return {id<EnvironmentId>(1), id<AuditStreamId>(2), 3, {64, 512}, std::nullopt};
}
constexpr JournalCapacity capacity{4096, 32};
const std::array original{std::byte{0}, std::byte{255}, std::byte{10}};
const std::array drafts{JournalDraft{FrameKind::source, original}};

void interrupted_write_settlement() {
  for (const int detail : {ECANCELED, ETIMEDOUT, EINTR}) {
    MemoryDirectory directory;
    auto writer =
        require(FramedJournal::create(directory, "journal", header(), capacity));
    const auto before = writer->cursor();
    const auto writes = directory.state->writes;
    directory.state->write_interrupt_detail = detail;
    directory.state->interrupt_writes = detail == EINTR ? 20 : 2;
    // Old endless retry fails this direct call-count oracle, not a wall clock.
    directory.state->write_guard = writes + 8;
    const auto result = writer->append(drafts);
    error_is(result, ErrorCode::interrupted);
    CHECK(result.error().detail == detail);
    CHECK(directory.state->writes == writes + (detail == EINTR ? 8 : 1));
    CHECK(writer->state() == JournalWriterState::poisoned);
    CHECK(writer->cursor().sequence == before.sequence &&
          writer->cursor().end_offset == before.end_offset &&
          writer->physical_records().empty());
    error_is(writer->append(drafts), ErrorCode::audit_unavailable);
    CHECK(directory.state->submitted == directory.state->synchronized);
  }
}

void commitment() {
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  CHECK(directory.directory_syncs == 1 && directory.state->syncs == 1);
  const auto live_bytes = require(writer->read_original_range(0, 64));
  CHECK(live_bytes.bytes ==
        std::vector<std::byte>(directory.state->submitted.begin(),
                               directory.state->submitted.begin() + 64));
  CHECK(writer->state() == JournalWriterState::live);
  directory.state->interrupt_writes = 2;
  const auto cursor = require(writer->append(drafts));
  CHECK(cursor.sequence == 2 && cursor.end_offset == 203);
  CHECK(writer->physical_records().size() == 1);
  CHECK(directory.state->submitted == directory.state->synchronized);
  const auto bytes = require(writer->read_payload(writer->physical_records()[0]));
  CHECK(std::equal(original.begin(), original.end(), bytes.begin(), bytes.end()));
  auto forged = writer->physical_records()[0];
  ++forged.payload_offset;
  error_is(writer->read_payload(forged), ErrorCode::stale_handle);
  forged = writer->physical_records()[0];
  ++forged.sequence; // Commit sequence is deliberately absent from payload records.
  error_is(writer->read_payload(forged), ErrorCode::stale_handle);
  // Commit follows a 35-byte source: first=1, count=1, reserved=0.
  const auto &stored = directory.state->synchronized;
  CHECK(stored[153] == std::byte{255} && stored[154] == std::byte{255});
  CHECK(stored[179] == std::byte{1} && stored[187] == std::byte{1});
  CHECK(stored[199] == std::byte{0} && stored[202] == std::byte{0});
  writer.reset();
  auto recovered =
      require(FramedJournal::open(directory, "journal", header(), capacity));
  CHECK(recovered->state() == JournalWriterState::recovery_pending);
  CHECK(recovered->physical_records().empty());
  CHECK(recovered->staged_records().size() == 1);
  directory.state->max_read = 2;
  directory.state->interrupt_reads = 3;
  CHECK(require(recovered->read_payload(recovered->staged_records()[0])) ==
        std::vector<std::byte>(original.begin(), original.end()));
  error_is(recovered->append(drafts), ErrorCode::audit_unavailable);
  require(recovered->confirm_recovery());
  CHECK(recovered->state() == JournalWriterState::recovered);
  CHECK(require(recovered->read_original_range(144, 3)).bytes ==
        std::vector<std::byte>(original.begin(), original.end()));
  CHECK(recovered->state() == JournalWriterState::recovered);
  error_is(recovered->append(drafts), ErrorCode::audit_unavailable);
  require(recovered->resume_after_reconciliation());
  CHECK(require(recovered->append(drafts)).sequence == 4);
}

void failures_and_cuts() {
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  const auto original_header = directory.state->synchronized;
  directory.state->sync_failure = true;
  error_is(writer->append(drafts), ErrorCode::io);
  CHECK(writer->state() == JournalWriterState::poisoned);
  CHECK(writer->physical_records().empty());
  CHECK(directory.state->synchronized == original_header);
  const auto complete_pending = directory.state->submitted;
  const auto writes = directory.state->writes;
  error_is(writer->append(drafts), ErrorCode::audit_unavailable);
  CHECK(directory.state->writes == writes);
  writer.reset();
  directory.state->sync_failure = false;
  auto discovered =
      require(FramedJournal::open(directory, "journal", header(), capacity));
  CHECK(discovered->staged_records().size() == 1);
  require(discovered->confirm_recovery());
  CHECK(directory.state->synchronized == complete_pending);
  discovered.reset();
  for (std::size_t end = 112; end < complete_pending.size(); ++end) {
    directory.state->submitted.assign(complete_pending.begin(),
                                      complete_pending.begin() +
                                          static_cast<std::ptrdiff_t>(end));
    auto cut = require(FramedJournal::open(directory, "journal", header(), capacity));
    CHECK(cut->staged_records().empty());
    if (end == 112) {
      CHECK(cut->recovery_report().clean());
    } else {
      CHECK(!cut->recovery_report().clean());
      CHECK(cut->recovery_report().problem.has_value() &&
            cut->recovery_report().problem->code == ErrorCode::incomplete);
    }
    const auto before = directory.state->submitted;
    const auto original_state = cut->state();
    std::vector<std::byte> inspected;
    directory.state->max_read = 2;
    directory.state->interrupt_reads = 1;
    directory.state->interrupts_between_reads = 7;
    for (std::size_t offset = 0; offset < end; offset += 11) {
      const auto length = std::min<std::size_t>(11, end - offset);
      const auto bytes = require(cut->read_original_range(offset, length));
      CHECK(bytes.journal == header().journal && bytes.offset == offset &&
            bytes.observed_extent == end);
      inspected.insert(inspected.end(), bytes.bytes.begin(), bytes.bytes.end());
    }
    directory.state->interrupts_between_reads = 0;
    directory.state->interrupt_reads = 0;
    CHECK(inspected == before && cut->state() == original_state &&
          directory.state->submitted == before);
    const auto boundary = require(cut->read_original_range(0, 64));
    CHECK(boundary.bytes ==
          std::vector<std::byte>(before.begin(), before.begin() + 64));
    CHECK(require(cut->read_original_range(end, 0)).bytes.empty());
    error_is(cut->read_original_range(UINT64_MAX, 1), ErrorCode::invalid_range);
    error_is(cut->read_original_range(end, 1), ErrorCode::invalid_range);
    error_is(cut->read_original_range(0, 65), ErrorCode::invalid_range);
    directory.state->zero_read = true;
    error_is(cut->read_original_range(112, 1),
             end == 112 ? ErrorCode::invalid_range : ErrorCode::incomplete);
    directory.state->zero_read = false;
    directory.state->impossible_read = true;
    error_is(cut->read_original_range(0, 1), ErrorCode::io);
    directory.state->impossible_read = false;
    directory.state->interrupt_reads = 8;
    error_is(cut->read_original_range(0, 1), ErrorCode::interrupted);
    directory.state->interrupts_between_reads = 8;
    error_is(cut->read_original_range(0, 3), ErrorCode::interrupted);
    directory.state->interrupts_between_reads = 0;
    directory.state->extent_failure = true;
    error_is(cut->read_original_range(0, 1), ErrorCode::io);
    directory.state->extent_failure = false;
    directory.state->interrupt_extents = 8;
    error_is(cut->read_original_range(0, 1), ErrorCode::interrupted);
    directory.state->interrupt_extents = 1;
    CHECK(require(cut->read_original_range(0, 1)).bytes[0] == before[0]);
    allocation_cut = 0;
    const auto no_memory = cut->read_original_range(0, 1);
    allocation_cut = -1;
    error_is(no_memory, ErrorCode::allocation);
    CHECK(cut->state() == original_state && cut->physical_records().empty());
    directory.state->max_read = SIZE_MAX;
    CHECK(cut->physical_records().empty());
    CHECK(cut->pending_records().size() == (end >= 147 ? 1U : 0U));
    if (end >= 147) {
      CHECK(require(cut->read_payload(cut->pending_records()[0])) ==
            std::vector<std::byte>(original.begin(), original.end()));
    }
    require(cut->confirm_recovery());
    CHECK(require(cut->read_original_range(0, 64)).bytes ==
          std::vector<std::byte>(before.begin(), before.begin() + 64));
    CHECK(cut->state() ==
          (end == 112 ? JournalWriterState::recovered : JournalWriterState::blocked));
    if (end != 112) {
      error_is(cut->resume_after_reconciliation(), ErrorCode::audit_unavailable);
    } else {
      require(cut->resume_after_reconciliation());
    }
  }
  directory.state->submitted = original_header;
  auto partial = require(FramedJournal::open(directory, "journal", header(), capacity));
  require(partial->confirm_recovery());
  require(partial->resume_after_reconciliation());
  directory.state->fail_after = 130;
  error_is(partial->append(drafts), ErrorCode::io);
  CHECK(partial->state() == JournalWriterState::poisoned);
  CHECK(directory.state->submitted.size() == 130);
  CHECK(partial->physical_records().empty());
  const auto observed = require(partial->read_original_range(112, 18));
  CHECK(observed.bytes ==
        std::vector<std::byte>(directory.state->submitted.begin() + 112,
                               directory.state->submitted.end()));
  CHECK(partial->state() == JournalWriterState::poisoned &&
        observed.observed_extent == 130);
}

void publication_and_capacity() {
  MemoryDirectory bad_directory;
  bad_directory.directory_failure = true;
  error_is(FramedJournal::create(bad_directory, "journal", header(), capacity),
           ErrorCode::io);
  CHECK(bad_directory.state->submitted.size() == 112);
  MemoryDirectory directory;
  auto exact = require(FramedJournal::create(directory, "journal", header(), {203, 1}));
  CHECK(require(exact->append(drafts)).end_offset == 203);
  const auto bytes = directory.state->submitted;
  const auto writes = directory.state->writes;
  error_is(exact->append(drafts), ErrorCode::capacity);
  CHECK(exact->state() == JournalWriterState::live);
  CHECK(directory.state->writes == writes && directory.state->submitted == bytes);
  exact.reset();
  auto recovered =
      require(FramedJournal::open(directory, "journal", header(), {203, 1}));
  CHECK(recovered->staged_records().size() == 1);
  directory.directory_failure = true;
  error_is(recovered->confirm_recovery(), ErrorCode::io);
  CHECK(recovered->state() == JournalWriterState::poisoned);
  CHECK(recovered->physical_records().empty());
  CHECK(recovered->staged_records().size() == 1);
  error_is(recovered->append(drafts), ErrorCode::audit_unavailable);
  CHECK(directory.state->writes == writes && directory.state->submitted == bytes);
}

void allocation_and_progress_failures() {
  bool finished = false;
  bool saw_preparation_failure = false;
  bool saw_submitted_failure = false;
  for (std::ptrdiff_t cut = 0; cut < 32; ++cut) {
    MemoryDirectory directory;
    auto writer =
        require(FramedJournal::create(directory, "journal", header(), capacity));
    const auto original_header = directory.state->synchronized;
    const auto writes = directory.state->writes;
    allocation_cut.store(cut);
    const auto result = writer->append(drafts);
    allocation_cut.store(-1);
    if (result.has_value()) {
      CHECK(result.value().end_offset == 203 && result.value().sequence == 2);
      CHECK(writer->physical_records().size() == 1);
      CHECK(directory.state->submitted == directory.state->synchronized);
      finished = true;
      break;
    }
    error_is(result, ErrorCode::allocation);
    CHECK(writer->physical_records().empty() && writer->cursor().sequence == 0);
    CHECK(directory.state->synchronized == original_header);
    if (directory.state->writes == writes) {
      CHECK(directory.state->submitted == original_header);
      CHECK(writer->state() == JournalWriterState::live);
      saw_preparation_failure = true;
    } else {
      CHECK(writer->state() == JournalWriterState::poisoned);
      saw_submitted_failure = true;
    }
  }
  CHECK(finished && saw_preparation_failure && saw_submitted_failure);
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  const auto original_header = directory.state->submitted;
  directory.state->zero_progress = true;
  error_is(writer->append(drafts), ErrorCode::io);
  CHECK(writer->state() == JournalWriterState::poisoned);
  CHECK(writer->physical_records().empty());
  CHECK(directory.state->submitted == original_header);
  writer.reset();
  directory.state->zero_progress = false;
  auto impossible =
      require(FramedJournal::open(directory, "journal", header(), capacity));
  require(impossible->confirm_recovery());
  require(impossible->resume_after_reconciliation());
  directory.state->impossible_write = true;
  error_is(impossible->append(drafts), ErrorCode::io);
  CHECK(impossible->state() == JournalWriterState::poisoned);
  CHECK(directory.state->submitted == original_header);
  impossible.reset();
  directory.state->impossible_write = false;
  auto interrupted =
      require(FramedJournal::open(directory, "journal", header(), capacity));
  require(interrupted->confirm_recovery());
  require(interrupted->resume_after_reconciliation());
  directory.state->interrupted_sync = true;
  error_is(interrupted->append(drafts), ErrorCode::interrupted);
  CHECK(interrupted->state() == JournalWriterState::poisoned);
  CHECK(interrupted->physical_records().empty());
}

void multi_frame_and_open_bounds() {
  MemoryDirectory directory;
  auto small_header = header();
  small_header.limits = {38, 126};
  auto writer =
      require(FramedJournal::create(directory, "journal", small_header, capacity));
  const std::array two{drafts[0], drafts[0]};
  const auto committed = require(writer->append(two));
  CHECK(committed.sequence == 3 && committed.end_offset == 238);
  const auto bytes = directory.state->submitted;
  CHECK(bytes[214] == std::byte{1} && bytes[222] == std::byte{2});
  // Independent format-derived CRC for the two 35-byte source frames.
  CHECK(bytes[230] == std::byte{106} && bytes[231] == std::byte{248} &&
        bytes[232] == std::byte{236} && bytes[233] == std::byte{195});
  const auto writes = directory.state->writes;
  const std::array three{drafts[0], drafts[0], drafts[0]};
  error_is(writer->append(three), ErrorCode::capacity);
  CHECK(writer->state() == JournalWriterState::live);
  CHECK(directory.state->submitted == bytes && directory.state->writes == writes);
  writer.reset();
  directory.state->max_read = 3;
  directory.state->interrupt_reads = 2;
  auto recovered =
      require(FramedJournal::open(directory, "journal", small_header, capacity));
  CHECK(recovered->staged_records().size() == 2);
  require(recovered->confirm_recovery());
  CHECK(recovered->physical_records()[0].sequence == 1 &&
        recovered->physical_records()[1].sequence == 2);
  recovered.reset();
  auto wrong = small_header;
  wrong.environment = id<EnvironmentId>(9);
  error_is(FramedJournal::open(directory, "journal", wrong, capacity),
           ErrorCode::wrong_environment);
  wrong = small_header;
  wrong.limits.max_payload = 37;
  error_is(FramedJournal::open(directory, "journal", wrong, capacity),
           ErrorCode::conflict);
  error_is(FramedJournal::open(directory, "journal", small_header, {237, 32}),
           ErrorCode::capacity);
  directory.state->submitted.resize(64);
  error_is(FramedJournal::open(directory, "journal", small_header, capacity),
           ErrorCode::incomplete);
  directory.state->submitted = bytes;
  auto limited =
      require(FramedJournal::open(directory, "journal", small_header, {4096, 1}));
  CHECK(limited->staged_records().empty());
  CHECK(limited->recovery_report().problem ==
        std::optional<Error>{{ErrorCode::capacity}});
  require(limited->confirm_recovery());
  CHECK(limited->state() == JournalWriterState::blocked);
  limited.reset();
  directory.state->submitted[136] = std::byte{39}; // Length > declared payload 38.
  auto invalid =
      require(FramedJournal::open(directory, "journal", small_header, capacity));
  CHECK(invalid->recovery_report().problem ==
        std::optional<Error>{{ErrorCode::corrupt}});
  CHECK(invalid->staged_records().empty());
  invalid.reset();
  directory.state->submitted = bytes;
  directory.state->zero_read = true;
  error_is(FramedJournal::open(directory, "journal", small_header, capacity),
           ErrorCode::incomplete);
  directory.state->zero_read = false;
  MemoryDirectory allocation;
  allocation_cut.store(0);
  const auto unavailable =
      FramedJournal::create(allocation, "journal", header(), capacity);
  allocation_cut.store(-1);
  error_is(unavailable, ErrorCode::allocation);
  CHECK(!allocation.exists && allocation.state->writes == 0);
  allocation_cut.store(0);
  const auto failed_open =
      FramedJournal::open(directory, "journal", small_header, capacity);
  allocation_cut.store(-1);
  error_is(failed_open, ErrorCode::allocation);
  CHECK(directory.state->submitted == bytes);
}

void nonprefix_survival_and_corruption() {
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  require(writer->append(drafts));
  const auto acknowledged = directory.state->synchronized;
  require(writer->append(drafts));
  const auto later = directory.state->submitted;
  writer.reset();
  // The model preserves acknowledged bytes, but independently persists pending
  // header/payload/commit writes. It does not assume a prefix or APFS ordering.
  for (unsigned int mask = 0; mask < 8; ++mask) {
    auto surviving = acknowledged;
    surviving.resize(294, std::byte{0});
    const std::array ranges{std::pair<std::size_t, std::size_t>{203, 235},
                            std::pair<std::size_t, std::size_t>{235, 238},
                            std::pair<std::size_t, std::size_t>{238, 294}};
    for (std::size_t group = 0; group < ranges.size(); ++group) {
      if ((mask & (1U << group)) != 0) {
        const auto [start, end] = ranges[group];
        std::copy(later.begin() + static_cast<std::ptrdiff_t>(start),
                  later.begin() + static_cast<std::ptrdiff_t>(end),
                  surviving.begin() + static_cast<std::ptrdiff_t>(start));
      }
    }
    directory.state->submitted = surviving;
    auto recovered =
        require(FramedJournal::open(directory, "journal", header(), capacity));
    CHECK(recovered->staged_records().size() == (mask == 7 ? 2U : 1U));
    CHECK(recovered->recovery_report().clean() == (mask == 7));
    require(recovered->confirm_recovery());
    const auto source =
        require(recovered->read_payload(recovered->physical_records()[0]));
    CHECK(std::equal(original.begin(), original.end(), source.begin(), source.end()));
    if (mask != 7) {
      error_is(recovered->resume_after_reconciliation(), ErrorCode::audit_unavailable);
    }
  }
  directory.state->submitted = acknowledged;
  auto changed = require(FramedJournal::open(directory, "journal", header(), capacity));
  require(changed->confirm_recovery());
  require(changed->resume_after_reconciliation());
  directory.state->submitted[144] ^= std::byte{1};
  error_is(changed->read_payload(changed->physical_records()[0]), ErrorCode::corrupt);
  CHECK(changed->state() == JournalWriterState::blocked);
  error_is(changed->append(drafts), ErrorCode::audit_unavailable);
  changed.reset();
  directory.state->submitted = acknowledged;
  auto unexpected_tail =
      require(FramedJournal::open(directory, "journal", header(), capacity));
  require(unexpected_tail->confirm_recovery());
  require(unexpected_tail->resume_after_reconciliation());
  directory.state->submitted.push_back(std::byte{99});
  const auto before = directory.state->submitted;
  error_is(unexpected_tail->append(drafts), ErrorCode::corrupt);
  CHECK(directory.state->submitted == before);
}
void selected_replay_prefixes() {
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  require(writer->append(drafts));
  require(writer->append(drafts));
  const auto original_bytes = directory.state->submitted;
  require(writer->restage());
  CHECK(writer->state() == JournalWriterState::recovery_pending);
  CHECK(writer->staged_records().size() == 2);
  CHECK(writer->cursor().sequence == 4 && writer->cursor().end_offset == 294);
  error_is(writer->append(drafts), ErrorCode::audit_unavailable);
  require(writer->select_recovered_prefix({header().journal, 4, 294}));
  require(writer->confirm_recovery());
  CHECK(writer->state() == JournalWriterState::blocked);
  CHECK(writer->staged_records().empty());
  error_is(writer->resume_after_reconciliation(), ErrorCode::audit_unavailable);
  require(writer->restage()); // Does not clear the historical flag.
  // Deliberately omit reselection: that would merely set the flag again and
  // conceal a restage implementation that incorrectly cleared it.
  require(writer->confirm_recovery());
  CHECK(writer->state() == JournalWriterState::blocked);
  error_is(writer->resume_after_reconciliation(), ErrorCode::audit_unavailable);
  writer.reset();
  for (const auto selected : std::array{JournalPredecessor{header().journal, 0, 112},
                                        JournalPredecessor{header().journal, 2, 203},
                                        JournalPredecessor{header().journal, 4, 294}}) {
    auto historical =
        require(FramedJournal::open(directory, "journal", header(), capacity));
    const auto staged_before = historical->staged_records().size();
    for (const auto invalid :
         std::array{JournalPredecessor{header().journal, 1, 147},
                    JournalPredecessor{header().journal, 2, 202},
                    JournalPredecessor{header().journal, 3, 238},
                    JournalPredecessor{header().journal, 6, 385}}) {
      error_is(historical->select_recovered_prefix(invalid), ErrorCode::invalid_range);
      CHECK(historical->staged_records().size() == staged_before);
      CHECK(historical->cursor().sequence == 4 &&
            historical->cursor().end_offset == 294);
    }
    error_is(historical->select_recovered_prefix({id<AuditStreamId>(77), 2, 203}),
             ErrorCode::conflict);
    require(historical->select_recovered_prefix(selected));
    CHECK(historical->staged_records().size() == selected.validated_sequence / 2);
    CHECK(historical->pending_records().size() == 2 - selected.validated_sequence / 2);
    CHECK(historical->cursor().sequence == selected.validated_sequence &&
          historical->cursor().end_offset == selected.validated_end_offset);
    CHECK(historical->recovery_report().available_end == 294);
    require(historical->confirm_recovery());
    CHECK(historical->state() == JournalWriterState::blocked);
    error_is(historical->resume_after_reconciliation(), ErrorCode::audit_unavailable);
    CHECK(directory.state->submitted == original_bytes);
  }
}
void restage_every_uncertain_tail() {
  for (std::size_t end = 203; end < 294; ++end) {
    MemoryDirectory directory;
    auto writer =
        require(FramedJournal::create(directory, "journal", header(), capacity));
    require(writer->append(drafts));
    const auto acknowledged = writer->physical_records()[0];
    directory.state->fail_after = end;
    error_is(writer->append(drafts), ErrorCode::io);
    const auto unchanged = directory.state->submitted;
    const auto writes = directory.state->writes;
    require(writer->restage());
    CHECK(writer->staged_records().size() == 1);
    CHECK(writer->staged_records()[0] == acknowledged);
    CHECK(writer->cursor().sequence == 2 && writer->cursor().end_offset == 203);
    CHECK(writer->recovery_report().clean() == (end == 203));
    CHECK(directory.state->writes == writes && directory.state->submitted == unchanged);
  }
}
void selected_multirecord_batch_and_diagnostic() {
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  const std::array pair{drafts[0], drafts[0]};
  require(writer->append(pair));
  CHECK(writer->cursor().sequence == 3 && writer->cursor().end_offset == 238);
  directory.state->fail_after = 260;
  error_is(writer->append(drafts), ErrorCode::io);
  const auto before = directory.state->submitted;
  require(writer->restage());
  CHECK(writer->staged_records().size() == 2);
  CHECK(writer->recovery_report().problem &&
        writer->recovery_report().problem->code == ErrorCode::incomplete);
  const auto diagnostic = writer->recovery_report().diagnostic_offset;
  // This inferred boundary follows source1 but is inside a two-source batch.
  error_is(writer->select_recovered_prefix({header().journal, 2, 203}),
           ErrorCode::invalid_range);
  CHECK(writer->cursor().sequence == 3 && writer->staged_records().size() == 2);
  allocation_cut.store(0);
  const auto selected = writer->select_recovered_prefix({header().journal, 0, 112});
  allocation_cut.store(-1);
  require(selected);
  CHECK(writer->staged_records().empty() && writer->pending_records().size() == 2);
  CHECK(writer->pending_records()[0].sequence == 1 &&
        writer->pending_records()[1].sequence == 2);
  CHECK(writer->recovery_report().diagnostic_offset == diagnostic);
  CHECK(writer->recovery_report().available_end == 260);
  CHECK(directory.state->submitted == before);
}
void restage_acknowledged_history() {
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  require(writer->append(drafts));
  const auto acknowledged = writer->physical_records()[0];
  const auto before = directory.state->submitted;
  // Replace the source AND its commit with a different, internally valid batch.
  // Identical identities, offsets and lengths cannot conceal changed source bytes.
  const std::array replacement{std::byte{9}, std::byte{8}, std::byte{7}};
  auto source = require(
      encode_journal_frame(FrameKind::source, 1, 1, replacement, header().limits));
  std::array<std::byte, 24> commit_payload{};
  commit_payload[0] = std::byte{1};
  commit_payload[8] = std::byte{1};
  auto checksum = crc32c(source);
  for (std::size_t i = 0; i < 4; ++i) {
    commit_payload[16 + i] = static_cast<std::byte>(checksum & 255);
    checksum >>= 8;
  }
  const auto commit = require(
      encode_journal_frame(FrameKind::commit, 2, 1, commit_payload, header().limits));
  std::copy(source.begin(), source.end(), directory.state->submitted.begin() + 112);
  std::copy(commit.begin(), commit.end(), directory.state->submitted.begin() + 147);
  const auto rewritten = directory.state->submitted;
  const auto writes = directory.state->writes;
  error_is(writer->read_payload(acknowledged), ErrorCode::corrupt);
  error_is(writer->restage(), ErrorCode::corrupt);
  CHECK(writer->state() == JournalWriterState::blocked);
  CHECK(writer->physical_records()[0] == acknowledged);
  CHECK(directory.state->submitted == rewritten && directory.state->writes == writes);
  // A fresh scan recognizes the replacement as valid; refusal above specifically
  // comes from the surviving acknowledged checksum, not malformed replacement bytes.
  auto fresh = require(FramedJournal::open(directory, "journal", header(), capacity));
  CHECK(fresh->recovery_report().clean() && fresh->staged_records().size() == 1);
  CHECK(require(fresh->read_payload(fresh->staged_records()[0])) ==
        std::vector<std::byte>(replacement.begin(), replacement.end()));
  for (std::size_t end = 112; end < 203; ++end) {
    directory.state->submitted.assign(
        before.begin(), before.begin() + static_cast<std::ptrdiff_t>(end));
    error_is(writer->restage(), ErrorCode::corrupt);
    CHECK(writer->state() == JournalWriterState::blocked);
    CHECK(writer->physical_records()[0] == acknowledged);
    CHECK(writer->recovery_report().available_end == end);
    error_is(writer->confirm_recovery(), ErrorCode::audit_unavailable);
  }
  directory.state->submitted = before;
  require(writer->restage());
  CHECK(writer->staged_records()[0] == acknowledged);
  CHECK(directory.state->locks == 2); // Create and independent open, never restage.
  // Replace the acknowledged commit with a second source in the same batch.
  // Original source metadata and CRC remain identical, but its old commit
  // boundary no longer exists. Containment alone must not accept this history.
  MemoryDirectory combined;
  auto combined_writer =
      require(FramedJournal::create(combined, "journal", header(), capacity));
  const std::array pair{drafts[0], drafts[0]};
  require(combined_writer->append(pair));
  CHECK(combined_writer->physical_records()[0] == acknowledged);
  directory.state->submitted = combined.state->submitted;
  error_is(writer->restage(), ErrorCode::corrupt);
  CHECK(writer->state() == JournalWriterState::blocked);
}
void selected_prefix_pending_order() {
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  require(writer->append(drafts));
  require(writer->append(drafts));
  directory.state->fail_after = 329; // Third source complete; its commit absent.
  error_is(writer->append(drafts), ErrorCode::io);
  require(writer->restage());
  CHECK(writer->staged_records().size() == 2 && writer->pending_records().size() == 1);
  CHECK(writer->pending_records()[0].sequence == 5);
  allocation_cut.store(0);
  const auto selected = writer->select_recovered_prefix({header().journal, 2, 203});
  allocation_cut.store(-1);
  require(selected);
  CHECK(writer->staged_records().size() == 1 && writer->pending_records().size() == 2);
  CHECK(writer->pending_records()[0].sequence == 3 &&
        writer->pending_records()[1].sequence == 5);
  require(writer->confirm_recovery());
  CHECK(writer->physical_records().size() == 1 && writer->staged_records().empty());
  CHECK(writer->pending_records()[0].sequence == 3 &&
        writer->pending_records()[1].sequence == 5);
}
void payload_failure_preserves_unknown_write() {
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  require(writer->append(drafts));
  const auto acknowledged = writer->physical_records()[0];
  directory.state->fail_after = 215;
  error_is(writer->append(drafts), ErrorCode::io);
  CHECK(writer->state() == JournalWriterState::poisoned);
  directory.state->read_failure_offset = 112;
  error_is(writer->read_payload(acknowledged), ErrorCode::io);
  CHECK(writer->state() == JournalWriterState::poisoned);
  directory.state->read_failure_offset = UINT64_MAX;
  directory.state->submitted[144] ^= std::byte{1};
  error_is(writer->read_payload(acknowledged), ErrorCode::corrupt);
  CHECK(writer->state() == JournalWriterState::poisoned);
}
void restage_unknown_and_rejected_batches() {
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  directory.state->fail_after = 130;
  error_is(writer->append(drafts), ErrorCode::io);
  require(writer->restage());
  CHECK(writer->staged_records().empty());
  CHECK(writer->cursor().sequence == 0 && writer->cursor().end_offset == 112);
  CHECK(writer->recovery_report().problem &&
        writer->recovery_report().problem->code == ErrorCode::incomplete);
  // Complete second submission with failed sync has no acknowledged record yet.
  directory.state->submitted.resize(112);
  directory.state->fail_after = SIZE_MAX;
  require(writer->restage());
  require(writer->confirm_recovery());
  require(writer->resume_after_reconciliation());
  directory.state->sync_failure = true;
  error_is(writer->append(drafts), ErrorCode::io);
  CHECK(writer->physical_records().empty());
  directory.state->sync_failure = false;
  require(writer->restage());
  CHECK(writer->staged_records().size() == 1 && writer->recovery_report().clean());
  require(writer->confirm_recovery());
  require(writer->resume_after_reconciliation());
  const auto acknowledged = writer->physical_records()[0];
  directory.state->sync_failure = true;
  error_is(writer->append(drafts), ErrorCode::io);
  directory.state->sync_failure = false;
  require(writer->restage());
  require(writer->reject_recovery_batch(3, {ErrorCode::corrupt}));
  CHECK(writer->staged_records().size() == 1 && writer->pending_records().size() == 1);
  require(writer->restage()); // Prior semantic rejection is not an acknowledged batch.
  CHECK(writer->staged_records().size() == 2 && writer->recovery_report().clean());
  CHECK(writer->physical_records().size() == 1 &&
        writer->physical_records()[0] == acknowledged);
  CHECK(directory.state->locks == 1);
}
void restage_failures_and_reentrancy() {
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  require(writer->append(drafts));
  const auto before = directory.state->submitted;
  const auto acknowledged = writer->physical_records()[0];
  const auto writes = directory.state->writes;
  const auto check_closed = [&] {
    CHECK(writer->state() == JournalWriterState::blocked);
    CHECK(writer->physical_records()[0] == acknowledged);
    error_is(writer->append(drafts), ErrorCode::audit_unavailable);
    CHECK(directory.state->writes == writes);
  };
  directory.state->extent_failure = true;
  auto failed = writer->restage();
  error_is(failed, ErrorCode::io);
  CHECK(failed.error().detail == 5);
  check_closed();
  directory.state->extent_failure = false;
  directory.state->interrupt_extents = 8;
  auto extents = directory.state->extents;
  error_is(writer->restage(), ErrorCode::interrupted);
  CHECK(directory.state->extents == extents + 8);
  check_closed();
  directory.state->interrupt_reads = 8;
  auto reads = directory.state->reads;
  error_is(writer->restage(), ErrorCode::interrupted);
  CHECK(directory.state->reads == reads + 8);
  check_closed();
  for (const auto offset : {std::uint64_t{0}, std::uint64_t{112}, std::uint64_t{144}}) {
    directory.state->read_failure_offset = offset;
    failed = writer->restage();
    error_is(failed, ErrorCode::io);
    CHECK(failed.error().detail == 5);
    check_closed();
  }
  directory.state->read_failure_offset = UINT64_MAX;
  directory.state->impossible_read = true;
  error_is(writer->restage(), ErrorCode::io);
  check_closed();
  directory.state->impossible_read = false;
  directory.state->zero_read = true;
  error_is(writer->restage(), ErrorCode::incomplete);
  check_closed();
  directory.state->zero_read = false;
  for (const auto which : {0, 1, 2}) {
    auto changed = header();
    if (which == 0)
      changed.environment = id<EnvironmentId>(7);
    if (which == 1)
      changed.issuer_namespace = 8;
    auto encoded = require(encode_journal_header(changed));
    if (which == 2)
      encoded[0] = std::byte{0};
    std::copy(encoded.begin(), encoded.end(), directory.state->submitted.begin());
    error_is(writer->restage(), which == 0   ? ErrorCode::wrong_environment
                                : which == 1 ? ErrorCode::conflict
                                             : ErrorCode::corrupt);
    check_closed();
  }
  directory.state->submitted = before;
  directory.state->submitted.resize(100);
  error_is(writer->restage(), ErrorCode::incomplete);
  check_closed();
  directory.state->submitted.resize(4097);
  error_is(writer->restage(), ErrorCode::capacity);
  check_closed();
  directory.state->submitted = before;
  allocation_cut.store(0);
  const auto allocation_failure = writer->restage();
  allocation_cut.store(-1);
  error_is(allocation_failure, ErrorCode::allocation);
  check_closed();
  bool callback_ran = false;
  directory.state->on_read = [&] {
    callback_ran = true;
    error_is(writer->restage(), ErrorCode::busy);
    error_is(writer->append(drafts), ErrorCode::busy);
    error_is(writer->select_recovered_prefix({header().journal, 0, 112}),
             ErrorCode::busy);
    error_is(writer->reject_recovery_batch(1, {ErrorCode::corrupt}), ErrorCode::busy);
    error_is(writer->confirm_recovery(), ErrorCode::busy);
    error_is(writer->resume_after_reconciliation(), ErrorCode::busy);
    // Payload validation can block the writer on error, so it too must not
    // mutate the staging owner's state from a storage callback.
    directory.state->read_failure_offset = 112;
    error_is(writer->read_payload(acknowledged), ErrorCode::busy);
    directory.state->read_failure_offset = UINT64_MAX;
  };
  directory.state->max_read = 2;
  directory.state->interrupt_reads = 7;
  directory.state->interrupts_between_reads = 7;
  directory.state->interrupt_extents = 7;
  require(writer->restage());
  CHECK(callback_ran && writer->staged_records()[0] == acknowledged);
  CHECK(writer->state() == JournalWriterState::recovery_pending);
  CHECK(directory.state->locks == 1 && directory.state->submitted == before);
}
void usage_observation() {
  MemoryDirectory directory;
  auto writer =
      require(FramedJournal::create(directory, "journal", header(), capacity));
  auto usage = writer->usage();
  CHECK(usage.prefix.end_offset == 112 && usage.observed_extent == 112);
  CHECK(usage.remaining_bytes() == 4096 - 112 && usage.remaining_records() == 32);
  CHECK(!usage.approaching());
  require(writer->append(drafts));
  usage = writer->usage();
  CHECK(usage.indexed_records == drafts.size());
  CHECK(usage.remaining_records() == 32 - drafts.size());
  CHECK(usage.remaining_bytes() == 4096 - directory.state->submitted.size());
  auto smaller = usage;
  smaller.limit.max_records = drafts.size();
  CHECK(smaller.remaining_records() == 0 && smaller.approaching());
  directory.state->extent_failure = true;
  auto unknown = writer->usage();
  CHECK(!unknown.observed_extent && unknown.extent_error && !unknown.remaining_bytes());
  CHECK(writer->state() == JournalWriterState::live);
  directory.state->extent_failure = false;
  // Unindexed tail remains a physical observation, not acknowledged history.
  const auto committed = usage.prefix.end_offset;
  directory.state->submitted.resize(4097);
  usage = writer->usage();
  CHECK(usage.prefix.end_offset == committed && usage.observed_extent == 4097);
  CHECK(usage.remaining_bytes() == 0 && usage.approaching());
  directory.state->submitted.resize(committed);
  require(writer->restage());
  usage = writer->usage();
  CHECK(usage.state == JournalWriterState::recovery_pending);
  CHECK(usage.indexed_records == drafts.size()); // overlapping indexes count once
}
} // namespace

int main() {
  try {
    interrupted_write_settlement();
    usage_observation();
    commitment();
    failures_and_cuts();
    publication_and_capacity();
    allocation_and_progress_failures();
    multi_frame_and_open_bounds();
    nonprefix_survival_and_corruption();
    payload_failure_preserves_unknown_write();
    selected_replay_prefixes();
    restage_every_uncertain_tail();
    selected_multirecord_batch_and_diagnostic();
    restage_acknowledged_history();
    selected_prefix_pending_order();
    restage_unknown_and_rejected_batches();
    restage_failures_and_reentrancy();
    std::puts("PASS journal commit, staged recovery, exact cuts and failure closure");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL journal batches: %s\n", error.what());
    return 1;
  }
}
