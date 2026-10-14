# Terminal custody and selected plain output: one plan attack per owner

Root read the complete two-owner proposal against actual TerminalMode/control,
editor draft native custody and shell command, UI wake/resize handler and cleanup,
Main emit/status/greeting/prompt, existing RawCommandWake/RawSignalPublisher and
shared RawEmission. No code, workload or extra emission architecture selected.

H1: Linux sigset_t is the larger libc object, not the qualified64-signal native
kernel mask. sigemptyset/sigaction do not supply initialized meaningful tail
bytes for recording. Reuse RawCommandInterrupt::signal_mask_bytes and existing
native action shape, including Linux actual restorer pointer. Do not copy padded
or unacquired bytes. Darwin uses its actual supported native mask. Pay the exact
request/return variants, and restore prior sigaction only after successful native
installation acquired it (the old unconditional zero-handler restore is a real
bug to fix in this owner).

H2: owner drain records read bytes, not resize_signal's actual write request /
return. The draft explicitly omits native handler publication, so a failed signal
write remains invisible. Reuse the established RawCommandWake signal mode and
prepaid RawSignalPublisher/POD handler machinery instead of inventing TLS calls,
per-signal producers, an ordinary signal emit or a second tracer. A resize wake
may coalesce already-pending notifications because the business drain already
ignores their count; capture every actual reached native wake operation and its
failure. Specify actual signal-owner installation/removal, legal rearm at owner
drain, producer exclusivity, potential worker handler join and paid cleanup.
If keeping the shared ordinary notification pipe, separately explain how its
actual async write is captured without racing its ordinary writer or running out
of preheld publication without an observable failure. Do not claim ordinary
reader observations cover omitted handler operations. The existing native
command-wake design supplies the mechanism; no new timer/service is required.

H3: specify returned draft original funding before the existing final acquisition,
and exactly where an allocation or raw failure ends the paid editor request
before existing business diagnostic fallback. Existing read chunks are real
sources even after limit refusal. Cleanup must preserve each real errno and
already-acquired fd/path before publication failure, and contain ordinary cleanup
failures without manufacturing accepted draft or successful terminal restoration.
The plan's preheld native cleanup equations and fixed path/quote bounds otherwise
fit actual source; use shared emission and no extra generic owner class.

P1 (separate plain owner): old greeting/status ostream sequences write existing
pieces; they do not acquire a joined complete packet. Do not create an additional
formatted string merely for metrics. Capture borrowed literal/actual native
business strings as separate exact pieces or one raw span sequence, with actual
existing safe output funded before the SAME business formatter. Preserve original
unrendered emit text before sanitization; native output refers to the actual
formatted acquisition. Give complete literal4340 DATA-request/adopt and4341
preheld terminal layouts, including source pieces/count/length only as transport
framing. No RawSessionCall CONTROL request at4340 plus an ambiguous same-site
DATA request. Native4342/3 request/return maximum113 is consistent with fd,
offset/requested and native ssize_t/bool/errno. Keep shared bank/publication and
origin handling; failures must not turn into accepted output.

P2: replacing failbit latching with an owned native write Error is an authorized
bug correction under the operator's fix-bugs scope, not a new approval gate.
Keep pre-runtime/read-only/post-teardown stdio outside the current selected
recorder; track bootstrap auditing separately. Do not silently change signal
policy to get EPIPE: native fixture ignores SIGPIPE explicitly and checks the
actual native return; production preserves its established policy. A successful
write records only the actual range, short/EINTR/error attempts stay distinct.

Resolve H1–3 and P1–2 in this same concrete plan; implement each whole owner,
real direct native checks, one code/oracle attack each and finite fixes. No
additional review layer or performance/workload qualification.
