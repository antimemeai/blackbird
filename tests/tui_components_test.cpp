#include "blackbird/tui_components.hpp"
#include <cstdlib>
#include <iostream>
#include <source_location>
#include <stdexcept>

using namespace blackbird;
using namespace blackbird::tui;
namespace {
thread_local std::ptrdiff_t allocation_cut = -1;
thread_local bool allocation_failed = false;
} // namespace
void *operator new(std::size_t size) {
  if (allocation_cut == 0) {
    allocation_cut = -1;
    allocation_failed = true;
    throw std::bad_alloc{};
  }
  if (allocation_cut > 0)
    --allocation_cut;
  if (void *pointer = std::malloc(size == 0 ? 1 : size))
    return pointer;
  throw std::bad_alloc{};
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { std::free(pointer); }
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class T> T take(Result<T> result) {
  if (!result.has_value())
    throw std::runtime_error("unexpected component refusal");
  return std::move(result).value();
}
void accept(Result<void> result) {
  check(result.has_value(), "unexpected component refusal");
}
template <class T> T &present(std::optional<T> &value) {
  if (!value)
    throw std::runtime_error("missing optional in fixture");
  return *value;
}
template <class T> const T &present(const std::optional<T> &value) {
  if (!value)
    throw std::runtime_error("missing optional in fixture");
  return *value;
}
template <class T>
void refuses(const Result<T> &result, ErrorCode code,
             std::source_location source = std::source_location::current()) {
  check(!result.has_value(), "invalid operation accepted");
  if (result.error().code != code)
    throw std::runtime_error("wrong refusal classification at line " +
                             std::to_string(source.line()));
}
constexpr ViewIdentity view{17, 4};
constexpr Limits limits{32, 64, 16384};
TargetRef target(std::string key, std::uint64_t incarnation = 1) {
  return {"fixture-session", "job", std::move(key), incarnation};
}
SceneCandidate seed(std::uint64_t base) {
  return {view,
          base,
          {{"root", {}, NodeKind::container, false},
           {"work", "root", NodeKind::collection, true},
           {"detail", "root", NodeKind::inspector, true},
           {"compose", "root", NodeKind::editor, true}},
          {{"work",
            {{"a", "Alpha", target("job-a")},
             {"b", "Beta", target("job-b")},
             {"c", "Gamma", target("job-c")}}}},
          {}};
}
void identity_and_publication() {
  refuses(SceneHost::create({0, 4}, limits), ErrorCode::invalid_identity);
  refuses(SceneHost::create({17, 0}, limits), ErrorCode::invalid_identity);
  refuses(SceneHost::create(view, {}), ErrorCode::capacity);
  auto host = take(SceneHost::create(view, limits));
  check(host.snapshot()->revision == 0, "initial scene revision");
  auto candidate = seed(0);
  accept(host.publish(candidate));
  const auto first = host.snapshot();
  check(first->revision == 1 && first->collections[0].items[1].label == "Beta",
        "valid scene not published");
  candidate.nodes[1].key = "mutated-borrow";
  candidate.collections[0].items[1].label = "changed after publication";
  present(candidate.collections[0].items[1].target).key = "wrong-job";
  check(first->nodes[1].key == "work" &&
            first->collections[0].items[1].label == "Beta" &&
            present(first->collections[0].items[1].target).key == "job-b",
        "mutable candidate aliases published scene");
  auto invalid = seed(1);
  invalid.nodes.push_back(invalid.nodes[1]);
  refuses(host.publish(invalid), ErrorCode::invalid_identity);
  check(host.snapshot() == first, "duplicate key changed published scene");
  invalid = seed(1);
  invalid.nodes[1].parent = "missing";
  refuses(host.publish(invalid), ErrorCode::invalid_identity);
  invalid = seed(1);
  invalid.nodes[0].parent = "detail";
  refuses(host.publish(invalid), ErrorCode::invalid_range);
  invalid = seed(1);
  invalid.collections[0].items.push_back(invalid.collections[0].items[0]);
  refuses(host.publish(invalid), ErrorCode::invalid_identity);
  invalid = seed(1);
  invalid.collections.push_back(invalid.collections[0]);
  refuses(host.publish(invalid), ErrorCode::invalid_identity);
  invalid = seed(1);
  invalid.collections[0].node = "detail";
  refuses(host.publish(invalid), ErrorCode::invalid_range);
  invalid = seed(1);
  invalid.collections.clear();
  refuses(host.publish(invalid), ErrorCode::invalid_range);
  invalid = seed(1);
  present(invalid.collections[0].items[0].target).incarnation = 0;
  refuses(host.publish(invalid), ErrorCode::invalid_identity);
  invalid = seed(1);
  invalid.view.owner = 99;
  refuses(host.publish(invalid), ErrorCode::wrong_registry);
  invalid = seed(1);
  invalid.view.incarnation = 99;
  refuses(host.publish(invalid), ErrorCode::wrong_incarnation);
  refuses(host.publish(seed(0)), ErrorCode::conflict);
  check(host.snapshot() == first, "rejected scene changed publication");
  auto limited = take(SceneHost::create(view, {2, 64, 16384}));
  refuses(limited.publish(seed(0)), ErrorCode::capacity);
  auto bytes = take(SceneHost::create(view, {32, 64, 12}));
  refuses(bytes.publish(seed(0)), ErrorCode::capacity);
  auto rows = take(SceneHost::create(view, {32, 2, 16384}));
  refuses(rows.publish(seed(0)), ErrorCode::capacity);
}
void collection_and_targets() {
  auto host = take(SceneHost::create(view, limits));
  accept(host.publish(seed(0)));
  const auto work = take(host.handle("work"));
  check(host.focused() == std::optional{work}, "first focusable not focused");
  check(take(host.collection_state(work)) == CollectionState{"a", "a", 0},
        "initial collection state");
  accept(host.select(work, "b"));
  accept(host.anchor(work, "b", 4));
  accept(host.arm(work));
  auto reordered = seed(1);
  reordered.collections[0].items = {{"c", "Gamma", target("job-c")},
                                    {"x", "New", target("job-x")},
                                    {"a", "Alpha", target("job-a")},
                                    {"b", "Beta", target("job-b")}};
  accept(host.publish(reordered));
  check(take(host.collection_state(work)) == CollectionState{"b", "b", 4},
        "insertion/reorder transferred selection or anchor");
  check(take(host.take_armed()) == target("job-b"), "armed identity retargeted");
  refuses(host.take_armed(), ErrorCode::incomplete);
  accept(host.publish(seed(2)));
  accept(host.arm(work));
  auto removed = seed(3);
  removed.collections[0].items.erase(removed.collections[0].items.begin() + 1);
  accept(host.publish(removed));
  check(take(host.collection_state(work)) == CollectionState{"c", "c", 0},
        "deleted row did not choose old-order successor/reset offset");
  check(!host.armed(), "deleted target left action armed");
  refuses(host.take_armed(), ErrorCode::incomplete);
  accept(host.arm(work)); // A new deliberate action may target the successor.
  check(take(host.take_armed()) == target("job-c"), "fresh successor action");
  accept(host.publish(seed(4)));
  check(take(host.collection_state(work)).selected == "c",
        "reinserted key stole surviving selection");
  accept(host.select(work, "b"));
  accept(host.arm(work));
  auto replaced = seed(5);
  replaced.collections[0].items[1].target = target("job-b", 2);
  accept(host.publish(replaced));
  check(!host.armed(), "changed domain incarnation left target armed");
  accept(host.arm(work));
  check(take(host.take_armed()) == target("job-b", 2), "new incarnation action");
  accept(host.arm(work));
  refuses(host.select(work, "absent"), ErrorCode::invalid_identity);
  check(host.armed(), "refused selection changed accepted state");
  accept(host.focus(take(host.handle("compose"))));
  check(!host.armed(), "focus change did not disarm");
  refuses(host.arm(work), ErrorCode::conflict);
  refuses(host.focus(take(host.handle("root"))), ErrorCode::invalid_range);
  auto foreign = work;
  foreign.view.incarnation = 5;
  refuses(host.select(foreign, "a"), ErrorCode::wrong_incarnation);
  auto changed = seed(6);
  changed.nodes[1].kind = NodeKind::inspector;
  changed.collections.clear();
  accept(host.publish(changed));
  refuses(host.focus(work), ErrorCode::stale_handle);
  const auto replacement = take(host.handle("work"));
  check(replacement.generation != work.generation, "kind change reused handle");
  accept(host.publish(seed(7)));
  check(take(host.handle("work")).generation != work.generation,
        "old handle resurrected after replacement");
  auto empty = seed(8);
  empty.collections[0].items.clear();
  accept(host.publish(empty));
  const auto current = take(host.handle("work"));
  check(take(host.collection_state(current)) == CollectionState{}, "empty state");
  accept(host.focus(current));
  refuses(host.arm(current), ErrorCode::incomplete);
  auto passive = seed(9);
  passive.collections[0].items[0].target.reset();
  accept(host.publish(passive));
  refuses(host.arm(current), ErrorCode::unsupported);
}
void draft_conflicts() {
  auto host = take(SceneHost::create(view, limits));
  accept(host.publish(seed(0)));
  accept(host.edit_draft({0, "operator typed", 14}));
  const auto original = host.snapshot();
  auto stale = seed(1);
  stale.draft = DraftEdit{0, "old composition", 3};
  refuses(host.publish(stale), ErrorCode::conflict);
  check(host.snapshot() == original && host.draft() == Draft{1, "operator typed", 14},
        "stale draft partially published");
  accept(host.publish(seed(1)));
  check(host.draft() == Draft{1, "operator typed", 14},
        "ordinary scene publication overwrote newer typing");
  auto fresh = seed(2);
  fresh.draft = DraftEdit{1, "deliberate change", 5};
  accept(host.publish(fresh));
  check(host.draft() == Draft{2, "deliberate change", 5}, "fresh draft not applied");
  refuses(host.edit_draft({1, "stale edit", 2}), ErrorCode::conflict);
  refuses(host.edit_draft({2, "short", 6}), ErrorCode::invalid_range);
  check(host.draft() == Draft{2, "deliberate change", 5}, "refused edit changed draft");
  check(original->revision == 1 && original->collections[0].items[0].label == "Alpha",
        "old snapshot changed after newer publication");
}
void allocation_cuts() {
  auto initial = seed(0);
  const std::string long_node = "collection-" + std::string(80, 'k');
  initial.nodes[1].key = long_node;
  initial.collections[0].node = long_node;
  for (auto &item : initial.collections[0].items) {
    item.key += std::string(80, 'i');
    present(item.target).owner += std::string(80, 'o');
    present(item.target).key += std::string(80, 't');
  }
  const auto first = initial.collections[0].items[0].key;
  const auto second = initial.collections[0].items[1].key;
  const auto expected_target = present(initial.collections[0].items[0].target);
  auto candidate = initial;
  candidate.base_revision = 1;
  candidate.collections[0].items.erase(candidate.collections[0].items.begin());
  candidate.draft = DraftEdit{1, std::string(128, 'n'), 5};
  const DraftEdit next_draft{1, std::string(128, 'd'), 5};
  enum class Cut { publication, selection, anchor, draft, arm, take };
  std::size_t failed_cuts = 0;
  for (const auto operation : {Cut::publication, Cut::selection, Cut::anchor,
                               Cut::draft, Cut::arm, Cut::take}) {
    // Stop at the first run with no allocation failure: every preceding cut
    // removes a different allocation from this exact operation, not a model.
    for (std::ptrdiff_t cut = 0;; ++cut) {
      auto host = take(SceneHost::create(view, limits));
      accept(host.publish(initial));
      const auto node = take(host.handle(long_node));
      accept(host.select(node, first));
      accept(host.anchor(node, second, 9));
      accept(host.edit_draft({0, std::string(128, 'p'), 4}));
      accept(host.arm(node));
      const auto snapshot = host.snapshot();
      const auto state = take(host.collection_state(node));
      const auto draft = host.draft();
      const auto focus = host.focused();
      allocation_failed = false;
      allocation_cut = cut;
      Result<void> result = Result<void>::success();
      try {
        switch (operation) {
        case Cut::publication:
          result = host.publish(candidate);
          break;
        case Cut::selection:
          result = host.select(node, second);
          break;
        case Cut::anchor:
          result = host.anchor(node, first, 11);
          break;
        case Cut::draft:
          result = host.edit_draft(next_draft);
          break;
        case Cut::arm:
          result = host.arm(node);
          break;
        case Cut::take: {
          const auto target_result = host.take_armed();
          if (!target_result.has_value())
            result = Result<void>::failure(target_result.error());
          break;
        }
        }
      } catch (...) {
        allocation_cut = -1;
        throw;
      }
      allocation_cut = -1;
      if (!allocation_failed) {
        accept(result);
        break;
      }
      ++failed_cuts;
      refuses(result, ErrorCode::allocation);
      check(host.snapshot() == snapshot && host.draft() == draft &&
                host.focused() == focus && take(host.collection_state(node)) == state &&
                host.armed(),
            "allocation failure partially changed accepted state");
      check(take(host.take_armed()) == expected_target,
            "allocation failure changed armed target");
      check(snapshot->collections[0].items[0].key == first,
            "allocation failure mutated retained immutable snapshot");
    }
  }
  check(failed_cuts > 0, "allocation failure fixture missed every allocation");
  std::cout << failed_cuts << " direct allocation cuts preserved live state\n";
}
} // namespace
int main() {
  try {
    identity_and_publication();
    collection_and_targets();
    draft_conflicts();
    allocation_cuts();
    std::cout << "native scene/collection contracts pass\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
