# Plain output: native source and writer ownership

The P1/P2 plan is implemented by `raw_plain.hpp/cpp` and Root's confined Main
integration. Main supplies existing literals and borrowed business strings as
native pieces. No joined greeting/status packet is built for metrics. The writer
preserves the complete actual proposed bytes4340 using transport scatter-copy,
then records each native write request4342 and actual return4343. Piece boundaries
remain business write boundaries. Positive returns advance a global accepted
byte offset; EINTR preserves the same remaining piece; native failure or zero
return produces one owned Error terminal4341. No writev, polling, formatting,
new producer, metric transformation or timer was introduced.

The source owner supplies literal fields only. Shared raw publication,
RawEmission adoption and exception dispatch own transport/bank/end policy.
Exact4340 DATA grant is53+sum(actual view sizes), and adopted terminal CONTROL52
is held before effects. Native write request plus maximal native failure return
costs113 CONTROL bytes, acquired before each actual write. Explicit preheld loans
split these same finite amounts; missing bytes refuse before request/effect.
Absent explicit context opts out even with a bound ambient runtime. Present
invalid parent marks capture failure, fences the actual producer, preserves entry
errno and throws before acquisition or write.

The existing business safe formatter is invoked exactly once after raw input4344
has been preserved. RawSessionCall4345/4346 owns that acquisition. The source-derived
rendered-output DATA bound is53+4*inputN+prefixN+suffixN, with terminal52 prepaid
before formatting. Main's same safe routine is the business transformation; the
writer has no transforms. Before-runtime/read-only/bootstrap output remains
outside this selected recorder scope; audit-wide bootstrap is separately tracked.

The dedicated real-pipe fixture has eight finite cases: actual forwarded short
positive writes, actual EPIPE after closing the only read end, allocator failure
inside the existing formatter after4344, successful counted binary-input rendering,
invalid supplied parent, insufficient supplied DATA, insufficient supplied CONTROL,
and explicit opt-out under an active ambient binding. Literal oracles compare
physical accepted bytes with4340 and every native request pointer/range and return;
PC1 adds exact actual parent/kind, complete4341 and4345/4346 layouts, optional Error
fields and exactly one terminal per emitted request. Its EPIPE pipe is created
**after** the recorder fork, so a recorder-inherited read end cannot invalidate
the failure oracle. The short proxy forwards two bytes to the real syscall;
this validates owner retry/range accounting for real native positive results,
not kernel short output from an unchanged larger syscall argument. EINTR/zero
native return and post-terminal recorder failure are not independently exercised
by these eight cases; no broader qualification follows.

The final source-matched isolated fixture passes all eight in
`context/station-owner/plain-diagnostic-native.log`. Initial matching compile is
`plain-first-build.log`, final registered fixture recheck is recorded separately.
Root's integrated Main real CLI/input/dispatch case passed in
`context/restart-recovery-owner/finite-second-native.log` (`main_input_native`,
3.59 seconds); the other case in that log is not claimed green here. ONE whole
Plain code/oracle review belongs to the Program colleague, with PC1 repaired in
this same unit. No second panel or duplicate CLI campaign was added.

Final registered matching link and CTest passed after Root source formatting:
`context/station-owner/plain-final-matching-build.log` and `plain-final-native.log`
(1.12 seconds). The ONE code/oracle report is
`papers/2026-10-14-plain-output-owner-raw-code-attack.md`; PC1 is resolved in the same
eight cases. All owned source/docs and compiler/static workers are idle.
