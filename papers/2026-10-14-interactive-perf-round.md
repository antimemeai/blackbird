# Interactive performance round after repairs — 2026-10-14

All 150 startup/input trials and both actual native command-control sessions
complete on repaired source **66547c6**. Every start renders the completed input
frame and unsubmitted marker, then exits normally without forced shutdown. Each
control session preserves every transferred keyboard byte exactly, retains one
launch/PID per command, and exercises actual background/foreground, input,
resize, Ctrl-C, full-queue background/stop, quit/reap and blocked-plain-stdin
progress. No product source changed or optimization was attempted.

## Startup and typing

Parent monotonic timestamps bracket launcher spawn to the completed input frame,
then writing an unsubmitted marker to its committed render. The 110×35 PTY drains
output and answers actual terminal queries. Graphics are enabled under Ghostty
identity. No inference prompt or external provider request is submitted. Ordinary
OS caches remain; no sample, first touch or scheduling outlier is excluded.
Each scenario gets 30 fresh processes in round-robin order. Fixture cloning or
copying is excluded from timing.

The reopened seed is a real native session produced by 100 workflow_registry
calls inside one local Lua turn, followed by normal quit. An independent closed
raw-journal parser checks 100 distinct workflow request/result pairs and 101
bridge pairs, including print, with unique identities and no ingest-failure
records. Each seed has 91,183 unique raw events. This is local-tool history,
not a large model conversation. macOS privately APFS-clones its seed per trial;
Linux privately copies its own native seed.

| Host / scenario | Startup p50 ms | p90 ms | Max ms | Typing p50 ms | p90 ms | Max ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| macOS / previous-segment | 125.370 | 143.219 | 186.621 | 1.989 | 2.918 | 4.917 |
| macOS / repaired-fresh | 124.849 | 157.626 | 210.385 | 2.643 | 4.678 | 7.394 |
| macOS / repaired-reopened | 145.414 | 223.347 | 332.287 | 2.561 | 4.875 | 7.717 |
| Linux / repaired-fresh | 31.458 | 35.529 | 57.592 | 1.216 | 1.596 | 2.061 |
| Linux / repaired-reopened | 173.807 | 193.596 | 248.304 | 1.191 | 1.408 | 1.725 |

The previous-segment executable is retained source ca6747b, native SHA e550adc…,
and is not the repaired source's exact parent. This same-round reference measures
local generation behavior; it does not isolate instrumentation's causal cost.
The macOS fresh median differs by about −0.52 ms, while typing increases by about
0.65 ms. Neither establishes a fixed regression budget from 30 ordinary-cache
samples. Reopening adds about 21 ms median on macOS and 142 ms on Linux in this
fixture. These measurements do not identify the cause of that host difference.

macOS load averages are 3.49/5.89/5.74 before and 5.76/6.21/5.85 after; Linux
loads are 25.40/24.60/19.06 before and 23.69/24.23/19.05 after. No local builds
or other native Blackbird campaigns run in either host's timing window. Other
operator/system activity remains uncontrolled. The native executables, source,
launcher and executed observer hashes are retained in the host metadata.

## Native command-control observations

One real session per host provides endpoint observations, not distributions or
per-operation tail limits. The parent clock starts immediately before the actual
command, keyboard bytes or WINCH. Terminal readiness observations are event-driven;
child-file/PID predicates poll at most every 5 ms before OS scheduling. Endpoint
observations also include Python observer work. The existing exact PID/effect,
terminal-mode and closed-disk keyboard-byte oracles remain. Control graphics are
disabled. Background-plus-local-Lua endpoints include both the detach chord and
the successful subsequent Lua command; they do not isolate detach processing time.

| Host / endpoint | Observed ms |
| --- | ---: |
| macOS / foreground-command-to-rendered-lease-pipe | 61.985 |
| macOS / foreground-command-to-rendered-lease-pty | 60.628 |
| macOS / pty-keyboard-to-first-child-effect | 54.050 |
| macOS / background-chord-plus-local-lua-to-result | 129.995 |
| macOS / foreground-command-to-rendered-lease-pty | 88.143 |
| macOS / outer-winch-to-child-size-file | 86.375 |
| macOS / pty-keyboard-to-second-child-effect | 122.898 |
| macOS / foreground-command-to-rendered-lease-pty | 160.666 |
| macOS / foreground-command-to-rendered-lease-pty | 71.866 |
| macOS / saturated-background-chord-plus-local-lua-to-result | 147.629 |
| macOS / saturated-stop-command-to-child-reap | 62.156 |
| Linux / foreground-command-to-rendered-lease-pipe | 57.350 |
| Linux / foreground-command-to-rendered-lease-pty | 52.053 |
| Linux / pty-keyboard-to-first-child-effect | 10.879 |
| Linux / background-chord-plus-local-lua-to-result | 47.861 |
| Linux / foreground-command-to-rendered-lease-pty | 59.190 |
| Linux / outer-winch-to-child-size-file | 13.889 |
| Linux / pty-keyboard-to-second-child-effect | 11.302 |
| Linux / foreground-command-to-rendered-lease-pty | 57.300 |
| Linux / foreground-command-to-rendered-lease-pty | 68.639 |
| Linux / saturated-background-chord-plus-local-lua-to-result | 308.505 |
| Linux / saturated-stop-command-to-child-reap | 22.492 |

Both pressure cases offer 524,288 bytes, stopping after actual refusal plus outer
EAGAIN. macOS transfers 316,670 bytes, leaves 207,618 unoffered, and observes
saturation after 6.076 seconds with 801 blocked writes. Linux transfers 346,112,
leaves 178,176 unoffered, and observes saturation after 2.310 seconds with 81
blocked writes. The cases then test reserved lifecycle progress; these are not
524,288-byte throughput claims. All 319,234 macOS and 348,282 Linux physically
transferred outer-keyboard bytes equal native disk originals. Saturated stop to
actual child reap measures 62.156 ms and 22.492 ms respectively. Saturated detach
plus a completed local Lua command measures 147.629 ms and 308.505 ms.

## Retained results and limits

Executed dev-only [startup observer](perf-round-2026-10-14/interactive/startup.py.txt),
[control observer](perf-round-2026-10-14/interactive/command-control.py.txt),
[seed oracle](perf-round-2026-10-14/interactive/seed-oracle.py.txt),
[plan](perf-round-2026-10-14/interactive/PLAN.md), exact specs, native/launcher/driver
hashes, every startup sample and endpoint observation are tracked under
papers/perf-round-2026-10-14/interactive. Full PTY transcripts, closed raw audits
and fixtures remain in context/perf-round-2026-10-14, including the copied Linux
artifact archive. The startup observer removes each private trial session after
normal exit; it retains its complete terminal transcript and timing sample. The
native source seeds and control-session raw audits are retained.

This finite round does not establish cache-flushed cold/warm behavior, a large
provider conversation's reopen cost, every UI interaction, or fixed control tails.
No whole correctness gate or long-duration campaign is repeated for these
measurements and text reports. Raw transport and complete owner paths have their
own measurement reports and explicit boundaries.
