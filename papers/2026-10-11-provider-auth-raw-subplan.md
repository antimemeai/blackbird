# Provider authorization and account emission

2026-10-11. Reviewed unit for census35/46; source implementation is released. The
root owns one plan attack, resolution, release, and the later one code/oracle
attack. Generalized protected data/control claims and split are now implemented;
this plan uses them rather than proposing another capacity mechanism.

## Source and reference grounding

Read the complete Store/save/save_account/token_record and public methods in
src/provider_auth.cpp; AuthSignals, Callback, browser_login and provider_auth_cli
in src/provider_oauth.cpp; codex_login/openai_http in src/openai.cpp; their owning
tests; RawRuntime open/prepare/retirement and native directory/identity/CRC code.
The already retained raw studies supply the recorder separation. For this unit
read skalibs2.15.1.0 selfpipe.c (portable pipe implementation, setup masking,
nonblocking CLOEXEC endpoints, errno preservation, handler restoration before
close), qmail1.03/daemontools0.76 signal blocking/catch source, and the existing
Blackbird main handler/owned spawn mutex. These are study inputs, not libraries.

Store takes store_mutex before opening the credential directory and flocking.
Its business flock loop invokes config.cancelled while holding that mutex. Private
refresh/device HTTP also occurs under Store. No raw admission occurs under these
locks. Existing business polling/sleep behavior is outside this emission change;
no instrumentation timer is introduced.

Save encodes the private packet, opens an exclusive temporary file, writes,
file-syncs, renames, then directory-syncs. Failure after rename leaves the replaced
file in place. Instrument the final save owner operation, including encode/open
failures and the reached stage; do not advertise every native retry/close.

Account and selection revisions already belong to the product. save_account
increments both, use changes selection, logout changes both, refresh preserves
account revision. Custom descriptors have no configuration revision. Do not
invent one. Ordinary request without expected_binding does not acquire an
account revision. A hook must not acquire it merely for metrics.

## Scope, identity, and original fields

Select owner entry/outcome for every public auth method, plus owned authorization
resolution, compatibility Codex resolution, the browser flow and standalone
mutation command. Metadata methods do not serialize their Value result. Their
hooks are active only when a caller already bound a lane; standalone status and
providers do not set up capture. Status/catalog select their whole owner return,
not each account projection. Registration selects its two existing native revision
acquisitions; selected selects its existing native boolean; binding additionally
copies its one existing nonsecret result's string fields and acquired revision.

An action's identity is the actual begin record's lane16/ordinal8 Ref. Internal
browser descriptor/registration/save_oauth operations, saves, private requests,
and callbacks link to that actual parent Ref. Independent public calls have no
parent. Explicit internal helper arguments transfer the actual parent and prepaid
child claims; no TLS ordinal guessing, dictionary, counter, callback registry or
new aggregate. The standalone command supplies its actual parent to its public
operation; browser then supplies its own parent to its finite children. Device
CLI polls are separate actions, each parented to the command. The command does
not prepay an unbounded polling loop.

Owned request resolution ends after Store releases and secret(token) succeeds,
before application-body construction/transport. Its terminal means authorization
resolved, not that the later provider request succeeded. Retain that actual Ref
through the actual application call. Compatibility OpenAiLogin retains its actual
resolution Ref by value (24 native bytes); it retains no capacity claim or raw
producer. At actual RawProviderTrace construction, prepay the additional64-byte
link and publish it with the actual1300 request Ref. No1300 layout changes and
no link to an injected HTTP function that emitted no application request.

Copy only already acquired native fields. Naming a business expression's result
in a local and using it for the original comparison/assignment does not add a
parse. Preserve existing short-circuit evaluation and callback order. No hook
calls Number.text(), number(), dump_json(), a packet encoder, getenv(), a clock,
or a field traversal. Number.text() allocates/formats; the earlier draft's claim
that it offered a representation to copy was wrong. Binding string snapshots use
the exact existing locals subsequently used to build the business Value, not
another lookup/projection. Snapshot publication precedes allocation of that Value.

Exclude keys, access/refresh/id tokens, credential file/packet bodies, auth-file
bodies, private HTTP/RPC bodies/headers/config/URLs, environment VALUES, browser
URL/query/state/nonce/verifier/code, device/user codes, provider claims/account IDs,
client IDs, JWT claims and metadata. No scanner/redactor/hash attempts to sanitize
them. Caller provider/local-account labels and the public binding descriptor are
nonsecret fields. add_provider's offered base URL is copied only after the existing
https_url validation passes and before its existing trailing-slash mutation;
invalid URLs still produce the action error, not a raw secret-bearing URL record.
Private auth Child objects stay payload-inactive. Semantic private request and
return hooks do not claim private-child native syscall completeness.

