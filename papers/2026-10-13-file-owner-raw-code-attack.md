# File47–48: one complete code and oracle review

2026-10-13. Root reviewed complete raw_file.hpp/cpp, confined tools.hpp/cpp
changes, raw_file_test.cpp and its typed stdio forwarding dylib, current file
subplan/plan dispositions and direct logs. No second plan panel, workload campaign
or rereview. Native external effects/read bytes use separate actual request and
return records. Read data precedes limit/append. Descriptor/name ownership enters
its prepaid guard before post-effect capture. Rename success consumes old-name
custody before capture; native errors and once-only close/unlink remain distinct.
Held teardown runs without new admission, including publication conflict and
sticky cancellation. Direct real lost-fd stdio parity is useful source evidence;
injected short/error calls are appropriately bounded fault oracles, not host-wide
claims. Mac tests2/2 pass; Linux-only fallback code has not been executed here.

## F1 — acquire/emit proposed bytes before subsequent fallible work

In tools.cpp's write branch, `next = content` acquires the complete actual proposed
string, then exists/read and the file.before observer can throw before
`trace.proposed(next)` ever emits it. Its raw grant is also obtained only after
that acquisition. The edit branch similarly completes `next.replace(...)` before
obtaining the proposed-body grant. A failed grant there discards an already
acquired business representation. Retained Operation17 input and the native file
before source do not represent the distinct actual completed proposed string.
This contradicts the plan's immediate original preservation rule.

For writes capture the borrowed content argument before its copy; prepay the
49+actual-content-size proposed body before `next = content`, and emit it
immediately afterward, before exists/read/observer. For edits use the actual unique
replacement position and checked `next.size()-old.size()+replacement.size()` as
admission bound before the SAME existing replace. Emit its completed output
immediately afterward. Pass the actual preheld claim to proposed emission; do not
claim again after acquisition, recompute replacement for metrics, add a second
content string or change the existing business observer/capture ordering. The
actual typed input Ref remains available to helper write; no duplicate body there.

Strengthen the SAME paths_and_tools observer cut: the real write-file before
observer throws, old disk bytes remain, no temporary write occurs, and its acquired
literal proposed `new` bytes are already preserved once. Add a literal exact/
one-byte-short grant cut at proposal preparation, require refusal before copy/
replacement/native effect, and compare actual edit output bytes on success.
This is a concrete owner oracle correction, not a new testing campaign.

## F2 — describe actual cleanup pathname capture

The subplan says cleanup references the name body rather than copying it anew.
Current3442 correctly emits the actual unlink CString operand and, when available,
its actual earlier mkstemp output Ref, using DATA held before mkstemp. Update that
sentence to describe these actual fields and prepaid original-native-operand
copy. Do not change correct native capture solely to fit stale prose.

After the finite F1 fix, direct owner tests and source gate must pass. The new
Linux-only resolver/fstream path needs its actual native Linux direct case before
source delivery; Mac cannot establish that branch. These checks are implementation
checks, not deferred throughput/capacity/workload qualification. No further code
or oracle review follows this one pass.


## SAME finding disposition, 2026-10-13

F1 fixed: trace.prepare_proposal obtains the literal49+actual output-size DATA
before the existing content copy or unique-position replace. Borrowed content
argument kind3 is captured first. The completed proposal is emitted immediately
through that held claim, before later exists/read/observer work; helper write
references the same actual output Ref without another content copy. Existing
before/proposed observer order is preserved. The actual throwing file.before
oracle now independently requires literal `new` already on disk before first
native before-read, plus the exact edited binary output. Exact and one-byte-short
proposal grants exercise actual copy/replace and verify unchanged destination/no
native write on refusal. F2 corrected the stale sentence only; native3442 remains
unchanged.

Strict current target links, registered Mac native2/2 pass20.02s, narrow
raw_file/tools/fixture clang-tidy and owned formatting/diff checks pass. Logs:
context/file-owner-unit/f1-build.log, f1-direct.log and f1-static.log. An attempted
pre-F1 archive check observed already-fixed proposal3 because the shared archives
had been rebuilt after source changes; it is relabeled f1-postfix-snapshot and
never claimed as red. The initial genuine published missing-byte red is intact.

Root's file-only Linux implementation attempt failed at SSH connection before
remote workspace/build/test/effect; context/file-owner-unit/linux-direct-launch.log
is network failure evidence only. No Linux fallback result, workload/profile
qualification or source delivery is claimed. The branch-specific check stays
pending; independent approved owner work proceeds. No second review follows F1/F2.
