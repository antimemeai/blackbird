# BHI: programmable agency over legible, continuing work

2026-10-15. Independent research and theory contribution. This is input to the
interaction theory, not an implementation plan. It does not amend the current
core contracts. BHI means bot-harness interaction in the operator's broader
sense; ACI, agent-computer interface, is the useful existing search term.

Read against [FOUNDATION](../../../docs/FOUNDATION.md),
[CORE_DESIGN](../../../docs/CORE_DESIGN.md),
[CURRENT_STATE](../../../docs/CURRENT_STATE.md), the prior
[ergonomics report](../../tui-2026-10-15/ergonomics/REPORT.md) and
[TUI synthesis](../../tui-2026-10-15/SYNTHESIS.md). The older implementation's
restricted workflows and fixed turn loop have no design authority here.

## The proposition

Blackbird should let an expert human and model colleagues perform continuing
work through different interfaces to identifiable objects, executable programs
and inspectable effects. Each participant must be able to determine what a
relevant observation establishes, direct an operation at its intended target,
learn what happened or remains unknown, and change the course of work without
having to reconstruct the environment from conversation alone.

This is a theory of the joint working arrangement. The human, model calls,
programs, retained observations, working context and evolving artifacts all
matter. An isolated model response or an isolated screen is an insufficient unit
for judging whether the arrangement works. The interfaces need compatible
meanings, not identical sensory or cognitive demands. The human can inspect a
spatial overview and manipulate a controller; a model can author a program that
queries many tasks and emits a concise diagnosis. Both must be referring to the
same work when they say which task, command, file revision or colleague they mean.

Normative commitments below come from the operator's aims and existing design.
Mechanism claims are narrower conclusions supported by the readings. Predictions
are proposals that could be wrong. Keeping these distinct prevents a desired
interface quality from masquerading as an experimentally established result.

## What the primary sources establish