## Exact records

Reserve1600–1639. E=raw_envelope_size; R=sizeof(IdentityBytes)+sizeof(uint64_t);
N=sizeof(size_t); U=sizeof(uint8_t); F=sizeof(bool); Q=sizeof(int64_t);
C=sizeof(ErrorCode); I=sizeof(int). Emit individual native fields, not C++ struct
padding. Current qualified64-bit profiles have E16,R24,N8,U1,F1,Q8,C1,I4.
Descriptions publish these fields/branches before activation. All maxima below
include E. Error fields occur only on branches which acquired that Error.

| Site | Existing fields and exact maximum |
| --- | --- |
|1600 action begin|u8 action,bool parent_present,[R],u8 expected_kind (none/native OAuthExpected/binding Value); native expected kind additionally copies the offered two i64. B=E+2U+F+[R]+[2Q]:19/43 normally,35/59 with typed expected values. Binding Value is not parsed here.|
|1601 label input|action R,u8 kind,provider length/bytes,account length/bytes; 57+P+A. Selected-account kind instead has one length/actual active-account bytes:49+A. Before Store for caller labels, immediately after existing active-string acquisition for selected account.|
|1602 binding strings|action R; five length+existing provider/account/kind/base_url/protocol byte spans. E+R+5N+sum(bytes)=80+sum. Revision is the separate native acquisition record, never formatted text.|
|1603 refresh entered|action R,u8 source stage: E+R+U=41. Before the actual private refresh exchange.|
|1604 refresh disposition|action R,u8 actual source branch:41. Branches cover tokens accepted, refresh_unknown, retry-after update, reauthorize; underlying native errors/status are separate request/save returns or the terminal.|
|1605 save begin|action R,u8 mutation:41. Before encode/file effects; Ref names this exact save attempt.|
|1606 save return|action R,save R,u8 last completed stage,u8 return kind,[C,Q on Error]:66/75.|
|1607 source outcome|action R,u8 actual source branch:41. Device poll outcome or owned/Codex resolution source; no parsed string/error normalization.|
|1608 action terminal|action R,u8 branch,u8 reached source stage,[C,Q on Error]:42/51. Returned/Error/bad_alloc/length_error/unknown_exception/unresolved_scope are distinct; caught unknown exceptions explicitly finish then rethrow.|
|1609 private HTTP begin|action R,u8 operation(device begin/poll,refresh,token exchange,JWKS):41; no request data.|
|1610 private HTTP return|action R,HTTP R,u8 branch; actual int status on returned branch, or C,Q on Error:69/74; unknown65.|
|1611 callback/notice boundary|action R,u8 named operation,u8 enter/return/refuse:42 each. No callback input/output.|
|1612 auth/application link|auth resolution R,actual application1300 R:E+2R=64. Actual transport prepays it outside Store.|
|1613 native scalar acquisition|action R,u8 named source field,existing native i64:49. Immediately after the existing expression, before a following callback/allocation.|
|1614 native boolean acquisition|action R,u8 named source field,existing native bool:42. Includes selected and Codex refresh input.|
|1615 private operation begin|action R,u8 operation(init RPC,initialized notification,auth RPC,PKCE,JWT validation):41.|
|1616 private operation return|action R,operation R,u8 branch,[C,Q on Error]:65/74. Whole operation, not every underlying native call.|
|1617 private auth-file begin|action R,u8 before/latest stage:41. No path/body/extent.|
|1618 private auth-file return|action R,read R,u8 branch,[C,Q on Error]:65/74.|
|1619 standalone purpose|size_t length,existing static provider-auth bytes:E+N+13=37. Setup once after actual lane registration/binding.|
|1620 offered public descriptor|action R; four length+existing id/base/protocol/environment-NAME arguments:E+R+4N+sum=72+sum. After existing validation, before Store and trim.|

Native scalar tags describe specific existing expressions, not a generic field
processor. They include resulting account/selection revisions; actually reached
expected/current revision comparisons; token clock/expiry/resulting expires_at;
refresh expiry-check clock, retry-check clock, retry and resulting refresh_after;
device expiry/interval/selection, each distinct existing clock and resulting
expires_at/next_poll/wait. Failed acquisition has no invented scalar. Set the
literal reached-stage before that existing expression so the terminal identifies
its failure. No metric-only arithmetic is added. In particular device_begin's two
now calls and device_poll's repeated next_poll parse remain distinct actual facts.

