# Provider request / native child raw emission

2026-10-11. Written owner subplan for nls.2 completion group 2. This paper
specifies the first real adapter/child vertical and its direct oracles. It does
not claim that all provider, file, Git, Beads or decision-model owners are done.
Source changes wait for the runtime admission interface and root review of the
details below. No dependency, evaluation or new broad research campaign.

## Source and purpose

Read `src/native_process.hpp`, `src/provider_auth.cpp`, `src/openai.cpp`,
`src/colleague.cpp`, `src/decision_models.cpp`, the actual coding-provider call,
and the existing provider/decision fixtures. The settled raw transport/recorder
design and its qmail/daemontools/s6 source studies supply the mechanism: copy
existing facts, publish, and let the separate recorder preserve the originals.

The first vertical is the application Responses transport using the owned
`provider_http` child path. Complete the compatibility OpenAI child path with the
same helper before claiming the main provider is covered. Each request preserves
the existing external JSON body before the child can send it; each actual bounded
stdout read preserves the original bytes before the observer, limit check,
HTTP-tail split, stream parser or preview. Write native returns, not derived
durations, estimates, summaries or normalization.

There is an important source distinction: `provider_http` also transports OAuth
tokens, and `Child` also runs the token-bearing Codex authorization RPC. Global
child payload capture would copy private authentication material. The semantic
application owner explicitly supplies a trace context. The default shared-child
mode is inactive until its owner is reviewed and opts in, including mechanical
facts; this avoids admission waits inside unreviewed private auth Store locks. No scanner attempts to recognize/redact secret
strings. Curl configuration, argv, environment and header bytes are never bodies
of these hooks. The exact pre-existing application body is a separate record;
curl input writes record native requested/returned amounts only.

## Identity and interface

The published request-begin record is the transport request identity:
`lane_epoch[16], ordinal:u64` (24 bytes, called `Ref` below). Its distinct response
records all contain that request Ref. Child spawn-begin is the child incarnation
Ref; the real returned PID/PGID is an additional native fact and cannot replace
that incarnation. No IDs based on clocks, pointer values or an extra counter.

Pass a typed optional provider owner context through `CodingProvider::respond`,
`OpenAiConfig`, and `ProviderAuth::request` to the transport. It contains only
existing attempt, invocation and source-generation identities and the existing
1080 owner-region Ref. Supply it explicitly at the coding owner boundary;
do not infer a parent from nearest lane ordinal or install a callback registry.
A default three-argument overload forwarding to the existing two-argument
provider interface keeps external/fake providers working while the owned adapter
uses the context. Null context means no invented business identity; a standalone
adapter request still has its own transport Ref. Exact signatures are a small
implementation choice after the root's runtime wait interface lands.

`Child` owns an inline optional raw-child state: immutable request Ref, child Ref,
stdout/stderr acquisition policy, and a move-only terminal control claim. Hooks
call the owned immediate emitter directly. Existing output/error observers keep
their business role and execute only after raw publication. They do not become
instrumentation subscribers. The capture helper has no heap-backed callback.

## Exact bodies and sites

Reserve sites 1300–1319 for the adapter and 1400–1439 for this shared child
mechanism; check the actual immutable-description table before use. Each listed
field is a separate byte span, never a dumped C++ struct. `size_t`, `ssize_t`,
`pid_t`, `int`, `short` and `id_t` are copied native representations; descriptions
publish their sizes with source/ABI identity. Ref is exactly 24 bytes. Define
source-stage/channel/branch enums with explicit u8 representation. Native errno
is copied immediately and unchanged; it is meaningful on native failure. Preserve caller errno.

