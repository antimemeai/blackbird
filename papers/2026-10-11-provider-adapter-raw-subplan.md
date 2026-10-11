# Provider exchange owners and bounded originals

2026-10-11. Proposed whole provider-exchange unit for census33–34/36–40.
Authorization35/46 has its completed separate unit. This paper is a concrete
source/ownership plan for root's ONE plan attack; no adapter source is released.
The participant owner shares the original-acquisition contract below and owns
its queue/state/retention implementation separately. Neither unit owns another
original-custody service.

## Actual source and remaining claims

Read current coding.cpp request/operation/OpenAiCodingProvider, openai.cpp
openai_http/ResponsePreview/completed_response, provider_auth.cpp request and
provider_http, colleague.cpp prepare/call/native transport, tools.cpp exec,
native_process.hpp Child, provider injection declarations and every injection
caller, existing provider/colleague/auth fixtures, and the raw design/completion
papers. Existing1300–1303 and1400–1410 qualify the actual owned HTTP and
compatibility Child path: external bodies before send;8192-byte native reads
published before observers, accumulation, limits and HTTP/SSE parsing; original
native returns including errors/EOF/cleanup. Existing1200–1202 and1070/1080
regions do not complete assembly/retry/decode/preview/acceptance or colleague
owners. Raw stream preservation is separate from the optional64KiB
StreamCapture diagnostic sink.

The current deterministic config.http injection is a one-argument function
returning status plus arbitrary std::string. It bypasses1300/Child, and its
returned body has no API bound. A post-return wait followed by throw destroys
unpublished originals. Do not count Child tests as injection qualification.
Current native provider_http collect caps output at16MiB, including its four-byte
HTTP trailer; compatibility config defaults16MiB and permits a caller-selected
response_limit. ValueLimits/codec defaults also have a16MiB output bound, but a
codec limit alone does not bound an arbitrary callback. The new business
acquisition contract explicitly bounds that callback before invocation.

Grounding remains the studied ktrace input/return/raw-I/O split with kdump
interpretation after persistence; Lamport publication; and the actual
qmail/daemontools/s6 process/buffering/retry paths. Reuse the owned retained lane
and generalized protected claims. Reject reference filtering, normalization,
formatting, dropped errors and recorder-private loss windows. No new literature
campaign, SDK, dependency, timer or writer transformation.

## One shared original-acquisition contract

Use include/blackbird/provider_original.hpp for this business source contract,
with foundation implementation provider_original.cpp. The names below are the
shared provider/participant seam, not another metric subscriber interface.

* ProviderOriginalRole is fixed u8: request_packet1, prepared_admission2,
  upstream_packet3, decoded_packet4, wrapper_packet5, native_stdout6,
  native_stderr7, callback_body8, public_prompt9, public_instructions10,
  standalone_request11. It identifies an actual existing source.
* ProviderOriginalReference contains actual RawEventRef, RawPartition, native
  size_t record_offset and size_t bytes. RawPartition is the actual native u32
  enum. bytes describes the published memory range; an unknown-used snapshot
  does not turn that range length into an observed response-body length. Offsets
  are from the start of the raw
  record, including its16-byte envelope. Bodies emit fields separately; native
  object padding/pointers are never serialized. The Ref is unavailable when
  raw capture is disabled; do not invent ordinal0 as an observed publication.
* ProviderOwnedOriginal is move-only. It holds either the owned native byte
  extent or the ONE existing std::string adopted by move, plus the actual
  reference/range, ProviderOriginalExtentKind:u8 (exact_used1/unknown_used2),
  and optional<size_t> observed_used. Native/codec sources carry exact_used with
  their actual positive native return or ONE encoded output length. A callback
  snapshot carries unknown_used and no observed_used. A reference-only variant denotes
  published raw custody with semantic byte retention still unresolved; normal
  participant handoff carries its already owned bytes too. No retained borrowed
  span survives a call, thread transfer or queue yield.
