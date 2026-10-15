# First TUI source unit qualification

2026-10-15. Source checkpoint `a68fec4`, branch
`work/tui-reference-2026-10-15`. Native C++/Lua stack plus development-only
Microsoft tui-test0.1.0 and owned Neovim-style screen expectations.

| Check | Result |
| --- | --- |
| Required full Mac Debug source gate | 146/146 CTest cases pass, 286.00 seconds; owned C++ formatting/dependency/static analysis and Lua syntax/format/language-server checks pass; hook creates source checkpoint |
| Affected Linux Debug, Release, ASan | Seven cases per profile, 21/21 pass; profile totals15.43/13.89/22.24 seconds |
| Corrected final PTY fixture, Linux Debug | Separate direct recheck passes, 9.01 seconds |
| Additional affected Mac Release, ASan | Incomplete: builds stop progressing at Apple linker processes; no CTest result claimed |

The seven affected Linux cases are `terminal`, `terminal_pty`, `chat_view`,
`chat_render_oracle`, `tui_components`, `tui_snapshots` and `tui_microsoft`.
They run the final native sources before the final PTY completion-oracle correction
described below. The corrected fixture subsequently passes Linux Debug and the
full Mac Debug gate. Do not describe all21 Linux runs as using that later marker.

Completed direct checks:

- Microsoft Ghostty/Alacritty current-executable scenarios pass palette styles,
  input/paste, resize, one deliberately submitted native effect and exit.
- Known-cell acceptance and wrong-cell refusal demonstrate external assertions.
- Final interrupted-body recheck preserves primary failure/commands and closes
  its unique terminal session; stale artifact/profile metadata is refused.
- Eleven native styled expectations and selected semantic assertions pass.
- Native scene identity/publication/local-state tests and50 allocation cuts pass.
- Initial ESC direct Composer red and repaired cases pass; independent review
  replaces the unsynchronized new PTY timing oracle with explicit native deadline
  dispatch cases. The final source gate executes that version successfully.

The first full Mac gate passes145/146 and fails the existing final PTY quit case
after deliberate UI-state-save refusal and retry. The unchanged diagnostic rerun
passes, leaving the observed stall unexplained. Source inspection finds an actual
fixture gap: a retained earlier `[exit 0]` row and the current shell effect do not
establish that the current native call has returned. The final retry now emits a
unique marker after the call returns and waits for it before quitting; the exact
effect-count assertion and existing busy-quit cases remain. No deadline is enlarged.
The corrected full gate passes, but this does not establish a product deadlock
repair. `arconaut-xbf` retains the unresolved quit/settlement observation.

The additional Mac Release/ASan builds compile through action60/68, then four
Apple `ld` processes for the archive helper and conversation test do not complete.
Their target output timestamps remain from earlier builds. `ps` and a one-second
`sample` of an affected process also fail to return; no stack or root cause is
obtained. Only these owned build/probe processes receive TERM/KILL. These profiles
remain incomplete under `arconaut-ew5`; Linux sanitizer/optimized passes do not
stand in for unexecuted Mac results. No build-system, tool-policy or deadline
workaround is introduced in this unit.

Local original logs remain under ignored
`context/tui-implementation-2026-10-15/`: `source-gate-first.log`,
`source-gate.log`, `linux-check.log`, `linux-fixture-recheck.log`,
`mac-release.log`, `mac-asan.log` and `mac-link-stall.json`.
The full Linux captures are `context/linux/run-dh20ryjl/checks.log` and
`context/linux/run-422o4mw0/checks.log`. Microsoft failure/final-state artifacts
remain under each build's `tui-microsoft/` directory.

One integrated code challenge and corrections are recorded in
[resolution](review/RESOLUTION.md). Existing ordinary PTY/custody and source/profile
checks remain. No GUI/font/device, throughput/latency, complete component-host,
provider-account or eval result is claimed by this unit. `arconaut-rhi.3.1` and
the focused ESC issue `arconaut-600` are complete; `arconaut-rhi.3` remains in
progress for the native host, Lua composition/action bridge and actual view reuse.
No main integration or running executable replacement occurs in this checkpoint.
