# Plain output: one whole code and oracle attack

2026-10-14, job_blackbird_map. This is the single independent review of the
plain-output conceptual owner. Source scope: `raw_plain.hpp/cpp`, actual Main
emit/status/outcome/warning/greeting/prompt/newline integration, the separate
plain-output plan and P1/P2 dispositions, and the whole dedicated
`tests/plain_output_raw_test.cpp`. No product/test edits or compiler workers are
owned by this reviewer. The previously reviewed shared emission machinery is
used as an interface, not reopened as another review unit. Pre-runtime/help,
read-only and post-teardown diagnostic gaps remain outside this unit.

## PC1 — the fixture does not establish its own completion outcomes

In the initial eight-case fixture, `tests/plain_output_raw_test.cpp:190–247`
checks proposed bytes4340, native writes4342/4343, input4344 and an allocation
branch count4346. It never parses4341 or4345. It discards the parent of4340,
4344 and4346 and the output kind4340. Its4346 check stops after the branch;
native4342 also leaves its remaining bytes unchecked.

Consequently a missing, duplicate, wrongly linked or falsely returned4341 could
pass the successful short-write and real EPIPE cases. A malformed or wrong
formatter parent/request/terminal could pass the format success/allocation cases.
Real pipe bytes and native return checks are valuable, but they cannot establish
these distinct request/outcome records. This is an oracle defect; the reviewed
production does not exhibit the corresponding terminal defect.

The finite repair stays inside the existing eight cases: independently declare
the expected request kind/parent, request-to-original/output links, terminal
presence and count, complete branch/stage/Error variants, and require complete
body consumption. Existing cases have these literal expectations:

| Existing case | Expected plain/formatter completion |
| --- | --- |
| Short-piece success | One4340 and one returned4341 linked to it; all actual positive writes sum to the independently received proposal. |
| Closed reader, explicitly ignored SIGPIPE | One4340, actual negative4343/EPIPE, and one error4341 with owned `io/EPIPE`; no accepted-success terminal. |
| Formatter allocation cut | One4345, its original4344, one allocation4346; no4340/4341 or actual output write. |
| Formatter success | One4345 with actual descriptor/kind/input length, its4344 original, child4340, returned4341 and returned4346 with exact links/counts. |
| Invalid supplied reference | No4340/4341 or native output effect; actual invalid-identity refusal and explicit permitted fence. |
| Short preheld DATA or terminal CONTROL | No4340/4341 or actual write; actual capacity refusal. |
| Explicit opt-out with active ambient runtime | Physical business bytes unchanged, no4340–4379 records. |

This finding and exact finite correction were sent directly to the owner and
Root during this sole pass. No additional scenario campaign or production
fault injection is required for PC1.

The first direct run also exposed the fixture's expected short-loan error as
incorrect: an insufficient live `RawCapacityClaim::split` returns `capacity`
(`src/raw_channel.cpp:716–724`), not `invalid_range`. The owner corrected that
literal oracle to the actual interface before the same finite recheck. This
does not require a production behavior change.

## Source assessment

No concrete plain-output production defect was established in this pass.
`include/blackbird/raw_plain.hpp:23–42` borrows existing native business pieces
and forms fields on the stack. It does not create a joined string or introduce
writev. On the supported widths the one DATA proposal is53+N: envelope16,
parent reference24, descriptor4, kind1, size_t8 and exact counted bytes. Those
originals are byte spans, not native CString operands; no trailing NUL is added.

`src/raw_plain.cpp:21–50` enforces a supplied valid reference before effects,
holds the52-byte maximum terminal before proposal publication, and adopts the
actual DATA request into shared RawEmission. A failed preheld split is explicitly
marked before throwing. No request is fabricated when admission failed.

The native loop at53–105 preholds113 per attempt: linked request64 plus maximum
failure return49. It names the actual descriptor, the global accepted offset and
the actual current piece extent. Only a positive result advances the offset;
EINTR retries without advancement, negative returns preserve actual errno, and
zero becomes the honest owned `io/0` failure. Empty pieces do not invent writes.
Returned, short and negative native variants stay distinct. Publication failure
is checked before another native write, and the final failure check after shared
completion prevents a failed terminal publication from returning success.

The formatting path at `raw_plain.hpp:46–74` preserves4344 input before calling
the same caller-supplied business formatter, prepays53+4*N+prefix+suffix DATA and
the child52-byte terminal, and retains the enclosing formatter request/failure
outcome. Allocation and other thrown outcomes use the common classifier.
Main's existing `safe` formatter satisfies4*N, including its C1 control-byte
pair handling. No duration calculation or new metric formatter is introduced.

Main supplies these selected owners at its actual output sites:
warning223–226; emit466–472; status536–543; outcome544–553;
once newline1393–1397; greeting1468–1478; prompt1565–1569.
The dispatch selection at570–579 borrows the real command context and restores
the enclosing output context lexically. The TUI paths continue through their
existing UI owner instead of also passing through plain output. The reviewed
P2 authorization covers native write Error replacing old silent failbit loss;
production signal policy is unchanged.

## Direct evidence and limits

The dedicated fixture uses an actual pipe, independently received binary bytes,
preserved raw records and a native forwarding observer. Its allocation cut is
at an actual C++ construction inside the business callback. The EPIPE case
explicitly ignores SIGPIPE in the fixture, not production. Preheld DATA/control
refusal and explicit opt-out exercise actual owner interfaces.

Its original short-write observer forwards `min(2,n)` to the kernel. This is
useful deterministic retry evidence with actual two-byte native results, but
does not establish a kernel short result for the unchanged original requested
count. That distinction must be reported honestly. EINTR and zero are distinct
in source but not exercised by these original eight cases. Neither limit is a
reason to reopen previously completed shared transport/UI qualification or add
another review panel.

Root's already executed integrated Main evidence is
`context/restart-recovery-owner/finite-second-native.log`: the actual
`main_input_native` case passed3.59s, including real input/refusal/rearm and
actual `/new`/`/resume` behavior. The other RRC case in that particular two-case
run failed and was corrected independently afterward; this report does not call
the whole original run green. No duplicate CLI campaign is requested.

The final same eight dedicated native cases are green in
`context/station-owner/plain-diagnostic-native.log`, all cases0–7 ending with
the actual source-to-preserved-byte success result. The owner used an isolated
link against the current source-matched Foundation archive, with no production
change. Its final handoff states that complete4341/4345/4346/4342 fields, actual
request parents/kinds and exactly-once/no-before-admission assertions pass.
The registered fixture-only relink is queued with Root; this report does not
claim that queued integration step has already run.

The same direct checks exposed another fixture lifetime error: the recorder
fork inherited the pipe reader because the original test created that pipe
before recorder startup. Merely closing the test parent's reader did not then
produce actual EPIPE. The finite correction creates the pipe after recorder and
thread setup; the final direct case obtains real EPIPE and checks its literal
native and owned terminal outcome. No production behavior was changed to make
the check pass. The short-forwarding evidence limit above remains explicit.

This finishes the one code/oracle attack and its concrete PC1 handoff. Owner
finite repairs/direct evidence are recorded without a new review of those
repairs, a new scenario campaign or another panel. No production defect was
established. Root retains the normal source-hook and matching integration work.

Owner final registered integration: `plain-final-matching-build.log` passed against
Root's final formatted Foundation sources, then `plain-final-native.log` passed
all eight registered cases in1.12 seconds. Both logs are under
`context/station-owner/`. This completes the queued matching step above without
another review or extra scenario. All owner compiler/static workers released.
