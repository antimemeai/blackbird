# Navigation tranche performance

Compare the previous delivered viewport tree, committed baseline
`cbb206b629d936c13c9636b0a7bac999fa68e3e9`, with the paging and shared
navigation/action tranche. Baseline preparation may proceed while implementation
continues. Root releases the candidate only after its direct checks and single
independent review; timing starts only after all owned remote builds/checks end.

Reuse the unchanged historical actual-PTY observer and external report from
`papers/tui-full-viewport-2026-10-16/performance/`: observer SHA256
`f19ae5fb117cf969836301d8002336aa642d2476bac3bdee6f2248d50fbde377`, report
`dd4227cf1b29825ccc758f3aeb6da39fab23670310149b1d1935f585eb73650c`.
One serial round-robin comparison runs 30 trials for each variant and geometry
(110x35 and 80x24), totaling 120 processes and 960 endpoint observations.
Startup, unsubmitted text, 4 KiB bracketed paste, palette open/selection, resize
draft, shrink and restore retain their historical definitions. These scenarios
measure common application responsiveness, not paging latency. Direct native and
PTY checks establish the new navigation behavior separately.

Use a new persistent owned Neuroses campaign, Clang 18.1.3, libstdc++ 13,
C++20, Release, DebugOFF and instrumentation=none. Both variants use the same
previously qualified Lua 5.4.8 headers/library (library SHA256
`95fa79336051315a656c56e0271a2990ffb598adf6e8b1987ae0e239efff7fc2`).
The intact upstream Lua archive remains unchanged, SHA256
`4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`.
Capture exact declared native source scopes, archives, path/content/byte-count/
executable-bit identities, original modes, build commands and executable hashes.
Keep one Ninja operation per directory and serial owned remote builds, parallel2.

Run selected native rendering/custody, blocked output and editor checks on each
variant, plus root's relevant changed-path checks on the final candidate. The
unchanged historical observer is a committed-text endpoint, including scheduling,
PTY transport and observer parsing; it does not measure GPU/display presentation,
isolated painting CPU, providers or model latency. Do not submit inference turns
or activate a live launcher/session.

Preserve every attempt, failure, endpoint timestamp, original terminal stream
and isolated session directory. Do not discard warmups/tails, repeat a valid
round for better numbers, widen observer deadlines or reinterpret failed output.
Derive p50/p95/max and deltas only through the separate report; at 30 trials p99
is simply the maximum. Keep shared-host load context without global-idle or
source-causality claims. Do not survey unrelated processes or inspect, poll,
stop or otherwise interact with AFL. Large originals remain in ignored context
and the persistent remote campaign; tracked results include exact source/build
identities, raw endpoint records, all deltas and a historical tranche ledger row.

Root owns product source, source-gate checks, review/fixes, Beads, journal,
commit/push and any subsequent merge or activation. This lane owns only the
performance files and ledger, and prepares no candidate until source-ready.
