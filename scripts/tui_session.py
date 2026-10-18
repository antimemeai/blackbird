"""End a fixture's own tui-test session daemon after its close.

`tui-test run` starts `tui-test __daemon --session NAME`, which outlives the
client and is reparented to init; `close` asks it to stop. When close fails or
times out the daemon and its terminal target keep running. Fixtures call
stop_session(name) on every exit path after they have tried close: it waits
briefly for the daemon to finish, then kills the daemon and its descendants.
Session names are unique per fixture run, so the exact argv identifies only
this fixture's daemon.
"""
import os
import signal
import time

import process_table


def session_processes(session):
    """The session's daemon and every process under it, as (pid, command)."""
    rows = process_table.processes()
    tree = {pid for pid, _, command in rows
            if command.split()[1:4] == ["__daemon", "--session", session]}
    grew = bool(tree)
    while grew:
        grew = False
        for pid, ppid, _ in rows:
            if ppid in tree and pid not in tree:
                tree.add(pid)
                grew = True
    return [(pid, command) for pid, _, command in rows if pid in tree]


def stop_session(session, grace=2.0):
    """Return the processes that had to be killed (empty after a clean close)."""
    deadline = time.monotonic() + grace
    left = session_processes(session)
    while left and time.monotonic() < deadline:
        time.sleep(0.05)
        left = session_processes(session)
    for pid, _ in left:
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    return left
