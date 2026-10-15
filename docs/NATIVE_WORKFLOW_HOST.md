# Native workflow host

The first concurrent-workflow implementation runs native tasks in an independent
session owner. `blackbird-workflow` owns one RetainedState journal, four worker
threads, a 32-task live limit and a private local socket. Clients may leave and
reconnect while tasks continue. Build the executable with the existing debug or
release CMake preset. The first source checkpoint is qualified and published;
[checks, fault oracles and every baseline sample](../papers/workflows-implementation-2026-10-15/QUALIFICATION.md)
record the actual scope.

Start a new private session in one terminal:

```sh
build/debug/blackbird-workflow --serve --session /tmp/blackbird-native-demo
```

Another terminal can submit work and inspect it:

```sh
build/debug/blackbird-workflow --session /tmp/blackbird-native-demo --request '{"op":"start","id":"a","tool":"echo","arguments":{"answer":42}}'
build/debug/blackbird-workflow --session /tmp/blackbird-native-demo --request '{"op":"read","id":"a"}'
build/debug/blackbird-workflow --session /tmp/blackbird-native-demo --request '{"op":"status"}'
```

`start` supports `echo`, `exec`, `read_file`, `write_file` and `edit_file`, using
the existing LocalTools arguments. Tasks run relative to the host's working
directory. Each owns its tool instance and captured output. The accepted input
records the native build identity. Explicit `output_max_bytes` bounds file
presentation while retaining the acquired original. The default conversation
turn program's 16 KiB budget does not apply to this host; `read_file` takes `path`,
and conversational `snapshot` recovery handles belong to CodingEngine. Reusing an ID with the same tool/arguments
returns the same invocation; changing those arguments rejects. Reopen keeps the
accepted source identity rather than silently choosing the new build.

A completed result retains its actual originals and terminal observation in the
same journal batch. The host funds bounded result space and its record identity
before release. Input is limited to 512 KiB, each tool file/process capture to 1 MiB,
and packaged results to 4 MiB. A process collection overflow stops collection and
reports an unknown result with the acquired prefix; an oversized file refuses.
`originals_complete=false` identifies an error/exception gap. Native BBM2 preserves
raw bytes. The explicitly selected JSON CLI wraps invalid UTF-8 value strings as
`{"encoding":"hex","bytes":"00ff10"}`. That is a presentation wrapper, not an
automatic import codec or a collision-free tag for arbitrary user objects.

Create a closed join over accepted task IDs:

```sh
build/debug/blackbird-workflow --session /tmp/blackbird-native-demo --request '{"op":"join","id":"answers","mode":"all_success","members":["a"]}'
build/debug/blackbird-workflow --session /tmp/blackbird-native-demo --request '{"op":"join-read","id":"answers"}'
```

The host exposes `all`, `all_success`, `first`, `first_success`, `quorum` and
`collect`; quorum takes a numeric `quorum`. The pure reducer also supports pinned
selection predicates and open membership, awaiting their owning frontend units.
Joins use retained owner acceptance order. No synthetic clock is assigned to an
untimed join. Chosen results reference exact task-cell revisions; the choice is
committed before exposure and remains unchanged by later completions. A join does
not cancel its losing members or assert their physical settlement.

`{"op":"cancel","id":"a"}` retains ordinary cancellation intent before native
stop, and distinguishes prevention from requesting an already-running task to
stop. `{"op":"shutdown"}` joins workers and retains results before the host exits.
A failed writer closes new admission and invokes mandatory cleanup. Completion
bytes remain owned for a later drain while the host lives; failed drain does not
claim a successful shutdown. Emergency cleanup may lack an ordinary control record;
its error is included in results when retention permits. Local child/group cleanup
has the existing LocalTools containment limits.

A clean reopen restores exact results and joins. A crash after an effect was
released preserves an unresolved attempt, shown as `unknown_after_recovery` with
`executor_absent`. The first host serves inspection read-only while the existing
RetainedState reconciliation fence is closed. It does not rerun that effect or
adopt an old PID. Stale socket recovery requires the actual journal writer lease
and exclusive ownership of the private endpoint namespace; inode comparison is
not atomic protection against arbitrary same-user namespace mutation.

This is the native execution substrate for
[the delivery sequence](WORKFLOW_DELIVERY.md). Task-local model/context loops,
standing scopes and resource leases, durable continuation/reconciliation commands,
recorded Lua, event/time/stream waits, artifacts/demand, live evolution and remote
stations remain owning delivery units. The existing conversational launcher has
not been replaced by this executable.
