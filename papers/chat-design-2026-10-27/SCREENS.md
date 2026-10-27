# Readable storyboards — DESIGN ARTIFACTS ONLY

Every screen below is authored text describing **proposed UI**, not a capture, runnable prototype, implemented component or observed usability pass. Illustrative IDs/text/outcomes are fictional; they demonstrate the contracts, not native facts. Rendering widths approximate layouts, not pixel/cell goldens. [Contracts](CONTRACTS.md) and [owners](OWNERS.md) govern every action shown. Focus `>` differs from selected `[x]`; words remain meaningful without color/animation.

## A. Send once, then correct pending intention (C1–C3)

```text
BLACKBIRD / parser repair                     Session detail
MAIN t12 · worker active · phase unavailable    Esc stop t12

YOU m31 · t12 · in context c8
  Diagnose the parser failure; don't change source yet.
  > tool k4  read_file  src/parser.cpp · lines 80–120
    completed · retained snapshot f6          [Original]
BLACKBIRD · streaming preview
  The failure starts at the incomplete escape prefix…

NEXT TURNS (current) 1 runnable
> m32 · queued next turn · “Then inspect the tests.”
  [Edit] [Hold] [Withdraw to drafts]
RECEIPT m32 · accepted locally; request inclusion not yet observed
┌ Draft · harness input ───────────────────────────────────┐
│ Also keep the raw input bytes.                           │
└ Enter send next turn · Ctrl-J newline · Ctrl-G local Back ┘
```

Enter created m32 even if text equals m31. It did not steer t12. Select Edit: native queue atomically holds m32 and saves current unsent draft before loading it. Save failure leaves both untouched. If launch won first, show `m32 launched as t13; correction not applied` and preserve edited text in drafts. Selected editor success:

```text
EDIT NEXT TURN m32 · held · revision 2 (prior text retained)
Target: next main turn, NOT active t12
┌ Then inspect tests, including incomplete mouse prefixes. ┐
[Apply—keep held] [Release selected to queue] [Back]
Main draft saved to drafts d9 · no text submitted by opening editor
```

ESC here requests stop t12, not discard edit. Current strip becomes `Stop requested t12 · next turns held1`. Later `t12 interrupted locally; tool k4 completed earlier`. Release m32 is explicit; old historical queue notices cannot make it runnable.

## B. Stop under ambiguity, blocked owner and PTY input (C3, C6)

```text
MAIN t12 · STOP REQUESTED · next turns held 2
Worker has not returned · last observed: program source retention
Remote stop not established. Earlier effects are not undone.
[Inspect t12] [Next turns]                     Draft preserved
```

If that frontier is unavailable, use `last phase unavailable`, not source-retention guess. This design does not diagnose the composition full-sync failure or alter storage. After completion race: `t12 completed; stop arrived after completion` only with established order. No fake interrupted effect or silent t13 cancellation.

```text
MAIN t14 · command j7 foreground wait
PTY INPUT j7 / epoch 3 · Esc/Ctrl-C go to child
Ctrl-B return to harness; then Esc stop main turn
┌ Private PTY text (escaped output; not a terminal emulator) ┐
│ editor>                                                  │
└─────────────────────────────────────────────────────────┘
```

Ctrl-B backgrounds/releases exact wait; child may continue. Return shows preserved main draft and explicit selected-job Stop option. A child exiting before detach yields actual completion, not retargeting j8. Earlier background jobs and participants are not included in main stop. If terminal output is blocked none of this new acknowledgment is visibly promised before drain; control-ledger/owner result and eventual screen are distinct later oracles.

## C. Tool hierarchy, selected original, return (C4–C6)

```text
YOU m40 · t20 · included in request q18 / attempt a1
  Run the narrow check and explain the failure.
BLACKBIRD
  I'll compare the result with the retained source.
  > k8 exec · python3 check.py …                [Args]
    timed out · local cleanup observed · effect outcome UNKNOWN
    partial capture: 814 retained / 900 received bytes
    head: checking prefix cases …
    … preview omitted … tail: child wait did not return
    [Expand preview] [Original] [Cause]
    controls/read polls 6 · detail under k8, not six peer replies
BLACKBIRD
  The command may already have written its report. Inspect it first.

READING k8 · updates available 3                 [Return live]
┌ Draft ───────────────────────────────────────────────────┐
│ Explain which evidence is missing.                       │
└ Tab focus · Enter preview · i Original (card focus only) ┘
```