| Site | Body in order | Boundary |
| --- | --- | --- |
| 1300 application request begin | owner-present:u8; when present: attempt[16], invocation[16], generation[16], owner-region Ref; timeout_seconds:int | Before adapter validation/effects, after held terminal credit |
| 1301 existing external body | request Ref; method_length:size_t, method bytes; url_length:size_t, URL bytes; body_length:size_t, existing body bytes | Before spawn/send; no second formatting pass |
| 1302 adapter HTTP observation | request Ref; child Ref; status:int | Immediately after the existing native HTTP-code parse, before acceptance |
| 1303 adapter return | request Ref; branch:u8; stage:u8; ErrorCode:u8; detail:i64 | Prepaid terminal; explicit returned/error/allocation/capacity/unknown-exception/unresolved-scope branches |
| 1400 child spawn begin | request Ref; merge_error:bool; separate_error:bool; capture_stdout:bool; capture_stderr:bool | Before spawn mutex and native child setup; capture flags distinguish the permitted read-body variants |
| 1401 child spawn return | child Ref; native-stage:u8; result:int; error:int; pid:pid_t; group:pid_t | Successful posix_spawnp or actual failing native setup operation; before business throw |
| 1402 child poll return | child Ref; channel:u8; fd:int; events:short; timeout:int; result:int; revents:short; error:int | Each acquired poll return, including zero/EINTR/error |
| 1403 child input write return | child Ref; requested:size_t; result:ssize_t; error:int | Each actual write, before remove_prefix or throw; no input/config bytes |
| 1404 child output read return | child Ref; channel:u8; requested:size_t; result:ssize_t; error:int; when result>0 and that channel is selected: exactly result original bytes | Before observer, append/limit, EOF close or throw |
| 1405 child wait observation | child Ref; method:u8; result:int; error:int; si_pid:pid_t; si_uid:uid_t; si_code:int; si_status:int | Actual waitid return, before exit-code conversion or branch |
| 1406 child descriptor close return | child Ref; channel:u8; fd:int; result:int; error:int | Actual input/output/error or acquired temporary endpoint close |
| 1407 child group signal return | child Ref; group:pid_t; signal:int; result:int; error:int | After the actual owned-group kill, while leader identity remains pinned |
| 1408 child final reap return | child Ref; pid:pid_t; result:pid_t; error:int; status_valid:bool; status:int (meaningful when valid) | Final actual waitpid return; no converted exit code |
| 1409 child release | child Ref; branch:u8 | After the finite cleanup sequence; not inferred effect success |
| 1410 child operation refusal | child Ref; operation:u8; stage:u8; ErrorCode:u8; detail:i64 | Cancellation, deadline, capacity or observer failure before further acquisition |

The HTTP status is copied from an existing business parse, not parsed for metrics.
The raw stdout record includes curl's existing `\nNNN` write-out suffix. Consumer
code can separate it later. It is not an exact TLS/network send capture: the
body record states which external body was offered to curl and native pipe
records state what curl accepted. Do not label accepted pipe bytes as remotely
received bytes or invent an upstream response identity absent from the protocol.

## Admission, credit and ownership

All admission happens outside `process_spawn_mutex` and before the measured
native syscall. The existing cancellation callback can run `poll_commands()`
and emit on this same lane. A capacity check preceding that callback is therefore
insufficient. The root's safe wait interface must either hold an actual data
credit, or finish cancellation callbacks and recheck immediately before the
sole-owner check/acquire/publish interval. No callback, allocation, owner yield or
product-lock acquisition belongs in that interval. Read result spans refer to
the stack buffer and are copied before its reuse.

Let E=16, R=24. On the qualified 64-bit profiles:

* Request terminal is E+R+1+1+1+8=51 bytes. The existing HTTP observation is
  E+2R+4=68; hold both (119 total) before site1300 so acquisition of the parsed
  HTTP status cannot be followed by an admission wait.
* Spawn result is E+R+1+4+4+4+4=57 bytes; retain its capacity before entering the
  spawn mutex. Setup failure is one actual native-stage/final-result record.
* Read maximum is E+R+1+8+8+4+8192=8253 bytes. Reserve that before an 8192-byte
  read. Positive return consumes the actual raw-data size; release unused credit.
  If the return is zero/negative, publish its fixed 61-byte return from held
  control credit, so a newly fenced data lane cannot hide EOF/error.
* Poll fixed record is E+R+1+4+2+4+4+2+4=61 bytes; write fixed record is
  E+R+8+8+4=60. Prepay the result before the respective syscall. Every normal
  retry reacquires capacity before another call. No retry result is synthesized.
* Waitid is E+R+1+4+4+4+4+4+4=65 bytes on these profiles; copied widths are
  described rather than assumed for a new platform.
* Close record is E+R+1+4+4+4=53; signal is E+R+4+4+4+4=56; final reap is
  E+R+4+4+4+1+4=57 worst case; release is E+R+1=41.
* A spawned child holds 12*53+56+57+41=790 cleanup bytes before spawn: six closes during pipe normalization, three child-side endpoint closes after spawn, and three final owned endpoints. An
  already closed input consumes its close allocation once, not twice. Spawn
  result plus one refusal (E+R+1+1+1+8=51) makes total fixed child credit898.
  Per-call poll/read/write/wait observation claims are separate; teardown never
  waits for recorder capacity or allocates another claim.

