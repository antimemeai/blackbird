# Decision / Git / narrow caller plan review

2026-10-13. ONE independent whole-plan review of
`papers/2026-10-13-tool-caller-raw-subplans.md`, before implementation. Read the
complete current `src/decision_models.cpp`, `src/observations.cpp`, both adapter
headers, `tests/decision_models_test.cpp`, `tests/observations_test.cpp`, actual
Coding caller sites, Child start/send/collect/read/destruction and RawChildTrace
capture/admission. Revisited the provider vertical and existing old Unix emission
and transport studies, current doctrine and bounded-hardening instruction. No
build, native execution, provider call, dependency adoption or source edit in
this review. Root's separate published-source decision red is not my execution.

The explicit owner context, private transport exclusion and immediate native
read preservation fit the existing implementation. The corrections below must
be incorporated before source. They belong to this plan review, not a later
review panel.

## T1 — extended outcomes exceed the shared terminal bank

Plan lines12–14 reserve an exact request plus a maximum52-byte terminal. That
fits E16 + requestRef24 + branch1 + stage1 + ErrorPresent1 + Error9. But3605
adds an acquired status:int (maximum56 on current profiles) and3609 adds the
actual Result-success:bool (maximum53). A helper owning only52 bytes cannot
publish these outcomes through its held token when the ordinary CTRL lane is
full or cancellation is sticky. A fresh claim after parse does not repair this
failure boundary.

Specify an explicit terminal-extension maximum in helper construction, and
reserve52+sizeof(int) for HTTP and52+sizeof(bool) for JSON parsing before the
actual parse. Describe presence/omission of the acquired status and Result flag
precisely, including thrown parse exceptions where no Result was returned.
Do not replace native int/bool widths with unconditional ABI assumptions.

Direct oracle: admit the actual parse owner, exhaust ordinary CTRL headroom,
then allow its real parse to return an acquired result. Require its exact
extended terminal bytes through held credit and no fresh post-acquisition claim.
Exercise non2xx status too: actual status acquisition precedes range rejection.

## T2 — rejected Git path copies have no bounded body contract

`src/observations.cpp:139–141` copies the `const std::string&` returned by
`string_field` into `const auto path`, then checks empty/4096/NUL. Plan3702
prepays48+4096 before this copy and says to publish the acquired original body
before later validation discards it. A4097-byte path is therefore an actual
successful copy whose48+4097 body does not fit. A much larger rejected input
has the same problem; this is not bounded by the business limit until after the
copy. Simply emitting only accepted paths silently drops the rejection body.

Choose and state the actual source change: borrow the existing input string,
validate its native size/empty/NUL before making the business copy, then prepay
and acquire the accepted copy; rejected inputs have the already-retained
Operation17 original and the stage2 caller error, with no invented3702 body.
Alternatively pay the actual pre-copy input size with checked framing and emit
every acquired copy. Do not claim the4096 bound protects the current ordering.
The first option avoids copying an arbitrarily large invalid path and preserves
valid command bytes. This is a deliberate business validation ordering change.

Direct oracle:4096-byte accepted-size input,4097-byte rejected path, embedded
NUL and wrong native type. Assert the selected policy's exact reached3702
events, stage2 error, and zero child starts on refusal. Place a one-byte-short
claim cut before the valid copy rather than only before child spawn.

## T3 — argv admission must precede the actual formatted argument acquisition

The current run lambda receives an already-constructed vector
(`src/observations.cpp:145`); three callers build initializer-list arguments
at161,172 and183–186. Moving instrumentation into that lambda starts after all
argument copies and the history `std::to_string(count)` acquisition. The plan
correctly proposes per-element construction, but its rule "hold56+actual source
size" is insufficient for the history count: the actual formatted source size
does not exist until formatting has returned.

Specify the vector-building helper outside the old by-value call. Reserve vector
storage before building elements; each pre-existing literal/path/head view pays
its exact source size before its business string acquisition. The actual history
count is0..64 and reaches this path only when positive, so prepay its existing
formatter's at-most-two-byte decimal result before `std::to_string(count)`;
publish the actual returned original bytes before emplacing/moving them into
the argv vector. Do not format it a second time for metrics. Distinguish that
acquired temporary from other elements copied directly into the business vector.
An allocation failure copying a later argv element must leave all previously
acquired elements captured and must not have spawned a child.

Direct oracle: literal argv for repository, HEAD and history64, and an allocation
cut at actual history formatting / later vector-element acquisition. Compare
each native argument separately, including ordering, rather than a joined shell
command or an expected vector built by the production formatter.

## T4 — HEAD predicate is conditional and repeated in the current business code

`src/observations.cpp:178–181` can invoke `oid(head)` three times after a zero
exit; it invokes it zero times when exit is nonzero because every expression
short-circuits. Plan3707 describes one "actual existing oid predicate:bool"
alongside exit, without defining which evaluation it represents or what is
emitted when the predicate was not evaluated. An unconditional predicate for
instrumentation would invent an acquisition; an unconditional false bool would
invent an observed result.

Choose either one actual business `head_valid` evaluation under `code==0`, reused
by the existing sample/history decisions, or wrap every actual reached predicate
evaluation. For a nonzero exit omit3707 or add an explicit predicate-present
field and omit its value; document the changed exact width (the current45 is
E16+R24+int4+bool1). A business cached boolean is sufficient here; no metric-only
predicate walk is permitted.

Direct oracle: valid HEAD/history0 and64, empty repository with failed HEAD,
invalid repository with failed first child. Assert reached predicate count and
absence of an acquired boolean for nonzero exit, plus unchanged semantic sample
statuses and unchanged child count (1/2/3 respectively where applicable).

## Grounding and scope corrections

Root independently reports no distinct Jev/Git integration study matching the
plan's claims. Replace those claims with the actual read sources:
`papers/2026-10-11-provider-raw-vertical-subplan.md`,
`papers/2026-10-10-instrumentation-raw-emission-study.md`,
`papers/2026-10-10-raw-transport-design-study.md`, existing adapter fixtures and
the actual Child source. `observations_test.cpp` exercises samples/clock/doctrine
and readers; it does not run the Git observation path. The new real temporary
Git owner oracle therefore carries that missing direct evidence.

Git has no caller observer callback: distinguish its reachable native failures,
allocation and semantic audit failures from Decision's actual observer cuts.
Use forwarding native fault cuts for malformed/failed reads where needed, not
a newly introduced production injection API. The published decision red at
`context/remaining-tool-callers/baseline.log` is root-owned evidence of a
successful actual Coding decision effect whose raw fixture stdout was absent.

Remaining source-conformance points: retain Decision's existing deadline clock
and remove only the result's derived elapsed_us calculation/member; keep token,
curl config, stdin and private environment unselected; call raw_capture only
with the selected actual caller Ref; emit3603/3706 only after actual collect
returns an acquired exit; Child must destruct before caller terminal, including
observer/allocation/nonstandard throws. A templated body runner or explicit
catch boundary must record distinct throw branches after child cleanup; a
destructor alone can only report unfinished, not classify an exception.

## Byte arithmetic accepted for current profiles

Both supported profiles use Ref24, E16, size_t8, int4 and bool1. Request3600=56;
plain request3700/3604/3610=40; body3602/3702=48+N;
3603/3706=68; observer3606 is51 without a child Ref and75 with it;
parse3608=48; invocation3703=49; argv3705=56+N. Request versus DATA/CTRL
partition and exact ABI widths still belong in the immutable field description.
No assertion of complete nls.2 coverage or workload qualification follows from
this one plan review.
