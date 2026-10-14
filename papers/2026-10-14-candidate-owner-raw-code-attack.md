# Candidate and native child admission: one code/oracle attack

Root inspected the whole selected Candidate CLI/Manager/helpers against the
reviewed subplan, immutable site description, existing ordinary candidate_test,
whole candidate_raw_test and narrow RawCaptureFailureScope/RawChildTrace change.
This is the ONE code/oracle layer for this conceptual unit, no review of reviews,
new workload/consumer qualification, live provider or new dependency.

CA1: the actual argument helper excludes the existing terminating NUL while the
plan/immutable native schema promises a CString including it. argument() sets
size=bytes.size(), funds56+size and copies raw_string_field(bytes); both CLI3901
and Git/run3916 call it with std::string_view of complete existing C strings or
std::strings. Those views exclude their terminator. The native fixture's expected
argv uses the same excluding representation, so its green permits this mismatch.
Use the complete existing operand buffer, including its actual NUL, set extent
N+1 and pay56+extent before copying; never append a synthetic byte, join argv or
rerun a formatter. Tighten this SAME native parser/oracle to independently expect
each literal actual CString buffer, count/index/order and terminator. The genuine
old/current semantic argv/effect checks stay unchanged.

CA2: canonical's generic filesystem path grant is fixed PATH_MAX. The actual GNU
realpath(NULL)/C++ canonical path can exceed this in a deep cwd; the selected
File canonical owner already preserves its growing/native result before C++
construction. The plan promises that shared native boundary. Route canonical
through its existing File owner, preserve the original throwing API behavior
including not_found/error distinction, use the actual acquired path/source ref
for the caller terminal, and remove the guessed output extent. Do not add another
realpath implementation, extra traversal or another formatter/encoder merely
for Candidate. The current short-path fixture cannot expose this loss. A real
long cwd case on the applicable native profile must check acquired source before
later path-copy/refusal; retain honest pending status if Linux cannot be reached.

The scoped deadline correction itself fits the actual invariant: each local
marker scope surrounds one actual raw admission. Only its own marked ETIMEDOUT
is consumed inside the PREEXISTING deadline_error translation; earlier parent
error is untouched, unequal/ECANCELED/inactive markers are not consumed. Existing
native ordinary timeout remains outcome-unknown once; actual capture interruption
must retain the intent, join the real child and escape before replacement save.
Direct previous-owner/unequal/inactive and real timeout/sticky child checks attack
these distinct faults. No global marker reset or Candidate error-code inference.

Other inspected boundaries: shared RawEmission publication/banks, declared native
request/end variants, before-effect packet grants, borrowed fields at existing
uses, real filesystem Error variants, actual FD ownership before publication,
independent paid close even when acquired-event publication fails, partial real
write sourceR/offset/remaining, whole runR through intent/output/reacquire/observed
save/child cleanup, opt-out and private recorder bootstrap without pool chmod or
symlink following. Fix CA1/CA2 and recheck their direct affected paths in this
same layer; no second panel. Final integrated source hook remains required.
