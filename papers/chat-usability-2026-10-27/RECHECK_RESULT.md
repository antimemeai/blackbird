# ONE focused findings recheck — answer

Prose unchanged; Markdown hard-break trailing spaces normalized for document whitespace checks. Exact original bytes remain in ROOT/chat-study-01/recheck.log.

Focused recheck of the six accepted defects only, using the supplied text. No tools or execution.

1. **Resolved — unsupported exactly-once / duplicate-external-effect guarantees.**
   REQUIREMENTS R2 separates submission, receipt, provider-attempt and effect identities; makes effect guarantees conditional on an identified capable owner/protocol; and preserves possibly admitted attempts as unknown without implicit redispatch. The delayed-start obligation explicitly covers lost acknowledgment. The participant episode limits duplicate handling to the owner’s message-identity receipt mechanism, not remote effects.
   **Remaining correction:** None.

2. **Resolved — source/runtime causation and ESC recovery qualification.**
   INVENTORY explicitly leaves installed build-source identity unproven. ACTUAL_USE distinguishes the studied source’s missing interrupt binding from the runtime’s bounded absence of interruption acknowledgment, and expressly rejects a proven installed-code causal trace. Decoder recovery is classified as historical/source evidence, with split-prefix, mouse-tail and other recovery cases unqualified by this reproduction. REFERENCES preserves that distinction.
   **Remaining correction:** None.

3. **Resolved — complete delegated-return, governing-change and context-repair episodes.**
   STATE_CONTROL_MAP now supplies each episode’s intention and target, actual owner/transitions, visible evidence, failure/conflict/stale/unknown handling, safe continuation and return, and explicit unobserved limits. REQUIREMENTS adds corresponding direct obligations without presenting them as executed passes. In particular, waiting versus sealed/completed, saved versus staged versus effective, and content restoration versus protocol repair are separated.
   **Remaining correction:** None.

4. **Resolved — unsupported independent child normal-exit claim.**
   ACTUAL_USE now distinguishes the UI’s exit0, the fixture’s intended normal completion, the observed backgrounded operation result, and the absence of independent child exit disposition. It also avoids inferring that the child remained running at the later screen.
   **Remaining correction:** None.

5. **Not resolved — one categorical no-inference sentence remains.**
   README and ACTUAL_USE explicitly classify absence of provider calls as source/fixture inference, and the auth-home qualifications correctly avoid excluding every credential route. However, INVENTORY still says:

   > “`/lua` local workflows do not invoke inference.”

   Taken literally, this generalizes beyond these fixtures and states the inference as an unconditional fact. The qualification immediately afterward does not make that sentence accurate.

   **Exact remaining correction:** Replace that sentence with:

   > “These fixtures select local `/lua` dispatch and contain no request invocation; the studied source path therefore supports a no-inference expectation, not an independently observed absence of provider calls.”

   Keep the existing provider-attempt/network-census and fallback-credential limitations.

6. **Resolved — submitted error text versus actual runtime cause and native effect classification.**
   ACTUAL_USE explicitly says submitted source text is not an exception record, leaves actual retained cause availability unverified, and does not treat `external_unknown(0)` as proof of an uncertain external shell effect. R7 and the diagnostic obligation distinguish linked, unlinked, unavailable and retention-failed causes and require actual evidence rather than guessed classification.
   **Remaining correction:** None.

**Disposition:** Five resolved; one narrow wording correction remains under finding 5. This is not implementation, design approval or broader qualification.
