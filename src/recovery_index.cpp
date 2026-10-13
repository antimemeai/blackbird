#include "blackbird/recovery_index.hpp"
#include <algorithm>

namespace blackbird {
namespace {
constexpr std::size_t header_size = 40;
constexpr std::size_t entry_size = 48;
constexpr std::size_t ref_size = 24;
constexpr std::size_t max_entries =
    (RecoveryIndex::page_size - header_size - ref_size) / (entry_size + ref_size);
constexpr std::size_t max_depth = 32;
void encode(MutableByteView out, std::uint64_t n) {
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<std::byte>(n & 255);
    n >>= 8;
  }
}
std::uint64_t decode(ByteView in) {
  std::uint64_t n = 0;
  for (std::size_t i = in.size(); i > 0; --i)
    n = (n << 8) | std::to_integer<unsigned char>(in[i - 1]);
  return n;
}
Result<void> transfer(Storage &file, std::uint64_t offset, MutableByteView out) {
  std::size_t done = 0;
  while (done < out.size()) {
    auto n = file.read_at(offset + done, out.subspan(done));
    if (!n.has_value())
      return Result<void>::failure(n.error());
    if (n.value() == 0 || n.value() > out.size() - done)
      return Result<void>::failure({ErrorCode::corrupt});
    done += n.value();
  }
  return Result<void>::success();
}
} // namespace
Result<std::unique_ptr<RecoveryIndex>>
RecoveryIndex::open(std::unique_ptr<JournalFile> file, std::uint64_t max_bytes,
                    const RawRetainedOrigin &raw_origin) {
  RawRetainedFields arguments;
  arguments.native(max_bytes);
  RawRetainedPair trace{raw_origin, {3180, 3181}, 106, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::unique_ptr<RecoveryIndex>> {
    if (!file || max_bytes < page_size)
      return Result<std::unique_ptr<RecoveryIndex>>::failure(
          {ErrorCode::invalid_range});
    auto extent = file->extent();
    if (!extent.has_value())
      return Result<std::unique_ptr<RecoveryIndex>>::failure(extent.error());
    if (extent.value() > max_bytes || extent.value() > UINT64_MAX - page_size)
      return Result<std::unique_ptr<RecoveryIndex>>::failure({ErrorCode::capacity});
    // A failed partial page is orphaned, never selected by a valid root.
    const auto end = (extent.value() + page_size - 1) / page_size * page_size;
    try {
      return Result<std::unique_ptr<RecoveryIndex>>::success(
          std::unique_ptr<RecoveryIndex>{
              new RecoveryIndex{std::move(file), end, max_bytes, trace.child()}});
    } catch (const std::bad_alloc &) {
      return Result<std::unique_ptr<RecoveryIndex>>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value())
    returned.native(answer.value()->end_);
  trace.result(answer, returned.view());
  return answer;
}
Result<RecoveryIndex::Node> RecoveryIndex::read(RecoveryPageRef ref) const {
  RawRetainedFields arguments;
  arguments.page(ref);
  RawRetainedPair trace{raw_origin_, {3186, 3187}, 119, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<Node> {
    if (ref.generation == 0 || ref.offset % page_size != 0 || ref.offset > end_ ||
        page_size > end_ - ref.offset)
      return Result<Node>::failure({ErrorCode::corrupt});
    std::array<std::byte, page_size> bytes{};
    auto loaded = transfer(*file_, ref.offset, bytes);
    if (!loaded.has_value())
      return Result<Node>::failure(loaded.error());
    if (decode(ByteView{bytes}.subspan(0, 8)) != 0x3158444942524242ULL ||
        decode(ByteView{bytes}.subspan(8, 8)) != ref.offset ||
        decode(ByteView{bytes}.subspan(16, 8)) != ref.generation ||
        decode(ByteView{bytes}.subspan(24, 4)) > 1 ||
        decode(ByteView{bytes}.subspan(32, 4)) != ref.checksum ||
        decode(ByteView{bytes}.subspan(36, 4)) != 0)
      return Result<Node>::failure({ErrorCode::corrupt});
    encode(MutableByteView{bytes}.subspan(32, 4), 0);
    if (crc32c(bytes) != ref.checksum)
      return Result<Node>::failure({ErrorCode::corrupt});
    const auto count = decode(ByteView{bytes}.subspan(28, 4));
    if (count == 0 || count > max_entries)
      return Result<Node>::failure({ErrorCode::corrupt});
    try {
      Node node;
      node.leaf = decode(ByteView{bytes}.subspan(24, 4)) == 0;
      node.entries.reserve(static_cast<std::size_t>(count));
      std::size_t at = header_size;
      for (std::size_t i = 0; i < count; ++i) {
        RecoveryIndexEntry entry{};
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(at), entry.key.size(),
                    entry.key.begin());
        entry.value = decode(ByteView{bytes}.subspan(at + 40, 8));
        at += entry_size;
        if (!node.entries.empty() && !(node.entries.back().key < entry.key))
          return Result<Node>::failure({ErrorCode::corrupt});
        node.entries.push_back(entry);
      }
      if (!node.leaf) {
        node.children.reserve(static_cast<std::size_t>(count) + 1);
        for (std::size_t i = 0; i <= count; ++i) {
          RecoveryPageRef child{
              decode(ByteView{bytes}.subspan(at, 8)),
              decode(ByteView{bytes}.subspan(at + 8, 8)),
              static_cast<std::uint32_t>(decode(ByteView{bytes}.subspan(at + 16, 4)))};
          if (decode(ByteView{bytes}.subspan(at + 20, 4)) != 0 ||
              child.generation == 0 || child.offset % page_size != 0 ||
              child.offset >= ref.offset || child.generation >= ref.generation)
            return Result<Node>::failure({ErrorCode::corrupt});
          node.children.push_back(child);
          at += ref_size;
        }
      }
      for (; at < bytes.size(); ++at)
        if (bytes[at] != std::byte{0})
          return Result<Node>::failure({ErrorCode::corrupt});
      return Result<Node>::success(std::move(node));
    } catch (const std::bad_alloc &) {
      return Result<Node>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto count = answer.value().entries.size();
    returned.native(answer.value().leaf);
    returned.native(count);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<RecoveryPageRef> RecoveryIndex::write(const Node &node) {
  RawRetainedFields arguments;
  const auto raw_entries = node.entries.size(), raw_children = node.children.size();
  arguments.native(end_);
  arguments.native(node.leaf);
  arguments.native(raw_entries);
  arguments.native(raw_children);
  RawRetainedPair trace{raw_origin_, {3188, 3189}, 143, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<RecoveryPageRef> {
    if (node.entries.empty() || node.entries.size() > max_entries ||
        (!node.leaf && node.children.size() != node.entries.size() + 1))
      return Result<RecoveryPageRef>::failure({ErrorCode::invalid_range});
    if (end_ > max_bytes_ || page_size > max_bytes_ - end_)
      return Result<RecoveryPageRef>::failure({ErrorCode::capacity});
    std::array<std::byte, page_size> bytes{};
    RecoveryPageRef ref{end_, end_ / page_size + 1, 0};
    encode(MutableByteView{bytes}.subspan(0, 8), 0x3158444942524242ULL);
    encode(MutableByteView{bytes}.subspan(8, 8), ref.offset);
    encode(MutableByteView{bytes}.subspan(16, 8), ref.generation);
    encode(MutableByteView{bytes}.subspan(24, 4), node.leaf ? 0 : 1);
    encode(MutableByteView{bytes}.subspan(28, 4), node.entries.size());
    std::size_t at = header_size;
    for (const auto &entry : node.entries) {
      std::copy(entry.key.begin(), entry.key.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(at));
      encode(MutableByteView{bytes}.subspan(at + 40, 8), entry.value);
      at += entry_size;
    }
    if (!node.leaf)
      for (const auto &child : node.children) {
        encode(MutableByteView{bytes}.subspan(at, 8), child.offset);
        encode(MutableByteView{bytes}.subspan(at + 8, 8), child.generation);
        encode(MutableByteView{bytes}.subspan(at + 16, 4), child.checksum);
        at += ref_size;
      }
    ref.checksum = crc32c(bytes);
    encode(MutableByteView{bytes}.subspan(32, 4), ref.checksum);
    // Burn the address even on failed writes; an old selected page is never mutated.
    RawRetainedEvent burn{raw_origin_, {3199, 68}};
    end_ += page_size;
    RawRetainedFields burned;
    burned.page(ref);
    burned.native(end_);
    burn.publish(burned.view());
    std::size_t done = 0;
    while (done < bytes.size()) {
      auto n = file_->write_at(ref.offset + done, ByteView{bytes}.subspan(done));
      if (!n.has_value())
        return Result<RecoveryPageRef>::failure(n.error());
      if (n.value() == 0 || n.value() > bytes.size() - done)
        return Result<RecoveryPageRef>::failure({ErrorCode::io});
      done += n.value();
    }
    return Result<RecoveryPageRef>::success(ref);
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    returned.page(answer.value());
    returned.native(end_);
  }
  trace.result(answer, returned.view());
  return answer;
}
Result<std::optional<std::uint64_t>> RecoveryIndex::find(RecoveryPageRef root,
                                                         const RecoveryKey &key) const {
  RawRetainedFields arguments;
  arguments.page(root);
  arguments.add(key);
  RawRetainedPair trace{raw_origin_, {3182, 3183}, 159, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<std::optional<std::uint64_t>> {
    for (std::size_t depth = 0; root.generation != 0 && depth < max_depth; ++depth) {
      auto loaded = read(root);
      if (!loaded.has_value())
        return Result<std::optional<std::uint64_t>>::failure(loaded.error());
      const auto &node = loaded.value();
      const auto it = std::lower_bound(
          node.entries.begin(), node.entries.end(), key,
          [](const auto &entry, const auto &k) { return entry.key < k; });
      if (it != node.entries.end() && it->key == key) {
        RawRetainedFields selected;
        selected.add(it->key);
        selected.native(it->value);
        (void)raw_retained_event(raw_origin_, {3190, 88}, selected.view());
        return Result<std::optional<std::uint64_t>>::success(it->value);
      }
      if (node.leaf)
        return Result<std::optional<std::uint64_t>>::success(std::nullopt);
      const auto &child =
          node.children[static_cast<std::size_t>(it - node.entries.begin())];
      RawRetainedFields selected;
      selected.page(child);
      (void)raw_retained_event(raw_origin_, {3191, 60}, selected.view());
      root = child;
    }
    if (root.generation != 0 || root.offset != 0 || root.checksum != 0)
      return Result<std::optional<std::uint64_t>>::failure({ErrorCode::corrupt});
    return Result<std::optional<std::uint64_t>>::success(std::nullopt);
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &selected = answer.value();
    const bool found = selected.has_value();
    returned.native(found);
    if (selected.has_value())
      returned.native(*selected);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<RecoveryIndex::Change> RecoveryIndex::insert(RecoveryPageRef root,
                                                    RecoveryKey key,
                                                    std::uint64_t value,
                                                    std::size_t depth) {
  if (depth >= max_depth)
    return Result<Change>::failure({ErrorCode::corrupt});
  Node node;
  if (root.generation != 0) {
    auto loaded = read(root);
    if (!loaded.has_value())
      return Result<Change>::failure(loaded.error());
    node = std::move(loaded).value();
  } else if (root.offset != 0 || root.checksum != 0) {
    return Result<Change>::failure({ErrorCode::corrupt});
  }
  auto it =
      std::lower_bound(node.entries.begin(), node.entries.end(), key,
                       [](const auto &entry, const auto &k) { return entry.key < k; });
  const auto slot = static_cast<std::size_t>(it - node.entries.begin());
  if (it != node.entries.end() && it->key == key)
    it->value = value;
  else if (node.leaf)
    node.entries.insert(it, {key, value});
  else {
    RawRetainedFields selected;
    selected.page(node.children[slot]);
    (void)raw_retained_event(raw_origin_, {3191, 60}, selected.view());
    auto child = insert(node.children[slot], key, value, depth + 1);
    if (!child.has_value())
      return Result<Change>::failure(child.error());
    node.children[slot] = child.value().left;
    const auto &change = child.value();
    if (change.split) {
      node.entries.insert(node.entries.begin() + static_cast<std::ptrdiff_t>(slot),
                          *change.split);
      node.children.insert(node.children.begin() +
                               static_cast<std::ptrdiff_t>(slot + 1),
                           child.value().right);
    }
  }
  if (node.entries.size() <= max_entries) {
    auto ref = write(node);
    if (!ref.has_value())
      return Result<Change>::failure(ref.error());
    return Result<Change>::success({ref.value(), std::nullopt, {}});
  }
  const auto middle = node.entries.size() / 2;
  const auto separator = node.entries[middle];
  Node right;
  right.leaf = node.leaf;
  right.entries.assign(node.entries.begin() + static_cast<std::ptrdiff_t>(middle + 1),
                       node.entries.end());
  node.entries.resize(middle);
  if (!node.leaf) {
    right.children.assign(node.children.begin() +
                              static_cast<std::ptrdiff_t>(middle + 1),
                          node.children.end());
    node.children.resize(middle + 1);
  }
  RawRetainedEvent split{raw_origin_, {3192, 128}};
  auto left_ref = write(node);
  if (!left_ref.has_value())
    return Result<Change>::failure(left_ref.error());
  auto right_ref = write(right);
  if (!right_ref.has_value())
    return Result<Change>::failure(right_ref.error());
  RawRetainedFields selected;
  selected.add(separator.key);
  selected.native(separator.value);
  selected.page(left_ref.value());
  selected.page(right_ref.value());
  split.publish(selected.view());
  return Result<Change>::success({left_ref.value(), separator, right_ref.value()});
}
Result<RecoveryPageRef> RecoveryIndex::put(RecoveryPageRef root, RecoveryKey key,
                                           std::uint64_t value) {
  RawRetainedFields arguments;
  arguments.page(root);
  arguments.add(key);
  arguments.native(value);
  RawRetainedPair trace{raw_origin_, {3184, 3185}, 178, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<RecoveryPageRef> {
    try {
      auto changed = insert(root, key, value, 0);
      if (!changed.has_value())
        return Result<RecoveryPageRef>::failure(changed.error());
      const auto &change = changed.value();
      if (!change.split)
        return Result<RecoveryPageRef>::success(change.left);
      Node node;
      node.leaf = false;
      node.entries.push_back(*change.split);
      node.children = {changed.value().left, changed.value().right};
      return write(node);
    } catch (const std::bad_alloc &) {
      return Result<RecoveryPageRef>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    returned.page(answer.value());
  }
  trace.result(answer, returned.view());
  return answer;
}
Result<bool>
RecoveryIndex::walk(RecoveryPageRef root, const RecoveryKey &lower, std::size_t &left,
                    const std::function<Result<bool>(RecoveryIndexEntry)> &visit,
                    std::size_t depth) const {
  if (depth >= max_depth)
    return Result<bool>::failure({ErrorCode::corrupt});
  auto loaded = read(root);
  if (!loaded.has_value())
    return Result<bool>::failure(loaded.error());
  const auto &node = loaded.value();
  auto it =
      std::lower_bound(node.entries.begin(), node.entries.end(), lower,
                       [](const auto &entry, const auto &k) { return entry.key < k; });
  auto slot = static_cast<std::size_t>(it - node.entries.begin());
  for (; slot <= node.entries.size(); ++slot) {
    if (!node.leaf) {
      RawRetainedFields selected;
      selected.page(node.children[slot]);
      (void)raw_retained_event(raw_origin_, {3191, 60}, selected.view());
      auto more = walk(node.children[slot], lower, left, visit, depth + 1);
      if (!more.has_value() || !more.value())
        return more;
    }
    if (slot == node.entries.size())
      break;
    if (left == 0)
      return Result<bool>::success(false);
    --left;
    const auto &entry = node.entries[slot];
    RawRetainedFields arguments;
    arguments.add(entry.key);
    arguments.native(entry.value);
    RawRetainedPair callback{raw_origin_, {3195, 3196}, 139, arguments.view()};
    RawRetainedOriginScope callback_origin{raw_origin_, callback.child()};
    auto more = callback.run([&] { return visit(entry); });
    RawRetainedFields returned;
    if (more.has_value())
      returned.native(more.value());
    callback.result(more, returned.view());
    if (!more.has_value() || !more.value())
      return more;
    if (left == 0)
      return Result<bool>::success(false);
  }
  return Result<bool>::success(true);
}
Result<void> RecoveryIndex::range(
    RecoveryPageRef root, const RecoveryKey &lower, std::size_t limit,
    const std::function<Result<bool>(RecoveryIndexEntry)> &visit) const {
  RawRetainedFields arguments;
  arguments.page(root);
  arguments.add(lower);
  arguments.native(limit);
  RawRetainedPair trace{raw_origin_, {3193, 3194}, 166, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<void> {
    if (!visit)
      return Result<void>::failure({ErrorCode::invalid_range});
    if (root.generation == 0)
      return (root.offset == 0 && root.checksum == 0)
                 ? Result<void>::success()
                 : Result<void>::failure({ErrorCode::corrupt});
    if (limit == 0)
      return Result<void>::success();
    auto result = walk(root, lower, limit, visit, 0);
    if (!result.has_value())
      return Result<void>::failure(result.error());
    return Result<void>::success();
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    returned.native(limit);
  }
  trace.result(answer, returned.view());
  return answer;
}
Result<void> RecoveryIndex::synchronize() {
  RawRetainedFields arguments;
  arguments.native(end_);
  RawRetainedPair trace{raw_origin_, {3197, 3198}, 98, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run(
      [&]() -> Result<void> { return file_->synchronize(SyncStrength::full); });
  RawRetainedFields returned;
  if (answer.has_value()) {
  }
  trace.result(answer, returned.view());
  return answer;
}
} // namespace blackbird
