# Session/UI owner's one code and oracle attack

2026-10-13. One whole UI unit review, replacing root's pending code review.
Read the complete reconciled UI subplan and its sole plan-review dispositions,
raw_ui.hpp/cpp, ChatView/Grid/Painter/Output, Terminal/Composer state and queue/
worker/read/render paths, confined graphics source bound, the Task borrowed
control/data and callback-origin seams, Participants' explicit native publication,
the whole ui_raw_test and direct-eight green log. Program changes owned by this
reviewer were not reviewed. No production/test edit, build, new subagent,
performance qualification or additional code panel was performed for this pass.

The seven direct UI fixture groups and eight affected targets passed in the
owner's supplied direct-green.log. They establish useful actual native behavior;
they do not exclude the following concrete source failures. Resolve these within
this same owner review, then one direct recheck; no further panel is requested.

## U1 — Main and UI disagree about prompt range offsets

UI4010/4052/4071 source metadata uses offset0 within the **original text payload**
(`src/terminal.cpp:706,999,1223` and draft recovery). Queue and launch carry those
offsets at lines1385/1872. Root's reviewed Main-input plan supplies48 for4202,
which is instead the offset into the complete raw record. The same RawUiSource
therefore has two incompatible conventions. A resumed/initial prompt using48
cannot denote the same bytes as a keyboard/recovered prompt using0.

The owner confirmed the current UI convention is logical original-payload offset;
its PTY fixture asserts that explicitly. Declare that convention in RawUiSource,
reconcile Main4202 to it (or consistently change every producer and consumer if
root chooses the other convention), and state that original-bearing site decoding
precedes applying the logical range. Do not parse/reencode a Value to fix it.

Direct oracle: actual Main initial/resume source plus UI launch/queue, with literal
nonempty bytes and the exact referenced original payload/range. The current PTY
checks queue offset0 but has no actual Main initial4202 source and cannot catch
this integration disagreement. Root owns the confined Main correction.

## U2 — empty default command palette underpays an acquired result

`Composer::capture_bound` at `src/terminal.cpp:948–954` uses only current draft,
history quota and choice_capture_bound_. The last is set by explicit choices(),
not by the default builtin/workflow command_catalog. From an empty composer and
history, Ctrl-T then Enter has source bound0 and pays77 DATA bytes. The actual
choose '/help' result requires76+5=81. A maximum validated workflow command can
be129 bytes plus the existing trailing space:76+130=206.

`feed_impl` constructs the real chosen string at lines1118–1120 before feed's
4010 emission. The insufficient grant then fences after an acquired result and
palette mutation. This is a normal supported action, not hypothetical malformed
caller input. The owner confirmed the arithmetic/source gap.

Include the default catalog's actual accepted source bound coherently before
feed. If acquiring a snapshot to determine it, the same immutable snapshot must
drive the feed so concurrent workflow publication cannot replace a small catalog
after admission. A bound derived from existing validated alias/prefix limits and
the actual builtin literals is also finite; no second command classifier or
metric formatter belongs here.

Direct oracles: real empty composer Ctrl-T/Enter with literal '/help', and the
longest accepted published workflow alias plus trailing space. Require unchanged
chosen text, complete4010, correct outcome and no ingest fence. The current
submit/history fixtures supply a large draft bound and miss the empty palette.

## U3 — selected raw failures are swallowed by business fallback catches

Run's state-load catch at `src/terminal.cpp:1688–1702`, persist catch at
1708–1729, and queue/draft recovery Error catches in the input handler conflate
instrumentation admission failure with ordinary damaged/full/save state. They
can publish a normal advisory and continue input/render/queue work. The owner
confirmed there is no deliberate enclosing RawCaptureFailureScope/rethrow in
these paths. Main/Session's same-plan SS1/MI2 origin mechanism does not magically
provide a scope to this independent UI run or worker.

Use the actual selected synchronous failure scope at these UI boundaries and
rethrow its actual error before business fallback or new selected work. Preserve
ordinary missing/corrupt/oversized metadata, save failures and business queue
quota behavior. Code-only filtering is insufficient: capacity is both a valid
business limit and an instrumentation refusal.