* ProviderOriginalTicket stages the existing business capture node/metadata and
  owned writable storage BEFORE native acquisition. Codec-adopt mode stages
  only node/metadata/control block: the ONE business encoder owns its bounded
  output. ProviderOriginalCapture::prepare(ProviderOriginalRequest) returns an
  owned ticket; participant prepares it outside
  its product mutex, briefly rechecks/link-reserves under that mutex, and releases
  the mutex before raw admission. A refusal here prevents acquisition.
  complete(ProviderOwnedOriginal&&) noexcept returns ProviderCaptureDisposition
  (accepted/overflow/stopped and acquired native Error). It first moves/links the
  staged node without allocation, parsing, formatting or a recorder wait; only
  after that may the source raise the existing capacity/interruption outcome.
  Queue limits move this same original to the Run emergency node; they do not
  delete/truncate it. An unknown-used snapshot moves to the same emergency node,
  stops acquisition and remains unresolved semantic debt; it never becomes an
  accepted response, parse input or Retain call. The participant owner specifies
  those finite queue banks.
* ProviderOriginalLease is move-only and remains on the issuer lane. It holds
  that source's DATA claim and finite native-return CONTROL, and optionally a
  staged ticket/native extent. It cannot transfer its claims. The lease owns
  the publication/handoff guard: on downstream success or exception it moves
  the already published original into the staged ticket exactly once. Native
  owner/lane lifetime must outlive every claim. No epoch check purports to make
  illegal mapping destruction safe.

Physical source order is: stage business ticket/storage; acquire checked DATA
and CONTROL with inherited cancellation/deadline; native read or the ONE
existing business encoder; immediate copy/publication; finish last synchronous
borrow/preview/append; transfer owned bytes plus actual source reference.
No callback occurs in the final admission→read→publication interval. Product
prepare/complete are the existing capture handoff, before acquisition and after
publication. They are not instrumentation subscribers. Participant must not also
call its old borrowed raw callback and make a duplicate queue copy.

Default main/standalone Child capture keeps the existing8192-byte stack buffer
and synchronous borrowed preview, with protected DATA and the actual emitted Ref.
A participant ticket allocates its native immutable-storage control block and
buffer before read. Shared immutable storage is confined to this existing
business capture mode; codec-adopt mode stages its control block but does not
also allocate a maximum-sized buffer. The source pins its synchronous borrow
until the last use;
complete transfers that storage/reference without allocation. Publication itself
performs no shared_ptr copy or reference-count operation. Child reads
directly into that buffer instead. This moves existing business allocation out
of the post-read failure path. Native capture still performs one copy to the
raw lane. Pending whole-response accumulation remains a product copy. Do not
universally add heap storage or shared_ptr reference counting per native read.
Complete must follow the last body borrow: an owner drain can free a moved node
concurrently. An unwind guard completes after an observer/append/preview error.

Change RawNativePermit to hold DATA as well as CONTROL for selected positive
reads; read_permit claims E+R+u8+size_t+ssize_t+int+8192 before read, and read
returns its actual1404 Ref. A positive return publishes with that DATA claim;
zero/error publishes with its existing CONTROL claim. Unused claims release on
the issuer lane. Existing preheld cleanup remains available on cancellation.
The payload starts at offset E+R+1+sizeof(size_t)+sizeof(ssize_t)+sizeof(int),
which is61 on both current64-bit ABIs. This is an actual recorded source range,
not a locator to StreamCapture or discarded private process memory.

A business encoder is bounded by the actual call's ValueLimits.bytes. Hold
maxbytes+49 before its ONE existing encode; adopt the returned string and
publish it before any later allocation, display or capture callback. The encoder
is not run again by instrumentation. Its original packet lives in the staged
node on post-encode refusal/exception. A codec refusal before an output exists
has an Error terminal and no invented encoded original. Invalid source APIs
cannot advertise an unbounded Value/string as a qualified bounded acquisition.

The shared signatures are owned native C++ interfaces. prepare takes
ProviderOriginalRequest{ProviderOriginalRole role, ProviderOriginalMode mode,
size_t maximum_bytes}, where mode is native_extent, callback_extent or
codec_adopt. It returns Result<unique_ptr<ProviderOriginalTicket>>. The ticket
exposes its preallocated native storage for extent modes; codec_adopt has no
second body buffer. ProviderCaptureDisposition is native u8 accepted/overflow/
stopped plus optional original Error, returned by complete after transfer.
Preparation failure is an Error before acquisition; completion never throws.
Lease commit supplies the actual optional ProviderOriginalReference and actual
owned extent/adopted string plus exact-used qualification, not caller-invented
source IDs. Its unwind snapshot supplies unknown-used qualification and no
observed_used. Caller throws an
existing capacity/interrupted result only after inspecting that returned
business disposition. Issuer-lane claims die before the retained handle.

