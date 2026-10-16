# Bounded native command observation

`CommandJobs::try_observe` now returns a fixed owned typed projection. It validates
canonical32-byte lower-hex IDs and finite budgets before one try-lock. Contention
returns Busy; native mutex errors return Unavailable. Selected identity absent
from the resident registry is NotResident, including a retired projection; this
says nothing about retained history. Invalid query identity/range has its own
outcome. No Value construction, dynamic allocation, raw admission/emission,
recorder wait, domain action, acknowledgment or owner retention change occurs.

The catalog uses transparent `std::less<>` lookup and map continuation. It projects
at most eight catalog rows plus one independently selected detail row, so selecting
a detail never shrinks pagination. Actual lifecycle flags, custody, capture state,
known-exit fields, byte frontiers and native launch/terminal references remain
separate facts. `sealed` retains the existing exact conjunction of terminal
retention, complete capture and reaped native custody; a sealed nonzero exit is
complete failed work, not a successful result.

The selected page copies at most4096 retained bytes and16 actual source chunks.
Deque binary seek skips earlier cache chunks without a full scan. Sources carry
the actual native read reference, logical chunk beginning, within-original byte
offset and length. Zero raw references make provenance explicitly incomplete.
The page clamps a request older than the real resident tail, explicitly reports
that omitted prefix, and never includes received bytes beyond the owner's
retained frontier. Byte/chunk budget endings are distinct from the true retained
frontier. Fixed returned bytes and references need no cross-owner lifetime pin.

`examined_rows` counts actual metadata projections (<=9), `examined_chunks` counts
copy-source visits (<=16), and `seek_steps` counts binary-search comparisons
separately. Map lookup examines tree nodes beyond metadata projection counts;
resident records are bounded by256. Tail storage is at most1MiB of positive-sized
chunks, bounding binary seek to at most21 comparisons. These are bounded work
claims, not measured latency guarantees or producer metrics. The owner never
walks engine history or the journal to fill an older page. A raw reference can be
unavailable; the API never manufactures evidence.

The UI adapter remains root's owner: it must pay one try-prepaid actual
observation-apply publication before adopting a result outside input/render tours,
and keep prior content explicitly stale on Busy or capture capacity refusal.
This observer itself does not record an ordinary successful tool read. Literal
source extents plus actual frames and the adapter publication have different
provenance purposes; no record2037/2038 is substituted for that publication.

## Behavioral proof

The permanent fixture is `tests/command_observation_test.cpp`, with CLI modes
allocation, ranges, ranges-raw, busy and lifecycle. Its actual child waits on a
private FIFO between emissions. One-byte emissions force source fragmentation;
130 gated8192-byte emissions cross the real1MiB resident cache frontier. Actual
owner acknowledgments establish retained bytes, including a deliberately withheld
received byte. No private owner state injection or synthetic journal scan supplies
the expected output. Raw-active pages resolve each source to actual2008 literal
bytes and assert no2037/2038 records from the pure observer. Thread-local allocation
refusal surrounds both empty and resident detail observations. A paused actual
old read allocation holds the genuine registry mutex while the query must return
Busy; no test-only production locking seam exists. Lifecycle coverage includes
live native ownership, complete nonzero exit, separate nine-projection pagination,
actual archive retirement and failed capture uncertainty.

Before production implementation, the same allocation claim failed using the
actual old allocating `CommandJobs::read` against the unchanged qualified Linux
foundation archive. The ignored adapter prebuilds its request before refusal,
then maps the actual read's allocation failure into Unavailable. Compilation
succeeded and the oracle exited1: “bounded empty observation remains ready with
all allocation refused”. This is behavioral RED, not a missing-symbol failure.

Current owning source and permanent fixture then compiled on Linux Release with
all production warning flags and linked the unchanged qualified foundation,
with the current owner object supplied explicitly. All five permanent cases
passed. Exact current recursive header closure, sources, command lines, old/new
archive identities, native job-runner source/binary identities and new object/
executable hashes are preserved under ignored
`context/tui-command-inspector-2026-10-16/`, including linux-red-metadata.json,
linux-red.log, linux-green-metadata.json, linux-green-source-hashes.json,
linux-green-build.log and linux-green-checks.log. No qualified old source/build
was edited. Current Mac owning source syntax with its actual compile command and
both owning clang-tidy translations passed. The fixture's initial swappable
count/width diagnostic was corrected using a typed ChildOutput value; no
suppression or production test seam was introduced.

The original private Mac old-baseline link eventually completed normally, and
its actual old-read allocation oracle failed the same permanent claim. No signal,
restart or duplicate link was used. One finite sample attempt of ld PID24290
(clang parent24185) returned0 after “Sampling completed, processing symbols...”
but produced no requested sample file; no stack diagnosis follows from it.
Component owner's one authorized Ninja had independently stalled at links.
After the component owner released the Debug build lease, the current Mac
command_observation_test target built successfully and all five exact CTests
passed (0.56s); mac-debug-build.log and mac-debug-checks.log retain that evidence.
ASan, TSan, complete source-gate and tranche performance qualification remain
pending root's coordination. These results claim only the reported checks.
Root also owns the one independent implementation/oracle challenge.

Root completed the one independent owner implementation/oracle challenge with no
finding: canonical fixed identities, separate catalog/detail budget, transparent
lookup, positive ordered tail chunks/random-access seek, retained frontier,
eviction/partial-source arithmetic, genuine contention and custody/nonzero-exit
interpretation were specifically checked. No additional review panel is needed.

Final integrated qualification and delivery are in [RESULTS](RESULTS.md); the
unit-local pending statements above describe their earlier report stage.
