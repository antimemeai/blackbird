# File replacement effect observation

The audit's [E4](../cpp-bug-class-audit-2026-10-15/EFFECTS.md) demonstrates that
successful rename is followed by allocating result construction. A one-shot
allocation failure then hides the known destination replacement in a generic
allocation error. This is effect knowledge loss, not a failed rename or replay.

The plain and traced rename owners will issue a nonallocating
`FileEffectObservation` immediately after actual rename success. Its public API
only observes applied state; private mutation is restricted to those native
owners. Existing unobserved writer APIs remain compatible. LocalTools prepares
the complete allocating success Value before replacement and threads an optional
per-call observation to the native writer. It does not retry any native effect.
The successful result explicitly reports effect_outcome=applied.

Root owns engine integration: preserve applied in an error result independently
of allocation/capture status, including one bounded recovery from bad_alloc during
result preparation after observed application. The recovery retries only result
preparation, never the write/edit. Existing raw/semantic capture failure prevents
that ordinary fallback and keeps the blocked-writer fence. Persistent fallback
failure remains a failed observation rather than permitting another effect.

Permanent engine checks intercept only the fixture destination's successful real
rename, arm exactly the next C++ allocation failure, and count successful renames.
Both write and edit must produce exact destination bytes once, report allocation
plus applied in the returned and retained operation result, and permit a later
read when observation retention succeeded. Plain and raw-on invocations exercise
both actual rename owners. A native observation counterpart verifies known success
survives beyond the allocation boundary. Test bytes, retained packet decoding,
actual rename count and later read are separate oracles for different claims.
Start with RED against the current owner/engine before the product edit. Preserve
exact logs in ignored context/cpp-intermission-2026-10-15/file-effect/. Root reviews
this unit once and integrates required gates; this lane never runs shared Ninja.

## Owner implementation and integration review

`FileEffectObservation` has no allocating state. Plain
`write_file_observed` and traced `raw_file_write` alone can set its private applied
fact. Both mark immediately after the actual successful rename, before any raw
return-publication/check or other post-rename work. Failed rename cannot mark it.
LocalTools prepares its complete `{written,path,effect_outcome=applied}` Value
before replacement. Historical public unobserved write signatures still work;
new observer parameters are trailing and optional.

Root integrates the stack-local fact through CodingEngine. One bounded result
preparation fallback preserves `error=allocation` alongside applied; its adapter
receipt remains the actual adapter outcome rather than being rewritten as a
failed effect. A second allocation failure fences further admission. Capturing
the failed observation remains distinct from knowing the replacement happened.
The operation activity and process-output text also preserve that known fact.

The permanent fixture now has eight private modes: write/edit, each plain/raw;
persistent-plain/raw; native-plain/raw. Persistent modes arm two consecutive
allocation failures only after actual successful rename, then require the first
call to stop and a later offered replacement to leave the rename count at one
and destination unchanged. Native modes verify the exact next allocation cannot
erase applied state and an actual failed rename to an existing directory cannot
issue it or alter the directory's sentinel. The native fault consumption shares the allocation helper used by global
operator new; its atomic fault check and bad_alloc throw remain observable in
Release even if an unused malloc/free pair could be optimized out. The engine's four normal modes additionally require failure
activity to say replacement applied.

The initial four engine modes fail against the old implementation after their
real rename-count, injected-allocation, and exact-bytes assertions pass; each
fails the required returned applied fact. The first hook setup mistakenly matched
`/tmp` against the writer's canonical `/private/tmp` destination; the fixture now
uses its actual canonical target. The first four corrected engine cases pass in
root's integrated Debug log
`context/cpp-intermission-2026-10-15/effects/file-initial-green.log`. No claim about
the later eight-mode batch or sanitizer/Linux source gate is made until root's
corresponding execution completes.

