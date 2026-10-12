# ONE audit-query code/oracle attack

Reviewed the source-stable candidate after the owner's actual debug audit,
trajectory and audit_query_raw 3/3 green and narrow static qualification. Scope:
raw_query.hpp/cpp, audit.cpp, the AuditLog query declarations, three Coding
query-dispatch arguments, runtime description installation, native fixture and
resolved query subplan/ONE plan attack. Also checked actual raw claim/publication
and retained fact/attempt contracts. No source/oracle edits, compiler workers,
new panel or repeated qualification run. Root owns finding disposition.

No production defect was found in this attack. Two concrete native-oracle gaps
remain against the approved direct cuts.

## QO1 — no actual cancelled next-read admission cut

The subplan's direct oracle4 requires ordinary capacity filled with the actual
recorder parked, cancellation of the next lookup before its native read, prior
returns preserved and the already-held query terminal published. The candidate
native fixture has no RawWaitScope, recorder stop, physical lane fill, cancellation
endpoint or blocked-admission observation. Its injected pread EIO and allocation
failures exercise different paths: each occurs after lookup admission succeeds.
They cannot establish the held terminal remains usable when ordinary CONTROL is
full and the next lookup's constructor refuses before acquisition.

This matters specifically to audit.cpp's successive fact/payload lookups and
nested packet helper: each fresh claim may throw before its request is published,
while RawQueryTrace's783 and an enclosing packet-helper/fact grant must survive
unwinding. Inspection finds the intended ownership, but the direct dynamic claim
has not been exercised by this candidate.

Resolve within the existing native fixture using a real query with an observed
successful first acquisition, then fill actual unheld ordinary CONTROL under a
confirmed stopped test-owned Rec/Sup and observe the actual next admission wait.
Cancel with its native wait control. Check the exact previous request/return bodies
remain, the unadmitted next read has neither a fabricated request/return nor an
actual native read, one linked interrupted query terminal is last, any already
admitted helper has its honest reached-stage failure, and semantic cursor/facts
are unchanged. Restore/kill only test-owned stopped holders on all exits before
runtime retirement. A ready cancellation endpoint before query construction is
not this cut: it refuses the initial owner and never tests the held owner terminal.
No production timer or test-only source callback is needed.

## QO2 — attempt selection/skip raw branch is never exercised

All five native attempt-state queries use end0 at
 tests/audit_query_raw_test.cpp:587–620. They correctly check the attempt lookup's
optional fields, but deliberately execute zero fact iterations. Consequently no
candidate native capture can contain audit.cpp347's2304 skip_attempt3 or prove
that an attempt-selected fact's2304 selected1 links its own actual fact-read R.
The variable fixture checks selected1/skip_variable2 only. Existing trajectory_test
checks business sibling filtering with raw disabled; it cannot observe these raw
parents/branches.

The direct subplan explicitly requires variable and attempt selected/skipped
branches with literal native bodies. Resolve by extending the real admitted
attempt fixture with a known matching fact and an unrelated sibling inside a
pinned nonempty prefix. Require actual business selection plus every exact fact
lookup/result and2304 selected1/skip_attempt3 parent, in native ordinal order,
followed by the exact terminal cursor/next/row fields. The expected branch and
reference must follow the independently constructed fixture, not candidate
summary/filter helpers. This is a confined oracle correction; no production
filter refactor is indicated.

## Checked with no finding

The fixed request/parsed-field/terminal bank783 covers74+11*57+82; bounded
variable DATA112 is held before query validation/copy. Fixed subtree sizes agree
with native layouts: fact158, attempt163, payload/source115, diagnostic147,
read-packet149 and decode-packet173. Claims stay issuer-side and nested grants
cannot consume held parent terminal/filter credit. All selected requests and
returns are distinct. Every complete result Value exists before2306 success;
Error/allocation/unknown paths use one held terminal with honest reached stage.

Fact results copy actual RecordReference, RetainedKindu16 and evidenceu8 before
summary allocation. Attempt results copy actual native flags/optional fields,
not a derived terminal. Ordinary reads remain at their existing business sites;
summary's application/Decision/Observation reads, inspect's separate payload
read and the diagnostic metadata reread are separately recorded. Decode failure
is separate from byte-read success, including handled summary errors. Direct
packet decode links its actual prior acquisition; diagnostic links its real
metadata read and already-computed native seconds. No new metrics read, encoder,
semantic append or transformation is introduced.

The exact byte/body fixture is independent of candidate enums/decoder and reads
actual closed disk preservation. It sorts a single actual lane by ordinal rather
than equating DATA/CONTROL preservation order with event order. Actual selected
journal-inode pread EIO after reopen, summary/result/hex allocation failures,
source binary output, packet/variable branches and optional attempt fields have
concrete direct oracles. The demonstrated source assignment changes copy to move
without changing existing validation/read order. Optional operation-attempt input
comes directly from the actual Coding operation lambda; standalone callers may
omit it. Description installation is exercised through the actual runtime.

Physical partial diagnostic-read originals remain explicitly assigned to the
native storage owner. This query attack makes no claim that query failure events
fix that separate custody defect, nor that unchanged business tests qualify the
two raw fault/branch cuts above. Supported final profiles and integration remain
root's gate after the finding corrections, not an additional attack panel.

## QO1/QO2 finding-fix disposition

The reviewer owned only the existing native fixture for this ONE correction
layer; query source/header/CMake stayed with root. QO1 now forwards the actual
cold journal header/frame preads. After the first successful frame, the next
real packet-decode allocation fills unheld CONTROL with fixture-only9290 frames.
Actual query ordinals1–10 (including the exact byte return) precede that fill;
the already-held helper return/filter and interrupted terminal follow it. The
native poll forwarding seam observes its actual cancellation watch unready;
a separate fixture controller writes the pipe only after that wait. Exactly one
header and one frame read occur, the next lookup has no request/return/read,
ECANCELED is retained, Rec/Sup remain natively stopped through terminal emission,
and semantic cursor/facts remain unchanged. Exact owned holder resume and controller
release/join guards run on logical failures before runtime retirement. Physical
filler events are also disk-preserved and checked for one lane/consecutive ordinal
order before the fixture removes them from the query-only body oracle.

QO2 now scans a real matching terminal observation and two distinct sibling
invocation/admission facts in a nonempty pinned prefix. Literal full bodies require
one selected1 and two skip_attempt3 outcomes, each linked to its actual fact-read
R, plus the actual attempt optional fields and exact terminal cursor/next/rows.
Business output contains only the independently constructed matching observation;
no semantic append occurs.

The old fixture had no QO1 cut and pinned end0 for QO2; these were oracle findings,
not an asserted production red. First strengthened candidate execution genuinely
refused the malformed sibling fixture because its Invocation was missing from the
Decision's actual planned list; that native declaration was corrected. The first
cancel expectation also incorrectly counted the header and frame as separate
payload acquisitions and expected detail0 instead of actual ECANCELED. Arming on
the actual frame and checking one header/one frame plus native ECANCELED corrected
that cut. No production defect appeared and no production source was changed.
The exact failed runs remain context/query-oracle-direct.log,
query-oracle-after-planned-invocation.log and query-oracle-cut-observation.log.

Isolated strict debug build/nls-query-oracles used one compiler worker. Final
existing audit_query_raw passes0.84s; changed-file clang-tidy exits0 with no diagnostic.
Logs: context/query-oracle-final-build.log, query-oracle-final-green.log,
query-oracle-final-tidy.log. The whole fixture is source-idle for root's supported
profiles/integration; no second review panel or repeated assurance layer.