Also mark final rejected **borrowed** claims at raw_ui_control and
RawUiTrace::split (`src/raw_ui.cpp:43–50,194–202`), and the confined borrowed
Task control/data splits used under the UI mutex. Fresh raw_claim_* already
mark final failure; split helpers currently just throw. Their final rejection
must propagate the same actual origin, while capacity probes remain unmarked.
No ordinary admission may move inside a UI mutex to accomplish this.

Direct oracles: selected raw load/save/queue source admission refusal with no
normal damaged/full acceptance, no subsequent read/queue/native write or dispatch;
separate ordinary business failure retains its old advisory/recovery behavior.
Use the actual native failure origin, not a synthetic ErrorCode that cannot
distinguish those cases.

## U4 — final worker publication can escape the thread and falsely precede completion

The worker finishes its4077 execution scope at `src/terminal.cpp:1993–1995`,
handles other failures with fresh post/text calls, then calls
`post(Kind::complete,{})` outside the entire try/catch at line2007. New raw
publication admission can throw Error. A failure of this final complete post,
or of post/text inside the outer catch, escapes std::jthread's function and invokes
std::terminate. The scope can already have emitted returned/stage3 before the
completion publication fails. Keeping the binding alive solves cancelled-source
selection but does not solve this exception/outcome lifetime.

Keep the actual completion publication within the paid worker outcome lifetime
and contain all worker exceptions. A native publication failure must have its
actual stage3 failure when that status is observed, preserve already completed
perform effects, and tell the UI owner to join/stop truthfully through the existing
worker/wakeup ownership. Do not retry fresh ordinary publication indefinitely
after the producer refuses it, and do not fabricate a successful complete message.
If this requires a finite completion/cleanup grant, own it before effects on the
actual worker issuer; never move a Main producer's token to the worker.

Direct oracle: real controlled worker performs one effect, then its actual selected
completion publication fails (recorder death or a genuine admission refusal).
Require process survival, actual failed worker outcome, join/retained uncertainty,
no duplicated effect and no successful completion claim. The present PTY fixture
only sees successful publication after cancellation. Checking4077's first branch
byte and a later complete message does not attack this failing branch.

## U5 — paid cleanup holders can outlive their original producer binding

ChatOutput retains cleanup_ and RawUiPacket retains terminal, but neither retains
the creating RawThreadHandle (`chat_view.hpp:116–124`, raw_ui.hpp packet fields).
Detach/retire uses ambient raw_ui_link/raw_emit
(`src/chat_view.cpp:588–623`, `src/raw_ui.cpp:277–286`). raw_emit returns successful
ordinal0 when bound_context is absent (`src/raw_runtime.cpp:2092–2093`). A selected
output/packet destroyed after its creating binding scope therefore performs flag
restoration/retires ownership but writes no paid native outcome. A foreign binding
instead tries its other producer with the original token. Held capacity alone
does not preserve issuer authority.

The owner confirmed there is no enforced binding-lifetime API contract beyond the
subplan's assumption. The actual TUI keeps its Main binding through local cleanup,
but the declared public holder boundary permits cold scope exit and the subplan
explicitly claims restoration uses its creating issuer. Retain the actual original
issuer and perform exclusive cleanup through it, or implement a concrete enforced
scoped-owner contract covering every public holder. Never silently accept ordinal0
as a recorded selected outcome or bind another live thread's producer concurrently.

Direct oracle: actual selected attach and captured paid packet outlive the binding
scope on their creating thread; then actual detach/destruction restores flags and
emits exactly one retirement/native flag result on their original lane. Include an
unadopted packet holder. Existing full-CONTROL tests keep the original binding live
and do not test this lifetime/authority boundary.

## U6 — handoff can claim an original packet that was never captured

RawUiFrame::handoff at `src/raw_ui.cpp:333–339` checks only failure_, splits its
terminal and closes construction. It does not require capture to have run. An
enabled frame can be handed off immediately after construction, with no4041.
ChatOutput::start accepts its terminal token (`src/chat_view.cpp:665–671`) and
then flush writes actual bytes referring to a frame with no original packet.
This defeats the selected output owner's explicit original-bearing admission.

