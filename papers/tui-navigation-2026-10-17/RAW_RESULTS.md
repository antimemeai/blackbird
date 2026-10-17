# Native view actions and unavailable-route qualification

2026-10-17. Extend the existing `ui_raw_test` palette and actual-terminal
scenarios. No production source changes or additional recorder/test framework.

The palette scenario uses the actual `Composer::views()` choices and Ctrl-\\
input. It checks typed `select_view` results for each of the four routes and the
untyped `view.picker` action, with exact stable strings:

- `view.conversation`
- `view.command-runs`
- `view.command-center`
- `view.result-comparison`
- `view.picker`

After real recorder shutdown, decode raw4010 records independently. Every result
must contain the expected action bytes, exact unchanged draft size/cursor and
zero source offset; its source must resolve to the actual raw4080 feed request
with the literal Enter or picker key. The draft and an intentionally nonterminal
cursor remain unchanged while filtering and choosing every route.

The normal existing PTY/TerminalUI queue scenario has no command-center context.
It now opens the native command inspector, requests the unavailable center, and
requires a completed emitted frame containing both the explicit current-session
owner refusal and the retained inspector caption. It then closes that inspector
and completes the existing real worker, queue, cancel and quit checks.

Dedicated Debug verification:

```
cmake --build build/paging --target ui_raw_test -j 4
ctest --test-dir build/paging -R '^ui_raw$' --output-on-failure
```

Result: 1/1 passes, 3.60 seconds. Logs:
`context/tui-navigation-2026-10-17/raw-build.log` and `raw-tests.log`.

The first run rejected the test's cursor setup: it incorrectly used Ctrl-B,
which Composer does not bind to cursor movement. Source inspection corrected
that setup to the actual left-arrow sequence ESC [ D. Preserve its original
failure as `raw-tests-wrong-key.log`; this is a test-authoring mistake, not a
product RED. The parent owns the prior meaningful product RED, the one source
review and integration qualification. No extra review layer is introduced.
