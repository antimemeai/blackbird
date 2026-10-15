# Human interaction: a theory of working through representations and instruments

2026-10-15. Independent HCI research and proposal for the interaction-theory
synthesis. This report proposes explanations and discriminating observations;
it does not select a layout, keyboard grammar, controller, runtime boundary, or
implementation sequence. The repository's existing product contracts still
govern. Source acquisition is recorded in [MANIFEST.tsv](MANIFEST.tsv).

## Claim

Blackbird should let an expert and their computational colleagues create,
inspect, change, and resume useful work through representations and instruments
that remain dependable while other work proceeds. Dependability includes
knowing what a representation refers to, what acting on it means, what actually
happened, and how to return to an interrupted line of work. The representations
must also be material the participants can think with: compare, arrange, mark,
filter, reinterpret, and transform.

The observe–target–act–feedback loop names a necessary relationship, but does
not explain the whole interaction. An operator may rearrange two results to
understand a discrepancy without changing either result. They may entrust a
workflow to a model and intentionally stop watching it. They may invest in a
custom instrument that makes a new kind of work possible. A theory that counts
only effectful commands, continuous monitoring, or immediate feedback misses
these cases.

This proposal therefore treats **acting on the work, arranging representations
to understand the work, delegating the work, and changing the instruments of
work** as related forms of interaction. These are analytic distinctions, not a
proposal for four product modes.

## Evidence read

These are primary theoretical works, with examples and studies of varying
kinds. They do not establish Blackbird's performance or an experimentally
validated theory of human–LLM collaboration. The Blackbird deductions below
are our hypotheses and design judgments.

### Distributed cognition

Hollan, Hutchins, and Kirsh, *Distributed Cognition: Toward a New Foundation for
Human–Computer Interaction Research* (2000). Read the searchable author
revision, especially sections 1–3 and 3.2–3.6; the existing acquisition is a
large scanned journal copy. The additional revision is dated April 23, 2000,
and has different pagination; citations here use its section numbers.

