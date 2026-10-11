# Command owners and live recorder: ONE code/oracle attack

Root inspected raw_command helpers, command_jobs worker and caller ownership,
coding command capture/settlement, terminal keyboard bursts, main cancellation,
runner bootstrap, live recorder/supervisor RPC and raw_try_claim_control; read
the existing command/recorder plans and current command_raw/native PTY oracles.
This is the single changed-unit code/oracle attack. Findings are corrected and
rechecked directly within this unit; no further panel.

## Findings

1. CommandJobs::wait emits2026 success before snapshot allocates the actual
   returned Value. A failed projection therefore records a successful return;
   non-Error exceptions from snapshot or the supplied tick/cancel callbacks
   bypass the Error-only catch entirely. Several CommandJobs::control branches
   similarly call ControlTrace::finish before constructing their returned Value.
   Their catch then calls finish again; the second event exhausts the one held
   terminal and fences the emitter while the first falsely reports return.
   Configure/archive2045 describe actual committed ownership and may correctly
   remain successful when result construction subsequently fails; do not
   confuse those effect facts with2014's actual API return. Stage each returned
   result before its terminal, handle actual exception branches in wait, and
   make terminal ownership single-use. Use a native allocation cut at the real
   post-effect projection and actual throwing tick callbacks; require exactly
   one linked terminal with the actual failure and no emitter fence, while the
   accepted control effect remains visible. Do not roll back accepted work or
   add fresh post-effect admission.

2. The raw command oracle checks literal binary bytes, malformed admission,
   four prepaid regions and final reap totals, but does not bind2008 to the
   actual2007 request or compare successful control/foreground request-return
   bodies. Swapped references/native control fields can pass these tests while
   existing business tests still return the right behavior. Replace that weak
   oracle with native field/reference expectations on real command operations:
   literal output read identity/request/response lengths, input partial/native
   refusal and its offsets, resize and signal native results, foreground release
   distinct from exit, and actual control success/refusal. Reuse actual child
   gates and existing product fixtures; do not build tests for the oracle or
   introduce another assurance layer. Compare the producer's ordinal order
   where records span DATA and CONTROL.

## Reviewed mechanisms

Worker setup2162, retry638 and semantic1858 claims stay on their own producer;
joined semantic/retry borrowers do not race the worker. The dedicated caller
cleanup producer is serialized under the existing mutex and uses immediate
57-byte claims, with one held75-byte initial shutdown event. Its context is
declared before jobs/claims. Native reads are bounded and prepaid before their
acquisition; crossing-limit originals stay in pending custody. Actual ECHILD
prevents later PID signals, whereas EIO preserves pending ownership.

Live recorder storage reserves vector capacity before constructing the borrowed
failure producer; front is not retired. Lifecycle runs only after lane/read
borrows cease. Retire seals the actual predecessor and seeds the successor with
the retiring lane until the success reply permits supervisor unlink. Recovery
still reads actual originals; ordinary registration no longer restarts or scans
history. Pending recorder death watches the independent SIGCHLD pipe and does
not spin on a closed datagram socket. The actual death cut and PID/original
checks address those changed boundaries. No additional transport finding is
asserted by this review.

## Finding dispositions

Finding1 is corrected at the original owner boundaries. Control2014 and wait2026
success are emitted only after the returned Value has been constructed. Their
terminal owners are single-use. Configure/archive2045 remain facts about the
committed effect and have independent single-use ownership; a later projection
exception does not rewrite that effect. Wait records bad_alloc as allocation and
other native C++ callback/projection exceptions as external_unknown, then
propagates the original exception. The existing Error cancel/refusal branches
remain distinct. No new admission follows a committed effect. Existing held
60-byte control and110-byte wait capacity suffice unchanged.

The native allocation red selected the actual one-pair configure return object
allocation after its limit mutation. It failed in3.06s because the producer was
fenced by the duplicate terminal. The corrected direct oracle also cuts the
actual eleven-pair wait snapshot allocation and throws from the supplied tick
and cancellation callbacks. Disk records must contain exactly one earlier-linked
failure terminal for each request, one successful configure effect, and no8001.
An actual second child launch remains capacity-refused under the committed
max_live=1 setting after the configure API throws.

