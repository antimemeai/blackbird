# Command-results inspector

Work branch `work/tui-reference-2026-10-15`, previous delivered checkpoint
`45b14541749119399ce2f6e4f07aee9c082c8e41`. This tranche implements the conversation
command-results instrument and the shared native collection/inspector it uses.
The current-session center and separately owned Lua composition remain subsequent
units of `arconaut-rhi.3`.

## Behavior and ownership

Ctrl-O opens or closes inspection without moving the existing Composer's draft or
byte cursor. `/jobs` opens the first catalog page. Arrows, Home/End and PageUp/Down
navigate the focused collection or result; Tab changes focus, Escape or committed
text returns to editing, and Ctrl-Left/Right pages the catalog or retained output.
Bracketed paste keeps embedded controls literal. Existing foreground PTY routing
and global stop/quit retain their priority. Selection never runs a command.

The immutable scene uses the actual command ID and bounded retained source ranges.
Its result distinguishes lifecycle, custody, terminal retention, sealing and
retained/received frontiers. The native owner query does no allocation, raw work,
wait, drain or journal scan: at most eight catalog rows plus independently selected
detail,4096 retained bytes and16 source chunks, with bounded logarithmic seek.
Missing resident data does not imply missing archived history. The generic control
has no command/runtime dependency and paints only inside checked cell rectangles.

The conversation adapter refreshes outside input and painting. A changed
observation requires nonblocking exact raw CONTROL admission before publication;
recording pressure preserves the accepted scene and successful apply reference,
with visible stale feedback. Site4087 names real native2008 source ranges rather
than duplicating output. Native4041 frames retain the displayed bytes. See the
[raw schema and failure contract](RAW_OBSERVATION.md). The component preserves
stable keyed selection, independent detail reading anchors and wide/narrow return.
There is one Composer and no new production dependency.

## Direct checks completed before the full source gate

| Mechanism | Observed result |
| --- | --- |
| Permanent actual-main old-source oracle | Ctrl-O header absent with a nonempty draft: behavioral RED, real terminal artifacts retained. |
| Pure owner oracle | Actual old allocating read fails under refused allocation; five current Mac Debug and Linux Release modes pass. [Owner report](OWNER_REPORT.md). |
| Generic component oracle | Actual Mac/Linux stub behavior fails the specification groups; current native component and prior host tests pass2/2. [Component report](COMPONENT_REPORT.md). |
| Adapter semantics and actual raw originals | Four Debug modes pass: semantic, raw decode/reconstruction, standalone physical pressure and separate-producer runtime pressure. |
| Microsoft actual native main | Ghostty and Alacritty pass exact draft/cursor continuity, two real IDs/results, narrow resize, literal paste and exactly two original command effects. |
| Blocked physical stdout | Three episodes pass: existing conversation, pane/graphics and new inspector navigation, all preserving typing/paste/cancel/resize and drafts. Cancellation is observed before stdout drain. |
| Readable styled expectations | Sixteen compare successfully: two explicitly generated/reviewed inspector fixtures; all fourteen earlier expectations unchanged. |
| Existing command ownership integration | Terminal, blocked stdout, command-job PTY/station and Microsoft inspector pass5/5 in31.03s. |

The old `/jobs` JSON test scrape was deliberately replaced by an explicit Lua
native process read matching exactly the fixture's actual PID to one canonical
job ID. All effect/custody/task assertions remain. The first adapter test compile
missed declarations from context.hpp; it is a preserved setup failure, not RED.
The original Mac reference link eventually completed normally without signals or
restart; its old-path oracle then failed the actual allocation contract. No new
link cause is claimed. Source, logs and real terminal artifacts are under ignored
`context/tui-command-inspector-2026-10-16/`; test failures retain fixture paths.

Root completed one independent implementation/oracle review each for the command
owner and generic component, with no concrete defect. The independent integrated input/raw/adapter review also found no concrete
defect; its [scope and disposition](REVIEW.md) are recorded. [Affected sanitizers](SANITIZERS.md) pass12/12 ASan+UBSan and6/6 TSan.
Final Linux checks, full installed source gate and matched performance are pending. This document will be updated with their actual
outcomes; the table above does not imply those qualifications have completed.

The first installed source gate fails205/206 Debug in286.42s: existing
raw_recorder_cuts hits its unchanged60s watchdog at the explicit-recovery episode.
The full output is preserved as full-debug-attempt-01.log in the declared context
directory. This repeats queued arconaut-3wt; the hook blocks the commit and does
not reach lint. A bounded isolated stage diagnostic reproduces a test-interposer
deadlock before recover(): handle retirement waits while the recorder seam waits
for a later stop signal that the parent cannot issue until retirement returns.
The fixture-only [synchronization repair](RECORDER_FIX.md) passes a
deterministic old-selector RED, current full Mac and Linux fixture checks,
owning-file lint and one independent root review without changing any original
assertion or watchdog. No raw data
loss or command-inspector source cause is inferred from this failure.

The final Linux candidate passes57 scoped Release checks in34.99s, then the
fixture-only overlay passes raw_recorder_cuts1/1 in0.17s. Its native input tree is
991b8fb77519afebefdcfb8a5b30a98916d97eb81616ab71c467dcc770c15820; the
product binary remains451271fc403b49a9b1577fe9f17366a22838a2f30320c983969c395525a4c0ca.
The earlier candidate and qualification remain separately recorded. The installed
full Debug/source-analysis hook is being retried; matched timing still waits.

The second installed hook passes206/206 Debug in226.78s, including recorder0.79s,
then rejects15 native diagnostics across four new translations. The
[owning fix plan](LINT_FIX_PLAN.md) removes the empty-control adapter state,
interchangeable renderer scalar parameters and unproven optional accesses;
[test-helper fixes](ORACLE_LINT_REMEDIATION.md) retain all exact assertions, including
the same invalid Ink byte255. All four actual owning translations now pass tidy.
The component2 cases and adapter4 cases remain green; actual-main Ghostty, blocked
stdout and16 unchanged expectations pass3/3 in9.91s after relinking. Affected
ASan+UBSan6/6 and TSan4/4 rechecks pass without diagnostics. The final full
source hook and final matched Linux overlay/checks follow these changes.
