# Provider authorization and account emission

2026-10-11. Study-only successor to the provider/native-child vertical. Covers
product census35 and46. This is proposed owner design, not approved source work.
The completed vertical does not imply these owners are instrumented.

## Actual source boundaries

Read provider_auth.cpp's Store, save_account, metadata methods, set_key/use/logout,
add_provider, save_oauth, device_begin/device_poll and request; provider_oauth.cpp's
browser_login/callback and dedicated CLI; openai.cpp's codex_login; and the owning
provider_auth/provider_oauth tests. The settled qmail/daemontools/s6 capture and
recording studies remain the mechanism reference. No new dependency or literature
campaign is needed.

Store acquires a process mutex before opening/locking the credential store. Its
flock contention loop calls the business cancellation callback while that mutex
is held. Refresh and device HTTP exchanges also happen while Store is held.
Store::save can encode, write, fsync, rename and fsync the directory; failure after
rename can leave a replaced credential file even when the owner returns Error.
An emission wait under these locks is forbidden. A capacity check before Store
construction is insufficient because the business callback can emit on this lane.

Existing account revision and selection_revision are business state. save_account
increments both; use increments selection_revision; logout changes both; refresh
currently preserves the account revision. Preserve these actual semantics, do
not invent a refresh revision or a provider-configuration revision. Providers have
descriptors, not an existing revision field. Export their exact nonsecret descriptor
fields only at an explicitly prepaid snapshot site.

Authorization has two paths. Owned authorization selects the active account or
environment, may refresh, and then starts application transport. Compatibility
codex_login pins the account from the private auth file, invokes private Codex
RPC, rereads the file and refuses a concurrent account/token change. Token bytes,
auth-file/packet bodies, environment values, RPC replies, OAuth request/reply
bodies, browser URLs containing nonce/state, callback queries/codes, verifier,
device_code, user_code, JWT claims and metadata are excluded from these hooks.
No scanner, redactor, hashing or serialization attempts to make them acceptable.

Both blackbird auth and blackbird-auth enter provider_auth_cli without starting
RawRuntime or binding a lane. Calling new hooks there would currently emit nothing.
Row46 cannot be declared complete without qualifying that actual launcher.

## Identity and selected facts

Each actual auth action begins with a raw record. Its lane_epoch/ordinal Ref is
the action incarnation. Nested save_oauth/publication/HTTP actions carry the exact
parent Ref. request obtains a separate auth-resolution Ref, linked to its actual
application request Ref. Compatibility OpenAiLogin can retain the published
authorization Ref for the later transport. There is no nearest-ordinal inference,
new global counter, clock-derived identifier or dictionary.

Caller provider/account labels are the nonsecret identity input. Emit their exact
existing bytes before Store acquisition, after admitting their measured size.
Existing provider/attempt/invocation/generation context may be supplied explicitly
at the current owner boundary; absence means absent, not a guessed owner.

Native scalars already computed by business expressions may be named locals and
copied immediately: prior/resulting account or selection revision, expected
revision, HTTP status, current business clock, expiry/retry/poll values, native
ErrorCode/detail and actual selected branch. Moving a business expression into a
local does not add another parse. No metrics-only number(), dump_json(), packet
encoding, duration calculation, field traversal or population counter is added.
Fields that the business path did not acquire are absent in that event. Existing
Number.text() or string spans are copied as their existing representation where
a snapshot is needed; consumers may parse them after disk preservation.

In particular, the ordinary request with no expected_binding does not currently
parse its account revision. Its action result must not claim to carry that native
revision. The separately selected binding snapshot can copy the existing native
i64 used to construct that result and exact provider/account/base_url/protocol
fields. Calling Number.text() for the hook would add formatting and is excluded.

## Proposed byte layouts

Reserve1600–1639 after checking the actual setup description registry. R=24,
E=16. Scalars use their declared native representation. Presence flags describe
fields actually acquired; they are not substitutes for failed acquisition events.

