# Native keyed scene/collection implementation

2026-10-15. Implements the [short subplan](SUBPLAN.md) in `include/blackbird/tui_components.hpp`, `src/tui_components.cpp` and `tests/tui_components_test.cpp`. Root owns build registration, integrated source qualification and publication. This unit does not edit terminal/chat source, execute references, adopt dependencies or claim the full TUI foundation is delivered.

## Exposed behavior

`blackbird::tui::SceneHost::create` accepts an explicit nonzero view owner/incarnation and finite positive node/item/string capacities. `publish` accepts an owned-data candidate against the current scene revision and publishes an immutable shared snapshot only after complete validation and local-state reconciliation. Public candidate data is copied across the publication boundary; existing mutable references into that candidate cannot mutate a retained snapshot.

Scene keys are unique across this bounded view forest. Parent references must exist and be acyclic; collection payloads match collection nodes; row keys are unique within each collection; actionable targets require owner/kind/key/incarnation. The module carries references, not domain execution authority. Checked node generations survive ordinary compatible updates and retire on removal/type replacement. A removed generation cannot return by reusing its key.

Native focus, keyed selection and scroll anchor are independent of composition. Insertion/reorder preserves surviving references and anchor offset. Removed rows choose the nearest surviving old successor, then predecessor, then the first new row; replaced anchors reset their offset. A deleted/replaced target or focus/selection change disarms prior intent. `arm` requires the focused collection's selected actionable item; `take_armed` returns that exact reference once. Callers still resolve actual domain authority and freshness before any effect.

One retained shell draft has its own revision and byte cursor. Candidate edits require that revision; stale candidates cannot overwrite later native typing or partially publish another scene. The module stores bytes and cursor bounds only; it does not implement grapheme editing, layout, rendering, Lua callbacks, virtual collection fetching or external actions.

## Costs and limits

Each publication copies and validates the complete **supplied bounded window**, not an unlimited domain collection. Node and prior-collection indexes avoid a per-node quadratic scan; keyed validation/reconciliation builds ordered maps/sets. Local row lookup remains linear in the supplied collection window. No measured latency, frame-rate or arbitrary domain-size claim is made.

`Limits::bytes` bounds the cumulative candidate scene string payload; the independent draft has the same separate byte bound. It is not an RSS/allocation ceiling. Node index keys, collection selection/anchor keys, an armed target and retained snapshots intentionally own additional storage. Each retained snapshot keeps its own bounded copied payload alive until its holders release it. This is an initial ownership-safe native unit, not a promise of incremental immutable-subtree sharing.

The host has one execution owner. Immutable snapshots may outlive it; mutable host methods require that owner. This unit adds no thread scheduler or cross-process publication protocol. Checked numeric handles are lifetime checks, not security capabilities.

## Direct verification

The initial behavioral red used an unsupported-publication stub and failed at the expected valid publication (`unexpected component refusal`). The first compiler invocation also exposed a host SDK-path mismatch; using the repository's configured Xcode sysroot corrected that environment issue before the behavioral red ran.

The final standalone pinned Clang23.1.2 build uses the repository's C++20 warning profile and Xcode sysroot, writing only to ignored `context/tui-components/`. This avoids concurrent mutation of `build/debug` while other owners build their units. The semantic cases pass: independent expected references after reorder/removal, immutable candidate-borrow isolation, malformed/duplicate/cyclic candidates, capacity rejection, wrong view/incarnation, stale scene/node handles, domain-target replacement, one-use arming and draft conflict preservation.

**50 actual sequential allocation-failure cuts** across publication, selection, anchor change, local draft edit, arm and taking an armed target pass. Each cut forces `std::bad_alloc` at a different actual allocation before the first uncut successful operation. Every rejected operation preserves the prior snapshot identity/content, draft, focus, selection/anchor and exact armed domain target. Long keys force real string allocation instead of relying on small-string storage. This is one direct mechanism for allocation/publication faults, not an implementation mirror.

Focused `clang-tidy` on both changed translation units passes after fixing unchecked optional access; no suppressions were added. `clang-format` and relevant diff checks pass. Root's registered CMake target and the single independent code challenge remain integration work; these standalone checks do not imply the entire repository suite or every host profile passed.
