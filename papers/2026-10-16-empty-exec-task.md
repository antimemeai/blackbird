# Empty optional exec task binding

Oxcart's live Python.org extraction reached the provider, whose first tool call
was `exec` with a valid command and `task_id=""`. The strict provider tool schema
materializes optional string fields as empty placeholders. CodingEngine::operation
looked up every present task_id, so it rejected the command before dispatch with
invalid_range. The saved provider result and lua.error in the Oxcart run
`run-6bxs7zm1/blackbird` establish this exact trigger; no provider release study or
new library is needed. The same predicate is present on current master.

Implement empty string as an absent task binding. Preserve rejection of a
nonempty unknown task and preserve existing-task activity. Update the advertised
field description. Add one native CodingEngine check using the exact real
arguments, checking observed output and a marker proving unknown-task refusal
does not dispatch. Run it red before the one-predicate fix, green afterward,
then run the relevant task/activity and application checks and build Blackbird.
Keep this in an isolated worktree and leave the active TUI checkout untouched.

The same live run exposed a related protocol ambiguity: command rejection emits
error alone, while a failed admitted turn emits error then final. Add an explicit
final_follows boolean (false for rejection/fatal errors, true for admitted turn
failure) so Oxcart retains request ownership and waits for final before saving the
attempt or accepting another turn. Check real ApplicationConnection failure
framing plus the native client/coordinator, and preserve failed submissions.
Remediation is bounded to these two observed defects and their direct checks;
one code recheck follows, with no unrelated suite or terminal campaign work.
