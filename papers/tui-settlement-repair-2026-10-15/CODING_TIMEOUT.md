# Coding-test timeout diagnosis

The operator explicitly reopens the two failed tests on 2026-10-15. This note
covers `arconaut-rln`; the separate quit-during-exec failure has another owner.
The retained final gate killed `coding` at its existing 60-second CTest deadline
with no output. Earlier identical production inputs passed in 26.10 and 24.67
seconds. A common command-lifecycle defect is a hypothesis, not an established
explanation for this timeout.

The existing test writes a `stage` string but prints it only after a caught
exception. CTest's external timeout does not pass through that catch. Its empty
output therefore identifies no stage or blocked operation.

Source and retained-file evidence instead support cumulative work as a serious
alternative. `main` performs independent file-selection, interruption, settlement,
mutation, process-capacity, provider-capacity, admission, capacity-warning, retry,
owned-route and retained-output fixtures before the ordinary coding workflow.
Each creates its own private journal and directly checks its own invariants.
Provider-capacity, retry and owned-route fixtures are also repeated by the
separately registered `coding_stream_capture` mode. Durable journal operations
can contend with other suite processes; a varying cumulative duration does not
establish an ownership stall.

Root's read-only inspection of the retained final fixture
`/tmp/arco-coding-DB3GQL` found successive activity through workflow-process,
workflow-bytes, workflow-records, admissions, retries, owned-routes and output,
then `source.cpp` at 20:38:12 UTC and the ordinary journal at 20:38:14 UTC.
Those original directories remain unchanged. This establishes late progress,
not the exact last executed statement or the host's cause of delay.

## Bounded repair and direct check

First add diagnostic stage entry and monotonic elapsed milliseconds in the test
only, including ordinary command cancellation, subsequent continuation, usage,
managed publication/provider settlement and restart cancellation. Keep all
assertions, the 60-second deadline and product behavior unchanged. Run the
instrumented `coding` directly through its existing CTest registration once the
root's serialized build slot is available. A last-stage report makes a persistent
wait actionable; continued late progress supports correcting the test's aggregate
shape instead of changing cancellation machinery without evidence.

If measurements establish cumulative independent work, decompose the independent
fixture groups into separately registered cases with the existing per-case
60-second limit and preserve the ordinary workflow's shared-journal sequence.
Keep every distinct oracle and its assertions, without increasing a case's
limit or hiding failures behind retries. No broad host metadata probes, recorder
policy changes, source-gate bypass, unchanged reruns or AFL activity belong to
this diagnosis.

The root owns integration, issue records and the mandatory final source gate.
A direct recheck follows a concrete fix within the same reopened two-layer scope;
there is no independent review of this diagnosis.

## Instrumented result and selected repair

The root's instrumented Debug CTest run passes in 16.48 seconds, with its original
60-second limit. Stage entries show independent prefix work reaching ordinary
coding at 13.814 seconds; ordinary command cancellation starts at 14.140 seconds,
continuation at 14.355, managed provider settlement at 14.860, and final restart
cancellation at 15.530. All checks finish at 15.599 seconds. The original log is
`context/tui-next-host-2026-10-15/reopen-coding-stages.log`. Together with historical
late progress, this supports repairing the aggregate test shape; it does not prove
that a specific operating-system facility caused the historical slowdown.

Select three coherent registrations on the existing executable: `coding` retains
file selection, retained process output and the entire dependent ordinary
workflow; `coding_capacity` runs interruption/settlement/mutation/process/provider
capacity and admission/warning fixtures; `coding_routing` runs retry and owned
route fixtures. Retain `coding_stream_capture` for its distinct ordinary stream
setup/interruption and actual 100,000-fragment/two-block oracle. Its duplicate
capacity/retry/owned-route prefix is removed because the new registrations run
those exact functions, arguments and assertions once per suite. No distinct
oracle is removed. All four registrations receive a 60-second CTest limit and
the existing source working directory. Unknown executable modes are rejected.
This decreases duplicate durable writes and locates future timeout reports at
coherent independent groups; it changes no product code or case assertion.

Direct recheck: build the existing `coding_test` target, then run CTest with
`-R '^coding($|_capacity$|_routing$|_stream_capture$)' --output-on-failure -V`.
Check an invalid mode exits with code 2. The root serializes builds and owns all
runs, full gate, publication and any further source findings.
