# Independent domain-admission implementation recheck

This is the single concrete implementation/oracle challenge requested by root,
after six decisive old-red cases and the current six greens. Read-only review
covered `admission.hpp/.cpp`, participant start/send guards, TaskState preparation
and TaskStore publication, private program configuration/encoding, operation
classification/retention/settlement, and the permanent domain engine fixtures.
It also followed the existing raw program fixture directly affected by the
changed private method signature and behavior. No new source/test edits or test
executions were performed by this reviewer.

## Findings to fix within this review layer

1. `tests/program_workflow_raw_test.cpp`'s `ProgramAccess::Type` still declares a
   member pointer taking only `const Value&`. `CodingEngine::program_config` now
   also takes `DomainAdmission*`, so the explicit `Access` instantiation cannot
   compile. Update its member-pointer type and all indirect invocations with an
   explicit null marker for the existing private raw fixtures. Default arguments
   do not apply through a member pointer.
2. That same fixture's `invalid_oracles` still requires an oversized encoded
   proposal through `operator_call` to throw capacity. The repaired public
   contract returns an ordinary retained capacity result naming
   `program_encoding` and `not_started`. Replace that old throw expectation with
   the actual result assertions, retaining the literal failed encoder raw event,
   unchanged workflow revision and zero-generation-issuance checks. This is an
   affected existing raw oracle, not a reason to weaken its source observations.

## Production conclusion and limits

No additional production correctness finding was established. Refusal markers
are private to the owner classes and constructed per actual engine call. Each
participant guard precedes new run/direction publication; task guard failures
discard only a prepared candidate. Program encoding marks only the bounded
encoder's returned capacity error; throwing raw DATA admission precedes that
marker. Generic operation classification requires the owner's explicit marker,
matching capacity error, no current raw capture error and no blocked raw owner.
It adds no operation-name or arbitrary-capacity exemption. Retained-result and
terminal-settlement capacity failures clear the recoverable classification and
preserve existing unknown/sticky blocking. No automatic retry was introduced.

The new semantic capture-fault case actually fails the refusal original write,
requires the writer blocked and provider continuation absent, then proves a later
operation remains blocked. Six domain engine cases run with raw inactive; the
existing `program_workflow_raw` fixture separately attacks actual raw admission,
fixed event fields and real lifecycle. Repairing its compile/contract fallout
and running that affected fixture is necessary before claiming raw-active
qualification. This review does not infer raw correctness from the semantic
writer fault or require a new broad panel/campaign.
