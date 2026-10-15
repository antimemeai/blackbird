# Mac linker block: primary-source research and diagnostic boundary

2026-10-15. Research lane: source/document reads only. No process attachment,
process probe, signal, build, toolchain substitution, service change or restart
was performed by this lane. Root owns host diagnosis and any implementation.

## What the retained observations establish

The [previous result](../tui-settlement-repair-2026-10-15/RESULTS.md) and
`context/tui-next-host-2026-10-15/reopen-coding-link.log` identify the actual
configured native link, followed by negligible reported linker CPU. Exact owned
linker PIDs57610/62006 remain reported after targeted SIGKILL requests;
`sample` also fails to return. This establishes a host/tool block before tests,
not a C++ test failure or a particular kernel deadlock.

The recorded public linker flags557858832 are hexadecimal0x21404010. They do
not contain `PROC_FLAG_INEXIT` bit4. This is one observation at that timestamp,
not proof that exit was impossible later. `PROC_FLAG_EXEC` means the process has
called exec; it is not a report that exec is currently in flight. XNU derives
the public exit flag from `P_LEXIT`.
[Flag construction](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/proc_info.c#L702).

## Read-only APIs that answer the next concrete questions

Use the installed SDK's public structures and exact returned byte counts. Match
PID, original start time/path and known ownership before interpreting a later
sample. A PID's continued presence alone is not an identity guarantee.

| Question | API | Meaning and limit |
| --- | --- | --- |
| Has the task consumed CPU or made scheduler/syscall progress? | `proc_pidinfo(pid, PROC_PIDTASKINFO, 0, ...)` | Total user/system time, pageins, syscall counts, context switches, thread count and running-thread count. Compare two observations; a single low CPU total is insufficient. |
| Which threads actually run or wait? | `PROC_PIDLISTTHREADS` flavor6, then `PROC_PIDTHREADINFO` flavor5 with each returned64-bit entry | The list contains cthread addresses, not unique thread IDs. Return states1running,2stopped,3waiting,4uninterruptible,5halted. No task port, debugger attachment or target suspension is required. No wait-channel string is supplied. |
| Does a unique-thread-ID API help? | XNU `PROC_PIDLISTTHREADIDS` plus `PROC_PIDTHREADID64INFO` flavor15 | These must be paired. Do not pass unique IDs to flavor5. The list flavor is absent from this installed public SDK header; public6→5 is the immediate route. |
| Did physical disk I/O complete? | `proc_pid_rusage` with a supported `rusage_info` flavor | Read/write byte counters and CPU/pagein fields can expose progress; unchanged counters do not identify which filesystem or lock is blocking. |
| Which exact files are held? | `PROC_PIDLISTFDS`, then `proc_pidfdinfo` with `PROC_PIDFDVNODEPATHINFO` for selected vnode descriptors | Locate input archives/output/temp paths. A descriptor list is not a list of current blocking operations; memory mappings can outlive descriptors. Path lookup can acquire filesystem locks, so this is a later isolated probe. |

The public definitions and thread implementation support these distinctions:
[process/thread structures](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/sys/proc_info.h#L119),
[unique-ID dispatch](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/proc_info.c#L2307),
[thread lookup](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/kern/bsd_kern.c),
[disk-I/O counters](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/sys/resource.h#L240).
Read-only comparison against the installed SDK confirms the public6/5/15
flavors, run-state constants and exit flag; it does not establish an exact
source/build match for the running kernel.

## Why SIGKILL, process status and probing need careful interpretation

XNU's SIGKILL delivery path sets `p_stat=SRUN` before requesting `thread_abort`.
Thus a reported process status of running can coexist with a thread still
blocked in the kernel. SIGKILL cannot be caught by the application, but sending
it is not a synchronous certificate that teardown completed.
[Signal implementation](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/kern_sig.c#L2426).

The thread implementation separately maps an internal uninterruptible wait to
public state4. That is stronger evidence of a kernel wait than low CPU alone;
it still does not identify storage, security policy or exit teardown.
[Run-state mapping](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/kern/thread.c#L2038).

There is a concrete mechanism by which diagnostic tools can themselves wait:
XNU's process-reference path can wait uninterruptibly during an in-flight exec,
using the wait message `proc_refwait`. `proc_pidinfo` also takes a process
reference. This makes observer blockage plausible; it does not prove this host
is in that path. An observer whose syscall blocks cannot enforce its own
Python timeout. Keep any uncertain probe in one separately owned child and
retain its identity; do not create a sequence of stranded helpers.
[Reference wait](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/kern_proc.c#L819).

## Linker reference and its limits

Apple's available `ld64` implementation writes mapped output on supported local
filesystems through a temporary path, hashes an embedded signature, then closes,
chmods and renames the file. It also contains an alternative write path.
This supplies useful output-file hypotheses, not an attribution for this
machine. The source describes the classic linker; the current Xcode linker may
use a different implementation. Do not switch linker, disable signatures or
adopt the reference's environment switches as a repair without showing the
actual blocked phase.
[Apple ld64 OutputFile.cpp, revision f60a74ea](https://github.com/apple-oss-distributions/ld64/blob/f60a74eaa2c99585de1dc0f2820e7a9f8aaf522c/src/ld/OutputFile.cpp#L4042).

## Narrow plan for the host owner

1. Read two exact-owned-PID task snapshots and one public thread list/state
   snapshot, preserving identity and raw values. Classify actual execution,
   ordinary wait, uninterruptible wait, stopped state or absence. Avoid global
   `ps`/`sample` or attaching to unrelated services.
2. If metadata works, obtain only the selected linker's relevant descriptor
   paths and inspect the named output/temp-file metadata. Relate actual progress
   and file ownership to the link command before choosing a filesystem or
   linker hypothesis. Do not remove live mapped/temp files.
3. Use observed causes to select one reversible repair. A persistent
   uninterruptible kernel wait without an identified dependency has no honest
   application-code repair yet. Repeated signals, broad daemon kills, security
   changes and unchanged build retries do not resolve the missing diagnosis.
4. Resume the existing mandatory source gate once the selected repair actually
   makes the configured linker finish. Preserve prior test deadlines and all
   source changes; the host block does not justify making TUI source inactive.

These steps are a proposed diagnostic sequence, not reports that probes or
repairs have run. They preserve the two repaired test families and existing
source publication requirements without expanding into a new assurance layer.
