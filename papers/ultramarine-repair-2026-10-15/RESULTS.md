# Ultramarine command and pane repair

2026-10-15 local; checks continue into2026-10-16 UTC. Source commit `65e5877dda1cc5b965b276a65c57668be0e1e038` passes the normal installed source hook:
160/160 Debug tests in260.95s and full C++/Lua analysis, including LuaLS.
No source-hook override or rule suppression was added.
Baseline9cea416 (native676120c), branch work/tui-reference-2026-10-15.

The screenshot combines two defects. The registry retains completed commands
until32 resident records exhaust admission. The refused build never starts, but
the old tool/turn path labels it unknown/stopped, and the model then explains an
interruption without inspecting the relevant facts. Independently, ordinary
spaces separated by zero-width joiners occupy three physical cells where the
renderer models two. Text crosses the pane divider, then differential rendering
leaves vertical debris. Both causes are reproduced directly from retained logs.

## Delivered behavior

- Keep8 live/32 resident bounds. Retire the oldest safely settled resident job
  under pressure, preserving originals and output lookup. Never retire live,
  unretained, pending-output, waiting or control-owned work. Join outside the
  registry mutex. Late controls cannot target a replacement job.
- Return an ordinary retained tool failure for an actual registry refusal:
  capacity, exact resource/count/limit, attempt locator and not_started. The
  provider receives function_call_output and can continue. Preserve sticky stops
  for capture/retention failure and uncertainty after actual execution. No retry.
- Gate pictographic joining on pinned Unicode17 EP/Extend properties. Preserve
  ordinary spaces and barrier characters; valid emoji still join. Correct the
  owned terminal oracle's matching assumption. Carry the distinction between
  known refusal and unknown effect, actual cells and modeled cells, into the
  TUI doctrine and command contracts.

[Plan/theory](PLAN.md), [command study](COMMANDS.md), [failure contract](FAILURES.md),
[independent failure review](FAILURE_REVIEW.md), [pane study](PANES.md), and
[Unicode sources/license](UNICODE_SOURCES.tsv) retain the research and bounded
implementation review. Normative data is acquired intact in ignored quarantine;
no production terminal framework or new library is adopted.

## Direct qualification

Native registry and engine episodes fail on the old implementation at command33
and pass40 commands with bounded residency and exact first-output lookup.
Provider fixtures prove actual live/resident refusal, forbidden command absence,
same live PID survival, exact result/attempt linkage, continued tool use and
sticky blocking when refusal-result retention itself fails. Existing post-start
capture/interruption contracts remain tested.

Both pinned Microsoft tui-test terminal engines reproduce the screenshot from
original bytes, and reject the old scanner on a public synthetic105×52 fixture.
The repaired scanner passes both. Native literal cell expectations,12 readable
Neovim-style snapshots, and a physical PTY pressure episode check the distinct
width, regression and responsiveness faults. The latter observes actual EAGAIN
and Ctrl-C before draining stdout, then exact divider/gutter/task/composer cells.
The Unicode fix is bounded; it does not claim full UAX29 conformance.

Mac TSan five affected cases pass112.25s. The new40-child workload is split from
existing suites, preserving their30/45second deadlines; each new case gets60s.
Linux Release24/24 pass26.28s. Linux ASan first passes23/24; the new physical
pressure fixture fails because rejecting4KiB does not mean a small packet cannot
fit. Fill through one byte and maintain pressure during the existing no-drain
wait; affected ASan1/1 passes0.92s and final Release1/1 passes. Preserve these as
separate invocations, not a claimed24/24 ASan round. Its owned temporary remote
workspace is cleaned.

An initial complete Mac Debug160/160 passes254.38s, but its staged tree becomes
obsolete after that fixture edit; analysis is interrupted and no commit claimed.
The next gate passes159/160 and exposes an existing raw_runtime cleanup race.
[Independent review](RAW_FIXTURE.md) establishes why signaling a recorder after
its supervisor dies is invalid. Observe the stopped parent, preserve the actual
queued-RPC barrier, kill the recorder while child identity is pinned, then the
supervisor. Mac affected1/1 passes1.99s; Linux Release1/1 passes0.12s. This is
arconaut-gbu, a test-only repair; no deadline or production body changes.
The final full source gate passes on the corrected staged tree, as recorded above.

Working logs and original bytes stay under ignored
context/ultramarine-repair-2026-10-15/ and
context/ultramarine-triage-2026-10-15/. Failed builds/gates remain retained,
including Clang18 array CTAD corrected by explicit dimensions rather than flags.
The original session is read-only: no launcher/recovery/provider/build command
from that conversation is replayed. AFL and user artwork remain untouched.

## Performance and delivery limits

[Matched performance](performance/README.md) retains120/120 normal runs and960
endpoint observations, all samples and source/binary identities. Input p50 wide
2.639→2.580ms and narrow1.431→1.469ms; paste wide41.987→40.470ms and narrow
39.834→39.799ms. Wide startup p95 worsens29.283→95.466ms and max31.403→347.610ms.
Shared host work remains uncontrolled; do not claim a general speedup or attribute
those tails to a specific source change. arconaut-f8f retains that observation.
The final raw_runtime test-only follow-up does not alter either measured binary.

The repaired product issues arconaut-zh2 and arconaut-6ao, and fixture issue
arconaut-gbu, are closed after that source gate. The performance-tail observation
arconaut-f8f remains open.

This delivery publishes the work branch. Main integration and running-executable
activation remain separate. Another authorized lane already integrated Paper
provider support into master and retired its old checkout. That checkout's absence
is intentional; do not recreate it or reopen the original session to validate this
repair. The current work branch does not itself supply Paper provider support.
