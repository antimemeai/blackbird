# ONE operation17 code and oracle attack

2026-10-11. Root reviewed the complete RawOperationTrace implementation and
field description, CodingEngine preflight/admission/private Boundary/result
preparation/dispatch/retention/settlement/callback paths, RetainedState dispatch
and variable rejection topology, native clock acquisition, and the whole native
fixture and literal byte oracles. This is this owner's one code/oracle attack.
No additional panel or review of this report is required.

The whole current native fixture passes autonomously in
context/operation-owner-baseline/native-complete-layer1.log. Genuine published
source failures preceded implementation. Its real rename, acquired result
allocation, partial receipt write, combined/split rejection groups, native errors,
clock fields, full DATA cancellation/deadline, paid post-effect settlement,
ordinary compact maintenance, later command exit and process-exit prefix cases
have concrete byte/status assertions. Two earlier fixture setup defects were
corrected without changing the acquisition/receipt cut or byte oracle: producer
registration must finish before recorder pause, and a paused recorder must
resume before its graceful signal-driven retirement. The real archive-scope
use-after-free exposed during maintenance was fixed in its semantic owner.

## OP1: dispatch return is missing on a real thrown exception

Coding publishes2420 then calls Root.dispatch, but2421 is published only when a
Result was returned. Root.dispatch copies the real DecisionEvent checkpoint's
planned_invocations vector inside a catch(Error) block; bad_alloc escapes. Other
actual allocations in that method can escape too. The whole2417 terminal does
not replace the missing2421 boundary outcome. Consumers cannot treat the
dispatch request as having returned a Result when the actual call threw.

Capture actual thrown Error, bad_alloc, length_error, standard and nonstandard
exceptions at this call, using the already-paid51-byte maximum. Preserve the
original throw. Publish returned Results as branch1 with Error presence and
actual Error members only on returned failure; publish actual throws with their
native exception branches. This distinguishes a returned failure from a thrown
Error without adding a fictitious Result or a second status projection. Publish
2408 only for an actually acquired DispatchReport, as the current code already
does. Keep the3290-byte fixed bank; no extra per-call admission is needed.

A direct native allocation refusal at the actual checkpoint copy must establish
the current missing2421 before source repair. Select the real typed copy/dispatch
call stack rather than guessing an allocation size. Require actual caller
failure, no adapter call or target mutation, one2420/2421 pair with allocation
branch and no invented Error, no2406/2408, and the separate whole-operation
exception terminal. Existing actual outer returned-Error/native EIO assertions
must also distinguish returned failure from thrown Error.

## OP2: provider exception originals and later outcomes are under-asserted

provider_case checks2407 and counts selected1322 originals, but for Error,
bad_alloc, length_error and standard exceptions it does not compare the actual
normalized result packet with an independent literal. It does not require each
expected caller Error, acquired dispatch report, real adapter receipt, final
settlement/disposition or whole terminal. A packet with wrong bytes or a later
misclassified outcome can pass this case. The nonstandard caller value93 is
already checked independently and must remain unchanged.

Strengthen these same cases using literal BBM2 error packets and source-derived
caller expectations: original io/EIO, normalized allocation/0, or
external_unknown/0 for length/standard; their real adapter receipt contains that
business Error. The raw2407 event still distinguishes the actual thrown type and
emits Error members only for the original Error. Compare the acquired report and
actual terminal/settlement. For the real post-encoder conversion refusal,
require successful adapter receipt/report despite later allocation failure and
preserve the one exact already-acquired packet; for oversized encoding, require
actual encoder refusal and no fabricated encoded primary. Do not use the
production encoder to manufacture expected bytes.

## Finding-driven completion

Both findings remain open at report creation. Fix them and directly recheck the
same native oracles, then finish affected supported-profile/static checks and the
required source gate. There is no additional assurance layer or nls.2 census
closure implied by this unit. Broader semantic/storage/remaining owners and real
workload costs remain explicit work.

## Direct finding recheck, 2026-10-13

OP1 and OP2 are fixed. The matching pre-fix archive fixture compiles and fails on
missing2421 after the actual checkpoint-copy allocation refusal reaches the caller
without an adapter effect. The current strict candidate publishes that allocation
outcome without invented Error fields and preserves the throw. Returned failure
is independently checked as returned branch1 plus its actual io/EIO Error.

The full current native fixture passes in
context/operation-owner-op1-op2-native-recheck.log. Provider cases now compare
independent literal BBM2 normal/error bytes, exact native caller errors, real
adapter receipt and acquired dispatch report, accepted terminal settlement and
unknown/success dispositions. Conversion/encoder/nonstandard failures assert the
absence of an unacquired settlement or disposition. Supported profiles and the
required source gate remain pending; no second code-review layer is added.


Root completed supported-profile integration and the installed source hook in
source ca6747b:110/110 Debug cases in146.29s, all161 owned C++ translation units
and owned Lua checks. The existing owner/finding oracles pass affected Mac/Linux
Release and ASan-UBSan profiles. The required hook's seven obsolete integration
fixture assumptions are corrected and their direct/profile checks pass; native
byte, failure, custody and publication assertions remain strict. Full disposition
and remaining scope are in [the completion plan](../docs/INSTRUMENTATION_COMPLETION.md).
nls.2 remains open; this completes the present owner segment.
