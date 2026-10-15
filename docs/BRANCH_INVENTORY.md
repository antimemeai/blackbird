# Blackbird branch integrations

## Current integration — 2026-10-15

The operator directs one bounded OpenRouter check and merge, with no further
certification round. Fast-forward `master` from trace-archive `d74f248` through
`work/tokenomics-openrouter-2026-10-14` at `4aa8f43`. The four commits include
research `27349a9`, sequencing `ad4a640`, qualified source `56b3f62` and result/
issue closeout `4aa8f43`. Remote master has no divergent commits; source merging
requires no conflict resolution, squash or history rewrite.

The [qualification record](../papers/tokenomics-2026-10-14/qualification.md)
records the passed full source gate and affected host profiles. One native live
launch refuses locally with provider_auth because no OpenRouter credential is
configured; no tool effect occurs. Reopen and raw usage inspection work, while
actual service continuation/cache behavior remains unqualified (arconaut-438).
The existing intermittent capture-test timeout remains arconaut-7qn. No further
source/test changes or certification round. The added integration record is
text-only under AGENTS's existing exception. Push master normally; retain the
work branch, operator artwork, historical worktrees and running executables.

## Earlier integration — 2026-10-10

The operator directed merging the pending work into the default main line,
`master`. The ready branches form a single ancestry chain: `provider-auth`
(`59539d3`) is already master; `trajectories` (`f8aa868`) is contained in
`command-jobs` (`2ac0635`). Fast-forward master through that tip and the text-only
doctrine/documentation checkpoint. No conflicting source, squash or forced
history rewrite is needed. Existing source qualification is recorded in the
command lifecycle report and journal; no new source is introduced by integration.

This includes command lifecycle ownership, trajectory/native-format work,
independent colleague/recovery bug fixes and the unimplemented instrumentation
proposal. The rejected instrumentation stash stays unapplied. Candidate refs,
user artwork, detached historical checkouts and the running executable remain
preserved. The sections below are earlier inventories, not pending merge work.

## Earlier consolidation — 2026-10-08

Consolidation baseline: `44defd7`. The repository's default main line is `master`.
All 42 other local heads have a disposition: 40 tips are ancestors of master;
the slash-menu tip is patch-equivalent; the useful-work tip's write-confirmation
guard and failed-write regression already landed in `abbd021` and remain present
under the Blackbird namespace. None contains new product work to squash or merge.
Existing public history is preserved.

Retired all 42 redundant local heads and the 13 remaining remote candidate heads
in the consolidation published as `55cd85d`; origin now has only `master`.
The two nonancestor tips already have annotated `archive/2026-10-07/` tags.
Five clean parked worktrees are detached at their historical tips in place,
preserving their ignored build products and experimental captures. They are
historical checkouts, not active branches or promised live workers. The operator's
untracked artwork and current interactive Blackbird process remain untouched.

[Exact branch tips and dispositions](branch-consolidation-2026-10-08.json) retain
restoration information. Included heads can be recreated with `git branch NAME SHA`;
the two duplicate-only heads can be recreated from their existing archive tags.
Detached worktrees retain their recorded commits and paths. No new archive tags,
source rollback, forced public-history rewrite or arbitrary branch re-merge.

The record below is historical: its earlier missing-functionality and remaining-
branch statements describe those dates, not the consolidated current state.

## Historical remote assessment 2026 10 07

Before cleanup, origin fetch/prune and GitHub heads list showed **32 branches**,
excluding the symbolic origin/HEAD. Comparison baseline: master `ee8f1e38daabe89fcba33f5b07f7e6143b206a8b`.
Cleanup subsequently executed at the operator's request: **two remote branches
remain**, master and candidate/giga-colleagues. The other 30 branch refs were
deleted in one atomic push alongside two annotated archive tags:

- archive/2026-10-07/tui-chat-shell retains 76c4016.
- archive/2026-10-07/useful-work-tools retains 7cbc088.

All retiring tips were unchanged from the assessment; the other 28 tips were
confirmed ancestors of current master before deletion. Remote heads and peeled
tag targets were checked after publication. Local branches/worktrees are retained;
master history was not rewritten.