Require the actual successful capture state before selected handoff; preserve
honest unselected/empty cases. Do not reconstruct/hash a packet in the emitter.
Direct oracle: actual RawUiFrame constructed but not captured, then attempted
selected handoff/start; require refusal before native write and a truthful held
construction/retirement outcome. Current rejected/unused packet fixtures always
capture first and leave this public owned-state transition unchecked.

## Source and claim limits

The current queue ingress preserves text before allocation/coalescing under its
mutex; native notification results are distinct from insertion. Drain's171 bank
precedes swap and each message's152 bank precedes application. The borrowed
Task restore/replay/read banks are finite and acquired outside the mutex; replay's
322 includes its221 commit and has a separate230 prepare borrower. No fresh
selected task DATA admission is performed while locked. Participants supplies its
actual2130 through the existing business callback, without another subscriber.

Presentation input originals precede clipping/retirement, and partial mutation
snapshot uses its held allowance. Native frame/output widths and offset progression
are explicit; the owned-grid tail bound is confined to reset/line ownership.
Graphics obtains the actual asset path once and enforces3072 before the existing
base64 construction. The successful full-CONTROL test stops the real recorder,
fills ordinary space and proves held detach/retire still emit under the live
original binding. Genuine kernel pipe pressure, real EBADF and real positive
prefix writes are exercised; its EINTR/scripted EAGAIN branch results are fixture
overrides, not claims of kernel-origin EINTR. Persistence compares existing packet
bytes with independently opened physical file bytes.

The report does not claim complete TerminalMode/editor/signal/pipe custody,
Main startup, arbitrary Value originals, recorder-death propagation, native
renderer qualification or workload/performance qualification. Those boundaries
remain as stated by the owner. U1–U6 are finite current-owner fixes/oracles, not a
new campaign or a request for a second review.

## Owner dispositions and final direct checks

All U1–U6 are valid and fixed within this sole review. U1's type now explicitly
addresses the decoded original payload; root corrected Main4202 to offset0.
U2's native catalog builder maintains its actual maximum chosen bytes and selected
feed uses exactly the admitted immutable snapshot. U3 marks final borrowed UI/
Task claims and checks actual failure origin before UI load/save/draft/queue
fallback. Its worker origin crosses to UI explicitly with the native failure
handoff, rather than being guessed from ErrorCode. U4 keeps4077 through completion,
uses separate4078 actual perform failure, preholds exact272 on the worker, contains
late failure, joins and propagates without another complete/diagnostic retry.
U5 retains the original handle/thread for exclusive temporary cleanup binding;
other prior binding is restored, wrong-thread methods refuse before effects.
U6 requires successful capture; the handed-off packet is opaque to callers, and
cold selected output still refuses a default uncaptured packet.

`context/session-ui-owner/review-finite-direct.log`: the affected eight direct
targets pass after the whole finding implementation. Genuine queue/allocation,
write/flags/pressure, history/graphics/native PTY cancellation checks remain.
`review-origin-direct.log`: the actual PTY load/save raw refusal and ordinary
corrupt-state cases pass; these replaced weaker helper-only fallback assurance.
`review-final-build.log` and `review-final-direct.log`: matching final UI and
ChatView build/direct checks pass after the final cold-owner check and explicit
worker-origin handoff. Final actual parent scope sees capacity origin; refused
load/save never dispatches, refused worker completion has failed4077/stage3,
exactly one native effect and no complete message. Cold/foreign-binding cleanup
emits original-lane outcomes, restores flags and emits exactly two expected
packet retirements. Missing capture and foreign-thread flush perform no write.
Literal /help and the maximum accepted workflow alias are complete disk originals.

Owned raw_ui, ChatView, terminal and fixture clang-tidy diagnostics are empty;
terminal/fixture were checked again for the final metadata source. Strict native
syntax and owned diff whitespace checks are clean. No new review/panel, full
suite, performance measurement, workload qualification or source gate/commit was
run by this owner. Remaining TerminalMode/editor/signal/pipe custody is a separate
whole H unit, as this subplan originally declared. Main typed provenance wiring
and original initial/resume oracle remain root's integration responsibility.
