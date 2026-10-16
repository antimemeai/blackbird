# Bounded command observation owner study

2026-10-16. Studied the current source, TUI doctrine/components, and existing
[next-slice source map](../tui-next-components-2026-10-15/SOURCE_MAP.md).
The original task was read-only; root subsequently authorized the narrowly scoped
pure observation unit in OWNER_PLAN.md. No provider, real session, AFL, artwork,
reference acquisition or dependency selection accompanies this study.

## Actual owner boundaries

CommandJobs owns current rows under one mutex. Default8live/32resident are
configurable up to64live/256resident (command_jobs.cpp configure guard), correcting
the source map's old32-as-absolute statement. Job IDs are already exactly32 lower
hexadecimal bytes: start rejects any other extent/alphabet before map insertion;
CodingEngine::call derives them from its real OperationAttemptId through
context.cpp's two-lower-hex-digits-per-identity-byte formatter. An observer does not
need to tighten that contract. Invalid/future unsupported identity is explicit,
not silently missing. Fixed cursors must never allocate an arbitrary key.

Current read is unsuitable as an input/render-tour query. It allocates Value
projections under the mutex; registry reads visit every resident row. Selected
reads return at most65536 bytes but iterate all cached chunks, even after the byte
quota is filled. Each selected original emits a DATA source link. Its raw2037
request and2039 terminal use ordinary claims; raw_command_claim/raw_command_data
can wait on capture. read_process_output is even less bounded: coding.cpp scans
all semantic facts, appends all matching originals, then applies the requested
range. A tiny requested output range does not bound examined history or memory.
These owner/tool APIs remain unchanged; none should be called by new UI painting.

Workers capture at most8192 bytes per native read into immutable shared strings.
received advances at acquisition; retained advances only after the engine's
process.output original was successfully preserved. Tail storage is at most1MiB
of positive chunks, so it can still contain up to1048576 one-byte nodes. Its
ordered offsets permit a random-access deque binary seek bounded by21 comparisons,
then a separately limited16-chunk traversal. Only bytes below retained may appear
as retained output. Earlier retained originals may have left this recent cache;
cache absence is not absence from durable history. Retirement can also remove a
settled current row while its original attempt/output remain retrievable.

Lifecycle flags differ: leader exit, output EOF, worker finish, semantic terminal
retention, capture completeness and native custody are independent. sealed means
terminal retained AND capture complete AND custody reaped. A settled nonzero exit
can be sealed; neither means successful external effects. Preserve unavailable
custody and capture-incomplete states rather than deriving a clean checkmark from
worker return or counter decrement.

main.cpp supplies a shared CommandJobs to TerminalUI. Engine poll_commands drains,
retains and settles on the actual execution owner; idle_work runs only when not
busy. TerminalUI has its own input/output owner and existing direct foreground
read/control paths. process_output is a string callback; the '[command ID]' text
and generic operation-completion strings are not a typed source reference. First
inspection can select a genuine ID from a bounded native current-command catalog.
Per-transcript clickable references would need a new explicit typed publication
at that real engine callback, not regex reconstruction. Calling CodingEngine or
retained-journal scanners concurrently from TerminalUI is outside their ownership.

## Smallest observation boundary

Use one allocation-free noexcept try_observe value return. It performs one
try_lock, returns Busy immediately if unavailable, projects at most8 catalog rows
plus one selected row, and copies at most4096 retained bytes from at most16 source
chunks into owned fixed arrays. Fixed32-byte IDs/cursors and transparent
std::less<> map lookup avoid a hidden temporary allocating string. Catalog keys
are in actual map order; continuation means after that fixed key in a live current
registry, not a historical or atomically frozen inventory. Map search is bounded
by the actual resident256 limit; projected rows and chunk visits have independent
limits. No filtering/full-registry sorting/complete-history query is hidden here.

A single locked cut copies actual flags/frontiers and exact byte/source windows;
UI keeps no borrowed strings or registry pointers after return. Bounded window
copy avoids pinning up to16 whole8192-byte allocations merely to show4096 bytes.
A page carries requested/actual start, next offset, retained/received/cache first,
source extents, and whether it stopped at frontier, byte quota or fragmentation.
Cached-prefix omission and not-resident status remain explicit. A zero-byte query
is metadata-only. Reads do not acknowledge bytes, drain events, settle, archive,
join, alter foreground, send input/signals or fetch old history.

Root's adapter owns session/view/query incarnations and validates them before
adoption. CommandJobs stays bound through the actual shared owner supplied for
that session. Its byte offsets do not become screen columns. SceneHost's current
selection reconciliation treats its supplied collection as membership; a missing
row in a paged window must not be misreported as domain retirement. Keep the
selected command detail reference separately through catalog pagination.

## Raw capture route

Do not reuse2037/2038 tool reads or ordinary raw claims. The pure observer emits
nothing and waits on no recorder. Literal captured frames prove displayed cells,
but do not by themselves prove which complete job reference/source extents supplied
a detail window. Root should record one actual UI observation-apply publication
when adopting a ready result: scoped full ID, view/query generation, copied-range
frontiers, actual lifecycle/custody/capture scalars and genuine source extents.
It is a derived local-view publication, not a successful command/tool read receipt.

At the adapter's80ms-open-inspector service point, acquire its finite maximum
CONTROL credit with raw_try_claim_control before observation/adoption, outside key
handling/rendering. The existing raw_runtime.cpp try-claim is nonallocating, makes
no RPC/poll and returns capacity/unavailable. Insufficient credit keeps last-good
content explicitly stale; it must not invoke an ordinary waiting claim. Encode
actual outcomes, including Busy/unavailable, not invented success. Existing2008
original bytes plus valid extents reconstruct the selected source; the existing
captured frame proves its safe rendering. No source ordinal may be manufactured.
When raw source is unavailable, say so; if literal fallback capture is desired,
it needs its own bounded try-DATA grant. Reusing a prior applied observation's
identity for an unchanged redraw avoids pretending every frame is a fresh query.
Accepted observation and physically emitted frame remain separate generations.

For scale, today's selected read of a32-byte ID emits a73-byte2037 request,
58-byte2039 terminal and80 DATA bytes per resident2038 source extent, before any
UI publication/notification overhead. These actual costs explain why existing
'just read it' is unsuitable. The new fixed record's exact native-size budget
must be calculated from its selected fields/array counts by root, not guessed or
paid by growing control capacity. Text stays in original DATA plus actual frames.

## Direct oracles and existing reference lessons

Use real children and actual retention acknowledgments: pending bytes stay hidden;
received>retained remains visible as a frontier distinction; exit-before-EOF and
capture failure stay distinct; settled nonzero exits are complete but failed work.
Force one-byte chunks and seek late ranges: source extents must name only copied
retained bytes, and fragment/byte budgets stop independently. Query count0/oversize,
invalid ID/cursor, retirement, wrong view/query incarnation and out-of-order apply.
Prohibit allocations during observation; hold the actual mutex via a controlled
existing read allocation and require Busy before allowing that holder to progress.
Full raw lanes must leave native editing/navigation/stop and last-good page useful.
Microsoft terminal literal-cell checks, readable snapshots and matched tranche
performance apply to root's eventual renderer; this owner study adds no new gate.

Read acquired Ito controls/list.lua identities(): stable item identity is useful,
but its full-item walk is not a bounded query. Read the already acquired component
REPORT.md and performance study's pinned FTXUI/Neovim/Lite XL discussions: queue
budgeting cannot interrupt one blocking callback, and latest presentation can be
coalesced without dropping accepted work/originals. These references guide the
boundary; no toolkit/source is adopted or newly acquired.