## Deterministic HTTP injection: native extent and actual outcomes

Replace ProviderHttp with the business callable
int(const ProviderHttpRequest&, ProviderHttpResponseLease&). Remove arbitrary
string-return compatibility. ProviderHttpResponseLease exposes
prepare(size_t requested)→writable native span and commit(size_t actual_used).
There is one prepared extent and at most one commit per callback. requested must
be <=16MiB; actual_used<=requested. This matches the current native application
response budget; it is a documented source API bound, not a metric filter.
prepare allocates the owned response storage once before the caller fills it;
commit shrinks/sets its business used length, publishes those exact original
bytes with protected DATA, and performs no wait/allocation. Even a zero-length
committed body is a distinct acquisition. It does not add HTTP trailers to a
callback result or pretend that this is a native read syscall.

For application use, wrapper holds16MiB+58 DATA plus99 CONTROL BEFORE invoking
the callback, after a live provider terminal guard and actual request/body
publication. It uses1310/1311/1312 below. A callback fills a partial extent,
commits its actual observed prefix, then throws: that original is already raw
published, the live lease hands its storage to the staged business ticket on
unwind, and1311 records the actual Error/allocation/length/unknown branch.
No invented status/Error fields for unknown exceptions.

The callback can also write then throw BEFORE commit. The lease guard publishes
the entire actual initialized prepared extent with unknown_used and no used
length. The owned storage is initialized before exposing a writable span; its
unchanged initialized tail and unreported writes are preserved as a native
buffer snapshot, never described as upstream response bytes or a complete body.
Use the same buffer and already held DATA, with no second allocation, encoder or
reader lookup. The guard transfers that snapshot and its actual Ref into the
participant's bounded emergency node, closes acquisition and leaves unresolved
semantic debt. It must not parse or call Retain on that extent. Without a
participant, raw custody retains the snapshot; the existing thrown failure
propagates. A failure before prepare has no buffer snapshot to invent. Process
death before guard publication remains unresolved acquisition; the design does
not claim preservation of bytes that were never published.

Returned status is captured unchanged before require_provider_success, observer
or parse. A normal callback return without commit still writes1311 returned with
that actual status. If an extent was prepared, the guard preserves it with the
same unknown-used variant and unresolved handoff before the enclosing phase
refuses the missing-body contract with conflict. No prepared extent means no
snapshot. No implicit success or empty-body fabrication.
Repeated prepare/commit and impossible used length refuse before publication.
After commit, business consumers use the same pinned original buffer. Without a
participant receiver, move its std::string into the existing ProviderHttpResponse.
With a receiver, complete the shared immutable original into its staged node,
then construct the existing business response string from that preserved extent.
That explicit product copy preserves the existing business response API; its
allocation failure cannot erase the queued original. No two competing unique
moves or dangling borrowed body. The source must not consume its data claim twice
or transfer it to another producer.

Private OAuth/token/JWKS callbacks use this same bounded business body interface,
with payload capture inactive. Their already reviewed1609/1610 private native
status/Error pairs remain the only instrumentation; payload-inactive leases do
not acquire a new raw DATA/control claim or wait under Store. No private bytes, credential
headers/config/argv/environment or private URLs are emitted. Update the actual
injection users in tests/provider_auth_test.cpp, provider_oauth_test.cpp,
provider_auth_raw_test.cpp and provider_colleague_test.cpp, plus the private
http wrappers in provider_auth.cpp/provider_oauth.cpp. Production selection of
an injector remains the existing deterministic configuration seam. No registry,
extra callback layer, new custody process or per-emission encoder.