No argument wall or collapsed-away unknown/error. `i` binds k8, never latest job. Example after selecting Original:

```text
ORIGINAL / k8 → job j9 → output_ref o9             [Back]
Source retained · resident job retired · controls unavailable
Result | Args | Stream | Cause
> Stream · bytes [256,512) · retained extent 814 · unsealed
Received 900 · not retained [814,900) · more may be unknown
  000256  expected CSI termination …
  000288  \x1b]52;...\x07 (escaped, not executed)
[Previous byte page] [Next byte page] [Hex] [Export exact range]
Copy: safe visible text · exact export is an explicit file action
MAIN t21 active · Esc stop t21; Ctrl-G Back       Draft held
```

Illustrative partial extents must come from owner facts; the UI cannot invent that a received gap is recoverable. A missing result source instead says `original unavailable` with actual cause and known references. Job retirement affects control, not output_ref retrieval. Back returns k8's anchor and same draft while new output remains a badge. Refresh is explicit while reading. No rerun action is involved.

## D. Reading through insertion, reflow and trimming (C5)

```text
READING / k4 source f6 byte 384         7 new updates [Live]
  88  if (prefix == escape) …
> 89  return interrupted;
  90  // Selection: f6 bytes [384,421)
```

Resize changes wrapping, not the source selection. Fold above it doesn't shift to a different byte. If display-only block retires: `k4 display retired; retained source f6 [Open]`, with deterministic nearby anchor rather than jump to tail. If original unavailable, show that loss. The source reader stays pinned; a later result doesn't overwrite selected bytes.

### Narrow (~42×16) — same work, stacked detail

```text
BLACKBIRD · MAIN t21 active
Esc stop turn · harness input
ORIGINAL k8 / Stream               [Back]
bytes 256..512 / retained 814
resident retired · unsealed
> expected CSI termination …
  \x1b]52;...\x07
[Prev bytes] [Next bytes] [Hex]
Copy safe / Export exact…
READING · 7 updates              [Live]
RECEIPT m41 · queued next turn
┌ Draft ───────────────────────────────┐
│ Explain missing evidence.           │
└─────────────────────────────────────┘
Ctrl-G Back · /keys effective controls
```

No empty tasks pane or clipped hidden return instruction. Tiny mode cannot display all details: retain draft/input service and offer resize/native help. Dense wide mode can show context beside result; it doesn't change identity or owner semantics.

## E. Return to colleague, read, direct, return (C7)

```text
CENTER / participants · current session · page 1 / more
Source stale: last accepted observation; refresh pending
> p4 · waiting · direction open · answer available
  p5 · running · cancel requested · remote disposition unknown
Task “diagnose parser” is declared active; not run progress

p4 / answer to request r4 [Summary] [Original]
  The incomplete prefix case differs from malformed CSI…
[Direction] [Await] [Join: seal and drain] [Stop p4]
```

Select Direction pins p4; submitting may refuse if p4 sealed since snapshot. It never falls through to p5. Editor keeps main draft; receipt `message d4 accepted for p4; inclusion not observed`, then actual request/result relation if published. Missing acceptance stays unresolved; same-identity reconciliation only within actual owner's support. Back restores main reading context. Join is not an innocuous refresh: its label states closure of direction admission.

## F. Change instrument, then recover omitted context (C8–C9)

```text
INSTRUMENT / workflow research                  [Source]
Saved root path: /project/turn.lua
Current turn t22 pins generation g7 / retained source s7
Effective registry rev g8 · t22-owned pending candidate g9
> edit buffer b17 / base g8 · operation u5 queued, not staged
[Inspect effective] [Withdraw queued u5] [Back]
Interrupt does not activate a canceled proposal.
```

