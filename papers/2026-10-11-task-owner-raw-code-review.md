# Task owner raw emission: one code and oracle attack

2026-10-11. Reviewed raw_tasks.hpp/cpp, tasks.hpp/cpp, tasks_raw_test.cpp and the
resolved task subplan/plan attack against the candidate qualified by the actual
native fixture (parent reported0.77s; context/tasks-raw-first-green.log). Read the
real claim/split/emit paths and AuditLog::record's void overload for the selected
post-append boundary. This is the ONE code/oracle attack; root owns fixes and
finding-directed rechecks. No production or owning test files were edited here.
Task activity binding row32 remains separate.

## 1. Prepared's by-value move can allocate after accepted append

TaskStore::edit calls the void log_.record overload and records successful1705,
then calls state_.commit(std::move(prepared), child). TaskState::commit still
accepts Prepared by value. Prepared contains std::deque<Value> receipts. On the
supported linux-clang18/libstdc++13 profile, deque's move constructor initializes
an empty replacement map before stealing the original storage. Constructing the
commit argument can therefore throw std::bad_alloc after the retained delta was
accepted but before the commit body, its1700 request, and state revision transfer.
The task state and callback remain uncommitted despite accepted publication.
Its prepaid control bank cannot eliminate this native allocation.

This follows the selected profile in CMakeLists.txt and the primary
[GCC13 deque implementation](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-13.3.0/libstdc%2B%2B-v3/include/bits/stl_deque.h):
_Deque_base's move constructor calls _M_initialize_map(0), which allocates its map
and nodes. A scratch native probe using the actual TaskState::Prepared declaration
and installed libstdc++15 observed `refused=1 entered=0 size=64` for the same
by-value std::move call shape. That local probe demonstrates the mechanism; it
is not a substitute for the supported Linux task edit cut. Its source/binary are
/tmp/blackbird-task-prepared-move-probe.cpp and corresponding executable.

The existing oracle only rejects an allocation when formatting_identity finds
hex_identity in the native backtrace. That genuine baseline red identifies the
removed40-byte formatting allocation, but the candidate's green does not test
other allocations in the accepted-cursor/old-task-revision interval. Replace
that restriction with refusal of every allocation in the actual selected
post-acceptance interval, keeping the genuine baseline log. Obtain the direct
libstdc++13 native red, then remove the extra Prepared construction at this
boundary (an rvalue-reference consuming signature is the simple fit). Recheck
actual accepted cursor, task revision, callback and original raw outcomes. Replay
uses the same commit API; preserve its preallocated transfer too.

## 2. Native owner-return bodies and state-publication links lack direct oracles

raw_surfaces checks the literal original request, publication identity/return,
callback Error, scalar state-publication fields and request/terminal counts.
However, it does not require or compare1702 prepared,1707 read-return or1708
snapshot-return records. Omitting these entire selected metric sites can leave
its business-result checks, terminal pairing and final branch census green.

For1706 it counts three records and compares the same resulting fields, but does
not resolve each return to its actual commit/restore/replay-commit request. Three
copies attributed to the live edit commit could satisfy that part while restore
and replay originals are missing. The recorded action tags for replay preparation
and replay commit are counted without proving their actual replay parent linkage.

Strengthen the existing source-to-disk oracle rather than adding another test
layer. For successful prepare, require its unique linked1702 and compare the
independent native revision/next/change/remove/order/receipt/population fields.
For actual read requests require their unique1707: the successful page includes
original u64 offset and size_t cursor/rows, while the revision-conflict branch
omits those fields. For each actual snapshot require its unique1708 and compare
native row/order/receipt sizes. Resolve every1706 through its full native request
R: live commit, successful restore and replay commit each have their own exact
state publication. Require replay preparation/commit parent R to be the actual
replay request and no fresh edit publication/callback for those reconstruction
requests. Malformed restore/replay must have their original error terminal and
no successful state publication. Existing failed branches should not acquire
fabricated prepared/read/snapshot success bodies.

## Boundaries and budgets checked

All fixed body widths match the selected64-bit native fields:1700 is26 without
parent/50 with parent;1702 max129;1706 is120;1707 max121;1708 is80;1704 is80;
1705/1710 max50;1709 is40;1711 max51. Owner whole banks206/198/157/197/518 and
replay298 cover those finite records; an explicit parent adds24. Edit holds its
221-byte commit child before append. Replay holds its final221-byte child before
reconstruction and admits its parented preparation before existing encoding.
Split moves native capacity without waiting or allocating. The commit-body
field publications and callback pair consume held credit after acceptance.

Prepare reserves the real codec bound ValueLimits{}.bytes+48 before its one
existing business encoder, copies the returned string once and releases unused
DATA capacity before staging/nested work. Over65536 business originals are copied
before refusal; codec failure has no successful string. The small-lane native
refusal checks original held terminal fields and released capacity. No metrics
encoder, task traversal, Number parser, formatted identity, clock, aggregate or
subscriber was introduced. Maintained product counts and actual native locals
are copied; existing read/snapshot/restore/replay product work remains business
work. Native fields are emitted individually without struct padding.

The actual pwrite refusal is scoped to the selected retained file's device/inode
and an observed1704 request. The post-sync cut forwards successful real sync,
writes sticky cancellation, and fills remaining CONTROL after actual reserve;
held capacity is excluded from that filler. Candidate still commits/calls changed
and publishes native outcomes without fresh post-acceptance admission. Those
cuts are distinct from the missing native allocation cut above. callback Error
is captured from held credit after state acceptance and preserves its intentional
swallow behavior; callback work does not borrow outer outcomes. Malformed restore
stages before replacement, and replay retains its actual reconstruction actions.
No additional production defect is asserted beyond the by-value move allocation.

Root finding resolution: supported linux-clang18/libstdc++13 native red in
context/linux/run-4wwx647k/checks.log refuses the actual64-byte allocation after
the task append, leaving revision0/callback0. The broadened cut pre-issues the
actual ID reservoir; an earlier attempted broadened cut instead caught its
reservation append and was replaced. Task commit now consumes Prepared&&.
The same fixture rejects every allocation in the accepted-task/revision0
interval. Native Mac affected recheck passes0.65s, revision1/callback1, no
allocation. The source-to-disk oracle now compares exact1702/1707/1708 native
bodies, each1706's full actual owner R, live edit and replay parents, unique
returns ordered between request and terminal, and absence of successful bodies
on failed owners. Supported Linux/profile and latest narrow static results are
pending; no second panel.


## Selected profile qualification

All10 selected owner cases pass Mac release9.22s and ASan/UBSan39.30s.
Linux debug7/7 and release7/7 pass. Linux ASan exposed a fixture allocation-hook
defect: global new delegates to malloc but libstdc++ selects sized delete.
Complete scalar/array sized-delete overloads forward to the same actual free;
no sanitizer is disabled. Mac direct tasks passes0.93s, narrow static analysis
is clean, and the affected tasks+command ASan pair passes9.92s. Linux ASan
finding recheck passes2/2 in10.63s (`run-eqymfxgq`), joining the other five
previously passing owner cases. These are affected-profile checks; full
installed source gate is still pending. No second review is added.
