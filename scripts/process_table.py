"""The current user's processes as (pid, ppid, argv string), for test fixtures.

macOS reads libproc and KERN_PROCARGS2 directly: `ps` uses sysctl(KERN_PROC),
which was measured blocking uninterruptibly for minutes on a heavily loaded
host while libproc answered in milliseconds. Linux reads /proc.
"""
import ctypes
import os
from pathlib import Path
import sys


def _darwin():
    libc = ctypes.CDLL("/usr/lib/libSystem.B.dylib", use_errno=True)
    libc.proc_listallpids.argtypes = [ctypes.c_void_p, ctypes.c_int]
    libc.proc_pidinfo.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64,
                                  ctypes.c_void_p, ctypes.c_int]
    libc.sysctl.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_uint, ctypes.c_void_p,
                            ctypes.POINTER(ctypes.c_size_t), ctypes.c_void_p, ctypes.c_size_t]

    class Info(ctypes.Structure):  # struct proc_bsdinfo, <sys/proc_info.h>
        _fields_ = [("flags", ctypes.c_uint32), ("status", ctypes.c_uint32),
                    ("xstatus", ctypes.c_uint32), ("pid", ctypes.c_uint32),
                    ("ppid", ctypes.c_uint32), ("uid", ctypes.c_uint32),
                    ("gid", ctypes.c_uint32), ("ruid", ctypes.c_uint32),
                    ("rgid", ctypes.c_uint32), ("svuid", ctypes.c_uint32),
                    ("svgid", ctypes.c_uint32), ("rfu", ctypes.c_uint32),
                    ("comm", ctypes.c_char * 16), ("name", ctypes.c_char * 32),
                    ("nfiles", ctypes.c_uint32), ("pgid", ctypes.c_uint32),
                    ("pjobc", ctypes.c_uint32), ("tdev", ctypes.c_uint32),
                    ("tpgid", ctypes.c_uint32), ("nice", ctypes.c_int32),
                    ("start_sec", ctypes.c_uint64), ("start_usec", ctypes.c_uint64)]

    count = libc.proc_listallpids(None, 0)
    pids = (ctypes.c_int * (count + 64))()
    found = libc.proc_listallpids(pids, ctypes.sizeof(pids))
    limit = ctypes.c_int()
    size = ctypes.c_size_t(ctypes.sizeof(limit))
    libc.sysctl((ctypes.c_int * 2)(1, 8), 2, ctypes.byref(limit), ctypes.byref(size), None, 0)
    buffer = ctypes.create_string_buffer(limit.value)
    rows = []
    for pid in pids[:found]:
        info = Info()
        if (libc.proc_pidinfo(pid, 3, 0, ctypes.byref(info),
                              ctypes.sizeof(info)) != ctypes.sizeof(info)
                or info.uid != os.getuid() or info.status == 5):  # SZOMB
            continue
        size = ctypes.c_size_t(limit.value)
        command = ""
        if libc.sysctl((ctypes.c_int * 3)(1, 49, pid), 3, buffer, ctypes.byref(size), None, 0) == 0:
            raw = buffer.raw[:size.value]
            argc = int.from_bytes(raw[:4], sys.byteorder)
            strings = [part for part in raw[4:].split(b"\0")]
            # executable path, padding, then argv
            strings = strings[1:]
            while strings and strings[0] == b"":
                strings = strings[1:]
            command = " ".join(part.decode(errors="replace") for part in strings[:argc])
        rows.append((pid, info.ppid, command))
    return rows


def _linux():
    rows = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            if entry.stat().st_uid != os.getuid():
                continue
            stat = (entry / "stat").read_text()
            state, ppid = stat[stat.rindex(")") + 2:].split()[:2]
            if state in ("Z", "X"):
                continue
            argv = (entry / "cmdline").read_bytes().rstrip(b"\0").split(b"\0")
        except (OSError, ValueError):
            continue
        rows.append((int(entry.name), int(ppid), " ".join(a.decode(errors="replace") for a in argv)))
    return rows


def processes():
    return _darwin() if sys.platform == "darwin" else _linux()