Save stages are entered, encoded, size-checked, entropy-returned, file-opened,
all-write-returned, file-sync-returned, rename-returned, directory-sync-returned.
Update a stage only after that business operation completes. Thus directory-sync
Error has rename-returned. Catch/rethrow preserves the existing Error already
copied from native errno, even if scratch unlink/close changes errno. Other hooks
preserve caller errno around emission. Unknown exception remains unknown; no
fabricated io Error is placed in absent fields.

## Finite scalar credits

Let K=B+T, with T=E+R+2U+C+Q=51; G=E+R+U+Q=49;
V=E+R+U+F=42; O=E+R+U=41; S=save begin41+maximum return75=116;
H=private begin41+maximum return74=115; J=callback enter42+return42=84.
Use constexpr sizeof arithmetic and checked sums. The table gives maximum held
CONTROL credit from actual finite paths, excluding variable DATA and the later
application transport's separate64-byte link. Parented entry adds24 to an
otherwise unparented action. All returns release unused claim bytes.

| Actual action | Native scalar maximum and finite subordinate work | Root CONTROL maximum |
| --- | --- | ---: |
|descriptor/catalog/status|K; whole metadata owner only|70|
|registration|K+2G; actual account and selection revisions|168|
|selected|K+V; existing result bool|112|
|binding|K+G; existing native revision|119|
|set_key|K+2G+S; resulting account/selection revisions|284|
|use|K+G+S; resulting selection revision|235|
|logout|K+2G+S; resulting account/selection revisions|284|
|add_provider|K+S; no existing config revision|186|
|save_oauth|K(native expected)+7G+S|545|
|device_begin|K+7G+S+H|644|
|device_poll|K+12G+2S+H+O|1046|
|resolve_owned, expected binding present|K+9G+2S+H+O(refresh entered)+2O(disposition/outcome)|981|
|resolve_owned, no expected binding|Same, no two comparison acquisitions|883|
|resolve_codex, default selected probe enabled|K+V(refresh argument)+136(parented selected)+3H(RPC/notification)+2H(file reads)+O|864|
|resolve_codex, selected probe skipped|Same, no selected child136|728|
|browser_login OpenAI|K+94(parented descriptor)+192(parented registration)+520(parented save_oauth, selection expected absent)+2G+2H(HTTP)+3J+2H(PKCE/JWT)|1686|
|browser_login Claude|K+94+192+520+G+H(HTTP)+4J+H(PKCE)|1491|
|standalone mutation command|K; key adds J(input_secret). Other methods/polls admit their own finite claims before their Store. Purpose37 is a separate one-time startup claim.|70, key154|

The save_oauth seven acquisitions are expected selection when checked, expected
prior account when checked, actual token clock/expiry/expires_at and two resulting
save_account revisions. Browser supplies expected account and selection=-1,
so its nested save_oauth has six, not seven: parented K110+6G+S=520.

Device_begin has returned expires_in/interval, existing selection revision, both
clock calls and both resulting timestamp expressions:7. Device_poll's due prefix
has clock, pending/current selection, expiry, next_poll comparison, interval and
new next_poll:7. Slow-down adds interval read, +=5, clamp, its clock and new
next_poll:5. Token success instead adds token clock/expiry/expires_at and resulting
account/selection:5. Too-early has a second next_poll acquisition and its existing
wait subtraction; it does not reach HTTP/save. The maximum is12 and two saves.
Retry429/5xx reaches9 scalar acquisitions and two saves; expiry/conflict reach
one save; no-pending or too-early reach zero.

Owned request reaches two expected-revision acquisitions only when those
short-circuited comparisons actually run; OAuth expiry+clock+retry+clock adds4,
then token clock/expiry/expires_at adds3. Success-save failure can lead to a second
refresh_unknown save. That path has two dispositions and no resolved outcome;
success has one disposition plus resolved outcome. Both consume at most2O.
A failed second save still has its own final native return. HTTP throw/429/non2xx
have their actual private return followed by the selected state save and error.

Codex private init, initialized notification and auth RPC each have distinct
begin/return; before/latest auth-file read likewise. Its default selected probe
can return false before all compatibility work, so including that child is a
real possible path. The private provider account_id is excluded, not relabeled
as the operator's local account. PKCE and JWT hooks cover those whole existing
helper operations; the JWT's internal claim parsing/crypto child is not re-run.

