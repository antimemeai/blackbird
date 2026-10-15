# Blackbird interaction theory

2026-10-15. A written working theory developed from the operator's aims, primary
HCI/ACI research, systems literature and reference implementations. It explains
what interaction in Blackbird is for, how it can succeed or fail, and what design
claims should follow. It supplies neither a selected screen layout nor an
implementation sequence. The [foundation](FOUNDATION.md),
[core contracts](CORE_DESIGN.md) and [current delivery record](CURRENT_STATE.md)
retain their existing meanings. The research and independent contributions are
indexed in [the theory research pack](../papers/interaction-theory-2026-10-15/README.md).

## Thesis

**Blackbird should be a programmable instrument for joint computational work:
participants can act on work, arrange representations to understand it, delegate
it, and change the instruments through which they work, while retaining usable
reference, meaningful feedback and continuity.**

The explanatory claim is that useful agency depends on a fit among four things:
the work being attempted; the capabilities and acquired skills of its
participants; the representations and instruments available to them; and the
timing, coordination and continuity of their interaction. Mismatches force
participants to translate, reconstruct, monitor, wait, repair or abandon useful
intentions. A good instrument removes unnecessary mismatches and opens useful
possibilities that were previously difficult to express.

This extends the earlier observe–target–act–feedback intuition. That loop describes
an important local relationship. It cannot by itself explain thinking through
external arrangements, learning a powerful instrument, choosing not to monitor
delegated work, or changing the interface while using it. Those are central to
Blackbird rather than exceptions at its edge.

## Values, explanations and claims

The operator has chosen an expert, expressive, programmable environment with
substantial model agency, ordinary computing integration, little routine approval
friction, exact retained originals, and meticulously considered presentation.
Unusual controls, MIDI and possible haptics are legitimate possibilities. These
are product commitments. An empirical paper need not prove that the operator
ought to want them.

The sources contribute explanatory models and findings with particular scopes.
Our application to Blackbird is a reasoned proposal. Causal predictions below
remain open to observation and counterexample; this document reports no new
Blackbird usability, physical-controller or comparative-performance experiment.
It does not treat humans and language models as cognitively equivalent.

Success includes worthwhile work, practical freedom to invent methods, mastery,
and an environment the operator enjoys inhabiting. A single score for speed,
screen occupancy, fewer calls, fewer keystrokes or fewer errors cannot express
all of these aims. Tradeoffs require judgment tied to the work and the operator.

## The unit is an episode of work

An episode is a pursuit or exploration that gives actions their meaning: repairing
a build, examining a disputed claim, steering several workflows, composing an
experiment, or changing Blackbird itself. Its purpose may become clearer or change
as discoveries arrive. It can span turns, programs, views, machines and sessions.
A frame or provider response is a useful engineering unit within it, not a
complete account of the activity.

