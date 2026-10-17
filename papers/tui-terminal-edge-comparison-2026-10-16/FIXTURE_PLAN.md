# Native comparison fixture

The fixture exercises the actual `ChatGrid`, `ChatPainter` and `ChatPresenter`
owners. It emits only the finite experiment protocol in PLAN.md and never emits
an expected screen. Microsoft drivers independently construct physical planes.
The fixture has no runtime raw capture, provider, session or artwork interaction.

Interactive mode constructs `TerminalMode(nullptr)` before an attached
`ChatPresenter`, so presenter teardown precedes terminal-mode cleanup. The current
ioctl size must be positive and within ChatGrid's own bound. Startup renders s as
sequence0; each acquired input byte increments the sequence and immediately emits
INPUT, including q and unknown bytes. Recognized render requests replace only a
small latest-request value, retaining scene, sequence, measured geometry and the
recover flag. A pending presenter's bytes are never mutated. r retains the last
requested scene and forces full invalidation at the dimensions measured when r
is acquired. Each completed packet emits READY for its own frozen geometry.
PENDING emits once per incomplete packet and makes no EAGAIN claim. q or input EOF
stops accepting input and drains the packet already started plus the latest
accepted request, without changing either in-flight packet. poll waits for stdin or writable stdout; immediate input dispatch
precedes output progress when both are ready. The fixture does not alter DECAWM
outside the corner policy's complete brackets. The external profile supplies
wrap-on as specified in PLAN.md.

PREP captures monotonic and thread CPU timestamps directly before and after
Presenter::prepare, plus literal returned packet length. Scene construction is
outside these stamps. An unchanged grid can yield an empty delta; append a complete synchronized-output bracket around literal CUP(1,1) to that
prepared packet so READY still follows actual flush/commit and the external
observer sees a frame endpoint. The end stamps and byte count follow heartbeat
construction. No duration is calculated in the producer.
Events go to the separately opened regular file or inherited event descriptor.
Partial writes and EINTR are handled exactly; a failed event write fails the
fixture. Expected records are small and the observer must drain an event pipe.

Batch mode does not enter terminal mode or write terminal packets. For each
scenario it constructs an ordinary fixed-size base grid once, then captures raw
monotonic/thread CPU timestamps around N actual grid mutations, prepare and
commit calls. It sums literal packet lengths and reports N frames. Every policy
receives identical mutations; no random generator or warmup/discard exists:

- sparse: change one physical cell at (i modulo columns, floor(i/columns) modulo
  rows), toggling its letter with each complete traversal. Other cells persist.
- stream: fill every cell with a row-dependent letter advanced by frame index;
  the final physical cell receives a lowercase variant as in sentinel s.
- blank: alternate sentinel s with the default blank-tail scene b.
- style: alternate code-colored blank-tail scene c with default blank-tail b.
- unicode: alternate the exact Unicode scene u with overwrite scene o.
- resize: alternate sentinel grids at supplied columns/rows and at
  min(columns,32) by min(rows,12), using positive values. Each frame requests full
  recovery because this scenario models physical resize/recovery.

`--batch all` emits those six BATCH records in that order, each with its own fresh
Painter and base state. Defaults are 200 iterations, 101 columns and30 rows.
The BATCH record is `BATCH scenario iterations frames wallBeforeNs wallAfterNs
cpuBeforeNs cpuAfterNs packetBytes`. Parsing and invalid arguments fail visibly;
no internal timeout silently discards a frame.
