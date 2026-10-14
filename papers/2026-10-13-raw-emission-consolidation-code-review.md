# Shared emission core: one bounded code attack

Read the consolidation plan and its E1–E7 dispositions, then the complete
`include/blackbird/raw_caller.hpp` and `src/raw_caller.cpp` candidate, and the
actual `raw_emit`, `raw_fence` and capture-failure paths in `src/raw_runtime.cpp`.
This is the sole shared-core code attack, before adapter migration; no build or
helper-only test was added.

No concrete core source defect was established. `prepare` obtains the bank
without publishing; `begin` rejects absent/already-active credit and releases
credit on failed publication. A zero emitted ordinal is refused and fenced.
Finite acquisition marks its final rejection, while the underlying try probe
remains unmarked. `close` and `close_linked` release credit after the one terminal
attempt even when publication fails. Publication returns the native failure so
resource owners can finish actual cleanup before exposing it.

Direct publication retains its supplied span; linked publication accepts thirty
additional parts plus the two native parent-reference fields. Checked record
sizing includes E16 and rejects overflow before admission. Admission, sizing,
publication and terminal paths preserve native errno. The core does not rebind
an issuer or emit an invented destructor terminal. `raw_call_run` declares the
actual invocation result type and preserves values, lvalue references and rvalue
references without returning a reference to its local value result.

The File adapter must map the shared runner's `length` branch to its existing
`RawFileBranch::standard`; its `finish_filesystem` method handles the distinct
native filesystem-error integer. This is an adapter compatibility requirement,
not a new core defect. Program and Bridge migration retain their literal native
schemas, existing grants and genuine issuer lifetimes. Their actual native owner
fixtures, rather than a second helper-certification layer, check migration.

The same attack includes the subsequently declared narrow UI seams. `adopt`
checks an absent existing bank and nonzero actual request before moving held
credit, with no implicit binding or new acquisition. Exception dispatch checks
filesystem before system error; owners choose their real native integer layout.
The optional compile-time `FinishReturned=false` retains UI's existing open
successful holder. Returning the request by const reference preserves borrowed
File field lifetime. No additional concrete defect was established.

Same bounded review disposition includes explicit release after real outcome /
paid holder transfer (bank reset only, errno preserved) and move construction
needed for Bridge BM1. Move transfers the existing bank/request/end and resets
source optional/request; it does not emit, admit, bind or register. Assignment
is deleted so an active target cannot silently lose its owner terminal. UI's
intermediate caught exception adapter invokes the shared <false> runner with
literal UI fields while keeping the actual surrounding holder open. Existing
native fixtures verify those actual owner lifetimes, without a helper-only
second assurance layer.

BM1's same-unit transfer uses the declared RawEmission noexcept move constructor.
It transfers actual request/site/credit and explicitly resets the moved-from
optional and identity; move assignment is deleted so an active target cannot
silently discard its terminal. No new admission, publication or binding occurs.
This is included in the same core disposition, not another review layer.
