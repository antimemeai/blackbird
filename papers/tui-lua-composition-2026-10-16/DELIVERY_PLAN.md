# Exact process-delivery observation owner

2026-10-16. Implements the bounded owner read from PLAN and DELIVERY_STUDY.
The later native bridge retains accepted action envelopes across view/reload;
this unit neither runs Lua nor admits another domain operation.

CommandJobs gains a borrowed, exact-key `try_observe_delivery` read. A query names
one canonical 32-hex job ID, one nonempty request string of at most128 bytes and
an optional expected owner incarnation. Zero bootstraps this owner's incarnation;
nonzero mismatch refuses. The incarnation identifies this physical CommandJobs
owner within this process only, never a restart/recovery token; root binds actual
environment/conversation and fresh accepted-inbox identity separately. After
restart, missing native history remains unknown. The caller
keeps CommandJobs alive throughout the call. No reader pins a Job/worker or escapes
owner destruction, and no query releases a final shared owner on the input thread.

Output is a fixed native value: query status, actual owner incarnation, exact
job/key, absent-job/absent-key/reserved/selected/queued/consumed/failed stage,
typed validated operation/signal, existing flags/counters, original admission,
enqueue, selection and control raw refs, launch ref, and at most3 actual native
signal-call facts. Missing job means not resident, which includes retirement;
it cannot prove never admitted. Missing key after explicit refusal or receipt
retirement remains unknown. No replay permission is inferred from absence.
Semantic operation links are explicitly unavailable in this tranche: no invented
ApplicationRecordId, SourceReference or reconstructed fact index.

The read validates fixed fields, try-locks the existing mutex, performs at most
three ordered map seeks, then copies scalar/fixed fields. Reservation/Delivery
cache validated op/signal tags and original provenance at actual transitions;
delivery maps gain transparent lookup. Query does not traverse Value/mailbox,
allocate, wait, emit, retain, drain or dispatch. Repeated observation changes no
registry/capture/delivery/frontier state.
`lookup_count` counts find calls, not comparator invocations or measured work.

At request enqueue retain first accepted2013/2015 refs; selection saves the
existing dequeue2015 ref once. Actual delivery installation transfers those refs
and saves the controlling2013 ref. Duplicates preserve original refs. The existing
signal_owned calls retain their actual method/target/number/rc/errno and2020/2021
refs in the matching Delivery's fixed3 slots. Routing, syscall count, raw claims,
capture fences and ordinary command responses remain unchanged. Consumption is
not kernel success or application interruption; errno is meaningful only for a
failed call. Final cleanup distinguishes abandoned delivery from worker consumption.
The existing launch_ref write is published under that same mutex, after the
capture call, so startup observations cannot race the worker's first reference.

Permanent tests use actual CommandJobs workers and private fixtures. First compile
an unavailable implementation seam and capture runtime RED for the new exact-key
contract. Then implement and verify: reserved/selected pure reads under allocation
denial; real worker signal consumption plus exact duplicates/conflict/fresh keys;
exact native call/error/fallback/raw references; busy actual mutex; explicit
refusal/retirement and wrong owner. Existing mailbox/command tests retain their
separate admission/custody coverage. Root owns CMake/shared builds, grants the
focused build lease and performs one independent implementation/oracle challenge
after GREEN. Logs stay under ignored context/tui-lua-composition-2026-10-16/delivery.
