# Input repair and snapshot code challenge

2026-10-15. One bounded independent challenge of the changed `src/terminal.cpp`, `tests/terminal_test.cpp`, `scripts/check-terminal-pty`, plus `tests/tui_snapshot_test.cpp` and `tests/snapshots/tui/{check.py,expected,README.md}`. Read-only inspection; no production mutation, counterfactual/mutation run, extra build or test campaign. Root owns one recheck and any fixes.

## P2 — PTY suspension does not establish the pending-prefix precondition

**Location:** `scripts/check-terminal-pty:290–319`, especially sending `marker + prefix`, waiting for the visible marker, then issuing SIGSTOP. The implementation report calls this a suspension after the parsed-input redraw.

**Fault:** Seeing the marker establishes that its ordinary text reached a frame. It does not establish that the trailing prefix was parsed or remains pending when the process stops. A PTY read can split between the marker and prefix; the rendered marker can therefore precede prefix handling. Conversely, the owner can service the existing 50ms ambiguity deadline before the test process gets scheduled to send SIGSTOP. `os.waitpid(...WUNTRACED)` confirms stopped state, but neither fact about the prefix.

In the first case, resumption can start a fresh ambiguity deadline just before the queued `x`, producing a scheduler-dependent failure. In the second, the prefix is already gone before the queued-ready-input scenario begins; the test can pass without distinguishing deadline service before readable input from the former idle-only ordering. The Composer direct cases still test `flush_escape`, and the reported original red is useful, but they do not establish this real-loop ordering precondition.

**Direct fix:** Give the owner/test a causal synchronization point after the prefix is actually installed and before deadline service, and suspend/release at that point; then queue `x` only after the deadline. A deterministic native wait/deadline seam can test the ordering directly while the PTY case continues to test actual transport and draft preservation. If that seam is deliberately deferred, narrow this PTY case's claim rather than treating displayed preceding text as a parser-state acknowledgment. Do not “fix” the test by merely increasing sleeps or inventing a new production timeout. The correction belongs to this one direct race oracle, not another layer of tests certifying it.

## No further substantive finding in inspected scope

The production repair clears expired non-paste prefixes and retains paste bytes/termination across expiry service. Moving expiry service before the readable-input branch addresses the identified consumption path. Existing mode semantics and explicit original/capture boundaries remain unchanged in this diff. This review makes no claim that the decoder now supports every protocol or malformed stream.

The snapshot comparator uses a fresh actual directory, compares emitted and expected case sets in a normal full run, fails missing/removed expectations, retains explicit update commands and writes actual/diff artifacts without rewriting baselines. Its selected-case mode intentionally checks only that case. The producer records actual cell glyph/tail bytes, Ink role, non-default widths, cursor and selected semantic state, with independent assertions for command identity, hidden draft and pasted text. Expected content fits the stated fixtures.

Composer/palette composition is test-local and cannot catch a change confined to the private full-window branch; both README and delivery report explicitly state that scope. The snapshots do not pretend to qualify terminal painting, actual theme RGB, full Unicode conformance, devices, external effects or the new native scene host. Those are separate direct contracts. No additional snapshot change is requested by this challenge.