Finding2 is corrected by replacing loose byte/total checks with actual native
request/ref/body expectations. Binary and crossing-limit2008 originals must link
their2007 request, exact descriptor,8192-byte buffer extent, producer ordinal and
original byte offset. A real gated private PTY receives65536 literal input bytes.
The test observes native write returns without replacing them: an actual partial
write and EAGAIN occur before the child read gate opens. Input request offsets
and remaining counts must follow precisely the positive native progress;
EAGAIN cannot advance custody. The child receives the complete original once.
Selected37x113 resize and stop/continue signal requests/returns must carry the
actual descriptor, target PID, native arguments and native result. Changed
input under the same delivery name is conflict-refused, while duplicates report
actual queued/completed progress. A separately bound real owner acquires a
foreground epoch; background releases it, an already-released repeat reports
false, and the wait return preserves a live child/open output rather than
claiming exit. Every checked return links an earlier same-producer request;
DATA and CONTROL ordinals share one order, and lease precedes wait return.

The new raw-input gate reuses the existing fixture gate; production waits and
observation deadlines are unchanged. Native observer and allocation cuts exist
only in command_raw_test. No additional code/oracle panel was added.

## Qualified direct results after finding corrections

Qualified Mac debug build uses the complete Xcode SDK in build/nls-command,
one worker. command_jobs3.68s, expanded command_raw4.08s,
command_jobs_engine3.43s, unchanged command_jobs_pty18.96s and
command_jobs_station1.34s all passed (31.49s total). After adding exact native
read/write descriptor expectations, command_raw passed5.49s. The sole narrow
static finding was adjacent same-type parameters in the test delivery helper;
reordering the helper's parameters corrected it. command_jobs.cpp,
raw_command.cpp and command_job_fixture.cpp were clean in the four-TU pass;
command_raw_test.cpp's finding-driven narrow recheck was clean. Its final rebuild
and affected command_raw recheck passed6.40s; git diff --check passed.

Logs: /tmp/blackbird-command-api-red.log,
/tmp/blackbird-command-findings-final-direct.log,
/tmp/blackbird-command-descriptor-direct.log,
/tmp/blackbird-command-findings-tidy.log,
/tmp/blackbird-command-findings-tidy-recheck.log,
/tmp/blackbird-command-final-static-fix-direct.log. Full source gates and
supported Linux integration remain the parent-owned qualification boundary.
Command source/oracles are idle and ready for that boundary; no remaining fault
from this unit's single code/oracle attack is left open.

## Linux qualification fixture correction

Linux debug and release passed the parent gate. Linux ASan identified a fixture
allocator mismatch: command_raw's deliberate allocation-cut operator new uses
malloc, but libstdc++ can select sized delete. Add the matching sized scalar and
array delete overloads, forwarding to the fixture's existing free path. No
production allocator or ASan check changes. The affected qualified Mac debug
rebuild passed; command_raw passed5.95s and the one-TU clang-tidy recheck was
clean. Logs: /tmp/blackbird-command-sized-delete-build.log,
/tmp/blackbird-command-sized-delete-direct.log and
/tmp/blackbird-command-sized-delete-tidy.log. The parent reruns supported Linux
ASan with this corrected fixture.

## Source-gate cancellation finding

The unchanged terminal_pty cut failed at its second Ctrl-C: exec returned
audit_unavailable instead of interrupted. Two actual reruns reproduced it.
Published originals contained the correctly interrupted2026 and successful
worker2035/2036, with no8001. RetainedState::dispatch then attempted its adapter
receipt through a fresh retained_append region on the canceled caller. Admission
returned interrupted before the semantic write; dispatch marked recording_failed_,
and Runtime's Lua error retention consequently returned audit_unavailable.

The first correction prepaid1728 on the actual caller before dispatch.
Boundary activates the existing prepaid region scope only after its real body
return/catch, covering the zero-source, one-event AdapterReceiptEvent. At most
four432-byte regions cover its actual rejection/diagnostic paths. A guard clears
the scope immediately after dispatch, including throws. It never lends worker
credit or applies the scope to launch, output or later result originals.

