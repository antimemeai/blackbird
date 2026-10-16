# Native collection and inspector results

The component now controls and paints a checked SceneHost collection and inspector
without a domain call, terminal dependency, input decoder or independent item
store. The host owns immutable properties, keyed selection, anchor and focus.
The component owns viewport geometry and one bounded detail line index.

Publication checks inspector identity, target identity, notice/ink enums, terminal
safe UTF8 across style-span boundaries, and the existing shared count/text budgets.
Invalid candidates preserve the prior scene. Collection movement updates selected
key and first-visible anchor atomically. Same target/source-page refresh and
viewport reflow preserve the reading source anchor; a different target or
content_offset resets it. Quiet informational notices also render.

Wide rectangles show adjacent collection and detail panes; narrow rectangles show
the focused pane. All writes use the shared display map and actual one-cell
fallback footprint. A rectangle bisecting a foreign wide glyph refuses before
painting. Render never changes host/domain state; an allocation failure requires
the caller to abandon its uncommitted target frame.

## Behavioral red and green

The permanent fixture was written against API stubs before implementation. The
original Mac stub run failed five semantic/literal groups after its ordinary
link eventually completed. A premature launch while the binary did not yet exist
was preserved as setup failure and is not behavioral evidence.

The first Linux Release stub build, `context/linux/run-aw_9_nat/checks.log`, exposed
a fixture portability error: pair CTAD retained a function type under libstdc++13.
The corrected fixture uses explicit function-pointer pairs with unchanged
oracles. The same ordinary runner then built successfully and failed all six
behavioral groups in `context/linux/run-1hkn6z8e/checks.log`: unsafe publication,
navigation, literal split geometry, narrow pane routing, reading continuity and
foreign-wide-footprint clipping. Corrected stub source copies and hashes remain
under the ignored component context directory.

After implementation:

```sh
cmake --build build/debug --parallel 3 --target tui_inspector_test tui_components_test
ctest --test-dir build/debug -R '^(tui_components|tui_inspector)$' --output-on-failure
```

Both tests passed, 2/2, in 0.72 seconds. Build and test logs are
`context/tui-command-inspector-2026-10-16/component/green-build.log` and
`green.log`. The fixture retains literal cell coordinates rather than deriving
them from renderer measurement. It covers immutable candidate ownership, budget
and enum refusal, split UTF8 styles, reorder/removal, atomic position refusal,
wide/narrow/one-cell painting, reading continuity through refresh and resize,
changed-page reset and stale handles.

Root owns the single independent review, command adapter/input integration,
actual-main physical/readable checks, final Linux/profile qualification, matched
performance and installed source gate. Those outcomes are not claimed here.

The root's single independent component review read render, measurement,
navigation, typed publication validation and the permanent oracle. It found no
concrete defect: clipping refusal, immutable properties, atomic position and source
anchor behavior agreed with the written plan. That review required no source or
test changes. Integration and final qualification remain in the root lane.

## Installed analysis findings

The installed full gate later passed206/206 native tests and diagnosed swappable
scalar parameters, a repeated optional retrieval, and two fixture constructions.
Private wrapping helpers now use existing CellCount, row placement uses existing
Rect, and render binds selected once before guarding/dereferencing. The fixture
constructs an explicit invalid incarnation-zero target and exact invalid Ink
byte255 through C++20 bit_cast. The original publication-refusal oracles remain.
No diagnostic suppression, public API or expected behavior changed.

The owning two targets rebuilt successfully. The same two component cases passed
2/2 in0.89s; actual clang-tidy analysis of src/tui_inspector.cpp and
tests/tui_inspector_test.cpp both exited0 with no diagnostics. Exact logs remain
under `context/tui-command-inspector-2026-10-16/component/tidy-fix/`. Root owns the
subsequent full gate, sanitizer and final Linux/performance source qualification.
