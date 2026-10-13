#include "blackbird/journal_writer.hpp"

#include <algorithm>

namespace blackbird {
namespace {
class AppendAttempt {
public:
  explicit AppendAttempt(JournalWriterState &state) : state_(state) {}
  ~AppendAttempt() {
    if (!complete_) {
      state_ = JournalWriterState::poisoned;
    }
  }
  AppendAttempt(const AppendAttempt &) = delete;
  AppendAttempt &operator=(const AppendAttempt &) = delete;
  void complete() noexcept { complete_ = true; }

private:
  JournalWriterState &state_;
  bool complete_ = false;
};
class RestageGuard {
public:
  explicit RestageGuard(bool &active) : active_(active) { active_ = true; }
  ~RestageGuard() { active_ = false; }
  RestageGuard(const RestageGuard &) = delete;
  RestageGuard &operator=(const RestageGuard &) = delete;

private:
  bool &active_;
};
JournalCursor commit_boundary(const PhysicalJournalRecord &last) noexcept {
  return {last.journal, last.sequence + 1,
          last.payload_offset + last.payload_size + 56};
}
void put_little(std::span<std::byte> bytes, std::uint64_t value) noexcept {
  for (auto &byte : bytes) {
    byte = static_cast<std::byte>(value & 255);
    value >>= 8;
  }
}
std::uint64_t get_little(ByteView bytes) noexcept {
  std::uint64_t value = 0;
  for (std::size_t offset = 0; offset < bytes.size(); ++offset) {
    value |= static_cast<std::uint64_t>(std::to_integer<unsigned int>(bytes[offset]))
             << (offset * 8);
  }
  return value;
}
} // namespace

FramedJournal::FramedJournal(JournalDirectory &directory, JournalHeader header,
                             JournalCapacity capacity, SyncStrength strength,
                             RawRetainedOrigin raw_origin)
    : raw_reader_origin_(
          std::make_shared<RawRetainedReadOrigin>(std::move(raw_origin))),
      raw_origin_(raw_reader_origin_->origin), header_(header), capacity_(capacity),
      strength_(strength), directory_(directory),
      cursor_{header.journal, 0, journal_header_size},
      recovery_{std::nullopt, journal_header_size, journal_header_size} {}

JournalUsage FramedJournal::usage() const {
  const auto extent = file_->extent();
  return {capacity_,
          cursor_,
          extent.has_value() ? std::optional{extent.value()} : std::nullopt,
          extent.has_value() ? std::nullopt : std::optional{extent.error()},
          archived_records_ +
              std::max(records_.size(), staged_.size() + pending_.size()),
          state_};
}

Result<std::unique_ptr<FramedJournal>>
FramedJournal::allocate(JournalDirectory &directory, JournalHeader header,
                        JournalCapacity capacity, SyncStrength strength,
                        RawRetainedOrigin raw_origin) {
  if (capacity.max_file_bytes < journal_header_size + 88 ||
      capacity.max_file_bytes > INT64_MAX || capacity.max_records == 0) {
    return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::invalid_range});
  }
  const auto encoded = encode_journal_header(header);
  if (!encoded.has_value()) {
    return Result<std::unique_ptr<FramedJournal>>::failure(encoded.error());
  }
  try {
    auto journal = std::unique_ptr<FramedJournal>{new FramedJournal{
        directory, header, capacity, strength, std::move(raw_origin)}};
    if (capacity.max_records > journal->records_.max_size()) {
      return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::capacity});
    }
    journal->records_.reserve(capacity.max_records);
    journal->staged_.reserve(capacity.max_records);
    journal->pending_.reserve(capacity.max_records);
    return Result<std::unique_ptr<FramedJournal>>::success(std::move(journal));
  } catch (const std::bad_alloc &) {
    return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::allocation});
  }
}

Result<std::unique_ptr<FramedJournal>>
FramedJournal::create(JournalDirectory &directory, std::string_view name,
                      JournalHeader header, JournalCapacity capacity,
                      SyncStrength strength, const RawRetainedOrigin &raw_origin) {
  const std::uint8_t method = 1;
  RawRetainedFields arguments;
  arguments.header(header);
  arguments.native(capacity.max_file_bytes);
  arguments.native(capacity.max_records);
  arguments.native(strength);
  arguments.native(method);
  RawRetainedPair trace{raw_origin, {3166, 3167}, 278, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::unique_ptr<FramedJournal>> {
    auto allocated = allocate(directory, header, capacity, strength, trace.child());
    if (!allocated.has_value()) {
      return allocated;
    }
    auto journal = std::move(allocated).value();
    journal->raw_origin_ = trace.child();
    try {
      journal->name_ = name;
    } catch (const std::bad_alloc &) {
      return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::allocation});
    }
    auto opened = directory.create_exclusive(name);
    if (!opened.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(opened.error());
    }
    try {
      journal->file_ = std::move(opened).value();
    } catch (const std::bad_alloc &) {
      return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::allocation});
    }
    const auto locked = journal->file_->lock_writer();
    if (!locked.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(locked.error());
    }
    const auto size = journal->file_->extent();
    if (!size.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(size.error());
    }
    if (size.value() != 0) {
      return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::conflict});
    }
    const auto encoded = encode_journal_header(header);
    if (!encoded.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(encoded.error());
    }
    const auto written = journal->write_exact(0, encoded.value());
    if (!written.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(written.error());
    }
    const auto synchronized = journal->file_->synchronize(strength);
    if (!synchronized.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(synchronized.error());
    }
    const auto published = directory.synchronize_directory(strength);
    if (!published.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(published.error());
    }
    journal->state_ = JournalWriterState::live;
    return Result<std::unique_ptr<FramedJournal>>::success(std::move(journal));
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &journal = *answer.value();
    returned.cursor(journal.cursor_);
    returned.native(journal.state_);
    returned.native(journal.used_scan_checkpoint_);
    returned.native(journal.archived_records_);
  }
  trace.result(answer, returned.view());
  return answer;
}

