# ONE audit-query plan attack — source held

Reviewed context/audit-query-raw-subplan.md against current src/audit.cpp,
context.hpp, retained_state.hpp/cpp, journal_writer.hpp/journal.hpp,
retained_events.hpp, diagnostics.cpp, CodingEngine's actual operation lambda and
three dispatch branches, raw claim/publication lifetime. No source/compiler/static
changes; this is one independent plan attack, not a new panel or implementation.

## Q1 — fact-return fields are unavailable; two native widths/banks are wrong

The draft states2303 fact-success PhysicalJournalRecord65+RetainedKind2 and says
capture actual fact.record/checksum. Actual RetainedFact::record is RecordReference
(journal16,sequence8), NOT PhysicalJournalRecord. RetainedFact has event and
RetainedEvidence; no environment/kind/batch_first/payload_offset/payload_size/checksum.
RetainedState::fact archive path deliberately returns only {journal,sequence}, decoded
event,evidence; resident path has the same type. Exposing/reading physical metadata
solely to populate metrics violates the draft's no extra traversal/acquisition rule.

Also native FrameKind is uint16, not the draft's uint8; PhysicalJournalRecord fields
would total66 if an actual owner legitimately had them. Fact request has queryRef24+
kind:u8+index:size_t8+envelope16 =49, not48. Current stated197 complete fact bank is
therefore not even sufficient for its own hypothetical request/body/filter layout
(49+109+41=199, not197).

Resolve by recording actual RecordReference24+native RetainedKind2+existing
RetainedEvidence1. Fact-success2303 then16+lookupRef24+branch1+record24+kind2+
evidence1 =68; fact subtree49+68+optional filter41=158. No fabricated checksum or
physical record. If future native storage owner has physical metadata, capture it
there on that real returned object and source owner, not by adding metrics reads here.

Direct oracle: real resident fact + compact archived fact with same logical identity;
exact fact return fields match known journal/sequence/kind/evidence, no source-metadata
acquisition is introduced. Explicit independently authored layout/size expectations
must not use candidate sizeof(struct PhysicalJournalRecord) or candidate decoder.

## Q2 — summary's reads and handled failures are not concretely enumerated/prepaid

Existing summary(fact,i) itself performs payload reads/decode BEFORE filters:
* ApplicationRecordEvent log/program payload<=65536: unwrap(read_packet(payload));
* DecisionEvent continuation<=4096: read_packet, errors handled by omitting time;
* AttemptObservationEvent observation<=4096: read_packet, errors handled by omitting
  result references. These are actual acquisitions even if query ultimately succeeds
  or skips a row, and a handled read/decode failure still needs its own failure event.

The draft's source windows discuss payload helper and actual selected record reference,
but do not spell these three summary calls or their handled failure behavior. Calling
read_packet unchanged does not expose the actual intermediate vector return/size to
2303; instrumenting only inspect's payload(fact.body) misses summary reads entirely.
A trajectory variable filter depends on decoded summary fields, so this is not an
optional rare path. inspect reads summary payload, then payload(body), and diagnostic
branch reads application payload again: these are THREE actual existing calls in that
case, not one read to be measured as though reused.

Resolve with a query-owned read_packet helper that preserves existing size-check/read/
decode order and existing failure handling, passes the actual fact RecordReference,
pays each actual payload read subtree before immutable.read(), and publishes the
literal vector acquisition return before decode/format/allocation. Keep decode failure
separate from byte acquisition success; capture its real returned failure even when
summary's Decision/Observation branch handles it. Either add an explicit parse/decode
req/return variant to2302/2303 or explicitly select the packet-helper acquisition as
a typed lookup return with its actual Result<Value> status; don't substitute a made-up
byte failure for a successful byte acquisition followed by corrupt decode. A held fact
branch token survives nested fresh payload grants/cancellation, and all lookups refer
to the same query root and actual selected record. No metrics-only extra reads/decode.

Direct oracle: valid variable.sample application, Decision payload with handled corrupt
packet, AttemptObservation corrupt packet with handled omission, inspect source and
provider diagnostic branches. Verify actual existing read count/order, each request/
return including handled failure, and successful overall query where current business
code handles the error. Forced summary allocation after fact return must retain that
fact return and emit one query failure, without false lookup error or filter selection.

