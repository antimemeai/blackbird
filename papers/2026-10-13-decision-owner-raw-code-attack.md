# Decision52 / common caller: one code and oracle attack

2026-10-13. ONE independent whole review of live raw_caller.hpp/cpp,
decision_models.hpp/cpp, Coding's confined decision branch, native Child capture/
cleanup and whole tests/decision_raw_test.cpp, against the reviewed caller plan
and its T1–T4 dispositions. No production/test edit, build, provider call or
native execution by this review. Root supplied the published-source red and
isolated direct green in context/caller-owner-source/manual/current-build.log.
Git remains a separately requested caller review; this report does not review it.

No concrete Decision production defect was established in this pass. The native
request-limit encoder actually bounds its acquired string before publication;
credentials/config/stdin remain unselected; explicit immutable operation Ref
selects the actual adapter and child. Every acquired read is published by1404
before observer/limit/parse. HTTP status is captured before range rejection;
JSON Result failure preserves its actual Error plus returned has_value=false.
Actual Child destruction precedes3601, including ordinary/nonstandard observer
throws. Control return/cleanup uses held claims without ordinary admission in
destructors. The helper now pays explicit int/bool terminal extensions and throws
if its request publication fails. Writer elapsed_us is removed; the existing
business deadline clock remains.

The following are direct oracle defects in this changed unit, requiring finite
same-review fixes. They are not new workload/profile qualification or a request
for another review.

## D1 — inner result bytes and causal/cleanup graph are not asserted

Fault reader tests/decision_raw_test.cpp:131–163 checks only1404 originals,3602
body and3601 outer terminal. The main reader similarly counts3600/3601 and
concatenates originals. It ignores3603–3611, all parent Refs and native child
cleanup. This permits an implementation that drops the inner HTTP/parser/
validator/callback records, mislinks them to another invocation, or emits an
outer terminal before child release to pass this fixture.

The particularly useful credit oracle is the existing malformed JSON case:
src/decision_models.cpp:405–413 returns Result failure and emits the maximum53
byte3609 (R24 + branch/stage/ErrorPresent3 + Error9 + actual bool1 + E16).
Removing the sizeof(bool) extension would leave the outer corrupt Error and
original bytes unchanged, while the inner failed-Result terminal could disappear
behind an ingest fence. The current oracle never checks that record. Successful
HTTP3605 is47 bytes, failed HTTP parse3605 is52; do not invent a56-byte status+
Error variant that this actual method never reaches. The helper's maximum56 is
an admission bound, not an expected every-call record length.

Replace these omissions in the SAME reader with exact local expected variants:
3604/3605 pair and acquired200/529 or corrupt without an acquired status;
3608/3609 returned Error plus false for malformed JSON, true for normal JSON;
3610/3611 reached only after parser success;3606/3607 actual channel/presence/
length/throw classification. Require each child1400 parent to be the actual3600,
each1404 to use that1400 Ref,3603 to use that same child, and each inner request/
terminal pair to use its actual request Ref. Check1407/1408/1409 after actual
spawn, exactly once release, and1409 before its own3601 on that producer lane.
Do not equate global recorder order across distinct lanes with native causal order.
The existing normal plus12 faults already reach these records; no new harness
layer or broad campaign is needed to assert them.

Add the already-planned nested observer invocation using two actual adapter
calls on the same lane and distinct actual request Refs. Compare their separate
literal source/body graphs; concatenating all1404 bytes without parents is
insufficient. This addresses parent corruption directly, not correctness of
the reader or a new effect journal.

## D2 — promised pre-effect and paid-stop cases are absent

The fault array covers HTTP/tail/JSON/semantic/overflow/transport/cancel and five
observer throw forms. There is no one-byte-short DATA admission before the
request encoder/credential/native start, missing credential, selected native
body default opt-out, or full ordinary CTRL/sticky stop after terminal admission.
These are explicit reviewed-plan oracles, not newly inferred product scope.
Cancellation after a response observer does not establish that a held terminal
still works with full CTRL, and a response capacity failure does not establish
that rejected body admission preceded external dispatch.

Use the existing native runtime/borrowed producer fault seams and actual adapter:
exactly one byte less than48+request_limit DATA grant, require a capacity Error
at stage2 and zero child start/fixture-side dispatch; missing credential with no
fallback file/environment, require stage3 unsupported plus acquired original
request and zero child; default adapter context=nullptr under an active producer,
require no360x/140x records despite the actual literal response. Once a real
callback owner has admitted its terminal, fill ordinary CTRL or activate the
existing sticky cancellation and throw its actual observer Error. Require exact
3607 and3601 through paid tokens, actual child teardown and original failure
preservation without an ordinary cleanup wait. Avoid stopping the recorder while
holding any ordinary claim needed before this terminal cut; the cut must reach
the intended owner, not hang earlier at callback request admission.

## Scope and evidence limits

The12 fault cases are useful direct original-byte and native throw-class oracles;
their emitted binary sentinels and literal request expectation are independent
of the production encoder. The semantic-invalid fixture lacks usage and therefore
fails before its noul=2 probability check; it establishes validator rejection,
not that exact numeric rule. Existing decision_models_test separately covers the
numeric rule. No additional production repair is demanded by that distinction.

This report completes the one code/oracle pass. Root owns the two finite oracle
repairs and their direct recheck; no rereview or assurance panel follows. Passing
these owner cases would still not qualify throughput, capacity sizing, remote
provider behavior or remaining nls.2 owners.

## Root dispositions and finite recheck

D1 and D2 fixed in the same owner reader. Literal native bodies, actual Ref graph,
failed-Result53, callback throw classes, reached boundaries and once-only child
cleanup now checked. The real nested observer invokes two adapters and checks
separate originals plus inner completion before outer callback/child completion.
One-byte-short encoder admission, missing credentials, default opt-out, full CTRL
and sticky stop use the real adapter and existing native producer seam. Mac shared
name length and explicit RawChannel::unlink API fixture corrections were necessary;
no production repair was demanded. All owner cases pass in
context/caller-owner-source/manual/nested-recheck.log. No rereview.

Same final source-gate static disposition (2026-10-14): repeated optional
snapshot access was not analyzer-proven stable. The const discovered vector owns
its run/snapshot for the full reader block. All three Decision guards now retain
single-run/presence/closed checks and the existing error check where present,
name the same run/snapshot once, and explicitly reject absence before dereference.
Reader paths and every byte/outcome oracle are unchanged; no retained-data copy.
That narrow recheck exposed two expected Error accesses after a parsed
error_present/availability equality check. The original equality and exact
code/detail assertions remain; the reached branch explicitly rejects an absent
expected optional, then borrows its owned Error by const reference. Root approved
this same finite analyzer correction before writing it. One narrow Decision TU
clang-tidy recheck is GREEN (context/preserved-run-guard-finish/
decision-static-final.log); dry formatting/diff checks pass. No new panel or
native-suite repeat: the preceding full Debug source gate passed131/131 tests;
Root owns the required installed gate retry.
