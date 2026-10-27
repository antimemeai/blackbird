## Consequential review

**Disposition: remediate the findings below before treating this as the completed conceptual unit.** The study has a substantive task analysis and useful reference tradeoffs; it is not merely a link catalog. Its strongest contributions are the distinction between receipt and execution, the observed counterexample to “folding is absent,” and the preservation of the composition failure’s unknown causal frontier.

This review uses **only the supplied documents, source excerpts, screen projections and event records**. I did not inspect files, execute anything or independently verify the referenced archives or raw captures.

### 1. High — The duplicate-delivery requirement implies a guarantee beyond the established owner model

**Location:** `REQUIREMENTS.md` → **R2: Every deliberate send remains identifiable**, particularly:

> “A transport duplicate/receipt must not create another effect.”

**Finding:** This conflates duplicate local submission handling, duplicate receipt presentation, provider request retransmission and external-effect execution. The study correctly rejects content deduplication, but it has not established a transport identity, receiver protocol or effect owner capable of guaranteeing the quoted statement. Read as a general requirement, it can invite unsafe retry logic or an unsupported exactly-once promise.

The two observed equal-text submissions establish **two deliberate actions executing separately**. They establish nothing about retransmission recovery.

**Specific remediation:**
- Separate deliberate submission identity, duplicate receipt/event processing, provider attempts and tool/effect identity.
- Require duplicate presentation events not to manufacture a new local intention.
- State that preventing repeated external effects requires an identified owner/protocol; otherwise an uncertain attempt remains unknown and must not be implicitly redispatched.
- Add a semantic obligation covering a lost acknowledgment after possible admission: preserve the attempt and its uncertainty rather than infer “not sent” or automatically resend.

This is a conceptual correction, not a request to select an implementation protocol.

### 2. High — The executable/source association is carefully qualified in the inventory, but causal wording elsewhere exceeds it

**Locations:**
- `INVENTORY.md` → **Studied source versus installed and running code**
- `ACTUAL_USE.md` → **Episode 1**, “The source explains why”
- `ACTUAL_USE.md` → **Classification**, “Lone-ESC decoder recovery — Present”
- `REFERENCES.md` → **Challenge to existing interaction theory**, “healthy escape recovery”

**Finding:** The inventory honestly says the installed build-source revision is unknown. Matching three terminal-related files between two repository revisions does not establish that the installed executable was built from those bytes, especially with unknown build-time modifications.

The reproduction establishes that **one bare Escape did not produce observed interruption during the stated dwell**. The supplied source establishes that the studied `flush_escape()` does not produce a cancellation action. Together they support a consistent explanation, not a proven installed-code causal trace. Nor does this scenario qualify the broader lone-ESC recovery behavior: subsequent successful input does not exercise all split-prefix, mouse-tail or decoder-recovery cases.

**Specific remediation:**
- Replace “The source explains why” with “The studied source provides a consistent explanation; installed source parity remains unproven.”
- Split the classification into:
  - studied-source fact: bare-ESC flush closes local layers and clears buffered escape state without emitting cancel;
  - direct observation: no interruption acknowledgment during this reproduction;
  - historical claim: lone-ESC repair was previously delivered.
- Remove “healthy escape recovery” as a runtime qualification, or narrow it to the precise observed input sequence.

Keep the pinned executable hash and the unknown build-source association exactly as they are; no rebuild or provenance repair is needed for this correction.

### 3. High — Several broader tasks are inventoried but do not yet have whole-task acceptance obligations

**Locations:**
- `REQUIREMENTS.md` → walkthrough rows **Delegate and come back**, **Change settings or program while active**
- `REQUIREMENTS.md` → **R10** and **Direct obligations**
- `STATE_CONTROL_MAP.md` → **Effective input routing and focus**, result lab and authored composition references

**Finding:** The study gives send/stop/command inspection substantially complete episodes. Delegation, program change and context repair largely remain capability names or source/history summaries. That leaves consequential relations unspecified:

- How does an operator return to delegated work, distinguish waiting-for-direction from finished, inspect stale observations, address the intended participant and know whether a direction was accepted?
- How does an operator identify pending versus effective program changes, interrupt without implying activation, recover from rejected activation and establish which generation governs the next turn?
- How does an operator understand and recover a model-authored context change while distinguishing working-context edits, retained originals, protocol repair and physical audit storage?

The historical digest itself identifies these as central expert-operator activities. A generic “continuity/recovery” obligation does not cover their complete decision loops.

**Specific remediation:** Add a compact episode/obligation for each of these three tasks, identifying:
1. operator intention and target;
2. actual owner and supported transitions;
3. visible acceptance/effect evidence;
4. failure or stale/unknown state;
5. safe continuation and return to the main task;
6. what remains unobserved.

