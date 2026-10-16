# Exec refusal, capture failure and model diagnosis

2026-10-15. Independent source and retained-session study for the Ultramarine
repair. This is the design/adversarial research pass, within the campaign's two
layers. No product source changed in this pass. The session was read as files;
no recovery launcher, provider, command replay, AFL output or artwork was used.

## What the model actually inspected

The original session is
`/Users/patrickbeam/.local/state/blackbird/session-nIWneY`. Its audit was read
using the native ARJ1 frame layout from `src/journal.cpp`, retained event layout
from `src/retained_events.cpp`, and the existing development BBM2 decoder in
`scripts/native-packet.py`. Source bytes and metadata below come from that
session, not a reconstructed provider conversation.

The refused exec original at source sequence2294 is
`{error:capacity,detail:0,output_ref:85de7df7981a5bf08803000000000000}`.
The application at2295 identifies it as an exec result. Capacity message
source2297 says effect outcome unknown, no replay, earlier originals retained,
rejected bytes not retained; application2298 records unretained_bytes0.
Neither this result nor its human error label identifies the exhausted registry.
The triage report establishes refusal before worker launch from raw command
events, separately from the semantic adapter-dispatch boundary.

The next-turn model did inspect tools, but never reached this fact:

1. Its audit_inspect arguments were `{query:{count:8,end:0}}`. The result source
   sequence2344, application2345, is `{end:0,next:0,records:[]}`. Zero is an empty
   pinned prefix, not a request for the newest facts.
2. Its trajectory_read arguments were `{query:{count:12}}`. Result source2382,
   application2383, returns cursor1055, end1183, next1067, scanned12,
   caught_up=false. It is the first12 facts of the default latest128-fact window,
   in ascending order, not the latest12 facts. It includes an older exec attempt
   `85de7df7981a5bf04e03000000000000` and that attempt's process.output records.
   It does not include the refused attempt ending8803000000000000.
3. The model then asserted operator interruption during the build and denied
   capacity, without following pagination or the relevant attempt ID. The
   retained provider response at source2407 contains this answer and its next
   exec request. That second request is also refused; source2422 contains the
   same undifferentiated capacity result with its distinct attempt locator.

The harness preserved the capacity fact but replaced the ordinary tool exchange
with a generic turn_stopped placeholder. The model misread partial, unrelated
inspection results as support for a particular running effect and stop cause.
Both defects matter. Better instructions alone cannot supply the missing native
classification; native classification alone cannot guarantee truthful model
reasoning about any future unrelated unknown effect.

## Actual source boundaries

`CodingEngine::operation` records attempt admission and opens its generic adapter
boundary before invoking the exec implementation. Adapter dispatch therefore
does not prove that CommandJobs accepted a job or started a worker.

`CommandJobs::start` prepares its job and raw capture resources, then under its
mutex checks duplicate identity, retained record count and live debt before
inserting the job and creating its worker. A refusal at the record/live checks
proves this new command did not launch. Other earlier operations in start may
fail because raw capture or allocation could not be prepared. They are not the
same recoverable resource refusal.

After a successful start, wait can encounter interruption, capture failure,
deadline or other errors after a child may have acted. Keeping the job ID stable
and stopping/joining local custody does not establish that external effects were
undone. Continue to preserve unknown results and prohibit implicit replay.

Current operation preparation recognizes capacity from process controls as an
adapter result, but exec capacity triggers capacity_stopped_, capacity.stop,
unknown terminal disposition and a thrown capacity error. Turn rollback appends
ContextStore::stop_outputs, which uses turn_interrupted only for Error.interrupted
and turn_stopped otherwise. This generic linkage does not preserve the original
error, attempt locator, exhausted resource or launch state.

## Smallest sound classification

Introduce a narrow native refusal reason at the exact command registry bounds
check: command records versus live commands. Encoding this owned enum in Error's
nonallocating detail is sufficient; do not reinterpret arbitrary capacity detail
values from unrelated subsystems as registry refusals.

