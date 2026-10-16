# Native application interface

Run `blackbird --application` with stdin/stdout pipes. The native host is a trusted
peer with the same engine and tools as the terminal operator. It owns one session
for the lifetime of the process. Diagnostics go to stderr; stdout carries packets
only. No browser actuation is added by this interface.

Each packet is a four-byte unsigned big-endian byte length followed by one BBM2
packet, including its `42 42 4d 02` prefix. Length must be 5 through 4,194,304 bytes.
The implementation and the exact BBM2 tags/word table are in
[`src/packet.cpp`](../src/packet.cpp); the development reader is
[`scripts/native-packet.py`](../scripts/native-packet.py). An encoder may always
emit text strings (tag 6), including object keys, instead of word-table tokens.
Strings must be UTF-8. IDs are nonempty strings of at most 128 bytes selected by the host.

The first packet is `open` with `id`, `session` (directory), optional `model` and
`effort`. Ordinary Blackbird session settings, retained context, locking and recovery
apply. Successful opening emits `ready` with the open ID, `protocol: 1`, `model`
and `session`. Startup failure emits `error` and exits.

Subsequent commands:

- `turn`: `id`, `objective`, optional `observations` array. Each observation has
  string `url`, `title`, `text`; additional native metadata is retained, including
  `origin`, `frame_id`, `document_id`, `capture_id`, integer `capture_start_ns` and
  `capture_end_ns`, boolean `truncated`, and `coverage`. Observation bytes are
  retained as BBM2 originals; a source-linked rendering enters context separately
  from the operator objective. The host chooses when to send fresh live evidence.
- `steer`: the same fields with a **new** ID. During a turn it requests cancellation
  and queues a replacement. The old turn's `final` precedes replacement output.
  One replacement may be pending; another receives `error: busy`.
- `cancel`: `id`. Cancels the active turn and clears a queued replacement. Emits
  `status` for the command ID; the active turn emits its own `final`.
- `close`: `id`. Cancels work, clears queued work, shuts down engine children,
  checkpoints context, emits `closed`, then exits.

Events carry `type` and `id`. `text` carries an assistant text delta in `text`;
`status` carries current engine status in `text`; `final` carries `outcome`
(`completed`, `cancelled`, `failed`) and the displayed text in `text`.
Final text retains at most the first 2 MiB; `text_truncated` states whether it is
only a prefix. Text events carry the entire stream in chunks of at most 64 KiB;
hosts retain their assembled stream when the final prefix was truncated.
`error` carries `message` and boolean `final_follows`. Invalid/rejected commands
set it false and emit no final. A failed admitted turn sets it true, then emits
its failed `final`; retain that request until final rather than treating the
error as the completed attempt. Fatal errors set it false and close transport.
Malformed/oversized/truncated frames terminate the connection; clean EOF cancels
active work and discards queued work. The host must continue reading output until
exit and must keep input open until the work it wants has completed.

Cancellation remains cooperative with the existing engine/provider/tool machinery.
The browser stays on its own event loop while the host reads packets asynchronously.
The host must keep draining output: a blocked stdout write can delay cooperative
cancellation until the reader resumes or disconnects.
Existing native engine tools remain available; browser proposal/actuation tools
will be added as explicit native operations when that workflow needs them.
