You are an independent, adversarial reviewer of an implementation plan. ONE round only. Read-only: do not edit, create, commit, build or run tests in any repository. Reading files and running read-only inspection commands (cat, grep, git log/show, clang++ -dM -E preprocessor queries) is fine.

Plan under review: /Users/patrickbeam/projects/blackbird/context/cpp26-toolchain/docs/CPP26_SUBPLAN.md
(worktree of the blackbird repo on branch work/cpp26-toolchain-2026-10-17, based on origin/master f4bd931).

Context to read as needed:
- Working doctrine: /Users/patrickbeam/projects/blackbird/BLACKBIRD.md and AGENTS.md (in the worktree). Note "one layer of proof", "never patch, we plan", no ceremony/bullshit, two-layer hardening bound, mutants never on laptop, no master merge by implication.
- Template precedent: papers/2026-10-17-cpp23-toolchain.md and commit 87c1116 (the C++23 upgrade).
- Build/test machinery: CMakeLists.txt, CMakePresets.json, scripts/rigor, scripts/check-linux, .githooks/pre-commit, tooling/README.md, tooling/probes/compiler_contract.cpp, tooling/local-profile.json.
- Fork findings: /Users/patrickbeam/projects/verbs/papers/2026-10-17-cpp26-clang-toolchain.md, /Users/patrickbeam/projects/verbs/journal/2026-10-17-operator-decisions.md, /Users/patrickbeam/projects/verbs/design/SPEC.md section 8.
- Pinned toolchain: /opt/homebrew/Cellar/llvm/23.1.2 (headers under include/c++/v1).

The operator asked to "upgrade blackbird to C++26". The plan splits this into Stage 1 (C++26 language mode on the already-pinned Clang 23 compilers on Mac and Linux, full requalification) and Stage 2 (adopting the Bloomberg P2996 reflection fork, deferred with entry conditions).

Review for:
1. Factual errors: verify the plan's concrete claims (file:line references, commands, flag names, library names, macro values, script behavior) against the actual files. Flag anything wrong.
2. Correctness gaps: steps that would not work as written, missing files/places that hard-code the standard, verification that would not actually detect the failure it claims to, ordering problems, worktree/hook pitfalls.
3. Doctrine fit: ceremony or bullshit to cut; missing rigor the doctrine demands; anything that violates the stated constraints.
4. Strategy: is the Stage 1/Stage 2 split right given the verbs decisions? Is anything important about C++26 on Clang 23 / libc++ 23 (deprecations/removals, ABI, Mac system-dylib availability, clang-tidy at c++26) missed?

Output: a ranked list of findings, most severe first. For each: severity (blocker/major/minor), the plan section, the concrete problem with evidence (file:line or command output), and the specific fix. Then a one-paragraph overall verdict. Do not pad; if a section is fine, say nothing about it. No second round will happen.