Byte/allocation cost is explicit: one response storage allocation for the
requested extent (not an unconditional16MiB allocation), up to requested bytes
initialized by its owned native storage implementation if std::string is used,
one raw copy of actual_used (or requested extent bytes on uncommitted unwind),
native moves into the existing response/node, and
one explicit product response-string copy only for the participant injection
mode described above. Its storage control block is allocated before acquisition.
The16MiB held DATA is capacity credit, not a16MiB body allocation. A callback can
prepare the exact200-byte fixture extent. Native Child's ordinary main path gets
no new allocation. Participant staging substitutes its existing queue allocation
and removes its post-read full original copy. No speed claim without measurement.

## Explicit identities and sites

E=16; R=sizeof(IdentityBytes)+sizeof(u64)=24; all native fields are copied
individually. Error is u8 ErrorCode plus i64 detail, only when actually acquired.
Terminal branches are1 returned,2 original Error,3 allocation,4 length/capacity,
5 unknown exception,6 unresolved scope. Every BEGIN has separately held terminal
credit before publication and a fully live guard; explicit start happens inside
a try block, so DATA admission failure cannot strand a constructed BEGIN.
Preserve errno around hooks, and native errno immediately unchanged on acquire.
Descriptions include actual ABI widths and every optional branch/role.

| Site | Exact body | Native64 record maximum / ownership |
| --- | --- | --- |
|1310 callback invocation|application request R,size_t maximum response bytes|48 CONTROL|
|1311 callback return|1310 R,u8 branch,bool status_present,[int status when returned],[Error only originalError]|51 CONTROL maximum|
|1312 callback extent publication|application request R,ExtentKind:u8,size_t requested,bool used_present,[size_t actual_used only exact_used],native extent bytes; exact_used commits the observed prefix, unknown_used snapshots the whole initialized extent|16MiB+58 DATA; exact-used body offset58, unknown-used offset50|
|1314 native response acquisition begin|application request R,child R|64 CONTROL|
|1315 native response acquisition end|1314 R,u8 branch,[originalError]|50 CONTROL maximum|
|1320 semantic phase begin|u8 kind,bool parent_present,[parent R],bool business_owner_present,[attempt16,invocation16,generation16]|91 CONTROL maximum|
|1321 semantic phase terminal|1320 R,u8 branch,u8 stage,[originalError]|51 CONTROL maximum|
|1322 existing original|1320 R,u8 role,size_t bytes,exact existing bytes|limit+49 DATA; body offset49|
|1323 owner-selected branch/scalar|1320 R,u8 static stage,u8 native variant,variant native fields|budget by each source below; no new Value/traversal|
|1324 actual source link|semantic1320 R,actual request/response/source R|64 CONTROL|
|1325 retry selected|group1320 R,actual retryGroupId16,unsigned max_attempts,unsigned base_ms,unsigned cap_ms|68 CONTROL|
|1326 attempt retry decision|group1320 R,unsigned actual ordinal,prior attempt1320 R,unsigned delay_ms,bool eligible|73 CONTROL|
|1327 retry wait clock|wait1320 R,clockid_t method,int result,int errno,time_t seconds,long nanoseconds|68 CONTROL|

Reserve1310–1315 in the existing provider1300–1319 range and1320–1359 for these
semantic owners, subject to root's registry check. Existing1300 layouts stay
unchanged;1200–1202 can retire as new owner sites take their place, with old
immutable descriptions retained for existing runs. Do not duplicate prepared
packet/body capture merely to keep both site families active.

RawProviderReceipt is a stack-owned native output of actual request and response
Refs with explicit presence. RawAdapterContext contains the existing optional
RawProviderOwner (whose dispatch is the actual1080 region), explicit semantic
parent R and a borrowed receipt pointer. Pass this typed context through owned
and compatible HTTP, coding provider and native colleague calls. A colleague
semantic Ref is linked with1324; it must not masquerade as an1080 dispatch.
The actual1314 record means response acquisition begun before collect, not a
received successful response. Child1400→request1300 and1314→that actual Child
join every1404 original to its response. Parser/acceptance owners get the actual
receipt even when transport throws after a partial response. No TLS nearest-site
inference, pointer ID, new counter, timestamp ID or parent-tree registry.

## Per-owner placement and finite credits

