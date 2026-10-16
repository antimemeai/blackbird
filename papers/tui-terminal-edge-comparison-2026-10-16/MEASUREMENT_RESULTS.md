# Terminal edge measurement unit

The first development qualification of the external observer and separate report
passed on the actual Mac Debug fixture, first attempt:16/16 normal processes.
Eight finite batch processes each emitted all six scenarios at four frames; eight
raw PTY processes exercised all four policies at110x35 and80x24. These are
endpoint/lifetime qualification executions, not the matched performance campaign
and not a basis for ranking policies.

Every PTY emitted and completed exactly ten prepared synchronized packets, for
sequences0 through8 and11. All INPUT1 through12 records arrived; the edit, both
resize commands and quit were observed before output release. Each retained
slave queue accepted1024 filler bytes before refusal; native frame8 stayed
pending and final accepted frame11 completed after drain, followed by normal
exit. TerminalMode's unmatched defensive synchronization close did not become a
spurious frame. No forced exit, missing observation or failed process occurred.

Originals and raw parent samples remain at
`context/terminal-edge-observer-qualification-2026-10-16-first/`.
The independently derived report completed without discarded samples.

- Fixture binary SHA256: `5f4de3471c19f521dbb39868153688467f1d59a93ef69e56af1c6f38f6e5daf2`.
- Observer SHA256: `93453eab0623c8f1dabdd14ef89c1e8864ff246a606b53b66f1f21b32c9810cb`.
- Source identity: development working tree, not a delivered commit or the future
  content-identified Linux source snapshot.
- Profile: Mac Debug development only. No performance inference follows.

The full physical Ghostty/Alacritty qualification and isolated Linux Release
build preceded the30-trial matched timing round described below.

The first Linux matched attempt then stopped after44 process samples:43passed
and one collector failed in addressed80x24 trial4. All ten frames and native
INPUT1–12 were present, including READY11 after release. The wait predicate
polled child termination, then an independent second poll treated that same
termination as a reason to break and report failure. A child exiting between
those polls triggered the contradictory branch. The collector did not record
that child's return code; its original final code remains unknown. All44samples,
terminal/event originals and source are retained, rather than replaced by later
results.

Root stopped only its own running source hook before commit (the hook exited
interrupted; no green gate or commit claim), reviewed this concrete same-unit
correction and released the tracked-file constraint for it. The observer now
uses its success predicate and unchanged5s deadline without the second poll,
keeps the explicit exit-code-zero requirement, and records return codes on all
finally paths. No native code, scene, compiler, timeout or assertion changed.

Corrected actual Linux development qualification passed first attempt:12/12
normal exit-zero processes, all three surviving policies at both geometries,
six batch-all executions at four frames per scenario and six full raw PTY
episodes. The exact corrected observer hash/patch and original44-sample attempt
remain under ignored campaign context. This is qualification of the corrected
observer, not a performance ranking. A fresh30-trial matched round is authorized
because the original observer aborted; it is not a repeat of a valid result to
improve tails.

The corrected matched Linux Release round completes360/360 normal exit-zero
processes on the unchanged native binary. It retains216,000 batch frames,1,800
PTY packets,2,160 input acquisitions and all720 blocked edit/resize/quit
acquisitions before drain. No failed or forced process occurs in that round.
The [published competition](performance/README.md), [204 metric rows](performance/ALL_METRICS.md)
and [raw samples](performance/round/samples.jsonl) give the actual CPU/byte and
PTY p50/p95/max tradeoffs. The first44-process attempt is published separately;
none of its observations are pooled into the corrected round.

Source0c62e964 passes the full installed source hook:257/257 Debug385.30s plus
all253 actual C++ units and owned Lua checks. The exact two differences from the
compiled snapshot are CMake survivor-test registration and the external
observer wait correction; native algorithms, fixture, scenes and transport are
unchanged. This experiment selects integration direction, not a repaired actual
TerminalUI or historical app before/after delta.
