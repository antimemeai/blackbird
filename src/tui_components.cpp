#include "blackbird/tui_components.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace blackbird::tui {
namespace {
using KeyIndex = std::map<std::string_view, std::size_t, std::less<>>;
Result<void> identity(ViewIdentity expected, ViewIdentity actual) {
  if (expected.owner != actual.owner)
    return Result<void>::failure({ErrorCode::wrong_registry});
  if (expected.incarnation != actual.incarnation)
    return Result<void>::failure({ErrorCode::wrong_incarnation});
  return Result<void>::success();
}
const Collection *find_collection(const Scene &scene, std::string_view key) {
  const auto found = std::ranges::find(scene.collections, key, &Collection::node);
  return found == scene.collections.end() ? nullptr : &*found;
}
const Item *find_item(const Collection &collection, std::string_view key) {
  const auto found = std::ranges::find(collection.items, key, &Item::key);
  return found == collection.items.end() ? nullptr : &*found;
}
bool valid_kind(NodeKind kind) {
  switch (kind) {
  case NodeKind::container:
  case NodeKind::collection:
  case NodeKind::editor:
  case NodeKind::inspector:
  case NodeKind::status:
    return true;
  }
  return false;
}
Result<void> validate(const SceneCandidate &candidate, Limits limits) {
  if (candidate.nodes.size() > limits.nodes)
    return Result<void>::failure({ErrorCode::capacity});
  std::size_t bytes = 0, items = 0;
  auto charge = [&](std::string_view value) {
    if (value.size() > limits.bytes - bytes)
      return false;
    bytes += value.size();
    return true;
  };
  KeyIndex nodes;
  for (std::size_t i = 0; i < candidate.nodes.size(); ++i) {
    const auto &node = candidate.nodes[i];
    if (node.key.empty() || !nodes.emplace(node.key, i).second ||
        (node.parent && node.parent->empty()))
      return Result<void>::failure({ErrorCode::invalid_identity});
    if (!valid_kind(node.kind))
      return Result<void>::failure({ErrorCode::unsupported});
    if (!charge(node.key) || (node.parent && !charge(*node.parent)))
      return Result<void>::failure({ErrorCode::capacity});
  }
  const auto count = candidate.nodes.size();
  std::vector<std::size_t> parents(count, count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto &parent = candidate.nodes[i].parent;
    if (parent) {
      const auto found = nodes.find(*parent);
      if (found == nodes.end())
        return Result<void>::failure({ErrorCode::invalid_identity});
      parents[i] = found->second;
    }
  }
  // Each parent edge is visited at most twice; no recursive stack/depth limit.
  std::vector<unsigned char> colors(count, 0);
  for (std::size_t start = 0; start < count; ++start) {
    auto at = start;
    while (at != count && colors[at] == 0) {
      colors[at] = 1;
      at = parents[at];
    }
    if (at != count && colors[at] == 1)
      return Result<void>::failure({ErrorCode::invalid_range});
    at = start;
    while (at != count && colors[at] == 1) {
      colors[at] = 2;
      at = parents[at];
    }
  }
  std::set<std::string_view, std::less<>> collections;
  for (const auto &collection : candidate.collections) {
    const auto node = nodes.find(collection.node);
    if (node == nodes.end() ||
        candidate.nodes[node->second].kind != NodeKind::collection)
      return Result<void>::failure({ErrorCode::invalid_range});
    if (!collections.insert(collection.node).second)
      return Result<void>::failure({ErrorCode::invalid_identity});
    if (!charge(collection.node) || collection.items.size() > limits.items - items)
      return Result<void>::failure({ErrorCode::capacity});
    items += collection.items.size();
    std::set<std::string_view, std::less<>> keys;
    for (const auto &item : collection.items) {
      if (item.key.empty() || !keys.insert(item.key).second)
        return Result<void>::failure({ErrorCode::invalid_identity});
      if (!charge(item.key) || !charge(item.label))
        return Result<void>::failure({ErrorCode::capacity});
      if (item.target) {
        const auto &target = *item.target;
        if (target.owner.empty() || target.kind.empty() || target.key.empty() ||
            !target.incarnation)
          return Result<void>::failure({ErrorCode::invalid_identity});
        if (!charge(target.owner) || !charge(target.kind) || !charge(target.key))
          return Result<void>::failure({ErrorCode::capacity});
      }
    }
  }
  for (const auto &node : candidate.nodes)
    if (node.kind == NodeKind::collection && !collections.contains(node.key))
      return Result<void>::failure({ErrorCode::invalid_range});
  return Result<void>::success();
}
// Keep the same key, then the nearest surviving old successor/predecessor.
// If none survives, use the first new row. New insertions do not define proximity.
std::optional<std::string> reconcile(const std::optional<std::string> &key,
                                     const Collection *old, const Collection &next,
                                     const KeyIndex &index) {
  if (key && index.contains(*key))
    return key;
  if (key && old) {
    const auto found = std::ranges::find(old->items, *key, &Item::key);
    if (found != old->items.end()) {
      for (auto it = std::next(found); it != old->items.end(); ++it)
        if (index.contains(it->key))
          return it->key;
      auto it = found;
      while (it != old->items.begin()) {
        --it;
        if (index.contains(it->key))
          return it->key;
      }
    }
  }
  if (next.items.empty())
    return {};
  return next.items.front().key;
}
Result<void> validate_draft(const Draft &current, const DraftEdit &edit,
                            Limits limits) {
  if (edit.base_revision != current.revision)
    return Result<void>::failure({ErrorCode::conflict});
  if (current.revision == std::numeric_limits<std::uint64_t>::max())
    return Result<void>::failure({ErrorCode::overflow});
  if (edit.text.size() > limits.bytes)
    return Result<void>::failure({ErrorCode::capacity});
  if (edit.cursor > edit.text.size())
    return Result<void>::failure({ErrorCode::invalid_range});
  return Result<void>::success();
}
} // namespace
Result<SceneHost> SceneHost::create(ViewIdentity view, Limits limits) {
  if (!view.owner || !view.incarnation)
    return Result<SceneHost>::failure({ErrorCode::invalid_identity});
  if (!limits.nodes || !limits.items || !limits.bytes)
    return Result<SceneHost>::failure({ErrorCode::capacity});
  try {
    auto scene = std::make_shared<Scene>();
    scene->view = view;
    return Result<SceneHost>::success(SceneHost{view, limits, std::move(scene)});
  } catch (const std::bad_alloc &) {
    return Result<SceneHost>::failure({ErrorCode::allocation});
  }
}
Result<void> SceneHost::publish(const SceneCandidate &candidate) {
  if (const auto checked = identity(view_, candidate.view); !checked.has_value())
    return checked;
  if (candidate.base_revision != scene_->revision)
    return Result<void>::failure({ErrorCode::conflict});
  if (scene_->revision == std::numeric_limits<std::uint64_t>::max())
    return Result<void>::failure({ErrorCode::overflow});
  if (candidate.draft) {
    const auto checked = validate_draft(draft_, *candidate.draft, limits_);
    if (!checked.has_value())
      return checked;
  }
  try {
    const auto valid = validate(candidate, limits_);
    if (!valid.has_value())
      return valid;
    // Copy, rather than move, across publication: external mutable borrows into a
    // public candidate must not become aliases of an immutable snapshot.
    std::shared_ptr<const Scene> next = std::make_shared<Scene>(
        Scene{view_, scene_->revision + 1, candidate.nodes, candidate.collections});
    decltype(generations_) generations;
    auto serial = next_generation_;
    for (std::size_t i = 0; i < next->nodes.size(); ++i) {
      const auto &node = next->nodes[i];
      const auto old = generations_.find(node.key);
      if (old != generations_.end() &&
          scene_->nodes[old->second.index].kind == node.kind)
        generations.emplace(node.key, NodeState{old->second.generation, i});
      else {
        if (serial == std::numeric_limits<std::uint64_t>::max())
          return Result<void>::failure({ErrorCode::overflow});
        generations.emplace(node.key, NodeState{++serial, i});
      }
    }
    decltype(collections_) states;
    KeyIndex prior_collections;
    for (std::size_t i = 0; i < scene_->collections.size(); ++i)
      prior_collections.emplace(scene_->collections[i].node, i);
    for (const auto &collection : next->collections) {
      KeyIndex index;
      for (std::size_t i = 0; i < collection.items.size(); ++i)
        index.emplace(collection.items[i].key, i);
      const auto found = collections_.find(collection.node);
      const auto prior = prior_collections.find(collection.node);
      const auto *old = prior == prior_collections.end()
                            ? nullptr
                            : &scene_->collections[prior->second];
      const CollectionState empty;
      const auto &before = found == collections_.end() ? empty : found->second;
      CollectionState state{reconcile(before.selected, old, collection, index),
                            reconcile(before.anchor, old, collection, index), 0};
      if (state.anchor == before.anchor)
        state.anchor_offset = before.anchor_offset;
      states.emplace(collection.node, std::move(state));
    }
    std::optional<NodeHandle> focus;
    auto choose = [&](std::string_view key, bool compatible) {
      const auto current = generations.find(key);
      if (current == generations.end())
        return false;
      const auto &node = next->nodes[current->second.index];
      if (!node.focusable)
        return false;
      const auto generation = current->second.generation;
      const auto prior = generations_.find(node.key);
      if (compatible &&
          (prior == generations_.end() || prior->second.generation != generation))
        return false;
      focus = NodeHandle{view_, node.key, generation};
      return true;
    };
    if (focus_)
      (void)choose(focus_->key, true);
    if (!focus && focus_) {
      const auto old = std::ranges::find(scene_->nodes, focus_->key, &Node::key);
      if (old != scene_->nodes.end()) {
        for (auto it = std::next(old); !focus && it != scene_->nodes.end(); ++it)
          (void)choose(it->key, true);
        auto it = old;
        while (!focus && it != scene_->nodes.begin()) {
          --it;
          (void)choose(it->key, true);
        }
      }
    }
    if (!focus)
      for (const auto &node : next->nodes)
        if (choose(node.key, false))
          break;
    auto armed = armed_;
    if (armed) {
      const auto *rows = find_collection(*next, armed->node.key);
      const auto *item = rows ? find_item(*rows, armed->item) : nullptr;
      const auto state = states.find(armed->node.key);
      if (!focus || *focus != armed->node || !item || state == states.end() ||
          state->second.selected != armed->item || !item->target ||
          *item->target != armed->target)
        armed.reset();
    }
    std::optional<Draft> draft;
    if (candidate.draft)
      draft =
          Draft{draft_.revision + 1, candidate.draft->text, candidate.draft->cursor};
    // All allocating work finished. Publication below uses nonthrowing swaps.
    scene_.swap(next);
    generations_.swap(generations);
    collections_.swap(states);
    focus_.swap(focus);
    armed_.swap(armed);
    if (draft)
      std::swap(draft_, *draft);
    next_generation_ = serial;
    return Result<void>::success();
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Result<void>::failure({ErrorCode::capacity});
  }
}
Result<void> SceneHost::check(const NodeHandle &node) const {
  const auto checked = identity(view_, node.view);
  if (!checked.has_value())
    return checked;
  const auto found = generations_.find(node.key);
  if (found == generations_.end() || found->second.generation != node.generation)
    return Result<void>::failure({ErrorCode::stale_handle});
  return Result<void>::success();
}
const Collection *SceneHost::collection(std::string_view key) const noexcept {
  return find_collection(*scene_, key);
}
Result<NodeHandle> SceneHost::handle(std::string_view key) const {
  const auto found = generations_.find(key);
  if (found == generations_.end())
    return Result<NodeHandle>::failure({ErrorCode::invalid_identity});
  try {
    return Result<NodeHandle>::success({view_, found->first, found->second.generation});
  } catch (const std::bad_alloc &) {
    return Result<NodeHandle>::failure({ErrorCode::allocation});
  }
}
Result<void> SceneHost::focus(const NodeHandle &node) {
  const auto checked = check(node);
  if (!checked.has_value())
    return checked;
  const auto &found = scene_->nodes[generations_.at(node.key).index];
  if (!found.focusable)
    return Result<void>::failure({ErrorCode::invalid_range});
  try {
    std::optional<NodeHandle> next{node};
    if (next != focus_)
      armed_.reset();
    focus_.swap(next);
    return Result<void>::success();
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
Result<void> SceneHost::select(const NodeHandle &node, std::string_view item) {
  const auto checked = check(node);
  if (!checked.has_value())
    return checked;
  const auto *rows = collection(node.key);
  if (!rows)
    return Result<void>::failure({ErrorCode::invalid_range});
  if (!find_item(*rows, item))
    return Result<void>::failure({ErrorCode::invalid_identity});
  try {
    auto &state = collections_.at(node.key);
    auto next = state;
    next.selected = item;
    if (next.selected != state.selected)
      armed_.reset();
    std::swap(state, next);
    return Result<void>::success();
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
Result<void> SceneHost::anchor(const NodeHandle &node, std::string_view item,
                               std::size_t offset) {
  const auto checked = check(node);
  if (!checked.has_value())
    return checked;
  const auto *rows = collection(node.key);
  if (!rows)
    return Result<void>::failure({ErrorCode::invalid_range});
  if (!find_item(*rows, item))
    return Result<void>::failure({ErrorCode::invalid_identity});
  try {
    auto &state = collections_.at(node.key);
    auto next = state;
    next.anchor = item;
    next.anchor_offset = offset;
    std::swap(state, next);
    return Result<void>::success();
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
Result<CollectionState> SceneHost::collection_state(const NodeHandle &node) const {
  const auto checked = check(node);
  if (!checked.has_value())
    return Result<CollectionState>::failure(checked.error());
  const auto found = collections_.find(node.key);
  if (found == collections_.end())
    return Result<CollectionState>::failure({ErrorCode::invalid_range});
  try {
    return Result<CollectionState>::success(found->second);
  } catch (const std::bad_alloc &) {
    return Result<CollectionState>::failure({ErrorCode::allocation});
  }
}
Result<void> SceneHost::edit_draft(const DraftEdit &edit) {
  const auto checked = validate_draft(draft_, edit, limits_);
  if (!checked.has_value())
    return checked;
  try {
    Draft next{draft_.revision + 1, edit.text, edit.cursor};
    std::swap(draft_, next);
    return Result<void>::success();
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
Result<void> SceneHost::arm(const NodeHandle &node) {
  const auto checked = check(node);
  if (!checked.has_value())
    return checked;
  if (!focus_ || *focus_ != node)
    return Result<void>::failure({ErrorCode::conflict});
  const auto *rows = collection(node.key);
  if (!rows)
    return Result<void>::failure({ErrorCode::invalid_range});
  const auto &selected = collections_.at(node.key).selected;
  if (!selected)
    return Result<void>::failure({ErrorCode::incomplete});
  const auto *item = find_item(*rows, *selected);
  if (!item || !item->target)
    return Result<void>::failure({ErrorCode::unsupported});
  try {
    std::optional<Armed> next{Armed{node, item->key, *item->target}};
    armed_.swap(next);
    return Result<void>::success();
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
Result<TargetRef> SceneHost::take_armed() {
  if (!armed_)
    return Result<TargetRef>::failure({ErrorCode::incomplete});
  try {
    auto result = Result<TargetRef>::success(armed_->target);
    armed_.reset();
    return result;
  } catch (const std::bad_alloc &) {
    return Result<TargetRef>::failure({ErrorCode::allocation});
  }
}
} // namespace blackbird::tui
