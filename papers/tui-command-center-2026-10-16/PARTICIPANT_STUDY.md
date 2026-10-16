# Current-session participant observation

2026-10-16. Source study and proposed owner contract for the command-center tranche
following `f2ab70b`. This document does not claim implementation or a red run.
The delegated unit is a read-only participant observation owner. Root owns
CodingEngine/main/terminal integration and raw UI adoption. Existing participant
admission, direction delivery, cancellation, retention and cleanup remain the
execution owners; this view creates no new execution authority.

## Purpose and design consequence

The operator needs to distinguish a colleague executing a request, waiting after
an answer, being asked to stop, and finished with retention or cleanup still
pending. Selecting a colleague should reveal the actual addressed request and
latest answer, with their references, without issuing a model turn, draining
capture or joining a worker. The roster describes **resident Participants runs
of this current CodingEngine/session incarnation**, not every computational actor,
all historical colleagues, workflow hosts, or inferred responsibility coverage.
A task ID is an actual optional link; it is not evidence of task completion.

This follows [interaction theory](../../docs/INTERACTION_THEORY.md)'s distinction
between work, its representation and evidence, [TUI doctrine](../../docs/TUI_DOCTRINE.md)
sections 1–3, 5–6 and 10–11, and the domain-adapter/immutable-publication boundary
in [components](../../docs/TUI_COMPONENTS.md). The proposed
[coordination views](../../docs/TUI_COORDINATION_VIEWS.md) already require explicit
scope, stable selection, honest coverage and separate declared/observed states.
The current-session center can provide useful observation without inventing
assignments, routing actions through widgets, or claiming activity equals progress.

## What the source actually owns

- [Participants interface](../../include/blackbird/participants.hpp) explicitly
  assigns controls/drain to one owner. Workers touch guarded Run state and the
  bounded capture queue. Retain/Save/Publish/cancelled callbacks have special
  reentry rules. An observation API must not read the unguarded
  `owner_callback_active`/`draining` flags from another thread.
- [Registry and state](../../src/participants.cpp:115): a map holds at most 32
  runs; concurrency defaults to 2 and is configurable through 16. IDs are
  nonempty byte strings up to 128 bytes without NUL, **not command-job 32hex IDs**.
  Each run holds row state, sealed/finished/cleanup facts, stop atomics, up to 16
  queued directions, 64 accepted direction receipts, references and a worker.
  Captures have a separate owner; emergency retention can outlive business
  terminal state. `archivable_locked` requires more than `finished`.
- [Start](../../src/participants.cpp:997) retains prepared admission before
  publishing the Run and constructing the worker. Its native 2102 reference names
  the actual run and task. A failure after accepted admission can leave a truthful
  finished row without a successfully launched worker. Do not label every
  admitted row as a running transport.
- [Worker](../../src/participants.cpp:592) sets the latest result and delivery
  under the product mutex. A completed request ordinarily becomes `waiting`, not
  a finished run. Dequeue changes it back to `running`; seal/cancel/failure can
  finish the run. Native 2104/2105 delimit the actual dispatch, and 2126 is the
  worker END. These are distinct from whether thread join and cleanup occurred.
- [Directions](../../src/participants.cpp:1170) retain prepared admission before
  inbox insertion. Exact duplicates reuse their receipt; conflicts refuse.
  `send` increments queue and receipt facts but does not publish a complete
  native roster. Current row `request` is the initial addressed request used as
  a basis for subsequent directions; `last_delivery` names the last answered
  request. Neither field alone is a reliable active-request identity.
- [Read](../../src/participants.cpp:1138) calls `drain()` **before** acquiring the
  projection guard. [Drain](../../src/participants.cpp:896) claims raw credit,
  retains originals, can stop runs on retention failure, and saves a full program
  snapshot. Calling read/drain from painting, composition or another owner is
  not a pure observation. `await`/`join` additionally wait; archive joins, cleans,
  drains final captures, removes a run and saves.
- [Native activity publication](../../src/participants.cpp:238) is only
  run/task/phase/operation/cancel-requested. It may originate on a worker and may
  fail independently. It has neither roster completeness nor answer bytes,
  queues, retention state, archive retirement or request lineage.
  [CodingEngine wiring](../../src/coding.cpp:2582) forwards it through task
  activity. Reconstructing a roster from this stream would fabricate an owner.
