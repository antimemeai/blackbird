# Composer escape recovery

2026-10-15. Owning unit: `arconaut-600`, prerequisite to the native TUI extraction.

`Composer::flush_escape` currently clears only a lone ESC in an open palette.
Outside a palette it retains ESC and truncated CSI/SS3 prefixes indefinitely.
`TerminalUI::run` checks the existing 50 ms deadline only when stdin is not ready;
an unrelated byte queued after that deadline can still be consumed as an escape
continuation. This unit changes those two recovery boundaries, not the supported
keyboard grammar. Existing Alt-word keys, fragmented legacy controls, bracketed
paste and submitted workflow selection remain.

Source grounding: `src/terminal.cpp` Composer/terminal polling; current
`tests/terminal_test.cpp`, `scripts/check-terminal-pty`, `tests/ui_raw_test.cpp`;
acquired xterm control-sequence reference in
`quarantine/tui-2026-10-15/input/documents/xterm-ctlseqs.html`. Xterm establishes
CSI/SS3 and bracketed-paste grammar; the 50 ms ambiguity deadline is Blackbird's
existing policy, not a protocol-required duration.

1. Add direct red cases: expired lone ESC outside palettes, expired truncated
   CSI/SS3, unchanged draft/cursor on recovery, next literal/control byte retained,
   and completed byte-fragmented controls. Preserve paste-end prefixes across
   explicit timeout service while in bracketed paste.
2. Add a real PTY case: make the owner parse ESC and redraw, stop the child,
   wait past its deadline, queue a following byte, then resume. The queued-ready
   path must preserve the byte. Always resume a stopped child before cleanup.
3. Make flush discard expired non-paste prefixes in every composer mode, preserving
   palette dismissal for lone ESC. Service expiry before ready-input dispatch as
   well as during idle polling. Do not add new key encodings or reinterpret paste.
4. Run focused terminal, PTY and raw UI checks. Retain raw bytes/credits and native
   capture ownership; no new producer transforms or capture format. Root performs
   the one independent code review and combined source gate after handoff.

Resource cost is bounded: composer prefix clear, one existing clock/deadline check
on each poll tour, no scan of draft/history and no added thread/library. The test
uses one short controlled process suspension; no provider/account calls. The
legacy timeout remains ambiguous for genuinely slow fragmented escape controls;
this unit does not claim enhanced keyboard protocol support or a universal input
latency bound.