[Distributed cognition](https://hci.ucsd.edu/hollan/Pubs/tochi-revised.pdf) motivates
studying people, artifacts, external representations and prior activity together.
For Blackbird, model calls and executable programs also participate in that
system. This is an analytical extension, not evidence that models possess human
perception, intentions or memory. An external service can contribute to the
episode without becoming a service Blackbird governs.

| Concept | Meaning in Blackbird |
| --- | --- |
| Participant | A human or computational actor whose capabilities, authority and actual contributions matter. Programs can participate without having a human-like inner state. |
| Purpose | What the work is trying to accomplish or discover; exploration can revise it. A declared purpose need not describe everything a participant understands. |
| Work object | A particular artifact, participant, workflow, attempt, result, context revision or other referent relevant to the episode. Environment and incarnation can be part of its identity. |
| Representation | A view, original, summary, annotation, selection or arrangement through which some aspects of work become available. It can omit, transform or lag what it represents. |
| Instrument | A means of addressing and changing objects or representations: command, key phrase, program, query, selection tool, knob, pedal or composed workflow. |
| Binding | The relation of an instrument to its target, operation, scope and current meaning. Screen position or device port number alone need not establish this relation. |
| Evidence | What an observation actually supports about the work, including where its authority or freshness ends. A representation changing is not proof of an external effect. |
| Continuity | Retaining or recovering the relationships, drafts, decisions, references and outstanding work needed to continue meaningfully. It need not preserve every pixel or every runtime stack. |
| Appropriation | Participants adapting representations and instruments to emerging work, including modifying their governing programs. |

These distinctions guide reasoning. They do not require a new universal object
database, fixed workflow topology or screen containing every concept.

## Acting, understanding, delegating and changing instruments

Acting on a work object changes something the episode concerns: edit a file,
start a test, send a colleague a message, or stop an owned command. Arranging
representations can instead change what a participant can understand: pin two
results, group related failures, annotate an assumption, or pause a displayed
stream while reading. Both can be productive work.

[Kirsh's spatial account](https://adrenaline.ucsd.edu/Kirsh/Articles/Space/AIJ.html)
explains how arrangements can simplify choice, perception and computation, while
also costing effort to create and interpret. Consequently, fewer effectful
commands is not necessarily better interaction: a comparison or view change can
save substantial reconstruction. Conversely, elaborate arrangement can become
another task that distracts from the original problem.

Delegation changes who acts and who needs feedback, when. The human can entrust
an episode to models and programs and turn attention elsewhere. Available
inspection, appropriate notification, useful handoff and intervention matter;
compulsory continuous monitoring defeats the purpose of delegation. Autonomous
programs can run long sequences without a human acknowledgment between effects.

Changing instruments is ordinary work too. The model may write a Lua operation,
the operator may invent a controller mapping, and either may alter a view or
context transformation. Instrumental interaction explicitly includes tools that
operate on other tools. Blackbird's self-development requirement makes this a
first-class part of its interaction theory.

These four activities are analytical distinctions, not four product modes or a
mandatory sequence. An exploratory gesture or program can combine several.

## Mechanisms and propositions

### 1. Practical agency requires an expressible and intelligible repertoire

Permission to do many things is insufficient when participants cannot discover,
name, compose or reliably address the useful ones. Agency becomes practical when
participants can articulate an intention at a suitable grain, select its scope,
and understand enough about the resulting work to continue or revise it.

[Direct-manipulation theory](https://hci.ucsd.edu/hollan/direct-manip.pdf)
distinguishes semantic fit from the effort of expressing it and allows symbolic
languages and scripts to serve tasks that repetitions, sets and precision make
awkward otherwise. Directness can involve words and reusable results; it need
not involve pictures or one gesture per primitive.

For Blackbird, a shared operation vocabulary can support different instruments:
human gestures and spatial views, model-addressable results and programs, Lua
composition and ordinary command-line use. Shared meaning concerns target,
operation and outcome. Identical names, screens, encodings or literal event
streams do not establish that meaning by themselves.

**Prediction:** for appropriate compound work, composable operations or a learned
gesture reduce translation and reconstruction compared with unrelated low-level
steps. **Counterexample:** a compound instrument can hide discovery, partial
failure or target choice and make the same work harder to inspect or repair.

### 2. Useful representations preserve relationships needed for the next judgment

The important question about a representation is what distinctions and
relationships it makes usable. A failure count can orient attention; diagnosing
one failure may require the request, actual result and relevant change together.
Presenting all three somewhere in a transcript can still leave the participant
to reconstruct their relationship from memory.

Density therefore means useful task relationships available together at an
appropriate level of detail. It is not a target percentage of occupied cells.
Grouping, alignment, hierarchy, linked detail and stable return paths can provide
more useful density with fewer visible facts. A sparse view can also conceal
important relations and require excessive navigation.

Human and model projections can preserve different useful structures. Spatial
juxtaposition may help the human; a concise relation with addressable originals
may help the model. A summary cannot stand in for an exact original when the
omitted detail matters. [SWE-agent](https://arxiv.org/abs/2405.15793v3) provides
historical evidence that action and observation design change model behavior;
its particular search, window and history settings do not select Blackbird's
limits. [Lost in the Middle](https://aclanthology.org/2024.tacl-1.9/) further
motivates distinguishing available context from evidence a model uses successfully.

**Prediction:** preserving relevant relations and a recoverable path to originals
reduces mistaken joins and repeated search on tasks that need them.
**Counterexample:** an attractive summary can omit the decisive clue; exhaustive
detail or mandatory provenance navigation can bury it just as effectively.

### 3. Fluent control depends on stable meaning and feedback suited to the action

A participant learns how an instrument behaves. Unsolicited target changes,
invisible mode changes and erratic response can defeat that acquired relation
even if the interface accepts every input. Stable meaning does not require
permanently fixed coordinates or prohibit deliberate customization.

[Buxton's phrasing account](https://www.billbuxton.com/chunkingRedux.pdf)
explains how a physical phrase can bind activation, movement and completion to
a conceptual unit. [Wessel and Wright](https://opensoundcontrol.stanford.edu/files/p1-wessel_0.pdf)
connect expressive skill with programmable mappings and predictable reaction,
including steering processes rather than merely triggering events. Their musical
timing criteria do not set a keyboard, model-call or remote-job budget here.

A held encoder gesture needs an intelligible target and ending; release,
disconnect, rebinding and target retirement require explicit meanings. Relative
motion, absolute position, text composition and discrete transitions carry
different information. Transforming all of them into synthetic keystrokes may
discard useful structure; exposing all raw channels everywhere may create burden.

**Prediction:** stable target/mapping meaning and consistent useful feedback
support learned control and reduce wrong-target actions during concurrent change.
**Counterexample:** fixed placement can obstruct useful overview, and a strict
freshness check on every input can reject meaningful actions for irrelevant changes.

### 4. Feedback must distinguish the instrument's reaction from the work's outcome

[Instrumental interaction](https://www.lri.fr/perso/~mbl/papers/CHI2000/paper.pdf)
separates an instrument's reaction from object feedback.
[End-to-end reasoning](https://web.mit.edu/Saltzer/www/publications/endtoend/endtoend.pdf)
adds that an intermediate acknowledgment cannot establish an application result
outside its knowledge. These distinctions explain why quick local acknowledgment
and slow external completion can coexist without misleading the participant.

Input acquired, action accepted, effect requested, effect observed and feedback
presented are different events. A haptic pulse can mean a press or an accepted
command; neither proves a remote job stopped. An unknown external outcome can
be the correct observation. A spinner that changes immediately can still leave
the next decision unsupported.

Meaningful feedback need not consist of a banner for every internal stage. It
should expose the distinctions needed to continue, including partial failure and
uncertainty. The local loop can overlap other loops, include long thought, and
close much later than its initiating view.

**Prediction:** truthful disposition reduces duplicate submissions and mistaken
continuation when settlement is delayed or uncertain. **Counterexample:** exposing
every internal state can cost more attention and context than it saves.

### 5. Responsiveness is continued opportunity for useful action under competition

Responsiveness relates a particular action and its feedback to the work competing
for resources and to when the action remains useful. An idle frame rate or fast
decoder alone cannot characterize it. Reading an interrupt is different from
handling it; accepting cancellation is different from completing termination;
writing bytes is different from visible presentation.

Competition exists at several levels: native scheduling and blocking work,
presentation queues, human attention, and model action/observation budgets.
These have different mechanisms. A slow display can obstruct a control handler;
unrelated status can interrupt a human's unresolved intention; unhelpful feedback
can cause a model to spend turns probing without progress. Calling all three
"load" does not make their units or remedies interchangeable.

A design needs an explicit operating envelope and ways for promised controls to
progress within it. A stopped output channel cannot display a new acknowledgment;
input/effect control may still progress and another channel may supply feedback.
Unbounded production, an indefinitely absent consumer, finite storage and exact
retention cannot all be promised together. Existing original/audit/raw capture
contracts govern those channels; superseding a derived view cannot silently
discard their records or a partly emitted terminal packet.

**Prediction:** reducing irrelevant interference and preserving service to
consequential controls improves the ability to direct work under competing demand.
**Counterexample:** a dedicated thread still waiting for a render lock or a
cooperative timeout around a blocking native call may provide no useful independence.

Attention has its own dynamics. Several individually manageable tasks can
coincide, interruptions can displace a deferred intention, and a return to quiet
delegated work can demand substantial reorientation. Good final performance can
conceal repeated checking and high compensation. A mechanically responsive
interface can still make the human's work untenable.

For this theory, task saturation means a situated inability to meet the relevant
competing demands with the resources and time available. It supplies no diagnostic
formula or universal threshold. Human workload, model action/observation behavior,
and software queue pressure need different observations and explanations. The
[workload study](../papers/interaction-theory-2026-10-15/saturation/REPORT.md)
grounds this distinction and its transfer limits.

Doing, monitoring, delegating and acknowledging therefore require distinct
treatment. A participant needs to see consequential coverage and deferred
obligations without being forced to redo a colleague's work. Deliberately
deferring, reallocating or reducing optional work can itself be a useful action.
More frequent updates or a permanently fixed priority order cannot be assumed
to solve coincident demand.

### 6. Joint work requires compatible references, expectations and repair

[Joint-activity research](https://www.ihmc.us/wp-content/uploads/2021/04/17.-Team-Players.pdf)
emphasizes predictability, directability, attention and coordination cost. A
shared display does not establish that a participant noticed or understood it.
For models, declared work state and observed responses are available evidence;
human-style mutual understanding cannot be assumed.

Participants need enough compatible reference to coordinate the current work:
which environment, object, version, attempt, intention and outstanding result
matter. They need not share all context, all authority or a common inner model.
A late result retains its actual request and environment; an edit that depends
on an old revision has a different freshness requirement from inspecting a
stable job's new output. [Causal order](https://lamport.azurewebsites.net/pubs/time-clocks.pdf)
can establish relations that a flat arrival-order transcript does not.

**Prediction:** explicit relevant reference and handoff meaning reduce duplicate
work, stale-target action and reconstruction after interruption.
**Counterexample:** imposing constant status exchange or consensus on independent
tasks adds coordination work without improving the task.

### 7. Continuity and appropriation let the instrument improve through use

Continuity preserves what makes the next action meaningful: the draft, selected
evidence, effective definitions, prior effects, pending results and unresolved
obligations relevant to the episode. It does not require identical pixels,
unchanged external services or an uninterrupted model stream of consciousness.

Programs and context transformations are ordinary instruments. [CodeAct](https://arxiv.org/abs/2402.01030v4)
provides bounded empirical evidence for benefits of executable compositional
actions; it does not select Python, eliminate structured commands, or establish
that a larger program is always easier to repair. Blackbird's C++/Lua choice
remains the operator's decision.

When an instrument changes, participants need to distinguish what was edited,
what is pending, what is effective, and which earlier work retains its meaning.
Current deferred activation and interrupt-and-apply semantics already specify
important parts of that relation. Deliberate evolution should preserve usable
old-action meaning without making every customization a promotion ceremony.

**Prediction:** editable instruments plus recoverable evidence allow useful
methods to emerge and improve without repeatedly rebuilding the entire episode.
**Counterexample:** opaque personal abstractions or unstable mappings can increase
the effort of joint work. The instrument for managing abstractions also needs
to be usable, as [cognitive dimensions](https://www.cl.cam.ac.uk/~afb21/CognitiveDimensions/CDtutorial.pdf)
helps make explicit.

## Aesthetic craft is part of the desired experience

Blackbird should have deliberate visual and temporal character. Typography,
spacing, proportion, alignment, color, animation, imagery and tactile feedback
can express grouping, hierarchy, identity and rhythm. They can also provide
pleasure independently of measurable task speed. The operator need not justify
every aesthetic choice as a productivity intervention.

Craft still needs to coexist with intelligibility and dependable control.
Perceived polish cannot establish that work completed, and a visually quiet
screen can hide important distinctions. A vivid screen need not overload the
operator if its hierarchy and timing fit the activity.

[Tractinsky and colleagues](https://www.ise.bgu.ac.il/faculty/noam/papers/00_nt_ask_di_iwc.pdf)
found relationships between perceived aesthetics and perceived usability in a
short ATM simulation, without an aesthetics effect on task times or errors in
that experiment. That is a reason to assess experience and actual performance
separately, not a law that beauty makes systems faster or more reliable.

## The SR-71 case: controls, compensation and saturation

The operator requested study of the aircraft itself. The
[controls report](../papers/interaction-theory-2026-10-15/aviation/REPORT.md)
uses visually inspected cockpit plates and control/procedure pages from two
public reproductions of the declassified flight manual, together with a
Lockheed technical-handbook scan. The two manual parts do not constitute the
entire publication, and the handbook mirror's exact edition date is unresolved.
The [reading record](../papers/interaction-theory-2026-10-15/aviation/reading-scope.tsv)
states the actual page scope. These are historical primary documents, not a
claim that the cockpit was optimal or that its crews never became overloaded.

**A control has a relationship to the aircraft, not merely a label.** The manual
distinguishes direct flying controls, stabilization and autopilot modes. Pitch
and roll trim-wheel effects depend on mode; the control-stick-steering release
and the autopilot disconnect have different consequences. The forward panel
groups flight references, engine and inlet indications, while the right console
groups SAS/autopilot controls. This challenges a single unqualified automation
toggle or autonomy dial: steering a reference, changing a policy and stopping
work can be different acts. [Manual part1, printed pp.1-23,1-26,1-122–123](https://www.firebirdv8.com/sr/sr71_fm_pt1.pdf).

**Useful tactile feel need not report the external state.** The handbook describes
hydraulic controls whose irreversible servos do not reflect aerodynamic loads;
springs supply artificial control resistance. A meaningful haptic signal can
likewise express a control boundary or accepted input without measuring remote
effect completion. Blackbird must decide what a detent or pulse means rather
than treating physical immediacy as truth about the work.
[HandbookII, printed p.2-31](https://www.usaf-sig.org/index.php/references/downloads/5-usaf-historical-documentation/9-type-specific?download=254%3Alockheed-sr-71-researchers-handbook-vol-2-technical-description).

**Division of labor requires explicit consequential coordination.** Normal
procedures assign the RSO attitude crosschecks, have crew members select
different attitude-reference sources, and require the pilot to communicate
autopilot engagement/disengagement and modes. A common aircraft does not create
automatic common knowledge. For Blackbird, compatible reference, declared
coverage and consequential-change signals are more useful concepts than a
magically shared context. This does not prescribe a permanent human-pilot,
model-RSO or lead/subagent hierarchy.
[Manual part2, printed pp.2-38–39](https://www.firebirdv8.com/sr/sr71_fm_pt2.pdf).

**The signal actually used changes the control problem.** NASA's
[TM4791](https://ntrs.nasa.gov/api/citations/19970014738/downloads/19970014738.pdf)
reports two pilots performing specified high-speed maneuvers manually with
autopilot modes disengaged. Available inertial vertical-speed feedback helped
explain why a pitch-attitude-based analysis did not match pilot ratings; adding
that feedback path improved the analysis. Concurrent tracking demands could
also require substantial concentration despite desired performance. This is
evidence about those handling tasks and pilots, not a measurement of operational
mission-wide saturation. The transfer is methodological: study the variables
participants actually perceive and control, and the compensation required to
maintain the result. A fast local echo may be the wrong feedback variable.

**Monitoring order is not always the order in which work should be performed.**
In [Schutte and Trujillo's simulator study](https://ntrs.nasa.gov/api/citations/20040110404/downloads/20040110404.pdf),
captains worked with a capable confederate covering flying duties during a novel
fuel-leak scenario. The analysis distinguished a monitoring pattern from the
attention needed for the actual threat; announcements could pull attention back
to already-covered work. The strategies were inferred rather than randomly
assigned. [ASRS's interruptions analysis](https://asrs.arc.nasa.gov/publications/directline/dl10_distract.htm)
also describes how an interruption can leave a deferred obligation forgotten.
For Blackbird these support examining coverage, interruption relevance, deferred
intentions and resumption cues. They select neither notification suppression nor
a universal task-priority algorithm.

The aircraft case therefore changes the theory's account of responsiveness:
the participant must have usable feedback, attention and a way to manage
competing demand, alongside a functioning input path. The
[NASA workload framework](https://ntrs.nasa.gov/api/citations/20190028242/downloads/20190028242.pdf)
reinforces that tasks, equipment, setting, expertise and automation shape demand;
it leaves general overload boundaries unresolved. Blackbird can sometimes pause,
query or program work in ways a flying aircraft cannot, while external effects
can continue independently. Dedicated gauges, flight-envelope restrictions,
training burdens and cockpit procedures do not automatically transfer to this
programmable environment. No aviation timing or utilization number becomes a
Blackbird limit by analogy.

## Worked consequences

**Inspecting while others work.** An operator compares an old failed test with a
new result while models and commands continue. Pinning, arranging and annotating
the results are legitimate work. New output can update the overview without
silently changing selected originals or moving the action's referent. The model
can receive references to those same originals through a different projection.
The relevant claims concern correct comparison, target stability and return to
the prior activity, not maximum visible message count.

**A controller instrument.** A held control temporarily binds an encoder to a
selected workflow parameter or to navigation through related evidence. The
phrase has an identifiable target, reaction and ending; a button can address a
discrete operation independently. If the device disconnects or the mapping
changes, declared semantics determine disposition. A pulse can acknowledge an
accepted control without claiming that a remote effect finished. The theory
supports investigating this mapping; it does not select it or assume it is
better than a keyboard for every task.

**Model-led continuation and self-change.** A model starts several checks, reads
structured results and exact originals, writes a Lua composition for a recurring
diagnosis, and stages a view/context change. It can continue unattended within
standing authority. A failed check or reload produces usable evidence, while
existing operation identities and prior effects remain meaningful. The human
can return, understand consequential work and steer it without reconstructing
every tool response. This needs complementary human/model representations,
not a requirement that the model use the same visual controls.

## How this theory derives design work

The derivation is: identify an episode and the operator's values; describe its
participants, intentions, referents and instruments; explain the important
translation, representation, timing, coordination and continuity mechanisms;
compare designs that change those mechanisms; then state the required behavior,
failure boundaries and direct observations. Only that can justify priorities or
an implementation sequence.

Before comparing alternatives, name the episode and plausible changing purposes,
the alternatives, the mechanism expected to improve, and the costs that could
offset it. Examine newly expressible or lost intentions and the ability to invent
and revise methods, alongside performance on familiar tasks. A narrower command
set must not win merely by making the untested intentions impossible.

If the predicted reduction in translation, reconstruction, waiting, coordination
or repair does not appear, or worse costs dominate, reject or narrow that local
claim. Do not rescue it by retrospectively calling the successful alternative
"appropriate" or redefining the participant's intention. A discovered change
of purpose can be legitimate, but must be recorded as a change rather than used
to rewrite the comparison. This is a reasoning discipline, not another required
certification process or numerical score.

| Theoretical claim | A discriminating question |
| --- | --- |
| Practical agency comes from a suitable repertoire. | Can a participant express the intended scope and composition, and recover from a partial result, without a workaround that changes its meaning? |
| Generative agency opens new methods. | What useful intentions and compositions become possible, which are lost, and can participants understand and revise the methods they invent? |
| Relationships matter more than displayed quantity alone. | Can human/model connect a failure to the right request and original, compared with alternatives that provide the same facts in a different structure? |
| Stable meaning supports skill. | Does asynchronous insertion, remapping or reconnect cause a wrong target or force avoidable relearning? |
| Reaction and outcome have different meanings. | Can participants tell accepted input from observed effect without being forced to monitor every internal transition? |
| Useful control must survive competing demand. | What happens to the promised action when output stalls, callbacks run long, updates flood, or attention is already occupied? |
| Successful performance can hide high compensation. | Does the same correct outcome require repeated checking, lost-place recovery, duplicated monitoring or abandonment of another obligation? |
| Coordination requires compatible reference. | Can participants hand off and resume the same work, while distinguishing old observations and unresolved effects from current facts? |
| Appropriation needs continuity. | Can an instrument be changed, understood and reused while already initiated work retains its declared semantics? |
| Beauty is an independent value with semantic opportunities. | Is the environment enjoyable and coherent during actual work, while its apparent polish remains distinguishable from demonstrated task/effect correctness? |

These questions provide possible observations and counterexamples. They neither
launch an evaluation campaign nor turn every interface decision into a new gate.
No universal numerical latency, density, notification or concurrency limit follows
from the cited sources. The next design must state actual task-relative promises
and assumptions before choosing those numbers.

## Boundaries of the theory

Observation here means access to useful distinctions with provenance and
uncertainty. It is not mathematical reconstruction of every hidden state;
steerability is not reachability of every possible system state. Formal control
claims would require explicit dynamics, signals and assumptions beyond a loop
diagram. Human attention and model context are also different mechanisms.

The theory is meant to generate and challenge Blackbird designs. It could be
weakened if its proposed relationships consistently fail to explain real work,
or if a competing account predicts important successes and failures it misses.
Its useful content lies in the stated mechanisms and counterexamples, not in
calling every successful interface an instance of the theory after the fact.

One independent conceptual challenge identified the risk of post-hoc fit and
under-testing generative agency. Both are addressed in the comparison discipline
above. The [challenge and resolution record](../papers/interaction-theory-2026-10-15/CHALLENGE_RESOLUTION.md)
preserves those concrete corrections; this is a researched working theory rather
than an empirical validation of its Blackbird predictions.

Current C++/Lua mechanisms matter when deriving a design. In particular,
`src/coding.cpp` already installs a Lua instruction-count interruption hook;
an unyielding pure-Lua workflow is not automatically an uninterruptible one.
The hook does not preempt arbitrary long native calls or establish a future
UI callback architecture. Current `ChatOutput` also retains partial output
nonblockingly. The [systems study](../papers/interaction-theory-2026-10-15/systems/REPORT.md)
records these source observations and their limits. Research must preserve such
existing capabilities and qualify claims about what remains missing.
