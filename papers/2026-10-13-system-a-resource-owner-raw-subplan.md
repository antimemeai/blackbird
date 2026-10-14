# SystemA owned storage lifetimes (4900–4949)

**Delivery boundary corrected by Root2026-10-13:** standalone SystemA storage
lifetimes and global codecs are outside the latest authorized tool/program/
session/operator delivery. This is a retained discussion proposal, not an
approved implementation plan or a delivery gate. No global Value/radix/ByteBuffer
allocator/deleter, lane rotation, capacity increase or representation-limit change
is implemented. Keep the corresponding census entries open for later operator
discussion. Only actual program-owned conversion observations needed by Bridge
remain in the current Bridge owner; they do not close standalone SystemA.


Proposed whole unit after Bridge, before codec/representation4950–4999 and
candidate3900–3999. No product source changes or qualification. This unit owns
actual Value shared storage, persistent radix nodes/leaves and ByteBuffer vector
storage. A completed synchronous Bridge/codec request does not close an allocation
still shared by returned data. Creating-binding exit and final drop on another
thread are required lifetime cases, including ByteBuffer.

Read actual value.hpp44–115, shared_sequence.hpp15–100,
foundation.hpp243–253/foundation.cpp82–94, and the SystemA census. Read the earlier
waste audit and retained-index lifetime design; their resource boundaries do not
already implement these allocations. Read release-matched installed LLVM23.1.2
libc++ shared_ptr/shared_count/vector sources, preserved intact with hashes and
restoration URLs in 2026-10-13-representation-native-acquisitions.json. The actual
compiler include-search log confirms those headers are used. shared_count64–69
runs zero-strong cleanup on the dropping thread. shared_ptr109–118 distinguishes
pointee destruction from control-block deallocation;142–150 gives allocate_shared
allocator destroy/deallocate,650–668 uses one rebound allocation. vector242–261
clears then frees its actual buffer; reserve/copy may allocate another buffer.
No library adoption, global allocator interceptor or dependency is proposed.
The instructed Rhizome papers paths are unavailable in this workspace; no new
findings are inferred from that missing watch.

## Selection and ownership

Use shared RawEmission/publication/classification. Add a confined resource
allocator/loan only for the three existing owned storage types, not a host-wide
allocator framework. A synchronous explicit RawRepresentationScope supplies an
immutable optional actual parentR and a shared lazy resource issuer to existing
construction paths. It opts out without a selected actual scope. The same issuer
is reused by nested representation/codec scopes and retained in every real loan;
no producer per allocation, event, copy or leaf. Preparing the issuer occurs once,
outside its mutex. Its mutex serializes only finite admission and immediate held
publication, with the actual retained handle bound. Native allocation/destruction/
deallocation runs OUTSIDE that mutex, including recursive child final drops.
No borrowed scope, Runtime pointer or later TLS producer escapes in a loan.

Each allocation prepays its request/return and both real destroy/deallocate pairs
before calling the existing allocator. The actual returned pointer belongs to
that allocationR, including a shared control block whose pointee can die earlier.
Failed allocation releases unused cleanup without fabricating a release. Failed
pointee construction can deallocate a successfully allocated block without a
pointee-destroy event. Cleanup emits only actual reached native callbacks. Its
credit/issuer lives until the actual final native deallocation, even when original
scope/binding/runtime objects have ended. Fatal loss of live capture custody follows
existing resource-owner policy; ordinary selected capture failure is explicitly
marked and exposed at the caller's safe boundary, never silently dropped.

Use allocate_shared with a confined rebound allocator for Value storage and radix
Node so the existing single shared-control-block allocation remains single. Its
actual requested n/sizeof(T), pointer and native allocator error are captured at
the real allocate/deallocate callbacks. A destroy hook exists only for the owned
Value-storage/Node object, around its actual standard destruction; do not introduce
per-byte or per-child destruction loops. Existing recursive vector/array/member
cleanup performs its own work. ByteBuffer/leaf buffer allocators expose only real
buffer allocate/deallocate; they must not force trivial-element destroy loops.
Allocator copy/rebind carries immutable origin/issuer, not a copied active bank.
Each actual new allocation gets a distinct loan. Copy-assignment allocator traits
must retain existing std::allocator reuse/propagation behavior; noexcept moves
and swaps keep native buffer/loan ownership without new allocation or terminal.
No changed bytes, artificial copy, new capacity limit or forced detach is allowed.