Browser OpenAI has initial notice, one whole Callback::wait, final notice:3J.
Rejected native accepts/retries inside wait are not advertised as individually
selected callback sites. Claude has initial notice, paste notice, read_secret,
final notice:4J; it has no JWKS/JWT stage. OpenAI's existing native expected
revision parse and validation clock account for2G. The default CLI descriptor
which chooses browser/device is a separate already parented action, not folded
into browser's own descriptor. Unbounded cancellation-callback invocations in
Store's business loop are not individually selected notice/callback events.

## Source-bounded DATA and split lifetime

Caller labels are measured existing argument spans. Before business validation
there is no64-byte assumption. Prepay the exact checked57+P+A input. If it cannot
fit the physical data lane, capture refuses before Store/effects and the held
terminal records admission refusal. A failed initial CONTROL admission has no
invented action begin; the established ingest-failure/fence path applies, with
cancellation remaining ordinary interruption. Ancestor scopes can record the
actual refusal with their already held terminal.

The credential packet source cap is S0=1048576. Decoded text cannot simply be
assumed to total S0: packet words can expand from2 bytes to the longest22-byte
word (committer_unix_seconds), and identity strings expand17 to32. The maximum
positive expansion of any one selected string is20. Decoder has no shared-string
reference. Distinct selected text spans consume disjoint encoded bytes. Therefore
one selected active string is at most S0+20; the four store-derived binding strings
(active/kind/base/protocol) jointly are at most S0+4*20. For builtins, base+protocol
are at most30+8 bytes, so two store strings plus those constants total at most
S0+2*20+38, below S0+80. Absent-record constant kind is also below this bound.
Provider is the caller's existing P bytes. This is one joint packet bound, not
four arbitrary1MiB allowances. Keep the codec-derived constants explicit; a
packet dictionary/format change must update this owner bound.

| Actual action | DATA claim before Store, valid-label maximum |
| --- | --- |
|ordinary provider/account action|57+P+A; at most185 after valid64-byte labels|
|descriptor/selected|57+P; at most121|
|catalog/status|0 (no Value result serialization)|
|binding|input57+P plus snapshot80+P+S0+80; total S0+217+2P, at most1048921|
|resolve_owned|input57+P plus selected account49+S0+20; total S0+126+P, at most1048766|
|add_provider|input57+id plus offered descriptor72+id+base+protocol+env; valid max121+2257=2378|
|browser nested finite work|four inputs, browser/descriptor/registration/save_oauth:228+4P+3A; valid max676|
|Codex default probe|two exact openai provider inputs (own and selected):126; skipped probe63|
|standalone command|one caller label input57+P+A (add-provider provider only); children admit their own measured inputs outside Store|

First construct a fully live outcome owner with its held CONTROL bank, without
publishing BEGIN. The constructed owner then publishes BEGIN and performs DATA
admission/input setup inside the caller's guarded operation. A DATA interruption
after1600 explicitly finishes1608 before propagating; never rely on destruction
of a constructor which threw. Claim the entire finite DATA burst before Store. At each actual acquisition use that protected claim directly; no
callback, allocation, lock or fresh admission between acquisition and raw_emit.
Moving a captured business expression into a native local cannot postpone its
publication across a cancellation callback. A snapshot claim stays held through
Store parsing/acquisition and any existing reentrant business cancellation.

One bank is the existing RawCapacityClaim, with no second ledger. Browser splits
its preclaimed CONTROL/DATA for finite descriptor/registration/save_oauth children;
children further split S/H/J settlement credits. Split never admits or changes
aggregate held bytes. Move-only children settle before the parent producer dies;
unspent bytes release normally. Internal saves/http/token_record receive explicit
borrowed owner context, not a new metrics callback. No child raw_claim call under
Store. The CLI command holds its terminal for its lifetime while each independently
bounded poll admits outside its own Store; no claimed count grows with that loop.
Reentrant business callbacks may emit elsewhere but cannot consume these protected
bytes. A callback rebinding a different lane cannot consume the old claim; existing
producer/partition validation refuses it. Borrowed producers and native cancellation
FDs outlive all scoped claims. No mapping retirement while a claim remains live.

## Standalone native environment and launcher

Add a setup-only ProviderAuthConfig metrics_directory setting, overridden when
nonempty; otherwise BLACKBIRD_AUTH_METRICS_HOME when nonempty; otherwise the
resolved auth directory's sibling <auth-basename>.metrics. Thus the default is
$HOME/.local/share/blackbird/auth.metrics. Resolve trailing components at setup,
reject root/empty/dot/dotdot basename, reject a metrics root equal to or below the
auth directory, and check native directory identities/ancestor components when
existing paths could alias. No credential path/account is hashed or emitted.
No credential Store is opened/locked/read before capture registration for a
mutation. Metadata commands create neither this directory nor a raw run.

