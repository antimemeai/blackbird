# Ultramarine operator-session triage, 2026-10-15

The retained logs identify command-registry admission refusal. The screenshot's
model explanation of interruption during a running build is unsupported.

## Retained session

`/Users/patrickbeam/.local/state/blackbird/session-nIWneY` contains the original
14,497,203-byte audit, diagnostic captures, UI state and raw run
`bb569e1e7cce6a0c95e34064ef`. Session metadata selects provider `paper`, model
`ultramarine-1-flash`, effort `max`, and workflow
`/Users/patrickbeam/projects/blackbird-paper-provider/programs/turn.lua`.
That checkout is absent now. Raw descriptions embed build identifier
`13a19f1daa95dd31c99697a1f7fc037aabaadeb803eec9c138bd42deed5f42ea`.
Do not identify it as current source676120c from the screenshot.

Original screenshot copied unchanged to ignored
`context/ultramarine-triage-2026-10-15/operator-screenshot.png`, SHA256
`b9d1996c00cf87ea50b9ef2a58d150cb39069d567b54a6f8911f0cf1b2815b9c`. Original session files
were only read; no launcher/recovery, provider request or user command executed.

## Direct observations

Read audit frames using their native ARJ1 framing and existing development BBM2
decoder (`scripts/native-packet.py`). There are2434 frames ending exactly at
14,497,203. Command inputs number34; command33 and34 return `capacity`, detail0,
at audit sequences2294 and2422. Both record `capacity.stop` with
`unretained_bytes=0`; that does not demonstrate loss of earlier originals.

Read raw ARJ1 records with the session's own descriptions: origin32 bytes,
envelope16 bytes, then site-specific native fields. Site2000 has34 start
requests. Caller variant2001 reports32 admissions and2 Error refusals.
Site2002 has32 worker launch requests. Refusals are:

| Attempt/job | Raw file / byte offset / sequence | Site2001 ordinal | Error / max_live / max_records |
| --- | --- | --- | --- |
| 85de7df7981a5bf08803000000000000 | events.3 /112 /1 | 847025 | capacity(0) /8 /32 |
| 85de7df7981a5bf0bc03000000000000 | events.5 /112 /1 | 113336 | capacity(0) /8 /32 |

No configure/archive event precedes either refusal. The sole2045 event is later
orderly shutdown. Both failed attempts are refused before worker launch; neither
build command was observed running. This is a command-record limit, with no
basis here for attributing the failure to provider context/token capacity.

Current source35c81f1 provides the matching mechanism:
`src/command_jobs.cpp` defaults max_live8/max_records32, refuses a full registry
before insertion/worker construction, and releases completed records through
explicit archive. `src/coding.cpp` classifies exec capacity as unknown, emits
capacity.stop, and renders unknown as stopped. The retained model-visible stop
linkage additionally says turn_stopped/consult audit. The actual model response
then asserts interruption during build and denies capacity before retrying.
That retry also fails admission. These observations identify the admission
failure and misleading outcome classification, without establishing whether the
model's inspection query omitted an available fact or the interface hid it.

## Remaining work

arconaut-zh2 tracks sustained exec record management, truthful pre-launch refusal
classification, and human/model failure detail. Study admission ownership and
retained-original query behavior before designing the repair. Raising the bound
alone merely postpones this failure.

arconaut-6ao tracks the independent visible pane corruption. The screenshot shows
vertical stray glyphs around the pane divider. Root cause remains unresolved;
reconstruct geometry and emitted output from retained raw UI events before
attributing it to layout, Unicode width, partial output or a stale build. No
claim that earlier presenter work fixes this run.