| Site | Exact body / selected boundary |
| --- | --- |
|1600 action begin|u8 action; bool parent_present; parent Ref when present; bool expected_present; when present i64 expected_account_revision,i64 expected_selection_revision. Before validation/Store/effects, after terminal and finite burst credit.|
|1601 nonsecret action input|action Ref; size_t provider_length,existing provider bytes; size_t account_length,existing account bytes. Before Store, with no key/token/body fields.|
|1602 binding snapshot|action Ref; length+existing representation for provider,account,kind,base_url,protocol; bool native_revision_present,[i64 already acquired revision]. Only the explicitly selected existing binding result; no metrics-only number parse/format or credential projection.|
|1603 refresh begin|action Ref; existing native i64 expiry/clock/refresh_after fields with individual presence flags. At the existing refresh branch, before the private exchange.|
|1604 refresh disposition|action Ref; u8 actual branch; optional actual HTTP status; optional exact ErrorCode/i64 detail. After the existing response/parse/state branch; no token fields.|
|1605 publication begin|action Ref; u8 mutation action; bool native_account_revision_present,[i64 revision]; bool native_selection_revision_present,[i64 revision]. Before the selected Store::save operation.|
|1606 publication return|action Ref; publication-begin Ref; u8 reached_stage; u8 return_kind; on Error exact ErrorCode/i64 detail. Final owner-operation result, not every underlying write retry. Explicit stages distinguish before/after actual rename.|
|1607 state disposition|action Ref; u8 source_branch. Exact existing ready/pending/expired/denied/reauthorize/refresh_unknown/rate-refusal branch, at its owner site. No derived error string.|
|1608 action terminal|action Ref; u8 returned/Error/allocation/capacity/unknown_exception/unresolved_scope; u8 source_stage; ErrorCode/i64 detail. Held credit, including throws and validation failures.|
|1609 private HTTP request begin|action Ref; u8 operation(token_exchange,refresh,device_begin,device_poll,jwks). Before the actual private request, with no URL/header/config/body bytes.|
|1610 private HTTP return|action Ref; private-request Ref; u8 return_kind; actual int status when returned, exact ErrorCode/i64 detail on Error, no fields on unknown exception. Distinct event from request begin.|
|1611 callback/notice boundary|action Ref; u8 named source_stage; u8 entered/returned/refused. Existing operator callback or browser acceptance boundary; no callback input/output bytes.|
|1612 auth/application linkage|auth-resolution Ref; actual application-request Ref. Produced at that actual transport begin, using explicit typed context. Does not change sites1300–1303's existing body layouts.|

Source action tags name actual methods: resolve_owned,resolve_codex,binding,
set_key,use,logout,add_provider,save_oauth,device_begin,device_poll,browser_login.
The returned callback stage includes actual refusal before any later token effect.
Unknown exceptions have explicit branches and are rethrown with existing behavior.
Descriptions publish these branches and widths before activation.

The snapshot is specifically the existing nonsecret binding result, not arbitrary
Value serialization. Input descriptors for add_provider contain only the existing
id/base/protocol/environment-name arguments; the environment's value is excluded.
The separate private Child remains payload-inactive. Shared file/child syscall
coverage belongs to its own owner unit; this semantic unit does not claim it.

## Prepaid bursts and the concrete missing primitive

Each scope prepays terminal51 and its complete finite scalar burst before Store.
Each publication holds its begin/return together before its effect. No metrics
claim or space/RPC wait occurs inside Store or between native acquisition and its
publication. Observer/cancellation callbacks may use other claims; they cannot
consume this scope's held capacity.

The source's maximum save counts are concrete: set_key/use/logout/add_provider/
save_oauth/device_begin each1; device_poll at most2 (next_poll plus the subsequent
pending/denied/ready state); owned request refresh at most2 (the successful save
can fail and the catch then attempts refresh_unknown). Browser_login has one
nested save_oauth and at most2 private HTTP exchanges (token and JWKS); each
CLI device poll is a separate scope, not an unbounded prepaid outer loop.
Compatibility resolution has bounded private init/auth stages and two auth-file
reads; only semantic results are selected here.

These counts determine the helper's sizeof-based scalar credit separately for
each action; do not choose an arbitrary large event count. Only acquired branches
consume credit. Ordinary return releases unused credit. A failed save consumes
its actual return record even if a later save is attempted.

Variable nonsecret binding fields expose a real design gap. Their existing decoded
representations are bounded by the1MiB credential-store packet, but corrupted
string/Number fields cannot be assumed to fit64-byte labels or20-byte decimals.
The current RawControlToken only protects control bytes. raw_wait_data alone
does not protect a future snapshot through reentrant cancellation callbacks.
Increasing the256KiB control default merely to fit credential-store-sized label
fields is not proposed.

Propose extending the existing owned move-only claim to admit data as well as
control, with per-partition held capacity, owner/lane validation and unused-credit
release. Reuse the existing cancel/deadline space wait. Emit consumes that held
claim with no allocation, callback, lock, transformation or new wait. The initial
claim covers the source-bounded nonsecret burst before Store; only actual bytes
are copied. This is a small missing transport ownership capability, not a new
metrics framework. Its source-before-green oracles must attack reentrant ordinary
emission, multiple claims, overflow/fence, unused release and fixed credit after
cancellation. Review this primitive and exact action burst arithmetic before auth
implementation. A plain capacity check or post-acquisition wait does not qualify.