- [Original handoff](../../src/participants.cpp:441) binds each capture to a
  run and direction request ID. It preserves a real ProviderOriginalReference,
  extent kind and observed-used fact. [Retention](../../src/participants.cpp:752)
  returns an optional SourceReference, but drain currently discards that return.
  Initial/direction prepared admission has a separate actual retained reference.
  [Colleague result](../../src/colleague.cpp:449) emits the wrapper `result`
  original before returning the reply. A latest-answer source can be indexed
  there without scanning the capture list or journal on each UI read.
- [Result projection](../../src/participants.cpp:82) keeps a result whose encoded
  packet fits 65536 bytes, otherwise returns selected status fields and at most
  8192 text bytes plus a truncation explanation. There is currently no native
  explicit result-projection completeness field. A view must not imply that the
  projection is the entire original answer.
- [Restore](../../src/participants.cpp:831) makes prior rows finished/sealed with
  cleanup closed, rewrites formerly running/waiting state to `unknown` and erases
  request. Its 2129 reference describes restoration, not a new transport launch.
  Old saved rows do not contain all the provenance proposed below; missing means
  unavailable, never synthesized from a recent event or ordinal zero.

## Native owner contract proposed for agreement

Expose a dedicated `ParticipantObservationReader`, not ordinary participant
controls or a mutable registry. `try_observe(query)` is noexcept and returns a
finite owned native value: `ready`, `busy`, `invalid_identity`, `invalid_range`,
`unavailable`, or `retired`. It performs no raw admission, retention, save,
notification callback, provider call, cancellation, wait or journal lookup.
The UI's recording/adoption owner separately records the exact observation it
actually accepts, using nonblocking recording admission as in the command
inspector. A read being ready is not evidence that a new scene was recorded.

Use a shared reader State containing only the existing product mutex, a nullable
Impl pointer, retired state and scope token. Impl aliases this mutex. A reader
first `try_to_lock`s State, checks the live pointer, and projects directly from
**const** guarded Run fields into its fixed native output. It never exposes the
pointer, a Value borrow, worker handle, callback or raw owner to its caller. The
pointer is usable only while that guard remains held. No second 32-row index or
per-transition full roster publication is needed.

Before existing cleanup/destruction, Participants takes the same guard, clears
the pointer and marks State retired, then releases the guard. A finite in-flight
copy finishes before that retirement can proceed. A later reader returns
`retired`; holding State cannot pin Impl, Runs, callbacks, producer handles or
threads. State must outlive the Impl mutex alias, so member construction and
destruction order are explicit. Cleanup and joins stay outside the held guard;
retiring the reader does not stop or settle work. Ordinary owner shutdown while
the object remains live need not retire observation: the same owner can publish
its actual final state. Destruction/session retirement explicitly invalidates it.

Every result carries the endpoint's scope token and distinct live incarnation.
Integration binds that token to actual session/environment identities and the
current UI route. A DefinitionGenerationId is not a session identity. Queries
and selection name that scope; an old reader or scope-bound cursor cannot attach
to a new owner merely because its run ID matches. Each returned page and selected
detail is one guarded point-in-time sample. Pages fetched at different times
are not one atomic catalog, and a consumer must not label their union a complete
coherent snapshot. This tranche need not invent a domain-wide revision counter
just to paginate. The UI's recorded adoption generation remains a presentation
identity, distinct from domain scope. Scope retirement clears live selection;
old owned output can remain inspectable as an explicitly retired observation.

Use explicit byte-length native IDs (128 bytes capacity), with the existing
nonempty/no-NUL rules. A row carries the real run ID; optional task ID;
provider/model/from/to bounded text with actual size and truncation flags;
current domain phase; stop-requested, sealed, business-finished and actual
worker-started/worker-ended/cleanup-closed facts; queued and accepted direction
counts; reopened/restored fact; and actual run admission/restore/dispatch/end
references where known. Do not derive worker-started from a jthread slot whose
joinability can change outside the product mutex. Cache that fact at its actual
publication point. There is one existing synchronization adjustment:
`cleanup_run` currently writes `cleanup_closed = true` outside the mutex, including
during archive while the observation reader can remain live. Publish that scalar
under the product guard before projecting it; a reader holding the guard does
not make an unguarded writer safe. Do not inspect activation/observer descriptors,
endpoint handoff or jthread state from the reader. All references have explicit presence and kind. Native raw
references and semantic journal SourceReference remain distinct.

Selected detail adds the current dispatched request ID, initial request ID,
last answered request ID, their prepared-admission/original references when
known, and latest result status/remote disposition plus a bounded text page.
Keep initial/current/last-answer identities distinct while a queued direction
waits or the next request is running. A result includes its associated request
ID and source; replacing it for a later request must be atomic with that binding.
Retain only the latest result provenance binding required by this view. Exact older
originals remain in their existing owners; no new lifetime history quota or
capture queue scan is introduced.