Calculate sizes from `sizeof` in code and check these numbers in the ABI note.
Current capacities are not selected here. The default64MiB data lane fits the
16MiB application-body limit plus framing. A smaller configured lane must refuse
the original body before spawn; never shorten/split it silently or perform an
effect first. Body export failures, invalid URL/method/timeout, setup failure,
partial send, stdout/stderr acquisition, EOF, bad_alloc, limit/observer refusal,
HTTP-tail failure and status rejection are explicit owner outcomes.

One design detail needs root review before code: existing destructor waitpid
retries EINTR without a finite bound. Finite prepaid cleanup can preserve the
final native return/status but cannot preserve an unbounded series of teardown
EINTR calls while also promising nonblocking cleanup with a dead recorder. Site
1408 is deliberately the final return of the existing reap operation; it must
not claim to be every underlying retry. If the requested scope is every teardown
retry, change cleanup ownership rather than silently dropping them. Ordinary
active poll/read/write/waitid retries are each recorded with fresh admission.

No further input/output effect is acquired after cancellation/deadline/admission
failure; earlier complete publications survive and prepaid cleanup runs. A child
or parent dying before a full return publication leaves that operation unresolved.
Do not manufacture cancelled, failed, EOF or success for it on recovery.

The root approved one absolute application-adapter deadline at entry, before
request/body admission and spawn. Send and collect reuse it; inherited cancellation
FDs and an earlier inherited deadline remain active. This intentionally includes
preparation/spawn in the existing transport timeout budget. Raw admission expiry
becomes the existing provider_transport/28 outcome; cancellation remains the exact
interrupted/ECANCELED outcome. Private authentication transport keeps its existing
deadline behavior. Existing business poll intervals are unchanged; no instrumentation
timer was added.

## Direct red oracles

Use one native fixture executable which also acts as fake curl when invoked with
`--disable`, like the existing decision-model fixture. It supplies literal bytes
and native statuses. No network/token/account service calls. A separate control
pipe makes reads/writes deterministic where native chunk boundaries matter;
the pipe handshake is a fixture protocol, not production metric processing.

Independently decode preserved raw frames in the test, without calling the
production body-decoder/helper. Compare field widths, identities, permitted
optional variants and original bytes. For unconstrained pipe scheduling, require
concatenated stdout/stderr bytes to equal fixture literals exactly and each
record's native positive return to equal its body length; do not assert child
write boundaries are parent read boundaries. Check actual native syscall facts
at deterministic cuts rather than a test-generated metric shadow stream.

| Fixture / cut | Expected original and owner result |
| --- | --- |
| Normal Responses stream | Literal request body exactly once before spawn; literal SSE bytes plus `\n200` exactly preserved before observer; HTTP200 record, native CLD_EXITED/status0, explicit adapter return |
| Binary stdout and stderr through opted-in Child | Literal `A\0B\xff\n` and `E\0R` kept independently; positive counts, separate native EOF records and actual exit7; merge mode identifies the single acquired pipe |
| Controlled partial input writes | Child drains only fixture-controlled pieces; write-return records retain requested size/native accepted amount and actual failure, with no curl config bytes in any record |
| Missing executable / setup failure | Actual posix_spawnp or setup errno and stage; no invented pid, response or success; cleanup follows actual acquired descriptors |
| Curl exits56 after partial stdout | Literal partial stdout retained, actual wait status56, provider_transport56; no HTTP observation or response-success |
| HTTP401/429/529 and valid empty200 | Exact unmodified output suffix/body, native status observation, owner's actual acceptance/rejection. Status0xx/out-of-range/non-digit/missing suffix records parse rejection without an invented HTTP observation |
| Malformed SSE/JSON or preview observer throws | Entire already read chunk retained before the observer/error. Subsequent reads are absent after throw; cleanup raw facts and explicit refusal remain |
| Response output limit exceeded | The entire overflowing acquired chunk is preserved, including bytes the current pending-string limit rejects. Caller receives capacity; no locator to discarded bytes |
| Blocked recorder/full lane before read | Fixture proves bytes remain unread until admission. Cancellation/deadline wakes admission; already published prefix unchanged, no post-cancel read, terminal cleanup completes without the recorder |
| Fencing immediately after a successful read publication | Complete raw record appears exactly once, held terminal records still publish, no blind retry of emitted bytes |
| Parent death after publication | Existing runtime recovery preserves the exact child record(s), without replaying request or inventing adapter outcome |
| Private OAuth/token and curl config sentinels | Inject unique synthetic credentials into auth body/RPC/header/config; no payload records for private exchanges and no metric record containing config/header sentinels. Application JSON body remains byte-exact |