These may remain source-grounded requirements. They do not require new runtime experiments or selection of a dashboard, framework or binding.

### 4. Medium — Child-lifetime conclusions exceed the supplied direct evidence

**Location:** `ACTUAL_USE.md` → **Episode 2**, especially:

> “the private child ends normally”

**Finding:** The child ledger directly establishes receipt of `18 1b 03`. The screen establishes `exec backgrounded` and turn completion. The supplied event record establishes the native UI’s exit code, not the child’s independently observed exit status.

The child program is expected to finish after its sleep, but elapsed time plus UI exit is not evidence of normal child settlement. This distinction matters because the episode’s central lesson is that turn completion, attachment and child lifetime have different owners.

**Specific remediation:**
- Either cite an already-retained native job disposition/exit record establishing normal child exit, or change the statement to “the fixture was written to finish normally; independent child exit disposition is not supplied here.”
- Label “backgrounded” as an observed operation result, not evidence that the child was still running at the later screen.
- Keep the literal-input ledger as the direct evidence for routing; it is strong and does not need a broader lifetime claim.

### 5. Medium — “No model request” needs an explicit evidence basis, separate from launch configuration and isolation

**Locations:**
- `ACTUAL_USE.md` → **Method and evidence custody**
- `INVENTORY.md` → private reproduction launch description
- `README.md` → **What this does not establish**

**Finding:** `/lua` dispatch and the supplied inline programs support a source-based expectation of no inference. The screens and event records do not independently establish absence of provider calls. A fresh `BLACKBIRD_AUTH_HOME` establishes an isolation measure, not by itself the absence of other credential paths or network activity.

This is not evidence that an unintended request occurred. It is an attribution gap in an important observation-safety claim.

**Specific remediation:** State which basis supports the claim:
- direct inspection of retained provider-attempt/session records, with exact locator; or
- source-path inference from local `/lua` execution and fixture contents, with the absence of an independent request/network observation explicitly stated.

Also distinguish “account selection was isolated by configuration” from “all possible authentication paths were independently excluded.” Do not add a network probe or expose credentials to close this documentation gap.

### 6. Medium — Error diagnosis needs to distinguish a recorded cause from a cause merely embedded in submitted source

**Locations:**
- `ACTUAL_USE.md` → **Episode 2**, Lua failure
- `REQUIREMENTS.md` → **R7: Diagnose at the right level**
- `STATE_CONTROL_MAP.md` → **Failed** and **Lua extension/render failure**

**Finding:** The supplied screen proves that the summary is unhelpful and that the submitted source contains `READ_ONLY_FAILURE`. It does not establish that this error text was retained as the actual runtime cause or that a human-accessible path to it exists. R7 partly acknowledges this with “or state that it is unavailable,” but surrounding references to “its recorded cause” can imply that only a missing UI link remains.

For an operator recovering from failure, these are different problems: hidden cause, discarded cause, failed cause retention or unknown failure provenance.

**Specific remediation:**
- Mark the actual retained runtime-cause availability as unverified unless an existing audit locator establishes it.
- Extend the diagnosis obligation to distinguish available-linked, available-but-not-linked, unavailable and retention-failed causes.
- Preserve `external_unknown (0)` as the observed diagnostic without promoting it into either external-effect uncertainty or a verified error-classification defect.

## Reference-study assessment

The supplied reference synthesis **does inform concrete tradeoffs**: Pi’s local expansion versus transcript flooding, Codex’s pending identity versus content matching, Crush’s working-tail/settled-head transition versus reading stability, and libvaxis’s separate reader versus queue pressure. The HCI material is applied to identifiable operator judgments rather than used as brand authority. No framework selection follows from it.

Its evidential limit remains important: I received the manifests and interpretive summaries, **not the seven reference source files or original publications**. Their hashes and archive-match assertions are reported custody evidence, not something this review independently verified.

## What remains unavailable

- Exact installed build-source commit/tree and dirty-source state.
- Original reference implementations, archives and HCI originals for independent comparison.
- Raw terminal streams, observation drivers and native session/audit originals; only their supplied projections and records were reviewed.
- Live operator inference, provider retry/cancellation and remote-effect settlement observations.
- Independent child exit disposition in the supplied evidence.
- Actual generic-tool flood from the operator’s reported episode.
- STATUS, JOURNAL, HANDOFF, final publication commits and remote-ref evidence; completion/publication cannot be assessed here.

The composition diagnostic’s kernel-versus-scheduling uncertainty, failed **309/310** gate, and cancellation-not-undo semantics are preserved appropriately. None of these findings calls for a source gate, sibling merge, storage repair or implementation.

**Remediation scope:** correct the attribution/guarantee wording and complete the three missing task episodes, then use the permitted **one focused findings recheck** to assess those changes and their direct consequences. This review supplies neither design approval nor a claim that chat is fixed.
