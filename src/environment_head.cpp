#include "blackbird/environment_head.hpp"

#include <algorithm>
#include <cerrno>
#include <string_view>

namespace blackbird {
namespace {
constexpr std::string_view magic = "ARCHEAD1";
template <std::size_t Width, std::size_t Offset>
void put(MutableByteView out, std::uint64_t value) noexcept {
  for (std::size_t i = 0; i < Width; ++i)
    out[Offset + i] = static_cast<std::byte>((value >> (i * 8)) & 255);
}
template <std::size_t Width, std::size_t Offset>
std::uint64_t get(ByteView in) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < Width; ++i)
    value |= static_cast<std::uint64_t>(in[Offset + i]) << (i * 8);
  return value;
}
bool valid_strength(SyncStrength strength) noexcept {
  return strength == SyncStrength::data || strength == SyncStrength::full;
}
bool valid_selector(const HeadSelector &selector) noexcept {
  return selector.generation != 0 &&
         ((selector.generation == 1) == (selector.active == selector.root));
}
template <typename Call> auto interrupted(Call call) {
  for (unsigned int n = 0;; ++n) {
    auto result = call();
    if (result.has_value() || result.error().code != ErrorCode::interrupted || n == 7)
      return result;
  }
}
Result<void> write_exact(JournalFile &file, ByteView bytes) {
  std::size_t done = 0;
  while (done < bytes.size()) {
    const auto result =
        interrupted([&] { return file.write_at(done, bytes.subspan(done)); });
    if (!result.has_value())
      return Result<void>::failure(result.error());
    if (result.value() > bytes.size() - done)
      return Result<void>::failure({ErrorCode::corrupt});
    if (result.value() == 0)
      return Result<void>::failure({ErrorCode::incomplete});
    done += result.value();
  }
  return Result<void>::success();
}
Result<void> read_exact(JournalFile &file, MutableByteView bytes) {
  const auto size = interrupted([&] { return file.extent(); });
  if (!size.has_value())
    return Result<void>::failure(size.error());
  if (size.value() != bytes.size())
    return Result<void>::failure({ErrorCode::corrupt});
  std::size_t done = 0;
  while (done < bytes.size()) {
    const auto result =
        interrupted([&] { return file.read_at(done, bytes.subspan(done)); });
    if (!result.has_value())
      return Result<void>::failure(result.error());
    if (result.value() > bytes.size() - done)
      return Result<void>::failure({ErrorCode::corrupt});
    if (result.value() == 0)
      return Result<void>::failure({ErrorCode::incomplete});
    done += result.value();
  }
  return Result<void>::success();
}
std::array<char, 37> temporary_name(AuditStreamId journal) noexcept {
  std::array<char, 37> name{};
  constexpr std::string_view prefix = "head.";
  constexpr std::string_view digits = "0123456789abcdef";
  std::copy(prefix.begin(), prefix.end(), name.begin());
  for (std::size_t i = 0; i < 16; ++i) {
    const auto value = std::to_integer<unsigned int>(journal.bytes()[i]);
    name[5 + i * 2] = digits[value >> 4];
    name[6 + i * 2] = digits[value & 15];
  }
  return name;
}
struct Transaction {
  bool &busy;
  bool &healthy;
  bool attempted = false;
  bool published = false;
  ~Transaction() {
    if (attempted && !published)
      healthy = false;
    busy = false;
  }
};
} // namespace
Result<std::array<std::byte, head_selector_size>>
encode_head_selector(const HeadSelector &selector) {
  if (!valid_selector(selector))
    return Result<std::array<std::byte, head_selector_size>>::failure(
        {ErrorCode::invalid_range});
  std::array<std::byte, head_selector_size> bytes{};
  for (std::size_t i = 0; i < magic.size(); ++i)
    bytes[i] = static_cast<std::byte>(magic[i]);
  std::copy(selector.environment.bytes().begin(), selector.environment.bytes().end(),
            bytes.begin() + 8);
  std::copy(selector.root.bytes().begin(), selector.root.bytes().end(),
            bytes.begin() + 24);
  std::copy(selector.active.bytes().begin(), selector.active.bytes().end(),
            bytes.begin() + 40);
  put<8, 56>(bytes, selector.generation);
  put<4, 68>(bytes, crc32c(ByteView{bytes}.first(68)));
  return Result<std::array<std::byte, head_selector_size>>::success(bytes);
}
Result<HeadSelector> decode_head_selector(ByteView bytes) {
  if (bytes.size() != head_selector_size || get<4, 64>(bytes) != 0 ||
      get<4, 68>(bytes) != crc32c(bytes.first(68)))
    return Result<HeadSelector>::failure({ErrorCode::corrupt});
  for (std::size_t i = 0; i < magic.size(); ++i)
    if (bytes[i] != static_cast<std::byte>(magic[i]))
      return Result<HeadSelector>::failure({ErrorCode::corrupt});
  IdentityBytes raw_environment{}, raw_root{}, raw_active{};
  std::copy_n(bytes.begin() + 8, 16, raw_environment.begin());
  std::copy_n(bytes.begin() + 24, 16, raw_root.begin());
  std::copy_n(bytes.begin() + 40, 16, raw_active.begin());
  auto environment = EnvironmentId::from_bytes(raw_environment);
  auto root = AuditStreamId::from_bytes(raw_root);
  auto active = AuditStreamId::from_bytes(raw_active);
  if (!environment.has_value() || !root.has_value() || !active.has_value())
    return Result<HeadSelector>::failure({ErrorCode::corrupt});
  const HeadSelector selector{environment.value(), root.value(), active.value(),
                              get<8, 56>(bytes)};
  if (!valid_selector(selector))
    return Result<HeadSelector>::failure({ErrorCode::corrupt});
  return Result<HeadSelector>::success(selector);
}
Result<std::pair<std::unique_ptr<JournalFile>, HeadSelector>>
EnvironmentHead::read_selector() {
  RawRetainedFields arguments;
  arguments.add(root_.environment.bytes());
  arguments.add(root_.journal.bytes());
  RawRetainedPair trace{raw_origin_, {3246, 3247}, 178, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer =
      trace.run([&]() -> Result<std::pair<std::unique_ptr<JournalFile>, HeadSelector>> {
        auto opened = directory_->open_existing("head", FileAccess::read_write);
        if (!opened.has_value())
          return Result<std::pair<std::unique_ptr<JournalFile>, HeadSelector>>::failure(
              opened.error());
        auto file = std::move(opened).value();
        std::array<std::byte, head_selector_size> bytes{};
        const auto read = read_exact(*file, bytes);
        if (!read.has_value())
          return Result<std::pair<std::unique_ptr<JournalFile>, HeadSelector>>::failure(
              read.error());
        const auto decoded = decode_head_selector(bytes);
        if (!decoded.has_value())
          return Result<std::pair<std::unique_ptr<JournalFile>, HeadSelector>>::failure(
              decoded.error());
        if (decoded.value().environment != root_.environment)
          return Result<std::pair<std::unique_ptr<JournalFile>, HeadSelector>>::failure(
              {ErrorCode::wrong_environment});
        if (decoded.value().root != root_.journal)
          return Result<std::pair<std::unique_ptr<JournalFile>, HeadSelector>>::failure(
              {ErrorCode::conflict});
        return Result<std::pair<std::unique_ptr<JournalFile>, HeadSelector>>::success(
            {std::move(file), decoded.value()});
      });
  RawRetainedFields returned;
  if (answer.has_value())
    returned.selector(answer.value().second);
  trace.result(answer, returned.view());
  return answer;
}
Result<void> EnvironmentHead::write_selector(const HeadSelector &next,
                                             SyncStrength strength, bool &attempted) {
  RawRetainedFields arguments;
  arguments.selector(next);
  arguments.native(strength);
  RawRetainedPair trace{raw_origin_, {3250, 3251}, 148, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    const auto encoded = encode_head_selector(next);
    if (!encoded.has_value())
      return Result<void>::failure(encoded.error());
    const auto name = temporary_name(next.active);
    const std::string_view temporary{name.data(), name.size()};
    auto opened = directory_->create_exclusive(temporary);
    if (!opened.has_value())
      return Result<void>::failure(opened.error());
    auto file = std::move(opened).value();
    attempted = true;
    const auto wrote = write_exact(*file, encoded.value());
    if (!wrote.has_value())
      return wrote;
    const auto synced = interrupted([&] { return file->synchronize(strength); });
    if (!synced.has_value())
      return synced;
    const auto renamed = directory_->replace_file(temporary, "head");
    if (!renamed.has_value())
      return renamed;
    return interrupted([&] { return directory_->synchronize_directory(strength); });
  });
  RawRetainedFields returned;
  returned.native(attempted);
  trace.result(answer, returned.view());
  return answer;
}
Result<std::unique_ptr<EnvironmentHead>>
EnvironmentHead::prepare(std::unique_ptr<JournalDirectory> directory,
                         const JournalHeader &root, SyncStrength strength,
                         const RawRetainedOrigin &raw_origin) {
  RawRetainedFields arguments;
  arguments.header(root);
  arguments.native(strength);
  RawRetainedPair trace{raw_origin, {3240, 3241}, 231, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::unique_ptr<EnvironmentHead>> {
    if (!directory || root.predecessor || !valid_strength(strength))
      return Result<std::unique_ptr<EnvironmentHead>>::failure(
          {ErrorCode::invalid_range});
    const auto encoded = encode_journal_header(root);
    if (!encoded.has_value())
      return Result<std::unique_ptr<EnvironmentHead>>::failure(encoded.error());
    try {
      std::unique_ptr<EnvironmentHead> owner{
          new EnvironmentHead{std::move(directory), root, trace.child()}};
      auto opened = owner->directory_->create_exclusive("authority");
      if (!opened.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(opened.error());
      owner->authority_ = std::move(opened).value();
      const auto locked = interrupted([&] { return owner->authority_->lock_writer(); });
      if (!locked.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(locked.error());
      const auto existing_head =
          owner->directory_->open_existing("head", FileAccess::read_only);
      if (existing_head.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure({ErrorCode::conflict});
      if (existing_head.error() != Error{ErrorCode::io, ENOENT})
        return Result<std::unique_ptr<EnvironmentHead>>::failure(existing_head.error());
      const auto wrote = write_exact(*owner->authority_, encoded.value());
      if (!wrote.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(wrote.error());
      const auto synced =
          interrupted([&] { return owner->authority_->synchronize(strength); });
      if (!synced.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(synced.error());
      owner->initial_prepared_ = true;
      return Result<std::unique_ptr<EnvironmentHead>>::success(std::move(owner));
    } catch (const std::bad_alloc &) {
      return Result<std::unique_ptr<EnvironmentHead>>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    returned.selector(answer.value()->selection_);
    returned.native(answer.value()->healthy_);
    returned.native(answer.value()->initial_prepared_);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<std::unique_ptr<EnvironmentHead>>
EnvironmentHead::create(std::unique_ptr<JournalDirectory> directory,
                        const JournalHeader &root, SyncStrength strength,
                        const RawRetainedOrigin &raw_origin) {
  auto staged = prepare(std::move(directory), root, strength, raw_origin);
  if (!staged.has_value())
    return staged;
  auto owner = std::move(staged).value();
  const auto published = owner->publish_initial(strength);
  if (!published.has_value())
    return Result<std::unique_ptr<EnvironmentHead>>::failure(published.error());
  return Result<std::unique_ptr<EnvironmentHead>>::success(std::move(owner));
}
Result<void> EnvironmentHead::publish_initial(SyncStrength strength) {
  RawRetainedFields arguments;
  arguments.selector(selection_);
  arguments.native(strength);
  RawRetainedPair trace{raw_origin_, {3242, 3243}, 206, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedEvent transaction_begin{trace.child(), {3248, 101}};
  RawRetainedEvent transaction_end{trace.child(), {3248, 101}};
  auto answer = trace.run([&]() -> Result<void> {
    if (in_transaction_)
      return Result<void>::failure({ErrorCode::busy});
    if (!initial_prepared_ || healthy_)
      return Result<void>::failure({ErrorCode::audit_unavailable});
    if (!valid_strength(strength))
      return Result<void>::failure({ErrorCode::invalid_range});
    initial_prepared_ = false;
    Transaction transaction{in_transaction_, healthy_};
    RawRetainedFinally final_transaction{[&]() noexcept {
      RawRetainedFields final;
      final.native(healthy_);
      final.native(initial_prepared_);
      final.native(transaction.attempted);
      final.native(transaction.published);
      final.native(in_transaction_);
      final.selector(selection_);
      transaction_end.publish(final.view());
    }};
    in_transaction_ = true;
    try {
      const auto existing_head =
          directory_->open_existing("head", FileAccess::read_only);
      if (existing_head.has_value())
        return Result<void>::failure({ErrorCode::conflict});
      if (existing_head.error() != Error{ErrorCode::io, ENOENT})
        return Result<void>::failure(existing_head.error());
      RawRetainedFields before;
      before.native(healthy_);
      before.native(initial_prepared_);
      before.native(transaction.attempted);
      before.native(transaction.published);
      before.native(in_transaction_);
      before.selector(selection_);
      transaction_begin.publish(before.view());
      const auto published =
          write_selector(selection_, strength, transaction.attempted);
      if (!published.has_value())
        return published;
      healthy_ = true;
      transaction.published = true;
      return Result<void>::success();
    } catch (const std::bad_alloc &) {
      return Result<void>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  returned.selector(selection_);
  returned.native(healthy_);
  returned.native(initial_prepared_);
  trace.result(answer, returned.view());
  return answer;
}
Result<std::unique_ptr<EnvironmentHead>>
EnvironmentHead::open(std::unique_ptr<JournalDirectory> directory,
                      const JournalHeader &root, SyncStrength strength,
                      const RawRetainedOrigin &raw_origin) {
  RawRetainedFields arguments;
  arguments.header(root);
  arguments.native(strength);
  RawRetainedPair trace{raw_origin, {3240, 3241}, 231, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::unique_ptr<EnvironmentHead>> {
    if (!directory || root.predecessor || !valid_strength(strength))
      return Result<std::unique_ptr<EnvironmentHead>>::failure(
          {ErrorCode::invalid_range});
    const auto encoded = encode_journal_header(root);
    if (!encoded.has_value())
      return Result<std::unique_ptr<EnvironmentHead>>::failure(encoded.error());
    try {
      std::unique_ptr<EnvironmentHead> owner{
          new EnvironmentHead{std::move(directory), root, trace.child()}};
      auto opened =
          owner->directory_->open_existing("authority", FileAccess::read_write);
      if (!opened.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(opened.error());
      owner->authority_ = std::move(opened).value();
      const auto locked = interrupted([&] { return owner->authority_->lock_writer(); });
      if (!locked.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(locked.error());
      std::array<std::byte, journal_header_size> bytes{};
      const auto read = read_exact(*owner->authority_, bytes);
      if (!read.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(read.error());
      const auto decoded = decode_journal_header(bytes);
      if (!decoded.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(decoded.error());
      if (decoded.value().environment != root.environment)
        return Result<std::unique_ptr<EnvironmentHead>>::failure(
            {ErrorCode::wrong_environment});
      if (decoded.value() != root)
        return Result<std::unique_ptr<EnvironmentHead>>::failure({ErrorCode::conflict});
      auto selected = owner->read_selector();
      if (!selected.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(selected.error());
      auto [file, selector] = std::move(selected).value();
      const auto synced =
          interrupted([&] { return owner->authority_->synchronize(strength); });
      if (!synced.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(synced.error());
      const auto head_synced = interrupted([&] { return file->synchronize(strength); });
      if (!head_synced.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(head_synced.error());
      const auto directory_synced = interrupted(
          [&] { return owner->directory_->synchronize_directory(strength); });
      if (!directory_synced.has_value())
        return Result<std::unique_ptr<EnvironmentHead>>::failure(
            directory_synced.error());
      owner->selection_ = selector;
      owner->healthy_ = true;
      return Result<std::unique_ptr<EnvironmentHead>>::success(std::move(owner));
    } catch (const std::bad_alloc &) {
      return Result<std::unique_ptr<EnvironmentHead>>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    returned.selector(answer.value()->selection_);
    returned.native(answer.value()->healthy_);
    returned.native(answer.value()->initial_prepared_);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<void> EnvironmentHead::replace(const HeadSelector &expected, AuditStreamId next,
                                      SyncStrength strength) {
  RawRetainedFields arguments;
  arguments.selector(expected);
  arguments.add(next.bytes());
  arguments.native(strength);
  RawRetainedPair trace{raw_origin_, {3244, 3245}, 222, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  RawRetainedEvent transaction_begin{trace.child(), {3248, 101}};
  RawRetainedEvent transaction_end{trace.child(), {3248, 101}};
  auto answer = trace.run([&]() -> Result<void> {
    if (in_transaction_)
      return Result<void>::failure({ErrorCode::busy});
    if (!healthy_)
      return Result<void>::failure({ErrorCode::audit_unavailable});
    if (expected != selection_ || next == selection_.active || next == selection_.root)
      return Result<void>::failure({ErrorCode::conflict});
    if (!valid_strength(strength))
      return Result<void>::failure({ErrorCode::invalid_range});
    if (selection_.generation == UINT64_MAX)
      return Result<void>::failure({ErrorCode::overflow});
    Transaction transaction{in_transaction_, healthy_};
    RawRetainedFinally final_transaction{[&]() noexcept {
      RawRetainedFields final;
      final.native(healthy_);
      final.native(initial_prepared_);
      final.native(transaction.attempted);
      final.native(transaction.published);
      final.native(in_transaction_);
      final.selector(selection_);
      transaction_end.publish(final.view());
    }};
    in_transaction_ = true;
    try {
      auto selected = read_selector();
      if (!selected.has_value()) {
        if (selected.error().code != ErrorCode::allocation)
          healthy_ = false;
        return Result<void>::failure(selected.error());
      }
      if (selected.value().second != selection_) {
        healthy_ = false;
        return Result<void>::failure({ErrorCode::conflict});
      }
      const HeadSelector desired{root_.environment, root_.journal, next,
                                 selection_.generation + 1};
      RawRetainedFields before;
      before.native(healthy_);
      before.native(initial_prepared_);
      before.native(transaction.attempted);
      before.native(transaction.published);
      before.native(in_transaction_);
      before.selector(selection_);
      transaction_begin.publish(before.view());
      const auto published = write_selector(desired, strength, transaction.attempted);
      if (!published.has_value())
        return published;
      selection_ = desired;
      transaction.published = true;
      return Result<void>::success();
    } catch (const std::bad_alloc &) {
      return Result<void>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  returned.selector(selection_);
  returned.native(healthy_);
  returned.native(initial_prepared_);
  trace.result(answer, returned.view());
  return answer;
}
} // namespace blackbird