Result<std::unique_ptr<FramedJournal>>
FramedJournal::open(JournalDirectory &directory, std::string_view name,
                    JournalHeader expected, JournalCapacity capacity,
                    SyncStrength strength, bool use_scan_checkpoint,
                    std::optional<JournalResume> resume, const ResumeSelector &selector,
                    const RawRetainedOrigin &raw_origin) {
  const std::uint8_t method = 2;
  RawRetainedFields arguments;
  arguments.header(expected);
  arguments.native(capacity.max_file_bytes);
  arguments.native(capacity.max_records);
  arguments.native(strength);
  arguments.native(method);
  const bool resumed = resume.has_value(), selector_present = bool(selector);
  arguments.native(use_scan_checkpoint);
  arguments.native(resumed);
  if (resume) {
    arguments.cursor(resume->cursor);
    arguments.native(resume->indexed_records);
    arguments.native(resume->commit_checksum);
  }
  arguments.native(selector_present);
  RawRetainedPair trace{raw_origin, {3166, 3167}, 278, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::unique_ptr<FramedJournal>> {
    auto allocated = allocate(directory, expected, capacity, strength, trace.child());
    if (!allocated.has_value()) {
      return allocated;
    }
    auto journal = std::move(allocated).value();
    journal->raw_origin_ = trace.child();
    try {
      journal->name_ = name;
    } catch (const std::bad_alloc &) {
      return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::allocation});
    }
    auto opened = directory.open_existing(name, FileAccess::read_write);
    if (!opened.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(opened.error());
    }
    try {
      journal->file_ = std::move(opened).value();
    } catch (const std::bad_alloc &) {
      return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::allocation});
    }
    const auto locked = journal->file_->lock_writer();
    if (!locked.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(locked.error());
    }
    const auto size = journal->file_->extent();
    if (!size.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(size.error());
    }
    if (size.value() < journal_header_size) {
      return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::incomplete});
    }
    if (size.value() > capacity.max_file_bytes) {
      return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::capacity});
    }
    journal->recovery_.available_end = size.value();
    std::array<std::byte, journal_header_size> bytes{};
    const auto read = journal->read_exact(0, bytes);
    if (!read.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(read.error());
    }
    const auto decoded = decode_journal_header(bytes);
    if (!decoded.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(decoded.error());
    }
    if (decoded.value().environment != expected.environment) {
      return Result<std::unique_ptr<FramedJournal>>::failure(
          {ErrorCode::wrong_environment});
    }
    if (decoded.value() != expected) {
      return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::conflict});
    }
    // Root and catalog selection observes the already-locked descriptor and the
    // checked header/extent. No unlocked preflight or second custodian is opened.
    if (selector) {
      if (resume || use_scan_checkpoint)
        return Result<std::unique_ptr<FramedJournal>>::failure(
            {ErrorCode::invalid_range});
      auto selected = selector(*journal);
      if (!selected.has_value())
        return Result<std::unique_ptr<FramedJournal>>::failure(selected.error());
      resume = std::move(selected).value();
    }
    if (resume) {
      constexpr std::size_t commit_size = journal_frame_header_size + 24;
      const auto &selected = *resume;
      if (use_scan_checkpoint || selected.cursor.journal != expected.journal ||
          !selected.cursor.sequence ||
          selected.cursor.end_offset < journal_header_size + commit_size ||
          selected.cursor.end_offset > size.value() ||
          selected.indexed_records > capacity.max_records)
        return Result<std::unique_ptr<FramedJournal>>::failure(
            {ErrorCode::invalid_range});
      std::array<std::byte, commit_size> anchor{};
      auto loaded =
          journal->read_exact(selected.cursor.end_offset - commit_size, anchor);
      if (!loaded.has_value())
        return Result<std::unique_ptr<FramedJournal>>::failure(loaded.error());
      auto commit = decode_journal_frame(anchor, expected.limits);
      if (crc32c(anchor) != selected.commit_checksum || !commit.has_value() ||
          commit.value().kind != FrameKind::commit ||
          commit.value().sequence != selected.cursor.sequence ||
          commit.value().payload.size() != 24)
        return Result<std::unique_ptr<FramedJournal>>::failure({ErrorCode::corrupt});
      journal->cursor_ = selected.cursor;
      journal->archived_records_ = selected.indexed_records;
    }
    if (use_scan_checkpoint) {
      auto a = journal->load_scan_checkpoint(0);
      auto b = journal->load_scan_checkpoint(1);
      if (!a.has_value() || !b.has_value())
        return Result<std::unique_ptr<FramedJournal>>::failure(
            !a.has_value() ? a.error() : b.error());
      auto selected = std::move(a).value();
      auto other = std::move(b).value();
      if (other && (!selected || other->cursor.sequence > selected->cursor.sequence))
        selected = std::move(other);
      if (selected) {
        journal->cursor_ = selected->cursor;
        journal->staged_.insert(journal->staged_.end(), selected->records.begin(),
                                selected->records.end());
        journal->used_scan_checkpoint_ = true;
      }
    }
    const auto scanned = journal->scan();
    if (!scanned.has_value()) {
      return Result<std::unique_ptr<FramedJournal>>::failure(scanned.error());
    }
    (void)directory.reclaim_publication_scratch(name); // exclusive writer lease held
    return Result<std::unique_ptr<FramedJournal>>::success(std::move(journal));
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &journal = *answer.value();
    returned.cursor(journal.cursor_);
    returned.native(journal.state_);
    returned.native(journal.used_scan_checkpoint_);
    returned.native(journal.archived_records_);
  }
  trace.result(answer, returned.view());
  return answer;
}