Value storage retains private immutable allocation origin alongside its Storage;
public value()/string()/array()/object() still return the original business types.
The implementation must avoid a second heap allocation for every Value merely to
attach a deleter. A selected allocator's native lifetime metadata is intentional
resident state; record actual sizeof changes and allocations in the implementation
report, without a workload benchmark or claims of zero overhead. Unselected paths
retain their current allocation/behavior. The loan metadata is raw native state,
never a Value, so instrumentation cannot recurse through its own constructor.

## Actual source sites and unchanged branches

Value constructors each allocate the actual existing Storage variant. Capture the
existing variant index and fixed scalar/string/Decimal fields directly, before
ownership transfer; Array/Object record only their already-known size/capacity/
data pointer, never a new walk. A supplied std::string and ByteBuffer input have
an existing body: prepay48+N and emit that body before the existing copy/allocation.
Decimal limbs are their existing contiguous native uint32 span, not a new encoder.
Exposed-copy uses the actual other.exposed_ branch: false shares the same block,
true allocates a clone. Copy assignment keeps its existing temporary+swap strong
exception behavior. write() reads use_count once into the business local, emits
that actual native long and detach predicate, then performs the existing clone
if required and sets exposed_ after success. Do not add another use_count read.
Move/swap transfers existing identity; destruction is the allocator's real final
callback, not every Value handle destructor or an assumed count decrement.

Radix own() captures its existing null/actual-use_count/clone branch once. The
initial node, copied path node, root growth and leaf reserve32 are distinct reached
sites. Record source node pointer, actual levels/size/digit where already computed,
returned node pointer and actual values.size()/capacity()/data() at reserve/copy.
No extra traversal or changed width32. SharedSequence copy pins the original root;
clear/pop/root replacement release through actual allocator lifetimes. The no-op
reserve remains a no-op, not a fabricated allocation. Root/leaf identities must
remain distinct and every old pinned reader must see unchanged values.

ByteBuffer::copy retains its bytes.size()>limit and vector.max_size() refusal,
actual data.assign and bad_alloc Result mapping. Default vector copy and assignment
are independently allocated/reused native buffers, never falsely shared. Input
bytes including zero/NUL/high bytes are exact originals. An empty buffer has no
fabricated allocation/free. view() retains its existing lvalue-only contract.
Cold copy/move/final-drop uses the retained immutable creating issuer, not a current
scope claim; buffer reuse must retain the loan of the actual reused allocation.

## Literal native fields and grants

E16,R24,int4,size_t8,pointer8,native long8,bool1,ErrorCode1/detail8 on qualified
profiles; use individual native spans, no padded struct bytes. Shared common
outcome is52 max. Bodies below have explicit method/kind tags and actual optional
flags, no pointer-derived source contents or invented allocation totals.

|Sites|Native body|Fixed grant (including E16)|
|---|---|---|
|4900/4901 source construction/copy/detach|entry:u8 ownerKind,u8 method,bool parent,[R],sourcePointer,requestedObjectBytes,knownElementCount,knownCapacity; outcome:common end,bool actualObject,[pointer,size_t bytes]|entry75; end69;144 per source operation|
|4902 existing original|sourceR,size_t N,original bytes|48+N DATA before acquisition/copy|
|4903/4904 actual allocate|entry:sourceR,u8 kind,size_t n,sizeof(T),allocator-state pointer; outcome:common end,bool returned,[actual pointer]|65+61=126 CONTROL|
|4905/4906 actual pointee destroy|entry:allocationR,pointer,size_t sizeof(U); return:destroyR,bool actual returned|56+41=97 held CONTROL|
|4907/4908 actual deallocate|entry:allocationR,pointer,size_t n,sizeof(T); return:deallocateR,bool actual returned|64+41=105 held CONTROL|
|4909 share/4910 return|entry:sourceR,sourcePointer,bool exposed; return:common end,actual destinationPointer|49+60=109 CONTROL|
|4911 detach predicate|sourceR,native long actual use_count,bool actual detach|49 CONTROL|
|4912 radix own predicate|sourceR,bool actual null,bool countPresent,[native long],bool actual clone|51 max CONTROL|
|4913 radix path/root|sourceR,u8 actual branch,size_t levels,size,digit,sourceNodePointer|73 CONTROL|
|4914/4915 leaf reserve|entry:nodeR,size_t requested32,sizeof(T),oldSize,oldCapacity,oldDataPointer; outcome:common end,size_t actualSize,capacity,actualDataPointer|80+76=156 CONTROL|
|4916 ByteBuffer validation|sourceR,size_t actual input,limit,max_size,bool actual refusal|65 CONTROL|
|4917 actual scalar/Decimal source|sourceR,u8 variant,exact existing native scalar OR bool sign,int64 exponent,size_t limbs,native uint32 limb bytes|checked native width; paid DATA before copy|