If t22 activates g9 first, u5 refuses its stale g8 base; b17 is preserved for explicit rebase. If t22 fails, queued u5 remains independent. Once dispatched, u5 stages its own candidate g10 and only its own successful boundary can activate it. Tool-definition operations use their corresponding guarded registry and independent workflow. A successful eligible boundary changes effective revision; next turn's retained source establishes what actually governed it. General Apply-now is not offered. Running executable and selected managed generation appear separately in Session detail; build-source unknown remains unknown.

```text
CONTEXT / effective c18 · change from c17 applied
Summary ancestry: entries e4..e9 [Changed entries] [Original]
Actual request q20 used c18; summary accuracy not established
> Original e6 / retained source s6 / bytes 0..512
  timeout returned partial output; external effects unknown
[Restore e6] [Author guarded repair] [Back]
Repair rejected: base c18 stale; edit retained · effective c19
[Inspect c19] [Rebase deliberately]
Protocol repair is separate: unanswered-call linkage, no replay.
```

Repair does not rerun timeout or settle its effect. Full audit usage is shown separately; context shrink doesn't reclaim it. Exact original can be missing/corrupt; retain known locator and say so. No human prompt must approve every autonomous transform.

## G. Reopen work without imaginary custody (C9)

```text
CONTINUE / parser repair
Draft d9 restored · 2 former queue items recovered, not runnable
Last main t22 failed · 1 prior effect unresolved [Inspect]
No child custody inherited from predecessor process
[Retained conversation] [Recovered drafts] [Session detail]
Recovery fence blocks new effects; no automatic replay.
```

Session detail explains saved/effective source, running/selected generation and actual continuation intent. Ordinary reopen doesn't consume pending resume-continue. A supported fresh independent session is an option, not evidence the predecessor settled. These screens specify later tasks; none is a capture or acceptance claim.

## H. Adversarial crossings added after first-layer pressure test

All frames remain **design artifacts**, not observed screens.

**Completion + queued launch + stop:** decoded ESC admission occurs while t12 is
active: native transition captures t12, holds m32 and closes dispatch together.
Delayed worker notification cannot permit m32 launch. If launch committed before
ESC decoded admission, screen instead says:

```text
MAIN t13 · stop requested (current at decoded admission)
t12 completed · m32 launched as t13 before stop admission
Next turns held1 · m32 no longer editable in queue
Draft retained · stop is not undo
```

If no turn is active at decoded admission: `No active main turn; queue held2`.
Physical keypress/first ESC byte is not a native ordering guarantee.

**Generic call + authored flood + partner answer:**

```text
MAIN t31 completed · effects below remain separately scoped
  k14 native result · completed · fields 28 [Original]
  a7 WORKFLOW OUTPUT · t31 · append stream · preview folded
    { records: [ …
    … 48 KiB preview omitted …
    ] }
    [Preview] original unavailable to this view · raw ref r7
BLACKBIRD / final reply
  The result is incomplete: two records lack the source relation.
  Read the retained call result k14 before choosing another attempt.
READING / final reply · 2 authored updates available [Live]
```

`48 KiB` must be an actual known omission, otherwise say extent unknown. A raw
reference is not falsely enabled as a bounded semantic original. Authored output
stays distinct from tools and ordinary prose; compact generic cards alone don't
qualify this case. A retained adapter later enables actual range inspection.

**Pinned page exhaustion:** `Comparison pins full2/2; release a pin first` leaves
current page/selection unchanged. At return depth8, nested open refuses or offers
explicit Replace detail. If source retires: `Pinned page still visible; refetch
unavailable` distinguishes resident selected bytes from an unavailable original.

**Stale pointer:**

```text
CENTER participant p4 [Join: seal and drain]
  → resize/partial frame; logical list has changed
Click refused: display changing; no Join submitted
Keyboard focus remains p4 · refresh pending · main draft retained
```

No hit test against a new row reinterprets the old coordinate. A coherent emitted
frame is not proof of physical paint; exact owner validation still applies.
