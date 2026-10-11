# Task owner raw plan: one source-grounded attack

2026-10-11. Reviewed papers/2026-10-11-task-owner-raw-subplan.md against
include/blackbird/tasks.hpp, complete src/tasks.cpp, AuditLog::record in
src/context.cpp, the packet codec/default ValueLimits, protected claim/split
semantics, RawRegionPrepaidScope/retained append, and the owning task fixture.
This is the single requested plan attack. No source edits, test runs, compiler,
new literature campaign or additional panel.

The16MiB+48 DATA allowance is grounded in the actual source. Two concrete gaps
need resolution before task source release.

## 1. The actual publication wrapper can throw after successful retained append

TaskStore::edit calls the string-returning AuditLog::record overload, discards
its result, then commits the task state. That overload first issues the actual
ApplicationRecordId, calls its void record overload (encode and root.submit),
then calls hex_identity. hex_identity reserves a32-byte string. Allocation can
fail after root.submit succeeded and before TaskStore::commit is reached.

The plan describes1705 as the actual append result and distinguishes append
failure from post-commit callback failure. An Error/exception from this wrapper
is not necessarily an append refusal. The concrete source admits a retained
accepted delta with old working task state and no callback. The prepared commit
credit cannot repair that product gap, and recording the wrapper exception as
rejected append would misdescribe the actual effect.

Resolve with the existing void overload at the task caller: acquire the existing
business identity before the publication operation, call record(identity, program,
packet), and omit the discarded post-append hex formatting entirely. This does
not introduce another identity, encoder or metric transform. Alternatively, if
keeping the wrapper, explicitly describe its owner boundary and distinguish actual
retained acceptance followed by return formatting failure; do not call that an
append refusal. The simpler void-overload route removes unnecessary work and the
unsafe interval.

Direct oracle: force allocation refusal at the current post-submit formatting cut
and check the actual retained cursor/delta, old task revision and missing callback.
That is the authentic red business behavior. Candidate's chosen route must then
show successful durable acceptance followed by task commit/callback without that
post-submit allocation, while retaining the separate actual pre-append failure
case. A raw1705 success must correspond to the actual publication owner returning,
and later task commit must remain prepaid/nonallocating. If actual publication
identity is additionally selected in1704, copy that already acquired native16-byte
identity and update its body/total; never create a metrics identity or format it.

Source: src/tasks.cpp TaskStore::edit around582–591; src/context.cpp58–67;
hex_identity around23–33. This is an existing business hazard discovered at the
boundary the new hooks intend to report, not a defect created by claim/split.

## 2. Restore/replay need an explicit post-effect site and finite control subplans

The event table says1708 is snapshot return after its projection is assembled,
and1706 is commit return after state transfer. restore does not call commit or
snapshot: it validates/stages candidate, then directly moves candidate into *this.
Its successful state transfer otherwise has only a generic terminal in this plan,
although the introductory scope selects the owner's native next/count/container
fields. Obtaining them via snapshot serialization or Value parsing would violate
the stated design. Replay does call prepare and commit, but their parent/action
mode and commit credit route are not written action-by-action.

Resolve by naming the restore state-publication boundary explicitly. Reuse1706
as native state-publication return whose request Ref identifies commit versus
restore, or add a specifically described native restore return. Copy native
revision,next,item/order/receipt sizes and maintained populations immediately
after the existing *this move; no projection/serializer/traversal. Prepay this
record and terminal before replacement. Extend the descriptions accordingly.
Specify replay's actual parented prepare/commit modes and the held221-byte commit
child at its real final state transfer; replay must not be relabeled a live edit.

With the proposed existing body sizes, a standalone commit or restore using1706
needs26+120+51=197 CONTROL bytes (parented221). Read needs26+121+51=198,
snapshot26+80+51=157, prepare26+129+51=206 (parented230). Replay can hold its
own26+51 plus the final parented commit221 before work; its parented preparation
230 admits before its existing encoder, because no replay product effect has yet
occurred. These are examples of the exact owner table to write, not another
ledger or a requirement that all replay preparation capacity remain held after
its publication. The current edit sub-bank476 and commit221 arithmetic is correct.

Direct oracle: actual successful/malformed restore under capture; successful
restore publishes its real native resulting fields and releases unused credit;
malformed restore leaves the old state and publishes the observed error stage.
Full/canceled admission refuses before *this replacement. Actual replay emits
replay parent/prepare/commit modes, validates the stored timestamps as existing
business code does, and publishes its final committed fields from held credit.
No1704 live-edit publication or changed callback may be fabricated for replay.

## The large source-string claim is the right constraint

TaskState::prepare currently performs keys/op_id validation, then calls
encode_packet_string(request) with default ValueLimits, then checks whether the
returned string exceeds65536. The codec's actual byte cap is16*1024*1024 including
its native BBM framing. It can successfully return a string over65536, which the
business method then refuses. That successful original must be captured before
that refusal; failed encoding yields no successful string to fabricate.

1701 is E16+prepare R24+size_t8+actual bytes:48 overhead. Holding
ValueLimits{}.bytes+48 before the existing encoder is therefore correct. It holds
capacity, not another16MiB allocation. A metrics-only pre-traversal, additional
encoder, Number.text parse or lower codec limit would change this contract.
Retain the one existing business result, raw-copy it once using the held DATA
claim, release remaining claim immediately, then execute the original65536 check.
Do not leave that unused giant claim held through later prepare/append/replay work.

Add the direct cuts to the owning fixture: a deliberately smaller physical DATA
lane refuses before the existing encoding acquisition; actual full protected
16MiB+48 admission cancels without publication/effect; a successful over65536
encoded input is preserved byte-for-byte before invalid_range; a codec failure
has no1701 bytes; a small source frees unused allowance immediately before nested
work. Include one actual codec-bound returned string if constructing it directly
is cheap, otherwise do not claim exact upper-bound success merely from small
fixtures. The source cap establishes the maximum; the dynamic checks attack its
admission/release/error boundaries. Raw disabled still performs exactly the one
original business encoding.

## Other checked boundaries

Native body maxima26/50,129,120,121,80,40,50,64,50 and51 match the documented
individual fields on the qualified64-bit profiles; do not copy native structs
with padding. Edit's476 post-request bank includes terminal, publication pair,
parented commit221 and callback pair90. Split the commit child before its state
transfer and do not admit again after publication acceptance. Native maintained
counts and container sizes need no metrics traversal. Existing read offset is a
uint64_t business local; copy that actual declared type rather than silently
normalizing it to size_t, even though the selected profiles have equal widths.
Cursor/returned count are existing size_t locals.

Callbacks may reenter read/edit/replay or throw arbitrary exceptions. Their own
admission may wait because they are independently invoked business operations,
but their emission cannot consume the outer bank. Keep any retained-append
prepaid borrowing scope limited to the intended actual record call, not around
changed(packet). Parent refs come from actual call arguments/retained Prepared
Ref; the binding/lane may differ later and no nearest event is inferred. Literal
expected bodies should use the native fixtures and independent original packet
bytes; retain actual baseline red rather than disabling hooks in a candidate.

Root owns resolution of these two findings, source release and later one code/
oracle attack. No review of this review or new assurance unit is proposed.
