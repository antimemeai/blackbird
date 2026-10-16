# Native command center adapter

2026-10-16. Implements the accepted [plan](PLAN.md) through the existing
CollectionInspector. The old-main Ctrl-X behavioral RED belongs to the display
owner; this unit does not substitute a missing-symbol failure for that episode.
Root owns integration, final source gate, remote qualification and publication.

CommandCenter performs one bounded query per owner in refresh, outside input and
painting. Task publication supplies the successful origin/revision/ingress stamp
under the existing UI mutex. Participant and command readers try-lock their real
owners. No new task store, command-task relationship, execution callback, journal
scan or producer-side transformation is added. Collections have at most eight
rows of each kind, in task/participant/command order, plus independent selected
detail from the selected owner. Task copies remain bounded by8960 text bytes;
participant and command result pages remain4096 bytes. Catalog continuation is
per kind and detail byte paging is separate. The three cuts are independent.

The adapter keeps each last-good value paired with its actual query. A busy or
unavailable owner does not prevent other cuts from advancing. Current statuses
are separate from accepted content. Native task revision conflict resets only
that catalog on the next bounded poll; selected identity remains independent.
Typed targets carry actual environment/conversation scope. Participant targets
use the actual cached endpoint incarnation. Retirement or scope mismatch retires
that center binding permanently: old rows disappear from the live collection,
old selected body remains explicitly a prior owned observation, and a replacement
reader with the same ID cannot attach it to another owner. Current endpoint
status and this sticky binding state are distinct fields in the raw snapshot.

Compact T/P/J labels expose phase before long IDs can consume the collection
width. Collection title reports loaded tasks/actual task total, loaded participant
and command rows with '+' when continuation exists, '?' without an accepted cut,
and retired participant scope separately. Full identities remain in keys and
detail. Task status is explicitly declared; participant task binding can be
'unbound'; command detail reports actual native exit and retained bytes. Selected
participant detail states stop-requested, pending-capture count and raw/retained
result-source presence; command detail states source-extent completeness, capture
completion and stop-requested. Missing references are explicitly unavailable. These
properties imply neither scheduler assignment nor a simultaneous union snapshot.

Refresh stages a finite65536-byte native wire and a scene, then try-prepays exact
DATA and CONTROL before emitting or publishing.4088 contains the actual finite
literal observation;4089 links its actual record and current triggering action
origin to the host publication result. CONTROL reserves exactly the record's two
references, three u64 fields, two booleans, ErrorCode and i64 detail. Physical
admission refusal preserves the exact accepted scene/applied reference and leaves
an explicit recording-capacity notice. Allocation, length or ordinary projection
refusal preserves the accepted scene; a real emission failure marks capture
failure and propagates. Unchanged source/query wire skips republishing. Action
and page handlers only update local controls/query/origin; render calls only the
shared component. View incarnations use a saturating checked CAS.

## Direct native cases and results

`command_center_test` has three modes:

* semantic: actual TaskState/UI publications; ten tasks show eight-row coverage,
  independent selected detail, continuation and revision reset. Nine actual
  participant workers expose waiting/unbound result detail and more-row coverage;
  retiring the real owner clears live rows while preserving old target/body, and
  a new same-ID reader cannot reattach. A real child writes COMMAND_LITERAL and
  exits7; inspection shows full identity and that actual exit. Rendering leaves
  the accepted snapshot unchanged. Root separately owns the real UI-publication
  mutex-contention oracle; this fixture does not duplicate that seam.
* raw: a real RawRuntime captures TaskState snapshot1708, successful UI ingress4050,
  actual Composer Home action4010, literal native4088 and actual4089 adoption.
  An independent decoder consumes the complete finite payload, including empty
  actual CommandJobs and unavailable participant reader; it verifies the literal
  task title/blocker, revision, exact source/publication links and current action
  origin. The final applied reference must identify the actual successful4089.
  An unchanged refresh preserves its scene and applied identity.
* pressure: real TaskState/UI source advance precedes physical DATA and CONTROL
  exhaustion in separate borrowed-producer cases. Both refusals preserve exact
  prior scene/applied identity without capture failure; releasing holds permits
  explicit adoption of the actual newer source. A fresh producer also leaves
  exactly the native4089 CONTROL bytes available and must successfully adopt,
  detecting the earlier24-byte overclaim directly.

The first runs exposed fixture setup limits rather than relaxed product rules:
Participants defaults to concurrency2, TaskState prepare requires the full16MiB
packet grant, and Darwin limits shared-memory names. The fixture now explicitly
configures concurrency16, admits that existing packet bound in its32MiB DATA
channel, and uses short private channel names. All failed attempts remain under
context/tui-command-center-2026-10-16/center-*.log.

Actual Debug build and CTest commands:

```
cmake --build build/debug --target command_center_test -j2
ctest --test-dir build/debug -R '^command_center_(semantic|raw|pressure)$' --output-on-failure
clang-tidy -p build/debug src/command_center.cpp
clang-tidy -p build/debug tests/command_center_test.cpp
```

All three native CTests passed in0.18s in center-green-final.log. Owning source
clang-tidy passed. Test clang-tidy identified three unchecked optional target
accesses in the assertions; the fixture now explicitly rejects absent targets
before accessing their incarnation. Final owning test clang-tidy passed with no diagnostics in
center-test-tidy-final.log. Root owns the final relink/three-case rerun after the
checked-accessor and retirement wording corrections. Root's one independent source
challenge found the
CONTROL overclaim, retirement/coverage wording and scope behavior, current-action
origin and incarnation overflow edges; those findings were corrected in this same
layer. The final selected-detail pass additionally exposed omitted stop/capture/
source-presence wording; finite detail lines and direct semantic assertions now
include those existing native facts. Root owns final integrated disposition. No extra review
panel, full gate, sanitizer claim or commit belongs to this owner report.