Root's one independent owner review found no defect in marking/order/preallocation.
This lane's independent review of root's file integration found no defect in
receipt separation, the bounded no-effect-replay fallback, raw-capture exclusions,
failed-fallback admission fence, applied capacity result fields, or human activity
wording. Domain refusal integration has a different independent reviewer. No
second assurance panel or review-of-review is introduced.

## Failed bounded recovery: actual new-turn fence gap

The first complete eight-mode Debug run passes six and fails both persistent
cases. Isolated diagnostics establish that the fixture reaches the actual intended
boundary: allocation fault one is Encoder::byte during primary encode_packet;
fault two is Value::Value() constructing the fallback's Value{} argument inside
Boundary::dispatch's recover_allocation. Both plain/raw first calls stop with
exactly one replacement and two injected failures; the journal writer remains
live. A subsequent operator_call nevertheless performs replacement two and writes
FORBIDDEN. This is a product gap, not an assumption that the second allocation
necessarily means failed recovery.

Root identified why the existing capacity_stopped flag was insufficient:
operator_call creates a new turn, and turn intentionally resets that ordinary
per-turn flag. Root will add a persistent per-engine unretained-file-effect fence
checked at turn and operation admission. No new clearing/replay API is selected.
The permanent two-fault oracle is retained unchanged. Prewarmed native stack
capture exists only in the ignored isolated diagnostic executable, not production
or the permanent regression. Exact diagnostics are
`context/cpp-intermission-2026-10-15/file-effect/persistent-{plain,raw}-diagnostic.log`
and `persistent-diagnostic.cpp`/build log. This is a correction driven by a real
new failed regression within the implementation layer, not an extra review panel.

The correction uses a per-engine file_effect_unretained flag and an operation
scope guard. The guard sets it only when the actual owner observed applied and
the primary operation.result was not successfully retained; it therefore covers
failed fallback and other intervening exceptions without classifying no-effect
failures as applied. Successful actual primary retention plus locator/index
assignment marks the guard retained. Turn admission checks the persistent flag
before ordinary per-turn reset, and operation admission checks it too. The flag
has no automatic clearing path. This lane's narrowly targeted source disposition
finds the demonstrated new-turn escape closed; root owns behavioral re-execution.

The final integrated eight-case re-execution passes8/8 in2.15s, including both
unchanged persistent-failure cases. Log: effects/fence-green.log. Root's ordinary
whole-source gate and changed-profile checks remain independent qualifications.

## Source-gate analyzer correction in the native fixture

The full192-test Debug run passes, then the required full analyzer reports
clang-analyzer-unix.MismatchedDeallocator in the direct native oracle's explicit
operator-new/function-pointer allocation followed by explicit operator delete
(which this fixture defines using free). The repair changes only the fixture:
the actual fault check and malloc implementation now live in one
allocate_fixture_bytes helper used by global operator new and by direct native
fault consumption. The latter matches its malloc result with free. This is the
same actual rename-armed fault mechanism; engine write/edit and finite two-fault
checks still intercept global operator new. There is no NOLINT, suppression,
deallocator indirection, or product behavior change. The atomics and fault throw
cannot be elided as a replaceable-new expression, so no volatile function-pointer
workaround is needed. Direct owning-TU clang-tidy with the installed profile exits0
with no diagnostics; exact output is ignored
`context/cpp-intermission-2026-10-15/file-effect/final-fixture-clang-tidy.log`.
Root coordinates affected Debug/ASan reruns before final publication gate.

After coordinated Ninja handoff, only file_effect_test is rebuilt in Debug and
ASan/UBSan. All8 Debug cases pass1.79s and all8 ASan/UBSan cases pass3.56s,
including actual rename/global-new faults, both failed bounded recoveries and the
native direct observations. Logs/build outputs are
`final-fixture-{debug,asan}{,-build}.log` in that same ignored file-effect directory.
The analyzer correction changes no product binary or performance measurement;
source is stable for root's final installed gate.
