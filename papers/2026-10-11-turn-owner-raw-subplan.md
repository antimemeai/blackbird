# Turn ownership, policy activation and protocol capture

Completed owner subplan ready for its ONE whole-plan attack; not source release. This unit covers census13–14,27–28 and32:
CodingEngine owns turn admission, immutable source pinning, successful workflow
activation, failed-turn settlement, budget observations and task activity.
The ContextStore owner supplies context transitions; the provider owner supplies
request/retry/transport facts; VM/authored capture already supplies1500–1518.
Native operation/result ownership17 and workflow/definition owners18–21 remain
explicit separate units. No existing region or VM event completes those owners.

Read coding.hpp/cpp, context cancellation/stop/workflow methods, task activity
callers, retained settlement, raw admission and local_timing's prepaid region.
Use the approved native raw design and already acquired Unix/Lamport/Cedar/BSD
studies. Copy existing fields, immutable source locators and native values.
There is no metrics Value, packet encoder, clock subtraction, registry, traversal
or callback on this path. The existing business budget encoder runs once; its
native length must be captured before constructing the Number presentation.

## Actual effects and ownership

turn currently refuses reentrancy/backstop before setting turn_running_, then
resets derived turn state, acquires/refreshes retained cancellation allowance,
installs context.protect and begins the workflow before its existing try block.
Its Error catch clears staged definitions/policy, calls finish_workflow(false),
then constructs interrupted/unknown tool placeholders and appends stop.linkage
only for interrupted/capacity with a live retained writer. Unknown exceptions
finish the workflow without manufacturing a completed tool effect. These are
actual distinct branches. Beginning capture or begin_workflow outside the
catch must not leave an unreported failed admission or strand workflow state.

Successful finish_workflow(true,boundary_program) publishes context and all
pending policy/tool/program definitions in the existing durable boundary.
Following swaps are noexcept. Record the actual activated textual revisions
after swaps; preserve the accepted boundary even if later display notification
throws. No metric event can perform a fresh fallible admission after that effect.

The root generation is issued and source retained before constructing Runtime.
Pin events belong at retention, including retention failure, rather than only
at the later VM constructor. Existing1505 can remain the VM's source association;
it is not the source-retention request/return pair.

TaskRun currently builds presentation Values and swallows observer exceptions.
Its destructor emits an unknown activity. Metric activity must copy the native
task text and attempt identity at the actual running/finished/failed/unknown
branch before presentation allocation/callback. It must not infer state from
the callback's formatted phase or silently inherit its exception swallowing.
Persistent command activity belongs at actual command owner settlement, with
the same attempt/task association; never declare background release process exit.

validate_protocol's existing traversal maintains seen/pending sets. Capture
native row count before traversal and actual branch/Error afterwards. The
per-call call_id used at a conflict is already available and may be copied
before throwing, without another traversal. stop_outputs already constructs
placeholder rows; capture their count and existing call IDs as that construction
occurs, before the actual append. These records describe placeholders and do
not assert that the underlying tool had no effect or that it is safe to retry.

## Proposed native sites

Sites1900–1949, subject to checking the actual registration table. E16 is the
envelope; R24 is lane16/ordinal8; G16 is native DefinitionGenerationId; Text
is native size_t followed by exact bytes. bool1/u8/int4/size_t8/u648 on the
selected profiles; native Error is u8 code/i64 detail. JournalCursor is native
journal16/sequence8/end_offset8, never its object padding.

