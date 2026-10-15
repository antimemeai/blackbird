# Composer ESC recovery implementation

2026-10-15. Implements the isolated `arconaut-600` decoder/deadline repair from
[SUBPLAN.md](SUBPLAN.md). Current legacy keyboard grammar remains; no enhanced
protocol, controller mapping or submitted workflow grammar is added.

`Composer::flush_escape` now discards an expired pending non-paste prefix in all
composer modes. Lone ESC retains its existing palette-dismissal meaning; an
incomplete CSI/SS3 expires without changing the draft or cursor. While bracketed
paste is active, pending literal bytes and its termination marker survive timeout
service between reads. `TerminalUI::run` now services expiry before input dispatch
even when stdin is already readable, so a later unrelated byte is processed as
new input rather than consumed by the expired prefix.

Direct Composer red was established before the source repair: the expired-prefix
flush assertion failed (`red-terminal.log` under ignored context). An initial
real-PTY suspension experiment also lost the next byte, but independent review
found its displayed marker did not causally establish parsed/pending prefix state.
That experiment was removed from the recurring fixture instead of claiming it
was deterministic or increasing its sleeps.

The production poll-turn policy is now a small `terminal_input_dispatch` function:
service expiry, then decide whether input is readable from actual poll events.
The actual TerminalUI uses that result before ready/idle branching. Direct native
cases supply explicit before/due clock points and idle/POLLIN/POLLHUP/POLLERR,
then assert pending state, redraw/read decisions and exact following draft text.
No wall-clock scheduling assumption or test-only callback is needed. Existing real
PTY input/mode/cancellation tests continue to exercise actual transport separately.

New Composer cases cover expired lone ESC, CSI introducer, CSI parameter prefix
and SS3; unchanged draft/cursor on expiry; following literal input and cancellation;
byte-fragmented supported cursor controls and existing Alt-word/page controls;
literal pasted command/control text and fragmented paste termination with timeout
service between each byte. The existing terminal/PTY cases retain palette,
completion, slash commands, explicit submit, raw control bytes, cancellation,
external editor, history and draft behavior.

Focused debug build of `terminal_test`, `blackbird` and `ui_raw_test` succeeds.
The selected CTest cases pass: `terminal` 1.72 s, `terminal_pty` 17.03 s and
`ui_raw` 3.14 s (21.90 s overall). That initial PTY version included a suspension case; after review, the revised
source gate runs the ordinary PTY scenarios plus deterministic native dispatch
cases as described above. Logs
remain in ignored `context/tui-implementation-2026-10-15/input/green-focused.log`.
Clang-format checks, the fixture's Python syntax check and scoped whitespace checks
pass. The debug cache required dependency recompilation during source/GLOB
reconfiguration; this is build cost, not another test campaign.

The root integrates the single independent review and combined source gate; no extra
hardening campaign, provider calls, dependency or production activation belongs
to this unit.

Cost/limits: no new thread, allocation on expiry, source scan or draft/history
walk. The existing poll tour gains deadline service on ready input; one expired
prefix causes a normal redraw request and then ceases to keep the composer in
pending-escape polling. No comparative latency measurement was performed. The
existing 50 ms ambiguity policy remains, serviced when the owner runs; genuinely
late fragmented non-paste escape controls can still be interpreted as a timed-out
prefix followed by ordinary input. Bracketed-paste transport can span longer
intervals without this timeout discarding its bytes. Raw event shapes, original
input bytes, capture credits and recorder ownership are unchanged.