## Launcher and cancellation decisions required

Dedicated auth processes need their own setup-only capture run and lane, outside
the credential Store, with an explicit auth scope identity and separate metrics
directory. There is currently no session EnvironmentId at this entry point. Do
not synthesize one by hashing a credential path/account, borrowing an unrelated
session or treating an absent binding as a run. Specify native identity creation,
directory ownership, read-only metadata command behavior and process startup/
retirement before wiring the launcher. This remains an explicit design dependency.

The root resolved its direction: one durable auth-audit environment outside
credential Store, with an owned native binary identity/header and a generated
EnvironmentId created once during setup and read on later mutations. Require
strict owner/permissions and atomic creation. A setup-specific metrics-directory
setting defaults alongside private auth state; credential paths/bodies are not
captured. Mutation commands open RawRuntime there and bind their own lane before
effects. status/providers remain read-only and create no run. Exact header layout,
concurrent creation, error handling and launcher native lifetime still need the
concrete setup subplan before source.

Inherited RawWaitControl handles metrics admission once the owner is bound.
Current provider cancellation callbacks still govern their business operations;
metrics must not invoke them. If actual signal/UI cancellation is to wake admission,
wire its existing sticky native endpoint at the launcher/owner scope. A boolean
callback does not itself provide an FD. Keep this cancellation integration
separate from changing Store's existing timed business lock loop. No new periodic
instrumentation timer.

The root selected a sticky native self-pipe in AuthSignals, borrowed by every
outer scope before Store. The signal handler preserves errno and performs only
the native sticky notification; a full pipe is already readable. That setup
resolution does not authorize new metrics-driven signal timers.

## Independent direct oracles

Use the existing private temporary credential store, deterministic business clock,
fake HTTP transport and fake Codex child. No real accounts/network/tokens. Compare
literal expected native records independently; never use the emitting helper's
decoder. Supply unique synthetic private sentinels and require their absence from
all metrics partitions and preserved disk events.

| Actual cut | Expected result |
| --- | --- |
|Set key/select/logout|Exact input labels; native revision/selection values where actually computed; one publication begin/return; terminal; sentinel key absent. Logout's selected label and no environment fallback remain unchanged.|
|Expected binding conflict|Actual conflicting owner branch/Error before application transport; no private request or application body; held terminal survives full ordinary lane.|
|Owned ready/environment/expired account|Exact selected source branch; no invented revision read; environment value absent; logged_out/reauthorize refuses fallback.|
|Concurrent rotating refresh|Two actual callers, one real refresh effect under the existing lock, each caller's distinct auth incarnation; actual refreshed revision remains unchanged; no writer-side refresh counter.|
|Refresh transport throw/429/401/invalid token/success|Distinct request/return; exact actual HTTP/native Error; refresh_unknown, retry or reauthorize publication as source dictates; application send only after ready. No token/config bytes.|
|Device too early/expired/selection conflict/pending/slow_down/denied/ready|Existing native poll/clock/interval values and actual branches, exact0/1/2 saves as the source path dictates; private codes/replies absent.|
|Browser callback/notice throws, state/client/account mismatch|Exact callback/validation stage terminal; no later token/save effect; no URL/query/state/code/verifier/claims bytes.|
|Store write/fsync/rename/directory-sync refusal|Actual final publication stage and Error; distinguish failure before rename from a replaced target followed by failure. No invented rollback/success.|
|Codex auth switch/unsupported/malformed/transport failure|Exact resolution branch and actual Error, no raw auth-file/RPC payload; later application request has the correct explicit auth Ref only on success.|
|Recorder stopped and ordinary lane full before Store|Admission cancels/expires before lock/effects; earlier emitted input survives; held finite outcome returns without recorder capacity.|
|Reentrant cancellation callback while Store held|An independently emitting callback cannot consume protected auth snapshot/result credit; no new claim/wait under Store.|
|Actual standalone auth launcher|A real private synthetic action emits preserved events from its own registered lane; preexisting secret store unchanged on startup failure; private data absent; producer death preserves emitted originals and fabricates no action terminal.|

The generalized data claim and dedicated launcher are concrete prerequisites,
not finished capabilities. Resolve them, finish action-by-action burst sizes and
review the plan before source. This paper does not advance census35/46 completion.