**At that earlier cleanup, only candidate/giga-colleagues contained substantive unintegrated work.**
It adds the standalone colleague library/CLI, selected-context request contract,
OpenAI/Claude adapters, truthful outcome captures and direct tests. It is one
unique commit, f233689, 66 commits behind this baseline. Port its useful slice
onto current master rather than merging old source blindly. Its existing live
readiness/timeout evidence does not establish completed useful collaboration.
No colleague source/library is currently present in master's src/include.

Two other tips have unique commits but their behavior is superseded:

- candidate/tui-chat-shell (76c4016): shared command discovery, completion and
  local responsive controls. Current master retains these and adds palette,
  editor and conversation rendering; inspected its shared declarations and
  /help, /keys, /queue, /cancel, /clear dispatch. No reason to integrate old UI.
- candidate/useful-work-tools (7cbc088): refuse dispatch after an unconfirmed
  request write. The same fence and failed-write regression are already in
  programs/useful_work.lua and tests/useful_work_test.lua. No missing fix.

**28 non-master branches are ancestors of master.** Retiring their branch names
loses no commits from master. This includes all 12 economy experiment refs,
which point to the exact same 3fd4f33 commit. The remaining giga/reconstruction/
recovery refs are historical checkpoints, not pending merges. G9's inclusion
means its discussion is retained, not that networking implementation was shipped.
No open GitHub PRs were reported by gh pr list.

The assessment recommendation has now been executed. The inventory below is the
pre-cleanup snapshot and preserves every retired name and exact tip.

## Current consolidation — 2026-10-07

Operator subsequently directed merging everything. All12 current candidate branch
tips are included in master. Recovery mergee743ce5 includes physical-hintdce1481,
cold-historyd32a764 and recovery-index87f786f ancestry; colleague merge95ed4b6
includesf233689 and adapts its namespace/include/target names to Blackbird.
Branch refs/worktrees are retained; no history rewrite or new worker.
Unfinished native current-state/suffix recovery remains unfinished in merged source.

| Included candidate | Exact tip |
| --- | --- |
| `candidate/cold-history-r2-2026-10-07` | `d32a764ea664ad1710a45e6de0bb0fa26b4a15d2` |
| `candidate/durable-recovery-2026-10-07` | `87f786fb982dae5a5035eb2146a9b0aac0beba2d` |
| `candidate/durable-state-r1-2026-10-07` | `dce1481fc05162432cb928f39cd14eb69ff0603a` |
| `candidate/giga-colleagues` | `f23368914f46f9edd2db935fb499d669c690b009` |
| `candidate/issuer-ranges-2026-10-07` | `02ff9acad35bf383958e5e51f7bfcfebf4de89c4` |
| `candidate/issuer-ranges-r2-2026-10-07` | `e0e0b980cb333eabe120bf1689040c528312c841` |
| `candidate/local-performance-2026-10-07` | `94bc2d4184a03f54a7e277d9cc408bda0d40436c` |
| `candidate/native-beads-2026-10-07` | `47bb9be83b771f8bbc11b3e164b06584af4a9c65` |
| `candidate/native-jev-2026-10-07` | `ccc26087328d882c3e2dd0ced2af38063f6d183b` |
| `candidate/startup-demand-loading-2026-10-07` | `8e35f55a9a1db808b80576b3a4cda0846961edc4` |
| `candidate/startup-loading-r2-2026-10-07` | `35413f3f9eb1cecb70cf3c44a2aadd4008708f29` |
| `candidate/workflow-core-2026-10-07` | `c8498f01d7c5b4d908d8d385d4f7e0eb4306a2a5` |

## Exact remote inventory before cleanup

Ahead/behind counts are against the comparison baseline above, not later docs
commits. Ancestry establishes retained commits; source comparison establishes
that the two unique superseded fixes are already implemented.

