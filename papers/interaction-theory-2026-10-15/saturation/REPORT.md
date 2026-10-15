# Pilot task saturation and the Blackbird working arrangement

2026-10-15. Independent primary-source study requested after the operator added
SR-71 controls and pilot task saturation to the interaction theory. This report
contributes workload and attention reasoning. The
[aviation controls report](../aviation/REPORT.md) owns the aircraft manual,
actual cockpit arrangement, and pilot–RSO division of work.
Neither cockpit styling nor an aviation certification analogy is proposed.

The useful question is how an interface lets an expert continue useful work,
notice relevant changes and redirect it when several demands coincide. A system
can remain mechanically responsive while demanding more attention than its
operator can usefully supply. Conversely, satisfactory task performance can hide
heavy compensatory effort. These possibilities require a theory of work and its
demands, not just terminal throughput.

## Definitions and scope

For Blackbird theory, distinguish four quantities rather than calling each
"load": the work and timing imposed by a task; the human effort and attention
needed to accomplish it in the actual setting; the model's action/observation
cost and behavioral limitations; and the software's queued work and available
execution capacity. They interact, but none measures the others directly.

Use **task saturation** here to mean a situated inability to continue meeting the
relevant competing demands with available resources and time. This is a proposed
working definition, not a diagnostic formula, physiological measurement or
universal boundary. High concentration alone does not prove saturation, and
successful completion alone does not prove adequate spare capacity. The selected
SR-71 study concerns handling qualities and compensation; it does not measure
full operational mission saturation.

## Primary evidence and its limits

### A. SR-71: the actual feedback variable changes the control problem

