# Independent launcher review

The question-unit owner independently reviewed the root owner's
`scripts/blackbird` executable-selection change and the default-launch selection
assertions in `scripts/check-session-startup`. This is the single code-review
layer for that conceptual unit; the root owner separately reviews the question.

No concrete defect found. A source layout containing programs/turn.lua delegates
to the default installation only when its launcher is executable, regular rather
than a symlink, and carries the installer-owned marker. The delegation occurs
before checkout assets/executable selection. Both explicit executable overrides
win. Packaged generations put programs under share/blackbird and remain pinned
to their own executable; there is no stale blackbird-ui fallback. Shell quoting
preserves spaces and literal argv, and current working directory is unchanged.
Missing local native executables receive installer advice.

The existing session allocation and exit75 loop retain one allocated directory;
explicit session/recovery selection stays intact. Direct selection tests include
stale checkout native/UI siblings, managed and unrelated launchers, installed
layout, explicit BLACKBIRD_EXECUTABLE and ARCO_EXECUTABLE overrides, missing native
fallback, normal native Paper startup in a real PTY and optional explicit reopen.
The separate installer transaction fixture tests prefix quoting, coherent
resources, argv/cwd, update rollback and unrelated-launcher preservation.

This review does not extend into changing launcher ownership conventions,
custom-prefix PATH discovery or library adoption. No third assurance layer is
requested.