Allocation loan128-style estimates are forbidden: exact bank is126+97+105=328
for shared storage/node blocks, or126+105=231 for byte/leaf buffers without an
owned-object destroy hook. Actual callback count follows the native owner, not
its parent operation's lifetime. Source operations separately reserve144 plus
the exact fixed reached predicate/reserve possibilities before effect. Optional
original DATA is separate. All arithmetic checks UINT32_MAX before admission.
Admission capacity/cancel refuses before a new allocation; already-held destroy/
deallocate remains possible under sticky cancellation and full ordinary lanes.
A create/copy that produces a Result failure emits that actual failure, not a
successful object; standard/length/nonstandard thrown branches remain distinct.

## Direct red and independent finite oracles

Before source edits, real selected native Value construction/share/exposed-copy/
detach, radix root/leaf growth and ByteBuffer binary copy must succeed while the
old preserved raw disk lacks4900/4903/4907. The exact missing-site assertion is the
baseline red. Then one whole code/oracle attack, finite findings and one recheck.
No adapter helper-only certification or extra review panel.

Independent expected native bodies use literal input bytes, actual returned
public object/storage pointers and actual allocator count/size native cuts. Do not
use emitted descriptions, a second Value encoder or infer final drop from scope
exit. Actual final deallocation is the oracle; a pinned copy must keep it absent.
Cases: Value share pointer identity, exposed copy and detached mutation keeping
old bytes; radix32/33 and1024/1025 boundaries with a pinned root and changed leaf;
empty/exact/max−1 ByteBuffer and binary input; actual C++ allocation rejection
before object completion and constructor rejection after block acquisition;
no duplicate release from moves/swaps; last Value/radix/ByteBuffer drop on a real
foreign thread after creating binding and original scope destruction; concurrent
final drops of independent loans serialize their actual producer; ordinary CONTROL
full/sticky cancellation while paid cleanup still emits; per-source/body/bank
one-byte-short grants refuse zero new allocations; selected Lua bridge returned
Value outlives its creating codec scope. Original output bytes and pinned-reader
contents are independently compared. No escaped PID adoption, process replay,
provider call, new dependency, full-suite campaign or performance qualification.

Candidate implementation files: raw_representation.hpp/cpp (genuine resource
issuer/loan/allocator semantics over shared core), confined value.hpp/value.cpp,
shared_sequence.hpp, foundation.hpp/foundation.cpp and dedicated resource fixture.
Root owns CMake/descriptions and synchronous scope integration disposition. The
following codec unit will install explicit real owner scopes; this resource unit
must also have direct selected native construction to prevent a vacuous opt-out
pass. This is a proposed plan, not coverage already delivered.

## Unresolved transport capacity before approval

The resource lifetime math exposes a real integration constraint:328 CONTROL
held per shared allocation (231 per buffer) survives as long as that resource.
Current Main/module producer CONTROL is1–2MiB, while the existing Value limit is
500000 nodes and retained sharing can survive many calls. A single producer with
that capacity cannot fund all promised future cleanup. This is not solved by
streaming already-emitted events, because held final-release claims are live.
No new representation-node cap, blocking admission under the issuer mutex,
producer per object, or claimed synchronous cleanup is acceptable.

Before implementation Root must disposition the exact transport ownership:
explicit resource lanes with sufficient declared owner capacity, actual exhausted
lane rotation retaining all old loan lanes until cleanup, or another concrete
aggregate cleanup protocol. That interface belongs to the shared transport owner;
this plan does not silently authorize it. Also pin selected copy/clone origin when
the original resource crosses bindings: use its immutable creating issuer if no
current actual owner scope is supplied, never borrow a later unrelated TLS lane.
Unselected objects remain opt-out unless an actual selected copy owner chooses
new instrumentation. The resource unit remains proposed until these constraints
and its single plan attack are resolved.