**SWE-agent**, Yang et al., [arXiv v3, 2024-11-11](https://arxiv.org/abs/2405.15793v3),
read sections 2–5 and appendix B.1. Its four ACI principles concern understandable
actions, compact efficient actions, concise informative feedback, and error
recovery. Table 3 uses GPT-4 Turbo on SWE-bench Lite: summarized search resolves
18.0%, iterative search 12.0%, and no search 15.7%. The authors observed exhaustive
next-result loops. A 100-line viewer outperformed both 30 lines and entire files;
last-five observation context outperformed full history. These are comparisons
within one system, task distribution and historical model configuration. They
support treating action and observation design as causal variables; they do not
select Blackbird's command count, read size or context policy. Lint rejection is
an edit-recovery mechanism, not evidence for operator approval dialogs. The
37-instance development sweep and 300-instance Lite ablations do not establish
universal parameter choices. Original PDF is reused from the previous corpus.

**CodeAct**, Wang et al., [arXiv v4, ICML 2024](https://arxiv.org/abs/2402.01030v4),
read sections 2, 5 and limitations. The action comparison isolates Python code,
text and JSON formats. M3 ToolEval has 82 curated tasks, 17 models, exact-answer
checking and a maximum of ten interaction turns. Code actions achieve the best
success and lowest average turns for 12 models; results are not universal. Its
atomic API experiment even gives several closed models their best result with
JSON. The relevant mechanism is composition through control and data flow,
combined with executable feedback. The paper does not establish Lua's relative
usability, unlimited scripts' superiority, or a need to change Blackbird's chosen
production languages. Ten-turn evaluation makes saved round trips particularly
valuable. Natural-language collaboration and self-debug examples illustrate
possibility; they are not a systematic human-collaboration study. The limitations
also name imagined variable contents: programmability does not make observation
unnecessary.

**ReAct**, Yao et al., [ICLR 2023 camera-ready v3](https://arxiv.org/abs/2210.03629v3),
read sections 2–4 and appendix A.3. Interleaved environment interaction and
reasoning improve results against action-only comparators on its tasks. However,
ReAct slightly trails chain-of-thought on HotpotQA while outperforming it on
FEVER. The manually classified HotpotQA failures include repetitive action loops
and uninformative search responses that derail recovery. Thus environmental
feedback helps under conditions; simply increasing feedback or enforcing an
interaction rhythm is no general remedy. The human thought-editing result in
A.3 is an illustrative ALFWorld correction, explicitly left for systematic future
study. It neither establishes a general collaboration benefit nor requires
access to private model reasoning. Blackbird's inspectable objects can instead
include emitted plans, programs, requests, results and context revisions.

**Lost in the Middle**, Liu et al., [TACL 2024, pp. 157–173](https://aclanthology.org/2024.tacl-1.9/),
read sections 2–5. Controlled position and length manipulations on multi-document
QA and synthetic key-value retrieval show that accepted context is not always
usable context. Tested systems include MPT-30B, LongChat-13B, GPT-3.5 versions
0613 and Claude-1.3; most experiments use greedy decoding. Model, task and position
matter. Placing the query before and after the material helps synthetic retrieval
more than QA. This supports treating observation arrangement as a real variable,
not merely fitting bytes under a context limit. It does not show current models'
positional behavior, measure Blackbird's CLM, or establish a universal shorter-
context policy. The transfer is a question to investigate, with recoverable
originals allowing correction when a derived context proves inadequate.

These sources were chosen for discriminating mechanisms, not as an exhaustive
survey of current agent performance. See [MANIFEST.tsv](MANIFEST.tsv) for retained
bytes and restoration commands. No acquired code was executed or dependency
selected.

## Theoretical commitments for Blackbird

### 1. Share referents, permit asymmetric representations

An actionable target has an identity and situation: which object, environment,
revision or incarnation, actor and operation. Human focus, model references and
Lua handles should resolve consistently where they express the same operation.
Screen coordinates, list rows and an unqualified remembered process name are
weak substitutes when the world changes underneath the participant.

This does not require exposing every native structure to every participant or
forcing all modalities through one universal command syntax. The relevant
agreement is semantic. A model inspecting task B and the human stopping task B
must agree about B, which effect was requested and its state. Presentation can
retain modality-specific strengths. The shared-service boundary remains intact:
controlling a consumer's invocation does not confer governance over the service.

### 2. Keep the world, its observations and working context distinct

An observation says what was captured through a particular interface at a
particular time. It does not establish everything about the world now. A model's
summary is a derived interpretation; context is the material chosen for a request;
an external file or service can change after both were formed. Retaining exact
originals enables repair but does not make the live world fully observable.

The interface must distinguish observed state from inferred, stale, provisional,
missing or unknown state when that distinction could change an action. This
includes gaps in capture. The human and models need routes from a concise view
to the pertinent retained source, and, when necessary, to a new observation.
Shared objects do not imply shared private contexts or instantaneous common
knowledge among colleagues.

### 3. Let actions obtain closure without manufacturing certainty

An accepted request, a dispatched effect, ongoing work and a settled result are
different things. Meaningful feedback identifies which transition was observed,
which object it concerns, what can be done next and what remains unresolved.
Cancellation acknowledgment is not proof of termination; a failed response is
not proof that an external effect never happened. Retrying an uncertain effect
requires the operation's actual replay semantics, not optimism.

Closure means that an operation can be followed to its current known disposition,
including pending or unknown. It does not mean demanding a terminal answer before
another action, inventing atomicity for a script, acknowledging every keystroke to
a model, or inserting routine confirmations. In a useful asynchronous workspace,
many loops overlap, and one colleague can keep working while another waits.

### 4. Make expression compositional and consequences inspectable

The model's action vocabulary includes programs. It should be able to discover
operations, construct its own compositions, select observations, transform its
context and revise its working programs during ordinary work. This is a product
commitment, not a reward granted after an autonomous mode is enabled.

Readable small operations and powerful composition are complementary. A program
can express iteration, branching, batching or waiting without asking the model
to retranscribe every intermediate value. But collapsing several effects into a
single model call must not collapse their identities, erase partial failure or
claim an unobserved rollback. Syntax/type errors should describe what was rejected;
execution errors should identify which work may have happened. The model can
then repair a composition rather than restart the entire task blindly.

Existing deferred/interrupt activation semantics still govern replacement of
running programs. Context editing, program replacement and native refit have
different consequences; an interface must preserve these distinctions while
making them usable. No new approval gate follows from the need for legibility.

### 5. Compress for the next decision, retain paths to detail

A useful observation contains the distinctions needed for the participant's next
decision and enough context to interpret them. It may be a summary with references,
a precise excerpt, a query over many operations or an exact original. The correct
amount depends on the task and participant. Fewest tokens, most visible cells and
most output are each inadequate objectives.

In particular, a limit should state that material was omitted and how to obtain
it. A reference should let the participant retrieve the intended snapshot, not
silently substitute a changed file. A summary should not present selected matches
as an exhaustive search. The model's own context program can choose more detail
when a hypothesis requires it. This is compatible with exact retained audit and
with economical default observations; those serve different purposes.

### 6. Preserve continuing work, rather than a fiction of uninterrupted cognition

Model requests do not establish a continuously thinking human analogue. What the
harness can preserve is concrete: participant and operation identities, drafts,
program generations, context revisions, available originals, pending messages,
known and unknown obligations, and supported continuations. This is the basis
for a useful handoff after waiting, compaction, disconnection, restart or refit.

A successful continuation can re-establish the purpose, relevant state, currently
operative definitions and unresolved effects without treating a stale summary as
the entire world. Re-explaining all background in conversation is costly and
error-prone; retaining every prior message in every request is also not a theory
of good continuity. Blackbird's editable context and original recovery provide
materials from which the participant can construct its next working view.

### 7. Respect different costs while preserving joint steerability

Humans pay attention, resumption and motor costs. Models pay input, generation,
tool-round-trip and context costs, with model-specific behavioral failure modes.
Programs and services have their own latency, load and failure boundaries. There
is no reason these costs collapse into one universal responsiveness score.

Fast human control and efficient model action can coexist with a detailed retained
record. Native control must remain able to admit a stop while optional presentation
work is overloaded; the model may prefer a coalesced operation summary and a
bounded wait instead of an event per output fragment. A human needs a stable target
and feedback that the stop was accepted. A colleague needs an operation reference
and a truthful disposition. A haptic pulse can indicate acceptance or completion
only according to its defined meaning; physical immediacy cannot supply missing
knowledge of an external effect.

## Predictions and ways this theory could fail

These are discriminating implications, not a benchmark campaign or implementation
sequence. They explain what would count against the proposed mechanisms.

| Proposed mechanism | Prediction | Counterexample or limitation that matters |
| --- | --- | --- |
| Shared identity across representations | Human/model handoffs during reordering produce fewer mistaken-target actions when targets retain identity. | Stable identifiers may be hard to discriminate, or harmful if models copy the wrong one. Identity is necessary to define the target, not sufficient to make selection usable. |
| Composable actions | A model can perform a repeated query/transform with less retranscription and fewer model round trips than a forced sequence of atomic calls. | A long program can compound errors, hide intermediate discoveries or cost more to repair. Tasks requiring unpredictable inspection can favor smaller steps. |
| Decision-relevant observations | A structured summary plus reachable originals can support correct next actions with less irrelevant material than an undifferentiated dump. | The summary may omit the one decisive clue, references may add retrieval overhead, or the model may misjudge when to inspect further. More detail can be better. |
| Explicit effect disposition | Participants can distinguish pending, stopped and unknown effects and choose a legitimate next action. | Status proliferation can obscure the important distinction. Truthful labels without usable explanations can leave the system formally explicit and practically opaque. |
| Recoverable context | A participant can repair an inadequate context from originals instead of repeating already observed external work. | Original recovery can be too expensive or miss uncaptured state. A reconstructed context cannot restore a dead heap or undo a past action. |
| Independent control progress | A stop can be admitted while display or optional Lua work is stalled, and the participant can observe the actual subsequent disposition. | Acceptance can progress while effect termination remains blocked. Independent input alone fails the proposition. Arbitrary external services need not be interruptible. |

The theory also fails if it treats every task as a rapid sequence of local
choices. Planning, contemplation, long computations, unattended work and building
new programs all legitimately delay immediate feedback. The stronger claim is
that participants can recover orientation and intervene at meaningful boundaries
when they need to. The harness should not force constant interaction to prove
that interaction is possible.

## What remains open

No paper here identifies Blackbird's correct screen layout, command grammar,
controller mapping, observation size, latency budget or model-context strategy.
The theory offers reasons to compare those choices, not their answers. Current
models' abilities and available provider semantics require current observations;
the historical studies supply failure mechanisms and methods rather than fresh
rankings. The most material unresolved issue is how to combine rich programmability
with concise, truthful partial-failure and continuation explanations without
forcing every participant to learn the core's implementation.

See [SHOPPING_LIST.md](SHOPPING_LIST.md) for the remaining evidence gaps. Existing
core contracts supply concrete semantics where already specified; this proposal
adds a rationale connecting human presentation, model interfaces and continuing
work rather than replacing those contracts with an abstract interaction loop.
