# Native concurrent workflow implementation

2026-10-15. Operator authorizes implementation with “now send it”. Work only in
`/Users/patrickbeam/projects/blackbird-workflows`, branch
`work/concurrent-durable-workflows-2026-10-15`; the main checkout belongs to a
colleague. The reviewed [design](../../docs/CONCURRENT_DURABLE_WORKFLOWS.md) and
[delivery sequence](../../docs/WORKFLOW_DELIVERY.md) remain the destination.

## First integrated execution path

Build the sole-owner native workflow service and attachable binary client over
the existing RetainedState/AuditLog. Add typed atomic application cells, a bounded
leaf executor and structured join reducer as independent conceptual components.
Compose them into an actual owner process: submit source-pinned native tasks,
execute independent tasks concurrently, preserve exact outcomes, select/commit
joins, cancel explicitly and reopen without replaying released effects.

Use one native semantic ledger. A cell transaction can append native
Decision/Invocation/Admission or terminal Observation events atomically with
state. Existing dispatch opens an attempt before executor visibility; successful
adapter receipt means native scheduling acceptance, never business success.
The final completion and original result update the same task atomically.
Recovery preserves logical command identity and reports released uncertainty;
recovered attempts never become live. Proven-unreleased successor attempts require
the exact recorded evidence and fresh guards specified by the design.

The execution host owns its journal lock, raw-storage grant and service socket.
Clients transfer native BBM2 requests/results; JSON parsing/formatting exists only
at explicitly selected CLI interchange boundaries. Disconnecting every client
does not cancel the host or work. A second owner must fail before task dispatch.
The host polls sockets and a completion wake pipe, without busy inference/timers.
Requests are bounded and partially transferred frames are not commands until
complete. Local private endpoints do not claim E2EE or cross-host takeover.

## Ownership lanes

- Root owns the service/host, CLI, native record integration, CMake, shared
  journal, Beads, build writers, qualification and integration.
- Native inventory lane owns WorkflowStore: typed cells/read sets, exact
  submission idempotency, native event batches, replay and commit/publication.
- Agent workflow lane owns WorkflowExecutor: bounded leaf workers, preallocated
  completion capacity, cancellation observations and owner drain/shutdown.
- Durable workflow lane owns WorkflowJoin: precise modes, member/clock/order
  semantics, immutable selection proposals and acceptance after commit.

Each lane writes its concrete subplan before code, direct tests before source,
and returns its whole conceptual unit. No lane shares a root build/index writer.
Root reviews their API/source before integration. The existing research reviewers
are reused for one concrete code attack and one finding-focused recheck; never
a third layer or review-of-review. Cross-component invariants remain root-owned.

## Direct checks

Independently gate native workers to count real concurrency and aggregate limits.
Reject wrong-owner/reentrant controls, stale revisions/handles and changed-payload
submission identities. Force failed retention before readiness; count zero effects.
Check multi-cell atomicity and native event linkage through actual reopen.
Keep completion bytes when owner publication fails; cancellation records whether
invocation was prevented or only requested. No generic failure implies nonexecution.

Control join completion order/clock and compare exact selected member IDs against
the declared mode; proposals remain unexposed until committed, then immutable.
Exercise unknown outcomes, impossible closed quorum and open-membership closure.
Actual host processes/clients prove detach, sole ownership, exact results and
zero automatic effect replay after SIGKILL. Add direct crash cuts at native
decision/admission/open/result and compare external marker files with retained IDs.

## Resource and verification scope

Use existing C++20/Lua5.4.8 profiles and owned native/BBM2 machinery; no new library.
Only root runs native builds, initially two compiler workers and one Ninja writer
in each owned buildtree. All runtime fixtures use private temporary sessions and
no provider/account requests. Checks: direct debug/release and sanitizer cases
appropriate to changed ownership/lifetime, full installed debug/source gate before
source commits, and scoped Neuroses qualification when the integrated path exists.
No mutation campaign or old instrumentation campaign. Required performance work
measures complete useful native tasks/joins/reopen/capture, retaining all samples;
no invented speed or readiness claims. Existing bugs/tuning join affected closeout.

The wider F5–F12 tool-using model facades, recorded Lua, events/time/streams,
artifacts/demand, live revisions, paradigms and station/remote surfaces follow
their dependencies. Track actual delivery rather than relabeling a native
executor library as the entire programmable workflow system.

## Join integration details

Persist a join descriptor once, then reconstruct pending observations from native
task result revisions in retained acceptance order. Each task indexes its waiting
joins; owner completion feeds only those reducers. Persist the selected reducer
snapshot before exposing its immutable chosen results. Selected results reference
immutable task-cell revisions rather than cloning original output into every join.
Untimed joins use retained sequence order with epoch/ns zero; deadlines require
the later explicit clock protocol. Open membership, custom select predicates and
streaming are not advertised by this first CLI surface. A failed selection append
leaves the join pending and retryable; it cannot expose an uncommitted choice.

## Direct qualification repairs

Actual Linux ASAN caught a fault-fixture allocation mismatch: malloc-backed global
new had only unsized delete, leaving sized deletion to ASAN/libstdc++. Match both
ordinary sized scalar/array forms to the existing unsized replacements, following
the existing station/storage raw fixtures. No sanitizer suppression. Recheck only
this Linux ASAN target; the other eight affected cases and debug/release passed.

Source grounding also finds the inherited raw build hash omitted private src/*.hpp
and compiler configuration. Include private headers, relative source names, the
owning CMake file and selected compiler/build/instrumentation flags in this source
build identity before using it to pin native task declarations. Complete executable,
Lua/context/resource manifests remain the subsequent generation unit. This does
not claim the source build ID is an executable digest. Mandatory full-source
lint reports an easily-swapped intrusive-list head/tail API; group the ends into
one queue object rather than suppressing the diagnostic. Finish all actual
findings, then rerun the mandatory source hook; no new review layer.