Distinct fault mechanisms: borrowed-channel exact source-to-record checks test
layouts/admission/observer ordering; real-runtime preserved-disk fixture checks
the activated owner path. Mac/Linux debug/release and ASan/UBSan cover this shared
native owner; static review attacks no waits under spawn mutex, no lifetime leak,
SIGPIPE/errno behavior, final group action before reap, and no writer transforms.
Do not repeat settled transport proofs or add a test that certifies this decoder.
Root integrates the full owned source gate once for the completed source unit.

## Implementation and qualification

The owner helpers and both real adapter paths now implement these sites. Native
setup cleanup also fixes the existing pipe-fcntl failure path: closed descriptors
are invalidated before outer cleanup, avoiding a second close of their old numbers.
All acquisition permits are held across their actual native operation. Final
cleanup consumes only the initial held credit, including setup failures and
normalization closes.

The initial source-before-green sequence was not followed: the first oracle was
written before integration, but its baseline run was delayed by the in-flight
runtime build and source was integrated before that run. The root required an
actual historical baseline qualification instead of a disabled-hook simulation.
A detached checkout of 206c106 built the public-API-only adapter oracle. Its actual
result was exit1, `raw read original missing before observer`. The same source
compiled against the candidate's qualified debug flags and foundation archive
returns exit0. No production switch, fabricated hook or mutant produced the red.
The temporary checkout is local qualification material, not a retained dependency.

The direct provider test now passes the borrowed-channel native/body checks,
both adapters' non-Error observer exceptions, actual disk preservation, real
full-data-lane cancellation/expiry and actual producer-death recovery. For the
full-lane tests the actual supervisor/recorder are stopped; a fixture poll entry
watch observes the queued status-RPC wait without substituting its return. Because
the supervisor is stopped, this is not a distinct later lane-space-poll oracle;
the marker matches either poll. Existing raw_admission tests separately cover
the stopped-recorder/running-supervisor full-space deadline. The provider test
requires original prefix bytes, no post-refusal read, exact caller outcome and
prepaid refusal/reap/release while the recorder remains stopped. This found and
fixed missing refusal emission in Child::line. For producer death, the actual
provider observer parks after raw publication; all original holders die and the
test waits for the actual catalogue lease to become free before cold recovery.
At the parked cut the parent opens the actual reported lane, checks A==0, and
independently decodes the already published native1404 original. This exercises
recovery of unacknowledged source bytes, not merely reading an earlier disk copy.
Recovered native bytes equal the literal binary curl output exactly; adapter
terminal, child release and post-death EOF are absent. Recovery invents none.

Mac debug direct results do not claim Linux/release/sanitizer qualification or
the full source lint gate. The root owns those aggregate checks. The test's
independent native-field decoder checks the source records directly; it does
not certify the production reader/body decoder.

## Later owners in this group

Rows33–34: assembly/format failures, retry selection, configured wait enter/leave,
cancel/refuse and each actual attempt remain the coding owner. Rows35–38 include
authorization selection/refresh states, request transport, decoder/preview,
original/result retention and acceptance. Existing1200–1202 are partial coverage;
they do not substitute for the new adapter body/read facts. Auth state events
copy revision/action/disposition only, with no private token response bodies.

Rows39–40: colleague prepared/admitted request, selected owned/compatibility/CLI
adapter, upstream read/parse/refusal and wrapper result retain their own existing
attempt/profile/request identity. Row46 covers account/sign-in owner transitions,
including device/browser callbacks, selected-account conflict and refresh failure;
private protocol bodies remain excluded. Rows47–48: file reads/writes/replace stages
need file incarnation/range/native return and before-effect original admission;
the child helper does not complete those owners. Rows51–52 and54: Beads,
decision-model and Git owners supply operation/invocation linkage, exact existing
request and stream originals, native child state, and mutation-possible/validation
disposition. The decision adapter currently calls its request observer *after*
send; the new raw body hook must be before send, independent of that business
observer. Those semantic owners each require a concrete successor subplan rather
than being smuggled into this child vertical.
