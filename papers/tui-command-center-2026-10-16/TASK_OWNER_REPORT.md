# Pure task observation result

Implemented the accepted native contract in task_observation.hpp and
TaskState::observe. The observer uses existing task source order/populations/
rollups, transparent selected lookup and bounded nine-field extraction. It copies
at most eight catalog rows plus one selected detail and8960 actual text bytes.
The result carries no Value, borrowed row/text pointer or raw capture operation.
TaskState stays sequential; root's existing UI projection/mutex and publication
stamp remain the integration owner. No extra TaskStore/reducer/callback is added.

Canonical IDs are native positive uint64 ordinals, formatted into a21-byte stack
buffer for lookup. Rows preserve declared status, parent, item guard/timestamp,
full title/owner and actual child rollup. Detail adds full note/blocker. Existing
numeric representations are read through storage without allocating text; checked
Decimal work is limited to three limbs and16 exponent steps. Double formatting
uses the same stack to_chars spelling and uint64 parse as the old accepted text
semantics. Noncanonical extra-key rows admitted by existing restore explicitly
return Unavailable without changing that owner's contract or scanning the extras.

Cursors carry exact task revision and order offset. Old revisions conflict before
projecting a page. Selected absence is NotResident, including archive, and does
not claim that retained task history is absent. Zero-row queries permit detail-
only projection. Actual examined_rows and copied_text_bytes describe projection/
copy coverage; tree navigation and fixed-field validation have separate finite
bounds. No history or linear position scan, source re-encoding, ordinary raw
claim, user callback or action enters observe.

## Direct RED and GREEN

Before implementing the method, an ignored old-read adapter prebuilt its Value
query, then called actual TaskState::read against the existing Debug foundation
while all allocations were refused. Compilation passed; the behavioral oracle
exited1: `FAIL pure task observation attempts no allocation`. It did not fail for
a missing symbol. Exact source, compile command and foundation/source hashes are
in context/tui-command-center-2026-10-16/tasks/red-metadata.json; red-build.log and
red.log preserve the actual result.

The permanent task_observation_test target built with production Debug warnings.
All six exact CTests passed in1.18s:

* allocation: empty Ready and all maximum admitted text fields under allocation
  refusal, exact8960 copied bytes/nine projections and unchanged task owner state.
* semantic: actual snapshot/delta replay, declared parent status versus completed
  child rollup/top-level counts, immutable copied result and actual archive.
* ranges: source-order page and independent selected ID, continuation, detail-only,
  invalid query/identity, revision conflict and exact order frontier.
* numeric: maximum issued21-byte task identity, UINT64/INT64/Decimal/double accepted
  representations and extra-key admitted shape explicitly unavailable.
* bounded: actual2048-item state, final eight entries plus independent early ID,
  fixed coverage and actual reorder invalidating the old cursor.
* raw: actual borrowed native producer with DATA and CONTROL physically exhausted,
  Ready without allocation/fence, and actual same-producer ordinals showing no
  observer event between the two emitted markers.

Commands were `cmake -S . -B build/debug`,
`cmake --build build/debug --target task_observation_test -j 2`, then
`ctest --test-dir build/debug --output-on-failure -R '^task_observation_'`.
Configuration/build/check logs are in the same ignored task directory. Actual
owning translation analysis via `clang-tidy -p build/debug src/tasks.cpp
 tests/task_observation_test.cpp` passed0 with no diagnostics, preserved in
owning-tidy.log. No rule suppression, new hold seam, test-of-test framework,
CMake/index/Beads edit, sanitizer/full gate or unassigned build was performed by
this owner. Debug Ninja lease was returned after the targeted GREEN.

Root owns one independent source/oracle challenge, the terminal/raw integration
and remaining tranche qualification. This report does not claim those completed.
