# SystemA representation and codec boundaries (4950–4999)

**Delivery boundary corrected by Root2026-10-13:** standalone SystemA storage
lifetimes and global codecs are outside the latest authorized tool/program/
session/operator delivery. This is a retained discussion proposal, not an
approved implementation plan or a delivery gate. No global Value/radix/ByteBuffer
allocator/deleter, lane rotation, capacity increase or representation-limit change
is implemented. Keep the corresponding census entries open for later operator
discussion. Only actual program-owned conversion observations needed by Bridge
remain in the current Bridge owner; they do not close standalone SystemA.


Proposed separate whole unit after Bridge and disposition of resource4900–4949.
No product source edits, new dependency, encoder, allocation interceptor or
qualification. This is actual native parse/write/convert work: JSON, BBM packet,
Number text/normalization/UTF8, Lua read/push and human presentation. It does not
claim shared-storage cleanup merely because a result was returned.

Read complete current value.cpp220 lines, json.cpp474, packet.cpp449,
presentation.cpp80, their headers and Runtime's existing push_value/read_value.
Read existing format/packet/value/Foundation direct fixtures and original packet
schema/source-compatibility papers. Release-matched Lua5.4.8 lapi/ldo/lbaselib/GC
source grounds protected native callbacks and failure: Bridge already owns the
actual protected push scope; it must not be replaced or bypassed. The storage
native sources and acquisitions in the preceding resource paper are grounding
for lifetime boundaries, not a promise that a codec scope finances final drop.
No extra generic metrics visitors or third representation of a Value is allowed.

## Exact owner integration

A thin RawCodecCall over shared RawEmission supplies literal direction/format,
actual parentR, limits and native state. raw_call_run retains exception branches;
Result-returning wrappers emit their actual Result.has_value and Error code/detail
before their existing business catches return. A selected RawCaptureFailureScope
checks inside and after terminal publication; instrumentation failure never gets
mistaken for an ordinary bad input/capacity fallback. Native Result mapping stays
unchanged: JSON maps length_error to capacity; packet/presentation do not invent
a catch they lack. Default context opts out. Parent is actual bridge/operation/
provider/retained source owner, explicitly supplied synchronously, not an ambient
borrow escaping in a result. Nested scopes reuse the same immutable real owner
and optional original source range; no producer/registry per codec call.

JSON parse/projection: actual Parser text/limits/omitted,pos,nodes,depth,retain/
keep. Capture input once before construction; omitted names are their existing
span elements, each native length/body, no joining or reencoding. Record the
actual keep local used for business branching; discarded values remain fully
validated. Actual source-string acquisition and completed parse state occur at
the existing parser, including failure before completion. No added traversal.
Dump: existing Writer nodes,depth,out and variant visit. Prepay exact output
bound48+limits.bytes before existing append work, capture completed acquired out
(or actual partial prefix at error) before destruction/callback/return. Do not
call dump_json/Number::text again to obtain a metric.

BBM encode/decode/projection: actual limits,out,nodes/at and existing native tag,
word_id/string identity branch, count and retain. Observe the actual local branch
used by encoding/decoding; no dictionary lookups run twice. Header/version checks
remain before Decoder construction and have honest no-decoder state on failure.
Input/output bytes are original native BBM2, not a new packet or hex rendering.
encode_packet_string's already-existing vector-to-string copy is distinct from
Encoder; decode_packet_value's actual hex input and acquired decoded vector are
separate native conversion boundaries. Capture exact native progress/partial
bytes before a failed conversion returns. The existing bounds and corrupt/
unsupported distinctions remain unchanged, including discarded projection data.

Number::Number(Decimal) retains its actual normalization loops and native limbs;
Number(double) preserves its real nonfinite predicate. parse_number owns actual
at,negative,scale,decimal,digits/limbs; record those native states only where they
exist, not shadow digits/operation totals. Number::text is the actual existing
business formatter; admit its conservative bound from existing variant/limb size
before calling it, then capture that completed native string once. Decimal text
bound is9*limbs plus sign/point/zero/exponent overhead (exact checked bound must
account for exponent INT64_MIN and zero behavior); numeric int/double bounds
follow existing to_chars buffer size. Existing invalid limbs/exponent failures
are emitted honestly; no replacement numeric formatting is proposed.

valid_utf8 and JSON's own utf8 validator record existing i/count/scalar/minimum and
actual returned bool. Their original string is copied once or linked to an
already captured explicit original range. Observe actual byte classification,
not a second validation pass. Human format_value's existing Writer limits/nodes/
depth/out and actual native escape branch remain intact; capture its actual
result, never render another expected or metric string.

Runtime read_value hooks the existing recursive conversion only: its actual
128-depth/500000-node bounds, native Lua type, marker branch, retained nodes/depth,
integer/numeric/string/table acquisition and actual Error. Protected Bridge
prepare_native continues to own allocating push_value. Push's existing Value
variant visit and actual Lua native status/body are observed at that traversal,
with no newly invented node/depth counter (push has none). Every selected recursive
occurrence has its own entry/outcome and paid terminal; no extra Value/Lua walker.
No C++ automatic raw owner is left across a Lua longjmp: the protected Bridge
frame owns scopes outside the actual pcall; a finite explicit token/pending stack
covers recursive native call state and is cleared by its native return boundary.
Root must approve this exact ownership before source, rather than placing ordinary
RAII destructors inside allocating push_value and claiming protection makes them
safe. Source/status151x and Bridge480x keep their original layout and grants.