The actual CodingEngine/RawRuntime gated-child oracle failed0.64s on unchanged
source. Corrected command_raw passed4.09s. It checks the real interrupted receipt,
unknown command terminal and matching native attempt; caller CONTROL region and
worker settlement have distinct real lanes; the semantic writer stays live;
the actual next Lua turn returns73. The complete unchanged terminal_pty script
passes all cuts, including interruption, process-group reap, draft/queue retention,
continued work and reopen. No assertion/deadline was weakened. Logs:
/tmp/blackbird-terminal-cancel-red.log,
/tmp/blackbird-terminal-preserved-red.log,
/tmp/blackbird-command-receipt-red.log,
/tmp/blackbird-command-receipt-green.log and
/tmp/blackbird-terminal-cancel-green.log. Narrow clang-tidy for coding.cpp and
command_raw_test.cpp is clean; git diff --check passes. Static logs:
/tmp/blackbird-command-receipt-coding-tidy.log and
/tmp/blackbird-command-receipt-test-tidy.log. The parent's final source gate
remains the qualification boundary.

## Source-gate late quit/reopen finding

Release and ASan then exposed a second interval: ordinary Ctrl-Q after the
sleep30 status could leave a retained attempt without a terminal. The unchanged
UI-state cut reproduced on its third isolated run. Preserved session:
/var/folders/nr/8bhjl__j67bbctrbn87yjt7w0000gn/T/arco-ui-state-jous23_f/session.
Actual unfinished attempt c7f55c61c18554a20a0c000000000000 belongs to raw run
bb2e0a8ba0f4735f915fe0e699. Its caller published admission, then complaint and
rejected-submission originals; neither a2000 launch request nor a2002 worker
launch exists for that attempt. The receipt bank itself was acquired after
admission, and AttemptOpen still used fresh admission. Cancellation could stop
both before adapter entry. Logs:
/tmp/blackbird-terminal-late-isolated.log,
/tmp/blackbird-terminal-late-audit-last.log and
/tmp/blackbird-terminal-late-raw-summary.log.

The final selected exec boundary prepays5184 CONTROL before authoritative
attempt admission: three1728 banks for actual zero-source, one-event open,
receipt, and interrupted no-command terminal. Each bank covers at most four
432-byte retained regions, including bounded diagnostic rejection. The open
scope clears on actual Boundary entry, before native body admission. The receipt
scope activates after body return/catch and clears immediately after dispatch.
The terminal scope covers only the actual interrupted terminal. No scope lends
worker capacity or permits a fresh canceled source/result acquisition.

The no-body branch records interruption before adapter dispatch using its actual
entry flag. The entered-body branch records UNKNOWN after interrupted adapter
return with no registered command; it never claims the body was undispatched.
CommandJobs::start inserts the registration before creating the worker. Only
worker-constructor failure erases that unlaunched registration; the other erase
is explicit archive after an existing retained terminal. posix_spawnp occurs
solely inside the registered worker. A native launch or later failure therefore
keeps the job and its custody. The caller prepares its exec ID before admission;
fresh result-source and other allocation failure cuts remain distinct.

The deterministic native postadmission cut uses the existing task activity
callback after actual admission to set the real sticky cancellation pipe. The
postadmission fixture uses32MiB DATA to cover the task writer's actual16MiB+48
preclaim; task setup, exec callback, admission, open and settlement all run with
real raw capture. Unchanged source failed
1.02s: missing same-attempt receipt/terminal. The corrected native fixture proves
actual accepted Open, interrupted receipt, one unknown terminal, no launch
request/worker, no child ready, zero debt and owned children, live semantic
writer, no raw fence, next Lua turn, and ordinary disk reopen without
acknowledgment or replay. The existing live-child cancellation cut still proves
distinct caller and worker settlement lanes. Final command_raw passed5.03s. The complete
unchanged terminal PTY script passed, and all eight isolated unchanged UI-state
cuts passed. No assertion/deadline was weakened. Logs:
/tmp/blackbird-command-admitted-red.log,
/tmp/blackbird-command-admitted-final.log,
/tmp/blackbird-terminal-late-green.log and
/tmp/blackbird-terminal-late-isolated-green.log.

Both production coding.cpp and command_raw_test.cpp narrow clang-tidy are clean:
/tmp/blackbird-command-admitted-coding-tidy.log and
/tmp/blackbird-command-admitted-test-tidy.log. The fixture's explicit Open assertion
now checks the actual optional receipt identity before dereference. git diff
--check passes. Owned source, tests and papers are idle; the parent's
release/ASan/supported Linux and final source gates remain the qualification
boundary. Context implementation remains held.
