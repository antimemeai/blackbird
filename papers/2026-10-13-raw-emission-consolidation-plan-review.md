# Raw emission consolidation: one concrete plan attack

Read the proposed consolidation and actual RawCallerTrace, RawProgramCall,
RawBeadsTrace, RawBridgeTrace/ModuleToken and File ToolTrace/Helper/Call/Resource
implementations. This is one bounded plan attack, with no production changes,
build or broader surface study. Shared raw_emit/channel/recorder remain unchanged.

E1: preserve admission before request. Beads run/invoke first admit their whole
CONTROL bank, then status DATA60, then publish the request. A combined
claim-and-publish start alone would publish before a failed status acquisition.
Provide prepare(amount, admission), then begin with complete request fields;
start may combine them for other callers. File DATA requests similarly retain
their distinct preheld request/return/original banks and original ordering.

E2: publication must report failure rather than impose throwing. File Call and
Resource first take actual acquired fd/name custody, retain the first capture
Error without throwing, complete their held close/unlink, then expose failure at
the owned check boundary. Shared raw_publish/raw_publish_linked should return
Result<RawEventRef> noexcept, preserve errno and existing fence behavior. Owners
choose immediate throw, advisory no-throw, or deferred native cleanup failure.

E3: close takes the complete owner-native body. File uses RawFileBranch0–6,
filesystem native int, and no common stage/Error-present fields. Beads has
variant-specific order, parse Result failure and native status/child fields.
Common branch/stage/Error construction is a convenience only for owners whose
existing wire actually has that layout. No common terminal schema is imposed.

E4: preserve field capacities. File direct spans currently support32 parts;
Beads fields support24. Do not inherit Caller's16-part cap. Shared direct
publication accepts the existing span; linked publication can use fixed32 parts
including its two parent parts. Checked size includes E16 and all actual spans,
with no payload traversal, allocation, encoding or projection.

E5: prepared credit does not imply a published request. A status-grant/begin
failure must release unused credit without an end linked to ordinal0. Keep
RawEmission destruction release-only, with surface-specific unfinished bodies
at their real owners. close consumes the active request once even when its native
publication fails; never replay a failed terminal or make a second native effect.

E6: one exception runner must retain real surface classification. File catches
filesystem_error before standard and emits its owned error_code int; File's
length_error is standard. Caller/Program/Beads separately classify length and
otherwise classify filesystem_error as standard. Support these choices through
compile-time owner methods/traits, not runtime callbacks or another per-surface
try/catch ladder. Use invoke_result_t<F> as the declared return type: preserve
references/noncopyable values, normal value NRVO and explicit forwarding for an
actual T&& result. Avoid a dangling decltype(auto) reference to a local value.

E7: guard errno during admission/validation as well as publication. Finite grant
refusal records actual capture origin at its existing selected owner. Shared
machinery must not automatically rebind a retained producer during close or
destruction: Bridge's serialized token owner and other crossing-lifetime owners
provide the actual exclusive binding. Holding a handle alone is not permission
to write another thread's producer. Existing native cleanup remains responsible
for its own ordering and complete original bytes.

Root accepted E1–E4 with split prepare/begin, Result publication and literal
complete close. E5–E7 are the remaining concrete API/lifetime constraints, sent
before source implementation. Existing native File/Beads/Program/Bridge owner
fixtures are the affected migration oracles. Fix any resulting direct finding
within this same unit; no helper-only certification, second plan panel or
workload/performance qualification.