Result<void> FramedJournal::read_exact(std::uint64_t offset, MutableByteView output) {
  std::size_t read = 0;
  unsigned int interruptions = 0;
  while (read < output.size()) {
    const auto result = file_->read_at(offset + read, output.subspan(read));
    if (!result.has_value()) {
      if (result.error().code == ErrorCode::interrupted && ++interruptions < 8) {
        continue;
      }
      return Result<void>::failure(result.error());
    }
    if (result.value() == 0) {
      return Result<void>::failure({ErrorCode::incomplete});
    }
    if (result.value() > output.size() - read) {
      return Result<void>::failure({ErrorCode::io});
    }
    read += result.value();
    interruptions = 0;
  }
  return Result<void>::success();
}
Result<void> FramedJournal::write_exact(std::uint64_t offset, ByteView input) {
  RawStorageWriteContinuation continuation;
  const auto prepared = file_->begin_write_batch(input.size(), continuation);
  if (!prepared.has_value())
    return prepared;
  std::size_t written = 0;
  while (written < input.size()) {
    const auto result = file_->write_at_continuing(
        offset + written, input.subspan(written), continuation);
    if (!result.has_value()) {
      if (result.error().code == ErrorCode::interrupted) {
        continue;
      }
      return Result<void>::failure(result.error());
    }
    if (result.value() == 0 || result.value() > input.size() - written) {
      return Result<void>::failure({ErrorCode::io});
    }
    written += result.value();
  }
  return Result<void>::success();
}

