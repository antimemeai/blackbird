# Restart/recovery/successor owner's ONE code and oracle attack

Read the complete raw_restart helper/header, current Session restart/resume/
restore, Coding recovery/validators/take/schedule, Main startup recovery and
new/resume/restart/outer handoff, shared caller/session/runtime machinery, the
426-line native fixture and existing recovery semantics fixture. Read the actual
native/baseline logs and the acquired GCC13 filesystem implementation and Darwin
SDK interface. No execution, compilation, source changes, credential access or
second review in this attack. Coordinates refer to the reviewed candidate.

## RRC1: rejected custody still releases the issuing producer's bank

src/raw_restart.cpp:175–199 detects wrong process/thread in retire(), calls
raw_fence(), sets a failed Result, then unconditionally resets bank_ and issuer_.
An unrelated thread calling consume() therefore receives wrong_incarnation but
also destroys the live creator's held credit. If it has no current raw binding,
raw_fence writes no event; if it inherited the selected producer across fork,
raw_fence can itself write through the violated SPSC producer. Neither behavior
preserves the ownership contract. The bank cannot be discarded on failed custody.

The same class's joined_ boolean (header42; cpp208/230) subsequently permits ANY
thread once set. It proves neither that this caller joined the worker nor that
this caller is the intended outer receiver; another thread can rebind the
selected issuer and publish concurrently with that receiver. Current actual
Main sequencing is joined, but the helper's advertised rejection path is unsafe.

Capture the outer receiver PID/thread at holder construction. Worker select may
retain its actual issuing producer; only that intended receiver may record actual
join and perform cold exec/consume/retirement after a worker selection. Check
custody before bank release or publication. An ordinary invalid method call must
leave the holder intact for its legitimate receiver to clean up. A destructor
that cannot legally release its members must use the existing fatal custody
policy before member destruction, without writing through the violated producer.
Do not introduce a new registry, migrated bank or cleanup producer.

Direct cuts: real select on one producer, unrelated-thread consume while creator
is still live, then correct receiver consume must still emit its one4482 using
the original ref. Actual worker select followed by std::thread join must permit
the designated receiver, while a third thread after join still refuses. Fork
after selection must not publish or release through the parent's producer. The
illegal-destructor subprocess must take the existing fatal path, preserving prior
raw bytes and unresolved process-death handling; do not claim orderly cleanup.

## RRC2: missing audit now throws instead of returning the original predicate

src/raw_restart.cpp:117–125 replaces the throwing is_regular_file(path) used by
Main with its error_code overload and throws for every nonzero error_code. These
overloads differ on not-found status. Acquired GCC13 fs_ops.cc1473–1516 returns
file_type::not_found and sets ec for ENOENT/ENOTDIR; throwing status1560–1566
throws only for file_type::none. Therefore original is_regular_file(path) returns
false for a missing file or a non-directory component. The new wrapper throws a
filesystem_error instead. Main /resume with an existing directory lacking audit
now misses its original invalid_range branch. Default opt-out changes too.

Preserve the original throwing predicate semantics with ONE underlying status
acquisition, retaining its actual error_code and returned bool. Account for the
actual status alternative, rather than assuming every ec means a thrown method
or adding a second status call. Returned false remains a returned native variant
even if that error_code overload supplied ENOENT. A real status failure must
retain the original filesystem exception class and native code/category.

Direct old/new comparison on actual missing path and an actual regular-file
parent/non-directory child must return false in both selected and opt-out calls.
Compare literal4463 return/error/category/bool. Actual permission failure must
still throw and emit its error variant. The actual Main /resume missing-audit
case must preserve invalid_range and perform no selection/exec/argument mutation.

## RRC3: material missing native oracles

tests/restart_recovery_raw_test.cpp validates useful local semantics, actual BBM
metadata, native enum width, and real failed/successful exec including CLOEXEC
drain. Existing session_recovery_test comprehensively checks recovery/link/no-
replay business behavior, but its calls are opt-out and cannot qualify the new
literal fields or recording refusal. The following source errors currently pass:

- Session4400 cursor sequence/end and Coding4420 generation are read/discarded.
  Most4402 roles only get a length check;4404 token,4405 locator,4406 mutation and
  reached predicates are not checked against the actual retained restart/context.
- Recovery4442 span count,4444 attempt/decision/invocation/opened,4446 actual link
  variants,4447 reached conditions,4448/4449 collected state and4450 actual append
  arguments are not validated.4451 returned cursor is discarded. Wrong native
  identities/counts or cursor can pass while the separate business oracle is right.
- Handoff tests select and execute on the same OS thread. They do not exercise
  real worker→joined receiver, consume/abandon, failed pointer-vector preparation,
  or the rejection paths in RRC1. The Main baseline proves missing sites for
  once restart, but no candidate cut exercises actual /new or /resume in-process
  reopen versus restart exec choice.
- There is no real recording source/admission refusal, post-acquisition allocation,
  or full-CONTROL/held-outcome cut for this owner. A method ending unfinished after
  a reached source failure can pass the current successful/busy/invalid checks.

Extend this SAME fixture with independent retained IDs/cursors/native argv and
the actual supported exception/refusal paths. Use the genuine RRC1/RRC2 cuts
above, actual Main in-process selection, and one source/acquisition failure with
already-paid terminal/outcome. Reuse existing shared transport coverage; no new
campaign, timer, dependency, profile, performance claim or second panel.

## Documentation conformance and rejected suspicion

The authoritative plan still states4440 mode:u8/73+52 and4442 bank100, followed
by correct native-int128/verifier110 corrections. Integrate the actual rows.
Its4467 paragraph says restartPending/newPending, while code and description emit
exec/plain; make the accepted literal names/actual selected branch coherent.

The suspected legacy-input short-circuit change is NOT a finding. Exact original
context/restart-recovery-owner/recovery-before.cpp and published source already
encoded legacy input before its object check. Preserve that original order; do
not change it on the strength of an invented baseline. No other production
defect is asserted by this attack. Fix RRC1/RRC2 and fill the concrete RRC3 cuts
within this same unit, then direct recheck; no rereview required.


## Finite disposition

RRC1/RRC2 source repairs and RRC3 direct oracles are implemented. Actual native
fixtures pass in context/restart-recovery-owner/finite-fourth-native.log and
finite-second-native.log (Main's case passes there). The final source gate will
recheck the integrated tree. The actual persisted append boundary includes an
exec marker's prior identity reservation; the independent disk-frame oracle
therefore uses that actual batch boundary. The allocation cut permits a real
issuer-reservation fact but forbids a new restart program, locator or state.
These correct fixture assumptions, not production behavior. File8's native
resolved-path dependency replaces fixed PATH_MAX grants at Restart/Session;
caller ends link the already-preserved actual native body. Plan literal rows
are integrated. No rereview or qualification is requested.


Final source-gate static disposition plan: execute's initial bank engagement is
not analyzer-proven after its non-const prepare_exec call. Source shows preparation
only spends the same engaged token, never resets/replaces it. Borrow that token
once through a checked local pointer before preparation. Preserve no-bank exec,
PID/receiver refusal, binding, original native exec/errno, paid4481 and retire(2).
No new emission, copy, fallback or source API. Recheck this TU then the mandatory
whole Debug/C++/Lua source hook; actual exec/foreign/fork cases already remain in
that unchanged suite.


The planned checked local-token borrow is implemented. Same-TU owned clang-tidy
passes (context/restart-successor-bank-static-finish.log); formatting/diff pass.
No native behavior, argument, identity, credit, return/errno or retirement change.
The unchanged actual-exec/foreign/fork cases remain in Root's full source gate.
