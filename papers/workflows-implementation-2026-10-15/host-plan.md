# F1 native session-host transport subplan

2026-10-15. This lane owns the local Unix-socket client/host boundary and lifecycle
only. Root owns the existing retained writer/session lock before construction,
business dispatch, worker cancellation and the build. All writes stay in the
isolated `blackbird-workflows` checkout. No second ledger or library.

One host owner accepts bounded native BBM2 frames from independent clients,
dispatches complete requests sequentially, returns typed native success/error
envelopes and ticks owner work outside any transport lock. A private nonblocking
wake socketpair lets workers request a tick without entering business state. Send
suppresses SIGPIPE; full endpoints are already readable, while another failure sets
an atomic fault and shuts down the connected receiver to independently wake owner poll and
surface host failure. Idle service has no periodic poll; client deadlines use the
nearest required timeout. Partial
reads/writes, malformed lengths/packets and disconnects affect that client only.
The host stops only on an explicit owner stop condition. Private directory/socket
ownership and permissions are checked. Existing sockets/symlinks/non-sockets are
never unlinked on startup; final cleanup unlinks only the inode this host bound.
Descriptors are close-on-exec and creation is serialized against owned spawns.

Direct tests precede implementation: actual fork clients exchange exact embedded
NUL/non-UTF8 bytes; multiple clients detach/reconnect without replacing host PID;
second bind cannot remove the first endpoint; malformed/oversized frames cannot
kill host; private path/symlink/non-socket refusal; worker wake triggers owner tick;
long partial client traffic does not block another client; final unlink preserves
a replacement inode. Client deadlines are monotonic and finite. No asynchronous
effect-survival or cross-process RawRetainedTransfer is claimed.

No builds in this lane. Root runs affected tests/compiler/static gate. One code
attack and one focused recheck/fix total; no third layer.

## Root attack H1/H2 corrections

Explicit stale cleanup is separate from host construction. After acquiring the
actual retained journal lease, the caller must also exclusively own the private
endpoint namespace until host destruction: no other same-user agent may rename,
unlink or bind there. This is an API authority precondition, not a property
created merely by directory permissions or advisory journal ownership. Arbitrary
same-user mutation is outside that ownership contract.

Cleanup permits either mode0600 or a wider own socket left by a crash between
bind and chmod inside the private0700 directory. It still requires socket type,
actual effective-uid ownership, refused connection, and unchanged device/inode
before unlink. The inode check detects preceding replacement; POSIX provides no
atomic unlink-if-inode operation, so it does not protect the final check/unlink
window from a caller violating namespace exclusivity. Final own-inode destructor
cleanup has the same explicit namespace precondition. Active sockets, symlinks
and ordinary files refuse and remain intact. Direct stale0755/stale0600, absent,
active-inode-preservation, file and symlink cases are in the owned host test.