The account chooses units by functional relationships, including people,
representations, artifacts, and earlier activity. Its cockpit example explains
why displaying the same numeric variable can lose useful perceptual
relationships. Sections 3.3–3.6 distinguish manipulating representations for
their own cognitive uses from manipulating their referents, and discuss
interaction histories and spatial organization. The proposed research framework
connects theory, observation of actual work, experiment, and design; it does
not substitute a universal interface recipe for that investigation. This
supports studying an entire work episode and the representations it uses.
Extending the analysis to LLM participants does not establish that their
mechanisms resemble human cognition. [Author revision](https://hci.ucsd.edu/hollan/Pubs/tochi-revised.pdf).

### Directness, skill, and expressive language

Hutchins, Hollan, and Norman, *Direct Manipulation Interfaces* (1985),
*Human–Computer Interaction* 1:311–338. Read sections 2–6, particularly 3.1–3.4
and 6; page numbers below are printed journal pages.

Semantic distance concerns how well interface expressions match intended work;
articulatory distance concerns the relation of an expression's form to its
meaning. Sections 3.3 and 6 separate fluency acquired through practice from a
good semantic fit. The discussion of virtuosity shows why directness depends
on the actual task: producing an isolated musical note differs from expressive
performance. Section 6 argues that repetitive tasks, variables, classes, and
precision can favor symbolic descriptions and scripts. The paper therefore
supports examining both rich instruments and composable languages. It does
not establish that direct manipulation is always superior, or that a familiar
interface covers intentions its vocabulary cannot express. These distinctions
are particularly useful for assessing expert customization without mistaking
memorized workarounds for successful design. [Author-hosted article](https://hci.ucsd.edu/hollan/direct-manip.pdf).

Section 2.2 also allows words and descriptions: the critical relation is that
output objects can enter subsequent input. Section 4 makes this
inter-referential relation central to engagement. This suggests investigating
addressable results in the model interface, without presuming a synchronous
model world for external effects.

### Instruments and their feedback

Beaudouin-Lafon, *Instrumental Interaction: An Interaction Model for Designing
Post-WIMP User Interfaces* (CHI 2000). Read the entire author preprint,
particularly printed pp. 3–7; its first page says publication is forthcoming.

The model distinguishes domain objects from instruments mediating action.
An instrument's immediate reaction is distinct from feedback about the object.
It examines activation costs, physical and logical components, and instruments
that modify other instruments. It offers indirection, integration, and
compatibility as comparative properties rather than an absolute quality score.
Its mixing-console example explicitly explores dedicated controls as an
alternative to repeatedly activating a shared device. The generative example
turns search-and-replace into an instrument. The paper supplies a vocabulary
for evaluating MIDI, keyboard, or scripted instruments without assuming
everything should emulate a keypress. Its GUI analyses do not validate any
particular controller for coding work or make its historical efficiency
comparisons Blackbird measurements. [Author preprint](https://www.lri.fr/perso/~mbl/papers/CHI2000/paper.pdf).

### Space as an active resource

Kirsh, *The Intelligent Use of Space* (1995), *Artificial Intelligence*
73:31–68. Read the author-hosted HTML introduction and selected passages in
sections 2–6, including its
counterexamples about ordering, meaningful location, and representation costs.

Kirsh classifies arrangements that simplify choice, simplify perception, or
reduce internal computation. Examples include cooking, assembly, packing,
and Tetris. Structuring an environment has costs: a simple line may be
insufficiently expressive, an arbitrary location may be difficult to remember,
and elaborate arrangements can defeat easy interpretation. Section 5 discusses
actions undertaken to obtain useful perceptual information while reasoning.
This grounds an investigation of pinning, arranging, comparing, and exposing
information as work in its own right. It does not supply a screen-density
optimum or a universal latency threshold; its reported Tetris timings concern
those tasks and participants. The transfer to Blackbird is a hypothesis about
how working representations might reduce reconstruction and comparison effort.
[Author HTML](https://adrenaline.ucsd.edu/Kirsh/Articles/Space/AIJ.html).

### Changes have cognitive costs

Green and Blackwell, *Cognitive Dimensions of Information Artefacts: A Tutorial*
(version 1.2, October 1998). Read selected sections of Part 1, especially printed pp. 5–10, 12,
17, 21–25, 29–35, and 39–43. This is an original author tutorial rather than
the Green–Petre 1996 journal treatment.

The framework describes tradeoffs rather than maximizing every dimension.
It distinguishes resistance to change, hidden dependencies, forced early
choices, abstraction costs, visibility, juxtaposition, and secondary notation.
The abstraction manager is itself a device requiring usable controls.
Exploration differs from transcription because its desired outcome may not yet
be known. Typography, grouping, and annotation can carry information beyond
formal syntax. The tutorial explicitly sets aesthetic and emotive aspects
outside its analytical scope; it does not imply they are unimportant. This
supports evaluating customization together with the costs of understanding
and maintaining it. It does not justify turning dimensions into one numerical
usability score. [Author tutorial](https://www.cl.cam.ac.uk/~afb21/CognitiveDimensions/CDtutorial.pdf).

## Proposed unit and constructs

The unit of analysis is a **situated work episode**: participants pursuing or
revising a purpose through work objects, representations, instruments, programs,
and retained history over an interval. Its boundary follows what contributes
to understanding and changing the work. An editor, external process, remote
service, or returned research report may belong in that analysis even though
Blackbird does not govern its lifecycle. A turn, frame, tool call, and process
are useful narrower units for particular fault mechanisms.

| Construct | Concrete meaning and observable signs |
| --- | --- |
| Purpose | What the participant is trying to achieve or learn; it can change during exploration. Ask what uncertainty a view change or action was resolving. |
| Work object | The particular workflow, participant, artifact, operation, context revision, or external resource involved. References must preserve the distinctions the task requires. |
| Representation | A view, summary, original result, annotation, or selection through which an object becomes available for thought and action. Identify what it includes, omits, and may lag. |
| Instrument | A command, key sequence, query, Lua program, knob, selection tool, or other means of operating on objects or representations. Distinguish activation, input, reaction, and observed effect. |
| Binding | The current relation between instrument, target, operation, and scope. A selection's color cannot alone establish which target an action will address. |
| Effect knowledge | What the participant can know about requested, admitted, ongoing, completed, failed, or unresolved effects. These categories need not be identical for every operation. |
| Continuity | The ability to retain or recover the task-relevant target, draft, comparison, reading position, and decisions across change. Continuity does not mean every pixel remains fixed. |
| Attention and effort | Work spent finding, interpreting, remembering, switching, monitoring, configuring, correcting, and resuming. Fewer keys or more visible data can increase other effort. |
| Appropriation | Participants changing instruments or representations to fit emerging work. Observe whether the new method is understandable and revisable after its creator moves on. |

Human/model shared reference should concern the work object and actual effect,
not identical presentation. A human may use spatial juxtaposition; a model may
need a compact structured relation and addressable originals. Applying human
embodiment claims to the model would exceed this evidence.

## Mechanisms proposed for Blackbird

The following are causal hypotheses, not findings from a Blackbird experiment.

**A representation can remove reconstruction work.** If a participant can keep
the failed operation, its originating request, and its exact output in a useful
relation, they need fewer memory-dependent joins to diagnose it. If a new
layout hides that relation, all three facts may remain present while diagnosis
becomes harder. This makes relationship preservation a stronger explanation
than raw data quantity. More information helps when it exposes a useful
distinction; otherwise it can increase search and interference.

**Stable reference allows skilled action while the world changes.** If the
participant starts an action on a particular object, unsolicited output should
not silently change the referent of that action. This does not require permanent
screen coordinates. A live overview may reorder useful information while an
active selection remains attached to identity and its consequences remain
inspectable. Conversely, keeping coordinates fixed is insufficient if the
object behind the coordinates changes.

**Feedback supports adjustment only when its meaning is credible.** A quick
reaction to a control can aid fluent use; it cannot establish completion of an
external operation. A pulse on a controller might acknowledge input, admission,
or observed completion, and those messages must have distinguishable meanings.
If the interface repeatedly displays intent as accomplished effect, the
participant must acquire extra evidence elsewhere, undoing the supposed
speed advantage. Honest pending or unresolved state can be more usable than a
confident but false success indicator.

**Expression changes the amount and kind of work a participant can attempt.**
The useful vocabulary must permit common intentions without forcing repeated
low-level choreography, while allowing new combinations. Selection, targets,
parameters, queries, named operations, scripts, and continuous values are
different resources for expression. Their relation matters more than shortcut
count. A controller that merely triggers menu commands might provide little
advantage; one that supports an appropriate continuous investigation could
change the task itself. Which investigations merit that capability remains open.

**Reliable timing makes a learned instrument worth relying on.** If equivalent
gestures have unpredictable effects or feedback, the participant must monitor
and repair each use rather than develop an economical routine. Response should
arrive while it is useful for the intended adjustment. This supplies no
universal millisecond budget: browsing evidence, making a discrete selection,
scrubbing a history, and steering external work have different timing demands.
Low mean latency cannot alone establish a dependable instrument.

**Representational actions support exploration without requiring effectful
commitment.** Comparing, annotating, filtering, and provisionally grouping can
expose patterns before the participant knows what change to make. Forcing an
early taxonomy, workflow decomposition, or operational decision can make the
participant keep unresolved relationships in memory. A good interface permits
incomplete understanding to become useful material. This is compatible with
precise execution semantics: tentative organization must be distinguishable
from changing the underlying work.

**Delegation can preserve agency while reducing active control.** Agency here
means the practical ability to pursue and revise purposes, including choosing
what to entrust to another participant. It does not require deciding every
operation or reading every event. Delegated work can continue outside focal
attention when its results, salient failures, and means of intervention remain
available. Maximizing streaming visibility or routinely asking for approval
can make the human the bottleneck and violate Blackbird's product aims.

**Changing an instrument is itself work.** Lua definitions, bindings, views,
context transforms, and workflow programs become objects of interaction.
Participants need to understand the active definition, their pending edit, and
which work will use it. Convenience in writing a new definition does not
eliminate the costs of discovering conflicts, maintaining it, or recovering a
broken interaction. The existing deferred/interrupt activation rule supplies
one part of this meaning; a new interface must preserve it.

## Normative aims and limits

The operator's aims supply expert agency, programmability, ambitious expression,
precise presentation, useful density, near-absent routine approval friction,
and the ability to develop Blackbird within itself. These are values and
product commitments; a literature citation cannot prove them optimal for every
user. The theory explains what may help or obstruct them in this setting.

Density should mean that useful distinctions and relationships can be perceived
together with manageable interpretation and navigation. It is relative to a
task, expertise, display, and moment. Neither cell occupancy nor permanently
exposing every status is an adequate definition. A comparison may merit many
details at once; composing an argument may benefit from a quiet focused area.

Beauty, delight, tactile satisfaction, and exacting presentation are legitimate
aims. Consistent alignment, deliberate typography, motion, color, and tactile
rhythm may also carry functional meaning. Their beauty is not exhausted by
speed or error counts, and these sources do not establish a causal rule that
beautiful interfaces perform better. Treat experiential judgments as evidence
about experience; investigate functional benefits separately where claimed.

Permissive expression does not mean forcing each participant to learn every
custom abstraction. A powerful extension can reduce repeated effort while
increasing the costs of discovery and coordination. A visible mode indicator
does not guarantee a mode is understood, and additional help can consume the
space or attention needed for the task. A human can be expert in coding but
new to this environment; fluency must be assessed in relation to the actual
instrument.

This theory applies to expert collaborative computing under asynchronous
change, including ordinary external tools and services. It is not a universal
account of human cognition, a claim about model consciousness, a guarantee of
controlling external services, or a hard real-time promise. It supplies no
best layout, language syntax, universal density, haptic vocabulary, or timing
threshold. Those require design choices and observations at the claimed level.

## Predictions that can discriminate designs

Each prediction compares alternatives on the same useful work. These are
candidates for design review and later evaluation, not a ranked implementation
plan or a demand for a large generic usability study.

| Hypothesis and comparison | Predicted difference and observation that would weaken it |
| --- | --- |
| Preserve selected object identity versus preserve only row number while work reorders. | During a chosen action, identity preservation should reduce wrong-target actions and recovery. If it obscures target loss or makes the chosen object harder to locate, that qualification may outweigh the expected benefit. Observe both. |
| Keep two related originals visible together versus require sequential navigation with equal content. | A discrepancy diagnosis should require less reconstruction and fewer mistaken joins with a useful juxtaposition. No improvement, or excessive visual search, would weaken the case for that layout. |
| Permit provisional grouping and annotations versus require a complete workflow taxonomy first. | Exploratory diagnosis should involve fewer lost hypotheses and less rework from early choices. Configuration effort exceeding that saved would favor the simpler alternative. |
| Expose every delegated update versus provide a stable work view with task-relevant transitions and access to originals. | The second may preserve task success with fewer attention shifts and easier resumption. More missed consequential transitions or harder diagnosis would defeat that configuration. |
| Immediate control reaction plus distinct effect state versus a single optimistic success signal. | The former should reduce duplicate actions and mistaken completion beliefs under delayed effects. If participants cannot interpret the distinction, the presentation has failed regardless of internal correctness. |
| Composable operations with optional task-specific instruments versus unrelated binding callbacks. | A newly desired combination should require less bespoke reconstruction and remain easier to inspect across adapters. Higher abstraction/maintenance burden or ambiguous scope would weaken the proposed composition. |
| Learned continuous instrument versus repeated discrete commands for a task needing adjustment. | The instrument may improve exploration and subjective control after practice; a discrete task or an instrument with hidden focus may show no benefit or greater errors. Do not infer value from device novelty. |
| Preserve a draft, reading target, and return relation across interruption versus reopen the same coarse view only. | Resumption should require less reconstruction, with fewer forgotten constraints. If participants naturally change tasks and discard the old relation, forcing its restoration may obstruct them. |

Retained observations can reveal state transitions, action counts, and returns;
they cannot by themselves establish what the operator was trying to learn,
whether a comparison was intelligible, or whether an experience was beautiful.
Use the direct observation or judgment appropriate to each claim, and keep
the scope of conclusions explicit.

## Contribution to the integrated written theory

The feedback-loop account survives as one mechanism within a broader account
of agency through manipulable representations and expressive instruments.
The integrated document should retain three distinctions:

1. **Normative purpose:** whom Blackbird serves and what kinds of agency and
   experience the operator wants.
2. **Explanatory theory:** how representations, instruments, timing, continuity,
   attention, and appropriation are proposed to enable or obstruct that work.
3. **Consequences to test:** what two concrete alternatives should do
   differently, with conditions that could defeat the prediction.

This makes responsiveness a derived requirement wherever delay disrupts a
specific instrument or intervention. It makes stable reference important
where an action spans asynchronous change. It makes rich expression necessary
where repeated low-level operations or a closed vocabulary obstruct intended
work. Their relative importance follows from actual episodes and candidate
designs; this report does not assign universal priorities.
