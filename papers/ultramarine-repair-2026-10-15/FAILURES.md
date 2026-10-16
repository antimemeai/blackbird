# Command admission and reported outcomes

Root design/plan, 2026-10-15; challenged by FAILURE_REVIEW.md. Implement only after
review of command admission status contract with command owner.

CommandJobs::start supplies a stack-owned native CommandStartStatus initialized
before entry, with refusal none/live_limit/resident_limit, counts/limits at the
actual guard, and worker_started set immediately after worker construction.
Only that explicit guard refusal with capacity and no worker start is an ordinary
resource refusal. Absence of a registry row or unsuccessful return alone is not
sufficient: settlement/raw capture/producer admission may fail before start and
must retain the existing capture fence.

Pass this per-call state explicitly into CodingEngine::operation; no member-wide
state or name/error-string heuristic. Encode the refusal in the normal retained
operation result with capacity, resource live_commands|command_records,
effect_outcome not_started, observed count/limit and concrete management advice.
Do not publish an output_ref for a command never launched. Settle its attempt as
failure and render a refused/not-started activity plus the same reason. Return
that result normally so the workflow appends the actual function-call output;
do not synthesize stop.linkage, pretend interruption or mark capacity.stop.

A failure to retain that result or its terminal observation overrides the
recoverable classification and preserves existing sticky capacity/unknown
settlement behavior. Every other capacity source remains unchanged. Started
command capture/timeout/cancellation uncertainty never becomes safe to replay.
Do not add a general safe_to_retry bit or automatically retry any command.

Direct red tests use one live job and a mailbox-protected completed job under a
one-record/one-live bound. A deterministic provider requests a second command,
then must receive the exact not_started/resource result as its real tool output
and continue normally. Observe no forbidden side-effect marker, failure terminal,
no capacity.stop/stop.linkage, live job preservation, actionable activity text,
and a subsequent real exec after controlled stop/archive. Existing engine raw
capture exhaustion and interrupted-wait cases continue to prove the stop fence.

The historical model also supplied audit end0 (empty prefix) and read only the
first12 of an older trajectory window. Clarify native tool descriptions: omitted
end snapshots the current head; end0 is empty; cursor/next/end define ascending
pages, defaults are not latest count facts, and recent failures can be located
with a pinned end and appropriate cursor/attempt. Do not change pagination or
introduce a second query engine. Existing query/raw tests validate those contracts.

## Implemented and rechecked

CommandStartStatus and explicit per-call operation state implement this design.
Both native registry and retained-engine old-source sustained-command checks fail
at command33; the repaired native checks and retained-engine40-command case pass.
The initial new admission fixture had0700-directory setup missing (EPERM); fixed
fixture permissions explicitly. Its old-source run then failed inside the
activity-disposition assertion (callback exception surfaces as external_unknown).
That run alone did not supply a detailed cause trace; do not claim it as a
separate storage/execution failure. Current test diagnostics print failing clauses.

The provider exchange and operator outcome cases pass for both actual live
capacity and mailbox-protected resident capacity. The layer2 review found two
oracle/document gaps and one missing fault case. Corrections: observe the same
original PID still alive/in custody after refusal; document variable queries'
zero-cursor default; inject a refused-result write failure at the JournalFile
boundary and require a stopped workflow, blocked journal and no later effects.
The injected refusal capture fails before any rejected command launches; provider
continuation is not called and the existing capture fence wins. All three native
admission cases pass after these corrections. Original session remains read-only.
