# Participant observation owner results

2026-10-16. Implements the agreed smaller endpoint in
[PARTICIPANT_STUDY](PARTICIPANT_STUDY.md), following the command-inspector tranche
at `f2ab70b`. Root owns current-session integration, raw UI adoption, source gate
and the independent implementation recheck. This report covers this owner only.

Participants now exposes a lifetime-safe ParticipantObservationReader. Its shared
State owns the product mutex and a nullable Impl pointer, never a Run or thread.
The reader try-locks and copies an owned native page: at most eight roster rows,
one selected detail, capped metadata and 4096 selected result bytes. Rows and
selected detail come from one guarded sample. A retired reader stays retired even
if a new owner uses the same run ID. Integration binds the live incarnation to
the actual environment/conversation; the native owner does not equate a definition
generation with session identity.

Projection uses const existing Values, checks each shallow object against 32
fields before lookup, and never traverses nested request/result metadata. Invalid
shape uses a sticky validation flag rather than exception allocation. Valid
queries allocate nothing and perform no drain, Retain, Save, provider invocation,
raw admission, callback, control, join or journal scan. Ordered map seeks are
bounded by 32 resident runs; `seek_steps` counts actual ordered seek operations,
not internal key-comparison counts. Examined fields/rows and copied bytes count
the work actually performed. Fixed text has presence, original length and explicit
truncation. The result projection has explicit completeness knowledge; old restored
rows do not acquire invented completeness or source references.

Only missing facts are cached at their actual publication points: initial/current
request and prepared admission references, latest wrapper-result raw reference,
Retain's returned semantic reference, projection truncation, worker entry/END/join,
and each run's pending original count. Result retention may precede worker answer
publication; request identity binds the cached reference to the correct answer.
The post-Retain path moves its pre-staged request token, without copying a request
body or adding an encoder/allocation. `cleanup_closed` publication now takes the
product guard. No query reads a jthread or native descriptor. Existing business
state, cancellation, original capture and cleanup ownership remain intact.

## Direct evidence

The dedicated `build/participant-observation` uses the pinned Debug preset,
Clang23.1.2 and qualified Xcode SDK. No shared Debug Ninja lease was used.
Exact logs are in ignored
`context/tui-command-center-2026-10-16/participants/`.

- Honest unavailable observer seam: target compiled/linked, then **5/5 behavioral
  RED** in `old-red.log`, total0.78s. All five reject an unavailable native roster.
  Initial direct-compiler SDK/include/fixture setup errors and incorrect initial
  library linkage are retained separately; none counts as RED.
- Final new observation5 plus existing participants and custody3: **9/9 GREEN**,
  1.96s, `green-build-05.log` and `green-05.log`.
- Direct actual translation-unit clang-tidy: source and test both exit0, empty
  `owner-tidy-02.log` and `oracle-tidy-03.log`. Earlier test shape findings and
  fixture setup failures remain in prior numbered logs. Scoped diff check passes.

The five permanent cases attack distinct observation claims:

1. `semantic`: actual gated transport, nonhex run ID and addressed request;
   allocation denial on observation; unchanged Retain/Save/transport counts;
   answer visibility without drain; pending originals; distinct initial/current/
   last-answer IDs while another direction executes; explicit owner join.
2. `ranges`: all32 actual resident runs, three-row continuation, exact997-byte
   pages over the known8192-byte truncated projection, examined work budgets,
   malformed ID and oversized limits, and absent selected identity.
3. `busy`: actual product guard is held while configure allocates its returned
   Value. The fixture arms the allocation pause only after Save's preparation;
   reader returns busy before release and later recovers. The initial first-
   allocation schedule proved to pause outside the guard and was corrected,
   rather than weakening the busy assertion.
4. `lifecycle`: selected archive retirement; real concurrent reader/destructor
   schedule; retained reader retirement; restored unknown/reopened state and new
   scope; old scoped selection refusal; oversized shallow shape refusal; large
   nested metadata ignored without allocation or invented completeness.
5. `raw`: genuine recorder and worker lane; actual prepared/result original refs;
   published raw result before semantic retention; injected Retain failure keeps
   debt/source; explicit successful drain publishes exactly the retainer's returned
   SourceReference. Independent native envelope/reference resolution finds exact
   raw original extents and byte equality with the retainer's original bytes.

The final oracle also explicitly decodes native1322 role/size/offset before
matching an original reference, and checks absent semantic Retain returns remain
absent after owner join. Affected Debug semantic/raw and the strengthened raw
schema case pass; exact supplemental logs are oracle-final-{build,checks}.log
and oracle-schema-{build,checks}.log. Final test clang-tidy exits0 with empty
oracle-tidy-final.log.

Affected sanitizer profiles use the existing pinned Clang23.1.2 Debug setup,
qualified Xcode SDK, parallel3 builds and sequential CTests. ASan+UBSan passes
**9/9 in4.82s**; TSan passes **9/9 in9.95s**. Both run precisely the five new
observation cases plus existing participants and custody3, including real
reader/worker/retirement schedules. No sanitizer diagnostics, suppressions or
deadline changes occurred. Exact configure/build/check logs use asan- and tsan-
prefixes in the same ignored participants directory. Both Ninja leases are free.

Root performed the one independent implementation/oracle challenge over the
whole diff, query lifetime, cleanup/join/restore/capture/drain transitions and
permanent tests; it reported no concrete defect. Root owns its review record.
This owner run does not claim whole-suite, actual-main, screen or performance
qualification; those remain the tranche integration owner's checks.