Open directory components through owned directory FDs with O_DIRECTORY,
O_NOFOLLOW and O_CLOEXEC. Missing owned components are mkdirat0700, re-opened
and fstat-checked. Do not follow existing symlinks or silently chmod unsafe
existing entries. Final metrics root, its environment directory and raw directory
must have effective-user ownership and exact0700; identity file is regular,
effective-user-owned, exact0600, link count1. Ancestors are real nonfollowed
directories with no untrusted write permission, with one precise native exception:
a root-owned sticky directory may be traversed when every later component is
independently opened/checked through its held FD. Other nonsticky untrusted
writable ancestors are refused. Mac private temporary fixtures use /private/tmp;
do not follow the /tmp symlink. Final roots remain owned0700. Do not require /
or /Users to be user-owned0700. Preserve native setup errors before any credential effect.

The durable layout is:

    auth.metrics/                    owned0700
      environment/                   immutable published directory,0700
        identity                     native40-byte header,0600,nlink1
      raw/                           existing RawRuntime runs/recovery,0700
      .environment.<native nonce>/   unpublished setup scratch only

Header offsets, fixed native representation:0..7 magic BBAUTH1 followed by NUL;
8..9 uint16 version1;10..11 uint16 extent40;12..15 uint32 endian marker0x01020304;
16..31 actual EnvironmentId bytes;32..35 native uint32 CRC32C of first32 bytes;
36..39 zero reserved bytes. Use a zero-initialized byte array and individual
native copies, not struct padding. Creation uses getentropy into IdentityBytes
and the actual EnvironmentId::from_bytes validation, the same owned identity
machinery as main. CRC32C is the existing journal implementation, used only at
setup. There is no invented Journal/AuditStream identity for a file with no
journal; no auth-host-id reuse; no generated per-session environment surrogate.
Read requires exact40-byte extent, all fixed fields/CRC/reserved validation and
the typed nonzero EnvironmentId check. Never regenerate on corrupt/incomplete
existing environment. Different native byte order/version fails explicitly;
no automatic migration or writer transform.

On absent environment, create a unique private scratch directory from native
entropy (name formatting only at setup), create its identity O_EXCL|NOFOLLOW|
CLOEXEC, write the40 original bytes completely, fsync file and scratch directory.
Publish with native exclusive directory rename: renameatx_np(...,RENAME_EXCL)
on Mac, renameat2(...,RENAME_NOREPLACE) on Linux. No replace fallback. Existing
empty/incomplete destination also wins exclusivity and is refused on read.
Winner synchronizes metrics-root directory. Loser on EEXIST/ENOTEMPTY removes
only its own unpublished identity/scratch, then reads/validates the winner.
Both return that same actual EnvironmentId. Other rename errors remain errors.
There is no flock wait, polling loop, signal timer or authority registry.

Before publication failure, remove only this invocation's known scratch. A
crashed unpublished scratch is ignored, never adopted/deleted as somebody else's
identity. Failure after exclusive rename/root-directory sync leaves the published
identity; return Error before Store and read that same identity next invocation.
No rollback or replacement. Concurrent readers see either absence or a complete
published directory, never a partially filled identity file. Bound exact error
classification: malformed extent/header => corrupt; missing identity in an
existing environment => incomplete; wrong owner/mode/type/link/symlink => conflict;
native operation failure => io with original errno; entropy zero => typed identity
error. Unsupported exclusive-rename capability fails; never weaken atomicity.

Keep the metrics directory FD alive across RawRuntime and its descendants. Use
its native root setup path (F_GETPATH with before/after identity checks on Mac,
/proc/self/fd/N on Linux) for existing RawRuntime::open. Mac compares the actual held root identity before
and after this path-based setup; Linux addresses the held descriptor through its
native portal. Neither route claims a same-user hostile namespace sandbox. This formatting is setup-only and is
never emitted. Pre-open/create and validate raw relative to that same FD; reject
symlink/non-directory/unsafe ownership. Existing run recovery remains the owned
RawRuntime protocol, not a second auth recovery implementation. The root FD is
CLOEXEC, inherited by recorder/supervisor fork and retained for their path use;
it outlives Runtime close. Runtime's actual run headers use the persisted auth
EnvironmentId and their own existing run/lane IDs. Separate invocations have
separate lanes/runs in this one environment. No cross-run logical clock is claimed.

