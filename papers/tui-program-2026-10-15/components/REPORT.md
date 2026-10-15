# Reusable C++/Lua TUI components: source-grounded proposal

2026-10-15. Independent research/design contribution for an enduring Blackbird TUI and reusable owned components. Read against HEAD `5fbb15197776eca4402df79f4201e7fbe6f19a66`. This is a component architecture proposal, distinct from the enduring doctrine and from a delivery plan. No product source, dependency, reference implementation, account or running Blackbird instance was changed or executed. The companion [component design](COMPONENT_DESIGN.md) develops the concrete contracts.

## The architectural finding

Blackbird already has useful native rendering and terminal-custody machinery. The missing reusable boundary is above that machinery: a domain-independent presentation/control host, typed keyed components and Lua composition that do not inherit conversation-specific execution or hold up terminal control. Build that boundary around actual existing seams. Replacing it with a foreign toolkit or putting current `TerminalUI::run` inside a more fashionable widget class would not answer the problem.

The proposed separation is:

1. Native terminal transport, text/geometry, local controls and input routing.
2. A typed retained scene of keyed components, published with the action bindings that give it meaning.
3. A separately owned Lua VM for composition and programmable local behavior; Lua never executes on the terminal/input owner.
4. Domain adapters that supply observations and submit semantic actions to existing runtime owners.

The toolkit owns presentation and local interaction. It does not own a second operation ledger, participant registry, permission model, capability executor or metric processor. A domain view can be complicated while its controls remain ordinary reusable lists, inspectors, editors and action affordances.

## Current seams and limits

| Inspected source | Current mechanism | Consequence for the proposed boundary |
| --- | --- | --- |
| `include/blackbird/chat_view.hpp`; `src/chat_view.cpp`, `ChatGrid`, `ChatPainter`, `ChatOutput` | Compact cells, delta preparation, explicit commit, single-writer nonblocking packet with retained offset. `ChatOutput` borrows its packet storage. | Preserve the unsent suffix and storage lifetime. A frame owner can package packet, grid and revision together; changing the rendering vocabulary must not lose transport correctness. |
| `src/chat_view.cpp`, `ChatView::rows`, `trim` | Bounded derived chat blocks with cached rows, incremental streaming state and width-dependent reflow. | Keep conversation formatting as a domain document adapter. Reuse its valid caching work, but do not make every reusable control a chat block or confuse its bounded display with retained history. |
| `include/blackbird/terminal.hpp`, `Composer`; `src/terminal.cpp`, `feed_impl`, `choices`, `palette_lines` | Byte decoder, multiline editing/history, paste, legacy keys, command palette and selection combined in one object. | Separate transport decoding from editor state and palette controls while preserving draft/history behavior. The current ESC gap is an identified defect, not permission for an unrelated input rewrite. |
| `src/terminal.cpp`, `TerminalUI::run` | One application loop combines incoming presentation, task pages, private command PTY input, prompt queue, status/avatar, editor handoff and layout. | Extract boundaries around ownership and state; retain native cancellation and child-input semantics. The loop is an application, not a generic component system. |
| `include/blackbird/task_view.hpp`; `src/task_view.cpp` | Bounded pure task-page formatting with no task authority, using shared `Value` and chat row/style types. | This is a clean domain-adapter seed. Generic collection controls should accept keyed rows, while task status/count meanings remain in the task adapter. |
| `include/blackbird/terminal_custody.hpp`; `src/terminal_custody.cpp` | Thread-owned terminal mode, resize, notification pipes and restore/handoff. | Retain a single terminal owner and explicit handoff. Widgets must not acquire termios, write escape bytes, install process signals or compete for descriptors. |
| `src/coding.cpp`, `Runtime::install_impl`, `interrupt_hook`; `tooling/lua/blackbird.lua` | Existing Lua APIs perform requests/calls/context edits/presentation through the audited native bridge; an instruction hook polls commands and cancellation. | A UI composition API is new. Existing workflow Lua and a proposed UI VM have different lifetimes and scheduling. A hook is not preemption of arbitrary native callbacks or a reason to run composition on the input owner. |