Use query limits of at most 8 rows and one selected text page of at most 4096
bytes, with offset and explicit next offset. Direct projection first uses
`Value::value()`/`get_if<Object>` to check each shallow row/result shape and its
constant field-count cap **before any find**. A 32-field cap covers the current
12-field owner row plus reopened state, and the current fixed colleague wrapper
fields; a malformed/oversized restored shape returns an explicit unavailable
observation. Each named-field lookup then examines at most that cap. Ignore nested
request/context/profile/model_usage/reported_error/usage graphs entirely. Query
must never encode, format, compare whole Value graphs, validate an entire long
string, or call a mutable object()/array() accessor. Report malformed field type
separately from a legitimate empty value.

Fixed metadata copies have declared byte caps and expose original byte length
plus truncation. A string's length and a selected substring copy are bounded by
size()/min(requested,remaining), not a full text scan. Existing result Value
storage stays private and alive under the guard; copy only the selected 4096-byte
page into returned output. No result keeper, Value copy or 64KiB result mirror is
necessary. Current queue/receipt sizes, concurrency, finished and sealed already
have native scalar storage. Where a row Number must be inspected, use its native
storage alternatives; never Number::text() or decimal normalization/limb traversal.
Unknown numeric representation can be explicitly unavailable rather than parsed
by an unbounded conversion in an observation call.

The query does at most one bounded ordered seek for continuation and one for
selected ID, projects at most 8 catalog rows plus one selected row, examines a
fixed number of at most 32-field shallow objects, and copies at most the declared
metadata caps plus requested detail bytes. Return examined rows/fields, seek steps
and copied bytes as actual finite query coverage, not transformed raw telemetry.
No traversal of captures, receipt history, nested result/request object graphs,
retained originals or application journal occurs. Cache only native facts absent
from existing owner state: request/provenance bindings, projection completeness,
worker lifecycle publication, and per-run pending capture ownership.

Completeness has separate dimensions: resident catalog complete/page-continuation;
selection not-requested/resident/not-resident; result not-yet-observed/available;
projection full/truncated; page complete/has-more; provenance present/unavailable;
and capture/retention pending/unknown/complete where actually established.
`finished` cannot mean capture complete: a final emergency original may still
arrive before actual worker END/join. Track per-run pending capture ownership at
its link/removal points; do not scan the global capture chain to render each row.
An absent optional Retain return does not prove a semantic source was recorded.
A result's raw reference may exist before semantic retention, and a pending
reference must not be represented as retained. Retention failure preserves the
Run/capture and its truthful debt; observation must not attempt to settle it.

Reader `busy` or producer/publication failure preserves the adapter's last good
immutable page and its applied reference with explicit stale status. `retired`
is distinct from a ready empty catalog. No clock guesses make a page fresh;
show current scope and actual successful observation/adoption; do not invent freshness
from a poll counter.
Literal detail escapes control bytes only in presentation, preserving original
bytes and references in the native observation/capture path.

## Turn, idle and retirement delivery

[Terminal worker launch](../../src/terminal.cpp:1948) runs `perform` on a separate
native worker while the UI owns its event loop. [Idle work](../../src/terminal.cpp:2275)
runs only after busy becomes false and RetainedState ownership permits it.
[Main](../../src/main.cpp:1484) currently drains participants there; it is not a
turn-time roster publication facility. The participant observation endpoint must
therefore be queryable outside idle work and outside the transferred retained
state owner. UI polling is bounded and occurs outside keyboard handling and
painting, just like the current command inspector. Worker/product changes to
the guarded owner state make actual answers/phase changes visible during a long turn
without forcing a drain or transferring the journal back to the UI.

Root installs the reader before `ui.run`, binds its live token to the current session scope, and
retires/replaces it during session changes and shutdown. Existing
[session-switch validation](../../src/coding.cpp:5583) rejects active participants,
but finished resident observations can remain and must not leak into another
session. Main currently replaces the process/session through explicit handoff;
reader lifetime must remain correct even in a direct fixture that destroys one
engine and retains its reader. Existing task activity remains useful feedback,
but is not used to fill missing roster records.

## Direct red oracle, before implementation

Add one permanent owner test target, with private gated fake transports and
actual Participants. Proposed cases are `semantic`, `ranges`, `busy`, `lifecycle`
and `raw`. Root owns CMake and builds. A missing symbol/header is setup failure,
not behavioral RED; use an honest minimal unavailable observation seam first,
then capture failure of the specified behavior against unchanged participant
execution code before implementing native publication.