Classify CLI metadata commands before capture setup. Mutations install signals,
resolve settings/header, open Runtime, prepare and bind one actual lane, emit
purpose from its protected37-byte startup credit, and begin the command before
validation/secret prompt/descriptor lookup/mutation. Invalid mutation syntax and
secret prompt failure get the command's held terminal. Ordinary status/providers
keep their existing metadata business behavior and create no capture run; they
must not gain a hidden environment write. Existing Store(false) lock-file behavior
is not advertised as filesystem-immutable by this instrumentation unit.

Runtime-open or registration failure returns before credential effects. A failure
which prevents the first raw lane cannot be fabricated as a raw event in that
unavailable lane. Report its actual native startup Error, preserve existing raw
prefix/recovery files, and refuse the mutation. This limitation is bootstrap
availability, not a claim that an unstarted action emitted an outcome. Once a
begin is published, held terminals include all acquired errors/unknown exceptions.

## Sticky AuthSignals and cancellation lifetime

AuthSignals owns one nonblocking CLOEXEC self-pipe. Before installing handlers,
block SIGINT/SIGTERM in the owning standalone thread, create both endpoints under
the existing spawn mutex and normalize/check both flags, then release the mutex.
No raw wait while that mutex is held. Install both handlers with both signals in
the handler mask, publish the sig_atomic_t write endpoint, clear the sticky flag,
and restore the prior mask. Any constructor failure restores installed handlers,
unpublishes endpoint, closes only acquired descriptors, and restores the mask.
No detached native endpoint or partially installed handler escapes.

Handler preserves errno, sets the existing sticky sig_atomic_t flag, writes one
byte to the nonblocking endpoint (retry only EINTR), restores errno. Full pipe
means already readable. Nobody drains this pipe. The handler performs no allocation,
metrics emission, callback, formatting or time calculation. Borrowed endpoint
lifetime excludes EBADF/EPIPE; tests attack teardown/FD reuse directly. Handler
mask prevents these two signals from nesting during its publication. Signal count
is not a metric counter.

Borrow read FD through RawWaitControl and inherited RawWaitScope before lane
registration and before every auth action/Store. Merge with any existing endpoint
and earlier deadline; do not replace inherited cancellation. Admission itself
never calls config.cancelled. Existing business callbacks still consult the sticky
flag. No invented auth timeout: only an already owned absolute deadline may narrow
scope. Scope capacity errors are real setup errors before effects.

