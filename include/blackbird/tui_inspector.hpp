#pragma once
#include "blackbird/tui_components.hpp"

namespace blackbird::tui {
struct Rect {
  std::size_t x = 0, y = 0, width = 0, height = 0;
  bool operator==(const Rect &) const = default;
};
enum class InspectorAction : std::uint8_t {
  previous,
  next,
  first,
  last,
  page_previous,
  page_next,
  focus_collection,
  focus_detail,
  toggle_focus
};
enum class InspectorBinding : std::uint8_t { selected_detail, independent };
struct InspectorLayout {
  Rect bounds, collection, detail;
  std::optional<std::size_t> divider;
  bool split = false;
};
// Native read-only local control. host must retain its address and outlive this.
// Adapters publish safe finite properties; no callback or domain access occurs.
class CollectionInspector {
public:
  CollectionInspector(const CollectionInspector &) = delete;
  CollectionInspector &operator=(const CollectionInspector &) = delete;
  CollectionInspector(CollectionInspector &&) noexcept = default;
  CollectionInspector &operator=(CollectionInspector &&) noexcept = default;
  static Result<CollectionInspector>
  create(SceneHost &host, std::string_view collection, std::string_view inspector);
  static Result<CollectionInspector> create(SceneHost &host,
                                            std::string_view collection,
                                            std::string_view inspector,
                                            InspectorBinding binding);
  Result<void> act(InspectorAction action);
  Result<InspectorLayout> render(ChatGrid &grid, Rect bounds);
  Result<CollectionState> state() const;
  std::size_t detail_scroll() const noexcept { return detail_offset_; }

private:
  CollectionInspector(SceneHost &host, NodeHandle collection, NodeHandle inspector,
                      InspectorBinding binding)
      : host_(&host), collection_(std::move(collection)),
        inspector_(std::move(inspector)), binding_(binding) {}
  SceneHost *host_;
  NodeHandle collection_, inspector_;
  InspectorBinding binding_;
  Result<std::shared_ptr<const Scene>> checked() const;
  Result<void> measure(std::shared_ptr<const Scene> scene, std::size_t width);
  std::shared_ptr<const Scene> measured_;
  std::vector<std::size_t> line_starts_;
  std::optional<TargetRef> detail_target_;
  std::uint64_t content_offset_ = 0;
  bool detail_identity_ = false;
  std::size_t detail_width_ = 0;
  std::size_t detail_offset_ = 0;
  std::size_t collection_page_ = 1, detail_page_ = 1;
};
} // namespace blackbird::tui