1. **Semantic/noninterference.** Start two real runs with distinct legal IDs
   (include a nonhex ID), addressed request/model/task values and gated transports.
   Hold a capture pending. Repeatedly query selected native observation while
   deliberately not draining. Assert actual selected identity/request/phase,
   exact result bytes when published, and unchanged transport count, Retain/Save
   call count, queued directions, receipt count and capture ownership. Send one
   actual direction and check pending count/receipt immediately; release gates
   and distinguish initial/current/last-answered IDs. An empty activity callback
   cannot hide either real run. This fails an activity-derived fake or read()
   wrapper directly.
2. **Finite work/detail.** Populate all 32 resident runs with actual bounded
   requests and distinct latest results, then query pages and selected output
   near its final offset. Independently concatenate returned bytes and compare
   to the known projection; assert next offset, explicit projection truncation
   for a larger original, exact row limits and actual examined-work bounds. Add a restored row with an oversized
   shallow result object and a huge nested reported_error/usage graph: the former
   must refuse its shape before field scan, while the latter is never traversed.
   Test malformed IDs, zero/oversized limits and missing selection distinctly.
   Do not make an elapsed-time threshold the bounded-work oracle.
3. **Busy without effect.** Hold the actual observation/product guard using a
   fixture friend seam, query from another thread, require `busy` and zero domain
   callbacks/work. Release it and require the real roster. TSan checks the same
   owner/worker schedules; no artificial sleep is required to induce contention.
4. **Lifecycle and late capture.** Observe waiting after an answer, requested
   cancellation, business terminal while a real late emergency original remains,
   then END/cleanup/retention separately. Reuse the existing legal callback gate
   from [participant custody](../../tests/participants_custody_test.cpp), without
   repeating its archive byte-retention claim. A restored formerly live row is
   unknown/reopened, never a current worker. Archive the selected run and require
   not-resident, preserving the selected ID rather than substituting a neighbor.
   Destroy/retire the owner with a retained reader, then create another owner
   with the same run ID and distinct scope/incarnation; the old reader must stay
   retired and old scope-bound cursors must not select the new run. A query racing
   destructor retirement either returns one fully copied old sample or retired;
   never a dangling borrow or mixed new-owner detail. Exercise archive cleanup
   concurrently with observation to attack the guarded cleanup publication.
5. **Real raw provenance.** Use genuine RawRuntime and actual provider original
   leases from the private transport. Independently decode prepared admission,
   dispatch/result original and semantic retention linkage; compare returned
   native references to actual recorded records, request IDs and exact payloads.
   Query before and after explicit owner drain to distinguish published raw bytes
   from successful semantic retention. Inject Retain failure/absent reference and
   verify observation stays read-only and never invents a reference or clears
   debt. Root's center adapter separately checks raw apply-before-paint and
   last-good-scene preservation on physical CONTROL try-refusal.

At the initial study handoff, only this study was authorized. Final field names, native wire
layout, scope-token wiring and minimal test seam are to be agreed with root
before edits to product source; no library or new concurrent domain-control
facility is proposed.

## Simpler endpoint evaluation

Root challenged the initial proposed duplicate native roster index. Source
inspection supports the smaller endpoint above: map insert/erase and useful row
mutations already use the product mutex, maximum resident count is 32, and const
Value access exposes shallow size/type without allocation. Value's COW behavior
means there is no reason to clone result text for a read performed under that
guard; avoid copying the exposed Run row itself, since Value copies can allocate
when `exposed_` is true. The concrete cleanup_closed publication caveat and jthread
access restriction are addressed above. No remaining source obstacle requires a
second index. This revision replaces the original mirror recommendation and its
unimplemented catalog-wide revision requirement; it adds no product code.

## Implementation unit

Implement the smaller lifetime endpoint and fixed native query above. First add
an honestly unavailable observer seam and permanent real-owner behavior assertions;
record their behavioral failures before implementing publication. The public API
is Participants::observer() -> ParticipantObservationReader and
reader.try_observe(ParticipantObservationQuery) noexcept. Root binds its unique
live incarnation to actual session/environment identity. Observation data uses
fixed native IDs/text/rows and a 4096-byte window; no returned Value graph.
Only missing worker lifecycle, current request/provenance, latest answer provenance,
projection completeness and pending-capture facts are added to native Run state.
Existing controls retain their behavior. Owning source/test translation-unit lint
and targeted Debug/sanitizer runs follow the real red-to-green implementation;
root owns build registration, integration and the one independent review.