Use an explicit launcher lifetime: AuthSignals and directory FD outlive Runtime;
setup RawWaitScope surrounds prepare; bound business RawWaitScope surrounds
command/actions and their held cleanup. Finish/release every action/child bank,
then destroy that business cancellation scope, release binding/handle (so ordinary
retirement is not forced to abort by the auth's sticky cancel), then close Runtime,
then close directory, then destroy AuthSignals. During signal teardown block the
two signals, restore previous handlers, unpublish endpoint, close endpoints, then
restore prior mask. No descriptor can be recycled while the auth handler can use
it. Previous handlers/masks are restored, including setup-failure paths. Native
supervisor/recorder inherit the pipe at fork and stop before the final AuthSignals
close; execed private children inherit neither endpoint because CLOEXEC is checked.

## Red oracles, implementation, and one code attack

Write direct oracles before source. Use independent literal native bodies/offsets
and expected bytes; never decode through the production emission helper. Use
private temporary directories, fixed business clocks, fake HTTP/private Codex/
openssl children and native interposition, with unique synthetic sentinels in
all private fields. No real provider, credentials, eval, dependency or timer.
Keep an authentic baseline run showing missing expected records before enabling
hooks; baseline uses baseline public APIs, not disabled-hook candidate behavior.

1. Action/copy: key/use/logout/add-provider/binding/metadata methods have exact
   begin/input/acquired-native/save/terminal references; malformed validation and
   std::runtime_error observer/callback produce actual Error/unknown terminal.
   Binding fields include actual decoded word/identity expansion and a large raw
   string; no Number.text/parser extra call. Clock callback invocation order/count
   equals the business source (two separate device_begin calls). Every distinct
   synthetic token/key/private file/RPC/config/URL/device/JWT/environment-value
   sentinel is absent from every capture partition and disk prefix.
2. Branches: expected-binding conflict before app; ready/environment/no-account/
   logged_out/reauthorize; two actual concurrent refresh callers with one refresh
   effect, unchanged account revision; HTTP throw/429/401/malformed token/success;
   device no-pending/too-early/selection-conflict/expired/pending/slow-down/retry/
   denied/ready, literal native scalars and exact0/1/2 save attempts. Browser
   callback/notice/state/client/account mismatch at actual reached stage, no later
   token/save. Codex before/latest account switch and bad mode/RPC exceptions,
   private operation pairs, selected-shortcut, correct later actual application link.
3. Save cuts: native exclusive-open/write short/failure/file-fsync/rename/directory-
   fsync interposition forwards normal calls. Assert owner save return's literal
   reached stage/native errno. After successful rename then failed dir fsync, inspect
   the real target replacement and error outcome; after pre-rename refusal inspect
   unchanged target. Refresh's first-save failure then refresh_unknown second-save
   outcome is independently expected, including second-save failure masking at
   the existing business boundary. No fabricated rollback/final success.
4. Protected credit: actual stopped recorder/running supervisor/full lane admission
   cut cancels/expires before Store lock/effects. Sticky available-space cancellation
   also refuses. Require the actual1600 begin and exact1608 interrupted/error terminal after
   DATA admission refuses, not just the public exception. Earlier inputs plus held
   terminal remain publishable. An existing
   reentrant business cancellation callback emits aggressively during actual flock
   contention: protected control and bound snapshot DATA survive; no raw claim/poll
   under Store. Split exact counts/remaining bytes are checked against the table,
   including unknown/allocation/early-error paths and failure after rename. Claim
   failures do not change business files/cursors or fabricate a child incarnation.
5. Setup: two actual concurrent processes create/read one literal40-byte native
   environment/header and report identical EnvironmentId; subsequent independent
   mutation run reads it unchanged. Corrupt/short/zero-ID/wrong-endian/reserved/
   CRC/owner/mode/hardlink/symlink/empty-environment fixtures refuse before Store.
   Fail file write/sync/exclusive rename/root sync; verify only own unpublished
   scratch cleanup, preserved published identity after rename, and no credential
   change. Alternate metrics setting/default sibling and held-FD anchoring are
   checked natively. status/providers leave a nonexistent metrics root nonexistent.
6. Launcher/signals: actual standalone binary with synthetic key input emits
   preserved metrics from its registered lane. Send SIGINT/SIGTERM at preclaim,
   queued/full admission, callback and cleanup cuts; literal native interruption,
   sticky readability, preserved errno and held terminal. Check both FD flags,
   constructor second-handler failure rollback, previous mask/handler restoration,
   exec FD absence and descriptor-reuse teardown. Kill a producer after actual
   P>A publication and recover its exact original prefix; no invented terminal.

After the root's single plan attack resolves findings, implement this one owner
unit, qualify owning direct cases and narrow lint (-j1 in build/nls-provider),
then root performs one code/oracle attack. Fix/recheck that set; no extra panel or
review of review. Root owns integrated gates/profiles/commit. This study-only
paper does not claim census35/46 complete and authorizes no auth source yet.

Root's ONE plan attack resolved these two points before source: native root-owned
sticky ancestors with independently checked later components; fully constructed
held-terminal owner before BEGIN, guarded subsequent DATA admission. The direct
full-DATA cancellation oracle requires actual BEGIN+terminal and no Store effect.
Other banks/packet bounds/setup/secret/signal decisions were reviewed in
papers/2026-10-11-provider-auth-raw-plan-attack.md. Source is released after these
resolutions; no second plan panel.

## Native Mac root-path correction during source qualification

The actual Mac native probe opens `/dev/fd/N` for the held directory but rejects
`/dev/fd/N/raw` with ENOENT. Runtime already appends `/raw`. Therefore Mac setup
uses F_GETPATH on the actual held owned0700 root descriptor, then lstat/open of
that returned canonical root with O_DIRECTORY|O_NOFOLLOW and a native
(device,inode,uid,mode) comparison against the held FD. Repeat that comparison
after Runtime::open and before preparing/binding a lane or entering Store. Keep
the original FD alive through Runtime teardown. A mismatch refuses setup and
retains any created originals. No invented descriptor subpath or new Runtime
transport is introduced. Linux retains its working `/proc/self/fd/N` root route.
This path-based Runtime does not promise continuity through later directory
renames and does not claim a same-user hostile namespace sandbox. Direct checks
exercise actual held-root rename before setup, canonical reopen, stale/mismatched
root refusal, and final launcher preservation.

## Source and direct qualification checkpoint

The current-source baseline provider_auth_raw executable genuinely completed
set_key while preserving no required1600 BEGIN; its red output is retained in
context/provider-auth-red.log. The existing qualified standalone binary likewise
created credentials and no durable auth environment (launcher red log). The
candidate helper keeps CONTROL, terminal and DATA banks distinct, and copies
individual already acquired native fields; private exchanges remain payload
inactive. Browser uses its one conservative1686-byte union bank for both routes:
Claude releases the unused195 bytes. No writer Number.text, extra parse/clock,
new callback registry, timer, dependency, payload redaction or private-body opt-in
was added. Store's existing sleeps/polls and private product serialization remain
business behavior.

Native direct oracles now compare the literal seven-record set_key sequence,
post-rename directory-sync ENOSPC with save stage7 and retained actual revision2,
actual stopped-recorder full DATA512/P512/A0 interruption after BEGIN with its
prepaid Error terminal and no Store effect, twelve acquired slow-down scalars,
nine acquired owned-refresh scalars, private unknown HTTP/observer outcomes,
original validated descriptor bytes before trimming and invalid credential URL
exclusion. Synthetic key/token/device/client/JWT/provider account sentinels are
absent from actual preserved bodies. Real fake-child Codex file/RPC request/return
pairs precede the real adapter's exact resolution/application Ref link.

Setup checks exercise immutable native header/reopen, corrupt/unsafe/incomplete
refusal, concurrent exclusive creation sharing the actual identity, root-sync
failure after publication retaining identity, native symlink refusal, and Mac
held-root rename before setup plus stale canonical-root mismatch. Ancestor FD
metadata excludes capture at or beneath the actual credential directory,
including aliases, without credential file reads or Store locking. Launcher
mutation emits purpose/command/child with the actual parent; status/providers
create no Store/capture directory. A native SIGTERM while acquiring a key leaves
the borrowed raw cancellation pipe readable, with native CLOEXEC/NONBLOCK flags,
restores the prior signal handler and preserves the held interrupted terminal.

Focused debug checks provider_auth_raw/provider_auth/provider_oauth/openai_auth
pass. Narrow static analysis covers the changed helper/auth/OAuth/Codex/provider
and direct-fixture translation units. Root owns the single code/oracle attack,
its fixes/recheck and later integrated gates/profiles/commit; this checkpoint does
not claim the code attack or integrated delivery has completed.

Root's ONE code/oracle attack found skipped private-exchange/scalar identities,
with no asserted source defect. The owning fixture now checks complete actual
action/request Refs, native success/Error/unknown variants, each operation tag
and exactly one return per begun private operation. Its affected direct recheck
and narrow helper/fixture lint pass. Disposition is recorded in the existing
provider-auth-raw-code-review paper; integrated delivery remains root-owned.

## Source-gate qualification findings (existing owner attack layer)

The integrated gate exposed two older fixtures incompatible with this reviewed
auth owner. The real auth CLI fixture uses macOS tempfile's `/var/folders`
alias; setup correctly refuses its `/var` symlink with conflict/ENOTDIR before
the key prompt. Resolve the already created fixture root to its native canonical
path; keep production nonfollow checks intact. No credential bytes are involved
in this path resolution. The existing provider raw fixture grants exactly1MiB
DATA, while resolve_owned must hold its credential-packet-derived1048576+126+P
burst before Store. Its direct current request consequently returns capacity:0
before native transport. Grant2MiB only to the fixture lanes exercising auth
resolution, including disk and producer-death variants. Direct native-only
blocked admission lanes stay16384 and retain their original cancellation/death
cuts. No auth bound or production admission is reduced.

Use the unchanged actual CLI/PTY and provider_raw oracles for genuine red and
affected green; apply narrow static checks to the changed provider fixture and
Python syntax check to the fixture script. This fixes qualification findings
within the existing ONE owner attack/fix layer, not a new adapter source unit or
review panel.

After the capacity correction, the unchanged compatibility fixture reaches its
older records.empty assertion after Codex login. The reviewed auth unit correctly
emits payload-free operation metadata there. Replace the obsolete empty-audit
assertion with the actual five private begin/return pairs, exact native Ref/op
fields and secret absence, while forbidding native/public response-body sites.
The existing full Codex oracle in provider_auth_raw still owns the detailed whole
action sequence; this affected compatibility assertion tests its own boundary.

Qualification disposition: authentic current provider_raw capacity:0 and CLI
missing-prompt reds are retained in context/provider-gate-red.log and
provider-auth-cli-gate-red.log. Original CLI/PTY oracle passes0.58s after its
fixture-only canonicalization. Revised provider fixture passes0.57s after
-j1 owning debug rebuild, preserving exact original/cleanup and actual P>A
producer-death checks. Selected private authorization metadata checks the actual
15site sequence and five complete action/request Ref pairs without token/account
payloads. Changed fixture clang-tidy, format/diff and Python syntax checks pass.
No production source, new adapter implementation or extra review panel changed;
root owns integrated gate/rebuild.
