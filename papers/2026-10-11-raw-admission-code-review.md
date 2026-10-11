# Raw admission code/oracle attack

2026-10-11. Independent attack requested by root. Scope is the admission
subplan, RawWaitControl/RawWaitScope, wait helpers, RPC gate/request ownership,
RawThreadContext retirement, raw_claim_control/raw_wait_data, hook interruption
propagation and raw_admission_test. Metadata recovery is excluded. This is one
source/oracle attack, not another design panel or qualification layer.

## Concrete findings

1. Supervisor `reply` sends recorder/fence/result/ErrorCode in four native i64s.
   `State::exchange` constructs Error with only the code. A native registration
   write/open error therefore loses errno: ENOENT becomes io:0. An owner then
   emitting that error cannot record the original fields. Carry i64 detail in
   the owned RPC and directly force a supervisor registration I/O failure to
   check exact ErrorCode/detail at the requester.
2. RawWaitScope merges parent controls but does not normalize duplicates in
   the new control. OuterA and child{B,B} produce capacity refusal although
   there are only two distinct descriptors. Normalize duplicates before merge
   or define/refuse invalid duplicate input explicitly; the existing stated
   distinct-control rule supports normalization. Add a nested duplicate oracle.
3. The subplan names actual deadline waits at full space, gate and reply. The
   fixture covers full data/control and reply deadlines, but gate ownership
   only with cancellation. Give a second requester a deadline while an actually
   queued first request holds the stopped-supervisor RPC gate. Require ETIMEDOUT
   and demonstrate that the first reply and next token remain correctly owned.
4. A successful status response reporting supervisor fenced does not persist
   that state in State or context. On a bound lane with free capacity,
   raw_wait_data/raw_claim_control return success without consulting the known
   supervisor fence. This contradicts the existing rule that ingestion failure
   closes new admission. This predates the cancellation change but remains in
   the reviewed admission boundary. Retain the known fence in shared admission
   state without adding a hot-path RPC; require a real fenced recorder with
   free lane space to refuse new admission while prepaid terminals still emit.

## Mechanisms that survived this attack

The pipe token has a separate acquire/release atomic ordering private RPC state;
nonblocking send/recv avoid an uninterruptible readiness race. After interrupted
send, pending_response remains set; the next owner drains it before sending its
own request. Sticky cancellation is never consumed by the waiter and held
terminal claims remain available. Poll uses the caller's actual deadline, with
no periodic timer. Current retirement catches interruption and leaves its
registered mapping in supervisor custody. These observations are source analysis;
root runs the direct fixes/rechecks and integrated gate.
