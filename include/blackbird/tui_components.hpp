#pragma once
#include "blackbird/foundation.hpp"
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace blackbird::tui {
struct ViewIdentity {
  std::uint64_t owner = 0, incarnation = 0;
  bool operator==(const ViewIdentity &) const = default;
};
// A presentation reference, never authority to execute a domain operation.
struct TargetRef {
  std::string owner, kind, key;
  std::uint64_t incarnation = 0;
  bool operator==(const TargetRef &) const = default;
};
enum class NodeKind : std::uint8_t { container, collection, editor, inspector, status };
struct Node {
  std::string key;
  std::optional<std::string> parent;
  NodeKind kind = NodeKind::container;
  bool focusable = false;
  bool operator==(const Node &) const = default;
};
struct Item {
  std::string key, label;
  std::optional<TargetRef> target;
  bool operator==(const Item &) const = default;
};
struct Collection {
  std::string node;
  std::vector<Item> items;
  bool operator==(const Collection &) const = default;
};
struct DraftEdit {
  std::uint64_t base_revision = 0;
  std::string text;
  std::size_t cursor = 0; // Byte offset; this storage unit performs no text editing.
};
struct SceneCandidate {
  ViewIdentity view;
  std::uint64_t base_revision = 0;
  std::vector<Node> nodes;
  std::vector<Collection> collections;
  std::optional<DraftEdit> draft;
};
struct Scene {
  ViewIdentity view;
  std::uint64_t revision = 0;
  std::vector<Node> nodes;
  std::vector<Collection> collections;
};
struct Limits {
  // Scene-owned strings sum to at most bytes; the independent shell draft also
  // has that byte limit. Node/item counts apply to the supplied bounded window.
  std::size_t nodes = 0, items = 0, bytes = 0;
};
struct NodeHandle {
  ViewIdentity view;
  std::string key;
  std::uint64_t generation = 0;
  bool operator==(const NodeHandle &) const = default;
};
struct CollectionState {
  std::optional<std::string> selected, anchor;
  std::size_t anchor_offset = 0;
  bool operator==(const CollectionState &) const = default;
};
struct Draft {
  std::uint64_t revision = 0;
  std::string text;
  std::size_t cursor = 0;
  bool operator==(const Draft &) const = default;
};

// Single execution owner. Snapshots are immutable owned copies and may outlive it.
// Keys are unique across one view, not merely within siblings. Borrowed getters
// remain valid until the next host mutation; queued work must retain owned values.
class SceneHost {
public:
  static Result<SceneHost> create(ViewIdentity view, Limits limits);
  SceneHost(const SceneHost &) = delete;
  SceneHost &operator=(const SceneHost &) = delete;
  SceneHost(SceneHost &&) noexcept = default;
  SceneHost &operator=(SceneHost &&) noexcept = default;
  std::shared_ptr<const Scene> snapshot() const noexcept { return scene_; }
  Result<void> publish(const SceneCandidate &candidate);
  Result<NodeHandle> handle(std::string_view key) const;
  Result<void> focus(const NodeHandle &node);
  const std::optional<NodeHandle> &focused() const noexcept { return focus_; }
  Result<void> select(const NodeHandle &node, std::string_view item);
  Result<void> anchor(const NodeHandle &node, std::string_view item,
                      std::size_t offset = 0);
  Result<CollectionState> collection_state(const NodeHandle &node) const;
  Result<void> edit_draft(const DraftEdit &edit);
  const Draft &draft() const noexcept { return draft_; }
  Result<void> arm(const NodeHandle &node);
  Result<TargetRef> take_armed();
  bool armed() const noexcept { return armed_.has_value(); }

private:
  struct NodeState {
    std::uint64_t generation;
    std::size_t index;
  };
  struct Armed {
    NodeHandle node;
    std::string item;
    TargetRef target;
  };
  SceneHost(ViewIdentity view, Limits limits, std::shared_ptr<const Scene> scene)
      : view_(view), limits_(limits), scene_(std::move(scene)) {}
  Result<void> check(const NodeHandle &node) const;
  const Collection *collection(std::string_view key) const noexcept;
  ViewIdentity view_;
  Limits limits_;
  std::shared_ptr<const Scene> scene_;
  std::map<std::string, NodeState, std::less<>> generations_;
  std::map<std::string, CollectionState, std::less<>> collections_;
  std::uint64_t next_generation_ = 0;
  std::optional<NodeHandle> focus_;
  Draft draft_;
  std::optional<Armed> armed_;
};
} // namespace blackbird::tui