A semantic phase holds at most91+51=142 CONTROL, not a generic arbitrary bank.
Use the actual optional-body size when known. One1323 fixed branch is42 bytes
(E+R+stage+variant); native scalar alternatives add sizeof(the existing scalar).
One Ref link is64. Claims release unused credit immediately after that phase.
Each repeated event gets its finite local claim before acquisition; do not reserve
500000 parser events on a256KiB control lane.

1323 native variants are0 no field,1 bool,2 unsigned,3 size_t,4 int,
5 std::errc,6 two size_t,7 native revision string (size_t plus<=32 bytes),
8 IdentityBytes (16 bytes). Semantic kinds13/14 are SSE-event/preview-event
subscopes, beneath their actual whole decode/feed owner.
Descriptions enumerate exact stages: assembly10 restart,11 protocol,12 workflow
drain,13 options/policy,14 final packet,15 generation/origin; group20 cancellation,
21 journal
warning,22 admission,23 transport,24 decoded-source capture,25 import,
26 settlement,27 retry decision,28 wait,29 usage observer,30 context conflict,
31 context append; decoder40 size,41 parse,42 node comparison,43 event type,
44 reported failure,45 incomplete,46 duplicate,47 item index/converter,
48 completed-output validation,49 final item validation,50 return;
preview60 input bound,61 append,62 parse,63 type,64 delta,65 presentation,
66 erase,67 return; colleague70 request encode,71 size,72 fields,
73 descriptor/binding,74 prompt format,75 upstream construction,
76 admission encode,77 admission capture,78 dispatch,79 upstream encode,
80 upstream capture,81 metadata projection,82 CLI exit,83 CLI parse,
84 decoded encode,85 decoded capture,86 wrapper selection,87 result encode,
88 result capture,89 return. A stage assignment is owner state used by its
terminal, not an emitted transition pretending the business operation happened.


33 Assembly: fully live phase kind1 before request validation/draining/options.
Hold the actual16MiB codec allowance+49 DATA before the existing final
encode_packet_string(request); publish that result immediately before Number
construction, diagnostics, request group issuance or dispatch. Copy existing
origin string/generation in1323 native variants7/8, with32-byte revision cap
from the actual ContextStore identity format. Assembly therefore prepays its
phase142 plus generation58 plus revision82 =282 CONTROL before start. No new
encode of options, input,
tools, usage or configuration. Failed preliminary business encoding/validation
has stage-specific native Error/exception terminal. Existing1070 clocks remain
separate, with their failure caught by this semantic owner.

34 Request group/retry: one group phase kind2 spans all attempts/acceptance and
actual existing retryGroupId. Keep that native issued ID before the existing
hex formatting used for product logs; the writer never formats/reparses it.
1325 copies the already validated unsigned policy. Each kind3 attempt links the
actual admitted attempt/invocation/generation and its transport/response.1326
copies the existing selected ordinal/delay/eligibility and preceding actual
attempt phase Ref (whose linked terminal holds the native error); no metric
retry counter.
The wait is phase kind4, with1327 raw CLOCK_MONOTONIC acquisitions at actual wait
entry/exit, including native clock failures. It never subtracts them or creates a
new timer. Existing business25ms cancellation polling is not redesigned here.

The source bounds max_attempts<=100. Prepay the group's full finite fixed
sequence: group phase142 + selection68 +100*decision73 +99*(wait phase142 +
two clocks68)=35032 CONTROL (sizeof expression in source, checked multiplication).
Actual max_attempts gives the smaller bound. Each wait/decision splits this bank,
without new admission after the preceding error. The actual group issued-ID is
inside1325, not an extra duplicate event. No recorder wait while holding product
locks; cancellation/clock/unknown terminals use the prepaid split.

36 Adapter semantic owner: kind5 begins before auth resolution, route/body
validation/export/dump or spawn. It encompasses auth/dump failures currently
outside1300. Auth retains its own private outcomes; link actual resolved action
and application transport, never copy credentials. Actual wire1301 uses its
existing dump string before send. Native response1314/1315 prepays114 CONTROL
before collect. HTTP interpretation remains1302; reject status only after
native observation, raw original preservation and1315 outcome. Injected paths
use the bounded lease above and their distinct invocation/body/return sites.

