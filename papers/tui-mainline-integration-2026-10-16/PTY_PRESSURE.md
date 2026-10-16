# PTY pressure gate finding and bounded response

The first combined-source installed hook fails226/227 in296.84s before lint.
`command_jobs_pty` reaches its unchanged30s pressure deadline with246402 of524288
bytes unoffered. The run has no retained terminal artifact because CTest supplied
no log directory. Do not call this a demonstrated product defect or dismissed flake.

One unchanged-source diagnostic with full artifacts passes:316798 bytes transferred,
207490 unoffered,1765 actual EAGAIN observations, saturation14.11940925s. The child
background/stop/quit, terminal restoration and closed-journal original-byte assertions
all pass. Staff offline replay finds literal refusal in14 consecutive completed
frames; main_text selects it correctly. Several hundred KiB must traverse128-byte
audited reads to fill pending input plus60 mailbox entries, so throughput remains
a possible contributor to the failed run. No source-causal conclusion follows.

A concrete fixture defect exists independently: it checks refusal only before
reading output inside EAGAIN. Either later output read can acquire the literal,
then the next loop's deadline assertion fails without examining it. Correct the
observation order: evaluate actual blocked-write plus current literal refusal after
reads and before deadline failure; at deadline consume one ready bounded output
read before the final evaluation. Preserve30s,512KiB, actual EAGAIN plus refusal,
and every child-lifecycle/input-conservation assertion. Do not add tests of tests.
The actual failing suite is retained RED; the corrected actual fixture supplies
its direct GREEN, without claiming the observation defect caused that RED.

CTest should always request artifacts. Allocate a unique child of its supplied
log directory and print it before spawning, so a subsequent run preserves failures
instead of overwriting them. Existing terminal/raw/keyboard/pressure trace output
is sufficient; no new certification mechanism or product instrumentation is needed.
Run the affected actual fixture, one independent concrete diff challenge, then the
required full source hook. Keep unresolved throughput in the ordinary collective
queue if it recurs; no blind retries, assertion weakening or deadline enlargement.
