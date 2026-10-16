# Oxcart away-team interface

Purpose: let Oxcart's live browser converse with a real Blackbird coding peer;
remove any need to construct an owned web page or emulate terminal input.

The user directs immediate implementation, live-site work, and rigor without
certification ceremony. This worktree starts at committed HEAD, preserving the
dirty TUI checkout. Reuse `CodingEngine`, its existing providers/tools, the normal
session bootstrap/recovery, and owned BBM2 values. Add no dependency or JSON IPC.

Build one application connection over uint32 big-endian framed BBM2. Open selects
the session; turns carry a separate objective and source-backed page observations;
text/status/final events feed the browser panel. Cooperative stdin polling handles
cancel, a single replacement steer, close and EOF during native engine work.

Direct defects to prevent: partial frame corruption, oversized input allocation,
unknown-command execution, lost observation metadata, objective/evidence conflation,
steer replacing an old turn without cancellation/final, work after close/EOF, and
accidental stdout diagnostics. Native tests drive framed commands against an
injected provider and retained context; a process test opens/closes the real CLI.
The independent review is one review of these changes and direct oracles, followed
by fixes and one affected recheck. No browser fixture or release certification.

Resource use: one fresh native debug build and required pre-commit checks. Tests
use local native streams and scripted provider responses, no model calls. Oxcart
owns live-site browser validation and host presentation.