37 Decode/preview: kind6 is completed_response or owned messages/chat parse and
normalization, before parse; kind7 is ResponsePreview::feed before pending append;
kind8 is the caller's display/preview acceptance. Their per-phase142 CONTROL
precedes parser/observer acquisition. Raw bytes have already been published by
the source; no parser limit/error discards that original. For each actual SSE
parse/event, prepay phase142 + selected branch42 + index/converter54 + native
node comparison58 =296 CONTROL before its existing parse/from_chars operations.
The preview event path needs phase142+branch42=184; it has no index/node ledger.
Copy only selected source branch and its existing index/converter return where
those exist. Other event branches release unused bytes. Copy the existing
total_nodes/limits pair only at its actual business comparison; no extra node
traversal or accumulated parser counter. Every actual parser failure,
duplicate/incomplete/refusal/final-return and ignored-event branch is recorded.
Preview deltas are product-derived data already created by feed; observe native
presentation acceptance/error, without another encode or a metrics text join.
No global hooks inside every JSON codec/node are claimed by this owner unit.

38 Receipt/import/context acceptance: kind9 immediately on provider Value return
with actual response/source Ref; kind10 covers existing import_provider_response
argument parse, result original capture/settlement, context conflict and append.
Instrument the ONE existing operation encode of result by an explicit optional
provider-original sink/claim passed to operation; prepay max16MiB+49 before
provider dispatch, publish the existing string before log_.original can refuse.
Generic operation types keep their separate owner units. Physical wire originals
already survive even if the existing result encoder itself fails.
Context publication has a prepaid82-byte fixed revision return (E+R+stage+
variant+size_t+32) in addition to the phase142, held BEFORE append. Emit the actual new
head only after successful append return; Error/unknown has the actual stage,
not inferred commit. ContextStore's own internal commit/restore sites belong to
its separate unit. Usage/model remains in original wire/report bytes; the writer
adds no Number.text, projection, normalization or second report encoding.

39 Colleague preparation/admission: kind11 before prepare/base reply/validation.
Copy the existing encoded(request) size-check buffer before refusal; its actual
encoder limit is16MiB, although accepted requests are<=65536. Preserve existing
prompt/instructions and prepared admission bytes before transport; the actual
format/packet limits bound each to16MiB. Transient max16MiB+49 DATA is held before
each existing encoder/formatter output, then released after immediate publication;
never prepay six simultaneous16MiB bodies or add a second encoder. Finite142
phase terminal exists through all capture failures, which prevent dispatch.
Semantic source roles replace string inference in the shared ticket seam.

40 Colleague transport/wrapper: kind12 links actual prepared admission and actual
native HTTP or CLI execution. Native HTTP uses the same bounded originals and
receipt; messages/chat normalization is kind6. CLI opts only this reviewed Child
into raw combined stdout/stderr capture by passing typed optional context to
LocalTools exec; general file/tool/Beads/Git owners remain inactive until reviewed.
Do not capture argv/environment. Public existing prompt/instructions are explicit
original bodies. The actual native Child exit status is recorded before product
Number construction; no second exit parsing. Existing upstream_result,
decoded_result and wrapper result packet encodings each acquire one transient
16MiB+49 claim BEFORE that one business encode, publish/adopt before capture,
and use142 CONTROL per owner phase. Product metadata projection/refusal selection
runs normally; capture only its actual branch/status, not a writer projection.
All returned/refused/failed/unknown statuses, malformed upstream, nonzero CLI,
unknown exception, allocation/capacity, observer/capture errors and cancellation
settle their actual phase. A wrapper exception cannot remove originals already
published or held by the staged business ticket.

Main/colleague integration avoids a new metadata lookup or metrics-only parse:
operation gets an optional stack RawProviderOperationContext output/sink. It
copies the already created native attempt/invocation/generation locals before
dispatch and uses the caller's protected result-original claim at its existing
encode. The colleague body reads those exact locals; it does not call
root.attempt merely to populate a metric. The main provider body already has
its business-acquired admission metadata and uses that. The shared source
capability is explicit in the native transport context; synthetic/custom
transport adapters receive it through their existing business interface.