|Site|Body and actual boundary|
|---|---|
|1900 turn request|bool turn_running/backstop_claimed, current G, head Text, native monotonic/thread-CPU clock results; before admission/state mutation|
|1901 turn admission|turn R, bool retained settlement existed, native retained cancellation limits, native root cursor; after actual reserve/refresh return|
|1902 source request|turn R,G,size_t source length,exact existing source bytes; before existing retain_program, DATA claimed before the copy|
|1903 source return|source request R,u8 returned/Error/allocation/unknown, success-only SourceReference journal16/sequence8, failure-only native Error|
|1904 turn state|turn R,u8 reset/begin/evaluate/settle/cancel-clear/closed,bool turn_running/backstop_claimed/capacity_stopped/pending_budget/pending_program/pending_tools/pending_workflows,unsigned workflow_depth/workflow_invocations,size_t operation_depth/unretained_bytes; no new counters|
|1905 activation|turn R,u8 program/tool/budget,revision Text; immediately after the actual noexcept swap|
|1906 turn terminal|turn R,u8 returned/refused/Error/allocation/unknown,u8 reached stage,native Error if present,native monotonic/thread-CPU clock results; one actual terminal|
|1907 policy request|u8 read/propose/observe/restore,bool parent,[R],revision Text; DATA before existing policy work|
|1908 policy fields|method-specific native validation or observation body linked to actual policy R; exact variants below, no invented fields|
|1909 policy stage|policy R,revision Text; after actual staged state transfer|
|1910 policy terminal|policy R,u8 returned/stale/Error/allocation/unknown,[native Error]; after returned Value construction|
|1911 protocol request|bool parent,[R],size_t input rows; before actual validation|
|1912 protocol terminal|protocol R,u8 valid/Error/allocation/unknown,[native Error]; exactly once|
|1913 conflict identity|protocol R,call_id Text; at actual duplicate/orphan conflict before throwing|
|1914 placeholder return|placeholder request R,u8 returned/Error/allocation/unknown,success-only size_t actual rows OR Error; after existing construction, before append|
|1915 task binding|operation R,native attempt16,task_id Text; after validated real binding, before dispatch|
|1916 task activity|binding R,u8 running/finished/failed/unknown; actual branch, independently of presentation callback|
|1917 notification return|notification request R,u8 absent/returned/Error/allocation/unknown,[native Error]; actual optional callback return|
|1918 placeholder request|turn/repair R,bool interrupted,input head Text; before existing stop_outputs construction|
|1920 notification request|activity R,bool callback present; before existing optional task presentation|

The selected native clock fields are clockid_t4/int4 result/int4 error/time_t8
seconds/long8 nanoseconds per reading, plus bool thread_cpu_acquired:57 bytes.
Capture those raw fields separately, including acquisition errors, with no
timestamp formatting or subtraction. Existing raw_local_clock performs those
two native acquisitions; no timer or extra per-stage clock is selected.

Maximum1900 is131 (16+2+16+8+32+57);1901 is89 (16+24+1+16+32);
1902 is64+source length in DATA, not CONTROL;1903 is65 for returned native
SourceReference and50 for Error;1904 is72;1905 is81;1906 is108. The root
context head and issued revisions are existing hex16-byte business IDs, at
most32 bytes. Builtin program/tool/budget revision strings are shorter. A
successful turn has at most6 state events and3 activation events. Those state/source/terminal records cost1068:131+89+65+6*72+3*81+108.
The two actual stop_outputs calls (initial repairable outputs and Error stop)
each additionally hold1918max81+1914max58=139, giving fixed turn1346. Source request DATA is separately
claimed before copy; held fixed turn outcomes cover its failure. There is no
source pin return before actual retention returns and no successful activation
record before actual boundary publication/swaps.

## Policy and protocol native ownership

A policy revision restored from existing packets has no32byte validation.
1907 therefore uses DATA50+actual revision length with a parent, or26+length
without. Hold it before copying, never assert a nonexistent string bound. New
1909 staged revision is the actual issued32byte ID, so its maximum is80CONTROL.
1908max68 and1910max51 give each budget_view/restore119CONTROL. An outer
context_budget read holds its own terminal51 plus child119=170. Proposal holds
fields68+stage80+terminal51+child view119=318 before transferring pending state.
Its child1907 request additionally holds checked50+actual effective
budget_revision_.size() DATA before staging. budget_view observes that effective
revision, even after a different pending32byte revision has been issued. A
restored effective revision may be longer; claim its actual size before transfer. Parent refs are explicit arguments.

