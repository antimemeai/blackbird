#include "blackbird/tui_inspector.hpp"
#include "blackbird/display_text.hpp"
#include <algorithm>
#include <limits>

namespace blackbird::tui {
namespace {
const Collection *collection(const Scene &scene, std::string_view key) {
  const auto found = std::ranges::find(scene.collections, key, &Collection::node);
  return found == scene.collections.end() ? nullptr : &*found;
}
const Inspector *inspector(const Scene &scene, std::string_view key) {
  const auto found = std::ranges::find(scene.inspectors, key, &Inspector::node);
  return found == scene.inspectors.end() ? nullptr : &*found;
}
std::size_t index_of(const Collection &rows, const std::optional<std::string> &key) {
  if (key) {
    const auto found = std::ranges::find(rows.items, *key, &Item::key);
    if (found != rows.items.end())
      return static_cast<std::size_t>(found - rows.items.begin());
  }
  return 0;
}
Rect clipped(const ChatGrid &grid, Rect bounds) {
  bounds.x = std::min(bounds.x, grid.width);
  bounds.y = std::min(bounds.y, grid.height);
  bounds.width = std::min(bounds.width, grid.width - bounds.x);
  bounds.height = std::min(bounds.height, grid.height - bounds.y);
  if (!bounds.width || !bounds.height)
    bounds.width = bounds.height = 0;
  return bounds;
}
bool focused(const SceneHost &host, const NodeHandle &node) noexcept {
  return host.focused() && *host.focused() == node;
}
bool present(const Notice &notice) {
  return notice.kind != NoticeKind::none || !notice.text.empty();
}
ChatRow notice_row(const Notice &notice) {
  const char *label = "";
  switch (notice.kind) {
  case NoticeKind::none:
    break;
  case NoticeKind::loading:
    label = "Loading";
    break;
  case NoticeKind::stale:
    label = "Stale";
    break;
  case NoticeKind::unavailable:
    label = "Unavailable";
    break;
  case NoticeKind::failed:
    label = "Failed";
    break;
  }
  std::string text{label};
  if (!text.empty() && !notice.text.empty())
    text += ": ";
  text += notice.text;
  return {
      {std::move(text), notice.kind == NoticeKind::failed ? Ink::failure : Ink::muted}};
}
std::size_t line_height(const ChatRow &row, CellCount width) {
  DisplayCursor cursor{row};
  DisplayCluster cluster;
  std::size_t lines = 1, cells = 0;
  while (cursor.next(cluster)) {
    display_fit(cluster, width);
    if (cells && cells + cluster.cells.value > width.value) {
      ++lines;
      cells = 0;
    }
    cells += cluster.cells.value;
  }
  return lines;
}
ByteOffset row_start(const ChatRow &row, CellCount width, std::size_t wanted) {
  if (wanted == 0)
    return {};
  DisplayCursor cursor{row};
  DisplayCluster cluster;
  std::size_t line = 0, cells = 0;
  ByteOffset start;
  while (cursor.next(cluster)) {
    display_fit(cluster, width);
    if (cells && cells + cluster.cells.value > width.value) {
      ++line;
      cells = 0;
      start = cluster.begin;
      if (line == wanted)
        return start;
    }
    cells += cluster.cells.value;
  }
  return wanted == 0 ? ByteOffset{} : start;
}
std::size_t row_for_byte(const ChatRow &row, CellCount width, ByteOffset byte) {
  DisplayCursor cursor{row};
  DisplayCluster cluster;
  std::size_t line = 0, cells = 0;
  while (cursor.next(cluster)) {
    display_fit(cluster, width);
    if (cells && cells + cluster.cells.value > width.value) {
      if (cluster.begin.value > byte.value)
        break;
      ++line;
      cells = 0;
    }
    cells += cluster.cells.value;
  }
  return line;
}
std::size_t move(std::size_t at, std::size_t last, std::size_t page,
                 InspectorAction action) {
  switch (action) {
  case InspectorAction::previous:
    return at - std::min(at, std::size_t{1});
  case InspectorAction::next:
    return at + std::min(last - at, std::size_t{1});
  case InspectorAction::first:
    return 0;
  case InspectorAction::last:
    return last;
  case InspectorAction::page_previous:
    return at - std::min(at, page);
  case InspectorAction::page_next:
    return at + std::min(last - at, page);
  case InspectorAction::focus_collection:
  case InspectorAction::focus_detail:
  case InspectorAction::toggle_focus:
    return at;
  }
  return at;
}
void put(ChatGrid &grid, Rect destination, const ChatRow &source) {
  if (!destination.width)
    return;
  DisplayCursor cursor{source};
  DisplayCluster cluster;
  ChatRow visible;
  std::size_t cells = 0;
  while (cursor.next(cluster)) {
    if (!cluster.cells.value)
      continue;
    display_fit(cluster, {destination.width});
    if (cells + cluster.cells.value > destination.width)
      break;
    if (visible.empty() || visible.back().ink != cluster.ink)
      visible.push_back({{}, cluster.ink});
    cursor.append(visible.back().text, cluster);
    cells += cluster.cells.value;
  }
  grid.line(destination.y, visible, destination.x);
}
std::size_t after_header(ChatGrid &grid, Rect bounds, std::string_view title,
                         bool focused, const Notice &notice) {
  if (!bounds.height)
    return bounds.y;
  put(grid, {bounds.x, bounds.y, bounds.width, 1},
      {{std::string{focused ? "> " : "  "} + std::string{title},
        focused ? Ink::selected : Ink::heading}});
  auto y = bounds.y + 1;
  if (present(notice) && y < bounds.y + bounds.height)
    put(grid, {bounds.x, y++, bounds.width, 1}, notice_row(notice));
  return y;
}
} // namespace
Result<CollectionInspector> CollectionInspector::create(SceneHost &host,
                                                        std::string_view left_key,
                                                        std::string_view right_key) {
  auto left = host.handle(left_key);
  if (!left.has_value())
    return Result<CollectionInspector>::failure(left.error());
  auto right = host.handle(right_key);
  if (!right.has_value())
    return Result<CollectionInspector>::failure(right.error());
  const auto scene = host.snapshot();
  const auto left_node = std::ranges::find(scene->nodes, left_key, &Node::key);
  const auto right_node = std::ranges::find(scene->nodes, right_key, &Node::key);
  if (left_node->kind != NodeKind::collection || !left_node->focusable ||
      right_node->kind != NodeKind::inspector || !right_node->focusable)
    return Result<CollectionInspector>::failure({ErrorCode::invalid_range});
  if (!collection(*scene, left_key) || !inspector(*scene, right_key))
    return Result<CollectionInspector>::failure({ErrorCode::incomplete});
  return Result<CollectionInspector>::success(
      CollectionInspector{host, std::move(left).value(), std::move(right).value()});
}
Result<std::shared_ptr<const Scene>> CollectionInspector::checked() const {
  for (const auto *node : {&collection_, &inspector_}) {
    auto current = host_->handle(node->key);
    if (!current.has_value())
      return Result<std::shared_ptr<const Scene>>::failure(
          current.error().code == ErrorCode::invalid_identity
              ? Error{ErrorCode::stale_handle}
              : current.error());
    if (current.value() != *node)
      return Result<std::shared_ptr<const Scene>>::failure({ErrorCode::stale_handle});
  }
  auto scene = host_->snapshot();
  if (!collection(*scene, collection_.key) || !inspector(*scene, inspector_.key))
    return Result<std::shared_ptr<const Scene>>::failure({ErrorCode::incomplete});
  return Result<std::shared_ptr<const Scene>>::success(std::move(scene));
}
Result<void> CollectionInspector::measure(std::shared_ptr<const Scene> scene,
                                          std::size_t width) {
  width = std::max(width, std::size_t{1});
  if (scene == measured_ && detail_width_ == width) {
    const auto total = line_starts_.empty() ? 0 : line_starts_.back();
    detail_offset_ = std::min(detail_offset_, total - std::min(total, detail_page_));
    return Result<void>::success();
  }
  const auto &detail = *inspector(*scene, inspector_.key);
  const bool same = detail_identity_ && detail.target == detail_target_ &&
                    detail.content_offset == content_offset_;
  std::size_t anchor_line = 0;
  ByteOffset anchor_byte;
  if (same && measured_ && line_starts_.size() > 1) {
    const auto after =
        std::upper_bound(line_starts_.begin(), line_starts_.end(), detail_offset_);
    anchor_line = static_cast<std::size_t>(after - line_starts_.begin() - 1);
    const auto &old = *inspector(*measured_, inspector_.key);
    anchor_byte = row_start(old.lines[anchor_line], {detail_width_},
                            detail_offset_ - line_starts_[anchor_line]);
  }
  std::vector<std::size_t> starts;
  starts.reserve(detail.lines.size() + 1);
  starts.push_back(0);
  for (const auto &row : detail.lines) {
    const auto height = line_height(row, {width});
    if (height > std::numeric_limits<std::size_t>::max() - starts.back())
      return Result<void>::failure({ErrorCode::overflow});
    starts.push_back(starts.back() + height);
  }
  std::optional<TargetRef> next_target;
  if (!same)
    next_target = detail.target;
  std::size_t offset = 0;
  if (same && !detail.lines.empty()) {
    anchor_line = std::min(anchor_line, detail.lines.size() - 1);
    offset = starts[anchor_line] +
             row_for_byte(detail.lines[anchor_line], {width}, anchor_byte);
  }
  offset = std::min(offset, starts.back() - std::min(starts.back(), detail_page_));
  line_starts_.swap(starts);
  measured_.swap(scene);
  if (!same)
    detail_target_.swap(next_target);
  content_offset_ = detail.content_offset;
  detail_identity_ = true;
  detail_width_ = width;
  detail_offset_ = offset;
  return Result<void>::success();
}
Result<void> CollectionInspector::act(InspectorAction action) {
  try {
    auto scene = checked();
    if (!scene.has_value())
      return Result<void>::failure(scene.error());
    switch (action) {
    case InspectorAction::focus_collection:
      return host_->focus(collection_);
    case InspectorAction::focus_detail:
      return host_->focus(inspector_);
    case InspectorAction::toggle_focus:
      return host_->focus(focused(*host_, collection_) ? inspector_ : collection_);
    case InspectorAction::previous:
    case InspectorAction::next:
    case InspectorAction::first:
    case InspectorAction::last:
    case InspectorAction::page_previous:
    case InspectorAction::page_next:
      break;
    default:
      return Result<void>::failure({ErrorCode::unsupported});
    }
    if (focused(*host_, inspector_)) {
      if (auto result = measure(scene.value(), detail_width_); !result.has_value())
        return result;
      const auto total = line_starts_.back();
      const auto last = total - std::min(total, detail_page_);
      detail_offset_ = move(detail_offset_, last, detail_page_, action);
      return Result<void>::success();
    }
    if (!focused(*host_, collection_))
      return Result<void>::failure({ErrorCode::conflict});
    auto state = host_->collection_state(collection_);
    if (!state.has_value())
      return Result<void>::failure(state.error());
    const auto &rows = *collection(*scene.value(), collection_.key);
    if (rows.items.empty())
      return Result<void>::success();
    const auto selected = move(index_of(rows, state.value().selected),
                               rows.items.size() - 1, collection_page_, action);
    auto anchor = index_of(rows, state.value().anchor);
    if (selected < anchor)
      anchor = selected;
    else if (selected - anchor >= collection_page_)
      anchor = selected - collection_page_ + 1;
    return host_->position(collection_, rows.items[selected].key,
                           rows.items[anchor].key);
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Result<void>::failure({ErrorCode::capacity});
  }
}
Result<InspectorLayout> CollectionInspector::render(ChatGrid &grid, Rect bounds) {
  try {
    auto scene = checked();
    if (!scene.has_value())
      return Result<InspectorLayout>::failure(scene.error());
    if (grid.width && grid.height > grid.cells.size() / grid.width)
      return Result<InspectorLayout>::failure({ErrorCode::invalid_range});
    bounds = clipped(grid, bounds);
    InspectorLayout layout;
    layout.bounds = bounds;
    if (!bounds.width || !bounds.height)
      return Result<InspectorLayout>::success(layout);
    const auto right = bounds.x + bounds.width;
    for (std::size_t y = bounds.y; y < bounds.y + bounds.height; ++y)
      if ((bounds.x && grid.cells[y * grid.width + bounds.x].width == 0) ||
          (right < grid.width && grid.cells[y * grid.width + right - 1].width == 2))
        return Result<InspectorLayout>::failure({ErrorCode::invalid_range});
    const bool detail_focus = focused(*host_, inspector_);
    const bool list_focus = focused(*host_, collection_);
    layout.split = bounds.width >= 72 && bounds.height >= 4;
    if (layout.split) {
      const auto list_width =
          std::clamp(bounds.width / 3, std::size_t{24}, std::size_t{36});
      layout.collection = {bounds.x, bounds.y, list_width, bounds.height};
      layout.divider = bounds.x + list_width;
      layout.detail = {bounds.x + list_width + 1, bounds.y,
                       bounds.width - list_width - 1, bounds.height};
    } else if (detail_focus)
      layout.detail = bounds;
    else
      layout.collection = bounds;
    auto state = host_->collection_state(collection_);
    if (!state.has_value())
      return Result<InspectorLayout>::failure(state.error());
    const auto &rows = *collection(*scene.value(), collection_.key);
    const auto &detail = *inspector(*scene.value(), inspector_.key);
    const auto &selection = state.value().selected;
    const auto selected = selection
                              ? std::ranges::find(rows.items, *selection, &Item::key)
                              : rows.items.end();
    const bool mismatch = selected == rows.items.end()
                              ? detail.target.has_value()
                              : selected->target != detail.target;
    if (layout.collection.height) {
      const auto consumed = std::min(layout.collection.height,
                                     std::size_t{1} + (present(rows.notice) ? 1U : 0U));
      collection_page_ = std::max(std::size_t{1}, layout.collection.height - consumed);
    }
    if (layout.detail.height) {
      const auto consumed = std::min(
          layout.detail.height,
          std::size_t{1} + (present(detail.notice) ? 1U : 0U) + (mismatch ? 1U : 0U));
      detail_page_ = std::max(std::size_t{1}, layout.detail.height - consumed);
      if (auto result = measure(scene.value(), layout.detail.width);
          !result.has_value())
        return Result<InspectorLayout>::failure(result.error());
    }
    const ChatRow blank{{std::string(bounds.width, ' '), Ink::normal}};
    for (std::size_t y = bounds.y; y < bounds.y + bounds.height; ++y)
      grid.line(y, blank, bounds.x);
    if (layout.divider) {
      const ChatRow divider{{"│", Ink::border}};
      for (std::size_t y = bounds.y; y < bounds.y + bounds.height; ++y)
        grid.line(y, divider, *layout.divider);
    }
    if (layout.collection.height) {
      auto y = after_header(grid, layout.collection,
                            rows.title.empty() ? "Items" : rows.title, list_focus,
                            rows.notice);
      if (rows.items.empty() && y < bounds.y + bounds.height)
        put(grid, {layout.collection.x, y, layout.collection.width, 1},
            {{"(empty)", Ink::muted}});
      for (auto at = index_of(rows, state.value().anchor);
           at < rows.items.size() && y < bounds.y + bounds.height; ++at, ++y) {
        const auto &item = rows.items[at];
        const bool active = selection == item.key;
        put(grid, {layout.collection.x, y, layout.collection.width, 1},
            {{std::string{active ? "> " : "  "} + item.label,
              active ? Ink::selected : Ink::normal}});
      }
    }
    if (layout.detail.height) {
      auto y = after_header(grid, layout.detail,
                            detail.title.empty() ? "Detail" : detail.title,
                            detail_focus, detail.notice);
      if (mismatch && y < bounds.y + bounds.height)
        put(grid, {layout.detail.x, y++, layout.detail.width, 1},
            {{"Detail target differs from selection", Ink::failure}});
      if (detail.lines.empty() && y < bounds.y + bounds.height)
        put(grid, {layout.detail.x, y, layout.detail.width, 1},
            {{"(empty)", Ink::muted}});
      std::size_t line = 0;
      if (line_starts_.size() > 1) {
        const auto after =
            std::upper_bound(line_starts_.begin(), line_starts_.end(), detail_offset_);
        line = static_cast<std::size_t>(after - line_starts_.begin() - 1);
      }
      for (; line < detail.lines.size() && y < bounds.y + bounds.height; ++line) {
        const auto skip = detail_offset_ > line_starts_[line]
                              ? detail_offset_ - line_starts_[line]
                              : 0;
        DisplayCursor cursor{detail.lines[line]};
        DisplayCluster cluster;
        ChatRow row;
        std::size_t cells = 0, subrow = 0;
        while (cursor.next(cluster)) {
          if (!cluster.cells.value)
            continue;
          display_fit(cluster, {layout.detail.width});
          if (cells && cells + cluster.cells.value > layout.detail.width) {
            if (subrow >= skip) {
              put(grid, {layout.detail.x, y++, layout.detail.width, 1}, row);
              row.clear();
              if (y == bounds.y + bounds.height)
                break;
            }
            ++subrow;
            cells = 0;
          }
          if (subrow >= skip) {
            if (row.empty() || row.back().ink != cluster.ink)
              row.push_back({{}, cluster.ink});
            cursor.append(row.back().text, cluster);
          }
          cells += cluster.cells.value;
        }
        if (subrow >= skip && y < bounds.y + bounds.height)
          put(grid, {layout.detail.x, y++, layout.detail.width, 1}, row);
      }
    }
    return Result<InspectorLayout>::success(layout);
  } catch (const std::bad_alloc &) {
    return Result<InspectorLayout>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Result<InspectorLayout>::failure({ErrorCode::capacity});
  }
}
Result<CollectionState> CollectionInspector::state() const {
  return host_->collection_state(collection_);
}
} // namespace blackbird::tui