Result<JournalCursor> FramedJournal::append(std::span<const JournalDraft> drafts) {
  const auto draft_count = drafts.size();
  RawRetainedFields arguments;
  arguments.native(draft_count);
  arguments.cursor(cursor_);
  arguments.native(state_);
  RawRetainedPair trace{raw_origin_, {3160, 3161}, 164, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<JournalCursor> {
    if (in_restage_) {
      return Result<JournalCursor>::failure({ErrorCode::busy});
    }
    if (state_ != JournalWriterState::live) {
      return Result<JournalCursor>::failure({ErrorCode::audit_unavailable});
    }
    if (drafts.empty()) {
      return Result<JournalCursor>::failure({ErrorCode::invalid_range});
    }
    if (drafts.size() > capacity_.max_records - archived_records_ - records_.size()) {
      return Result<JournalCursor>::failure({ErrorCode::capacity});
    }
    if (drafts.size() >= UINT64_MAX - cursor_.sequence) {
      return Result<JournalCursor>::failure({ErrorCode::overflow});
    }
    std::size_t size = 56;
    for (const auto &draft : drafts) {
      if (draft.kind != FrameKind::source && draft.kind != FrameKind::semantic) {
        return Result<JournalCursor>::failure({ErrorCode::invalid_range});
      }
      if (draft.payload.size() > header_.limits.max_payload ||
          draft.payload.size() >
              header_.limits.max_batch_bytes - journal_frame_header_size ||
          size > header_.limits.max_batch_bytes - journal_frame_header_size -
                     draft.payload.size()) {
        return Result<JournalCursor>::failure({ErrorCode::capacity});
      }
      size += journal_frame_header_size + draft.payload.size();
    }
    if (size > capacity_.max_file_bytes - cursor_.end_offset) {
      return Result<JournalCursor>::failure({ErrorCode::capacity});
    }
    try {
      std::vector<std::byte> encoded;
      std::vector<PhysicalJournalRecord> prepared;
      encoded.reserve(size);
      prepared.reserve(drafts.size());
      const auto first = cursor_.sequence + 1;
      for (const auto &draft : drafts) {
        RawRetainedEvent prepared_event{raw_origin_, {3162, 106}};
        const auto sequence = first + prepared.size();
        auto frame = encode_journal_frame(draft.kind, sequence, first, draft.payload,
                                          header_.limits);
        if (!frame.has_value()) {
          return Result<JournalCursor>::failure(frame.error());
        }
        prepared.push_back(
            {header_.environment, header_.journal, draft.kind, sequence, first,
             cursor_.end_offset + encoded.size() + journal_frame_header_size,
             static_cast<std::uint32_t>(draft.payload.size()),
             static_cast<std::uint32_t>(
                 get_little(ByteView{frame.value()}.subspan(28, 4)))});
        RawRetainedFields selected;
        selected.physical(prepared.back());
        prepared_event.publish(selected.view());
        encoded.insert(encoded.end(), frame.value().begin(), frame.value().end());
      }
      std::array<std::byte, 24> commit_payload{};
      put_little(std::span{commit_payload}.first(8), first);
      put_little(std::span{commit_payload}.subspan(8, 8), drafts.size());
      const auto checksum = crc32c(encoded);
      put_little(std::span{commit_payload}.subspan(16, 4), checksum);
      const auto encoded_size = encoded.size();
      RawRetainedFields commit_fields;
      commit_fields.native(first);
      commit_fields.native(draft_count);
      commit_fields.native(encoded_size);
      commit_fields.native(checksum);
      (void)raw_retained_event(raw_origin_, {3163, 68}, commit_fields.view());
      auto commit = encode_journal_frame(FrameKind::commit, first + drafts.size(),
                                         first, commit_payload, header_.limits);
      if (!commit.has_value()) {
        return Result<JournalCursor>::failure(commit.error());
      }
      encoded.insert(encoded.end(), commit.value().begin(), commit.value().end());
      const auto extent = file_->extent();
      if (!extent.has_value()) {
        state_ = JournalWriterState::blocked;
        return Result<JournalCursor>::failure(extent.error());
      }
      if (extent.value() != cursor_.end_offset) {
        state_ = JournalWriterState::blocked;
        recovery_ = {Error{ErrorCode::corrupt}, cursor_.end_offset, extent.value()};
        return Result<JournalCursor>::failure({ErrorCode::corrupt});
      }
      AppendAttempt attempt{state_};
      const auto written = write_exact(cursor_.end_offset, encoded);
      if (!written.has_value()) {
        state_ = JournalWriterState::poisoned;
        return Result<JournalCursor>::failure(written.error());
      }
      const auto synchronized = file_->synchronize(strength_);
      if (!synchronized.has_value()) {
        state_ = JournalWriterState::poisoned;
        return Result<JournalCursor>::failure(synchronized.error());
      }
      // All storage/index allocations preceded the first write. Reserved trivial
      // metadata publication cannot allocate or throw after this sync.
      for (const auto &record : prepared) {
        records_.push_back(record);
      }
      cursor_.sequence = first + drafts.size();
      cursor_.end_offset += encoded.size();
      recovery_ = {std::nullopt, cursor_.end_offset, cursor_.end_offset};
      attempt.complete();
      return Result<JournalCursor>::success(cursor_);
    } catch (const std::bad_alloc &) {
      return Result<JournalCursor>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  returned.cursor(cursor_);
  returned.native(state_);
  trace.result(answer, returned.view());
  return answer;
}

Result<void> FramedJournal::scan(bool propagate_read_errors) {
  RawRetainedFields arguments;
  arguments.cursor(cursor_);
  arguments.native(propagate_read_errors);
  arguments.native(in_restage_);
  RawRetainedPair trace{raw_origin_, {3168, 3169}, 183, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    try {
      auto offset = cursor_.end_offset;
      if (cursor_.sequence == UINT64_MAX) {
        if (offset != recovery_.available_end) {
          recovery_.problem = Error{ErrorCode::overflow};
          recovery_.diagnostic_offset = offset;
        }
        return Result<void>::success();
      }
      std::uint64_t expected_sequence = cursor_.sequence + 1;
      std::uint64_t batch_offset = offset;
      std::size_t batch_bytes = 0;
      std::uint32_t batch_checksum = 0;
      std::vector<std::byte> frame;
      frame.reserve(journal_frame_header_size + header_.limits.max_payload);
      auto failed = [&](Error error, std::uint64_t at) {
        recovery_.problem = error;
        recovery_.diagnostic_offset = at;
      };
      while (offset < recovery_.available_end) {
        if (recovery_.available_end - offset < journal_frame_header_size) {
          failed({ErrorCode::incomplete}, offset);
          break;
        }
        frame.resize(journal_frame_header_size);
        const auto read_header = read_exact(offset, frame);
        if (!read_header.has_value()) {
          failed(read_header.error(), offset);
          if (propagate_read_errors &&
              read_header.error().code != ErrorCode::incomplete) {
            return Result<void>::failure(read_header.error());
          }
          break;
        }
        const auto length = get_little(ByteView{frame}.subspan(24, 4));
        if (length > header_.limits.max_payload) {
          failed({ErrorCode::corrupt}, offset);
          break;
        }
        if (length > recovery_.available_end - offset - journal_frame_header_size) {
          failed({ErrorCode::incomplete}, offset);
          break;
        }
        frame.resize(journal_frame_header_size + static_cast<std::size_t>(length));
        const auto read_payload =
            read_exact(offset + journal_frame_header_size,
                       std::span{frame}.subspan(journal_frame_header_size));
        if (!read_payload.has_value()) {
          failed(read_payload.error(), offset);
          if (propagate_read_errors &&
              read_payload.error().code != ErrorCode::incomplete) {
            return Result<void>::failure(read_payload.error());
          }
          break;
        }
        const auto decoded = decode_journal_frame(frame, header_.limits);
        if (!decoded.has_value()) {
          failed(decoded.error(), offset);
          break;
        }
        const auto &view = decoded.value();
        const auto first =
            pending_.empty() ? expected_sequence : pending_.front().sequence;
        if (view.sequence != expected_sequence || view.batch_first != first ||
            view.encoded_size > header_.limits.max_batch_bytes - batch_bytes) {
          failed({ErrorCode::corrupt}, offset);
          break;
        }
        if (view.kind == FrameKind::commit) {
          if (pending_.empty() || view.payload.size() != 24 ||
              get_little(view.payload.first(8)) != first ||
              get_little(view.payload.subspan(8, 8)) != pending_.size() ||
              get_little(view.payload.subspan(16, 4)) != batch_checksum ||
              get_little(view.payload.subspan(20, 4)) != 0) {
            failed({ErrorCode::corrupt}, offset);
            break;
          }
          staged_.insert(staged_.end(), pending_.begin(), pending_.end());
          pending_.clear();
          cursor_ = {header_.journal, view.sequence, offset + view.encoded_size};
          batch_bytes = 0;
          batch_checksum = 0;
          batch_offset = cursor_.end_offset;
        } else {
          if (archived_records_ + staged_.size() + pending_.size() >=
              capacity_.max_records) {
            failed({ErrorCode::capacity}, offset);
            break;
          }
          RawRetainedEvent selected_frame{raw_origin_, {3162, 106}};
          pending_.push_back(
              {header_.environment, header_.journal, view.kind, view.sequence, first,
               offset + journal_frame_header_size, static_cast<std::uint32_t>(length),
               static_cast<std::uint32_t>(get_little(ByteView{frame}.subspan(28, 4)))});
          RawRetainedFields selected;
          selected.physical(pending_.back());
          selected_frame.publish(selected.view());
          batch_bytes += view.encoded_size;
          batch_checksum = crc32c(frame, batch_checksum);
        }
        offset += view.encoded_size;
        if (expected_sequence == UINT64_MAX) {
          if (offset != recovery_.available_end) {
            failed({ErrorCode::overflow}, offset);
          }
          break;
        }
        ++expected_sequence;
      }
      if (!recovery_.problem && !pending_.empty()) {
        failed({ErrorCode::incomplete}, batch_offset);
      }
      if (!recovery_.problem) {
        recovery_.diagnostic_offset = cursor_.end_offset;
      }
      return Result<void>::success();
    } catch (const std::bad_alloc &) {
      return Result<void>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  returned.cursor(cursor_);
  returned.native(state_);
  returned.native(recovery_.diagnostic_offset);
  returned.native(recovery_.available_end);
  const bool present = recovery_.problem.has_value();
  returned.native(present);
  if (recovery_.problem) {
    returned.native(recovery_.problem->code);
    returned.native(recovery_.problem->detail);
  }
  trace.result(answer, returned.view());
  return answer;
}

Result<void> FramedJournal::restage() {
  RawRetainedFields arguments;
  arguments.cursor(cursor_);
  const bool propagate_read_errors = true;
  arguments.native(propagate_read_errors);
  arguments.native(in_restage_);
  RawRetainedPair trace{raw_origin_, {3168, 3169}, 183, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    if (archived_records_) {
      // The selected-root owner must reopen/reselect root+tail. Do not lose the
      // archive accounting or compare a suffix vector to a full-history prefix.
      state_ = JournalWriterState::blocked;
      return Result<void>::failure({ErrorCode::unsupported});
    }
    if (in_restage_) {
      return Result<void>::failure({ErrorCode::busy});
    }
    RestageGuard guard{in_restage_};
    state_ = JournalWriterState::recovery_pending;
    staged_.clear();
    pending_.clear();
    cursor_ = {header_.journal, 0, journal_header_size};
    recovery_ = {std::nullopt, journal_header_size, journal_header_size};
    const auto refuse = [&](Error error) {
      state_ = JournalWriterState::blocked;
      recovery_.problem = error;
      return Result<void>::failure(error);
    };
    auto extent = file_->extent();
    unsigned int interruptions = 1;
    while (!extent.has_value() && extent.error().code == ErrorCode::interrupted &&
           interruptions < 8) {
      ++interruptions;
      extent = file_->extent();
    }
    if (!extent.has_value()) {
      return refuse(extent.error());
    }
    recovery_.available_end = extent.value();
    if (extent.value() < journal_header_size) {
      return refuse({ErrorCode::incomplete});
    }
    if (extent.value() > capacity_.max_file_bytes) {
      return refuse({ErrorCode::capacity});
    }
    std::array<std::byte, journal_header_size> bytes{};
    const auto read = read_exact(0, bytes);
    if (!read.has_value()) {
      return refuse(read.error());
    }
    const auto decoded = decode_journal_header(bytes);
    if (!decoded.has_value()) {
      return refuse(decoded.error());
    }
    if (decoded.value().environment != header_.environment) {
      return refuse({ErrorCode::wrong_environment});
    }
    if (decoded.value() != header_) {
      return refuse({ErrorCode::conflict});
    }
    const auto scanned = scan(true);
    if (!scanned.has_value()) {
      return refuse(scanned.error());
    }
    if (!records_.empty()) {
      const auto acknowledged = commit_boundary(records_.back());
      const auto selected = records_.size();
      if (staged_.size() < selected ||
          !std::equal(records_.begin(), records_.end(), staged_.begin()) ||
          (selected < staged_.size() &&
           staged_[selected].batch_first == staged_[selected - 1].batch_first)) {
        return refuse({ErrorCode::corrupt});
      }
      const auto actual = commit_boundary(staged_[selected - 1]);
      if (actual.sequence != acknowledged.sequence ||
          actual.end_offset != acknowledged.end_offset) {
        return refuse({ErrorCode::corrupt});
      }
    }
    return Result<void>::success();
  });
  RawRetainedFields returned;
  returned.cursor(cursor_);
  returned.native(state_);
  returned.native(recovery_.diagnostic_offset);
  returned.native(recovery_.available_end);
  const bool present = recovery_.problem.has_value();
  returned.native(present);
  if (recovery_.problem) {
    returned.native(recovery_.problem->code);
    returned.native(recovery_.problem->detail);
  }
  trace.result(answer, returned.view());
  return answer;
}

Result<void> FramedJournal::select_recovered_prefix(JournalPredecessor selected) {
  RawRetainedFields arguments;
  arguments.add(selected.journal.bytes());
  arguments.native(selected.validated_sequence);
  arguments.native(selected.validated_end_offset);
  RawRetainedPair trace{raw_origin_, {3170, 3171}, 155, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    if (in_restage_) {
      return Result<void>::failure({ErrorCode::busy});
    }
    if (state_ != JournalWriterState::recovery_pending) {
      return Result<void>::failure({ErrorCode::audit_unavailable});
    }
    if (selected.journal != header_.journal) {
      return Result<void>::failure({ErrorCode::conflict});
    }
    std::size_t count = 0;
    bool found = selected.validated_sequence == 0 &&
                 selected.validated_end_offset == journal_header_size;
    for (std::size_t index = 0; !found && index < staged_.size(); ++index) {
      if (index + 1 < staged_.size() &&
          staged_[index + 1].batch_first == staged_[index].batch_first) {
        continue;
      }
      const auto boundary = commit_boundary(staged_[index]);
      if (boundary.sequence == selected.validated_sequence &&
          boundary.end_offset == selected.validated_end_offset) {
        count = index + 1;
        found = true;
      }
    }
    if (!found) {
      return Result<void>::failure({ErrorCode::invalid_range});
    }
    const auto first = staged_.begin() + static_cast<std::ptrdiff_t>(count);
    pending_.insert(pending_.begin(), first, staged_.end());
    staged_.erase(first, staged_.end());
    cursor_ = {header_.journal, selected.validated_sequence,
               selected.validated_end_offset};
    if (!recovery_.problem) {
      recovery_.diagnostic_offset = cursor_.end_offset;
    }
    historical_ = true;
    return Result<void>::success();
  });
  RawRetainedFields returned;
  returned.cursor(cursor_);
  returned.native(state_);
  trace.result(answer, returned.view());
  return answer;
}

Result<void> FramedJournal::reject_recovery_batch(std::uint64_t batch_first,
                                                  Error reason) {
  RawRetainedFields arguments;
  arguments.native(batch_first);
  arguments.native(reason.code);
  arguments.native(reason.detail);
  RawRetainedPair trace{raw_origin_, {3172, 3173}, 156, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    if (in_restage_) {
      return Result<void>::failure({ErrorCode::busy});
    }
    if (state_ != JournalWriterState::recovery_pending) {
      return Result<void>::failure({ErrorCode::audit_unavailable});
    }
    const auto first =
        std::find_if(staged_.begin(), staged_.end(), [&](const auto &record) {
          return record.sequence == batch_first && record.batch_first == batch_first;
        });
    if (first == staged_.end()) {
      return Result<void>::failure({ErrorCode::invalid_range});
    }
    const auto offset = first->payload_offset - journal_frame_header_size;
    cursor_ = {header_.journal, batch_first - 1, offset};
    recovery_.problem = reason;
    recovery_.diagnostic_offset = offset;
    // Combined staged/pending metadata is bounded by max_records and both vectors
    // were reserved before file I/O. This move of trivial metadata cannot allocate.
    pending_.insert(pending_.begin(), first, staged_.end());
    staged_.erase(first, staged_.end());
    return Result<void>::success();
  });
  RawRetainedFields returned;
  returned.cursor(cursor_);
  returned.native(state_);
  returned.native(recovery_.diagnostic_offset);
  returned.native(recovery_.available_end);
  trace.result(answer, returned.view());
  return answer;
}

Result<void> FramedJournal::confirm_recovery() {
  const std::uint8_t method = 1;
  RawRetainedFields arguments;
  arguments.cursor(cursor_);
  arguments.native(state_);
  arguments.native(method);
  RawRetainedPair trace{raw_origin_, {3178, 3179}, 183, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    if (in_restage_) {
      return Result<void>::failure({ErrorCode::busy});
    }
    if (state_ != JournalWriterState::recovery_pending) {
      return Result<void>::failure({ErrorCode::audit_unavailable});
    }
    const auto synchronized = file_->synchronize(strength_);
    if (!synchronized.has_value()) {
      state_ = JournalWriterState::poisoned;
      return Result<void>::failure(synchronized.error());
    }
    const auto directory_sync = directory_.synchronize_directory(strength_);
    if (!directory_sync.has_value()) {
      state_ = JournalWriterState::poisoned;
      return Result<void>::failure(directory_sync.error());
    }
    records_.swap(staged_);
    staged_.clear();
    state_ = recovery_.clean() && !historical_ ? JournalWriterState::recovered
                                               : JournalWriterState::blocked;
    return Result<void>::success();
  });
  RawRetainedFields returned;
  returned.cursor(cursor_);
  returned.native(state_);
  returned.native(recovery_.diagnostic_offset);
  returned.native(recovery_.available_end);
  const bool present = recovery_.problem.has_value();
  returned.native(present);
  if (recovery_.problem) {
    returned.native(recovery_.problem->code);
    returned.native(recovery_.problem->detail);
  }
  trace.result(answer, returned.view());
  return answer;
}
Result<void> FramedJournal::resume_after_reconciliation() {
  const std::uint8_t method = 2;
  RawRetainedFields arguments;
  arguments.cursor(cursor_);
  arguments.native(state_);
  arguments.native(method);
  RawRetainedPair trace{raw_origin_, {3178, 3179}, 183, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    if (in_restage_) {
      return Result<void>::failure({ErrorCode::busy});
    }
    if (state_ != JournalWriterState::recovered || !recovery_.clean() || historical_) {
      return Result<void>::failure({ErrorCode::audit_unavailable});
    }
    state_ = JournalWriterState::live;
    return Result<void>::success();
  });
  RawRetainedFields returned;
  returned.cursor(cursor_);
  returned.native(state_);
  returned.native(recovery_.diagnostic_offset);
  returned.native(recovery_.available_end);
  const bool present = recovery_.problem.has_value();
  returned.native(present);
  if (recovery_.problem) {
    returned.native(recovery_.problem->code);
    returned.native(recovery_.problem->detail);
  }
  trace.result(answer, returned.view());
  return answer;
}
Result<std::vector<std::byte>>
FramedJournal::read_payload(const PhysicalJournalRecord &record) {
  if (in_restage_) {
    return Result<std::vector<std::byte>>::failure({ErrorCode::busy});
  }
  const auto contains = [&](const auto &records) {
    // Sequence order is preserved by append, scan and suffix recovery moves.
    // Still compare the complete handle: sequence alone cannot authorize reads.
    const auto found =
        std::lower_bound(records.begin(), records.end(), record.sequence,
                         [](const auto &candidate, std::uint64_t sequence) {
                           return candidate.sequence < sequence;
                         });
    return found != records.end() && *found == record;
  };
  if (!contains(records_) && !contains(staged_) && !contains(pending_)) {
    return Result<std::vector<std::byte>>::failure({ErrorCode::stale_handle});
  }
  return read_checked_payload(record);
}
Result<std::vector<std::byte>>
FramedJournal::read_catalog_payload(const PhysicalJournalRecord &record,
                                    JournalCursor boundary) {
  if (in_restage_)
    return Result<std::vector<std::byte>>::failure({ErrorCode::busy});
  if (boundary.journal != header_.journal ||
      record.environment != header_.environment || record.journal != header_.journal ||
      !record.sequence || record.sequence >= boundary.sequence || !record.batch_first ||
      record.batch_first > record.sequence ||
      record.payload_offset < journal_header_size + journal_frame_header_size ||
      record.payload_offset > boundary.end_offset ||
      record.payload_size > boundary.end_offset - record.payload_offset ||
      record.payload_size > header_.limits.max_payload ||
      (record.kind != FrameKind::source && record.kind != FrameKind::semantic))
    return Result<std::vector<std::byte>>::failure({ErrorCode::corrupt});
  return read_checked_payload(record);
}
Result<std::vector<std::byte>>
FramedJournal::read_checked_payload(const PhysicalJournalRecord &record) {
  const std::uint8_t method = 1;
  RawRetainedFields arguments;
  arguments.physical(record);
  arguments.cursor(cursor_);
  arguments.native(method);
  RawRetainedPair trace{raw_origin_, {3164, 3165}, 198, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::vector<std::byte>> {
    try {
      const auto frame_offset = record.payload_offset - journal_frame_header_size;
      std::vector<std::byte> bytes(journal_frame_header_size + record.payload_size);
      const auto read = read_exact(frame_offset, bytes);
      if (!read.has_value()) {
        if (state_ != JournalWriterState::poisoned) {
          state_ = JournalWriterState::blocked;
        }
        recovery_.problem = read.error();
        recovery_.diagnostic_offset = frame_offset;
        return Result<std::vector<std::byte>>::failure(read.error());
      }
      const auto decoded = decode_journal_frame(bytes, header_.limits);
      if (!decoded.has_value() || decoded.value().kind != record.kind ||
          decoded.value().sequence != record.sequence ||
          decoded.value().batch_first != record.batch_first ||
          decoded.value().payload.size() != record.payload_size ||
          get_little(ByteView{bytes}.subspan(28, 4)) != record.frame_checksum) {
        if (state_ != JournalWriterState::poisoned) {
          state_ = JournalWriterState::blocked;
        }
        recovery_.problem = Error{ErrorCode::corrupt};
        recovery_.diagnostic_offset = frame_offset;
        return Result<std::vector<std::byte>>::failure({ErrorCode::corrupt});
      }
      bytes.erase(bytes.begin(), bytes.begin() + journal_frame_header_size);
      return Result<std::vector<std::byte>>::success(std::move(bytes));
    } catch (const std::bad_alloc &) {
      return Result<std::vector<std::byte>>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto size = answer.value().size();
    returned.native(size);
    returned.native(state_);
    trace.result(answer, returned.view());
  } else {
    returned.native(state_);
    trace.finish(1, returned.view(), answer.error());
  }
  return answer;
}

FramedJournal::ArchiveReader
FramedJournal::archive_reader(JournalCursor boundary) const {
  RawRetainedFields arguments;
  arguments.cursor(boundary);
  RawRetainedPair trace{raw_origin_, {3174, 3175}, 124, arguments.view()};
  auto answer = trace.run([&]() -> ArchiveReader {
    return [file = file_, header = header_, origin = raw_reader_origin_,
            boundary](const PhysicalJournalRecord &record) {
      const auto selected = origin->snapshot();
      const std::uint8_t read_method = 2;
      RawRetainedFields read_arguments;
      read_arguments.physical(record);
      read_arguments.cursor(boundary);
      read_arguments.native(read_method);
      RawRetainedPair read_trace{selected, {3164, 3165}, 198, read_arguments.view()};
      auto read_answer = read_trace.run([&]() -> Result<std::vector<std::byte>> {
        if (boundary.journal != header.journal ||
            record.environment != header.environment ||
            record.journal != header.journal || !record.sequence ||
            record.sequence >= boundary.sequence || !record.batch_first ||
            record.batch_first > record.sequence ||
            record.payload_offset < journal_header_size + journal_frame_header_size ||
            record.payload_offset > boundary.end_offset ||
            record.payload_size > boundary.end_offset - record.payload_offset ||
            record.payload_size > header.limits.max_payload ||
            (record.kind != FrameKind::source && record.kind != FrameKind::semantic))
          return Result<std::vector<std::byte>>::failure({ErrorCode::corrupt});
        const auto exact = [&](std::uint64_t offset,
                               MutableByteView output) -> Result<void> {
          std::size_t done = 0;
          unsigned interruptions = 0;
          while (done < output.size()) {
            if (offset > UINT64_MAX - done)
              return Result<void>::failure({ErrorCode::invalid_range});
            auto read = file->read_at(offset + done, output.subspan(done));
            if (!read.has_value()) {
              if (read.error().code == ErrorCode::interrupted && ++interruptions < 8)
                continue;
              return Result<void>::failure(read.error());
            }
            if (read.value() == 0 || read.value() > output.size() - done)
              return Result<void>::failure({ErrorCode::corrupt});
            done += read.value();
          }
          return Result<void>::success();
        };
        try {
          std::array<std::byte, journal_header_size> root{};
          auto got = exact(0, root);
          if (!got.has_value())
            return Result<std::vector<std::byte>>::failure(got.error());
          auto identity = decode_journal_header(root);
          if (!identity.has_value() || identity.value() != header)
            return Result<std::vector<std::byte>>::failure({ErrorCode::corrupt});
          std::vector<std::byte> bytes(journal_frame_header_size + record.payload_size);
          got = exact(record.payload_offset - journal_frame_header_size, bytes);
          if (!got.has_value())
            return Result<std::vector<std::byte>>::failure(got.error());
          auto frame = decode_journal_frame(bytes, header.limits);
          if (!frame.has_value() || frame.value().kind != record.kind ||
              frame.value().sequence != record.sequence ||
              frame.value().batch_first != record.batch_first ||
              frame.value().payload.size() != record.payload_size ||
              get_little(ByteView{bytes}.subspan(28, 4)) != record.frame_checksum)
            return Result<std::vector<std::byte>>::failure({ErrorCode::corrupt});
          bytes.erase(bytes.begin(), bytes.begin() + journal_frame_header_size);
          return Result<std::vector<std::byte>>::success(std::move(bytes));
        } catch (const std::bad_alloc &) {
          return Result<std::vector<std::byte>>::failure({ErrorCode::allocation});
        }
      });
      RawRetainedFields read_returned;
      if (read_answer.has_value()) {
        const auto read_size = read_answer.value().size();
        read_returned.native(read_size);
        read_trace.result(read_answer, read_returned.view());
      } else
        read_trace.result(read_answer);
      return read_answer;
    };
  });
  const bool created = static_cast<bool>(answer);
  RawRetainedFields returned;
  returned.native(created);
  returned.native(state_);
  trace.finish(0, returned.view());
  return answer;
}

Result<FramedJournal::PayloadReader>
FramedJournal::payload_reader(const PhysicalJournalRecord &record) {
  const std::uint8_t method = 1;
  RawRetainedFields arguments;
  arguments.physical(record);
  arguments.cursor(cursor_);
  arguments.native(method);
  RawRetainedPair trace{raw_origin_, {3174, 3175}, 190, arguments.view()};
  auto answer = trace.run([&]() -> Result<PayloadReader> {
    const auto contains = [&](const auto &records) {
      const auto found =
          std::lower_bound(records.begin(), records.end(), record.sequence,
                           [](const auto &candidate, std::uint64_t sequence) {
                             return candidate.sequence < sequence;
                           });
      return found != records.end() && *found == record;
    };
    if (in_restage_ || (!contains(records_) && !contains(staged_)))
      return Result<PayloadReader>::failure({ErrorCode::stale_handle});
    if (record.environment != header_.environment ||
        record.journal != header_.journal ||
        record.payload_offset < journal_header_size + journal_frame_header_size ||
        record.payload_size > header_.limits.max_payload)
      return Result<PayloadReader>::failure({ErrorCode::corrupt});
    try {
      return Result<PayloadReader>::success([file = file_, header = header_, record,
                                             origin = raw_reader_origin_,
                                             boundary = cursor_]() {
        const auto selected = origin->snapshot();
        const std::uint8_t read_method = 3;
        RawRetainedFields read_arguments;
        read_arguments.physical(record);
        read_arguments.cursor(boundary);
        read_arguments.native(read_method);
        RawRetainedPair read_trace{selected, {3164, 3165}, 198, read_arguments.view()};
        auto read_answer = read_trace.run([&]() -> Result<std::vector<std::byte>> {
          const auto exact = [&](std::uint64_t offset,
                                 MutableByteView output) -> Result<void> {
            std::size_t done = 0;
            unsigned interruptions = 0;
            while (done < output.size()) {
              if (offset > UINT64_MAX - done)
                return Result<void>::failure({ErrorCode::invalid_range});
              auto read = file->read_at(offset + done, output.subspan(done));
              if (!read.has_value()) {
                if (read.error().code == ErrorCode::interrupted && ++interruptions < 8)
                  continue;
                return Result<void>::failure(read.error());
              }
              if (read.value() == 0 || read.value() > output.size() - done)
                return Result<void>::failure({ErrorCode::corrupt});
              done += read.value();
            }
            return Result<void>::success();
          };
          try {
            std::array<std::byte, journal_header_size> root{};
            auto got = exact(0, root);
            if (!got.has_value())
              return Result<std::vector<std::byte>>::failure(got.error());
            auto identity = decode_journal_header(root);
            if (!identity.has_value() || identity.value() != header)
              return Result<std::vector<std::byte>>::failure({ErrorCode::corrupt});
            std::vector<std::byte> bytes(journal_frame_header_size +
                                         record.payload_size);
            got = exact(record.payload_offset - journal_frame_header_size, bytes);
            if (!got.has_value())
              return Result<std::vector<std::byte>>::failure(got.error());
            auto frame = decode_journal_frame(bytes, header.limits);
            if (!frame.has_value() || frame.value().kind != record.kind ||
                frame.value().sequence != record.sequence ||
                frame.value().batch_first != record.batch_first ||
                frame.value().payload.size() != record.payload_size ||
                get_little(ByteView{bytes}.subspan(28, 4)) != record.frame_checksum)
              return Result<std::vector<std::byte>>::failure({ErrorCode::corrupt});
            bytes.erase(bytes.begin(), bytes.begin() + journal_frame_header_size);
            return Result<std::vector<std::byte>>::success(std::move(bytes));
          } catch (const std::bad_alloc &) {
            return Result<std::vector<std::byte>>::failure({ErrorCode::allocation});
          }
        });
        RawRetainedFields read_returned;
        if (read_answer.has_value()) {
          const auto read_size = read_answer.value().size();
          read_returned.native(read_size);
          read_trace.result(read_answer, read_returned.view());
        } else
          read_trace.result(read_answer);
        return read_answer;
      });
    } catch (const std::bad_alloc &) {
      return Result<PayloadReader>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const bool created = static_cast<bool>(answer.value());
    returned.native(created);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}

Result<ObservedJournalBytes>
FramedJournal::read_original_range(std::uint64_t offset, std::size_t length,
                                   const RawRetainedOrigin *origin) {
  RawRetainedFields arguments;
  arguments.native(offset);
  arguments.native(length);
  RawRetainedPair trace{
      origin ? *origin : raw_origin_, {3176, 3177}, 122, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<ObservedJournalBytes> {
    if (length > header_.limits.max_payload) {
      return Result<ObservedJournalBytes>::failure({ErrorCode::invalid_range});
    }
    auto size = file_->extent();
    for (unsigned int count = 1;
         !size.has_value() && size.error().code == ErrorCode::interrupted && count < 8;
         ++count) {
      size = file_->extent();
    }
    if (!size.has_value()) {
      return Result<ObservedJournalBytes>::failure(size.error());
    }
    if (offset > size.value() || length > size.value() - offset) {
      return Result<ObservedJournalBytes>::failure({ErrorCode::invalid_range});
    }
    try {
      ObservedJournalBytes observed{header_.journal, offset, size.value(),
                                    std::vector<std::byte>(length)};
      std::size_t done = 0;
      unsigned int interruptions = 0;
      while (done < length) {
        const auto read = file_->read_at(offset + done,
                                         MutableByteView{observed.bytes}.subspan(done));
        if (!read.has_value()) {
          if (read.error().code == ErrorCode::interrupted && ++interruptions < 8) {
            continue;
          }
          return Result<ObservedJournalBytes>::failure(read.error());
        }
        if (read.value() > length - done) {
          return Result<ObservedJournalBytes>::failure({ErrorCode::io});
        }
        if (read.value() == 0) {
          return Result<ObservedJournalBytes>::failure({ErrorCode::incomplete});
        }
        done += read.value();
        interruptions = 0;
      }
      return Result<ObservedJournalBytes>::success(std::move(observed));
    } catch (const std::bad_alloc &) {
      return Result<ObservedJournalBytes>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto count = answer.value().bytes.size();
    returned.native(answer.value().observed_extent);
    returned.native(count);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}

} // namespace blackbird
