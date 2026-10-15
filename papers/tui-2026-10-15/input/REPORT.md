# Expressive terminal and physical input: Blackbird reference supplement

2026-10-15. This is acquired-source research and a set of design questions, not a dependency choice or implementation plan. Read [MANIFEST.md](MANIFEST.md), [sources.tsv](sources.tsv), [documents.tsv](documents.tsv), and [SHOPPING_LIST.md](SHOPPING_LIST.md) for restoration and acquisition limits. Seven pinned source archives cover keyboard encoding, remapping, MIDI, OSC and a haptic controller. Primary document captures additionally include the official MIDI 1 and UMP/MIDI 2 PDFs; these standards were acquired for later close reading, not deeply reviewed here. Original archives and cleaned source remain under `quarantine/tui-2026-10-15/input/`. No acquired program, firmware, installer, or account integration ran.

The strongest direction is to make commands independent of their input devices. A terminal key, MIDI pedal, encoder, Lua operation and model tool should be able to address the same operation with the same target and cancellation meaning. Keep text composition, key identity, gesture recognition, command dispatch and device feedback distinct. That architecture is an inference from the sources below; it is not a claim that any acquired project already supplies Blackbird's desired system.

## Terminal input loses information before the TUI sees it

Legacy encodings collapse distinct keys and modifiers; Escape also starts control sequences. A timeout cannot recover information already collapsed into identical bytes. [Paul Evans's fixterms specification](https://www.leonerd.org.uk/hacks/fixterms/) describes the collisions and an extended encoding. Its CSI-u convention is not interchangeable with every later protocol using the same final byte. Preserve which negotiated encoding is active.

[Kitty's keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/) supplies opt-in disambiguation, event types, alternate key identities, all-key reporting and associated text. Negotiate capabilities before interpreting enhanced input, and restore the previous keyboard mode on terminal handoff or exit. Protocol support must be checked through the actual multiplexer path. The protocol provides a base-layout alternative useful for layout-independent shortcuts; it does not promise arbitrary hardware scan codes or that the OS will deliver shortcuts it reserves.

The pinned Kitty source is particularly useful for precise semantics:

| Source anchor, relative to `quarantine/tui-2026-10-15/input/kitty/` | Mechanism to study |
| --- | --- |
| `docs/keyboard-protocol.rst:83` | Startup push and exit pop; main and alternate screens have independent mode state. |
| `docs/keyboard-protocol.rst:139` | Structured key, alternate-key, modifier/event and associated-text fields. |
| `docs/keyboard-protocol.rst:225` | Press=1, repeat=2, release=3 on the wire; missing event field means press. |
| `docs/keyboard-protocol.rst:249` | Pure text can have key number zero; composition is not a keypress-to-character lookup. |
| `docs/keyboard-protocol.rst:279` | Five progressive flags and query/push/pop semantics. |
| `kitty/key_encoding.c:445` | `encode_glfw_key_event` maps internal flags and events, including plain text versus structured output. Internal action enum values differ from wire values. |
| `kitty_tests/keys.py:16` | Encoding cases including modifiers; later alternate-key matching tests at 838 and 871. |

Do not enable event-type reporting alone and assume letter releases now exist. Plain text stays UTF-8 unless all-key reporting is enabled; associated text then needs its own enhancement. When the OS provides composed text without usable key identity, preserve text as text. A text composer must not reconstruct committed characters from a US keyboard table. This makes dead keys, AltGr, IMEs and non-Latin layouts concrete qualification cases, not cleanup work.

[XTerm control sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html) give separate protocols for keyboard modifiers, paste and focus. The retained document is updated for patch 411, 2026-08-23. Focus reporting enables clearing app-owned transient holds when focus is lost. Bracketed paste distinguishes pasted bytes from commands; paste boundaries can span arbitrary reads. A terminal query reply also arrives in the input byte stream and must not become a user command.

Blackbird already officially supports phux (`docs/PHUX.md`). Compatibility must therefore distinguish direct terminal capabilities from capabilities exposed by the pane. Test Ghostty/Kitty directly and through phux, detach/reattach, SSH where relevant, and a legacy terminal. A fallback mode should explain which gestures cannot be expressed. Do not assume a multiplexer supports enhanced keys because images or ordinary arrows work.

## Chords and expressive bindings trade ambiguity for delay

Kanata and QMK are references for input state machines, not terminal libraries to embed. They operate earlier in the input stack and can see presses/releases unavailable in a legacy PTY.

Kanata's pinned `docs/config.adoc:4110` describes `defchordsv2`: unordered participants, a deadline starting with the first press, layer exclusions, and either first-release or all-released lifetime. `docs/config.adoc:2011` describes tap-hold variants; `:4913` describes concurrent evaluation. Inspect `keyberon/`, parser and simulated-input tests before transferring rules. QMK's `docs/tap_hold.md:176` distinguishes default timeout, nested-key permissive hold, and hold-on-other-press; `docs/features/combo.md:28` treats overlapping combos. These are different decisions, not alternative names for one timeout.

For Blackbird, define sequence-prefix completion, overlapping chord priority, repeat handling, press/release lifetime, mode precedence and failed-prefix replay before choosing syntax. A prefix hint can show valid continuations and the active target. A binding inspector can explain which layer consumed an event, which alternate key matched, and why another binding lost. Keep an immediate binding path available for operations where waiting is unacceptable. Changing a chord deadline cannot make a fundamentally ambiguous gesture both instantaneous and perfectly classified.

A model- or Lua-editable binding registry needs stable command identities and descriptions, argument types, current target identities, cancellation meaning and presentation labels. It must not permit stale closures to retain a removed pane or old Lua VM. One proposed division is native transport/decoding/gesture state with Lua defining mappings and actions through owned values; qualification must decide whether Lua callbacks can yield, allocate or fail while handling input. This proposal adopts no library and requires no internal JSON.

## MIDI is a separate event source, with device-specific meaning

libremidi offers the richest acquired native reference. Its current `include/libremidi/input_configuration.hpp` has callbacks, raw-byte input and distinct timestamp modes. `book/src/hotplug.md` explicitly warns observer callbacks can arrive on other threads. `book/src/midi-1-in.md` warns against GUI work in input callbacks. `book/src/polling.md` shows how applicable backends can join an existing poll loop instead of creating a private thread. `book/src/queue.md` explains why the application owns its event queue. These are useful seams for an owned C++ adapter, not a dependency decision.

Port disappearance/reappearance must be an event. A numeric port index is not durable identity. Reconnection also does not prove a physically indistinguishable device is the original device: libremidi's README notes that port comparison is use-case dependent. Give each connected instance a generation and retire its held buttons, pending chords and feedback work on disconnect. Keep configured routing separate from the currently connected handle.

`input_configuration.hpp:86` and RtMidi `RtMidi.h:439` reveal an easy trap: timing, sensing and SysEx are ignored by default. Clock-driven interactions or controller identity/configuration exchanges must explicitly choose what to receive. Preserve source timestamps and their clock domain; libremidi supports API-absolute, system-monotonic, relative and audio-frame modes, which cannot be compared by treating all numbers as nanoseconds from the same origin. Its `detail/midi_stream_decoder.hpp` separates decoded messages from raw data; tests under `tests/unit/midi_stream_decoder.cpp` are a source for fragmented-stream cases.

RtMidi supplies a smaller contrasting design. `RtMidi.h:337` documents queue capacity and incoming-message discard when full. Callback mode bypasses that queue. `RtMidi.h:406` says cancellation does not wait for an in-flight backend callback; callback-owned data must remain alive until the port closes. This is directly relevant to Lua reload and shutdown: removing a binding does not automatically quiesce the callback that references it.

[The libremidi SMC2024 paper](https://smcnetwork.org/smc2024/papers/SMC2024_paper_id104.pdf) is retained locally. Its API discussion motivates handle-based ports, callback installation before opening, explicit queues, application control of event loops and timestamp domains. It is a 2024 architectural reference; its platform-status claims must not be treated as today's compatibility matrix.

[Ableton's controller manual](https://www.ableton.com/en/manual/midi-and-key-remote-control/) gives valuable physical-control semantics: absolute controls require a takeover policy after rebinding; relative encoders have several incompatible encodings and may implement acceleration themselves. Output feedback keeps LEDs or motorized controls consistent with the application. These patterns transfer well to a TUI, but the selected controller's manual must define the actual mapping.

The acquired libremidi `include/libremidi/protocols/remote_control.hpp:18` is explicitly a clean-room reverse-engineered Mackie/Logic control reference. `:120` gives relative V-pot values, `:84` LED-ring encoding, and `:281` fader touch messages. It is implementation evidence for a controller family, not the MIDI standard. Device profiles must distinguish absolute values, deltas, thresholds, toggles and momentary buttons. Feedback must preserve provenance so a motor/LED update echoed into an input port cannot create a command loop. Relative deltas cannot be replaced with only the latest packet: summing deltas and replacing an absolute preview are different operations.

## Haptic terminal work needs an actuator channel

A text terminal can be paired with haptics, but no acquired PTY protocol supplies universal actuator control. QMK's `docs/features/haptic_feedback.md` and `quantum/haptic.c` concern keyboard firmware: solenoids, LRA/ERM drivers, waveform selection and press/release feedback. This is tactile confirmation at the device, not proof that a remote TUI command completed. Linux's [force-feedback documentation](https://www.kernel.org/doc/html/latest/input/ff.html) describes capability queries, uploading effects, explicit playback/stop and removal through evdev. Hardware and driver capabilities differ. A cross-platform product would need an explicitly owned adapter boundary.

SmartKnob is tangible UI hardware: a BLDC motor and encoder create software-defined detents and limits. In the acquired `firmware/src/motor_task.cpp:105`, the motor loop owns torque, validation, hysteresis and updates. At `:270` it inhibits torque above a shaft-speed threshold; at `:283` it publishes state separately; it ends iterations with `delay(1)`. These are source mechanisms, not measured hard real-time guarantees. Host-driven rendering must not become the servo clock.

`proto/smartknob.proto:48` preserves integer/fractional position and a press nonce. At `:77`, configuration updates use position-change/idempotency rules and a nonce for forced resets. `firmware/src/serial/serial_protocol_protobuf.cpp:111` acknowledges a received command before passing configuration to the application; receiving an ACK therefore does not prove the motor applied that configuration. State publication is rate-limited. A future adapter needs to distinguish transport receipt, accepted configuration and observed physical state, and define behavior on host pause, stale target, reconnect and lost feedback. The finite press nonce improves recovery from missed state packets but is not an unbounded event log.

Promising Blackbird mappings include a detent per conversation branch or visible item, a finer mode for scroll, pressure/hold to temporarily clutch an encoder to a target, and a short pulse for accepted cancellation. These are hypotheses for the later UX/HCI discussion. Do not equate more vibration with better ergonomics, or execute a destructive command merely because the knob crossed a threshold.

## OSC is useful for expressive surfaces, not a free reliability layer

[OSC 1.0](https://opensoundcontrol.stanford.edu/spec-1_0.html) offers typed arguments, hierarchical addresses, timed bundles and ordered messages inside a bundle. It supplies no clock synchronization. Pattern matches can invoke multiple methods with unspecified match order. Choose a narrow command schema before exposing it; wildcard fan-out and future-dated bundles must not leave cancellation waiting behind scheduled work.

The acquired liblo `lo/lo_lowlevel.h:887` documents a particularly relevant failure: the blocking receive API may never return when there is no incoming message, preventing shutdown. It recommends nonblocking receive/wait. `:970` exposes a descriptor for event-loop integration. Study its parsing/dispatch and bounded work needs before selecting a transport. OSC's transport independence does not promise datagram delivery, deduplication or shared clock correctness.

## Current Blackbird observations and falsification work

Read-only source inspection found the following. No current product test or live device session was run in this research lane.

1. `src/terminal.cpp:861` matches a selected collection of legacy escape strings. Kitty CSI-u events currently fall through to clearing the escape sequence without a corresponding command. This is an expressiveness gap when enhanced mode is supplied; it is not evidence that Blackbird currently requests that mode.
2. `src/terminal.cpp:695` clears a lone Escape only when palette/slash state is open. The event loop sets a 50 ms escape deadline at `:2754`, and polls for pending escape work at `:2599`. Outside those cases, a lone Escape can remain pending and consume the following ordinary byte. Parameter-only CSI runs also stay in the pending string until a final byte. Direct stream cases should characterize recovery and bound behavior before changing the parser.
3. `src/terminal_custody.cpp:241` already clears `ISIG`/canonical mode and `IXON`, so this inspection does not identify Ctrl-S software flow control as a current Blackbird freeze. Nested applications, multiplexers and restored terminal state still require their own cases.

The highest-value next experiments have independent, direct oracles:

| Experiment | Stimulus and oracle |
| --- | --- |
| Byte-stream decoder conformance | Derive expected events from the negotiated protocol. Split every sample at every byte boundary; interleave query replies, UTF-8, paste and focus. Compare identical event sequences, text and state, including malformed/truncated input recovery. |
| Expressive gestures | Compare a simple timed state-machine model against native recognition for overlapping chords, order permutations, deadline boundaries, repeats, missed releases, focus loss and mapping reload. Verify failed-prefix text is neither duplicated nor silently deleted. |
| Cancellation under pressure | Flood terminal output and controller traffic while Lua yields, allocates or fails. Observe native receive, dispatch, cancellation acknowledgment and first presented feedback separately. Bound required service gaps from intended behavior, then measure tails; average frame rate does not answer the claim. |
| Connection lifetime | Disconnect while a pedal or modifier is held, reconnect another identical device, and remove/reload the Lua mapping during an in-flight callback. Check no stuck hold, use-after-free, stale-target dispatch or old-generation feedback. |
| Continuous controls | Replay absolute bank changes with takeover, all selected relative encodings, direction reversals and accelerated bursts. Assert expected final parameter value and preserved button transitions; deliberately echo feedback and require no extra command. |
| Haptic application state | Simulate delayed ACK, rejected configuration, dropped state, nonce wrap and stopped host. Check host displays applied state separately from receipt and that a reconnect cannot revive an old target's feedback. Actual feel and torque behavior need physical hardware later. |
| Terminal path degradation | Replay the same operation through direct terminal, phux and relevant remote paths. Compare advertised capabilities, actual encoded events, restores and reconnect behavior. Make inaccessible bindings observable. |

These experiments should precede selecting a generic keybinding syntax or native/Lua callback ABI. Their purpose is to choose semantics and expose the failure paths that make an interface feel stuck; they are not a request to launch a broad test campaign during source acquisition.
