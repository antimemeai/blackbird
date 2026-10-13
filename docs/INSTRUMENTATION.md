# Instrumentation: raw emission, recording and transformation

2026-10-11. Raw design approved; first source unit integrated on master.

Blackbird emits raw metric events into the audit log. A separate recorder preserves
the raw bytes on disk. A tertiary process transforms the preserved records.
Operations and evals consume metrics. No writer-side transforms or metric framework.

Read the concrete artifacts:

* [Design](INSTRUMENTATION_RAW_DESIGN.md): raw layout, retained byte channels,
  publication/reclaim, wakeups, capacity, audit recorder and exact crash boundaries.
* [Raw hook/body census](../papers/2026-10-10-raw-instrumentation-surfaces.md):
  all60 product surfaces, nine system domains and per-class emission design.
* [Implementation and verification plan](INSTRUMENTATION_PLAN.md): direct static,
  byte/order, fault, process-death, owner-lock/cancellation and performance oracles.
* [Completion plan](INSTRUMENTATION_COMPLETION.md): implemented machinery,
  remaining design gaps, every owner group and the checks needed to close nls.2.
* [Transport study](../papers/2026-10-10-raw-transport-design-study.md) and
  [recorder study](../papers/2026-10-10-raw-recorder-design-study.md): complete
  reference paths, mechanisms, limits and restoration.
* [Executed primitive experiments](../papers/2026-10-10-raw-emission-probes.md):
  macOS/Linux costs and five recorder-death cuts, with explicit limits.
* [Single adversarial review](../papers/2026-10-10-raw-instrumentation-design-review.md).

The [producer-transform proposal](../papers/2026-10-10-instrumentation-producer-transforms-superseded.md)
is superseded. OTel/Prometheus no longer guide emission. No dependency adopted.

The operator approved implementation on2026-10-11 and excludes host power loss.
An emitted record is a fully published source envelope/body. Named memory retains
published records through process failure until the separate recorder writes and
synchronizes them; only then may the producer reuse their space. Return does not
wait for disk. Interrupted acquisition before publication remains unresolved.

The implementation includes the owned channel, raw audit, separate recorder and
supervisor, independent sampler, native/Lua authored emission, and the delivered
provider, command, participant, Context, turn, task and audit-query owners. The
current operation/storage/retained segment adds exact result capture, native I/O
and semantic retained/index hooks with prepaid finite cleanup and explicit
main/worker ownership. See the [completion plan](INSTRUMENTATION_COMPLETION.md)
for each delivered unit, its direct/profile checks and remaining census work.
Application capture starts before retained-state creation/recovery and drains on
clean shutdown. Read-only audit inspection does not create a metric stream.
The launcher oracle checks actual native storage setup, Root factory/teardown,
linked startup outcomes, native journal calls, reopen and unchanged inspection.
Full-path workload rates, costs and capacity measurements remain open.

nls.2 remains open; logical clocks nls.9 and audit completeness nls.10 are separate
unfinished work. Evals, multiplayer and steering/trajectory implementation remain
deferred.