Cox and Jackson, [NASA TM 4791, 1997](https://ntrs.nasa.gov/api/citations/19970014738/downloads/19970014738.pdf),
*Evaluation of High-Speed Civil Transport Handling Qualities Criteria With
Supersonic Flight Data*. Read aircraft/task descriptions, pilot evaluations,
Neal/Smith analysis and conclusions; key printed pages 4–9, 18–20 and 25–26.

Two pilots performed three Mach-3 maneuvers manually with autopilot modes
disengaged. Pressure-driven IVSI feedback produced altitude-change ratings of
Cooper–Harper 5–7; inertial vertical-speed feedback produced rating 3. Tracking
ground track while controlling vertical speed and airspeed required greater
concentration, even when desired performance was achieved. The authors describe
individually modest demands combining into higher compensation. Conventional
pitch-attitude-based Neal/Smith analysis disagreed with pilot comments and
ratings. An extension representing the available vertical-speed feedback at the
pilot station correlated better. The research warns that high-speed data remain
insufficient for conclusive generalization. These are task-specific pilot ratings
and analytical comparisons, not population workload measurements or a cockpit
layout experiment. The separate conference version was not redundantly acquired.

### B. Workload is contextual; automation changes its distribution

Hooey, Kaber, Adams, Fong and Gore,
[NASA/TM–2017–219482](https://ntrs.nasa.gov/api/citations/20190028242/downloads/20190028242.pdf),
*The Underpinnings of Workload in Unmanned Vehicle Systems*. Read sections 1,
2.2, 2.3.5–2.3.6 and conclusion; framework pp. 2–4, task structure pp. 11–16,
link pp. 19–20, conclusion pp. 23–24.

This is an original conceptual synthesis and taxonomy, not a new controlled
experiment. It separates environment, task, equipment and operator drivers from
moderators such as automation and interface design. It distinguishes perceptual,
information-processing and response effort, and sustained load from transitions
and temporary spikes. Automation can remove manual work while adding supervision,
troubleshooting and reorientation demands. Its explicit unresolved question is
how to determine useful overload/underload boundaries across contexts. It reviews
specific utilization and vehicle-count findings; those cannot supply a Blackbird
percentage, colleague limit or task-saturation threshold. Nor does the framework
establish human/model cognitive equivalence. Its human-workload construct and
its automated-system context are the scope of its claims.

### C. Monitoring order and work priority can be different

Schutte and Trujillo,
[Flight Crew Task Management in Non-Normal Situations, 1996](https://ntrs.nasa.gov/api/citations/20040110404/downloads/20040110404.pdf).
Read all six pages, especially method, strategy classification and discussion.

Sixteen airline pilots acted as captain/pilot-not-flying in a simulator with a
capable confederate flying, during a novel fuel-leak scenario. The authors
distinguish how pilots allocated their attention from how they monitored other
functions. Severity-directed attention with an Aviate–Navigate–Communicate–Systems
monitoring pattern appeared useful in this scenario; imposing that order on
personal work could neglect the systems threat. Routine first-officer action
announcements sometimes pulled attention back to already-covered flying duties.
Strategies were inferred subjectively from recordings, not randomly assigned;
the authors call for further controlled testing. ATC interruption timing was
uncontrolled, and subjects had questions about unfamiliar simulator systems.
Utterance-category plots alone could not distinguish acknowledging, delegating
or actually performing a task. This is strong motivation to distinguish those
operations, but weak grounds for a universal priority algorithm.

### D. Interruptions can leave deferred obligations behind

Dismukes, Young and Sumwalt,
[Cockpit Interruptions and Distractions, ASRS Directline 10, December 1998](https://asrs.arc.nasa.gov/publications/directline/dl10_distract.htm).
Read the full original article and its endnotes. Attribution: Aviation Safety
Reporting System, ASRS Directline, and the named authors.

Their analysis of 107 competing-task reports identifies neglected monitoring,
communication, head-down work and abnormal situations as important mechanisms.
Their advice includes explicit role assignment, rescheduling conflicting work,
and cues marking the last completed step after interruption. The article explains
why a resolved interruption can still leave a deferred obligation forgotten.
These are selected voluntary reports and expert recommendations, not randomized
tests of the recommendations. The endnotes explicitly caution that reported
frequencies need not reflect operational frequencies: externally visible
deviations may be reported more often. The lesson concerns vulnerability and
possible mitigation, not an error-rate prediction for Blackbird. The article's
historical cognitive account is not adopted as a description of language models.

The official NASA [attention-management research page](https://www.nasa.gov/human-systems-integration-division/attention-management-and-prospective-memory/)
was also retained as provenance and a lead to the program's publications. It is
marked historical and is not treated as a further independent experiment.

## Consequences proposed for the interaction theory

The following are Blackbird design propositions, grounded in the operator's
expert, programmable, collaborative working environment. The aviation evidence
supplies mechanisms and counterexamples; it does not establish these transfers
as measured Blackbird results.

**Responsiveness has an attentional condition.** The human must be able to
recognize the consequential change and choose a relevant action, in addition to
being able to submit input quickly. An interface that delivers every event
instantly can make this harder. A fast event path therefore belongs alongside a
theory of what merits attention, how work is grouped and how the operator can
return after attending elsewhere. This does not select a notification policy.

**Feedback should reveal the variable the participant is actually controlling.**
For Blackbird, plausible candidates include the named operation's actual
disposition, which definition is active, what remains blocked, and whether an
external effect is known. A bright local acknowledgment may establish only
receipt. The representation must not imply greater knowledge merely because it
responds immediately. Good feedback can reduce the work required to interpret
and correct an action; additional display detail can also add that work.

**Expert performance includes the effort needed to sustain it.** If an operator
must keep several transient identities and pending obligations in memory while
repeatedly checking that nothing moved, correct final actions do not settle
ergonomic quality. The theory should account for recovery work, repeated checking,
lost place and the ability to redirect attention, alongside successful outcomes.
This is a claim about useful inquiry, not a request for an instrument suite.

**Monitoring, doing, delegating and acknowledging are separate work.** A human
can inspect another colleague's status without taking over its task. A model can
acknowledge a message without completing the requested operation. A colleague's
coverage should be inspectable enough to avoid both duplicated effort and an
unattended gap. Roles can change, and work may be delegated dynamically; the
theory must not force a permanent pilot/RSO hierarchy or a single lead model.

**Coordination should preserve attention for the work it coordinates.** Status
exchange is valuable when it changes another participant's decision. Requiring
the operator to acknowledge every colleague update can turn delegated work into
continuous supervisory labor. Suppressing every update can hide the occasion to
intervene. The interface should permit the operator and programs to express which
changes warrant immediate interruption and which can wait for inspection. The
tradeoff depends on task, consequence, timing and current working arrangement.

**Deferred intentions belong in the working environment.** Pending work,
resumption points, unanswered requests and who currently covers an obligation
should survive a view change or interruption where the semantics permit. A
conversation summary saying "continue later" is weaker than a retrievable object
identifying what remains and its relevant state. This need applies to model
continuations through their own mechanisms; it does not imply models possess
human prospective memory.

**Peak demand and transition demand matter.** Several ordinary completions can
coincide with a failure, a controller disconnect or a handoff. Quiet periods may
also leave a participant poorly oriented when urgent intervention becomes
necessary. The theory should permit reconstruction of the current situation
without first replaying every background event. It should also permit deliberate
deferral, reallocation or reduction of optional work rather than demanding that
an expert simply attend faster. No automatic workload-adaptation policy is
selected by this proposition.

**Observation and control must retain their failure boundaries.** An emergency
control that works while painting is stalled addresses a software progress fault.
It does not alone solve human attention saturation. A useful overview that shows
the right task cannot terminate an uninterruptible remote effect. Attention cues,
admission, execution and confirmed outcome therefore remain distinct mechanisms.
Good presentation must communicate the distinctions without making ordinary
operation depend on learning the runtime's implementation.

## Counterexamples that constrain transfer

A dense stable panel can reduce view switching for one task and obscure the
decisive anomaly in another. A tactile cue can attract attention without
identifying its cause. Automation can finish work without the operator watching,
yet require an expensive reconstruction after an exceptional failure. Peer
updates can save a colleague from a wrong assumption while interrupting the
operator's difficult edit. A retained history can make reconstruction possible
without making it quick. The theory has to explain these tradeoffs rather than
declaring density, automation, haptics or transparency good in isolation.

An aircraft has physical dynamics, immediate hazards, specialized training and
roles that differ from a coding workspace. Blackbird can sometimes pause work,
retain exact originals, run queries over many operations or create new programs;
some external effects nevertheless continue independently. The question is which
local mechanism transfers under the actual task, not whether operating an agent
"is like flying". Human ratings from two test pilots do not select TUI latency
budgets. Model context limits do not measure human attention. Software queue
depth does not measure either participant's understanding.

## Corpus and outstanding originals

[MANIFEST.tsv](MANIFEST.tsv) records acquired originals, byte lengths, hashes,
versions and restoration commands. PDFs remain ignored, HTML remains unchanged
under ignored quarantine, and derived reading text stays in ignored context.
No acquired program was executed; no dependency, threshold, widget arrangement
or implementation plan was adopted. Two promising flight-cognition originals
could not be downloaded from the NASA Ames hosts and are recorded in
[SHOPPING_LIST.md](SHOPPING_LIST.md), rather than presented as acquired or fully
read. Their absence does not prevent the explicit theory from incorporating
the mechanisms supported by the retained sources above.
