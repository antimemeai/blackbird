# Session state owner's one plan review

2026-10-13. Review of `2026-10-13-session-state-owner-raw-subplan.md`, before
production implementation. Scope is settings, identity, snapshot metadata,
read-only discovery and confined Main origin wiring; restart/input remain outside
this unit. Read the complete plan and live session.hpp/cpp, AuditLog publication
overloads, raw_file.hpp/cpp, existing session_store_test, Main callers, and current
workspace/repository doctrine and bounded-hardening instructions. No build,
production edit, new literature campaign or additional review layer was performed.

The unit is cohesive and the genuine old-source native red reported by its owner
fits its claim. The following corrections belong in this same plan before coding.

## SS1 — ordinary business recovery must not swallow instrumentation refusal

`src/session.cpp:63–69` catches every `Error` from the metadata read and then writes
replacement metadata. `src/session.cpp:100–127` catches everything from snapshot
discovery and publishes `damaged`. The plan's lines49–54 explicitly introduce the
selected raw file helper into the former block; lines64–66 introduce paid leaf
acquisition into the latter. The raw helper claims and its `reading.check()` can
throw instrumentation errors independently of a business read failure
(`src/raw_file.cpp:753–814`). Keeping the existing catch unchanged would turn a
new source-grant refusal into a replacement write, or classify it as a damaged
session and proceed with more work. In particular, a capacity Error from a
metrics claim is not interchangeable with a capacity Error from the existing
bounded business read.

Specify a concrete separation of the two failure origins. Keep instrumentation
admission/publication errors outside business recovery, or mark and rethrow those
actual exceptions through the catch. A blanket ErrorCode test is insufficient
when the same ErrorCode is a legitimate business-read outcome. Preserve the old
replacement/damaged behavior for ordinary missing, unreadable, malformed and
oversized business metadata. Do not add a process-wide failure flag as a substitute
for identifying the actual failed selected operation.

Direct oracles: exhaust the actual raw source grant immediately before a selected
metadata read and immediately before a parsed-field capture. Require an actual
instrumentation failure, no replacement write, no accepted discovery completion,
and the retained earlier sources. Separately require an ordinary missing/read
failure to produce replacement or `damaged` exactly as before. Those two executions
must not share a synthetic injected Error that cannot distinguish their origins.

## SS2 — two declared source bounds are smaller than their emitted bodies

The kind-bearing 4212 leaf is declared `49+N` at plan line26, but snapshot kind6
prepays `48+512KiB` at line49. Pay `49+512KiB`; a source of the exact declared
maximum otherwise lacks one byte of envelope/field credit. This is distinct from
4213's legitimate `48+32` locator body, which has no kind byte.

The business quote formatter replaces each apostrophe with the five-byte string
`'"'"'` (`src/session.cpp:136–139`). The plan's `4*stable.length+2` at line60 is not
an upper bound. Use checked `5*stable.length+2` before acquiring the actual quoted
string, plus the declared 49-byte kind-bearing leaf overhead. The actual string
is copied unchanged; no second quote formatter belongs in the writer.

Direct oracle: a real session path with repeated apostrophes must retain the
literal existing quote and resume-command bytes and still list successfully.
Compute the expected output independently from fixture literals. A one-byte-short
grant must fail before acquisition, rather than leave an acquired string that
cannot be emitted. The snapshot arithmetic should be checked against the declared
layout independently even though current validated settings produce a smaller
packet than 512KiB.

## SS3 — selected filesystem integration must preserve the actual APIs

Discovery uses `std::filesystem::weakly_canonical` at
`src/session.cpp:74,134`. The selected helper named in plan line63,
`raw_file_canonical`, calls the owned **canonical** implementation
(`src/raw_file.cpp:980–992`), and its unselected path calls
`std::filesystem::canonical`. These are different operations. Weak canonicalization
retains a normalized nonexistent suffix after canonicalizing its existing prefix;
canonicalization can report missing-path failure and select the old fallback
branch instead. Do not replace one with the other merely to obtain raw coverage.
Either give this unit a confined typed wrapper around the actual weakly_canonical
call and its exact error_code, or explicitly extend the file owner's helper with
a distinct method and matching native semantics. That extension must be declared,
not silently authorized as unrelated file work.

