# Command center native wire, version1

The raw envelope is existing native u32 length/site and u64 ordinal. Fields below
are emitted individually in host byte order, never by copying C++ object padding.
R is lane16+u64 ordinal, Text is size_t length+literal bytes, Opt(X) is bool then X
only when present. Query bounds and source counters are recorded as native size_t.
The three source cuts are independent; their shared UI generation is not a domain
revision. A result's raw/retained references remain absent when its owner has none.

4088 DATA starts with u8 version1, environment16, conversation16, u64 view owner,
u64 view incarnation, selected kind u8 (none0/task1/participant2/command3) and its
native ID when present. Task ID is u64 ordinal; participant ID is Text bounded128;
command ID is32 literal bytes. Then:

1. attempted TaskQuery, u8 publication status, u8 TaskObservationStatus;
2. attempted ParticipantQuery, u8 actual endpoint status, bool center binding retired;
3. attempted CommandQuery, u8 CommandObservationStatus;
4. Opt(cached TaskQuery + TaskPublishedObservation);
5. Opt(cached ParticipantQuery + ParticipantObservation);
6. Opt(cached CommandQuery + CommandObservation).

TaskQuery is Opt(u64 revision+size_t offset) cursor, Opt(u64 selected), size_t row
limit. ParticipantQuery is u64 endpoint incarnation, Opt(Text after), Opt(Text
selected), size_t row limit/byte limit/offset. CommandQuery is Opt(ID32 after),
Opt(ID32 selected), size_t row limit/byte limit/chunk limit/offset.

A TaskPublishedObservation is u8 publication status, RawUiValueOrigin (u8 kind,
Opt(R source), Opt(application16 fact), Opt(u64 revision)), R actual successful
UI ingress, then TaskObservation. The latter is u8 status, u64 revision, size_t
total, five size_t declared top-level counts, Text list title/bead, size_t row
count, that many TaskRows, Opt(u64 revision+size_t offset) next cursor, bool more,
u8 selection, selected TaskRow+Text note/blocker only when Resident, size_t
examined rows/copied text bytes. A TaskRow is u64 ordinal, Opt(u64 parent), u8
declared status, u64 version/updated, Text title/owner, size_t subtasks/done.

ParticipantObservation is u8 status, u64 actual endpoint incarnation, size_t row
count, ParticipantRows, Opt(Text next), bool more, u8 selection. Resident detail
contains one ParticipantRow and ParticipantDetail. Then every selection has the
Window and four size_t work counters (rows,fields,seeks,copied bytes).

A ParticipantRow contains Text ID, five ParticipantTexts (task/provider/model/from/
to), u8 phase, four size_t counts (queued/accepted/dropped/pending capture), nine
bools (stop_requested,sealed,business_finished,worker_started,worker_ended,
worker_joined,cleanup_closed,restored,reopened), and three R (admission/dispatch/end).
ParticipantText is bool present, bool truncated,
size_t original total, Text finite copied literal. ParticipantDetail has five
ParticipantTexts (initial/current/last answered request,result status,remote
disposition), three Opt(ProviderOriginalReference), three Opt(SourceReference),
and bool result available/projection known/projection truncated. ProviderOriginal
is R,u32 partition,size_t record offset/bytes; retained SourceReference is journal16,
u64 sequence. Window is size_t size/requested/begin/next/total,bool more, followed
by exactly size literal bytes. Copy/projection truncation is not source absence.

CommandObservation is u8 status,size_t row count, CommandRows, Opt(ID32 next),bool
more,u8 selection, selected CommandRow only when Resident. Window has eight
size_t fields (size,source count,requested,begin,next,cached begin,retained end,
received end), bool omitted prefix/provenance complete,u8 end reason. Each source
has R original,size_t chunk begin/offset/length; exactly size actual literal bytes
follow. Three size_t work counters finish the observation (rows,chunks,seeks).
CommandRow is ID32,u8 phase/custody,u64 admission order,size_t received/retained,
ten bools (pty,leader exited,output closed,worker finished,terminal retained,capture
complete,stop requested,timed out,has exit,sealed), three int (exit,signal,failure),
and R launch/terminal. Source references locate originals; literal DATA does not
invent missing original references.

4089 CONTROL is exactly R snapshot4088, R current input/action origin, u64 view
owner/incarnation/generation, bool adopted/has error, ErrorCode, i64 detail. It is
prepaid together with4088 before either emission or host publication. Its full
native byte claim is raw_envelope_size+48+3*sizeof(u64)+2*sizeof(bool)+sizeof(ErrorCode)
+sizeof(i64). A refused claim emits nothing and retains the prior accepted host
and applied reference. Action origin may be absent for native embedding; actual
TerminalUI dispatch supplies its current captured input-result reference.
