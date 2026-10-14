# Same recovery fixture publication correction

The mandatory gate preserved at
`context/remaining-owner-source-gate-recovery-main-failed.log` failed129/131:
the existing session_recovery fixture reported its final interrupted-wrappers
assertion, `recovery killed prior command or replayed its effect`. That combined
assertion does not distinguish absent marker, incomplete marker bytes or repeated
effect. The original failed fixture removes its directory, so the old gate
cannot establish which operand failed from that message alone. Do not claim
an actual recovery containment/replay defect from it.

Source evidence: `tests/session_recovery_test.cpp:416–423` waits only for path
existence. The actually detached shell at449–451 redirects its final printf to
the done path. Opening/truncating that file makes it visible before printf has
written its complete bytes. At547–549 the fixture immediately reads that visible
file and requires `DONE\n`. Real scheduling can expose an empty/prefix file
without a killed child or duplicate effect. The earlier EFFECT synchronization
and cleanup wait use the same insufficient existence predicate.

Finite plan, before source correction:

1. Preserve a direct native reproduction of this exact observation error using
   a newly owned shell: append the one effect, open the done descriptor, stop
   the shell before its final native write, observe the original existence/full
   byte predicate fail, then resume and require exact DONE/effect bytes. This
   controls the real scheduling cut; it fabricates no native return or payload,
   adopts no prior PID and involves no recovery/product command replay.
2. Replace fixture existence synchronization with a complete-byte publication
   predicate. Use the same five-second deadline and five-millisecond polling;
   only exact independently declared EFFECT/DONE bytes satisfy it. Incomplete
   data is pending, never accepted as completion. A dead child or missing/wrong
   marker still fails within the unchanged bound. Keep the final exact DONE
   read and exact one-effect read; keep the pre-release absent-DONE check,
   original written-file assertion, UNKNOWN/linkage/no-dispatch assertions and
   actual nested workflow/Lua/exec crash seam unchanged.
3. Provide useful failure diagnostics for the existing final combined assertion
   (completion predicate and actually observed done/effect bytes), without
   accepting a prefix or changing the predicate's expected content.
4. Run the same fixture TU static checks, request Root's matching target link,
   and run only session_recovery. No production edit is justified by the source
   evidence yet. No deadline extension, new recovery mode, PID action or wider
   dynamic campaign is introduced.

The complete-marker condition is an independent native byte oracle, not a test
of implementation spelling. It establishes the prior shell reached its actual
post-release marker while its effect count stayed one. It does not claim old
process containment or successful PID adoption. Root retains the full gate and
final source integration.

The current production `recover_coding_session` was also inspected: its custody
verifier validates the abandoned metadata/input linkage, classifies local and
wrapper operations, reconciles the protocol, and appends terminal UNKNOWN
observations plus the sticky uncontained-exec program marker. This examined
seam does not invoke native child controls, adopt a PID or dispatch the old
effect. Its production source is unchanged by this correction.

## Finite red, repair and direct result

The context-only scheduling reproduction is preserved in
`context/session-recovery-publication/native-red.py` and `native-red.log`.
It exits1 deliberately after obtaining a real stopped shell with
`exists=True`, actual empty done bytes and exactly `EFFECT\n`. Resuming that
newly owned fixture shell completes with exact `DONE\n` and the same one
effect. This proves that the old completion predicate can reject a live writer;
it does not retroactively prove which operand failed in the discarded gate run.
No permanent helper/test layer was added for that demonstration.

The existing fixture helper now waits for exact independently expected contents
within the original5s/5ms bounds. Its actual crash synchronization waits for the
complete first effect, and cleanup waits for the complete final marker. The
final assertion still independently reads exact DONE/one-effect bytes and
retains the pre-release absent-DONE check and all UNKNOWN/identity/no-dispatch
checks. On failure it reports actual completion/existence and observed marker
and effect bytes rather than conflating every failure with containment/replay.
The shell program, effect count, recovery mode and old command lifecycle were
not changed.

The matching strict `session_recovery_test` target link is green in
`context/session-recovery-publication/matching-build.log`; its changed-TU
clang-tidy is green (exit0) in `fixture-tidy.log`. The existing native CTest
case passed3.80s in `native.log`. Formatter/diff checks are clean. Root owns the
full source-gate retry; this direct result is not a claim that the whole prior
129/131 gate has become green. Source/test/paper work is idle, with no compiler,
static worker, native child or shared Ninja writer held by this owner.