Existing tests in `tests/chat_view_test.cpp`, `tests/terminal_test.cpp` and the terminal custody drivers already attack some affected contracts. This research ran no test campaign; the design names additional direct oracles only for new claims.

## What the acquired references teach

Reuse the [uji/Ito corpus manifest](../../tui-2026-10-15/uji/sources.tsv) and [report](../../tui-2026-10-15/uji/REPORT.md). Actual source inspection in this lane read the captured `quarantine/tui-2026-10-15/uji/source/ito/lua/ito/controls/list.lua`, `runtime/composition.lua` and `window/init.lua`: keyed placement distinguishes identity from sibling position; list selection/first-row anchors can follow item identity; window routing explicitly settles focus. The same source walks all item IDs during arrangement and may retry row placement to expose a selected variable-height item. Thus keyed lazy drawing is useful, but is not proof of bounded whole-collection work.

The [performance source study](../../tui-2026-10-15/performance/REPORT.md) supplies exact pins and anchors for Notcurses's render/write relationship, Neovim's overloaded display queue, Turbo Vision's flush deadlines and Lite XL's cooperative Lua budget. These demonstrate distinct failure paths: drawing, transport, queue service and callback runtime need separate ownership. They do not establish that any one framework is fast enough here.

The [input source study](../../tui-2026-10-15/input/REPORT.md) supplies protocol/device specifics: key identity is not committed text, gestures can require deadlines, press/release information may not survive a legacy terminal, device reconnection changes lifetime, and relative controls differ from absolute state. A reusable host must carry these semantics instead of converting every controller event into a synthetic character.

The [interaction theory](../../../docs/INTERACTION_THEORY.md) supplies the purpose: expert programmable joint work, compatible references, stable meaning, meaningful feedback and continuity. It neither selects retained-mode rendering nor mandates a universal widget hierarchy. The proposed keyed native scene is a concrete design choice: it lets native controls continue while Lua composition is unavailable, gives published geometry and actions a coherent lifetime, and permits explicit invalidation without discovering every mutation of arbitrary Lua tables.

The read-only Rhizome watch at `projects_old/inactive-2026-10-07/rhizome/papers/cpp-rigor-stack.md`, section “Design constraints before tools,” reinforces the directly applicable C++ questions: ownership and invalidation of borrowed storage, checked dimensions, mutable-to-published transitions and reclamation after actual readers finish. Its engine choices remain local. For this proposal, these principles apply directly to borrowed frame bytes, scene publication, queued Lua/native values and view-generation retirement; no new tooling or qualification campaign follows from rereading them.

## Important tradeoffs

A separate composition owner costs queues, revision management and memory lifetimes. It earns independence from view-script execution and permits a native fallback. It does not contain a C++ memory corruption, and a permanently blocked native call in that worker cannot be safely killed as a thread. The proposed UI API therefore supplies bounded construction/local operations and async domain requests; extensions needing arbitrary blocking/native execution belong behind an independently owned operation boundary. Full process isolation is a further option if that fault class becomes a requirement, not an assumed free property.

Native editor/focus/collection behavior reduces needless Lua round trips. It also fixes some semantics in owned C++; those primitives must be small and composable, expose useful policy rather than arbitrary hidden restrictions, and allow programmable actions to compose them. Lua owns view composition, styles, arrangement and ordinary application policy, while underlying effect authority remains where it already belongs.

A reusable tree can become a large generic framework. The proposed inventory groups components by shared mechanism and excludes speculative convenience controls until a domain view needs them. Virtual collection, typed form and inspector behavior merit shared ownership because command centers, team views, capability management, hooks and metric inspection all need them. Their domain meanings remain separate.

## Delivery boundary

The proposal is ready for integration with the root doctrine, existing-view design and future application surfaces. It does not authorize implementation. A later plan should choose a coherent vertical slice that preserves present capabilities and tests actual new claims; an extraction commit alone must not be described as a delivered reusable programmable toolkit.