At the exec call site, recognize only capacity with one of those explicit
registry refusal reasons. This establishes a normal, retained failure result:

- error and numeric detail remain available;
- resource names the command-record or live-command bound;
- effect_outcome says not_started;
- output_ref identifies the refused attempt;
- message says which resource refused the command, that no command launched,
  and directs inspection of process read; completed jobs can be archived where
  eligible, or existing live jobs can be awaited or explicitly controlled.

Do not add automatic retry or a broad safe_to_retry promise. A later request is
a fresh operator/model decision under possibly changed conditions. Do not raise
limits as the primary repair or terminate unrelated jobs to make admission fit.

This recoverable result must be retained and settled as failure, returned through
the existing tool-output path, and shown as refused/not started to the human.
It must neither set the sticky capture fence nor force generic stop.linkage.
Model tools and Lua calls share this classification.

Do not classify all exec capacity when `contains(id)` is false or start has not
returned as recoverable. Journal settlement-budget preparation, RawRetainedExec,
raw producer preparation and capture credit also precede launch. Their failure
must continue to block effects. A dedicated refusal reason distinguishes the
resource owner that actually made the decision without allocating a new error
object or parsing English.

Recording failures after a resource refusal still require the existing stop
behavior. Exemption is conditional on successfully preserving the result and
settlement, not merely on the original adapter error. Retain the existing
fallbacks if retaining output or terminal settlement exceeds capacity. Genuine
post-start capture failure, cancellation and timeout remain unknown, preserve
partial originals, and stop/join the correct owned job. An allocation failure
must not be swallowed as a recoverable registry refusal.

## Small query ergonomics repair

`src/audit.cpp` implements the observed behavior correctly and has no tail
query. Change the tool descriptions/examples in `src/tools.cpp`, not the
meaning of an existing query:

- Omit end to snapshot the current committed prefix. Explicit end0 selects an
  empty prefix. Pin returned end while paging with cursor=next.
- audit_inspect defaults cursor0 and returns its first count records.
- trajectory_read defaults cursor=max(end-128,0), or0 with an attempt filter,
  then returns the first count matching facts in that scan. It does not return
  the newest count matches. caught_up=false means relevant later facts may
  still be absent.
- With an exact attempt locator, use the attempt filter and follow next/end.
  For an unfiltered newest page, obtain end, then request
  cursor=max(0,end-count), end=end. Never infer cancellation from missing facts.

This is enough to correct the particular misleading expectations visible in
the session without adding a new inspector architecture. A direct refusal result
should make archaeology unnecessary for this common failure in the first place.

## Direct checks for the changed fault classes

Use existing owned semantic/fault tests rather than a live provider:

1. Force an actual max_live refusal while an earlier owned command remains
   active. Verify the refused command's independent marker never appears,
   resource and not_started are returned, terminal disposition is failure, and
   the existing command remains under its original custody.
2. Force a record bound with retained, eligible/noneligible completed jobs as
   appropriate to the lifecycle design. Verify either safe bounded reclamation
   admits sustained commands or a remaining refusal returns the record reason.
   A completed job with undrained output, unretained terminal state, active wait
   or queued control must not disappear merely to make room.
3. In one workflow, append an actual function_call, deliver the refused result
   as function_call_output, then successfully inspect/control current jobs or
   perform a distinct benign operation. Verify linkage is exact, no generic
   stop placeholder replaces the result, and no capacity.stop claims capture
   loss for this ordinary refusal.
4. Keep the existing output-overflow/capture-capacity scenario: marker proves
   the effect began, partial output remains retained, unknown is preserved,
   later effects cannot pass the sticky fence, and child custody settles.
5. Preserve prelaunch recording/settlement-credit refusal checks: no shell
   marker and no later effect after catching capacity in Lua. This specifically
   catches an overbroad recoverability exemption.
6. Retain cancellation and timeout checks with exact child/group settlement and
   no replay. New wording must not convert either into not_started merely
   because the collector has since finished.