## Q3 — diagnostic request omits decisive actual native arguments

read_diagnostic(name,now) uses existing actual parsed filename and uint64 now for
expiration; current2302 diagnostic request lists only an actual retained RecordReference
or SourceReference. A record reference alone does not capture the actual now used to
return stale vs available. Native diagnostics are optional/non-authoritative, so a
later read of the same metadata is not the acquired request. Inspect computes seconds,
checksnegative, then passes nativeuint64 now; that existing value must be copied before
read_diagnostic, with no second clock read/conversion in writer.

Resolve a diagnostic-specific request carrying actual now:uint64 and either exact
already-owned filename bytes, or an explicit actual metadata-payload read Ref plus
selected native RecordReference that unambiguously identifies that existing filename.
If copying filename, admit its exact size BEFORE the diagnostic call (invalid strings
can exceed53; diagnostics.cpp rejects names only within the callee, so do not assume
all incoming metadata strings already satisfy its canonical filename bound). Expand
request/body and held bank accordingly. Do not drop invalid/missing/expired failures.

Direct oracle: same retained provider.stream metadata, known expiry, actual now before/
after expiry + malformed filename; distinguish request's native inputs and failure.
Physical partial read original preservation remains correctly deferred to native
storage owner as explicitly stated by draft; don't promote query return coverage to
that qualification claim.

## Q4 — attempt-success 'terminal:bool' is a new derived value, not actual field

AttemptState has admission, opened:bool, optional observation, optional receipt,
evidence, reconciliation_required:bool. It has NO terminal bool. Existing query uses
admission invocation/decision and formats them; computing terminal from phase adds
new semantic interpretation at the instrumentation surface, contrary to raw originals.

Resolve by copying actual opened, observation-presence, receipt-presence, evidence,
reconciliation_required and (when present) native observation phase/disposition/
receipt failure fields as explicitly selected. Or retain only opened and reconciliation
if that is the intended measurement surface; do not call observation presence terminal.
Recalculate149 attempt subtree from the final actual layout; opened/observation-present/
reconciliation-only keeps current92 return but must use honest field name.

Direct oracle: unopened admission, opened/no observation, running observation,
terminal observation, adapter receipt withfailure and recovered reconciliation. Exact
native fields distinguish them without a candidate terminal-computation helper.

## Checked / no finding

2300 maximum74 is correct on qualified size_t8 hosts:16+method1+presence1+attempt16+
JournalCursor32+fact_count8. Parsed max57 is16+queryRef24+field1+nativeID16. Variable
DATA112=16+R24+length8+64; default parse field max57 times11 is conservative across
the actual field union, not necessarily a per-method dynamic count. Terminal82 covers
hex shape16+R24+branch1+shape1+five size_t8; page/packet/trajectory are smaller.
Byte-return max50 covers native Error9 vs size_t8. Source request65 is correct.
Result Value must be fully staged before terminal; failure/destructor held terminal
and actual reached-stage are correct, provided constructor/admission ownership is
exception-safe and finish is single-use. Optional attempt is available in the existing
operation body lambda; pass it directly, not inferred from result packet or lookup.
Moving variables wrapper into AuditLog correctly captures copy/check/add failure
before trajectory; wrapper must not create a second query request/terminal when it
calls the selected trajectory implementation.
Raw-disabled CLI not creating a capture run, independent reader not acknowledging,
no semantic append, native storage partial diagnostic custody remains deferred,
and actual redundant raw=original copy measurement/fix are sound boundaries.

## Disposition

The revised [owner subplan](2026-10-11-audit-query-raw-subplan.md) resolves Q1–Q4
before baseline/source: actual RecordReference/kind/evidence158, actual attempt
fields163, each existing read and packet-helper request/result149/173, and
diagnostic metadata-read Ref/native now147. No source result is claimed by these
plan resolutions. No additional plan attack is requested. Relevant direct oracles are
within the same owner fixture; supported qualifications/static/root one code attack
remain the existing unit process, with no new panel, timers, dependencies or evals.
