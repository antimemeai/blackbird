# Owned frame implementation subplan

Status: implemented in the [retained candidate](SOURCE_STATUS.md), now restored
in the working tree after the operator reopened the failures.
The required final source gate failed; this checkpoint publishes evidence.

The presenter makes the existing one-frame output convention explicit. It owns
the final packet, staged grid and painter; its writer borrows only that stable
containing storage. Preparation does not advance committed cursor/grid state.
Completion swaps grids without allocation and publishes tiny/normal geometry.
TerminalUI retains native input, polling and cancellation; graphics observations
are staged locally and published by the same completion service.

Direct checks extend the existing chat-view pipe case for owned packets,
backpressure, refusal, exact suffix and completion observations, and the raw UI
fixture for allocation denial and native capture lifecycle. The existing terminal
screen oracle will consume presenter output. A separate colleague owns the
actual TerminalUI blocked-controls fixture. No new dependency or queue is added.

Storage stays two viewport grids and reusable packet capacity. Completion swaps
the old committed grid back into build scratch; it never copies the viewport
after writing. There is one prepared or writing packet, and no supersession.
Transport budgets remain eight writes of at most 8192 bytes per service call.

Implemented the containing presenter and migrated the normal, graphics and tiny
TerminalUI branches plus editor drain to one completion service. The painter
retains committed cursor separately from prepared cursor. Graphics augmentation
uses the owned final packet directly; placement/load observations publish after
full output. The low-level ChatOutput API remains available for existing direct
transport callers.

Red syntax compilation failed on the absent ChatPresenter API. Focused Debug
build then passed; chat_view and ui_raw passed, including real pipe pressure,
all cuts through UTF-8/CSI, allocation-denied completion, failed preparation,
cursor-only abandonment and captured binary partial output. The VT oracle now
reads actual presenter-emitted bytes and checks explicit cells/styles/cursor
through resize and tiny→normal. Its first explicit row expectation forgot the
12-column clipping; corrected the fixture expectation and the direct recheck
passes. No product behavior or timeout changed for that fixture correction.

The independent product challenge is owned by the root colleague. This lane's
independent challenge of the separately authored actual TerminalUI blocked-output
fixture found no actionable issue in the EAGAIN/side-channel ordering, native
cancel acknowledgment, finite cleanup or exact draft/scroll/resize assertions.
Full qualification and tranche measurements remain root-owned.

Root's integrated product challenge found no actionable defect in containing
storage/destruction, preparation failure, completion swap/cursor publication,
tiny/graphics capture handoff or editor/input integration. The actual blocked
fixture passes on Mac Debug: native stdout reports EAGAIN; the worker observes
cancellation before the observer drains stdout; after resize the exact draft,
cursor and ROW034–063 at scroll16 appear; clean exit retains the byte cursor and
restores input mode. The required full source gate and affected Linux profiles
follow; their eventual results are recorded separately rather than inferred from
the focused checks.