ColleagueTransport receives explicit optional RawAdapterContext/shared source
capture capability through its existing business call boundary. Its synthetic
Value result is subject to the ONE existing packet encoder's actual16MiB bound;
this is callback-return ownership, not a native upstream read claim. Arbitrary
external custom implementations must acquire/publish their raw sources through
the same bounded contract; a borrowed callback view alone is not native-read
qualification. Participant workers use the shared contract on their separately
registered lane, never ContextStore from the worker.

The standalone blackbird-colleague must bind a raw lane for these library hooks.
Its explicit NEW_CAPTURE_DIRECTORY is created natively0700 before acquisition,
with exclusive existing-directory refusal. Native getentropy creates the actual
EnvironmentId, persisted by the Runtime registration; no path/hash/session
surrogate. Runtime writes under that existing new root/raw, and its scope/handle
outlives Child/lease/claims. The existing request-file bytes (<=65536) are retained
before parse under a prepaid65536+49 source allowance. Native file syscall detail
belongs to the separate file owner, not a fabricated Child/read claim. The standalone launcher uses the caller-inherited RawWaitControl, with an
absolute validated request deadline, before prepare/bind/read. A new SIGINT/TERM
manager is not part of this unit; default OS termination remains a producer-death
cut with published-original recovery. Root will coordinate any existing native
signal scope extraction as a separate owner change. No duplicate signal manager.

## Per-phase control maxima to implement

These maxima include each phase's full91-byte optional begin and51-byte terminal;
unused optional-field credit releases. Every1322 original includes its owning
phase Ref, so it does not require a duplicate1324 link.

| Owner | Fixed held CONTROL before start/effect | DATA allowance |
| --- | --- | --- |
|Assembly1|142+generation58+revision82=282|one final codec16MiB+49|
|Retry group2|35032 worst case at100 attempts, checked formula above|none|
|Attempt3|142+actual request/response links2*64=270|protected result codec16MiB+49 before dispatch|
|Retry wait4|phase142+two clocks136=278 split from group bank|none|
|Adapter5|phase142+two request/response links128+max(native114,callback99)=384|existing exact1301 body; callback16MiB+58 only for injection|
|Whole decoder6|142+actual source link64=206|none; original source already published|
|Preview feed7|142+actual source link64=206|none; original source already published|
|Presentation8|142|none; product text already exists|
|Receipt9|142+actual response link64=206|none|
|Import/accept10|142+new revision82+response link64=288|one existing result codec16MiB+49, owned by attempt sink|
|Colleague prepare11|142|one transient16MiB+49 per existing output|
|Colleague transport/wrapper12|142+actual request/response links128=270|one transient16MiB+49 per existing packet|
|SSE event13|142+branch42+converter54+node comparison58=296|none|
|Preview event14|142+branch42=184|none|
|Native child return|existing61 read-return control, separate cleanup budget unchanged|selected native read8192+61=8253|
|Injected source operation|1310+1311=99, split adapter bank|1312 max16MiB+58|

All counts use sizeof expressions in code; checked sum/multiplication rejects
before effects. Independent adapter scopes retain their own existing1303 credit
and Child cleanup; the table does not silently charge them against the semantic
phase. Raw adapter/context links split the semantic bank before their publication.
Per-event/next-source claims are allowed at those explicit acquisition boundaries,
with fully live terminal guards; no recorder wait under Store, participant,
spawn or other product mutex. Expected fields wider than these supported profiles
require a source/description qualification instead of narrowing native values.

## Independent direct oracles and implementation sequence

1. Genuine baseline source: real RawRuntime plus current public owned/compatible
   adapter and colleague APIs. Literal original bodies/streams must be present;
   missing semantic1320/1314 and injected1310/1312 are actual reds. An isolated
   baseline fixture uses only baseline public APIs; no disabled hooks or synthetic
   baseline introduced by the candidate's new interface.