validate_context_budget already parses target and trigger. Fill a small
native out/guard as each actual parsed value is acquired, including the Error
path; successful-return-only collection misses valid thresholds followed by
reason refusal. Do not parse again for instrumentation. Existing budget_view only parses trigger
when enabled; preserve its short-circuit. Validation1908 is R,enabled-present,
[enabled],target-present,[size_t target],trigger-present,[size_t trigger],
with no input or triggered fields. Observation1908 is R,enabled,trigger-present,
[size_t trigger],size_t actual input_bytes,bool actual triggered; no target.
The actual method in1907 selects the body. Both fit the held68 conservatively.
No fabricated false/zero on validation or proposed/effective hybrid is emitted. No permanent
metric cache is needed. The existing request/context codec runs once and its
actual size feeds the existing business call and raw field before Number
presentation. No metric codec, additional Number.text or format_value. Native
policy publication follows actual pending transfer; later projection failure
records that reached state and cannot pretend no proposal was staged.

Protocol1911 request is49 with a parent,25 without.1912 terminal is50; each
actual validation holds99/75 before validation.1913 is DATA48+actual call_id.
At the existing duplicate/orphan branch, acquire that exact bounded body before
copy; pending-at-end uses the existing pending.begin() string. A capacity refusal
there is an actual reached admission error, not a fabricated successful
validation or conflict record. There is no second walk, call-ID parse, string
normalization or held unbounded stream. Valid rows do not emit conflict IDs.
Call sites pass the actual turn/request/workflow source parent if available;
standalone validation has no guessed parent. All actual exceptions get one
terminal and stage.

Placeholder1918/1914 describe the existing transformation separately from
context publication. Their native input head references the actual retained
context whose call IDs are originals; subsequent Context publication retains
the actual constructed rows.1914's count is the actual returned array size;
no second traversal or invented completed effect. The initial and cancellation
branches use the root's held139 each, including under sticky cancellation.

## Task binding and actual activity

Task IDs are already validated canonical t+positive u64 by TaskState, at most
21bytes.1915max85 uses actual raw command-admission1060 Ref and native issued
OperationAttemptId; no metrics hex formatter. Two activity41 records, each
with notification request41/return50, give binding+2*(41+41+50)=349CONTROL.
Hold all349 before the actual retained attempt append and preconstruct every
post-append object. The native activity copies real branch enum; existing
presentation still creates its phase Value and may fail independently.

Replace command_tasks_'s mapped string with a native CommandTaskActivity holding
that existing task string, native attempt, actual binding Ref, retained producer
handle and remaining terminal132 bank. Stage its actual map node and optional
returned business projection before attempt acceptance. TaskRun consumes the
initial132 after accepted binding. The selected exec body receives an explicit
TaskActivity pointer (private operation body signature updated at its three
actual call sites); after successful start it inserts the staged node by node
handle without allocation. No TLS guessing or second map. Failed start retains
local ownership and emits one actual final activity.

A persistent job transfers its remaining132 and original handle to this same
node; foreground background/yield return emits no finished activity. Actual
accepted terminal settlement in poll_commands selects that node, binds its
issuer handle sequentially on the owner, emits native finished/failed/unknown,
erases the accepted node and then invokes presentation with its held request/
return. All observer Error/allocation/unknown outcomes are swallowed exactly as
TaskRun already does. The current persistent callback instead escapes after
accepted settlement, stops commands and can leave activity debt; correcting
that inconsistency is part of this unit. No fresh claim after accepted terminal
and no retry of the observer. Existing command per-worker terminal banks remain
separate. Borrowed native producers must outlive all such retained handles.

Each normal nonpersistent TaskRun's noexcept destructor emits its once-only
unknown branch if no final branch executed, spending the same remaining132.
Fields and native flags precede projection; no allocation occurs in raw emit.
Callbacks remain actual presentation, not a metrics subscriber registry.

## Admission and dependency integration