## Native sites and exact common fields

E16,R24,size_t8,int4,pointer8,bool1,ErrorCode1/detail8. Complete fields are literal
native spans, no padded structures. Formats: JSON1,BBM2,Number3,UTF8 4,Lua5,
presentation6. Direction/method tags identify actual existing APIs/branches.

|Sites|Native body|Maximum fixed bytes|
|---|---|---|
|4950/4951 codec API|entry:parentPresent bool,[R],u8 format,u8 method,size_t limitBytes,nodes,depth,bool input,[size_t input]; end:common52,bool Result,[ErrorCode,i64],bool state,[size_t cursor,nodes],bool output,[size_t bytes]|entry76; end90|
|4952 input/4953 output|codecR,size_t N,actual original bytes|48+N DATA|
|4954 omitted field|codecR,size_t actual index,N,original name|56+N DATA|
|4955/4956 existing item traversal|entry:codecR,u8 format,size_t actual nodes,bool depth,[size_t depth],u8 actual tag,bool retain; end:common52,bool cursor,[size_t cursor],bool result,[u8 actual tag]|entry61; end63|
|4957 actual retain/dictionary/identity branch|itemR,u8 kind,bool actual predicate,bool native value,[size_t value]|51 max|
|4958 actual progress|itemR,size_t existing cursor,nodes,bool depth,[size_t depth]|65 max|
|4960/4961 native number/UTF8 helper|entry:parentPresent,[R],u8 method,bool input,[size_t N],bool source,[pointer]; end:common52,bool nativeBool,[bool],bool nativeState,[size_t index,count,uint32 scalar,minimum],bool output,[size_t bytes]|entry60; end87|
|4962 existing native numeric fields|helperR,u8 actual variant,bool scalar,[native actual int64/uint64/double],bool decimal,[bool sign,int64 exponent,size_t limbs,native uint32 limb span]|checked DATA from actual span|
|4963/4964 actual Lua conversion|entry:bridgeR,u8 direction,int actual native type,bool source,[pointer],bool depth,[size_t],bool nodes,[size_t]; end:common52,bool returned,[int actual native type,pointer]|entry71; end65|
|4965 existing Lua borrowed string|conversionR,size_t N,actual STRING bytes|48+N DATA|
|4966 native container branch|conversionR,u8 kind,bool marker,bool count,[size_t native size],bool key,[int native Lua type]|56 max|
|4967 acquired error/partial prefix|ownerR,u8 role,size_t N,actual already-completed bytes|49+N DATA|

API CONTROL166 plus exact declared fixed conditional sites before acquisition;
item124 plus one51 predicate and65 progress=240 per live recursive frame. This
bank is released per actual return, so it does not retain one bank per completed
node. Helper147 plus exact known branch sites; Lua136 plus56 branch=192 per live
frame. Do not imply raw field availability at an unreached constructor/cursor.
Original input grant uses its actual length; output grants use existing maintained
bounds before mutation. Nested number/UTF8 strings can link the actual source
range supplied by the owner instead of duplicating it. Maximum arithmetic checks
UINT32_MAX, not a new business cap. Real captures publish before callbacks,
parsing another body or returning success, including actual partial/error prefixes.

## Independent finite checks and sequence

Baseline actual native JSON/BBM roundtrip, exact Decimal text, human presentation
and Lua conversion succeed with raw binding while preserved disk lacks4950/4963.
Fail on that missing observation before product coding. Expected bytes are literal
independent JSON/BBM fixtures and known native scalars, never generated from a
production encoder/descriptions. Real API outputs/pinned pointers/host effects
are checked directly. One plan attack, source, direct greens, one code/oracle
attack and finite fixes/recheck; no extra helper panel or qualification.

Finite cases: strict JSON syntax/duplicate key/trailing/UTF8/escape/projection
validation; BBM2 version/tag/varint/word/identity/Decimal/real/count/omitted data;
128/129 depth and actual node-bound refusal without new traversal; binary strings
and exact formatter output; positive/negative zero and exponent/limb extremes;
actual existing partial output on capacity/allocator rejection; vector-to-string
and hex conversion allocation cuts; all native exception/Result branches;
DATA grant−1 before parse/output acquisition and fullCONTROL/sticky cancellation
with paid frame terminals; real Lua table markers/integer/string/failure and
finalizer reentry through existing protected Bridge shim. No invented pushes or
compiler-only red. Source-original linkage must match the real parent and logical
payload range; pointer/status/node fields match actual reached native locals.
Resource final drop remains checked by the separate resource owner, not this
codec fixture. No account/provider calls, broad campaign or new library.

Files: thin codec adapter in raw_representation.hpp/cpp over shared machinery;
confined value.cpp/json.cpp/packet.cpp/presentation.cpp and Runtime read/push;
header optional explicit owner contexts; dedicated real-native codec fixture.
Root owns common CMake/descriptions and actual caller propagation. Final exact
recursive Lua pending-token and Number text grant derivation are required design
items before approval, not permission to improvise a native longjmp owner.