2. Bounded injection: exact200-byte response with binary/newline/NUL data,
   literal expected1310/1312/1311; exact actual Refs/native widths/status. Commit
   partial prefix then original io/ECONNRESET or std::runtime_error; exact prefix
   plus original Error or unknown branch, no observer/parse, no fabricated status.
   Independently initialize a literal extent, write a binary prefix then throw
   WITHOUT commit: compare the entire extent including initialized tail, native
   unknown_used/used_present=false, full actual Ref/range and original terminal.
   Guard publication must not be implemented by the fixture calling commit.
   Repeat normal return without commit (actual status remains distinct), and
   failure before prepare (no invented snapshot). Unknown-used participant
   handoff stops and remains unresolved debt, with zero semantic Retain/parse.
   Sticky cancellation/full DATA before invocation yields callback count0 and
   held live owner terminal. Reentrant callback emits other events without
   consuming protected body credit. At available capacity just below required
   maximum, preflight refuses before callback/body acquisition. Bound/repeated
   prepare/commit/invalid count or missing commit has the specified refusal.
3. Actual owned/compat native adapters: fake children validate original external
   request bytes pre-send and produce independently authored SSE/messages/chat
   bytes with partial writes, malformed tail/status, nonzero exit, malformed/
   incomplete/refusal/duplicate/limit paths. Compare exact1404 originals and
   full1300/1314/phase/response Refs, native status/Error/EOF/cleanup. Inject actual
   observer std::runtime_error and allocation/parse failure after publication;
   originals and every begun terminal remain present. Secrets never enter bodies.
4. Main real CodingEngine: exact assembled packet from the existing encoder,
   actual admitted IDs, two natural transient attempts then success, original
   failed attempt/prefix, existing selected delays/policy and raw wait endpoints.
   Cancellation during retry has no next dispatch. Result-log refusal after its
   ONE existing encode retains exact published result packet, original wire and
   actual failed acceptance stage; stale context and append failure never become
   successful publication. A thrown diagnostic/preview/capture cannot erase the
   earlier request/prefix. Distinct operations do not exchange receipts.
5. Colleague: actual native Responses/messages/chat and fake CLI modes, exact
   selected public prompt/admission, raw byte prefix/native exit, decoded/wrapper
   original packets and all refusal/failure/unknown branches. Refused admission
   callback has dispatch count0. Partial CLI timeout/nonzero/malformed/oversize
   output keeps originals. Direct source scopes with and without an explicit
   upstream owner compare exact native optional layouts and actual links.
6. Shared handoff direct source: staged real queue ticket before8192 read, fill
   DATA/CONTROL except preclaims, native read+raw publication before receiver;
   mutate/overwrite/free the original stack source after handoff and independently
   compare moved owned bytes and RawRunReader record range. Consumer refusal,
   downstream allocation/preview exception and producer death retain the same
   prefix/Ref; no queue allocation/read occurs under the product mutex. Parent
   participant fixture owns queue/emergency/retention checks, not a duplicate
   provider test panel. Actual P>A death cut must be inspected before kill.

After the shared contract and complete owner details pass root's ONE plan attack,
write authentic source-before-green oracles, implement this exchange unit,
qualify direct affected adapter/colleague/coding/auth injection checks and narrow
lint with one worker in build/nls-provider. Root owns helper/site/CMake integration,
ONE code/oracle attack, its fixes/affected recheck and integrated native profiles.
No commit, eval, mutation campaign or third assurance layer from this lane.

## ONE plan attack disposition

Root's [provider plan attack](2026-10-11-provider-adapter-raw-plan-attack.md)
found callback writes before commit could disappear on throw. The same prepared
initialized extent now has an explicit unknown-used snapshot branch, protected
by the preheld16MiB+58 grant, with no fabricated used length or returned status.
Known commits retain exact observed prefixes. Normal missing commit preserves
a prepared snapshot before its actual contract refusal. Private payload capture
remains inactive, including uncommitted failure. Native1404 and codec1322
retain their actual counts and unchanged61/49 offsets. Shared participant carrier
and handoff use native RawPartition:u32 and explicit known/unknown-used fields;
unknown-used storage stops acquisition and remains unresolved semantic debt.
The direct oracle includes genuine fill/throw without commit, not only a
committed-prefix failure. No further plan defect was asserted. Source remains
held until root's completed-owner checkpoint; no second review panel.
