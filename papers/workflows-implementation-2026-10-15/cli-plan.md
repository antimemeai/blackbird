# Native workflow execution host CLI subplan

2026-10-15. Own `workflow_main.cpp` and its process oracle; root owns CMake and
builds. Link `blackbird_foundation`, with the existing archive executable available
for its normal optional archive worker. No provider/model calls or new library.

`--serve --session PATH` establishes a private session, existing RawRuntime owner
lane and NativeJournalDirectory/RetainedState writer before binding the host.
Native BBM2 remains the only IPC and retained format. `--session PATH --request
JSON` explicitly selects the external JSON request/presentation boundary. Client
disconnect does not stop the server. The execution host survives every client.

On reopen, confirm the checked prefix and reconcile only when no unresolved
attempt remains. Otherwise expose read/list/status in a read-only host; do not
acknowledge old effects, revive a worker, adopt a PID or admit independent work
through an unreconciled writer. A stale socket is removed only after actual
journal ownership and a refused connection plus unchanged owned inode. Active,
foreign, symlink and regular-file endpoints refuse.

SIGINT/TERM use an owned synchronous signal waiter and the host wake endpoint;
ordinary owner shutdown retains its control intent, joins workers and drains
results before the raw lane/writer die. A failed runtime drain does not imply
successful shutdown or close the usable host endpoint. Transport-fatal failure
still attempts mandatory cleanup and reports the actual error.

Direct process test: launch the real executable and raw recorder, submit native
file/command work through independent clients, detach/reconnect and count one
host/one effect; inspect and cleanly reopen exact settled results; reject a second
owner without removing endpoint; kill the host around a gated effect, reopen
read-only and independently verify no replay/new admission; explicit shutdown and
stale endpoint recovery. Root owns all test execution and static checking.

The explicit JSON response boundary recursively presents an invalid-UTF8 native
value string using the CLI binary-value wrapper
`{"encoding":"hex","bytes":"00ff10"}`. Native retained/IPC packets keep raw
strings. Valid UTF8 strings remain JSON strings; this presentation wrapper is not
an automatic import codec or an unambiguous tag for arbitrary user objects.

The owning CLI process oracle's crash phase now releases two separately identified
native execs under the actual RawRuntime at once. Each owns its own effect file,
readiness/PID marker and explicit gate. Both readiness markers and one effect each
must exist while both results remain pending before host kill. Exact duplicates
preserve their distinct invocation/attempt identities and neither effect count
changes. After kill, both own gates release, both fixture exits are observed, and
reopen must expose both exact attempts as unknown with no executor or replay.
This qualifies concurrent native raw funding and root settlement through the same
existing process path; it introduces no new product source or review family.
