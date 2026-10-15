# Skilled expression, phrasing and aesthetic experience

2026-10-15. Root research contribution to the explicit interaction theory.
Originals, failed acquisition and exact restoration URLs are recorded in
[documents.tsv](documents.tsv). Original PDFs remain ignored under this directory;
the OSC index HTML is ignored under quarantine. No controller or library is
selected by these readings.

## Sources read and their claims

**Buxton, 1986: Chunking and phrasing and the design of human-computer dialogues.**
Read the full author-hosted corrected version, especially its move/cut-paste,
pop-up menu and input-transducer examples. The later1995 reprint is not the
version acquired here. The paper proposes aligning physical phrases with the
conceptual units an expert learns. A simpler command syntax can merely move
effort elsewhere if it splits a meaningful operation into disconnected pieces.
A held gesture can bind activation, movement and completion into a recognizable
phrase. Device choice changes what can be expressed together. These are
theoretical arguments and examples; claims that particular gestures prevent
all syntax/mode errors do not survive arbitrary device loss or concurrent
retargeting without further contracts. Blackbird should investigate meaningful
phrases and familiar skills, not assume a key count predicts effort. Its source
does not establish a best gesture for coding or embodied motor learning by a
language model. [Author version](https://www.billbuxton.com/chunkingRedux.pdf).

**Wessel and Wright, NIME2001: Problems and Prospects for Intimate Musical Control
of Computers.** Read the complete four-page conference paper. Its aims include
easy entry, continuing development of expressive skill, predictable responsiveness
and programmable gesture/result mappings. Its conceptual diagram explicitly
acknowledges that an instrument can reveal possibilities that change the user's
initial intentions. Continuous gestures, event-triggering and steering generative
processes are different relationships. The authors' proposed musical timing
criteria concern audible performance; they are not Blackbird requirements or a
current benchmark. Its historical MIDI critique must also not be projected onto
the current MIDI2 specification. The usable transfer is a question: does a control
merely trigger fixed actions, or give the participant a dependable way to shape
evolving processes? The acquired version is2001: the author-maintained OSC entry
labels2002 but links the conference copy, while the initially found CNMAT journal
PDF URL returned404. [Conference original](https://opensoundcontrol.stanford.edu/files/p1-wessel_0.pdf),
[author-submitted metadata](https://arxiv.org/abs/2010.01570v1).

**Tractinsky, Katz and Ikar, 2000: What is beautiful is usable.** Read the article's
experimental design, results, limitations and conclusion, especially printed
pp.131–135 and140–142. Its ATM simulation examines perceived aesthetics/usability
before and after use; the manipulated aesthetics affected perceptions. Section5.1
reports no aesthetics effect on completion times or errors in that experiment.
Participants were engineering students and exposure was short. This supports
treating aesthetic experience and practical task performance as distinct evidence,
despite their relationship in judgments. The title does not establish that prettier
controls are faster, clearer, correct, or trustworthy. It also cannot tell us
which terminal typography, motion or style this operator will find beautiful.
The operator's wish for meticulous presentation is sufficient reason to value it;
it needs no invented performance benefit to justify its existence.
[Author-hosted article](https://www.ise.bgu.ac.il/faculty/noam/papers/00_nt_ask_di_iwc.pdf).

## Proposed consequences for Blackbird

The following are deductions and hypotheses, not empirical findings about this
product.

**Expert use should have room to grow.** A fast default command path and a rich
programmable interface can coexist. A model or human can name a useful compound
operation and later embody it in a Lua program, key phrase, pedal/encoder gesture
or view. The abstraction should expose its target and outcome, so saving gestures
does not merely hide important failure or authority distinctions. A sprawling
macro that is hard to inspect can cost more than a clear sequence.

**Expression includes shaping, not just triggering.** Some tasks call for a
discrete action, others for navigating alternatives, selecting a range, adjusting
a value or steering a continuing program. The expressive property is the useful
range of work a participant can articulate and revise. A controller is valuable
when its capabilities fit that work. Mapping every knob increment to a synthetic
arrow key can throw away relative/continuous/held-control information; exposing
every sensor channel everywhere can instead create mode and mapping burden.

**Phrases need a beginning, target and disposition.** A hold-to-clutch gesture can
join target selection, controlled movement and release. What counts as a target
must remain stable throughout, and release/disconnect/rebinding must have declared
effects. Human completion of the physical phrase need not mean a remote operation
completed. The model-facing equivalent might be a composable operation/program
with explicit inputs and returned ongoing-work references; it is not a claim that
the model experiences muscular tension or human chunking.

**Predictability supports acquired skill.** Variable acknowledgment delay,
unexpected remapping or a moving target can break the relationship a participant
learned. Stable semantics need not prohibit customization: the participant can
deliberately replace the instrument while the system preserves the meaning of
already initiated work. An exploration view may legitimately rearrange objects;
the question is whether the current action still refers to the intended one.

**Beauty is an end and can also carry meaning.** Proportion, alignment, restrained
or vivid contrast, rhythm, typography, animation and thematic character can make
the environment worth inhabiting. Some choices also reveal grouping, identity,
state and hierarchy. Neither role exhausts the other. A flourish may be welcome
even when task speed is unchanged. Conversely, perceived polish cannot replace
evidence about effects or excuse making control unavailable. The eventual design
must explicitly choose the desired character and judge it through actual use.

## Comparisons that could challenge these deductions

| Hypothesis | Discriminating comparison and possible counterexample |
| --- | --- |
| Coherent phrases reduce articulation/recovery effort. | Perform one meaningful multi-target operation through separate commands and a compound instrument. A compound path that increases wrong targets or hides partial failures defeats that proposed mapping. |
| Rich mappings enable continuing skill. | Revisit a useful operation after practice and after a period away. More options that force renewed configuration or memorization can reduce practical expression. |
| Timing predictability matters to fluent control. | Hold mean acknowledgment time similar while varying its consistency, and distinguish navigation feedback from remote effect completion. No assumption that musical timing numbers transfer. |
| Aesthetic craft improves the experience on its own terms. | Compare coherent visual treatments on prolonged real work, recording the operator's enjoyment/recognition and task errors separately. A short preference ranking is not proof of sustained benefit. |
| Programmability permits useful appropriation. | Let human/model modify a small instrument, then ask another participant to understand, use and revise it. An opaque one-off customization can be successful for its author yet burdensome in joint work. |

These are proposals for later design inquiry. This theory work launches no
performance experiment, user study or model evaluation campaign.