Query-description edits need a focused source/document check against the
actual defaults above. Existing pagination tests directly establish query
semantics; a second test that checks a description contains chosen prose would
only mirror implementation and is unnecessary.

## Limits of the claim

This repair can make native launch/capacity facts truthful and readily available
to both participants. It cannot establish why the model chose an unsupported
interpretation, guarantee its future explanations, identify the old build's
source from a current checkout, or explain the separate vertical pane corruption.
The retained session must remain unchanged and must not be reopened to prove
these source-level repairs.

## Layer2: single candidate recheck

Reviewed root's `FAILURES.md`, the candidate CodingEngine operation/call changes,
CommandStartStatus, registry retirement and direct tests, native tool descriptions,
and raw command event2045. This is the planned implementation recheck; no source
was edited or independent test/build started by this reviewer.

The adopted CommandStartStatus out-parameter is preferable to the initial
Error.detail proposal above. It identifies the exact registry decision without
reserving global Error detail values. It resets on start entry, records counts
under the registry lock, marks only actual live/resident refusals, and marks
worker_started immediately after successful jthread construction. A call-local
status reaches operation preparation; pre-start capture/settlement admission
failures cannot acquire a registry refusal merely because no worker exists.

The adopted refusal result uses an attempt locator and omits output_ref because
the command produced no process output. This supersedes the initial suggestion
to reuse output_ref above. The result carries the native error, resource,
observed count/limit and not_started, and ordinary workflow handling supplies
the exact function_call_output. No native retry was introduced.

Result-retention capacity resets both adapter_capacity and command_refused,
then uses the existing sticky stop. Terminal-retention capacity resets them too
and preserves unknown fallback settlement. Adapter-receipt recording failure
exits before the ordinary return, and RetainedState::dispatch marks its recording
failure to block future admission. Other exceptions are not converted into a
recoverable refusal. The candidate keeps the existing post-start effect
uncertainty paths.

Registry retirement removes only finished jobs with retained terminal state,
empty output pending list, no owned/pending custody, no active wait, no mailbox
reference and no unfinished control delivery. It chooses the oldest admission
ordinal rather than identity sort order. The retired owner joins outside the
mutex, and its retained-loan joined operation is noexcept. Worker construction
failure still erases only the new registration and unwinds the prepared owner.
Refusing a live limit does not evict a completed row first.

Event2045 retains its existing linked request, job text, operation, branch,
Error and limit layout, with operation4 meaning automatic settled retirement.
Its claimed credit75+job-ID-length matches that linked native record. Historical
read_process_output scans retained attempt/output records and does not require
the current registry row. Existing IDs are not reassigned to another resident
slot; the added stale-control test checks invalid_range after retirement.

No additional source correctness defect was found in these changed boundaries.
The review raised these bounded findings for the owning lane to resolve:

1. The trajectory description's start-at0 exception must include variable-only
   queries, because `variables=true` or the variable filter also selects cursor0
   by default in src/audit.cpp. This is a prose mismatch, not a query bug.
2. The new engine/provider refusal scenario should observe the same original
   job still live immediately after refusal and before deliberate shutdown.
   Its original version checked future recovery but could pass if the earlier
   child had been stopped unnecessarily. Lower registry tests already check
   exact PID survival; this observation attacks the changed engine path.
3. The refusal exemption's own retention-failure edge deserves one direct
   regression. Existing capture-overflow cases cover different operations and
   do not specifically catch removing one of the new exemption resets. Arrange
   a registry refusal and then fail its operation.result preservation, using a
   bounded journal-capacity fixture or the existing journal-directory/file fault
   interface. Assert a sticky refusal of later effects despite Lua pcall, no
   forbidden marker, preserved earlier originals and the expected unknown/stop
   settlement if the reserved maintenance path remains writable. If injection
   deliberately makes the writer unavailable, assert the blocked writer and
   no subsequent effects instead of requiring an impossible maintenance write.

One such direct fault case is enough; do not build a second test framework or
review the resulting test as another assurance layer. The owning lane records
actual test results and resolves the findings within this same layer2.
