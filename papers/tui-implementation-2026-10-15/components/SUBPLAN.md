# Native keyed scene and collection unit

2026-10-15. Implement the first small native component unit required by the proposed command center, without changing terminal rendering or claiming the complete component system delivered.

The unit owns a bounded immutable scene snapshot, checked view/node incarnations, keyed collection rows, local focus/selection/scroll anchor, one retained shell draft and a deliberately armed target. Scene publication validates the complete candidate before changing live state. Reordering preserves identity; deletion chooses a deterministic nearby location and disarms prior intent; replacing an item's domain target also disarms. A candidate cannot overwrite a newer native draft. Domain targets remain references and grant no execution authority.

Expose these as `blackbird::tui::SceneHost`, typed candidate/snapshot/handle/reference structures in `tui_components.hpp`, implemented in `tui_components.cpp`. No provider/task/workflow/terminal dependency, JSON, renderer, Lua callbacks, domain admission or action replay is added. The host is single-owner; queued cross-thread callers must transfer owned candidates. Public candidates are copied into private publication storage so pre-existing mutable borrows cannot mutate a published scene.

Direct red cases use independently stated expected selections, anchors and targets after insert/reorder/removal, malformed/duplicate/cyclic candidates, stale scene/draft bases, replaced incarnations and stale node handles. Then implement the state transitions and run the single new target; inspect affected diagnostics. Root owns CMake registration/integration and the one later bounded code review. No repository-wide assurance campaign is initiated by this unit.