Before the admitted turn's effects, hold its complete finite terminal bank and
a ContextStore stop bank. Error settlement invokes finish(false), then a view
and potentially append(stop.linkage); unknown settlement invokes finish(false).
Their raw origins must be explicit and share the real caller producer. Each
finite retained append region also needs its original held control budget.
RetainedState's actual issue pool is1024 IDs, shortened near UINT64_MAX. Allow
each of the two possible stop records its own issuer refill: four top-level
retained appends, not a success-only assumption that cached IDs are available.
Each one can call rejection retention; its current bounded source/event
proposal permits at mostfour nested retained regions. Reserve16*432=6912 for
those actual retained_append regions, separate from semantic context records.
Unused held bytes are released. The context owner's actual semantic maximum is
2286:finish(false)484 +reject-managed513 +view209 +stop append592 +optional
checkpoint488. Together with6912 retained-region credit this is9198, giving a
whole root turn fixed/stop bank of1346+9198=10544 before effects. The optional
checkpoint has no fresh source acquisition allowance: it reports that actual
admission refusal before an unpaid read, preserving the accepted stop append.
No blanket cancellation bypass, nested registry or borrowed dead producer.

The root RawTurn owner is constructed before Running so it survives actual
turn_running_=false/continuation clear and records the closed state before its
terminal. Move fallible admission/reset/begin inside one outer catch; call
finish(false) only if the actual workflow began. Keep three distinct native flags: context_settlement_accepted retires the
managed pending child; boundary_program_accepted records the actual program or
atomic program/context append; activation_applied guards the prepared noexcept
swaps. Managed rejection alone never activates a program. If history pinning
throws after actual program acceptance, perform every accepted program/tool/
policy swap once BEFORE fallible notification/emission or failed-turn cleanup.
Then emit the actual activation fields from their existing held bank. No
prepublication failure activates anything, and accepted work is never reappended. Loan Context9198 and its retained-region portions explicitly from
the same producer; no fresh claims during Error stop. Six actual state stages
are each emitted once, no per-loop stream.

Protocol loops claim each bounded conflict-identity record before acquisition;
the final terminal is already held. Budget helpers retain native parsed values
from the existing business work, with no Number text parsing solely for capture.
Notification records use held credit; presentation callback failures cannot undo
retained activity. The source and configured revisions remain owned until the
single immediate copy finishes. No timer is introduced.

## Direct checks

Baseline red uses real CodingEngine turns with RawRuntime and independently
expects1900. Existing engine/context_budget/managed-context/protocol/task-command
fixtures remain business oracles. Compare full bodies and actual R/G/retained
SourceReference for empty/prompt turns, source retention failure, rejected Lua
source, callback exceptions, staged policy/program/tool activation and failed
turn cancellation. Full ordinary lanes plus sticky cancellation must preserve
the held failed-turn and stop-placeholder facts and accepted prior work.

Observe task binding/activity from real exec task_id and actual settled command,
including background release, nonzero exit, capture refusal and throwing observer.
Check policy input bytes against the existing business encoder's actual output,
and protocol conflicts against independently specified rows/call IDs. No second
metrics encoding or tests of a test helper. One complete plan attack precedes
source; one code/oracle attack follows genuine red/green, then finding-driven
fixes/direct recheck and affected native profiles plus the installed source gate.
Six total build/analysis workers; evals remain deferred.


## ONE plan attack resolution

The whole-plan report is [turn plan attack](2026-10-11-turn-owner-raw-plan-attack.md).
Its two policy findings are resolved above without increasing119/318: dynamic
child1907 uses the effective restored revision, and1908 distinguishes actual
validation from observation with guarded partial parsed values. Known Context
acceptance integration names the separate pending/program/activation flags.
Direct real-engine oracles include a513byte restored effective revision plus a
new pending32byte revision; insufficient child credit refuses before transfer.
Use effective enabled/trigger100, proposed disabled/trigger900/target450 and
actual input above100 to compare independent validation/observation bodies.
Empty reason after valid parsed thresholds preserves those actual fields and
invalid_range with no staging. Disabled observation has no parsed trigger.

The four actual accepted/prepublication Context history-pin cuts compare live
engine policy/tools/program and retained boundary packets, then reopen without
a repeated append or dispatch. Task callback Error and actual allocation refusal
cannot change accepted terminal; persistent foreground release is still running.
There is no new review panel or metrics-only parser. Source waits for the
completed-owner qualification checkpoint and coordinated coding.cpp windows.