Likewise `src/session.cpp:101` currently calls throwing `exists(path)`, while
`raw_file_exists` takes an error_code and returns a bool. Preserve its original
throw/catch behavior if selecting that helper: the native error must produce the
same business exception rather than silently become `missing`. Candidate checks
at lines80–84 are already error_code overloads and must retain their exact OR
short circuit, including the second check after a false first result with an
error. Do not add a new `if (e) return` between those existing checks.

Direct oracles: discover through an existing symlinked prefix plus a nonexistent
suffix and compare the literal expected weakly-canonical root; separately exercise
the throwing snapshot-exists error path and both candidate OR branches. Assert
which native operations were reached and which were not, rather than only the
final row count.

## SS4 — define the actual stat variants instead of a shared unused-field body

Plan lines68–73 say successful stat and lstat emit both mode and modification time
and that both are fields the business uses. The source uses only `info.st_mode`
after lstat (`src/session.cpp:103–105`), and only `metadata.st_mtime` after stat
(`src/session.cpp:130–132`). Define method-specific successful bodies: request R,
native rc, then mode_t for lstat or time_t for stat. A negative rc carries only the
actual saved errno; no failed struct fields. The corresponding maximum return
credits are `max(48,44+sizeof(mode_t))` and
`max(48,44+sizeof(time_t))`, including E16/R24/int4. Pay the 41-byte request as
well. Use native widths for the selected platform, not a presumed Linux mode_t.

The filesystem/iterator declarations should also fix exact returned variants:
error_code value first, iterator-end bool only when that comparison was actually
reached. An iterator constructor/increment error must not manufacture an end
predicate or claim an entry was acquired. The current loop at lines88–92 supplies
the actual short circuits. Their maximum57 bank is conservatively sufficient for
the declared bool/int fields; this review does not claim a quota defect there.

Direct oracle: literal native lstat mode and stat mtime from separate forwarding
syscall observations; absent unused and failed fields; a real failed iterator
construction/increment with no fabricated end bool or entry leaf.

## Required placement details, without additional mechanisms

The plan's general immediate-capture rule must be applied literally when spelling
out the implementation. Separate the existing absolute-path acquisition from its
next lexical normalization (`src/session.cpp:72`); separate resume-command and
final root-string acquisition from their following Value construction
(`src/session.cpp:140–150`). Capture completed results before that following
allocation. Named restored/decoded fields are borrowed before their copies at
lines110–114 and185–188, not merely before the later display copies. Inputs rejected
by validation can exceed all valid-settings bounds, so their borrowed actual
lengths determine their input credits.

4214 should emit the acquired complete native identity triple immediately before
the later hex/Value/publication work at lines174–179, not only at successful
function return. Lower retained ownership still captures any individually issued
IDs if acquisition of the triple itself fails. The 4213 locator is correctly
textual: `src/context.cpp:58–62` creates its existing hex string **before** the
underlying record publication, then returns it. Do not describe this source as
an existing postappend string conversion. A postappend return-failure fixture must
cut a genuinely reached allocation/return boundary; it must not fabricate a
conversion that the current implementation performs before append.

4225 must be emitted only after the final returned Value has been successfully
constructed, and its UI out-ref supplied only after that actual publication.
Prepayment alone does not mean completion. Explicitly pass copied immutable
startup/dispatch refs at the live Main calls; read-only `--sessions` occurs before
runtime creation (`src/main.cpp:168–170`) and remains opted out. No metrics can
refer to a future Main input scope or dereference a mutable RawRegion across a
worker handoff.

These are the sole plan findings for this unit. Resolve them in the same plan,
then implement and run the declared direct native/error oracles. This report is
not implementation assurance, a performance qualification, or a request for a
second review of the plan.
