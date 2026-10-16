#include "blackbird/tui_inspector.hpp"
#include <bit>
#include <clocale>
#include <iostream>
#include <stdexcept>

using namespace blackbird;
using namespace blackbird::tui;
namespace {
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
template <class T> T take(Result<T> result) {
  check(result.has_value(), "unexpected component refusal");
  return std::move(result).value();
}
void accept(Result<void> result) {
  check(result.has_value(), "unexpected control refusal");
}
constexpr ViewIdentity view{21, 6};
TargetRef target(std::string key) { return {"fixture", "item", std::move(key), 1}; }
SceneCandidate seed(std::uint64_t revision = 0) {
  SceneCandidate candidate{view,
                           revision,
                           {{"list", {}, NodeKind::collection, true},
                            {"detail", {}, NodeKind::inspector, true}},
                           {{"list",
                             {{"a", "Alpha", target("a")},
                              {"b", "Beta", target("b")},
                              {"c", "Gamma", target("c")}},
                             "Work",
                             {}}},
                           {}};
  Inspector detail{"detail", "Output", target("a"), {}, {}, 0};
  for (unsigned i = 0; i < 12; ++i)
    detail.lines.push_back({{"line " + std::to_string(i), Ink::normal}});
  candidate.inspectors.push_back(std::move(detail));
  return candidate;
}
SceneHost host() {
  auto result = take(SceneHost::create(view, {16, 256, 65536}));
  accept(result.publish(seed()));
  return result;
}
void validation() {
  auto owner = host();
  auto immutable = owner.snapshot();
  auto bad = seed(1);
  bad.inspectors[0].lines[0][0].text = "unsafe\x1b";
  auto result = owner.publish(bad);
  check(!result.has_value() && result.error().code == ErrorCode::invalid_range,
        "unsafe inspector controls accepted");
  check(owner.snapshot() == immutable, "rejected properties replaced last good");
  bad = seed(1);
  bad.inspectors.push_back(bad.inspectors[0]);
  result = owner.publish(bad);
  check(!result.has_value() && result.error().code == ErrorCode::invalid_identity,
        "duplicate inspector accepted");
  bad = seed(1);
  bad.inspectors[0].node = "list";
  check(!owner.publish(bad).has_value(), "wrong inspector node kind accepted");
  bad = seed(1);
  bad.inspectors[0].target = TargetRef{"fixture", "item", "a", 0};
  check(!owner.publish(bad).has_value(), "invalid inspector target accepted");
  bad = seed(1);
  bad.collections[0].items[0].label = "bad\nlabel";
  check(!owner.publish(bad).has_value(), "multiline collection label accepted");
  bad = seed(1);
  bad.inspectors[0].lines[0][0].ink = std::bit_cast<Ink>(std::uint8_t{255});
  check(!owner.publish(bad).has_value(), "invalid inspector ink accepted");
  bad = seed(1);
  bad.inspectors[0].lines[0].assign(300, ChatSpan{});
  check(!owner.publish(bad).has_value(), "empty spans bypassed structural budget");
  bad = seed(1);
  bad.inspectors[0].lines[0] = {{"\xc3", Ink::normal}, {"\xa9", Ink::code}};
  accept(owner.publish(bad));
  check(owner.snapshot()->inspectors[0].lines[0][0].text == "\xc3",
        "valid cross-style UTF8 changed at publication");
  bad.inspectors[0].lines[0][0].text = "mutated";
  check(owner.snapshot()->inspectors[0].lines[0][0].text == "\xc3",
        "candidate aliases immutable inspector");
  auto bounded = take(SceneHost::create(view, {16, 12, 65536}));
  check(!bounded.publish(seed()).has_value(), "detail rows/spans bypassed item bound");
}
void navigation() {
  auto owner = host();
  auto widget = take(CollectionInspector::create(owner, "list", "detail"));
  accept(widget.act(InspectorAction::next));
  check(take(widget.state()).selected == "b", "next did not select stable key b");
  auto changed = seed(1);
  std::swap(changed.collections[0].items[0], changed.collections[0].items[1]);
  accept(owner.publish(changed));
  check(take(widget.state()).selected == "b", "reorder moved keyed selection");
  changed = seed(2);
  changed.collections[0].items.erase(changed.collections[0].items.begin() + 1);
  accept(owner.publish(changed));
  check(take(widget.state()).selected == "a", "removal lost old-order successor");
  accept(widget.act(InspectorAction::last));
  check(take(widget.state()).selected == "c", "last did not select c");
  accept(widget.act(InspectorAction::first));
  check(take(widget.state()).selected == "a", "first did not select a");
  const auto list = take(owner.handle("list"));
  const auto before = take(widget.state());
  check(!owner.position(list, "b", "absent").has_value() &&
            take(widget.state()) == before,
        "refused combined position partially moved selection");
}
void literal_layout() {
  auto owner = host();
  auto widget = take(CollectionInspector::create(owner, "list", "detail"));
  ChatGrid grid;
  grid.reset(84, 12);
  auto layout = take(widget.render(grid, {2, 1, 78, 8}));
  check(layout.split && layout.collection == Rect{2, 1, 26, 8} &&
            layout.detail == Rect{29, 1, 51, 8} && layout.divider == 28,
        "wide split geometry differs from literal contract");
  check(grid.cells[2 * 84 + 2].scalar == U'>' && grid.cells[2 * 84 + 4].scalar == U'A',
        "selection marker or label origin moved");
  check(grid.cells[2 * 84 + 29].scalar == U'l' &&
            grid.cells[2 * 84 + 28].scalar == U'│',
        "detail source/divider origin moved");
  for (std::size_t y = 0; y < 12; ++y)
    for (std::size_t x = 0; x < 84; ++x)
      if (x < 2 || x >= 80 || y < 1 || y >= 9)
        check(grid.cells[y * 84 + x].scalar == U' ', "renderer spilled outside Rect");
}
void narrow_and_text() {
  auto owner = host();
  auto changed = seed(1);
  changed.inspectors[0].lines[0] = {{"👩‍", Ink::heading}, {"💻X", Ink::normal}};
  accept(owner.publish(changed));
  auto widget = take(CollectionInspector::create(owner, "list", "detail"));
  ChatGrid grid;
  grid.reset(20, 8);
  auto layout = take(widget.render(grid, {0, 0, 20, 8}));
  check(!layout.split && layout.collection.width == 20 && layout.detail.width == 0,
        "narrow default did not show list");
  accept(widget.act(InspectorAction::focus_detail));
  grid.reset(20, 8);
  layout = take(widget.render(grid, {0, 0, 20, 8}));
  check(layout.detail.width == 20 && grid.cells[20 + 2].scalar == U'X' &&
            grid.cells[20].ink == Ink::heading,
        "cross-style emoji detail cells or first-base ink differ");
  grid.reset(1, 5);
  (void)take(widget.render(grid, {0, 0, 1, 5}));
  check(grid.cells[1].scalar == U'�' && grid.cells[2].scalar == U'X',
        "one-cell detail fallback/wrap is not literal");
  accept(widget.act(InspectorAction::focus_collection));
  check(take(widget.state()).selected == "a", "narrow return changed selection");
}
void independent_detail() {
  auto owner = host();
  auto strict = take(CollectionInspector::create(owner, "list", "detail"));
  accept(strict.act(InspectorAction::next));
  ChatGrid grid;
  grid.reset(100, 10);
  (void)take(strict.render(grid, {0, 0, 100, 10}));
  const auto text = [&] {
    std::string result;
    for (const auto &cell : grid.cells)
      result += cell.scalar < 128 ? static_cast<char>(cell.scalar) : '?';
    return result;
  };
  check(text().find("Detail target differs from selection") != std::string::npos,
        "selected-detail relation must still warn on mismatched target");
  auto independent = take(CollectionInspector::create(owner, "list", "detail",
                                                      InspectorBinding::independent));
  grid.reset(100, 10);
  (void)take(independent.render(grid, {0, 0, 100, 10}));
  check(text().find("Detail target differs from selection") == std::string::npos,
        "independent comparison must not claim selected-detail relation");
  check(take(independent.state()).selected == "b",
        "relation policy cannot change native selection");
}
void reading_continuity() {
  auto owner = host();
  auto widget = take(CollectionInspector::create(owner, "list", "detail"));
  accept(widget.act(InspectorAction::focus_detail));
  ChatGrid grid;
  grid.reset(30, 5);
  (void)take(widget.render(grid, {0, 0, 30, 5}));
  accept(widget.act(InspectorAction::page_next));
  check(widget.detail_scroll() == 4, "detail page used wrong visible height");
  accept(owner.publish(seed(1)));
  grid.reset(30, 5);
  (void)take(widget.render(grid, {0, 0, 30, 5}));
  check(widget.detail_scroll() == 4, "same target/page refresh lost reading anchor");
  accept(widget.act(InspectorAction::focus_collection));
  accept(widget.act(InspectorAction::focus_detail));
  check(widget.detail_scroll() == 4, "focus return lost detail reading anchor");
  grid.reset(3, 5);
  (void)take(widget.render(grid, {0, 0, 3, 5}));
  check(widget.detail_scroll() == 8, "reflow lost source line4 anchor");
  grid.reset(30, 5);
  (void)take(widget.render(grid, {0, 0, 30, 5}));
  check(widget.detail_scroll() == 4, "wide return did not restore source anchor");
  auto other = seed(2);
  other.inspectors[0].content_offset = 4096;
  accept(owner.publish(other));
  grid.reset(30, 5);
  (void)take(widget.render(grid, {0, 0, 30, 5}));
  check(widget.detail_scroll() == 0, "different source page inherited scroll");
  accept(widget.act(InspectorAction::last));
  check(widget.detail_scroll() == 8, "detail last line not clamped to viewport");
}
void clipping_and_retirement() {
  auto owner = host();
  auto widget = take(CollectionInspector::create(owner, "list", "detail"));
  ChatGrid grid;
  grid.reset(10, 4);
  grid.line(0, {{"界", Ink::normal}});
  const auto before = grid.cells;
  auto result = widget.render(grid, {1, 0, 4, 3});
  check(!result.has_value() && result.error().code == ErrorCode::invalid_range,
        "bisected foreign wide footprint was not refused");
  for (std::size_t i = 0; i < before.size(); ++i)
    check(before[i].scalar == grid.cells[i].scalar &&
              before[i].width == grid.cells[i].width,
          "failed clipping changed a neighboring owner");
  auto empty =
      take(widget.render(grid, {std::numeric_limits<std::size_t>::max(), 0, 100, 100}));
  check(empty.bounds.width == 0 && empty.bounds.height == 0,
        "out-of-range rectangle overflowed");
  auto changed = seed(1);
  changed.inspectors.clear();
  changed.nodes[1].kind = NodeKind::status;
  accept(owner.publish(changed));
  result = widget.render(grid, {0, 0, 10, 4});
  check(!result.has_value() && result.error().code == ErrorCode::stale_handle,
        "retired inspector handle silently changed meaning");
}
} // namespace
int main() {
  if (!std::setlocale(LC_CTYPE, "en_US.UTF-8") && !std::setlocale(LC_CTYPE, "C.UTF-8"))
    return 2;
  unsigned failures = 0;
  using Case = std::pair<const char *, void (*)()>;
  for (const auto &[name, run] :
       {Case{"validation", &validation},
        {"navigation", navigation},
        {"literal layout", literal_layout},
        {"narrow text", narrow_and_text},
        {"reading continuity", reading_continuity},
        {"independent comparison", independent_detail},
        {"clipping and retirement", clipping_and_retirement}}) {
    try {
      run();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception &error) {
      ++failures;
      std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
  }
  return failures ? 1 : 0;
}