| Branch | Tip | Unique commits | Behind | Recommendation |
| --- | --- | ---: | ---: | --- |
| `candidate/atomic-operation-admission` | `fb2b5a51900ad084adb6de3f0afd2d94e061d7af` | 0 | 80 | Retire branch: fully contained |
| `candidate/economy-a-1-baseline` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/economy-a-1-bounded-first` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/economy-a-2-baseline` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/economy-a-2-bounded-first` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/economy-b-1-baseline` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/economy-b-1-bounded-first` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/economy-b-2-baseline` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/economy-b-2-bounded-first` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/economy-c-1-baseline` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/economy-c-1-bounded-first` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/economy-d-1-baseline` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/economy-d-1-bounded-first` | `3fd4f33ddeee90d4894f2cd5f0929a8e6d9b19d6` | 0 | 90 | Retire branch: fully contained |
| `candidate/g6-orchestration` | `62a0a5ca7a56dc6452f0052ac593fc3c69924b9e` | 0 | 39 | Retire branch: fully contained |
| `candidate/giga-backstop` | `b052a4936f0ae1031499387552314ef962deb469` | 0 | 63 | Retire branch: fully contained |
| `candidate/giga-colleagues` | `f23368914f46f9edd2db935fb499d669c690b009` | 1 | 66 | Retain for integration |
| `candidate/giga-context-budget` | `8a0e959edb1d85515c4647305f7535a1bacf88c2` | 0 | 61 | Retire branch: fully contained |
| `candidate/giga-workflow-repair` | `397187116858ccf2df3d0ea0bdd1eda5928e798a` | 0 | 55 | Retire branch: fully contained |
| `candidate/history-headroom` | `72d7116086b543b99c8a5788b07b47d99ba9fe55` | 0 | 86 | Retire branch: fully contained |
| `candidate/live-provider-retries` | `87e06e671eac51dbf9af983ee31527f198414592` | 0 | 87 | Retire branch: fully contained |
| `candidate/protected-settlement-credit` | `e0dcbe2a9306a1ff57588ee37baecf738e2fce55` | 0 | 81 | Retire branch: fully contained |
| `candidate/session-successor-seed` | `10a8de49c18c15839073c7e52048f2e8259f8f95` | 0 | 85 | Retire branch: fully contained |
| `candidate/tui-chat-shell` | `76c401613bad81e510c23a118edd6891f220575f` | 1 | 63 | Archive unique commit; superseded |
| `candidate/useful-range-revision` | `d2b89d7e6c33e8656b241485712092621ebe2eba` | 0 | 89 | Retire branch: fully contained |
| `candidate/useful-work-tools` | `7cbc088a0bae3c2971d3caaf0098c2f2cf845d4c` | 1 | 92 | Archive unique commit; superseded |
| `giga/g3-live-lua-tools` | `370a90fba80ff3eeb3ae9273dcfbbb5f27d4294f` | 0 | 47 | Retire branch: fully contained |
| `giga/g4-modules-config` | `f961248d08babbb5f27a523e7d6094a79ec35078` | 0 | 33 | Retire branch: fully contained |
| `giga/g7-station` | `5e58d51d1df87df92016d69f4d3f7e1af001bae5` | 0 | 36 | Retire branch: fully contained |
| `giga/g8-packages-hud` | `d097f2e88c5dfa8d9f4955fdad09168c100136e9` | 0 | 24 | Retire branch: fully contained |
| `giga/g9-security-decision` | `4419a4e235146ea18030d20e19ef032129c21ee7` | 0 | 17 | Retire branch: fully contained |
| `master` | `ee8f1e38daabe89fcba33f5b07f7e6143b206a8b` | 0 | 0 | Keep: accepted source |
| `reconstruction/cpp-lua-2026-10-06` | `650db43ea80b00f6b04eb85b512fd97f6529c91d` | 0 | 65 | Retire branch: fully contained |

## Worktrees

Current worktrees still include the colleague slot at
context/giga-parallel/pool/slot-0 and the old station slot at
context/useful-work/pilot-pool/slot-0. Git metadata names the compatibility
arconaut path for these slots. Keep colleague work until integration; inspect
station-slot local state before any later worktree removal. Deleting a remote
branch does not remove these local worktrees.
